// ==============================================================================
// Audio Processor Implementation
// ==============================================================================

#include "processor.h"
#include "plugin_ids.h"

#include "dsp/sweep_morph_link.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"

#include <algorithm>  // for std::max, std::min
#include <cmath>      // for std::log10, std::pow
#include <cstring>    // for memcpy

namespace Disrumpo {

// ==============================================================================
// Constructor
// ==============================================================================

Processor::Processor() {
    // Set the controller class ID for host to create the correct controller
    // Constitution Principle I: Processor/Controller separation
    setControllerClass(kControllerUID);
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

    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Processor::terminate() {
    // Cleanup any resources allocated in initialize()
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

        // Reset spectrum FIFOs and send pointers to controller
        spectrumInputFIFO_.clear();
        spectrumOutputFIFO_.clear();
        sendSpectrumFIFOMessage();
        sendModOffsetsMessage();
    } else {
        // Deactivating: notify controller to disconnect FIFOs
        spectrumInputFIFO_.clear();
        spectrumOutputFIFO_.clear();
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
    // Spectrum Analyzer: Push pre-distortion input samples to FIFO
    // Mono mixdown (L+R)*0.5 for UI-thread FFT analysis
    // ==========================================================================
    {
        constexpr Steinberg::int32 kMonoChunkSize = 512;
        float monoChunk[kMonoChunkSize];
        for (Steinberg::int32 offset = 0; offset < data.numSamples; offset += kMonoChunkSize) {
            auto chunkLen = std::min(kMonoChunkSize, data.numSamples - offset);
            for (Steinberg::int32 i = 0; i < chunkLen; ++i) {
                monoChunk[i] = (inputL[offset + i] + inputR[offset + i]) * 0.5f;
            }
            spectrumInputFIFO_.push(monoChunk, static_cast<size_t>(chunkLen));
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

        // Band Drive/Mix: pass raw offsets to BandProcessor/MorphEngine
        // For morph path: MorphEngine applies per-sample after interpolation
        // For non-morph path: BandProcessor applies at block rate in processBlock()
        const float driveOffset = modulationEngine_.getModulationOffset(
            ModDest::bandParam(bandIdx, ModDest::kBandDrive));
        const float mixOffset = modulationEngine_.getModulationOffset(
            ModDest::bandParam(bandIdx, ModDest::kBandMix));
        bandProcessors_[b].setDriveMixModOffset(driveOffset, mixOffset);
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

    // Note: modInputGain, modOutputGain, modGlobalMix are computed above but
    // not applied here because the original processor doesn't apply global
    // gain/mix in the audio loop. They will take effect when global parameter
    // application is added. The modulation offsets are correctly computed
    // and available via modulationEngine_.getModulatedValue().
    (void)modInputGain;
    (void)modOutputGain;
    (void)modGlobalMix;

    std::array<float, kMaxBands> bandsL{};
    std::array<float, kMaxBands> bandsR{};

    // Apply block-rate drive/mix modulation to non-morph distortion adapters
    for (int b = 0; b < numBands; ++b) {
        bandProcessors_[b].beginBlockModulation();
    }

    for (Steinberg::int32 n = 0; n < data.numSamples; ++n) {
        // Split input through crossover networks (FR-001b: independent L/R)
        crossoverL_.process(inputL[n], bandsL);
        crossoverR_.process(inputR[n], bandsR);

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

        outputL[n] = sumL;
        outputR[n] = sumR;
    }

    // Restore base distortion params after per-sample processing
    for (int b = 0; b < numBands; ++b) {
        bandProcessors_[b].endBlockModulation();
    }

    // ==========================================================================
    // Spectrum Analyzer: Push post-distortion output samples to FIFO
    // ==========================================================================
    {
        constexpr Steinberg::int32 kMonoChunkSize = 512;
        float monoChunk[kMonoChunkSize];
        for (Steinberg::int32 offset = 0; offset < data.numSamples; offset += kMonoChunkSize) {
            auto chunkLen = std::min(kMonoChunkSize, data.numSamples - offset);
            for (Steinberg::int32 i = 0; i < chunkLen; ++i) {
                monoChunk[i] = (outputL[offset + i] + outputR[offset + i]) * 0.5f;
            }
            spectrumOutputFIFO_.push(monoChunk, static_cast<size_t>(chunkLen));
        }
    }

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
// IComponent - State Management
// ==============================================================================

Steinberg::tresult PLUGIN_API Processor::getState(Steinberg::IBStream* state) {
    // FR-018, FR-037: Serialize all parameters to IBStream
    // FR-020: Version field MUST be first for future migration

    Steinberg::IBStreamer streamer(state, kLittleEndian);

    // Write version first (MUST be first per FR-020)
    if (!streamer.writeInt32(kPresetVersion)) {
        return Steinberg::kResultFalse;
    }

    // Write global parameters in order (per data-model.md Section 3)
    if (!streamer.writeFloat(inputGain_.load(std::memory_order_relaxed))) {
        return Steinberg::kResultFalse;
    }
    if (!streamer.writeFloat(outputGain_.load(std::memory_order_relaxed))) {
        return Steinberg::kResultFalse;
    }
    if (!streamer.writeFloat(globalMix_.load(std::memory_order_relaxed))) {
        return Steinberg::kResultFalse;
    }

    // FR-037: Band management state (v2+)
    // Band count
    if (!streamer.writeInt32(bandCount_.load(std::memory_order_relaxed))) {
        return Steinberg::kResultFalse;
    }

    // Per-band state for all 8 bands (fixed for format stability)
    for (int b = 0; b < kMaxBands; ++b) {
        const auto& bs = bandStates_[b];
        if (!streamer.writeFloat(bs.gainDb)) return Steinberg::kResultFalse;
        if (!streamer.writeFloat(bs.pan)) return Steinberg::kResultFalse;
        if (!streamer.writeInt8(static_cast<Steinberg::int8>(bs.solo ? 1 : 0))) return Steinberg::kResultFalse;
        if (!streamer.writeInt8(static_cast<Steinberg::int8>(bs.bypass ? 1 : 0))) return Steinberg::kResultFalse;
        if (!streamer.writeInt8(static_cast<Steinberg::int8>(bs.mute ? 1 : 0))) return Steinberg::kResultFalse;
    }

    // Crossover frequencies (7 floats)
    for (int c = 0; c < kMaxBands - 1; ++c) {
        float freq = crossoverL_.getCrossoverFrequency(c);
        if (!streamer.writeFloat(freq)) return Steinberg::kResultFalse;
    }

    // =========================================================================
    // Sweep System State (v4+) — SC-012
    // =========================================================================

    // Sweep Core (6 values)
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(sweepProcessor_.isEnabled() ? 1 : 0)))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat(normalizeSweepFrequency(sweepProcessor_.getTargetFrequency())))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat((sweepProcessor_.getWidth() - 0.5f) / 3.5f))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat(sweepProcessor_.getIntensity() / 2.0f))
        return Steinberg::kResultFalse;
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(sweepProcessor_.getFalloffMode())))
        return Steinberg::kResultFalse;
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(sweepProcessor_.getMorphLinkMode())))
        return Steinberg::kResultFalse;

    // LFO (6 values)
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(sweepLFO_.isEnabled() ? 1 : 0)))
        return Steinberg::kResultFalse;
    // LFO Rate: denormalized Hz → normalized using inverse log formula
    {
        constexpr float kMinRateLog = -4.6052f;  // ln(0.01)
        constexpr float kMaxRateLog = 2.9957f;   // ln(20)
        float normalizedRate = (std::log(sweepLFO_.getRate()) - kMinRateLog) / (kMaxRateLog - kMinRateLog);
        normalizedRate = std::clamp(normalizedRate, 0.0f, 1.0f);
        if (!streamer.writeFloat(normalizedRate)) return Steinberg::kResultFalse;
    }
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(sweepLFO_.getWaveform())))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat(sweepLFO_.getDepth()))
        return Steinberg::kResultFalse;
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(sweepLFO_.isTempoSynced() ? 1 : 0)))
        return Steinberg::kResultFalse;
    {
        // Encode note value + modifier as single index: noteValueIndex * 3 + modifierIndex
        int noteIndex = static_cast<int>(sweepLFO_.getNoteValue()) * 3 +
                        static_cast<int>(sweepLFO_.getNoteModifier());
        if (!streamer.writeInt8(static_cast<Steinberg::int8>(noteIndex)))
            return Steinberg::kResultFalse;
    }

    // Envelope (4 values)
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(sweepEnvelope_.isEnabled() ? 1 : 0)))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat((sweepEnvelope_.getAttackTime() - kMinSweepEnvAttackMs) /
                             (kMaxSweepEnvAttackMs - kMinSweepEnvAttackMs)))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat((sweepEnvelope_.getReleaseTime() - kMinSweepEnvReleaseMs) /
                             (kMaxSweepEnvReleaseMs - kMinSweepEnvReleaseMs)))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat(sweepEnvelope_.getSensitivity()))
        return Steinberg::kResultFalse;

    // Custom Curve breakpoints
    {
        int32_t pointCount = static_cast<int32_t>(customCurve_.getBreakpointCount());
        if (!streamer.writeInt32(pointCount)) return Steinberg::kResultFalse;
        for (int32_t i = 0; i < pointCount; ++i) {
            auto bp = customCurve_.getBreakpoint(i);
            if (!streamer.writeFloat(bp.x)) return Steinberg::kResultFalse;
            if (!streamer.writeFloat(bp.y)) return Steinberg::kResultFalse;
        }
    }

    // =========================================================================
    // Modulation System State (v5+) — SC-010
    // =========================================================================

    // --- Source Parameters ---

    // LFO 1 (7 values: rate[float], shape[int8], phase[float], sync[int8],
    //         noteValue[int8], unipolar[int8], retrigger[int8])
    {
        constexpr float kMinLog = -4.6052f;  // ln(0.01)
        constexpr float kMaxLog = 2.9957f;   // ln(20)
        float rateNorm = (std::log(modulationEngine_.getLFO1Rate()) - kMinLog) / (kMaxLog - kMinLog);
        if (!streamer.writeFloat(std::clamp(rateNorm, 0.0f, 1.0f))) return Steinberg::kResultFalse;
    }
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(modulationEngine_.getLFO1Waveform())))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat(modulationEngine_.getLFO1PhaseOffset() / 360.0f))
        return Steinberg::kResultFalse;
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(modulationEngine_.getLFO1TempoSync() ? 1 : 0)))
        return Steinberg::kResultFalse;
    {
        int noteIdx = static_cast<int>(modulationEngine_.getLFO1NoteValue()) * 3 +
                      static_cast<int>(modulationEngine_.getLFO1NoteModifier());
        if (!streamer.writeInt8(static_cast<Steinberg::int8>(noteIdx)))
            return Steinberg::kResultFalse;
    }
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(modulationEngine_.getLFO1Unipolar() ? 1 : 0)))
        return Steinberg::kResultFalse;
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(modulationEngine_.getLFO1Retrigger() ? 1 : 0)))
        return Steinberg::kResultFalse;

    // LFO 2 (7 values: same layout as LFO 1)
    {
        constexpr float kMinLog = -4.6052f;
        constexpr float kMaxLog = 2.9957f;
        float rateNorm = (std::log(modulationEngine_.getLFO2Rate()) - kMinLog) / (kMaxLog - kMinLog);
        if (!streamer.writeFloat(std::clamp(rateNorm, 0.0f, 1.0f))) return Steinberg::kResultFalse;
    }
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(modulationEngine_.getLFO2Waveform())))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat(modulationEngine_.getLFO2PhaseOffset() / 360.0f))
        return Steinberg::kResultFalse;
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(modulationEngine_.getLFO2TempoSync() ? 1 : 0)))
        return Steinberg::kResultFalse;
    {
        int noteIdx = static_cast<int>(modulationEngine_.getLFO2NoteValue()) * 3 +
                      static_cast<int>(modulationEngine_.getLFO2NoteModifier());
        if (!streamer.writeInt8(static_cast<Steinberg::int8>(noteIdx)))
            return Steinberg::kResultFalse;
    }
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(modulationEngine_.getLFO2Unipolar() ? 1 : 0)))
        return Steinberg::kResultFalse;
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(modulationEngine_.getLFO2Retrigger() ? 1 : 0)))
        return Steinberg::kResultFalse;

    // Envelope Follower (4 values: attack[float], release[float], sensitivity[float], source[int8])
    if (!streamer.writeFloat((modulationEngine_.getEnvFollowerAttack() - 1.0f) / 99.0f))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat((modulationEngine_.getEnvFollowerRelease() - 10.0f) / 490.0f))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat(modulationEngine_.getEnvFollowerSensitivity()))
        return Steinberg::kResultFalse;
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(modulationEngine_.getEnvFollowerSource())))
        return Steinberg::kResultFalse;

    // Random (3 values: rate[float], smoothness[float], sync[int8])
    if (!streamer.writeFloat((modulationEngine_.getRandomRate() - 0.1f) / 49.9f))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat(modulationEngine_.getRandomSmoothness()))
        return Steinberg::kResultFalse;
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(modulationEngine_.getRandomTempoSync() ? 1 : 0)))
        return Steinberg::kResultFalse;

    // Chaos (3 values: model[int8], speed[float], coupling[float])
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(modulationEngine_.getChaosModel())))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat((modulationEngine_.getChaosSpeed() - 0.05f) / 19.95f))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat(modulationEngine_.getChaosCoupling()))
        return Steinberg::kResultFalse;

    // Sample & Hold (3 values: source[int8], rate[float], slew[float])
    if (!streamer.writeInt8(static_cast<Steinberg::int8>(modulationEngine_.getSampleHoldSource())))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat((modulationEngine_.getSampleHoldRate() - 0.1f) / 49.9f))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat(modulationEngine_.getSampleHoldSlew() / 500.0f))
        return Steinberg::kResultFalse;

    // Pitch Follower (4 values: minHz[float], maxHz[float], confidence[float], trackingSpeed[float])
    if (!streamer.writeFloat((modulationEngine_.getPitchFollowerMinHz() - 20.0f) / 480.0f))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat((modulationEngine_.getPitchFollowerMaxHz() - 200.0f) / 4800.0f))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat(modulationEngine_.getPitchFollowerConfidence()))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat((modulationEngine_.getPitchFollowerTrackingSpeed() - 10.0f) / 290.0f))
        return Steinberg::kResultFalse;

    // Transient (3 values: sensitivity[float], attack[float], decay[float])
    if (!streamer.writeFloat(modulationEngine_.getTransientSensitivity()))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat((modulationEngine_.getTransientAttack() - 0.5f) / 9.5f))
        return Steinberg::kResultFalse;
    if (!streamer.writeFloat((modulationEngine_.getTransientDecay() - 20.0f) / 180.0f))
        return Steinberg::kResultFalse;

    // Macros (4 × 4 = 16 values: value[float], min[float], max[float], curve[int8])
    for (size_t m = 0; m < Krate::DSP::kMaxMacros; ++m) {
        const auto& macro = modulationEngine_.getMacro(m);
        if (!streamer.writeFloat(macro.value)) return Steinberg::kResultFalse;
        if (!streamer.writeFloat(macro.minOutput)) return Steinberg::kResultFalse;
        if (!streamer.writeFloat(macro.maxOutput)) return Steinberg::kResultFalse;
        if (!streamer.writeInt8(static_cast<Steinberg::int8>(macro.curve)))
            return Steinberg::kResultFalse;
    }

    // --- Routing Parameters (32 × 4 values: source[int8], dest[int32], amount[float], curve[int8]) ---
    for (size_t r = 0; r < Krate::DSP::kMaxModRoutings; ++r) {
        const auto& routing = modulationEngine_.getRouting(r);
        if (!streamer.writeInt8(static_cast<Steinberg::int8>(routing.source)))
            return Steinberg::kResultFalse;
        if (!streamer.writeInt32(static_cast<int32_t>(routing.destParamId)))
            return Steinberg::kResultFalse;
        if (!streamer.writeFloat(routing.amount))
            return Steinberg::kResultFalse;
        if (!streamer.writeInt8(static_cast<Steinberg::int8>(routing.curve)))
            return Steinberg::kResultFalse;
    }

    // =========================================================================
    // Morph Node State (v6+)
    // =========================================================================
    for (int b = 0; b < kMaxBands; ++b) {
        const auto& cache = bandMorphCache_[b];

        // Band morph position & config (3 floats + 2 int8)
        if (!streamer.writeFloat(cache.morphX)) return Steinberg::kResultFalse;
        if (!streamer.writeFloat(cache.morphY)) return Steinberg::kResultFalse;
        if (!streamer.writeInt8(static_cast<Steinberg::int8>(0)))  // morphMode
            return Steinberg::kResultFalse;
        if (!streamer.writeInt8(static_cast<Steinberg::int8>(cache.activeNodeCount)))
            return Steinberg::kResultFalse;
        if (!streamer.writeFloat(0.0f))  // morphSmoothing (ms)
            return Steinberg::kResultFalse;

        // Per-node state (4 nodes × 7 values each)
        for (int n = 0; n < kMaxMorphNodes; ++n) {
            const auto& mn = cache.nodes[static_cast<size_t>(n)];
            if (!streamer.writeInt8(static_cast<Steinberg::int8>(mn.type)))
                return Steinberg::kResultFalse;
            if (!streamer.writeFloat(mn.commonParams.drive)) return Steinberg::kResultFalse;
            if (!streamer.writeFloat(mn.commonParams.mix)) return Steinberg::kResultFalse;
            if (!streamer.writeFloat(mn.commonParams.toneHz)) return Steinberg::kResultFalse;
            if (!streamer.writeFloat(mn.params.bias)) return Steinberg::kResultFalse;
            if (!streamer.writeFloat(mn.params.folds)) return Steinberg::kResultFalse;
            if (!streamer.writeFloat(mn.params.bitDepth)) return Steinberg::kResultFalse;
        }
    }

    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Processor::setState(Steinberg::IBStream* state) {
    // FR-019, FR-038: Deserialize parameters from IBStream
    // FR-021: Handle corrupted/invalid data gracefully

    Steinberg::IBStreamer streamer(state, kLittleEndian);

    // Read version first
    int32_t version = 0;
    if (!streamer.readInt32(version)) {
        // Corrupted state: return kResultFalse, plugin uses defaults
        return Steinberg::kResultFalse;
    }

    // FR-021: Version handling
    if (version < 1) {
        // Invalid version: corrupted data
        return Steinberg::kResultFalse;
    }

    if (version > kPresetVersion) {
        // Future version: read what we understand, skip unknown
    }

    // Read global parameters (v1+)
    float inputGain = 0.5f;
    float outputGain = 0.5f;
    float globalMix = 1.0f;

    if (!streamer.readFloat(inputGain)) {
        return Steinberg::kResultFalse;
    }
    if (!streamer.readFloat(outputGain)) {
        return Steinberg::kResultFalse;
    }
    if (!streamer.readFloat(globalMix)) {
        return Steinberg::kResultFalse;
    }

    // Apply global parameters
    inputGain_.store(inputGain, std::memory_order_relaxed);
    outputGain_.store(outputGain, std::memory_order_relaxed);
    globalMix_.store(globalMix, std::memory_order_relaxed);

    // FR-038: Band management state (v2+)
    if (version >= 2) {
        // Read band count
        int32_t bandCount = kDefaultBands;
        if (!streamer.readInt32(bandCount)) {
            // Use defaults if read fails
            return Steinberg::kResultOk;
        }
        bandCount = std::clamp(bandCount, kMinBands, 4);
        bandCount_.store(bandCount, std::memory_order_relaxed);

        // Read per-band state
        // v7 and earlier wrote 8 bands; v8+ writes 4 bands
        constexpr int kV7MaxBands = 8;
        const int streamBands = (version <= 7) ? kV7MaxBands : kMaxBands;
        for (int b = 0; b < streamBands; ++b) {
            float gainDb = 0.0f;
            float pan = 0.0f;
            Steinberg::int8 soloVal = 0;
            Steinberg::int8 bypassVal = 0;
            Steinberg::int8 muteVal = 0;

            if (!streamer.readFloat(gainDb)) gainDb = 0.0f;
            if (!streamer.readFloat(pan)) pan = 0.0f;
            if (!streamer.readInt8(soloVal)) soloVal = 0;
            if (!streamer.readInt8(bypassVal)) bypassVal = 0;
            if (!streamer.readInt8(muteVal)) muteVal = 0;

            if (b < kMaxBands) {
                auto& bs = bandStates_[b];
                bs.gainDb = std::clamp(gainDb, kMinBandGainDb, kMaxBandGainDb);
                bs.pan = std::clamp(pan, -1.0f, 1.0f);
                bs.solo = (soloVal != 0);
                bs.bypass = (bypassVal != 0);
                bs.mute = (muteVal != 0);

                bandProcessors_[b].setGainDb(bs.gainDb);
                bandProcessors_[b].setPan(bs.pan);
                bandProcessors_[b].setMute(bs.mute);
            }
            // else: discard data from bands 4-7 (v7 migration)
        }

        // Read crossover frequencies
        // v7 and earlier wrote 7 crossovers; v8+ writes 3
        const int streamCrossovers = (version <= 7) ? 7 : (kMaxBands - 1);
        for (int c = 0; c < streamCrossovers; ++c) {
            float freq = 1000.0f;  // Default
            if (!streamer.readFloat(freq)) break;

            if (c < kMaxBands - 1) {
                crossoverL_.setCrossoverFrequency(c, freq);
                crossoverR_.setCrossoverFrequency(c, freq);
            }
            // else: discard crossovers 3-6 (v7 migration)
        }

        // Update band counts in crossover networks
        crossoverL_.setBandCount(bandCount);
        crossoverR_.setBandCount(bandCount);
    }

    // =========================================================================
    // Sweep System State (v4+) — SC-012
    // =========================================================================
    if (version >= 4) {
        // Sweep Core
        Steinberg::int8 sweepEnable = 0;
        float sweepFreqNorm = 0.566f;
        float sweepWidthNorm = 0.286f;
        float sweepIntensityNorm = 0.25f;
        Steinberg::int8 sweepFalloff = 1;
        Steinberg::int8 sweepMorphLink = 0;

        if (streamer.readInt8(sweepEnable))
            sweepProcessor_.setEnabled(sweepEnable != 0);
        if (streamer.readFloat(sweepFreqNorm)) {
            float freqHz = denormalizeSweepFrequency(sweepFreqNorm);
            baseSweepFrequency_.store(freqHz, std::memory_order_relaxed);
            sweepProcessor_.setCenterFrequency(freqHz);
        }
        if (streamer.readFloat(sweepWidthNorm))
            sweepProcessor_.setWidth(0.5f + sweepWidthNorm * 3.5f);
        if (streamer.readFloat(sweepIntensityNorm))
            sweepProcessor_.setIntensity(sweepIntensityNorm * 2.0f);
        if (streamer.readInt8(sweepFalloff))
            sweepProcessor_.setFalloffMode(static_cast<SweepFalloff>(sweepFalloff));
        if (streamer.readInt8(sweepMorphLink))
            sweepProcessor_.setMorphLinkMode(static_cast<MorphLinkMode>(
                std::clamp(static_cast<int>(sweepMorphLink), 0, kMorphLinkModeCount - 1)));

        // LFO
        Steinberg::int8 lfoEnable = 0;
        float lfoRateNorm = 0.606f;
        Steinberg::int8 lfoWaveform = 0;
        float lfoDepth = 0.0f;
        Steinberg::int8 lfoSync = 0;
        Steinberg::int8 lfoNoteIndex = 0;

        if (streamer.readInt8(lfoEnable))
            sweepLFO_.setEnabled(lfoEnable != 0);
        if (streamer.readFloat(lfoRateNorm)) {
            constexpr float kMinRateLog = -4.6052f;
            constexpr float kMaxRateLog = 2.9957f;
            float rateHz = std::exp(kMinRateLog + lfoRateNorm * (kMaxRateLog - kMinRateLog));
            sweepLFO_.setRate(rateHz);
        }
        if (streamer.readInt8(lfoWaveform))
            sweepLFO_.setWaveform(static_cast<Krate::DSP::Waveform>(
                std::clamp(static_cast<int>(lfoWaveform), 0, 5)));
        if (streamer.readFloat(lfoDepth))
            sweepLFO_.setDepth(lfoDepth);
        if (streamer.readInt8(lfoSync))
            sweepLFO_.setTempoSync(lfoSync != 0);
        if (streamer.readInt8(lfoNoteIndex)) {
            int idx = std::clamp(static_cast<int>(lfoNoteIndex), 0, 14);
            sweepLFO_.setNoteValue(
                static_cast<Krate::DSP::NoteValue>(idx / 3),
                static_cast<Krate::DSP::NoteModifier>(idx % 3));
        }

        // Envelope
        Steinberg::int8 envEnable = 0;
        float envAttackNorm = 0.091f;
        float envReleaseNorm = 0.184f;
        float envSensitivity = 0.5f;

        if (streamer.readInt8(envEnable))
            sweepEnvelope_.setEnabled(envEnable != 0);
        if (streamer.readFloat(envAttackNorm))
            sweepEnvelope_.setAttackTime(kMinSweepEnvAttackMs +
                envAttackNorm * (kMaxSweepEnvAttackMs - kMinSweepEnvAttackMs));
        if (streamer.readFloat(envReleaseNorm))
            sweepEnvelope_.setReleaseTime(kMinSweepEnvReleaseMs +
                envReleaseNorm * (kMaxSweepEnvReleaseMs - kMinSweepEnvReleaseMs));
        if (streamer.readFloat(envSensitivity))
            sweepEnvelope_.setSensitivity(envSensitivity);

        // Custom Curve
        int32_t pointCount = 2;
        if (streamer.readInt32(pointCount)) {
            pointCount = std::clamp(pointCount, 2, 8);
            // Clear and rebuild custom curve
            while (customCurve_.getBreakpointCount() > 2) {
                customCurve_.removeBreakpoint(1);
            }
            // Read first point (endpoint x=0)
            float px = 0.0f;
            float py = 0.0f;
            if (pointCount >= 1 && streamer.readFloat(px) && streamer.readFloat(py)) {
                customCurve_.setBreakpoint(0, 0.0f, py);
            }
            // Read intermediate points
            for (int32_t i = 1; i < pointCount - 1; ++i) {
                if (streamer.readFloat(px) && streamer.readFloat(py)) {
                    customCurve_.addBreakpoint(px, py);
                }
            }
            // Read last point (endpoint x=1)
            if (pointCount >= 2 && streamer.readFloat(px) && streamer.readFloat(py)) {
                customCurve_.setBreakpoint(customCurve_.getBreakpointCount() - 1, 1.0f, py);
            }
        }
    }

    // =========================================================================
    // Modulation System State (v5+) — SC-010
    // =========================================================================
    if (version >= 5) {
        // --- Source Parameters ---

        // LFO 1 (7 values)
        float lfo1RateNorm = 0.5f;
        if (streamer.readFloat(lfo1RateNorm)) {
            constexpr float kMinLog = -4.6052f;
            constexpr float kMaxLog = 2.9957f;
            float rateHz = std::exp(kMinLog + lfo1RateNorm * (kMaxLog - kMinLog));
            modulationEngine_.setLFO1Rate(rateHz);
        }
        Steinberg::int8 lfo1Shape = 0;
        if (streamer.readInt8(lfo1Shape))
            modulationEngine_.setLFO1Waveform(static_cast<Krate::DSP::Waveform>(
                std::clamp(static_cast<int>(lfo1Shape), 0, 5)));
        float lfo1Phase = 0.0f;
        if (streamer.readFloat(lfo1Phase))
            modulationEngine_.setLFO1PhaseOffset(lfo1Phase * 360.0f);
        Steinberg::int8 lfo1Sync = 0;
        if (streamer.readInt8(lfo1Sync))
            modulationEngine_.setLFO1TempoSync(lfo1Sync != 0);
        Steinberg::int8 lfo1NoteIdx = 0;
        if (streamer.readInt8(lfo1NoteIdx)) {
            int idx = std::clamp(static_cast<int>(lfo1NoteIdx), 0, 14);
            modulationEngine_.setLFO1NoteValue(
                static_cast<Krate::DSP::NoteValue>(idx / 3),
                static_cast<Krate::DSP::NoteModifier>(idx % 3));
        }
        Steinberg::int8 lfo1Unipolar = 0;
        if (streamer.readInt8(lfo1Unipolar))
            modulationEngine_.setLFO1Unipolar(lfo1Unipolar != 0);
        Steinberg::int8 lfo1Retrigger = 1;
        if (streamer.readInt8(lfo1Retrigger))
            modulationEngine_.setLFO1Retrigger(lfo1Retrigger != 0);

        // LFO 2 (7 values)
        float lfo2RateNorm = 0.5f;
        if (streamer.readFloat(lfo2RateNorm)) {
            constexpr float kMinLog = -4.6052f;
            constexpr float kMaxLog = 2.9957f;
            float rateHz = std::exp(kMinLog + lfo2RateNorm * (kMaxLog - kMinLog));
            modulationEngine_.setLFO2Rate(rateHz);
        }
        Steinberg::int8 lfo2Shape = 0;
        if (streamer.readInt8(lfo2Shape))
            modulationEngine_.setLFO2Waveform(static_cast<Krate::DSP::Waveform>(
                std::clamp(static_cast<int>(lfo2Shape), 0, 5)));
        float lfo2Phase = 0.0f;
        if (streamer.readFloat(lfo2Phase))
            modulationEngine_.setLFO2PhaseOffset(lfo2Phase * 360.0f);
        Steinberg::int8 lfo2Sync = 0;
        if (streamer.readInt8(lfo2Sync))
            modulationEngine_.setLFO2TempoSync(lfo2Sync != 0);
        Steinberg::int8 lfo2NoteIdx = 0;
        if (streamer.readInt8(lfo2NoteIdx)) {
            int idx = std::clamp(static_cast<int>(lfo2NoteIdx), 0, 14);
            modulationEngine_.setLFO2NoteValue(
                static_cast<Krate::DSP::NoteValue>(idx / 3),
                static_cast<Krate::DSP::NoteModifier>(idx % 3));
        }
        Steinberg::int8 lfo2Unipolar = 0;
        if (streamer.readInt8(lfo2Unipolar))
            modulationEngine_.setLFO2Unipolar(lfo2Unipolar != 0);
        Steinberg::int8 lfo2Retrigger = 1;
        if (streamer.readInt8(lfo2Retrigger))
            modulationEngine_.setLFO2Retrigger(lfo2Retrigger != 0);

        // Envelope Follower (4 values)
        float envAttackNorm = 0.0f;
        if (streamer.readFloat(envAttackNorm))
            modulationEngine_.setEnvFollowerAttack(1.0f + envAttackNorm * 99.0f);
        float envReleaseNorm = 0.0f;
        if (streamer.readFloat(envReleaseNorm))
            modulationEngine_.setEnvFollowerRelease(10.0f + envReleaseNorm * 490.0f);
        float envSensitivity = 0.5f;
        if (streamer.readFloat(envSensitivity))
            modulationEngine_.setEnvFollowerSensitivity(envSensitivity);
        Steinberg::int8 envSource = 0;
        if (streamer.readInt8(envSource))
            modulationEngine_.setEnvFollowerSource(static_cast<Krate::DSP::EnvFollowerSourceType>(
                std::clamp(static_cast<int>(envSource), 0, 4)));

        // Random (3 values)
        float randomRateNorm = 0.0f;
        if (streamer.readFloat(randomRateNorm))
            modulationEngine_.setRandomRate(0.1f + randomRateNorm * 49.9f);
        float randomSmoothness = 0.0f;
        if (streamer.readFloat(randomSmoothness))
            modulationEngine_.setRandomSmoothness(randomSmoothness);
        Steinberg::int8 randomSync = 0;
        if (streamer.readInt8(randomSync))
            modulationEngine_.setRandomTempoSync(randomSync != 0);

        // Chaos (3 values)
        Steinberg::int8 chaosModel = 0;
        if (streamer.readInt8(chaosModel))
            modulationEngine_.setChaosModel(static_cast<Krate::DSP::ChaosModel>(
                std::clamp(static_cast<int>(chaosModel), 0, 3)));
        float chaosSpeedNorm = 0.0f;
        if (streamer.readFloat(chaosSpeedNorm))
            modulationEngine_.setChaosSpeed(0.05f + chaosSpeedNorm * 19.95f);
        float chaosCoupling = 0.0f;
        if (streamer.readFloat(chaosCoupling))
            modulationEngine_.setChaosCoupling(chaosCoupling);

        // Sample & Hold (3 values)
        Steinberg::int8 shSource = 0;
        if (streamer.readInt8(shSource))
            modulationEngine_.setSampleHoldSource(static_cast<Krate::DSP::SampleHoldInputType>(
                std::clamp(static_cast<int>(shSource), 0, 3)));
        float shRateNorm = 0.0f;
        if (streamer.readFloat(shRateNorm))
            modulationEngine_.setSampleHoldRate(0.1f + shRateNorm * 49.9f);
        float shSlewNorm = 0.0f;
        if (streamer.readFloat(shSlewNorm))
            modulationEngine_.setSampleHoldSlew(shSlewNorm * 500.0f);

        // Pitch Follower (4 values)
        float pitchMinNorm = 0.0f;
        if (streamer.readFloat(pitchMinNorm))
            modulationEngine_.setPitchFollowerMinHz(20.0f + pitchMinNorm * 480.0f);
        float pitchMaxNorm = 0.0f;
        if (streamer.readFloat(pitchMaxNorm))
            modulationEngine_.setPitchFollowerMaxHz(200.0f + pitchMaxNorm * 4800.0f);
        float pitchConfidence = 0.5f;
        if (streamer.readFloat(pitchConfidence))
            modulationEngine_.setPitchFollowerConfidence(pitchConfidence);
        float pitchTrackNorm = 0.0f;
        if (streamer.readFloat(pitchTrackNorm))
            modulationEngine_.setPitchFollowerTrackingSpeed(10.0f + pitchTrackNorm * 290.0f);

        // Transient (3 values)
        float transSensitivity = 0.5f;
        if (streamer.readFloat(transSensitivity))
            modulationEngine_.setTransientSensitivity(transSensitivity);
        float transAttackNorm = 0.0f;
        if (streamer.readFloat(transAttackNorm))
            modulationEngine_.setTransientAttack(0.5f + transAttackNorm * 9.5f);
        float transDecayNorm = 0.0f;
        if (streamer.readFloat(transDecayNorm))
            modulationEngine_.setTransientDecay(20.0f + transDecayNorm * 180.0f);

        // Macros (4 × 4 = 16 values)
        for (size_t m = 0; m < Krate::DSP::kMaxMacros; ++m) {
            float macroValue = 0.0f;
            if (streamer.readFloat(macroValue))
                modulationEngine_.setMacroValue(m, macroValue);
            float macroMin = 0.0f;
            if (streamer.readFloat(macroMin))
                modulationEngine_.setMacroMin(m, macroMin);
            float macroMax = 1.0f;
            if (streamer.readFloat(macroMax))
                modulationEngine_.setMacroMax(m, macroMax);
            Steinberg::int8 macroCurve = 0;
            if (streamer.readInt8(macroCurve))
                modulationEngine_.setMacroCurve(m, static_cast<Krate::DSP::ModCurve>(
                    std::clamp(static_cast<int>(macroCurve), 0, 3)));
        }

        // --- Routing Parameters (32 × 4 values) ---
        for (size_t r = 0; r < Krate::DSP::kMaxModRoutings; ++r) {
            Krate::DSP::ModRouting routing{};
            Steinberg::int8 source = 0;
            if (streamer.readInt8(source))
                routing.source = static_cast<Krate::DSP::ModSource>(
                    std::clamp(static_cast<int>(source), 0, 12));
            int32_t dest = 0;
            if (streamer.readInt32(dest))
                routing.destParamId = static_cast<uint32_t>(
                    std::clamp(dest, 0, static_cast<int32_t>(ModDest::kTotalDestinations - 1)));
            if (!streamer.readFloat(routing.amount))
                routing.amount = 0.0f;
            Steinberg::int8 curve = 0;
            if (streamer.readInt8(curve))
                routing.curve = static_cast<Krate::DSP::ModCurve>(
                    std::clamp(static_cast<int>(curve), 0, 3));
            routing.active = (routing.source != Krate::DSP::ModSource::None);
            modulationEngine_.setRouting(r, routing);
        }
    }

    // =========================================================================
    // Morph Node State (v6+)
    // =========================================================================
    if (version >= 6) {
        // v7 and earlier wrote 8 bands of morph state; v8+ writes 4
        constexpr int kV7MorphBands = 8;
        const int streamMorphBands = (version <= 7) ? kV7MorphBands : kMaxBands;
        for (int b = 0; b < streamMorphBands; ++b) {
            // Read band morph position & config (always read to advance stream)
            float morphX = 0.5f;
            float morphY = 0.5f;
            Steinberg::int8 morphMode = 0;
            auto activeNodes = static_cast<Steinberg::int8>(kDefaultActiveNodes);
            float morphSmoothing = 0.0f;

            streamer.readFloat(morphX);
            streamer.readFloat(morphY);
            streamer.readInt8(morphMode);
            streamer.readInt8(activeNodes);
            streamer.readFloat(morphSmoothing);

            if (b < kMaxBands) {
                auto& cache = bandMorphCache_[b];
                cache.morphX = morphX;
                cache.morphY = morphY;
                bandProcessors_[b].setMorphMode(
                    static_cast<MorphMode>(std::clamp(static_cast<int>(morphMode), 0, 2)));
                cache.activeNodeCount = std::clamp(
                    static_cast<int>(activeNodes), kMinActiveNodes, kMaxMorphNodes);
                bandProcessors_[b].setMorphSmoothingTime(morphSmoothing);
            }

            // Per-node state (always read to advance stream)
            for (int n = 0; n < kMaxMorphNodes; ++n) {
                Steinberg::int8 nodeType = 0;
                float drive = 1.0f, mix = 1.0f, toneHz = 4000.0f;
                float bias = 0.0f, folds = 1.0f, bitDepth = 16.0f;

                streamer.readInt8(nodeType);
                streamer.readFloat(drive);
                streamer.readFloat(mix);
                streamer.readFloat(toneHz);
                streamer.readFloat(bias);
                streamer.readFloat(folds);
                streamer.readFloat(bitDepth);

                if (b < kMaxBands) {
                    auto& mn = bandMorphCache_[b].nodes[static_cast<size_t>(n)];
                    mn.type = static_cast<DistortionType>(
                        std::clamp(static_cast<int>(nodeType), 0, 25));
                    mn.commonParams.drive = drive;
                    mn.commonParams.mix = mix;
                    mn.commonParams.toneHz = toneHz;
                    mn.params.bias = bias;
                    mn.params.folds = folds;
                    mn.params.bitDepth = bitDepth;
                }
            }

            if (b < kMaxBands) {
                bandProcessors_[b].setMorphEnabled(true);
                bandProcessors_[b].setMorphNodes(
                    bandMorphCache_[b].nodes, bandMorphCache_[b].activeNodeCount);
                bandProcessors_[b].setMorphPosition(
                    bandMorphCache_[b].morphX, bandMorphCache_[b].morphY);
            }
            // else: discard morph data from bands 4-7 (v7 migration)
        }
    }

    return Steinberg::kResultOk;
}

// ==============================================================================
// Parameter Handling
// ==============================================================================

void Processor::processParameterChanges(Steinberg::Vst::IParameterChanges* changes) {
    if (!changes) {
        return;
    }

    const Steinberg::int32 numParamsChanged = changes->getParameterCount();

    for (Steinberg::int32 i = 0; i < numParamsChanged; ++i) {
        Steinberg::Vst::IParamValueQueue* paramQueue = changes->getParameterData(i);
        if (!paramQueue) {
            continue;
        }

        const Steinberg::Vst::ParamID paramId = paramQueue->getParameterId();
        const Steinberg::int32 numPoints = paramQueue->getPointCount();

        // Get the last value (most recent)
        Steinberg::int32 sampleOffset = 0;
        Steinberg::Vst::ParamValue value = 0.0;

        if (paramQueue->getPoint(numPoints - 1, sampleOffset, value)
            != Steinberg::kResultTrue) {
            continue;
        }

        // =======================================================================
        // Route parameter changes by ID
        // Constitution Principle V: Values are normalized 0.0 to 1.0
        // =======================================================================

        switch (paramId) {
            case kInputGainId:
                inputGain_.store(static_cast<float>(value), std::memory_order_relaxed);
                break;

            case kOutputGainId:
                outputGain_.store(static_cast<float>(value), std::memory_order_relaxed);
                break;

            case kGlobalMixId:
                globalMix_.store(static_cast<float>(value), std::memory_order_relaxed);
                break;

            case kBandCountId: {
                // Convert normalized [0,1] to band count [1,4]
                const int newBandCount = 1 + static_cast<int>(value * 3.0 + 0.5);
                const int clamped = std::clamp(newBandCount, kMinBands, 4);
                bandCount_.store(clamped, std::memory_order_relaxed);
                crossoverL_.setBandCount(clamped);
                crossoverR_.setBandCount(clamped);
                break;
            }

            case kOversampleMaxId: {
                // FR-005, FR-006: Map normalized [0,1] to {1, 2, 4, 8}
                // StringListParameter with 4 items: index = round(value * 3)
                // Index 0 = 1x, Index 1 = 2x, Index 2 = 4x, Index 3 = 8x
                static constexpr int kOversampleFactors[] = {1, 2, 4, 8};
                const int index = std::clamp(
                    static_cast<int>(value * 3.0 + 0.5), 0, 3);
                const int factor = kOversampleFactors[index];
                maxOversampleFactor_.store(factor, std::memory_order_relaxed);
                // FR-016: Apply to all band processors
                for (auto& bp : bandProcessors_) {
                    bp.setMaxOversampleFactor(factor);
                }
                break;
            }

            default:
                // =================================================================
                // Sweep Parameters (spec 007-sweep-system)
                // FR-002 to FR-005: Sweep frequency, width, intensity, falloff
                // =================================================================
                if (isSweepParamId(paramId)) {
                    const SweepParamType sweepType = static_cast<SweepParamType>(paramId & 0xFF); // NOLINT(modernize-use-auto) explicit type for readability
                    switch (sweepType) {
                        case SweepParamType::kSweepEnable:
                            // FR-011: Enable/disable sweep
                            sweepProcessor_.setEnabled(value >= 0.5);
                            break;

                        case SweepParamType::kSweepFrequency: {
                            // FR-002: Convert normalized [0,1] to Hz [20, 20000] logarithmically
                            // Using log2 scale as per data-model.md
                            constexpr float kSweepLog2Min = 4.321928f;   // log2(20)
                            constexpr float kSweepLog2Max = 14.287712f;  // log2(20000)
                            constexpr float kSweepLog2Range = kSweepLog2Max - kSweepLog2Min;
                            const float log2Freq = kSweepLog2Min + static_cast<float>(value) * kSweepLog2Range;
                            const float freqHz = std::pow(2.0f, log2Freq);
                            // Store base frequency for modulation (FR-029a)
                            baseSweepFrequency_.store(freqHz, std::memory_order_relaxed);
                            sweepProcessor_.setCenterFrequency(freqHz);
                            break;
                        }

                        case SweepParamType::kSweepWidth: {
                            // FR-003: Convert normalized [0,1] to octaves [0.5, 4.0]
                            constexpr float kMinWidth = 0.5f;
                            constexpr float kMaxWidth = 4.0f;
                            baseSweepWidthNorm_.store(static_cast<float>(value), std::memory_order_relaxed);
                            const float widthOctaves = kMinWidth + static_cast<float>(value) * (kMaxWidth - kMinWidth);
                            sweepProcessor_.setWidth(widthOctaves);
                            break;
                        }

                        case SweepParamType::kSweepIntensity: {
                            // FR-004: Convert normalized [0,1] to intensity [0, 2] (0-200%)
                            baseSweepIntensityNorm_.store(static_cast<float>(value), std::memory_order_relaxed);
                            const float intensity = static_cast<float>(value) * 2.0f;
                            sweepProcessor_.setIntensity(intensity);
                            break;
                        }

                        case SweepParamType::kSweepMorphLink: {
                            // FR-014: Sweep-morph link mode
                            const int modeIndex = static_cast<int>(value * static_cast<float>(kMorphLinkModeCount - 1) + 0.5f);
                            sweepProcessor_.setMorphLinkMode(static_cast<MorphLinkMode>(modeIndex));
                            break;
                        }

                        case SweepParamType::kSweepFalloff:
                            // FR-005: Falloff mode (0 = Sharp, 1 = Smooth)
                            sweepProcessor_.setFalloffMode(value >= 0.5f ? SweepFalloff::Smooth : SweepFalloff::Sharp);
                            break;

                        // ========================================================
                        // Sweep LFO Parameters (FR-024, FR-025)
                        // ========================================================
                        case SweepParamType::kSweepLFOEnable:
                            sweepLFO_.setEnabled(value >= 0.5);
                            break;

                        case SweepParamType::kSweepLFORate: {
                            // Convert normalized [0,1] to Hz [0.01, 20] logarithmically
                            constexpr float kMinRateLog = -4.6052f;  // ln(0.01)
                            constexpr float kMaxRateLog = 2.9957f;   // ln(20)
                            const float logRate = kMinRateLog + static_cast<float>(value) * (kMaxRateLog - kMinRateLog);
                            const float rateHz = std::exp(logRate);
                            sweepLFO_.setRate(rateHz);
                            break;
                        }

                        case SweepParamType::kSweepLFOWaveform: {
                            // Convert normalized [0,1] to waveform index [0,5]
                            const int waveformIndex = static_cast<int>(value * 5.0f + 0.5f);
                            sweepLFO_.setWaveform(static_cast<Krate::DSP::Waveform>(waveformIndex));
                            break;
                        }

                        case SweepParamType::kSweepLFODepth:
                            // Depth is already normalized [0,1]
                            sweepLFO_.setDepth(static_cast<float>(value));
                            break;

                        case SweepParamType::kSweepLFOSync:
                            sweepLFO_.setTempoSync(value >= 0.5);
                            break;

                        case SweepParamType::kSweepLFONoteValue: {
                            // Convert normalized [0,1] to note value index [0,15]
                            // Standard note values: Whole, Half, Quarter, Eighth, Sixteenth (x3 for normal, dotted, triplet)
                            const int noteIndex = static_cast<int>(value * 14.0f + 0.5f);
                            const int noteValueIndex = noteIndex / 3;  // 0-4: Whole, Half, Quarter, Eighth, Sixteenth
                            const int modifierIndex = noteIndex % 3;   // 0: Normal, 1: Dotted, 2: Triplet
                            sweepLFO_.setNoteValue(
                                static_cast<Krate::DSP::NoteValue>(noteValueIndex),
                                static_cast<Krate::DSP::NoteModifier>(modifierIndex));
                            break;
                        }

                        // ========================================================
                        // Sweep Envelope Parameters (FR-026, FR-027)
                        // ========================================================
                        case SweepParamType::kSweepEnvEnable:
                            sweepEnvelope_.setEnabled(value >= 0.5);
                            break;

                        case SweepParamType::kSweepEnvAttack: {
                            // Convert normalized [0,1] to ms [1, 100]
                            const float attackMs = kMinSweepEnvAttackMs +
                                static_cast<float>(value) * (kMaxSweepEnvAttackMs - kMinSweepEnvAttackMs);
                            sweepEnvelope_.setAttackTime(attackMs);
                            break;
                        }

                        case SweepParamType::kSweepEnvRelease: {
                            // Convert normalized [0,1] to ms [10, 500]
                            const float releaseMs = kMinSweepEnvReleaseMs +
                                static_cast<float>(value) * (kMaxSweepEnvReleaseMs - kMinSweepEnvReleaseMs);
                            sweepEnvelope_.setReleaseTime(releaseMs);
                            break;
                        }

                        case SweepParamType::kSweepEnvSensitivity:
                            // Sensitivity is already normalized [0,1]
                            sweepEnvelope_.setSensitivity(static_cast<float>(value));
                            break;

                        // ========================================================
                        // Custom Curve Parameters (FR-039a, FR-039b, FR-039c)
                        // ========================================================
                        case SweepParamType::kSweepCustomCurvePointCount: {
                            // Rebuild curve when point count changes
                            int pointCount = static_cast<int>(2.0f + static_cast<float>(value) * 6.0f + 0.5f);
                            pointCount = std::clamp(pointCount, 2, 8);
                            // Curve will be rebuilt next time a point param changes
                            (void)pointCount;
                            break;
                        }

                        case SweepParamType::kSweepCustomCurveP0X:
                        case SweepParamType::kSweepCustomCurveP0Y:
                        case SweepParamType::kSweepCustomCurveP1X:
                        case SweepParamType::kSweepCustomCurveP1Y:
                        case SweepParamType::kSweepCustomCurveP2X:
                        case SweepParamType::kSweepCustomCurveP2Y:
                        case SweepParamType::kSweepCustomCurveP3X:
                        case SweepParamType::kSweepCustomCurveP3Y:
                        case SweepParamType::kSweepCustomCurveP4X:
                        case SweepParamType::kSweepCustomCurveP4Y:
                        case SweepParamType::kSweepCustomCurveP5X:
                        case SweepParamType::kSweepCustomCurveP5Y:
                        case SweepParamType::kSweepCustomCurveP6X:
                        case SweepParamType::kSweepCustomCurveP6Y:
                        case SweepParamType::kSweepCustomCurveP7X:
                        case SweepParamType::kSweepCustomCurveP7Y:
                            // Curve point changed - defer rebuild to process loop
                            // (handled below after all params processed)
                            break;

                        // ========================================================
                        // MIDI Parameters (FR-028, FR-029)
                        // ========================================================
                        case SweepParamType::kSweepMidiLearnActive:
                            midiLearnActive_ = (value >= 0.5);
                            break;

                        case SweepParamType::kSweepMidiCCNumber: {
                            assignedMidiCC_ = static_cast<int>(value * 128.0 + 0.5);
                            assignedMidiCC_ = std::clamp(assignedMidiCC_, 0, 128);
                            break;
                        }

                        default:
                            break;
                    }
                    break;  // Exit the default case after handling sweep params
                }
                // =================================================================
                // Modulation Parameters (spec 008-modulation-system)
                // =================================================================
                if (isModulationParamId(paramId)) {
                    if (isRoutingParamId(paramId)) {
                        // Routing parameters handled separately
                        const uint8_t routIdx = extractRoutingIndex(paramId);
                        const uint8_t routOff = extractRoutingOffset(paramId);
                        if (routIdx < Krate::DSP::kMaxModRoutings) {
                            auto routing = modulationEngine_.getRouting(routIdx);
                            switch (routOff) {
                                case 0:  // Source
                                    routing.source = static_cast<Krate::DSP::ModSource>(
                                        static_cast<int>(value * 12.0 + 0.5));
                                    routing.active = (routing.source != Krate::DSP::ModSource::None);
                                    break;
                                case 1:  // Destination
                                    routing.destParamId = static_cast<uint32_t>(
                                        value * static_cast<double>(ModDest::kTotalDestinations - 1) + 0.5);
                                    break;
                                case 2:  // Amount [-1, +1]
                                    routing.amount = static_cast<float>(value * 2.0 - 1.0);
                                    break;
                                case 3:  // Curve
                                    routing.curve = static_cast<Krate::DSP::ModCurve>(
                                        static_cast<int>(value * 3.0 + 0.5));
                                    break;
                                default:
                                    break;
                            }
                            modulationEngine_.setRouting(routIdx, routing);
                        }
                    } else {
                        const auto modType = static_cast<ModParamType>(paramId & 0xFF);
                        switch (modType) {
                            // LFO 1
                            case ModParamType::kLFO1Rate: {
                                constexpr float kMinLog = -4.6052f;
                                constexpr float kMaxLog = 2.9957f;
                                float rateHz = std::exp(kMinLog + static_cast<float>(value) * (kMaxLog - kMinLog));
                                modulationEngine_.setLFO1Rate(rateHz);
                                break;
                            }
                            case ModParamType::kLFO1Shape: {
                                int idx = static_cast<int>(value * 5.0f + 0.5f);
                                modulationEngine_.setLFO1Waveform(static_cast<Krate::DSP::Waveform>(idx));
                                break;
                            }
                            case ModParamType::kLFO1Phase:
                                modulationEngine_.setLFO1PhaseOffset(static_cast<float>(value) * 360.0f);
                                break;
                            case ModParamType::kLFO1Sync:
                                modulationEngine_.setLFO1TempoSync(value >= 0.5);
                                break;
                            case ModParamType::kLFO1NoteValue: {
                                int idx = static_cast<int>(value * 14.0f + 0.5f);
                                modulationEngine_.setLFO1NoteValue(
                                    static_cast<Krate::DSP::NoteValue>(idx / 3),
                                    static_cast<Krate::DSP::NoteModifier>(idx % 3));
                                break;
                            }
                            case ModParamType::kLFO1Unipolar:
                                modulationEngine_.setLFO1Unipolar(value >= 0.5);
                                break;
                            case ModParamType::kLFO1Retrigger:
                                modulationEngine_.setLFO1Retrigger(value >= 0.5);
                                break;

                            // LFO 2
                            case ModParamType::kLFO2Rate: {
                                constexpr float kMinLog = -4.6052f;
                                constexpr float kMaxLog = 2.9957f;
                                float rateHz = std::exp(kMinLog + static_cast<float>(value) * (kMaxLog - kMinLog));
                                modulationEngine_.setLFO2Rate(rateHz);
                                break;
                            }
                            case ModParamType::kLFO2Shape: {
                                int idx = static_cast<int>(value * 5.0f + 0.5f);
                                modulationEngine_.setLFO2Waveform(static_cast<Krate::DSP::Waveform>(idx));
                                break;
                            }
                            case ModParamType::kLFO2Phase:
                                modulationEngine_.setLFO2PhaseOffset(static_cast<float>(value) * 360.0f);
                                break;
                            case ModParamType::kLFO2Sync:
                                modulationEngine_.setLFO2TempoSync(value >= 0.5);
                                break;
                            case ModParamType::kLFO2NoteValue: {
                                int idx = static_cast<int>(value * 14.0f + 0.5f);
                                modulationEngine_.setLFO2NoteValue(
                                    static_cast<Krate::DSP::NoteValue>(idx / 3),
                                    static_cast<Krate::DSP::NoteModifier>(idx % 3));
                                break;
                            }
                            case ModParamType::kLFO2Unipolar:
                                modulationEngine_.setLFO2Unipolar(value >= 0.5);
                                break;
                            case ModParamType::kLFO2Retrigger:
                                modulationEngine_.setLFO2Retrigger(value >= 0.5);
                                break;

                            // Envelope Follower
                            case ModParamType::kEnvFollowerAttack: {
                                float ms = 1.0f + static_cast<float>(value) * 99.0f;
                                modulationEngine_.setEnvFollowerAttack(ms);
                                break;
                            }
                            case ModParamType::kEnvFollowerRelease: {
                                float ms = 10.0f + static_cast<float>(value) * 490.0f;
                                modulationEngine_.setEnvFollowerRelease(ms);
                                break;
                            }
                            case ModParamType::kEnvFollowerSensitivity:
                                modulationEngine_.setEnvFollowerSensitivity(static_cast<float>(value));
                                break;
                            case ModParamType::kEnvFollowerSource: {
                                int idx = static_cast<int>(value * 4.0f + 0.5f);
                                modulationEngine_.setEnvFollowerSource(
                                    static_cast<Krate::DSP::EnvFollowerSourceType>(idx));
                                break;
                            }

                            // Random
                            case ModParamType::kRandomRate: {
                                float hz = 0.1f + static_cast<float>(value) * 49.9f;
                                modulationEngine_.setRandomRate(hz);
                                break;
                            }
                            case ModParamType::kRandomSmoothness:
                                modulationEngine_.setRandomSmoothness(static_cast<float>(value));
                                break;
                            case ModParamType::kRandomSync:
                                modulationEngine_.setRandomTempoSync(value >= 0.5);
                                break;

                            // Chaos
                            case ModParamType::kChaosModel: {
                                int idx = static_cast<int>(value * 3.0f + 0.5f);
                                modulationEngine_.setChaosModel(
                                    static_cast<Krate::DSP::ChaosModel>(idx));
                                break;
                            }
                            case ModParamType::kChaosSpeed: {
                                float speed = 0.05f + static_cast<float>(value) * 19.95f;
                                modulationEngine_.setChaosSpeed(speed);
                                break;
                            }
                            case ModParamType::kChaosCoupling:
                                modulationEngine_.setChaosCoupling(static_cast<float>(value));
                                break;

                            // Sample & Hold
                            case ModParamType::kSampleHoldSource: {
                                int idx = static_cast<int>(value * 3.0f + 0.5f);
                                modulationEngine_.setSampleHoldSource(
                                    static_cast<Krate::DSP::SampleHoldInputType>(idx));
                                break;
                            }
                            case ModParamType::kSampleHoldRate: {
                                float hz = 0.1f + static_cast<float>(value) * 49.9f;
                                modulationEngine_.setSampleHoldRate(hz);
                                break;
                            }
                            case ModParamType::kSampleHoldSlew: {
                                float ms = static_cast<float>(value) * 500.0f;
                                modulationEngine_.setSampleHoldSlew(ms);
                                break;
                            }

                            // Pitch Follower
                            case ModParamType::kPitchFollowerMinHz: {
                                float hz = 20.0f + static_cast<float>(value) * 480.0f;
                                modulationEngine_.setPitchFollowerMinHz(hz);
                                break;
                            }
                            case ModParamType::kPitchFollowerMaxHz: {
                                float hz = 200.0f + static_cast<float>(value) * 4800.0f;
                                modulationEngine_.setPitchFollowerMaxHz(hz);
                                break;
                            }
                            case ModParamType::kPitchFollowerConfidence:
                                modulationEngine_.setPitchFollowerConfidence(static_cast<float>(value));
                                break;
                            case ModParamType::kPitchFollowerTrackingSpeed: {
                                float ms = 10.0f + static_cast<float>(value) * 290.0f;
                                modulationEngine_.setPitchFollowerTrackingSpeed(ms);
                                break;
                            }

                            // Transient Detector
                            case ModParamType::kTransientSensitivity:
                                modulationEngine_.setTransientSensitivity(static_cast<float>(value));
                                break;
                            case ModParamType::kTransientAttack: {
                                float ms = 0.5f + static_cast<float>(value) * 9.5f;
                                modulationEngine_.setTransientAttack(ms);
                                break;
                            }
                            case ModParamType::kTransientDecay: {
                                float ms = 20.0f + static_cast<float>(value) * 180.0f;
                                modulationEngine_.setTransientDecay(ms);
                                break;
                            }

                            // Macros
                            case ModParamType::kMacro1Value:
                                modulationEngine_.setMacroValue(0, static_cast<float>(value));
                                break;
                            case ModParamType::kMacro1Min:
                                modulationEngine_.setMacroMin(0, static_cast<float>(value));
                                break;
                            case ModParamType::kMacro1Max:
                                modulationEngine_.setMacroMax(0, static_cast<float>(value));
                                break;
                            case ModParamType::kMacro1Curve: {
                                int idx = static_cast<int>(value * 3.0f + 0.5f);
                                modulationEngine_.setMacroCurve(0, static_cast<Krate::DSP::ModCurve>(idx));
                                break;
                            }
                            case ModParamType::kMacro2Value:
                                modulationEngine_.setMacroValue(1, static_cast<float>(value));
                                break;
                            case ModParamType::kMacro2Min:
                                modulationEngine_.setMacroMin(1, static_cast<float>(value));
                                break;
                            case ModParamType::kMacro2Max:
                                modulationEngine_.setMacroMax(1, static_cast<float>(value));
                                break;
                            case ModParamType::kMacro2Curve: {
                                int idx = static_cast<int>(value * 3.0f + 0.5f);
                                modulationEngine_.setMacroCurve(1, static_cast<Krate::DSP::ModCurve>(idx));
                                break;
                            }
                            case ModParamType::kMacro3Value:
                                modulationEngine_.setMacroValue(2, static_cast<float>(value));
                                break;
                            case ModParamType::kMacro3Min:
                                modulationEngine_.setMacroMin(2, static_cast<float>(value));
                                break;
                            case ModParamType::kMacro3Max:
                                modulationEngine_.setMacroMax(2, static_cast<float>(value));
                                break;
                            case ModParamType::kMacro3Curve: {
                                int idx = static_cast<int>(value * 3.0f + 0.5f);
                                modulationEngine_.setMacroCurve(2, static_cast<Krate::DSP::ModCurve>(idx));
                                break;
                            }
                            case ModParamType::kMacro4Value:
                                modulationEngine_.setMacroValue(3, static_cast<float>(value));
                                break;
                            case ModParamType::kMacro4Min:
                                modulationEngine_.setMacroMin(3, static_cast<float>(value));
                                break;
                            case ModParamType::kMacro4Max:
                                modulationEngine_.setMacroMax(3, static_cast<float>(value));
                                break;
                            case ModParamType::kMacro4Curve: {
                                int idx = static_cast<int>(value * 3.0f + 0.5f);
                                modulationEngine_.setMacroCurve(3, static_cast<Krate::DSP::ModCurve>(idx));
                                break;
                            }

                            default:
                                break;
                        }
                    }
                    break;  // Exit the default case after handling modulation params
                }
                // =============================================================
                // Node Parameters (per-band, per-node distortion params)
                // =============================================================
                if (isNodeParamId(paramId)) {
                    const uint8_t band = extractBandFromNodeParam(paramId);
                    const uint8_t node = extractNode(paramId);
                    const NodeParamType nodeType = extractNodeParamType(paramId);

                    if (band < kMaxBands && node < kMaxMorphNodes) {
                        auto& cache = bandMorphCache_[band];
                        auto& mn = cache.nodes[static_cast<size_t>(node)];

                        switch (nodeType) {
                            case NodeParamType::kNodeType: {
                                // StringListParameter: 26 types
                                int idx = static_cast<int>(value * 25.0 + 0.5);
                                mn.type = static_cast<DistortionType>(std::clamp(idx, 0, 25));
                                break;
                            }
                            case NodeParamType::kNodeDrive:
                                // RangeParameter [0, 10]
                                mn.commonParams.drive = static_cast<float>(value) * 10.0f;
                                break;
                            case NodeParamType::kNodeMix:
                                // RangeParameter [0, 100]% -> [0, 1]
                                mn.commonParams.mix = static_cast<float>(value);
                                break;
                            case NodeParamType::kNodeTone:
                                // RangeParameter [200, 8000] Hz
                                mn.commonParams.toneHz = 200.0f + static_cast<float>(value) * 7800.0f;
                                break;
                            case NodeParamType::kNodeBias:
                                // RangeParameter [-1, +1]
                                mn.params.bias = static_cast<float>(value) * 2.0f - 1.0f;
                                break;
                            case NodeParamType::kNodeFolds:
                                // RangeParameter [1, 12] (integer steps)
                                mn.params.folds = 1.0f + std::round(static_cast<float>(value) * 11.0f);
                                break;
                            case NodeParamType::kNodeBitDepth:
                                // RangeParameter [4, 24] (integer steps)
                                mn.params.bitDepth = 4.0f + std::round(static_cast<float>(value) * 20.0f);
                                break;
                        }

                        // Push updated nodes to BandProcessor
                        bandProcessors_[band].setMorphNodes(cache.nodes, cache.activeNodeCount);
                    }
                    break;
                }
                // Check for band parameters
                if (isBandParamId(paramId)) {
                    const uint8_t band = extractBandIndex(paramId);
                    const BandParamType paramType = extractBandParamType(paramId);

                    if (band < kMaxBands) {
                        switch (paramType) {
                            case BandParamType::kBandGain: {
                                // Convert normalized [0,1] to dB [-24, +24]
                                const float gainDb = kMinBandGainDb + static_cast<float>(value) * (kMaxBandGainDb - kMinBandGainDb);
                                bandStates_[band].gainDb = gainDb;
                                bandProcessors_[band].setGainDb(gainDb);
                                break;
                            }
                            case BandParamType::kBandPan: {
                                // Convert normalized [0,1] to pan [-1, +1]
                                const float pan = static_cast<float>(value) * 2.0f - 1.0f;
                                bandStates_[band].pan = pan;
                                bandProcessors_[band].setPan(pan);
                                break;
                            }
                            case BandParamType::kBandSolo:
                                bandStates_[band].solo = value >= 0.5;
                                break;
                            case BandParamType::kBandBypass:
                                bandStates_[band].bypass = value >= 0.5;
                                bandProcessors_[band].setBypassed(bandStates_[band].bypass);
                                break;
                            case BandParamType::kBandMute:
                                bandStates_[band].mute = value >= 0.5;
                                bandProcessors_[band].setMute(bandStates_[band].mute);
                                break;
                            case BandParamType::kBandMorphX: {
                                bandMorphCache_[band].morphX = static_cast<float>(value);
                                bandProcessors_[band].setMorphPosition(
                                    bandMorphCache_[band].morphX,
                                    bandMorphCache_[band].morphY);
                                break;
                            }
                            case BandParamType::kBandMorphY: {
                                bandMorphCache_[band].morphY = static_cast<float>(value);
                                bandProcessors_[band].setMorphPosition(
                                    bandMorphCache_[band].morphX,
                                    bandMorphCache_[band].morphY);
                                break;
                            }
                            case BandParamType::kBandActiveNodes: {
                                // StringListParameter: 3 entries ["2","3","4"]
                                int idx = static_cast<int>(value * 2.0 + 0.5);
                                int count = std::clamp(idx + 2, kMinActiveNodes, kMaxMorphNodes);
                                bandMorphCache_[band].activeNodeCount = count;
                                bandProcessors_[band].setMorphNodes(
                                    bandMorphCache_[band].nodes, count);
                                break;
                            }
                            case BandParamType::kBandMorphSmoothing: {
                                // RangeParameter [0, 500] ms
                                float timeMs = static_cast<float>(value) * 500.0f;
                                bandProcessors_[band].setMorphSmoothingTime(timeMs);
                                break;
                            }
                            case BandParamType::kBandMorphMode: {
                                // StringListParameter: 3 entries
                                int idx = static_cast<int>(value * 2.0 + 0.5);
                                bandProcessors_[band].setMorphMode(
                                    static_cast<MorphMode>(std::clamp(idx, 0, 2)));
                                break;
                            }
                            case BandParamType::kBandMorphXLink:
                            case BandParamType::kBandMorphYLink:
                            case BandParamType::kBandExpanded:
                            case BandParamType::kBandSelectedNode:
                            case BandParamType::kBandDisplayedType:
                            default:
                                // UI-only params (sweep-morph link, expanded,
                                // selectedNode, displayedType): no processor action
                                break;
                        }
                    }
                }
                // Check for crossover frequency parameters
                else if (isCrossoverParamId(paramId)) {
                    const uint8_t index = extractCrossoverIndex(paramId);
                    if (index < kMaxBands - 1) {
                        // Convert normalized [0,1] to Hz [20, 20000] logarithmically
                        const float logMin = std::log10(kMinCrossoverHz);
                        const float logMax = std::log10(kMaxCrossoverHz);
                        const float logFreq = logMin + static_cast<float>(value) * (logMax - logMin);
                        const float freqHz = std::pow(10.0f, logFreq);
                        crossoverL_.setCrossoverFrequency(static_cast<int>(index), freqHz);
                        crossoverR_.setCrossoverFrequency(static_cast<int>(index), freqHz);
                    }
                }
                break;
        }
    }
}

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

// ==============================================================================
// Spectrum FIFO IMessage
// ==============================================================================

void Processor::sendSpectrumFIFOMessage() {
    auto msg = Steinberg::owned(allocateMessage());
    if (!msg)
        return;

    msg->setMessageID("SpectrumFIFO");
    auto* attrs = msg->getAttributes();
    if (!attrs)
        return;

    // Send FIFO pointers as int64 (safe: both components are in-process)
    attrs->setInt("inputPtr",
        static_cast<Steinberg::int64>(
            reinterpret_cast<intptr_t>(&spectrumInputFIFO_)));
    attrs->setInt("outputPtr",
        static_cast<Steinberg::int64>(
            reinterpret_cast<intptr_t>(&spectrumOutputFIFO_)));
    attrs->setFloat("sampleRate", sampleRate_);

    sendMessage(msg);
}

void Processor::sendModOffsetsMessage() {
    auto msg = Steinberg::owned(allocateMessage());
    if (!msg)
        return;

    msg->setMessageID("ModOffsets");
    auto* attrs = msg->getAttributes();
    if (!attrs)
        return;

    // Send pointer to modulation offset array (safe: both components in-process)
    attrs->setInt("ptr",
        static_cast<Steinberg::int64>(
            reinterpret_cast<intptr_t>(modulationEngine_.getModOffsetsArray())));

    sendMessage(msg);
}

} // namespace Disrumpo
