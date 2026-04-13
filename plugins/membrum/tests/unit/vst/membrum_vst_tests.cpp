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
    // Phase 6 (US1, spec 141): +2 (kUiModeId, kEditorSizeId) + 160 (32 pads x 5 macros) = 1388.
    int32 paramCount = controller.getParameterCount();
    CHECK(paramCount == 1388);

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

    // Write custom parameter values via setState with known values
    // State format: int32 version + 5x float64
    {
        auto* writeStream = new MemoryStream();
        int32 version = 1;
        writeStream->write(&version, sizeof(version), nullptr);

        double params[] = {0.2, 0.7, 0.1, 0.9, 0.6}; // Material, Size, Decay, StrikePos, Level
        for (double p : params)
            writeStream->write(&p, sizeof(p), nullptr);

        writeStream->seek(0, IBStream::kIBSeekSet, nullptr);
        REQUIRE(processor.setState(writeStream) == kResultOk);
        writeStream->release();
    }

    // Save state
    auto* saveStream = new MemoryStream();
    REQUIRE(processor.getState(saveStream) == kResultOk);

    // Read back and verify -- v4 format: version(4) + maxPoly(4) + stealPolicy(4)
    // then pad 0: exciterType(4) + bodyModel(4) + 34 x float64 + uint8 + uint8
    // The first 5 sound params (offsets 2-6) start at position 20 in the stream.
    saveStream->seek(0, IBStream::kIBSeekSet, nullptr);

    int32 version = 0;
    saveStream->read(&version, sizeof(version), nullptr);
    CHECK(version == Membrum::kCurrentStateVersion);

    int32 maxPoly = 0, stealPolicy = 0;
    saveStream->read(&maxPoly, sizeof(maxPoly), nullptr);
    saveStream->read(&stealPolicy, sizeof(stealPolicy), nullptr);

    int32 excType = 0, bodyModel = 0;
    saveStream->read(&excType, sizeof(excType), nullptr);
    saveStream->read(&bodyModel, sizeof(bodyModel), nullptr);

    // Read pad 0's 34 float64 sound params: first 5 are material, size, decay, strikePos, level
    double readParams[5] = {};
    for (int i = 0; i < 5; ++i)
        saveStream->read(&readParams[i], sizeof(double), nullptr);

    CHECK(readParams[0] == Approx(0.2).margin(0.001));  // Material
    CHECK(readParams[1] == Approx(0.7).margin(0.001));  // Size
    CHECK(readParams[2] == Approx(0.1).margin(0.001));  // Decay
    CHECK(readParams[3] == Approx(0.9).margin(0.001));  // StrikePosition
    CHECK(readParams[4] == Approx(0.6).margin(0.001));  // Level

    saveStream->release();
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
// T071(d): Forward-compatible state load -- 4 params only, 5th retains default
// =============================================================================

TEST_CASE("Membrum: setState with only 4 params -- 5th retains default",
          "[membrum][vst][state][edge]")
{
    Membrum::Processor processor;
    REQUIRE(processor.initialize(nullptr) == kResultOk);

    auto setup = makeSetup();
    REQUIRE(processor.setupProcessing(setup) == kResultOk);
    REQUIRE(processor.setActive(true) == kResultOk);

    // Build a state blob with version=1 and only 4 float64 values (omitting Level)
    auto* stream = new MemoryStream();
    int32 version = 1;
    stream->write(&version, sizeof(version), nullptr);

    double params[] = {0.2, 0.7, 0.1, 0.9}; // Material, Size, Decay, StrikePos -- no Level
    for (double p : params)
        stream->write(&p, sizeof(p), nullptr);

    stream->seek(0, IBStream::kIBSeekSet, nullptr);

    // Must not crash
    REQUIRE(processor.setState(stream) == kResultOk);
    stream->release();

    // Save state back and verify the 5th param (Level) retains its default (0.8)
    auto* saveStream = new MemoryStream();
    REQUIRE(processor.getState(saveStream) == kResultOk);

    double readParams[5] = {};
    readV4Pad0SoundParams(saveStream, readParams, 5);

    CHECK(readParams[0] == Approx(0.2).margin(0.001));  // Material
    CHECK(readParams[1] == Approx(0.7).margin(0.001));  // Size
    CHECK(readParams[2] == Approx(0.1).margin(0.001));  // Decay
    CHECK(readParams[3] == Approx(0.9).margin(0.001));  // StrikePosition
    CHECK(readParams[4] == Approx(0.8).margin(0.001));  // Level = default

    saveStream->release();
    REQUIRE(processor.setActive(false) == kResultOk);
    REQUIRE(processor.terminate() == kResultOk);
}

// =============================================================================
// T071(e): Future-version state load -- version=2, 7 params, loads first 5 OK
// =============================================================================

TEST_CASE("Membrum: setState with version=2 and 7 params -- loads first 5 correctly",
          "[membrum][vst][state][edge]")
{
    Membrum::Processor processor;
    REQUIRE(processor.initialize(nullptr) == kResultOk);

    auto setup = makeSetup();
    REQUIRE(processor.setupProcessing(setup) == kResultOk);
    REQUIRE(processor.setActive(true) == kResultOk);

    // Build a state blob with version=2 and 7 float64 values
    // v2 format: version + 5 Phase1 float64 + exciterType(int32) + bodyModel(int32) + 27 Phase2 float64
    // This test writes version=2 with only 7 float64 (5 Phase1 + extra), no selectors
    // Actually v2 expects selectors after Phase1 params. Let's write a proper v2 stub:
    auto* stream = new MemoryStream();
    int32 version = 2;
    stream->write(&version, sizeof(version), nullptr);

    double params[] = {0.15, 0.85, 0.45, 0.65, 0.35};
    for (double p : params)
        stream->write(&p, sizeof(p), nullptr);

    // v2 selectors
    int32 excType = 0, bodyModel = 0;
    stream->write(&excType, sizeof(excType), nullptr);
    stream->write(&bodyModel, sizeof(bodyModel), nullptr);

    // 27 Phase 2 float64 defaults (just write some)
    for (int i = 0; i < 27; ++i)
    {
        double v = 0.0;
        stream->write(&v, sizeof(v), nullptr);
    }

    stream->seek(0, IBStream::kIBSeekSet, nullptr);
    REQUIRE(processor.setState(stream) == kResultOk);
    stream->release();

    // Save state back and verify first 5 params loaded correctly
    auto* saveStream = new MemoryStream();
    REQUIRE(processor.getState(saveStream) == kResultOk);

    double readParams[5] = {};
    readV4Pad0SoundParams(saveStream, readParams, 5);

    CHECK(readParams[0] == Approx(0.15).margin(0.001));  // Material
    CHECK(readParams[1] == Approx(0.85).margin(0.001));  // Size
    CHECK(readParams[2] == Approx(0.45).margin(0.001));  // Decay
    CHECK(readParams[3] == Approx(0.65).margin(0.001));  // StrikePosition
    CHECK(readParams[4] == Approx(0.35).margin(0.001));  // Level

    saveStream->release();
    REQUIRE(processor.setActive(false) == kResultOk);
    REQUIRE(processor.terminate() == kResultOk);
}

// =============================================================================
// Controller setComponentState (FR-016)
// =============================================================================

TEST_CASE("Membrum Controller setComponentState syncs parameters",
          "[membrum][vst][state]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // Build a state stream with known values
    auto* stream = new MemoryStream();
    int32 version = 1;
    stream->write(&version, sizeof(version), nullptr);

    double params[] = {0.2, 0.7, 0.1, 0.9, 0.6};
    for (double p : params)
        stream->write(&p, sizeof(p), nullptr);

    stream->seek(0, IBStream::kIBSeekSet, nullptr);
    REQUIRE(controller.setComponentState(stream) == kResultOk);
    stream->release();

    // Check that controller has the correct normalized values
    CHECK(controller.getParamNormalized(Membrum::kMaterialId) == Approx(0.2));
    CHECK(controller.getParamNormalized(Membrum::kSizeId) == Approx(0.7));
    CHECK(controller.getParamNormalized(Membrum::kDecayId) == Approx(0.1));
    CHECK(controller.getParamNormalized(Membrum::kStrikePositionId) == Approx(0.9));
    CHECK(controller.getParamNormalized(Membrum::kLevelId) == Approx(0.6));

    REQUIRE(controller.terminate() == kResultOk);
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
