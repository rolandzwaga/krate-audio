# IArpLane Interface Extensions Contract

## Existing Interface (to preserve)

```cpp
class IArpLane {
public:
    virtual ~IArpLane() = default;
    virtual VSTGUI::CView* getView() = 0;
    virtual float getExpandedHeight() const = 0;
    virtual float getCollapsedHeight() const = 0;
    virtual bool isCollapsed() const = 0;
    virtual void setCollapsed(bool collapsed) = 0;
    virtual void setPlayheadStep(int32_t step) = 0;
    virtual void setLength(int32_t length) = 0;
    virtual void setCollapseCallback(std::function<void()> cb) = 0;
};
```

## New Methods (Phase 11c additions)

```cpp
class IArpLane {
public:
    // ... all existing methods preserved ...

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
    virtual int32_t getActiveLength() const = 0;

    /// Get the normalized step value at the given index.
    /// For bar lanes: returns 0.0-1.0. For modifier: bitmask/15. For condition: index/17.
    virtual float getNormalizedStepValue(int32_t step) const = 0;

    /// Set the normalized step value at the given index.
    /// Used by paste and transform operations.
    virtual void setNormalizedStepValue(int32_t step, float value) = 0;

    /// Get the lane type identifier for copy/paste normalization.
    virtual int32_t getLaneTypeId() const = 0;

    /// Set transform callback. TransformType: 0=Invert, 1=ShiftLeft, 2=ShiftRight, 3=Randomize.
    using TransformCallback = std::function<void(int transformType)>;
    virtual void setTransformCallback(TransformCallback cb) = 0;

    /// Set copy/paste callbacks.
    using CopyCallback = std::function<void()>;
    using PasteCallback = std::function<void()>;
    virtual void setCopyPasteCallbacks(CopyCallback copy, PasteCallback paste) = 0;

    /// Set whether paste is available (enables/disables paste in context menu).
    virtual void setPasteEnabled(bool enabled) = 0;
};
```

## Backward Compatibility

All existing IArpLane implementors (ArpLaneEditor, ArpModifierLane, ArpConditionLane) must implement the new pure virtual methods. Since all three are in our codebase and we modify them in this spec, this is safe.
