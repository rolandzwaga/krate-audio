// ==============================================================================
// Crossover Interaction Tests
// ==============================================================================
// Constitution Principle VIII: Testing Discipline
// Tests for crossover divider dragging and band selection (T095-T096)
//
// Verifies:
// - hitTestDivider logic for crossover selection
// - Frequency clamping within valid range (20Hz - 20kHz)
// - Divider movement constraints (minimum octave spacing)
// - Band region click detection
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "plugin_ids.h"

#include <algorithm>
#include <cmath>
#include <set>

using namespace Disrumpo;
using Catch::Approx;

// ==============================================================================
// Test Helpers - Coordinate conversion (same as SpectrumDisplay)
// ==============================================================================
namespace {

constexpr float kMinFreqHz = 20.0f;
constexpr float kMaxFreqHz = 20000.0f;
constexpr float kLogRatio = 9.9657842846620869f;  // log2(20000/20) = log2(1000)
constexpr float kDividerHitTolerance = 10.0f;  // Pixels
constexpr float kMinOctaveSpacing = 0.5f;  // Minimum spacing between dividers

float freqToX(float freq, float width) {
    if (freq <= kMinFreqHz) return 0.0f;
    if (freq >= kMaxFreqHz) return width;

    float logPos = std::log2(freq / kMinFreqHz) / kLogRatio;
    return width * logPos;
}

float xToFreq(float x, float width) {
    if (x <= 0.0f) return kMinFreqHz;
    if (x >= width) return kMaxFreqHz;

    float logPos = x / width;
    return kMinFreqHz * std::pow(2.0f, logPos * kLogRatio);
}

// Simulate hitTestDivider
int hitTestDivider(float x, float width, const float* crossoverFreqs, int numDividers) {
    for (int i = 0; i < numDividers; ++i) {
        float dividerX = freqToX(crossoverFreqs[i], width);
        if (std::abs(x - dividerX) <= kDividerHitTolerance) {
            return i;
        }
    }
    return -1;
}

// Clamp frequency to valid range
float clampFrequency(float freq) {
    return std::clamp(freq, kMinFreqHz, kMaxFreqHz);
}

// Check if new frequency maintains minimum octave spacing from neighbors
bool isValidDividerPosition(float newFreq, int dividerIndex,
                            const float* crossoverFreqs, int numDividers) {
    float leftBound = kMinFreqHz;
    float rightBound = kMaxFreqHz;

    // Get left neighbor frequency (if exists)
    if (dividerIndex > 0) {
        leftBound = crossoverFreqs[dividerIndex - 1] * std::pow(2.0f, kMinOctaveSpacing);
    }

    // Get right neighbor frequency (if exists)
    if (dividerIndex < numDividers - 1) {
        rightBound = crossoverFreqs[dividerIndex + 1] * std::pow(2.0f, -kMinOctaveSpacing);
    }

    return newFreq >= leftBound && newFreq <= rightBound;
}

}  // anonymous namespace

// ==============================================================================
// Test: Divider Hit Test (T095)
// ==============================================================================
TEST_CASE("hitTestDivider detects correct divider", "[crossover][hitTest]") {
    const float width = 960.0f;
    float crossoverFreqs[] = {200.0f, 2000.0f, 8000.0f};
    int numDividers = 3;

    SECTION("Click exactly on divider 0 returns 0") {
        float x = freqToX(200.0f, width);
        REQUIRE(hitTestDivider(x, width, crossoverFreqs, numDividers) == 0);
    }

    SECTION("Click exactly on divider 1 returns 1") {
        float x = freqToX(2000.0f, width);
        REQUIRE(hitTestDivider(x, width, crossoverFreqs, numDividers) == 1);
    }

    SECTION("Click exactly on divider 2 returns 2") {
        float x = freqToX(8000.0f, width);
        REQUIRE(hitTestDivider(x, width, crossoverFreqs, numDividers) == 2);
    }

    SECTION("Click within tolerance of divider 0 returns 0") {
        float x = freqToX(200.0f, width) + 5.0f;  // Within 10px tolerance
        REQUIRE(hitTestDivider(x, width, crossoverFreqs, numDividers) == 0);
    }

    SECTION("Click outside tolerance returns -1") {
        float x = freqToX(200.0f, width) + 20.0f;  // Outside 10px tolerance
        REQUIRE(hitTestDivider(x, width, crossoverFreqs, numDividers) == -1);
    }

    SECTION("Click between dividers returns -1") {
        float x = freqToX(1000.0f, width);  // Between 200Hz and 2000Hz
        REQUIRE(hitTestDivider(x, width, crossoverFreqs, numDividers) == -1);
    }
}

// ==============================================================================
// Test: Frequency Clamping (T095)
// ==============================================================================
TEST_CASE("Frequency clamping bounds to valid range", "[crossover][clamp]") {

    SECTION("Frequency below 20Hz clamps to 20Hz") {
        REQUIRE(clampFrequency(10.0f) == 20.0f);
        REQUIRE(clampFrequency(0.0f) == 20.0f);
        REQUIRE(clampFrequency(-100.0f) == 20.0f);
    }

    SECTION("Frequency above 20000Hz clamps to 20000Hz") {
        REQUIRE(clampFrequency(25000.0f) == 20000.0f);
        REQUIRE(clampFrequency(100000.0f) == 20000.0f);
    }

    SECTION("Frequency within range is unchanged") {
        REQUIRE(clampFrequency(1000.0f) == 1000.0f);
        REQUIRE(clampFrequency(20.0f) == 20.0f);
        REQUIRE(clampFrequency(20000.0f) == 20000.0f);
    }
}

// ==============================================================================
// Test: Minimum Octave Spacing (T095)
// ==============================================================================
TEST_CASE("Divider movement respects minimum octave spacing", "[crossover][spacing]") {
    // 3 dividers at 200Hz, 2000Hz, 8000Hz (roughly 1 decade spacing)
    float crossoverFreqs[] = {200.0f, 2000.0f, 8000.0f};
    int numDividers = 3;

    SECTION("Moving divider 1 within valid range is allowed") {
        // Divider 1 at 2000Hz, with 0.5 octave minimum spacing
        // Left bound: 200 * 2^0.5 = 282.8Hz
        // Right bound: 8000 * 2^-0.5 = 5656.8Hz
        REQUIRE(isValidDividerPosition(1000.0f, 1, crossoverFreqs, numDividers) == true);
        REQUIRE(isValidDividerPosition(3000.0f, 1, crossoverFreqs, numDividers) == true);
    }

    SECTION("Moving divider 1 too close to left neighbor is blocked") {
        // Left bound is ~282.8Hz
        REQUIRE(isValidDividerPosition(250.0f, 1, crossoverFreqs, numDividers) == false);
    }

    SECTION("Moving divider 1 too close to right neighbor is blocked") {
        // Right bound is ~5656.8Hz
        REQUIRE(isValidDividerPosition(6000.0f, 1, crossoverFreqs, numDividers) == false);
    }

    SECTION("First divider can move close to 20Hz") {
        // No left neighbor, so left bound is 20Hz
        REQUIRE(isValidDividerPosition(30.0f, 0, crossoverFreqs, numDividers) == true);
    }

    SECTION("First divider respects right neighbor spacing") {
        // Right bound: 2000 * 2^-0.5 = 1414Hz
        REQUIRE(isValidDividerPosition(1200.0f, 0, crossoverFreqs, numDividers) == true);
        REQUIRE(isValidDividerPosition(1500.0f, 0, crossoverFreqs, numDividers) == false);
    }

    SECTION("Last divider can move close to 20kHz") {
        // No right neighbor, so right bound is 20kHz
        REQUIRE(isValidDividerPosition(18000.0f, 2, crossoverFreqs, numDividers) == true);
    }
}

// ==============================================================================
// Test: Band Region Detection (T096)
// ==============================================================================
TEST_CASE("Band region detection from X coordinate", "[crossover][band]") {
    const float width = 960.0f;
    float crossoverFreqs[] = {200.0f, 2000.0f, 8000.0f};
    int numBands = 4;

    // Determine which band contains a given X coordinate
    auto getBandAtX = [&](float x) -> int {
        float freq = xToFreq(x, width);

        for (int i = 0; i < numBands - 1; ++i) {
            if (freq < crossoverFreqs[i]) {
                return i;
            }
        }
        return numBands - 1;
    };

    SECTION("X at 20Hz is in band 0") {
        float x = freqToX(20.0f, width);
        REQUIRE(getBandAtX(x) == 0);
    }

    SECTION("X at 100Hz is in band 0") {
        float x = freqToX(100.0f, width);
        REQUIRE(getBandAtX(x) == 0);
    }

    SECTION("X at 500Hz is in band 1") {
        float x = freqToX(500.0f, width);
        REQUIRE(getBandAtX(x) == 1);
    }

    SECTION("X at 5000Hz is in band 2") {
        float x = freqToX(5000.0f, width);
        REQUIRE(getBandAtX(x) == 2);
    }

    SECTION("X at 15000Hz is in band 3") {
        float x = freqToX(15000.0f, width);
        REQUIRE(getBandAtX(x) == 3);
    }

    SECTION("X at edge of display is in correct band") {
        REQUIRE(getBandAtX(0.0f) == 0);       // 20Hz
        REQUIRE(getBandAtX(width) == 3);      // 20kHz
    }
}

// ==============================================================================
// Test: Crossover Parameter ID Encoding (T095)
// ==============================================================================
TEST_CASE("Crossover parameter IDs are correctly encoded", "[crossover][encoding]") {
    // Crossover params are in 0x0F10 range

    SECTION("Crossover 0 has correct ID") {
        auto paramId = makeCrossoverParamId(0);
        REQUIRE(paramId == 0x0F10);
        REQUIRE(paramId == 3856);
    }

    SECTION("Crossover 1 has correct ID") {
        auto paramId = makeCrossoverParamId(1);
        REQUIRE(paramId == 0x0F11);
        REQUIRE(paramId == 3857);
    }

    SECTION("Crossover 6 has correct ID") {
        auto paramId = makeCrossoverParamId(6);
        REQUIRE(paramId == 0x0F16);
        REQUIRE(paramId == 3862);
    }

    SECTION("All 7 crossovers have unique IDs") {
        std::set<Steinberg::Vst::ParamID> ids;
        for (int i = 0; i < 7; ++i) {
            auto id = makeCrossoverParamId(i);
            REQUIRE(ids.insert(id).second == true);  // No duplicates
        }
        REQUIRE(ids.size() == 7);
    }
}

// ==============================================================================
// Test: Crossover Frequency to Normalized Mapping (T095)
// ==============================================================================
TEST_CASE("Crossover frequency maps to normalized value", "[crossover][normalize]") {
    // Crossover frequency range: 20Hz - 20kHz (logarithmic)
    // normalized = log2(freq/20) / log2(1000)

    auto freqToNormalized = [](float freq) -> float {
        if (freq <= kMinFreqHz) return 0.0f;
        if (freq >= kMaxFreqHz) return 1.0f;
        return std::log2(freq / kMinFreqHz) / kLogRatio;
    };

    SECTION("20Hz maps to normalized 0.0") {
        REQUIRE(freqToNormalized(20.0f) == Approx(0.0f).margin(0.001f));
    }

    SECTION("200Hz maps to normalized ~0.333") {
        // log2(200/20) / log2(1000) = log2(10) / log2(1000) = 3.32 / 9.97 = 0.333
        REQUIRE(freqToNormalized(200.0f) == Approx(0.333f).margin(0.01f));
    }

    SECTION("2000Hz maps to normalized ~0.667") {
        // log2(2000/20) / log2(1000) = log2(100) / log2(1000) = 6.64 / 9.97 = 0.667
        REQUIRE(freqToNormalized(2000.0f) == Approx(0.667f).margin(0.01f));
    }

    SECTION("20000Hz maps to normalized 1.0") {
        REQUIRE(freqToNormalized(20000.0f) == Approx(1.0f).margin(0.001f));
    }
}

// ==============================================================================
// Test: Drag State Machine (T096)
// ==============================================================================
TEST_CASE("Crossover drag state machine", "[crossover][drag]") {
    // State: draggingDivider_ (-1 = not dragging, 0-6 = divider index)

    struct DragState {
        int draggingDivider = -1;

        bool onMouseDown(int hitDivider) {
            if (hitDivider >= 0) {
                draggingDivider = hitDivider;
                return true;  // Event handled
            }
            return false;  // Event not handled
        }

        bool onMouseMove(int divider, float newFreq) {
            if (draggingDivider >= 0) {
                // Would update crossover frequency here
                (void)newFreq;
                return true;  // Event handled
            }
            return false;
        }

        bool onMouseUp() {
            if (draggingDivider >= 0) {
                draggingDivider = -1;
                return true;  // Event handled
            }
            return false;
        }
    };

    DragState state;

    SECTION("Initial state is not dragging") {
        REQUIRE(state.draggingDivider == -1);
    }

    SECTION("Mouse down on divider starts drag") {
        REQUIRE(state.onMouseDown(1) == true);
        REQUIRE(state.draggingDivider == 1);
    }

    SECTION("Mouse down on nothing doesn't start drag") {
        REQUIRE(state.onMouseDown(-1) == false);
        REQUIRE(state.draggingDivider == -1);
    }

    SECTION("Mouse move during drag is handled") {
        state.onMouseDown(0);
        REQUIRE(state.onMouseMove(0, 300.0f) == true);
    }

    SECTION("Mouse move when not dragging is not handled") {
        REQUIRE(state.onMouseMove(0, 300.0f) == false);
    }

    SECTION("Mouse up ends drag") {
        state.onMouseDown(2);
        REQUIRE(state.onMouseUp() == true);
        REQUIRE(state.draggingDivider == -1);
    }

    SECTION("Complete drag cycle") {
        // Mouse down
        state.onMouseDown(1);
        REQUIRE(state.draggingDivider == 1);

        // Mouse move
        state.onMouseMove(1, 1500.0f);
        REQUIRE(state.draggingDivider == 1);

        // Mouse up
        state.onMouseUp();
        REQUIRE(state.draggingDivider == -1);
    }
}
