#pragma once

// ==============================================================================
// AnimatedModDisplay - Base class for animated modulation visualizers
// ==============================================================================
// Provides shared infrastructure for all mod source display views:
// - Color configuration (hex string parse/format, CColor get/set)
// - Animation timer lifecycle (~30fps, start/stop tied to attach/detach)
// - Subclass hook: onTimerTick() called each frame before invalidation
//
// Subclasses implement draw() and onTimerTick() for specific visualizations.
// ==============================================================================

#include "vstgui/lib/cview.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cvstguitimer.h"

#include <cstdint>
#include <cstdio>
#include <string>

namespace Krate::Plugins {

class AnimatedModDisplay : public VSTGUI::CView {
public:
    static constexpr uint32_t kTimerIntervalMs = 33; // ~30fps

    explicit AnimatedModDisplay(const VSTGUI::CRect& size)
        : CView(size) {}

    AnimatedModDisplay(const AnimatedModDisplay& other)
        : CView(other)
        , color_(other.color_) {}

    ~AnimatedModDisplay() override {
        stopTimer();
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

protected:
    /// Called at ~30fps by the animation timer. Subclass performs simulation
    /// steps and state updates. View invalidation happens automatically after.
    virtual void onTimerTick() = 0;

    /// Stop the animation timer. Call from derived destructors to prevent
    /// timer callbacks after derived state is destroyed.
    void stopTimer() {
        animTimer_ = nullptr;
    }

    VSTGUI::CColor color_{90, 200, 130, 255}; // Default: modulation green

private:
    void startTimer() {
        if (animTimer_) return;
        animTimer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>(
            [this](VSTGUI::CVSTGUITimer*) {
                onTimerTick();
                invalid();
            }, kTimerIntervalMs);
    }

    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> animTimer_;
};

} // namespace Krate::Plugins
