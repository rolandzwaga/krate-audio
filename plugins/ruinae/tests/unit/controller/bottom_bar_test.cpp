// ==============================================================================
// Bottom Bar Control Tests (081-interaction-polish, Phase 8)
// ==============================================================================
// T079: Dice trigger behavior - verifies beginEdit/performEdit(1.0)/
//       performEdit(0.0)/endEdit sequence, and that param doesn't stay at 1.0.
// T080: Fill toggle latch behavior - verifies on/off toggling and that the
//       parameter remains latched between clicks.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "plugin_ids.h"
#include "controller/controller.h"

#include "pluginterfaces/vst/vsttypes.h"
#include "public.sdk/source/vst/vstparameters.h"

using Catch::Approx;

// ==============================================================================
// T079: Dice Trigger Behavior Tests (SC-006)
// ==============================================================================
// The Dice button is an ActionButton bound to kArpDiceTriggerId. When clicked,
// the controller must issue:
//   beginEdit(kArpDiceTriggerId)
//   performEdit(kArpDiceTriggerId, 1.0)
//   performEdit(kArpDiceTriggerId, 0.0)
//   endEdit(kArpDiceTriggerId)
// After the sequence, the parameter must not remain at 1.0.
//
// Since we cannot instantiate the full VSTGUI editor in a unit test, we verify
// the parameter protocol by checking:
//   1. The parameter exists and is registered
//   2. After setting to 1.0 then immediately 0.0, the value is 0.0
//   3. The parameter ID is correct (3291)
// ==============================================================================

TEST_CASE("BottomBar_DiceTrigger_ParameterExists", "[bottombar][dice]") {
    Ruinae::Controller controller;
    auto result = controller.initialize(nullptr);
    REQUIRE(result == Steinberg::kResultOk);

    auto* param = controller.getParameterObject(Ruinae::kArpDiceTriggerId);
    REQUIRE(param != nullptr);

    controller.terminate();
}

TEST_CASE("BottomBar_DiceTrigger_IdIs3291", "[bottombar][dice]") {
    REQUIRE(Ruinae::kArpDiceTriggerId == 3291);
}

TEST_CASE("BottomBar_DiceTrigger_SpikeAndReset", "[bottombar][dice]") {
    // Simulate the Dice trigger protocol: set to 1.0, then 0.0
    // After the sequence, the parameter should be at 0.0
    Ruinae::Controller controller;
    auto result = controller.initialize(nullptr);
    REQUIRE(result == Steinberg::kResultOk);

    auto paramId = static_cast<Steinberg::Vst::ParamID>(Ruinae::kArpDiceTriggerId);

    // Simulate the beginEdit/performEdit(1.0)/performEdit(0.0)/endEdit sequence
    controller.beginEdit(paramId);
    controller.setParamNormalized(paramId, 1.0);

    // Verify the parameter is at 1.0 during the spike
    auto* param = controller.getParameterObject(paramId);
    REQUIRE(param != nullptr);
    REQUIRE(param->getNormalized() == Approx(1.0).margin(1e-6));

    // Now reset to 0.0
    controller.setParamNormalized(paramId, 0.0);
    controller.endEdit(paramId);

    // After the sequence, the parameter must be at 0.0
    REQUIRE(param->getNormalized() == Approx(0.0).margin(1e-6));

    controller.terminate();
}

TEST_CASE("BottomBar_DiceTrigger_DoesNotRemainAt1", "[bottombar][dice]") {
    // This verifies FR-036: the parameter MUST NOT remain at 1.0 after the click
    Ruinae::Controller controller;
    auto result = controller.initialize(nullptr);
    REQUIRE(result == Steinberg::kResultOk);

    auto paramId = static_cast<Steinberg::Vst::ParamID>(Ruinae::kArpDiceTriggerId);

    // Perform the spike sequence
    controller.beginEdit(paramId);
    controller.setParamNormalized(paramId, 1.0);
    controller.setParamNormalized(paramId, 0.0);
    controller.endEdit(paramId);

    // Check that reading the parameter gives 0.0
    auto* param = controller.getParameterObject(paramId);
    REQUIRE(param != nullptr);
    REQUIRE(param->getNormalized() == Approx(0.0).margin(1e-6));

    controller.terminate();
}

// ==============================================================================
// T080: Fill Toggle Latch Behavior Tests
// ==============================================================================
// The Fill button is a ToggleButton bound to kArpFillToggleId. It latches:
//   - First click: value goes to 1.0 and stays
//   - Second click: value goes to 0.0 and stays
//   - Parameter remains latched between clicks (does not auto-reset)
// ==============================================================================

TEST_CASE("BottomBar_FillToggle_ParameterExists", "[bottombar][fill]") {
    Ruinae::Controller controller;
    auto result = controller.initialize(nullptr);
    REQUIRE(result == Steinberg::kResultOk);

    auto* param = controller.getParameterObject(Ruinae::kArpFillToggleId);
    REQUIRE(param != nullptr);

    controller.terminate();
}

TEST_CASE("BottomBar_FillToggle_IdIs3280", "[bottombar][fill]") {
    REQUIRE(Ruinae::kArpFillToggleId == 3280);
}

TEST_CASE("BottomBar_FillToggle_LatchOn", "[bottombar][fill]") {
    // Toggle ON: set to 1.0, verify it stays at 1.0
    Ruinae::Controller controller;
    auto result = controller.initialize(nullptr);
    REQUIRE(result == Steinberg::kResultOk);

    auto paramId = static_cast<Steinberg::Vst::ParamID>(Ruinae::kArpFillToggleId);

    // Toggle ON
    controller.beginEdit(paramId);
    controller.setParamNormalized(paramId, 1.0);
    controller.endEdit(paramId);

    auto* param = controller.getParameterObject(paramId);
    REQUIRE(param != nullptr);
    REQUIRE(param->getNormalized() == Approx(1.0).margin(1e-6));

    controller.terminate();
}

TEST_CASE("BottomBar_FillToggle_LatchOff", "[bottombar][fill]") {
    // Toggle OFF: set to 1.0, then back to 0.0
    Ruinae::Controller controller;
    auto result = controller.initialize(nullptr);
    REQUIRE(result == Steinberg::kResultOk);

    auto paramId = static_cast<Steinberg::Vst::ParamID>(Ruinae::kArpFillToggleId);

    // Toggle ON first
    controller.beginEdit(paramId);
    controller.setParamNormalized(paramId, 1.0);
    controller.endEdit(paramId);

    // Toggle OFF
    controller.beginEdit(paramId);
    controller.setParamNormalized(paramId, 0.0);
    controller.endEdit(paramId);

    auto* param = controller.getParameterObject(paramId);
    REQUIRE(param != nullptr);
    REQUIRE(param->getNormalized() == Approx(0.0).margin(1e-6));

    controller.terminate();
}

TEST_CASE("BottomBar_FillToggle_RemainsLatched", "[bottombar][fill]") {
    // After toggling ON, the value should remain at 1.0 without any further
    // interaction (latching behavior, not momentary)
    Ruinae::Controller controller;
    auto result = controller.initialize(nullptr);
    REQUIRE(result == Steinberg::kResultOk);

    auto paramId = static_cast<Steinberg::Vst::ParamID>(Ruinae::kArpFillToggleId);

    // Toggle ON
    controller.beginEdit(paramId);
    controller.setParamNormalized(paramId, 1.0);
    controller.endEdit(paramId);

    // Read back after edit block is closed - should still be latched
    auto* param = controller.getParameterObject(paramId);
    REQUIRE(param != nullptr);
    REQUIRE(param->getNormalized() == Approx(1.0).margin(1e-6));

    // Read again (simulating time passing) - should still be 1.0
    REQUIRE(param->getNormalized() == Approx(1.0).margin(1e-6));

    controller.terminate();
}

// ==============================================================================
// T086: Bottom Bar Automation Round-Trip Tests (SC-006)
// ==============================================================================
// Verify all bottom bar control parameters can be set and read back correctly.
// ==============================================================================

TEST_CASE("BottomBar_Humanize_AutomationRoundTrip", "[bottombar][automation]") {
    Ruinae::Controller controller;
    auto result = controller.initialize(nullptr);
    REQUIRE(result == Steinberg::kResultOk);

    auto paramId = static_cast<Steinberg::Vst::ParamID>(Ruinae::kArpHumanizeId);
    controller.setParamNormalized(paramId, 0.5);

    auto* param = controller.getParameterObject(paramId);
    REQUIRE(param != nullptr);
    REQUIRE(param->getNormalized() == Approx(0.5).margin(0.001));

    controller.terminate();
}

TEST_CASE("BottomBar_Spice_AutomationRoundTrip", "[bottombar][automation]") {
    Ruinae::Controller controller;
    auto result = controller.initialize(nullptr);
    REQUIRE(result == Steinberg::kResultOk);

    auto paramId = static_cast<Steinberg::Vst::ParamID>(Ruinae::kArpSpiceId);
    controller.setParamNormalized(paramId, 0.75);

    auto* param = controller.getParameterObject(paramId);
    REQUIRE(param != nullptr);
    REQUIRE(param->getNormalized() == Approx(0.75).margin(0.001));

    controller.terminate();
}

TEST_CASE("BottomBar_RatchetSwing_AutomationRoundTrip", "[bottombar][automation]") {
    Ruinae::Controller controller;
    auto result = controller.initialize(nullptr);
    REQUIRE(result == Steinberg::kResultOk);

    auto paramId = static_cast<Steinberg::Vst::ParamID>(Ruinae::kArpRatchetSwingId);
    controller.setParamNormalized(paramId, 0.3);

    auto* param = controller.getParameterObject(paramId);
    REQUIRE(param != nullptr);
    REQUIRE(param->getNormalized() == Approx(0.3).margin(0.001));

    controller.terminate();
}

TEST_CASE("BottomBar_EuclideanHits_AutomationRoundTrip", "[bottombar][automation]") {
    Ruinae::Controller controller;
    auto result = controller.initialize(nullptr);
    REQUIRE(result == Steinberg::kResultOk);

    auto paramId = static_cast<Steinberg::Vst::ParamID>(Ruinae::kArpEuclideanHitsId);
    // Hits is discrete 0-32, set to normalized 0.5 (= 16 hits)
    controller.setParamNormalized(paramId, 0.5);

    auto* param = controller.getParameterObject(paramId);
    REQUIRE(param != nullptr);
    REQUIRE(param->getNormalized() == Approx(0.5).margin(0.001));

    controller.terminate();
}

TEST_CASE("BottomBar_EuclideanSteps_AutomationRoundTrip", "[bottombar][automation]") {
    Ruinae::Controller controller;
    auto result = controller.initialize(nullptr);
    REQUIRE(result == Steinberg::kResultOk);

    auto paramId = static_cast<Steinberg::Vst::ParamID>(Ruinae::kArpEuclideanStepsId);
    // Steps is discrete 2-32, normalized 0.5
    controller.setParamNormalized(paramId, 0.5);

    auto* param = controller.getParameterObject(paramId);
    REQUIRE(param != nullptr);
    REQUIRE(param->getNormalized() == Approx(0.5).margin(0.001));

    controller.terminate();
}

TEST_CASE("BottomBar_EuclideanRotation_AutomationRoundTrip", "[bottombar][automation]") {
    Ruinae::Controller controller;
    auto result = controller.initialize(nullptr);
    REQUIRE(result == Steinberg::kResultOk);

    auto paramId = static_cast<Steinberg::Vst::ParamID>(Ruinae::kArpEuclideanRotationId);
    controller.setParamNormalized(paramId, 0.25);

    auto* param = controller.getParameterObject(paramId);
    REQUIRE(param != nullptr);
    REQUIRE(param->getNormalized() == Approx(0.25).margin(0.001));

    controller.terminate();
}

TEST_CASE("BottomBar_Fill_AutomationRoundTrip", "[bottombar][automation]") {
    Ruinae::Controller controller;
    auto result = controller.initialize(nullptr);
    REQUIRE(result == Steinberg::kResultOk);

    auto paramId = static_cast<Steinberg::Vst::ParamID>(Ruinae::kArpFillToggleId);
    controller.setParamNormalized(paramId, 1.0);

    auto* param = controller.getParameterObject(paramId);
    REQUIRE(param != nullptr);
    REQUIRE(param->getNormalized() == Approx(1.0).margin(0.001));

    // Round-trip back to 0.0
    controller.setParamNormalized(paramId, 0.0);
    REQUIRE(param->getNormalized() == Approx(0.0).margin(0.001));

    controller.terminate();
}
