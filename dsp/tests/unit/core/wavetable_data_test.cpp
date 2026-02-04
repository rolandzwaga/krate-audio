// ==============================================================================
// Tests: WavetableData and Mipmap Level Selection
// ==============================================================================
// Test suite for WavetableData struct and selectMipmapLevel functions (Layer 0).
// Covers User Story 1: data structure, guard samples, level selection.
//
// Reference: specs/016-wavetable-oscillator/spec.md
//
// IMPORTANT: All sample-processing loops collect metrics inside the loop and
// assert ONCE after the loop. See testing-guide anti-patterns.
// ==============================================================================

#include <krate/dsp/core/wavetable_data.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>

using Catch::Approx;
using namespace Krate::DSP;

// =============================================================================
// WavetableData Construction and Properties (T005-T008)
// =============================================================================

TEST_CASE("WavetableData default construction", "[WavetableData][US1]") {
    SECTION("constants have correct values (SC-001 area)") {
        REQUIRE(kDefaultTableSize == 2048);
        REQUIRE(kMaxMipmapLevels == 11);
        REQUIRE(kGuardSamples == 4);
    }

    SECTION("default state: numLevels is 0 and data is zero-initialized") {
        WavetableData data;
        REQUIRE(data.numLevels() == 0);

        // Verify zero-initialized by checking level 0 data
        // Note: getLevel should return nullptr when numLevels == 0 for out-of-range
        // But getMutableLevel should work for physical access
        float* level0 = data.getMutableLevel(0);
        REQUIRE(level0 != nullptr);

        bool allZero = true;
        for (size_t i = 0; i < kDefaultTableSize; ++i) {
            if (level0[i] != 0.0f) {
                allZero = false;
                break;
            }
        }
        REQUIRE(allZero);
    }

    SECTION("getLevel with invalid index returns nullptr") {
        WavetableData data;
        data.setNumLevels(5);

        REQUIRE(data.getLevel(5) == nullptr);
        REQUIRE(data.getLevel(11) == nullptr);
        REQUIRE(data.getLevel(100) == nullptr);

        // Valid indices should work
        REQUIRE(data.getLevel(0) != nullptr);
        REQUIRE(data.getLevel(4) != nullptr);
    }

    SECTION("tableSize returns kDefaultTableSize") {
        WavetableData data;
        REQUIRE(data.tableSize() == 2048);
    }
}

// =============================================================================
// selectMipmapLevel Tests (T009-T014)
// =============================================================================

TEST_CASE("selectMipmapLevel integer level selection", "[WavetableData][US1]") {
    SECTION("low frequency returns level 0 (SC-001)") {
        // ratio = 20 * 2048 / 44100 = 0.93, log2 = -0.11, clamped to 0
        size_t level = selectMipmapLevel(20.0f, 44100.0f, 2048);
        REQUIRE(level == 0);
    }

    SECTION("high frequency returns level 9 (SC-002)") {
        // ratio = 10000 * 2048 / 44100 = 464.4, log2 = 8.86, ceil = 9
        // Level 9 has 2 harmonics (max 20000 Hz < Nyquist). Level 8 would have
        // 4 harmonics (max 40000 Hz > Nyquist), so ceil is required for safety.
        size_t level = selectMipmapLevel(10000.0f, 44100.0f, 2048);
        REQUIRE(level == 9);
    }

    SECTION("zero frequency returns level 0 (SC-003)") {
        size_t level = selectMipmapLevel(0.0f, 44100.0f, 2048);
        REQUIRE(level == 0);
    }

    SECTION("Nyquist frequency returns highest level (SC-004)") {
        // ratio = 22050 * 2048 / 44100 = 1024, log2 = 10
        size_t level = selectMipmapLevel(22050.0f, 44100.0f, 2048);
        REQUIRE(level == 10);
    }

    SECTION("negative frequency returns 0") {
        size_t level = selectMipmapLevel(-100.0f, 44100.0f, 2048);
        REQUIRE(level == 0);
    }

    SECTION("frequency above Nyquist is clamped to highest level") {
        size_t level = selectMipmapLevel(30000.0f, 44100.0f, 2048);
        REQUIRE(level == 10);  // kMaxMipmapLevels - 1
    }

    SECTION("100 Hz returns level 3") {
        // ratio = 100 * 2048 / 44100 = 4.64, log2 = 2.22, ceil = 3
        // Level 3 has 128 harmonics (max 12800 Hz < Nyquist). Level 2 would have
        // 256 harmonics (max 25600 Hz > Nyquist).
        size_t level = selectMipmapLevel(100.0f, 44100.0f, 2048);
        REQUIRE(level == 3);
    }

    SECTION("440 Hz returns level 5") {
        // ratio = 440 * 2048 / 44100 = 20.42, log2 = 4.35, ceil = 5
        // Level 5 has 32 harmonics (max 14080 Hz < Nyquist). Level 4 would have
        // 64 harmonics (max 28160 Hz > Nyquist).
        size_t level = selectMipmapLevel(440.0f, 44100.0f, 2048);
        REQUIRE(level == 5);
    }
}

// =============================================================================
// selectMipmapLevelFractional Tests (T015-T018)
// =============================================================================

TEST_CASE("selectMipmapLevelFractional fractional level selection", "[WavetableData][US1]") {
    SECTION("returns float values for crossfading") {
        float fracLevel = selectMipmapLevelFractional(440.0f, 44100.0f, 2048);
        // Should be approximately 4.35 (log2(440 * 2048 / 44100) = log2(20.42))
        REQUIRE(fracLevel == Approx(4.35f).margin(0.1f));
    }

    SECTION("low frequency returns value near 0.0") {
        float fracLevel = selectMipmapLevelFractional(100.0f, 44100.0f, 2048);
        // log2(100 * 2048 / 44100) = log2(4.64) = 2.22
        REQUIRE(fracLevel == Approx(2.22f).margin(0.05f));
    }

    SECTION("each frequency doubling increases fractional level by approximately 1.0") {
        float level1 = selectMipmapLevelFractional(500.0f, 44100.0f, 2048);
        float level2 = selectMipmapLevelFractional(1000.0f, 44100.0f, 2048);
        float level3 = selectMipmapLevelFractional(2000.0f, 44100.0f, 2048);

        REQUIRE(level2 - level1 == Approx(1.0f).margin(0.01f));
        REQUIRE(level3 - level2 == Approx(1.0f).margin(0.01f));
    }

    SECTION("result clamped to [0.0, kMaxMipmapLevels - 1.0]") {
        // Very low frequency should be clamped to 0
        float lowLevel = selectMipmapLevelFractional(1.0f, 44100.0f, 2048);
        REQUIRE(lowLevel >= 0.0f);

        // Very high frequency should be clamped to kMaxMipmapLevels - 1
        float highLevel = selectMipmapLevelFractional(30000.0f, 44100.0f, 2048);
        REQUIRE(highLevel <= static_cast<float>(kMaxMipmapLevels - 1));

        // Zero frequency
        float zeroLevel = selectMipmapLevelFractional(0.0f, 44100.0f, 2048);
        REQUIRE(zeroLevel == 0.0f);

        // Negative frequency
        float negLevel = selectMipmapLevelFractional(-100.0f, 44100.0f, 2048);
        REQUIRE(negLevel == 0.0f);
    }
}
