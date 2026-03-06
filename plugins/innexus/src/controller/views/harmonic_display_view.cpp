// ==============================================================================
// HarmonicDisplayView Implementation
// ==============================================================================
// FR-009: 48-bar spectral display with dB scaling
// FR-011: Empty/placeholder state
// FR-012: Active vs attenuated partial coloring
// ==============================================================================

#include "harmonic_display_view.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"

#include <algorithm>
#include <cmath>

namespace Innexus {

HarmonicDisplayView::HarmonicDisplayView(const VSTGUI::CRect& size)
    : CView(size)
{
}

void HarmonicDisplayView::updateData(const DisplayData& data)
{
    for (int i = 0; i < 48; ++i)
    {
        amplitudes_[i] = data.partialAmplitudes[i];
        active_[i] = (data.partialActive[i] != 0);
    }
    hasData_ = true;
    invalid();
}

float HarmonicDisplayView::amplitudeToBarHeight(float amp, float viewHeight)
{
    // dB range: -60 dB (floor) to 0 dB (full height)
    constexpr float kMinDb = -60.0f;
    constexpr float kMaxDb = 0.0f;

    float clamped = std::max(amp, 1e-6f);
    float dB = 20.0f * std::log10(clamped);

    if (dB <= kMinDb)
        return 0.0f;
    if (dB >= kMaxDb)
        return viewHeight;

    // Map [-60, 0] to [0, viewHeight]
    return viewHeight * (dB - kMinDb) / (kMaxDb - kMinDb);
}

void HarmonicDisplayView::draw(VSTGUI::CDrawContext* context)
{
    auto rect = getViewSize();
    float viewWidth = static_cast<float>(rect.getWidth());
    float viewHeight = static_cast<float>(rect.getHeight());

    // Dark background
    context->setFillColor(VSTGUI::CColor(13, 13, 26));
    context->drawRect(rect, VSTGUI::kDrawFilled);

    if (!hasData_)
    {
        // FR-011: Empty state placeholder text
        auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 12);
        context->setFont(font);
        context->setFontColor(VSTGUI::CColor(128, 128, 128));
        context->drawString("No analysis data", rect);
        setDirty(false);
        return;
    }

    // FR-009: Draw 48 vertical bars
    constexpr float kPadding = 4.0f;
    constexpr int kNumPartials = 48;
    constexpr float kGap = 1.0f;

    float barAreaWidth = static_cast<float>(viewWidth) - 2.0f * kPadding;
    float barWidth = (barAreaWidth - static_cast<float>(kNumPartials - 1) * kGap)
                     / static_cast<float>(kNumPartials);

    if (barWidth < 1.0f)
        barWidth = 1.0f;

    // Active partial color: cyan (#00bcd4)
    VSTGUI::CColor activeColor(0, 188, 212);
    // Attenuated partial color: dark gray (#333333)
    VSTGUI::CColor attenuatedColor(51, 51, 51);

    for (int i = 0; i < kNumPartials; ++i)
    {
        float barHeight = amplitudeToBarHeight(amplitudes_[i],
                                               static_cast<float>(viewHeight) - 2.0f * kPadding);
        if (barHeight < 0.5f)
            continue;  // Skip bars below threshold

        float x = static_cast<float>(rect.left) + kPadding +
                  static_cast<float>(i) * (barWidth + kGap);
        float barTop = static_cast<float>(rect.bottom) - kPadding - barHeight;
        float barBottom = static_cast<float>(rect.bottom) - kPadding;

        VSTGUI::CRect barRect(x, barTop, x + barWidth, barBottom);

        // FR-012: Active vs attenuated coloring
        if (active_[i])
            context->setFillColor(activeColor);
        else
            context->setFillColor(attenuatedColor);

        context->drawRect(barRect, VSTGUI::kDrawFilled);
    }

    setDirty(false);
}

} // namespace Innexus
