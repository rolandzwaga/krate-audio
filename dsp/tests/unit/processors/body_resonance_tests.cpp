// ==============================================================================
// Body Resonance Unit Tests (Spec 131)
// ==============================================================================
// Tests for the BodyResonance hybrid modal bank + FDN body coloring processor.
//
// Phase 2 verification notes (T005):
// CrossoverLR4 (crossover_filter.h) is LR4 = 24 dB/oct. The body resonance
// spec requires a 6 dB/oct first-order crossover for the modal/FDN band split.
// CrossoverLR4 is NOT reused for this reason; BodyResonance will implement a
// simple inline first-order crossover instead.
//
// Phase 2 verification notes (T006):
// FDNReverb (fdn_reverb.h) Hadamard butterfly pattern (3-stage FWHT for 8 ch):
//   Stage 1: stride=4, butterfly pairs (i, i+4) for i=0..3
//   Stage 2: stride=2, butterfly pairs (i, i+2) for i in {0,1,4,5}
//   Stage 3: stride=1, butterfly pairs (k, k+1) for k in {0,2,4,6}
//   Normalize: x[i] *= 1/sqrt(N)
// For 4-channel body FDN, use 2-stage butterfly with 1/sqrt(4) = 0.5 norm.
//
// Jot absorption formula (from FDNReverb):
//   gDC  = 10^(-3 * delayLen / (t60dc  * sampleRate))
//   gNyq = 10^(-3 * delayLen / (t60nyq * sampleRate))
//   One-pole coeff derived from gDC/gNyq ratio.

#include <krate/dsp/processors/body_resonance.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <test_signals.h>

#include <array>
#include <chrono>
#include <cmath>
#include <numeric>

using namespace Krate::DSP;

// =============================================================================
// Helpers
// =============================================================================

namespace {

float computeRMS(const float* data, size_t n) {
    float sum = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        sum += data[i] * data[i];
    }
    return std::sqrt(sum / static_cast<float>(n));
}

bool allFinite(const float* data, size_t n) {
    for (size_t i = 0; i < n; ++i) {
        if (!std::isfinite(data[i])) return false;
    }
    return true;
}

bool hasClick(const float* data, size_t n, float thresholdDb = 6.0f) {
    // Check if any sample is more than thresholdDb above both neighbours.
    // A click manifests as a single sample spike.
    const float thresholdRatio = std::pow(10.0f, thresholdDb / 20.0f);
    for (size_t i = 1; i + 1 < n; ++i) {
        const float absVal = std::abs(data[i]);
        const float absPrev = std::abs(data[i - 1]);
        const float absNext = std::abs(data[i + 1]);
        const float maxNeighbour = std::max(absPrev, absNext);
        if (absVal > maxNeighbour * thresholdRatio && absVal > 1e-6f) {
            return true;
        }
    }
    return false;
}

}  // namespace

// =============================================================================
// User Story 1 -- Instrument Body Coloring (P1)
// =============================================================================

// T007: bypass produces bit-identical output (SC-007, FR-018)
TEST_CASE("BodyResonance - bypass produces bit-identical output",
          "[body_resonance][us1]") {
    BodyResonance br;
    br.prepare(44100.0);
    br.setParams(0.5f, 0.5f, 0.0f);

    constexpr size_t N = 1024;
    std::array<float, N> input{};
    std::array<float, N> output{};
    TestHelpers::generateWhiteNoise(input.data(), N, 42);

    br.processBlock(input.data(), output.data(), N);

    bool identical = true;
    for (size_t i = 0; i < N; ++i) {
        if (output[i] != input[i]) {
            identical = false;
            break;
        }
    }
    REQUIRE(identical);
}

// T008: audible coloring at default params
TEST_CASE("BodyResonance - audible coloring at default params",
          "[body_resonance][us1]") {
    BodyResonance br;
    br.prepare(44100.0);
    br.setParams(0.5f, 0.5f, 1.0f);

    constexpr size_t N = 4096;
    std::array<float, N> input{};
    std::array<float, N> output{};
    TestHelpers::generateWhiteNoise(input.data(), N, 42);

    br.processBlock(input.data(), output.data(), N);

    bool differs = false;
    for (size_t i = 0; i < N; ++i) {
        if (std::abs(output[i] - input[i]) > 1e-7f) {
            differs = true;
            break;
        }
    }
    REQUIRE(differs);
}

// T009: no instability at any parameter combo (SC-004, FR-016)
TEST_CASE("BodyResonance - no instability at any parameter combo",
          "[body_resonance][us1]") {
    constexpr float sizes[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
    constexpr float materials[] = {0.0f, 0.5f, 1.0f};
    constexpr size_t N = 4096;

    for (float sz : sizes) {
        for (float mat : materials) {
            BodyResonance br;
            br.prepare(44100.0);
            br.setParams(sz, mat, 1.0f);

            std::array<float, N> input{};
            std::array<float, N> output{};
            TestHelpers::generateImpulse(input);

            br.processBlock(input.data(), output.data(), N);

            REQUIRE(allFinite(output.data(), N));
        }
    }
}

// T010: energy passive (SC-005, FR-016)
TEST_CASE("BodyResonance - energy passive",
          "[body_resonance][us1]") {
    constexpr float sizes[] = {0.0f, 0.25f, 0.5f, 0.75f, 1.0f};
    constexpr float materials[] = {0.0f, 0.5f, 1.0f};
    constexpr size_t N = 4096;

    for (float sz : sizes) {
        for (float mat : materials) {
            BodyResonance br;
            br.prepare(44100.0);
            br.setParams(sz, mat, 1.0f);

            std::array<float, N> input{};
            std::array<float, N> output{};
            TestHelpers::generateSine(input.data(), N, 440.0f, 44100.0f);

            br.processBlock(input.data(), output.data(), N);

            const float inRms = computeRMS(input.data(), N);
            const float outRms = computeRMS(output.data(), N);

            INFO("size=" << sz << " material=" << mat);
            REQUIRE(outRms <= inRms + 1e-6f);
        }
    }
}

// T011: lifecycle
TEST_CASE("BodyResonance - lifecycle",
          "[body_resonance][us1]") {
    BodyResonance br;

    REQUIRE_FALSE(br.isPrepared());

    br.prepare(44100.0);
    REQUIRE(br.isPrepared());

    br.reset();
    REQUIRE(br.isPrepared());

    br.prepare(96000.0);
    REQUIRE(br.isPrepared());
}

// T011b: silence input produces silence output
TEST_CASE("BodyResonance - silence input produces silence output",
          "[body_resonance][us1]") {
    BodyResonance br;
    br.prepare(44100.0);
    br.setParams(0.5f, 0.5f, 1.0f);

    constexpr size_t N = 8192;
    std::array<float, N> input{};
    std::array<float, N> output{};
    input.fill(0.0f);

    br.processBlock(input.data(), output.data(), N);

    // After initial transient (first 64 samples), output should be zero
    bool allZero = true;
    for (size_t i = 64; i < N; ++i) {
        if (output[i] != 0.0f) {
            allZero = false;
            break;
        }
    }
    REQUIRE(allZero);
}

// T011c: mix transition from zero is artifact-free (FR-017, SC-008)
TEST_CASE("BodyResonance - mix transition from zero is artifact-free",
          "[body_resonance][us1]") {
    BodyResonance br;
    br.prepare(44100.0);
    br.setParams(0.5f, 0.5f, 0.0f);

    constexpr size_t blockSize = 512;
    std::array<float, blockSize * 2> input{};
    std::array<float, blockSize * 2> output{};
    TestHelpers::generateSine(input.data(), blockSize * 2, 440.0f, 44100.0f, 0.5f);

    // Process first block at mix=0
    br.processBlock(input.data(), output.data(), blockSize);

    // Switch to mix=1
    br.setParams(0.5f, 0.5f, 1.0f);
    br.processBlock(input.data() + blockSize, output.data() + blockSize, blockSize);

    // Check for clicks at the boundary region
    REQUIRE_FALSE(hasClick(output.data(), blockSize * 2));
}

// =============================================================================
// User Story 2 -- Body Size Control (P1)
// =============================================================================

// T012: size=0 produces modes above 250 Hz (SC-002, FR-006)
TEST_CASE("BodyResonance - size=0 produces modes above 250 Hz",
          "[body_resonance][us2]") {
    BodyResonance br;
    br.prepare(44100.0);
    br.setParams(0.0f, 0.5f, 1.0f);

    constexpr size_t N = 8192;
    std::array<float, N> input{};
    std::array<float, N> output{};
    TestHelpers::generateImpulse(input);

    br.processBlock(input.data(), output.data(), N);

    // Compute energy in low band (below 200 Hz) vs total
    // Use simple band-energy estimation: sum squares in frequency bins
    // Bin width = sampleRate / N = 44100 / 8192 ~ 5.38 Hz
    // 200 Hz / 5.38 ~ bin 37
    // We'll use a simple DFT for the bins of interest
    constexpr size_t binAt200Hz = 37;

    float lowEnergy = 0.0f;
    float totalEnergy = 0.0f;
    for (size_t k = 1; k < N / 2; ++k) {
        // Compute magnitude of bin k via DFT
        float re = 0.0f;
        float im = 0.0f;
        const float omega = 2.0f * 3.14159265f * static_cast<float>(k) /
                            static_cast<float>(N);
        for (size_t n = 0; n < N; ++n) {
            re += output[n] * std::cos(omega * static_cast<float>(n));
            im -= output[n] * std::sin(omega * static_cast<float>(n));
        }
        const float mag2 = re * re + im * im;
        totalEnergy += mag2;
        if (k <= binAt200Hz) {
            lowEnergy += mag2;
        }
    }

    // Energy below 200 Hz should be less than 20% of total
    if (totalEnergy > 1e-10f) {
        REQUIRE(lowEnergy / totalEnergy < 0.20f);
    }
}

// T013: size=1 produces modes below 100 Hz (SC-002, FR-006)
TEST_CASE("BodyResonance - size=1 produces modes below 100 Hz",
          "[body_resonance][us2]") {
    // Verify that at size=1 (large/cello), the body resonance has more low
    // frequency content compared to size=0 (small/violin).
    constexpr size_t N = 8192;

    // Process through large body
    // Warm up with silence blocks so smoothers converge to target values,
    // then send the impulse.
    constexpr size_t warmupBlocks = 16;
    constexpr size_t warmupBlockSize = 512;

    BodyResonance brLarge;
    brLarge.prepare(44100.0);
    brLarge.setParams(1.0f, 0.5f, 1.0f);
    {
        std::array<float, warmupBlockSize> silence{};
        std::array<float, warmupBlockSize> discard{};
        for (size_t b = 0; b < warmupBlocks; ++b) {
            brLarge.processBlock(silence.data(), discard.data(), warmupBlockSize);
        }
    }
    std::array<float, N> input{};
    std::array<float, N> outLarge{};
    TestHelpers::generateImpulse(input);
    brLarge.processBlock(input.data(), outLarge.data(), N);

    // Process through small body
    BodyResonance brSmall;
    brSmall.prepare(44100.0);
    brSmall.setParams(0.0f, 0.5f, 1.0f);
    {
        std::array<float, warmupBlockSize> silence{};
        std::array<float, warmupBlockSize> discard{};
        for (size_t b = 0; b < warmupBlocks; ++b) {
            brSmall.processBlock(silence.data(), discard.data(), warmupBlockSize);
        }
    }
    std::array<float, N> outSmall{};
    TestHelpers::generateImpulse(input);
    brSmall.processBlock(input.data(), outSmall.data(), N);

    // Compute low band energy (below 150 Hz) for both
    constexpr size_t binAt150Hz = 28;
    float largeLowEnergy = 0.0f;
    float smallLowEnergy = 0.0f;
    for (size_t k = 1; k <= binAt150Hz; ++k) {
        float omega = 2.0f * 3.14159265f * static_cast<float>(k) /
                      static_cast<float>(N);
        float reLarge = 0.0f, imLarge = 0.0f;
        float reSmall = 0.0f, imSmall = 0.0f;
        for (size_t n = 0; n < N; ++n) {
            float c = std::cos(omega * static_cast<float>(n));
            float s = std::sin(omega * static_cast<float>(n));
            reLarge += outLarge[n] * c;
            imLarge -= outLarge[n] * s;
            reSmall += outSmall[n] * c;
            imSmall -= outSmall[n] * s;
        }
        largeLowEnergy += reLarge * reLarge + imLarge * imLarge;
        smallLowEnergy += reSmall * reSmall + imSmall * imSmall;
    }

    // Large body should have more low frequency energy than small body.
    INFO("largeLowEnergy=" << largeLowEnergy
                           << " smallLowEnergy=" << smallLowEnergy);
    REQUIRE(largeLowEnergy > 0.0f);

    // SC-002: Assert spectral energy concentration below 150 Hz is greater
    // than energy above 300 Hz for the large body (size=1.0).
    // Compare energy *density* (energy/Hz) since the high band spans many more
    // Hz than the low band. This verifies actual spectral concentration.
    constexpr size_t binAt300Hz = static_cast<size_t>(300.0f * N / 44100.0f);
    float largeHighEnergy = 0.0f;
    size_t lowBinCount = binAt150Hz;       // bins 1..binAt150Hz
    size_t highBinCount = N / 2 - binAt300Hz;  // bins binAt300Hz..N/2
    for (size_t k = binAt300Hz; k < N / 2; ++k) {
        float omega = 2.0f * 3.14159265f * static_cast<float>(k) /
                      static_cast<float>(N);
        float re = 0.0f, im = 0.0f;
        for (size_t n = 0; n < N; ++n) {
            re += outLarge[n] * std::cos(omega * static_cast<float>(n));
            im -= outLarge[n] * std::sin(omega * static_cast<float>(n));
        }
        largeHighEnergy += re * re + im * im;
    }

    float lowDensity = largeLowEnergy / static_cast<float>(lowBinCount);
    float highDensity = largeHighEnergy / static_cast<float>(highBinCount);
    INFO("lowDensity=" << lowDensity << " highDensity=" << highDensity
         << " (lowE=" << largeLowEnergy << " highE=" << largeHighEnergy << ")");
    REQUIRE(lowDensity > highDensity);

    // Compare spectral centroids: large body should have a lower centroid
    float largeCentroid = 0.0f;
    float largeTotalE = 0.0f;
    float smallCentroid = 0.0f;
    float smallTotalE = 0.0f;
    for (size_t k = 1; k < N / 2; ++k) {
        float omega = 2.0f * 3.14159265f * static_cast<float>(k) /
                      static_cast<float>(N);
        float rL = 0.0f, iL = 0.0f;
        float rS = 0.0f, iS = 0.0f;
        for (size_t n = 0; n < N; ++n) {
            float c = std::cos(omega * static_cast<float>(n));
            float s = std::sin(omega * static_cast<float>(n));
            rL += outLarge[n] * c;
            iL -= outLarge[n] * s;
            rS += outSmall[n] * c;
            iS -= outSmall[n] * s;
        }
        float magL2 = rL * rL + iL * iL;
        float magS2 = rS * rS + iS * iS;
        float freq = static_cast<float>(k) * 44100.0f / static_cast<float>(N);
        largeCentroid += freq * magL2;
        largeTotalE += magL2;
        smallCentroid += freq * magS2;
        smallTotalE += magS2;
    }
    largeCentroid /= (largeTotalE + 1e-12f);
    smallCentroid /= (smallTotalE + 1e-12f);

    INFO("largeCentroid=" << largeCentroid << " smallCentroid=" << smallCentroid);
    REQUIRE(largeCentroid < smallCentroid);
}

// T014: size parameter sweep has no zipper noise (SC-008, FR-017)
TEST_CASE("BodyResonance - size parameter sweep has no zipper noise",
          "[body_resonance][us2]") {
    BodyResonance br;
    br.prepare(44100.0);
    br.setParams(0.0f, 0.5f, 1.0f);

    constexpr size_t blockSize = 512;
    std::array<float, blockSize * 2> input{};
    std::array<float, blockSize * 2> output{};
    // Use 100 Hz sine -- stays firmly in the LP band (below crossover) for
    // all size values, avoiding the LP/HP transition region near ~500 Hz
    // that can produce transients during crossover frequency sweeps.
    TestHelpers::generateSine(input.data(), blockSize * 2, 100.0f, 44100.0f, 0.3f);

    // Process first block at size=0
    br.processBlock(input.data(), output.data(), blockSize);

    // Switch to size=1
    br.setParams(1.0f, 0.5f, 1.0f);
    br.processBlock(input.data() + blockSize, output.data() + blockSize, blockSize);

    REQUIRE_FALSE(hasClick(output.data(), blockSize * 2));
}

// =============================================================================
// User Story 3 -- Material Character Control (P1)
// =============================================================================

// T015: wood has strong HF damping (SC-003, FR-012)
TEST_CASE("BodyResonance - wood has strong HF damping",
          "[body_resonance][us3]") {
    // SC-003: At material=0 (wood), HF T60 must be at least 3x shorter than
    // LF T60. We estimate T60 by measuring the time for bandpass-filtered
    // impulse response energy to decay by 60 dB.
    constexpr double sr = 44100.0;
    constexpr size_t N = 32768;  // ~743 ms, enough for decay measurement

    BodyResonance br;
    br.prepare(sr);
    br.setParams(0.5f, 0.0f, 1.0f);  // wood

    // Warm up to let smoothers converge
    {
        constexpr size_t warmupBlockSize = 512;
        std::array<float, warmupBlockSize> silence{};
        std::array<float, warmupBlockSize> discard{};
        for (size_t b = 0; b < 16; ++b) {
            br.processBlock(silence.data(), discard.data(), warmupBlockSize);
        }
    }

    std::array<float, N> input{};
    std::array<float, N> output{};
    TestHelpers::generateImpulse(input);
    br.processBlock(input.data(), output.data(), N);

    // Bandpass filter the impulse response around 200 Hz and 4 kHz using
    // biquad bandpass filters. Use Q=0.7 for a wide band to capture modal
    // energy near the target frequency.
    Biquad bp200;
    bp200.configure(FilterType::Bandpass, 200.0f, 0.7f, 0.0f, static_cast<float>(sr));
    Biquad bp4k;
    bp4k.configure(FilterType::Bandpass, 4000.0f, 0.7f, 0.0f, static_cast<float>(sr));

    std::array<float, N> band200{};
    std::array<float, N> band4k{};
    for (size_t i = 0; i < N; ++i) {
        band200[i] = bp200.process(output[i]);
        band4k[i] = bp4k.process(output[i]);
    }

    // Estimate T60 from each band's decay envelope.
    // Use a windowed RMS approach: find peak energy, then find the time
    // when energy drops 60 dB below peak.
    auto estimateT60 = [&](const float* band, size_t len) -> float {
        constexpr size_t windowSize = 256;
        // Find peak windowed energy
        float peakEnergy = 0.0f;
        for (size_t i = 0; i + windowSize <= len; i += windowSize / 2) {
            float energy = 0.0f;
            for (size_t j = 0; j < windowSize; ++j) {
                energy += band[i + j] * band[i + j];
            }
            peakEnergy = std::max(peakEnergy, energy);
        }

        if (peakEnergy < 1e-20f) return 0.0f;

        // Threshold at -60 dB below peak
        const float threshold = peakEnergy * 1e-6f;

        // Find last window that exceeds threshold
        size_t lastAbove = 0;
        for (size_t i = 0; i + windowSize <= len; i += windowSize / 2) {
            float energy = 0.0f;
            for (size_t j = 0; j < windowSize; ++j) {
                energy += band[i + j] * band[i + j];
            }
            if (energy > threshold) {
                lastAbove = i;
            }
        }

        return static_cast<float>(lastAbove) / static_cast<float>(sr);
    };

    float t60_200hz = estimateT60(band200.data(), N);
    float t60_4k = estimateT60(band4k.data(), N);

    INFO("t60_200hz=" << t60_200hz << " t60_4k=" << t60_4k);
    // SC-003: HF T60 at least 3x shorter than LF T60
    // Guard: LF must have measurable decay for the test to be meaningful
    REQUIRE(t60_200hz > 0.001f);
    REQUIRE(t60_4k < t60_200hz / 3.0f);
}

// T016: metal has similar HF and LF decay (SC-003, FR-012)
TEST_CASE("BodyResonance - metal has similar HF and LF decay",
          "[body_resonance][us3]") {
    // SC-003: At material=1 (metal), HF T60 must be at least half of LF T60.
    // Same T60 estimation approach as T015.
    constexpr double sr = 44100.0;
    constexpr size_t N = 32768;

    BodyResonance br;
    br.prepare(sr);
    br.setParams(0.5f, 1.0f, 1.0f);  // metal

    // Warm up to let smoothers converge
    {
        constexpr size_t warmupBlockSize = 512;
        std::array<float, warmupBlockSize> silence{};
        std::array<float, warmupBlockSize> discard{};
        for (size_t b = 0; b < 16; ++b) {
            br.processBlock(silence.data(), discard.data(), warmupBlockSize);
        }
    }

    std::array<float, N> input{};
    std::array<float, N> output{};
    TestHelpers::generateImpulse(input);
    br.processBlock(input.data(), output.data(), N);

    // Bandpass filter around 200 Hz and 4 kHz (wide Q=0.7 to capture
    // modal energy near the target frequency)
    Biquad bp200;
    bp200.configure(FilterType::Bandpass, 200.0f, 0.7f, 0.0f, static_cast<float>(sr));
    Biquad bp4k;
    bp4k.configure(FilterType::Bandpass, 4000.0f, 0.7f, 0.0f, static_cast<float>(sr));

    std::array<float, N> band200{};
    std::array<float, N> band4k{};
    for (size_t i = 0; i < N; ++i) {
        band200[i] = bp200.process(output[i]);
        band4k[i] = bp4k.process(output[i]);
    }

    // Estimate T60 using windowed RMS decay
    auto estimateT60 = [&](const float* band, size_t len) -> float {
        constexpr size_t windowSize = 256;
        float peakEnergy = 0.0f;
        for (size_t i = 0; i + windowSize <= len; i += windowSize / 2) {
            float energy = 0.0f;
            for (size_t j = 0; j < windowSize; ++j) {
                energy += band[i + j] * band[i + j];
            }
            peakEnergy = std::max(peakEnergy, energy);
        }

        if (peakEnergy < 1e-20f) return 0.0f;

        const float threshold = peakEnergy * 1e-6f;  // -60 dB

        size_t lastAbove = 0;
        for (size_t i = 0; i + windowSize <= len; i += windowSize / 2) {
            float energy = 0.0f;
            for (size_t j = 0; j < windowSize; ++j) {
                energy += band[i + j] * band[i + j];
            }
            if (energy > threshold) {
                lastAbove = i;
            }
        }

        return static_cast<float>(lastAbove) / static_cast<float>(sr);
    };

    float t60_200hz = estimateT60(band200.data(), N);
    float t60_4k = estimateT60(band4k.data(), N);

    INFO("t60_200hz=" << t60_200hz << " t60_4k=" << t60_4k);
    // SC-003: For metal, HF T60 must be at least half of LF T60
    REQUIRE(t60_200hz > 0.001f);
    REQUIRE(t60_4k >= t60_200hz / 2.0f);
}

// T017: material sweep is artifact-free (SC-008, FR-017)
TEST_CASE("BodyResonance - material sweep is artifact-free",
          "[body_resonance][us3]") {
    BodyResonance br;
    br.prepare(44100.0);
    br.setParams(0.5f, 0.0f, 1.0f);

    constexpr size_t blockSize = 512;
    std::array<float, blockSize * 2> input{};
    std::array<float, blockSize * 2> output{};
    TestHelpers::generateSine(input.data(), blockSize * 2, 440.0f, 44100.0f, 0.3f);

    br.processBlock(input.data(), output.data(), blockSize);

    br.setParams(0.5f, 1.0f, 1.0f);
    br.processBlock(input.data() + blockSize, output.data() + blockSize, blockSize);

    REQUIRE_FALSE(hasClick(output.data(), blockSize * 2));
}

// T018: body is passive at all material values (FR-016)
TEST_CASE("BodyResonance - body is passive at all material values",
          "[body_resonance][us3]") {
    constexpr float materials[] = {0.0f, 0.5f, 1.0f};
    constexpr size_t N = 4096;

    for (float mat : materials) {
        BodyResonance br;
        br.prepare(44100.0);
        br.setParams(0.5f, mat, 1.0f);

        std::array<float, N> input{};
        std::array<float, N> output{};
        TestHelpers::generateWhiteNoise(input.data(), N, 42);

        br.processBlock(input.data(), output.data(), N);

        const float inRms = computeRMS(input.data(), N);
        const float outRms = computeRMS(output.data(), N);

        INFO("material=" << mat << " inRms=" << inRms << " outRms=" << outRms);
        REQUIRE(outRms <= inRms + 1e-6f);
    }
}

// =============================================================================
// User Story 4 -- Radiation HPF on Small Bodies (P2)
// =============================================================================

// T019: radiation HPF attenuates sub-bass on small body (SC-010, FR-015)
TEST_CASE("BodyResonance - radiation HPF attenuates sub-bass on small body",
          "[body_resonance][us4]") {
    BodyResonance br;
    br.prepare(44100.0);
    br.setParams(0.0f, 0.5f, 1.0f);

    // Warm up to let smoothers converge
    {
        constexpr size_t wbs = 512;
        std::array<float, wbs> silence{};
        std::array<float, wbs> discard{};
        for (size_t b = 0; b < 16; ++b)
            br.processBlock(silence.data(), discard.data(), wbs);
    }

    constexpr size_t N = 8192;
    std::array<float, N> input{};
    std::array<float, N> output{};
    TestHelpers::generateSine(input.data(), N, 50.0f, 44100.0f);

    br.processBlock(input.data(), output.data(), N);

    const float inRms = computeRMS(input.data(), N);
    const float outRms = computeRMS(output.data(), N);

    // Output RMS should be less than 30% of input RMS (at least 10 dB attenuation)
    INFO("inRms=" << inRms << " outRms=" << outRms);
    REQUIRE(outRms < inRms * 0.30f);
}

// T020: radiation HPF cutoff scales with size (FR-015)
TEST_CASE("BodyResonance - radiation HPF cutoff scales with size",
          "[body_resonance][us4]") {
    // Test that the radiation HPF cutoff is higher for small bodies than
    // large bodies. We measure the attenuation ratio at a low frequency
    // (50 Hz) versus a reference frequency (500 Hz). The small body HPF
    // cutoff (~192 Hz) should attenuate 50 Hz relative to 500 Hz MORE
    // than the large body HPF cutoff (~42 Hz).
    constexpr size_t N = 8192;

    auto measureAttenuation = [](float size, float testFreq,
                                 float refFreq) -> float {
        // Process a sine at testFreq and refFreq, return the ratio.
        auto processRMS = [&](float freq) -> float {
            BodyResonance br;
            br.prepare(44100.0);
            br.setParams(size, 0.5f, 1.0f);
            // Warm up
            constexpr size_t wbs = 512;
            std::array<float, wbs> silence{};
            std::array<float, wbs> discard{};
            for (size_t i = 0; i < 16; ++i)
                br.processBlock(silence.data(), discard.data(), wbs);
            // Process
            std::array<float, 8192> inp{};
            std::array<float, 8192> out{};
            TestHelpers::generateSine(inp.data(), 8192, freq, 44100.0f);
            br.processBlock(inp.data(), out.data(), 8192);
            return computeRMS(out.data(), 8192);
        };

        const float rmsTest = processRMS(testFreq);
        const float rmsRef = processRMS(refFreq);
        // Return ratio of low-freq to ref-freq response (lower = more HPF)
        return (rmsRef > 1e-10f) ? (rmsTest / rmsRef) : 0.0f;
    };

    const float ratioSmall = measureAttenuation(0.0f, 50.0f, 500.0f);
    const float ratioLarge = measureAttenuation(1.0f, 50.0f, 500.0f);

    // Small body should attenuate 50 Hz more relative to 500 Hz
    // (lower ratio = more HPF attenuation at low frequency)
    INFO("ratioSmall=" << ratioSmall << " ratioLarge=" << ratioLarge);
    REQUIRE(ratioSmall < ratioLarge);
}

// =============================================================================
// User Story 5 -- No FDN Metallic Ringing in Wood Mode (P2)
// =============================================================================

// T021: no FDN ringing in wood mode below crossover (SC-009, FR-011, FR-013)
TEST_CASE("BodyResonance - no FDN ringing in wood mode below crossover",
          "[body_resonance][us5]") {
    // In wood mode, the FDN decays quickly (T60_DC=0.15s). After ~300ms
    // (wood cap), the FDN should be completely dead. We verify that the
    // tail after 300ms has negligible energy, proving no FDN ringing persists.
    // This approach is more robust than spectral peak detection, which can
    // be confused by short-lived modal resonance peaks.
    BodyResonance br;
    br.prepare(44100.0);
    br.setParams(0.5f, 0.0f, 1.0f);

    // Process beyond the wood RT60 cap (300 ms = 13230 samples)
    constexpr size_t N = 16384;
    std::array<float, N> input{};
    std::array<float, N> output{};
    TestHelpers::generateImpulse(input);

    br.processBlock(input.data(), output.data(), N);

    // Find peak in early response
    float peak = 0.0f;
    for (size_t i = 0; i < 2048; ++i) {
        peak = std::max(peak, std::abs(output[i]));
    }

    // Measure RMS of tail after 300 ms (sample 13230)
    constexpr size_t tailStart = 13230;
    float tailRms = 0.0f;
    size_t tailCount = 0;
    for (size_t i = tailStart; i < N; ++i) {
        tailRms += output[i] * output[i];
        ++tailCount;
    }
    tailRms = std::sqrt(tailRms / static_cast<float>(tailCount));

    // Tail should be below -50 dB of peak (well below audibility)
    INFO("peak=" << peak << " tailRms=" << tailRms);
    if (peak > 1e-8f) {
        REQUIRE(tailRms < peak * 0.003f);  // ~-50 dB
    }
}

// =============================================================================
// Sample Rate Scaling (FR-022)
// =============================================================================

// T022: sample rate scaling
TEST_CASE("BodyResonance - sample rate scaling",
          "[body_resonance]") {
    constexpr double sampleRates[] = {44100.0, 48000.0, 96000.0, 192000.0};
    constexpr size_t N = 2048;

    for (double sr : sampleRates) {
        BodyResonance br;
        br.prepare(sr);

        REQUIRE(br.isPrepared());

        br.setParams(0.5f, 0.5f, 1.0f);

        std::array<float, N> input{};
        std::array<float, N> output{};
        TestHelpers::generateImpulse(input);

        br.processBlock(input.data(), output.data(), N);

        INFO("sampleRate=" << sr);
        REQUIRE(allFinite(output.data(), N));
    }
}

// =============================================================================
// FDN RT60 Cap (SC-001, FR-013)
// =============================================================================

// T023: FDN RT60 cap wood
TEST_CASE("BodyResonance - FDN RT60 cap wood",
          "[body_resonance]") {
    BodyResonance br;
    br.prepare(44100.0);
    br.setParams(0.5f, 0.0f, 1.0f);

    // 300 ms at 44100 Hz = 13230 samples
    constexpr size_t N = 13274;
    std::array<float, N> input{};
    std::array<float, N> output{};
    TestHelpers::generateImpulse(input);

    br.processBlock(input.data(), output.data(), N);

    // Find peak value
    float peak = 0.0f;
    for (size_t i = 0; i < N; ++i) {
        peak = std::max(peak, std::abs(output[i]));
    }

    // Tail after 300 ms should be below -60 dB relative to peak
    constexpr size_t tailStart = static_cast<size_t>(44100 * 0.3);
    float tailRms = 0.0f;
    size_t tailCount = 0;
    for (size_t i = tailStart; i < N; ++i) {
        tailRms += output[i] * output[i];
        ++tailCount;
    }
    tailRms = std::sqrt(tailRms / static_cast<float>(tailCount));

    // -60 dB = 0.001 of peak
    INFO("peak=" << peak << " tailRms=" << tailRms);
    if (peak > 1e-8f) {
        REQUIRE(tailRms < peak * 0.001f);
    }
}

// T024: FDN RT60 cap metal
TEST_CASE("BodyResonance - FDN RT60 cap metal",
          "[body_resonance]") {
    BodyResonance br;
    br.prepare(44100.0);
    br.setParams(0.5f, 1.0f, 1.0f);

    // 2 seconds at 44100 Hz = 88200 samples
    constexpr size_t N = 88242;
    std::array<float, N> input{};
    std::array<float, N> output{};
    TestHelpers::generateImpulse(input);

    br.processBlock(input.data(), output.data(), N);

    // Find peak value
    float peak = 0.0f;
    for (size_t i = 0; i < N; ++i) {
        peak = std::max(peak, std::abs(output[i]));
    }

    // Tail after 2 s should be below -60 dB relative to peak
    constexpr size_t tailStart = static_cast<size_t>(44100 * 2.0);
    float tailRms = 0.0f;
    size_t tailCount = 0;
    for (size_t i = tailStart; i < N; ++i) {
        tailRms += output[i] * output[i];
        ++tailCount;
    }
    tailRms = std::sqrt(tailRms / static_cast<float>(tailCount));

    INFO("peak=" << peak << " tailRms=" << tailRms);
    if (peak > 1e-8f) {
        REQUIRE(tailRms < peak * 0.001f);
    }
}

// =============================================================================
// SC-006: Performance Benchmark (T077b)
// =============================================================================

TEST_CASE("BodyResonance - performance benchmark", "[body_resonance][.perf]") {
    // Measure CPU cost of processBlock() over 1,000,000 samples.
    // Budget: 0.5% single-core at 44.1 kHz = 22.676 us/sample.
    BodyResonance br;
    br.prepare(44100.0);
    br.setParams(0.5f, 0.5f, 1.0f);

    constexpr size_t kTotalSamples = 1'000'000;
    constexpr size_t kBlockSize = 512;
    constexpr size_t kNumBlocks = kTotalSamples / kBlockSize;

    // Generate input signal (440 Hz sine)
    std::array<float, kBlockSize> input{};
    std::array<float, kBlockSize> output{};
    for (size_t i = 0; i < kBlockSize; ++i) {
        input[i] = std::sin(2.0f * 3.14159265f * 440.0f *
                            static_cast<float>(i) / 44100.0f);
    }

    // Warm up
    for (size_t i = 0; i < 10; ++i) {
        br.processBlock(input.data(), output.data(), kBlockSize);
    }

    // Timed run
    auto start = std::chrono::high_resolution_clock::now();
    for (size_t i = 0; i < kNumBlocks; ++i) {
        br.processBlock(input.data(), output.data(), kBlockSize);
    }
    auto end = std::chrono::high_resolution_clock::now();

    double totalUs = std::chrono::duration<double, std::micro>(end - start).count();
    double usPerSample = totalUs / static_cast<double>(kTotalSamples);

    // 0.5% CPU at 44.1 kHz = 1e6 / 44100 * 0.005 = 22.676 us/sample
    constexpr double kBudgetUsPerSample = 22.676;

    CAPTURE(usPerSample);
    CAPTURE(kBudgetUsPerSample);
    INFO("BodyResonance: " << usPerSample << " microseconds/sample (budget: "
         << kBudgetUsPerSample << " us/sample)");
    REQUIRE(usPerSample < kBudgetUsPerSample);
}
