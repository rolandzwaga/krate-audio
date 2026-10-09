// ==============================================================================
// Vorago Phase 14 - RT-safe preset loading; FR-039, FR-040, SC-016 (T032)
// ==============================================================================
// The streams under test are every factory definition regenerated through THE
// drive (VoragoTest::buildPresetComponentState, vorago_preset_host.h) plus one
// default-surface stream (a fresh processor's getState()), so neither loop is
// ever empty - allPresets() is empty until the library is authored (T037).
//
// Vorago_FactoryPresets_SequentialLoadNoAlloc (FR-039): setState() runs OUTSIDE
// any scope (its stream decode is the message thread's business); an
// AllocationScope wraps each of the next 4 process() calls, and the count is read
// from the live atomic as the last statement inside the scope (the
// automation_rt_test.cpp pattern). Everything the processor defers to the next
// process() - the pushAllSurfaces(PresetLoad) re-push and the latch release
// requested by setState() - therefore runs inside the scope.
//
// Vorago_FactoryPresets_ConcurrentLoadIsRtSafe (FR-040): a message thread (this
// one) loops setState() over the prebuilt streams while an audio thread renders
// 4 s under TestHelpers::ThreadScopedAllocationScope. The global AllocationScope
// is FORBIDDEN here: it would count the message thread's MemoryStream / decode
// allocations against the audio thread. The audio thread drives a Catch2-free
// PresetHost (never ProcessorFixture, whose processBlock REQUIREs), executes no
// Catch2 macro and allocates nothing of its own; every observation is a plain
// variable asserted after join() (model: Seraphis
// preset_render_sweep_test.cpp, Seraphis_PresetSweep_ConcurrentLoadIsRtSafe).
// Finiteness is by bit pattern (FR-062); this TU carries -fno-fast-math.
// ==============================================================================

#include "plugin_ids.h"
#include "vorago_preset_defs.h"
#include "vorago_preset_host.h"
#include "vorago_test_fixture.h"

#include <allocation_detector.h>
#include <enable_ftz_daz.h>
#include <vst_event_list.h>

#include <krate/dsp/core/db_utils.h>

#include "public.sdk/source/common/memorystream.h"

#include <pluginterfaces/base/ibstream.h>

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <exception>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr double kSampleRate = 48000.0;
constexpr std::size_t kBlock = 512;
constexpr std::size_t kWarmBlocks = 20;
constexpr std::size_t kBlocksPerLoad = 4;
constexpr double kConcurrentSeconds = 4.0;
constexpr Steinberg::int16 kNote = 36;
constexpr float kVelocity = 100.0f / 127.0f;
constexpr float kOutputCeiling = 0.9661f;  // 10^(-0.3/20) = 0.96605, vorago_engine.h:203

struct NamedStream {
    std::string name;
    std::vector<std::uint8_t> comp;
};

// Every factory definition through THE drive, then the default surface (a fresh
// processor's getState()). Built on the test thread only.
std::vector<NamedStream> buildStreams() {
    std::vector<NamedStream> out;
    for (const Vorago::PresetDefs::VoragoPresetDef& def : Vorago::PresetDefs::allPresets()) {
        NamedStream s;
        s.name = std::string(def.name);
        std::string why;
        const bool built = VoragoTest::buildPresetComponentState(def, s.comp, why);
        INFO("preset " << s.name << ": buildPresetComponentState: " << why);
        REQUIRE(built);
        REQUIRE_FALSE(s.comp.empty());
        out.push_back(std::move(s));
    }

    NamedStream surface;
    surface.name = "<default surface>";
    VoragoTest::PresetHost host;
    REQUIRE(host.prepare(kSampleRate, static_cast<Steinberg::int32>(kBlock)) ==
            Steinberg::kResultOk);
    REQUIRE(host.saveState(surface.comp));
    REQUIRE_FALSE(surface.comp.empty());
    out.push_back(std::move(surface));
    return out;
}

}  // namespace

// FR-039 / SC-016: sequential loads between process() calls of a warm processor.
TEST_CASE("Vorago_FactoryPresets_SequentialLoadNoAlloc", "[vorago][preset]") {
    std::vector<NamedStream> streams = buildStreams();
    REQUIRE_FALSE(streams.empty());  // the default surface is always present

    VoragoTest::ProcessorFixture fx;
    fx.prepare(kSampleRate, static_cast<Steinberg::int32>(kBlock));
    fx.reserveCapture(kBlock);  // cleared (capacity kept) before every block

    // Warm: NoteOn 36 held, 20 blocks.
    Krate::Test::EventList noteOn;
    noteOn.addNoteOn(kNote, kVelocity, 0);
    for (std::size_t b = 0; b < kWarmBlocks; ++b) {
        fx.capturedL.clear();
        fx.capturedR.clear();
        REQUIRE(fx.processBlock(kBlock, (b == 0) ? &noteOn : nullptr) == Steinberg::kResultOk);
    }

    std::size_t totalAllocs = 0;
    std::string firstAllocating;
    bool finite = true;
    float peak = 0.0f;

    for (NamedStream& s : streams) {
        INFO("stream: " << s.name);

        // ---- message side, outside any scope ----
        Steinberg::MemoryStream stream(s.comp.data(),
                                       static_cast<Steinberg::TSize>(s.comp.size()));
        REQUIRE(fx.proc->setState(&stream) == Steinberg::kResultOk);

        // ---- audio side: one scope per process() call ----
        for (std::size_t b = 0; b < kBlocksPerLoad; ++b) {
            fx.capturedL.clear();  // keeps capacity
            fx.capturedR.clear();

            Steinberg::tresult r = Steinberg::kResultFalse;
            std::size_t blockAllocs = 0;
            {
                TestHelpers::AllocationScope scope;
                r = fx.processBlock(kBlock);
                blockAllocs = TestHelpers::AllocationDetector::instance().getAllocationCount();
            }
            REQUIRE(r == Steinberg::kResultOk);
            if (blockAllocs > 0 && firstAllocating.empty()) {
                firstAllocating = s.name + " block " + std::to_string(b);
            }
            totalAllocs += blockAllocs;

            const std::span<const float> l(fx.capturedL);
            const std::span<const float> rr(fx.capturedR);
            finite = finite && VoragoTest::allFinite(l) && VoragoTest::allFinite(rr);
            peak = std::max({peak, VoragoTest::peakOf(l), VoragoTest::peakOf(rr)});
        }
    }

    INFO("streams=" << streams.size() << " allocations=" << totalAllocs
                    << " first allocating=" << firstAllocating << " peak=" << peak);
    REQUIRE(totalAllocs == 0u);
    REQUIRE(finite);
    REQUIRE(peak <= kOutputCeiling);
}

// FR-040 / SC-016: setState() looped on a message thread while the audio thread
// renders. ThreadScopedAllocationScope only - the global AllocationScope is
// forbidden for this arm.
TEST_CASE("Vorago_FactoryPresets_ConcurrentLoadIsRtSafe", "[vorago][preset]") {
    const std::vector<NamedStream> streams = buildStreams();
    REQUIRE_FALSE(streams.empty());

    VoragoTest::PresetHost host;
    REQUIRE(host.prepare(kSampleRate, static_cast<Steinberg::int32>(kBlock)) ==
            Steinberg::kResultOk);

    // First stream loaded and the note started HERE, before any concurrency, so
    // the audio thread's first measured block already renders a sounding voice.
    REQUIRE(host.loadState(streams.front().comp) == Steinberg::kResultOk);
    {
        Krate::Test::EventList noteOn;
        noteOn.addNoteOn(kNote, kVelocity, 0);
        REQUIRE(host.process(kBlock, &noteOn, nullptr) == Steinberg::kResultOk);
    }

    const std::size_t concurrentBlocks =
        static_cast<std::size_t>(std::llround(kConcurrentSeconds * kSampleRate)) / kBlock;

    std::atomic<bool> audioReady{false};
    std::atomic<bool> audioFinished{false};

    // Written on the audio thread, read after join() (the synchronisation point).
    std::size_t blocksOk = 0;
    std::size_t blocksRendered = 0;
    std::size_t nonFiniteSamples = 0;
    std::size_t allocations = 0;
    std::size_t probe = 0;
    float peak = 0.0f;
    std::array<char, 256> audioThreadError{};  // fixed buffer: snprintf cannot throw

    const auto audioBody = [&] {
        // FIRST TOUCH of the TLS opt-in before the scope opens
        // (allocation_detector.h:41), and FTZ/DAZ: MXCSR is per thread, so main()'s
        // setting (unit/test_main.cpp:27) does not carry over.
        TestHelpers::tAllocationTrackThisThread = true;
        enableFTZDAZ();
        // One unmeasured warm-up block on THIS thread.
        static_cast<void>(host.process(kBlock, nullptr, nullptr));

        {
            TestHelpers::ThreadScopedAllocationScope scope;
            audioReady.store(true, std::memory_order_release);

            for (std::size_t b = 0; b < concurrentBlocks; ++b) {
                if (host.process(kBlock, nullptr, nullptr) == Steinberg::kResultOk) {
                    ++blocksOk;
                }
                ++blocksRendered;
                const std::span<const float> l = host.outL();
                const std::span<const float> r = host.outR();
                for (std::size_t i = 0; i < l.size(); ++i) {
                    const float a = l[i];
                    const float c = r[i];
                    if (!Krate::DSP::detail::isFinite(a) || !Krate::DSP::detail::isFinite(c)) {
                        ++nonFiniteSamples;
                        continue;  // a NaN is not a peak
                    }
                    peak = std::max({peak, std::fabs(a), std::fabs(c)});
                }
            }

            allocations = TestHelpers::AllocationDetector::instance().getAllocationCount();
        }

        // Liveness probe in its OWN scope: the filter admits this thread, so the
        // zero above is not vacuous.
        {
            TestHelpers::ThreadScopedAllocationScope probeScope;
            int* volatile deliberate = new int(11);
            probe = TestHelpers::AllocationDetector::instance().getAllocationCount();
            delete deliberate;
        }
    };

    std::thread audioThread([&] {
        try {
            audioBody();
        } catch (const std::exception& e) {
            std::snprintf(audioThreadError.data(), audioThreadError.size(), "%s", e.what());
        } catch (...) {
            std::snprintf(audioThreadError.data(), audioThreadError.size(), "%s",
                          "unknown (non-std::exception) throw");
        }
        // Unconditional: a throw must never leave the message loop spinning.
        audioReady.store(true, std::memory_order_release);
        audioFinished.store(true, std::memory_order_release);
    });

    // ---- the MESSAGE thread is this one; no Catch2 macro until after join ----
    while (!audioReady.load(std::memory_order_acquire)) {
        std::this_thread::yield();
    }

    // Loops until the audio thread stops, and always completes at least one full
    // pass so every stream is loaded concurrently at least once.
    std::size_t loads = 0;
    std::size_t failedLoads = 0;
    std::size_t firstFailed = streams.size();
    while (!audioFinished.load(std::memory_order_acquire) || loads < streams.size()) {
        const std::size_t i = loads % streams.size();
        if (host.loadState(streams[i].comp) != Steinberg::kResultOk) {
            ++failedLoads;
            if (firstFailed == streams.size()) {
                firstFailed = i;
            }
        }
        ++loads;
        std::this_thread::yield();
    }

    audioThread.join();

    INFO("audio thread threw: " << audioThreadError.data());
    REQUIRE(audioThreadError[0] == '\0');

    const std::string firstFailedName =
        (firstFailed < streams.size()) ? streams[firstFailed].name : std::string("none");
    INFO("streams=" << streams.size() << " loads=" << loads << " failed=" << failedLoads
                    << " first failed=" << firstFailedName << " blocks=" << blocksRendered
                    << "/" << concurrentBlocks << " allocations=" << allocations
                    << " probe=" << probe << " nonFinite=" << nonFiniteSamples
                    << " peak=" << peak);
    REQUIRE(blocksRendered == concurrentBlocks);
    REQUIRE(blocksOk == concurrentBlocks);
    REQUIRE(loads >= streams.size());
    REQUIRE(failedLoads == 0u);  // every setState() returns kResultOk
    REQUIRE(probe >= 1u);        // the filter admits the audio thread
    REQUIRE(allocations == 0u);  // SC-016
    REQUIRE(nonFiniteSamples == 0u);
    REQUIRE(peak <= kOutputCeiling);
}
