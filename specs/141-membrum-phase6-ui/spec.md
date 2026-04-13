# Feature Specification: Membrum Phase 6 -- Macros, Acoustic/Extended Modes, and Custom Editor

**Feature Branch**: `141-membrum-phase6-ui`
**Plugin**: Membrum (`plugins/membrum/`)
**Created**: 2026-04-12
**Status**: Draft
**Input**: Phase 6 scope from Spec 135 (Membrum Synthesized Drum Machine); builds on Phase 5 (`specs/140-membrum-phase5-coupling/`)

## Background

Phases 1 through 5 delivered the complete synthesis engine and architectural framework of Membrum: single-voice MembraneBody (Phase 1), six exciters and six body models with Tone Shaper and Unnatural Zone (Phase 2), multi-voice polyphony with voice stealing and choke groups (Phase 3), the 32-pad architecture with kit and per-pad presets and 16 output buses (Phase 4), and cross-pad sympathetic resonance with a tiered Tier 1/Tier 2 coupling matrix (Phase 5, v0.5.0). The underlying parameter surface is now very wide -- 5 Phase 1 parameters, 45 Phase 2 parameters, 3 Phase 3 parameters, 1 Phase 4 selector, 4 Phase 5 globals, 32 per-pad coupling amounts, and 1,152 per-pad parameters across the 32 `PadConfig[32]` instances -- but the plugin currently ships with the default host-generated parameter list as its only UI.

Phase 6 is where that surface becomes playable. It delivers the three capabilities that Spec 135's roadmap entry for this phase names explicitly: **Macro controls, Acoustic/Extended UI modes, and the custom editor.** It also lands every outstanding UI-facing commitment from Spec 135 that has been deferred across Phases 1 through 5: the 4x8 pad grid, the selected-pad editor, the kit-level controls column, the Tier 2 coupling matrix editor, choke group UI, voice management UI, output routing UI, kit and per-pad preset browsing inside the editor, the pitch-envelope promotion to primary voice control (promised in the Tone Shaper section), and the Punch macro's integration of pitch-envelope depth/speed.

The goal is a complete, shipping user experience at v0.6.0 -- not a partial editor with macros deferred. Every parameter registered by Phases 1 through 5 must be reachable from the Phase 6 editor, either directly (Extended mode) or through a macro (Acoustic mode). Every feature that Spec 135 describes as a UI surface must exist as a control in this phase.

## Design Philosophy (from Spec 135)

Membrum's control model is explicitly two-layered:

> "Macro Controls that sit on top of the detailed parameters ... These are **on top of** the detailed parameters, not instead of them. In Acoustic mode (default), users see macros + key controls. In Extended mode, full parameter access is available including the Unnatural Zone. This dual-layer approach reduces overwhelm while preserving full control for power users." -- Spec 135 §Control Philosophy

Phase 6 implements this literally. The Acoustic/Extended toggle is a top-level, always-visible kit control. Acoustic mode is the default and presents macros, primary body controls, Level, and the pitch envelope. Extended mode reveals the Unnatural Zone, raw physics params, and the full Tier 2 coupling matrix. The toggle is **session-scoped**: it resets to Acoustic on plugin instantiation and on any state load without an explicit mode value. Kit presets MAY encode and restore the mode; DAW project state does not independently persist it.

The five macros are defined in Spec 135 §Control Philosophy and MUST map to the listed detailed parameters:

| Macro         | Maps To (per Spec 135) |
|---------------|------------------------|
| **Tightness** | Tension + Damping + Decay Skew |
| **Brightness**| Exciter spectral content + Mode Weighting + Filter cutoff |
| **Body Size** | Size + Mode Spacing + Envelope scaling |
| **Punch**     | Exciter attack + Pitch Envelope depth/speed |
| **Complexity**| Mode Count + Coupling strength + Nonlinearity |

Macros are per-pad. Each pad has its own set of five macro values, stored inside `PadConfig` and serialised with kit presets. The macros drive the detailed parameters at control-rate; editing a macro moves the underlying parameters, and editing an underlying parameter does not snap a macro value (macros are forward-only drivers with a stored control value, not bidirectional display proxies).

## Clarifications

### Session 2026-04-12

- Q: How are macro offsets encoded -- as deltas relative to each target parameter's registered default value, absolute parameter values, or deltas relative to the current live parameter value at the time of macro adjustment? → A: Macro offsets are deltas relative to each target parameter's registered default value.
- Q: Where does the MacroMapper run -- in the Processor (audio thread) or in the Controller (UI thread), and at what granularity? → A: MacroMapper runs in the Processor on the audio thread, once per block inside processParameterChanges().
- Q: How does the pad-glow envelope follower publish per-pad amplitude levels from the audio thread to the UI thread? → A: 1024-bit threshold-gated bitfield (128 bytes/frame): each of the 32 pads maps to one 32-bit word; bits encode quantised amplitude buckets. The bitfield is written atomically from the audio thread and read by the UI thread at <= 30 Hz with no per-pad float allocations.
- Q: What is the persistence scope of the Acoustic/Extended UI mode toggle -- is it session-scoped (resets to Acoustic on plugin re-open), persisted in the plugin state (survives DAW project save/load), or saved in kit presets only? → A: Session-scoped with preset override: the toggle defaults to Acoustic at plugin instantiation and on any state load that carries no explicit mode value; kit presets MAY encode the mode and restore it on load, but DAW project state does NOT persist the mode independently of a kit preset.
- Q: How is the editor size switch (Default 1280x800 / Compact 1024x640) triggered -- by a user-controlled toggle inside the Kit Column, by a host-driven resize breakpoint, or by a persistent preference stored in the plugin state? → A: User-controlled toggle in the Kit Column; no host-driven resize breakpoint. The size preference is stored in `kEditorSizeId` (session-scoped, not written to the VST3 state blob).

## User Scenarios & Testing *(mandatory)*

**Priority levels used in this section**: P1 = must ship with Phase 6 (blocking delivery); P2 = important but non-blocking; P2 stories MUST be completed before the spec is closed but may be implemented after all P1 stories are verified working.

### User Story 1 -- First-Open Acoustic Mode with Macros (Priority: P1)

A producer opens Membrum for the first time. The custom editor appears with a 4x8 pad grid on the left (lit-up default GM kit), a selected-pad panel in the centre showing the five macros (Tightness, Brightness, Body Size, Punch, Complexity), the primary body controls (Material, Size, Decay, Strike Position), Level, and the pitch envelope. A kit-level column on the right shows the Acoustic/Extended toggle (default: Acoustic), Max Polyphony, Voice Stealing policy, Global Coupling, Snare Buzz, and Tom Resonance. The producer clicks a pad, turns the Tightness macro on the selected pad, and hears the drum become tighter in real time without ever touching Tension, Damping, or Decay Skew individually.

**Why this priority**: This is the entire reason Phase 6 exists. Without a custom editor and macros, Membrum is unplayable for its target audience. If a user opens the plugin and sees only a host-generated parameter list of 1,200+ entries, they close it. P1.

**Independent Test**: Open the plugin in a host. Verify the custom editor shows the pad grid, selected-pad panel with macros, and kit column. Select a pad, automate the Tightness macro from 0.0 to 1.0 over 2 s, and verify the underlying Tension, Damping, and Decay Skew parameters follow the documented mapping curves.

**Acceptance Scenarios**:

1. **Given** a freshly instantiated plugin, **When** the editor opens, **Then** the 4x8 pad grid, selected-pad panel, and kit column are visible simultaneously without scrolling at the default editor size.
2. **Given** the editor is in Acoustic mode (default), **When** the user inspects the selected-pad panel, **Then** the Unnatural Zone controls and raw physics params (b1/b3, Air Coupling, Nonlinear Pitch) are hidden, and the five macros plus Material/Size/Decay/Strike Position/Level/Pitch Envelope are visible.
3. **Given** pad 1 is selected, **When** the user moves the Tightness macro from 0.0 to 1.0, **Then** the Tension, Damping, and Decay Skew underlying parameters change along their documented macro curves, and the audio output becomes audibly tighter.
4. **Given** pad 1 has been edited via macros, **When** pad 2 is selected and pad 1 is re-selected, **Then** pad 1's macro values and underlying parameter values are preserved exactly as left.

---

### User Story 2 -- Extended Mode Reveals Full Parameter Access (Priority: P1)

A sound designer wants to push beyond the macro abstraction and sculpt modal frequencies directly. They click the Acoustic/Extended toggle in the kit column. The selected-pad panel expands to show additional tabs or sections for the Unnatural Zone (Mode Stretch, Mode Inject, Decay Skew, Material Morph, Nonlinear Coupling), raw physics parameters (Tension, Damping, Air Coupling, Nonlinear Pitch, b1/b3 if modelled as separate controls), the full Tone Shaper (Filter, Drive, Wavefolder, Pitch Envelope with envelope stages), the full Exciter parameter set for the active exciter type, and the Tier 2 coupling matrix editor. Toggling back to Acoustic hides these without changing their values.

**Why this priority**: Extended mode is what makes Membrum more than a toy. Sound designers, the second-largest target audience, require full parameter access. P1 alongside US1.

**Independent Test**: Toggle Extended mode. Verify every parameter registered by Phases 1 through 5 is visible and editable. Toggle back to Acoustic. Verify the hidden parameters retain their Extended-mode values (hiding is visual only; no parameter resets).

**Acceptance Scenarios**:

1. **Given** the editor is in Acoustic mode, **When** the user toggles Extended mode, **Then** Unnatural Zone, raw physics, full Tone Shaper, full Exciter, and Tier 2 coupling matrix controls become visible.
2. **Given** Extended mode is active, **When** the user edits Mode Stretch on pad 3, toggles back to Acoustic, and toggles Extended again, **Then** pad 3's Mode Stretch value is unchanged.
3. **Given** Extended mode is active and a kit preset is saved, **When** a fresh plugin instance loads that preset, **Then** the editor opens in the mode stored in the preset.
4. **Given** Acoustic mode is active, **When** the host sends automation for an Unnatural Zone parameter, **Then** the automation still takes effect on the audio (host automation is never blocked by UI mode).

---

### User Story 3 -- Pad Grid Trigger, Select, and Visual Feedback (Priority: P1)

A producer interacts with the 4x8 pad grid. Each pad shows its MIDI note (C1 through G#3), a name (kick, snare, etc.), and a small category icon. Clicking a pad selects it for editing (the selected-pad panel updates). Shift-clicking or right-clicking (via VSTGUI's cross-platform event model) triggers a preview audition at a fixed velocity without changing selection. When any pad is sounding (including via external MIDI), its cell glows with an envelope-follower-driven intensity. Pads assigned to a choke group display a small group number; pads routed to an aux bus show the bus number.

**Why this priority**: The pad grid is the primary navigational surface. Without it, users cannot efficiently reach per-pad parameters, and the 32-pad architecture is invisible. P1.

**Independent Test**: Open the editor. Verify all 32 pads are visible in a 4x8 grid, labelled correctly (MIDI 36 through 67, GM names). Trigger MIDI note 36 externally and verify pad 1 glows. Click pad 5 and verify the selected-pad panel switches to pad 5's parameters. Shift-click pad 7 and verify an audition plays without changing the selected pad.

**Acceptance Scenarios**:

1. **Given** the editor is open, **When** the user looks at the grid, **Then** all 32 pads are visible in a 4x8 layout with MIDI numbers 36 (bottom-left) through 67 (top-right) matching GM drum map ordering.
2. **Given** pad 1 is selected, **When** the user clicks pad 5, **Then** the selected-pad panel updates to show pad 5's parameters within 1 frame (<= 16.7 ms) and the grid highlights pad 5 as selected.
3. **Given** the host is playing a pattern, **When** pad 3 sounds, **Then** pad 3's cell glows with intensity proportional to the voice envelope, decaying with the voice's release.
4. **Given** pad 7 is not selected, **When** the user shift-clicks pad 7, **Then** pad 7 plays at velocity 100 and pad 1 remains the selected pad.
5. **Given** pad 4 is assigned to choke group 2 and routed to aux bus 3, **When** the user looks at the grid, **Then** pad 4 displays indicators showing "CG2" and "BUS3" (compact glyphs, not full text).

---

### User Story 4 -- Kit-Level Controls and Preset Browser (Priority: P1)

A producer needs to manage kits and presets from inside the plugin. The kit column on the right shows a kit preset browser (current kit name, prev/next, browse button opening a modal browser), a per-pad preset browser (scoped to the currently selected pad), the Acoustic/Extended toggle, Max Polyphony, Voice Stealing policy selector, Global Coupling, Snare Buzz, Tom Resonance, Coupling Delay, a global output meter, and a CPU indicator. Kit presets load the entire 32-pad state; per-pad presets load only the selected pad's sound parameters (and preserve coupling amount and output bus per the Phase 4 contract).

**Why this priority**: Preset browsing was shipped at the data level in Phase 4 but has no UI yet. Users currently cannot load a preset without using the host's generic parameter automation controls. P1.

**Independent Test**: Open the kit preset browser, load a factory kit, and verify all 32 pads update. Save a modified kit as a user preset. Open the per-pad preset browser for pad 3, load a per-pad preset, and verify only pad 3's sound parameters change while its output bus and coupling amount remain at the pre-load values.

**Acceptance Scenarios**:

1. **Given** the editor is open, **When** the user clicks the kit browse button, **Then** a modal browser (VSTGUI-based `PresetBrowserView`) opens showing factory and user kits with search and category filters.
2. **Given** pad 5 is selected, **When** the user loads a per-pad preset for that pad, **Then** pad 5's sound parameters change to the preset values, but pad 5's `outputBus` and per-pad `couplingAmount` are preserved at their pre-load values (Phase 4 FR-022 and Phase 5 FR-022 contract).
3. **Given** the kit has been edited, **When** the user clicks "Save As" in the kit browser, **Then** a text input dialog (cross-platform VSTGUI) accepts the name and the kit is written to the user preset directory documented in Phase 4.
4. **Given** a kit preset file is malformed, **When** the user attempts to load it, **Then** an error indicator appears in the browser and the editor state is unchanged (no crash, no partial load).

---

### User Story 5 -- Choke Groups and Voice Management UI (Priority: P2)

A producer wants to set the open hat (pad 9) and closed hat (pad 10) to the same choke group. In the selected-pad panel, a "Choke Group" dropdown exposes values 0 (none) through 8. In the kit column, a voice management panel shows Max Polyphony (slider 4-16), Voice Stealing policy (Oldest/Quietest/Priority dropdown), and a live active-voices readout.

**Why this priority**: Choke groups and voice management were shipped at the data level in Phase 3 but have no dedicated UI. They are important but less central than pad grid and macros. P2.

**Independent Test**: Select pad 9, set Choke Group to 1. Select pad 10, set Choke Group to 1. Trigger pad 9 (open hat) and while it is ringing, trigger pad 10 (closed hat). Verify pad 9 is choked. Check that the active-voice readout updates live as notes sound.

**Acceptance Scenarios**:

1. **Given** pad 9 and pad 10 are both assigned to choke group 1, **When** pad 9 sounds and pad 10 is triggered during pad 9's decay, **Then** pad 9 is choked (fast-released) per Phase 3 behaviour, and the grid glow for pad 9 rapidly fades.
2. **Given** Max Polyphony is set to 4 in the voice management panel, **When** a 5th note arrives, **Then** voice stealing occurs per the selected policy and the UI voice counter shows 4 (not 5).
3. **Given** Voice Stealing is "Priority", **When** the user hovers over the dropdown, **Then** a tooltip explains the policy in plain language.

---

### User Story 6 -- Tier 2 Coupling Matrix Editor (Priority: P2)

A power user switches to Extended mode and opens the Tier 2 coupling matrix editor. A 32x32 grid visualises per-pair coupling coefficients as colour intensity (0.0 transparent, 0.05 opaque). The rows are sources, columns are destinations (or a symmetric toggle). Clicking a cell opens an inline slider (or drag-to-edit) to set `overrideGain` directly. A "Reset" button per cell clears `hasOverride` and reverts the cell to the Tier 1 computed value. Pads actively contributing to coupling are outlined in the matrix during playback. A "Solo path" button highlights a specific src->dst pair and temporarily mutes all others for debugging.

**Why this priority**: Tier 2 data model ships in Phase 5, but without the matrix editor UI, overrides are unreachable from the plugin itself. P2 -- important for sound designers but not blocking P1 delivery.

**Independent Test**: Open the matrix editor in Extended mode. Verify 32x32 cells are drawn with colour mapped to effective gain. Click cell (1, 3) and set it to 0.04. Verify the coupling matrix state updates (Phase 5 FR-030 override path) and that saving+loading a kit preset round-trips the override value.

**Acceptance Scenarios**:

1. **Given** Extended mode is active and the matrix editor is open, **When** the user sets cell (kick, snare) to 0.04, **Then** the `CouplingMatrix::overrideGain` for that pair is 0.04 and `hasOverride` is true.
2. **Given** a cell has an override, **When** the user clicks Reset on that cell, **Then** `hasOverride` becomes false and the cell renders the Tier 1 computed value.
3. **Given** the kick is firing with non-zero coupling to the snare, **When** the user looks at the matrix during playback, **Then** the (kick, snare) cell is outlined with an activity indicator.
4. **Given** Solo is engaged on the (kick, snare) pair, **When** any drum is triggered, **Then** only the (kick, snare) coupling path is audible in the coupling output; Solo disengages automatically when the matrix editor closes.

---

### User Story 7 -- Output Routing UI (Priority: P2)

A producer routing drums to separate DAW channels needs to assign pads to aux buses from inside the plugin. In the selected-pad panel (Extended mode section, always visible as a compact control in Acoustic mode), an "Output" selector with 16 choices (Main, Aux 1, Aux 2, ..., Aux 15) sets the pad's output bus. The grid cell for that pad updates its bus-number indicator. Assigning a pad to an inactive aux bus shows a "Host-activate required" warning tooltip.

**Why this priority**: Output routing shipped at the data level in Phase 4 but has no editor UI. Routing is critical for live multi-track export. P2.

**Independent Test**: Select pad 3. Assign to Aux 2. Verify the grid shows BUS2 on pad 3 and the processor routes pad 3 audio to both Main and Aux 2 (per Phase 4 FR-023 send model). Assign to Aux 5 (inactive). Verify pad 3 routes only to Main and a warning tooltip is shown.

**Acceptance Scenarios**:

1. **Given** pad 3 is selected, **When** the user picks Aux 2 in the Output selector, **Then** pad 3's `outputBus` becomes 2 and the grid indicator updates.
2. **Given** pad 3 is assigned to Aux 5 and Aux 5 is not activated by the host, **When** the user hovers the Output selector, **Then** a tooltip warns "Host must activate Aux 5 bus" and the pad falls back to Main only per Phase 4.

---

### User Story 8 -- Pitch Envelope as Primary Voice Control (Priority: P2)

Per Spec 135's Tone Shaper section: "Pitch Envelope ... NOTE: This is identity-defining for kicks -- promoted to a primary voice control in the UI, not buried in tone shaping. Integrated into the 'Punch' macro." Phase 6 delivers this promotion. The pitch envelope (Start Hz, End Hz, Time ms, Curve) is visible in the Acoustic-mode selected-pad panel as a small envelope display with draggable start/end/time points -- not buried behind a Tone Shaper tab. The Punch macro drives the envelope's depth (scale of End->Start span) and speed (scale of Time) according to the mapping documented in this spec.

**Why this priority**: Non-negotiable per Spec 135. Without pitch-envelope promotion, the Punch macro has no UI surface and kicks cannot be shaped in Acoustic mode. P2 (grouped with macros but called out separately for clarity).

**Independent Test**: Open a pad set to the Kick archetype. Verify the pitch envelope display is visible in Acoustic mode. Drag the envelope's End point from 50 Hz to 80 Hz and confirm the kick's pitch-down span changes. Set Punch macro to 1.0 and verify both envelope Time and depth scale per the mapping table.

**Acceptance Scenarios**:

1. **Given** pad 1 is selected (kick archetype), **When** the user looks at the selected-pad panel in Acoustic mode, **Then** the pitch envelope display is visible as a primary control (not behind a tab or collapsed section).
2. **Given** the Punch macro is at 0.5, **When** the user increases it to 1.0, **Then** the pitch envelope's span (Start-End) and Time values update per the documented mapping and the kick audibly gains "snap."
3. **Given** the pitch envelope display is visible, **When** the user drags the Start point vertically, **Then** `kToneShaperPitchEnvStartId` updates in real time with parameter smoothing.

---

### Edge Cases

- **Window resize**: The editor supports two fixed sizes (Default 1280x800 and Compact 1024x640) switched by a user-controlled toggle in the Kit Column. There is no host-driven resize breakpoint; the host may resize the window but the plugin only snaps to one of the two template sizes. Switching sizes MUST NOT break the pad grid or hide macros. The active size preference is stored in `kEditorSizeId` (session-scoped; not written to the VST3 state blob).
- **DPI scaling / retina displays**: Text, knobs, and grid cells MUST remain legible at 1.0x, 1.5x, and 2.0x scale. VSTGUI's built-in scale handling is used; no manual per-platform paths.
- **Extremely fast automation on macros**: Automating a macro at audio-block rate (every 128 samples) MUST NOT introduce clicks in the underlying parameters (smoothed).
- **Simultaneous macro and underlying edit**: If automation moves a macro while the user drags the underlying Tension knob, the most recent write wins (standard VST3 automation semantics). No bidirectional macro snap-back.
- **Preset load with stale parameter values**: Loading a kit preset replaces all 1,152 per-pad parameters atomically; the editor updates all visible controls in one UI frame.
- **External MIDI triggering during pad selection change**: MIDI triggers continue to sound the correct pad (selection is UI-only, audio path unchanged).
- **Host bypass of the plugin**: The editor continues to render but pad glow stops; Acoustic/Extended state is preserved through bypass.
- **Matrix editor open during kit preset load**: The matrix re-renders to reflect the loaded preset's overrides without requiring the user to close and re-open it.
- **Editor opened then closed rapidly**: Controller state (IDependent subscribers, timers, matrix editor handles) is cleaned up with no use-after-free (ASan clean).
- **macOS AU validation**: The custom editor loads correctly under `auval -v aumu Mb01 KrAu` (AU wrapper view). No regressions to AU bus config (Phase 4/5 `au-info.plist` and `audiounitconfig.h` are unchanged by Phase 6 since no new audio buses are added).
- **Per-pad preset that predates Phase 6 macros**: Loading a v4 or v5 per-pad preset with no macro data MUST succeed; macro values take their defaults (0.5 = neutral). No inverse-mapping attempt.
- **Acoustic/Extended toggle via host automation**: The toggle is a plugin parameter and MUST be automatable; automating it triggers the visibility change on the UI thread via the `IDependent` pattern.

## Requirements *(mandatory)*

### Functional Requirements

#### Custom Editor Shell

- **FR-001**: The plugin MUST provide a custom VST3 editor built with VSTGUI, replacing the host-generated parameter view. The editor MUST open at a default size of 1280x800. A Compact 1024x640 size is available and switched by a user-controlled toggle in the Kit Column (see FR-040); there is no host-driven resize breakpoint. Both sizes are implemented as VSTGUI size-switching templates. The active size preference is stored in `kEditorSizeId` (session-scoped; not written to the VST3 state blob).
- **FR-002**: The editor MUST be fully cross-platform (Windows, macOS, Linux) using only VSTGUI abstractions. Any native API use (Win32, Cocoa/AppKit) for UI is FORBIDDEN (project constitution rule).
- **FR-003**: The editor layout MUST contain three primary regions visible simultaneously at default size: (a) the 4x8 Pad Grid on the left, (b) the Selected-Pad Panel in the centre, (c) the Kit Column on the right.
- **FR-004**: The editor MUST register as a VST3 `IPlugView` returned from the `Controller::createView()` factory, following the Ruinae/Innexus plugin patterns (`plugins/ruinae/src/controller/controller.cpp` reference).
- **FR-005**: All editor state changes (pad selection, Acoustic/Extended mode, matrix editor open/close) MUST be communicated via the `IDependent`/deferred-update pattern documented in the `vst-guide` skill. Direct modification of VSTGUI controls from non-UI threads is FORBIDDEN.

#### Pad Grid

- **FR-010**: The Pad Grid MUST render all 32 pads in a 4x8 layout with MIDI note 36 at the bottom-left and MIDI note 67 at the top-right, matching the GM drum map ordering from Phase 4.
- **FR-011**: Each pad cell MUST display: (a) the MIDI note number or note name, (b) the pad's default GM name (e.g., "Kick", "Snare", "Closed Hi-Hat"), (c) a compact category glyph (Kick/Snare/Tom/Hat/Cymbal/Perc/Tonal/FX) derived from the Phase 5 pad-category rule chain or the default kit archetype, (d) a choke-group indicator when `chokeGroup != 0`, (e) an output-bus indicator when `outputBus != 0`.
- **FR-012**: Clicking a pad cell MUST set it as the selected pad, updating the `kSelectedPadId` parameter (introduced in Phase 4). The Selected-Pad Panel MUST refresh within one UI frame (<= 16.7 ms).
- **FR-013**: Shift-click or right-click on a pad cell MUST audition the pad at velocity 100 without changing the selection. Detection uses VSTGUI's `MouseEvent::modifiers` (for Shift) and `MouseButton::Right` (for right-click), both via the cross-platform VSTGUI event API. (See also US3 Acceptance Scenario 4 for the user-facing test.)
- **FR-014**: When a pad is actively sounding, its cell MUST glow with intensity proportional to the voice's amplitude envelope. The envelope follower used for this visualisation MUST run on the UI thread using a thread-safe read of per-pad amplitude state published from the audio thread via a pre-allocated 1024-bit threshold-gated bitfield (128 bytes): each of the 32 pads occupies one 32-bit word whose bits encode quantised amplitude buckets (e.g., 32 levels). The audio thread writes the word for the active pad atomically (`memory_order_relaxed`); the UI thread reads all 32 words at <= 30 Hz (`memory_order_acquire`). No heap allocations on the audio thread. No DataExchange API required for this path.
- **FR-015**: The grid MUST render correctly at 1.0x, 1.5x, and 2.0x DPI scales via VSTGUI's built-in scaling.

#### Selected-Pad Panel and Macros

- **FR-020**: The Selected-Pad Panel MUST show, in Acoustic mode: (a) the five Macros (Tightness, Brightness, Body Size, Punch, Complexity), (b) the five primary body controls (Material, Size, Decay, Strike Position, Level), (c) the Pitch Envelope display (Start Hz, End Hz, Time, Curve), (d) the Output selector, (e) a compact Exciter/Body type pair of dropdowns, (f) the Choke Group selector.
- **FR-021**: Each macro MUST be a per-pad normalised [0.0, 1.0] parameter stored inside `PadConfig` at a new reserved offset range (see FR-070). Default value: 0.5 for all macros on all pads (neutral -- macro at 0.5 leaves underlying parameters at their un-macro-driven base values).
- **FR-022**: Macro mappings MUST be applied at control-rate by a `MacroMapper` component that lives in the **Processor** and executes once per audio block inside `processParameterChanges()` on the audio thread. It reads the five macro values from `PadConfig` and writes the affected underlying parameters directly into the Processor's parameter state. The mapping is a forward driver: macros drive underlying parameters; editing an underlying parameter does not update the macro's stored value. The `MacroMapper` MUST NOT allocate, lock, or perform I/O on the audio thread.
- **FR-023**: The macro-to-parameter mapping MUST be implemented per the following table. For each macro, the listed target parameters are driven using a documented curve (linear, exponential, or tabulated). Macro offsets are **deltas relative to each target parameter's registered default value**: macro=0.5 produces a delta of zero (target parameter sits at its registered default), macro=0.0 produces a defined negative delta, and macro=1.0 produces a defined positive delta. The `MacroMapper` computes `effectiveValue = registeredDefault + delta(macroValue)` for each target; it does NOT reference the current live parameter value, which means the mapping is deterministic and preset-independent.

  | Macro         | Targets                                        | Curve Notes |
  |---------------|------------------------------------------------|-------------|
  | Tightness     | Tension (kMaterialId partial), Damping (derived from Decay), Decay Skew (kUnnaturalDecaySkewId) | Linear for Tension; exponential for Damping; linear for Decay Skew (-0.5 at macro=0.0 to +0.5 at macro=1.0). |
  | Brightness    | Exciter spectral content (per-exciter: Noise Burst cutoff (`kExciterNoiseBurstCutoffId`), Mallet hardness (`kExciterMalletHardnessId`), Impulse width (`kExciterImpulseWidthId`)), Mode Weighting (inject amount `kUnnaturalModeInjectAmountId`), Filter Cutoff (`kToneShaperFilterCutoffId`) | Exponential for cutoff (log-Hz); linear for inject amount; exciter target varies by active exciter type. |
  | Body Size     | Size (kSizeId), Mode Spacing (Mode Stretch kUnnaturalModeStretchId offset), Amp Envelope scaling (decay multiplier) | Linear for Size; +-10% stretch offset around 1.0; linear decay scaling. |
  | Punch         | Exciter attack (per-exciter), Pitch Envelope depth (Start-End span), Pitch Envelope speed (Time scaling) | Exponential for depth; inverse-exponential for time (higher Punch = faster env); linear for exciter attack. |
  | Complexity    | Mode Count proxy (via `kUnnaturalModeInjectAmountId`, as stepped proxy for partial count 8..32; a dedicated `kModeCountId` is out of scope for Phase 6), Coupling strength (per-pad `couplingAmount` modifier), Nonlinearity (`kUnnaturalNonlinearCouplingId`) | Stepped for mode count proxy; linear for coupling modifier; linear for nonlinearity. |

- **FR-024**: The Pitch Envelope display MUST be interactive: dragging the Start point changes `kToneShaperPitchEnvStartId`, the End point changes `kToneShaperPitchEnvEndId`, and the Time/horizontal drag changes `kToneShaperPitchEnvTimeId`. Curve selection uses the existing `kToneShaperPitchEnvCurveId` string-list control.
- **FR-025**: In Extended mode, the Selected-Pad Panel MUST additionally expose: the full Unnatural Zone (Mode Stretch, Mode Inject, Decay Skew, Nonlinear Coupling, Material Morph block of 5 params), the raw physics parameters (Tension, Damping, Air Coupling, Nonlinear Pitch -- where not already represented by primary controls), the full Tone Shaper (Filter Type, Cutoff, Resonance, EnvAmount, Drive, Fold, and Filter ADSR), the complete exciter parameter set for the active exciter (FM Ratio, Feedback Amount, Noise Burst Duration, Friction Pressure), and the per-pad coupling amount.
- **FR-026**: Every parameter registered in Phases 1 through 5 MUST be reachable via the Extended-mode editor. No parameter may be UI-orphaned. A static check (test) MUST enumerate all registered parameters and verify each appears in the editor XML or in a matrix/grid cell.

#### Acoustic/Extended Mode

- **FR-030**: The plugin MUST add a global `kUiModeId` parameter (StringListParameter: "Acoustic", "Extended"). Default: "Acoustic". Persistence is **session-scoped with preset override**: on plugin instantiation or any state load that carries no explicit `kUiModeId` value, the mode resets to "Acoustic"; kit presets MAY include `kUiModeId` and restore it on load; DAW project state does NOT independently persist the mode outside of an embedded kit preset. The parameter is NOT written into the VST3 `IBStream` state blob directly.
- **FR-031**: Toggling `kUiModeId` MUST swap the Selected-Pad Panel between its Acoustic and Extended layouts using VSTGUI's `UIViewSwitchContainer` template-switching mechanism (per the `vst-guide` skill guidance).
- **FR-032**: Toggling modes MUST NOT change any audio parameter values. Hidden parameters retain their current values; host automation on hidden parameters continues to take effect.
- **FR-033**: The mode toggle MUST be automatable (standard VST3 parameter) and MUST apply the visibility change on the UI thread via the `IDependent` pattern, never from the audio or automation thread.

#### Kit Column

- **FR-040**: The Kit Column MUST show: kit preset browser (name display, prev/next, browse), per-pad preset browser (scoped to selected pad), Acoustic/Extended toggle, an editor size toggle (Default / Compact, switching between 1280x800 and 1024x640 via VSTGUI size-switching templates -- user-initiated only, no host breakpoint), Max Polyphony slider (4-16), Voice Stealing dropdown (Oldest/Quietest/Priority), Global Coupling knob (tagged to the existing Phase 5 global coupling parameter), Snare Buzz knob (tagged to the existing Phase 5 `kSnareBuzzId` parameter), Tom Resonance knob (tagged to the existing Phase 5 `kTomResonanceId` parameter), Coupling Delay knob (tagged to `kCouplingDelayId = 273` from Phase 5), an active-voices readout, a global output level meter (stereo), and a CPU usage indicator. All four Phase 5 global knobs are UI wiring only; no new parameter IDs are allocated in Phase 6 for these controls.
- **FR-041**: The kit preset browser MUST use the shared `PresetBrowserView` component from `plugins/shared/src/ui/preset_browser_view.h`, configured with Membrum's kit preset directory (from Phase 4).
- **FR-042**: The per-pad preset browser MUST be scoped to the currently selected pad. Loading a per-pad preset MUST apply only to that pad's sound parameters and MUST preserve `outputBus` and `couplingAmount` (Phase 4 FR-022, Phase 5 FR-022).
- **FR-043**: The active-voices readout and CPU indicator MUST update at a UI-thread-safe rate (<= 30 Hz) using values published from the audio thread via DataExchange API or lock-free atomics.
- **FR-044**: The output level meter MUST be a stereo peak+RMS display fed by lock-free audio-thread writes. No allocations on the audio thread.

#### Tier 2 Coupling Matrix Editor

- **FR-050**: In Extended mode, the editor MUST provide a 32x32 coupling matrix editor view, reachable via a tab or collapsible panel in the Kit Column. The matrix cells MUST render colour intensity proportional to `effectiveGain[src][dst]` from the Phase 5 `CouplingMatrix`.
- **FR-051**: Clicking a cell MUST allow the user to set `overrideGain` for that pair via an inline editor (slider, spinbox, or drag). Clicking a "Reset" control (per cell or contextual) MUST clear `hasOverride` for that pair and revert the cell to its Tier 1 computed value.
- **FR-052**: During playback, cells for pairs that are currently contributing non-zero coupling energy MUST be outlined or highlighted. The highlight is driven by the pre-allocated `MatrixActivityPublisher` lock-free atomic bitfield (32 x `std::atomic<uint32_t>`, one word per source pad, bits encoding active destination pads), written by the audio thread with `memory_order_relaxed` and read by the UI thread at <= 30 Hz with `memory_order_acquire`. No DataExchange API is used for this path; no heap allocations occur on the audio thread.
- **FR-053**: A "Solo" toggle MUST allow the user to isolate a single src->dst coupling pair for debugging: all other pair gains are temporarily zeroed in the resolver output. Closing the matrix editor MUST automatically disengage Solo.
- **FR-054**: Matrix overrides set via the editor MUST round-trip through state version 6 preserving the Phase 5 override list serialisation format.

#### Voice Management UI

- **FR-060**: The Voice Management panel (inside the Kit Column or a sub-tab) MUST expose `kMaxPolyphonyId`, `kVoiceStealingId`, and the active-voices readout.
- **FR-061**: Hovering the Voice Stealing dropdown MUST show a tooltip briefly describing each policy (Oldest, Quietest, Priority).
- **FR-062**: Setting Choke Group (`kChokeGroupId` for the selected pad, scoped via the Phase 4 selected-pad proxy) MUST be available in the Selected-Pad Panel in both Acoustic and Extended modes.

#### Output Routing UI

- **FR-065**: The Selected-Pad Panel MUST include an Output selector (16 entries: Main, Aux 1..15) reflecting the pad's `outputBus` (Phase 4 `PadConfig` field).
- **FR-066**: If the user selects an aux bus that the host has not activated, the selector MUST display a non-modal warning glyph/tooltip indicating that the bus will fall back to Main until the host activates it (Phase 4 contract preserved).

#### Parameter Allocations

- **FR-070**: Phase 6 MUST add the following parameter IDs without colliding with existing allocations:
  - **Global range 280-299**: reserved for Phase 6 globals.
    - `kUiModeId = 280` (StringListParameter: Acoustic, Extended; default Acoustic).
    - `kEditorSizeId = 281` (StringListParameter: Default, Compact; default Default). Persists editor size preference.
    - Remaining 282-299 reserved for future Phase 6 globals (no-op in this phase).
  - **Per-pad macro offsets 37-41** (inside the reserved `PadConfig` offset range 36-63, immediately after Phase 5's `kPadCouplingAmount = 36`):
    - `kPadMacroTightness = 37`
    - `kPadMacroBrightness = 38`
    - `kPadMacroBodySize = 39`
    - `kPadMacroPunch = 40`
    - `kPadMacroComplexity = 41`
  - All per-pad macro parameter IDs follow the existing Phase 4 formula: `kPadBaseId + N * kPadParamStride + offset`.
- **FR-071**: A `static_assert` MUST verify Phase 6 globals do not overlap Phase 5 (`kCouplingDelayId < kUiModeId`) and do not overlap per-pad range (`kUiModeId + kPhase6GlobalCount <= kPadBaseId`).
- **FR-072**: Parameter naming MUST follow the project convention `k{Scope}{Parameter}Id` documented in `CLAUDE.md`.

#### State Versioning and Migration

- **FR-080**: The plugin MUST use state version 6 (`kCurrentStateVersion = 6`).
- **FR-081**: Loading a v5 state blob MUST succeed via a v5->v6 migration step that assigns defaults for all Phase 6 additions: all 32x5 = 160 per-pad macro values = 0.5. `kUiModeId` and `kEditorSizeId` are session-scoped and are NOT written into the versioned state blob; they always reset to "Acoustic" and "Default" respectively on state load.
- **FR-082**: Loading v1/v2/v3/v4 state blobs MUST succeed via the existing migration chain (v1->v2->v3->v4->v5->v6), with Phase 6 defaults applied at the v5->v6 step.
- **FR-083**: State version 6 serialisation MUST include the 160 per-pad macro values. Macro values are serialised as 160 x float64 (8 bytes each) = 1280 bytes APPENDED after the existing v5 payload, in pad-major order (pad0.tightness, pad0.brightness, pad0.bodySize, pad0.punch, pad0.complexity, pad1.tightness, ..., pad31.complexity), per the binary layout in `contracts/state_v6_migration.md`. The macro data is NOT written inline into the per-pad PadConfig blobs; it is appended as a single block after the complete v5 payload. `kUiModeId` and `kEditorSizeId` are session-scoped and MUST NOT be written into the v6 state blob; they are omitted from `IBStream` serialisation entirely.
- **FR-084**: Round-trip (save->load) of any valid v6 state blob MUST be byte-identical for all parameter values (within float tolerance) including macro values.

#### Existing Component Reuse

- **FR-090**: The editor MUST reuse the shared UI components already available in `plugins/shared/src/ui/`: `ArcKnob`, `ToggleButton`, `ActionButton`, `ADSR Display`, `XY Morph Pad` (for Material Morph editor), `PresetBrowserView`. New custom views are introduced only where no shared component fits (the Pad Grid and the Coupling Matrix view are the only expected new custom views).
- **FR-091**: The macro-to-parameter mapping curves MUST be computed using existing `Krate::DSP` utilities (`fast_math.h`, `pitch_utils.h`, `interpolation.h`). No re-implementation of log/exp/lerp utilities.
- **FR-092**: The editor MUST follow the Ruinae/Innexus editor patterns for template XML layout, sub-controller wiring, and `IDependent` subscription.

#### Performance and Real-Time Safety

- **FR-100**: The editor UI thread MUST NOT block the audio thread. All cross-thread communication MUST use the VST3 DataExchange API or lock-free atomics (see `vst-guide` THREAD-SAFETY).
- **FR-101**: The pad-glow envelope follower MUST NOT add allocations on the audio thread. Per-pad amplitude state is encoded into a pre-allocated 1024-bit threshold-gated bitfield (32 x `std::atomic<uint32_t>`, one word per pad) using quantised amplitude buckets. Audio thread writes a single word atomically; UI thread reads the full 128-byte structure at <= 30 Hz. No floating-point atomics, no DataExchange, no heap allocation.
- **FR-102**: Macro re-computation MUST run at control-rate (once per audio block inside `processParameterChanges()` in the Processor), not per-sample. Control-rate is sufficient because macros are user-facing knobs, not modulation sources. The `MacroMapper` is invoked by the Processor on the audio thread; no Controller-side or UI-thread re-computation of underlying parameters is performed.
- **FR-103**: Opening the editor MUST NOT cause audio glitches (no blocking on shared state; the controller's initial snapshot is read through a lock-free or copy-then-swap path).

### Key Entities

- **Macro**: A per-pad normalised [0.0, 1.0] parameter that drives a documented set of underlying parameters through a mapping curve. Five macros per pad x 32 pads = 160 macro parameters total.
- **MacroMapper**: A control-rate component that lives in `plugins/membrum/src/processor/` and runs on the audio thread inside `processParameterChanges()` once per block. It reads `PadConfig` macro values and writes the affected target parameters directly into the Processor's parameter state. It performs no allocations, locks, or I/O.
- **UI Mode**: An enumerated global parameter (Acoustic/Extended) controlling the visibility of Extended-only sections in the Selected-Pad Panel and the Coupling Matrix Editor.
- **Pad Grid View**: A custom VSTGUI `CView` rendering 32 pad cells with selection, glow, shift-click audition, and category/choke/bus indicators. Glow intensity is derived from a 1024-bit threshold-gated bitfield (32 x `std::atomic<uint32_t>`, one word per pad) written by the audio thread and polled by the UI thread at <= 30 Hz.
- **Selected-Pad Panel**: A VSTGUI `UIViewSwitchContainer` swapping between Acoustic and Extended templates; both templates are wired to the same underlying parameters via the Phase 4 selected-pad proxy (`kSelectedPadId`).
- **Kit Column**: A VSTGUI composite view hosting the preset browsers, kit-level controls, meters, and voice management panel.
- **Coupling Matrix View**: A custom VSTGUI `CView` rendering the 32x32 `CouplingMatrix` with click-to-edit, Reset, activity highlight, and Solo behaviour.
- **Pitch Envelope Display**: An interactive widget exposing `kToneShaperPitchEnv{Start,End,Time,Curve}Id` as draggable control points, reused from or patterned on `plugins/shared/src/ui/adsr_display.h`.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A first-time user MUST be able to identify and adjust the five macros on a selected pad within 30 seconds of opening the editor, with no host-specific knowledge. Verified manually by a structured usability walkthrough against acceptance criteria in US1 (not an automated test; this criterion requires human evaluation).
- **SC-002**: In Acoustic mode, 100% of parameters registered in Phases 1-5 that are macro-driven MUST be reachable through a macro; in Extended mode, 100% of parameters MUST be reachable directly in the editor. Verified by an automated test enumerating the controller's parameter registry and matching each ID against the editor XML and macro mapping table.
- **SC-003**: Switching Acoustic <-> Extended mode MUST NOT change any audio parameter value. Verified by snapshotting all parameter values before and after 10 toggles; byte-identical within float tolerance.
- **SC-004**: Pad selection change (click on a grid cell) MUST update the Selected-Pad Panel within 16.7 ms (one frame at 60 Hz) on the CI reference machine. Verified by a controller-level timing test.
- **SC-005**: Pad-glow visualisation MUST track voice envelopes with <= 50 ms visual latency at 60 Hz UI refresh. Verified by triggering a note and measuring the frame at which the corresponding cell first exceeds 50% glow intensity.
- **SC-006**: Loading a v5 state blob into a Phase 6 plugin MUST produce output identical to Phase 5 behaviour (macros at 0.5 = neutral, which MUST leave underlying parameters exactly where the preset placed them). Tolerance: less than -120 dBFS difference vs Phase 5 reference audio.
- **SC-007**: State version 6 MUST round-trip all Phase 6 additions (2 globals + 160 macros) with zero loss through save/load cycles. Verified by byte-for-byte comparison.
- **SC-008**: Editor-thread memory allocations during a 10-second stress test (continuous MIDI, automation on all five macros of pad 1, matrix cell edits) MUST be bounded to the initial view-creation allocations only. Zero audio-thread allocations across the same test.
- **SC-009**: The editor MUST render correctly (no clipping, no overlapping controls, all labels legible) at 1.0x, 1.5x, and 2.0x DPI scales. Verified by manual visual inspection at 1.0x, 1.5x, and 2.0x DPI scales on the CI reference machine at completion.
- **SC-010**: Pluginval strictness level 5 MUST pass with zero errors on Windows and macOS builds.
- **SC-011**: `auval -v aumu Mb01 KrAu` MUST pass on macOS with the Phase 6 editor loaded (no regressions to Phase 4/5 AU config).
- **SC-012**: Clang-tidy MUST report zero new warnings on Phase 6 code (`./tools/run-clang-tidy.ps1 -Target membrum`).
- **SC-013**: Total plugin CPU usage with the editor open, pad glow active, meters running, and 8 voices sounding MUST stay within the Phase 5 CPU budget headroom (no regression greater than 0.5% absolute on the CI reference machine).
- **SC-014**: AddressSanitizer-instrumented build MUST show zero use-after-free or heap errors during a 1-minute editor open/close cycle (100 iterations) combined with continuous MIDI input.
- **SC-015**: Every parameter reachable via the Extended-mode editor MUST be automatable by the host and respond to automation while the editor is open.

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Phase 5 (v0.5.0) is complete and stable. All 32-pad architecture, selected-pad proxy, preset system, output routing, and coupling infrastructure are functioning correctly.
- VSTGUI's `UIViewSwitchContainer`, `UIDescription` template loading, and `IDependent` pattern are sufficient for all visibility/swap behaviour. No native OS UI code is required.
- The five macros can be implemented as forward-only drivers with per-pad storage, without requiring bidirectional linking between macros and underlying parameters. This matches the Spec 135 intent ("on top of the detailed parameters").
- The macro mapping curves (linear/exp/stepped per the FR-023 table) provide a musically useful result when macro=0.5 is the neutral point (zero delta from registered defaults), with symmetric up/down deltas. Exact delta magnitudes are tuned during implementation based on listening tests; final values are captured in the MacroMapper as named `constexpr` offsets. Because offsets are relative to registered defaults (not live values), the mapping is deterministic: any preset loaded with macros at 0.5 produces exactly the preset's authored parameter values.
- The 32x32 coupling matrix view can be rendered at 60 Hz as a single `CView` draw pass using existing VSTGUI primitives; no GPU acceleration required.
- Per-pad envelope-follower values for the pad glow can be published from the audio thread at <= 30 Hz without perceptible lag for the user.
- The plugin's current AU and AUv3 bus configuration (Phase 4/5) is unchanged by Phase 6; no audio input or output buses are added, so `au-info.plist` and `audiounitconfig.h` require no modifications.
- The existing `PresetBrowserView` component supports both kit-level and pad-level preset scopes via configuration; a small configuration layer (not a new browser implementation) wires it to each scope.

### Existing Codebase Components (Principle XIV)

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| `ArcKnob` | `plugins/shared/src/ui/arc_knob.h` | Primary knob control for macros, body controls, coupling knobs. |
| `ToggleButton` | `plugins/shared/src/ui/toggle_button.h` | Acoustic/Extended toggle, matrix Solo toggle. |
| `ActionButton` | `plugins/shared/src/ui/action_button.h` | Audition triggers, Reset buttons in the matrix editor. |
| `ADSR Display` | `plugins/shared/src/ui/adsr_display.h` | Pattern/reuse for the Pitch Envelope display; interactive drag for start/end/time. |
| `XY Morph Pad` | `plugins/shared/src/ui/xy_morph_pad.h` | Material Morph envelope editor in Extended mode. |
| `PresetBrowserView` | `plugins/shared/src/ui/preset_browser_view.h` | Kit preset browser and per-pad preset browser (different scopes, same component). |
| `PresetManager` | `plugins/shared/src/preset/preset_manager.h` | Underlying preset save/load; no new preset infrastructure required. |
| `MidiCCManager` | `plugins/shared/src/midi/midi_cc_manager.h` | MIDI Learn support for macros (optional enhancement within scope). |
| Ruinae `controller.cpp` | `plugins/ruinae/src/controller/controller.cpp` | Reference pattern for `createView`, template wiring, `IDependent`. |
| Innexus `controller.cpp` | `plugins/innexus/src/controller/controller.cpp` | Reference pattern for instrument-plugin editor lifecycle. |
| `CouplingMatrix` | `plugins/membrum/src/dsp/coupling_matrix.h` (Phase 5) | Data source for the Tier 2 Matrix View; already exposes `effectiveGain[32][32]`, override state, and resolver. |
| `PadConfig` | `plugins/membrum/src/dsp/pad_config.h` | Per-pad storage extended with 5 new macro fields (offsets 37-41). |
| `VoicePool` / `VoiceAllocator` | `plugins/membrum/src/voice_pool/` + `dsp/.../voice_allocator.h` | Source of active-voice count and per-voice envelope levels for UI meters. |

**Initial codebase search for key terms:**

```bash
grep -r "createView" plugins/ruinae plugins/innexus
grep -r "UIViewSwitchContainer" plugins/
grep -r "IDependent" plugins/
grep -r "kPadMacro" plugins/membrum
grep -r "kUiMode" plugins/membrum
grep -r "editor.uidesc" plugins/membrum
```

**Search Results Summary**: The Ruinae and Innexus editors provide the proven pattern. No `editor.uidesc` exists yet in `plugins/membrum/resources/` (confirmed: directory contains only `au-info.plist`, `auv3/`, `presets/`, `win32resource.rc`). No macro or UI-mode parameters exist yet. The Coupling Matrix data model exists from Phase 5 but has no editor view.

### Forward Reusability Consideration

**Sibling features at same layer:**
- A future Membrum v1 release may add a MIDI Learn panel; the `MidiCCManager` binding used for macros is generalisable.
- Gradus (the arpeggiator) may eventually gain a macro-style UI; the `MacroMapper` pattern developed here is portable.

**Potential shared components:**
- If the Pad Grid view is written with a configurable N x M layout, it could be reused by future multi-pad instruments.
- The `UIViewSwitchContainer` Acoustic/Extended pattern is directly reusable by any future plugin with a dual-complexity UI.

## Out of Scope

The following are **explicitly deferred** from Phase 6 and not part of this spec's delivery:

- **MIDI Learn for macros**: The `MidiCCManager` is referenced as a reusable component, but dedicated MIDI Learn UI for macro controls is deferred to a later version unless trivially added via existing infrastructure.
- **Snare wire modeling UI (deferred from Phase 2)**: The Snare Buzz Tier 1 knob remains the UX surface; actual snare wire DSP is still deferred per Spec 135.
- **Sample layer UI**: The sample layer itself is deferred per Spec 135 §Open Questions.
- **LFOs / mod matrix per pad**: Still open question in Spec 135; not part of Phase 6.
- **Microtuning UI / per-kit tuning tables**: Deferred.
- **Double-membrane UI**: The double-membrane DSP model is itself deferred; consequently no Phase 6 UI.
- **A built-in sequencer**: Spec 135 is explicit -- "No built-in sequencer -- relies on external MIDI (pairs well with Gradus)."
- **GPU-accelerated visualisations**: Coupling matrix activity heatmap and pad glow are CPU-rendered via VSTGUI primitives.
- **Per-pad effect sends (reverb/delay)**: Deferred; auxiliary bus routing remains the mechanism.
- **Animated macro visualisation**: Macros drive underlying parameters silently; no animated graph of the driven values is required in this phase (plain knobs are sufficient).

## Risks

- **R1: Macro curve tuning fatigue**. Choosing the exact up/down offsets for each macro target requires iteration. Mitigation: start with linear/exp defaults from FR-023, verify via listening tests, capture final constants in the MacroMapper as named constexpr values.
- **R2: UI thread stall on matrix editor at 60 Hz**. Drawing 1024 cells per frame plus per-cell activity could exceed a single VSTGUI draw pass budget on low-end hardware. Mitigation: (a) cap matrix-editor redraw to 30 Hz, (b) use dirty-rect invalidation per cell, (c) profile on the CI reference machine and tune.
- **R3: Per-pad envelope publication race**. Writing per-pad amplitude state from the audio thread to the UI thread must be lock-free and tear-safe. Mitigation: use the 1024-bit threshold-gated bitfield (32 x `std::atomic<uint32_t>`) per FR-101; `memory_order_relaxed` for audio-thread writes, `memory_order_acquire` for UI-thread reads. A torn 32-bit word at most causes one incorrect glow bucket for one frame -- visually imperceptible and safe (no crash, no UB).
- **R4: Macro round-trip with older presets**. v5 presets have no macro data. If macros default to 0.5 (neutral), the sound is preserved; if the inverse-mapping path is used, it may produce unexpected macro readings. Mitigation: use the neutral default (0.5) explicitly in the v5->v6 migration; do not attempt inverse mapping.
- **R5: Constitution rule on cross-platform UI**. No native popups or Win32/Cocoa code. Mitigation: all dialogs (preset save name entry, confirmation) use VSTGUI's text-input overlays and `COptionMenu`.
- **R6: Editor window close during matrix Solo engaged**. Solo must disengage or the coupling engine is left in a silenced state. Mitigation: FR-053 mandates automatic Solo disengage on matrix editor close, enforced in the view's destructor/removeView.
- **R7: Parameter ID collision with deferred Phase 2 snare wire**. The deferred snare wire modeling may eventually claim parameter IDs. Mitigation: the Phase 6 global range (280-299) is above the reserved Phase 2 range; any future snare-wire parameters belong in the Phase 2 range (240-249 unused slots) or in `PadConfig`.

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*To be filled in during `/speckit.implement` per Constitution Principle XVI. Each row MUST cite specific file paths, line numbers, test names, and measured values. Generic claims are NOT acceptable.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 .. FR-103 | pending | pending implementation |
| SC-001 .. SC-015 | pending | pending implementation |

**Status Key:**
- MET: Requirement verified against actual code and test output with specific evidence
- NOT MET: Requirement not satisfied (spec is NOT complete)
- PARTIAL: Partially met with documented gap and specific evidence of what IS met
- DEFERRED / PENDING: Not yet verified (either pre-implementation or explicitly deferred with user approval)

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

**Overall Status**: PENDING -- spec only, no implementation yet.

**Recommendation**: Proceed to `/speckit.clarify` (if clarifications are desired) or `/speckit.plan` to break Phase 6 into an implementation plan and task list. No implementation work has been done under this branch.
