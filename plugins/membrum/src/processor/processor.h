#pragma once

// ==============================================================================
// Membrum Processor -- Phase 5
// ==============================================================================
// Phase 5 adds cross-pad sympathetic resonance (coupling). The coupling engine
// (SympatheticResonance) processes after VoicePool output: pre-pan voice sum -> delay ->
// engine -> energy limiter -> add to L/R. Global coupling parameters (270-273)
// control the coupling amount, snare buzz, tom resonance, and propagation delay.
// ==============================================================================

#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "public.sdk/source/vst/vstaudioeffect.h"

#include "dsp/drum_voice.h"
#include "dsp/pad_config.h"
#include "dsp/pad_category.h"
#include "dsp/coupling_matrix.h"
#include "dsp/pad_glow_publisher.h"
#include "voice_pool/voice_pool.h"

#include <krate/dsp/systems/sympathetic_resonance.h>
#include <krate/dsp/primitives/delay_line.h>
#include <krate/dsp/processors/true_peak_limiter.h>

#include "state/state_codec.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <memory>
#include <mutex>

// Forward-declare DataExchangeHandler so processor.h stays cheap to include.
namespace Steinberg::Vst { class DataExchangeHandler; }

namespace Membrum {

class Processor : public Steinberg::Vst::AudioEffect
{
public:
    Processor();
    ~Processor() override;

    static Steinberg::FUnknown* createInstance(void*)
    {
        return static_cast<Steinberg::Vst::IAudioProcessor*>(new Processor());
    }

    Steinberg::tresult PLUGIN_API initialize(Steinberg::FUnknown* context) override;
    Steinberg::tresult PLUGIN_API process(Steinberg::Vst::ProcessData& data) override;
    Steinberg::tresult PLUGIN_API setState(Steinberg::IBStream* state) override;
    Steinberg::tresult PLUGIN_API getState(Steinberg::IBStream* state) override;

    Steinberg::tresult PLUGIN_API activateBus(Steinberg::Vst::MediaType type,
                                              Steinberg::Vst::BusDirection dir,
                                              Steinberg::int32 index,
                                              Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API setBusArrangements(
        Steinberg::Vst::SpeakerArrangement* inputs, Steinberg::int32 numIns,
        Steinberg::Vst::SpeakerArrangement* outputs, Steinberg::int32 numOuts) override;
    Steinberg::tresult PLUGIN_API setupProcessing(Steinberg::Vst::ProcessSetup& setup) override;
    Steinberg::tresult PLUGIN_API setActive(Steinberg::TBool state) override;
    Steinberg::tresult PLUGIN_API setProcessing(Steinberg::TBool state) override;

    // Voices ring for up to about 35 s (decay clamp 5 s) and the coupling
    // resonators ring after them, so no finite tail is right. Idle detection is
    // the per-block silence flag set in Processor::process (T2.26).
    Steinberg::uint32 PLUGIN_API getTailSamples() override
    {
        return Steinberg::Vst::kInfiniteTail;
    }

    Steinberg::tresult PLUGIN_API connect(Steinberg::Vst::IConnectionPoint* other) override;
    Steinberg::tresult PLUGIN_API disconnect(Steinberg::Vst::IConnectionPoint* other) override;

    // T068 (Spec 141, retry): handle "CouplingMatrixEdit" and
    // "CouplingMatrixSnapshotRequest" messages from the controller. Replies
    // to snapshot requests with "CouplingMatrixSnapshot".
    Steinberg::tresult PLUGIN_API notify(Steinberg::Vst::IMessage* message) override;

    // Test-only accessors (Phase 4)
    VoicePool& voicePoolForTest() noexcept { return voicePool_; }
    const VoicePool& voicePoolForTest() const noexcept { return voicePool_; }
    void setSelectedPadIndexForTest(int idx) noexcept { selectedPadIndex_.store(idx); }
    int selectedPadIndexForTest() const noexcept { return selectedPadIndex_.load(); }

    /// Test-only: a test that wrote pad configs straight through
    /// voicePoolForTest() (behind the audio thread's back) republishes them so
    /// getState sees the write. Call only while no process() call can run.
    void publishPadConfigsForTest() noexcept { publishPadConfigs(); }

    // Test-only accessors (Phase 5: coupling)
    Krate::DSP::SympatheticResonance& couplingEngineForTest() noexcept { return couplingEngine_; }
    const CouplingMatrix& couplingMatrixForTest() const noexcept { return couplingMatrix_; }
    float energyEnvelopeForTest() const noexcept { return energyEnvelope_; }
    float applyEnergyLimiterForTest(float x) noexcept { return applyEnergyLimiter(x); }
    /// Test-only: how many times recomputeCouplingMatrix() has run (MB-075).
    std::uint32_t couplingMatrixRecomputeCountForTest() const noexcept
    {
        return couplingMatrixRecomputeCount_;
    }

    // Test-only accessors (Phase 6: publishers)
    PadGlowPublisher& padGlowPublisherForTest() noexcept { return padGlowPublisher_; }
    bool editorOpenForTest() const noexcept { return editorOpen_.load(); }
    void setEditorOpenForTest(bool open) noexcept { editorOpen_.store(open); }

private:
    void processParameterChanges(Steinberg::Vst::IParameterChanges* paramChanges);
    void processEvents(Steinberg::Vst::IEventList* events);
    /// One note event, applied at the current render position (T1.14).
    void applyNoteEvent(const Steinberg::Vst::Event& event);
    /// Applies the queued editor audition events in order (T2.4). A pad-up
    /// for a pad whose pad-down was applied in the same call stays queued
    /// for the next call, so a click shorter than one block still sounds.
    void consumePendingAudition();

    /// Applies a parsed kit: the one apply that setState and a kit-preset
    /// browser load ("LoadPresetState", R-202) share. Fades the sounding
    /// voices and the coupling tail out over 5 ms, then flushes the coupling
    /// engine (T2.3); installs every pad and global. Runs only through consumePendingLoad (MB-001): on the audio
    /// thread, or while processing is stopped.
    void applyKitSnapshot(const State::KitSnapshot& kit);

    /// The kit itself, without the audio-state reset: polyphony and stealing,
    /// every pad, the choke table, the selected pad, the globals and master
    /// gain, the pad categories and the coupling matrix. applyKitSnapshot
    /// calls it; initialize installs the default kit through it alone (T2.7,
    /// MB-116), before any DSP is prepared.
    void installKit(const State::KitSnapshot& kit);

    /// Flushes the coupling engine, the coupling delay and the energy
    /// envelope, then applies the stored Global Coupling (T1.19, MB-067,
    /// MB-115, MB-094). Runs after every coupling prepare, on activation and
    /// on a kit apply, so the stored amount is always the live one.
    void resetCouplingState() noexcept;

    /// The category of what `pad` plays: classifyPad of its config with the
    /// macro offsets applied (T1.22).
    [[nodiscard]] PadCategory effectivePadCategory(int pad) const noexcept;

    /// Applies a parsed pad preset to one pad through
    /// State::applyPadPresetSnapshot (the one pad-preset apply, R-203), then
    /// refreshes the pad's category and the coupling matrix.
    void applyPadPresetSnapshot(int pad, const State::PadPresetSnapshot& preset);

    // ---- Preset-load hand-off (MB-001) ----------------------------------
    // notify("LoadPresetState" / "LoadPadPreset") and setState run on the
    // host's message thread. They never write the engine while processing is
    // active: each parses into pendingLoad_ and publishes it through
    // pendingSlot_; process() applies it at its top, before the block's
    // parameter changes. Contract: processor.cpp, "Preset-load hand-off".

    /// A parsed load not yet applied. Fixed size, allocated once in the
    /// constructor; the audio thread never allocates while applying it.
    struct PendingLoad
    {
        bool hasKit = false;
        State::KitSnapshot kit{};
        bool keepSelectedPad = false;  ///< kit message: FR-052 keeps the pad
        std::array<bool, kNumPads> hasPadPreset{};
        std::array<State::PadPresetSnapshot, kNumPads> padPresets{};
    };

    /// Slot states of pendingSlot_.
    static constexpr int kSlotIdle = 0;      ///< nothing pending
    static constexpr int kSlotWriting = 1;   ///< a host-thread call owns pendingLoad_
    static constexpr int kSlotReady = 2;     ///< published, not yet applied
    static constexpr int kSlotApplying = 3;  ///< being applied (audio thread, or processing stopped)

    /// Takes pendingSlot_ for a host-thread call (yields while it is being
    /// applied). Returns the state it held: kSlotIdle or kSlotReady.
    int acquireLoadSlot() noexcept;

    /// The one publish of the three load paths: takes lifecycleMutex_, lets
    /// `fill` write pendingLoad_ (`fill(load)`), then hands it to the audio
    /// thread, or applies it at once when processing is not active.
    template <typename Fill>
    void publishLoad(const Fill& fill);

    /// Applies a published pendingLoad_ if there is one: the kit first, then
    /// each pad preset in pad order. Audio thread at the top of process();
    /// setActive / setupProcessing / an inactive load while processing is
    /// stopped. Allocation-free and lock-free.
    void consumePendingLoad() noexcept;

    /// Copies the live pad configs into the triple buffer and publishes them
    /// for getState. Single writer: the audio thread while active, the
    /// lifecycle calls while inactive.
    void publishPadConfigs() noexcept;

    /// The newest published pad configs. Single reader: getState, under
    /// lifecycleMutex_.
    const std::array<PadConfig, kNumPads>& latestPublishedPadConfigs() noexcept;

    /// Phase 6 (US4): recompute the coupling matrix passing the current
    /// per-pad couplingAmount values so they are baked into effectiveGain
    /// (FR-014, FR-023). Called whenever a Tier 1 knob, pad category, or
    /// per-pad coupling amount changes.
    void recomputeCouplingMatrix() noexcept;

    // DSP
    VoicePool voicePool_;
    double sampleRate_ = 44100.0;
    int    maxBlockSize_ = 2048;

    // ---- Global-only parameters (not per-pad) ----
    std::atomic<int> maxPolyphony_{8};           // FR-111 -- [4, 16]
    std::atomic<int> voiceStealingPolicy_{0};    // FR-120 -- VoiceStealingPolicy int

    // ---- Editor audition events (T2.4, MB-006, MB-055) ----
    // "AuditionPad" (pad-down) and "AuditionPadOff" (pad-up, velocity 0) from
    // the controller. One word per event:
    //   bits 0..6  = MIDI pitch (0-127)
    //   bits 7..13 = velocity (0-127; 0 = pad-up)
    //   bit  14    = valid flag
    /// Lock-free single-producer single-consumer ring of audition words. The
    /// producer is notify() on the message thread, the consumer process().
    /// A full ring refuses the incoming event; an unread one is never
    /// overwritten.
    struct AuditionEventQueue
    {
        static constexpr std::uint32_t kCapacity = 16;  // power of two
        static constexpr std::uint32_t kMask = kCapacity - 1u;

        /// Producer only. False when the ring is full (the event is dropped).
        bool push(std::uint32_t word) noexcept
        {
            const std::uint32_t w = write_.load(std::memory_order_relaxed);
            const std::uint32_t r = read_.load(std::memory_order_acquire);
            if (w - r >= kCapacity)
                return false;
            slots_[w & kMask] = word;
            write_.store(w + 1u, std::memory_order_release);
            return true;
        }

        /// Consumer only. The oldest unread word, left in the ring.
        bool peek(std::uint32_t& word) const noexcept
        {
            const std::uint32_t r = read_.load(std::memory_order_relaxed);
            if (r == write_.load(std::memory_order_acquire))
                return false;
            word = slots_[r & kMask];
            return true;
        }

        /// Consumer only. Drops the word peek() returned.
        void pop() noexcept
        {
            read_.store(read_.load(std::memory_order_relaxed) + 1u,
                        std::memory_order_release);
        }

    private:
        std::array<std::uint32_t, kCapacity> slots_{};
        std::atomic<std::uint32_t> write_{0};
        std::atomic<std::uint32_t> read_{0};
    };
    AuditionEventQueue auditionQueue_;
    /// Set by setProcessing(false) / setActive(false): the next
    /// consumePendingAudition() drops every queued event, so a request posted
    /// while the host was not processing never fires on resume. Only the
    /// consumer clears the ring.
    std::atomic<bool> discardAudition_{false};

    // ---- Phase 4: per-pad parameters are stored in VoicePool::padConfigs_ ----
    // No individual atomic<float> fields for material/size/decay etc. anymore.
    // The processor dispatches per-pad parameter changes directly to VoicePool.

    // ---- Phase 4: bus activation tracking ----
    std::array<bool, kMaxOutputBuses> busActive_ = {true};  // bus 0 always active

    // ---- Phase 4: selected pad index (for state only, not used in audio) ----
    // Written by processParameterChanges (audio thread), read by getState.
    std::atomic<int> selectedPadIndex_{0};

    // ---- Preset-load hand-off (MB-001) ----
    std::unique_ptr<PendingLoad> pendingLoad_;
    std::atomic<int> pendingSlot_{kSlotIdle};

    /// True between setActive(true) and setActive(false): process() may run,
    /// so a load defers to it.
    std::atomic<bool> active_{false};

    /// Serialises setActive, setupProcessing, setState, the preset-load
    /// messages and getState. Never taken by process(). A host that calls
    /// setActive on one thread while setState runs on another cannot make
    /// the load read "inactive" and then write the engine after processing
    /// began; getState never saves a load half-applied.
    std::mutex lifecycleMutex_;

    // Published pad configs for getState (triple buffer: the writer owns
    // padCopyBack_, the reader padCopyFront_, padCopyMiddle_ holds the third
    // index plus kPadCopyFresh when it is newer than the reader's).
    static constexpr std::uint8_t kPadCopyIndexMask = 0x3u;
    static constexpr std::uint8_t kPadCopyFresh = 0x4u;
    std::unique_ptr<std::array<std::array<PadConfig, kNumPads>, 3>> padCopies_;
    std::atomic<std::uint8_t> padCopyMiddle_{1};
    std::uint8_t padCopyBack_ = 0;
    std::uint8_t padCopyFront_ = 2;

    /// A pad config changed in this block (parameter changes, a load apply):
    /// publish at the block's end. Audio thread (or a lifecycle call while
    /// inactive) only.
    bool padsDirty_ = false;

    // ---- Phase 5: Cross-pad coupling (sympathetic resonance) ----
    Krate::DSP::SympatheticResonance  couplingEngine_;
    Krate::DSP::DelayLine             couplingDelay_;
    // Main-bus true-peak brickwall limiter (gain-staging Step 1). Bounds the
    // summed voice + coupling output to -1 dBTP before the master gain so the
    // per-voice path can run linear (no per-voice softClip gain stage).
    Krate::DSP::TruePeakLimiter       busLimiter_;
    CouplingMatrix                    couplingMatrix_;
    std::uint32_t couplingMatrixRecomputeCount_ = 0;  ///< test-only rebuild counter

    // Global coupling parameters (atomics for thread-safe parameter updates)
    std::atomic<float> globalCoupling_{0.0f};
    std::atomic<float> snareBuzz_{0.0f};
    std::atomic<float> tomResonance_{0.0f};
    std::atomic<float> couplingDelayMs_{1.0f};

    // Phase 9: global master output gain. Stored as normalized [0..1] mapping
    // linearly to [-24..+12] dB. Default 0.5 -> -6 dB to give the kit a -6 dB
    // headroom margin out-of-the-box (templates run hot at +0 dB master).
    std::atomic<float> masterGainNorm_{0.5f};

    // Cached pad categories (recomputed on pad config changes)
    std::array<PadCategory, kNumPads> padCategories_{};

    // Energy limiter state
    float energyEnvelope_ = 0.0f;

    // ---- T2.3 (MB-054, MB-091): kit-switch fades. Audio thread only (or a
    // lifecycle call while processing is stopped). ----
    /// Length of a kit-switch fade in samples: the voices' 5 ms fast release
    /// (kFastReleaseSecs) at the current rate, plus two samples of margin.
    int   kitFadeSamples_ = 0;
    /// Samples left in the coupling-return fade; at 0 the engine is flushed
    /// and the new kit's Global Coupling applied (finishCouplingFade).
    int   couplingFadeRemaining_ = 0;
    /// Samples until the old kit's voices have faded; then the bus limiter
    /// is reset when nothing sounds, so the new kit starts at unity gain.
    int   kitFadeRemaining_ = 0;
    /// Master gain applied to the last rendered sample. T2.10 (MB-059): a new
    /// target starts a geometric ramp of kMasterGainRampSeconds (equal dB per
    /// sample) from this value, carried across block boundaries, so the gain
    /// curve does not depend on the host block size.
    float masterGainApplied_ = 1.0f;
    /// The gain the ramp ends on (the last target read from masterGainNorm_).
    float masterGainTarget_ = 1.0f;
    /// Per-sample gain ratio of the ramp in flight.
    float masterGainRatio_ = 1.0f;
    /// Samples left in the ramp; at 0 the gain is set to the target exactly.
    int   masterGainRampRemaining_ = 0;
    /// Ramp length in samples at the current rate (setupProcessing).
    int   masterGainRampSamples_ = 0;

    /// Applies `gain` at once, ending any ramp (activation, a rate change, a
    /// kit load with nothing sounding).
    void snapMasterGain(float gain) noexcept;

    /// The coupling fade has ended: flush the engine (resetCouplingState,
    /// which applies the stored Global Coupling) and re-register the voices
    /// struck during the fade.
    void finishCouplingFade() noexcept;

    // ---- Phase 6: publishers, meters DataExchange, editor state ----
    PadGlowPublisher         padGlowPublisher_;
    std::unique_ptr<Steinberg::Vst::DataExchangeHandler> dataExchangeHandler_;
    std::atomic<bool>        editorOpen_{false};

    /// Phase 6 (T045): EWMA-smoothed CPU usage (per-mille) measured as the
    /// ratio of process() work time to block budget. Updated every block from
    /// the audio thread and read back into MetersBlock. `steady_clock::now()`
    /// is allocation-free on all supported platforms.
    float cpuPermilleEwma_ = 0.0f;

    /// T2.31 (MB-065): main-bus peaks and pad-glow buckets folded (max) over
    /// every block since the last MetersBlock that was sent. A block the
    /// DataExchange queue cannot take keeps its peak here for the next send.
    /// Audio thread only; cleared on each successful send and in setActive.
    float metersPeakAccL_ = 0.0f;
    float metersPeakAccR_ = 0.0f;
    std::array<std::uint8_t, kNumPads> metersGlowAcc_{};

    /// Release time constant of the coupling energy limiter. T2.26 (MB-015):
    /// a time in seconds, so it is the same at every host rate; 0.2267 s is
    /// the former fixed 10,000-sample constant at 44.1 kHz.
    static constexpr float kEnergyLimiterReleaseSec = 0.2267f;
    /// Per-sample release coefficient at the current rate (setupProcessing).
    float energyReleaseCoeff_ = 0.9999f;

    /// T2.26 (MB-064, MB-182): samples for which no voice and no coupling
    /// resonator has been active. The coupling loop stops once this reaches
    /// couplingIdleHoldSamples_ (the 2 ms maximum coupling delay plus a
    /// 100 ms hold, set in setupProcessing). Audio thread only.
    int couplingIdleSamples_ = 0;
    int couplingIdleHoldSamples_ = 0;

    /// Envelope-follower energy limiter (FR-014, SC-007). Caps the coupling
    /// output at -20 dBFS. T2.26 (MB-015): the attack is a peak hold (the
    /// envelope never sits below |x|, so the cap holds from the first sample
    /// at any rate) and the release is kEnergyLimiterReleaseSec.
    float applyEnergyLimiter(float sample) noexcept
    {
        constexpr float kThreshold = 0.1f;     // -20 dBFS
        const float absVal = std::abs(sample);
        if (absVal > energyEnvelope_)
            energyEnvelope_ = absVal;
        else
            energyEnvelope_ += (1.0f - energyReleaseCoeff_) * (absVal - energyEnvelope_);
        if (energyEnvelope_ > kThreshold)
        {
            // The clamp removes the one-ulp overshoot of the gain's rounding,
            // so the cap is exact.
            return std::clamp(sample * (kThreshold / energyEnvelope_),
                              -kThreshold, kThreshold);
        }
        return sample;
    }
};

} // namespace Membrum
