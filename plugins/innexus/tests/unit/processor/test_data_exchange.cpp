// ==============================================================================
// DataExchange Processor Tests
// ==============================================================================
// Verifies the DataExchangeHandler integration on the Processor side:
//   - DisplayData is trivially copyable (safe for block transport)
//   - Processor lifecycle without connect() (sendDisplayData is no-op)
//   - connect/disconnect lifecycle
//   - setActive after connect
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "controller/display_data.h"
#include "controller/controller.h"
#include "processor/processor.h"
#include "plugin_ids.h"

#include "public.sdk/source/vst/hosting/hostclasses.h"

#include <type_traits>

using namespace Steinberg;
using namespace Steinberg::Vst;

// ==============================================================================
// DisplayData trivially copyable (compile-time safety for block transport)
// ==============================================================================

TEST_CASE("DisplayData is trivially copyable",
          "[innexus][data-exchange][processor]")
{
    STATIC_REQUIRE(std::is_trivially_copyable_v<Innexus::DisplayData>);
}

// ==============================================================================
// Processor lifecycle without connect (existing tests compatibility)
// ==============================================================================

TEST_CASE("Processor lifecycle without connect - sendDisplayData is no-op",
          "[innexus][data-exchange][processor]")
{
    Innexus::Processor proc;

    // Initialize with minimal host context
    HostApplication host;
    REQUIRE(proc.initialize(&host) == kResultOk);

    // Setup processing
    ProcessSetup setup{};
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 512;
    setup.symbolicSampleSize = kSample32;
    setup.processMode = kRealtime;
    REQUIRE(proc.setupProcessing(setup) == kResultOk);

    // Activate without connect - should work fine
    REQUIRE(proc.setActive(true) == kResultOk);

    // sendDisplayData should be a no-op (no crash, no message sent)
    ProcessData data{};
    data.numSamples = 0;
    proc.sendDisplayData(data);

    // Deactivate and terminate
    REQUIRE(proc.setActive(false) == kResultOk);
    REQUIRE(proc.terminate() == kResultOk);
}

// ==============================================================================
// Processor connect/disconnect lifecycle
// ==============================================================================

TEST_CASE("Processor connect/disconnect lifecycle",
          "[innexus][data-exchange][processor]")
{
    Innexus::Processor proc;
    Innexus::Controller ctrl;

    HostApplication host;
    REQUIRE(proc.initialize(&host) == kResultOk);
    REQUIRE(ctrl.initialize(&host) == kResultOk);

    // Get IConnectionPoint from controller
    auto* ctrlConn = static_cast<IConnectionPoint*>(&ctrl);

    REQUIRE(proc.connect(ctrlConn) == kResultOk);
    REQUIRE(proc.disconnect(ctrlConn) == kResultOk);

    REQUIRE(ctrl.terminate() == kResultOk);
    REQUIRE(proc.terminate() == kResultOk);
}

// ==============================================================================
// setActive after connect (full lifecycle)
// ==============================================================================

TEST_CASE("Processor setActive after connect - full lifecycle",
          "[innexus][data-exchange][processor]")
{
    Innexus::Processor proc;
    Innexus::Controller ctrl;

    HostApplication host;
    REQUIRE(proc.initialize(&host) == kResultOk);
    REQUIRE(ctrl.initialize(&host) == kResultOk);

    // Connect
    auto* ctrlConn = static_cast<IConnectionPoint*>(&ctrl);
    REQUIRE(proc.connect(ctrlConn) == kResultOk);

    // Setup + activate
    ProcessSetup setup{};
    setup.sampleRate = 44100.0;
    setup.maxSamplesPerBlock = 512;
    setup.symbolicSampleSize = kSample32;
    setup.processMode = kRealtime;
    REQUIRE(proc.setupProcessing(setup) == kResultOk);
    REQUIRE(proc.setActive(true) == kResultOk);

    // Deactivate
    REQUIRE(proc.setActive(false) == kResultOk);

    // Disconnect
    REQUIRE(proc.disconnect(ctrlConn) == kResultOk);

    REQUIRE(ctrl.terminate() == kResultOk);
    REQUIRE(proc.terminate() == kResultOk);
}
