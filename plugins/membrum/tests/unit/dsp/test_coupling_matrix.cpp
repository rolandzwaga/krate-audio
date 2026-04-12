// ==============================================================================
// CouplingMatrix Tests -- Tier 1/Tier 2 resolver logic (Phase 5, T014)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "dsp/coupling_matrix.h"
#include "dsp/pad_category.h"
#include "dsp/pad_config.h"

using namespace Membrum;
using Catch::Approx;

namespace {

// Helper: create a categories array with specific assignments
void fillCategories(PadCategory* cats, int size, PadCategory defaultCat)
{
    for (int i = 0; i < size; ++i)
        cats[i] = defaultCat;
}

} // namespace

// ==============================================================================
// Tier 1: recomputeFromTier1
// ==============================================================================

TEST_CASE("CouplingMatrix: Kick->Snare gain = snareBuzz * 0.05",
          "[coupling_matrix]")
{
    CouplingMatrix matrix;
    PadCategory cats[CouplingMatrix::kSize];
    fillCategories(cats, CouplingMatrix::kSize, PadCategory::Perc);

    // Pad 0 = Kick, Pad 1 = Snare
    cats[0] = PadCategory::Kick;
    cats[1] = PadCategory::Snare;

    float snareBuzz = 0.8f;
    float tomResonance = 0.0f;
    matrix.recomputeFromTier1(snareBuzz, tomResonance, cats);

    REQUIRE(matrix.getEffectiveGain(0, 1) ==
            Approx(snareBuzz * CouplingMatrix::kMaxCoefficient));
    // Reverse direction (Snare->Kick) should be 0 since it's not a Kick->Snare pair
    REQUIRE(matrix.getEffectiveGain(1, 0) == 0.0f);
}

TEST_CASE("CouplingMatrix: Tom->Tom gain = tomResonance * 0.05",
          "[coupling_matrix]")
{
    CouplingMatrix matrix;
    PadCategory cats[CouplingMatrix::kSize];
    fillCategories(cats, CouplingMatrix::kSize, PadCategory::Perc);

    // Pads 2 and 3 = Tom
    cats[2] = PadCategory::Tom;
    cats[3] = PadCategory::Tom;

    float snareBuzz = 0.0f;
    float tomResonance = 0.6f;
    matrix.recomputeFromTier1(snareBuzz, tomResonance, cats);

    REQUIRE(matrix.getEffectiveGain(2, 3) ==
            Approx(tomResonance * CouplingMatrix::kMaxCoefficient));
    // Symmetric: Tom->Tom works both ways
    REQUIRE(matrix.getEffectiveGain(3, 2) ==
            Approx(tomResonance * CouplingMatrix::kMaxCoefficient));
}

TEST_CASE("CouplingMatrix: all other pairs have gain 0.0", "[coupling_matrix]")
{
    CouplingMatrix matrix;
    PadCategory cats[CouplingMatrix::kSize];
    fillCategories(cats, CouplingMatrix::kSize, PadCategory::Perc);

    cats[0] = PadCategory::Kick;
    cats[1] = PadCategory::Snare;
    cats[2] = PadCategory::Tom;
    cats[3] = PadCategory::HatCymbal;

    matrix.recomputeFromTier1(1.0f, 1.0f, cats);

    // Kick->HatCymbal = 0
    REQUIRE(matrix.getEffectiveGain(0, 3) == 0.0f);
    // HatCymbal->Snare = 0
    REQUIRE(matrix.getEffectiveGain(3, 1) == 0.0f);
    // Perc->anything = 0
    REQUIRE(matrix.getEffectiveGain(4, 0) == 0.0f);
    // Kick->Kick = 0 (no self-coupling for Kick)
    REQUIRE(matrix.getEffectiveGain(0, 0) == 0.0f);
}

TEST_CASE("CouplingMatrix: self-pairs always 0", "[coupling_matrix]")
{
    CouplingMatrix matrix;
    PadCategory cats[CouplingMatrix::kSize];
    fillCategories(cats, CouplingMatrix::kSize, PadCategory::Tom);

    matrix.recomputeFromTier1(1.0f, 1.0f, cats);

    for (int i = 0; i < CouplingMatrix::kSize; ++i)
        REQUIRE(matrix.getEffectiveGain(i, i) == 0.0f);
}

// ==============================================================================
// Tier 2: Per-pair overrides
// ==============================================================================

TEST_CASE("CouplingMatrix: setOverride replaces computed gain",
          "[coupling_matrix]")
{
    CouplingMatrix matrix;
    PadCategory cats[CouplingMatrix::kSize];
    fillCategories(cats, CouplingMatrix::kSize, PadCategory::Perc);

    cats[0] = PadCategory::Kick;
    cats[1] = PadCategory::Snare;
    matrix.recomputeFromTier1(1.0f, 0.0f, cats);

    float computedGain = matrix.getEffectiveGain(0, 1);
    REQUIRE(computedGain > 0.0f);

    // Override with a different value
    float overrideValue = 0.02f;
    matrix.setOverride(0, 1, overrideValue);
    REQUIRE(matrix.getEffectiveGain(0, 1) == Approx(overrideValue));
    REQUIRE(matrix.hasOverrideAt(0, 1) == true);
}

TEST_CASE("CouplingMatrix: clearOverride reverts to computed",
          "[coupling_matrix]")
{
    CouplingMatrix matrix;
    PadCategory cats[CouplingMatrix::kSize];
    fillCategories(cats, CouplingMatrix::kSize, PadCategory::Perc);

    cats[0] = PadCategory::Kick;
    cats[1] = PadCategory::Snare;
    matrix.recomputeFromTier1(0.5f, 0.0f, cats);

    float expected = 0.5f * CouplingMatrix::kMaxCoefficient;
    REQUIRE(matrix.getEffectiveGain(0, 1) == Approx(expected));

    matrix.setOverride(0, 1, 0.01f);
    REQUIRE(matrix.getEffectiveGain(0, 1) == Approx(0.01f));

    matrix.clearOverride(0, 1);
    REQUIRE(matrix.hasOverrideAt(0, 1) == false);
    REQUIRE(matrix.getEffectiveGain(0, 1) == Approx(expected));
}

TEST_CASE("CouplingMatrix: setOverride clamps to kMaxCoefficient",
          "[coupling_matrix]")
{
    CouplingMatrix matrix;
    matrix.setOverride(0, 1, 1.0f); // way above max
    REQUIRE(matrix.getEffectiveGain(0, 1) ==
            Approx(CouplingMatrix::kMaxCoefficient));
}

TEST_CASE("CouplingMatrix: setOverride clamps negative to 0",
          "[coupling_matrix]")
{
    CouplingMatrix matrix;
    matrix.setOverride(0, 1, -0.5f);
    REQUIRE(matrix.getEffectiveGain(0, 1) == 0.0f);
}

// ==============================================================================
// Serialization helpers
// ==============================================================================

TEST_CASE("CouplingMatrix: getOverrideCount correct", "[coupling_matrix]")
{
    CouplingMatrix matrix;
    REQUIRE(matrix.getOverrideCount() == 0);

    matrix.setOverride(0, 1, 0.03f);
    REQUIRE(matrix.getOverrideCount() == 1);

    matrix.setOverride(2, 3, 0.01f);
    REQUIRE(matrix.getOverrideCount() == 2);

    matrix.clearOverride(0, 1);
    REQUIRE(matrix.getOverrideCount() == 1);
}

TEST_CASE("CouplingMatrix: forEachOverride iterates correctly",
          "[coupling_matrix]")
{
    CouplingMatrix matrix;
    matrix.setOverride(1, 2, 0.04f);
    matrix.setOverride(5, 10, 0.02f);

    int count = 0;
    matrix.forEachOverride([&](int src, int dst, float coeff) {
        ++count;
        if (src == 1 && dst == 2) {
            REQUIRE(coeff == Approx(0.04f));
        } else if (src == 5 && dst == 10) {
            REQUIRE(coeff == Approx(0.02f));
        } else {
            FAIL("Unexpected override pair");
        }
    });
    REQUIRE(count == 2);
}

TEST_CASE("CouplingMatrix: clearAll zeros everything", "[coupling_matrix]")
{
    CouplingMatrix matrix;
    PadCategory cats[CouplingMatrix::kSize];
    fillCategories(cats, CouplingMatrix::kSize, PadCategory::Tom);

    matrix.recomputeFromTier1(0.0f, 1.0f, cats);
    matrix.setOverride(0, 1, 0.03f);

    // Verify something is non-zero before clearing
    REQUIRE(matrix.getEffectiveGain(0, 1) > 0.0f);

    matrix.clearAll();

    for (int s = 0; s < CouplingMatrix::kSize; ++s)
        for (int d = 0; d < CouplingMatrix::kSize; ++d)
            REQUIRE(matrix.getEffectiveGain(s, d) == 0.0f);

    REQUIRE(matrix.getOverrideCount() == 0);
}

TEST_CASE("CouplingMatrix: out-of-range setOverride is no-op",
          "[coupling_matrix]")
{
    CouplingMatrix matrix;
    matrix.setOverride(-1, 0, 0.01f);
    matrix.setOverride(0, 32, 0.01f);
    matrix.setOverride(32, 0, 0.01f);
    REQUIRE(matrix.getOverrideCount() == 0);
}

TEST_CASE("CouplingMatrix: out-of-range getEffectiveGain returns 0",
          "[coupling_matrix]")
{
    CouplingMatrix matrix;
    REQUIRE(matrix.getEffectiveGain(-1, 0) == 0.0f);
    REQUIRE(matrix.getEffectiveGain(0, 32) == 0.0f);
}

TEST_CASE("CouplingMatrix: kMaxCoefficient is 0.05", "[coupling_matrix]")
{
    REQUIRE(CouplingMatrix::kMaxCoefficient == Approx(0.05f));
}
