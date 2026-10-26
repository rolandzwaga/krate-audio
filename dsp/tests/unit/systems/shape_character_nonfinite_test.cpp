// ==============================================================================
// Layer 3: System Tests - Shape & Character, non-finite TU
//                          (Profundum Phase 5, specs/profundum-phase5-shape-character)
// ==============================================================================
// Reference: specs/profundum-phase5-shape-character/plan.md (S8, S9.1)
//            specs/profundum-phase5-shape-character/tasks.md (T002 wires this TU,
//                                                             T020 fills it)
//
// SCOPE OF THIS TU (tags [systems][profundum][shape-character][nonfinite]):
//   SC-017 (FR-066) for ShapeMorph and BassCharacterStage, the FR-066 non-finite
//   setter rule and the FR-052 non-finite f0PerSample rule. Listed in the
//   -fno-fast-math block of dsp/tests/CMakeLists.txt: NaN / ±Inf are built from
//   bit patterns and finiteness is read only through detail::isFinite.
// ==============================================================================

#include "shape_character_test_helpers.h"

#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/processors/spectral_shape_recipe.h>
#include <krate/dsp/systems/bass_character_stage.h>
#include <krate/dsp/systems/shape_morph.h>

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <vector>

using namespace Krate::DSP;

namespace {

namespace Sct = Krate::DSP::ShapeCharacterTest;
using ScnfAlgo = BassCharacterStage::Algorithm;

constexpr double kScnfFs = 48000.0;
constexpr std::uint32_t kScnfQuietNanBits = 0x7FC00000u;
constexpr std::uint32_t kScnfPosInfBits = 0x7F800000u;
constexpr std::uint32_t kScnfNegInfBits = 0xFF800000u;

float scnfFromBits(std::uint32_t bits) noexcept { return std::bit_cast<float>(bits); }

/// NaN, +Inf, -Inf (built from bit patterns).
std::array<float, 3> scnfBadValues() {
    return {scnfFromBits(kScnfQuietNanBits), scnfFromBits(kScnfPosInfBits), scnfFromBits(kScnfNegInfBits)};
}

constexpr std::array<ScnfAlgo, 6> kScnfAlgorithms{ScnfAlgo::Tube,  ScnfAlgo::Fold,  ScnfAlgo::Rectify,
                                                  ScnfAlgo::Bite,  ScnfAlgo::Crush, ScnfAlgo::Fracture};
constexpr std::array<float, 3> kScnfShapes{0.0f, 0.5f, 1.0f};

const char* scnfAlgoName(ScnfAlgo a) {
    switch (a) {
        case ScnfAlgo::Tube:
            return "Tube";
        case ScnfAlgo::Fold:
            return "Fold";
        case ScnfAlgo::Rectify:
            return "Rectify";
        case ScnfAlgo::Bite:
            return "Bite";
        case ScnfAlgo::Crush:
            return "Crush";
        case ScnfAlgo::Fracture:
            return "Fracture";
    }
    return "?";
}

/// A fresh BassCharacterStage configured before prepare (prepare's reset() leaves it steady:
/// no crossfade, smoothers snapped, f0 keyed). Amount 1, Tone 0.5.
void scnfConfigureStage(BassCharacterStage& s, ScnfAlgo a, float f0, float amountOffset = 0.0f,
                        float toneOffset = 0.0f) {
    s.setAlgorithm(a);
    s.setAmount(1.0f);
    s.setTone(0.5f);
    s.setChannelOffsets(amountOffset, toneOffset);
    s.setFundamentalHz(f0);
    s.prepare(kScnfFs, 512);
}

/// A fresh ShapeMorph configured before prepare.
void scnfConfigureMorph(ShapeMorph& m, float shape, float f0) {
    m.setShape(shape);
    m.setFundamentalHz(f0);
    m.prepare(kScnfFs, 512);
}

/// Stereo in-place render in 512-sample blocks; a block also ends at every index in `splits`
/// (sorted). `onBlock(pos)` runs before each block. `f0` (nullable) is the whole-render
/// f0PerSample array.
template <class Stage, class OnBlock>
void scnfRender(Stage& s, std::vector<float>& L, std::vector<float>& R, const std::vector<std::size_t>& splits,
                const float* f0, const OnBlock& onBlock) {
    REQUIRE(L.size() == R.size());
    constexpr std::size_t kBlock = 512;
    const std::size_t total = L.size();
    std::size_t pos = 0;
    std::size_t si = 0;
    while (pos < total) {
        while (si < splits.size() && splits[si] <= pos)
            ++si;
        std::size_t n = std::min(kBlock, total - pos);
        if (si < splits.size())
            n = std::min(n, splits[si] - pos);
        onBlock(pos);
        s.processBlock(L.data() + pos, R.data() + pos, n, f0 != nullptr ? f0 + pos : nullptr);
        pos += n;
    }
}

std::size_t scnfNonFiniteCount(const std::vector<float>& L, const std::vector<float>& R) {
    std::size_t bad = 0;
    for (std::size_t k = 0; k < L.size(); ++k) {
        if (!detail::isFinite(L[k]) || !detail::isFinite(R[k]))
            ++bad;
    }
    return bad;
}

bool scnfBitIdentical(const std::vector<float>& a, const std::vector<float>& b) {
    return a.size() == b.size() && std::memcmp(a.data(), b.data(), a.size() * sizeof(float)) == 0;
}

/// First index where a and b differ bit-wise (the common length when identical).
std::size_t scnfFirstDiff(const std::vector<float>& a, const std::vector<float>& b) {
    const std::size_t n = std::min(a.size(), b.size());
    for (std::size_t k = 0; k < n; ++k) {
        if (std::bit_cast<std::uint32_t>(a[k]) != std::bit_cast<std::uint32_t>(b[k]))
            return k;
    }
    return n;
}

// -----------------------------------------------------------------------------
// SC-017 recovery cell
// -----------------------------------------------------------------------------

constexpr std::size_t kScnfBurstStart = 48000;  // 1.0 s at 48 kHz
constexpr std::size_t kScnfBurstLen = 16;
constexpr std::size_t kScnfBurstEnd = kScnfBurstStart + kScnfBurstLen;
constexpr std::size_t kScnfRecoverySamples = 512;
constexpr std::size_t kScnfFrame = TestUtils::kLowFrequencyFftSize;
constexpr std::size_t kScnfFrameStart = kScnfBurstEnd + 4800;  // 100 ms after the burst end

/// Renders `in` through a fresh stage (set up by `configure`) with and without the burst and
/// checks SC-017: every output finite, stateFinite() 512 samples after the burst end, and the
/// descriptors of the frame starting 100 ms after the burst end within 0.1 dB (L, E_low, E_high)
/// and 0.01 oct (C) of the same frame of the burst-free render.
template <class Stage, class Configure>
void scnfRecoveryCell(const char* label, const std::vector<float>& in, const Configure& configure) {
    INFO("cell: " << label);
    const double f0 = Sct::kE1Hz;

    Stage ref;
    configure(ref);
    std::vector<float> refL = in;
    std::vector<float> refR = in;
    scnfRender(ref, refL, refR, {}, nullptr, [](std::size_t) {});
    REQUIRE(scnfNonFiniteCount(refL, refR) == 0u);

    std::vector<float> L = in;
    std::vector<float> R = in;
    const std::array<float, 3> bad = scnfBadValues();
    for (std::size_t k = 0; k < kScnfBurstLen; ++k) {
        L[kScnfBurstStart + k] = bad[k % 3];
        R[kScnfBurstStart + k] = bad[(k + 1) % 3];
    }
    Stage s;
    configure(s);
    bool checked = false;
    bool finiteAfterBurst = false;
    scnfRender(s, L, R, {kScnfBurstStart, kScnfBurstEnd, kScnfBurstEnd + kScnfRecoverySamples}, nullptr,
               [&](std::size_t pos) {
                   if (pos == kScnfBurstEnd + kScnfRecoverySamples) {
                       finiteAfterBurst = s.stateFinite();
                       checked = true;
                   }
               });
    REQUIRE(checked);
    CHECK(finiteAfterBurst);
    CHECK(s.stateFinite());

    const std::size_t nonFinite = scnfNonFiniteCount(L, R);
    INFO("non-finite output samples: " << nonFinite);
    CHECK(nonFinite == 0u);
    if (nonFinite != 0u)
        return;  // descriptors of a non-finite frame are meaningless

    const Sct::Descriptors dBurst = Sct::describe(L.data() + kScnfFrameStart, kScnfFrame, kScnfFs, f0);
    const Sct::Descriptors dRef = Sct::describe(refL.data() + kScnfFrameStart, kScnfFrame, kScnfFs, f0);
    INFO("burst: L " << dBurst.L << " dB, Elow " << dBurst.Elow << " dB, Ehigh " << dBurst.Ehigh << " dB, C "
                     << dBurst.C << " oct; burst-free: L " << dRef.L << ", Elow " << dRef.Elow << ", Ehigh "
                     << dRef.Ehigh << ", C " << dRef.C);
    CHECK(std::abs(dBurst.L - dRef.L) <= 0.1);
    CHECK(std::abs(dBurst.Elow - dRef.Elow) <= 0.1);
    CHECK(std::abs(dBurst.Ehigh - dRef.Ehigh) <= 0.1);
    CHECK(std::abs(dBurst.C - dRef.C) <= 0.01);
}

const char* scnfShapeName(float shape) {
    if (shape == 0.0f)
        return "SHAPE 0 (ROUND)";
    if (shape == 0.5f)
        return "SHAPE 0.5 (HOLLOW)";
    return "SHAPE 1 (BITE)";
}

}  // namespace

// =============================================================================
// SC-017 (FR-066): a 16-sample NaN / +Inf / -Inf input burst is absorbed and the state recovers
// =============================================================================

TEST_CASE("ShapeCharacter_NonFiniteRecovery", "[systems][profundum][shape-character][nonfinite]") {
    const double f0 = Sct::kE1Hz;
    const auto f0f = static_cast<float>(f0);
    const double seconds = static_cast<double>(kScnfFrameStart + kScnfFrame) / kScnfFs + 0.01;
    const std::vector<float> in = Sct::fixture(SpectralShapeRecipe::kSawAnchor, f0, kScnfFs, seconds);
    REQUIRE(in.size() >= kScnfFrameStart + kScnfFrame);

    for (const ScnfAlgo a : kScnfAlgorithms) {
        scnfRecoveryCell<BassCharacterStage>(scnfAlgoName(a), in,
                                             [&](BassCharacterStage& s) { scnfConfigureStage(s, a, f0f); });
    }
    for (const float shape : kScnfShapes) {
        scnfRecoveryCell<ShapeMorph>(scnfShapeName(shape), in,
                                     [&](ShapeMorph& m) { scnfConfigureMorph(m, shape, f0f); });
    }
}

// =============================================================================
// FR-066: NaN / +Inf / -Inf passed to every setter mid-render leave the output bit-identical
// =============================================================================

TEST_CASE("ShapeCharacter_NonFiniteSetters", "[systems][profundum][shape-character][nonfinite]") {
    const double f0 = Sct::kE1Hz;
    const auto f0f = static_cast<float>(f0);
    const std::vector<float> in = Sct::fixture(SpectralShapeRecipe::kSawAnchor, f0, kScnfFs, 1.0);
    const std::array<float, 3> bad = scnfBadValues();

    // Event k (0-based) lands on the block boundary 4000 + 1500·k (both renders split there).
    const auto eventAt = [](std::size_t k) { return std::size_t{4000} + std::size_t{1500} * k; };

    SECTION("BassCharacterStage") {
        // Zero offsets (linked) and nonzero offsets (unlinked from prepare).
        constexpr std::array<std::array<float, 2>, 2> kOffsets{{{0.0f, 0.0f}, {0.1f, 0.05f}}};
        constexpr std::size_t kEvents = 15;  // 5 setter arms × 3 bad values
        std::vector<std::size_t> splits;
        for (std::size_t k = 0; k < kEvents; ++k)
            splits.push_back(eventAt(k));
        REQUIRE(splits.back() < in.size());

        for (const auto& off : kOffsets) {
            for (const ScnfAlgo a : kScnfAlgorithms) {
                INFO("algorithm " << scnfAlgoName(a) << ", offsets " << off[0] << " / " << off[1]);
                BassCharacterStage ref;
                scnfConfigureStage(ref, a, f0f, off[0], off[1]);
                std::vector<float> refL = in;
                std::vector<float> refR = in;
                scnfRender(ref, refL, refR, splits, nullptr, [](std::size_t) {});

                BassCharacterStage s;
                scnfConfigureStage(s, a, f0f, off[0], off[1]);
                std::vector<float> L = in;
                std::vector<float> R = in;
                std::size_t fired = 0;
                scnfRender(s, L, R, splits, nullptr, [&](std::size_t pos) {
                    for (std::size_t k = 0; k < kEvents; ++k) {
                        if (pos != eventAt(k))
                            continue;
                        const float v = bad[k % 3];
                        switch (k / 3) {
                            case 0:
                                s.setAmount(v);
                                break;
                            case 1:
                                s.setTone(v);
                                break;
                            case 2:
                                s.setChannelOffsets(v, off[1]);  // the amount-offset argument
                                break;
                            case 3:
                                s.setChannelOffsets(off[0], v);  // the tone-offset argument
                                break;
                            default:
                                s.setFundamentalHz(v);
                                break;
                        }
                        ++fired;
                    }
                });
                REQUIRE(fired == kEvents);
                CHECK(s.amount() == 1.0f);
                CHECK(s.tone() == 0.5f);
                CHECK(s.fundamentalHz() == f0f);
                CHECK(scnfNonFiniteCount(L, R) == 0u);
                INFO("first differing sample L " << scnfFirstDiff(L, refL) << ", R " << scnfFirstDiff(R, refR)
                                                 << " of " << L.size());
                CHECK(scnfBitIdentical(L, refL));
                CHECK(scnfBitIdentical(R, refR));
            }
        }
    }

    SECTION("ShapeMorph") {
        constexpr std::size_t kEvents = 6;  // 2 setter arms × 3 bad values
        std::vector<std::size_t> splits;
        for (std::size_t k = 0; k < kEvents; ++k)
            splits.push_back(eventAt(k));
        REQUIRE(splits.back() < in.size());

        for (const float shape : kScnfShapes) {
            INFO(scnfShapeName(shape));
            ShapeMorph ref;
            scnfConfigureMorph(ref, shape, f0f);
            std::vector<float> refL = in;
            std::vector<float> refR = in;
            scnfRender(ref, refL, refR, splits, nullptr, [](std::size_t) {});

            ShapeMorph m;
            scnfConfigureMorph(m, shape, f0f);
            std::vector<float> L = in;
            std::vector<float> R = in;
            std::size_t fired = 0;
            scnfRender(m, L, R, splits, nullptr, [&](std::size_t pos) {
                for (std::size_t k = 0; k < kEvents; ++k) {
                    if (pos != eventAt(k))
                        continue;
                    const float v = bad[k % 3];
                    if (k / 3 == 0)
                        m.setShape(v);
                    else
                        m.setFundamentalHz(v);
                    ++fired;
                }
            });
            REQUIRE(fired == kEvents);
            CHECK(scnfNonFiniteCount(L, R) == 0u);
            INFO("first differing sample L " << scnfFirstDiff(L, refL) << ", R " << scnfFirstDiff(R, refR) << " of "
                                             << L.size());
            CHECK(scnfBitIdentical(L, refL));
            CHECK(scnfBitIdentical(R, refR));
        }
    }
}

// =============================================================================
// FR-052: a NaN / +Inf entry in f0PerSample equals the previous f0 held at that entry
// =============================================================================

TEST_CASE("ShapeCharacter_NonFiniteF0Feed", "[systems][profundum][shape-character][nonfinite]") {
    constexpr std::size_t kNanIndex = 3000;
    constexpr std::size_t kInfIndex = 7000;
    const double f0 = Sct::kE1Hz;
    const std::vector<float> in = Sct::fixture(SpectralShapeRecipe::kSawAnchor, f0, kScnfFs, 0.25);
    REQUIRE(in.size() > kInfIndex + 1000);

    // A gliding feed (E2 down to E1 over the render): every entry differs from its neighbours, so
    // a substitution other than "the previous f0" shows in the output.
    std::vector<float> feedClean(in.size(), 0.0f);
    for (std::size_t n = 0; n < feedClean.size(); ++n) {
        const double t = static_cast<double>(n) / static_cast<double>(feedClean.size());
        feedClean[n] = static_cast<float>(Sct::kE2Hz * std::exp2(-t));
    }
    REQUIRE(feedClean[kNanIndex] != feedClean[kNanIndex - 1]);
    REQUIRE(feedClean[kInfIndex] != feedClean[kInfIndex - 1]);
    // Reference: the previous f0 held at the bad entries.
    std::vector<float> feedHeld = feedClean;
    feedHeld[kNanIndex] = feedHeld[kNanIndex - 1];
    feedHeld[kInfIndex] = feedHeld[kInfIndex - 1];
    std::vector<float> feedBad = feedClean;
    feedBad[kNanIndex] = scnfFromBits(kScnfQuietNanBits);
    feedBad[kInfIndex] = scnfFromBits(kScnfPosInfBits);

    const auto f0Start = static_cast<float>(Sct::kE2Hz);

    SECTION("BassCharacterStage") {
        for (const ScnfAlgo a : kScnfAlgorithms) {
            INFO("algorithm " << scnfAlgoName(a));
            BassCharacterStage ref;
            scnfConfigureStage(ref, a, f0Start);
            std::vector<float> refL = in;
            std::vector<float> refR = in;
            scnfRender(ref, refL, refR, {}, feedHeld.data(), [](std::size_t) {});

            BassCharacterStage s;
            scnfConfigureStage(s, a, f0Start);
            std::vector<float> L = in;
            std::vector<float> R = in;
            scnfRender(s, L, R, {}, feedBad.data(), [](std::size_t) {});

            CHECK(scnfNonFiniteCount(L, R) == 0u);
            INFO("first differing sample L " << scnfFirstDiff(L, refL) << " of " << L.size());
            CHECK(scnfBitIdentical(L, refL));
            CHECK(scnfBitIdentical(R, refR));
        }
    }

    SECTION("ShapeMorph") {
        for (const float shape : kScnfShapes) {
            INFO(scnfShapeName(shape));
            ShapeMorph ref;
            scnfConfigureMorph(ref, shape, f0Start);
            std::vector<float> refL = in;
            std::vector<float> refR = in;
            scnfRender(ref, refL, refR, {}, feedHeld.data(), [](std::size_t) {});

            ShapeMorph m;
            scnfConfigureMorph(m, shape, f0Start);
            std::vector<float> L = in;
            std::vector<float> R = in;
            scnfRender(m, L, R, {}, feedBad.data(), [](std::size_t) {});

            CHECK(scnfNonFiniteCount(L, R) == 0u);
            INFO("first differing sample L " << scnfFirstDiff(L, refL) << " of " << L.size());
            CHECK(scnfBitIdentical(L, refL));
            CHECK(scnfBitIdentical(R, refR));
        }
    }
}
