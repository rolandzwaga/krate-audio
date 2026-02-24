# Data Model: Specialized Lane Types (080)

**Date**: 2026-02-24 | **Branch**: `080-specialized-lane-types`

---

## Entity Definitions

### IArpLane (Interface)

**Location**: `plugins/shared/src/ui/arp_lane.h`
**Type**: Pure virtual interface (C++ abstract class)

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

**Relationships**:
- Implemented by: `ArpLaneEditor`, `ArpModifierLane`, `ArpConditionLane`
- Held by: `ArpLaneContainer` as `std::vector<IArpLane*>`

---

### ArpLaneHeader (Helper Struct)

**Location**: `plugins/shared/src/ui/arp_lane_header.h`
**Type**: Non-CView helper struct, owned by composition

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `laneName_` | `std::string` | `""` | Display name (e.g., "VEL", "PITCH") |
| `accentColor_` | `VSTGUI::CColor` | copper | Lane accent color |
| `isCollapsed_` | `bool` | `false` | Collapsed state |
| `numSteps_` | `int` | `16` | Current step count (for length display) |
| `lengthParamId_` | `uint32_t` | `0` | Parameter ID for length |
| `collapseCallback_` | `std::function<void()>` | null | Called on collapse toggle |
| `lengthParamCallback_` | `std::function<void(uint32_t, float)>` | null | Called on length change |

**Methods**:
- `draw(CDrawContext*, CRect headerRect)` - Renders collapse triangle, name label, length dropdown
- `handleMouseDown(CPoint, CButtonState) -> bool` - Returns true if click was in header area
- `getHeight() -> float` - Returns `kHeaderHeight` (16.0f)
- `setCollapsed(bool)` / `isCollapsed()` - State accessors
- `setNumSteps(int)` - Update displayed step count
- `openLengthDropdown(CPoint, CFrame*)` - Show COptionMenu popup

---

### ArpLaneEditor (Extended)

**Location**: `plugins/shared/src/ui/arp_lane_editor.h`
**Type**: CControl subclass (extends StepPatternEditor), implements IArpLane

**New/Modified Fields**:

| Field | Type | Default | Change |
|-------|------|---------|--------|
| `header_` | `ArpLaneHeader` | | NEW: replaces inline header logic |

**New/Modified Methods**:

| Method | Signature | Change |
|--------|-----------|--------|
| `draw()` | override | MODIFIED: dispatch to bipolar/discrete/bar modes |
| `drawBipolarBars()` | `void(CDrawContext*)` | NEW: center-line + signed bars |
| `drawDiscreteBlocks()` | `void(CDrawContext*)` | NEW: stacked blocks 1-4 |
| `drawBipolarMiniPreview()` | `void(CDrawContext*, CRect)` | NEW: mini bipolar bars |
| `drawDiscreteMiniPreview()` | `void(CDrawContext*, CRect)` | NEW: mini block indicators |
| `onMouseDown()` | override | MODIFIED: dispatch to bipolar/discrete/bar interaction |
| `onMouseMoved()` | override | MODIFIED: dispatch by mode |
| `handleDiscreteClick()` | `void(int step)` | NEW: cycle 1->2->3->4->1 |
| `handleDiscreteDrag()` | `void(CPoint, int step)` | NEW: vertical drag, 8px/level |
| `getView()` | IArpLane override | NEW: returns `this` |
| `setPlayheadStep()` | IArpLane override | NEW: delegates to `setPlaybackStep()` |
| `setLength()` | IArpLane override | NEW: delegates to `setNumSteps()` |

**Bipolar Mode Specifics** (laneType_ == kPitch):
- Center line drawn at vertical midpoint of bar area
- `getLevelFromY()` returns signed value: -1.0 to +1.0 (mapped from -24..+24)
- Values snap to nearest integer semitone: `semitones = round((normalized - 0.5f) * 48.0f)`, `normalized = 0.5f + semitones / 48.0f`
- Grid labels: "+24" (top), "0" (center), "-24" (bottom)
- Miniature preview: bars above/below center line

**Discrete Mode Specifics** (laneType_ == kRatchet):
- Stacked blocks instead of bars: N blocks per step (N = 1..4)
- Block height = (barAreaHeight - 3*gap) / 4, gap = 2px
- Click cycles value: 1->2->3->4->1 (click = <4px vertical movement)
- Drag: 8px per level, clamped 1-4 (no wrap)
- Grid labels: "4" (top), "1" (bottom)
- Miniature preview: tiny block indicators at 25%/50%/75%/100% height
- `expandedHeight_` = 52.0f (36px body + 16px header)

---

### ArpModifierLane (New)

**Location**: `plugins/shared/src/ui/arp_modifier_lane.h`
**Type**: CControl subclass, implements IArpLane

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `header_` | `ArpLaneHeader` | | Collapsible header |
| `stepFlags_` | `std::array<uint8_t, 32>` | all `0x01` | Bitmask per step (kStepActive default) |
| `numSteps_` | `int` | `16` | Active step count |
| `playheadStep_` | `int` | `-1` | Current playhead position |
| `accentColor_` | `VSTGUI::CColor` | Rose | #C0707C |
| `stepFlagBaseParamId_` | `uint32_t` | `0` | Base param ID for step flags |
| `expandedHeight_` | `float` | `60.0f` | 44px body + 16px header |
| `paramCallback_` | `ParameterCallback` | null | Value change callback |
| `beginEditCallback_` | `EditCallback` | null | Begin edit callback |
| `endEditCallback_` | `EditCallback` | null | End edit callback |

**Constants**:
- `kRowCount = 4` (Rest, Tie, Slide, Accent)
- `kLeftMargin = 40.0f` (for row labels)
- `kDotRadius = 4.0f`
- `kRowHeight = 11.0f` (44px / 4 rows)
- `kRowLabels[] = {"Rest", "Tie", "Slide", "Accent"}`
- `kRowBits[] = {0x01, 0x02, 0x04, 0x08}` (kStepActive, kStepTie, kStepSlide, kStepAccent)

**Modifier Bitmask Encoding**:
| Bit | Flag | Meaning |
|-----|------|---------|
| 0 | kStepActive (0x01) | Note fires. "Rest" dot active = this bit OFF |
| 1 | kStepTie (0x02) | Sustain previous note |
| 2 | kStepSlide (0x04) | Legato/portamento |
| 3 | kStepAccent (0x08) | Velocity boost |
| 4-7 | (reserved) | MUST be masked off: always use `flags & 0x0F` |

**Normalized encoding**: `normalizedValue = (flags & 0x0F) / 15.0f`. Default (kStepActive only) = `1/15.0f`. Decode: `flags = uint8_t(round(normalized * 15.0f)) & 0x0F`.

**Rest Toggle Logic**: The Rest row is inverted relative to kStepActive:
- Rest dot inactive (outline) = kStepActive ON = note fires (default)
- Rest dot active (filled) = kStepActive OFF = rest/silence
- Toggle: `flags ^= kStepActive` (XOR flips bit 0)

**Other Flags**: Direct mapping:
- Tie dot active = kStepTie ON
- Slide dot active = kStepSlide ON
- Accent dot active = kStepAccent ON
- Toggle: `flags ^= kStepTie` (or kStepSlide, kStepAccent)

---

### ArpConditionLane (New)

**Location**: `plugins/shared/src/ui/arp_condition_lane.h`
**Type**: CControl subclass, implements IArpLane

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `header_` | `ArpLaneHeader` | | Collapsible header |
| `stepConditions_` | `std::array<uint8_t, 32>` | all `0` | TrigCondition index per step |
| `numSteps_` | `int` | `8` | Active step count |
| `playheadStep_` | `int` | `-1` | Current playhead position |
| `accentColor_` | `VSTGUI::CColor` | Slate | #7C90B0 |
| `stepConditionBaseParamId_` | `uint32_t` | `0` | Base param ID for step conditions |
| `expandedHeight_` | `float` | `44.0f` (= kBodyHeight + ArpLaneHeader::kHeight) | 28px body + 16px header |
| `paramCallback_` | `ParameterCallback` | null | Value change callback |

**Normalized encoding**: `normalizedValue = index / 17.0f` (indices 0..17 → 0.0..1.0). Decode: `index = clamp(round(normalized * 17.0f), 0, 17)`.

**Condition Abbreviation Table** (static constexpr array):
```cpp
static constexpr const char* kConditionAbbrev[18] = {
    "Alw", "10%", "25%", "50%", "75%", "90%",
    "Ev2", "2:2", "Ev3", "2:3", "3:3",
    "Ev4", "2:4", "3:4", "4:4",
    "1st", "Fill", "!F"
};
```

**Condition Full Names** (for COptionMenu popup and tooltips):

The array `kConditionFullNames[18]` is used for the COptionMenu popup entries. The array `kConditionTooltips[18]` provides the longer descriptions used by `setTooltipText()`. Both are defined in the implementation; implementations MUST use the name `kConditionTooltips` for the tooltip array to match the task references (T056) and contract.

```cpp
static constexpr const char* kConditionTooltips[18] = {
    "Always -- Step fires unconditionally",
    "10% -- ~10% probability of firing",
    "25% -- ~25% probability of firing",
    "50% -- ~50% probability of firing",
    "75% -- ~75% probability of firing",
    "90% -- ~90% probability of firing",
    "Every 2 -- Fires on 1st of every 2 loops",
    "2nd of 2 -- Fires on 2nd of every 2 loops",
    "Every 3 -- Fires on 1st of every 3 loops",
    "2nd of 3 -- Fires on 2nd of every 3 loops",
    "3rd of 3 -- Fires on 3rd of every 3 loops",
    "Every 4 -- Fires on 1st of every 4 loops",
    "2nd of 4 -- Fires on 2nd of every 4 loops",
    "3rd of 4 -- Fires on 3rd of every 4 loops",
    "4th of 4 -- Fires on 4th of every 4 loops",
    "First -- Fires only on first loop",
    "Fill -- Fires only when fill mode is active",
    "Not Fill -- Fires only when fill mode is NOT active"
};
```

---

### ArpLaneContainer (Modified)

**Location**: `plugins/shared/src/ui/arp_lane_container.h`

**Changes**:
| Field | Old | New |
|-------|-----|-----|
| `lanes_` | `std::vector<ArpLaneEditor*>` | `std::vector<IArpLane*>` |

**Method changes**:
| Method | Old signature | New signature |
|--------|--------------|---------------|
| `addLane()` | `void(ArpLaneEditor*)` | `void(IArpLane*)` |
| `removeLane()` | `void(ArpLaneEditor*)` | `void(IArpLane*)` |
| `getLane()` | `ArpLaneEditor*(size_t)` | `IArpLane*(size_t)` |
| `recalculateLayout()` | uses `lane->isCollapsed()` etc. | uses `IArpLane` interface methods |

**Note**: `getLane()` is for layout/container purposes only. Plugin-specific operations (e.g., calling `setStepFlags()` on ArpModifierLane) use direct typed pointers held by controller.cpp, not container->getLane().

---

## Parameter ID Map

| Lane | Length ID | Step Base ID | Step Range | Normalized Encoding | Playhead ID |
|------|----------|-------------|------------|---------------------|-------------|
| Velocity | 3020 | 3021 | 0.0-1.0 (32 steps) | direct (0.0-1.0) | 3294 |
| Gate | 3060 | 3061 | 0.0-1.0 (32 steps) | direct (0.0-1.0) | 3295 |
| Pitch | 3100 | 3101 | -24..+24 semitones (32 steps) | `0.5f + semitones/48.0f` | 3296 (NEW) |
| Modifier | 3140 | 3141 | 0x00-0x0F bitmask (32 steps) | `(flags & 0x0F) / 15.0f` | 3298 (NEW) |
| Ratchet | 3190 | 3191 | 1-4 (32 steps) | `(count - 1) / 3.0f` | 3297 (NEW) |
| Condition | 3240 | 3241 | 0-17 enum (32 steps) | `index / 17.0f` | 3299 (NEW) |

**Playhead encoding (all lanes)**: `normalizedValue = (step + 1) / 32.0f` when active; `0.0f` = no playhead. Decode: `step = round(normalized * 32.0f) - 1`.

**Note**: Velocity/Gate IDs (3020/3021, 3060/3061) are as implemented in Phase 11a. The data-model originally showed incorrect placeholder IDs (3000/3050).

---

## Color Palette

| Lane | Accent | Normal (0.6x) | Ghost (0.35x) |
|------|--------|---------------|----------------|
| Velocity | #D0845C (208,132,92) | (124,79,55) | (72,46,32) |
| Gate | #C8A464 (200,164,100) | (120,98,60) | (70,57,35) |
| Pitch | #6CA8A0 (108,168,160) | (64,100,96) | (37,58,56) |
| Ratchet | #9880B0 (152,128,176) | (91,76,105) | (53,44,61) |
| Modifier | #C0707C (192,112,124) | (115,67,74) | (67,39,43) |
| Condition | #7C90B0 (124,144,176) | (74,86,105) | (43,50,61) |

---

## State Transitions

### Lane Collapse/Expand State Machine

```
EXPANDED --[click collapse toggle]--> COLLAPSED
COLLAPSED --[click collapse toggle]--> EXPANDED
```

- On EXPANDED->COLLAPSED: save expandedHeight_, resize to headerHeight (16px)
- On COLLAPSED->EXPANDED: resize to expandedHeight_
- Both transitions fire collapseCallback_ which triggers container relayout

### Ratchet Click Cycle

```
1 --[click]--> 2 --[click]--> 3 --[click]--> 4 --[click]--> 1 (wrap)
```

### Modifier Toggle

```
INACTIVE --[click]--> ACTIVE
ACTIVE --[click]--> INACTIVE
```

Per bit, per step. Bits are independent.
