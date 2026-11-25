// ==============================================================================
// Kit switch while voices ring (MB-054, MB-091, fix plan T2.3)
// ==============================================================================
// A kit load (host setState, or the "LoadPresetState" browser message) used to
// wipe every ringing voice in one sample (VoicePool::resetAllVoicesForKitSwitch),
// hard-reset the coupling return, keep the bus limiter's gain reduction from
// the old kit and step the master gain. The audit probe measured a 0.0290 step
// at a pre-switch level of -13.70 dBFS and one click.
//
// The kit arrives through the calls a host makes: setState between two
// process() calls, and a LoadPresetState IMessage to Processor::notify between
// two process() calls. Step bound: the largest sample-to-sample step in the
// 10 ms after the switch is at most -30 dB relative to the main-bus peak of the
// 10 ms before it (the steal / choke click criterion, spec 138 SC-021 / SC-022).
// The click count uses the audit probe's window (2048 samples either side of
// the switch), so the detector's statistics see the ringing signal before it.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "processor/processor.h"
#include "plugin_ids.h"
#include "state/state_codec.h"
#include "dsp/default_kit.h"
#include "dsp/pad_config.h"

#include <krate/dsp/core/db_utils.h>

#include <artifact_detection.h>
#include "vst_event_list.h"

#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/vstpresetfile.h"
#include "pluginterfaces/base/ibstream.h"
#include "pluginterfaces/vst/ivstaudioprocessor.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

constexpr int32  kKsBlock  = 256;
constexpr double kKsRate   = 48000.0;
// The audit probe's switch point: 37 blocks (9472 samples, ~200 ms) after the
// eight hits at sample 0.
constexpr int    kKsSwitchBlock = 37;
constexpr std::size_t kKsWindow = 480;            // 10 ms at 48 kHz
constexpr float  kKsStepRatio = 0.031622777f;     // -30 dB
constexpr std::size_t kKsClickHalfWindow = 2048;  // the probe's click window

// ---- Kit bytes ---------------------------------------------------------------

std::vector<char> streamBytes(MemoryStream& stream)
{
    const auto size = static_cast<std::size_t>(stream.getSize());
    const char* data = stream.getData();
    return std::vector<char>(data, data + size);
}

std::vector<char> kitBytes(const Membrum::State::KitSnapshot& kit)
{
    MemoryStream stream;
    REQUIRE(Membrum::State::writeKitBlob(&stream, kit) == kResultOk);
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

std::filesystem::path factoryKitRoot()
{
    auto cur = std::filesystem::current_path();
    for (int i = 0; i < 8; ++i)
    {
        const auto candidate =
            cur / "plugins" / "membrum" / "resources" / "presets" / "Kit Presets";
        if (std::filesystem::exists(candidate))
            return candidate;
        if (!cur.has_parent_path() || cur.parent_path() == cur)
            break;
        cur = cur.parent_path();
    }
    return {};
}

// The component-state chunk (the kit blob) of a factory .vstpreset.
std::vector<char> factoryKitBytes(const std::string& subcat, const std::string& name)
{
    const auto root = factoryKitRoot();
    REQUIRE_FALSE(root.empty());
    const auto path = root / subcat / (name + ".vstpreset");
    REQUIRE(std::filesystem::exists(path));

    std::ifstream in(path, std::ios::binary);
    std::vector<char> file((std::istreambuf_iterator<char>(in)),
                           std::istreambuf_iterator<char>());
    auto presetStream = owned(new MemoryStream(file.data(), static_cast<TSize>(file.size())));
    PresetFile pf(presetStream);
    REQUIRE(pf.readChunkList());
    const auto* comp = pf.getEntry(kComponentState);
    REQUIRE(comp != nullptr);
    return std::vector<char>(file.begin() + static_cast<std::ptrdiff_t>(comp->offset),
                             file.begin() + static_cast<std::ptrdiff_t>(comp->offset + comp->size));
}

Membrum::State::KitSnapshot parseKit(const std::vector<char>& bytes)
{
    MemoryStream stream;
    stream.write(const_cast<char*>(bytes.data()), static_cast<int32>(bytes.size()), nullptr);
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    Membrum::State::KitSnapshot kit;
    REQUIRE(Membrum::State::readKitBlob(&stream, kit) == kResultOk);
    return kit;
}

float masterGainLinear(double norm)
{
    return std::pow(10.0f, (-24.0f + 36.0f * static_cast<float>(norm)) * 0.05f);
}

void hostLoad(Membrum::Processor& proc, const std::vector<char>& bytes)
{
    MemoryStream stream;
    stream.write(const_cast<char*>(bytes.data()), static_cast<int32>(bytes.size()), nullptr);
    stream.seek(0, IBStream::kIBSeekSet, nullptr);
    REQUIRE(proc.setState(&stream) == kResultOk);
}

tresult sendKitMessage(Membrum::Processor& proc, const std::vector<char>& bytes)
{
    auto msg = owned(new HostMessage());
    msg->setMessageID("LoadPresetState");
    msg->getAttributes()->setBinary("bytes", bytes.data(), static_cast<uint32>(bytes.size()));
    return proc.notify(msg);
}

enum class LoadPath { SetState, Message };

void loadKit(Membrum::Processor& proc, const std::vector<char>& bytes, LoadPath path)
{
    if (path == LoadPath::SetState)
        hostLoad(proc, bytes);
    else
        REQUIRE(sendKitMessage(proc, bytes) == kResultOk);
}

// ---- Rig -----------------------------------------------------------------------

// A processor at 48 kHz, block 256, that records its main bus.
struct KsRig
{
    std::unique_ptr<Membrum::Processor> proc = std::make_unique<Membrum::Processor>();
    std::vector<float> L;
    std::vector<float> R;
    Krate::Test::EventList events;
    bool finite = true;

    KsRig()
    {
        REQUIRE(proc->initialize(nullptr) == kResultOk);
        ProcessSetup setup{};
        setup.processMode = kRealtime;
        setup.symbolicSampleSize = kSample32;
        setup.maxSamplesPerBlock = kKsBlock;
        setup.sampleRate = kKsRate;
        REQUIRE(proc->setupProcessing(setup) == kResultOk);
        REQUIRE(proc->setActive(true) == kResultOk);
        L.reserve(static_cast<std::size_t>(kKsRate * 3.0));
        R.reserve(static_cast<std::size_t>(kKsRate * 3.0));
    }

    ~KsRig()
    {
        proc->setActive(false);
        proc->terminate();
    }

    KsRig(const KsRig&) = delete;
    KsRig& operator=(const KsRig&) = delete;
    KsRig(KsRig&&) = delete;
    KsRig& operator=(KsRig&&) = delete;

    // One block; the queued events apply in it and are cleared after.
    void block()
    {
        std::array<float, kKsBlock> outL{};
        std::array<float, kKsBlock> outR{};
        std::array<float*, 2> chans{outL.data(), outR.data()};
        AudioBusBuffers bus{};
        bus.numChannels = 2;
        bus.channelBuffers32 = chans.data();
        ProcessData data{};
        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = kKsBlock;
        data.numOutputs = 1;
        data.outputs = &bus;
        data.inputEvents = &events;
        proc->process(data);
        events.clear();
        for (int32 i = 0; i < kKsBlock; ++i)
        {
            const auto s = static_cast<std::size_t>(i);
            if (!Krate::DSP::detail::isFinite(outL[s]) || !Krate::DSP::detail::isFinite(outR[s]))
                finite = false;
            L.push_back(outL[s]);
            R.push_back(outR[s]);
        }
    }

    void blocks(int n)
    {
        for (int i = 0; i < n; ++i)
            block();
    }

    // The probe's eight hits: pads 36..43 at full velocity, sample 0 of the
    // next block.
    void hitEight()
    {
        for (int16 note = 36; note < 44; ++note)
            events.addNoteOn(note, 1.0f, 0);
    }

    [[nodiscard]] std::size_t pos() const { return L.size(); }
};

float peakOf(const std::vector<float>& x, std::size_t from, std::size_t to)
{
    float p = 0.0f;
    for (std::size_t i = from; i < std::min(to, x.size()); ++i)
        p = std::max(p, std::fabs(x[i]));
    return p;
}

float maxStepOf(const std::vector<float>& x, std::size_t from, std::size_t to)
{
    float s = 0.0f;
    for (std::size_t i = std::max<std::size_t>(from, 1); i < std::min(to, x.size()); ++i)
        s = std::max(s, std::fabs(x[i] - x[i - 1]));
    return s;
}

double rmsOf(const std::vector<float>& x, std::size_t from, std::size_t to)
{
    double acc = 0.0;
    std::size_t n = 0;
    for (std::size_t i = from; i < std::min(to, x.size()); ++i, ++n)
        acc += static_cast<double>(x[i]) * static_cast<double>(x[i]);
    return n > 0 ? std::sqrt(acc / static_cast<double>(n)) : 0.0;
}

int clickCountOf(const std::vector<float>& x, std::size_t from, std::size_t to)
{
    to = std::min(to, x.size());
    Krate::DSP::TestUtils::ClickDetectorConfig cfg;
    cfg.sampleRate = static_cast<float>(kKsRate);
    cfg.frameSize = 512;
    cfg.hopSize = 256;
    cfg.detectionThreshold = 5.0f;
    Krate::DSP::TestUtils::ClickDetector det(cfg);
    det.prepare();
    return static_cast<int>(det.detect(x.data() + from, to - from).size());
}

struct SwitchMetrics
{
    float prePeak = 0.0f;   // main-bus peak of the 10 ms before the switch
    float maxStep = 0.0f;   // largest step in the 10 ms after it (from c-1 -> c on)
    int   clicks = 0;       // ClickDetector, 2048 samples either side
    float preStep = 0.0f;   // largest step of the 10 ms before (the signal's own)
};

SwitchMetrics measureSwitch(const KsRig& r, std::size_t c)
{
    SwitchMetrics m;
    m.prePeak = std::max(peakOf(r.L, c - kKsWindow, c), peakOf(r.R, c - kKsWindow, c));
    m.maxStep = std::max(maxStepOf(r.L, c, c + kKsWindow), maxStepOf(r.R, c, c + kKsWindow));
    m.preStep = std::max(maxStepOf(r.L, c - kKsWindow, c), maxStepOf(r.R, c - kKsWindow, c));
    m.clicks = clickCountOf(r.L, c - kKsClickHalfWindow, c + kKsClickHalfWindow)
             + clickCountOf(r.R, c - kKsClickHalfWindow, c + kKsClickHalfWindow);
    return m;
}

void checkSwitch(const SwitchMetrics& m)
{
    INFO("pre-switch natural max step " << m.preStep);
    INFO("pre-switch peak " << m.prePeak << " (" << 20.0f * std::log10(m.prePeak + 1e-30f)
         << " dBFS), max step after " << m.maxStep << ", bound " << m.prePeak * kKsStepRatio
         << ", clicks " << m.clicks);
    REQUIRE(m.prePeak > 0.01f);  // voices really ring at the switch
    CHECK(m.maxStep <= m.prePeak * kKsStepRatio);
    CHECK(m.clicks == 0);
}

const char* pathName(LoadPath p)
{
    return p == LoadPath::SetState ? "setState" : "LoadPresetState";
}

} // namespace

// ==============================================================================

TEST_CASE("Kit switch: ringing voices fade out instead of being cut", "[kit_switch][click]")
{
    const auto studio = factoryKitBytes("Acoustic", "Acoustic Studio Kit");
    for (const LoadPath path : {LoadPath::SetState, LoadPath::Message})
    {
        DYNAMIC_SECTION("via " << pathName(path))
        {
            KsRig r;
            r.hitEight();
            r.blocks(kKsSwitchBlock);
            const std::size_t c = r.pos();
            loadKit(*r.proc, studio, path);
            r.blocks(static_cast<int>(1.5 * kKsRate) / kKsBlock);

            checkSwitch(measureSwitch(r, c));
            CHECK(r.finite);

            // No ring left behind: 1.2-1.3 s after the switch the bus is at
            // the probe's -240 dBFS floor.
            const auto from = c + static_cast<std::size_t>(1.2 * kKsRate);
            const auto to = c + static_cast<std::size_t>(1.3 * kKsRate);
            CHECK(std::max(rmsOf(r.L, from, to), rmsOf(r.R, from, to)) <= 1.0e-12);
        }
    }
}

TEST_CASE("Kit switch: the coupling tail fades instead of being cut", "[kit_switch][click]")
{
    // The old kit couples at 1.0. A twin with coupling 0 plays the same
    // voices, so (coupled - twin) is the coupling return on the bus. A cut
    // steps that component by its whole instantaneous value; a fade keeps
    // its steps at its own sample-to-sample rate of change.
    auto coupled = defaultKitSnapshot();
    coupled.globalCoupling = 1.0;
    coupled.snareBuzz = 0.5;
    coupled.tomResonance = 0.5;
    auto plain = coupled;
    plain.globalCoupling = 0.0;
    const auto coupledBytes = kitBytes(coupled);
    const auto plainBytes = kitBytes(plain);
    const auto studio = factoryKitBytes("Acoustic", "Acoustic Studio Kit");

    const auto render = [&](const std::vector<char>& oldKit, LoadPath path, KsRig& r) {
        hostLoad(*r.proc, oldKit);
        r.block();
        r.hitEight();
        r.blocks(kKsSwitchBlock);
        const std::size_t c = r.pos();
        loadKit(*r.proc, studio, path);
        r.blocks(static_cast<int>(1.5 * kKsRate) / kKsBlock);
        return c;
    };

    for (const LoadPath path : {LoadPath::SetState, LoadPath::Message})
    {
        DYNAMIC_SECTION("via " << pathName(path))
        {
            KsRig twin;
            (void)render(plainBytes, path, twin);

            KsRig r;
            hostLoad(*r.proc, coupledBytes);
            r.block();
            r.hitEight();
            r.blocks(kKsSwitchBlock);
            // The coupling return really rings at the switch.
            REQUIRE_FALSE(r.proc->couplingEngineForTest().isBypassed());
            REQUIRE(r.proc->couplingEngineForTest().getActiveResonatorCount() > 0);
            const std::size_t c = r.pos();
            loadKit(*r.proc, studio, path);
            r.blocks(static_cast<int>(1.5 * kKsRate) / kKsBlock);
            REQUIRE(r.pos() == twin.pos());

            // Main bus: the step bound of the first case.
            checkSwitch(measureSwitch(r, c));
            CHECK(r.finite);

            // The coupling component.
            std::vector<float> dL(r.pos());
            std::vector<float> dR(r.pos());
            for (std::size_t i = 0; i < r.pos(); ++i)
            {
                dL[i] = r.L[i] - twin.L[i];
                dR[i] = r.R[i] - twin.R[i];
            }
            const float couplingPeak = std::max(peakOf(dL, c - kKsWindow, c), peakOf(dR, c - kKsWindow, c));
            const float couplingPreStep =
                std::max(maxStepOf(dL, c - kKsWindow, c), maxStepOf(dR, c - kKsWindow, c));
            const float couplingStep =
                std::max(maxStepOf(dL, c, c + kKsWindow), maxStepOf(dR, c, c + kKsWindow));
            INFO("coupling component: pre-switch peak " << couplingPeak << ", own max step before "
                 << couplingPreStep << ", max step after " << couplingStep);
            REQUIRE(couplingPeak > 1.0e-3f);
            CHECK(couplingStep <= couplingPreStep);

            const auto from = c + static_cast<std::size_t>(1.2 * kKsRate);
            const auto to = c + static_cast<std::size_t>(1.3 * kKsRate);
            CHECK(std::max(rmsOf(r.L, from, to), rmsOf(r.R, from, to)) <= 1.0e-12);
        }
    }
}

TEST_CASE("Kit switch: the first hit of the new kit is not ducked by the old kit's limiter state",
          "[kit_switch][limiter]")
{
    // Glass Bell Garden pad 46 (FMImpulse / Bell) is at the bus limiter's
    // ceiling in specs/membrum-audit/probes/limiter-pads.csv (+2.24 dBTP
    // before the limiter at the audit). Phase 1 lowered it to about -4 dB
    // under the ceiling, so it is struck together with pads 36..42 of the
    // same kit; the assertion below checks that this hit really drives the
    // limiter. The new kit's hit is Acoustic Studio Kit pad 38.
    const auto glass = factoryKitBytes("Unnatural", "Glass Bell Garden");
    const auto studio = factoryKitBytes("Acoustic", "Acoustic Studio Kit");
    const float glassMaster = masterGainLinear(parseKit(glass).masterGainNorm);

    constexpr int kHitToSwitchBlocks = 4;   // switch ~21 ms after the old hit
    // The fade ends 5 ms (240 samples) into the switch block; the new hit
    // lands 10 ms (480 samples) after that.
    constexpr int32 kNewHitAfterSwitch = 240 + 480;
    constexpr int kNewHitBlock = kNewHitAfterSwitch / kKsBlock;
    constexpr int32 kNewHitOffset = kNewHitAfterSwitch % kKsBlock;
    constexpr std::size_t kHitWindow = static_cast<std::size_t>(0.1 * kKsRate);

    // Reference: a fresh processor with the new kit loaded, the same hit at
    // the same block position.
    KsRig ref;
    hostLoad(*ref.proc, studio);
    ref.blocks(1 + kHitToSwitchBlocks + kNewHitBlock);
    ref.events.addNoteOn(38, 1.0f, kNewHitOffset);
    const std::size_t refHit = ref.pos() + static_cast<std::size_t>(kNewHitOffset);
    ref.blocks(static_cast<int>(kHitWindow) / kKsBlock + 2);
    const float refPeak = std::max(peakOf(ref.L, refHit, refHit + kHitWindow),
                                   peakOf(ref.R, refHit, refHit + kHitWindow));

    KsRig r;
    hostLoad(*r.proc, glass);
    r.block();
    r.events.addNoteOn(46, 1.0f, 0);
    for (int16 note = 36; note < 43; ++note)
        r.events.addNoteOn(note, 1.0f, 0);
    const std::size_t oldHit = r.pos();
    r.blocks(kHitToSwitchBlocks);
    // The old hit drives the limiter: its post-master peak reaches the
    // -1 dBTP ceiling (0.891) times the kit's master gain within 0.5 dB.
    const float oldPeak = std::max(peakOf(r.L, oldHit, r.pos()), peakOf(r.R, oldHit, r.pos()));
    INFO("old hit peak " << oldPeak << ", ceiling x master " << 0.891251f * glassMaster);
    REQUIRE(oldPeak >= 0.891251f * glassMaster * 0.944061f);

    hostLoad(*r.proc, studio);
    r.blocks(kNewHitBlock);
    r.events.addNoteOn(38, 1.0f, kNewHitOffset);
    const std::size_t newHit = r.pos() + static_cast<std::size_t>(kNewHitOffset);
    r.blocks(static_cast<int>(kHitWindow) / kKsBlock + 2);
    const float newPeak = std::max(peakOf(r.L, newHit, newHit + kHitWindow),
                                   peakOf(r.R, newHit, newHit + kHitWindow));

    const float diffDb = 20.0f * std::log10(newPeak / refPeak);
    INFO("new-kit hit peak " << newPeak << " vs fresh " << refPeak << " -> " << diffDb << " dB");
    REQUIRE(refPeak > 0.01f);
    CHECK(std::fabs(diffDb) <= 0.1f);
    CHECK(r.finite);
}

TEST_CASE("Kit switch: a master gain change at a kit load does not step", "[kit_switch][click]")
{
    auto quiet = defaultKitSnapshot();
    quiet.masterGainNorm = 0.5;          // -6 dB
    auto loud = defaultKitSnapshot();
    loud.masterGainNorm = 1.0;           // +12 dB
    const auto quietBytes = kitBytes(quiet);
    const auto loudBytes = kitBytes(loud);

    for (const LoadPath path : {LoadPath::SetState, LoadPath::Message})
    {
        DYNAMIC_SECTION("via " << pathName(path))
        {
            KsRig r;
            hostLoad(*r.proc, quietBytes);
            r.block();
            r.hitEight();
            r.blocks(kKsSwitchBlock);
            const std::size_t c = r.pos();
            loadKit(*r.proc, loudBytes, path);
            r.blocks(40);

            checkSwitch(measureSwitch(r, c));
            CHECK(r.finite);
        }
    }
}

TEST_CASE("Kit switch: deterministic interleavings keep the fade", "[kit_switch][threading]")
{
    const auto studio = factoryKitBytes("Acoustic", "Acoustic Studio Kit");

    SECTION("one message between two blocks")
    {
        KsRig r;
        r.events.addNoteOn(41, 1.0f, 0);   // one ringing tom
        r.blocks(20);
        const std::size_t c = r.pos();
        REQUIRE(sendKitMessage(*r.proc, studio) == kResultOk);
        r.blocks(40);
        checkSwitch(measureSwitch(r, c));
        CHECK(r.proc->voicePoolForTest().getActiveVoiceCount() == 0);
        CHECK(r.finite);
    }

    SECTION("two kit messages between two blocks: the second wins, one fade")
    {
        auto small = defaultKitSnapshot();
        small.maxPolyphony = 5;
        auto big = defaultKitSnapshot();
        big.maxPolyphony = 13;
        KsRig r;
        r.hitEight();
        r.blocks(kKsSwitchBlock);
        const std::size_t c = r.pos();
        REQUIRE(sendKitMessage(*r.proc, kitBytes(small)) == kResultOk);
        REQUIRE(sendKitMessage(*r.proc, kitBytes(big)) == kResultOk);
        r.block();
        CHECK(r.proc->voicePoolForTest().maxPolyphony() == 13);
        // Every ringing voice is in one fade (shadow role); none still plays.
        CHECK(r.proc->voicePoolForTest().getActiveVoiceCount() == 0);
        r.blocks(40);
        checkSwitch(measureSwitch(r, c));
        CHECK(r.finite);
    }

    SECTION("a message while eight voices ring")
    {
        KsRig r;
        r.hitEight();
        r.blocks(kKsSwitchBlock);
        REQUIRE(r.proc->voicePoolForTest().getActiveVoiceCount() == 8);
        const std::size_t c = r.pos();
        REQUIRE(sendKitMessage(*r.proc, studio) == kResultOk);
        r.blocks(40);
        checkSwitch(measureSwitch(r, c));
        CHECK(r.proc->voicePoolForTest().getActiveVoiceCount() == 0);
        CHECK_FALSE(r.proc->voicePoolForTest().isAnyVoiceActive());
        CHECK(r.finite);
    }
}

TEST_CASE("Kit switch: message thread and audio thread run concurrently", "[kit_switch][threading]")
{
    auto kitA = defaultKitSnapshot();
    kitA.globalCoupling = 0.7;
    kitA.maxPolyphony = 12;
    auto kitB = defaultKitSnapshot();
    kitB.globalCoupling = 0.2;
    kitB.maxPolyphony = 6;
    kitB.masterGainNorm = 0.8;
    const std::array<std::vector<char>, 2> kits{kitBytes(kitA), kitBytes(kitB)};
    constexpr int kLoads = 200;

    // A ringing low tom (pad 41): the first load lands at a block the thread
    // timing picks, so the voice must be one whose own sample-to-sample
    // steps stay under the bound at any moment (about -36 dB of its peak);
    // the eight-hit mix of the cases above carries partials whose own steps
    // reach -30 dB at some block boundaries, with or without a switch.
    KsRig r;
    r.events.addNoteOn(41, 1.0f, 0);
    r.blocks(20);
    const std::size_t start = r.pos();
    const float prePeak = std::max(peakOf(r.L, start - kKsWindow, start),
                                   peakOf(r.R, start - kKsWindow, start));
    REQUIRE(prePeak > 0.01f);

    std::atomic<bool> done{false};
    std::atomic<int> sent{0};
    std::thread messageThread([&] {
        for (int i = 0; i < kLoads; ++i)
        {
            if (sendKitMessage(*r.proc, kits[static_cast<std::size_t>(i % 2)]) == kResultOk)
                sent.fetch_add(1, std::memory_order_relaxed);
            std::this_thread::sleep_for(std::chrono::microseconds(500));
        }
        done.store(true, std::memory_order_release);
    });

    while (!done.load(std::memory_order_acquire))
        r.block();
    messageThread.join();
    r.blocks(4);

    const float maxStep = std::max(maxStepOf(r.L, start, r.pos()), maxStepOf(r.R, start, r.pos()));
    INFO("loads sent " << sent.load() << ", blocks " << (r.pos() - start) / kKsBlock
         << ", pre-switch peak " << prePeak << ", max step " << maxStep
         << ", bound " << prePeak * kKsStepRatio);
    CHECK(sent.load() == kLoads);
    CHECK(r.finite);
    CHECK(maxStep <= prePeak * kKsStepRatio);
}
