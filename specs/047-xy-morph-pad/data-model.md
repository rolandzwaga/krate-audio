# Data Model: XYMorphPad Custom Control

**Date**: 2026-02-10
**Spec**: [spec.md](spec.md)

## Entities

### XYMorphPad (CControl subclass)

**Location**: `plugins/shared/src/ui/xy_morph_pad.h`
**Namespace**: `Krate::Plugins`
**Inherits**: `VSTGUI::CControl`

| Field | Type | Default | Description | Validation |
|-------|------|---------|-------------|------------|
| `morphX_` | `float` | 0.5f | Normalized X position [0,1] (morph position) | Clamped [0.0, 1.0] |
| `morphY_` | `float` | 0.5f | Normalized Y position [0,1] (spectral tilt) | Clamped [0.0, 1.0] |
| `modRangeX_` | `float` | 0.0f | Modulation range on X axis (bipolar) | Unclamped, display clamped |
| `modRangeY_` | `float` | 0.0f | Modulation range on Y axis (bipolar) | Unclamped, display clamped |
| `gridSize_` | `int` | 24 | Gradient grid resolution (square) | Min 4, max 64 |
| `isDragging_` | `bool` | false | Whether user is actively dragging | -- |
| `isFineAdjustment_` | `bool` | false | Whether Shift is held for 0.1x scale | -- |
| `preDragMorphX_` | `float` | 0.0f | X position before drag started (for Escape) | -- |
| `preDragMorphY_` | `float` | 0.0f | Y position before drag started (for Escape) | -- |
| `dragStartPixelX_` | `float` | 0.0f | Pixel X at drag start (for fine adjustment) | -- |
| `dragStartPixelY_` | `float` | 0.0f | Pixel Y at drag start (for fine adjustment) | -- |
| `dragStartMorphX_` | `float` | 0.0f | Morph X at drag start (for fine adjustment) | -- |
| `dragStartMorphY_` | `float` | 0.0f | Morph Y at drag start (for fine adjustment) | -- |
| `controller_` | `EditControllerEx1*` | nullptr | Controller for Y axis performEdit calls | Must be set before use |
| `secondaryParamId_` | `ParamID` | 0 | Parameter ID for Y axis (tilt) | 0 = disabled |
| `colorBottomLeft_` | `CColor` | {48, 84, 120} | Gradient corner: OSC A + dark | -- |
| `colorBottomRight_` | `CColor` | {132, 102, 36} | Gradient corner: OSC B + dark | -- |
| `colorTopLeft_` | `CColor` | {80, 140, 200} | Gradient corner: OSC A + bright | -- |
| `colorTopRight_` | `CColor` | {220, 170, 60} | Gradient corner: OSC B + bright | -- |
| `cursorColor_` | `CColor` | {255, 255, 255} | Cursor circle and center dot color | -- |
| `labelColor_` | `CColor` | {170, 170, 170} | Corner and position label color | -- |
| `crosshairOpacity_` | `float` | 0.12f | Crosshair line opacity [0-1] | Clamped [0.0, 1.0] |

### Constants

| Constant | Type | Value | Description |
|----------|------|-------|-------------|
| `kCursorDiameter` | `float` | 16.0f | Cursor open circle diameter in pixels |
| `kCursorStrokeWidth` | `float` | 2.0f | Cursor circle stroke width |
| `kCenterDotDiameter` | `float` | 4.0f | Cursor center dot diameter |
| `kPadding` | `float` | 8.0f | Edge padding for coordinate conversion |
| `kFineAdjustmentScale` | `float` | 0.1f | Fine mode sensitivity multiplier |
| `kScrollSensitivity` | `float` | 0.05f | Scroll wheel change per unit (5%) |
| `kMinDimension` | `float` | 80.0f | Minimum pad size for usability |
| `kLabelHideThreshold` | `float` | 100.0f | Below this (either dimension in px), hide labels |
| `kDefaultGridSize` | `int` | 24 | Default gradient grid resolution |

## Relationships

```
XYMorphPad --[uses]--> color_utils.h (lerpColor, bilinearColor)
XYMorphPad --[binds X via]--> CControl tag -> kMixerPositionId (301)
XYMorphPad --[sends Y via]--> controller_->performEdit(kMixerTiltId)
XYMorphPad --[registered by]--> XYMorphPadCreator (ViewCreator)
Ruinae::Controller --[verifyView]--> XYMorphPad (sets controller_, secondaryParamId_)
Ruinae::Controller --[setParamNormalized]--> XYMorphPad (syncs X/Y from host)
editor.uidesc --[declares]--> XYMorphPad instance with attributes
```

## State Transitions

### Drag State Machine

```
IDLE
  |-- onMouseDownEvent (left, not double-click) --> DRAGGING
  |-- onMouseDownEvent (double-click) --> IDLE (reset to 0.5, 0.5)
  |-- onMouseWheelEvent --> IDLE (adjust X/Y by scroll amount)

DRAGGING
  |-- onMouseMoveEvent --> DRAGGING (update position, check Shift for fine mode)
  |-- onMouseUpEvent --> IDLE (end edit)
  |-- onKeyboardEvent (Escape) --> IDLE (revert to preDrag values)
```

### Parameter Flow

```
User drag/click:
  1. beginEdit() for CControl tag (X)
  2. controller_->beginEdit(secondaryParamId_) for Y
  3. setValue(newX), valueChanged() -> host receives X
  4. controller_->performEdit(secondaryParamId_, newY) -> host receives Y
  5. On mouse up: endEdit() for X, controller_->endEdit(secondaryParamId_) for Y

Host automation:
  1. setParamNormalized(kMixerPositionId, value) -> Controller forwards to pad
  2. setParamNormalized(kMixerTiltId, value) -> Controller forwards to pad
  3. Pad updates morphX_/morphY_ and invalidates (NO valueChanged feedback)
```

## New Parameter: kMixerTiltId

**Location**: `plugins/ruinae/src/plugin_ids.h`

| Property | Value |
|----------|-------|
| ID | 302 |
| Name | "Spectral Tilt" |
| Units | "dB/oct" |
| Range | [-12.0, +12.0] |
| Default | 0.0 (normalized: 0.5) |
| Type | RangeParameter |
| Flags | kCanAutomate |

### MixerParams Extension

**Location**: `plugins/ruinae/src/parameters/mixer_params.h`

New field: `std::atomic<float> tilt{0.0f};` (in dB/oct, [-12, +12])

Handler: Denormalize from [0,1] to [-12, +12]:
```
tilt = -12.0f + static_cast<float>(value) * 24.0f
```

Registration: `RangeParameter` with min=-12, max=12, default=0, stepCount=0, units="dB/oct"

## New Utility: bilinearColor

**Location**: `plugins/shared/src/ui/color_utils.h`

```cpp
/// @brief Bilinear interpolation of 4 corner colors.
/// @param bottomLeft Color at (0, 0)
/// @param bottomRight Color at (1, 0)
/// @param topLeft Color at (0, 1)
/// @param topRight Color at (1, 1)
/// @param tx Horizontal interpolation factor [0.0, 1.0]
/// @param ty Vertical interpolation factor [0.0, 1.0]
/// @return Interpolated color
[[nodiscard]] inline VSTGUI::CColor bilinearColor(
    const VSTGUI::CColor& bottomLeft,
    const VSTGUI::CColor& bottomRight,
    const VSTGUI::CColor& topLeft,
    const VSTGUI::CColor& topRight,
    float tx, float ty)
{
    VSTGUI::CColor bottom = lerpColor(bottomLeft, bottomRight, tx);
    VSTGUI::CColor top = lerpColor(topLeft, topRight, tx);
    return lerpColor(bottom, top, ty);
}
```
