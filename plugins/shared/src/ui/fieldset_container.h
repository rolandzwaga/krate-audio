#pragma once

// ==============================================================================
// FieldsetContainer - HTML fieldset-style container with rounded outline
// ==============================================================================
// A minimal container that draws a rounded border around its children,
// with an optional title label that creates a gap in the top edge.
//
// Visual elements:
// 1. Rounded outline: 1px border with configurable corner radius
// 2. Title gap: outline breaks where the title text is rendered
// 3. Corner highlight: top-left corner gleams slightly brighter
//
// Inherits CViewContainer for child view management, hit testing, and
// background drawing. Default background is transparent.
//
// Registered as "FieldsetContainer" via VSTGUI ViewCreator system.
// ==============================================================================

#include "color_utils.h"

#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/uidescription/iviewcreator.h"
#include "vstgui/uidescription/uiviewfactory.h"
#include "vstgui/uidescription/uiviewcreator.h"
#include "vstgui/uidescription/uiattributes.h"
#include "vstgui/uidescription/detail/uiviewcreatorattributes.h"

#include <algorithm>
#include <cmath>
#include <string>

namespace Krate::Plugins {

// ==============================================================================
// FieldsetContainer
// ==============================================================================

class FieldsetContainer : public VSTGUI::CViewContainer {
public:
    explicit FieldsetContainer(const VSTGUI::CRect& size)
        : CViewContainer(size) {}

    FieldsetContainer(const FieldsetContainer& other)
        : CViewContainer(other)
        , title_(other.title_)
        , color_(other.color_)
        , cornerRadius_(other.cornerRadius_)
        , lineWidth_(other.lineWidth_)
        , titleFontSize_(other.titleFontSize_) {}

    // =========================================================================
    // Color (single color for outline + title text)
    // =========================================================================

    void setColor(VSTGUI::CColor color) noexcept { color_ = color; setDirty(); }
    [[nodiscard]] VSTGUI::CColor getColor() const noexcept { return color_; }

    // =========================================================================
    // Title
    // =========================================================================

    void setTitle(const std::string& title) { title_ = title; setDirty(); }
    [[nodiscard]] const std::string& getTitle() const noexcept { return title_; }

    // =========================================================================
    // Geometry
    // =========================================================================

    void setCornerRadius(VSTGUI::CCoord radius) noexcept { cornerRadius_ = radius; setDirty(); }
    [[nodiscard]] VSTGUI::CCoord getCornerRadius() const noexcept { return cornerRadius_; }

    void setLineWidth(VSTGUI::CCoord width) noexcept { lineWidth_ = width; setDirty(); }
    [[nodiscard]] VSTGUI::CCoord getLineWidth() const noexcept { return lineWidth_; }

    void setTitleFontSize(VSTGUI::CCoord size) noexcept { titleFontSize_ = size; setDirty(); }
    [[nodiscard]] VSTGUI::CCoord getTitleFontSize() const noexcept { return titleFontSize_; }

    // =========================================================================
    // Drawing
    // =========================================================================

    void drawBackgroundRect(VSTGUI::CDrawContext* context,
                            const VSTGUI::CRect& _updateRect) override {
        // Let parent draw standard background (fill color if set)
        CViewContainer::drawBackgroundRect(context, _updateRect);

        context->setDrawMode(VSTGUI::kAntiAliasing | VSTGUI::kNonIntegralMode);

        // Compute outline rect in LOCAL coordinates (0,0 origin).
        // drawBackgroundRect is called with the context already translated
        // by the container's absolute position, so we must use relative coords.
        VSTGUI::CRect vs = getViewSize();
        VSTGUI::CCoord width = vs.getWidth();
        VSTGUI::CCoord height = vs.getHeight();
        VSTGUI::CCoord halfLine = lineWidth_ / 2.0;
        VSTGUI::CCoord titleOffset = title_.empty() ? 0.0 : titleFontSize_ / 2.0;

        VSTGUI::CRect outlineRect(
            halfLine,
            titleOffset + halfLine,
            width - halfLine,
            height - halfLine
        );

        drawOutline(context, outlineRect);
        drawHighlight(context, outlineRect);

        if (!title_.empty()) {
            drawTitle(context, outlineRect);
        }
    }

    CLASS_METHODS(FieldsetContainer, CViewContainer)

private:
    // =========================================================================
    // Drawing Helpers
    // =========================================================================

    /// Compute the title text width for gap calculation.
    /// Must be called after setFont() on the context.
    [[nodiscard]] VSTGUI::CCoord getTitleWidth(VSTGUI::CDrawContext* context) const {
        return context->getStringWidth(VSTGUI::UTF8String(title_));
    }

    /// Draw the rounded outline, with a gap for the title if present.
    void drawOutline(VSTGUI::CDrawContext* context,
                     const VSTGUI::CRect& outlineRect) const {
        auto path = VSTGUI::owned(context->createGraphicsPath());
        if (!path)
            return;

        context->setFrameColor(color_);
        context->setLineWidth(lineWidth_);
        context->setLineStyle(VSTGUI::kLineSolid);

        VSTGUI::CCoord left   = outlineRect.left;
        VSTGUI::CCoord right  = outlineRect.right;
        VSTGUI::CCoord top    = outlineRect.top;
        VSTGUI::CCoord bottom = outlineRect.bottom;
        VSTGUI::CCoord r = std::min(cornerRadius_,
                            std::min(outlineRect.getWidth(),
                                     outlineRect.getHeight()) / 2.0);

        if (title_.empty()) {
            // Simple case: full rounded rect
            path->addRoundRect(outlineRect, r);
        } else {
            // Set font for text measurement
            auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", titleFontSize_);
            context->setFont(font);
            VSTGUI::CCoord titleWidth = getTitleWidth(context);

            constexpr VSTGUI::CCoord kTitlePaddingLeft = 8.0;
            constexpr VSTGUI::CCoord kTitleGapPad = 4.0;

            VSTGUI::CCoord titleX = left + r + kTitlePaddingLeft;
            VSTGUI::CCoord gapStart = titleX - kTitleGapPad;
            VSTGUI::CCoord gapEnd = titleX + titleWidth + kTitleGapPad;

            // Clamp gap to not intrude into corner arcs
            gapStart = std::max(gapStart, left + r);
            gapEnd = std::min(gapEnd, right - r);

            // Single open subpath from gapEnd clockwise around to gapStart.
            // The gap between gapStart and gapEnd on the top edge stays open.
            path->beginSubpath(VSTGUI::CPoint(gapEnd, top));

            // Top edge → top-right corner
            path->addLine(VSTGUI::CPoint(right - r, top));
            path->addArc(VSTGUI::CRect(right - 2 * r, top,
                                        right, top + 2 * r),
                         270, 360, true);

            // Right edge → bottom-right corner
            path->addLine(VSTGUI::CPoint(right, bottom - r));
            path->addArc(VSTGUI::CRect(right - 2 * r, bottom - 2 * r,
                                        right, bottom),
                         0, 90, true);

            // Bottom edge → bottom-left corner
            path->addLine(VSTGUI::CPoint(left + r, bottom));
            path->addArc(VSTGUI::CRect(left, bottom - 2 * r,
                                        left + 2 * r, bottom),
                         90, 180, true);

            // Left edge → top-left corner
            path->addLine(VSTGUI::CPoint(left, top + r));
            path->addArc(VSTGUI::CRect(left, top,
                                        left + 2 * r, top + 2 * r),
                         180, 270, true);

            // Top edge up to gap start
            path->addLine(VSTGUI::CPoint(gapStart, top));
            // No closeSubpath() — leaves the gap open
        }

        context->drawGraphicsPath(path, VSTGUI::CDrawContext::kPathStroked);
    }

    /// Draw a subtle highlight on the top-left corner, fading along both edges.
    void drawHighlight(VSTGUI::CDrawContext* context,
                       const VSTGUI::CRect& outlineRect) const {
        VSTGUI::CCoord left = outlineRect.left;
        VSTGUI::CCoord top  = outlineRect.top;
        VSTGUI::CCoord r = std::min(cornerRadius_,
                            std::min(outlineRect.getWidth(),
                                     outlineRect.getHeight()) / 2.0);

        VSTGUI::CColor bright = brightenColor(color_, 1.8f);

        context->setLineWidth(lineWidth_);
        context->setLineStyle(VSTGUI::kLineSolid);

        // Overdraw the top-left corner arc in bright color
        {
            auto arcPath = VSTGUI::owned(context->createGraphicsPath());
            if (arcPath) {
                arcPath->addArc(
                    VSTGUI::CRect(left, top, left + 2 * r, top + 2 * r),
                    180, 270, true);
                context->setFrameColor(bright);
                context->drawGraphicsPath(arcPath,
                                          VSTGUI::CDrawContext::kPathStroked);
            }
        }

        // Fade along top edge: from (left + r, top) rightward
        // If there's a title, stop before the gap so we don't draw under the text
        constexpr int kSegments = 5;
        constexpr VSTGUI::CCoord kFadeLength = 25.0;
        VSTGUI::CCoord topFadeEnd = std::min(
            left + r + kFadeLength, outlineRect.right - r);

        if (!title_.empty()) {
            constexpr VSTGUI::CCoord kTitlePaddingLeft = 8.0;
            constexpr VSTGUI::CCoord kTitleGapPad = 4.0;
            VSTGUI::CCoord gapStart = std::max(
                left + r + kTitlePaddingLeft - kTitleGapPad, left + r);
            topFadeEnd = std::min(topFadeEnd, gapStart);
        }

        for (int i = 0; i < kSegments; ++i) {
            float t0 = static_cast<float>(i) / static_cast<float>(kSegments);
            float t1 = static_cast<float>(i + 1) / static_cast<float>(kSegments);
            VSTGUI::CColor segColor = lerpColor(bright, color_, (t0 + t1) / 2.0f);

            VSTGUI::CCoord x0 = left + r + t0 * kFadeLength;
            VSTGUI::CCoord x1 = left + r + t1 * kFadeLength;
            if (x0 >= topFadeEnd) break;
            if (x1 > topFadeEnd) x1 = topFadeEnd;

            context->setFrameColor(segColor);
            context->drawLine(VSTGUI::CPoint(x0, top),
                              VSTGUI::CPoint(x1, top));
        }

        // Fade along left edge: from (left, top + r) downward
        VSTGUI::CCoord leftFadeEnd = std::min(
            top + r + kFadeLength, outlineRect.bottom - r);

        for (int i = 0; i < kSegments; ++i) {
            float t0 = static_cast<float>(i) / static_cast<float>(kSegments);
            float t1 = static_cast<float>(i + 1) / static_cast<float>(kSegments);
            VSTGUI::CColor segColor = lerpColor(bright, color_, (t0 + t1) / 2.0f);

            VSTGUI::CCoord y0 = top + r + t0 * kFadeLength;
            VSTGUI::CCoord y1 = top + r + t1 * kFadeLength;
            if (y1 > leftFadeEnd) y1 = leftFadeEnd;

            context->setFrameColor(segColor);
            context->drawLine(VSTGUI::CPoint(left, y0),
                              VSTGUI::CPoint(left, y1));
        }
    }

    /// Draw the title text centered vertically on the top edge of the outline.
    void drawTitle(VSTGUI::CDrawContext* context,
                   const VSTGUI::CRect& outlineRect) const {
        VSTGUI::CCoord r = std::min(cornerRadius_,
                            std::min(outlineRect.getWidth(),
                                     outlineRect.getHeight()) / 2.0);

        constexpr VSTGUI::CCoord kTitlePaddingLeft = 8.0;

        auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", titleFontSize_);
        context->setFont(font);
        context->setFontColor(color_);

        VSTGUI::CCoord titleX = outlineRect.left + r + kTitlePaddingLeft;
        VSTGUI::CCoord titleWidth = getTitleWidth(context);

        VSTGUI::CRect titleRect(
            titleX,
            outlineRect.top - titleFontSize_ / 2.0,
            titleX + titleWidth,
            outlineRect.top + titleFontSize_ / 2.0
        );

        context->drawString(VSTGUI::UTF8String(title_), titleRect,
                             VSTGUI::kLeftText, true);
    }

    // =========================================================================
    // State
    // =========================================================================

    std::string title_;
    VSTGUI::CColor color_{60, 60, 64, 255};     // #3C3C40
    VSTGUI::CCoord cornerRadius_ = 4.0;
    VSTGUI::CCoord lineWidth_ = 1.0;
    VSTGUI::CCoord titleFontSize_ = 10.0;
};

// =============================================================================
// ViewCreator Registration
// =============================================================================
// Registers "FieldsetContainer" with the VSTGUI UIViewFactory.
// getBaseViewName() -> "CViewContainer" ensures all container attributes
// (background-color, background-color-draw-style, etc.) are applied.

struct FieldsetContainerCreator : VSTGUI::ViewCreatorAdapter {
    FieldsetContainerCreator() {
        VSTGUI::UIViewFactory::registerViewCreator(*this);
    }

    VSTGUI::IdStringPtr getViewName() const override {
        return "FieldsetContainer";
    }

    VSTGUI::IdStringPtr getBaseViewName() const override {
        return VSTGUI::UIViewCreator::kCViewContainer;
    }

    VSTGUI::UTF8StringPtr getDisplayName() const override {
        return "Fieldset Container";
    }

    VSTGUI::CView* create(
        const VSTGUI::UIAttributes& /*attributes*/,
        const VSTGUI::IUIDescription* /*description*/) const override {
        return new FieldsetContainer(VSTGUI::CRect(0, 0, 200, 100));
    }

    bool apply(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
               const VSTGUI::IUIDescription* description) const override {
        auto* container = dynamic_cast<FieldsetContainer*>(view);
        if (!container)
            return false;

        // Color attribute
        VSTGUI::CColor color;
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("fieldset-color"), color, description))
            container->setColor(color);

        // Title string attribute
        if (auto val = attributes.getAttributeValue("fieldset-title"))
            container->setTitle(*val);

        // Numeric attributes
        double d;
        if (attributes.getDoubleAttribute("fieldset-radius", d))
            container->setCornerRadius(d);
        if (attributes.getDoubleAttribute("fieldset-line-width", d))
            container->setLineWidth(d);
        if (attributes.getDoubleAttribute("fieldset-font-size", d))
            container->setTitleFontSize(d);

        return true;
    }

    bool getAttributeNames(
        VSTGUI::IViewCreator::StringList& attributeNames) const override {
        attributeNames.emplace_back("fieldset-color");
        attributeNames.emplace_back("fieldset-title");
        attributeNames.emplace_back("fieldset-radius");
        attributeNames.emplace_back("fieldset-line-width");
        attributeNames.emplace_back("fieldset-font-size");
        return true;
    }

    AttrType getAttributeType(
        const std::string& attributeName) const override {
        if (attributeName == "fieldset-color") return kColorType;
        if (attributeName == "fieldset-title") return kStringType;
        if (attributeName == "fieldset-radius") return kFloatType;
        if (attributeName == "fieldset-line-width") return kFloatType;
        if (attributeName == "fieldset-font-size") return kFloatType;
        return kUnknownType;
    }

    bool getAttributeValue(VSTGUI::CView* view,
                           const std::string& attributeName,
                           std::string& stringValue,
                           const VSTGUI::IUIDescription* desc) const override {
        auto* container = dynamic_cast<FieldsetContainer*>(view);
        if (!container)
            return false;

        if (attributeName == "fieldset-color") {
            VSTGUI::UIViewCreator::colorToString(
                container->getColor(), stringValue, desc);
            return true;
        }
        if (attributeName == "fieldset-title") {
            stringValue = container->getTitle();
            return true;
        }
        if (attributeName == "fieldset-radius") {
            stringValue = VSTGUI::UIAttributes::doubleToString(
                container->getCornerRadius());
            return true;
        }
        if (attributeName == "fieldset-line-width") {
            stringValue = VSTGUI::UIAttributes::doubleToString(
                container->getLineWidth());
            return true;
        }
        if (attributeName == "fieldset-font-size") {
            stringValue = VSTGUI::UIAttributes::doubleToString(
                container->getTitleFontSize());
            return true;
        }
        return false;
    }
};

// Inline variable (C++17) - safe for inclusion from multiple translation units.
// Include this header from each plugin's entry.cpp to register the view type.
inline FieldsetContainerCreator gFieldsetContainerCreator;

} // namespace Krate::Plugins
