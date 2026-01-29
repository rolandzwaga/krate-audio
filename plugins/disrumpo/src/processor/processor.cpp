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
    }

    // Initialize sweep processor (spec 007-sweep-system)
    sweepProcessor_.prepare(sampleRate_, setup.maxSamplesPerBlock);
    sweepProcessor_.setCustomCurve(&customCurve_);

    // Initialize sweep LFO and envelope (FR-024 to FR-027)
    sweepLFO_.prepare(sampleRate_);
    sweepEnvelope_.prepare(sampleRate_, setup.maxSamplesPerBlock);

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
    // Sweep Processing (spec 007-sweep-system)
    // FR-007: Process sweep smoother for the entire block
    // ==========================================================================

    // Get base sweep frequency
    float baseFreq = baseSweepFrequency_.load(std::memory_order_relaxed);

    // ==========================================================================
    // Sweep Automation / Additive Modulation (FR-029a)
    // LFO and envelope modulate in log space, then add offsets
    // ==========================================================================

    // Process envelope follower with input signal (average of L+R)
    // Must be done sample-by-sample for accuracy, but we can use first sample
    // as approximation for the whole block (acceptable for per-block sweep update)
    if (sweepEnvelope_.isEnabled()) {
        float inputMono = (inputL[0] + inputR[0]) * 0.5f;
        (void)sweepEnvelope_.processSample(inputMono);  // Discard return; we use getModulatedFrequency()
    }

    // Calculate modulated frequency with additive modulation
    float modulatedFreq = baseFreq;

    // Get LFO modulation (bidirectional: +/- 2 octaves at full depth)
    if (sweepLFO_.isEnabled()) {
        float lfoValue = sweepLFO_.process();  // Returns [-depth, +depth]
        // LFO value maps to octave shift
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

    const int numBands = bandCount_.load(std::memory_order_relaxed);

    // Calculate per-band sweep intensities
    // Band center frequencies (approximate Bark scale for 8 bands)
    static constexpr std::array<float, kMaxBands> kBandCenterFreqs = {
        50.0f, 150.0f, 350.0f, 750.0f, 1500.0f, 3000.0f, 6000.0f, 12000.0f
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

    std::array<float, kMaxBands> bandsL{};
    std::array<float, kMaxBands> bandsR{};

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
        bandCount = std::clamp(bandCount, kMinBands, kMaxBands);
        bandCount_.store(bandCount, std::memory_order_relaxed);

        // Read per-band state for all 8 bands
        for (int b = 0; b < kMaxBands; ++b) {
            auto& bs = bandStates_[b];
            Steinberg::int8 soloVal = 0;
            Steinberg::int8 bypassVal = 0;
            Steinberg::int8 muteVal = 0;

            if (!streamer.readFloat(bs.gainDb)) bs.gainDb = 0.0f;
            if (!streamer.readFloat(bs.pan)) bs.pan = 0.0f;
            if (!streamer.readInt8(soloVal)) soloVal = 0;
            if (!streamer.readInt8(bypassVal)) bypassVal = 0;
            if (!streamer.readInt8(muteVal)) muteVal = 0;

            bs.solo = (soloVal != 0);
            bs.bypass = (bypassVal != 0);
            bs.mute = (muteVal != 0);

            // Clamp values to valid ranges
            bs.gainDb = std::clamp(bs.gainDb, kMinBandGainDb, kMaxBandGainDb);
            bs.pan = std::clamp(bs.pan, -1.0f, 1.0f);

            // Apply to band processors
            bandProcessors_[b].setGainDb(bs.gainDb);
            bandProcessors_[b].setPan(bs.pan);
            bandProcessors_[b].setMute(bs.mute);
        }

        // Read crossover frequencies (7 floats)
        for (int c = 0; c < kMaxBands - 1; ++c) {
            float freq = 1000.0f;  // Default
            if (!streamer.readFloat(freq)) break;

            // Apply to both L and R crossover networks
            crossoverL_.setCrossoverFrequency(c, freq);
            crossoverR_.setCrossoverFrequency(c, freq);
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
                // Convert normalized [0,1] to band count [1,8]
                const int newBandCount = 1 + static_cast<int>(value * 7.0 + 0.5);
                const int clamped = std::clamp(newBandCount, kMinBands, kMaxBands);
                bandCount_.store(clamped, std::memory_order_relaxed);
                crossoverL_.setBandCount(clamped);
                crossoverR_.setBandCount(clamped);
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
                            const float widthOctaves = kMinWidth + static_cast<float>(value) * (kMaxWidth - kMinWidth);
                            sweepProcessor_.setWidth(widthOctaves);
                            break;
                        }

                        case SweepParamType::kSweepIntensity: {
                            // FR-004: Convert normalized [0,1] to intensity [0, 2] (0-200%)
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
                                break;
                            case BandParamType::kBandMute:
                                bandStates_[band].mute = value >= 0.5;
                                bandProcessors_[band].setMute(bandStates_[band].mute);
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

} // namespace Disrumpo
