// ==============================================================================
// Unit Test: Controller Parameter Registration
// ==============================================================================
// Verifies that all parameters are registered in the Controller with correct
// count, names, step counts, and kCanAutomate flag.
//
// Reference: specs/045-plugin-shell/spec.md FR-013, US2
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "controller/controller.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/ivsteditcontroller.h"

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

TEST_CASE("All registered parameters have kCanAutomate flag", "[controller][params]") {
    auto* ctrl = makeControllerRaw();
    int32 paramCount = ctrl->getParameterCount();

    for (int32 i = 0; i < paramCount; ++i) {
        ParameterInfo info{};
        tresult result = ctrl->getParameterInfo(i, info);
        REQUIRE(result == kResultTrue);
        // Every parameter should have kCanAutomate flag set
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
