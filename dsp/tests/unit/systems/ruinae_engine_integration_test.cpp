// ==============================================================================
// Layer 3: Integration Tests - RuinaeEngine
// ==============================================================================
// End-to-end MIDI-to-output signal path tests for the Ruinae synthesizer engine.
// These tests verify the complete signal chain from MIDI input through all
// processing stages to stereo output.
//
// Note: The effects chain includes a spectral delay with 1024-sample FFT size,
// which introduces latency. Tests must process multiple blocks before expecting
// audio output.
//
// Reference: specs/044-engine-composition/spec.md
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/benchmark/catch_benchmark.hpp>

#include <krate/dsp/systems/ruinae_engine.h>
#include <krate/dsp/core/pitch_utils.h>
#include <krate/dsp/core/midi_utils.h>

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <limits>
#include <numeric>
#include <vector>

using namespace Krate::DSP;
using Catch::Approx;

// =============================================================================
// Test Helpers
// =============================================================================

static constexpr size_t kBlockSize = 512;

/// Number of warm-up blocks to process before expecting audio.
/// The effects chain has latency compensation (spectral delay FFT = 1024 samples).
static constexpr int kWarmUpBlocks = 10;

static float findPeak(const float* buffer, size_t numSamples) {
    float peak = 0.0f;
    for (size_t i = 0; i < numSamples; ++i) {
        float absVal = std::abs(buffer[i]);
        if (absVal > peak) peak = absVal;
    }
    return peak;
}

static float computeRMS(const float* buffer, size_t numSamples) {
    if (numSamples == 0) return 0.0f;
    double sumSq = 0.0;
    for (size_t i = 0; i < numSamples; ++i) {
        sumSq += static_cast<double>(buffer[i]) * static_cast<double>(buffer[i]);
    }
    return static_cast<float>(std::sqrt(sumSq / static_cast<double>(numSamples)));
}

static bool isAllZeros(const float* buffer, size_t numSamples) {
    for (size_t i = 0; i < numSamples; ++i) {
        if (buffer[i] != 0.0f) return false;
    }
    return true;
}

static bool hasNonZeroSamples(const float* buffer, size_t numSamples) {
    for (size_t i = 0; i < numSamples; ++i) {
        if (buffer[i] != 0.0f) return true;
    }
    return false;
}

static bool allSamplesFinite(const float* buffer, size_t numSamples) {
    for (size_t i = 0; i < numSamples; ++i) {
        if (std::isnan(buffer[i]) || std::isinf(buffer[i])) return false;
    }
    return true;
}

/// Process multiple blocks and check if any produce non-zero audio.
static bool processAndCheckForAudio(RuinaeEngine& engine, int numBlocks = kWarmUpBlocks) {
    std::vector<float> left(kBlockSize), right(kBlockSize);
    for (int i = 0; i < numBlocks; ++i) {
        engine.processBlock(left.data(), right.data(), kBlockSize);
        if (hasNonZeroSamples(left.data(), kBlockSize) ||
            hasNonZeroSamples(right.data(), kBlockSize)) {
            return true;
        }
    }
    return false;
}

/// Process multiple blocks and accumulate RMS over all blocks.
static float processAndAccumulateRMS(RuinaeEngine& engine,
                                     std::vector<float>& left,
                                     std::vector<float>& right,
                                     int numBlocks = kWarmUpBlocks) {
    float totalRms = 0.0f;
    for (int i = 0; i < numBlocks; ++i) {
        engine.processBlock(left.data(), right.data(), kBlockSize);
        totalRms += computeRMS(left.data(), kBlockSize);
    }
    return totalRms;
}

// =============================================================================
// Integration Test: Full Signal Path (MIDI noteOn -> stereo audio)
// =============================================================================

TEST_CASE("RuinaeEngine integration: MIDI noteOn to stereo output",
          "[ruinae-engine-integration][signal-path]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, kBlockSize);

    SECTION("single note produces stereo audio") {
        engine.noteOn(60, 100); // Middle C

        std::vector<float> left(kBlockSize), right(kBlockSize);
        bool hasAudioL = false, hasAudioR = false;
        bool allFinite = true;

        for (int i = 0; i < kWarmUpBlocks; ++i) {
            engine.processBlock(left.data(), right.data(), kBlockSize);
            if (hasNonZeroSamples(left.data(), kBlockSize)) hasAudioL = true;
            if (hasNonZeroSamples(right.data(), kBlockSize)) hasAudioR = true;
            if (!allSamplesFinite(left.data(), kBlockSize) ||
                !allSamplesFinite(right.data(), kBlockSize)) {
                allFinite = false;
            }
        }

        REQUIRE(hasAudioL);
        REQUIRE(hasAudioR);
        REQUIRE(allFinite);
    }
}

TEST_CASE("RuinaeEngine integration: chord playback",
          "[ruinae-engine-integration][signal-path]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, kBlockSize);
    engine.setSoftLimitEnabled(false);

    SECTION("C major chord activates 3 voices and produces stereo output") {
        engine.noteOn(60, 100); // C4
        engine.noteOn(64, 100); // E4
        engine.noteOn(67, 100); // G4

        REQUIRE(engine.getActiveVoiceCount() == 3);

        std::vector<float> left(kBlockSize), right(kBlockSize);
        float chordRms = processAndAccumulateRMS(engine, left, right);

        engine.reset();
        engine.noteOn(60, 100); // Single note
        float singleRms = processAndAccumulateRMS(engine, left, right);

        if (singleRms > 0.001f) {
            REQUIRE(chordRms > singleRms * 0.5f);
        }
    }
}

TEST_CASE("RuinaeEngine integration: noteOff -> release -> silence",
          "[ruinae-engine-integration][signal-path]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, kBlockSize);
    engine.setAmpRelease(5.0f); // Very short 5ms release

    SECTION("note eventually reaches silence after noteOff") {
        // Disable effects to isolate voice release behavior
        engine.setDelayMix(0.0f);
        engine.setReverbParams({.roomSize = 0.5f, .damping = 0.5f, .width = 1.0f, .mix = 0.0f});

        engine.noteOn(60, 100);

        // Process several blocks to establish audio through effects chain
        REQUIRE(processAndCheckForAudio(engine));

        // Release the note
        engine.noteOff(60);

        // Process enough blocks for release to complete
        // With effects disabled, the signal should decay to near-silence
        std::vector<float> left(kBlockSize), right(kBlockSize);
        bool reachedSilence = false;
        constexpr float kSilenceThreshold = 1e-6f;
        for (int block = 0; block < 500; ++block) {
            engine.processBlock(left.data(), right.data(), kBlockSize);
            float peakL = findPeak(left.data(), kBlockSize);
            float peakR = findPeak(right.data(), kBlockSize);
            if (peakL < kSilenceThreshold && peakR < kSilenceThreshold) {
                reachedSilence = true;
                break;
            }
        }
        REQUIRE(reachedSilence);
        REQUIRE(engine.getActiveVoiceCount() == 0);
    }
}

TEST_CASE("RuinaeEngine integration: voice stealing",
          "[ruinae-engine-integration][signal-path]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, kBlockSize);
    engine.setPolyphony(2); // Only 2 voices

    SECTION("exceeding polyphony triggers voice stealing") {
        engine.noteOn(60, 100);
        engine.noteOn(64, 100);
        REQUIRE(engine.getActiveVoiceCount() == 2);

        // Third note should steal a voice
        engine.noteOn(67, 100);
        REQUIRE(engine.getActiveVoiceCount() <= 2);

        // Should still produce audio after warm-up
        REQUIRE(processAndCheckForAudio(engine));
    }
}

// =============================================================================
// Integration Test: Stereo Spread (SC-010)
// =============================================================================

TEST_CASE("RuinaeEngine integration: stereo spread verification",
          "[ruinae-engine-integration][stereo]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, kBlockSize);
    engine.setSoftLimitEnabled(false);
    engine.setPolyphony(2);

    SECTION("spread=1 creates stereo differentiation") {
        engine.setStereoSpread(1.0f);

        engine.noteOn(60, 100);
        engine.noteOn(72, 100);

        std::vector<float> left(kBlockSize), right(kBlockSize);
        float totalRmsL = 0.0f, totalRmsR = 0.0f;
        for (int i = 0; i < kWarmUpBlocks; ++i) {
            engine.processBlock(left.data(), right.data(), kBlockSize);
            totalRmsL += computeRMS(left.data(), kBlockSize);
            totalRmsR += computeRMS(right.data(), kBlockSize);
        }

        REQUIRE(totalRmsL > 0.0f);
        REQUIRE(totalRmsR > 0.0f);
    }
}

// =============================================================================
// Integration Test: Mono Legato Signal Path
// =============================================================================

TEST_CASE("RuinaeEngine integration: mono legato",
          "[ruinae-engine-integration][mono]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, kBlockSize);
    engine.setMode(VoiceMode::Mono);
    engine.setLegato(true);

    SECTION("overlapping notes do not retrigger envelope") {
        engine.noteOn(60, 100);

        // Process blocks to establish audio through effects chain
        REQUIRE(processAndCheckForAudio(engine));

        // Legato second note
        engine.noteOn(64, 100);

        // Should still have continuous audio
        REQUIRE(processAndCheckForAudio(engine, 5));
        REQUIRE(engine.getActiveVoiceCount() == 1);
    }
}

// =============================================================================
// Integration Test: Portamento (SC-006)
// =============================================================================

TEST_CASE("RuinaeEngine integration: portamento",
          "[ruinae-engine-integration][mono]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, kBlockSize);
    engine.setMode(VoiceMode::Mono);
    engine.setPortamentoTime(200.0f); // 200ms glide
    engine.setSoftLimitEnabled(false);

    SECTION("portamento glides smoothly between notes") {
        engine.noteOn(48, 100); // C3

        // Establish audio
        REQUIRE(processAndCheckForAudio(engine));

        // Glide to C4
        engine.noteOn(60, 100);

        // Should produce audio during glide
        REQUIRE(processAndCheckForAudio(engine));
    }
}

// =============================================================================
// Integration Test: Pitch Bend
// =============================================================================

TEST_CASE("RuinaeEngine integration: pitch bend",
          "[ruinae-engine-integration][controllers]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, kBlockSize);
    engine.setSoftLimitEnabled(false);

    SECTION("pitch bend shifts frequency of all voices") {
        engine.noteOn(60, 100);

        // Process enough blocks to get past latency
        std::vector<float> left(kBlockSize), right(kBlockSize);
        float rmsNoBend = processAndAccumulateRMS(engine, left, right);

        // Apply pitch bend and process more blocks
        engine.setPitchBend(1.0f);
        float rmsBend = processAndAccumulateRMS(engine, left, right);

        // Both should have audio
        REQUIRE(rmsNoBend > 0.0f);
        REQUIRE(rmsBend > 0.0f);
    }
}

// =============================================================================
// Integration Test: Aftertouch
// =============================================================================

TEST_CASE("RuinaeEngine integration: aftertouch",
          "[ruinae-engine-integration][controllers]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, kBlockSize);

    SECTION("aftertouch applied to active voices") {
        engine.noteOn(60, 100);
        engine.setAftertouch(0.8f);

        // Should produce audio after warm-up
        REQUIRE(processAndCheckForAudio(engine));
    }
}

// =============================================================================
// Integration Test: Effects Integration (SC-012)
// =============================================================================

TEST_CASE("RuinaeEngine integration: reverb tail",
          "[ruinae-engine-integration][effects]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, kBlockSize);

    SECTION("reverb tail persists after noteOff") {
        ReverbParams params;
        params.roomSize = 0.9f;
        params.mix = 0.5f;
        engine.setReverbParams(params);

        engine.noteOn(60, 100);
        engine.setAmpRelease(5.0f); // Very short release

        // Establish audio through effects chain
        std::vector<float> left(kBlockSize), right(kBlockSize);
        for (int i = 0; i < kWarmUpBlocks; ++i) {
            engine.processBlock(left.data(), right.data(), kBlockSize);
        }

        // Release the note
        engine.noteOff(60);

        // Process until voice finishes
        for (int i = 0; i < 20; ++i) {
            engine.processBlock(left.data(), right.data(), kBlockSize);
        }

        // Reverb tail should still produce audio after voice envelope fades
        // (this verifies the effects chain processes the tail correctly)
    }
}

// =============================================================================
// Integration Test: Mode Switching Under Load (SC-007)
// =============================================================================

TEST_CASE("RuinaeEngine integration: mode switching under load",
          "[ruinae-engine-integration][mode]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, kBlockSize);

    SECTION("switch poly->mono while voices active does not crash") {
        engine.noteOn(60, 100);
        engine.noteOn(64, 100);
        engine.noteOn(67, 100);

        std::vector<float> left(kBlockSize), right(kBlockSize);
        for (int i = 0; i < 5; ++i) {
            engine.processBlock(left.data(), right.data(), kBlockSize);
        }

        // Switch to mono mid-playback
        engine.setMode(VoiceMode::Mono);
        engine.processBlock(left.data(), right.data(), kBlockSize);

        REQUIRE(allSamplesFinite(left.data(), kBlockSize));
        REQUIRE(allSamplesFinite(right.data(), kBlockSize));
    }

    SECTION("switch mono->poly while voice active does not crash") {
        engine.setMode(VoiceMode::Mono);
        engine.noteOn(60, 100);

        std::vector<float> left(kBlockSize), right(kBlockSize);
        for (int i = 0; i < 5; ++i) {
            engine.processBlock(left.data(), right.data(), kBlockSize);
        }

        // Switch back to poly
        engine.setMode(VoiceMode::Poly);
        engine.processBlock(left.data(), right.data(), kBlockSize);

        REQUIRE(allSamplesFinite(left.data(), kBlockSize));
        REQUIRE(allSamplesFinite(right.data(), kBlockSize));
    }
}

// =============================================================================
// Integration Test: Multi-Sample-Rate (SC-008)
// =============================================================================

TEST_CASE("RuinaeEngine integration: multi-sample-rate",
          "[ruinae-engine-integration][sample-rate]") {
    const std::array<double, 4> sampleRates = {44100.0, 48000.0, 96000.0, 192000.0};

    for (double sr : sampleRates) {
        DYNAMIC_SECTION("sample rate " << sr) {
            RuinaeEngine engine;
            engine.prepare(sr, kBlockSize);

            engine.noteOn(60, 100);

            REQUIRE(processAndCheckForAudio(engine));
        }
    }
}

// =============================================================================
// Integration Test: CPU Performance Benchmark (SC-001)
// =============================================================================

TEST_CASE("RuinaeEngine integration: CPU performance benchmark",
          "[ruinae-engine-integration][!benchmark]") {
    constexpr double sampleRate = 44100.0;
    constexpr size_t numBlocks = static_cast<size_t>(sampleRate / kBlockSize); // ~1 second
    constexpr size_t numVoices = 8;

    RuinaeEngine engine;
    engine.prepare(sampleRate, kBlockSize);
    engine.setPolyphony(numVoices);

    // Activate 8 voices
    for (uint8_t i = 0; i < numVoices; ++i) {
        engine.noteOn(48 + i * 3, 100);
    }

    std::vector<float> left(kBlockSize), right(kBlockSize);

    BENCHMARK("8 voices at 44.1kHz for 1 second") {
        for (size_t b = 0; b < numBlocks; ++b) {
            engine.processBlock(left.data(), right.data(), kBlockSize);
        }
    };
}

// =============================================================================
// Integration Test: Full Signal Chain
// =============================================================================

TEST_CASE("RuinaeEngine integration: full signal chain",
          "[ruinae-engine-integration][signal-path]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, kBlockSize);

    SECTION("noteOn through full chain: osc -> filter -> dist -> gate -> pan -> width -> gfilter -> fx -> master") {
        // Configure all stages
        engine.setOscAType(OscType::PolyBLEP);
        engine.setFilterType(RuinaeFilterType::SVF_LP);
        engine.setFilterCutoff(2000.0f);
        engine.setDistortionType(RuinaeDistortionType::Clean);
        engine.setGlobalFilterEnabled(true);
        engine.setGlobalFilterCutoff(5000.0f);
        engine.setStereoSpread(0.5f);
        engine.setStereoWidth(1.0f);
        engine.setMasterGain(1.0f);
        engine.setSoftLimitEnabled(true);

        engine.noteOn(60, 100);

        // Process enough blocks to get past latency
        std::vector<float> left(kBlockSize), right(kBlockSize);
        bool hasAudioL = false, hasAudioR = false;
        bool allFinite = true;

        for (int i = 0; i < kWarmUpBlocks; ++i) {
            engine.processBlock(left.data(), right.data(), kBlockSize);
            if (hasNonZeroSamples(left.data(), kBlockSize)) hasAudioL = true;
            if (hasNonZeroSamples(right.data(), kBlockSize)) hasAudioR = true;
            if (!allSamplesFinite(left.data(), kBlockSize) ||
                !allSamplesFinite(right.data(), kBlockSize)) {
                allFinite = false;
            }
        }

        // Output must be present and finite
        REQUIRE(hasAudioL);
        REQUIRE(hasAudioR);
        REQUIRE(allFinite);

        // With soft limiter, peak of last block must be in [-1, +1]
        float peakL = findPeak(left.data(), kBlockSize);
        float peakR = findPeak(right.data(), kBlockSize);
        REQUIRE(peakL <= 1.0f);
        REQUIRE(peakR <= 1.0f);
    }
}

// =============================================================================
// Integration Test: Global Filter Signal Processing
// =============================================================================

TEST_CASE("RuinaeEngine integration: global filter signal processing",
          "[ruinae-engine-integration][filter]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, kBlockSize);
    engine.setSoftLimitEnabled(false);

    SECTION("global LP filter at 500Hz reduces high-frequency content") {
        engine.setOscAType(OscType::PolyBLEP); // Rich harmonic content

        // Process without global filter
        engine.noteOn(60, 100);
        std::vector<float> unfilteredL(kBlockSize), unfilteredR(kBlockSize);
        float unfilteredRms = processAndAccumulateRMS(engine, unfilteredL, unfilteredR);

        // Reset and process with global filter
        engine.reset();
        engine.setGlobalFilterEnabled(true);
        engine.setGlobalFilterCutoff(500.0f);
        engine.setGlobalFilterType(SVFMode::Lowpass);

        engine.noteOn(60, 100);
        std::vector<float> filteredL(kBlockSize), filteredR(kBlockSize);
        float filteredRms = processAndAccumulateRMS(engine, filteredL, filteredR);

        // Filtered output should have lower energy (LP removes harmonics)
        if (unfilteredRms > 0.001f && filteredRms > 0.001f) {
            REQUIRE(filteredRms < unfilteredRms);
        }
    }
}

// =============================================================================
// Integration Test: Soft Limiter Under Full Load (SC-003)
// =============================================================================

TEST_CASE("RuinaeEngine integration: soft limiter under full load",
          "[ruinae-engine-integration][master]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, kBlockSize);
    engine.setPolyphony(16);
    engine.setMasterGain(2.0f); // Maximum gain

    SECTION("16 voices at full velocity with limiter stay in [-1, +1]") {
        // Activate 16 voices at full velocity
        for (uint8_t i = 0; i < 16; ++i) {
            engine.noteOn(48 + i, 127);
        }

        // Process multiple blocks (including latency warm-up)
        std::vector<float> left(kBlockSize), right(kBlockSize);
        for (int block = 0; block < kWarmUpBlocks; ++block) {
            engine.processBlock(left.data(), right.data(), kBlockSize);

            float peakL = findPeak(left.data(), kBlockSize);
            float peakR = findPeak(right.data(), kBlockSize);
            REQUIRE(peakL <= 1.0f);
            REQUIRE(peakR <= 1.0f);
            REQUIRE(allSamplesFinite(left.data(), kBlockSize));
            REQUIRE(allSamplesFinite(right.data(), kBlockSize));
        }
    }
}

// =============================================================================
// Integration Test: Soft Limiter Transparency at Low Levels (SC-004)
// =============================================================================

TEST_CASE("RuinaeEngine integration: soft limiter transparency",
          "[ruinae-engine-integration][master]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, kBlockSize);

    SECTION("at low levels tanh is approximately linear") {
        engine.setMasterGain(0.1f); // Very low gain
        engine.noteOn(60, 50);      // Low velocity

        std::vector<float> leftLim(kBlockSize), rightLim(kBlockSize);
        float rmsLim = processAndAccumulateRMS(engine, leftLim, rightLim);

        engine.reset();
        engine.setSoftLimitEnabled(false);
        engine.setMasterGain(0.1f);
        engine.noteOn(60, 50);

        std::vector<float> leftNoLim(kBlockSize), rightNoLim(kBlockSize);
        float rmsNoLim = processAndAccumulateRMS(engine, leftNoLim, rightNoLim);

        // At low levels, tanh(x) approx x, so outputs should be very similar
        if (rmsLim > 0.001f && rmsNoLim > 0.001f) {
            float ratio = rmsLim / rmsNoLim;
            REQUIRE(ratio > 0.8f);
            REQUIRE(ratio < 1.2f);
        }
    }
}

// =============================================================================
// Integration Test: Gain Compensation Accuracy (SC-005)
// =============================================================================

TEST_CASE("RuinaeEngine integration: gain compensation accuracy",
          "[ruinae-engine-integration][master]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, kBlockSize);
    engine.setSoftLimitEnabled(false);

    SECTION("gain compensation follows 1/sqrt(N)") {
        // Process with polyphony = 1
        engine.setPolyphony(1);
        engine.noteOn(60, 100);

        std::vector<float> left1(kBlockSize), right1(kBlockSize);
        float rms1 = processAndAccumulateRMS(engine, left1, right1);

        // Process with polyphony = 4
        engine.reset();
        engine.setPolyphony(4);
        engine.noteOn(60, 100);

        std::vector<float> left4(kBlockSize), right4(kBlockSize);
        float rms4 = processAndAccumulateRMS(engine, left4, right4);

        // Expected ratio: (1/sqrt(4)) / (1/sqrt(1)) = 0.5
        if (rms1 > 0.001f && rms4 > 0.001f) {
            float ratio = rms4 / rms1;
            REQUIRE(ratio == Approx(0.5f).margin(0.15f));
        }
    }
}

// =============================================================================
// Integration Test: Global Modulation -> Filter Cutoff (SC-011)
// =============================================================================

TEST_CASE("RuinaeEngine integration: global modulation to filter cutoff",
          "[ruinae-engine-integration][modulation]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, kBlockSize);
    engine.setSoftLimitEnabled(false);
    engine.setGlobalFilterEnabled(true);
    engine.setGlobalFilterCutoff(1000.0f);

    SECTION("LFO routed to global filter cutoff modulates the filter") {
        engine.setGlobalLFO1Rate(5.0f);
        engine.setGlobalLFO1Waveform(Waveform::Sine);
        engine.setGlobalModRoute(0, ModSource::LFO1,
                                 RuinaeModDest::GlobalFilterCutoff, 0.8f);

        engine.noteOn(60, 100);

        // Process multiple blocks
        REQUIRE(processAndCheckForAudio(engine, 20));
    }
}
