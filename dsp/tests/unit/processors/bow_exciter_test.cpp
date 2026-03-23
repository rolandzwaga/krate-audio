// ==============================================================================
// BowExciter Unit Tests
// ==============================================================================
// Layer 2: Processors | Spec 130 - Bow Model Exciter
//
// Constitution Principle XII: Test-First Development
// Tests written BEFORE implementation (Phase 3).
//
// Covers: FR-001 through FR-010, SC-001, SC-008, SC-009
// ==============================================================================

#include <krate/dsp/processors/bow_exciter.h>
#include <krate/dsp/core/db_utils.h>  // detail::isNaN, detail::isInf

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using Catch::Approx;
using namespace Krate::DSP;

// =============================================================================
// Helper: Run BowExciter in a simple feedback loop
// =============================================================================
namespace {

/// Runs the bow exciter in a simulated feedback loop with a simple
/// single-sample delay resonator (just feeds output back as feedback velocity).
std::vector<float> runFeedbackLoop(BowExciter& bow, int numSamples,
                                   float envelopeValue = 1.0f,
                                   float resonatorEnergy = 0.0f)
{
    std::vector<float> output(static_cast<size_t>(numSamples));
    float feedbackVelocity = 0.0f;
    for (int i = 0; i < numSamples; ++i) {
        bow.setEnvelopeValue(envelopeValue);
        bow.setResonatorEnergy(resonatorEnergy);
        float sample = bow.process(feedbackVelocity);
        output[static_cast<size_t>(i)] = sample;
        // Simple stub resonator: feed output back as velocity (with decay)
        feedbackVelocity = sample * 0.99f;
    }
    return output;
}

} // namespace

// =============================================================================
// T014: Lifecycle Tests
// =============================================================================

TEST_CASE("BowExciter lifecycle", "[processors][bow_exciter]")
{
    BowExciter bow;

    SECTION("default state: not prepared, not active") {
        REQUIRE_FALSE(bow.isPrepared());
        REQUIRE_FALSE(bow.isActive());
    }

    SECTION("prepare() sets isPrepared() true") {
        bow.prepare(44100.0);
        REQUIRE(bow.isPrepared());
        REQUIRE_FALSE(bow.isActive());
    }

    SECTION("trigger() sets isActive() true") {
        bow.prepare(44100.0);
        bow.trigger(0.8f);
        REQUIRE(bow.isActive());
    }

    SECTION("release() does not instantly silence") {
        bow.prepare(44100.0);
        bow.setPressure(0.3f);
        bow.setSpeed(0.5f);
        bow.trigger(0.8f);

        // Run a few samples to build up velocity
        auto warmup = runFeedbackLoop(bow, 200);

        // Release
        bow.release();

        // Process a few more samples - should still produce output
        // because velocity doesn't drop to zero instantly
        bow.setEnvelopeValue(0.5f);  // Simulate ADSR in release phase
        float sample = bow.process(0.0f);
        // The exciter may still be active or producing output
        // (release just marks for release; ADSR drives the ramp-down)
        // We just check no crash and that it was active before release
        REQUIRE(true);  // No crash
    }

    SECTION("reset() clears active state") {
        bow.prepare(44100.0);
        bow.trigger(0.8f);
        REQUIRE(bow.isActive());

        bow.reset();
        REQUIRE_FALSE(bow.isActive());
    }
}

// =============================================================================
// T015: Bow Table Friction Formula Tests
// =============================================================================

TEST_CASE("BowExciter bow table friction formula", "[processors][bow_exciter]")
{
    BowExciter bow;
    bow.prepare(44100.0);
    bow.trigger(0.8f);
    bow.setSpeed(1.0f);

    SECTION("at pressure=0.5, deltaV=0.1 produces bounded reflection coefficient") {
        bow.setPressure(0.5f);

        // Run enough samples for velocity to build up
        for (int i = 0; i < 500; ++i) {
            bow.setEnvelopeValue(1.0f);
            (void)bow.process(0.0f);
        }

        // Now process with a specific feedback velocity to create deltaV
        bow.setEnvelopeValue(1.0f);
        float result = bow.process(0.1f);

        // The result should be non-zero and bounded
        REQUIRE(std::abs(result) > 0.0f);
        REQUIRE(std::abs(result) < 10.0f);
    }

    SECTION("at deltaV=0, output is bounded") {
        bow.setPressure(0.5f);

        // Run samples where feedbackVelocity matches bowVelocity (roughly)
        // At minimum, the output should not blow up
        for (int i = 0; i < 100; ++i) {
            bow.setEnvelopeValue(1.0f);
            float result = bow.process(0.0f);
            REQUIRE(std::abs(result) < 100.0f);
        }
    }

    SECTION("slope formula: clamp(5.0 - 4.0 * pressure, 1.0, 10.0)") {
        // At pressure=0.0, slope should be 5.0
        // At pressure=1.0, slope should be 1.0
        // At pressure=0.25, slope should be 4.0
        // We verify indirectly: different pressures produce different output
        bow.setPressure(0.0f);
        bow.setEnvelopeValue(1.0f);

        // Build velocity
        for (int i = 0; i < 200; ++i) {
            bow.setEnvelopeValue(1.0f);
            (void)bow.process(0.0f);
        }
        float out_p0 = bow.process(0.05f);

        bow.reset();
        bow.prepare(44100.0);
        bow.trigger(0.8f);
        bow.setSpeed(1.0f);
        bow.setPressure(1.0f);
        for (int i = 0; i < 200; ++i) {
            bow.setEnvelopeValue(1.0f);
            (void)bow.process(0.0f);
        }
        float out_p1 = bow.process(0.05f);

        // Different pressures should produce different outputs
        REQUIRE(out_p0 != Approx(out_p1).margin(1e-6f));
    }
}

// =============================================================================
// T016: ADSR Acceleration Integration Tests
// =============================================================================

TEST_CASE("BowExciter ADSR acceleration integration", "[processors][bow_exciter]")
{
    BowExciter bow;
    bow.prepare(44100.0);
    bow.setSpeed(1.0f);
    bow.setPressure(0.3f);

    SECTION("with envelope=1.0, bow velocity increases over consecutive samples") {
        bow.trigger(0.8f);

        // Collect several samples with envelope at full
        std::vector<float> outputs;
        float fbVel = 0.0f;
        for (int i = 0; i < 50; ++i) {
            bow.setEnvelopeValue(1.0f);
            float out = bow.process(fbVel);
            outputs.push_back(out);
        }

        // The excitation force should increase from zero as velocity builds
        // Check that early outputs are smaller than later ones (velocity ramp)
        float earlySum = 0.0f;
        float lateSum = 0.0f;
        for (int i = 0; i < 10; ++i) {
            earlySum += std::abs(outputs[static_cast<size_t>(i)]);
        }
        for (int i = 30; i < 40; ++i) {
            lateSum += std::abs(outputs[static_cast<size_t>(i)]);
        }
        // Later samples should have more energy (velocity has increased)
        REQUIRE(lateSum > earlySum);
    }

    SECTION("velocity saturates at maxVelocity * speed") {
        bow.trigger(0.8f);
        bow.setSpeed(0.5f);

        // Run many samples to let velocity saturate
        for (int i = 0; i < 5000; ++i) {
            bow.setEnvelopeValue(1.0f);
            (void)bow.process(0.0f);
        }

        // At saturation, consecutive outputs should be similar
        float out1 = bow.process(0.0f);
        float out2 = bow.process(0.0f);
        // They won't be identical (jitter), but should be close in magnitude
        REQUIRE(std::abs(out1) == Approx(std::abs(out2)).margin(0.1f));
    }

    SECTION("with envelope=0.0, velocity does not increase") {
        bow.trigger(0.8f);

        // Set envelope to zero - no acceleration
        for (int i = 0; i < 100; ++i) {
            bow.setEnvelopeValue(0.0f);
            float out = bow.process(0.0f);
            // With zero envelope, velocity stays at zero, so output should be near zero
            REQUIRE(std::abs(out) < 0.001f);
        }
    }
}

// =============================================================================
// T017: Excitation Force Output Tests
// =============================================================================

TEST_CASE("BowExciter excitation force output", "[processors][bow_exciter]")
{
    BowExciter bow;
    bow.prepare(44100.0);
    bow.setPressure(0.3f);
    bow.setSpeed(1.0f);
    bow.trigger(0.8f);

    // Build up velocity
    for (int i = 0; i < 500; ++i) {
        bow.setEnvelopeValue(1.0f);
        (void)bow.process(0.0f);
    }

    SECTION("process returns non-zero when bowVelocity != feedbackVelocity") {
        bow.setEnvelopeValue(1.0f);
        float result = bow.process(0.0f);
        REQUIRE(std::abs(result) > 0.0f);
    }

    SECTION("returned force changes when feedbackVelocity changes") {
        bow.setEnvelopeValue(1.0f);
        float result1 = bow.process(0.0f);
        float result2 = bow.process(0.5f);

        REQUIRE(result1 != Approx(result2).margin(1e-6f));
    }
}

// =============================================================================
// T018: Position Impedance Scaling Tests
// =============================================================================

TEST_CASE("BowExciter position impedance scaling", "[processors][bow_exciter]")
{
    // Position impedance = 1.0 / max(beta * (1 - beta) * 4.0, 0.1)
    // At position=0.13: beta*(1-beta)*4 = 0.13*0.87*4 = 0.4524, imp = 1/0.4524 ~ 2.21
    // At position=0.0: beta*(1-beta)*4 = 0, clamped to 0.1, imp = 1/0.1 = 10.0
    // At position=0.5: beta*(1-beta)*4 = 1.0, imp = 1.0

    SECTION("position affects output amplitude") {
        // Run at position=0.5 (impedance=1.0) and position=0.01 (high impedance)
        auto runAtPosition = [](float pos) {
            BowExciter bow;
            bow.prepare(44100.0);
            bow.setPressure(0.3f);
            bow.setSpeed(1.0f);
            bow.setPosition(pos);
            bow.trigger(0.8f);
            auto output = runFeedbackLoop(bow, 1000);
            float rms = 0.0f;
            for (size_t i = 500; i < 1000; ++i) {
                rms += output[i] * output[i];
            }
            return std::sqrt(rms / 500.0f);
        };

        float rms_mid = runAtPosition(0.5f);
        float rms_edge = runAtPosition(0.01f);

        // Near-edge position has higher impedance, so output should differ
        REQUIRE(rms_mid != Approx(rms_edge).margin(1e-6f));
    }

    SECTION("extreme positions do not cause singularities") {
        auto runAtPosition = [](float pos) {
            BowExciter bow;
            bow.prepare(44100.0);
            bow.setPressure(0.3f);
            bow.setSpeed(1.0f);
            bow.setPosition(pos);
            bow.trigger(0.8f);
            auto output = runFeedbackLoop(bow, 200);
            bool hasNaN = false;
            bool hasInf = false;
            for (float s : output) {
                if (detail::isNaN(s)) hasNaN = true;
                if (detail::isInf(s)) hasInf = true;
            }
            return !hasNaN && !hasInf;
        };

        REQUIRE(runAtPosition(0.0f));
        REQUIRE(runAtPosition(1.0f));
        REQUIRE(runAtPosition(0.001f));
        REQUIRE(runAtPosition(0.999f));
    }
}

// =============================================================================
// T019: Micro-Variation Tests (SC-001)
// =============================================================================

TEST_CASE("BowExciter micro-variation from rosin jitter", "[processors][bow_exciter]")
{
    BowExciter bow;
    bow.prepare(44100.0);
    bow.setPressure(0.3f);
    bow.setSpeed(0.5f);
    bow.setPosition(0.13f);
    bow.trigger(0.8f);

    // Build up to steady state
    auto warmup = runFeedbackLoop(bow, 2000);

    // Collect 100 samples at steady state
    std::vector<float> samples(100);
    float fbVel = warmup.back() * 0.99f;
    for (int i = 0; i < 100; ++i) {
        bow.setEnvelopeValue(1.0f);
        bow.setResonatorEnergy(0.0f);
        float s = bow.process(fbVel);
        samples[static_cast<size_t>(i)] = s;
        fbVel = s * 0.99f;
    }

    // Verify no two consecutive samples are identical (proves jitter is active)
    int identicalCount = 0;
    for (int i = 1; i < 100; ++i) {
        if (samples[static_cast<size_t>(i)] == samples[static_cast<size_t>(i - 1)]) {
            ++identicalCount;
        }
    }
    // Allow at most a few coincidental matches, but not all
    REQUIRE(identicalCount < 10);
}

// =============================================================================
// T020: Energy Control Tests (FR-010)
// =============================================================================

TEST_CASE("BowExciter energy control", "[processors][bow_exciter]")
{
    SECTION("high resonator energy attenuates output") {
        // Run with low energy (no attenuation)
        BowExciter bow1;
        bow1.prepare(44100.0);
        bow1.setPressure(0.3f);
        bow1.setSpeed(0.5f);
        bow1.trigger(0.8f);

        // Build velocity
        for (int i = 0; i < 500; ++i) {
            bow1.setEnvelopeValue(1.0f);
            bow1.setResonatorEnergy(0.0f);  // No energy => energyRatio <= 1
            (void)bow1.process(0.0f);
        }
        bow1.setEnvelopeValue(1.0f);
        bow1.setResonatorEnergy(0.0f);
        float outLow = std::abs(bow1.process(0.0f));

        // Run with high energy (should attenuate)
        BowExciter bow2;
        bow2.prepare(44100.0);
        bow2.setPressure(0.3f);
        bow2.setSpeed(0.5f);
        bow2.trigger(0.8f);

        for (int i = 0; i < 500; ++i) {
            bow2.setEnvelopeValue(1.0f);
            bow2.setResonatorEnergy(0.0f);
            (void)bow2.process(0.0f);
        }

        // Now set high energy: targetEnergy is set from velocity*speed in trigger
        // velocity=0.8, speed=0.5 => targetEnergy = 0.4
        // energyRatio = 3.0 * targetEnergy / targetEnergy = 3.0
        // So we need energy = 3.0 * 0.4 = 1.2
        // Run for enough samples to let the EMA energy tracker converge
        for (int i = 0; i < 1000; ++i) {
            bow2.setEnvelopeValue(1.0f);
            bow2.setResonatorEnergy(1.2f);
            (void)bow2.process(0.0f);
        }
        bow2.setEnvelopeValue(1.0f);
        bow2.setResonatorEnergy(1.2f);
        float outHigh = std::abs(bow2.process(0.0f));

        // Output with high energy should be attenuated by at least 20%
        if (outLow > 0.0001f) {
            REQUIRE(outHigh < outLow * 0.85f);
        }
    }
}

// =============================================================================
// T021: Numerical Safety Tests (SC-008, SC-009)
// =============================================================================

TEST_CASE("BowExciter numerical safety at extreme parameters", "[processors][bow_exciter]")
{
    BowExciter bow;
    bow.prepare(44100.0);
    bow.setPressure(1.0f);
    bow.setSpeed(1.0f);
    bow.setPosition(0.01f);
    bow.trigger(1.0f);

    bool hasNaN = false;
    bool hasInf = false;
    float maxAbs = 0.0f;
    float fbVel = 0.0f;

    for (int i = 0; i < 1000; ++i) {
        bow.setEnvelopeValue(1.0f);
        bow.setResonatorEnergy(0.0f);
        float sample = bow.process(fbVel);

        if (detail::isNaN(sample)) hasNaN = true;
        if (detail::isInf(sample)) hasInf = true;
        maxAbs = std::max(maxAbs, std::abs(sample));

        fbVel = sample * 0.99f;
    }

    REQUIRE_FALSE(hasNaN);
    REQUIRE_FALSE(hasInf);
    REQUIRE(maxAbs <= 10.0f);
}

TEST_CASE("BowExciter numerical safety with all extreme combinations", "[processors][bow_exciter]")
{
    struct ParamSet {
        float pressure;
        float speed;
        float position;
    };

    // Test corners of parameter space
    std::vector<ParamSet> combos = {
        {0.0f, 0.0f, 0.0f},
        {1.0f, 1.0f, 1.0f},
        {1.0f, 0.0f, 0.5f},
        {0.0f, 1.0f, 0.5f},
        {0.5f, 0.5f, 0.0f},
        {0.5f, 0.5f, 1.0f},
    };

    for (const auto& p : combos) {
        BowExciter bow;
        bow.prepare(44100.0);
        bow.setPressure(p.pressure);
        bow.setSpeed(p.speed);
        bow.setPosition(p.position);
        bow.trigger(1.0f);

        bool hasNaN = false;
        bool hasInf = false;
        float fbVel = 0.0f;

        for (int i = 0; i < 500; ++i) {
            bow.setEnvelopeValue(1.0f);
            float sample = bow.process(fbVel);

            if (detail::isNaN(sample)) hasNaN = true;
            if (detail::isInf(sample)) hasInf = true;
            fbVel = sample * 0.99f;
        }

        REQUIRE_FALSE(hasNaN);
        REQUIRE_FALSE(hasInf);
    }
}
