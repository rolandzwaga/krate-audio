// ==============================================================================
// MultiTap tap pattern editor screenshots (hidden: [.][screenshots][tap-editor]; never a gate)
// ==============================================================================
// Opens the editor headlessly, switches Mode to MultiTap and Timing Pattern to Custom, then renders the MultiTap panel
// (the template root, at 2x) for the tap counts, snap settings and level shapes the tap editor design work reviews.
// Not goldens: nothing asserts on pixels.
//
// By hand: plugin_tests.exe "Iterum_TapEditorScreenshots"  with ITERUM_TAP_SHOTS_DIR set (default <temp>/iterum-tap-shots)

#include <editor_lifecycle_harness.h>

#include "controller/controller.h"
#include "delay_mode.h"
#include "plugin_ids.h"
#include "ui/tap_pattern_editor.h"
// The shared controls the uidesc names register their ViewCreators when their header is included; the plugin does
// that in entry.cpp, which plugin_tests does not compile. Without them the factory builds a plain (black) CViewContainer.
#include "ui/arc_knob.h"
#include "ui/fieldset_container.h"
#include "ui/icon_segment_button.h"
#include "ui/toggle_button.h"

#include <base/source/updatehandler.h>
#include <vstgui/lib/cbitmap.h>
#include <vstgui/lib/cframe.h>
#include <vstgui/lib/coffscreencontext.h>
#include <vstgui/lib/controls/ccontrol.h>
#include <vstgui/uidescription/uiviewswitchcontainer.h>
#include <vstgui/lib/platform/platformfactory.h>
#include <vstgui/plugin-bindings/vst3editor.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#if SMTG_OS_WINDOWS
#include <objbase.h>   // CoInitializeEx: Direct2D offscreen bitmaps are WIC bitmaps, which need COM (a host has it)
#endif

namespace {

[[nodiscard]] std::filesystem::path tapShotsDir() {
    if (const char* dir = std::getenv("ITERUM_TAP_SHOTS_DIR"); dir != nullptr && *dir != '\0') {
        return dir;
    }
    return std::filesystem::temp_directory_path() / "iterum-tap-shots";
}

[[nodiscard]] VSTGUI::CControl* findTaggedControl(VSTGUI::CViewContainer* container, std::int32_t tag) {
    VSTGUI::CControl* found = nullptr;
    container->forEachChild([&](VSTGUI::CView* child) {
        if (found != nullptr) {
            return;
        }
        if (auto* control = dynamic_cast<VSTGUI::CControl*>(child); control != nullptr && control->getTag() == tag) {
            found = control;
        } else if (auto* sub = child->asViewContainer()) {
            found = findTaggedControl(sub, tag);
        }
    });
    return found;
}

[[nodiscard]] VSTGUI::UIViewSwitchContainer* findPanelSwitch(VSTGUI::CViewContainer* container) {
    VSTGUI::UIViewSwitchContainer* found = nullptr;
    container->forEachChild([&](VSTGUI::CView* child) {
        if (found != nullptr) {
            return;
        }
        if (auto* sw = dynamic_cast<VSTGUI::UIViewSwitchContainer*>(child)) {
            found = sw;
        } else if (auto* sub = child->asViewContainer()) {
            found = findPanelSwitch(sub);
        }
    });
    return found;
}

/// Renders @p root at 2x and writes it as a PNG; returns the byte count written (0 on failure).
std::size_t writeTapShot(VSTGUI::CViewContainer* root, const std::filesystem::path& file, double scale = 2.0) {
    const VSTGUI::CRect rect = root->getViewSize();
    auto bitmap = VSTGUI::renderBitmapOffscreen(rect.getSize(), scale, [&](VSTGUI::CDrawContext& ctx) {
        VSTGUI::CDrawContext::Transform shift(ctx, VSTGUI::CGraphicsTransform().translate(-rect.left, -rect.top));
        root->drawRect(&ctx, rect);
    });
    if (bitmap == nullptr || bitmap->getPlatformBitmap() == nullptr) {
        return 0;
    }
    const VSTGUI::PNGBitmapBuffer png =
        VSTGUI::getPlatformFactory().createBitmapMemoryPNGRepresentation(bitmap->getPlatformBitmap());
    if (png.empty()) {
        return 0;
    }
    std::filesystem::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    out.write(reinterpret_cast<const char*>(png.data()), static_cast<std::streamsize>(png.size()));
    return out ? png.size() : 0;
}

constexpr double kSnapStep = 1.0 / 21.0;
constexpr int kSnapQuarter = 14;   // 1/4
constexpr int kSnapEighth = 11;    // 1/8
constexpr int kSnapOff = 0;

[[nodiscard]] double tapCountNormalized(int count) {
    return static_cast<double>(count - 2) / 14.0;
}

}  // namespace

TEST_CASE("Iterum_TapEditorScreenshots", "[.][screenshots][tap-editor]") {
#if SMTG_OS_WINDOWS
    REQUIRE(SUCCEEDED(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)));
#endif
    const std::filesystem::path dir = tapShotsDir();

    auto controller = Steinberg::owned(new Iterum::Controller());
    REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);
    {
        Krate::TestSupport::ensureVstguiInitialized();
        const std::string uidescPath = std::string(ITERUM_RESOURCES_DIR) + "/editor.uidesc";
        auto* editor = new VSTGUI::VST3Editor(controller.get(), "Editor", uidescPath.c_str());
        Steinberg::IPlugView* view = editor;
        REQUIRE(view->attached(nullptr, Krate::TestSupport::nativePlatformType()) == Steinberg::kResultTrue);

        VSTGUI::CFrame* frame = editor->getFrame();
        REQUIRE(frame != nullptr);
        VSTGUI::CViewContainer* root = frame->getView(0) != nullptr ? frame->getView(0)->asViewContainer() : nullptr;
        REQUIRE(root != nullptr);
        if (!root->isAttached()) {
            root->attached(frame);
        }
        Steinberg::UpdateHandler::instance()->triggerDeferedUpdates();

        VSTGUI::UIViewSwitchContainer* panels = findPanelSwitch(frame);
        REQUIRE(panels != nullptr);
        panels->setAnimationTime(0);

        // Mode = MultiTap
        VSTGUI::CControl* modeControl = findTaggedControl(frame, Iterum::kModeId);
        REQUIRE(modeControl != nullptr);
        modeControl->setValueNormalized(static_cast<float>(static_cast<int>(Iterum::DelayMode::MultiTap)) / 9.0f);
        modeControl->valueChanged();
        Steinberg::UpdateHandler::instance()->triggerDeferedUpdates();

        auto setParam = [&](Steinberg::Vst::ParamID id, double value) {
            controller->setParamNormalized(id, value);
            controller->performEdit(id, value);
            Steinberg::UpdateHandler::instance()->triggerDeferedUpdates();
        };

        // Timing Pattern = Custom (the control path, so the visibility controllers show the CUSTOM PATTERN fieldset)
        VSTGUI::CControl* patternControl = findTaggedControl(frame, Iterum::kMultiTapTimingPatternId);
        REQUIRE(patternControl != nullptr);
        controller->setParamNormalized(Iterum::kMultiTapTimingPatternId, 1.0);
        patternControl->setValueNormalized(1.0f);
        patternControl->valueChanged();
        Steinberg::UpdateHandler::instance()->triggerDeferedUpdates();

        auto* tapEditor = dynamic_cast<Iterum::TapPatternEditor*>(findTaggedControl(frame, Iterum::kMultiTapPatternEditorTagId));
        REQUIRE(tapEditor != nullptr);

        auto shot = [&](const char* name) {
            const std::size_t bytes = writeTapShot(root, dir / name);
            std::cout << name << ": " << bytes << " bytes\n";
            CHECK(bytes > 0u);
        };
        // A set the way the processor's Tempo output reaches the controller: setParamNormalized alone.
        auto setTempo = [&](double bpm) {
            controller->setParamNormalized(Iterum::kMultiTapTempoId, (bpm - 20.0) / 280.0);
            Steinberg::UpdateHandler::instance()->triggerDeferedUpdates();
        };
        auto setLength = [&](int index) { setParam(Iterum::kMultiTapCustomLengthId, static_cast<double>(index) / 5.0); };

        // custom-4taps: the fresh instance (Length 1/1, Snap 1/4): the taps on beats 1-4, identical to Reset
        setTempo(120.0);
        setParam(Iterum::kMultiTapTapCountId, tapCountNormalized(4));
        setParam(Iterum::kMultiTapSnapDivisionId, kSnapQuarter * kSnapStep);
        shot("custom-4taps.png");

        // custom-8taps: 4 -> 8 one step at a time through the user path (setParamNormalized + performEdit, as the
        // slider drag does): every eighth, nothing written
        for (int n = 5; n <= 8; ++n) {
            setParam(Iterum::kMultiTapTapCountId, tapCountNormalized(n));
        }
        shot("custom-8taps.png");

        // custom-16taps: on to 16 the same way: every sixteenth
        for (int n = 9; n <= 16; ++n) {
            setParam(Iterum::kMultiTapTapCountId, tapCountNormalized(n));
        }
        shot("custom-16taps.png");

        // back to 8 (a decrease writes nothing), then the Snap shots
        setParam(Iterum::kMultiTapTapCountId, tapCountNormalized(8));
        setParam(Iterum::kMultiTapSnapDivisionId, kSnapEighth * kSnapStep);
        shot("custom-8taps-snap8.png");
        setParam(Iterum::kMultiTapSnapDivisionId, kSnapOff * kSnapStep);
        shot("custom-8taps-snap-off.png");  // the beat and bar grid must stay readable with Snap Off

        // custom-8taps-levels: Snap 1/4, the eight levels through the level parameters only (one at -46 dB, one silent)
        setParam(Iterum::kMultiTapSnapDivisionId, kSnapQuarter * kSnapStep);
        const std::array<float, 8> levels{1.0f, 0.7f, 0.5f, 0.25f, 0.1f, 0.05f, 0.00501f, 0.0f};
        for (std::size_t i = 0; i < levels.size(); ++i) {
            setParam(Iterum::kMultiTapCustomLevel0Id + static_cast<Steinberg::Vst::ParamID>(i),
                     static_cast<double>(levels[i]));
        }
        shot("custom-8taps-levels.png");

        // custom-8taps-free: Length Free, the legacy 5 s axis with the tempo's beat grid
        setLength(0);
        shot("custom-8taps-free.png");

        // custom-8taps-2bars-60bpm: Length 2/1 at 60 BPM after Reset: the over-ceiling region and eight taps dividing
        // the playable 5 s
        setLength(5);
        setTempo(60.0);
        controller->resetPatternToDefault();
        Steinberg::UpdateHandler::instance()->triggerDeferedUpdates();
        shot("custom-8taps-2bars-60bpm.png");

        // custom-length8-snap4: Length 1/8 with Snap 1/4: the 1/16 fallback lines
        setTempo(120.0);
        setLength(1);
        shot("custom-length8-snap4.png");

        // The read-only display of the other patterns, on the Length Copy would choose
        setLength(4);
        setParam(Iterum::kMultiTapTimingPatternId, 2.0 / 19.0);  // Quarter
        setParam(Iterum::kMultiTapTapCountId, tapCountNormalized(8));
        shot("preset-quarter-8taps.png");  // on 2/1; the review checks the title line under the Copy row
        setParam(Iterum::kMultiTapTapCountId, tapCountNormalized(16));
        shot("preset-quarter-16taps.png");  // on Free, taps 10-16 stacked at 5 s
        setParam(Iterum::kMultiTapTimingPatternId, 14.0 / 19.0);  // Golden Ratio
        setParam(Iterum::kMultiTapNoteValueId, 2.0 / 9.0);        // 1/4
        setParam(Iterum::kMultiTapTapCountId, tapCountNormalized(8));
        shot("preset-golden-8taps.png");

        view->removed();
        view->release();
    }
    REQUIRE(controller->terminate() == Steinberg::kResultOk);
    std::cout << "tap editor shots written to " << dir.string() << '\n';
}
