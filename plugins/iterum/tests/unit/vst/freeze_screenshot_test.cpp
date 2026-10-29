// ==============================================================================
// Freeze panel screenshots (hidden: [.][screenshots][freeze]; never a gate)
// ==============================================================================
// Opens the editor headlessly, switches Mode to Freeze and renders the panel once per Pattern Type, every other
// parameter at its registered default. Not goldens: nothing asserts on pixels.
//
// By hand: plugin_tests.exe "Iterum_FreezeScreenshots"  with ITERUM_FREEZE_SHOTS_DIR set (default <temp>/iterum-freeze-shots)

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

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#if SMTG_OS_WINDOWS
#include <objbase.h>   // CoInitializeEx: Direct2D offscreen bitmaps are WIC bitmaps, which need COM (a host has it)
#endif

namespace {

[[nodiscard]] std::filesystem::path freezeShotsDir() {
    if (const char* dir = std::getenv("ITERUM_FREEZE_SHOTS_DIR"); dir != nullptr && *dir != '\0') {
        return dir;
    }
    return std::filesystem::temp_directory_path() / "iterum-freeze-shots";
}

[[nodiscard]] VSTGUI::CControl* findFreezeTaggedControl(VSTGUI::CViewContainer* container, std::int32_t tag) {
    VSTGUI::CControl* found = nullptr;
    container->forEachChild([&](VSTGUI::CView* child) {
        if (found != nullptr) {
            return;
        }
        if (auto* control = dynamic_cast<VSTGUI::CControl*>(child); control != nullptr && control->getTag() == tag) {
            found = control;
        } else if (auto* sub = child->asViewContainer()) {
            found = findFreezeTaggedControl(sub, tag);
        }
    });
    return found;
}

[[nodiscard]] VSTGUI::UIViewSwitchContainer* findFreezePanelSwitch(VSTGUI::CViewContainer* container) {
    VSTGUI::UIViewSwitchContainer* found = nullptr;
    container->forEachChild([&](VSTGUI::CView* child) {
        if (found != nullptr) {
            return;
        }
        if (auto* sw = dynamic_cast<VSTGUI::UIViewSwitchContainer*>(child)) {
            found = sw;
        } else if (auto* sub = child->asViewContainer()) {
            found = findFreezePanelSwitch(sub);
        }
    });
    return found;
}

/// Renders @p root at 2x and writes it as a PNG; returns the byte count written (0 on failure).
std::size_t writeFreezeShot(VSTGUI::CViewContainer* root, const std::filesystem::path& file, double scale = 2.0) {
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

TEST_CASE("Iterum_FreezeScreenshots", "[.][screenshots][freeze]") {
#if SMTG_OS_WINDOWS
    REQUIRE(SUCCEEDED(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)));
#endif
    const std::filesystem::path dir = freezeShotsDir();

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

        VSTGUI::UIViewSwitchContainer* panels = findFreezePanelSwitch(frame);
        REQUIRE(panels != nullptr);
        panels->setAnimationTime(0);

        // Mode = Freeze
        VSTGUI::CControl* modeControl = findFreezeTaggedControl(frame, Iterum::kModeId);
        REQUIRE(modeControl != nullptr);
        modeControl->setValueNormalized(static_cast<float>(static_cast<int>(Iterum::DelayMode::Freeze)) / 9.0f);
        modeControl->valueChanged();
        Steinberg::UpdateHandler::instance()->triggerDeferedUpdates();

        // Pattern Type through the control path, so the visibility controllers show the matching variant fieldset
        VSTGUI::CControl* patternControl = findFreezeTaggedControl(frame, Iterum::kFreezePatternTypeId);
        REQUIRE(patternControl != nullptr);

        const char* const files[] = {"freeze-euclidean.png", "freeze-granular.png", "freeze-drones.png",
                                     "freeze-noise.png"};
        for (int i = 0; i < 4; ++i) {
            const double normalized = static_cast<double>(i) / 3.0;
            controller->setParamNormalized(Iterum::kFreezePatternTypeId, normalized);
            patternControl->setValueNormalized(static_cast<float>(normalized));
            patternControl->valueChanged();
            Steinberg::UpdateHandler::instance()->triggerDeferedUpdates();

            const std::size_t bytes = writeFreezeShot(root, dir / files[i]);
            std::cout << files[i] << ": " << bytes << " bytes\n";
            CHECK(bytes > 0u);
        }

        view->removed();
        view->release();
    }
    REQUIRE(controller->terminate() == Steinberg::kResultOk);
    std::cout << "freeze shots written to " << dir.string() << '\n';
}
