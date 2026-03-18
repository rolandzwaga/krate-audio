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

#include "animated_mod_display.h"
#include "mod_display_utils.h"
#include "mod_display_creator.h"

#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"

#include <algorithm>
#include <cmath>
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
[[nodiscard]] inline float applySymmetry(float phase, float symmetry) noexcept {
    symmetry = std::clamp(symmetry, 0.001f, 0.999f);
    if (phase < symmetry) {
        return 0.5f * (phase / symmetry);
    }
    return 0.5f + 0.5f * ((phase - symmetry) / (1.0f - symmetry));
}

/// Compute waveform value from phase. Pure analytic functions.
[[nodiscard]] inline float computeWaveformValue(LfoDisplayShape shape, float phase) noexcept {
    switch (shape) {
    case LfoDisplayShape::Sine:
        return std::sin(phase * 2.0f * 3.14159265358979f);

    case LfoDisplayShape::Triangle: {
        if (phase < 0.25f)
            return phase * 4.0f;
        if (phase < 0.75f)
            return 2.0f - phase * 4.0f;
        return phase * 4.0f - 4.0f;
    }

    case LfoDisplayShape::Sawtooth:
        return 1.0f - 2.0f * phase;

    case LfoDisplayShape::Square:
        return phase < 0.5f ? 1.0f : -1.0f;

    case LfoDisplayShape::SampleHold:
    case LfoDisplayShape::SmoothRandom:
        return 0.0f;

    default:
        return 0.0f;
    }
}

/// Apply quantization to a value.
[[nodiscard]] inline float applyQuantize(float value, int steps) noexcept {
    if (steps < 2) return value;
    float s = static_cast<float>(steps);
    return std::round(value * s) / s;
}

/// Generate deterministic pseudo-random values for S&H / Smooth Random display.
[[nodiscard]] inline float deterministicRandom(uint32_t seed) noexcept {
    seed ^= seed << 13;
    seed ^= seed >> 17;
    seed ^= seed << 5;
    seed = seed * 1597334677u;
    seed ^= seed >> 16;
    return static_cast<float>(seed & 0x7FFFFFFFu) / 1073741823.5f - 1.0f;
}

} // namespace LfoWaveformMath

// ==============================================================================
// LfoWaveformDisplay View
// ==============================================================================

class LfoWaveformDisplay : public AnimatedModDisplay {
public:
    static constexpr int kNumPoints = 256;
    static constexpr float kDefaultBpm = 120.0f;

    // =========================================================================
    // Construction / Destruction
    // =========================================================================

    explicit LfoWaveformDisplay(const VSTGUI::CRect& size)
        : AnimatedModDisplay(size) {
        points_.resize(kNumPoints);
        dirty_ = true;
    }

    LfoWaveformDisplay(const LfoWaveformDisplay& other)
        : AnimatedModDisplay(other)
        , shape_(other.shape_)
        , symmetry_(other.symmetry_)
        , quantizeSteps_(other.quantizeSteps_)
        , unipolar_(other.unipolar_)
        , phaseOffset_(other.phaseOffset_)
        , frequencyHz_(other.frequencyHz_)
        , depth_(other.depth_)
        , fadeInMs_(other.fadeInMs_)
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

    void setFrequencyHz(float hz) {
        frequencyHz_ = std::clamp(hz, 0.01f, 50.0f);
        dirty_ = true;
        invalid();
    }

    void setDepth(float depth) {
        depth = std::clamp(depth, 0.0f, 1.0f);
        if (depth != depth_) {
            depth_ = depth;
            dirty_ = true;
            invalid();
        }
    }

    void setFadeInMs(float ms) {
        ms = std::clamp(ms, 0.0f, 5000.0f);
        if (ms != fadeInMs_) {
            fadeInMs_ = ms;
            dirty_ = true;
            invalid();
        }
    }

    // =========================================================================
    // Drawing
    // =========================================================================

    void draw(VSTGUI::CDrawContext* context) override {
        if (dirty_) {
            recomputePolyline();
            dirty_ = false;
        }

        auto r = getViewSize();
        drawRoundedBackground(context, r);

        // Margins
        constexpr double kMarginX = 10.0;
        constexpr double kMarginY = 10.0;
        double plotLeft = r.left + kMarginX;
        double plotRight = r.right - kMarginX;
        double plotTop = r.top + kMarginY;
        double plotBottom = r.bottom - kMarginY;
        double plotW = plotRight - plotLeft;
        double plotH = plotBottom - plotTop;

        double zeroY = unipolar_ ? plotBottom : (plotTop + plotH * 0.5);

        // Gridlines
        drawDashedLine(context,
            {plotLeft, zeroY}, {plotRight, zeroY},
            VSTGUI::CColor(160, 160, 165, 50));

        drawDashedLine(context,
            {plotLeft + plotW * 0.5, plotTop}, {plotLeft + plotW * 0.5, plotBottom},
            VSTGUI::CColor(160, 160, 165, 30));

        // ---- Build graphics paths ----
        auto* fillPath = context->createGraphicsPath();
        auto* strokePath = context->createGraphicsPath();
        if (!fillPath || !strokePath) {
            if (fillPath) fillPath->forget();
            if (strokePath) strokePath->forget();
            setDirty(false);
            return;
        }

        auto mapX = [&](int i) -> double {
            return plotLeft + (static_cast<double>(i) / (kNumPoints - 1)) * plotW;
        };
        auto mapY = [&](float value) -> double {
            if (unipolar_) {
                return plotBottom - static_cast<double>(value) * plotH;
            }
            return plotBottom - static_cast<double>((value + 1.0f) * 0.5f) * plotH;
        };

        fillPath->beginSubpath(VSTGUI::CPoint(mapX(0), zeroY));
        fillPath->addLine(VSTGUI::CPoint(mapX(0), mapY(points_[0])));
        strokePath->beginSubpath(VSTGUI::CPoint(mapX(0), mapY(points_[0])));

        for (int i = 1; i < kNumPoints; ++i) {
            VSTGUI::CPoint pt(mapX(i), mapY(points_[i]));
            fillPath->addLine(pt);
            strokePath->addLine(pt);
        }

        fillPath->addLine(VSTGUI::CPoint(mapX(kNumPoints - 1), zeroY));
        fillPath->closeSubpath();

        // Filled area
        VSTGUI::CColor fillColor(color_.red, color_.green, color_.blue, 40);
        context->setFillColor(fillColor);
        context->drawGraphicsPath(fillPath, VSTGUI::CDrawContext::kPathFilled);
        fillPath->forget();

        // Waveform stroke
        context->setFrameColor(color_);
        context->setLineWidth(2.0);
        context->setLineStyle(VSTGUI::CLineStyle(
            VSTGUI::CLineStyle::kLineCapRound,
            VSTGUI::CLineStyle::kLineJoinRound));
        context->drawGraphicsPath(strokePath, VSTGUI::CDrawContext::kPathStroked);
        strokePath->forget();

        // ---- Playback cursor ----
        {
            double cursorNorm = static_cast<double>(playbackPhase_) / 2.0;
            double cursorX = plotLeft + cursorNorm * plotW;

            // Glow line
            context->setFrameColor(VSTGUI::CColor(
                color_.red, color_.green, color_.blue, 60));
            context->setLineWidth(4.0);
            context->setLineStyle(VSTGUI::kLineSolid);
            context->drawLine(VSTGUI::CPoint(cursorX, plotTop),
                              VSTGUI::CPoint(cursorX, plotBottom));

            // Cursor line
            context->setFrameColor(VSTGUI::CColor(
                color_.red, color_.green, color_.blue, 200));
            context->setLineWidth(1.5);
            context->drawLine(VSTGUI::CPoint(cursorX, plotTop),
                              VSTGUI::CPoint(cursorX, plotBottom));

            // Dot at waveform intersection
            double exactIdx = cursorNorm * (kNumPoints - 1);
            int idx0 = std::clamp(static_cast<int>(exactIdx), 0, kNumPoints - 2);
            int idx1 = idx0 + 1;
            float frac = static_cast<float>(exactIdx - idx0);
            float interpValue = points_[idx0] + frac * (points_[idx1] - points_[idx0]);
            double dotY = mapY(interpValue);

            drawGlowDot(context, cursorX, dotY, color_, 8.0, 5.0, 2.5, 40, 220);
        }

        setDirty(false);
    }

    CLASS_METHODS(LfoWaveformDisplay, AnimatedModDisplay)

protected:
    void onTimerTick() override {
        float dt = static_cast<float>(kTimerIntervalMs) / 1000.0f;
        playbackPhase_ += frequencyHz_ * dt;
        if (playbackPhase_ >= 2.0f)
            playbackPhase_ -= 2.0f * std::floor(playbackPhase_ / 2.0f);
    }

private:
    // =========================================================================
    // Polyline Computation
    // =========================================================================

    void recomputePolyline() {
        using namespace LfoWaveformMath;

        for (int i = 0; i < kNumPoints; ++i) {
            float phase = static_cast<float>(i) / static_cast<float>(kNumPoints - 1) * 2.0f;
            phase += phaseOffset_;
            float cyclePhase = phase - std::floor(phase);
            float lookupPhase = applySymmetry(cyclePhase, symmetry_);

            float value = 0.0f;

            if (shape_ == LfoDisplayShape::SampleHold) {
                constexpr int kSegmentsPerCycle = 8;
                int segIndex = static_cast<int>(std::floor(phase * kSegmentsPerCycle));
                value = deterministicRandom(static_cast<uint32_t>(segIndex * 7 + 13));
            } else if (shape_ == LfoDisplayShape::SmoothRandom) {
                constexpr int kSegmentsPerCycle = 8;
                float segPhase = phase * kSegmentsPerCycle;
                int segIndex = static_cast<int>(std::floor(segPhase));
                float segFrac = segPhase - std::floor(segPhase);
                float v0 = deterministicRandom(static_cast<uint32_t>(segIndex * 7 + 13));
                float v1 = deterministicRandom(static_cast<uint32_t>((segIndex + 1) * 7 + 13));
                float t = segFrac * segFrac * (3.0f - 2.0f * segFrac);
                value = v0 + t * (v1 - v0);
            } else {
                value = computeWaveformValue(shape_, lookupPhase);
            }

            value = applyQuantize(value, quantizeSteps_);
            value *= depth_;

            if (fadeInMs_ > 0.0f && frequencyHz_ > 0.0f) {
                float displayDurationSec = 2.0f / frequencyHz_;
                float timeSec = (static_cast<float>(i) / static_cast<float>(kNumPoints - 1))
                                * displayDurationSec;
                float fadeInSec = fadeInMs_ * 0.001f;
                if (timeSec < fadeInSec) {
                    value *= timeSec / fadeInSec;
                }
            }

            if (unipolar_) {
                value = (value + 1.0f) * 0.5f;
            }

            points_[i] = value;
        }
    }

    // =========================================================================
    // State
    // =========================================================================

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
    float frequencyHz_ = 1.0f;
    float playbackPhase_ = 0.0f;
};

inline ModDisplayCreator<LfoWaveformDisplay> gLfoWaveformDisplayCreator{
    "LfoWaveformDisplay", "LFO Waveform Display", "lfo-color", {0, 0, 510, 230}};

} // namespace Krate::Plugins
