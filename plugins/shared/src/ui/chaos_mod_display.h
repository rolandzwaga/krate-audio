#pragma once

// ==============================================================================
// ChaosModDisplay - Hybrid Chaos Modulation Visualizer
// ==============================================================================
// A shared VSTGUI CView subclass that renders a chaos modulation source as:
//   Left:  XY phase plot showing the attractor trajectory (Lorenz butterfly,
//          Rossler spiral, etc.) with a fading trail
//   Right: Scrolling time-series of the normalized output value
//
// The display runs its own lightweight attractor simulation on the UI thread
// at ~30fps, mirroring the DSP parameters (model, speed) set from the
// controller. This is purely visual -- it does NOT read from the audio thread.
//
// Registered as "ChaosModDisplay" via VSTGUI ViewCreator system.
// ==============================================================================

#include "animated_mod_display.h"
#include "mod_display_utils.h"
#include "mod_display_creator.h"

#include "vstgui/lib/cdrawcontext.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace Krate::Plugins {

class ChaosModDisplay : public AnimatedModDisplay {
public:
    static constexpr int kTrailLength = 512;
    static constexpr int kHistoryLength = 256;
    static constexpr int kStepsPerFrame = 48;

    enum class Model { Lorenz = 0, Rossler = 1 };

    // =========================================================================
    // Construction / Destruction
    // =========================================================================

    explicit ChaosModDisplay(const VSTGUI::CRect& size)
        : AnimatedModDisplay(size) {
        trailX_.fill(0.0f);
        trailY_.fill(0.0f);
        history_.fill(0.0f);
        resetAttractor();
    }

    ChaosModDisplay(const ChaosModDisplay& other)
        : AnimatedModDisplay(other)
        , model_(other.model_)
        , speed_(other.speed_)
        , depth_(other.depth_) {
        trailX_.fill(0.0f);
        trailY_.fill(0.0f);
        history_.fill(0.0f);
        resetAttractor();
    }

    ~ChaosModDisplay() override {
        stopTimer();
    }

    // =========================================================================
    // Parameter Setters (called from controller)
    // =========================================================================

    void setModel(int modelIndex) {
        auto newModel = static_cast<Model>(std::clamp(modelIndex, 0, 1));
        if (newModel != model_) {
            model_ = newModel;
            resetAttractor();
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

    void setSpeed(float speed) {
        speed_ = std::clamp(speed, 0.05f, 20.0f);
    }

    void setDepth(float depth) {
        depth_ = std::clamp(depth, 0.0f, 1.0f);
    }

    // =========================================================================
    // Drawing
    // =========================================================================

    void draw(VSTGUI::CDrawContext* context) override {
        auto r = getViewSize();
        drawRoundedBackground(context, r);

        // Layout: left = XY plot, right = time series
        constexpr double kMargin = 10.0;
        constexpr double kGap = 12.0;
        double contentW = r.getWidth() - 2.0 * kMargin;
        double contentH = r.getHeight() - 2.0 * kMargin;

        double xySize = std::min(contentH, contentW * 0.42);
        double xyLeft = r.left + kMargin;
        double xyTop = r.top + kMargin + (contentH - xySize) * 0.5;

        double tsLeft = xyLeft + xySize + kGap;
        double tsRight = r.right - kMargin;
        double tsTop = r.top + kMargin;
        double tsBottom = r.bottom - kMargin;

        drawXYPlot(context, xyLeft, xyTop, xySize, xySize);
        drawTimeSeries(context, tsLeft, tsTop, tsRight - tsLeft, tsBottom - tsTop);

        setDirty(false);
    }

    CLASS_METHODS(ChaosModDisplay, AnimatedModDisplay)

protected:
    void onTimerTick() override {
        for (int i = 0; i < kStepsPerFrame; ++i)
            stepAttractor();
        pushHistory();
    }

private:
    // =========================================================================
    // XY Phase Plot
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
    // Scrolling Time Series
    // =========================================================================

    void drawTimeSeries(VSTGUI::CDrawContext* context,
                        double left, double top, double w, double h) const {
        VSTGUI::CRect tsRect(left, top, left + w, top + h);
        drawSubPanel(context, tsRect);

        double zeroY = top + h * 0.5;
        drawDashedLine(context,
            {left + 4, zeroY}, {left + w - 4, zeroY},
            VSTGUI::CColor(160, 160, 165, 40));

        constexpr double kPadX = 4.0;
        constexpr double kPadY = 8.0;
        RingBufferView histView{history_.data(), historyHead_,
                                 std::min(historyCount_, kHistoryLength), kHistoryLength};
        drawTimeSeriesCurve(context,
            left + kPadX, top + kPadY,
            w - 2.0 * kPadX, h - 2.0 * kPadY,
            histView, color_, zeroY, 30);
    }

    // =========================================================================
    // Attractor Simulation (UI thread only)
    // =========================================================================

    void resetAttractor() {
        switch (model_) {
            case Model::Lorenz:
                sx_ = 1.0f; sy_ = 1.0f; sz_ = 1.0f;
                break;
            case Model::Rossler:
                sx_ = -5.0f; sy_ = -5.0f; sz_ = 0.5f;
                break;
        }
    }

    void stepAttractor() {
        float dt = getBaseDt() * speed_;

        switch (model_) {
            case Model::Lorenz: {
                constexpr float sigma = 10.0f;
                constexpr float rho = 28.0f;
                constexpr float beta = 8.0f / 3.0f;
                float dx = sigma * (sy_ - sx_);
                float dy = sx_ * (rho - sz_) - sy_;
                float dz = sx_ * sy_ - beta * sz_;
                sx_ += dx * dt;
                sy_ += dy * dt;
                sz_ += dz * dt;
                break;
            }
            case Model::Rossler: {
                constexpr float a = 0.2f;
                constexpr float b = 0.2f;
                constexpr float c = 5.7f;
                float dx = -sy_ - sz_;
                float dy = sx_ + a * sy_;
                float dz = b + sz_ * (sx_ - c);
                sx_ += dx * dt;
                sy_ += dy * dt;
                sz_ += dz * dt;
                break;
            }
        }

        // Divergence check
        float bound = getSafeBound();
        if (std::abs(sx_) > bound * 10.0f ||
            std::abs(sy_) > bound * 10.0f ||
            std::abs(sz_) > bound * 10.0f) {
            resetAttractor();
        }

        // Normalized outputs for display
        float scale = getNormScale();
        float normX = std::clamp(std::tanh(sx_ / scale), -1.0f, 1.0f);
        float normY = std::clamp(std::tanh(sy_ / scale), -1.0f, 1.0f);

        trailX_[trailHead_] = normX;
        trailY_[trailHead_] = normY;
        trailHead_ = (trailHead_ + 1) % kTrailLength;
        if (trailCount_ < kTrailLength) ++trailCount_;
    }

    void pushHistory() {
        float scale = getNormScale();
        float output = std::clamp(std::tanh(sx_ / scale), -1.0f, 1.0f) * depth_;
        history_[historyHead_] = output;
        historyHead_ = (historyHead_ + 1) % kHistoryLength;
        if (historyCount_ < kHistoryLength) ++historyCount_;
    }

    [[nodiscard]] float getBaseDt() const noexcept {
        switch (model_) {
            case Model::Lorenz:  return 0.005f;
            case Model::Rossler: return 0.01f;
        }
        return 0.005f;
    }

    [[nodiscard]] float getNormScale() const noexcept {
        switch (model_) {
            case Model::Lorenz:  return 20.0f;
            case Model::Rossler: return 10.0f;
        }
        return 20.0f;
    }

    [[nodiscard]] float getSafeBound() const noexcept {
        switch (model_) {
            case Model::Lorenz:  return 50.0f;
            case Model::Rossler: return 30.0f;
        }
        return 50.0f;
    }

    // =========================================================================
    // State
    // =========================================================================

    Model model_ = Model::Lorenz;
    float speed_ = 1.0f;
    float depth_ = 1.0f;

    // Attractor state
    float sx_ = 1.0f, sy_ = 1.0f, sz_ = 1.0f;

    // XY trail ring buffer
    std::array<float, kTrailLength> trailX_{};
    std::array<float, kTrailLength> trailY_{};
    int trailHead_ = 0;
    int trailCount_ = 0;

    // Output history ring buffer
    std::array<float, kHistoryLength> history_{};
    int historyHead_ = 0;
    int historyCount_ = 0;
};

inline ModDisplayCreator<ChaosModDisplay> gChaosModDisplayCreator{
    "ChaosModDisplay", "Chaos Mod Display", "chaos-color", {0, 0, 510, 230}};

} // namespace Krate::Plugins
