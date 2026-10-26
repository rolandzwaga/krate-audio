// ==============================================================================
// Layer 3: System - Profundum Phase 9 (specs/profundum-phase9-voice-engine)
// ==============================================================================
// ProfundumMotion (Motion sources at fs / 32, ModulationSource output).
//   Serves: FR-040 - FR-049; SC-026 (unit arm).
//   Encodes: plan S3 (S3.1 - S3.4), S9.1; C-13 (all three sources run at fs / 32 and advance every interval,
//            whichever is selected), C-18 (every MotionConfig field realised).
//   Spec:  specs/profundum-phase9-voice-engine/spec.md
//   Plan:  specs/profundum-phase9-voice-engine/plan.md
//   Tasks: specs/profundum-phase9-voice-engine/tasks.md (T003)
//
// One active source (Off / Env / Cycle / Drift) routed explicitly to one of five fixed targets (FR-048, Q1:
// no VoiceModRouter). Offset = v_pol * amount * depth * halfSpan[target] (FR-042, Q2). Depth 0 gives +-0 in every
// field, which the engine treats as "no offset" (SC-026). Separate from BassStereoField's own MOTION (FR-049).
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>                 // L0: detail::isFinite
#include <krate/dsp/core/modulation_source.h>        // L0: ModulationSource
#include <krate/dsp/core/note_value.h>               // L0: NoteValue, NoteModifier
#include <krate/dsp/primitives/adsr_envelope.h>      // L1: ADSREnvelope (ENV)
#include <krate/dsp/primitives/envelope_utils.h>     // L1: RetriggerMode
#include <krate/dsp/primitives/lfo.h>                // L1: LFO (CYCLE), Waveform
#include <krate/dsp/processors/brownian_drift.h>     // L2: BrownianDrift (DRIFT)
#include <krate/dsp/systems/morph_state.h>           // L3 peer: MorphState::MotionConfig, field table

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>
#include <utility>

namespace Krate::DSP {

namespace detail::profundum_motion {

[[nodiscard]] constexpr bool namesEqual(const char* a, const char* b) noexcept {
    while (*a != '\0' && *a == *b) {
        ++a;
        ++b;
    }
    return *a == *b;
}

/// Row of the MorphState field table named `name` (kMorphLeafFieldCount when absent).
[[nodiscard]] constexpr std::size_t fieldIndex(const char* name) noexcept {
    for (std::size_t i = 0; i < kMorphLeafFieldCount; ++i) {
        if (namesEqual(morph_state_io::kFieldTable[i].name, name)) {
            return i;
        }
    }
    return kMorphLeafFieldCount;
}

inline constexpr std::size_t kSource = fieldIndex("motion.source");
inline constexpr std::size_t kTarget = fieldIndex("motion.target");
inline constexpr std::size_t kAmount = fieldIndex("motion.amount");
inline constexpr std::size_t kDepth = fieldIndex("motion.depth");
inline constexpr std::size_t kCycleRateHz = fieldIndex("motion.cycleRateHz");
inline constexpr std::size_t kCycleShape = fieldIndex("motion.cycleShape");
inline constexpr std::size_t kCycleNote = fieldIndex("motion.cycleNote");
inline constexpr std::size_t kCycleModifier = fieldIndex("motion.cycleModifier");
inline constexpr std::size_t kCyclePhaseDeg = fieldIndex("motion.cyclePhaseDeg");
inline constexpr std::size_t kCycleFadeInMs = fieldIndex("motion.cycleFadeInMs");
inline constexpr std::size_t kPolarity = fieldIndex("motion.polarity");
inline constexpr std::size_t kDriftSmoothness = fieldIndex("motion.driftSmoothness");
inline constexpr std::size_t kEnvAttackMs = fieldIndex("motion.envAttackMs");
inline constexpr std::size_t kEnvDecayMs = fieldIndex("motion.envDecayMs");

static_assert(kSource < kMorphLeafFieldCount && kTarget < kMorphLeafFieldCount && kAmount < kMorphLeafFieldCount
              && kDepth < kMorphLeafFieldCount && kCycleRateHz < kMorphLeafFieldCount
              && kCycleShape < kMorphLeafFieldCount && kCycleNote < kMorphLeafFieldCount
              && kCycleModifier < kMorphLeafFieldCount && kCyclePhaseDeg < kMorphLeafFieldCount
              && kCycleFadeInMs < kMorphLeafFieldCount && kPolarity < kMorphLeafFieldCount
              && kDriftSmoothness < kMorphLeafFieldCount && kEnvAttackMs < kMorphLeafFieldCount
              && kEnvDecayMs < kMorphLeafFieldCount,
              "every MotionConfig row ProfundumMotion sanitises is in the MorphState field table");

/// Non-finite keeps `prev`; otherwise clamp to the row's box (the open upper end of cyclePhaseDeg maps to the
/// largest float below it).
[[nodiscard]] inline float sanitizeFloat(float x, float prev, std::size_t row) noexcept {
    if (!detail::isFinite(x)) {
        return prev;
    }
    const auto& info = morph_state_io::kFieldTable[row];
    float y = std::clamp(x, info.lo, info.hi);
    if (info.hiOpen && y >= info.hi) {
        y = std::nextafter(info.hi, info.lo);
    }
    return y;
}

/// An undeclared enumerator keeps `prev`.
template <typename E>
[[nodiscard]] constexpr E sanitizeEnum(E x, E prev, std::size_t row) noexcept {
    static_assert(std::is_enum_v<E> && sizeof(E) == 1);
    return static_cast<std::uint8_t>(x) <= morph_state_io::kFieldTable[row].maxEnum ? x : prev;
}

[[nodiscard]] inline bool bitsDiffer(float a, float b) noexcept {
    return std::bit_cast<std::uint32_t>(a) != std::bit_cast<std::uint32_t>(b);
}

} // namespace detail::profundum_motion

/// The Profundum Motion section (FR-040 - FR-049, plan S3). Control-thread setters and the per-grid-point
/// advanceInterval() run on the audio thread in the engine; no allocation, no lock, no virtual call on that path.
class ProfundumMotion {
public:
    using Config = MorphState::MotionConfig;
    static constexpr std::size_t kControlInterval = 32;                 // the engine asserts == ProfundumCore's
    static constexpr float kMassHalfSpan = 50.0f;                       // FR-042 (Q2): |offset| 1 = half the range
    static constexpr float kUnitHalfSpan = 0.5f;                        //   Depth / Body / Edge
    static constexpr float kShiftHalfSpan = 1.0f;                       //   Shift
    static constexpr std::uint32_t kDefaultSeed = BrownianDrift::kDefaultDriftSeed;

    struct Offsets {                       // one interval's target offsets; exactly one is non-zero at most
        float mass = 0.0f;                 // Mass units
        float depth = 0.0f, body = 0.0f, edge = 0.0f;   // recipe units
        float shift = 0.0f;
    };

    class Output final : public ModulationSource {       // FR-048: the BassStereoField::MotionOutput pattern
    public:
        [[nodiscard]] float getCurrentValue() const noexcept override { return value_; }
        [[nodiscard]] std::pair<float, float> getSourceRange() const noexcept override { return {-1.0f, 1.0f}; }

    private:
        friend class ProfundumMotion;
        float value_ = 0.0f;
    };

    ProfundumMotion() noexcept = default;
    ProfundumMotion(const ProfundumMotion&) = delete;    // owns an LFO (lfo.h has no copy contract we rely on)
    ProfundumMotion& operator=(const ProfundumMotion&) = delete;

    /// Control-thread. Clocks ENV and CYCLE at fs / 32; DRIFT at fs, advanced 32 samples per interval (C-13). Then reset().
    void prepare(double sampleRate) noexcept {
        const double controlRate = sampleRate / static_cast<double>(kControlInterval);
        env_.prepare(static_cast<float>(controlRate));
        env_.setSustain(0.0f);                     // an AD contour that rests at 0 (C-13)
        env_.setRetriggerMode(RetriggerMode::Hard);
        lfo_.prepare(controlRate);
        drift_.prepare(sampleRate);
        drift_.setDepth(1.0f);
        drift_.setMean(0.0f);
        reset();
    }

    /// Env idle, LFO / Drift reset (Drift re-seeded with the stored seed), offsets and values +0, every setter forced
    /// at the next setConfig.
    void reset() noexcept {
        env_.reset();
        lfo_.reset();
        drift_.setSeed(seed_);
        drift_.reset();
        offsets_ = Offsets{};
        v_ = 0.0f;
        output_.value_ = 0.0f;
        configured_ = false;
    }

    /// Stored; takes effect at the next prepare / reset (FR-032, C-17).
    void setSeed(std::uint32_t seed) noexcept { seed_ = seed; }

    /// Per interval. Each component setter is called only on a bitwise change (FR-046, C-20), or for every field on
    /// the first call after prepare / reset. Undeclared enums and non-finite floats keep the previous value; floats
    /// clamp to the MorphState field-table ranges.
    void setConfig(const Config& c) noexcept {
        namespace mp = detail::profundum_motion;
        Config n = applied_;
        n.source = mp::sanitizeEnum(c.source, applied_.source, mp::kSource);
        n.target = mp::sanitizeEnum(c.target, applied_.target, mp::kTarget);
        n.amount = mp::sanitizeFloat(c.amount, applied_.amount, mp::kAmount);
        n.depth = mp::sanitizeFloat(c.depth, applied_.depth, mp::kDepth);
        n.cycleRateHz = mp::sanitizeFloat(c.cycleRateHz, applied_.cycleRateHz, mp::kCycleRateHz);
        n.cycleShape = mp::sanitizeEnum(c.cycleShape, applied_.cycleShape, mp::kCycleShape);
        n.cycleSync = c.cycleSync;
        n.cycleNote = mp::sanitizeEnum(c.cycleNote, applied_.cycleNote, mp::kCycleNote);
        n.cycleModifier = mp::sanitizeEnum(c.cycleModifier, applied_.cycleModifier, mp::kCycleModifier);
        n.cyclePhaseDeg = mp::sanitizeFloat(c.cyclePhaseDeg, applied_.cyclePhaseDeg, mp::kCyclePhaseDeg);
        n.cycleRetrigger = c.cycleRetrigger;
        n.cycleFadeInMs = mp::sanitizeFloat(c.cycleFadeInMs, applied_.cycleFadeInMs, mp::kCycleFadeInMs);
        n.polarity = mp::sanitizeEnum(c.polarity, applied_.polarity, mp::kPolarity);
        n.driftSmoothness = mp::sanitizeFloat(c.driftSmoothness, applied_.driftSmoothness, mp::kDriftSmoothness);
        n.envAttackMs = mp::sanitizeFloat(c.envAttackMs, applied_.envAttackMs, mp::kEnvAttackMs);
        n.envDecayMs = mp::sanitizeFloat(c.envDecayMs, applied_.envDecayMs, mp::kEnvDecayMs);

        const bool all = !configured_;
        // CYCLE (FR-046 call list, in its order)
        if (all || n.cycleShape != applied_.cycleShape) {
            lfo_.setWaveform(n.cycleShape);
        }
        if (all || mp::bitsDiffer(n.cycleRateHz, applied_.cycleRateHz)) {
            lfo_.setFrequency(n.cycleRateHz);
        }
        if (all || n.cycleSync != applied_.cycleSync) {
            lfo_.setTempoSync(n.cycleSync);
        }
        if (all || n.cycleNote != applied_.cycleNote || n.cycleModifier != applied_.cycleModifier) {
            lfo_.setNoteValue(n.cycleNote, n.cycleModifier);
        }
        if (all || mp::bitsDiffer(n.cyclePhaseDeg, applied_.cyclePhaseDeg)) {
            lfo_.setPhaseOffset(n.cyclePhaseDeg);
        }
        if (all || n.cycleRetrigger != applied_.cycleRetrigger) {
            lfo_.setRetriggerEnabled(n.cycleRetrigger);
        }
        if (all || mp::bitsDiffer(n.cycleFadeInMs, applied_.cycleFadeInMs)) {
            lfo_.setFadeInTime(n.cycleFadeInMs);
        }
        // DRIFT
        if (all || mp::bitsDiffer(n.driftSmoothness, applied_.driftSmoothness)) {
            drift_.setSmoothness(n.driftSmoothness);
        }
        // ENV (release = decay: the contour never gates off, C-13)
        if (all || mp::bitsDiffer(n.envAttackMs, applied_.envAttackMs)) {
            env_.setAttack(n.envAttackMs);
        }
        if (all || mp::bitsDiffer(n.envDecayMs, applied_.envDecayMs)) {
            env_.setDecay(n.envDecayMs);
            env_.setRelease(n.envDecayMs);
        }
        // source, target, amount, depth, polarity are stored only (read by advanceInterval).
        applied_ = n;
        configured_ = true;
    }

    /// Host tempo (FR-028). Non-finite or <= 0 keeps the previous tempo; LFO::setTempo on a bitwise change only.
    void setTempoBpm(float bpm) noexcept {
        if (!detail::isFinite(bpm) || bpm <= 0.0f || !detail::profundum_motion::bitsDiffer(bpm, tempoBpm_)) {
            return;
        }
        tempoBpm_ = bpm;
        lfo_.setTempo(bpm);
    }

    /// An envelope-restarting note event (FR-045, FR-046): ENV restarts its attack; CYCLE re-phases when
    /// cycleRetrigger is on (LFO::retrigger is a no-op otherwise).
    void noteRestart() noexcept {
        env_.gate(true);
        lfo_.retrigger();
    }

    /// Exactly once per grid point (FR-026 step 4; plan S9.1).
    const Offsets& advanceInterval() noexcept {
        // 1. Advance every source, whichever is selected (C-13).
        const float e = env_.process();
        const float l = lfo_.process();
        drift_.processBlock(kControlInterval);
        const float dRaw = drift_.getCurrentValue();
        const float d = detail::isFinite(dRaw) ? std::clamp(dRaw, -1.0f, 1.0f) : 0.0f;

        // 2-3. Select and map the polarity (FR-042).
        const bool unipolar = applied_.polarity == Config::Polarity::Unipolar;
        float v = 0.0f;
        switch (applied_.source) {
            case Config::Source::Off:
                v = 0.0f;
                break;
            case Config::Source::Env:
                v = unipolar ? e : (2.0f * e) - 1.0f;
                break;
            case Config::Source::Cycle:
                v = unipolar ? (l + 1.0f) / 2.0f : l;
                break;
            case Config::Source::Drift:
                v = unipolar ? (d + 1.0f) / 2.0f : d;
                break;
        }

        // 4. Scale (float, left to right).
        const float m = (v * applied_.amount) * applied_.depth;
        v_ = v;
        output_.value_ = std::clamp(m, -1.0f, 1.0f);

        // 5. Route to the one target (FR-041, FR-048).
        offsets_ = Offsets{};
        switch (applied_.target) {
            case Config::Target::Mass:
                offsets_.mass = m * kMassHalfSpan;
                break;
            case Config::Target::Depth:
                offsets_.depth = m * kUnitHalfSpan;
                break;
            case Config::Target::Body:
                offsets_.body = m * kUnitHalfSpan;
                break;
            case Config::Target::Edge:
                offsets_.edge = m * kUnitHalfSpan;
                break;
            case Config::Target::Shift:
                offsets_.shift = m * kShiftHalfSpan;
                break;
        }
        return offsets_;
    }

    [[nodiscard]] const Offsets& offsets() const noexcept { return offsets_; }
    /// v after polarity, before amount / depth (FR-086 tap).
    [[nodiscard]] float sourceValue() const noexcept { return v_; }
    /// m = v * amount * depth, clamped to [-1, 1].
    [[nodiscard]] float outputValue() const noexcept { return output_.value_; }
    [[nodiscard]] const ModulationSource& output() const noexcept { return output_; }

private:
    ADSREnvelope env_;          // ENV at fs / 32
    LFO lfo_;                   // CYCLE at fs / 32
    BrownianDrift drift_;       // DRIFT, processBlock(32) per interval
    Config applied_{};
    bool configured_ = false;
    Offsets offsets_{};
    float v_ = 0.0f;
    Output output_;
    std::uint32_t seed_ = kDefaultSeed;
    float tempoBpm_ = 120.0f;
};

// The header includes neither ProfundumCore nor MassLaw (plan S2); the engine asserts equality with their grid.
static_assert(ProfundumMotion::kControlInterval == BrownianDrift::kControlRateInterval);

} // namespace Krate::DSP
