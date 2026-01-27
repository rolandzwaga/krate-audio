// ==============================================================================
// Audio Processor Implementation
// ==============================================================================

#include "processor.h"
#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

#include <cstring>  // for memcpy

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
    // (No additional buffers needed for passthrough skeleton)

    return AudioEffect::setupProcessing(setup);
}

Steinberg::tresult PLUGIN_API Processor::setActive(Steinberg::TBool state) {
    if (state) {
        // Activating: reset any processing state
        // (No state to reset in passthrough skeleton)
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

    // ==========================================================================
    // FR-012: Bit-transparent audio passthrough
    // Parameters are registered but have no effect in skeleton
    // ==========================================================================

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
    // FR-012, SC-003: Bit-transparent passthrough (output = input)
    // Use memcpy for guaranteed bit-identical copy
    // ==========================================================================

    const size_t bytesToCopy = static_cast<size_t>(data.numSamples) * sizeof(float);
    std::memcpy(outputL, inputL, bytesToCopy);
    std::memcpy(outputR, inputR, bytesToCopy);

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
    // FR-018: Serialize all parameters to IBStream
    // FR-020: Version field MUST be first for future migration

    Steinberg::IBStreamer streamer(state, kLittleEndian);

    // Write version first (MUST be first per FR-020)
    if (!streamer.writeInt32(kPresetVersion)) {
        return Steinberg::kResultFalse;
    }

    // Write parameters in order (per data-model.md Section 3)
    if (!streamer.writeFloat(inputGain_.load(std::memory_order_relaxed))) {
        return Steinberg::kResultFalse;
    }
    if (!streamer.writeFloat(outputGain_.load(std::memory_order_relaxed))) {
        return Steinberg::kResultFalse;
    }
    if (!streamer.writeFloat(globalMix_.load(std::memory_order_relaxed))) {
        return Steinberg::kResultFalse;
    }

    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Processor::setState(Steinberg::IBStream* state) {
    // FR-019: Deserialize parameters from IBStream
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
        // (For v1, we just read all known fields)
    }

    // Read parameters
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

    // Apply to atomics (thread-safe)
    inputGain_.store(inputGain, std::memory_order_relaxed);
    outputGain_.store(outputGain, std::memory_order_relaxed);
    globalMix_.store(globalMix, std::memory_order_relaxed);

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

            default:
                // Unknown parameter ID - ignore
                break;
        }
    }
}

} // namespace Disrumpo
