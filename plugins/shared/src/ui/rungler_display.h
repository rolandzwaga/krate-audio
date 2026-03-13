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
// This is purely visual — it does NOT read from the audio thread.
//
// Registered as "RunglerDisplay" via VSTGUI ViewCreator system.
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
#include <cstdint>
#include <string>

namespace Krate::Plugins {

// ==============================================================================
// RunglerDisplay View
// ==============================================================================

class RunglerDisplay : public VSTGUI::CView {
public:
    static constexpr int kTrailLength = 512;
    static constexpr int kHistoryLength = 256;
    static constexpr int kMaxBits = 16;
    static constexpr uint32_t kTimerIntervalMs = 33;   // ~30fps
    static constexpr int kStepsPerFrame = 64;           // rungler steps per timer tick

    // Lightweight Xorshift32 PRNG (mirrors DSP Xorshift32)
    struct Rng {
        uint32_t state = 1;
        uint32_t next() noexcept {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return state;
        }
        void seed(uint32_t s) noexcept { state = s ? s : 1; }
    };

    // =========================================================================
    // Construction / Destruction
    // =========================================================================

    explicit RunglerDisplay(const VSTGUI::CRect& size)
        : CView(size) {
        trailX_.fill(0.0f);
        trailY_.fill(0.0f);
        history_.fill(0.0f);
        resetState();
    }

    RunglerDisplay(const RunglerDisplay& other)
        : CView(other)
        , color_(other.color_)
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
            // Reset state when switching modes to show fresh behavior
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
    // Color Configuration
    // =========================================================================

    void setColor(const VSTGUI::CColor& color) {
        color_ = color;
        invalid();
    }

    [[nodiscard]] VSTGUI::CColor getColor() const { return color_; }

    void setColorFromString(const std::string& hexStr) {
        if (hexStr.size() >= 7 && hexStr[0] == '#') {
            auto r = static_cast<uint8_t>(std::stoul(hexStr.substr(1, 2), nullptr, 16));
            auto g = static_cast<uint8_t>(std::stoul(hexStr.substr(3, 2), nullptr, 16));
            auto b = static_cast<uint8_t>(std::stoul(hexStr.substr(5, 2), nullptr, 16));
            color_ = VSTGUI::CColor(r, g, b, 255);
            invalid();
        }
    }

    [[nodiscard]] std::string getColorString() const {
        char buf[8];
        snprintf(buf, sizeof(buf), "#%02X%02X%02X",
                 color_.red, color_.green, color_.blue);
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
        auto r = getViewSize();

        // ---- Rounded border background ----
        constexpr double kBorderRadius = 6.0;
        {
            auto* bgPath = context->createGraphicsPath();
            if (bgPath) {
                bgPath->addRoundRect(r, kBorderRadius);
                context->setFillColor(VSTGUI::CColor(22, 22, 26, 255));
                context->drawGraphicsPath(bgPath, VSTGUI::CDrawContext::kPathFilled);
                context->setFrameColor(VSTGUI::CColor(60, 60, 65, 255));
                context->setLineWidth(1.0);
                context->setLineStyle(VSTGUI::kLineSolid);
                context->drawGraphicsPath(bgPath, VSTGUI::CDrawContext::kPathStroked);
                bgPath->forget();
            }
        }

        // Layout: left = XY plot, right = CV + bits
        constexpr double kMargin = 10.0;
        constexpr double kGap = 12.0;
        double contentW = r.getWidth() - 2.0 * kMargin;
        double contentH = r.getHeight() - 2.0 * kMargin;

        // XY plot is square, sized to fit height
        double xySize = std::min(contentH, contentW * 0.42);
        double xyLeft = r.left + kMargin;
        double xyTop = r.top + kMargin + (contentH - xySize) * 0.5;

        // Right panel fills remaining width
        double rpLeft = xyLeft + xySize + kGap;
        double rpRight = r.right - kMargin;
        double rpTop = r.top + kMargin;
        double rpBottom = r.bottom - kMargin;
        double rpW = rpRight - rpLeft;
        double rpH = rpBottom - rpTop;

        drawXYPlot(context, xyLeft, xyTop, xySize, xySize);

        // Split right panel: CV staircase on top, shift register dots at bottom
        constexpr double kBitsRowHeight = 28.0;
        constexpr double kInnerGap = 6.0;
        double cvH = rpH - kBitsRowHeight - kInnerGap;
        drawCVStaircase(context, rpLeft, rpTop, rpW, cvH);
        drawShiftRegister(context, rpLeft, rpTop + cvH + kInnerGap, rpW, kBitsRowHeight);

        setDirty(false);
    }

    CLASS_METHODS(RunglerDisplay, CView)

private:
    // =========================================================================
    // XY Phase Plot Drawing (Osc1 vs Osc2 Lissajous)
    // =========================================================================

    void drawXYPlot(VSTGUI::CDrawContext* context,
                    double left, double top, double w, double h) const {
        // Sub-panel background
        VSTGUI::CRect xyRect(left, top, left + w, top + h);
        {
            auto* bgPath = context->createGraphicsPath();
            if (bgPath) {
                bgPath->addRoundRect(xyRect, 4.0);
                context->setFillColor(VSTGUI::CColor(16, 16, 20, 255));
                context->drawGraphicsPath(bgPath, VSTGUI::CDrawContext::kPathFilled);
                bgPath->forget();
            }
        }

        // Crosshair gridlines
        {
            const VSTGUI::CLineStyle::CoordVector dashes = {4.0, 4.0};
            VSTGUI::CLineStyle dashedStyle(
                VSTGUI::CLineStyle::kLineCapButt,
                VSTGUI::CLineStyle::kLineJoinMiter,
                0.0, dashes);
            context->setFrameColor(VSTGUI::CColor(160, 160, 165, 30));
            context->setLineWidth(1.0);
            context->setLineStyle(dashedStyle);
            double cx = left + w * 0.5;
            double cy = top + h * 0.5;
            context->drawLine(VSTGUI::CPoint(left + 4, cy), VSTGUI::CPoint(left + w - 4, cy));
            context->drawLine(VSTGUI::CPoint(cx, top + 4), VSTGUI::CPoint(cx, top + h - 4));
        }

        if (trailCount_ < 2) return;

        constexpr double kPadding = 8.0;
        double plotLeft = left + kPadding;
        double plotTop = top + kPadding;
        double plotW = w - 2.0 * kPadding;
        double plotH = h - 2.0 * kPadding;

        auto mapX = [&](float v) -> double {
            return plotLeft + (static_cast<double>(v) + 1.0) * 0.5 * plotW;
        };
        auto mapY = [&](float v) -> double {
            return plotTop + (1.0 - (static_cast<double>(v) + 1.0) * 0.5) * plotH;
        };

        // Draw trail segments with fading opacity
        int count = std::min(trailCount_, kTrailLength);
        context->setLineWidth(1.5);
        context->setLineStyle(VSTGUI::CLineStyle(
            VSTGUI::CLineStyle::kLineCapRound,
            VSTGUI::CLineStyle::kLineJoinRound));

        for (int i = 1; i < count; ++i) {
            int idxPrev = (trailHead_ - count + i - 1 + kTrailLength) % kTrailLength;
            int idxCurr = (trailHead_ - count + i + kTrailLength) % kTrailLength;

            float age = static_cast<float>(i) / static_cast<float>(count);
            auto alpha = static_cast<uint8_t>(age * age * 200);

            VSTGUI::CColor segColor(color_.red, color_.green, color_.blue, alpha);
            context->setFrameColor(segColor);
            context->drawLine(
                VSTGUI::CPoint(mapX(trailX_[idxPrev]), mapY(trailY_[idxPrev])),
                VSTGUI::CPoint(mapX(trailX_[idxCurr]), mapY(trailY_[idxCurr])));
        }

        // Current position dot
        if (count > 0) {
            int newest = (trailHead_ - 1 + kTrailLength) % kTrailLength;
            double dotX = mapX(trailX_[newest]);
            double dotY = mapY(trailY_[newest]);

            // Outer glow
            constexpr double kGlowR = 6.0;
            VSTGUI::CRect glowRect(dotX - kGlowR, dotY - kGlowR,
                                    dotX + kGlowR, dotY + kGlowR);
            context->setFillColor(VSTGUI::CColor(color_.red, color_.green, color_.blue, 50));
            context->drawEllipse(glowRect, VSTGUI::kDrawFilled);

            // Bright dot
            constexpr double kDotR = 3.5;
            VSTGUI::CRect dotRect(dotX - kDotR, dotY - kDotR,
                                   dotX + kDotR, dotY + kDotR);
            context->setFillColor(color_);
            context->drawEllipse(dotRect, VSTGUI::kDrawFilled);

            // White center
            constexpr double kInnerR = 1.5;
            VSTGUI::CRect innerRect(dotX - kInnerR, dotY - kInnerR,
                                     dotX + kInnerR, dotY + kInnerR);
            context->setFillColor(VSTGUI::CColor(255, 255, 255, 200));
            context->drawEllipse(innerRect, VSTGUI::kDrawFilled);
        }
    }

    // =========================================================================
    // CV Staircase Drawing (scrolling stepped voltage)
    // =========================================================================

    void drawCVStaircase(VSTGUI::CDrawContext* context,
                         double left, double top, double w, double h) const {
        // Sub-panel background
        VSTGUI::CRect tsRect(left, top, left + w, top + h);
        {
            auto* bgPath = context->createGraphicsPath();
            if (bgPath) {
                bgPath->addRoundRect(tsRect, 4.0);
                context->setFillColor(VSTGUI::CColor(16, 16, 20, 255));
                context->drawGraphicsPath(bgPath, VSTGUI::CDrawContext::kPathFilled);
                bgPath->forget();
            }
        }

        // Reference lines at 0% and 100% of CV range
        constexpr double kPadX = 4.0;
        constexpr double kPadY = 8.0;
        double plotLeft = left + kPadX;
        double plotW = w - 2.0 * kPadX;
        double plotTop = top + kPadY;
        double plotH = h - 2.0 * kPadY;

        // Dashed midpoint reference
        {
            double midY = plotTop + plotH * 0.5;
            const VSTGUI::CLineStyle::CoordVector dashes = {4.0, 4.0};
            VSTGUI::CLineStyle dashedStyle(
                VSTGUI::CLineStyle::kLineCapButt,
                VSTGUI::CLineStyle::kLineJoinMiter,
                0.0, dashes);
            context->setFrameColor(VSTGUI::CColor(160, 160, 165, 30));
            context->setLineWidth(1.0);
            context->setLineStyle(dashedStyle);
            context->drawLine(VSTGUI::CPoint(left + 4, midY),
                              VSTGUI::CPoint(left + w - 4, midY));
        }

        int count = std::min(historyCount_, kHistoryLength);
        if (count < 2) return;

        auto mapX = [&](int i) -> double {
            return plotLeft + (static_cast<double>(i) / (count - 1)) * plotW;
        };
        // CV is [0, 1] → map to full plot height (1=top, 0=bottom)
        auto mapY = [&](float v) -> double {
            return plotTop + (1.0 - static_cast<double>(v)) * plotH;
        };

        auto getHistoryValue = [&](int i) -> float {
            int idx = (historyHead_ - count + i + kHistoryLength) % kHistoryLength;
            return history_[idx];
        };

        // Build staircase fill path (area under curve from bottom)
        auto* fillPath = context->createGraphicsPath();
        auto* strokePath = context->createGraphicsPath();
        if (!fillPath || !strokePath) {
            if (fillPath) fillPath->forget();
            if (strokePath) strokePath->forget();
            return;
        }

        double bottomY = plotTop + plotH;

        // Start from bottom-left
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

        // Filled area
        context->setFillColor(VSTGUI::CColor(color_.red, color_.green, color_.blue, 25));
        context->drawGraphicsPath(fillPath, VSTGUI::CDrawContext::kPathFilled);
        fillPath->forget();

        // Staircase stroke
        context->setFrameColor(color_);
        context->setLineWidth(1.5);
        context->setLineStyle(VSTGUI::CLineStyle(
            VSTGUI::CLineStyle::kLineCapButt,
            VSTGUI::CLineStyle::kLineJoinMiter));
        context->drawGraphicsPath(strokePath, VSTGUI::CDrawContext::kPathStroked);
        strokePath->forget();

        // Current value dot at right edge
        if (count > 0) {
            float newestVal = getHistoryValue(count - 1);
            double dotX = mapX(count - 1);
            double dotY = mapY(newestVal);

            constexpr double kDotR = 4.0;
            VSTGUI::CRect dotRect(dotX - kDotR, dotY - kDotR,
                                   dotX + kDotR, dotY + kDotR);
            context->setFillColor(color_);
            context->drawEllipse(dotRect, VSTGUI::kDrawFilled);

            constexpr double kInnerR = 2.0;
            VSTGUI::CRect innerRect(dotX - kInnerR, dotY - kInnerR,
                                     dotX + kInnerR, dotY + kInnerR);
            context->setFillColor(VSTGUI::CColor(255, 255, 255, 220));
            context->drawEllipse(innerRect, VSTGUI::kDrawFilled);
        }
    }

    // =========================================================================
    // Shift Register Bit Display
    // =========================================================================

    void drawShiftRegister(VSTGUI::CDrawContext* context,
                           double left, double top, double w, double h) const {
        // Sub-panel background
        VSTGUI::CRect bgRect(left, top, left + w, top + h);
        {
            auto* bgPath = context->createGraphicsPath();
            if (bgPath) {
                bgPath->addRoundRect(bgRect, 4.0);
                context->setFillColor(VSTGUI::CColor(16, 16, 20, 255));
                context->drawGraphicsPath(bgPath, VSTGUI::CDrawContext::kPathFilled);
                bgPath->forget();
            }
        }

        // Draw bits as circles, MSB on left, LSB on right
        int numBits = bits_;
        constexpr double kDotRadius = 5.0;
        constexpr double kDotSpacing = 4.0;
        double totalWidth = numBits * (kDotRadius * 2.0 + kDotSpacing) - kDotSpacing;
        double startX = left + (w - totalWidth) * 0.5;
        double centerY = top + h * 0.5;

        for (int i = 0; i < numBits; ++i) {
            // Bit index: MSB (N-1) on left, LSB (0) on right
            int bitIdx = numBits - 1 - i;
            bool bitOn = (registerState_ >> static_cast<uint32_t>(bitIdx)) & 1u;

            double cx = startX + i * (kDotRadius * 2.0 + kDotSpacing) + kDotRadius;
            VSTGUI::CRect dotRect(cx - kDotRadius, centerY - kDotRadius,
                                   cx + kDotRadius, centerY + kDotRadius);

            if (bitOn) {
                // Lit: colored fill with subtle glow
                VSTGUI::CRect glowRect(cx - kDotRadius - 2, centerY - kDotRadius - 2,
                                        cx + kDotRadius + 2, centerY + kDotRadius + 2);
                context->setFillColor(VSTGUI::CColor(color_.red, color_.green, color_.blue, 40));
                context->drawEllipse(glowRect, VSTGUI::kDrawFilled);

                context->setFillColor(color_);
                context->drawEllipse(dotRect, VSTGUI::kDrawFilled);
            } else {
                // Off: dim outline
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
    // Rungler Simulation (UI thread only, mirrors DSP math)
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
        // UI simulation runs at an internal "sample rate" for visual speed
        constexpr float kSimSampleRate = 1000.0f;

        // Compute effective frequencies with cross-modulation
        float osc1Eff = computeEffFreq(osc1Freq_, depth_, runglerCV_);
        float osc2Eff = computeEffFreq(osc2Freq_, depth_, runglerCV_);

        // Update Osc1 triangle
        float osc1Inc = 4.0f * osc1Eff / kSimSampleRate;
        osc1Phase_ += static_cast<float>(osc1Dir_) * osc1Inc;
        if (osc1Phase_ >= 1.0f) { osc1Phase_ = 2.0f - osc1Phase_; osc1Dir_ = -1; }
        if (osc1Phase_ <= -1.0f) { osc1Phase_ = -2.0f - osc1Phase_; osc1Dir_ = 1; }

        // Update Osc2 triangle
        float osc2Inc = 4.0f * osc2Eff / kSimSampleRate;
        osc2Phase_ += static_cast<float>(osc2Dir_) * osc2Inc;
        if (osc2Phase_ >= 1.0f) { osc2Phase_ = 2.0f - osc2Phase_; osc2Dir_ = -1; }
        if (osc2Phase_ <= -1.0f) { osc2Phase_ = -2.0f - osc2Phase_; osc2Dir_ = 1; }

        // Clock shift register on Osc2 rising edge
        if (osc2Prev_ < 0.0f && osc2Phase_ >= 0.0f) {
            clockShiftRegister();
        }
        osc2Prev_ = osc2Phase_;

        // Simple one-pole CV filter
        float alpha = filterAmount_ > 0.001f
            ? std::exp(-6.2832f * (5.0f * std::pow(100.0f, 1.0f - filterAmount_)) / kSimSampleRate)
            : 0.0f;
        filterState_ = alpha * filterState_ + (1.0f - alpha) * rawDac_;
        runglerCV_ = filterState_;

        // Push to XY trail
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

        // 3-bit DAC from top 3 bits
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
        return std::clamp(baseFreq * factor, 0.1f, 500.0f);  // UI sim caps at 500Hz
    }

    // =========================================================================
    // Animation Timer
    // =========================================================================

    void startTimer() {
        if (animTimer_) return;
        animTimer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>(
            [this](VSTGUI::CVSTGUITimer*) {
                for (int i = 0; i < kStepsPerFrame; ++i) {
                    stepRungler();
                }
                pushHistory();
                invalid();
            }, kTimerIntervalMs);
    }

    void stopTimer() {
        animTimer_ = nullptr;
    }

    // =========================================================================
    // State
    // =========================================================================

    VSTGUI::CColor color_{90, 200, 130, 255};  // Default: modulation green

    // Parameters (set from controller)
    float osc1Freq_ = 2.0f;       // [0.1, 100] Hz
    float osc2Freq_ = 3.0f;       // [0.1, 100] Hz
    float depth_ = 0.0f;          // [0, 1] cross-mod depth
    float filterAmount_ = 0.0f;   // [0, 1] CV smoothing
    int bits_ = 8;                // [4, 16] shift register bits
    bool loopMode_ = false;       // false=chaos, true=loop

    // Simulation state (UI thread only)
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
    Rng rng_{};

    // XY trail ring buffer (phase plot)
    std::array<float, kTrailLength> trailX_{};
    std::array<float, kTrailLength> trailY_{};
    int trailHead_ = 0;
    int trailCount_ = 0;

    // CV output history ring buffer (staircase)
    std::array<float, kHistoryLength> history_{};
    int historyHead_ = 0;
    int historyCount_ = 0;

    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> animTimer_;
};

// =============================================================================
// ViewCreator Registration
// =============================================================================

struct RunglerDisplayCreator : VSTGUI::ViewCreatorAdapter {
    RunglerDisplayCreator() {
        VSTGUI::UIViewFactory::registerViewCreator(*this);
    }

    VSTGUI::IdStringPtr getViewName() const override {
        return "RunglerDisplay";
    }

    VSTGUI::IdStringPtr getBaseViewName() const override {
        return VSTGUI::UIViewCreator::kCView;
    }

    VSTGUI::UTF8StringPtr getDisplayName() const override {
        return "Rungler Display";
    }

    VSTGUI::CView* create(
        const VSTGUI::UIAttributes& /*attributes*/,
        const VSTGUI::IUIDescription* /*description*/) const override {
        return new RunglerDisplay(VSTGUI::CRect(0, 0, 510, 230));
    }

    bool apply(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
               const VSTGUI::IUIDescription* /*description*/) const override {
        auto* display = dynamic_cast<RunglerDisplay*>(view);
        if (!display) return false;

        if (auto colorStr = attributes.getAttributeValue("rungler-color"))
            display->setColorFromString(*colorStr);

        return true;
    }

    bool getAttributeNames(
        VSTGUI::IViewCreator::StringList& attributeNames) const override {
        attributeNames.emplace_back("rungler-color");
        return true;
    }

    AttrType getAttributeType(const std::string& attributeName) const override {
        if (attributeName == "rungler-color") return kStringType;
        return kUnknownType;
    }

    bool getAttributeValue(VSTGUI::CView* view,
                           const std::string& attributeName,
                           std::string& stringValue,
                           const VSTGUI::IUIDescription* /*desc*/) const override {
        auto* display = dynamic_cast<RunglerDisplay*>(view);
        if (!display) return false;
        if (attributeName == "rungler-color") {
            stringValue = display->getColorString();
            return true;
        }
        return false;
    }
};

/// Inline variable (C++17) -- safe for inclusion from multiple translation units.
inline RunglerDisplayCreator gRunglerDisplayCreator;

} // namespace Krate::Plugins
