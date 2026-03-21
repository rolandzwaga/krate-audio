// ==============================================================================
// ImpactExciter Unit Tests
// ==============================================================================
// Layer 2: Processors | Spec 128 - Impact Exciter
//
// Tests for:
//  - Lifecycle (prepare, reset, isActive, isPrepared)
//  - Pulse shape mathematics (skew, duration, amplitude)
//  - SVF lowpass and brightness trim
//  - Strike position comb filter
//
// Constitution Principle XII: Test-First Development
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/processors/impact_exciter.h>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

using Catch::Approx;

namespace {

constexpr double kSampleRate = 44100.0;
constexpr int kBlockSize = 2048;

// Helper: generate a block from the exciter after triggering
std::vector<float> generateExciterBlock(
    Krate::DSP::ImpactExciter& exciter,
    float velocity, float hardness, float mass,
    float brightness, float position, float f0,
    int numSamples = kBlockSize)
{
    std::vector<float> buffer(static_cast<size_t>(numSamples), 0.0f);
    exciter.trigger(velocity, hardness, mass, brightness, position, f0);
    exciter.processBlock(buffer.data(), numSamples);
    return buffer;
}

// Helper: find the sample index of the peak absolute value
int findPeakIndex(const std::vector<float>& buffer)
{
    int peakIdx = 0;
    float peakVal = 0.0f;
    for (int i = 0; i < static_cast<int>(buffer.size()); ++i) {
        float absVal = std::abs(buffer[static_cast<size_t>(i)]);
        if (absVal > peakVal) {
            peakVal = absVal;
            peakIdx = i;
        }
    }
    return peakIdx;
}

// Helper: find the peak absolute value
float findPeakAmplitude(const std::vector<float>& buffer)
{
    float peak = 0.0f;
    for (float s : buffer)
        peak = std::max(peak, std::abs(s));
    return peak;
}

// Helper: find the index where signal first reaches a fraction of peak
int findRiseTimeIndex(const std::vector<float>& buffer, float fraction)
{
    float peak = findPeakAmplitude(buffer);
    float threshold = peak * fraction;
    for (int i = 0; i < static_cast<int>(buffer.size()); ++i) {
        if (std::abs(buffer[static_cast<size_t>(i)]) >= threshold)
            return i;
    }
    return static_cast<int>(buffer.size());
}

// Helper: find the last non-zero sample index
int findLastNonZeroIndex(const std::vector<float>& buffer)
{
    for (int i = static_cast<int>(buffer.size()) - 1; i >= 0; --i) {
        if (std::abs(buffer[static_cast<size_t>(i)]) > 1e-10f)
            return i;
    }
    return -1;
}

// Helper: estimate spectral centroid via FFT-like approach
// Uses simple DFT magnitude spectrum to compute weighted average frequency.
// This is test infrastructure only.
float estimateSpectralCentroid(const std::vector<float>& buffer, double sampleRate)
{
    // Use the whole buffer or up to 2048 samples
    size_t N = std::min(buffer.size(), static_cast<size_t>(2048));

    // Compute magnitude spectrum via DFT (simplified, only up to Nyquist)
    size_t numBins = N / 2;
    std::vector<float> magnitudes(numBins, 0.0f);

    for (size_t k = 1; k < numBins; ++k) {
        float realPart = 0.0f;
        float imagPart = 0.0f;
        for (size_t n = 0; n < N; ++n) {
            float angle = static_cast<float>(2.0 * 3.14159265358979323846 * k * n / N);
            realPart += buffer[n] * std::cos(angle);
            imagPart -= buffer[n] * std::sin(angle);
        }
        magnitudes[k] = std::sqrt(realPart * realPart + imagPart * imagPart);
    }

    // Weighted average frequency
    float weightedSum = 0.0f;
    float totalMag = 0.0f;
    for (size_t k = 1; k < numBins; ++k) {
        float freq = static_cast<float>(k * sampleRate / N);
        weightedSum += freq * magnitudes[k];
        totalMag += magnitudes[k];
    }

    return (totalMag > 0.0f) ? (weightedSum / totalMag) : 0.0f;
}

// Helper: measure magnitude at a specific harmonic
float measureHarmonicMagnitude(const std::vector<float>& buffer, double sampleRate, float harmonicFreq)
{
    size_t N = std::min(buffer.size(), static_cast<size_t>(2048));
    float realPart = 0.0f;
    float imagPart = 0.0f;
    for (size_t n = 0; n < N; ++n) {
        float angle = static_cast<float>(2.0 * 3.14159265358979323846 * harmonicFreq * n / sampleRate);
        realPart += buffer[n] * std::cos(angle);
        imagPart -= buffer[n] * std::sin(angle);
    }
    return std::sqrt(realPart * realPart + imagPart * imagPart);
}

} // anonymous namespace

// =============================================================================
// T013: Lifecycle and pulse shape tests
// =============================================================================

TEST_CASE("ImpactExciter prepare transitions to isPrepared", "[processors][impact_exciter]")
{
    Krate::DSP::ImpactExciter exciter;
    REQUIRE_FALSE(exciter.isPrepared());

    exciter.prepare(kSampleRate, 0);
    REQUIRE(exciter.isPrepared());
}

TEST_CASE("ImpactExciter isActive tracks pulse lifecycle", "[processors][impact_exciter]")
{
    Krate::DSP::ImpactExciter exciter;
    exciter.prepare(kSampleRate, 0);

    // Not active before trigger
    REQUIRE_FALSE(exciter.isActive());

    // Active after trigger
    exciter.trigger(0.7f, 0.5f, 0.3f, 0.0f, 0.13f, 440.0f);
    REQUIRE(exciter.isActive());

    // Process until pulse completes -- at mass=0.3, T ~= 0.5 + 14.5 * pow(0.3, 0.4) ~ 8.6ms
    // At 44100 Hz: ~380 samples. Process extra for bounce.
    // Use generous count of 2048 samples (~46ms) to ensure full completion.
    for (int i = 0; i < kBlockSize; ++i) {
        (void)exciter.process();
    }

    REQUIRE_FALSE(exciter.isActive());
}

TEST_CASE("ImpactExciter processBlock produces non-zero then zero output", "[processors][impact_exciter]")
{
    Krate::DSP::ImpactExciter exciter;
    exciter.prepare(kSampleRate, 0);

    auto buffer = generateExciterBlock(exciter, 0.7f, 0.8f, 0.3f, 0.0f, 0.13f, 440.0f, kBlockSize);

    // Should have non-zero output during pulse
    float peak = findPeakAmplitude(buffer);
    REQUIRE(peak > 0.01f);

    // After sufficient samples, output should be zero
    // The pulse at mass=0.3 is ~380 samples. By sample 1500, it should be done.
    bool allZeroAfterPulse = true;
    for (int i = 1500; i < kBlockSize; ++i) {
        if (std::abs(buffer[static_cast<size_t>(i)]) > 1e-10f) {
            allZeroAfterPulse = false;
            break;
        }
    }
    REQUIRE(allZeroAfterPulse);
}

TEST_CASE("ImpactExciter pulse is zero after maximum duration at all mass settings", "[processors][impact_exciter]")
{
    Krate::DSP::ImpactExciter exciter;
    exciter.prepare(kSampleRate, 0);

    // T_max = 15ms, plus bounce delay up to 2ms + bounce duration
    // At mass=1.0, T=15ms. Bounce can add ~2ms delay + 15ms bounce = 32ms max.
    // At 44100: ~1412 samples. Use 2048 to be safe.
    // With micro-variation, T can be 5% longer, so ~1482 samples max.
    for (float mass : {0.0f, 0.3f, 0.5f, 1.0f}) {
        exciter.reset();
        auto buffer = generateExciterBlock(exciter, 0.7f, 0.5f, mass, 0.0f, 0.13f, 440.0f, kBlockSize);

        // All samples after 1800 should be zero (generous bound)
        bool allZero = true;
        for (int i = 1800; i < kBlockSize; ++i) {
            if (std::abs(buffer[static_cast<size_t>(i)]) > 1e-10f) {
                allZero = false;
                break;
            }
        }
        REQUIRE(allZero);
    }
}

TEST_CASE("ImpactExciter reset clears active state", "[processors][impact_exciter]")
{
    Krate::DSP::ImpactExciter exciter;
    exciter.prepare(kSampleRate, 0);

    exciter.trigger(0.7f, 0.5f, 0.3f, 0.0f, 0.13f, 440.0f);
    REQUIRE(exciter.isActive());

    exciter.reset();
    REQUIRE_FALSE(exciter.isActive());

    // All subsequent process() calls return 0
    for (int i = 0; i < 100; ++i) {
        REQUIRE(exciter.process() == Approx(0.0f).margin(1e-10f));
    }
}

// =============================================================================
// T014: Pulse shape mathematics tests
// =============================================================================

TEST_CASE("ImpactExciter pulse peak occurs earlier at high hardness", "[processors][impact_exciter]")
{
    Krate::DSP::ImpactExciter exciter;
    exciter.prepare(kSampleRate, 0);

    // Hard strike (hardness=0.9): peak should be earlier due to skew
    auto bufferHard = generateExciterBlock(exciter, 0.7f, 0.9f, 0.3f, 0.0f, 0.0f, 440.0f);
    int peakHard = findPeakIndex(bufferHard);

    exciter.reset();

    // Soft strike (hardness=0.1): peak should be later (more symmetric)
    auto bufferSoft = generateExciterBlock(exciter, 0.7f, 0.1f, 0.3f, 0.0f, 0.0f, 440.0f);
    int peakSoft = findPeakIndex(bufferSoft);

    // Hard strike peak should be earlier than soft strike peak
    REQUIRE(peakHard < peakSoft);
}

TEST_CASE("ImpactExciter pulse duration increases with mass", "[processors][impact_exciter]")
{
    Krate::DSP::ImpactExciter exciter;
    exciter.prepare(kSampleRate, 0);

    // Low mass (short pulse)
    auto bufferLow = generateExciterBlock(exciter, 0.7f, 0.5f, 0.0f, 0.0f, 0.0f, 440.0f, kBlockSize);
    int lastLow = findLastNonZeroIndex(bufferLow);

    exciter.reset();

    // High mass (long pulse)
    auto bufferHigh = generateExciterBlock(exciter, 0.7f, 0.5f, 1.0f, 0.0f, 0.0f, 440.0f, kBlockSize);
    int lastHigh = findLastNonZeroIndex(bufferHigh);

    // High mass should produce a longer pulse
    REQUIRE(lastHigh > lastLow);
}

TEST_CASE("ImpactExciter amplitude scales nonlinearly with velocity", "[processors][impact_exciter]")
{
    Krate::DSP::ImpactExciter exciter;
    exciter.prepare(kSampleRate, 0);

    // Velocity 1.0: amplitude = pow(1.0, 0.6) = 1.0
    auto bufferFull = generateExciterBlock(exciter, 1.0f, 0.5f, 0.3f, 0.0f, 0.0f, 440.0f);
    float peakFull = findPeakAmplitude(bufferFull);

    exciter.reset();

    // Velocity 0.25: amplitude = pow(0.25, 0.6) ~ 0.38 (NOT 0.25)
    auto bufferQuarter = generateExciterBlock(exciter, 0.25f, 0.5f, 0.3f, 0.0f, 0.0f, 440.0f);
    float peakQuarter = findPeakAmplitude(bufferQuarter);

    // The ratio should NOT be 4:1 (linear). With pow(v, 0.6):
    // pow(1.0, 0.6) / pow(0.25, 0.6) = 1.0 / 0.38 ~ 2.63
    // So the ratio should be between 2 and 3.5 (nonlinear compression)
    float ratio = peakFull / peakQuarter;
    REQUIRE(ratio > 2.0f);
    REQUIRE(ratio < 3.5f);
}

TEST_CASE("ImpactExciter rise-time is shorter at high hardness (SC-003)", "[processors][impact_exciter]")
{
    // SC-003: Hard strikes have snappier attack than soft strikes.
    // Measure the ratio of energy in the first 2ms vs the total pulse energy.
    // A snappier attack concentrates more energy in the initial portion.

    Krate::DSP::ImpactExciter exciterHard;
    exciterHard.prepare(kSampleRate, 42);
    auto bufferHard = generateExciterBlock(exciterHard, 0.7f, 0.9f, 0.5f, 0.0f, 0.0f, 440.0f);

    Krate::DSP::ImpactExciter exciterSoft;
    exciterSoft.prepare(kSampleRate, 42);
    auto bufferSoft = generateExciterBlock(exciterSoft, 0.7f, 0.1f, 0.5f, 0.0f, 0.0f, 440.0f);

    // Compute early energy ratio (first 2ms = ~88 samples at 44.1kHz)
    int earlyWindow = static_cast<int>(0.002 * kSampleRate);
    auto earlyEnergyRatio = [&](const std::vector<float>& buf) {
        float earlyEnergy = 0.0f;
        float totalEnergy = 0.0f;
        for (size_t i = 0; i < buf.size(); ++i) {
            float e = buf[i] * buf[i];
            totalEnergy += e;
            if (static_cast<int>(i) < earlyWindow)
                earlyEnergy += e;
        }
        return (totalEnergy > 0.0f) ? earlyEnergy / totalEnergy : 0.0f;
    };

    float ratioHard = earlyEnergyRatio(bufferHard);
    float ratioSoft = earlyEnergyRatio(bufferSoft);

    // Hard strike should have more energy concentrated in the early window
    REQUIRE(ratioHard > ratioSoft);
}

// =============================================================================
// T015: SVF lowpass and brightness trim tests
// =============================================================================

TEST_CASE("ImpactExciter spectral centroid increases with hardness", "[processors][impact_exciter]")
{
    Krate::DSP::ImpactExciter exciter;
    exciter.prepare(kSampleRate, 0);

    // Soft: hardness=0.0 (SVF cutoff ~500 Hz)
    auto bufferSoft = generateExciterBlock(exciter, 0.7f, 0.0f, 0.3f, 0.0f, 0.0f, 440.0f);
    float centroidSoft = estimateSpectralCentroid(bufferSoft, kSampleRate);

    exciter.reset();

    // Hard: hardness=1.0 (SVF cutoff ~12 kHz)
    auto bufferHard = generateExciterBlock(exciter, 0.7f, 1.0f, 0.3f, 0.0f, 0.0f, 440.0f);
    float centroidHard = estimateSpectralCentroid(bufferHard, kSampleRate);

    // Hard should have higher centroid
    REQUIRE(centroidHard > centroidSoft);
}

TEST_CASE("ImpactExciter brightness trim at default preserves hardness mapping", "[processors][impact_exciter]")
{
    Krate::DSP::ImpactExciter exciter;
    exciter.prepare(kSampleRate, 0);

    // Brightness = 0.0 (default, no trim)
    auto bufferDefault = generateExciterBlock(exciter, 0.7f, 0.5f, 0.3f, 0.0f, 0.0f, 440.0f);
    float centroidDefault = estimateSpectralCentroid(bufferDefault, kSampleRate);

    // Centroid should be a reasonable value (not NaN, not zero, not extreme)
    REQUIRE(centroidDefault > 100.0f);
    REQUIRE(centroidDefault < 20000.0f);
}

TEST_CASE("ImpactExciter brightness trim shifts spectral centroid", "[processors][impact_exciter]")
{
    Krate::DSP::ImpactExciter exciter;
    exciter.prepare(kSampleRate, 0);

    // Brightness = 0.0 (neutral)
    auto bufferNeutral = generateExciterBlock(exciter, 0.7f, 0.5f, 0.3f, 0.0f, 0.0f, 440.0f);
    float centroidNeutral = estimateSpectralCentroid(bufferNeutral, kSampleRate);

    exciter.reset();

    // Brightness = +1.0 (brighter, +12 semitones)
    auto bufferBright = generateExciterBlock(exciter, 0.7f, 0.5f, 0.3f, 1.0f, 0.0f, 440.0f);
    float centroidBright = estimateSpectralCentroid(bufferBright, kSampleRate);

    exciter.reset();

    // Brightness = -1.0 (darker, -12 semitones)
    auto bufferDark = generateExciterBlock(exciter, 0.7f, 0.5f, 0.3f, -1.0f, 0.0f, 440.0f);
    float centroidDark = estimateSpectralCentroid(bufferDark, kSampleRate);

    // Bright > Neutral > Dark
    REQUIRE(centroidBright > centroidNeutral);
    REQUIRE(centroidNeutral > centroidDark);
}

// =============================================================================
// T016: Strike position comb filter tests
// =============================================================================

TEST_CASE("ImpactExciter position 0.0 and 0.5 produce different spectra", "[processors][impact_exciter]")
{
    // Use separate instances with same seed so pulse shape is identical
    Krate::DSP::ImpactExciter exciterEdge;
    exciterEdge.prepare(kSampleRate, 77);
    auto bufferEdge = generateExciterBlock(exciterEdge, 0.7f, 0.5f, 0.3f, 0.0f, 0.0f, 440.0f);

    Krate::DSP::ImpactExciter exciterCenter;
    exciterCenter.prepare(kSampleRate, 77);
    auto bufferCenter = generateExciterBlock(exciterCenter, 0.7f, 0.5f, 0.3f, 0.0f, 0.5f, 440.0f);

    // They should produce different output
    bool identical = true;
    for (size_t i = 0; i < bufferEdge.size(); ++i) {
        if (std::abs(bufferEdge[i] - bufferCenter[i]) > 1e-6f) {
            identical = false;
            break;
        }
    }
    REQUIRE_FALSE(identical);
}

TEST_CASE("ImpactExciter position 0.5 attenuates even harmonics (SC-008)", "[processors][impact_exciter]")
{
    float f0 = 440.0f;

    // Use separate exciter instances with same seed to isolate the position effect
    Krate::DSP::ImpactExciter exciterEdge;
    exciterEdge.prepare(kSampleRate, 99);
    auto bufferEdge = generateExciterBlock(exciterEdge, 0.7f, 0.5f, 0.3f, 0.0f, 0.0f, f0);

    Krate::DSP::ImpactExciter exciterCenter;
    exciterCenter.prepare(kSampleRate, 99);
    auto bufferCenter = generateExciterBlock(exciterCenter, 0.7f, 0.5f, 0.3f, 0.0f, 0.5f, f0);

    // Measure 2nd harmonic (880 Hz) magnitude in both
    float evenHarmEdge = measureHarmonicMagnitude(bufferEdge, kSampleRate, f0 * 2.0f);
    float evenHarmCenter = measureHarmonicMagnitude(bufferCenter, kSampleRate, f0 * 2.0f);

    // Center position should attenuate even harmonics relative to edge
    // With 70% wet blend, attenuation won't be complete but should be measurable
    REQUIRE(evenHarmCenter < evenHarmEdge * 0.8f);
}

TEST_CASE("ImpactExciter position 0.13 produces output with all harmonics", "[processors][impact_exciter]")
{
    Krate::DSP::ImpactExciter exciter;
    exciter.prepare(kSampleRate, 0);

    float f0 = 440.0f;

    auto buffer = generateExciterBlock(exciter, 0.7f, 0.5f, 0.3f, 0.0f, 0.13f, f0);

    // Check that both fundamental region and harmonics are present
    float mag1 = measureHarmonicMagnitude(buffer, kSampleRate, f0);
    float mag2 = measureHarmonicMagnitude(buffer, kSampleRate, f0 * 2.0f);
    float mag3 = measureHarmonicMagnitude(buffer, kSampleRate, f0 * 3.0f);

    // All should be present (non-zero)
    REQUIRE(mag1 > 0.001f);
    REQUIRE(mag2 > 0.001f);
    REQUIRE(mag3 > 0.001f);
}

TEST_CASE("ImpactExciter comb blend is neither fully dry nor fully wet", "[processors][impact_exciter]")
{
    Krate::DSP::ImpactExciter exciter;
    exciter.prepare(kSampleRate, 0);

    float f0 = 440.0f;

    // Position 0.0 (effectively dry -- comb delay is 0)
    auto bufferDry = generateExciterBlock(exciter, 0.7f, 0.5f, 0.3f, 0.0f, 0.0f, f0);

    exciter.reset();

    // Position 0.5 (strong comb effect with 70/30 blend)
    auto bufferBlend = generateExciterBlock(exciter, 0.7f, 0.5f, 0.3f, 0.0f, 0.5f, f0);

    // Output should differ from dry (not identical)
    bool differFromDry = false;
    for (size_t i = 0; i < bufferDry.size(); ++i) {
        if (std::abs(bufferDry[i] - bufferBlend[i]) > 1e-6f) {
            differFromDry = true;
            break;
        }
    }
    REQUIRE(differFromDry);

    // The blended output should still have some energy at even harmonics
    // (not a perfect null), proving it's not fully wet
    float evenHarmBlend = measureHarmonicMagnitude(bufferBlend, kSampleRate, f0 * 2.0f);
    REQUIRE(evenHarmBlend > 0.001f);
}

// =============================================================================
// T024: Multi-dimensional velocity response tests (User Story 2)
// =============================================================================

TEST_CASE("ImpactExciter velocity 1.0 vs 0.2: higher peak amplitude at full velocity (SC-004)",
          "[processors][impact_exciter][velocity]")
{
    Krate::DSP::ImpactExciter exciterHigh;
    exciterHigh.prepare(kSampleRate, 50);
    auto bufHigh = generateExciterBlock(exciterHigh, 1.0f, 0.5f, 0.3f, 0.0f, 0.0f, 440.0f);
    float peakHigh = findPeakAmplitude(bufHigh);

    Krate::DSP::ImpactExciter exciterLow;
    exciterLow.prepare(kSampleRate, 50);
    auto bufLow = generateExciterBlock(exciterLow, 0.2f, 0.5f, 0.3f, 0.0f, 0.0f, 440.0f);
    float peakLow = findPeakAmplitude(bufLow);

    // Higher velocity must produce higher amplitude
    REQUIRE(peakHigh > peakLow);

    // Ratio should reflect nonlinear pow(v, 0.6): pow(1.0, 0.6)/pow(0.2, 0.6) ~ 2.89
    // Allow wide margin due to SVF and noise effects
    float ratio = peakHigh / peakLow;
    REQUIRE(ratio > 1.5f);
}

TEST_CASE("ImpactExciter velocity 1.0 vs 0.2: higher spectral centroid at full velocity (SC-004)",
          "[processors][impact_exciter][velocity]")
{
    // Use separate instances with same voice seed to isolate velocity effect
    Krate::DSP::ImpactExciter exciterHigh;
    exciterHigh.prepare(kSampleRate, 60);
    auto bufHigh = generateExciterBlock(exciterHigh, 1.0f, 0.5f, 0.3f, 0.0f, 0.0f, 440.0f);
    float centroidHigh = estimateSpectralCentroid(bufHigh, kSampleRate);

    Krate::DSP::ImpactExciter exciterLow;
    exciterLow.prepare(kSampleRate, 60);
    auto bufLow = generateExciterBlock(exciterLow, 0.2f, 0.5f, 0.3f, 0.0f, 0.0f, 440.0f);
    float centroidLow = estimateSpectralCentroid(bufLow, kSampleRate);

    // Higher velocity = higher cutoff from FR-019, so brighter output
    REQUIRE(centroidHigh > centroidLow);
}

TEST_CASE("ImpactExciter velocity 1.0 vs 0.2: shorter pulse duration at full velocity (SC-004)",
          "[processors][impact_exciter][velocity]")
{
    // FR-020: T *= pow(1 - v, 0.2), so higher velocity gives shorter T
    Krate::DSP::ImpactExciter exciterHigh;
    exciterHigh.prepare(kSampleRate, 70);
    auto bufHigh = generateExciterBlock(exciterHigh, 1.0f, 0.5f, 0.5f, 0.0f, 0.0f, 440.0f);
    int lastHigh = findLastNonZeroIndex(bufHigh);

    Krate::DSP::ImpactExciter exciterLow;
    exciterLow.prepare(kSampleRate, 70);
    auto bufLow = generateExciterBlock(exciterLow, 0.2f, 0.5f, 0.5f, 0.0f, 0.0f, 440.0f);
    int lastLow = findLastNonZeroIndex(bufLow);

    // Full velocity pulse should end sooner
    REQUIRE(lastHigh < lastLow);
}

TEST_CASE("ImpactExciter velocity coupling is nonlinear: centroid at 0.5 is NOT midway (FR-021)",
          "[processors][impact_exciter][velocity]")
{
    // SC-004, FR-021: Exponential/log curves, not linear
    Krate::DSP::ImpactExciter excLow;
    excLow.prepare(kSampleRate, 80);
    auto bufLow = generateExciterBlock(excLow, 0.0001f, 0.5f, 0.3f, 0.0f, 0.0f, 440.0f);
    float centroidLow = estimateSpectralCentroid(bufLow, kSampleRate);

    Krate::DSP::ImpactExciter excMid;
    excMid.prepare(kSampleRate, 80);
    auto bufMid = generateExciterBlock(excMid, 0.5f, 0.5f, 0.3f, 0.0f, 0.0f, 440.0f);
    float centroidMid = estimateSpectralCentroid(bufMid, kSampleRate);

    Krate::DSP::ImpactExciter excHigh;
    excHigh.prepare(kSampleRate, 80);
    auto bufHigh = generateExciterBlock(excHigh, 1.0f, 0.5f, 0.3f, 0.0f, 0.0f, 440.0f);
    float centroidHigh = estimateSpectralCentroid(bufHigh, kSampleRate);

    // If linear: centroidMid would be midpoint of low and high
    float linearMidpoint = (centroidLow + centroidHigh) * 0.5f;

    // Exponential coupling: centroidMid should NOT be at the linear midpoint
    // Allow 5% tolerance for "midpoint" -- the deviation should exceed this
    float deviation = std::abs(centroidMid - linearMidpoint) / std::abs(centroidHigh - centroidLow);
    REQUIRE(deviation > 0.05f);
}

TEST_CASE("ImpactExciter pulse duration nonlinear with velocity: T(0.5) not midway (FR-021)",
          "[processors][impact_exciter][velocity]")
{
    // FR-020, FR-021: T *= pow(1-v, 0.2) is nonlinear
    auto measureDuration = [](float vel) {
        Krate::DSP::ImpactExciter exc;
        exc.prepare(kSampleRate, 90);
        auto buf = generateExciterBlock(exc, vel, 0.5f, 0.5f, 0.0f, 0.0f, 440.0f);
        return findLastNonZeroIndex(buf);
    };

    int durationLow = measureDuration(0.0001f);
    int durationMid = measureDuration(0.5f);
    int durationHigh = measureDuration(1.0f);

    // Ensure ordering: high vel = shorter
    REQUIRE(durationHigh < durationLow);

    // Mid should NOT be the arithmetic midpoint
    float linearMid = static_cast<float>(durationLow + durationHigh) * 0.5f;
    float actualMid = static_cast<float>(durationMid);
    float range = static_cast<float>(durationLow - durationHigh);
    float deviation = std::abs(actualMid - linearMid) / std::max(range, 1.0f);
    REQUIRE(deviation > 0.03f);
}
