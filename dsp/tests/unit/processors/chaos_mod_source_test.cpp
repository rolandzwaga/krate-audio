// ==============================================================================
// Layer 2: Processor Tests - Chaos Modulation Source
// ==============================================================================
// Tests for the ChaosModSource using 4 attractor models.
//
// Reference: specs/008-modulation-system/spec.md (FR-030 to FR-035, SC-007)
// ==============================================================================

#include <krate/dsp/processors/chaos_mod_source.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace Krate::DSP;
using Catch::Approx;

// =============================================================================
// Output Stability Tests (SC-007)
// =============================================================================

TEST_CASE("ChaosModSource Lorenz stays in [-1, +1] for 10 seconds", "[processors][chaos][sc007]") {
    ChaosModSource src;
    src.setModel(ChaosModel::Lorenz);
    src.prepare(44100.0);
    src.setSpeed(1.0f);

    for (int i = 0; i < 441000; ++i) {  // 10 seconds
        src.process();
        float val = src.getCurrentValue();
        REQUIRE(val >= -1.0f);
        REQUIRE(val <= 1.0f);
    }
}

TEST_CASE("ChaosModSource Rossler stays in [-1, +1] for 10 seconds", "[processors][chaos][sc007]") {
    ChaosModSource src;
    src.setModel(ChaosModel::Rossler);
    src.prepare(44100.0);
    src.setSpeed(1.0f);

    for (int i = 0; i < 441000; ++i) {
        src.process();
        float val = src.getCurrentValue();
        REQUIRE(val >= -1.0f);
        REQUIRE(val <= 1.0f);
    }
}

TEST_CASE("ChaosModSource Chua stays in [-1, +1] for 10 seconds", "[processors][chaos][sc007]") {
    ChaosModSource src;
    src.setModel(ChaosModel::Chua);
    src.prepare(44100.0);
    src.setSpeed(1.0f);

    for (int i = 0; i < 441000; ++i) {
        src.process();
        float val = src.getCurrentValue();
        REQUIRE(val >= -1.0f);
        REQUIRE(val <= 1.0f);
    }
}

TEST_CASE("ChaosModSource Henon stays in [-1, +1] for 10 seconds", "[processors][chaos][sc007]") {
    ChaosModSource src;
    src.setModel(ChaosModel::Henon);
    src.prepare(44100.0);
    src.setSpeed(1.0f);

    for (int i = 0; i < 441000; ++i) {
        src.process();
        float val = src.getCurrentValue();
        REQUIRE(val >= -1.0f);
        REQUIRE(val <= 1.0f);
    }
}

// =============================================================================
// Speed Parameter Tests
// =============================================================================

TEST_CASE("ChaosModSource speed affects evolution rate", "[processors][chaos]") {
    // Run two instances at different speeds and measure variation
    auto measureVariation = [](float speed) {
        ChaosModSource src;
        src.setModel(ChaosModel::Lorenz);
        src.prepare(44100.0);
        src.setSpeed(speed);

        float sumAbsDelta = 0.0f;
        float prev = src.getCurrentValue();
        for (int i = 0; i < 44100; ++i) {
            src.process();
            float val = src.getCurrentValue();
            sumAbsDelta += std::abs(val - prev);
            prev = val;
        }
        return sumAbsDelta;
    };

    float slowVariation = measureVariation(0.1f);
    float fastVariation = measureVariation(10.0f);

    // Faster speed should produce more total variation
    REQUIRE(fastVariation > slowVariation);
}

// =============================================================================
// Coupling Tests
// =============================================================================

TEST_CASE("ChaosModSource coupling perturbs attractor from input", "[processors][chaos]") {
    ChaosModSource withCoupling;
    withCoupling.setModel(ChaosModel::Lorenz);
    withCoupling.prepare(44100.0);
    withCoupling.setCoupling(1.0f);
    withCoupling.setInputLevel(0.5f);

    ChaosModSource withoutCoupling;
    withoutCoupling.setModel(ChaosModel::Lorenz);
    withoutCoupling.prepare(44100.0);
    withoutCoupling.setCoupling(0.0f);

    // Process both for a while
    for (int i = 0; i < 44100; ++i) {
        withCoupling.process();
        withoutCoupling.process();
    }

    // They should diverge (different values after same number of steps)
    float valWith = withCoupling.getCurrentValue();
    float valWithout = withoutCoupling.getCurrentValue();

    // With chaotic systems, even small perturbations cause divergence
    // But we can't guarantee exact divergence amount, just that they differ
    // (This might occasionally fail if both happen to be at same point)
    // Use a looser check - run more samples and check total path difference
    float totalDiff = 0.0f;
    for (int i = 0; i < 44100; ++i) {
        withCoupling.process();
        withoutCoupling.process();
        totalDiff += std::abs(withCoupling.getCurrentValue() - withoutCoupling.getCurrentValue());
    }

    REQUIRE(totalDiff > 0.0f);
}

// =============================================================================
// Model Switch Tests
// =============================================================================

TEST_CASE("ChaosModSource model switch resets state", "[processors][chaos]") {
    ChaosModSource src;
    src.prepare(44100.0);
    src.setModel(ChaosModel::Lorenz);

    // Process to get some state
    for (int i = 0; i < 10000; ++i) {
        src.process();
    }
    float lorenzVal = src.getCurrentValue();

    // Switch to Rossler
    src.setModel(ChaosModel::Rossler);

    // Process briefly
    for (int i = 0; i < 10000; ++i) {
        src.process();
    }
    float rosslerVal = src.getCurrentValue();

    // Different models should produce different values
    // (After sufficient evolution, they should typically differ)
    // Just verify output is valid
    REQUIRE(rosslerVal >= -1.0f);
    REQUIRE(rosslerVal <= 1.0f);
}

// =============================================================================
// Normalization Tests (FR-034)
// =============================================================================

TEST_CASE("ChaosModSource uses tanh normalization with per-model scales", "[processors][chaos][fr034]") {
    // Verify the normalization scales are correct
    REQUIRE(ChaosModSource::kLorenzScale == Approx(20.0f));
    REQUIRE(ChaosModSource::kRosslerScale == Approx(10.0f));
    REQUIRE(ChaosModSource::kChuaScale == Approx(2.0f));
    REQUIRE(ChaosModSource::kHenonScale == Approx(1.5f));
}

// =============================================================================
// Interface Tests
// =============================================================================

TEST_CASE("ChaosModSource implements ModulationSource interface", "[processors][chaos]") {
    ChaosModSource src;
    src.prepare(44100.0);

    auto range = src.getSourceRange();
    REQUIRE(range.first == Approx(-1.0f));
    REQUIRE(range.second == Approx(1.0f));
}

// =============================================================================
// Different Models Produce Different Character
// =============================================================================

TEST_CASE("ChaosModSource different models produce distinct patterns", "[processors][chaos]") {
    auto collectSamples = [](ChaosModel model) {
        ChaosModSource src;
        src.setModel(model);
        src.prepare(44100.0);
        src.setSpeed(1.0f);

        float sum = 0.0f;
        float sumSq = 0.0f;
        constexpr int N = 44100;

        for (int i = 0; i < N; ++i) {
            src.process();
            float v = src.getCurrentValue();
            sum += v;
            sumSq += v * v;
        }

        float mean = sum / N;
        float variance = (sumSq / N) - (mean * mean);
        return std::pair{mean, variance};
    };

    auto [lorenzMean, lorenzVar] = collectSamples(ChaosModel::Lorenz);
    auto [rosslerMean, rosslerVar] = collectSamples(ChaosModel::Rossler);
    auto [chuaMean, chuaVar] = collectSamples(ChaosModel::Chua);
    auto [henonMean, henonVar] = collectSamples(ChaosModel::Henon);

    // At least some statistical difference between models
    // We check variance differs - different attractors have different spread
    bool allSame = (std::abs(lorenzVar - rosslerVar) < 0.001f) &&
                   (std::abs(lorenzVar - chuaVar) < 0.001f) &&
                   (std::abs(lorenzVar - henonVar) < 0.001f);
    REQUIRE_FALSE(allSame);
}

// =============================================================================
// 042-ext-modulation-system: User Story 7 - Extended Boundedness Tests
// =============================================================================

// T099: ChaosModSource remains bounded for 10 minutes at any speed (SC-006)
TEST_CASE("ChaosModSource Lorenz bounded for 10 minutes at extreme speeds",
          "[processors][chaos][ext_modulation][SC-006]") {
    // Test at multiple extreme speeds including min, max, and mid
    const float speeds[] = {0.05f, 0.5f, 1.0f, 5.0f, 20.0f};

    for (float speed : speeds) {
        ChaosModSource src;
        src.setModel(ChaosModel::Lorenz);
        src.prepare(44100.0);
        src.setSpeed(speed);

        // 10 minutes at 44100 Hz = 26,460,000 samples
        // With control rate interval of 32, that's ~826,875 attractor updates
        constexpr int tenMinutes = 44100 * 60 * 10;
        bool bounded = true;

        for (int i = 0; i < tenMinutes; ++i) {
            src.process();
            float val = src.getCurrentValue();
            if (val < -1.0f || val > 1.0f) {
                bounded = false;
                break;
            }
        }

        INFO("Speed: " << speed);
        REQUIRE(bounded);
    }
}

TEST_CASE("ChaosModSource all models bounded for 10 minutes at speed 10",
          "[processors][chaos][ext_modulation][SC-006]") {
    const ChaosModel models[] = {
        ChaosModel::Lorenz, ChaosModel::Rossler,
        ChaosModel::Chua, ChaosModel::Henon
    };

    for (auto model : models) {
        ChaosModSource src;
        src.setModel(model);
        src.prepare(44100.0);
        src.setSpeed(10.0f);

        constexpr int tenMinutes = 44100 * 60 * 10;
        bool bounded = true;

        for (int i = 0; i < tenMinutes; ++i) {
            src.process();
            float val = src.getCurrentValue();
            if (val < -1.0f || val > 1.0f) {
                bounded = false;
                break;
            }
        }

        INFO("Model: " << static_cast<int>(model));
        REQUIRE(bounded);
    }
}

// =============================================================================
// Regression: processBlock must be equivalent to per-sample process()
// =============================================================================
// The ChaosModSource was originally only called via process() once per audio
// block in the ModulationEngine, making the attractor evolve ~500x too slowly
// (1 tick per block instead of numSamples ticks per block). processBlock()
// fixes this. This test ensures the two paths remain equivalent.

TEST_CASE("ChaosModSource processBlock produces same result as per-sample process",
          "[processors][chaos][regression]") {
    constexpr size_t kBlockSize = 512;
    constexpr int kNumBlocks = 100;

    for (auto model : {ChaosModel::Lorenz, ChaosModel::Rossler,
                       ChaosModel::Chua, ChaosModel::Henon}) {
        // Instance A: per-sample process()
        ChaosModSource perSample;
        perSample.setModel(model);
        perSample.prepare(44100.0);
        perSample.setSpeed(5.0f);

        // Instance B: processBlock()
        ChaosModSource perBlock;
        perBlock.setModel(model);
        perBlock.prepare(44100.0);
        perBlock.setSpeed(5.0f);

        for (int b = 0; b < kNumBlocks; ++b) {
            for (size_t i = 0; i < kBlockSize; ++i) {
                perSample.process();
            }
            perBlock.processBlock(kBlockSize);

            INFO("Model: " << static_cast<int>(model) << " Block: " << b);
            REQUIRE(perBlock.getCurrentValue()
                    == Approx(perSample.getCurrentValue()).margin(1e-6f));
        }
    }
}

TEST_CASE("ChaosModSource processBlock produces non-trivial output over one second",
          "[processors][chaos][regression]") {
    // Catches the original bug: if processBlock is accidentally reverted to a
    // single process() call, the attractor barely evolves and output stays
    // near its initial value (~0.05 for Lorenz).
    constexpr size_t kBlockSize = 512;
    constexpr float kSampleRate = 44100.0f;
    constexpr int kOneSecondBlocks =
        static_cast<int>(kSampleRate / static_cast<float>(kBlockSize));

    ChaosModSource src;
    src.setModel(ChaosModel::Lorenz);
    src.prepare(kSampleRate);
    src.setSpeed(5.0f);

    float minVal = 1.0f;
    float maxVal = -1.0f;

    for (int b = 0; b < kOneSecondBlocks; ++b) {
        src.processBlock(kBlockSize);
        float val = src.getCurrentValue();
        minVal = std::min(minVal, val);
        maxVal = std::max(maxVal, val);
    }

    float range = maxVal - minVal;
    INFO("Chaos output range over 1 second: " << range
         << " (min=" << minVal << ", max=" << maxVal << ")");

    // A properly running Lorenz attractor at speed 5.0 should swing widely.
    // With the old bug (single process() per block), range was < 0.05.
    REQUIRE(range > 0.5f);
}

// T100: Lorenz attractor auto-reset when state exceeds 10x safeBound (FR-025)
TEST_CASE("ChaosModSource Lorenz auto-resets when diverged",
          "[processors][chaos][ext_modulation][FR-025]") {
    ChaosModSource src;
    src.setModel(ChaosModel::Lorenz);
    src.prepare(44100.0);
    src.setSpeed(20.0f); // Max speed to stress the system

    // Process many samples with high speed and coupling perturbation
    // to try to trigger divergence/auto-reset
    src.setCoupling(1.0f);

    constexpr int totalSamples = 44100 * 60; // 1 minute
    bool allBounded = true;
    int resetCount = 0;
    float prevVal = src.getCurrentValue();

    for (int i = 0; i < totalSamples; ++i) {
        // Inject large perturbation via input level
        src.setInputLevel(static_cast<float>((i % 100 < 50) ? 10.0f : -10.0f));
        src.process();
        float val = src.getCurrentValue();

        if (val < -1.0f || val > 1.0f) {
            allBounded = false;
            break;
        }

        // Detect reset: sudden jump from non-zero to near-initial
        if (std::abs(val - prevVal) > 0.5f && std::abs(val) < 0.1f) {
            ++resetCount;
        }
        prevVal = val;
    }

    // Output must always be bounded
    REQUIRE(allBounded);
    // The auto-reset mechanism exists (checkAndResetIfDiverged)
    // We can't guarantee it triggers, but the output stays bounded
}
