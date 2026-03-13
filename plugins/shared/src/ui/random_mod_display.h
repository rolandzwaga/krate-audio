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
// RandomModDisplay View
// ==============================================================================

class RandomModDisplay : public VSTGUI::CView {
public:
    static constexpr int kHistoryLength = 256;
    static constexpr uint32_t kTimerIntervalMs = 33;   // ~30fps
    static constexpr int kStepsPerFrame = 64;

    // Lightweight Xorshift32 PRNG (mirrors DSP)
    struct Rng {
        uint32_t state = 98765;
        uint32_t next() noexcept {
            state ^= state << 13;
            state ^= state >> 17;
            state ^= state << 5;
            return state;
        }
        float nextFloat() noexcept {
            return static_cast<float>(static_cast<int32_t>(next())) / 2147483648.0f;
        }
    };

    // =========================================================================
    // Construction / Destruction
    // =========================================================================

    explicit RandomModDisplay(const VSTGUI::CRect& size)
        : CView(size) {
        history_.fill(0.0f);
    }

    RandomModDisplay(const RandomModDisplay& other)
        : CView(other)
        , color_(other.color_)
        , rate_(other.rate_)
        , smoothness_(other.smoothness_) {
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
    // CView Overrides
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
    // Drawing
    // =========================================================================

    void draw(VSTGUI::CDrawContext* context) override {
        auto r = getViewSize();

        // Rounded border background
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

        constexpr double kMargin = 10.0;
        double plotLeft = r.left + kMargin;
        double plotTop = r.top + kMargin;
        double plotW = r.getWidth() - 2.0 * kMargin;
        double plotH = r.getHeight() - 2.0 * kMargin;

        // Inner panel background
        VSTGUI::CRect plotRect(plotLeft, plotTop, plotLeft + plotW, plotTop + plotH);
        {
            auto* bgPath = context->createGraphicsPath();
            if (bgPath) {
                bgPath->addRoundRect(plotRect, 4.0);
                context->setFillColor(VSTGUI::CColor(16, 16, 20, 255));
                context->drawGraphicsPath(bgPath, VSTGUI::CDrawContext::kPathFilled);
                bgPath->forget();
            }
        }

        // Zero line (center, bipolar [-1, +1])
        double zeroY = plotTop + plotH * 0.5;
        {
            const VSTGUI::CLineStyle::CoordVector dashes = {4.0, 4.0};
            VSTGUI::CLineStyle dashedStyle(
                VSTGUI::CLineStyle::kLineCapButt,
                VSTGUI::CLineStyle::kLineJoinMiter,
                0.0, dashes);
            context->setFrameColor(VSTGUI::CColor(160, 160, 165, 30));
            context->setLineWidth(1.0);
            context->setLineStyle(dashedStyle);
            context->drawLine(VSTGUI::CPoint(plotLeft + 4, zeroY),
                              VSTGUI::CPoint(plotLeft + plotW - 4, zeroY));
        }

        int count = std::min(historyCount_, kHistoryLength);
        if (count < 2) { setDirty(false); return; }

        constexpr double kPadX = 4.0;
        constexpr double kPadY = 8.0;
        double innerLeft = plotLeft + kPadX;
        double innerW = plotW - 2.0 * kPadX;
        double innerTop = plotTop + kPadY;
        double innerH = plotH - 2.0 * kPadY;

        auto mapX = [&](int i) -> double {
            return innerLeft + (static_cast<double>(i) / (count - 1)) * innerW;
        };
        auto mapY = [&](float v) -> double {
            return innerTop + (1.0 - (static_cast<double>(v) + 1.0) * 0.5) * innerH;
        };

        auto getVal = [&](int i) -> float {
            int idx = (historyHead_ - count + i + kHistoryLength) % kHistoryLength;
            return history_[idx];
        };

        // Build waveform paths
        auto* fillPath = context->createGraphicsPath();
        auto* strokePath = context->createGraphicsPath();
        if (!fillPath || !strokePath) {
            if (fillPath) fillPath->forget();
            if (strokePath) strokePath->forget();
            setDirty(false);
            return;
        }

        fillPath->beginSubpath(VSTGUI::CPoint(mapX(0), zeroY));
        float firstVal = getVal(0);
        fillPath->addLine(VSTGUI::CPoint(mapX(0), mapY(firstVal)));
        strokePath->beginSubpath(VSTGUI::CPoint(mapX(0), mapY(firstVal)));

        for (int i = 1; i < count; ++i) {
            VSTGUI::CPoint pt(mapX(i), mapY(getVal(i)));
            fillPath->addLine(pt);
            strokePath->addLine(pt);
        }

        fillPath->addLine(VSTGUI::CPoint(mapX(count - 1), zeroY));
        fillPath->closeSubpath();

        // Filled area
        context->setFillColor(VSTGUI::CColor(color_.red, color_.green, color_.blue, 25));
        context->drawGraphicsPath(fillPath, VSTGUI::CDrawContext::kPathFilled);
        fillPath->forget();

        // Waveform stroke
        context->setFrameColor(color_);
        context->setLineWidth(1.5);
        context->setLineStyle(VSTGUI::CLineStyle(
            VSTGUI::CLineStyle::kLineCapRound,
            VSTGUI::CLineStyle::kLineJoinRound));
        context->drawGraphicsPath(strokePath, VSTGUI::CDrawContext::kPathStroked);
        strokePath->forget();

        // Current value dot at right edge
        if (count > 0) {
            float newestVal = getVal(count - 1);
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

        setDirty(false);
    }

    CLASS_METHODS(RandomModDisplay, CView)

private:
    // =========================================================================
    // Random Source Simulation (UI thread only)
    // =========================================================================

    void stepRandom() {
        constexpr float kSimSampleRate = 1000.0f;

        float phaseInc = rate_ / kSimSampleRate;
        phase_ += phaseInc;

        if (phase_ >= 1.0f) {
            phase_ -= 1.0f;
            currentTarget_ = rng_.nextFloat();
        }

        // Apply smoothing (mirrors DSP: smoothness * 200ms)
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

    // =========================================================================
    // Animation Timer
    // =========================================================================

    void startTimer() {
        if (animTimer_) return;
        animTimer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>(
            [this](VSTGUI::CVSTGUITimer*) {
                for (int i = 0; i < kStepsPerFrame; ++i) {
                    stepRandom();
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

    // Parameters
    float rate_ = 4.0f;          // [0.1, 50] Hz
    float smoothness_ = 0.0f;    // [0, 1]

    // Simulation state
    float phase_ = 0.0f;
    float currentTarget_ = 0.0f;
    float smoothedValue_ = 0.0f;
    Rng rng_{};

    // History ring buffer
    std::array<float, kHistoryLength> history_{};
    int historyHead_ = 0;
    int historyCount_ = 0;

    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> animTimer_;
};

// =============================================================================
// ViewCreator Registration
// =============================================================================

struct RandomModDisplayCreator : VSTGUI::ViewCreatorAdapter {
    RandomModDisplayCreator() {
        VSTGUI::UIViewFactory::registerViewCreator(*this);
    }

    VSTGUI::IdStringPtr getViewName() const override {
        return "RandomModDisplay";
    }

    VSTGUI::IdStringPtr getBaseViewName() const override {
        return VSTGUI::UIViewCreator::kCView;
    }

    VSTGUI::UTF8StringPtr getDisplayName() const override {
        return "Random Mod Display";
    }

    VSTGUI::CView* create(
        const VSTGUI::UIAttributes& /*attributes*/,
        const VSTGUI::IUIDescription* /*description*/) const override {
        return new RandomModDisplay(VSTGUI::CRect(0, 0, 510, 290));
    }

    bool apply(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
               const VSTGUI::IUIDescription* /*description*/) const override {
        auto* display = dynamic_cast<RandomModDisplay*>(view);
        if (!display) return false;

        if (auto colorStr = attributes.getAttributeValue("random-color"))
            display->setColorFromString(*colorStr);

        return true;
    }

    bool getAttributeNames(
        VSTGUI::IViewCreator::StringList& attributeNames) const override {
        attributeNames.emplace_back("random-color");
        return true;
    }

    AttrType getAttributeType(const std::string& attributeName) const override {
        if (attributeName == "random-color") return kStringType;
        return kUnknownType;
    }

    bool getAttributeValue(VSTGUI::CView* view,
                           const std::string& attributeName,
                           std::string& stringValue,
                           const VSTGUI::IUIDescription* /*desc*/) const override {
        auto* display = dynamic_cast<RandomModDisplay*>(view);
        if (!display) return false;
        if (attributeName == "random-color") {
            stringValue = display->getColorString();
            return true;
        }
        return false;
    }
};

inline RandomModDisplayCreator gRandomModDisplayCreator;

} // namespace Krate::Plugins
