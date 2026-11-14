// ==============================================================================
// Editor screenshots for layout review (hidden: [.][screenshots]; never a gate)
// ==============================================================================
// Opens the editor headlessly with Sweep enabled and renders it offscreen at 2x into PNG files: the whole editor
// (default height, mod panel closed), the sweep row, the header bar, the band panels' title rows, and the editor
// with Band 1's expanded strip open. The level meters are fed one fixed window first (Output L clipped). Not goldens:
// nothing asserts on pixels. Every parameter is at its registered default except Sweep Enable and Band 1 Expanded.
//
// By hand: disrumpo_tests.exe "Disrumpo_EditorScreenshots"  with DISRUMPO_SHOTS_DIR set (default <temp>/disrumpo-shots)

#include "../../test_support/headless_editor.h"

#include "controller/controller.h"
#include "controller/spectrum_block.h"
#include "controller/visibility_controllers.h"
#include "plugin_ids.h"
// The shared controls the uidesc names register their ViewCreators when their header is included; the plugin does
// that in entry.cpp, which disrumpo_tests does not compile. Without them the factory builds a plain CViewContainer.
#include "ui/arc_knob.h"
#include "ui/toggle_button.h"

#include <base/source/updatehandler.h>
#include <vstgui/lib/cbitmap.h>
#include <vstgui/lib/cframe.h>
#include <vstgui/lib/coffscreencontext.h>
#include <vstgui/lib/controls/ccontrol.h>
#include <vstgui/lib/platform/platformfactory.h>

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <string>

#if SMTG_OS_WINDOWS
#include <objbase.h>   // CoInitializeEx: Direct2D offscreen bitmaps are WIC bitmaps, which need COM (a host has it)
#endif

namespace {

[[nodiscard]] std::filesystem::path disrumpoShotsDir() {
    if (const char* dir = std::getenv("DISRUMPO_SHOTS_DIR"); dir != nullptr && *dir != '\0') {
        return dir;
    }
    return std::filesystem::temp_directory_path() / "disrumpo-shots";
}

[[nodiscard]] VSTGUI::CControl* findDisrumpoTaggedControl(VSTGUI::CViewContainer* container, std::int32_t tag) {
    VSTGUI::CControl* found = nullptr;
    container->forEachChild([&](VSTGUI::CView* child) {
        if (found != nullptr) {
            return;
        }
        if (auto* control = dynamic_cast<VSTGUI::CControl*>(child); control != nullptr && control->getTag() == tag) {
            found = control;
        } else if (auto* sub = child->asViewContainer()) {
            found = findDisrumpoTaggedControl(sub, tag);
        }
    });
    return found;
}

/// Rectangle of @p view in @p root's coordinates (each parent's origin added up to the root).
[[nodiscard]] VSTGUI::CRect rectInRoot(const VSTGUI::CView* view, const VSTGUI::CView* root) {
    VSTGUI::CRect r = view->getViewSize();
    for (const VSTGUI::CView* p = view->getParentView(); p != nullptr && p != root; p = p->getParentView()) {
        r.offset(p->getViewSize().left, p->getViewSize().top);
    }
    return r;
}

/// Renders @p rect of @p root at 2x and writes it as a PNG; returns the byte count written (0 on failure).
std::size_t writeDisrumpoShot(VSTGUI::CViewContainer* root, const VSTGUI::CRect& rect,
                              const std::filesystem::path& file, double scale = 2.0) {
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

void shoot(VSTGUI::CViewContainer* root, const VSTGUI::CRect& rect, const std::filesystem::path& file) {
    const std::size_t bytes = writeDisrumpoShot(root, rect, file);
    std::cout << file.filename().string() << ": " << rect.getWidth() << " x " << rect.getHeight() << " at ("
              << rect.left << ", " << rect.top << "), " << bytes << " bytes\n";
    CHECK(bytes > 0u);
}

}  // namespace

TEST_CASE("Disrumpo_EditorScreenshots", "[.][screenshots]") {
#if SMTG_OS_WINDOWS
    REQUIRE(SUCCEEDED(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)));
#endif
    const std::filesystem::path dir = disrumpoShotsDir();
    const auto sweepEnableId = Disrumpo::makeSweepParamId(Disrumpo::SweepParamType::kSweepEnable);
    const auto band1ExpandedId = Disrumpo::makeBandParamId(0, Disrumpo::BandParamType::kBandExpanded);

    auto controller = Steinberg::owned(new Disrumpo::Controller());
    REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);
    controller->setParamNormalized(sweepEnableId, 1.0);
    {
        Disrumpo::TestSupport::HeadlessEditor editor(*controller);
        REQUIRE(editor.ready());
        VSTGUI::CFrame* frame = editor.frame();
        // The template root: the frame's first child (the preset overlays come after it).
        VSTGUI::CViewContainer* root = frame->getView(0) != nullptr ? frame->getView(0)->asViewContainer() : nullptr;
        REQUIRE(root != nullptr);
        if (!root->isAttached()) {
            root->attached(frame);
        }
        Steinberg::UpdateHandler::instance()->triggerDeferedUpdates();

        // The editor opens at the base height; the mod panel below it is closed.
        const VSTGUI::CRect editorRect(0.0, 0.0, root->getViewSize().getWidth(),
                                       Disrumpo::ModPanelToggleController::kBaseHeight);

        VSTGUI::CControl* sweepEnable = findDisrumpoTaggedControl(root, static_cast<std::int32_t>(sweepEnableId));
        REQUIRE(sweepEnable != nullptr);
        REQUIRE(sweepEnable->getValueNormalized() > 0.5f);
        REQUIRE(sweepEnable->getParentView() != nullptr);
        const VSTGUI::CRect sweepRowRect = rectInRoot(sweepEnable->getParentView(), root);

        // Level meters lit: one processor window (300 ms at 48 kHz) through the editor timer, Output L clipped
        {
            auto block = std::make_unique<Disrumpo::SpectrumBlock>();
            block->numSamples = 64;
            block->sampleRate = 48000.0f;
            auto& m = block->meters;
            m.inPeak[0] = 0.5f;
            m.inPeak[1] = 0.4f;
            m.inMeanSq[0] = 0.125f;
            m.inMeanSq[1] = 0.08f;
            m.outPeak[0] = 1.1f;
            m.outPeak[1] = 0.8f;
            m.outMeanSq[0] = 0.35f;
            m.outMeanSq[1] = 0.2f;
            m.bandPeak[0] = 0.7f;
            m.bandPeak[1] = 0.35f;
            m.bandPeak[2] = 0.2f;
            m.bandPeak[3] = 0.1f;
            m.clipFlags = Disrumpo::kMeterClipOutL;
            m.numSamples = 14400;
            Steinberg::Vst::DataExchangeBlock deb{
                .data = block.get(), .size = static_cast<Steinberg::uint32>(sizeof(Disrumpo::SpectrumBlock)),
                .blockID = 0};
            controller->onDataExchangeBlocksReceived(0, 1, &deb, static_cast<Steinberg::TBool>(false));
            controller->tickEditorTimerForTest();
        }

        shoot(root, editorRect, dir / "editor-main.png");
        shoot(root, sweepRowRect, dir / "sweep-row.png");
        // The header bar (IN/OUT meters) and the band panels' title rows (band meters)
        shoot(root, VSTGUI::CRect(0.0, 0.0, root->getViewSize().getWidth(), 50.0), dir / "header.png");
        shoot(root, VSTGUI::CRect(20.0, 190.0, 980.0, 222.0), dir / "band-titles.png");

        // Band 1's expanded strip: the param drives AnimatedExpandController (shown at once headlessly).
        controller->setParamNormalized(band1ExpandedId, 1.0);
        Steinberg::UpdateHandler::instance()->triggerDeferedUpdates();
        shoot(root, editorRect, dir / "editor-band1-expanded.png");

        // The strip itself: the container that the Band1ExpandedContainer marker sits in.
        VSTGUI::CControl* marker = findDisrumpoTaggedControl(root, 9100);
        REQUIRE(marker != nullptr);
        REQUIRE(marker->getParentView() != nullptr);
        REQUIRE(marker->getParentView()->isVisible());
        shoot(root, rectInRoot(marker->getParentView(), root), dir / "band1-expanded-strip.png");

        editor.close();
    }
    REQUIRE(controller->terminate() == Steinberg::kResultOk);
    std::cout << "disrumpo shots written to " << dir.string() << '\n';
}
