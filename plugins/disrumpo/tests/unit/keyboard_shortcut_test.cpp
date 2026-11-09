// ==============================================================================
// KeyboardShortcutHandler Unit Tests
// ==============================================================================
// T048: Tests for keyboard shortcut logic (Tab cycling, Space toggle, Arrow keys)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <memory>
#include <optional>
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

bool sendKey(KeyboardShortcutHandler& handler, VSTGUI::CFrame* frame, VSTGUI::VirtualKey key,
             std::optional<VSTGUI::ModifierKey> modifier = std::nullopt) {
    VSTGUI::KeyboardEvent event;
    event.virt = key;
    if (modifier) event.modifiers.add(*modifier);
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
    // sendKey builds a VSTGUI::KeyboardEvent, whose constructor reads the
    // platform factory: without this the case crashes when it runs first
    const VstguiReady vstgui;
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
// Tab, Space and arrow keys through the real handler (D-123, fix plan T5.03)
// =============================================================================

TEST_CASE("Tab advances the focused band and wraps; Shift+Tab goes back",
          "[disrumpo][keyboard][tab][D-123]") {
    const VstguiReady vstgui;  // KeyboardEvent reads the platform factory
    KeyboardShortcutHandler handler(nullptr, nullptr, 4);
    REQUIRE(handler.getFocusedBandIndex() == -1);

    int forward[5] = {};
    for (int& v : forward) {
        REQUIRE(sendKey(handler, nullptr, VSTGUI::VirtualKey::Tab));
        v = handler.getFocusedBandIndex();
    }
    INFO("Tab x5: " << forward[0] << "," << forward[1] << "," << forward[2] << "," << forward[3] << ","
                    << forward[4]);
    CHECK(forward[0] == 0);
    CHECK(forward[1] == 1);
    CHECK(forward[2] == 2);
    CHECK(forward[3] == 3);
    CHECK(forward[4] == 0);

    int backward[2] = {};
    for (int& v : backward) {
        REQUIRE(sendKey(handler, nullptr, VSTGUI::VirtualKey::Tab, VSTGUI::ModifierKey::Shift));
        v = handler.getFocusedBandIndex();
    }
    INFO("Shift+Tab x2 from band 0: " << backward[0] << "," << backward[1]);
    CHECK(backward[0] == 3);
    CHECK(backward[1] == 2);
}

TEST_CASE("Space toggles the focused band's bypass and does nothing before a band is focused",
          "[disrumpo][keyboard][space][D-123]") {
    KeyboardRig rig;
    KeyboardShortcutHandler handler(rig.controller, rig.frame, 4);
    // A parameter control holds the focus (from nothing, Tab would move it into the text field)
    rig.frame->setFocusView(rig.knob);

    CHECK_FALSE(sendKey(handler, rig.frame, VSTGUI::VirtualKey::Space));
    CHECK(rig.value(rig.bypassId) == 0.0);

    REQUIRE(sendKey(handler, rig.frame, VSTGUI::VirtualKey::Tab));
    REQUIRE(handler.getFocusedBandIndex() == 0);
    REQUIRE(rig.frame->getFocusView() != rig.textEdit);
    CHECK(sendKey(handler, rig.frame, VSTGUI::VirtualKey::Space));
    CHECK(rig.value(rig.bypassId) == 1.0);
    CHECK(sendKey(handler, rig.frame, VSTGUI::VirtualKey::Space));
    CHECK(rig.value(rig.bypassId) == 0.0);
}

TEST_CASE("Arrow keys step by 1/100 or by 1/10 with Shift and clamp to the range",
          "[disrumpo][keyboard][arrow][D-123]") {
    KeyboardRig rig;
    KeyboardShortcutHandler handler(rig.controller, rig.frame, 4);

    rig.frame->setFocusView(rig.knob);
    REQUIRE(sendKey(handler, rig.frame, VSTGUI::VirtualKey::Up, VSTGUI::ModifierKey::Shift));
    CHECK_THAT(rig.value(kKnobParamId), Catch::Matchers::WithinAbs(0.6, 1e-6));
    rig.frame->setFocusView(rig.knob);
    REQUIRE(sendKey(handler, rig.frame, VSTGUI::VirtualKey::Left, VSTGUI::ModifierKey::Shift));
    CHECK_THAT(rig.value(kKnobParamId), Catch::Matchers::WithinAbs(0.5, 1e-6));

    rig.controller->setParamNormalized(kKnobParamId, 0.995);
    rig.frame->setFocusView(rig.knob);
    REQUIRE(sendKey(handler, rig.frame, VSTGUI::VirtualKey::Up));
    CHECK(rig.value(kKnobParamId) == 1.0);

    rig.controller->setParamNormalized(kKnobParamId, 0.005);
    rig.frame->setFocusView(rig.knob);
    REQUIRE(sendKey(handler, rig.frame, VSTGUI::VirtualKey::Down));
    CHECK(rig.value(kKnobParamId) == 0.0);
}

TEST_CASE("Arrow keys move a discrete parameter one list step", "[disrumpo][keyboard][arrow][D-123]") {
    KeyboardRig rig;
    constexpr Steinberg::Vst::ParamID kListParamId = 4243;
    rig.controller->addParam(u"List", 3, 0.0, kListParamId);  // 4 entries, like Band Count
    auto* listKnob = new VSTGUI::CKnob(VSTGUI::CRect(10, 110, 60, 160), nullptr, static_cast<int32_t>(kListParamId),
                                       nullptr, nullptr);
    rig.frame->addView(listKnob);
    KeyboardShortcutHandler handler(rig.controller, rig.frame, 4);

    rig.frame->setFocusView(listKnob);
    REQUIRE(sendKey(handler, rig.frame, VSTGUI::VirtualKey::Up));
    CHECK_THAT(rig.value(kListParamId), Catch::Matchers::WithinAbs(1.0 / 3.0, 1e-9));
    rig.frame->setFocusView(listKnob);
    REQUIRE(sendKey(handler, rig.frame, VSTGUI::VirtualKey::Up, VSTGUI::ModifierKey::Shift));
    CHECK_THAT(rig.value(kListParamId), Catch::Matchers::WithinAbs(2.0 / 3.0, 1e-9));
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
