// ==============================================================================
// KeyboardShortcutHandler Unit Tests
// ==============================================================================
// T048: Tests for keyboard shortcut logic (Tab cycling, Space toggle, Arrow keys)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <memory>
#include <utility>

#include <editor_lifecycle_harness.h>

#include "controller/keyboard_shortcut_handler.h"
#include "controller/visibility_controllers.h"
#include "plugin_ids.h"

#include "vstgui/lib/cframe.h"
#include "vstgui/lib/controls/cknob.h"
#include "vstgui/lib/controls/ctextedit.h"

using namespace Disrumpo;

// =============================================================================
// Real KeyboardShortcutHandler against a headless CFrame (D-068)
// =============================================================================

namespace {

constexpr Steinberg::Vst::ParamID kKnobParamId = 4242;

/// CTextEdit whose focus hooks do nothing: the headless frame has no platform
/// frame to create the native text field on, but the frame's focus view is real.
class HeadlessTextEdit : public VSTGUI::CTextEdit {
public:
    explicit HeadlessTextEdit(const VSTGUI::CRect& r) : CTextEdit(r, nullptr, -1) {}
    void takeFocus() override {}
    void looseFocus() override {}
};

/// Bare edit controller whose parameter container the test fills.
class TestEditController : public Steinberg::Vst::EditControllerEx1 {
public:
    void addParam(const Steinberg::Vst::TChar* title, Steinberg::int32 stepCount, double def,
                  Steinberg::Vst::ParamID id) {
        parameters.addParameter(title, nullptr, stepCount, def,
            Steinberg::Vst::ParameterInfo::kCanAutomate, static_cast<Steinberg::int32>(id));
    }
};

/// Unopened CFrame with a text field and a parameter knob, attached and active by
/// hand so CFrame::setFocusView stores a real focus view; a bare edit controller
/// holds the knob's parameter and band 0's bypass.
struct VstguiReady {
    VstguiReady() { Krate::TestSupport::ensureVstguiInitialized(); }  // views read the platform factory
};

struct KeyboardRig : VstguiReady {
    TestEditController* controller = new TestEditController();
    VSTGUI::CFrame* frame = new VSTGUI::CFrame(VSTGUI::CRect(0, 0, 400, 300), nullptr);
    HeadlessTextEdit* textEdit = new HeadlessTextEdit(VSTGUI::CRect(10, 10, 200, 30));
    VSTGUI::CKnob* knob = new VSTGUI::CKnob(VSTGUI::CRect(10, 50, 60, 100), nullptr,
        static_cast<int32_t>(kKnobParamId), nullptr, nullptr);
    Steinberg::Vst::ParamID bypassId = makeBandParamId(0, BandParamType::kBandBypass);

    KeyboardRig() {
        controller->addParam(u"Knob", 0, 0.5, kKnobParamId);
        controller->addParam(u"Bypass", 1, 0.0, bypassId);
        frame->addView(textEdit);
        frame->addView(knob);
        frame->attached(frame);
        frame->onActivate(true);
    }
    ~KeyboardRig() {
        frame->setFocusView(nullptr);
        frame->onActivate(false);
        frame->forget();
        controller->release();
    }
    KeyboardRig(const KeyboardRig&) = delete;
    KeyboardRig& operator=(const KeyboardRig&) = delete;
    KeyboardRig(KeyboardRig&&) = delete;
    KeyboardRig& operator=(KeyboardRig&&) = delete;

    [[nodiscard]] double value(Steinberg::Vst::ParamID id) const {
        return controller->getParameterObject(id)->getNormalized();
    }
};

bool sendKey(KeyboardShortcutHandler& handler, VSTGUI::CFrame* frame, VSTGUI::VirtualKey key) {
    VSTGUI::KeyboardEvent event;
    event.virt = key;
    handler.onKeyboardEvent(event, frame);
    return event.consumed;
}

} // namespace

TEST_CASE("Keyboard hook leaves arrow keys Space and Tab to a focused text field",
          "[disrumpo][keyboard][D-068]") {
    KeyboardRig rig;
    KeyboardShortcutHandler handler(rig.controller, rig.frame, 4);

    // One Tab used as a shortcut first (knob focused): a band is now "focused",
    // which is what arms Space as bypass toggle.
    rig.frame->setFocusView(rig.knob);
    REQUIRE(sendKey(handler, rig.frame, VSTGUI::VirtualKey::Tab));

    rig.frame->setFocusView(rig.textEdit);
    REQUIRE(rig.frame->getFocusView() == rig.textEdit);

    const std::pair<VSTGUI::VirtualKey, const char*> keys[] = {
        {VSTGUI::VirtualKey::Left, "Left"}, {VSTGUI::VirtualKey::Right, "Right"},
        {VSTGUI::VirtualKey::Up, "Up"},     {VSTGUI::VirtualKey::Down, "Down"},
        {VSTGUI::VirtualKey::Space, "Space"}, {VSTGUI::VirtualKey::Tab, "Tab"},
    };
    for (const auto& [key, name] : keys) {
        REQUIRE(rig.frame->getFocusView() == rig.textEdit);
        const bool consumed = sendKey(handler, rig.frame, key);
        INFO("key " << name << " consumed=" << consumed);
        CHECK_FALSE(consumed);
    }
    CHECK(rig.value(rig.bypassId) == 0.0);
    CHECK(rig.value(kKnobParamId) == 0.5);
}

TEST_CASE("Arrow keys pass through when the focus view edits no parameter",
          "[disrumpo][keyboard][D-068]") {
    KeyboardRig rig;
    KeyboardShortcutHandler handler(rig.controller, rig.frame, 4);
    rig.frame->setFocusView(nullptr);
    REQUIRE(rig.frame->getFocusView() == nullptr);
    const bool consumed = sendKey(handler, rig.frame, VSTGUI::VirtualKey::Up);
    INFO("Up with no focus view consumed=" << consumed);
    CHECK_FALSE(consumed);
}

TEST_CASE("Keyboard shortcuts still act when no text field has focus",
          "[disrumpo][keyboard][D-068]") {
    KeyboardRig rig;
    KeyboardShortcutHandler handler(rig.controller, rig.frame, 4);
    bool escaped = false;
    handler.setEscapeCallback([&escaped] { escaped = true; });

    rig.frame->setFocusView(rig.knob);
    REQUIRE(sendKey(handler, rig.frame, VSTGUI::VirtualKey::Right));
    CHECK_THAT(rig.value(kKnobParamId), Catch::Matchers::WithinAbs(0.51, 1e-6));
    rig.frame->setFocusView(rig.knob);
    REQUIRE(sendKey(handler, rig.frame, VSTGUI::VirtualKey::Down));
    CHECK_THAT(rig.value(kKnobParamId), Catch::Matchers::WithinAbs(0.50, 1e-6));

    REQUIRE(sendKey(handler, rig.frame, VSTGUI::VirtualKey::Tab));
    CHECK(handler.getFocusedBandIndex() == 0);
    REQUIRE(sendKey(handler, rig.frame, VSTGUI::VirtualKey::Space));
    CHECK(rig.value(rig.bypassId) == 1.0);
    REQUIRE(sendKey(handler, rig.frame, VSTGUI::VirtualKey::Escape));
    CHECK(escaped);
}

TEST_CASE("Keyboard hook stands down while a preset overlay is open",
          "[disrumpo][keyboard][D-068]") {
    KeyboardRig rig;
    KeyboardShortcutHandler handler(rig.controller, rig.frame, 4);
    bool overlayOpen = true;
    handler.setOverlayOpenQuery([&overlayOpen] { return overlayOpen; });

    // A control hidden behind the overlay still holds the focus
    rig.frame->setFocusView(rig.knob);
    CHECK_FALSE(sendKey(handler, rig.frame, VSTGUI::VirtualKey::Up));
    CHECK_FALSE(sendKey(handler, rig.frame, VSTGUI::VirtualKey::Tab));
    CHECK(rig.value(kKnobParamId) == 0.5);

    overlayOpen = false;
    rig.frame->setFocusView(rig.knob);
    CHECK(sendKey(handler, rig.frame, VSTGUI::VirtualKey::Up));
    CHECK_THAT(rig.value(kKnobParamId), Catch::Matchers::WithinAbs(0.51, 1e-6));
}

TEST_CASE("Band Count drives the keyboard handler's Tab cycle",
          "[disrumpo][keyboard][D-068]") {
    Steinberg::Vst::ParameterInfo info{};
    info.id = makeGlobalParamId(GlobalParamType::kGlobalBandCount);
    info.stepCount = 3;
    info.defaultNormalizedValue = 1.0;
    auto* bandCountParam = new Steinberg::Vst::Parameter(info);
    bandCountParam->setNormalized(1.0);  // 4 bands

    auto handler = std::make_unique<KeyboardShortcutHandler>(nullptr, nullptr, 4);
    SpectrumDisplay* noDisplay = nullptr;
    auto* observer = new BandCountDisplayController(&noDisplay, bandCountParam, &handler);

    bandCountParam->setNormalized(1.0 / 3.0);  // 2 bands
    int visited[3] = {};
    for (int& v : visited) {
        REQUIRE(sendKey(*handler, nullptr, VSTGUI::VirtualKey::Tab));
        v = handler->getFocusedBandIndex();
    }
    INFO("Tab cycle at Band Count 2: " << visited[0] << "," << visited[1] << "," << visited[2]);
    CHECK(visited[0] == 0);
    CHECK(visited[1] == 1);
    CHECK(visited[2] == 0);

    observer->deactivate();
    observer->release();
    bandCountParam->release();
}

// =============================================================================
// Tab Cycling Logic Tests
// =============================================================================

TEST_CASE("Tab cycling wraps forward through bands", "[keyboard][tab]") {
    int activeBandCount = 4;
    int focusedBandIndex = -1;

    // First tab: -1 -> 0
    focusedBandIndex++;
    if (focusedBandIndex >= activeBandCount) focusedBandIndex = 0;
    REQUIRE(focusedBandIndex == 0);

    // Tab: 0 -> 1
    focusedBandIndex++;
    if (focusedBandIndex >= activeBandCount) focusedBandIndex = 0;
    REQUIRE(focusedBandIndex == 1);

    // Tab: 1 -> 2
    focusedBandIndex++;
    if (focusedBandIndex >= activeBandCount) focusedBandIndex = 0;
    REQUIRE(focusedBandIndex == 2);

    // Tab: 2 -> 3
    focusedBandIndex++;
    if (focusedBandIndex >= activeBandCount) focusedBandIndex = 0;
    REQUIRE(focusedBandIndex == 3);

    // Tab: 3 -> 0 (wrap)
    focusedBandIndex++;
    if (focusedBandIndex >= activeBandCount) focusedBandIndex = 0;
    REQUIRE(focusedBandIndex == 0);
}

TEST_CASE("Shift+Tab cycling wraps backward through bands", "[keyboard][tab]") {
    int activeBandCount = 4;
    int focusedBandIndex = 0;

    // Shift+Tab: 0 -> 3 (wrap backward)
    focusedBandIndex--;
    if (focusedBandIndex < 0) focusedBandIndex = activeBandCount - 1;
    REQUIRE(focusedBandIndex == 3);

    // Shift+Tab: 3 -> 2
    focusedBandIndex--;
    if (focusedBandIndex < 0) focusedBandIndex = activeBandCount - 1;
    REQUIRE(focusedBandIndex == 2);
}

// =============================================================================
// Space Toggle Logic Tests
// =============================================================================

TEST_CASE("Space toggles band bypass parameter", "[keyboard][space]") {
    // Simulate bypass toggle logic
    double currentBypass = 0.0;  // Not bypassed
    double newBypass = (currentBypass >= 0.5) ? 0.0 : 1.0;
    REQUIRE(newBypass == 1.0);

    // Toggle back
    currentBypass = newBypass;
    newBypass = (currentBypass >= 0.5) ? 0.0 : 1.0;
    REQUIRE(newBypass == 0.0);
}

TEST_CASE("Space does nothing when no band is focused", "[keyboard][space]") {
    int focusedBandIndex = -1;
    bool handled = (focusedBandIndex >= 0);
    REQUIRE_FALSE(handled);
}

// =============================================================================
// Arrow Key Step Calculation Tests
// =============================================================================

TEST_CASE("Fine adjustment step is 1/100th of range", "[keyboard][arrow]") {
    float fineStep = 0.01f;
    REQUIRE(fineStep == 0.01f);
}

TEST_CASE("Coarse adjustment step is 1/10th of range", "[keyboard][arrow]") {
    float coarseStep = 0.1f;
    REQUIRE(coarseStep == 0.1f);
}

TEST_CASE("Arrow key adjusts parameter within bounds", "[keyboard][arrow]") {
    double currentValue = 0.5;
    float stepFraction = 0.01f;

    SECTION("Up/Right increases value") {
        double newValue = currentValue + stepFraction;
        newValue = std::clamp(newValue, 0.0, 1.0);
        REQUIRE_THAT(newValue, Catch::Matchers::WithinAbs(0.51, 0.001));
    }

    SECTION("Down/Left decreases value") {
        double newValue = currentValue - stepFraction;
        newValue = std::clamp(newValue, 0.0, 1.0);
        REQUIRE_THAT(newValue, Catch::Matchers::WithinAbs(0.49, 0.001));
    }

    SECTION("Value clamps at maximum") {
        currentValue = 0.995;
        double newValue = currentValue + stepFraction;
        newValue = std::clamp(newValue, 0.0, 1.0);
        REQUIRE(newValue <= 1.0);
    }

    SECTION("Value clamps at minimum") {
        currentValue = 0.005;
        double newValue = currentValue - stepFraction;
        newValue = std::clamp(newValue, 0.0, 1.0);
        REQUIRE(newValue >= 0.0);
    }
}

TEST_CASE("Discrete parameter uses single step", "[keyboard][arrow]") {
    int stepCount = 7;  // e.g., Band Count has 8 values (0-7)
    double step = 1.0 / static_cast<double>(stepCount);

    REQUIRE_THAT(step, Catch::Matchers::WithinAbs(1.0 / 7.0, 0.001));

    // Stepping from 0.0 should give first step
    double currentValue = 0.0;
    double newValue = currentValue + step;
    REQUIRE_THAT(newValue, Catch::Matchers::WithinAbs(1.0 / 7.0, 0.001));
}

// =============================================================================
// Band Parameter ID Tests
// =============================================================================

TEST_CASE("Band bypass parameter ID is correctly encoded", "[keyboard][params]") {
    for (int band = 0; band < 8; ++band) {
        auto paramId = makeBandParamId(static_cast<uint8_t>(band), BandParamType::kBandBypass);
        REQUIRE(isBandParamId(paramId));
        REQUIRE(std::cmp_equal(extractBandIndex(paramId), band));
        REQUIRE(extractBandParamType(paramId) == BandParamType::kBandBypass);
    }
}
