// ==============================================================================
// DataExchange Pipeline Integration Tests
// ==============================================================================
// Verifies the full round-trip: Processor → DataExchangeHandler → Controller
// using the IMessage fallback path (no host IDataExchangeHandler available).
//
// The DataExchangeHandler automatically falls back to IMessage when the host
// doesn't provide IDataExchangeHandler. The DataExchangeReceiverHandler on the
// controller side transparently handles these fallback messages.
//
// NOTE: The fallback path uses a timer (1ms) to send queued blocks. On Windows,
// this requires message dispatching. We pump messages in the test to allow delivery.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "controller/controller.h"
#include "controller/display_data.h"
#include "processor/processor.h"
#include "plugin_ids.h"

#include "public.sdk/source/vst/hosting/hostclasses.h"

#include <cstring>
#include <chrono>
#include <thread>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace Steinberg;
using namespace Steinberg::Vst;
using Catch::Approx;

/// Pump platform messages to allow SDK timers to fire.
static void pumpMessages(int durationMs)
{
#ifdef _WIN32
    auto start = std::chrono::steady_clock::now();
    while (std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now() - start).count() < durationMs)
    {
        MSG msg;
        while (PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE))
        {
            TranslateMessage(&msg);
            DispatchMessageW(&msg);
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
#else
    std::this_thread::sleep_for(std::chrono::milliseconds(durationMs));
#endif
}

// ==============================================================================
// Full round-trip in fallback mode
// ==============================================================================

TEST_CASE("DataExchange full round-trip via IMessage fallback",
          "[innexus][data-exchange][integration]")
{
    Innexus::Processor proc;
    Innexus::Controller ctrl;

    HostApplication host;

    // Initialize both
    REQUIRE(proc.initialize(&host) == kResultOk);
    REQUIRE(ctrl.initialize(&host) == kResultOk);

    // Wire IConnectionPoint peers so IMessages are delivered
    auto* procConn = static_cast<IConnectionPoint*>(
        static_cast<AudioEffect*>(&proc));
    auto* ctrlConn = static_cast<IConnectionPoint*>(
        static_cast<EditControllerEx1*>(&ctrl));

    // Connect both directions
    REQUIRE(proc.connect(ctrlConn) == kResultOk);
    REQUIRE(ctrl.connect(procConn) == kResultOk);

    // Setup processing
    ProcessSetup setup{};
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 512;
    setup.symbolicSampleSize = kSample32;
    setup.processMode = kRealtime;
    REQUIRE(proc.setupProcessing(setup) == kResultOk);

    // Activate — this opens the DataExchange queue (or fallback timer)
    REQUIRE(proc.setActive(true) == kResultOk);

    // Call sendDisplayData — pushes block into the fallback message buffer
    ProcessData data{};
    data.numSamples = 0;
    proc.sendDisplayData(data);

    // The fallback MessageHandler sends via a 1ms timer.
    // Pump platform messages to allow the timer callback to fire.
    pumpMessages(50);

    // Check that controller received the display data
    const auto& cached = ctrl.getCachedDisplayData();
    // frameCounter should have been incremented from 0 to 1
    REQUIRE(cached.frameCounter >= 1u);
    // activePartialCount should be set (processor default is 48)
    REQUIRE(cached.activePartialCount == 48);

    // Deactivate and clean up
    REQUIRE(proc.setActive(false) == kResultOk);
    REQUIRE(proc.disconnect(ctrlConn) == kResultOk);
    REQUIRE(ctrl.disconnect(procConn) == kResultOk);
    REQUIRE(ctrl.terminate() == kResultOk);
    REQUIRE(proc.terminate() == kResultOk);
}
