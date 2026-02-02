// ==============================================================================
// Integration Test: Mode Switch Buffer Reset
// ==============================================================================
// Regression tests verifying that delay mode buffers are properly cleared when
// switching modes. The processor keeps all 10 effect instances alive, but only
// calls process() on the active mode. When switching to a new mode,
// Processor::resetMode() clears the target effect's delay buffers so stale
// audio from a previous session doesn't play back as "ghost" echoes.
//
// Fix location: Processor::process() calls resetMode(currentProcessingMode_)
// when a mode change is detected, matching the reset behavior of setActive().
//
// These tests simulate the processor's mode switching pattern at the DSP level:
//   1. Play audio through mode A (Digital) with feedback -> fills delay buffer
//   2. Switch to mode B (PingPong) -> reset Digital's buffers (the fix)
//   3. Process audio through PingPong
//   4. Switch back to Digital -> reset Digital's buffers again
//   5. Verify Digital outputs silence (no ghost audio)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/core/block_context.h>
#include <krate/dsp/core/crossfade_utils.h>
#include <krate/dsp/effects/digital_delay.h>
#include <krate/dsp/effects/ping_pong_delay.h>
#include <test_signals.h>

#include <algorithm>
#include <cmath>
#include <numeric>
#include <vector>

using Catch::Approx;
using namespace Krate::DSP;

namespace {

constexpr double kSampleRate = 44100.0;
constexpr size_t kBlockSize = 512;
constexpr float kDelayTimeMs = 100.0f;   // 100ms delay
constexpr float kFeedback = 0.7f;        // 70% feedback (significant tail)
constexpr float kMix = 1.0f;             // 100% wet for clarity
constexpr float kTestCrossfadeTimeMs = 50.0f;

/// Calculate RMS of a buffer
float rms(const float* buffer, size_t size) {
    if (size == 0) return 0.0f;
    float sumSquares = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        sumSquares += buffer[i] * buffer[i];
    }
    return std::sqrt(sumSquares / static_cast<float>(size));
}

/// Calculate peak absolute value of a buffer
float peakAbs(const float* buffer, size_t size) {
    float peak = 0.0f;
    for (size_t i = 0; i < size; ++i) {
        peak = std::max(peak, std::abs(buffer[i]));
    }
    return peak;
}

/// Process N blocks of silence through an effect
void processSilenceBlocks(DigitalDelay& effect, size_t numBlocks, const BlockContext& ctx) {
    std::vector<float> silenceL(kBlockSize, 0.0f);
    std::vector<float> silenceR(kBlockSize, 0.0f);
    for (size_t b = 0; b < numBlocks; ++b) {
        std::fill(silenceL.begin(), silenceL.end(), 0.0f);
        std::fill(silenceR.begin(), silenceR.end(), 0.0f);
        effect.process(silenceL.data(), silenceR.data(), kBlockSize, ctx);
    }
}

void processSilenceBlocks(PingPongDelay& effect, size_t numBlocks, const BlockContext& ctx) {
    std::vector<float> silenceL(kBlockSize, 0.0f);
    std::vector<float> silenceR(kBlockSize, 0.0f);
    for (size_t b = 0; b < numBlocks; ++b) {
        std::fill(silenceL.begin(), silenceL.end(), 0.0f);
        std::fill(silenceR.begin(), silenceR.end(), 0.0f);
        effect.process(silenceL.data(), silenceR.data(), kBlockSize, ctx);
    }
}

} // anonymous namespace

// =============================================================================
// Core Regression: No Stale Buffer Playback After Mode Round-Trip
// =============================================================================

TEST_CASE("Mode switch resets delay buffers - no ghost audio on return",
          "[processor][mode-switch][buffer-retention]") {
    // Regression test for the user-reported issue:
    // "When I play continuous sound and switch between modes, the sound
    //  doesn't seem to get reset properly"

    BlockContext ctx{
        .sampleRate = kSampleRate,
        .blockSize = kBlockSize,
        .tempoBPM = 120.0,
        .isPlaying = true
    };

    DigitalDelay digital;
    PingPongDelay pingPong;

    digital.prepare(kSampleRate, kBlockSize, 5000.0f);
    pingPong.prepare(kSampleRate, kBlockSize, 5000.0f);

    // Configure Digital delay
    digital.setTime(kDelayTimeMs);
    digital.setFeedback(kFeedback);
    digital.setMix(kMix);
    digital.reset();

    // Configure PingPong delay
    pingPong.setDelayTimeMs(kDelayTimeMs);
    pingPong.setFeedback(kFeedback);
    pingPong.setMix(kMix);
    pingPong.reset();

    SECTION("reset on mode switch clears stale audio after round-trip") {
        // =====================================================================
        // Step 1: Feed audio into Digital delay to fill its buffer
        // =====================================================================
        std::vector<float> inputL(kBlockSize, 0.0f);
        std::vector<float> inputR(kBlockSize, 0.0f);

        // Generate a loud impulse train (simulates continuous drum loop)
        for (size_t i = 0; i < kBlockSize; i += 100) {
            inputL[i] = 0.9f;
            inputR[i] = 0.9f;
        }

        // Process several blocks through Digital to build up feedback
        for (int block = 0; block < 20; ++block) {
            std::vector<float> procL(inputL);
            std::vector<float> procR(inputR);
            digital.process(procL.data(), procR.data(), kBlockSize, ctx);
        }

        // Verify Digital has audio in its buffer by processing one block of silence
        std::vector<float> checkL(kBlockSize, 0.0f);
        std::vector<float> checkR(kBlockSize, 0.0f);
        digital.process(checkL.data(), checkR.data(), kBlockSize, ctx);
        float digitalRmsAfterFill = rms(checkL.data(), kBlockSize);
        INFO("Digital RMS after filling buffer: " << digitalRmsAfterFill);
        REQUIRE(digitalRmsAfterFill > 0.01f); // Buffer has audio

        // =====================================================================
        // Step 2: "Switch to PingPong" - reset PingPong (as processor does),
        // then process through PingPong while Digital sits dormant.
        // =====================================================================
        pingPong.reset();  // Processor calls resetMode() on new mode

        // Process 200 blocks (~2.3 seconds) of silence through PingPong
        // This is far longer than Digital's feedback tail would last
        processSilenceBlocks(pingPong, 200, ctx);

        // =====================================================================
        // Step 3: "Switch back to Digital" - reset Digital (as processor does),
        // then process silence. Should output silence, not ghost echoes.
        // =====================================================================
        digital.reset();  // Processor calls resetMode() on new mode

        std::vector<float> returnL(kBlockSize, 0.0f);
        std::vector<float> returnR(kBlockSize, 0.0f);
        digital.process(returnL.data(), returnR.data(), kBlockSize, ctx);

        float digitalRmsOnReturn = rms(returnL.data(), kBlockSize);
        INFO("Digital RMS on return (should be ~0 with reset): "
             << digitalRmsOnReturn);

        // With reset() on mode switch, Digital outputs silence (no ghost audio)
        REQUIRE(digitalRmsOnReturn < 0.001f);
    }

    SECTION("multiple round-trips produce no stale audio artifacts") {
        // Verify that repeated mode switches don't accumulate stale buffer content.

        std::vector<float> rmsValues;

        for (int trip = 0; trip < 5; ++trip) {
            // Feed audio into Digital
            for (int block = 0; block < 10; ++block) {
                std::vector<float> inputL(kBlockSize, 0.0f);
                std::vector<float> inputR(kBlockSize, 0.0f);
                for (size_t i = 0; i < kBlockSize; i += 100) {
                    inputL[i] = 0.9f;
                    inputR[i] = 0.9f;
                }
                digital.process(inputL.data(), inputR.data(), kBlockSize, ctx);
            }

            // "Switch" to PingPong
            pingPong.reset();
            processSilenceBlocks(pingPong, 50, ctx);

            // "Switch back" to Digital with reset
            digital.reset();
            std::vector<float> measureL(kBlockSize, 0.0f);
            std::vector<float> measureR(kBlockSize, 0.0f);
            digital.process(measureL.data(), measureR.data(), kBlockSize, ctx);

            float measuredRms = rms(measureL.data(), kBlockSize);
            rmsValues.push_back(measuredRms);
            INFO("Round-trip " << trip << " return RMS: " << measuredRms);
        }

        // Each return should produce silence
        for (size_t i = 0; i < rmsValues.size(); ++i) {
            INFO("Trip " << i << " RMS: " << rmsValues[i]);
            REQUIRE(rmsValues[i] < 0.001f);
        }
    }
}

// =============================================================================
// Crossfade with Reset: Clean transition after dormant period
// =============================================================================

TEST_CASE("Crossfade with reset produces clean transition",
          "[processor][mode-switch][crossfade][buffer-retention]") {
    // Verifies that when the processor crossfades back to a previously-used
    // mode, the reset ensures no stale audio bleeds through after the
    // crossfade completes.

    BlockContext ctx{
        .sampleRate = kSampleRate,
        .blockSize = kBlockSize,
        .tempoBPM = 120.0,
        .isPlaying = true
    };

    DigitalDelay digital;
    PingPongDelay pingPong;

    digital.prepare(kSampleRate, kBlockSize, 5000.0f);
    pingPong.prepare(kSampleRate, kBlockSize, 5000.0f);

    digital.setTime(kDelayTimeMs);
    digital.setFeedback(kFeedback);
    digital.setMix(kMix);
    digital.reset();

    pingPong.setDelayTimeMs(kDelayTimeMs);
    pingPong.setFeedback(0.0f);  // No feedback on PingPong - pure silence
    pingPong.setMix(kMix);
    pingPong.reset();

    // Step 1: Fill Digital's delay buffer with audio
    for (int block = 0; block < 20; ++block) {
        std::vector<float> inputL(kBlockSize, 0.0f);
        std::vector<float> inputR(kBlockSize, 0.0f);
        for (size_t i = 0; i < kBlockSize; i += 50) {
            inputL[i] = 0.8f;
            inputR[i] = 0.8f;
        }
        digital.process(inputL.data(), inputR.data(), kBlockSize, ctx);
    }

    // Step 2: "Switch" to PingPong, process plenty of silence
    pingPong.reset();
    processSilenceBlocks(pingPong, 200, ctx);

    // Step 3: Simulate switching back to Digital WITH reset + crossfade
    // (mimicking what the fixed processor does)
    digital.reset();  // resetMode() clears stale buffers

    float crossfadeInc = Krate::DSP::crossfadeIncrement(kTestCrossfadeTimeMs, kSampleRate);
    float crossfadePosition = 0.0f;

    // Digital output (should be silence after reset)
    std::vector<float> digitalL(kBlockSize, 0.0f);
    std::vector<float> digitalR(kBlockSize, 0.0f);
    digital.process(digitalL.data(), digitalR.data(), kBlockSize, ctx);

    // PingPong output (should be silence after 200 blocks with no feedback)
    std::vector<float> pingPongL(kBlockSize, 0.0f);
    std::vector<float> pingPongR(kBlockSize, 0.0f);
    pingPong.process(pingPongL.data(), pingPongR.data(), kBlockSize, ctx);

    // Apply crossfade blending
    std::vector<float> blendedL(kBlockSize, 0.0f);
    std::vector<float> blendedR(kBlockSize, 0.0f);
    for (size_t i = 0; i < kBlockSize; ++i) {
        float fadeOut, fadeIn;
        Krate::DSP::equalPowerGains(crossfadePosition, fadeOut, fadeIn);

        // Old mode (PingPong) fading out, New mode (Digital) fading in
        blendedL[i] = pingPongL[i] * fadeOut + digitalL[i] * fadeIn;
        blendedR[i] = pingPongR[i] * fadeOut + digitalR[i] * fadeIn;

        crossfadePosition = std::min(1.0f, crossfadePosition + crossfadeInc);
    }

    // After the crossfade, continue processing Digital with silence
    std::vector<float> postCrossfadeL(kBlockSize, 0.0f);
    std::vector<float> postCrossfadeR(kBlockSize, 0.0f);
    digital.process(postCrossfadeL.data(), postCrossfadeR.data(), kBlockSize, ctx);

    float postCrossfadeRms = rms(postCrossfadeL.data(), kBlockSize);
    INFO("Post-crossfade Digital RMS (should be ~0 with reset): " << postCrossfadeRms);

    // With reset, Digital outputs silence after the crossfade completes
    REQUIRE(postCrossfadeRms < 0.001f);
}

// =============================================================================
// Continuous Audio Scenario (closest to user's report)
// =============================================================================

TEST_CASE("Continuous drum loop with mode switching - clean reset",
          "[processor][mode-switch][buffer-retention][integration]") {
    // Regression test for the user's reported scenario:
    // playing a continuous drum loop and switching between modes.

    BlockContext ctx{
        .sampleRate = kSampleRate,
        .blockSize = kBlockSize,
        .tempoBPM = 120.0,
        .isPlaying = true
    };

    DigitalDelay digital;
    PingPongDelay pingPong;

    digital.prepare(kSampleRate, kBlockSize, 5000.0f);
    pingPong.prepare(kSampleRate, kBlockSize, 5000.0f);

    digital.setTime(250.0f);    // 250ms delay (quarter note at 120 BPM)
    digital.setFeedback(0.5f);  // Moderate feedback
    digital.setMix(0.5f);       // 50% wet
    digital.reset();

    pingPong.setDelayTimeMs(250.0f);
    pingPong.setFeedback(0.5f);
    pingPong.setMix(0.5f);
    pingPong.reset();

    // Generate a "drum loop" pattern - periodic impulses at ~8th note intervals
    auto generateDrumBlock = [](float* left, float* right, size_t size, int blockNum) {
        std::fill_n(left, size, 0.0f);
        std::fill_n(right, size, 0.0f);
        // Kick-like hits every ~5513 samples (8th note at 120 BPM)
        const size_t hitInterval = 5513;
        const size_t globalOffset = blockNum * size;
        for (size_t i = 0; i < size; ++i) {
            if ((globalOffset + i) % hitInterval < 20) {
                // Short burst simulating a drum transient
                float envelope = 1.0f - static_cast<float>((globalOffset + i) % hitInterval) / 20.0f;
                left[i] = 0.8f * envelope;
                right[i] = 0.8f * envelope;
            }
        }
    };

    // Phase 1: Play drum loop through Digital delay for ~1 second
    for (int block = 0; block < 86; ++block) {  // ~1 second at 512 samples
        std::vector<float> drumL(kBlockSize);
        std::vector<float> drumR(kBlockSize);
        generateDrumBlock(drumL.data(), drumR.data(), kBlockSize, block);
        digital.process(drumL.data(), drumR.data(), kBlockSize, ctx);
    }

    // Phase 2: Switch to PingPong (reset it first), play drum loop for ~1 second
    pingPong.reset();
    for (int block = 0; block < 86; ++block) {
        std::vector<float> drumL(kBlockSize);
        std::vector<float> drumR(kBlockSize);
        generateDrumBlock(drumL.data(), drumR.data(), kBlockSize, 86 + block);
        pingPong.process(drumL.data(), drumR.data(), kBlockSize, ctx);
    }

    // Phase 3: Stop the drum loop (silence), still on PingPong
    // Let PingPong's tail decay completely
    processSilenceBlocks(pingPong, 200, ctx);

    // Phase 4: Switch back to Digital (reset it first)
    // With reset, feeding silence should produce silence.
    digital.reset();

    std::vector<float> testL(kBlockSize, 0.0f);
    std::vector<float> testR(kBlockSize, 0.0f);
    digital.process(testL.data(), testR.data(), kBlockSize, ctx);

    float returnPeak = peakAbs(testL.data(), kBlockSize);
    float returnRms = rms(testL.data(), kBlockSize);
    INFO("Digital peak on return after silence: " << returnPeak);
    INFO("Digital RMS on return after silence: " << returnRms);

    // With reset on mode switch, Digital outputs silence - no ghost echoes
    REQUIRE(returnPeak < 0.001f);
    REQUIRE(returnRms < 0.001f);
}
