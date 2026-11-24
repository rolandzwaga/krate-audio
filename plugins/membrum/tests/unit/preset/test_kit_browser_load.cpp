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
#include "dsp/exciter_type.h"
#include "dsp/body_model_type.h"

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
#include <sstream>
#include <utility>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

constexpr int32  kKbBlockSize  = 256;
constexpr double kKbSampleRate = 48000.0;

// A host's component handler: records every performEdit the controller sends,
// so the test can forward them to the processor as a host would, and counts
// the edit gestures, restartComponent flag words and setDirty calls.
class KbRecordingHandler final : public IComponentHandler, public IComponentHandler2
{
public:
    std::vector<std::pair<ParamID, ParamValue>> edits;
    int beginEdits = 0;
    int endEdits = 0;
    std::vector<int32> restartFlags;
    std::vector<bool> dirtyCalls;

    tresult PLUGIN_API beginEdit(ParamID /*id*/) override
    {
        ++beginEdits;
        return kResultOk;
    }
    tresult PLUGIN_API performEdit(ParamID id, ParamValue value) override
    {
        edits.emplace_back(id, value);
        return kResultOk;
    }
    tresult PLUGIN_API endEdit(ParamID /*id*/) override
    {
        ++endEdits;
        return kResultOk;
    }
    tresult PLUGIN_API restartComponent(int32 flags) override
    {
        restartFlags.push_back(flags);
        return kResultOk;
    }

    // IComponentHandler2
    tresult PLUGIN_API setDirty(TBool state) override
    {
        dirtyCalls.push_back(state != 0);
        return kResultOk;
    }
    tresult PLUGIN_API requestOpenEditor(FIDString /*name*/) override { return kResultOk; }
    tresult PLUGIN_API startGroupEdit() override { return kResultOk; }
    tresult PLUGIN_API finishGroupEdit() override { return kResultOk; }

    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
    {
        if (FUnknownPrivate::iidEqual(iid, IComponentHandler::iid) ||
            FUnknownPrivate::iidEqual(iid, FUnknown::iid))
        {
            *obj = static_cast<IComponentHandler*>(this);
            return kResultOk;
        }
        if (FUnknownPrivate::iidEqual(iid, IComponentHandler2::iid))
        {
            *obj = static_cast<IComponentHandler2*>(this);
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
// the RMS over both output channels. `samplesOut`, when given, receives the
// block's left then right samples.
double processKbBlock(Membrum::Processor& proc,
                      IParameterChanges* changes,
                      KbEventList& events,
                      std::vector<float>* samplesOut = nullptr)
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

    if (samplesOut != nullptr)
    {
        samplesOut->assign(outL.begin(), outL.end());
        samplesOut->insert(samplesOut->end(), outR.begin(), outR.end());
    }

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
Membrum::State::KitSnapshot makeKitSnapshot(bool variantB)
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
    return kit;
}

std::vector<char> makeKitPreset(bool variantB)
{
    return kitBytes(makeKitSnapshot(variantB));
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

// ==============================================================================
// One processor apply per browser load (MB-152, MB-075, MB-110, MB-158)
// ==============================================================================
// The LoadPresetState message is the processor's only apply. The controller
// mirrors the kit with the plain setter and tells the host through
// restartComponent(kParamValuesChanged) and setDirty(true): no begin / perform /
// endEdit replay that applies the kit a second time, rebuilds the coupling
// matrix per pad, or lands a stale kit's values on a later kit.
// ==============================================================================

namespace {

std::size_t firstDifference(const std::vector<char>& a, const std::vector<char>& b)
{
    std::size_t i = 0;
    while (i < std::min(a.size(), b.size()) && a[i] == b[i])
        ++i;
    return i;
}

} // namespace

TEST_CASE("Kit browser load sends no parameter edits and rebuilds the coupling matrix once",
          "[presets][kit][state]")
{
    const auto kit = makeKitSnapshot(false);

    KitBrowserPair pair;
    REQUIRE(pair.browserLoad(kitBytes(kit)));

    INFO("performEdit " << pair.handler.edits.size() << ", beginEdit "
         << pair.handler.beginEdits << ", endEdit " << pair.handler.endEdits);
    CHECK(pair.handler.edits.empty());
    CHECK(pair.handler.beginEdits == 0);
    CHECK(pair.handler.endEdits == 0);

    const auto& flags = pair.handler.restartFlags;
    const auto valuesRestarts = std::count_if(flags.begin(), flags.end(),
        [](int32 f) { return (f & kParamValuesChanged) != 0; });
    CHECK(valuesRestarts == 1);
    const auto& dirty = pair.handler.dirtyCalls;
    CHECK(std::find(dirty.begin(), dirty.end(), true) != dirty.end());

    const auto before = pair.proc->couplingMatrixRecomputeCountForTest();
    pair.forwardEditsAndProcess();
    const auto rebuilds = pair.proc->couplingMatrixRecomputeCountForTest() - before;
    INFO("coupling-matrix rebuilds in the block after the load: " << rebuilds);
    CHECK(rebuilds == 1u);

    // The UI still mirrors the load.
    const auto couplingId = static_cast<ParamID>(
        Membrum::padParamId(4, Membrum::kPadCouplingAmount));
    CHECK(pair.controller.getParamNormalized(couplingId) ==
          kit.pads[4].couplingAmount);
    for (int m = 0; m < 5; ++m)
    {
        const auto macroId = static_cast<ParamID>(
            Membrum::padParamId(0, Membrum::kPadMacroTightness + m));
        CAPTURE(m);
        CHECK(pair.controller.getParamNormalized(macroId) ==
              kit.pads[0].macros[static_cast<std::size_t>(m)]);
    }
}

TEST_CASE("A stale edit batch from an earlier kit load cannot leave a hybrid kit",
          "[presets][kit][state]")
{
    // Kit A: explicit body damping on pad 5. Kit B: the -1 sentinel there, and
    // a different exciter and body on pads 0-9 (the snare, note 38, included).
    const auto snapA = makeKitSnapshot(false);
    auto snapB = makeKitSnapshot(true);
    constexpr int kExciters = static_cast<int>(Membrum::ExciterType::kCount);
    constexpr int kBodies = static_cast<int>(Membrum::BodyModelType::kCount);
    for (int p = 0; p < 10; ++p)
    {
        auto& pad = snapB.pads[static_cast<std::size_t>(p)];
        pad.exciterType = static_cast<Membrum::ExciterType>(
            (static_cast<int>(pad.exciterType) + 3) % kExciters);
        pad.bodyModel = static_cast<Membrum::BodyModelType>(
            (static_cast<int>(pad.bodyModel) + 2) % kBodies);
    }
    const auto kitA = kitBytes(snapA);
    const auto kitB = kitBytes(snapB);

    KitBrowserPair pair;
    REQUIRE(pair.browserLoad(kitA));
    const auto batchA = std::move(pair.handler.edits);
    pair.handler.edits.clear();
    REQUIRE(pair.browserLoad(kitB));

    // Kit A's edits arrive in the block after kit B's message, with a snare hit.
    std::vector<float> got;
    {
        Krate::Test::ParameterChanges changes;
        for (const auto& [id, value] : batchA)
            changes.addChange(id, value);
        pair.events.noteOn(38, 1.0f);
        processKbBlock(*pair.proc, &changes, pair.events, &got);
    }
    pair.forwardEditsAndProcess();

    auto reference = makeKbProcessor(nullptr);
    hostLoad(*reference, kitB);
    KbEventList refEvents;
    refEvents.noteOn(38, 1.0f);
    std::vector<float> want;
    processKbBlock(*reference, nullptr, refEvents, &want);
    processKbBlock(*reference, nullptr, refEvents);

    const auto gotState = processorStateBytes(*pair.proc);
    const auto wantState = processorStateBytes(*reference);
    INFO("stale batch A: " << batchA.size() << " edits; first state difference at byte "
         << firstDifference(gotState, wantState));
    CHECK(gotState == wantState);

    REQUIRE(got.size() == want.size());
    float maxDiff = 0.0f;
    for (std::size_t i = 0; i < got.size(); ++i)
        maxDiff = std::max(maxDiff, std::abs(got[i] - want[i]));
    INFO("note-38 block, max abs diff vs a pure kit-B render: " << maxDiff);
    CHECK(maxDiff <= 1e-6f);

    reference->setActive(false);
    reference->terminate();
}

TEST_CASE("Browser load and host load agree on out-of-range sound slots",
          "[presets][kit][state]")
{
    auto kit = defaultKit();
    kit.pads[0].sound[2] = 1.7;    // decay
    kit.pads[0].sound[0] = -0.3;   // material
    const auto bytes = kitBytes(kit);

    KitBrowserPair pair;
    REQUIRE(pair.browserLoad(bytes));
    pair.forwardEditsAndProcess();

    auto reference = makeKbProcessor(nullptr);
    hostLoad(*reference, bytes);
    KbEventList none;
    processKbBlock(*reference, nullptr, none);

    const auto got = processorStateBytes(*pair.proc);
    const auto want = processorStateBytes(*reference);
    const auto& cfg = pair.proc->voicePoolForTest().padConfig(0);
    INFO("browser-loaded pad 0: decay " << cfg.decay << ", material " << cfg.material
         << "; first state difference at byte " << firstDifference(got, want));
    CHECK(got == want);

    reference->setActive(false);
    reference->terminate();
}

// ==============================================================================
// Kit preset SAVE from the controller (MB-087, MB-162, MB-030, MB-043, MB-166,
// MB-139, MB-152 save half)
// ==============================================================================
// The editor's Save button builds the blob from the controller. noiseLayerGain
// (sound[57]) has no parameter and the body-damping -1 sentinel (sound[42/43])
// cannot be a normalised value, so both must come from a controller mirror of
// the loaded state: a saved kit reloads to the state it was saved from.
// ==============================================================================

namespace {

std::vector<char> readAllBytes(IBStream& stream)
{
    std::vector<char> bytes;
    std::array<char, 4096> chunk{};
    int32 bytesRead = 0;
    while (stream.read(chunk.data(), static_cast<int32>(chunk.size()), &bytesRead) ==
               kResultOk &&
           bytesRead > 0)
    {
        bytes.insert(bytes.end(), chunk.data(), chunk.data() + bytesRead);
    }
    return bytes;
}

void loadComponentState(Membrum::Controller& controller, const std::vector<char>& bytes)
{
    MemoryStream stream;
    stream.write(const_cast<char*>(bytes.data()),
                 static_cast<int32>(bytes.size()), nullptr);
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    REQUIRE(controller.setComponentState(&stream) == kResultOk);
}

Membrum::State::KitSnapshot savedKitSnapshot(Membrum::Controller& controller)
{
    IBStream* saved = controller.kitPresetStateProvider();
    REQUIRE(saved != nullptr);
    Membrum::State::KitSnapshot kit;
    const auto readResult = Membrum::State::readKitBlob(saved, kit);
    saved->release();
    REQUIRE(readResult == kResultOk);
    return kit;
}

} // namespace

TEST_CASE("Kit preset saved from the controller reloads to the processor's own state",
          "[presets][state][save]")
{
    std::vector<char> kit;
    SECTION("default kit") { kit = kitBytes(defaultKit()); }
    SECTION("kit preset A: noiseLayerGain 2.5, explicit damping on pad 5")
    {
        kit = makeKitPreset(false);
    }
    SECTION("kit preset B: noiseLayerGain 0.75, damping sentinel on pad 5")
    {
        kit = makeKitPreset(true);
    }

    KbEventList none;
    auto p1 = makeKbProcessor(nullptr);
    hostLoad(*p1, kit);
    processKbBlock(*p1, nullptr, none);
    const auto bytes0 = processorStateBytes(*p1);

    std::vector<char> saved;
    {
        HostApplication host;
        Membrum::Controller controller;
        REQUIRE(controller.initialize(&host) == kResultOk);
        loadComponentState(controller, bytes0);
        IBStream* s = controller.kitPresetStateProvider();
        REQUIRE(s != nullptr);
        saved = readAllBytes(*s);
        s->release();
        controller.terminate();
    }
    REQUIRE_FALSE(saved.empty());

    auto p2 = makeKbProcessor(nullptr);
    hostLoad(*p2, saved);
    processKbBlock(*p2, nullptr, none);
    const auto bytes1 = processorStateBytes(*p2);

    std::ostringstream pads;
    for (const int p : {0, 1, 2, 5, 6})
    {
        const auto& want = p1->voicePoolForTest().padConfig(p);
        const auto& got = p2->voicePoolForTest().padConfig(p);
        pads << "pad " << p << ": noiseLayerGain " << want.noiseLayerGain << " -> "
             << got.noiseLayerGain << ", bodyDampingB1 " << want.bodyDampingB1
             << " -> " << got.bodyDampingB1 << ", bodyDampingB3 "
             << want.bodyDampingB3 << " -> " << got.bodyDampingB3 << "\n";
    }
    INFO(pads.str());
    INFO("saved and reloaded state " << bytes1.size() << " bytes, original "
         << bytes0.size() << " bytes, first difference at byte "
         << firstDifference(bytes1, bytes0));
    const bool identical = (bytes1 == bytes0);
    CHECK(identical);

    p1->setActive(false);
    p1->terminate();
    p2->setActive(false);
    p2->terminate();
}

TEST_CASE("A sentinel pad loaded after an explicit one saves as the sentinel",
          "[presets][state][save]")
{
    KitBrowserPair pair;
    // Kit A: pad 5 explicit B1 0.4 / B3 0.3. Kit B: pad 5 on the sentinel,
    // pad 1 noiseLayerGain 0.75.
    loadComponentState(pair.controller, makeKitPreset(false));
    REQUIRE(pair.browserLoad(makeKitPreset(true)));

    const auto b1Id = static_cast<ParamID>(
        Membrum::padParamId(5, Membrum::kPadBodyDampingB1));

    SECTION("untouched")
    {
        const auto saved = savedKitSnapshot(pair.controller);
        CHECK(saved.pads[5].sound[42] == -1.0);
        CHECK(saved.pads[5].sound[43] == -1.0);
        CHECK(saved.pads[1].sound[57] == 0.75);
    }

    SECTION("edited")
    {
        // A per-pad edit (VST3Editor, host automation) and a selected-pad
        // proxy edit: each makes the pad's damping an explicit override.
        pair.controller.setParamNormalized(b1Id, 0.3);
        pair.controller.setParamNormalized(Membrum::kSelectedPadId, 5.0 / 31.0);
        pair.controller.setParamNormalized(Membrum::kBodyDampingB3Id, 0.2);

        const auto saved = savedKitSnapshot(pair.controller);
        CHECK(saved.pads[5].sound[42] == 0.3);
        CHECK(saved.pads[5].sound[43] == 0.2);
        CHECK(saved.pads[1].sound[57] == 0.75);
    }
}
