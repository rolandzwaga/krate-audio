// ==============================================================================
// SpectrumDisplay Implementation
// ==============================================================================
// FR-013: Custom VSTGUI view for displaying frequency band regions
// Phase 3: Static colored regions only (no FFT)
// ==============================================================================

#include "spectrum_display.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cstring.h"

#include <cmath>
#include <algorithm>

namespace Disrumpo {

// Band colors from ui-mockups.md
const std::array<VSTGUI::CColor, SpectrumDisplay::kMaxBands> SpectrumDisplay::kBandColors = {{
    VSTGUI::CColor(0xFF, 0x6B, 0x35, 0xFF),  // Band 1: #FF6B35
    VSTGUI::CColor(0x4E, 0xCD, 0xC4, 0xFF),  // Band 2: #4ECDC4
    VSTGUI::CColor(0x95, 0xE8, 0x6B, 0xFF),  // Band 3: #95E86B
    VSTGUI::CColor(0xC7, 0x92, 0xEA, 0xFF),  // Band 4: #C792EA
    VSTGUI::CColor(0xFF, 0xCB, 0x6B, 0xFF),  // Band 5: #FFCB6B
    VSTGUI::CColor(0xFF, 0x53, 0x70, 0xFF),  // Band 6: #FF5370
    VSTGUI::CColor(0x89, 0xDD, 0xFF, 0xFF),  // Band 7: #89DDFF
    VSTGUI::CColor(0xF7, 0x8C, 0x6C, 0xFF),  // Band 8: #F78C6C
}};

// ==============================================================================
// Constructor
// ==============================================================================

SpectrumDisplay::SpectrumDisplay(const VSTGUI::CRect& size)
    : CView(size)
{
    // Initialize default crossover frequencies with logarithmic spacing
    // For 4 bands: ~200Hz, ~2kHz, ~8kHz
    crossoverFreqs_[0] = 200.0f;
    crossoverFreqs_[1] = 2000.0f;
    crossoverFreqs_[2] = 8000.0f;

    // Initialize remaining crossover frequencies for more bands
    // These follow the same logarithmic pattern
    crossoverFreqs_[3] = 100.0f;   // Between band 4 and 5
    crossoverFreqs_[4] = 500.0f;   // Between band 5 and 6
    crossoverFreqs_[5] = 4000.0f;  // Between band 6 and 7
    crossoverFreqs_[6] = 12000.0f; // Between band 7 and 8

    // Re-sort for proper order when more bands are used
    // For now, we'll keep the simple defaults and let setNumBands handle proper spacing
}

// ==============================================================================
// Configuration API
// ==============================================================================

void SpectrumDisplay::setNumBands(int numBands) {
    numBands_ = std::clamp(numBands, 1, kMaxBands);

    // Recalculate crossover frequencies for even logarithmic spacing
    if (numBands_ > 1) {
        float logMin = std::log2(kMinFreqHz);
        float logMax = std::log2(kMaxFreqHz);
        float step = (logMax - logMin) / static_cast<float>(numBands_);

        for (int i = 0; i < numBands_ - 1; ++i) {
            float logFreq = logMin + step * static_cast<float>(i + 1);
            crossoverFreqs_[static_cast<size_t>(i)] = std::pow(2.0f, logFreq);
        }
    }

    invalid();  // Request redraw
}

void SpectrumDisplay::setCrossoverFrequency(int index, float freqHz) {
    if (index >= 0 && index < numBands_ - 1) {
        crossoverFreqs_[static_cast<size_t>(index)] = std::clamp(freqHz, kMinFreqHz, kMaxFreqHz);
        invalid();  // Request redraw
    }
}

float SpectrumDisplay::getCrossoverFrequency(int index) const {
    if (index >= 0 && index < numBands_ - 1) {
        return crossoverFreqs_[static_cast<size_t>(index)];
    }
    return kMinFreqHz;
}

// ==============================================================================
// Coordinate Conversion
// ==============================================================================

float SpectrumDisplay::freqToX(float freq) const {
    float width = static_cast<float>(getViewSize().getWidth());

    if (freq <= kMinFreqHz) return 0.0f;
    if (freq >= kMaxFreqHz) return width;

    float logPos = std::log2(freq / kMinFreqHz) / kLogRatio;
    return width * logPos;
}

float SpectrumDisplay::xToFreq(float x) const {
    float width = static_cast<float>(getViewSize().getWidth());

    if (x <= 0.0f) return kMinFreqHz;
    if (x >= width) return kMaxFreqHz;

    float logPos = x / width;
    return kMinFreqHz * std::pow(2.0f, logPos * kLogRatio);
}

// ==============================================================================
// CView Overrides
// ==============================================================================

void SpectrumDisplay::draw(VSTGUI::CDrawContext* context) {
    // Get view bounds
    auto viewSize = getViewSize();

    // Draw background
    context->setFillColor(VSTGUI::CColor(0x1A, 0x1A, 0x1E, 0xFF));  // Background Primary
    context->drawRect(viewSize, VSTGUI::kDrawFilled);

    // Draw band regions (semi-transparent)
    drawBandRegions(context);

    // Draw crossover dividers
    drawCrossoverDividers(context);

    // Draw frequency scale
    drawFrequencyScale(context);

    setDirty(false);
}

VSTGUI::CMouseEventResult SpectrumDisplay::onMouseDown(
    VSTGUI::CPoint& where,
    const VSTGUI::CButtonState& buttons) {

    if ((buttons & VSTGUI::kLButton) == 0) {
        return VSTGUI::kMouseEventNotHandled;
    }

    auto viewSize = getViewSize();
    float localX = static_cast<float>(where.x - viewSize.left);

    // Check if clicking on a divider
    int divider = hitTestDivider(localX);
    if (divider >= 0) {
        draggingDivider_ = divider;
        return VSTGUI::kMouseEventHandled;
    }

    // Check if clicking on a band region
    if (listener_) {
        float freq = xToFreq(localX);
        int bandIndex = 0;
        for (int i = 0; i < numBands_ - 1; ++i) {
            if (freq >= crossoverFreqs_[static_cast<size_t>(i)]) {
                bandIndex = i + 1;
            }
        }
        listener_->onBandSelected(bandIndex);
        return VSTGUI::kMouseEventHandled;
    }

    return VSTGUI::kMouseEventNotHandled;
}

VSTGUI::CMouseEventResult SpectrumDisplay::onMouseUp(
    VSTGUI::CPoint& where,
    const VSTGUI::CButtonState& buttons) {

    (void)where;
    (void)buttons;

    if (draggingDivider_ >= 0) {
        draggingDivider_ = -1;
        return VSTGUI::kMouseEventHandled;
    }

    return VSTGUI::kMouseEventNotHandled;
}

VSTGUI::CMouseEventResult SpectrumDisplay::onMouseMoved(
    VSTGUI::CPoint& where,
    const VSTGUI::CButtonState& buttons) {

    if (draggingDivider_ < 0 || ((buttons & VSTGUI::kLButton) == 0)) {
        return VSTGUI::kMouseEventNotHandled;
    }

    auto viewSize = getViewSize();
    float localX = static_cast<float>(where.x - viewSize.left);
    float newFreq = xToFreq(localX);

    // Clamp to valid range
    newFreq = std::clamp(newFreq, kMinFreqHz, kMaxFreqHz);

    // Calculate bounds based on neighboring dividers (with minimum octave spacing)
    float leftBound = kMinFreqHz;
    float rightBound = kMaxFreqHz;

    if (draggingDivider_ > 0) {
        // Left neighbor: must be at least kMinOctaveSpacing octaves higher
        leftBound = crossoverFreqs_[static_cast<size_t>(draggingDivider_ - 1)]
                    * std::pow(2.0f, kMinOctaveSpacing);
    }

    if (draggingDivider_ < numBands_ - 2) {
        // Right neighbor: must be at least kMinOctaveSpacing octaves lower
        rightBound = crossoverFreqs_[static_cast<size_t>(draggingDivider_) + 1]
                     * std::pow(2.0f, -kMinOctaveSpacing);
    }

    // Apply constraints
    newFreq = std::clamp(newFreq, leftBound, rightBound);

    // Update crossover frequency
    crossoverFreqs_[static_cast<size_t>(draggingDivider_)] = newFreq;
    invalid();  // Request redraw

    // Notify listener
    if (listener_) {
        listener_->onCrossoverChanged(draggingDivider_, newFreq);
    }

    return VSTGUI::kMouseEventHandled;
}

// ==============================================================================
// Internal Helpers
// ==============================================================================

void SpectrumDisplay::drawBandRegions(VSTGUI::CDrawContext* context) {
    auto viewSize = getViewSize();
    float height = static_cast<float>(viewSize.getHeight());
    float viewLeft = static_cast<float>(viewSize.left);

    for (int band = 0; band < numBands_; ++band) {
        // Calculate left and right X coordinates for this band
        float leftFreq = (band == 0) ? kMinFreqHz : crossoverFreqs_[static_cast<size_t>(band - 1)];
        float rightFreq = (band == numBands_ - 1) ? kMaxFreqHz : crossoverFreqs_[static_cast<size_t>(band)];

        float leftX = freqToX(leftFreq) + viewLeft;
        float rightX = freqToX(rightFreq) + viewLeft;

        // Create semi-transparent band color
        VSTGUI::CColor bandColor = kBandColors[static_cast<size_t>(band)];
        bandColor.alpha = 64;  // 25% opacity

        // Draw filled rectangle for band region
        VSTGUI::CRect bandRect(leftX, viewSize.top, rightX, viewSize.top + height);
        context->setFillColor(bandColor);
        context->drawRect(bandRect, VSTGUI::kDrawFilled);
    }
}

void SpectrumDisplay::drawCrossoverDividers(VSTGUI::CDrawContext* context) {
    auto viewSize = getViewSize();
    float viewLeft = static_cast<float>(viewSize.left);

    // Divider color
    VSTGUI::CColor dividerColor(0x3A, 0x3A, 0x40, 0xFF);  // Border color

    for (int i = 0; i < numBands_ - 1; ++i) {
        float x = freqToX(crossoverFreqs_[static_cast<size_t>(i)]) + viewLeft;

        // Draw vertical divider line (2px wide)
        context->setLineWidth(2.0);
        context->setFrameColor(dividerColor);
        context->drawLine(
            VSTGUI::CPoint(x, viewSize.top),
            VSTGUI::CPoint(x, viewSize.bottom)
        );

        // Draw small triangular handle at top
        VSTGUI::CGraphicsPath* path = context->createGraphicsPath();
        if (path) {
            path->beginSubpath(VSTGUI::CPoint(x - 6, viewSize.top));
            path->addLine(VSTGUI::CPoint(x + 6, viewSize.top));
            path->addLine(VSTGUI::CPoint(x, viewSize.top + 10));
            path->closeSubpath();

            context->setFillColor(kBandColors[static_cast<size_t>(i)]);
            context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathFilled);
            path->forget();
        }
    }
}

void SpectrumDisplay::drawFrequencyScale(VSTGUI::CDrawContext* context) {
    auto viewSize = getViewSize();
    float viewLeft = static_cast<float>(viewSize.left);
    float viewBottom = static_cast<float>(viewSize.bottom);

    // Font for frequency labels
    VSTGUI::CFontRef font = VSTGUI::kNormalFontSmall;
    context->setFont(font);
    context->setFontColor(VSTGUI::CColor(0x88, 0x88, 0xAA, 0xFF));  // Text Secondary

    // Standard frequency markers
    const float frequencies[] = {20, 50, 100, 200, 500, 1000, 2000, 5000, 10000, 20000};
    const char* labels[] = {"20", "50", "100", "200", "500", "1k", "2k", "5k", "10k", "20k"};

    float y = viewBottom - 5.0f;  // Position at bottom

    for (size_t i = 0; i < sizeof(frequencies) / sizeof(frequencies[0]); ++i) {
        float x = freqToX(frequencies[i]) + viewLeft;

        // Draw tick mark
        context->setLineWidth(1.0);
        context->setFrameColor(VSTGUI::CColor(0x3A, 0x3A, 0x40, 0xFF));
        context->drawLine(
            VSTGUI::CPoint(x, viewSize.bottom - 15),
            VSTGUI::CPoint(x, viewSize.bottom - 10)
        );

        // Draw label (centered on tick)
        VSTGUI::CRect labelRect(x - 20, y - 12, x + 20, y);
        context->drawString(labels[i], labelRect, VSTGUI::kCenterText);
    }
}

int SpectrumDisplay::hitTestDivider(float x) const {
    for (int i = 0; i < numBands_ - 1; ++i) {
        float dividerX = freqToX(crossoverFreqs_[static_cast<size_t>(i)]);
        if (std::abs(x - dividerX) <= kDividerHitTolerance) {
            return i;
        }
    }
    return -1;
}

int SpectrumDisplay::getBandAtFrequency(float freq) const {
    for (int i = 0; i < numBands_ - 1; ++i) {
        if (freq < crossoverFreqs_[static_cast<size_t>(i)]) {
            return i;
        }
    }
    return numBands_ - 1;
}

} // namespace Disrumpo
