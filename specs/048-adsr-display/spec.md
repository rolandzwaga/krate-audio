# Feature Specification: ADSRDisplay Custom Control

**Feature Branch**: `048-adsr-display`
**Created**: 2026-02-10
**Status**: Draft
**Input**: User description: "Interactive ADSR envelope editor with per-segment curve shaping for the Ruinae synthesizer. Supports a simple drag-to-bend mode (continuous curve parameter) and a pro Bezier mode for S-curves and overshoots. Used for all three voice envelopes (Amp, Filter, Mod). Implements real-time playback visualization, logarithmic time axis auto-scaling, and full parameter communication via the VST3 parameter system."

## Clarifications

### Session 2026-02-10

- Q: When the user tries to drag a curve segment to adjust the curve amount (FR-016), how should the control distinguish between "clicking on the curve line" versus "clicking on a control point" when the cursor is near both? → A: Always prioritize control points; curves only draggable in the "middle third" of each segment, avoiding the endpoints where control points live.
- Q: FR-009 specifies logarithmic time axis scaling, and FR-010 guarantees that each segment occupies at least 15% of the display width. When the user creates an envelope with wildly unbalanced segment times (e.g., attack=0.1ms, decay=10ms, release=10000ms), how should the logarithmic scaling be constrained? → A: Pure logarithmic scaling with clamping to ensure 15% minimum per segment (log scale first, then clamp).
- Q: FR-005 requires a dashed horizontal line showing the "sustain hold period" from the Sustain point to the release start. However, the ADSR model doesn't include a fixed sustain duration parameter — sustain is held indefinitely while the gate is on. How should the display determine the visual length of this hold segment? → A: Use a fixed proportion of total display width (e.g., 25%) for the sustain hold segment.
- Q: FR-032 requires approximating a Bezier curve as a single curve amount value when switching from Bezier to Simple mode. For curves that are simple monotonic shapes (no S-curves or overshoots), what algorithm should be used to find the best-fit curve amount? → A: When converting a Bezier envelope segment to Simple mode, the system SHALL sample the Bezier curve at 50% of the segment's normalized phase and derive the Simple curve amount that produces the same value at that phase. This conversion is lossy and deterministic.
- Q: FR-036 specifies a timer-based refresh at ~30fps for the playback dot visualization. FR-037 states that when multiple voices are playing, only the most recently triggered voice's position is shown. How should the controller determine which voice is "most recent" when voices are finishing and new ones start? → A: Track note-on timestamps and always display the voice with the most recent note-on time (most recently triggered active voice).

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Drag Control Points to Shape Envelope (Priority: P1)

A sound designer working on a synthesizer patch wants to visually adjust the ADSR envelope shape for the amp envelope. They see the envelope curve displayed as a filled area with control points at the Peak, Sustain, and End positions. They click and drag the Peak point horizontally to adjust the attack time, drag the Sustain point both horizontally (decay time) and vertically (sustain level), and drag the End point horizontally to adjust the release time. The envelope curve updates in real-time as they drag, and the corresponding knobs below the display stay synchronized. This is the minimum viable product -- a visual, interactive envelope editor that replaces the need to manually tweak individual knobs.

**Why this priority**: Without interactive control point dragging, the ADSRDisplay is just a passive visualizer with no reason to exist as a custom control. This is the core interaction that makes the component useful.

**Independent Test**: Can be fully tested by placing the ADSRDisplay in a plugin window, clicking and dragging each control point, and verifying that the corresponding ADSR parameter values update correctly. The curve redraws to reflect the new shape and the knobs below synchronize.

**Acceptance Scenarios**:

1. **Given** the display shows an envelope with attack=10ms, decay=50ms, sustain=0.5, release=100ms, **When** the user drags the Peak point to the right, **Then** the attack time increases (e.g., to 50ms), the attack knob updates to reflect the new value, and the curve redraws showing a longer attack segment.
2. **Given** the display shows the envelope, **When** the user drags the Sustain point downward, **Then** the sustain level decreases (e.g., from 0.5 to 0.3), the Sustain knob updates, and the curve redraws with a lower sustain plateau.
3. **Given** the display shows the envelope, **When** the user drags the Sustain point to the right, **Then** the decay time increases, the Decay knob updates, and the decay segment widens visually.
4. **Given** the display shows the envelope, **When** the user drags the End point to the right, **Then** the release time increases, the Release knob updates, and the release segment widens.
5. **Given** the user begins a drag on any control point, **When** the drag starts and ends, **Then** the parameter edits are wrapped in beginEdit()/endEdit() pairs so the host can treat the gesture as a single undo operation.

---

### User Story 2 - Envelope Curve Rendering and Visual Feedback (Priority: P1)

A user glances at the Envelope section and immediately understands the shape and timing of each envelope from the visual representation. The envelope curve is drawn as a filled area with a gradient fill matching the envelope's identity color (blue for Amp, gold for Filter, purple for Mod), with a brighter stroke on top. Grid lines provide visual reference for level positions (25%, 50%, 75%). A dashed horizontal line shows the sustain hold period, and a vertical dashed gate-off marker separates the gate-on and gate-off sections. Time labels at control points show the actual timing values (e.g., "10ms", "50ms").

**Why this priority**: The visual rendering is co-equal with interaction because without clear visual feedback, the user cannot understand what they are editing. The envelope display must be readable at a glance.

**Independent Test**: Can be tested by rendering the ADSRDisplay with known ADSR values and verifying that the curve, fill, grid lines, time labels, sustain hold line, and gate marker render correctly at the specified dimensions.

**Acceptance Scenarios**:

1. **Given** an ADSRDisplay configured for the Amp envelope, **When** rendered, **Then** the filled area uses rgba(80,140,200,0.3) and the stroke uses rgb(80,140,200).
2. **Given** the envelope has sustain=0.5, **When** rendered, **Then** a dashed horizontal line extends from the Sustain point to the release start at the 50% level.
3. **Given** the envelope has attack=10ms, decay=50ms, release=100ms, **When** rendered, **Then** time labels "10ms", "50ms", "100ms" appear near the corresponding control points.
4. **Given** the display renders the full ADSR shape, **When** the time axis auto-scales, **Then** both short segments (e.g., 1ms attack) and long segments (e.g., 5s release) are comfortably visible due to logarithmic time scaling.
5. **Given** any segment, **When** rendered, **Then** the segment occupies at least 15% of the display width regardless of its time proportion, ensuring all control points remain grabbable.

---

### User Story 3 - Drag Curves to Adjust Curve Shape (Priority: P2)

A sound designer wants to give the attack a logarithmic feel (fast start, slow end) without switching to Bezier mode. They click directly on the attack curve line (not on a control point) and drag upward. The curve bends in real-time, showing the logarithmic curvature. A tooltip displays the current curve value (e.g., "Curve: -0.35"). This provides continuous curve control that replaces the old 3-value discrete enum (Exponential/Linear/Logarithmic) with a smooth [-1, +1] parameter.

**Why this priority**: Curve shaping is the primary differentiator from a simple ADSR display. It turns the display from a parameter visualizer into a full-featured envelope editor. Without it, users must rely on hidden parameters or external controls.

**Independent Test**: Can be tested by clicking on a curve segment and dragging up/down, verifying that the curve amount parameter changes continuously and the curve visually bends accordingly.

**Acceptance Scenarios**:

1. **Given** the attack segment has curve=0.0 (linear), **When** the user clicks on the attack curve and drags upward, **Then** the curve amount moves toward -1.0 (logarithmic), the curve visually bends to show fast start/slow end, and the attack curve parameter updates.
2. **Given** the decay segment has curve=0.0, **When** the user clicks on the decay curve and drags downward, **Then** the curve amount moves toward +1.0 (exponential), showing slow start/fast end.
3. **Given** any segment's curve has been modified, **When** the user double-clicks on the curve, **Then** the curve resets to linear (0.0).

---

### User Story 4 - Fine Adjustment and Reset Interactions (Priority: P2)

A producer performing precise sound design needs fine control over envelope timing. They hold Shift while dragging a control point to get 10x precision (0.1x movement scale). They double-click a control point to reset it to its default value (attack=10ms, decay=50ms, sustain=0.5, release=100ms). They press Escape during a drag to cancel and revert to pre-drag values.

**Why this priority**: Fine adjustment, reset, and cancel are standard interaction patterns that make the control production-ready. Without them, precise adjustments are frustrating and mistakes are irreversible within a gesture.

**Independent Test**: Can be tested independently by performing Shift+drag, double-click, and Escape interactions and verifying the expected behavior for each.

**Acceptance Scenarios**:

1. **Given** the user is dragging the Peak point, **When** they hold Shift, **Then** the movement sensitivity reduces to 0.1x (10x precision) without the point jumping.
2. **Given** the Sustain point is at level 0.3, **When** the user double-clicks it, **Then** the sustain level resets to 0.5 (default) and the decay time resets to 50ms (default).
3. **Given** the user is mid-drag on the End point and has moved it significantly, **When** they press Escape, **Then** the release time reverts to its pre-drag value and the display redraws to the original state.

---

### User Story 5 - Bezier Mode for Advanced Curve Shaping (Priority: P3)

An advanced sound designer wants to create an S-curve on the decay segment or an overshoot on the attack. They click the [S]/[B] toggle in the top-right corner of the display to switch to Bezier mode. Two diamond-shaped handles appear on each segment, connected to the segment endpoints by thin gray lines. They drag these handles to sculpt cubic Bezier curves, enabling shapes that are impossible with simple curve bending (S-curves, overshoots). Switching back to Simple mode approximates the Bezier curve as a single curve amount value.

**Why this priority**: Bezier mode is a pro feature that adds expressive depth for advanced users. The display is fully functional without it (Simple mode covers most use cases), making this a P3 enhancement.

**Independent Test**: Can be tested by toggling to Bezier mode, verifying handles appear, dragging them to create S-curves and overshoots, then toggling back and verifying the best-fit approximation.

**Acceptance Scenarios**:

1. **Given** the display is in Simple mode, **When** the user clicks the [S] toggle, **Then** the toggle shows [B], and 2 diamond-shaped Bezier handles appear on each segment connected by thin gray lines to the segment endpoints.
2. **Given** Bezier mode is active on the decay segment, **When** the user crosses the two handles, **Then** an S-curve forms on the decay segment.
3. **Given** Bezier mode is active and the user has created S-curves, **When** they switch back to Simple mode, **Then** a confirmation prompt appears (since S-curves lose fidelity), and if confirmed, the Bezier curve is approximated as a single curve amount value.
4. **Given** Simple mode is active with a non-zero curve amount, **When** the user switches to Bezier mode, **Then** the Bezier handles are positioned to reproduce the same curve shape.

---

### User Story 6 - Real-Time Playback Visualization (Priority: P3)

A user plays a note on their MIDI keyboard and watches the ADSRDisplay. A bright dot travels along the envelope curve, showing the current position of the envelope in real-time. When the note is held, the dot moves through Attack, Decay, and rests at Sustain. When the note is released, the dot travels through the Release phase and disappears. This gives immediate visual feedback about the envelope's behavior during performance.

**Why this priority**: Playback visualization is a polishing feature that enhances the user experience but is not required for the control's editing functionality. The display works fully without it.

**Independent Test**: Can be tested by triggering notes and verifying that the playback dot appears, moves along the correct curve path at the correct rate, and disappears when the envelope reaches idle.

**Acceptance Scenarios**:

1. **Given** a note is playing and the envelope is in the Attack stage, **When** the display renders, **Then** a bright 6px dot is visible on the attack curve at the position corresponding to the current envelope output level.
2. **Given** multiple notes are playing simultaneously, **When** the display renders, **Then** only the most recently triggered voice's envelope position is shown (avoiding visual clutter).
3. **Given** no notes are playing, **When** the display renders, **Then** no playback dot is visible and the refresh timer is stopped to conserve resources.

---

### Edge Cases

- What happens when the user drags a control point outside the display bounds? The position must be clamped to valid parameter ranges. Attack, decay, and release times are clamped to [0.1, 10000] ms. Sustain level is clamped to [0.0, 1.0].
- What happens when the host sets parameter values programmatically (e.g., automation playback)? The display must update to reflect the new parameter values without triggering feedback loops. The control must not call valueChanged() for programmatic updates (only for user-initiated edits where isEditing() == true).
- What happens when attack time is extremely short (0.1ms) and release time is extremely long (10s)? The logarithmic time axis ensures both segments are visible. The minimum segment width of 15% prevents any segment from becoming too narrow to interact with.
- What happens when sustain level is 0.0 or 1.0? At sustain=0.0, the sustain hold line is at the bottom and the release segment is essentially zero-length visually. At sustain=1.0, the decay segment is zero-length visually (peak equals sustain). Both cases must render correctly.
- What happens when the display is too small? At the specified dimensions (130-150px W x 80-100px H), all elements must remain usable. Below a minimum threshold, labels and time values should be hidden to prevent overlap.
- What happens when switching from Bezier mode with S-curves back to Simple mode? A confirmation should be shown since S-curves cannot be represented by a single curve amount. If confirmed, the best-fit single-value approximation is used.
- What happens when the Bezier control points create a curve that overshoots beyond [0, 1] level range? The DSP lookup table generation must clamp output values. The display must render the overshoot visually to show what the user has created.

## Requirements *(mandatory)*

### Functional Requirements

#### Envelope Display Rendering

- **FR-001**: The display MUST render the ADSR envelope as a filled area with a gradient fill in the envelope's identity color, with a brighter stroke on top.
- **FR-002**: The display MUST use these envelope identity colors:
  - ENV 1 (Amp): fill rgba(80,140,200,0.3), stroke rgb(80,140,200)
  - ENV 2 (Filter): fill rgba(220,170,60,0.3), stroke rgb(220,170,60)
  - ENV 3 (Mod): fill rgba(160,90,200,0.3), stroke rgb(160,90,200)
- **FR-003**: The display MUST render horizontal grid lines at 25%, 50%, and 75% level positions and vertical grid lines at time divisions (auto-scaled).
- **FR-004**: The display MUST render a dark background fill (rgb(30,30,33)) with the subtle grid.
- **FR-005**: The display MUST render a dashed horizontal line from the Sustain point to the release start, indicating the sustain hold period. The sustain hold segment occupies a fixed 25% of the total display width. Calculation order: (1) allocate 25% of display width to the sustain hold segment; (2) compute logarithmic proportions for Attack, Decay, and Release across the remaining 75%; (3) apply the 15% minimum constraint to Attack, Decay, and Release segments and renormalize if needed.
- **FR-006**: The display MUST render a vertical dashed line (gate marker) separating the gate-on (Attack+Decay+Sustain) and gate-off (Release) sections.
- **FR-007**: The display MUST render time labels at control points showing actual timing values (e.g., "10ms", "50ms", "100ms") and a total duration label in the bottom-right corner.

#### Time Axis Scaling

- **FR-008**: The display MUST auto-scale the time axis to fit the full ADSR shape within the display width.
- **FR-009**: The display MUST use logarithmic time axis scaling so that short segments (e.g., 1ms attack) and long segments (e.g., 5s release) are both comfortably visible. Logarithmic scaling is applied first, then segment widths are clamped to ensure FR-010 minimum.
- **FR-010**: Each ADSR segment MUST occupy at least 15% of the display width, regardless of its time proportion relative to other segments, ensuring all control points remain grabbable. If logarithmic scaling produces segments narrower than 15%, those segments are widened to meet the minimum and remaining space is renormalized.

#### Control Point Rendering and Hit Testing

- **FR-011**: The display MUST render 4 key control points:
  - **Start**: Fixed at (0, 0) -- not interactive.
  - **Peak**: At (attackTime, 1.0) -- draggable horizontally only.
  - **Sustain**: At (attackTime + decayTime, sustainLevel) -- draggable both axes.
  - **End**: At (totalTime, 0) -- draggable horizontally only.
- **FR-012**: Control points MUST be rendered as 8px filled circles with a 12px radius hit target for easy grabbing.
- **FR-013**: The display MUST support dragging the Peak point horizontally to adjust attack time (vertical locked at 1.0).
- **FR-014**: The display MUST support dragging the Sustain point horizontally to adjust decay time and vertically to adjust sustain level. Shift+drag MUST constrain to a single axis.
- **FR-015**: The display MUST support dragging the End point horizontally to adjust release time (vertical locked at 0.0).

#### Curve Interaction (Simple Mode)

- **FR-016**: The display MUST support click+drag on the curve line itself (not on a control point) to adjust the curve amount for that segment. Control points take priority; curve segments are only draggable in the middle third of each segment, avoiding the endpoint regions where control points are located.
- **FR-017**: Dragging the curve upward MUST increase the curve toward logarithmic (negative curve amount, -1.0 = fast start, slow end). Dragging downward MUST increase toward exponential (positive curve amount, +1.0 = slow start, fast end).
- **FR-018**: The curve MUST visually bend in real-time as the user drags.
- **FR-019**: A tooltip or label MUST show the current curve value during drag (e.g., "Curve: -0.35").

#### General Interaction

- **FR-020**: Double-clicking a control point MUST reset that point to its default value (attack=10ms, decay=50ms, sustain=0.5, release=100ms).
- **FR-021**: Double-clicking a curve MUST reset the curve amount to linear (0.0).
- **FR-022**: Shift+drag on a control point MUST activate fine adjustment mode with 0.1x movement scale (10x precision).
- **FR-023**: Pressing Escape during a drag MUST cancel the drag and revert all affected parameters to their pre-drag values.
- **FR-024**: Right-click MUST be reserved for the host parameter context menu (no custom right-click menu).
- **FR-025**: All drag gestures MUST be wrapped in beginEdit()/endEdit() pairs for proper host undo support. One pair per drag gesture.

#### Bezier Mode (Pro)

- **FR-026**: The display MUST include a small toggle button in the top-right corner: [S] for Simple mode, [B] for Bezier mode.
- **FR-027**: In Bezier mode, each segment MUST show 2 Bezier control handles (cp1, cp2) defining a cubic Bezier curve, connected to the segment endpoints by thin 1px rgb(100,100,100) lines.
- **FR-028**: Bezier handles MUST be rendered as 6px diamond shapes to distinguish them from the 8px circle control points, with an 8px radius hit target.
- **FR-029**: The active/dragged handle MUST brighten to rgb(200,200,200).
- **FR-030**: In Bezier mode, Bezier control points MUST be the source of truth for curve shape; the simple curve amount parameter MUST be ignored.
- **FR-031**: Handles MUST be draggable to create S-curves (handles crossing) and overshoots.
- **FR-032**: Switching from Bezier to Simple mode MUST approximate the current Bezier curve as a single curve amount value. When converting a Bezier envelope segment to Simple mode, the system SHALL sample the Bezier curve at 50% of the segment's normalized phase and derive the Simple curve amount that produces the same value at that phase. This conversion is lossy and deterministic. A confirmation MUST be shown if the Bezier curve contains S-curves (since fidelity will be lost).
- **FR-033**: Switching from Simple to Bezier mode MUST generate default Bezier handles that reproduce the same curve shape.

#### Real-Time Playback Visualization

- **FR-034**: The display MUST show a bright 6px dot traveling along the curve at the current envelope position when a note is playing.
- **FR-035**: The playback dot position MUST reflect the current envelope stage and output level, obtained via IMessage from the processor.
- **FR-036**: The playback visualization MUST use a timer-based refresh strategy (CVSTGUITimer) at approximately 30fps, started/stopped based on voice activity.
- **FR-037**: When multiple voices are playing simultaneously, the display MUST show only the most recently triggered voice's envelope position. The controller tracks note-on timestamps for all active voices and always displays the voice with the most recent note-on time.

#### Parameter Communication

- **FR-038**: The display MUST communicate with the following existing ADSR parameters (per envelope):

  | Envelope | Attack | Decay | Sustain | Release |
  |----------|--------|-------|---------|---------|
  | Amp (ENV 1) | 700 | 701 | 702 | 703 |
  | Filter (ENV 2) | 800 | 801 | 802 | 803 |
  | Mod (ENV 3) | 900 | 901 | 902 | 903 |

- **FR-039**: The display MUST communicate with the following new curve amount parameters (per envelope):

  | Envelope | Attack Curve | Decay Curve | Release Curve |
  |----------|-------------|-------------|---------------|
  | Amp (ENV 1) | 704 | 705 | 706 |
  | Filter (ENV 2) | 804 | 805 | 806 |
  | Mod (ENV 3) | 904 | 905 | 906 |

  Curve parameters range [-1.0, +1.0] with 0.0 = linear, -1.0 = logarithmic, +1.0 = exponential.

- **FR-040**: The display MUST communicate with the following new Bezier control point parameters (per envelope, hidden in host unless Bezier mode is active):

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

  All Bezier values normalized [0.0-1.0] within the segment's bounding box.

- **FR-041**: Per-envelope Bezier mode flags MUST use parameter IDs: Amp=707, Filter=807, Mod=907.
- **FR-042**: Total new parameters: 9 curve amounts + 36 Bezier control points + 3 Bezier enabled flags = 48 new parameters.

#### DSP Integration

- **FR-043**: The continuous curve amount parameter [-1, +1] MUST replace the current discrete EnvCurve enum (Exponential/Linear/Logarithmic) with a continuous curve system. The 3 discrete values map approximately to: Logarithmic = -0.7, Linear = 0.0, Exponential = +0.7.
- **FR-044**: The DSP MUST use a 256-entry lookup table per segment for curve evaluation. In Simple mode, the table is generated using a power curve formula: `output = phase^(2^(curve * k))` where k controls curvature range. In Bezier mode, the table is generated from the cubic Bezier control points.
- **FR-045**: Both modes MUST result in the same table-based processing -- the audio thread sees no difference between Simple and Bezier curve sources.
- **FR-046**: Lookup tables MUST be evaluated once per parameter change, not per sample, ensuring zero real-time cost beyond table lookup.

#### Knob-Display Synchronization

- **FR-047**: The ADSR knobs below the display and the display's drag points MUST control the same parameters. Either input method updates both through the VST parameter system.
- **FR-048**: Curve amount parameters MUST have no knobs; they are controlled exclusively by dragging the curve in the display.

#### Shared Control Architecture

- **FR-049**: The ADSRDisplay MUST be implemented as a shared custom VSTGUI control in `plugins/shared/src/ui/` under the `Krate::Plugins` namespace, following the same pattern as ArcKnob, FieldsetContainer, StepPatternEditor, and XYMorphPad.
- **FR-050**: The ADSRDisplay MUST be registered with the VSTGUI UIViewFactory via a ViewCreator struct, allowing placement and configuration in `.uidesc` files.
- **FR-051**: The ADSRDisplay MUST support ViewCreator attributes for configurable envelope identity colors (fill and stroke), background color, grid color, control point color, and hit target sizes, enabling per-envelope customization via `.uidesc` without code changes.
- **FR-052**: The ADSRDisplay MUST use the shared `color_utils.h` utility functions for any color manipulation, avoiding ODR violations.

#### Dimensions

- **FR-053**: A single ADSRDisplay MUST target dimensions of 130-150px width by 80-100px height.
- **FR-054**: The mode toggle [S]/[B] MUST be 16x16px and positioned in the top-right corner of the display.
- **FR-055**: Bezier handle hit targets MUST be 8px radius. Control point hit targets MUST be 12px radius.

### Key Entities

- **ADSRDisplay**: Custom CControl subclass in the `Krate::Plugins` namespace. Holds references to ADSR parameters, curve parameters, and optionally Bezier parameters. Manages rendering, hit testing, drag state, playback visualization, and mode toggling.
- **ControlPoint**: Represents a draggable point on the envelope (Peak, Sustain, End). Each has a position derived from parameter values, constraints on which axes are draggable, and a default value for reset.
- **CurveSegment**: Represents one of the three curve segments (Attack, Decay, Release). Each has a curve amount parameter [-1, +1] in Simple mode, or two Bezier control points (cp1, cp2) in Bezier mode.
- **BezierHandle**: In Bezier mode, each segment has two handles (cp1, cp2) normalized within the segment bounding box. Each handle has x,y coordinates [0,1] and maps to a VST parameter ID.
- **PlaybackState**: Tracks the current voice envelope stage and output level for the playback dot visualization. Updated via IMessage from the processor at approximately 30fps.
- **EnvelopeConfig**: Per-instance configuration including the base parameter ID for the ADSR parameters, identity colors, and envelope name (Amp/Filter/Mod).

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Users can adjust any ADSR parameter (attack, decay, sustain, release) by dragging control points, with the display and knobs updating simultaneously within a single frame.
- **SC-002**: Fine adjustment mode (Shift+drag) achieves 10x precision compared to normal drag, verified by measuring cursor displacement per pixel of mouse movement.
- **SC-003**: Double-click reset returns a control point to its default value within a single interaction, completing in under 500 milliseconds.
- **SC-004**: The logarithmic time axis correctly displays envelopes with extreme timing ratios (e.g., 0.1ms attack + 10s release) where all segments are visible and all control points remain grabbable (each segment occupies at least 15% of the display width).
- **SC-005**: Curve amount changes via drag produce a smooth, continuous range from -1.0 to +1.0 with visible curve bending that matches the expected logarithmic/linear/exponential character.
- **SC-006**: All ADSR and curve parameter edits are correctly communicated to the host with proper beginEdit/endEdit wrapping, verifiable by host automation lane recording showing smooth value changes during drag gestures.
- **SC-007**: The playback dot accurately tracks the envelope stage and output level during note playback at approximately 30fps, disappearing when no voices are active.
- **SC-008**: Escape during drag restores all affected parameters to pre-drag values, verified by comparing parameter values before drag start and after Escape.
- **SC-009**: The control renders correctly at the target dimensions (130-150px x 80-100px) without visual artifacts, truncated labels, or overlapping elements.
- **SC-010**: The Bezier mode toggle correctly switches between Simple and Bezier modes, with handles appearing/disappearing and curve shapes preserving continuity during transitions.
- **SC-011**: Three ADSRDisplay instances (one per envelope) can be placed side by side in the Envelope section, each with distinct identity colors, without interference or shared state.
- **SC-012**: Coordinate conversion round-trips (parameter-to-pixel-to-parameter) are accurate within 0.01 tolerance of the original values.

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- The existing ADSREnvelope DSP component (spec 032) already provides `setAttackCurve()`, `setDecayCurve()`, `setReleaseCurve()` accepting the `EnvCurve` enum. This spec requires replacing the discrete EnvCurve enum with a continuous curve system using 256-entry lookup tables, which is a DSP modification coordinated with this UI spec.
- The existing ADSR parameter IDs (700-703, 800-803, 900-903) are already defined and registered in the Ruinae plugin. The ADSRDisplay reads these parameters for its visualization and writes to them when the user drags control points.
- The new curve amount parameters (704-706, 804-806, 904-906) and Bezier parameters (710-721, 810-821, 910-921) and Bezier mode flags (707, 807, 907) need to be added to `plugin_ids.h` and registered in the controller.
- The knobs below the ADSRDisplay are standard CAnimKnob controls in `editor.uidesc`, already bound to the ADSR parameter IDs. No modifications to the knobs are needed -- synchronization happens automatically through the VST parameter system.
- The existing `AmpEnvParams`, `FilterEnvParams`, and `ModEnvParams` structs in the `parameters/` directory will need to be extended with new atomic fields for curve amounts and Bezier control points.
- The processor communicates voice activity and envelope position to the controller via `IMessage`, following the same pattern used by StepPatternEditor for playback position.
- The ViewCreator registration pattern (inline global variable) is safe for use across multiple plugins, as established by ArcKnob, FieldsetContainer, StepPatternEditor, and XYMorphPad.

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| ADSREnvelope (DSP) | `dsp/include/krate/dsp/primitives/adsr_envelope.h` | **Modify.** Current DSP uses `EnvCurve` enum with 3 discrete values. Must be extended to support continuous curve amount + lookup table system. `setAttackCurve()`, `setDecayCurve()`, `setReleaseCurve()` signatures change. |
| EnvCurve enum | `dsp/include/krate/dsp/primitives/envelope_utils.h` | **Replace/Extend.** Current enum (Exponential/Linear/Logarithmic) is replaced by continuous [-1,+1] curve parameter. Discrete values map to: Log=-0.7, Linear=0.0, Exp=+0.7. |
| AmpEnvParams | `plugins/ruinae/src/parameters/amp_env_params.h` | **Extend.** Add atomic fields for 3 curve amounts + 12 Bezier control points + 1 Bezier enabled flag. |
| FilterEnvParams | `plugins/ruinae/src/parameters/filter_env_params.h` | **Extend.** Same additions as AmpEnvParams. |
| ModEnvParams | `plugins/ruinae/src/parameters/mod_env_params.h` | **Extend.** Same additions as AmpEnvParams. |
| Plugin IDs | `plugins/ruinae/src/plugin_ids.h` | **Extend.** Add 48 new parameter IDs (704-721, 804-821, 904-921, 707, 807, 907). |
| StepPatternEditor | `plugins/shared/src/ui/step_pattern_editor.h` | **Pattern reference.** CControl subclass pattern, ParameterCallback for multi-parameter communication, drawing decomposition (separate draw methods), CLASS_METHODS macro, ViewCreator with color attributes, CVSTGUITimer for playback refresh. |
| XYMorphPad | `plugins/shared/src/ui/xy_morph_pad.h` | **Pattern reference.** Dual-parameter pattern (X via tag, Y via performEdit), coordinate conversion with Y-axis inversion, fine adjustment (Shift, 0.1x), double-click reset, Escape cancel, beginEdit/endEdit wrapping. |
| color_utils.h | `plugins/shared/src/ui/color_utils.h` | **Direct reuse.** Provides `lerpColor()`, `darkenColor()`, `brightenColor()`. Use for grid colors, dimmed states, and any color interpolation. |
| ArcKnob | `plugins/shared/src/ui/arc_knob.h` | **Pattern reference.** ViewCreator registration pattern (inline global variable), `Krate::Plugins` namespace convention. |

**Initial codebase search for key terms:**

```bash
grep -r "ADSRDisplay" plugins/ dsp/
grep -r "class.*Display" plugins/shared/src/ui/
grep -r "kAmpEnvAttackCurve\|kAmpEnvDecayCurve" plugins/
```

**Search Results Summary**:
- `ADSRDisplay`: No existing implementations found. This is a new component.
- `class.*Display`: No existing display controls in shared UI. This is the first display-type control.
- Curve parameter IDs: Not yet defined. Must be added to plugin_ids.h.

### Forward Reusability Consideration

**Sibling features at same layer** (if known):
- WaveformDisplay (roadmap custom control) shares the custom CControl + ViewCreator registration architecture and timer-based refresh.
- ModMatrixGrid (roadmap custom control #4) shares multi-parameter communication patterns.
- Other envelope editors in future plugins can reuse this same control with different identity colors.

**Potential shared components** (preliminary, refined in plan.md):
- The 256-entry lookup table generation (power curve formula and cubic Bezier evaluation) could be extracted to a shared utility in `dsp/include/krate/dsp/core/` for reuse by any DSP component needing parameterized curve shapes.
- The logarithmic time axis calculation could be extracted to a shared utility if other display components (e.g., WaveformDisplay) need similar auto-scaling.
- The multi-parameter CControl pattern (communicating with 7+ parameters per instance via callbacks) should be documented as a pattern, building on XYMorphPad's dual-parameter approach.
- The coordinate conversion with logarithmic scaling (time-to-pixel, pixel-to-time) is specific to this control but could be shared with future time-domain display controls.

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
| FR-001 | | |
| FR-002 | | |
| FR-003 | | |
| FR-004 | | |
| FR-005 | | |
| FR-006 | | |
| FR-007 | | |
| FR-008 | | |
| FR-009 | | |
| FR-010 | | |
| FR-011 | | |
| FR-012 | | |
| FR-013 | | |
| FR-014 | | |
| FR-015 | | |
| FR-016 | | |
| FR-017 | | |
| FR-018 | | |
| FR-019 | | |
| FR-020 | | |
| FR-021 | | |
| FR-022 | | |
| FR-023 | | |
| FR-024 | | |
| FR-025 | | |
| FR-026 | | |
| FR-027 | | |
| FR-028 | | |
| FR-029 | | |
| FR-030 | | |
| FR-031 | | |
| FR-032 | | |
| FR-033 | | |
| FR-034 | | |
| FR-035 | | |
| FR-036 | | |
| FR-037 | | |
| FR-038 | | |
| FR-039 | | |
| FR-040 | | |
| FR-041 | | |
| FR-042 | | |
| FR-043 | | |
| FR-044 | | |
| FR-045 | | |
| FR-046 | | |
| FR-047 | | |
| FR-048 | | |
| FR-049 | | |
| FR-050 | | |
| FR-051 | | |
| FR-052 | | |
| FR-053 | | |
| FR-054 | | |
| FR-055 | | |
| SC-001 | | |
| SC-002 | | |
| SC-003 | | |
| SC-004 | | |
| SC-005 | | |
| SC-006 | | |
| SC-007 | | |
| SC-008 | | |
| SC-009 | | |
| SC-010 | | |
| SC-011 | | |
| SC-012 | | |

**Status Key:**
- MET: Requirement verified against actual code and test output with specific evidence
- NOT MET: Requirement not satisfied (spec is NOT complete)
- PARTIAL: Partially met with documented gap and specific evidence of what IS met
- DEFERRED: Explicitly moved to future work with user approval

### Completion Checklist

*All items must be checked before claiming completion:*

- [ ] Each FR-xxx row was verified by re-reading the actual implementation code (not from memory)
- [ ] Each SC-xxx row was verified by running tests or reading actual test output (not assumed)
- [ ] Evidence column contains specific file paths, line numbers, test names, and measured values
- [ ] No evidence column contains only generic claims like "implemented", "works", or "test passes"
- [ ] No test thresholds relaxed from spec requirements
- [ ] No placeholder values or TODO comments in new code
- [ ] No features quietly removed from scope
- [ ] User would NOT feel cheated by this completion claim

### Honest Assessment

**Overall Status**: [COMPLETE / NOT COMPLETE / PARTIAL]

**If NOT COMPLETE, document gaps:**
- [Gap 1: FR-xxx not met because...]
- [Gap 2: SC-xxx achieves X instead of Y because...]

**Recommendation**: [What needs to happen to achieve completion]
