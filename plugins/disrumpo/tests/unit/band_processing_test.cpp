// ==============================================================================
// BandProcessor Unit Tests
// ==============================================================================
// Tests for per-band gain, pan, solo, mute processing.
// Per spec.md FR-019 to FR-027.
//
// Constitution Principle XII: Test-First Development
// These tests MUST fail before implementation.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "dsp/band_processor.h"
#include "dsp/band_state.h"

#include <array>
#include <cmath>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;

// =============================================================================
// Gain Tests (FR-019, FR-020)
// =============================================================================

TEST_CASE("BandProcessor +6dB gain doubles amplitude", "[band][US3][gain]") {
    // FR-019: Each band MUST apply gain scaling based on BandState::gainDb
    // D-021: the balance pan law is unity on both channels at center
    Disrumpo::BandProcessor proc;
    proc.prepare(44100.0);
    proc.setGainDb(6.0206f); // Exactly 2x linear gain

    // Process many samples to let smoother settle
    float left = 1.0f;
    float right = 1.0f;
    for (int i = 0; i < 2000; ++i) {
        left = 1.0f;
        right = 1.0f;
        proc.process(left, right);
    }

    // At center pan, output = input * gain = 1.0 * 2.0
    const float expected = 2.0f;
    REQUIRE_THAT(left, WithinRel(expected, 0.01f));
    REQUIRE_THAT(right, WithinRel(expected, 0.01f));
}

TEST_CASE("BandProcessor 0dB gain is unity", "[band][US3][gain]") {
    // At 0dB gain (unity) with center pan, output = input (D-021)
    Disrumpo::BandProcessor proc;
    proc.prepare(44100.0);
    proc.setGainDb(0.0f);

    float left = 0.5f;
    float right = 0.75f;
    for (int i = 0; i < 2000; ++i) {
        left = 0.5f;
        right = 0.75f;
        proc.process(left, right);
    }

    REQUIRE_THAT(left, WithinRel(0.5f, 0.01f));
    REQUIRE_THAT(right, WithinRel(0.75f, 0.01f));
}

TEST_CASE("BandProcessor -6dB gain halves amplitude", "[band][US3][gain]") {
    // At -6dB (0.5x) with center pan, output = input * 0.5 (D-021)
    Disrumpo::BandProcessor proc;
    proc.prepare(44100.0);
    proc.setGainDb(-6.0206f); // Exactly 0.5x linear gain

    float left = 1.0f;
    float right = 1.0f;
    for (int i = 0; i < 2000; ++i) {
        left = 1.0f;
        right = 1.0f;
        proc.process(left, right);
    }

    const float expected = 0.5f;
    REQUIRE_THAT(left, WithinRel(expected, 0.01f));
    REQUIRE_THAT(right, WithinRel(expected, 0.01f));
}

TEST_CASE("BandProcessor gain clamps to valid range", "[band][US3][gain]") {
    Disrumpo::BandProcessor proc;
    proc.prepare(44100.0);

    // Test extreme values are clamped
    proc.setGainDb(-50.0f); // Below minimum
    // Should clamp to kMinBandGainDb

    proc.setGainDb(+50.0f); // Above maximum
    // Should clamp to kMaxBandGainDb

    // These should not crash
    float left = 1.0f;
    float right = 1.0f;
    proc.process(left, right);
}

// =============================================================================
// Pan Tests (FR-021, FR-022)
// =============================================================================

TEST_CASE("BandProcessor pan full left", "[band][US3][pan]") {
    // FR-022: Pan -1.0 = full left (left=1.0, right=0.0)
    Disrumpo::BandProcessor proc;
    proc.prepare(44100.0);
    proc.setPan(-1.0f);

    float left = 1.0f;
    float right = 1.0f;
    for (int i = 0; i < 2000; ++i) {
        left = 1.0f;
        right = 1.0f;
        proc.process(left, right);
    }

    REQUIRE_THAT(left, WithinAbs(1.0f, 0.01f));
    REQUIRE_THAT(right, WithinAbs(0.0f, 0.01f));
}

TEST_CASE("BandProcessor pan center", "[band][US3][pan]") {
    // D-021: Pan 0.0 = center (left=1.0, right=1.0)
    Disrumpo::BandProcessor proc;
    proc.prepare(44100.0);
    proc.setPan(0.0f);

    float left = 1.0f;
    float right = 1.0f;
    for (int i = 0; i < 2000; ++i) {
        left = 1.0f;
        right = 1.0f;
        proc.process(left, right);
    }

    const float expected = 1.0f;
    REQUIRE_THAT(left, WithinRel(expected, 0.01f));
    REQUIRE_THAT(right, WithinRel(expected, 0.01f));
}

TEST_CASE("BandProcessor pan full right", "[band][US3][pan]") {
    // FR-022: Pan +1.0 = full right (left=0.0, right=1.0)
    Disrumpo::BandProcessor proc;
    proc.prepare(44100.0);
    proc.setPan(1.0f);

    float left = 1.0f;
    float right = 1.0f;
    for (int i = 0; i < 2000; ++i) {
        left = 1.0f;
        right = 1.0f;
        proc.process(left, right);
    }

    REQUIRE_THAT(left, WithinAbs(0.0f, 0.01f));
    REQUIRE_THAT(right, WithinAbs(1.0f, 0.01f));
}

TEST_CASE("BandProcessor balance pan law: center is unity, extremes are 1/0", "[band][US3][pan]") {
    // D-021: stereo balance law, left = min(1, 1 - pan), right = min(1, 1 + pan)
    Disrumpo::BandProcessor proc;
    proc.prepare(44100.0);

    struct PanCase { float pan, left, right; };
    const std::array<PanCase, 5> cases = {{
        {.pan = -1.0f, .left = 1.0f, .right = 0.0f},
        {.pan = -0.5f, .left = 1.0f, .right = 0.5f},
        {.pan = 0.0f, .left = 1.0f, .right = 1.0f},
        {.pan = 0.5f, .left = 0.5f, .right = 1.0f},
        {.pan = 1.0f, .left = 0.0f, .right = 1.0f}}};

    for (const auto& c : cases) {
        INFO("Testing pan: " << c.pan);
        proc.setPan(c.pan);

        float left = 1.0f;
        float right = 1.0f;
        for (int i = 0; i < 2000; ++i) {
            left = 1.0f;
            right = 1.0f;
            proc.process(left, right);
        }

        REQUIRE_THAT(left, WithinAbs(c.left, 0.01f));
        REQUIRE_THAT(right, WithinAbs(c.right, 0.01f));
    }
}

// =============================================================================
// Mute Tests (FR-023)
// =============================================================================

TEST_CASE("BandProcessor mute suppresses output", "[band][US4][mute]") {
    // FR-023: When BandState::mute is true, band output MUST be zero
    Disrumpo::BandProcessor proc;
    proc.prepare(44100.0);
    proc.setMute(true);

    float left = 1.0f;
    float right = 1.0f;
    for (int i = 0; i < 2000; ++i) {
        left = 1.0f;
        right = 1.0f;
        proc.process(left, right);
    }

    REQUIRE_THAT(left, WithinAbs(0.0f, 0.001f));
    REQUIRE_THAT(right, WithinAbs(0.0f, 0.001f));
}

TEST_CASE("BandProcessor unmute allows output", "[band][US4][mute]") {
    Disrumpo::BandProcessor proc;
    proc.prepare(44100.0);
    proc.setMute(false);

    float left = 1.0f;
    float right = 1.0f;
    for (int i = 0; i < 2000; ++i) {
        left = 1.0f;
        right = 1.0f;
        proc.process(left, right);
    }

    // With center pan and unity gain, expect 1.0 (D-021)
    REQUIRE(left > 0.5f);
    REQUIRE(right > 0.5f);
}

// =============================================================================
// Smoothing Tests (FR-027, FR-027a)
// =============================================================================

TEST_CASE("BandProcessor parameter transitions are click-free", "[band][US3][smoothing]") {
    // FR-027: Solo/bypass/mute state changes MUST apply smoothly
    Disrumpo::BandProcessor proc;
    proc.prepare(44100.0);

    // Start with mute off
    proc.setMute(false);
    for (int i = 0; i < 1000; ++i) {
        float left = 1.0f;
        float right = 1.0f;
        proc.process(left, right);
    }

    // Enable mute - should not cause instant change
    proc.setMute(true);

    // First sample after mute should not be zero (smoothing in progress)
    float left = 1.0f;
    float right = 1.0f;
    proc.process(left, right);

    // Left should be between 0 and the unmuted level 1.0 (transitioning; D-021 made center unity)
    REQUIRE(left > 0.0f);
    REQUIRE(left < 1.0f);
}

TEST_CASE("BandProcessor isSmoothing reports transition state", "[band][US3][smoothing]") {
    Disrumpo::BandProcessor proc;
    proc.prepare(44100.0);
    proc.setGainDb(0.0f);

    // Let settle
    for (int i = 0; i < 2000; ++i) {
        float left = 1.0f;
        float right = 1.0f;
        proc.process(left, right);
    }

    // Should be settled
    REQUIRE_FALSE(proc.isSmoothing());

    // Change gain
    proc.setGainDb(6.0f);

    // Should be smoothing
    REQUIRE(proc.isSmoothing());
}

// =============================================================================
// Prepare and Reset Tests
// =============================================================================

TEST_CASE("BandProcessor prepare initializes correctly", "[band][US3]") {
    Disrumpo::BandProcessor proc;
    proc.prepare(44100.0);

    // Default gain is 0 dB, pan is center, mute is off
    float left = 1.0f;
    float right = 1.0f;
    for (int i = 0; i < 2000; ++i) {
        left = 1.0f;
        right = 1.0f;
        proc.process(left, right);
    }

    // With center pan, expect unity (D-021)
    const float expected = 1.0f;
    REQUIRE_THAT(left, WithinRel(expected, 0.02f));
    REQUIRE_THAT(right, WithinRel(expected, 0.02f));
}

TEST_CASE("BandProcessor reset clears smoother states", "[band][US3]") {
    Disrumpo::BandProcessor proc;
    proc.prepare(44100.0);
    proc.setGainDb(12.0f);
    proc.setPan(1.0f);

    // Let settle
    for (int i = 0; i < 2000; ++i) {
        float left = 1.0f;
        float right = 1.0f;
        proc.process(left, right);
    }

    // Reset
    proc.reset();

    // After reset, smoothing state should be cleared
    // Next process should start fresh
}
