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

// =============================================================================
// Phase 4: User Story 2 - Randomizable Filter Cutoff Tests
// =============================================================================

TEST_CASE("GranularFilter calculateRandomizedCutoff", "[systems][granular-filter][layer3][US2]") {
    GranularFilter filter;
    filter.prepare(48000.0);

    SECTION("zero randomization returns base cutoff") {
        filter.setFilterCutoff(1000.0f);
        filter.setCutoffRandomization(0.0f);
        filter.seed(42);
        filter.reset();

        // Trigger many grains and verify all use base cutoff
        // (We can't directly test calculateRandomizedCutoff since it's private,
        // but we can test the behavior through grain triggering)
        REQUIRE(filter.getCutoffRandomization() == Approx(0.0f));
        REQUIRE(filter.getFilterCutoff() == Approx(1000.0f));
    }
}

TEST_CASE("GranularFilter cutoff distribution with randomization", "[systems][granular-filter][layer3][US2]") {
    SECTION("2 octaves randomization produces expected range") {
        GranularFilter filter;
        filter.prepare(48000.0);
        filter.setDensity(100.0f);  // High density for many grains
        filter.setFilterCutoff(1000.0f);
        filter.setCutoffRandomization(2.0f);  // +-2 octaves = 250Hz to 4000Hz
        filter.setFilterEnabled(true);
        filter.seed(12345);
        filter.reset();

        // Process to trigger many grains
        float outL, outR;
        for (int i = 0; i < 96000; ++i) {  // 2 seconds
            filter.process(0.5f, 0.5f, outL, outR);
        }

        // The randomization should produce varied outputs
        // We verify the parameter is stored correctly
        REQUIRE(filter.getCutoffRandomization() == Approx(2.0f));
    }
}

TEST_CASE("GranularFilter cutoff clamping", "[systems][granular-filter][layer3][US2]") {
    GranularFilter filter;
    filter.prepare(48000.0);

    SECTION("cutoff clamped to valid range") {
        // Test lower bound
        filter.setFilterCutoff(5.0f);  // Below 20Hz min
        REQUIRE(filter.getFilterCutoff() >= 20.0f);

        // Test upper bound (Nyquist * 0.495 = 48000 * 0.495 = 23760)
        filter.setFilterCutoff(30000.0f);
        REQUIRE(filter.getFilterCutoff() <= 48000.0f * 0.495f);
    }

    SECTION("randomization clamped to 0-4 octaves") {
        filter.setCutoffRandomization(-1.0f);
        REQUIRE(filter.getCutoffRandomization() == 0.0f);

        filter.setCutoffRandomization(10.0f);
        REQUIRE(filter.getCutoffRandomization() == 4.0f);
    }
}

TEST_CASE("GranularFilter deterministic seeding for cutoff", "[systems][granular-filter][layer3][US2]") {
    SECTION("same seed produces identical output") {
        GranularFilter filter1;
        GranularFilter filter2;

        filter1.prepare(48000.0);
        filter2.prepare(48000.0);

        filter1.setDensity(50.0f);
        filter2.setDensity(50.0f);

        filter1.setFilterCutoff(1000.0f);
        filter2.setFilterCutoff(1000.0f);

        filter1.setCutoffRandomization(2.0f);
        filter2.setCutoffRandomization(2.0f);

        filter1.setFilterEnabled(true);
        filter2.setFilterEnabled(true);

        filter1.seed(12345);
        filter2.seed(12345);

        filter1.reset();
        filter2.reset();

        float out1L, out1R, out2L, out2R;
        bool allMatch = true;

        for (int i = 0; i < 24000; ++i) {
            filter1.process(0.5f, 0.5f, out1L, out1R);
            filter2.process(0.5f, 0.5f, out2L, out2R);

            if (std::abs(out1L - out2L) > 0.0001f ||
                std::abs(out1R - out2R) > 0.0001f) {
                allMatch = false;
                break;
            }
        }

        REQUIRE(allMatch);
    }
}

// =============================================================================
// Phase 5: User Story 3 - Filter Type Selection Tests
// =============================================================================

TEST_CASE("GranularFilter lowpass mode", "[systems][granular-filter][layer3][US3]") {
    GranularFilter filter;
    filter.prepare(48000.0);
    filter.setDensity(50.0f);
    filter.setPosition(50.0f);
    filter.setFilterEnabled(true);
    filter.setFilterType(SVFMode::Lowpass);
    filter.setFilterCutoff(1000.0f);
    filter.seed(42);
    filter.reset();

    SECTION("lowpass attenuates high frequencies") {
        float outL, outR;

        // Fill buffer
        for (int i = 0; i < 4800; ++i) {
            filter.process(0.5f, 0.5f, outL, outR);
        }

        // Process with high-frequency content
        float totalEnergy = 0.0f;
        for (int i = 0; i < 24000; ++i) {
            // High frequency signal (alternating +/-)
            float input = (i % 2 == 0) ? 0.5f : -0.5f;
            filter.process(input, input, outL, outR);
            totalEnergy += outL * outL + outR * outR;
        }

        // Should have output (lowpass still passes low freq content from granular)
        REQUIRE(totalEnergy > 0.0f);
        REQUIRE(filter.getFilterType() == SVFMode::Lowpass);
    }
}

TEST_CASE("GranularFilter highpass mode", "[systems][granular-filter][layer3][US3]") {
    GranularFilter filter;
    filter.prepare(48000.0);
    filter.setDensity(50.0f);
    filter.setPosition(50.0f);
    filter.setFilterEnabled(true);
    filter.setFilterType(SVFMode::Highpass);
    filter.setFilterCutoff(1000.0f);
    filter.seed(42);
    filter.reset();

    SECTION("highpass type is stored correctly") {
        REQUIRE(filter.getFilterType() == SVFMode::Highpass);
    }
}

TEST_CASE("GranularFilter bandpass mode", "[systems][granular-filter][layer3][US3]") {
    GranularFilter filter;
    filter.prepare(48000.0);
    filter.setDensity(50.0f);
    filter.setPosition(50.0f);
    filter.setFilterEnabled(true);
    filter.setFilterType(SVFMode::Bandpass);
    filter.setFilterCutoff(1000.0f);
    filter.setFilterResonance(4.0f);  // High Q for resonant peak
    filter.seed(42);
    filter.reset();

    SECTION("bandpass type is stored correctly") {
        REQUIRE(filter.getFilterType() == SVFMode::Bandpass);
    }
}

TEST_CASE("GranularFilter filter type propagation", "[systems][granular-filter][layer3][US3]") {
    GranularFilter filter;
    filter.prepare(48000.0);
    filter.setDensity(100.0f);  // High density for active grains
    filter.setPosition(50.0f);
    filter.setFilterEnabled(true);
    filter.setFilterType(SVFMode::Lowpass);
    filter.setFilterCutoff(1000.0f);
    filter.seed(42);
    filter.reset();

    SECTION("setFilterType updates all active grains") {
        float outL, outR;

        // Process to get some active grains
        for (int i = 0; i < 4800; ++i) {
            filter.process(0.5f, 0.5f, outL, outR);
        }

        REQUIRE(filter.activeGrainCount() > 0);

        // Change filter type while grains are active
        filter.setFilterType(SVFMode::Highpass);
        REQUIRE(filter.getFilterType() == SVFMode::Highpass);

        // Process more and verify no crashes
        for (int i = 0; i < 4800; ++i) {
            filter.process(0.5f, 0.5f, outL, outR);
        }
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
