// ==============================================================================
// Membrum VST3 Tests -- Parameter Registration, State Round-Trip, Bus Config
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "controller/controller.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/common/memorystream.h"

#include <cstring>
#include <memory>
#include <string>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

// =============================================================================
// Helpers
// =============================================================================

static constexpr double kTestSampleRate = 44100.0;
static constexpr int32 kTestBlockSize = 512;

// Helper: read pad 0's first N sound params from a v4 state stream.
// Seeks to the beginning and skips the v4 header + pad 0 selectors.
static void readV4Pad0SoundParams(MemoryStream* stream, double* outParams, int count)
{
    stream->seek(0, IBStream::kIBSeekSet, nullptr);
    int32 version = 0, maxPoly = 0, stealPolicy = 0;
    stream->read(&version, sizeof(version), nullptr);
    stream->read(&maxPoly, sizeof(maxPoly), nullptr);
    stream->read(&stealPolicy, sizeof(stealPolicy), nullptr);
    int32 excType = 0, bodyModel = 0;
    stream->read(&excType, sizeof(excType), nullptr);
    stream->read(&bodyModel, sizeof(bodyModel), nullptr);
    for (int i = 0; i < count; ++i)
        stream->read(&outParams[i], sizeof(double), nullptr);
}

static ProcessSetup makeSetup(double sampleRate = kTestSampleRate)
{
    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = kTestBlockSize;
    setup.sampleRate = sampleRate;
    return setup;
}

// Helper: convert Steinberg TChar string to std::string
static std::string tcharToString(const TChar* str)
{
    std::string result;
    while (*str)
    {
        result += static_cast<char>(*str);
        ++str;
    }
    return result;
}

// =============================================================================
// Parameter Registration (FR-020)
// =============================================================================

TEST_CASE("Membrum Controller registers all Phase 2 parameters",
          "[membrum][vst][params]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // Phase 2: 5 Phase-1 params + 2 selectors + 27 Phase-2 continuous = 34.
    // Phase 3: +3 (maxPolyphony, voiceStealing, chokeGroup) = 37.
    // Phase 4: +1 (selectedPad) + 1152 (32 pads x 36 params) = 1190.
    // Phase 5: +4 (global coupling + snare buzz + tom resonance + coupling delay) = 1194.
    // Phase 6 (US4): +32 (per-pad coupling amount, offset 36) = 1226.
    // Phase 6 (US1, spec 141): +1 (kUiModeId) + 160 (32 pads x 5 macros) = 1387.
    // Phase 8 (US7, spec 141): +1 (kOutputBusId Output Bus selector proxy) = 1388.
    // Phase 7: +8 (noise/click global proxies) + 256 (32 pads x 8 offsets) = 1652.
    // Phase 8A: +2 (b1/b3 global proxies) + 64 (32 pads x 2 offsets) = 1718.
    int32 paramCount = controller.getParameterCount();
    CHECK(paramCount == 1718);

    REQUIRE(controller.terminate() == kResultOk);
}

TEST_CASE("Membrum Controller parameter metadata matches contract",
          "[membrum][vst][params]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    struct ExpectedParam
    {
        ParamID id;
        const char* name;
        double defaultNorm;
    };

    const ExpectedParam expected[] = {
        {Membrum::kMaterialId, "Material", 0.5},
        {Membrum::kSizeId, "Size", 0.5},
        {Membrum::kDecayId, "Resonance", 0.3},
        {Membrum::kStrikePositionId, "Strike Point", 0.3},
        {Membrum::kLevelId, "Level", 0.8},
    };

    for (const auto& exp : expected)
    {
        SECTION(std::string("Parameter ") + exp.name)
        {
            ParameterInfo info{};
            bool found = false;
            int32 paramCount = controller.getParameterCount();
            for (int32 i = 0; i < paramCount; ++i)
            {
                controller.getParameterInfo(i, info);
                if (info.id == exp.id)
                {
                    found = true;
                    break;
                }
            }
            REQUIRE(found);

            // Check name
            std::string name = tcharToString(info.title);
            CHECK(name == exp.name);

            // Check range is [0, 1]
            CHECK(info.stepCount == 0);

            // Check default value
            CHECK(info.defaultNormalizedValue == Approx(exp.defaultNorm));
        }
    }

    REQUIRE(controller.terminate() == kResultOk);
}

TEST_CASE("Membrum Level parameter has dB unit",
          "[membrum][vst][params]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    ParameterInfo info{};
    int32 paramCount = controller.getParameterCount();
    for (int32 i = 0; i < paramCount; ++i)
    {
        controller.getParameterInfo(i, info);
        if (info.id == Membrum::kLevelId)
            break;
    }

    std::string units = tcharToString(info.units);
    CHECK(units == "dB");

    REQUIRE(controller.terminate() == kResultOk);
}

// =============================================================================
// State Round-Trip (FR-016, SC-004)
// =============================================================================

TEST_CASE("Membrum state round-trip preserves all parameter values",
          "[membrum][vst][state]")
{
    // Set up processor with non-default parameter values
    Membrum::Processor processor;
    REQUIRE(processor.initialize(nullptr) == kResultOk);

    auto setup = makeSetup();
    REQUIRE(processor.setupProcessing(setup) == kResultOk);
    REQUIRE(processor.setActive(true) == kResultOk);

    // Mutate pad 0 directly to non-default values.
    {
        auto& pad0 = processor.voicePoolForTest().padConfigMut(0);
        pad0.material       = 0.2f;
        pad0.size           = 0.7f;
        pad0.decay          = 0.1f;
        pad0.strikePosition = 0.9f;
        pad0.level          = 0.6f;
    }

    // Save state -> load into a fresh processor -> round-trip values.
    auto* saveStream = new MemoryStream();
    REQUIRE(processor.getState(saveStream) == kResultOk);
    saveStream->seek(0, IBStream::kIBSeekSet, nullptr);

    Membrum::Processor loader;
    REQUIRE(loader.initialize(nullptr) == kResultOk);
    REQUIRE(loader.setupProcessing(setup) == kResultOk);
    REQUIRE(loader.setActive(true) == kResultOk);
    REQUIRE(loader.setState(saveStream) == kResultOk);

    const auto& loaded = loader.voicePoolForTest().padConfig(0);
    CHECK(loaded.material       == Approx(0.2f).margin(1e-6f));
    CHECK(loaded.size           == Approx(0.7f).margin(1e-6f));
    CHECK(loaded.decay          == Approx(0.1f).margin(1e-6f));
    CHECK(loaded.strikePosition == Approx(0.9f).margin(1e-6f));
    CHECK(loaded.level          == Approx(0.6f).margin(1e-6f));

    saveStream->release();
    REQUIRE(loader.setActive(false) == kResultOk);
    REQUIRE(loader.terminate() == kResultOk);
    REQUIRE(processor.setActive(false) == kResultOk);
    REQUIRE(processor.terminate() == kResultOk);
}

TEST_CASE("Membrum state round-trip with defaults",
          "[membrum][vst][state]")
{
    Membrum::Processor processor;
    REQUIRE(processor.initialize(nullptr) == kResultOk);

    // Save state with defaults
    auto* saveStream = new MemoryStream();
    REQUIRE(processor.getState(saveStream) == kResultOk);

    // Read back and verify defaults (v4 format)
    double readParams[5] = {};
    readV4Pad0SoundParams(saveStream, readParams, 5);

    // After Phase 4 DefaultKit::apply(), pad 0 = Kick template (FR-030/FR-031)
    CHECK(readParams[0] == Approx(0.3).margin(0.001));  // Material (kick default)
    CHECK(readParams[1] == Approx(0.8).margin(0.001));  // Size (kick default)
    CHECK(readParams[2] == Approx(0.3).margin(0.001));  // Decay (kick default)
    CHECK(readParams[3] == Approx(0.3).margin(0.001));  // StrikePosition (kick default)
    CHECK(readParams[4] == Approx(0.8).margin(0.001));  // Level (kick default)

    saveStream->release();
    REQUIRE(processor.terminate() == kResultOk);
}

// =============================================================================
// Controller setComponentState (FR-016) -- round-trip through processor state.
// =============================================================================

TEST_CASE("Membrum Controller setComponentState syncs parameters",
          "[membrum][vst][state]")
{
    // Generate a v6 state blob from a processor whose pad 0 has custom values,
    // then push it through Controller::setComponentState and verify the
    // corresponding global-proxy params reflect those values.
    Membrum::Processor processor;
    REQUIRE(processor.initialize(nullptr) == kResultOk);
    auto setup = makeSetup();
    REQUIRE(processor.setupProcessing(setup) == kResultOk);
    REQUIRE(processor.setActive(true) == kResultOk);

    {
        auto& pad0 = processor.voicePoolForTest().padConfigMut(0);
        pad0.material       = 0.2f;
        pad0.size           = 0.7f;
        pad0.decay          = 0.1f;
        pad0.strikePosition = 0.9f;
        pad0.level          = 0.6f;
    }

    auto* stream = new MemoryStream();
    REQUIRE(processor.getState(stream) == kResultOk);
    stream->seek(0, IBStream::kIBSeekSet, nullptr);

    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);
    REQUIRE(controller.setComponentState(stream) == kResultOk);
    stream->release();

    // Global proxy params mirror the selected pad (pad 0 by default).
    CHECK(controller.getParamNormalized(Membrum::kMaterialId)       == Approx(0.2).margin(1e-6));
    CHECK(controller.getParamNormalized(Membrum::kSizeId)           == Approx(0.7).margin(1e-6));
    CHECK(controller.getParamNormalized(Membrum::kDecayId)          == Approx(0.1).margin(1e-6));
    CHECK(controller.getParamNormalized(Membrum::kStrikePositionId) == Approx(0.9).margin(1e-6));
    CHECK(controller.getParamNormalized(Membrum::kLevelId)          == Approx(0.6).margin(1e-6));

    REQUIRE(controller.terminate() == kResultOk);
    REQUIRE(processor.setActive(false) == kResultOk);
    REQUIRE(processor.terminate() == kResultOk);
}

// =============================================================================
// Bus Configuration (FR-002)
// =============================================================================

TEST_CASE("Membrum Processor has 0 audio inputs and 1 stereo output",
          "[membrum][vst][buses]")
{
    Membrum::Processor processor;
    REQUIRE(processor.initialize(nullptr) == kResultOk);

    // Check bus arrangement: 0 audio inputs, 1 stereo output
    SpeakerArrangement stereo = SpeakerArr::kStereo;
    auto result = processor.setBusArrangements(nullptr, 0, &stereo, 1);
    CHECK(result == kResultOk);

    REQUIRE(processor.terminate() == kResultOk);
}

// =============================================================================
// Subcategory & createView (FR-002, FR-021)
// =============================================================================

TEST_CASE("Membrum subcategory is Instrument|Drum",
          "[membrum][vst][identity]")
{
    // The kSubCategories constant is checked directly
    CHECK(std::string(Membrum::kSubCategories) == "Instrument|Drum");
}

TEST_CASE("Membrum Processor and Controller FUIDs are distinct",
          "[membrum][vst][identity]")
{
    CHECK(Membrum::kProcessorUID != Membrum::kControllerUID);
}

TEST_CASE("Membrum Controller createView accepts unknown names as nullptr",
          "[membrum][vst][ui]")
{
    // Phase 6 (spec 141 T028): createView("editor") constructs a VST3Editor
    // bound to editor.uidesc; outside a loaded VST3 bundle this resource
    // lookup can throw, so we do NOT exercise the positive branch in this
    // unit test (it is covered by pluginval at plugin-shell level, SC-010).
    // We still verify the negative branch: unknown view names return nullptr.
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    auto* unknown = controller.createView("something-else");
    CHECK(unknown == nullptr);

    REQUIRE(controller.terminate() == kResultOk);
}
