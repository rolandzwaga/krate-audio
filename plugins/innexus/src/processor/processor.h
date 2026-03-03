#pragma once

// ==============================================================================
// Audio Processor
// ==============================================================================
// Constitution Principle I: VST3 Architecture Separation
// - This is the Processor component (IAudioProcessor + IComponent)
// - MUST be completely separate from Controller
// - MUST function without Controller instantiation
//
// Constitution Principle II: Real-Time Audio Thread Safety
// - NEVER allocate memory in process()
// - NEVER use locks/mutexes
// - Pre-allocate ALL buffers in setupProcessing()
//
// Innexus is an INSTRUMENT with SIDECHAIN:
// - Sidechain audio input bus (for live analysis)
// - MIDI event input bus
// - Stereo audio output bus
// ==============================================================================

#include "plugin_ids.h"

#include "public.sdk/source/vst/vstaudioeffect.h"

#include <atomic>

namespace Innexus {

class Processor : public Steinberg::Vst::AudioEffect
{
public:
    Processor();
    ~Processor() override = default;

    // --- IAudioProcessor ---
    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API setupProcessing(
        Steinberg::Vst::ProcessSetup& newSetup) override;
    Steinberg::tresult PLUGIN_API process(
        Steinberg::Vst::ProcessData& data) override;
    Steinberg::tresult PLUGIN_API canProcessSampleSize(
        Steinberg::int32 symbolicSampleSize) override;

    // --- IComponent ---
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;

    static Steinberg::FUnknown* createInstance(void*)
    {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new Processor());
    }

private:
    // Parameter atomics
    std::atomic<float> bypass_{0.0f};
    std::atomic<float> masterGain_{1.0f};

    double sampleRate_ = 44100.0;
};

} // namespace Innexus
