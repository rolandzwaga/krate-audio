// ==============================================================================
// KitMetersView -- Phase 6 implementation (Spec 141, T046)
// ==============================================================================

#include "ui/kit_meters_view.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/ccolor.h"

#include <algorithm>

namespace Membrum::UI {

namespace {

constexpr VSTGUI::CColor kBgColor     = { 18,  18,  22, 255 };
constexpr VSTGUI::CColor kBarColor    = {110, 200, 140, 255 };
constexpr VSTGUI::CColor kClipColor   = {230,  90,  90, 255 };

constexpr float kClipThreshold = 0.9f;

// T2.31 (MB-065): peak hold with a 20 dB/s fall at the 30 Hz UI tick,
// 10^(-20 / 20 / 30) = 10^(-1/30) per tick.
constexpr float kFallPerTick = 0.92612f;

} // anonymous namespace

KitMetersView::KitMetersView(const VSTGUI::CRect& size) noexcept
    : CView(size)
{
}

void KitMetersView::setPeaks(float peakL, float peakR) noexcept
{
    // A new peak shows at once; a lower one lets the shown peak fall.
    peakL_ = std::max(std::clamp(peakL, 0.0f, 1.0f), peakL_ * kFallPerTick);
    peakR_ = std::max(std::clamp(peakR, 0.0f, 1.0f), peakR_ * kFallPerTick);
    invalid();
}

void KitMetersView::draw(VSTGUI::CDrawContext* ctx)
{
    if (!ctx)
        return;

    const auto& rect = getViewSize();
    ctx->setFillColor(kBgColor);
    ctx->drawRect(rect, VSTGUI::kDrawFilled);

    const double halfH = rect.getHeight() * 0.5;
    const double gap   = 1.0;

    // Left channel (top half)
    {
        VSTGUI::CRect bar = rect;
        bar.bottom = rect.top + halfH - gap;
        bar.right  = rect.left + rect.getWidth() * static_cast<double>(peakL_);
        ctx->setFillColor(peakL_ > kClipThreshold ? kClipColor : kBarColor);
        ctx->drawRect(bar, VSTGUI::kDrawFilled);
    }
    // Right channel (bottom half)
    {
        VSTGUI::CRect bar = rect;
        bar.top    = rect.top + halfH + gap;
        bar.right  = rect.left + rect.getWidth() * static_cast<double>(peakR_);
        ctx->setFillColor(peakR_ > kClipThreshold ? kClipColor : kBarColor);
        ctx->drawRect(bar, VSTGUI::kDrawFilled);
    }

    setDirty(false);
}

} // namespace Membrum::UI
