// ==============================================================================
// Audio Processor Implementation
// ==============================================================================

#include "processor.h"

#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"

namespace Innexus {

// ==============================================================================
// Constructor
// ==============================================================================
Processor::Processor()
{
    setControllerClass(kControllerUID);
}

// ==============================================================================
// Initialize
// ==============================================================================
Steinberg::tresult PLUGIN_API Processor::initialize(Steinberg::FUnknown* context)
{
    auto result = AudioEffect::initialize(context);
    if (result != Steinberg::kResultOk)
        return result;

    // Sidechain audio input (for live analysis mode)
    addAudioInput(STR16("Sidechain"), Steinberg::Vst::SpeakerArr::kStereo,
                  Steinberg::Vst::BusTypes::kAux);

    // MIDI event input
    addEventInput(STR16("Event In"), 1);

    // Stereo audio output
    addAudioOutput(STR16("Stereo Out"), Steinberg::Vst::SpeakerArr::kStereo);

    return Steinberg::kResultOk;
}

// ==============================================================================
// Terminate
// ==============================================================================
Steinberg::tresult PLUGIN_API Processor::terminate()
{
    return AudioEffect::terminate();
}

// ==============================================================================
// Set Active
// ==============================================================================
Steinberg::tresult PLUGIN_API Processor::setActive(Steinberg::TBool state)
{
    return AudioEffect::setActive(state);
}

// ==============================================================================
// Setup Processing
// ==============================================================================
Steinberg::tresult PLUGIN_API Processor::setupProcessing(
    Steinberg::Vst::ProcessSetup& newSetup)
{
    sampleRate_ = newSetup.sampleRate;
    return AudioEffect::setupProcessing(newSetup);
}

// ==============================================================================
// Process
// ==============================================================================
Steinberg::tresult PLUGIN_API Processor::process(Steinberg::Vst::ProcessData& data)
{
    // --- Handle parameter changes ---
    if (data.inputParameterChanges)
    {
        auto numParams = data.inputParameterChanges->getParameterCount();
        for (Steinberg::int32 i = 0; i < numParams; ++i)
        {
            auto* paramQueue = data.inputParameterChanges->getParameterData(i);
            if (!paramQueue)
                continue;

            Steinberg::Vst::ParamValue value;
            Steinberg::int32 sampleOffset;
            auto numPoints = paramQueue->getPointCount();
            if (paramQueue->getPoint(numPoints - 1, sampleOffset, value) ==
                Steinberg::kResultTrue)
            {
                switch (paramQueue->getParameterId())
                {
                case kBypassId:
                    bypass_.store(static_cast<float>(value));
                    break;
                case kMasterGainId:
                    masterGain_.store(static_cast<float>(value));
                    break;
                }
            }
        }
    }

    // --- Output silence for now ---
    if (data.numOutputs < 1 || data.outputs[0].numChannels < 2)
        return Steinberg::kResultOk;

    auto numSamples = data.numSamples;
    auto** out = data.outputs[0].channelBuffers32;

    for (Steinberg::int32 s = 0; s < numSamples; ++s)
    {
        out[0][s] = 0.0f;
        out[1][s] = 0.0f;
    }

    data.outputs[0].silenceFlags = 0x3; // Both channels silent

    return Steinberg::kResultOk;
}

// ==============================================================================
// Can Process Sample Size
// ==============================================================================
Steinberg::tresult PLUGIN_API Processor::canProcessSampleSize(
    Steinberg::int32 symbolicSampleSize)
{
    if (symbolicSampleSize == Steinberg::Vst::kSample32)
        return Steinberg::kResultTrue;
    return Steinberg::kResultFalse;
}

// ==============================================================================
// State Management
// ==============================================================================
Steinberg::tresult PLUGIN_API Processor::setState(Steinberg::IBStream* state)
{
    if (!state)
        return Steinberg::kResultFalse;

    // TODO: Load state
    return Steinberg::kResultOk;
}

Steinberg::tresult PLUGIN_API Processor::getState(Steinberg::IBStream* state)
{
    if (!state)
        return Steinberg::kResultFalse;

    // TODO: Save state
    return Steinberg::kResultOk;
}

} // namespace Innexus
