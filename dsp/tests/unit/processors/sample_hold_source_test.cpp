// ==============================================================================
// Layer 2: Processor Tests - Sample & Hold Source
// ==============================================================================
// Tests for the SampleHoldSource modulation source.
//
// Reference: specs/008-modulation-system/spec.md (FR-036 to FR-040, SC-017)
// ==============================================================================

#include <krate/dsp/processors/sample_hold_source.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <vector>

using namespace Krate::DSP;
using Catch::Approx;

// =============================================================================
// Hold Behavior Tests
// =============================================================================

TEST_CASE("SampleHoldSource holds value between samples", "[processors][sample_hold]") {
    SampleHoldSource src;
    src.prepare(44100.0);
    src.setInputType(SampleHoldInputType::Random);
    src.setRate(2.0f);  // 2 Hz = hold for ~22050 samples
    src.setSlewTime(0.0f);  // No slew

    // Process past first trigger to get initial value
    for (int i = 0; i < 22100; ++i) {
        src.process();
    }

    float heldValue = src.getCurrentValue();

    // Process several hundred more samples - value should remain held
    bool allSame = true;
    for (int i = 0; i < 1000; ++i) {
        src.process();
        if (std::abs(src.getCurrentValue() - heldValue) > 0.001f) {
            allSame = false;
            break;
        }
    }

    REQUIRE(allSame);
}

// =============================================================================
// Rate Tests
// =============================================================================

TEST_CASE("SampleHoldSource rate controls sampling frequency", "[processors][sample_hold]") {
    auto countChanges = [](float rate, int numSamples) {
        SampleHoldSource src;
        src.prepare(44100.0);
        src.setInputType(SampleHoldInputType::Random);
        src.setRate(rate);
        src.setSlewTime(0.0f);

        int changes = 0;
        float prev = src.getCurrentValue();
        for (int i = 0; i < numSamples; ++i) {
            src.process();
            float val = src.getCurrentValue();
            if (std::abs(val - prev) > 0.001f) {
                ++changes;
                prev = val;
            }
        }
        return changes;
    };

    // Use 2 seconds to give enough time for triggers at slow rates
    int slowChanges = countChanges(2.0f, 88200);   // 2 Hz for 2 seconds = ~4 triggers
    int fastChanges = countChanges(20.0f, 88200);   // 20 Hz for 2 seconds = ~40 triggers

    // Faster rate should produce more value changes
    REQUIRE(fastChanges > slowChanges);

    // At 2 Hz for 2 seconds, we expect approximately 4 changes
    REQUIRE(slowChanges >= 2);
    REQUIRE(slowChanges <= 8);

    // At 20 Hz for 2 seconds, we expect approximately 40 changes
    REQUIRE(fastChanges >= 30);
    REQUIRE(fastChanges <= 50);
}

// =============================================================================
// Slew Tests (SC-017)
// =============================================================================

TEST_CASE("SampleHoldSource slew smooths transitions", "[processors][sample_hold][sc017]") {
    auto measureMaxJump = [](float slewMs) {
        SampleHoldSource src;
        src.prepare(44100.0);
        src.setInputType(SampleHoldInputType::Random);
        src.setRate(10.0f);  // Fast triggers
        src.setSlewTime(slewMs);

        float maxJump = 0.0f;
        float prev = src.getCurrentValue();
        for (int i = 0; i < 44100; ++i) {
            src.process();
            float val = src.getCurrentValue();
            float jump = std::abs(val - prev);
            maxJump = std::max(maxJump, jump);
            prev = val;
        }
        return maxJump;
    };

    float noSlewMaxJump = measureMaxJump(0.0f);
    float slewMaxJump = measureMaxJump(200.0f);

    // With slew, max per-sample jump should be smaller
    REQUIRE(slewMaxJump < noSlewMaxJump);
}

// =============================================================================
// Input Source Tests (FR-037)
// =============================================================================

TEST_CASE("SampleHoldSource Random input produces random values", "[processors][sample_hold]") {
    SampleHoldSource src;
    src.prepare(44100.0);
    src.setInputType(SampleHoldInputType::Random);
    src.setRate(20.0f);
    src.setSlewTime(0.0f);

    // Collect several held values
    bool hasVariation = false;
    float firstVal = 0.0f;
    bool gotFirst = false;

    for (int i = 0; i < 44100; ++i) {
        src.process();
    }
    firstVal = src.getCurrentValue();
    gotFirst = true;

    for (int i = 0; i < 44100; ++i) {
        src.process();
        if (gotFirst && std::abs(src.getCurrentValue() - firstVal) > 0.1f) {
            hasVariation = true;
            break;
        }
    }

    REQUIRE(hasVariation);
}

TEST_CASE("SampleHoldSource External input returns external level", "[processors][sample_hold]") {
    SampleHoldSource src;
    src.prepare(44100.0);
    src.setInputType(SampleHoldInputType::External);
    src.setRate(50.0f);  // Fast sampling
    src.setSlewTime(0.0f);
    src.setExternalLevel(0.75f);

    // Process enough to trigger several times
    for (int i = 0; i < 4410; ++i) {
        src.process();
    }

    // Value should be close to the external level (0.75)
    float val = src.getCurrentValue();
    REQUIRE(val == Approx(0.75f).margin(0.05f));
}

// =============================================================================
// Output Range Tests (FR-040)
// =============================================================================

TEST_CASE("SampleHoldSource Random output range is [-1, +1]", "[processors][sample_hold][fr040]") {
    SampleHoldSource src;
    src.prepare(44100.0);
    src.setInputType(SampleHoldInputType::Random);
    src.setRate(50.0f);
    src.setSlewTime(0.0f);

    auto range = src.getSourceRange();
    REQUIRE(range.first == Approx(-1.0f));
    REQUIRE(range.second == Approx(1.0f));
}

TEST_CASE("SampleHoldSource External output range is [0, +1]", "[processors][sample_hold][fr040]") {
    SampleHoldSource src;
    src.prepare(44100.0);
    src.setInputType(SampleHoldInputType::External);

    auto range = src.getSourceRange();
    REQUIRE(range.first == Approx(0.0f));
    REQUIRE(range.second == Approx(1.0f));
}

// =============================================================================
// Interface Tests
// =============================================================================

TEST_CASE("SampleHoldSource implements ModulationSource interface", "[processors][sample_hold]") {
    SampleHoldSource src;
    src.prepare(44100.0);

    // Default is Random input
    auto range = src.getSourceRange();
    REQUIRE(range.first == Approx(-1.0f));
    REQUIRE(range.second == Approx(1.0f));

    // Process and verify output is valid
    for (int i = 0; i < 1000; ++i) {
        src.process();
    }
    float val = src.getCurrentValue();
    REQUIRE(val >= -1.0f);
    REQUIRE(val <= 1.0f);
}

// Ruinae drives SampleHoldSource through processBlock(), once per host block.
// Its slew smoother used to advance one sample per call, so Slew ran 64 times
// slower at 64-sample blocks and 512 times slower at 512, and the depth of an
// S&H route followed the host's buffer size (Ruinae's "Quantum Field", S&H ->
// spectral tilt with 150 ms slew: 1.2 dB between 64- and 512-sample buffers).
// The slew must take the same audio time at every block size.
//
// The run is arranged so that every block size triggers at the same sample: at
// 65536 Hz and 1 Hz the phase step is 2^-16, every partial sum is exact, and the
// phase reaches 1.0 exactly at the end of sample 65536 for every block size that
// divides it. processBlock() then glides for the whole block as if the trigger
// fell at the block's first sample (control-rate quantisation, by design), so its
// value at the end of a block ending at T equals the per-sample value after
// T + B - 1 samples.
TEST_CASE("SampleHoldSource slew takes the same time at every block size",
          "[processors][sample_hold][block_size]") {
    constexpr double kRate = 65536.0;
    constexpr std::size_t kTrigger = 65536;
    constexpr std::size_t kTotal = 2 * 65536;
    const auto makeSource = [] {
        SampleHoldSource src;
        src.prepare(kRate);
        src.setInputType(SampleHoldInputType::Random);
        src.setRate(1.0f);
        src.setSlewTime(500.0f);  // tau = 100 ms = 6554 samples
        return src;
    };

    // Per-sample reference: perSample[n] is the value after n process() calls.
    std::vector<float> perSample(kTotal + 1, 0.0f);
    {
        auto src = makeSource();
        for (std::size_t n = 1; n <= kTotal; ++n) {
            src.process();
            perSample[n] = src.getCurrentValue();
        }
    }
    const float held = perSample[kTrigger + 60000];  // settled (9 tau)
    REQUIRE(std::abs(held) > 0.05f);  // not vacuous: the trigger moved the output

    for (std::size_t blockSize : {64u, 512u, 4096u}) {
        auto src = makeSource();
        std::size_t t = 0;
        float maxErr = 0.0f;
        std::size_t worst = 0;
        while (t < kTrigger + 8 * 4096) {
            src.processBlock(blockSize);
            t += blockSize;
            if (t < kTrigger) {
                continue;
            }
            const float err = std::abs(src.getCurrentValue() - perSample[t + blockSize - 1]);
            if (err > maxErr) { maxErr = err; worst = t; }
        }
        // The closed-form block advance and the iterated per-sample step differ
        // only by float rounding over ~4e4 steps (measured ~2.5e-6; 1e-4 is
        // -80 dB re the [-1, 1] range); a smoother that steps once per block
        // lags by most of the glide (~0.4).
        INFO("block size " << blockSize << ": max |block - per-sample| " << maxErr
             << " at sample " << worst << ", held value " << held);
        CHECK(maxErr <= 1e-4f);
    }
}
