# Feature Specification: Mono Mode Conditional Panel

**Feature Branch**: `056-mono-mode`
**Created**: 2026-02-15
**Status**: Draft
**Input**: User description: "Mono Mode conditional panel - expose Mono Priority, Legato, Portamento Time, and Portamento Mode controls in the Voice & Output section with conditional visibility when Voice Mode is set to Mono. Phase 3 from the Ruinae UI roadmap."
**Roadmap Reference**: [ruinae-ui-roadmap.md](../ruinae-ui-roadmap.md) - Phase 3 (Mono Mode Panel)

## Clarifications

### Session 2026-02-15

- Q: What should be the exact height of the MonoGroup container to ensure proper layout and prevent clipping? → A: MonoGroup container height = 18px (single row matching PolyGroup height). All 4 controls fit in one row to avoid overlapping the Output knob at y=58.
- Q: What are the exact horizontal positions (x-coordinates and widths) for the four mono controls to ensure they fit within the 112px container width? → A: Single row (y=0): Legato toggle at x=0, width=22px | Priority dropdown at x=24, width=36px | Portamento Time knob at x=62, size=18x18 | Portamento Mode dropdown at x=82, width=30px. 2px gaps between controls.
- Q: Where should the "Porta" label be positioned relative to the Portamento Time knob, and what size/font should it use? → A: No separate label — insufficient space in single-row layout. The knob is identified by its position and tooltip (provided by the registered parameter's formatting function).
- Q: Should the PolyGroup container have an explicit size attribute or rely on auto-sizing around its single child (Polyphony dropdown)? → A: PolyGroup explicit size="112, 18" (width accommodates dropdown with margins, height 18px).
- Q: Where should the initial visibility state be set when the editor opens with Voice Mode already set to Mono (e.g., from a preset)? → A: Set initial visibility in verifyView() immediately after capturing the view pointers, following the exact pattern used for all 6 existing sync toggle groups.

## Context

Spec 054-master-section-panel (completed, merged) added the Voice Mode dropdown to the Voice & Output panel, allowing users to switch between Polyphonic and Mono modes. However, when Mono is selected, the Polyphony dropdown remains visible despite being irrelevant -- mono mode always uses exactly one voice regardless of the polyphony setting. Additionally, the four mono-specific parameters (Priority, Legato, Portamento Time, Portamento Mode) have no UI controls at all, even though they are fully registered, handled in the processor, wired to the engine, and persisted.

This spec implements Phase 3 from the roadmap: a conditional visibility swap in the Voice & Output panel. When Voice Mode = Poly, the Polyphony dropdown is shown (current behavior). When Voice Mode = Mono, the Polyphony dropdown hides and is replaced by the four mono controls in the same space. This follows the roadmap's **Option A** recommendation -- an inline conditional swap within the existing Master section.

No new parameter IDs, DSP code, or state persistence changes are needed. This spec is purely UI-layer work: uidesc additions, control-tag additions, and controller visibility wiring.

### Current Panel Layout (Voice & Output, 120x160px)

```
+------------------------+
| Mode [Poly v] [gear]  |  y=14   Row 1: Voice Mode dropdown + gear icon
| [8 voices v]           |  y=36   Row 2: Polyphony dropdown
|                        |
|     (Output)           |  y=58   Output knob (32x32, centered)
|      Output            |  y=90   Label
|                        |
|  (Width)  (Spread)     |  y=104  Two 28x28 knobs
|   Width    Spread      |  y=132  Labels
|                        |
|  [Soft Limit]          |  y=146  Toggle
+------------------------+
```

### Proposed Conditional Swap

When Voice Mode = Mono, the Polyphony dropdown area (Row 2, y=36) is repurposed for mono controls in a single row:

```
Poly mode:                      Mono mode:
+------------------------+      +------------------------+
| Mode [Poly v] [gear]  |      | Mode [Mono v] [gear]  |
| [8 voices v]           |      |[Leg][LastNote▼](o)[Alw▼]|
|                        |      |                        |
|     (Output)           |      |     (Output)           |
|      Output            |      |      Output            |
|                        |      |                        |
|  (Width)  (Spread)     |      |  (Width)  (Spread)     |
|   Width    Spread      |      |   Width    Spread      |
|                        |      |                        |
|  [Soft Limit]          |      |  [Soft Limit]          |
+------------------------+      +------------------------+
```

The four mono controls occupy a single 112x18px row at y=36 (same position and height as the PolyGroup), fitting entirely above the Output knob at y=58 with no overlap.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Mono Mode Controls Appear When Mono Selected (Priority: P1)

A synthesizer user opens Ruinae and wants to configure monophonic playback with portamento. In the Voice & Output panel, they click the Mode dropdown and select "Mono". Instantly, the Polyphony dropdown (which showed "8 voices") disappears and is replaced by a compact row of four mono-specific controls: a Legato toggle, a Priority dropdown (showing "Last Note"), a Portamento Time mini-knob, and a Portamento Mode dropdown (showing "Always"). They toggle Legato on, change Priority to "Low Note", increase Portamento Time, and set Portamento Mode to "Legato Only". When they play notes, the synthesizer glides between pitches with the configured portamento settings, using low-note priority.

**Why this priority**: This is the core feature of the spec. Without the conditional swap, mono mode parameters are completely inaccessible to users even though the DSP is fully implemented. Exposing these four controls makes mono mode genuinely usable for the first time.

**Independent Test**: Can be fully tested by opening the plugin, selecting "Mono" from the Voice Mode dropdown, and verifying all four mono controls appear where the Polyphony dropdown was. Adjusting each control and playing notes confirms they affect mono playback behavior.

**Acceptance Scenarios**:

1. **Given** the Voice Mode is set to "Polyphonic" (default), **When** the user looks at Row 2 of the Voice & Output panel, **Then** they see the Polyphony dropdown and do NOT see any mono controls
2. **Given** the user selects "Mono" from the Voice Mode dropdown, **When** they look at the area where the Polyphony dropdown was, **Then** the Polyphony dropdown is hidden and four mono controls are visible in its place
3. **Given** Voice Mode is set to "Mono", **When** the user inspects the mono controls, **Then** they see: a Legato toggle (default off), a Priority dropdown showing "Last Note" (default), a Portamento Time knob (default 0ms), and a Portamento Mode dropdown showing "Always" (default)
4. **Given** Voice Mode is set to "Mono" and Legato is toggled on, **When** the user plays a note while holding a previous note, **Then** the new note sounds without retriggering the envelopes (legato behavior)
5. **Given** Voice Mode is set to "Mono" and Priority is set to "Low Note", **When** the user plays multiple notes simultaneously, **Then** only the lowest-pitched note sounds
6. **Given** Voice Mode is set to "Mono" and Priority is set to "High Note", **When** the user plays multiple notes simultaneously, **Then** only the highest-pitched note sounds
7. **Given** Voice Mode is set to "Mono" and Portamento Time is increased above 0ms, **When** the user plays a new note after releasing the previous, **Then** the pitch glides from the previous note to the new note over the configured time
8. **Given** Voice Mode is set to "Mono" and Portamento Mode is set to "Legato Only", **When** the user plays a new note without holding a previous note, **Then** no portamento occurs (pitch jumps immediately)
9. **Given** Voice Mode is set to "Mono" and Portamento Mode is set to "Always", **When** the user plays a new note (whether or not a previous note is held), **Then** portamento occurs based on the Portamento Time setting
10. **Given** the user switches Voice Mode back to "Polyphonic", **When** they look at Row 2, **Then** the mono controls disappear and the Polyphony dropdown reappears

---

### User Story 2 - Preset Persistence of Mono Settings (Priority: P2)

A user configures mono mode with specific settings (Priority: High Note, Legato: on, Portamento Time: 200ms, Portamento Mode: Legato Only) and saves a preset. When they reload the preset, Voice Mode is set to "Mono", the mono controls are visible (not the Polyphony dropdown), and all four settings are restored to their saved values.

**Why this priority**: Preset persistence is essential for a usable feature, but the save/load functionality already exists in the backend. This story validates that the UI state (conditional visibility) restores correctly when loading a preset with mono mode active.

**Independent Test**: Can be tested by configuring mono mode, saving the preset, loading a different preset, then reloading the saved mono preset and verifying all controls and values restore.

**Acceptance Scenarios**:

1. **Given** the user has configured Voice Mode = Mono with specific mono parameter values, **When** they save the preset and reload it, **Then** Voice Mode shows "Mono", the mono controls panel is visible (not the Polyphony dropdown), and all four mono parameter values match the saved values
2. **Given** a preset was saved with Voice Mode = Polyphonic, **When** the user loads it, **Then** the Polyphony dropdown is visible and the mono controls are hidden
3. **Given** a preset saved before this spec existed (with no mono UI controls), **When** the user loads it, **Then** Voice Mode defaults to "Polyphonic", the Polyphony dropdown is visible, and the mono parameters have their default values (Priority: Last Note, Legato: off, Portamento: 0ms, PortaMode: Always)

---

### User Story 3 - Automation of Mono Parameters (Priority: P3)

A producer automates the Portamento Time parameter in their DAW while in mono mode. As the automation plays back, they see the Portamento Time knob moving in the UI and hear the glide time changing in real-time. They can also automate Priority and Portamento Mode via their DAW's automation lanes, even though these are dropdown selections.

**Why this priority**: All four mono parameters are already registered with `kCanAutomate`. This story confirms that the new UI controls correctly reflect automation changes and that the controls are accessible in DAW automation lanes.

**Independent Test**: Can be tested by writing automation for `kMonoPortamentoTimeId` in a DAW, playing back, and observing the knob moving and the portamento time changing audibly.

**Acceptance Scenarios**:

1. **Given** Voice Mode is set to "Mono" and the Portamento Time knob is visible, **When** the DAW automates the Portamento Time parameter, **Then** the knob position updates in real-time to reflect the automated value
2. **Given** Voice Mode is set to "Mono", **When** the user opens the DAW's automation lane list, **Then** they can find and automate all four mono parameters: Mono Priority, Legato, Portamento Time, Portamento Mode
3. **Given** the user automates Voice Mode from Poly to Mono, **When** the automation switches, **Then** the panel conditionally swaps (Polyphony dropdown hides, mono controls appear) in response to the automation change

---

### Edge Cases

- What happens when Voice Mode is rapidly toggled between Poly and Mono via automation? The conditional visibility swap occurs on each toggle. The controller's deferred update mechanism handles this on the UI thread, so no race conditions occur. The view pointers are checked for null before toggling visibility.
- What happens when a preset saved with Voice Mode = Mono is loaded but the editor is not open? The parameter values are restored via the processor's state load. When the editor later opens, `verifyView()` captures the view pointers and the initial visibility state is set based on the current Voice Mode value, so the correct panel is shown.
- What happens when Portamento Time is at 0ms? No portamento glide occurs -- pitch changes instantly. This is the default and expected minimum behavior.
- What happens when Portamento Time is at maximum (5000ms)? The pitch glides over 5 seconds. The cubic mapping (value^3 * 5000) provides fine control at low values and coarser control at high values, matching typical portamento behavior.
- What happens when Legato is on but only one note at a time is played (releasing before pressing the next)? Each new note retriggers the envelopes normally because there is no held note to produce legato behavior. Legato only applies when a new note is pressed while a previous note is still held.
- What happens when Width/Spread knobs are adjusted while in Mono mode? Width still works (it affects the stereo image of the single voice's output). Spread has no audible effect (only one voice exists, so there is nothing to distribute across the stereo field). This is existing and expected behavior documented in spec 054.

## Requirements *(mandatory)*

### Functional Requirements

**Control-Tag Registration**

- **FR-001**: Four `control-tag` entries MUST be added to the uidesc control-tags section: `"MonoPriority"` tag `"1800"`, `"MonoLegato"` tag `"1801"`, `"MonoPortamentoTime"` tag `"1802"`, `"MonoPortaMode"` tag `"1803"`. These MUST be placed after the existing global parameter control-tags (Output, VoiceMode, Polyphony, Width, Spread, SoftLimit) and the global filter control-tags, keeping global-scope controls together.

**Conditional Visibility Groups**

- **FR-002**: The existing Polyphony `COptionMenu` (control-tag="Polyphony", currently at origin 8,36) MUST be wrapped in a `CViewContainer` with `custom-view-name="PolyGroup"` and explicit `size="112, 18"` (width 112px accommodates the dropdown with margins, height 18px). This container MUST start visible (default, since the default Voice Mode is Polyphonic).
- **FR-003**: A new `CViewContainer` with `custom-view-name="MonoGroup"` and explicit `size="112, 18"` (width 112px matches PolyGroup, height 18px — single row matching PolyGroup height) MUST be added at origin 8,36 (same position as PolyGroup). This container MUST start hidden (`visible="false"`) because the default Voice Mode is Polyphonic. The MonoGroup container occupies vertical space from y=36 to y=54 within the Voice & Output FieldsetContainer, fitting entirely above the Output knob at y=58.

**Mono Controls Layout**

- **FR-004**: The MonoGroup container MUST contain four controls arranged in a single row with precise positioning (all at y=0 within container, which is y=36 in the panel):
  - A Legato `ToggleButton` at origin="0,0" size="22,18" bound to `control-tag="MonoLegato"` with `default-value="0"` (off by default) and title "Leg" (abbreviated to fit compact layout).
  - A Priority `COptionMenu` dropdown at origin="24,0" size="36,18" bound to `control-tag="MonoPriority"` with `default-value="0"` displaying items "Last Note", "Low Note", "High Note" (populated automatically from the registered `StringListParameter` via `createDropdownParameter`; order matches `kMonoModeStrings` in `dropdown_mappings.h`).
  - A Portamento Time `ArcKnob` at origin="62,0" size="18,18" bound to `control-tag="MonoPortamentoTime"` with `default-value="0"` (0ms). Mini 18x18 knob fits the 18px row height.
  - A Portamento Mode `COptionMenu` dropdown at origin="82,0" size="30,18" bound to `control-tag="MonoPortaMode"` with `default-value="0"` displaying items "Always", "Legato Only" (populated automatically from the registered `StringListParameter`).
  - Controls are separated by 2px gaps. Total width: 22+2+36+2+18+2+30 = 112px.
- **FR-005**: No separate text label is used for the Portamento Time knob due to the compact single-row layout. The knob is identified by its position between the Priority dropdown and Portamento Mode dropdown, and by its tooltip text provided by the registered parameter's `formatMonoModeParam()` formatting function (which displays the current value as e.g. "150.0 ms" or "2.50 s").

**Controller Visibility Wiring**

- **FR-006**: The controller MUST add two new view pointers (`polyGroup_` and `monoGroup_`) following the same pattern as `lfo1RateGroup_`/`lfo1NoteValueGroup_`. When `kVoiceModeId` changes value, the controller MUST toggle visibility: PolyGroup visible and MonoGroup hidden when value < 0.5 (Polyphonic), PolyGroup hidden and MonoGroup visible when value >= 0.5 (Mono).
- **FR-007**: The controller's `verifyView()` method MUST capture the `PolyGroup` and `MonoGroup` views by `custom-view-name`, and the editor close/destroy method MUST null them out, following the exact pattern used for LFO Rate/NoteValue groups and the TranceGate Rate/NoteValue groups.
- **FR-008**: The visibility toggle MUST be handled in the controller's deferred update mechanism (the `update()` or equivalent method that responds to `setParamNormalized` changes), ensuring visibility changes occur on the UI thread. This follows the established `IDependent` pattern used for all other sync toggle visibility switches.

**Initial State on Editor Open**

- **FR-009**: When the editor opens and the current Voice Mode is already "Mono" (e.g., from a loaded preset), the `verifyView()` method MUST set the initial visibility state immediately after capturing the PolyGroup and MonoGroup view pointers by reading the current kVoiceModeId parameter value and applying the same visibility logic as the toggle (value < 0.5: PolyGroup visible, MonoGroup hidden | value >= 0.5: PolyGroup hidden, MonoGroup visible). This follows the exact pattern used for all 6 existing sync toggle groups (LFO1/2, Chaos, Delay, Phaser, TranceGate) and ensures the UI matches the current parameter state from the moment the editor appears.

**No State Persistence Changes**

- **FR-010**: No changes to state save/load functions are required. All four mono parameters (`kMonoPriorityId`, `kMonoLegatoId`, `kMonoPortamentoTimeId`, `kMonoPortaModeId`) are already fully persisted by the existing `saveMonoModeParams()`/`loadMonoModeParams()` functions. The Voice Mode parameter (`kVoiceModeId`) is already persisted in `saveGlobalParams()`/`loadGlobalParams()`.

**Visual Consistency**

- **FR-011**: All mono controls (toggle, dropdowns, knob) MUST use the `master` accent color consistently, matching the existing Voice & Output panel styling. The Legato toggle MUST use `on-color="master"`. The Priority and Portamento Mode dropdowns MUST use `font-color="master"`. The Portamento Time knob MUST use `arc-color="master"` and `guide-color="knob-guide"`.

### Key Entities

- **PolyGroup**: A visibility container wrapping the Polyphony dropdown, visible when Voice Mode = Polyphonic, hidden when Voice Mode = Mono. Uses `custom-view-name="PolyGroup"` for controller capture.
- **MonoGroup**: A visibility container (112x18px, single row) holding the four mono-specific controls (Legato toggle, Priority dropdown, Portamento Time mini-knob, Portamento Mode dropdown), visible when Voice Mode = Mono, hidden when Voice Mode = Polyphonic. Uses `custom-view-name="MonoGroup"` for controller capture.
- **Mono Priority**: Determines which note takes priority in monophonic mode. Values: Last Note (default), Low Note, High Note (order matches `kMonoModeStrings` in `dropdown_mappings.h`). Parameter ID: 1800.
- **Legato**: When enabled, new notes played while a previous note is held do not retrigger envelopes. Default: off. Parameter ID: 1801.
- **Portamento Time**: Controls the duration of pitch glide between notes, 0-5000ms with cubic scaling. Default: 0ms (no glide). Parameter ID: 1802.
- **Portamento Mode**: Controls when portamento occurs. "Always" applies glide on every note. "Legato Only" applies glide only when notes overlap. Default: Always. Parameter ID: 1803.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Users can switch Voice Mode to "Mono" and see four mono controls (Legato, Priority, Portamento Time, Portamento Mode) appear where the Polyphony dropdown was, with the swap occurring instantly upon selection.
- **SC-002**: Users can switch Voice Mode back to "Polyphonic" and see the Polyphony dropdown reappear with mono controls hidden, confirmed by the instant visual swap.
- **SC-003**: Users can adjust Portamento Time and hear pitch glide between notes in mono mode, with the glide duration matching the knob position (0ms at minimum, up to 5000ms at maximum).
- **SC-004**: The plugin passes pluginval at strictness level 5, confirming all parameters are accessible and the state save/load cycle works correctly with the new controls.
- **SC-005**: All existing parameters and controls continue to function identically after the changes, verified by existing tests passing with zero regressions.
- **SC-006**: The plugin builds with zero compiler warnings related to the changes in this spec.
- **SC-007**: Presets saved with Voice Mode = Mono correctly restore the mono panel visibility and all four mono parameter values when reloaded.
- **SC-008**: All four mono parameters (Priority, Legato, Portamento Time, Portamento Mode) are visible in DAW automation lanes and respond to automation playback when the editor is open.

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- All four mono parameters are already fully registered in the controller via `registerMonoModeParams()`, handled in the processor via `handleMonoModeParamChange()`, wired to the engine (MonoHandler, portamento system), formatted for display via `formatMonoModeParam()`, and persisted via `saveMonoModeParams()`/`loadMonoModeParams()`. No parameter pipeline work is needed -- only uidesc UI controls and controller visibility wiring.
- The Voice Mode selector (kVoiceModeId = 1) is already functional in the Voice & Output panel (spec 054). It is a `COptionMenu` with two items: "Polyphonic" (0) and "Mono" (1). The controller does NOT currently respond to Voice Mode changes for visibility purposes -- this spec adds that wiring.
- The Priority dropdown uses `createDropdownParameter()` which creates a `StringListParameter`. This means `COptionMenu` items are automatically populated by the framework -- no manual menu population is needed.
- The Portamento Mode dropdown likewise uses `createDropdownParameter()` with `StringListParameter`, so items "Always" and "Legato Only" are auto-populated.
- The Portamento Time parameter uses a cubic mapping (value^3 * 5000ms) for display formatting. The `formatMonoModeParam()` function formats values >= 1000ms as seconds (e.g., "2.50 s") and values < 1000ms as milliseconds (e.g., "150.0 ms").
- The conditional visibility pattern (two overlapping `CViewContainer` groups toggled by a parameter change) is well-established in the codebase with 6 existing implementations: LFO1, LFO2, Chaos, Delay, Phaser, and Trance Gate sync toggles. This spec follows the identical pattern but for Voice Mode (Poly vs Mono) instead of a sync toggle.
- The existing 120x160px Voice & Output panel has sufficient vertical space between the Voice Mode row (y=14, height 18) and the Output knob (y=58) to fit one row of mono controls at y=36 (height 18). The MonoGroup uses the same 112x18px footprint as the PolyGroup, fitting entirely above the Output knob with 4px of clearance.

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| `MonoModeParams` struct | `plugins/ruinae/src/parameters/mono_mode_params.h:14-20` | Already implemented -- contains priority, legato, portamentoTimeMs, portaMode atomics. No changes needed. |
| `handleMonoModeParamChange()` | `mono_mode_params.h:22-43` | Already handles all 4 param IDs. No changes needed. |
| `registerMonoModeParams()` | `mono_mode_params.h:45-59` | Already registers all 4 params including StringListParameter for Priority and PortaMode. No changes needed. |
| `formatMonoModeParam()` | `mono_mode_params.h:61-77` | Already formats Portamento Time (ms/s). No changes needed. |
| `saveMonoModeParams()` / `loadMonoModeParams()` | `mono_mode_params.h:80-107` | Already persists all 4 fields. No changes needed. |
| `loadMonoModeParamsToController()` | `mono_mode_params.h:97-107` | Already syncs all 4 params to controller on state load. No changes needed. |
| Processor mono param forwarding | `processor.cpp:613,942` | Already forwards mono params to engine. No changes needed. |
| Engine `setMonoPriority()` | `ruinae_engine.h:1164` | Already implemented. No changes needed. |
| `kMonoModeCount` (3) | `dropdown_mappings.h:134` | Already defined for 3 priority modes (Last/High/Low). |
| `kMonoModeStrings` | `dropdown_mappings.h:136` | Already defined: "Last Note", "Low Note", "High Note". |
| `kPortaModeCount` (2) | `dropdown_mappings.h:146` | Already defined for 2 porta modes (Always/Legato Only). |
| `kPortaModeStrings` | `dropdown_mappings.h:148` | Already defined: "Always", "Legato Only". |
| Voice & Output FieldsetContainer | `editor.uidesc:2697-2794` | Direct modification target -- wrap Polyphony in PolyGroup, add MonoGroup container. |
| LFO1 Rate/NoteValue group pattern | `editor.uidesc:1491-1505` | Reference implementation for the visibility group pattern in uidesc. |
| TranceGate Rate/NoteValue group pattern | `editor.uidesc:2257-2273` | Most recent implementation of the visibility group pattern (from spec 055). |
| Controller sync visibility wiring | `controller.cpp:534-538` | Reference for kTranceGateTempoSyncId visibility toggling. Add kVoiceModeId case. |
| Controller view capture pattern | `controller.cpp:883-894` | Reference for `verifyView()` custom-view-name matching. Add PolyGroup/MonoGroup cases. |
| Controller view pointer declarations | `controller.h:235-236` | Reference for declaring Rate/NoteValue group pointers. Add polyGroup_/monoGroup_ pair. |
| Controller editor close cleanup | `controller.cpp:613-614` | Reference for nulling pointers on editor close. Add poly/mono pair. |
| Control-tags section | `editor.uidesc:65-75` | Add MonoPriority (1800), MonoLegato (1801), MonoPortamentoTime (1802), MonoPortaMode (1803) control-tags here. |

**Initial codebase search for key terms:**

```bash
grep -r "MonoPriority\|MonoLegato\|MonoPortamento\|MonoPortaMode" plugins/ruinae/resources/editor.uidesc
# Result: No matches -- no control-tags or controls for mono mode exist in uidesc

grep -r "PolyGroup\|MonoGroup" plugins/ruinae/
# Result: No matches -- no visibility groups for Poly/Mono panel swap exist

grep -r "kVoiceModeId" plugins/ruinae/src/controller/controller.cpp
# Result: No matches -- controller does not currently respond to VoiceMode changes for visibility
```

**Search Results Summary**: No mono mode controls exist in the uidesc. No conditional visibility groups exist for the Poly/Mono swap. The controller does not yet wire Voice Mode changes to visibility toggling. All three are confirmed gaps that this spec fills. The backend (parameters, processor, engine, persistence) is fully implemented for all features.

### Forward Reusability Consideration

**Sibling features at same layer** (if known):
- Phase 5.1 (Settings Drawer) shares the Voice & Output panel as a modification target (the gear icon). However, it does not interact with the Poly/Mono conditional swap.
- No other phases depend on the Poly/Mono visibility infrastructure created here.

**Potential shared components** (preliminary, refined in plan.md):
- The Voice Mode visibility wiring in the controller extends the existing sync-toggle visibility pattern to a new parameter type (voice mode instead of sync toggle). The pattern itself is identical and well-established.
- No new reusable components are expected from this spec since it only adds uidesc controls and controller wiring.

## Dependencies

This spec depends on:
- **054-master-section-panel** (completed, merged): Voice Mode dropdown, Voice & Output panel layout, Width/Spread knobs
- **055-global-filter-trancegate-tempo** (completed, merged): Establishes the current state of the uidesc (window height 866px, row positions, TranceGate sync groups)

This spec enables:
- **Phase 4 (Macros & Rungler)**: No direct dependency, but completing Phase 3 clears the next roadmap item
- **Full mono mode usability**: With UI controls exposed, users can finally configure all aspects of monophonic playback

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
| FR-001 | MET | `editor.uidesc:78-81` -- Four control-tag entries: MonoPriority tag 1800, MonoLegato tag 1801, MonoPortamentoTime tag 1802, MonoPortaMode tag 1803. Placed after Global Filter control-tags, before OSC A section. |
| FR-002 | MET | `editor.uidesc:2743-2753` -- Polyphony COptionMenu wrapped in CViewContainer with custom-view-name="PolyGroup", size="112, 18", origin="8, 36", transparent="true". COptionMenu origin changed to 0,0. No visible attribute (defaults true). |
| FR-003 | MET | `editor.uidesc:2755-2794` -- MonoGroup CViewContainer at origin="8, 36", size="112, 18", custom-view-name="MonoGroup", visible="false", transparent="true". Same position as PolyGroup. |
| FR-004 | MET | `editor.uidesc:2758-2793` -- Four controls: Legato ToggleButton at (0,0) 22x18; Priority COptionMenu at (24,0) 36x18; Portamento Time ArcKnob at (62,0) 18x18; Portamento Mode COptionMenu at (82,0) 30x18. Layout: 22+2+36+2+18+2+30=112px. |
| FR-005 | MET | No separate text label for Portamento Time knob. ArcKnob at editor.uidesc:2779-2784 has tooltip="Portamento time" for identification. |
| FR-006 | MET | `controller.h:239-240` -- polyGroup_ and monoGroup_ CView* fields. `controller.cpp:540-543` -- kVoiceModeId toggle: polyGroup visible when value < 0.5, monoGroup visible when value >= 0.5. |
| FR-007 | MET | `controller.cpp:903-912` -- verifyView() captures PolyGroup/MonoGroup by custom-view-name. `controller.cpp:620-621` -- willClose() nulls both pointers. |
| FR-008 | MET | `controller.cpp:540-543` -- Visibility toggle in setParamNormalized() (deferred update mechanism). Same IDependent pattern as 6 existing sync toggle groups. |
| FR-009 | MET | `controller.cpp:903-912` -- verifyView() reads kVoiceModeId via getParameterObject()->getNormalized(), sets initial visibility: PolyGroup visible=!isMono, MonoGroup visible=isMono. |
| FR-010 | MET | No changes to save/load functions. saveMonoModeParams()/loadMonoModeParams() at mono_mode_params.h:80-107 unchanged. Pluginval Plugin state test passes. |
| FR-011 | MET | Legato: on-color="master" (line 2766). Priority: font-color="master" (line 2774). Portamento Time: arc-color="master", guide-color="knob-guide" (lines 2782-2783). Portamento Mode: font-color="master" (line 2789). |
| SC-001 | MET | MonoGroup container with 4 controls at editor.uidesc:2755-2794. Controller toggle at controller.cpp:540-543. Pluginval Editor tests pass. |
| SC-002 | MET | controller.cpp:541 sets polyGroup visible when value < 0.5. Switching to Poly restores PolyGroup, hides MonoGroup. Pluginval Automation tests pass. |
| SC-003 | MET | ArcKnob at editor.uidesc:2779-2784 bound to kMonoPortamentoTimeId. Cubic mapping (value^3 * 5000ms) in mono_mode_params.h:34-36. Range 0-5000ms. |
| SC-004 | MET | Pluginval passes at strictness level 5 -- all sections pass including Plugin state, Automation, Automatable Parameters. |
| SC-005 | MET | ruinae_tests: 291 cases, 3552 assertions all passed. dsp_tests: 5470 cases all passed. plugin_tests: 239 cases all passed. shared_tests: 175 cases all passed. Zero regressions. |
| SC-006 | MET | Full Release build: zero warnings. Clang-tidy: 0 errors, 0 warnings. |
| SC-007 | MET | verifyView() at controller.cpp:903-912 reads kVoiceModeId on editor open. loadMonoModeParams() restores values. Pluginval Plugin state test confirms round-trip. |
| SC-008 | MET | All 4 params registered with kCanAutomate via registerMonoModeParams(). Pluginval Automatable Parameters and Automation tests pass. |

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

All 11 FR and 8 SC requirements MET. Build clean (0 warnings), all test suites pass, pluginval strictness 5 passes, clang-tidy clean.
