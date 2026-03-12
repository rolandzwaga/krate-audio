// ==============================================================================
// Flanger DSP Processor Tests
// ==============================================================================
// Constitution Principle XIII: Test-First Development
// Tests for the Flanger class (Layer 2 processor)
// ==============================================================================

#include <krate/dsp/processors/flanger.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <array>
#include <cmath>
#include <vector>

using Catch::Approx;
using namespace Krate::DSP;

// =============================================================================
// Helper: fill buffer with constant value
// =============================================================================
static void fillStereo(float* left, float* right, size_t n, float value) {
    for (size_t i = 0; i < n; ++i) {
        left[i] = value;
        right[i] = value;
    }
}

// =============================================================================
// User Story 1: Basic Flanging Sound
// =============================================================================

TEST_CASE("Flanger lifecycle: prepare sets isPrepared", "[flanger][lifecycle]") {
    Flanger flanger;
    REQUIRE_FALSE(flanger.isPrepared());

    flanger.prepare(44100.0);
    REQUIRE(flanger.isPrepared());
}

TEST_CASE("Flanger lifecycle: reset clears state without crash", "[flanger][lifecycle]") {
    Flanger flanger;
    flanger.prepare(44100.0);
    flanger.setMix(1.0f);
    flanger.setFeedback(0.9f);
    flanger.setDepth(1.0f);

    // Process some audio to build up state
    constexpr size_t N = 512;
    std::array<float, N> left{};
    std::array<float, N> right{};
    fillStereo(left.data(), right.data(), N, 0.5f);
    flanger.processStereo(left.data(), right.data(), N);

    // Reset should not crash
    REQUIRE_NOTHROW(flanger.reset());

    // After reset, processing should still work
    fillStereo(left.data(), right.data(), N, 0.5f);
    REQUIRE_NOTHROW(flanger.processStereo(left.data(), right.data(), N));
}

TEST_CASE("Flanger lifecycle: processStereo before prepare is safe", "[flanger][lifecycle]") {
    Flanger flanger;
    constexpr size_t N = 64;
    std::array<float, N> left{};
    std::array<float, N> right{};
    fillStereo(left.data(), right.data(), N, 0.5f);

    // Should not crash - just return without processing
    REQUIRE_NOTHROW(flanger.processStereo(left.data(), right.data(), N));

    // Data should be unchanged (guard returns immediately)
    REQUIRE(left[0] == Approx(0.5f));
    REQUIRE(right[0] == Approx(0.5f));
}

TEST_CASE("Flanger basic processing: output differs from dry at Mix=0.5", "[flanger][processing]") {
    Flanger flanger;
    flanger.prepare(44100.0);
    flanger.setRate(0.5f);
    flanger.setDepth(0.5f);
    flanger.setMix(0.5f);
    flanger.setFeedback(0.0f);

    // Process enough samples for the LFO to produce meaningful modulation
    // At 0.5Hz, one full cycle = 2 seconds = 88200 samples. Process one full cycle.
    constexpr size_t N = 88200;
    std::vector<float> left(N, 0.5f);
    std::vector<float> right(N, 0.5f);
    std::vector<float> dryLeft(N, 0.5f);

    flanger.processStereo(left.data(), right.data(), N);

    // At least some samples should differ from the dry signal
    bool anyDifference = false;
    for (size_t i = 0; i < N; ++i) {
        if (std::abs(left[i] - dryLeft[i]) > 1e-6f) {
            anyDifference = true;
            break;
        }
    }
    REQUIRE(anyDifference);
}

TEST_CASE("Flanger Mix=0.0 passthrough: output identical to dry", "[flanger][mix]") {
    Flanger flanger;
    flanger.prepare(44100.0);
    flanger.setRate(1.0f);
    flanger.setDepth(1.0f);
    flanger.setMix(0.0f);
    flanger.setFeedback(0.5f);

    // Process warmup block so smoothers converge from default 0.5 to target 0.0
    // At 5ms smoothing time, ~5ms * 44.1 = ~220 samples for ~99% convergence
    constexpr size_t kWarmup = 512;
    std::vector<float> warmL(kWarmup, 0.0f);
    std::vector<float> warmR(kWarmup, 0.0f);
    flanger.processStereo(warmL.data(), warmR.data(), kWarmup);

    // Reset delay state but keep smoother targets converged
    // (We just need the smoothers to be at 0.0 now)
    constexpr size_t N = 4096;
    std::vector<float> left(N);
    std::vector<float> right(N);

    // Generate a simple signal
    for (size_t i = 0; i < N; ++i) {
        float val = std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 44100.0f);
        left[i] = val;
        right[i] = val;
    }

    std::vector<float> dryLeft = left;
    std::vector<float> dryRight = right;

    flanger.processStereo(left.data(), right.data(), N);

    // With mix=0 (after smoother convergence), output should match dry
    bool allMatch = true;
    for (size_t i = 0; i < N; ++i) {
        if (std::abs(left[i] - dryLeft[i]) > 1e-6f ||
            std::abs(right[i] - dryRight[i]) > 1e-6f) {
            allMatch = false;
            break;
        }
    }
    REQUIRE(allMatch);
}

TEST_CASE("Flanger Mix=1.0 wet-only: output differs from dry", "[flanger][mix]") {
    Flanger flanger;
    flanger.prepare(44100.0);
    flanger.setRate(1.0f);
    flanger.setDepth(0.5f);
    flanger.setMix(1.0f);
    flanger.setFeedback(0.0f);

    // Need enough samples for delay line to be populated and LFO to modulate
    constexpr size_t N = 44100;
    std::vector<float> left(N);
    std::vector<float> right(N);

    for (size_t i = 0; i < N; ++i) {
        float val = std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 44100.0f);
        left[i] = val;
        right[i] = val;
    }

    std::vector<float> dryLeft = left;

    flanger.processStereo(left.data(), right.data(), N);

    // At mix=1.0, output should be purely wet (delayed signal), different from dry
    bool anyDifference = false;
    for (size_t i = 0; i < N; ++i) {
        if (std::abs(left[i] - dryLeft[i]) > 1e-6f) {
            anyDifference = true;
            break;
        }
    }
    REQUIRE(anyDifference);
}

TEST_CASE("Flanger true crossfade formula: at Mix=0.5, output ~= 0.5*dry + 0.5*wet", "[flanger][mix]") {
    // Run flanger twice: once at mix=1.0 to capture wet, then verify mix=0.5
    // uses true crossfade (not additive)
    Flanger flangerWet;
    flangerWet.prepare(44100.0);
    flangerWet.setRate(1.0f);
    flangerWet.setDepth(0.5f);
    flangerWet.setMix(1.0f);
    flangerWet.setFeedback(0.0f);

    constexpr size_t N = 4096;
    std::vector<float> wetLeft(N);
    std::vector<float> wetRight(N);

    for (size_t i = 0; i < N; ++i) {
        float val = std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 44100.0f);
        wetLeft[i] = val;
        wetRight[i] = val;
    }

    std::vector<float> dry(N);
    for (size_t i = 0; i < N; ++i) {
        dry[i] = wetLeft[i];
    }

    flangerWet.processStereo(wetLeft.data(), wetRight.data(), N);

    // Now process with mix=0.5
    Flanger flangerHalf;
    flangerHalf.prepare(44100.0);
    flangerHalf.setRate(1.0f);
    flangerHalf.setDepth(0.5f);
    flangerHalf.setMix(0.5f);
    flangerHalf.setFeedback(0.0f);

    std::vector<float> halfLeft(N);
    std::vector<float> halfRight(N);
    for (size_t i = 0; i < N; ++i) {
        float val = std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 44100.0f);
        halfLeft[i] = val;
        halfRight[i] = val;
    }

    flangerHalf.processStereo(halfLeft.data(), halfRight.data(), N);

    // True crossfade: output = 0.5*dry + 0.5*wet
    // Verify a few samples after warmup (skip first ~200 samples for smoother convergence)
    int matchCount = 0;
    int checkCount = 0;
    for (size_t i = 500; i < N; i += 100) {
        float expected = 0.5f * dry[i] + 0.5f * wetLeft[i];
        if (std::abs(halfLeft[i] - expected) < 0.02f) {
            matchCount++;
        }
        checkCount++;
    }
    // Most samples should match the crossfade formula
    REQUIRE(matchCount > checkCount / 2);
}

TEST_CASE("Flanger rate parameter range: extreme values produce valid output", "[flanger][rate]") {
    Flanger flanger;
    flanger.prepare(44100.0);
    flanger.setDepth(0.5f);
    flanger.setMix(0.5f);

    constexpr size_t N = 1024;
    std::array<float, N> left{};
    std::array<float, N> right{};

    SECTION("Rate = 0.05 Hz (minimum)") {
        flanger.setRate(0.05f);
        fillStereo(left.data(), right.data(), N, 0.5f);
        flanger.processStereo(left.data(), right.data(), N);

        bool hasNaN = false;
        for (size_t i = 0; i < N; ++i) {
            if (detail::isNaN(left[i]) || detail::isNaN(right[i])) {
                hasNaN = true;
                break;
            }
        }
        REQUIRE_FALSE(hasNaN);
    }

    SECTION("Rate = 5.0 Hz (maximum)") {
        flanger.setRate(5.0f);
        fillStereo(left.data(), right.data(), N, 0.5f);
        flanger.processStereo(left.data(), right.data(), N);

        bool hasNaN = false;
        for (size_t i = 0; i < N; ++i) {
            if (detail::isNaN(left[i]) || detail::isNaN(right[i])) {
                hasNaN = true;
                break;
            }
        }
        REQUIRE_FALSE(hasNaN);
    }
}

TEST_CASE("Flanger Depth=0.0 produces static comb filter", "[flanger][depth]") {
    Flanger flanger;
    flanger.prepare(44100.0);
    flanger.setRate(1.0f);
    flanger.setDepth(0.0f);
    flanger.setMix(1.0f);
    flanger.setFeedback(0.0f);

    // Process two consecutive blocks and compare
    constexpr size_t N = 4096;
    std::vector<float> left1(N, 0.5f);
    std::vector<float> right1(N, 0.5f);
    flanger.processStereo(left1.data(), right1.data(), N);

    std::vector<float> left2(N, 0.5f);
    std::vector<float> right2(N, 0.5f);
    flanger.processStereo(left2.data(), right2.data(), N);

    // With depth=0, the delay time is static at kMinDelayMs, so after warmup
    // the output of consecutive constant-input blocks should be consistent
    bool consistent = true;
    for (size_t i = N - 100; i < N; ++i) {
        if (std::abs(left1[i] - left2[i]) > 0.01f) {
            consistent = false;
            break;
        }
    }
    REQUIRE(consistent);
}

TEST_CASE("Flanger Depth=1.0 produces maximum sweep range", "[flanger][depth]") {
    Flanger flangerMax;
    flangerMax.prepare(44100.0);
    flangerMax.setRate(2.0f);
    flangerMax.setDepth(1.0f);
    flangerMax.setMix(1.0f);
    flangerMax.setFeedback(0.0f);

    Flanger flangerMin;
    flangerMin.prepare(44100.0);
    flangerMin.setRate(2.0f);
    flangerMin.setDepth(0.0f);
    flangerMin.setMix(1.0f);
    flangerMin.setFeedback(0.0f);

    constexpr size_t N = 44100; // 1 second
    std::vector<float> leftMax(N);
    std::vector<float> rightMax(N);
    std::vector<float> leftMin(N);
    std::vector<float> rightMin(N);

    for (size_t i = 0; i < N; ++i) {
        float val = std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 44100.0f);
        leftMax[i] = val;
        rightMax[i] = val;
        leftMin[i] = val;
        rightMin[i] = val;
    }

    flangerMax.processStereo(leftMax.data(), rightMax.data(), N);
    flangerMin.processStereo(leftMin.data(), rightMin.data(), N);

    // Max depth should produce more variation than min depth
    float varianceMax = 0.0f;
    float varianceMin = 0.0f;
    for (size_t i = 1000; i < N; ++i) {
        float diffMax = leftMax[i] - leftMax[i - 1];
        float diffMin = leftMin[i] - leftMin[i - 1];
        varianceMax += diffMax * diffMax;
        varianceMin += diffMin * diffMin;
    }
    // Depth=1.0 should produce noticeably different behavior than depth=0.0
    REQUIRE(varianceMax != Approx(varianceMin).margin(0.001f));
}

// =============================================================================
// User Story 2: Feedback and Tonal Shaping
// =============================================================================

// Helper: compute RMS of a buffer
static float computeRMS(const float* buf, size_t n) {
    float sum = 0.0f;
    for (size_t i = 0; i < n; ++i) {
        sum += buf[i] * buf[i];
    }
    return std::sqrt(sum / static_cast<float>(n));
}

TEST_CASE("Flanger positive feedback increases resonance energy", "[flanger][feedback]") {
    // Use a constant-value signal where positive feedback clearly accumulates
    // energy at the comb filter peaks (DC always adds constructively with + feedback)
    Flanger flangerZero;
    flangerZero.prepare(44100.0);
    flangerZero.setRate(1.0f);
    flangerZero.setDepth(0.5f);
    flangerZero.setMix(0.5f);
    flangerZero.setFeedback(0.0f);

    constexpr size_t N = 44100; // 1 second
    std::vector<float> leftZero(N, 0.5f);
    std::vector<float> rightZero(N, 0.5f);
    flangerZero.processStereo(leftZero.data(), rightZero.data(), N);

    Flanger flangerPos;
    flangerPos.prepare(44100.0);
    flangerPos.setRate(1.0f);
    flangerPos.setDepth(0.5f);
    flangerPos.setMix(0.5f);
    flangerPos.setFeedback(0.95f);

    std::vector<float> leftPos(N, 0.5f);
    std::vector<float> rightPos(N, 0.5f);
    flangerPos.processStereo(leftPos.data(), rightPos.data(), N);

    // Positive feedback on a constant signal should produce higher peak magnitude
    // because the feedback path accumulates (tanh-limited) energy
    float peakZero = 0.0f;
    float peakPos = 0.0f;
    for (size_t i = 4000; i < N; ++i) {
        peakZero = std::max(peakZero, std::abs(leftZero[i]));
        peakPos = std::max(peakPos, std::abs(leftPos[i]));
    }

    REQUIRE(peakPos > peakZero);
}

TEST_CASE("Flanger negative feedback produces different output than positive", "[flanger][feedback]") {
    constexpr size_t N = 44100;

    Flanger flangerPos;
    flangerPos.prepare(44100.0);
    flangerPos.setRate(1.0f);
    flangerPos.setDepth(0.5f);
    flangerPos.setMix(1.0f);
    flangerPos.setFeedback(0.95f);

    std::vector<float> leftPos(N);
    std::vector<float> rightPos(N);
    for (size_t i = 0; i < N; ++i) {
        float val = std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 44100.0f);
        leftPos[i] = val;
        rightPos[i] = val;
    }
    flangerPos.processStereo(leftPos.data(), rightPos.data(), N);

    Flanger flangerNeg;
    flangerNeg.prepare(44100.0);
    flangerNeg.setRate(1.0f);
    flangerNeg.setDepth(0.5f);
    flangerNeg.setMix(1.0f);
    flangerNeg.setFeedback(-0.95f);

    std::vector<float> leftNeg(N);
    std::vector<float> rightNeg(N);
    for (size_t i = 0; i < N; ++i) {
        float val = std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 44100.0f);
        leftNeg[i] = val;
        rightNeg[i] = val;
    }
    flangerNeg.processStereo(leftNeg.data(), rightNeg.data(), N);

    // Positive and negative feedback should produce different spectral content
    bool anyDifference = false;
    for (size_t i = 4000; i < N; ++i) {
        if (std::abs(leftPos[i] - leftNeg[i]) > 1e-6f) {
            anyDifference = true;
            break;
        }
    }
    REQUIRE(anyDifference);
}

TEST_CASE("Flanger feedback=0 matches no-feedback reference", "[flanger][feedback]") {
    constexpr size_t N = 44100;

    // Two identical flangers with feedback=0 should produce identical output
    Flanger flangerA;
    flangerA.prepare(44100.0);
    flangerA.setRate(1.0f);
    flangerA.setDepth(0.5f);
    flangerA.setMix(1.0f);
    flangerA.setFeedback(0.0f);

    Flanger flangerB;
    flangerB.prepare(44100.0);
    flangerB.setRate(1.0f);
    flangerB.setDepth(0.5f);
    flangerB.setMix(1.0f);
    flangerB.setFeedback(0.0f);

    std::vector<float> leftA(N), rightA(N);
    std::vector<float> leftB(N), rightB(N);
    for (size_t i = 0; i < N; ++i) {
        float val = std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 44100.0f);
        leftA[i] = val;
        rightA[i] = val;
        leftB[i] = val;
        rightB[i] = val;
    }

    flangerA.processStereo(leftA.data(), rightA.data(), N);
    flangerB.processStereo(leftB.data(), rightB.data(), N);

    // Identical settings should produce identical output
    bool allMatch = true;
    for (size_t i = 0; i < N; ++i) {
        if (std::abs(leftA[i] - leftB[i]) > 1e-6f) {
            allMatch = false;
            break;
        }
    }
    REQUIRE(allMatch);
}

TEST_CASE("Flanger waveform Sine vs Triangle produces different modulation", "[flanger][waveform]") {
    constexpr size_t N = 44100; // 1 second

    Flanger flangerSine;
    flangerSine.prepare(44100.0);
    flangerSine.setRate(2.0f);
    flangerSine.setDepth(1.0f);
    flangerSine.setMix(1.0f);
    flangerSine.setFeedback(0.0f);
    flangerSine.setWaveform(Waveform::Sine);

    std::vector<float> leftSine(N);
    std::vector<float> rightSine(N);
    for (size_t i = 0; i < N; ++i) {
        float val = std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 44100.0f);
        leftSine[i] = val;
        rightSine[i] = val;
    }
    flangerSine.processStereo(leftSine.data(), rightSine.data(), N);

    Flanger flangerTri;
    flangerTri.prepare(44100.0);
    flangerTri.setRate(2.0f);
    flangerTri.setDepth(1.0f);
    flangerTri.setMix(1.0f);
    flangerTri.setFeedback(0.0f);
    flangerTri.setWaveform(Waveform::Triangle);

    std::vector<float> leftTri(N);
    std::vector<float> rightTri(N);
    for (size_t i = 0; i < N; ++i) {
        float val = std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 44100.0f);
        leftTri[i] = val;
        rightTri[i] = val;
    }
    flangerTri.processStereo(leftTri.data(), rightTri.data(), N);

    // Sine and Triangle waveforms should produce different outputs
    bool anyDifference = false;
    for (size_t i = 1000; i < N; ++i) {
        if (std::abs(leftSine[i] - leftTri[i]) > 1e-6f) {
            anyDifference = true;
            break;
        }
    }
    REQUIRE(anyDifference);
}

TEST_CASE("Flanger stability: 10 seconds at feedback=0.99 remains bounded", "[flanger][stability]") {
    Flanger flanger;
    flanger.prepare(44100.0);
    flanger.setRate(0.5f);
    flanger.setDepth(1.0f);
    flanger.setMix(1.0f);
    flanger.setFeedback(0.99f);

    constexpr size_t N = 44100 * 10; // 10 seconds
    // Process in blocks to avoid huge single allocation
    constexpr size_t kBlockSize = 4096;
    std::array<float, kBlockSize> left{};
    std::array<float, kBlockSize> right{};

    bool hasNaN = false;
    float maxMagnitude = 0.0f;

    size_t remaining = N;
    while (remaining > 0) {
        size_t blockLen = std::min(remaining, kBlockSize);

        // Fill with a test signal
        for (size_t i = 0; i < blockLen; ++i) {
            left[i] = 0.5f;
            right[i] = 0.5f;
        }

        flanger.processStereo(left.data(), right.data(), blockLen);

        for (size_t i = 0; i < blockLen; ++i) {
            if (detail::isNaN(left[i]) || detail::isNaN(right[i])) {
                hasNaN = true;
            }
            maxMagnitude = std::max(maxMagnitude, std::max(std::abs(left[i]), std::abs(right[i])));
        }

        remaining -= blockLen;
    }

    REQUIRE_FALSE(hasNaN);
    REQUIRE(maxMagnitude < 2.0f);
}

// =============================================================================
// User Story 3: Stereo Width and Spread
// =============================================================================

TEST_CASE("Flanger stereo spread=0: L and R outputs are sample-identical", "[flanger][stereo]") {
    Flanger flanger;
    flanger.prepare(44100.0);
    flanger.setRate(1.0f);
    flanger.setDepth(0.5f);
    flanger.setMix(1.0f);
    flanger.setFeedback(0.0f);
    flanger.setStereoSpread(0.0f);

    constexpr size_t N = 4096;
    std::vector<float> left(N);
    std::vector<float> right(N);

    // Feed identical input to both channels
    for (size_t i = 0; i < N; ++i) {
        float val = std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 44100.0f);
        left[i] = val;
        right[i] = val;
    }

    flanger.processStereo(left.data(), right.data(), N);

    // With spread=0 and identical input, L and R should be sample-identical
    bool allMatch = true;
    for (size_t i = 0; i < N; ++i) {
        if (std::abs(left[i] - right[i]) > 1e-7f) {
            allMatch = false;
            break;
        }
    }
    REQUIRE(allMatch);
}

TEST_CASE("Flanger stereo spread=180: L and R outputs diverge", "[flanger][stereo]") {
    Flanger flanger;
    flanger.prepare(44100.0);
    flanger.setRate(1.0f);
    flanger.setDepth(1.0f);
    flanger.setMix(1.0f);
    flanger.setFeedback(0.0f);
    flanger.setStereoSpread(180.0f);

    constexpr size_t N = 44100; // 1 second = 1 full LFO cycle at 1Hz
    std::vector<float> left(N);
    std::vector<float> right(N);

    for (size_t i = 0; i < N; ++i) {
        float val = std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 44100.0f);
        left[i] = val;
        right[i] = val;
    }

    flanger.processStereo(left.data(), right.data(), N);

    // With spread=180, L and R LFOs are in anti-phase, so outputs should differ
    // Accumulate the L-R difference magnitude over the second half (after warmup)
    float diffEnergy = 0.0f;
    for (size_t i = N / 2; i < N; ++i) {
        float diff = left[i] - right[i];
        diffEnergy += diff * diff;
    }

    // Significant difference expected with 180 degrees of spread
    REQUIRE(diffEnergy > 0.01f);
}

TEST_CASE("Flanger stereo spread=90: output differs from spread=0 and spread=180", "[flanger][stereo]") {
    auto processWithSpread = [](float spreadDeg) {
        Flanger flanger;
        flanger.prepare(44100.0);
        flanger.setRate(1.0f);
        flanger.setDepth(1.0f);
        flanger.setMix(1.0f);
        flanger.setFeedback(0.0f);
        flanger.setStereoSpread(spreadDeg);

        constexpr size_t N = 44100;
        std::vector<float> left(N);
        std::vector<float> right(N);
        for (size_t i = 0; i < N; ++i) {
            float val = std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 44100.0f);
            left[i] = val;
            right[i] = val;
        }
        flanger.processStereo(left.data(), right.data(), N);

        // Return L-R energy as a summary metric
        float energy = 0.0f;
        for (size_t i = N / 2; i < N; ++i) {
            float diff = left[i] - right[i];
            energy += diff * diff;
        }
        return energy;
    };

    float energy0 = processWithSpread(0.0f);
    float energy90 = processWithSpread(90.0f);
    float energy180 = processWithSpread(180.0f);

    // Spread=0 should have near-zero L-R difference
    REQUIRE(energy0 < 1e-6f);

    // Spread=90 should differ from both 0 and 180
    REQUIRE(energy90 > energy0 + 0.001f);
    // Spread=90 and 180 should produce different L-R energy values
    REQUIRE(std::abs(energy90 - energy180) > 0.001f);
}

TEST_CASE("Flanger stereo spread: out-of-range values do not crash", "[flanger][stereo]") {
    Flanger flanger;
    flanger.prepare(44100.0);
    flanger.setRate(1.0f);
    flanger.setDepth(0.5f);
    flanger.setMix(0.5f);

    constexpr size_t N = 512;
    std::array<float, N> left{};
    std::array<float, N> right{};

    SECTION("400 degrees wraps gracefully") {
        REQUIRE_NOTHROW(flanger.setStereoSpread(400.0f));
        // 400 mod 360 = 40
        REQUIRE(flanger.getStereoSpread() == Approx(40.0f).margin(0.01f));
        fillStereo(left.data(), right.data(), N, 0.5f);
        REQUIRE_NOTHROW(flanger.processStereo(left.data(), right.data(), N));

        // No NaN
        bool hasNaN = false;
        for (size_t i = 0; i < N; ++i) {
            if (detail::isNaN(left[i]) || detail::isNaN(right[i])) {
                hasNaN = true;
                break;
            }
        }
        REQUIRE_FALSE(hasNaN);
    }

    SECTION("-10 degrees wraps gracefully") {
        REQUIRE_NOTHROW(flanger.setStereoSpread(-10.0f));
        // -10 mod 360 -> 350
        REQUIRE(flanger.getStereoSpread() == Approx(350.0f).margin(0.01f));
        fillStereo(left.data(), right.data(), N, 0.5f);
        REQUIRE_NOTHROW(flanger.processStereo(left.data(), right.data(), N));

        bool hasNaN = false;
        for (size_t i = 0; i < N; ++i) {
            if (detail::isNaN(left[i]) || detail::isNaN(right[i])) {
                hasNaN = true;
                break;
            }
        }
        REQUIRE_FALSE(hasNaN);
    }
}

TEST_CASE("Flanger stereo spread persists after prepare and reset", "[flanger][stereo]") {
    Flanger flanger;
    flanger.setStereoSpread(180.0f);
    flanger.prepare(44100.0);

    // Spread should still be 180 after prepare
    REQUIRE(flanger.getStereoSpread() == Approx(180.0f));

    // Process to verify it actually works (L/R should differ)
    constexpr size_t N = 22050; // half-second
    std::vector<float> left(N);
    std::vector<float> right(N);
    for (size_t i = 0; i < N; ++i) {
        float val = std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 44100.0f);
        left[i] = val;
        right[i] = val;
    }

    flanger.setRate(1.0f);
    flanger.setDepth(1.0f);
    flanger.setMix(1.0f);
    flanger.processStereo(left.data(), right.data(), N);

    float diffEnergy = 0.0f;
    for (size_t i = N / 2; i < N; ++i) {
        float diff = left[i] - right[i];
        diffEnergy += diff * diff;
    }
    REQUIRE(diffEnergy > 0.001f);

    // Now reset and verify spread is still applied
    flanger.reset();
    REQUIRE(flanger.getStereoSpread() == Approx(180.0f));

    // Process again - should still show stereo difference
    for (size_t i = 0; i < N; ++i) {
        float val = std::sin(2.0f * 3.14159265f * 440.0f * static_cast<float>(i) / 44100.0f);
        left[i] = val;
        right[i] = val;
    }
    flanger.processStereo(left.data(), right.data(), N);

    float diffEnergyAfterReset = 0.0f;
    for (size_t i = N / 2; i < N; ++i) {
        float diff = left[i] - right[i];
        diffEnergyAfterReset += diff * diff;
    }
    REQUIRE(diffEnergyAfterReset > 0.001f);
}

TEST_CASE("Flanger feedback clamp: feedback=1.0 does not cause instability", "[flanger][feedback]") {
    Flanger flanger;
    flanger.prepare(44100.0);
    flanger.setRate(1.0f);
    flanger.setDepth(1.0f);
    flanger.setMix(1.0f);
    flanger.setFeedback(1.0f); // Should be internally clamped to 0.98

    // Verify the stored value is clamped to valid range
    REQUIRE(flanger.getFeedback() == Approx(1.0f)); // Stored as 1.0 (within [-1, +1])

    // But processing should be stable because the process loop clamps to 0.98
    constexpr size_t N = 44100 * 5; // 5 seconds
    constexpr size_t kBlockSize = 4096;
    std::array<float, kBlockSize> left{};
    std::array<float, kBlockSize> right{};

    bool hasNaN = false;
    float maxMagnitude = 0.0f;

    size_t remaining = N;
    while (remaining > 0) {
        size_t blockLen = std::min(remaining, kBlockSize);
        for (size_t i = 0; i < blockLen; ++i) {
            left[i] = 0.5f;
            right[i] = 0.5f;
        }

        flanger.processStereo(left.data(), right.data(), blockLen);

        for (size_t i = 0; i < blockLen; ++i) {
            if (detail::isNaN(left[i]) || detail::isNaN(right[i])) {
                hasNaN = true;
            }
            maxMagnitude = std::max(maxMagnitude, std::max(std::abs(left[i]), std::abs(right[i])));
        }

        remaining -= blockLen;
    }

    REQUIRE_FALSE(hasNaN);
    REQUIRE(maxMagnitude < 2.0f);
}

// =============================================================================
// User Story 5: Tempo Sync
// =============================================================================

// Helper: measure LFO period by counting samples between successive upward
// zero-crossings of the left-channel output. We feed a constant input so the
// output variation comes only from the LFO sweeping the delay time. The
// "zero-crossing" here is relative to the DC mean of the output, so we first
// compute the mean then detect crossings.
// Strategy: Feed periodic impulses through the flanger at mix=1.0 (wet only)
// with feedback=0. Each impulse produces exactly one echo at the current LFO
// delay position. By spacing impulses far apart (every 200 samples = ~4.5ms,
// which exceeds the max delay of 4ms), each echo is cleanly separable.
// We record the position of each echo's peak, which tracks the LFO waveform.
// The period of the peak-position oscillation gives us the LFO period.
static float measureFlangerLfoPeriodSamples(Flanger& flanger, [[maybe_unused]] double sampleRate, size_t totalSamples) {
    constexpr size_t kBlockSize = 4096;
    std::array<float, kBlockSize> left{};
    std::array<float, kBlockSize> right{};

    // Space impulses 250 samples apart (> max delay of ~176 samples at 44.1kHz)
    constexpr size_t kImpulseSpacing = 250;

    // Collect delay measurements: for each impulse, find the peak echo position
    // which directly maps to the LFO-controlled delay time
    std::vector<float> delayPositions;

    size_t sampleCounter = 0;
    size_t remaining = totalSamples;
    // Buffer to accumulate output for peak finding
    std::vector<float> output;
    output.reserve(totalSamples);

    while (remaining > 0) {
        size_t blockLen = std::min(remaining, kBlockSize);
        for (size_t i = 0; i < blockLen; ++i) {
            size_t globalIdx = sampleCounter + i;
            // Place impulse every kImpulseSpacing samples
            left[i] = (globalIdx % kImpulseSpacing == 0) ? 1.0f : 0.0f;
            right[i] = left[i];
        }
        flanger.processStereo(left.data(), right.data(), blockLen);
        for (size_t i = 0; i < blockLen; ++i) {
            output.push_back(left[i]);
        }
        sampleCounter += blockLen;
        remaining -= blockLen;
    }

    // For each impulse, find the peak echo within its window
    for (size_t impulseIdx = 1; impulseIdx < totalSamples / kImpulseSpacing; ++impulseIdx) {
        size_t impulsePos = impulseIdx * kImpulseSpacing;
        // Search for the peak in the response window after the impulse
        // (the echo appears 1-200 samples after the impulse position)
        size_t searchStart = impulsePos + 1;
        size_t searchEnd = std::min(impulsePos + kImpulseSpacing, output.size());

        float maxVal = 0.0f;
        size_t maxPos = searchStart;
        for (size_t j = searchStart; j < searchEnd; ++j) {
            float absVal = std::abs(output[j]);
            if (absVal > maxVal) {
                maxVal = absVal;
                maxPos = j;
            }
        }

        // Only record if there was a clear echo (above noise floor)
        if (maxVal > 0.01f) {
            delayPositions.push_back(static_cast<float>(maxPos - impulsePos));
        }
    }

    // Now delayPositions[] traces the LFO waveform (delay in samples over time).
    // Find its period by detecting upward zero-crossings relative to its mean.
    if (delayPositions.size() < 10) {
        return 0.0f;
    }

    // Skip first 15% as warmup
    size_t startIdx = delayPositions.size() * 15 / 100;

    // Compute mean delay
    double sum = 0.0;
    for (size_t i = startIdx; i < delayPositions.size(); ++i) {
        sum += static_cast<double>(delayPositions[i]);
    }
    float meanDelay = static_cast<float>(sum / static_cast<double>(delayPositions.size() - startIdx));

    // Find upward zero-crossings of (delay - meanDelay)
    std::vector<size_t> crossings;
    for (size_t i = startIdx + 1; i < delayPositions.size(); ++i) {
        float prev = delayPositions[i - 1] - meanDelay;
        float curr = delayPositions[i] - meanDelay;
        if (prev <= 0.0f && curr > 0.0f) {
            crossings.push_back(i);
        }
    }

    if (crossings.size() < 2) {
        return 0.0f;
    }

    // Average period in impulse-spacing units, then convert to samples
    double totalPeriod = 0.0;
    for (size_t i = 1; i < crossings.size(); ++i) {
        totalPeriod += static_cast<double>(crossings[i] - crossings[i - 1]);
    }
    double avgPeriodInImpulses = totalPeriod / static_cast<double>(crossings.size() - 1);

    // Convert from impulse indices to samples
    return static_cast<float>(avgPeriodInImpulses * static_cast<double>(kImpulseSpacing));
}

TEST_CASE("Flanger tempo sync off: LFO runs at setRate frequency", "[flanger][temposync]") {
    Flanger flanger;
    flanger.prepare(44100.0);
    flanger.setRate(2.0f);   // 2 Hz -> period = 0.5s = 22050 samples
    flanger.setDepth(1.0f);
    flanger.setMix(1.0f);
    flanger.setFeedback(0.0f);
    flanger.setStereoSpread(0.0f);
    flanger.setTempoSync(false);

    // Process 3 seconds to get at least 6 full cycles
    constexpr size_t N = 44100 * 3;
    float period = measureFlangerLfoPeriodSamples(flanger, 44100.0, N);

    // Expected: 44100 / 2.0 = 22050 samples per cycle
    float expectedPeriod = 44100.0f / 2.0f;
    REQUIRE(period == Approx(expectedPeriod).margin(expectedPeriod * 0.01f));
}

TEST_CASE("Flanger tempo sync on: quarter note at 120 BPM = 0.5s period", "[flanger][temposync]") {
    Flanger flanger;
    flanger.prepare(44100.0);
    flanger.setDepth(1.0f);
    flanger.setMix(1.0f);
    flanger.setFeedback(0.0f);
    flanger.setStereoSpread(0.0f);

    // Enable tempo sync: quarter note at 120 BPM
    // Quarter note = 1 beat. At 120 BPM, 1 beat = 0.5s = 22050 samples
    flanger.setTempoSync(true);
    flanger.setNoteValue(Krate::DSP::NoteValue::Quarter, Krate::DSP::NoteModifier::None);
    flanger.setTempo(120.0);

    constexpr size_t N = 44100 * 3;
    float period = measureFlangerLfoPeriodSamples(flanger, 44100.0, N);

    float expectedPeriod = 44100.0f * 0.5f; // 22050 samples
    REQUIRE(period == Approx(expectedPeriod).margin(expectedPeriod * 0.01f));
}

TEST_CASE("Flanger tempo sync: tempo change 120 -> 140 BPM adjusts period", "[flanger][temposync]") {
    Flanger flanger;
    flanger.prepare(44100.0);
    flanger.setDepth(1.0f);
    flanger.setMix(1.0f);
    flanger.setFeedback(0.0f);
    flanger.setStereoSpread(0.0f);

    // Start at 120 BPM, quarter note
    flanger.setTempoSync(true);
    flanger.setNoteValue(Krate::DSP::NoteValue::Quarter, Krate::DSP::NoteModifier::None);
    flanger.setTempo(120.0);

    // Warm up at 120 BPM for 1 second
    {
        constexpr size_t kWarmup = 44100;
        constexpr size_t kBlockSize = 4096;
        std::array<float, kBlockSize> left{};
        std::array<float, kBlockSize> right{};
        size_t remaining = kWarmup;
        while (remaining > 0) {
            size_t blockLen = std::min(remaining, kBlockSize);
            for (size_t i = 0; i < blockLen; ++i) { left[i] = 0.5f; right[i] = 0.5f; }
            flanger.processStereo(left.data(), right.data(), blockLen);
            remaining -= blockLen;
        }
    }

    // Change to 140 BPM
    flanger.setTempo(140.0);

    // Quarter note at 140 BPM = 60/140 = 0.4286s
    constexpr size_t N = 44100 * 3;
    float period = measureFlangerLfoPeriodSamples(flanger, 44100.0, N);

    float expectedPeriod = 44100.0f * (60.0f / 140.0f); // ~18900 samples
    REQUIRE(period == Approx(expectedPeriod).margin(expectedPeriod * 0.01f));
}

TEST_CASE("Flanger tempo sync: sync enabled with default tempo does not crash", "[flanger][temposync]") {
    Flanger flanger;
    flanger.prepare(44100.0);
    flanger.setDepth(1.0f);
    flanger.setMix(1.0f);
    flanger.setFeedback(0.0f);

    // Enable sync but don't explicitly set tempo (should use default 120 BPM)
    flanger.setTempoSync(true);
    flanger.setNoteValue(Krate::DSP::NoteValue::Quarter, Krate::DSP::NoteModifier::None);

    constexpr size_t N = 44100;
    constexpr size_t kBlockSize = 4096;
    std::array<float, kBlockSize> left{};
    std::array<float, kBlockSize> right{};

    bool hasNaN = false;
    size_t remaining = N;
    while (remaining > 0) {
        size_t blockLen = std::min(remaining, kBlockSize);
        for (size_t i = 0; i < blockLen; ++i) { left[i] = 0.5f; right[i] = 0.5f; }
        flanger.processStereo(left.data(), right.data(), blockLen);
        for (size_t i = 0; i < blockLen; ++i) {
            if (detail::isNaN(left[i]) || detail::isNaN(right[i])) {
                hasNaN = true;
            }
        }
        remaining -= blockLen;
    }
    REQUIRE_FALSE(hasNaN);
}

TEST_CASE("Flanger tempo sync: switching sync on/off does not produce NaN", "[flanger][temposync]") {
    Flanger flanger;
    flanger.prepare(44100.0);
    flanger.setRate(1.0f);
    flanger.setDepth(1.0f);
    flanger.setMix(1.0f);
    flanger.setFeedback(0.5f);
    flanger.setTempo(120.0);
    flanger.setNoteValue(Krate::DSP::NoteValue::Eighth, Krate::DSP::NoteModifier::None);

    constexpr size_t kBlockSize = 512;
    std::array<float, kBlockSize> left{};
    std::array<float, kBlockSize> right{};

    bool hasNaN = false;

    // Process with sync off
    for (size_t i = 0; i < kBlockSize; ++i) { left[i] = 0.5f; right[i] = 0.5f; }
    flanger.processStereo(left.data(), right.data(), kBlockSize);

    // Switch sync on
    flanger.setTempoSync(true);
    for (size_t i = 0; i < kBlockSize; ++i) { left[i] = 0.5f; right[i] = 0.5f; }
    flanger.processStereo(left.data(), right.data(), kBlockSize);

    // Switch sync off
    flanger.setTempoSync(false);
    for (size_t i = 0; i < kBlockSize; ++i) { left[i] = 0.5f; right[i] = 0.5f; }
    flanger.processStereo(left.data(), right.data(), kBlockSize);

    // Switch sync on again
    flanger.setTempoSync(true);
    for (size_t i = 0; i < kBlockSize; ++i) { left[i] = 0.5f; right[i] = 0.5f; }
    flanger.processStereo(left.data(), right.data(), kBlockSize);

    // Check all outputs for NaN
    // (We check after the last block but NaN would propagate through feedback)
    for (size_t i = 0; i < kBlockSize; ++i) {
        if (detail::isNaN(left[i]) || detail::isNaN(right[i])) {
            hasNaN = true;
        }
    }
    REQUIRE_FALSE(hasNaN);
}
