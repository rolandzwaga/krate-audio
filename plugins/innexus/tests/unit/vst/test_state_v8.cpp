// ==============================================================================
// State Persistence Tests (Analysis Feedback Loop)
// ==============================================================================
// Tests that getState()/setState() correctly persist the 2 feedback loop
// parameters (FeedbackAmount, FeedbackDecay) in the flat v1 state format.
//
// Feature: 123-analysis-feedback-loop
// Tasks: T008
// Requirements: FR-017, FR-020
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "controller/controller.h"
#include "processor/processor.h"
#include "plugin_ids.h"

#include "pluginterfaces/base/ibstream.h"
#include "base/source/fstreamer.h"

#include <cstring>
#include <memory>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

// Minimal IBStream implementation for state tests
class V8TestStream : public IBStream
{
public:
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    tresult PLUGIN_API read(void* buffer, int32 numBytes, int32* numBytesRead) override
    {
        if (!buffer || numBytes <= 0) return kResultFalse;
        int32 available = static_cast<int32>(data_.size()) - readPos_;
        int32 toRead = std::min(numBytes, available);
        if (toRead <= 0) { if (numBytesRead) *numBytesRead = 0; return kResultFalse; }
        std::memcpy(buffer, data_.data() + readPos_, static_cast<size_t>(toRead));
        readPos_ += toRead;
        if (numBytesRead) *numBytesRead = toRead;
        return kResultOk;
    }

    tresult PLUGIN_API write(void* buffer, int32 numBytes, int32* numBytesWritten) override
    {
        if (!buffer || numBytes <= 0) return kResultFalse;
        auto* bytes = static_cast<const char*>(buffer);
        data_.insert(data_.end(), bytes, bytes + numBytes);
        if (numBytesWritten) *numBytesWritten = numBytes;
        return kResultOk;
    }

    tresult PLUGIN_API seek(int64 pos, int32 mode, int64* result) override
    {
        int64 newPos = 0;
        switch (mode) {
        case kIBSeekSet: newPos = pos; break;
        case kIBSeekCur: newPos = readPos_ + pos; break;
        case kIBSeekEnd: newPos = static_cast<int64>(data_.size()) + pos; break;
        default: return kResultFalse;
        }
        if (newPos < 0 || newPos > static_cast<int64>(data_.size())) return kResultFalse;
        readPos_ = static_cast<int32>(newPos);
        if (result) *result = readPos_;
        return kResultOk;
    }

    tresult PLUGIN_API tell(int64* pos) override
    {
        if (pos) *pos = readPos_;
        return kResultOk;
    }

    void resetReadPos() { readPos_ = 0; }
    [[nodiscard]] size_t size() const { return data_.size(); }
    [[nodiscard]] const std::vector<char>& data() const { return data_; }

private:
    std::vector<char> data_;
    int32 readPos_ = 0;
};

static std::unique_ptr<Innexus::Processor> createAndSetupProcessor()
{
    auto proc = std::make_unique<Innexus::Processor>();
    proc->initialize(nullptr);
    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = 128;
    setup.sampleRate = 44100.0;
    proc->setupProcessing(setup);
    proc->setActive(true);
    return proc;
}

/// Helper to build a complete v1 state blob with configurable feedback params
static void writeV1StateBlob(V8TestStream& stream,
                             float feedbackAmount = 0.0f,
                             float feedbackDecay = 0.2f,
                             float masterGain = 0.8f)
{
    IBStreamer streamer(&stream, kLittleEndian);

    // Version 1
    streamer.writeInt32(1);

    // M1 parameters
    streamer.writeFloat(100.0f);     // releaseTimeMs
    streamer.writeFloat(0.5f);       // inharmonicityAmount
    streamer.writeFloat(masterGain); // masterGain
    streamer.writeFloat(0.0f);       // bypass
    streamer.writeInt32(0);          // path length (empty)

    // M2 parameters
    streamer.writeFloat(1.0f);       // harmonicLevel (plain)
    streamer.writeFloat(1.0f);       // residualLevel (plain)
    streamer.writeFloat(0.0f);       // brightness (plain)
    streamer.writeFloat(0.0f);       // transientEmphasis (plain)
    streamer.writeInt32(0);          // residual frame count
    streamer.writeInt32(0);          // fftSize
    streamer.writeInt32(0);          // hopSize

    // M3 parameters
    streamer.writeInt32(0);          // inputSource
    streamer.writeInt32(0);          // latencyMode

    // M4 parameters
    streamer.writeInt8(static_cast<int8>(0)); // freeze
    streamer.writeFloat(0.0f);       // morphPosition
    streamer.writeInt32(0);          // harmonicFilterType
    streamer.writeFloat(0.5f);       // responsiveness

    // M5 parameters
    streamer.writeInt32(0);          // selected slot
    for (int s = 0; s < 8; ++s)
        streamer.writeInt8(static_cast<int8>(0)); // unoccupied

    // M6 parameters (31 floats -- defaults)
    streamer.writeFloat(1.0f);   // timbralBlend (default=1.0)
    streamer.writeFloat(0.0f);   // stereoSpread
    streamer.writeFloat(0.0f);   // evolutionEnable
    streamer.writeFloat(0.0f);   // evolutionSpeed
    streamer.writeFloat(0.5f);   // evolutionDepth (default=0.5)
    streamer.writeFloat(0.0f);   // evolutionMode
    streamer.writeFloat(0.0f);   // mod1Enable
    streamer.writeFloat(0.0f);   // mod1Waveform
    streamer.writeFloat(0.0f);   // mod1Rate
    streamer.writeFloat(0.0f);   // mod1Depth
    streamer.writeFloat(0.0f);   // mod1RangeStart
    streamer.writeFloat(1.0f);   // mod1RangeEnd (default=1.0)
    streamer.writeFloat(0.0f);   // mod1Target
    streamer.writeFloat(0.0f);   // mod2Enable
    streamer.writeFloat(0.0f);   // mod2Waveform
    streamer.writeFloat(0.0f);   // mod2Rate
    streamer.writeFloat(0.0f);   // mod2Depth
    streamer.writeFloat(0.0f);   // mod2RangeStart
    streamer.writeFloat(1.0f);   // mod2RangeEnd (default=1.0)
    streamer.writeFloat(0.0f);   // mod2Target
    streamer.writeFloat(0.0f);   // detuneSpread
    streamer.writeFloat(0.0f);   // blendEnable
    for (int i = 0; i < 8; ++i)
        streamer.writeFloat(0.0f); // blendSlotWeights
    streamer.writeFloat(0.0f);   // blendLiveWeight

    // Harmonic Physics parameters
    streamer.writeFloat(0.0f);   // warmth
    streamer.writeFloat(0.0f);   // coupling
    streamer.writeFloat(0.0f);   // stability
    streamer.writeFloat(0.0f);   // entropy

    // Feedback Loop parameters
    streamer.writeFloat(feedbackAmount);
    streamer.writeFloat(feedbackDecay);

    // ADSR global parameters (9 floats -- defaults)
    streamer.writeFloat(10.0f);  // adsrAttackMs
    streamer.writeFloat(100.0f); // adsrDecayMs
    streamer.writeFloat(1.0f);   // adsrSustainLevel
    streamer.writeFloat(100.0f); // adsrReleaseMs
    streamer.writeFloat(0.0f);   // adsrAmount
    streamer.writeFloat(1.0f);   // adsrTimeScale
    streamer.writeFloat(0.0f);   // adsrAttackCurve
    streamer.writeFloat(0.0f);   // adsrDecayCurve
    streamer.writeFloat(0.0f);   // adsrReleaseCurve

    // Per-slot ADSR data (8 slots x 9 floats)
    for (int s = 0; s < 8; ++s)
    {
        streamer.writeFloat(10.0f);  streamer.writeFloat(100.0f);
        streamer.writeFloat(1.0f);   streamer.writeFloat(100.0f);
        streamer.writeFloat(0.0f);   streamer.writeFloat(1.0f);
        streamer.writeFloat(0.0f);   streamer.writeFloat(0.0f);
        streamer.writeFloat(0.0f);
    }

    // Partial Count parameter
    streamer.writeFloat(0.0f);   // default = 48
}

// ==============================================================================
// T008(a): getState writes version 1
// ==============================================================================

TEST_CASE("StateV8: getState writes current version (1)",
          "[innexus][vst][state][v8][feedback]")
{
    V8TestStream stream;

    auto proc = createAndSetupProcessor();
    REQUIRE(proc->getState(&stream) == kResultOk);

    // Read back the version from the raw stream
    stream.resetReadPos();
    IBStreamer reader(&stream, kLittleEndian);
    int32 version = 0;
    REQUIRE(reader.readInt32(version));
    REQUIRE(version == 2);

    proc->setActive(false);
    proc->terminate();
}

// ==============================================================================
// T008(b): setState round-trips FeedbackAmount and FeedbackDecay
// ==============================================================================

TEST_CASE("StateV8: getState/setState round-trip preserves feedback loop parameters",
          "[innexus][vst][state][v8][feedback]")
{
    V8TestStream stream;

    // Processor A: save default state
    {
        auto procA = createAndSetupProcessor();
        REQUIRE(procA->getState(&stream) == kResultOk);
        procA->setActive(false);
        procA->terminate();
    }

    // Processor B: load state and verify feedback defaults preserved
    {
        auto procB = createAndSetupProcessor();
        stream.resetReadPos();
        REQUIRE(procB->setState(&stream) == kResultOk);

        constexpr float kTol = 1e-6f;
        REQUIRE(procB->getFeedbackAmount() == Approx(0.0f).margin(kTol));
        REQUIRE(procB->getFeedbackDecay() == Approx(0.2f).margin(kTol));

        procB->setActive(false);
        procB->terminate();
    }
}

TEST_CASE("StateV8: save/load roundtrip preserves non-default feedback values",
          "[innexus][vst][state][v8][feedback]")
{
    constexpr float kFeedbackAmount = 0.75f;
    constexpr float kFeedbackDecay = 0.6f;

    V8TestStream stream;
    writeV1StateBlob(stream, kFeedbackAmount, kFeedbackDecay);

    // Load into processor and verify all values restored
    {
        auto procB = createAndSetupProcessor();
        stream.resetReadPos();
        REQUIRE(procB->setState(&stream) == kResultOk);

        constexpr float kTol = 1e-6f;
        REQUIRE(procB->getFeedbackAmount() == Approx(kFeedbackAmount).margin(kTol));
        REQUIRE(procB->getFeedbackDecay() == Approx(kFeedbackDecay).margin(kTol));

        procB->setActive(false);
        procB->terminate();
    }
}

// ==============================================================================
// Controller setComponentState test
// ==============================================================================

TEST_CASE("StateV8: Controller setComponentState reads feedback params",
          "[innexus][vst][state][v8][feedback]")
{
    V8TestStream stream;
    writeV1StateBlob(stream, 0.65f, 0.4f);

    Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    stream.resetReadPos();
    REQUIRE(controller.setComponentState(&stream) == kResultOk);

    // Verify feedback parameters in controller
    REQUIRE(controller.getParamNormalized(Innexus::kAnalysisFeedbackId)
            == Approx(0.65).margin(0.001));
    REQUIRE(controller.getParamNormalized(Innexus::kAnalysisFeedbackDecayId)
            == Approx(0.4).margin(0.001));

    REQUIRE(controller.terminate() == kResultOk);
}

TEST_CASE("StateV8: Feedback parameters are at defaults in default state",
          "[innexus][vst][state][v8][feedback]")
{
    V8TestStream stream;
    writeV1StateBlob(stream);

    auto proc = createAndSetupProcessor();
    stream.resetReadPos();
    REQUIRE(proc->setState(&stream) == kResultOk);

    constexpr float kTol = 1e-6f;
    REQUIRE(proc->getFeedbackAmount() == Approx(0.0f).margin(kTol));
    REQUIRE(proc->getFeedbackDecay() == Approx(0.2f).margin(kTol));
    REQUIRE(proc->getMasterGain() == Approx(0.8f).margin(kTol));

    proc->setActive(false);
    proc->terminate();
}
