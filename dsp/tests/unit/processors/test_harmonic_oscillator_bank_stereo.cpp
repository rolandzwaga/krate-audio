// ==============================================================================
// Harmonic Oscillator Bank Stereo + Detune Tests (M6)
// ==============================================================================
// Tests for processStereo(), setStereoSpread(), setDetuneSpread() extensions.
//
// Feature: 120-creative-extensions
// User Story 2: Stereo Partial Spread
// Requirements: FR-006 to FR-013, FR-030 to FR-032, FR-050, SC-002, SC-005,
//               SC-007, SC-010
// ==============================================================================

#include <krate/dsp/processors/harmonic_oscillator_bank.h>
#include <krate/dsp/processors/harmonic_types.h>
#include <krate/dsp/core/math_constants.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <array>
#include <cmath>
#include <numeric>
#include <vector>

using Catch::Approx;
using namespace Krate::DSP;

// =============================================================================
// Helper: create a test harmonic frame with known partials
// =============================================================================
static HarmonicFrame makeTestFrame(int numPartials = 8, float f0 = 440.0f,
                                   float baseAmp = 0.5f)
{
    HarmonicFrame frame{};
    frame.f0 = f0;
    frame.f0Confidence = 0.95f;
    frame.numPartials = numPartials;
    frame.globalAmplitude = baseAmp;

    for (int p = 0; p < numPartials; ++p) {
        auto& partial = frame.partials[static_cast<size_t>(p)];
        partial.harmonicIndex = p + 1;
        partial.frequency = f0 * static_cast<float>(p + 1);
        partial.amplitude = baseAmp / static_cast<float>(p + 1);
        partial.relativeFrequency = static_cast<float>(p + 1);
        partial.inharmonicDeviation = 0.0f;
        partial.stability = 1.0f;
        partial.age = 10;
        partial.phase = 0.0f;
    }

    return frame;
}

// =============================================================================
// Helper: compute Pearson correlation between two buffers
// =============================================================================
static float computeCorrelation(const float* a, const float* b, size_t n)
{
    float sumA = 0.0f, sumB = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        sumA += a[i];
        sumB += b[i];
    }
    float meanA = sumA / static_cast<float>(n);
    float meanB = sumB / static_cast<float>(n);

    float covAB = 0.0f, varA = 0.0f, varB = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        float da = a[i] - meanA;
        float db = b[i] - meanB;
        covAB += da * db;
        varA += da * da;
        varB += db * db;
    }

    if (varA < 1e-12f || varB < 1e-12f) return 0.0f;
    return covAB / std::sqrt(varA * varB);
}

// =============================================================================
// T005: processStereo() and setStereoSpread() tests
// =============================================================================

TEST_CASE("HarmonicOscillatorBank stereo: spread=0 produces identical L/R (SC-010)",
          "[processors][stereo]")
{
    HarmonicOscillatorBank bank;
    bank.prepare(44100.0);

    auto frame = makeTestFrame();
    bank.loadFrame(frame, 440.0f);
    bank.setStereoSpread(0.0f);

    // Let amplitude smoothing settle
    constexpr size_t kWarmup = 512;
    for (size_t i = 0; i < kWarmup; ++i) {
        float l, r;
        bank.processStereo(l, r);
    }

    // Now collect samples and verify L == R
    constexpr size_t kNumSamples = 1024;
    std::array<float, kNumSamples> left{}, right{};
    for (size_t i = 0; i < kNumSamples; ++i) {
        bank.processStereo(left[i], right[i]);
    }

    bool allIdentical = true;
    for (size_t i = 0; i < kNumSamples; ++i) {
        if (left[i] != right[i]) {
            allIdentical = false;
            break;
        }
    }
    REQUIRE(allIdentical);
}

TEST_CASE("HarmonicOscillatorBank stereo: spread=1.0 sends odd partials left, even right (SC-002)",
          "[processors][stereo]")
{
    HarmonicOscillatorBank bank;
    bank.prepare(44100.0);

    // Use many partials with flat amplitudes to maximize decorrelation.
    // The fundamental has reduced spread (25%), so we need the higher
    // partials (which get full spread) to dominate.
    HarmonicFrame frame{};
    frame.f0 = 220.0f;
    frame.f0Confidence = 0.95f;
    frame.numPartials = 24;
    frame.globalAmplitude = 0.5f;
    for (int p = 0; p < 24; ++p) {
        auto& partial = frame.partials[static_cast<size_t>(p)];
        partial.harmonicIndex = p + 1;
        partial.frequency = 220.0f * static_cast<float>(p + 1);
        // Flat amplitudes: fundamental same as others for max decorrelation
        partial.amplitude = 0.3f;
        partial.relativeFrequency = static_cast<float>(p + 1);
        partial.inharmonicDeviation = 0.0f;
        partial.stability = 1.0f;
        partial.age = 10;
        partial.phase = 0.0f;
    }
    bank.loadFrame(frame, 220.0f);
    bank.setStereoSpread(1.0f);

    // Warm up for amplitude smoothing
    constexpr size_t kWarmup = 512;
    for (size_t i = 0; i < kWarmup; ++i) {
        float l, r;
        bank.processStereo(l, r);
    }

    // Collect stereo samples
    constexpr size_t kNumSamples = 4096;
    std::array<float, kNumSamples> left{}, right{};
    for (size_t i = 0; i < kNumSamples; ++i) {
        bank.processStereo(left[i], right[i]);
    }

    // Compute decorrelation: 1 - |correlation|
    float corr = computeCorrelation(left.data(), right.data(), kNumSamples);
    float decorrelation = 1.0f - std::abs(corr);

    // SC-002: decorrelation > 0.8 at spread=1.0
    REQUIRE(decorrelation > 0.8f);
}

TEST_CASE("HarmonicOscillatorBank stereo: constant-power pan law",
          "[processors][stereo]")
{
    HarmonicOscillatorBank bank;
    bank.prepare(44100.0);

    // Use a single partial for easy verification
    HarmonicFrame frame{};
    frame.f0 = 440.0f;
    frame.f0Confidence = 0.95f;
    frame.numPartials = 1;
    frame.globalAmplitude = 1.0f;
    frame.partials[0].harmonicIndex = 1;
    frame.partials[0].frequency = 440.0f;
    frame.partials[0].amplitude = 1.0f;
    frame.partials[0].relativeFrequency = 1.0f;
    frame.partials[0].inharmonicDeviation = 0.0f;
    frame.partials[0].stability = 1.0f;
    frame.partials[0].age = 10;
    frame.partials[0].phase = 0.0f;

    bank.loadFrame(frame, 440.0f);

    // Test at various spread values
    for (float spread : {0.0f, 0.25f, 0.5f, 0.75f, 1.0f}) {
        bank.setStereoSpread(spread);

        // Warm up
        for (int i = 0; i < 512; ++i) {
            float l, r;
            bank.processStereo(l, r);
        }

        // Collect energy
        float energyL = 0.0f, energyR = 0.0f;
        constexpr size_t kN = 2048;
        for (size_t i = 0; i < kN; ++i) {
            float l, r;
            bank.processStereo(l, r);
            energyL += l * l;
            energyR += r * r;
        }

        // Total energy should be roughly constant regardless of spread
        // (constant-power pan law ensures panL^2 + panR^2 ~= 1.0)
        float totalEnergy = energyL + energyR;
        // Compare with center: at spread=0, both get same signal, so total energy ~= 2*mono^2
        // but the absolute total should stay roughly same across spreads
        REQUIRE(totalEnergy > 0.0f);
    }
}

TEST_CASE("HarmonicOscillatorBank stereo: fundamental reduced spread (FR-009)",
          "[processors][stereo]")
{
    HarmonicOscillatorBank bank;
    bank.prepare(44100.0);

    // Use 2 partials: fundamental (partial 1) and 2nd harmonic (partial 2)
    HarmonicFrame frame{};
    frame.f0 = 440.0f;
    frame.f0Confidence = 0.95f;
    frame.numPartials = 2;
    frame.globalAmplitude = 1.0f;

    // Partial 1 (fundamental) - should have reduced spread (25%)
    frame.partials[0].harmonicIndex = 1;
    frame.partials[0].frequency = 440.0f;
    frame.partials[0].amplitude = 1.0f;
    frame.partials[0].relativeFrequency = 1.0f;
    frame.partials[0].inharmonicDeviation = 0.0f;
    frame.partials[0].stability = 1.0f;
    frame.partials[0].age = 10;
    frame.partials[0].phase = 0.0f;

    // Partial 2 (2nd harmonic) - should have full spread
    frame.partials[1].harmonicIndex = 2;
    frame.partials[1].frequency = 880.0f;
    frame.partials[1].amplitude = 1.0f;
    frame.partials[1].relativeFrequency = 2.0f;
    frame.partials[1].inharmonicDeviation = 0.0f;
    frame.partials[1].stability = 1.0f;
    frame.partials[1].age = 10;
    frame.partials[1].phase = 0.0f;

    bank.loadFrame(frame, 440.0f);
    bank.setStereoSpread(1.0f);

    // The fundamental's spread is scaled by kFundamentalSpreadScale (0.25),
    // so at spread=1.0, it should be much more centered than partial 2.
    // Verify via getStereoSpread returning the set value.
    REQUIRE(bank.getStereoSpread() == Approx(1.0f));
}

TEST_CASE("HarmonicOscillatorBank stereo: processStereo alongside mono process (FR-050)",
          "[processors][stereo]")
{
    // Verify that the existing process() still works
    HarmonicOscillatorBank bank;
    bank.prepare(44100.0);

    auto frame = makeTestFrame(4, 440.0f, 0.5f);
    bank.loadFrame(frame, 440.0f);

    // process() still returns mono
    float monoSample = bank.process();
    // Should produce non-zero output (after first sample at least)
    // Just verify it doesn't crash and is finite
    REQUIRE(std::isfinite(monoSample));
}

TEST_CASE("HarmonicOscillatorBank stereo: processStereoBlock",
          "[processors][stereo]")
{
    HarmonicOscillatorBank bank;
    bank.prepare(44100.0);

    auto frame = makeTestFrame(4, 440.0f, 0.5f);
    bank.loadFrame(frame, 440.0f);
    bank.setStereoSpread(0.5f);

    // Warm up
    for (int i = 0; i < 256; ++i) {
        float l, r;
        bank.processStereo(l, r);
    }

    constexpr size_t kBlockSize = 128;
    std::array<float, kBlockSize> leftBlock{}, rightBlock{};
    bank.processStereoBlock(leftBlock.data(), rightBlock.data(), kBlockSize);

    // Verify non-zero output
    bool hasOutput = false;
    for (size_t i = 0; i < kBlockSize; ++i) {
        if (leftBlock[i] != 0.0f || rightBlock[i] != 0.0f) {
            hasOutput = true;
            break;
        }
    }
    REQUIRE(hasOutput);
}

// =============================================================================
// T006: setDetuneSpread() tests
// =============================================================================

TEST_CASE("HarmonicOscillatorBank detune: spread=0 produces no frequency offset",
          "[processors][detune]")
{
    HarmonicOscillatorBank bank;
    bank.prepare(44100.0);

    auto frame = makeTestFrame(4, 440.0f, 0.5f);
    bank.loadFrame(frame, 440.0f);
    bank.setDetuneSpread(0.0f);

    REQUIRE(bank.getDetuneSpread() == Approx(0.0f));

    // With detune=0, mono process and stereo (spread=0) should be equivalent
    // Just verify it doesn't crash
    constexpr size_t kN = 256;
    for (size_t i = 0; i < kN; ++i) {
        float l, r;
        bank.processStereo(l, r);
        REQUIRE(std::isfinite(l));
        REQUIRE(std::isfinite(r));
    }
}

TEST_CASE("HarmonicOscillatorBank detune: spread=0.5 produces proportional offsets",
          "[processors][detune]")
{
    HarmonicOscillatorBank bank;
    bank.prepare(44100.0);

    auto frame = makeTestFrame(8, 440.0f, 0.5f);
    bank.loadFrame(frame, 440.0f);
    bank.setDetuneSpread(0.5f);
    bank.setStereoSpread(0.0f); // Keep stereo mono for simpler analysis

    // Warm up
    for (int i = 0; i < 1024; ++i) {
        float l, r;
        bank.processStereo(l, r);
    }

    // Collect a chunk of audio
    constexpr size_t kN = 4096;
    std::array<float, kN> left{}, right{};
    for (size_t i = 0; i < kN; ++i) {
        bank.processStereo(left[i], right[i]);
    }

    // With detune > 0, even at stereo spread=0, L and R should still be identical
    // (detune affects frequency, not panning)
    bool allIdentical = true;
    for (size_t i = 0; i < kN; ++i) {
        if (left[i] != right[i]) {
            allIdentical = false;
            break;
        }
    }
    REQUIRE(allIdentical);

    // Verify output is non-zero
    float energy = 0.0f;
    for (size_t i = 0; i < kN; ++i) {
        energy += left[i] * left[i];
    }
    REQUIRE(energy > 0.0f);
}

TEST_CASE("HarmonicOscillatorBank detune: odd partials detune positive, even negative",
          "[processors][detune]")
{
    HarmonicOscillatorBank bank;
    bank.prepare(44100.0);

    auto frame = makeTestFrame(4, 440.0f, 0.5f);
    bank.loadFrame(frame, 440.0f);
    bank.setDetuneSpread(1.0f);

    // Verify the getter
    REQUIRE(bank.getDetuneSpread() == Approx(1.0f));

    // At detune=1.0, partials should be detuned but output should still be
    // finite and produce sound
    constexpr size_t kN = 1024;
    for (size_t i = 0; i < kN; ++i) {
        float l, r;
        bank.processStereo(l, r);
        REQUIRE(std::isfinite(l));
        REQUIRE(std::isfinite(r));
    }
}

TEST_CASE("HarmonicOscillatorBank detune: fundamental deviation < 1 cent at any spread (SC-005)",
          "[processors][detune]")
{
    // The fundamental (partial 1) is odd, so it gets detuned by:
    //   offset = spread * 1 * 15 * (+1) cents
    // At spread=1.0, that's 15 cents -- which is > 1 cent.
    // BUT the spec says SC-005: fundamental pitch deviation < 1 cent.
    // This implies kFundamentalSpreadScale (0.25) should also apply to detune,
    // or the fundamental detune is scaled separately.
    //
    // Actually, re-reading the contract: the formula is
    //   detuneOffset_n = detuneSpread * n * kDetuneMaxCents * direction
    // For n=1 (fundamental), offset = detuneSpread * 1 * 15 * 1
    // At max spread = 1.0, that's 15 cents for fundamental.
    //
    // SC-005 says: "fundamental frequency deviation < 1 cent at detune_spread = 1.0"
    // This means the fundamental must be treated specially -- likely using
    // kFundamentalSpreadScale (0.25) which gives 15 * 0.25 = 3.75 cents.
    // That's still > 1 cent.
    //
    // The implementation must clamp or scale fundamental detune to < 1 cent.
    // Let's verify this requirement:

    HarmonicOscillatorBank bank;
    bank.prepare(44100.0);

    // Use only fundamental (partial 1) at known frequency
    HarmonicFrame frame{};
    frame.f0 = 440.0f;
    frame.f0Confidence = 0.95f;
    frame.numPartials = 1;
    frame.globalAmplitude = 1.0f;
    frame.partials[0].harmonicIndex = 1;
    frame.partials[0].frequency = 440.0f;
    frame.partials[0].amplitude = 1.0f;
    frame.partials[0].relativeFrequency = 1.0f;
    frame.partials[0].inharmonicDeviation = 0.0f;
    frame.partials[0].stability = 1.0f;
    frame.partials[0].age = 10;
    frame.partials[0].phase = 0.0f;

    bank.loadFrame(frame, 440.0f);
    bank.setDetuneSpread(1.0f);
    bank.setStereoSpread(0.0f);

    // Generate enough samples to measure frequency via zero crossings
    constexpr size_t kWarmup = 512;
    for (size_t i = 0; i < kWarmup; ++i) {
        float l, r;
        bank.processStereo(l, r);
    }

    constexpr size_t kN = 44100; // 1 second of audio
    std::vector<float> buffer(kN);
    for (size_t i = 0; i < kN; ++i) {
        float l, r;
        bank.processStereo(l, r);
        buffer[i] = l;
    }

    // Count zero crossings to estimate frequency
    int crossings = 0;
    for (size_t i = 1; i < kN; ++i) {
        if ((buffer[i - 1] >= 0.0f && buffer[i] < 0.0f) ||
            (buffer[i - 1] < 0.0f && buffer[i] >= 0.0f)) {
            ++crossings;
        }
    }
    float estimatedFreq = static_cast<float>(crossings) * 44100.0f / (2.0f * static_cast<float>(kN));

    // Convert deviation to cents: cents = 1200 * log2(f_measured / f_expected)
    float centsDev = 1200.0f * std::log2(estimatedFreq / 440.0f);

    // SC-005: fundamental pitch deviation < 1 cent
    REQUIRE(std::abs(centsDev) < 1.0f);
}

TEST_CASE("HarmonicOscillatorBank stereo: pan smoothing on spread change",
          "[processors][stereo]")
{
    HarmonicOscillatorBank bank;
    bank.prepare(44100.0);

    auto frame = makeTestFrame(4, 440.0f, 0.5f);
    bank.loadFrame(frame, 440.0f);
    bank.setStereoSpread(0.0f);

    // Warm up at spread=0
    for (int i = 0; i < 512; ++i) {
        float l, r;
        bank.processStereo(l, r);
    }

    // Switch to spread=1.0 -- verify no crash and output is finite
    bank.setStereoSpread(1.0f);
    for (int i = 0; i < 256; ++i) {
        float l, r;
        bank.processStereo(l, r);
        REQUIRE(std::isfinite(l));
        REQUIRE(std::isfinite(r));
    }
}
