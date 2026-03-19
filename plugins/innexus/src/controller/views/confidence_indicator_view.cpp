// ==============================================================================
// ConfidenceIndicatorView Implementation
// ==============================================================================
// FR-013: Confidence bar with color coding
// FR-014: Green/yellow/red color zones
// FR-015: Detected note name + frequency text
// Polyphonic extension: multi-voice display with mode badge
// ==============================================================================

#include "confidence_indicator_view.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cfont.h"

#include <algorithm>
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
    numVoices_ = static_cast<int>(data.numVoices);
    isPolyphonic_ = data.isPolyphonic != 0;
    analysisMode_ = data.analysisMode;

    for (int i = 0; i < 8; ++i)
    {
        voices_[i].f0 = data.voices[i].f0;
        voices_[i].confidence = data.voices[i].confidence;
        voices_[i].amplitude = data.voices[i].amplitude;
    }

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

std::string ConfidenceIndicatorView::modeLabel(uint8_t analysisMode, bool isPolyphonic)
{
    switch (analysisMode)
    {
    case 0: return "MONO";
    case 1: return "POLY";
    case 2: // Auto
        return isPolyphonic ? "AUTO>P" : "AUTO>M";
    default: return "AUTO";
    }
}

void ConfidenceIndicatorView::draw(VSTGUI::CDrawContext* context)
{
    auto rect = getViewSize();
    float viewWidth = static_cast<float>(rect.getWidth());

    // Dark background
    context->setFillColor(VSTGUI::CColor(13, 13, 26));
    context->drawRect(rect, VSTGUI::kDrawFilled);

    constexpr float kPadding = 6.0f;

    // --- Mode badge (top-left, y=2-14) ---
    {
        auto badgeFont = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 8);
        context->setFont(badgeFont);

        auto label = modeLabel(analysisMode_, isPolyphonic_);

        // Badge background pill
        bool isPoly = isPolyphonic_ || analysisMode_ == 1;
        VSTGUI::CColor badgeColor = isPoly
            ? VSTGUI::CColor(120, 80, 180)   // Purple for poly
            : VSTGUI::CColor(70, 100, 140);  // Steel blue for mono

        // Measure approximate badge width (8pt font, ~5px per char + padding)
        float badgeWidth = static_cast<float>(label.size()) * 5.5f + 10.0f;
        VSTGUI::CRect badgeRect(
            rect.left + kPadding,
            rect.top + 2.0,
            rect.left + kPadding + badgeWidth,
            rect.top + 14.0);
        context->setFillColor(badgeColor);
        context->drawRect(badgeRect, VSTGUI::kDrawFilled);

        // Badge text
        context->setFontColor(VSTGUI::CColor(220, 220, 240));
        context->drawString(label.c_str(), badgeRect, VSTGUI::kCenterText);
    }

    // Determine if we have polyphonic voice data to show
    bool showPolyVoices = numVoices_ > 1 && isPolyphonic_;

    if (showPolyVoices)
    {
        // --- Multi-voice layout ---

        // Primary voice (y=18-40): 6px confidence bar + 11pt note text
        constexpr float kPrimaryBarY = 18.0f;
        constexpr float kPrimaryBarHeight = 6.0f;
        float barMaxWidth = viewWidth - 2.0f * kPadding;

        // Primary voice bar
        float primaryConf = voices_[0].confidence;
        float primaryF0 = voices_[0].f0;

        VSTGUI::CRect primaryBarBg(
            rect.left + kPadding,
            rect.top + kPrimaryBarY,
            rect.left + kPadding + barMaxWidth,
            rect.top + kPrimaryBarY + kPrimaryBarHeight);
        context->setFillColor(VSTGUI::CColor(40, 40, 50));
        context->drawRect(primaryBarBg, VSTGUI::kDrawFilled);

        float primaryBarWidth = barMaxWidth * primaryConf;
        if (primaryBarWidth > 0.5f)
        {
            VSTGUI::CRect primaryBarFill(
                rect.left + kPadding,
                rect.top + kPrimaryBarY,
                rect.left + kPadding + primaryBarWidth,
                rect.top + kPrimaryBarY + kPrimaryBarHeight);
            context->setFillColor(getConfidenceColor(primaryConf));
            context->drawRect(primaryBarFill, VSTGUI::kDrawFilled);
        }

        // Primary voice text
        auto primaryFont = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 11);
        context->setFont(primaryFont);

        VSTGUI::CRect primaryTextRect(
            rect.left + kPadding,
            rect.top + kPrimaryBarY + kPrimaryBarHeight + 2.0,
            rect.right - kPadding,
            rect.top + kPrimaryBarY + kPrimaryBarHeight + 16.0);

        if (primaryConf > 0.1f && primaryF0 > 0.0f)
        {
            auto noteName = freqToNoteName(primaryF0);
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%s - %.0f Hz", noteName.c_str(),
                          static_cast<double>(primaryF0));
            context->setFontColor(VSTGUI::CColor(200, 200, 220));
            context->drawString(buf, primaryTextRect);
        }
        else
        {
            context->setFontColor(VSTGUI::CColor(100, 100, 120));
            context->drawString("--", primaryTextRect);
        }

        // --- Secondary voices (y=44+, 12px each) ---
        auto secFont = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 9);
        context->setFont(secFont);

        constexpr float kSecondaryStartY = 44.0f;
        constexpr float kSecondaryRowHeight = 14.0f;
        constexpr float kSecondaryBarHeight = 3.0f;

        // Find max amplitude for relative scaling
        float maxAmp = 0.001f; // avoid div by zero
        for (int i = 0; i < numVoices_; ++i)
            maxAmp = std::max(maxAmp, voices_[i].amplitude);

        int maxSecondary = std::min(numVoices_ - 1, 7);
        for (int i = 0; i < maxSecondary; ++i)
        {
            int voiceIdx = i + 1;
            float y = kSecondaryStartY + static_cast<float>(i) * kSecondaryRowHeight;

            // Check we don't draw beyond view bounds
            if (rect.top + y + kSecondaryRowHeight > rect.bottom - 2.0)
                break;

            float conf = voices_[voiceIdx].confidence;
            float freq = voices_[voiceIdx].f0;
            float amp = voices_[voiceIdx].amplitude;

            // Secondary bar background
            VSTGUI::CRect secBarBg(
                rect.left + kPadding,
                rect.top + y,
                rect.left + kPadding + barMaxWidth,
                rect.top + y + kSecondaryBarHeight);
            context->setFillColor(VSTGUI::CColor(40, 40, 50));
            context->drawRect(secBarBg, VSTGUI::kDrawFilled);

            // Secondary bar fill
            float secBarWidth = barMaxWidth * conf;
            if (secBarWidth > 0.5f)
            {
                VSTGUI::CRect secBarFill(
                    rect.left + kPadding,
                    rect.top + y,
                    rect.left + kPadding + secBarWidth,
                    rect.top + y + kSecondaryBarHeight);
                context->setFillColor(getConfidenceColor(conf));
                context->drawRect(secBarFill, VSTGUI::kDrawFilled);
            }

            // Secondary voice text (alpha scaled by relative amplitude)
            VSTGUI::CRect secTextRect(
                rect.left + kPadding,
                rect.top + y + kSecondaryBarHeight,
                rect.right - kPadding,
                rect.top + y + kSecondaryRowHeight);

            if (conf > 0.1f && freq > 0.0f)
            {
                auto noteName = freqToNoteName(freq);
                char buf[64];
                std::snprintf(buf, sizeof(buf), "%s - %.0f Hz", noteName.c_str(),
                              static_cast<double>(freq));

                // Scale text brightness by relative amplitude
                float relAmp = std::clamp(amp / maxAmp, 0.3f, 1.0f);
                auto alpha = static_cast<uint8_t>(relAmp * 220.0f);
                context->setFontColor(VSTGUI::CColor(180, 180, 200, alpha));
                context->drawString(buf, secTextRect);
            }
            else
            {
                context->setFontColor(VSTGUI::CColor(80, 80, 100));
                context->drawString("--", secTextRect);
            }
        }
    }
    else
    {
        // --- Mono layout (original behavior) ---
        constexpr float kBarHeight = 8.0f;
        constexpr float kBarY = 18.0f;

        float barMaxWidth = viewWidth - 2.0f * kPadding;
        float barWidth = barMaxWidth * confidence_;

        // Bar background (track)
        VSTGUI::CRect barBgRect(
            rect.left + kPadding,
            rect.top + kBarY,
            rect.left + kPadding + barMaxWidth,
            rect.top + kBarY + kBarHeight);
        context->setFillColor(VSTGUI::CColor(40, 40, 50));
        context->drawRect(barBgRect, VSTGUI::kDrawFilled);

        // Bar fill
        if (barWidth > 0.5f)
        {
            VSTGUI::CRect barFillRect(
                rect.left + kPadding,
                rect.top + kBarY,
                rect.left + kPadding + barWidth,
                rect.top + kBarY + kBarHeight);
            context->setFillColor(getConfidenceColor(confidence_));
            context->drawRect(barFillRect, VSTGUI::kDrawFilled);
        }

        // Note name + frequency text
        auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 11);
        context->setFont(font);

        VSTGUI::CRect textRect(
            rect.left + kPadding,
            rect.top + kBarY + kBarHeight + 6.0,
            rect.right - kPadding,
            rect.top + kBarY + kBarHeight + 22.0);

        if (confidence_ > 0.3f && f0_ > 0.0f)
        {
            auto noteName = freqToNoteName(f0_);
            char buf[64];
            std::snprintf(buf, sizeof(buf), "%s - %.0f Hz", noteName.c_str(),
                          static_cast<double>(f0_));
            context->setFontColor(VSTGUI::CColor(200, 200, 220));
            context->drawString(buf, textRect);
        }
        else
        {
            context->setFontColor(VSTGUI::CColor(100, 100, 120));
            context->drawString("--", textRect);
        }
    }

    setDirty(false);
}

} // namespace Innexus
