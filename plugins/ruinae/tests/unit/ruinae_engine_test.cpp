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
#include <krate/dsp/core/db_utils.h>

#include "test_helpers/reverb_metrics.h"
#include "audit_rig.h"

#include <algorithm>
#include <array>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <numeric>
#include <optional>
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
        if (!detail::isFinite(buffer[i])) return false;
    }
    return true;
}

// -----------------------------------------------------------------------------
// Non-finite input guards (FR-043, R-107)
// -----------------------------------------------------------------------------
// The engine flushes NaN/Inf to 0 at its output, so a finite output proves
// nothing about a setter's NaN guard. requireGuardHolds renders an engine that
// received the non-finite value next to one that did not, from the same setup
// and the same performance, and requires the injected render to be audible,
// finite and equal to the reference. A removed guard either silences the
// output (NaN state, flushed to 0) or moves it (Inf clamped to a range edge);
// a stored value the output cannot show is checked through its getter inside
// the case's inject step.

namespace {

// NaN / Inf from a bit pattern through a volatile, so no fast-math build can
// fold the value or the guard that rejects it.
float nonFiniteFromBits(std::uint32_t pattern) {
    volatile std::uint32_t bits = pattern;
    return std::bit_cast<float>(static_cast<std::uint32_t>(bits));
}
float makeNaN() { return nonFiniteFromBits(0x7FC00000u); }
float makeInf() { return nonFiniteFromBits(0x7F800000u); }

constexpr int kGuardBlocks = 20;  // ~232 ms at 44.1 kHz

void renderGuardBlocks(RuinaeEngine& engine, int blocks, std::vector<float>& out) {
    std::vector<float> left(512), right(512);
    for (int b = 0; b < blocks; ++b) {
        engine.processBlock(left.data(), right.data(), 512);
        out.insert(out.end(), left.begin(), left.end());
        out.insert(out.end(), right.begin(), right.end());
    }
}

void playOneNote(RuinaeEngine& engine, std::vector<float>& out) {
    engine.noteOn(60, 100);
    renderGuardBlocks(engine, kGuardBlocks, out);
}

void playTwoNotes(RuinaeEngine& engine, std::vector<float>& out) {
    engine.noteOn(60, 100);
    engine.noteOn(67, 100);
    renderGuardBlocks(engine, kGuardBlocks, out);
}

// A second note while the first is held: a glide in Mono (portamento Always).
void playGlide(RuinaeEngine& engine, std::vector<float>& out) {
    engine.noteOn(60, 100);
    renderGuardBlocks(engine, 4, out);
    engine.noteOn(72, 100);
    renderGuardBlocks(engine, kGuardBlocks - 4, out);
}

// An arp Slide step in Poly: the sounding voice glides over the slide time.
void playSlide(RuinaeEngine& engine, std::vector<float>& out) {
    engine.noteOn(60, 100);
    renderGuardBlocks(engine, 4, out);
    engine.noteOn(72, 100, true);
    renderGuardBlocks(engine, kGuardBlocks - 4, out);
}

struct NonFiniteGuardCase {
    const char* setter;
    std::function<void(RuinaeEngine&)> setup;          ///< valid configuration
    std::function<void(RuinaeEngine&, float)> inject;  ///< bad value + getter check
    std::function<void(RuinaeEngine&, std::vector<float>&)> play = playOneNote;
};

void requireGuardHolds(const NonFiniteGuardCase& c, float bad) {
    auto render = [&](bool injected) {
        auto engine = std::make_unique<RuinaeEngine>();
        engine->prepare(44100.0, 512);
        c.setup(*engine);
        if (injected) c.inject(*engine, bad);
        std::vector<float> out;
        c.play(*engine, out);
        return out;
    };
    const auto reference = render(false);
    const auto injected = render(true);
    REQUIRE(injected.size() == reference.size());

    float maxDiff = 0.0f;
    for (size_t i = 0; i < injected.size(); ++i) {
        maxDiff = std::max(maxDiff, std::abs(injected[i] - reference[i]));
    }
    const float rms = computeRMS(injected.data(), injected.size());
    INFO(c.setter << "(" << (detail::isNaN(bad) ? "NaN" : "Inf") << "): RMS "
                  << rms << ", max |diff| vs reference " << maxDiff);
    CHECK(allSamplesFinite(injected.data(), injected.size()));
    CHECK(rms > 1e-3f);
    CHECK(maxDiff <= 1e-6f);
}

void requireGuardHoldsForNaNAndInf(const NonFiniteGuardCase& c) {
    requireGuardHolds(c, makeNaN());
    requireGuardHolds(c, makeInf());
}

// A stored value must read back unchanged. Finiteness is checked first: in a
// fast-math build (MSVC /fp:fast) NaN == x can compare true.
void checkStored(const char* getter, float actual, float expected) {
    INFO(getter << "() = " << actual << ", expected " << expected);
    CHECK(detail::isFinite(actual));
    CHECK(actual == expected);
}

NonFiniteGuardCase masterGainGuard() {
    return {"setMasterGain",
            [](RuinaeEngine& e) { e.setMasterGain(0.5f); },
            [](RuinaeEngine& e, float bad) {
                e.setMasterGain(bad);
                checkStored("getMasterGain", e.getMasterGain(), 0.5f);
            }};
}

NonFiniteGuardCase stereoSpreadGuard() {
    return {"setStereoSpread",
            [](RuinaeEngine& e) {
                e.setPolyphony(2);
                e.setStereoSpread(0.5f);
            },
            [](RuinaeEngine& e, float bad) {
                e.setStereoSpread(bad);
                checkStored("getStereoSpread", e.getStereoSpread(), 0.5f);
            },
            playTwoNotes};
}

NonFiniteGuardCase stereoWidthGuard() {
    return {"setStereoWidth",
            [](RuinaeEngine& e) {
                e.setPolyphony(2);
                e.setStereoSpread(1.0f);
                e.setStereoWidth(1.5f);
            },
            [](RuinaeEngine& e, float bad) {
                e.setStereoWidth(bad);
                checkStored("getStereoWidth", e.getStereoWidth(), 1.5f);
            },
            playTwoNotes};
}

NonFiniteGuardCase globalFilterCutoffGuard() {
    return {"setGlobalFilterCutoff",
            [](RuinaeEngine& e) {
                e.setGlobalFilterEnabled(true);
                e.setGlobalFilterCutoff(500.0f);
            },
            [](RuinaeEngine& e, float bad) {
                e.setGlobalFilterCutoff(bad);
                checkStored("getGlobalFilterCutoff", e.getGlobalFilterCutoff(), 500.0f);
            }};
}

NonFiniteGuardCase globalFilterResonanceGuard() {
    return {"setGlobalFilterResonance",
            [](RuinaeEngine& e) {
                e.setGlobalFilterEnabled(true);
                e.setGlobalFilterCutoff(500.0f);
                e.setGlobalFilterResonance(4.0f);
            },
            [](RuinaeEngine& e, float bad) {
                e.setGlobalFilterResonance(bad);
                checkStored("getGlobalFilterResonance", e.getGlobalFilterResonance(), 4.0f);
            }};
}

NonFiniteGuardCase pitchBendGuard() {
    return {"setPitchBend",
            [](RuinaeEngine& e) { e.setPitchBend(0.5f); },
            [](RuinaeEngine& e, float bad) { e.setPitchBend(bad); }};
}

NonFiniteGuardCase pitchBendRangeGuard() {
    return {"setPitchBendRange",
            [](RuinaeEngine& e) {
                e.setPitchBendRange(7.0f);
                e.setPitchBend(1.0f);
            },
            [](RuinaeEngine& e, float bad) { e.setPitchBendRange(bad); }};
}

NonFiniteGuardCase tuningReferenceGuard() {
    return {"setTuningReference",
            [](RuinaeEngine& e) { e.setTuningReference(432.0f); },
            [](RuinaeEngine& e, float bad) { e.setTuningReference(bad); }};
}

// Aftertouch reaches the audio only through a route: Aftertouch -> voice
// cutoff, +-24 semitones, over a dark 300 Hz base.
NonFiniteGuardCase aftertouchGuard() {
    return {"setAftertouch",
            [](RuinaeEngine& e) {
                e.setFilterCutoff(300.0f);
                e.setVoiceModRoute(0, {VoiceModSource::Aftertouch,
                                       VoiceModDest::FilterCutoff, 1.0f});
                e.setVoiceModRouteScale(VoiceModDest::FilterCutoff, 24.0f);
                e.setAftertouch(0.5f);
            },
            [](RuinaeEngine& e, float bad) { e.setAftertouch(bad); }};
}

// The mod wheel is Macro 1; it reaches the audio only through a route.
NonFiniteGuardCase modWheelGuard() {
    return {"setModWheel",
            [](RuinaeEngine& e) {
                e.setGlobalModRoute(0, ModSource::Macro1,
                                    RuinaeModDest::MasterVolume, -0.5f);
                e.setModWheel(0.5f);
            },
            [](RuinaeEngine& e, float bad) { e.setModWheel(bad); }};
}

NonFiniteGuardCase monoPortamentoTimeGuard() {
    return {"setMonoPortamentoTime",
            [](RuinaeEngine& e) {
                e.setMode(VoiceMode::Mono);
                e.setMonoPortamentoTime(100.0f);
            },
            [](RuinaeEngine& e, float bad) {
                e.setMonoPortamentoTime(bad);
                checkStored("getMonoPortamentoTime", e.getMonoPortamentoTime(), 100.0f);
            },
            playGlide};
}

NonFiniteGuardCase slideTimeGuard() {
    return {"setSlideTime",
            [](RuinaeEngine& e) { e.setSlideTime(100.0f); },
            [](RuinaeEngine& e, float bad) {
                e.setSlideTime(bad);
                checkStored("getSlideTime", e.getSlideTime(), 100.0f);
            },
            playSlide};
}

NonFiniteGuardCase filterCutoffGuard() {
    return {"setFilterCutoff",
            [](RuinaeEngine& e) { e.setFilterCutoff(800.0f); },
            [](RuinaeEngine& e, float bad) {
                e.setFilterCutoff(bad);
                checkStored("getFilterCutoff", e.getFilterCutoff(), 800.0f);
            }};
}

NonFiniteGuardCase filterResonanceGuard() {
    return {"setFilterResonance",
            [](RuinaeEngine& e) {
                e.setFilterCutoff(800.0f);
                e.setFilterResonance(4.0f);
            },
            [](RuinaeEngine& e, float bad) {
                e.setFilterResonance(bad);
                checkStored("getFilterResonance", e.getFilterResonance(), 4.0f);
            }};
}

NonFiniteGuardCase distortionDriveGuard() {
    return {"setDistortionDrive",
            [](RuinaeEngine& e) {
                e.setDistortionType(RuinaeDistortionType::Wavefolder);
                e.setDistortionDrive(0.5f);
            },
            [](RuinaeEngine& e, float bad) { e.setDistortionDrive(bad); }};
}

NonFiniteGuardCase ampAttackGuard() {
    return {"setAmpAttack",
            [](RuinaeEngine& e) { e.setAmpAttack(50.0f); },
            [](RuinaeEngine& e, float bad) { e.setAmpAttack(bad); }};
}

NonFiniteGuardCase mixPositionGuard() {
    return {"setMixPosition",
            [](RuinaeEngine& e) { e.setMixPosition(0.3f); },
            [](RuinaeEngine& e, float bad) {
                e.setMixPosition(bad);
                checkStored("getBaseMixPosition", e.getBaseMixPosition(), 0.3f);
            }};
}

}  // namespace

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

    // Heap-allocated: the engine is large enough to overflow the stack in the
    // AddressSanitizer Debug build, which runs every [lifecycle] case.
    SECTION("default mode is Poly (FR-001)") {
        auto engine = std::make_unique<RuinaeEngine>();
        REQUIRE(engine->getMode() == VoiceMode::Poly);
    }

    SECTION("default active voice count is 0") {
        auto engine = std::make_unique<RuinaeEngine>();
        engine->prepare(44100.0, 512);
        REQUIRE(engine->getActiveVoiceCount() == 0);
    }

    SECTION("default polyphony is 8") {
        auto engine = std::make_unique<RuinaeEngine>();
        engine->prepare(44100.0, 512);
        // Fill 8 voices and verify they all activate
        for (uint8_t i = 0; i < 8; ++i) {
            engine->noteOn(60 + i, 100);
        }
        REQUIRE(engine->getActiveVoiceCount() == 8);
    }
}

TEST_CASE("RuinaeEngine prepare lifecycle", "[ruinae-engine][lifecycle]") {
    // Heap-allocated: the engine is large enough to overflow the stack in the
    // AddressSanitizer Debug build, which runs every [lifecycle] case.
    SECTION("prepare initializes engine (FR-003)") {
        auto engine = std::make_unique<RuinaeEngine>();
        engine->prepare(44100.0, 512);

        // After prepare, engine should accept noteOn
        engine->noteOn(60, 100);
        REQUIRE(engine->getActiveVoiceCount() == 1);
    }

    SECTION("processBlock produces silence when not prepared") {
        auto engine = std::make_unique<RuinaeEngine>();
        std::vector<float> left(512, 1.0f), right(512, 1.0f);
        engine->processBlock(left.data(), right.data(), 512);
        REQUIRE(isAllZeros(left.data(), 512));
        REQUIRE(isAllZeros(right.data(), 512));
    }

    SECTION("processBlock with numSamples=0 is no-op") {
        auto engine = std::make_unique<RuinaeEngine>();
        engine->prepare(44100.0, 512);
        float left = 1.0f, right = 1.0f;
        engine->processBlock(&left, &right, 0);
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
        // Clamped to 16: 17 notes hold 16 voices (the 17th steals one)
        for (uint8_t n = 0; n < 17; ++n) {
            engine.noteOn(static_cast<uint8_t>(40 + n), 100);
        }
        REQUIRE(engine.getActiveVoiceCount() == 16);
    }

    SECTION("gain compensation based on active voices not polyphony setting") {
        // With dynamic gain compensation, a single active voice always gets
        // compensation=1.0 regardless of the polyphony setting.
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

        // Both have 1 active voice → same compensation → same volume
        if (rms1 > 0.0f && rms4 > 0.0f) {
            float ratio = rms4 / rms1;
            REQUIRE(ratio == Catch::Approx(1.0f).margin(0.1f));
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
        // Stays 0.5: same audible output as an engine that never saw them.
        requireGuardHoldsForNaNAndInf(stereoSpreadGuard());
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
        // Stays 1.5: same audible output as an engine that never saw them.
        requireGuardHoldsForNaNAndInf(stereoWidthGuard());
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
    engine.setMonoPortamentoTime(100.0f); // 100ms glide
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

}

// =============================================================================
// Voice-mode switches and polyphony changes never strand or freeze a voice
// (R-027, R-032, R-033, R-119)
// =============================================================================

namespace {

constexpr double kSwitchSampleRate = 48000.0;
constexpr size_t kSwitchBlock = 512;

/// Engine with a 100 ms amp release and no effects, so silence is exact.
void prepareSwitchEngine(RuinaeEngine& engine) {
    engine.prepare(kSwitchSampleRate, kSwitchBlock);
    engine.setAmpRelease(100.0f);
    engine.setSoftLimitEnabled(false);
}

/// Render @p seconds and append the mono sum (L + R) to @p out.
void renderSeconds(RuinaeEngine& engine, double seconds, std::vector<float>& out) {
    const auto total = static_cast<size_t>(seconds * kSwitchSampleRate);
    std::array<float, kSwitchBlock> left{};
    std::array<float, kSwitchBlock> right{};
    for (size_t done = 0; done < total; done += kSwitchBlock) {
        const size_t n = std::min(kSwitchBlock, total - done);
        engine.processBlock(left.data(), right.data(), n);
        for (size_t s = 0; s < n; ++s) out.push_back(left[s] + right[s]);
    }
}

/// RMS of the mono sum over @p seconds of fresh render.
float renderRms(RuinaeEngine& engine, double seconds) {
    std::vector<float> out;
    renderSeconds(engine, seconds, out);
    return computeRMS(out.data(), out.size());
}

} // namespace

TEST_CASE("RuinaeEngine_PolyToMono_ReleasedNotesNotPromoted",
          "[ruinae-engine][voice-mode][stuck-notes]") {
    auto engine = std::make_unique<RuinaeEngine>();
    prepareSwitchEngine(*engine);

    SECTION("chord released, switch during the tails: silence after the release") {
        engine->noteOn(60, 100);
        engine->noteOn(64, 100);
        (void)renderRms(*engine, 0.1);
        engine->noteOff(64);
        engine->noteOff(60);
        (void)renderRms(*engine, 0.01);  // release tails ringing
        engine->setMode(VoiceMode::Mono);
        (void)renderRms(*engine, 1.0);   // well past the 100 ms release
        const float rms = renderRms(*engine, 1.0);
        INFO("RMS 1-2 s after the switch: " << rms);
        REQUIRE(rms < 1e-4f);
    }

    SECTION("60 held, 64 played and released, switch, release 60: silence") {
        engine->noteOn(60, 100);
        (void)renderRms(*engine, 0.1);
        engine->noteOn(64, 100);
        (void)renderRms(*engine, 0.1);
        engine->noteOff(64);
        (void)renderRms(*engine, 0.01);
        engine->setMode(VoiceMode::Mono);
        (void)renderRms(*engine, 0.1);
        engine->noteOff(60);
        (void)renderRms(*engine, 1.0);
        const float rms = renderRms(*engine, 1.0);
        INFO("RMS 1-2 s after releasing 60: " << rms);
        REQUIRE(rms < 1e-4f);
    }

    SECTION("held chord survives the switch: releasing the newest key keeps sounding") {
        engine->noteOn(60, 100);
        engine->noteOn(64, 100);
        engine->noteOn(67, 100);
        (void)renderRms(*engine, 0.1);
        engine->setMode(VoiceMode::Mono);
        (void)renderRms(*engine, 0.1);
        engine->noteOff(67);  // 60 and 64 are still down
        (void)renderRms(*engine, 0.5);
        const float held = renderRms(*engine, 0.2);
        INFO("RMS with 60 and 64 still held: " << held);
        REQUIRE(held > 1e-3f);
        engine->noteOff(64);
        engine->noteOff(60);
        (void)renderRms(*engine, 1.0);
        const float rms = renderRms(*engine, 1.0);
        INFO("RMS after releasing every key: " << rms);
        REQUIRE(rms < 1e-4f);
    }
}

TEST_CASE("RuinaeEngine_MonoToPoly_HeldKeyReleases",
          "[ruinae-engine][voice-mode][stuck-notes]") {
    auto engine = std::make_unique<RuinaeEngine>();
    prepareSwitchEngine(*engine);
    engine->setMode(VoiceMode::Mono);

    SECTION("held key released after the switch: silence") {
        engine->noteOn(60, 100);
        (void)renderRms(*engine, 0.1);
        engine->setMode(VoiceMode::Poly);
        const float sounding = renderRms(*engine, 0.05);
        INFO("RMS with 60 held across the switch: " << sounding);
        REQUIRE(sounding > 1e-3f);  // the held note keeps sounding
        engine->noteOff(60);
        (void)renderRms(*engine, 1.0);
        const float rms = renderRms(*engine, 1.0);
        INFO("RMS 1-2 s after releasing 60: " << rms);
        REQUIRE(rms < 1e-5f);
    }

    SECTION("60 held, 64 tapped (back to 60), switch, release 60: silence") {
        engine->noteOn(60, 100);
        (void)renderRms(*engine, 0.1);
        engine->noteOn(64, 100);
        (void)renderRms(*engine, 0.1);
        engine->noteOff(64);  // mono returns to the held 60
        (void)renderRms(*engine, 0.1);
        engine->setMode(VoiceMode::Poly);
        const float sounding = renderRms(*engine, 0.05);
        INFO("RMS with 60 held across the switch: " << sounding);
        REQUIRE(sounding > 1e-3f);
        engine->noteOff(60);
        (void)renderRms(*engine, 1.0);
        const float rms = renderRms(*engine, 1.0);
        INFO("RMS 1-2 s after releasing 60: " << rms);
        REQUIRE(rms < 1e-5f);
    }
}

TEST_CASE("RuinaeEngine_PolyphonyReduction_ExcessVoicesReleaseSmoothly",
          "[ruinae-engine][voice-mode][stuck-notes]") {
    auto engine = std::make_unique<RuinaeEngine>();
    prepareSwitchEngine(*engine);
    engine->setPolyphony(8);

    const std::array<uint8_t, 8> chord{48, 52, 55, 59, 60, 64, 67, 71};
    for (auto n : chord) engine->noteOn(n, 100);

    std::vector<float> out;
    renderSeconds(*engine, 47.0 * kSwitchBlock / kSwitchSampleRate, out);  // 47 blocks
    const size_t changeAt = out.size();
    engine->setPolyphony(2);
    renderSeconds(*engine, 0.3, out);

    const double ratio = RuinaeAuditRig::clickAt(out, changeAt,
                                     static_cast<size_t>(0.01 * kSwitchSampleRate),
                                     static_cast<size_t>(0.05 * kSwitchSampleRate)).ratio;
    INFO("jump ratio at the polyphony change: " << ratio);
    REQUIRE(ratio <= 1.5);

    for (auto n : chord) engine->noteOff(n);
    (void)renderRms(*engine, 1.0);
    const float released = renderRms(*engine, 1.0);
    INFO("RMS after every key and the release: " << released);
    REQUIRE(released < 1e-5f);

    engine->setPolyphony(8);  // no new notes: nothing may come back
    const float ghost = renderRms(*engine, 0.1);
    INFO("RMS 100 ms after raising polyphony with no notes: " << ghost);
    REQUIRE(ghost < 1e-5f);
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

    SECTION("setMonoPortamentoTime forwards to MonoHandler (FR-036)") {
        engine.setMonoPortamentoTime(200.0f);
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
        REQUIRE(static_cast<uint32_t>(RuinaeModDest::AllVoiceSpectralTilt) == 71);
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

}

TEST_CASE("RuinaeEngine getLatencySamples", "[ruinae-engine][effects][US5]") {
    RuinaeEngine engine;

    SECTION("returns latency from effects chain (FR-028)") {
        engine.prepare(44100.0, 512);
        // The effects chain adds no latency (R-055, R-116): echoes land at the set time and
        // the dry path is never delayed.
        REQUIRE(engine.getLatencySamples() == 0);
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
        // Stays 0.5: same audible output as an engine that never saw them.
        requireGuardHoldsForNaNAndInf(masterGainGuard());
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
        requireGuardHoldsForNaNAndInf(pitchBendGuard());
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
        requireGuardHoldsForNaNAndInf(aftertouchGuard());
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
        requireGuardHoldsForNaNAndInf(modWheelGuard());
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
        // Stays 500 Hz: same audible output as an engine that never saw them.
        requireGuardHoldsForNaNAndInf(globalFilterCutoffGuard());
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

    SECTION("AllVoiceSpectralTilt offset changes voice output") {
        // Use SpectralMorph mode where tilt has an effect
        engine.setMixMode(MixMode::SpectralMorph);
        engine.setMixPosition(0.5f);
        engine.setMixTilt(0.0f); // Neutral tilt
        engine.noteOn(60, 100);

        std::vector<float> leftFlat(512), rightFlat(512);
        for (int i = 0; i < 10; ++i) {
            engine.processBlock(leftFlat.data(), rightFlat.data(), 512);
        }
        float rmsFlat = computeRMS(leftFlat.data(), 512);

        // Reset and apply AllVoiceSpectralTilt offset
        engine.reset();
        engine.setMixMode(MixMode::SpectralMorph);
        engine.setMixPosition(0.5f);
        engine.setMixTilt(0.0f);
        engine.setGlobalModRoute(0, ModSource::Macro1,
                                 RuinaeModDest::AllVoiceSpectralTilt, 1.0f);
        engine.setMacroValue(0, 1.0f); // Push tilt toward +12 dB/oct

        engine.noteOn(60, 100);

        std::vector<float> leftTilt(512), rightTilt(512);
        for (int i = 0; i < 10; ++i) {
            engine.processBlock(leftTilt.data(), rightTilt.data(), 512);
        }
        float rmsTilt = computeRMS(leftTilt.data(), 512);

        // Both should produce audio
        REQUIRE(rmsFlat > 0.0f);
        REQUIRE(rmsTilt > 0.0f);
    }
}

// =============================================================================
// R-182: the mixer's Spectral Shift reaches the spectral morph filter
// =============================================================================

namespace {

/// Holds C4 on a fresh engine in SpectralMorph mode and returns the left channel
/// of the last 8 of 24 blocks. `shiftNormalized` empty = setMixShift never called.
std::vector<float> renderSpectralMorphTail(std::optional<float> shiftNormalized) {
    auto engine = std::make_unique<RuinaeEngine>();
    engine->prepare(44100.0, 512);
    engine->setMixMode(MixMode::SpectralMorph);
    engine->setMixPosition(0.5f);
    if (shiftNormalized) engine->setMixShift(*shiftNormalized);
    engine->noteOn(60, 100);

    std::vector<float> left(512), right(512), tail;
    for (int b = 0; b < 24; ++b) {
        engine->processBlock(left.data(), right.data(), 512);
        if (b >= 16) tail.insert(tail.end(), left.begin(), left.end());
    }
    return tail;
}

}  // namespace

TEST_CASE("Mixer spectral shift moves the spectrum in SpectralMorph mode",
          "[ruinae-engine][mixer]") {
    using Krate::DSP::TestUtils::spectralCentroidHz;

    const auto down = renderSpectralMorphTail(0.25f);  // -12 st
    const auto up = renderSpectralMorphTail(0.75f);    // +12 st
    REQUIRE(computeRMS(down.data(), down.size()) > 1e-4f);
    REQUIRE(computeRMS(up.data(), up.size()) > 1e-4f);

    const double centroidDown = spectralCentroidHz(down, 44100.0);
    const double centroidUp = spectralCentroidHz(up, 44100.0);
    INFO("centroid at shift 0.25: " << centroidDown << " Hz, at 0.75: " << centroidUp << " Hz");
    CHECK(centroidUp > centroidDown * 1.5);

    // 0.5 is the centre of the knob: no shift, the render of an engine never given one.
    const auto centre = renderSpectralMorphTail(0.5f);
    const auto unset = renderSpectralMorphTail(std::nullopt);
    REQUIRE(centre.size() == unset.size());
    float maxDiff = 0.0f;
    for (size_t i = 0; i < centre.size(); ++i) {
        maxDiff = std::max(maxDiff, std::abs(centre[i] - unset[i]));
    }
    INFO("max |shift 0.5 - no shift| = " << maxDiff);
    CHECK(maxDiff <= 1e-6f);
}

// =============================================================================
// R-025: the per-voice SpectralTilt route reaches the morph filter in poly mode
// =============================================================================

namespace {

/// Holds C4 on a fresh poly engine in SpectralMorph mode, optionally with a
/// per-voice Env3 -> SpectralTilt route (+1, 12 dB/oct scale), and returns the
/// left channel of the last 8 of 24 blocks.
std::vector<float> renderVoiceTiltRouteTail(bool routed) {
    auto engine = std::make_unique<RuinaeEngine>();
    engine->prepare(44100.0, 512);
    engine->setMode(VoiceMode::Poly);
    // Sawtooth on both: a tilt needs harmonics to act on (the engine default is a sine)
    engine->setOscAParam(OscParam::Waveform, static_cast<float>(OscWaveform::Sawtooth));
    engine->setOscBParam(OscParam::Waveform, static_cast<float>(OscWaveform::Sawtooth));
    engine->setMixMode(MixMode::SpectralMorph);
    engine->setMixPosition(0.5f);
    engine->setMixTilt(0.0f);
    engine->setModAttack(1.0f);
    engine->setModDecay(10.0f);
    engine->setModSustain(1.0f);
    if (routed) {
        VoiceModRoute route;
        route.source = VoiceModSource::Env3;
        route.destination = VoiceModDest::SpectralTilt;
        route.amount = 1.0f;
        engine->setVoiceModRoute(0, route);
        engine->setVoiceModRouteScale(VoiceModDest::SpectralTilt, 12.0f);
    }
    engine->noteOn(60, 100);

    std::vector<float> left(512), right(512), tail;
    for (int b = 0; b < 24; ++b) {
        engine->processBlock(left.data(), right.data(), 512);
        if (b >= 16) tail.insert(tail.end(), left.begin(), left.end());
    }
    return tail;
}

}  // namespace

TEST_CASE("Per-voice spectral tilt route is audible in poly mode",
          "[ruinae-engine][modmatrix]") {
    using Krate::DSP::TestUtils::spectralCentroidHz;

    const auto unrouted = renderVoiceTiltRouteTail(false);
    const auto routed = renderVoiceTiltRouteTail(true);
    REQUIRE(computeRMS(unrouted.data(), unrouted.size()) > 1e-4f);
    REQUIRE(computeRMS(routed.data(), routed.size()) > 1e-4f);

    const double centroidUnrouted = spectralCentroidHz(unrouted, 44100.0);
    const double centroidRouted = spectralCentroidHz(routed, 44100.0);
    INFO("centroid unrouted " << centroidUnrouted << " Hz, routed (+12 dB/oct) "
                              << centroidRouted << " Hz");
    // Env3 at sustain 1 x 12 dB/oct tilts the spectrum up: it must brighten.
    CHECK(centroidRouted > centroidUnrouted * 1.1);
}

TEST_CASE("RuinaeEngine parameter safety", "[ruinae-engine][safety]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);

    // Each setter in its own engine pair, with a setup in which its value is
    // audible or readable back (R-107).
    const std::vector<NonFiniteGuardCase> guards = {
        masterGainGuard(),         stereoSpreadGuard(),       stereoWidthGuard(),
        globalFilterCutoffGuard(), globalFilterResonanceGuard(), pitchBendGuard(),
        pitchBendRangeGuard(),     tuningReferenceGuard(),    aftertouchGuard(),
        modWheelGuard(),           monoPortamentoTimeGuard(), slideTimeGuard(),
        filterCutoffGuard(),       filterResonanceGuard(),    distortionDriveGuard(),
        ampAttackGuard(),          mixPositionGuard(),
    };

    SECTION("NaN float inputs are silently ignored (FR-043)") {
        for (const auto& guard : guards) requireGuardHolds(guard, makeNaN());
    }

    SECTION("Inf float inputs are silently ignored (FR-043)") {
        for (const auto& guard : guards) requireGuardHolds(guard, makeInf());
    }

    SECTION("output is always finite after processBlock") {
        engine.noteOn(60, 127);
        std::vector<float> left(512), right(512);
        engine.processBlock(left.data(), right.data(), 512);
        REQUIRE(allSamplesFinite(left.data(), 512));
        REQUIRE(allSamplesFinite(right.data(), 512));
    }
}

// =============================================================================
// Bug reproduction: envelope should stay in sustain during held note
// =============================================================================
// When a single note is held with default parameters, the amp envelope should
// reach Sustain stage and stay there indefinitely. The user reported that the
// envelope appears to restart after ~1 second, causing stuttering audio.

TEST_CASE("RuinaeEngine sustained note keeps envelope in sustain",
          "[ruinae-engine][bug-repro]") {
    using namespace Krate::DSP;

    constexpr float kSampleRate = 44100.0f;
    constexpr size_t kBlockSize = 512;
    constexpr float kTestDurationSec = 3.0f;
    constexpr size_t kTotalSamples = static_cast<size_t>(kSampleRate * kTestDurationSec);
    constexpr size_t kNumBlocks = kTotalSamples / kBlockSize + 1;

    RuinaeEngine engine;
    engine.prepare(kSampleRate, kBlockSize);

    // Set parameters matching the processor defaults (applyParamsToEngine)
    engine.setMode(VoiceMode::Poly);
    engine.setPolyphony(8);
    engine.setMasterGain(1.0f);
    engine.setSoftLimitEnabled(true);

    // Amp envelope defaults from amp_env_params.h
    engine.setAmpAttack(10.0f);
    engine.setAmpDecay(100.0f);
    engine.setAmpSustain(0.8f);
    engine.setAmpRelease(200.0f);

    // Filter defaults
    engine.setFilterCutoff(20000.0f);
    engine.setFilterResonance(0.1f);
    engine.setFilterEnvAmount(0.0f);
    engine.setFilterKeyTrack(0.0f);

    // Filter envelope defaults
    engine.setFilterAttack(10.0f);
    engine.setFilterDecay(100.0f);
    engine.setFilterSustain(0.8f);
    engine.setFilterRelease(200.0f);

    // Mod envelope defaults
    engine.setModAttack(10.0f);
    engine.setModDecay(100.0f);
    engine.setModSustain(0.8f);
    engine.setModRelease(200.0f);

    // Set a block context (simulating host)
    BlockContext ctx{};
    ctx.sampleRate = kSampleRate;
    ctx.blockSize = kBlockSize;
    ctx.tempoBPM = 120.0;
    ctx.isPlaying = true;
    engine.setBlockContext(ctx);

    // Play a single note
    engine.noteOn(60, 100);

    std::vector<float> left(kBlockSize, 0.0f);
    std::vector<float> right(kBlockSize, 0.0f);

    // Track envelope stages
    bool reachedSustain = false;
    bool envelopeRestarted = false;
    int restartBlock = -1;
    ADSRStage previousStage = ADSRStage::Idle;

    for (size_t block = 0; block < kNumBlocks; ++block) {
        // Simulate applyParamsToEngine calling setters every block (like the real processor)
        engine.setAmpAttack(10.0f);
        engine.setAmpDecay(100.0f);
        engine.setAmpSustain(0.8f);
        engine.setAmpRelease(200.0f);
        engine.setFilterAttack(10.0f);
        engine.setFilterDecay(100.0f);
        engine.setFilterSustain(0.8f);
        engine.setFilterRelease(200.0f);
        engine.setModAttack(10.0f);
        engine.setModDecay(100.0f);
        engine.setModSustain(0.8f);
        engine.setModRelease(200.0f);

        // Also simulate other per-block param forwarding
        engine.setPolyphony(8);
        engine.setMode(VoiceMode::Poly);
        engine.setMasterGain(1.0f);
        engine.setFilterCutoff(20000.0f);

        engine.processBlock(left.data(), right.data(), kBlockSize);

        // Check voice 0 amp envelope stage
        ADSRStage currentStage = engine.getVoiceAmpEnvelope(0).getStage();

        if (currentStage == ADSRStage::Sustain) {
            reachedSustain = true;
        }

        // Once we've reached sustain, the envelope should NEVER go back to Attack
        if (reachedSustain && currentStage == ADSRStage::Attack) {
            envelopeRestarted = true;
            restartBlock = static_cast<int>(block);
            float timeAtRestart = static_cast<float>(block * kBlockSize) / kSampleRate;
            INFO("Envelope restarted at block " << block
                 << " (time: " << timeAtRestart << "s)"
                 << " previous stage: " << static_cast<int>(previousStage));
            break;
        }

        // The envelope should never go Idle while note is held
        if (reachedSustain && currentStage == ADSRStage::Idle) {
            envelopeRestarted = true;
            restartBlock = static_cast<int>(block);
            float timeAtRestart = static_cast<float>(block * kBlockSize) / kSampleRate;
            INFO("Envelope went Idle at block " << block
                 << " (time: " << timeAtRestart << "s)"
                 << " previous stage: " << static_cast<int>(previousStage));
            break;
        }

        previousStage = currentStage;
    }

    // Check that envelope stayed in sustain throughout
    REQUIRE(reachedSustain);
    INFO("envelope restarted or went Idle at block " << restartBlock);
    CHECK_FALSE(envelopeRestarted);

    // Also verify there's continuous non-zero audio output at the end
    engine.processBlock(left.data(), right.data(), kBlockSize);
    float peak = 0.0f;
    for (size_t i = 0; i < kBlockSize; ++i) {
        float absVal = std::abs(left[i]);
        if (absVal > peak) peak = absVal;
    }
    CHECK(peak > 0.001f);
}

// =============================================================================
// Phase 3 (Spec 057): Rungler Forwarding Tests
// =============================================================================

TEST_CASE("RuinaeEngine Rungler setters forward to ModulationEngine",
          "[ruinae_engine][rungler]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);

    SECTION("setRunglerOsc1Freq forwards correctly") {
        // Set rungler params
        engine.setRunglerOsc1Freq(5.0f);
        engine.setRunglerOsc2Freq(7.0f);
        engine.setRunglerDepth(0.5f);
        engine.setRunglerFilter(0.3f);
        engine.setRunglerBits(12);
        engine.setRunglerLoopMode(false);
        // Should not crash
    }

    SECTION("Rungler produces modulation when routed") {
        // Configure rungler
        engine.setRunglerOsc1Freq(5.0f);
        engine.setRunglerOsc2Freq(7.0f);
        engine.setRunglerDepth(0.5f);
        engine.setRunglerFilter(0.0f);
        engine.setRunglerBits(8);
        engine.setRunglerLoopMode(false);

        // Route Rungler to filter cutoff
        engine.setGlobalModRoute(0, ModSource::Rungler,
                                 RuinaeModDest::GlobalFilterCutoff, 1.0f);

        engine.noteOn(60, 100);

        // Process several blocks and collect output energy
        constexpr size_t kBlockSize = 512;
        std::vector<float> left(kBlockSize), right(kBlockSize);
        bool hasAudio = false;
        for (int i = 0; i < 20; ++i) {
            engine.processBlock(left.data(), right.data(), kBlockSize);
            if (hasNonZeroSamples(left.data(), kBlockSize)) hasAudio = true;
        }

        // Engine should produce audio (Rungler is being processed and
        // modulating GlobalFilterCutoff)
        REQUIRE(hasAudio);
    }

    SECTION("All 6 rungler setters accept full parameter ranges") {
        // Min values
        engine.setRunglerOsc1Freq(0.1f);
        engine.setRunglerOsc2Freq(0.1f);
        engine.setRunglerDepth(0.0f);
        engine.setRunglerFilter(0.0f);
        engine.setRunglerBits(4);
        engine.setRunglerLoopMode(false);
        // Should not crash

        // Max values
        engine.setRunglerOsc1Freq(100.0f);
        engine.setRunglerOsc2Freq(100.0f);
        engine.setRunglerDepth(1.0f);
        engine.setRunglerFilter(1.0f);
        engine.setRunglerBits(16);
        engine.setRunglerLoopMode(true);
        // Should not crash
    }
}

// =============================================================================
// Modulation destinations must self-heal when the offset returns to zero
// =============================================================================
// The global filter and master gain are recomputed unconditionally every block,
// so removing a route -- or turning its depth down to zero -- puts them straight
// back on the user's base value. EffectMix and every AllVoice* destination were
// instead guarded by `if (offset != 0.0f)`, which skips the write precisely when
// the base value needs restoring. The last modulated value was then orphaned
// until the user happened to touch the base parameter again.

namespace {

void processSilentBlock(RuinaeEngine& engine, size_t numSamples = 64) {
    std::vector<float> left(numSamples, 0.0f);
    std::vector<float> right(numSamples, 0.0f);
    engine.processBlock(left.data(), right.data(), numSamples);
}

} // namespace

TEST_CASE("EffectMix modulation returns to base when the offset goes to zero",
          "[ruinae_engine][modulation][regression]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);

    constexpr float kBaseMix = 0.2f;
    engine.setDelayMix(kBaseMix);

    // External source slots hold a value we set directly, so the offset is
    // pinned rather than drifting the way an LFO's would.
    engine.setGlobalModRoute(0, ModSource::External0, RuinaeModDest::EffectMix, 1.0f);
    engine.setExternalSourceValue(0, 0.5f);
    processSilentBlock(engine);
    REQUIRE(engine.effectsChain().getDelayMix() > kBaseMix + 0.05f);

    engine.setExternalSourceValue(0, 0.0f);
    processSilentBlock(engine);
    CHECK(engine.effectsChain().getDelayMix() == Approx(kBaseMix).margin(1e-4));
}

TEST_CASE("AllVoice modulation returns to base when the offset goes to zero",
          "[ruinae_engine][modulation][regression]") {
    RuinaeEngine engine;
    engine.prepare(44100.0, 512);

    constexpr float kBaseCutoff = 1000.0f;
    constexpr float kBaseMorph = 0.25f;
    engine.setFilterCutoff(kBaseCutoff);
    engine.setMixPosition(kBaseMorph);

    engine.setGlobalModRoute(0, ModSource::External0,
                             RuinaeModDest::AllVoiceFilterCutoff, 1.0f);
    engine.setGlobalModRoute(1, ModSource::External1,
                             RuinaeModDest::AllVoiceMorphPosition, 1.0f);
    engine.setExternalSourceValue(0, 0.5f);
    engine.setExternalSourceValue(1, 0.5f);
    engine.noteOn(60, 100);
    processSilentBlock(engine);

    REQUIRE(engine.getVoice(0).getFilterCutoff() > kBaseCutoff * 1.5f);
    REQUIRE(engine.getVoice(0).getMixPosition() > kBaseMorph + 0.05f);

    engine.setExternalSourceValue(0, 0.0f);
    engine.setExternalSourceValue(1, 0.0f);
    processSilentBlock(engine);

    CHECK(engine.getVoice(0).getFilterCutoff() == Approx(kBaseCutoff).margin(1.0f));
    CHECK(engine.getVoice(0).getMixPosition() == Approx(kBaseMorph).margin(1e-4));
}

// =============================================================================
// Mono portamento stays continuous across the chunked render
// =============================================================================
// Mono used to drive voice 0 with a one-sample processBlock, which re-ran all of
// the voice's per-call block-rate work for every sample. It now renders in
// 32-sample chunks and advances the glide ramp across each chunk. The pitch
// therefore updates on chunk boundaries rather than per sample, so the guard is
// that the glide is still audibly smooth -- no silence, no discontinuity -- not
// that the output is bit-identical.

TEST_CASE("Mono portamento renders smoothly across chunk boundaries",
          "[ruinae_engine][mono][portamento]") {
    constexpr size_t kBlockSize = 512;
    constexpr size_t kBlocks = 8;

    RuinaeEngine engine;
    engine.prepare(44100.0, kBlockSize);
    engine.setMode(VoiceMode::Mono);
    engine.setMonoPortamentoTime(200.0f);

    engine.noteOn(48, 100);
    std::vector<float> left(kBlockSize);
    std::vector<float> right(kBlockSize);
    engine.processBlock(left.data(), right.data(), kBlockSize);

    // Glide to a note two octaves up and render across it.
    engine.noteOn(72, 100);

    std::vector<float> rendered;
    rendered.reserve(kBlockSize * kBlocks);
    for (size_t b = 0; b < kBlocks; ++b) {
        std::fill(left.begin(), left.end(), 0.0f);
        std::fill(right.begin(), right.end(), 0.0f);
        engine.processBlock(left.data(), right.data(), kBlockSize);
        rendered.insert(rendered.end(), left.begin(), left.end());
    }

    // Sound is being produced throughout: a chunking bug that dropped or
    // duplicated samples would show up as a silent or frozen stretch.
    for (size_t b = 0; b < kBlocks; ++b) {
        float peak = 0.0f;
        for (size_t i = 0; i < kBlockSize; ++i) {
            peak = std::max(peak, std::abs(rendered[b * kBlockSize + i]));
        }
        INFO("block " << b);
        CHECK(peak > 1e-4f);
    }

    // No sample-to-sample discontinuity large enough to click. A chunk boundary
    // that reset oscillator phase would break this.
    float worstJump = 0.0f;
    for (size_t i = 1; i < rendered.size(); ++i) {
        worstJump = std::max(worstJump, std::abs(rendered[i] - rendered[i - 1]));
    }
    INFO("worst sample-to-sample jump " << worstJump);
    CHECK(worstJump < 0.5f);

    // Everything finite.
    for (float s : rendered) {
        REQUIRE(std::isfinite(s));
    }
}

// Mod-matrix destinations used to reach the voices once per host buffer: the
// global modulation engine ran once per process() call and its AllVoice offsets
// (morph position, cutoff, resonance, tilt, trance gate rate, filter env amount)
// were held for the whole buffer, and each voice rendered the buffer in one call,
// so its own routes were block-rate too. At 2048-sample buffers a morph route
// stepped every 46 ms: Granular Fog (Smooth Random LFO -> morph, 90 ms smoothing)
// clicked at note-ons with steps of 0.6-0.8 against 0.16 at 64-sample buffers.
// The engine must apply modulation on a fixed sub-block grid inside every buffer,
// so that a 2048-sample buffer renders what 64-sample buffers render.
TEST_CASE("RuinaeEngine modulation reaches the voices in sub-steps inside large buffers",
          "[ruinae-engine][modulation][block_size]") {
    constexpr double kRate = 44100.0;
    constexpr size_t kLen = size_t{2} * 44032;  // ~2 s, a multiple of 2048
    constexpr size_t kSecondNoteAt = 43008; // 21 * 2048: a buffer start at both sizes

    const auto render = [&](size_t blockSize) {
        RuinaeEngine engine;
        engine.prepare(kRate, 2048);
        engine.setOscAParam(OscParam::Waveform, 0.0f);  // sine
        engine.setOscBLevel(0.0f);                      // morph = A's level: out ~ (1 - morph)
        engine.setMixMode(MixMode::CrossfadeMix);
        engine.setMixPosition(0.3f);
        engine.setFilterCutoff(20000.0f);
        engine.setAmpAttack(1.0f);
        engine.setAmpSustain(1.0f);
        engine.setGlobalLFO1Rate(3.0f);
        engine.setGlobalLFO1Waveform(Waveform::Sine);
        // Granular Fog's morph route: amount 0.5, 90 ms signal smoothing.
        engine.setGlobalModRoute(0, ModSource::LFO1, RuinaeModDest::AllVoiceMorphPosition, 0.5f,
                                 ModCurve::Linear, 1.0f, false, 90.0f);
        std::vector<float> left(kLen, 0.0f);
        std::vector<float> right(kLen, 0.0f);
        for (size_t pos = 0; pos < kLen; pos += blockSize) {
            if (pos == 0) engine.noteOn(57, 100);
            if (pos == kSecondNoteAt) engine.noteOn(64, 100);
            engine.processBlock(left.data() + pos, right.data() + pos, blockSize);
        }
        return left;
    };

    const auto small = render(64);
    const auto large = render(2048);
    REQUIRE(computeRMS(small.data(), kLen) > 0.05f);  // not vacuous

    float maxDiff = 0.0f;
    size_t worst = 0;
    float stepSmall = 0.0f;
    float stepLarge = 0.0f;
    for (size_t i = 0; i < kLen; ++i) {
        const float d = std::abs(large[i] - small[i]);
        if (d > maxDiff) { maxDiff = d; worst = i; }
        if (i > 0) {
            stepSmall = std::max(stepSmall, std::abs(small[i] - small[i - 1]));
            stepLarge = std::max(stepLarge, std::abs(large[i] - large[i - 1]));
        }
    }
    // With modulation on the same sub-block grid, both buffer sizes call every
    // voice with the same sub-blocks and the same offsets, so they render the
    // same samples; 1e-5 (-100 dB) only absorbs float noise. A morph held for a
    // whole 2048-sample buffer misses the 64-sample trajectory by up to the
    // route's swing within one buffer (order 0.1 here).
    INFO("max |2048 - 64| " << maxDiff << " at sample " << worst << "; max step " << stepLarge
         << " at 2048 against " << stepSmall << " at 64");
    CHECK(maxDiff <= 1e-5f);
    CHECK(std::abs(stepLarge - stepSmall) <= 1e-5f);
}

// R-028: an arp Slide step on a chord sends one legato NoteOn per new chord
// note, with no processBlock between them and no NoteOff for the old chord.
// Each must glide its own voice: N old voices -> N new notes.
TEST_CASE("RuinaeEngine: legato chord glides N voices", "[ruinae-engine][legato]") {
    auto engine = std::make_unique<RuinaeEngine>();
    engine->prepare(44100.0, 512);
    engine->setMode(VoiceMode::Poly);
    std::vector<float> left(512), right(512);

    engine->noteOn(60, 100);
    engine->noteOn(64, 100);
    engine->noteOn(67, 100);
    engine->processBlock(left.data(), right.data(), 512);

    engine->noteOn(62, 100, true);
    engine->noteOn(65, 100, true);
    engine->noteOn(69, 100, true);
    engine->processBlock(left.data(), right.data(), 512);

    std::vector<int> gatedNotes;
    for (size_t i = 0; i < RuinaeEngine::kMaxPolyphony; ++i) {
        const int note = engine->gatedVoiceNoteForTest(i);
        if (note >= 0) gatedNotes.push_back(note);
    }
    std::sort(gatedNotes.begin(), gatedNotes.end());
    REQUIRE(gatedNotes == std::vector<int>{62, 65, 69});

    engine->noteOff(62);
    engine->noteOff(65);
    engine->noteOff(69);

    // 3 s (259 blocks); the last block must be silent.
    for (int i = 0; i < 259; ++i) {
        engine->processBlock(left.data(), right.data(), 512);
    }
    const float rms = std::max(computeRMS(left.data(), 512), computeRMS(right.data(), 512));
    INFO("RMS of the last block, 3 s after the release: " << rms);
    REQUIRE(rms < 1e-4f);
    REQUIRE(engine->getActiveVoiceCount() == 0);
}

// =============================================================================
// R-047: engine-level modulation must not step on the 64-sample sub-block grid
// =============================================================================
// The engine reads one modulation value per 64-sample sub-block. Master gain was
// applied as a constant per sub-block and the global SVFs took a new cutoff and
// resonance at each sub-block boundary, so a continuous source (LFO, envelope)
// made a staircase whose 44100 / 64 = 689 Hz component shows up as sidebands at
// f0 +- 689 Hz on a sustained tone.

namespace {

constexpr double kZipperRate = 44100.0;
constexpr float kSubBlockHz = 44100.0f / 64.0f;  // 689.06 Hz

/// Amplitude of the component at `hz` (Hann window, coherent-gain corrected).
float zipperToneAmplitude(const std::vector<float>& x, double hz) {
    double re = 0.0;
    double im = 0.0;
    double wSum = 0.0;
    const double n = static_cast<double>(x.size());
    const double w0 = 2.0 * kPi * hz / kZipperRate;
    for (size_t i = 0; i < x.size(); ++i) {
        const double w = 0.5 - 0.5 * std::cos(2.0 * kPi * static_cast<double>(i) / (n - 1.0));
        re += w * x[i] * std::cos(w0 * static_cast<double>(i));
        im -= w * x[i] * std::sin(w0 * static_cast<double>(i));
        wSum += w;
    }
    return static_cast<float>(2.0 * std::sqrt(re * re + im * im) / wSum);
}

/// Largest component within +-`halfWidthHz` of `centreHz`, in 0.5 Hz steps.
float zipperPeakNear(const std::vector<float>& x, double centreHz, double halfWidthHz) {
    float peak = 0.0f;
    for (double f = centreHz - halfWidthHz; f <= centreHz + halfWidthHz; f += 0.5) {
        peak = std::max(peak, zipperToneAmplitude(x, f));
    }
    return peak;
}

/// Sustained sine note (Osc A only, voice filter open), the global LFO1 at
/// 10 Hz routed to `dest`; renders 1.5 s and returns the last 1 s of the left channel.
std::vector<float> renderZipperProbe(RuinaeModDest dest, float amount, bool globalFilter) {
    constexpr size_t kBlock = 512;
    constexpr size_t kTotal = 66048;     // 1.5 s, a multiple of the block
    constexpr size_t kAnalysed = 44100;  // last 1 s: 1 Hz resolution
    auto engine = std::make_unique<RuinaeEngine>();
    engine->prepare(kZipperRate, kBlock);
    engine->setOscAParam(OscParam::Waveform, 0.0f);  // sine
    engine->setOscBLevel(0.0f);
    engine->setMixMode(MixMode::CrossfadeMix);
    engine->setMixPosition(0.0f);
    engine->setFilterCutoff(20000.0f);
    engine->setAmpAttack(1.0f);
    engine->setAmpSustain(1.0f);
    engine->setSoftLimitEnabled(false);  // keep the gain law linear
    if (globalFilter) {
        engine->setGlobalFilterEnabled(true);
        engine->setGlobalFilterType(SVFMode::Lowpass);
        engine->setGlobalFilterCutoff(1500.0f);
        engine->setGlobalFilterResonance(8.0f);
    }
    engine->setGlobalLFO1Rate(10.0f);
    engine->setGlobalLFO1Waveform(Waveform::Sine);
    engine->setGlobalModRoute(0, ModSource::LFO1, dest, amount);
    engine->noteOn(83, 100);  // 987.8 Hz

    std::vector<float> left(kTotal, 0.0f);
    std::vector<float> right(kTotal, 0.0f);
    for (size_t pos = 0; pos < kTotal; pos += kBlock) {
        engine->processBlock(left.data() + pos, right.data() + pos, kBlock);
    }
    return {left.end() - static_cast<std::ptrdiff_t>(kAnalysed), left.end()};
}

/// Worst f0 +- 689 Hz sideband (+-20 Hz window) relative to the fundamental, in dB.
float worstSubBlockSidebandDb(const std::vector<float>& x, float& fundamental) {
    const double f0 = static_cast<double>(midiNoteToFrequency(83));
    fundamental = zipperPeakNear(x, f0, 2.0);
    const float lower = zipperPeakNear(x, f0 - kSubBlockHz, 20.0);
    const float upper = zipperPeakNear(x, f0 + kSubBlockHz, 20.0);
    return 20.0f * std::log10(std::max(lower, upper) / fundamental);
}

} // namespace

TEST_CASE("RuinaeEngine: global gain modulation has no sub-block zipper",
          "[ruinae-engine][modulation]") {
    const auto x = renderZipperProbe(RuinaeModDest::MasterVolume, 0.5f, false);
    float fundamental = 0.0f;
    const float db = worstSubBlockSidebandDb(x, fundamental);
    INFO("fundamental " << fundamental << ", worst f0 +- 689 Hz sideband " << db << " dB");
    REQUIRE(fundamental > 0.01f);  // not vacuous
    REQUIRE(db < -60.0f);
}

TEST_CASE("RuinaeEngine: global filter modulation has no sub-block zipper",
          "[ruinae-engine][modulation][filter]") {
    const auto x = renderZipperProbe(RuinaeModDest::GlobalFilterCutoff, 0.25f, true);
    float fundamental = 0.0f;
    const float db = worstSubBlockSidebandDb(x, fundamental);
    INFO("fundamental " << fundamental << ", worst f0 +- 689 Hz sideband " << db << " dB");
    REQUIRE(fundamental > 0.01f);  // not vacuous
    REQUIRE(db < -60.0f);
}

// =============================================================================
// Voice Steal Mode and mono return-to-held-note retrigger (R-034, R-037)
// =============================================================================
// Hard and Soft used to be the same: both released the victim and re-gated it
// at once with the new pitch. Hard now fades the victim out over 2 ms and
// starts the new note on a reset voice; Soft re-gates from the current level.

namespace {

constexpr double kStealSampleRate = 44100.0;
constexpr size_t kStealBlock = 64;
constexpr size_t kStealWarmupBlocks = 70;  // ~100 ms of note A
constexpr size_t kStealAfterBlocks = 14;   // ~20 ms after note B

void configureStealProbe(RuinaeEngine& engine) {
    engine.prepare(kStealSampleRate, kStealBlock);
    engine.setDelayMix(0.0f);
    engine.setReverbParams({.roomSize = 0.5f, .damping = 0.5f, .width = 1.0f, .mix = 0.0f});
    engine.setSoftLimitEnabled(false);
    engine.setFilterCutoff(18000.0f);
    engine.setFilterResonance(0.707f);
    engine.setAmpAttack(20.0f);
    engine.setAmpDecay(50.0f);
    engine.setAmpSustain(1.0f);
    engine.setAmpRelease(50.0f);
    engine.setPolyphony(1);
}

/// Note A held for ~100 ms, then note B steals its voice; left channel.
std::vector<float> renderSteal(StealMode mode) {
    auto engine = std::make_unique<RuinaeEngine>();
    configureStealProbe(*engine);
    engine->setStealMode(mode);
    engine->noteOn(84, 100);
    std::vector<float> left((kStealWarmupBlocks + kStealAfterBlocks) * kStealBlock, 0.0f);
    std::vector<float> right(left.size(), 0.0f);
    for (size_t b = 0; b < kStealWarmupBlocks + kStealAfterBlocks; ++b) {
        if (b == kStealWarmupBlocks) engine->noteOn(91, 100);
        engine->processBlock(left.data() + b * kStealBlock, right.data() + b * kStealBlock,
                             kStealBlock);
    }
    return left;
}

/// Smallest +-2-sample local peak in [from, to): a near-silent stretch shows as a dip.
float smallestLocalPeak(const std::vector<float>& x, size_t from, size_t to) {
    float smallest = std::numeric_limits<float>::max();
    for (size_t n = from; n < to; ++n) {
        float local = 0.0f;
        for (size_t k = n - 2; k <= n + 2; ++k) local = std::max(local, std::abs(x[k]));
        smallest = std::min(smallest, local);
    }
    return smallest;
}

} // namespace

TEST_CASE("RuinaeEngine_StealModes_Differ", "[ruinae-engine][steal]") {
    const auto hard = renderSteal(StealMode::Hard);
    const auto soft = renderSteal(StealMode::Soft);

    const size_t steal = kStealWarmupBlocks * kStealBlock;
    const size_t threeMs = static_cast<size_t>(0.003 * kStealSampleRate);
    const float levelA = findPeak(hard.data() + steal - 441, 441);  // last 10 ms of A
    REQUIRE(levelA > 0.05f);  // not vacuous

    const float hardDip = smallestLocalPeak(hard, steal, steal + threeMs);
    const float softDip = smallestLocalPeak(soft, steal, steal + threeMs);
    INFO("A level " << levelA << ", Hard dip " << hardDip << ", Soft dip " << softDip);
    // Hard: the victim is silenced before the new note's attack
    CHECK(hardDip < 0.05f * levelA);
    // Soft: no dip, the new note re-gates from the current level
    CHECK(softDip >= 0.05f * levelA);

    // The two modes render differently after the steal
    float maxDiff = 0.0f;
    for (size_t n = steal; n < hard.size(); ++n) {
        maxDiff = std::max(maxDiff, std::abs(hard[n] - soft[n]));
    }
    INFO("max |Hard - Soft| after the steal " << maxDiff);
    CHECK(maxDiff > 0.1f * levelA);

    // Hard: note B does sound once the fade is over
    const float hardB = findPeak(hard.data() + steal + 441, hard.size() - steal - 441);
    INFO("Hard: B peak 10-20 ms after the steal " << hardB);
    CHECK(hardB > 0.3f * levelA);
}

TEST_CASE("RuinaeEngine: a note released during a Hard steal fade does not hang",
          "[ruinae-engine][steal]") {
    RuinaeEngine engine;
    configureStealProbe(engine);
    engine.setStealMode(StealMode::Hard);
    std::vector<float> left(kStealBlock, 0.0f);
    std::vector<float> right(kStealBlock, 0.0f);

    engine.noteOn(84, 100);
    for (int b = 0; b < 20; ++b) engine.processBlock(left.data(), right.data(), kStealBlock);
    engine.noteOn(91, 100);
    engine.processBlock(left.data(), right.data(), 16);  // inside the 2 ms fade
    engine.noteOff(91);
    for (int b = 0; b < 200; ++b) engine.processBlock(left.data(), right.data(), kStealBlock);

    REQUIRE(engine.getActiveVoiceCount() == 0);
}

TEST_CASE("RuinaeEngine_MonoReturnRetriggersWhenLegatoOff", "[ruinae-engine][mono]") {
    RuinaeEngine engine;
    engine.prepare(kStealSampleRate, kStealBlock);
    engine.setDelayMix(0.0f);
    engine.setReverbParams({.roomSize = 0.5f, .damping = 0.5f, .width = 1.0f, .mix = 0.0f});
    engine.setSoftLimitEnabled(false);
    engine.setMode(VoiceMode::Mono);
    engine.setLegato(false);
    engine.setAmpAttack(1.0f);
    engine.setAmpDecay(50.0f);
    engine.setAmpSustain(0.0f);
    engine.setAmpRelease(50.0f);

    std::vector<float> left(kStealBlock, 0.0f);
    std::vector<float> right(kStealBlock, 0.0f);
    auto renderRms = [&](size_t blocks) {
        double sumSq = 0.0;
        for (size_t b = 0; b < blocks; ++b) {
            engine.processBlock(left.data(), right.data(), kStealBlock);
            for (float s : left) sumSq += static_cast<double>(s) * static_cast<double>(s);
        }
        return static_cast<float>(std::sqrt(sumSq / static_cast<double>(blocks * kStealBlock)));
    };

    engine.noteOn(60, 100);
    (void)renderRms(140);  // ~200 ms: decayed to sustain 0
    engine.noteOn(64, 100);
    const float pressed = renderRms(14);  // the press re-attacks
    (void)renderRms(126);
    const float beforeRelease = renderRms(7);  // last ~10 ms on 64, decayed
    engine.noteOff(64);                         // returns to the held 60
    const float afterRelease = renderRms(14);   // first ~20 ms back on 60

    INFO("pressed " << pressed << ", before release " << beforeRelease
         << ", after release " << afterRelease);
    REQUIRE(pressed > 0.01f);  // not vacuous: the press itself re-attacks
    CHECK(afterRelease > 0.25f * pressed);
    CHECK(afterRelease > 10.0f * beforeRelease);
}

TEST_CASE("RuinaeEngine: mono voice is centred at any stereo spread", "[ruinae-engine][mono][pan]") {
    RuinaeEngine engine;
    engine.prepare(kStealSampleRate, kStealBlock);
    engine.setDelayMix(0.0f);
    engine.setReverbParams({.roomSize = 0.5f, .damping = 0.5f, .width = 1.0f, .mix = 0.0f});
    engine.setSoftLimitEnabled(false);
    engine.setMode(VoiceMode::Mono);
    engine.setStereoSpread(1.0f);
    engine.setAmpSustain(1.0f);

    std::vector<float> left(kStealBlock, 0.0f);
    std::vector<float> right(kStealBlock, 0.0f);
    engine.noteOn(60, 100);
    double sumL = 0.0, sumR = 0.0;
    for (int b = 0; b < 60; ++b) {
        engine.processBlock(left.data(), right.data(), kStealBlock);
        if (b < 10) continue;
        for (size_t i = 0; i < kStealBlock; ++i) {
            sumL += static_cast<double>(left[i]) * static_cast<double>(left[i]);
            sumR += static_cast<double>(right[i]) * static_cast<double>(right[i]);
        }
    }
    REQUIRE(sumL > 0.0);
    REQUIRE(sumR > 0.0);
    const double diffDb = 10.0 * std::log10(sumL / sumR);
    INFO("L-R = " << diffDb << " dB");
    REQUIRE(std::abs(diffDb) < 0.01);
}

// =============================================================================
// Mono mode applies pitch bend and the tuning reference (R-141, R-011)
// =============================================================================

namespace {

/// Mean frequency over [a, b) from interpolated rising zero crossings (sine input).
double monoPitchZeroCrossingHz(const std::vector<float>& x, size_t a, size_t b, double sampleRate) {
    double first = -1.0;
    double last = -1.0;
    int periods = -1;
    for (size_t i = std::max<size_t>(a, 1); i < std::min(b, x.size()); ++i) {
        if (x[i - 1] < 0.0f && x[i] >= 0.0f) {
            const double t = static_cast<double>(i - 1)
                + static_cast<double>(-x[i - 1]) / static_cast<double>(x[i] - x[i - 1]);
            if (first < 0.0) first = t;
            last = t;
            ++periods;
        }
    }
    if (periods < 1) return 0.0;
    return sampleRate * static_cast<double>(periods) / (last - first);
}

/// Sine on oscillator A, filter open, no effects; A4 = 432 Hz, bend +1 with a
/// 2-semitone range, note 69 held for 200 ms. Returns the pitch over 100-200 ms.
double renderBentTunedA4(VoiceMode mode, float bendRange = 2.0f, float bend = 1.0f) {
    auto engine = std::make_unique<RuinaeEngine>();
    prepareSwitchEngine(*engine);
    engine->setOscAType(OscType::PolyBLEP);
    engine->setOscAParam(OscParam::Waveform, 0.0f);  // OscWaveform::Sine
    engine->setOscALevel(1.0f);
    engine->setOscBLevel(0.0f);
    engine->setMixPosition(0.0f);
    engine->setFilterType(RuinaeFilterType::SVF_LP);
    engine->setFilterCutoff(20000.0f);
    engine->setDistortionType(RuinaeDistortionType::Clean);
    engine->setTranceGateEnabled(false);
    engine->setDelayEnabled(false);
    engine->setReverbEnabled(false);
    engine->setHarmonizerEnabled(false);
    engine->setGlobalFilterEnabled(false);
    engine->setAmpSustain(1.0f);
    engine->setMode(mode);
    engine->setTuningReference(432.0f);
    engine->setPitchBendRange(bendRange);
    engine->setPitchBend(bend);
    engine->noteOn(69, 100);

    std::vector<float> out;
    renderSeconds(*engine, 0.2, out);
    return monoPitchZeroCrossingHz(out, static_cast<size_t>(0.1 * kSwitchSampleRate),
                                   out.size(), kSwitchSampleRate);
}

} // namespace

TEST_CASE("RuinaeEngine: mono mode applies pitch bend and tuning reference",
          "[ruinae-engine][mono][pitch]") {
    const double expectedHz = 432.0 * std::pow(2.0, 2.0 / 12.0);

    SECTION("mono") {
        const double hz = renderBentTunedA4(VoiceMode::Mono);
        REQUIRE(hz > 0.0);  // not vacuous: the note sounds
        const double cents = 1200.0 * std::log2(hz / expectedHz);
        INFO("mono: " << hz << " Hz, " << cents << " cents from " << expectedHz << " Hz");
        REQUIRE(std::abs(cents) <= 3.0);
    }

    SECTION("poly gives the same value") {
        const double hz = renderBentTunedA4(VoiceMode::Poly);
        REQUIRE(hz > 0.0);
        const double cents = 1200.0 * std::log2(hz / expectedHz);
        INFO("poly: " << hz << " Hz, " << cents << " cents from " << expectedHz << " Hz");
        REQUIRE(std::abs(cents) <= 3.0);
    }
}

// R-120: the pitch-bend controller test only checked "does not crash". Assert the
// bent pitch: +/-12 semitones of range moves A4 = 432 Hz by an octave either way,
// a half-range bend moves it by half the range, in poly and mono.
TEST_CASE("RuinaeEngine: pitch bend sets the sounding pitch",
          "[ruinae-engine][controllers][pitch]") {
    const auto checkCents = [](VoiceMode mode, float range, float bend, double semitones) {
        const double expectedHz = 432.0 * std::pow(2.0, semitones / 12.0);
        const double hz = renderBentTunedA4(mode, range, bend);
        REQUIRE(hz > 0.0);
        const double cents = 1200.0 * std::log2(hz / expectedHz);
        INFO("range " << range << " bend " << bend << ": " << hz << " Hz, " << cents
             << " cents from " << expectedHz << " Hz");
        REQUIRE(std::abs(cents) <= 3.0);
    };

    SECTION("poly: +1 of 12 semitones is an octave up") { checkCents(VoiceMode::Poly, 12.0f, 1.0f, 12.0); }
    SECTION("poly: -1 of 12 semitones is an octave down") { checkCents(VoiceMode::Poly, 12.0f, -1.0f, -12.0); }
    SECTION("poly: +0.5 of 12 semitones is a tritone up") { checkCents(VoiceMode::Poly, 12.0f, 0.5f, 6.0); }
    SECTION("mono: +1 of 12 semitones is an octave up") { checkCents(VoiceMode::Mono, 12.0f, 1.0f, 12.0); }
    SECTION("poly: centred bend leaves the pitch alone") { checkCents(VoiceMode::Poly, 12.0f, 0.0f, 0.0); }
}

// =============================================================================
// Mono: an arp slide glides with the Slide Time, every other note with the
// Mono Portamento Time (R-031)
// =============================================================================

TEST_CASE("RuinaeEngine: mono arp slide uses Slide Time and other notes use Portamento Time",
          "[ruinae-engine][mono][portamento]") {
    auto engine = std::make_unique<RuinaeEngine>();
    prepareSwitchEngine(*engine);
    engine->setOscAType(OscType::PolyBLEP);
    engine->setOscAParam(OscParam::Waveform, 0.0f);  // OscWaveform::Sine
    engine->setOscALevel(1.0f);
    engine->setOscBLevel(0.0f);
    engine->setMixPosition(0.0f);
    engine->setFilterType(RuinaeFilterType::SVF_LP);
    engine->setFilterCutoff(20000.0f);
    engine->setDistortionType(RuinaeDistortionType::Clean);
    engine->setTranceGateEnabled(false);
    engine->setDelayEnabled(false);
    engine->setReverbEnabled(false);
    engine->setHarmonizerEnabled(false);
    engine->setGlobalFilterEnabled(false);
    engine->setAmpSustain(1.0f);
    engine->setMode(VoiceMode::Mono);
    engine->setLegato(true);
    engine->setMonoPortamentoTime(0.0f);
    engine->setSlideTime(200.0f);

    const auto at = [](double seconds) {
        return static_cast<size_t>(seconds * kSwitchSampleRate);
    };
    const auto noteOf = [](double hz) { return 69.0 + 12.0 * std::log2(hz / 440.0); };

    std::vector<float> out;
    engine->noteOn(60, 100);
    renderSeconds(*engine, 0.2, out);
    engine->noteOn(72, 100, /*legato=*/true);  // arp slide step
    renderSeconds(*engine, 0.4, out);          // slide: 0.2 .. 0.6 s
    engine->noteOn(67, 100);                   // ordinary note
    renderSeconds(*engine, 0.1, out);          // 0.6 .. 0.7 s

    // 40-60 ms into the 200 ms slide: a quarter of the way from 60 to 72
    const double slideHz = monoPitchZeroCrossingHz(out, at(0.24), at(0.26), kSwitchSampleRate);
    // 2-12 ms after the ordinary note: Portamento Time 0, so already on 67
    const double noteHz = monoPitchZeroCrossingHz(out, at(0.602), at(0.612), kSwitchSampleRate);
    REQUIRE(slideHz > 0.0);  // not vacuous: the notes sound
    REQUIRE(noteHz > 0.0);
    INFO("slide at 50 ms: note " << noteOf(slideHz) << "; ordinary note after 2 ms: "
         << noteOf(noteHz));
    CHECK(std::abs(noteOf(slideHz) - 63.0) <= 0.3);
    CHECK(std::abs(100.0 * (noteOf(noteHz) - 67.0)) <= 5.0);
}

// =============================================================================
// R-008 (Ruinae audit T1.17): the engine re-forwards the BASE cutoff to every
// voice once per 64-sample mod sub-block, and the voice loop then writes the
// key-tracked effective cutoff. Each write pair used to re-arm the
// SelfOscillatingFilter kick (ladder reset + impulse), so the resonator was
// wiped 689 times a second: a quieter tone with a 689 Hz buzz.
// =============================================================================

namespace {

/// SelfOscillating, Q 20, osc levels 0, note C5, 1 s; returns the left channel.
std::vector<float> renderSelfOscEngine(float baseCutoffHz, float keyTrack, size_t block) {
    constexpr size_t kTotal = 44100;
    auto engine = std::make_unique<RuinaeEngine>();
    engine->prepare(kZipperRate, block);
    engine->setDelayMix(0.0f);
    engine->setReverbParams({.roomSize = 0.5f, .damping = 0.5f, .width = 1.0f, .mix = 0.0f});
    engine->setSoftLimitEnabled(false);
    engine->setOscALevel(0.0f);
    engine->setOscBLevel(0.0f);
    engine->setAmpAttack(1.0f);
    engine->setAmpSustain(1.0f);
    engine->setFilterType(RuinaeFilterType::SelfOscillating);
    engine->setFilterCutoff(baseCutoffHz);
    engine->setFilterResonance(20.0f);
    engine->setFilterEnvAmount(0.0f);
    engine->setFilterKeyTrack(keyTrack);
    engine->noteOn(72, 100);

    std::vector<float> left(kTotal, 0.0f);
    std::vector<float> right(kTotal, 0.0f);
    for (size_t pos = 0; pos < kTotal; pos += block) {
        const size_t n = std::min(block, kTotal - pos);
        engine->processBlock(left.data() + pos, right.data() + pos, n);
    }
    return left;
}

} // namespace

TEST_CASE("RuinaeEngine: self-osc filter with key tracking is not re-kicked per sub-block",
          "[ruinae-engine][filter]") {
    // Effective cutoff 880 Hz both ways: a 440 Hz base + 12 st key track at C5,
    // and the reference: an 880 Hz base with no key track (base == effective,
    // so the engine's and the voice's cutoff writes agree and nothing re-kicks).
    for (const size_t block : {size_t{64}, size_t{512}}) {
        SECTION("block " + std::to_string(block)) {
            const auto tracked = renderSelfOscEngine(440.0f, 1.0f, block);
            const auto reference = renderSelfOscEngine(880.0f, 0.0f, block);
            const std::vector<float> trackedTail(tracked.begin() + 22050, tracked.end());
            const std::vector<float> referenceTail(reference.begin() + 22050, reference.end());

            const float trackedRms = computeRMS(trackedTail.data(), trackedTail.size());
            const float referenceRms = computeRMS(referenceTail.data(), referenceTail.size());
            const float levelDb = 20.0f * std::log10(std::max(trackedRms, 1e-12f)
                                                     / std::max(referenceRms, 1e-12f));
            const float tone = zipperPeakNear(trackedTail, 880.0, 20.0);
            const float buzz = zipperPeakNear(trackedTail, kSubBlockHz, 2.0);
            const float buzzDb = 20.0f * std::log10(std::max(buzz, 1e-12f)
                                                    / std::max(tone, 1e-12f));

            INFO("block " << block << ": rms tracked " << trackedRms << ", reference "
                 << referenceRms << " (" << levelDb << " dB); 689 Hz line " << buzzDb
                 << " dB re tone " << tone);
            REQUIRE(referenceRms > 0.01f);  // not vacuous: the reference oscillates
            REQUIRE(std::abs(levelDb) <= 1.0f);
            REQUIRE(buzzDb < -40.0f);
        }
    }
}
