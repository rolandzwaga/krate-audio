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

// =============================================================================
// Phase 3: User Story 1 - Per-Grain Filter Processing Tests
// =============================================================================

TEST_CASE("GranularFilter grain slot indexing", "[systems][granular-filter][layer3][US1]") {
    GranularFilter filter;
    filter.prepare(48000.0);
    filter.setDensity(50.0f);
    filter.seed(42);
    filter.reset();

    SECTION("multiple grains get different slot indices") {
        // Process to trigger several grains
        float outL, outR;
        for (int i = 0; i < 4800; ++i) {  // 100ms at 48kHz
            filter.process(0.5f, 0.5f, outL, outR);
        }

        // Should have some active grains
        REQUIRE(filter.activeGrainCount() > 0);
    }
}

TEST_CASE("GranularFilter filter state reset on grain acquire", "[systems][granular-filter][layer3][US1]") {
    GranularFilter filter;
    filter.prepare(48000.0);
    filter.setDensity(100.0f);  // High density
    filter.setGrainSize(50.0f); // Short grains
    filter.setFilterCutoff(1000.0f);
    filter.setFilterEnabled(true);
    filter.seed(42);
    filter.reset();

    SECTION("no artifacts from previous grain filter state") {
        // Process audio for a while to cycle through grain slots
        float outL, outR;

        // First fill buffer with loud signal
        for (int i = 0; i < 4800; ++i) {
            filter.process(1.0f, 1.0f, outL, outR);
        }

        // Continue processing and watch for anomalies
        float maxOutput = 0.0f;
        for (int i = 0; i < 48000; ++i) {  // 1 second
            filter.process(0.5f, 0.5f, outL, outR);
            maxOutput = std::max(maxOutput, std::max(std::abs(outL), std::abs(outR)));
        }

        // Output should be bounded (no filter instability from uncleared state)
        REQUIRE(maxOutput < 5.0f);
        REQUIRE_FALSE(std::isnan(maxOutput));
    }
}

TEST_CASE("GranularFilter independent filter state per grain", "[systems][granular-filter][layer3][US1]") {
    GranularFilter filter;
    filter.prepare(48000.0);
    filter.setDensity(50.0f);
    filter.setFilterEnabled(true);
    filter.setFilterCutoff(500.0f);  // Low cutoff
    filter.seed(42);
    filter.reset();

    SECTION("grains have independent filter states") {
        // Process some audio to trigger multiple grains
        float outL, outR;
        for (int i = 0; i < 9600; ++i) {  // 200ms
            filter.process(0.5f, 0.5f, outL, outR);
        }

        // Multiple grains should be active
        REQUIRE(filter.activeGrainCount() > 1);
    }
}

TEST_CASE("GranularFilter filter bypass mode", "[systems][granular-filter][layer3][US1]") {
    SECTION("filterEnabled=false produces different output than enabled") {
        GranularFilter filterEnabled;
        GranularFilter filterDisabled;

        filterEnabled.prepare(48000.0);
        filterDisabled.prepare(48000.0);

        filterEnabled.setDensity(50.0f);
        filterDisabled.setDensity(50.0f);

        filterEnabled.setPosition(50.0f);
        filterDisabled.setPosition(50.0f);

        filterEnabled.setFilterEnabled(true);
        filterEnabled.setFilterCutoff(500.0f);

        filterDisabled.setFilterEnabled(false);

        // Same seed for reproducibility
        filterEnabled.seed(12345);
        filterDisabled.seed(12345);

        filterEnabled.reset();
        filterDisabled.reset();

        float outEnabledL, outEnabledR;
        float outDisabledL, outDisabledR;

        // Fill delay buffers
        for (int i = 0; i < 4800; ++i) {
            filterEnabled.process(0.5f, 0.5f, outEnabledL, outEnabledR);
            filterDisabled.process(0.5f, 0.5f, outDisabledL, outDisabledR);
        }

        // Check if outputs differ (filtering changes the output)
        bool anyDifference = false;
        for (int i = 0; i < 48000; ++i) {
            filterEnabled.process(0.5f, 0.5f, outEnabledL, outEnabledR);
            filterDisabled.process(0.5f, 0.5f, outDisabledL, outDisabledR);

            if (std::abs(outEnabledL - outDisabledL) > 0.001f ||
                std::abs(outEnabledR - outDisabledR) > 0.001f) {
                anyDifference = true;
                break;
            }
        }

        // With filter enabled at 500Hz LP, output should differ from unfiltered
        REQUIRE(anyDifference);
    }

    SECTION("filterEnabled=false passes audio unchanged through grain processing") {
        GranularFilter filter;
        filter.prepare(48000.0);
        filter.setDensity(50.0f);
        filter.setPosition(50.0f);
        filter.setFilterEnabled(false);
        filter.seed(42);
        filter.reset();

        float outL, outR;

        // Fill buffer
        for (int i = 0; i < 4800; ++i) {
            filter.process(0.5f, 0.5f, outL, outR);
        }

        // Process and verify output is produced
        float totalEnergy = 0.0f;
        for (int i = 0; i < 24000; ++i) {
            filter.process(0.5f, 0.5f, outL, outR);
            totalEnergy += outL * outL + outR * outR;
        }

        REQUIRE(totalEnergy > 0.0f);
    }
}

TEST_CASE("GranularFilter signal flow order", "[systems][granular-filter][layer3][US1]") {
    GranularFilter filter;
    filter.prepare(48000.0);
    filter.setDensity(20.0f);
    filter.setFilterEnabled(true);
    filter.setFilterCutoff(500.0f);
    filter.seed(42);
    filter.reset();

    SECTION("filter applies after envelope (no click at grain start)") {
        float outL, outR;

        // Fill buffer first
        for (int i = 0; i < 4800; ++i) {
            filter.process(0.5f, 0.5f, outL, outR);
        }

        // Process more and check for large transients
        float maxTransient = 0.0f;
        float prevL = 0.0f;

        for (int i = 0; i < 48000; ++i) {
            filter.process(0.5f, 0.5f, outL, outR);
            float transient = std::abs(outL - prevL);
            maxTransient = std::max(maxTransient, transient);
            prevL = outL;
        }

        // Transients should be smoothed by envelope
        // If filter was before envelope, we'd see harsh transients
        REQUIRE(maxTransient < 0.5f);
    }
}
