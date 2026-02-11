#pragma once

// ==============================================================================
// ModMatrixGrid - Slot-based Modulation Route List
// ==============================================================================
// A CViewContainer that manages a list of modulation route rows. Each row
// contains: source color dot, source dropdown, arrow "->", destination dropdown,
// BipolarSlider for amount, numeric label, and remove [x] button.
//
// Supports 8 global route slots and 16 voice route slots with tab switching.
// Includes expandable per-route detail controls (Curve, Smooth, Scale, Bypass).
// Supports vertical scrolling when routes exceed visible area (FR-061).
//
// Registered as "ModMatrixGrid" via VSTGUI ViewCreator system.
// Spec: 049-mod-matrix-grid
// ==============================================================================

#include "mod_source_colors.h"
#include "mod_heatmap.h"
#include "color_utils.h"

#include "vstgui/lib/cviewcontainer.h"
#include "vstgui/lib/cdrawcontext.h"
#include "vstgui/lib/cvstguitimer.h"
#include "vstgui/lib/events.h"
#include "vstgui/lib/ccolor.h"
#include "vstgui/lib/cfont.h"
#include "vstgui/lib/cgraphicspath.h"
#include "vstgui/lib/controls/ccontrol.h"
#include "vstgui/uidescription/iviewcreator.h"
#include "vstgui/uidescription/uiviewfactory.h"
#include "vstgui/uidescription/uiviewcreator.h"
#include "vstgui/uidescription/uiattributes.h"
#include "vstgui/uidescription/detail/uiviewcreatorattributes.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <sstream>
#include <iomanip>
#include <string>

namespace Krate::Plugins {

// ==============================================================================
// ModMatrixGrid
// ==============================================================================

class ModMatrixGrid : public VSTGUI::CViewContainer {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr VSTGUI::CCoord kRowHeight = 28.0;
    static constexpr VSTGUI::CCoord kExpandedRowHeight = 56.0;
    static constexpr VSTGUI::CCoord kTabBarHeight = 24.0;
    static constexpr VSTGUI::CCoord kAddButtonHeight = 24.0;
    static constexpr VSTGUI::CCoord kColorDotSize = 8.0;
    static constexpr VSTGUI::CCoord kRowPadding = 4.0;
    static constexpr VSTGUI::CCoord kScrollStep = 20.0;

    // Inline slider layout constants (T036)
    static constexpr VSTGUI::CCoord kSliderWidth = 80.0;
    static constexpr VSTGUI::CCoord kSliderHeight = 8.0;
    static constexpr VSTGUI::CCoord kSliderIndicatorRadius = 4.0;

    // Fine adjustment for inline amount slider (FR-009)
    static constexpr float kDefaultAmountSensitivity = 1.0f / 200.0f;
    static constexpr float kFineAmountScale = 0.1f;

    // Expand animation (T099)
    static constexpr float kExpandAnimSpeed = 0.15f;  // seconds for full expand/collapse
    static constexpr int kAnimTimerIntervalMs = 16;    // ~60fps

    // Detail section hit area layout (T100-T103)
    // X-offsets relative to detail section (starting at x=20)
    static constexpr VSTGUI::CCoord kDetailCurveLeft = 56.0;
    static constexpr VSTGUI::CCoord kDetailCurveRight = 116.0;
    static constexpr VSTGUI::CCoord kDetailSmoothLeft = 162.0;
    static constexpr VSTGUI::CCoord kDetailSmoothRight = 202.0;
    static constexpr VSTGUI::CCoord kDetailScaleLeft = 247.0;
    static constexpr VSTGUI::CCoord kDetailScaleRight = 287.0;
    static constexpr VSTGUI::CCoord kDetailBypassLeft = 292.0;
    static constexpr VSTGUI::CCoord kDetailBypassRight = 360.0;

    // Smooth knob drag sensitivity
    static constexpr float kSmoothDragSensitivity = 0.5f; // ms per pixel

    // =========================================================================
    // Construction
    // =========================================================================

    explicit ModMatrixGrid(const VSTGUI::CRect& size)
        : CViewContainer(size) {
        setBackgroundColor(VSTGUI::CColor(25, 25, 28, 255));
    }

    ModMatrixGrid(const ModMatrixGrid& other)
        : CViewContainer(other)
        , globalRoutes_(other.globalRoutes_)
        , voiceRoutes_(other.voiceRoutes_)
        , activeTab_(other.activeTab_)
        , expanded_(other.expanded_)
        , expandProgress_(other.expandProgress_)
        , selectedSlot_(other.selectedSlot_)
        , scrollOffset_(other.scrollOffset_) {}

    CLASS_METHODS(ModMatrixGrid, CViewContainer)

    // =========================================================================
    // Tab Management
    // =========================================================================

    void setActiveTab(int tabIndex) {
        activeTab_ = std::clamp(tabIndex, 0, 1);
        selectedSlot_ = -1;
        scrollOffset_ = 0.0;
        // Update heatmap mode (T127)
        if (heatmap_) {
            heatmap_->setMode(activeTab_);
            syncHeatmap();
        }
        setDirty();
    }

    [[nodiscard]] int getActiveTab() const { return activeTab_; }

    // =========================================================================
    // Route Data (for programmatic updates from controller)
    // =========================================================================

    void setGlobalRoute(int slot, const ModRoute& route) {
        if (slot >= 0 && slot < kMaxGlobalRoutes) {
            globalRoutes_[static_cast<size_t>(slot)] = route;
            syncHeatmap(); // T126
            setDirty();
        }
    }

    void setVoiceRoute(int slot, const ModRoute& route) {
        if (slot >= 0 && slot < kMaxVoiceRoutes) {
            voiceRoutes_[static_cast<size_t>(slot)] = route;
            syncHeatmap(); // T126
            setDirty();
        }
    }

    [[nodiscard]] ModRoute getGlobalRoute(int slot) const {
        if (slot >= 0 && slot < kMaxGlobalRoutes)
            return globalRoutes_[static_cast<size_t>(slot)];
        return {};
    }

    [[nodiscard]] ModRoute getVoiceRoute(int slot) const {
        if (slot >= 0 && slot < kMaxVoiceRoutes)
            return voiceRoutes_[static_cast<size_t>(slot)];
        return {};
    }

    // =========================================================================
    // Route Management
    // =========================================================================

    /// Add a route to the first available slot in the current tab.
    /// Returns the slot index, or -1 if full.
    int addRoute() {
        if (activeTab_ == 0) {
            for (int i = 0; i < kMaxGlobalRoutes; ++i) {
                if (!globalRoutes_[static_cast<size_t>(i)].active) {
                    globalRoutes_[static_cast<size_t>(i)] = ModRoute{};
                    globalRoutes_[static_cast<size_t>(i)].active = true;
                    syncHeatmap(); // T126
                    setDirty();
                    if (routeChangedCallback_)
                        routeChangedCallback_(activeTab_, i,
                                              globalRoutes_[static_cast<size_t>(i)]);
                    return i;
                }
            }
        } else {
            for (int i = 0; i < kMaxVoiceRoutes; ++i) {
                if (!voiceRoutes_[static_cast<size_t>(i)].active) {
                    voiceRoutes_[static_cast<size_t>(i)] = ModRoute{};
                    voiceRoutes_[static_cast<size_t>(i)].active = true;
                    syncHeatmap(); // T126
                    setDirty();
                    if (routeChangedCallback_)
                        routeChangedCallback_(activeTab_, i,
                                              voiceRoutes_[static_cast<size_t>(i)]);
                    return i;
                }
            }
        }
        return -1; // All slots full
    }

    /// Remove a route at the given slot index in the current tab.
    void removeRoute(int slot) {
        if (activeTab_ == 0 && slot >= 0 && slot < kMaxGlobalRoutes) {
            // Shift remaining routes up
            for (int i = slot; i < kMaxGlobalRoutes - 1; ++i) {
                globalRoutes_[static_cast<size_t>(i)] =
                    globalRoutes_[static_cast<size_t>(i + 1)];
            }
            globalRoutes_[kMaxGlobalRoutes - 1] = ModRoute{}; // Clear last slot
            if (routeRemovedCallback_)
                routeRemovedCallback_(activeTab_, slot);
            syncHeatmap(); // T126
            setDirty();
        } else if (activeTab_ == 1 && slot >= 0 && slot < kMaxVoiceRoutes) {
            for (int i = slot; i < kMaxVoiceRoutes - 1; ++i) {
                voiceRoutes_[static_cast<size_t>(i)] =
                    voiceRoutes_[static_cast<size_t>(i + 1)];
            }
            voiceRoutes_[kMaxVoiceRoutes - 1] = ModRoute{};
            if (routeRemovedCallback_)
                routeRemovedCallback_(activeTab_, slot);
            syncHeatmap(); // T126
            setDirty();
        }
    }

    /// Get the count of active routes in the given tab.
    [[nodiscard]] int getActiveRouteCount(int tab) const {
        int count = 0;
        if (tab == 0) {
            for (const auto& r : globalRoutes_)
                if (r.active) ++count;
        } else {
            for (const auto& r : voiceRoutes_)
                if (r.active) ++count;
        }
        return count;
    }

    // =========================================================================
    // Selection (for cross-component communication, FR-027)
    // =========================================================================

    void selectRoute(int sourceIndex, int destIndex) {
        int maxSlots = (activeTab_ == 0) ? kMaxGlobalRoutes : kMaxVoiceRoutes;
        for (int i = 0; i < maxSlots; ++i) {
            const ModRoute& r = getRouteForTab(activeTab_, i);
            if (r.active &&
                static_cast<int>(r.source) == sourceIndex &&
                static_cast<int>(r.destination) == destIndex) {
                selectedSlot_ = i;
                setDirty();
                return;
            }
        }
    }

    [[nodiscard]] int getSelectedSlot() const { return selectedSlot_; }

    // =========================================================================
    // Expand/Collapse (FR-017 to FR-019)
    // =========================================================================

    void toggleExpanded(int slot) {
        if (slot >= 0 && slot < static_cast<int>(expanded_.size())) {
            expanded_[static_cast<size_t>(slot)] = !expanded_[static_cast<size_t>(slot)];
            // If attached to a frame, animate; otherwise snap instantly
            if (getFrame()) {
                startExpandAnimation();
            } else {
                // Snap to target (no animation in test or detached state)
                expandProgress_[static_cast<size_t>(slot)] =
                    expanded_[static_cast<size_t>(slot)] ? 1.0f : 0.0f;
            }
            setDirty();
        }
    }

    [[nodiscard]] bool isExpanded(int slot) const {
        if (slot >= 0 && slot < static_cast<int>(expanded_.size()))
            return expanded_[static_cast<size_t>(slot)];
        return false;
    }

    /// Get expand animation progress for a slot (0.0 = collapsed, 1.0 = expanded).
    [[nodiscard]] float getExpandProgress(int slot) const {
        if (slot >= 0 && slot < static_cast<int>(expandProgress_.size()))
            return expandProgress_[static_cast<size_t>(slot)];
        return 0.0f;
    }

    // =========================================================================
    // Scroll Support (T034a, FR-061)
    // =========================================================================

    [[nodiscard]] VSTGUI::CCoord getScrollOffset() const { return scrollOffset_; }

    void setScrollOffset(VSTGUI::CCoord offset) {
        scrollOffset_ = clampScrollOffset(offset);
        setDirty();
    }

    // =========================================================================
    // Heatmap Integration (T125-T127)
    // =========================================================================

    /// Wire an external ModHeatmap to receive route data updates.
    void setHeatmap(ModHeatmap* heatmap) {
        heatmap_ = heatmap;
        if (heatmap_) {
            syncHeatmap();
        }
    }

    /// Get the wired heatmap (may be nullptr).
    [[nodiscard]] ModHeatmap* getHeatmap() const { return heatmap_; }

    // =========================================================================
    // Callbacks (Controller Integration)
    // =========================================================================

    using RouteChangedCallback = std::function<void(int tab, int slot, const ModRoute&)>;
    using RouteRemovedCallback = std::function<void(int tab, int slot)>;
    using BeginEditCallback = std::function<void(int32_t paramId)>;
    using EndEditCallback = std::function<void(int32_t paramId)>;
    using ParameterCallback = std::function<void(int32_t paramId, float normalizedValue)>;

    void setRouteChangedCallback(RouteChangedCallback cb) { routeChangedCallback_ = std::move(cb); }
    void setRouteRemovedCallback(RouteRemovedCallback cb) { routeRemovedCallback_ = std::move(cb); }
    void setBeginEditCallback(BeginEditCallback cb) { beginEditCallback_ = std::move(cb); }
    void setEndEditCallback(EndEditCallback cb) { endEditCallback_ = std::move(cb); }
    void setParameterCallback(ParameterCallback cb) { paramCallback_ = std::move(cb); }

    // =========================================================================
    // Drawing
    // =========================================================================

    void drawBackgroundRect(VSTGUI::CDrawContext* context,
                            const VSTGUI::CRect& _updateRect) override {
        CViewContainer::drawBackgroundRect(context, _updateRect);

        context->setDrawMode(VSTGUI::kAntiAliasing | VSTGUI::kNonIntegralMode);

        VSTGUI::CRect vs = getViewSize();
        VSTGUI::CCoord width = vs.getWidth();

        // Draw tab bar (FR-057)
        drawTabBar(context, width);

        // Clip to route list area below tab bar (T034a)
        VSTGUI::CCoord routeAreaTop = kTabBarHeight + 2.0;
        VSTGUI::CCoord routeAreaBottom = vs.getHeight();

        // Calculate total content height for scroll clamping
        VSTGUI::CCoord totalContentHeight = computeContentHeight();

        // Draw route rows (shifted by scroll offset)
        VSTGUI::CCoord y = routeAreaTop - scrollOffset_;
        int maxSlots = (activeTab_ == 0) ? kMaxGlobalRoutes : kMaxVoiceRoutes;
        int activeCount = 0;

        for (int i = 0; i < maxSlots; ++i) {
            const ModRoute& route = getRouteForTab(activeTab_, i);
            if (!route.active)
                break;

            VSTGUI::CCoord rowH = computeRowHeight(i);

            // Only draw rows that are within the visible area
            if (y + rowH > routeAreaTop && y < routeAreaBottom) {
                drawRouteRow(context, route, i, y, width, rowH);
            }
            y += rowH;
            ++activeCount;
        }

        // Draw [+ Add Route] button if not full (FR-003)
        bool canAdd = (activeTab_ == 0)
            ? (activeCount < kMaxGlobalRoutes)
            : (activeCount < kMaxVoiceRoutes);
        if (canAdd && y + kAddButtonHeight > routeAreaTop && y < routeAreaBottom) {
            drawAddButton(context, y, width);
        }

        // Draw scroll indicators if content overflows (FR-061)
        if (totalContentHeight > (routeAreaBottom - routeAreaTop)) {
            drawScrollIndicators(context, width, routeAreaTop, routeAreaBottom,
                                 totalContentHeight);
        }
    }

    // =========================================================================
    // Mouse Interaction
    // =========================================================================

    VSTGUI::CMouseEventResult onMouseDown(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override {
        if (!(buttons & VSTGUI::kLButton))
            return CViewContainer::onMouseDown(where, buttons);

        VSTGUI::CRect vs = getViewSize();
        VSTGUI::CCoord localY = where.y - vs.top;
        VSTGUI::CCoord localX = where.x - vs.left;
        VSTGUI::CCoord width = vs.getWidth();

        // Tab bar click detection
        if (localY < kTabBarHeight) {
            int newTab = (localX < width / 2.0) ? 0 : 1;
            setActiveTab(newTab);
            return VSTGUI::kMouseEventHandled;
        }

        // Route row click detection (account for scroll offset)
        VSTGUI::CCoord y = kTabBarHeight + 2.0 - scrollOffset_;
        int maxSlots = (activeTab_ == 0) ? kMaxGlobalRoutes : kMaxVoiceRoutes;

        for (int i = 0; i < maxSlots; ++i) {
            const ModRoute& route = getRouteForTab(activeTab_, i);
            if (!route.active)
                break;

            VSTGUI::CCoord rowH = computeRowHeight(i);

            if (localY >= y && localY < y + rowH) {
                // Check remove button [x] (right edge, ~20px wide) - only in collapsed row
                if (localX > width - 24.0 && localY < y + kRowHeight) {
                    removeRoute(i);
                    return VSTGUI::kMouseEventHandled;
                }
                // Check disclosure triangle (left edge, ~16px wide)
                if (localX < 16.0 && localY < y + kRowHeight) {
                    toggleExpanded(i);
                    return VSTGUI::kMouseEventHandled;
                }

                // Check inline slider hit area (T036, T041)
                auto sliderRect = computeSliderRect(y, width);
                if (localX >= sliderRect.left && localX <= sliderRect.right &&
                    localY >= y && localY < y + kRowHeight) {
                    // Begin amount slider drag
                    amountDragSlot_ = i;
                    amountDragStartY_ = where.y;
                    amountPreDragValue_ = route.amount;

                    // Fire beginEdit (T042)
                    int32_t amountId = modSlotAmountId(i);
                    if (beginEditCallback_)
                        beginEditCallback_(amountId);

                    selectedSlot_ = i;
                    setDirty();
                    return VSTGUI::kMouseEventHandled;
                }

                // Check source label area (T039)
                VSTGUI::CCoord srcAreaLeft = kRowPadding + 16.0;
                VSTGUI::CCoord srcAreaRight = srcAreaLeft + kColorDotSize + 4.0 + 42.0;
                if (localX >= srcAreaLeft && localX < srcAreaRight &&
                    localY >= y && localY < y + kRowHeight) {
                    fireSourceCycleForSlot(i);
                    selectedSlot_ = i;
                    setDirty();
                    return VSTGUI::kMouseEventHandled;
                }

                // Check destination label area (T040)
                VSTGUI::CCoord dstAreaLeft = srcAreaRight + 22.0; // After arrow
                VSTGUI::CCoord dstAreaRight = dstAreaLeft + 42.0;
                if (localX >= dstAreaLeft && localX < dstAreaRight &&
                    localY >= y && localY < y + kRowHeight) {
                    fireDestCycleForSlot(i);
                    selectedSlot_ = i;
                    setDirty();
                    return VSTGUI::kMouseEventHandled;
                }

                // =========================================================
                // Detail section click handling (T100-T103)
                // =========================================================
                VSTGUI::CCoord detailY = y + kRowHeight;
                if (expanded_[static_cast<size_t>(i)] && localY >= detailY &&
                    localY < y + rowH) {
                    // Curve click-to-cycle (T100)
                    if (localX >= kDetailCurveLeft && localX < kDetailCurveRight) {
                        fireCurveCycleForSlot(i);
                        selectedSlot_ = i;
                        return VSTGUI::kMouseEventHandled;
                    }
                    // Smooth drag start (T101)
                    if (localX >= kDetailSmoothLeft && localX < kDetailSmoothRight) {
                        smoothDragSlot_ = i;
                        smoothDragStartY_ = where.y;
                        smoothPreDragValue_ = route.smoothMs;
                        int32_t smoothId = modSlotSmoothId(i);
                        if (beginEditCallback_)
                            beginEditCallback_(smoothId);
                        selectedSlot_ = i;
                        setDirty();
                        return VSTGUI::kMouseEventHandled;
                    }
                    // Scale click-to-cycle (T102)
                    if (localX >= kDetailScaleLeft && localX < kDetailScaleRight) {
                        fireScaleCycleForSlot(i);
                        selectedSlot_ = i;
                        return VSTGUI::kMouseEventHandled;
                    }
                    // Bypass toggle (T103)
                    if (localX >= kDetailBypassLeft && localX < kDetailBypassRight) {
                        fireBypassToggleForSlot(i);
                        selectedSlot_ = i;
                        return VSTGUI::kMouseEventHandled;
                    }
                }

                // Select this route
                selectedSlot_ = i;
                setDirty();
                return VSTGUI::kMouseEventHandled;
            }
            y += rowH;
        }

        // [+ Add Route] button click
        int activeCount = getActiveRouteCount(activeTab_);
        bool canAdd = (activeTab_ == 0)
            ? (activeCount < kMaxGlobalRoutes)
            : (activeCount < kMaxVoiceRoutes);
        if (canAdd && localY >= y && localY < y + kAddButtonHeight) {
            addRoute();
            return VSTGUI::kMouseEventHandled;
        }

        return CViewContainer::onMouseDown(where, buttons);
    }

    VSTGUI::CMouseEventResult onMouseMoved(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override {
        // Amount slider drag (T041)
        if (amountDragSlot_ >= 0) {
            float sensitivity = kDefaultAmountSensitivity;
            if (buttons.isShiftSet()) {
                sensitivity *= kFineAmountScale; // FR-009
            }

            float delta = static_cast<float>(amountDragStartY_ - where.y) * sensitivity;
            amountDragStartY_ = where.y;

            // Update bipolar amount
            float newBipolar = std::clamp(
                getMutableRouteForTab(activeTab_, amountDragSlot_).amount + delta * 2.0f,
                -1.0f, 1.0f);
            getMutableRouteForTab(activeTab_, amountDragSlot_).amount = newBipolar;

            // Fire parameter callback (T041, T042)
            int32_t amountId = modSlotAmountId(amountDragSlot_);
            float normalized = (newBipolar + 1.0f) / 2.0f; // bipolar to normalized
            if (paramCallback_)
                paramCallback_(amountId, normalized);

            // Fire route changed callback
            if (routeChangedCallback_)
                routeChangedCallback_(activeTab_, amountDragSlot_,
                    getRouteForTab(activeTab_, amountDragSlot_));

            setDirty();
            return VSTGUI::kMouseEventHandled;
        }

        // Smooth knob drag (T101)
        if (smoothDragSlot_ >= 0) {
            float sensitivity = kSmoothDragSensitivity;
            if (buttons.isShiftSet()) {
                sensitivity *= kFineAmountScale; // Fine adjustment
            }

            float delta = static_cast<float>(smoothDragStartY_ - where.y) * sensitivity;
            smoothDragStartY_ = where.y;

            float newSmooth = std::clamp(
                getMutableRouteForTab(activeTab_, smoothDragSlot_).smoothMs + delta,
                0.0f, 100.0f);
            getMutableRouteForTab(activeTab_, smoothDragSlot_).smoothMs = newSmooth;

            // Fire parameter callback (T104)
            int32_t smoothId = modSlotSmoothId(smoothDragSlot_);
            float normalized = newSmooth / 100.0f; // 0-100ms to 0-1
            if (paramCallback_)
                paramCallback_(smoothId, normalized);

            if (routeChangedCallback_)
                routeChangedCallback_(activeTab_, smoothDragSlot_,
                    getRouteForTab(activeTab_, smoothDragSlot_));

            setDirty();
            return VSTGUI::kMouseEventHandled;
        }

        return CViewContainer::onMouseMoved(where, buttons);
    }

    VSTGUI::CMouseEventResult onMouseUp(
        VSTGUI::CPoint& where,
        const VSTGUI::CButtonState& buttons) override {
        if (amountDragSlot_ >= 0) {
            // Fire endEdit (T042)
            int32_t amountId = modSlotAmountId(amountDragSlot_);
            if (endEditCallback_)
                endEditCallback_(amountId);

            amountDragSlot_ = -1;
            return VSTGUI::kMouseEventHandled;
        }

        if (smoothDragSlot_ >= 0) {
            // Fire endEdit for smooth (T104)
            int32_t smoothId = modSlotSmoothId(smoothDragSlot_);
            if (endEditCallback_)
                endEditCallback_(smoothId);

            smoothDragSlot_ = -1;
            return VSTGUI::kMouseEventHandled;
        }

        return CViewContainer::onMouseUp(where, buttons);
    }

    VSTGUI::CMouseEventResult onMouseCancel() override {
        if (amountDragSlot_ >= 0) {
            // Restore pre-drag value
            getMutableRouteForTab(activeTab_, amountDragSlot_).amount = amountPreDragValue_;

            // Fire endEdit
            int32_t amountId = modSlotAmountId(amountDragSlot_);
            if (paramCallback_) {
                float normalized = (amountPreDragValue_ + 1.0f) / 2.0f;
                paramCallback_(amountId, normalized);
            }
            if (endEditCallback_)
                endEditCallback_(amountId);

            amountDragSlot_ = -1;
            setDirty();
        }
        if (smoothDragSlot_ >= 0) {
            // Restore pre-drag smooth value
            getMutableRouteForTab(activeTab_, smoothDragSlot_).smoothMs = smoothPreDragValue_;

            int32_t smoothId = modSlotSmoothId(smoothDragSlot_);
            if (paramCallback_) {
                float normalized = smoothPreDragValue_ / 100.0f;
                paramCallback_(smoothId, normalized);
            }
            if (endEditCallback_)
                endEditCallback_(smoothId);

            smoothDragSlot_ = -1;
            setDirty();
        }
        return VSTGUI::kMouseEventHandled;
    }

    // Mouse wheel for scrolling (T034a, FR-061)
    void onMouseWheelEvent(VSTGUI::MouseWheelEvent& event) override {
        if (event.deltaY != 0.0) {
            VSTGUI::CCoord newOffset = scrollOffset_ - static_cast<VSTGUI::CCoord>(
                event.deltaY * kScrollStep);
            setScrollOffset(newOffset);
            event.consumed = true;
            return;
        }
        CViewContainer::onMouseWheelEvent(event);
    }

private:
    // =========================================================================
    // Route Access Helpers
    // =========================================================================

    [[nodiscard]] const ModRoute& getRouteForTab(int tab, int slot) const {
        if (tab == 0)
            return globalRoutes_[static_cast<size_t>(slot)];
        return voiceRoutes_[static_cast<size_t>(slot)];
    }

    [[nodiscard]] ModRoute& getMutableRouteForTab(int tab, int slot) {
        if (tab == 0)
            return globalRoutes_[static_cast<size_t>(slot)];
        return voiceRoutes_[static_cast<size_t>(slot)];
    }

    // =========================================================================
    // Content Height Computation (T034a)
    // =========================================================================

    [[nodiscard]] VSTGUI::CCoord computeContentHeight() const {
        VSTGUI::CCoord height = 0.0;
        int maxSlots = (activeTab_ == 0) ? kMaxGlobalRoutes : kMaxVoiceRoutes;
        for (int i = 0; i < maxSlots; ++i) {
            const ModRoute& route = getRouteForTab(activeTab_, i);
            if (!route.active)
                break;
            height += computeRowHeight(i);
        }
        // Add space for [+ Add Route] button if not full
        int activeCount = getActiveRouteCount(activeTab_);
        int maxCount = (activeTab_ == 0) ? kMaxGlobalRoutes : kMaxVoiceRoutes;
        if (activeCount < maxCount)
            height += kAddButtonHeight;
        return height;
    }

    [[nodiscard]] VSTGUI::CCoord clampScrollOffset(VSTGUI::CCoord offset) const {
        VSTGUI::CCoord viewableHeight = getViewSize().getHeight() - kTabBarHeight - 2.0;
        VSTGUI::CCoord contentHeight = computeContentHeight();
        VSTGUI::CCoord maxScroll = std::max(0.0, contentHeight - viewableHeight);
        return std::clamp(offset, 0.0, maxScroll);
    }

    // =========================================================================
    // Row Height with Animation (T099)
    // =========================================================================

    /// Get the current animated row height for a slot.
    [[nodiscard]] VSTGUI::CCoord computeRowHeight(int slot) const {
        if (slot < 0 || slot >= static_cast<int>(expandProgress_.size()))
            return kRowHeight;
        float progress = expandProgress_[static_cast<size_t>(slot)];
        return kRowHeight + static_cast<VSTGUI::CCoord>(
            progress * (kExpandedRowHeight - kRowHeight));
    }

    /// Start the expand/collapse animation timer if not already running.
    void startExpandAnimation() {
        if (animTimer_)
            return; // Already animating
        animTimer_ = VSTGUI::makeOwned<VSTGUI::CVSTGUITimer>(
            [this](VSTGUI::CVSTGUITimer*) {
                tickExpandAnimation();
            }, kAnimTimerIntervalMs);
    }

    /// Animation tick: interpolate expandProgress_ toward target.
    void tickExpandAnimation() {
        float step = static_cast<float>(kAnimTimerIntervalMs) /
                     (kExpandAnimSpeed * 1000.0f);
        bool anyAnimating = false;

        for (size_t i = 0; i < expanded_.size(); ++i) {
            float target = expanded_[i] ? 1.0f : 0.0f;
            float& progress = expandProgress_[i];
            if (std::abs(progress - target) > 0.001f) {
                if (progress < target) {
                    progress = std::min(target, progress + step);
                } else {
                    progress = std::max(target, progress - step);
                }
                anyAnimating = true;
            } else {
                progress = target;
            }
        }

        setDirty();

        if (!anyAnimating) {
            animTimer_ = nullptr; // Stop timer
        }
    }

    // =========================================================================
    // Inline Slider Rect Computation (T036)
    // =========================================================================

    [[nodiscard]] VSTGUI::CRect computeSliderRect(VSTGUI::CCoord rowY,
                                                    VSTGUI::CCoord width) const {
        // Slider is placed after destination label, before amount text + remove button
        // Layout: disclosure(16) + dot(12) + src(42) + arrow(22) + dst(42) = 134
        VSTGUI::CCoord sliderLeft = 138.0;
        VSTGUI::CCoord sliderRight = sliderLeft + kSliderWidth;
        // Clamp to available space
        if (sliderRight > width - 70.0)
            sliderRight = width - 70.0;

        VSTGUI::CCoord sliderTop = rowY + (kRowHeight - kSliderHeight) / 2.0;
        return VSTGUI::CRect(sliderLeft, sliderTop,
                              sliderRight, sliderTop + kSliderHeight);
    }

    // =========================================================================
    // Source/Dest Cycling (T039, T040)
    // =========================================================================

    void fireSourceCycleForSlot(int slot) {
        auto& route = getMutableRouteForTab(activeTab_, slot);
        int srcIndex = static_cast<int>(route.source);
        int maxSources = (activeTab_ == 0) ? kNumGlobalSources : kNumVoiceSources;
        int newIndex = (srcIndex + 1) % maxSources;
        route.source = static_cast<ModSource>(newIndex);

        // Fire callbacks (T039, T042)
        int32_t sourceId = modSlotSourceId(slot);
        float normalized = static_cast<float>(newIndex) /
            static_cast<float>(maxSources - 1);
        if (beginEditCallback_) beginEditCallback_(sourceId);
        if (paramCallback_) paramCallback_(sourceId, normalized);
        if (endEditCallback_) endEditCallback_(sourceId);

        if (routeChangedCallback_)
            routeChangedCallback_(activeTab_, slot, route);
    }

    void fireDestCycleForSlot(int slot) {
        auto& route = getMutableRouteForTab(activeTab_, slot);
        int dstIndex = static_cast<int>(route.destination);
        int maxDests = (activeTab_ == 0) ? kNumGlobalDestinations : kNumVoiceDestinations;
        int newIndex = (dstIndex + 1) % maxDests;
        route.destination = static_cast<ModDestination>(newIndex);

        // Fire callbacks (T040, T042)
        int32_t destId = modSlotDestinationId(slot);
        float normalized = static_cast<float>(newIndex) /
            static_cast<float>(maxDests - 1);
        if (beginEditCallback_) beginEditCallback_(destId);
        if (paramCallback_) paramCallback_(destId, normalized);
        if (endEditCallback_) endEditCallback_(destId);

        if (routeChangedCallback_)
            routeChangedCallback_(activeTab_, slot, route);
    }

    // =========================================================================
    // Detail Control Interactions (T100-T104)
    // =========================================================================

    /// Cycle Curve type (0->1->2->3->0) with parameter callback (T100, T104)
    void fireCurveCycleForSlot(int slot) {
        auto& route = getMutableRouteForTab(activeTab_, slot);
        int curveIndex = static_cast<int>(route.curve);
        int newIndex = (curveIndex + 1) % static_cast<int>(kCurveTypeNames.size());
        route.curve = static_cast<uint8_t>(newIndex);

        int32_t curveId = modSlotCurveId(slot);
        float normalized = static_cast<float>(newIndex) /
            static_cast<float>(kCurveTypeNames.size() - 1);
        if (beginEditCallback_) beginEditCallback_(curveId);
        if (paramCallback_) paramCallback_(curveId, normalized);
        if (endEditCallback_) endEditCallback_(curveId);

        if (routeChangedCallback_)
            routeChangedCallback_(activeTab_, slot, route);

        setDirty();
    }

    /// Cycle Scale type (0->1->2->3->4->0) with parameter callback (T102, T104)
    void fireScaleCycleForSlot(int slot) {
        auto& route = getMutableRouteForTab(activeTab_, slot);
        int scaleIndex = static_cast<int>(route.scale);
        int newIndex = (scaleIndex + 1) % static_cast<int>(kScaleNames.size());
        route.scale = static_cast<uint8_t>(newIndex);

        int32_t scaleId = modSlotScaleId(slot);
        float normalized = static_cast<float>(newIndex) /
            static_cast<float>(kScaleNames.size() - 1);
        if (beginEditCallback_) beginEditCallback_(scaleId);
        if (paramCallback_) paramCallback_(scaleId, normalized);
        if (endEditCallback_) endEditCallback_(scaleId);

        if (routeChangedCallback_)
            routeChangedCallback_(activeTab_, slot, route);

        setDirty();
    }

    /// Toggle Bypass with parameter callback (T103, T104)
    void fireBypassToggleForSlot(int slot) {
        auto& route = getMutableRouteForTab(activeTab_, slot);
        route.bypass = !route.bypass;

        int32_t bypassId = modSlotBypassId(slot);
        float normalized = route.bypass ? 1.0f : 0.0f;
        if (beginEditCallback_) beginEditCallback_(bypassId);
        if (paramCallback_) paramCallback_(bypassId, normalized);
        if (endEditCallback_) endEditCallback_(bypassId);

        if (routeChangedCallback_)
            routeChangedCallback_(activeTab_, slot, route);

        setDirty();
    }

    // =========================================================================
    // Heatmap Sync (T126)
    // =========================================================================

    /// Update the wired heatmap with current route data.
    void syncHeatmap() {
        if (!heatmap_) return;

        // Clear all cells first
        for (int s = 0; s < ModHeatmap::kMaxSources; ++s) {
            for (int d = 0; d < ModHeatmap::kMaxDestinations; ++d) {
                heatmap_->setCell(s, d, 0.0f, false);
            }
        }

        // Populate from current tab's routes
        int maxSlots = (activeTab_ == 0) ? kMaxGlobalRoutes : kMaxVoiceRoutes;
        for (int i = 0; i < maxSlots; ++i) {
            const ModRoute& route = getRouteForTab(activeTab_, i);
            if (!route.active) continue;
            int srcIdx = static_cast<int>(route.source);
            int dstIdx = static_cast<int>(route.destination);
            heatmap_->setCell(srcIdx, dstIdx, route.amount, true);
        }
    }

    // =========================================================================
    // Drawing Helpers
    // =========================================================================

    void drawTabBar(VSTGUI::CDrawContext* context, VSTGUI::CCoord width) const {
        auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 11.0);
        context->setFont(font);

        // Tab backgrounds
        VSTGUI::CCoord halfW = width / 2.0;

        // Global tab
        VSTGUI::CRect globalRect(0, 0, halfW, kTabBarHeight);
        context->setFillColor(activeTab_ == 0
            ? VSTGUI::CColor(45, 45, 50, 255)
            : VSTGUI::CColor(30, 30, 33, 255));
        context->drawRect(globalRect, VSTGUI::kDrawFilled);

        // Voice tab
        VSTGUI::CRect voiceRect(halfW, 0, width, kTabBarHeight);
        context->setFillColor(activeTab_ == 1
            ? VSTGUI::CColor(45, 45, 50, 255)
            : VSTGUI::CColor(30, 30, 33, 255));
        context->drawRect(voiceRect, VSTGUI::kDrawFilled);

        // Tab labels with route counts (FR-039)
        int globalCount = getActiveRouteCount(0);
        int voiceCount = getActiveRouteCount(1);

        std::ostringstream globalLabel;
        globalLabel << "Global (" << globalCount << ")";
        context->setFontColor(activeTab_ == 0
            ? VSTGUI::CColor(220, 220, 230, 255)
            : VSTGUI::CColor(140, 140, 150, 255));
        context->drawString(VSTGUI::UTF8String(globalLabel.str()),
                            globalRect, VSTGUI::kCenterText, true);

        std::ostringstream voiceLabel;
        voiceLabel << "Voice (" << voiceCount << ")";
        context->setFontColor(activeTab_ == 1
            ? VSTGUI::CColor(220, 220, 230, 255)
            : VSTGUI::CColor(140, 140, 150, 255));
        context->drawString(VSTGUI::UTF8String(voiceLabel.str()),
                            voiceRect, VSTGUI::kCenterText, true);

        // Separator line
        context->setFrameColor(VSTGUI::CColor(60, 60, 65, 255));
        context->setLineWidth(1.0);
        context->drawLine(VSTGUI::CPoint(0, kTabBarHeight),
                          VSTGUI::CPoint(width, kTabBarHeight));
    }

    void drawRouteRow(VSTGUI::CDrawContext* context,
                      const ModRoute& route, int slot,
                      VSTGUI::CCoord y, VSTGUI::CCoord width,
                      VSTGUI::CCoord rowHeight) const {
        bool isSelected = (slot == selectedSlot_);
        bool isBypassed = route.bypass;

        // Row background
        VSTGUI::CRect rowRect(0, y, width, y + rowHeight);
        VSTGUI::CColor rowBg = isSelected
            ? VSTGUI::CColor(50, 50, 58, 255)
            : VSTGUI::CColor(35, 35, 38, 255);
        if (isBypassed) {
            rowBg = darkenColor(rowBg, 0.7f);
        }
        context->setFillColor(rowBg);
        context->drawRect(rowRect, VSTGUI::kDrawFilled);

        auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 10.0);
        context->setFont(font);

        int srcIndex = static_cast<int>(route.source);
        VSTGUI::CColor srcColor = sourceColorForIndex(srcIndex);
        if (isBypassed)
            srcColor = darkenColor(srcColor, 0.5f);

        VSTGUI::CCoord x = kRowPadding + 16.0; // After disclosure triangle area

        // Source color dot (8px circle, FR-011)
        VSTGUI::CCoord dotY = y + (kRowHeight - kColorDotSize) / 2.0;
        VSTGUI::CRect dotRect(x, dotY, x + kColorDotSize, dotY + kColorDotSize);
        context->setFillColor(srcColor);
        context->drawEllipse(dotRect, VSTGUI::kDrawFilled);
        x += kColorDotSize + 4.0;

        // Source name label (clickable area for T039)
        VSTGUI::CColor textColor = isBypassed
            ? VSTGUI::CColor(100, 100, 105, 255)
            : VSTGUI::CColor(200, 200, 210, 255);
        context->setFontColor(textColor);

        const char* srcName = sourceAbbrForIndex(srcIndex);
        VSTGUI::CRect srcRect(x, y, x + 40.0, y + kRowHeight);
        context->drawString(VSTGUI::UTF8String(srcName), srcRect,
                            VSTGUI::kLeftText, true);
        x += 42.0;

        // Arrow "->"
        VSTGUI::CRect arrowRect(x, y, x + 20.0, y + kRowHeight);
        context->setFontColor(VSTGUI::CColor(80, 80, 85, 255));
        context->drawString(VSTGUI::UTF8String("->"), arrowRect,
                            VSTGUI::kCenterText, true);
        x += 22.0;

        // Destination name label (clickable area for T040)
        int dstIndex = static_cast<int>(route.destination);
        bool isGlobal = (activeTab_ == 0);
        const char* dstName = destinationAbbrForIndex(dstIndex, isGlobal);
        context->setFontColor(textColor);
        VSTGUI::CRect dstRect(x, y, x + 40.0, y + kRowHeight);
        context->drawString(VSTGUI::UTF8String(dstName), dstRect,
                            VSTGUI::kLeftText, true);
        x += 42.0;

        // Inline BipolarSlider (T036)
        auto sliderRect = computeSliderRect(y, width);
        drawInlineSlider(context, route, srcColor, sliderRect, isBypassed);

        // Amount value label (bipolar, 2 decimal places)
        float bipolarAmount = route.amount;
        std::ostringstream amountStr;
        if (bipolarAmount >= 0.0f)
            amountStr << "+";
        amountStr << std::fixed << std::setprecision(2) << bipolarAmount;

        VSTGUI::CCoord amountLabelLeft = sliderRect.right + 4.0;
        VSTGUI::CRect amountRect(amountLabelLeft, y, width - 28.0, y + kRowHeight);
        context->setFont(font);
        context->setFontColor(srcColor);
        context->drawString(VSTGUI::UTF8String(amountStr.str()), amountRect,
                            VSTGUI::kRightText, true);

        // Remove button [x]
        VSTGUI::CRect removeRect(width - 24.0, y, width - 4.0, y + kRowHeight);
        context->setFontColor(VSTGUI::CColor(150, 60, 60, 255));
        context->drawString(VSTGUI::UTF8String("x"), removeRect,
                            VSTGUI::kCenterText, true);

        // Disclosure triangle
        VSTGUI::CCoord triX = 4.0;
        VSTGUI::CCoord triY = y + kRowHeight / 2.0;
        context->setFillColor(VSTGUI::CColor(100, 100, 105, 255));
        if (expanded_[static_cast<size_t>(slot)]) {
            // Down-pointing triangle
            auto triPath = VSTGUI::owned(context->createGraphicsPath());
            if (triPath) {
                triPath->beginSubpath(VSTGUI::CPoint(triX, triY - 3.0));
                triPath->addLine(VSTGUI::CPoint(triX + 8.0, triY - 3.0));
                triPath->addLine(VSTGUI::CPoint(triX + 4.0, triY + 3.0));
                triPath->closeSubpath();
                context->drawGraphicsPath(triPath, VSTGUI::CDrawContext::kPathFilled);
            }
        } else {
            // Right-pointing triangle
            auto triPath = VSTGUI::owned(context->createGraphicsPath());
            if (triPath) {
                triPath->beginSubpath(VSTGUI::CPoint(triX, triY - 4.0));
                triPath->addLine(VSTGUI::CPoint(triX + 6.0, triY));
                triPath->addLine(VSTGUI::CPoint(triX, triY + 4.0));
                triPath->closeSubpath();
                context->drawGraphicsPath(triPath, VSTGUI::CDrawContext::kPathFilled);
            }
        }

        // Expanded detail section (FR-017 to FR-019, T099)
        if (expandProgress_[static_cast<size_t>(slot)] > 0.01f && rowHeight > kRowHeight + 1.0) {
            drawDetailSection(context, route, slot, y + kRowHeight, width);
        }

        // Row separator
        context->setFrameColor(VSTGUI::CColor(45, 45, 50, 255));
        context->setLineWidth(1.0);
        context->drawLine(VSTGUI::CPoint(0, y + rowHeight),
                          VSTGUI::CPoint(width, y + rowHeight));
    }

    // Inline BipolarSlider rendering within a route row (T036)
    void drawInlineSlider(VSTGUI::CDrawContext* context,
                          const ModRoute& route,
                          const VSTGUI::CColor& srcColor,
                          const VSTGUI::CRect& rect,
                          bool isBypassed) const {
        VSTGUI::CColor trackColor(50, 50, 55, 255);
        VSTGUI::CColor fillColor = isBypassed ? darkenColor(srcColor, 0.5f) : srcColor;

        // Draw track background
        context->setFillColor(trackColor);
        context->drawRect(rect, VSTGUI::kDrawFilled);

        // Compute fill from center
        VSTGUI::CCoord centerX = (rect.left + rect.right) / 2.0;
        float normalized = (route.amount + 1.0f) / 2.0f; // bipolar to [0,1]
        VSTGUI::CCoord valueX = rect.left + static_cast<VSTGUI::CCoord>(
            normalized * rect.getWidth());

        VSTGUI::CRect fillRect;
        if (normalized < 0.5f) {
            fillRect = VSTGUI::CRect(valueX, rect.top, centerX, rect.bottom);
        } else {
            fillRect = VSTGUI::CRect(centerX, rect.top, valueX, rect.bottom);
        }
        context->setFillColor(fillColor);
        context->drawRect(fillRect, VSTGUI::kDrawFilled);

        // Center tick
        context->setFrameColor(VSTGUI::CColor(120, 120, 125, 255));
        context->setLineWidth(1.0);
        VSTGUI::CCoord tickExtend = 2.0;
        context->drawLine(VSTGUI::CPoint(centerX, rect.top - tickExtend),
                          VSTGUI::CPoint(centerX, rect.bottom + tickExtend));

        // Value indicator (small circle)
        VSTGUI::CCoord cy = (rect.top + rect.bottom) / 2.0;
        VSTGUI::CRect indicator(
            valueX - kSliderIndicatorRadius, cy - kSliderIndicatorRadius,
            valueX + kSliderIndicatorRadius, cy + kSliderIndicatorRadius);
        context->setFillColor(fillColor);
        context->drawEllipse(indicator, VSTGUI::kDrawFilled);
    }

    void drawDetailSection(VSTGUI::CDrawContext* context,
                           const ModRoute& route,
                           [[maybe_unused]] int slot,
                           VSTGUI::CCoord y, VSTGUI::CCoord width) const {
        auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 9.0);
        context->setFont(font);

        VSTGUI::CColor labelColor(140, 140, 150, 255);
        VSTGUI::CColor valueColor(200, 200, 210, 255);
        VSTGUI::CColor clickableColor(160, 190, 220, 255); // Brighter for clickable items

        VSTGUI::CCoord x = 20.0;

        // Curve label + value (clickable - T100)
        context->setFontColor(labelColor);
        VSTGUI::CRect curveLabel(x, y, x + 35.0, y + kRowHeight);
        context->drawString(VSTGUI::UTF8String("Curve:"), curveLabel,
                            VSTGUI::kLeftText, true);
        x += 36.0;
        context->setFontColor(clickableColor);
        VSTGUI::CRect curveVal(x, y, x + 55.0, y + kRowHeight);
        context->drawString(
            VSTGUI::UTF8String(kCurveTypeNames[std::min(
                static_cast<size_t>(route.curve), kCurveTypeNames.size() - 1)]),
            curveVal, VSTGUI::kLeftText, true);
        // Underline to indicate clickable
        VSTGUI::CCoord underY = y + kRowHeight - 6.0;
        context->setFrameColor(VSTGUI::CColor(160, 190, 220, 80));
        context->setLineWidth(1.0);
        context->drawLine(VSTGUI::CPoint(x, underY),
                          VSTGUI::CPoint(x + 52.0, underY));
        x += 60.0;

        // Smooth label + value (draggable - T101)
        context->setFontColor(labelColor);
        VSTGUI::CRect smoothLabel(x, y, x + 45.0, y + kRowHeight);
        context->drawString(VSTGUI::UTF8String("Smooth:"), smoothLabel,
                            VSTGUI::kLeftText, true);
        x += 46.0;
        bool smoothDragging = (smoothDragSlot_ == slot);
        context->setFontColor(smoothDragging ? brightenColor(valueColor, 1.3f) : valueColor);
        std::ostringstream smoothStr;
        smoothStr << std::fixed << std::setprecision(0) << route.smoothMs << "ms";
        VSTGUI::CRect smoothVal(x, y, x + 40.0, y + kRowHeight);
        context->drawString(VSTGUI::UTF8String(smoothStr.str()), smoothVal,
                            VSTGUI::kLeftText, true);
        x += 45.0;

        // Scale label + value (clickable - T102)
        context->setFontColor(labelColor);
        VSTGUI::CRect scaleLabel(x, y, x + 35.0, y + kRowHeight);
        context->drawString(VSTGUI::UTF8String("Scale:"), scaleLabel,
                            VSTGUI::kLeftText, true);
        x += 36.0;
        context->setFontColor(clickableColor);
        VSTGUI::CRect scaleVal(x, y, x + 35.0, y + kRowHeight);
        context->drawString(
            VSTGUI::UTF8String(kScaleNames[std::min(
                static_cast<size_t>(route.scale), kScaleNames.size() - 1)]),
            scaleVal, VSTGUI::kLeftText, true);
        // Underline for clickable
        context->setFrameColor(VSTGUI::CColor(160, 190, 220, 80));
        context->drawLine(VSTGUI::CPoint(x, underY),
                          VSTGUI::CPoint(x + 32.0, underY));
        x += 40.0;

        // Bypass button (toggle - T103)
        VSTGUI::CRect bypassBtn(x, y + 4.0, x + 60.0, y + kRowHeight - 4.0);
        if (route.bypass) {
            // Active bypass - red background
            context->setFillColor(VSTGUI::CColor(160, 50, 50, 255));
            context->drawRect(bypassBtn, VSTGUI::kDrawFilled);
            context->setFontColor(VSTGUI::CColor(255, 200, 200, 255));
        } else {
            // Inactive bypass - subtle outline
            context->setFrameColor(VSTGUI::CColor(80, 80, 85, 255));
            context->setLineWidth(1.0);
            context->drawRect(bypassBtn, VSTGUI::kDrawStroked);
            context->setFontColor(VSTGUI::CColor(120, 120, 130, 255));
        }
        context->drawString(VSTGUI::UTF8String("Bypass"), bypassBtn,
                            VSTGUI::kCenterText, true);
    }

    void drawAddButton(VSTGUI::CDrawContext* context,
                       VSTGUI::CCoord y, VSTGUI::CCoord width) const {
        VSTGUI::CRect btnRect(0, y, width, y + kAddButtonHeight);
        context->setFillColor(VSTGUI::CColor(35, 40, 38, 255));
        context->drawRect(btnRect, VSTGUI::kDrawFilled);

        auto font = VSTGUI::makeOwned<VSTGUI::CFontDesc>("Arial", 10.0);
        context->setFont(font);
        context->setFontColor(VSTGUI::CColor(120, 180, 120, 255));
        context->drawString(VSTGUI::UTF8String("[+ Add Route]"), btnRect,
                            VSTGUI::kCenterText, true);
    }

    // Scroll indicators (T034a, FR-061)
    void drawScrollIndicators(VSTGUI::CDrawContext* context,
                              VSTGUI::CCoord width,
                              VSTGUI::CCoord areaTop,
                              VSTGUI::CCoord areaBottom,
                              VSTGUI::CCoord contentHeight) const {
        VSTGUI::CCoord viewableHeight = areaBottom - areaTop;
        if (contentHeight <= viewableHeight) return;

        // Scroll bar track
        VSTGUI::CCoord barWidth = 4.0;
        VSTGUI::CCoord barLeft = width - barWidth - 1.0;

        // Compute thumb position and size
        float visibleFraction = static_cast<float>(viewableHeight / contentHeight);
        float thumbHeight = std::max(16.0f, visibleFraction * static_cast<float>(viewableHeight));
        float scrollFraction = static_cast<float>(
            scrollOffset_ / (contentHeight - viewableHeight));
        float thumbTop = static_cast<float>(areaTop) +
            scrollFraction * (static_cast<float>(viewableHeight) - thumbHeight);

        // Draw thumb
        VSTGUI::CRect thumbRect(barLeft,
            static_cast<VSTGUI::CCoord>(thumbTop),
            barLeft + barWidth,
            static_cast<VSTGUI::CCoord>(thumbTop + thumbHeight));
        context->setFillColor(VSTGUI::CColor(80, 80, 85, 180));
        context->drawRect(thumbRect, VSTGUI::kDrawFilled);
    }

    // =========================================================================
    // State
    // =========================================================================

    std::array<ModRoute, kMaxGlobalRoutes> globalRoutes_{};
    std::array<ModRoute, kMaxVoiceRoutes> voiceRoutes_{};
    int activeTab_ = 0;                                           // 0=Global, 1=Voice
    std::array<bool, kMaxVoiceRoutes> expanded_{};                // Per-slot expand state
    std::array<float, kMaxVoiceRoutes> expandProgress_{};         // Per-slot expand animation (T099)
    int selectedSlot_ = -1;

    // Scroll state (T034a)
    VSTGUI::CCoord scrollOffset_ = 0.0;

    // Amount drag state (T041)
    int amountDragSlot_ = -1;
    VSTGUI::CCoord amountDragStartY_ = 0.0;
    float amountPreDragValue_ = 0.0f;

    // Smooth drag state (T101)
    int smoothDragSlot_ = -1;
    VSTGUI::CCoord smoothDragStartY_ = 0.0;
    float smoothPreDragValue_ = 0.0f;

    // Expand animation timer (T099)
    VSTGUI::SharedPointer<VSTGUI::CVSTGUITimer> animTimer_;

    // Heatmap integration (T125)
    ModHeatmap* heatmap_ = nullptr;

    // Callbacks
    RouteChangedCallback routeChangedCallback_;
    RouteRemovedCallback routeRemovedCallback_;
    BeginEditCallback beginEditCallback_;
    EndEditCallback endEditCallback_;
    ParameterCallback paramCallback_;
};

// =============================================================================
// ViewCreator Registration
// =============================================================================

struct ModMatrixGridCreator : VSTGUI::ViewCreatorAdapter {
    ModMatrixGridCreator() {
        VSTGUI::UIViewFactory::registerViewCreator(*this);
    }

    VSTGUI::IdStringPtr getViewName() const override {
        return "ModMatrixGrid";
    }

    VSTGUI::IdStringPtr getBaseViewName() const override {
        return VSTGUI::UIViewCreator::kCViewContainer;
    }

    VSTGUI::UTF8StringPtr getDisplayName() const override {
        return "Mod Matrix Grid";
    }

    VSTGUI::CView* create(
        const VSTGUI::UIAttributes& /*attributes*/,
        const VSTGUI::IUIDescription* /*description*/) const override {
        return new ModMatrixGrid(VSTGUI::CRect(0, 0, 430, 250));
    }

    bool apply(VSTGUI::CView* /*view*/,
               const VSTGUI::UIAttributes& /*attributes*/,
               const VSTGUI::IUIDescription* /*description*/) const override {
        return true;
    }

    bool getAttributeNames(
        VSTGUI::IViewCreator::StringList& /*attributeNames*/) const override {
        return true;
    }

    AttrType getAttributeType(
        const std::string& /*attributeName*/) const override {
        return kUnknownType;
    }

    bool getAttributeValue(VSTGUI::CView* /*view*/,
                           const std::string& /*attributeName*/,
                           std::string& /*stringValue*/,
                           const VSTGUI::IUIDescription* /*desc*/) const override {
        return false;
    }
};

inline ModMatrixGridCreator gModMatrixGridCreator;

} // namespace Krate::Plugins
