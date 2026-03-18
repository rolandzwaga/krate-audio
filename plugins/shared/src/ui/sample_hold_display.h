#pragma once

// ==============================================================================
// SampleHoldDisplay - Sample & Hold Modulation Visualizer
// ==============================================================================
// Renders a scrolling staircase of held values with trigger markers and
// optional slew smoothing curves. Runs a lightweight S&H simulation on
// the UI thread, mirroring DSP parameters set from the controller.
//
// Registered as "SampleHoldDisplay" via VSTGUI ViewCreator system.
// ==============================================================================

#include "animated_mod_display.h"
#include "mod_display_utils.h"
#include "mod_display_creator.h"

#include "vstgui/lib/cdrawcontext.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Krate::Plugins {

class SampleHoldDisplay : public AnimatedModDisplay {
public:
    static constexpr int kHistoryLength = 256;
    static constexpr int kTriggerHistoryLength = 256;
    static constexpr int kStepsPerFrame = 64;

    // =========================================================================
    // Construction / Destruction
    // =========================================================================

    explicit SampleHoldDisplay(const VSTGUI::CRect& size)
        : AnimatedModDisplay(size) {
        rng_.seed(54321);
        history_.fill(0.0f);
        triggerHistory_.fill(false);
    }

    SampleHoldDisplay(const SampleHoldDisplay& other)
        : AnimatedModDisplay(other)
        , rate_(other.rate_)
        , slewMs_(other.slewMs_) {
        rng_.seed(54321);
        history_.fill(0.0f);
        triggerHistory_.fill(false);
    }

    ~SampleHoldDisplay() override {
        stopTimer();
    }

    // =========================================================================
    // Parameter Setters (called from controller)
    // =========================================================================

    void setRate(float hz) {
        rate_ = std::clamp(hz, 0.1f, 50.0f);
    }

    void setSlew(float ms) {
        slewMs_ = std::clamp(ms, 0.0f, 500.0f);
    }

    // =========================================================================
    // Drawing
    // =========================================================================

    void draw(VSTGUI::CDrawContext* context) override {
        auto r = getViewSize();
        drawRoundedBackground(context, r);

        constexpr double kMargin = 10.0;
        double plotLeft = r.left + kMargin;
        double plotTop = r.top + kMargin;
        double plotW = r.getWidth() - 2.0 * kMargin;
        double plotH = r.getHeight() - 2.0 * kMargin;

        VSTGUI::CRect plotRect(plotLeft, plotTop, plotLeft + plotW, plotTop + plotH);
        drawSubPanel(context, plotRect);

        double zeroY = plotTop + plotH * 0.5;
        drawDashedLine(context,
            {plotLeft + 4, zeroY}, {plotLeft + plotW - 4, zeroY},
            VSTGUI::CColor(160, 160, 165, 30));

        int count = std::min(historyCount_, kHistoryLength);
        if (count < 2) { setDirty(false); return; }

        constexpr double kPadX = 4.0;
        constexpr double kPadY = 8.0;
        double innerLeft = plotLeft + kPadX;
        double innerW = plotW - 2.0 * kPadX;
        double innerTop = plotTop + kPadY;
        double innerH = plotH - 2.0 * kPadY;

        // Draw trigger markers (subtle vertical lines)
        context->setLineStyle(VSTGUI::kLineSolid);
        context->setLineWidth(1.0);
        for (int i = 0; i < count; ++i) {
            int idx = (historyHead_ - count + i + kTriggerHistoryLength) % kTriggerHistoryLength;
            if (triggerHistory_[idx]) {
                double x = innerLeft + (static_cast<double>(i) / (count - 1)) * innerW;
                context->setFrameColor(VSTGUI::CColor(color_.red, color_.green, color_.blue, 35));
                context->drawLine(VSTGUI::CPoint(x, innerTop),
                                  VSTGUI::CPoint(x, innerTop + innerH));
            }
        }

        // Time series curve
        RingBufferView histView{history_.data(), historyHead_, count, kHistoryLength};
        drawTimeSeriesCurve(context,
            innerLeft, innerTop, innerW, innerH,
            histView, color_, zeroY);

        setDirty(false);
    }

    CLASS_METHODS(SampleHoldDisplay, AnimatedModDisplay)

protected:
    void onTimerTick() override {
        for (int i = 0; i < kStepsPerFrame; ++i)
            stepSH();
        pushHistory();
    }

private:
    void stepSH() {
        constexpr float kSimSampleRate = 1000.0f;

        phase_ += rate_ / kSimSampleRate;

        bool triggered = false;
        if (phase_ >= 1.0f) {
            phase_ -= 1.0f;
            heldValue_ = rng_.nextFloat();
            triggered = true;
        }

        if (slewMs_ <= 0.01f) {
            smoothedValue_ = heldValue_;
        } else {
            float coeff = std::exp(-5000.0f / (slewMs_ * kSimSampleRate));
            smoothedValue_ = heldValue_ + coeff * (smoothedValue_ - heldValue_);
        }

        triggered_ = triggered;
    }

    void pushHistory() {
        history_[historyHead_] = smoothedValue_;
        triggerHistory_[historyHead_] = triggered_;
        historyHead_ = (historyHead_ + 1) % kHistoryLength;
        if (historyCount_ < kHistoryLength) ++historyCount_;
    }

    // Parameters
    float rate_ = 4.0f;         // [0.1, 50] Hz
    float slewMs_ = 0.0f;       // [0, 500] ms

    // Simulation state
    float phase_ = 0.0f;
    float heldValue_ = 0.0f;
    float smoothedValue_ = 0.0f;
    bool triggered_ = false;
    Xorshift32 rng_{};

    // History ring buffers
    std::array<float, kHistoryLength> history_{};
    std::array<bool, kTriggerHistoryLength> triggerHistory_{};
    int historyHead_ = 0;
    int historyCount_ = 0;
};

inline ModDisplayCreator<SampleHoldDisplay> gSampleHoldDisplayCreator{
    "SampleHoldDisplay", "Sample Hold Display", "sh-color", {0, 0, 510, 290}};

} // namespace Krate::Plugins
