# Feature Specification: Morph UI & Type-Specific Parameters

**Feature Branch**: `006-morph-ui`
**Created**: 2026-01-28
**Status**: Draft
**Input**: User description: "Morph UI & Type-Specific Parameters for Disrumpo (Week 7 per roadmap)"

**Related Documents**:
- [Disrumpo/roadmap.md](../Disrumpo/roadmap.md) - Task breakdown T7.1-T7.43
- [Disrumpo/ui-mockups.md](../Disrumpo/ui-mockups.md) - UI layout specifications
- [Disrumpo/custom-controls.md](../Disrumpo/custom-controls.md) - MorphPad control specification
- [Disrumpo/vstgui-implementation.md](../Disrumpo/vstgui-implementation.md) - VSTGUI patterns, UIViewSwitchContainer
- [Disrumpo/dsp-details.md](../Disrumpo/dsp-details.md) - Parameter ID encoding, data structures
- [005-morph-system/spec.md](../005-morph-system/spec.md) - Morph DSP engine (prerequisite)

**Prerequisites**:
- 005-morph-system MUST be complete (MorphEngine class, weight computation, interpolation)
- 004-vstgui-infrastructure MUST be complete (parameter registration, editor.uidesc foundation)

---

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Control Morph Position via MorphPad (Priority: P1)

A sound designer wants to interactively explore morph space by dragging a cursor on a 2D pad to blend between distortion types in real-time.

**Why this priority**: The MorphPad is the primary interface for the morph system - without it, users cannot control the most distinctive feature of Disrumpo.

**Independent Test**: Can be fully tested by dragging the morph cursor and verifying that the MorphX/MorphY parameters update and audio changes correspondingly.

**Acceptance Scenarios**:

1. **Given** the MorphPad is displayed with nodes at corners, **When** the user clicks at position (0.7, 0.3), **Then** the morph cursor moves to that position and Band*MorphX/Y parameters are set to 0.7/0.3
2. **Given** the user is dragging the morph cursor, **When** they hold Shift while dragging, **Then** cursor movement is 10x finer for precise adjustment
3. **Given** the morph cursor is at any position, **When** the user double-clicks the MorphPad, **Then** the cursor resets to center (0.5, 0.5)

---

### User Story 2 - View Type-Specific Parameters (Priority: P1)

A user wants to see and adjust parameters specific to the currently selected distortion type, with different controls appearing based on the type selection.

**Why this priority**: Type-specific parameters are essential for sound design - Drive/Mix/Tone alone are insufficient for the 26 distortion types.

**Independent Test**: Can be fully tested by changing the distortion type dropdown and verifying the parameter panel switches to show the correct controls.

**Acceptance Scenarios**:

1. **Given** Band 0 Node 0 Type is set to "Tube", **When** the expanded band view is shown, **Then** the type-specific zone displays Bias, Sag, and Stage controls
2. **Given** Band 0 Node 0 Type is "Sine Fold", **When** the user changes it to "Bitcrush", **Then** the type-specific zone switches from Folds/Shape/Symmetry to BitDepth/Dither/Mode
3. **Given** the type-specific panel is visible, **When** the user adjusts a type-specific parameter, **Then** the corresponding VST parameter updates and audio changes

---

### User Story 3 - Expand/Collapse Band View (Priority: P1)

A user wants to see detailed controls for a specific band without cluttering the interface when not needed.

**Why this priority**: Progressive disclosure is a core UX principle in Disrumpo - users need both quick access (collapsed) and detailed editing (expanded).

**Independent Test**: Can be fully tested by clicking the expand button and verifying the expanded view appears with all expected sections.

**Acceptance Scenarios**:

1. **Given** Band 2 is in collapsed state showing Type/Drive/Mix/S/B/M, **When** the user clicks the expand button (+), **Then** the band expands to show Morph controls, Type-specific params, and Output section
2. **Given** Band 2 is expanded, **When** the user clicks the collapse button (-), **Then** the band collapses back to the compact strip view
3. **Given** Band 2 is expanded, **When** the user expands Band 3, **Then** both bands can be expanded simultaneously (no accordion behavior)

---

### User Story 4 - Select Morph Mode (Priority: P2)

A user wants to switch between 1D Linear, 2D Planar, and 2D Radial morph modes to change how the morph space behaves.

**Why this priority**: Different morph modes suit different creative workflows, but 2D Planar (default) covers most use cases.

**Independent Test**: Can be fully tested by changing the morph mode selector and verifying the MorphPad visual changes accordingly.

**Acceptance Scenarios**:

1. **Given** Morph Mode is "2D Planar" (default), **When** the user selects "1D Linear", **Then** the MorphPad constrains cursor to horizontal center line and nodes arrange along X axis
2. **Given** Morph Mode is "2D Planar", **When** the user selects "2D Radial", **Then** the MorphPad shows radial grid overlay and cursor position maps to angle + distance from center
3. **Given** Morph Mode has been changed, **When** the user adjusts morph position, **Then** weights are calculated according to the selected mode's algorithm

---

### User Story 5 - Reposition Morph Nodes (Priority: P2)

A power user wants to customize the node positions in the morph space to create non-standard morph geometries.

**Why this priority**: Customizable node positions enable advanced sound design, but default corner positions work for most users.

**Independent Test**: Can be fully tested by Alt+dragging a node and verifying its position updates.

**Acceptance Scenarios**:

1. **Given** the MorphPad shows 4 nodes at corners, **When** the user Alt+drags Node B, **Then** Node B moves to the new position and is persisted
2. **Given** a node has been repositioned, **When** the user moves the morph cursor, **Then** weight calculation uses the updated node positions
3. **Given** nodes are in non-default positions, **When** the preset is saved and reloaded, **Then** node positions are preserved

---

### User Story 6 - Configure Active Nodes Count (Priority: P2)

A user wants to simplify the morph space by using only 2 or 3 nodes instead of 4.

**Why this priority**: Simpler configurations reduce complexity for focused sound design tasks.

**Independent Test**: Can be fully tested by changing the active nodes selector and verifying node visibility changes.

**Acceptance Scenarios**:

1. **Given** Active Nodes is set to 4, **When** the user selects 2, **Then** only Nodes A and B are displayed and active in weight calculations
2. **Given** Active Nodes is set to 2, **When** the user selects 3, **Then** Node C becomes visible and participates in morph calculations
3. **Given** Active Nodes is 2, **When** the user positions the cursor, **Then** weights are distributed only between the 2 active nodes

---

### User Story 7 - View Node-Specific Parameters in Expanded View (Priority: P3)

A user editing a complex morph space wants to see all node parameters (not just the dominant node) to fine-tune each morph destination.

**Why this priority**: Advanced editing capability, but most users focus on one node at a time.

**Independent Test**: Can be fully tested by clicking on different node indicators and verifying the parameter panel switches to show that node's settings.

**Acceptance Scenarios**:

1. **Given** the expanded band view shows Node A parameters, **When** the user clicks on Node B indicator, **Then** the type-specific panel switches to show Node B's parameters
2. **Given** 4 nodes are active, **When** the user views the node editor panel, **Then** all 4 nodes are listed with their types displayed
3. **Given** the node editor shows Node C, **When** the user changes Node C's type, **Then** the UIViewSwitchContainer updates to show that type's parameters

---

### User Story 8 - Link Morph Position to Sweep Frequency (Priority: P1)

A sound designer wants the morph position to automatically follow the sweep frequency, creating evolving timbres that change as the sweep moves through the spectrum.

**Why this priority**: Morph-sweep linking is a core differentiator of Disrumpo - it enables the signature "sweeping distortion character" effect.

**Independent Test**: Can be fully tested by enabling sweep, setting Morph X Link to "Sweep Freq", and verifying the morph cursor moves with the sweep.

**Acceptance Scenarios**:

1. **Given** Morph X Link is set to "Sweep Freq" and sweep is at 200Hz, **When** the sweep moves to 2kHz, **Then** the Morph X position changes proportionally (low freq = 0, high freq = 1)
2. **Given** Morph X Link is set to "Inverse Sweep", **When** the sweep moves from low to high frequency, **Then** the Morph X position moves from 1 to 0 (opposite direction)
3. **Given** Morph Y Link is set to "None", **When** the sweep position changes, **Then** Morph Y remains at its manually-set position

---

## Clarifications

### Session 2026-01-28

- Q: When morphing between cross-family types (e.g., Tube ↔ Sine Fold), how should the type-specific zone display both parameter sets? → A: Side-by-side equal split - Fixed 50/50 layout, opacity fades each side based on weight
- Q: When the user rapidly changes morph position (e.g., dragging cursor quickly), how should Morph Smoothing apply interpolation? → A: Target-chase with rate limiting - Morph position always moves toward current cursor at fixed rate (ms determines speed)
- Q: What visual style should the connection lines use to show morph weights? → A: Gradient lines from white to node color - Visual flow from cursor (white) to node (colored), opacity by weight
- Q: How should the user select which node's parameters are displayed in the type-specific zone? → A: Click node in MorphPad or node editor list - Either clicking a node circle in MorphPad or clicking node row in editor selects it
- Q: What should the "EaseIn" link mode do when applied to a morph axis? → A: Exponential curve emphasizing low frequencies - More morph range for bass, less for highs

### Edge Cases

- What happens when morph cursor is exactly on a node position? - That node gets 100% weight
- What happens when UIViewSwitchContainer receives an invalid type index? - Falls back to first template (Soft Clip)
- What happens when user drags cursor outside MorphPad bounds? - Cursor is clamped to [0,1] range
- What happens when expanded view has insufficient space for all 26 type templates? - Each template has fixed maximum size, scrolling if needed
- What happens when preset loads with morph mode different from current display? - MorphPad visual updates to match loaded mode
- What happens when Morph X Link is "Sweep Freq" but sweep is disabled? - Link has no effect; morph position stays at last manual value
- What happens when both X and Y are linked to Sweep Freq? - Both axes move together along the diagonal (0,0 to 1,1)
- What happens when Morph Smoothing is 0ms and linked to sweep? - Morph follows sweep exactly with no interpolation (target-chase reaches target instantly)
- What happens when cross-family morph weight is exactly 50%? - Both type panels show at equal opacity (50% each) in side-by-side layout
- What happens when EaseIn and EaseOut are applied to X and Y respectively? - Each axis uses its own curve mapping independently
- What happens when node is clicked while cursor is being dragged? - Click-to-select has priority; cursor drag is interrupted and node selection changes
- What happens when Morph Smoothing > 0 and user rapidly clicks different MorphPad positions? - Each click sets a new target; smoothing chases the latest target (previous targets are discarded)

---

## Requirements *(mandatory)*

### Functional Requirements

#### MorphPad Custom Control (T7.1-T7.10)

- **FR-001**: System MUST provide a MorphPad custom VSTGUI control class that displays morph nodes and cursor position
- **FR-002**: MorphPad MUST render nodes as 12px diameter filled circles with category-specific colors (per custom-controls.md Section 2.3.1)
- **FR-003**: MorphPad MUST render the morph cursor as a 16px diameter open circle with 2px white stroke
- **FR-004**: MorphPad MUST support click-to-move cursor interaction
- **FR-005**: MorphPad MUST support drag interaction for continuous cursor movement
- **FR-006**: MorphPad MUST implement Shift+drag for fine adjustment (10x precision)
- **FR-007**: MorphPad MUST implement Alt+drag for node repositioning
- **FR-008**: MorphPad MUST render connection lines from cursor to nodes as gradient lines (white at cursor, node category color at node) with opacity proportional to node weight
- **FR-009**: MorphPad MUST support three visual modes: 1D Linear (nodes on X axis, cursor constrained), 2D Planar (nodes at corners), 2D Radial (radial grid overlay)
- **FR-010**: MorphPad MUST be registered in Controller::createCustomView() with identifier "MorphPad"
- **FR-011**: MorphPad MUST wire cursor position to Band*MorphX and Band*MorphY parameters
- **FR-012**: System MUST provide a CSegmentButton for morph mode selection with options "1D", "2D", "Radial"

#### Expanded Band View (T7.11-T7.14)

- **FR-013**: System MUST provide BandStripExpanded template (680x280 per ui-mockups.md) containing collapsed header + expanded content
- **FR-014**: Each band MUST have an expand/collapse toggle button that shows/hides the expanded content
- **FR-015**: Visibility controllers MUST manage expanded state per-band using the IDependent pattern
- **FR-016**: Expanded view MUST include a mini MorphPad (180x120 per vstgui-implementation.md) bound to that band's morph parameters
- **FR-017**: Expanded view MUST include Morph Mode CSegmentButton (1D/2D/Radial) bound to Band*MorphMode
- **FR-018**: Expanded view MUST include Active Nodes CSegmentButton (2/3/4) bound to Band*ActiveNodes

#### UIViewSwitchContainer for Type Parameters (T7.15-T7.42)

- **FR-019**: System MUST use UIViewSwitchContainer to display type-specific parameters based on the selected distortion type
- **FR-020**: UIViewSwitchContainer MUST have template-switch-control bound to Band*Node*Type parameter
- **FR-021**: System MUST provide 26 TypeParams_* templates, one for each distortion type:
  - TypeParams_SoftClip (D01)
  - TypeParams_HardClip (D02)
  - TypeParams_Tube (D03)
  - TypeParams_Tape (D04)
  - TypeParams_Fuzz (D05)
  - TypeParams_AsymFuzz (D06)
  - TypeParams_SineFold (D07)
  - TypeParams_TriFold (D08)
  - TypeParams_SergeFold (D09)
  - TypeParams_FullRectify (D10)
  - TypeParams_HalfRectify (D11)
  - TypeParams_Bitcrush (D12)
  - TypeParams_SampleReduce (D13)
  - TypeParams_Quantize (D14)
  - TypeParams_Temporal (D15)
  - TypeParams_RingSat (D16)
  - TypeParams_Feedback (D17)
  - TypeParams_Aliasing (D18)
  - TypeParams_Bitwise (D19)
  - TypeParams_Chaos (D20)
  - TypeParams_Formant (D21)
  - TypeParams_Granular (D22)
  - TypeParams_Spectral (D23)
  - TypeParams_Fractal (D24)
  - TypeParams_Stochastic (D25)
  - TypeParams_AllpassRes (D26)

- **FR-022**: Each TypeParams template MUST contain only the type-specific controls (common params Drive/Mix/Tone are outside the switcher)
- **FR-023**: Template layouts MUST follow ui-mockups.md Section 5-12 for parameter placement

#### Node Editor Panel (T7.43)

- **FR-024**: Expanded view MUST include a node editor panel showing all active nodes with their types
- **FR-025**: Node editor MUST allow selecting which node's parameters to display in the type-specific zone by clicking on the node row in the editor list or clicking the node circle in the MorphPad
- **FR-026**: Node editor MUST show node letter (A/B/C/D), type name, and category color indicator
- **FR-027**: MorphPad MUST support clicking on a node circle to select that node for editing (visual feedback: selected node has highlight ring)

#### Parameter Wiring

- **FR-028**: All MorphPad controls MUST be wired to their corresponding control-tags as defined in vstgui-implementation.md Section 2.3
- **FR-029**: Type dropdown in collapsed band view MUST be wired to Band*Node0Type (always shows first node)
- **FR-030**: In expanded view, type-specific controls MUST wire to the currently selected node's parameters

#### Morph Smoothing (from FR-MORPH-003)

- **FR-031**: Expanded view MUST include a Morph Smoothing knob (0-500ms) controlling transition time between morph positions using target-chase interpolation (morph position always moves toward current cursor at rate determined by smoothing value)

#### Morph-Sweep Linking (from ui-mockups.md Level 2)

- **FR-032**: Expanded view MUST include a Morph X Link dropdown with options: None, Sweep Freq, Inverse Sweep, EaseIn, EaseOut, Hold-Rise, Stepped
- **FR-033**: Expanded view MUST include a Morph Y Link dropdown with the same options as X Link
- **FR-034**: When Link is set to "Sweep Freq", the corresponding morph axis MUST follow the sweep center frequency position (low freq = 0, high freq = 1) using linear mapping
- **FR-034a**: When Link is set to "Inverse Sweep", morph position moves opposite to sweep frequency (high freq = 0, low freq = 1)
- **FR-034b**: When Link is set to "EaseIn", morph position uses exponential curve emphasizing low frequencies (more morph range allocated to bass/low-mid, less to highs)
- **FR-034c**: When Link is set to "EaseOut", morph position uses exponential curve emphasizing high frequencies (more morph range allocated to highs, less to bass)
- **FR-034d**: When Link is set to "Hold-Rise", morph position holds at 0 until sweep reaches mid-point, then rises to 1
- **FR-034e**: When Link is set to "Stepped", morph position quantizes to discrete steps (e.g., 0, 0.25, 0.5, 0.75, 1.0) based on sweep frequency ranges

#### Output Section in Expanded View

- **FR-035**: Expanded view MUST include an Output section with Gain knob (-24dB to +24dB) and Pan knob (-100% to +100%)
- **FR-036**: Output section MUST include Solo, Bypass, Mute toggles (duplicating collapsed view for convenience)

#### Morph Blend Visualization (from ui-mockups.md Section 13)

- **FR-037**: When morphing between same-family types, type-specific zone SHOULD show interpolated parameter values
- **FR-038**: When morphing between cross-family types, type-specific zone MUST use side-by-side equal split layout (fixed 50/50 width) showing both types' panels with opacity fading each side proportional to morph weight
- **FR-039**: When a type's morph weight drops below 10%, its panel MAY collapse entirely to reduce visual clutter

#### Additional MorphPad Interactions

- **FR-040**: MorphPad SHOULD support scroll wheel interaction: vertical scroll adjusts X, horizontal scroll adjusts Y
- **FR-041**: MorphPad MUST display current position as "X: 0.00 Y: 0.00" label at bottom-left corner

### Key Entities

- **MorphPad**: Custom CControl displaying 2D morph space with nodes and cursor; communicates morph position to Band*MorphX/Y parameters
- **BandStripExpanded**: CViewContainer template containing morph section, type-specific UIViewSwitchContainer, and output section
- **TypeParams_***: 26 templates each containing type-specific knobs/dropdowns/toggles per ui-mockups.md
- **UIViewSwitchContainer**: VSTGUI container that switches visible child based on controlling parameter value
- **Morph Smoothing**: Parameter controlling transition time (0-500ms) when morph position changes
- **Morph X/Y Link**: Dropdown parameters that bind morph axes to sweep frequency or other sources (None, Sweep Freq, Inverse Sweep, EaseIn, EaseOut, Hold-Rise, Stepped)
- **Output Section**: Band output controls (Gain, Pan, Solo/Bypass/Mute) shown in expanded view for convenient access

---

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: MorphPad cursor drag updates parameter values within 1 audio buffer latency (< 12ms at 44.1kHz/512 samples)
- **SC-002**: UIViewSwitchContainer switches between all 26 templates without visual glitch or flicker
- **SC-003**: Expand/collapse toggle completes instantly (< 1 frame, no animation). If animation is added later, it MUST complete in under 100ms
- **SC-004**: All 26 type-specific parameter panels display correct controls matching ui-mockups.md specifications
- **SC-005**: User can control morph position for any band using only the MorphPad (no need to type values)
- **SC-006**: Type-specific parameters persist correctly across preset save/load cycles
- **SC-007**: MorphPad displays correctly at both full size (250x200) and mini size (180x120)
- **SC-008**: All morph mode visual changes (1D constraint, radial grid) render correctly
- **SC-009**: Shift+drag provides noticeably finer control (10x precision verified by parameter value changes)
- **SC-010**: Alt+drag node repositioning updates node position and persists in preset
- **SC-011**: Morph Smoothing knob affects transition time (0ms = instant, 500ms = slow glide) verified by audio output
- **SC-012**: Morph X/Y Link dropdowns correctly bind morph position to sweep frequency when "Sweep Freq" is selected
- **SC-013**: Output section Gain/Pan controls in expanded view affect the band's output correctly
- **SC-014**: MorphPad position label updates in real-time showing "X: 0.00 Y: 0.00" format with 2 decimal precision

---

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- 005-morph-system is complete and MorphEngine correctly computes weights from cursor position
- 004-vstgui-infrastructure provides all required parameter registrations and control-tags
- VSTGUI 4.11+ UIViewSwitchContainer supports template-switch-control binding
- All 450+ parameters are already registered in Controller::initialize()
- editor.uidesc foundation with colors, fonts, gradients exists from 004-vstgui-infrastructure

### Existing Codebase Components (Principle XIV)

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| VST3EditorDelegate | vst3sdk/vstgui/plugin-bindings/vst3editor.h | Base class for createCustomView() |
| CXYPad | vstgui4/lib/controls/cxypad.h | Reference implementation for MorphPad |
| UIViewSwitchContainer | vstgui4/uidescription/uiviewswitchcontainer.h | Used directly for type switching |
| VisibilityController pattern | .claude/skills/vst-guide/THREAD-SAFETY.md | IDependent pattern for visibility |
| BandStripCollapsed template | 004-vstgui-infrastructure spec | Header portion reused in expanded |

**Initial codebase search for key terms:**

```bash
# Run these searches to identify existing implementations
grep -r "MorphPad" plugins/
grep -r "UIViewSwitchContainer" plugins/
grep -r "createCustomView" plugins/
```

**Search Results Summary**: MorphPad is specified in custom-controls.md but not yet implemented. UIViewSwitchContainer is a standard VSTGUI component. createCustomView pattern documented in vstgui-implementation.md but implementation depends on 004-vstgui-infrastructure.

### Forward Reusability Consideration

**Sibling features at same layer**:
- 007 (Week 8 Sweep System) may need similar custom control patterns
- 008 (Week 9-10 Modulation) will need modulation routing matrix UI

**Potential shared components**:
- MorphPad drag/interaction patterns could inform future XY controls
- UIViewSwitchContainer template pattern applies to any multi-mode UI
- Node editor panel pattern could apply to modulation routing sources

---

## Type-Specific Parameter Templates Reference

The following documents the parameter controls for each of the 26 distortion types as specified in ui-mockups.md:

### Saturation Types (D01-D06)

| Type | Parameters | Controls |
|------|------------|----------|
| D01 Soft Clip | Curve, Knee | 2 knobs |
| D02 Hard Clip | Threshold, Ceiling | 2 knobs |
| D03 Tube | Bias, Sag, Stage | 2 knobs, 1 dropdown |
| D04 Tape | Bias, Sag, Speed, Model, HF Roll, Flutter | 4 knobs, 1 dropdown |
| D05 Fuzz | Bias, Gate, Transistor, Octave, Sustain | 4 knobs, 1 dropdown |
| D06 Asym Fuzz | Bias, Asymmetry, Transistor, Gate, Sustain, Body | 5 knobs, 1 dropdown |

### Wavefold Types (D07-D09)

| Type | Parameters | Controls |
|------|------------|----------|
| D07 Sine Fold | Folds, Symmetry, Shape, Bias, Smooth | 5 knobs |
| D08 Triangle Fold | Folds, Symmetry, Angle, Bias, Smooth | 5 knobs |
| D09 Serge Fold | Folds, Symmetry, Model, Bias, Shape, Smooth | 5 knobs, 1 dropdown |

### Rectify Types (D10-D11)

| Type | Parameters | Controls |
|------|------------|----------|
| D10 Full Rectify | Smooth, DC Block | 1 knob, 1 toggle |
| D11 Half Rectify | Threshold, Smooth, DC Block | 2 knobs, 1 toggle |

### Digital Types (D12-D14, D18-D19)

| Type | Parameters | Controls |
|------|------------|----------|
| D12 Bitcrush | Bit Depth, Dither, Mode, Jitter | 3 knobs, 1 dropdown |
| D13 Sample Reduce | Rate Ratio, Jitter, Mode, Smooth | 3 knobs, 1 dropdown |
| D14 Quantize | Levels, Dither, Smooth, Offset | 4 knobs |
| D18 Aliasing | Downsample, Freq Shift, Pre-Filter, Feedback, Resonance | 4 knobs, 1 toggle |
| D19 Bitwise | Operation, Intensity, Pattern, Bit Range, Smooth | 3 knobs, 1 dropdown, 1 range slider |

### Dynamic Type (D15)

| Type | Parameters | Controls |
|------|------------|----------|
| D15 Temporal | Mode, Sensitivity, Curve, Attack, Release, Depth, Lookahead, Hold | 6 knobs, 2 dropdowns |

### Hybrid Types (D16-D17, D26)

| Type | Parameters | Controls |
|------|------------|----------|
| D16 Ring Sat | Mod Depth, Stages, Curve, Carrier, Bias, Carrier Freq | 4 knobs, 2 dropdowns |
| D17 Feedback | Feedback, Delay, Curve, Filter, Filter Freq, Stages, Limiter, Limit Thresh | 5 knobs, 3 dropdowns, 1 toggle |
| D26 Allpass Res | Topology, Frequency, Feedback, Decay, Curve, Stages, Pitch Track, Damping | 5 knobs, 2 dropdowns, 1 toggle |

### Experimental Types (D20-D25)

| Type | Parameters | Controls |
|------|------------|----------|
| D20 Chaos | Attractor, Speed, Amount, Coupling, X-Drive, Y-Drive, Smooth, Seed | 7 knobs, 1 dropdown, 1 button |
| D21 Formant | Vowel, Shift, Curve, Resonance, Bandwidth, Formants, Gender, Blend | 6 knobs, 2 dropdowns |
| D22 Granular | Grain Size, Density, Pitch Var, Drive Var, Position, Curve, Envelope, Spread, Freeze | 7 knobs, 2 dropdowns, 1 toggle |
| D23 Spectral | Mode, FFT Size, Curve, Tilt, Thresh, Mag Bits, Freq Range, Phase Mode | 4 knobs, 3 dropdowns, 1 range slider |
| D24 Fractal | Mode, Iterations, Scale, Curve, Freq Decay, Feedback, Blend, Depth | 6 knobs, 2 dropdowns |
| D25 Stochastic | Base Curve, Jitter, Jitter Rate, Coeff Noise, Drift, Seed, Correlation, Smooth | 7 knobs, 1 dropdown, 1 button |

---

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*Fill this table when claiming completion. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

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
| SC-013 | | |
| SC-014 | | |

**Status Key:**
- MET: Requirement fully satisfied with test evidence
- NOT MET: Requirement not satisfied (spec is NOT complete)
- PARTIAL: Partially met with documented gap
- DEFERRED: Explicitly moved to future work with user approval

### Completion Checklist

*All items must be checked before claiming completion:*

- [ ] All FR-xxx requirements verified against implementation
- [ ] All SC-xxx success criteria measured and documented
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
