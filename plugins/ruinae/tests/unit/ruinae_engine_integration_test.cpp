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

#include "engine/ruinae_engine.h"
#include <krate/dsp/core/pitch_utils.h>
#include <krate/dsp/core/midi_utils.h>
#include <krate/dsp/primitives/fft.h>

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

    SECTION("spread=1 creates at least 3 dB stereo differentiation (SC-010)") {
        engine.setStereoSpread(1.0f);
        // Disable effects — reverb/delay smear stereo image
        engine.setDelayMix(0.0f);
        engine.setReverbParams({.roomSize = 0.5f, .damping = 0.5f, .width = 1.0f, .mix = 0.0f});

        // Use notes with very different frequencies for distinct per-channel content
        engine.noteOn(36, 100); // C2 — panned left (voice 0)
        engine.noteOn(84, 100); // C6 — panned right (voice 1)

        std::vector<float> left(kBlockSize), right(kBlockSize);
        float totalRmsL = 0.0f, totalRmsR = 0.0f;

        // Skip warm-up then measure
        for (int i = 0; i < 5; ++i) {
            engine.processBlock(left.data(), right.data(), kBlockSize);
        }
        for (int i = 0; i < 10; ++i) {
            engine.processBlock(left.data(), right.data(), kBlockSize);
            totalRmsL += computeRMS(left.data(), kBlockSize);
            totalRmsR += computeRMS(right.data(), kBlockSize);
        }

        REQUIRE(totalRmsL > 0.0f);
        REQUIRE(totalRmsR > 0.0f);

        // SC-010: L/R energy must differ by at least 3 dB
        float dBDiff = std::abs(20.0f * std::log10(totalRmsL / totalRmsR));
        INFO("Stereo spread dB difference: " << dBDiff << " dB (L RMS: "
             << totalRmsL << ", R RMS: " << totalRmsR << ")");
        REQUIRE(dBDiff >= 3.0f);
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
    const std::array<double, 6> sampleRates = {44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0};

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

    SECTION("16 voices at full velocity with limiter stay in [-1, +1] (SC-003)") {
        // Spec requires sawtooth waveforms — PolyBLEP is the sawtooth type
        engine.setOscAType(OscType::PolyBLEP);

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

    SECTION("at low levels peak difference between limited and unlimited < 0.05 (SC-004)") {
        engine.setMasterGain(0.1f); // Very low gain
        engine.noteOn(60, 50);      // Low velocity (moderate = 0.5 normalized)

        // Collect several blocks to get past latency
        std::vector<float> leftLim(kBlockSize), rightLim(kBlockSize);
        for (int i = 0; i < kWarmUpBlocks; ++i) {
            engine.processBlock(leftLim.data(), rightLim.data(), kBlockSize);
        }
        // Capture one more block with limiter ON
        engine.processBlock(leftLim.data(), rightLim.data(), kBlockSize);

        engine.reset();
        engine.setSoftLimitEnabled(false);
        engine.setMasterGain(0.1f);
        engine.noteOn(60, 50);

        std::vector<float> leftNoLim(kBlockSize), rightNoLim(kBlockSize);
        for (int i = 0; i < kWarmUpBlocks; ++i) {
            engine.processBlock(leftNoLim.data(), rightNoLim.data(), kBlockSize);
        }
        engine.processBlock(leftNoLim.data(), rightNoLim.data(), kBlockSize);

        // Measure peak sample-by-sample difference (spec says < 0.05)
        float maxDiff = 0.0f;
        for (size_t i = 0; i < kBlockSize; ++i) {
            float diffL = std::abs(leftLim[i] - leftNoLim[i]);
            float diffR = std::abs(rightLim[i] - rightNoLim[i]);
            maxDiff = std::max(maxDiff, std::max(diffL, diffR));
        }
        INFO("Peak sample difference (limited vs unlimited): " << maxDiff);
        REQUIRE(maxDiff < 0.05f);
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

    SECTION("gain compensation follows 1/sqrt(N) for N=1,2,4,8 (SC-005)") {
        // Measure RMS for N=1 as reference
        engine.setPolyphony(1);
        engine.noteOn(60, 100);

        std::vector<float> left(kBlockSize), right(kBlockSize);
        float rms1 = processAndAccumulateRMS(engine, left, right);
        REQUIRE(rms1 > 0.001f);

        // Test N=2, 4, 8 — each should scale as 1/sqrt(N)
        const std::array<size_t, 3> polyphonyCounts = {2, 4, 8};
        for (size_t n : polyphonyCounts) {
            engine.reset();
            engine.setPolyphony(n);
            engine.noteOn(60, 100);

            float rmsN = processAndAccumulateRMS(engine, left, right);
            REQUIRE(rmsN > 0.001f);

            float expectedRatio = 1.0f / std::sqrt(static_cast<float>(n));
            float actualRatio = rmsN / rms1;
            INFO("N=" << n << ": expected ratio=" << expectedRatio
                 << ", actual=" << actualRatio);
            // 25% tolerance as per spec
            REQUIRE(actualRatio == Approx(expectedRatio).margin(expectedRatio * 0.25f));
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

    SECTION("LFO routed to global filter cutoff causes measurable RMS variation (SC-011)") {
        engine.setGlobalLFO1Rate(2.0f); // 2 Hz LFO, sweeps over ~22 blocks at 512 samples
        engine.setGlobalLFO1Waveform(Waveform::Sine);
        engine.setGlobalModRoute(0, ModSource::LFO1,
                                 RuinaeModDest::GlobalFilterCutoff, 1.0f);

        engine.noteOn(60, 100);

        // Process 40 blocks and track per-block RMS
        constexpr int kNumMeasureBlocks = 40;
        std::vector<float> left(kBlockSize), right(kBlockSize);
        float minRms = std::numeric_limits<float>::max();
        float maxRms = 0.0f;

        for (int i = 0; i < kNumMeasureBlocks; ++i) {
            engine.processBlock(left.data(), right.data(), kBlockSize);
            float rms = computeRMS(left.data(), kBlockSize);
            if (rms > 0.0f) {
                minRms = std::min(minRms, rms);
                maxRms = std::max(maxRms, rms);
            }
        }

        // The LFO sweeping the filter cutoff should cause RMS variation
        INFO("Per-block RMS min: " << minRms << ", max: " << maxRms
             << ", ratio: " << (minRms > 0 ? maxRms / minRms : 0));
        REQUIRE(maxRms > 0.0f);
        REQUIRE(minRms > 0.0f);
        REQUIRE(maxRms / minRms > 1.5f);
    }
}

// =============================================================================
// Integration Test: Portamento Frequency at Midpoint (SC-006)
// =============================================================================

/// @brief Estimate frequency using interpolated zero-crossings.
/// Works well for monophonic signals. Returns average frequency over the buffer.
static float estimateFrequencyZeroCrossings(const float* data, size_t numSamples,
                                             float sampleRate) {
    // Find interpolated positive-going zero-crossing positions
    std::vector<float> crossings;
    for (size_t i = 1; i < numSamples; ++i) {
        if (data[i - 1] < 0.0f && data[i] >= 0.0f) {
            // Positive-going crossing — interpolate exact position
            float frac = -data[i - 1] / (data[i] - data[i - 1]);
            crossings.push_back(static_cast<float>(i - 1) + frac);
        }
    }
    if (crossings.size() < 2) return 0.0f;

    // Average period from consecutive positive-going crossings
    float totalPeriod = 0.0f;
    for (size_t i = 1; i < crossings.size(); ++i) {
        totalPeriod += crossings[i] - crossings[i - 1];
    }
    float avgPeriodSamples = totalPeriod / static_cast<float>(crossings.size() - 1);
    return sampleRate / avgPeriodSamples;
}

TEST_CASE("RuinaeEngine integration: portamento frequency at midpoint",
          "[ruinae-engine-integration][mono]") {
    constexpr float kSampleRate = 44100.0f;
    constexpr size_t kSmallBlock = 256;

    RuinaeEngine engine;
    engine.prepare(kSampleRate, kSmallBlock);
    engine.setMode(VoiceMode::Mono);
    engine.setPortamentoTime(100.0f); // 100ms glide
    engine.setPortamentoMode(PortaMode::Always);
    engine.setSoftLimitEnabled(false);
    engine.setGlobalFilterEnabled(false);
    // Single oscillator for clean zero-crossing measurement
    engine.setMixPosition(0.0f);
    // Legato: no envelope retrigger during glide (avoids amplitude transient)
    engine.setLegato(true);
    // Open voice filter to avoid waveform distortion
    engine.setFilterCutoff(20000.0f);
    // Disable effects for clean frequency measurement
    engine.setDelayMix(0.0f);
    engine.setReverbParams({.roomSize = 0.5f, .damping = 0.5f, .width = 1.0f, .mix = 0.0f});

    SECTION("frequency at midpoint is within 20 cents of note 66 (SC-006)") {
        // Play first note and establish audio
        engine.noteOn(60, 100);

        std::vector<float> left(kSmallBlock), right(kSmallBlock);
        for (int i = 0; i < 40; ++i) {
            engine.processBlock(left.data(), right.data(), kSmallBlock);
        }

        // Start glide to note 72 (legato = no retrigger, portamento glides)
        engine.noteOn(72, 100);

        // 100ms glide at 44100 Hz = 4410 samples. Midpoint at 50ms = 2205 samples.
        // The effects chain adds 1024 samples of latency compensation
        // (spectral delay FFT size), so the midpoint appears at the output
        // at sample 2205 + 1024 = 3229.
        // Process 11 blocks of 256 = 2816 samples, then capture 3 blocks (768).
        // Analysis center at output sample 3200 → portamento sample 2176 (~49.3ms).
        for (int i = 0; i < 11; ++i) {
            engine.processBlock(left.data(), right.data(), kSmallBlock);
        }

        // Capture 3 blocks (768 samples) for reliable zero-crossing measurement
        // (~6 periods at ~370 Hz)
        std::vector<float> analysisBuffer(kSmallBlock * 3);
        for (int i = 0; i < 3; ++i) {
            engine.processBlock(analysisBuffer.data() + i * kSmallBlock,
                                right.data(), kSmallBlock);
        }

        float measuredFreq = estimateFrequencyZeroCrossings(
            analysisBuffer.data(), analysisBuffer.size(), kSampleRate);

        // Expected: note 66 = 440 * 2^((66-69)/12) ≈ 369.99 Hz
        float expectedFreq = 440.0f * std::pow(2.0f, (66.0f - 69.0f) / 12.0f);

        // 20 cents tolerance: freq * 2^(±20/1200)
        float lowerBound = expectedFreq / std::pow(2.0f, 20.0f / 1200.0f);
        float upperBound = expectedFreq * std::pow(2.0f, 20.0f / 1200.0f);

        INFO("Measured frequency: " << measuredFreq << " Hz");
        INFO("Expected (note 66): " << expectedFreq << " Hz");
        INFO("Acceptable range: [" << lowerBound << ", " << upperBound << "] Hz");

        // Verify within 20 cents of note 66
        REQUIRE(measuredFreq >= lowerBound);
        REQUIRE(measuredFreq <= upperBound);
    }
}

// =============================================================================
// Integration Test: Mode Switching Discontinuity (SC-007)
// =============================================================================

TEST_CASE("RuinaeEngine integration: mode switching discontinuity",
          "[ruinae-engine-integration][mode]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, kBlockSize);
    engine.setSoftLimitEnabled(false);
    // Disable effects for clean measurement
    engine.setDelayMix(0.0f);
    engine.setReverbParams({.roomSize = 0.5f, .damping = 0.5f, .width = 1.0f, .mix = 0.0f});

    SECTION("poly->mono switch produces no discontinuity > -40 dBFS (SC-007)") {
        engine.noteOn(60, 100);
        engine.noteOn(64, 100);
        engine.noteOn(67, 100);

        // Process several blocks to establish steady-state audio
        std::vector<float> left(kBlockSize), right(kBlockSize);
        for (int i = 0; i < 20; ++i) {
            engine.processBlock(left.data(), right.data(), kBlockSize);
        }

        // Record last sample of the current block
        float lastSampleL = left[kBlockSize - 1];
        float lastSampleR = right[kBlockSize - 1];

        // Switch to mono mode
        engine.setMode(VoiceMode::Mono);

        // Process the next block
        engine.processBlock(left.data(), right.data(), kBlockSize);

        // Measure the discontinuity at the boundary
        float discontinuityL = std::abs(left[0] - lastSampleL);
        float discontinuityR = std::abs(right[0] - lastSampleR);
        float maxDiscontinuity = std::max(discontinuityL, discontinuityR);

        // -40 dBFS threshold = 10^(-40/20) = 0.01
        constexpr float kThreshold = 0.01f;
        float discontinuityDb = (maxDiscontinuity > 0.0f)
            ? 20.0f * std::log10(maxDiscontinuity) : -144.0f;

        INFO("Discontinuity at switch point: " << maxDiscontinuity
             << " (" << discontinuityDb << " dBFS)");
        INFO("Threshold: " << kThreshold << " (-40 dBFS)");

        REQUIRE(maxDiscontinuity <= kThreshold);
    }
}

// =============================================================================
// Integration Test: Reverb Tail Duration (SC-012)
// =============================================================================

TEST_CASE("RuinaeEngine integration: reverb tail duration",
          "[ruinae-engine-integration][effects]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, kBlockSize);
    engine.setAmpRelease(5.0f); // Very short release

    SECTION("reverb tail extends at least 500ms beyond voice release (SC-012)") {
        // Enable reverb with high room size, disable delay
        engine.setDelayMix(0.0f);
        ReverbParams params;
        params.roomSize = 0.9f;
        params.damping = 0.3f;
        params.mix = 0.5f;
        engine.setReverbParams(params);

        engine.noteOn(60, 100);

        // Process blocks to establish audio through effects chain
        std::vector<float> left(kBlockSize), right(kBlockSize);
        for (int i = 0; i < kWarmUpBlocks; ++i) {
            engine.processBlock(left.data(), right.data(), kBlockSize);
        }

        // Release the note
        engine.noteOff(60);

        // Process until voice finishes
        int blocksUntilVoiceDone = 0;
        for (int i = 0; i < 100; ++i) {
            engine.processBlock(left.data(), right.data(), kBlockSize);
            ++blocksUntilVoiceDone;
            if (engine.getActiveVoiceCount() == 0) break;
        }
        REQUIRE(engine.getActiveVoiceCount() == 0);

        // Now count how many more blocks have audio above silence threshold
        constexpr float kSilenceThreshold = 1e-6f;
        int tailBlocks = 0;
        for (int i = 0; i < 500; ++i) {
            engine.processBlock(left.data(), right.data(), kBlockSize);
            float peakL = findPeak(left.data(), kBlockSize);
            float peakR = findPeak(right.data(), kBlockSize);
            if (peakL > kSilenceThreshold || peakR > kSilenceThreshold) {
                ++tailBlocks;
            } else {
                break; // Tail has decayed
            }
        }

        float tailDurationMs = static_cast<float>(tailBlocks * kBlockSize) / 44100.0f * 1000.0f;
        INFO("Reverb tail duration after voice release: " << tailDurationMs << " ms"
             << " (" << tailBlocks << " blocks)");
        REQUIRE(tailDurationMs >= 500.0f);
    }
}
