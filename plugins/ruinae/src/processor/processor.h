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
#include "parameters/flanger_params.h"
#include "parameters/chorus_params.h"
#include "parameters/harmonizer_params.h"
#include "parameters/mono_mode_params.h"
#include "parameters/macro_params.h"
#include "parameters/rungler_params.h"
#include "parameters/settings_params.h"
#include "parameters/env_follower_params.h"
#include "parameters/sample_hold_params.h"
#include "parameters/random_params.h"
#include "parameters/pitch_follower_params.h"
#include "parameters/transient_params.h"
#include "parameters/arpeggiator_params.h"

#include <krate/dsp/processors/arpeggiator_core.h>

#include "ui/mod_matrix_types.h"

#include "public.sdk/source/vst/vstaudioeffect.h"
#include "public.sdk/source/vst/utility/rttransfer.h"
#include "pluginterfaces/vst/ivstmessage.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace Ruinae {

/// Serialized preset bytes for lock-free transfer from UI thread to audio thread.
/// Used by RTTransferT to atomically hand off an entire preset state.
struct PresetSnapshot {
    std::vector<char> bytes;
};

// ==============================================================================
// AtomicVoiceModRoute — per-field atomics for lock-free voice route access
// ==============================================================================
// Eliminates the data race where notify() (UI thread) writes voiceRoutes_
// while process() (audio thread) reads them. Each field is individually atomic
// with relaxed ordering (same pattern as all other parameter packs).

struct AtomicVoiceModRoute {
    std::atomic<uint8_t> source{0};
    std::atomic<uint8_t> destination{0};
    std::atomic<float> amount{0.0f};
    std::atomic<uint8_t> curve{0};
    std::atomic<float> smoothMs{0.0f};
    std::atomic<uint8_t> scale{2};
    std::atomic<uint8_t> bypass{0};
    std::atomic<uint8_t> active{0};

    /// Store all fields from a plain VoiceModRoute (UI thread writes)
    void store(const Krate::Plugins::VoiceModRoute& r) {
        source.store(r.source, std::memory_order_relaxed);
        destination.store(r.destination, std::memory_order_relaxed);
        amount.store(r.amount, std::memory_order_relaxed);
        curve.store(r.curve, std::memory_order_relaxed);
        smoothMs.store(r.smoothMs, std::memory_order_relaxed);
        scale.store(r.scale, std::memory_order_relaxed);
        bypass.store(r.bypass, std::memory_order_relaxed);
        active.store(r.active, std::memory_order_relaxed);
    }

    /// Load all fields into a plain VoiceModRoute (audio thread reads)
    Krate::Plugins::VoiceModRoute load() const {
        Krate::Plugins::VoiceModRoute r;
        r.source = source.load(std::memory_order_relaxed);
        r.destination = destination.load(std::memory_order_relaxed);
        r.amount = amount.load(std::memory_order_relaxed);
        r.curve = curve.load(std::memory_order_relaxed);
        r.smoothMs = smoothMs.load(std::memory_order_relaxed);
        r.scale = scale.load(std::memory_order_relaxed);
        r.bypass = bypass.load(std::memory_order_relaxed);
        r.active = active.load(std::memory_order_relaxed);
        return r;
    }
};

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

    // --- MIDI Event Dispatcher Callbacks ---
    // Used by Krate::Plugins::dispatchMidiEvents (midi_event_dispatcher.h)
    void onNoteOn(int16_t pitch, float velocity);
    void onNoteOff(int16_t pitch);

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

    // ==========================================================================
    // Pre-allocated IMessages (accessible to test subclass)
    // ==========================================================================

    /// Pre-allocated message for voice route state sync (Issue 1)
    Steinberg::IPtr<Steinberg::Vst::IMessage> voiceRouteSyncMsg_;

    /// Pre-allocated one-time pointer messages (Issue 3)
    Steinberg::IPtr<Steinberg::Vst::IMessage> playbackMsg_;
    Steinberg::IPtr<Steinberg::Vst::IMessage> envDisplayMsg_;
    Steinberg::IPtr<Steinberg::Vst::IMessage> morphPadModMsg_;

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
    std::atomic<int> modulationType_{0};  // ModulationType: 0=None, 1=Phaser, 2=Flanger, 3=Chorus
    std::atomic<bool> harmonizerEnabled_{false};

    RuinaeDelayParams delayParams_;
    RuinaeReverbParams reverbParams_;
    RuinaePhaserParams phaserParams_;
    RuinaeFlangerParams flangerParams_;
    RuinaeChorusParams chorusParams_;
    RuinaeHarmonizerParams harmonizerParams_;
    MonoModeParams monoModeParams_;
    MacroParams macroParams_;
    RunglerParams runglerParams_;
    SettingsParams settingsParams_;
    EnvFollowerParams envFollowerParams_;
    SampleHoldParams sampleHoldParams_;
    RandomParams randomParams_;
    PitchFollowerParams pitchFollowerParams_;
    TransientParams transientParams_;
    ArpeggiatorParams arpParams_;

    // ==========================================================================
    // Arpeggiator Engine (FR-010)
    // ==========================================================================

    Krate::DSP::ArpeggiatorCore arpCore_;
    std::array<Krate::DSP::ArpEvent, 128> arpEvents_{};
    float lastArpPitch_ = 0.0f;  ///< Last arp NoteOn pitch, normalized 0-1 (for mod source)
    float lastSidechainActive_ = -1.0f;  ///< Tracks sidechain state for output param changes
    bool wasTransportPlaying_{false};
    bool hostSupportsTransport_{false}; ///< True once host reports kPlaying
    double prevProjectTimeMusic_{-1.0}; ///< For detecting backward PPQ jumps (DAW loop)

    // Previous arp param values -- only call setters that reset internal state
    // (step index, swing counter) when the value actually changes.
    Krate::DSP::ArpMode prevArpMode_{Krate::DSP::ArpMode::Up};
    int prevArpOctaveRange_{1};
    Krate::DSP::OctaveMode prevArpOctaveMode_{Krate::DSP::OctaveMode::Sequential};
    int prevArpNoteValue_{10};  // kNoteValueDefaultIndex
    Krate::DSP::LatchMode prevArpLatchMode_{Krate::DSP::LatchMode::Off};
    Krate::DSP::ArpRetriggerMode prevArpRetrigger_{Krate::DSP::ArpRetriggerMode::Off};

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
    // Morph Pad Modulation Display (shared with controller via IMessage pointer)
    // ==========================================================================

    /// Modulated morph position (normalized [0,1]) for XYMorphPad animation
    std::atomic<float> modulatedMorphX_{0.5f};
    /// Modulated spectral tilt (normalized [0,1]) for XYMorphPad animation
    std::atomic<float> modulatedMorphY_{0.5f};

    bool morphPadModMessageSent_ = false;

    // ==========================================================================
    // Voice Route State (communicated via IMessage, T085-T086)
    // ==========================================================================
    // Per-field atomics eliminate data race between notify() (UI thread) and
    // process() (audio thread). See AtomicVoiceModRoute above.

    std::array<AtomicVoiceModRoute, Krate::Plugins::kMaxVoiceRoutes>
        voiceRoutes_{};

    /// Send authoritative voice route state to controller
    void sendVoiceModRouteState();

    // ==========================================================================
    // Arp Skip Event State (Phase 11c - FR-012)
    // ==========================================================================

    /// Pre-allocated IMessages for skip events (one per lane, 6 total)
    std::array<Steinberg::IPtr<Steinberg::Vst::IMessage>, 6> skipMessages_{};

    /// Send a skip event to the controller (no allocations)
    void sendSkipEvent(int lane, int step);

    /// Whether the editor is open (gating skip event sends)
    std::atomic<bool> editorOpen_{false};

    // ==========================================================================
    // Crash-Proof Preset Loading (RTTransferT)
    // ==========================================================================

    /// Lock-free triple-buffer for transferring preset bytes from UI to audio thread
    Steinberg::Vst::RTTransferT<PresetSnapshot> stateTransfer_;

    /// Flag set by applyPresetSnapshot() to trigger voice route sync in process()
    std::atomic<bool> needVoiceRouteSync_{false};

    /// Apply a serialized preset snapshot on the audio thread (RT-safe)
    void applyPresetSnapshot(const PresetSnapshot& snapshot);
};

} // namespace Ruinae
