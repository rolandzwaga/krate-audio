// ==============================================================================
// Editor screenshots for the manual (hidden: [.][screenshots]; never a gate)
// ==============================================================================
// Builds the editor headlessly (HeadlessEditor), loads a factory preset into the controller, and renders the whole
// editor or one uidesc-labelled section offscreen at 2x into PNG files for MANUAL.md. The images are documentation,
// not goldens: nothing asserts on pixels.
//
// Regenerate: node tools/gen-screenshots.js profundum   (writes plugins/profundum/images/)
// By hand:    profundum_tests.exe "[screenshots]"  with PROFUNDUM_SHOTS_DIR set (default <temp>/profundum-shots)
//
// The live harmonic displays stay at rest: no processor feeds them here.

#include "controller/controller.h"
#include "profundum_preset_host.h"
#include "ui/panel_sub_controller.h"
#include "ui_tree_helpers.h"

#include <base/source/updatehandler.h>
#include <public.sdk/source/common/memorystream.h>
#include <vstgui/lib/cbitmap.h>
#include <vstgui/lib/cframe.h>
#include <vstgui/lib/coffscreencontext.h>
#include <vstgui/lib/controls/csegmentbutton.h>
#include <vstgui/lib/platform/platformfactory.h>
#include <vstgui/uidescription/viewcreator/viewcreator.h>

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <vector>

#if SMTG_OS_WINDOWS
#include <objbase.h>   // CoInitializeEx: Direct2D offscreen bitmaps are WIC bitmaps, which need COM (a host has it)
#endif

namespace {

namespace P = ::Profundum;
namespace Pd = ::Profundum::PresetDefs;

/// One manual image: the preset loaded, the page (0 Main, 1 Advanced) and tab (Advanced: 0 SECTIONS, 1 MODULATION,
/// 2 SETTINGS) shown, and the uidesc-label of the view cropped ("" = the whole editor).
struct Shot {
    std::string_view file;
    std::string_view preset;
    int page;
    int tab;
    std::string_view label;
};

constexpr std::string_view kShowcase = "Stiff Wire";
constexpr std::string_view kAnimated = "Tidal Fold";   // Morph Modulation on Cycle

constexpr std::array kShots{
    Shot{.file = "editor-main.png", .preset = kShowcase, .page = 0, .tab = 0, .label = ""},
    Shot{.file = "editor-advanced.png", .preset = kShowcase, .page = 1, .tab = 0, .label = ""},
    Shot{.file = "header.png", .preset = kShowcase, .page = 0, .tab = 0, .label = "header"},
    Shot{.file = "main-mass.png", .preset = kShowcase, .page = 0, .tab = 0, .label = "mass-group"},
    Shot{.file = "main-anchor.png", .preset = kShowcase, .page = 0, .tab = 0, .label = "anchor-group"},
    Shot{.file = "main-bass-morph.png", .preset = kShowcase, .page = 0, .tab = 0, .label = "morph-band"},
    Shot{.file = "adv-core-spectrum.png", .preset = kShowcase, .page = 1, .tab = 0, .label = "section-core-spectrum"},
    Shot{.file = "adv-excitation-fold.png", .preset = kShowcase, .page = 1, .tab = 0,
         .label = "section-excitation-fold"},
    Shot{.file = "adv-shape-character.png", .preset = kShowcase, .page = 1, .tab = 0,
         .label = "section-shape-character"},
    Shot{.file = "adv-mass.png", .preset = kShowcase, .page = 1, .tab = 0, .label = "section-mass"},
    Shot{.file = "adv-sub-anchor.png", .preset = kShowcase, .page = 1, .tab = 0, .label = "section-sub-anchor"},
    Shot{.file = "adv-stereo-output.png", .preset = kShowcase, .page = 1, .tab = 0, .label = "section-stereo-output"},
    Shot{.file = "adv-pitch-envelopes.png", .preset = kShowcase, .page = 1, .tab = 0,
         .label = "section-pitch-envelopes"},
    Shot{.file = "adv-morph-behaviour.png", .preset = kShowcase, .page = 1, .tab = 0,
         .label = "section-morph-behaviour"},
    Shot{.file = "adv-modulation.png", .preset = kAnimated, .page = 1, .tab = 1, .label = "tab-modulation"},
    Shot{.file = "adv-settings.png", .preset = kShowcase, .page = 1, .tab = 2, .label = "tab-settings"},
};

[[nodiscard]] std::filesystem::path shotsDir() {
    if (const char* dir = std::getenv("PROFUNDUM_SHOTS_DIR"); dir != nullptr && *dir != '\0') {
        return dir;
    }
    return std::filesystem::temp_directory_path() / "profundum-shots";
}

[[nodiscard]] std::string labelOf(const VSTGUI::CView* view) {
    std::uint32_t size = 0;
    const auto id = VSTGUI::UIViewCreator::ViewCreator::labelAttrID;
    if (!view->getAttributeSize(id, size) || size == 0) {
        return {};
    }
    std::vector<char> buf(size + 1, '\0');
    std::uint32_t outSize = 0;
    if (!view->getAttribute(id, size, buf.data(), outSize)) {
        return {};
    }
    return {buf.data()};
}

/// Finds the uidesc-labelled view under @p container and returns its rectangle in frame coordinates (@p origin = the
/// container's own offset). The walk carries the offset itself rather than reading parent links.
[[nodiscard]] bool findLabelled(VSTGUI::CViewContainer* container, std::string_view label, VSTGUI::CPoint origin,
                                VSTGUI::CRect& out) {
    bool found = false;
    container->forEachChild([&](VSTGUI::CView* child) {
        if (found) {
            return;
        }
        VSTGUI::CRect r = child->getViewSize();
        r.offset(origin.x, origin.y);
        if (labelOf(child) == label) {
            out = r;
            found = true;
        } else if (auto* sub = child->asViewContainer()) {
            found = findLabelled(sub, label, r.getTopLeft(), out);
        }
    });
    return found;
}

template <typename T>
[[nodiscard]] T* findTagged(VSTGUI::CViewContainer* container, std::int32_t tag) {
    T* found = nullptr;
    container->forEachChild([&](VSTGUI::CView* child) {
        if (found != nullptr) {
            return;
        }
        if (auto* typed = dynamic_cast<T*>(child); typed != nullptr && typed->getTag() == tag) {
            found = typed;
        } else if (auto* sub = child->asViewContainer()) {
            found = findTagged<T>(sub, tag);
        }
    });
    return found;
}

void select(VSTGUI::CViewContainer* frame, std::int32_t stripTag, int segment) {
    auto* strip = findTagged<VSTGUI::CSegmentButton>(frame, stripTag);
    REQUIRE(strip != nullptr);
    strip->setSelectedSegment(static_cast<std::uint32_t>(segment));
    strip->valueChanged();
}

/// Renders @p rect of @p root (the editor's 960 x 600 template container, at the frame origin) at @p scale and writes
/// it as a PNG; returns the byte count written (0 on failure). The root is drawn, not the frame: a headless frame has
/// no window and reads 0 x 0.
std::size_t writeShot(VSTGUI::CViewContainer* root, const VSTGUI::CRect& rect, const std::filesystem::path& file,
                      double scale = 2.0) {
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

void loadPreset(P::Controller& ctl, std::string_view name) {
    for (const Pd::ProfundumPresetDef& def : Pd::allPresets()) {
        if (def.name != name) {
            continue;
        }
        std::vector<std::uint8_t> comp;
        std::string why;
        INFO(why);
        REQUIRE(ProfundumPluginTest::buildPresetComponentState(def, comp, why));
        auto stream = Steinberg::owned(new Steinberg::MemoryStream());
        Steinberg::int32 written = 0;
        stream->write(comp.data(), static_cast<Steinberg::int32>(comp.size()), &written);
        REQUIRE(static_cast<std::size_t>(written) == comp.size());
        stream->seek(0, Steinberg::IBStream::kIBSeekSet, nullptr);
        REQUIRE(ctl.setComponentState(stream) == Steinberg::kResultOk);
        return;
    }
    FAIL("no factory preset " << name);
}

/// One editor per preset: load it into a fresh controller, build the editor, render every shot that uses it.
void renderShotsFor(std::string_view preset, const std::filesystem::path& dir) {
    auto controller = Steinberg::owned(new P::Controller());
    REQUIRE(controller->initialize(nullptr) == Steinberg::kResultOk);
    loadPreset(*controller, preset);
    {
        ProfundumPluginTest::HeadlessEditor ed(controller.get());
        VSTGUI::CFrame* frame = ed.frame();
        REQUIRE(frame != nullptr);
        // The template root: the frame's first child (the second is the preset browser overlay).
        VSTGUI::CViewContainer* root = frame->getView(0) != nullptr ? frame->getView(0)->asViewContainer() : nullptr;
        REQUIRE(root != nullptr);
        // A headless frame has no window, so CFrame::open never attaches the tree; segment buttons lay out their
        // segments only once attached (CSegmentButton::updateSegmentSizes), and other views draw blank too.
        if (!root->isAttached()) {
            root->attached(frame);
        }
        // VST3Editor fills a bound menu's entries and a segment button's segments in a deferred parameter update,
        // which a host's idle timer flushes; no timer runs here.
        Steinberg::UpdateHandler::instance()->triggerDeferedUpdates();

        for (const Shot& shot : kShots) {
            if (shot.preset != preset) {
                continue;
            }
            INFO("shot " << shot.file);
            select(frame, P::UI::kPageStripTag, shot.page);
            if (shot.page == 1) {
                select(frame, P::UI::kTabStripTag, shot.tab);
            }
            VSTGUI::CRect rect = root->getViewSize();
            if (!shot.label.empty()) {
                REQUIRE(findLabelled(frame, shot.label, {0, 0}, rect));
            }
            const std::size_t bytes = writeShot(root, rect, dir / shot.file);
            std::cout << shot.file << ": " << rect.getWidth() << " x " << rect.getHeight() << " at (" << rect.left
                      << ", " << rect.top << "), " << bytes << " bytes\n";
            CHECK(bytes > 0u);
        }
    }
    REQUIRE(controller->terminate() == Steinberg::kResultOk);
}

}  // namespace

TEST_CASE("Profundum_EditorScreenshots", "[.][screenshots]") {
#if SMTG_OS_WINDOWS
    REQUIRE(SUCCEEDED(::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED)));
#endif
    const std::filesystem::path dir = shotsDir();
    for (const std::string_view preset : {kShowcase, kAnimated}) {
        renderShotsFor(preset, dir);
    }
    // No CoUninitialize: VSTGUI's platform factory keeps its WIC factory until VSTGUI::exit at process exit, and
    // releasing it after COM is torn down crashes.
    std::cout << kShots.size() << " shots written to " << dir.string() << '\n';
}
