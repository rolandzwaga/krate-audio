// ==============================================================================
// Display delivery and unload safety
// ==============================================================================
// The open editor's spectrum and level meters keep receiving the processor's
// audio whatever thread the host (re)activates the processor on, and no
// teardown order leaves a timer or a pointer behind.
//
// Without a host IDataExchangeHandler the SDK's DataExchange fallback drains its
// queue from a timer bound to the thread that opened the queue (setActive). A
// host that reactivates on a thread that dispatches no messages left that queue
// full forever, and a deactivation on another thread could not kill the timer.
// The SDK timers and the VSTGUI timers are thread timers on Windows, so the
// cases dispatch this thread's messages; macOS and Linux test harnesses run no
// loop that fires them, so the DataExchange-dependent checks are Windows-only.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "../test_support/disrumpo_rig.h"
#include "../test_support/headless_editor.h"

#include "controller/controller.h"
#include "controller/views/level_meter_view.h"
#include "controller/views/spectrum_display.h"
#include "plugin_ids.h"
#include "processor/processor.h"

#include "base/source/timer.h"
#include "public.sdk/source/common/memorystream.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <mutex>
#include <numbers>
#include <random>
#include <set>
#include <thread>
#include <utility>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;

namespace {

/// (timer id, TIMERPROC) of a thread timer that fired while dispatching.
using FiredTimers = std::set<std::pair<std::uintptr_t, std::intptr_t>>;

/// Dispatches this thread's messages for `ms` milliseconds (at least one pass).
/// On Windows every WM_TIMER is recorded in `fired` when one is given.
void pumpMessages(int ms, FiredTimers* fired = nullptr)
{
    const auto start = std::chrono::steady_clock::now();
    for (;;) {
#ifdef _WIN32
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
            if (fired != nullptr && msg.message == WM_TIMER && msg.hwnd == nullptr)
                fired->emplace(static_cast<std::uintptr_t>(msg.wParam), static_cast<std::intptr_t>(msg.lParam));
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
#else
        (void)fired;
#endif
        if (std::chrono::steady_clock::now() - start >= std::chrono::milliseconds(ms)) return;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}

void collectLevelMeters(VSTGUI::CViewContainer* container, std::vector<Disrumpo::LevelMeterView*>& out)
{
    container->forEachChild([&out](VSTGUI::CView* child) {
        if (auto* m = dynamic_cast<Disrumpo::LevelMeterView*>(child)) out.push_back(m);
        if (auto* sub = child->asViewContainer()) collectLevelMeters(sub, out);
    });
}

/// A processor and a controller connected the way a host connects them, with
/// HostApplication as the host context: no IDataExchangeHandler, so DataExchange
/// runs on the SDK's fallback timer.
struct ConnectedPair {
    static constexpr double kRate = 48000.0;
    static constexpr int32 kBlock = 512;

    HostApplication host;
    std::unique_ptr<Disrumpo::Processor> proc = std::make_unique<Disrumpo::Processor>();
    std::unique_ptr<Disrumpo::Controller> ctrl = std::make_unique<Disrumpo::Controller>();
    std::vector<float> inL = std::vector<float>(kBlock);
    std::vector<float> inR = std::vector<float>(kBlock);
    std::vector<float> outL = std::vector<float>(kBlock);
    std::vector<float> outR = std::vector<float>(kBlock);
    size_t sample = 0;
    bool connected = false;

    ConnectedPair()
    {
        REQUIRE(proc->initialize(&host) == kResultOk);
        REQUIRE(ctrl->initialize(&host) == kResultOk);
        REQUIRE(proc->connect(ctrlConn()) == kResultOk);
        REQUIRE(ctrl->connect(procConn()) == kResultOk);
        connected = true;
        ProcessSetup setup{};
        setup.processMode = kRealtime;
        setup.symbolicSampleSize = kSample32;
        setup.sampleRate = kRate;
        setup.maxSamplesPerBlock = kBlock;
        REQUIRE(proc->setupProcessing(setup) == kResultOk);
        // The host hands the processor's state to the controller (its instance ID: Tier 3)
        auto state = Disrumpo::TestSupport::getStateBytes(*proc);
        auto* stream = new MemoryStream(state.data(), static_cast<TSize>(state.size()));
        REQUIRE(ctrl->setComponentState(stream) == kResultOk);
        stream->release();
    }
    ~ConnectedPair() { teardown(); }
    ConnectedPair(const ConnectedPair&) = delete;
    ConnectedPair& operator=(const ConnectedPair&) = delete;
    ConnectedPair(ConnectedPair&&) = delete;
    ConnectedPair& operator=(ConnectedPair&&) = delete;

    IConnectionPoint* procConn() const { return static_cast<IConnectionPoint*>(static_cast<AudioEffect*>(proc.get())); }
    IConnectionPoint* ctrlConn() const
    {
        return static_cast<IConnectionPoint*>(static_cast<EditControllerEx1*>(ctrl.get()));
    }

    void teardown()
    {
        if (!connected) return;
        proc->setActive(static_cast<TBool>(false));
        proc->disconnect(ctrlConn());
        ctrl->disconnect(procConn());
        ctrl->terminate();
        proc->terminate();
        connected = false;
    }

    /// One block of a 1 kHz sine at `amplitude` on both channels.
    void process(float amplitude, IParameterChanges* in = nullptr, IParameterChanges* out = nullptr)
    {
        for (int32 i = 0; i < kBlock; ++i) {
            const double t = static_cast<double>(sample + static_cast<size_t>(i)) / kRate;
            inL[static_cast<size_t>(i)] = amplitude * static_cast<float>(std::sin(2.0 * std::numbers::pi * 1000.0 * t));
        }
        inR = inL;
        sample += static_cast<size_t>(kBlock);
        float* inCh[2] = {inL.data(), inR.data()};
        float* outCh[2] = {outL.data(), outR.data()};
        AudioBusBuffers inBus{};
        inBus.numChannels = 2;
        inBus.channelBuffers32 = inCh;
        AudioBusBuffers outBus{};
        outBus.numChannels = 2;
        outBus.channelBuffers32 = outCh;
        ProcessData data{};
        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = kBlock;
        data.numInputs = 1;
        data.numOutputs = 1;
        data.inputs = &inBus;
        data.outputs = &outBus;
        data.inputParameterChanges = in;
        data.outputParameterChanges = out;
        proc->process(data);
    }
};

/// What the open editor shows: the samples its spectrum analysis has been fed and the input meter.
struct DisplayReading {
    size_t spectrumSamples = 0;
    float inputPeakDb = Disrumpo::MeterBallistics::kFloorDb;
};

DisplayReading readDisplays(Disrumpo::Controller& ctrl, VSTGUI::CFrame* frame)
{
    DisplayReading r;
    if (const auto* display = ctrl.spectrumDisplayForTest(); display != nullptr && display->isAnalysisActive()) {
        if (const auto* fifo = display->outputFIFOForTest(); fifo != nullptr) r.spectrumSamples = fifo->totalWritten();
    }
    std::vector<Disrumpo::LevelMeterView*> meters;
    collectLevelMeters(frame, meters);
    for (const auto* m : meters)
        if (m->source() == Disrumpo::LevelMeterView::Source::Input) r.inputPeakDb = m->channel(0).peakDb();
    return r;
}

constexpr float kQuiet = 0.0630957f;  // -24 dBFS
constexpr float kLoud = 0.7079458f;   // -3 dBFS

}  // namespace

// The user's report: with the editor open, loading presets and toggling bypass
// froze the spectrum and dropped the meters until the editor was reopened. A host
// that reactivates on a thread with no message loop left the DataExchange queue
// undrained; the controller kept waiting for DataExchange and never read the
// processor's shared display (Tier 3).
TEST_CASE("Displays keep receiving audio after a reactivation on a thread that dispatches no messages",
          "[disrumpo][data-exchange][display-recovery]")
{
    ConnectedPair pair;
    REQUIRE(pair.proc->setActive(static_cast<TBool>(true)) == kResultOk);
    Disrumpo::TestSupport::HeadlessEditor ed(*pair.ctrl);
    REQUIRE(ed.ready());

    // DataExchange delivers through the fallback timer on this thread
    for (int i = 0; i < 30; ++i) {
        pair.process(kQuiet);
        pumpMessages(2);
        pair.ctrl->tickEditorTimerForTest();
    }
#ifdef _WIN32
    REQUIRE(pair.ctrl->isDataExchangeDeliveringForTest());
#endif

    // The host deactivates here and reactivates from a thread that dispatches no messages
    REQUIRE(pair.proc->setActive(static_cast<TBool>(false)) == kResultOk);
    std::thread([&pair] { pair.proc->setActive(static_cast<TBool>(true)); }).join();

    const auto before = readDisplays(*pair.ctrl, ed.frame());
    for (int i = 0; i < 30; ++i) {
        pair.process(kLoud);
        pumpMessages(2);
        pair.ctrl->tickEditorTimerForTest();
    }
    const auto after = readDisplays(*pair.ctrl, ed.frame());
    std::printf("Reactivated on a thread without a message loop, 30 blocks of -3 dBFS: spectrum fed %zu new samples "
                "(want >= 4096), input meter %.2f -> %.2f dB (want >= -3.5)\n",
                after.spectrumSamples - before.spectrumSamples, static_cast<double>(before.inputPeakDb),
                static_cast<double>(after.inputPeakDb));
    CHECK(after.spectrumSamples - before.spectrumSamples >= 4096u);
    CHECK(after.inputPeakDb >= -3.5f);

    // Reactivated on this thread again: DataExchange feeds the displays again
    REQUIRE(pair.proc->setActive(static_cast<TBool>(false)) == kResultOk);
    REQUIRE(pair.proc->setActive(static_cast<TBool>(true)) == kResultOk);
    const size_t localBefore = pair.ctrl->getLocalOutputFIFO().totalWritten();
    const auto resumedBefore = readDisplays(*pair.ctrl, ed.frame());
    for (int i = 0; i < 30; ++i) {
        pair.process(kQuiet);
        pumpMessages(2);
        pair.ctrl->tickEditorTimerForTest();
    }
    const auto resumed = readDisplays(*pair.ctrl, ed.frame());
    std::printf("Reactivated on this thread, 30 blocks: DataExchange delivering %d, controller FIFO +%zu samples, "
                "spectrum fed +%zu samples\n",
                pair.ctrl->isDataExchangeDeliveringForTest() ? 1 : 0,
                pair.ctrl->getLocalOutputFIFO().totalWritten() - localBefore,
                resumed.spectrumSamples - resumedBefore.spectrumSamples);
    CHECK(resumed.spectrumSamples - resumedBefore.spectrumSamples >= 4096u);
#ifdef _WIN32
    CHECK(pair.ctrl->isDataExchangeDeliveringForTest());
    CHECK(pair.ctrl->getLocalOutputFIFO().totalWritten() - localBefore >= 4096u);
#endif
    ed.close();
}

#ifdef _WIN32
namespace {
struct NoopTimerCallback : ITimerCallback {
    void onTimer(Timer* /*timer*/) override {}
};
}  // namespace

// A deactivation on a thread other than the one that opened the DataExchange queue
// could not kill the SDK's fallback timer (a Windows thread timer is killed only by
// its own thread), so the timer kept firing into the plugin's code after the plugin
// was gone: a crash at unload. Measured as SDK thread timers still firing after
// a full teardown.
TEST_CASE("A deactivation on another thread leaves no DataExchange timer alive after teardown",
          "[disrumpo][data-exchange][display-recovery]")
{
    // The SDK's TIMERPROC, from a timer of our own; and SDK timers already left alive (earlier cases)
    FiredTimers fired;
    NoopTimerCallback callback;
    {
        auto probe = owned(Timer::create(&callback, 1));
        REQUIRE(probe != nullptr);
        pumpMessages(30, &fired);
        probe->stop();
    }
    REQUIRE_FALSE(fired.empty());
    const std::intptr_t sdkTimerProc = fired.begin()->second;
    FiredTimers before;
    pumpMessages(30, &before);

    {
        ConnectedPair pair;
        REQUIRE(pair.proc->setActive(static_cast<TBool>(true)) == kResultOk);  // queue + timer on this thread
        pair.process(kQuiet);
        pumpMessages(5);
        std::thread([&pair] { pair.proc->setActive(static_cast<TBool>(false)); }).join();
        REQUIRE(pair.proc->setActive(static_cast<TBool>(true)) == kResultOk);
        pair.process(kQuiet);
        pumpMessages(5);
        pair.teardown();
    }

    FiredTimers after;
    pumpMessages(30, &after);
    size_t leaked = 0;
    for (const auto& t : after) {
        if (t.second == sdkTimerProc && !before.contains(t)) ++leaked;
    }
    std::printf("SDK thread timers still firing after teardown: %zu (want 0)\n", leaked);
    CHECK(leaked == 0u);
}

// A host that toggles bypass (or reactivates for a latency change) by calling
// setActive(false) / setActive(true) off the main thread: the queue opened on the
// main thread was closed and reopened on that thread, whose fallback timer never
// fires there, so the 8 blocks filled and DataExchange stopped for good.
TEST_CASE("DataExchange keeps delivering when the host toggles activation on another thread",
          "[disrumpo][data-exchange][display-recovery]")
{
    ConnectedPair pair;
    REQUIRE(pair.proc->setActive(static_cast<TBool>(true)) == kResultOk);
    for (int i = 0; i < 10; ++i) {
        pair.process(kQuiet);
        pumpMessages(2);
    }
    REQUIRE(pair.ctrl->getLocalOutputFIFO().totalWritten() > 0u);

    std::thread([&pair] {
        pair.proc->setActive(static_cast<TBool>(false));
        pair.proc->setActive(static_cast<TBool>(true));
    }).join();

    const size_t before = pair.ctrl->getLocalOutputFIFO().totalWritten();
    for (int i = 0; i < 30; ++i) {
        pair.process(kQuiet);
        pumpMessages(2);
    }
    const size_t delivered = pair.ctrl->getLocalOutputFIFO().totalWritten() - before;
    std::printf("After setActive(false/true) on another thread, 30 blocks: DataExchange delivered %zu samples "
                "(want >= 4096)\n",
                delivered);
    CHECK(delivered >= 4096u);
}
#endif

// A host may terminate the controller while the editor is still open. The update
// banner polls the update checker every 500 ms and the editor's views stay alive
// until the editor closes, so the checker must too.
TEST_CASE("Terminating the controller before the editor closes leaves the update views safe",
          "[disrumpo][lifecycle][editor-uaf]")
{
    Disrumpo::Controller c;
    REQUIRE(c.initialize(nullptr) == kResultOk);
    {
        Disrumpo::TestSupport::HeadlessEditor ed(c);
        REQUIRE(ed.ready());
        REQUIRE(c.terminate() == kResultOk);
        pumpMessages(700);  // the banner's 500 ms poll fires (Windows)
        c.tickEditorTimerForTest();
        ed.close();
    }
}

// A host may destroy the processor while the editor is still open. In Tier 3 (no
// DataExchange) the controller reads the processor's SharedDisplay, found by its
// instance ID; the spectrum view's own 33 ms analysis timer and the editor timer
// then fire after the processor is gone. No view may hold a pointer into the
// processor: the spectrum view reads the controller's FIFOs only. Under ASan a
// view reading the freed processor is a heap-use-after-free; the timers fire on
// Windows (thread timers).
TEST_CASE("Destroying the processor with the editor open leaves no view reading its memory",
          "[disrumpo][lifecycle][editor-uaf]")
{
    constexpr int32 kBlock = 512;
    auto proc = std::make_unique<Disrumpo::Processor>();
    REQUIRE(proc->initialize(nullptr) == kResultOk);
    ProcessSetup setup{};
    setup.processMode = kRealtime;
    setup.symbolicSampleSize = kSample32;
    setup.sampleRate = 48000.0;
    setup.maxSamplesPerBlock = kBlock;
    REQUIRE(proc->setupProcessing(setup) == kResultOk);
    REQUIRE(proc->setActive(static_cast<TBool>(true)) == kResultOk);

    Disrumpo::Controller ctrl;
    REQUIRE(ctrl.initialize(nullptr) == kResultOk);
    {
        // The processor's state carries its instance ID: the controller's Tier-3 source
        auto state = Disrumpo::TestSupport::getStateBytes(*proc);
        auto* stream = new MemoryStream(state.data(), static_cast<TSize>(state.size()));
        REQUIRE(ctrl.setComponentState(stream) == kResultOk);
        stream->release();
    }

    std::vector<float> in(kBlock);
    for (int32 i = 0; i < kBlock; ++i)
        in[static_cast<size_t>(i)] = kLoud * static_cast<float>(std::sin(2.0 * std::numbers::pi * i / 48.0));
    std::vector<float> outL(kBlock);
    std::vector<float> outR(kBlock);
    const auto processBlock = [&] {
        std::vector<float> l = in;
        std::vector<float> r = in;
        float* inCh[2] = {l.data(), r.data()};
        float* outCh[2] = {outL.data(), outR.data()};
        AudioBusBuffers inBus{};
        inBus.numChannels = 2;
        inBus.channelBuffers32 = inCh;
        AudioBusBuffers outBus{};
        outBus.numChannels = 2;
        outBus.channelBuffers32 = outCh;
        ProcessData data{};
        data.processMode = kRealtime;
        data.symbolicSampleSize = kSample32;
        data.numSamples = kBlock;
        data.numInputs = 1;
        data.numOutputs = 1;
        data.inputs = &inBus;
        data.outputs = &outBus;
        proc->process(data);
    };

    {
        Disrumpo::TestSupport::HeadlessEditor ed(ctrl);
        REQUIRE(ed.ready());
        // No DataExchange: the 10th editor tick switches the displays to Tier 3
        for (int i = 0; i < 20; ++i) {
            processBlock();
            ctrl.tickEditorTimerForTest();
        }
        pumpMessages(80);  // the spectrum view's analysis timer reads its FIFOs (Windows)
        const auto* display = ctrl.spectrumDisplayForTest();
        REQUIRE(display != nullptr);
        const auto* viewFifo = display->outputFIFOForTest();
        const auto fed = viewFifo != nullptr ? viewFifo->totalWritten() : size_t{0};
        std::printf("Tier 3 before the processor is destroyed: spectrum analysis %s, the view reads the %s output "
                    "FIFO, %zu samples fed (want: active, controller's, > 0)\n",
                    display->isAnalysisActive() ? "active" : "inactive",
                    viewFifo == &ctrl.getLocalOutputFIFO() ? "controller's" : "PROCESSOR's (or none)", fed);
        REQUIRE(display->isAnalysisActive());
        CHECK(viewFifo == &ctrl.getLocalOutputFIFO());
        CHECK(fed > 0u);

        // The host destroys the processor; the editor stays open
        REQUIRE(proc->setActive(static_cast<TBool>(false)) == kResultOk);
        REQUIRE(proc->terminate() == kResultOk);
        proc.reset();
        pumpMessages(150);  // several of the spectrum view's 33 ms analysis ticks
        ctrl.tickEditorTimerForTest();
        pumpMessages(80);
        ed.close();
    }
    ctrl.terminate();
}

// =============================================================================
// Soak: the user's session. Editor open, a real audio thread processing a sine,
// presets loaded, states restored, bypass toggled and latency changes reactivated
// on the main thread and on the audio thread, all while audio runs.
// =============================================================================

namespace {

/// The host side of the controller: forwards edits to the audio thread, records kLatencyChanged.
class SoakComponentHandler : public IComponentHandler {
public:
    SoakComponentHandler() = default;
    virtual ~SoakComponentHandler() = default;
    SoakComponentHandler(const SoakComponentHandler&) = delete;
    SoakComponentHandler& operator=(const SoakComponentHandler&) = delete;
    SoakComponentHandler(SoakComponentHandler&&) = delete;
    SoakComponentHandler& operator=(SoakComponentHandler&&) = delete;

    tresult PLUGIN_API queryInterface(const TUID iid, void** obj) override
    {
        if (FUnknownPrivate::iidEqual(iid, IComponentHandler::iid) || FUnknownPrivate::iidEqual(iid, FUnknown::iid)) {
            *obj = static_cast<IComponentHandler*>(this);
            return kResultOk;
        }
        *obj = nullptr;
        return kNoInterface;
    }
    uint32 PLUGIN_API addRef() override { return 1; }
    uint32 PLUGIN_API release() override { return 1; }
    tresult PLUGIN_API beginEdit(ParamID /*id*/) override { return kResultOk; }
    tresult PLUGIN_API performEdit(ParamID id, ParamValue value) override
    {
        const std::scoped_lock lock(mutex_);
        edits_.emplace_back(id, value);
        return kResultOk;
    }
    tresult PLUGIN_API endEdit(ParamID /*id*/) override { return kResultOk; }
    tresult PLUGIN_API restartComponent(int32 flags) override
    {
        if ((flags & kLatencyChanged) != 0) latencyChanged.store(true, std::memory_order_release);
        return kResultOk;
    }

    std::vector<std::pair<ParamID, ParamValue>> takeEdits()
    {
        const std::scoped_lock lock(mutex_);
        return std::exchange(edits_, {});
    }

    std::atomic<bool> latencyChanged{false};

private:
    std::mutex mutex_;
    std::vector<std::pair<ParamID, ParamValue>> edits_;
};

/// The host's audio thread: process() back to back with a sine and the controller's
/// edits, the processor's output parameters kept for the main thread.
class SoakAudioThread {
public:
    SoakAudioThread(ConnectedPair& pair, SoakComponentHandler& handler)
        : pair_(pair), handler_(handler), thread_([this] { run(); })
    {
    }
    ~SoakAudioThread() { stop(); }
    SoakAudioThread(const SoakAudioThread&) = delete;
    SoakAudioThread& operator=(const SoakAudioThread&) = delete;
    SoakAudioThread(SoakAudioThread&&) = delete;
    SoakAudioThread& operator=(SoakAudioThread&&) = delete;

    void stop()
    {
        if (!thread_.joinable()) return;
        stop_.store(true, std::memory_order_release);
        thread_.join();
    }
    void waitBlocks(size_t n) const
    {
        const size_t target = blocks_.load(std::memory_order_acquire) + n;
        while (blocks_.load(std::memory_order_acquire) < target) std::this_thread::yield();
    }
    /// The host stops calling process(); returns once the audio thread is between blocks.
    void pause()
    {
        pauseRequested_.store(true, std::memory_order_release);
        while (!paused_.load(std::memory_order_acquire)) std::this_thread::yield();
    }
    void resume()
    {
        pauseRequested_.store(false, std::memory_order_release);
        while (paused_.load(std::memory_order_acquire)) std::this_thread::yield();
    }
    /// The host reactivates the processor on its audio thread, between two blocks.
    void reactivateHere()
    {
        reactivate_.store(true, std::memory_order_release);
        while (reactivate_.load(std::memory_order_acquire)) std::this_thread::yield();
    }
    std::vector<std::pair<ParamID, ParamValue>> takeOutputs()
    {
        const std::scoped_lock lock(outputMutex_);
        return std::exchange(outputs_, {});
    }
    [[nodiscard]] size_t nonFinite() const { return nonFinite_; }  // after stop()

private:
    void run()
    {
        Krate::Test::ParameterChanges in;
        Disrumpo::TestSupport::CaptureChanges out;
        while (!stop_.load(std::memory_order_acquire)) {
            if (pauseRequested_.load(std::memory_order_acquire)) {
                paused_.store(true, std::memory_order_release);
                while (pauseRequested_.load(std::memory_order_acquire) && !stop_.load(std::memory_order_acquire))
                    std::this_thread::yield();
                paused_.store(false, std::memory_order_release);
                continue;
            }
            if (reactivate_.load(std::memory_order_acquire)) {
                pair_.proc->setActive(static_cast<TBool>(false));
                pair_.proc->setActive(static_cast<TBool>(true));
                reactivate_.store(false, std::memory_order_release);
            }
            in.clear();
            for (const auto& e : handler_.takeEdits()) in.addChange(e.first, e.second);
            out.clear();
            pair_.process(kQuiet, &in, &out);
            double latency = 0.0;
            if (out.last(Disrumpo::kLatencyOutputId, latency)) {
                const std::scoped_lock lock(outputMutex_);
                outputs_.emplace_back(Disrumpo::kLatencyOutputId, latency);
            }
            for (size_t i = 0; i < pair_.outL.size(); ++i) {
                if (!Disrumpo::TestSupport::fin(pair_.outL[i]) || !Disrumpo::TestSupport::fin(pair_.outR[i]))
                    ++nonFinite_;
            }
            blocks_.fetch_add(1, std::memory_order_release);
        }
    }

    ConnectedPair& pair_;
    SoakComponentHandler& handler_;
    std::atomic<bool> stop_{false};
    std::atomic<bool> pauseRequested_{false};
    std::atomic<bool> paused_{false};
    std::atomic<bool> reactivate_{false};
    std::atomic<size_t> blocks_{0};
    std::mutex outputMutex_;
    std::vector<std::pair<ParamID, ParamValue>> outputs_;
    size_t nonFinite_ = 0;  // audio thread only until stop()
    std::thread thread_;    // last: started once every member above exists
};

/// Component chunk ("Comp") of a .vstpreset file.
std::vector<uint8_t> presetComponentState(const std::filesystem::path& path)
{
    std::ifstream f(path, std::ios::binary);
    const std::vector<uint8_t> file((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    const auto readI64 = [&file](size_t at) {
        int64_t v = 0;
        std::memcpy(&v, file.data() + at, sizeof(v));
        return v;
    };
    if (file.size() < 48 || std::memcmp(file.data(), "VST3", 4) != 0) return {};
    const auto list = static_cast<size_t>(readI64(40));
    if (list + 8 > file.size() || std::memcmp(file.data() + list, "List", 4) != 0) return {};
    uint32_t entries = 0;
    std::memcpy(&entries, file.data() + list + 4, sizeof(entries));
    for (uint32_t i = 0; i < entries; ++i) {
        const size_t e = list + 8 + static_cast<size_t>(i) * 20;
        if (e + 20 <= file.size() && std::memcmp(file.data() + e, "Comp", 4) == 0) {
            const auto off = static_cast<size_t>(readI64(e + 4));
            const auto size = static_cast<size_t>(readI64(e + 12));
            if (off + size > file.size()) return {};
            return {file.begin() + static_cast<std::ptrdiff_t>(off),
                    file.begin() + static_cast<std::ptrdiff_t>(off + size)};
        }
    }
    return {};
}

struct FactoryPreset {
    Krate::Plugins::PresetInfo info;
    std::vector<uint8_t> component;
};

std::vector<FactoryPreset> factoryPresets()
{
    std::vector<FactoryPreset> out;
    for (const auto& e : std::filesystem::recursive_directory_iterator(DISRUMPO_RESOURCES_DIR "/presets")) {
        if (e.path().extension() != ".vstpreset") continue;
        FactoryPreset p;
        p.info.name = e.path().stem().string();
        p.info.subcategory = e.path().parent_path().filename().string();
        p.info.path = e.path();
        p.info.isFactory = true;
        p.component = presetComponentState(e.path());
        out.push_back(std::move(p));
    }
    std::sort(out.begin(), out.end(), [](const auto& a, const auto& b) { return a.info.path < b.info.path; });
    return out;
}

enum class SoakTeardown : std::uint8_t { EditorFirst, ControllerFirst };

void runSoak(int iterations, SoakTeardown teardown)
{
    const auto presets = factoryPresets();
    REQUIRE(presets.size() > 100u);
    for (const auto& p : presets) REQUIRE_FALSE(p.component.empty());

    SoakComponentHandler handler;  // outlives the controller's reference
    ConnectedPair pair;
    REQUIRE(pair.ctrl->setComponentHandler(&handler) == kResultOk);
    REQUIRE(pair.proc->setActive(static_cast<TBool>(true)) == kResultOk);
    auto editor = std::make_unique<Disrumpo::TestSupport::HeadlessEditor>(*pair.ctrl);
    REQUIRE(editor->ready());

    std::mt19937 rng(20261111u);
    std::array<int, 4> ops{};
    int failedLoads = 0;
    int reactivatedOnMain = 0;
    int reactivatedOnAudio = 0;
    size_t lastSpectrum = 0;
    size_t lastWindows = 0;
    int spectrumGap = 0;
    int meterGap = 0;
    int worstSpectrumGap = 0;
    int worstMeterGap = 0;
    {
        SoakAudioThread audio(pair, handler);
        const auto hostReactivateOnMain = [&] {
            audio.pause();
            pair.proc->setActive(static_cast<TBool>(false));
            pair.proc->setActive(static_cast<TBool>(true));
            audio.resume();
        };
        for (int it = 0; it < iterations; ++it) {
            const auto op = static_cast<size_t>(rng() % 4u);
            ++ops[op];
            const auto& preset = presets[rng() % presets.size()];
            switch (op) {
                case 0:  // the preset browser's load path, while audio runs
                    if (!pair.ctrl->getPresetManager()->loadPreset(preset.info)) ++failedLoads;
                    break;
                case 1: {  // a host state restore overlapping a block
                    std::vector<uint8_t> bytes = preset.component;
                    auto* s = new MemoryStream(bytes.data(), static_cast<TSize>(bytes.size()));
                    if (pair.proc->setState(s) != kResultOk) ++failedLoads;
                    s->seek(0, IBStream::kIBSeekSet, nullptr);
                    if (pair.ctrl->setComponentState(s) != kResultOk) ++failedLoads;
                    s->release();
                    break;
                }
                case 2:  // host bypass: stop processing, deactivate, activate, resume
                    hostReactivateOnMain();
                    break;
                default:  // audio only
                    break;
            }
            audio.waitBlocks(3);  // one spectrum send (1440 samples) at least
            for (const auto& [id, value] : audio.takeOutputs()) pair.ctrl->setParamNormalized(id, value);
            if (handler.latencyChanged.exchange(false, std::memory_order_acq_rel)) {
                if (rng() % 2u == 0u) {
                    hostReactivateOnMain();
                    ++reactivatedOnMain;
                } else {
                    audio.reactivateHere();
                    ++reactivatedOnAudio;
                }
                audio.waitBlocks(3);
            }
            pumpMessages(1);
            pair.ctrl->tickEditorTimerForTest();

            const auto* display = pair.ctrl->spectrumDisplayForTest();
            const auto* fifo = display != nullptr ? display->outputFIFOForTest() : nullptr;
            const size_t spectrum = fifo != nullptr ? fifo->totalWritten() : 0;
            std::vector<Disrumpo::LevelMeterView*> meters;
            collectLevelMeters(editor->frame(), meters);
            size_t windows = 0;
            for (const auto* m : meters)
                if (m->source() == Disrumpo::LevelMeterView::Source::Input) windows = m->windowsReceivedForTest();
            spectrumGap = spectrum != lastSpectrum ? 0 : spectrumGap + 1;
            meterGap = windows != lastWindows ? 0 : meterGap + 1;
            lastSpectrum = spectrum;
            lastWindows = windows;
            worstSpectrumGap = std::max(worstSpectrumGap, spectrumGap);
            worstMeterGap = std::max(worstMeterGap, meterGap);
        }
        audio.stop();
        std::printf("Soak %d iterations (%s first): %d preset browser loads, %d host state restores, %d bypass "
                    "toggles, %d audio-only; latency reactivations %d on main, %d on the audio thread; %d failed "
                    "loads; %zu non-finite samples; longest run of editor ticks without a new spectrum %d, "
                    "without a new meter window %d (want <= 9: every 10th tick at the latest)\n",
                    iterations, teardown == SoakTeardown::EditorFirst ? "editor" : "controller", ops[0], ops[1],
                    ops[2], ops[3], reactivatedOnMain, reactivatedOnAudio, failedLoads, audio.nonFinite(),
                    worstSpectrumGap, worstMeterGap);
        CHECK(failedLoads == 0);
        CHECK(audio.nonFinite() == 0u);
        CHECK(worstSpectrumGap <= 9);
        CHECK(worstMeterGap <= 9);
    }

    if (teardown == SoakTeardown::EditorFirst) {
        editor.reset();  // closes it
        pair.teardown();
    } else {
        pair.proc->setActive(static_cast<TBool>(false));
        pair.proc->disconnect(pair.ctrlConn());
        pair.ctrl->disconnect(pair.procConn());
        pair.ctrl->terminate();
        pumpMessages(600);  // the open editor's timers fire on a terminated controller
        pair.ctrl->tickEditorTimerForTest();
        editor.reset();
        pair.proc->terminate();
        pair.connected = false;
    }
}

}  // namespace

TEST_CASE("Soak: preset loads and state restores during bypass and latency reactivations with the editor open",
          "[disrumpo][data-exchange][display-recovery][soak]")
{
    SECTION("editor closes, then both terminate") { runSoak(120, SoakTeardown::EditorFirst); }
    SECTION("the controller terminates before the editor closes") { runSoak(120, SoakTeardown::ControllerFirst); }
}

TEST_CASE("Soak (long): preset loads and state restores during bypass and latency reactivations with the editor open",
          "[disrumpo][data-exchange][display-recovery][soak][long]")
{
    runSoak(1500, SoakTeardown::EditorFirst);
}
