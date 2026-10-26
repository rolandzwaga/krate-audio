// ==============================================================================
// Profundum Phase 9 (specs/profundum-phase9-voice-engine) - profundum_macro_matrix_test.cpp
// ==============================================================================
// ProfundumMacroMatrix unit cases (T002): FR-050 - FR-056, SC-008 (unit arm).
//   plan S4 (types, row table, class), S9.3 (contribution and application), C-7 (compression row post-law),
//   C-8 (base A_s = showcase A with ampEnv.sustain = 1).
// Includes every Phase 9 header so each is compiled by every gate from Group 1 on (tasks.md deviation 1).
// ==============================================================================

#include <krate/dsp/systems/profundum_engine.h>
#include <krate/dsp/systems/profundum_engine_observer.h>
#include <krate/dsp/systems/profundum_macro_matrix.h>
#include <krate/dsp/systems/profundum_motion.h>
#include <krate/dsp/systems/profundum_voice.h>

#include "bass_morph_state_helpers.h"

#include <krate/dsp/core/modulation_curves.h>
#include <krate/dsp/core/modulation_types.h>
#include <krate/dsp/primitives/envelope_utils.h>
#include <krate/dsp/systems/morph_state.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iomanip>
#include <random>

namespace {

using Krate::DSP::ModCurve;
using Krate::DSP::MorphState;
using Krate::DSP::ProfundumMacro;
using Krate::DSP::ProfundumMacroMatrix;
using Krate::DSP::ProfundumMacroTarget;
using Krate::DSP::ProfundumMacroValues;

constexpr std::size_t veNumTargets = static_cast<std::size_t>(ProfundumMacroTarget::Count);

// ---- FR-052's documented owners and signs, written independently of the header ----

constexpr ProfundumMacro veExpectedOwner(ProfundumMacroTarget t) noexcept {
    switch (t) {
        case ProfundumMacroTarget::RecipeDepth:
        case ProfundumMacroTarget::RecipeBody:
        case ProfundumMacroTarget::AnchorLevel:
            return ProfundumMacro::Weight;
        case ProfundumMacroTarget::RecipeEdge:
        case ProfundumMacroTarget::RecipeShift:
        case ProfundumMacroTarget::Shape:
        case ProfundumMacroTarget::CharacterTone:
            return ProfundumMacro::DarkBright;
        case ProfundumMacroTarget::AmpAttackOct:
        case ProfundumMacroTarget::AmpDecayOct:
        case ProfundumMacroTarget::AmpReleaseOct:
        case ProfundumMacroTarget::TransientAttackDb:
        case ProfundumMacroTarget::CompressionAmount:
            return ProfundumMacro::TightLoose;
        case ProfundumMacroTarget::ExcitationMix:
        case ProfundumMacroTarget::ExcitationDrive:
        case ProfundumMacroTarget::FoldAmount:
        case ProfundumMacroTarget::CharacterAmount:
            return ProfundumMacro::CleanViolent;
        case ProfundumMacroTarget::Count:
            break;
    }
    return ProfundumMacro::Count;
}

constexpr bool veExpectNegative(ProfundumMacroTarget t) noexcept {
    return t == ProfundumMacroTarget::TransientAttackDb || t == ProfundumMacroTarget::CompressionAmount;
}

// ---- the same predicates as constexpr loops over kRows (STATIC_REQUIRE'd) ----

constexpr bool veEveryTargetOnce() noexcept {
    for (std::size_t t = 0; t < veNumTargets; ++t) {
        int count = 0;
        for (const auto& row : ProfundumMacroMatrix::kRows) {
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

constexpr bool veNoStepped() noexcept {
    for (const auto& row : ProfundumMacroMatrix::kRows) {
        if (row.curve == ModCurve::Stepped) {
            return false;
        }
    }
    return true;
}

constexpr bool veOwnersMatch() noexcept {
    for (const auto& row : ProfundumMacroMatrix::kRows) {
        if (row.macro != veExpectedOwner(row.target)) {
            return false;
        }
    }
    return true;
}

constexpr bool veSignsMatch() noexcept {
    for (const auto& row : ProfundumMacroMatrix::kRows) {
        if (row.target == ProfundumMacroTarget::Shape && row.amount == 0.0f) {
            continue;   // ruling B10-2 (2026-10-20): SHAPE dropped from Dark <-> Bright (amount 0)
        }
        if (veExpectNegative(row.target) ? !(row.amount < 0.0f) : !(row.amount > 0.0f)) {
            return false;
        }
    }
    return true;
}

// ---- helpers ----

std::uint32_t veBits(float x) noexcept { return std::bit_cast<std::uint32_t>(x); }
bool veIsZero(float x) noexcept { return (veBits(x) & 0x7FFFFFFFu) == 0u; }

/// A real non-finite bit pattern at runtime under any FP mode (volatile sink defeats constant folding).
float veFromBits(std::uint32_t bits) noexcept {
    volatile std::uint32_t sink = bits;
    const std::uint32_t read = sink;
    return std::bit_cast<float>(read);
}

float veMacroValue(const ProfundumMacroValues& v, ProfundumMacro m) noexcept {
    switch (m) {
        case ProfundumMacro::Weight: return v.weight;
        case ProfundumMacro::DarkBright: return v.darkBright;
        case ProfundumMacro::TightLoose: return v.tightLoose;
        case ProfundumMacro::CleanViolent: return v.cleanViolent;
        case ProfundumMacro::Count: break;
    }
    return 0.0f;
}

/// The MorphState leaf each target writes (S9.3). CompressionAmount has none (C-7): field == nullptr.
struct VeField {
    ProfundumMacroTarget target;
    float& (*field)(MorphState&);
    float lo;
    float hi;
    bool logTime;   // AmpAttack/Decay/ReleaseOct: t * exp2(c), clamped to the envelope time range
};

const std::array<VeField, veNumTargets>& veFields() {
    using T = ProfundumMacroTarget;
    constexpr float tMin = Krate::DSP::kMinEnvelopeTimeMs;
    constexpr float tMax = Krate::DSP::kMaxEnvelopeTimeMs;
    static const std::array<VeField, veNumTargets> fields{{
        {.target = T::RecipeDepth, .field = +[](MorphState& s) -> float& { return s.recipe.depth; },
         .lo = 0.0f, .hi = 1.0f, .logTime = false},
        {.target = T::RecipeBody, .field = +[](MorphState& s) -> float& { return s.recipe.body; },
         .lo = 0.0f, .hi = 1.0f, .logTime = false},
        {.target = T::AnchorLevel, .field = +[](MorphState& s) -> float& { return s.anchor.level; },
         .lo = 0.0f, .hi = 1.0f, .logTime = false},
        {.target = T::RecipeEdge, .field = +[](MorphState& s) -> float& { return s.recipe.edge; },
         .lo = 0.0f, .hi = 1.0f, .logTime = false},
        {.target = T::RecipeShift, .field = +[](MorphState& s) -> float& { return s.recipe.shift; },
         .lo = -1.0f, .hi = 1.0f, .logTime = false},
        {.target = T::Shape, .field = +[](MorphState& s) -> float& { return s.shape; },
         .lo = 0.0f, .hi = 1.0f, .logTime = false},
        {.target = T::CharacterTone, .field = +[](MorphState& s) -> float& { return s.characterTone; },
         .lo = 0.0f, .hi = 1.0f, .logTime = false},
        {.target = T::AmpAttackOct, .field = +[](MorphState& s) -> float& { return s.ampEnv.attackMs; },
         .lo = tMin, .hi = tMax, .logTime = true},
        {.target = T::AmpDecayOct, .field = +[](MorphState& s) -> float& { return s.ampEnv.decayMs; },
         .lo = tMin, .hi = tMax, .logTime = true},
        {.target = T::AmpReleaseOct, .field = +[](MorphState& s) -> float& { return s.ampEnv.releaseMs; },
         .lo = tMin, .hi = tMax, .logTime = true},
        {.target = T::TransientAttackDb, .field = +[](MorphState& s) -> float& { return s.transientAttackDb; },
         .lo = -12.0f, .hi = 12.0f, .logTime = false},
        {.target = T::CompressionAmount, .field = nullptr, .lo = 0.0f, .hi = 1.0f, .logTime = false},
        {.target = T::ExcitationMix, .field = +[](MorphState& s) -> float& { return s.excitation.mix; },
         .lo = 0.0f, .hi = 1.0f, .logTime = false},
        {.target = T::ExcitationDrive, .field = +[](MorphState& s) -> float& { return s.excitation.drive; },
         .lo = 0.0f, .hi = 1.0f, .logTime = false},
        {.target = T::FoldAmount, .field = +[](MorphState& s) -> float& { return s.foldAmount; },
         .lo = 0.0f, .hi = 100.0f, .logTime = false},
        {.target = T::CharacterAmount, .field = +[](MorphState& s) -> float& { return s.characterAmount; },
         .lo = 0.0f, .hi = 1.0f, .logTime = false},
    }};
    return fields;
}

ProfundumMacroMatrix veMatrixWith(ProfundumMacro m, float value) noexcept {
    ProfundumMacroMatrix matrix;
    matrix.setMacro(m, value);
    return matrix;
}

} // namespace

// FR-050: the table claims every target once, never Stepped, each row owned by its documented macro, FR-052 signs.
TEST_CASE("ProfundumMacro_TableInvariants", "[systems][profundum][voice-engine][fr-050]") {
    STATIC_REQUIRE(ProfundumMacroMatrix::kRows.size() == 16);
    STATIC_REQUIRE(ProfundumMacroMatrix::kNumTargets == 16);
    STATIC_REQUIRE(veEveryTargetOnce());
    STATIC_REQUIRE(veNoStepped());
    STATIC_REQUIRE(veOwnersMatch());
    STATIC_REQUIRE(veSignsMatch());

    for (std::size_t t = 0; t < veNumTargets; ++t) {
        int count = 0;
        for (const auto& row : ProfundumMacroMatrix::kRows) {
            if (static_cast<std::size_t>(row.target) == t) {
                ++count;
            }
        }
        INFO("target " << t);
        REQUIRE(count == 1);
    }
    for (const auto& row : ProfundumMacroMatrix::kRows) {
        INFO("target " << static_cast<int>(row.target) << " amount " << std::setprecision(6) << row.amount);
        REQUIRE(row.curve != ModCurve::Stepped);
        REQUIRE(row.macro == veExpectedOwner(row.target));
        if (row.target == ProfundumMacroTarget::Shape) {
            REQUIRE(veIsZero(row.amount));   // ruling B10-2 (2026-10-20): dropped from Dark <-> Bright
        } else if (veExpectNegative(row.target)) {
            REQUIRE(row.amount < 0.0f);
        } else {
            REQUIRE(row.amount > 0.0f);
        }
    }
}

// SC-008 (unit arm), FR-051: four macros at 0.5 leave any valid state bit-identical; every contribution is +-0.
TEST_CASE("ProfundumMacro_NeutralIsIdentity", "[systems][profundum][voice-engine][sc-008][fr-051]") {
    const ProfundumMacroMatrix m;
    const ProfundumMacroValues v = m.macros();
    REQUIRE(veBits(v.weight) == veBits(0.5f));
    REQUIRE(veBits(v.darkBright) == veBits(0.5f));
    REQUIRE(veBits(v.tightLoose) == veBits(0.5f));
    REQUIRE(veBits(v.cleanViolent) == veBits(0.5f));

    const auto c = m.contributions();
    for (std::size_t t = 0; t < veNumTargets; ++t) {
        INFO("target " << t);
        REQUIRE(veIsZero(c[t]));
    }
    REQUIRE(veIsZero(m.compressionContribution()));

    std::mt19937 rng{0x9008u};
    for (int i = 0; i < 1000; ++i) {
        const MorphState s = Krate::DSP::BassMorphTest::randomValidState(rng);
        MorphState s2 = s;
        m.apply(s2);
        INFO("state " << i);
        REQUIRE(Krate::DSP::BassMorphTest::bitEqual(s, s2));
    }
}

// FR-052: each macro at an end moves exactly its own fields, in its documented direction; nothing else changes.
// plan S1 C-8: base A_s = showcase A with ampEnv.sustain = 1; base B as harvested. plan S1 C-7: compression post-law.
TEST_CASE("ProfundumMacro_Directions", "[systems][profundum][voice-engine][fr-052]") {
    auto [a, b] = Krate::DSP::BassMorphTest::showcasePair();
    a.ampEnv.sustain = 1.0f;   // plan S1 C-8
    const std::array<MorphState, 2> bases{a, b};
    const auto& fields = veFields();

    for (std::size_t baseIdx = 0; baseIdx < bases.size(); ++baseIdx) {
        const MorphState& orig = bases[baseIdx];
        for (std::size_t mi = 0; mi < static_cast<std::size_t>(ProfundumMacro::Count); ++mi) {
            const auto macro = static_cast<ProfundumMacro>(mi);
            for (const float atEnd : {1.0f, 0.0f}) {
                const ProfundumMacroMatrix m = veMatrixWith(macro, atEnd);
                const auto c = m.contributions();
                MorphState moved = orig;
                m.apply(moved);

                MorphState restored = moved;
                for (const auto& f : fields) {
                    if (veExpectedOwner(f.target) != macro || f.field == nullptr) {
                        continue;
                    }
                    if (veIsZero(ProfundumMacroMatrix::kRows[static_cast<std::size_t>(f.target)].amount)) {
                        continue;   // ruling B10-2 (2026-10-20): a dropped row (SHAPE of Dark <-> Bright) contributes 0
                    }
                    MorphState origCopy = orig;
                    const float x0 = f.field(origCopy);
                    const float x1 = f.field(moved);
                    const float ct = c[static_cast<std::size_t>(f.target)];
                    const float sign = veExpectNegative(f.target) ? -1.0f : 1.0f;
                    const bool up = (atEnd == 1.0f) ? (sign > 0.0f) : (sign < 0.0f);
                    INFO("base " << baseIdx << " macro " << mi << " end " << atEnd << " target "
                                 << static_cast<int>(f.target) << std::setprecision(6) << " x0 " << x0 << " x1 " << x1
                                 << " c " << ct);
                    REQUIRE(!veIsZero(ct));
                    REQUIRE((ct > 0.0f) == up);
                    if (f.logTime) {
                        const float expected = std::clamp(x0 * std::exp2(ct), f.lo, f.hi);
                        REQUIRE(veBits(x1) == veBits(expected));
                    }
                    if (up) {
                        REQUIRE(x1 >= x0);
                        if (f.logTime ? (x0 * std::exp2(ct) >= f.hi) : (x0 + ct >= f.hi)) {
                            REQUIRE(x1 == f.hi);
                        } else {
                            REQUIRE(x1 > x0);
                        }
                    } else {
                        REQUIRE(x1 <= x0);
                        if (f.logTime ? (x0 * std::exp2(ct) <= f.lo) : (x0 + ct <= f.lo)) {
                            REQUIRE(x1 == f.lo);
                        } else {
                            REQUIRE(x1 < x0);
                        }
                    }
                    f.field(restored) = x0;
                }
                INFO("base " << baseIdx << " macro " << mi << " end " << atEnd << ": no foreign field changes");
                REQUIRE(Krate::DSP::BassMorphTest::bitEqual(restored, orig));
            }
        }
    }

    // Row 10: Loose (tightLoose = 1) lowers the transient attack gain; row 11 (C-7): negative at Loose, positive at Tight.
    {
        const ProfundumMacroMatrix loose = veMatrixWith(ProfundumMacro::TightLoose, 1.0f);
        const ProfundumMacroMatrix tight = veMatrixWith(ProfundumMacro::TightLoose, 0.0f);
        MorphState s = b;
        loose.apply(s);
        REQUIRE(s.transientAttackDb < b.transientAttackDb);
        REQUIRE(loose.compressionContribution() < 0.0f);
        REQUIRE(tight.compressionContribution() > 0.0f);
        // compressionContribution() is row 11 of contributions(); apply() never writes it (C-7).
        REQUIRE(veBits(loose.compressionContribution())
                == veBits(loose.contributions()[static_cast<std::size_t>(ProfundumMacroTarget::CompressionAmount)]));
    }
    // Every other macro hands no compression contribution.
    for (const auto macro : {ProfundumMacro::Weight, ProfundumMacro::DarkBright, ProfundumMacro::CleanViolent}) {
        for (const float atEnd : {0.0f, 1.0f}) {
            REQUIRE(veIsZero(veMatrixWith(macro, atEnd).compressionContribution()));
        }
    }
}

// FR-055, FR-061: the four coordinates round-trip bitwise; non-finite keeps, out-of-range clamps, Count is ignored.
TEST_CASE("ProfundumMacro_ValuesRoundTrip", "[systems][profundum][voice-engine][fr-055][fr-061]") {
    ProfundumMacroMatrix m;
    const ProfundumMacroValues in{.weight = 0.1f, .darkBright = 0.9f, .tightLoose = 0.3f, .cleanViolent = 0.7f};
    m.setMacros(in);
    auto out = m.macros();
    REQUIRE(veBits(out.weight) == veBits(in.weight));
    REQUIRE(veBits(out.darkBright) == veBits(in.darkBright));
    REQUIRE(veBits(out.tightLoose) == veBits(in.tightLoose));
    REQUIRE(veBits(out.cleanViolent) == veBits(in.cleanViolent));

    for (const std::uint32_t bits : {0x7FC00000u, 0x7F800000u, 0xFF800000u}) {
        m.setMacro(ProfundumMacro::Weight, veFromBits(bits));
        INFO("bits 0x" << std::hex << bits);
        REQUIRE(veBits(m.macros().weight) == veBits(0.1f));
    }

    m.setMacro(ProfundumMacro::Weight, 1.5f);
    REQUIRE(veBits(m.macros().weight) == veBits(1.0f));
    m.setMacro(ProfundumMacro::Weight, -0.5f);
    REQUIRE(veBits(m.macros().weight) == veBits(0.0f));

    const ProfundumMacroValues before = m.macros();
    m.setMacro(ProfundumMacro::Count, 0.2f);
    out = m.macros();
    REQUIRE(veBits(out.weight) == veBits(before.weight));
    REQUIRE(veBits(out.darkBright) == veBits(before.darkBright));
    REQUIRE(veBits(out.tightLoose) == veBits(before.tightLoose));
    REQUIRE(veBits(out.cleanViolent) == veBits(before.cleanViolent));

    // setMacros obeys the same rules field-wise: a NaN field keeps, an out-of-range field clamps.
    m.setMacros({.weight = veFromBits(0x7FC00000u), .darkBright = 2.0f, .tightLoose = -1.0f, .cleanViolent = 0.25f});
    out = m.macros();
    REQUIRE(veBits(out.weight) == veBits(before.weight));
    REQUIRE(veBits(out.darkBright) == veBits(1.0f));
    REQUIRE(veBits(out.tightLoose) == veBits(0.0f));
    REQUIRE(veBits(out.cleanViolent) == veBits(0.25f));
}

// FR-050, plan S9.3: contribution = amount * applyModCurve(curve, |u|) * sign(u), u = (m - 0.5) * 2 (the Vorago form,
// vorago_macro_matrix.h:1170-1173), bitwise against the same expression executed here.
TEST_CASE("ProfundumMacro_ContributionArithmetic", "[systems][profundum][voice-engine][fr-050]") {
    for (const float mv : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        ProfundumMacroMatrix m;
        m.setMacros({.weight = mv, .darkBright = mv, .tightLoose = mv, .cleanViolent = mv});
        const auto c = m.contributions();
        const ProfundumMacroValues v = m.macros();
        for (const auto& row : ProfundumMacroMatrix::kRows) {
            const float macroValue = veMacroValue(v, row.macro);
            const float u = (macroValue - 0.5f) * 2.0f;
            const float expected =
                row.amount * Krate::DSP::applyModCurve(row.curve, std::fabs(u)) * (u < 0.0f ? -1.0f : 1.0f);
            INFO("m " << std::setprecision(6) << mv << " target " << static_cast<int>(row.target) << " got "
                      << c[static_cast<std::size_t>(row.target)] << " expected " << expected);
            REQUIRE(veBits(c[static_cast<std::size_t>(row.target)]) == veBits(expected));
            if (row.target == ProfundumMacroTarget::CompressionAmount) {
                REQUIRE(veBits(m.compressionContribution()) == veBits(expected));
            }
        }
    }
}
