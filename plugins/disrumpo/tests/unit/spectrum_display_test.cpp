// ==============================================================================
// SpectrumDisplay Coordinate Conversion Tests
// ==============================================================================
// Constitution Principle VIII: Testing Discipline
// Tests for frequency-to-pixel and pixel-to-frequency coordinate mapping
// using logarithmic scale (20Hz - 20kHz)
//
// Formula: x = width * log2(freq/20) / log2(20000/20)
//        = width * log2(freq/20) / log2(1000)
// Inverse: freq = 20 * 2^(x/width * log2(1000))
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <cmath>

using Catch::Approx;

// ==============================================================================
// Coordinate Conversion Functions (to be implemented in spectrum_display.cpp)
// ==============================================================================
// These are standalone functions for testing purposes.
// The actual SpectrumDisplay class will use these same formulas.

namespace {

constexpr float kMinFreqHz = 20.0f;
constexpr float kMaxFreqHz = 20000.0f;
constexpr float kLogRatio = 9.9657842846620869f;  // log2(20000/20) = log2(1000)

/// Convert frequency (Hz) to X coordinate (pixels from left edge)
/// @param freq Frequency in Hz [20, 20000]
/// @param width Display width in pixels
/// @return X coordinate [0, width]
float freqToX(float freq, float width) {
    if (freq <= kMinFreqHz) return 0.0f;
    if (freq >= kMaxFreqHz) return width;

    float logPos = std::log2(freq / kMinFreqHz) / kLogRatio;
    return width * logPos;
}

/// Convert X coordinate (pixels from left edge) to frequency (Hz)
/// @param x X coordinate [0, width]
/// @param width Display width in pixels
/// @return Frequency in Hz [20, 20000]
float xToFreq(float x, float width) {
    if (x <= 0.0f) return kMinFreqHz;
    if (x >= width) return kMaxFreqHz;

    float logPos = x / width;
    return kMinFreqHz * std::pow(2.0f, logPos * kLogRatio);
}

}  // anonymous namespace

// ==============================================================================
// Test: freqToX Boundary Conditions
// ==============================================================================
TEST_CASE("freqToX returns correct boundary values", "[spectrum][coordinate]") {
    const float width = 960.0f;  // Typical display width

    SECTION("20 Hz maps to x = 0") {
        REQUIRE(freqToX(20.0f, width) == Approx(0.0f).margin(0.001f));
    }

    SECTION("20000 Hz maps to x = width") {
        REQUIRE(freqToX(20000.0f, width) == Approx(width).margin(0.001f));
    }

    SECTION("Frequencies below 20 Hz clamp to 0") {
        REQUIRE(freqToX(10.0f, width) == 0.0f);
        REQUIRE(freqToX(0.0f, width) == 0.0f);
    }

    SECTION("Frequencies above 20000 Hz clamp to width") {
        REQUIRE(freqToX(25000.0f, width) == width);
        REQUIRE(freqToX(100000.0f, width) == width);
    }
}

// ==============================================================================
// Test: xToFreq Boundary Conditions
// ==============================================================================
TEST_CASE("xToFreq returns correct boundary values", "[spectrum][coordinate]") {
    const float width = 960.0f;

    SECTION("x = 0 maps to 20 Hz") {
        REQUIRE(xToFreq(0.0f, width) == Approx(20.0f).margin(0.001f));
    }

    SECTION("x = width maps to 20000 Hz") {
        REQUIRE(xToFreq(width, width) == Approx(20000.0f).margin(1.0f));
    }

    SECTION("Negative x clamps to 20 Hz") {
        REQUIRE(xToFreq(-10.0f, width) == 20.0f);
        REQUIRE(xToFreq(-100.0f, width) == 20.0f);
    }

    SECTION("x > width clamps to 20000 Hz") {
        REQUIRE(xToFreq(width + 10.0f, width) == 20000.0f);
        REQUIRE(xToFreq(width * 2.0f, width) == 20000.0f);
    }
}

// ==============================================================================
// Test: freqToX Known Reference Points
// ==============================================================================
TEST_CASE("freqToX returns correct values for known frequencies", "[spectrum][coordinate]") {
    const float width = 960.0f;

    // For log2 scale: x/width = log2(freq/20) / log2(1000)
    // log2(1000) ≈ 9.9658

    SECTION("200 Hz (one decade above 20)") {
        // log2(200/20) = log2(10) ≈ 3.3219
        // x = 960 * 3.3219 / 9.9658 ≈ 320.0
        float x = freqToX(200.0f, width);
        REQUIRE(x == Approx(320.0f).margin(1.0f));
    }

    SECTION("2000 Hz (two decades above 20)") {
        // log2(2000/20) = log2(100) ≈ 6.6439
        // x = 960 * 6.6439 / 9.9658 ≈ 640.0
        float x = freqToX(2000.0f, width);
        REQUIRE(x == Approx(640.0f).margin(1.0f));
    }

    SECTION("1000 Hz (geometric center)") {
        // log2(1000/20) = log2(50) ≈ 5.6439
        // x = 960 * 5.6439 / 9.9658 ≈ 544.0
        float x = freqToX(1000.0f, width);
        REQUIRE(x == Approx(544.0f).margin(2.0f));
    }
}

// ==============================================================================
// Test: Round-Trip Conversion Accuracy
// ==============================================================================
TEST_CASE("Round-trip conversion xToFreq(freqToX(f)) recovers original frequency", "[spectrum][coordinate]") {
    const float width = 960.0f;

    SECTION("Test at 20 Hz") {
        float freq = 20.0f;
        float recovered = xToFreq(freqToX(freq, width), width);
        REQUIRE(recovered == Approx(freq).margin(0.1f));
    }

    SECTION("Test at 100 Hz") {
        float freq = 100.0f;
        float recovered = xToFreq(freqToX(freq, width), width);
        REQUIRE(recovered == Approx(freq).margin(0.5f));
    }

    SECTION("Test at 500 Hz") {
        float freq = 500.0f;
        float recovered = xToFreq(freqToX(freq, width), width);
        REQUIRE(recovered == Approx(freq).margin(2.0f));
    }

    SECTION("Test at 1000 Hz") {
        float freq = 1000.0f;
        float recovered = xToFreq(freqToX(freq, width), width);
        REQUIRE(recovered == Approx(freq).margin(5.0f));
    }

    SECTION("Test at 5000 Hz") {
        float freq = 5000.0f;
        float recovered = xToFreq(freqToX(freq, width), width);
        REQUIRE(recovered == Approx(freq).margin(20.0f));
    }

    SECTION("Test at 20000 Hz") {
        float freq = 20000.0f;
        float recovered = xToFreq(freqToX(freq, width), width);
        REQUIRE(recovered == Approx(freq).margin(50.0f));
    }
}

// ==============================================================================
// Test: Inverse Round-Trip Conversion
// ==============================================================================
TEST_CASE("Round-trip conversion freqToX(xToFreq(x)) recovers original x", "[spectrum][coordinate]") {
    const float width = 960.0f;

    SECTION("Test at x = 0") {
        float x = 0.0f;
        float recovered = freqToX(xToFreq(x, width), width);
        REQUIRE(recovered == Approx(x).margin(0.1f));
    }

    SECTION("Test at x = width/4") {
        float x = width / 4.0f;
        float recovered = freqToX(xToFreq(x, width), width);
        REQUIRE(recovered == Approx(x).margin(0.5f));
    }

    SECTION("Test at x = width/2") {
        float x = width / 2.0f;
        float recovered = freqToX(xToFreq(x, width), width);
        REQUIRE(recovered == Approx(x).margin(0.5f));
    }

    SECTION("Test at x = 3*width/4") {
        float x = 3.0f * width / 4.0f;
        float recovered = freqToX(xToFreq(x, width), width);
        REQUIRE(recovered == Approx(x).margin(0.5f));
    }

    SECTION("Test at x = width") {
        float x = width;
        float recovered = freqToX(xToFreq(x, width), width);
        REQUIRE(recovered == Approx(x).margin(0.1f));
    }
}

// ==============================================================================
// Test: Monotonicity (freqToX is strictly increasing)
// ==============================================================================
TEST_CASE("freqToX is monotonically increasing", "[spectrum][coordinate]") {
    const float width = 960.0f;

    float prevX = -1.0f;
    for (float freq = 20.0f; freq <= 20000.0f; freq *= 1.1f) {
        float x = freqToX(freq, width);
        REQUIRE(x > prevX);
        prevX = x;
    }
}

// ==============================================================================
// Test: Different Display Widths
// ==============================================================================
TEST_CASE("Coordinate conversion works with different display widths", "[spectrum][coordinate]") {
    SECTION("Width 800 pixels") {
        const float width = 800.0f;
        REQUIRE(freqToX(20.0f, width) == Approx(0.0f).margin(0.001f));
        REQUIRE(freqToX(20000.0f, width) == Approx(width).margin(0.001f));

        float recovered = xToFreq(freqToX(1000.0f, width), width);
        REQUIRE(recovered == Approx(1000.0f).margin(5.0f));
    }

    SECTION("Width 1200 pixels") {
        const float width = 1200.0f;
        REQUIRE(freqToX(20.0f, width) == Approx(0.0f).margin(0.001f));
        REQUIRE(freqToX(20000.0f, width) == Approx(width).margin(0.001f));

        float recovered = xToFreq(freqToX(1000.0f, width), width);
        REQUIRE(recovered == Approx(1000.0f).margin(5.0f));
    }
}
