// ==============================================================================
// Layer 3: System Component Tests - Granular Filter
// Part of spec 102-granular-filter
// ==============================================================================

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <krate/dsp/systems/granular_filter.h>

#include <array>
#include <cmath>
#include <vector>

using namespace Krate::DSP;
using Catch::Approx;

// =============================================================================
// Phase 2: Foundational Tests - FilteredGrainState
// =============================================================================

TEST_CASE("FilteredGrainState struct exists and has correct fields", "[systems][granular-filter][layer3][foundational]") {
    SECTION("struct can be instantiated with default values") {
        FilteredGrainState state;

        // Verify default cutoffHz value (1000.0f per data-model)
        REQUIRE(state.cutoffHz == Approx(1000.0f));

        // Verify default filterEnabled value (true per data-model)
        REQUIRE(state.filterEnabled == true);
    }

    SECTION("struct contains SVF instances for both channels") {
        FilteredGrainState state;

        // Prepare filters to verify they exist and can be used
        state.filterL.prepare(44100.0);
        state.filterR.prepare(44100.0);

        // Should be able to process through filters without crash
        float outL = state.filterL.process(0.5f);
        float outR = state.filterR.process(0.5f);

        // Just verify we got valid output (not NaN)
        REQUIRE_FALSE(std::isnan(outL));
        REQUIRE_FALSE(std::isnan(outR));
    }
}

// =============================================================================
// Phase 2: Foundational Tests - GranularFilter Class Skeleton
// =============================================================================

TEST_CASE("GranularFilter class instantiation and prepare", "[systems][granular-filter][layer3][foundational]") {
    SECTION("class can be instantiated") {
        GranularFilter filter;
        // Should not crash
    }

    SECTION("prepare() initializes with sample rate") {
        GranularFilter filter;
        filter.prepare(48000.0);
        // Should not crash
        REQUIRE(filter.activeGrainCount() == 0);
    }

    SECTION("prepare() with custom delay buffer size") {
        GranularFilter filter;
        filter.prepare(44100.0, 5.0f);  // 5 second buffer
        REQUIRE(filter.activeGrainCount() == 0);
    }
}

TEST_CASE("GranularFilter default parameter values", "[systems][granular-filter][layer3][foundational]") {
    GranularFilter filter;
    filter.prepare(44100.0);

    SECTION("default filter parameters") {
        REQUIRE(filter.isFilterEnabled() == true);
        REQUIRE(filter.getFilterCutoff() == Approx(1000.0f));
        REQUIRE(filter.getFilterResonance() == Approx(0.7071f).margin(0.001f)); // Butterworth Q
        REQUIRE(filter.getFilterType() == SVFMode::Lowpass);
        REQUIRE(filter.getCutoffRandomization() == Approx(0.0f));
    }

    SECTION("default granular parameters") {
        REQUIRE(filter.getTexture() == Approx(0.0f));
        REQUIRE(filter.isFrozen() == false);
        REQUIRE(filter.getPitchQuantMode() == PitchQuantMode::Off);
    }
}
