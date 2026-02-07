// ==============================================================================
// Audio Processor Implementation
// ==============================================================================

#include "processor.h"
#include "plugin_ids.h"

#include "base/source/fstreamer.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <algorithm>

namespace Ruinae {

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

    // Ruinae is a synthesizer instrument:
    // - Event input (MIDI notes)
    // - Stereo audio output (no audio input)
    addEventInput(STR16("Event Input"));
    addAudioOutput(STR16("Audio Output"), Steinberg::Vst::SpeakerArr::kStereo);

    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Processor::terminate() {
    return AudioEffect::terminate();
}

// ==============================================================================
// IAudioProcessor
// ==============================================================================

Steinberg::tresult PLUGIN_API Processor::setupProcessing(
    Steinberg::Vst::ProcessSetup& setup) {

    sampleRate_ = setup.sampleRate;
    maxBlockSize_ = setup.maxSamplesPerBlock;

    // ==========================================================================
    // Constitution Principle II & VI: Pre-allocate ALL buffers HERE
    // ==========================================================================

    mixBufferL_.resize(static_cast<size_t>(maxBlockSize_));
    mixBufferR_.resize(static_cast<size_t>(maxBlockSize_));

    // TODO: Prepare RuinaeEngine when implemented (Phase 6)
    // engine_.prepare(sampleRate_, static_cast<size_t>(maxBlockSize_));

    return AudioEffect::setupProcessing(setup);
}

Steinberg::tresult PLUGIN_API Processor::setActive(Steinberg::TBool state) {
    if (state) {
        // Activating: reset DSP state
        std::fill(mixBufferL_.begin(), mixBufferL_.end(), 0.0f);
        std::fill(mixBufferR_.begin(), mixBufferR_.end(), 0.0f);
        // TODO: engine_.reset();
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

    // Process MIDI events
    if (data.inputEvents) {
        processEvents(data.inputEvents);
    }

    // Check if we have audio to process
    if (data.numSamples == 0) {
        return Steinberg::kResultTrue;
    }

    // Verify we have valid output
    if (data.numOutputs == 0 || data.outputs[0].numChannels < 2) {
        return Steinberg::kResultTrue;
    }

    float* outputL = data.outputs[0].channelBuffers32[0];
    float* outputR = data.outputs[0].channelBuffers32[1];

    if (!outputL || !outputR) {
        return Steinberg::kResultTrue;
    }

    [[maybe_unused]] const size_t numSamples = static_cast<size_t>(data.numSamples);
    [[maybe_unused]] const float currentGain = masterGain_.load(std::memory_order_relaxed);

    // ==========================================================================
    // Main Audio Processing
    // ==========================================================================

    // TODO: Replace with RuinaeEngine processing (Phase 6)
    // For now, output silence
    std::fill_n(outputL, numSamples, 0.0f);
    std::fill_n(outputR, numSamples, 0.0f);

    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Processor::setBusArrangements(
    Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
    Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) {

    // Ruinae is an instrument: no audio inputs, stereo output only
    if (numIns == 0 && numOuts == 1 &&
        outputs[0] == Steinberg::Vst::SpeakerArr::kStereo) {
        return AudioEffect::setBusArrangements(inputs, numIns, outputs, numOuts);
    }

    return Steinberg::kResultFalse;
}

// ==============================================================================
// IComponent - State Management
// ==============================================================================

Steinberg::tresult PLUGIN_API Processor::getState(Steinberg::IBStream* state) {
    Steinberg::IBStreamer streamer(state, kLittleEndian);

    // Save global parameters
    float gain = masterGain_.load(std::memory_order_relaxed);
    streamer.writeFloat(gain);

    Steinberg::int32 voiceMode = voiceMode_.load(std::memory_order_relaxed);
    streamer.writeInt32(voiceMode);

    Steinberg::int32 polyphony = polyphony_.load(std::memory_order_relaxed);
    streamer.writeInt32(polyphony);

    Steinberg::int32 softLimit = softLimit_.load(std::memory_order_relaxed) ? 1 : 0;
    streamer.writeInt32(softLimit);

    // TODO: Save all parameter packs when implemented

    return Steinberg::kResultTrue;
}

Steinberg::tresult PLUGIN_API Processor::setState(Steinberg::IBStream* state) {
    Steinberg::IBStreamer streamer(state, kLittleEndian);

    float gain = 1.0f;
    if (streamer.readFloat(gain)) {
        masterGain_.store(gain, std::memory_order_relaxed);
    }

    Steinberg::int32 voiceMode = 0;
    if (streamer.readInt32(voiceMode)) {
        voiceMode_.store(voiceMode, std::memory_order_relaxed);
    }

    Steinberg::int32 polyphony = 8;
    if (streamer.readInt32(polyphony)) {
        polyphony_.store(polyphony, std::memory_order_relaxed);
    }

    Steinberg::int32 softLimit = 1;
    if (streamer.readInt32(softLimit)) {
        softLimit_.store(softLimit != 0, std::memory_order_relaxed);
    }

    // TODO: Load all parameter packs when implemented

    return Steinberg::kResultTrue;
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
        // Route parameter changes by ID range
        // Constitution Principle V: Values are normalized 0.0 to 1.0
        // =======================================================================

        switch (paramId) {
            case kMasterGainId:
                masterGain_.store(static_cast<float>(value * 2.0),
                                 std::memory_order_relaxed);
                break;
            case kVoiceModeId:
                voiceMode_.store(static_cast<int>(value + 0.5),
                                std::memory_order_relaxed);
                break;
            case kPolyphonyId:
                // Convert normalized (0-1) to polyphony (1-16)
                polyphony_.store(static_cast<int>(value * 15.0 + 1.0 + 0.5),
                                std::memory_order_relaxed);
                break;
            case kSoftLimitId:
                softLimit_.store(value >= 0.5, std::memory_order_relaxed);
                break;
            default:
                // TODO: Route to section-specific parameter handlers
                break;
        }
    }
}

// ==============================================================================
// MIDI Event Handling
// ==============================================================================

void Processor::processEvents(Steinberg::Vst::IEventList* events) {
    if (!events) {
        return;
    }

    const Steinberg::int32 numEvents = events->getEventCount();

    for (Steinberg::int32 i = 0; i < numEvents; ++i) {
        Steinberg::Vst::Event event{};
        if (events->getEvent(i, event) != Steinberg::kResultTrue) {
            continue;
        }

        switch (event.type) {
            case Steinberg::Vst::Event::kNoteOnEvent:
                // TODO: Dispatch to RuinaeEngine
                // engine_.noteOn(event.noteOn.pitch, event.noteOn.velocity * 127);
                break;

            case Steinberg::Vst::Event::kNoteOffEvent:
                // TODO: Dispatch to RuinaeEngine
                // engine_.noteOff(event.noteOff.pitch);
                break;

            default:
                break;
        }
    }
}

} // namespace Ruinae
