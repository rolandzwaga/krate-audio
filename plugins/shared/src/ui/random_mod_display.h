#pragma once

// ==============================================================================
// RandomModDisplay - Random Modulation Source Visualizer
// ==============================================================================
// Renders a scrolling time-series of random modulation output. With smoothness
// at 0, shows sharp staircase transitions; with smoothness > 0, transitions
// curve smoothly between random targets.
//
// Runs a lightweight random source simulation on the UI thread at ~30fps,
// mirroring the DSP parameters set from the controller.
//
// Registered as "RandomModDisplay" via VSTGUI ViewCreator system.
// ==============================================================================

#include "animated_mod_display.h"
#include "mod_display_utils.h"
#include "mod_display_creator.h"

#include "vstgui/lib/cdrawcontext.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Krate::Plugins {

class RandomModDisplay : public AnimatedModDisplay {
public:
    static constexpr int kHistoryLength = 256;
    static constexpr int kStepsPerFrame = 64;

    // =========================================================================
    // Construction / Destruction
    // =========================================================================

    explicit RandomModDisplay(const VSTGUI::CRect& size)
        : AnimatedModDisplay(size) {
        rng_.seed(98765);
        history_.fill(0.0f);
    }

    RandomModDisplay(const RandomModDisplay& other)
        : AnimatedModDisplay(other)
        , rate_(other.rate_)
        , smoothness_(other.smoothness_) {
        rng_.seed(98765);
        history_.fill(0.0f);
    }

    ~RandomModDisplay() override {
        stopTimer();
    }

    // =========================================================================
    // Parameter Setters (called from controller)
    // =========================================================================

    void setRate(float hz) {
        rate_ = std::clamp(hz, 0.1f, 50.0f);
    }

    void setSmoothness(float normalized) {
        smoothness_ = std::clamp(normalized, 0.0f, 1.0f);
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

        constexpr double kPadX = 4.0;
        constexpr double kPadY = 8.0;
        RingBufferView histView{history_.data(), historyHead_,
                                 std::min(historyCount_, kHistoryLength), kHistoryLength};
        drawTimeSeriesCurve(context,
            plotLeft + kPadX, plotTop + kPadY,
            plotW - 2.0 * kPadX, plotH - 2.0 * kPadY,
            histView, color_, zeroY);

        setDirty(false);
    }

    CLASS_METHODS(RandomModDisplay, AnimatedModDisplay)

protected:
    void onTimerTick() override {
        for (int i = 0; i < kStepsPerFrame; ++i)
            stepRandom();
        pushHistory();
    }

private:
    void stepRandom() {
        constexpr float kSimSampleRate = 1000.0f;

        phase_ += rate_ / kSimSampleRate;

        if (phase_ >= 1.0f) {
            phase_ -= 1.0f;
            currentTarget_ = rng_.nextFloat();
        }

        if (smoothness_ <= 0.001f) {
            smoothedValue_ = currentTarget_;
        } else {
            float smoothMs = smoothness_ * 200.0f;
            float coeff = std::exp(-5000.0f / (smoothMs * kSimSampleRate));
            smoothedValue_ = currentTarget_ + coeff * (smoothedValue_ - currentTarget_);
        }
    }

    void pushHistory() {
        history_[historyHead_] = std::clamp(smoothedValue_, -1.0f, 1.0f);
        historyHead_ = (historyHead_ + 1) % kHistoryLength;
        if (historyCount_ < kHistoryLength) ++historyCount_;
    }

    // Parameters
    float rate_ = 4.0f;          // [0.1, 50] Hz
    float smoothness_ = 0.0f;    // [0, 1]

    // Simulation state
    float phase_ = 0.0f;
    float currentTarget_ = 0.0f;
    float smoothedValue_ = 0.0f;
    Xorshift32 rng_{};

    // History ring buffer
    std::array<float, kHistoryLength> history_{};
    int historyHead_ = 0;
    int historyCount_ = 0;
};

inline ModDisplayCreator<RandomModDisplay> gRandomModDisplayCreator{
    "RandomModDisplay", "Random Mod Display", "random-color", {0, 0, 510, 290}};

} // namespace Krate::Plugins
