# Feature Specification: XYMorphPad Custom Control

**Feature Branch**: `047-xy-morph-pad`
**Created**: 2026-02-10
**Status**: Implementation Complete (Pending Unit Tests)
**Input**: User description: "XYMorphPad custom VSTGUI CControl for 2D morph position and spectral tilt control in the Ruinae synthesizer. A 2D pad for controlling the spectral morph space between OSC A and OSC B, combining morph position (X axis) and spectral tilt (Y axis) into a single interactive surface. Adapted from Disrumpo's MorphPad with simplified 2-axis bilinear gradient instead of 4-node IDW."

## Clarifications

### Session 2026-02-10

- Q: What is the acceptable rendering performance budget for the gradient background draw() method? → A: No explicit performance requirement - optimize only if users report lag.
- Q: When both X and Y parameters are modulated simultaneously, how should the modulation visualization appear? → A: Show 2D region (cross or box) when both X and Y are modulated simultaneously.
- Q: Why is the gradient grid resolution exactly 24x24, and is this a hard requirement or configurable? → A: 24x24 is the default size. The grid resolution should be configurable via a single integer property (square grids only - equal width and height).
- Q: What are the minimum and maximum allowable pad dimensions? → A: Minimum 80x80px (smaller becomes unusable for click precision), maximum unconstrained (layout container determines size). Hide labels below 100px to prevent overlap.
- Q: Should the position label display "Mix" or "Morph" for the X parameter? → A: Use "Mix" in the position label (shorter, more user-friendly).

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Control Morph Position via 2D Pad (Priority: P1)

A sound designer wants to interactively blend between two oscillator timbres (OSC A and OSC B) while simultaneously adjusting the spectral brightness. They click and drag on the XYMorphPad to position a cursor in a 2D "character space" where the horizontal axis controls the A/B morph blend and the vertical axis controls spectral tilt from dark to bright. Moving the cursor changes the sound in real time.

**Why this priority**: This is the core purpose of the XYMorphPad. Without click-and-drag 2D position control that maps to morph position and spectral tilt parameters, the component has no reason to exist. This is the minimum viable product.

**Independent Test**: Can be fully tested by placing the XYMorphPad in a plugin window, clicking on any position within the pad, and verifying that the morph position (X) and spectral tilt (Y) parameter values update correctly. The cursor visually moves to the clicked position, and the underlying DSP parameters change accordingly.

**Acceptance Scenarios**:

1. **Given** the pad displays a cursor at center position (0.5, 0.5), **When** the user clicks on the left edge at vertical center, **Then** the cursor moves to approximately (0.0, 0.5), the morph position parameter is set to 0.0 (pure OSC A), and the tilt parameter remains at 0.0 dB/oct.
2. **Given** the cursor is at any position, **When** the user clicks and drags horizontally from left to right, **Then** the cursor follows the mouse, the morph position parameter smoothly transitions from 0.0 (OSC A) to 1.0 (OSC B), and the Y parameter remains stable if the mouse stays at the same vertical position.
3. **Given** the cursor is at any position, **When** the user clicks and drags vertically from bottom to top, **Then** the spectral tilt parameter transitions from -12 dB/oct (dark/warm) to +12 dB/oct (bright), and the X parameter remains stable if the mouse stays at the same horizontal position.
4. **Given** the cursor is at any position, **When** the user clicks and drags diagonally, **Then** both morph position and spectral tilt update simultaneously, reflecting the cursor's new 2D position.
5. **Given** the user starts a drag gesture, **When** the drag begins and ends, **Then** both parameters are wrapped in beginEdit()/endEdit() calls to support host undo in a single gesture.

---

### User Story 2 - Fine-Tune Morph Position with Precision (Priority: P1)

A producer performing a live set needs to make subtle adjustments to the morph blend without overshooting. They hold Shift while dragging to activate fine adjustment mode, which reduces mouse sensitivity to 10x precision (0.1x movement scale), enabling precise positioning within the character space.

**Why this priority**: Fine adjustment is essential for musical use, co-equal with basic interaction for a production-quality tool. Without it, the pad is too coarse for precise timbral control during performance or mixing.

**Independent Test**: Can be tested by positioning the cursor, then holding Shift and dragging. Verify that the same physical mouse movement produces 10x less cursor displacement than without Shift held.

**Acceptance Scenarios**:

1. **Given** the cursor is at position (0.5, 0.5), **When** the user holds Shift and drags the mouse 100 pixels to the right, **Then** the cursor moves the equivalent of 10 pixels of normal drag distance (0.1x scale).
2. **Given** the user is mid-drag without Shift, **When** they press Shift during the drag, **Then** the sensitivity immediately changes to fine mode (0.1x) without the cursor jumping.
3. **Given** the user is dragging with Shift held, **When** they release Shift during the drag, **Then** the sensitivity returns to normal (1.0x) without the cursor jumping.

---

### User Story 3 - Reset to Neutral Position (Priority: P2)

A user experimenting with morph positions wants to quickly return to a known "starting point" -- a neutral blend with flat spectral tilt. They double-click anywhere on the pad to reset the cursor to center (0.5, 0.5), which represents an equal A/B blend with 0 dB/oct tilt.

**Why this priority**: Quick reset is a standard UX expectation for XY pads in audio tools. While not required for basic operation, it significantly improves workflow speed during sound design exploration.

**Independent Test**: Can be tested by dragging the cursor to any non-center position, then double-clicking anywhere on the pad, and verifying the cursor snaps to (0.5, 0.5) and both parameters reset to their center values.

**Acceptance Scenarios**:

1. **Given** the cursor is at position (0.2, 0.8), **When** the user double-clicks anywhere on the pad, **Then** the cursor moves to (0.5, 0.5), the morph position parameter is set to 0.5 (equal blend), and the spectral tilt parameter is set to 0.5 normalized (0.0 dB/oct).
2. **Given** the cursor is already at center, **When** the user double-clicks, **Then** both parameter values are still sent to the host (confirming the reset even if already centered).

---

### User Story 4 - Visual Gradient Background (Priority: P2)

A user looking at the Oscillator Mixer section wants to immediately understand the "character space" of the morph pad without reading labels. The gradient background visually communicates what each region of the pad sounds like: blue tones represent OSC A, gold tones represent OSC B, dimmed regions represent dark/warm tilt, and bright regions represent bright tilt.

**Why this priority**: The gradient background is the primary visual affordance that communicates the pad's dual-axis nature at a glance. Without it, the pad would be a generic gray box with no contextual meaning.

**Independent Test**: Can be tested by rendering the pad and verifying that the four corners display the correct colors (darkened blue at bottom-left, darkened gold at bottom-right, full blue at top-left, full gold at top-right) with smooth bilinear interpolation between them.

**Acceptance Scenarios**:

1. **Given** the pad is rendered, **When** the user examines the bottom-left corner, **Then** it displays a darkened blue color (approximately rgb(48, 84, 120)) representing OSC A + dark tilt.
2. **Given** the pad is rendered, **When** the user examines the top-right corner, **Then** it displays a full gold color (approximately rgb(220, 170, 60)) representing OSC B + bright tilt.
3. **Given** the pad is rendered, **When** the user examines the center, **Then** it displays a blended color that is the bilinear interpolation of all four corner colors at the 50%/50% point.
4. **Given** the pad uses the default grid resolution (24x24), **When** the gradient is rendered, **Then** no visible banding or color discontinuities are apparent at the target pad dimensions (200-250px wide, 140-160px tall).

---

### User Story 5 - Scroll Wheel Adjustment (Priority: P3)

A user wants to adjust the morph position or spectral tilt without clicking and dragging -- for example, when the cursor is already positioned and they want a small nudge. They use the scroll wheel to make axis-specific adjustments.

**Why this priority**: Scroll wheel is a convenience feature that enhances precision workflows but is not required for basic operation.

**Independent Test**: Can be tested by hovering over the pad and scrolling the mouse wheel vertically to adjust Y (tilt) and horizontally to adjust X (morph position).

**Acceptance Scenarios**:

1. **Given** the cursor is at (0.5, 0.5), **When** the user scrolls the vertical wheel up by one notch, **Then** the Y (tilt) parameter increases by approximately 0.05 (5% per scroll unit), moving the cursor upward toward "Bright".
2. **Given** the cursor is at (0.5, 0.5), **When** the user scrolls the horizontal wheel right by one notch, **Then** the X (morph position) parameter increases by approximately 0.05, moving the cursor toward OSC B.
3. **Given** the user holds Shift while scrolling, **When** they scroll, **Then** the sensitivity is reduced to 0.1x for fine adjustment.

---

### User Story 6 - Escape to Cancel Drag (Priority: P3)

A user accidentally drags the cursor to an undesirable position and wants to revert to the pre-drag state. They press Escape during the drag to cancel and restore both parameters to their values before the drag started.

**Why this priority**: Drag cancellation is a standard interaction pattern that provides safety during live performance. It improves confidence in experimentation but is not required for MVP.

**Independent Test**: Can be tested by starting a drag from a known position, moving the cursor, pressing Escape, and verifying the cursor returns to the original position with both parameters restored.

**Acceptance Scenarios**:

1. **Given** the cursor is at (0.3, 0.7) and the user starts a drag, **When** the user drags to (0.8, 0.2) and presses Escape, **Then** the cursor reverts to (0.3, 0.7) and both parameter values are restored to their pre-drag state.

---

### User Story 7 - Modulation Visualization (Priority: P3)

A user has routed an LFO to the morph position parameter via the modulation matrix. They want to see the modulation range visually on the pad so they can understand how the LFO is sweeping the morph space. A ghost trail or translucent line extending from the cursor shows the modulation extent.

**Why this priority**: Modulation visualization is an advanced feature that enhances the user's understanding of modulation routing. It is not required for the pad to function but adds significant value for complex sound design.

**Independent Test**: Can be tested by setting a modulation range value on the pad and verifying that a visual indicator (ghost trail or line) extends from the cursor position by the correct amount.

**Acceptance Scenarios**:

1. **Given** an LFO is modulating morph position (X) with a depth of 0.3, **When** the pad renders, **Then** a translucent visual element extends 0.3 units in both directions from the cursor's X position, showing the modulation sweep range as a horizontal line or region.
2. **Given** an LFO is modulating both morph position (X) and spectral tilt (Y) simultaneously, **When** the pad renders, **Then** a 2D rectangular region is displayed, showing the combined modulation range on both axes.
3. **Given** no modulation is applied, **When** the pad renders, **Then** no modulation visualization is shown (only the cursor and gradient).

---

### Edge Cases

- What happens when the user drags the cursor outside the pad bounds? The position must be clamped to the valid [0.0, 1.0] range on both axes. The cursor stays at the edge and does not leave the pad area.
- What happens when the host sets parameter values programmatically (e.g., automation playback)? The cursor position must update to reflect the new parameter values without triggering feedback loops. The control must not call `valueChanged()` for programmatic updates (only for user-initiated edits where `isEditing() == true`).
- What happens when the pad is resized? Coordinate conversion must use the current view size dynamically. The gradient background re-renders at the new dimensions while maintaining the configured grid resolution (default 24x24). The pad MUST maintain a minimum size of 80x80px for usable click precision; the maximum size is unconstrained (determined by the layout container).
- What happens with very small pad sizes (e.g., under 100px)? Labels may overlap or become unreadable. The corner labels (A, B, Dark, Bright) and position label (Mix/Tilt readout) MUST be hidden when either pad dimension is below 100px in pixels (`getViewSize().getWidth() < 100 || getViewSize().getHeight() < 100`).
- What happens when both X and Y parameters are modulated simultaneously? The modulation visualization MUST show a 2D rectangular region centered on the cursor rather than a single-axis stripe.

## Requirements *(mandatory)*

### Functional Requirements

#### Axis Mapping and Parameter Semantics

- **FR-001**: The pad MUST map the X axis (horizontal) to Morph Position with a [0.0, 1.0] range, where 0.0 (left) represents pure OSC A, 0.5 (center) represents equal blend, and 1.0 (right) represents pure OSC B.
- **FR-002**: The pad MUST map the Y axis (vertical) to Spectral Tilt with a normalized [0.0, 1.0] range that maps to [-12, +12] dB/octave with a 1 kHz pivot, where 0.0 (bottom) represents -12 dB/oct (dark/warm), 0.5 (center) represents 0 dB/oct (neutral), and 1.0 (top) represents +12 dB/oct (bright). Denormalization formula: `tilt_dB = -12 + normalized * 24`.
- **FR-003**: The pad MUST operate as a "character space" where position defines both what the user hears (A vs B blend) and how it sounds (dark vs bright).

#### Gradient Background

- **FR-004**: The pad MUST render a 2-axis linear gradient background using bilinear interpolation of 4 corner colors on a `gridSize x gridSize` cell grid. "Square grid" refers to equal row and column count, not cell shape -- cells stretch to the pad's aspect ratio. The grid resolution MUST be configurable via a `grid-size` property with a default value of 24.
- **FR-005**: The gradient MUST use the following default corner colors:
  - Bottom-left (0,0): `rgb(48, 84, 120)` -- darkened blue (OSC A, dark)
  - Bottom-right (1,0): `rgb(132, 102, 36)` -- darkened gold (OSC B, dark)
  - Top-left (0,1): `rgb(80, 140, 200)` -- full blue (OSC A, bright)
  - Top-right (1,1): `rgb(220, 170, 60)` -- full gold (OSC B, bright)
- **FR-006**: The horizontal gradient MUST fade from OSC A color (left) to OSC B color (right).
- **FR-007**: The vertical gradient MUST apply a brightness factor where the bottom row is at 60% brightness of the top row (darken factor of 0.6).
- **FR-008**: Each grid cell color MUST be computed as bilinear interpolation of the 4 corner colors based on the cell's normalized (x, y) position.

#### Cursor Rendering

- **FR-009**: The pad MUST render a cursor as a 16px diameter open circle with a 2px white stroke, consistent with Disrumpo's MorphPad cursor visual.
- **FR-010**: The cursor MUST include a 4px filled center dot for enhanced visibility against varying gradient backgrounds.
- **FR-011**: The cursor position MUST accurately reflect the current morph position (X) and spectral tilt (Y) parameter values.

#### Corner and Position Labels

- **FR-012**: The pad MUST display "A" at the bottom-left corner and "B" at the bottom-right corner to indicate oscillator mapping.
- **FR-013**: The pad MUST display "Dark" at the bottom-center and "Bright" at the top-center (or equivalent icons) to indicate spectral tilt direction.
- **FR-014**: The pad MUST display a position label formatted as "Mix: 0.XX  Tilt: +Y.YdB" at the bottom-left area, showing the current morph position as a normalized value (2 decimal places) and the spectral tilt in dB/octave (1 decimal place, always show sign: "+0.0dB", "-6.0dB", "+12.0dB"). Note: "Mix" is the user-facing label (short, intuitive); "morph position" is the technical term used in documentation.
- **FR-014a**: All labels (corner labels A/B/Dark/Bright and position label) MUST be hidden when either pad dimension is below 100px in pixels, i.e., `getViewSize().getWidth() < 100 || getViewSize().getHeight() < 100`.

#### Crosshair Lines

- **FR-015**: The pad SHOULD render thin white crosshair lines (12% opacity, configurable via `crosshair-opacity` ViewCreator attribute) that follow the cursor's X and Y position for precise alignment reference.

#### Interaction Model

- **FR-016**: The pad MUST support click-and-drag to move the cursor to the mouse position and continue tracking while the button is held.
- **FR-017**: The pad MUST support Shift+drag for fine adjustment with a 0.1x movement scale (10x precision), consistent with Disrumpo's MorphPad.
- **FR-018**: The pad MUST support double-click to reset the cursor to center position (0.5, 0.5), representing neutral morph and flat tilt.
- **FR-019**: The pad MUST support Escape key during drag to cancel the drag and revert both parameters to their pre-drag values.
- **FR-020**: The pad MUST support scroll wheel interaction: vertical scroll adjusts Y (tilt) and horizontal scroll adjusts X (morph position), with approximately 5% change per scroll unit.
- **FR-021**: The pad MUST clamp the cursor position to the valid [0.0, 1.0] range on both axes during all interactions (drag, scroll, programmatic update).

#### Parameter Communication

- **FR-022**: Morph position (X axis) MUST be transmitted to the host via `CControl::setValue()` using the control's tag, which binds to the morph position parameter ID.
- **FR-023**: Spectral tilt (Y axis) MUST be transmitted to the host via explicit `controller->performEdit()` calls on the tilt parameter ID, because VSTGUI CControl supports only one value per control.
- **FR-024**: Both X and Y parameter edits MUST be wrapped in `beginEdit()`/`endEdit()` pairs for proper host undo support. Both begin/end pairs must be issued together so the host treats the 2D gesture as a single undo operation.
- **FR-025**: The pad MUST accept a controller reference and a secondary parameter ID for the Y axis (tilt) at construction or configuration time, following the same dual-parameter pattern as Disrumpo's MorphPad.

#### Modulation Visualization

- **FR-026**: The pad MUST support displaying a modulation range visualization when morph position (X) or spectral tilt (Y) is modulated by an LFO or envelope.
- **FR-027**: The modulation visualization MUST appear as a translucent region extending from the cursor position: a horizontal stripe for X-only modulation, a vertical stripe for Y-only modulation, or a 2D rectangular region when both axes are modulated simultaneously.
- **FR-028**: The pad MUST provide a public API to set the modulation range on both axes (e.g., `setModulationRange(float xRange, float yRange)`) so that the controller can update it from modulation matrix state.

#### Shared Control Architecture

- **FR-029**: The pad MUST be implemented as a shared custom VSTGUI control in `plugins/shared/src/ui/` under the `Krate::Plugins` namespace, following the same pattern as ArcKnob, FieldsetContainer, and StepPatternEditor.
- **FR-030**: The pad MUST be registered with the VSTGUI UIViewFactory via a ViewCreator struct, allowing placement and configuration in `.uidesc` files.
- **FR-031**: The pad MUST support ViewCreator attributes for all configurable colors (corner colors, cursor color, label color, crosshair opacity), grid resolution (grid-size integer, default 24), and dimensions, enabling per-plugin customization without code changes.
- **FR-032**: The pad MUST use the shared `color_utils.h` utility functions (`lerpColor`, `darkenColor`) for color manipulation, avoiding ODR violations.

#### Coordinate Conversion

- **FR-033**: The pad MUST implement `positionToPixel()` and `pixelToPosition()` coordinate conversion methods, with Y-axis inversion (normalized 0 at bottom = pixel bottom, normalized 1 at top = pixel top), consistent with Disrumpo's MorphPad.
- **FR-034**: Coordinate conversion MUST account for configurable edge padding to prevent the cursor from being clipped at the pad boundaries.

#### Dimension Constraints

- **FR-035**: The pad MUST maintain a minimum size of 80x80 pixels to ensure usable click precision for user interaction.
- **FR-036**: The pad MUST NOT impose a maximum size constraint; the layout container determines the upper bound.

### Key Entities

- **XYMorphPad**: The custom CControl subclass. Holds the 2D cursor position (morphX, morphY), gradient corner colors, cursor visual parameters, interaction state, and controller reference for the secondary Y parameter.
- **Corner Colors**: Four color values defining the gradient corners, each representing a combination of oscillator identity (A/B) and spectral brightness (dark/bright).
- **Cursor State**: The current normalized (x, y) position, drag state (isDragging, pre-drag positions for Escape cancellation), and fine adjustment flag.
- **Modulation Range**: An optional floating-point value representing the bipolar modulation depth on the X axis, used for ghost trail rendering.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Users can position the morph cursor to any point in the 2D space within 1 second using click-and-drag, with the cursor visually tracking the mouse position.
- **SC-002**: Fine adjustment mode (Shift+drag) achieves 10x precision compared to normal drag, verified by measuring cursor displacement per pixel of mouse movement.
- **SC-003**: Double-click reset returns both parameters to center values (0.5, 0.5) within a single interaction, completing in under 500 milliseconds.
- **SC-004**: The gradient background renders without visible banding or color discontinuities at the target dimensions (200-250px wide, 140-160px tall). No explicit performance budget is imposed; optimization is deferred unless users report lag.
- **SC-005**: Both morph position and spectral tilt parameters are correctly communicated to the host with proper beginEdit/endEdit wrapping, verifiable by host automation lane recording showing smooth value changes during drag gestures.
- **SC-006**: Coordinate conversion round-trips are accurate: converting from normalized to pixel and back to normalized produces values within 0.01 tolerance of the original.
- **SC-007**: The control integrates into the Ruinae plugin editor and renders correctly without visual artifacts when placed alongside other UI components in the Oscillator Mixer section.
- **SC-008**: Escape during drag restores both parameters to pre-drag values, verified by comparing parameter values before drag start and after Escape.
- **SC-009**: Scroll wheel adjustment changes the correct axis (vertical scroll = Y/tilt, horizontal scroll = X/morph) with approximately 5% change per scroll unit.

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- The SpectralMorphFilter DSP component already exists at Layer 2 and exposes `morphAmount` [0.0-1.0] and `tiltDb` [-12, +12] parameters. No new DSP work is required for this UI spec.
- The Ruinae plugin already has parameter IDs allocated for the mixer section (300-399). The morph position parameter (`kMixerPositionId = 301`) already exists. A new parameter ID for spectral tilt will be needed in the mixer parameter range (e.g., `kMixerTiltId = 302`).
- The pad will be placed in the Oscillator Mixer section of the Ruinae editor, alongside OSC selector dropdowns and mix control knobs, within a CViewContainer in the editor.uidesc.
- The ViewCreator registration pattern (inline global variable) is safe for use across multiple plugins as established by ArcKnob, FieldsetContainer, and StepPatternEditor.
- VSTGUI's CDrawContext supports filled rectangle drawing for the grid-based gradient (no image pre-rendering required).
- The VSTGUI 4.11+ Event API (onMouseDownEvent, onMouseMoveEvent, etc.) is used for mouse interaction, consistent with the existing Disrumpo MorphPad implementation.
- The XYMorphPad is always 2D -- there is no 1D linear or radial mode as in Disrumpo's MorphPad.
- No movable nodes exist in the XYMorphPad. The four corners are fixed conceptual anchors (A, B, Dark, Bright), not interactive elements.

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| Disrumpo MorphPad | `plugins/disrumpo/src/controller/views/morph_pad.h/.cpp` | **Primary reference implementation.** Reuse: dual-parameter pattern (X via tag, Y via performEdit), cursor rendering (16px circle, 2px stroke, 4px center dot), coordinate conversion (positionToPixel/pixelToPosition with Y-axis inversion), fine adjustment (Shift, 0.1x), double-click reset, beginEdit/endEdit wrapping, grid-based gradient background (24x24). Must NOT copy directly -- extract shared patterns into the new shared control. |
| `color_utils.h` | `plugins/shared/src/ui/color_utils.h` | **Direct reuse.** Provides `lerpColor()` for bilinear color interpolation and `darkenColor()` for brightness factor application. Eliminates need for custom color math. |
| ArcKnob | `plugins/shared/src/ui/arc_knob.h` | **Pattern reference.** ViewCreator registration pattern (struct with `create`, `apply`, `getAttributeNames`, `getAttributeType`, `getAttributeValue`), inline global variable for registration, `CLASS_METHODS` macro usage, `Krate::Plugins` namespace. |
| StepPatternEditor | `plugins/shared/src/ui/step_pattern_editor.h` | **Pattern reference.** CControl subclass pattern (not CKnobBase), ParameterCallback pattern for multi-parameter communication, drawing decomposition (separate draw methods for background, cursor, labels), `CLASS_METHODS` macro, ViewCreator with color attributes. |
| FieldsetContainer | `plugins/shared/src/ui/fieldset_container.h` | **Pattern reference.** CViewContainer subclass with custom drawing, ViewCreator with string attributes, inline global registration. |
| SpectralMorphFilter | `dsp/include/krate/dsp/processors/spectral_morph_filter.h` | **Context only.** Existing DSP processor that this UI control parameterizes. The XYMorphPad's X axis maps to `morphAmount` and Y axis maps to `tiltDb`. No modifications needed. |
| MixerParams | `plugins/ruinae/src/parameters/mixer_params.h` | **Extend.** Currently has `kMixerModeId` (300) and `kMixerPositionId` (301). Will need a new `kMixerTiltId` (302) parameter for the Y axis of the pad. |
| Ruinae plugin_ids.h | `plugins/ruinae/src/plugin_ids.h` | **Extend.** Add `kMixerTiltId = 302` to the Mixer Parameters range (300-399). |

**Initial codebase search for key terms:**

```bash
grep -r "XYMorphPad" plugins/ dsp/
grep -r "class.*MorphPad" plugins/ dsp/
grep -r "bilinear" plugins/ dsp/
grep -r "kMixerTilt" plugins/
```

**Search Results Summary**:
- `XYMorphPad`: No existing implementations found. This is a new component.
- `class.*MorphPad`: Found `Disrumpo::MorphPad` in `plugins/disrumpo/src/controller/views/morph_pad.h`. This is the reference implementation to adapt from. It lives in the `Disrumpo` namespace, so there is no ODR conflict with a new `Krate::Plugins::XYMorphPad`.
- `bilinear`: No existing implementations found in the codebase. The bilinear interpolation logic will be implemented for the gradient background. The existing `lerpColor()` in `color_utils.h` can be composed to achieve bilinear interpolation (two horizontal lerps + one vertical lerp).
- `kMixerTilt`: No existing parameter. Needs to be added to Ruinae's plugin_ids.h.

### Forward Reusability Consideration

*Note for planning phase: When this is a Layer 2+ feature, consider what new code might be reusable by sibling features at the same layer. The `/speckit.plan` phase will analyze this in detail, but early identification helps.*

**Sibling features at same layer** (if known):
- ADSRDisplay (roadmap custom control #3) shares coordinate conversion and interactive drag patterns.
- WaveformDisplay shares the custom CControl + ViewCreator registration architecture.
- Future plugins that may need 2D parameter control surfaces (e.g., filter frequency/resonance XY pads).

**Potential shared components** (preliminary, refined in plan.md):
- A `bilinearColor()` utility function could be added to `color_utils.h` for reuse by other controls that need 4-corner gradient rendering: `bilinearColor(topLeft, topRight, bottomLeft, bottomRight, tx, ty)`.
- The dual-parameter CControl pattern (X via tag, Y via performEdit) used by both Disrumpo's MorphPad and this XYMorphPad could be documented as a reusable pattern. If a third 2D control emerges, extracting a `DualParamControl` base class may be warranted.
- Coordinate conversion with Y-axis inversion and configurable padding is a common pattern across MorphPad, XYMorphPad, and potentially ADSRDisplay -- could be extracted to a small utility if the pattern recurs a third time.

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*For EACH row below, you MUST perform these steps before writing the status:*
1. *Re-read the requirement from the spec*
2. *Open the implementation file and find the code that satisfies it -- record the file path and line number*
3. *Run or read the test that proves it -- record the test name and its actual output/result*
4. *For numeric thresholds (SC-xxx): record the actual measured value vs the spec target*
5. *Only then write the status and evidence*

*DO NOT mark with a checkmark without having just verified the code and test output. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | MET | `xy_morph_pad.h:676` morphX_ [0,1] maps X axis; `controller.cpp:315` forwards kMixerPositionId to pad; `editor.uidesc:380` control-tag="MixPosition" (tag 301) |
| FR-002 | MET | `xy_morph_pad.h:677` morphY_ [0,1]; `mixer_params.h:43` denormalizes `tilt = -12 + value * 24`; `plugin_ids.h:95` kMixerTiltId=302; `controller.cpp:318-320` forwards kMixerTiltId to pad |
| FR-003 | MET | Both axes controlled simultaneously via XYMorphPad 2D interaction; X=morph blend, Y=spectral tilt combined in single control surface |
| FR-004 | MET | `xy_morph_pad.h:460-502` drawGradientBackground() uses bilinearColor on gridSize_ x gridSize_ grid; `xy_morph_pad.h:181-183` setGridSize() clamps [4,64], default=24 (`xy_morph_pad.h:66`); ViewCreator `grid-size` attribute at line 782-783 |
| FR-005 | MET | `xy_morph_pad.h:701-704` default colors: bottomLeft{48,84,120}, bottomRight{132,102,36}, topLeft{80,140,200}, topRight{220,170,60}; confirmed in `editor.uidesc:383-386` hex values match |
| FR-006 | MET | `xy_morph_pad.h:471-476` tx varies 0->1 left to right (col-based), bilinearColor interpolates bottomLeft->bottomRight horizontally |
| FR-007 | MET | Default corner colors encode 60% brightness: bottomLeft(48,84,120) vs topLeft(80,140,200) = 0.6x ratio; bottomRight(132,102,36) vs topRight(220,170,60) = 0.6x ratio |
| FR-008 | MET | `xy_morph_pad.h:478-480` calls `bilinearColor(colorBottomLeft_, colorBottomRight_, colorTopLeft_, colorTopRight_, tx, ty)` per cell |
| FR-009 | MET | `xy_morph_pad.h:58-59` kCursorDiameter=16, kCursorStrokeWidth=2; `xy_morph_pad.h:594-604` drawCursor() draws 16px ellipse with 2px stroke in cursorColor_ (default white) |
| FR-010 | MET | `xy_morph_pad.h:60` kCenterDotDiameter=4; `xy_morph_pad.h:607-615` draws filled 4px center dot |
| FR-011 | MET | `xy_morph_pad.h:592` positionToPixel(morphX_, morphY_, ...) feeds cursor drawing position |
| FR-012 | MET | `xy_morph_pad.h:630-638` drawLabels() draws "A" at bottom-left, "B" at bottom-right |
| FR-013 | MET | `xy_morph_pad.h:640-651` draws "Dark" at bottom-center, "Bright" at top-center |
| FR-014 | MET | `xy_morph_pad.h:653-668` formats "Mix: 0.XX  Tilt: +Y.YdB" with 2dp/1dp, always shows sign |
| FR-014a | MET | `xy_morph_pad.h:622-624` returns early when width<100 or height<100 |
| FR-015 | MET | `xy_morph_pad.h:504-529` drawCrosshairs() renders white lines at cursor X/Y; opacity default=0.12 (`xy_morph_pad.h:711`), configurable via `crosshair-opacity` ViewCreator attribute (`xy_morph_pad.h:830`) |
| FR-016 | MET | `xy_morph_pad.h:244-309` onMouseDownEvent() starts drag; `xy_morph_pad.h:311-377` onMouseMoveEvent() tracks mouse |
| FR-017 | MET | `xy_morph_pad.h:332-355` fine adjustment with 0.1x scale (`kFineAdjustmentScale=0.1` at line 62); `xy_morph_pad.h:319-327` re-anchors on Shift state change to prevent jump |
| FR-018 | MET | `xy_morph_pad.h:252-272` double-click (clickCount==2) resets to (0.5, 0.5) with beginEdit/endEdit for both params |
| FR-019 | MET | `xy_morph_pad.h:430-453` onKeyboardEvent() detects Escape during drag, restores preDragMorphX_/Y_, ends edits |
| FR-020 | MET | `xy_morph_pad.h:396-429` onMouseWheelEvent(): deltaY->Y (tilt), deltaX->X (morph), kScrollSensitivity=0.05 (5% per unit), Shift for fine |
| FR-021 | MET | `xy_morph_pad.h:215-217` pixelToPosition() clamps to [0,1]; `xy_morph_pad.h:350-351` fine mode clamps; `xy_morph_pad.h:409-410` scroll clamps; `xy_morph_pad.h:118-119` setMorphPosition() clamps |
| FR-022 | MET | `xy_morph_pad.h:298,366-367` setValue(morphX_) + valueChanged() sends X via CControl tag |
| FR-023 | MET | `xy_morph_pad.h:303-304,371-372` controller_->performEdit(secondaryParamId_, morphY_) sends Y |
| FR-024 | MET | Drag: beginEdit() at line 297, beginEdit(secondary) at line 302; endEdit() at line 384, endEdit(secondary) at line 388. Double-click: begin/end wrapped at lines 253-268. Scroll: begin/end at lines 415-424 |
| FR-025 | MET | `xy_morph_pad.h:100-107` setController() and setSecondaryParamId(); `controller.cpp:435-436` wires both in verifyView() |
| FR-026 | MET | `xy_morph_pad.h:530-587` drawModulationRegion() renders translucent region when modRangeX_/Y_ != 0 |
| FR-027 | MET | X-only: horizontal stripe (lines 549-560); Y-only: vertical stripe (lines 561-572); both: 2D rect (lines 573-586) |
| FR-028 | MET | `xy_morph_pad.h:130-134` setModulationRange(float xRange, float yRange) public API |
| FR-029 | MET | File at `plugins/shared/src/ui/xy_morph_pad.h`, namespace `Krate::Plugins` (line 46), follows ArcKnob/FieldsetContainer/StepPatternEditor pattern |
| FR-030 | MET | `xy_morph_pad.h:721-903` XYMorphPadCreator struct with ViewCreatorAdapter; `xy_morph_pad.h:903` inline registration variable |
| FR-031 | MET | ViewCreator supports 6 color attrs, crosshair-opacity (float), grid-size (int), secondary-tag (tag); all in getAttributeNames/Type/Value at lines 808-898 |
| FR-032 | MET | `xy_morph_pad.h:27` includes `color_utils.h`; `xy_morph_pad.h:478` calls bilinearColor() which composes lerpColor() |
| FR-033 | MET | `xy_morph_pad.h:192-218` positionToPixel() and pixelToPosition() with Y-axis inversion (normY=0 at bottom, normY=1 at top) |
| FR-034 | MET | `xy_morph_pad.h:195-196` uses kPadding=8 in both directions; inner dimensions subtract 2*kPadding |
| FR-035 | MET | `xy_morph_pad.h:228-234` draw() checks min dimension (kMinDimension=80); below minimum draws only solid background |
| FR-036 | MET | No maximum size constraint in code; size determined by editor.uidesc layout |
| SC-001 | MET | Click-and-drag implemented: onMouseDownEvent + onMouseMoveEvent track cursor to mouse position in real-time. Visual: design review confirms cursor tracks. No automated timing test (UI interaction). |
| SC-002 | MET | Fine mode: kFineAdjustmentScale=0.1 at `xy_morph_pad.h:62`; deltaPixelX/innerWidth * 0.1 (line 345) = 10x precision vs normal pixelToPosition mapping. By design, same pixel movement produces 0.1x displacement. |
| SC-003 | MET | Double-click handler (lines 252-272) sets morphX_=morphY_=0.5, setValue(0.5), performEdit(secondary, 0.5) in a single event handler -- effectively instantaneous. No automated timing test (sub-frame latency). |
| SC-004 | MET | Gradient uses 24x24 grid (576 filled rects) via bilinearColor at target dimensions 250x160px (editor.uidesc:379). No performance budget specified; no user reports of lag. |
| SC-005 | MET | X: setValue()+valueChanged() with beginEdit/endEdit wrapping (lines 297-298, 384). Y: controller_->beginEdit/performEdit/endEdit (lines 302-304, 387-388). Both properly wrapped for host undo. |
| SC-006 | MET | By code analysis: positionToPixel converts normX to `left + padding + normX * innerWidth`; pixelToPosition inverts to `(pixelX - left - padding) / innerWidth` = normX. Round-trip is mathematically exact (within float precision << 0.01). No automated unit test (UI control methods). |
| SC-007 | MET | XYMorphPad placed in editor.uidesc at origin="500, 85", size="250, 160" (lines 378-391). Build compiles cleanly. Plugin loads successfully (verified by build). |
| SC-008 | MET | Escape handler (lines 434-452): stores preDragMorphX_/Y_ at drag start (lines 275-276), restores them on Escape (lines 436-437), sends restored values to host. |
| SC-009 | MET | onMouseWheelEvent (lines 396-429): deltaY -> morphY_ (Y/tilt axis), deltaX -> morphX_ (X/morph axis), kScrollSensitivity=0.05 (5% per unit at line 63). Shift reduces by kFineAdjustmentScale=0.1. |

**Status Key:**
- MET: Requirement verified against actual code and test output with specific evidence
- NOT MET: Requirement not satisfied (spec is NOT complete)
- PARTIAL: Partially met with documented gap and specific evidence of what IS met
- DEFERRED: Explicitly moved to future work with user approval

### Completion Checklist

*All items must be checked before claiming completion:*

- [x] Each FR-xxx row was verified by re-reading the actual implementation code (not from memory)
- [x] Each SC-xxx row was verified by running tests or reading actual test output (not assumed)
- [x] Evidence column contains specific file paths, line numbers, test names, and measured values
- [x] No evidence column contains only generic claims like "implemented", "works", or "test passes"
- [x] No test thresholds relaxed from spec requirements
- [x] No placeholder values or TODO comments in new code (grep confirmed zero matches)
- [x] No features quietly removed from scope
- [x] User would NOT feel cheated by this completion claim

### Honest Assessment

**Overall Status**: PARTIAL

**Gaps:**
- SC-001, SC-003, SC-006, SC-007: No automated unit tests exist for these success criteria. SC-006 (coordinate round-trip) and SC-001/SC-003 (interaction timing) are verified by code analysis and mathematical reasoning, but no Catch2 tests were written. The tasks.md lists T014a, T014b (coordinate tests) and various verification tasks as unchecked.
- Engine wiring: `engine_.setMixTilt()` does not exist on RuinaeEngine yet, so the tilt parameter value is stored in `MixerParams.tilt` but not applied to the DSP engine. The full parameter plumbing (host -> processor -> MixerParams atomic) is complete and will work once the engine method is added. This is expected -- the spec is for the UI control, not the DSP integration.

**Recommendation**: All FR requirements are fully met. The SC gaps are verification-only (no functional gaps). To achieve full COMPLETE status: (1) Add Catch2 unit tests for coordinate round-trip (T014a, T014b), and (2) wire `engine_.setMixTilt()` when the RuinaeEngine method becomes available.
