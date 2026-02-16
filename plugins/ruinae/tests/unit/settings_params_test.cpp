// ==============================================================================
// Unit Test: Settings Parameter Handling and State Persistence
// ==============================================================================
// Verifies that settings parameters are correctly handled, formatted, and
// persisted through save/load cycles.
//
// Reference: specs/058-settings-drawer/spec.md FR-003, FR-004, FR-006, FR-007
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "plugin_ids.h"
#include "parameters/settings_params.h"

#include "public.sdk/source/common/memorystream.h"
#include "base/source/fstreamer.h"

#include <vector>

using Catch::Approx;

// =============================================================================
// T008: Settings parameter changes update engine
// =============================================================================

TEST_CASE("Settings parameter handle functions", "[settings_params][processor]") {
    SECTION("handleSettingsParamChange stores correct pitch bend range") {
        Ruinae::SettingsParams params;

        // 0.5 normalized -> round(0.5 * 24) = 12 semitones
        Ruinae::handleSettingsParamChange(params, Ruinae::kSettingsPitchBendRangeId, 0.5);
        REQUIRE(params.pitchBendRangeSemitones.load() == Approx(12.0f));

        // 0.0 -> 0 semitones
        Ruinae::handleSettingsParamChange(params, Ruinae::kSettingsPitchBendRangeId, 0.0);
        REQUIRE(params.pitchBendRangeSemitones.load() == Approx(0.0f));

        // 1.0 -> 24 semitones
        Ruinae::handleSettingsParamChange(params, Ruinae::kSettingsPitchBendRangeId, 1.0);
        REQUIRE(params.pitchBendRangeSemitones.load() == Approx(24.0f));

        // Default normalized 2/24 = 0.0833 -> round(0.0833 * 24) = 2 semitones
        Ruinae::handleSettingsParamChange(params, Ruinae::kSettingsPitchBendRangeId, 2.0 / 24.0);
        REQUIRE(params.pitchBendRangeSemitones.load() == Approx(2.0f));
    }

    SECTION("handleSettingsParamChange stores correct velocity curve") {
        Ruinae::SettingsParams params;

        // 0.0 -> Linear (0)
        Ruinae::handleSettingsParamChange(params, Ruinae::kSettingsVelocityCurveId, 0.0);
        REQUIRE(params.velocityCurve.load() == 0);

        // 1.0 -> Fixed (3)
        Ruinae::handleSettingsParamChange(params, Ruinae::kSettingsVelocityCurveId, 1.0);
        REQUIRE(params.velocityCurve.load() == 3);

        // 0.333... -> Soft (1)
        Ruinae::handleSettingsParamChange(params, Ruinae::kSettingsVelocityCurveId, 1.0 / 3.0);
        REQUIRE(params.velocityCurve.load() == 1);
    }

    SECTION("handleSettingsParamChange stores correct tuning reference") {
        Ruinae::SettingsParams params;

        // 0.5 -> 400 + 0.5 * 80 = 440 Hz
        Ruinae::handleSettingsParamChange(params, Ruinae::kSettingsTuningReferenceId, 0.5);
        REQUIRE(params.tuningReferenceHz.load() == Approx(440.0f));

        // 0.0 -> 400 Hz
        Ruinae::handleSettingsParamChange(params, Ruinae::kSettingsTuningReferenceId, 0.0);
        REQUIRE(params.tuningReferenceHz.load() == Approx(400.0f));

        // 1.0 -> 480 Hz
        Ruinae::handleSettingsParamChange(params, Ruinae::kSettingsTuningReferenceId, 1.0);
        REQUIRE(params.tuningReferenceHz.load() == Approx(480.0f));

        // (432-400)/80 = 0.4 -> 432 Hz
        Ruinae::handleSettingsParamChange(params, Ruinae::kSettingsTuningReferenceId, 0.4);
        REQUIRE(params.tuningReferenceHz.load() == Approx(432.0f));
    }

    SECTION("handleSettingsParamChange stores correct voice allocation mode") {
        Ruinae::SettingsParams params;

        // 0.0 -> RoundRobin (0)
        Ruinae::handleSettingsParamChange(params, Ruinae::kSettingsVoiceAllocModeId, 0.0);
        REQUIRE(params.voiceAllocMode.load() == 0);

        // 1/3 -> Oldest (1)
        Ruinae::handleSettingsParamChange(params, Ruinae::kSettingsVoiceAllocModeId, 1.0 / 3.0);
        REQUIRE(params.voiceAllocMode.load() == 1);

        // 1.0 -> HighestNote (3)
        Ruinae::handleSettingsParamChange(params, Ruinae::kSettingsVoiceAllocModeId, 1.0);
        REQUIRE(params.voiceAllocMode.load() == 3);
    }

    SECTION("handleSettingsParamChange stores correct voice steal mode") {
        Ruinae::SettingsParams params;

        // 0.0 -> Hard (0)
        Ruinae::handleSettingsParamChange(params, Ruinae::kSettingsVoiceStealModeId, 0.0);
        REQUIRE(params.voiceStealMode.load() == 0);

        // 1.0 -> Soft (1)
        Ruinae::handleSettingsParamChange(params, Ruinae::kSettingsVoiceStealModeId, 1.0);
        REQUIRE(params.voiceStealMode.load() == 1);
    }

    SECTION("handleSettingsParamChange stores correct gain compensation") {
        Ruinae::SettingsParams params;

        // 1.0 -> enabled
        Ruinae::handleSettingsParamChange(params, Ruinae::kSettingsGainCompensationId, 1.0);
        REQUIRE(params.gainCompensation.load() == true);

        // 0.0 -> disabled
        Ruinae::handleSettingsParamChange(params, Ruinae::kSettingsGainCompensationId, 0.0);
        REQUIRE(params.gainCompensation.load() == false);

        // 0.5 -> enabled (>= 0.5 threshold)
        Ruinae::handleSettingsParamChange(params, Ruinae::kSettingsGainCompensationId, 0.5);
        REQUIRE(params.gainCompensation.load() == true);

        // 0.49 -> disabled
        Ruinae::handleSettingsParamChange(params, Ruinae::kSettingsGainCompensationId, 0.49);
        REQUIRE(params.gainCompensation.load() == false);
    }
}

TEST_CASE("Settings parameter format functions", "[settings_params][processor]") {
    SECTION("formatSettingsParam produces correct pitch bend range string") {
        Steinberg::Vst::String128 str{};

        // 0.5 normalized -> 12 st
        auto result = Ruinae::formatSettingsParam(Ruinae::kSettingsPitchBendRangeId, 0.5, str);
        REQUIRE(result == Steinberg::kResultOk);

        char ascii[128];
        Steinberg::UString(str, 128).toAscii(ascii, 128);
        REQUIRE(std::string(ascii) == "12 st");
    }

    SECTION("formatSettingsParam produces correct tuning reference string") {
        Steinberg::Vst::String128 str{};

        // 0.5 normalized -> 440.0 Hz
        auto result = Ruinae::formatSettingsParam(Ruinae::kSettingsTuningReferenceId, 0.5, str);
        REQUIRE(result == Steinberg::kResultOk);

        char ascii[128];
        Steinberg::UString(str, 128).toAscii(ascii, 128);
        REQUIRE(std::string(ascii) == "440.0 Hz");
    }

    SECTION("formatSettingsParam returns kResultFalse for dropdown IDs") {
        Steinberg::Vst::String128 str{};
        REQUIRE(Ruinae::formatSettingsParam(Ruinae::kSettingsVelocityCurveId, 0.5, str) == Steinberg::kResultFalse);
        REQUIRE(Ruinae::formatSettingsParam(Ruinae::kSettingsVoiceAllocModeId, 0.5, str) == Steinberg::kResultFalse);
        REQUIRE(Ruinae::formatSettingsParam(Ruinae::kSettingsVoiceStealModeId, 0.5, str) == Steinberg::kResultFalse);
        REQUIRE(Ruinae::formatSettingsParam(Ruinae::kSettingsGainCompensationId, 0.5, str) == Steinberg::kResultFalse);
    }

    SECTION("formatSettingsParam returns kResultFalse for non-settings IDs") {
        Steinberg::Vst::String128 str{};
        REQUIRE(Ruinae::formatSettingsParam(Ruinae::kMasterGainId, 0.5, str) == Steinberg::kResultFalse);
    }
}

// =============================================================================
// T009: Settings params save and load
// =============================================================================

TEST_CASE("Settings params save and load round-trip", "[settings_params][state_persistence]") {
    Ruinae::SettingsParams params;

    // Set non-default values
    params.pitchBendRangeSemitones.store(7.0f, std::memory_order_relaxed);
    params.velocityCurve.store(2, std::memory_order_relaxed);          // Hard
    params.tuningReferenceHz.store(432.0f, std::memory_order_relaxed);
    params.voiceAllocMode.store(0, std::memory_order_relaxed);         // RoundRobin
    params.voiceStealMode.store(1, std::memory_order_relaxed);         // Soft
    params.gainCompensation.store(true, std::memory_order_relaxed);

    // Save to stream
    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    {
        Steinberg::IBStreamer streamer(stream, kLittleEndian);
        Ruinae::saveSettingsParams(params, streamer);
    }

    // Reset params to defaults
    params.pitchBendRangeSemitones.store(2.0f, std::memory_order_relaxed);
    params.velocityCurve.store(0, std::memory_order_relaxed);
    params.tuningReferenceHz.store(440.0f, std::memory_order_relaxed);
    params.voiceAllocMode.store(1, std::memory_order_relaxed);
    params.voiceStealMode.store(0, std::memory_order_relaxed);
    params.gainCompensation.store(false, std::memory_order_relaxed);

    // Load from stream
    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    {
        Steinberg::IBStreamer streamer(stream, kLittleEndian);
        bool ok = Ruinae::loadSettingsParams(params, streamer);
        REQUIRE(ok);
    }

    // Verify restored values
    REQUIRE(params.pitchBendRangeSemitones.load() == Approx(7.0f));
    REQUIRE(params.velocityCurve.load() == 2);
    REQUIRE(params.tuningReferenceHz.load() == Approx(432.0f));
    REQUIRE(params.voiceAllocMode.load() == 0);
    REQUIRE(params.voiceStealMode.load() == 1);
    REQUIRE(params.gainCompensation.load() == true);
}

TEST_CASE("Settings params controller load maps values correctly", "[settings_params][state_persistence]") {
    Ruinae::SettingsParams params;
    params.pitchBendRangeSemitones.store(7.0f, std::memory_order_relaxed);
    params.velocityCurve.store(2, std::memory_order_relaxed);
    params.tuningReferenceHz.store(432.0f, std::memory_order_relaxed);
    params.voiceAllocMode.store(0, std::memory_order_relaxed);
    params.voiceStealMode.store(1, std::memory_order_relaxed);
    params.gainCompensation.store(true, std::memory_order_relaxed);

    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    {
        Steinberg::IBStreamer streamer(stream, kLittleEndian);
        Ruinae::saveSettingsParams(params, streamer);
    }

    // Track setParam calls
    struct ParamSet {
        Steinberg::Vst::ParamID id;
        double value;
    };
    std::vector<ParamSet> calls;

    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    {
        Steinberg::IBStreamer streamer(stream, kLittleEndian);
        Ruinae::loadSettingsParamsToController(streamer,
            [&](Steinberg::Vst::ParamID id, double value) {
                calls.push_back({id, value});
            });
    }

    REQUIRE(calls.size() == 6);

    // Pitch Bend Range: 7.0 / 24.0
    REQUIRE(calls[0].id == Ruinae::kSettingsPitchBendRangeId);
    REQUIRE(calls[0].value == Approx(7.0 / 24.0).margin(0.001));

    // Velocity Curve: 2 / 3.0
    REQUIRE(calls[1].id == Ruinae::kSettingsVelocityCurveId);
    REQUIRE(calls[1].value == Approx(2.0 / 3.0).margin(0.001));

    // Tuning Reference: (432 - 400) / 80 = 0.4
    REQUIRE(calls[2].id == Ruinae::kSettingsTuningReferenceId);
    REQUIRE(calls[2].value == Approx(0.4).margin(0.001));

    // Voice Allocation: 0 / 3.0 = 0.0
    REQUIRE(calls[3].id == Ruinae::kSettingsVoiceAllocModeId);
    REQUIRE(calls[3].value == Approx(0.0).margin(0.001));

    // Voice Steal: 1 / 1.0 = 1.0
    REQUIRE(calls[4].id == Ruinae::kSettingsVoiceStealModeId);
    REQUIRE(calls[4].value == Approx(1.0).margin(0.001));

    // Gain Compensation: true -> 1.0
    REQUIRE(calls[5].id == Ruinae::kSettingsGainCompensationId);
    REQUIRE(calls[5].value == Approx(1.0).margin(0.001));
}

TEST_CASE("Settings params load returns false on truncated stream", "[settings_params][state_persistence]") {
    // Empty stream should fail gracefully
    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    Ruinae::SettingsParams params;
    Steinberg::IBStreamer streamer(stream, kLittleEndian);
    REQUIRE(Ruinae::loadSettingsParams(params, streamer) == false);
}
