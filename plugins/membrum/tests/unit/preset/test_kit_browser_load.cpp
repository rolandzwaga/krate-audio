// ==============================================================================
// One preset-load path for kit presets (R-202, Ruinae audit fix plan T1.36)
// ==============================================================================
// A kit-preset browser load used to reach the processor only as per-parameter
// performEdits: Processor::setState never ran, so the voice flush (the "tonal
// beep" fix), the coupling flush (the "infinite ring" fix), each pad's
// noiseLayerGain (no ParamID), the bodyDamping -1 sentinel and the macro cache
// resync were skipped. These cases pin a browser load to the host load.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "controller/controller.h"
#include "processor/processor.h"
#include "plugin_ids.h"
#include "state/state_codec.h"
#include "dsp/default_kit.h"
#include "dsp/pad_config.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/ivstevents.h"

#include "vst_param_changes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

constexpr int32  kKbBlockSize  = 256;
constexpr double kKbSampleRate = 48000.0;

// A host's component handler: records every performEdit the controller sends,
// so the test can forward them to the processor as a host would.
class KbRecordingHandler final : public IComponentHandler
{
public:
    std::vector<std::pair<ParamID, ParamValue>> edits;

    tresult PLUGIN_API beginEdit(ParamID /*id*/) override { return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID id, ParamValue value) override
    {
        edits.emplace_back(id, value);
        return kResultOk;
    }
    tresult PLUGIN_API endEdit(ParamID /*id*/) override { return kResultOk; }
    tresult PLUGIN_API restartComponent(int32 /*flags*/) override { return kResultOk; }

    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
    {
        if (FUnknownPrivate::iidEqual(iid, IComponentHandler::iid) ||
            FUnknownPrivate::iidEqual(iid, FUnknown::iid))
        {
            *obj = static_cast<IComponentHandler*>(this);
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    // Owned by the test: reference counting is a no-op.
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
};

class KbEventList final : public IEventList
{
public:
    tresult PLUGIN_API queryInterface(const TUID, void**) override { return kNoInterface; }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    int32 PLUGIN_API getEventCount() override { return static_cast<int32>(events_.size()); }
    tresult PLUGIN_API getEvent(int32 index, Event& e) override
    {
        if (index < 0 || index >= static_cast<int32>(events_.size()))
            return kResultFalse;
        e = events_[static_cast<std::size_t>(index)];
        return kResultOk;
    }
    tresult PLUGIN_API addEvent(Event& e) override
    {
        events_.push_back(e);
        return kResultOk;
    }
    void noteOn(int16 pitch, float velocity)
    {
        Event e{};
        e.type = Event::kNoteOnEvent;
        e.noteOn.pitch = pitch;
        e.noteOn.velocity = velocity;
        events_.push_back(e);
    }
    void clear() { events_.clear(); }

private:
    std::vector<Event> events_;
};

std::unique_ptr<Membrum::Processor> makeKbProcessor(FUnknown* context)
{
    auto proc = std::make_unique<Membrum::Processor>();
    REQUIRE(proc->initialize(context) == kResultOk);
    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = kKbBlockSize;
    setup.sampleRate = kKbSampleRate;
    REQUIRE(proc->setupProcessing(setup) == kResultOk);
    REQUIRE(proc->setActive(true) == kResultOk);
    return proc;
}

// Processes one block with the given parameter changes and events; returns
// the RMS over both output channels.
double processKbBlock(Membrum::Processor& proc,
                      IParameterChanges* changes,
                      KbEventList& events)
{
    std::array<float, kKbBlockSize> outL{};
    std::array<float, kKbBlockSize> outR{};
    std::array<float*, 2> chans{outL.data(), outR.data()};
    AudioBusBuffers bus{};
    bus.numChannels = 2;
    bus.channelBuffers32 = chans.data();

    ProcessData data{};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = kKbBlockSize;
    data.numOutputs = 1;
    data.outputs = &bus;
    data.inputParameterChanges = changes;
    data.inputEvents = &events;
    proc.process(data);
    events.clear();

    double sum = 0.0;
    for (int32 i = 0; i < kKbBlockSize; ++i)
    {
        const auto idx = static_cast<std::size_t>(i);
        sum += static_cast<double>(outL[idx]) * outL[idx];
        sum += static_cast<double>(outR[idx]) * outR[idx];
    }
    return std::sqrt(sum / (2.0 * kKbBlockSize));
}

std::vector<char> streamBytes(MemoryStream& stream)
{
    const auto size = static_cast<std::size_t>(stream.getSize());
    const char* data = stream.getData();
    return std::vector<char>(data, data + size);
}

std::vector<char> processorStateBytes(Membrum::Processor& proc)
{
    MemoryStream stream;
    REQUIRE(proc.getState(&stream) == kResultOk);
    return streamBytes(stream);
}

std::vector<char> kitBytes(const Membrum::State::KitSnapshot& kit)
{
    MemoryStream stream;
    REQUIRE(Membrum::State::writeKitBlob(&stream, kit) == kResultOk);
    return streamBytes(stream);
}

Membrum::State::KitSnapshot defaultKit()
{
    std::array<Membrum::PadConfig, Membrum::kNumPads> pads;
    Membrum::DefaultKit::apply(pads);
    Membrum::State::KitSnapshot kit;
    for (int i = 0; i < Membrum::kNumPads; ++i)
        kit.pads[static_cast<std::size_t>(i)] =
            Membrum::State::toPadSnapshot(pads[static_cast<std::size_t>(i)]);
    return kit;
}

// Kit preset A (variantB = false) or B, written as a kit-preset file carries
// it (with the session uiMode). They differ in a pad's noiseLayerGain, an
// explicit B1/B3 body damping (A) versus the -1 sentinel (B), the macros and
// the coupling matrix inputs (global coupling, snare buzz, tom resonance,
// coupling delay, per-pad coupling amount).
std::vector<char> makeKitPreset(bool variantB)
{
    auto kit = defaultKit();
    const auto pick = [variantB](double a, double b) { return variantB ? b : a; };

    kit.globalCoupling  = pick(0.7, 0.15);
    kit.snareBuzz       = pick(0.5, 0.05);
    kit.tomResonance    = pick(0.6, 0.2);
    kit.couplingDelayMs = pick(1.2, 0.8);
    kit.selectedPadIndex = 0;

    auto& snare = kit.pads[1];
    snare.sound[57] = pick(2.5, 0.75);       // noiseLayerGain (no ParamID)

    auto& tom = kit.pads[5];
    tom.sound[42] = pick(0.4, -1.0);         // bodyDampingB1: explicit vs sentinel
    tom.sound[43] = pick(0.3, -1.0);         // bodyDampingB3

    auto& kick = kit.pads[0];
    kick.macros = variantB ? std::array<double, 5>{0.7, 0.3, 0.6, 0.4, 0.2}
                           : std::array<double, 5>{0.2, 0.8, 0.35, 0.65, 0.9};
    kit.pads[4].couplingAmount = pick(0.9, 0.2);

    kit.hasSession = true;
    kit.uiMode = variantB ? 1 : 0;
    return kitBytes(kit);
}

// A connected Controller + Processor pair, as a host wires them.
struct KitBrowserPair
{
    HostApplication host;
    KbRecordingHandler handler;
    KbEventList events;
    std::unique_ptr<Membrum::Processor> proc;
    Membrum::Controller controller;

    KitBrowserPair()
    {
        proc = makeKbProcessor(&host);
        REQUIRE(controller.initialize(&host) == kResultOk);
        controller.setComponentHandler(&handler);
        controller.connect(static_cast<IConnectionPoint*>(
            static_cast<AudioEffect*>(proc.get())));
        proc->connect(static_cast<IConnectionPoint*>(
            static_cast<EditControllerEx1*>(&controller)));
    }

    ~KitBrowserPair()
    {
        proc->disconnect(static_cast<IConnectionPoint*>(
            static_cast<EditControllerEx1*>(&controller)));
        controller.disconnect(static_cast<IConnectionPoint*>(
            static_cast<AudioEffect*>(proc.get())));
        controller.setComponentHandler(nullptr);
        controller.terminate();
        proc->setActive(false);
        proc->terminate();
    }

    KitBrowserPair(const KitBrowserPair&) = delete;
    KitBrowserPair& operator=(const KitBrowserPair&) = delete;

    bool browserLoad(const std::vector<char>& bytes)
    {
        MemoryStream stream;
        stream.write(const_cast<char*>(bytes.data()),
                     static_cast<int32>(bytes.size()), nullptr);
        stream.seek(0, IBStream::kIBSeekSet, nullptr);
        return controller.kitPresetLoadProvider(&stream);
    }

    // What a host does with the controller's performEdits: hand them to the
    // processor in the next process() call. Returns that block's RMS.
    double forwardEditsAndProcess()
    {
        Krate::Test::ParameterChanges changes;
        for (const auto& [id, value] : handler.edits)
            changes.addChange(id, value);
        handler.edits.clear();
        return processKbBlock(*proc, &changes, events);
    }
};

void hostLoad(Membrum::Processor& proc, const std::vector<char>& bytes)
{
    MemoryStream stream;
    stream.write(const_cast<char*>(bytes.data()),
                 static_cast<int32>(bytes.size()), nullptr);
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    REQUIRE(proc.setState(&stream) == kResultOk);
}

} // namespace

TEST_CASE("Browser load leaves the processor in the same state as a host load",
          "[membrum][presets][state]")
{
    const auto kitA = makeKitPreset(false);
    const auto kitB = makeKitPreset(true);
    REQUIRE(kitA != kitB);

    KitBrowserPair pair;
    REQUIRE(pair.browserLoad(kitA));
    pair.forwardEditsAndProcess();
    REQUIRE(pair.browserLoad(kitB));
    pair.forwardEditsAndProcess();

    auto reference = makeKbProcessor(nullptr);
    hostLoad(*reference, kitB);
    KbEventList none;
    processKbBlock(*reference, nullptr, none);

    const auto got = processorStateBytes(*pair.proc);
    const auto want = processorStateBytes(*reference);
    std::size_t firstDiff = 0;
    while (firstDiff < std::min(got.size(), want.size()) &&
           got[firstDiff] == want[firstDiff])
        ++firstDiff;
    INFO("browser-loaded state " << got.size() << " bytes, host-loaded "
         << want.size() << " bytes, first difference at byte " << firstDiff);
    CHECK(got == want);

    reference->setActive(false);
    reference->terminate();
}

TEST_CASE("Browser kit load silences the previous kit's voices and coupling",
          "[membrum][presets][kit]")
{
    const auto kitA = makeKitPreset(false);
    const auto kitB = makeKitPreset(true);

    // Both processors start from the same host load of kit A.
    KitBrowserPair pair;
    hostLoad(*pair.proc, kitA);
    {
        MemoryStream stream;
        stream.write(const_cast<char*>(kitA.data()),
                     static_cast<int32>(kitA.size()), nullptr);
        stream.seek(0, IBStream::kIBSeekSet, nullptr);
        REQUIRE(pair.controller.setComponentState(&stream) == kResultOk);
    }
    pair.forwardEditsAndProcess();

    auto reference = makeKbProcessor(nullptr);
    hostLoad(*reference, kitA);
    KbEventList refEvents;
    processKbBlock(*reference, nullptr, refEvents);

    // Kit A: hit kick, snare and two toms and keep them sounding (no note-off).
    constexpr std::array<int16, 4> kPitches{36, 37, 40, 41};
    for (const auto pitch : kPitches)
    {
        pair.events.noteOn(pitch, 1.0f);
        refEvents.noteOn(pitch, 1.0f);
    }
    double lastRms = 0.0;
    for (int block = 0; block < 6; ++block)
    {
        lastRms = processKbBlock(*pair.proc, nullptr, pair.events);
        const double refRms = processKbBlock(*reference, nullptr, refEvents);
        REQUIRE(std::abs(lastRms - refRms) < 1e-9);
    }
    INFO("kit A RMS before the load: " << lastRms);
    REQUIRE(lastRms > 1e-3);

    // Load kit B: through the browser on one side, through setState on the
    // other, at the same point of the render.
    REQUIRE(pair.browserLoad(kitB));
    const double browserRms = pair.forwardEditsAndProcess();
    hostLoad(*reference, kitB);
    const double hostRms = processKbBlock(*reference, nullptr, refEvents);

    INFO("next block RMS: browser load " << browserRms << ", host load " << hostRms);
    CHECK(std::abs(browserRms - hostRms) <= 1e-6);

    reference->setActive(false);
    reference->terminate();
}

TEST_CASE("Browser kit load keeps the selected pad in the processor (FR-052)",
          "[membrum][presets][kit]")
{
    auto kit = defaultKit();
    kit.selectedPadIndex = 9;
    const auto bytes = kitBytes(kit);

    KitBrowserPair pair;
    pair.proc->setSelectedPadIndexForTest(3);
    REQUIRE(pair.browserLoad(bytes));
    pair.forwardEditsAndProcess();
    CHECK(pair.proc->selectedPadIndexForTest() == 3);
}
