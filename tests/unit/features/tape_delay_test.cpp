// ==============================================================================
// Layer 4: User Feature - TapeDelay Tests
// ==============================================================================
// Tests for the TapeDelay user feature (classic tape echo emulation).
// Follows test-first development per Constitution Principle XII.
//
// Feature: 024-tape-delay
// Layer: 4 (User Feature)
// Reference: specs/024-tape-delay/spec.md
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/features/tape_delay.h"

#include <array>
#include <cmath>

using Catch::Approx;
using namespace Iterum::DSP;

// =============================================================================
// Phase 2: Foundational Component Tests
// =============================================================================

TEST_CASE("TapeHead default construction", "[features][tape-delay][tape-head]") {
    TapeHead head;

    SECTION("default values") {
        REQUIRE(head.ratio == Approx(1.0f));
        REQUIRE(head.levelDb == Approx(0.0f));
        REQUIRE(head.pan == Approx(0.0f));
        REQUIRE(head.enabled == true);
    }
}

TEST_CASE("TapeHead configurable construction", "[features][tape-delay][tape-head]") {
    SECTION("head at 1.5x ratio") {
        TapeHead head{1.5f, -6.0f, -50.0f, true};
        REQUIRE(head.ratio == Approx(1.5f));
        REQUIRE(head.levelDb == Approx(-6.0f));
        REQUIRE(head.pan == Approx(-50.0f));
        REQUIRE(head.enabled == true);
    }

    SECTION("disabled head") {
        TapeHead head{2.0f, 0.0f, 50.0f, false};
        REQUIRE(head.enabled == false);
    }
}

TEST_CASE("MotorController basic functionality", "[features][tape-delay][motor-controller]") {
    MotorController motor;

    SECTION("default state") {
        // Not prepared yet, should have reasonable defaults
        REQUIRE(motor.getCurrentDelayMs() >= 0.0f);
    }

    SECTION("prepare initializes state") {
        motor.prepare(44100.0f, 512);
        REQUIRE(motor.getCurrentDelayMs() >= 0.0f);
    }
}

TEST_CASE("MotorController delay time management", "[features][tape-delay][motor-controller]") {
    MotorController motor;
    motor.prepare(44100.0f, 512);

    SECTION("setTargetDelayMs sets target") {
        motor.setTargetDelayMs(500.0f);
        REQUIRE(motor.getTargetDelayMs() == Approx(500.0f));
    }

    SECTION("delay smooths over time (inertia)") {
        motor.setTargetDelayMs(0.0f);
        motor.snapToTarget();

        motor.setTargetDelayMs(500.0f);

        // First sample should not be at target (inertia)
        float firstDelay = motor.process();
        REQUIRE(firstDelay < 500.0f);
        REQUIRE(firstDelay > 0.0f);
    }

    SECTION("snapToTarget bypasses inertia") {
        motor.setTargetDelayMs(500.0f);
        motor.snapToTarget();
        REQUIRE(motor.getCurrentDelayMs() == Approx(500.0f));
    }
}

TEST_CASE("MotorController inertia timing", "[features][tape-delay][motor-controller]") {
    MotorController motor;
    motor.prepare(44100.0f, 512);

    SECTION("default inertia time is tape-realistic (200-500ms)") {
        motor.setTargetDelayMs(0.0f);
        motor.snapToTarget();

        motor.setTargetDelayMs(1000.0f);

        // Process for 200ms worth of samples
        const size_t samples200ms = static_cast<size_t>(44100.0f * 0.2f);
        float delay = 0.0f;
        for (size_t i = 0; i < samples200ms; ++i) {
            delay = motor.process();
        }

        // Should be significantly toward target but not there yet
        REQUIRE(delay > 500.0f);  // Past halfway
        REQUIRE(delay < 990.0f);  // Not at target yet
    }

    SECTION("setInertiaTimeMs changes transition speed") {
        motor.setInertiaTimeMs(100.0f);  // Fast inertia
        motor.setTargetDelayMs(0.0f);
        motor.snapToTarget();

        motor.setTargetDelayMs(1000.0f);

        // Process for 100ms
        const size_t samples100ms = static_cast<size_t>(44100.0f * 0.1f);
        float delay = 0.0f;
        for (size_t i = 0; i < samples100ms; ++i) {
            delay = motor.process();
        }

        // With 100ms inertia, should be near target after 100ms
        REQUIRE(delay > 900.0f);
    }
}

TEST_CASE("MotorController reset", "[features][tape-delay][motor-controller]") {
    MotorController motor;
    motor.prepare(44100.0f, 512);

    motor.setTargetDelayMs(500.0f);
    motor.snapToTarget();

    motor.reset();

    // After reset, current should snap to target
    REQUIRE(motor.getCurrentDelayMs() == Approx(motor.getTargetDelayMs()));
}

// =============================================================================
// Phase 3: TapeDelay Construction and Lifecycle Tests
// =============================================================================

TEST_CASE("TapeDelay construction", "[features][tape-delay][lifecycle]") {
    TapeDelay delay;

    SECTION("default construction succeeds") {
        REQUIRE_FALSE(delay.isPrepared());
    }

    SECTION("constants are correct") {
        REQUIRE(TapeDelay::kNumHeads == 3);
        REQUIRE(TapeDelay::kMinDelayMs == Approx(20.0f));
        REQUIRE(TapeDelay::kMaxDelayMs == Approx(2000.0f));
        REQUIRE(TapeDelay::kHeadRatio1 == Approx(1.0f));
        REQUIRE(TapeDelay::kHeadRatio2 == Approx(1.5f));
        REQUIRE(TapeDelay::kHeadRatio3 == Approx(2.0f));
    }
}

TEST_CASE("TapeDelay prepare", "[features][tape-delay][lifecycle]") {
    TapeDelay delay;

    SECTION("prepare marks as prepared") {
        delay.prepare(44100.0, 512, 2000.0f);
        REQUIRE(delay.isPrepared());
    }

    SECTION("prepare accepts various sample rates") {
        delay.prepare(48000.0, 256, 2000.0f);
        REQUIRE(delay.isPrepared());
    }
}

TEST_CASE("TapeDelay reset", "[features][tape-delay][lifecycle]") {
    TapeDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);

    delay.setMotorSpeed(500.0f);
    delay.setFeedback(0.5f);

    SECTION("reset clears delay state") {
        delay.reset();
        // After reset, isPrepared should still be true
        REQUIRE(delay.isPrepared());
    }
}

// =============================================================================
// Phase 3: Motor Speed (Delay Time) Tests
// =============================================================================

TEST_CASE("TapeDelay motor speed control", "[features][tape-delay][motor-speed]") {
    TapeDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);

    SECTION("setMotorSpeed sets target delay") {
        delay.setMotorSpeed(500.0f);
        REQUIRE(delay.getTargetDelayMs() == Approx(500.0f));
    }

    SECTION("delay time clamped to valid range") {
        delay.setMotorSpeed(10.0f);  // Below minimum
        REQUIRE(delay.getTargetDelayMs() >= TapeDelay::kMinDelayMs);

        delay.setMotorSpeed(5000.0f);  // Above maximum
        REQUIRE(delay.getTargetDelayMs() <= TapeDelay::kMaxDelayMs);
    }
}

// =============================================================================
// Phase 3: Feedback Tests
// =============================================================================

TEST_CASE("TapeDelay feedback control", "[features][tape-delay][feedback]") {
    TapeDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);

    SECTION("setFeedback stores value") {
        delay.setFeedback(0.5f);
        REQUIRE(delay.getFeedback() == Approx(0.5f));
    }

    SECTION("feedback clamped to valid range") {
        delay.setFeedback(-0.1f);
        REQUIRE(delay.getFeedback() >= 0.0f);

        delay.setFeedback(1.5f);
        REQUIRE(delay.getFeedback() <= 1.2f);
    }
}

// =============================================================================
// Phase 3: Mix Tests
// =============================================================================

TEST_CASE("TapeDelay mix control", "[features][tape-delay][mix]") {
    TapeDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);

    SECTION("setMix stores value") {
        delay.setMix(0.5f);
        REQUIRE(delay.getMix() == Approx(0.5f));
    }

    SECTION("mix clamped to 0-1 range") {
        delay.setMix(-0.1f);
        REQUIRE(delay.getMix() >= 0.0f);

        delay.setMix(1.1f);
        REQUIRE(delay.getMix() <= 1.0f);
    }
}

// =============================================================================
// Phase 3: Output Level Tests
// =============================================================================

TEST_CASE("TapeDelay output level control", "[features][tape-delay][output-level]") {
    TapeDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);

    SECTION("setOutputLevel stores value") {
        delay.setOutputLevel(-6.0f);
        REQUIRE(delay.getOutputLevel() == Approx(-6.0f));
    }

    SECTION("output level clamped to valid range") {
        delay.setOutputLevel(-100.0f);
        REQUIRE(delay.getOutputLevel() >= -96.0f);

        delay.setOutputLevel(20.0f);
        REQUIRE(delay.getOutputLevel() <= 12.0f);
    }
}

// =============================================================================
// Phase 3: Basic Processing Tests
// =============================================================================

TEST_CASE("TapeDelay basic processing", "[features][tape-delay][processing]") {
    TapeDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);

    std::array<float, 512> left{};
    std::array<float, 512> right{};

    SECTION("process with silence produces silence initially") {
        delay.process(left.data(), right.data(), 512);

        // With no input and no delay built up, output should be near zero
        float maxOutput = 0.0f;
        for (size_t i = 0; i < 512; ++i) {
            maxOutput = std::max(maxOutput, std::abs(left[i]));
            maxOutput = std::max(maxOutput, std::abs(right[i]));
        }
        REQUIRE(maxOutput < 0.001f);
    }

    SECTION("process handles impulse") {
        left[0] = 1.0f;
        right[0] = 1.0f;

        delay.setMotorSpeed(100.0f);  // 100ms delay
        delay.setFeedback(0.5f);
        delay.setMix(1.0f);  // Full wet

        delay.process(left.data(), right.data(), 512);

        // Impulse should appear after delay time
        // At 44100Hz, 100ms = 4410 samples, but we only have 512
        // So no echo should appear yet in this block
        // (This is just a basic smoke test)
        REQUIRE_FALSE(std::isnan(left[511]));
        REQUIRE_FALSE(std::isnan(right[511]));
    }
}

TEST_CASE("TapeDelay mono processing", "[features][tape-delay][processing]") {
    TapeDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);

    std::array<float, 512> buffer{};

    SECTION("mono process handles silence") {
        delay.process(buffer.data(), 512);

        for (size_t i = 0; i < 512; ++i) {
            REQUIRE_FALSE(std::isnan(buffer[i]));
        }
    }
}

// =============================================================================
// Phase 4: Wear (Wow/Flutter) Tests
// =============================================================================

TEST_CASE("TapeDelay wear control", "[features][tape-delay][wear]") {
    TapeDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);

    SECTION("setWear stores value") {
        delay.setWear(0.5f);
        REQUIRE(delay.getWear() == Approx(0.5f));
    }

    SECTION("wear clamped to 0-1 range") {
        delay.setWear(-0.1f);
        REQUIRE(delay.getWear() >= 0.0f);

        delay.setWear(1.5f);
        REQUIRE(delay.getWear() <= 1.0f);
    }
}

// =============================================================================
// Phase 5: Saturation Tests
// =============================================================================

TEST_CASE("TapeDelay saturation control", "[features][tape-delay][saturation]") {
    TapeDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);

    SECTION("setSaturation stores value") {
        delay.setSaturation(0.5f);
        REQUIRE(delay.getSaturation() == Approx(0.5f));
    }

    SECTION("saturation clamped to 0-1 range") {
        delay.setSaturation(-0.1f);
        REQUIRE(delay.getSaturation() >= 0.0f);

        delay.setSaturation(1.5f);
        REQUIRE(delay.getSaturation() <= 1.0f);
    }
}

// =============================================================================
// Phase 6: Echo Heads (Multi-Tap) Tests
// =============================================================================

TEST_CASE("TapeDelay head control", "[features][tape-delay][heads]") {
    TapeDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);

    SECTION("heads enabled by default") {
        for (size_t i = 0; i < TapeDelay::kNumHeads; ++i) {
            REQUIRE(delay.isHeadEnabled(i));
        }
    }

    SECTION("setHeadEnabled toggles head") {
        delay.setHeadEnabled(0, false);
        REQUIRE_FALSE(delay.isHeadEnabled(0));

        delay.setHeadEnabled(0, true);
        REQUIRE(delay.isHeadEnabled(0));
    }

    SECTION("setHeadLevel stores value") {
        delay.setHeadLevel(0, -6.0f);
        auto head = delay.getHead(0);
        REQUIRE(head.levelDb == Approx(-6.0f));
    }

    SECTION("setHeadPan stores value") {
        delay.setHeadPan(1, 50.0f);
        auto head = delay.getHead(1);
        REQUIRE(head.pan == Approx(50.0f));
    }

    SECTION("head ratios are fixed") {
        auto head0 = delay.getHead(0);
        auto head1 = delay.getHead(1);
        auto head2 = delay.getHead(2);

        REQUIRE(head0.ratio == Approx(TapeDelay::kHeadRatio1));
        REQUIRE(head1.ratio == Approx(TapeDelay::kHeadRatio2));
        REQUIRE(head2.ratio == Approx(TapeDelay::kHeadRatio3));
    }

    SECTION("out-of-range head index handled gracefully") {
        // Should not crash
        delay.setHeadEnabled(10, true);
        delay.setHeadLevel(10, 0.0f);
        delay.setHeadPan(10, 0.0f);

        // Querying out of range returns safe defaults
        REQUIRE_FALSE(delay.isHeadEnabled(10));
    }

    SECTION("getActiveHeadCount returns correct count") {
        delay.setHeadEnabled(0, true);
        delay.setHeadEnabled(1, true);
        delay.setHeadEnabled(2, false);
        REQUIRE(delay.getActiveHeadCount() == 2);
    }
}

// =============================================================================
// Phase 7: Age/Degradation Tests
// =============================================================================

TEST_CASE("TapeDelay age control", "[features][tape-delay][age]") {
    TapeDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);

    SECTION("setAge stores value") {
        delay.setAge(0.5f);
        REQUIRE(delay.getAge() == Approx(0.5f));
    }

    SECTION("age clamped to 0-1 range") {
        delay.setAge(-0.1f);
        REQUIRE(delay.getAge() >= 0.0f);

        delay.setAge(1.5f);
        REQUIRE(delay.getAge() <= 1.0f);
    }
}

// =============================================================================
// Phase 8: Motor Inertia Tests
// =============================================================================

TEST_CASE("TapeDelay motor inertia", "[features][tape-delay][motor-inertia]") {
    TapeDelay delay;
    delay.prepare(44100.0, 512, 2000.0f);

    SECTION("setMotorInertia stores value") {
        delay.setMotorInertia(300.0f);
        // No getter, but verify it doesn't crash
    }

    SECTION("isTransitioning detects motor changes") {
        delay.setMotorSpeed(200.0f);
        delay.reset();  // Snap to target

        delay.setMotorSpeed(500.0f);
        REQUIRE(delay.isTransitioning());
    }
}
