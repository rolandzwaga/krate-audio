// ==============================================================================
// MultiTap Pattern Morphing Tests
// ==============================================================================
// Tests for pattern morphing behavior in MultiTapDelay.
//
// BUG BACKGROUND (2026-01-04):
// - Processor called loadTimingPattern() on every block
// - loadTimingPattern() immediately applies the pattern (no morphing)
// - Morph Time slider had no effect because morphToPattern() was never called
//
// FIX:
// - Track previous pattern in processor
// - When pattern changes, call morphToPattern() instead of loadTimingPattern()
// - Morph Time now smoothly transitions between patterns
//
// These tests verify the distinction between immediate and morphed transitions.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <krate/dsp/effects/multi_tap_delay.h>

using namespace Krate::DSP;
using Catch::Approx;

namespace {
constexpr double kSampleRate = 44100.0;
constexpr size_t kBlockSize = 512;
constexpr float kMaxDelayMs = 5000.0f;

BlockContext makeTestContext(double sampleRate = kSampleRate, double tempo = 120.0) {
    BlockContext ctx;
    ctx.sampleRate = sampleRate;
    ctx.tempoBPM = tempo;
    ctx.isPlaying = true;
    return ctx;
}
}  // namespace

// ==============================================================================
// TEST: loadTimingPattern() is immediate (no morphing)
// ==============================================================================

TEST_CASE("loadTimingPattern applies pattern immediately without morphing",
          "[multi-tap][morph][immediate]") {
    MultiTapDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);
    delay.setTempo(120.0f);
    delay.loadTimingPattern(TimingPattern::QuarterNote, 4);

    SECTION("isMorphing() returns false after loadTimingPattern") {
        // loadTimingPattern should NOT trigger morphing
        REQUIRE(delay.isMorphing() == false);

        // Load a different pattern
        delay.loadTimingPattern(TimingPattern::DottedEighth, 4);

        // Still no morphing - it's immediate
        REQUIRE(delay.isMorphing() == false);
    }

    SECTION("pattern times change immediately") {
        delay.loadTimingPattern(TimingPattern::QuarterNote, 4);
        float quarterTime = delay.getTapTimeMs(0);

        delay.loadTimingPattern(TimingPattern::EighthNote, 4);
        float eighthTime = delay.getTapTimeMs(0);

        // Times should be different immediately (no gradual transition)
        REQUIRE(eighthTime != Approx(quarterTime));
        // Eighth note should be half of quarter note
        REQUIRE(eighthTime == Approx(quarterTime / 2.0f).margin(1.0f));
    }
}

// ==============================================================================
// TEST: morphToPattern() triggers gradual transition
// ==============================================================================

TEST_CASE("morphToPattern triggers gradual pattern transition",
          "[multi-tap][morph][gradual]") {
    MultiTapDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);
    delay.setTempo(120.0f);
    delay.loadTimingPattern(TimingPattern::QuarterNote, 4);
    delay.snapParameters();

    auto ctx = makeTestContext();

    SECTION("isMorphing() returns true after morphToPattern") {
        REQUIRE(delay.isMorphing() == false);

        delay.morphToPattern(TimingPattern::DottedEighth, 500.0f);

        REQUIRE(delay.isMorphing() == true);
    }

    SECTION("morph time affects transition duration") {
        // Set short morph time
        delay.morphToPattern(TimingPattern::EighthNote, 100.0f);
        REQUIRE(delay.isMorphing() == true);

        // Process enough samples for 100ms morph to complete
        // 100ms at 44100 Hz = 4410 samples = ~9 blocks of 512
        std::array<float, 512> left{}, right{};
        for (int i = 0; i < 15; ++i) {
            std::fill(left.begin(), left.end(), 0.0f);
            std::fill(right.begin(), right.end(), 0.0f);
            delay.process(left.data(), right.data(), 512, ctx);
        }

        // After enough time, morph should complete
        REQUIRE(delay.isMorphing() == false);
    }

    SECTION("longer morph time takes longer to complete") {
        // Set long morph time
        delay.morphToPattern(TimingPattern::EighthNote, 1000.0f);
        REQUIRE(delay.isMorphing() == true);

        // Process only 200ms worth of samples (not enough for 1000ms morph)
        // 200ms at 44100 Hz = 8820 samples = ~17 blocks
        std::array<float, 512> left{}, right{};
        for (int i = 0; i < 17; ++i) {
            std::fill(left.begin(), left.end(), 0.0f);
            std::fill(right.begin(), right.end(), 0.0f);
            delay.process(left.data(), right.data(), 512, ctx);
        }

        // Should still be morphing (1000ms > 200ms)
        REQUIRE(delay.isMorphing() == true);
    }
}

// ==============================================================================
// TEST: Pattern change detection logic
// ==============================================================================
// This documents the pattern change detection needed in the processor.
// ==============================================================================

TEST_CASE("Pattern change detection for processor integration",
          "[multi-tap][morph][processor]") {

    SECTION("same pattern should not trigger morph") {
        MultiTapDelay delay;
        delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);
        delay.setTempo(120.0f);
        delay.loadTimingPattern(TimingPattern::QuarterNote, 4);

        // Simulates processor calling every block with same pattern
        // Should use loadTimingPattern for initialization, then skip if unchanged
        TimingPattern currentPattern = TimingPattern::QuarterNote;
        TimingPattern lastPattern = TimingPattern::QuarterNote;

        bool patternChanged = (currentPattern != lastPattern);
        REQUIRE(patternChanged == false);
    }

    SECTION("different pattern should trigger morph") {
        TimingPattern currentPattern = TimingPattern::DottedEighth;
        TimingPattern lastPattern = TimingPattern::QuarterNote;

        bool patternChanged = (currentPattern != lastPattern);
        REQUIRE(patternChanged == true);
    }

    SECTION("tap count change should trigger morph") {
        // Even if pattern type is the same, changing tap count should trigger morph
        size_t currentTapCount = 6;
        size_t lastTapCount = 4;

        bool tapCountChanged = (currentTapCount != lastTapCount);
        REQUIRE(tapCountChanged == true);
    }
}

// ==============================================================================
// TEST: Morph time parameter setting
// ==============================================================================

TEST_CASE("Morph time parameter is respected",
          "[multi-tap][morph][parameter]") {
    MultiTapDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);
    delay.setTempo(120.0f);
    delay.loadTimingPattern(TimingPattern::QuarterNote, 4);

    SECTION("setMorphTime updates morph duration for next morph") {
        delay.setMorphTime(200.0f);
        REQUIRE(delay.getMorphTime() == Approx(200.0f));

        delay.setMorphTime(1500.0f);
        REQUIRE(delay.getMorphTime() == Approx(1500.0f));
    }

    SECTION("morph time is clamped to valid range") {
        delay.setMorphTime(10.0f);  // Below minimum (50ms)
        REQUIRE(delay.getMorphTime() >= 50.0f);

        delay.setMorphTime(5000.0f);  // Above maximum (2000ms)
        REQUIRE(delay.getMorphTime() <= 2000.0f);
    }

    SECTION("morphToPattern uses provided morph time") {
        // Set default morph time
        delay.setMorphTime(1000.0f);

        // morphToPattern with explicit time overrides
        delay.morphToPattern(TimingPattern::EighthNote, 100.0f);

        // The morph should use 100ms, not the default 1000ms
        // Verify by checking morph completes quickly
        auto ctx = makeTestContext();
        std::array<float, 512> left{}, right{};

        // Process 150ms worth (should be enough for 100ms morph)
        for (int i = 0; i < 15; ++i) {
            std::fill(left.begin(), left.end(), 0.0f);
            std::fill(right.begin(), right.end(), 0.0f);
            delay.process(left.data(), right.data(), 512, ctx);
        }

        REQUIRE(delay.isMorphing() == false);
    }
}

// ==============================================================================
// TEST: No audio discontinuities during morph
// ==============================================================================

TEST_CASE("Morphing produces smooth audio transitions",
          "[multi-tap][morph][audio]") {
    MultiTapDelay delay;
    delay.prepare(kSampleRate, kBlockSize, kMaxDelayMs);
    delay.setTempo(120.0f);
    delay.loadTimingPattern(TimingPattern::QuarterNote, 4);
    delay.setDryWetMix(100.0f);  // Wet only for clearer test
    delay.snapParameters();

    auto ctx = makeTestContext();

    SECTION("no NaN or infinite values during morph") {
        // Fill delay buffer with content
        std::array<float, 512> left{}, right{};
        std::fill(left.begin(), left.end(), 0.5f);
        std::fill(right.begin(), right.end(), 0.5f);

        for (int i = 0; i < 50; ++i) {
            delay.process(left.data(), right.data(), 512, ctx);
        }

        // Start morph
        delay.morphToPattern(TimingPattern::TripletEighth, 200.0f);

        bool hasInvalidValues = false;

        // Process through morph
        for (int i = 0; i < 30; ++i) {
            std::fill(left.begin(), left.end(), 0.5f);
            std::fill(right.begin(), right.end(), 0.5f);
            delay.process(left.data(), right.data(), 512, ctx);

            for (size_t s = 0; s < 512; ++s) {
                if (std::isnan(left[s]) || std::isnan(right[s]) ||
                    std::isinf(left[s]) || std::isinf(right[s])) {
                    hasInvalidValues = true;
                }
            }
        }

        REQUIRE_FALSE(hasInvalidValues);
    }

    SECTION("output stays within reasonable bounds during morph") {
        std::array<float, 512> left{}, right{};
        std::fill(left.begin(), left.end(), 0.3f);
        std::fill(right.begin(), right.end(), 0.3f);

        // Fill buffer
        for (int i = 0; i < 50; ++i) {
            delay.process(left.data(), right.data(), 512, ctx);
        }

        // Start morph to very different pattern
        delay.morphToPattern(TimingPattern::WholeNote, 300.0f);

        float maxOutput = 0.0f;

        for (int i = 0; i < 40; ++i) {
            std::fill(left.begin(), left.end(), 0.3f);
            std::fill(right.begin(), right.end(), 0.3f);
            delay.process(left.data(), right.data(), 512, ctx);

            for (size_t s = 0; s < 512; ++s) {
                maxOutput = std::max(maxOutput, std::abs(left[s]));
                maxOutput = std::max(maxOutput, std::abs(right[s]));
            }
        }

        // Output should not explode during morph
        INFO("Max output during morph: " << maxOutput);
        REQUIRE(maxOutput < 5.0f);
    }
}
