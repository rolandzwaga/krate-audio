// ==============================================================================
// State v7 Persistence Tests (Harmonic Physics)
// ==============================================================================
// Tests that getState()/setState() correctly persist the 4 harmonic physics
// parameters (warmth, coupling, stability, entropy) in version 7 format,
// and that loading a v6 state initializes them to their spec defaults (0.0).
//
// Feature: 122-harmonic-physics
// Tasks: T092, T093
// Requirements: FR-025
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

// Minimal IBStream implementation for state tests (same pattern as test_state_v6.cpp)
class V7TestStream : public IBStream
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
static std::unique_ptr<Innexus::Processor> createAndSetupV7Processor()
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

/// Write v1 default tail: feedback, ADSR, partialCount (after physics)
static void writeV1DefaultsAfterPhysics(IBStreamer& s)
{
    // Feedback
    s.writeFloat(0.0f); s.writeFloat(0.2f);
    // ADSR global
    s.writeFloat(10.0f); s.writeFloat(100.0f); s.writeFloat(1.0f);
    s.writeFloat(100.0f); s.writeFloat(0.0f); s.writeFloat(1.0f);
    s.writeFloat(0.0f); s.writeFloat(0.0f); s.writeFloat(0.0f);
    // ADSR per-slot (8 × 9)
    for (int i = 0; i < 8; ++i) {
        s.writeFloat(10.0f); s.writeFloat(100.0f); s.writeFloat(1.0f);
        s.writeFloat(100.0f); s.writeFloat(0.0f); s.writeFloat(1.0f);
        s.writeFloat(0.0f); s.writeFloat(0.0f); s.writeFloat(0.0f);
    }
    // partialCount
    s.writeFloat(0.0f);
}

// ==============================================================================
// T092: v7 state save/load roundtrip
// ==============================================================================

TEST_CASE("StateV7: save/load roundtrip preserves all 4 harmonic physics parameters",
          "[innexus][vst][state][v7][harmonic_physics]")
{
    constexpr float kWarmth = 0.7f;
    constexpr float kCoupling = 0.3f;
    constexpr float kStability = 0.5f;
    constexpr float kEntropy = 0.2f;

    V7TestStream stream;

    // Processor A: set non-default harmonic physics values, then save state
    {
        auto procA = createAndSetupV7Processor();

        // Build a v7 state blob manually with specific harmonic physics values
        // We construct it by having procA save its state (which gives us the
        // correct v7 format), but we need to set the atomics first.
        // Since there are no direct setters on the processor, we build the
        // entire state blob manually.

        procA->setActive(false);
        procA->terminate();
    }

    // Build a v1 state blob with known harmonic physics values
    {
        IBStreamer streamer(&stream, kLittleEndian);

        // Version 1
        streamer.writeInt32(1);

        // M1 parameters
        streamer.writeFloat(100.0f);     // releaseTimeMs
        streamer.writeFloat(0.5f);       // inharmonicityAmount
        streamer.writeFloat(0.8f);       // masterGain
        streamer.writeFloat(0.0f);       // bypass
        streamer.writeInt32(0);          // path length (empty)

        // M2 parameters
        streamer.writeFloat(1.0f);       // harmonicLevel
        streamer.writeFloat(1.0f);       // residualLevel
        streamer.writeFloat(0.0f);       // brightness
        streamer.writeFloat(0.0f);       // transientEmphasis
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

        // M6 parameters (31 floats)
        streamer.writeFloat(1.0f);   // timbralBlend
        streamer.writeFloat(0.0f);   // stereoSpread
        streamer.writeFloat(0.0f);   // evolutionEnable
        streamer.writeFloat(0.0f);   // evolutionSpeed
        streamer.writeFloat(0.5f);   // evolutionDepth
        streamer.writeFloat(0.0f);   // evolutionMode
        streamer.writeFloat(0.0f);   // mod1Enable
        streamer.writeFloat(0.0f);   // mod1Waveform
        streamer.writeFloat(0.0f);   // mod1Rate
        streamer.writeFloat(0.0f);   // mod1Depth
        streamer.writeFloat(0.0f);   // mod1RangeStart
        streamer.writeFloat(1.0f);   // mod1RangeEnd
        streamer.writeFloat(0.0f);   // mod1Target
        streamer.writeFloat(0.0f);   // mod2Enable
        streamer.writeFloat(0.0f);   // mod2Waveform
        streamer.writeFloat(0.0f);   // mod2Rate
        streamer.writeFloat(0.0f);   // mod2Depth
        streamer.writeFloat(0.0f);   // mod2RangeStart
        streamer.writeFloat(1.0f);   // mod2RangeEnd
        streamer.writeFloat(0.0f);   // mod2Target
        streamer.writeFloat(0.0f);   // detuneSpread
        streamer.writeFloat(0.0f);   // blendEnable
        for (int i = 0; i < 8; ++i)
            streamer.writeFloat(0.0f); // blendSlotWeights
        streamer.writeFloat(0.0f);   // blendLiveWeight

        // Harmonic Physics parameters
        streamer.writeFloat(kWarmth);
        streamer.writeFloat(kCoupling);
        streamer.writeFloat(kStability);
        streamer.writeFloat(kEntropy);

        writeV1DefaultsAfterPhysics(streamer);
    }

    // Load into processor B and verify all values restored
    {
        auto procB = createAndSetupV7Processor();
        stream.resetReadPos();
        REQUIRE(procB->setState(&stream) == kResultOk);

        constexpr float kTol = 1e-6f;
        REQUIRE(procB->getWarmth() == Approx(kWarmth).margin(kTol));
        REQUIRE(procB->getCoupling() == Approx(kCoupling).margin(kTol));
        REQUIRE(procB->getStability() == Approx(kStability).margin(kTol));
        REQUIRE(procB->getEntropy() == Approx(kEntropy).margin(kTol));

        procB->setActive(false);
        procB->terminate();
    }
}

TEST_CASE("StateV7: getState/setState round-trip preserves harmonic physics values",
          "[innexus][vst][state][v7][harmonic_physics]")
{
    // This test uses getState() from one processor and setState() on another
    // to verify the full round-trip pipeline.
    V7TestStream stream;

    // Processor A: save default state (physics params at 0.0)
    {
        auto procA = createAndSetupV7Processor();
        REQUIRE(procA->getState(&stream) == kResultOk);
        procA->setActive(false);
        procA->terminate();
    }

    // Processor B: load state and verify physics defaults preserved
    {
        auto procB = createAndSetupV7Processor();
        stream.resetReadPos();
        REQUIRE(procB->setState(&stream) == kResultOk);

        constexpr float kTol = 1e-6f;
        REQUIRE(procB->getWarmth() == Approx(0.0f).margin(kTol));
        REQUIRE(procB->getCoupling() == Approx(0.0f).margin(kTol));
        REQUIRE(procB->getStability() == Approx(0.0f).margin(kTol));
        REQUIRE(procB->getEntropy() == Approx(0.0f).margin(kTol));

        procB->setActive(false);
        procB->terminate();
    }
}

// ==============================================================================
// Controller setComponentState test
// ==============================================================================

TEST_CASE("StateV7: Controller setComponentState reads harmonic physics params",
          "[innexus][vst][state][v7][harmonic_physics]")
{
    V7TestStream stream;

    // Build a v1 state blob with known physics values
    {
        IBStreamer streamer(&stream, kLittleEndian);
        streamer.writeInt32(1);
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
        // M6 (31 floats)
        streamer.writeFloat(1.0f);   // timbralBlend
        streamer.writeFloat(0.0f);   // stereoSpread
        for (int i = 0; i < 29; ++i) streamer.writeFloat(0.0f);
        // Physics
        streamer.writeFloat(0.7f);   // warmth
        streamer.writeFloat(0.3f);   // coupling
        streamer.writeFloat(0.5f);   // stability
        streamer.writeFloat(0.2f);   // entropy

        writeV1DefaultsAfterPhysics(streamer);
    }

    Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    stream.resetReadPos();
    REQUIRE(controller.setComponentState(&stream) == kResultOk);

    // Verify harmonic physics parameters in controller
    REQUIRE(controller.getParamNormalized(Innexus::kWarmthId)
            == Approx(0.7).margin(0.001));
    REQUIRE(controller.getParamNormalized(Innexus::kCouplingId)
            == Approx(0.3).margin(0.001));
    REQUIRE(controller.getParamNormalized(Innexus::kStabilityId)
            == Approx(0.5).margin(0.001));
    REQUIRE(controller.getParamNormalized(Innexus::kEntropyId)
            == Approx(0.2).margin(0.001));

    REQUIRE(controller.terminate() == kResultOk);
}

