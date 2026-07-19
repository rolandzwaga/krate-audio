// ==============================================================================
// Unit Test: Controller Parameter Registration
// ==============================================================================
// Verifies that all parameters are registered in the Controller with correct
// count, names, step counts, and kCanAutomate flag.
//
// Reference: specs/045-plugin-shell/spec.md FR-013, US2
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "controller/controller.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <set>
#include "parameters/osc_params.h"
#include <string>

using namespace Steinberg;
using namespace Steinberg::Vst;

// =============================================================================
// Helper to initialize a Controller for testing
// =============================================================================

static Ruinae::Controller* makeControllerRaw() {
    auto* ctrl = new Ruinae::Controller();
    ctrl->initialize(nullptr);
    return ctrl;
}

// Helper to convert String128 to std::string for comparison
static std::string to_string(const Steinberg::Vst::String128& str128) {
    std::string result;
    for (int i = 0; i < 128 && str128[i] != 0; ++i) {
        result += static_cast<char>(str128[i]);
    }
    return result;
}

// =============================================================================
// Tests
// =============================================================================

TEST_CASE("Controller registers parameters on initialize", "[controller][params]") {
    auto* ctrl = makeControllerRaw();

    // Should have at least 80 parameters (19 sections, most with 2+ params)
    int32 paramCount = ctrl->getParameterCount();
    REQUIRE(paramCount >= 80);

    ctrl->terminate();
}

TEST_CASE("All registered parameters have kCanAutomate flag (except hidden)", "[controller][params]") {
    auto* ctrl = makeControllerRaw();
    int32 paramCount = ctrl->getParameterCount();

    for (int32 i = 0; i < paramCount; ++i) {
        ParameterInfo info{};
        tresult result = ctrl->getParameterInfo(i, info);
        REQUIRE(result == kResultTrue);
        // Hidden/read-only params (e.g., playhead indicators) don't need kCanAutomate
        if (info.flags & ParameterInfo::kIsHidden)
            continue;
        // Every non-hidden parameter should have kCanAutomate flag set
        CHECK((info.flags & ParameterInfo::kCanAutomate) != 0);
    }

    ctrl->terminate();
}

TEST_CASE("All registered parameters have non-empty titles", "[controller][params]") {
    auto* ctrl = makeControllerRaw();
    int32 paramCount = ctrl->getParameterCount();

    for (int32 i = 0; i < paramCount; ++i) {
        ParameterInfo info{};
        ctrl->getParameterInfo(i, info);
        std::string title = to_string(info.title);
        CHECK(!title.empty());
    }

    ctrl->terminate();
}

TEST_CASE("Specific parameters are registered with correct names", "[controller][params]") {
    auto* ctrl = makeControllerRaw();

    // Check a representative sample of parameters by ID
    auto checkParam = [&](ParamID id, const char* expectedName) {
        ParameterInfo info{};
        // Find by iterating (getParameterInfo takes index, not ID)
        int32 paramCount = ctrl->getParameterCount();
        bool found = false;
        for (int32 i = 0; i < paramCount; ++i) {
            ctrl->getParameterInfo(i, info);
            if (info.id == id) {
                found = true;
                break;
            }
        }
        INFO("Looking for parameter ID " << id << " (" << expectedName << ")");
        REQUIRE(found);
        std::string title = to_string(info.title);
        CHECK(title == expectedName);
    };

    // Global
    checkParam(Ruinae::kMasterGainId, "Master Gain");
    checkParam(Ruinae::kPolyphonyId, "Polyphony");
    checkParam(Ruinae::kSoftLimitId, "Soft Limit");

    // OSC A
    checkParam(Ruinae::kOscATypeId, "OSC A Type");
    checkParam(Ruinae::kOscATuneId, "OSC A Tune");
    checkParam(Ruinae::kOscALevelId, "OSC A Level");

    // Filter
    checkParam(Ruinae::kFilterTypeId, "Filter Type");
    checkParam(Ruinae::kFilterCutoffId, "Filter Cutoff");

    // Distortion
    checkParam(Ruinae::kDistortionTypeId, "Distortion Type");
    checkParam(Ruinae::kDistortionDriveId, "Distortion Drive");

    // Amp Envelope
    checkParam(Ruinae::kAmpEnvAttackId, "Amp Attack");
    checkParam(Ruinae::kAmpEnvReleaseId, "Amp Release");

    // LFO 1
    checkParam(Ruinae::kLFO1RateId, "LFO 1 Rate");
    checkParam(Ruinae::kLFO1ShapeId, "LFO 1 Shape");

    // Global Filter
    checkParam(Ruinae::kGlobalFilterEnabledId, "Global Filter");
    checkParam(Ruinae::kGlobalFilterCutoffId, "Global Filter Cutoff");

    // Delay
    checkParam(Ruinae::kDelayTypeId, "Delay Type");
    checkParam(Ruinae::kDelayTimeId, "Delay Time");

    // Reverb
    checkParam(Ruinae::kReverbSizeId, "Reverb Size");
    checkParam(Ruinae::kReverbMixId, "Reverb Mix");

    // Mono Mode
    checkParam(Ruinae::kMonoPriorityId, "Mono Priority");
    checkParam(Ruinae::kMonoPortamentoTimeId, "Portamento Time");

    ctrl->terminate();
}

TEST_CASE("Discrete parameters have correct step counts", "[controller][params]") {
    auto* ctrl = makeControllerRaw();

    auto getStepCount = [&](ParamID id) -> int32 {
        int32 paramCount = ctrl->getParameterCount();
        for (int32 i = 0; i < paramCount; ++i) {
            ParameterInfo info{};
            ctrl->getParameterInfo(i, info);
            if (info.id == id) return info.stepCount;
        }
        FAIL("Parameter " << id << " not found");
        return -1;
    };

    // Boolean parameters: stepCount = 1
    CHECK(getStepCount(Ruinae::kSoftLimitId) == 1);
    CHECK(getStepCount(Ruinae::kTranceGateEnabledId) == 1);
    CHECK(getStepCount(Ruinae::kLFO1SyncId) == 1);
    CHECK(getStepCount(Ruinae::kDelaySyncId) == 1);
    CHECK(getStepCount(Ruinae::kReverbFreezeId) == 1);
    CHECK(getStepCount(Ruinae::kMonoLegatoId) == 1);

    // Polyphony: stepCount = 15 (1-16 = 15 steps)
    CHECK(getStepCount(Ruinae::kPolyphonyId) == 15);

    ctrl->terminate();
}

// =============================================================================
// T034: Harmonizer parameter IDs registered with correct defaults
// =============================================================================

TEST_CASE("Harmonizer parameters are registered after initialize", "[controller][params][harmonizer]") {
    auto* ctrl = makeControllerRaw();
    int32 paramCount = ctrl->getParameterCount();

    auto findParam = [&](ParamID id) -> bool {
        for (int32 i = 0; i < paramCount; ++i) {
            ParameterInfo info{};
            ctrl->getParameterInfo(i, info);
            if (info.id == id) return true;
        }
        return false;
    };

    auto getDefault = [&](ParamID id) -> double {
        for (int32 i = 0; i < paramCount; ++i) {
            ParameterInfo info{};
            ctrl->getParameterInfo(i, info);
            if (info.id == id) return info.defaultNormalizedValue;
        }
        return -1.0;
    };

    // FX enable
    INFO("kHarmonizerEnabledId should be registered");
    CHECK(findParam(Ruinae::kHarmonizerEnabledId));
    CHECK(getDefault(Ruinae::kHarmonizerEnabledId) == 0.0);

    // Global harmonizer params (2800-2807)
    CHECK(findParam(Ruinae::kHarmonizerHarmonyModeId));
    CHECK(findParam(Ruinae::kHarmonizerKeyId));
    CHECK(findParam(Ruinae::kHarmonizerScaleId));
    CHECK(findParam(Ruinae::kHarmonizerPitchShiftModeId));
    CHECK(findParam(Ruinae::kHarmonizerFormantPreserveId));
    CHECK(findParam(Ruinae::kHarmonizerNumVoicesId));
    CHECK(findParam(Ruinae::kHarmonizerDryLevelId));
    CHECK(findParam(Ruinae::kHarmonizerWetLevelId));

    // Per-voice params: Voice 1 (2810-2814)
    CHECK(findParam(Ruinae::kHarmonizerVoice1IntervalId));
    CHECK(findParam(Ruinae::kHarmonizerVoice1LevelId));
    CHECK(findParam(Ruinae::kHarmonizerVoice1PanId));
    CHECK(findParam(Ruinae::kHarmonizerVoice1DelayId));
    CHECK(findParam(Ruinae::kHarmonizerVoice1DetuneId));

    // Per-voice params: Voice 2 (2820-2824)
    CHECK(findParam(Ruinae::kHarmonizerVoice2IntervalId));
    CHECK(findParam(Ruinae::kHarmonizerVoice2LevelId));
    CHECK(findParam(Ruinae::kHarmonizerVoice2PanId));
    CHECK(findParam(Ruinae::kHarmonizerVoice2DelayId));
    CHECK(findParam(Ruinae::kHarmonizerVoice2DetuneId));

    // Per-voice params: Voice 3 (2830-2834)
    CHECK(findParam(Ruinae::kHarmonizerVoice3IntervalId));
    CHECK(findParam(Ruinae::kHarmonizerVoice3LevelId));
    CHECK(findParam(Ruinae::kHarmonizerVoice3PanId));
    CHECK(findParam(Ruinae::kHarmonizerVoice3DelayId));
    CHECK(findParam(Ruinae::kHarmonizerVoice3DetuneId));

    // Per-voice params: Voice 4 (2840-2844)
    CHECK(findParam(Ruinae::kHarmonizerVoice4IntervalId));
    CHECK(findParam(Ruinae::kHarmonizerVoice4LevelId));
    CHECK(findParam(Ruinae::kHarmonizerVoice4PanId));
    CHECK(findParam(Ruinae::kHarmonizerVoice4DelayId));
    CHECK(findParam(Ruinae::kHarmonizerVoice4DetuneId));

    // Verify default normalized values for key continuous params
    // dryLevel default = 0.909 (0 dB in [-60, +6])
    CHECK(getDefault(Ruinae::kHarmonizerDryLevelId) == Catch::Approx(60.0 / 66.0).margin(0.01));
    // wetLevel default = 0.818 (-6 dB in [-60, +6])
    CHECK(getDefault(Ruinae::kHarmonizerWetLevelId) == Catch::Approx(54.0 / 66.0).margin(0.01));
    // Interval default = 0.5 (0 steps in [-24, +24])
    CHECK(getDefault(Ruinae::kHarmonizerVoice1IntervalId) == Catch::Approx(0.5).margin(0.01));
    // Pan default = 0.5 (center)
    CHECK(getDefault(Ruinae::kHarmonizerVoice1PanId) == Catch::Approx(0.5).margin(0.01));
    // Delay default = 0.0 (0 ms)
    CHECK(getDefault(Ruinae::kHarmonizerVoice1DelayId) == Catch::Approx(0.0).margin(0.01));
    // Detune default = 0.5 (0 cents)
    CHECK(getDefault(Ruinae::kHarmonizerVoice1DetuneId) == Catch::Approx(0.5).margin(0.01));

    ctrl->terminate();
}

// =============================================================================
// T035: kActionFxExpandHarmonizerTag is handled by the controller
// =============================================================================

// (FX expand test removed — FX panels are always visible in Tab_Fx now)

// =============================================================================
// Bug Regression: Harmonizer voice interval dropdowns must be StringListParameter
// =============================================================================
// COptionMenu requires StringListParameter (kIsList flag) to populate entries.
// A plain RangeParameter with stepCount=48 causes the dropdown to default to
// index 0 (-24 steps) and snap to -24 on any selection.

TEST_CASE("Harmonizer voice interval params are StringListParameter with kIsList",
          "[controller][params][harmonizer][dropdown]") {
    auto* ctrl = makeControllerRaw();
    int32 paramCount = ctrl->getParameterCount();

    auto getFlags = [&](ParamID id) -> int32 {
        for (int32 i = 0; i < paramCount; ++i) {
            ParameterInfo info{};
            ctrl->getParameterInfo(i, info);
            if (info.id == id) return info.flags;
        }
        FAIL("Parameter " << id << " not found");
        return -1;
    };

    auto getStepCount = [&](ParamID id) -> int32 {
        for (int32 i = 0; i < paramCount; ++i) {
            ParameterInfo info{};
            ctrl->getParameterInfo(i, info);
            if (info.id == id) return info.stepCount;
        }
        FAIL("Parameter " << id << " not found");
        return -1;
    };

    auto getDefault = [&](ParamID id) -> double {
        for (int32 i = 0; i < paramCount; ++i) {
            ParameterInfo info{};
            ctrl->getParameterInfo(i, info);
            if (info.id == id) return info.defaultNormalizedValue;
        }
        return -1.0;
    };

    // All 4 voice interval params must have kIsList flag (StringListParameter)
    const ParamID intervalIds[] = {
        Ruinae::kHarmonizerVoice1IntervalId,
        Ruinae::kHarmonizerVoice2IntervalId,
        Ruinae::kHarmonizerVoice3IntervalId,
        Ruinae::kHarmonizerVoice4IntervalId,
    };

    for (auto id : intervalIds) {
        INFO("Checking interval param ID " << id);
        int32 flags = getFlags(id);
        CHECK((flags & ParameterInfo::kIsList) != 0);
        CHECK(getStepCount(id) == 48);  // 49 entries (0..48)
        CHECK(getDefault(id) == Catch::Approx(0.5).margin(0.02));  // index 24 = "0 steps"
    }

    // Verify the display string at default (index 24 = "0 steps")
    String128 str;
    tresult result = ctrl->getParamStringByValue(
        Ruinae::kHarmonizerVoice1IntervalId, 0.5, str);
    if (result == kResultOk) {
        std::string display = to_string(str);
        CHECK(display == "0 steps");
    }

    // Verify display at index 0 = "-24 steps" (normalized = 0.0)
    result = ctrl->getParamStringByValue(
        Ruinae::kHarmonizerVoice1IntervalId, 0.0, str);
    if (result == kResultOk) {
        std::string display = to_string(str);
        CHECK(display == "-24 steps");
    }

    // Verify display at index 48 = "+24 steps" (normalized = 1.0)
    result = ctrl->getParamStringByValue(
        Ruinae::kHarmonizerVoice1IntervalId, 1.0, str);
    if (result == kResultOk) {
        std::string display = to_string(str);
        CHECK(display == "+24 steps");
    }

    ctrl->terminate();
}

TEST_CASE("Mod matrix parameters are all registered", "[controller][params]") {
    auto* ctrl = makeControllerRaw();

    // 8 slots x 3 params = 24 mod matrix parameters
    const ParamID modParamIds[] = {
        Ruinae::kModMatrixSlot0SourceId, Ruinae::kModMatrixSlot0DestId, Ruinae::kModMatrixSlot0AmountId,
        Ruinae::kModMatrixSlot1SourceId, Ruinae::kModMatrixSlot1DestId, Ruinae::kModMatrixSlot1AmountId,
        Ruinae::kModMatrixSlot2SourceId, Ruinae::kModMatrixSlot2DestId, Ruinae::kModMatrixSlot2AmountId,
        Ruinae::kModMatrixSlot3SourceId, Ruinae::kModMatrixSlot3DestId, Ruinae::kModMatrixSlot3AmountId,
        Ruinae::kModMatrixSlot4SourceId, Ruinae::kModMatrixSlot4DestId, Ruinae::kModMatrixSlot4AmountId,
        Ruinae::kModMatrixSlot5SourceId, Ruinae::kModMatrixSlot5DestId, Ruinae::kModMatrixSlot5AmountId,
        Ruinae::kModMatrixSlot6SourceId, Ruinae::kModMatrixSlot6DestId, Ruinae::kModMatrixSlot6AmountId,
        Ruinae::kModMatrixSlot7SourceId, Ruinae::kModMatrixSlot7DestId, Ruinae::kModMatrixSlot7AmountId,
    };

    int32 paramCount = ctrl->getParameterCount();

    for (auto id : modParamIds) {
        bool found = false;
        for (int32 i = 0; i < paramCount; ++i) {
            ParameterInfo info{};
            ctrl->getParameterInfo(i, info);
            if (info.id == id) {
                found = true;
                break;
            }
        }
        INFO("Mod matrix parameter ID " << id << " should be registered");
        CHECK(found);
    }

    ctrl->terminate();
}

// =============================================================================
// Registered ParamIDs must be unique
// =============================================================================
// VST3 requires unique ParamIDs, and a collision is invisible until a host
// happens to route the wrong parameter. kSidechainActiveId was defined as 3400,
// which sits inside the arp routing range (kArpBaseId..kArpEndId) -- so
// processParameterChanges dispatched it to handleArpParamChange. Rather than
// pin the specific pair, this walks the whole registered set, which also catches
// the next accidental reuse.

TEST_CASE("All registered parameter IDs are unique",
          "[ruinae][controller][params][regression]") {
    auto* ctrl = makeControllerRaw();

    const int32 count = ctrl->getParameterCount();
    REQUIRE(count > 0);

    std::set<ParamID> seen;
    for (int32 i = 0; i < count; ++i) {
        ParameterInfo info{};
        REQUIRE(ctrl->getParameterInfo(i, info) == kResultOk);
        INFO("duplicate ParamID " << info.id << " at index " << i);
        CHECK(seen.insert(info.id).second);
    }

    ctrl->terminate();
    ctrl->release();
}

TEST_CASE("Sidechain status parameter sits outside the arp routing range",
          "[ruinae][controller][params][regression]") {
    // processParameterChanges dispatches the whole kArpBaseId..kArpEndId span to
    // the arp handler, so any non-arp parameter placed in that window is routed
    // to the wrong destination regardless of whether its ID happens to collide.
    CHECK(Ruinae::kSidechainActiveId > Ruinae::kArpEndId);
}

// =============================================================================
// OSC dropdown labels must come from the canonical tables
// =============================================================================
// Each OSC dropdown used to exist in three hand-synced copies: the canonical
// k*Strings table in dropdown_mappings.h (which nothing read), plus an inline
// literal list in the OSC A registration and another in OSC B. This walks the
// registered parameters and compares every label against the table, so the
// registration cannot drift away from the single source of truth again.

namespace {

void checkDropdownMatchesTable(Ruinae::Controller* ctrl,
                               ParamID id,
                               const Steinberg::Vst::TChar* const* strings,
                               int count) {
    auto* param = ctrl->getParameterObject(id);
    REQUIRE(param != nullptr);

    const auto info = param->getInfo();
    INFO("param " << id);
    REQUIRE(info.stepCount == count - 1);

    for (int i = 0; i < count; ++i) {
        const ParamValue normalized =
            (count > 1) ? static_cast<ParamValue>(i) / static_cast<ParamValue>(count - 1)
                        : 0.0;
        String128 actual{};
        param->toString(normalized, actual);

        INFO("param " << id << " index " << i);
        CHECK(std::u16string(reinterpret_cast<const char16_t*>(actual))
              == std::u16string(reinterpret_cast<const char16_t*>(strings[i])));
    }
}

} // namespace

TEST_CASE("OSC dropdowns are registered from the canonical label tables",
          "[ruinae][controller][params][dropdowns]") {
    using namespace Ruinae;

    auto* ctrl = makeControllerRaw();

    // Both banks, so a table wired into only one of them is still caught.
    for (const ParamID base : {static_cast<ParamID>(kOscATypeId),
                               static_cast<ParamID>(kOscBTypeId)}) {
        checkDropdownMatchesTable(ctrl, base + kOscTypeOffset,
                                  kOscTypeStrings, kOscTypeCount);
        checkDropdownMatchesTable(ctrl, base + kOscWaveformOffset,
                                  kOscWaveformStrings, kOscWaveformCount);
        checkDropdownMatchesTable(ctrl, base + kOscSyncWaveformOffset,
                                  kOscWaveformStrings, kOscWaveformCount);
        checkDropdownMatchesTable(ctrl, base + kOscPDWaveformOffset,
                                  kPDWaveformStrings, kPDWaveformCount);
        checkDropdownMatchesTable(ctrl, base + kOscSyncModeOffset,
                                  kSyncModeStrings, kSyncModeCount);
        checkDropdownMatchesTable(ctrl, base + kOscChaosAttractorOffset,
                                  kChaosAttractorStrings, kChaosAttractorCount);
        checkDropdownMatchesTable(ctrl, base + kOscChaosOutputOffset,
                                  kChaosOutputStrings, kChaosOutputCount);
        checkDropdownMatchesTable(ctrl, base + kOscParticleSpawnModeOffset,
                                  kParticleSpawnModeStrings, kParticleSpawnModeCount);
        checkDropdownMatchesTable(ctrl, base + kOscParticleEnvTypeOffset,
                                  kParticleEnvTypeStrings, kParticleEnvTypeCount);
        checkDropdownMatchesTable(ctrl, base + kOscFormantVowelOffset,
                                  kFormantVowelStrings, kFormantVowelCount);
        checkDropdownMatchesTable(ctrl, base + kOscNoiseColorOffset,
                                  kNoiseColorStrings, kNoiseColorCount);
    }

    ctrl->terminate();
    ctrl->release();
}
