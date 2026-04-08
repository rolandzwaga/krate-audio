// ==============================================================================
// Membrum Processor -- Audio processing, state, MIDI handling
// ==============================================================================

#include "processor.h"
#include "plugin_ids.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"

// FTZ/DAZ for denormal prevention on x86 (SC-007)
#if defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__)
#include <xmmintrin.h> // _MM_SET_FLUSH_ZERO_MODE
#include <pmmintrin.h> // _MM_SET_DENORMALS_ZERO_MODE
#endif

namespace Membrum {

using namespace Steinberg;
using namespace Steinberg::Vst;

Processor::Processor()
{
    setControllerClass(kControllerUID);
}

tresult PLUGIN_API Processor::initialize(FUnknown* context)
{
    auto result = AudioEffect::initialize(context);
    if (result != kResultOk)
        return result;

    addEventInput(STR16("Event In"));
    addAudioOutput(STR16("Stereo Out"), SpeakerArr::kStereo);

    return kResultOk;
}

// FR-015: Process parameter changes from host
void Processor::processParameterChanges(IParameterChanges* paramChanges)
{
    if (!paramChanges)
        return;

    int32 numParams = paramChanges->getParameterCount();
    for (int32 i = 0; i < numParams; ++i)
    {
        auto* queue = paramChanges->getParameterData(i);
        if (!queue)
            continue;

        ParamID paramId = queue->getParameterId();
        int32 numPoints = queue->getPointCount();
        if (numPoints <= 0)
            continue;

        // Use the last point value (most recent)
        ParamValue value = 0.0;
        int32 sampleOffset = 0;
        if (queue->getPoint(numPoints - 1, sampleOffset, value) != kResultOk)
            continue;

        float fValue = static_cast<float>(value);

        switch (paramId)
        {
        case kMaterialId:
            material_.store(fValue);
            voice_.setMaterial(fValue);
            break;
        case kSizeId:
            size_.store(fValue);
            voice_.setSize(fValue);
            break;
        case kDecayId:
            decay_.store(fValue);
            voice_.setDecay(fValue);
            break;
        case kStrikePositionId:
            strikePosition_.store(fValue);
            voice_.setStrikePosition(fValue);
            break;
        case kLevelId:
            level_.store(fValue);
            voice_.setLevel(fValue);
            break;
        default:
            break;
        }
    }
}

// Process MIDI events: note-on/note-off for MIDI note 36 only
void Processor::processEvents(IEventList* events)
{
    if (!events)
        return;

    int32 numEvents = events->getEventCount();
    for (int32 i = 0; i < numEvents; ++i)
    {
        Event event{};
        if (events->getEvent(i, event) != kResultOk)
            continue;

        switch (event.type)
        {
        case Event::kNoteOnEvent:
        {
            // FR-010: Only respond to MIDI note 36
            // FR-011: Ignore all other notes
            if (event.noteOn.pitch != 36)
                break;

            // FR-013: Velocity 0 = note-off per MIDI convention
            if (event.noteOn.velocity <= 0.0f)
            {
                voice_.noteOff();
                break;
            }

            // FR-014: Retrigger -- just restart the voice
            voice_.noteOn(event.noteOn.velocity);
            break;
        }
        case Event::kNoteOffEvent:
        {
            // FR-013: Only respond to note 36
            if (event.noteOff.pitch != 36)
                break;

            voice_.noteOff();
            break;
        }
        default:
            break;
        }
    }
}

tresult PLUGIN_API Processor::process(ProcessData& data)
{
    // Handle zero-length blocks (edge case)
    if (data.numSamples == 0)
        return kResultOk;

    // FR-015: Process parameter changes
    processParameterChanges(data.inputParameterChanges);

    // Process MIDI events
    processEvents(data.inputEvents);

    // FR-012: Produce stereo output (mono voice -> both channels)
    if (data.numOutputs > 0 && data.outputs != nullptr)
    {
        float* outL = data.outputs[0].channelBuffers32[0];
        float* outR = data.outputs[0].channelBuffers32[1];

        for (int32 i = 0; i < data.numSamples; ++i)
        {
            float s = voice_.process();
            outL[i] = s;
            outR[i] = s;
        }

        // Set silence flags if voice is not active
        data.outputs[0].silenceFlags = voice_.isActive() ? 0 : 3;
    }

    return kResultOk;
}

// FR-016: Save state -- version int32 + 5x float64
tresult PLUGIN_API Processor::getState(IBStream* state)
{
    if (!state)
        return kResultFalse;

    int32 version = kCurrentStateVersion;
    state->write(&version, sizeof(version), nullptr);

    double params[] = {
        static_cast<double>(material_.load()),
        static_cast<double>(size_.load()),
        static_cast<double>(decay_.load()),
        static_cast<double>(strikePosition_.load()),
        static_cast<double>(level_.load()),
    };

    for (double p : params)
        state->write(&p, sizeof(p), nullptr);

    return kResultOk;
}

// FR-016: Load state -- version int32 + 5x float64
tresult PLUGIN_API Processor::setState(IBStream* state)
{
    if (!state)
        return kResultFalse;

    int32 version = 0;
    if (state->read(&version, sizeof(version), nullptr) != kResultOk)
        return kResultFalse;

    // Read parameters in order: Material, Size, Decay, StrikePosition, Level
    const float defaults[] = {0.5f, 0.5f, 0.3f, 0.3f, 0.8f};
    std::atomic<float>* atomics[] = {
        &material_, &size_, &decay_, &strikePosition_, &level_};

    for (int i = 0; i < 5; ++i)
    {
        double value = static_cast<double>(defaults[i]);
        if (state->read(&value, sizeof(value), nullptr) != kResultOk)
        {
            // Fewer fields -- use defaults for remaining
            atomics[i]->store(defaults[i]);
            continue;
        }
        atomics[i]->store(static_cast<float>(value));
    }

    return kResultOk;
}

tresult PLUGIN_API Processor::setupProcessing(ProcessSetup& setup)
{
    // T070: Enable FTZ/DAZ on x86 to flush denormals to zero (SC-007)
#if defined(_M_X64) || defined(__x86_64__) || defined(_M_IX86) || defined(__i386__)
    _MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON);
    _MM_SET_DENORMALS_ZERO_MODE(_MM_DENORMALS_ZERO_ON);
#endif

    sampleRate_ = setup.sampleRate;
    // SC-002: zero-latency by design -- DrumVoice.prepare() completes all allocation
    // before process(), so the first process() block can produce audio immediately.
    voice_.prepare(sampleRate_);
    return AudioEffect::setupProcessing(setup);
}

tresult PLUGIN_API Processor::setActive(TBool state)
{
    if (state)
        voice_.prepare(sampleRate_);
    return kResultOk;
}

} // namespace Membrum
