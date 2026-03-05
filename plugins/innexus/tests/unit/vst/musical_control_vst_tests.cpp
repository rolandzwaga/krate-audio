// ==============================================================================
// Musical Control Layer VST Parameter Registration Tests (M4)
// ==============================================================================
// Tests that the four new M4 parameters (Freeze, Morph Position, Harmonic
// Filter, Responsiveness) are correctly registered in the Controller with
// proper types, ranges, defaults, and step counts.
//
// Feature: 118-musical-control-layer
// User Stories: US1-US4
// Requirements: FR-001, FR-010, FR-019, FR-029, FR-033
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "controller/controller.h"
#include "plugin_ids.h"

#include "pluginterfaces/base/ibstream.h"
#include "base/source/fstreamer.h"

#include <algorithm>
#include <cstring>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

// Minimal IBStream for Controller::setComponentState tests
class M4VstTestStream : public IBStream
{
public:
    tresult PLUGIN_API queryInterface(const TUID, void**) override
    {
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }

    tresult PLUGIN_API read(void* buffer, int32 numBytes,
                            int32* numBytesRead) override
    {
        if (!buffer || numBytes <= 0)
            return kResultFalse;

        int32 available = static_cast<int32>(data_.size()) - readPos_;
        int32 toRead = std::min(numBytes, available);
        if (toRead <= 0)
        {
            if (numBytesRead) *numBytesRead = 0;
            return kResultFalse;
        }

        std::memcpy(buffer, data_.data() + readPos_, static_cast<size_t>(toRead));
        readPos_ += toRead;
        if (numBytesRead) *numBytesRead = toRead;
        return kResultOk;
    }

    tresult PLUGIN_API write(void* buffer, int32 numBytes,
                             int32* numBytesWritten) override
    {
        if (!buffer || numBytes <= 0)
            return kResultFalse;

        auto* bytes = static_cast<const char*>(buffer);
        data_.insert(data_.end(), bytes, bytes + numBytes);
        if (numBytesWritten) *numBytesWritten = numBytes;
        return kResultOk;
    }

    tresult PLUGIN_API seek(int64 pos, int32 mode, int64* result) override
    {
        int64 newPos = 0;
        switch (mode)
        {
        case kIBSeekSet: newPos = pos; break;
        case kIBSeekCur: newPos = readPos_ + pos; break;
        case kIBSeekEnd: newPos = static_cast<int64>(data_.size()) + pos; break;
        default: return kResultFalse;
        }
        if (newPos < 0 || newPos > static_cast<int64>(data_.size()))
            return kResultFalse;
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

private:
    std::vector<char> data_;
    int32 readPos_ = 0;
};

// =============================================================================
// T019: VST Parameter Registration Tests for M4 Musical Control
// =============================================================================

TEST_CASE("M4: All four musical control parameter IDs are retrievable from controller",
          "[innexus][vst][m4][params]")
{
    Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    ParameterInfo info{};

    // kFreezeId
    REQUIRE(controller.getParameterInfoByTag(Innexus::kFreezeId, info) == kResultOk);
    REQUIRE(info.id == Innexus::kFreezeId);

    // kMorphPositionId
    REQUIRE(controller.getParameterInfoByTag(Innexus::kMorphPositionId, info) == kResultOk);
    REQUIRE(info.id == Innexus::kMorphPositionId);

    // kHarmonicFilterTypeId
    REQUIRE(controller.getParameterInfoByTag(Innexus::kHarmonicFilterTypeId, info) == kResultOk);
    REQUIRE(info.id == Innexus::kHarmonicFilterTypeId);

    // kResponsivenessId
    REQUIRE(controller.getParameterInfoByTag(Innexus::kResponsivenessId, info) == kResultOk);
    REQUIRE(info.id == Innexus::kResponsivenessId);

    REQUIRE(controller.terminate() == kResultOk);
}

TEST_CASE("M4: kFreezeId has stepCount == 1 (toggle parameter)",
          "[innexus][vst][m4][params]")
{
    Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    ParameterInfo info{};
    REQUIRE(controller.getParameterInfoByTag(Innexus::kFreezeId, info) == kResultOk);

    // A toggle parameter must have exactly one step (two states: 0 and 1)
    REQUIRE(info.stepCount == 1);

    // Default is off (0.0)
    REQUIRE(info.defaultNormalizedValue == Approx(0.0).margin(0.001));

    // Must be automatable
    REQUIRE((info.flags & ParameterInfo::kCanAutomate) != 0);

    REQUIRE(controller.terminate() == kResultOk);
}

TEST_CASE("M4: kHarmonicFilterTypeId has 5 list entries",
          "[innexus][vst][m4][params]")
{
    Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    ParameterInfo info{};
    REQUIRE(controller.getParameterInfoByTag(Innexus::kHarmonicFilterTypeId, info) == kResultOk);

    // StringListParameter with 5 entries has stepCount == 4 (0,1,2,3,4)
    REQUIRE(info.stepCount == 4);

    // Must be automatable and a list
    REQUIRE((info.flags & ParameterInfo::kCanAutomate) != 0);
    REQUIRE((info.flags & ParameterInfo::kIsList) != 0);

    // Default is All-Pass (normalized 0.0)
    REQUIRE(info.defaultNormalizedValue == Approx(0.0).margin(0.001));

    REQUIRE(controller.terminate() == kResultOk);
}

TEST_CASE("M4: kResponsivenessId default normalized value is 0.5",
          "[innexus][vst][m4][params]")
{
    Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    ParameterInfo info{};
    REQUIRE(controller.getParameterInfoByTag(Innexus::kResponsivenessId, info) == kResultOk);

    // Default is 0.5 (balanced stability/responsiveness)
    REQUIRE(info.defaultNormalizedValue == Approx(0.5).margin(0.001));

    // Must be automatable
    REQUIRE((info.flags & ParameterInfo::kCanAutomate) != 0);

    // Verify range: 0.0 to 1.0
    auto plainMin = controller.normalizedParamToPlain(Innexus::kResponsivenessId, 0.0);
    auto plainMax = controller.normalizedParamToPlain(Innexus::kResponsivenessId, 1.0);
    REQUIRE(plainMin == Approx(0.0).margin(0.001));
    REQUIRE(plainMax == Approx(1.0).margin(0.001));

    REQUIRE(controller.terminate() == kResultOk);
}

TEST_CASE("M4: kMorphPositionId has range 0-1 and default 0",
          "[innexus][vst][m4][params]")
{
    Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    ParameterInfo info{};
    REQUIRE(controller.getParameterInfoByTag(Innexus::kMorphPositionId, info) == kResultOk);

    // Default is 0.0 (fully frozen state when freeze is active)
    REQUIRE(info.defaultNormalizedValue == Approx(0.0).margin(0.001));

    // Must be automatable
    REQUIRE((info.flags & ParameterInfo::kCanAutomate) != 0);

    // Verify range: 0.0 to 1.0
    auto plainMin = controller.normalizedParamToPlain(Innexus::kMorphPositionId, 0.0);
    auto plainMax = controller.normalizedParamToPlain(Innexus::kMorphPositionId, 1.0);
    REQUIRE(plainMin == Approx(0.0).margin(0.001));
    REQUIRE(plainMax == Approx(1.0).margin(0.001));

    REQUIRE(controller.terminate() == kResultOk);
}

// =============================================================================
// T097: Controller::setComponentState with v3 data applies M4 defaults
// =============================================================================
TEST_CASE("M4 VST: setComponentState with v3 data applies M4 default normalized values",
          "[innexus][vst][m4][state][backward]")
{
    // Build a v3 state blob manually (no M4 data)
    M4VstTestStream stream;
    {
        IBStreamer streamer(&stream, kLittleEndian);

        // Version 3
        streamer.writeInt32(3);

        // M1 params
        streamer.writeFloat(100.0f);  // releaseTimeMs
        streamer.writeFloat(1.0f);    // inharmonicityAmount
        streamer.writeFloat(1.0f);    // masterGain
        streamer.writeFloat(0.0f);    // bypass

        // File path (empty)
        streamer.writeInt32(0);

        // M2 params (plain values)
        streamer.writeFloat(1.0f);    // harmonicLevel plain
        streamer.writeFloat(1.0f);    // residualLevel plain
        streamer.writeFloat(0.0f);    // brightness plain
        streamer.writeFloat(0.0f);    // transientEmphasis plain

        // Residual frames (none)
        streamer.writeInt32(0);  // residualFrameCount
        streamer.writeInt32(0);  // analysisFFTSize
        streamer.writeInt32(0);  // analysisHopSize

        // M3 params
        streamer.writeInt32(0);  // inputSource = Sample
        streamer.writeInt32(0);  // latencyMode = LowLatency

        // NO M4 data
    }

    // Load into controller and verify M4 defaults
    stream.resetReadPos();
    Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);

    REQUIRE(controller.setComponentState(&stream) == kResultOk);

    // Verify M4 default normalized values
    // kFreezeId default = 0.0 (off)
    REQUIRE(controller.getParamNormalized(Innexus::kFreezeId) ==
            Approx(0.0).margin(0.001));

    // kMorphPositionId default = 0.0
    REQUIRE(controller.getParamNormalized(Innexus::kMorphPositionId) ==
            Approx(0.0).margin(0.001));

    // kHarmonicFilterTypeId default = 0.0 (All-Pass normalized)
    REQUIRE(controller.getParamNormalized(Innexus::kHarmonicFilterTypeId) ==
            Approx(0.0).margin(0.001));

    // kResponsivenessId default = 0.5
    REQUIRE(controller.getParamNormalized(Innexus::kResponsivenessId) ==
            Approx(0.5).margin(0.001));

    REQUIRE(controller.terminate() == kResultOk);
}
