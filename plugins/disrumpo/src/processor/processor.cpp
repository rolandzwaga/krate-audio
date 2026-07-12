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

    // Store processing parameters
    // FR-011: Store sample rate for DSP calculations
    sampleRate_ = setup.sampleRate;
    spectrumSendIntervalSamples_ = 0; // recompute on next send

    // Constitution Principle II: Pre-allocate ALL buffers HERE

    // Initialize crossover networks for both channels (FR-001b)
    const int numBands = bandCount_.load(std::memory_order_relaxed);
    crossoverL_.prepare(sampleRate_, numBands);
    crossoverR_.prepare(sampleRate_, numBands);

    // Initialize band processors
    for (int i = 0; i < kMaxBands; ++i) {
        bandProcessors_[i].prepare(sampleRate_);
        bandProcessors_[i].setGainDb(bandStates_[i].gainDb);
        bandProcessors_[i].setPan(bandStates_[i].pan);
        bandProcessors_[i].setMute(bandStates_[i].mute);

        // Initialize morph cache with defaults matching Controller defaults
        auto& cache = bandMorphCache_[i];
        constexpr DistortionCommonParams kDefaultCommon{.drive = 1.0f, .mix = 1.0f, .toneHz = 4000.0f};
        cache.nodes[0] = MorphNode(0, 0.0f, 0.0f, DistortionType::SoftClip);
        cache.nodes[0].commonParams = kDefaultCommon;
        cache.nodes[1] = MorphNode(1, 1.0f, 0.0f, DistortionType::SoftClip);
        cache.nodes[1].commonParams = kDefaultCommon;
        cache.nodes[2] = MorphNode(2, 0.0f, 1.0f, DistortionType::SoftClip);
        cache.nodes[2].commonParams = kDefaultCommon;
        cache.nodes[3] = MorphNode(3, 1.0f, 1.0f, DistortionType::SoftClip);
        cache.nodes[3].commonParams = kDefaultCommon;
        cache.activeNodeCount = kDefaultActiveNodes;

        // Enable morph mode and push initial nodes
        bandProcessors_[i].setMorphEnabled(true);
        bandProcessors_[i].setMorphNodes(cache.nodes, cache.activeNodeCount);
        bandProcessors_[i].setMorphPosition(cache.morphX, cache.morphY);
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
    if (state) {
        // Activating: reset processing state
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

        // Reset modulation engine
        modulationEngine_.reset();

        // DataExchange: open queue for spectrum data transfer
        if (dataExchange_)
            dataExchange_->onActivate(processSetup);

        sendModOffsetsMessage();
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

    // Process parameter changes first
    if (data.inputParameterChanges) {
        processParameterChanges(data.inputParameterChanges);
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

    const int numBands = bandCount_.load(std::memory_order_relaxed);

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

        // Band MorphX/Y: already [0,1] normalized, apply offset
        const float modMorphX = modulationEngine_.getModulatedValue(
            ModDest::bandParam(bandIdx, ModDest::kBandMorphX),
            bandMorphCache_[b].morphX);
        const float modMorphY = modulationEngine_.getModulatedValue(
            ModDest::bandParam(bandIdx, ModDest::kBandMorphY),
            bandMorphCache_[b].morphY);
        bandProcessors_[b].setMorphPosition(modMorphX, modMorphY);

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

    // Process envelope follower with input signal (average of L+R)
    if (sweepEnvelope_.isEnabled()) {
        float inputMono = (inputL[0] + inputR[0]) * 0.5f;
        (void)sweepEnvelope_.processSample(inputMono);
    }

    // Calculate modulated frequency: base (+ mod engine offset) + sweep LFO + envelope
    float modulatedFreq = baseFreq;

    // Get LFO modulation (bidirectional: +/- 2 octaves at full depth)
    if (sweepLFO_.isEnabled()) {
        float lfoValue = sweepLFO_.process();
        constexpr float kMaxOctaveShift = 2.0f;
        float octaveShift = lfoValue * kMaxOctaveShift;
        float log2Freq = std::log2(modulatedFreq) + octaveShift;
        modulatedFreq = std::pow(2.0f, log2Freq);
    }

    // Get envelope modulation (unidirectional: 0 to +2 octaves)
    if (sweepEnvelope_.isEnabled()) {
        modulatedFreq = sweepEnvelope_.getModulatedFrequency(modulatedFreq);
    }

    // Clamp to sweep frequency range (20Hz - 20kHz)
    constexpr float kMinSweepFreq = 20.0f;
    constexpr float kMaxSweepFreq = 20000.0f;
    modulatedFreq = std::clamp(modulatedFreq, kMinSweepFreq, kMaxSweepFreq);

    // Update sweep processor with modulated frequency
    sweepProcessor_.setCenterFrequency(modulatedFreq);

    sweepProcessor_.processBlock(data.numSamples);

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
    // MIDI Learn: Scan for CC events (FR-028, FR-029)
    // ==========================================================================
    if (midiLearnActive_ && data.inputEvents) {
        Steinberg::int32 eventCount = data.inputEvents->getEventCount();
        for (Steinberg::int32 ei = 0; ei < eventCount; ++ei) {
            Steinberg::Vst::Event e{};
            if (data.inputEvents->getEvent(ei, e) == Steinberg::kResultOk) {
                if (e.type == Steinberg::Vst::Event::kLegacyMIDICCOutEvent) {
                    uint8_t cc = e.midiCCOut.controlNumber;
                    // Write detected CC to output parameter
                    if (data.outputParameterChanges) {
                        Steinberg::int32 idx = 0;
                        auto* q = data.outputParameterChanges->addParameterData(
                            kSweepDetectedCCOutputId, idx);
                        if (q) {
                            q->addPoint(0, static_cast<double>(cc) / 127.0, idx);
                        }
                    }
                    midiLearnActive_ = false;
                    assignedMidiCC_ = cc;
                    break;  // Only capture first CC
                }
            }
        }
    }

    // ==========================================================================
    // Per-Band Sweep Intensity (spec 007-sweep-system FR-001, T067)
    // Calculate and apply sweep intensities to band processors once per block
    // ==========================================================================

    // Band center frequencies (log-spaced for 4-band Bark scale)
    static constexpr std::array<float, kMaxBands> kBandCenterFreqs = {
        100.0f, 600.0f, 3000.0f, 12000.0f
    };

    if (sweepProcessor_.isEnabled()) {
        // Calculate intensities for all active bands
        std::array<float, kMaxBands> sweepIntensities{};
        sweepProcessor_.calculateAllBandIntensities(
            kBandCenterFreqs.data(), numBands, sweepIntensities.data());

        // Apply sweep intensities to band processors
        for (int b = 0; b < numBands; ++b) {
            bandProcessors_[b].setSweepIntensity(sweepIntensities[b]);
        }
    } else {
        // Sweep disabled: set all bands to full intensity (1.0)
        for (int b = 0; b < numBands; ++b) {
            bandProcessors_[b].setSweepIntensity(1.0f);
        }
    }

    // ==========================================================================
    // Band Processing (FR-001a: sample-by-sample processing)
    // ==========================================================================

    // Denormalize global gain/mix parameters
    // Input/Output Gain: normalized [0,1] → dB [-24,+24] → linear
    constexpr float kGainMinDb = -24.0f;
    constexpr float kGainRangeDb = 48.0f; // 24 - (-24)
    const float inputGainDb = kGainMinDb + modInputGain * kGainRangeDb;
    const float outputGainDb = kGainMinDb + modOutputGain * kGainRangeDb;
    const float inputGainLinear = Krate::DSP::dbToGain(inputGainDb);
    const float outputGainLinear = Krate::DSP::dbToGain(outputGainDb);
    // Mix: normalized [0,1] maps directly to dry/wet fraction
    const float wetMix = modGlobalMix;
    const float dryMix = 1.0f - wetMix;

    std::array<float, kMaxBands> bandsL{};
    std::array<float, kMaxBands> bandsR{};

    // Apply block-rate drive/mix modulation to non-morph distortion adapters
    for (int b = 0; b < numBands; ++b) {
        bandProcessors_[b].beginBlockModulation();
    }

    for (Steinberg::int32 n = 0; n < data.numSamples; ++n) {
        // Apply input gain before crossover
        const float inL = inputL[n] * inputGainLinear;
        const float inR = inputR[n] * inputGainLinear;

        // Split input through crossover networks (FR-001b: independent L/R)
        crossoverL_.process(inL, bandsL);
        crossoverR_.process(inR, bandsR);

        // Initialize output accumulators
        float sumL = 0.0f;
        float sumR = 0.0f;

        // Process each band and sum (FR-013: sample-by-sample summation)
        for (int b = 0; b < numBands; ++b) {
            // Check solo/mute logic (FR-025, FR-025a)
            if (!shouldBandContribute(b)) {
                // Process to keep smoothers running, but don't add to output
                float discardL = bandsL[b];
                float discardR = bandsR[b];
                bandProcessors_[b].process(discardL, discardR);
                continue;
            }

            // Apply per-band processing (gain, pan, mute with smoothing)
            float bandL = bandsL[b];
            float bandR = bandsR[b];
            bandProcessors_[b].process(bandL, bandR);

            // Sum to output
            sumL += bandL;
            sumR += bandR;
        }

        // Apply output gain and dry/wet mix
        outputL[n] = inputL[n] * dryMix + sumL * outputGainLinear * wetMix;
        outputR[n] = inputR[n] * dryMix + sumR * outputGainLinear * wetMix;
    }

    // Restore base distortion params after per-sample processing
    for (int b = 0; b < numBands; ++b) {
        bandProcessors_[b].endBlockModulation();
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
    const int numBands = bandCount_.load(std::memory_order_relaxed);
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
} // namespace Disrumpo
