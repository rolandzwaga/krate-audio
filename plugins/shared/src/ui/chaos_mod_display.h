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
// controller. This is purely visual — it does NOT read from the audio thread.
//
// Registered as "ChaosModDisplay" via VSTGUI ViewCreator system.
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

namespace Krate::Plugins {

// ==============================================================================
// ChaosModDisplay View
// ==============================================================================

class ChaosModDisplay : public VSTGUI::CView {
public:
    static constexpr int kTrailLength = 512;
    static constexpr int kHistoryLength = 256;
    static constexpr uint32_t kTimerIntervalMs = 33;  // ~30fps
    static constexpr int kStepsPerFrame = 48;          // attractor steps per timer tick (~1440/sec to match DSP control rate)

    // Attractor model (mirrors ChaosModel enum without DSP dependency)
    enum class Model { Lorenz = 0, Rossler = 1 };

    // =========================================================================
    // Construction / Destruction
    // =========================================================================

    explicit ChaosModDisplay(const VSTGUI::CRect& size)
        : CView(size) {
        trailX_.fill(0.0f);
        trailY_.fill(0.0f);
        history_.fill(0.0f);
        resetAttractor();
    }

    ChaosModDisplay(const ChaosModDisplay& other)
        : CView(other)
        , color_(other.color_)
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

        // Layout: left = XY plot, right = time series, with a gap between
        constexpr double kMargin = 10.0;
        constexpr double kGap = 12.0;
        double contentW = r.getWidth() - 2.0 * kMargin;
        double contentH = r.getHeight() - 2.0 * kMargin;

        // XY plot is square, sized to fit height
        double xySize = std::min(contentH, contentW * 0.42);
        double xyLeft = r.left + kMargin;
        double xyTop = r.top + kMargin + (contentH - xySize) * 0.5;

        // Time series fills remaining width
        double tsLeft = xyLeft + xySize + kGap;
        double tsRight = r.right - kMargin;
        double tsTop = r.top + kMargin;
        double tsBottom = r.bottom - kMargin;
        double tsW = tsRight - tsLeft;
        double tsH = tsBottom - tsTop;

        drawXYPlot(context, xyLeft, xyTop, xySize, xySize);
        drawTimeSeries(context, tsLeft, tsTop, tsW, tsH);

        setDirty(false);
    }

    CLASS_METHODS(ChaosModDisplay, CView)

private:
    // =========================================================================
    // XY Phase Plot Drawing
    // =========================================================================

    void drawXYPlot(VSTGUI::CDrawContext* context,
                    double left, double top, double w, double h) const {
        // Subtle sub-panel background
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
        VSTGUI::CColor gridColor(160, 160, 165, 30);
        {
            const VSTGUI::CLineStyle::CoordVector dashes = {4.0, 4.0};
            VSTGUI::CLineStyle dashedStyle(
                VSTGUI::CLineStyle::kLineCapButt,
                VSTGUI::CLineStyle::kLineJoinMiter,
                0.0, dashes);
            context->setFrameColor(gridColor);
            context->setLineWidth(1.0);
            context->setLineStyle(dashedStyle);
            double cx = left + w * 0.5;
            double cy = top + h * 0.5;
            context->drawLine(VSTGUI::CPoint(left + 4, cy), VSTGUI::CPoint(left + w - 4, cy));
            context->drawLine(VSTGUI::CPoint(cx, top + 4), VSTGUI::CPoint(cx, top + h - 4));
        }

        if (trailCount_ < 2) return;

        // Map normalized [-1,+1] attractor coordinates to pixel space
        constexpr double kPadding = 8.0;
        double plotLeft = left + kPadding;
        double plotTop = top + kPadding;
        double plotW = w - 2.0 * kPadding;
        double plotH = h - 2.0 * kPadding;

        auto mapX = [&](float v) -> double {
            return plotLeft + (static_cast<double>(v) + 1.0) * 0.5 * plotW;
        };
        auto mapY = [&](float v) -> double {
            // Invert Y so positive is up
            return plotTop + (1.0 - (static_cast<double>(v) + 1.0) * 0.5) * plotH;
        };

        // Draw trail segments with fading opacity
        int count = std::min(trailCount_, kTrailLength);
        context->setLineWidth(1.5);
        context->setLineStyle(VSTGUI::CLineStyle(
            VSTGUI::CLineStyle::kLineCapRound,
            VSTGUI::CLineStyle::kLineJoinRound));

        for (int i = 1; i < count; ++i) {
            // Oldest = index 0, newest = count-1
            int idxPrev = (trailHead_ - count + i - 1 + kTrailLength) % kTrailLength;
            int idxCurr = (trailHead_ - count + i + kTrailLength) % kTrailLength;

            float age = static_cast<float>(i) / static_cast<float>(count);
            auto alpha = static_cast<uint8_t>(age * age * 200);  // quadratic fade-in

            VSTGUI::CColor segColor(color_.red, color_.green, color_.blue, alpha);
            context->setFrameColor(segColor);
            context->drawLine(
                VSTGUI::CPoint(mapX(trailX_[idxPrev]), mapY(trailY_[idxPrev])),
                VSTGUI::CPoint(mapX(trailX_[idxCurr]), mapY(trailY_[idxCurr])));
        }

        // Draw current position dot (bright)
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
    // Scrolling Time Series Drawing
    // =========================================================================

    void drawTimeSeries(VSTGUI::CDrawContext* context,
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

        // Zero line
        double zeroY = top + h * 0.5;
        {
            const VSTGUI::CLineStyle::CoordVector dashes = {4.0, 4.0};
            VSTGUI::CLineStyle dashedStyle(
                VSTGUI::CLineStyle::kLineCapButt,
                VSTGUI::CLineStyle::kLineJoinMiter,
                0.0, dashes);
            context->setFrameColor(VSTGUI::CColor(160, 160, 165, 40));
            context->setLineWidth(1.0);
            context->setLineStyle(dashedStyle);
            context->drawLine(VSTGUI::CPoint(left + 4, zeroY),
                              VSTGUI::CPoint(left + w - 4, zeroY));
        }

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
        auto mapY = [&](float v) -> double {
            // v in [-1, +1] -> plotTop..plotBottom
            return plotTop + (1.0 - (static_cast<double>(v) + 1.0) * 0.5) * plotH;
        };

        // Build fill path (area under curve from zero line)
        auto* fillPath = context->createGraphicsPath();
        auto* strokePath = context->createGraphicsPath();
        if (!fillPath || !strokePath) {
            if (fillPath) fillPath->forget();
            if (strokePath) strokePath->forget();
            return;
        }

        // Read history from oldest to newest
        auto getHistoryValue = [&](int i) -> float {
            int idx = (historyHead_ - count + i + kHistoryLength) % kHistoryLength;
            return history_[idx];
        };

        fillPath->beginSubpath(VSTGUI::CPoint(mapX(0), zeroY));
        fillPath->addLine(VSTGUI::CPoint(mapX(0), mapY(getHistoryValue(0))));
        strokePath->beginSubpath(VSTGUI::CPoint(mapX(0), mapY(getHistoryValue(0))));

        for (int i = 1; i < count; ++i) {
            VSTGUI::CPoint pt(mapX(i), mapY(getHistoryValue(i)));
            fillPath->addLine(pt);
            strokePath->addLine(pt);
        }

        fillPath->addLine(VSTGUI::CPoint(mapX(count - 1), zeroY));
        fillPath->closeSubpath();

        // Draw filled area
        context->setFillColor(VSTGUI::CColor(color_.red, color_.green, color_.blue, 30));
        context->drawGraphicsPath(fillPath, VSTGUI::CDrawContext::kPathFilled);
        fillPath->forget();

        // Draw waveform stroke
        context->setFrameColor(color_);
        context->setLineWidth(1.5);
        context->setLineStyle(VSTGUI::CLineStyle(
            VSTGUI::CLineStyle::kLineCapRound,
            VSTGUI::CLineStyle::kLineJoinRound));
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
    // Attractor Simulation (UI thread only)
    // =========================================================================

    void resetAttractor() {
        switch (model_) {
            case Model::Lorenz:
                sx_ = 1.0f; sy_ = 1.0f; sz_ = 1.0f;
                break;
            case Model::Rossler:
                sx_ = -5.0f; sy_ = -5.0f; sz_ = 0.5f;  // Near the attractor
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
        float scale = getNormScale();
        float bound = getSafeBound();
        if (std::abs(sx_) > bound * 10.0f ||
            std::abs(sy_) > bound * 10.0f ||
            std::abs(sz_) > bound * 10.0f) {
            resetAttractor();
        }

        // Normalized outputs for display
        float normX = std::clamp(std::tanh(sx_ / scale), -1.0f, 1.0f);
        float normY = std::clamp(std::tanh(sy_ / scale), -1.0f, 1.0f);

        // Push to XY trail ring buffer
        trailX_[trailHead_] = normX;
        trailY_[trailHead_] = normY;
        trailHead_ = (trailHead_ + 1) % kTrailLength;
        if (trailCount_ < kTrailLength) ++trailCount_;

    }

    /// Push current output to the time-series history (call once per frame, not per step)
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
    // Animation Timer
    // =========================================================================

    void startTimer() {
        if (animTimer_) return;
        animTimer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>(
            [this](VSTGUI::CVSTGUITimer*) {
                for (int i = 0; i < kStepsPerFrame; ++i) {
                    stepAttractor();
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
    Model model_ = Model::Lorenz;
    float speed_ = 1.0f;
    float depth_ = 1.0f;

    // Attractor state (UI-thread simulation)
    float sx_ = 1.0f, sy_ = 1.0f, sz_ = 1.0f;

    // XY trail ring buffer (phase plot)
    std::array<float, kTrailLength> trailX_{};
    std::array<float, kTrailLength> trailY_{};
    int trailHead_ = 0;
    int trailCount_ = 0;

    // Output history ring buffer (time series)
    std::array<float, kHistoryLength> history_{};
    int historyHead_ = 0;
    int historyCount_ = 0;

    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> animTimer_;
};

// =============================================================================
// ViewCreator Registration
// =============================================================================

struct ChaosModDisplayCreator : VSTGUI::ViewCreatorAdapter {
    ChaosModDisplayCreator() {
        VSTGUI::UIViewFactory::registerViewCreator(*this);
    }

    VSTGUI::IdStringPtr getViewName() const override {
        return "ChaosModDisplay";
    }

    VSTGUI::IdStringPtr getBaseViewName() const override {
        return VSTGUI::UIViewCreator::kCView;
    }

    VSTGUI::UTF8StringPtr getDisplayName() const override {
        return "Chaos Mod Display";
    }

    VSTGUI::CView* create(
        const VSTGUI::UIAttributes& /*attributes*/,
        const VSTGUI::IUIDescription* /*description*/) const override {
        return new ChaosModDisplay(VSTGUI::CRect(0, 0, 510, 230));
    }

    bool apply(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
               const VSTGUI::IUIDescription* /*description*/) const override {
        auto* display = dynamic_cast<ChaosModDisplay*>(view);
        if (!display) return false;

        if (auto colorStr = attributes.getAttributeValue("chaos-color"))
            display->setColorFromString(*colorStr);

        return true;
    }

    bool getAttributeNames(
        VSTGUI::IViewCreator::StringList& attributeNames) const override {
        attributeNames.emplace_back("chaos-color");
        return true;
    }

    AttrType getAttributeType(const std::string& attributeName) const override {
        if (attributeName == "chaos-color") return kStringType;
        return kUnknownType;
    }

    bool getAttributeValue(VSTGUI::CView* view,
                           const std::string& attributeName,
                           std::string& stringValue,
                           const VSTGUI::IUIDescription* /*desc*/) const override {
        auto* display = dynamic_cast<ChaosModDisplay*>(view);
        if (!display) return false;
        if (attributeName == "chaos-color") {
            stringValue = display->getColorString();
            return true;
        }
        return false;
    }
};

/// Inline variable (C++17) -- safe for inclusion from multiple translation units.
inline ChaosModDisplayCreator gChaosModDisplayCreator;

} // namespace Krate::Plugins
