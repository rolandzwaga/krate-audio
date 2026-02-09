# Data Model: Step Pattern Editor

**Feature Branch**: `046-step-pattern-editor`
**Date**: 2026-02-09

## Entities

### 1. StepPatternEditor (UI Component)

**Location**: `plugins/shared/src/ui/step_pattern_editor.h`
**Namespace**: `Krate::Plugins`
**Base Class**: `VSTGUI::CControl`

#### State Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `stepLevels_` | `std::array<float, 32>` | all 1.0 | Per-step gain levels [0.0, 1.0] |
| `numSteps_` | `int` | 16 | Active step count [2, 32] |
| `playbackStep_` | `int` | -1 | Currently playing step index (-1 = none) |
| `isPlaying_` | `bool` | false | Transport playing state |
| `phaseOffset_` | `float` | 0.0 | Phase offset [0.0, 1.0] |
| `euclideanEnabled_` | `bool` | false | Euclidean mode active |
| `euclideanHits_` | `int` | 4 | Euclidean pulse count |
| `euclideanRotation_` | `int` | 0 | Euclidean rotation offset |
| `euclideanPattern_` | `uint32_t` | 0 | Current Euclidean bitmask (generated) |
| `isModified_` | `bool` | false | True if steps deviate from pure Euclidean |

#### Drag State Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `isDragging_` | `bool` | false | Active drag gesture in progress |
| `dirtySteps_` | `std::bitset<32>` | all 0 | Steps modified in current gesture |
| `preDragLevels_` | `std::array<float, 32>` | -- | Snapshot at gesture start for Escape cancel |
| `dragStartY_` | `float` | 0 | Y coordinate at drag start (for fine mode) |
| `fineMode_` | `bool` | false | Shift-drag active (0.1x sensitivity) |

#### Zoom/Scroll State Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `zoomLevel_` | `float` | 1.0 | Zoom factor (1.0 = fit all) |
| `scrollOffset_` | `int` | 0 | First visible step index |
| `visibleSteps_` | `int` | 32 | Number of visible steps at current zoom |

#### Color Configuration Fields (ViewCreator attributes)

| Field | Type | Default | Attribute Name |
|-------|------|---------|----------------|
| `barColorAccent_` | `CColor` | `{220, 170, 60, 255}` | `bar-color-accent` |
| `barColorNormal_` | `CColor` | `{80, 140, 200, 255}` | `bar-color-normal` |
| `barColorGhost_` | `CColor` | `{60, 90, 120, 255}` | `bar-color-ghost` |
| `silentOutlineColor_` | `CColor` | `{50, 50, 55, 255}` | `silent-outline-color` |
| `gridColor_` | `CColor` | `{255, 255, 255, 30}` | `grid-color` |
| `backgroundColor_` | `CColor` | `{35, 35, 38, 255}` | `background-color` |
| `playbackColor_` | `CColor` | `{255, 200, 80, 255}` | `playback-color` |
| `textColor_` | `CColor` | `{180, 180, 185, 255}` | `text-color` |

#### Callback

| Field | Type | Description |
|-------|------|-------------|
| `paramCallback_` | `std::function<void(ParamID, float)>` | Called on step level changes during interaction |

#### Timer

| Field | Type | Description |
|-------|------|-------------|
| `refreshTimer_` | `SharedPointer<CVSTGUITimer>` | ~30fps timer for playback position updates |

#### Random Number Generator

| Field | Type | Description |
|-------|------|-------------|
| `rng_` | `std::mt19937` | Mersenne Twister for random pattern generation |

### 2. StepPatternEditorCreator (ViewCreator Registration)

**Location**: Same header as StepPatternEditor
**Type**: `struct` extending `VSTGUI::ViewCreatorAdapter`
**Registration**: `inline StepPatternEditorCreator gStepPatternEditorCreator;`

| Method | Purpose |
|--------|---------|
| `getViewName()` | Returns `"StepPatternEditor"` |
| `getBaseViewName()` | Returns `kCControl` |
| `create()` | Returns `new StepPatternEditor(CRect(0,0,500,200), nullptr, -1)` |
| `apply()` | Reads color attributes from XML |
| `getAttributeNames()` | Lists all configurable attributes |
| `getAttributeType()` | Returns type for each attribute |
| `getAttributeValue()` | Reads current attribute values for serialization |

### 3. ColorUtils (Shared Utility)

**Location**: `plugins/shared/src/ui/color_utils.h`
**Namespace**: `Krate::Plugins`

| Function | Signature | Description |
|----------|-----------|-------------|
| `lerpColor` | `inline CColor lerpColor(const CColor& a, const CColor& b, float t)` | Linear interpolation between two colors |
| `darkenColor` | `inline CColor darkenColor(const CColor& color, float factor)` | Darken color by factor (0=black, 1=unchanged) |
| `brightenColor` | `inline CColor brightenColor(const CColor& color, float factor)` | Brighten color by factor (1=unchanged, >1=brighter) |

### 4. Parameter Extensions (Ruinae-specific)

**Location**: `plugins/ruinae/src/plugin_ids.h` (ID definitions)
**Location**: `plugins/ruinae/src/parameters/trance_gate_params.h` (registration + handling)

#### New Parameter IDs

| ID | Name | Type | Range | Default | Description |
|----|------|------|-------|---------|-------------|
| 608 | `kTranceGateEuclideanEnabledId` | bool | 0/1 | 0 | Euclidean mode toggle |
| 609 | `kTranceGateEuclideanHitsId` | int | 0-32 | 4 | Euclidean pulse count |
| 610 | `kTranceGateEuclideanRotationId` | int | 0-31 | 0 | Euclidean rotation |
| 611 | `kTranceGatePhaseOffsetId` | float | 0.0-1.0 | 0.0 | Pattern phase offset |
| 668 | `kTranceGateStepLevel0Id` | float | 0.0-1.0 | 1.0 | Step 0 level |
| 669 | `kTranceGateStepLevel1Id` | float | 0.0-1.0 | 1.0 | Step 1 level |
| ... | ... | ... | ... | ... | ... |
| 699 | `kTranceGateStepLevel31Id` | float | 0.0-1.0 | 1.0 | Step 31 level |

#### Modified Parameter

| ID | Name | Old Type | New Type | New Range | New Default |
|----|------|----------|----------|-----------|-------------|
| 601 | `kTranceGateNumStepsId` | StringListParameter (3 values) | RangeParameter | 2-32 (int, stepCount=30) | 16 |

### 5. Layout Zones (Computed, Internal to StepPatternEditor)

The view dynamically computes zone rectangles from its total size. These are the **internal zones** of the StepPatternEditor CControl only. Euclidean toolbar controls, quick action buttons, and parameter knobs are **separate standard VSTGUI controls** in the parent container.

| Zone | Height (px) | Purpose | Visible When |
|------|-------------|---------|-------------|
| 1. Scroll indicator | 6 (conditional) | ◀ thumb ▶ visible range bar | Step count >= 24 and zoomed |
| 2. Phase offset indicator | 12 | ▽ phase start triangle | Always (when phaseOffset set) |
| 3. Top grid line | (within bars) | ── with "1.0" label | Always |
| 4. Step bars (main edit) | Remaining | Bar chart with color-coded bars | Always |
| 5. Bottom grid line | (within bars) | ── with "0.0" label | Always |
| 6. Euclidean dots | 10 (conditional) | ● hit / ○ rest / ○· rest w/ ghost | Euclidean mode active |
| 7. Step labels | 12 | Numbers every 4th step (1, 5, 9...) | Always |
| 8. Playback indicator | 8 | ▲ current step triangle | Transport playing |

### 6. Quick Action Buttons (External — NOT part of StepPatternEditor)

Quick action buttons are **separate standard VSTGUI buttons** (CTextButton or CKickButton) in the parent container, wired to call the editor's public preset/transform API methods. They are NOT computed or rendered by the StepPatternEditor.

| Button | Label | API Method Called |
|--------|-------|-------------------|
| All | "All" | `applyPresetAll()` |
| Off | "Off" | `applyPresetOff()` |
| Alt | "Alt" | `applyPresetAlternate()` |
| Ramp Up | "↗" | `applyPresetRampUp()` |
| Ramp Down | "↘" | `applyPresetRampDown()` |
| Invert | "Inv" | `applyTransformInvert()` |
| Shift Left | "◀" | `applyTransformShiftLeft()` |
| Shift Right | "▶" | `applyTransformShiftRight()` |
| Random | "Rnd" | `applyPresetRandom()` |
| Regen | "Regen" | `regenerateEuclidean()` (visible only when Euclidean ON) |

## Relationships

```
StepPatternEditor --uses--> ColorUtils (lerpColor, darkenColor, brightenColor)
StepPatternEditor --uses--> EuclideanPattern (generate, isHit from DSP Layer 0)
StepPatternEditor --registered-by--> StepPatternEditorCreator (ViewCreator)
StepPatternEditor --communicates-via--> ParameterCallback --> Controller --> Host Parameters
Controller --sets--> StepPatternEditor.setPlaybackStep() (from IMessage or output parameter)
Controller --sets--> StepPatternEditor.setPlaying() (transport state)
Controller --sets--> StepPatternEditor step levels (from host parameter changes)
```

## State Transitions

### Drag States

```
IDLE --[mouseDown]--> DRAGGING
DRAGGING --[mouseUp]--> IDLE (commit: endEdit all dirty steps)
DRAGGING --[Escape]--> IDLE (cancel: revert to preDragLevels_)
DRAGGING --[mouseCancel]--> IDLE (cancel: revert to preDragLevels_)
DRAGGING --[stepCountChange]--> IDLE (cancel: revert to preDragLevels_)
```

### Euclidean Mode States

```
OFF --[enable toggle]--> ON (generate pattern, set steps)
ON --[user edits step]--> ON_MODIFIED (asterisk indicator)
ON_MODIFIED --[regen button]--> ON (pure Euclidean, reset levels)
ON --[disable toggle]--> OFF (keep current levels)
ON_MODIFIED --[disable toggle]--> OFF (keep current levels)
```

### Timer States

```
NO_TIMER --[setPlaying(true)]--> TIMER_ACTIVE (create CVSTGUITimer at 33ms)
TIMER_ACTIVE --[setPlaying(false)]--> NO_TIMER (release timer)
TIMER_ACTIVE --[view removed]--> NO_TIMER (destructor releases timer)
```

## Validation Rules

| Field | Rule |
|-------|------|
| stepLevels_[i] | Clamped to [0.0, 1.0] on set |
| numSteps_ | Clamped to [2, 32] on set |
| euclideanHits_ | Clamped to [0, numSteps_] |
| euclideanRotation_ | Clamped to [0, numSteps_-1] |
| phaseOffset_ | Clamped to [0.0, 1.0] |
| zoomLevel_ | Clamped to [1.0, maxZoom] where maxZoom = numSteps/4 |
| scrollOffset_ | Clamped to [0, numSteps_ - visibleSteps_] |
