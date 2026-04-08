#pragma once

// ==============================================================================
// Membrum Processor
// ==============================================================================

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "public.sdk/source/vst/vstaudioeffect.h"

#include "dsp/drum_voice.h"

#include <atomic>

namespace Membrum {

class Processor : public Steinberg::Vst::AudioEffect
{
public:
    Processor();

    static Steinberg::FUnknown* createInstance(void*)
    {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new Processor());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) override;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) override;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;

private:
    void processParameterChanges(Steinberg::Vst::IParameterChanges* paramChanges);
    void processEvents(Steinberg::Vst::IEventList* events);

    // DSP
    DrumVoice voice_;
    double sampleRate_ = 44100.0;

    // Parameter state (atomic for thread-safe transfer from controller)
    std::atomic<float> material_{0.5f};
    std::atomic<float> size_{0.5f};
    std::atomic<float> decay_{0.3f};
    std::atomic<float> strikePosition_{0.3f};
    std::atomic<float> level_{0.8f};
};

} // namespace Membrum
