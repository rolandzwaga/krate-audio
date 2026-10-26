// ==============================================================================
// Layer 3: System - Profundum Phase 6 (specs/profundum-phase6-mass-dynamics)
// ==============================================================================
// MassLaw: the Mass macro law. One Mass value in [0, 100], bipolar about 50, plus the user's base
// values of seven targets -> the seven targets, each a clamped bias on its base (FR-020 - FR-029, FR-049):
// the six roadmap targets plus the calibrated output trim FR-021 anticipated (user ruling 2026-10-17,
// spec Clarifications "Group 8 harvest ruling" G8-5: a dB gain after the Mass dynamics, 0 at Mass 50,
// monotone non-increasing, harvested so the reference voice's K-weighted level is flat; SC-003).
//   Spec:  specs/profundum-phase6-mass-dynamics/spec.md (FR-020 - FR-029, FR-049; SC-001(a), SC-004,
//          SC-016 law arm, SC-023 law, SC-025 law)
//   Plan:  specs/profundum-phase6-mass-dynamics/plan.md (S7, S1 C-7, C-9, C-11, C-12, S8.2 seed)
//   Tests: dsp/tests/unit/systems/mass_law_test.cpp (tag [mass-law]); rendered arms in
//          mass_law_spectral_test.cpp and mass_law_automation_test.cpp
//
// Includes Layer 0, Layer 1 and Layer 2 only (transient_shaper.h for the duration range constants).
// MassLaw does no audio processing.
//
// Row form (plan C-9): with u = (mass - 50) / 50 and s = applyModCurve(curve, |u|),
//     target = clamp(base + (u >= 0 ? amountUp * s : -amountDown * s), lo, hi).
//   - Monotonic: amountUp and amountDown carry the D-2 direction sign (Depth and OutputTrim <= 0, the other five >= 0),
//     so each target is monotone in Mass, with clamped runs allowed (FR-024).
//   - Neutral: at mass == 50 exactly u == 0, s == 0 and the result is base + (+/-0.0f) == base, bit-exact
//     for an in-range base (FR-027, SC-023). There is no neutral fast path: the arithmetic is the same
//     at every Mass (the VoragoMacroMatrix evaluateAll precedent).
//
// Slew (FR-025; plan C-7 RULED 2026-10-17, option (i), spec SC-004 row): Mass is smoothed by a linear
// SlewLimiter whose full 0 -> 100 traversal takes kMassSweepMs = 40 ms; D-7's 20 ms minimum is the
// half-sweep time (Mass is bipolar and some bases sit at a range end), so SC-004's per-interval bound
// derives from kSlewBoundRefMs = 20 ms and no row is capped at half range. The compile-time check
//     max(|aUp|, |aDown|) * maxSlope(curve) <= (hi - lo) * kMassSweepMs / (2 * kSlewBoundRefMs)
// is therefore max(|a|) * maxSlope <= hi - lo.
//
// Control grid (plan C-11): MassLaw has no audio clock. Phase 9's engine and the Phase 6 fixture call
// advanceInterval() once per 32-sample interval of an absolute sample counter (plan C-11); the targets
// are smoothed per plan C-12 (no target is double-smoothed).
//
// Recorded decisions (tasks.md T009):
//   - Before prepare(), advanceInterval() returns the current smoothed Mass unchanged (initially 50,
//     kNeutralMass) and does not step the slew.
//   - prepare() configures the slew and then calls reset(), so a prepared law starts at the pending Mass
//     (no sweep from a stale smoothed value after a sample-rate change).
//   - kRows seeds the S8.2 rows with the C-7 (i) ruling applied: FoldAmount amountUp +60 and
//     CompressionAmount amountUp +1 (the S8.2 table's "under (i)" values); both amountDown values keep the
//     S8.2 seed (+50 / +0.5), which clamps at the 0 base of the reference voice either way.
//
// Contract: fixed size, zero heap, every method noexcept and allocation-free (SC-016). setMass /
// setBase: a non-finite argument keeps the previous value, finite values clamp (FR-029).
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/modulation_curves.h>
#include <krate/dsp/primitives/smoother.h>
#include <krate/dsp/processors/transient_shaper.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace Krate::DSP {

class MassLaw {
public:
    /// The six roadmap targets plus the calibrated output trim (FR-021 as amended, G8-5 ruling 2026-10-17). Append-only.
    enum class Target : std::uint8_t {
        Depth, Body, TransientDuration, FoldAmount, CompressionAmount, AnchorLevel, OutputTrim
    };

    static constexpr std::size_t kNumTargets = 7;            // FR-021 (six + the output trim, G8-5 ruling 2026-10-17)
    static constexpr float kMinMass = 0.0f;
    static constexpr float kMaxMass = 100.0f;
    static constexpr float kNeutralMass = 50.0f;
    static constexpr std::size_t kControlInterval = 32;      // FR-049 (ProfundumCore::kControlInterval)
    /// Full 0 -> 100 traversal time (C-7 (i), RULED 2026-10-17).
    static constexpr float kMassSweepMs = 40.0f;
    /// SC-004's per-interval bound reference: D-7's 20 ms half sweep (C-7 (i), RULED 2026-10-17).
    static constexpr float kSlewBoundRefMs = 20.0f;

    /// The six targets, in component units. Doubles as the base (the user's own values = Mass 50, D-1).
    struct Targets {
        float depth = 0.5f;                                                // SpectralShapeRecipe::Controls::depth [0, 1]
        float body = 0.5f;                                                 // SpectralShapeRecipe::Controls::body [0, 1]
        float transientDurationMs = TransientShaper::kDefaultDurationMs;   // TransientShaper [5, 250] ms
        float foldAmount = 0.0f;                                           // HarmonicFold::setAmount [0, 100]
        float compressionAmount = 0.0f;                                    // BassCompressor::setAmount [0, 1]
        float anchorLevel = 0.5f;                                          // AnchorOscillator::Controls::level [0, 1]
        float outputTrimDb = 0.0f;                                         // MassDynamics::setOutputTrimDb [-12, 12] dB (G8-5 ruling 2026-10-17)
    };

    struct Row {
        Target target;
        float lo;
        float hi;
        float amountDown;   // applied as -amountDown * s below Mass 50
        float amountUp;     // applied as +amountUp * s above Mass 50
        ModCurve curve;
    };

    /// Mass law table (plan S8.2 seed, C-7 (i) applied; T019 harvests and pastes the calibrated rows).
    // HARVEST-BEGIN mass-law-rows
    // harvested 2026-10-18 from evidence/mass-law-table.txt (plan S8)
    static constexpr std::array<Row, kNumTargets> kRows{{
        {.target = Target::Depth, .lo = 0.0f, .hi = 1.0f, .amountDown = -0.5f, .amountUp = -0.5f, .curve = ModCurve::Linear},
        {.target = Target::Body, .lo = 0.0f, .hi = 1.0f, .amountDown = 0.5f, .amountUp = 0.400000006f, .curve = ModCurve::Linear},
        {.target = Target::TransientDuration, .lo = 5.0f, .hi = 250.0f, .amountDown = 30.0f, .amountUp = 110.0f, .curve = ModCurve::Linear},
        {.target = Target::FoldAmount, .lo = 0.0f, .hi = 100.0f, .amountDown = 50.0f, .amountUp = 61.328125f, .curve = ModCurve::SCurve},
        {.target = Target::CompressionAmount, .lo = 0.0f, .hi = 1.0f, .amountDown = 0.5f, .amountUp = 1.0f, .curve = ModCurve::Linear},
        {.target = Target::AnchorLevel, .lo = 0.0f, .hi = 1.0f, .amountDown = 0.5f, .amountUp = 0.5f, .curve = ModCurve::Linear},
        {.target = Target::OutputTrim, .lo = -12.0f, .hi = 12.0f, .amountDown = -2.0493834f, .amountUp = -0.0f, .curve = ModCurve::Linear},
    }};
    // HARVEST-END mass-law-rows

    /// Largest slope of applyModCurve over [0, 1] (Linear 1, Exponential 2, SCurve 1.5). Stepped is
    /// forbidden in kRows and returns 0 here.
    [[nodiscard]] static constexpr float maxSlope(ModCurve curve) noexcept {
        switch (curve) {
            case ModCurve::Linear: return 1.0f;
            case ModCurve::Exponential: return 2.0f;
            case ModCurve::SCurve: return 1.5f;
            case ModCurve::Stepped: return 0.0f;
        }
        return 0.0f;
    }

    // ---- pure (FR-026): same inputs -> bit-identical outputs; no state ----

    /// One row. A non-finite mass becomes kNeutralMass; a non-finite base becomes the Targets{}
    /// default of the row's field (the pure function has no "previous": FR-029's keep-previous rule
    /// lives in setMass / setBase).
    [[nodiscard]] static float evaluateRow(const Row& row, float mass, float base) noexcept {
        // FR-026 (bit-identical outputs for the same inputs, neutral at kNeutralMass) needs IEEE order here. Under
        // the macOS SDK's -ffast-math, clang rewrote (mass - 50) * 0.02f as fma(mass, 0.02f, -1): -2.2e-8 at Mass 50,
        // so Mass 50 was not neutral and the same evaluate() differed by call site (macOS arm64 CI, 2026-10-25).
#if defined(__clang__)
#pragma clang fp contract(off) reassociate(off)
#endif
        if (!detail::isFinite(mass)) mass = kNeutralMass;
        if (!detail::isFinite(base)) base = fieldOf(Targets{}, row.target);
        mass = std::clamp(mass, kMinMass, kMaxMass);
        const float u = (mass - kNeutralMass) * 0.02f;
        const float s = applyModCurve(row.curve, u >= 0.0f ? u : -u);
        return std::clamp(base + (u >= 0.0f ? row.amountUp * s : -row.amountDown * s), row.lo, row.hi);
    }

    /// The six rows applied to the six fields, in Target order.
    [[nodiscard]] static Targets evaluate(float mass, const Targets& base) noexcept {
        Targets out{};
        for (const Row& row : kRows) {
            fieldOf(out, row.target) = evaluateRow(row, mass, fieldOf(base, row.target));
        }
        return out;
    }

    // ---- stateful part (FR-025, FR-029) ----

    /// SlewLimiter at the interval rate fs / 32: 100 Mass units per kMassSweepMs. Then reset().
    void prepare(double sampleRate) noexcept {
        slew_.configure(kMaxMass / kMassSweepMs, static_cast<float>(sampleRate / static_cast<double>(kControlInterval)));
        prepared_ = true;
        reset();
    }

    /// Snap the smoothed Mass to the pending Mass.
    void reset() noexcept { slew_.snapTo(pendingMass_); }

    /// Non-finite keeps the previous pending value; finite values clamp to [0, 100].
    void setMass(float mass) noexcept {
        if (!detail::isFinite(mass)) return;
        pendingMass_ = std::clamp(mass, kMinMass, kMaxMass);
        slew_.setTarget(pendingMass_);
    }

    /// Per field: non-finite keeps the previous field; finite values clamp to the row's [lo, hi].
    void setBase(const Targets& base) noexcept {
        for (const Row& row : kRows) {
            const float v = fieldOf(base, row.target);
            if (detail::isFinite(v)) fieldOf(base_, row.target) = std::clamp(v, row.lo, row.hi);
        }
    }

    /// One SlewLimiter step (one 32-sample interval); returns the smoothed Mass. Before prepare() it
    /// returns the current smoothed value unchanged.
    [[nodiscard]] float advanceInterval() noexcept {
        if (!prepared_) return slew_.getCurrentValue();
        return slew_.process();
    }

    /// evaluate(smoothedMass(), base()).
    [[nodiscard]] Targets currentTargets() const noexcept { return evaluate(smoothedMass(), base_); }
    [[nodiscard]] float smoothedMass() const noexcept { return slew_.getCurrentValue(); }
    [[nodiscard]] float pendingMass() const noexcept { return pendingMass_; }
    [[nodiscard]] const Targets& base() const noexcept { return base_; }

private:
    [[nodiscard]] static float& fieldOf(Targets& t, Target target) noexcept {
        switch (target) {
            case Target::Depth: return t.depth;
            case Target::Body: return t.body;
            case Target::TransientDuration: return t.transientDurationMs;
            case Target::FoldAmount: return t.foldAmount;
            case Target::CompressionAmount: return t.compressionAmount;
            case Target::AnchorLevel: return t.anchorLevel;
            case Target::OutputTrim: return t.outputTrimDb;
        }
        return t.depth;
    }
    [[nodiscard]] static float fieldOf(const Targets& t, Target target) noexcept {
        switch (target) {
            case Target::Depth: return t.depth;
            case Target::Body: return t.body;
            case Target::TransientDuration: return t.transientDurationMs;
            case Target::FoldAmount: return t.foldAmount;
            case Target::CompressionAmount: return t.compressionAmount;
            case Target::AnchorLevel: return t.anchorLevel;
            case Target::OutputTrim: return t.outputTrimDb;
        }
        return t.depth;
    }

    SlewLimiter slew_{kNeutralMass};
    float pendingMass_ = kNeutralMass;
    Targets base_{};
    bool prepared_ = false;
};

// ---- kRows invariants (plan S7.2; tasks.md T009) ----
static_assert(
    [] {
        for (std::size_t i = 0; i < MassLaw::kRows.size(); ++i) {
            if (static_cast<std::size_t>(MassLaw::kRows[i].target) != i) return false;
        }
        return MassLaw::kRows.size() == MassLaw::kNumTargets;
    }(),
    "MassLaw::kRows: exactly one row per target, in Target order");
static_assert(
    [] {
        for (const auto& r : MassLaw::kRows) {
            if (!(r.lo < r.hi)) return false;
        }
        return true;
    }(),
    "MassLaw::kRows: lo < hi");
static_assert(
    [] {
        for (const auto& r : MassLaw::kRows) {
            if (!(r.amountUp * r.amountDown >= 0.0f)) return false;
            // D-2: Depth falls; the output trim falls (non-increasing, G8-5 ruling 2026-10-17); the other five rise
            const bool falls = (r.target == MassLaw::Target::Depth || r.target == MassLaw::Target::OutputTrim);
            if (falls ? (r.amountUp > 0.0f || r.amountDown > 0.0f) : (r.amountUp < 0.0f || r.amountDown < 0.0f))
                return false;
        }
        return true;
    }(),
    "MassLaw::kRows: amounts carry the D-2 direction (Depth and OutputTrim <= 0, the other five >= 0)");
static_assert(
    [] {
        for (const auto& r : MassLaw::kRows) {
            if (r.curve == ModCurve::Stepped) return false;
        }
        return true;
    }(),
    "MassLaw::kRows: Stepped curve forbidden");
static_assert(
    [] {
        for (const auto& r : MassLaw::kRows) {
            const float aUp = r.amountUp >= 0.0f ? r.amountUp : -r.amountUp;
            const float aDown = r.amountDown >= 0.0f ? r.amountDown : -r.amountDown;
            const float a = aUp > aDown ? aUp : aDown;
            if (a * MassLaw::maxSlope(r.curve) >
                (r.hi - r.lo) * MassLaw::kMassSweepMs / (2.0f * MassLaw::kSlewBoundRefMs))
                return false;
        }
        return true;
    }(),
    "MassLaw::kRows: slew bound (plan C-7): max(|aUp|, |aDown|) * maxSlope <= range * kMassSweepMs / (2 * kSlewBoundRefMs)");
static_assert(MassLaw::kRows[static_cast<std::size_t>(MassLaw::Target::TransientDuration)].lo ==
                      TransientShaper::kMinDurationMs &&
                  MassLaw::kRows[static_cast<std::size_t>(MassLaw::Target::TransientDuration)].hi ==
                      TransientShaper::kMaxDurationMs,
              "MassLaw::kRows: the duration row's [lo, hi] is TransientShaper's range");

} // namespace Krate::DSP
