// ==============================================================================
// XYMorphPad Coordinate Conversion Tests (047-xy-morph-pad T014a, T014b)
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include "ui/xy_morph_pad.h"

#include "test_helpers/editor_lifecycle_harness.h"   // ensureVstguiInitialized (CControl edits need VSTGUI up)

#include "public.sdk/source/vst/vstparameters.h"
#include "pluginterfaces/vst/ivsteditcontroller.h"

#include <utility>
#include <vector>

using namespace Krate::Plugins;
using namespace VSTGUI;
using Catch::Approx;

// Helper: create a default 250x160 pad (matching editor.uidesc dimensions)
static XYMorphPad makePad() {
    return {CRect(0, 0, 250, 160), nullptr, -1};
}

namespace {

constexpr Steinberg::Vst::ParamID kXTag = 100;
constexpr Steinberg::Vst::ParamID kSecondaryTag = 101;

/// A host's component handler that records every performEdit (owned by the test: no reference counting).
class XYPadRecordingHandler final : public Steinberg::Vst::IComponentHandler {
public:
    std::vector<std::pair<Steinberg::Vst::ParamID, Steinberg::Vst::ParamValue>> edits;

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
    Steinberg::tresult PLUGIN_API queryInterface(const Steinberg::TUID iid, void** obj) override {
        if (Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::Vst::IComponentHandler::iid) ||
            Steinberg::FUnknownPrivate::iidEqual(iid, Steinberg::FUnknown::iid)) {
            *obj = static_cast<Steinberg::Vst::IComponentHandler*>(this);
            return Steinberg::kResultOk;
        }
        *obj = nullptr;
        return Steinberg::kNoInterface;
    }
    Steinberg::uint32 PLUGIN_API addRef() override { return 1; }
    Steinberg::uint32 PLUGIN_API release() override { return 1; }

    /// The value of the last performEdit sent for @p id, or -1 when none was sent.
    [[nodiscard]] double lastEdit(Steinberg::Vst::ParamID id) const {
        double last = -1.0;
        for (const auto& [editId, value] : edits) {
            if (editId == id)
                last = value;
        }
        return last;
    }
};

/// The smallest edit controller the pad can talk to: the X and the secondary parameter, both [0, 1].
class XYPadTestController final : public Steinberg::Vst::EditControllerEx1 {
public:
    XYPadTestController() {
        parameters.addParameter(new Steinberg::Vst::RangeParameter(
            STR16("X"), kXTag, nullptr, 0.0, 1.0, 0.25));
        parameters.addParameter(new Steinberg::Vst::RangeParameter(
            STR16("Secondary"), kSecondaryTag, nullptr, 0.0, 1.0, 0.0));
    }
};

MouseDownEvent mouseDownAt(const XYMorphPad& pad, float normX, float normY, uint32_t clicks) {
    float px = 0.0f;
    float py = 0.0f;
    pad.positionToPixel(normX, normY, px, py);
    MouseDownEvent e(CPoint(px, py), MouseEventButtonState(MouseButton::Left));
    e.clickCount = clicks;
    return e;
}

MouseMoveEvent mouseMoveTo(const XYMorphPad& pad, float normX, float normY) {
    float px = 0.0f;
    float py = 0.0f;
    pad.positionToPixel(normX, normY, px, py);
    return {CPoint(px, py), MouseEventButtonState(MouseButton::Left)};
}

} // namespace

// ==============================================================================
// MB-097 (Membrum audit, fix plan T1.25): the secondary (Y) axis edits the controller's own value
// ==============================================================================
// EditController::performEdit only forwards to the host, so a Y edit that skips setParamNormalized leaves the
// controller's value (what preset saves and the generic UI read) at its old value.

TEST_CASE("XYMorphPad: a secondary-axis edit sets the controller's parameter value as well as performEdit",
          "[xy_morph_pad][controller]") {
    Krate::TestSupport::ensureVstguiInitialized();
    XYPadTestController controller;
    XYPadRecordingHandler handler;
    REQUIRE(controller.setComponentHandler(&handler) == Steinberg::kResultOk);

    XYMorphPad pad(CRect(0, 0, 250, 160), nullptr, static_cast<int32_t>(kXTag));
    pad.setController(&controller);
    pad.setSecondaryParamId(kSecondaryTag);

    const auto checkSecondary = [&](const char* step) {
        INFO(step);
        CHECK(controller.getParamNormalized(kSecondaryTag) == Approx(pad.getMorphY()).margin(1e-6));
        CHECK(handler.lastEdit(kSecondaryTag) == Approx(pad.getMorphY()).margin(1e-6));
    };

    // Click and drag
    auto down = mouseDownAt(pad, 0.3f, 0.7f, 1);
    pad.onMouseDownEvent(down);
    CHECK(pad.getMorphY() == Approx(0.7).margin(0.01));
    checkSecondary("mouse down");

    auto move = mouseMoveTo(pad, 0.6f, 0.2f);
    pad.onMouseMoveEvent(move);
    CHECK(pad.getMorphY() == Approx(0.2).margin(0.01));
    checkSecondary("mouse move");

    MouseUpEvent up(move.mousePosition, MouseEventButtonState(MouseButton::Left));
    pad.onMouseUpEvent(up);
    checkSecondary("mouse up");

    // Mouse wheel: vertical scroll moves Y
    MouseWheelEvent wheel;
    wheel.mousePosition = move.mousePosition;
    wheel.deltaY = 2.0;
    pad.onMouseWheelEvent(wheel);
    CHECK(pad.getMorphY() == Approx(0.3).margin(0.01));
    checkSecondary("mouse wheel");

    // Double-click resets to the centre
    auto dbl = mouseDownAt(pad, 0.9f, 0.9f, 2);
    pad.onMouseDownEvent(dbl);
    CHECK(pad.getMorphY() == Approx(0.5).margin(1e-6));
    checkSecondary("double-click");

    // Escape during a drag restores the pre-drag value
    auto down2 = mouseDownAt(pad, 0.1f, 0.9f, 1);
    pad.onMouseDownEvent(down2);
    checkSecondary("second mouse down");
    KeyboardEvent esc;
    esc.virt = VirtualKey::Escape;
    pad.onKeyboardEvent(esc);
    CHECK(pad.getMorphY() == Approx(0.5).margin(1e-6));
    checkSecondary("escape");

    controller.setComponentHandler(nullptr);
}

// ==============================================================================
// T014a: pixelToPosition out-of-bounds clamping
// ==============================================================================

TEST_CASE("pixelToPosition clamps negative X to 0.0", "[xy_morph_pad][coord]") {
    auto pad = makePad();
    float normX = 0.0f;
    float normY = 0.0f;
    pad.pixelToPosition(-50.0f, 80.0f, normX, normY);

    REQUIRE(normX == 0.0f);
}

TEST_CASE("pixelToPosition clamps X beyond right edge to 1.0", "[xy_morph_pad][coord]") {
    auto pad = makePad();
    float normX = 0.0f;
    float normY = 0.0f;
    pad.pixelToPosition(300.0f, 80.0f, normX, normY);

    REQUIRE(normX == 1.0f);
}

TEST_CASE("pixelToPosition clamps negative Y to 1.0 (top)", "[xy_morph_pad][coord]") {
    auto pad = makePad();
    float normX = 0.0f;
    float normY = 0.0f;
    // Negative pixel Y = above the pad = normY should clamp to 1.0 (Y-inverted)
    pad.pixelToPosition(125.0f, -50.0f, normX, normY);

    REQUIRE(normY == 1.0f);
}

TEST_CASE("pixelToPosition clamps Y beyond bottom edge to 0.0", "[xy_morph_pad][coord]") {
    auto pad = makePad();
    float normX = 0.0f;
    float normY = 0.0f;
    // Large pixel Y = below the pad = normY should clamp to 0.0 (Y-inverted)
    pad.pixelToPosition(125.0f, 300.0f, normX, normY);

    REQUIRE(normY == 0.0f);
}

TEST_CASE("pixelToPosition clamps both axes for far out-of-bounds", "[xy_morph_pad][coord]") {
    auto pad = makePad();
    float normX = 0.5f;
    float normY = 0.5f;
    // Way off bottom-right
    pad.pixelToPosition(1000.0f, 1000.0f, normX, normY);

    REQUIRE(normX == 1.0f);
    REQUIRE(normY == 0.0f);
}

// ==============================================================================
// T014b: Coordinate round-trip within 0.01 tolerance (SC-006)
// ==============================================================================

TEST_CASE("Coordinate round-trip at center (0.5, 0.5)", "[xy_morph_pad][coord][roundtrip]") {
    auto pad = makePad();
    float pixelX = 0.0f;
    float pixelY = 0.0f;
    pad.positionToPixel(0.5f, 0.5f, pixelX, pixelY);

    float normX = 0.0f;
    float normY = 0.0f;
    pad.pixelToPosition(pixelX, pixelY, normX, normY);

    REQUIRE(normX == Approx(0.5f).margin(0.01f));
    REQUIRE(normY == Approx(0.5f).margin(0.01f));
}

TEST_CASE("Coordinate round-trip at origin (0.0, 0.0)", "[xy_morph_pad][coord][roundtrip]") {
    auto pad = makePad();
    float pixelX = 0.0f;
    float pixelY = 0.0f;
    pad.positionToPixel(0.0f, 0.0f, pixelX, pixelY);

    float normX = 0.0f;
    float normY = 0.0f;
    pad.pixelToPosition(pixelX, pixelY, normX, normY);

    REQUIRE(normX == Approx(0.0f).margin(0.01f));
    REQUIRE(normY == Approx(0.0f).margin(0.01f));
}

TEST_CASE("Coordinate round-trip at (1.0, 1.0)", "[xy_morph_pad][coord][roundtrip]") {
    auto pad = makePad();
    float pixelX = 0.0f;
    float pixelY = 0.0f;
    pad.positionToPixel(1.0f, 1.0f, pixelX, pixelY);

    float normX = 0.0f;
    float normY = 0.0f;
    pad.pixelToPosition(pixelX, pixelY, normX, normY);

    REQUIRE(normX == Approx(1.0f).margin(0.01f));
    REQUIRE(normY == Approx(1.0f).margin(0.01f));
}

TEST_CASE("Coordinate round-trip at arbitrary position (0.3, 0.7)", "[xy_morph_pad][coord][roundtrip]") {
    auto pad = makePad();
    float pixelX = 0.0f;
    float pixelY = 0.0f;
    pad.positionToPixel(0.3f, 0.7f, pixelX, pixelY);

    float normX = 0.0f;
    float normY = 0.0f;
    pad.pixelToPosition(pixelX, pixelY, normX, normY);

    REQUIRE(normX == Approx(0.3f).margin(0.01f));
    REQUIRE(normY == Approx(0.7f).margin(0.01f));
}

TEST_CASE("Coordinate round-trip on large pad (500x400)", "[xy_morph_pad][coord][roundtrip]") {
    XYMorphPad pad(CRect(0, 0, 500, 400), nullptr, -1);
    float pixelX = 0.0f;
    float pixelY = 0.0f;
    pad.positionToPixel(0.75f, 0.25f, pixelX, pixelY);

    float normX = 0.0f;
    float normY = 0.0f;
    pad.pixelToPosition(pixelX, pixelY, normX, normY);

    REQUIRE(normX == Approx(0.75f).margin(0.01f));
    REQUIRE(normY == Approx(0.25f).margin(0.01f));
}

TEST_CASE("Coordinate round-trip on minimum size pad (80x80)", "[xy_morph_pad][coord][roundtrip]") {
    XYMorphPad pad(CRect(0, 0, 80, 80), nullptr, -1);
    float pixelX = 0.0f;
    float pixelY = 0.0f;
    pad.positionToPixel(0.5f, 0.5f, pixelX, pixelY);

    float normX = 0.0f;
    float normY = 0.0f;
    pad.pixelToPosition(pixelX, pixelY, normX, normY);

    REQUIRE(normX == Approx(0.5f).margin(0.01f));
    REQUIRE(normY == Approx(0.5f).margin(0.01f));
}

TEST_CASE("Coordinate round-trip with non-zero origin", "[xy_morph_pad][coord][roundtrip]") {
    // Pad positioned at (100, 50) to (350, 210) — matching real layout offset
    XYMorphPad pad(CRect(100, 50, 350, 210), nullptr, -1);
    float pixelX = 0.0f;
    float pixelY = 0.0f;
    pad.positionToPixel(0.6f, 0.4f, pixelX, pixelY);

    float normX = 0.0f;
    float normY = 0.0f;
    pad.pixelToPosition(pixelX, pixelY, normX, normY);

    REQUIRE(normX == Approx(0.6f).margin(0.01f));
    REQUIRE(normY == Approx(0.4f).margin(0.01f));
}
