# Data Model: ADSRDisplay Custom Control

**Feature**: 048-adsr-display | **Date**: 2026-02-10

## Entities

### ADSRDisplay (CControl subclass)

**Location**: `plugins/shared/src/ui/adsr_display.h`
**Namespace**: `Krate::Plugins`
**Base class**: `VSTGUI::CControl`

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `attackMs_` | `float` | 10.0f | Attack time in milliseconds |
| `decayMs_` | `float` | 50.0f | Decay time in milliseconds |
| `sustainLevel_` | `float` | 0.5f | Sustain level [0, 1] |
| `releaseMs_` | `float` | 100.0f | Release time in milliseconds |
| `attackCurve_` | `float` | 0.0f | Attack curve amount [-1, +1] |
| `decayCurve_` | `float` | 0.0f | Decay curve amount [-1, +1] |
| `releaseCurve_` | `float` | 0.0f | Release curve amount [-1, +1] |
| `bezierEnabled_` | `bool` | false | Bezier mode active |
| `bezierHandles_` | `BezierHandles[3]` | default | Bezier cp1/cp2 per segment |
| `layout_` | `SegmentLayout` | computed | Cached segment pixel positions |
| `dragState_` | `DragState` | None | Current drag interaction state |
| `isDragging_` | `bool` | false | Whether a drag is in progress |
| `preDragValues_` | `PreDragValues` | -- | Stored values for Escape cancel |
| `playbackOutput_` | `float` | 0.0f | Current envelope output level |
| `playbackStage_` | `int` | 0 | Current envelope stage |
| `voiceActive_` | `bool` | false | Whether a voice is playing |

**Color attributes (ViewCreator-configurable)**:

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `fillColor_` | `CColor` | rgba(80,140,200,77) | Envelope fill color |
| `strokeColor_` | `CColor` | rgb(80,140,200) | Envelope stroke color |
| `backgroundColor_` | `CColor` | rgb(30,30,33) | Background fill |
| `gridColor_` | `CColor` | rgba(255,255,255,25) | Grid line color |
| `controlPointColor_` | `CColor` | rgb(255,255,255) | Control point fill |
| `textColor_` | `CColor` | rgba(255,255,255,180) | Label text color |

**Callbacks**:

| Field | Type | Description |
|-------|------|-------------|
| `paramCallback_` | `ParameterCallback` | `std::function<void(uint32_t paramId, float normalizedValue)>` |
| `beginEditCallback_` | `EditCallback` | `std::function<void(uint32_t paramId)>` |
| `endEditCallback_` | `EditCallback` | `std::function<void(uint32_t paramId)>` |

**Parameter ID fields**:

| Field | Type | Description |
|-------|------|-------------|
| `attackParamId_` | `uint32_t` | VST parameter ID for attack time |
| `decayParamId_` | `uint32_t` | VST parameter ID for decay time |
| `sustainParamId_` | `uint32_t` | VST parameter ID for sustain level |
| `releaseParamId_` | `uint32_t` | VST parameter ID for release time |
| `attackCurveParamId_` | `uint32_t` | VST parameter ID for attack curve |
| `decayCurveParamId_` | `uint32_t` | VST parameter ID for decay curve |
| `releaseCurveParamId_` | `uint32_t` | VST parameter ID for release curve |
| `bezierEnabledParamId_` | `uint32_t` | VST parameter ID for Bezier mode flag |
| `bezierBaseParamId_` | `uint32_t` | Base ID for Bezier control point params (12 consecutive) |

---

### SegmentLayout (computed)

Cached pixel positions for the four display segments, computed on parameter change.

| Field | Type | Description |
|-------|------|-------------|
| `attackStartX` | `float` | Left edge of attack segment (pixels) |
| `attackEndX` | `float` | Right edge of attack segment |
| `decayEndX` | `float` | Right edge of decay segment |
| `sustainEndX` | `float` | Right edge of sustain-hold segment |
| `releaseEndX` | `float` | Right edge of release segment |
| `topY` | `float` | Top of drawing area (level=1.0) |
| `bottomY` | `float` | Bottom of drawing area (level=0.0) |

---

### DragTarget (enum)

Identifies which element is being dragged. Points use the `Point` suffix, curves use the `Curve` suffix, and Bezier handles use the `Handle` suffix.

```cpp
enum class DragTarget {
    None,
    PeakPoint,       // Horizontal only (attack time)
    SustainPoint,    // Both axes (decay time + sustain level)
    EndPoint,        // Horizontal only (release time)
    AttackCurve,     // Curve amount [-1, +1]
    DecayCurve,      // Curve amount [-1, +1]
    ReleaseCurve,    // Curve amount [-1, +1]
    BezierHandle     // Specific Bezier cp (identified by segment + handle index)
};
```

---

### PreDragValues (for Escape cancel)

| Field | Type | Description |
|-------|------|-------------|
| `attackMs` | `float` | Stored attack time before drag |
| `decayMs` | `float` | Stored decay time before drag |
| `sustainLevel` | `float` | Stored sustain level before drag |
| `releaseMs` | `float` | Stored release time before drag |
| `attackCurve` | `float` | Stored attack curve before drag |
| `decayCurve` | `float` | Stored decay curve before drag |
| `releaseCurve` | `float` | Stored release curve before drag |

---

### BezierHandles (per-segment struct, stored as `bezierHandles_[3]` array — one per segment: Attack, Decay, Release)

| Field | Type | Range | Description |
|-------|------|-------|-------------|
| `cp1x` | `float` | [0, 1] | Control point 1 X (normalized in segment) |
| `cp1y` | `float` | [0, 1] | Control point 1 Y (normalized in segment) |
| `cp2x` | `float` | [0, 1] | Control point 2 X (normalized in segment) |
| `cp2y` | `float` | [0, 1] | Control point 2 Y (normalized in segment) |

---

## New Parameter IDs

### Curve Amount Parameters (9 total)

| Envelope | Parameter | ID | Range | Default |
|----------|-----------|-----|-------|---------|
| Amp | Attack Curve | 704 | [-1, +1] | 0.0 |
| Amp | Decay Curve | 705 | [-1, +1] | 0.0 |
| Amp | Release Curve | 706 | [-1, +1] | 0.0 |
| Filter | Attack Curve | 804 | [-1, +1] | 0.0 |
| Filter | Decay Curve | 805 | [-1, +1] | 0.0 |
| Filter | Release Curve | 806 | [-1, +1] | 0.0 |
| Mod | Attack Curve | 904 | [-1, +1] | 0.0 |
| Mod | Decay Curve | 905 | [-1, +1] | 0.0 |
| Mod | Release Curve | 906 | [-1, +1] | 0.0 |

### Bezier Mode Flags (3 total)

| Envelope | Parameter | ID | Range | Default |
|----------|-----------|-----|-------|---------|
| Amp | Bezier Enabled | 707 | 0/1 | 0 |
| Filter | Bezier Enabled | 807 | 0/1 | 0 |
| Mod | Bezier Enabled | 907 | 0/1 | 0 |

### Bezier Control Point Parameters (36 total)

| Envelope | Segment | cp1.x | cp1.y | cp2.x | cp2.y |
|----------|---------|-------|-------|-------|-------|
| Amp | Attack | 710 | 711 | 712 | 713 |
| Amp | Decay | 714 | 715 | 716 | 717 |
| Amp | Release | 718 | 719 | 720 | 721 |
| Filter | Attack | 810 | 811 | 812 | 813 |
| Filter | Decay | 814 | 815 | 816 | 817 |
| Filter | Release | 818 | 819 | 820 | 821 |
| Mod | Attack | 910 | 911 | 912 | 913 |
| Mod | Decay | 914 | 915 | 916 | 917 |
| Mod | Release | 918 | 919 | 920 | 921 |

All Bezier values normalized [0.0, 1.0].

**Total new parameters: 48** (9 curve + 3 flags + 36 Bezier)

---

## DSP Modifications

### ADSREnvelope (Modified)

**Location**: `dsp/include/krate/dsp/primitives/adsr_envelope.h`

New/modified fields:

| Field | Type | Description |
|-------|------|-------------|
| `attackCurveAmount_` | `float` | Continuous curve [-1, +1] (replaces enum) |
| `decayCurveAmount_` | `float` | Continuous curve [-1, +1] |
| `releaseCurveAmount_` | `float` | Continuous curve [-1, +1] |
| `attackTable_` | `std::array<float, 256>` | Precomputed curve lookup |
| `decayTable_` | `std::array<float, 256>` | Precomputed curve lookup |
| `releaseTable_` | `std::array<float, 256>` | Precomputed curve lookup |

New/modified methods:

| Method | Signature | Description |
|--------|-----------|-------------|
| `setAttackCurve(float)` | `void setAttackCurve(float amount) noexcept` | Set continuous curve amount |
| `setDecayCurve(float)` | `void setDecayCurve(float amount) noexcept` | Set continuous curve amount |
| `setReleaseCurve(float)` | `void setReleaseCurve(float amount) noexcept` | Set continuous curve amount |
| `setAttackCurve(EnvCurve)` | Preserved | Backward compat, maps to float |
| `regenerateTable()` | Private | Fills lookup table from curve amount |

### Curve Lookup Table Utility (New, Layer 0)

**Location**: `dsp/include/krate/dsp/core/curve_table.h`
**Namespace**: `Krate::DSP`

Shared utility for generating 256-entry curve lookup tables.

| Function | Signature | Description |
|----------|-----------|-------------|
| `generatePowerCurveTable` | `void generatePowerCurveTable(std::array<float, 256>& table, float curveAmount, float startLevel, float endLevel)` | Power curve: output = lerp(start, end, phase^(2^(curve*k))) |
| `generateBezierCurveTable` | `void generateBezierCurveTable(std::array<float, 256>& table, float cp1x, float cp1y, float cp2x, float cp2y, float startLevel, float endLevel)` | Cubic Bezier evaluation |
| `lookupCurveTable` | `float lookupCurveTable(const std::array<float, 256>& table, float phase)` | Linear interpolation in table |

---

## State Transitions

### Drag Interaction State Machine

```
Idle
  |-- onMouseDown (hit control point) --> Dragging(PeakPoint|SustainPoint|EndPoint)
  |-- onMouseDown (hit curve segment) --> Dragging(AttackCurve|DecayCurve|ReleaseCurve)
  |-- onMouseDown (hit Bezier handle) --> Dragging(BezierHandle)
  |-- onMouseDown (hit mode toggle) --> Toggle Bezier mode, stay Idle
  |-- onMouseDown (double-click point) --> Reset to default, stay Idle
  |-- onMouseDown (double-click curve) --> Reset curve to 0.0, stay Idle

Dragging(target)
  |-- onMouseMoved --> Update parameter(s) for target, redraw
  |-- onMouseUp --> endEdit, return to Idle
  |-- onKeyDown(Escape) --> Revert to pre-drag values, endEdit, return to Idle
  |-- Shift held --> Fine adjustment mode (0.1x scale)
```

### Envelope Playback State

```
Inactive (no voices)
  |-- Voice triggered --> Active

Active
  |-- Timer tick (33ms) --> Read atomic {stage, output}, update dot position, redraw
  |-- All voices idle --> Inactive, stop timer
```

---

## Validation Rules

| Field | Rule |
|-------|------|
| `attackMs_` | Clamp to [0.1, 10000] |
| `decayMs_` | Clamp to [0.1, 10000] |
| `sustainLevel_` | Clamp to [0.0, 1.0] |
| `releaseMs_` | Clamp to [0.1, 10000] |
| `*Curve_` | Clamp to [-1.0, +1.0] |
| `bezierHandles_.cp*` | Clamp to [0.0, 1.0] |
| Segment widths | Min 15% of display width |

---

## Relationships

```
ADSRDisplay (CControl)
  |-- uses --> ParameterCallback (from controller)
  |-- uses --> EditCallback (beginEdit/endEdit from controller)
  |-- reads --> atomic playback state (from processor via IMessage)
  |-- contains --> SegmentLayout (recomputed on param change)
  |-- contains --> DragTarget (current interaction)
  |-- contains --> PreDragValues (for cancel)
  |-- contains --> BezierHandles[3] (one per segment)
  |-- registered-by --> ADSRDisplayCreator (ViewCreator)

ADSREnvelope (DSP, Layer 1)
  |-- uses --> curve_table.h (Layer 0, table generation)
  |-- contains --> std::array<float, 256>[3] (lookup tables)

AmpEnvParams / FilterEnvParams / ModEnvParams
  |-- extended-with --> attackCurve, decayCurve, releaseCurve (atomic<float>)
  |-- extended-with --> bezierEnabled (atomic<float>)
  |-- extended-with --> bezier cp1x/cp1y/cp2x/cp2y x3 segments (atomic<float>)
```
