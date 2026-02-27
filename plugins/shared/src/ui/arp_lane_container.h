#pragma once

// ==============================================================================
// ArpLaneContainer - Vertically-Scrolling Lane Container
// ==============================================================================
// A CViewContainer subclass that stacks IArpLane instances vertically,
// manages dynamic height on collapse/expand, and provides manual scroll offset.
//
// Children are added programmatically (NOT from XML). ArpLaneContainer manages
// the lane vector, collapse callbacks, and layout recalculation.
//
// Registered as "ArpLaneContainer" via VSTGUI ViewCreator system.
// ==============================================================================

#include "arp_lane.h"
#include "arp_lane_editor.h"

#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/events.h"
#include "vstgui/uidescription/iviewcreator.h"
#include "vstgui/uidescription/uiviewfactory.h"
#include "vstgui/uidescription/uiviewcreator.h"
#include "vstgui/uidescription/uiattributes.h"
#include "vstgui/uidescription/detail/uiviewcreatorattributes.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace Krate::Plugins {

// ==============================================================================
// ArpLaneContainer
// ==============================================================================

class ArpLaneContainer : public VSTGUI::CViewContainer {
public:
    // =========================================================================
    // Construction
    // =========================================================================

    explicit ArpLaneContainer(const VSTGUI::CRect& size)
        : CViewContainer(size) {
        setTransparency(true);
    }

    ArpLaneContainer(const ArpLaneContainer& other)
        : CViewContainer(other)
        , viewportHeight_(other.viewportHeight_)
        , scrollOffset_(other.scrollOffset_)
        , totalContentHeight_(other.totalContentHeight_) {
        // Note: lanes_ vector is NOT copied (child views are copied by CViewContainer)
    }

    // =========================================================================
    // Viewport
    // =========================================================================

    void setViewportHeight(float height) {
        viewportHeight_ = height;
    }

    [[nodiscard]] float getViewportHeight() const { return viewportHeight_; }

    // =========================================================================
    // Lane Management
    // =========================================================================

    void addLane(IArpLane* lane) {
        lanes_.push_back(lane);
        addView(lane->getView());

        // Set collapse callback so layout is recalculated when a lane
        // collapses or expands
        lane->setCollapseCallback([this]() {
            recalculateLayout();
        });

        recalculateLayout();
    }

    void removeLane(IArpLane* lane) {
        auto it = std::find(lanes_.begin(), lanes_.end(), lane);
        if (it != lanes_.end()) {
            lanes_.erase(it);
        }
        removeView(lane->getView(), true);
        recalculateLayout();
    }

    [[nodiscard]] size_t getLaneCount() const { return lanes_.size(); }

    [[nodiscard]] IArpLane* getLane(size_t index) const {
        if (index >= lanes_.size()) return nullptr;
        return lanes_[index];
    }

    // =========================================================================
    // Layout
    // =========================================================================

    void recalculateLayout() {
        float y = 0.0f;
        float containerWidth = static_cast<float>(getViewSize().getWidth());

        // First pass: calculate total content height
        for (auto* lane : lanes_) {
            float laneHeight = lane->isCollapsed()
                ? lane->getCollapsedHeight()
                : lane->getExpandedHeight();
            y += laneHeight;
        }

        totalContentHeight_ = y;

        // Clamp scroll offset before applying to positions
        float maxScroll = getMaxScrollOffset();
        scrollOffset_ = std::clamp(scrollOffset_, 0.0f, maxScroll);

        // Second pass: position lanes with scroll offset translation
        float contentY = 0.0f;
        for (auto* lane : lanes_) {
            float laneHeight = lane->isCollapsed()
                ? lane->getCollapsedHeight()
                : lane->getExpandedHeight();

            float visualY = contentY - scrollOffset_;
            VSTGUI::CRect laneRect(0.0, visualY, containerWidth,
                                    visualY + laneHeight);

            auto* view = lane->getView();
            view->setViewSize(laneRect);
            view->setMouseableArea(laneRect);

            contentY += laneHeight;
        }

        invalid();
    }

    [[nodiscard]] float getTotalContentHeight() const { return totalContentHeight_; }

    // =========================================================================
    // Scroll
    // =========================================================================

    [[nodiscard]] float getScrollOffset() const { return scrollOffset_; }

    void setScrollOffset(float offset) {
        scrollOffset_ = std::clamp(offset, 0.0f, getMaxScrollOffset());
        recalculateLayout();
    }

    [[nodiscard]] float getMaxScrollOffset() const {
        float visibleHeight = static_cast<float>(getViewSize().getHeight());
        return std::max(0.0f, totalContentHeight_ - visibleHeight);
    }

    /// Apply a wheel scroll delta (deltaY from MouseWheelEvent).
    /// Returns true if the scroll offset changed.
    /// Formula: scrollDelta = -wheelDeltaY * 20.0f (20px per wheel unit).
    bool scrollByWheelDelta(float wheelDeltaY) {
        float delta = -wheelDeltaY * 20.0f;
        float newOffset = std::clamp(
            scrollOffset_ + delta, 0.0f, getMaxScrollOffset());
        if (newOffset != scrollOffset_) {
            scrollOffset_ = newOffset;
            recalculateLayout();
            return true;
        }
        return false;
    }

    // =========================================================================
    // CViewContainer Overrides
    // =========================================================================

    void drawBackgroundRect(VSTGUI::CDrawContext* context,
                            const VSTGUI::CRect& _updateRect) override {
        // Fill with dark background
        VSTGUI::CColor bgColor{25, 25, 28, 255};
        context->setFillColor(bgColor);

        VSTGUI::CRect vs = getViewSize();
        VSTGUI::CCoord width = vs.getWidth();
        VSTGUI::CCoord height = vs.getHeight();
        VSTGUI::CRect localRect(0.0, 0.0, width, height);

        context->drawRect(localRect, VSTGUI::kDrawFilled);
    }

    void onMouseWheelEvent(VSTGUI::MouseWheelEvent& event) override {
        if (event.deltaY != 0.0) {
            if (scrollByWheelDelta(static_cast<float>(event.deltaY))) {
                event.consumed = true;
                return;
            }
        }
        CViewContainer::onMouseWheelEvent(event);
    }

    CLASS_METHODS(ArpLaneContainer, CViewContainer)

private:
    // =========================================================================
    // State
    // =========================================================================

    float viewportHeight_ = 390.0f;
    float scrollOffset_ = 0.0f;
    std::vector<IArpLane*> lanes_;
    float totalContentHeight_ = 0.0f;
};

// =============================================================================
// ViewCreator Registration
// =============================================================================

struct ArpLaneContainerCreator : VSTGUI::ViewCreatorAdapter {
    ArpLaneContainerCreator() {
        VSTGUI::UIViewFactory::registerViewCreator(*this);
    }

    VSTGUI::IdStringPtr getViewName() const override { return "ArpLaneContainer"; }

    VSTGUI::IdStringPtr getBaseViewName() const override {
        return VSTGUI::UIViewCreator::kCViewContainer;
    }

    VSTGUI::UTF8StringPtr getDisplayName() const override {
        return "Arp Lane Container";
    }

    VSTGUI::CView* create(
        const VSTGUI::UIAttributes& /*attributes*/,
        const VSTGUI::IUIDescription* /*description*/) const override {
        return new ArpLaneContainer(VSTGUI::CRect(0, 0, 500, 390));
    }

    bool apply(VSTGUI::CView* view, const VSTGUI::UIAttributes& attributes,
               [[maybe_unused]] const VSTGUI::IUIDescription* description) const override {
        auto* container = dynamic_cast<ArpLaneContainer*>(view);
        if (!container)
            return false;

        double value = 0.0;
        if (attributes.getDoubleAttribute("viewport-height", value))
            container->setViewportHeight(static_cast<float>(value));

        return true;
    }

    bool getAttributeNames(
        VSTGUI::IViewCreator::StringList& attributeNames) const override {
        attributeNames.emplace_back("viewport-height");
        return true;
    }

    AttrType getAttributeType(const std::string& attributeName) const override {
        if (attributeName == "viewport-height") return kFloatType;
        return kUnknownType;
    }

    bool getAttributeValue(VSTGUI::CView* view,
                           const std::string& attributeName,
                           std::string& stringValue,
                           [[maybe_unused]] const VSTGUI::IUIDescription* desc) const override {
        auto* container = dynamic_cast<ArpLaneContainer*>(view);
        if (!container)
            return false;

        if (attributeName == "viewport-height") {
            stringValue = std::to_string(container->getViewportHeight());
            return true;
        }
        return false;
    }
};

inline ArpLaneContainerCreator gArpLaneContainerCreator;

} // namespace Krate::Plugins
