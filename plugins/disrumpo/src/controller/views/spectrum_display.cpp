// ==============================================================================
// SpectrumDisplay Implementation
// ==============================================================================
// FR-013: Custom VSTGUI view for displaying frequency band regions
// Real-time FFT spectrum curves with per-band coloring, peak hold, dB scale
// ==============================================================================

#include "spectrum_display.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cstring.h"

#include <cmath>
#include <algorithm>
#include <cstdio>

namespace Disrumpo {

// Band colors from ui-mockups.md
const std::array<VSTGUI::CColor, SpectrumDisplay::kMaxBands> SpectrumDisplay::kBandColors = {{
    VSTGUI::CColor(0xFF, 0x6B, 0x35, 0xFF),  // Band 1: #FF6B35
    VSTGUI::CColor(0x4E, 0xCD, 0xC4, 0xFF),  // Band 2: #4ECDC4
    VSTGUI::CColor(0x95, 0xE8, 0x6B, 0xFF),  // Band 3: #95E86B
    VSTGUI::CColor(0xC7, 0x92, 0xEA, 0xFF),  // Band 4: #C792EA
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
}

SpectrumDisplay::~SpectrumDisplay() {
    stopAnalysis();
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

void SpectrumDisplay::setSweepBandIntensities(
    const std::array<float, 4>& intensities, int numBands) {
    for (int i = 0; i < numBands && i < kMaxBands; ++i) {
        sweepIntensities_[static_cast<size_t>(i)] = intensities[static_cast<size_t>(i)];
    }
    invalid();
}

void SpectrumDisplay::setSweepEnabled(bool enabled) {
    if (sweepEnabled_ != enabled) {
        sweepEnabled_ = enabled;
        if (!enabled) {
            sweepIntensities_.fill(0.0f);
        }
        invalid();
    }
}

void SpectrumDisplay::setHighContrastMode(bool enabled,
                                           const VSTGUI::CColor& borderColor,
                                           const VSTGUI::CColor& bgColor,
                                           const VSTGUI::CColor& accentColor) {
    highContrastEnabled_ = enabled;
    hcBorderColor_ = borderColor;
    hcBgColor_ = bgColor;
    hcAccentColor_ = accentColor;
    invalid();
}

// ==============================================================================
// Spectrum Analyzer API
// ==============================================================================

void SpectrumDisplay::setSpectrumFIFOs(
    Krate::DSP::SpectrumFIFO<8192>* inputFIFO,
    Krate::DSP::SpectrumFIFO<8192>* outputFIFO) {
    inputFIFO_ = inputFIFO;
    outputFIFO_ = outputFIFO;
}

void SpectrumDisplay::startAnalysis(double sampleRate) {
    if (analysisActive_)
        return;

    SpectrumConfig config;
    config.sampleRate = static_cast<float>(sampleRate);
    inputAnalyzer_.prepare(config);
    outputAnalyzer_.prepare(config);

    // ~30fps timer (33ms interval)
    analysisTimer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>(
        [this](VSTGUI::CVSTGUITimer* /*timer*/) {
            constexpr float kDeltaTime = 33.0f / 1000.0f;  // ~30fps
            bool needsRedraw = false;

            if (outputFIFO_) {
                needsRedraw |= outputAnalyzer_.process(outputFIFO_, kDeltaTime);
            }
            if (inputFIFO_ && viewMode_ != SpectrumViewMode::kWet) {
                needsRedraw |= inputAnalyzer_.process(inputFIFO_, kDeltaTime);
            }

            if (needsRedraw) {
                invalid();
            }
        },
        33  // 33ms interval
    );

    analysisActive_ = true;
}

void SpectrumDisplay::stopAnalysis() {
    analysisTimer_ = nullptr;  // SharedPointer releases the timer
    analysisActive_ = false;
    inputFIFO_ = nullptr;
    outputFIFO_ = nullptr;
    inputAnalyzer_.reset();
    outputAnalyzer_.reset();
    invalid();
}

// ==============================================================================
// Frequency Formatting
// ==============================================================================

std::string SpectrumDisplay::formatFrequency(float freqHz, bool precise) {
    char buf[32];
    if (precise) {
        if (freqHz < 1000.0f) {
            std::snprintf(buf, sizeof(buf), "%d Hz", static_cast<int>(freqHz + 0.5f));
        } else {
            float kHz = freqHz / 1000.0f;
            if (kHz >= 10.0f) {
                std::snprintf(buf, sizeof(buf), "%.1f kHz", static_cast<double>(kHz));
            } else {
                std::snprintf(buf, sizeof(buf), "%.2f kHz", static_cast<double>(kHz));
            }
        }
    } else {
        if (freqHz < 1000.0f) {
            std::snprintf(buf, sizeof(buf), "%d", static_cast<int>(freqHz + 0.5f));
        } else {
            float kHz = freqHz / 1000.0f;
            if (kHz >= 10.0f || kHz == std::floor(kHz)) {
                std::snprintf(buf, sizeof(buf), "%.0fk", static_cast<double>(kHz));
            } else {
                std::snprintf(buf, sizeof(buf), "%.1fk", static_cast<double>(kHz));
            }
        }
    }
    return buf;
}

// ==============================================================================
// Drawing
// ==============================================================================

void SpectrumDisplay::draw(VSTGUI::CDrawContext* context) {
    // Get view bounds
    auto viewSize = getViewSize();

    // Draw background (Spec 012 FR-025a: use high contrast colors when enabled)
    if (highContrastEnabled_) {
        context->setFillColor(hcBgColor_);
    } else {
        context->setFillColor(VSTGUI::CColor(0x1A, 0x1A, 0x1E, 0xFF));  // Background Primary
    }
    context->drawRect(viewSize, VSTGUI::kDrawFilled);

    // Layer 1: Band regions (semi-transparent)
    drawBandRegions(context);

    // Layer 2: Spectrum filled areas (per-band colored)
    if (analysisActive_) {
        if (viewMode_ == SpectrumViewMode::kBoth && inputFIFO_) {
            drawSpectrumCurve(context, inputAnalyzer_, 0.2f);
        }
        if (viewMode_ != SpectrumViewMode::kDry && outputFIFO_) {
            drawSpectrumCurve(context, outputAnalyzer_, 0.5f);
        }
        if (viewMode_ == SpectrumViewMode::kDry && inputFIFO_) {
            drawSpectrumCurve(context, inputAnalyzer_, 0.5f);
        }
    }

    // Layer 3: Peak hold lines
    if (analysisActive_) {
        if (viewMode_ == SpectrumViewMode::kBoth && inputFIFO_) {
            drawPeakHoldLine(context, inputAnalyzer_, 80);
        }
        if (viewMode_ != SpectrumViewMode::kDry && outputFIFO_) {
            drawPeakHoldLine(context, outputAnalyzer_, 140);
        }
        if (viewMode_ == SpectrumViewMode::kDry && inputFIFO_) {
            drawPeakHoldLine(context, inputAnalyzer_, 140);
        }
    }

    // Layer 4: Sweep intensity overlay (FR-050)
    if (sweepEnabled_) {
        drawSweepIntensityOverlay(context);
    }

    // Layer 5: dB scale gridlines
    if (analysisActive_) {
        drawDbScale(context);
    }

    // Layer 6: Crossover dividers
    drawCrossoverDividers(context);

    // Layer 6b: Crossover frequency labels
    drawCrossoverLabels(context);

    // Layer 7: Frequency scale
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
        hoveredDivider_ = -1;  // Drag takes priority over hover
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
        invalid();
        return VSTGUI::kMouseEventHandled;
    }

    return VSTGUI::kMouseEventNotHandled;
}

VSTGUI::CMouseEventResult SpectrumDisplay::onMouseExited(
    VSTGUI::CPoint& where,
    const VSTGUI::CButtonState& buttons) {

    (void)where;
    (void)buttons;

    if (hoveredDivider_ >= 0) {
        hoveredDivider_ = -1;
        invalid();
    }
    return VSTGUI::kMouseEventHandled;
}

VSTGUI::CMouseEventResult SpectrumDisplay::onMouseMoved(
    VSTGUI::CPoint& where,
    const VSTGUI::CButtonState& buttons) {

    auto viewSize = getViewSize();
    float localX = static_cast<float>(where.x - viewSize.left);

    // When not dragging, track hover state for frequency labels
    if (draggingDivider_ < 0 || ((buttons & VSTGUI::kLButton) == 0)) {
        int newHover = hitTestDivider(localX);
        if (newHover != hoveredDivider_) {
            hoveredDivider_ = newHover;
            invalid();
        }
        return VSTGUI::kMouseEventNotHandled;
    }

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

    // Divider color (Spec 012 FR-025a: use high contrast border when enabled)
    VSTGUI::CColor dividerColor = highContrastEnabled_ ?
        hcBorderColor_ : VSTGUI::CColor(0x3A, 0x3A, 0x40, 0xFF);

    for (int i = 0; i < numBands_ - 1; ++i) {
        float x = freqToX(crossoverFreqs_[static_cast<size_t>(i)]) + viewLeft;

        // Draw vertical divider line (2px normal, 3px high contrast)
        context->setLineWidth(highContrastEnabled_ ? 3.0 : 2.0);
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

void SpectrumDisplay::drawCrossoverLabels(VSTGUI::CDrawContext* context) {
    if (numBands_ <= 1) return;

    auto viewSize = getViewSize();
    float viewLeft = static_cast<float>(viewSize.left);

    // Label geometry
    constexpr float kLabelY = 13.0f;       // Top of label below triangle (triangle ends at 10)
    constexpr float kLabelHeight = 14.0f;
    constexpr float kLabelPadH = 4.0f;     // Horizontal padding for pill background
    constexpr float kAbbrevHalfWidth = 18.0f;
    constexpr float kPreciseHalfWidth = 30.0f;

    VSTGUI::CFontRef font = VSTGUI::kNormalFontSmaller;
    context->setFont(font);

    // Build label rects and determine which are precise
    struct LabelInfo {
        float centerX = 0.0f;
        bool precise = false;
        std::string text;
        VSTGUI::CRect rect;
    };

    int numDividers = numBands_ - 1;
    std::vector<LabelInfo> labels(static_cast<size_t>(numDividers));

    for (int i = 0; i < numDividers; ++i) {
        auto& label = labels[static_cast<size_t>(i)];
        label.centerX = freqToX(crossoverFreqs_[static_cast<size_t>(i)]) + viewLeft;
        label.precise = (hoveredDivider_ == i || draggingDivider_ == i);
        label.text = formatFrequency(crossoverFreqs_[static_cast<size_t>(i)], label.precise);

        float halfW = label.precise ? kPreciseHalfWidth : kAbbrevHalfWidth;
        float top = static_cast<float>(viewSize.top) + kLabelY;
        label.rect = VSTGUI::CRect(
            label.centerX - halfW, top,
            label.centerX + halfW, top + kLabelHeight
        );
    }

    // Simple collision avoidance: nudge overlapping adjacent labels apart
    for (int i = 0; i < numDividers - 1; ++i) {
        auto& left = labels[static_cast<size_t>(i)];
        auto& right = labels[static_cast<size_t>(i) + 1];
        float overlap = static_cast<float>(left.rect.right - right.rect.left);
        if (overlap > 0.0f) {
            float nudge = (overlap + 2.0f) / 2.0f;  // +2px gap
            left.rect.offset(-nudge, 0);
            left.centerX -= nudge;
            right.rect.offset(nudge, 0);
            right.centerX += nudge;
        }
    }

    // Draw each label
    for (int i = 0; i < numDividers; ++i) {
        const auto& label = labels[static_cast<size_t>(i)];

        if (label.precise) {
            // Precise mode: dark pill background + white text
            VSTGUI::CRect pillRect = label.rect;
            pillRect.inset(-kLabelPadH, -1);

            VSTGUI::CGraphicsPath* pill = context->createGraphicsPath();
            if (pill) {
                pill->addRoundRect(pillRect, 3.0);
                context->setFillColor(VSTGUI::CColor(0x1A, 0x1A, 0x1E, 0xDD));
                context->drawGraphicsPath(pill, VSTGUI::CDrawContext::kPathFilled);
                pill->forget();
            }

            context->setFontColor(VSTGUI::CColor(0xFF, 0xFF, 0xFF, 0xFF));
        } else {
            // Abbreviated mode: band-colored text at reduced opacity
            VSTGUI::CColor textColor = kBandColors[static_cast<size_t>(i)];
            textColor.alpha = 180;  // ~70% opacity
            context->setFontColor(textColor);
        }

        context->drawString(label.text.c_str(), label.rect, VSTGUI::kCenterText);
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

void SpectrumDisplay::drawSweepIntensityOverlay(VSTGUI::CDrawContext* context) {
    auto viewSize = getViewSize();
    float height = static_cast<float>(viewSize.getHeight());
    float viewLeft = static_cast<float>(viewSize.left);

    // Sweep highlight color (accent-secondary with variable alpha)
    constexpr uint8_t kMaxAlpha = 60;  // Max alpha for sweep overlay

    for (int band = 0; band < numBands_; ++band) {
        float intensity = sweepIntensities_[static_cast<size_t>(band)];
        if (intensity <= 0.001f) {
            continue;  // Skip bands with negligible intensity
        }

        // Calculate band X boundaries
        float leftFreq = (band == 0) ? kMinFreqHz : crossoverFreqs_[static_cast<size_t>(band - 1)];
        float rightFreq = (band == numBands_ - 1) ? kMaxFreqHz : crossoverFreqs_[static_cast<size_t>(band)];

        float leftX = freqToX(leftFreq) + viewLeft;
        float rightX = freqToX(rightFreq) + viewLeft;

        // Clamp intensity to reasonable display range
        float clampedIntensity = std::min(intensity, 2.0f);

        // Semi-transparent highlight overlay
        VSTGUI::CColor highlightColor(0x4E, 0xCD, 0xC4, 0xFF);  // accent-secondary
        highlightColor.alpha = static_cast<uint8_t>(clampedIntensity * kMaxAlpha);

        VSTGUI::CRect bandRect(leftX, viewSize.top, rightX, viewSize.top + height);
        context->setFillColor(highlightColor);
        context->drawRect(bandRect, VSTGUI::kDrawFilled);
    }
}

// ==============================================================================
// Spectrum Rendering
// ==============================================================================

float SpectrumDisplay::dbToY(float db) const {
    auto viewSize = getViewSize();
    float viewTop = static_cast<float>(viewSize.top);
    // Reserve 20px at bottom for frequency labels
    float usableHeight = static_cast<float>(viewSize.getHeight()) - 20.0f;

    // Normalize dB to [0, 1] where 0dB = top, -96dB = bottom
    float normalized = (db - kMinDb) / (kMaxDb - kMinDb);
    normalized = std::clamp(normalized, 0.0f, 1.0f);

    // Invert: 0dB at top, -96dB at bottom
    return viewTop + usableHeight * (1.0f - normalized);
}

void SpectrumDisplay::drawSpectrumCurve(
    VSTGUI::CDrawContext* context,
    const SpectrumAnalyzer& analyzer,
    float alphaScale) {

    const auto& smoothedDb = analyzer.getSmoothedDb();
    if (smoothedDb.empty())
        return;

    auto viewSize = getViewSize();
    float viewLeft = static_cast<float>(viewSize.left);
    float baseline = dbToY(kMinDb);  // Bottom of spectrum area

    for (int band = 0; band < numBands_; ++band) {
        // Get band frequency boundaries
        float leftFreq = (band == 0) ? kMinFreqHz
            : crossoverFreqs_[static_cast<size_t>(band - 1)];
        float rightFreq = (band == numBands_ - 1) ? kMaxFreqHz
            : crossoverFreqs_[static_cast<size_t>(band)];

        // Convert to scope indices
        auto leftIdx = static_cast<size_t>(analyzer.freqToScopeIndex(leftFreq));
        auto rightIdx = static_cast<size_t>(std::ceil(analyzer.freqToScopeIndex(rightFreq)));
        if (rightIdx <= leftIdx)
            continue;

        VSTGUI::CGraphicsPath* path = context->createGraphicsPath();
        if (!path)
            continue;

        // Start at baseline, left edge
        float startX = freqToX(leftFreq) + viewLeft;
        path->beginSubpath(VSTGUI::CPoint(startX, baseline));

        // Trace the spectrum curve from left to right
        // Use ~2px step for smooth curves
        float pixelStep = 2.0f;
        float leftX = freqToX(leftFreq);
        float rightX = freqToX(rightFreq);
        float currentX = leftX;

        while (currentX <= rightX) {
            float freq = xToFreq(currentX);
            auto idx = static_cast<size_t>(analyzer.freqToScopeIndex(freq));
            idx = std::clamp(idx, leftIdx, rightIdx - 1);

            float db = smoothedDb[idx];
            float screenX = currentX + viewLeft;
            float screenY = dbToY(db);

            path->addLine(VSTGUI::CPoint(screenX, screenY));
            currentX += pixelStep;
        }

        // Final point at right edge
        {
            size_t idx = std::min(rightIdx - 1, smoothedDb.size() - 1);
            float db = smoothedDb[idx];
            float screenX = freqToX(rightFreq) + viewLeft;
            float screenY = dbToY(db);
            path->addLine(VSTGUI::CPoint(screenX, screenY));
        }

        // Close back to baseline
        float endX = freqToX(rightFreq) + viewLeft;
        path->addLine(VSTGUI::CPoint(endX, baseline));
        path->closeSubpath();

        // Fill with band color at given alpha
        VSTGUI::CColor fillColor = kBandColors[static_cast<size_t>(band)];
        fillColor.alpha = static_cast<uint8_t>(255.0f * alphaScale);

        context->setFillColor(fillColor);
        context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathFilled);
        path->forget();
    }
}

void SpectrumDisplay::drawPeakHoldLine(
    VSTGUI::CDrawContext* context,
    const SpectrumAnalyzer& analyzer,
    uint8_t alpha) {

    const auto& peakDb = analyzer.getPeakDb();
    if (peakDb.empty())
        return;

    auto viewSize = getViewSize();
    float viewLeft = static_cast<float>(viewSize.left);

    context->setLineWidth(1.0);
    context->setFrameColor(VSTGUI::CColor(255, 255, 255, alpha));

    // Draw peak line as a connected path across the entire frequency range
    VSTGUI::CGraphicsPath* path = context->createGraphicsPath();
    if (!path)
        return;

    bool started = false;
    float pixelStep = 2.0f;
    float width = static_cast<float>(viewSize.getWidth());

    for (float currentX = 0.0f; currentX <= width; currentX += pixelStep) {
        float freq = xToFreq(currentX);
        auto idx = static_cast<size_t>(analyzer.freqToScopeIndex(freq));
        idx = std::min(idx, peakDb.size() - 1);

        float db = peakDb[idx];
        // Skip silent regions (below noise floor)
        if (db <= kMinDb + 1.0f)
            continue;

        float screenX = currentX + viewLeft;
        float screenY = dbToY(db);

        if (!started) {
            path->beginSubpath(VSTGUI::CPoint(screenX, screenY));
            started = true;
        } else {
            path->addLine(VSTGUI::CPoint(screenX, screenY));
        }
    }

    if (started) {
        context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathStroked);
    }
    path->forget();
}

void SpectrumDisplay::drawDbScale(VSTGUI::CDrawContext* context) {
    auto viewSize = getViewSize();
    float viewLeft = static_cast<float>(viewSize.left);
    float viewRight = static_cast<float>(viewSize.right);

    // dB gridline levels
    const float dbLevels[] = {0.0f, -12.0f, -24.0f, -48.0f, -96.0f};
    const char* dbLabels[] = {"0", "-12", "-24", "-48", "-96"};

    // Faint gridline color
    VSTGUI::CColor gridColor(0x3A, 0x3A, 0x40, 0x60);
    VSTGUI::CColor labelColor(0x88, 0x88, 0xAA, 0x80);

    VSTGUI::CFontRef font = VSTGUI::kNormalFontSmaller;
    context->setFont(font);

    for (size_t i = 0; i < sizeof(dbLevels) / sizeof(dbLevels[0]); ++i) {
        float y = dbToY(dbLevels[i]);

        // Draw horizontal gridline
        context->setLineWidth(1.0);
        context->setFrameColor(gridColor);
        context->drawLine(
            VSTGUI::CPoint(viewLeft, y),
            VSTGUI::CPoint(viewRight, y)
        );

        // Draw dB label on right edge
        VSTGUI::CRect labelRect(viewRight - 30, y - 6, viewRight - 2, y + 6);
        context->setFontColor(labelColor);
        context->drawString(dbLabels[i], labelRect, VSTGUI::kRightText);
    }
}

} // namespace Disrumpo
