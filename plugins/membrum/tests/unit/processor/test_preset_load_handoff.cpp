// ==============================================================================
// Preset-load hand-off (MB-001, fix plan T1.1)
// ==============================================================================
// Processor::notify("LoadPresetState" / "LoadPadPreset") runs on the host's
// message thread and setState may run there while process() runs on the audio
// thread. Neither may write the voice pool, the pad configs, the allocator or
// the coupling engine directly: the parsed load is handed to the audio thread
// and applied at the top of the next process() call. getState returns the
// loaded state at once (the host contract), even before that block.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"
#include "state/state_codec.h"
#include "dsp/default_kit.h"
#include "dsp/pad_config.h"
#include "dsp/exciter_type.h"
#include "dsp/body_model_type.h"

#include <krate/dsp/core/db_utils.h>

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

#include "vst_event_list.h"
#include "vst_param_changes.h"

#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <memory>
#include <thread>
#include <utility>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

constexpr int32  kHoBlockSize  = 256;
constexpr double kHoSampleRate = 48000.0;

std::unique_ptr<Membrum::Processor> makeHoProcessor(bool activate = true)
{
    auto proc = std::make_unique<Membrum::Processor>();
    REQUIRE(proc->initialize(nullptr) == kResultOk);
    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.maxSamplesPerBlock = kHoBlockSize;
    setup.sampleRate = kHoSampleRate;
    REQUIRE(proc->setupProcessing(setup) == kResultOk);
    if (activate)
        REQUIRE(proc->setActive(true) == kResultOk);
    return proc;
}

void shutdownHo(Membrum::Processor& proc)
{
    proc.setActive(false);
    proc.terminate();
}

// One block with a stereo main bus. Returns true when every sample is finite.
bool processHoBlock(Membrum::Processor& proc,
                    IParameterChanges* changes = nullptr,
                    IEventList* events = nullptr)
{
    std::array<float, kHoBlockSize> outL{};
    std::array<float, kHoBlockSize> outR{};
    std::array<float*, 2> chans{outL.data(), outR.data()};
    AudioBusBuffers bus{};
    bus.numChannels = 2;
    bus.channelBuffers32 = chans.data();

    ProcessData data{};
    data.processMode = kRealtime;
    data.symbolicSampleSize = kSample32;
    data.numSamples = kHoBlockSize;
    data.numOutputs = 1;
    data.outputs = &bus;
    data.inputParameterChanges = changes;
    data.inputEvents = events;
    proc.process(data);

    bool finite = true;
    for (std::size_t i = 0; i < outL.size(); ++i)
    {
        if (!Krate::DSP::detail::isFinite(outL[i]) ||
            !Krate::DSP::detail::isFinite(outR[i]))
            finite = false;
    }
    return finite;
}

std::vector<char> streamBytes(MemoryStream& stream)
{
    const auto size = static_cast<std::size_t>(stream.getSize());
    const char* data = stream.getData();
    return std::vector<char>(data, data + size);
}

std::vector<char> stateBytes(Membrum::Processor& proc)
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

std::vector<char> padPresetBytes(const Membrum::State::PadPresetSnapshot& pad)
{
    MemoryStream stream;
    REQUIRE(Membrum::State::writePadPresetBlob(&stream, pad) == kResultOk);
    return streamBytes(stream);
}

Membrum::State::KitSnapshot defaultKitSnapshot()
{
    std::array<Membrum::PadConfig, Membrum::kNumPads> pads;
    Membrum::DefaultKit::apply(pads);
    Membrum::State::KitSnapshot kit;
    for (int i = 0; i < Membrum::kNumPads; ++i)
        kit.pads[static_cast<std::size_t>(i)] =
            Membrum::State::toPadSnapshot(pads[static_cast<std::size_t>(i)]);
    return kit;
}

// Three kits that differ in a pad's exciter and body type, noiseLayerGain,
// maxPolyphony and globalCoupling (and the selected pad they carry: 0, so a
// kit message and a host load agree on it).
std::vector<char> makeHoKit(int variant)
{
    using Membrum::BodyModelType;
    using Membrum::ExciterType;
    auto kit = defaultKitSnapshot();
    kit.selectedPadIndex = 0;
    switch (variant)
    {
    case 0: // kit A
        kit.maxPolyphony = 12;
        kit.globalCoupling = 0.7;
        kit.snareBuzz = 0.5;
        kit.tomResonance = 0.6;
        kit.pads[1].sound[57] = 2.5;
        kit.pads[3].exciterType = ExciterType::Mallet;
        kit.pads[3].bodyModel = BodyModelType::Membrane;
        break;
    case 1: // kit B
        kit.maxPolyphony = 6;
        kit.globalCoupling = 0.15;
        kit.snareBuzz = 0.05;
        kit.tomResonance = 0.2;
        kit.pads[1].sound[57] = 0.75;
        kit.pads[3].exciterType = ExciterType::FMImpulse;
        kit.pads[3].bodyModel = BodyModelType::Bell;
        kit.pads[5].sound[42] = 0.4;
        kit.pads[5].sound[43] = 0.3;
        break;
    default: // kit C
        kit.maxPolyphony = 16;
        kit.globalCoupling = 0.4;
        kit.pads[1].sound[57] = 4.0;
        kit.pads[3].exciterType = ExciterType::Friction;
        kit.pads[3].bodyModel = BodyModelType::String;
        kit.pads[7].exciterType = ExciterType::NoiseBurst;
        kit.pads[7].bodyModel = BodyModelType::Plate;
        break;
    }
    return kitBytes(kit);
}

std::vector<char> makeHoPadPreset()
{
    std::array<Membrum::PadConfig, Membrum::kNumPads> pads;
    Membrum::DefaultKit::apply(pads);
    auto cfg = pads[10];
    cfg.exciterType = Membrum::ExciterType::Clap;
    cfg.bodyModel = Membrum::BodyModelType::NoiseBody;
    cfg.noiseLayerGain = 3.3f;
    cfg.material = 0.85f;
    cfg.decay = 0.62f;
    return padPresetBytes(Membrum::State::toPadPresetSnapshot(cfg));
}

void hostLoad(Membrum::Processor& proc, const std::vector<char>& bytes)
{
    MemoryStream stream;
    stream.write(const_cast<char*>(bytes.data()),
                 static_cast<int32>(bytes.size()), nullptr);
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    REQUIRE(proc.setState(&stream) == kResultOk);
}

tresult sendKitMessage(Membrum::Processor& proc, const std::vector<char>& bytes)
{
    auto msg = Steinberg::owned(new HostMessage());
    msg->setMessageID("LoadPresetState");
    msg->getAttributes()->setBinary("bytes", bytes.data(),
                                    static_cast<uint32>(bytes.size()));
    return proc.notify(msg);
}

tresult sendPadPresetMessage(Membrum::Processor& proc,
                             const std::vector<char>& bytes, int pad)
{
    auto msg = Steinberg::owned(new HostMessage());
    msg->setMessageID("LoadPadPreset");
    msg->getAttributes()->setBinary("bytes", bytes.data(),
                                    static_cast<uint32>(bytes.size()));
    msg->getAttributes()->setInt("pad", static_cast<int64>(pad));
    return proc.notify(msg);
}

// The state a processor reports after a host load of `bytes` and one block.
std::vector<char> referenceState(const std::vector<char>& bytes)
{
    auto ref = makeHoProcessor();
    hostLoad(*ref, bytes);
    processHoBlock(*ref);
    auto out = stateBytes(*ref);
    shutdownHo(*ref);
    return out;
}

bool samePad(const Membrum::PadConfig& a, const Membrum::PadConfig& b)
{
    const auto sa = Membrum::State::toPadSnapshot(a);
    const auto sb = Membrum::State::toPadSnapshot(b);
    return sa.exciterType == sb.exciterType && sa.bodyModel == sb.bodyModel &&
           sa.sound == sb.sound && sa.chokeGroup == sb.chokeGroup &&
           sa.outputBus == sb.outputBus && sa.couplingAmount == sb.couplingAmount &&
           sa.macros == sb.macros;
}

std::array<Membrum::PadConfig, Membrum::kNumPads>
padConfigs(const Membrum::Processor& proc)
{
    std::array<Membrum::PadConfig, Membrum::kNumPads> out{};
    for (int p = 0; p < Membrum::kNumPads; ++p)
        out[static_cast<std::size_t>(p)] = proc.voicePoolForTest().padConfig(p);
    return out;
}

void checkPadsEqual(const Membrum::Processor& proc,
                    const std::array<Membrum::PadConfig, Membrum::kNumPads>& want)
{
    for (int p = 0; p < Membrum::kNumPads; ++p)
    {
        INFO("pad " << p);
        CHECK(samePad(proc.voicePoolForTest().padConfig(p),
                      want[static_cast<std::size_t>(p)]));
    }
}

} // namespace

TEST_CASE("Preset loads from the message thread reach the engine only inside process()",
          "[processor][state][threading]")
{
    const auto kitA = makeHoKit(0);
    const auto kitB = makeHoKit(1);
    const auto kitC = makeHoKit(2);
    REQUIRE(kitA != kitB);
    REQUIRE(kitB != kitC);
    const auto wantB = referenceState(kitB);
    const auto wantC = referenceState(kitC);
    REQUIRE(wantB != wantC);

    SECTION("one kit message between two blocks")
    {
        auto proc = makeHoProcessor();
        hostLoad(*proc, kitA);
        processHoBlock(*proc);
        const auto padsA = padConfigs(*proc);

        REQUIRE(sendKitMessage(*proc, kitB) == kResultOk);
        checkPadsEqual(*proc, padsA);
        CHECK(stateBytes(*proc) == wantB);

        processHoBlock(*proc);
        CHECK(stateBytes(*proc) == wantB);
        shutdownHo(*proc);
    }

    SECTION("two messages between two blocks")
    {
        auto proc = makeHoProcessor();
        hostLoad(*proc, kitA);
        processHoBlock(*proc);

        REQUIRE(sendKitMessage(*proc, kitB) == kResultOk);
        REQUIRE(sendKitMessage(*proc, kitC) == kResultOk);
        processHoBlock(*proc);
        CHECK(stateBytes(*proc) == wantC);

        // Kit, then a pad preset in the same gap: both apply, in that order.
        const auto padBlob = makeHoPadPreset();
        REQUIRE(sendKitMessage(*proc, kitB) == kResultOk);
        REQUIRE(sendPadPresetMessage(*proc, padBlob, 2) == kResultOk);
        const auto pendingBytes = stateBytes(*proc);
        processHoBlock(*proc);
        const auto got = stateBytes(*proc);

        auto ref = makeHoProcessor();
        hostLoad(*ref, kitB);
        processHoBlock(*ref);
        REQUIRE(sendPadPresetMessage(*ref, padBlob, 2) == kResultOk);
        processHoBlock(*ref);
        const auto wantBPad = stateBytes(*ref);
        shutdownHo(*ref);
        REQUIRE(wantBPad != wantB);
        CHECK(got == wantBPad);
        CHECK(pendingBytes == wantBPad);

        // A pad preset that arrives before a kit in the same gap is replaced
        // by the kit.
        REQUIRE(sendPadPresetMessage(*proc, padBlob, 2) == kResultOk);
        REQUIRE(sendKitMessage(*proc, kitC) == kResultOk);
        processHoBlock(*proc);
        CHECK(stateBytes(*proc) == wantC);
        shutdownHo(*proc);
    }

    SECTION("message while voices ring")
    {
        auto proc = makeHoProcessor();
        hostLoad(*proc, kitA);
        processHoBlock(*proc);

        Krate::Test::EventList events;
        for (const int16 pitch : {int16{36}, int16{38}, int16{41}, int16{42}})
            events.addNoteOn(pitch, 1.0f);
        processHoBlock(*proc, nullptr, &events);
        events.clear();
        for (int b = 0; b < 5; ++b)
            processHoBlock(*proc);

        const int voices = proc->voicePoolForTest().getActiveVoiceCount();
        const int resonators = proc->couplingEngineForTest().getActiveResonatorCount();
        const bool bypassed = proc->couplingEngineForTest().isBypassed();
        const float envelope = proc->energyEnvelopeForTest();
        INFO("voices " << voices << ", resonators " << resonators
             << ", energy envelope " << envelope);
        REQUIRE(voices > 0);

        REQUIRE(sendKitMessage(*proc, kitB) == kResultOk);
        CHECK(proc->voicePoolForTest().getActiveVoiceCount() == voices);
        CHECK(proc->couplingEngineForTest().getActiveResonatorCount() == resonators);
        CHECK(proc->couplingEngineForTest().isBypassed() == bypassed);
        CHECK(proc->energyEnvelopeForTest() == envelope);

        CHECK(processHoBlock(*proc));
        CHECK(stateBytes(*proc) == wantB);
        shutdownHo(*proc);
    }

    SECTION("setState while active")
    {
        auto proc = makeHoProcessor();
        hostLoad(*proc, kitA);
        processHoBlock(*proc);
        const auto padsA = padConfigs(*proc);

        hostLoad(*proc, kitB);
        checkPadsEqual(*proc, padsA);
        CHECK(stateBytes(*proc) == wantB);

        processHoBlock(*proc);
        CHECK(stateBytes(*proc) == wantB);
        shutdownHo(*proc);
    }

    SECTION("setState while inactive")
    {
        auto ref = makeHoProcessor();
        hostLoad(*ref, kitB);
        processHoBlock(*ref);
        const auto padsB = padConfigs(*ref);
        shutdownHo(*ref);

        auto proc = makeHoProcessor(false);
        hostLoad(*proc, kitB);
        CHECK(stateBytes(*proc) == wantB);
        checkPadsEqual(*proc, padsB);
        proc->terminate();
    }

    SECTION("kit load keeps the processor's selected pad")
    {
        auto kit = defaultKitSnapshot();
        kit.selectedPadIndex = 9;
        const auto bytes = kitBytes(kit);

        auto proc = makeHoProcessor();
        Krate::Test::ParameterChanges select;
        select.addChange(Membrum::kSelectedPadId, 5.0 / 31.0);
        processHoBlock(*proc, &select);
        REQUIRE(proc->selectedPadIndexForTest() == 5);

        REQUIRE(sendKitMessage(*proc, bytes) == kResultOk);
        processHoBlock(*proc);
        CHECK(proc->selectedPadIndexForTest() == 5);

        // A host setState carries its selected pad.
        hostLoad(*proc, bytes);
        processHoBlock(*proc);
        CHECK(proc->selectedPadIndexForTest() == 9);
        shutdownHo(*proc);
    }
}

TEST_CASE("Kit loads on a message thread while process() runs leave the engine consistent",
          "[processor][state][threading][stress]")
{
    const std::array<std::vector<char>, 2> kits{makeHoKit(0), makeHoKit(1)};
    constexpr int kLoads = 160;

    auto proc = makeHoProcessor();
    hostLoad(*proc, kits[0]);
    processHoBlock(*proc);

    std::atomic<bool> done{false};
    std::atomic<int> sent{0};
    std::thread messageThread([&] {
        for (int i = 0; i < kLoads; ++i)
        {
            const auto& bytes = kits[static_cast<std::size_t>(i % 2)];
            auto msg = Steinberg::owned(new HostMessage());
            msg->setMessageID("LoadPresetState");
            msg->getAttributes()->setBinary("bytes", bytes.data(),
                                            static_cast<uint32>(bytes.size()));
            if (proc->notify(msg) == kResultOk)
                sent.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        done.store(true, std::memory_order_release);
    });

    Krate::Test::EventList events;
    long blocks = 0;
    long nonFiniteBlocks = 0;
    int nextNote = 36;
    while (!done.load(std::memory_order_acquire))
    {
        if (blocks % 4 == 0)
        {
            events.addNoteOn(static_cast<int16>(nextNote), 0.9f);
            nextNote = nextNote >= 67 ? 36 : nextNote + 1;
        }
        if (!processHoBlock(*proc, nullptr, &events))
            ++nonFiniteBlocks;
        events.clear();
        ++blocks;
    }
    messageThread.join();
    processHoBlock(*proc);
    processHoBlock(*proc);

    INFO("blocks " << blocks << ", loads sent " << sent.load()
         << ", blocks with non-finite samples " << nonFiniteBlocks);
    CHECK(sent.load() == kLoads);
    CHECK(nonFiniteBlocks == 0);
    CHECK(stateBytes(*proc) == referenceState(kits[(kLoads - 1) % 2]));
    shutdownHo(*proc);
}
