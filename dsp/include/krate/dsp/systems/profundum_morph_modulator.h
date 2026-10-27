// ==============================================================================
// Layer 3: System - Profundum Phase 14 (specs/profundum-phase14-morph-modulation)
// ==============================================================================
// ProfundumMorphModulator (Morph Modulation: one global source at fs / 32, one offset per interval).
//   Serves: FR-001 - FR-008; SC-001 (a), SC-002, SC-003 (modulator part).
//   Encodes: plan 4.1, 5.2, 5.3; C-3 (sources in the list order of 1003), C-4 (Envelope: restart from 0 on every
//            accepted note-on, never gated off), C-5 (Cycle: free-running, never retriggered), C-6 (Velocity latch),
//            FR-006 (Mod Wheel latch).
//   Spec:  specs/profundum-phase14-morph-modulation/spec.md
//   Plan:  specs/profundum-phase14-morph-modulation/plan.md
//   Tasks: specs/profundum-phase14-morph-modulation/tasks.md (T006)
//
// Every source advances once per interval whichever is selected (the ProfundumMotion pattern,
// profundum_motion.h:249-279); advanceInterval() returns the selected s after that step, so the interval that applies a
// restart already holds the first Attack step from 0 (the C-4 restart observable). The offset is
// clamp(amount * s, -1, 1), exposed through a ModulationSource (FR-007). The config is not a MorphState field (FR-018),
// so morph_state.h is not included. Placed in systems/ beside its only consumer, ProfundumEngine.
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>                 // L0: detail::isFinite, detail::flushDenormal
#include <krate/dsp/core/modulation_source.h>        // L0: ModulationSource
#include <krate/dsp/core/note_value.h>               // L0: NoteValue, NoteModifier
#include <krate/dsp/primitives/adsr_envelope.h>      // L1: ADSREnvelope (Envelope source)
#include <krate/dsp/primitives/envelope_utils.h>     // L1: RetriggerMode, kMin/MaxEnvelopeTimeMs
#include <krate/dsp/primitives/lfo.h>                // L1: LFO (Cycle source), Waveform, kMin/MaxFrequency

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <utility>

namespace Krate::DSP {

namespace detail::profundum_morph_modulator {

[[nodiscard]] inline bool bitsDiffer(float a, float b) noexcept {
    return std::bit_cast<std::uint32_t>(a) != std::bit_cast<std::uint32_t>(b);
}

/// A non-finite value keeps `prev`; a finite one clamps to [lo, hi].
[[nodiscard]] inline float sanitizeFloat(float x, float prev, float lo, float hi) noexcept {
    return detail::isFinite(x) ? std::clamp(x, lo, hi) : prev;
}

/// An enum above its last enumerator keeps `prev`.
template <typename E>
[[nodiscard]] inline E sanitizeEnum(E x, E prev, E last) noexcept {
    return static_cast<std::uint8_t>(x) <= static_cast<std::uint8_t>(last) ? x : prev;
}

} // namespace detail::profundum_morph_modulator

/// Phase 14 Morph Modulation (FR-001 - FR-008). One global source at fs / 32; one offset per interval.
/// prepare() is control-thread; every other member is noexcept and allocation-, lock-, exception- and I/O-free.
class ProfundumMorphModulator {
public:
    enum class Source : std::uint8_t { Off = 0, Envelope, Cycle, Velocity, ModWheel };   // C-3, list order of 1003

    struct Config {                                   // FR-002: exactly the C-12 fields (Sync Rate as note + modifier)
        Source source = Source::Off;
        float amount = 0.0f;                          // [-1, 1]
        float envAttackMs = 10.0f;                    // [kMinEnvelopeTimeMs, kMaxEnvelopeTimeMs]
        float envDecayMs = 300.0f;
        float envSustain = 0.0f;                      // [0, 1]
        Waveform cycleShape = Waveform::Sine;
        float cycleRateHz = 1.0f;                     // [kMinFrequency, kMaxFrequency]
        bool cycleSync = false;
        NoteValue cycleNote = NoteValue::Quarter;     // dropdown index 13 with cycleModifier None
        NoteModifier cycleModifier = NoteModifier::None;
    };

    class Output final : public ModulationSource {   // FR-007 (profundum_motion.h:126-134 pattern)
    public:
        [[nodiscard]] float getCurrentValue() const noexcept override { return value_; }
        [[nodiscard]] std::pair<float, float> getSourceRange() const noexcept override { return {-1.0f, 1.0f}; }

    private:
        friend class ProfundumMorphModulator;
        float value_ = 0.0f;
    };

    static constexpr std::size_t kControlInterval = 32;   // the engine asserts == ProfundumEngine::kControlInterval
    static constexpr float kMinAmount = -1.0f;
    static constexpr float kMaxAmount = 1.0f;
    static constexpr std::uint8_t kMaxVelocity = 127;
    /// The restart interval's first Attack step from 0 at or above this floor is an Attack of at most one interval:
    /// the clamp step to the peak runs in that interval (SC-002 (b), C-4). Eight ulps of 1.0 below the peak.
    static constexpr float kOneStepPeakFloor = 1.0f - 8.0f * std::numeric_limits<float>::epsilon();

    ProfundumMorphModulator() noexcept = default;
    ProfundumMorphModulator(const ProfundumMorphModulator&) = delete;            // owns an LFO (lfo.h:73)
    ProfundumMorphModulator& operator=(const ProfundumMorphModulator&) = delete;

    /// Control-thread. Clocks the envelope and the LFO at fs / 32 (the Motion pattern, profundum_motion.h:141-151),
    /// Hard retrigger, then reset(). `sampleRate` is the engine's already clamped rate.
    void prepare(double sampleRate) noexcept {
        const double controlRate = sampleRate / static_cast<double>(kControlInterval);
        controlRate_ = static_cast<float>(controlRate);
        env_.prepare(controlRate_);
        env_.setRetriggerMode(RetriggerMode::Hard);
        lfo_.prepare(controlRate);
        reset();
    }

    /// Envelope Idle (output 0), LFO reset (phase 0, seed 12345u), Velocity 0, s and offset +0; every setter is
    /// handed again at the next setConfig. The wheel and the tempo are kept (the wheel is re-handed each interval by
    /// the engine; the tempo lives in the LFO, which LFO::reset() does not touch, lfo.h:95-108).
    void reset() noexcept {
        env_.reset();
        lfo_.reset();
        velocity_ = 0.0f;
        s_ = 0.0f;
        output_.value_ = 0.0f;
        configured_ = false;
        restartPending_ = false;
    }

    /// Change-only and sanitising (FR-001): a non-finite float keeps the previous applied value, finite floats clamp,
    /// an undeclared enum keeps the previous value. A component setter runs only on a bitwise change, or for every
    /// field on the first call after prepare / reset. Source and amount are stored only.
    void setConfig(const Config& c) noexcept {
        namespace mm = detail::profundum_morph_modulator;
        Config n = applied_;
        n.source = mm::sanitizeEnum(c.source, applied_.source, Source::ModWheel);
        n.amount = mm::sanitizeFloat(c.amount, applied_.amount, kMinAmount, kMaxAmount);
        n.envAttackMs = mm::sanitizeFloat(c.envAttackMs, applied_.envAttackMs, kMinEnvelopeTimeMs, kMaxEnvelopeTimeMs);
        n.envDecayMs = mm::sanitizeFloat(c.envDecayMs, applied_.envDecayMs, kMinEnvelopeTimeMs, kMaxEnvelopeTimeMs);
        n.envSustain = mm::sanitizeFloat(c.envSustain, applied_.envSustain, 0.0f, 1.0f);
        n.cycleShape = mm::sanitizeEnum(c.cycleShape, applied_.cycleShape, Waveform::SmoothRandom);
        n.cycleRateHz = mm::sanitizeFloat(c.cycleRateHz, applied_.cycleRateHz, kMinFrequency, kMaxFrequency);
        n.cycleSync = c.cycleSync;
        n.cycleNote = mm::sanitizeEnum(c.cycleNote, applied_.cycleNote, NoteValue::SixtyFourth);
        n.cycleModifier = mm::sanitizeEnum(c.cycleModifier, applied_.cycleModifier, NoteModifier::Triplet);

        const bool all = !configured_;
        // Envelope (no release: the contour is never gated off, C-4)
        if (all || mm::bitsDiffer(n.envAttackMs, applied_.envAttackMs)) {
            env_.setAttack(n.envAttackMs);
        }
        if (all || mm::bitsDiffer(n.envDecayMs, applied_.envDecayMs)) {
            env_.setDecay(n.envDecayMs);
        }
        if (all || mm::bitsDiffer(n.envSustain, applied_.envSustain)) {
            env_.setSustain(n.envSustain);
        }
        // Cycle (free-running: no retrigger, phase or fade-in, C-5)
        if (all || n.cycleShape != applied_.cycleShape) {
            lfo_.setWaveform(n.cycleShape);
        }
        if (all || mm::bitsDiffer(n.cycleRateHz, applied_.cycleRateHz)) {
            lfo_.setFrequency(n.cycleRateHz);
        }
        if (all || n.cycleSync != applied_.cycleSync) {
            lfo_.setTempoSync(n.cycleSync);
        }
        if (all || n.cycleNote != applied_.cycleNote || n.cycleModifier != applied_.cycleModifier) {
            lfo_.setNoteValue(n.cycleNote, n.cycleModifier);
        }
        applied_ = n;
        configured_ = true;
    }

    /// Host tempo. Non-finite or <= 0 keeps the previous tempo; LFO::setTempo on a bitwise change only
    /// (profundum_motion.h:233-239 rule).
    void setTempoBpm(float bpm) noexcept {
        if (!detail::isFinite(bpm) || bpm <= 0.0f ||
            !detail::profundum_morph_modulator::bitsDiffer(bpm, tempoBpm_)) {
            return;
        }
        tempoBpm_ = bpm;
        lfo_.setTempo(bpm);
    }

    /// An accepted note-on (C-4 restart + C-6 latch). The envelope restarts from 0: Hard mode's enterAttack alone would
    /// start from the current output (adsr_envelope.h:451-456). reset + gate is idempotent, so several calls before one
    /// advanceInterval() are one restart. Velocity 127 is exactly 1 by branch (fast-math). The engine never passes 0
    /// (it turns that into a note-off); the modulator treats 0 as no note.
    void noteOn(std::uint8_t velocity) noexcept {
        if (velocity == 0) {
            return;
        }
        env_.reset();
        env_.gate(true);
        restartPending_ = true;
        velocity_ = (velocity >= kMaxVelocity) ? 1.0f : static_cast<float>(velocity) / 127.0f;
    }

    /// FR-006: non-finite keeps the latched wheel; otherwise clamped to [0, 1].
    void setModWheel(float w) noexcept {
        if (!detail::isFinite(w)) {
            return;
        }
        wheel_ = std::clamp(w, 0.0f, 1.0f);
    }

    /// Exactly once per grid point (plan 5.1 step 2). Advances every source, selects s, sets the offset, returns s.
    float advanceInterval() noexcept {
        // The flush covers the Sustain-stage tail: a Sustain move to 0 while held decays geometrically through the
        // subnormal range and, where the 1 ms coefficient is > 0.5 (48 and 96 kHz), sticks at denorm_min
        // (adsr_envelope.h:245, :72). Normal values pass bit-unchanged.
        float eRaw = env_.process();
        // An attack of at most one interval peaks in the interval that applies the restart (SC-002 (b), C-4). The
        // rate-clamped one-pole's single step is base = 1.3 (1 - 0.3 / 1.3), which float32 rounds to 0.99999988, one
        // ulp short of the peak (envelope_utils.h:96-104), so the clamp to the peak and the entry to Decay
        // (adsr_envelope.h:302-306) would land one interval late. The clamp step runs in this interval instead, by
        // branch, so the peak reads 1.0 exactly whether or not a toolchain rounds the first step to 1. The decision is
        // the envelope's own first step from 0 (its attackBase_), never a recomputation of its rate clamp: under
        // -ffast-math two compiled copies of that expression can disagree by an ulp at the boundary.
        if (restartPending_ && env_.getStage() == ADSRStage::Attack && eRaw >= kOneStepPeakFloor) {
            eRaw = env_.process();
        }
        restartPending_ = false;
        const float e = detail::flushDenormal(eRaw);
        const float lRaw = lfo_.process();
        const float l = detail::isFinite(lRaw) ? std::clamp(lRaw, -1.0f, 1.0f) : 0.0f;

        float s = 0.0f;
        switch (applied_.source) {
            case Source::Off:
                s = 0.0f;
                break;
            case Source::Envelope:
                s = e;
                break;
            case Source::Cycle:
                s = l;
                break;
            case Source::Velocity:
                s = velocity_;
                break;
            case Source::ModWheel:
                s = wheel_;
                break;
        }
        s_ = s;
        output_.value_ = std::clamp(applied_.amount * s, kMinAmount, kMaxAmount);
        return s_;
    }

    [[nodiscard]] float sourceValue() const noexcept { return s_; }
    [[nodiscard]] float offsetValue() const noexcept { return output_.value_; }   // clamp(amount * s, -1, 1)
    [[nodiscard]] const Config& config() const noexcept { return applied_; }      // the sanitised, applied config
    [[nodiscard]] const ModulationSource& output() const noexcept { return output_; }

private:
    ADSREnvelope env_;
    LFO lfo_;
    Config applied_{};
    bool configured_ = false;
    float velocity_ = 0.0f;      // C-6: [1/127, 1] after a note, 0 after reset
    float wheel_ = 0.0f;         // FR-006: [0, 1]
    float s_ = 0.0f;
    Output output_;
    float tempoBpm_ = 120.0f;    // LFO's own default (lfo.h:548)
    float controlRate_ = 44100.0f / static_cast<float>(kControlInterval);   // set by prepare()
    bool restartPending_ = false;        // noteOn restarted the envelope; cleared by the next advanceInterval()
};

} // namespace Krate::DSP
