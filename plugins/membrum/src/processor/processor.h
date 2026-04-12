#pragma once

// ==============================================================================
// Membrum Processor -- Phase 2
// ==============================================================================
// All atomics store NORMALIZED parameter values (0..1) for continuous
// parameters, matching Phase 1 and the VST3 host convention. Discrete selectors
// (Exciter Type, Body Model) are stored as std::atomic<int> because
// ExciterBank / BodyBank take a typed enum.
// ==============================================================================

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "public.sdk/source/vst/vstaudioeffect.h"

#include "dsp/drum_voice.h"
#include "voice_pool/voice_pool.h"

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
    VoicePool voicePool_;
    double sampleRate_ = 44100.0;
    int    maxBlockSize_ = 2048;

    // ---- Phase 3 polyphony / voice stealing / choke group ----
    std::atomic<int> maxPolyphony_{8};           // FR-111 -- [4, 16]
    std::atomic<int> voiceStealingPolicy_{0};    // FR-120 -- VoiceStealingPolicy int
    std::atomic<int> chokeGroup_{0};             // FR-138 -- [0, 8]; 0 = none

    // ---- Phase 1 parameters (normalized 0..1) ----
    std::atomic<float> material_{0.5f};
    std::atomic<float> size_{0.5f};
    std::atomic<float> decay_{0.3f};
    std::atomic<float> strikePosition_{0.3f};
    std::atomic<float> level_{0.8f};

    // ---- Phase 2 discrete selectors (typed integer) ----
    std::atomic<int> exciterType_{0};   // ExciterType::Impulse
    std::atomic<int> bodyModel_{0};     // BodyModelType::Membrane

    // ---- Phase 2 continuous parameters (normalized 0..1) ----
    // Defaults per vst_parameter_contract.md (all "bypass" values).
    // Secondary exciter params
    std::atomic<float> exciterFMRatio_{0.133333f};            // (1.4 - 1.0) / (4.0 - 1.0)
    std::atomic<float> exciterFeedbackAmount_{0.0f};
    std::atomic<float> exciterNoiseBurstDuration_{0.230769f}; // (5 - 2) / (15 - 2)
    std::atomic<float> exciterFrictionPressure_{0.3f};

    // Tone shaper
    std::atomic<float> toneShaperFilterType_{0.0f};            // Lowpass
    std::atomic<float> toneShaperFilterCutoff_{1.0f};          // 20 kHz = bypass
    std::atomic<float> toneShaperFilterResonance_{0.0f};
    std::atomic<float> toneShaperFilterEnvAmount_{0.5f};       // midpoint = 0 in [-1,1]
    std::atomic<float> toneShaperDriveAmount_{0.0f};
    std::atomic<float> toneShaperFoldAmount_{0.0f};
    std::atomic<float> toneShaperPitchEnvStart_{0.070721f};    // ≈ 160 Hz on 20..2000 log
    std::atomic<float> toneShaperPitchEnvEnd_{0.0f};           // 20..2000 start
    std::atomic<float> toneShaperPitchEnvTime_{0.0f};          // 0 ms = disabled
    std::atomic<float> toneShaperPitchEnvCurve_{0.0f};         // Exp
    std::atomic<float> toneShaperFilterEnvAttack_{0.0f};
    std::atomic<float> toneShaperFilterEnvDecay_{0.1f};
    std::atomic<float> toneShaperFilterEnvSustain_{0.0f};
    std::atomic<float> toneShaperFilterEnvRelease_{0.1f};

    // Unnatural Zone
    std::atomic<float> unnaturalModeStretch_{0.333333f};       // 1.0 on [0.5, 2.0]
    std::atomic<float> unnaturalDecaySkew_{0.5f};              // 0 on [-1, 1]
    std::atomic<float> unnaturalModeInjectAmount_{0.0f};
    std::atomic<float> unnaturalNonlinearCoupling_{0.0f};

    // Material Morph
    std::atomic<float> morphEnabled_{0.0f};                    // off
    std::atomic<float> morphStart_{1.0f};
    std::atomic<float> morphEnd_{0.0f};
    std::atomic<float> morphDurationMs_{0.095477f};            // (200 - 10) / (2000 - 10)
    std::atomic<float> morphCurve_{0.0f};                      // Lin
};

} // namespace Membrum
