// ==============================================================================
// Phase 4 / Phase 7: Controller bus activation tests
// ==============================================================================
// Tests that the controller's notifyBusActivation() correctly tracks bus
// activation state and updates per-pad Output Bus parameters. FR-043.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>

#include "controller/controller.h"
#include "plugin_ids.h"
#include "dsp/pad_config.h"

namespace {

using namespace Membrum;
using namespace Steinberg;
using namespace Steinberg::Vst;

} // namespace

TEST_CASE("Controller bus activation: bus 0 always active", "[controller][bus-activation]")
{
    Controller ctrl;
    ctrl.initialize(nullptr);

    // Even if we try to deactivate bus 0, it should stay active
    ctrl.notifyBusActivation(0, false);
    CHECK(ctrl.isBusActive(0) == true);

    // The Output Bus parameter for pad 0 should be accessible
    auto paramId = static_cast<ParamID>(padParamId(0, kPadOutputBus));
    auto* param = ctrl.getParameterObject(paramId);
    REQUIRE(param != nullptr);

    // Setting to bus 0 (main) should always be valid
    CHECK(ctrl.setParamNormalized(paramId, 0.0) == kResultOk);
}

TEST_CASE("Controller bus activation: activate and deactivate aux bus", "[controller][bus-activation]")
{
    Controller ctrl;
    ctrl.initialize(nullptr);

    // Activate bus 2
    ctrl.notifyBusActivation(2, true);
    CHECK(ctrl.isBusActive(2) == true);

    // Assign pad 0 to bus 2
    const auto outputBusParam = static_cast<ParamID>(padParamId(0, kPadOutputBus));
    const double bus2Norm = 2.0 / static_cast<double>(kMaxOutputBuses - 1);
    ctrl.setParamNormalized(outputBusParam, bus2Norm);
    CHECK(ctrl.getParamNormalized(outputBusParam) == bus2Norm);

    // Deactivate bus 2 -- pad 0 should be reset to bus 0
    ctrl.notifyBusActivation(2, false);
    CHECK(ctrl.isBusActive(2) == false);
    CHECK(ctrl.getParamNormalized(outputBusParam) == 0.0);
}

TEST_CASE("Controller bus activation: deactivating bus resets affected pads only", "[controller][bus-activation]")
{
    Controller ctrl;
    ctrl.initialize(nullptr);

    // Activate buses 2 and 3
    ctrl.notifyBusActivation(2, true);
    ctrl.notifyBusActivation(3, true);

    const auto pad0Bus = static_cast<ParamID>(padParamId(0, kPadOutputBus));
    const auto pad1Bus = static_cast<ParamID>(padParamId(1, kPadOutputBus));

    const double bus2Norm = 2.0 / static_cast<double>(kMaxOutputBuses - 1);
    const double bus3Norm = 3.0 / static_cast<double>(kMaxOutputBuses - 1);

    // Assign pad 0 to bus 2, pad 1 to bus 3
    ctrl.setParamNormalized(pad0Bus, bus2Norm);
    ctrl.setParamNormalized(pad1Bus, bus3Norm);

    // Deactivate bus 2 -- only pad 0 should reset, pad 1 stays on bus 3
    ctrl.notifyBusActivation(2, false);
    CHECK(ctrl.getParamNormalized(pad0Bus) == 0.0);
    CHECK(ctrl.getParamNormalized(pad1Bus) == bus3Norm);
}

TEST_CASE("Controller bus activation: out-of-range bus index is safe", "[controller][bus-activation]")
{
    Controller ctrl;
    ctrl.initialize(nullptr);

    // Should not crash
    ctrl.notifyBusActivation(-1, true);
    ctrl.notifyBusActivation(16, true);
    ctrl.notifyBusActivation(100, true);

    // Out-of-range queries return false
    CHECK(ctrl.isBusActive(-1) == false);
    CHECK(ctrl.isBusActive(16) == false);
    CHECK(ctrl.isBusActive(100) == false);

    // Valid buses should still be queryable
    CHECK(ctrl.isBusActive(0) == true);
    CHECK(ctrl.isBusActive(1) == false);
}
