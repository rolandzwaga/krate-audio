# Data Model: Arpeggiator Layout Restructure & Lane Framework

**Feature**: 079-layout-framework
**Date**: 2026-02-24

## Entity: ArpLaneType (enum class)

**Location**: `plugins/shared/src/ui/arp_lane_editor.h`
**Namespace**: `Krate::Plugins`

| Value | Name | Display Min | Display Max | Top Label | Bottom Label |
|-------|------|-------------|-------------|-----------|--------------|
| 0 | kVelocity | 0.0 | 1.0 | "1.0" | "0.0" |
| 1 | kGate | 0.0 | 2.0 | "200%" | "0%" |
| 2 | kPitch | -24.0 | 24.0 | "+24" | "-24" |
| 3 | kRatchet | 1.0 | 4.0 | "4" | "1" |

**Validation**: Phase 11a implements only kVelocity and kGate. kPitch and kRatchet are placeholder enum values for Phase 11b forward compatibility.

---

## Entity: ArpLaneEditor (class)

**Location**: `plugins/shared/src/ui/arp_lane_editor.h`
**Namespace**: `Krate::Plugins`
**Base class**: `StepPatternEditor`
**ViewCreator name**: `"ArpLaneEditor"`

### Fields (additional to StepPatternEditor)

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| laneType_ | ArpLaneType | kVelocity | Determines value range and labels |
| laneName_ | std::string | "" | Header label text (e.g., "VEL", "GATE") |
| accentColor_ | CColor | {208,132,92,255} | Primary accent color |
| normalColor_ | CColor | derived | Mid-level bar color (darken 0.6x) |
| ghostColor_ | CColor | derived | Low-level bar color (darken 0.35x) |
| displayMin_ | float | 0.0f | Display range minimum |
| displayMax_ | float | 1.0f | Display range maximum |
| topLabel_ | std::string | "1.0" | Grid label at top of bar area |
| bottomLabel_ | std::string | "0.0" | Grid label at bottom of bar area |
| lengthParamId_ | uint32_t | 0 | VST parameter ID for lane length |
| playheadParamId_ | uint32_t | 0 | VST parameter ID for playhead position |
| isCollapsed_ | bool | false | Collapse/expand state (transient) |
| collapseCallback_ | std::function<void()> | nullptr | Called when collapse state changes |
| lengthParamCallback_ | std::function<void(uint32_t, float)> | nullptr | Called when user selects a new step count from the length dropdown; arguments are (paramId, normalizedValue) |

### Constants

| Constant | Value | Description |
|----------|-------|-------------|
| kHeaderHeight | 16.0f | Height of the lane header bar |
| kMiniPreviewHeight | 12.0f | Usable height for miniature bar preview (= kHeaderHeight - kMiniPreviewPaddingTop - kMiniPreviewPaddingBottom) |
| kMiniPreviewPaddingTop | 2.0f | Top padding inside header before preview bars begin |
| kMiniPreviewPaddingBottom | 2.0f | Bottom padding inside header below preview bars |
| kCollapseTriangleSize | 8.0f | Size of the collapse/expand triangle icon (`>` when collapsed, `v` when expanded) |

### State Transitions

| State | Trigger | Result |
|-------|---------|--------|
| Expanded | Click collapse toggle | isCollapsed_ = true, body hidden, miniature preview shown, collapseCallback_ called |
| Collapsed | Click collapse toggle | isCollapsed_ = false, body shown, collapseCallback_ called |
| Any | Host automates step value | setStepLevel() updates bar, setDirty() |
| Any | Host automates length | setNumSteps() updates bar count |
| Playing | Controller timer polls playhead param | Controller calls setPlaybackStep(stepIndex); ArpLaneEditor updates indicator |
| Playing -> Stopped | Transport stops | Controller calls setPlaybackStep(-1) to clear indicator |

### Key Methods

```cpp
// Configuration (called during programmatic construction)
void setLaneType(ArpLaneType type);
void setLaneName(const std::string& name);
void setAccentColor(const CColor& color);  // Also derives normal/ghost
void setDisplayRange(float min, float max, const std::string& topLabel, const std::string& bottomLabel);
void setLengthParamId(uint32_t paramId);
void setLengthParamCallback(std::function<void(uint32_t, float)> cb);  // Invoked when user changes length dropdown
void setPlayheadParamId(uint32_t paramId);
void setCollapseCallback(std::function<void()> cb);

// State
void setCollapsed(bool collapsed);
[[nodiscard]] bool isCollapsed() const;
[[nodiscard]] float getExpandedHeight() const;  // kHeaderHeight + body height
[[nodiscard]] float getCollapsedHeight() const;  // kHeaderHeight only

// Override
void draw(CDrawContext* context) override;
CMouseEventResult onMouseDown(CPoint& where, const CButtonState& buttons) override;
```

---

## Entity: ArpLaneContainer (class)

**Location**: `plugins/shared/src/ui/arp_lane_container.h`
**Namespace**: `Krate::Plugins`
**Base class**: `CViewContainer`
**ViewCreator name**: `"ArpLaneContainer"`

### Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| viewportHeight_ | float | 390.0f | Visible scroll area height (from XML) |
| scrollOffset_ | float | 0.0f | Current vertical scroll position (pixels) |
| lanes_ | std::vector<ArpLaneEditor*> | empty | Ordered list of child lanes |
| totalContentHeight_ | float | 0.0f | Sum of all lane heights |

### Key Methods

```cpp
// Lane management (called from controller initialize)
void addLane(ArpLaneEditor* lane);
void removeLane(ArpLaneEditor* lane);

// Layout
void recalculateLayout();  // Reposition lanes, update totalContentHeight_
void setViewportHeight(float height);

// Scroll
void scrollToOffset(float offset);
[[nodiscard]] float getMaxScrollOffset() const;

// Override
void draw(CDrawContext* context) override;
void drawBackgroundRect(CDrawContext* context, const CRect& rect) override;
bool onWheel(const CPoint& where, const CMouseWheelAxis& axis,
             const float& distance, const CButtonState& buttons) override;
```

### Layout Algorithm

```
recalculateLayout():
  y = 0
  for each lane in lanes_:
    if lane.isCollapsed():
      laneHeight = lane.getCollapsedHeight()  // 16px
    else:
      laneHeight = lane.getExpandedHeight()   // 16px header + body
    lane.setViewSize(CRect(0, y, containerWidth, y + laneHeight))
    y += laneHeight
  totalContentHeight_ = y
  scrollOffset_ = clamp(scrollOffset_, 0, max(0, totalContentHeight_ - viewportHeight_))
```

---

## Entity: New Parameter IDs

**Location**: `plugins/ruinae/src/plugin_ids.h`

| ID | Name | Type | Range | Persisted | Automatable | Description |
|----|------|------|-------|-----------|-------------|-------------|
| 3294 | kArpVelocityPlayheadId | Hidden | 0.0-1.0 | No | No (kIsHidden) | Velocity lane playhead position |
| 3295 | kArpGatePlayheadId | Hidden | 0.0-1.0 | No | No (kIsHidden) | Gate lane playhead position |

**Encoding**: `stepIndex / kMaxSteps` (e.g., step 5 of 16 = 5/32 = 0.15625). Uses `kMaxSteps=32` as denominator for consistent encoding regardless of actual lane length. The controller decodes: `stepIndex = std::lround(normalized * 32)`. The controller (not ArpLaneEditor) reads this parameter on the 30fps timer and calls `setPlaybackStep(stepIndex)` on the ArpLaneEditor.

**Sentinel value**: When the arpeggiator is not playing, the processor writes `1.0f`. Decoded: `std::lround(1.0f * 32) = 32`. Since valid step indices are always 0 to (laneLength - 1), and the maximum lane length is `kMaxSteps = 32` (indices 0-31), the decoded value 32 is always out of range. The controller detects `stepIndex >= kMaxSteps` and calls `setPlaybackStep(-1)` to clear the highlight. This sentinel is safe at all lane lengths including the maximum 32-step lane.

**Note**: kNumParameters stays at 3300 since these IDs (3294-3295) are within the existing 3000-3299 arp range. They must be registered in `registerArpParams()` and excluded from state save/load.

---

## Entity: Named Colors (editor.uidesc)

**Location**: `plugins/ruinae/resources/editor.uidesc` (colors section)

| Name | RGBA | Hex | Usage |
|------|------|-----|-------|
| arp-lane-velocity | 208, 132, 92, 255 | #D0845Cff | Velocity lane accent |
| arp-lane-velocity-normal | 125, 79, 55, 255 | #7D4F37ff | Velocity lane mid-level |
| arp-lane-velocity-ghost | 73, 46, 32, 255 | #492E20ff | Velocity lane low-level |
| arp-lane-gate | 200, 164, 100, 255 | #C8A464ff | Gate lane accent |
| arp-lane-gate-normal | 120, 98, 60, 255 | #78623Cff | Gate lane mid-level |
| arp-lane-gate-ghost | 70, 57, 35, 255 | #463923ff | Gate lane low-level |

---

## Relationship Diagram

```
Controller (Ruinae)
  |
  |-- owns --> ArpLaneContainer (1)
  |               |
  |               |-- contains --> ArpLaneEditor [Velocity] (1)
  |               |                    |-- inherits --> StepPatternEditor
  |               |                    |-- reads --> kArpVelocityLaneStep0..31Id (params)
  |               |                    |-- reads --> kArpVelocityLaneLengthId (param)
  |               |                    |-- reads --> kArpVelocityPlayheadId (param)
  |               |
  |               |-- contains --> ArpLaneEditor [Gate] (1)
  |                                    |-- inherits --> StepPatternEditor
  |                                    |-- reads --> kArpGateLaneStep0..31Id (params)
  |                                    |-- reads --> kArpGateLaneLengthId (param)
  |                                    |-- reads --> kArpGatePlayheadId (param)
  |
  |-- polls (30fps timer) --> kArpVelocityPlayheadId, kArpGatePlayheadId
  |-- pushes --> ArpLaneEditor.setPlaybackStep()
```
