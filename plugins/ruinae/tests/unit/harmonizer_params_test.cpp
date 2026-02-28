// ==============================================================================
// Unit Test: Harmonizer Parameters
// ==============================================================================
// Verifies:
// - T008: RuinaeHarmonizerParams struct existence and field defaults
// - T009: handleHarmonizerParamChange() global param denormalization
// - T010: Effects chain enable/bypass contract
//
// Reference: specs/067-ruinae-harmonizer/spec.md FR-004, FR-005, FR-008,
//            FR-011, FR-012
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "parameters/harmonizer_params.h"
#include "engine/ruinae_effects_chain.h"

#include "public.sdk/source/common/memorystream.h"
#include "base/source/fstreamer.h"

#include <cmath>
#include <vector>

using Catch::Approx;

// ==============================================================================
// T008: Struct Defaults
// ==============================================================================

TEST_CASE("RuinaeHarmonizerParams struct defaults", "[harmonizer][params]") {
    Ruinae::RuinaeHarmonizerParams params;

    SECTION("Global param defaults") {
        CHECK(params.harmonyMode.load() == 0);
        CHECK(params.key.load() == 0);
        CHECK(params.scale.load() == 0);
        CHECK(params.pitchShiftMode.load() == 0);
        CHECK(params.formantPreserve.load() == false);
        CHECK(params.numVoices.load() == 4);
        CHECK(params.dryLevelDb.load() == Approx(0.0f));
        CHECK(params.wetLevelDb.load() == Approx(-6.0f));
    }

    SECTION("Per-voice defaults") {
        for (int v = 0; v < 4; ++v) {
            auto vi = static_cast<size_t>(v);
            CHECK(params.voiceInterval[vi].load() == 0);
            CHECK(params.voiceLevelDb[vi].load() == Approx(0.0f));
            CHECK(params.voicePan[vi].load() == Approx(0.0f));
            CHECK(params.voiceDelayMs[vi].load() == Approx(0.0f));
            CHECK(params.voiceDetuneCents[vi].load() == Approx(0.0f));
        }
    }
}

// ==============================================================================
// T009: Global Parameter Denormalization
// ==============================================================================

TEST_CASE("handleHarmonizerParamChange global params", "[harmonizer][params]") {
    using namespace Ruinae;
    RuinaeHarmonizerParams params;

    SECTION("harmonyMode: 0.0 -> 0, 1.0 -> 1") {
        handleHarmonizerParamChange(params, kHarmonizerHarmonyModeId, 0.0);
        CHECK(params.harmonyMode.load() == 0);
        handleHarmonizerParamChange(params, kHarmonizerHarmonyModeId, 1.0);
        CHECK(params.harmonyMode.load() == 1);
    }

    SECTION("key: 0.0 -> 0, 1.0 -> 11") {
        handleHarmonizerParamChange(params, kHarmonizerKeyId, 0.0);
        CHECK(params.key.load() == 0);
        handleHarmonizerParamChange(params, kHarmonizerKeyId, 1.0);
        CHECK(params.key.load() == 11);
    }

    SECTION("scale: 0.0 -> 0, 1.0 -> 15") {
        handleHarmonizerParamChange(params, kHarmonizerScaleId, 0.0);
        CHECK(params.scale.load() == 0);
        handleHarmonizerParamChange(params, kHarmonizerScaleId, 1.0);
        CHECK(params.scale.load() == 15);
    }

    SECTION("pitchShiftMode: 0.0 -> 0, 1.0 -> 3") {
        handleHarmonizerParamChange(params, kHarmonizerPitchShiftModeId, 0.0);
        CHECK(params.pitchShiftMode.load() == 0);
        handleHarmonizerParamChange(params, kHarmonizerPitchShiftModeId, 1.0);
        CHECK(params.pitchShiftMode.load() == 3);
    }

    SECTION("formantPreserve: 0.0 -> false, 1.0 -> true") {
        handleHarmonizerParamChange(params, kHarmonizerFormantPreserveId, 0.0);
        CHECK(params.formantPreserve.load() == false);
        handleHarmonizerParamChange(params, kHarmonizerFormantPreserveId, 1.0);
        CHECK(params.formantPreserve.load() == true);
    }

    SECTION("numVoices: 0.0 -> 1, 1.0 -> 4") {
        handleHarmonizerParamChange(params, kHarmonizerNumVoicesId, 0.0);
        CHECK(params.numVoices.load() == 1);
        handleHarmonizerParamChange(params, kHarmonizerNumVoicesId, 1.0);
        CHECK(params.numVoices.load() == 4);
    }

    SECTION("dryLevelDb: 0.0 -> -60, 1.0 -> 6, ~0.909 -> ~0") {
        handleHarmonizerParamChange(params, kHarmonizerDryLevelId, 0.0);
        CHECK(params.dryLevelDb.load() == Approx(-60.0f).margin(0.1f));
        handleHarmonizerParamChange(params, kHarmonizerDryLevelId, 1.0);
        CHECK(params.dryLevelDb.load() == Approx(6.0f).margin(0.1f));
        // 0.909 -> 0.909 * 66 - 60 = 59.994 - 60 = -0.006 ~ 0 dB
        handleHarmonizerParamChange(params, kHarmonizerDryLevelId, 0.909);
        CHECK(params.dryLevelDb.load() == Approx(0.0f).margin(0.1f));
    }

    SECTION("wetLevelDb: ~0.818 -> ~-6") {
        // 0.818 -> 0.818 * 66 - 60 = 53.988 - 60 = -6.012 ~ -6 dB
        handleHarmonizerParamChange(params, kHarmonizerWetLevelId, 0.818);
        CHECK(params.wetLevelDb.load() == Approx(-6.0f).margin(0.1f));
    }
}

// ==============================================================================
// T010: Effects Chain Enable/Bypass
// ==============================================================================

TEST_CASE("Effects chain harmonizer enable/bypass", "[harmonizer][effects-chain]") {
    Krate::DSP::RuinaeEffectsChain chain;
    chain.prepare(44100.0, 512);

    const size_t numSamples = 128;
    std::vector<float> leftIn(numSamples);
    std::vector<float> rightIn(numSamples);
    std::vector<float> left(numSamples);
    std::vector<float> right(numSamples);

    // Fill with a test signal
    for (size_t i = 0; i < numSamples; ++i) {
        float val = 0.5f * std::sin(static_cast<float>(i) * 0.1f);
        leftIn[i] = val;
        rightIn[i] = val;
    }

    SECTION("Harmonizer disabled produces pass-through (no harmonizer effect)") {
        chain.setHarmonizerEnabled(false);

        // Copy input
        std::copy(leftIn.begin(), leftIn.end(), left.begin());
        std::copy(rightIn.begin(), rightIn.end(), right.begin());

        chain.processBlock(left.data(), right.data(), numSamples);

        // With all FX disabled, output should equal input
        // (delay, reverb, phaser, harmonizer all disabled by default)
        for (size_t i = 0; i < numSamples; ++i) {
            CHECK(left[i] == Approx(leftIn[i]).margin(1e-5f));
            CHECK(right[i] == Approx(rightIn[i]).margin(1e-5f));
        }
    }

    SECTION("setHarmonizerEnabled exists and is callable") {
        // Verify the setter exists and can be called
        chain.setHarmonizerEnabled(true);
        chain.setHarmonizerEnabled(false);
        // No crash = pass
    }
}

// ==============================================================================
// T024: Per-Voice Parameter Denormalization
// ==============================================================================

TEST_CASE("handleHarmonizerParamChange per-voice params", "[harmonizer][params]") {
    using namespace Ruinae;
    RuinaeHarmonizerParams params;

    // Voice base IDs for each of the 4 voices
    constexpr Steinberg::Vst::ParamID voiceIntervalIds[] = {
        kHarmonizerVoice1IntervalId, kHarmonizerVoice2IntervalId,
        kHarmonizerVoice3IntervalId, kHarmonizerVoice4IntervalId};
    constexpr Steinberg::Vst::ParamID voiceLevelIds[] = {
        kHarmonizerVoice1LevelId, kHarmonizerVoice2LevelId,
        kHarmonizerVoice3LevelId, kHarmonizerVoice4LevelId};
    constexpr Steinberg::Vst::ParamID voicePanIds[] = {
        kHarmonizerVoice1PanId, kHarmonizerVoice2PanId,
        kHarmonizerVoice3PanId, kHarmonizerVoice4PanId};
    constexpr Steinberg::Vst::ParamID voiceDelayIds[] = {
        kHarmonizerVoice1DelayId, kHarmonizerVoice2DelayId,
        kHarmonizerVoice3DelayId, kHarmonizerVoice4DelayId};
    constexpr Steinberg::Vst::ParamID voiceDetuneIds[] = {
        kHarmonizerVoice1DetuneId, kHarmonizerVoice2DetuneId,
        kHarmonizerVoice3DetuneId, kHarmonizerVoice4DetuneId};

    SECTION("Interval: 0.0 -> -24, 0.5 -> 0, 1.0 -> 24 for all 4 voices") {
        for (int v = 0; v < 4; ++v) {
            auto vi = static_cast<size_t>(v);
            handleHarmonizerParamChange(params, voiceIntervalIds[v], 0.0);
            CHECK(params.voiceInterval[vi].load() == -24);
            handleHarmonizerParamChange(params, voiceIntervalIds[v], 0.5);
            CHECK(params.voiceInterval[vi].load() == 0);
            handleHarmonizerParamChange(params, voiceIntervalIds[v], 1.0);
            CHECK(params.voiceInterval[vi].load() == 24);
        }
    }

    SECTION("LevelDb: 0.0 -> -60, 1.0 -> 6 for all 4 voices") {
        for (int v = 0; v < 4; ++v) {
            auto vi = static_cast<size_t>(v);
            handleHarmonizerParamChange(params, voiceLevelIds[v], 0.0);
            CHECK(params.voiceLevelDb[vi].load() == Approx(-60.0f).margin(0.1f));
            handleHarmonizerParamChange(params, voiceLevelIds[v], 1.0);
            CHECK(params.voiceLevelDb[vi].load() == Approx(6.0f).margin(0.1f));
        }
    }

    SECTION("Pan: 0.0 -> -1, 0.5 -> 0, 1.0 -> 1 for all 4 voices") {
        for (int v = 0; v < 4; ++v) {
            auto vi = static_cast<size_t>(v);
            handleHarmonizerParamChange(params, voicePanIds[v], 0.0);
            CHECK(params.voicePan[vi].load() == Approx(-1.0f).margin(0.01f));
            handleHarmonizerParamChange(params, voicePanIds[v], 0.5);
            CHECK(params.voicePan[vi].load() == Approx(0.0f).margin(0.01f));
            handleHarmonizerParamChange(params, voicePanIds[v], 1.0);
            CHECK(params.voicePan[vi].load() == Approx(1.0f).margin(0.01f));
        }
    }

    SECTION("DelayMs: 0.0 -> 0, 1.0 -> 50 for all 4 voices") {
        for (int v = 0; v < 4; ++v) {
            auto vi = static_cast<size_t>(v);
            handleHarmonizerParamChange(params, voiceDelayIds[v], 0.0);
            CHECK(params.voiceDelayMs[vi].load() == Approx(0.0f).margin(0.01f));
            handleHarmonizerParamChange(params, voiceDelayIds[v], 1.0);
            CHECK(params.voiceDelayMs[vi].load() == Approx(50.0f).margin(0.01f));
        }
    }

    SECTION("DetuneCents: 0.0 -> -50, 0.5 -> 0, 1.0 -> 50 for all 4 voices") {
        for (int v = 0; v < 4; ++v) {
            auto vi = static_cast<size_t>(v);
            handleHarmonizerParamChange(params, voiceDetuneIds[v], 0.0);
            CHECK(params.voiceDetuneCents[vi].load() == Approx(-50.0f).margin(0.01f));
            handleHarmonizerParamChange(params, voiceDetuneIds[v], 0.5);
            CHECK(params.voiceDetuneCents[vi].load() == Approx(0.0f).margin(0.01f));
            handleHarmonizerParamChange(params, voiceDetuneIds[v], 1.0);
            CHECK(params.voiceDetuneCents[vi].load() == Approx(50.0f).margin(0.01f));
        }
    }

    SECTION("Voice index routing: each voice ID maps to correct voice array slot") {
        // Set distinct values for each voice
        handleHarmonizerParamChange(params, kHarmonizerVoice1IntervalId, 0.75); // +12
        handleHarmonizerParamChange(params, kHarmonizerVoice2IntervalId, 0.25); // -12
        handleHarmonizerParamChange(params, kHarmonizerVoice3IntervalId, 1.0);  // +24
        handleHarmonizerParamChange(params, kHarmonizerVoice4IntervalId, 0.0);  // -24

        CHECK(params.voiceInterval[0].load() == 12);
        CHECK(params.voiceInterval[1].load() == -12);
        CHECK(params.voiceInterval[2].load() == 24);
        CHECK(params.voiceInterval[3].load() == -24);
    }
}

// ==============================================================================
// T025: Save/Load Round-Trip
// ==============================================================================

TEST_CASE("saveHarmonizerParams/loadHarmonizerParams round-trip", "[harmonizer][state]") {
    using namespace Ruinae;

    // Set all params to non-default values
    RuinaeHarmonizerParams original;
    original.harmonyMode.store(1, std::memory_order_relaxed);
    original.key.store(7, std::memory_order_relaxed);
    original.scale.store(4, std::memory_order_relaxed);
    original.pitchShiftMode.store(2, std::memory_order_relaxed);
    original.formantPreserve.store(true, std::memory_order_relaxed);
    original.numVoices.store(3, std::memory_order_relaxed);
    original.dryLevelDb.store(-3.5f, std::memory_order_relaxed);
    original.wetLevelDb.store(-12.0f, std::memory_order_relaxed);

    for (int v = 0; v < 4; ++v) {
        auto vi = static_cast<size_t>(v);
        original.voiceInterval[vi].store(v * 3 - 6, std::memory_order_relaxed); // -6, -3, 0, 3
        original.voiceLevelDb[vi].store(-10.0f + static_cast<float>(v) * 2.0f, std::memory_order_relaxed);
        original.voicePan[vi].store(-0.5f + static_cast<float>(v) * 0.3f, std::memory_order_relaxed);
        original.voiceDelayMs[vi].store(5.0f + static_cast<float>(v) * 10.0f, std::memory_order_relaxed);
        original.voiceDetuneCents[vi].store(-20.0f + static_cast<float>(v) * 15.0f, std::memory_order_relaxed);
    }

    // Serialize to a memory stream
    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    {
        Steinberg::IBStreamer writeStream(stream, kLittleEndian);
        saveHarmonizerParams(original, writeStream);
    }

    // Deserialize to a fresh struct
    RuinaeHarmonizerParams loaded;
    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    {
        Steinberg::IBStreamer readStream(stream, kLittleEndian);
        bool ok = loadHarmonizerParams(loaded, readStream);
        REQUIRE(ok);
    }

    // Verify global params
    CHECK(loaded.harmonyMode.load() == 1);
    CHECK(loaded.key.load() == 7);
    CHECK(loaded.scale.load() == 4);
    CHECK(loaded.pitchShiftMode.load() == 2);
    CHECK(loaded.formantPreserve.load() == true);
    CHECK(loaded.numVoices.load() == 3);
    CHECK(loaded.dryLevelDb.load() == Approx(-3.5f));
    CHECK(loaded.wetLevelDb.load() == Approx(-12.0f));

    // Verify per-voice params
    for (int v = 0; v < 4; ++v) {
        auto vi = static_cast<size_t>(v);
        CHECK(loaded.voiceInterval[vi].load() == v * 3 - 6);
        CHECK(loaded.voiceLevelDb[vi].load() == Approx(-10.0f + static_cast<float>(v) * 2.0f));
        CHECK(loaded.voicePan[vi].load() == Approx(-0.5f + static_cast<float>(v) * 0.3f));
        CHECK(loaded.voiceDelayMs[vi].load() == Approx(5.0f + static_cast<float>(v) * 10.0f));
        CHECK(loaded.voiceDetuneCents[vi].load() == Approx(-20.0f + static_cast<float>(v) * 15.0f));
    }
}

// ==============================================================================
// T026: Edge Values
// ==============================================================================

TEST_CASE("handleHarmonizerParamChange edge values", "[harmonizer][params][edge]") {
    using namespace Ruinae;
    RuinaeHarmonizerParams params;

    SECTION("Interval clamped at -24/+24") {
        handleHarmonizerParamChange(params, kHarmonizerVoice1IntervalId, 0.0);
        CHECK(params.voiceInterval[0].load() == -24);
        handleHarmonizerParamChange(params, kHarmonizerVoice1IntervalId, 1.0);
        CHECK(params.voiceInterval[0].load() == 24);
    }

    SECTION("dB levels clamped at -60/+6") {
        handleHarmonizerParamChange(params, kHarmonizerDryLevelId, 0.0);
        CHECK(params.dryLevelDb.load() == Approx(-60.0f).margin(0.1f));
        handleHarmonizerParamChange(params, kHarmonizerDryLevelId, 1.0);
        CHECK(params.dryLevelDb.load() == Approx(6.0f).margin(0.1f));

        handleHarmonizerParamChange(params, kHarmonizerVoice1LevelId, 0.0);
        CHECK(params.voiceLevelDb[0].load() == Approx(-60.0f).margin(0.1f));
        handleHarmonizerParamChange(params, kHarmonizerVoice1LevelId, 1.0);
        CHECK(params.voiceLevelDb[0].load() == Approx(6.0f).margin(0.1f));
    }

    SECTION("Pan clamped at -1/+1") {
        handleHarmonizerParamChange(params, kHarmonizerVoice1PanId, 0.0);
        CHECK(params.voicePan[0].load() == Approx(-1.0f).margin(0.01f));
        handleHarmonizerParamChange(params, kHarmonizerVoice1PanId, 1.0);
        CHECK(params.voicePan[0].load() == Approx(1.0f).margin(0.01f));
    }

    SECTION("DelayMs clamped at 0/50") {
        handleHarmonizerParamChange(params, kHarmonizerVoice1DelayId, 0.0);
        CHECK(params.voiceDelayMs[0].load() == Approx(0.0f).margin(0.01f));
        handleHarmonizerParamChange(params, kHarmonizerVoice1DelayId, 1.0);
        CHECK(params.voiceDelayMs[0].load() == Approx(50.0f).margin(0.01f));
    }

    SECTION("DetuneCents clamped at -50/+50") {
        handleHarmonizerParamChange(params, kHarmonizerVoice1DetuneId, 0.0);
        CHECK(params.voiceDetuneCents[0].load() == Approx(-50.0f).margin(0.01f));
        handleHarmonizerParamChange(params, kHarmonizerVoice1DetuneId, 1.0);
        CHECK(params.voiceDetuneCents[0].load() == Approx(50.0f).margin(0.01f));
    }
}

// ==============================================================================
// T049: Full Processor State Round-Trip (including harmonizerEnabled int8)
// ==============================================================================

TEST_CASE("Harmonizer full state round-trip with enabled flag", "[harmonizer][state]") {
    using namespace Ruinae;

    // --- Set params to non-default values via handleHarmonizerParamChange ---
    RuinaeHarmonizerParams original;
    handleHarmonizerParamChange(original, kHarmonizerHarmonyModeId, 1.0);   // Scalic
    handleHarmonizerParamChange(original, kHarmonizerKeyId, 7.0 / 11.0);   // G (index 7)
    handleHarmonizerParamChange(original, kHarmonizerScaleId, 4.0 / 8.0);  // Dorian (index 4)
    handleHarmonizerParamChange(original, kHarmonizerPitchShiftModeId, 2.0 / 3.0); // PhaseVocoder
    handleHarmonizerParamChange(original, kHarmonizerFormantPreserveId, 1.0); // true
    handleHarmonizerParamChange(original, kHarmonizerNumVoicesId, 2.0 / 3.0); // 3 voices
    handleHarmonizerParamChange(original, kHarmonizerDryLevelId, 0.5);     // -27 dB
    handleHarmonizerParamChange(original, kHarmonizerWetLevelId, 0.7);     // -13.8 dB

    // Set per-voice params for voice 1 and voice 3 to non-defaults
    handleHarmonizerParamChange(original, kHarmonizerVoice1IntervalId, 0.75); // +12
    handleHarmonizerParamChange(original, kHarmonizerVoice1LevelId, 0.8);
    handleHarmonizerParamChange(original, kHarmonizerVoice1PanId, 0.25);     // -0.5
    handleHarmonizerParamChange(original, kHarmonizerVoice1DelayId, 0.4);    // 20 ms
    handleHarmonizerParamChange(original, kHarmonizerVoice1DetuneId, 0.6);   // +10 cents

    handleHarmonizerParamChange(original, kHarmonizerVoice3IntervalId, 0.25); // -12
    handleHarmonizerParamChange(original, kHarmonizerVoice3LevelId, 0.6);
    handleHarmonizerParamChange(original, kHarmonizerVoice3PanId, 0.75);     // +0.5
    handleHarmonizerParamChange(original, kHarmonizerVoice3DelayId, 0.6);    // 30 ms
    handleHarmonizerParamChange(original, kHarmonizerVoice3DetuneId, 0.4);   // -10 cents

    // Set enabled to true
    bool originalEnabled = true;

    // Serialize: saveHarmonizerParams + writeInt8 for enabled flag
    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    {
        Steinberg::IBStreamer writeStream(stream, kLittleEndian);
        saveHarmonizerParams(original, writeStream);
        writeStream.writeInt8(originalEnabled ? 1 : 0);
    }

    // Deserialize into fresh structs
    RuinaeHarmonizerParams loaded;
    bool loadedEnabled = false;
    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    {
        Steinberg::IBStreamer readStream(stream, kLittleEndian);
        bool ok = loadHarmonizerParams(loaded, readStream);
        REQUIRE(ok);
        Steinberg::int8 i8 = 0;
        REQUIRE(readStream.readInt8(i8));
        loadedEnabled = (i8 != 0);
    }

    // Verify enabled flag
    CHECK(loadedEnabled == true);

    // Verify global params
    CHECK(loaded.harmonyMode.load() == original.harmonyMode.load());
    CHECK(loaded.key.load() == original.key.load());
    CHECK(loaded.scale.load() == original.scale.load());
    CHECK(loaded.pitchShiftMode.load() == original.pitchShiftMode.load());
    CHECK(loaded.formantPreserve.load() == original.formantPreserve.load());
    CHECK(loaded.numVoices.load() == original.numVoices.load());
    CHECK(loaded.dryLevelDb.load() == Approx(original.dryLevelDb.load()));
    CHECK(loaded.wetLevelDb.load() == Approx(original.wetLevelDb.load()));

    // Verify per-voice params
    for (int v = 0; v < 4; ++v) {
        auto vi = static_cast<size_t>(v);
        CHECK(loaded.voiceInterval[vi].load() == original.voiceInterval[vi].load());
        CHECK(loaded.voiceLevelDb[vi].load() == Approx(original.voiceLevelDb[vi].load()));
        CHECK(loaded.voicePan[vi].load() == Approx(original.voicePan[vi].load()));
        CHECK(loaded.voiceDelayMs[vi].load() == Approx(original.voiceDelayMs[vi].load()));
        CHECK(loaded.voiceDetuneCents[vi].load() == Approx(original.voiceDetuneCents[vi].load()));
    }
}

// ==============================================================================
// T050: v15->v16 State Migration (no harmonizer data in old stream)
// ==============================================================================

TEST_CASE("Harmonizer state migration from v15 (no harmonizer data)", "[harmonizer][state][migration]") {
    using namespace Ruinae;

    // A v15 stream has no harmonizer data at the end.
    // Simulate this by creating an empty stream (no harmonizer bytes to read).
    // When version < 16, the processor skips loading harmonizer params entirely,
    // so the struct remains at its default values.
    RuinaeHarmonizerParams params;
    bool harmonizerEnabled = false;

    // Verify the struct defaults are what we expect for an old preset:
    // All harmonizer params should be at registration defaults
    CHECK(params.harmonyMode.load() == 0);       // Chromatic
    CHECK(params.key.load() == 0);                // C
    CHECK(params.scale.load() == 0);              // Major
    CHECK(params.pitchShiftMode.load() == 0);     // Simple
    CHECK(params.formantPreserve.load() == false); // off
    CHECK(params.numVoices.load() == 4);           // 4 voices (default)
    CHECK(params.dryLevelDb.load() == Approx(0.0f));
    CHECK(params.wetLevelDb.load() == Approx(-6.0f));
    CHECK(harmonizerEnabled == false);             // disabled

    for (int v = 0; v < 4; ++v) {
        auto vi = static_cast<size_t>(v);
        CHECK(params.voiceInterval[vi].load() == 0);
        CHECK(params.voiceLevelDb[vi].load() == Approx(0.0f));
        CHECK(params.voicePan[vi].load() == Approx(0.0f));
        CHECK(params.voiceDelayMs[vi].load() == Approx(0.0f));
        CHECK(params.voiceDetuneCents[vi].load() == Approx(0.0f));
    }

    // Now verify that if we try to read from an empty stream (simulating the
    // end of a v15 state), loadHarmonizerParams returns false (no data),
    // and the struct remains at defaults.
    auto emptyStream = Steinberg::owned(new Steinberg::MemoryStream());
    {
        Steinberg::IBStreamer readStream(emptyStream, kLittleEndian);
        bool ok = loadHarmonizerParams(params, readStream);
        CHECK(ok == false); // No data to read
    }

    // Struct remains unchanged at defaults
    CHECK(params.harmonyMode.load() == 0);
    CHECK(params.key.load() == 0);
    CHECK(params.numVoices.load() == 4);
    CHECK(params.dryLevelDb.load() == Approx(0.0f));
    CHECK(params.wetLevelDb.load() == Approx(-6.0f));
    CHECK(harmonizerEnabled == false);
}

// ==============================================================================
// T058: Latency Reporting - Combined spectral delay + harmonizer PhaseVocoder
// ==============================================================================

TEST_CASE("Effects chain latency includes harmonizer PhaseVocoder worst-case",
          "[harmonizer][effects-chain][latency]") {
    Krate::DSP::RuinaeEffectsChain chain;
    chain.prepare(44100.0, 512);

    // Spectral delay latency: default FFT size = 1024 samples
    // Harmonizer PhaseVocoder latency: FFT(4096) + Hop(1024) = 5120 samples
    // Combined: 1024 + 5120 = 6144 samples
    const size_t latency = chain.getLatencySamples();
    CHECK(latency == 6144);
}
