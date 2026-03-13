#pragma once

// ==============================================================================
// LfoWaveformDisplay - Animated LFO Waveform Visualizer
// ==============================================================================
// A shared VSTGUI CView subclass that renders 2 cycles of an LFO waveform
// as a filled curve with an animated playback cursor. Updates in real-time
// when shape, symmetry, quantize, unipolar, or phase offset parameters change.
//
// Visual design:
// - Semi-transparent filled area under the waveform curve
// - Solid stroked waveform line (2px, configurable color)
// - Subtle gridlines at zero line and cycle boundary
// - Animated vertical playback cursor sweeping at the LFO rate
// - Unipolar mode shifts display range from [-1,+1] to [0,+1]
//
// Animation:
// - Internal ~30fps timer advances a phase accumulator
// - Frequency set via setFrequencyHz() (free-running) or computed from
//   tempo sync parameters (BPM + note value)
// - Timer starts/stops automatically with view attach/detach
//
// Registered as "LfoWaveformDisplay" via VSTGUI ViewCreator system.
// ==============================================================================

#include "color_utils.h"

#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/uidescription/iviewcreator.h"
#include "vstgui/uidescription/uiviewfactory.h"
#include "vstgui/uidescription/uiviewcreator.h"
#include "vstgui/uidescription/uiattributes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>
#include <vector>

namespace Krate::Plugins {

// ==============================================================================
// Waveform Enum (mirrors DSP Waveform but avoids DSP dependency)
// ==============================================================================

enum class LfoDisplayShape {
    Sine = 0,
    Triangle,
    Sawtooth,
    Square,
    SampleHold,
    SmoothRandom,
    kCount
};

// ==============================================================================
// Pure Waveform Math Functions (UI-thread safe, no state)
// ==============================================================================

namespace LfoWaveformMath {

/// Apply symmetry skew to phase. Mirrors DSP LFO::applySymmetry().
/// @param phase Phase in [0, 1]
/// @param symmetry Symmetry amount in (0, 1), 0.5 = centered
/// @return Skewed phase in [0, 1]
[[nodiscard]] inline float applySymmetry(float phase, float symmetry) noexcept {
    // Guard against division by zero at extremes
    symmetry = std::clamp(symmetry, 0.001f, 0.999f);
    if (phase < symmetry) {
        return 0.5f * (phase / symmetry);
    }
    return 0.5f + 0.5f * ((phase - symmetry) / (1.0f - symmetry));
}

/// Compute waveform value from phase. Pure analytic functions.
/// @param shape Waveform shape
/// @param phase Phase in [0, 1]
/// @return Output in [-1, +1]
[[nodiscard]] inline float computeWaveformValue(LfoDisplayShape shape, float phase) noexcept {
    switch (shape) {
    case LfoDisplayShape::Sine:
        return std::sin(phase * 2.0f * 3.14159265358979f);

    case LfoDisplayShape::Triangle: {
        // 0..0.25 -> 0..1, 0.25..0.75 -> 1..-1, 0.75..1 -> -1..0
        if (phase < 0.25f)
            return phase * 4.0f;
        if (phase < 0.75f)
            return 2.0f - phase * 4.0f;
        return phase * 4.0f - 4.0f;
    }

    case LfoDisplayShape::Sawtooth:
        // 0->1 maps to 1..-1
        return 1.0f - 2.0f * phase;

    case LfoDisplayShape::Square:
        return phase < 0.5f ? 1.0f : -1.0f;

    case LfoDisplayShape::SampleHold:
    case LfoDisplayShape::SmoothRandom:
        // Handled separately with deterministic random
        return 0.0f;

    default:
        return 0.0f;
    }
}

/// Apply quantization to a value.
/// @param value Input in [-1, +1]
/// @param steps Number of quantization steps (0 or 1 = no quantization)
/// @return Quantized value
[[nodiscard]] inline float applyQuantize(float value, int steps) noexcept {
    if (steps < 2) return value;
    float s = static_cast<float>(steps);
    return std::round(value * s) / s;
}

/// Generate deterministic pseudo-random values for S&H / Smooth Random display.
/// Uses a simple LCG seeded per-segment so display is stable across redraws.
[[nodiscard]] inline float deterministicRandom(uint32_t seed) noexcept {
    // Hash-style mixing to spread small seeds across full range
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    seed = seed * 1597334677u;  // Large odd multiplier
    seed ^= seed >> 16;
    return static_cast<float>(seed & 0x7FFFFFFFu) / 1073741823.5f - 1.0f;
}

} // namespace LfoWaveformMath

// ==============================================================================
// LfoWaveformDisplay View
// ==============================================================================

class LfoWaveformDisplay : public VSTGUI::CView {
public:
    static constexpr int kNumPoints = 256;
    static constexpr float kDefaultBpm = 120.0f;
    static constexpr uint32_t kTimerIntervalMs = 33;  // ~30fps

    // =========================================================================
    // Construction / Destruction
    // =========================================================================

    explicit LfoWaveformDisplay(const VSTGUI::CRect& size)
        : CView(size) {
        points_.resize(kNumPoints);
        dirty_ = true;
    }

    LfoWaveformDisplay(const LfoWaveformDisplay& other)
        : CView(other)
        , lfoColor_(other.lfoColor_)
        , shape_(other.shape_)
        , symmetry_(other.symmetry_)
        , quantizeSteps_(other.quantizeSteps_)
        , unipolar_(other.unipolar_)
        , phaseOffset_(other.phaseOffset_)
        , frequencyHz_(other.frequencyHz_)
        , dirty_(true) {
        points_.resize(kNumPoints);
    }

    ~LfoWaveformDisplay() override {
        stopTimer();
    }

    // =========================================================================
    // Parameter Setters (called from controller)
    // =========================================================================

    void setShape(int shapeIndex) {
        auto newShape = static_cast<LfoDisplayShape>(
            std::clamp(shapeIndex, 0, static_cast<int>(LfoDisplayShape::kCount) - 1));
        if (newShape != shape_) {
            shape_ = newShape;
            dirty_ = true;
            invalid();
        }
    }

    void setSymmetry(float symmetry) {
        symmetry = std::clamp(symmetry, 0.0f, 1.0f);
        if (symmetry != symmetry_) {
            symmetry_ = symmetry;
            dirty_ = true;
            invalid();
        }
    }

    void setQuantizeSteps(int steps) {
        if (steps != quantizeSteps_) {
            quantizeSteps_ = steps;
            dirty_ = true;
            invalid();
        }
    }

    void setUnipolar(bool unipolar) {
        if (unipolar != unipolar_) {
            unipolar_ = unipolar;
            dirty_ = true;
            invalid();
        }
    }

    void setPhaseOffset(float offset) {
        offset = std::clamp(offset, 0.0f, 1.0f);
        if (offset != phaseOffset_) {
            phaseOffset_ = offset;
            dirty_ = true;
            invalid();
        }
    }

    /// Set LFO frequency in Hz (for free-running mode).
    void setFrequencyHz(float hz) {
        frequencyHz_ = std::clamp(hz, 0.01f, 50.0f);
        dirty_ = true;
        invalid();
    }

    /// Set LFO depth (0-1). Scales waveform amplitude.
    void setDepth(float depth) {
        depth = std::clamp(depth, 0.0f, 1.0f);
        if (depth != depth_) {
            depth_ = depth;
            dirty_ = true;
            invalid();
        }
    }

    /// Set LFO fade-in time in milliseconds.
    void setFadeInMs(float ms) {
        ms = std::clamp(ms, 0.0f, 5000.0f);
        if (ms != fadeInMs_) {
            fadeInMs_ = ms;
            dirty_ = true;
            invalid();
        }
    }

    // =========================================================================
    // Color Configuration
    // =========================================================================

    void setLfoColor(const VSTGUI::CColor& color) {
        lfoColor_ = color;
        invalid();
    }

    [[nodiscard]] VSTGUI::CColor getLfoColor() const { return lfoColor_; }

    void setLfoColorFromString(const std::string& hexStr) {
        if (hexStr.size() >= 7 && hexStr[0] == '#') {
            auto r = static_cast<uint8_t>(std::stoul(hexStr.substr(1, 2), nullptr, 16));
            auto g = static_cast<uint8_t>(std::stoul(hexStr.substr(3, 2), nullptr, 16));
            auto b = static_cast<uint8_t>(std::stoul(hexStr.substr(5, 2), nullptr, 16));
            lfoColor_ = VSTGUI::CColor(r, g, b, 255);
            invalid();
        }
    }

    [[nodiscard]] std::string getLfoColorString() const {
        char buf[8];
        snprintf(buf, sizeof(buf), "#%02X%02X%02X",
                 lfoColor_.red, lfoColor_.green, lfoColor_.blue);
        return buf;
    }

    // =========================================================================
    // CView Overrides: Attach / Detach (timer lifecycle)
    // =========================================================================

    bool attached(VSTGUI::CView* parent) override {
        if (CView::attached(parent)) {
            startTimer();
            return true;
        }
        return false;
    }

    bool removed(VSTGUI::CView* parent) override {
        stopTimer();
        return CView::removed(parent);
    }

    // =========================================================================
    // CView Override: Drawing
    // =========================================================================

    void draw(VSTGUI::CDrawContext* context) override {
        if (dirty_) {
            recomputePolyline();
            dirty_ = false;
        }

        auto r = getViewSize();

        // ---- Rounded border background ----
        constexpr double kBorderRadius = 6.0;
        {
            auto* bgPath = context->createGraphicsPath();
            if (bgPath) {
                bgPath->addRoundRect(r, kBorderRadius);
                // Subtle dark background
                context->setFillColor(VSTGUI::CColor(22, 22, 26, 255));
                context->drawGraphicsPath(bgPath, VSTGUI::CDrawContext::kPathFilled);
                // Border stroke
                context->setFrameColor(VSTGUI::CColor(60, 60, 65, 255));
                context->setLineWidth(1.0);
                context->setLineStyle(VSTGUI::kLineSolid);
                context->drawGraphicsPath(bgPath, VSTGUI::CDrawContext::kPathStroked);
                bgPath->forget();
            }
        }

        // Margins (inset from border)
        constexpr double kMarginX = 10.0;
        constexpr double kMarginY = 10.0;
        double plotLeft = r.left + kMarginX;
        double plotRight = r.right - kMarginX;
        double plotTop = r.top + kMarginY;
        double plotBottom = r.bottom - kMarginY;
        double plotW = plotRight - plotLeft;
        double plotH = plotBottom - plotTop;

        // Zero/center line Y position
        double zeroY = unipolar_ ? plotBottom : (plotTop + plotH * 0.5);

        // ---- Gridlines ----
        VSTGUI::CColor gridColor(160, 160, 165, 50);

        // Horizontal zero line (dashed)
        {
            const VSTGUI::CLineStyle::CoordVector dashes = {4.0, 4.0};
            VSTGUI::CLineStyle dashedStyle(
                VSTGUI::CLineStyle::kLineCapButt,
                VSTGUI::CLineStyle::kLineJoinMiter,
                0.0, dashes);
            context->setFrameColor(gridColor);
            context->setLineWidth(1.0);
            context->setLineStyle(dashedStyle);
            context->drawLine(VSTGUI::CPoint(plotLeft, zeroY),
                              VSTGUI::CPoint(plotRight, zeroY));
        }

        // Vertical cycle boundary (dashed, at halfway)
        {
            double midX = plotLeft + plotW * 0.5;
            VSTGUI::CColor vGridColor(160, 160, 165, 30);
            const VSTGUI::CLineStyle::CoordVector dashes = {4.0, 4.0};
            VSTGUI::CLineStyle dashedStyle(
                VSTGUI::CLineStyle::kLineCapButt,
                VSTGUI::CLineStyle::kLineJoinMiter,
                0.0, dashes);
            context->setFrameColor(vGridColor);
            context->setLineWidth(1.0);
            context->setLineStyle(dashedStyle);
            context->drawLine(VSTGUI::CPoint(midX, plotTop),
                              VSTGUI::CPoint(midX, plotBottom));
        }

        // ---- Build graphics path ----
        auto* fillPath = context->createGraphicsPath();
        auto* strokePath = context->createGraphicsPath();
        if (!fillPath || !strokePath) {
            if (fillPath) fillPath->forget();
            if (strokePath) strokePath->forget();
            setDirty(false);
            return;
        }

        // Map points to pixel coordinates
        auto mapX = [&](int i) -> double {
            return plotLeft + (static_cast<double>(i) / (kNumPoints - 1)) * plotW;
        };
        auto mapY = [&](float value) -> double {
            // value is in [-1, +1] (bipolar) or [0, +1] (unipolar)
            if (unipolar_) {
                // 0 -> plotBottom, 1 -> plotTop
                return plotBottom - static_cast<double>(value) * plotH;
            }
            // -1 -> plotBottom, +1 -> plotTop
            return plotBottom - static_cast<double>((value + 1.0f) * 0.5f) * plotH;
        };

        // Start fill path from bottom-left
        fillPath->beginSubpath(VSTGUI::CPoint(mapX(0), zeroY));
        // Line up to first point
        fillPath->addLine(VSTGUI::CPoint(mapX(0), mapY(points_[0])));

        strokePath->beginSubpath(VSTGUI::CPoint(mapX(0), mapY(points_[0])));

        for (int i = 1; i < kNumPoints; ++i) {
            VSTGUI::CPoint pt(mapX(i), mapY(points_[i]));
            fillPath->addLine(pt);
            strokePath->addLine(pt);
        }

        // Close fill path back to zero line
        fillPath->addLine(VSTGUI::CPoint(mapX(kNumPoints - 1), zeroY));
        fillPath->closeSubpath();

        // ---- Draw filled area ----
        VSTGUI::CColor fillColor(lfoColor_.red, lfoColor_.green, lfoColor_.blue, 40);
        context->setFillColor(fillColor);
        context->drawGraphicsPath(fillPath, VSTGUI::CDrawContext::kPathFilled);
        fillPath->forget();

        // ---- Draw waveform stroke ----
        context->setFrameColor(lfoColor_);
        context->setLineWidth(2.0);
        context->setLineStyle(VSTGUI::CLineStyle(
            VSTGUI::CLineStyle::kLineCapRound,
            VSTGUI::CLineStyle::kLineJoinRound));
        context->drawGraphicsPath(strokePath, VSTGUI::CDrawContext::kPathStroked);
        strokePath->forget();

        // ---- Draw playback cursor ----
        {
            // playbackPhase_ is in [0, 2) representing position across 2 cycles
            double cursorNorm = static_cast<double>(playbackPhase_) / 2.0;
            double cursorX = plotLeft + cursorNorm * plotW;

            // Glow line (wider, semi-transparent)
            context->setFrameColor(VSTGUI::CColor(
                lfoColor_.red, lfoColor_.green, lfoColor_.blue, 60));
            context->setLineWidth(4.0);
            context->setLineStyle(VSTGUI::kLineSolid);
            context->drawLine(VSTGUI::CPoint(cursorX, plotTop),
                              VSTGUI::CPoint(cursorX, plotBottom));

            // Cursor line (solid, bright)
            context->setFrameColor(VSTGUI::CColor(
                lfoColor_.red, lfoColor_.green, lfoColor_.blue, 200));
            context->setLineWidth(1.5);
            context->drawLine(VSTGUI::CPoint(cursorX, plotTop),
                              VSTGUI::CPoint(cursorX, plotBottom));

            // Dot at waveform intersection
            // Interpolate between nearest points for smooth positioning
            double exactIdx = cursorNorm * (kNumPoints - 1);
            int idx0 = std::clamp(static_cast<int>(exactIdx), 0, kNumPoints - 2);
            int idx1 = idx0 + 1;
            float frac = static_cast<float>(exactIdx - idx0);
            float interpValue = points_[idx0] + frac * (points_[idx1] - points_[idx0]);
            double dotY = mapY(interpValue);

            // Outer glow (large, faint)
            constexpr double kGlowRadius = 8.0;
            VSTGUI::CRect glowRect(cursorX - kGlowRadius, dotY - kGlowRadius,
                                    cursorX + kGlowRadius, dotY + kGlowRadius);
            context->setFillColor(VSTGUI::CColor(
                lfoColor_.red, lfoColor_.green, lfoColor_.blue, 40));
            context->drawEllipse(glowRect, VSTGUI::kDrawFilled);

            // Colored dot
            constexpr double kDotRadius = 5.0;
            VSTGUI::CRect dotRect(cursorX - kDotRadius, dotY - kDotRadius,
                                   cursorX + kDotRadius, dotY + kDotRadius);
            context->setFillColor(lfoColor_);
            context->drawEllipse(dotRect, VSTGUI::kDrawFilled);

            // Bright center
            constexpr double kInnerRadius = 2.5;
            VSTGUI::CRect innerRect(cursorX - kInnerRadius, dotY - kInnerRadius,
                                     cursorX + kInnerRadius, dotY + kInnerRadius);
            context->setFillColor(VSTGUI::CColor(255, 255, 255, 220));
            context->drawEllipse(innerRect, VSTGUI::kDrawFilled);
        }

        setDirty(false);
    }

    CLASS_METHODS(LfoWaveformDisplay, CView)

private:
    // =========================================================================
    // Animation Timer
    // =========================================================================

    void startTimer() {
        if (animTimer_) return;
        animTimer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>(
            [this](VSTGUI::CVSTGUITimer*) {
                // Advance phase by frequency * dt
                float dt = static_cast<float>(kTimerIntervalMs) / 1000.0f;
                playbackPhase_ += frequencyHz_ * dt;
                // Wrap to [0, 2) — display shows 2 cycles
                if (playbackPhase_ >= 2.0f)
                    playbackPhase_ -= 2.0f * std::floor(playbackPhase_ / 2.0f);
                invalid();
            }, kTimerIntervalMs);
    }

    void stopTimer() {
        animTimer_ = nullptr;
    }

    // =========================================================================
    // Polyline Computation
    // =========================================================================

    void recomputePolyline() {
        using namespace LfoWaveformMath;

        for (int i = 0; i < kNumPoints; ++i) {
            // Phase spans 0..2 (two full cycles)
            float phase = static_cast<float>(i) / static_cast<float>(kNumPoints - 1) * 2.0f;
            // Add phase offset
            phase += phaseOffset_;
            // Wrap to single cycle phase [0, 1)
            float cyclePhase = phase - std::floor(phase);

            // Apply symmetry
            float lookupPhase = applySymmetry(cyclePhase, symmetry_);

            float value = 0.0f;

            if (shape_ == LfoDisplayShape::SampleHold) {
                // Deterministic S&H: one random value per segment
                // Use floor of (phase * segmentsPerCycle) as seed
                constexpr int kSegmentsPerCycle = 8;
                int segIndex = static_cast<int>(std::floor(phase * kSegmentsPerCycle));
                value = deterministicRandom(static_cast<uint32_t>(segIndex * 7 + 13));
            } else if (shape_ == LfoDisplayShape::SmoothRandom) {
                // Deterministic smooth random: interpolate between random values
                constexpr int kSegmentsPerCycle = 8;
                float segPhase = phase * kSegmentsPerCycle;
                int segIndex = static_cast<int>(std::floor(segPhase));
                float segFrac = segPhase - std::floor(segPhase);
                float v0 = deterministicRandom(static_cast<uint32_t>(segIndex * 7 + 13));
                float v1 = deterministicRandom(static_cast<uint32_t>((segIndex + 1) * 7 + 13));
                // Smoothstep interpolation
                float t = segFrac * segFrac * (3.0f - 2.0f * segFrac);
                value = v0 + t * (v1 - v0);
            } else {
                value = computeWaveformValue(shape_, lookupPhase);
            }

            // Apply quantization
            value = applyQuantize(value, quantizeSteps_);

            // Apply depth scaling (before unipolar shift)
            value *= depth_;

            // Apply fade-in envelope
            // The display shows 2 cycles. Compute how much time that represents,
            // then apply a linear ramp over the fade-in portion.
            if (fadeInMs_ > 0.0f && frequencyHz_ > 0.0f) {
                // Time position of this point in seconds
                // 2 cycles at frequencyHz_ takes (2 / frequencyHz_) seconds
                float displayDurationSec = 2.0f / frequencyHz_;
                float timeSec = (static_cast<float>(i) / static_cast<float>(kNumPoints - 1))
                                * displayDurationSec;
                float fadeInSec = fadeInMs_ * 0.001f;
                if (timeSec < fadeInSec) {
                    value *= timeSec / fadeInSec;
                }
            }

            // Apply unipolar shift
            if (unipolar_) {
                value = (value + 1.0f) * 0.5f;
            }

            points_[i] = value;
        }
    }

    // =========================================================================
    // State
    // =========================================================================

    VSTGUI::CColor lfoColor_{90, 200, 130, 255};  // Default: modulation green #5AC882
    LfoDisplayShape shape_ = LfoDisplayShape::Sine;
    float symmetry_ = 0.5f;
    int quantizeSteps_ = 0;
    bool unipolar_ = false;
    float phaseOffset_ = 0.0f;
    float depth_ = 1.0f;
    float fadeInMs_ = 0.0f;
    bool dirty_ = true;
    std::vector<float> points_;

    // Animation state
    float frequencyHz_ = 1.0f;       // LFO rate in Hz
    float playbackPhase_ = 0.0f;     // Current phase [0, 2) across 2 displayed cycles
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> animTimer_;
};

// =============================================================================
// ViewCreator Registration
// =============================================================================

struct LfoWaveformDisplayCreator : VSTGUI::ViewCreatorAdapter {
    LfoWaveformDisplayCreator() {
        VSTGUI::UIViewFactory::registerViewCreator(*this);
    }

    VSTGUI::IdStringPtr getViewName() const override {
        return "LfoWaveformDisplay";
    }

    VSTGUI::IdStringPtr getBaseViewName() const override {
        return VSTGUI::UIViewCreator::kCView;
    }

    VSTGUI::UTF8StringPtr getDisplayName() const override {
        return "LFO Waveform Display";
    }

    VSTGUI::CView* create(
        const VSTGUI::UIAttributes& /*attributes*/,
        const VSTGUI::IUIDescription* /*description*/) const override {
        return new LfoWaveformDisplay(VSTGUI::CRect(0, 0, 510, 230));
    }

    bool apply(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
               const VSTGUI::IUIDescription* /*description*/) const override {
        auto* display = dynamic_cast<LfoWaveformDisplay*>(view);
        if (!display) return false;

        if (auto colorStr = attributes.getAttributeValue("lfo-color"))
            display->setLfoColorFromString(*colorStr);

        return true;
    }

    bool getAttributeNames(
        VSTGUI::IViewCreator::StringList& attributeNames) const override {
        attributeNames.emplace_back("lfo-color");
        return true;
    }

    AttrType getAttributeType(const std::string& attributeName) const override {
        if (attributeName == "lfo-color") return kStringType;
        return kUnknownType;
    }

    bool getAttributeValue(VSTGUI::CView* view,
                           const std::string& attributeName,
                           std::string& stringValue,
                           const VSTGUI::IUIDescription* /*desc*/) const override {
        auto* display = dynamic_cast<LfoWaveformDisplay*>(view);
        if (!display) return false;
        if (attributeName == "lfo-color") {
            stringValue = display->getLfoColorString();
            return true;
        }
        return false;
    }
};

/// Inline variable (C++17) -- safe for inclusion from multiple translation units.
inline LfoWaveformDisplayCreator gLfoWaveformDisplayCreator;

} // namespace Krate::Plugins
