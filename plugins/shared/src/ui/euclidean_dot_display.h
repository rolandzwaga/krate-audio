// ==============================================================================
// EuclideanDotDisplay
// ==============================================================================
// Circular Euclidean pattern visualization CView.
// Draws a ring of dots: filled dots for hits, stroked dots for rests.
// Uses EuclideanPattern::generate() from KrateDSP Layer 0.
//
// Registered as "EuclideanDotDisplay" via VSTGUI ViewCreator system.
//
// Phase 11c - User Story 5: Euclidean Dual Visualization
// ==============================================================================

#pragma once

#include "vstgui/lib/cview.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/uidescription/iviewcreator.h"
#include "vstgui/uidescription/uiviewfactory.h"
#include "vstgui/uidescription/uiviewcreator.h"
#include "vstgui/uidescription/uiattributes.h"
#include "vstgui/uidescription/detail/uiviewcreatorattributes.h"

#include <krate/dsp/core/euclidean_pattern.h>

#include <algorithm>
#include <cmath>
#include <string>

namespace Krate::Plugins {

// ==============================================================================
// EuclideanDotDisplay
// ==============================================================================

class EuclideanDotDisplay : public VSTGUI::CView {
public:
    explicit EuclideanDotDisplay(const VSTGUI::CRect& size)
        : CView(size) {
        setTransparency(true);
    }

    EuclideanDotDisplay(const EuclideanDotDisplay& other)
        : CView(other)
        , hits_(other.hits_)
        , steps_(other.steps_)
        , rotation_(other.rotation_)
        , dotRadius_(other.dotRadius_)
        , accentColor_(other.accentColor_)
        , outlineColor_(other.outlineColor_) {}

    // =========================================================================
    // Properties
    // =========================================================================

    void setHits(int hits) {
        hits_ = std::clamp(hits, 0, steps_);
    }

    int getHits() const { return hits_; }

    void setSteps(int steps) {
        steps_ = std::clamp(steps, kMinSteps, kMaxSteps);
        // Re-clamp hits and rotation to new steps range
        hits_ = std::clamp(hits_, 0, steps_);
        rotation_ = std::clamp(rotation_, 0, steps_ - 1);
    }

    int getSteps() const { return steps_; }

    void setRotation(int rotation) {
        rotation_ = std::clamp(rotation, 0, steps_ - 1);
    }

    int getRotation() const { return rotation_; }

    void setDotRadius(float radius) {
        dotRadius_ = std::max(radius, 1.0f);
    }

    float getDotRadius() const { return dotRadius_; }

    void setAccentColor(const VSTGUI::CColor& color) {
        accentColor_ = color;
    }

    VSTGUI::CColor getAccentColor() const { return accentColor_; }

    // =========================================================================
    // CView Overrides
    // =========================================================================

    void draw(VSTGUI::CDrawContext* context) override {
        VSTGUI::CRect viewRect = getViewSize();

        float viewWidth = static_cast<float>(viewRect.getWidth());
        float viewHeight = static_cast<float>(viewRect.getHeight());

        // Calculate center and ring radius
        float centerX = static_cast<float>(viewRect.left) + viewWidth / 2.0f;
        float centerY = static_cast<float>(viewRect.top) + viewHeight / 2.0f;
        float ringRadius =
            std::min(viewWidth, viewHeight) / 2.0f - dotRadius_ - 2.0f;

        if (ringRadius <= 0.0f || steps_ < kMinSteps) return;

        // Generate pattern once
        uint32_t pattern =
            Krate::DSP::EuclideanPattern::generate(hits_, steps_, rotation_);

        constexpr float kPi = 3.14159265358979323846f;

        for (int i = 0; i < steps_; ++i) {
            // Angle: start from top (-PI/2), go clockwise
            float angle = -kPi / 2.0f +
                          2.0f * kPi * static_cast<float>(i) /
                              static_cast<float>(steps_);

            float dotX = centerX + ringRadius * std::cos(angle);
            float dotY = centerY + ringRadius * std::sin(angle);

            VSTGUI::CRect dotRect(
                static_cast<double>(dotX - dotRadius_),
                static_cast<double>(dotY - dotRadius_),
                static_cast<double>(dotX + dotRadius_),
                static_cast<double>(dotY + dotRadius_));

            if (Krate::DSP::EuclideanPattern::isHit(pattern, i, steps_)) {
                // Filled dot for hit
                context->setFillColor(accentColor_);
                context->drawEllipse(dotRect, VSTGUI::kDrawFilled);
            } else {
                // Stroked dot for rest
                context->setFrameColor(outlineColor_);
                context->setLineWidth(1.0);
                context->drawEllipse(dotRect, VSTGUI::kDrawStroked);
            }
        }
    }

    CLASS_METHODS(EuclideanDotDisplay, CView)

private:
    static constexpr int kMinSteps = 2;
    static constexpr int kMaxSteps = 32;

    int hits_ = 0;
    int steps_ = 8;
    int rotation_ = 0;
    float dotRadius_ = 3.0f;
    VSTGUI::CColor accentColor_{208, 132, 92, 255};
    VSTGUI::CColor outlineColor_{80, 80, 85, 255};
};

// ==============================================================================
// ViewCreator Registration
// ==============================================================================

struct EuclideanDotDisplayCreator : VSTGUI::ViewCreatorAdapter {
    EuclideanDotDisplayCreator() {
        VSTGUI::UIViewFactory::registerViewCreator(*this);
    }

    VSTGUI::IdStringPtr getViewName() const override {
        return "EuclideanDotDisplay";
    }

    VSTGUI::IdStringPtr getBaseViewName() const override {
        return VSTGUI::UIViewCreator::kCView;
    }

    VSTGUI::UTF8StringPtr getDisplayName() const override {
        return "Euclidean Dot Display";
    }

    VSTGUI::CView* create(
        const VSTGUI::UIAttributes& /*attributes*/,
        const VSTGUI::IUIDescription* /*description*/) const override {
        return new EuclideanDotDisplay(VSTGUI::CRect(0, 0, 60, 60));
    }

    bool apply(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
               const VSTGUI::IUIDescription* description) const override {
        auto* display = dynamic_cast<EuclideanDotDisplay*>(view);
        if (!display) return false;

        int32_t intVal;
        if (attributes.getIntegerAttribute("hits", intVal))
            display->setHits(static_cast<int>(intVal));
        if (attributes.getIntegerAttribute("steps", intVal))
            display->setSteps(static_cast<int>(intVal));
        if (attributes.getIntegerAttribute("rotation", intVal))
            display->setRotation(static_cast<int>(intVal));

        double d;
        if (attributes.getDoubleAttribute("dot-radius", d))
            display->setDotRadius(static_cast<float>(d));

        VSTGUI::CColor color;
        if (VSTGUI::UIViewCreator::stringToColor(
                attributes.getAttributeValue("accent-color"), color,
                description))
            display->setAccentColor(color);

        return true;
    }

    bool getAttributeNames(
        VSTGUI::IViewCreator::StringList& attributeNames) const override {
        attributeNames.emplace_back("hits");
        attributeNames.emplace_back("steps");
        attributeNames.emplace_back("rotation");
        attributeNames.emplace_back("dot-radius");
        attributeNames.emplace_back("accent-color");
        return true;
    }

    AttrType getAttributeType(
        const std::string& attributeName) const override {
        if (attributeName == "hits") return kIntegerType;
        if (attributeName == "steps") return kIntegerType;
        if (attributeName == "rotation") return kIntegerType;
        if (attributeName == "dot-radius") return kFloatType;
        if (attributeName == "accent-color") return kColorType;
        return kUnknownType;
    }

    bool getAttributeValue(VSTGUI::CView* view,
                           const std::string& attributeName,
                           std::string& stringValue,
                           const VSTGUI::IUIDescription* desc) const override {
        auto* display = dynamic_cast<EuclideanDotDisplay*>(view);
        if (!display) return false;

        if (attributeName == "hits") {
            stringValue = std::to_string(display->getHits());
            return true;
        }
        if (attributeName == "steps") {
            stringValue = std::to_string(display->getSteps());
            return true;
        }
        if (attributeName == "rotation") {
            stringValue = std::to_string(display->getRotation());
            return true;
        }
        if (attributeName == "dot-radius") {
            stringValue = VSTGUI::UIAttributes::doubleToString(
                display->getDotRadius());
            return true;
        }
        if (attributeName == "accent-color") {
            VSTGUI::UIViewCreator::colorToString(
                display->getAccentColor(), stringValue, desc);
            return true;
        }
        return false;
    }
};

inline EuclideanDotDisplayCreator gEuclideanDotDisplayCreator;

} // namespace Krate::Plugins
