// ==============================================================================
// Editor screenshots for the site and the manual (hidden: [.][screenshots][site]; never a gate)
// ==============================================================================
// Loads the "Crystal_Choir" factory preset into the controller, builds the editor headlessly and renders offscreen at
// 2x: the site images (hero.png, og-image.png, card.png; tests/test_helpers/editor_screenshot.h), and for MANUAL.md
// every tab, every panel group (found by its fieldset title), each of the ten modulation sources, each delay type,
// the step lanes and the settings drawer. The images are documentation, not goldens: nothing asserts on pixels.
//
// Tabs, modulation sources, delay types and the modulation effect are switched through their parameters, as the
// editor's own controls do; every UIViewSwitchContainer is made instant first, since no timer runs here to finish a
// fade. The settings drawer slides in on a timer, so it is moved into view by hand for its shot. Playheads and the
// voice routes come from a running processor and are absent here.
//
// Regenerate: node tools/gen-screenshots.js ruinae   (writes plugins/ruinae/images/)
// By hand:    ruinae_tests.exe "[site]"  with RUINAE_SHOTS_DIR set (default <temp>/ruinae-shots)

#include "controller/controller.h"
#include "plugin_ids.h"
// The shared controls the uidesc names register their ViewCreators when their header is included; entry.cpp, which
// does that in the plugin, is not in ruinae_tests.
#include "ui/action_button.h"
#include "ui/arc_knob.h"
#include "ui/arp_lane_container.h"
#include "ui/bipolar_slider.h"
#include "ui/fieldset_container.h"
#include "ui/icon_segment_button.h"
#include "ui/lfo_shape_selector.h"
#include "ui/mod_heatmap.h"
#include "ui/oscillator_type_selector.h"
#include "ui/toggle_button.h"

#include <editor_lifecycle_harness.h>
#include <editor_screenshot.h>

#include <base/source/updatehandler.h>
#include <vstgui/lib/cframe.h>
#include <vstgui/plugin-bindings/vst3editor.h>
#include <vstgui/uidescription/uiviewswitchcontainer.h>

#include <array>
#include <cstdint>
#include <catch2/catch_test_macros.hpp>
#include <filesystem>
#include <iostream>
#include <string>
#include <string_view>

namespace {

namespace Shots = ::Krate::TestSupport::Screenshots;

constexpr std::string_view kPresetPath = "Pads/Crystal_Choir.vstpreset";

enum class Tab : std::uint8_t { Sound, Mod, Fx, Seq };   // the Main Tab list order

const VSTGUI::CRect kTopBarRect{0, 0, 1400, 40};
const VSTGUI::CRect kTabAreaRect{0, 40, 1400, 571};   // the SOUND tab's panels (editor.uidesc Tab_Sound :3735)
const VSTGUI::CRect kGlobalFilterRect{8, 4, 1392, 48};  // within the FX tab (:2520)
const VSTGUI::CRect kDrawerOpenRect{1180, 0, 1400, 800};

/// A fieldset (by title) on tab @p tab.
struct GroupShot {
    std::string_view file;
    std::string_view title;
    Tab tab;
};

constexpr std::array kGroupShots{
    GroupShot{.file = "osc-a.png", .title = "OSC A", .tab = Tab::Sound},
    GroupShot{.file = "spectral-morph.png", .title = "SPECTRAL MORPH", .tab = Tab::Sound},
    GroupShot{.file = "osc-b.png", .title = "OSC B", .tab = Tab::Sound},
    GroupShot{.file = "master.png", .title = "MASTER", .tab = Tab::Sound},
    GroupShot{.file = "filter.png", .title = "FILTER", .tab = Tab::Sound},
    GroupShot{.file = "distortion.png", .title = "DISTORTION", .tab = Tab::Sound},
    GroupShot{.file = "envelopes.png", .title = "ENVELOPES", .tab = Tab::Sound},
    GroupShot{.file = "mod-matrix.png", .title = "MODULATION CONFIGURATION", .tab = Tab::Mod},
    GroupShot{.file = "harmonizer.png", .title = "HARMONIZER", .tab = Tab::Fx},
    GroupShot{.file = "reverb.png", .title = "REVERB", .tab = Tab::Fx},
    GroupShot{.file = "trance-gate.png", .title = "TRANCE GATE", .tab = Tab::Seq},
    GroupShot{.file = "arpeggiator.png", .title = "ARPEGGIATOR", .tab = Tab::Seq},
};

/// The Mod Source View list (controller.cpp, kModSourceViewModeTag), in order.
constexpr std::array<std::string_view, 10> kModSourceShots{
    "mod-lfo.png", "mod-lfo-2.png", "mod-chaos.png", "mod-macros.png", "mod-rungler.png",
    "mod-env-follower.png", "mod-sample-hold.png", "mod-random.png", "mod-pitch-follower.png", "mod-transient.png",
};

/// The Modulation Type list (modulation_effect_params.h) after None: Phaser, Flanger, Chorus.
constexpr std::array<std::string_view, 3> kModulationShots{
    "modulation-phaser.png", "modulation-flanger.png", "modulation-chorus.png",
};

/// The Delay Type list (delay_params.h): Digital, Tape, Ping Pong, Granular, Spectral.
constexpr std::array<std::string_view, 5> kDelayShots{
    "delay-digital.png", "delay-tape.png", "delay-ping-pong.png", "delay-granular.png", "delay-spectral.png",
};

/// Makes every view switch in the tree instant (a fade needs a timer that never runs here). Called after each
/// switch, since a newly built tab brings switch containers of its own.
void instantSwitches(VSTGUI::CViewContainer* container) {
    container->forEachChild([](VSTGUI::CView* child) {
        if (auto* sw = dynamic_cast<VSTGUI::UIViewSwitchContainer*>(child)) {
            sw->setAnimationTime(0);
        }
        if (auto* sub = child->asViewContainer()) {
            instantSwitches(sub);
        }
    });
}

/// Sets a list parameter to entry @p index of @p count, as its control would, and settles the views.
void setList(Ruinae::Controller& controller, VSTGUI::CViewContainer* root, Steinberg::Vst::ParamID id, int index,
             int count) {
    instantSwitches(root);
    controller.setParamNormalized(id, count > 1 ? static_cast<double>(index) / (count - 1) : 0.0);
    Steinberg::UpdateHandler::instance()->triggerDeferedUpdates();
    instantSwitches(root);
}

void setTab(Ruinae::Controller& controller, VSTGUI::CViewContainer* root, Tab tab) {
    setList(controller, root, Ruinae::kMainTabTag, static_cast<int>(tab), 4);
}

void shoot(VSTGUI::CViewContainer* root, const VSTGUI::CRect& rect, const std::filesystem::path& file) {
    INFO("shot " << file.filename().string());
    const std::size_t bytes = Shots::writeShot(root, rect, file);
    std::cout << file.filename().string() << ": " << rect.getWidth() << " x " << rect.getHeight() << " at ("
              << rect.left << ", " << rect.top << "), " << bytes << " bytes\n";
    CHECK(bytes > 0u);
}

[[nodiscard]] VSTGUI::CRect fieldset(VSTGUI::CViewContainer* root, std::string_view title) {
    VSTGUI::CRect rect;
    INFO("fieldset " << title);
    REQUIRE(Shots::findView(
        root,
        [title](VSTGUI::CView* v) {
            auto* f = dynamic_cast<Krate::Plugins::FieldsetContainer*>(v);
            return f != nullptr && v->isVisible() && f->getTitle() == title;
        },
        {0, 0}, rect));
    return rect;
}

template <typename T>
[[nodiscard]] VSTGUI::CRect rectOfFirst(VSTGUI::CViewContainer* root) {
    VSTGUI::CRect rect;
    REQUIRE(Shots::findView(root, [](VSTGUI::CView* v) { return dynamic_cast<T*>(v) != nullptr; }, {0, 0}, rect));
    return rect;
}

}  // namespace

TEST_CASE("Ruinae_EditorScreenshots", "[.][screenshots][site]") {
    REQUIRE(Shots::initPlatformForShots());
    const std::filesystem::path dir = Shots::shotsDir("RUINAE_SHOTS_DIR", "ruinae-shots");

    auto controller = Steinberg::owned(new Ruinae::Controller());
    REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);
    const std::filesystem::path preset = std::filesystem::path(RUINAE_RESOURCES_DIR) / "presets" / kPresetPath;
    REQUIRE(Shots::loadPresetIntoController(preset, controller.get()));
    {
        Krate::TestSupport::ensureVstguiInitialized();
        const std::string uidesc = std::string(RUINAE_RESOURCES_DIR) + "/editor.uidesc";
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

        // SOUND
        setTab(*controller, root, Tab::Sound);
        CHECK(Shots::writeSiteShots(root, dir) == 2);
        shoot(root, kTopBarRect, dir / "top-bar.png");
        shoot(root, kTabAreaRect, dir / "sound-tab.png");

        for (const GroupShot& shot : kGroupShots) {
            setTab(*controller, root, shot.tab);
            shoot(root, fieldset(root, shot.title), dir / shot.file);
        }

        // MOD: the whole tab, each modulation source, the heatmap
        setTab(*controller, root, Tab::Mod);
        shoot(root, root->getViewSize(), dir / "mod-tab.png");
        // The card: LFO 1's second knob row over its whole waveform (the SOUND tab at card size is mostly the empty
        // Clean distortion box).
        CHECK(Shots::writeCard(root, dir, VSTGUI::CRect(8, 165, 560, 462)) > 0u);
        for (std::size_t i = 0; i < kModSourceShots.size(); ++i) {
            setList(*controller, root, Ruinae::kModSourceViewModeTag, static_cast<int>(i),
                    static_cast<int>(kModSourceShots.size()));
            shoot(root, fieldset(root, "MODULATION SOURCES"), dir / kModSourceShots[i]);
        }
        setList(*controller, root, Ruinae::kModSourceViewModeTag, 0, static_cast<int>(kModSourceShots.size()));
        shoot(root, rectOfFirst<Krate::Plugins::ModHeatmap>(root), dir / "mod-heatmap.png");

        // FX: the whole tab, the global filter strip, each modulation effect, each delay type
        setTab(*controller, root, Tab::Fx);
        shoot(root, root->getViewSize(), dir / "fx-tab.png");
        VSTGUI::CRect filterStrip = kGlobalFilterRect;
        filterStrip.offset(0, kTabAreaRect.top);
        shoot(root, filterStrip, dir / "global-filter.png");
        for (std::size_t i = 0; i < kModulationShots.size(); ++i) {
            // index 0 of the list is None
            setList(*controller, root, Ruinae::kModulationTypeId, static_cast<int>(i) + 1,
                    static_cast<int>(kModulationShots.size()) + 1);
            shoot(root, fieldset(root, "MODULATION"), dir / kModulationShots[i]);
        }
        for (std::size_t i = 0; i < kDelayShots.size(); ++i) {
            setList(*controller, root, Ruinae::kDelayTypeId, static_cast<int>(i), static_cast<int>(kDelayShots.size()));
            shoot(root, fieldset(root, "DELAY"), dir / kDelayShots[i]);
        }

        // SEQ: the whole tab and the step lanes
        setTab(*controller, root, Tab::Seq);
        shoot(root, root->getViewSize(), dir / "seq-tab.png");
        shoot(root, rectOfFirst<Krate::Plugins::ArpLaneContainer>(root), dir / "arp-step-lanes.png");

        // The settings drawer, moved to where its slide animation ends.
        VSTGUI::CView* drawer = nullptr;
        root->forEachChild([&](VSTGUI::CView* child) {
            const VSTGUI::CRect r = child->getViewSize();
            if (drawer == nullptr && r.left >= 1400 && r.getWidth() == 220 && r.getHeight() == 800) {
                drawer = child;
            }
        });
        REQUIRE(drawer != nullptr);
        const VSTGUI::CRect parked = drawer->getViewSize();
        drawer->setViewSize(kDrawerOpenRect);
        drawer->setMouseableArea(kDrawerOpenRect);
        shoot(root, kDrawerOpenRect, dir / "settings-drawer.png");
        drawer->setViewSize(parked);
        drawer->setMouseableArea(parked);

        view->removed();
        view->release();
    }
    REQUIRE(controller->terminate() == Steinberg::kResultOk);
    std::cout << kGroupShots.size() + kModSourceShots.size() + kModulationShots.size() + kDelayShots.size() + 11
              << " shots written to " << dir.string() << '\n';
}
