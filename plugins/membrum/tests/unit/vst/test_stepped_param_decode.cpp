// T2.30 (MB-080): a stepped parameter decodes with the SDK's convention
// (Steinberg::FromNormalized: floor(norm * (steps + 1)), clamped to steps) in
// the processor and the controller, so the host's displayed value and the
// DSP agree at every intermediate automation value.

#include <catch2/catch_test_macros.hpp>

#include "controller/controller.h"
#include "plugin_ids.h"
#include "processor/processor.h"
#include "voice_pool/voice_pool.h"

#include "vst_param_changes.h"

#include <pluginterfaces/vst/ivstaudioprocessor.h>
#include <public.sdk/source/vst/vstparameters.h>

#include <string>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

constexpr int kSweepSteps = 1000;

int expectedIndex(Membrum::Controller& ctl, ParamID id, double v)
{
    auto* p = ctl.getParameterObject(id);
    REQUIRE(p != nullptr);
    return static_cast<int>(p->toPlain(v));
}

struct ProcessorHost
{
    Membrum::Processor processor;
    float outL[64] = {};
    float outR[64] = {};
    float* channels[2] = {outL, outR};
    AudioBusBuffers bus{};
    ProcessData data{};

    ProcessorHost()
    {
        REQUIRE(processor.initialize(nullptr) == kResultOk);
        ProcessSetup setup{};
        setup.processMode = kRealtime;
        setup.symbolicSampleSize = kSample32;
        setup.maxSamplesPerBlock = 64;
        setup.sampleRate = 44100.0;
        processor.setupProcessing(setup);
        processor.setActive(true);
        bus.numChannels = 2;
        bus.channelBuffers32 = channels;
        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = 64;
        data.numOutputs = 1;
        data.outputs = &bus;
    }
    ~ProcessorHost()
    {
        processor.setActive(false);
        processor.terminate();
    }
    void send(ParamID id, double v)
    {
        Krate::Test::ParameterChanges changes;
        changes.add(id, v);
        data.inputParameterChanges = &changes;
        processor.process(data);
        data.inputParameterChanges = nullptr;
    }
};

} // namespace

TEST_CASE("Stepped parameters: processor and controller decode every normalized value to the index the host displays",
          "[vst][param-decode]")
{
    Membrum::Controller ctl;
    REQUIRE(ctl.initialize(nullptr) == kResultOk);

    ProcessorHost host;
    Membrum::VoicePool& pool = host.processor.voicePoolForTest();

    const auto chokeId = static_cast<ParamID>(Membrum::padParamId(0, Membrum::kPadChokeGroup));
    const auto busId = static_cast<ParamID>(Membrum::padParamId(0, Membrum::kPadOutputBus));

    for (int i = 0; i <= kSweepSteps; ++i)
    {
        const double v = static_cast<double>(i) / kSweepSteps;
        INFO("normalized value " << v);

        // Per-pad Choke Group and Output Bus: VoicePool decode.
        pool.setPadConfigField(0, Membrum::kPadChokeGroup, static_cast<float>(v));
        CHECK(static_cast<int>(pool.padConfig(0).chokeGroup) == expectedIndex(ctl, chokeId, v));
        pool.setPadConfigField(0, Membrum::kPadOutputBus, static_cast<float>(v));
        CHECK(static_cast<int>(pool.padConfig(0).outputBus) == expectedIndex(ctl, busId, v));

        // The proxy Choke Group shares the per-pad grid.
        CHECK(expectedIndex(ctl, Membrum::kChokeGroupId, v) == expectedIndex(ctl, chokeId, v));

        // Max Polyphony (RangeParameter 4..16): processor.
        host.send(Membrum::kMaxPolyphonyId, v);
        CHECK(pool.maxPolyphony() == expectedIndex(ctl, Membrum::kMaxPolyphonyId, v));

        // Selected Pad: processor and controller.
        host.send(Membrum::kSelectedPadId, v);
        const int pad = expectedIndex(ctl, Membrum::kSelectedPadId, v);
        CHECK(host.processor.selectedPadIndexForTest() == pad);
        ctl.setParamNormalized(Membrum::kSelectedPadId, v);
        CHECK(ctl.selectedPadIndexForTest() == pad);
    }
}
