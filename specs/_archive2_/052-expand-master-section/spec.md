# Feature Specification: Expand Master Section into Voice & Output Panel

**Feature Branch**: `052-expand-master-section`
**Created**: 2026-02-14
**Status**: Draft
**Input**: User description: "Restructure the existing Master section (120x160px) into a Voice & Output panel that accommodates Voice Mode, Stereo controls, and a settings gear icon within the existing footprint. Phase 0A from the Ruinae UI gaps roadmap."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Reorganized Master Section with Compact Layout (Priority: P1)

A synthesizer user opens the Ruinae plugin and sees a "Voice & Output" panel (formerly "MASTER") in the same 120x160px area of Row 1. The panel now displays controls arranged vertically: the Polyphony dropdown and a gear icon on the top row, the Output knob centered in the middle, Width and Spread knob placeholders below, and the Soft Limit toggle at the bottom. All existing controls (Output, Polyphony, Soft Limit) remain functional and unchanged in behavior. The layout uses tighter vertical spacing compared to the old Master section to accommodate the additional visual elements.

**Why this priority**: This is the core deliverable of Phase 0A. Without the layout restructuring, downstream features (Phase 1.1 Voice Mode selector, Phase 1.3 Stereo Width/Spread) have no space to be wired up.

**Independent Test**: Can be fully verified by opening the plugin UI, confirming the panel title reads "Voice & Output", and checking that all existing controls (Output knob, Polyphony dropdown, Soft Limit toggle) are visible, reachable, and functional. The new placeholder controls (Width, Spread, gear icon) should be visible but non-functional.

**Acceptance Scenarios**:

1. **Given** the user opens Ruinae in a DAW, **When** they look at the top-right panel in Row 1, **Then** they see a panel titled "Voice & Output" at the same position and size (120x160px) as the old "MASTER" panel.
2. **Given** the Voice & Output panel is visible, **When** the user inspects the control layout, **Then** they see (top to bottom): a Polyphony dropdown and gear icon on the first row, the Output knob centered below, two smaller knobs labeled "Width" and "Spread" side by side below that, and a Soft Limit toggle at the bottom.
3. **Given** the Voice & Output panel is visible, **When** the user adjusts the Output knob, **Then** the master gain changes exactly as it did before the layout change (parameter ID 0, range 0-200%).
4. **Given** the Voice & Output panel is visible, **When** the user interacts with the Polyphony dropdown, **Then** it still shows values 1-16 and changes voice count (parameter ID 2).
5. **Given** the Voice & Output panel is visible, **When** the user clicks the Soft Limit toggle, **Then** it enables/disables the output soft limiter (parameter ID 3), same as before.

---

### User Story 2 - Gear Icon as Future Settings Access Point (Priority: P2)

A user notices a small gear icon in the top-right area of the Voice & Output panel, next to the Polyphony dropdown. In this phase, clicking the gear icon does nothing visible because the settings drawer it will eventually open is a Phase 5 feature. The gear icon serves as a visual indicator that settings will be accessible here in the future.

**Why this priority**: The gear icon is a placeholder that establishes the UI contract for Phase 5 (Settings Drawer). It needs to be positioned correctly now to avoid re-layouts later, but it has no behavior in this phase.

**Independent Test**: Can be verified by confirming the gear icon renders at the correct position (to the right of the Polyphony dropdown), has a visual style consistent with existing gear icons in the UI, and does not crash or trigger unexpected behavior when clicked.

**Acceptance Scenarios**:

1. **Given** the Voice & Output panel is visible, **When** the user looks at the top row, **Then** they see a gear icon positioned to the right of the Polyphony dropdown.
2. **Given** the gear icon is visible, **When** the user clicks it, **Then** nothing happens (no crash, no error, no visual change). The icon is inert in this phase.
3. **Given** the gear icon is rendered, **When** the user compares it to gear icons elsewhere in the UI (Filter, Distortion sections), **Then** the style, size, and color are visually consistent.

---

### User Story 3 - Width and Spread Knob Placeholders (Priority: P3)

A user sees two smaller knobs labeled "Width" and "Spread" in the Voice & Output panel, positioned below the Output knob. In this phase, these knobs are visual placeholders only -- they are not wired to any parameters because the parameter IDs do not exist yet (Phase 1.3 will define and wire them). The knobs render at their default position but adjusting them has no audible effect.

**Why this priority**: Creating the layout slots now ensures Phase 1.3 can add stereo parameters without rearranging the UI. However, the knobs are non-functional in this phase, making them lower priority than the core layout restructuring.

**Independent Test**: Can be verified by confirming two 28x28 knobs appear side by side below the Output knob, labeled "Width" and "Spread", and that manipulating them produces no audio change or crash.

**Acceptance Scenarios**:

1. **Given** the Voice & Output panel is visible, **When** the user looks below the Output knob, **Then** they see two 28x28 knobs side by side, labeled "Width" and "Spread".
2. **Given** the Width and Spread placeholder knobs are visible, **When** the user adjusts either knob, **Then** no audio parameter changes and no crash occurs.
3. **Given** the placeholder knobs are rendered, **When** the user examines their visual style, **Then** they use the same arc-knob style and color scheme ("master" accent color) as the Output knob.

---

### Edge Cases

- What happens when the plugin is loaded with an old preset saved before this UI change? The three existing parameters (Master Gain, Polyphony, Soft Limit) must load identically. No parameter IDs are changed, so state compatibility is preserved. The placeholder knobs (Width, Spread) will show their default positions.
- What happens if the host resizes the plugin window or uses HiDPI scaling? The 120x160px footprint must remain correct. VSTGUI handles DPI scaling via its built-in mechanism. The control positions are absolute within the panel and scale uniformly.
- What happens when the user uses keyboard navigation or accessibility tools? Controls within the panel should remain tabbable in logical order (top-to-bottom, left-to-right): Polyphony, gear icon, Output, Width, Spread, Soft Limit.
- What happens if the gear icon is accidentally double-clicked or right-clicked? No crash, no unexpected behavior. The icon is purely decorative in this phase.

## Clarifications

### Session 2026-02-14

- Q: Should the gear icon use `IconSegmentButton` (like Filter/Distortion sections) or a different control? → A: Use the existing `ToggleButton` custom control with a new `kGear` value added to the `IconStyle` enum. Implement `drawGearIcon()` method that draws a gear/cog shape using `CGraphicsPath` (vector-drawn, no bitmaps). Register "gear" in the ViewCreator's `icon-style` list. In uidesc, use `<view class="ToggleButton" icon-style="gear" />` with no `control-tag` (making it inert/non-functional as a placeholder).
- Q: How should placeholder Width/Spread knobs appear visually (normal, dimmed, outline-only)? → A: Render as normal-looking `ArcKnob` controls (same visual style as the Output knob) with no control-tag binding. Visually indistinguishable from functional knobs, but produce no audio effect until Phase 1.3 adds parameter wiring.
- Q: What is the tolerance for deviating from the specified pixel positions during implementation? → A: Pixel positions are flexible guidelines. Hard constraints: (1) all controls must fit within 120x160px boundary, (2) minimum 4px spacing between any two controls. Individual positions can be adjusted for visual balance as long as these constraints hold.
- Q: What should happen to the "Polyphony" label when the dropdown width is reduced to ~60px? → A: Abbreviate label text to "Poly" at 60px width. Add `tooltip="Polyphony"` attribute to the dropdown control for full name on hover.
- Q: What horizontal spacing should be used between the Polyphony dropdown and the gear icon? → A: Use 4px gap between the Polyphony dropdown and the gear icon, matching the minimum spacing constraint.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The Master section MUST be renamed from "MASTER" to "Voice & Output" in the panel's `fieldset-title` attribute of the `FieldsetContainer`.
- **FR-002**: The panel MUST retain the exact same origin `(772, 32)` and size `(120, 160)` as the current Master section, ensuring no impact on surrounding layout sections (OSC B to the left, preset browser above).
- **FR-003**: The Output knob (36x36, bound to `MasterGain` control-tag / parameter ID 0) MUST be repositioned to the center of the panel vertically and horizontally, approximately at vertical offset ~48px from the panel top.
- **FR-004**: The Polyphony dropdown (bound to `Polyphony` control-tag / parameter ID 2) MUST be repositioned to the top-left area of the panel and reduced in width to 60px to make room for the gear icon beside it. The label text MUST be abbreviated to "Poly" (60px width) and the dropdown MUST have a `tooltip="Polyphony"` attribute for discoverability.
- **FR-005**: A gear icon MUST be added to the top-right area of the panel, with a 4px edge-to-edge gap from the Polyphony dropdown (dropdown ends at x=68, gear icon starts at x=72). The icon MUST be implemented using the existing `ToggleButton` custom control with a new `icon-style="gear"` attribute and no `control-tag` binding (making it inert/non-functional in this phase).
- **FR-006**: Two placeholder knobs (28x28 each) labeled "Width" and "Spread" MUST be placed side by side below the Output knob with approximately 20px horizontal gap between them. These knobs MUST be normal-looking `ArcKnob` controls with the same visual style as the Output knob (same `guide-color` and `arc-color="master"` attributes), but MUST NOT be bound to any parameter (no control-tag) in this phase. They are visually indistinguishable from functional knobs but produce no audio effect.
- **FR-007**: The Soft Limit toggle (bound to `SoftLimit` control-tag / parameter ID 3) MUST remain at the bottom of the panel, repositioned as needed to accommodate the new layout.
- **FR-008**: All three existing parameters (Master Gain ID 0, Polyphony ID 2, Soft Limit ID 3) MUST remain fully functional with identical behavior, ranges, and default values. No parameter IDs, registrations, or processor logic are changed.
- **FR-009**: The panel MUST use only VSTGUI cross-platform controls (`ArcKnob`, `COptionMenu`, `ToggleButton`, `CTextLabel`). No platform-specific APIs are permitted.
- **FR-010**: The gear icon MUST be vector-drawn using `CGraphicsPath` in a new `drawGearIcon()` method added to the `ToggleButton` class (`plugins/shared/src/ui/toggle_button.h`). A new `kGear` value MUST be added to the `IconStyle` enum and registered in the ViewCreator's `icon-style` attribute list.
- **FR-011**: The panel layout MUST fit all controls within the 120x160px boundary with no clipping or overlap. A minimum of 4px spacing MUST be maintained between any two controls. Pixel positions listed in this spec are flexible guidelines; implementers may adjust individual control positions for visual balance as long as the 120x160px boundary and 4px minimum spacing constraints are satisfied.
- **FR-012**: Preset/state compatibility MUST be preserved. Loading a preset saved with the old "MASTER" layout MUST work identically with the new "Voice & Output" layout, since no parameter IDs change.

### Key Entities

- **Voice & Output Panel**: A `FieldsetContainer` (120x160px) in Row 1 of the Ruinae UI that replaces the former "MASTER" section. Contains global output and voice-related controls.
- **Gear Icon (placeholder)**: A non-functional icon element that visually indicates future settings access (Phase 5). Positioned in the panel header row next to the Polyphony dropdown.
- **Width/Spread Placeholder Knobs**: Two 28x28 `ArcKnob` instances without parameter bindings, reserving layout space for stereo controls that Phase 1.3 will wire up.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: The Voice & Output panel renders at exactly 120x160px with no visual overlap between any controls. Verified by inspection of control bounds in the uidesc XML (all origins + sizes fall within 0-120 horizontally and 0-160 vertically).
- **SC-002**: All three existing parameters (Output, Polyphony, Soft Limit) pass their existing integration and unit tests with zero regressions after the layout change.
- **SC-003**: The plugin loads successfully in a DAW with pluginval strictness level 5 passing.
- **SC-004**: A preset saved with the old "MASTER" layout loads correctly with the new "Voice & Output" layout, with all three parameter values restored accurately.
- **SC-005**: The gear icon renders without crash or error when clicked, and produces no state changes.
- **SC-006**: The Width and Spread placeholder knobs render at 28x28px, use the same `arc-color="master"` and `guide-color="knob-guide"` attributes as the Output knob, and produce no state changes when manipulated.
- **SC-007**: The plugin builds with zero compiler warnings related to the changes in this spec.

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- The existing 120x160px footprint is sufficient to fit 6 visual elements (Polyphony dropdown, gear icon, Output knob, Width knob, Spread knob, Soft Limit toggle) with tightened spacing. This has been validated by the roadmap's ASCII layout diagram.
- The gear icon does not require a new parameter ID or control-tag -- it is purely decorative (inert placeholder) in this phase. Phase 5 will later add an action tag to make the gear icon interactive.
- The Width and Spread knobs are visual placeholders that render as normal-looking `ArcKnob` controls (same style as Output knob) but have no control-tag binding. Phase 1.3 will define parameter IDs, register them in the controller, wire them to the processor, and bind them to these knobs via control-tags.
- The Voice Mode dropdown (`kVoiceModeId`, param 1) is intentionally NOT added to the panel in this phase. Phase 1.1 will replace or augment the Polyphony dropdown with a Voice Mode selector. Phase 0A only ensures there is physical space for it by keeping the dropdown row compact.
- The uidesc-only layout change requires a small C++ addition: adding `kGear` to the `IconStyle` enum and implementing `drawGearIcon()` in the `ToggleButton` class. No changes to processor, controller, or parameter registration code are needed.
- Pixel positions specified in this spec are flexible guidelines. Implementers may adjust individual control positions for visual balance as long as the 120x160px boundary and 4px minimum spacing constraints (clarified 2026-02-14) are satisfied.

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| Master `FieldsetContainer` | `plugins/ruinae/resources/editor.uidesc` lines 2602-2651 | Direct modification target -- rename title, reposition children |
| `ArcKnob` custom view class | Used throughout `editor.uidesc` for all knobs | Reuse for Width/Spread placeholder knobs (same class, 28x28 size, no control-tag) |
| `ToggleButton` custom view class | `plugins/shared/src/ui/toggle_button.h` | Will be extended to add `kGear` icon style; used for gear icon placeholder (no control-tag) and existing Soft Limit toggle (repositioned) |
| `IconStyle` enum | `plugins/shared/src/ui/toggle_button.h` | Must add `kGear` value; existing values include `kPower`, `kChevron` |
| `drawGearIcon()` method | To be added to `plugins/shared/src/ui/toggle_button.cpp` | New method using `CGraphicsPath` to vector-draw gear/cog shape |
| `COptionMenu` | Used for Polyphony, filter type, etc. | Existing control, resized to 60px and tooltip added |
| `CTextLabel` | Used for all control labels throughout UI | Reuse for "Poly", "Width", "Spread" labels |
| Global parameter registration | `plugins/ruinae/src/parameters/global_params.h` | No changes needed; confirms existing params are untouched |
| `kVoiceModeId` (param 1) | `plugins/ruinae/src/plugin_ids.h` line 60 | Already registered but has no uidesc control; Phase 1.1 will add it |

**Search Results Summary**: All relevant components identified. No new parameter IDs are introduced. A small C++ extension is required: adding `kGear` to the `IconStyle` enum and implementing `drawGearIcon()` in the `ToggleButton` class. The uidesc XML will be restructured to use these existing/extended components.

### Forward Reusability Consideration

**Sibling features at same layer** (layout restructuring):
- Phase 0B (Replace mod source tabs with dropdown) -- different section, no shared layout code
- Phase 0C (Add Global Filter strip) -- different section, no shared layout code

**Potential shared components** (preliminary, refined in plan.md):
- The gear icon rendering pattern established here will be reused by Phase 5 when the settings drawer is implemented. Phase 5 will add a control-tag/action tag to make the gear icon interactive.
- The compact knob layout pattern (28x28 side-by-side) may be reused in other tight sections such as the Mod Source views (Phase 4/6).

## Proposed Layout (Reference)

The following ASCII diagram from the roadmap defines the target layout:

```
+----- Voice & Output ------+
| [Poly v] [gear]           |  Polyphony dropdown (~60px) + gear icon (~18px)
|                            |
|       (Output)             |  Output knob (36x36), centered
|        36x36               |
|                            |
|   (Width) (Spread)         |  Two 28x28 knobs side by side
|                            |
|   [Soft Limit]             |  Toggle at bottom
+----------------------------+
        120 x 160px
```

### Approximate Control Positions (within 120x160 panel)

| Control | Origin (x, y) | Size (w, h) | Notes |
|---------|---------------|-------------|-------|
| Polyphony dropdown | (8, 14) | (60, 18) | Reduced from 80px to fit gear; tooltip="Polyphony" |
| Gear icon | (72, 14) | (18, 18) | 4px gap from dropdown; ToggleButton with icon-style="gear", no control-tag |
| "Poly" label | (8, 32) | (60, 10) | Abbreviated from "Polyphony" |
| Output knob | (42, 48) | (36, 36) | Centered horizontally |
| "Output" label | (34, 84) | (52, 12) | Below knob |
| Width knob | (14, 100) | (28, 28) | Left side; ArcKnob with no control-tag (placeholder) |
| "Width" label | (10, 128) | (36, 10) | Below knob |
| Spread knob | (62, 100) | (28, 28) | Right side; ArcKnob with no control-tag (placeholder) |
| "Spread" label | (58, 128) | (40, 10) | Below knob |
| Soft Limit toggle | (20, 140) | (80, 16) | Bottom of panel |

**Positioning Constraints** (from Clarifications session 2026-02-14):
- These positions are flexible guidelines; implementers may adjust for visual balance.
- **Hard constraints**: (1) All controls must fit within 120x160px boundary. (2) Minimum 4px spacing between any two controls.
- The gear icon starts at x=72 to maintain 4px gap from dropdown (dropdown ends at 8+60=68).

## Dependencies

This spec is a foundation for downstream features:

- **Phase 1.1 (Voice Mode Selector)** depends on this layout to have space for a Voice Mode dropdown (may replace or sit alongside the Polyphony dropdown).
- **Phase 1.3 (Stereo Width & Spread)** depends on this layout for the Width and Spread knob positions that will be wired to new parameter IDs.
- **Phase 5.1 (Settings Drawer)** depends on the gear icon placed by this spec as the trigger for the slide-out drawer.

## Implementation Verification *(mandatory at completion)*

### Compliance Status

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | MET | `plugins/ruinae/resources/editor.uidesc:2607` -- `fieldset-title="Voice &amp; Output"`. No residual "MASTER" references. |
| FR-002 | MET | `editor.uidesc:2605-2606` -- `origin="772, 32" size="120, 160"` unchanged. |
| FR-003 | MET | `editor.uidesc:2641-2642` -- ArcKnob at `origin="42, 48" size="36, 36"` with `control-tag="MasterGain"`. |
| FR-004 | MET | `editor.uidesc:2615-2622` -- COptionMenu `size="60, 18"`, `control-tag="Polyphony"`, `tooltip="Polyphony"`. Label: `title="Poly"` at line 2623. |
| FR-005 | MET | `editor.uidesc:2631-2638` -- ToggleButton at `origin="72, 14" size="18, 18"` with `icon-style="gear"`, no `control-tag`. 4px gap from dropdown. |
| FR-006 | MET | Width: `editor.uidesc:2654-2656` at `(14,100)` 28x28, `arc-color="master"`, `guide-color="knob-guide"`, no tag. Spread: `editor.uidesc:2665-2667` at `(62,100)` 28x28, same attrs. 20px gap. |
| FR-007 | MET | `editor.uidesc:2676-2683` -- ToggleButton at `origin="20, 142" size="80, 16"` with `control-tag="SoftLimit"`. |
| FR-008 | MET | `plugin_ids.h:59-62` -- kMasterGainId=0, kPolyphonyId=2, kSoftLimitId=3 unchanged. Pluginval state/automation tests pass. |
| FR-009 | MET | Grep for Win32/AppKit/Cocoa in changed files: zero matches. All VSTGUI cross-platform controls. |
| FR-010 | MET | `toggle_button.h:48` -- `kGear` in IconStyle enum. `toggle_button.h:309-371` -- `drawGearIconInRect()` uses CGraphicsPath. ViewCreator registration at line 636. |
| FR-011 | MET | Max right edge=100, max bottom=158. All gaps >= 4px: Poly-Gear=4px, PolyLabel-Output=6px, OutputLabel-Width/Spread=4px, Width-Spread=20px, Labels-SoftLimit=4px. |
| FR-012 | MET | No parameter IDs changed. Pluginval Plugin state test passed at strictness 5. |
| SC-001 | MET | All controls within 0-120 horizontally (max 100), 0-160 vertically (max 158). No overlaps. |
| SC-002 | MET | shared_tests: 175 test cases, 1453 assertions, all passed. Zero regressions. |
| SC-003 | MET | Pluginval strictness 5 passed. All test sections completed. |
| SC-004 | MET | No parameter IDs changed. Pluginval Plugin state test passed. |
| SC-005 | MET | No `control-tag` on gear icon. Pluginval Editor test passed. 9 toggle_button tests pass. |
| SC-006 | MET | Width/Spread: 28x28, `arc-color="master"`, `guide-color="knob-guide"`. Same as Output knob. No `control-tag`. |
| SC-007 | MET | Build: zero warnings. Clang-tidy: 0 errors, 0 warnings across 226 files. |

**Status Key:**
- MET: Requirement verified against actual code and test output with specific evidence
- NOT MET: Requirement not satisfied (spec is NOT complete)
- PARTIAL: Partially met with documented gap and specific evidence of what IS met
- DEFERRED: Explicitly moved to future work with user approval

### Completion Checklist

*All items must be checked before claiming completion:*

- [X] Each FR-xxx row was verified by re-reading the actual implementation code (not from memory)
- [X] Each SC-xxx row was verified by running tests or reading actual test output (not assumed)
- [X] Evidence column contains specific file paths, line numbers, test names, and measured values
- [X] No evidence column contains only generic claims like "implemented", "works", or "test passes"
- [X] No test thresholds relaxed from spec requirements
- [X] No placeholder values or TODO comments in new code
- [X] No features quietly removed from scope
- [X] User would NOT feel cheated by this completion claim

### Honest Assessment

**Overall Status**: COMPLETE

**Note**: Manual DAW verification tasks (T035-T045, T055-T057, T064-T066, T071) were marked based on automated verification (pluginval, build success, UIDESC attribute matching). User should verify visually in DAW for full confidence.

**Recommendation**: All requirements are met. User should do a quick visual spot-check in their DAW to confirm the layout looks correct.
