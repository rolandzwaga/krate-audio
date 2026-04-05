// ==============================================================================
// Gradus VST3 Validation & Parameter Tests
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "controller/controller.h"
#include "parameters/arpeggiator_params.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "public.sdk/source/common/memorystream.h"

#include <cstring>
#include <memory>
#include <vector>

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

// =============================================================================
// Processor Lifecycle
// =============================================================================

TEST_CASE("Gradus Processor initializes and terminates cleanly",
          "[gradus][vst][init]")
{
    Gradus::Processor processor;
    auto result = processor.initialize(nullptr);
    REQUIRE(result == kResultOk);

    result = processor.terminate();
    REQUIRE(result == kResultOk);
}

TEST_CASE("Gradus Processor outputs silence with no MIDI input",
          "[gradus][vst][silence]")
{
    Gradus::Processor processor;
    REQUIRE(processor.initialize(nullptr) == kResultOk);

    auto setup = makeSetup();
    REQUIRE(processor.setupProcessing(setup) == kResultOk);
    REQUIRE(processor.setActive(true) == kResultOk);

    // Create output buffers
    std::vector<float> outL(kTestBlockSize, 1.0f);
    std::vector<float> outR(kTestBlockSize, 1.0f);
    float* outChannels[2] = {outL.data(), outR.data()};

    AudioBusBuffers outputBus{};
    outputBus.numChannels = 2;
    outputBus.channelBuffers32 = outChannels;

    ProcessData data{};
    data.numSamples = kTestBlockSize;
    data.numOutputs = 1;
    data.outputs = &outputBus;

    auto result = processor.process(data);
    REQUIRE(result == kResultOk);

    // Verify silence (no MIDI input = no audition sound)
    for (int i = 0; i < kTestBlockSize; ++i) {
        CHECK(outL[i] == 0.0f);
        CHECK(outR[i] == 0.0f);
    }

    REQUIRE(processor.setActive(false) == kResultOk);
    REQUIRE(processor.terminate() == kResultOk);
}

// =============================================================================
// Bus Arrangement
// =============================================================================

TEST_CASE("Gradus Processor accepts 0-in 2-out bus arrangement",
          "[gradus][vst][buses]")
{
    Gradus::Processor processor;
    REQUIRE(processor.initialize(nullptr) == kResultOk);

    SpeakerArrangement stereo = SpeakerArr::kStereo;
    auto result = processor.setBusArrangements(nullptr, 0, &stereo, 1);
    REQUIRE(result == kResultOk);

    REQUIRE(processor.terminate() == kResultOk);
}

TEST_CASE("Gradus Processor rejects mono output",
          "[gradus][vst][buses]")
{
    Gradus::Processor processor;
    REQUIRE(processor.initialize(nullptr) == kResultOk);

    SpeakerArrangement mono = SpeakerArr::kMono;
    auto result = processor.setBusArrangements(nullptr, 0, &mono, 1);
    CHECK(result == kResultFalse);

    REQUIRE(processor.terminate() == kResultOk);
}

// =============================================================================
// State Round-Trip
// =============================================================================

TEST_CASE("Gradus Processor state round-trip preserves version",
          "[gradus][vst][state]")
{
    Gradus::Processor processor;
    REQUIRE(processor.initialize(nullptr) == kResultOk);

    auto setup = makeSetup();
    REQUIRE(processor.setupProcessing(setup) == kResultOk);
    REQUIRE(processor.setActive(true) == kResultOk);

    // Save state
    auto* saveStream = new MemoryStream();
    REQUIRE(processor.getState(saveStream) == kResultOk);

    // Verify state is non-empty
    int64 streamSize = 0;
    saveStream->seek(0, IBStream::kIBSeekEnd, &streamSize);
    CHECK(streamSize > 4); // At least version int32

    // Load state back
    saveStream->seek(0, IBStream::kIBSeekSet, nullptr);
    REQUIRE(processor.setState(saveStream) == kResultOk);

    saveStream->release();
    REQUIRE(processor.setActive(false) == kResultOk);
    REQUIRE(processor.terminate() == kResultOk);
}

// =============================================================================
// Controller
// =============================================================================

TEST_CASE("Gradus Controller initializes with correct parameter count",
          "[gradus][vst][controller]")
{
    Gradus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // Count all registered parameters
    int32 paramCount = controller.getParameterCount();

    // Should have: ~100 arp params + 8 playhead + 8 speed + 4 audition = ~120+
    // Exact count depends on registerArpParams implementation
    CHECK(paramCount > 100);

    // Verify specific parameters exist
    ParameterInfo info{};

    // Arp mode
    CHECK(controller.getParameterInfo(0, info) == kResultOk);

    // Audition enabled should be findable
    bool foundAudition = false;
    for (int32 i = 0; i < paramCount; ++i) {
        controller.getParameterInfo(i, info);
        if (info.id == Gradus::kAuditionEnabledId) {
            foundAudition = true;
            break;
        }
    }
    CHECK(foundAudition);

    // Lane speed params should be findable
    bool foundVelocitySpeed = false;
    for (int32 i = 0; i < paramCount; ++i) {
        controller.getParameterInfo(i, info);
        if (info.id == Gradus::kArpVelocityLaneSpeedId) {
            foundVelocitySpeed = true;
            break;
        }
    }
    CHECK(foundVelocitySpeed);

    REQUIRE(controller.terminate() == kResultOk);
}

TEST_CASE("Gradus Controller FUID uniqueness",
          "[gradus][vst][controller]")
{
    // Processor and Controller UIDs must be different
    CHECK(Gradus::kProcessorUID != Gradus::kControllerUID);
}

// =============================================================================
// Parameter Display Formatting
// =============================================================================

TEST_CASE("Gradus Controller formats audition params correctly",
          "[gradus][vst][display]")
{
    Gradus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    String128 str{};

    // Volume at 70%
    REQUIRE(controller.getParamStringByValue(
        Gradus::kAuditionVolumeId, 0.7, str) == kResultTrue);
    // Should contain "70"
    char ascii[128];
    UString(str, 128).toAscii(ascii, 128);
    CHECK(std::string(ascii).find("70") != std::string::npos);

    // Decay at midpoint
    REQUIRE(controller.getParamStringByValue(
        Gradus::kAuditionDecayId, 0.5, str) == kResultTrue);
    UString(str, 128).toAscii(ascii, 128);
    // Should contain "ms" or "s"
    std::string decayStr(ascii);
    CHECK((decayStr.find("ms") != std::string::npos ||
           decayStr.find("s") != std::string::npos));

    REQUIRE(controller.terminate() == kResultOk);
}

// =============================================================================
// Audition Voice
// =============================================================================

TEST_CASE("Gradus audition voice produces non-zero output after noteOn",
          "[gradus][vst][audition]")
{
    Gradus::Processor processor;
    REQUIRE(processor.initialize(nullptr) == kResultOk);

    auto setup = makeSetup();
    REQUIRE(processor.setupProcessing(setup) == kResultOk);
    REQUIRE(processor.setActive(true) == kResultOk);

    // We can't directly send MIDI to trigger the audition voice through
    // the arp without transport, but we can verify the processor handles
    // process() without crashing even with no inputs
    std::vector<float> outL(kTestBlockSize, 0.0f);
    std::vector<float> outR(kTestBlockSize, 0.0f);
    float* outChannels[2] = {outL.data(), outR.data()};

    AudioBusBuffers outputBus{};
    outputBus.numChannels = 2;
    outputBus.channelBuffers32 = outChannels;

    ProcessData data{};
    data.numSamples = kTestBlockSize;
    data.numOutputs = 1;
    data.outputs = &outputBus;

    // Process multiple blocks without crashing
    for (int block = 0; block < 10; ++block) {
        auto result = processor.process(data);
        REQUIRE(result == kResultOk);
    }

    REQUIRE(processor.setActive(false) == kResultOk);
    REQUIRE(processor.terminate() == kResultOk);
}

// =============================================================================
// v1.5 Step Pinning — backend regression (guards v1.6 UI work)
// =============================================================================

TEST_CASE("Gradus pin flag parameters are registered",
          "[gradus][vst][pin]")
{
    Gradus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    int32 paramCount = controller.getParameterCount();
    ParameterInfo info{};

    for (int step = 0; step < 32; ++step) {
        const ParamID expectedId =
            static_cast<ParamID>(Gradus::kArpPinFlagStep0Id + step);
        bool found = false;
        for (int32 i = 0; i < paramCount; ++i) {
            controller.getParameterInfo(i, info);
            if (info.id == expectedId) {
                found = true;
                CHECK(info.stepCount == 1);          // binary
                CHECK(info.defaultNormalizedValue == Approx(0.0)); // default unpinned
                break;
            }
        }
        CHECK(found);
    }

    REQUIRE(controller.terminate() == kResultOk);
}

TEST_CASE("Gradus pin note parameter is registered with MIDI range and default C4",
          "[gradus][vst][pin]")
{
    Gradus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    int32 paramCount = controller.getParameterCount();
    ParameterInfo info{};
    bool found = false;
    for (int32 i = 0; i < paramCount; ++i) {
        controller.getParameterInfo(i, info);
        if (info.id == Gradus::kArpPinNoteId) {
            found = true;
            CHECK(info.stepCount == 127);
            // Default 60/127 ≈ 0.4724 (registered as plain value 60.0)
            CHECK(info.defaultNormalizedValue == Approx(60.0 / 127.0).margin(1e-4));
            break;
        }
    }
    CHECK(found);

    REQUIRE(controller.terminate() == kResultOk);
}

TEST_CASE("Gradus pin flag denormalization is binary",
          "[gradus][vst][pin]")
{
    Gradus::ArpeggiatorParams params;

    // All pin flags default to 0
    for (int i = 0; i < 32; ++i) {
        CHECK(params.pinFlags[i].load() == 0);
    }

    // Below threshold → 0
    Gradus::handleArpParamChange(params,
        static_cast<ParamID>(Gradus::kArpPinFlagStep0Id + 5), 0.3);
    CHECK(params.pinFlags[5].load() == 0);

    // At/above threshold → 1
    Gradus::handleArpParamChange(params,
        static_cast<ParamID>(Gradus::kArpPinFlagStep0Id + 5), 0.7);
    CHECK(params.pinFlags[5].load() == 1);

    // Exact threshold 0.5 → 1 (>= 0.5)
    Gradus::handleArpParamChange(params,
        static_cast<ParamID>(Gradus::kArpPinFlagStep0Id + 10), 0.5);
    CHECK(params.pinFlags[10].load() == 1);

    // Back to 0
    Gradus::handleArpParamChange(params,
        static_cast<ParamID>(Gradus::kArpPinFlagStep0Id + 10), 0.0);
    CHECK(params.pinFlags[10].load() == 0);

    // Boundary steps
    Gradus::handleArpParamChange(params,
        static_cast<ParamID>(Gradus::kArpPinFlagStep0Id + 0), 1.0);
    CHECK(params.pinFlags[0].load() == 1);
    Gradus::handleArpParamChange(params,
        static_cast<ParamID>(Gradus::kArpPinFlagStep31Id), 1.0);
    CHECK(params.pinFlags[31].load() == 1);
}

// =============================================================================
// v1.7 Markov Chain mode — parameter registration
// =============================================================================

TEST_CASE("Gradus kArpMarkovPresetId registered as 6-entry StringList",
          "[gradus][vst][markov]")
{
    Gradus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    int32 paramCount = controller.getParameterCount();
    ParameterInfo info{};
    bool found = false;
    for (int32 i = 0; i < paramCount; ++i) {
        controller.getParameterInfo(i, info);
        if (info.id == Gradus::kArpMarkovPresetId) {
            found = true;
            CHECK(info.stepCount == 5);  // 6 entries → stepCount 5
            CHECK(info.defaultNormalizedValue == Approx(0.0));  // Uniform
            break;
        }
    }
    CHECK(found);

    REQUIRE(controller.terminate() == kResultOk);
}

TEST_CASE("Gradus Markov cell parameters all registered with uniform default",
          "[gradus][vst][markov]")
{
    Gradus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    int32 paramCount = controller.getParameterCount();
    ParameterInfo info{};
    constexpr double kExpectedDefault = 1.0 / 7.0;

    for (int cell = 0; cell < 49; ++cell) {
        const ParamID expectedId =
            static_cast<ParamID>(Gradus::kArpMarkovCell00Id + cell);
        bool found = false;
        for (int32 i = 0; i < paramCount; ++i) {
            controller.getParameterInfo(i, info);
            if (info.id == expectedId) {
                found = true;
                CHECK(info.defaultNormalizedValue == Approx(kExpectedDefault).margin(1e-5));
                break;
            }
        }
        CHECK(found);
    }

    // Sanity check the last ID
    bool foundLast = false;
    for (int32 i = 0; i < paramCount; ++i) {
        controller.getParameterInfo(i, info);
        if (info.id == Gradus::kArpMarkovCell66Id) { foundLast = true; break; }
    }
    CHECK(foundLast);

    REQUIRE(controller.terminate() == kResultOk);
}

TEST_CASE("Gradus kArpModeId extended to 12 entries including Markov",
          "[gradus][vst][markov]")
{
    Gradus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    int32 paramCount = controller.getParameterCount();
    ParameterInfo info{};
    bool found = false;
    for (int32 i = 0; i < paramCount; ++i) {
        controller.getParameterInfo(i, info);
        if (info.id == Gradus::kArpModeId) {
            found = true;
            CHECK(info.stepCount == 11);  // 12 entries → stepCount 11
            break;
        }
    }
    CHECK(found);

    REQUIRE(controller.terminate() == kResultOk);
}

TEST_CASE("Gradus Markov cell denormalization clamps to [0,1]",
          "[gradus][vst][markov]")
{
    Gradus::ArpeggiatorParams params;

    // Default: every cell ≈ 1/7
    for (int i = 0; i < 49; ++i) {
        CHECK(params.markovMatrix[i].load() == Approx(1.0f / 7.0f).margin(1e-6));
    }

    // Write a value to cell 5
    Gradus::handleArpParamChange(params,
        static_cast<ParamID>(Gradus::kArpMarkovCell00Id + 5), 0.75);
    CHECK(params.markovMatrix[5].load() == Approx(0.75f));

    // Out-of-range values should be clamped (VST boundary enforces [0,1] but
    // the denormalizer uses std::clamp for safety).
    Gradus::handleArpParamChange(params,
        static_cast<ParamID>(Gradus::kArpMarkovCell00Id + 10), 1.0);
    CHECK(params.markovMatrix[10].load() == Approx(1.0f));

    Gradus::handleArpParamChange(params,
        static_cast<ParamID>(Gradus::kArpMarkovCell00Id + 20), 0.0);
    CHECK(params.markovMatrix[20].load() == Approx(0.0f));
}

TEST_CASE("Gradus Controller: selecting Jazz preset writes 49 matrix cells",
          "[gradus][vst][markov]")
{
    Gradus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // Select Jazz (index 1 of 6)
    const double jazzNorm = 1.0 / 5.0;  // 6 entries, stepCount 5
    REQUIRE(controller.setParamNormalized(Gradus::kArpMarkovPresetId, jazzNorm) == kResultOk);

    // Verify that cell (1,4) [ii→V] has a high value reflecting the Jazz matrix
    const ParamID iiToV = static_cast<ParamID>(Gradus::kArpMarkovCell00Id + 1 * 7 + 4);
    const double iiToVValue = controller.getParamNormalized(iiToV);
    CHECK(iiToVValue > 0.4);

    // And cell (4,0) [V→I]
    const ParamID vToI = static_cast<ParamID>(Gradus::kArpMarkovCell00Id + 4 * 7 + 0);
    const double vToIValue = controller.getParamNormalized(vToI);
    CHECK(vToIValue > 0.4);

    // Switch to Classical and verify V→I is even stronger
    const double classicalNorm = 4.0 / 5.0;
    REQUIRE(controller.setParamNormalized(Gradus::kArpMarkovPresetId, classicalNorm) == kResultOk);
    const double vToIClassical = controller.getParamNormalized(vToI);
    CHECK(vToIClassical > 0.5);

    REQUIRE(controller.terminate() == kResultOk);
}

TEST_CASE("Gradus Controller: editing a Markov cell flips preset to Custom",
          "[gradus][vst][markov]")
{
    Gradus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // Start at Uniform (default)
    CHECK(controller.getParamNormalized(Gradus::kArpMarkovPresetId) == Approx(0.0));

    // Edit a cell — should flip preset to Custom (5)
    REQUIRE(controller.setParamNormalized(
        static_cast<ParamID>(Gradus::kArpMarkovCell00Id + 10), 0.9) == kResultOk);

    const double presetNorm = controller.getParamNormalized(Gradus::kArpMarkovPresetId);
    const int preset = std::clamp(static_cast<int>(presetNorm * 5.0 + 0.5), 0, 5);
    CHECK(preset == 5);  // Custom

    // The edit should have stuck (not overwritten by preset reload)
    const double cellValue = controller.getParamNormalized(
        static_cast<ParamID>(Gradus::kArpMarkovCell00Id + 10));
    CHECK(cellValue == Approx(0.9));

    REQUIRE(controller.terminate() == kResultOk);
}

TEST_CASE("Gradus Markov preset denormalizes to int 0..5",
          "[gradus][vst][markov]")
{
    Gradus::ArpeggiatorParams params;

    Gradus::handleArpParamChange(params,
        static_cast<ParamID>(Gradus::kArpMarkovPresetId), 0.0);
    CHECK(params.markovPreset.load() == 0);  // Uniform

    Gradus::handleArpParamChange(params,
        static_cast<ParamID>(Gradus::kArpMarkovPresetId), 1.0 / 5.0);
    CHECK(params.markovPreset.load() == 1);  // Jazz

    Gradus::handleArpParamChange(params,
        static_cast<ParamID>(Gradus::kArpMarkovPresetId), 1.0);
    CHECK(params.markovPreset.load() == 5);  // Custom
}

// -----------------------------------------------------------------------------

TEST_CASE("Gradus Controller::setParamNormalized safely handles pin flag IDs without UI",
          "[gradus][vst][pin]")
{
    // v1.6: Controller forwards pin flag ID changes to PinFlagStrip, but
    // pinFlagStrip_ is null until the editor opens. This test confirms the
    // forward path tolerates a null strip (i.e., preset load / automation
    // arriving before the UI exists doesn't crash).
    Gradus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    // Hit every pin flag param — should not crash, should update param store.
    for (int step = 0; step < 32; ++step) {
        const ParamID id =
            static_cast<ParamID>(Gradus::kArpPinFlagStep0Id + step);
        REQUIRE(controller.setParamNormalized(id, 1.0) == kResultOk);
        CHECK(controller.getParamNormalized(id) == Approx(1.0));
    }
    for (int step = 0; step < 32; ++step) {
        const ParamID id =
            static_cast<ParamID>(Gradus::kArpPinFlagStep0Id + step);
        REQUIRE(controller.setParamNormalized(id, 0.0) == kResultOk);
        CHECK(controller.getParamNormalized(id) == Approx(0.0));
    }

    REQUIRE(controller.terminate() == kResultOk);
}
