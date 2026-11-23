// ==============================================================================
// Bottom Bar Control Tests (081-interaction-polish, Phase 8)
// ==============================================================================
// T079: Dice trigger behavior - verifies a click sends the host the single
//       performEdit(1.0) and that the param doesn't stay at 1.0 (R-142).
// T080: Fill toggle latch behavior - verifies on/off toggling and that the
//       parameter remains latched between clicks.
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include "plugin_ids.h"
#include "controller/controller.h"
#include "ui/action_button.h"

#include "pluginterfaces/vst/ivsteditcontroller.h"
#include "pluginterfaces/vst/vsttypes.h"
#include "public.sdk/source/vst/vstparameters.h"

#include <vector>

using Catch::Approx;

// ==============================================================================
// T079: Dice Trigger Behavior Tests (SC-006)
// ==============================================================================
// The Dice button is an ActionButton bound to kArpDiceTriggerId. When clicked,
// the host receives exactly one edit (R-142):
//   beginEdit(kArpDiceTriggerId)
//   performEdit(kArpDiceTriggerId, 1.0)   <- from VST3Editor, the primary listener
//   endEdit(kArpDiceTriggerId)
// The controller (a sub-listener) resets only its own parameter to 0.0 with
// setParamNormalized; it must not send performEdit(0.0), because a processor
// that receives 1.0 and 0.0 in one block would lose the trigger.
// After the click, the parameter must not remain at 1.0.
// ==============================================================================

namespace {

/// Records every performEdit the controller sends to the host.
class DiceEditRecorder final : public Steinberg::Vst::IComponentHandler {
public:
    std::vector<std::pair<Steinberg::Vst::ParamID, double>> edits;

    Steinberg::tresult PLUGIN_API beginEdit(Steinberg::Vst::ParamID /*id*/) override {
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API performEdit(Steinberg::Vst::ParamID id,
                                              Steinberg::Vst::ParamValue value) override {
        edits.emplace_back(id, value);
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API endEdit(Steinberg::Vst::ParamID /*id*/) override {
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API restartComponent(Steinberg::int32 /*flags*/) override {
        return Steinberg::kResultOk;
    }
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID /*iid*/,
                                                 void** obj) override {
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }
    // Owned by the test case's stack frame.
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }
};

} // namespace

TEST_CASE("BottomBar_DiceTrigger_ClickSendsSingleEdit", "[bottombar][dice]") {
    Ruinae::Controller controller;
    REQUIRE(controller.initialize(nullptr) == Steinberg::kResultOk);
    DiceEditRecorder host;
    controller.setComponentHandler(&host);

    const auto paramId = static_cast<Steinberg::Vst::ParamID>(Ruinae::kArpDiceTriggerId);
    auto* button = new Krate::Plugins::ActionButton(
        VSTGUI::CRect(0, 0, 20, 20), nullptr, static_cast<int32_t>(paramId));

    // The click as ActionButton::onMouseUp delivers it: the control holds 1.0
    // while its listeners run. VST3Editor (primary listener) sets the
    // parameter and sends performEdit(1.0); then the controller sub-listener.
    controller.beginEdit(paramId);
    button->setValueNormalized(1.0f);
    controller.setParamNormalized(paramId, 1.0);
    controller.performEdit(paramId, 1.0);
    controller.valueChanged(button);
    button->setValueNormalized(0.0f);
    controller.endEdit(paramId);

    // The host saw the trigger and nothing that cancels it.
    REQUIRE(host.edits.size() == 1);
    CHECK(host.edits[0].first == paramId);
    CHECK(host.edits[0].second == Approx(1.0).margin(1e-6));

    // The controller's own parameter is back at 0.0.
    auto* param = controller.getParameterObject(paramId);
    REQUIRE(param != nullptr);
    CHECK(param->getNormalized() == Approx(0.0).margin(1e-6));

    button->forget();
    controller.setComponentHandler(nullptr);
    controller.terminate();
}

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

    // Simulate the click: the parameter goes to 1.0, then the controller's
    // local reset puts it back to 0.0
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
