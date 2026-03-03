# Research: XYMorphPad Custom Control

**Date**: 2026-02-10
**Spec**: [spec.md](spec.md)

## Research Topics

### R1: VSTGUI CControl Dual-Parameter Pattern

**Decision**: Use the same dual-parameter pattern as Disrumpo's MorphPad: X axis via `CControl::setValue()` + `valueChanged()` (bound to control-tag), Y axis via explicit `controller->beginEdit()/performEdit()/endEdit()` on a secondary parameter ID.

**Rationale**: This is the established pattern in the codebase (Disrumpo MorphPad, `morph_pad.h:163` `setMorphYParamId()`). VSTGUI's `CControl` fundamentally supports only one value per control. The secondary parameter is communicated directly to the host via the EditController reference. The VST3Editor's built-in feedback loop prevention (`isEditing()` check in `valueChanged()`) handles the primary parameter; the secondary parameter is manually managed with begin/end/performEdit calls.

**Alternatives considered**:
- **Multiple dummy CControls**: Rejected -- adds complexity, requires managing invisible controls, no codebase precedent.
- **CView with polling**: Rejected -- loses CControl parameter binding, requires custom idle polling, poor UX.
- **Custom IControlListener with both tags**: Rejected -- doesn't integrate with standard CControl tag binding.

### R2: Shared Control Architecture (ViewCreator Pattern)

**Decision**: Implement XYMorphPad as a header-only class in `plugins/shared/src/ui/xy_morph_pad.h` with a ViewCreator registration struct, following the exact pattern of ArcKnob, FieldsetContainer, and StepPatternEditor.

**Rationale**: All three existing shared controls use the same pattern:
1. Class inherits from CControl (or CKnobBase/CViewContainer)
2. ViewCreator struct with `create()`, `apply()`, `getAttributeNames()`, `getAttributeType()`, `getAttributeValue()`
3. Inline global variable for registration (`inline XYMorphPadCreator gXYMorphPadCreator;`)
4. All in `Krate::Plugins` namespace
5. Header included from plugin's entry.cpp to trigger registration

The XYMorphPad will be a header-only control (like ArcKnob and FieldsetContainer). StepPatternEditor is also header-only. The control class inherits from `VSTGUI::CControl` directly (not CKnobBase) because XY pads don't need rotary knob behavior.

**Alternatives considered**:
- **Header + .cpp split**: Possible but inconsistent with existing shared controls (ArcKnob, FieldsetContainer are header-only). StepPatternEditor is also header-only. Keeping header-only maintains consistency.
- **Plugin-local implementation**: Rejected -- spec FR-029 explicitly requires shared location, and forward reusability for other plugins is expected.

### R3: Bilinear Interpolation for Gradient Background

**Decision**: Use composable `lerpColor()` calls from `color_utils.h` to implement bilinear interpolation: two horizontal lerps followed by one vertical lerp. Render as filled rectangles on a configurable `gridSize x gridSize` grid (default 24x24). "Square" refers to equal row/column count; cells stretch to the pad's aspect ratio.

**Rationale**: Bilinear interpolation of 4 corner colors is mathematically:
```
topColor    = lerpColor(topLeft, topRight, tx)
bottomColor = lerpColor(bottomLeft, bottomRight, tx)
cellColor   = lerpColor(bottomColor, topColor, ty)
```
where tx and ty are the normalized cell center positions. This is simpler and more correct than Disrumpo's MorphPad IDW (inverse distance weighting) approach because the XYMorphPad has fixed corner positions, not movable nodes. The existing `lerpColor()` in `color_utils.h` provides the building block. A new `bilinearColor()` convenience function should be added to `color_utils.h` for reuse.

**Performance**: A 24x24 grid = 576 filled rectangles per draw. Each cell requires 3 color lerps + 1 `drawRect()` call. At the target dimensions (200-250px wide), each cell is approximately 8-10px, which is sufficient for smooth gradients. No explicit performance budget is required per spec (SC-004). The grid resolution is configurable via ViewCreator attribute.

**Alternatives considered**:
- **Bitmap pre-rendering**: Rejected -- requires regeneration on resize, adds texture management complexity, unnecessary for 576 rects.
- **IDW (Inverse Distance Weighting)**: Rejected -- overkill for fixed 4-corner case. IDW is for movable nodes (Disrumpo pattern). Bilinear is exact for rectangular grids.
- **GPU shader gradient**: Rejected -- VSTGUI does not expose GPU shaders; CDrawContext is CPU-based.

### R4: Coordinate Conversion with Y-Axis Inversion

**Decision**: Reuse the same coordinate conversion pattern as Disrumpo's MorphPad (`positionToPixel()` / `pixelToPosition()`) with Y-axis inversion: normalized 0.0 maps to pixel bottom, normalized 1.0 maps to pixel top. Include configurable edge padding (default 8px).

**Rationale**: Screen coordinates have Y increasing downward, but the pad's conceptual space has Y=0 (dark/warm) at bottom and Y=1 (bright) at top. The inversion is:
```
pixelY = rect.bottom - padding - normY * innerHeight
normY  = (rect.bottom - padding - pixelY) / innerHeight
```
This is proven correct in `morph_pad.cpp:259-286`. The padding prevents cursor clipping at edges.

**Alternatives considered**:
- **No padding**: Rejected -- cursor circle (16px diameter) would be clipped at edges.
- **Separate utility class**: Premature -- only XYMorphPad and Disrumpo MorphPad use this pattern. Extract after a third consumer appears.

### R5: Modulation Visualization

**Decision**: Implement modulation visualization as a translucent rectangle region centered on the cursor position. X-only modulation shows a horizontal stripe, Y-only shows a vertical stripe, and simultaneous X+Y shows a cross or rectangular region. Use a public API `setModulationRange(float xRange, float yRange)`.

**Rationale**: The spec (FR-026, FR-027, FR-028) requires visual modulation feedback. A 2D region (rectangle/cross) for simultaneous modulation was explicitly confirmed in clarifications. The translucent rectangle approach is simple to implement with `CDrawContext::drawRect()` using alpha-blended fill colors, consistent with ArcKnob's modulation arc pattern.

**Alternatives considered**:
- **Animated ghost trail**: Rejected -- requires timer-based animation, complex state tracking, higher CPU cost.
- **Separate modulation ring (like ArcKnob)**: Not applicable to 2D pad geometry.

### R6: Parameter ID for Tilt (kMixerTiltId)

**Decision**: Add `kMixerTiltId = 302` to Ruinae's `plugin_ids.h` in the Mixer Parameters range (300-399). Register as a `RangeParameter` with range [-12, +12] dB/oct, default 0.0. The normalized value 0.5 maps to 0 dB/oct.

**Rationale**: The Mixer Parameters range (300-399) currently has `kMixerModeId = 300` and `kMixerPositionId = 301`. ID 302 is the next available slot. Using RangeParameter allows direct display of dB/oct values. The parameter needs to be added to:
1. `plugin_ids.h` (enum value)
2. `mixer_params.h` (struct member, handler, registration, save/load)
3. `processor.h/cpp` (atomic + processParameterChanges handler)

**Alternatives considered**:
- **Reuse existing parameter**: No existing tilt parameter in the mixer range.
- **Normalized-only parameter**: Rejected -- RangeParameter provides automatic dB/oct display formatting.

### R7: ViewCreator Attribute Design

**Decision**: The ViewCreator will expose these custom attributes (definitive table also in `contracts/xy_morph_pad_api.md`):

| Attribute | Type | Default | Description |
|-----------|------|---------|-------------|
| `color-bottom-left` | Color | `rgb(48, 84, 120)` | Bottom-left corner gradient color |
| `color-bottom-right` | Color | `rgb(132, 102, 36)` | Bottom-right corner gradient color |
| `color-top-left` | Color | `rgb(80, 140, 200)` | Top-left corner gradient color |
| `color-top-right` | Color | `rgb(220, 170, 60)` | Top-right corner gradient color |
| `cursor-color` | Color | White | Cursor circle and crosshair color |
| `label-color` | Color | `rgb(170, 170, 170)` | Corner and position label color |
| `crosshair-opacity` | Float | 0.12 | Crosshair line opacity [0-1] |
| `grid-size` | Integer | 24 | Gradient grid resolution (square) |
| `secondary-tag` | Tag | -1 | Parameter tag for Y axis (tilt) |

**Rationale**: Following the established ViewCreator patterns from ArcKnob (color + float attributes) and FieldsetContainer (string + float attributes). The `secondary-tag` attribute enables configuration of the Y parameter ID from .uidesc XML without code changes. All 4 corner colors are configurable per FR-031 to support per-plugin customization.

**Alternatives considered**:
- **Hardcoded colors**: Rejected -- FR-031 requires configurable colors.
- **Single "gradient-style" enum**: Rejected -- doesn't support per-plugin customization.

### R8: Controller Integration (verifyView Pattern)

**Decision**: Wire the XYMorphPad in Ruinae's `Controller::verifyView()` using `dynamic_cast<Krate::Plugins::XYMorphPad*>()`, similar to how StepPatternEditor is wired. Set the controller reference and secondary parameter ID, sync initial parameter values, and store a raw pointer for setParamNormalized() forwarding.

**Rationale**: This is the exact pattern used for StepPatternEditor in `controller.cpp:348-416`. The controller needs to:
1. Detect the XYMorphPad via `dynamic_cast` in `verifyView()`
2. Store a raw pointer (`xyMorphPad_`) for parameter sync
3. Configure the secondary parameter ID (kMixerTiltId) and controller reference
4. Sync initial X/Y values from current parameter state
5. Forward `setParamNormalized()` calls for kMixerPositionId and kMixerTiltId to the pad
6. Null the pointer in `willClose()`

**Alternatives considered**:
- **Sub-controller**: Overkill for a single pad instance. Sub-controllers are for template instantiation.
- **IDependent pattern**: Not needed -- the pad is a CControl, so standard parameter sync via verifyView + setParamNormalized is sufficient.

### R9: VSTGUI Event API (4.11+)

**Decision**: Use the VSTGUI 4.11+ Event API (`onMouseDownEvent`, `onMouseMoveEvent`, `onMouseUpEvent`, `onMouseWheelEvent`, `onKeyboardEvent`) for mouse interaction, consistent with Disrumpo's MorphPad.

**Rationale**: Disrumpo's MorphPad already uses this API (`morph_pad.h:200-203`). The Event API provides:
- `event.mousePosition` for cursor position
- `event.modifiers.has(VSTGUI::ModifierKey::Shift)` for modifier detection
- `event.clickCount` for double-click detection
- `event.consumed = true` for event handling
- `event.deltaY`/`event.deltaX` for scroll wheel

For Escape key handling during drag, use `onKeyboardEvent()` which is available in the Event API.

**Alternatives considered**:
- **Legacy onMouseDown/onMouseMoved API**: Works but deprecated-style; inconsistent with MorphPad reference.

## Summary of Decisions

| Topic | Decision |
|-------|----------|
| Dual-parameter pattern | X via tag/setValue, Y via performEdit (same as Disrumpo MorphPad) |
| Shared control location | `plugins/shared/src/ui/xy_morph_pad.h`, header-only, `Krate::Plugins` namespace |
| Gradient rendering | Bilinear interpolation via composable `lerpColor()`, 24x24 default grid |
| Coordinate conversion | Y-inverted, 8px default padding, same as Disrumpo MorphPad |
| Modulation visualization | Translucent rectangle regions, public `setModulationRange(xRange, yRange)` |
| Tilt parameter | `kMixerTiltId = 302`, RangeParameter [-12, +12] dB/oct |
| ViewCreator attributes | 4 corner colors, cursor/label colors, crosshair opacity, grid-size, secondary-tag |
| Controller integration | verifyView pattern (same as StepPatternEditor) |
| Event API | VSTGUI 4.11+ Event API (same as Disrumpo MorphPad) |
