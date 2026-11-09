// ==============================================================================
// Audio Processor Implementation
// ==============================================================================

#include "processor.h"
#include "plugin_ids.h"

#include "dsp/sweep_morph_link.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/note_value.h>

#include "display/shared_display_bridge.h"
#include "display/display_bridge_log.h"

#include <algorithm>  // for std::max, std::min
#include <cmath>      // for std::log10, std::pow
#include <cstring>    // for memcpy
#include <random>     // for instance ID generation

namespace Disrumpo {

// ==============================================================================
// Processor: lifecycle, audio process, bus, morph cache, solo/band helpers
// ==============================================================================


// ==============================================================================
// Constructor
// ==============================================================================

Processor::Processor() {
    // Set the controller class ID for host to create the correct controller
    // Constitution Principle I: Processor/Controller separation
    setControllerClass(kControllerUID);

    // Generate unique instance ID for SharedDisplayBridge
    std::random_device rd;
    std::mt19937_64 gen(rd());
    instanceId_ = gen();

    // Wire shared display pointers
    sharedDisplay_.inputFIFO = &sharedInputFIFO_;
    sharedDisplay_.outputFIFO = &sharedOutputFIFO_;
    sharedDisplay_.sampleRate = &sharedSampleRate_;

    // Morph cache defaults matching Controller defaults. Set once here, never
    // in setupProcessing, so a restored patch survives re-preparation (D-046).
    constexpr DistortionCommonParams kDefaultCommon{.drive = 1.0f, .mix = 1.0f, .toneHz = 4000.0f};
    for (auto& cache : bandMorphCache_) {
        cache.nodes[0] = MorphNode(0, 0.0f, 0.0f, DistortionType::SoftClip);
        cache.nodes[0].commonParams = kDefaultCommon;
        cache.nodes[1] = MorphNode(1, 1.0f, 0.0f, DistortionType::SoftClip);
        cache.nodes[1].commonParams = kDefaultCommon;
        cache.nodes[2] = MorphNode(2, 0.0f, 1.0f, DistortionType::SoftClip);
        cache.nodes[2].commonParams = kDefaultCommon;
        cache.nodes[3] = MorphNode(3, 1.0f, 1.0f, DistortionType::SoftClip);
        cache.nodes[3].commonParams = kDefaultCommon;
        cache.activeNodeCount = kDefaultActiveNodes;
    }
}

// ==============================================================================
// IPluginBase
// ==============================================================================

Steinberg::tresult PLUGIN_API Processor::initialize(FUnknown* context) {
    // Always call parent first
    Steinberg::tresult result = AudioEffect::initialize(context);
    if (result != Steinberg::kResultTrue) {
        return result;
    }

    // Add audio I/O buses
    // FR-009: Stereo input/output bus configuration
    addAudioInput(STR16("Audio Input"), Steinberg::Vst::SpeakerArr::kStereo);
    addAudioOutput(STR16("Audio Output"), Steinberg::Vst::SpeakerArr::kStereo);

    // MIDI CC reaches the plugin only through an event input: the host maps it to
    // a parameter with the controller's IMidiMapping and reports it to IMidiLearn (D-096)
    addEventInput(STR16("MIDI In"), 16);

    // Register in SharedDisplayBridge (Tier 3 fallback)
    Krate::Plugins::SharedDisplayBridge::instance().registerInstance(
        instanceId_, &sharedDisplay_);

    KRATE_BRIDGE_LOG("Disrumpo::Processor::initialize() — id=0x%llx",
        static_cast<unsigned long long>(instanceId_));

    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Processor::terminate() {
    KRATE_BRIDGE_LOG("Disrumpo::Processor::terminate() — id=0x%llx",
        static_cast<unsigned long long>(instanceId_));
    Krate::Plugins::SharedDisplayBridge::instance().unregisterInstance(instanceId_);
    return AudioEffect::terminate();
}

// ==============================================================================
// IAudioProcessor
// ==============================================================================

Steinberg::tresult PLUGIN_API Processor::setupProcessing(
    Steinberg::Vst::ProcessSetup& setup) {

    // Processing is stopped here: apply a load setState left pending (D-003)
    consumePendingState();

    // Store processing parameters
    // FR-011: Store sample rate for DSP calculations
    sampleRate_ = setup.sampleRate;
    spectrumSendIntervalSamples_ = 0; // recompute on next send

    // Constitution Principle II: Pre-allocate ALL buffers HERE

    // Initialize crossover networks for both channels (FR-001b)
    const int numBands = bandCount_.load(std::memory_order_relaxed);
    if (crossoverL_.isPrepared()) {
        // A band-count change still owed (its dip did not finish) redistributes
        // first; prepare() alone sets the count without redistributing (D-023)
        crossoverL_.setBandCount(numBands);
        crossoverR_.setBandCount(numBands);
    }
    crossoverL_.prepare(sampleRate_, numBands);
    crossoverR_.prepare(sampleRate_, numBands);

    // Band-count dip length; the crossovers now run the requested count (D-023)
    bandCountFadeSamples_ = std::max<size_t>(
        1, static_cast<size_t>(std::lround(kBandCountFadeMs * 0.001 * sampleRate_)));
    bandCountFade_ = BandCountFade::Idle;
    bandCountFadePos_ = 0;
    processedSinceActivate_ = false;

    // Global gain/mix smoothing (D-025)
    const auto srFloat = static_cast<float>(sampleRate_);
    inputGainSmoother_.configure(kGlobalSmoothingMs, srFloat);
    outputGainSmoother_.configure(kGlobalSmoothingMs, srFloat);
    wetMixSmoother_.configure(kGlobalSmoothingMs, srFloat);

    // Initialize band processors
    for (int i = 0; i < kMaxBands; ++i) {
        bandProcessors_[i].prepare(sampleRate_);
        bandProcessors_[i].setGainDb(bandStates_[i].gainDb);
        bandProcessors_[i].setPan(bandStates_[i].pan);
        bandProcessors_[i].setMute(bandStates_[i].mute);

        // Re-push the patch held in the morph cache: prepare() recreated the
        // MorphEngine, and setState may have run before the first prepare
        // (D-046). The cache is initialised once, in the constructor.
        const auto& cache = bandMorphCache_[i];
        bandProcessors_[i].setMorphEnabled(true);
        bandProcessors_[i].setMorphNodes(cache.nodes, cache.activeNodeCount);
        bandProcessors_[i].setMorphPosition(cache.morphX, cache.morphY);
        bandProcessors_[i].setMorphMode(cache.morphMode);
        bandProcessors_[i].setMorphSmoothingTime(cache.morphSmoothingMs);
    }

    // Initialize sweep processor (spec 007-sweep-system)
    sweepProcessor_.prepare(sampleRate_, setup.maxSamplesPerBlock);
    sweepProcessor_.setCustomCurve(&customCurve_);

    // Initialize sweep LFO and envelope (FR-024 to FR-027)
    sweepLFO_.prepare(sampleRate_);
    sweepEnvelope_.prepare(sampleRate_, setup.maxSamplesPerBlock);

    // Initialize modulation engine (spec 008-modulation-system)
    modulationEngine_.prepare(sampleRate_, setup.maxSamplesPerBlock);

    return AudioEffect::setupProcessing(setup);
}

Steinberg::tresult PLUGIN_API Processor::setActive(Steinberg::TBool state) {
    KRATE_BRIDGE_LOG("Disrumpo::Processor::setActive(%s)",
        state ? "true" : "false");
    // Processing is stopped on both edges: setState applies at once from now on
    // (until the end of an activation), and a load left pending is applied here (D-003)
    if (!state) {
        active_.store(false, std::memory_order_release);
    }
    consumePendingState();
    if (state) {
        // Activating: the crossovers take the requested band count at once (no
        // audio to dip, D-023), then reset processing state
        crossoverL_.setBandCount(bandCount_.load(std::memory_order_relaxed));
        crossoverR_.setBandCount(bandCount_.load(std::memory_order_relaxed));
        bandCountFade_ = BandCountFade::Idle;
        bandCountFadePos_ = 0;
        processedSinceActivate_ = false;
        crossoverL_.reset();
        crossoverR_.reset();
        for (auto& proc : bandProcessors_) {
            proc.reset();
        }
        // Reset sweep processor
        sweepProcessor_.reset();
        sweepPositionBuffer_.clear();
        samplePosition_ = 0;

        // Reset sweep LFO and envelope
        sweepLFO_.reset();
        sweepEnvelope_.reset();

        // Reset modulation sources only: routings and macros are user state (D-047)
        modulationEngine_.resetSources();

        // DataExchange: open queue for spectrum data transfer
        if (dataExchange_)
            dataExchange_->onActivate(processSetup);

        sendModOffsetsMessage();

        // process() may run from now on: setState hands its state over (D-003)
        active_.store(true, std::memory_order_release);
    } else {
        // DataExchange: close queue before deactivation
        if (dataExchange_)
            dataExchange_->onDeactivate();
    }

    return AudioEffect::setActive(state);
}

Steinberg::tresult PLUGIN_API Processor::process(Steinberg::Vst::ProcessData& data) {
    // ==========================================================================
    // Constitution Principle II: REAL-TIME SAFETY CRITICAL
    // - NO memory allocation (new, malloc, vector resize, etc.)
    // - NO locks or mutexes
    // - NO file I/O or system calls
    // - NO exceptions (throw/catch)
    // - This function MUST complete within the buffer duration
    // ==========================================================================

    // A state setState handed over (D-003), then this block's parameter changes
    consumePendingState();

    // Process parameter changes first
    if (data.inputParameterChanges) {
        processParameterChanges(data.inputParameterChanges);
    }

    // A band-count change (parameter or load) before any audio since activation
    // switches at once; later ones go through the output dip below (D-023)
    if (!processedSinceActivate_) {
        const int requested = bandCount_.load(std::memory_order_relaxed);
        if (requested != crossoverL_.getBandCount()) {
            switchBandCount(requested);
        }
    }

    // Check if we have audio to process
    if (data.numSamples == 0) {
        return Steinberg::kResultTrue;
    }

    // Verify we have valid stereo I/O
    if (data.numInputs == 0 || data.numOutputs == 0) {
        return Steinberg::kResultTrue;
    }

    if (data.inputs[0].numChannels < 2 || data.outputs[0].numChannels < 2) {
        return Steinberg::kResultTrue;
    }

    float* inputL = data.inputs[0].channelBuffers32[0];
    float* inputR = data.inputs[0].channelBuffers32[1];
    float* outputL = data.outputs[0].channelBuffers32[0];
    float* outputR = data.outputs[0].channelBuffers32[1];

    if (!inputL || !inputR || !outputL || !outputR) {
        return Steinberg::kResultTrue;
    }
    const bool firstAudioBlock = !processedSinceActivate_;
    processedSinceActivate_ = true;

    // ==========================================================================
    // Spectrum Analyzer: Prepare pre-distortion mono mixdown for DataExchange
    // ==========================================================================
    {
        const auto blockSamples = std::min(
            static_cast<uint32_t>(data.numSamples), kSpectrumBlockMaxSamples);
        for (uint32_t i = 0; i < blockSamples; ++i) {
            spectrumBlockBuffer_.inputSamples[i] =
                (inputL[i] + inputR[i]) * 0.5f;
        }
    }

    // ==========================================================================
    // Modulation Engine Processing (spec 008-modulation-system)
    // Runs FIRST so modulation offsets are available for sweep and band params.
    // ==========================================================================

    {
        Krate::DSP::BlockContext modCtx{};
        modCtx.sampleRate = sampleRate_;
        modCtx.blockSize = static_cast<size_t>(data.numSamples);

        // Extract tempo from process context if available
        if (data.processContext) {
            modCtx.tempoBPM = data.processContext->tempo;
            modCtx.isPlaying = (data.processContext->state & Steinberg::Vst::ProcessContext::kPlaying) != 0;
        }

        modulationEngine_.process(modCtx, inputL, inputR,
                                  static_cast<size_t>(data.numSamples));
    }

    // ==========================================================================
    // Apply Modulation Offsets (FR-063, FR-064)
    // Reads modulation engine offsets and applies to processor parameters.
    // Operates in normalized [0,1] space; denormalizes after application.
    // When no routing targets a destination, offset is 0 (base value unchanged).
    // ==========================================================================

    // The running topology; a requested change starts (or reverses) the dip (D-023)
    int numBands = crossoverL_.getBandCount();
    {
        const int requested = bandCount_.load(std::memory_order_relaxed);
        if (bandCountFade_ == BandCountFade::Idle && requested != numBands) {
            bandCountFade_ = BandCountFade::FadeOut;
            bandCountFadePos_ = 0;
        } else if (bandCountFade_ == BandCountFade::FadeOut && requested == numBands) {
            // Back to the running count: ramp up again from the current gain
            bandCountFade_ = BandCountFade::FadeIn;
            bandCountFadePos_ = bandCountFadeSamples_ - bandCountFadePos_;
        }
        // During FadeIn a new request is picked up when the ramp completes
    }

    // --- Global parameters ---
    const float modInputGain = modulationEngine_.getModulatedValue(
        ModDest::kInputGain, inputGain_.load(std::memory_order_relaxed));
    const float modOutputGain = modulationEngine_.getModulatedValue(
        ModDest::kOutputGain, outputGain_.load(std::memory_order_relaxed));
    const float modGlobalMix = modulationEngine_.getModulatedValue(
        ModDest::kGlobalMix, globalMix_.load(std::memory_order_relaxed));

    // --- Sweep parameters (modulation shifts the base, sweep LFO/env stack on top) ---
    float baseFreq = baseSweepFrequency_.load(std::memory_order_relaxed);
    {
        const float baseFreqNorm = normalizeSweepFrequency(baseFreq);
        const float modFreqNorm = modulationEngine_.getModulatedValue(
            ModDest::kSweepFrequency, baseFreqNorm);
        baseFreq = denormalizeSweepFrequency(modFreqNorm);
    }

    {
        const float baseWidthNorm = baseSweepWidthNorm_.load(std::memory_order_relaxed);
        const float modWidthNorm = modulationEngine_.getModulatedValue(
            ModDest::kSweepWidth, baseWidthNorm);
        constexpr float kMinWidth = 0.5f;
        constexpr float kMaxWidth = 4.0f;
        sweepProcessor_.setWidth(kMinWidth + modWidthNorm * (kMaxWidth - kMinWidth));
    }

    {
        const float baseIntNorm = baseSweepIntensityNorm_.load(std::memory_order_relaxed);
        const float modIntNorm = modulationEngine_.getModulatedValue(
            ModDest::kSweepIntensity, baseIntNorm);
        sweepProcessor_.setIntensity(modIntNorm * 2.0f);
    }

    // --- Per-band parameters (gain, pan, morphX/Y, drive/mix) ---
    for (int b = 0; b < numBands; ++b) {
        const auto bandIdx = static_cast<uint8_t>(b);

        // Band Gain: normalize to [0,1], apply offset, denormalize to dB
        const float baseGainNorm = (bandStates_[b].gainDb - kMinBandGainDb) /
                                   (kMaxBandGainDb - kMinBandGainDb);
        const float modGainNorm = modulationEngine_.getModulatedValue(
            ModDest::bandParam(bandIdx, ModDest::kBandGain), baseGainNorm);
        bandProcessors_[b].setGainDb(kMinBandGainDb + modGainNorm * (kMaxBandGainDb - kMinBandGainDb));

        // Band Pan: normalize [-1,+1] to [0,1], apply offset, denormalize back
        const float basePanNorm = (bandStates_[b].pan + 1.0f) * 0.5f;
        const float modPanNorm = modulationEngine_.getModulatedValue(
            ModDest::bandParam(bandIdx, ModDest::kBandPan), basePanNorm);
        bandProcessors_[b].setPan(modPanNorm * 2.0f - 1.0f);

        // Band MorphX/Y: set after the sweep below (the sweep link sets their base)

        // Band Drive/Mix/Tone/Bias: pass raw offsets to BandProcessor/MorphEngine
        // For morph path: MorphEngine applies per-sample after interpolation
        // For non-morph path: BandProcessor applies at block rate in processBlock()
        const float driveOffset = modulationEngine_.getModulationOffset(
            ModDest::bandParam(bandIdx, ModDest::kBandDrive));
        const float mixOffset = modulationEngine_.getModulationOffset(
            ModDest::bandParam(bandIdx, ModDest::kBandMix));
        const float toneOffset = modulationEngine_.getModulationOffset(
            ModDest::bandParam(bandIdx, ModDest::kBandTone));
        const float biasOffset = modulationEngine_.getModulationOffset(
            ModDest::bandParam(bandIdx, ModDest::kBandBias));
        bandProcessors_[b].setDriveMixModOffset(driveOffset, mixOffset);
        bandProcessors_[b].setToneBiasModOffset(toneOffset, biasOffset);
    }

    // ==========================================================================
    // Sweep Processing (spec 007-sweep-system)
    // FR-007: Process sweep smoother for the entire block
    // Sweep LFO/envelope modulate on top of the (possibly modulated) base freq.
    // ==========================================================================

    // Synced sweep LFO follows the host tempo (FR-024). A missing, non-finite or
    // non-positive tempo keeps the last valid one (120 BPM before any).
    if (data.processContext &&
        (data.processContext->state & Steinberg::Vst::ProcessContext::kTempoValid) != 0) {
        const auto hostTempo = static_cast<float>(data.processContext->tempo);
        if (Krate::DSP::detail::isFinite(hostTempo) && hostTempo > 0.0f) {
            sweepLFO_.setTempo(hostTempo);
        }
    }

    // Clamp to sweep frequency range (20Hz - 20kHz)
    constexpr float kMinSweepFreq = 20.0f;
    constexpr float kMaxSweepFreq = 20000.0f;
    float modulatedFreq = std::clamp(baseFreq, kMinSweepFreq, kMaxSweepFreq);

    if (sweepLFO_.isEnabled() || sweepEnvelope_.isEnabled()) {
        // The sweep LFO and envelope follower advance once per sample, and the
        // centre smoother follows their per-sample target. LFO: +/- 2 octaves at
        // full depth; envelope: 0 to +2 octaves; summed in log2 space.
        constexpr float kMaxOctaveShift = 2.0f;
        const bool lfoOn = sweepLFO_.isEnabled();
        const bool envOn = sweepEnvelope_.isEnabled();
        const float log2Base = std::log2(modulatedFreq);
        for (Steinberg::int32 n = 0; n < data.numSamples; ++n) {
            float octaveShift = 0.0f;
            if (lfoOn) {
                octaveShift += sweepLFO_.process() * kMaxOctaveShift;
            }
            if (envOn) {
                (void)sweepEnvelope_.processSample((inputL[n] + inputR[n]) * 0.5f);
                octaveShift += sweepEnvelope_.getModulationAmount() * kMaxOctaveShift;
            }
            modulatedFreq = std::exp2(std::clamp(log2Base + octaveShift, kLog2MinFreq, kLog2MaxFreq));
            sweepProcessor_.setCenterFrequency(modulatedFreq);
            sweepProcessor_.process();
        }
    } else {
        sweepProcessor_.setCenterFrequency(modulatedFreq);
        sweepProcessor_.processBlock(data.numSamples);
    }

    // Push sweep position data for UI synchronization (FR-046)
    if (sweepProcessor_.isEnabled()) {
        auto positionData = sweepProcessor_.getPositionData(samplePosition_);
        sweepPositionBuffer_.push(positionData);
    }

    // Write modulated frequency as output parameter for Controller visualization (FR-047, FR-049)
    if (data.outputParameterChanges) {
        Steinberg::int32 index = 0;
        auto* queue = data.outputParameterChanges->addParameterData(
            kSweepModulatedFrequencyOutputId, index);
        if (queue) {
            float normalizedFreq = normalizeSweepFrequency(modulatedFreq);
            queue->addPoint(0, static_cast<Steinberg::Vst::ParamValue>(normalizedFreq), index);
        }
    }

    // ==========================================================================
    // Sweep -> Morph Link (FR-032, FR-033, D-137) and Band MorphX/Y modulation
    // Once per block from the smoothed sweep centre (base, mod matrix, sweep LFO
    // and envelope): a linked axis takes the link curve as its base, None keeps
    // the manual position. The global Sweep Morph Link drives X of every band
    // whose own X link is None. Mod-matrix offsets go on top.
    // ==========================================================================
    {
        const float sweepNorm = normalizeSweepFrequency(sweepProcessor_.getSmoothedFrequency());
        const bool sweepNormValid = Krate::DSP::detail::isFinite(sweepNorm);
        const MorphLinkMode globalLink = sweepProcessor_.getMorphLinkMode();
        const auto linked = [&](MorphLinkMode mode, float manual) noexcept {
            if (mode == MorphLinkMode::None || !sweepNormValid) return manual;
            const float y = (mode == MorphLinkMode::Custom) ? customCurve_.evaluate(sweepNorm)
                                                            : applyMorphLinkCurve(mode, sweepNorm);
            return std::clamp(y, 0.0f, 1.0f);
        };
        for (int b = 0; b < numBands; ++b) {
            const auto bandIdx = static_cast<uint8_t>(b);
            const MorphLinkMode xLink =
                (bandStates_[b].xLink != MorphLinkMode::None) ? bandStates_[b].xLink : globalLink;
            const float baseX = linked(xLink, bandMorphCache_[b].morphX);
            const float baseY = linked(bandStates_[b].yLink, bandMorphCache_[b].morphY);
            const float modMorphX = modulationEngine_.getModulatedValue(
                ModDest::bandParam(bandIdx, ModDest::kBandMorphX), baseX);
            const float modMorphY = modulationEngine_.getModulatedValue(
                ModDest::bandParam(bandIdx, ModDest::kBandMorphY), baseY);
            bandProcessors_[b].setMorphPosition(modMorphX, modMorphY);
        }
    }

    // ==========================================================================
    // Per-Band Sweep Intensity (FR-SWEEP-001, D-019, D-037)
    // Once per block; each band smooths it and scales its shaper's drive with it.
    // A band's distance from the sweep centre is measured from the geometric
    // centre of its actual crossover edges (20 Hz / 20 kHz outside). The single
    // full-range band of 1-band mode always holds the sweep centre.
    // ==========================================================================

    if (sweepProcessor_.isEnabled()) {
        std::array<float, kMaxBands> sweepIntensities{};
        if (numBands <= 1) {
            sweepIntensities[0] = sweepProcessor_.getIntensity();
        } else {
            std::array<float, kMaxBands> bandCentres{};
            for (int b = 0; b < numBands; ++b) {
                const float lo = (b == 0) ? kMinCrossoverHz : crossoverL_.getCrossoverFrequency(b - 1);
                const float hi = (b == numBands - 1) ? kMaxCrossoverHz : crossoverL_.getCrossoverFrequency(b);
                bandCentres[b] = std::sqrt(lo * hi);  // crossovers are clamped to >= 20 Hz
            }
            sweepProcessor_.calculateAllBandIntensities(
                bandCentres.data(), numBands, sweepIntensities.data());
        }
        for (int b = 0; b < numBands; ++b) {
            bandProcessors_[b].setSweepIntensity(sweepIntensities[b]);
        }
    } else {
        // Sweep disabled: every band's drive as set
        for (int b = 0; b < numBands; ++b) {
            bandProcessors_[b].setSweepIntensity(0.0f);
        }
    }

    // ==========================================================================
    // Band Processing: per-sample crossover, per-band blocks, per-sample sum
    // ==========================================================================

    // Denormalize global gain/mix parameters
    // Input/Output Gain: normalized [0,1] → dB [-24,+24] → linear
    constexpr float kGainMinDb = -24.0f;
    constexpr float kGainRangeDb = 48.0f; // 24 - (-24)
    const float inputGainDb = kGainMinDb + modInputGain * kGainRangeDb;
    const float outputGainDb = kGainMinDb + modOutputGain * kGainRangeDb;
    // Block-rate targets, smoothed per sample below (D-025); the first audio block
    // after activation starts at them (no ramp from a stale value)
    const float inputGainTarget = Krate::DSP::dbToGain(inputGainDb);
    const float outputGainTarget = Krate::DSP::dbToGain(outputGainDb);
    // Mix: normalized [0,1] maps directly to the wet fraction
    const float wetMixTarget = modGlobalMix;
    if (firstAudioBlock) {
        inputGainSmoother_.snapTo(inputGainTarget);
        outputGainSmoother_.snapTo(outputGainTarget);
        wetMixSmoother_.snapTo(wetMixTarget);
    } else {
        inputGainSmoother_.setTarget(inputGainTarget);
        outputGainSmoother_.setTarget(outputGainTarget);
        wetMixSmoother_.setTarget(wetMixTarget);
    }

    // Solo/mute logic (FR-025, FR-025a): band states only change in
    // processParameterChanges, so this holds for the whole block
    std::array<bool, kMaxBands> contributes{};
    for (int b = 0; b < numBands; ++b) {
        contributes[b] = shouldBandContribute(b);
    }

    std::array<float, kMaxBands> bandsL{};
    std::array<float, kMaxBands> bandsR{};

    const auto totalSamples = static_cast<size_t>(data.numSamples);
    const float fadeInv = 1.0f / static_cast<float>(bandCountFadeSamples_);
    size_t chunkStart = 0;
    while (chunkStart < totalSamples) {
        size_t chunkLen = std::min(totalSamples - chunkStart, BandProcessor::kMaxBlockSize);
        // A band-count dip ramp ends on a chunk boundary, so the topology switches
        // exactly where the output gain reaches 0 (D-023)
        const bool fading = bandCountFade_ != BandCountFade::Idle;
        if (fading) {
            chunkLen = std::min(chunkLen, bandCountFadeSamples_ - bandCountFadePos_);
        }
        const bool fadeOut = bandCountFade_ == BandCountFade::FadeOut;
        const float* inL = inputL + chunkStart;
        const float* inR = inputR + chunkStart;

        // Split input through crossover networks into per-band block buffers
        // (FR-001b: independent L/R; input gain before the crossover)
        for (size_t n = 0; n < chunkLen; ++n) {
            const float inputGain = inputGainSmoother_.process();
            crossoverL_.process(inL[n] * inputGain, bandsL);
            crossoverR_.process(inR[n] * inputGain, bandsR);
            for (int b = 0; b < numBands; ++b) {
                bandBufL_[b][n] = bandsL[b];
                bandBufR_[b][n] = bandsR[b];
            }
        }

        // Each band as a block: oversampling, bypass, block-rate modulation
        // (processBlock brackets begin/endBlockModulation itself). Non-contributing
        // bands are processed too, to keep their smoothers running.
        for (int b = 0; b < numBands; ++b) {
            bandProcessors_[b].processBlock(bandBufL_[b].data(), bandBufR_[b].data(), chunkLen);
        }

        // Sum contributing bands (FR-013), dry/wet mix, then output gain on the
        // mix (D-025: post-mix, so it trims the dry path too).
        // The dry term runs through the crossovers' allpass chain so it has the
        // phase of the clean band sum: no notches at intermediate mix (D-018)
        float* outL = outputL + chunkStart;
        float* outR = outputR + chunkStart;
        for (size_t n = 0; n < chunkLen; ++n) {
            float sumL = 0.0f;
            float sumR = 0.0f;
            for (int b = 0; b < numBands; ++b) {
                if (contributes[b]) {
                    sumL += bandBufL_[b][n];
                    sumR += bandBufR_[b][n];
                }
            }
            const float wetMix = wetMixSmoother_.process();
            const float dryMix = 1.0f - wetMix;
            const float outputGain = outputGainSmoother_.process();
            outL[n] = (crossoverL_.processDryCompensation(inL[n]) * dryMix + sumL * wetMix) * outputGain;
            outR[n] = (crossoverR_.processDryCompensation(inR[n]) * dryMix + sumR * wetMix) * outputGain;
            if (fading) {
                ++bandCountFadePos_;
                const float ramp = static_cast<float>(bandCountFadePos_) * fadeInv;
                const float fadeGain = fadeOut ? 1.0f - ramp : ramp;
                outL[n] *= fadeGain;
                outR[n] *= fadeGain;
            }
        }

        chunkStart += chunkLen;
        if (fading && bandCountFadePos_ >= bandCountFadeSamples_) {
            advanceBandCountFade();
            numBands = crossoverL_.getBandCount();
            for (int b = 0; b < numBands; ++b) {
                contributes[b] = shouldBandContribute(b);
            }
        }
    }

    // ==========================================================================
    // Spectrum Analyzer: Send pre+post distortion samples via DataExchange
    // ==========================================================================
    sendSpectrumBlock(inputL, inputR, outputL, outputR, data.numSamples);

    // Update sample position for timing synchronization
    samplePosition_ += static_cast<uint64_t>(data.numSamples);

    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Processor::setBusArrangements(
    Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
    Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) {

    // FR-010: Accept stereo only, reject non-stereo arrangements
    if (numIns == 1 && numOuts == 1 &&
        inputs[0] == Steinberg::Vst::SpeakerArr::kStereo &&
        outputs[0] == Steinberg::Vst::SpeakerArr::kStereo) {
        return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
    }

    // Non-stereo arrangement: return kResultFalse
    // Host will fall back to the default stereo arrangement
    return Steinberg::kResultFalse;
}

// ==============================================================================
// Parameter Handling
// ==============================================================================

const std::array<float, kMaxMorphNodes>& Processor::getMorphWeightsForBand(int band) const {
    return bandProcessors_[band].getMorphWeights();
}
float Processor::getMorphCacheX(int band) const { return bandMorphCache_[band].morphX; }
float Processor::getMorphCacheY(int band) const { return bandMorphCache_[band].morphY; }
int Processor::getMorphCacheActiveNodes(int band) const { return bandMorphCache_[band].activeNodeCount; }

// ==============================================================================
// Solo/Mute Logic (FR-025, FR-025a)
// ==============================================================================

bool Processor::isAnySoloed() const noexcept {
    const int numBands = crossoverL_.getBandCount();  // the running topology (D-023)
    for (int b = 0; b < numBands; ++b) {
        if (bandStates_[b].solo) {
            return true;
        }
    }
    return false;
}

bool Processor::shouldBandContribute(int bandIndex) const noexcept {
    // FR-025a: Mute always takes priority
    if (bandStates_[bandIndex].mute) {
        return false;
    }

    // FR-025: If any band is soloed, only soloed bands contribute
    if (isAnySoloed()) {
        return bandStates_[bandIndex].solo;
    }

    // No solo active - all non-muted bands contribute
    return true;
}

// ==============================================================================
// Band-count change (D-023)
// ==============================================================================

void Processor::switchBandCount(int bands) noexcept {
    const int previous = crossoverL_.getBandCount();
    crossoverL_.setBandCount(bands);
    crossoverR_.setBandCount(bands);
    const int running = crossoverL_.getBandCount();
    for (int b = previous; b < running; ++b) {
        bandProcessors_[b].reset();
    }
}

void Processor::advanceBandCountFade() noexcept {
    bandCountFadePos_ = 0;
    const int requested = bandCount_.load(std::memory_order_relaxed);
    if (bandCountFade_ == BandCountFade::FadeOut) {
        if (requested != crossoverL_.getBandCount()) {
            switchBandCount(requested);
        }
        bandCountFade_ = BandCountFade::FadeIn;
    } else {
        bandCountFade_ = (requested != crossoverL_.getBandCount()) ? BandCountFade::FadeOut
                                                                   : BandCountFade::Idle;
    }
}
} // namespace Disrumpo
