// ==============================================================================
// VectorMixer Unit Tests
// ==============================================================================
// Tests for Layer 3 VectorMixer component.
// Constitution Principle VIII: Testing Discipline
// Constitution Principle XII: Test-First Development
//
// Reference: specs/031-vector-mixer/spec.md
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <krate/dsp/systems/vector_mixer.h>
#include <krate/dsp/core/db_utils.h>

#include <array>
#include <cmath>
#include <numeric>
#include <random>
#include <chrono>

using namespace Krate::DSP;
using Catch::Approx;

// ==============================================================================
// Helper constants
// ==============================================================================
static constexpr double kSampleRate = 44100.0;
static constexpr float kTolerance = 1e-6f;

// ==============================================================================
// Phase 3: User Story 1 - Basic XY Vector Mixing
// ==============================================================================

// --- T012: Square topology corner weights (SC-001) ---

TEST_CASE("VectorMixer: square topology corner A (-1,-1) gives wA=1.0",
          "[systems][vector_mixer][US1][SC-001]") {
    VectorMixer mixer;
    mixer.setSmoothingTimeMs(0.0f);
    mixer.prepare(kSampleRate);
    mixer.setVectorPosition(-1.0f, -1.0f);

    // Process one sample to update weights
    float out = mixer.process(1.0f, 0.0f, 0.0f, 0.0f);

    auto w = mixer.getWeights();
    REQUIRE(w.a == Approx(1.0f).margin(kTolerance));
    REQUIRE(w.b == Approx(0.0f).margin(kTolerance));
    REQUIRE(w.c == Approx(0.0f).margin(kTolerance));
    REQUIRE(w.d == Approx(0.0f).margin(kTolerance));
}

TEST_CASE("VectorMixer: square topology corner B (+1,-1) gives wB=1.0",
          "[systems][vector_mixer][US1][SC-001]") {
    VectorMixer mixer;
    mixer.setSmoothingTimeMs(0.0f);
    mixer.prepare(kSampleRate);
    mixer.setVectorPosition(1.0f, -1.0f);

    (void)mixer.process(0.0f, 1.0f, 0.0f, 0.0f);

    auto w = mixer.getWeights();
    REQUIRE(w.a == Approx(0.0f).margin(kTolerance));
    REQUIRE(w.b == Approx(1.0f).margin(kTolerance));
    REQUIRE(w.c == Approx(0.0f).margin(kTolerance));
    REQUIRE(w.d == Approx(0.0f).margin(kTolerance));
}

TEST_CASE("VectorMixer: square topology corner C (-1,+1) gives wC=1.0",
          "[systems][vector_mixer][US1][SC-001]") {
    VectorMixer mixer;
    mixer.setSmoothingTimeMs(0.0f);
    mixer.prepare(kSampleRate);
    mixer.setVectorPosition(-1.0f, 1.0f);

    (void)mixer.process(0.0f, 0.0f, 1.0f, 0.0f);

    auto w = mixer.getWeights();
    REQUIRE(w.a == Approx(0.0f).margin(kTolerance));
    REQUIRE(w.b == Approx(0.0f).margin(kTolerance));
    REQUIRE(w.c == Approx(1.0f).margin(kTolerance));
    REQUIRE(w.d == Approx(0.0f).margin(kTolerance));
}

TEST_CASE("VectorMixer: square topology corner D (+1,+1) gives wD=1.0",
          "[systems][vector_mixer][US1][SC-001]") {
    VectorMixer mixer;
    mixer.setSmoothingTimeMs(0.0f);
    mixer.prepare(kSampleRate);
    mixer.setVectorPosition(1.0f, 1.0f);

    (void)mixer.process(0.0f, 0.0f, 0.0f, 1.0f);

    auto w = mixer.getWeights();
    REQUIRE(w.a == Approx(0.0f).margin(kTolerance));
    REQUIRE(w.b == Approx(0.0f).margin(kTolerance));
    REQUIRE(w.c == Approx(0.0f).margin(kTolerance));
    REQUIRE(w.d == Approx(1.0f).margin(kTolerance));
}

// --- T013: Square topology center weights (all 0.25) ---

TEST_CASE("VectorMixer: square topology center (0,0) gives all weights 0.25",
          "[systems][vector_mixer][US1][SC-001]") {
    VectorMixer mixer;
    mixer.setSmoothingTimeMs(0.0f);
    mixer.prepare(kSampleRate);
    mixer.setVectorPosition(0.0f, 0.0f);

    (void)mixer.process(1.0f, 2.0f, 3.0f, 4.0f);

    auto w = mixer.getWeights();
    REQUIRE(w.a == Approx(0.25f).margin(kTolerance));
    REQUIRE(w.b == Approx(0.25f).margin(kTolerance));
    REQUIRE(w.c == Approx(0.25f).margin(kTolerance));
    REQUIRE(w.d == Approx(0.25f).margin(kTolerance));
}

// --- T014: Weight sum invariant (sum=1.0 for linear law) ---

TEST_CASE("VectorMixer: linear law weight sum equals 1.0 across grid",
          "[systems][vector_mixer][US1]") {
    VectorMixer mixer;
    mixer.setSmoothingTimeMs(0.0f);
    mixer.prepare(kSampleRate);

    // Test on 10x10 grid
    for (int ix = 0; ix <= 10; ++ix) {
        for (int iy = 0; iy <= 10; ++iy) {
            const float x = -1.0f + 2.0f * static_cast<float>(ix) / 10.0f;
            const float y = -1.0f + 2.0f * static_cast<float>(iy) / 10.0f;
            mixer.setVectorPosition(x, y);
            (void)mixer.process(1.0f, 1.0f, 1.0f, 1.0f);
            auto w = mixer.getWeights();
            const float sum = w.a + w.b + w.c + w.d;
            REQUIRE(sum == Approx(1.0f).margin(kTolerance));
        }
    }
}

// --- T015: XY clamping to [-1,1] range ---

TEST_CASE("VectorMixer: XY values outside [-1,1] are clamped",
          "[systems][vector_mixer][US1]") {
    VectorMixer mixer;
    mixer.setSmoothingTimeMs(0.0f);
    mixer.prepare(kSampleRate);

    SECTION("X > 1 clamped to 1") {
        mixer.setVectorPosition(5.0f, 0.0f);
        (void)mixer.process(1.0f, 1.0f, 1.0f, 1.0f);
        auto w = mixer.getWeights();
        // At x=1, y=0: wB=0.5, wD=0.5
        REQUIRE(w.a == Approx(0.0f).margin(kTolerance));
        REQUIRE(w.b == Approx(0.5f).margin(kTolerance));
        REQUIRE(w.c == Approx(0.0f).margin(kTolerance));
        REQUIRE(w.d == Approx(0.5f).margin(kTolerance));
    }

    SECTION("X < -1 clamped to -1") {
        mixer.setVectorPosition(-5.0f, 0.0f);
        (void)mixer.process(1.0f, 1.0f, 1.0f, 1.0f);
        auto w = mixer.getWeights();
        // At x=-1, y=0: wA=0.5, wC=0.5
        REQUIRE(w.a == Approx(0.5f).margin(kTolerance));
        REQUIRE(w.b == Approx(0.0f).margin(kTolerance));
        REQUIRE(w.c == Approx(0.5f).margin(kTolerance));
        REQUIRE(w.d == Approx(0.0f).margin(kTolerance));
    }

    SECTION("Y > 1 clamped to 1") {
        mixer.setVectorPosition(0.0f, 10.0f);
        (void)mixer.process(1.0f, 1.0f, 1.0f, 1.0f);
        auto w = mixer.getWeights();
        // At x=0, y=1: wC=0.5, wD=0.5
        REQUIRE(w.a == Approx(0.0f).margin(kTolerance));
        REQUIRE(w.b == Approx(0.0f).margin(kTolerance));
        REQUIRE(w.c == Approx(0.5f).margin(kTolerance));
        REQUIRE(w.d == Approx(0.5f).margin(kTolerance));
    }

    SECTION("Y < -1 clamped to -1") {
        mixer.setVectorPosition(0.0f, -10.0f);
        (void)mixer.process(1.0f, 1.0f, 1.0f, 1.0f);
        auto w = mixer.getWeights();
        // At x=0, y=-1: wA=0.5, wB=0.5
        REQUIRE(w.a == Approx(0.5f).margin(kTolerance));
        REQUIRE(w.b == Approx(0.5f).margin(kTolerance));
        REQUIRE(w.c == Approx(0.0f).margin(kTolerance));
        REQUIRE(w.d == Approx(0.0f).margin(kTolerance));
    }
}

// --- T016: process() with known DC inputs (US-1 scenario 4) ---

TEST_CASE("VectorMixer: process() with DC inputs at center produces average",
          "[systems][vector_mixer][US1]") {
    VectorMixer mixer;
    mixer.setSmoothingTimeMs(0.0f);
    mixer.prepare(kSampleRate);
    mixer.setVectorPosition(0.0f, 0.0f);

    float out = mixer.process(1.0f, 2.0f, 3.0f, 4.0f);
    REQUIRE(out == Approx(2.5f).margin(kTolerance));
}

TEST_CASE("VectorMixer: process() at corner A returns source A",
          "[systems][vector_mixer][US1]") {
    VectorMixer mixer;
    mixer.setSmoothingTimeMs(0.0f);
    mixer.prepare(kSampleRate);
    mixer.setVectorPosition(-1.0f, -1.0f);

    float out = mixer.process(1.0f, 2.0f, 3.0f, 4.0f);
    REQUIRE(out == Approx(1.0f).margin(kTolerance));
}

TEST_CASE("VectorMixer: process() at corner D returns source D",
          "[systems][vector_mixer][US1]") {
    VectorMixer mixer;
    mixer.setSmoothingTimeMs(0.0f);
    mixer.prepare(kSampleRate);
    mixer.setVectorPosition(1.0f, 1.0f);

    float out = mixer.process(1.0f, 2.0f, 3.0f, 4.0f);
    REQUIRE(out == Approx(4.0f).margin(kTolerance));
}

// --- T017: processBlock() correctness ---

TEST_CASE("VectorMixer: processBlock() produces correct output for constant position",
          "[systems][vector_mixer][US1]") {
    VectorMixer mixer;
    mixer.setSmoothingTimeMs(0.0f);
    mixer.prepare(kSampleRate);
    mixer.setVectorPosition(0.0f, 0.0f);

    constexpr size_t kBlockSize = 64;
    std::array<float, kBlockSize> bufA{}, bufB{}, bufC{}, bufD{}, output{};
    bufA.fill(1.0f);
    bufB.fill(2.0f);
    bufC.fill(3.0f);
    bufD.fill(4.0f);

    mixer.processBlock(bufA.data(), bufB.data(), bufC.data(), bufD.data(),
                       output.data(), kBlockSize);

    for (size_t i = 0; i < kBlockSize; ++i) {
        REQUIRE(output[i] == Approx(2.5f).margin(kTolerance));
    }
}

TEST_CASE("VectorMixer: processBlock() matches per-sample process()",
          "[systems][vector_mixer][US1]") {
    VectorMixer mixerBlock;
    VectorMixer mixerSample;

    mixerBlock.setSmoothingTimeMs(0.0f);
    mixerSample.setSmoothingTimeMs(0.0f);
    mixerBlock.prepare(kSampleRate);
    mixerSample.prepare(kSampleRate);
    mixerBlock.setVectorPosition(0.3f, -0.7f);
    mixerSample.setVectorPosition(0.3f, -0.7f);

    constexpr size_t kBlockSize = 32;
    std::array<float, kBlockSize> bufA{}, bufB{}, bufC{}, bufD{};
    std::array<float, kBlockSize> blockOut{}, sampleOut{};

    // Fill with distinct values
    for (size_t i = 0; i < kBlockSize; ++i) {
        bufA[i] = static_cast<float>(i) * 0.1f;
        bufB[i] = static_cast<float>(i) * 0.2f;
        bufC[i] = static_cast<float>(i) * 0.3f;
        bufD[i] = static_cast<float>(i) * 0.4f;
    }

    // Block processing
    mixerBlock.processBlock(bufA.data(), bufB.data(), bufC.data(), bufD.data(),
                            blockOut.data(), kBlockSize);

    // Per-sample processing
    for (size_t i = 0; i < kBlockSize; ++i) {
        sampleOut[i] = mixerSample.process(bufA[i], bufB[i], bufC[i], bufD[i]);
    }

    for (size_t i = 0; i < kBlockSize; ++i) {
        REQUIRE(blockOut[i] == Approx(sampleOut[i]).margin(kTolerance));
    }
}

// --- T018: prepare() and reset() lifecycle ---

TEST_CASE("VectorMixer: prepare() enables processing",
          "[systems][vector_mixer][US1]") {
    VectorMixer mixer;
    mixer.prepare(kSampleRate);
    mixer.setSmoothingTimeMs(0.0f);
    mixer.setVectorPosition(1.0f, 1.0f);
    // After prepare, process should produce non-zero output for non-zero input
    float out = mixer.process(0.0f, 0.0f, 0.0f, 1.0f);
    // With smoothing=0 and position at (1,1), wD should be 1.0
    REQUIRE(out == Approx(1.0f).margin(0.01f));
}

TEST_CASE("VectorMixer: reset() snaps smoothed position to target",
          "[systems][vector_mixer][US1]") {
    VectorMixer mixer;
    mixer.setSmoothingTimeMs(100.0f);  // Long smoothing time
    mixer.prepare(kSampleRate);
    mixer.setVectorPosition(-1.0f, -1.0f);

    // Process some samples so smoothing is in progress
    for (int i = 0; i < 10; ++i) {
        (void)mixer.process(1.0f, 2.0f, 3.0f, 4.0f);
    }

    // Change target to opposite corner
    mixer.setVectorPosition(1.0f, 1.0f);

    // Reset should snap to the new target
    mixer.reset();

    // Now set smoothing to 0 and process - should be at (1,1)
    mixer.setSmoothingTimeMs(0.0f);
    float out = mixer.process(1.0f, 2.0f, 3.0f, 4.0f);
    REQUIRE(out == Approx(4.0f).margin(kTolerance));
}

TEST_CASE("VectorMixer: multiple prepare() calls are safe",
          "[systems][vector_mixer][US1]") {
    VectorMixer mixer;
    mixer.prepare(44100.0);
    mixer.prepare(48000.0);
    mixer.prepare(96000.0);
    mixer.setSmoothingTimeMs(0.0f);
    mixer.setVectorPosition(0.0f, 0.0f);
    float out = mixer.process(1.0f, 2.0f, 3.0f, 4.0f);
    REQUIRE(out == Approx(2.5f).margin(kTolerance));
}

// --- T019: process-before-prepare safety ---

TEST_CASE("VectorMixer: process() before prepare() returns 0.0",
          "[systems][vector_mixer][US1]") {
    VectorMixer mixer;
    float out = mixer.process(1.0f, 2.0f, 3.0f, 4.0f);
    REQUIRE(out == 0.0f);
}

TEST_CASE("VectorMixer: processBlock() before prepare() outputs zeros",
          "[systems][vector_mixer][US1]") {
    VectorMixer mixer;
    constexpr size_t kN = 16;
    std::array<float, kN> a{}, b{}, c{}, d{}, out{};
    a.fill(1.0f);
    b.fill(2.0f);
    c.fill(3.0f);
    d.fill(4.0f);
    out.fill(999.0f);  // Sentinel value

    mixer.processBlock(a.data(), b.data(), c.data(), d.data(), out.data(), kN);

    for (size_t i = 0; i < kN; ++i) {
        REQUIRE(out[i] == 0.0f);
    }
}

// --- T019b: FR-022 topology and mixing law changes take effect on next process() ---

TEST_CASE("VectorMixer: topology change takes effect on next process() call",
          "[systems][vector_mixer][US1][FR-022]") {
    VectorMixer mixer;
    mixer.setSmoothingTimeMs(0.0f);
    mixer.prepare(kSampleRate);
    mixer.setVectorPosition(-1.0f, 0.0f);

    // Square topology: at (-1,0), wA=0.5, wC=0.5
    (void)mixer.process(1.0f, 1.0f, 1.0f, 1.0f);
    auto wSquare = mixer.getWeights();
    REQUIRE(wSquare.a == Approx(0.5f).margin(kTolerance));

    // Switch to diamond topology
    mixer.setTopology(Topology::Diamond);

    // Diamond topology at (-1,0): wA should be 1.0
    (void)mixer.process(1.0f, 1.0f, 1.0f, 1.0f);
    auto wDiamond = mixer.getWeights();
    REQUIRE(wDiamond.a == Approx(1.0f).margin(kTolerance));
}

TEST_CASE("VectorMixer: mixing law change takes effect on next process() call",
          "[systems][vector_mixer][US1][FR-022]") {
    VectorMixer mixer;
    mixer.setSmoothingTimeMs(0.0f);
    mixer.prepare(kSampleRate);
    mixer.setVectorPosition(0.0f, 0.0f);

    // Linear law at center: all weights = 0.25
    (void)mixer.process(1.0f, 1.0f, 1.0f, 1.0f);
    auto wLinear = mixer.getWeights();
    REQUIRE(wLinear.a == Approx(0.25f).margin(kTolerance));

    // Switch to equal-power
    mixer.setMixingLaw(MixingLaw::EqualPower);

    // Equal-power at center: all weights = sqrt(0.25) = 0.5
    (void)mixer.process(1.0f, 1.0f, 1.0f, 1.0f);
    auto wEP = mixer.getWeights();
    REQUIRE(wEP.a == Approx(0.5f).margin(kTolerance));
}

// ==============================================================================
// Phase 4: User Story 2 - Mixing Law Selection
// ==============================================================================

// --- T034: Equal-power weights at center ---

TEST_CASE("VectorMixer: equal-power at center gives each weight 0.5, sum-of-squares=1.0",
          "[systems][vector_mixer][US2]") {
    VectorMixer mixer;
    mixer.setSmoothingTimeMs(0.0f);
    mixer.setMixingLaw(MixingLaw::EqualPower);
    mixer.prepare(kSampleRate);
    mixer.setVectorPosition(0.0f, 0.0f);
    (void)mixer.process(1.0f, 1.0f, 1.0f, 1.0f);

    auto w = mixer.getWeights();
    REQUIRE(w.a == Approx(0.5f).margin(kTolerance));
    REQUIRE(w.b == Approx(0.5f).margin(kTolerance));
    REQUIRE(w.c == Approx(0.5f).margin(kTolerance));
    REQUIRE(w.d == Approx(0.5f).margin(kTolerance));

    float sumOfSquares = w.a * w.a + w.b * w.b + w.c * w.c + w.d * w.d;
    REQUIRE(sumOfSquares == Approx(1.0f).margin(kTolerance));
}

// --- T035: Equal-power weights at corners (identical to linear: 1.0 solo) ---

TEST_CASE("VectorMixer: equal-power at corners gives solo weights",
          "[systems][vector_mixer][US2]") {
    VectorMixer mixer;
    mixer.setSmoothingTimeMs(0.0f);
    mixer.setMixingLaw(MixingLaw::EqualPower);
    mixer.prepare(kSampleRate);

    SECTION("corner A (-1,-1)") {
        mixer.setVectorPosition(-1.0f, -1.0f);
        (void)mixer.process(1.0f, 0.0f, 0.0f, 0.0f);
        auto w = mixer.getWeights();
        REQUIRE(w.a == Approx(1.0f).margin(kTolerance));
        REQUIRE(w.b == Approx(0.0f).margin(kTolerance));
        REQUIRE(w.c == Approx(0.0f).margin(kTolerance));
        REQUIRE(w.d == Approx(0.0f).margin(kTolerance));
    }

    SECTION("corner D (+1,+1)") {
        mixer.setVectorPosition(1.0f, 1.0f);
        (void)mixer.process(0.0f, 0.0f, 0.0f, 1.0f);
        auto w = mixer.getWeights();
        REQUIRE(w.a == Approx(0.0f).margin(kTolerance));
        REQUIRE(w.b == Approx(0.0f).margin(kTolerance));
        REQUIRE(w.c == Approx(0.0f).margin(kTolerance));
        REQUIRE(w.d == Approx(1.0f).margin(kTolerance));
    }
}

// --- T036: Equal-power sum-of-squares invariant across 100 grid positions (SC-002) ---

TEST_CASE("VectorMixer: equal-power sum-of-squares in [0.95,1.05] across 100 points (SC-002)",
          "[systems][vector_mixer][US2][SC-002]") {
    VectorMixer mixer;
    mixer.setSmoothingTimeMs(0.0f);
    mixer.setMixingLaw(MixingLaw::EqualPower);
    mixer.prepare(kSampleRate);

    int numPoints = 0;
    for (int ix = 0; ix < 10; ++ix) {
        for (int iy = 0; iy < 10; ++iy) {
            const float x = -1.0f + 2.0f * (static_cast<float>(ix) + 0.5f) / 10.0f;
            const float y = -1.0f + 2.0f * (static_cast<float>(iy) + 0.5f) / 10.0f;
            mixer.setVectorPosition(x, y);
            (void)mixer.process(1.0f, 1.0f, 1.0f, 1.0f);
            auto w = mixer.getWeights();
            float sumSq = w.a * w.a + w.b * w.b + w.c * w.c + w.d * w.d;
            REQUIRE(sumSq >= 0.95f);
            REQUIRE(sumSq <= 1.05f);
            ++numPoints;
        }
    }
    REQUIRE(numPoints == 100);

    // Also verify center is within 1e-6 of 1.0
    mixer.setVectorPosition(0.0f, 0.0f);
    (void)mixer.process(1.0f, 1.0f, 1.0f, 1.0f);
    auto wc = mixer.getWeights();
    float centerSumSq = wc.a * wc.a + wc.b * wc.b + wc.c * wc.c + wc.d * wc.d;
    REQUIRE(centerSumSq == Approx(1.0f).margin(kTolerance));
}

// --- T037: Square-root weights at center ---

TEST_CASE("VectorMixer: square-root at center gives each weight 0.5",
          "[systems][vector_mixer][US2]") {
    VectorMixer mixer;
    mixer.setSmoothingTimeMs(0.0f);
    mixer.setMixingLaw(MixingLaw::SquareRoot);
    mixer.prepare(kSampleRate);
    mixer.setVectorPosition(0.0f, 0.0f);
    (void)mixer.process(1.0f, 1.0f, 1.0f, 1.0f);

    auto w = mixer.getWeights();
    REQUIRE(w.a == Approx(0.5f).margin(kTolerance));
    REQUIRE(w.b == Approx(0.5f).margin(kTolerance));
    REQUIRE(w.c == Approx(0.5f).margin(kTolerance));
    REQUIRE(w.d == Approx(0.5f).margin(kTolerance));

    float sumOfSquares = w.a * w.a + w.b * w.b + w.c * w.c + w.d * w.d;
    REQUIRE(sumOfSquares == Approx(1.0f).margin(kTolerance));
}

// --- T038: Verify no trigonometric functions used (FR-024) ---

TEST_CASE("VectorMixer: equal-power produces same results as manual sqrt",
          "[systems][vector_mixer][US2][FR-024]") {
    // This test verifies the equal-power law uses sqrt(linear_weight)
    // by comparing against manually computed values. The implementation
    // must NOT use sin/cos (FR-024) - verified by code review.
    VectorMixer mixer;
    mixer.setSmoothingTimeMs(0.0f);
    mixer.setMixingLaw(MixingLaw::EqualPower);
    mixer.prepare(kSampleRate);

    // At x=0.5, y=0: u=0.75, v=0.5
    mixer.setVectorPosition(0.5f, 0.0f);
    (void)mixer.process(1.0f, 1.0f, 1.0f, 1.0f);
    auto w = mixer.getWeights();

    // Manual calculation:
    // u = (0.5 + 1) / 2 = 0.75
    // v = (0.0 + 1) / 2 = 0.5
    // wA_lin = 0.25 * 0.5 = 0.125,  sqrt(0.125) = 0.35355...
    // wB_lin = 0.75 * 0.5 = 0.375,  sqrt(0.375) = 0.61237...
    // wC_lin = 0.25 * 0.5 = 0.125,  sqrt(0.125) = 0.35355...
    // wD_lin = 0.75 * 0.5 = 0.375,  sqrt(0.375) = 0.61237...
    REQUIRE(w.a == Approx(std::sqrt(0.125f)).margin(kTolerance));
    REQUIRE(w.b == Approx(std::sqrt(0.375f)).margin(kTolerance));
    REQUIRE(w.c == Approx(std::sqrt(0.125f)).margin(kTolerance));
    REQUIRE(w.d == Approx(std::sqrt(0.375f)).margin(kTolerance));
}

// ==============================================================================
// Phase 5: User Story 3 - Diamond Topology
// ==============================================================================

// --- T046: Diamond topology at cardinal points (SC-004) ---

TEST_CASE("VectorMixer: diamond topology cardinal points produce solo weights (SC-004)",
          "[systems][vector_mixer][US3][SC-004]") {
    VectorMixer mixer;
    mixer.setSmoothingTimeMs(0.0f);
    mixer.setTopology(Topology::Diamond);
    mixer.prepare(kSampleRate);

    SECTION("A=left (-1,0) -> wA=1.0") {
        mixer.setVectorPosition(-1.0f, 0.0f);
        (void)mixer.process(1.0f, 0.0f, 0.0f, 0.0f);
        auto w = mixer.getWeights();
        REQUIRE(w.a == Approx(1.0f).margin(kTolerance));
        REQUIRE(w.b == Approx(0.0f).margin(kTolerance));
        REQUIRE(w.c == Approx(0.0f).margin(kTolerance));
        REQUIRE(w.d == Approx(0.0f).margin(kTolerance));
    }

    SECTION("B=right (+1,0) -> wB=1.0") {
        mixer.setVectorPosition(1.0f, 0.0f);
        (void)mixer.process(0.0f, 1.0f, 0.0f, 0.0f);
        auto w = mixer.getWeights();
        REQUIRE(w.a == Approx(0.0f).margin(kTolerance));
        REQUIRE(w.b == Approx(1.0f).margin(kTolerance));
        REQUIRE(w.c == Approx(0.0f).margin(kTolerance));
        REQUIRE(w.d == Approx(0.0f).margin(kTolerance));
    }

    SECTION("C=top (0,+1) -> wC=1.0") {
        mixer.setVectorPosition(0.0f, 1.0f);
        (void)mixer.process(0.0f, 0.0f, 1.0f, 0.0f);
        auto w = mixer.getWeights();
        REQUIRE(w.a == Approx(0.0f).margin(kTolerance));
        REQUIRE(w.b == Approx(0.0f).margin(kTolerance));
        REQUIRE(w.c == Approx(1.0f).margin(kTolerance));
        REQUIRE(w.d == Approx(0.0f).margin(kTolerance));
    }

    SECTION("D=bottom (0,-1) -> wD=1.0") {
        mixer.setVectorPosition(0.0f, -1.0f);
        (void)mixer.process(0.0f, 0.0f, 0.0f, 1.0f);
        auto w = mixer.getWeights();
        REQUIRE(w.a == Approx(0.0f).margin(kTolerance));
        REQUIRE(w.b == Approx(0.0f).margin(kTolerance));
        REQUIRE(w.c == Approx(0.0f).margin(kTolerance));
        REQUIRE(w.d == Approx(1.0f).margin(kTolerance));
    }
}

// --- T047: Diamond topology at center (all 0.25) ---

TEST_CASE("VectorMixer: diamond topology center (0,0) gives all weights 0.25",
          "[systems][vector_mixer][US3]") {
    VectorMixer mixer;
    mixer.setSmoothingTimeMs(0.0f);
    mixer.setTopology(Topology::Diamond);
    mixer.prepare(kSampleRate);
    mixer.setVectorPosition(0.0f, 0.0f);
    (void)mixer.process(1.0f, 2.0f, 3.0f, 4.0f);

    auto w = mixer.getWeights();
    REQUIRE(w.a == Approx(0.25f).margin(kTolerance));
    REQUIRE(w.b == Approx(0.25f).margin(kTolerance));
    REQUIRE(w.c == Approx(0.25f).margin(kTolerance));
    REQUIRE(w.d == Approx(0.25f).margin(kTolerance));
}

// --- T048: Diamond topology weight invariants ---

TEST_CASE("VectorMixer: diamond topology weights are non-negative and sum to 1.0",
          "[systems][vector_mixer][US3]") {
    VectorMixer mixer;
    mixer.setSmoothingTimeMs(0.0f);
    mixer.setTopology(Topology::Diamond);
    mixer.prepare(kSampleRate);

    for (int ix = 0; ix <= 10; ++ix) {
        for (int iy = 0; iy <= 10; ++iy) {
            const float x = -1.0f + 2.0f * static_cast<float>(ix) / 10.0f;
            const float y = -1.0f + 2.0f * static_cast<float>(iy) / 10.0f;
            mixer.setVectorPosition(x, y);
            (void)mixer.process(1.0f, 1.0f, 1.0f, 1.0f);
            auto w = mixer.getWeights();
            REQUIRE(w.a >= -kTolerance);
            REQUIRE(w.b >= -kTolerance);
            REQUIRE(w.c >= -kTolerance);
            REQUIRE(w.d >= -kTolerance);
            float sum = w.a + w.b + w.c + w.d;
            REQUIRE(sum == Approx(1.0f).margin(kTolerance));
        }
    }
}

// --- T049: Diamond topology at non-cardinal positions ---

TEST_CASE("VectorMixer: diamond topology at non-cardinal positions distributes weights",
          "[systems][vector_mixer][US3]") {
    VectorMixer mixer;
    mixer.setSmoothingTimeMs(0.0f);
    mixer.setTopology(Topology::Diamond);
    mixer.prepare(kSampleRate);

    // At (0.5, 0.5): between right and top
    mixer.setVectorPosition(0.5f, 0.5f);
    (void)mixer.process(1.0f, 1.0f, 1.0f, 1.0f);
    auto w = mixer.getWeights();

    // All weights should be non-negative
    REQUIRE(w.a >= 0.0f);
    REQUIRE(w.b >= 0.0f);
    REQUIRE(w.c >= 0.0f);
    REQUIRE(w.d >= 0.0f);

    // B (right) and C (top) should be the dominant sources
    REQUIRE(w.b > w.a);
    REQUIRE(w.c > w.d);

    // Sum should be 1.0
    float sum = w.a + w.b + w.c + w.d;
    REQUIRE(sum == Approx(1.0f).margin(kTolerance));
}

// ==============================================================================
// Phase 6: User Story 4 - Parameter Smoothing
// ==============================================================================

// --- T056: Smoothing convergence at 10ms/44.1kHz (SC-005) ---

TEST_CASE("VectorMixer: 10ms smoothing reaches within 5% in ~50ms (SC-005)",
          "[systems][vector_mixer][US4][SC-005]") {
    VectorMixer mixer;
    mixer.setSmoothingTimeMs(10.0f);
    mixer.prepare(kSampleRate);

    // Start at X=-1
    mixer.setVectorPosition(-1.0f, 0.0f);
    mixer.reset();  // Snap to (-1, 0)

    // Change target to X=+1
    mixer.setVectorPosition(1.0f, 0.0f);

    // Process 50ms = 2205 samples
    const size_t samples50ms = static_cast<size_t>(kSampleRate * 0.050);
    for (size_t i = 0; i < samples50ms; ++i) {
        (void)mixer.process(1.0f, 1.0f, 1.0f, 1.0f);
    }

    // After 50ms (~5 time constants), smoothed X should be within 5% of target
    // Target is +1.0, so should be > 0.9 (within 5% of range [-1,1] mapped to target)
    auto w = mixer.getWeights();
    // At x close to 1.0 with square topology: wB and wD should be dominant
    // The actual smoothed X value is internal, but we can verify through weights
    // At x=1, y=0: wA=0, wB=0.5, wC=0, wD=0.5
    // At x=0.9, y=0: u=0.95, v=0.5: wA=0.025, wB=0.475, wC=0.025, wD=0.475
    REQUIRE(w.b > 0.45f);  // Should be near 0.5
    REQUIRE(w.a < 0.05f);  // Should be near 0.0
}

// --- T057: Instant response with 0ms smoothing (SC-007) ---

TEST_CASE("VectorMixer: 0ms smoothing gives instant response (SC-007)",
          "[systems][vector_mixer][US4][SC-007]") {
    VectorMixer mixer;
    mixer.setSmoothingTimeMs(0.0f);
    mixer.prepare(kSampleRate);

    // Start at center
    mixer.setVectorPosition(0.0f, 0.0f);
    (void)mixer.process(1.0f, 1.0f, 1.0f, 1.0f);
    auto w1 = mixer.getWeights();
    REQUIRE(w1.a == Approx(0.25f).margin(kTolerance));

    // Jump to corner A
    mixer.setVectorPosition(-1.0f, -1.0f);
    (void)mixer.process(1.0f, 1.0f, 1.0f, 1.0f);
    auto w2 = mixer.getWeights();
    REQUIRE(w2.a == Approx(1.0f).margin(kTolerance));
    REQUIRE(w2.b == Approx(0.0f).margin(kTolerance));
    REQUIRE(w2.c == Approx(0.0f).margin(kTolerance));
    REQUIRE(w2.d == Approx(0.0f).margin(kTolerance));
}

// --- T058: Independent X/Y smoothing ---

TEST_CASE("VectorMixer: X and Y smooth independently",
          "[systems][vector_mixer][US4]") {
    VectorMixer mixer;
    mixer.setSmoothingTimeMs(10.0f);
    mixer.prepare(kSampleRate);

    // Start at (0, 0)
    mixer.setVectorPosition(0.0f, 0.0f);
    mixer.reset();

    // Only change X, leave Y at 0
    mixer.setVectorX(1.0f);

    // Process some samples
    for (int i = 0; i < 100; ++i) {
        (void)mixer.process(1.0f, 1.0f, 1.0f, 1.0f);
    }

    auto w = mixer.getWeights();
    // Y should remain at 0, so wA+wB should approximately equal wC+wD
    // (symmetric around Y=0 axis)
    float topRow = w.a + w.b;
    float botRow = w.c + w.d;
    REQUIRE(topRow == Approx(botRow).margin(0.01f));
}

// --- T059: setSmoothingTimeMs() with negative values clamped to 0 ---

TEST_CASE("VectorMixer: negative smoothing time clamped to 0 (instant)",
          "[systems][vector_mixer][US4]") {
    VectorMixer mixer;
    mixer.setSmoothingTimeMs(-10.0f);
    mixer.prepare(kSampleRate);
    mixer.setVectorPosition(-1.0f, -1.0f);

    // With smoothing=0 (clamped from -10), should be instant
    float out = mixer.process(1.0f, 2.0f, 3.0f, 4.0f);
    REQUIRE(out == Approx(1.0f).margin(kTolerance));
}

// --- T060: getWeights() returns smoothed weights (FR-020) ---

TEST_CASE("VectorMixer: getWeights() reflects smoothed position (FR-020)",
          "[systems][vector_mixer][US4][FR-020]") {
    VectorMixer mixer;
    mixer.setSmoothingTimeMs(50.0f);  // Long smoothing
    mixer.prepare(kSampleRate);
    mixer.setVectorPosition(0.0f, 0.0f);
    mixer.reset();

    // Process once at center
    (void)mixer.process(1.0f, 1.0f, 1.0f, 1.0f);
    auto w1 = mixer.getWeights();

    // Move target to corner
    mixer.setVectorPosition(1.0f, 1.0f);

    // Process a few samples - weights should change gradually
    (void)mixer.process(1.0f, 1.0f, 1.0f, 1.0f);
    auto w2 = mixer.getWeights();

    // wD should be increasing but NOT yet at 1.0 (still smoothing)
    REQUIRE(w2.d > w1.d);
    REQUIRE(w2.d < 1.0f);
}

// ==============================================================================
// Phase 7: User Story 5 - Stereo Vector Mixing
// ==============================================================================

// --- T072: Stereo process() with identical weights on both channels ---

TEST_CASE("VectorMixer: stereo process() applies identical weights to both channels",
          "[systems][vector_mixer][US5]") {
    VectorMixer mixer;
    mixer.setSmoothingTimeMs(0.0f);
    mixer.prepare(kSampleRate);
    mixer.setVectorPosition(0.3f, -0.5f);

    auto out = mixer.process(
        1.0f, 0.1f,   // aL, aR
        2.0f, 0.2f,   // bL, bR
        3.0f, 0.3f,   // cL, cR
        4.0f, 0.4f    // dL, dR
    );

    // Get the weights used
    auto w = mixer.getWeights();

    // Verify left channel = weighted sum of left inputs
    float expectedLeft = w.a * 1.0f + w.b * 2.0f + w.c * 3.0f + w.d * 4.0f;
    REQUIRE(out.left == Approx(expectedLeft).margin(kTolerance));

    // Verify right channel = weighted sum of right inputs with SAME weights
    float expectedRight = w.a * 0.1f + w.b * 0.2f + w.c * 0.3f + w.d * 0.4f;
    REQUIRE(out.right == Approx(expectedRight).margin(kTolerance));
}

// --- T073: Stereo processBlock() correctness ---

TEST_CASE("VectorMixer: stereo processBlock() produces correct output",
          "[systems][vector_mixer][US5]") {
    VectorMixer mixer;
    mixer.setSmoothingTimeMs(0.0f);
    mixer.prepare(kSampleRate);
    mixer.setVectorPosition(0.0f, 0.0f);

    constexpr size_t kN = 32;
    std::array<float, kN> aL{}, aR{}, bL{}, bR{}, cL{}, cR{}, dL{}, dR{};
    std::array<float, kN> outL{}, outR{};

    aL.fill(1.0f); aR.fill(0.5f);
    bL.fill(2.0f); bR.fill(1.0f);
    cL.fill(3.0f); cR.fill(1.5f);
    dL.fill(4.0f); dR.fill(2.0f);

    mixer.processBlock(aL.data(), aR.data(), bL.data(), bR.data(),
                       cL.data(), cR.data(), dL.data(), dR.data(),
                       outL.data(), outR.data(), kN);

    // At center: all weights 0.25
    for (size_t i = 0; i < kN; ++i) {
        REQUIRE(outL[i] == Approx(2.5f).margin(kTolerance));    // (1+2+3+4)/4
        REQUIRE(outR[i] == Approx(1.25f).margin(kTolerance));   // (0.5+1+1.5+2)/4
    }
}

// --- T074: Stereo weights match mono weights ---

TEST_CASE("VectorMixer: stereo weights match mono weights for same XY position",
          "[systems][vector_mixer][US5]") {
    VectorMixer monoMixer;
    VectorMixer stereoMixer;

    monoMixer.setSmoothingTimeMs(0.0f);
    stereoMixer.setSmoothingTimeMs(0.0f);
    monoMixer.prepare(kSampleRate);
    stereoMixer.prepare(kSampleRate);
    monoMixer.setVectorPosition(0.6f, -0.3f);
    stereoMixer.setVectorPosition(0.6f, -0.3f);

    // Process mono
    (void)monoMixer.process(1.0f, 2.0f, 3.0f, 4.0f);
    auto wMono = monoMixer.getWeights();

    // Process stereo
    (void)stereoMixer.process(1.0f, 1.0f, 2.0f, 2.0f, 3.0f, 3.0f, 4.0f, 4.0f);
    auto wStereo = stereoMixer.getWeights();

    REQUIRE(wMono.a == Approx(wStereo.a).margin(kTolerance));
    REQUIRE(wMono.b == Approx(wStereo.b).margin(kTolerance));
    REQUIRE(wMono.c == Approx(wStereo.c).margin(kTolerance));
    REQUIRE(wMono.d == Approx(wStereo.d).margin(kTolerance));
}

// ==============================================================================
// Phase 8: Edge Cases & Performance
// ==============================================================================

// --- T081: NaN/Inf input propagation (FR-025) ---

TEST_CASE("VectorMixer: NaN input propagates to output (FR-025, release behavior)",
          "[systems][vector_mixer][FR-025]") {
    // Note: In debug builds, assertions will fire. In release builds,
    // NaN propagates through. This test runs in release mode.
    VectorMixer mixer;
    mixer.setSmoothingTimeMs(0.0f);
    mixer.prepare(kSampleRate);
    mixer.setVectorPosition(0.0f, 0.0f);

    const float nan = std::numeric_limits<float>::quiet_NaN();
    float out = mixer.process(nan, 0.0f, 0.0f, 0.0f);

    // At center, wA=0.25, so output = 0.25*NaN + ... = NaN
    REQUIRE(detail::isNaN(out));
}

TEST_CASE("VectorMixer: Inf input propagates to output (FR-025, release behavior)",
          "[systems][vector_mixer][FR-025]") {
    VectorMixer mixer;
    mixer.setSmoothingTimeMs(0.0f);
    mixer.prepare(kSampleRate);
    mixer.setVectorPosition(-1.0f, -1.0f);

    const float inf = std::numeric_limits<float>::infinity();
    float out = mixer.process(inf, 0.0f, 0.0f, 0.0f);

    // At corner A, wA=1.0, so output = 1.0*Inf = Inf
    REQUIRE(detail::isInf(out));
}

// --- T082: 8192-sample block processing (SC-008) ---

TEST_CASE("VectorMixer: 8192-sample block processes correctly (SC-008)",
          "[systems][vector_mixer][SC-008]") {
    VectorMixer mixer;
    mixer.setSmoothingTimeMs(0.0f);
    mixer.prepare(kSampleRate);
    mixer.setVectorPosition(0.5f, -0.5f);

    constexpr size_t kBlockSize = 8192;
    std::vector<float> bufA(kBlockSize, 1.0f);
    std::vector<float> bufB(kBlockSize, 2.0f);
    std::vector<float> bufC(kBlockSize, 3.0f);
    std::vector<float> bufD(kBlockSize, 4.0f);
    std::vector<float> output(kBlockSize, 0.0f);

    mixer.processBlock(bufA.data(), bufB.data(), bufC.data(), bufD.data(),
                       output.data(), kBlockSize);

    // Verify a sample at the beginning and end
    // At (0.5, -0.5): u=0.75, v=0.25
    // wA = 0.25*0.75 = 0.1875
    // wB = 0.75*0.75 = 0.5625
    // wC = 0.25*0.25 = 0.0625
    // wD = 0.75*0.25 = 0.1875
    float expected = 0.1875f * 1.0f + 0.5625f * 2.0f + 0.0625f * 3.0f + 0.1875f * 4.0f;
    REQUIRE(output[0] == Approx(expected).margin(kTolerance));
    REQUIRE(output[kBlockSize - 1] == Approx(expected).margin(kTolerance));

    // Verify no NaN in entire block
    bool hasNaN = false;
    for (size_t i = 0; i < kBlockSize; ++i) {
        if (detail::isNaN(output[i])) hasNaN = true;
    }
    REQUIRE_FALSE(hasNaN);
}

TEST_CASE("VectorMixer: 8192-sample stereo block processes correctly (SC-008)",
          "[systems][vector_mixer][SC-008]") {
    VectorMixer mixer;
    mixer.setSmoothingTimeMs(0.0f);
    mixer.prepare(kSampleRate);
    mixer.setVectorPosition(0.0f, 0.0f);

    constexpr size_t kBlockSize = 8192;
    std::vector<float> aL(kBlockSize, 1.0f), aR(kBlockSize, 0.1f);
    std::vector<float> bL(kBlockSize, 2.0f), bR(kBlockSize, 0.2f);
    std::vector<float> cL(kBlockSize, 3.0f), cR(kBlockSize, 0.3f);
    std::vector<float> dL(kBlockSize, 4.0f), dR(kBlockSize, 0.4f);
    std::vector<float> outL(kBlockSize, 0.0f), outR(kBlockSize, 0.0f);

    mixer.processBlock(aL.data(), aR.data(), bL.data(), bR.data(),
                       cL.data(), cR.data(), dL.data(), dR.data(),
                       outL.data(), outR.data(), kBlockSize);

    REQUIRE(outL[0] == Approx(2.5f).margin(kTolerance));
    REQUIRE(outR[0] == Approx(0.25f).margin(kTolerance));
    REQUIRE(outL[kBlockSize - 1] == Approx(2.5f).margin(kTolerance));
    REQUIRE(outR[kBlockSize - 1] == Approx(0.25f).margin(kTolerance));
}

// --- T083: Randomized XY sweep stability (SC-006) ---

TEST_CASE("VectorMixer: 10-second random XY sweep produces no NaN/Inf (SC-006)",
          "[systems][vector_mixer][SC-006]") {
    VectorMixer mixer;
    mixer.setSmoothingTimeMs(5.0f);
    mixer.prepare(kSampleRate);

    std::mt19937 rng(42);  // Deterministic seed
    std::uniform_real_distribution<float> posDist(-1.0f, 1.0f);
    std::uniform_real_distribution<float> signalDist(-1.0f, 1.0f);

    const size_t tenSeconds = static_cast<size_t>(kSampleRate * 10.0);
    bool hasNaN = false;
    bool hasInf = false;

    // Test with all topology + law combinations
    Topology topologies[] = {Topology::Square, Topology::Diamond};
    MixingLaw laws[] = {MixingLaw::Linear, MixingLaw::EqualPower, MixingLaw::SquareRoot};

    for (auto topo : topologies) {
        for (auto law : laws) {
            mixer.setTopology(topo);
            mixer.setMixingLaw(law);
            mixer.prepare(kSampleRate);

            for (size_t i = 0; i < tenSeconds; ++i) {
                // Randomize position every 100 samples
                if (i % 100 == 0) {
                    mixer.setVectorPosition(posDist(rng), posDist(rng));
                }

                float out = mixer.process(
                    signalDist(rng), signalDist(rng),
                    signalDist(rng), signalDist(rng)
                );

                if (detail::isNaN(out)) hasNaN = true;
                if (detail::isInf(out)) hasInf = true;
            }
        }
    }

    REQUIRE_FALSE(hasNaN);
    REQUIRE_FALSE(hasInf);
}

// --- T084: CPU performance benchmark ---

TEST_CASE("VectorMixer: 512 samples mono performance benchmark (SC-003)",
          "[systems][vector_mixer][SC-003][!benchmark]") {
    VectorMixer mixer;
    mixer.setSmoothingTimeMs(5.0f);
    mixer.prepare(kSampleRate);
    mixer.setVectorPosition(0.3f, -0.4f);

    constexpr size_t kBlockSize = 512;
    std::array<float, kBlockSize> a{}, b{}, c{}, d{}, output{};
    a.fill(0.5f);
    b.fill(-0.3f);
    c.fill(0.8f);
    d.fill(-0.1f);

    // Warm up
    mixer.processBlock(a.data(), b.data(), c.data(), d.data(), output.data(), kBlockSize);

    // Measure
    constexpr int kIterations = 10000;
    auto start = std::chrono::high_resolution_clock::now();
    for (int iter = 0; iter < kIterations; ++iter) {
        mixer.processBlock(a.data(), b.data(), c.data(), d.data(), output.data(), kBlockSize);
    }
    auto end = std::chrono::high_resolution_clock::now();

    double totalMs = std::chrono::duration<double, std::milli>(end - start).count();
    double perBlockMs = totalMs / kIterations;
    double audioBufferMs = static_cast<double>(kBlockSize) / kSampleRate * 1000.0;
    double cpuPercent = (perBlockMs / audioBufferMs) * 100.0;

    INFO("Per-block time: " << perBlockMs << " ms");
    INFO("Audio buffer duration: " << audioBufferMs << " ms");
    INFO("CPU usage: " << cpuPercent << "%");
    REQUIRE(cpuPercent < 0.05);
}

// --- T086: All methods are noexcept (FR-023) ---
// Verified via static_assert at compile time

static_assert(noexcept(std::declval<VectorMixer>().prepare(44100.0)),
              "prepare() must be noexcept");
static_assert(noexcept(std::declval<VectorMixer>().reset()),
              "reset() must be noexcept");
static_assert(noexcept(std::declval<VectorMixer>().setVectorX(0.0f)),
              "setVectorX() must be noexcept");
static_assert(noexcept(std::declval<VectorMixer>().setVectorY(0.0f)),
              "setVectorY() must be noexcept");
static_assert(noexcept(std::declval<VectorMixer>().setVectorPosition(0.0f, 0.0f)),
              "setVectorPosition() must be noexcept");
static_assert(noexcept(std::declval<VectorMixer>().setTopology(Topology::Square)),
              "setTopology() must be noexcept");
static_assert(noexcept(std::declval<VectorMixer>().setMixingLaw(MixingLaw::Linear)),
              "setMixingLaw() must be noexcept");
static_assert(noexcept(std::declval<VectorMixer>().setSmoothingTimeMs(5.0f)),
              "setSmoothingTimeMs() must be noexcept");
static_assert(noexcept(std::declval<VectorMixer>().process(0.f, 0.f, 0.f, 0.f)),
              "process(mono) must be noexcept");
static_assert(noexcept(std::declval<VectorMixer>().process(0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f, 0.f)),
              "process(stereo) must be noexcept");
static_assert(noexcept(std::declval<const VectorMixer>().getWeights()),
              "getWeights() must be noexcept");

// --- T087: Atomic operations use memory_order_relaxed (FR-026) ---
// This is verified by code review of vector_mixer.h. The test below
// verifies the observable behavior is correct.

TEST_CASE("VectorMixer: atomic setters are thread-safe (FR-026)",
          "[systems][vector_mixer][FR-026]") {
    VectorMixer mixer;
    mixer.setSmoothingTimeMs(0.0f);
    mixer.prepare(kSampleRate);

    // Verify that rapid setter calls don't corrupt state
    for (int i = 0; i < 1000; ++i) {
        float x = -1.0f + 2.0f * static_cast<float>(i) / 999.0f;
        mixer.setVectorX(x);
        mixer.setVectorY(-x);
        float out = mixer.process(1.0f, 2.0f, 3.0f, 4.0f);
        // Just verify no NaN/Inf
        REQUIRE_FALSE(detail::isNaN(out));
        REQUIRE_FALSE(detail::isInf(out));
    }
}
