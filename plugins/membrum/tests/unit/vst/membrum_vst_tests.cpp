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
    int32 paramCount = controller.getParameterCount();
    CHECK(paramCount == 34);

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

    // Read back and verify
    saveStream->seek(0, IBStream::kIBSeekSet, nullptr);

    int32 version = 0;
    saveStream->read(&version, sizeof(version), nullptr);
    CHECK(version == Membrum::kCurrentStateVersion);

    double readParams[5] = {};
    for (int i = 0; i < 5; ++i)
        saveStream->read(&readParams[i], sizeof(double), nullptr);

    CHECK(readParams[0] == Approx(0.2).margin(0.0));  // Material
    CHECK(readParams[1] == Approx(0.7).margin(0.0));  // Size
    CHECK(readParams[2] == Approx(0.1).margin(0.0));  // Decay
    CHECK(readParams[3] == Approx(0.9).margin(0.0));  // StrikePosition
    CHECK(readParams[4] == Approx(0.6).margin(0.0));  // Level

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

    // Read back and verify defaults
    saveStream->seek(0, IBStream::kIBSeekSet, nullptr);

    int32 version = 0;
    saveStream->read(&version, sizeof(version), nullptr);
    CHECK(version == Membrum::kCurrentStateVersion);

    double readParams[5] = {};
    for (int i = 0; i < 5; ++i)
        saveStream->read(&readParams[i], sizeof(double), nullptr);

    CHECK(readParams[0] == Approx(0.5).margin(0.0));  // Material default
    CHECK(readParams[1] == Approx(0.5).margin(0.0));  // Size default
    CHECK(readParams[2] == Approx(0.3).margin(0.0));  // Decay default
    CHECK(readParams[3] == Approx(0.3).margin(0.0));  // StrikePosition default
    CHECK(readParams[4] == Approx(0.8).margin(0.0));  // Level default

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

    saveStream->seek(0, IBStream::kIBSeekSet, nullptr);
    int32 readVersion = 0;
    saveStream->read(&readVersion, sizeof(readVersion), nullptr);

    double readParams[5] = {};
    for (int i = 0; i < 5; ++i)
        saveStream->read(&readParams[i], sizeof(double), nullptr);

    CHECK(readParams[0] == Approx(0.2).margin(0.0));  // Material
    CHECK(readParams[1] == Approx(0.7).margin(0.0));  // Size
    CHECK(readParams[2] == Approx(0.1).margin(0.0));  // Decay
    CHECK(readParams[3] == Approx(0.9).margin(0.0));  // StrikePosition
    CHECK(readParams[4] == Approx(0.8).margin(0.0));  // Level = default

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
    auto* stream = new MemoryStream();
    int32 version = 2; // Future version
    stream->write(&version, sizeof(version), nullptr);

    double params[] = {0.15, 0.85, 0.45, 0.65, 0.35, 0.99, 0.77};
    for (double p : params)
        stream->write(&p, sizeof(p), nullptr);

    stream->seek(0, IBStream::kIBSeekSet, nullptr);

    // Must not crash -- processor should load the first 5 known params
    REQUIRE(processor.setState(stream) == kResultOk);
    stream->release();

    // Save state back and verify first 5 params loaded correctly
    auto* saveStream = new MemoryStream();
    REQUIRE(processor.getState(saveStream) == kResultOk);

    saveStream->seek(0, IBStream::kIBSeekSet, nullptr);
    int32 readVersion = 0;
    saveStream->read(&readVersion, sizeof(readVersion), nullptr);

    double readParams[5] = {};
    for (int i = 0; i < 5; ++i)
        saveStream->read(&readParams[i], sizeof(double), nullptr);

    CHECK(readParams[0] == Approx(0.15).margin(0.0));  // Material
    CHECK(readParams[1] == Approx(0.85).margin(0.0));  // Size
    CHECK(readParams[2] == Approx(0.45).margin(0.0));  // Decay
    CHECK(readParams[3] == Approx(0.65).margin(0.0));  // StrikePosition
    CHECK(readParams[4] == Approx(0.35).margin(0.0));  // Level

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

TEST_CASE("Membrum Controller createView returns nullptr",
          "[membrum][vst][ui]")
{
    Membrum::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    auto* view = controller.createView("editor");
    CHECK(view == nullptr);

    REQUIRE(controller.terminate() == kResultOk);
}
