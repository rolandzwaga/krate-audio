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
// Ruinae is a SYNTHESIZER (Instrument):
// - No audio input bus
// - MIDI event input bus
// - Stereo audio output bus
// ==============================================================================

#include "engine/ruinae_engine.h"
#include "parameters/global_params.h"
#include "parameters/osc_a_params.h"
#include "parameters/osc_b_params.h"
#include "parameters/mixer_params.h"
#include "parameters/filter_params.h"
#include "parameters/distortion_params.h"
#include "parameters/trance_gate_params.h"
#include "parameters/amp_env_params.h"
#include "parameters/filter_env_params.h"
#include "parameters/mod_env_params.h"
#include "parameters/lfo1_params.h"
#include "parameters/lfo2_params.h"
#include "parameters/chaos_mod_params.h"
#include "parameters/mod_matrix_params.h"
#include "parameters/global_filter_params.h"
#include "parameters/freeze_params.h"
#include "parameters/delay_params.h"
#include "parameters/reverb_params.h"
#include "parameters/mono_mode_params.h"

#include "public.sdk/source/vst/vstaudioeffect.h"

#include <vector>

namespace Ruinae {

// State version for serialization
constexpr Steinberg::int32 kCurrentStateVersion = 1;

// ==============================================================================
// Processor Class
// ==============================================================================

class Processor : public Steinberg::Vst::AudioEffect {
public:
    Processor();
    ~Processor() override = default;

    // ===========================================================================
    // IPluginBase
    // ===========================================================================

    Steinberg::tresult PLUGIN_API initialize(FUnknown* context) override;
    Steinberg::tresult PLUGIN_API terminate() override;

    // ===========================================================================
    // IAudioProcessor
    // ===========================================================================

    Steinberg::tresult PLUGIN_API setupProcessing(
        Steinberg::Vst::ProcessSetup& setup) override;

    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;

    Steinberg::tresult PLUGIN_API process(
        Steinberg::Vst::ProcessData& data) override;

    Steinberg::tresult PLUGIN_API setBusArrangements(
        Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
        Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) override;

    // ===========================================================================
    // IComponent
    // ===========================================================================

    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;

    // ===========================================================================
    // Factory
    // ===========================================================================

    static FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new Processor());
    }

protected:
    // ==========================================================================
    // Parameter Handling
    // ==========================================================================

    void processParameterChanges(Steinberg::Vst::IParameterChanges* changes);
    void processEvents(Steinberg::Vst::IEventList* events);
    void applyParamsToEngine();

private:
    // ==========================================================================
    // Processing State
    // ==========================================================================

    double sampleRate_ = 44100.0;
    Steinberg::int32 maxBlockSize_ = 0;

    // ==========================================================================
    // Parameter Packs (atomic for thread-safe access)
    // ==========================================================================

    GlobalParams globalParams_;
    OscAParams oscAParams_;
    OscBParams oscBParams_;
    MixerParams mixerParams_;
    RuinaeFilterParams filterParams_;
    RuinaeDistortionParams distortionParams_;
    RuinaeTranceGateParams tranceGateParams_;
    AmpEnvParams ampEnvParams_;
    FilterEnvParams filterEnvParams_;
    ModEnvParams modEnvParams_;
    LFO1Params lfo1Params_;
    LFO2Params lfo2Params_;
    ChaosModParams chaosModParams_;
    ModMatrixParams modMatrixParams_;
    GlobalFilterParams globalFilterParams_;
    RuinaeFreezeParams freezeParams_;
    RuinaeDelayParams delayParams_;
    RuinaeReverbParams reverbParams_;
    MonoModeParams monoModeParams_;

    // ==========================================================================
    // DSP Engine
    // ==========================================================================

    Krate::DSP::RuinaeEngine engine_;

    // ==========================================================================
    // Scratch Buffers (pre-allocated in setupProcessing)
    // ==========================================================================

    std::vector<float> mixBufferL_;
    std::vector<float> mixBufferR_;
};

} // namespace Ruinae
