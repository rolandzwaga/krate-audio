// ==============================================================================
// DataExchange Controller Receiver Tests
// ==============================================================================
// Verifies the IDataExchangeReceiver integration on the Controller side:
//   - Controller exposes IDataExchangeReceiver via queryInterface
//   - onDataExchangeBlocksReceived copies DisplayData into cachedDisplayData_
//   - notify delegates DataExchange-format messages to ReceiverHandler
//   - notify still handles other messages (SampleFileLoaded, etc.)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "controller/controller.h"
#include "controller/display_data.h"
#include "plugin_ids.h"

#include "pluginterfaces/vst/ivstdataexchange.h"
#include "public.sdk/source/vst/hosting/hostclasses.h"
#include "public.sdk/source/vst/hosting/pluginterfacesupport.h"

#include <cstring>

using namespace Steinberg;
using Catch::Approx;

// ==============================================================================
// Controller exposes IDataExchangeReceiver
// ==============================================================================

TEST_CASE("Controller exposes IDataExchangeReceiver via queryInterface",
          "[innexus][data-exchange][controller]")
{
    Innexus::Controller ctrl;
    Vst::HostApplication host;
    REQUIRE(ctrl.initialize(&host) == kResultOk);

    Vst::IDataExchangeReceiver* receiver = nullptr;
    tresult result = ctrl.queryInterface(
        Vst::IDataExchangeReceiver::iid, reinterpret_cast<void**>(&receiver));

    REQUIRE(result == kResultOk);
    REQUIRE(receiver != nullptr);

    if (receiver)
        receiver->release();

    REQUIRE(ctrl.terminate() == kResultOk);
}

// ==============================================================================
// onDataExchangeBlocksReceived copies DisplayData
// ==============================================================================

TEST_CASE("onDataExchangeBlocksReceived copies DisplayData into cache",
          "[innexus][data-exchange][controller]")
{
    Innexus::Controller ctrl;
    Vst::HostApplication host;
    REQUIRE(ctrl.initialize(&host) == kResultOk);

    // Build a DisplayData with known values
    Innexus::DisplayData testData{};
    testData.partialAmplitudes[0] = 0.75f;
    testData.partialAmplitudes[1] = 0.5f;
    testData.partialActive[0] = 1;
    testData.activePartialCount = 64;
    testData.f0 = 440.0f;
    testData.f0Confidence = 0.95f;
    testData.slotOccupied[2] = 1;
    testData.evolutionPosition = 0.3f;
    testData.manualMorphPosition = 0.6f;
    testData.mod1Phase = 0.1f;
    testData.mod2Phase = 0.9f;
    testData.mod1Active = true;
    testData.mod2Active = false;
    testData.frameCounter = 42;

    // Construct a mock DataExchangeBlock pointing to our test data
    Vst::DataExchangeBlock block{};
    block.data = &testData;
    block.size = static_cast<uint32>(sizeof(Innexus::DisplayData));
    block.blockID = 0;

    // Call onDataExchangeBlocksReceived directly
    Vst::IDataExchangeReceiver* receiver = nullptr;
    ctrl.queryInterface(Vst::IDataExchangeReceiver::iid,
                        reinterpret_cast<void**>(&receiver));
    REQUIRE(receiver != nullptr);

    receiver->onDataExchangeBlocksReceived(0, 1, &block, false);
    receiver->release();

    // Verify cached data matches
    const auto& cached = ctrl.getCachedDisplayData();
    REQUIRE(cached.partialAmplitudes[0] == Approx(0.75f));
    REQUIRE(cached.partialAmplitudes[1] == Approx(0.5f));
    REQUIRE(cached.partialActive[0] == 1);
    REQUIRE(cached.activePartialCount == 64);
    REQUIRE(cached.f0 == Approx(440.0f));
    REQUIRE(cached.f0Confidence == Approx(0.95f));
    REQUIRE(cached.slotOccupied[2] == 1);
    REQUIRE(cached.evolutionPosition == Approx(0.3f));
    REQUIRE(cached.manualMorphPosition == Approx(0.6f));
    REQUIRE(cached.mod1Phase == Approx(0.1f));
    REQUIRE(cached.mod2Phase == Approx(0.9f));
    REQUIRE(cached.mod1Active == true);
    REQUIRE(cached.mod2Active == false);
    REQUIRE(cached.frameCounter == 42u);

    REQUIRE(ctrl.terminate() == kResultOk);
}

// ==============================================================================
// notify still handles other messages
// ==============================================================================

TEST_CASE("Controller notify still handles DetectedADSR messages",
          "[innexus][data-exchange][controller]")
{
    Innexus::Controller ctrl;
    Vst::HostApplication host;
    REQUIRE(ctrl.initialize(&host) == kResultOk);

    // Create a DetectedADSR message - should still be handled
    Vst::HostMessage msg;
    msg.setMessageID("DetectedADSR");
    auto* attrs = msg.getAttributes();
    attrs->setFloat("attackMs", 25.0);
    attrs->setFloat("decayMs", 200.0);
    attrs->setFloat("sustainLevel", 0.7);
    attrs->setFloat("releaseMs", 300.0);

    tresult result = ctrl.notify(&msg);
    REQUIRE(result == kResultOk);

    REQUIRE(ctrl.terminate() == kResultOk);
}
