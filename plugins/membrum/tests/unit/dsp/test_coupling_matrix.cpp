// ==============================================================================
// CouplingMatrix Tests -- single-tier resolver
// ==============================================================================
// The Tier 2 per-pair override layer + Solo were removed when the 32x32 Matrix
// UI was retired. The matrix now resolves only the global-knobs/categories/
// per-pad-amount product into effectiveGain[].
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "dsp/coupling_matrix.h"
#include "dsp/pad_category.h"
#include "dsp/pad_config.h"

using namespace Membrum;
using Catch::Approx;

namespace {

void fillCategories(PadCategory* cats, int size, PadCategory defaultCat)
{
    for (int i = 0; i < size; ++i)
        cats[i] = defaultCat;
}

} // namespace

TEST_CASE("CouplingMatrix: Kick->Snare gain = snareBuzz * kMaxCoefficient",
          "[coupling_matrix]")
{
    CouplingMatrix matrix;
    PadCategory cats[CouplingMatrix::kSize];
    fillCategories(cats, CouplingMatrix::kSize, PadCategory::Perc);

    cats[0] = PadCategory::Kick;
    cats[1] = PadCategory::Snare;

    const float snareBuzz = 0.8f;
    matrix.recomputeFromTier1(snareBuzz, 0.0f, cats);

    REQUIRE(matrix.getEffectiveGain(0, 1) ==
            Approx(snareBuzz * CouplingMatrix::kMaxCoefficient));
    // Reverse direction not in the kick->snare path.
    REQUIRE(matrix.getEffectiveGain(1, 0) == 0.0f);
}

TEST_CASE("CouplingMatrix: Tom->Tom gain = tomResonance * kMaxCoefficient",
          "[coupling_matrix]")
{
    CouplingMatrix matrix;
    PadCategory cats[CouplingMatrix::kSize];
    fillCategories(cats, CouplingMatrix::kSize, PadCategory::Perc);

    cats[2] = PadCategory::Tom;
    cats[3] = PadCategory::Tom;

    const float tomResonance = 0.6f;
    matrix.recomputeFromTier1(0.0f, tomResonance, cats);

    REQUIRE(matrix.getEffectiveGain(2, 3) ==
            Approx(tomResonance * CouplingMatrix::kMaxCoefficient));
    REQUIRE(matrix.getEffectiveGain(3, 2) ==
            Approx(tomResonance * CouplingMatrix::kMaxCoefficient));
}

TEST_CASE("CouplingMatrix: pairs outside kick->snare and tom->tom paths are 0",
          "[coupling_matrix]")
{
    CouplingMatrix matrix;
    PadCategory cats[CouplingMatrix::kSize];
    fillCategories(cats, CouplingMatrix::kSize, PadCategory::Perc);

    cats[0] = PadCategory::Kick;
    cats[1] = PadCategory::Snare;
    cats[2] = PadCategory::Tom;
    cats[3] = PadCategory::HatCymbal;

    matrix.recomputeFromTier1(1.0f, 1.0f, cats);

    REQUIRE(matrix.getEffectiveGain(0, 3) == 0.0f);  // Kick -> HatCymbal
    REQUIRE(matrix.getEffectiveGain(3, 1) == 0.0f);  // HatCymbal -> Snare
    REQUIRE(matrix.getEffectiveGain(4, 0) == 0.0f);  // Perc -> anything
    REQUIRE(matrix.getEffectiveGain(0, 0) == 0.0f);  // Self
}

TEST_CASE("CouplingMatrix: self-pairs always 0",
          "[coupling_matrix]")
{
    CouplingMatrix matrix;
    PadCategory cats[CouplingMatrix::kSize];
    fillCategories(cats, CouplingMatrix::kSize, PadCategory::Tom);

    matrix.recomputeFromTier1(1.0f, 1.0f, cats);

    for (int i = 0; i < CouplingMatrix::kSize; ++i)
        REQUIRE(matrix.getEffectiveGain(i, i) == 0.0f);
}

TEST_CASE("CouplingMatrix: out-of-range getEffectiveGain returns 0",
          "[coupling_matrix]")
{
    CouplingMatrix matrix;
    REQUIRE(matrix.getEffectiveGain(-1, 0) == 0.0f);
    REQUIRE(matrix.getEffectiveGain(0, 32) == 0.0f);
}

TEST_CASE("CouplingMatrix: clearAll zeros every cell",
          "[coupling_matrix]")
{
    CouplingMatrix matrix;
    PadCategory cats[CouplingMatrix::kSize];
    fillCategories(cats, CouplingMatrix::kSize, PadCategory::Tom);

    matrix.recomputeFromTier1(0.0f, 1.0f, cats);
    REQUIRE(matrix.getEffectiveGain(0, 1) > 0.0f);

    matrix.clearAll();
    for (int s = 0; s < CouplingMatrix::kSize; ++s)
        for (int d = 0; d < CouplingMatrix::kSize; ++d)
            REQUIRE(matrix.getEffectiveGain(s, d) == 0.0f);
}

TEST_CASE("CouplingMatrix: kMaxCoefficient is 0.05",
          "[coupling_matrix]")
{
    REQUIRE(CouplingMatrix::kMaxCoefficient == Approx(0.05f));
}

TEST_CASE("CouplingMatrix: Tom Resonance = 0.5 produces Tom->Tom gain = 0.025",
          "[coupling_matrix]")
{
    CouplingMatrix matrix;
    PadCategory cats[CouplingMatrix::kSize];
    fillCategories(cats, CouplingMatrix::kSize, PadCategory::Perc);

    cats[5]  = PadCategory::Tom;
    cats[9]  = PadCategory::Tom;
    cats[14] = PadCategory::Tom;

    matrix.recomputeFromTier1(0.0f, 0.5f, cats);

    const float expected = 0.5f * CouplingMatrix::kMaxCoefficient;
    REQUIRE(matrix.getEffectiveGain(5, 9)   == Approx(expected));
    REQUIRE(matrix.getEffectiveGain(9, 5)   == Approx(expected));
    REQUIRE(matrix.getEffectiveGain(5, 14)  == Approx(expected));
    REQUIRE(matrix.getEffectiveGain(14, 5)  == Approx(expected));
    REQUIRE(matrix.getEffectiveGain(9, 14)  == Approx(expected));
    REQUIRE(matrix.getEffectiveGain(14, 9)  == Approx(expected));
    REQUIRE(matrix.getEffectiveGain(5, 5)   == 0.0f);
    REQUIRE(matrix.getEffectiveGain(9, 9)   == 0.0f);
    REQUIRE(matrix.getEffectiveGain(14, 14) == 0.0f);
}

TEST_CASE("CouplingMatrix: Tom Resonance = 0 yields zero Tom->Tom gain",
          "[coupling_matrix]")
{
    CouplingMatrix matrix;
    PadCategory cats[CouplingMatrix::kSize];
    fillCategories(cats, CouplingMatrix::kSize, PadCategory::Perc);

    cats[5]  = PadCategory::Tom;
    cats[7]  = PadCategory::Tom;
    cats[9]  = PadCategory::Tom;
    cats[11] = PadCategory::Tom;
    cats[12] = PadCategory::Tom;
    cats[14] = PadCategory::Tom;

    matrix.recomputeFromTier1(1.0f, 0.0f, cats);

    constexpr int toms[] = {5, 7, 9, 11, 12, 14};
    for (int src : toms)
        for (int dst : toms)
            REQUIRE(matrix.getEffectiveGain(src, dst) == 0.0f);
}

TEST_CASE("CouplingMatrix: per-pad amounts scale gain by amounts[src] * amounts[dst]",
          "[coupling_matrix]")
{
    CouplingMatrix matrix;
    PadCategory cats[CouplingMatrix::kSize];
    fillCategories(cats, CouplingMatrix::kSize, PadCategory::Perc);
    cats[2] = PadCategory::Tom;
    cats[3] = PadCategory::Tom;

    float amounts[CouplingMatrix::kSize] = {};
    for (int i = 0; i < CouplingMatrix::kSize; ++i) amounts[i] = 1.0f;
    amounts[3] = 0.0f;  // pad 3 fully out

    matrix.recomputeFromTier1(0.0f, 1.0f, cats, amounts);

    // amounts[3] = 0 zeroes both inbound and outbound coupling for pad 3.
    REQUIRE(matrix.getEffectiveGain(2, 3) == 0.0f);
    REQUIRE(matrix.getEffectiveGain(3, 2) == 0.0f);
}
