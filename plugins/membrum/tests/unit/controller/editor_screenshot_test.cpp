// ==============================================================================
// Editor screenshots for the site and the manual (hidden: [.][screenshots][site]; never a gate)
// ==============================================================================
// Loads the "Acoustic Studio Kit" factory kit into the controller, builds the editor headlessly and renders
// offscreen at 2x: the site images (hero.png, og-image.png, card.png; tests/test_helpers/editor_screenshot.h), and
// for MANUAL.md the pad grid, the selected-pad panel in Acoustic (Simple) and Extended (Advanced) mode, and the
// panel groups, each found by its fieldset title. The images are documentation, not goldens: nothing asserts on
// pixels. The meters and pad glow stay at rest: no processor feeds them here.
//
// Regenerate: node tools/gen-screenshots.js membrum   (writes plugins/membrum/images/)
// By hand:    membrum_tests.exe "[site]"  with MEMBRUM_SHOTS_DIR set (default <temp>/membrum-shots)

#include "controller/controller.h"
#include "plugin_ids.h"
// The shared controls the uidesc names register their ViewCreators when their header is included; entry.cpp, which
// does that in the plugin, is not in membrum_tests. Without them knobs and fieldsets build as black containers.
#include "ui/arc_knob.h"
#include "ui/fieldset_container.h"
#include "ui/toggle_button.h"

#include <editor_lifecycle_harness.h>
#include <editor_screenshot.h>

#include <base/source/updatehandler.h>
#include <vstgui/lib/cframe.h>
#include <vstgui/plugin-bindings/vst3editor.h>
#include <vstgui/uidescription/uiviewswitchcontainer.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

namespace Shots = ::Krate::TestSupport::Screenshots;

constexpr std::string_view kKitPath = "Kit Presets/Acoustic/Acoustic Studio Kit.vstpreset";

const VSTGUI::CRect kPadGridRect{0, 0, 640, 800};       // the left column (editor.uidesc :351)
const VSTGUI::CRect kSelectedPadRect{640, 0, 1040, 800};  // the middle column, both modes (:354)

/// A fieldset (by title) shown in UI mode @p mode (0 Acoustic / Simple, 1 Extended / Advanced; the right column
/// exists in both).
struct GroupShot {
    std::string_view file;
    std::string_view title;
    int mode;
};

constexpr std::array kGroupShots{
    GroupShot{.file = "macros.png", .title = "Macros", .mode = 0},
    GroupShot{.file = "exciter.png", .title = "Source", .mode = 1},
    GroupShot{.file = "body-physics.png", .title = "Physics", .mode = 1},
    GroupShot{.file = "tone-shaper.png", .title = "Tone Shaper", .mode = 1},
    GroupShot{.file = "parallel-layers.png", .title = "Parallel Layers", .mode = 1},
    GroupShot{.file = "pitch-envelope.png", .title = "Pitch Envelope", .mode = 1},
    GroupShot{.file = "unnatural-zone.png", .title = "Character", .mode = 1},
    GroupShot{.file = "material-morph.png", .title = "Material Morph", .mode = 1},
    GroupShot{.file = "routing.png", .title = "Routing", .mode = 1},
    GroupShot{.file = "master.png", .title = "Master", .mode = 0},
    GroupShot{.file = "kit-browser.png", .title = "Kit Browser", .mode = 0},
    GroupShot{.file = "pad-browser.png", .title = "Pad Browser", .mode = 0},
    GroupShot{.file = "voices.png", .title = "Voices", .mode = 0},
    GroupShot{.file = "coupling.png", .title = "Coupling", .mode = 0},
    GroupShot{.file = "meter.png", .title = "Meter", .mode = 0},
};

void shoot(VSTGUI::CViewContainer* root, const VSTGUI::CRect& rect, const std::filesystem::path& file) {
    INFO("shot " << file.filename().string());
    const std::size_t bytes = Shots::writeShot(root, rect, file);
    std::cout << file.filename().string() << ": " << rect.getWidth() << " x " << rect.getHeight() << " at ("
              << rect.left << ", " << rect.top << "), " << bytes << " bytes\n";
    CHECK(bytes > 0u);
}

/// Switches the selected-pad panel through the UiMode parameter, as the editor's mode button does. The switch
/// container rebuilds its template, so every view pointer into the panel is stale afterwards.
void setMode(Membrum::Controller& controller, VSTGUI::CViewContainer* root, int mode) {
    controller.setParamNormalized(Membrum::kUiModeId, static_cast<double>(mode));
    Steinberg::UpdateHandler::instance()->triggerDeferedUpdates();
    auto* panel = Shots::findFirst<VSTGUI::UIViewSwitchContainer>(root);
    REQUIRE(panel != nullptr);
    REQUIRE(panel->getCurrentViewIndex() == mode);
}

}  // namespace

TEST_CASE("Membrum_EditorScreenshots", "[.][screenshots][site]") {
    REQUIRE(Shots::initPlatformForShots());
    const std::filesystem::path dir = Shots::shotsDir("MEMBRUM_SHOTS_DIR", "membrum-shots");

    auto controller = Steinberg::owned(new Membrum::Controller());
    REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);
    const std::filesystem::path kit = std::filesystem::path(MEMBRUM_RESOURCES_DIR) / "presets" / kKitPath;
    REQUIRE(Shots::loadPresetIntoController(kit, controller.get()));
    {
        Krate::TestSupport::ensureVstguiInitialized();
        const std::string uidesc = std::string(MEMBRUM_RESOURCES_DIR) + "/editor.uidesc";
        auto* editor = new VSTGUI::VST3Editor(controller.get(), "EditorDefault", uidesc.c_str());
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
        // The mode swap fades over 120 ms once attached; no timer runs here, so an animated swap would never finish.
        auto* panel = Shots::findFirst<VSTGUI::UIViewSwitchContainer>(root);
        REQUIRE(panel != nullptr);
        panel->setAnimationTime(0);

        setMode(*controller, root, 0);
        CHECK(Shots::writeSiteShots(root, dir) == 3);
        shoot(root, kPadGridRect, dir / "pad-grid.png");
        shoot(root, kSelectedPadRect, dir / "selected-pad-acoustic.png");
        setMode(*controller, root, 1);
        shoot(root, kSelectedPadRect, dir / "selected-pad-extended.png");

        for (const GroupShot& shot : kGroupShots) {
            setMode(*controller, root, shot.mode);
            VSTGUI::CRect rect;
            const auto byTitle = [title = shot.title](VSTGUI::CView* v) {
                auto* fieldset = dynamic_cast<Krate::Plugins::FieldsetContainer*>(v);
                return fieldset != nullptr && fieldset->getTitle() == title;
            };
            INFO("fieldset " << shot.title);
            REQUIRE(Shots::findView(root, byTitle, {0, 0}, rect));
            shoot(root, rect, dir / shot.file);
        }

        view->removed();
        view->release();
    }
    REQUIRE(controller->terminate() == Steinberg::kResultOk);
    std::cout << kGroupShots.size() + 6 << " shots written to " << dir.string() << '\n';
}
