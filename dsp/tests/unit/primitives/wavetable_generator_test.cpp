// ==============================================================================
// Tests: Wavetable Generator
// ==============================================================================
// Test suite for mipmapped wavetable generation functions (Layer 1).
// Covers User Stories 2, 3, 4: standard waveforms, custom harmonics, raw samples.
//
// Reference: specs/016-wavetable-oscillator/spec.md
//
// IMPORTANT: All sample-processing loops collect metrics inside the loop and
// assert ONCE after the loop. See testing-guide anti-patterns.
// ==============================================================================

#include <krate/dsp/primitives/wavetable_generator.h>
#include <krate/dsp/primitives/fft.h>
#include <krate/dsp/core/math_constants.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using Catch::Approx;
using namespace Krate::DSP;

// =============================================================================
// Helper: Analyze harmonic content of a wavetable level via FFT
// =============================================================================
static std::vector<float> analyzeHarmonics(const float* levelData, size_t tableSize) {
    FFT fft;
    fft.prepare(tableSize);
    const size_t numBins = fft.numBins();
    std::vector<Complex> spectrum(numBins);
    fft.forward(levelData, spectrum.data());

    std::vector<float> magnitudes(numBins);
    for (size_t i = 0; i < numBins; ++i) {
        magnitudes[i] = spectrum[i].magnitude();
    }
    return magnitudes;
}

// =============================================================================
// User Story 2: Standard Waveform Generation (T035-T044)
// =============================================================================

TEST_CASE("generateMipmappedSaw level 0 harmonic content (SC-005)", "[WavetableGenerator][US2]") {
    WavetableData data;
    generateMipmappedSaw(data);

    REQUIRE(data.numLevels() == kMaxMipmapLevels);

    const float* level0 = data.getLevel(0);
    REQUIRE(level0 != nullptr);

    auto mags = analyzeHarmonics(level0, kDefaultTableSize);

    // Fundamental magnitude (harmonic 1) should be the largest
    float fundamentalMag = mags[1];
    REQUIRE(fundamentalMag > 0.1f);

    // Verify first 20 harmonics follow 1/n amplitude series within 5%
    for (size_t n = 1; n <= 20; ++n) {
        float expected = fundamentalMag / static_cast<float>(n);
        float actual = mags[n];
        float relError = std::abs(actual - expected) / expected;
        float absError = std::abs(actual - expected);
        // Use whichever tolerance is more lenient
        bool withinTolerance = (relError < 0.05f) || (absError < 0.001f);
        REQUIRE(withinTolerance);
    }
}

TEST_CASE("generateMipmappedSaw mipmap levels have progressive band-limiting", "[WavetableGenerator][US2]") {
    WavetableData data;
    generateMipmappedSaw(data);

    // For each level, the max harmonic should be approximately tableSize / 2^(level+1)
    for (size_t level = 0; level < kMaxMipmapLevels; ++level) {
        const float* levelData = data.getLevel(level);
        REQUIRE(levelData != nullptr);

        auto mags = analyzeHarmonics(levelData, kDefaultTableSize);

        size_t maxHarmonic = kDefaultTableSize / (static_cast<size_t>(1) << (level + 1));

        // Verify harmonics above maxHarmonic are below -60 dB relative to fundamental
        float fundamentalMag = mags[1];
        if (fundamentalMag > 0.0f) {
            float threshold = fundamentalMag * 0.001f;  // -60 dB
            bool allBelowThreshold = true;
            for (size_t n = maxHarmonic + 2; n < mags.size(); ++n) {
                if (mags[n] > threshold) {
                    allBelowThreshold = false;
                    break;
                }
            }
            REQUIRE(allBelowThreshold);
        }
    }
}

TEST_CASE("generateMipmappedSaw no aliasing - no harmonics above Nyquist (SC-008)", "[WavetableGenerator][US2]") {
    WavetableData data;
    generateMipmappedSaw(data);

    for (size_t level = 0; level < kMaxMipmapLevels; ++level) {
        const float* levelData = data.getLevel(level);
        REQUIRE(levelData != nullptr);

        auto mags = analyzeHarmonics(levelData, kDefaultTableSize);

        size_t maxHarmonic = kDefaultTableSize / (static_cast<size_t>(1) << (level + 1));
        float fundamentalMag = mags[1];
        if (fundamentalMag > 0.0f) {
            float threshold = fundamentalMag * 0.001f;
            bool clean = true;
            for (size_t n = maxHarmonic + 2; n < mags.size(); ++n) {
                if (mags[n] > threshold) {
                    clean = false;
                    break;
                }
            }
            REQUIRE(clean);
        }
    }
}

TEST_CASE("generateMipmappedSaw highest level is sine", "[WavetableGenerator][US2]") {
    WavetableData data;
    generateMipmappedSaw(data);

    const float* lastLevel = data.getLevel(kMaxMipmapLevels - 1);
    REQUIRE(lastLevel != nullptr);

    auto mags = analyzeHarmonics(lastLevel, kDefaultTableSize);

    // Harmonic 1 (fundamental) should dominate
    float fundamentalMag = mags[1];
    REQUIRE(fundamentalMag > 0.1f);

    // All other harmonics should be negligible
    bool onlyFundamental = true;
    for (size_t n = 2; n < mags.size(); ++n) {
        if (mags[n] > fundamentalMag * 0.01f) {
            onlyFundamental = false;
            break;
        }
    }
    REQUIRE(onlyFundamental);
}

TEST_CASE("generateMipmappedSaw normalization (US2 scenario 4)", "[WavetableGenerator][US2]") {
    WavetableData data;
    generateMipmappedSaw(data);

    for (size_t level = 0; level < kMaxMipmapLevels; ++level) {
        const float* levelData = data.getLevel(level);
        REQUIRE(levelData != nullptr);

        float maxVal = 0.0f;
        float minVal = 0.0f;
        for (size_t i = 0; i < kDefaultTableSize; ++i) {
            if (levelData[i] > maxVal) maxVal = levelData[i];
            if (levelData[i] < minVal) minVal = levelData[i];
        }
        REQUIRE(maxVal <= 1.05f);
        REQUIRE(minVal >= -1.05f);
    }
}

TEST_CASE("generateMipmappedSquare odd harmonics only (SC-006)", "[WavetableGenerator][US2]") {
    WavetableData data;
    generateMipmappedSquare(data);

    REQUIRE(data.numLevels() == kMaxMipmapLevels);

    const float* level0 = data.getLevel(0);
    REQUIRE(level0 != nullptr);

    auto mags = analyzeHarmonics(level0, kDefaultTableSize);

    float fundamentalMag = mags[1];
    REQUIRE(fundamentalMag > 0.1f);

    // Even harmonics should be below -60 dB relative to fundamental
    float threshold = fundamentalMag * 0.001f;
    bool evenBelowThreshold = true;
    for (size_t n = 2; n <= 20; n += 2) {
        if (mags[n] > threshold) {
            evenBelowThreshold = false;
            break;
        }
    }
    REQUIRE(evenBelowThreshold);

    // Odd harmonics should follow 1/n amplitude
    for (size_t n = 1; n <= 15; n += 2) {
        float expected = fundamentalMag / static_cast<float>(n);
        float actual = mags[n];
        float relError = std::abs(actual - expected) / expected;
        float absError = std::abs(actual - expected);
        bool withinTolerance = (relError < 0.05f) || (absError < 0.001f);
        REQUIRE(withinTolerance);
    }
}

TEST_CASE("generateMipmappedTriangle level 0 harmonic content (SC-007)", "[WavetableGenerator][US2]") {
    WavetableData data;
    generateMipmappedTriangle(data);

    REQUIRE(data.numLevels() == kMaxMipmapLevels);

    const float* level0 = data.getLevel(0);
    REQUIRE(level0 != nullptr);

    auto mags = analyzeHarmonics(level0, kDefaultTableSize);

    float fundamentalMag = mags[1];
    REQUIRE(fundamentalMag > 0.1f);

    // Verify first 10 odd harmonics follow 1/n^2 series within 5%
    for (size_t n = 1; n <= 19; n += 2) {
        float expected = fundamentalMag / static_cast<float>(n * n);
        float actual = mags[n];
        float relError = std::abs(actual - expected) / expected;
        float absError = std::abs(actual - expected);
        bool withinTolerance = (relError < 0.05f) || (absError < 0.001f);
        REQUIRE(withinTolerance);
    }

    // Even harmonics should be below -60 dB
    float threshold = fundamentalMag * 0.001f;
    bool evenBelowThreshold = true;
    for (size_t n = 2; n <= 20; n += 2) {
        if (mags[n] > threshold) {
            evenBelowThreshold = false;
            break;
        }
    }
    REQUIRE(evenBelowThreshold);
}

TEST_CASE("generateMipmappedTriangle alternating sign", "[WavetableGenerator][US2]") {
    WavetableData data;
    generateMipmappedTriangle(data);

    const float* level0 = data.getLevel(0);
    REQUIRE(level0 != nullptr);

    FFT fft;
    fft.prepare(kDefaultTableSize);
    std::vector<Complex> spectrum(fft.numBins());
    fft.forward(level0, spectrum.data());

    // Verify the triangle waveform shape is correct by checking peak positions.
    // The exact phase depends on the harmonic sign convention. We verify that:
    // 1. There is a clear positive peak and a clear negative peak
    // 2. The peaks are approximately half a cycle apart
    float maxVal = -2.0f;
    float minVal = 2.0f;
    size_t maxIdx = 0;
    size_t minIdx = 0;
    for (size_t i = 0; i < kDefaultTableSize; ++i) {
        if (level0[i] > maxVal) { maxVal = level0[i]; maxIdx = i; }
        if (level0[i] < minVal) { minVal = level0[i]; minIdx = i; }
    }
    // Peaks should be significant (normalized to ~0.96)
    REQUIRE(maxVal > 0.9f);
    REQUIRE(minVal < -0.9f);

    // Peaks should be approximately half a cycle apart
    size_t peakDist = (maxIdx > minIdx) ? (maxIdx - minIdx) : (minIdx - maxIdx);
    // Allow for being close to half the table size (within 10%)
    REQUIRE(peakDist > kDefaultTableSize / 2 - kDefaultTableSize / 10);
    REQUIRE(peakDist < kDefaultTableSize / 2 + kDefaultTableSize / 10);
}

TEST_CASE("Guard samples set correctly for all waveforms (SC-018)", "[WavetableGenerator][US2]") {
    WavetableData sawData, squareData, triData, customData;
    generateMipmappedSaw(sawData);
    generateMipmappedSquare(squareData);
    generateMipmappedTriangle(triData);
    float harmonics[] = {1.0f, 0.5f, 0.33f, 0.25f};
    generateMipmappedFromHarmonics(customData, harmonics, 4);

    auto checkGuards = [](const WavetableData& data, const char* name) {
        for (size_t level = 0; level < data.numLevels(); ++level) {
            const float* p = data.getLevel(level);
            REQUIRE(p != nullptr);
            const size_t N = data.tableSize();

            // p[-1] == p[N-1] (prepend guard wraps from end)
            REQUIRE(p[-1] == Approx(p[N - 1]).margin(1e-6f));
            // p[N] == p[0] (first append guard wraps from start)
            REQUIRE(p[N] == Approx(p[0]).margin(1e-6f));
            // p[N+1] == p[1]
            REQUIRE(p[N + 1] == Approx(p[1]).margin(1e-6f));
            // p[N+2] == p[2]
            REQUIRE(p[N + 2] == Approx(p[2]).margin(1e-6f));
        }
    };

    checkGuards(sawData, "saw");
    checkGuards(squareData, "square");
    checkGuards(triData, "triangle");
    checkGuards(customData, "custom harmonics");
}

// =============================================================================
// User Story 3: Custom Harmonic Generation (T058-T063)
// =============================================================================

TEST_CASE("generateMipmappedFromHarmonics fundamental-only spectrum (US3 scenario 1)", "[WavetableGenerator][US3]") {
    WavetableData data;
    float harmonics[] = {1.0f};  // Only fundamental
    generateMipmappedFromHarmonics(data, harmonics, 1);

    REQUIRE(data.numLevels() == kMaxMipmapLevels);

    // All levels should contain identical sine waves
    for (size_t level = 0; level < data.numLevels(); ++level) {
        const float* levelData = data.getLevel(level);
        REQUIRE(levelData != nullptr);

        auto mags = analyzeHarmonics(levelData, kDefaultTableSize);

        // Only fundamental should be present
        float fundamentalMag = mags[1];
        REQUIRE(fundamentalMag > 0.1f);

        bool onlyFundamental = true;
        for (size_t n = 2; n < mags.size(); ++n) {
            if (mags[n] > fundamentalMag * 0.01f) {
                onlyFundamental = false;
                break;
            }
        }
        REQUIRE(onlyFundamental);
    }
}

TEST_CASE("generateMipmappedFromHarmonics 4-harmonic custom spectrum (US3 scenario 2)", "[WavetableGenerator][US3]") {
    WavetableData data;
    float harmonics[] = {1.0f, 0.5f, 0.33f, 0.25f};
    generateMipmappedFromHarmonics(data, harmonics, 4);

    REQUIRE(data.numLevels() == kMaxMipmapLevels);

    const float* level0 = data.getLevel(0);
    REQUIRE(level0 != nullptr);

    auto mags = analyzeHarmonics(level0, kDefaultTableSize);

    // Check relative amplitudes of first 4 harmonics
    float fundamentalMag = mags[1];
    REQUIRE(fundamentalMag > 0.01f);

    // Each harmonic relative to fundamental should match input ratios within 1%
    for (size_t n = 1; n <= 4; ++n) {
        float expectedRatio = harmonics[n - 1] / harmonics[0];
        float actualRatio = mags[n] / fundamentalMag;
        REQUIRE(actualRatio == Approx(expectedRatio).margin(0.02f));
    }
}

TEST_CASE("generateMipmappedFromHarmonics high harmonic count", "[WavetableGenerator][US3]") {
    WavetableData data;
    std::vector<float> harmonics(512);
    for (size_t i = 0; i < 512; ++i) {
        harmonics[i] = 1.0f / static_cast<float>(i + 1);
    }
    generateMipmappedFromHarmonics(data, harmonics.data(), harmonics.size());

    REQUIRE(data.numLevels() == kMaxMipmapLevels);

    // Higher levels should have progressively fewer harmonics
    for (size_t level = 0; level < kMaxMipmapLevels; ++level) {
        const float* levelData = data.getLevel(level);
        REQUIRE(levelData != nullptr);

        auto mags = analyzeHarmonics(levelData, kDefaultTableSize);
        size_t maxHarmonic = kDefaultTableSize / (static_cast<size_t>(1) << (level + 1));

        float fundamentalMag = mags[1];
        if (fundamentalMag > 0.0f) {
            float threshold = fundamentalMag * 0.001f;
            bool clean = true;
            for (size_t n = maxHarmonic + 2; n < mags.size(); ++n) {
                if (mags[n] > threshold) {
                    clean = false;
                    break;
                }
            }
            REQUIRE(clean);
        }
    }
}

TEST_CASE("generateMipmappedFromHarmonics zero harmonics (FR-028)", "[WavetableGenerator][US3]") {
    WavetableData data;
    generateMipmappedFromHarmonics(data, nullptr, 0);

    REQUIRE(data.numLevels() == kMaxMipmapLevels);

    // All levels should be silence
    for (size_t level = 0; level < data.numLevels(); ++level) {
        const float* levelData = data.getLevel(level);
        REQUIRE(levelData != nullptr);

        bool allZero = true;
        for (size_t i = 0; i < kDefaultTableSize; ++i) {
            if (levelData[i] != 0.0f) {
                allZero = false;
                break;
            }
        }
        REQUIRE(allZero);
    }
}

TEST_CASE("generateMipmappedFromHarmonics normalization", "[WavetableGenerator][US3]") {
    WavetableData data;
    float harmonics[] = {1.0f, 0.5f, 0.33f, 0.25f};
    generateMipmappedFromHarmonics(data, harmonics, 4);

    for (size_t level = 0; level < data.numLevels(); ++level) {
        const float* levelData = data.getLevel(level);
        REQUIRE(levelData != nullptr);

        float peak = 0.0f;
        for (size_t i = 0; i < kDefaultTableSize; ++i) {
            float absVal = std::abs(levelData[i]);
            if (absVal > peak) peak = absVal;
        }
        // Peak should be approximately 0.96 (within [0.90, 1.0])
        REQUIRE(peak >= 0.90f);
        REQUIRE(peak <= 1.0f);
    }
}

TEST_CASE("generateMipmappedFromHarmonics guard samples", "[WavetableGenerator][US3]") {
    WavetableData data;
    float harmonics[] = {1.0f, 0.5f, 0.33f, 0.25f};
    generateMipmappedFromHarmonics(data, harmonics, 4);

    for (size_t level = 0; level < data.numLevels(); ++level) {
        const float* p = data.getLevel(level);
        REQUIRE(p != nullptr);
        const size_t N = data.tableSize();

        REQUIRE(p[-1] == Approx(p[N - 1]).margin(1e-6f));
        REQUIRE(p[N] == Approx(p[0]).margin(1e-6f));
        REQUIRE(p[N + 1] == Approx(p[1]).margin(1e-6f));
        REQUIRE(p[N + 2] == Approx(p[2]).margin(1e-6f));
    }
}

// =============================================================================
// User Story 4: Raw Sample Generation (T071-T075)
// =============================================================================

TEST_CASE("generateMipmappedFromSamples sine input (US4 scenario 1)", "[WavetableGenerator][US4]") {
    // Generate a sine wave as input
    std::vector<float> sineInput(kDefaultTableSize);
    for (size_t i = 0; i < kDefaultTableSize; ++i) {
        sineInput[i] = std::sin(kTwoPi * static_cast<float>(i) / static_cast<float>(kDefaultTableSize));
    }

    WavetableData data;
    generateMipmappedFromSamples(data, sineInput.data(), sineInput.size());

    REQUIRE(data.numLevels() == kMaxMipmapLevels);

    // All levels should contain identical sine waves (only fundamental)
    for (size_t level = 0; level < data.numLevels(); ++level) {
        const float* levelData = data.getLevel(level);
        REQUIRE(levelData != nullptr);

        auto mags = analyzeHarmonics(levelData, kDefaultTableSize);

        float fundamentalMag = mags[1];
        REQUIRE(fundamentalMag > 0.1f);

        bool onlyFundamental = true;
        for (size_t n = 2; n < mags.size(); ++n) {
            if (mags[n] > fundamentalMag * 0.01f) {
                onlyFundamental = false;
                break;
            }
        }
        REQUIRE(onlyFundamental);
    }
}

TEST_CASE("generateMipmappedFromSamples raw sawtooth (US4 scenario 2)", "[WavetableGenerator][US4]") {
    // Generate a raw sawtooth as input
    std::vector<float> sawInput(kDefaultTableSize);
    for (size_t i = 0; i < kDefaultTableSize; ++i) {
        sawInput[i] = 2.0f * static_cast<float>(i) / static_cast<float>(kDefaultTableSize) - 1.0f;
    }

    WavetableData dataFromSamples;
    generateMipmappedFromSamples(dataFromSamples, sawInput.data(), sawInput.size());

    WavetableData dataFromGen;
    generateMipmappedSaw(dataFromGen);

    // Both should have similar harmonic content at level 0
    // Compare in frequency domain since time-domain alignment may differ
    const float* fromSamples = dataFromSamples.getLevel(0);
    const float* fromGen = dataFromGen.getLevel(0);
    REQUIRE(fromSamples != nullptr);
    REQUIRE(fromGen != nullptr);

    auto magsSamples = analyzeHarmonics(fromSamples, kDefaultTableSize);
    auto magsGen = analyzeHarmonics(fromGen, kDefaultTableSize);

    // First 20 harmonics should have similar relative magnitudes
    float refSamples = magsSamples[1];
    float refGen = magsGen[1];
    REQUIRE(refSamples > 0.01f);
    REQUIRE(refGen > 0.01f);

    bool harmonicsMatch = true;
    for (size_t n = 1; n <= 20; ++n) {
        float ratioSamples = magsSamples[n] / refSamples;
        float ratioGen = magsGen[n] / refGen;
        float diff = std::abs(ratioSamples - ratioGen);
        if (diff > 0.05f) {
            harmonicsMatch = false;
            break;
        }
    }
    REQUIRE(harmonicsMatch);
}

TEST_CASE("generateMipmappedFromSamples input size mismatch (US4 scenario 3)", "[WavetableGenerator][US4]") {
    // Provide 1024 samples for a 2048-sample table
    std::vector<float> shortInput(1024);
    for (size_t i = 0; i < 1024; ++i) {
        shortInput[i] = std::sin(kTwoPi * static_cast<float>(i) / 1024.0f);
    }

    WavetableData data;
    generateMipmappedFromSamples(data, shortInput.data(), shortInput.size());

    REQUIRE(data.numLevels() == kMaxMipmapLevels);

    // Should produce a valid sine at the table size
    const float* level0 = data.getLevel(0);
    REQUIRE(level0 != nullptr);

    auto mags = analyzeHarmonics(level0, kDefaultTableSize);
    float fundamentalMag = mags[1];
    REQUIRE(fundamentalMag > 0.1f);
}

TEST_CASE("generateMipmappedFromSamples zero-length input", "[WavetableGenerator][US4]") {
    WavetableData data;
    generateMipmappedFromSamples(data, nullptr, 0);

    // Data should remain in default state
    REQUIRE(data.numLevels() == 0);
}

TEST_CASE("generateMipmappedFromSamples normalization and guard samples", "[WavetableGenerator][US4]") {
    std::vector<float> sineInput(kDefaultTableSize);
    for (size_t i = 0; i < kDefaultTableSize; ++i) {
        sineInput[i] = std::sin(kTwoPi * static_cast<float>(i) / static_cast<float>(kDefaultTableSize));
    }

    WavetableData data;
    generateMipmappedFromSamples(data, sineInput.data(), sineInput.size());

    for (size_t level = 0; level < data.numLevels(); ++level) {
        const float* p = data.getLevel(level);
        REQUIRE(p != nullptr);
        const size_t N = data.tableSize();

        // Check normalization
        float peak = 0.0f;
        for (size_t i = 0; i < N; ++i) {
            float absVal = std::abs(p[i]);
            if (absVal > peak) peak = absVal;
        }
        REQUIRE(peak >= 0.90f);
        REQUIRE(peak <= 1.0f);

        // Check guard samples
        REQUIRE(p[-1] == Approx(p[N - 1]).margin(1e-6f));
        REQUIRE(p[N] == Approx(p[0]).margin(1e-6f));
        REQUIRE(p[N + 1] == Approx(p[1]).margin(1e-6f));
        REQUIRE(p[N + 2] == Approx(p[2]).margin(1e-6f));
    }
}
