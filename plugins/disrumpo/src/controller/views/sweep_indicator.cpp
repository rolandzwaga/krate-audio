// ==============================================================================
// SweepIndicator Implementation
// ==============================================================================

#include "sweep_indicator.h"
#include "vstgui/lib/cgraphicspath.h"

#include <algorithm>

namespace Disrumpo {

// ==============================================================================
// Constructor
// ==============================================================================

SweepIndicator::SweepIndicator(const VSTGUI::CRect& size)
    : CView(size)
{
    setTransparency(true);  // Allow underlying view to show through
}

// ==============================================================================
// Public API
// ==============================================================================

float SweepIndicator::freqToX(float freq) const noexcept {
    freq = std::clamp(freq, kMinFreqHz, kMaxFreqHz);
    const auto width = static_cast<float>(getViewSize().getWidth());
    return width * std::log2(freq / kMinFreqHz) / kLogRatio;
}

float SweepIndicator::xToFreq(float x) const noexcept {
    const auto width = static_cast<float>(getViewSize().getWidth());
    x = std::clamp(x, 0.0f, width);
    return kMinFreqHz * std::pow(2.0f, x / width * kLogRatio);
}

// ==============================================================================
// CView Overrides
// ==============================================================================

void SweepIndicator::draw(VSTGUI::CDrawContext* context) {
    if (!enabled_ || intensity_ <= 0.0f) {
        return;
    }

    CView::draw(context);

    // Draw the appropriate curve based on falloff mode
    if (falloffMode_ == SweepFalloff::Smooth) {
        drawGaussianCurve(context);
    } else {
        drawTriangularCurve(context);
    }

    // Always draw center line
    drawCenterLine(context);
}

// ==============================================================================
// Rendering Helpers
// ==============================================================================

void SweepIndicator::drawGaussianCurve(VSTGUI::CDrawContext* context) {
    const auto& rect = getViewSize();
    const auto width = static_cast<float>(rect.getWidth());
    const auto height = static_cast<float>(rect.getHeight());

    // Create path for Gaussian curve
    auto path = VSTGUI::owned(context->createGraphicsPath());
    if (!path) {
        return;
    }

    // Calculate center X position
    const float centerX = freqToX(centerFreq_);

    // Start path at bottom-left of curve
    const float startX = std::max(0.0f, centerX - width * 0.5f);
    path->beginSubpath(VSTGUI::CPoint(rect.left + startX, rect.bottom));

    // Draw Gaussian curve points
    for (int i = 0; i <= kCurveResolution; ++i) {
        const float t = static_cast<float>(i) / static_cast<float>(kCurveResolution);
        const float x = startX + t * width;

        if (x > width) break;

        // Convert X to frequency, then calculate octave distance from center
        const float freq = xToFreq(x);
        const float octaveDistance = std::abs(std::log2(freq / centerFreq_));

        // Calculate intensity using Gaussian falloff (SC-001, SC-002, SC-003)
        const float curveIntensity = calculateGaussianIntensity(octaveDistance) * intensity_;

        // Map intensity to Y position (0 at bottom, 1 at top)
        const float y = height * (1.0f - curveIntensity);

        path->addLine(VSTGUI::CPoint(rect.left + x, rect.top + y));
    }

    // Close path at bottom
    const float endX = std::min(width, centerX + width * 0.5f);
    path->addLine(VSTGUI::CPoint(rect.left + endX, rect.bottom));
    path->closeSubpath();

    // Fill with semi-transparent color
    VSTGUI::CColor fillColor = indicatorColor_;
    fillColor.alpha = static_cast<uint8_t>(255 * kAlpha * intensity_);

    context->setFillColor(fillColor);
    context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathFilled);

    // Draw outline
    VSTGUI::CColor strokeColor = indicatorColor_;
    strokeColor.alpha = static_cast<uint8_t>(255 * 0.8f);
    context->setFrameColor(strokeColor);
    context->setLineWidth(1.5);
    context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathStroked);
}

void SweepIndicator::drawTriangularCurve(VSTGUI::CDrawContext* context) {
    const auto& rect = getViewSize();
    const auto height = static_cast<float>(rect.getHeight());

    // Create path for triangular curve
    auto path = VSTGUI::owned(context->createGraphicsPath());
    if (!path) {
        return;
    }

    // Calculate key X positions
    const float centerX = freqToX(centerFreq_);

    // Calculate edge frequencies (center +/- width/2 octaves)
    const float lowFreq = centerFreq_ / std::pow(2.0f, widthOctaves_ * 0.5f);
    const float highFreq = centerFreq_ * std::pow(2.0f, widthOctaves_ * 0.5f);
    const float lowX = freqToX(std::max(lowFreq, kMinFreqHz));
    const float highX = freqToX(std::min(highFreq, kMaxFreqHz));

    // Triangle path: left edge -> peak -> right edge
    path->beginSubpath(VSTGUI::CPoint(rect.left + lowX, rect.bottom));
    path->addLine(VSTGUI::CPoint(rect.left + centerX, rect.top + height * (1.0f - intensity_)));
    path->addLine(VSTGUI::CPoint(rect.left + highX, rect.bottom));
    path->closeSubpath();

    // Fill with semi-transparent color
    VSTGUI::CColor fillColor = indicatorColor_;
    fillColor.alpha = static_cast<uint8_t>(255 * kAlpha * intensity_);

    context->setFillColor(fillColor);
    context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathFilled);

    // Draw outline
    VSTGUI::CColor strokeColor = indicatorColor_;
    strokeColor.alpha = static_cast<uint8_t>(255 * 0.8f);
    context->setFrameColor(strokeColor);
    context->setLineWidth(1.5);
    context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathStroked);
}

void SweepIndicator::drawCenterLine(VSTGUI::CDrawContext* context) {
    const auto& rect = getViewSize();
    const float centerX = freqToX(centerFreq_);

    // Draw vertical center line (FR-043)
    VSTGUI::CColor lineColor = indicatorColor_;
    lineColor.alpha = 200;

    context->setFrameColor(lineColor);
    context->setLineWidth(2.0);
    context->drawLine(
        VSTGUI::CPoint(rect.left + centerX, rect.top),
        VSTGUI::CPoint(rect.left + centerX, rect.bottom)
    );
}

float SweepIndicator::calculateGaussianIntensity(float distanceOctaves) const noexcept {
    // Gaussian falloff: sigma = width / 2 (so 1 sigma = half width)
    // Per SC-001, SC-002, SC-003:
    // - At center (0 sigma): intensity = 1.0
    // - At 1 sigma: intensity = 0.606
    // - At 2 sigma: intensity = 0.135
    const float sigma = widthOctaves_ * 0.5f;
    if (sigma <= 0.0f) {
        return distanceOctaves == 0.0f ? 1.0f : 0.0f;
    }

    const float x = distanceOctaves / sigma;
    return std::exp(-0.5f * x * x);
}

float SweepIndicator::calculateLinearIntensity(float distanceOctaves) const noexcept {
    // Linear falloff: 1 at center, 0 at edge (half-width)
    // Per SC-004, SC-005: edge = 0.0, beyond edge = 0.0
    const float halfWidth = widthOctaves_ * 0.5f;
    if (halfWidth <= 0.0f || distanceOctaves >= halfWidth) {
        return 0.0f;
    }

    return 1.0f - (distanceOctaves / halfWidth);
}

} // namespace Disrumpo
