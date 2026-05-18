// ==============================================================================
// Host-level end-to-end simulation of the "move XY pad, hit pad zero, hear no
// difference" scenario.
//
// Drives the Processor's actual public entry points (processParameterChanges
// via IParameterChanges, MIDI events via IEventList) exactly the way a VST3
// host would after the editor performed edits. If THIS test fails to
// reproduce audible differences between two XY positions, the bug is below
// the host->processor boundary (DSP). If it produces audible differences but
// the user does not hear them in their DAW, the bug is above the boundary
// (editor wiring or DAW host).
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivstprocesscontext.h"
#include "pluginterfaces/vst/ivstevents.h"
#include "pluginterfaces/vst/ivstparameterchanges.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdint>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

class PV : public IParamValueQueue
{
public:
    PV(ParamID id, ParamValue v) : id_(id), v_(v) {}
    ParamID PLUGIN_API getParameterId() override { return id_; }
    int32 PLUGIN_API getPointCount() override { return 1; }
    tresult PLUGIN_API getPoint(int32, int32& s, ParamValue& v) override
    { s = 0; v = v_; return kResultTrue; }
    tresult PLUGIN_API addPoint(int32, ParamValue, int32&) override { return kResultFalse; }
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
private:
    ParamID    id_;
    ParamValue v_;
};

class PC : public IParameterChanges
{
public:
    void add(ParamID id, ParamValue v) { q_.emplace_back(id, v); }
    int32 PLUGIN_API getParameterCount() override
    { return static_cast<int32>(q_.size()); }
    IParamValueQueue* PLUGIN_API getParameterData(int32 i) override
    { return (i >= 0 && i < static_cast<int32>(q_.size()))
        ? &q_[static_cast<std::size_t>(i)] : nullptr; }
    IParamValueQueue* PLUGIN_API addParameterData(const ParamID&, int32&) override
    { return nullptr; }
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
private:
    std::vector<PV> q_;
};

class EL : public IEventList
{
public:
    void noteOn(int16 midi)
    {
        Event e{};
        e.type = Event::kNoteOnEvent;
        e.sampleOffset = 0;
        e.noteOn.channel = 0;
        e.noteOn.pitch = midi;
        e.noteOn.velocity = 0.9f;
        e.noteOn.noteId = midi;
        events_.push_back(e);
    }
    void clear() { events_.clear(); }
    int32 PLUGIN_API getEventCount() override
    { return static_cast<int32>(events_.size()); }
    tresult PLUGIN_API getEvent(int32 i, Event& out) override
    { if (i < 0 || i >= static_cast<int32>(events_.size())) return kResultFalse;
      out = events_[static_cast<std::size_t>(i)]; return kResultOk; }
    tresult PLUGIN_API addEvent(Event&) override { return kResultFalse; }
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
private:
    std::vector<Event> events_;
};

ProcessSetup makeSetup(double sr, int32 bs)
{
    ProcessSetup s{};
    s.processMode = kRealtime;
    s.symbolicSampleSize = kSample32;
    s.maxSamplesPerBlock = bs;
    s.sampleRate = sr;
    return s;
}

double rmsDb(const float* p, int n)
{
    double sq = 0.0;
    for (int i = 0; i < n; ++i) sq += static_cast<double>(p[i]) * p[i];
    return 20.0 * std::log10(std::sqrt(sq / std::max(1, n)) + 1e-30);
}

// Render kBlocks of audio after firing a noteOn on `midi`. Each block can
// optionally carry a fresh IParameterChanges payload (matches VST3 host
// behaviour where the host drives both events and params per process call).
template <typename Setup>
std::vector<float> renderAfterEvent(Membrum::Processor& processor,
                                     int16 midi, int blocks, int bs,
                                     Setup setupPerBlock)
{
    std::vector<float> outL(bs, 0.0f), outR(bs, 0.0f);
    float* chans[2] = { outL.data(), outR.data() };
    AudioBusBuffers bus{};
    bus.numChannels = 2;
    bus.channelBuffers32 = chans;
    bus.silenceFlags = 0;

    std::vector<float> all;
    all.reserve(static_cast<std::size_t>(blocks) * bs);

    for (int b = 0; b < blocks; ++b)
    {
        EL events;
        PC paramChanges;
        if (b == 0)
            events.noteOn(midi);
        setupPerBlock(b, paramChanges);

        std::fill(outL.begin(), outL.end(), 0.0f);
        std::fill(outR.begin(), outR.end(), 0.0f);

        ProcessData data{};
        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = bs;
        data.numOutputs = 1;
        data.outputs = &bus;
        data.numInputs = 0;
        data.inputEvents = &events;
        data.inputParameterChanges = (paramChanges.getParameterCount() > 0)
            ? &paramChanges : nullptr;

        processor.process(data);
        for (int i = 0; i < bs; ++i) all.push_back(outL[i]);
    }
    return all;
}

} // namespace

// Send an AuditionPad IMessage exactly the way controller.cpp:1145 does.
void sendAuditionPadMessage(Membrum::Processor& processor, int midi)
{
    auto msg = Steinberg::owned(new HostMessage());
    msg->setMessageID("AuditionPad");
    auto* attrs = msg->getAttributes();
    REQUIRE(attrs != nullptr);
    attrs->setInt("midi",     static_cast<Steinberg::int64>(midi));
    attrs->setInt("velocity", static_cast<Steinberg::int64>(100));
    REQUIRE(processor.notify(msg) == kResultOk);
}

TEST_CASE("Morph host-sim 2 (AuditionPad message path): user clicks pad in editor",
          "[membrum][morph][host_sim]")
{
    constexpr double sr = 44100.0;
    constexpr int    bs = 256;
    constexpr int    blocksPerHit = 48;

    Membrum::Processor processor;
    REQUIRE(processor.initialize(nullptr) == kResultOk);
    auto setupMsg = makeSetup(sr, bs);
    REQUIRE(processor.setupProcessing(setupMsg) == kResultOk);
    REQUIRE(processor.setActive(true) == kResultOk);

    auto render = [&](bool firstHit, float morphStart, float morphEnd) {
        std::vector<float> outL(bs, 0.0f), outR(bs, 0.0f);
        float* chans[2] = { outL.data(), outR.data() };
        AudioBusBuffers bus{};
        bus.numChannels = 2;
        bus.channelBuffers32 = chans;

        std::vector<float> samples;
        samples.reserve(static_cast<std::size_t>(blocksPerHit) * bs);

        for (int b = 0; b < blocksPerHit; ++b)
        {
            EL events;
            PC paramChanges;
            if (b == 0)
            {
                // First block: simulate the editor's exact behaviour --
                // pad-click -> performEdit(kSelectedPadId) + AuditionPad msg.
                if (firstHit)
                {
                    paramChanges.add(Membrum::kSelectedPadId,  0.0);
                    paramChanges.add(Membrum::kMorphEnabledId, 1.0);
                }
                paramChanges.add(Membrum::kSelectedPadId, 0.0);
                paramChanges.add(Membrum::kMorphStartId,  morphStart);
                paramChanges.add(Membrum::kMorphEndId,    morphEnd);
                // Send the IMessage BEFORE process() -- matches the editor's
                // synchronous sendMessage() pattern.
                sendAuditionPadMessage(processor, 36);
            }

            std::fill(outL.begin(), outL.end(), 0.0f);
            std::fill(outR.begin(), outR.end(), 0.0f);

            ProcessData data{};
            data.processMode = kRealtime;
            data.symbolicSampleSize = kSample32;
            data.numSamples = bs;
            data.numOutputs = 1;
            data.outputs = &bus;
            data.numInputs = 0;
            data.inputEvents = &events;  // empty
            data.inputParameterChanges = (paramChanges.getParameterCount() > 0)
                ? &paramChanges : nullptr;

            REQUIRE(processor.process(data) == kResultOk);
            for (int i = 0; i < bs; ++i) samples.push_back(outL[i]);
        }
        return samples;
    };

    auto hitA = render(/*firstHit*/true,  0.0f, 1.0f);
    auto hitB = render(/*firstHit*/false, 1.0f, 0.0f);

    REQUIRE(processor.setActive(false) == kResultOk);
    REQUIRE(processor.terminate() == kResultOk);

    const int n = static_cast<int>(hitA.size());
    const double rmsA = rmsDb(hitA.data(), n);
    const double rmsB = rmsDb(hitB.data(), n);
    std::vector<float> diff(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) diff[i] = hitA[i] - hitB[i];
    const double rmsDiff = rmsDb(diff.data(), n);

    std::fprintf(stderr, "[host-sim-msg] hitA RMS = %.2f dBFS\n", rmsA);
    std::fprintf(stderr, "[host-sim-msg] hitB RMS = %.2f dBFS\n", rmsB);
    std::fprintf(stderr, "[host-sim-msg] diff RMS = %.2f dBFS\n", rmsDiff);
    std::fprintf(stderr, "[host-sim-msg] selectedPadIndex_ = %d\n",
        processor.selectedPadIndexForTest());

    CHECK(rmsDiff > -60.0);
}

TEST_CASE("Morph host-sim: XY-then-hit reproduces audibility through Processor::process",
          "[membrum][morph][host_sim]")
{
    constexpr double sr = 44100.0;
    constexpr int    bs = 256;
    constexpr int    blocksPerHit = 48;  // ~278 ms

    Membrum::Processor processor;
    REQUIRE(processor.initialize(nullptr) == kResultOk);
    auto setup = makeSetup(sr, bs);
    REQUIRE(processor.setupProcessing(setup) == kResultOk);
    REQUIRE(processor.setActive(true) == kResultOk);

    // Step 1: editor on first open writes default state through paramChanges.
    //         Simulate: select pad 0, enable morph, set XY position A.
    auto hitA = renderAfterEvent(processor, /*midi*/36, blocksPerHit, bs,
        [](int blockIdx, PC& pc) {
            if (blockIdx == 0)
            {
                pc.add(Membrum::kSelectedPadId,   0.0);   // pad index 0 / 31
                pc.add(Membrum::kMorphEnabledId,  1.0);   // ON
                pc.add(Membrum::kMorphStartId,    0.0);   // woody
                pc.add(Membrum::kMorphEndId,      1.0);   // -> metallic
            }
        });

    // Step 2: user moves XY to opposite corner, hits pad 0 again.
    auto hitB = renderAfterEvent(processor, /*midi*/36, blocksPerHit, bs,
        [](int blockIdx, PC& pc) {
            if (blockIdx == 0)
            {
                pc.add(Membrum::kSelectedPadId, 0.0);
                pc.add(Membrum::kMorphStartId, 1.0);      // metallic
                pc.add(Membrum::kMorphEndId,   0.0);      // -> woody
            }
        });

    REQUIRE(processor.setActive(false) == kResultOk);
    REQUIRE(processor.terminate() == kResultOk);

    const int n = static_cast<int>(hitA.size());
    REQUIRE(n == static_cast<int>(hitB.size()));

    const double rmsA = rmsDb(hitA.data(), n);
    const double rmsB = rmsDb(hitB.data(), n);

    // RMS of the per-sample difference.
    std::vector<float> diff(static_cast<std::size_t>(n));
    for (int i = 0; i < n; ++i) diff[i] = hitA[i] - hitB[i];
    const double rmsDiff = rmsDb(diff.data(), n);

    std::fprintf(stderr, "[host-sim] hitA RMS = %.2f dBFS\n", rmsA);
    std::fprintf(stderr, "[host-sim] hitB RMS = %.2f dBFS\n", rmsB);
    std::fprintf(stderr, "[host-sim] diff RMS = %.2f dBFS (lower = identical)\n", rmsDiff);
    std::fprintf(stderr, "[host-sim] processor.selectedPadIndex_ at end = %d\n",
        processor.selectedPadIndexForTest());

    // If the host->processor->voice pipeline is intact, the per-sample diff
    // RMS should be well above -60 dBFS (clearly audible). If the user
    // reports "no audible difference" and this test prints diff RMS near
    // hitA/hitB RMS (i.e. the two renders are essentially the same audio),
    // we've reproduced the bug at the host boundary.
    CHECK(rmsDiff > -60.0);  // anything above -60 is loud enough to hear
}
