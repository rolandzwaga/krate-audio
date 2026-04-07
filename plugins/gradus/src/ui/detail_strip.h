#pragma once

// ==============================================================================
// DetailStrip — Tab Bar + Per-Lane Controls + Embedded Linear Editor
// ==============================================================================
// Shows a tab bar at top, per-lane controls (steps, speed), and embeds one
// of the 8 existing lane editors at a time for precision linear editing.
// Selected tab highlights the corresponding ring in the ring renderer.
// ==============================================================================

#include "lane_tab_bar.h"
#include "ui/arp_lane.h"

#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/cdrawcontext.h"

#include <array>
#include <functional>

namespace Gradus {

class DetailStrip : public VSTGUI::CViewContainer {
public:
    explicit DetailStrip(const VSTGUI::CRect& size)
        : CViewContainer(size)
    {
        setBackgroundColor({0x1A, 0x1A, 0x1E, 0xFF});

        // Create tab bar at top
        float w = static_cast<float>(size.getWidth());
        tabBar_ = new LaneTabBar(VSTGUI::CRect(0, 0, w, kTabBarHeight));
        tabBar_->setTabSelectedCallback([this](int laneIndex) {
            selectLane(laneIndex);
        });
        addView(tabBar_);
    }

    // =========================================================================
    // Lane Management
    // =========================================================================

    /// Set a lane view for a given index. The view is added as a child but
    /// hidden until its tab is selected.
    ///
    /// Lane views are positioned below the header (tab bar + per-lane control
    /// row) AND below the reserved pin-row slot (kPinRowHeight px). The pin
    /// row slot is empty for non-Pitch lanes; the PinFlagStrip widget is
    /// placed in that slot by the Controller and shown only on the Pitch tab.
    void setLane(int index, Krate::Plugins::IArpLane* lane)
    {
        if (index < 0 || index >= kLaneCount || !lane) return;

        lanes_[index] = lane;
        auto* laneView = lane->getView();
        if (!laneView) return;

        float w = static_cast<float>(getViewSize().getWidth());
        const float laneTop = kHeaderHeight + kPinRowHeight;
        float h = static_cast<float>(getViewSize().getHeight()) - laneTop;
        laneView->setViewSize(VSTGUI::CRect(0, laneTop, w, laneTop + h));
        laneView->setMouseableArea(laneView->getViewSize());
        laneView->setVisible(index == selectedLane_);

        addView(laneView);
    }

    // Vertical slot reserved above the lane editors for the v1.6
    // PinFlagStrip widget (only shown when Pitch lane is selected).
    // Exposed so the Controller can position its child strip at
    // [0, kHeaderHeight, w, kHeaderHeight + kPinRowHeight].
    static constexpr float pinRowTop()    { return kHeaderHeight; }
    static constexpr float pinRowHeight() { return kPinRowHeight; }

    /// Select a lane by index, showing its editor and hiding others.
    void selectLane(int index)
    {
        if (index < 0 || index >= kLaneCount || index == selectedLane_) return;

        // Hide current
        if (selectedLane_ >= 0 && lanes_[selectedLane_]) {
            auto* view = lanes_[selectedLane_]->getView();
            if (view) view->setVisible(false);
        }

        selectedLane_ = index;

        // Show new
        if (lanes_[index]) {
            auto* view = lanes_[index]->getView();
            if (view) {
                view->setVisible(true);
                view->invalidRect(view->getViewSize());
            }
        }

        // Sync tab bar
        if (tabBar_) tabBar_->setSelectedTab(index);

        // Notify ring renderer
        if (laneSelectedCallback_) laneSelectedCallback_(index);

        invalid();
    }

    [[nodiscard]] int selectedLane() const { return selectedLane_; }

    LaneTabBar* getTabBar() const { return tabBar_; }

    // =========================================================================
    // Callbacks
    // =========================================================================

    using LaneSelectedCallback = std::function<void(int laneIndex)>;
    void setLaneSelectedCallback(LaneSelectedCallback cb)
    {
        laneSelectedCallback_ = std::move(cb);
    }

private:
    static constexpr float kTabBarHeight = 24.0f;
    static constexpr float kPerLaneRowHeight = 32.0f;
    static constexpr float kHeaderHeight = kTabBarHeight + kPerLaneRowHeight;
    // v1.6: Reserved slot above lane editors for the PinFlagStrip widget.
    // Non-Pitch lanes leave this empty; the strip is only visible on Pitch.
    static constexpr float kPinRowHeight = 14.0f;

    LaneTabBar* tabBar_ = nullptr;  // Owned by CViewContainer
    std::array<Krate::Plugins::IArpLane*, kLaneCount> lanes_{};
    int selectedLane_ = 0;

    LaneSelectedCallback laneSelectedCallback_;

    CLASS_METHODS(DetailStrip, CViewContainer)
};

} // namespace Gradus
