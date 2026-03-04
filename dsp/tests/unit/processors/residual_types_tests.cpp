// ==============================================================================
// Unit tests for ResidualFrame, kResidualBands, and band frequency helpers
// ==============================================================================
// Layer: 2 (Processors)
// Spec: specs/116-residual-noise-model/spec.md
// Covers: FR-004 (16-band spectral envelope), FR-005 (log-spaced bands),
//         FR-008 (ResidualFrame struct)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <krate/dsp/processors/residual_types.h>

using namespace Krate::DSP;

// ============================================================================
// ResidualFrame default construction
// ============================================================================

TEST_CASE("ResidualFrame default-constructed has all zeros and transientFlag=false",
          "[processors][residual_types]")
{
    ResidualFrame frame;

    // All band energies should be zero
    for (size_t i = 0; i < kResidualBands; ++i)
    {
        REQUIRE(frame.bandEnergies[i] == 0.0f);
    }

    REQUIRE(frame.totalEnergy == 0.0f);
    REQUIRE(frame.transientFlag == false);
}

// ============================================================================
// kResidualBands constant
// ============================================================================

TEST_CASE("ResidualBands constant is 16", "[processors][residual_types]")
{
    REQUIRE(kResidualBands == 16);
}

// ============================================================================
// Band centers
// ============================================================================

TEST_CASE("ResidualBand centers array has 16 entries in ascending order",
          "[processors][residual_types]")
{
    const auto& centers = getResidualBandCenters();

    REQUIRE(centers.size() == 16);

    // All entries must be in strictly ascending order
    for (size_t i = 1; i < centers.size(); ++i)
    {
        REQUIRE(centers[i] > centers[i - 1]);
    }
}

TEST_CASE("ResidualBand centers are all positive and less than 1.0",
          "[processors][residual_types]")
{
    const auto& centers = getResidualBandCenters();

    for (size_t i = 0; i < centers.size(); ++i)
    {
        REQUIRE(centers[i] > 0.0f);
        REQUIRE(centers[i] < 1.0f);
    }
}

// ============================================================================
// Band edges
// ============================================================================

TEST_CASE("ResidualBand edges array has 17 entries starting at 0.0 and ending at 1.0",
          "[processors][residual_types]")
{
    const auto& edges = getResidualBandEdges();

    REQUIRE(edges.size() == 17);
    REQUIRE(edges[0] == 0.0f);
    REQUIRE(edges[16] == 1.0f);
}

TEST_CASE("ResidualBand edges are in ascending order", "[processors][residual_types]")
{
    const auto& edges = getResidualBandEdges();

    for (size_t i = 1; i < edges.size(); ++i)
    {
        REQUIRE(edges[i] > edges[i - 1]);
    }
}

// ============================================================================
// Centers within their corresponding edge pair
// ============================================================================

TEST_CASE("ResidualBand all centers are within their corresponding edge pair",
          "[processors][residual_types]")
{
    const auto& centers = getResidualBandCenters();
    const auto& edges = getResidualBandEdges();

    for (size_t i = 0; i < kResidualBands; ++i)
    {
        INFO("Band " << i << ": center=" << centers[i]
             << " edges=[" << edges[i] << ", " << edges[i + 1] << "]");
        REQUIRE(centers[i] > edges[i]);
        REQUIRE(centers[i] < edges[i + 1]);
    }
}
