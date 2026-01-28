// ==============================================================================
// Audio Processor Implementation
// ==============================================================================

#include "processor.h"
#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

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
    // Band Processing (FR-001a: sample-by-sample processing)
    // ==========================================================================

    const int numBands = bandCount_.load(std::memory_order_relaxed);
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
                const int clamped = std::max(kMinBands, std::min(kMaxBands, newBandCount));
                bandCount_.store(clamped, std::memory_order_relaxed);
                crossoverL_.setBandCount(clamped);
                crossoverR_.setBandCount(clamped);
                break;
            }

            default:
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
