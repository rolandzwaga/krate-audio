# Feature Specification: Master Section Panel - Wire Voice & Output Controls

**Feature Branch**: `054-master-section-panel`
**Created**: 2026-02-14
**Status**: Draft
**Input**: User description: "Wire the Voice & Output panel with Voice Mode dropdown, Width/Spread parameter controls, replacing placeholders from spec 052 with functional parameters. Phase 0A completion + Phase 1.1 and 1.3 from the Ruinae UI roadmap."
**Roadmap Reference**: [ruinae-ui-roadmap.md](../ruinae-ui-roadmap.md) - Phase 1.1 (Voice Mode Selector) + Phase 1.3 (Stereo Width & Spread)

## Clarifications

### Session 2026-02-14

- Q: How should Voice Mode and Polyphony dropdowns be arranged in the top row given the 120px width constraint? → A: C (Vertical stack - Voice Mode on row 1 with gear icon, Polyphony on row 2 below it)
- Q: What denormalization formula should be used for the Width parameter (0-200% range)? → A: A (Linear scaling: norm * 2.0 = denorm, with display showing 0%, 100%, 200%)
- Q: Should the Width and Spread knobs have visible text labels or tooltips only? → A: A (Visible text labels below each knob - "Width" and "Spread" using CTextLabel controls)
- Q: What pattern should be used for backward-compatible EOF handling when loading old presets? → A: A (Check IBStream::read() return value, use defaults if read fails)
- Q: What label text should appear for the Voice Mode dropdown, and what menu items should it show? → A: Label "Mode", menu items "Polyphonic" and "Mono" (not "Poly")

## Context

Spec 052-expand-master-section (completed) restructured the Master section into a "Voice & Output" panel within the existing 120x160px footprint. It placed:
- The Polyphony dropdown and gear icon on the top row
- The Output knob centered
- Placeholder Width and Spread knobs (no control-tag, non-functional)
- Soft Limit toggle at the bottom

This spec completes the panel by wiring functional controls to the placeholder positions:
- Adding a **Voice Mode dropdown** (Poly/Mono) using the already-registered `kVoiceModeId` (1)
- Defining **two new global parameter IDs** (`kWidthId` = 4, `kSpreadId` = 5) for stereo Width and Spread
- Binding the placeholder Width and Spread knobs to these new parameters
- Wiring the new parameters through the processor to the engine's existing `setStereoWidth()` and `setStereoSpread()` methods

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Voice Mode Selection (Priority: P1)

A synthesizer user opens Ruinae and wants to switch between polyphonic and monophonic modes. In the Voice & Output panel, they see a dropdown labeled "Mode" (vertically stacked above the Polyphony dropdown) showing "Polyphonic" as the default. They click the dropdown and select "Mono". The synthesizer immediately behaves monophonically -- only one note sounds at a time. When they switch back to "Polyphonic", polyphonic playback resumes with the previously configured voice count.

**Why this priority**: Voice Mode selection is the gateway to mono mode (Phase 3), which adds portamento, legato, and priority controls. Without a Voice Mode selector in the UI, the already-registered kVoiceModeId parameter (ID 1) remains inaccessible to users. This is the highest-impact single control to add.

**Independent Test**: Can be fully tested by opening the plugin, selecting "Mono" from the Voice Mode dropdown, playing multiple MIDI notes, and confirming only one note sounds. Switching back to "Poly" and playing chords confirms polyphony returns.

**Acceptance Scenarios**:

1. **Given** the plugin UI is open and the Voice & Output panel is visible, **When** the user looks at row 1, **Then** they see a dropdown labeled "Mode" displaying "Polyphonic" (default value) with the gear icon to its right
2. **Given** the Mode dropdown is visible, **When** the user clicks it, **Then** it shows two options: "Polyphonic" and "Mono"
3. **Given** the user selects "Mono" from the Mode dropdown, **When** they play multiple MIDI notes, **Then** only one note sounds at a time (monophonic behavior)
4. **Given** the user has selected "Mono", **When** they switch back to "Polyphonic", **Then** polyphonic playback resumes and chords sound correctly
5. **Given** the Mode is set to "Polyphonic", **When** the user looks at row 2, **Then** the Polyphony dropdown is visible and functional (voice count 1-16)
6. **Given** the user saves a preset with Mode set to "Mono", **When** they reload that preset, **Then** Mode restores to "Mono"

---

### User Story 2 - Stereo Width Control (Priority: P2)

A sound designer wants to narrow or widen the stereo image of Ruinae's output. In the Voice & Output panel, they see a "Width" knob with a text label below the Output knob. Turning Width to its minimum collapses the output to mono. At the default center position (50%), the stereo image is unchanged. Turning it to maximum doubles the stereo effect for an extra-wide sound.

**Why this priority**: Stereo Width is a fundamental mixing tool. The DSP implementation (`setStereoWidth()` using Mid/Side processing) already exists in the engine but has no user-accessible control. Wiring it to a parameter and UI knob provides immediate creative value.

**Independent Test**: Can be tested by playing a stereo-panned patch, adjusting the Width knob from minimum to maximum, and listening for the stereo field narrowing to mono at 0% and widening beyond natural at 100%. The change should be audible in real-time.

**Acceptance Scenarios**:

1. **Given** the Voice & Output panel is visible, **When** the user looks below the Output knob, **Then** they see a functional "Width" knob (28x28, same visual style as Output knob) with a visible text label "Width" below it
2. **Given** a stereo signal is playing, **When** the user turns the Width knob to its minimum (0%), **Then** the output collapses to mono (left and right channels become identical)
3. **Given** a stereo signal is playing, **When** the user leaves Width at its default position (50%), **Then** the stereo image is unchanged from the natural mix
4. **Given** a stereo signal is playing, **When** the user turns Width to its maximum (100%), **Then** the stereo image is exaggerated beyond the natural width (extra-wide effect)
5. **Given** the Width parameter is automated, **When** automation plays back, **Then** the stereo width changes smoothly in real-time without clicks or artifacts

---

### User Story 3 - Stereo Spread Control (Priority: P2)

A sound designer wants to control how voices are distributed across the stereo field. In the Voice & Output panel, they see a "Spread" knob with a text label next to the Width knob. With Spread at minimum, all voices are panned to center. Increasing Spread distributes the voices evenly across the stereo field, creating a wider, more spacious sound. This is especially noticeable when multiple polyphonic voices are playing.

**Why this priority**: Stereo Spread complements Width by controlling voice panning distribution rather than Mid/Side balance. The DSP implementation (`setStereoSpread()`) already exists in the engine but has no user-accessible control. Together with Width, these two knobs give users complete stereo field control.

**Independent Test**: Can be tested by playing a chord (4+ notes) with Spread at 0% (all centered), then increasing Spread and listening for voices distributing across the stereo field.

**Acceptance Scenarios**:

1. **Given** the Voice & Output panel is visible, **When** the user looks below the Output knob, **Then** they see a functional "Spread" knob (28x28) to the right of the Width knob with a visible text label "Spread" below it
2. **Given** a polyphonic chord is playing, **When** the user turns the Spread knob to its minimum (0%), **Then** all voices are panned to the center (mono voice distribution)
3. **Given** a polyphonic chord is playing, **When** the user turns the Spread knob to its maximum (100%), **Then** voices are distributed evenly across the stereo field from left to right
4. **Given** the Spread parameter is automated, **When** automation plays back, **Then** the voice panning changes smoothly without clicks or artifacts
5. **Given** the synthesizer is in Mono mode, **When** the user adjusts the Spread knob, **Then** it has no audible effect (only one voice exists, so there is nothing to spread)

---

### User Story 4 - Layout Cohesion with Existing Panel (Priority: P3)

A user who was familiar with the Voice & Output panel from spec 052 opens the updated plugin. The panel looks visually consistent -- same position, same size, same accent color. The differences are: (1) a new Voice Mode dropdown appears alongside the Polyphony dropdown, and (2) the Width and Spread knobs now respond to user input and affect the audio output. The gear icon remains inert. The Soft Limit toggle continues to work as before.

**Why this priority**: Maintaining visual continuity with the spec 052 layout ensures users are not disoriented by the update. This is lower priority because the functional controls (Voice Mode, Width, Spread) deliver the core value.

**Independent Test**: Can be verified by comparing the panel visually before and after this spec's implementation, confirming the panel size, position, and visual style remain identical. The only observable differences should be the addition of the Voice Mode dropdown and the Width/Spread knobs becoming functional.

**Acceptance Scenarios**:

1. **Given** the user opens Ruinae, **When** they look at the Voice & Output panel, **Then** it is at the same position (772, 32) and size (120, 160) as before
2. **Given** the Voice & Output panel is visible, **When** the user inspects all controls, **Then** the Output knob, gear icon, and Soft Limit toggle behave identically to the spec 052 implementation
3. **Given** the user interacts with the Width or Spread knobs, **When** they look at the knob visuals, **Then** the knobs use the same `arc-color="master"` and `guide-color="knob-guide"` style as the Output knob

---

### Edge Cases

- What happens when a preset saved before this spec (with no Width/Spread parameter data) is loaded? Width defaults to 50% (natural stereo, engine value 1.0) and Spread defaults to 0% (all voices centered). The `loadGlobalParams()` function must handle missing fields gracefully by keeping defaults if the stream ends early.
- What happens when Voice Mode is set to Mono but the Width knob is at 0%? The output should be mono in both voice distribution and stereo image. Setting Width back to 50% restores the natural stereo image while remaining monophonic (single voice).
- What happens when Spread is at maximum but only one voice is playing (either Mono mode or single note in Poly mode)? There is no audible stereo spread effect because there is only one voice. This is expected behavior, not a bug.
- What happens when Width or Spread is modulated rapidly via automation? The engine methods are called per audio block, so changes apply smoothly at block boundaries. No allocation or locking occurs on the audio thread.
- What happens when the host reads/writes parameter state? Width and Spread must be saved/loaded alongside the existing global parameters. The state format changes (more bytes written), so `loadGlobalParams()` must handle old presets that lack these fields.
- What happens when Voice Mode is set to Mono and the user changes the Polyphony dropdown? The Polyphony dropdown remains visible but has no audible effect in Mono mode (the engine ignores polyphony count when in mono). This is expected behavior for this spec; Phase 3 will hide the Polyphony dropdown when Mono is selected.

## Requirements *(mandatory)*

### Functional Requirements

**Voice Mode Dropdown (Phase 1.1)**

- **FR-001**: A `control-tag` named `"VoiceMode"` with tag `"1"` MUST be added to the uidesc control-tags section, mapping to the already-registered `kVoiceModeId` parameter (ID 1).
- **FR-002**: A `COptionMenu` dropdown bound to the `"VoiceMode"` control-tag MUST be added to the Voice & Output panel's row 1 with a visible label "Mode" to its left. It MUST display two items: "Polyphonic" and "Mono". The default selection MUST be "Polyphonic" (index 0).
- **FR-003**: The Voice Mode dropdown and the Polyphony dropdown MUST both be visible in the panel using a vertical stack layout. Row 1 contains the Voice Mode dropdown with the gear icon to its right. Row 2 contains the Polyphony dropdown. Both rows MUST fit within the panel's 120px width and minimum 4px spacing between controls.
- **FR-004**: Selecting "Mono" from the Voice Mode dropdown MUST cause the synthesizer engine to operate in monophonic mode (single voice). Selecting "Poly" MUST restore polyphonic operation. This behavior already works via the existing `kVoiceModeId` parameter and `engine_.setMode()` wiring -- this spec only adds the UI control.

**Stereo Width Parameter (Phase 1.3)**

- **FR-005**: A new parameter ID `kWidthId = 4` MUST be added to the Global Parameters section (0-99) of `plugin_ids.h`.
- **FR-006**: The Width parameter MUST be registered in `registerGlobalParams()` as a continuous `Parameter` with range 0-200% (normalized 0.0-1.0 maps to 0.0-2.0 linear via formula: denorm = norm * 2.0), default value 0.5 (50% normalized = 1.0 linear = natural stereo width), and automatable.
- **FR-007**: A `control-tag` named `"Width"` with tag `"4"` MUST be added to the uidesc control-tags section.
- **FR-008**: The existing placeholder Width `ArcKnob` in the Voice & Output panel MUST be updated to include `control-tag="Width"` and `default-value="0.5"`, making it functional. A `CTextLabel` with text "Width" MUST be added below the knob.
- **FR-009**: The Width parameter MUST be handled in `handleGlobalParamChange()` to store the denormalized value (0.0-2.0, computed as norm * 2.0) in a new `std::atomic<float> width{1.0f}` field in `GlobalParams`.
- **FR-010**: The processor MUST forward the Width parameter value to `engine_.setStereoWidth()` in the per-block parameter update section.

**Stereo Spread Parameter (Phase 1.3)**

- **FR-011**: A new parameter ID `kSpreadId = 5` MUST be added to the Global Parameters section (0-99) of `plugin_ids.h`.
- **FR-012**: The Spread parameter MUST be registered in `registerGlobalParams()` as a continuous `Parameter` with range 0-100% (normalized 0.0-1.0 maps to 0.0-1.0 linear), default value 0.0 (0% = all voices centered), and automatable.
- **FR-013**: A `control-tag` named `"Spread"` with tag `"5"` MUST be added to the uidesc control-tags section.
- **FR-014**: The existing placeholder Spread `ArcKnob` in the Voice & Output panel MUST be updated to include `control-tag="Spread"` and `default-value="0"`, making it functional. A `CTextLabel` with text "Spread" MUST be added below the knob.
- **FR-015**: The Spread parameter MUST be handled in `handleGlobalParamChange()` to store the value (0.0-1.0) in a new `std::atomic<float> spread{0.0f}` field in `GlobalParams`.
- **FR-016**: The processor MUST forward the Spread parameter value to `engine_.setStereoSpread()` in the per-block parameter update section.

**State Persistence**

- **FR-017**: The Width and Spread parameters MUST be saved and loaded in `saveGlobalParams()` / `loadGlobalParams()` alongside existing global parameters.
- **FR-018**: Loading a preset saved before this spec (which lacks Width/Spread data) MUST NOT crash. The `loadGlobalParams()` function MUST check `IBStream::read()` return values (kResultOk vs error) and keep default values (Width=1.0, Spread=0.0) if read fails. The `loadGlobalParamsToController()` function MUST use the same pattern for EOF-safe reading.

**Display Formatting**

- **FR-019**: The Width parameter MUST display as a percentage (e.g., "100%") in host automation views and tooltips via `formatGlobalParam()`. 0.0 normalized displays as "0%", 0.5 normalized displays as "100%", 1.0 normalized displays as "200%".
- **FR-020**: The Spread parameter MUST display as a percentage (e.g., "50%") in host automation views and tooltips via `formatGlobalParam()`. 0.0 normalized displays as "0%", 1.0 normalized displays as "100%".

**Panel Layout Integrity**

- **FR-021**: The Voice & Output panel MUST remain at origin (772, 32) and size (120, 160). No surrounding sections are affected.
- **FR-022**: All controls MUST fit within the 120x160px panel boundary with minimum 4px spacing between controls. The addition of the Voice Mode dropdown MUST NOT cause any control to be clipped or overlap.
- **FR-023**: The Output knob, gear icon, and Soft Limit toggle MUST retain their existing behavior and parameter bindings (`MasterGain` ID 0, no tag for gear, `SoftLimit` ID 3).
- **FR-024**: The Voice Mode dropdown MUST have a visible `CTextLabel` displaying "Mode" positioned to the left of the dropdown control.
- **FR-025**: The Width and Spread knobs MUST each have a visible `CTextLabel` displaying "Width" and "Spread" respectively, positioned below their corresponding knobs.

### Key Entities

- **Width Parameter**: A global continuous parameter (ID 4) controlling the stereo width of the final output via Mid/Side processing. Range: 0% (mono) to 200% (extra-wide). Default: 100% (natural). Engine method: `setStereoWidth(float)` where 0.0=mono, 1.0=natural, 2.0=extra-wide.
- **Spread Parameter**: A global continuous parameter (ID 5) controlling the distribution of polyphonic voices across the stereo field. Range: 0% (all center) to 100% (fully spread). Default: 0% (center). Engine method: `setStereoSpread(float)` where 0.0=center, 1.0=fully spread.
- **Voice Mode Control**: A UI-bound dropdown for the existing `kVoiceModeId` parameter (ID 1) that was registered in the controller but had no uidesc control. Values: 0=Polyphonic, 1=Mono. Displayed with a "Mode" label to the left of the dropdown.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Users can switch between Poly and Mono voice modes via the Voice Mode dropdown, with monophonic behavior verified by playing multiple MIDI notes and hearing only one at a time.
- **SC-002**: Users can adjust the Width knob from minimum to maximum and hear the stereo field change from mono (0%) to natural (50%) to extra-wide (100%).
- **SC-003**: Users can adjust the Spread knob and hear polyphonic voices distribute across the stereo field (most noticeable with 4+ simultaneous notes).
- **SC-004**: All existing parameters (Output, Polyphony, Soft Limit) continue to function identically after the changes, verified by existing tests passing with zero regressions.
- **SC-005**: The plugin passes pluginval at strictness level 5, including state save/load tests with the new parameters.
- **SC-006**: A preset saved with the old format (pre-Width/Spread) loads correctly without crash, with Width defaulting to 100% and Spread defaulting to 0%.
- **SC-007**: The plugin builds with zero compiler warnings related to the changes in this spec.
- **SC-008**: Width and Spread parameters are automatable and display correct percentage values in host automation lanes.

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- The Voice Mode parameter (`kVoiceModeId` = 1) is already fully registered as a `StringListParameter` with values "Poly" and "Mono". The UI will display these as "Polyphonic" and "Mono" via the dropdown menu items. It is already saved/loaded in state persistence. It is already wired in the processor to `engine_.setMode()`. The only missing piece is a uidesc UI control with label.
- The engine's `setStereoWidth()` and `setStereoSpread()` methods are already implemented and tested. No DSP code changes are needed -- only parameter pipeline wiring (IDs, registration, handler, processor forwarding, uidesc binding).
- The parameter IDs 4 and 5 are available in the Global Parameters range (0-99). Currently only IDs 0-3 are used.
- The state format will grow by 2 floats (Width and Spread). Backward compatibility requires `loadGlobalParams()` to detect EOF and use defaults when reading old presets. The recommended approach is to attempt reading and return defaults on failure, following the pattern already used for optional state fields.
- The Voice Mode dropdown and Polyphony dropdown are arranged vertically: Voice Mode on row 1 with the gear icon to its right, Polyphony on row 2 below it. This vertical stack layout accommodates the 120px width constraint. Specific pixel positions are implementation details to be determined during planning, subject to the 120px width constraint and 4px minimum spacing rule.
- Phase 3 (Mono Mode conditional panel) is NOT part of this spec. When Voice Mode is set to Mono, the Polyphony dropdown remains visible but has no functional effect (since mono uses only 1 voice). Phase 3 will later hide the Polyphony dropdown and show Mono-specific controls (Priority, Legato, Portamento) in its place.

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| Voice & Output `FieldsetContainer` | `plugins/ruinae/resources/editor.uidesc:2607-2689` | Direct modification target -- add VoiceMode dropdown, bind Width/Spread knobs |
| `kVoiceModeId` (param 1) | `plugins/ruinae/src/plugin_ids.h:60` | Already registered, just needs uidesc control-tag and COptionMenu |
| `GlobalParams` struct | `plugins/ruinae/src/parameters/global_params.h:26-31` | Extend with `width` and `spread` atomic fields |
| `handleGlobalParamChange()` | `plugins/ruinae/src/parameters/global_params.h:37-67` | Extend with kWidthId and kSpreadId cases |
| `registerGlobalParams()` | `plugins/ruinae/src/parameters/global_params.h:73-100` | Extend with Width and Spread parameter registration |
| `formatGlobalParam()` | `plugins/ruinae/src/parameters/global_params.h:106-134` | Extend with Width and Spread display formatting |
| `saveGlobalParams()` / `loadGlobalParams()` | `plugins/ruinae/src/parameters/global_params.h:140-164` | Extend with Width/Spread persistence; backward-compat EOF handling |
| `loadGlobalParamsToController()` | `plugins/ruinae/src/parameters/global_params.h:170-185` | Extend with Width/Spread controller sync |
| Processor global param forwarding | `plugins/ruinae/src/processor/processor.cpp:627-632` | Extend with `engine_.setStereoWidth()` and `engine_.setStereoSpread()` calls |
| `RuinaeEngine::setStereoWidth()` | `plugins/ruinae/src/engine/ruinae_engine.h:309-312` | Already implemented (Mid/Side processing). No changes needed. |
| `RuinaeEngine::setStereoSpread()` | `plugins/ruinae/src/engine/ruinae_engine.h:300-304` | Already implemented (voice pan distribution). No changes needed. |
| Placeholder Width `ArcKnob` | `editor.uidesc:2659-2661` | Add `control-tag="Width"` and `default-value="0.5"` |
| Placeholder Spread `ArcKnob` | `editor.uidesc:2670-2672` | Add `control-tag="Spread"` and `default-value="0"` |
| `createDropdownParameter()` helper | `plugins/ruinae/src/controller/parameter_helpers.h` | Already used for VoiceMode registration; reference for dropdown pattern |
| Polyphony `COptionMenu` | `editor.uidesc:2620-2627` | Existing control, may need repositioning to fit alongside VoiceMode dropdown |
| Gear icon `ToggleButton` | `editor.uidesc:2636-2643` | Existing placeholder, position may shift to accommodate VoiceMode dropdown |
| Control-tags section | `editor.uidesc:63-65` (MasterGain, Polyphony, SoftLimit) | Add VoiceMode (tag 1), Width (tag 4), Spread (tag 5) |

**Initial codebase search for key terms:**

```bash
grep -r "kWidthId\|kSpreadId" plugins/ruinae/src/plugin_ids.h
# Result: No global kWidthId or kSpreadId (only effect-specific like kDelayDigitalWidthId)

grep -r "setStereoWidth\|setStereoSpread" plugins/ruinae/src/processor/
# Result: No matches -- engine methods exist but are not called from processor

grep -r "VoiceMode" plugins/ruinae/resources/editor.uidesc
# Result: No matches -- no control-tag or COptionMenu for VoiceMode exists
```

**Search Results Summary**: No global `kWidthId` or `kSpreadId` exist (only effect-specific Width/Spread IDs like `kDelayDigitalWidthId`). The processor does NOT call `setStereoWidth()` or `setStereoSpread()` -- these engine methods exist but are unwired. No "VoiceMode" control-tag or COptionMenu exists in the uidesc. All three are confirmed gaps that this spec fills.

### Forward Reusability Consideration

**Sibling features at same layer** (if known):
- Phase 3 (Mono Mode Panel) depends on the Voice Mode dropdown added here. When mono is selected, Phase 3 will conditionally show Portamento/Legato/Priority controls in place of the Polyphony dropdown.
- Phase 5 (Settings Drawer) depends on the gear icon already placed by spec 052. No changes to the gear icon are needed in this spec.

**Potential shared components** (preliminary, refined in plan.md):
- The backward-compatible state loading pattern (EOF detection for new fields) established here will be reused by any future spec that adds global parameters to `GlobalParams`.
- The Width/Spread parameter wiring pattern is a straightforward "parameter -> atomic -> engine setter" pipeline, identical to the existing MasterGain pattern. No new infrastructure is needed.

## Dependencies

This spec depends on:
- **052-expand-master-section** (completed): Provides the panel layout with placeholder knobs and gear icon

This spec enables:
- **Phase 3 (Mono Mode Panel)**: Requires the Voice Mode dropdown to exist so users can select Mono and trigger conditional mono controls
- **Future modulation targets**: Width and Spread become available as modulation destinations in the mod matrix once their parameter IDs exist

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
| SC-001 | | |
| SC-002 | | |
| SC-003 | | |
| SC-004 | | |
| SC-005 | | |
| SC-006 | | |
| SC-007 | | |
| SC-008 | | |

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
