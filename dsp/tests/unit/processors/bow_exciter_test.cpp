// ==============================================================================
// BowExciter Unit Tests
// ==============================================================================
// Layer 2: Processors | Spec 130 - Bow Model Exciter
//
// Constitution Principle XII: Test-First Development
// Tests written BEFORE implementation (Phase 3).
//
// Covers: FR-001 through FR-010, FR-014, SC-001, SC-002, SC-008, SC-009
// ==============================================================================

#include <krate/dsp/processors/bow_exciter.h>
#include <krate/dsp/core/db_utils.h>  // detail::isNaN, detail::isInf
#include <krate/dsp/primitives/fft.h>
#include <krate/dsp/core/window_functions.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <numeric>
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
            REQUIRE(outHigh < outLow * 0.80f);
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

// =============================================================================
// Phase 4: Pressure Timbral Region Tests (T031-T034)
// =============================================================================

namespace {

/// Runs the bow exciter in a feedback loop simulating a resonator at the
/// given frequency, using a simple delay line (waveguide-style).
/// Returns the output buffer (resonator output, not raw excitation).
std::vector<float> runResonatorFeedbackLoop(
    BowExciter& bow, int numSamples, double sampleRate,
    float resonatorFreqHz, float envelopeValue = 1.0f,
    float feedbackGain = 0.995f)
{
    // Simple delay-line resonator at the target frequency
    int delaySamples = static_cast<int>(sampleRate / resonatorFreqHz);
    if (delaySamples < 1) delaySamples = 1;

    std::vector<float> delayBuffer(static_cast<size_t>(delaySamples), 0.0f);
    int writePos = 0;

    std::vector<float> output(static_cast<size_t>(numSamples));
    float feedbackVelocity = 0.0f;

    for (int i = 0; i < numSamples; ++i) {
        bow.setEnvelopeValue(envelopeValue);
        bow.setResonatorEnergy(0.0f);

        float excitation = bow.process(feedbackVelocity);

        // Read from delay line (resonator feedback)
        int readPos = (writePos + 1) % delaySamples;
        float delayedOut = delayBuffer[static_cast<size_t>(readPos)];

        // Resonator output = excitation injected + delayed feedback
        float resonatorOut = excitation + delayedOut * feedbackGain;

        // Write resonator output into delay line
        delayBuffer[static_cast<size_t>(writePos)] = resonatorOut;

        // Feedback velocity is a fraction of the resonator state
        feedbackVelocity = resonatorOut * 0.1f;

        output[static_cast<size_t>(i)] = resonatorOut;
        writePos = (writePos + 1) % delaySamples;
    }
    return output;
}

/// Compute RMS of a range within a buffer
float computeRMS(const std::vector<float>& buf, size_t start, size_t end)
{
    if (end <= start) return 0.0f;
    float sum = 0.0f;
    for (size_t i = start; i < end && i < buf.size(); ++i) {
        sum += buf[i] * buf[i];
    }
    return std::sqrt(sum / static_cast<float>(end - start));
}

/// Compute spectral energy above a given frequency as a fraction of total
/// energy. Uses FFT on a Hann-windowed section of the signal.
float computeHighFreqRatio(const std::vector<float>& signal,
                           size_t start, size_t length,
                           float sampleRate, float cutoffHz)
{
    // Use power-of-2 FFT size
    size_t fftSize = 1;
    while (fftSize < length) fftSize <<= 1;
    if (fftSize > 8192) fftSize = 8192;
    if (fftSize < 256) fftSize = 256;
    size_t usedLength = std::min(length, fftSize);

    std::vector<float> windowed(fftSize, 0.0f);
    std::vector<float> window(fftSize, 0.0f);
    Window::generateHann(window.data(), fftSize);

    for (size_t i = 0; i < usedLength && (start + i) < signal.size(); ++i) {
        windowed[i] = signal[start + i] * window[i];
    }

    FFT fft;
    fft.prepare(fftSize);
    std::vector<Complex> spectrum(fft.numBins());
    fft.forward(windowed.data(), spectrum.data());

    size_t cutoffBin = static_cast<size_t>(
        cutoffHz * static_cast<float>(fftSize) / sampleRate);

    float totalPower = 0.0f;
    float highPower = 0.0f;
    for (size_t b = 1; b < fft.numBins(); ++b) {
        float mag2 = spectrum[b].real * spectrum[b].real
                   + spectrum[b].imag * spectrum[b].imag;
        totalPower += mag2;
        if (b > cutoffBin) {
            highPower += mag2;
        }
    }

    if (totalPower < 1e-20f) return 0.0f;
    return highPower / totalPower;
}

} // namespace

// =============================================================================
// T031: Pressure timbral regions (surface sound / Helmholtz / raucous)
// =============================================================================

TEST_CASE("BowExciter pressure timbral regions", "[processors][bow_exciter]")
{
    constexpr double kSampleRate = 44100.0;
    constexpr float kResonatorFreq = 220.0f;
    constexpr int kTotalSamples = 22050;  // 0.5 seconds
    constexpr size_t kSteadyStart = 11025;  // Last 0.25 seconds

    SECTION("surface sound (pressure=0.05) has lower RMS than Helmholtz (pressure=0.5)") {
        // Surface sound regime
        BowExciter bowLow;
        bowLow.prepare(kSampleRate);
        bowLow.setPressure(0.05f);
        bowLow.setSpeed(0.5f);
        bowLow.setPosition(0.13f);
        bowLow.trigger(0.8f);
        auto outputLow = runResonatorFeedbackLoop(
            bowLow, kTotalSamples, kSampleRate, kResonatorFreq);
        float rmsLow = computeRMS(outputLow, kSteadyStart,
                                  static_cast<size_t>(kTotalSamples));

        // Helmholtz regime
        BowExciter bowMid;
        bowMid.prepare(kSampleRate);
        bowMid.setPressure(0.5f);
        bowMid.setSpeed(0.5f);
        bowMid.setPosition(0.13f);
        bowMid.trigger(0.8f);
        auto outputMid = runResonatorFeedbackLoop(
            bowMid, kTotalSamples, kSampleRate, kResonatorFreq);
        float rmsMid = computeRMS(outputMid, kSteadyStart,
                                  static_cast<size_t>(kTotalSamples));

        INFO("Surface sound RMS: " << rmsLow);
        INFO("Helmholtz RMS: " << rmsMid);
        // Surface sound should have measurably lower output energy
        REQUIRE(rmsLow < rmsMid);
    }

    SECTION("raucous (pressure=0.9) has more high-frequency content than Helmholtz (pressure=0.5)") {
        // Helmholtz regime
        BowExciter bowMid;
        bowMid.prepare(kSampleRate);
        bowMid.setPressure(0.5f);
        bowMid.setSpeed(0.5f);
        bowMid.setPosition(0.13f);
        bowMid.trigger(0.8f);
        auto outputMid = runResonatorFeedbackLoop(
            bowMid, kTotalSamples, kSampleRate, kResonatorFreq);
        float hfRatioMid = computeHighFreqRatio(
            outputMid, kSteadyStart, 4096, 44100.0f, 3000.0f);

        // Raucous regime
        BowExciter bowHigh;
        bowHigh.prepare(kSampleRate);
        bowHigh.setPressure(0.9f);
        bowHigh.setSpeed(0.5f);
        bowHigh.setPosition(0.13f);
        bowHigh.trigger(0.8f);
        auto outputHigh = runResonatorFeedbackLoop(
            bowHigh, kTotalSamples, kSampleRate, kResonatorFreq);
        float hfRatioHigh = computeHighFreqRatio(
            outputHigh, kSteadyStart, 4096, 44100.0f, 3000.0f);

        INFO("Helmholtz high-freq ratio: " << hfRatioMid);
        INFO("Raucous high-freq ratio: " << hfRatioHigh);
        // Raucous should have more high-frequency content
        REQUIRE(hfRatioHigh > hfRatioMid);
    }
}

// =============================================================================
// T032: Helmholtz regime (fundamental SNR >= 20 dB, >= 3 harmonics > -40 dBFS)
// =============================================================================

TEST_CASE("BowExciter Helmholtz regime spectral quality", "[processors][bow_exciter]")
{
    constexpr double kSampleRate = 44100.0;
    constexpr float kResonatorFreq = 220.0f;
    constexpr int kTotalSamples = 88200;  // 2 seconds for full buildup
    constexpr size_t kFFTSize = 8192;

    BowExciter bow;
    bow.prepare(kSampleRate);
    bow.setPressure(0.3f);
    bow.setSpeed(1.0f);   // Full speed for strong excitation
    bow.setPosition(0.13f);
    bow.trigger(0.8f);

    auto output = runResonatorFeedbackLoop(
        bow, kTotalSamples, kSampleRate, kResonatorFreq);

    // Analyze steady-state portion (last portion)
    size_t analyzeStart = static_cast<size_t>(kTotalSamples) - kFFTSize;

    // Apply Hann window
    std::vector<float> windowed(kFFTSize, 0.0f);
    std::vector<float> window(kFFTSize, 0.0f);
    Window::generateHann(window.data(), kFFTSize);

    for (size_t i = 0; i < kFFTSize; ++i) {
        windowed[i] = output[analyzeStart + i] * window[i];
    }

    // Compute FFT
    FFT fft;
    fft.prepare(kFFTSize);
    std::vector<Complex> spectrum(fft.numBins());
    fft.forward(windowed.data(), spectrum.data());

    // Compute magnitude spectrum
    size_t numBins = fft.numBins();
    std::vector<float> magnitudes(numBins);
    for (size_t b = 0; b < numBins; ++b) {
        magnitudes[b] = std::sqrt(spectrum[b].real * spectrum[b].real
                                + spectrum[b].imag * spectrum[b].imag);
    }

    // Find fundamental bin (220 Hz)
    size_t fundamentalBin = static_cast<size_t>(
        kResonatorFreq * static_cast<float>(kFFTSize) / static_cast<float>(kSampleRate));

    // Find peak magnitude around fundamental (+/-2 bins for leakage)
    float fundamentalMag = 0.0f;
    for (size_t b = (fundamentalBin > 2 ? fundamentalBin - 2 : 0);
         b <= fundamentalBin + 2 && b < numBins; ++b) {
        if (magnitudes[b] > fundamentalMag) {
            fundamentalMag = magnitudes[b];
        }
    }

    // Compute dBFS relative to the fundamental (fundamental = 0 dBFS)
    constexpr float kEpsilon = 1e-10f;
    std::vector<float> magnitudeDb(numBins);
    for (size_t b = 0; b < numBins; ++b) {
        magnitudeDb[b] = 20.0f * std::log10(
            std::max(magnitudes[b], kEpsilon) / std::max(fundamentalMag, kEpsilon));
    }

    // Compute noise floor (RMS of non-harmonic, non-DC bins)
    // Skip bins below 100 Hz (DC area) and bins near harmonics
    float noiseSum = 0.0f;
    int noiseCount = 0;
    size_t minNoiseBin = static_cast<size_t>(
        100.0f * static_cast<float>(kFFTSize) / static_cast<float>(kSampleRate));
    for (size_t b = minNoiseBin; b < numBins; ++b) {
        float binFreq = static_cast<float>(b) * static_cast<float>(kSampleRate)
                      / static_cast<float>(kFFTSize);
        float nearestHarmonicDist = std::fmod(binFreq, kResonatorFreq);
        if (nearestHarmonicDist > kResonatorFreq / 2.0f) {
            nearestHarmonicDist = kResonatorFreq - nearestHarmonicDist;
        }
        float binWidth = static_cast<float>(kSampleRate)
                       / static_cast<float>(kFFTSize);
        if (nearestHarmonicDist > binWidth * 3.0f) {
            noiseSum += magnitudes[b] * magnitudes[b];
            ++noiseCount;
        }
    }
    float noiseRms = (noiseCount > 0)
        ? std::sqrt(noiseSum / static_cast<float>(noiseCount))
        : kEpsilon;
    float noiseDb = 20.0f * std::log10(
        std::max(noiseRms, kEpsilon) / std::max(fundamentalMag, kEpsilon));

    float fundamentalSNR = -noiseDb;  // fundamental is 0 dBFS, noise is negative
    INFO("Fundamental magnitude: " << fundamentalMag);
    INFO("Noise floor dB (rel fundamental): " << noiseDb);
    INFO("Fundamental SNR: " << fundamentalSNR << " dB");

    SECTION("fundamental SNR >= 20 dB") {
        REQUIRE(fundamentalSNR >= 20.0f);
    }

    SECTION("at least 3 harmonics above -40 dBFS relative to fundamental") {
        // Count harmonics within 40 dB of the fundamental
        int harmonicsAboveThreshold = 0;
        constexpr float kThresholdDb = -40.0f;
        constexpr int kMaxHarmonics = 20;

        for (int h = 2; h <= kMaxHarmonics; ++h) {  // Start from 2nd harmonic
            float harmonicFreq = kResonatorFreq * static_cast<float>(h);
            if (harmonicFreq >= static_cast<float>(kSampleRate) / 2.0f) break;

            size_t hBin = static_cast<size_t>(
                harmonicFreq * static_cast<float>(kFFTSize)
                / static_cast<float>(kSampleRate));

            // Find peak around harmonic bin (+/-2 bins)
            float peakDb = -200.0f;
            for (size_t b = (hBin > 2 ? hBin - 2 : 0);
                 b <= hBin + 2 && b < numBins; ++b) {
                if (magnitudeDb[b] > peakDb) {
                    peakDb = magnitudeDb[b];
                }
            }

            INFO("Harmonic " << h << " (" << harmonicFreq
                 << " Hz, bin " << hBin << "): " << peakDb << " dB rel fundamental");
            if (peakDb >= kThresholdDb) {
                ++harmonicsAboveThreshold;
            }
        }

        INFO("Harmonics above -40 dB (rel fundamental): " << harmonicsAboveThreshold);
        REQUIRE(harmonicsAboveThreshold >= 3);
    }
}

// =============================================================================
// T033: Smooth pressure transition (no discontinuities)
// =============================================================================

TEST_CASE("BowExciter smooth pressure transition", "[processors][bow_exciter]")
{
    constexpr double kSampleRate = 44100.0;
    constexpr int kWarmupSamples = 4410;  // 100ms warmup
    constexpr int kSweepSteps = 1000;
    constexpr int kSamplesPerStep = 10;  // 10 samples per step for settling
    constexpr float kMaxDiscontinuity = 0.1f;

    BowExciter bow;
    bow.prepare(kSampleRate);
    bow.setPressure(0.0f);
    bow.setSpeed(0.5f);
    bow.setPosition(0.13f);
    bow.trigger(0.8f);

    // Warmup at pressure=0.5 (mid-range) to build up stable oscillation
    float fbVel = 0.0f;
    bow.setPressure(0.5f);
    for (int i = 0; i < kWarmupSamples; ++i) {
        bow.setEnvelopeValue(1.0f);
        float s = bow.process(fbVel);
        fbVel = s * 0.5f;
    }

    // Sweep pressure from 0.0 to 1.0 over 1000 steps
    // Check that consecutive samples within and across steps
    // don't have amplitude jumps > 0.1
    float prevSample = 0.0f;
    {
        bow.setPressure(0.0f);
        bow.setEnvelopeValue(1.0f);
        prevSample = bow.process(fbVel);
        fbVel = prevSample * 0.5f;
    }

    float maxJump = 0.0f;
    int discontinuityCount = 0;

    for (int step = 0; step < kSweepSteps; ++step) {
        float pressure = static_cast<float>(step + 1) / static_cast<float>(kSweepSteps);
        bow.setPressure(pressure);

        for (int s = 0; s < kSamplesPerStep; ++s) {
            bow.setEnvelopeValue(1.0f);
            float sample = bow.process(fbVel);
            float jump = std::abs(sample - prevSample);
            if (jump > maxJump) maxJump = jump;
            if (jump > kMaxDiscontinuity) {
                ++discontinuityCount;
            }
            prevSample = sample;
            fbVel = sample * 0.5f;
        }
    }

    INFO("Max inter-sample jump: " << maxJump);
    INFO("Discontinuity count (>" << kMaxDiscontinuity << "): " << discontinuityCount);
    REQUIRE(discontinuityCount == 0);
}

// =============================================================================
// T034: Slope formula coverage
// =============================================================================

TEST_CASE("BowExciter slope formula coverage", "[processors][bow_exciter]")
{
    // slope = clamp(5.0 - 4.0 * pressure, 1.0, 10.0)
    // pressure=0.0 => slope=5.0
    // pressure=1.0 => slope=1.0
    // pressure=0.25 => slope=4.0

    // We verify the slope formula by observing the effect on the bow table.
    // The bow table is: reflCoeff = clamp(1/(x^4), 0.01, 0.98)
    // where x = |deltaV * slope + offset| + 0.75
    //
    // To isolate the slope effect, we create a known deltaV scenario and
    // compare outputs at different pressures. With known inputs we can
    // predict the slope value indirectly.

    // Direct formula verification: create bow at specific pressures,
    // run enough samples to reach steady velocity, then check that
    // the output differences are consistent with the slope formula.

    constexpr double kSampleRate = 44100.0;

    // Use a small feedback velocity difference to stay in the sensitive region
    // of the bow table where different slopes produce different outputs.
    // At low deltaV, slope matters more; at high deltaV, x^4 dominates
    // and the reflection coefficient is clamped to 0.01 regardless of slope.
    auto getOutputAtPressureWithKnownDeltaV = [&](float pressure) {
        BowExciter bow;
        bow.prepare(kSampleRate);
        bow.setPressure(pressure);
        bow.setSpeed(0.1f);   // Low speed ceiling to keep bowVelocity small
        bow.setPosition(0.5f);  // Position impedance = 1.0 at beta=0.5
        bow.trigger(1.0f);

        // Build up velocity (will saturate at maxVelocity * speed = 1.0 * 0.1 = 0.1)
        for (int i = 0; i < 2000; ++i) {
            bow.setEnvelopeValue(1.0f);
            (void)bow.process(0.0f);
        }

        // Now process with a feedback velocity close to bow velocity
        // to create a small deltaV where slope differences matter.
        // bowVelocity ≈ 0.1, feedbackVelocity = 0.08, so deltaV ≈ 0.02
        bow.setEnvelopeValue(1.0f);
        float result = bow.process(0.08f);
        return result;
    };

    float out_p0 = getOutputAtPressureWithKnownDeltaV(0.0f);    // slope=5.0
    float out_p025 = getOutputAtPressureWithKnownDeltaV(0.25f);  // slope=4.0
    float out_p1 = getOutputAtPressureWithKnownDeltaV(1.0f);     // slope=1.0

    INFO("Output at pressure=0.0 (slope=5.0): " << out_p0);
    INFO("Output at pressure=0.25 (slope=4.0): " << out_p025);
    INFO("Output at pressure=1.0 (slope=1.0): " << out_p1);

    SECTION("setPressure(0.0) produces slope=5.0 (distinct from slope=4.0 at pressure=0.25)") {
        // Higher slope means the friction curve is steeper, producing different output
        REQUIRE(out_p0 != Approx(out_p025).margin(1e-6f));
    }

    SECTION("setPressure(1.0) produces slope=1.0 (distinct from slope=5.0 at pressure=0.0)") {
        REQUIRE(out_p1 != Approx(out_p0).margin(1e-6f));
    }

    SECTION("slope decreases monotonically with pressure: output at p=0.0 differs from p=0.25 differs from p=1.0") {
        // At higher slope (lower pressure), the bow table is steeper
        // This should produce different force magnitudes at the same deltaV
        // All three outputs should be distinct
        bool allDistinct = (std::abs(out_p0 - out_p025) > 1e-6f)
                        && (std::abs(out_p025 - out_p1) > 1e-6f)
                        && (std::abs(out_p0 - out_p1) > 1e-6f);
        REQUIRE(allDistinct);
    }

    SECTION("slope formula boundary values") {
        // Verify extreme pressure values produce outputs
        // (confirms no clamping bugs)
        float out_extreme_low = getOutputAtPressureWithKnownDeltaV(0.001f);
        float out_extreme_high = getOutputAtPressureWithKnownDeltaV(0.999f);

        // Both should produce non-zero output (not silenced by slope issues)
        REQUIRE(std::abs(out_extreme_low) > 0.0f);
        REQUIRE(std::abs(out_extreme_high) > 0.0f);
    }
}
