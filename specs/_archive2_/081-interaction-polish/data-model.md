# Data Model: Arpeggiator Interaction Polish (081)

**Date**: 2026-02-25
**Spec**: `specs/081-interaction-polish/spec.md`

## Entities

### PlayheadTrailState (Helper Struct)

Stored per-lane (either as a member of each lane class or via a shared helper).

```cpp
namespace Krate::Plugins {

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

} // namespace Krate::Plugins
```

### LaneClipboard (Copy/Paste Storage)

One instance per Controller. The clipboard is in-memory and editor-scoped: it is cleared when the editor closes. It is the authoritative definition; `contracts/copy-paste.md` references this struct.

```cpp
namespace Krate::Plugins {

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

} // namespace Krate::Plugins
```

### EuclideanDotDisplay (CView Subclass)

New standalone CView for circular Euclidean pattern visualization. See `contracts/euclidean-dot-display.md` for the complete class definition, draw algorithm, and ViewCreator attribute table. Default values: hits=0, steps=8, rotation=0, dotRadius=3.0f, accentColor={208, 132, 92, 255}.

### IArpLane Interface Extensions

See `contracts/iarp-lane-extensions.md` for the complete and authoritative IArpLane extension API, including all new pure virtual methods (`setTrailSteps()`, `setSkippedStep()`, `clearOverlays()`, `getActiveLength()`, `getNormalizedStepValue()`, `setNormalizedStepValue()`, `getLaneTypeId()`, `setTransformCallback()`, `setCopyPasteCallbacks()`, `setPasteEnabled()`).

Note: the trail method signature is `setTrailSteps(const int32_t steps[4], const float alphas[4])`, passing unpacked arrays rather than a `PlayheadTrailState` struct reference, to keep the lane interface independent of the struct definition location.

### ArpLaneHeader Extensions

New members and methods for transform buttons:

```cpp
class ArpLaneHeader {
public:
    // ... existing members ...

    // Transform button constants
    static constexpr float kButtonSize = 12.0f;
    static constexpr float kButtonGap = 2.0f;
    static constexpr float kButtonsRightMargin = 4.0f;
    // 4 buttons: (12+2)*4 - 2 + 4 = 58px from right edge

    // Transform callback types
    enum TransformType { kInvert = 0, kShiftLeft = 1, kShiftRight = 2, kRandomize = 3 };

    using TransformCallback = std::function<void(TransformType)>;
    void setTransformCallback(TransformCallback cb);

    // Right-click context menu
    using CopyCallback = std::function<void()>;
    using PasteCallback = std::function<void()>;
    void setCopyPasteCallbacks(CopyCallback copy, PasteCallback paste);
    void setPasteEnabled(bool enabled);

    // Rendering: draws 4 small icon buttons in header
    void drawTransformButtons(CDrawContext* context, const CRect& headerRect);

    // Interaction: returns true if click was on a transform button
    bool handleTransformClick(const CPoint& where, const CRect& headerRect);

    // Right-click: opens Copy/Paste context menu
    bool handleRightClick(const CPoint& where, const CRect& headerRect, CFrame* frame);
};
```

## Relationships

```
Controller
  |-- owns LaneClipboard (1 instance)
  |-- owns CVSTGUITimer for trail polling (1 instance, ~30fps)
  |-- references ArpLaneContainer
  |       |-- contains 6 IArpLane* instances
  |               |-- each has PlayheadTrailState (composition)
  |               |-- each has ArpLaneHeader (composition)
  |                       |-- draws 4 transform buttons
  |                       |-- handles right-click for copy/paste
  |-- references EuclideanDotDisplay (1 instance in bottom bar)
  |-- references bottom bar controls (knobs, buttons)

Processor
  |-- owns 6 pre-allocated IMessage instances for skip events
  |-- owns ArpeggiatorCore
  |       |-- generates ArpEvent with kSkip type
  |-- sends "ArpSkipEvent" IMessage to Controller
```

## State Transitions

### Trail State Machine (per lane)
```
[Idle] --(transport starts)--> [Tracking]
[Tracking] --(timer tick)--> [Tracking] (advance trail, invalidate)
[Tracking] --(transport stops)--> [Idle] (clear all overlays)
[Tracking] --(editor closes)--> [Idle] (timer destroyed)
```

### Skip Overlay State Machine (per step)
```
[Clear] --(skip event received)--> [Marked]
[Marked] --(trail passes step)--> [Clear]
[Marked] --(transport stops)--> [Clear]
```

### Clipboard State Machine
```
[Empty] --(Copy clicked)--> [HasData]
[HasData] --(Copy clicked)--> [HasData] (overwrite)
[HasData] --(Paste clicked)--> [HasData] (data preserved)
[HasData] --(editor closes)--> [Empty]
```

## Validation Rules

- Trail length is always 4 (fixed). No dynamic allocation.
- Skip step index must be 0-31. Out-of-range values are ignored.
- Skip lane index must be 0-5. Out-of-range values are ignored.
- Clipboard length must be 1-32. Paste with length 0 is a no-op.
- Transform operations clamp values to valid ranges per lane type.
- EuclideanDotDisplay: hits clamped to [0, steps], steps clamped to [2, 32], rotation clamped to [0, steps-1].
