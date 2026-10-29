// ==============================================================================
// Editor screenshots, one per delay mode (hidden: [.][screenshots]; never a gate)
// ==============================================================================
// Builds the editor headlessly, switches the Mode control to each delay mode and renders the whole editor offscreen
// at 2x into PNG files. The images are review material for the UI consistency workflow
// (.claude/workflows/iterum-ui-consistency.js), not goldens: nothing asserts on pixels.
//
// Regenerate: node tools/gen-screenshots.js iterum   (writes plugins/iterum/images/)
// By hand:    plugin_tests.exe "[screenshots]"  with ITERUM_SHOTS_DIR set (default <temp>/iterum-shots)
//
// Every mode is shown at its default parameter values; state-dependent groups (e.g. the Freeze pattern fieldsets)
// show whichever variant those defaults select.

#include <editor_lifecycle_harness.h>

#include "controller/controller.h"
#include "delay_mode.h"
#include "plugin_ids.h"
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
#include <string_view>

#if SMTG_OS_WINDOWS
#include <objbase.h>   // CoInitializeEx: Direct2D offscreen bitmaps are WIC bitmaps, which need COM (a host has it)
#endif

namespace {

/// File stem per DelayMode, in enum order.
constexpr std::array<std::string_view, static_cast<int>(Iterum::DelayMode::NumModes)> kModeNames{
    "granular", "spectral", "shimmer", "tape", "bbd", "digital", "pingpong", "reverse", "multitap", "freeze"};

[[nodiscard]] std::filesystem::path shotsDir() {
    if (const char* dir = std::getenv("ITERUM_SHOTS_DIR"); dir != nullptr && *dir != '\0') {
        return dir;
    }
    return std::filesystem::temp_directory_path() / "iterum-shots";
}

[[nodiscard]] VSTGUI::CControl* findTagged(VSTGUI::CViewContainer* container, std::int32_t tag) {
    VSTGUI::CControl* found = nullptr;
    container->forEachChild([&](VSTGUI::CView* child) {
        if (found != nullptr) {
            return;
        }
        if (auto* control = dynamic_cast<VSTGUI::CControl*>(child); control != nullptr && control->getTag() == tag) {
            found = control;
        } else if (auto* sub = child->asViewContainer()) {
            found = findTagged(sub, tag);
        }
    });
    return found;
}

[[nodiscard]] VSTGUI::UIViewSwitchContainer* findSwitchContainer(VSTGUI::CViewContainer* container) {
    VSTGUI::UIViewSwitchContainer* found = nullptr;
    container->forEachChild([&](VSTGUI::CView* child) {
        if (found != nullptr) {
            return;
        }
        if (auto* sw = dynamic_cast<VSTGUI::UIViewSwitchContainer*>(child)) {
            found = sw;
        } else if (auto* sub = child->asViewContainer()) {
            found = findSwitchContainer(sub);
        }
    });
    return found;
}

/// Renders the whole of @p root (the editor template container, at the frame origin) at @p scale and writes it as a
/// PNG; returns the byte count written (0 on failure). The root is drawn, not the frame: a headless frame has no
/// window and reads 0 x 0.
std::size_t writeShot(VSTGUI::CViewContainer* root, const std::filesystem::path& file, double scale = 2.0) {
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

}  // namespace

TEST_CASE("Iterum_EditorScreenshots", "[.][screenshots]") {
#if SMTG_OS_WINDOWS
    REQUIRE(SUCCEEDED(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)));
#endif
    const std::filesystem::path dir = shotsDir();

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
        // The template root: the frame's first child (a preset browser overlay, if any, comes after it).
        VSTGUI::CViewContainer* root = frame->getView(0) != nullptr ? frame->getView(0)->asViewContainer() : nullptr;
        REQUIRE(root != nullptr);
        // A headless frame has no window, so CFrame::open never attaches the tree; segment buttons lay out their
        // segments only once attached, and other views draw blank too.
        if (!root->isAttached()) {
            root->attached(frame);
        }
        Steinberg::UpdateHandler::instance()->triggerDeferedUpdates();

        // The mode panels swap with an animation whenever the container is attached; no timer runs here, so an
        // animated swap would only complete at the next switch and every shot would show the previous mode.
        VSTGUI::UIViewSwitchContainer* panels = findSwitchContainer(frame);
        REQUIRE(panels != nullptr);
        panels->setAnimationTime(0);

        VSTGUI::CControl* modeControl = findTagged(frame, Iterum::kModeId);
        REQUIRE(modeControl != nullptr);
        constexpr float kLastMode = static_cast<float>(static_cast<int>(Iterum::DelayMode::NumModes) - 1);

        for (std::size_t mode = 0; mode < kModeNames.size(); ++mode) {
            const std::string file = "editor-" + std::string(kModeNames[mode]) + ".png";
            INFO("shot " << file);
            // valueChanged() notifies the UIViewSwitchContainer's controller, which swaps in the mode's panel; the
            // deferred updates then let the visibility controllers settle on the new panel's views.
            modeControl->setValueNormalized(static_cast<float>(mode) / kLastMode);
            modeControl->valueChanged();
            Steinberg::UpdateHandler::instance()->triggerDeferedUpdates();

            const std::size_t bytes = writeShot(root, dir / file);
            std::cout << file << ": " << bytes << " bytes\n";
            CHECK(bytes > 0u);
        }

        view->removed();
        view->release();
    }
    REQUIRE(controller->terminate() == Steinberg::kResultOk);
    // No CoUninitialize: VSTGUI's platform factory keeps its WIC factory until VSTGUI::exit at process exit, and
    // releasing it after COM is torn down crashes.
    std::cout << kModeNames.size() << " shots written to " << dir.string() << '\n';
}
