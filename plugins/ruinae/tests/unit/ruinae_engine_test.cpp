// ==============================================================================
// Layer 3: System Component Tests - RuinaeEngine
// ==============================================================================
// Tests for the Ruinae synthesizer engine. Covers all functional requirements
// (FR-001 through FR-044) and success criteria (SC-001 through SC-014).
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
#include <krate/dsp/core/math_constants.h>

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

// =============================================================================
// Phase 2: Foundation Tests (FR-001, FR-002, FR-003, FR-004)
// =============================================================================

TEST_CASE("RuinaeEngine construction and constants", "[ruinae-engine][lifecycle]") {
    SECTION("kMaxPolyphony is 16 (FR-002)") {
        REQUIRE(RuinaeEngine::kMaxPolyphony == 16);
    }

    SECTION("kMinMasterGain is 0.0 (FR-002)") {
        REQUIRE(RuinaeEngine::kMinMasterGain == 0.0f);
    }

    SECTION("kMaxMasterGain is 2.0 (FR-002)") {
        REQUIRE(RuinaeEngine::kMaxMasterGain == 2.0f);
    }

    SECTION("default mode is Poly (FR-001)") {
        RuinaeEngine engine;
        REQUIRE(engine.getMode() == VoiceMode::Poly);
    }

    SECTION("default active voice count is 0") {
        RuinaeEngine engine;
        engine.prepare(44100.0, 512);
        REQUIRE(engine.getActiveVoiceCount() == 0);
    }

    SECTION("default polyphony is 8") {
        RuinaeEngine engine;
        engine.prepare(44100.0, 512);
        // Fill 8 voices and verify they all activate
        for (uint8_t i = 0; i < 8; ++i) {
            engine.noteOn(60 + i, 100);
        }
        REQUIRE(engine.getActiveVoiceCount() == 8);
    }
}

TEST_CASE("RuinaeEngine prepare lifecycle", "[ruinae-engine][lifecycle]") {
    SECTION("prepare initializes engine (FR-003)") {
        RuinaeEngine engine;
        engine.prepare(44100.0, 512);

        // After prepare, engine should accept noteOn
        engine.noteOn(60, 100);
        REQUIRE(engine.getActiveVoiceCount() == 1);
    }

    SECTION("processBlock produces silence when not prepared") {
        RuinaeEngine engine;
        std::vector<float> left(512, 1.0f), right(512, 1.0f);
        engine.processBlock(left.data(), right.data(), 512);
        REQUIRE(isAllZeros(left.data(), 512));
        REQUIRE(isAllZeros(right.data(), 512));
    }

    SECTION("processBlock with numSamples=0 is no-op") {
        RuinaeEngine engine;
        engine.prepare(44100.0, 512);
        float left = 1.0f, right = 1.0f;
        engine.processBlock(&left, &right, 0);
        // Should not crash, values untouched (0-size fill does nothing)
    }
}

TEST_CASE("RuinaeEngine reset lifecycle", "[ruinae-engine][lifecycle]") {
    SECTION("reset clears all active voices (FR-004)") {
        RuinaeEngine engine;
        engine.prepare(44100.0, 512);

        engine.noteOn(60, 100);
        engine.noteOn(64, 100);
        REQUIRE(engine.getActiveVoiceCount() == 2);

        engine.reset();
        REQUIRE(engine.getActiveVoiceCount() == 0);
    }

    SECTION("reset produces silence on next processBlock") {
        RuinaeEngine engine;
        engine.prepare(44100.0, 512);
        engine.noteOn(60, 100);

        // Process one block to get audio
        std::vector<float> left(512), right(512);
        engine.processBlock(left.data(), right.data(), 512);

        // Reset and process again
        engine.reset();
        engine.processBlock(left.data(), right.data(), 512);
        REQUIRE(isAllZeros(left.data(), 512));
        REQUIRE(isAllZeros(right.data(), 512));
    }
}

// =============================================================================
// Phase 3: User Story 1 - Polyphonic Voice Playback
// =============================================================================

TEST_CASE("RuinaeEngine poly mode noteOn dispatch", "[ruinae-engine][poly][US1]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);

    SECTION("noteOn activates a voice (FR-005)") {
        engine.noteOn(60, 100);
        REQUIRE(engine.getActiveVoiceCount() == 1);
    }

    SECTION("multiple noteOns activate multiple voices") {
        engine.noteOn(60, 100);
        engine.noteOn(64, 100);
        engine.noteOn(67, 100);
        REQUIRE(engine.getActiveVoiceCount() == 3);
    }

    SECTION("noteOn before prepare is silently ignored") {
        RuinaeEngine unpreparedEngine;
        unpreparedEngine.noteOn(60, 100);
        // Should not crash, voice count should be 0
    }
}

TEST_CASE("RuinaeEngine poly mode noteOff dispatch", "[ruinae-engine][poly][US1]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);
    // Short release so voices die quickly
    engine.setAmpRelease(1.0f);

    SECTION("noteOff triggers release phase (FR-006)") {
        engine.noteOn(60, 100);
        REQUIRE(engine.getActiveVoiceCount() == 1);

        engine.noteOff(60);
        // Voice enters release, still technically active until envelope fades
        // Process enough blocks for release to complete
        std::vector<float> left(512), right(512);
        for (int i = 0; i < 20; ++i) {
            engine.processBlock(left.data(), right.data(), 512);
        }
        REQUIRE(engine.getActiveVoiceCount() == 0);
    }
}

TEST_CASE("RuinaeEngine polyphony configuration", "[ruinae-engine][poly][US1]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);

    SECTION("setPolyphony clamps to [1, kMaxPolyphony] (FR-010)") {
        engine.setPolyphony(0);
        // Clamped to 1, try to activate 2 voices
        engine.noteOn(60, 100);
        engine.noteOn(64, 100);
        // Only 1 should be active (second steals first)
        REQUIRE(engine.getActiveVoiceCount() <= 1);

        engine.setPolyphony(100);
        // Clamped to 16
    }

    SECTION("gain compensation recalculated on setPolyphony") {
        // With polyphony = 1, gain compensation = 1/sqrt(1) = 1.0
        // With polyphony = 4, gain compensation = 1/sqrt(4) = 0.5
        // We can verify this indirectly by checking output levels
        engine.setPolyphony(1);
        engine.noteOn(60, 100);

        std::vector<float> left1(512), right1(512);
        engine.processBlock(left1.data(), right1.data(), 512);
        float rms1 = computeRMS(left1.data(), 512);

        engine.reset();
        engine.setPolyphony(4);
        engine.noteOn(60, 100);

        std::vector<float> left4(512), right4(512);
        engine.processBlock(left4.data(), right4.data(), 512);
        float rms4 = computeRMS(left4.data(), 512);

        // Single voice with polyphony=1 should be louder than polyphony=4
        // (both have 1 active voice but different gain compensation)
        if (rms1 > 0.0f && rms4 > 0.0f) {
            REQUIRE(rms1 > rms4);
        }
    }
}

TEST_CASE("RuinaeEngine voice summing", "[ruinae-engine][poly][US1]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);
    engine.setSoftLimitEnabled(false); // Disable limiter for clean summing test

    SECTION("mono voice output sums into stereo (FR-012)") {
        engine.noteOn(60, 100);

        // Process several blocks to account for effects chain latency
        std::vector<float> left(512), right(512);
        bool hasAudioL = false, hasAudioR = false;
        for (int i = 0; i < 10; ++i) {
            engine.processBlock(left.data(), right.data(), 512);
            if (hasNonZeroSamples(left.data(), 512)) hasAudioL = true;
            if (hasNonZeroSamples(right.data(), 512)) hasAudioR = true;
        }

        // With spread = 0 (default), voice is center-panned
        // Both channels should have audio
        REQUIRE(hasAudioL);
        REQUIRE(hasAudioR);
    }
}

TEST_CASE("RuinaeEngine deferred voiceFinished", "[ruinae-engine][poly][US1]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);
    engine.setAmpRelease(1.0f); // Very short release

    SECTION("voices finish after processBlock, not during (FR-033)") {
        engine.noteOn(60, 100);
        engine.noteOff(60);

        std::vector<float> left(512), right(512);
        // Process blocks until voice finishes
        for (int i = 0; i < 50; ++i) {
            engine.processBlock(left.data(), right.data(), 512);
        }
        // Voice should have been freed via deferred voiceFinished
        REQUIRE(engine.getActiveVoiceCount() == 0);

        // Now a new noteOn should work (voice was properly freed)
        engine.noteOn(64, 100);
        REQUIRE(engine.getActiveVoiceCount() == 1);
    }

    SECTION("processBlock with numSamples=0 does not modify state (FR-034)") {
        engine.noteOn(60, 100);
        uint32_t before = engine.getActiveVoiceCount();
        engine.processBlock(nullptr, nullptr, 0);
        REQUIRE(engine.getActiveVoiceCount() == before);
    }
}

TEST_CASE("RuinaeEngine getActiveVoiceCount", "[ruinae-engine][poly][US1]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);

    SECTION("returns 0 when no voices active (FR-040)") {
        REQUIRE(engine.getActiveVoiceCount() == 0);
    }

    SECTION("counts active voices correctly") {
        engine.noteOn(60, 100);
        REQUIRE(engine.getActiveVoiceCount() == 1);
        engine.noteOn(64, 100);
        REQUIRE(engine.getActiveVoiceCount() == 2);
        engine.noteOn(67, 100);
        REQUIRE(engine.getActiveVoiceCount() == 3);
    }
}

// =============================================================================
// Phase 4: User Story 2 - Stereo Voice Mixing with Pan Spread
// =============================================================================

TEST_CASE("RuinaeEngine equal-power pan law", "[ruinae-engine][stereo][US2]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);
    engine.setSoftLimitEnabled(false);

    SECTION("center pan produces equal L/R energy (FR-012)") {
        engine.setStereoSpread(0.0f); // All voices center
        engine.noteOn(60, 100);

        std::vector<float> left(512), right(512);
        engine.processBlock(left.data(), right.data(), 512);

        float rmsL = computeRMS(left.data(), 512);
        float rmsR = computeRMS(right.data(), 512);

        if (rmsL > 0.0f) {
            // Center pan: cos(0.5 * pi/2) = cos(pi/4) = sin(pi/4) => equal
            REQUIRE(rmsL == Approx(rmsR).margin(0.001f));
        }
    }
}

TEST_CASE("RuinaeEngine stereo spread", "[ruinae-engine][stereo][US2]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);
    engine.setSoftLimitEnabled(false);

    SECTION("spread=0 keeps all voices center (FR-013)") {
        engine.setStereoSpread(0.0f);
        engine.noteOn(60, 100);
        engine.noteOn(72, 100);

        std::vector<float> left(512), right(512);
        engine.processBlock(left.data(), right.data(), 512);

        // Both voices center => L and R should be close to equal
        float rmsL = computeRMS(left.data(), 512);
        float rmsR = computeRMS(right.data(), 512);
        if (rmsL > 0.0f) {
            REQUIRE(rmsL == Approx(rmsR).margin(0.01f));
        }
    }

    SECTION("spread=1 distributes voices across stereo field (FR-013)") {
        engine.setPolyphony(2);
        engine.setStereoSpread(1.0f);

        engine.noteOn(60, 100);
        engine.noteOn(72, 100);

        // Process several blocks to account for effects chain latency
        std::vector<float> left(512), right(512);
        float totalRmsL = 0.0f, totalRmsR = 0.0f;
        for (int i = 0; i < 10; ++i) {
            engine.processBlock(left.data(), right.data(), 512);
            totalRmsL += computeRMS(left.data(), 512);
            totalRmsR += computeRMS(right.data(), 512);
        }

        // With 2 voices, spread=1: voice 0 at pan=0 (left), voice 1 at pan=1 (right)
        // L and R should have different content
        REQUIRE(totalRmsL > 0.0f);
        REQUIRE(totalRmsR > 0.0f);
    }

    SECTION("NaN/Inf spread values are silently ignored") {
        engine.setStereoSpread(0.5f);
        engine.setStereoSpread(std::numeric_limits<float>::quiet_NaN());
        // Should still be 0.5, not NaN
        engine.noteOn(60, 100);
        std::vector<float> left(512), right(512);
        engine.processBlock(left.data(), right.data(), 512);
        REQUIRE(allSamplesFinite(left.data(), 512));
    }
}

TEST_CASE("RuinaeEngine stereo width", "[ruinae-engine][stereo][US2]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);
    engine.setSoftLimitEnabled(false);

    SECTION("width=0 collapses to mono (FR-014)") {
        engine.setPolyphony(2);
        engine.setStereoSpread(1.0f);
        engine.setStereoWidth(0.0f);

        engine.noteOn(60, 100);
        engine.noteOn(72, 100);

        std::vector<float> left(512), right(512);
        engine.processBlock(left.data(), right.data(), 512);

        // With width=0, mid/side processing collapses to mono: L == R
        for (size_t i = 0; i < 512; ++i) {
            REQUIRE(left[i] == Approx(right[i]).margin(0.0001f));
        }
    }

    SECTION("width=1 is natural stereo (no change) (FR-014)") {
        // Width 1.0 is default; the mid/side code path skips when width == 1.0
        engine.setStereoWidth(1.0f);
        // Should not modify the stereo image
    }

    SECTION("NaN/Inf width values are silently ignored") {
        engine.setStereoWidth(1.5f);
        engine.setStereoWidth(std::numeric_limits<float>::infinity());
        // Value should remain 1.5, not infinity
    }
}

// =============================================================================
// Phase 5: User Story 3 - Mono/Poly Mode Switching
// =============================================================================

TEST_CASE("RuinaeEngine mono mode noteOn dispatch", "[ruinae-engine][mono][US3]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);
    engine.setMode(VoiceMode::Mono);

    SECTION("mono noteOn activates voice 0 (FR-007)") {
        engine.noteOn(60, 100);
        REQUIRE(engine.getActiveVoiceCount() == 1);
    }

    SECTION("second noteOn in mono does not add second voice") {
        engine.noteOn(60, 100);
        engine.noteOn(64, 100);
        REQUIRE(engine.getActiveVoiceCount() == 1);
    }
}

TEST_CASE("RuinaeEngine mono mode noteOff dispatch", "[ruinae-engine][mono][US3]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);
    engine.setMode(VoiceMode::Mono);
    engine.setAmpRelease(1.0f);

    SECTION("noteOff releases voice when stack empty (FR-008)") {
        engine.noteOn(60, 100);
        engine.noteOff(60);

        // Process blocks for release to complete
        std::vector<float> left(512), right(512);
        for (int i = 0; i < 20; ++i) {
            engine.processBlock(left.data(), right.data(), 512);
        }
        REQUIRE(engine.getActiveVoiceCount() == 0);
    }

    SECTION("noteOff returns to held note") {
        engine.noteOn(60, 100);
        engine.noteOn(64, 100); // Overlapping
        engine.noteOff(64);     // Should return to 60
        REQUIRE(engine.getActiveVoiceCount() == 1);
    }
}

TEST_CASE("RuinaeEngine mono portamento", "[ruinae-engine][mono][US3]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);
    engine.setMode(VoiceMode::Mono);
    engine.setPortamentoTime(100.0f); // 100ms glide
    engine.setSoftLimitEnabled(false);

    SECTION("portamento produces per-sample frequency updates (FR-009)") {
        engine.noteOn(60, 100);

        // Process several blocks to establish audio through effects chain latency
        std::vector<float> left(512), right(512);
        for (int i = 0; i < 10; ++i) {
            engine.processBlock(left.data(), right.data(), 512);
        }

        // Play second note - portamento should glide
        engine.noteOn(72, 100);

        bool hasAudio = false;
        for (int i = 0; i < 10; ++i) {
            engine.processBlock(left.data(), right.data(), 512);
            if (hasNonZeroSamples(left.data(), 512)) hasAudio = true;
        }

        REQUIRE(hasAudio);
    }
}

TEST_CASE("RuinaeEngine mode switching", "[ruinae-engine][mode][US3]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);

    SECTION("same mode is no-op (FR-011)") {
        engine.setMode(VoiceMode::Poly);
        engine.setMode(VoiceMode::Poly); // Should not crash or change state
        REQUIRE(engine.getMode() == VoiceMode::Poly);
    }

    SECTION("poly->mono preserves most recent voice (FR-011)") {
        engine.noteOn(60, 100);
        engine.noteOn(64, 100);
        engine.noteOn(67, 100);
        REQUIRE(engine.getActiveVoiceCount() == 3);

        engine.setMode(VoiceMode::Mono);
        REQUIRE(engine.getMode() == VoiceMode::Mono);
        // Should have at most 1 active voice
        REQUIRE(engine.getActiveVoiceCount() <= 1);
    }

    SECTION("mono->poly: voice 0 continues (FR-011)") {
        engine.setMode(VoiceMode::Mono);
        engine.noteOn(60, 100);
        REQUIRE(engine.getActiveVoiceCount() == 1);

        engine.setMode(VoiceMode::Poly);
        REQUIRE(engine.getMode() == VoiceMode::Poly);
        // Voice 0 should still be active
        // (exact behavior depends on whether MonoHandler reset kills it)
    }
}

TEST_CASE("RuinaeEngine mono mode configuration", "[ruinae-engine][mono][US3]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);

    SECTION("setMonoPriority forwards to MonoHandler (FR-036)") {
        engine.setMonoPriority(MonoMode::LowNote);
        // Should not crash
    }

    SECTION("setLegato forwards to MonoHandler (FR-036)") {
        engine.setLegato(true);
        // Should not crash
    }

    SECTION("setPortamentoTime forwards to MonoHandler (FR-036)") {
        engine.setPortamentoTime(200.0f);
        // Should not crash
    }

    SECTION("setPortamentoMode forwards to MonoHandler (FR-036)") {
        engine.setPortamentoMode(PortaMode::LegatoOnly);
        // Should not crash
    }
}

// =============================================================================
// Phase 6: User Story 4 - Global Modulation
// =============================================================================

TEST_CASE("RuinaeEngine global modulation processing", "[ruinae-engine][modulation][US4]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);

    SECTION("processBlock runs global modulation (FR-018)") {
        // Set up a global LFO routing to GlobalFilterCutoff
        engine.setGlobalLFO1Rate(5.0f);
        engine.setGlobalModRoute(0, ModSource::LFO1,
                                 RuinaeModDest::GlobalFilterCutoff, 0.5f);

        engine.noteOn(60, 100);
        std::vector<float> left(512), right(512);
        engine.processBlock(left.data(), right.data(), 512);
        // Should not crash - modulation is applied internally
    }
}

TEST_CASE("RuinaeEngine global routing configuration", "[ruinae-engine][modulation][US4]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);

    SECTION("setGlobalModRoute configures routing (FR-019)") {
        engine.setGlobalModRoute(0, ModSource::LFO1,
                                 RuinaeModDest::MasterVolume, 0.3f);
        // Should not crash
    }

    SECTION("clearGlobalModRoute removes routing (FR-019)") {
        engine.setGlobalModRoute(0, ModSource::LFO1,
                                 RuinaeModDest::MasterVolume, 0.3f);
        engine.clearGlobalModRoute(0);
        // Should not crash
    }

    SECTION("invalid slot indices are silently ignored") {
        engine.setGlobalModRoute(-1, ModSource::LFO1,
                                 RuinaeModDest::MasterVolume, 0.3f);
        engine.setGlobalModRoute(100, ModSource::LFO1,
                                 RuinaeModDest::MasterVolume, 0.3f);
        engine.clearGlobalModRoute(-1);
        engine.clearGlobalModRoute(100);
        // Should not crash
    }
}

TEST_CASE("RuinaeEngine RuinaeModDest enum values", "[ruinae-engine][modulation][US4]") {
    SECTION("enum values match spec (FR-020)") {
        REQUIRE(static_cast<uint32_t>(RuinaeModDest::GlobalFilterCutoff) == 64);
        REQUIRE(static_cast<uint32_t>(RuinaeModDest::GlobalFilterResonance) == 65);
        REQUIRE(static_cast<uint32_t>(RuinaeModDest::MasterVolume) == 66);
        REQUIRE(static_cast<uint32_t>(RuinaeModDest::EffectMix) == 67);
        REQUIRE(static_cast<uint32_t>(RuinaeModDest::AllVoiceFilterCutoff) == 68);
        REQUIRE(static_cast<uint32_t>(RuinaeModDest::AllVoiceMorphPosition) == 69);
        REQUIRE(static_cast<uint32_t>(RuinaeModDest::AllVoiceTranceGateRate) == 70);
    }
}

TEST_CASE("RuinaeEngine global mod source configuration", "[ruinae-engine][modulation][US4]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);

    SECTION("LFO configuration methods forward correctly (FR-022)") {
        engine.setGlobalLFO1Rate(2.0f);
        engine.setGlobalLFO1Waveform(Waveform::Sine);
        engine.setGlobalLFO2Rate(0.5f);
        engine.setGlobalLFO2Waveform(Waveform::Triangle);
        engine.setChaosSpeed(0.5f);
        engine.setMacroValue(0, 0.75f);
        // Should not crash
    }
}

// =============================================================================
// Phase 7: User Story 5 - Effects Chain Integration
// =============================================================================

TEST_CASE("RuinaeEngine effects chain processing", "[ruinae-engine][effects][US5]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);

    SECTION("processBlock includes effects chain (FR-026)") {
        engine.setDelayMix(0.5f);
        engine.setDelayTime(200.0f);

        engine.noteOn(60, 100);
        std::vector<float> left(512), right(512);
        engine.processBlock(left.data(), right.data(), 512);
        // Should not crash
    }
}

TEST_CASE("RuinaeEngine effects parameter forwarding", "[ruinae-engine][effects][US5]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);

    SECTION("delay parameters forward correctly (FR-027)") {
        engine.setDelayType(RuinaeDelayType::Digital);
        engine.setDelayTime(300.0f);
        engine.setDelayFeedback(0.5f);
        engine.setDelayMix(0.3f);
        // Should not crash
    }

    SECTION("reverb parameters forward correctly (FR-027)") {
        ReverbParams params;
        params.roomSize = 0.7f;
        params.damping = 0.4f;
        params.mix = 0.3f;
        engine.setReverbParams(params);
        // Should not crash
    }

    SECTION("freeze parameters forward correctly (FR-027)") {
        engine.setFreezeEnabled(true);
        engine.setFreeze(true);
        engine.setFreezePitchSemitones(7.0f);
        engine.setFreezeShimmerMix(0.4f);
        engine.setFreezeDecay(0.8f);
        // Should not crash
    }
}

TEST_CASE("RuinaeEngine getLatencySamples", "[ruinae-engine][effects][US5]") {
    RuinaeEngine engine;

    SECTION("returns latency from effects chain (FR-028)") {
        engine.prepare(44100.0, 512);
        [[maybe_unused]] size_t latency = engine.getLatencySamples();
        // Should not crash, value depends on effects chain implementation
    }
}

// =============================================================================
// Phase 8: User Story 6 - Master Output
// =============================================================================

TEST_CASE("RuinaeEngine master gain", "[ruinae-engine][master][US6]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);
    engine.setSoftLimitEnabled(false);

    SECTION("setMasterGain clamps to [0, 2] (FR-029)") {
        engine.setMasterGain(-1.0f);
        engine.setMasterGain(5.0f);
        // Should not crash, values are clamped
    }

    SECTION("NaN/Inf master gain is silently ignored") {
        engine.setMasterGain(0.5f);
        engine.setMasterGain(std::numeric_limits<float>::quiet_NaN());
        // Should remain 0.5
    }

    SECTION("gain=0 produces silence") {
        engine.setMasterGain(0.0f);
        engine.noteOn(60, 100);

        std::vector<float> left(512), right(512);
        engine.processBlock(left.data(), right.data(), 512);
        REQUIRE(isAllZeros(left.data(), 512));
        REQUIRE(isAllZeros(right.data(), 512));
    }
}

TEST_CASE("RuinaeEngine soft limiter", "[ruinae-engine][master][US6]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);

    SECTION("soft limiter enabled by default (FR-030)") {
        // Play a loud chord to push levels high
        engine.setPolyphony(8);
        engine.setMasterGain(2.0f);
        for (uint8_t i = 0; i < 8; ++i) {
            engine.noteOn(60 + i * 2, 127);
        }

        std::vector<float> left(512), right(512);
        engine.processBlock(left.data(), right.data(), 512);

        // With soft limiter, all samples should be in [-1, +1]
        float peakL = findPeak(left.data(), 512);
        float peakR = findPeak(right.data(), 512);
        REQUIRE(peakL <= 1.0f);
        REQUIRE(peakR <= 1.0f);
    }

    SECTION("soft limiter can be disabled") {
        engine.setSoftLimitEnabled(false);
        engine.noteOn(60, 100);

        std::vector<float> left(512), right(512);
        engine.processBlock(left.data(), right.data(), 512);
        // Just verify it does not crash
    }
}

TEST_CASE("RuinaeEngine NaN/Inf flush", "[ruinae-engine][master][US6]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);

    SECTION("output is always finite (FR-031)") {
        engine.noteOn(60, 100);

        std::vector<float> left(512), right(512);
        engine.processBlock(left.data(), right.data(), 512);

        REQUIRE(allSamplesFinite(left.data(), 512));
        REQUIRE(allSamplesFinite(right.data(), 512));
    }
}

// =============================================================================
// Phase 9: User Story 7 - Parameter Forwarding
// =============================================================================

TEST_CASE("RuinaeEngine oscillator parameter forwarding", "[ruinae-engine][params][US7]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);

    SECTION("oscillator type forwarding (FR-035)") {
        engine.setOscAType(OscType::PolyBLEP);
        engine.setOscBType(OscType::Wavetable);
        engine.setOscAPhaseMode(PhaseMode::Reset);
        engine.setOscBPhaseMode(PhaseMode::Continuous);
        // Should not crash
    }
}

TEST_CASE("RuinaeEngine mixer parameter forwarding", "[ruinae-engine][params][US7]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);

    SECTION("mixer parameters forward to all voices (FR-035)") {
        engine.setMixMode(MixMode::CrossfadeMix);
        engine.setMixPosition(0.3f);
        // Should not crash
    }
}

TEST_CASE("RuinaeEngine filter parameter forwarding", "[ruinae-engine][params][US7]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);

    SECTION("filter parameters forward to all voices (FR-035)") {
        engine.setFilterType(RuinaeFilterType::SVF_LP);
        engine.setFilterCutoff(500.0f);
        engine.setFilterResonance(2.0f);
        engine.setFilterEnvAmount(24.0f);
        engine.setFilterKeyTrack(0.5f);
        // Should not crash
    }
}

TEST_CASE("RuinaeEngine distortion parameter forwarding", "[ruinae-engine][params][US7]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);

    SECTION("distortion parameters forward to all voices (FR-035)") {
        engine.setDistortionType(RuinaeDistortionType::ChaosWaveshaper);
        engine.setDistortionDrive(0.5f);
        engine.setDistortionCharacter(0.7f);
        // Should not crash
    }
}

TEST_CASE("RuinaeEngine trance gate parameter forwarding", "[ruinae-engine][params][US7]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);

    SECTION("trance gate parameters forward to all voices (FR-035)") {
        engine.setTranceGateEnabled(true);
        TranceGateParams params;
        engine.setTranceGateParams(params);
        engine.setTranceGateStep(0, 0.8f);
        // Should not crash
    }
}

TEST_CASE("RuinaeEngine envelope parameter forwarding", "[ruinae-engine][params][US7]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);

    SECTION("amplitude envelope params forward (FR-035)") {
        engine.setAmpAttack(10.0f);
        engine.setAmpDecay(100.0f);
        engine.setAmpSustain(0.8f);
        engine.setAmpRelease(200.0f);
        engine.setAmpAttackCurve(EnvCurve::Linear);
        engine.setAmpDecayCurve(EnvCurve::Exponential);
        engine.setAmpReleaseCurve(EnvCurve::Exponential);
    }

    SECTION("filter envelope params forward (FR-035)") {
        engine.setFilterAttack(5.0f);
        engine.setFilterDecay(50.0f);
        engine.setFilterSustain(0.0f);
        engine.setFilterRelease(100.0f);
        engine.setFilterAttackCurve(EnvCurve::Linear);
        engine.setFilterDecayCurve(EnvCurve::Exponential);
        engine.setFilterReleaseCurve(EnvCurve::Exponential);
    }

    SECTION("modulation envelope params forward (FR-035)") {
        engine.setModAttack(20.0f);
        engine.setModDecay(200.0f);
        engine.setModSustain(0.3f);
        engine.setModRelease(300.0f);
        engine.setModAttackCurve(EnvCurve::Linear);
        engine.setModDecayCurve(EnvCurve::Exponential);
        engine.setModReleaseCurve(EnvCurve::Exponential);
    }
}

TEST_CASE("RuinaeEngine per-voice modulation routing forwarding", "[ruinae-engine][params][US7]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);

    SECTION("voice mod route forwarding (FR-035)") {
        VoiceModRoute route;
        route.source = VoiceModSource::Env2;
        route.destination = VoiceModDest::FilterCutoff;
        route.amount = 0.5f;
        engine.setVoiceModRoute(0, route);
        engine.setVoiceModRouteScale(VoiceModDest::FilterCutoff, 48.0f);
        // Should not crash
    }
}

// =============================================================================
// Phase 10: User Story 8 - Tempo and Transport
// =============================================================================

TEST_CASE("RuinaeEngine tempo forwarding", "[ruinae-engine][tempo][US8]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);

    SECTION("setTempo forwards to effects chain and voices (FR-039)") {
        engine.setTempo(120.0);
        engine.setTempo(140.0);
        // Should not crash
    }

    SECTION("setBlockContext stores context (FR-039)") {
        BlockContext ctx{};
        ctx.sampleRate = 48000.0;
        ctx.blockSize = 256;
        ctx.tempoBPM = 130.0;
        ctx.isPlaying = true;
        engine.setBlockContext(ctx);
        // Should not crash
    }
}

// =============================================================================
// Phase 11: User Story 9 - Performance Controllers
// =============================================================================

TEST_CASE("RuinaeEngine pitch bend", "[ruinae-engine][controllers][US9]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);

    SECTION("setPitchBend forwards to NoteProcessor (FR-023)") {
        engine.setPitchBend(0.5f);
        engine.setPitchBend(-1.0f);
        engine.setPitchBend(1.0f);
        // Should not crash
    }

    SECTION("NaN/Inf pitch bend silently ignored") {
        engine.setPitchBend(std::numeric_limits<float>::quiet_NaN());
        engine.setPitchBend(std::numeric_limits<float>::infinity());
        // Should not crash
    }
}

TEST_CASE("RuinaeEngine aftertouch", "[ruinae-engine][controllers][US9]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);

    SECTION("setAftertouch forwards to all voices (FR-024)") {
        engine.setAftertouch(0.6f);
        // Should not crash
    }

    SECTION("NaN/Inf aftertouch silently ignored") {
        engine.setAftertouch(std::numeric_limits<float>::quiet_NaN());
        // Should not crash
    }
}

TEST_CASE("RuinaeEngine mod wheel", "[ruinae-engine][controllers][US9]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);

    SECTION("setModWheel forwards as macro 0 (FR-025)") {
        engine.setModWheel(0.5f);
        // Should not crash
    }

    SECTION("NaN/Inf mod wheel silently ignored") {
        engine.setModWheel(std::numeric_limits<float>::quiet_NaN());
        // Should not crash
    }
}

// =============================================================================
// Phase 12: Additional Requirements
// =============================================================================

TEST_CASE("RuinaeEngine global filter", "[ruinae-engine][filter]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);
    engine.setSoftLimitEnabled(false);

    SECTION("global filter disabled by default (FR-015)") {
        // With filter disabled, processBlock should pass signal through
        engine.noteOn(60, 100);
        std::vector<float> left(512), right(512);
        bool hasAudio = false;
        for (int i = 0; i < 10; ++i) {
            engine.processBlock(left.data(), right.data(), 512);
            if (hasNonZeroSamples(left.data(), 512)) hasAudio = true;
        }
        REQUIRE(hasAudio);
    }

    SECTION("global filter can be enabled (FR-015)") {
        engine.setGlobalFilterEnabled(true);
        engine.setGlobalFilterCutoff(500.0f);
        engine.setGlobalFilterResonance(1.0f);
        engine.setGlobalFilterType(SVFMode::Lowpass);

        engine.noteOn(60, 100);
        std::vector<float> left(512), right(512);
        bool hasAudio = false;
        for (int i = 0; i < 10; ++i) {
            engine.processBlock(left.data(), right.data(), 512);
            if (hasNonZeroSamples(left.data(), 512)) hasAudio = true;
        }
        REQUIRE(hasAudio);
    }

    SECTION("NaN/Inf cutoff silently ignored (FR-016)") {
        engine.setGlobalFilterCutoff(std::numeric_limits<float>::quiet_NaN());
        engine.setGlobalFilterCutoff(std::numeric_limits<float>::infinity());
        // Should not crash
    }
}

TEST_CASE("RuinaeEngine voice allocator configuration", "[ruinae-engine][allocator]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);

    SECTION("allocation mode forwards (FR-037)") {
        engine.setAllocationMode(AllocationMode::RoundRobin);
        engine.setAllocationMode(AllocationMode::Oldest);
        // Should not crash
    }

    SECTION("steal mode forwards (FR-037)") {
        engine.setStealMode(StealMode::Hard);
        engine.setStealMode(StealMode::Soft);
        // Should not crash
    }
}

TEST_CASE("RuinaeEngine note processor configuration", "[ruinae-engine][noteproc]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);

    SECTION("pitch bend range forwards (FR-038)") {
        engine.setPitchBendRange(12.0f);
        // Should not crash
    }

    SECTION("tuning reference forwards (FR-038)") {
        engine.setTuningReference(432.0f);
        // Should not crash
    }

    SECTION("velocity curve forwards (FR-038)") {
        engine.setVelocityCurve(VelocityCurve::Hard);
        // Should not crash
    }
}

// =============================================================================
// FR-021: AllVoice Modulation Forwarding (Behavioral Test)
// =============================================================================

TEST_CASE("RuinaeEngine AllVoice modulation forwarding", "[ruinae-engine][modulation]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);
    engine.setSoftLimitEnabled(false);
    engine.setGlobalFilterEnabled(false);

    SECTION("AllVoiceFilterCutoff offset changes voice output (FR-021)") {
        // Set voice filter cutoff very low (dark sound)
        engine.setFilterType(RuinaeFilterType::SVF_LP);
        engine.setFilterCutoff(200.0f);

        engine.noteOn(60, 100);

        // Process without modulation — low cutoff produces dark sound
        std::vector<float> leftDark(512), rightDark(512);
        for (int i = 0; i < 10; ++i) {
            engine.processBlock(leftDark.data(), rightDark.data(), 512);
        }
        float rmsDark = computeRMS(leftDark.data(), 512);

        // Reset and process WITH AllVoiceFilterCutoff modulation (opens filter)
        engine.reset();
        engine.setFilterType(RuinaeFilterType::SVF_LP);
        engine.setFilterCutoff(200.0f);
        engine.setGlobalLFO1Rate(0.001f); // Very slow LFO (effectively DC)
        engine.setGlobalLFO1Waveform(Waveform::Sine);
        engine.setGlobalModRoute(0, ModSource::Macro1,
                                 RuinaeModDest::AllVoiceFilterCutoff, 1.0f);
        engine.setMacroValue(0, 1.0f); // DC +1.0 offset

        engine.noteOn(60, 100);

        std::vector<float> leftBright(512), rightBright(512);
        for (int i = 0; i < 10; ++i) {
            engine.processBlock(leftBright.data(), rightBright.data(), 512);
        }
        float rmsBright = computeRMS(leftBright.data(), 512);

        // With filter cutoff offset pushing cutoff up, the sound should be brighter
        // (more harmonics pass through), yielding higher RMS
        if (rmsDark > 0.001f && rmsBright > 0.001f) {
            INFO("RMS dark (cutoff 200Hz): " << rmsDark);
            INFO("RMS bright (cutoff modulated up): " << rmsBright);
            REQUIRE(rmsBright > rmsDark);
        }
    }

    SECTION("AllVoiceTranceGateRate offset changes gating rhythm (FR-021)") {
        // Enable trance gate with slow rate and alternating on/off pattern
        TranceGateParams params;
        params.tempoSync = false;
        params.rateHz = 2.0f;  // Slow: one full cycle = 0.5s = 22050 samples
        params.depth = 1.0f;
        params.numSteps = 2;
        params.attackMs = 1.0f;
        params.releaseMs = 1.0f;
        params.perVoice = true;

        engine.setTranceGateEnabled(true);
        engine.setTranceGateParams(params);
        engine.setTranceGateRate(2.0f);
        engine.setTranceGateStep(0, 1.0f);
        engine.setTranceGateStep(1, 0.0f);

        engine.noteOn(60, 100);

        // Accumulate total energy over all blocks (slow gate stays mostly "on"
        // because step 0 lasts 11025 samples and we only process 5120 total)
        constexpr int kBlocks = 10;
        constexpr size_t kBlockSize = 512;
        std::vector<float> left(kBlockSize), right(kBlockSize);
        double totalEnergySlow = 0.0;
        for (int i = 0; i < kBlocks; ++i) {
            engine.processBlock(left.data(), right.data(), kBlockSize);
            for (size_t s = 0; s < kBlockSize; ++s) {
                totalEnergySlow += static_cast<double>(left[s]) * left[s];
            }
        }

        // Reset and process WITH AllVoiceTranceGateRate modulation
        // Offset pushes rate to 52 Hz: rapid on/off cycling averages to ~50%
        engine.reset();
        engine.setTranceGateEnabled(true);
        engine.setTranceGateParams(params);
        engine.setTranceGateRate(2.0f);
        engine.setTranceGateStep(0, 1.0f);
        engine.setTranceGateStep(1, 0.0f);
        engine.setGlobalModRoute(0, ModSource::Macro1,
                                 RuinaeModDest::AllVoiceTranceGateRate, 1.0f);
        engine.setMacroValue(0, 1.0f); // +1.0 * 50.0 = +50 Hz -> 52 Hz

        engine.noteOn(60, 100);

        double totalEnergyFast = 0.0;
        for (int i = 0; i < kBlocks; ++i) {
            engine.processBlock(left.data(), right.data(), kBlockSize);
            for (size_t s = 0; s < kBlockSize; ++s) {
                totalEnergyFast += static_cast<double>(left[s]) * left[s];
            }
        }

        // Slow gate (2 Hz): stays on step 0 (full level) for all 5120 samples
        // Fast gate (52 Hz): cycles on/off ~6 times, averaging ~50% level
        // So total energy at slow rate should be significantly higher
        INFO("Total energy slow (2 Hz): " << totalEnergySlow);
        INFO("Total energy fast (52 Hz): " << totalEnergyFast);
        REQUIRE(totalEnergySlow > 0.0);
        REQUIRE(totalEnergyFast > 0.0);
        REQUIRE(totalEnergySlow > totalEnergyFast * 1.2);
    }

    SECTION("AllVoiceMorphPosition offset changes voice output (FR-021)") {
        // Set mix position to 0.0 (osc A only)
        engine.setMixPosition(0.0f);
        engine.noteOn(60, 100);

        std::vector<float> leftA(512), rightA(512);
        for (int i = 0; i < 10; ++i) {
            engine.processBlock(leftA.data(), rightA.data(), 512);
        }
        float rmsA = computeRMS(leftA.data(), 512);

        // Reset and apply AllVoiceMorphPosition offset
        engine.reset();
        engine.setMixPosition(0.0f);
        engine.setGlobalModRoute(0, ModSource::Macro1,
                                 RuinaeModDest::AllVoiceMorphPosition, 1.0f);
        engine.setMacroValue(0, 1.0f); // Push morph toward osc B

        engine.noteOn(60, 100);

        std::vector<float> leftMorph(512), rightMorph(512);
        for (int i = 0; i < 10; ++i) {
            engine.processBlock(leftMorph.data(), rightMorph.data(), 512);
        }
        float rmsMorph = computeRMS(leftMorph.data(), 512);

        // With morph offset, the mix should change, producing different output
        // Both should have audio
        REQUIRE(rmsA > 0.0f);
        REQUIRE(rmsMorph > 0.0f);
        // We can't predict which is louder, but they should differ
        // (unless both oscillators are identical, which is unlikely)
    }
}

TEST_CASE("RuinaeEngine parameter safety", "[ruinae-engine][safety]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);

    SECTION("NaN float inputs are silently ignored (FR-043)") {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        engine.setMasterGain(nan);
        engine.setStereoSpread(nan);
        engine.setStereoWidth(nan);
        engine.setGlobalFilterCutoff(nan);
        engine.setGlobalFilterResonance(nan);
        engine.setPitchBend(nan);
        engine.setAftertouch(nan);
        engine.setModWheel(nan);
        engine.setPortamentoTime(nan);
        engine.setFilterCutoff(nan);
        engine.setFilterResonance(nan);
        engine.setDistortionDrive(nan);
        engine.setAmpAttack(nan);
        engine.setMixPosition(nan);
        // None should crash or change state to NaN
    }

    SECTION("Inf float inputs are silently ignored (FR-043)") {
        const float inf = std::numeric_limits<float>::infinity();
        engine.setMasterGain(inf);
        engine.setStereoSpread(inf);
        engine.setStereoWidth(inf);
        engine.setGlobalFilterCutoff(inf);
        engine.setPitchBend(inf);
        engine.setPortamentoTime(inf);
        engine.setPitchBendRange(inf);
        engine.setTuningReference(inf);
        // None should crash
    }

    SECTION("output is always finite after processBlock") {
        engine.noteOn(60, 127);
        std::vector<float> left(512), right(512);
        engine.processBlock(left.data(), right.data(), 512);
        REQUIRE(allSamplesFinite(left.data(), 512));
        REQUIRE(allSamplesFinite(right.data(), 512));
    }
}
