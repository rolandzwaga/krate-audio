// ==============================================================================
// Tests: Sweep Envelope Follower (User Story 9)
// ==============================================================================
// Tests for sweep frequency modulation via envelope follower.
//
// Reference: specs/007-sweep-system/spec.md (FR-026, FR-027, SC-016)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "dsp/sweep_envelope.h"

#include <cmath>

using Catch::Approx;
using namespace Disrumpo;

namespace {
constexpr double kTestSampleRate = 44100.0;
constexpr int kTestBlockSize = 512;
}

// ==============================================================================
// FR-026: Envelope Follower Input-Driven Modulation
// ==============================================================================

TEST_CASE("SweepEnvelope input level response", "[sweep][envelope]") {
    SweepEnvelope env;
    env.prepare(kTestSampleRate, kTestBlockSize);
    env.setEnabled(true);

    SECTION("Responds to input signal level") {
        env.setSensitivity(1.0f);  // Full sensitivity
        env.setAttackTime(1.0f);   // Fast attack
        env.setReleaseTime(100.0f);

        // Process loud signal
        float loudEnvelope = 0.0f;
        for (int i = 0; i < 1000; ++i) {
            float inputSample = (i % 2 == 0) ? 1.0f : -1.0f;  // Full scale square
            loudEnvelope = env.processSample(inputSample);
        }

        // Process quiet signal
        env.reset();
        float quietEnvelope = 0.0f;
        for (int i = 0; i < 1000; ++i) {
            float inputSample = (i % 2 == 0) ? 0.1f : -0.1f;  // 10% scale
            quietEnvelope = env.processSample(inputSample);
        }

        // Loud signal should produce higher envelope
        REQUIRE(loudEnvelope > quietEnvelope);
    }

    SECTION("Silent input produces zero envelope") {
        env.setSensitivity(1.0f);
        env.setAttackTime(1.0f);
        env.setReleaseTime(10.0f);

        // Process silence
        for (int i = 0; i < 10000; ++i) {
            (void)env.processSample(0.0f);
        }

        float envelope = env.getEnvelopeLevel();
        REQUIRE(envelope < 0.01f);  // Near zero
    }
}

// ==============================================================================
// FR-027: Attack/Release Times
// ==============================================================================

TEST_CASE("SweepEnvelope attack/release times", "[sweep][envelope]") {
    SweepEnvelope env;
    env.prepare(kTestSampleRate, kTestBlockSize);
    env.setEnabled(true);
    env.setSensitivity(1.0f);

    SECTION("Attack time range 1-100ms") {
        // Minimum attack
        env.setAttackTime(1.0f);
        REQUIRE(env.getAttackTime() == Approx(1.0f).margin(0.1f));

        // Maximum attack
        env.setAttackTime(100.0f);
        REQUIRE(env.getAttackTime() == Approx(100.0f).margin(0.1f));

        // Clamping below minimum
        env.setAttackTime(0.1f);
        REQUIRE(env.getAttackTime() >= 1.0f);

        // Clamping above maximum
        env.setAttackTime(500.0f);
        REQUIRE(env.getAttackTime() <= 100.0f);
    }

    SECTION("Release time range 10-500ms") {
        // Minimum release
        env.setReleaseTime(10.0f);
        REQUIRE(env.getReleaseTime() == Approx(10.0f).margin(0.1f));

        // Maximum release
        env.setReleaseTime(500.0f);
        REQUIRE(env.getReleaseTime() == Approx(500.0f).margin(0.1f));

        // Clamping below minimum
        env.setReleaseTime(1.0f);
        REQUIRE(env.getReleaseTime() >= 10.0f);

        // Clamping above maximum
        env.setReleaseTime(1000.0f);
        REQUIRE(env.getReleaseTime() <= 500.0f);
    }

    SECTION("Fast attack responds quickly to transients") {
        env.setAttackTime(1.0f);  // 1ms attack
        env.setReleaseTime(500.0f);

        env.reset();

        // Feed impulse
        (void)env.processSample(1.0f);

        // After just a few samples at 1ms attack, should have significant envelope
        for (int i = 0; i < 100; ++i) {
            (void)env.processSample(1.0f);
        }

        float envelope = env.getEnvelopeLevel();
        REQUIRE(envelope > 0.5f);  // Should be responding
    }

    SECTION("Slow attack responds gradually") {
        env.setAttackTime(100.0f);  // 100ms attack
        env.setReleaseTime(500.0f);

        env.reset();

        // Process short burst
        for (int i = 0; i < 100; ++i) {
            (void)env.processSample(1.0f);
        }

        float envelope = env.getEnvelopeLevel();
        // With 100ms attack and only ~2.3ms of signal, envelope should be low
        REQUIRE(envelope < 0.5f);
    }
}

// ==============================================================================
// FR-027: Sensitivity Parameter
// ==============================================================================

TEST_CASE("SweepEnvelope sensitivity", "[sweep][envelope]") {
    SweepEnvelope env;
    env.prepare(kTestSampleRate, kTestBlockSize);
    env.setEnabled(true);
    env.setAttackTime(1.0f);
    env.setReleaseTime(100.0f);

    SECTION("Sensitivity 0% produces no modulation") {
        env.setSensitivity(0.0f);

        // Process loud signal
        for (int i = 0; i < 1000; ++i) {
            (void)env.processSample(1.0f);
        }

        float modAmount = env.getModulationAmount();
        REQUIRE(modAmount < 0.01f);  // Near zero modulation
    }

    SECTION("Sensitivity 100% produces full modulation") {
        env.setSensitivity(1.0f);

        // Process loud signal
        for (int i = 0; i < 10000; ++i) {
            (void)env.processSample(1.0f);
        }

        float modAmount = env.getModulationAmount();
        REQUIRE(modAmount > 0.5f);  // Significant modulation
    }

    SECTION("Sensitivity 50% produces scaled modulation") {
        // First get 100% sensitivity level
        env.setSensitivity(1.0f);
        for (int i = 0; i < 10000; ++i) {
            (void)env.processSample(1.0f);
        }
        float fullMod = env.getModulationAmount();

        // Now test 50% sensitivity
        env.reset();
        env.setSensitivity(0.5f);
        for (int i = 0; i < 10000; ++i) {
            (void)env.processSample(1.0f);
        }
        float halfMod = env.getModulationAmount();

        // Half sensitivity should produce roughly half modulation
        REQUIRE(halfMod < fullMod);
        REQUIRE(halfMod == Approx(fullMod * 0.5f).margin(fullMod * 0.2f));
    }
}

// ==============================================================================
// SC-016: Envelope Follower Response Time
// ==============================================================================

TEST_CASE("SweepEnvelope response time", "[sweep][envelope]") {
    SweepEnvelope env;
    env.prepare(kTestSampleRate, kTestBlockSize);
    env.setEnabled(true);
    env.setSensitivity(1.0f);
    env.setAttackTime(10.0f);   // 10ms attack
    env.setReleaseTime(100.0f); // 100ms release

    SECTION("Envelope rises during attack phase") {
        env.reset();

        // Process loud signal for attack duration
        int attackSamples = static_cast<int>(kTestSampleRate * 0.010);  // 10ms

        float startEnv = env.getEnvelopeLevel();
        for (int i = 0; i < attackSamples * 2; ++i) {
            (void)env.processSample(1.0f);
        }
        float endEnv = env.getEnvelopeLevel();

        REQUIRE(endEnv > startEnv);
    }

    SECTION("Envelope falls during release phase") {
        env.reset();

        // First charge up envelope
        for (int i = 0; i < 10000; ++i) {
            (void)env.processSample(1.0f);
        }
        float peakEnv = env.getEnvelopeLevel();

        // Then release with silence
        for (int i = 0; i < 10000; ++i) {
            (void)env.processSample(0.0f);
        }
        float releasedEnv = env.getEnvelopeLevel();

        REQUIRE(releasedEnv < peakEnv);
    }
}

// ==============================================================================
// Frequency Modulation Output
// ==============================================================================

TEST_CASE("SweepEnvelope frequency modulation", "[sweep][envelope]") {
    SweepEnvelope env;
    env.prepare(kTestSampleRate, kTestBlockSize);
    env.setEnabled(true);
    env.setSensitivity(1.0f);
    env.setAttackTime(1.0f);
    env.setReleaseTime(100.0f);

    SECTION("getModulatedFrequency returns frequency in range") {
        constexpr float baseFreq = 1000.0f;

        // Process some input
        for (int i = 0; i < 1000; ++i) {
            (void)env.processSample(0.5f);
        }

        float modFreq = env.getModulatedFrequency(baseFreq);

        // Should be within sweep frequency range
        REQUIRE(modFreq >= 20.0f);
        REQUIRE(modFreq <= 20000.0f);
    }

    SECTION("Higher input produces higher frequency") {
        constexpr float baseFreq = 1000.0f;

        // Process quiet signal
        env.reset();
        for (int i = 0; i < 10000; ++i) {
            (void)env.processSample(0.1f);
        }
        float quietFreq = env.getModulatedFrequency(baseFreq);

        // Process loud signal
        env.reset();
        for (int i = 0; i < 10000; ++i) {
            (void)env.processSample(1.0f);
        }
        float loudFreq = env.getModulatedFrequency(baseFreq);

        REQUIRE(loudFreq > quietFreq);
    }
}
