#pragma once

// ==============================================================================
// RunglerDisplay - Benjolin Rungler Modulation Visualizer
// ==============================================================================
// A shared VSTGUI CView subclass that renders the Rungler modulation source as:
//   Left:  XY phase plot of Osc1 vs Osc2 with fading trail (cross-mod Lissajous)
//   Right: Top - scrolling staircase of stepped CV output
//          Bottom - shift register bits as lit/unlit dots
//
// The display runs its own lightweight rungler simulation on the UI thread
// at ~30fps, mirroring the DSP parameters set from the controller.
// This is purely visual -- it does NOT read from the audio thread.
//
// Registered as "RunglerDisplay" via VSTGUI ViewCreator system.
// ==============================================================================

#include "animated_mod_display.h"
#include "mod_display_utils.h"
#include "mod_display_creator.h"

#include "vstgui/lib/cdrawcontext.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace Krate::Plugins {

class RunglerDisplay : public AnimatedModDisplay {
public:
    static constexpr int kTrailLength = 512;
    static constexpr int kHistoryLength = 256;
    static constexpr int kMaxBits = 16;
    static constexpr int kStepsPerFrame = 64;

    // =========================================================================
    // Construction / Destruction
    // =========================================================================

    explicit RunglerDisplay(const VSTGUI::CRect& size)
        : AnimatedModDisplay(size) {
        trailX_.fill(0.0f);
        trailY_.fill(0.0f);
        history_.fill(0.0f);
        resetState();
    }

    RunglerDisplay(const RunglerDisplay& other)
        : AnimatedModDisplay(other)
        , osc1Freq_(other.osc1Freq_)
        , osc2Freq_(other.osc2Freq_)
        , depth_(other.depth_)
        , filterAmount_(other.filterAmount_)
        , bits_(other.bits_)
        , loopMode_(other.loopMode_) {
        trailX_.fill(0.0f);
        trailY_.fill(0.0f);
        history_.fill(0.0f);
        resetState();
    }

    ~RunglerDisplay() override {
        stopTimer();
    }

    // =========================================================================
    // Parameter Setters (called from controller)
    // =========================================================================

    void setOsc1Freq(float hz) {
        osc1Freq_ = std::clamp(hz, 0.1f, 100.0f);
    }

    void setOsc2Freq(float hz) {
        osc2Freq_ = std::clamp(hz, 0.1f, 100.0f);
    }

    void setDepth(float depth) {
        depth_ = std::clamp(depth, 0.0f, 1.0f);
    }

    void setFilterAmount(float amount) {
        filterAmount_ = std::clamp(amount, 0.0f, 1.0f);
    }

    void setBits(int bits) {
        bits = std::clamp(bits, 4, kMaxBits);
        if (bits != bits_) {
            bits_ = bits;
            registerMask_ = (1u << static_cast<uint32_t>(bits)) - 1u;
            registerState_ &= registerMask_;
            invalid();
        }
    }

    void setLoopMode(bool loop) {
        if (loop != loopMode_) {
            loopMode_ = loop;
            resetState();
            trailHead_ = 0;
            trailCount_ = 0;
            historyHead_ = 0;
            historyCount_ = 0;
            trailX_.fill(0.0f);
            trailY_.fill(0.0f);
            history_.fill(0.0f);
            invalid();
        }
    }

    // =========================================================================
    // Drawing
    // =========================================================================

    void draw(VSTGUI::CDrawContext* context) override {
        auto r = getViewSize();
        drawRoundedBackground(context, r);

        // Layout: left = XY plot, right = CV + bits
        constexpr double kMargin = 10.0;
        constexpr double kGap = 12.0;
        double contentW = r.getWidth() - 2.0 * kMargin;
        double contentH = r.getHeight() - 2.0 * kMargin;

        double xySize = std::min(contentH, contentW * 0.42);
        double xyLeft = r.left + kMargin;
        double xyTop = r.top + kMargin + (contentH - xySize) * 0.5;

        double rpLeft = xyLeft + xySize + kGap;
        double rpRight = r.right - kMargin;
        double rpTop = r.top + kMargin;
        double rpBottom = r.bottom - kMargin;
        double rpW = rpRight - rpLeft;
        double rpH = rpBottom - rpTop;

        drawXYPlot(context, xyLeft, xyTop, xySize, xySize);

        constexpr double kBitsRowHeight = 28.0;
        constexpr double kInnerGap = 6.0;
        double cvH = rpH - kBitsRowHeight - kInnerGap;
        drawCVStaircase(context, rpLeft, rpTop, rpW, cvH);
        drawShiftRegister(context, rpLeft, rpTop + cvH + kInnerGap, rpW, kBitsRowHeight);

        setDirty(false);
    }

    CLASS_METHODS(RunglerDisplay, AnimatedModDisplay)

protected:
    void onTimerTick() override {
        for (int i = 0; i < kStepsPerFrame; ++i)
            stepRungler();
        pushHistory();
    }

private:
    // =========================================================================
    // XY Phase Plot (Osc1 vs Osc2 Lissajous)
    // =========================================================================

    void drawXYPlot(VSTGUI::CDrawContext* context,
                    double left, double top, double w, double h) const {
        VSTGUI::CRect xyRect(left, top, left + w, top + h);
        drawSubPanel(context, xyRect);
        drawCrosshairGrid(context, left, top, w, h);

        if (trailCount_ < 2) return;

        constexpr double kPadding = 8.0;
        RingBufferView trailXView{trailX_.data(), trailHead_,
                                   std::min(trailCount_, kTrailLength), kTrailLength};
        RingBufferView trailYView{trailY_.data(), trailHead_,
                                   std::min(trailCount_, kTrailLength), kTrailLength};
        drawXYTrail(context,
            left + kPadding, top + kPadding,
            w - 2.0 * kPadding, h - 2.0 * kPadding,
            trailXView, trailYView, color_);
    }

    // =========================================================================
    // CV Staircase Drawing (scrolling stepped voltage)
    // =========================================================================

    void drawCVStaircase(VSTGUI::CDrawContext* context,
                         double left, double top, double w, double h) const {
        VSTGUI::CRect tsRect(left, top, left + w, top + h);
        drawSubPanel(context, tsRect);

        // Reference line at midpoint
        double midY = top + h * 0.5;
        drawDashedLine(context,
            {left + 4, midY}, {left + w - 4, midY},
            VSTGUI::CColor(160, 160, 165, 30));

        int count = std::min(historyCount_, kHistoryLength);
        if (count < 2) return;

        constexpr double kPadX = 4.0;
        constexpr double kPadY = 8.0;
        double plotLeft = left + kPadX;
        double plotW = w - 2.0 * kPadX;
        double plotTop = top + kPadY;
        double plotH = h - 2.0 * kPadY;

        auto mapX = [&](int i) -> double {
            return plotLeft + (static_cast<double>(i) / (count - 1)) * plotW;
        };
        // CV is [0, 1] -> map to full plot height (1=top, 0=bottom)
        auto mapY = [&](float v) -> double {
            return plotTop + (1.0 - static_cast<double>(v)) * plotH;
        };

        auto getHistoryValue = [&](int i) -> float {
            int idx = (historyHead_ - count + i + kHistoryLength) % kHistoryLength;
            return history_[idx];
        };

        // Build staircase paths
        auto* fillPath = context->createGraphicsPath();
        auto* strokePath = context->createGraphicsPath();
        if (!fillPath || !strokePath) {
            if (fillPath) fillPath->forget();
            if (strokePath) strokePath->forget();
            return;
        }

        double bottomY = plotTop + plotH;
        fillPath->beginSubpath(VSTGUI::CPoint(mapX(0), bottomY));

        float prevVal = getHistoryValue(0);
        double prevX = mapX(0);
        fillPath->addLine(VSTGUI::CPoint(prevX, mapY(prevVal)));
        strokePath->beginSubpath(VSTGUI::CPoint(prevX, mapY(prevVal)));

        for (int i = 1; i < count; ++i) {
            float val = getHistoryValue(i);
            double x = mapX(i);

            // Horizontal step then vertical step (staircase)
            fillPath->addLine(VSTGUI::CPoint(x, mapY(prevVal)));
            fillPath->addLine(VSTGUI::CPoint(x, mapY(val)));
            strokePath->addLine(VSTGUI::CPoint(x, mapY(prevVal)));
            strokePath->addLine(VSTGUI::CPoint(x, mapY(val)));

            prevVal = val;
        }

        fillPath->addLine(VSTGUI::CPoint(mapX(count - 1), bottomY));
        fillPath->closeSubpath();

        context->setFillColor(VSTGUI::CColor(color_.red, color_.green, color_.blue, 25));
        context->drawGraphicsPath(fillPath, VSTGUI::CDrawContext::kPathFilled);
        fillPath->forget();

        context->setFrameColor(color_);
        context->setLineWidth(1.5);
        context->setLineStyle(VSTGUI::CLineStyle(
            VSTGUI::CLineStyle::kLineCapButt,
            VSTGUI::CLineStyle::kLineJoinMiter));
        context->drawGraphicsPath(strokePath, VSTGUI::CDrawContext::kPathStroked);
        strokePath->forget();

        // Current value dot
        if (count > 0) {
            float newestVal = getHistoryValue(count - 1);
            drawValueDot(context, mapX(count - 1), mapY(newestVal), color_);
        }
    }

    // =========================================================================
    // Shift Register Bit Display
    // =========================================================================

    void drawShiftRegister(VSTGUI::CDrawContext* context,
                           double left, double top, double w, double h) const {
        VSTGUI::CRect bgRect(left, top, left + w, top + h);
        drawSubPanel(context, bgRect);

        int numBits = bits_;
        constexpr double kDotRadius = 5.0;
        constexpr double kDotSpacing = 4.0;
        double totalWidth = numBits * (kDotRadius * 2.0 + kDotSpacing) - kDotSpacing;
        double startX = left + (w - totalWidth) * 0.5;
        double centerY = top + h * 0.5;

        for (int i = 0; i < numBits; ++i) {
            int bitIdx = numBits - 1 - i;
            bool bitOn = (registerState_ >> static_cast<uint32_t>(bitIdx)) & 1u;

            double cx = startX + i * (kDotRadius * 2.0 + kDotSpacing) + kDotRadius;
            VSTGUI::CRect dotRect(cx - kDotRadius, centerY - kDotRadius,
                                   cx + kDotRadius, centerY + kDotRadius);

            if (bitOn) {
                VSTGUI::CRect glowRect(cx - kDotRadius - 2, centerY - kDotRadius - 2,
                                        cx + kDotRadius + 2, centerY + kDotRadius + 2);
                context->setFillColor(VSTGUI::CColor(color_.red, color_.green, color_.blue, 40));
                context->drawEllipse(glowRect, VSTGUI::kDrawFilled);

                context->setFillColor(color_);
                context->drawEllipse(dotRect, VSTGUI::kDrawFilled);
            } else {
                context->setFillColor(VSTGUI::CColor(40, 40, 45, 255));
                context->drawEllipse(dotRect, VSTGUI::kDrawFilled);
                context->setFrameColor(VSTGUI::CColor(60, 60, 65, 255));
                context->setLineWidth(1.0);
                context->setLineStyle(VSTGUI::kLineSolid);
                context->drawEllipse(dotRect, VSTGUI::kDrawStroked);
            }

            // Highlight the 3 DAC bits (top 3) with a small marker
            if (bitIdx >= numBits - 3) {
                constexpr double kMarkerR = 1.5;
                double markerY = centerY + kDotRadius + 5.0;
                VSTGUI::CRect markerRect(cx - kMarkerR, markerY - kMarkerR,
                                          cx + kMarkerR, markerY + kMarkerR);
                context->setFillColor(VSTGUI::CColor(color_.red, color_.green, color_.blue, 120));
                context->drawEllipse(markerRect, VSTGUI::kDrawFilled);
            }
        }
    }

    // =========================================================================
    // Rungler Simulation (UI thread only)
    // =========================================================================

    void resetState() {
        osc1Phase_ = 0.0f;
        osc1Dir_ = 1;
        osc2Phase_ = 0.0f;
        osc2Dir_ = 1;
        osc2Prev_ = 0.0f;
        runglerCV_ = 0.0f;
        rawDac_ = 0.0f;
        filterState_ = 0.0f;

        registerMask_ = (1u << static_cast<uint32_t>(bits_)) - 1u;
        rng_.seed(42);
        registerState_ = rng_.next() & registerMask_;
        if (registerState_ == 0) registerState_ = 1;
    }

    void stepRungler() {
        constexpr float kSimSampleRate = 1000.0f;

        float osc1Eff = computeEffFreq(osc1Freq_, depth_, runglerCV_);
        float osc2Eff = computeEffFreq(osc2Freq_, depth_, runglerCV_);

        float osc1Inc = 4.0f * osc1Eff / kSimSampleRate;
        osc1Phase_ += static_cast<float>(osc1Dir_) * osc1Inc;
        if (osc1Phase_ >= 1.0f) { osc1Phase_ = 2.0f - osc1Phase_; osc1Dir_ = -1; }
        if (osc1Phase_ <= -1.0f) { osc1Phase_ = -2.0f - osc1Phase_; osc1Dir_ = 1; }

        float osc2Inc = 4.0f * osc2Eff / kSimSampleRate;
        osc2Phase_ += static_cast<float>(osc2Dir_) * osc2Inc;
        if (osc2Phase_ >= 1.0f) { osc2Phase_ = 2.0f - osc2Phase_; osc2Dir_ = -1; }
        if (osc2Phase_ <= -1.0f) { osc2Phase_ = -2.0f - osc2Phase_; osc2Dir_ = 1; }

        if (osc2Prev_ < 0.0f && osc2Phase_ >= 0.0f) {
            clockShiftRegister();
        }
        osc2Prev_ = osc2Phase_;

        float alpha = filterAmount_ > 0.001f
            ? std::exp(-6.2832f * (5.0f * std::pow(100.0f, 1.0f - filterAmount_)) / kSimSampleRate)
            : 0.0f;
        filterState_ = alpha * filterState_ + (1.0f - alpha) * rawDac_;
        runglerCV_ = filterState_;

        trailX_[trailHead_] = osc1Phase_;
        trailY_[trailHead_] = osc2Phase_;
        trailHead_ = (trailHead_ + 1) % kTrailLength;
        if (trailCount_ < kTrailLength) ++trailCount_;
    }

    void clockShiftRegister() noexcept {
        uint32_t bitCount = static_cast<uint32_t>(bits_);
        uint32_t lastBit = (registerState_ >> (bitCount - 1u)) & 1u;

        uint32_t dataBit = 0;
        if (loopMode_) {
            dataBit = lastBit;
        } else {
            uint32_t osc1Pulse = (osc1Phase_ >= 0.0f) ? 1u : 0u;
            dataBit = osc1Pulse ^ lastBit;
        }

        registerState_ = ((registerState_ << 1u) | dataBit) & registerMask_;

        uint32_t msb = (registerState_ >> (bitCount - 1u)) & 1u;
        uint32_t mid = (registerState_ >> (bitCount - 2u)) & 1u;
        uint32_t lsb = (registerState_ >> (bitCount - 3u)) & 1u;
        rawDac_ = static_cast<float>(msb * 4u + mid * 2u + lsb) / 7.0f;
    }

    void pushHistory() {
        history_[historyHead_] = runglerCV_;
        historyHead_ = (historyHead_ + 1) % kHistoryLength;
        if (historyCount_ < kHistoryLength) ++historyCount_;
    }

    [[nodiscard]] static float computeEffFreq(float baseFreq, float depth, float cv) noexcept {
        if (depth <= 0.0f) return baseFreq;
        constexpr float kModOctaves = 4.0f;
        float exponent = depth * kModOctaves * (cv - 0.5f);
        float factor = std::pow(2.0f, exponent);
        return std::clamp(baseFreq * factor, 0.1f, 500.0f);
    }

    // =========================================================================
    // State
    // =========================================================================

    // Parameters
    float osc1Freq_ = 2.0f;
    float osc2Freq_ = 3.0f;
    float depth_ = 0.0f;
    float filterAmount_ = 0.0f;
    int bits_ = 8;
    bool loopMode_ = false;

    // Simulation state
    float osc1Phase_ = 0.0f;
    int osc1Dir_ = 1;
    float osc2Phase_ = 0.0f;
    int osc2Dir_ = 1;
    float osc2Prev_ = 0.0f;
    uint32_t registerState_ = 0;
    uint32_t registerMask_ = (1u << 8) - 1u;
    float runglerCV_ = 0.0f;
    float rawDac_ = 0.0f;
    float filterState_ = 0.0f;
    Xorshift32 rng_{};

    // XY trail ring buffer
    std::array<float, kTrailLength> trailX_{};
    std::array<float, kTrailLength> trailY_{};
    int trailHead_ = 0;
    int trailCount_ = 0;

    // CV output history ring buffer
    std::array<float, kHistoryLength> history_{};
    int historyHead_ = 0;
    int historyCount_ = 0;
};

inline ModDisplayCreator<RunglerDisplay> gRunglerDisplayCreator{
    "RunglerDisplay", "Rungler Display", "rungler-color", {0, 0, 510, 230}};

} // namespace Krate::Plugins
