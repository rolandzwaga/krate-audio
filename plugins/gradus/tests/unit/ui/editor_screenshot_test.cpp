// ==============================================================================
// Editor screenshots for the site and the manual (hidden: [.][screenshots][site]; never a gate)
// ==============================================================================
// Loads the "Kitchen Sink" factory preset (it sets nearly every lane), builds the editor headlessly and renders
// offscreen at 2x: the site images (hero.png, og-image.png, card.png; tests/test_helpers/editor_screenshot.h), and
// for MANUAL.md both control rows, the ring display, the detail strip on five of its lanes and the audition
// footer. The images are documentation, not goldens: nothing asserts on pixels.
//
// Gradus's uidesc carries no uidesc-label: the ring and the detail strip are found by type, the rows by their
// rectangles in the 1100 x 816 template (editor.uidesc: control row 1 :174, row 2 :272, audition footer :635). The
// Markov matrix and speed-curve editors are frame overlays (controller_verify_view.cpp), outside the template root
// that is drawn.
//
// Regenerate: node tools/gen-screenshots.js gradus   (writes plugins/gradus/images/)
// By hand:    gradus_tests.exe "[site]"  with GRADUS_SHOTS_DIR set (default <temp>/gradus-shots)

#include "controller/controller.h"
#include "ui/detail_strip.h"
#include "ui/ring_display.h"
// ActionButton (the Dice buttons) registers its ViewCreator only where its header is included; entry.cpp does that
// in the plugin and is not in gradus_tests.
#include "ui/action_button.h"

#include <editor_lifecycle_harness.h>
#include <editor_screenshot.h>

#include <base/source/updatehandler.h>
#include <vstgui/lib/cframe.h>
#include <vstgui/plugin-bindings/vst3editor.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

namespace Shots = ::Krate::TestSupport::Screenshots;

constexpr std::string_view kPresetPath = "presets/Arp Advanced/Kitchen_Sink.vstpreset";

const VSTGUI::CRect kControlRow1Rect{0, 36, 1100, 68};
const VSTGUI::CRect kControlRow2Rect{0, 68, 1100, 112};
const VSTGUI::CRect kAuditionRect{0, 778, 1100, 812};

/// Detail-strip lanes (DetailStrip::selectLane index) and their image names.
struct LaneShot {
    int lane;
    std::string_view file;
};
constexpr std::array kLaneShots{
    LaneShot{.lane = 0, .file = "lane-velocity.png"},
    LaneShot{.lane = 2, .file = "lane-pitch.png"},
    LaneShot{.lane = 5, .file = "lane-ratchet.png"},
    LaneShot{.lane = 6, .file = "lane-chord.png"},
    LaneShot{.lane = 8, .file = "lane-midi-delay.png"},
};

void shoot(VSTGUI::CViewContainer* root, const VSTGUI::CRect& rect, const std::filesystem::path& file) {
    INFO("shot " << file.filename().string());
    const std::size_t bytes = Shots::writeShot(root, rect, file);
    std::cout << file.filename().string() << ": " << rect.getWidth() << " x " << rect.getHeight() << " at ("
              << rect.left << ", " << rect.top << "), " << bytes << " bytes\n";
    CHECK(bytes > 0u);
}

/// The rectangle of @p target in root coordinates.
[[nodiscard]] VSTGUI::CRect rectOf(VSTGUI::CViewContainer* root, const VSTGUI::CView* target) {
    VSTGUI::CRect rect;
    REQUIRE(Shots::findView(root, [target](VSTGUI::CView* v) { return v == target; }, {0, 0}, rect));
    return rect;
}

}  // namespace

TEST_CASE("Gradus_EditorScreenshots", "[.][screenshots][site]") {
    REQUIRE(Shots::initPlatformForShots());
    const std::filesystem::path dir = Shots::shotsDir("GRADUS_SHOTS_DIR", "gradus-shots");

    auto controller = Steinberg::owned(new Gradus::Controller());
    REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);
    REQUIRE(Shots::loadPresetIntoController(std::filesystem::path(GRADUS_RESOURCES_DIR) / kPresetPath, controller.get()));
    {
        Krate::TestSupport::ensureVstguiInitialized();
        const std::string uidesc = std::string(GRADUS_RESOURCES_DIR) + "/editor.uidesc";
        auto* editor = new VSTGUI::VST3Editor(controller.get(), "editor", uidesc.c_str());
        Steinberg::IPlugView* view = editor;
        REQUIRE(view->attached(nullptr, Krate::TestSupport::nativePlatformType()) == Steinberg::kResultTrue);
        VSTGUI::CFrame* cframe = editor->getFrame();
        REQUIRE(cframe != nullptr);
        VSTGUI::CViewContainer* root = cframe->getView(0) != nullptr ? cframe->getView(0)->asViewContainer() : nullptr;
        REQUIRE(root != nullptr);
        // A headless frame has no window, so CFrame::open never attaches the tree; views draw blank until attached.
        if (!root->isAttached()) {
            root->attached(cframe);
        }
        Steinberg::UpdateHandler::instance()->triggerDeferedUpdates();

        auto* ring = Shots::findFirst<Gradus::RingDisplay>(root);
        auto* strip = Shots::findFirst<Gradus::DetailStrip>(root);
        REQUIRE(ring != nullptr);
        REQUIRE(strip != nullptr);

        CHECK(Shots::writeSiteShots(root, dir) == 2);
        // The card: the ring and the lane strip beside it, whole (a cropped ring reads as a mistake).
        CHECK(Shots::writeCard(root, dir, rectOf(root, ring).unite(rectOf(root, strip)), Shots::CardFit::Contain) > 0u);
        shoot(root, kControlRow1Rect, dir / "control-row-1.png");
        shoot(root, kControlRow2Rect, dir / "control-row-2.png");
        shoot(root, rectOf(root, ring), dir / "ring-display.png");
        shoot(root, kAuditionRect, dir / "audition.png");
        for (const LaneShot& shot : kLaneShots) {
            strip->selectLane(shot.lane);
            Steinberg::UpdateHandler::instance()->triggerDeferedUpdates();
            shoot(root, rectOf(root, strip), dir / shot.file);
        }

        view->removed();
        view->release();
    }
    REQUIRE(controller->terminate() == Steinberg::kResultOk);
    std::cout << kLaneShots.size() + 7 << " shots written to " << dir.string() << '\n';
}
