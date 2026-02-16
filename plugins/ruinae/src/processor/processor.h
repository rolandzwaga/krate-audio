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
#include "parameters/delay_params.h"
#include "parameters/reverb_params.h"
#include "parameters/phaser_params.h"
#include "parameters/mono_mode_params.h"
#include "parameters/macro_params.h"
#include "parameters/rungler_params.h"
#include "parameters/settings_params.h"
#include "parameters/env_follower_params.h"
#include "parameters/sample_hold_params.h"
#include "parameters/random_params.h"

#include "ui/mod_matrix_types.h"

#include "public.sdk/source/vst/vstaudioeffect.h"

#include <array>
#include <atomic>
#include <vector>

namespace Ruinae {

// State version for serialization
// v1: Original 19 parameter packs (base mod matrix: source, dest, amount only)
// v2: Extended mod matrix with detail params (curve, smooth, scale, bypass)
// v3: Voice modulation routes (16 slots, IMessage-based, persisted in state)
// v4: Added MixerShift parameter to mixer pack
// v5: Added filter type-specific params (ladder slope/drive, formant morph/gender, comb damping)
// v6: Added SVF slope/drive params
// v7: Added SVF gain, envelope filter, and self-oscillating filter params
// v8: Removed freeze effect from effects chain
// v9: Added type-specific delay parameters (51 new params)
// v10: Added FX enable parameters (delay/reverb on/off)
// v11: Added phaser params + enable flag
// v12: Extended LFO params (phase offset, retrigger, note value, unipolar, fade-in, symmetry, quantize)
// v13: Macro and Rungler params
// v14: Settings params (pitch bend range, velocity curve, tuning ref, alloc mode, steal mode, gain comp)
// v15: Mod source params (Env Follower, S&H, Random, Pitch Follower, Transient)
constexpr Steinberg::int32 kCurrentStateVersion = 15;

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
    // IMessage (Voice Route Communication, T085-T086)
    // ===========================================================================

    /// Receive messages from controller (VoiceModRouteUpdate)
    Steinberg::tresult PLUGIN_API notify(
        Steinberg::Vst::IMessage* message) override;

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
    double tempoBPM_ = 120.0;
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
    // FX Enable (1500-1502)
    std::atomic<bool> delayEnabled_{false};
    std::atomic<bool> reverbEnabled_{false};
    std::atomic<bool> phaserEnabled_{false};

    RuinaeDelayParams delayParams_;
    RuinaeReverbParams reverbParams_;
    RuinaePhaserParams phaserParams_;
    MonoModeParams monoModeParams_;
    MacroParams macroParams_;
    RunglerParams runglerParams_;
    SettingsParams settingsParams_;
    EnvFollowerParams envFollowerParams_;
    SampleHoldParams sampleHoldParams_;
    RandomParams randomParams_;

    // ==========================================================================
    // DSP Engine
    // ==========================================================================

    Krate::DSP::RuinaeEngine engine_;

    // ==========================================================================
    // Scratch Buffers (pre-allocated in setupProcessing)
    // ==========================================================================

    std::vector<float> mixBufferL_;
    std::vector<float> mixBufferR_;

    // ==========================================================================
    // Playback Position (shared with controller via IMessage pointer)
    // ==========================================================================

    std::atomic<int> tranceGatePlaybackStep_{-1};
    std::atomic<bool> isTransportPlaying_{false};
    bool playbackMessageSent_ = false;

    // ==========================================================================
    // Envelope Display State (shared with controller via IMessage pointer)
    // ==========================================================================

    // Amp envelope display state (most-recently-triggered voice)
    std::atomic<float> ampEnvDisplayOutput_{0.0f};
    std::atomic<int> ampEnvDisplayStage_{0};

    // Filter envelope display state
    std::atomic<float> filterEnvDisplayOutput_{0.0f};
    std::atomic<int> filterEnvDisplayStage_{0};

    // Mod envelope display state
    std::atomic<float> modEnvDisplayOutput_{0.0f};
    std::atomic<int> modEnvDisplayStage_{0};

    // Any voice active flag (shared across all envelope displays)
    std::atomic<bool> envVoiceActive_{false};

    bool envDisplayMessageSent_ = false;

    // ==========================================================================
    // Voice Route State (communicated via IMessage, T085-T086)
    // ==========================================================================

    std::array<Krate::Plugins::VoiceModRoute, Krate::Plugins::kMaxVoiceRoutes>
        voiceRoutes_{};

    /// Send authoritative voice route state to controller
    void sendVoiceModRouteState();
};

} // namespace Ruinae
