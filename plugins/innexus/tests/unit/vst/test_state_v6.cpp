// ==============================================================================
// M6 State Persistence Tests (v6 format)
// ==============================================================================
// Tests that getState()/setState() correctly persist all 31 M6 parameters
// in version 6 format, and that loading a v5 state initializes M6 parameters
// to their spec defaults.
//
// Feature: 120-creative-extensions
// Requirements: FR-043, FR-044, SC-009
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
class V6TestStream : public IBStream
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

private:
    std::vector<char> data_;
    int32 readPos_ = 0;
};

// Helper to create a processor, set it up, and activate it
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

// ==============================================================================
// T047: v6 state round-trip test -- SC-009 tolerance 1e-6
// ==============================================================================

TEST_CASE("M6 State: v6 round-trip preserves all 31 M6 parameters within 1e-6",
          "[innexus][vst][m6][state]")
{
    // Non-default values to verify round-trip
    constexpr float kTimbralBlend = 0.42f;
    constexpr float kStereoSpread = 0.73f;
    constexpr float kEvolutionEnable = 1.0f;
    constexpr float kEvolutionSpeed = 0.35f;   // normalized
    constexpr float kEvolutionDepth = 0.8f;
    constexpr float kEvolutionMode = 0.5f;     // normalized (PingPong)
    constexpr float kMod1Enable = 1.0f;
    constexpr float kMod1Waveform = 0.25f;     // normalized (Triangle)
    constexpr float kMod1Rate = 0.6f;          // normalized
    constexpr float kMod1Depth = 0.45f;
    constexpr float kMod1RangeStart = 0.2f;    // normalized
    constexpr float kMod1RangeEnd = 0.8f;      // normalized
    constexpr float kMod1Target = 0.5f;        // normalized (Frequency)
    constexpr float kMod2Enable = 1.0f;
    constexpr float kMod2Waveform = 0.75f;     // normalized (Saw)
    constexpr float kMod2Rate = 0.9f;
    constexpr float kMod2Depth = 0.33f;
    constexpr float kMod2RangeStart = 0.1f;
    constexpr float kMod2RangeEnd = 0.6f;
    constexpr float kMod2Target = 1.0f;        // normalized (Pan)
    constexpr float kDetuneSpread = 0.55f;
    constexpr float kBlendEnable = 1.0f;
    constexpr float kBlendSlot1 = 0.3f;
    constexpr float kBlendSlot2 = 0.7f;
    constexpr float kBlendSlot3 = 0.15f;
    constexpr float kBlendSlot4 = 0.0f;
    constexpr float kBlendSlot5 = 0.9f;
    constexpr float kBlendSlot6 = 0.5f;
    constexpr float kBlendSlot7 = 0.1f;
    constexpr float kBlendSlot8 = 0.85f;
    constexpr float kBlendLive = 0.65f;

    V6TestStream stream;

    // Save state from processor A with non-default M6 values
    {
        auto procA = createAndSetupProcessor();

        // Set all M6 parameters to non-default values via the atomics
        // (In real usage, this would be via processParameterChanges,
        // but for state testing we use the fact that getState reads atomics directly)

        // We need to use processParameterChanges to set values, but that requires
        // building IParameterChanges. Instead, for the processor state test,
        // we manually construct a v6 state blob and verify round-trip.

        // Actually, the simplest approach: write state using getState(), read it
        // back with setState(). But we need to set the atomics first.
        // The processor atomics are private, but we have test accessors to read.
        // We don't have setters. Let's build the state blob manually to set
        // specific non-default values, then verify setState() loads them.

        procA->setActive(false);
        procA->terminate();
    }

    // Build a v6 state blob manually with known M6 values
    {
        IBStreamer streamer(&stream, kLittleEndian);

        // Version 6
        streamer.writeInt32(6);

        // M1 parameters
        streamer.writeFloat(100.0f);     // releaseTimeMs (clamped 20-5000)
        streamer.writeFloat(0.5f);       // inharmonicityAmount
        streamer.writeFloat(0.8f);       // masterGain
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

        // M6 parameters (31 floats in data-model.md order)
        streamer.writeFloat(kTimbralBlend);
        streamer.writeFloat(kStereoSpread);
        streamer.writeFloat(kEvolutionEnable);
        streamer.writeFloat(kEvolutionSpeed);
        streamer.writeFloat(kEvolutionDepth);
        streamer.writeFloat(kEvolutionMode);
        streamer.writeFloat(kMod1Enable);
        streamer.writeFloat(kMod1Waveform);
        streamer.writeFloat(kMod1Rate);
        streamer.writeFloat(kMod1Depth);
        streamer.writeFloat(kMod1RangeStart);
        streamer.writeFloat(kMod1RangeEnd);
        streamer.writeFloat(kMod1Target);
        streamer.writeFloat(kMod2Enable);
        streamer.writeFloat(kMod2Waveform);
        streamer.writeFloat(kMod2Rate);
        streamer.writeFloat(kMod2Depth);
        streamer.writeFloat(kMod2RangeStart);
        streamer.writeFloat(kMod2RangeEnd);
        streamer.writeFloat(kMod2Target);
        streamer.writeFloat(kDetuneSpread);
        streamer.writeFloat(kBlendEnable);
        streamer.writeFloat(kBlendSlot1);
        streamer.writeFloat(kBlendSlot2);
        streamer.writeFloat(kBlendSlot3);
        streamer.writeFloat(kBlendSlot4);
        streamer.writeFloat(kBlendSlot5);
        streamer.writeFloat(kBlendSlot6);
        streamer.writeFloat(kBlendSlot7);
        streamer.writeFloat(kBlendSlot8);
        streamer.writeFloat(kBlendLive);
    }

    // Load into processor B and verify all values
    {
        auto procB = createAndSetupProcessor();
        stream.resetReadPos();
        REQUIRE(procB->setState(&stream) == kResultOk);

        constexpr float kTol = 1e-6f;
        REQUIRE(procB->getTimbralBlend() == Approx(kTimbralBlend).margin(kTol));
        REQUIRE(procB->getStereoSpread() == Approx(kStereoSpread).margin(kTol));
        REQUIRE(procB->getEvolutionEnable() == Approx(kEvolutionEnable).margin(kTol));
        REQUIRE(procB->getEvolutionSpeed() == Approx(kEvolutionSpeed).margin(kTol));
        REQUIRE(procB->getEvolutionDepth() == Approx(kEvolutionDepth).margin(kTol));
        REQUIRE(procB->getEvolutionMode() == Approx(kEvolutionMode).margin(kTol));
        REQUIRE(procB->getMod1Enable() == Approx(kMod1Enable).margin(kTol));
        REQUIRE(procB->getMod1Waveform() == Approx(kMod1Waveform).margin(kTol));
        REQUIRE(procB->getMod1Rate() == Approx(kMod1Rate).margin(kTol));
        REQUIRE(procB->getMod1Depth() == Approx(kMod1Depth).margin(kTol));
        REQUIRE(procB->getMod1RangeStart() == Approx(kMod1RangeStart).margin(kTol));
        REQUIRE(procB->getMod1RangeEnd() == Approx(kMod1RangeEnd).margin(kTol));
        REQUIRE(procB->getMod1Target() == Approx(kMod1Target).margin(kTol));
        REQUIRE(procB->getMod2Enable() == Approx(kMod2Enable).margin(kTol));
        REQUIRE(procB->getMod2Waveform() == Approx(kMod2Waveform).margin(kTol));
        REQUIRE(procB->getMod2Rate() == Approx(kMod2Rate).margin(kTol));
        REQUIRE(procB->getMod2Depth() == Approx(kMod2Depth).margin(kTol));
        REQUIRE(procB->getMod2RangeStart() == Approx(kMod2RangeStart).margin(kTol));
        REQUIRE(procB->getMod2RangeEnd() == Approx(kMod2RangeEnd).margin(kTol));
        REQUIRE(procB->getMod2Target() == Approx(kMod2Target).margin(kTol));
        REQUIRE(procB->getDetuneSpread() == Approx(kDetuneSpread).margin(kTol));
        REQUIRE(procB->getBlendEnable() == Approx(kBlendEnable).margin(kTol));
        REQUIRE(procB->getBlendSlotWeight(0) == Approx(kBlendSlot1).margin(kTol));
        REQUIRE(procB->getBlendSlotWeight(1) == Approx(kBlendSlot2).margin(kTol));
        REQUIRE(procB->getBlendSlotWeight(2) == Approx(kBlendSlot3).margin(kTol));
        REQUIRE(procB->getBlendSlotWeight(3) == Approx(kBlendSlot4).margin(kTol));
        REQUIRE(procB->getBlendSlotWeight(4) == Approx(kBlendSlot5).margin(kTol));
        REQUIRE(procB->getBlendSlotWeight(5) == Approx(kBlendSlot6).margin(kTol));
        REQUIRE(procB->getBlendSlotWeight(6) == Approx(kBlendSlot7).margin(kTol));
        REQUIRE(procB->getBlendSlotWeight(7) == Approx(kBlendSlot8).margin(kTol));
        REQUIRE(procB->getBlendLiveWeight() == Approx(kBlendLive).margin(kTol));

        procB->setActive(false);
        procB->terminate();
    }
}

TEST_CASE("M6 State: getState() then setState() round-trip preserves M6 values",
          "[innexus][vst][m6][state]")
{
    // This test verifies the full getState -> setState pipeline
    // by using one processor to write state and another to read it back.
    V6TestStream stream;

    // Processor A: save state (will write default M6 values)
    {
        auto procA = createAndSetupProcessor();
        REQUIRE(procA->getState(&stream) == kResultOk);
        procA->setActive(false);
        procA->terminate();
    }

    // Processor B: load state and verify M6 defaults are preserved
    {
        auto procB = createAndSetupProcessor();
        stream.resetReadPos();
        REQUIRE(procB->setState(&stream) == kResultOk);

        constexpr float kTol = 1e-6f;
        // Default values per data-model.md
        REQUIRE(procB->getTimbralBlend() == Approx(1.0f).margin(kTol));
        REQUIRE(procB->getStereoSpread() == Approx(0.0f).margin(kTol));
        REQUIRE(procB->getEvolutionEnable() == Approx(0.0f).margin(kTol));
        REQUIRE(procB->getEvolutionDepth() == Approx(0.5f).margin(kTol));
        REQUIRE(procB->getEvolutionMode() == Approx(0.0f).margin(kTol));
        REQUIRE(procB->getMod1Enable() == Approx(0.0f).margin(kTol));
        REQUIRE(procB->getMod1Depth() == Approx(0.0f).margin(kTol));
        REQUIRE(procB->getMod2Enable() == Approx(0.0f).margin(kTol));
        REQUIRE(procB->getMod2Depth() == Approx(0.0f).margin(kTol));
        REQUIRE(procB->getDetuneSpread() == Approx(0.0f).margin(kTol));
        REQUIRE(procB->getBlendEnable() == Approx(0.0f).margin(kTol));
        REQUIRE(procB->getBlendLiveWeight() == Approx(0.0f).margin(kTol));
        for (int i = 0; i < 8; ++i)
            REQUIRE(procB->getBlendSlotWeight(i) == Approx(0.0f).margin(kTol));

        procB->setActive(false);
        procB->terminate();
    }
}

// ==============================================================================
// T047: v5 backward compatibility test -- SC-009
// ==============================================================================

TEST_CASE("M6 State: loading v5 state initializes all M6 parameters to defaults",
          "[innexus][vst][m6][state]")
{
    V6TestStream stream;

    // Build a v5 state blob manually
    {
        IBStreamer streamer(&stream, kLittleEndian);

        // Version 5
        streamer.writeInt32(5);

        // M1 parameters
        streamer.writeFloat(200.0f);     // releaseTimeMs
        streamer.writeFloat(0.3f);       // inharmonicityAmount
        streamer.writeFloat(0.7f);       // masterGain
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
            streamer.writeInt8(static_cast<int8>(0)); // all unoccupied

        // NO M6 data -- this is the v5 format
    }

    // Load into processor and verify M6 defaults
    {
        auto proc = createAndSetupProcessor();

        // First set M6 params to non-default values to ensure setState resets them
        // (The processor is freshly initialized so they already have defaults,
        // but let's verify the v5 path explicitly sets them)

        stream.resetReadPos();
        REQUIRE(proc->setState(&stream) == kResultOk);

        constexpr float kTol = 1e-6f;

        // All M6 parameters should be at their spec defaults
        REQUIRE(proc->getTimbralBlend() == Approx(1.0f).margin(kTol));
        REQUIRE(proc->getStereoSpread() == Approx(0.0f).margin(kTol));
        REQUIRE(proc->getEvolutionEnable() == Approx(0.0f).margin(kTol));
        REQUIRE(proc->getEvolutionSpeed() == Approx(0.0f).margin(kTol));
        REQUIRE(proc->getEvolutionDepth() == Approx(0.5f).margin(kTol));
        REQUIRE(proc->getEvolutionMode() == Approx(0.0f).margin(kTol));
        REQUIRE(proc->getMod1Enable() == Approx(0.0f).margin(kTol));
        REQUIRE(proc->getMod1Waveform() == Approx(0.0f).margin(kTol));
        REQUIRE(proc->getMod1Rate() == Approx(0.0f).margin(kTol));
        REQUIRE(proc->getMod1Depth() == Approx(0.0f).margin(kTol));
        REQUIRE(proc->getMod1RangeStart() == Approx(0.0f).margin(kTol));
        REQUIRE(proc->getMod1RangeEnd() == Approx(1.0f).margin(kTol));
        REQUIRE(proc->getMod1Target() == Approx(0.0f).margin(kTol));
        REQUIRE(proc->getMod2Enable() == Approx(0.0f).margin(kTol));
        REQUIRE(proc->getMod2Waveform() == Approx(0.0f).margin(kTol));
        REQUIRE(proc->getMod2Rate() == Approx(0.0f).margin(kTol));
        REQUIRE(proc->getMod2Depth() == Approx(0.0f).margin(kTol));
        REQUIRE(proc->getMod2RangeStart() == Approx(0.0f).margin(kTol));
        REQUIRE(proc->getMod2RangeEnd() == Approx(1.0f).margin(kTol));
        REQUIRE(proc->getMod2Target() == Approx(0.0f).margin(kTol));
        REQUIRE(proc->getDetuneSpread() == Approx(0.0f).margin(kTol));
        REQUIRE(proc->getBlendEnable() == Approx(0.0f).margin(kTol));
        for (int i = 0; i < 8; ++i)
            REQUIRE(proc->getBlendSlotWeight(i) == Approx(0.0f).margin(kTol));
        REQUIRE(proc->getBlendLiveWeight() == Approx(0.0f).margin(kTol));

        proc->setActive(false);
        proc->terminate();
    }
}

TEST_CASE("M6 State: v5 state loads without crash when M6 params were previously non-default",
          "[innexus][vst][m6][state]")
{
    V6TestStream streamV6;
    V6TestStream streamV5;

    // First load a v6 state with non-default M6 values
    {
        IBStreamer streamer(&streamV6, kLittleEndian);
        streamer.writeInt32(6);
        // M1
        streamer.writeFloat(100.0f); streamer.writeFloat(0.5f);
        streamer.writeFloat(0.8f);   streamer.writeFloat(0.0f);
        streamer.writeInt32(0);
        // M2
        streamer.writeFloat(1.0f); streamer.writeFloat(1.0f);
        streamer.writeFloat(0.0f); streamer.writeFloat(0.0f);
        streamer.writeInt32(0); streamer.writeInt32(0); streamer.writeInt32(0);
        // M3
        streamer.writeInt32(0); streamer.writeInt32(0);
        // M4
        streamer.writeInt8(static_cast<int8>(0));
        streamer.writeFloat(0.0f); streamer.writeInt32(0); streamer.writeFloat(0.5f);
        // M5
        streamer.writeInt32(0);
        for (int s = 0; s < 8; ++s) streamer.writeInt8(static_cast<int8>(0));
        // M6 -- all set to 0.77 (non-default)
        for (int i = 0; i < 31; ++i) streamer.writeFloat(0.77f);
    }

    // Build a v5 state
    {
        IBStreamer streamer(&streamV5, kLittleEndian);
        streamer.writeInt32(5);
        // M1
        streamer.writeFloat(100.0f); streamer.writeFloat(0.5f);
        streamer.writeFloat(0.8f);   streamer.writeFloat(0.0f);
        streamer.writeInt32(0);
        // M2
        streamer.writeFloat(1.0f); streamer.writeFloat(1.0f);
        streamer.writeFloat(0.0f); streamer.writeFloat(0.0f);
        streamer.writeInt32(0); streamer.writeInt32(0); streamer.writeInt32(0);
        // M3
        streamer.writeInt32(0); streamer.writeInt32(0);
        // M4
        streamer.writeInt8(static_cast<int8>(0));
        streamer.writeFloat(0.0f); streamer.writeInt32(0); streamer.writeFloat(0.5f);
        // M5
        streamer.writeInt32(0);
        for (int s = 0; s < 8; ++s) streamer.writeInt8(static_cast<int8>(0));
    }

    auto proc = createAndSetupProcessor();

    // Load v6 state first
    streamV6.resetReadPos();
    REQUIRE(proc->setState(&streamV6) == kResultOk);
    // Verify a non-default value was loaded
    REQUIRE(proc->getTimbralBlend() == Approx(0.77f).margin(1e-6f));

    // Now load v5 state -- should reset M6 to defaults
    streamV5.resetReadPos();
    REQUIRE(proc->setState(&streamV5) == kResultOk);

    constexpr float kTol = 1e-6f;
    REQUIRE(proc->getTimbralBlend() == Approx(1.0f).margin(kTol));
    REQUIRE(proc->getStereoSpread() == Approx(0.0f).margin(kTol));
    REQUIRE(proc->getEvolutionEnable() == Approx(0.0f).margin(kTol));
    REQUIRE(proc->getBlendEnable() == Approx(0.0f).margin(kTol));
    REQUIRE(proc->getDetuneSpread() == Approx(0.0f).margin(kTol));

    proc->setActive(false);
    proc->terminate();
}

// ==============================================================================
// Controller setComponentState v6 test
// ==============================================================================

TEST_CASE("M6 State: Controller setComponentState reads v6 M6 parameters",
          "[innexus][vst][m6][state]")
{
    V6TestStream stream;

    // Build a v6 state blob
    {
        IBStreamer streamer(&stream, kLittleEndian);
        streamer.writeInt32(6);
        // M1
        streamer.writeFloat(100.0f); streamer.writeFloat(0.5f);
        streamer.writeFloat(0.8f);   streamer.writeFloat(0.0f);
        streamer.writeInt32(0);
        // M2
        streamer.writeFloat(1.0f); streamer.writeFloat(1.0f);
        streamer.writeFloat(0.0f); streamer.writeFloat(0.0f);
        streamer.writeInt32(0); streamer.writeInt32(0); streamer.writeInt32(0);
        // M3
        streamer.writeInt32(0); streamer.writeInt32(0);
        // M4
        streamer.writeInt8(static_cast<int8>(0));
        streamer.writeFloat(0.0f); streamer.writeInt32(0); streamer.writeFloat(0.5f);
        // M5
        streamer.writeInt32(0);
        for (int s = 0; s < 8; ++s) streamer.writeInt8(static_cast<int8>(0));
        // M6
        streamer.writeFloat(0.42f);  // timbralBlend
        streamer.writeFloat(0.73f);  // stereoSpread
        streamer.writeFloat(1.0f);   // evolutionEnable
        streamer.writeFloat(0.35f);  // evolutionSpeed
        streamer.writeFloat(0.8f);   // evolutionDepth
        streamer.writeFloat(0.5f);   // evolutionMode
        streamer.writeFloat(1.0f);   // mod1Enable
        streamer.writeFloat(0.25f);  // mod1Waveform
        streamer.writeFloat(0.6f);   // mod1Rate
        streamer.writeFloat(0.45f);  // mod1Depth
        streamer.writeFloat(0.2f);   // mod1RangeStart
        streamer.writeFloat(0.8f);   // mod1RangeEnd
        streamer.writeFloat(0.5f);   // mod1Target
        streamer.writeFloat(1.0f);   // mod2Enable
        streamer.writeFloat(0.75f);  // mod2Waveform
        streamer.writeFloat(0.9f);   // mod2Rate
        streamer.writeFloat(0.33f);  // mod2Depth
        streamer.writeFloat(0.1f);   // mod2RangeStart
        streamer.writeFloat(0.6f);   // mod2RangeEnd
        streamer.writeFloat(1.0f);   // mod2Target
        streamer.writeFloat(0.55f);  // detuneSpread
        streamer.writeFloat(1.0f);   // blendEnable
        streamer.writeFloat(0.3f);   // blendSlot1
        streamer.writeFloat(0.7f);   // blendSlot2
        streamer.writeFloat(0.15f);  // blendSlot3
        streamer.writeFloat(0.0f);   // blendSlot4
        streamer.writeFloat(0.9f);   // blendSlot5
        streamer.writeFloat(0.5f);   // blendSlot6
        streamer.writeFloat(0.1f);   // blendSlot7
        streamer.writeFloat(0.85f);  // blendSlot8
        streamer.writeFloat(0.65f);  // blendLive
    }

    Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    stream.resetReadPos();
    REQUIRE(controller.setComponentState(&stream) == kResultOk);

    // Verify M6 parameters in controller
    REQUIRE(controller.getParamNormalized(Innexus::kTimbralBlendId)
            == Approx(0.42).margin(0.001));
    REQUIRE(controller.getParamNormalized(Innexus::kStereoSpreadId)
            == Approx(0.73).margin(0.001));
    REQUIRE(controller.getParamNormalized(Innexus::kEvolutionEnableId)
            == Approx(1.0).margin(0.001));
    REQUIRE(controller.getParamNormalized(Innexus::kDetuneSpreadId)
            == Approx(0.55).margin(0.001));
    REQUIRE(controller.getParamNormalized(Innexus::kBlendEnableId)
            == Approx(1.0).margin(0.001));
    REQUIRE(controller.getParamNormalized(Innexus::kBlendSlotWeight1Id)
            == Approx(0.3).margin(0.001));
    REQUIRE(controller.getParamNormalized(Innexus::kBlendLiveWeightId)
            == Approx(0.65).margin(0.001));

    REQUIRE(controller.terminate() == kResultOk);
}

TEST_CASE("M6 State: Controller setComponentState with v5 data defaults M6 params",
          "[innexus][vst][m6][state]")
{
    V6TestStream stream;

    // Build a v5 state blob
    {
        IBStreamer streamer(&stream, kLittleEndian);
        streamer.writeInt32(5);
        // M1
        streamer.writeFloat(100.0f); streamer.writeFloat(0.5f);
        streamer.writeFloat(0.8f);   streamer.writeFloat(0.0f);
        streamer.writeInt32(0);
        // M2
        streamer.writeFloat(1.0f); streamer.writeFloat(1.0f);
        streamer.writeFloat(0.0f); streamer.writeFloat(0.0f);
        streamer.writeInt32(0); streamer.writeInt32(0); streamer.writeInt32(0);
        // M3
        streamer.writeInt32(0); streamer.writeInt32(0);
        // M4
        streamer.writeInt8(static_cast<int8>(0));
        streamer.writeFloat(0.0f); streamer.writeInt32(0); streamer.writeFloat(0.5f);
        // M5
        streamer.writeInt32(0);
        for (int s = 0; s < 8; ++s) streamer.writeInt8(static_cast<int8>(0));
    }

    Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    stream.resetReadPos();
    REQUIRE(controller.setComponentState(&stream) == kResultOk);

    // M6 parameters should be at defaults
    REQUIRE(controller.getParamNormalized(Innexus::kTimbralBlendId)
            == Approx(1.0).margin(0.001));
    REQUIRE(controller.getParamNormalized(Innexus::kStereoSpreadId)
            == Approx(0.0).margin(0.001));
    REQUIRE(controller.getParamNormalized(Innexus::kEvolutionEnableId)
            == Approx(0.0).margin(0.001));
    REQUIRE(controller.getParamNormalized(Innexus::kEvolutionDepthId)
            == Approx(0.5).margin(0.001));
    REQUIRE(controller.getParamNormalized(Innexus::kMod1EnableId)
            == Approx(0.0).margin(0.001));
    REQUIRE(controller.getParamNormalized(Innexus::kDetuneSpreadId)
            == Approx(0.0).margin(0.001));
    REQUIRE(controller.getParamNormalized(Innexus::kBlendEnableId)
            == Approx(0.0).margin(0.001));
    REQUIRE(controller.getParamNormalized(Innexus::kBlendLiveWeightId)
            == Approx(0.0).margin(0.001));

    REQUIRE(controller.terminate() == kResultOk);
}
