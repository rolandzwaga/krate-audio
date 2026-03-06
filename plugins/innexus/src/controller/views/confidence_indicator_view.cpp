// ==============================================================================
// ConfidenceIndicatorView Implementation
// ==============================================================================
// FR-013: Confidence bar with color coding
// FR-014: Green/yellow/red color zones
// FR-015: Detected note name + frequency text
// ==============================================================================

#include "confidence_indicator_view.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"

#include <cmath>
#include <cstdio>

namespace Innexus {

ConfidenceIndicatorView::ConfidenceIndicatorView(const VSTGUI::CRect& size)
    : CView(size)
{
}

void ConfidenceIndicatorView::updateData(const DisplayData& data)
{
    confidence_ = data.f0Confidence;
    f0_ = data.f0;
    invalid();
}

VSTGUI::CColor ConfidenceIndicatorView::getConfidenceColor(float confidence)
{
    if (confidence < 0.3f)
        return {220, 50, 50};   // Red
    if (confidence < 0.7f)
        return {220, 200, 50};  // Yellow
    return {50, 200, 80};       // Green
}

std::string ConfidenceIndicatorView::freqToNoteName(float freq)
{
    if (freq <= 0.0f)
        return "--";

    // MIDI note = 12 * log2(freq / 440) + 69
    float midiNote = 12.0f * std::log2(freq / 440.0f) + 69.0f;
    int noteNum = static_cast<int>(std::round(midiNote));

    if (noteNum < 0 || noteNum > 127)
        return "--";

    static const char* noteNames[] = {
        "C", "C#", "D", "D#", "E", "F",
        "F#", "G", "G#", "A", "A#", "B"
    };

    int octave = (noteNum / 12) - 1;
    int noteIndex = noteNum % 12;

    return std::string(noteNames[noteIndex]) + std::to_string(octave);
}

void ConfidenceIndicatorView::draw(VSTGUI::CDrawContext* context)
{
    auto rect = getViewSize();
    float viewWidth = static_cast<float>(rect.getWidth());

    // Dark background
    context->setFillColor(VSTGUI::CColor(13, 13, 26));
    context->drawRect(rect, VSTGUI::kDrawFilled);

    constexpr float kPadding = 6.0f;
    constexpr float kBarHeight = 8.0f;

    // FR-013: Draw confidence bar
    float barMaxWidth = viewWidth - 2.0f * kPadding;
    float barWidth = barMaxWidth * confidence_;

    // Bar background (track)
    VSTGUI::CRect barBgRect(
        rect.left + kPadding,
        rect.top + kPadding,
        rect.left + kPadding + barMaxWidth,
        rect.top + kPadding + kBarHeight);
    context->setFillColor(VSTGUI::CColor(40, 40, 50));
    context->drawRect(barBgRect, VSTGUI::kDrawFilled);

    // Bar fill
    if (barWidth > 0.5f)
    {
        VSTGUI::CRect barFillRect(
            rect.left + kPadding,
            rect.top + kPadding,
            rect.left + kPadding + barWidth,
            rect.top + kPadding + kBarHeight);
        context->setFillColor(getConfidenceColor(confidence_));
        context->drawRect(barFillRect, VSTGUI::kDrawFilled);
    }

    // FR-015: Note name + frequency text
    auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 11);
    context->setFont(font);

    VSTGUI::CRect textRect(
        rect.left + kPadding,
        rect.top + kPadding + kBarHeight + 6.0,
        rect.right - kPadding,
        rect.top + kPadding + kBarHeight + 22.0);

    if (confidence_ > 0.3f && f0_ > 0.0f)
    {
        auto noteName = freqToNoteName(f0_);
        char buf[64];
        std::snprintf(buf, sizeof(buf), "%s - %.0f Hz", noteName.c_str(), f0_);
        context->setFontColor(VSTGUI::CColor(200, 200, 220));
        context->drawString(buf, textRect);
    }
    else
    {
        context->setFontColor(VSTGUI::CColor(100, 100, 120));
        context->drawString("--", textRect);
    }

    setDirty(false);
}

} // namespace Innexus
