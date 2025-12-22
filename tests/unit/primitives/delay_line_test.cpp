// ==============================================================================
// Layer 1: DSP Primitive Tests - DelayLine
// ==============================================================================
// Test-First Development (Constitution Principle XII)
// Tests written before implementation.
// ==============================================================================

#define CATCH_CONFIG_MAIN
#include <catch2/catch_all.hpp>

#include "dsp/primitives/delay_line.h"

#include <cmath>
#include <array>
#include <numeric>

using namespace Iterum::DSP;

// =============================================================================
// Phase 2: Foundational Tests (T006)
// =============================================================================

TEST_CASE("nextPowerOf2 utility function", "[delay][utility]") {
    SECTION("powers of 2 return unchanged") {
        CHECK(nextPowerOf2(1) == 1);
        CHECK(nextPowerOf2(2) == 2);
        CHECK(nextPowerOf2(4) == 4);
        CHECK(nextPowerOf2(1024) == 1024);
        CHECK(nextPowerOf2(65536) == 65536);
    }

    SECTION("non-powers of 2 round up") {
        CHECK(nextPowerOf2(3) == 4);
        CHECK(nextPowerOf2(5) == 8);
        CHECK(nextPowerOf2(100) == 128);
        CHECK(nextPowerOf2(1000) == 1024);
        CHECK(nextPowerOf2(44100) == 65536);
    }

    SECTION("zero returns 1") {
        CHECK(nextPowerOf2(0) == 1);
    }
}

TEST_CASE("DelayLine prepare allocates buffer", "[delay][prepare]") {
    DelayLine delay;

    SECTION("prepares with standard sample rate") {
        delay.prepare(44100.0, 1.0f);  // 1 second max delay

        // Should have at least 44100 samples capacity
        REQUIRE(delay.maxDelaySamples() >= 44100);
        REQUIRE(delay.sampleRate() == 44100.0);
    }

    SECTION("prepares with high sample rate") {
        delay.prepare(96000.0, 0.5f);  // 0.5 seconds at 96kHz

        // Should have at least 48000 samples capacity
        REQUIRE(delay.maxDelaySamples() >= 48000);
        REQUIRE(delay.sampleRate() == 96000.0);
    }

    SECTION("prepares with maximum delay (10 seconds at 192kHz)") {
        delay.prepare(192000.0, 10.0f);  // 10 seconds at 192kHz

        // Should have at least 1,920,000 samples capacity
        REQUIRE(delay.maxDelaySamples() >= 1920000);
        REQUIRE(delay.sampleRate() == 192000.0);
    }
}

TEST_CASE("DelayLine prepare with different sample rates", "[delay][prepare]") {
    DelayLine delay;

    SECTION("44.1 kHz") {
        delay.prepare(44100.0, 1.0f);
        REQUIRE(delay.sampleRate() == 44100.0);
        REQUIRE(delay.maxDelaySamples() == 44100);
    }

    SECTION("48 kHz") {
        delay.prepare(48000.0, 1.0f);
        REQUIRE(delay.sampleRate() == 48000.0);
        REQUIRE(delay.maxDelaySamples() == 48000);
    }

    SECTION("96 kHz") {
        delay.prepare(96000.0, 1.0f);
        REQUIRE(delay.sampleRate() == 96000.0);
        REQUIRE(delay.maxDelaySamples() == 96000);
    }

    SECTION("192 kHz") {
        delay.prepare(192000.0, 1.0f);
        REQUIRE(delay.sampleRate() == 192000.0);
        REQUIRE(delay.maxDelaySamples() == 192000);
    }
}

TEST_CASE("DelayLine reset clears buffer to silence", "[delay][reset]") {
    DelayLine delay;
    delay.prepare(44100.0, 0.1f);  // 100ms max delay

    // Write some samples
    for (int i = 0; i < 100; ++i) {
        delay.write(1.0f);
    }

    // Reset should clear all samples to zero
    delay.reset();

    // All reads should return zero after reset
    REQUIRE(delay.read(0) == 0.0f);
    REQUIRE(delay.read(10) == 0.0f);
    REQUIRE(delay.read(50) == 0.0f);
    REQUIRE(delay.read(99) == 0.0f);
}

TEST_CASE("DelayLine reset preserves configuration", "[delay][reset]") {
    DelayLine delay;
    delay.prepare(48000.0, 0.5f);

    // Capture config before reset
    double sampleRateBefore = delay.sampleRate();
    size_t maxDelayBefore = delay.maxDelaySamples();

    delay.reset();

    // Configuration should be unchanged
    REQUIRE(delay.sampleRate() == sampleRateBefore);
    REQUIRE(delay.maxDelaySamples() == maxDelayBefore);
}

TEST_CASE("DelayLine can be re-prepared", "[delay][prepare]") {
    DelayLine delay;

    // First prepare
    delay.prepare(44100.0, 1.0f);
    REQUIRE(delay.sampleRate() == 44100.0);
    REQUIRE(delay.maxDelaySamples() == 44100);

    // Re-prepare with different settings
    delay.prepare(96000.0, 2.0f);
    REQUIRE(delay.sampleRate() == 96000.0);
    REQUIRE(delay.maxDelaySamples() == 192000);
}

TEST_CASE("DelayLine unprepared state", "[delay][prepare]") {
    DelayLine delay;

    // Before prepare(), should return zeros
    REQUIRE(delay.sampleRate() == 0.0);
    REQUIRE(delay.maxDelaySamples() == 0);
}
