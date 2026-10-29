// ==============================================================================
// TapPatternEditor view: zones, drags, gestures and what each writes (specs/iterum-tap-editor/design.md 3, 4, 5)
// ==============================================================================
// The view is built at (0, 0, 852, 140) with no frame and a recording edit sink; the mouse handlers and
// dispatchEvent are driven directly. The popup and cursor-setting paths need a frame, so the cursor is checked
// through the static cursorForZone(hitZoneAt(...)).
//
// Geometry (design 5.1, 5.2): x(ratio) = 42 + ratio x 802; y(level) = 14 + (-dB / 48) x 92; the silence band is
// y 106..114. A tap at 0 dB: handle y 11..22, body y 22..106.

#include "ui/tap_pattern_editor.h"
#include "ui/tap_pattern_editor_logic.h"
#include "parameters/custom_pattern_defaults.h"
#include "plugin_ids.h"

#include <krate/dsp/core/db_utils.h>

#include <editor_lifecycle_harness.h>

#include <vstgui/lib/cbuttonstate.h>
#include <vstgui/lib/cframe.h>
#include <vstgui/lib/events.h>
#include <vstgui/lib/vstkeycode.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cmath>
#include <utility>
#include <vector>

using namespace Iterum;
using Catch::Approx;
using Steinberg::Vst::ParamID;

namespace {

struct Write {
    char kind;  // 'b' begin, 'p' perform, 'e' end
    ParamID id;
    float value;
};

/// The edit sink createCustomView installs, recording every call.
struct Recorder {
    std::vector<Write> writes;

    TapEditSink sink() {
        TapEditSink s;
        s.begin = [this](ParamID id) { writes.push_back({.kind = 'b', .id = id, .value = 0.0f}); };
        s.end = [this](ParamID id) { writes.push_back({.kind = 'e', .id = id, .value = 0.0f}); };
        s.perform = [this](ParamID id, float v) { writes.push_back({.kind = 'p', .id = id, .value = v}); };
        return s;
    }

    [[nodiscard]] size_t count(char kind, ParamID id) const {
        size_t n = 0;
        for (const auto& w : writes) n += (w.kind == kind && w.id == id) ? 1u : 0u;
        return n;
    }
    [[nodiscard]] size_t performs() const {
        size_t n = 0;
        for (const auto& w : writes) n += w.kind == 'p' ? 1u : 0u;
        return n;
    }
    [[nodiscard]] float lastPerform(ParamID id) const {
        float v = -1.0f;
        for (const auto& w : writes) {
            if (w.kind == 'p' && w.id == id) v = w.value;
        }
        return v;
    }
    /// begin before every perform and end after the last one, for @p id.
    [[nodiscard]] bool bracketed(ParamID id) const {
        int state = 0;  // 0 none, 1 begun, 2 ended
        for (const auto& w : writes) {
            if (w.id != id) continue;
            if (w.kind == 'b') {
                if (state == 1) return false;
                state = 1;
            } else if (w.kind == 'p') {
                if (state != 1) return false;
            } else if (w.kind == 'e') {
                if (state != 1) return false;
                state = 2;
            }
        }
        return state == 2;
    }
    void clear() { writes.clear(); }
};

constexpr ParamID timeId(size_t i) { return kMultiTapCustomTime0Id + static_cast<ParamID>(i); }
constexpr ParamID levelId(size_t i) { return kMultiTapCustomLevel0Id + static_cast<ParamID>(i); }

[[nodiscard]] double xOf(float ratio) { return 42.0 + static_cast<double>(ratio) * 802.0; }
[[nodiscard]] double yOfDb(float dB) { return 14.0 + (-static_cast<double>(dB) / 48.0) * 92.0; }

constexpr double kBodyY = 80.0;    // inside the body of every tap above about -30 dB
constexpr double kHandleY = 16.0;  // inside the handle of a 0 dB tap
constexpr double kBandY = 110.0;   // the silence band

/// A fresh view: the registered defaults, 4 taps, Length 1/1, 120 BPM, Snap 1/4, editable, recording.
struct ViewRig {
    VSTGUI::SharedPointer<TapPatternEditor> view;
    Recorder rec;

    explicit ViewRig(size_t taps = 4) {
        // A VSTGUI::Event reads its timestamp from the platform factory, which only VSTGUI::init installs; a case
        // that runs alone in its shard has no editor test before it to have done that.
        Krate::TestSupport::ensureVstguiInitialized();
        view =VSTGUI::owned(new TapPatternEditor(VSTGUI::CRect(0, 0, 852, 140)));
        view->setEditSink(rec.sink());
        view->setCustomLength(CustomLength::N1);
        view->setTempo(120.0);
        view->setSnapDivision(SnapDivision::N4);
        view->setActiveTapCount(taps);
        view->setEditable(true);
        rec.clear();
    }

    void press(double x, double y, int32_t buttons = VSTGUI::kLButton) const {
        VSTGUI::CPoint p(x, y);
        view->onMouseDown(p, VSTGUI::CButtonState(buttons));
    }
    void move(double x, double y, int32_t buttons = VSTGUI::kLButton) const {
        VSTGUI::CPoint p(x, y);
        view->onMouseMoved(p, VSTGUI::CButtonState(buttons));
    }
    void release(double x, double y, int32_t buttons = VSTGUI::kLButton) const {
        VSTGUI::CPoint p(x, y);
        view->onMouseUp(p, VSTGUI::CButtonState(buttons));
    }
    void doubleClick(double x, double y) {
        press(x, y);
        release(x, y);
        rec.clear();
        press(x, y, VSTGUI::kLButton | VSTGUI::kDoubleClick);
        release(x, y);
    }
    /// A Ctrl/Cmd-click as the frame delivers it; returns whether the event was consumed.
    bool ctrlClick(double x, double y) const {
        VSTGUI::MouseDownEvent down(VSTGUI::CPoint(x, y), VSTGUI::MouseButton::Left);
        down.modifiers = VSTGUI::Modifiers(VSTGUI::ModifierKey::Control);
        down.clickCount = 1;
        view->dispatchEvent(down);
        const bool consumed = static_cast<bool>(down.consumed);
        VSTGUI::MouseMoveEvent drag(VSTGUI::CPoint(x + 40.0, y + 30.0), VSTGUI::MouseButton::Left);
        view->dispatchEvent(drag);
        VSTGUI::MouseUpEvent up(VSTGUI::CPoint(x + 40.0, y + 30.0), VSTGUI::MouseButton::Left);
        view->dispatchEvent(up);
        return consumed;
    }
};

[[nodiscard]] std::array<float, kMaxPatternTaps> presetRatios() {
    std::array<float, kMaxPatternTaps> r{};
    for (size_t i = 0; i < 4; ++i) r[i] = 0.25f * static_cast<float>(i + 1);
    return r;
}

[[nodiscard]] std::array<float, kMaxPatternTaps> presetLevels() {
    std::array<float, kMaxPatternTaps> l{};
    l.fill(1.0f);
    return l;
}

} // namespace

TEST_CASE("fresh pattern, Snap 1/4: a 3 px body drag of tap 1 does not land on tap 4's ratio",
          "[iterum][ui][tap-editor]") {
    ViewRig rig;
    for (size_t i = 0; i < 4; ++i) {
        REQUIRE(rig.view->getTapTimeRatio(i) == kDefaultCustomTimeRatios[i]);
    }
    const double x = xOf(0.25f);
    rig.press(x, kBodyY);
    rig.move(x + 3.0, kBodyY);
    rig.release(x + 3.0, kBodyY);

    REQUIRE(rig.view->getTapTimeRatio(0) == Approx(0.25f));
    REQUIRE(rig.view->getTapTimeRatio(0) != Approx(1.0f));
    REQUIRE(rig.rec.performs() == 0u);
    REQUIRE(rig.rec.count('b', timeId(0)) == 1u);
    REQUIRE(rig.rec.count('e', timeId(0)) == 1u);
}

TEST_CASE("the cursor per zone", "[iterum][ui][tap-editor]") {
    ViewRig rig;
    rig.view->setTapLevel(1, Krate::DSP::dbToGain(-46.0f));  // tap 2: short
    rig.view->setTapLevel(2, 0.0f);                          // tap 3: silent

    const auto cursorAt = [&](double x, double y, bool editable) {
        return TapPatternEditor::cursorForZone(rig.view->hitZoneAt(static_cast<float>(x), static_cast<float>(y)).zone,
                                               editable);
    };
    CHECK(cursorAt(xOf(0.25f), kBodyY, true) == VSTGUI::kCursorHSize);
    CHECK(cursorAt(xOf(0.25f), kHandleY, true) == VSTGUI::kCursorVSize);
    CHECK(cursorAt(xOf(0.5f), 108.0, true) == VSTGUI::kCursorSizeAll);
    CHECK(cursorAt(xOf(0.75f), kBandY, true) == VSTGUI::kCursorSizeAll);
    CHECK(cursorAt(xOf(0.375f), 60.0, true) == VSTGUI::kCursorDefault);

    SECTION("read-only: the default cursor everywhere") {
        for (const auto& [x, y] : {std::pair{xOf(0.25f), kBodyY}, std::pair{xOf(0.25f), kHandleY},
                                   std::pair{xOf(0.5f), 108.0}, std::pair{xOf(0.75f), kBandY}}) {
            CHECK(cursorAt(x, y, false) == VSTGUI::kCursorDefault);
        }
    }
}

TEST_CASE("hitZoneAt: handle, body, grip and silence band, highest index first", "[iterum][ui][tap-editor]") {
    ViewRig rig;
    rig.view->setTapLevel(1, Krate::DSP::dbToGain(-46.0f));
    rig.view->setTapLevel(2, 0.0f);

    auto hit = rig.view->hitZoneAt(static_cast<float>(xOf(0.25f)), static_cast<float>(kBodyY));
    CHECK(hit.tap == 0);
    CHECK(hit.zone == HitZone::Body);
    hit = rig.view->hitZoneAt(static_cast<float>(xOf(0.25f)), 12.0f);  // 2 px above a 0 dB bar top
    CHECK(hit.tap == 0);
    CHECK(hit.zone == HitZone::Handle);
    hit = rig.view->hitZoneAt(static_cast<float>(xOf(0.25f) + 7.5), static_cast<float>(kBodyY));  // in the 16 px box
    CHECK(hit.tap == 0);
    hit = rig.view->hitZoneAt(static_cast<float>(xOf(0.25f) + 9.0), static_cast<float>(kBodyY));  // outside it
    CHECK(hit.tap == -1);
    CHECK(hit.zone == HitZone::None);
    hit = rig.view->hitZoneAt(static_cast<float>(xOf(0.5f)), 112.0f);  // a short tap's grip reaches the band bottom
    CHECK(hit.tap == 1);
    CHECK(hit.zone == HitZone::Grip);
    hit = rig.view->hitZoneAt(static_cast<float>(xOf(0.25f)), static_cast<float>(kBandY));  // a tall tap ends at 106
    CHECK(hit.tap == -1);

    SECTION("stacked taps: the highest index wins") {
        rig.view->setTapTimeRatio(3, 0.25f);
        hit = rig.view->hitZoneAt(static_cast<float>(xOf(0.25f)), static_cast<float>(kBodyY));
        CHECK(hit.tap == 3);
    }
    SECTION("inactive taps are never hit") {
        rig.view->setTapTimeRatio(6, 0.375f);
        hit = rig.view->hitZoneAt(static_cast<float>(xOf(0.375f)), static_cast<float>(kBodyY));
        CHECK(hit.tap == -1);
    }
}

TEST_CASE("a silent tap is hit in the silence band", "[iterum][ui][tap-editor]") {
    ViewRig rig;
    rig.view->setTapLevel(2, 0.0f);
    const auto hit = rig.view->hitZoneAt(static_cast<float>(xOf(0.75f)), static_cast<float>(kBandY));
    CHECK(hit.tap == 2);
    CHECK(hit.zone == HitZone::Grip);
}

TEST_CASE("Ctrl-click on tap i writes level 1.0 only", "[iterum][ui][tap-editor]") {
    ViewRig rig;
    rig.view->setTapLevel(1, 0.5f);
    REQUIRE(rig.ctrlClick(xOf(0.5f), kBodyY));

    CHECK(rig.rec.count('b', levelId(1)) == 1u);
    CHECK(rig.rec.count('p', levelId(1)) == 1u);
    CHECK(rig.rec.count('e', levelId(1)) == 1u);
    CHECK(rig.rec.lastPerform(levelId(1)) == Approx(1.0f));
    CHECK(rig.rec.bracketed(levelId(1)));
    CHECK(rig.rec.count('b', timeId(1)) == 0u);
    CHECK(rig.rec.count('p', timeId(1)) == 0u);
    CHECK(rig.rec.writes.size() == 3u);
    CHECK(rig.view->getTapTimeRatio(1) == Approx(0.5f));
    CHECK(rig.view->getTapLevel(1) == Approx(1.0f));

    SECTION("a tap already at 0 dB is bracketed but not performed") {
        rig.rec.clear();
        REQUIRE(rig.ctrlClick(xOf(0.25f), kBodyY));
        CHECK(rig.rec.count('p', levelId(0)) == 0u);
    }
}

TEST_CASE("Ctrl-click never reaches CControl's default-value path", "[iterum][ui][tap-editor]") {
    ViewRig rig;
    rig.view->setDefaultValue(0.5f);
    rig.view->setValue(0.3f);
    rig.view->setTapLevel(1, 0.5f);

    REQUIRE(rig.ctrlClick(xOf(0.5f), kBodyY));
    CHECK(rig.view->getValue() == Approx(0.3f));
    CHECK(rig.view->getTapTimeRatio(1) == Approx(0.5f));  // the follow-up moves moved nothing

    rig.rec.clear();
    REQUIRE(rig.ctrlClick(xOf(0.375f), 60.0));  // empty plot
    CHECK(rig.view->getValue() == Approx(0.3f));
    CHECK(rig.rec.writes.empty());
    for (size_t i = 0; i < 4; ++i) {
        CHECK(rig.view->getTapTimeRatio(i) == kDefaultCustomTimeRatios[i]);
    }
}

TEST_CASE("Ctrl-click in read-only mode writes nothing and is consumed", "[iterum][ui][tap-editor]") {
    ViewRig rig;
    rig.view->setTapLevel(1, 0.5f);
    rig.view->setPresetPattern(presetRatios(), presetLevels(), 4, CustomLength::N1);
    rig.view->setEditable(false);
    REQUIRE(rig.ctrlClick(xOf(0.5f), kBodyY));
    CHECK(rig.rec.writes.empty());
    CHECK(rig.view->getTapLevel(1) == Approx(0.5f));
}

TEST_CASE("setActiveTapCount reveals stored taps and writes nothing", "[iterum][ui][tap-editor]") {
    ViewRig rig;
    rig.view->setTapTimeRatio(4, 0.5f);  // stacked on tap 2
    rig.view->setActiveTapCount(5);
    CHECK(rig.view->getActiveTapCount() == 5u);
    CHECK(rig.view->getTapTimeRatio(4) == Approx(0.5f));
    CHECK(rig.rec.writes.empty());

    rig.view->setActiveTapCount(3);
    rig.view->setActiveTapCount(8);
    CHECK(rig.rec.writes.empty());
    CHECK(rig.view->getTapTimeRatio(3) == Approx(1.0f));
    CHECK(rig.view->getTapTimeRatio(4) == Approx(0.5f));
}

TEST_CASE("placeRevealedTaps(old, new) moves only colliding taps in [old, new), bracketed", "[iterum][ui][tap-editor]") {
    ViewRig rig;
    rig.view->setTapTimeRatio(2, 0.5f);  // tap 3 stacked on tap 2: below old, never moved
    rig.view->setTapTimeRatio(4, 0.5f);  // tap 5 collides
    // tap 6 keeps its free stored 0.375
    rig.view->placeRevealedTaps(4, 6);

    CHECK(rig.view->getActiveTapCount() == 6u);
    CHECK(rig.rec.count('p', timeId(4)) == 1u);
    CHECK(rig.rec.bracketed(timeId(4)));
    CHECK(rig.rec.lastPerform(timeId(4)) == Approx(0.75f));  // ideal 5/6: the nearest free 1/4 line
    CHECK(rig.view->getTapTimeRatio(4) == Approx(0.75f));
    CHECK(rig.rec.count('b', timeId(5)) == 0u);
    CHECK(rig.rec.count('b', timeId(2)) == 0u);
    CHECK(rig.view->getTapTimeRatio(2) == Approx(0.5f));
    CHECK(rig.view->getTapTimeRatio(5) == Approx(0.375f));
    for (size_t i = 0; i < 16; ++i) {
        CHECK(rig.rec.count('p', levelId(i)) == 0u);
    }
}

TEST_CASE("a body drag changes time only, snapped, with one begin and one end and no perform inside the dead zone",
          "[iterum][ui][tap-editor]") {
    ViewRig rig;
    const double x = xOf(0.25f);
    rig.press(x, kBodyY);
    rig.move(x + 1.0, kBodyY + 1.0);
    CHECK(rig.rec.performs() == 0u);

    rig.move(x + 110.0, kBodyY + 30.0);  // 0.25 + 110/802 = 0.387 -> the 1/4 line 0.5
    CHECK(rig.view->getTapTimeRatio(0) == Approx(0.5f));
    CHECK(rig.view->getTapLevel(0) == Approx(1.0f));
    rig.release(x + 110.0, kBodyY + 30.0);

    CHECK(rig.rec.count('b', timeId(0)) == 1u);
    CHECK(rig.rec.count('e', timeId(0)) == 1u);
    CHECK(rig.rec.count('p', timeId(0)) == 1u);
    CHECK(rig.rec.lastPerform(timeId(0)) == Approx(0.5f));
    CHECK(rig.rec.bracketed(timeId(0)));
    CHECK(rig.rec.count('b', levelId(0)) == 0u);
    CHECK(rig.rec.count('p', levelId(0)) == 0u);
}

TEST_CASE("a snapped drag with Length 1/8 and Snap 1/4 never lands on ratio 0", "[iterum][ui][tap-editor]") {
    ViewRig rig;
    rig.view->setCustomLength(CustomLength::N8);  // the 1/16 fallback: lines 0.5 and 1.0
    const double x = xOf(0.25f);
    rig.press(x, kBodyY);
    rig.move(x - 300.0, kBodyY);
    rig.release(x - 300.0, kBodyY);
    CHECK(rig.view->getTapTimeRatio(0) > 0.0f);
    CHECK(rig.view->getTapTimeRatio(0) == Approx(0.5f));
}

TEST_CASE("a handle drag changes level only, never time", "[iterum][ui][tap-editor]") {
    ViewRig rig;
    const double x = xOf(0.25f);
    rig.press(x, kHandleY);
    rig.move(x + 50.0, kHandleY + 23.0);  // 23 px down: -12 dB
    rig.release(x + 50.0, kHandleY + 23.0);

    CHECK(rig.view->getTapTimeRatio(0) == Approx(0.25f));
    CHECK(rig.view->getTapLevel(0) == Approx(Krate::DSP::dbToGain(-12.0f)).margin(1e-3));
    CHECK(rig.rec.count('b', timeId(0)) == 0u);
    CHECK(rig.rec.count('p', timeId(0)) == 0u);
    CHECK(rig.rec.bracketed(levelId(0)));
}

TEST_CASE("Shift drag with Snap 1/4 lands off-grid, 10:1", "[iterum][ui][tap-editor]") {
    ViewRig rig;
    const double x = xOf(0.25f);
    rig.press(x, kBodyY, VSTGUI::kLButton | VSTGUI::kShift);
    rig.move(x + 80.0, kBodyY, VSTGUI::kLButton | VSTGUI::kShift);
    rig.release(x + 80.0, kBodyY, VSTGUI::kLButton | VSTGUI::kShift);
    CHECK(rig.view->getTapTimeRatio(0) == Approx(0.25f + 8.0f / 802.0f).margin(1e-5));
    CHECK(rig.rec.lastPerform(timeId(0)) == Approx(0.25f + 8.0f / 802.0f).margin(1e-5));
}

TEST_CASE("pressing Shift mid-drag does not move the tap", "[iterum][ui][tap-editor]") {
    ViewRig rig;
    const double x = xOf(0.25f);
    rig.press(x, kBodyY);
    rig.move(x + 110.0, kBodyY);
    REQUIRE(rig.view->getTapTimeRatio(0) == Approx(0.5f));

    rig.move(x + 110.0, kBodyY, VSTGUI::kLButton | VSTGUI::kShift);  // the modifier changes, the pointer does not
    CHECK(rig.view->getTapTimeRatio(0) == Approx(0.5f));
    rig.move(x + 120.0, kBodyY, VSTGUI::kLButton | VSTGUI::kShift);  // re-based: 10 px fine from 0.5
    CHECK(rig.view->getTapTimeRatio(0) == Approx(0.5f + 10.0f / 8020.0f).margin(1e-5));
    rig.release(x + 120.0, kBodyY, VSTGUI::kLButton | VSTGUI::kShift);
}

TEST_CASE("a silent tap can be raised by a 10 px upward drag", "[iterum][ui][tap-editor]") {
    ViewRig rig;
    rig.view->setTapLevel(2, 0.0f);
    const double x = xOf(0.75f);
    rig.press(x, kBandY);
    rig.move(x, kBandY - 10.0);
    rig.release(x, kBandY - 10.0);
    CHECK(rig.view->getTapLevel(2) == Approx(Krate::DSP::dbToGain(-48.0f + 10.0f * 48.0f / 92.0f)).margin(1e-4));
    CHECK(rig.view->getTapTimeRatio(2) == Approx(0.75f));
    CHECK(rig.rec.bracketed(levelId(2)));
    CHECK(rig.rec.count('p', timeId(2)) == 0u);
}

TEST_CASE("a silent tap can be dragged in time", "[iterum][ui][tap-editor]") {
    ViewRig rig;
    rig.view->setSnapDivision(SnapDivision::Off);
    rig.view->setTapLevel(2, 0.0f);
    const double x = xOf(0.75f);
    rig.press(x, kBandY);
    rig.move(x + 60.0, kBandY + 1.0);
    rig.release(x + 60.0, kBandY + 1.0);
    CHECK(rig.view->getTapTimeRatio(2) == Approx(0.75f + 60.0f / 802.0f).margin(1e-5));
    CHECK(rig.view->getTapLevel(2) == 0.0f);
    CHECK(rig.rec.bracketed(timeId(2)));
    CHECK(rig.rec.count('p', levelId(2)) == 0u);
}

TEST_CASE("a -46 dB tap can be dragged in time", "[iterum][ui][tap-editor]") {
    ViewRig rig;
    rig.view->setSnapDivision(SnapDivision::Off);
    const float level = Krate::DSP::dbToGain(-46.0f);
    rig.view->setTapLevel(1, level);
    const double x = xOf(0.5f);
    rig.press(x, 108.0);
    rig.move(x + 40.0, 108.0);
    rig.release(x + 40.0, 108.0);
    CHECK(rig.view->getTapTimeRatio(1) == Approx(0.5f + 40.0f / 802.0f).margin(1e-5));
    CHECK(rig.view->getTapLevel(1) == Approx(level));
}

TEST_CASE("a click on a tap writes nothing", "[iterum][ui][tap-editor]") {
    ViewRig rig;
    rig.press(xOf(0.5f), kBodyY);
    rig.release(xOf(0.5f), kBodyY);
    CHECK(rig.rec.performs() == 0u);
    CHECK(rig.view->getTapTimeRatio(1) == Approx(0.5f));
    CHECK(rig.view->getTapLevel(1) == Approx(1.0f));
}

TEST_CASE("double-click resets to (i+1)/n at 0 dB with both ids bracketed", "[iterum][ui][tap-editor]") {
    ViewRig rig;
    rig.view->setTapTimeRatio(1, 0.4f);
    rig.view->setTapLevel(1, 0.3f);
    rig.doubleClick(xOf(0.4f), kBodyY);

    CHECK(rig.view->getTapTimeRatio(1) == Approx(0.5f));
    CHECK(rig.view->getTapLevel(1) == Approx(1.0f));
    CHECK(rig.rec.bracketed(timeId(1)));
    CHECK(rig.rec.bracketed(levelId(1)));
    CHECK(rig.rec.lastPerform(timeId(1)) == Approx(0.5f));
    CHECK(rig.rec.lastPerform(levelId(1)) == Approx(1.0f));
}

TEST_CASE("double-click keeps a free ideal", "[iterum][ui][tap-editor]") {
    ViewRig rig;
    rig.view->setTapLevel(0, 0.5f);
    rig.doubleClick(xOf(0.25f), kBodyY);
    CHECK(rig.view->getTapTimeRatio(0) == Approx(0.25f));
    CHECK(rig.view->getTapLevel(0) == Approx(1.0f));
    CHECK(rig.rec.bracketed(timeId(0)));
    CHECK(rig.rec.bracketed(levelId(0)));
    CHECK(rig.rec.lastPerform(levelId(0)) == Approx(1.0f));
}

TEST_CASE("double-click moves a tap through placeTap and never onto another tap", "[iterum][ui][tap-editor]") {
    ViewRig rig(8);  // fresh 8: every eighth
    rig.doubleClick(xOf(0.125f), kBodyY);  // tap 5, ideal D(4, 8) = 0.625 is tap 7's
    CHECK(rig.view->getTapTimeRatio(4) == Approx(0.6875f));
    CHECK(rig.view->getTapLevel(4) == Approx(1.0f));
    for (size_t j = 0; j < 8; ++j) {
        if (j == 4) continue;
        CHECK(std::abs(rig.view->getTapTimeRatio(4) - rig.view->getTapTimeRatio(j)) >= kCollisionRatio);
    }
    CHECK(rig.rec.bracketed(timeId(4)));
    CHECK(rig.rec.bracketed(levelId(4)));
}

TEST_CASE("Reset at 2/1 and 60 BPM divides the playable 0.625", "[iterum][ui][tap-editor]") {
    ViewRig rig(8);
    rig.view->setCustomLength(CustomLength::N2x1);
    rig.view->setTempo(60.0);
    rig.view->resetToDefault();
    for (size_t i = 0; i < 8; ++i) {
        INFO("tap " << i + 1);
        CHECK(rig.view->getTapTimeRatio(i) == Approx(static_cast<float>(i + 1) / 8.0f * 0.625f).margin(1e-5));
        CHECK(rig.view->getTapLevel(i) == Approx(1.0f));
        CHECK(rig.rec.bracketed(timeId(i)));
        CHECK(rig.rec.bracketed(levelId(i)));
    }
}

TEST_CASE("Reset writes active taps only", "[iterum][ui][tap-editor]") {
    ViewRig rig;
    rig.view->setTapTimeRatio(5, 0.3f);
    rig.view->setTapLevel(5, 0.2f);
    rig.view->resetToDefault();
    for (size_t i = 4; i < 16; ++i) {
        CHECK(rig.rec.count('b', timeId(i)) == 0u);
        CHECK(rig.rec.count('b', levelId(i)) == 0u);
    }
    for (size_t i = 0; i < 4; ++i) {
        CHECK(rig.rec.bracketed(timeId(i)));
        CHECK(rig.rec.bracketed(levelId(i)));
    }
    CHECK(rig.view->getTapTimeRatio(5) == Approx(0.3f));
    CHECK(rig.view->getTapLevel(5) == Approx(0.2f));
}

TEST_CASE("Escape restores the pre-drag value with one perform then end", "[iterum][ui][tap-editor]") {
    ViewRig rig;
    const double x = xOf(0.25f);
    rig.press(x, kBodyY);
    rig.move(x + 110.0, kBodyY);
    REQUIRE(rig.view->getTapTimeRatio(0) == Approx(0.5f));
    rig.rec.clear();

    VstKeyCode escape{};
    escape.virt = VKEY_ESCAPE;
    rig.view->onKeyDown(escape);

    REQUIRE(rig.rec.writes.size() == 2u);
    CHECK(rig.rec.writes[0].kind == 'p');
    CHECK(rig.rec.writes[0].id == timeId(0));
    CHECK(rig.rec.writes[0].value == Approx(0.25f));
    CHECK(rig.rec.writes[1].kind == 'e');
    CHECK(rig.rec.writes[1].id == timeId(0));
    CHECK(rig.view->getTapTimeRatio(0) == Approx(0.25f));
}

TEST_CASE("removing the view mid-drag ends every begun id once", "[iterum][ui][tap-editor]") {
    ViewRig rig;
    // CViewContainer::removeView calls removed() only on an attached container (cviewcontainer.cpp:596), which the
    // editor's switch container always is; a headless frame has no window, so the container is attached by hand as
    // editor_screenshot_test.cpp does for the template root.
    auto* frame = new VSTGUI::CFrame(VSTGUI::CRect(0, 0, 852, 140), nullptr);
    auto* parent = new VSTGUI::CViewContainer(VSTGUI::CRect(0, 0, 852, 140));
    frame->addView(parent);
    if (!parent->isAttached()) {
        parent->attached(frame);
    }
    rig.view->remember();  // the container takes a reference of its own; the rig keeps its SharedPointer
    parent->addView(rig.view);
    REQUIRE(rig.view->isAttached());
    const double x = xOf(0.25f);
    rig.press(x, kBodyY);
    rig.move(x + 110.0, kBodyY);
    REQUIRE(rig.rec.count('b', timeId(0)) == 1u);
    REQUIRE(rig.rec.count('e', timeId(0)) == 0u);

    parent->removeView(rig.view, true);

    CHECK(rig.rec.count('e', timeId(0)) == 1u);
    CHECK(rig.rec.bracketed(timeId(0)));
    frame->forget();
}

TEST_CASE("the dragged tap ignores incoming sets of its own ids","[iterum][ui][tap-editor]") {
    ViewRig rig;
    const double x = xOf(0.25f);
    rig.press(x, kBodyY);
    rig.move(x + 110.0, kBodyY);
    REQUIRE(rig.view->getTapTimeRatio(0) == Approx(0.5f));

    rig.view->setTapTimeRatio(0, 0.9f);  // a host echo of the dragged id
    rig.view->setTapLevel(0, 0.2f);
    rig.view->setTapTimeRatio(1, 0.6f);  // another tap's id still applies
    CHECK(rig.view->getTapTimeRatio(0) == Approx(0.5f));
    CHECK(rig.view->getTapLevel(0) == Approx(1.0f));
    CHECK(rig.view->getTapTimeRatio(1) == Approx(0.6f));
    rig.release(x + 110.0, kBodyY);

    rig.view->setTapTimeRatio(0, 0.9f);  // after the drag it applies
    CHECK(rig.view->getTapTimeRatio(0) == Approx(0.9f));
}

TEST_CASE("read-only mode ignores mouse down and double-click", "[iterum][ui][tap-editor]") {
    ViewRig rig;
    rig.view->setPresetPattern(presetRatios(), presetLevels(), 4, CustomLength::N1);
    rig.view->setEditable(false);
    REQUIRE_FALSE(rig.view->isEditable());

    rig.press(xOf(0.25f), kBodyY);
    rig.move(xOf(0.25f) + 110.0, kBodyY);
    rig.release(xOf(0.25f) + 110.0, kBodyY);
    rig.doubleClick(xOf(0.5f), kBodyY);
    CHECK(rig.rec.writes.empty());
    for (size_t i = 0; i < 4; ++i) {
        CHECK(rig.view->getTapTimeRatio(i) == kDefaultCustomTimeRatios[i]);
        CHECK(rig.view->getTapLevel(i) == Approx(1.0f));
    }
}
