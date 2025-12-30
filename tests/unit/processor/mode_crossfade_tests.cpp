// ==============================================================================
// Processor Tests: Mode Crossfade Logic
// ==============================================================================
// Constitution Principle XII: Test-First Development
// Tests written BEFORE implementation per spec 041-mode-switch-clicks
//
// Reference: specs/041-mode-switch-clicks/spec.md
// - FR-001: Mode switching produces no audible clicks
// - FR-002: Crossfade applied to prevent discontinuities
// - FR-003: Fade duration under 50ms
// - FR-006: Rapid switching produces no cumulative artifacts
// - SC-001: Zero audible clicks in any mode-to-mode switch
// - SC-002: Transition completes under 50ms
// - SC-005: Rapid switching (10/sec) stable
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/core/crossfade_utils.h"

#include <array>
#include <cmath>
#include <vector>

using Catch::Approx;
using namespace Iterum::DSP;

// =============================================================================
// CrossfadeState - Test Harness for Mode Crossfade Logic
// =============================================================================
// This struct encapsulates the crossfade state management that will be
// integrated into the Processor class. By isolating it here, we can
// thoroughly test the logic without VST3 SDK dependencies.
// =============================================================================

namespace {

constexpr float kCrossfadeTimeMs = 50.0f;

/// CrossfadeState manages the smooth transition between two modes
struct CrossfadeState {
    int currentMode = 0;      // Mode currently being transitioned TO
    int previousMode = 0;     // Mode being transitioned FROM
    float position = 1.0f;    // 0.0 = start of fade, 1.0 = complete
    float increment = 0.0f;   // Per-sample position increment
    bool active = false;      // True while crossfade is in progress

    /// Initialize crossfade timing for a given sample rate
    void prepare(double sampleRate) noexcept {
        increment = crossfadeIncrement(kCrossfadeTimeMs, sampleRate);
        position = 1.0f;  // Start in "complete" state
        active = false;
    }

    /// Check for mode change and start crossfade if needed
    /// @param newMode The requested mode
    /// @return True if a new crossfade was started
    bool checkModeChange(int newMode) noexcept {
        if (newMode != currentMode) {
            // Start crossfade from current mode to new mode
            previousMode = currentMode;
            currentMode = newMode;
            position = 0.0f;
            active = true;
            return true;
        }
        return false;
    }

    /// Advance crossfade position by one sample
    /// @return True if crossfade is still in progress
    bool advanceSample() noexcept {
        if (!active) return false;

        position += increment;
        if (position >= 1.0f) {
            position = 1.0f;
            active = false;
        }
        return active;
    }

    /// Get the number of samples remaining in the crossfade
    int samplesRemaining() const noexcept {
        if (!active) return 0;
        return static_cast<int>((1.0f - position) / increment + 0.5f);
    }

    /// Get crossfade gains for blending old and new mode outputs
    void getGains(float& fadeOut, float& fadeIn) const noexcept {
        equalPowerGains(position, fadeOut, fadeIn);
    }
};

// Test helper: Simulate processing a block of samples
int processBlock(CrossfadeState& state, size_t numSamples) {
    int samplesWhileActive = 0;
    for (size_t i = 0; i < numSamples; ++i) {
        if (state.active) {
            ++samplesWhileActive;
        }
        state.advanceSample();
    }
    return samplesWhileActive;
}

} // anonymous namespace

// =============================================================================
// T013: Crossfade State Initialization Tests
// =============================================================================

TEST_CASE("CrossfadeState initializes correctly", "[processor][crossfade][T013]") {
    CrossfadeState state;

    SECTION("default state has crossfade complete") {
        REQUIRE(state.position == 1.0f);
        REQUIRE(state.active == false);
        REQUIRE(state.currentMode == 0);
        REQUIRE(state.previousMode == 0);
    }

    SECTION("prepare() sets increment for sample rate") {
        state.prepare(44100.0);

        // 50ms at 44100Hz = 2205 samples
        // increment = 1/2205 ≈ 0.000453
        REQUIRE(state.increment == Approx(1.0f / 2205.0f).margin(1e-6f));
        REQUIRE(state.position == 1.0f);
        REQUIRE(state.active == false);
    }

    SECTION("prepare() works at different sample rates") {
        state.prepare(48000.0);
        REQUIRE(state.increment == Approx(1.0f / 2400.0f).margin(1e-6f));

        state.prepare(96000.0);
        REQUIRE(state.increment == Approx(1.0f / 4800.0f).margin(1e-6f));
    }
}

TEST_CASE("CrossfadeState mode change detection", "[processor][crossfade][T013]") {
    CrossfadeState state;
    state.prepare(44100.0);

    SECTION("changing mode starts crossfade") {
        REQUIRE(state.checkModeChange(1) == true);
        REQUIRE(state.active == true);
        REQUIRE(state.position == 0.0f);
        REQUIRE(state.currentMode == 1);
        REQUIRE(state.previousMode == 0);
    }

    SECTION("same mode does not start crossfade") {
        state.currentMode = 5;
        REQUIRE(state.checkModeChange(5) == false);
        REQUIRE(state.active == false);
    }

    SECTION("multiple mode changes update state correctly") {
        state.checkModeChange(3);
        REQUIRE(state.currentMode == 3);
        REQUIRE(state.previousMode == 0);

        // Process some samples
        for (int i = 0; i < 100; ++i) state.advanceSample();

        // Change again before crossfade completes
        state.checkModeChange(7);
        REQUIRE(state.currentMode == 7);
        REQUIRE(state.previousMode == 3);
        REQUIRE(state.position == 0.0f);  // Reset to start
    }
}

// =============================================================================
// T014: Crossfade Increment Calculation Tests
// =============================================================================

TEST_CASE("CrossfadeState increment produces correct timing", "[processor][crossfade][T014]") {
    CrossfadeState state;

    SECTION("increment matches crossfadeIncrement utility") {
        state.prepare(44100.0);
        float expected = crossfadeIncrement(kCrossfadeTimeMs, 44100.0);
        REQUIRE(state.increment == expected);
    }

    SECTION("increment scales with sample rate") {
        state.prepare(44100.0);
        float inc44 = state.increment;

        state.prepare(96000.0);
        float inc96 = state.increment;

        // Higher sample rate = smaller increment (more samples needed)
        REQUIRE(inc96 < inc44);
        // Ratio should match sample rate ratio
        REQUIRE(inc44 / inc96 == Approx(96000.0 / 44100.0).margin(0.001f));
    }
}

// =============================================================================
// T015: Crossfade Duration Tests (50ms = ~2205 samples at 44.1kHz)
// =============================================================================

TEST_CASE("CrossfadeState completes in expected samples", "[processor][crossfade][T015]") {
    CrossfadeState state;

    SECTION("completes in ~2205 samples at 44.1kHz") {
        state.prepare(44100.0);
        state.checkModeChange(1);

        int sampleCount = 0;
        while (state.active && sampleCount < 5000) {
            state.advanceSample();
            ++sampleCount;
        }

        // Should complete in 2205 ± 1 samples
        REQUIRE(sampleCount == Approx(2205).margin(1));
        REQUIRE(state.active == false);
        REQUIRE(state.position == 1.0f);
    }

    SECTION("completes in ~2400 samples at 48kHz") {
        state.prepare(48000.0);
        state.checkModeChange(1);

        int sampleCount = 0;
        while (state.active && sampleCount < 5000) {
            state.advanceSample();
            ++sampleCount;
        }

        REQUIRE(sampleCount == Approx(2400).margin(1));
    }

    SECTION("completes in ~4800 samples at 96kHz") {
        state.prepare(96000.0);
        state.checkModeChange(1);

        int sampleCount = 0;
        while (state.active && sampleCount < 10000) {
            state.advanceSample();
            ++sampleCount;
        }

        REQUIRE(sampleCount == Approx(4800).margin(1));
    }

    SECTION("samplesRemaining() reports accurate count") {
        state.prepare(44100.0);
        state.checkModeChange(1);

        int initialRemaining = state.samplesRemaining();
        REQUIRE(initialRemaining == Approx(2205).margin(1));

        // Process 1000 samples
        for (int i = 0; i < 1000; ++i) state.advanceSample();

        int remaining = state.samplesRemaining();
        REQUIRE(remaining == Approx(1205).margin(2));
    }
}

// =============================================================================
// T016: Rapid Mode Switching Stability Tests
// =============================================================================

TEST_CASE("CrossfadeState handles rapid mode switching", "[processor][crossfade][T016]") {
    CrossfadeState state;
    state.prepare(44100.0);

    SECTION("switching 10 times per second is stable") {
        // 44100 samples/sec ÷ 10 switches = 4410 samples between switches
        constexpr int samplesPerSwitch = 4410;
        constexpr int numSwitches = 10;

        for (int switchNum = 0; switchNum < numSwitches; ++switchNum) {
            int newMode = (switchNum % 11);  // Cycle through 11 modes
            state.checkModeChange(newMode);

            // Process samples until next switch
            for (int i = 0; i < samplesPerSwitch; ++i) {
                state.advanceSample();
            }

            // Crossfade should be complete (50ms < 100ms between switches)
            REQUIRE(state.active == false);
            REQUIRE(state.currentMode == newMode);
        }
    }

    SECTION("switching faster than crossfade time handles gracefully") {
        // Switch every 25ms (half of crossfade time)
        constexpr int samplesPerSwitch = 1103;  // ~25ms at 44.1kHz

        // Switch to mode 1
        state.checkModeChange(1);
        REQUIRE(state.active == true);
        REQUIRE(state.previousMode == 0);
        REQUIRE(state.currentMode == 1);

        // Process 25ms (crossfade not complete)
        for (int i = 0; i < samplesPerSwitch; ++i) {
            state.advanceSample();
        }
        REQUIRE(state.active == true);  // Still in progress
        float midPosition = state.position;
        REQUIRE(midPosition > 0.0f);
        REQUIRE(midPosition < 1.0f);

        // Switch to mode 2 before crossfade completes
        state.checkModeChange(2);
        REQUIRE(state.active == true);
        REQUIRE(state.position == 0.0f);  // Reset to start
        REQUIRE(state.previousMode == 1);  // Now fading FROM mode 1
        REQUIRE(state.currentMode == 2);   // TO mode 2
    }

    SECTION("rapid switching maintains valid gain values") {
        // Simulate rapid switching with gain checks
        for (int switchNum = 0; switchNum < 20; ++switchNum) {
            state.checkModeChange(switchNum % 11);

            // Process a few samples and check gains
            for (int i = 0; i < 100; ++i) {
                float fadeOut, fadeIn;
                state.getGains(fadeOut, fadeIn);

                // Gains must be in valid range [0, 1]
                // Use margin to handle IEEE 754 negative zero (-0.0f) edge case
                REQUIRE(fadeOut >= -1e-6f);
                REQUIRE(fadeOut <= 1.0f + 1e-6f);
                REQUIRE(fadeIn >= -1e-6f);
                REQUIRE(fadeIn <= 1.0f + 1e-6f);

                // Constant-power property must hold
                float totalPower = fadeOut * fadeOut + fadeIn * fadeIn;
                REQUIRE(totalPower == Approx(1.0f).margin(0.001f));

                state.advanceSample();
            }
        }
    }

    SECTION("switching every sample is stable (stress test)") {
        // Extreme case: switch mode every sample
        for (int sample = 0; sample < 1000; ++sample) {
            int newMode = (sample % 11);
            state.checkModeChange(newMode);
            state.advanceSample();

            // Should not crash or produce invalid state
            REQUIRE(state.currentMode == newMode);
            REQUIRE(state.position >= 0.0f);
            REQUIRE(state.position <= 1.0f);
        }
    }
}

// =============================================================================
// Crossfade Blending Tests (FR-002, SC-001)
// =============================================================================

TEST_CASE("CrossfadeState produces click-free blending", "[processor][crossfade]") {
    CrossfadeState state;
    state.prepare(44100.0);

    SECTION("gains transition smoothly from old to new mode") {
        state.checkModeChange(1);

        float prevFadeOut = 2.0f;
        float prevFadeIn = -1.0f;

        while (state.active) {
            float fadeOut, fadeIn;
            state.getGains(fadeOut, fadeIn);

            // fadeOut should decrease monotonically
            REQUIRE(fadeOut <= prevFadeOut);
            prevFadeOut = fadeOut;

            // fadeIn should increase monotonically
            REQUIRE(fadeIn >= prevFadeIn);
            prevFadeIn = fadeIn;

            state.advanceSample();
        }

        // At end, should be fully transitioned
        float fadeOut, fadeIn;
        state.getGains(fadeOut, fadeIn);
        REQUIRE(fadeOut == Approx(0.0f).margin(1e-6f));
        REQUIRE(fadeIn == Approx(1.0f));
    }

    SECTION("blending with simulated mode outputs produces no discontinuity") {
        // Simulate crossfade between two constant signals (worst case for clicks)
        constexpr float oldModeOutput = 1.0f;
        constexpr float newModeOutput = -1.0f;

        state.checkModeChange(1);

        float prevBlended = oldModeOutput;  // Before crossfade starts
        float maxJump = 0.0f;

        while (state.active) {
            float fadeOut, fadeIn;
            state.getGains(fadeOut, fadeIn);

            float blended = oldModeOutput * fadeOut + newModeOutput * fadeIn;
            float jump = std::abs(blended - prevBlended);
            maxJump = std::max(maxJump, jump);
            prevBlended = blended;

            state.advanceSample();
        }

        // Maximum per-sample jump should be small (no clicks)
        // With 2205 samples and a 2.0 range, max theoretical is ~0.001 per sample
        REQUIRE(maxJump < 0.01f);
    }
}

// =============================================================================
// Block Processing Tests
// =============================================================================

TEST_CASE("CrossfadeState works with block-based processing", "[processor][crossfade]") {
    CrossfadeState state;
    state.prepare(44100.0);

    SECTION("crossfade spans multiple blocks correctly") {
        constexpr size_t blockSize = 256;
        constexpr int expectedBlocks = (2205 + blockSize - 1) / blockSize;  // ~9 blocks

        state.checkModeChange(1);

        int blocksWithCrossfade = 0;
        while (state.active) {
            processBlock(state, blockSize);
            ++blocksWithCrossfade;
            if (blocksWithCrossfade > 20) break;  // Safety limit
        }

        REQUIRE(blocksWithCrossfade == expectedBlocks);
    }

    SECTION("crossfade completes mid-block correctly") {
        constexpr size_t blockSize = 512;

        state.prepare(44100.0);
        state.checkModeChange(1);

        // Process 4 blocks (2048 samples) - crossfade should complete during 5th block
        for (int i = 0; i < 4; ++i) {
            processBlock(state, blockSize);
            REQUIRE(state.active == true);
        }

        // 5th block - crossfade completes somewhere in the middle
        int activeInBlock = processBlock(state, blockSize);
        REQUIRE(state.active == false);
        REQUIRE(activeInBlock > 0);
        REQUIRE(activeInBlock < static_cast<int>(blockSize));
    }
}

// =============================================================================
// All 11 Modes Support Tests (FR-008)
// =============================================================================

TEST_CASE("CrossfadeState supports all 11 delay modes", "[processor][crossfade]") {
    CrossfadeState state;
    state.prepare(44100.0);

    // Delay mode enum values (from parameters)
    enum DelayMode {
        Granular = 0,
        Spectral = 1,
        Shimmer = 2,
        Tape = 3,
        BBD = 4,
        Digital = 5,
        PingPong = 6,
        Reverse = 7,
        MultiTap = 8,
        Freeze = 9,
        Ducking = 10
    };

    SECTION("all 11 modes can be crossfaded to/from") {
        for (int fromMode = 0; fromMode < 11; ++fromMode) {
            state.currentMode = fromMode;
            state.previousMode = fromMode;
            state.position = 1.0f;
            state.active = false;

            for (int toMode = 0; toMode < 11; ++toMode) {
                if (toMode == fromMode) continue;

                // Start crossfade
                REQUIRE(state.checkModeChange(toMode) == true);
                REQUIRE(state.active == true);
                REQUIRE(state.currentMode == toMode);
                REQUIRE(state.previousMode == fromMode);

                // Complete crossfade
                while (state.active) {
                    state.advanceSample();
                }

                REQUIRE(state.active == false);
                REQUIRE(state.position == 1.0f);

                // Reset for next test
                state.currentMode = fromMode;
                state.position = 1.0f;
            }
        }
    }
}
