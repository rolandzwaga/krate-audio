// ==============================================================================
// Layer 3: System - Profundum Phase 9 (specs/profundum-phase9-voice-engine)
// ==============================================================================
// ProfundumMacroMatrix (four personality macros -> sixteen target offsets on the resolved morph state).
//   Serves: FR-050 - FR-056; SC-008 (unit arm).
//   Encodes: plan S4 (S4.1 types, S4.2 row table, S4.3 class), S9.3 (contribution and application);
//            C-7 (the compression row is handed post-law by the engine; apply() never touches it).
//   Spec:  specs/profundum-phase9-voice-engine/spec.md
//   Plan:  specs/profundum-phase9-voice-engine/plan.md
//   Tasks: specs/profundum-phase9-voice-engine/tasks.md (T002)
//
// A pattern copy of VoragoMacroMatrix (FR-050): a constexpr row table evaluated through applyModCurve, with
// static_assert'ed invariants. Unlike Vorago the base is the resolved MorphState's own field (D-4), so the table has
// no base column, and all four macros are bipolar about the neutral 0.5 (D-7).
//
// Real-time safety: four floats of state, no heap, every method noexcept and allocation / lock / I/O free.
// ==============================================================================

#pragma once

#include <krate/dsp/core/db_utils.h>              // L0: detail::isFinite
#include <krate/dsp/core/modulation_curves.h>     // L0: applyModCurve
#include <krate/dsp/core/modulation_types.h>      // L0: ModCurve
#include <krate/dsp/primitives/envelope_utils.h>  // L1: kMinEnvelopeTimeMs, kMaxEnvelopeTimeMs
#include <krate/dsp/systems/morph_state.h>        // L3 peer: MorphState

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <type_traits>

namespace Krate::DSP {

/// FR-051: the four personality macros.
enum class ProfundumMacro : std::uint8_t { Weight = 0, DarkBright, TightLoose, CleanViolent, Count };

/// FR-052: the sixteen targets, grouped by their owning macro (plan S4.1).
enum class ProfundumMacroTarget : std::uint8_t {
    RecipeDepth = 0, RecipeBody, AnchorLevel,                                         // Weight
    RecipeEdge, RecipeShift, Shape, CharacterTone,                                    // Dark <-> Bright
    AmpAttackOct, AmpDecayOct, AmpReleaseOct, TransientAttackDb, CompressionAmount,   // Tight <-> Loose
    ExcitationMix, ExcitationDrive, FoldAmount, CharacterAmount,                      // Clean <-> Violent
    Count
};

/// One row: `amount` is the contribution at the macro's end (u = +-1), in the target's units (plan S4.2).
struct ProfundumMacroRow {
    ProfundumMacro macro;
    ProfundumMacroTarget target;
    float amount;
    ModCurve curve;
};

/// FR-055: the stored coordinates, neutral 0.5 each (FR-051, D-7).
struct ProfundumMacroValues {
    float weight = 0.5f;
    float darkBright = 0.5f;
    float tightLoose = 0.5f;
    float cleanViolent = 0.5f;
};

class ProfundumMacroMatrix {
public:
    static constexpr float kNeutral = 0.5f;   // FR-051 / D-7, all four
    static constexpr std::size_t kNumMacros = static_cast<std::size_t>(ProfundumMacro::Count);
    static constexpr std::size_t kNumTargets = static_cast<std::size_t>(ProfundumMacroTarget::Count);

    // Plan S4.2 seed table. Directions are normative (FR-052); amounts and curves are harvested (S10.5, T014), which
    // may change magnitudes and curves, never signs. T014 pastes ONLY between the two marker lines.
    // HARVEST-BEGIN macro-rows
    // harvested 2026-10-21 from evidence/macro-rows.txt (plan S10.5, T014)
    static constexpr std::array<ProfundumMacroRow, kNumTargets> kRows{{
        {.macro = ProfundumMacro::Weight, .target = ProfundumMacroTarget::RecipeDepth, .amount = 0.5625f, .curve = ModCurve::Linear},
        {.macro = ProfundumMacro::Weight, .target = ProfundumMacroTarget::RecipeBody, .amount = 0.5625f, .curve = ModCurve::Linear},
        {.macro = ProfundumMacro::Weight, .target = ProfundumMacroTarget::AnchorLevel, .amount = 0.5625f, .curve = ModCurve::Linear},
        {.macro = ProfundumMacro::DarkBright, .target = ProfundumMacroTarget::RecipeEdge, .amount = 0.5f, .curve = ModCurve::Linear},
        {.macro = ProfundumMacro::DarkBright, .target = ProfundumMacroTarget::RecipeShift, .amount = 0.5f, .curve = ModCurve::Linear},
        {.macro = ProfundumMacro::DarkBright, .target = ProfundumMacroTarget::Shape, .amount = 0.0f, .curve = ModCurve::Linear},
        {.macro = ProfundumMacro::DarkBright, .target = ProfundumMacroTarget::CharacterTone, .amount = 0.300000012f, .curve = ModCurve::Linear},
        {.macro = ProfundumMacro::TightLoose, .target = ProfundumMacroTarget::AmpAttackOct, .amount = 5.0f, .curve = ModCurve::Linear},
        {.macro = ProfundumMacro::TightLoose, .target = ProfundumMacroTarget::AmpDecayOct, .amount = 5.0f, .curve = ModCurve::Linear},
        {.macro = ProfundumMacro::TightLoose, .target = ProfundumMacroTarget::AmpReleaseOct, .amount = 5.0f, .curve = ModCurve::Linear},
        {.macro = ProfundumMacro::TightLoose, .target = ProfundumMacroTarget::TransientAttackDb, .amount = -15.0f, .curve = ModCurve::Linear},
        {.macro = ProfundumMacro::TightLoose, .target = ProfundumMacroTarget::CompressionAmount, .amount = -1.25f, .curve = ModCurve::Linear},
        {.macro = ProfundumMacro::CleanViolent, .target = ProfundumMacroTarget::ExcitationMix, .amount = 0.5f, .curve = ModCurve::Linear},
        {.macro = ProfundumMacro::CleanViolent, .target = ProfundumMacroTarget::ExcitationDrive, .amount = 0.5f, .curve = ModCurve::Linear},
        {.macro = ProfundumMacro::CleanViolent, .target = ProfundumMacroTarget::FoldAmount, .amount = 50.0f, .curve = ModCurve::Linear},
        {.macro = ProfundumMacro::CleanViolent, .target = ProfundumMacroTarget::CharacterAmount, .amount = 0.5f, .curve = ModCurve::Linear},
    }};
    // HARVEST-END macro-rows

    // ---- FR-050 table invariants (constexpr loops over kRows; static_assert'ed after the class) ----

    /// FR-052's documented owner of each target.
    [[nodiscard]] static constexpr ProfundumMacro documentedOwner(ProfundumMacroTarget t) noexcept {
        const auto i = static_cast<std::size_t>(t);
        if (i <= static_cast<std::size_t>(ProfundumMacroTarget::AnchorLevel)) {
            return ProfundumMacro::Weight;
        }
        if (i <= static_cast<std::size_t>(ProfundumMacroTarget::CharacterTone)) {
            return ProfundumMacro::DarkBright;
        }
        if (i <= static_cast<std::size_t>(ProfundumMacroTarget::CompressionAmount)) {
            return ProfundumMacro::TightLoose;
        }
        if (i < kNumTargets) {
            return ProfundumMacro::CleanViolent;
        }
        return ProfundumMacro::Count;
    }

    [[nodiscard]] static constexpr bool everyTargetClaimedOnce() noexcept {
        for (std::size_t t = 0; t < kNumTargets; ++t) {
            int count = 0;
            for (const auto& row : kRows) {
                if (static_cast<std::size_t>(row.target) == t) {
                    ++count;
                }
            }
            if (count != 1) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] static constexpr bool noSteppedCurve() noexcept {
        for (const auto& row : kRows) {
            if (row.curve == ModCurve::Stepped) {
                return false;
            }
        }
        return true;
    }

    [[nodiscard]] static constexpr bool ownersDocumented() noexcept {
        for (const auto& row : kRows) {
            if (row.macro != documentedOwner(row.target)) {
                return false;
            }
        }
        return true;
    }

    /// FR-052 signs: TransientAttackDb and CompressionAmount fall as their macro rises; every other row rises.
    [[nodiscard]] static constexpr bool signsDocumented() noexcept {
        for (const auto& row : kRows) {
            if (row.target == ProfundumMacroTarget::Shape && row.amount == 0.0f) {
                continue;   // ruling B10-2 (2026-10-20): SHAPE is dropped from Dark <-> Bright (amount 0)
            }
            const bool negative = row.target == ProfundumMacroTarget::TransientAttackDb
                                  || row.target == ProfundumMacroTarget::CompressionAmount;
            if (negative ? !(row.amount < 0.0f) : !(row.amount > 0.0f)) {
                return false;
            }
        }
        return true;
    }

    // ---- coordinates (FR-055, FR-061) ----

    /// Non-finite keeps the previous value; finite clamps to [0, 1]; an undeclared macro is ignored.
    void setMacro(ProfundumMacro m, float value) noexcept {
        const auto i = static_cast<std::size_t>(m);
        if (i >= kNumMacros || !detail::isFinite(value)) {
            return;
        }
        values_[i] = std::clamp(value, 0.0f, 1.0f);
    }

    /// Field-wise, same rules as setMacro.
    void setMacros(const ProfundumMacroValues& v) noexcept {
        setMacro(ProfundumMacro::Weight, v.weight);
        setMacro(ProfundumMacro::DarkBright, v.darkBright);
        setMacro(ProfundumMacro::TightLoose, v.tightLoose);
        setMacro(ProfundumMacro::CleanViolent, v.cleanViolent);
    }

    [[nodiscard]] ProfundumMacroValues macros() const noexcept {
        return ProfundumMacroValues{.weight = values_[0],
                                    .darkBright = values_[1],
                                    .tightLoose = values_[2],
                                    .cleanViolent = values_[3]};
    }

    // ---- evaluation (plan S9.3) ----

    /// Per-target contribution. Each target is claimed by exactly one row (static_assert below), so the row's value is
    /// assigned, not summed onto +0: a summed -0 (negative amount at the neutral) would read back as +0. Pure.
    [[nodiscard]] std::array<float, kNumTargets> contributions() const noexcept {
        std::array<float, kNumTargets> out{};
        for (const auto& row : kRows) {
            out[static_cast<std::size_t>(row.target)] = contributionOf(row);
        }
        return out;
    }

    /// FR-053: offsets the resolved state, rows 0-10 and 12-15, clamping each field to its range. A +-0 contribution is
    /// skipped, so the neutral leaves the state bit-identical (SC-008). CompressionAmount is not a MorphState field:
    /// the engine hands it post-law through compressionContribution() (plan S1 C-7).
    void apply(MorphState& s) const noexcept {
        using T = ProfundumMacroTarget;
        const auto c = contributions();
        const auto at = [&c](T t) noexcept { return c[static_cast<std::size_t>(t)]; };

        addClamped(s.recipe.depth, at(T::RecipeDepth), 0.0f, 1.0f);
        addClamped(s.recipe.body, at(T::RecipeBody), 0.0f, 1.0f);
        addClamped(s.anchor.level, at(T::AnchorLevel), 0.0f, 1.0f);
        addClamped(s.recipe.edge, at(T::RecipeEdge), 0.0f, 1.0f);
        addClamped(s.recipe.shift, at(T::RecipeShift), -1.0f, 1.0f);
        addClamped(s.shape, at(T::Shape), 0.0f, 1.0f);
        addClamped(s.characterTone, at(T::CharacterTone), 0.0f, 1.0f);
        scaleTimeClamped(s.ampEnv.attackMs, at(T::AmpAttackOct));
        scaleTimeClamped(s.ampEnv.decayMs, at(T::AmpDecayOct));
        scaleTimeClamped(s.ampEnv.releaseMs, at(T::AmpReleaseOct));
        addClamped(s.transientAttackDb, at(T::TransientAttackDb), kTransientMinDb, kTransientMaxDb);
        addClamped(s.excitation.mix, at(T::ExcitationMix), 0.0f, 1.0f);
        addClamped(s.excitation.drive, at(T::ExcitationDrive), 0.0f, 1.0f);
        addClamped(s.foldAmount, at(T::FoldAmount), 0.0f, kFoldMax);
        addClamped(s.characterAmount, at(T::CharacterAmount), 0.0f, 1.0f);
    }

    /// Row 11 (plan S1 C-7): the engine hands clamp(law compressionAmount + this, 0, 1), the add skipped at +-0.
    [[nodiscard]] float compressionContribution() const noexcept {
        return contributions()[static_cast<std::size_t>(ProfundumMacroTarget::CompressionAmount)];
    }

private:
    // Field ranges (S9.3). TransientShaper::kMinGainDb / kMaxGainDb (transient_shaper.h:78-79) and the Fold range
    // (harmonic_fold.h:166-168, morph_state.h kFieldTable "foldAmount") as literals: plan S2 fixes this header's
    // include list, which has neither header.
    static constexpr float kTransientMinDb = -12.0f;
    static constexpr float kTransientMaxDb = 12.0f;
    static constexpr float kFoldMax = 100.0f;

    /// u = (m - 0.5) * 2; c = amount * applyModCurve(curve, |u|) * sign(u) (the Vorago form, vorago_macro_matrix.h:1170-1173).
    [[nodiscard]] float contributionOf(const ProfundumMacroRow& row) const noexcept {
        const float u = (values_[static_cast<std::size_t>(row.macro)] - kNeutral) * 2.0f;
        return row.amount * applyModCurve(row.curve, std::fabs(u)) * (u < 0.0f ? -1.0f : 1.0f);
    }

    [[nodiscard]] static bool isNonZero(float c) noexcept {
        return (std::bit_cast<std::uint32_t>(c) & 0x7FFFFFFFu) != 0u;
    }

    static void addClamped(float& x, float c, float lo, float hi) noexcept {
        if (!isNonZero(c)) {
            return;
        }
        const float v = x + c;
        if (detail::isFinite(v)) {
            x = std::clamp(v, lo, hi);
        }
    }

    /// Amp times move in the log domain: t' = clamp(t * 2^c, kMinEnvelopeTimeMs, kMaxEnvelopeTimeMs) (S9.3).
    static void scaleTimeClamped(float& t, float c) noexcept {
        if (!isNonZero(c)) {
            return;
        }
        const float v = t * std::exp2(c);
        if (detail::isFinite(v)) {
            t = std::clamp(v, kMinEnvelopeTimeMs, kMaxEnvelopeTimeMs);
        }
    }

    std::array<float, kNumMacros> values_{kNeutral, kNeutral, kNeutral, kNeutral};
};

// FR-050 invariants (plan S4.2).
static_assert(ProfundumMacroMatrix::kNumTargets == 16 && ProfundumMacroMatrix::kNumMacros == 4);
static_assert(ProfundumMacroMatrix::everyTargetClaimedOnce(), "every ProfundumMacroTarget must be claimed exactly once");
static_assert(ProfundumMacroMatrix::noSteppedCurve(), "macro rows use Linear / Exponential / SCurve only");
static_assert(ProfundumMacroMatrix::ownersDocumented(), "each row's macro must be its target's FR-052 owner");
static_assert(ProfundumMacroMatrix::signsDocumented(), "FR-052 signs: rows 10 and 11 negative, every other positive (row 5 dropped, B10-2)");
static_assert(std::is_trivially_copyable_v<ProfundumMacroMatrix>);

} // namespace Krate::DSP
