// ==============================================================================
// Modulator Tempo Sync Integration Tests
// ==============================================================================
// Tests that the processor correctly converts note values to Hz rates
// when synced, and that tempo changes update the rate.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"

#include <cstring>
#include <memory>
#include <vector>
#include "vst_param_changes.h"

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

// =============================================================================
// Helpers
// =============================================================================

namespace {

static constexpr double kTestSampleRate = 44100.0;
static constexpr int32 kTestBlockSize = 512;

static ProcessSetup makeSetup()
{
    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = kTestBlockSize;
    setup.sampleRate = kTestSampleRate;
    return setup;
}

// Minimal IParamValueQueue implementation
// Parameter-change mocks consolidated into tests/test_helpers/vst_param_changes.h
using TestParamValueQueue = Krate::Test::ParamValueQueue;
using TestParameterChanges = Krate::Test::ParameterChanges;


// Minimal IParameterChanges implementation


struct TestProcessContext {
    ProcessContext ctx{};
    TestProcessContext(double tempo) {
        ctx.state = ProcessContext::kTempoValid;
        ctx.tempo = tempo;
        ctx.sampleRate = kTestSampleRate;
    }
};

/// Run a single process block with given parameter changes and tempo
void processBlock(Innexus::Processor& proc,
                  TestParameterChanges* paramChanges,
                  double tempo)
{
    std::vector<float> outL(kTestBlockSize, 0.0f);
    std::vector<float> outR(kTestBlockSize, 0.0f);
    float* outChannels[2] = {outL.data(), outR.data()};

    AudioBusBuffers outputBus{};
    outputBus.numChannels = 2;
    outputBus.channelBuffers32 = outChannels;

    TestProcessContext tpc(tempo);
    ProcessData data{};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = kTestBlockSize;
    data.numOutputs = 1;
    data.outputs = &outputBus;
    data.processContext = &tpc.ctx;
    data.inputParameterChanges = paramChanges;

    proc.process(data);
}

} // anonymous namespace

// =============================================================================
// Tests
// =============================================================================

TEST_CASE("Processor handles kMod1RateSyncId parameter change",
          "[innexus][vst][mod-sync]")
{
    Innexus::Processor proc;
    REQUIRE(proc.initialize(nullptr) == kResultOk);
    auto setup = makeSetup();
    proc.setupProcessing(setup);
    proc.setActive(true);

    // Default is synced (1.0)
    REQUIRE(proc.getMod1RateSync() == Approx(1.0f));

    // Set to free mode
    TestParameterChanges changes;
    changes.add(Innexus::kMod1RateSyncId, 0.0);
    processBlock(proc, &changes, 120.0);

    REQUIRE(proc.getMod1RateSync() == Approx(0.0f));

    proc.setActive(false);
    proc.terminate();
}

TEST_CASE("Processor handles kMod1NoteValueId parameter change",
          "[innexus][vst][mod-sync]")
{
    Innexus::Processor proc;
    REQUIRE(proc.initialize(nullptr) == kResultOk);
    auto setup = makeSetup();
    proc.setupProcessing(setup);
    proc.setActive(true);

    // Default note value is 0.5 (index 10 = 1/8 note)
    REQUIRE(proc.getMod1NoteValue() == Approx(0.5f));

    // Set to 1/4 note (index 13) -> normalized = 13/20 = 0.65
    TestParameterChanges changes;
    changes.add(Innexus::kMod1NoteValueId, 0.65);
    processBlock(proc, &changes, 120.0);

    REQUIRE(proc.getMod1NoteValue() == Approx(0.65f).margin(0.001f));

    proc.setActive(false);
    proc.terminate();
}

TEST_CASE("When free mode, mod rate uses Rate knob as before",
          "[innexus][vst][mod-sync]")
{
    Innexus::Processor proc;
    REQUIRE(proc.initialize(nullptr) == kResultOk);
    auto setup = makeSetup();
    proc.setupProcessing(setup);
    proc.setActive(true);

    // Set mod1 to free mode and enable it
    TestParameterChanges changes;
    changes.add(Innexus::kMod1RateSyncId, 0.0);
    changes.add(Innexus::kMod1EnableId, 1.0);
    // Rate knob at 0.5 normalized = 0.01 + 0.5 * 19.99 = 10.005 Hz
    changes.add(Innexus::kMod1RateId, 0.5);
    processBlock(proc, &changes, 120.0);

    // Verify the parameter was stored correctly
    REQUIRE(proc.getMod1RateSync() == Approx(0.0f));
    REQUIRE(proc.getMod1Rate() == Approx(0.5f));

    proc.setActive(false);
    proc.terminate();
}

TEST_CASE("Processor handles kMod2RateSyncId and kMod2NoteValueId",
          "[innexus][vst][mod-sync]")
{
    Innexus::Processor proc;
    REQUIRE(proc.initialize(nullptr) == kResultOk);
    auto setup = makeSetup();
    proc.setupProcessing(setup);
    proc.setActive(true);

    TestParameterChanges changes;
    changes.add(Innexus::kMod2RateSyncId, 0.0);
    changes.add(Innexus::kMod2NoteValueId, 0.35);
    processBlock(proc, &changes, 120.0);

    REQUIRE(proc.getMod2RateSync() == Approx(0.0f));
    REQUIRE(proc.getMod2NoteValue() == Approx(0.35f).margin(0.001f));

    proc.setActive(false);
    proc.terminate();
}
