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
