// ==============================================================================
// Test helpers - MorphState (light; no render stack)
//                (Profundum Phase 8, specs/profundum-phase8-bass-morph)
// ==============================================================================
// tasks.md deviation 3: what needs only MorphState / BassMorphEngine lives here, so the engine-only TUs
// (T002 - T006) can use it before the render fixture exists. T002 creates this file; T003 appends the
// `Resolved` overload of bitEqual; read-only afterwards. bass_morph_test_helpers.h (T007) includes it.
//   Serves: FR-005a (bitEqual), FR-071 (named pairs), plan S5.1 (randomValidState / randomValidPair, R-8).
// ==============================================================================

#pragma once

#include <krate/dsp/systems/morph_state.h>
#include <krate/dsp/systems/bass_morph_engine.h>   // T003: the Resolved overload of bitEqual

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <random>
#include <type_traits>
#include <utility>
#include <vector>

namespace Krate::DSP::BassMorphTest {

/// The version-1 stream of a valid state (REQUIREs the serialiser accepted it).
inline std::array<std::byte, kMorphStateBytes> serializedBytes(const MorphState& s) {
    std::array<std::byte, kMorphStateBytes> bytes{};
    const std::size_t written = serializeMorphState(s, bytes.data(), bytes.size());
    REQUIRE(written == kMorphStateBytes);
    return bytes;
}

/// FR-005a: two valid states are bitwise equal iff their serialised bytes are equal (the layout has no padding).
inline bool bitEqual(const MorphState& a, const MorphState& b) {
    REQUIRE(isValidMorphState(a));
    REQUIRE(isValidMorphState(b));
    return serializedBytes(a) == serializedBytes(b);
}

/// Indices i of the leaves that differ (floats by std::bit_cast, enums / bools by value).
inline std::vector<std::size_t> differingLeaves(const MorphState& a, const MorphState& b) {
    std::vector<std::size_t> out;
    detail::morph_state_io::forEachMorphLeaf(
        [&](auto leaf, const auto& x, const auto& y) {
            using T = std::remove_cvref_t<decltype(x)>;
            bool same = false;
            if constexpr (std::is_same_v<T, float>) {
                same = std::bit_cast<std::uint32_t>(x) == std::bit_cast<std::uint32_t>(y);
            } else {
                same = x == y;
            }
            if (!same) {
                out.push_back(decltype(leaf)::value);
            }
        },
        a, b);
    return out;
}

/// One ulp at |x|: nextafter(|x|, +inf) - |x|. The direction is FLT_MAX (no infinity under fast-math), identical
/// for every |x| < FLT_MAX.
inline float ulpOf(float x) {
    const float ax = std::fabs(x);
    return std::nextafter(ax, std::numeric_limits<float>::max()) - ax;
}

/// u = (w >> 8) * 2^-24 in [0, 1), from one raw mt19937 word (portable across standard libraries, plan R-8).
inline double unitFromWord(std::uint32_t w) {
    return static_cast<double>(w >> 8) * (1.0 / 16777216.0);
}

/// Plan S5.1: one valid random state. Per table row in order: F32 uniform on [lo, hi] (log rows log-uniform on
/// [lo + o, hi + o] - o, clamped into [lo, hi]; the hiOpen phase row on [0, 360) with 360 -> 0); Enum8
/// w % (maxEnum + 1); Bool w & 1. With probability 0.05 per F32 leaf it is set to exactly lo or hi (hiOpen: lo
/// only); then with probability 0.15 per governing amount (characterAmount, field.spread, field.motionDepth,
/// motion.amount, anchor.level) it is set to exactly 0.
inline MorphState randomValidState(std::mt19937& rng) {
    namespace io = detail::morph_state_io;
    MorphState s{};
    io::forEachMorphLeaf(
        [&]([[maybe_unused]] auto leaf, auto& v) {
            using T = std::remove_cvref_t<decltype(v)>;
            const std::uint32_t w = static_cast<std::uint32_t>(rng());
            if constexpr (std::is_same_v<T, float>) {
                constexpr MorphState::FieldInfo row = io::kFieldTable[decltype(leaf)::value];
                const double u = unitFromWord(w);
                const double lo = static_cast<double>(row.lo);
                const double hi = static_cast<double>(row.hi);
                double x = 0.0;
                if constexpr (row.law == MorphState::Law::Logarithmic) {
                    const double o = static_cast<double>(row.logOffset);
                    const double l0 = std::log2(lo + o);
                    const double l1 = std::log2(hi + o);
                    x = std::exp2(l0 + u * (l1 - l0)) - o;
                } else {
                    x = lo + u * (hi - lo);
                }
                float f = static_cast<float>(x);
                if (f < row.lo) {
                    f = row.lo;
                }
                if constexpr (row.hiOpen) {
                    if (f >= row.hi) {
                        f = 0.0f;   // 360 -> 0 (LFO wrap)
                    }
                } else {
                    if (f > row.hi) {
                        f = row.hi;
                    }
                }
                const std::uint32_t wEdge = static_cast<std::uint32_t>(rng());
                if (unitFromWord(wEdge) < 0.05) {
                    const std::uint32_t wSide = static_cast<std::uint32_t>(rng());
                    if constexpr (row.hiOpen) {
                        static_cast<void>(wSide);   // the draw is kept so the word sequence is row-independent
                        f = row.lo;
                    } else {
                        f = (wSide & 1u) == 0u ? row.lo : row.hi;
                    }
                }
                v = f;
            } else if constexpr (std::is_same_v<T, bool>) {
                v = (w & 1u) != 0u;
            } else {
                constexpr std::uint32_t count =
                    static_cast<std::uint32_t>(io::kFieldTable[decltype(leaf)::value].maxEnum) + 1u;
                v = static_cast<T>(static_cast<std::uint8_t>(w % count));
            }
        },
        s);

    float* governing[] = {&s.characterAmount, &s.field.spread, &s.field.motionDepth, &s.motion.amount,
                          &s.anchor.level};
    for (float* g : governing) {
        if (unitFromWord(static_cast<std::uint32_t>(rng())) < 0.15) {
            *g = 0.0f;
        }
    }
    REQUIRE(BassStereoField::isValidControls(s.field));   // cannot fail by construction
    return s;
}

/// Plan S5.1: two random valid states, then each leaf of B copied from A with probability pEqual (one draw per
/// leaf, in table order).
inline std::pair<MorphState, MorphState> randomValidPair(std::mt19937& rng, float pEqual = 0.1f) {
    MorphState a = randomValidState(rng);
    MorphState b = randomValidState(rng);
    detail::morph_state_io::forEachMorphLeaf(
        [&](auto, const auto& x, auto& y) {
            if (unitFromWord(static_cast<std::uint32_t>(rng())) < static_cast<double>(pEqual)) {
                y = x;
            }
        },
        std::as_const(a), b);
    return {a, b};
}

// ---- Named pairs (FR-071, plan S5.1, spec descriptor block) ----

/// The showcase pair: A a clean sine-anchor patch, B the growl. Curves stay at the C-8 defaults on both sides.
inline std::pair<MorphState, MorphState> showcasePair() {
    MorphState a{};
    a.recipe = SpectralShapeRecipe::kSineAnchor;
    a.excitation.mix = 0.0f;
    a.foldAmount = 0.0f;
    a.shape = 0.0f;
    a.characterAlgorithm = BassCharacterStage::Algorithm::Tube;
    a.characterAmount = 0.0f;
    a.characterTone = 0.5f;
    a.dropAmountSt = 0.0f;
    a.ampEnv.attackMs = 1.0f;
    a.ampEnv.decayMs = 150.0f;
    a.ampEnv.sustain = 0.0f;
    a.ampEnv.releaseMs = 50.0f;
    a.field.mode = BassStereoField::ImageMode::Solid;
    a.field.spread = 0.0f;
    a.motion.source = MorphState::MotionConfig::Source::Off;
    a.mass = 50.0f;
    a.anchor.waveform = AnchorOscillator::Waveform::Sine;
    a.anchor.mode = AnchorOscillator::Mode::Lock;
    a.anchor.level = 1.0f;
    a.anchorFoldFeed = 0.0f;

    // B: harvested 2026-10-19 (spec B7-2; evidence/harvest/run3, candidate 59): the drafted kGrowl / SHAPE 1 /
    // Amount 0.8 / level 0.7 B failed the f0 Lock floor at E1 and the Anchor clause (its Anchor sat at -47 dB).
    MorphState b{};
    b.recipe = SpectralShapeRecipe::kHeavy;
    b.recipe.edge = 1.0f;
    b.excitation.mix = 1.0f;
    b.excitation.drive = 1.0f;
    b.excitation.asymmetry = 0.5f;
    b.foldAmount = 80.0f;
    b.shape = 0.5f;
    b.characterAlgorithm = BassCharacterStage::Algorithm::Fold;
    b.characterAmount = 0.5f;
    b.characterTone = 0.8f;
    b.dropAmountSt = 0.0f;
    b.ampEnv.attackMs = 1.0f;
    b.ampEnv.decayMs = 2000.0f;
    b.ampEnv.sustain = 0.8f;
    b.ampEnv.releaseMs = 800.0f;
    b.field.mode = BassStereoField::ImageMode::Wide;
    b.field.spread = 1.0f;
    b.motion.source = MorphState::MotionConfig::Source::Off;
    b.mass = 50.0f;
    b.anchor.waveform = AnchorOscillator::Waveform::Sine;
    b.anchor.mode = AnchorOscillator::Mode::Lock;
    b.anchor.level = 0.5f;
    b.anchorFoldFeed = 0.0f;

    REQUIRE(isValidMorphState(a));
    REQUIRE(isValidMorphState(b));
    return {a, b};
}

/// SC-005(e): Mass 0 -> 100, everything else default.
inline std::pair<MorphState, MorphState> massOnlyPair() {
    MorphState a{};
    a.mass = 0.0f;
    MorphState b{};
    b.mass = 100.0f;
    REQUIRE(isValidMorphState(a));
    REQUIRE(isValidMorphState(b));
    return {a, b};
}

/// SC-005(a-d): anchor.level 1.0 -> 0.7 at Mass 50.
inline std::pair<MorphState, MorphState> anchorCurvePair() {
    MorphState a{};
    a.anchor.level = 1.0f;
    MorphState b{};
    b.anchor.level = 0.7f;
    REQUIRE(isValidMorphState(a));
    REQUIRE(isValidMorphState(b));
    return {a, b};
}

/// SC-017 grid: A Fold 0, Tube, Amount 0; B Fold 100, Fracture, Amount 1, excitation mix 1, drive 1.
inline std::pair<MorphState, MorphState> maxNonlinearPair() {
    MorphState a{};
    a.foldAmount = 0.0f;
    a.characterAlgorithm = BassCharacterStage::Algorithm::Tube;
    a.characterAmount = 0.0f;
    MorphState b{};
    b.foldAmount = 100.0f;
    b.characterAlgorithm = BassCharacterStage::Algorithm::Fracture;
    b.characterAmount = 1.0f;
    b.excitation.mix = 1.0f;
    b.excitation.drive = 1.0f;
    REQUIRE(isValidMorphState(a));
    REQUIRE(isValidMorphState(b));
    return {a, b};
}

/// SC-017 crossing arms and (b) window (plan S1 C-16): both Fold 100, mix 1, drive 1, Amount 1, Tone 0.5;
/// A Tube, B Fracture; every other leaf equal.
inline std::pair<MorphState, MorphState> algorithmCrossingPair() {
    MorphState a{};
    a.foldAmount = 100.0f;
    a.excitation.mix = 1.0f;
    a.excitation.drive = 1.0f;
    a.characterAmount = 1.0f;
    a.characterTone = 0.5f;
    MorphState b = a;
    a.characterAlgorithm = BassCharacterStage::Algorithm::Tube;
    b.characterAlgorithm = BassCharacterStage::Algorithm::Fracture;
    REQUIRE(isValidMorphState(a));
    REQUIRE(isValidMorphState(b));
    return {a, b};
}

// ---- T003 append: engine output ----

/// FR-005a for the engine's output: the state bitwise (bitEqual above) and position, anchorPosition and anchorMass
/// by std::bit_cast (+0 and -0 differ).
inline bool bitEqual(const BassMorphEngine::Resolved& a, const BassMorphEngine::Resolved& b) {
    return bitEqual(a.state, b.state)
           && std::bit_cast<std::uint32_t>(a.position) == std::bit_cast<std::uint32_t>(b.position)
           && std::bit_cast<std::uint32_t>(a.anchorPosition) == std::bit_cast<std::uint32_t>(b.anchorPosition)
           && std::bit_cast<std::uint32_t>(a.anchorMass) == std::bit_cast<std::uint32_t>(b.anchorMass);
}

}  // namespace Krate::DSP::BassMorphTest
