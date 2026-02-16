// ==============================================================================
// Unit Test: Macro Parameter Handling and State Persistence
// ==============================================================================
// Verifies that macro parameters are correctly handled, formatted, and
// persisted through save/load cycles.
//
// Reference: specs/057-macros-rungler/spec.md FR-004, FR-006, FR-011
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"
#include "parameters/macro_params.h"

#include "public.sdk/source/common/memorystream.h"
#include "base/source/fstreamer.h"

using Catch::Approx;

// =============================================================================
// Helper: create and initialize a Processor
// =============================================================================

namespace {

class TestableProcessor : public Ruinae::Processor {
public:
    using Ruinae::Processor::processParameterChanges;
};

std::unique_ptr<TestableProcessor> makeTestableProcessor() {
    auto p = std::make_unique<TestableProcessor>();
    p->initialize(nullptr);

    Steinberg::Vst::ProcessSetup setup{};
    setup.processMode = Steinberg::Vst::kRealtime;
    setup.symbolicSampleSize = Steinberg::Vst::kSample32;
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 512;
    p->setupProcessing(setup);

    return p;
}

} // anonymous namespace

// =============================================================================
// T036: Macro parameter changes update engine
// =============================================================================

TEST_CASE("Macro parameter handle/format functions", "[macro_params]") {
    SECTION("handleMacroParamChange stores correct values") {
        Ruinae::MacroParams params;

        Ruinae::handleMacroParamChange(params, Ruinae::kMacro1ValueId, 0.25);
        Ruinae::handleMacroParamChange(params, Ruinae::kMacro2ValueId, 0.50);
        Ruinae::handleMacroParamChange(params, Ruinae::kMacro3ValueId, 0.75);
        Ruinae::handleMacroParamChange(params, Ruinae::kMacro4ValueId, 1.0);

        REQUIRE(params.values[0].load() == Approx(0.25f));
        REQUIRE(params.values[1].load() == Approx(0.50f));
        REQUIRE(params.values[2].load() == Approx(0.75f));
        REQUIRE(params.values[3].load() == Approx(1.0f));
    }

    SECTION("handleMacroParamChange clamps to [0,1]") {
        Ruinae::MacroParams params;

        Ruinae::handleMacroParamChange(params, Ruinae::kMacro1ValueId, -0.5);
        REQUIRE(params.values[0].load() == Approx(0.0f));

        Ruinae::handleMacroParamChange(params, Ruinae::kMacro1ValueId, 1.5);
        REQUIRE(params.values[0].load() == Approx(1.0f));
    }

    SECTION("formatMacroParam produces percentage string") {
        Steinberg::Vst::String128 str{};

        auto result = Ruinae::formatMacroParam(Ruinae::kMacro1ValueId, 0.75, str);
        REQUIRE(result == Steinberg::kResultOk);

        // Verify the formatted string
        char ascii[128];
        Steinberg::UString(str, 128).toAscii(ascii, 128);
        REQUIRE(std::string(ascii) == "75%");
    }

    SECTION("formatMacroParam returns kResultFalse for non-macro IDs") {
        Steinberg::Vst::String128 str{};
        auto result = Ruinae::formatMacroParam(Ruinae::kMasterGainId, 0.5, str);
        REQUIRE(result == Steinberg::kResultFalse);
    }
}

// =============================================================================
// T037: Macro params save and load
// =============================================================================

TEST_CASE("Macro params save and load round-trip", "[macro_params][state_persistence]") {
    Ruinae::MacroParams params;

    // Set non-default values
    params.values[0].store(0.25f, std::memory_order_relaxed);
    params.values[1].store(0.50f, std::memory_order_relaxed);
    params.values[2].store(0.75f, std::memory_order_relaxed);
    params.values[3].store(0.0f, std::memory_order_relaxed);

    // Save to stream
    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    {
        Steinberg::IBStreamer streamer(stream, kLittleEndian);
        Ruinae::saveMacroParams(params, streamer);
    }

    // Reset params to defaults
    params.values[0].store(0.0f, std::memory_order_relaxed);
    params.values[1].store(0.0f, std::memory_order_relaxed);
    params.values[2].store(0.0f, std::memory_order_relaxed);
    params.values[3].store(0.0f, std::memory_order_relaxed);

    // Load from stream
    stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
    {
        Steinberg::IBStreamer streamer(stream, kLittleEndian);
        bool ok = Ruinae::loadMacroParams(params, streamer);
        REQUIRE(ok);
    }

    // Verify restored values
    REQUIRE(params.values[0].load() == Approx(0.25f));
    REQUIRE(params.values[1].load() == Approx(0.50f));
    REQUIRE(params.values[2].load() == Approx(0.75f));
    REQUIRE(params.values[3].load() == Approx(0.0f));
}

TEST_CASE("Macro params controller load maps values correctly", "[macro_params][state_persistence]") {
    Ruinae::MacroParams params;
    params.values[0].store(0.33f, std::memory_order_relaxed);
    params.values[1].store(0.66f, std::memory_order_relaxed);
    params.values[2].store(0.99f, std::memory_order_relaxed);
    params.values[3].store(0.01f, std::memory_order_relaxed);

    auto stream = Steinberg::owned(new Steinberg::MemoryStream());
    {
        Steinberg::IBStreamer streamer(stream, kLittleEndian);
        Ruinae::saveMacroParams(params, streamer);
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
        Ruinae::loadMacroParamsToController(streamer,
            [&](Steinberg::Vst::ParamID id, double value) {
                calls.push_back({id, value});
            });
    }

    REQUIRE(calls.size() == 4);
    REQUIRE(calls[0].id == Ruinae::kMacro1ValueId);
    REQUIRE(calls[0].value == Approx(0.33).margin(0.001));
    REQUIRE(calls[1].id == Ruinae::kMacro2ValueId);
    REQUIRE(calls[1].value == Approx(0.66).margin(0.001));
    REQUIRE(calls[2].id == Ruinae::kMacro3ValueId);
    REQUIRE(calls[2].value == Approx(0.99).margin(0.001));
    REQUIRE(calls[3].id == Ruinae::kMacro4ValueId);
    REQUIRE(calls[3].value == Approx(0.01).margin(0.001));
}
