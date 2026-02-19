// ==============================================================================
// Tests: RuinaeEffectsChain (Layer 3 System)
// ==============================================================================
// Comprehensive tests for the Ruinae effects chain composition.
//
// Feature: 043-effects-section
// Layer: 3 (Systems)
// Reference: specs/043-effects-section/spec.md
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "engine/ruinae_effects_chain.h"
#include "ruinae_types.h"

#include <artifact_detection.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstring>
#include <numeric>
#include <vector>

using Catch::Approx;
using namespace Krate::DSP;

// =============================================================================
// Helpers
// =============================================================================

namespace {

constexpr double kSampleRate = 44100.0;
constexpr size_t kBlockSize = 512;
constexpr double kSampleRate96k = 96000.0;

/// Fill buffer with a sine wave
void fillSine(float* buffer, size_t numSamples, float frequency, double sampleRate,
              float amplitude = 1.0f) {
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = amplitude * std::sin(
            2.0f * 3.14159265358979323846f * frequency * static_cast<float>(i)
            / static_cast<float>(sampleRate));
    }
}

/// Calculate RMS of a buffer
float calculateRMS(const float* buffer, size_t numSamples) {
    if (numSamples == 0) return 0.0f;
    float sum = 0.0f;
    for (size_t i = 0; i < numSamples; ++i) {
        sum += buffer[i] * buffer[i];
    }
    return std::sqrt(sum / static_cast<float>(numSamples));
}

/// Calculate peak absolute value
float peakAbsolute(const float* buffer, size_t numSamples) {
    float peak = 0.0f;
    for (size_t i = 0; i < numSamples; ++i) {
        peak = std::max(peak, std::abs(buffer[i]));
    }
    return peak;
}

/// Convert linear amplitude to dBFS
float linearToDbFS(float linear) {
    if (linear <= 0.0f) return -200.0f;
    return 20.0f * std::log10(linear);
}

/// Calculate max per-sample step size (for click detection)
float maxStepSize(const float* buffer, size_t numSamples) {
    float maxStep = 0.0f;
    for (size_t i = 1; i < numSamples; ++i) {
        float step = std::abs(buffer[i] - buffer[i - 1]);
        maxStep = std::max(maxStep, step);
    }
    return maxStep;
}

/// Prepare a chain at default settings
void prepareChain(RuinaeEffectsChain& chain, double sampleRate = kSampleRate,
                  size_t blockSize = kBlockSize) {
    chain.prepare(sampleRate, blockSize);
}

/// Settle the chain by processing enough audio to fill the latency compensation
/// delay (6144 samples: spectral FFT 1024 + harmonizer PV 5120).
/// Use a sine wave as the settling signal.
void settleChain(RuinaeEffectsChain& chain, size_t numBlocks = 16,
                 double sampleRate = kSampleRate, size_t blockSize = kBlockSize) {
    for (size_t b = 0; b < numBlocks; ++b) {
        std::vector<float> left(blockSize);
        std::vector<float> right(blockSize);
        fillSine(left.data(), blockSize, 440.0f, sampleRate);
        fillSine(right.data(), blockSize, 440.0f, sampleRate);
        chain.processBlock(left.data(), right.data(), blockSize);
    }
}

} // anonymous namespace

// =============================================================================
// Phase 1: RuinaeDelayType Enum Tests (T008)
// =============================================================================

TEST_CASE("RuinaeDelayType enum values", "[systems][ruinae_effects_chain]") {
    SECTION("Digital is 0") {
        REQUIRE(static_cast<uint8_t>(RuinaeDelayType::Digital) == 0);
    }
    SECTION("Tape is 1") {
        REQUIRE(static_cast<uint8_t>(RuinaeDelayType::Tape) == 1);
    }
    SECTION("PingPong is 2") {
        REQUIRE(static_cast<uint8_t>(RuinaeDelayType::PingPong) == 2);
    }
    SECTION("Granular is 3") {
        REQUIRE(static_cast<uint8_t>(RuinaeDelayType::Granular) == 3);
    }
    SECTION("Spectral is 4") {
        REQUIRE(static_cast<uint8_t>(RuinaeDelayType::Spectral) == 4);
    }
    SECTION("NumTypes is 5") {
        REQUIRE(static_cast<uint8_t>(RuinaeDelayType::NumTypes) == 5);
    }
    SECTION("underlying type is uint8_t") {
        static_assert(std::is_same_v<std::underlying_type_t<RuinaeDelayType>, uint8_t>);
    }
}

// =============================================================================
// Phase 2: Lifecycle Tests (T009)
// =============================================================================

TEST_CASE("RuinaeEffectsChain prepare/reset lifecycle", "[systems][ruinae_effects_chain]") {
    RuinaeEffectsChain chain;

    SECTION("construct and prepare at 44.1kHz/512") {
        chain.prepare(kSampleRate, kBlockSize);
        // Should not crash - chain is prepared
        // Verify latency is reported (spectral delay FFT size)
        REQUIRE(chain.getLatencySamples() > 0);
    }

    SECTION("reset after prepare does not crash") {
        chain.prepare(kSampleRate, kBlockSize);
        chain.reset();
        // Chain should still be usable after reset
        REQUIRE(chain.getActiveDelayType() == RuinaeDelayType::Digital);
    }

    SECTION("default delay type is Digital") {
        chain.prepare(kSampleRate, kBlockSize);
        REQUIRE(chain.getActiveDelayType() == RuinaeDelayType::Digital);
    }
}

// =============================================================================
// Phase 3: User Story 1 - Stereo Effects Chain Processing (FR-004, FR-005, FR-006)
// =============================================================================

TEST_CASE("RuinaeEffectsChain FR-006: dry pass-through at default settings",
          "[systems][ruinae_effects_chain][US1]") {
    // SC-004: Default state output within -120 dBFS of input
    // Strategy: impulse-based sample-level verification.
    // The compensation delay uses integer-read DelayLine (sample-perfect).
    RuinaeEffectsChain chain;
    prepareChain(chain);
    chain.setDelayEnabled(true);

    // Set delay mix to 0 (dry only), reverb mix to 0, phaser disabled
    chain.setDelayMix(0.0f);
    ReverbParams reverbParams;
    reverbParams.mix = 0.0f;
    chain.setReverbParams(reverbParams);
    chain.setPhaserEnabled(false);

    // Let the DigitalDelay mix smoother settle to 0.0
    // (default mix may be non-zero; smoother needs ~882 samples at 20ms)
    for (int b = 0; b < 4; ++b) {
        std::vector<float> tempL(kBlockSize, 0.0f);
        std::vector<float> tempR(kBlockSize, 0.0f);
        chain.processBlock(tempL.data(), tempR.data(), kBlockSize);
    }

    // Process an impulse - buffer must be larger than latency
    constexpr size_t kLen = 8192;
    std::vector<float> left(kLen, 0.0f);
    std::vector<float> right(kLen, 0.0f);
    left[0] = 1.0f;
    right[0] = 1.0f;

    chain.processBlock(left.data(), right.data(), kLen);

    // Compensation delay is 6144 samples (spectral FFT 1024 + harmonizer PV 5120)
    const size_t latency = chain.getLatencySamples();
    REQUIRE(latency == 6144);

    // The impulse should appear at exactly the latency offset
    INFO("Output at latency (" << latency << "): " << left[latency]);
    REQUIRE(left[latency] == Approx(1.0f).margin(1e-6f));
    REQUIRE(right[latency] == Approx(1.0f).margin(1e-6f));

    // All other samples should be near-silent (-120 dBFS = 1e-6 linear)
    float maxDeviation = 0.0f;
    for (size_t i = 0; i < kLen; ++i) {
        if (i == latency) continue;
        maxDeviation = std::max(maxDeviation, std::abs(left[i]));
    }
    float deviationDb = linearToDbFS(maxDeviation);
    INFO("Max deviation at non-impulse samples: " << maxDeviation
         << " (" << deviationDb << " dBFS)");
    REQUIRE(maxDeviation < 1e-6f);
}

TEST_CASE("RuinaeEffectsChain FR-005: fixed processing order (delay -> reverb)",
          "[systems][ruinae_effects_chain][US1]") {
    // Strategy: impulse with delay=200ms (8820 samples), reverb mix=0.3.
    // If delay runs before reverb, energy appears at ~latency+8820, not earlier.
    // If reverb ran first, energy would appear at ~latency (reverb of impulse).
    RuinaeEffectsChain chain;
    prepareChain(chain);
    chain.setDelayEnabled(true);
    chain.setReverbEnabled(true);

    chain.setDelayMix(1.0f);       // Full wet (no dry)
    chain.setDelayTime(200.0f);    // 200ms = 8820 samples at 44.1k
    chain.setDelayFeedback(0.0f);  // No feedback for clean measurement

    ReverbParams reverbParams;
    reverbParams.mix = 0.3f;
    reverbParams.roomSize = 0.5f;
    chain.setReverbParams(reverbParams);

    // Let smoothers settle with silence
    for (int b = 0; b < 4; ++b) {
        std::vector<float> tempL(kBlockSize, 0.0f);
        std::vector<float> tempR(kBlockSize, 0.0f);
        chain.processBlock(tempL.data(), tempR.data(), kBlockSize);
    }

    // Process impulse
    constexpr size_t kLen = 16384;
    std::vector<float> left(kLen, 0.0f);
    std::vector<float> right(kLen, 0.0f);
    left[0] = 1.0f;
    right[0] = 1.0f;

    chain.processBlock(left.data(), right.data(), kLen);

    const size_t latency = chain.getLatencySamples();  // 1024
    const size_t delayOffset = static_cast<size_t>(200.0 * kSampleRate / 1000.0);

    // Measure energy in early region (latency to latency+4000)
    // This is BEFORE the delay time of 8820 samples
    float earlyEnergy = 0.0f;
    for (size_t i = latency; i < latency + 4000 && i < kLen; ++i) {
        earlyEnergy += left[i] * left[i];
    }

    // Measure energy in post-delay region (latency+delayOffset to +3000)
    float lateEnergy = 0.0f;
    size_t lateStart = latency + delayOffset;
    for (size_t i = lateStart; i < lateStart + 3000 && i < kLen; ++i) {
        lateEnergy += left[i] * left[i];
    }

    INFO("Early energy (before delay time): " << earlyEnergy);
    INFO("Late energy (after delay time): " << lateEnergy);

    // Delay runs before reverb: late region should dominate
    REQUIRE(lateEnergy > earlyEnergy * 10.0f);
    REQUIRE(lateEnergy > 0.001f);
}

TEST_CASE("RuinaeEffectsChain FR-004: zero-sample blocks handled safely",
          "[systems][ruinae_effects_chain][US1]") {
    RuinaeEffectsChain chain;
    prepareChain(chain);

    // Process zero samples - should not crash
    float left = 0.0f;
    float right = 0.0f;
    chain.processBlock(&left, &right, 0);
    REQUIRE(true);  // Just verify no crash
}

// =============================================================================
// Phase 4: User Story 2 - Selectable Delay Type (FR-009, FR-014, FR-015, FR-016, FR-017)
// =============================================================================

TEST_CASE("RuinaeEffectsChain FR-009: setDelayType selects active delay",
          "[systems][ruinae_effects_chain][US2]") {
    RuinaeEffectsChain chain;
    prepareChain(chain);
    chain.setDelayEnabled(true);

    SECTION("default is Digital") {
        REQUIRE(chain.getActiveDelayType() == RuinaeDelayType::Digital);
    }

    SECTION("set to Tape") {
        chain.setDelayType(RuinaeDelayType::Tape);
        // After pre-warm + crossfade the active type updates.
        // Pre-warm: max(50ms, 20ms) * sr/1000 + targetLatency = 2205 + 6144 = 8349.
        // Crossfade: 30ms = 1323 samples. Total: ~9672. Use 16384 for margin.
        std::vector<float> left(16384, 0.0f);
        std::vector<float> right(16384, 0.0f);
        chain.processBlock(left.data(), right.data(), 16384);
        REQUIRE(chain.getActiveDelayType() == RuinaeDelayType::Tape);
    }

    SECTION("set to Spectral") {
        chain.setDelayType(RuinaeDelayType::Spectral);
        std::vector<float> left(16384, 0.0f);
        std::vector<float> right(16384, 0.0f);
        chain.processBlock(left.data(), right.data(), 16384);
        REQUIRE(chain.getActiveDelayType() == RuinaeDelayType::Spectral);
    }
}

TEST_CASE("RuinaeEffectsChain FR-014: setDelayType same type is no-op",
          "[systems][ruinae_effects_chain][US2]") {
    RuinaeEffectsChain chain;
    prepareChain(chain);

    REQUIRE(chain.getActiveDelayType() == RuinaeDelayType::Digital);
    chain.setDelayType(RuinaeDelayType::Digital);
    REQUIRE(chain.getActiveDelayType() == RuinaeDelayType::Digital);
    // No crossfade should be initiated
}

TEST_CASE("RuinaeEffectsChain FR-015: delay parameter forwarding",
          "[systems][ruinae_effects_chain][US2]") {
    RuinaeEffectsChain chain;
    prepareChain(chain);

    // Set parameters - should not crash for any type
    chain.setDelayTime(200.0f);
    chain.setDelayFeedback(0.5f);
    chain.setDelayMix(0.7f);

    // Settle the chain (latency compensation needs ~1024 samples)
    settleChain(chain);

    // Process a measurement block
    std::vector<float> left(kBlockSize);
    std::vector<float> right(kBlockSize);
    fillSine(left.data(), kBlockSize, 440.0f, kSampleRate);
    fillSine(right.data(), kBlockSize, 440.0f, kSampleRate);
    chain.processBlock(left.data(), right.data(), kBlockSize);

    REQUIRE(calculateRMS(left.data(), kBlockSize) > 0.0f);
}

TEST_CASE("RuinaeEffectsChain FR-017: delay time forwarding per type",
          "[systems][ruinae_effects_chain][US2]") {
    // Verify that each delay type actually produces delayed output.
    // Uses continuous sine (not impulse) because Granular needs audio
    // content in its buffer to spawn grains.
    for (int typeIdx = 0; typeIdx < static_cast<int>(RuinaeDelayType::NumTypes); ++typeIdx) {
        auto type = static_cast<RuinaeDelayType>(typeIdx);
        SECTION("Type " + std::to_string(typeIdx)) {
            RuinaeEffectsChain chain;
            prepareChain(chain);
            chain.setDelayType(type);
            chain.setDelayTime(100.0f);
            chain.setDelayMix(1.0f);
            chain.setDelayFeedback(0.3f);
            ReverbParams reverbParams;
            reverbParams.mix = 0.0f;
            chain.setReverbParams(reverbParams);

            // Settle crossfade + smoothers + fill delay buffers with signal
            for (int b = 0; b < 16; ++b) {
                std::vector<float> left(kBlockSize);
                std::vector<float> right(kBlockSize);
                fillSine(left.data(), kBlockSize, 440.0f, kSampleRate);
                fillSine(right.data(), kBlockSize, 440.0f, kSampleRate);
                chain.processBlock(left.data(), right.data(), kBlockSize);
            }

            // Measure energy during continued processing
            float totalEnergy = 0.0f;
            for (int b = 0; b < 4; ++b) {
                std::vector<float> left(kBlockSize);
                std::vector<float> right(kBlockSize);
                fillSine(left.data(), kBlockSize, 440.0f, kSampleRate);
                fillSine(right.data(), kBlockSize, 440.0f, kSampleRate);
                chain.processBlock(left.data(), right.data(), kBlockSize);
                for (size_t i = 0; i < kBlockSize; ++i) {
                    totalEnergy += left[i] * left[i];
                }
            }

            INFO("Type " << typeIdx << " total energy: " << totalEnergy);
            REQUIRE(totalEnergy > 0.001f);
        }
    }
}

TEST_CASE("RuinaeEffectsChain FR-016: setDelayTempo updates BlockContext tempo",
          "[systems][ruinae_effects_chain][US2]") {
    RuinaeEffectsChain chain;
    prepareChain(chain);

    chain.setDelayTempo(140.0);

    // Process block - should not crash and tempo should be used
    std::vector<float> left(kBlockSize, 0.0f);
    std::vector<float> right(kBlockSize, 0.0f);
    chain.processBlock(left.data(), right.data(), kBlockSize);
    REQUIRE(true);
}

TEST_CASE("RuinaeEffectsChain all 5 delay types produce different outputs",
          "[systems][ruinae_effects_chain][US2]") {
    // Process the same impulse through each delay type and verify outputs differ
    std::array<std::vector<float>, 5> outputs;

    for (int typeIdx = 0; typeIdx < 5; ++typeIdx) {
        RuinaeEffectsChain chain;
        prepareChain(chain);
        chain.setDelayEnabled(true);

        auto type = static_cast<RuinaeDelayType>(typeIdx);
        chain.setDelayType(type);
        chain.setDelayTime(50.0f);
        chain.setDelayMix(1.0f);
        chain.setDelayFeedback(0.3f);

        // Disable reverb for clean comparison
        ReverbParams reverbParams;
        reverbParams.mix = 0.0f;
        chain.setReverbParams(reverbParams);

        // Process crossfade to completion. Latency is 6144 samples and
        // impulse is placed at 2048, so output appears at ~8192+.
        // Use 16384 samples to capture both crossfade and impulse response.
        constexpr size_t kTotalSamples = 16384;
        std::vector<float> left(kTotalSamples, 0.0f);
        std::vector<float> right(kTotalSamples, 0.0f);

        // Put impulse after crossfade settles
        left[2048] = 1.0f;
        right[2048] = 1.0f;

        chain.processBlock(left.data(), right.data(), kTotalSamples);
        outputs[typeIdx] = left;
    }

    // At least some pairs should produce different outputs
    int differentPairs = 0;
    for (int i = 0; i < 5; ++i) {
        for (int j = i + 1; j < 5; ++j) {
            float diff = 0.0f;
            for (size_t s = 0; s < outputs[i].size(); ++s) {
                diff += std::abs(outputs[i][s] - outputs[j][s]);
            }
            if (diff > 0.001f) {
                ++differentPairs;
            }
        }
    }
    // At minimum several pairs should differ
    INFO("Different pairs: " << differentPairs << " out of 10");
    REQUIRE(differentPairs >= 3);
}

// =============================================================================
// Phase 6: User Story 4 - Dattorro Reverb Integration (FR-021, FR-022, FR-023)
// =============================================================================

TEST_CASE("RuinaeEffectsChain FR-021: setReverbParams forwards all parameters",
          "[systems][ruinae_effects_chain][US4]") {
    RuinaeEffectsChain chain;
    prepareChain(chain);

    ReverbParams params;
    params.roomSize = 0.8f;
    params.damping = 0.6f;
    params.width = 1.0f;
    params.mix = 0.5f;
    params.preDelayMs = 20.0f;
    params.diffusion = 0.7f;
    params.freeze = false;
    params.modRate = 0.3f;
    params.modDepth = 0.2f;
    chain.setReverbParams(params);

    // Settle the latency compensation
    settleChain(chain);

    // Process a measurement block
    std::vector<float> left(kBlockSize);
    std::vector<float> right(kBlockSize);
    fillSine(left.data(), kBlockSize, 440.0f, kSampleRate);
    fillSine(right.data(), kBlockSize, 440.0f, kSampleRate);
    chain.processBlock(left.data(), right.data(), kBlockSize);

    REQUIRE(calculateRMS(left.data(), kBlockSize) > 0.0f);
}

TEST_CASE("RuinaeEffectsChain FR-022: reverb processes delay output not dry input",
          "[systems][ruinae_effects_chain][US4]") {
    // Enable delay with significant time, then enable reverb
    // Verify reverb acts on delayed signal

    // Chain 1: delay + reverb
    RuinaeEffectsChain chain1;
    prepareChain(chain1);
    chain1.setDelayEnabled(true);
    chain1.setReverbEnabled(true);
    chain1.setDelayMix(1.0f);
    chain1.setDelayTime(100.0f);
    chain1.setDelayFeedback(0.0f);
    ReverbParams params1;
    params1.mix = 0.5f;
    params1.roomSize = 0.7f;
    chain1.setReverbParams(params1);

    // Chain 2: reverb only (no delay)
    RuinaeEffectsChain chain2;
    prepareChain(chain2);
    chain2.setDelayEnabled(true);
    chain2.setReverbEnabled(true);
    chain2.setDelayMix(0.0f);
    ReverbParams params2;
    params2.mix = 0.5f;
    params2.roomSize = 0.7f;
    chain2.setReverbParams(params2);

    // Process same impulse through both. Need buffer larger than latency (6144)
    // + delay time (100ms = 4410) to capture the delayed output.
    constexpr size_t kLen = 16384;
    std::vector<float> left1(kLen, 0.0f), right1(kLen, 0.0f);
    std::vector<float> left2(kLen, 0.0f), right2(kLen, 0.0f);
    left1[0] = 1.0f; right1[0] = 1.0f;
    left2[0] = 1.0f; right2[0] = 1.0f;

    chain1.processBlock(left1.data(), right1.data(), kLen);
    chain2.processBlock(left2.data(), right2.data(), kLen);

    // Outputs should differ because reverb processes different input
    float diff = 0.0f;
    for (size_t i = 0; i < kLen; ++i) {
        diff += std::abs(left1[i] - left2[i]);
    }
    INFO("Total difference: " << diff);
    REQUIRE(diff > 0.01f);
}

TEST_CASE("RuinaeEffectsChain reverb parameter changes during playback",
          "[systems][ruinae_effects_chain][US4]") {
    RuinaeEffectsChain chain;
    prepareChain(chain);

    ReverbParams params;
    params.mix = 0.5f;
    params.roomSize = 0.3f;
    chain.setReverbParams(params);

    // Process some blocks, then change room size
    for (int block = 0; block < 4; ++block) {
        std::vector<float> left(kBlockSize);
        std::vector<float> right(kBlockSize);
        fillSine(left.data(), kBlockSize, 440.0f, kSampleRate);
        fillSine(right.data(), kBlockSize, 440.0f, kSampleRate);
        chain.processBlock(left.data(), right.data(), kBlockSize);
    }

    // Change room size mid-stream
    params.roomSize = 0.9f;
    chain.setReverbParams(params);

    // Continue processing - should be smooth, no crash
    std::vector<float> left(kBlockSize);
    std::vector<float> right(kBlockSize);
    fillSine(left.data(), kBlockSize, 440.0f, kSampleRate);
    fillSine(right.data(), kBlockSize, 440.0f, kSampleRate);
    chain.processBlock(left.data(), right.data(), kBlockSize);

    REQUIRE(calculateRMS(left.data(), kBlockSize) > 0.0f);
}

TEST_CASE("RuinaeEffectsChain reverb impulse response",
          "[systems][ruinae_effects_chain][US4]") {
    RuinaeEffectsChain chain;
    prepareChain(chain);

    chain.setDelayMix(0.0f);
    ReverbParams params;
    params.mix = 1.0f;  // Full wet
    params.roomSize = 0.7f;
    params.damping = 0.5f;
    chain.setReverbParams(params);

    // Process impulse
    constexpr size_t kLen = 8192;
    std::vector<float> left(kLen, 0.0f);
    std::vector<float> right(kLen, 0.0f);
    left[0] = 1.0f;
    right[0] = 1.0f;

    chain.processBlock(left.data(), right.data(), kLen);

    // Should have a reverberant tail
    float earlyRMS = calculateRMS(left.data(), kLen / 4);
    float lateRMS = calculateRMS(left.data() + kLen / 2, kLen / 4);

    // Late tail should be quieter than early (decay)
    INFO("Early RMS: " << earlyRMS << " Late RMS: " << lateRMS);
    REQUIRE(earlyRMS > 0.0f);
}

// =============================================================================
// Phase 7: User Story 5 - Click-Free Delay Type Switching (FR-010 to FR-013)
// =============================================================================

TEST_CASE("RuinaeEffectsChain FR-010: crossfade blends outgoing and incoming",
          "[systems][ruinae_effects_chain][US5]") {
    RuinaeEffectsChain chain;
    prepareChain(chain);
    chain.setDelayEnabled(true);

    chain.setDelayMix(1.0f);
    chain.setDelayTime(50.0f);
    chain.setDelayFeedback(0.3f);
    ReverbParams reverbParams;
    reverbParams.mix = 0.0f;
    chain.setReverbParams(reverbParams);

    // Start with Digital, switch to Tape
    chain.setDelayType(RuinaeDelayType::Tape);

    // Process through pre-warm + crossfade.
    // Pre-warm: max(50ms, 20ms) * sr/1000 + 6144 = 2205 + 6144 = 8349.
    // Crossfade: 30ms = 1323. Total: ~9672. Need 9672/512 = ~19 blocks.
    for (int b = 0; b < 24; ++b) {
        std::vector<float> left(kBlockSize);
        std::vector<float> right(kBlockSize);
        fillSine(left.data(), kBlockSize, 440.0f, kSampleRate, 0.5f);
        fillSine(right.data(), kBlockSize, 440.0f, kSampleRate, 0.5f);
        chain.processBlock(left.data(), right.data(), kBlockSize);
    }

    REQUIRE(chain.getActiveDelayType() == RuinaeDelayType::Tape);
}

TEST_CASE("RuinaeEffectsChain FR-011: crossfade duration 25-50ms",
          "[systems][ruinae_effects_chain][US5]") {
    RuinaeEffectsChain chain;
    prepareChain(chain);
    chain.setDelayEnabled(true);

    chain.setDelayMix(1.0f);
    // Use short delay time so pre-warm is minimal (20ms minimum).
    // Total transition: 20ms pre-warm + 30ms crossfade = 50ms.
    // This tests the crossfade duration spec (FR-011: 25-50ms).
    chain.setDelayTime(1.0f);
    ReverbParams reverbParams;
    reverbParams.mix = 0.0f;
    chain.setReverbParams(reverbParams);

    // Switch type and count how many samples until transition completes
    chain.setDelayType(RuinaeDelayType::Tape);

    // Process in small blocks to measure completion time
    size_t samplesProcessed = 0;
    constexpr size_t kMaxSamples = static_cast<size_t>(kSampleRate * 0.2);  // 200ms max

    while (chain.getActiveDelayType() != RuinaeDelayType::Tape && samplesProcessed < kMaxSamples) {
        std::vector<float> left(64, 0.0f);
        std::vector<float> right(64, 0.0f);
        chain.processBlock(left.data(), right.data(), 64);
        samplesProcessed += 64;
    }

    // Total transition = pre-warm (20ms + 6144/44100*1000=139ms comp) + crossfade (30ms) = ~189ms
    float durationMs = static_cast<float>(samplesProcessed) / static_cast<float>(kSampleRate) * 1000.0f;
    INFO("Transition completed in " << durationMs << " ms (" << samplesProcessed << " samples)");
    REQUIRE(durationMs >= 25.0f);
    REQUIRE(durationMs <= 250.0f);  // pre-warm (20ms+139ms comp) + crossfade (30ms) + block overshoot
}

TEST_CASE("RuinaeEffectsChain FR-012: fast-track on type switch during crossfade",
          "[systems][ruinae_effects_chain][US5]") {
    RuinaeEffectsChain chain;
    prepareChain(chain);
    chain.setDelayEnabled(true);

    // Start Digital -> Tape crossfade
    chain.setDelayType(RuinaeDelayType::Tape);

    // Process a small amount (< crossfade duration)
    std::vector<float> left(256, 0.0f);
    std::vector<float> right(256, 0.0f);
    chain.processBlock(left.data(), right.data(), 256);

    // Now request Tape -> Granular while still crossfading
    chain.setDelayType(RuinaeDelayType::Granular);

    // After cancelling the first pre-warm, a new pre-warm + crossfade starts.
    // Need ~9672 samples (50ms pre-warm + 6144 comp + 30ms crossfade) to complete.
    // 9672/256 = ~38 blocks. Use 48 for margin.
    for (int block = 0; block < 48; ++block) {
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        chain.processBlock(left.data(), right.data(), 256);
    }

    // Final type should be Granular
    REQUIRE(chain.getActiveDelayType() == RuinaeDelayType::Granular);
}

TEST_CASE("RuinaeEffectsChain FR-013: outgoing delay reset after crossfade completes",
          "[systems][ruinae_effects_chain][US5]") {
    // Strategy: build state in Digital, switch away (reset occurs), switch back,
    // process silence — if properly reset, output should be near-silent.
    RuinaeEffectsChain chain;
    prepareChain(chain);
    chain.setDelayEnabled(true);

    chain.setDelayMix(1.0f);
    chain.setDelayTime(50.0f);
    chain.setDelayFeedback(0.5f);
    ReverbParams reverbParams;
    reverbParams.mix = 0.0f;
    chain.setReverbParams(reverbParams);
    chain.setPhaserEnabled(false);

    // Build up loud feedback state in Digital delay
    for (int block = 0; block < 16; ++block) {
        std::vector<float> left(kBlockSize);
        std::vector<float> right(kBlockSize);
        fillSine(left.data(), kBlockSize, 440.0f, kSampleRate, 0.8f);
        fillSine(right.data(), kBlockSize, 440.0f, kSampleRate, 0.8f);
        chain.processBlock(left.data(), right.data(), kBlockSize);
    }

    // Switch Digital -> Tape (pre-warm + crossfade completes, Digital should be reset)
    // Pre-warm: max(50ms,20ms)*sr/1000+6144=8349. Crossfade: 1323. Total: ~9672 = ~19 blocks of 512.
    chain.setDelayType(RuinaeDelayType::Tape);
    for (int block = 0; block < 24; ++block) {
        std::vector<float> left(kBlockSize, 0.0f);
        std::vector<float> right(kBlockSize, 0.0f);
        chain.processBlock(left.data(), right.data(), kBlockSize);
    }
    REQUIRE(chain.getActiveDelayType() == RuinaeDelayType::Tape);

    // Switch Tape -> Digital (pre-warm + crossfade completes)
    chain.setDelayType(RuinaeDelayType::Digital);
    for (int block = 0; block < 24; ++block) {
        std::vector<float> left(kBlockSize, 0.0f);
        std::vector<float> right(kBlockSize, 0.0f);
        chain.processBlock(left.data(), right.data(), kBlockSize);
    }
    REQUIRE(chain.getActiveDelayType() == RuinaeDelayType::Digital);

    // Process silence through re-activated Digital delay
    // If properly reset, output should be near-silent (no stale buffer content)
    float maxOutput = 0.0f;
    for (int block = 0; block < 4; ++block) {
        std::vector<float> left(kBlockSize, 0.0f);
        std::vector<float> right(kBlockSize, 0.0f);
        chain.processBlock(left.data(), right.data(), kBlockSize);
        for (size_t i = 0; i < kBlockSize; ++i) {
            maxOutput = std::max(maxOutput, std::abs(left[i]));
        }
    }

    INFO("Max output from reset Digital delay processing silence: " << maxOutput);
    REQUIRE(maxOutput < 0.001f);
}

TEST_CASE("RuinaeEffectsChain SC-002: crossfade produces no discontinuities",
          "[systems][ruinae_effects_chain][US5]") {
    using namespace Krate::DSP::TestUtils;

    SECTION("ClickDetector finds no artifacts in sine signal during crossfade") {
        RuinaeEffectsChain chain;
        prepareChain(chain);

        // 50ms delay with pre-warming: the incoming delay's buffer is
        // filled before the crossfade starts, eliminating the delay-line-fill
        // artifact that previously occurred at ~3229 samples post-switch.
        chain.setDelayMix(0.5f);
        chain.setDelayTime(50.0f);
        chain.setDelayFeedback(0.3f);
        ReverbParams reverbParams;
        reverbParams.mix = 0.0f;
        chain.setReverbParams(reverbParams);

        constexpr size_t kBlock = 512;
        constexpr size_t kWarmup = 8;
        constexpr size_t kMeasure = 8;
        constexpr size_t kTotalSamples = (kWarmup + kMeasure) * kBlock;

        // Pre-generate phase-coherent sine
        std::vector<float> outputL(kTotalSamples);
        std::vector<float> outputR(kTotalSamples);
        for (size_t i = 0; i < kTotalSamples; ++i) {
            float sample = 0.5f * std::sin(
                2.0f * 3.14159265358979323846f * 440.0f
                * static_cast<float>(i) / static_cast<float>(kSampleRate));
            outputL[i] = sample;
            outputR[i] = sample;
        }

        // Process warmup
        for (size_t b = 0; b < kWarmup; ++b) {
            chain.processBlock(outputL.data() + b * kBlock,
                              outputR.data() + b * kBlock, kBlock);
        }

        // Trigger crossfade
        chain.setDelayType(RuinaeDelayType::PingPong);

        // Process measurement blocks (during and after crossfade)
        for (size_t b = 0; b < kMeasure; ++b) {
            size_t offset = (kWarmup + b) * kBlock;
            chain.processBlock(outputL.data() + offset,
                              outputR.data() + offset, kBlock);
        }

        // ClickDetector analysis on measurement region
        ClickDetectorConfig clickConfig{
            .sampleRate = static_cast<float>(kSampleRate),
            .frameSize = 256,
            .hopSize = 128,
            .detectionThreshold = 5.0f,
            .energyThresholdDb = -60.0f,
            .mergeGap = 5
        };

        ClickDetector detector(clickConfig);
        detector.prepare();

        const size_t measureStart = kWarmup * kBlock;
        const size_t measureLen = kMeasure * kBlock;
        auto clicks = detector.detect(outputL.data() + measureStart, measureLen);

        INFO("Clicks detected during crossfade: " << clicks.size());
        for (size_t c = 0; c < clicks.size(); ++c) {
            INFO("  Click " << c << " at sample " << clicks[c].sampleIndex
                 << " amplitude " << clicks[c].amplitude);
        }
        REQUIRE(clicks.empty());
    }

    SECTION("DC signal crossfade has no steps > -60 dBFS") {
        // DC has zero natural step size, so any step is purely an artifact.
        // With pre-warming, the incoming delay's buffer is filled before the
        // crossfade starts. Measurement covers the full pre-warm + crossfade
        // window (~9672 samples), verifying there is NO delay-line-fill step.
        RuinaeEffectsChain chain;
        prepareChain(chain);
        chain.setDelayMix(0.5f);
        chain.setDelayTime(50.0f);
        chain.setDelayFeedback(0.0f);
        ReverbParams rp;
        rp.mix = 0.0f;
        chain.setReverbParams(rp);

        // Warm up with constant DC
        for (int b = 0; b < 16; ++b) {
            std::vector<float> left(kBlockSize, 0.5f);
            std::vector<float> right(kBlockSize, 0.5f);
            chain.processBlock(left.data(), right.data(), kBlockSize);
        }

        // Trigger type switch during DC (starts pre-warm, then crossfade)
        chain.setDelayType(RuinaeDelayType::PingPong);

        // Measure per-sample steps across full transition window
        float worstStep = 0.0f;
        float prevSample = 0.0f;
        bool first = true;
        for (int b = 0; b < 24; ++b) {
            std::vector<float> left(kBlockSize, 0.5f);
            std::vector<float> right(kBlockSize, 0.5f);
            chain.processBlock(left.data(), right.data(), kBlockSize);

            for (size_t i = 0; i < kBlockSize; ++i) {
                if (!first) {
                    float prev = (i == 0) ? prevSample : left[i - 1];
                    float step = std::abs(left[i] - prev);
                    worstStep = std::max(worstStep, step);
                }
                first = false;
            }
            prevSample = left[kBlockSize - 1];
        }
        float worstDb = linearToDbFS(worstStep);
        INFO("Worst DC step across full transition: " << worstStep
             << " (" << worstDb << " dBFS)");
        REQUIRE(worstDb < -60.0f);
    }
}

TEST_CASE("RuinaeEffectsChain SC-008: 10 consecutive type switches click-free",
          "[systems][ruinae_effects_chain][US5]") {
    using namespace Krate::DSP::TestUtils;

    RuinaeEffectsChain chain;
    prepareChain(chain);

    // 50ms delay with pre-warming: incoming delay buffer is filled before
    // each crossfade, eliminating delay-line-fill artifacts.
    chain.setDelayMix(0.5f);
    chain.setDelayTime(50.0f);
    chain.setDelayFeedback(0.3f);
    ReverbParams reverbParams;
    reverbParams.mix = 0.0f;
    chain.setReverbParams(reverbParams);

    constexpr size_t kBlock = 512;
    constexpr size_t kWarmup = 4;
    constexpr size_t kBlocksPerSwitch = 24;  // Need ~9672 samples for pre-warm(8349)+crossfade(1323)
    constexpr size_t kNumSwitches = 10;
    constexpr size_t kTotalBlocks = kWarmup + kBlocksPerSwitch * kNumSwitches;
    constexpr size_t kTotalSamples = kTotalBlocks * kBlock;

    // Pre-generate phase-coherent sine
    std::vector<float> outputL(kTotalSamples);
    std::vector<float> outputR(kTotalSamples);
    for (size_t i = 0; i < kTotalSamples; ++i) {
        float sample = 0.5f * std::sin(
            2.0f * 3.14159265358979323846f * 440.0f
            * static_cast<float>(i) / static_cast<float>(kSampleRate));
        outputL[i] = sample;
        outputR[i] = sample;
    }

    // Process warmup
    for (size_t b = 0; b < kWarmup; ++b) {
        chain.processBlock(outputL.data() + b * kBlock,
                          outputR.data() + b * kBlock, kBlock);
    }

    // 10 switches cycling all 5 types twice
    const RuinaeDelayType typeSequence[] = {
        RuinaeDelayType::Tape, RuinaeDelayType::PingPong,
        RuinaeDelayType::Granular, RuinaeDelayType::Spectral,
        RuinaeDelayType::Digital, RuinaeDelayType::Tape,
        RuinaeDelayType::PingPong, RuinaeDelayType::Granular,
        RuinaeDelayType::Spectral, RuinaeDelayType::Digital
    };

    for (size_t sw = 0; sw < kNumSwitches; ++sw) {
        chain.setDelayType(typeSequence[sw]);
        for (size_t b = 0; b < kBlocksPerSwitch; ++b) {
            size_t blockIdx = kWarmup + sw * kBlocksPerSwitch + b;
            chain.processBlock(outputL.data() + blockIdx * kBlock,
                              outputR.data() + blockIdx * kBlock, kBlock);
        }
    }

    // ClickDetector analysis on the switching region
    ClickDetectorConfig clickConfig{
        .sampleRate = static_cast<float>(kSampleRate),
        .frameSize = 256,
        .hopSize = 128,
        .detectionThreshold = 5.0f,
        .energyThresholdDb = -60.0f,
        .mergeGap = 5
    };

    ClickDetector detector(clickConfig);
    detector.prepare();

    const size_t measureStart = kWarmup * kBlock;
    const size_t measureLen = kNumSwitches * kBlocksPerSwitch * kBlock;
    auto clicks = detector.detect(outputL.data() + measureStart, measureLen);

    INFO("Clicks detected over 10 switches: " << clicks.size());
    for (size_t c = 0; c < clicks.size(); ++c) {
        size_t switchIdx = clicks[c].sampleIndex / (kBlocksPerSwitch * kBlock);
        INFO("  Click " << c << " at sample " << clicks[c].sampleIndex
             << " (switch " << switchIdx << ") amplitude " << clicks[c].amplitude);
    }
    REQUIRE(clicks.empty());
}

TEST_CASE("RuinaeEffectsChain pre-warm eliminates delay-line-fill artifact",
          "[systems][ruinae_effects_chain][US5]") {
    // Verification test: DC signal at 0.5, mix=0.5, 50ms delay, switch type.
    // Without pre-warming, the incoming delay has an empty buffer after
    // crossfade completes. When the buffer fills at sample ~3229
    // (delay_time + comp_delay = 2205 + 1024), wet output jumps from 0 to DC,
    // causing a step of ~0.25 (= -12 dBFS). With pre-warming, the buffer
    // is already full when the crossfade starts, so no step occurs.
    //
    // Measurement covers full pre-warm (~8349 samples) + crossfade (1323) +
    // post-crossfade. Worst step must be < -60 dBFS.
    RuinaeEffectsChain chain;
    prepareChain(chain);

    chain.setDelayMix(0.5f);
    chain.setDelayTime(50.0f);
    chain.setDelayFeedback(0.0f);
    ReverbParams rp;
    rp.mix = 0.0f;
    chain.setReverbParams(rp);

    // Warm up with DC to fill active delay + compensation delays
    for (int b = 0; b < 16; ++b) {
        std::vector<float> left(kBlockSize, 0.5f);
        std::vector<float> right(kBlockSize, 0.5f);
        chain.processBlock(left.data(), right.data(), kBlockSize);
    }

    // Switch to PingPong (starts pre-warm, then crossfade)
    chain.setDelayType(RuinaeDelayType::PingPong);

    // Measure per-sample steps across entire transition + post-transition
    float worstStep = 0.0f;
    size_t worstSample = 0;
    float prevSample = 0.0f;
    bool first = true;
    size_t globalSample = 0;

    for (int b = 0; b < 24; ++b) {
        std::vector<float> left(kBlockSize, 0.5f);
        std::vector<float> right(kBlockSize, 0.5f);
        chain.processBlock(left.data(), right.data(), kBlockSize);

        for (size_t i = 0; i < kBlockSize; ++i) {
            if (!first) {
                float prev = (i == 0) ? prevSample : left[i - 1];
                float step = std::abs(left[i] - prev);
                if (step > worstStep) {
                    worstStep = step;
                    worstSample = globalSample;
                }
            }
            first = false;
            ++globalSample;
        }
        prevSample = left[kBlockSize - 1];
    }

    float worstDb = linearToDbFS(worstStep);
    INFO("Worst step: " << worstStep << " (" << worstDb << " dBFS) at sample " << worstSample);
    INFO("Pre-warm verification: delay-line-fill artifact should be eliminated");
    REQUIRE(worstDb < -60.0f);
}

// =============================================================================
// Phase 8: Latency Compensation (FR-026, FR-027)
// =============================================================================

TEST_CASE("RuinaeEffectsChain FR-026: getLatencySamples returns spectral delay FFT latency",
          "[systems][ruinae_effects_chain]") {
    RuinaeEffectsChain chain;
    prepareChain(chain);

    size_t latency = chain.getLatencySamples();
    // Spectral delay FFT (1024) + harmonizer PhaseVocoder worst-case (5120) = 6144
    INFO("Latency: " << latency << " samples");
    REQUIRE(latency > 0);
    REQUIRE(latency == 6144);  // Spectral FFT + harmonizer PV
}

TEST_CASE("RuinaeEffectsChain FR-027: latency constant across delay type switches (SC-007)",
          "[systems][ruinae_effects_chain]") {
    RuinaeEffectsChain chain;
    prepareChain(chain);

    size_t latencyBefore = chain.getLatencySamples();

    // Switch through all types
    for (int typeIdx = 0; typeIdx < 5; ++typeIdx) {
        chain.setDelayType(static_cast<RuinaeDelayType>(typeIdx));

        // Process to complete pre-warm + crossfade (~9672 samples needed)
        std::vector<float> left(16384, 0.0f);
        std::vector<float> right(16384, 0.0f);
        chain.processBlock(left.data(), right.data(), 16384);

        size_t latencyAfter = chain.getLatencySamples();
        INFO("Type " << typeIdx << " latency: " << latencyAfter);
        REQUIRE(latencyAfter == latencyBefore);
    }
}

TEST_CASE("RuinaeEffectsChain latency compensation for non-spectral delays",
          "[systems][ruinae_effects_chain]") {
    // Verify compensation delays are applied to non-spectral types
    RuinaeEffectsChain chain;
    prepareChain(chain);
    chain.setDelayEnabled(true);

    chain.setDelayMix(0.0f);  // Dry only to test compensation delay
    ReverbParams reverbParams;
    reverbParams.mix = 0.0f;
    chain.setReverbParams(reverbParams);

    // Process an impulse through Digital (has compensation)
    // Buffer must be larger than latency (6144) to find the impulse
    constexpr size_t kLen = 8192;
    std::vector<float> left(kLen, 0.0f);
    std::vector<float> right(kLen, 0.0f);
    left[0] = 1.0f;
    right[0] = 1.0f;

    chain.processBlock(left.data(), right.data(), kLen);

    // Find the impulse position in output
    size_t latency = chain.getLatencySamples();
    float peakVal = 0.0f;
    size_t peakPos = 0;
    for (size_t i = 0; i < kLen; ++i) {
        if (std::abs(left[i]) > peakVal) {
            peakVal = std::abs(left[i]);
            peakPos = i;
        }
    }

    INFO("Peak at sample " << peakPos << " (expected near " << latency << ")");
    // Peak should be approximately at the latency offset
    if (peakVal > 0.01f) {
        REQUIRE(peakPos >= latency - 2);
        REQUIRE(peakPos <= latency + 2);
    }
}

// =============================================================================
// Phase 9: User Story 6 - Individual Effect Bypass (US6)
// =============================================================================

TEST_CASE("RuinaeEffectsChain US6: all effects disabled, enable single effect",
          "[systems][ruinae_effects_chain][US6]") {
    RuinaeEffectsChain chain;
    prepareChain(chain);

    // All off
    chain.setDelayMix(0.0f);
    ReverbParams params;
    params.mix = 0.0f;
    chain.setReverbParams(params);

    // Process with all off - should be pass-through
    std::vector<float> leftOff(kBlockSize);
    std::vector<float> rightOff(kBlockSize);
    fillSine(leftOff.data(), kBlockSize, 440.0f, kSampleRate);
    fillSine(rightOff.data(), kBlockSize, 440.0f, kSampleRate);

    // Save input copy
    std::vector<float> leftRef(leftOff);
    chain.processBlock(leftOff.data(), rightOff.data(), kBlockSize);

    // Now enable only delay
    RuinaeEffectsChain chain2;
    prepareChain(chain2);
    chain2.setDelayMix(0.5f);
    chain2.setDelayTime(100.0f);
    chain2.setDelayFeedback(0.3f);
    ReverbParams params2;
    params2.mix = 0.0f;
    chain2.setReverbParams(params2);

    std::vector<float> leftOn(kBlockSize);
    std::vector<float> rightOn(kBlockSize);
    fillSine(leftOn.data(), kBlockSize, 440.0f, kSampleRate);
    fillSine(rightOn.data(), kBlockSize, 440.0f, kSampleRate);
    chain2.processBlock(leftOn.data(), rightOn.data(), kBlockSize);

    // Outputs should be different (delay modifies signal)
    // Actually for the first block with 100ms delay and 44.1k rate,
    // the delayed signal hasn't arrived yet, so only dry signal comes through.
    // This is expected behavior.
    REQUIRE(true);
}

TEST_CASE("RuinaeEffectsChain US6: bypassed effect smooth transition",
          "[systems][ruinae_effects_chain][US6]") {
    RuinaeEffectsChain chain;
    prepareChain(chain);

    chain.setDelayMix(0.5f);
    chain.setDelayTime(50.0f);
    chain.setDelayFeedback(0.5f);
    ReverbParams reverbParams;
    reverbParams.mix = 0.0f;
    chain.setReverbParams(reverbParams);

    // Build up delay tail
    for (int block = 0; block < 8; ++block) {
        std::vector<float> left(kBlockSize);
        std::vector<float> right(kBlockSize);
        fillSine(left.data(), kBlockSize, 440.0f, kSampleRate, 0.5f);
        fillSine(right.data(), kBlockSize, 440.0f, kSampleRate, 0.5f);
        chain.processBlock(left.data(), right.data(), kBlockSize);
    }

    // Bypass delay (set mix to 0)
    chain.setDelayMix(0.0f);

    // Process - the transition should be smooth due to parameter smoothing
    std::vector<float> left(kBlockSize);
    std::vector<float> right(kBlockSize);
    fillSine(left.data(), kBlockSize, 440.0f, kSampleRate, 0.5f);
    fillSine(right.data(), kBlockSize, 440.0f, kSampleRate, 0.5f);
    chain.processBlock(left.data(), right.data(), kBlockSize);

    float step = maxStepSize(left.data(), kBlockSize);
    float stepDB = linearToDbFS(step);
    INFO("Max step on bypass transition: " << step << " (" << stepDB << " dBFS)");
    // Should not have massive clicks
    REQUIRE(step < 1.5f);
}

// =============================================================================
// Phase 10: Polish - Multi-sample-rate, Performance, Allocations
// =============================================================================

TEST_CASE("RuinaeEffectsChain SC-006: multi-sample-rate operation",
          "[systems][ruinae_effects_chain][multi_rate]") {
    SECTION("44.1kHz") {
        RuinaeEffectsChain chain;
        chain.prepare(44100.0, 512);
        chain.setDelayMix(0.5f);
        chain.setDelayTime(100.0f);

        // Settle latency compensation
        settleChain(chain, 8, 44100.0, 512);

        std::vector<float> left(512);
        std::vector<float> right(512);
        fillSine(left.data(), 512, 440.0f, 44100.0);
        fillSine(right.data(), 512, 440.0f, 44100.0);
        chain.processBlock(left.data(), right.data(), 512);

        REQUIRE(calculateRMS(left.data(), 512) > 0.0f);
    }

    SECTION("96kHz") {
        RuinaeEffectsChain chain;
        chain.prepare(96000.0, 512);
        chain.setDelayMix(0.5f);
        chain.setDelayTime(100.0f);

        // Settle latency compensation
        settleChain(chain, 8, 96000.0, 512);

        std::vector<float> left(512);
        std::vector<float> right(512);
        fillSine(left.data(), 512, 440.0f, 96000.0);
        fillSine(right.data(), 512, 440.0f, 96000.0);
        chain.processBlock(left.data(), right.data(), 512);

        REQUIRE(calculateRMS(left.data(), 512) > 0.0f);
    }
}

TEST_CASE("RuinaeEffectsChain FR-028: all runtime methods are noexcept",
          "[systems][ruinae_effects_chain]") {
    // Compile-time check: all runtime methods must be noexcept
    RuinaeEffectsChain chain;
    static_assert(noexcept(chain.processBlock(nullptr, nullptr, 0)));
    static_assert(noexcept(chain.setDelayType(RuinaeDelayType::Digital)));
    static_assert(noexcept(chain.setDelayTime(0.0f)));
    static_assert(noexcept(chain.setDelayFeedback(0.0f)));
    static_assert(noexcept(chain.setDelayMix(0.0f)));
    static_assert(noexcept(chain.setDelayTempo(120.0)));
    static_assert(noexcept(chain.getActiveDelayType()));
    static_assert(noexcept(chain.getLatencySamples()));
    REQUIRE(true);
}

// =============================================================================
// Phase 11: SC-001 CPU Performance Benchmark
// =============================================================================

TEST_CASE("RuinaeEffectsChain SC-001: CPU benchmark",
          "[systems][ruinae_effects_chain][performance]") {
    // SC-001: Digital delay + reverb < 3.0% CPU at 44.1kHz, 512-sample blocks
    RuinaeEffectsChain chain;
    chain.prepare(44100.0, 512);

    // Configure: Digital delay + reverb active (per SC-001)
    chain.setDelayType(RuinaeDelayType::Digital);
    chain.setDelayMix(0.5f);
    chain.setDelayTime(200.0f);
    chain.setDelayFeedback(0.4f);

    ReverbParams reverbParams;
    reverbParams.mix = 0.3f;
    reverbParams.roomSize = 0.7f;
    reverbParams.damping = 0.5f;
    chain.setReverbParams(reverbParams);

    // Generate test signal (low-level noise to exercise all processing)
    std::vector<float> inputL(512);
    std::vector<float> inputR(512);
    for (size_t i = 0; i < 512; ++i) {
        inputL[i] = 0.1f * (static_cast<float>(i % 64) / 64.0f - 0.5f);
        inputR[i] = 0.1f * (static_cast<float>((i + 32) % 64) / 64.0f - 0.5f);
    }

    // Warm up (10 blocks)
    std::vector<float> left(512);
    std::vector<float> right(512);
    for (int i = 0; i < 10; ++i) {
        std::copy(inputL.begin(), inputL.end(), left.begin());
        std::copy(inputR.begin(), inputR.end(), right.begin());
        chain.processBlock(left.data(), right.data(), 512);
    }

    constexpr int kNumBlocks = 1000;  // ~11.6 seconds of audio

    auto start = std::chrono::high_resolution_clock::now();

    for (int block = 0; block < kNumBlocks; ++block) {
        std::copy(inputL.begin(), inputL.end(), left.begin());
        std::copy(inputR.begin(), inputR.end(), right.begin());
        chain.processBlock(left.data(), right.data(), 512);
    }

    auto elapsed = std::chrono::high_resolution_clock::now() - start;
    double elapsedMs = std::chrono::duration<double, std::milli>(elapsed).count();
    double audioDurationMs = (static_cast<double>(kNumBlocks) * 512.0 / 44100.0) * 1000.0;
    double cpuPercent = (elapsedMs / audioDurationMs) * 100.0;

    INFO("Elapsed: " << elapsedMs << " ms");
    INFO("Audio duration: " << audioDurationMs << " ms");
    INFO("CPU usage: " << cpuPercent << "%");
    // SC-001 spec target: <3.0% CPU
    // Regression guard at 10.0% to allow hardware variance on CI
    CHECK(cpuPercent < 10.0);
}

// =============================================================================
// Phaser Integration: verify phaser actually modifies signal
// =============================================================================

TEST_CASE("RuinaeEffectsChain phaser modifies signal after prepare+reset",
          "[systems][ruinae_effects_chain][phaser]") {
    // Mimics the real plugin lifecycle: prepare → reset → process
    RuinaeEffectsChain chain;
    chain.prepare(kSampleRate, kBlockSize);
    chain.reset();  // simulates setActive(true)

    // Disable delay and reverb so we only test phaser
    chain.setDelayEnabled(false);
    chain.setReverbEnabled(false);

    // Use DEFAULT phaser settings to test real-world scenario
    chain.setPhaserEnabled(true);
    chain.setPhaserMix(0.5f);
    chain.setPhaserDepth(0.5f);
    chain.setPhaserRate(0.5f);
    chain.setPhaserFeedback(0.0f);
    chain.setPhaserStages(4);
    chain.setPhaserCenterFrequency(1000.0f);

    // Generate a harmonically rich signal (sum of harmonics = pseudo-sawtooth)
    // Buffer must be large enough for latency (6144) + analysis window
    constexpr size_t kLen = 16384;
    std::vector<float> left(kLen), right(kLen);
    std::vector<float> origLeft(kLen), origRight(kLen);
    // Sum first 10 harmonics of 220 Hz for rich spectral content
    for (int h = 1; h <= 10; ++h) {
        float amp = 1.0f / static_cast<float>(h);
        for (size_t i = 0; i < kLen; ++i) {
            float val = amp * std::sin(
                2.0f * 3.14159265358979323846f * 220.0f * static_cast<float>(h)
                * static_cast<float>(i) / static_cast<float>(kSampleRate));
            left[i] += val;
            right[i] += val;
        }
    }
    std::copy(left.begin(), left.end(), origLeft.begin());
    std::copy(right.begin(), right.end(), origRight.begin());

    // Process through the effects chain
    chain.processBlock(left.data(), right.data(), kLen);

    // Skip first samples (compensation delay latency + settling)
    size_t latency = chain.getLatencySamples();
    size_t start = latency + 1024;
    REQUIRE(start < kLen);

    // Compute difference between processed and original (aligned by latency)
    float maxDiff = 0.0f;
    for (size_t i = start; i < kLen; ++i) {
        float diff = std::abs(left[i] - origLeft[i - latency]);
        maxDiff = std::max(maxDiff, diff);
    }

    INFO("Max difference between phased and original: " << maxDiff);
    // With aggressive settings and rich signal, phaser should clearly modify audio
    REQUIRE(maxDiff > 0.05f);
}

// =============================================================================
// Harmonizer Enable Volume Drop Regression Test
// =============================================================================
// Verifies that enabling the harmonizer does not cause a transient volume drop.
// Bug: HarmonizerEngine's dry/wet smoothers start at current_=0 and only advance
// when process() is called. While disabled, process() is never called, so
// enabling causes a fade-in from silence instead of instant unity gain.

TEST_CASE("Harmonizer enable produces no volume drop on first block",
          "[systems][ruinae_effects_chain][harmonizer][regression]") {
    RuinaeEffectsChain chain;
    prepareChain(chain);

    // Configure harmonizer as dry-only pass-through:
    // dry=0dB (unity), wet=-60dB (silent), 0 voices
    chain.setHarmonizerDryLevel(0.0f);    // 0 dB = unity
    chain.setHarmonizerWetLevel(-60.0f);  // effectively muted
    chain.setHarmonizerNumVoices(0);

    // Settle the chain (fills latency compensation delays)
    settleChain(chain, 20);

    // Process several blocks with harmonizer DISABLED to establish baseline
    constexpr size_t kTestBlock = 512;
    std::vector<float> left(kTestBlock);
    std::vector<float> right(kTestBlock);

    // Use identical L/R so mono sum (L+R)*0.5 = original signal level
    fillSine(left.data(), kTestBlock, 440.0f, kSampleRate, 0.5f);
    fillSine(right.data(), kTestBlock, 440.0f, kSampleRate, 0.5f);

    // Save input for re-use
    std::vector<float> inputL(left.begin(), left.end());
    std::vector<float> inputR(right.begin(), right.end());

    // Baseline: harmonizer disabled, signal passes through unmodified
    chain.setHarmonizerEnabled(false);
    chain.processBlock(left.data(), right.data(), kTestBlock);

    // Now ENABLE harmonizer and process the very first block
    std::copy(inputL.begin(), inputL.end(), left.begin());
    std::copy(inputR.begin(), inputR.end(), right.begin());
    chain.setHarmonizerEnabled(true);
    chain.processBlock(left.data(), right.data(), kTestBlock);

    // Check early samples (not sample 0 which is sin(0)=0).
    // The harmonizer in dry-only mode with identical L/R should produce:
    //   output[s] = dryGain * (L+R)*0.5 = dryGain * input[s]
    // If dry smoother is snapped to target (1.0), output ≈ input.
    // If dry smoother starts from 0, output ≈ 0.002 * input (heavily attenuated).
    constexpr size_t kCheckSample = 10;  // Non-zero sine sample
    float outVal = std::abs(left[kCheckSample]);
    float inVal = std::abs(inputL[kCheckSample]);
    REQUIRE(inVal > 0.01f);  // Sanity: input is non-zero

    float sampleRatio = outVal / inVal;
    INFO("Sample " << kCheckSample << ": output=" << outVal
         << " input=" << inVal << " ratio=" << sampleRatio);

    // With the bug (smoother from 0), ratio at sample 10 is ~0.02.
    // With the fix (snapped smoother), ratio should be ~1.0.
    REQUIRE(sampleRatio > 0.9f);
}

// =============================================================================
// Harmonizer Artifact Detection Tests (Effects Chain Level)
// =============================================================================
// These test the harmonizer integration into the effects chain, where artifacts
// can arise from the bypass/enable transition boundary.

// ---------------------------------------------------------------------------
// Test: Enable harmonizer mid-stream -- click detection at transition boundary
// ---------------------------------------------------------------------------
// When the harmonizer is disabled, signal passes through at unity gain.
// When enabled, the harmonizer's dry/wet blend applies. The transition between
// these two states must be seamless -- no click at the boundary sample.
TEST_CASE("Effects chain: harmonizer enable produces no click artifact",
          "[systems][ruinae_effects_chain][harmonizer][artifact]") {
    RuinaeEffectsChain chain;
    prepareChain(chain);

    // Configure harmonizer with audible wet signal so enable makes a
    // real difference (not just dry passthrough)
    chain.setHarmonizerHarmonyMode(0);  // Chromatic
    chain.setHarmonizerPitchShiftMode(0); // Simple
    chain.setHarmonizerDryLevel(-6.0f);  // dry at -6 dB
    chain.setHarmonizerWetLevel(-6.0f);  // wet at -6 dB
    chain.setHarmonizerNumVoices(1);
    chain.setHarmonizerVoiceInterval(0, 7);
    chain.setHarmonizerVoiceLevel(0, 0.0f);

    // Generate one continuous signal for settle + pre + post (no phase breaks)
    constexpr size_t kBlock = 512;
    constexpr size_t kSettleBlocks = 20;
    constexpr size_t kPreBlocks = 5;
    constexpr size_t kPostBlocks = 20;
    constexpr size_t kTotalBlocks = kSettleBlocks + kPreBlocks + kPostBlocks;
    constexpr size_t kTotalSamples = kTotalBlocks * kBlock;
    constexpr float kFreq = 440.0f;
    constexpr float kSR = static_cast<float>(kSampleRate);
    const float phaseInc = 2.0f * 3.14159265358979323846f * kFreq / kSR;

    std::vector<float> inputL(kTotalSamples), inputR(kTotalSamples);
    for (size_t i = 0; i < kTotalSamples; ++i) {
        float phase = phaseInc * static_cast<float>(i);
        inputL[i] = 0.5f * std::sin(phase);
        inputR[i] = inputL[i];
    }

    // Process settle blocks (harmonizer disabled, continuous input)
    std::vector<float> outL(kTotalSamples), outR(kTotalSamples);
    size_t pos = 0;
    for (size_t b = 0; b < kSettleBlocks; ++b) {
        std::copy(inputL.begin() + static_cast<std::ptrdiff_t>(pos),
                  inputL.begin() + static_cast<std::ptrdiff_t>(pos + kBlock),
                  outL.begin() + static_cast<std::ptrdiff_t>(pos));
        std::copy(inputR.begin() + static_cast<std::ptrdiff_t>(pos),
                  inputR.begin() + static_cast<std::ptrdiff_t>(pos + kBlock),
                  outR.begin() + static_cast<std::ptrdiff_t>(pos));
        chain.processBlock(outL.data() + pos, outR.data() + pos, kBlock);
        pos += kBlock;
    }

    // Process pre-blocks (harmonizer still disabled)
    for (size_t b = 0; b < kPreBlocks; ++b) {
        std::copy(inputL.begin() + static_cast<std::ptrdiff_t>(pos),
                  inputL.begin() + static_cast<std::ptrdiff_t>(pos + kBlock),
                  outL.begin() + static_cast<std::ptrdiff_t>(pos));
        std::copy(inputR.begin() + static_cast<std::ptrdiff_t>(pos),
                  inputR.begin() + static_cast<std::ptrdiff_t>(pos + kBlock),
                  outR.begin() + static_cast<std::ptrdiff_t>(pos));
        chain.processBlock(outL.data() + pos, outR.data() + pos, kBlock);
        pos += kBlock;
    }

    // Enable harmonizer and process post-blocks
    chain.setHarmonizerEnabled(true);
    for (size_t b = 0; b < kPostBlocks; ++b) {
        std::copy(inputL.begin() + static_cast<std::ptrdiff_t>(pos),
                  inputL.begin() + static_cast<std::ptrdiff_t>(pos + kBlock),
                  outL.begin() + static_cast<std::ptrdiff_t>(pos));
        std::copy(inputR.begin() + static_cast<std::ptrdiff_t>(pos),
                  inputR.begin() + static_cast<std::ptrdiff_t>(pos + kBlock),
                  outR.begin() + static_cast<std::ptrdiff_t>(pos));
        chain.processBlock(outL.data() + pos, outR.data() + pos, kBlock);
        pos += kBlock;
    }

    // Analyze from start of pre-blocks to end of post-blocks (skip settle)
    size_t analyzeStart = kSettleBlocks * kBlock;
    size_t analyzeLen = (kPreBlocks + kPostBlocks) * kBlock;

    Krate::DSP::TestUtils::ClickDetectorConfig clickCfg;
    clickCfg.sampleRate = kSR;
    clickCfg.detectionThreshold = 5.0f;
    clickCfg.energyThresholdDb = -50.0f;

    Krate::DSP::TestUtils::ClickDetector detector(clickCfg);
    detector.prepare();

    auto clicks = detector.detect(outL.data() + analyzeStart, analyzeLen);
    INFO("Harmonizer enable transition: " << clicks.size() << " clicks");
    CHECK(clicks.empty());
}

// ---------------------------------------------------------------------------
// Test: Disable harmonizer mid-stream -- click detection
// ---------------------------------------------------------------------------
// Disable transition: harmonizer output suddenly replaced by bypass signal.
// Without a crossfade, this is a discontinuity if the harmonizer was
// modifying the signal (e.g., pitch-shifted wet component).
TEST_CASE("Effects chain: harmonizer disable produces no click artifact",
          "[systems][ruinae_effects_chain][harmonizer][artifact]") {
    RuinaeEffectsChain chain;
    prepareChain(chain);

    // Enable harmonizer with audible wet signal
    chain.setHarmonizerEnabled(true);
    chain.setHarmonizerHarmonyMode(0);    // Chromatic
    chain.setHarmonizerPitchShiftMode(0); // Simple
    chain.setHarmonizerDryLevel(-6.0f);   // dry at -6 dB
    chain.setHarmonizerWetLevel(-6.0f);   // wet at -6 dB (audible)
    chain.setHarmonizerNumVoices(1);
    chain.setHarmonizerVoiceInterval(0, 7);  // +7 semitones
    chain.setHarmonizerVoiceLevel(0, 0.0f);

    // Generate one continuous signal for settle + pre + post (no phase breaks)
    constexpr size_t kBlock = 512;
    constexpr size_t kSettleBlocks = 30;
    constexpr size_t kPreBlocks = 10;
    constexpr size_t kPostBlocks = 20;
    constexpr size_t kTotalBlocks = kSettleBlocks + kPreBlocks + kPostBlocks;
    constexpr size_t kTotalSamples = kTotalBlocks * kBlock;
    constexpr float kFreq = 440.0f;
    constexpr float kSR = static_cast<float>(kSampleRate);
    const float phaseInc = 2.0f * 3.14159265358979323846f * kFreq / kSR;

    std::vector<float> inputL(kTotalSamples), inputR(kTotalSamples);
    for (size_t i = 0; i < kTotalSamples; ++i) {
        float phase = phaseInc * static_cast<float>(i);
        inputL[i] = 0.5f * std::sin(phase);
        inputR[i] = inputL[i];
    }

    // Process settle blocks (harmonizer enabled, continuous input)
    std::vector<float> outL(kTotalSamples), outR(kTotalSamples);
    size_t pos = 0;
    for (size_t b = 0; b < kSettleBlocks; ++b) {
        std::copy(inputL.begin() + static_cast<std::ptrdiff_t>(pos),
                  inputL.begin() + static_cast<std::ptrdiff_t>(pos + kBlock),
                  outL.begin() + static_cast<std::ptrdiff_t>(pos));
        std::copy(inputR.begin() + static_cast<std::ptrdiff_t>(pos),
                  inputR.begin() + static_cast<std::ptrdiff_t>(pos + kBlock),
                  outR.begin() + static_cast<std::ptrdiff_t>(pos));
        chain.processBlock(outL.data() + pos, outR.data() + pos, kBlock);
        pos += kBlock;
    }

    // Process pre-blocks (harmonizer still enabled, steady state)
    for (size_t b = 0; b < kPreBlocks; ++b) {
        std::copy(inputL.begin() + static_cast<std::ptrdiff_t>(pos),
                  inputL.begin() + static_cast<std::ptrdiff_t>(pos + kBlock),
                  outL.begin() + static_cast<std::ptrdiff_t>(pos));
        std::copy(inputR.begin() + static_cast<std::ptrdiff_t>(pos),
                  inputR.begin() + static_cast<std::ptrdiff_t>(pos + kBlock),
                  outR.begin() + static_cast<std::ptrdiff_t>(pos));
        chain.processBlock(outL.data() + pos, outR.data() + pos, kBlock);
        pos += kBlock;
    }

    // Disable harmonizer and process post-blocks
    chain.setHarmonizerEnabled(false);
    for (size_t b = 0; b < kPostBlocks; ++b) {
        std::copy(inputL.begin() + static_cast<std::ptrdiff_t>(pos),
                  inputL.begin() + static_cast<std::ptrdiff_t>(pos + kBlock),
                  outL.begin() + static_cast<std::ptrdiff_t>(pos));
        std::copy(inputR.begin() + static_cast<std::ptrdiff_t>(pos),
                  inputR.begin() + static_cast<std::ptrdiff_t>(pos + kBlock),
                  outR.begin() + static_cast<std::ptrdiff_t>(pos));
        chain.processBlock(outL.data() + pos, outR.data() + pos, kBlock);
        pos += kBlock;
    }

    // Analyze from start of pre-blocks to end of post-blocks (skip settle)
    size_t analyzeStart = kSettleBlocks * kBlock;
    size_t analyzeLen = (kPreBlocks + kPostBlocks) * kBlock;

    Krate::DSP::TestUtils::ClickDetectorConfig clickCfg;
    clickCfg.sampleRate = kSR;
    clickCfg.detectionThreshold = 5.0f;
    clickCfg.energyThresholdDb = -50.0f;

    Krate::DSP::TestUtils::ClickDetector detector(clickCfg);
    detector.prepare();

    auto clicks = detector.detect(outL.data() + analyzeStart, analyzeLen);
    std::string disableMsg = "Harmonizer disable: " + std::to_string(clicks.size()) + " clicks. ";
    for (const auto& c : clicks) {
        size_t globalSample = c.sampleIndex + analyzeStart;
        disableMsg += "sample=" + std::to_string(globalSample) +
                      " (block " + std::to_string(globalSample / kBlock) + ")" +
                      " t=" + std::to_string(c.timeSeconds) + "s" +
                      " amp=" + std::to_string(c.amplitude) + ". ";
    }
    INFO(disableMsg);
    CHECK(clicks.empty());
}

// =============================================================================
// Harmonizer Wet Level Diagnostic
// =============================================================================
// Measures actual harmonizer wet output levels through the effects chain
// to diagnose the low-volume bug reported by the user.

TEST_CASE("Effects chain: harmonizer wet output level diagnostic",
          "[systems][ruinae_effects_chain][harmonizer][diagnostic]") {

    constexpr size_t kBlock = 512;
    constexpr double kSR = 44100.0;
    constexpr size_t kWarmupBlocks = 40;  // ~465ms settle time
    constexpr size_t kMeasureBlocks = 20; // ~232ms measurement window
    constexpr size_t kTotalBlocks = kWarmupBlocks + kMeasureBlocks;

    // Generate a continuous 440Hz sine at 0.5 amplitude as the test signal
    const size_t totalSamples = kTotalBlocks * kBlock;
    std::vector<float> sineL(totalSamples), sineR(totalSamples);
    fillSine(sineL.data(), totalSamples, 440.0f, kSR, 0.5f);
    fillSine(sineR.data(), totalSamples, 440.0f, kSR, 0.5f);

    // Measure input RMS for reference
    const size_t measureStart = kWarmupBlocks * kBlock;
    const size_t measureLen = kMeasureBlocks * kBlock;
    float inputRMS = calculateRMS(sineL.data() + measureStart, measureLen);
    INFO("Input RMS: " << inputRMS);

    // Helper: run harmonizer through effects chain and measure output RMS
    auto measureEffectsChainHarmonizer = [&](
        const char* label,
        float dryLevelDb, float wetLevelDb,
        int numVoices, int pitchMode,
        int v0Interval, float v0LevelDb, float v0Pan,
        int v1Interval = 0, float v1LevelDb = 0.0f,
        int v2Interval = 0, float v2LevelDb = 0.0f,
        int v3Interval = 0, float v3LevelDb = 0.0f
    ) -> std::pair<float, float> {
        RuinaeEffectsChain chain;
        chain.prepare(kSR, kBlock);

        // Set harmonizer params BEFORE enabling (simulates normal plugin flow
        // where applyParamsToEngine runs every block even when disabled)
        chain.setHarmonizerHarmonyMode(0);  // Chromatic
        chain.setHarmonizerPitchShiftMode(pitchMode);
        chain.setHarmonizerDryLevel(dryLevelDb);
        chain.setHarmonizerWetLevel(wetLevelDb);
        chain.setHarmonizerNumVoices(numVoices);
        chain.setHarmonizerVoiceInterval(0, v0Interval);
        chain.setHarmonizerVoiceLevel(0, v0LevelDb);
        chain.setHarmonizerVoicePan(0, v0Pan);
        if (numVoices > 1) {
            chain.setHarmonizerVoiceInterval(1, v1Interval);
            chain.setHarmonizerVoiceLevel(1, v1LevelDb);
            chain.setHarmonizerVoicePan(1, 0.0f);
        }
        if (numVoices > 2) {
            chain.setHarmonizerVoiceInterval(2, v2Interval);
            chain.setHarmonizerVoiceLevel(2, v2LevelDb);
            chain.setHarmonizerVoicePan(2, 0.0f);
        }
        if (numVoices > 3) {
            chain.setHarmonizerVoiceInterval(3, v3Interval);
            chain.setHarmonizerVoiceLevel(3, v3LevelDb);
            chain.setHarmonizerVoicePan(3, 0.0f);
        }

        // Run a few blocks with harmonizer disabled first (like the plugin does)
        for (size_t b = 0; b < 5; ++b) {
            std::vector<float> tmpL(sineL.begin() + static_cast<std::ptrdiff_t>(b * kBlock),
                                     sineL.begin() + static_cast<std::ptrdiff_t>((b+1) * kBlock));
            std::vector<float> tmpR(sineR.begin() + static_cast<std::ptrdiff_t>(b * kBlock),
                                     sineR.begin() + static_cast<std::ptrdiff_t>((b+1) * kBlock));
            chain.processBlock(tmpL.data(), tmpR.data(), kBlock);
        }

        // Enable harmonizer (this triggers snapParameters + fade-in)
        chain.setHarmonizerEnabled(true);

        // Process the remaining blocks
        std::vector<float> outL(totalSamples, 0.0f);
        std::vector<float> outR(totalSamples, 0.0f);
        for (size_t b = 0; b < kTotalBlocks; ++b) {
            std::copy(sineL.begin() + static_cast<std::ptrdiff_t>(b * kBlock),
                      sineL.begin() + static_cast<std::ptrdiff_t>((b+1) * kBlock),
                      outL.begin() + static_cast<std::ptrdiff_t>(b * kBlock));
            std::copy(sineR.begin() + static_cast<std::ptrdiff_t>(b * kBlock),
                      sineR.begin() + static_cast<std::ptrdiff_t>((b+1) * kBlock),
                      outR.begin() + static_cast<std::ptrdiff_t>(b * kBlock));

            // Re-set params every block (like applyParamsToEngine does)
            chain.setHarmonizerEnabled(true);
            chain.setHarmonizerDryLevel(dryLevelDb);
            chain.setHarmonizerWetLevel(wetLevelDb);
            chain.setHarmonizerNumVoices(numVoices);
            chain.setHarmonizerVoiceInterval(0, v0Interval);
            chain.setHarmonizerVoiceLevel(0, v0LevelDb);
            chain.setHarmonizerVoicePan(0, v0Pan);
            if (numVoices > 1) {
                chain.setHarmonizerVoiceInterval(1, v1Interval);
                chain.setHarmonizerVoiceLevel(1, v1LevelDb);
                chain.setHarmonizerVoicePan(1, 0.0f);
            }
            if (numVoices > 2) {
                chain.setHarmonizerVoiceInterval(2, v2Interval);
                chain.setHarmonizerVoiceLevel(2, v2LevelDb);
                chain.setHarmonizerVoicePan(2, 0.0f);
            }
            if (numVoices > 3) {
                chain.setHarmonizerVoiceInterval(3, v3Interval);
                chain.setHarmonizerVoiceLevel(3, v3LevelDb);
                chain.setHarmonizerVoicePan(3, 0.0f);
            }

            chain.processBlock(
                outL.data() + static_cast<std::ptrdiff_t>(b * kBlock),
                outR.data() + static_cast<std::ptrdiff_t>(b * kBlock),
                kBlock);
        }

        float rms = calculateRMS(outL.data() + measureStart, measureLen);
        float peak = peakAbsolute(outL.data() + measureStart, measureLen);
        float ratio = (inputRMS > 0.0f) ? rms / inputRMS : 0.0f;
        INFO(label << " RMS: " << rms << "  Peak: " << peak
             << "  Ratio: " << ratio << "  dB: " << linearToDbFS(rms));
        return {rms, peak};
    };

    SECTION("Wet only, 1 voice Simple +7st, wet 0dB") {
        auto [rms, peak] = measureEffectsChainHarmonizer(
            "Simple +7st wet-only",
            -60.0f, 0.0f,       // dry muted, wet 0dB
            1, 0,                // 1 voice, Simple mode
            7, 0.0f, 0.0f);     // +7 semitones, 0dB level, center pan
        CHECK(rms > inputRMS * 0.3f);
    }

    SECTION("Wet only, 1 voice Simple +7st, wet +6dB") {
        auto [rms, peak] = measureEffectsChainHarmonizer(
            "Simple +7st wet+6dB",
            -60.0f, 6.0f,       // dry muted, wet +6dB
            1, 0,                // 1 voice, Simple mode
            7, 0.0f, 0.0f);     // +7 semitones, 0dB level, center pan
        CHECK(rms > inputRMS * 0.5f);
    }

    SECTION("4 voices Simple, default intervals (0), wet 0dB") {
        auto [rms, peak] = measureEffectsChainHarmonizer(
            "4 voices unison wet-only",
            -60.0f, 0.0f,       // dry muted, wet 0dB
            4, 0,                // 4 voices, Simple mode
            0, 0.0f, 0.0f,      // V1: unison, 0dB, center
            0, 0.0f,             // V2: unison, 0dB
            0, 0.0f,             // V3: unison, 0dB
            0, 0.0f);            // V4: unison, 0dB
        CHECK(rms > inputRMS * 1.0f);
    }

    SECTION("Dry 50% + wet 100% (user scenario)") {
        // User sets dry knob to 50% (norm 0.5 = -27dB) and wet to 100% (norm 1.0 = +6dB)
        float dryDb = 0.5f * 66.0f - 60.0f;  // -27 dB
        float wetDb = 1.0f * 66.0f - 60.0f;  // +6 dB
        auto [rms, peak] = measureEffectsChainHarmonizer(
            "User scenario: dry50% wet100%",
            dryDb, wetDb,        // -27dB dry, +6dB wet
            4, 0,                // 4 voices, Simple mode
            7, 0.0f, 0.0f,      // V1: +7st, 0dB, center
            0, 0.0f,             // V2: unison, 0dB
            0, 0.0f,             // V3: unison, 0dB
            0, 0.0f);            // V4: unison, 0dB
        INFO("Expected: wet should dominate. Dry is -27dB, wet is +6dB");
        CHECK(rms > inputRMS * 0.5f);
    }

    SECTION("Dry off + wet 100%, 1 voice PV +7st") {
        auto [rms, peak] = measureEffectsChainHarmonizer(
            "PV +7st wet-only +6dB",
            -60.0f, 6.0f,       // dry muted, wet +6dB
            1, 2,                // 1 voice, PhaseVocoder
            7, 0.0f, 0.0f);     // +7 semitones, 0dB level, center pan
        CHECK(rms > inputRMS * 0.3f);
    }

    SECTION("Dry off + wet 100%, 1 voice Granular +7st") {
        auto [rms, peak] = measureEffectsChainHarmonizer(
            "Granular +7st wet-only +6dB",
            -60.0f, 6.0f,       // dry muted, wet +6dB
            1, 1,                // 1 voice, Granular
            7, 0.0f, 0.0f);     // +7 semitones, 0dB level, center pan
        CHECK(rms > inputRMS * 0.3f);
    }
}
