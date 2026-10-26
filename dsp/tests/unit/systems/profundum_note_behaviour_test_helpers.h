// ==============================================================================
// Test helpers: NoteBehaviourHarness and Phase 4 measurement helpers
//                        (Profundum Phase 4, specs/profundum-phase4-drop-note-behaviour)
// ==============================================================================
// Plan S8 (test composition), S11.2 (shared helpers); tasks.md T019 (created), T020+ (append only).
//
// NoteBehaviourHarness is the TEST-ONLY composition of the Phase 4 note behaviour around one voice:
//   MonoHandler (f_res) -> DropEnvelope (d) -> DropPhaseLock (d_c) -> f_core = f_res * exp2(d_c / 12)
//   ProfundumCore(f_core) + AnchorOscillator(f_res, or f_core in FOLLOW)
//   out = (core + anchor) * declick gain * ADSR, the same float on L and R.
// NoteMemory classifies and resolves each event (plan S8 steps 1-7). Phase 9's ProfundumVoice
// replaces it.
//
// The event / render path is allocation-free and Catch-free (SC-018 wraps it in an
// AllocationScope): fixed-size arrays sized for 512-sample host blocks; harness contract breaches
// are counted (contractViolations()), never asserted inline. Traces are preallocated std::vectors
// (NoteBehaviourTraces::allocate, outside any AllocationScope).
//
// Sample positions (eventSample, effectiveSample, call log) count samples rendered since prepare();
// a NoteBehaviourTraces allocated right after prepare() therefore indexes the same positions.
//
// Plan deviations (surfaced):
//   - tasks.md deviation 6: the Anchor's a1 is core.deliveredGains()[0] * kCenterPanGain (the Phase 3
//     chain-test form, profundum_anchor_chain_test.cpp:745), set after the core renders each sub-block.
//   - anchor.noteOn takes the Anchor's own trajectory value at the effective sample (f_res in LOCK,
//     PUNCH, GHOST, exactly as plan S8 step 6; f_core in FOLLOW, where the Anchor is fed f_core).
//   - The core is rendered in chunks that end on its 16-sample pitch grid, so currentFrequency()
//     after a chunk is the held f0 of every sample in that chunk (exact per-sample d_core trace,
//     D-15, at no per-sample cost; the core already splits its runs on that grid,
//     profundum_core.h:253-266). The Anchor is rendered one sample per call so phase(),
//     phaseIncrement() and modeGain() are read per sample (Phase 3 renderAnchor perSample form).
//   - Outcome records live in the harness (fixed array) and are copied into the traces after
//     every render().
//
// Every helper lives in namespace Krate::DSP::ProfundumTest with a noteBehaviour / NoteBehaviour /
// NoteOutcome prefix (ODR sweep 2026-10-13: no other definition in dsp/ or plugins/).
// ==============================================================================

#pragma once

#include "anchor_oscillator_test_helpers.h"
#include "profundum_core_test_helpers.h"
#include "../processors/drop_c1_checker.h"

#include <krate/dsp/core/drop_curve.h>
#include <krate/dsp/core/midi_utils.h>
#include <krate/dsp/primitives/adsr_envelope.h>
#include <krate/dsp/primitives/retrigger_declick.h>
#include <krate/dsp/processors/drop_envelope.h>
#include <krate/dsp/processors/drop_phase_lock.h>
#include <krate/dsp/processors/mono_handler.h>
#include <krate/dsp/processors/note_memory.h>
#include <krate/dsp/processors/spectral_shape_recipe.h>
#include <krate/dsp/systems/anchor_oscillator.h>
#include <krate/dsp/systems/profundum_core.h>
#include <krate/dsp/systems/voice_allocator.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>
#include <vector>

namespace Krate::DSP::ProfundumTest {

// =============================================================================
// Event outcomes (plan S8 "Harness self-test")
// =============================================================================

/// One event's outcome: a NoteMemory::Transition (same underlying values), Release (the last held
/// key released: ADSR gate off, nothing resolved) or NoAction (a non-transition event).
enum class NoteOutcome : std::uint8_t {
    FromSilence = 0,
    Overlap = 1,
    Tail = 2,
    ReturnToHeld = 3,
    VoiceReuse = 4,
    Release = 5,
    NoAction = 6
};
static_assert(static_cast<std::uint8_t>(NoteOutcome::FromSilence)
              == static_cast<std::uint8_t>(NoteMemory::Transition::FromSilence));
static_assert(static_cast<std::uint8_t>(NoteOutcome::Overlap)
              == static_cast<std::uint8_t>(NoteMemory::Transition::Overlap));
static_assert(static_cast<std::uint8_t>(NoteOutcome::Tail) == static_cast<std::uint8_t>(NoteMemory::Transition::Tail));
static_assert(static_cast<std::uint8_t>(NoteOutcome::ReturnToHeld)
              == static_cast<std::uint8_t>(NoteMemory::Transition::ReturnToHeld));
static_assert(static_cast<std::uint8_t>(NoteOutcome::VoiceReuse)
              == static_cast<std::uint8_t>(NoteMemory::Transition::VoiceReuse));

inline const char* noteOutcomeName(NoteOutcome o) {
    switch (o) {
        case NoteOutcome::FromSilence:
            return "FromSilence";
        case NoteOutcome::Overlap:
            return "Overlap";
        case NoteOutcome::Tail:
            return "Tail";
        case NoteOutcome::ReturnToHeld:
            return "ReturnToHeld";
        case NoteOutcome::VoiceReuse:
            return "VoiceReuse";
        case NoteOutcome::Release:
            return "Release";
        case NoteOutcome::NoAction:
            return "NoAction";
    }
    return "?";
}

inline constexpr std::size_t kNoteBehaviourNoSample = std::numeric_limits<std::size_t>::max();

/// What the harness did with one event.
struct NoteOutcomeRecord {
    NoteOutcome outcome = NoteOutcome::NoAction;
    std::size_t eventSample = kNoteBehaviourNoSample;      // sample the event arrived at
    std::size_t effectiveSample = kNoteBehaviourNoSample;  // sample its actions applied at (transitions)
    int note = -1;                                          // the active note after the event (-1: none)
    bool audible = false;                                   // env.isActive() when the event arrived
    NoteMemory::Actions actions{};                          // the resolved actions (transitions only)
    bool lockTriggered = false;                             // DropPhaseLock::trigger was issued
    DropPhaseLock::TriggerKind lockKind = DropPhaseLock::TriggerKind::SnapReset;
    bool coreResetBranch = false;                           // the core took its Reset branch (policy Reset or first note)
    bool superseded = false;                                // a later event replaced it during a dip (FR-043)
};

/// Element-by-element equality of an expected outcome list and the recorded one (plan S8).
inline bool noteOutcomesMatch(std::span<const NoteOutcome> expected, std::span<const NoteOutcomeRecord> recorded) {
    if (expected.size() != recorded.size())
        return false;
    for (std::size_t i = 0; i < expected.size(); ++i) {
        if (expected[i] != recorded[i].outcome)
            return false;
    }
    return true;
}

/// One recorded call on the core or the Anchor (SC-012(a) call log).
struct NoteBehaviourCall {
    enum class Target : std::uint8_t { Core, Anchor };
    enum class Kind : std::uint8_t { SetRetriggerPhase, NoteOn };
    std::size_t sample = 0;
    Target target = Target::Core;
    Kind kind = Kind::SetRetriggerPhase;
    NoteMemory::PhasePolicy policy = NoteMemory::PhasePolicy::Reset;   // SetRetriggerPhase
    float frequency = 0.0f;                                            // NoteOn
    bool rearm = false;                                                // Anchor NoteOn
};

// =============================================================================
// Traces (preallocated; plan S8 "Event API")
// =============================================================================

struct NoteBehaviourTraces {
    static constexpr std::uint8_t kMarkEvent = 0x1;    // an event arrived at this sample
    static constexpr std::uint8_t kMarkApplied = 0x2;  // transition actions applied at this sample
    static constexpr std::uint8_t kMarkArmed = 0x4;    // the declick was armed (or re-targeted) here

    std::vector<float> L, R;                 // composition output
    std::vector<float> coreL, coreR;         // core output (pre-gain)
    std::vector<float> anchorL, anchorR;     // Anchor output (pre-gain)
    std::vector<float> fRes, d, dc, fCore;   // f_res, Drop d, corrected d_c, f_core = f_res * exp2(d_c / 12)
    std::vector<float> fAnchor;              // the trajectory fed to the Anchor
    std::vector<float> coreFreq;             // core currentFrequency() at each sample (16-sample staircase)
    std::vector<double> anchorPhase;         // Anchor phase() after the sample
    std::vector<double> anchorInc;           // Anchor phaseIncrement() applied after the sample
    std::vector<double> modeGain;            // Anchor modeGain() at the sample
    std::vector<float> anchorA1;             // the Anchor's smoothed a1 at the sample (harness replica of its S4.9
                                             // a1 smoother over the level feed; 0 while the Anchor is silent)
    std::vector<float> gain;                // declick gain (1.0f while inactive)
    std::vector<float> env;                  // ADSR output
    std::vector<std::uint8_t> stage;         // ADSRStage after the sample
    std::vector<std::uint8_t> marks;         // kMark* bits
    std::vector<float> coreDeltaBound;       // analytic bound on |coreL[n] - coreL[n-1]| from the delivered gains
                                             // (SC-010(b) D, ruling (B'); 0 at a Reset-branch noteOn sample)
    std::vector<NoteOutcomeRecord> outcomes; // copied from the harness after every render
    std::size_t numOutcomes = 0;
    std::size_t length = 0;                  // samples written

    /// Allocates n samples per trace (call outside any AllocationScope).
    void allocate(std::size_t n, std::size_t maxOutcomes = 128) {
        for (auto* v : {&L, &R, &coreL, &coreR, &anchorL, &anchorR, &fRes, &d, &dc, &fCore, &fAnchor, &coreFreq,
                        &anchorA1, &gain, &env, &coreDeltaBound})
            v->assign(n, 0.0f);
        for (auto* v : {&anchorPhase, &anchorInc, &modeGain})
            v->assign(n, 0.0);
        stage.assign(n, std::uint8_t{0});
        marks.assign(n, std::uint8_t{0});
        outcomes.assign(maxOutcomes, NoteOutcomeRecord{});
        numOutcomes = 0;
        length = 0;
    }

    [[nodiscard]] std::size_t capacity() const noexcept { return L.size(); }
    [[nodiscard]] std::span<const NoteOutcomeRecord> recorded() const noexcept {
        return {outcomes.data(), numOutcomes};
    }
};

// =============================================================================
// NoteBehaviourHarness (plan S8)
// =============================================================================

/// Heavy (it owns a ProfundumCore): construct with std::make_unique.
class NoteBehaviourHarness {
public:
    static constexpr std::size_t kMaxBlock = 512;
    static constexpr std::size_t kMaxEvents = 64;
    static constexpr std::size_t kMaxOutcomes = 128;
    static constexpr std::size_t kMaxCalls = 1024;
    static constexpr int kNumPartials = 64;
    static constexpr float kAttackMs = 2.0f;
    static constexpr float kDecayMs = 200.0f;
    static constexpr float kSustain = 0.3f;
    static constexpr float kReleaseMs = 300.0f;

    // C-14: the harness passes the core's own clamp constants to DropPhaseLock::trigger.
    static_assert(DropPhaseLock::kPitchUpdateInterval == ProfundumCore::kPitchUpdateInterval);

    /// Prepares every component (not RT-safe: the core's prepare calibrates its bank) and clears the
    /// timeline, the event queue, the outcome list and the call log. Component settings made before
    /// prepare() are kept only where the component keeps them; configure after prepare().
    void prepare(double sampleRate) {
        fs_ = sampleRate;
        core_.prepare(sampleRate, kNumPartials);
        core_.setExcitation(ProfundumCore::kDefaultExcitation);   // excitation off (mix 0)
        core_.setControls(SpectralShapeRecipe::kDefaultControls);
        anchor_.prepare(sampleRate);
        a1Model_.configure(AnchorOscillator::kLevelSmoothMs, static_cast<float>(sampleRate));   // as anchor :160
        anchorPolicyReset_ = false;
        env_.prepare(static_cast<float>(sampleRate));
        env_.setAttack(kAttackMs);
        env_.setDecay(kDecayMs);
        env_.setSustain(kSustain);
        env_.setRelease(kReleaseMs);
        env_.setRetriggerMode(RetriggerMode::Hard);   // D-10
        env_.reset();
        mono_.prepare(sampleRate);
        mono_.setLegato(false);                       // D-11
        memory_ = NoteMemory{};
        drop_.prepare(sampleRate);
        lock_.prepare(sampleRate);
        declick_.prepare(sampleRate);
        capHz_ = SpectralShapeRecipe::capFrequency(sampleRate);
        ampCoeff_ = 1.0 - std::exp(-1.0 / (static_cast<double>(HarmonicOscillatorBank::kAmpSmoothTimeSec) * sampleRate));
        ampModel_.fill(0.0);
        ampRadius_.fill(1.0);
        coreResetPending_ = false;
        followRouting_ = false;
        forceLockBypass_ = false;
        lock_.setBypass(false);
        useDeclick_ = true;
        pos_ = 0;
        gridCount_ = 0;
        numEvents_ = 0;
        numOutcomes_ = 0;
        numCalls_ = 0;
        violations_ = 0;
        activeNote_ = -1;
        coreSounding_ = false;
        dipPending_ = false;
        switchPos_ = kNoteBehaviourNoSample;
        clearOffsetZeroFlags();
    }

    // ---- configuration (control side) ----

    /// Sets the Anchor controls; FOLLOW routes f_core to the Anchor and bypasses the lock (FR-058).
    void setAnchorControls(const AnchorOscillator::Controls& c) noexcept {
        anchor_.setControls(c);
        followRouting_ = c.mode == AnchorOscillator::Mode::Follow;
        lock_.setBypass(followRouting_ || forceLockBypass_);
    }
    /// Forces DropPhaseLock bypass in any Anchor mode (SC-013 negative control, SC-026).
    void setLockBypass(bool bypass) noexcept {
        forceLockBypass_ = bypass;
        lock_.setBypass(followRouting_ || forceLockBypass_);
    }
    /// false: declick-requiring transitions apply at once, with no dip (SC-010 / SC-011 controls).
    void setUseDeclick(bool on) noexcept { useDeclick_ = on; }
    void setDrop(float amountSemitones, float timeMs, float shape, float rebound) noexcept {
        drop_.setAmount(amountSemitones);
        drop_.setTime(timeMs);
        drop_.setShape(shape);
        drop_.setRebound(rebound);
    }
    void setNoteMemory(NoteMemory::Mode m, NoteMemory::Retrigger r) noexcept {
        memory_.setMode(m);
        memory_.setRetrigger(r);
    }

    // ---- component access (tests configure and observe through these) ----

    [[nodiscard]] ProfundumCore& core() noexcept { return core_; }
    [[nodiscard]] AnchorOscillator& anchor() noexcept { return anchor_; }
    [[nodiscard]] ADSREnvelope& envelope() noexcept { return env_; }
    [[nodiscard]] MonoHandler& mono() noexcept { return mono_; }
    [[nodiscard]] NoteMemory& memory() noexcept { return memory_; }
    [[nodiscard]] DropEnvelope& drop() noexcept { return drop_; }
    [[nodiscard]] DropPhaseLock& lock() noexcept { return lock_; }
    [[nodiscard]] RetriggerDeclick& declick() noexcept { return declick_; }
    [[nodiscard]] double sampleRate() const noexcept { return fs_; }
    [[nodiscard]] float capHz() const noexcept { return capHz_; }
    [[nodiscard]] std::size_t position() const noexcept { return pos_; }
    [[nodiscard]] int activeNote() const noexcept { return activeNote_; }
    [[nodiscard]] std::size_t contractViolations() const noexcept { return violations_; }
    [[nodiscard]] std::span<const NoteOutcomeRecord> outcomes() const noexcept {
        return {outcomes_.data(), numOutcomes_};
    }
    [[nodiscard]] std::span<const NoteBehaviourCall> callLog() const noexcept { return {calls_.data(), numCalls_}; }

    // ---- events (offset: samples after the start of the next render() call) ----

    void noteOn(int note, int velocity, std::size_t offset) noexcept { enqueue(EventKind::On, note, velocity, offset); }
    void noteOff(int note, std::size_t offset) noexcept { enqueue(EventKind::Off, note, 0, offset); }
    /// POLY: this voice is re-assigned by a Hard steal (SC-009(c)).
    void voiceReuse(int note, int velocity, std::size_t offset) noexcept {
        enqueue(EventKind::Reuse, note, velocity, offset);
    }
    /// Queues the events a VoiceAllocator emitted for voice `voiceIndex` (Steal + NoteOn -> VoiceReuse).
    void queueVoiceEvents(std::span<const VoiceEvent> events, std::size_t voiceIndex, std::size_t offset) noexcept {
        bool stolen = false;
        for (const VoiceEvent& e : events) {
            if (static_cast<std::size_t>(e.voiceIndex) != voiceIndex)
                continue;
            switch (e.type) {
                case VoiceEvent::Type::Steal:
                    stolen = true;
                    break;
                case VoiceEvent::Type::NoteOn:
                    if (stolen)
                        voiceReuse(static_cast<int>(e.note), static_cast<int>(e.velocity), offset);
                    else
                        noteOn(static_cast<int>(e.note), static_cast<int>(e.velocity), offset);
                    stolen = false;
                    break;
                case VoiceEvent::Type::NoteOff:
                    noteOff(static_cast<int>(e.note), offset);
                    break;
            }
        }
    }

    // ---- render ----

    /// Renders nSamples in host blocks of hostBlock (clamped to [1, kMaxBlock]); events and the
    /// declick switch split the blocks (plan S8 step 5). Traces are written from traces.length on.
    void render(std::size_t nSamples, std::size_t hostBlock, NoteBehaviourTraces& traces) noexcept {
        if (hostBlock == 0 || hostBlock > kMaxBlock) {
            ++violations_;
            hostBlock = std::clamp<std::size_t>(hostBlock, 1, kMaxBlock);
        }
        const std::size_t end = pos_ + nSamples;
        while (pos_ < end) {
            const std::size_t blockEnd = std::min(pos_ + hostBlock, end);
            while (pos_ < blockEnd) {
                handleEventsAt();
                std::size_t next = blockEnd;
                if (numEvents_ > 0)
                    next = std::min(next, events_[0].at);
                if (dipPending_)
                    next = std::min(next, switchPos_);
                if (next <= pos_)
                    next = pos_ + 1;   // unreachable: handleEventsAt consumed everything at pos_
                renderSub(next - pos_, traces);
            }
        }
        const std::size_t n = std::min(numOutcomes_, traces.outcomes.size());
        std::copy_n(outcomes_.begin(), n, traces.outcomes.begin());
        traces.numOutcomes = n;
        if (n < numOutcomes_)
            ++violations_;
    }

private:
    enum class EventKind : std::uint8_t { On, Off, Reuse };
    struct QueuedEvent {
        std::size_t at = 0;
        int note = 0;
        int velocity = 0;
        EventKind kind = EventKind::On;
    };
    struct PendingTransition {
        NoteMemory::Actions actions{};
        std::size_t record = 0;
    };

    void clearOffsetZeroFlags() noexcept {
        lockTriggerPending_ = false;
        anchorNoteOnPending_ = false;
        coreNoteOnPending_ = false;
        anchorRearm_ = false;
        lockKind_ = DropPhaseLock::TriggerKind::SnapReset;
        pendingMark_ = 0;
    }

    void enqueue(EventKind kind, int note, int velocity, std::size_t offset) noexcept {
        if (numEvents_ >= kMaxEvents) {
            ++violations_;
            return;
        }
        const QueuedEvent ev{.at = pos_ + offset, .note = note, .velocity = velocity, .kind = kind};
        std::size_t i = numEvents_;
        while (i > 0 && events_[i - 1].at > ev.at) {   // stable: after every event at the same sample
            events_[i] = events_[i - 1];
            --i;
        }
        events_[i] = ev;
        ++numEvents_;
    }

    [[nodiscard]] static int noteFromFrequency(float hz) noexcept {
        for (int n = 0; n <= 127; ++n) {
            if (midiNoteToFrequency(n) == hz)
                return n;
        }
        return -1;
    }

    std::size_t addRecord(NoteOutcome o, bool audible) noexcept {
        if (numOutcomes_ >= kMaxOutcomes) {
            ++violations_;
            return kMaxOutcomes;   // sentinel: not stored
        }
        NoteOutcomeRecord& r = outcomes_[numOutcomes_];
        r = NoteOutcomeRecord{};
        r.outcome = o;
        r.eventSample = pos_;
        r.note = activeNote_;
        r.audible = audible;
        return numOutcomes_++;
    }

    void logCall(NoteBehaviourCall c) noexcept {
        if (numCalls_ >= kMaxCalls) {
            ++violations_;
            return;
        }
        calls_[numCalls_++] = c;
    }

    /// Plan S8 steps 1-5 for every queued event at pos_, then the deferred transition if pos_ is the
    /// declick switch sample.
    void handleEventsAt() noexcept {
        while (numEvents_ > 0 && events_[0].at <= pos_) {
            const QueuedEvent ev = events_[0];
            for (std::size_t i = 1; i < numEvents_; ++i)
                events_[i - 1] = events_[i];
            --numEvents_;
            pendingMark_ |= NoteBehaviourTraces::kMarkEvent;
            handleEvent(ev);
        }
        if (dipPending_ && switchPos_ == pos_) {
            dipPending_ = false;
            switchPos_ = kNoteBehaviourNoSample;
            applyTransition(pending_.actions, pending_.record);
        }
    }

    void handleEvent(const QueuedEvent& e) noexcept {
        // Step 1.
        const bool wasHeld = mono_.hasActiveNote();
        const bool audible = env_.isActive();
        NoteMemory::Transition t = NoteMemory::Transition::FromSilence;

        // Steps 2-3.
        if (e.kind == EventKind::Reuse) {
            if (activeNote_ >= 0)
                (void)mono_.noteOff(activeNote_);
            const MonoNoteEvent mev = mono_.noteOn(e.note, e.velocity);
            if (!mev.isNoteOn) {
                ++violations_;
                (void)addRecord(NoteOutcome::NoAction, audible);
                return;
            }
            activeNote_ = noteFromFrequency(mev.frequency);
            t = NoteMemory::Transition::VoiceReuse;
        } else if (e.kind == EventKind::On) {
            const MonoNoteEvent mev = mono_.noteOn(e.note, e.velocity);
            if (!mev.isNoteOn) {   // out-of-range note (or velocity 0 releasing): not a transition here
                ++violations_;
                (void)addRecord(NoteOutcome::NoAction, audible);
                return;
            }
            activeNote_ = noteFromFrequency(mev.frequency);
            t = !audible ? NoteMemory::Transition::FromSilence
                         : (wasHeld ? NoteMemory::Transition::Overlap : NoteMemory::Transition::Tail);
        } else {
            const MonoNoteEvent mev = mono_.noteOff(e.note);
            if (activeNote_ < 0) {   // nothing held: a stray release
                (void)addRecord(NoteOutcome::NoAction, audible);
                return;
            }
            if (!mev.isNoteOn) {   // the last held key released (mono_handler.h:259-262)
                env_.gate(false);
                activeNote_ = -1;
                (void)addRecord(NoteOutcome::Release, audible);
                return;
            }
            if (e.note != activeNote_) {   // a non-active held key (mono_handler.h:265-270)
                if (!(mev.frequency == midiNoteToFrequency(activeNote_)) || mev.retrigger)
                    ++violations_;
                (void)addRecord(NoteOutcome::NoAction, audible);
                return;
            }
            activeNote_ = noteFromFrequency(mev.frequency);   // the winner (exact for integer notes)
            t = NoteMemory::Transition::ReturnToHeld;
        }

        // Step 4.
        const NoteMemory::Actions a = memory_.resolve(t, audible);
        const std::size_t rec = addRecord(static_cast<NoteOutcome>(static_cast<std::uint8_t>(t)), audible);
        if (rec < kMaxOutcomes)
            outcomes_[rec].actions = a;

        // Step 5: the declick defers the transition to the switch sample (last call wins, FR-043).
        if (a.declick && useDeclick_) {
            declick_.arm();   // no-op while a dip runs: the switch sample stays
            pendingMark_ |= NoteBehaviourTraces::kMarkArmed;
            if (dipPending_ && pending_.record < kMaxOutcomes)
                outcomes_[pending_.record].superseded = true;
            pending_ = PendingTransition{.actions = a, .record = rec};
            dipPending_ = true;
            switchPos_ = pos_ + declick_.samplesUntilSwitch();
            return;
        }
        if (dipPending_) {   // any transition during a dip re-targets the pending one (FR-043)
            if (pending_.record < kMaxOutcomes)
                outcomes_[pending_.record].superseded = true;
            pending_ = PendingTransition{.actions = a, .record = rec};
            return;
        }
        applyTransition(a, rec);
    }

    /// Plan S8 step 6 at offset 0 of the sub-block starting at pos_; the f_res-dependent calls are
    /// latched and issued in renderSub once f_res[0] and d_c[0] are known.
    void applyTransition(const NoteMemory::Actions& a, std::size_t rec) noexcept {
        const auto corePolicy = static_cast<ProfundumCore::RetriggerPhase>(static_cast<std::uint8_t>(a.phase));
        const auto anchorPolicy = static_cast<AnchorOscillator::RetriggerPhase>(static_cast<std::uint8_t>(a.phase));
        core_.setRetriggerPhase(corePolicy);
        logCall({.sample = pos_, .target = NoteBehaviourCall::Target::Core,
                 .kind = NoteBehaviourCall::Kind::SetRetriggerPhase, .policy = a.phase});
        anchor_.setRetriggerPhase(anchorPolicy);
        anchorPolicyReset_ = anchorPolicy == AnchorOscillator::RetriggerPhase::Reset;   // a1 replica's snap rule
        logCall({.sample = pos_, .target = NoteBehaviourCall::Target::Anchor,
                 .kind = NoteBehaviourCall::Kind::SetRetriggerPhase, .policy = a.phase});

        // The core's Reset branch (profundum_core.h:528): policy Reset, or not sounding yet.
        const bool resetBranch = a.phase == NoteMemory::PhasePolicy::Reset || !coreSounding_;
        if (resetBranch) {
            gridCount_ = 0;            // step 7: the core's grid restarts at the note (intervalPhase_ = 0)
            coreResetPending_ = true;  // the bank re-seeds: amplitudes restart from 0 (ruling (B') D model)
        }

        if (a.dropTrigger) {
            drop_.trigger(a.dropSnap);
            lockTriggerPending_ = true;
            lockKind_ = !a.dropSnap ? DropPhaseLock::TriggerKind::Bridged
                                    : (resetBranch ? DropPhaseLock::TriggerKind::SnapReset
                                                   : DropPhaseLock::TriggerKind::SnapFreeRunning);
        }
        coreNoteOnPending_ = true;
        anchorNoteOnPending_ = true;
        anchorRearm_ = a.anchorRearm;
        if (a.envelopeRestart)
            env_.gate(true);   // D-10: Hard
        coreSounding_ = true;
        pendingMark_ |= NoteBehaviourTraces::kMarkApplied;

        if (rec < kMaxOutcomes) {
            NoteOutcomeRecord& r = outcomes_[rec];
            r.effectiveSample = pos_;
            r.coreResetBranch = resetBranch;
            r.lockTriggered = a.dropTrigger;
            r.lockKind = lockKind_;
        }
    }

    [[nodiscard]] std::size_t gridPhase() const noexcept {
        constexpr std::size_t U = ProfundumCore::kPitchUpdateInterval;
        return (U - gridCount_ % U) % U;
    }

    /// SC-010(b) D from the core's delivered gains (T023 epoch 6, ruling (B')): an analytic bound on the core's
    /// per-sample output change over [s, s + len) of the current sub-block, at the held f0 cf of that chunk. Model of
    /// the bank (harmonic_oscillator_bank.h): partial k is a sampled sinusoid of radius R_k = 1 / cos(w_k / 2) (MCF,
    /// :1118-1123) times a one-pole amplitude A_k (kAmpSmoothTimeSec, :139-141, :756) chasing delivered_k / headroom *
    /// cos(w_k / 2) * aa_k (:755, :1105-1133); the core multiplies the headroom back (profundum_core.h:270-279) and
    /// L == R carries kCenterPanGain, so in output units the chase target is delivered_k * cos(w_k / 2) * aa_k.
    /// Per sample: |A_k(n) s_k(n) - A_k(n-1) s_k(n-1)| <= A_k(n) R_k 2 sin(w_k / 2) + |dA_k| R_k + A_k(n-1) |dR_k|.
    /// A Reset-branch noteOn re-seeds the bank with A = 0 (:288-297); its first sample carries no slew term: the
    /// restart itself is what the declick hides, so the bound never absorbs it.
    void coreDeltaBoundChunk(std::size_t s, std::size_t len, float cf) noexcept {
        const std::span<const float> gains = core_.deliveredGains();
        const double nyq = 0.5 * fs_;
        const double fadeStart = static_cast<double>(HarmonicOscillatorBank::kAntiAliasFadeStart) * nyq;
        std::array<double, static_cast<std::size_t>(kNumPartials)> target{};
        std::array<double, static_cast<std::size_t>(kNumPartials)> slope{};
        std::array<double, static_cast<std::size_t>(kNumPartials)> radius{};
        const std::size_t np = std::min(gains.size(), static_cast<std::size_t>(kNumPartials));
        for (std::size_t k = 0; k < np; ++k) {
            const double f = static_cast<double>(k + 1) * static_cast<double>(cf);
            if (!(f < nyq)) {
                radius[k] = 0.0;   // silent past Nyquist (aa = 0, the MCF correction clamps to 0)
                continue;
            }
            const double half = 3.14159265358979323846 * f / fs_;
            const double aa = f <= fadeStart ? 1.0 : (nyq - f) / (nyq - fadeStart);
            target[k] = std::abs(static_cast<double>(gains[k])) * std::cos(half) * aa;
            radius[k] = 1.0 / std::cos(half);
            slope[k] = 2.0 * std::sin(half);
        }
        if (s == 0 && coreResetPending_) {
            ampModel_.fill(0.0);
            ampRadius_ = radius;
        }
        for (std::size_t i = s; i < s + len; ++i) {
            double sum = 0.0;
            const bool restart = i == 0 && coreResetPending_;
            for (std::size_t k = 0; k < np; ++k) {
                const double aPrev = ampModel_[k];
                const double a = aPrev + ampCoeff_ * (target[k] - aPrev);
                ampModel_[k] = a;
                if (radius[k] == 0.0) {
                    ampRadius_[k] = 0.0;
                    continue;
                }
                double term = a * radius[k] * slope[k];
                if (!restart)
                    term += std::abs(a - aPrev) * radius[k] + aPrev * std::abs(radius[k] - ampRadius_[k]);
                ampRadius_[k] = radius[k];
                sum += term;
            }
            coreDelta_[i] = static_cast<float>(sum * static_cast<double>(SpectralShapeRecipe::kCenterPanGain));
        }
        if (s == 0)
            coreResetPending_ = false;
    }

    /// Per-sample routing of plan S8 for n samples (n <= kMaxBlock) from pos_.
    void renderSub(std::size_t n, NoteBehaviourTraces& traces) noexcept {
        n = std::min(n, kMaxBlock);

        // f_res.
        for (std::size_t i = 0; i < n; ++i)
            fRes_[i] = mono_.processPortamento();

        // Latched at trigger, solved at the next lock.process (S5.4).
        if (lockTriggerPending_)
            lock_.trigger(drop_.plannedCurve(), fRes_[0], gridPhase(), ProfundumCore::kMinF0Hz, capHz_, lockKind_);

        // d, d_c, f_core, the Anchor trajectory.
        for (std::size_t i = 0; i < n; ++i) {
            d_[i] = drop_.process();
            dc_[i] = lock_.process(d_[i]);
            fCore_[i] = fRes_[i] * std::exp2(dc_[i] / 12.0f);
            fAnchor_[i] = followRouting_ ? fCore_[i] : fRes_[i];
        }

        // Same boundary for the core and the Anchor (FR-051).
        if (coreNoteOnPending_) {
            core_.noteOn(fCore_[0]);   // f_res * 2^(d_0 / 12), d_0 the effective sample's d_c
            logCall({.sample = pos_, .target = NoteBehaviourCall::Target::Core, .kind = NoteBehaviourCall::Kind::NoteOn,
                     .frequency = fCore_[0]});
        }
        if (anchorNoteOnPending_) {
            anchor_.noteOn(fAnchor_[0], anchorRearm_);
            logCall({.sample = pos_, .target = NoteBehaviourCall::Target::Anchor,
                     .kind = NoteBehaviourCall::Kind::NoteOn, .frequency = fAnchor_[0], .rearm = anchorRearm_});
        }
        const std::uint8_t mark = pendingMark_;
        // a1 replica (SC-026 ruling (A), epoch 7): the Anchor snaps its a1 smoother on a Reset-branch noteOn
        // (policy Reset, or not sounding: anchor_oscillator.h applyNoteOn).
        const bool a1Snap = anchorNoteOnPending_ && (anchorPolicyReset_ || !anchor_.isSounding());
        clearOffsetZeroFlags();

        // Core, in chunks ending on its pitch grid: currentFrequency() after a chunk is every sample's.
        constexpr std::size_t U = ProfundumCore::kPitchUpdateInterval;
        for (std::size_t s = 0; s < n;) {
            const std::size_t len = std::min(n - s, U - (gridCount_ + s) % U);
            core_.processBlock(coreL_.data() + s, coreR_.data() + s, len, fCore_.data() + s);
            const float cf = core_.currentFrequency();
            for (std::size_t k = s; k < s + len; ++k)
                coreFreq_[k] = cf;
            coreDeltaBoundChunk(s, len, cf);
            s += len;
        }

        // Anchor: a1 from the core (deviation 6), then one sample per call.
        const float a1Feed = core_.deliveredGains()[0] * SpectralShapeRecipe::kCenterPanGain;
        anchor_.setCoreH1Amplitude(a1Feed);
        // Replica of the Anchor's a1 path (anchor_oscillator.h setCoreH1Amplitude :262-267, applyNoteOn snap,
        // processBlock :339): same clamp, same OnePoleSmoother, same calls, advanced only while sounding.
        a1Model_.setTarget(std::clamp(a1Feed, 0.0f, AnchorOscillator::kMaxCoreH1Amplitude));
        if (a1Snap)
            a1Model_.snapToTarget();
        for (std::size_t i = 0; i < n; ++i) {
            anchor_.processBlock(anchorL_.data() + i, anchorR_.data() + i, 1, fAnchor_.data() + i);
            phase_[i] = anchor_.phase();
            inc_[i] = anchor_.phaseIncrement();
            modeGain_[i] = anchor_.modeGain();
            a1_[i] = anchor_.isSounding() ? a1Model_.process() : 0.0f;
        }

        // Mix: out = (core + anchor) * gain * env; the gain multiply is skipped while inactive (FR-044).
        const std::size_t t0 = traces.length;
        const std::size_t cap = traces.capacity();
        for (std::size_t i = 0; i < n; ++i) {
            const bool dip = declick_.isActive();
            const float g = dip ? declick_.process() : 1.0f;
            const float e = env_.process();
            const float sL = coreL_[i] + anchorL_[i];
            const float sR = coreR_[i] + anchorR_[i];
            const float outL = dip ? sL * g * e : sL * e;
            const float outR = dip ? sR * g * e : sR * e;
            const std::size_t t = t0 + i;
            if (t < cap) {
                traces.L[t] = outL;
                traces.R[t] = outR;
                traces.coreL[t] = coreL_[i];
                traces.coreR[t] = coreR_[i];
                traces.anchorL[t] = anchorL_[i];
                traces.anchorR[t] = anchorR_[i];
                traces.fRes[t] = fRes_[i];
                traces.d[t] = d_[i];
                traces.dc[t] = dc_[i];
                traces.fCore[t] = fCore_[i];
                traces.fAnchor[t] = fAnchor_[i];
                traces.coreFreq[t] = coreFreq_[i];
                traces.anchorPhase[t] = phase_[i];
                traces.anchorInc[t] = inc_[i];
                traces.modeGain[t] = modeGain_[i];
                traces.anchorA1[t] = a1_[i];
                traces.gain[t] = g;
                traces.env[t] = e;
                traces.stage[t] = static_cast<std::uint8_t>(env_.getStage());
                traces.marks[t] = i == 0 ? mark : std::uint8_t{0};
                traces.coreDeltaBound[t] = coreDelta_[i];
            }
        }
        if (t0 + n > cap)
            ++violations_;
        traces.length = std::min(t0 + n, cap);
        gridCount_ += n;
        pos_ += n;
    }

    // Components.
    ProfundumCore core_;
    AnchorOscillator anchor_;
    ADSREnvelope env_;
    MonoHandler mono_;
    NoteMemory memory_;
    DropEnvelope drop_;
    DropPhaseLock lock_;
    RetriggerDeclick declick_;

    // Sub-block scratch (fixed size, plan S8).
    std::array<float, kMaxBlock> fRes_{}, d_{}, dc_{}, fCore_{}, fAnchor_{}, coreFreq_{};
    std::array<float, kMaxBlock> coreL_{}, coreR_{}, anchorL_{}, anchorR_{};
    std::array<double, kMaxBlock> phase_{}, inc_{}, modeGain_{};
    std::array<float, kMaxBlock> a1_{};

    // SC-026 ruling (A) (epoch 7): replica of the Anchor's a1 smoother (S4.9), and the policy of its last noteOn.
    OnePoleSmoother a1Model_{AnchorOscillator::kMaxCoreH1Amplitude};
    bool anchorPolicyReset_ = false;
    std::array<float, kMaxBlock> coreDelta_{};

    // SC-010(b) D model (ruling (B')): the bank's per-partial amplitude one-pole and MCF radius.
    std::array<double, static_cast<std::size_t>(kNumPartials)> ampModel_{};
    std::array<double, static_cast<std::size_t>(kNumPartials)> ampRadius_{};
    double ampCoeff_ = 0.0;
    bool coreResetPending_ = false;

    // Events, outcomes, call log.
    std::array<QueuedEvent, kMaxEvents> events_{};
    std::size_t numEvents_ = 0;
    std::array<NoteOutcomeRecord, kMaxOutcomes> outcomes_{};
    std::size_t numOutcomes_ = 0;
    std::array<NoteBehaviourCall, kMaxCalls> calls_{};
    std::size_t numCalls_ = 0;

    // Timeline and state.
    double fs_ = 48000.0;
    float capHz_ = 0.0f;
    std::size_t pos_ = 0;
    std::size_t gridCount_ = 0;   // samples since the core's last Reset branch (its grid)
    std::size_t violations_ = 0;
    int activeNote_ = -1;         // MonoHandler has no active-note getter (mono_handler.h:382)
    bool coreSounding_ = false;
    bool followRouting_ = false;
    bool forceLockBypass_ = false;
    bool useDeclick_ = true;

    // Declick-deferred transition.
    bool dipPending_ = false;
    std::size_t switchPos_ = kNoteBehaviourNoSample;
    PendingTransition pending_{};

    // Offset-0 latches for the next sub-block.
    bool lockTriggerPending_ = false;
    bool anchorNoteOnPending_ = false;
    bool coreNoteOnPending_ = false;
    bool anchorRearm_ = false;
    DropPhaseLock::TriggerKind lockKind_ = DropPhaseLock::TriggerKind::SnapReset;
    std::uint8_t pendingMark_ = 0;
};

// =============================================================================
// Scripts (plan S8 harness self-test; reused by SC-009(b), SC-010, SC-011, SC-012(a), SC-014)
// =============================================================================

struct NoteScriptEvent {
    std::size_t at;   // sample from the script start
    int note;
    int velocity;
    enum class Kind : std::uint8_t { On, Off } kind;
};

struct NoteScript {
    std::vector<NoteScriptEvent> events;
    std::vector<NoteOutcome> expected;   // one per event
    std::size_t length = 0;              // samples to render
};

inline std::size_t noteBehaviourSeconds(double seconds, double fs) {
    return static_cast<std::size_t>(std::llround(seconds * fs));
}

/// SC-014's fixed script (also SC-009(b), SC-010, SC-012(a)): n1 from silence; n2 over held n1
/// (Overlap); release n2 (ReturnToHeld to n1); release n1, then n3 in its 300 ms release tail (Tail);
/// release n3, >= 1 s of silence after the release, then n4 (FromSilence). Every note holds >= 1.1 s,
/// so a window 1 s after each onset fits before the next event.
inline NoteScript noteBehaviourStandardScript(int n1, double fs) {
    using K = NoteScriptEvent::Kind;
    const int n2 = n1 + 5;
    const int n3 = n1 + 3;
    const int n4 = n1 + 7;
    const auto s = [fs](double sec) { return noteBehaviourSeconds(sec, fs); };
    NoteScript sc;
    sc.events = {
        {s(0.0), n1, 100, K::On},  {s(1.2), n2, 100, K::On},  {s(2.4), n2, 0, K::Off}, {s(3.6), n1, 0, K::Off},
        {s(3.7), n3, 100, K::On},  {s(4.9), n3, 0, K::Off},   {s(6.4), n4, 100, K::On},
    };
    sc.expected = {NoteOutcome::FromSilence, NoteOutcome::Overlap, NoteOutcome::ReturnToHeld, NoteOutcome::Release,
                   NoteOutcome::Tail,        NoteOutcome::Release, NoteOutcome::FromSilence};
    sc.length = s(7.6);
    return sc;
}

/// Positive control: releasing a non-active held key is NoAction.
inline NoteScript noteBehaviourNonActiveReleaseScript(int n1, double fs) {
    using K = NoteScriptEvent::Kind;
    const auto s = [fs](double sec) { return noteBehaviourSeconds(sec, fs); };
    NoteScript sc;
    sc.events = {{s(0.0), n1, 100, K::On}, {s(0.5), n1 + 4, 100, K::On}, {s(1.0), n1, 0, K::Off},
                 {s(1.5), n1 + 4, 0, K::Off}};
    sc.expected = {NoteOutcome::FromSilence, NoteOutcome::Overlap, NoteOutcome::NoAction, NoteOutcome::Release};
    sc.length = s(2.0);
    return sc;
}

/// SC-011: a new note during the dip re-targets the pending transition (8 samples after the arm).
inline NoteScript noteBehaviourDipRetargetScript(int n1, double fs) {
    using K = NoteScriptEvent::Kind;
    const auto s = [fs](double sec) { return noteBehaviourSeconds(sec, fs); };
    NoteScript sc;
    sc.events = {{s(0.0), n1, 100, K::On}, {s(0.5), n1 + 4, 100, K::On}, {s(0.5) + 8, n1 + 7, 100, K::On}};
    sc.expected = {NoteOutcome::FromSilence, NoteOutcome::Overlap, NoteOutcome::Overlap};
    sc.length = s(1.0);
    return sc;
}

/// Allocates the traces for the script, queues its events and renders it (call right after
/// harness.prepare() and configuration).
inline void noteBehaviourRunScript(NoteBehaviourHarness& h, const NoteScript& sc, std::size_t hostBlock,
                                   NoteBehaviourTraces& traces) {
    traces.allocate(sc.length);
    for (const NoteScriptEvent& e : sc.events) {
        if (e.kind == NoteScriptEvent::Kind::On)
            h.noteOn(e.note, e.velocity, e.at);
        else
            h.noteOff(e.note, e.at);
    }
    h.render(sc.length, hostBlock, traces);
}

// =============================================================================
// Measurement helpers (plan S11.2)
// =============================================================================

/// Wraps an angle in radians into (-pi, pi].
inline double noteBehaviourWrapRad(double a) {
    const double twoPi = kTwoPi;
    a = std::fmod(a, twoPi);
    if (a > 0.5 * twoPi)
        a -= twoPi;
    if (a <= -0.5 * twoPi)
        a += twoPi;
    return a;
}

/// The Anchor's phase AT sample n (before its advance): phase() - phaseIncrement() wrapped to [0, 1).
inline double noteBehaviourAnchorPhaseAt(const NoteBehaviourTraces& tr, std::size_t n) {
    double p = tr.anchorPhase[n] - tr.anchorInc[n];
    if (p < 0.0)
        p += 1.0;
    return p;
}

/// One half-period estimate of the D-15 zero-crossing estimator.
struct NoteBehaviourZcEstimate {
    double tA;     // crossing time (samples, fractional)
    double tB;     // next crossing time
    double tMid;   // (tA + tB) / 2, the stamp
    double hz;     // f_zc = fs / (2 (tB - tA))
};

/// D-15 f_zc: zero crossings located by linear interpolation between the two straddling samples, in
/// double; each half-period between successive crossings gives f_zc = 1 / (2 dt), stamped at its
/// midpoint. A crossing is a change of (x < 0) between neighbours, so an exact 0.0f sample is
/// counted once.
inline std::vector<NoteBehaviourZcEstimate> noteBehaviourZeroCrossingPitch(std::span<const float> x, double fs) {
    std::vector<double> crossings;
    for (std::size_t n = 0; n + 1 < x.size(); ++n) {
        const double a = static_cast<double>(x[n]);
        const double b = static_cast<double>(x[n + 1]);
        if ((a < 0.0) != (b < 0.0))
            crossings.push_back(static_cast<double>(n) + a / (a - b));
    }
    std::vector<NoteBehaviourZcEstimate> out;
    if (crossings.size() < 2)
        return out;
    out.reserve(crossings.size() - 1);
    for (std::size_t k = 0; k + 1 < crossings.size(); ++k) {
        const double tA = crossings[k];
        const double tB = crossings[k + 1];
        out.push_back({tA, tB, 0.5 * (tA + tB), fs / (2.0 * (tB - tA))});
    }
    return out;
}

// =============================================================================
// T020 additions (append-only after T019)
// =============================================================================

/// SC-009(b) matrix script: the standard script, then n4 released at 7.6 s and re-pressed at 7.7 s in
/// its own release tail (a same-note Tail: the FreeRunning same-note case of SC-009(b) wherever the
/// cell resolves to FreeRunning). The last event is the same-note one, so dropping it yields the
/// uninterrupted reference script.
inline NoteScript noteBehaviourMatrixScript(int n1, double fs) {
    using K = NoteScriptEvent::Kind;
    const auto s = [fs](double sec) { return noteBehaviourSeconds(sec, fs); };
    NoteScript sc = noteBehaviourStandardScript(n1, fs);
    const int n4 = n1 + 7;
    sc.events.push_back({s(7.6), n4, 0, K::Off});
    sc.events.push_back({s(7.7), n4, 100, K::On});
    sc.expected.push_back(NoteOutcome::Release);
    sc.expected.push_back(NoteOutcome::Tail);
    sc.length = s(8.9);
    return sc;
}

/// Anchor phase-advance measurement on a harness trace (Phase 3 SC-026(a)/(c) model, restated for the
/// harness): f_grid is the Anchor's trajectory value fAnchor, clamped to [kMinF0Hz, capHz] as the
/// Anchor's trajectoryF0 does, sampled at every 16th sample of a grid that restarts at each Reset-branch
/// noteOn and taken from the noteOn sample itself at a FreeRunning noteOn (Phase 3 deviation 5).
struct NoteBehaviourAnchorAdvance {
    double worstStep = 0.0;          // max |wrap(phase[n] - phase[n-1]) - f_grid(n)/fs| (cycles)
    std::size_t worstStepAt = 0;
    double worstAccumulated = 0.0;   // max |sum dphi - sum f_grid/fs| over [from, n] (cycles)
    std::size_t worstAccumulatedAt = 0;
    std::size_t checked = 0;         // samples compared
};

/// Compares samples n in [from, to) (Reset-branch noteOn samples excluded: phi restarts there).
inline NoteBehaviourAnchorAdvance noteBehaviourAnchorAdvance(const NoteBehaviourTraces& tr, double fs, float capHz,
                                                             std::size_t from, std::size_t to) {
    std::vector<std::uint8_t> applied(tr.length, std::uint8_t{0});   // 1: Reset branch, 2: FreeRunning
    std::size_t first = kNoteBehaviourNoSample;
    for (const NoteOutcomeRecord& r : tr.recorded()) {
        if (r.effectiveSample >= tr.length)
            continue;   // not applied (kNoteBehaviourNoSample) or outside the trace
        applied[r.effectiveSample] = r.coreResetBranch ? std::uint8_t{1} : std::uint8_t{2};
        first = std::min(first, r.effectiveSample);
    }
    NoteBehaviourAnchorAdvance out;
    if (first == kNoteBehaviourNoSample)
        return out;
    to = std::min(to, tr.length);
    double fGrid = 0.0;
    std::size_t grid = 0;
    double accMeasured = 0.0;
    double accExpected = 0.0;
    for (std::size_t n = first; n < to; ++n) {
        const double f = static_cast<double>(std::clamp(tr.fAnchor[n], AnchorOscillator::kMinF0Hz, capHz));
        if (applied[n] == 1)
            grid = 0;
        else if (applied[n] == 2)
            fGrid = f;
        if (grid % AnchorOscillator::kPitchUpdateInterval == 0)
            fGrid = f;
        ++grid;
        if (applied[n] == 1 || n == 0 || n < from)
            continue;
        double d = tr.anchorPhase[n] - tr.anchorPhase[n - 1];
        d -= std::floor(d + 0.5);   // [-0.5, 0.5)
        const double e = fGrid / fs;
        const double stepErr = std::abs(d - e);
        if (stepErr > out.worstStep) {
            out.worstStep = stepErr;
            out.worstStepAt = n;
        }
        accMeasured += d;
        accExpected += e;
        const double accErr = std::abs(accMeasured - accExpected);
        if (accErr > out.worstAccumulated) {
            out.worstAccumulated = accErr;
            out.worstAccumulatedAt = n;
        }
        ++out.checked;
    }
    return out;
}

// -----------------------------------------------------------------------------
// SC-026 (T023 epoch 7, ruling (A), spec Clarifications 2026-10-13 "T023 epoch 6 readings"): the Anchor's
// pitch, phase() and mode gain stay bit-identical with the lock enabled vs bypassed; its OUTPUT follows the
// core's delivered h1 (deviation 6: a1 = deliveredGains()[0], which applyMask renormalises per f). Gate: per
// sample, output_on == output_off * (a1_on / a1_off) within float rounding (<= -120 dB re the bypassed
// Anchor's peak), a1 = the smoothed level feed each run applied (NoteBehaviourTraces::anchorA1). The raw
// output difference (no division) is recorded per cell, not gated (a Phase 9 listening item).
// -----------------------------------------------------------------------------
inline constexpr double kNoteBehaviourAnchorScaledBarDb = -120.0;

struct NoteBehaviourAnchorDiff {
    double maxDiff = 0.0;        // raw: max |on - off| over L and R (recorded)
    double peak = 0.0;           // max |off| over L and R (the bypassed Anchor's peak)
    double db = -400.0;          // raw: 20 log10(maxDiff / peak); -400 when maxDiff == 0 (recorded)
    double scaledMaxDiff = 0.0;  // gated: max |on - off * a1_on / a1_off| over L and R
    double scaledDb = -400.0;    // gated: 20 log10(scaledMaxDiff / peak); -400 when scaledMaxDiff == 0
    std::size_t unchecked = 0;   // samples with a1_off == 0 and a1_on != 0 (no ratio exists; recorded)
};

inline double noteBehaviourDbRe(double diff, double peak) {
    if (diff > 0.0 && peak > 0.0)
        return 20.0 * std::log10(diff / peak);
    return diff > 0.0 ? 0.0 : -400.0;   // a difference against a silent reference fails any gate
}

/// Samples [0, n) of both channels of two traces' Anchor outputs.
inline NoteBehaviourAnchorDiff noteBehaviourAnchorOutputDiff(const NoteBehaviourTraces& on,
                                                              const NoteBehaviourTraces& off, std::size_t n) {
    NoteBehaviourAnchorDiff out;
    for (std::size_t i = 0; i < n; ++i) {
        const double onL = static_cast<double>(on.anchorL[i]);
        const double onR = static_cast<double>(on.anchorR[i]);
        const double offL = static_cast<double>(off.anchorL[i]);
        const double offR = static_cast<double>(off.anchorR[i]);
        out.maxDiff = std::max({out.maxDiff, std::abs(onL - offL), std::abs(onR - offR)});
        out.peak = std::max({out.peak, std::abs(offL), std::abs(offR)});
        const double a1On = static_cast<double>(on.anchorA1[i]);
        const double a1Off = static_cast<double>(off.anchorA1[i]);
        if (a1Off == 0.0) {
            if (a1On == 0.0)
                out.scaledMaxDiff = std::max({out.scaledMaxDiff, std::abs(onL), std::abs(onR)});
            else
                ++out.unchecked;
            continue;
        }
        const double ratio = a1On / a1Off;
        out.scaledMaxDiff = std::max({out.scaledMaxDiff, std::abs(onL - offL * ratio), std::abs(onR - offR * ratio)});
    }
    out.db = noteBehaviourDbRe(out.maxDiff, out.peak);
    out.scaledDb = noteBehaviourDbRe(out.scaledMaxDiff, out.peak);
    return out;
}

}  // namespace Krate::DSP::ProfundumTest
