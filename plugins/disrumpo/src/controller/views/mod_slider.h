#pragma once

// ==============================================================================
// ModSlider - CSlider subclass with modulation range visualization
// ==============================================================================
// Draws a colored bar extending from the base parameter value to the
// modulated value, providing visual feedback when modulation is active.
//
// Registered as "ModSlider" via VSTGUI ViewCreator system.
// Inherits all CSlider attributes (drawStyle, colors, frame, etc.)
// via getBaseViewName() -> "CSlider" chain.
//
// Constitution Compliance:
// - Principle V: VSTGUI cross-platform (no native code)
// ==============================================================================

#include "vstgui/lib/controls/cslider.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/uidescription/iviewcreator.h"
#include "vstgui/uidescription/uiviewfactory.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace Disrumpo {

/// @brief CSlider with modulation range overlay for visual feedback.
class ModSlider : public VSTGUI::CSlider {
public:
    ModSlider(const VSTGUI::CRect& size, VSTGUI::IControlListener* listener,
              int32_t tag)
        : CSlider(size, listener, tag, 0, 1, nullptr, nullptr) {}

    ModSlider(const ModSlider& other) = default;

    /// @brief Set the current modulation offset (normalized, [-1, +1]).
    /// Only invalidates if the value actually changed to avoid unnecessary redraws.
    void setModulationOffset(float offset) noexcept {
        if (std::abs(offset - modOffset_) > 0.0005f) {
            modOffset_ = offset;
            setDirty();
        }
    }

    [[nodiscard]] float getModulationOffset() const noexcept { return modOffset_; }

    /// @brief Set the modulation destination ID (from ModDest namespace).
    void setModDestId(uint32_t destId) noexcept { modDestId_ = destId; }
    [[nodiscard]] uint32_t getModDestId() const noexcept { return modDestId_; }

    /// @brief Set the modulation indicator color.
    void setModColor(VSTGUI::CColor color) noexcept { modColor_ = color; }

    void draw(VSTGUI::CDrawContext* context) override {
        // Draw the base CSlider (background, frame, value bar, handle)
        CSlider::draw(context);

        // Draw modulation range overlay if active
        if (std::abs(modOffset_) < 0.001f)
            return;

        const float baseValue = getValueNormalized();
        const float modValue = std::clamp(baseValue + modOffset_, 0.0f, 1.0f);

        // Skip if base and modulated are effectively the same position
        if (std::abs(modValue - baseValue) < 0.001f)
            return;

        VSTGUI::CRect r(getViewSize());

        // Inset for frame if present
        auto lineWidth = getFrameWidth();
        if (lineWidth < 0.)
            lineWidth = context->getHairlineSize();
        if (getDrawStyle() & kDrawFrame)
            r.inset(lineWidth / 2., lineWidth / 2.);

        // Calculate the modulation bar rectangle
        const float minVal = std::min(baseValue, modValue);
        const float maxVal = std::max(baseValue, modValue);

        VSTGUI::CRect modRect;
        if (isStyleHorizontal()) {
            const auto width = r.getWidth();
            if (getDrawStyle() & kDrawInverted) {
                modRect = r;
                modRect.left = r.right - width * maxVal;
                modRect.right = r.right - width * minVal;
            } else {
                modRect = r;
                modRect.left = r.left + width * minVal;
                modRect.right = r.left + width * maxVal;
            }
        } else {
            const auto height = r.getHeight();
            if (getDrawStyle() & kDrawInverted) {
                modRect = r;
                modRect.top = r.top + height * minVal;
                modRect.bottom = r.top + height * maxVal;
            } else {
                modRect = r;
                modRect.top = r.bottom - height * maxVal;
                modRect.bottom = r.bottom - height * minVal;
            }
        }

        modRect.normalize();

        if (modRect.getWidth() >= 0.5 && modRect.getHeight() >= 0.5) {
            context->setDrawMode(VSTGUI::kAliasing);
            context->setFillColor(modColor_);
            if (auto path =
                    VSTGUI::owned(context->createGraphicsPath())) {
                path->addRect(modRect);
                context->drawGraphicsPath(
                    path, VSTGUI::CDrawContext::kPathFilled);
            } else {
                context->drawRect(modRect, VSTGUI::kDrawFilled);
            }
        }

        setDirty(false);
    }

    CLASS_METHODS(ModSlider, CSlider)

private:
    float modOffset_ = 0.0f;
    uint32_t modDestId_ = 0;
    VSTGUI::CColor modColor_{100, 200, 255, 140}; // Semi-transparent cyan
};

// =============================================================================
// ViewCreator registration
// =============================================================================
// Registers "ModSlider" with the VSTGUI UIViewFactory.
// getBaseViewName() -> "CSlider" ensures all CSlider attributes
// (drawStyle, colors, frame, handle, etc.) are applied automatically.

struct ModSliderCreator : VSTGUI::ViewCreatorAdapter {
    ModSliderCreator() {
        VSTGUI::UIViewFactory::registerViewCreator(*this);
    }

    VSTGUI::IdStringPtr getViewName() const override { return "ModSlider"; }
    VSTGUI::IdStringPtr getBaseViewName() const override { return "CSlider"; }

    VSTGUI::CView* create(
        const VSTGUI::UIAttributes& /*attributes*/,
        const VSTGUI::IUIDescription* /*description*/) const override {
        return new ModSlider(VSTGUI::CRect(0, 0, 0, 0), nullptr, -1);
    }
};

// Static instance auto-registers with UIViewFactory at startup
static ModSliderCreator __gModSliderCreator;

} // namespace Disrumpo
