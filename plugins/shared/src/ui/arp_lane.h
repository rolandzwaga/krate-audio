#pragma once

// ==============================================================================
// IArpLane - Pure Virtual Interface for Arpeggiator Lanes
// ==============================================================================
// Lightweight pure virtual interface for polymorphic lane management.
// All concrete lane classes (ArpLaneEditor, ArpModifierLane, ArpConditionLane)
// implement this interface. ArpLaneContainer holds std::vector<IArpLane*>.
//
// Phase 11c extensions: trail rendering, skip overlay, transform, copy/paste.
//
// Location: plugins/shared/src/ui/arp_lane.h
// ==============================================================================

#include "vstgui/lib/cview.h"

#include <array>
#include <cstdint>
#include <functional>

namespace Krate::Plugins {

// ==============================================================================
// PlayheadTrailState - Per-Lane Trail + Skip Overlay State
// ==============================================================================

struct PlayheadTrailState {
    static constexpr int kTrailLength = 4;  // current + 3 trailing
    // Alpha levels (0-255 scale): index 0 = current step (~63%), 1 = ~39%, 2 = ~22%, 3 = ~10%
    // Authoritative values referenced by spec.md FR-001.
    static constexpr float kTrailAlphas[kTrailLength] = {160.0f, 100.0f, 55.0f, 25.0f};

    int32_t steps[kTrailLength] = {-1, -1, -1, -1};  // step indices, -1 = empty
    bool skipped[32] = {};  // per-step skip overlay flags

    /// Advance the trail: push new step into position 0, shift others down.
    void advance(int32_t newStep) {
        for (int i = kTrailLength - 1; i > 0; --i) {
            steps[i] = steps[i - 1];
        }
        steps[0] = newStep;
    }

    /// Clear all trail positions and skip overlays.
    void clear() {
        for (int i = 0; i < kTrailLength; ++i) steps[i] = -1;
        for (int i = 0; i < 32; ++i) skipped[i] = false;
    }

    /// Mark a step as skipped.
    void markSkipped(int32_t step) {
        if (step >= 0 && step < 32) skipped[step] = true;
    }

    /// Clear skipped flag for steps that the trail has passed.
    void clearPassedSkips() {
        // Clear skip flags for steps that are NOT in the current trail
        for (int i = 0; i < 32; ++i) {
            bool inTrail = false;
            for (int t = 0; t < kTrailLength; ++t) {
                if (steps[t] == i) { inTrail = true; break; }
            }
            if (!inTrail) skipped[i] = false;
        }
    }
};

// ==============================================================================
// ClipboardLaneType & LaneClipboard (Phase 11c - Copy/Paste)
// ==============================================================================

/// Identifies the type of lane data in the clipboard for cross-type normalization.
enum class ClipboardLaneType {
    kVelocity = 0,   // 0.0-1.0 normalized
    kGate = 1,       // 0.0-1.0 normalized (represents 0-200%)
    kPitch = 2,      // 0.0-1.0 normalized (represents -24..+24 semitones)
    kRatchet = 3,    // 0.0-1.0 normalized (represents 1-4 discrete)
    kModifier = 4,   // 0.0-1.0 normalized (represents bitmask 0-15)
    kCondition = 5   // 0.0-1.0 normalized (represents index 0-17)
};

struct LaneClipboard {
    std::array<float, 32> values{};       // normalized 0.0-1.0
    int length = 0;                       // number of valid steps
    ClipboardLaneType sourceType = ClipboardLaneType::kVelocity;
    bool hasData = false;

    void clear() {
        values.fill(0.0f);
        length = 0;
        hasData = false;
    }
};

// ==============================================================================
// IArpLane - Pure Virtual Interface
// ==============================================================================

class IArpLane {
public:
    virtual ~IArpLane() = default;

    /// Get the underlying CView for this lane (for addView/removeView).
    virtual VSTGUI::CView* getView() = 0;

    /// Get the height of this lane when expanded (header + body).
    [[nodiscard]] virtual float getExpandedHeight() const = 0;

    /// Get the height of this lane when collapsed (header only = 16px).
    [[nodiscard]] virtual float getCollapsedHeight() const = 0;

    /// Whether this lane is currently collapsed.
    [[nodiscard]] virtual bool isCollapsed() const = 0;

    /// Set the collapsed state. Fires collapseCallback if state changes.
    virtual void setCollapsed(bool collapsed) = 0;

    /// Set the current playhead step (-1 = no playhead).
    virtual void setPlayheadStep(int32_t step) = 0;

    /// Set the active step count (2-32).
    virtual void setLength(int32_t length) = 0;

    /// Register a callback for collapse/expand state changes.
    /// The container uses this to trigger relayout.
    virtual void setCollapseCallback(std::function<void()> cb) = 0;

    // =========================================================================
    // Phase 11c: Trail, Skip, Transform, Copy/Paste
    // =========================================================================

    /// Update the trail rendering state. Called by the controller's trail timer.
    /// The trail contains the current step + previous 2-3 steps with fade levels.
    virtual void setTrailSteps(const int32_t steps[4], const float alphas[4]) = 0;

    /// Mark a specific step as skipped (shows X overlay).
    /// Called when a skip event IMessage is received from the processor.
    virtual void setSkippedStep(int32_t step) = 0;

    /// Clear all visual overlays (trail positions, skip X markers).
    /// Called when transport stops or editor closes.
    virtual void clearOverlays() = 0;

    /// Get the number of active steps in this lane.
    [[nodiscard]] virtual int32_t getActiveLength() const = 0;

    /// Get the normalized step value at the given index.
    /// For bar lanes: returns 0.0-1.0. For modifier: bitmask/15. For condition: index/17.
    [[nodiscard]] virtual float getNormalizedStepValue(int32_t step) const = 0;

    /// Set the normalized step value at the given index.
    /// Used by paste and transform operations.
    virtual void setNormalizedStepValue(int32_t step, float value) = 0;

    /// Get the lane type identifier for copy/paste normalization.
    [[nodiscard]] virtual int32_t getLaneTypeId() const = 0;

    /// Set transform callback. TransformType: 0=Invert, 1=ShiftLeft, 2=ShiftRight, 3=Randomize.
    using TransformCallback = std::function<void(int transformType)>;
    virtual void setTransformCallback(TransformCallback cb) = 0;

    /// Set copy/paste callbacks.
    using CopyCallback = std::function<void()>;
    using PasteCallback = std::function<void()>;
    virtual void setCopyPasteCallbacks(CopyCallback copy, PasteCallback paste) = 0;

    /// Set whether paste is available (enables/disables paste in context menu).
    virtual void setPasteEnabled(bool enabled) = 0;

    /// Set Euclidean overlay state for linear dot overlay in lane editors.
    /// Bar lanes (ArpLaneEditor) draw dots above the step bars.
    /// Non-bar lanes (ArpModifierLane, ArpConditionLane) ignore the overlay.
    virtual void setEuclideanOverlay(int hits, int steps, int rotation,
                                     bool enabled) = 0;
};

} // namespace Krate::Plugins
