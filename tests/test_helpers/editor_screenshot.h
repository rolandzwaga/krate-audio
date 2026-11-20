#pragma once
// ==============================================================================
// Offscreen editor screenshots for krateaudio.com and the user manuals
// ==============================================================================
// Shared by every plugin's hidden `[.][screenshots][site]` test case. A case builds its editor headlessly, then calls
// writeSiteShots() once (the three images every plugin page uses) and writeShot() for each manual image. The PNGs
// are committed under plugins/<plugin>/images/ by `node tools/gen-screenshots.js <plugin>`; tools/gen-site.js reads
// them. They are documentation, not goldens: nothing asserts on pixels.
//
//   hero.png       the whole editor at 2x: the landing page's main image and its structured-data screenshot
//   og-image.png   1200 x 630: the social card (og:image / twitter:image), the editor fitted and letterboxed
//   card.png       600 x 315: the root page's plugin card (tools/gen-root-index.js writes that size into the HTML)
//
// A headless frame has no window and reads 0 x 0, so a case draws the template root (the frame's first child), not the
// frame; overlays the controller adds to the frame (preset browsers, popups) are therefore never in a shot.

#include <pluginterfaces/base/smartpointer.h>
#include <pluginterfaces/vst/ivsteditcontroller.h>
#include <public.sdk/source/vst/vstpresetfile.h>
#include <vstgui/lib/cbitmap.h>
#include <vstgui/lib/cdrawcontext.h>
#include <vstgui/lib/cgraphicstransform.h>
#include <vstgui/lib/coffscreencontext.h>
#include <vstgui/lib/cviewcontainer.h>
#include <vstgui/lib/platform/platformfactory.h>
#include <vstgui/uidescription/viewcreator/viewcreator.h>

#include <algorithm>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#if SMTG_OS_WINDOWS
#include <objbase.h>   // CoInitializeEx: Direct2D offscreen bitmaps are WIC bitmaps, which need COM (a host has it)
#endif

namespace Krate::TestSupport::Screenshots {

inline constexpr int kOgWidth = 1200;
inline constexpr int kOgHeight = 630;
inline constexpr int kCardWidth = 600;    // tools/gen-root-index.js CARD_IMAGE
inline constexpr int kCardHeight = 315;
inline constexpr double kShotScale = 2.0;

/// The output directory: $@p envVar, else <temp>/@p fallback.
[[nodiscard]] inline std::filesystem::path shotsDir(const char* envVar, std::string_view fallback) {
    if (const char* dir = std::getenv(envVar); dir != nullptr && *dir != '\0') {
        return dir;
    }
    return std::filesystem::temp_directory_path() / std::string(fallback);
}

/// COM for the WIC bitmaps behind Direct2D offscreen contexts (Windows); nothing elsewhere. Never uninitialised:
/// VSTGUI's platform factory keeps its WIC factory until VSTGUI::exit at process exit, and releasing it after COM is
/// torn down crashes.
[[nodiscard]] inline bool initPlatformForShots() {
#if SMTG_OS_WINDOWS
    const HRESULT hr = ::CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    return SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
#else
    return true;
#endif
}

/// The view's `uidesc-label` attribute, or "" when it has none.
[[nodiscard]] inline std::string labelOf(const VSTGUI::CView* view) {
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

/// Finds the first view under @p container (depth first) that @p match accepts and returns its rectangle in the
/// coordinates of the container's own origin @p origin (pass {0, 0} for the frame or template root). The walk carries
/// the offset itself rather than reading parent links.
inline bool findView(VSTGUI::CViewContainer* container, const std::function<bool(VSTGUI::CView*)>& match,
                     VSTGUI::CPoint origin, VSTGUI::CRect& out) {
    bool found = false;
    container->forEachChild([&](VSTGUI::CView* child) {
        if (found) {
            return;
        }
        VSTGUI::CRect r = child->getViewSize();
        r.offset(origin.x, origin.y);
        if (match(child)) {
            out = r;
            found = true;
        } else if (auto* sub = child->asViewContainer()) {
            found = findView(sub, match, r.getTopLeft(), out);
        }
    });
    return found;
}

/// The union of the rectangles of every view under @p container that @p match accepts; empty when none does.
[[nodiscard]] inline VSTGUI::CRect unionOfViews(VSTGUI::CViewContainer* container,
                                                const std::function<bool(VSTGUI::CView*)>& match,
                                                VSTGUI::CPoint origin = {0, 0}) {
    VSTGUI::CRect all;
    bool any = false;
    container->forEachChild([&](VSTGUI::CView* child) {
        VSTGUI::CRect r = child->getViewSize();
        r.offset(origin.x, origin.y);
        if (match(child)) {
            all = any ? all.unite(r) : r;
            any = true;
        } else if (auto* sub = child->asViewContainer()) {
            const VSTGUI::CRect inner = unionOfViews(sub, match, r.getTopLeft());
            if (!inner.isEmpty()) {
                all = any ? all.unite(inner) : inner;
                any = true;
            }
        }
    });
    return all;
}

inline bool findLabelled(VSTGUI::CViewContainer* container, std::string_view label, VSTGUI::CRect& out) {
    return findView(container, [label](VSTGUI::CView* v) { return labelOf(v) == label; }, {0, 0}, out);
}

/// The first view of type @p T under @p container (depth first) accepted by @p match, or nullptr.
template <typename T>
[[nodiscard]] T* findFirst(VSTGUI::CViewContainer* container, const std::function<bool(T*)>& match = {}) {
    T* found = nullptr;
    container->forEachChild([&](VSTGUI::CView* child) {
        if (found != nullptr) {
            return;
        }
        if (auto* typed = dynamic_cast<T*>(child); typed != nullptr && (!match || match(typed))) {
            found = typed;
        } else if (auto* sub = child->asViewContainer()) {
            found = findFirst<T>(sub, match);
        }
    });
    return found;
}

/// Writes @p bitmap as a PNG; returns the byte count written (0 on failure).
inline std::size_t writePng(VSTGUI::CBitmap* bitmap, const std::filesystem::path& file) {
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

/// Renders @p rect of @p root (the editor template container, at the frame origin) at @p scale into a PNG.
inline std::size_t writeShot(VSTGUI::CViewContainer* root, const VSTGUI::CRect& rect, const std::filesystem::path& file,
                             double scale = kShotScale) {
    auto bitmap = VSTGUI::renderBitmapOffscreen(rect.getSize(), scale, [&](VSTGUI::CDrawContext& ctx) {
        VSTGUI::CDrawContext::Transform shift(ctx, VSTGUI::CGraphicsTransform().translate(-rect.left, -rect.top));
        root->drawRect(&ctx, rect);
    });
    return writePng(bitmap, file);
}

/// Applies the component state of the .vstpreset at @p file to @p controller (setComponentState), as a host does
/// when it loads a preset; false when the file cannot be read or holds no component state.
[[nodiscard]] inline bool loadPresetIntoController(const std::filesystem::path& file,
                                                   Steinberg::Vst::IEditController* controller) {
    Steinberg::IPtr<Steinberg::IBStream> stream =
        Steinberg::owned(Steinberg::Vst::FileStream::open(file.string().c_str(), "rb"));
    if (!stream) {
        return false;
    }
    Steinberg::Vst::PresetFile preset(stream);
    return preset.readChunkList() && preset.restoreComponentState(controller);
}

/// The colour the letterbox bars of a fitted image take: the root's background colour when it is opaque, else the
/// site background.
[[nodiscard]] inline VSTGUI::CColor backgroundOf(VSTGUI::CViewContainer* root) {
    const VSTGUI::CColor c = root->getBackgroundColor();
    return c.alpha == 255 ? c : VSTGUI::CColor(10, 10, 15, 255);
}

/// Renders @p rect of @p root scaled to fit @p width x @p height (aspect kept, centred, letterboxed in
/// @p background) at 1x, so the PNG is exactly that size.
inline std::size_t writeFitted(VSTGUI::CViewContainer* root, const VSTGUI::CRect& rect, int width, int height,
                               VSTGUI::CColor background, const std::filesystem::path& file) {
    const double scale = std::min(width / rect.getWidth(), height / rect.getHeight());
    const double ox = (width - rect.getWidth() * scale) / 2.0;
    const double oy = (height - rect.getHeight() * scale) / 2.0;
    auto bitmap = VSTGUI::renderBitmapOffscreen(VSTGUI::CPoint(width, height), 1.0, [&](VSTGUI::CDrawContext& ctx) {
        ctx.setFillColor(background);
        ctx.drawRect(VSTGUI::CRect(0, 0, width, height), VSTGUI::kDrawFilled);
        // p' = scale * (p - rect.topLeft) + (ox, oy)   (VSTGUI: x' = m11 x + m12 y + dx)
        const VSTGUI::CGraphicsTransform fit(scale, 0.0, 0.0, scale, ox - scale * rect.left, oy - scale * rect.top);
        VSTGUI::CDrawContext::Transform place(ctx, fit);
        root->drawRect(&ctx, rect);
    });
    return writePng(bitmap, file);
}

/// hero.png, og-image.png and card.png of the editor (see the file header); returns how many were written.
/// @p rect is the editor area to show: the whole root unless a plugin's root holds more than the visible editor
/// (Disrumpo's closed mod panel).
/// The part of @p rect a wide (1.9 : 1) card shows: all of it, unless the editor is portrait-like (Innexus,
/// 800 x 1035), whose letterboxed card would be a thin strip; then its top at the card's aspect, full width.
[[nodiscard]] inline VSTGUI::CRect cardArea(const VSTGUI::CRect& rect) {
    constexpr double kCardAspect = static_cast<double>(kOgWidth) / kOgHeight;
    if (rect.getWidth() / rect.getHeight() >= 0.65 * kCardAspect) {
        return rect;
    }
    VSTGUI::CRect top = rect;
    top.bottom = rect.top + rect.getWidth() / kCardAspect;
    return top;
}

inline int writeSiteShots(VSTGUI::CViewContainer* root, const std::filesystem::path& dir, const VSTGUI::CRect& rect) {
    const VSTGUI::CColor bg = backgroundOf(root);
    const VSTGUI::CRect card = cardArea(rect);
    int written = 0;
    written += writeShot(root, rect, dir / "hero.png") > 0 ? 1 : 0;
    written += writeFitted(root, card, kOgWidth, kOgHeight, bg, dir / "og-image.png") > 0 ? 1 : 0;
    written += writeFitted(root, card, kCardWidth, kCardHeight, bg, dir / "card.png") > 0 ? 1 : 0;
    return written;
}

inline int writeSiteShots(VSTGUI::CViewContainer* root, const std::filesystem::path& dir) {
    return writeSiteShots(root, dir, root->getViewSize());
}

}  // namespace Krate::TestSupport::Screenshots
