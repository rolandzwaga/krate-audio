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
#include "parameters/voice_lfo_params.h"
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

/// What the audio thread needs from a preset load, handed over by RTTransferT.
/// The stream was already parsed into the parameter atomics on the message
/// thread (Processor::loadStateBytes); the audio thread only resets (R-197).
struct PresetSnapshot {
    bool arpOnly{false};  ///< an "Arp" browser preset: only the arp was replaced
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

    /// kInfiniteTail with reverb Freeze or a delay at feedback >= 1; otherwise
    /// release + delay + reverb decay, in samples (R-190).
    Steinberg::uint32 PLUGIN_API getTailSamples() override;

    // ===========================================================================
    // IComponent
    // ===========================================================================

    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;

    // ===========================================================================
    // IMessage (Voice Route Communication, T085-T086)
    // ===========================================================================

    /// Receive messages from controller (VoiceModRouteUpdate, LoadPresetState)
    Steinberg::tresult PLUGIN_API notify(
        Steinberg::Vst::IMessage* message) override;

    // ===========================================================================
    // Factory
    // ===========================================================================

    // --- MIDI Event Dispatcher Callbacks ---
    // Used by Krate::Plugins::dispatchMidiEvents (midi_event_dispatcher.h)
    void onNoteOn(int16_t pitch, float velocity);
    void onNoteOff(int16_t pitch);

    // --- MPE Callbacks (noteId-aware) ---
    void onNoteOn(int16_t pitch, float velocity, int32_t noteId);
    void onNoteOff(int16_t pitch, int32_t noteId);
    void onNoteExpression(int32_t noteId, uint32_t typeId, double value);
    void onPitchBend(float bipolar);

    static FUnknown* createInstance(void*) {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new Processor());
    }

    /// Test hook: true once the one-time pointer handshakes have been issued to
    /// the controller. These are sent from setActive() so that process() makes no
    /// synchronous host sendMessage() calls.
    [[nodiscard]] bool handshakeMessagesSent() const { return handshakeMessagesSent_; }

    /// Test hook: voices the engine still holds (gated or releasing).
    [[nodiscard]] uint32_t engineActiveVoiceCountForTest() const noexcept {
        return engine_.getActiveVoiceCount();
    }

    /// Test hook: engine processBlock calls so far (one per render segment).
    [[nodiscard]] uint64_t engineRenderCallsForTest() const noexcept { return engineRenderCalls_; }

protected:
    // ==========================================================================
    // Parameter Handling
    // ==========================================================================

    void processParameterChanges(Steinberg::Vst::IParameterChanges* changes);
    void processEvents(Steinberg::Vst::IEventList* events, Steinberg::int32 numSamples);

    // --- Sample-accurate note timing ---
    // Note and expression events reach the engine at their own sample: they are
    // queued with their offset and the engine renders the buffer in segments
    // split at those offsets (renderEngineSegments). Keys for the arpeggiator
    // are queued the same way and the arp runs in segments split at them
    // (runArpSegments, R-072).
    enum class TimedEventKind : uint8_t {
        NoteOn, NoteOff, ArpNoteOn, ArpNoteOff, Expression, PitchBend, Aftertouch, ModWheel
    };
    struct TimedEvent {
        uint32_t offset{0};
        TimedEventKind kind{TimedEventKind::NoteOn};
        uint8_t pitch{0};
        uint8_t velocity{0};
        bool legato{false};
        bool legatoContinues{false};  ///< ArpNoteOn: same Slide batch as the previous legato note
        int32_t noteId{-1};
        uint32_t typeId{0};
        double value{0.0};
    };
    static constexpr size_t kMaxTimedEvents = 1024;
    void queueTimedEvent(const TimedEvent& event) noexcept;
    void applyTimedEvent(const TimedEvent& event) noexcept;
    void flushTimedEvents() noexcept;
    void renderEngineSegments(float* left, float* right, size_t numSamples,
                              const Krate::DSP::BlockContext& blockCtx,
                              const float* sidechainL, const float* sidechainR) noexcept;

    /// A host key for the arp core, applied at its offset by runArpSegments.
    struct ArpKeyEvent {
        uint32_t offset{0};
        uint8_t pitch{0};
        uint8_t velocity{0};
        bool on{false};
    };
    static constexpr size_t kMaxArpKeyEvents = 256;
    void queueArpKey(const ArpKeyEvent& key) noexcept;
    void applyArpKey(const ArpKeyEvent& key) noexcept;
    void flushArpKeys() noexcept;
    /// Run the arp core over the buffer in segments split at the queued keys'
    /// offsets, applying each key between segments; writes arpEvents_ with
    /// offsets from the buffer start and returns their count. `hostSync`
    /// re-aligns the step clock to each segment's musical position.
    size_t runArpSegments(const Krate::DSP::BlockContext& arpCtx, bool hostSync) noexcept;

    void applyParamsToEngine();

    // applyParamsToEngine() is split into one helper per parameter section,
    // mirroring the handle*ParamChange split on the other side. They are called
    // in a fixed order -- see the definition in processor_params.cpp.
    void applyVoiceAndOscParams();
    void applyFilterParams();
    void applyDistortionParams();
    void applyTranceGateParams();
    void applyEnvelopeParams();
    void applyModSourceParams();
    void applyModRoutingParams();
    void applyGlobalFilterAndFxEnableParams();
    void applyDelayParams();
    void applyReverbParams();
    void applyModulationEffectParams();
    void applyHarmonizerParams();
    void applyAuxModSourceParams();
    void applyArpParams();

    // ==========================================================================
    // Pre-allocated IMessages (accessible to test subclass)
    // ==========================================================================

    /// Pre-allocated message for voice route state sync (Issue 1)
    Steinberg::IPtr<Steinberg::Vst::IMessage> voiceRouteSyncMsg_;

    /// Pre-allocated one-time pointer messages (Issue 3)
    Steinberg::IPtr<Steinberg::Vst::IMessage> playbackMsg_;
    Steinberg::IPtr<Steinberg::Vst::IMessage> envDisplayMsg_;
    Steinberg::IPtr<Steinberg::Vst::IMessage> morphPadModMsg_;

    /// Read-only engine access for tests that need to assert on DSP state
    /// rather than infer it from rendered audio.
    [[nodiscard]] const Krate::DSP::RuinaeEngine& engine() const noexcept { return engine_; }

private:
    // ==========================================================================
    // Processing State
    // ==========================================================================

    double sampleRate_ = 44100.0;
    /// Last valid host tempo. Atomic because getTailSamples() may run on any thread.
    std::atomic<double> tempoBPM_{120.0};
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
    VoiceLfoParams voiceLfoParams_;
    ArpeggiatorParams arpParams_;

    // ==========================================================================
    // Arpeggiator Engine (FR-010)
    // ==========================================================================

    Krate::DSP::ArpeggiatorCore arpCore_;
    std::array<Krate::DSP::ArpEvent, 128> arpEvents_{};
    float lastArpPitch_ = 0.0f;  ///< Last arp NoteOn pitch, normalized 0-1 (for mod source)
    // Host keys for the arp core in the current buffer, sorted by offset (stable)
    std::array<ArpKeyEvent, kMaxArpKeyEvents> arpKeyEvents_{};
    size_t arpKeyEventCount_ = 0;

    // Timed note queue for the current buffer, sorted by offset (stable)
    std::array<TimedEvent, kMaxTimedEvents> timedEvents_{};
    size_t timedEventCount_ = 0;
    uint64_t engineRenderCalls_ = 0;    ///< engine_.processBlock calls (test hook)
    uint32_t currentEventOffset_ = 0;   ///< sampleOffset of the event being dispatched
    uint32_t currentBlockSamples_ = 0;  ///< numSamples of the buffer being processed
    // The mod wheel (kMidiModWheelId) drives Macro 1. The last writer wins: the
    // wheel holds Macro 1 until the Macro 1 knob changes or a full reset
    // (activation, preset load) hands it back to the knob (R-042).
    float modWheelValue_ = 0.0f;
    bool modWheelOwnsMacro1_ = false;
    float lastSidechainActive_ = -1.0f;  ///< Tracks sidechain state for output param changes
    bool wasTransportPlaying_{false};
    bool hostSupportsTransport_{false}; ///< True once host reports kPlaying
    double prevProjectTimeMusic_{-1.0}; ///< For detecting backward PPQ jumps (DAW loop)
    /// The arp's transport position when the host gives no musical time
    /// (R-068): samples since activation or since the arp started running,
    /// advanced by each block the arp runs.
    int64_t arpFreeRunSamples_{0};

    // Previous arp param values -- only call setters that reset internal state
    // (step index, swing counter) when the value actually changes.
    Krate::DSP::ArpMode prevArpMode_{Krate::DSP::ArpMode::Up};
    int prevArpOctaveRange_{1};
    Krate::DSP::OctaveMode prevArpOctaveMode_{Krate::DSP::OctaveMode::Sequential};
    int prevArpNoteValue_{10};  // kNoteValueDefaultIndex
    Krate::DSP::LatchMode prevArpLatchMode_{Krate::DSP::LatchMode::Off};
    Krate::DSP::ArpRetriggerMode prevArpRetrigger_{Krate::DSP::ArpRetriggerMode::Off};

    // Note ownership (R-029, R-064, R-065). A note-off goes wherever its
    // note-on went, whatever the operating mode is by then. Open-note counts
    // per pitch, so two notes on one pitch (MPE) each get their note-off.
    std::array<uint8_t, 128> engineDirectOpen_{};  ///< host notes queued straight to the engine
    std::array<uint8_t, 128> arpCoreKeysOpen_{};   ///< host notes fed to arpCore_
    std::array<uint8_t, 128> arpEngineOpen_{};     ///< arp NoteOns queued to the engine
    std::array<uint8_t, 128> arpMidiOutOpen_{};    ///< arp NoteOns added to the MIDI output
    int prevArpOpMode_{kArpOff};  ///< operating mode of the previous block
    /// The arp core was reset (setupProcessing, setActive(true), preset load)
    /// and forgot the notes it put on the MIDI output; the next process() with
    /// an output list sends their NoteOffs from arpMidiOutOpen_ (R-070, R-136).
    bool pendingArpMidiOutFlush_{false};

    /// Release every arp note still open on the engine and the MIDI output, at
    /// `offset`, and zero both ledgers. With `out` null the MIDI-out ledger is
    /// kept for a later flush.
    void flushArpLedger(Steinberg::Vst::IEventList* out, Steinberg::int32 offset) noexcept;

    /// Route one arp Slide step (R-028, R-113): arpEvents_[begin, end) are its
    /// legato NoteOns. Each goes to the engine as a legato note of one batch and
    /// to the MIDI output; then every arp note still open that is not in the
    /// batch is released on both sinks, at the batch's last NoteOn.
    void routeArpSlideBatch(Steinberg::Vst::ProcessData& data, size_t begin, size_t end,
                            bool midiOut) noexcept;

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

    /// Guard for the one-time controller handshakes issued from setActive().
    /// Kept so a deactivate/reactivate cycle does not re-send them.
    bool handshakeMessagesSent_ = false;

    /// Send the pointer-handshake messages to the controller. Message thread
    /// only: sendMessage() is a synchronous host call.
    void sendOneTimeHandshakes();

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
    // Arp Skip Highlight State (Phase 11c - FR-012, R-069)
    // ==========================================================================

    /// Whether the editor is open (gating the skip-highlight output parameters)
    std::atomic<bool> editorOpen_{false};

    /// Counts published skips; its low two bits go into kArpSkipLane*Id so a
    /// repeated step still changes the parameter value (audio thread only)
    uint32_t skipPublishSeq_{0};

    // ==========================================================================
    // Crash-Proof Preset Loading (RTTransferT)
    // ==========================================================================

    /// Lock-free triple-buffer handing a preset load to the audio thread
    Steinberg::Vst::RTTransferT<PresetSnapshot> stateTransfer_;

    /// The one preset-load path (R-196): setState and the controller's
    /// "LoadPresetState" message both call it. Parses a whole component state
    /// into the parameter atomics and the voice routes, then hands the reset to
    /// the audio thread. With `arpOnly` only the arp pack is stored; the rest
    /// is parsed into scratch and dropped. Message thread only (allocates).
    Steinberg::tresult loadStateBytes(std::vector<char> bytes, bool arpOnly);

    /// The parser behind loadStateBytes: reads every pack of a component state
    /// into `packs` (this processor, or scratch with the same member names),
    /// the arp pack into `arp`. False when the stream is not a state this
    /// version reads or ends before the voice routes; nothing is handed on then.
    template <typename Packs>
    static bool readStatePacks(Packs& packs, ArpeggiatorParams& arp,
                               Steinberg::IBStreamer& streamer);

    /// Apply a preset load on the audio thread: resets only, no stream (R-197)
    void applyPresetSnapshot(const PresetSnapshot& snapshot);

    /// Drop what is playing (R-198): with `engineToo` the engine and the arp
    /// (activation, full preset load), otherwise the arp alone (arp-only
    /// preset load). The arp's MIDI-out notes are sent their NoteOffs by the
    /// next process() with an output list.
    void resetPlayingState(bool engineToo) noexcept;
};

} // namespace Ruinae
