// ==============================================================================
// dB/Linear Conversion Utilities - Unit Tests
// ==============================================================================
// Layer 0: Core Utilities
// Constitution Principle VIII: Testing Discipline
// Constitution Principle XII: Test-First Development
//
// Tests for: src/dsp/core/db_utils.h
// Contract: specs/001-db-conversion/contracts/db_utils.h
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/core/db_utils.h"

#include <array>
#include <cmath>
#include <limits>

using namespace Iterum::DSP;
using Catch::Approx;

// ==============================================================================
// User Story 1: dbToGain Function Tests
// ==============================================================================
// Formula: gain = 10^(dB/20)
// Reference: specs/001-db-conversion/spec.md US1

TEST_CASE("dbToGain converts decibels to linear gain", "[dsp][core][db_utils][US1]") {

    SECTION("T007: 0 dB returns exactly 1.0 (unity gain)") {
        REQUIRE(dbToGain(0.0f) == 1.0f);
    }

    SECTION("T008: -20 dB returns 0.1") {
        REQUIRE(dbToGain(-20.0f) == Approx(0.1f));
    }

    SECTION("T009: +20 dB returns 10.0") {
        REQUIRE(dbToGain(20.0f) == Approx(10.0f));
    }

    SECTION("T010: -6.0206 dB returns approximately 0.5") {
        // -6.0206 dB = 20 * log10(0.5) = -6.0205999...
        REQUIRE(dbToGain(-6.0206f) == Approx(0.5f).margin(0.001f));
    }

    SECTION("T011: NaN input returns 0.0 (safe fallback)") {
        float nan = std::numeric_limits<float>::quiet_NaN();
        REQUIRE(dbToGain(nan) == 0.0f);
    }

    SECTION("T012: Extreme values return valid results without overflow") {
        // +200 dB should return a large but finite value
        float highGain = dbToGain(200.0f);
        REQUIRE(std::isfinite(highGain));
        REQUIRE(highGain > 0.0f);

        // -200 dB should return a very small but positive value
        float lowGain = dbToGain(-200.0f);
        REQUIRE(std::isfinite(lowGain));
        REQUIRE(lowGain > 0.0f);
        REQUIRE(lowGain < 1e-9f);
    }
}

// Additional dbToGain tests for comprehensive coverage

TEST_CASE("dbToGain formula verification", "[dsp][core][db_utils][US1]") {

    SECTION("+6 dB is approximately double") {
        // +6.0206 dB = 20 * log10(2) = 6.0205999...
        REQUIRE(dbToGain(6.0206f) == Approx(2.0f).margin(0.001f));
    }

    SECTION("-40 dB returns 0.01") {
        REQUIRE(dbToGain(-40.0f) == Approx(0.01f));
    }

    SECTION("-60 dB returns 0.001") {
        REQUIRE(dbToGain(-60.0f) == Approx(0.001f));
    }

    SECTION("Negative infinity returns 0") {
        float negInf = -std::numeric_limits<float>::infinity();
        // 10^(-inf/20) = 10^(-inf) = 0
        REQUIRE(dbToGain(negInf) == 0.0f);
    }
}
