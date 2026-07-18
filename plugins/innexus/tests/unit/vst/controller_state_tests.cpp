// ==============================================================================
// Controller State Round-Trip Tests
// ==============================================================================
// WI-4: Controller::setComponentState() historically skipped the two Sympathetic
// Resonance floats that Processor::getState() writes just before the instance-ID
// trailer. That left kSympatheticAmountId/kSympatheticDecayId at their defaults
// on every reload AND desynchronized the stream by 8 bytes so the SharedDisplay
// instance-ID marker was never recognized. This verifies both are restored.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "processor/processor.h"
#include "controller/controller.h"
#include "plugin_ids.h"

#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "base/source/fstreamer.h"

#include "test_helpers/vst_param_changes.h"

#include <algorithm>
#include <cstring>
#include <memory>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

namespace {

// Minimal in-memory IBStream (read/write/seek) for state round-trips.
class MemStream : public IBStream
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

private:
    std::vector<char> data_;
    int32 readPos_ = 0;
};

std::unique_ptr<Innexus::Processor> makeProcessor()
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

// Apply parameter changes by driving one process() block.
void applyParams(Innexus::Processor& proc, Krate::Test::ParameterChanges& changes)
{
    constexpr int32 kBlock = 128;
    std::vector<float> outL(kBlock, 0.0f);
    std::vector<float> outR(kBlock, 0.0f);
    float* channels[2] = {outL.data(), outR.data()};

    AudioBusBuffers outBus{};
    outBus.numChannels = 2;
    outBus.channelBuffers32 = channels;

    ProcessData data{};
    data.numSamples = kBlock;
    data.numInputs = 0;
    data.numOutputs = 1;
    data.outputs = &outBus;
    data.inputParameterChanges = &changes;

    proc.process(data);
}

} // namespace

TEST_CASE("ControllerState: setComponentState restores Sympathetic params and consumes instance-id (WI-4)",
          "[innexus][vst][state][sympathetic]")
{
    constexpr double kAmount = 0.7;
    constexpr double kDecay = 0.3;

    MemStream stream;

    {
        auto proc = makeProcessor();
        // Sympathetic defaults are amount=0.0, decay=0.5 — set both to non-defaults.
        Krate::Test::ParameterChanges changes;
        changes.addChange(Innexus::kSympatheticAmountId, kAmount);
        changes.addChange(Innexus::kSympatheticDecayId, kDecay);
        applyParams(*proc, changes);

        REQUIRE(proc->getState(&stream) == kResultOk);
        proc->setActive(false);
        proc->terminate();
    }

    Innexus::Controller controller;
    REQUIRE(controller.initialize(nullptr) == kResultOk);
    stream.resetReadPos();
    REQUIRE(controller.setComponentState(&stream) == kResultOk);

    constexpr double kTol = 1e-3;
    REQUIRE(controller.getParamNormalized(Innexus::kSympatheticAmountId)
            == Approx(kAmount).margin(kTol));
    REQUIRE(controller.getParamNormalized(Innexus::kSympatheticDecayId)
            == Approx(kDecay).margin(kTol));

    REQUIRE(controller.terminate() == kResultOk);
}
