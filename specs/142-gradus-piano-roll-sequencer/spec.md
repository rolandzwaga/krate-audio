# Feature Specification: Gradus Piano-Roll Step Sequencer Mode

**Feature Branch**: `142-gradus-piano-roll-sequencer`
**Plugin**: Gradus
**Created**: 2026-05-23
**Status**: Draft
**Input**: User description: "Add a piano-roll step sequencer mode to Gradus as an alternative MIDI note source, alongside existing live MIDI input."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Program a melody in the piano roll and route it through Gradus's lanes (Priority: P1)

A user opens Gradus, switches the new Source toggle from `Live` to `Sequencer`, and programs a short monophonic melody by clicking notes onto a 4-octave piano-roll grid. The programmed melody is then driven through Gradus's existing lane-based processors (velocity, gate, modifier, ratchet, condition, chord, inversion, MIDI delay) exactly as held-note MIDI input would be in Live mode. The user can play the pattern with no MIDI input at all (it plays as written), or hold a note on a controller to transpose the entire pattern.

**Why this priority**: This is the headline feature. Without it, none of the value of this spec exists. It must work end-to-end before the spec is considered useful at any level.

**Independent Test**: With Source = Sequencer, place a 4-step pattern (e.g., C4, E4, G4, rest), set host transport to play, and verify the output MIDI stream emits C4 -> E4 -> G4 -> silence, with all lane processors applied (e.g., velocity lane scaling the velocities, gate lane scaling note durations). Verify identical timing/processing to what a held 4-note chord would receive in Live mode.

**Acceptance Scenarios**:

1. **Given** a fresh Gradus instance with Source = Sequencer and a programmed 4-step pattern, **When** the host transport starts with no MIDI input, **Then** the plugin emits the programmed notes in order at the configured tempo, with all output-side lane processors applied.
2. **Given** Source = Sequencer with the pattern `[60, 64, 67, rest]` and lane Length = 4, **When** the host plays the pattern, **Then** the output MIDI matches notes 60, 64, 67 followed by a silent step (no note-on for the rest step).
3. **Given** Source = Sequencer with a programmed pattern, **When** the user enables the velocity lane and sets per-step velocities `[127, 64, 32, 96]`, **Then** the emitted notes carry those velocities respectively (velocity lane still post-processes sequencer output).
4. **Given** Source = Sequencer with a programmed pattern, **When** the user enables the MIDI delay lane on selected steps, **Then** the programmed notes are echoed by the MIDI delay processor identically to how held-note input would be echoed in Live mode.

---

### User Story 2 - Transpose programmed pattern with held MIDI input (Priority: P1)

While in Sequencer mode, a user holds a note on their MIDI controller. The held note transposes the programmed pattern by `(heldNote - 60)` semitones. Releasing the note and pressing another transposes to the new root. If multiple notes are held, the most-recently-played note wins (last-played root).

**Why this priority**: This is the second pillar of the feature. Without it, the sequencer is a static playback toy. Transposition makes it a performance instrument and is explicitly part of the requested design.

**Independent Test**: With Source = Sequencer and pattern `[60, 64, 67]`, hold MIDI note 60 (C4) -> output is `[60, 64, 67]`. Release and hold MIDI note 62 (D4) -> output becomes `[62, 66, 69]`. Hold both note 60 then play note 65 on top -> output transposes to root 65 (most-recently-played wins).

**Acceptance Scenarios**:

1. **Given** Source = Sequencer, pattern `[60, 64, 67]`, no MIDI input, **When** the pattern plays, **Then** output is `[60, 64, 67]` (no transposition).
2. **Given** Source = Sequencer, pattern `[60, 64, 67]`, **When** MIDI note 62 is held, **Then** output is `[62, 66, 69]` (transposed +2 semitones).
3. **Given** Source = Sequencer with held note 60, **When** the user additionally plays note 65 while still holding 60, **Then** subsequent pattern steps are transposed by `+5` semitones (last-played note wins as transposition root).
4. **Given** Source = Sequencer, pattern `[60, 64, 67]`, **When** held note 62 is released and held note 64 was pressed before it, **Then** transposition root falls back to 64 (next-most-recent still-held note).
5. **Given** Source = Sequencer with pitch lane active (e.g., `[0, +12, -7, 0]`), **When** held note 62 transposes the pattern, **Then** pitch lane offsets are added on top of the transposed notes (additive behavior).

---

### User Story 3 - Author melody visually in a piano-roll editor (Priority: P1)

A user interacts with a new piano-roll view in Gradus's UI to author the melody. The view shows a fixed 4-octave window (C2 to B5, MIDI 36-83), with horizontal step columns matching the Sequencer Note Lane's length parameter. Clicking a cell places a note at that step/pitch. Click-and-drag paints multiple steps. Right-click marks a step as a rest. The view is visible only when Source = Sequencer.

**Why this priority**: The feature is unusable without an editor. The piano-roll view is part of the feature's identity (vs. a hidden list of parameter sliders).

**Independent Test**: Open the plugin UI, switch Source to Sequencer, click a cell at step 1 / pitch 60, then click a cell at step 2 / pitch 64. Verify visual feedback shows the two notes; play the host transport and confirm the audible output reflects those notes. Right-click step 3 and verify it is rendered as a rest and produces no note-on at that step.

**Acceptance Scenarios**:

1. **Given** the Gradus editor is open with Source = Live, **When** the user looks at the UI, **Then** the piano-roll view is hidden.
2. **Given** the Gradus editor is open with Source = Sequencer, **When** the user looks at the UI, **Then** the piano-roll view is visible and displays the current pattern.
3. **Given** the piano-roll view is visible and empty, **When** the user clicks the cell at column 0, row corresponding to MIDI 60, **Then** that step's pitch becomes 60 and its rest flag becomes 0 (play), reflected in the underlying parameters and persisted. **Given** that same step already has pitch 60 and rest = 0, **When** the user clicks the same cell again, **Then** the rest flag becomes 1 (toggle-to-rest).
4. **Given** an existing note at step 2, **When** the user right-clicks that step, **Then** the step's rest flag becomes 1 (rest) and the step renders as a rest in the view.
5. **Given** the piano-roll view is visible, **When** the user drags from step 0 to step 5 at pitch 60, **Then** steps 0..5 are unconditionally set to pitch 60 and play (rest = 0), regardless of any prior cell state. No toggle-to-rest behavior occurs during the drag. Results are persisted to the underlying parameters.
6. **Given** the lane length parameter is changed from 16 to 8 while the piano-roll view is visible, **When** the user looks at the view, **Then** only the first 8 step columns are editable / active (matching existing lane length behavior).
7. **Given** the piano-roll view is visible, **When** the user views the pitch range, **Then** exactly 4 octaves from C2 to B5 (MIDI 36-83) are shown with no scrolling.

---

### User Story 4 - Patterns persist with presets and across reloads (Priority: P1)

A user programs a pattern, saves it as a Gradus preset, closes the host, reopens the host and the preset, and finds the pattern intact along with the Source = Sequencer setting. Existing Live-mode presets created before this feature shipped continue to load and behave identically (no regression).

**Why this priority**: Persistence is a hard requirement of any plugin parameter. Backward compatibility with existing presets is non-negotiable -- breaking existing users' workflows is unacceptable.

**Independent Test**: (a) Program a 32-step pattern in Sequencer mode, save preset, reload preset -> pattern matches exactly. (b) Load a pre-existing Gradus preset that was saved before this feature shipped -> it loads as Source = Live and behaves identically to its pre-update behavior (no parameter drift, no warnings, no unexpected Sequencer mode activation).

**Acceptance Scenarios**:

1. **Given** Source = Sequencer with a 32-step pattern, **When** the user saves the preset, closes and reopens the host, and reloads the preset, **Then** all 32 step pitches, all 32 rest flags, the lane length, swing, jitter, speed, speed-curve depth, and the Source = Sequencer mode are restored exactly.
2. **Given** a pre-existing Gradus preset saved before this feature shipped, **When** the user loads it, **Then** Source defaults to Live (no Source parameter in state stream) and the plugin's audible/MIDI behavior is unchanged.
3. **Given** a preset saved with Source = Sequencer and a non-empty pattern, **When** the host saves and reloads its session state, **Then** the same state is restored without preset bank involvement (host-level state path also works).

---

### User Story 5 - Lanes irrelevant in Sequencer mode are disabled (Priority: P2)

Certain ordering-related controls (ArpMode, OctaveRange, OctaveMode, ScaleQuantizeInput) are conceptually meaningless when the pattern itself defines the ordering. In Sequencer mode these controls are visually disabled and ignored at the audio thread, so the user is not confused by knobs that have no effect.

**Why this priority**: Important for UX clarity, but not a blocker for the core feature. Without it the feature still works (the controls would just be silently ignored at the audio thread).

**Independent Test**: Switch to Sequencer mode, observe that ArpMode, OctaveRange, OctaveMode, and ScaleQuantizeInput controls are visually disabled. Toggle ArpMode through its values -> output pattern order does not change. Switch back to Live mode -> those controls re-enable and resume their normal behavior.

**Acceptance Scenarios**:

1. **Given** Source = Sequencer, **When** the user looks at the UI, **Then** the ArpMode, OctaveRange, OctaveMode, and ScaleQuantizeInput controls are visually disabled (greyed out or hidden, consistent with existing UI conventions).
2. **Given** Source = Sequencer with a programmed pattern, **When** ArpMode is changed (via host automation or UI), **Then** the output pattern order remains the order written in the piano roll.
3. **Given** Source = Live, **When** the user looks at the UI, **Then** ArpMode, OctaveRange, OctaveMode, and ScaleQuantizeInput controls are enabled and behave as today.

---

### User Story 6 - Output-side scale quantize still applies in Sequencer mode (Priority: P3)

The scale-quantize-output stage (which quantizes emitted notes to a scale) continues to apply in Sequencer mode. A user can program a pattern, set the output scale to, say, C minor, and the emitted notes (including transposed and pitch-lane-modified notes) are quantized to that scale.

**Why this priority**: Smaller but real value-add -- keeps the feature consistent with the existing pipeline. Not blocking core functionality.

**Independent Test**: With Source = Sequencer, pattern `[60, 64, 67]`, output scale = C minor, expect emitted notes to be quantized to the nearest scale tone (so 64 = E becomes E-flat = 63). Disable the output scale and verify the unquantized pattern emits.

**Acceptance Scenarios**:

1. **Given** Source = Sequencer with output scale quantize enabled and set to C minor, **When** the pattern `[60, 64, 67]` plays, **Then** emitted MIDI notes are quantized to C minor (e.g., 64 -> 63).
2. **Given** Source = Sequencer with output scale quantize disabled, **When** any pattern plays, **Then** emitted notes pass through unquantized.

---

### Edge Cases

- **Source toggled mid-playback**: Switching between Live and Sequencer while the transport is running must not produce stuck notes, MIDI panics, or click artifacts. Currently-sounding notes from the previous source must receive note-offs cleanly. Pending MIDI Delay lane echoes from the previous source MUST fire as scheduled (natural tail-out); no new echoes are generated until the new source emits notes.
- **No held note + Velocity lane disabled**: Sequencer-emitted notes carry a base velocity of 100 (per FR-025a). The Velocity lane is a multiplier; with the lane disabled (or all-1.0 values) the emitted velocity is 100.
- **Held note with velocity = 0 (MIDI note-on with vel 0 = note-off convention)**: Treated as note-off; the previous held note (if any still held) becomes the transposition root per FR-018. Test coverage: this scenario MUST be verified in the held-note transpose test suite (source_mode_transpose_test.cpp).
- **`kArpTransposeId` automation while in Sequencer mode**: The global transpose stacks additively with held-note transpose and pitch lane per FR-021. Automating it during playback simply re-evaluates the pitch formula each step; no special handling.
- **Retrigger = Note + rapid note-on retriggers**: Each held note-on resets the Sequencer Note lane's playhead to step 0. Rapidly repeated note-ons cause the pattern to start over from the top each time; this is intentional per FR-022a.
- **State migration from v2 preset**: Old v2 presets that pre-date this feature MUST load with Source = Live, default-rest pattern (all rest=1, all pitch=60), default Length = 16, and produce byte-identical Live-mode MIDI to the pre-feature version (SC-004, FR-039a, FR-039b).
- **All steps marked rest**: When every step is a rest, no MIDI is emitted but the playhead continues to advance (so resuming with a played step still works at the right time). Lane processors must not malfunction.
- **Held note + Sequencer mode toggled to Live**: When the user switches Source to Live while still holding a MIDI note, the held note becomes the live input as normal (no stuck transposition).
- **Held note released while pattern is playing**: When the last held note releases, the transposition root reverts to none (no transposition), but the pattern keeps playing at concert pitch from the next step onward.
- **Multiple held notes**: Last-played note is the transposition root. If the last-played note is released, the next-most-recent still-held note becomes the root.
- **Lane Length = 1**: The pattern degenerates to a single repeating step. Valid behavior, no special-casing needed.
- **Transposition causing MIDI out of range**: Programmed pitch 60 + transposition root = 100 + pitch lane = +24 yields MIDI 124. Out-of-range notes (< 0 or > 127) MUST be clamped or dropped per existing Gradus output-clamp behavior (consistent with how the pitch lane behaves in Live mode today -- same clamp policy).
- **Right-click on an empty step**: Right-click on a step that has no note -> step's rest flag stays 1 (rest), no note is placed.
- **Click on a step with a rest set**: Click on a step that is marked rest -> the step's pitch parameter is set to the clicked row's MIDI pitch AND the rest flag clears to 0 (becomes play), regardless of the step's previously stored pitch.
- **Click on a step at a different pitch (replace)**: Click on a step whose current pitch does NOT match the clicked row's pitch -> step pitch is silently overwritten with the clicked pitch, rest flag clears to 0. One-note-per-step; no modifier required.
- **Click on a step at the same pitch (toggle-to-rest)**: Click on a step whose pitch already matches the clicked row and whose rest flag is 0 -> rest flag sets to 1 (step becomes rest). A subsequent click on that step clears the rest flag (step becomes play again at the same pitch). This is the canonical "erase a note" gesture.
- **Lane Length changed while pattern is playing**: Mid-playback lane length change must follow the same behavior as existing lanes (no crash, playhead wraps to the new length on the next loop boundary).
- **Click and drag outside the piano-roll bounds**: Mouse drag that exits the view bounds must not crash and must clamp to the visible step/pitch range. Drag always paints (always-paint semantics; toggle-to-rest is single-click-only and never fires during drag). The paint pitch remains locked to the row where the drag started regardless of vertical exit from the view.

## Clarifications

### Session 2026-05-23

- Q: When a user clicks a cell that is already filled (same pitch, not a rest) — what happens? → A: Toggle to rest. Clicking a cell that already has that exact pitch sets its rest flag to 1 (erases it). A subsequent click re-places the note.
- Q: When the user drags across cells that are already filled — does the drag toggle them to rest, or always paint them? → A: Always paint. Drag sets each dragged cell to that row's pitch and rest = 0 regardless of prior state; no toggling during drag. Toggle-to-rest fires only on isolated single clicks (not during drag).
- Q: Clicking a step that already has a note, but at a different pitch row — what happens? → A: Silent replace. The step's pitch parameter is overwritten with the newly clicked row's pitch; the step's rest flag clears to 0 (play). One-note-per-step is a strict invariant; no modifier key is required or recognised.
- Q: During a click-and-drag, what pitch row governs painting as the mouse moves vertically across rows? → A: Lock to start pitch. The pitch row is fixed to the row where the drag began. Horizontal mouse motion across step columns paints that same locked pitch; vertical mouse motion during the drag is ignored (pitch does not follow the cursor). A new drag picks up a new start-row pitch.
- Q: Should the piano-roll view show a playhead cursor indicating the currently-active step during playback? → A: Yes. The view highlights the currently-active step column in real time, driven by the Sequencer Note lane's Playhead parameter, mirroring the ring view's playhead convention.

### Session 2026-05-23 (Grilling pass — overlap with existing arp features)

- Q: Existing `kArpTransposeId` (-24..+24 semitone, scale-quantized) in Sequencer mode — interact with held-note transpose how? → A: Additive stacking. `finalPitch = programmedPitch + (heldNote - 60) + kArpTranspose + pitchLaneOffset`, then output scale quantize.
- Q: Latch mode (`kArpLatchModeId`: Off/Hold/Add) in Sequencer mode? → A: Greyed/ignored in Sequencer mode. Transposition root always reverts on release of the last held note (i.e., behaves as Latch=Off regardless of the param's value). Param visible in UI but disabled.
- Q: Retrigger (`kArpRetriggerId`: Off/Note/Beat) in Sequencer mode? → A: Kept active with full semantics. Note retrigger resets the Sequencer Note lane playhead to step 0 on each new held note-on (the same gesture that updates the transposition root). Beat retrigger resets playhead on transport beat. All other lane playheads are independently affected per their existing per-lane behavior.
- Q: Note-source-generating features (`kArpMarkov*`, `kArpEuclidean*`, `kArpPinNote*` + 32 pin flags) in Sequencer mode? → A: All greyed/ignored. Add to the same "disabled in Seq mode" bucket as ArpMode/OctaveRange/OctaveMode/ScaleQuantizeInput. Params persist for backward compat but inert.
- Q: Note Range Mapping (`kArpRangeLowId`, `kArpRangeHighId`, `kArpRangeModeId`: Wrap/Clamp/Skip) in Sequencer mode? → A: Greyed/ignored. Range mapping is conceptually a held-input → play-range mapper; in Sequencer mode the pattern is the source, not held input.
- Q: Base velocity of sequencer-emitted notes (programmed pattern has no per-step velocity)? → A: Use the last-played held note's velocity if any note is held; otherwise default 100. Velocity lane scales on top as in Live mode.
- Q: Sequencer Note lane clocking model relative to other lanes? → A: Polymetric. The Sequencer Note lane has its own length/speed/swing/jitter/speed-curve-depth, independently advancing alongside the existing lanes' own playheads (same model as today). The Sequencer Note lane provides the "current source pitch" at its current playhead; other lanes provide their values at their own playheads.
- Q: State versioning when adding new params (Source + Sequencer Note lane params)? → A: Bump `kCurrentStateVersion` from 2 to 3. `setState` MUST detect `version == 2` and call a legacy loader that loads only the v2 param block; new params default to their declared defaults (Source=Live, all rest flags=1, all pitches=60, lane Length=16, lane modulators=neutral). A unit test MUST verify v2→v3 load produces byte-identical Live-mode MIDI output for a saved-then-loaded v2 preset.
- Q: Default state of rest flags on fresh instantiation? → A: Default rest flag = 1 (rest) for all 32 steps. Fresh piano roll is silent until the user clicks. Default pitch stays at 60 (C4) so first click on any row places a played note at that row's pitch.
- Q: Default Sequencer Note lane Length on fresh instantiation? → A: 16 steps.
- Q: Pending MIDI Delay lane echoes when Source toggles mid-playback? → A: Pending echoes fire as scheduled (natural tail-out). No new echoes are generated until the new source emits notes. Note-on/note-off pairing remains preserved per FR-025.

## Requirements *(mandatory)*

### Functional Requirements

**Source Mode Toggle**

- **FR-001**: Gradus MUST expose a new VST3 parameter representing the note source mode with the identifier `kArpSourceModeId`, a 2-entry list (`Live`, `Sequencer`), defaulting to `Live`.
- **FR-002**: Existing presets saved before this feature shipped MUST load with Source = `Live` (i.e., absent state field is interpreted as Live mode), with zero audible/MIDI behavior change from before this feature.
- **FR-003**: The Source parameter MUST be host-automatable and persist via the existing parameter/state machinery (no special-case state stream code).

**Sequencer Note Lane Parameters**

- **FR-004**: Gradus MUST expose a new lane "Sequencer Note" with the same structural conventions as existing lanes (per-step parameters, lane-level modulators).
- **FR-005**: The Sequencer Note lane MUST expose a Length parameter accepting 1-32 steps, with the same min/max as existing lanes (`kMaxSteps = 32`). **Default Length = 16.**
- **FR-006**: The Sequencer Note lane MUST expose 32 per-step MIDI note parameters with range 0-127 and default 60 (C4).
- **FR-007**: The Sequencer Note lane MUST expose 32 per-step rest flag parameters (0 = play, 1 = rest). **Default rest flag = 1 (rest)** so a fresh pattern is silent until the user populates it.
- **FR-008**: The Sequencer Note lane MUST expose a lane-level Speed multiplier (mirroring other lanes' speed parameter range/units).
- **FR-009**: The Sequencer Note lane MUST expose a lane-level Swing parameter (mirroring other lanes' swing parameter range/units).
- **FR-010**: The Sequencer Note lane MUST expose a lane-level Length Jitter parameter (mirroring other lanes' jitter parameter range/units).
- **FR-011**: The Sequencer Note lane MUST expose a lane-level Speed Curve Depth parameter (mirroring other lanes' speed-curve depth parameter range/units).
- **FR-012**: The Sequencer Note lane MUST expose a hidden output-only Playhead parameter (mirroring other lanes' playhead parameter for UI display purposes).
- **FR-013**: All new Sequencer Note lane parameter IDs MUST be allocated starting at `3741` in a dense contiguous block, immediately after the current `kArpMidiDelayPlayheadId = 3740`.

**Audio-Thread Behavior (Live mode)**

- **FR-014**: When Source = `Live`, Gradus's audio-thread behavior MUST be byte-for-byte identical to the current behavior -- same MIDI output for the same MIDI input, same automation response, same internal state.

**Audio-Thread Behavior (Sequencer mode)**

- **FR-015**: When Source = `Sequencer` and no MIDI notes are held, Gradus MUST emit the programmed pattern as written, advancing one step per lane tick (subject to Speed / Swing / Jitter / Speed Curve Depth modulators of the Sequencer Note lane).
- **FR-016**: When Source = `Sequencer` and one MIDI note is held, Gradus MUST emit the programmed pattern transposed by `(heldNote - 60)` semitones. C4 (MIDI 60) acts as the unity-transposition reference pitch.
- **FR-017**: When Source = `Sequencer` and multiple MIDI notes are held, the **most-recently-pressed** still-held note MUST act as the transposition root.
- **FR-018**: When Source = `Sequencer` and the most-recently-pressed held note is released, the transposition root MUST fall back to the next-most-recently-pressed still-held note. When no notes are held, transposition reverts to none (root = 60, i.e., no offset).
- **FR-019**: When Source = `Sequencer`, steps with `rest = 1` MUST NOT emit a note-on, but the playhead MUST still advance through the rest step at the configured tempo (rests consume time).
- **FR-020**: When Source = `Sequencer`, the output of the Sequencer Note lane MUST be fed into the existing lane processors in the same order and with the same semantics as held-note input is fed in Live mode. Specifically, these lanes MUST still apply: Velocity, Gate, Modifier, Ratchet, Condition, Chord, Inversion, MIDI Delay.
- **FR-021**: When Source = `Sequencer`, the Pitch lane MUST be applied additively on top of the transposed programmed pitch. The full pitch formula is `finalPitch = programmedPitch + (heldNote - 60) + kArpTranspose + pitchLaneOffset`, evaluated before the output scale-quantize stage. All four contributions stack additively (FR-021a).
- **FR-021a**: The existing global Transpose parameter (`kArpTransposeId`, -24..+24 semitones, scale-quantized) MUST remain active in Sequencer mode and stack additively with the held-note transpose, the programmed pitch, and the pitch lane offset (see FR-021). It is NOT placed in the disabled-controls bucket of FR-022.
- **FR-022**: When Source = `Sequencer`, the following parameters MUST be ignored on the audio thread (they have no effect on emitted MIDI in Sequencer mode):
  - `kArpModeId` (Up/Down/UpDown/etc. — ordering is defined by the pattern)
  - `kArpOctaveRangeId`, `kArpOctaveModeId` (octave traversal is replaced by pattern-defined pitches)
  - `kArpScaleQuantizeInputId` (no held-input quantize; pattern is the source)
  - `kArpLatchModeId` (Off/Hold/Add — irrelevant; transposition root always reverts on release of the last held note, behaving as Latch=Off)
  - `kArpMarkovPresetId` + 49 matrix cells (`kArpMarkovCell00Id`..`kArpMarkovCell66Id`) — Markov-driven note generation is replaced by pattern
  - `kArpEuclideanEnabledId`, `kArpEuclideanHitsId`, `kArpEuclideanStepsId`, `kArpEuclideanRotationId` — Euclidean rhythm generation overlaps with rest flags and pattern length
  - `kArpPinNoteId` + 32 pin flags (`kArpPinFlagStep0Id`..`kArpPinFlagStep31Id`) — step pinning is replaced by direct pattern editing
  - `kArpRangeLowId`, `kArpRangeHighId`, `kArpRangeModeId` — note range mapping is conceptually a held-input mapper; pattern is the source in Seq mode
- **FR-022a**: The `kArpRetriggerId` parameter (Off/Note/Beat) MUST remain active in Sequencer mode with full semantics:
  - `Note`: each held note-on (which also updates the transposition root) resets the Sequencer Note lane's playhead to step 0.
  - `Beat`: transport beat boundary resets the Sequencer Note lane's playhead to step 0.
  - All other lanes' playheads are affected by retrigger per their existing per-lane behavior (unchanged from Live mode).
- **FR-022b**: The `kArpSpiceId` (Spice), `kArpDiceTriggerId` (Dice), `kArpHumanizeId` (Humanize) parameters and per-lane Speed Curve Depth / Swing / Jitter parameters MUST continue to apply in Sequencer mode (no change from Live mode behavior).
- **FR-023**: When Source = `Sequencer`, the output-side scale quantize stage MUST continue to apply as it does in Live mode.
- **FR-024**: Out-of-range emitted MIDI notes (below 0 or above 127) MUST follow the same clamp/drop policy as the existing Pitch lane uses today in Live mode -- there must be no new policy introduced.
- **FR-025**: Switching the Source parameter between Live and Sequencer at any time, including mid-playback, MUST NOT produce stuck notes; any currently-sounding note from the previous source MUST receive a clean note-off. **Pending MIDI Delay lane echoes scheduled before the toggle MUST fire as scheduled (natural tail-out); no new echoes are generated until the new source emits notes.**
- **FR-025a**: The base velocity for sequencer-emitted notes (before the Velocity lane scales it) MUST be:
  - The velocity of the most-recently-pressed still-held MIDI note, if any note is held.
  - Otherwise, a default of 100 (out of 127).
  - The Velocity lane then scales this base velocity per existing semantics.
- **FR-025b**: The Sequencer Note lane MUST advance polymetrically alongside other Gradus lanes, each driven by its own length/speed/swing/jitter/speed-curve-depth parameters. There is no "master" lane in Sequencer mode; the Sequencer Note lane provides the current source pitch at its own playhead position while other lanes provide their values at their own independent playheads.

**UI**

- **FR-026**: Gradus's editor MUST contain a new piano-roll view that displays the Sequencer Note lane's pattern.
- **FR-027**: The piano-roll view MUST be visible if and only if Source = `Sequencer`; in Live mode it MUST be hidden.
- **FR-028**: The piano-roll view MUST show a fixed 4-octave pitch range from C2 (MIDI 36) to B5 (MIDI 83), with no scrolling for v1.
- **FR-029**: The piano-roll view's horizontal step grid MUST match the Sequencer Note lane Length parameter (only the first `Length` columns are active/editable).
- **FR-030**: Clicking a cell in the piano-roll view MUST obey the following strict one-note-per-step rules:
  - **Different-pitch click (replace)**: If the clicked row's MIDI pitch differs from the step's current pitch, the step's pitch parameter is overwritten with the clicked pitch and the rest flag clears to 0 (play). This is a silent replace; no modifier key is required or recognised.
  - **Same-pitch click (toggle-to-rest)**: If the clicked row's MIDI pitch already equals the step's current pitch and the step is not a rest, the click sets the rest flag to 1 (erases the note). A subsequent click on that same resting step clears the rest flag (re-places the note at the same pitch).
  - **Click on a resting step**: Regardless of the step's stored pitch, clicking any row clears the rest flag and sets the pitch to the clicked row's pitch.
  - Only one MIDI pitch may be active per step at any time (strict monophonic invariant).
- **FR-031**: Click-and-drag in the piano-roll view MUST always paint notes across the dragged step columns using the pitch row where the drag began (lock-to-start-pitch): the pitch is fixed to the row where the mouse button was pressed, and vertical mouse motion during the drag is ignored (the paint pitch does not follow the cursor). Each step column under the drag is unconditionally set to that locked pitch with rest = 0, regardless of prior cell state. Toggling to rest is NOT triggered during a drag; toggle-to-rest fires only on an isolated single click (no mouse movement between press and release). Drag painting is bounded by the visible step range. Releasing and starting a new drag picks up a new start-row pitch.
- **FR-032**: Right-click (or platform-appropriate equivalent gesture) on a step in the piano-roll view MUST set that step's rest flag to 1 (rest).
- **FR-033**: The piano-roll view MUST be implemented as a cross-platform VSTGUI custom view. Platform-specific UI code (Win32, Cocoa/AppKit, GTK natives) is FORBIDDEN per project rules.
- **FR-034**: The piano-roll view MUST reflect external changes to the underlying step parameters (e.g., from preset load, automation, or host) in real time per the project's standard VST3 parameter-binding patterns.
- **FR-034a**: The piano-roll view MUST display a real-time playhead cursor that highlights the currently-active step column during playback. The cursor is driven by the Sequencer Note lane's hidden Playhead parameter (FR-012) using the same read pattern as the ring view's playhead indicator (no new parameter or communication path required).
- **FR-035**: The existing ring view's Pitch lane indicator MUST continue to display additive offsets unchanged. No UI change is made to the ring view.
- **FR-036**: When Source = `Sequencer`, the UI MUST visually disable every control whose audio-thread effect is ignored per FR-022. Visual disable is implemented via `setMouseEnabled(false)` and `setAlphaValue(0.4f)` on each affected control view (consistent with the existing disabled-control pattern in the Gradus controller). Every control listed below MUST be both non-interactive and visually dimmed:
  - ArpMode, OctaveRange, OctaveMode, ScaleQuantizeInput
  - LatchMode
  - All Markov controls (preset dropdown + 49 matrix cells)
  - All Euclidean controls (enabled, hits, steps, rotation)
  - All Pin Note controls (pin note + 32 pin flags)
  - All Note Range Mapping controls (low, high, mode)
  Controls whose audio-thread effect is preserved (Retrigger per FR-022a, Transpose per FR-021a, Spice/Dice/Humanize/per-lane curves per FR-022b) MUST remain enabled and interactive.

**Persistence**

- **FR-037**: All new Sequencer Note lane parameters and the Source parameter MUST persist via Gradus's existing state save/load machinery -- no parallel storage path.
- **FR-038**: Sequencer Note patterns MUST be saved into and restored from Gradus presets through the existing preset machinery.
- **FR-039**: Loading a preset saved with Source = `Sequencer` into a Gradus instance running this version MUST restore the Source parameter, all 32 step pitches, all 32 rest flags, lane length, and all lane-level modulators (speed, swing, jitter, speed-curve depth) exactly.
- **FR-039a**: `kCurrentStateVersion` MUST be bumped from 2 to 3 for this feature. `Processor::setState` MUST inspect the version field and dispatch to a legacy loader for `version == 2`. The legacy loader:
  - Reads only the v2 parameter block (no Source / Sequencer Note lane params in the stream)
  - Defaults the Source parameter to `Live`
  - Defaults all 32 Sequencer Note rest flags to 1 (rest)
  - Defaults all 32 Sequencer Note pitches to 60 (C4)
  - Defaults Sequencer Note lane Length to 16
  - Defaults Sequencer Note lane modulators to their declared neutral values (speed=1.0x, swing=0, jitter=0, speed-curve depth=0)
- **FR-039b**: A unit test MUST exercise the v2→v3 migration: save a v2 state (using the pre-feature code path or a captured byte stream), load it via the v3 `setState`, and assert byte-identical Live-mode MIDI output over a fixed 60-second test sequence (covering SC-004).

**Validation**

- **FR-040**: The built Gradus.vst3 MUST pass `pluginval --strictness-level 5 --validate` with this feature enabled, with no new failures or warnings compared to the baseline before this feature.

### Key Entities

- **Source Mode (parameter)**: A 2-entry list (`Live`, `Sequencer`) controlling whether the active note source is live MIDI input or the programmed sequencer pattern. Default `Live`. Persists in state and presets.
- **Sequencer Note Lane (parameter group)**: A lane structurally analogous to the existing Gradus lanes. Contains:
  - Length (1-32)
  - 32 per-step MIDI note (0-127, default 60)
  - 32 per-step rest flag (0/1, default 1)
  - Lane-level Speed, Swing, Length Jitter, Speed Curve Depth (mirroring other lanes)
  - Hidden Playhead (output-only)
- **Sequencer Pattern (runtime concept)**: The active (length, pitches, rest flags) tuple driving step emission when Source = Sequencer. Owned by the audio thread, mirrored to the UI via the standard parameter binding.
- **Transposition Root (runtime concept)**: The MIDI pitch (or "none") that the audio thread is currently using to transpose the pattern. Derived from the held-note stack; updated on note-on/note-off events. Not a persisted parameter.
- **Piano-Roll View (UI entity)**: A new VSTGUI custom view rendering the Sequencer Note lane's pattern on a fixed C2-B5 grid; visible only when Source = Sequencer.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: A user can author and play back a monophonic 1-32 step melody entirely from within Gradus's UI in under 2 minutes from opening the plugin, without consulting documentation. Verified by timing the sequence: open plugin → switch Source to Sequencer → click 4 cells → start host transport → confirm audible output. Timer starts on plugin open and stops when notes are heard. Target: elapsed time < 120 seconds.
- **SC-002**: When Source = `Sequencer`, the programmed pattern's emitted MIDI passes through 100% of the output-side lane processors (Velocity, Gate, Modifier, Ratchet, Condition, Chord, Inversion, MIDI Delay) with the same per-step semantics those processors have today in Live mode -- verified by a side-by-side test: same pattern fed (a) as held-note Live input and (b) as a Sequencer-mode pattern produces identical post-processor MIDI streams.
- **SC-003**: When Source = `Sequencer` and a single MIDI note is held, every emitted programmed-pitch MIDI message is offset by exactly `(heldNote - 60)` semitones from the programmed value (before output scale quantize), verified across at least 12 held-note values spanning the keyboard.
- **SC-004**: Loading 100% of pre-existing Gradus presets (created before this feature shipped) produces byte-identical MIDI output to the previous Gradus version for the same input over a fixed 60-second test sequence -- i.e., zero regression in Live mode.
- **SC-004b**: Loading 100% of shipped Ruinae factory presets (which share `ArpeggiatorCore` with Gradus) produces byte-identical MIDI output to the previous Ruinae version over the same fixed input. This proves that the `kNumLanes` 9→10 extension and lane 10's conditional-inert branch do not perturb Ruinae's behavior (Ruinae has no Sequencer mode and never sets `sourceMode_ = Sequencer`).
- **SC-005**: A round-trip preset save -> host restart -> preset reload restores 100% of the Sequencer Note lane state (all 32 step pitches, all 32 rest flags, lane length, speed, swing, jitter, speed-curve depth, Source = Sequencer) with bit-exact parameter values.
- **SC-006**: The built Gradus.vst3 passes `pluginval --strictness-level 5 --validate` with zero new failures and zero new warnings compared to the baseline before this feature was implemented.
- **SC-007**: Toggling Source between Live and Sequencer mid-playback over 100 consecutive transport-running toggles produces zero stuck notes (every note-on observed has a matching note-off within the same transport cycle).
- **SC-008**: When all 32 steps are marked as rests, the plugin emits zero note-on messages while the playhead advances continuously -- verified by a 32-step rest-only test.
- **SC-009**: The piano-roll view renders and responds to clicks/drags/right-clicks on all 3 supported platforms (Windows, macOS, Linux) using only VSTGUI cross-platform APIs -- verified by build success on all platform CI jobs and manual interaction testing on each.
- **SC-010**: Switching Source = `Sequencer` and pressing the most-recently-played note out of 4 simultaneously held notes produces a transposition root equal to that most-recently-played note within one audio block of the note-on, verified by an automated test on the audio thread.

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- **State versioning is mandatory.** Gradus state is a sequential binary stream (not keyed by parameter ID), so adding new params requires bumping `kCurrentStateVersion` and an explicit legacy loader for the prior version. See FR-039a, FR-039b. This is non-negotiable for backward compatibility.
- **Default Sequencer pattern is silent.** All 32 rest flags default to 1, so a freshly added Gradus instance in Sequencer mode emits no notes until the user populates the pattern. Default pitch = 60 means the first click on any row simply sets the rest flag to 0 at the clicked pitch.
- **Sequencer Note lane is "just another lane."** It has the same modulator structure as Velocity / Gate / Pitch / Modifier / Ratchet / Condition / Chord / Inversion / MIDI Delay lanes (Length + Speed + Swing + Length Jitter + Speed Curve Depth + Playhead) and advances polymetrically on its own clock. This keeps the audio-thread architecture identical to today; only the input-stage note-source path changes.
- **Sequencer mode greys out conflicting note-source and note-mapping features.** See FR-022 and FR-036 for the full list (ArpMode, OctaveRange, OctaveMode, ScaleQuantizeInput, LatchMode, all Markov controls, all Euclidean controls, all Pin Note controls, all Range Mapping controls). Each of these either generates notes (Markov), defines a rhythm pattern (Euclidean), overrides programmed pitches (PinNote), or maps from a held-input space that no longer applies (ArpMode/Octave/Range). Greying preserves the parameter for backward compatibility but makes the UI honest about effect.
- **Sequencer mode keeps the global Transpose param active.** `kArpTransposeId` (-24..+24, scale-quantized) acts as an additional global offset that stacks additively with held-note transpose and pitch lane (see FR-021, FR-021a). This is the most musically useful interpretation: held-note transpose is a "current root" gesture, while global Transpose is a "permanent key shift."
- **Sequencer mode keeps Retrigger active.** `Note` retrigger is a key live-performance gesture: pressing a new note both updates the transposition root and restarts the pattern from step 0. See FR-022a.
- **Sequencer mode keeps Spice / Dice / Humanize and per-lane modulators active.** These are global expressivity/randomization layers; they apply to Sequencer-emitted notes exactly as they apply to Live-mode notes. See FR-022b.
- **Base velocity for emitted notes is dynamic.** Last-played held note's velocity wins (musical expressivity carries from the controller); falls back to 100 when no note held (sensible default). Velocity lane scales on top. See FR-025a.
- **Pending MIDI Delay echoes survive Source toggles.** Echoes that were validly scheduled before the toggle fire as scheduled (natural tail-out). New echoes are not generated until the new source emits notes. See FR-025 (extended).
- **C4 = MIDI 60** is the unity-transposition reference pitch. This is the standard convention; documenting it so it isn't litigated downstream.
- **Last-played note wins** semantics for transposition root match how most arp/sequencer plugins behave; if the user wanted a different policy (lowest-note, highest-note, first-played), they would have said so.
- The **piano-roll view's fixed 4-octave window (C2-B5)** covers the practical monophonic melody range. Programmed pitches outside this range remain valid in the underlying parameter (0-127) -- they're just not editable via the v1 piano-roll view (but transposition can still push them around). v2 may add scrolling.
- **Right-click as rest gesture** is the natural piano-roll convention. On platforms without a literal right mouse button, an equivalent VSTGUI gesture (e.g., modifier+click) will be used per VSTGUI conventions.
- **"Source mode disables ArpMode/OctaveRange/OctaveMode/ScaleQuantizeInput"** means visually greyed out in the UI AND ignored at the audio thread. The parameters still exist (they must, for backward compatibility), they're just inert in Sequencer mode.
- **Scale snap on placed notes is deferred to v2** per explicit user direction -- not a bug.
- **Polyphonic patterns are deferred to v2** per explicit user direction -- v1 is strictly monophonic (one note per step, or rest).
- **MIDI drag-export of patterns is deferred** per explicit user direction.
- **The Sequencer Note lane's "speed", "swing", "jitter", "speed curve depth"** parameters share the exact same value range and semantics as the same-named parameters on other Gradus lanes (e.g., Velocity lane, Pitch lane). They are not new behaviors -- they are the same modulators replicated on this new lane.
- **Pitch lane is additive on top of transposed programmed pitch.** This is the most musically useful default per the user's stated "subtle melodic variation" use case. (Alternative would be "replace", but the user explicitly said additive.)
- **Output scale quantize still applies in Sequencer mode.** Consistent with the existing pipeline; input scale quantize (which applies to held input) is irrelevant in Sequencer mode because the input is the pattern, not held notes.
- **Held notes do NOT gate the sequencer's emission**. Per the spec input, patterns play with no input held. Held notes only set the transposition root. (If patterns required held input to gate, the input would have said so.)
- **The Source toggle, the piano-roll view, and the Sequencer Note lane parameters are Gradus-exclusive.** They live at parameter IDs 3741+ outside the Ruinae-shared 3000-3372 block, so they do not affect Ruinae.

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| Lane parameter scheme (per-step + lane-level modulators) | `plugins/gradus/src/plugin_ids.h` (e.g., `kArpVelocityLane*`, `kArpPitchLane*`, `kArpMidiDelay*`) | Should reuse the structural pattern; the Sequencer Note lane mirrors this exact shape |
| Lane parameter registration helpers | `plugins/gradus/src/parameters/` | Should reuse -- add a `sequencer_note_lane.{h,cpp}` alongside existing lane helpers |
| Per-step / per-lane processor in audio thread | `plugins/gradus/src/processor/` (existing arp processor that walks lanes) | Should extend with a Sequencer-mode branch at the input stage; lane processors downstream are unchanged |
| Held-note stack / "last-played note" tracking | `plugins/gradus/src/processor/` (existing live-input handling already tracks held notes) | Should reuse -- last-played-note is already a concept; add a read accessor for it from the Sequencer source path |
| State save/load (parameter persistence) | Gradus controller + processor `getState` / `setState` | Should reuse without modification -- new parameters get persisted automatically because they are normal VST3 parameters |
| Preset save/load infrastructure | `plugins/shared/src/preset/` and `plugins/gradus/src/preset/` | Should reuse -- new parameters travel with presets via the existing mechanism |
| Existing lane Length pattern | All existing Gradus lanes | Should reuse -- same min/max (1-32) and same per-step gating |
| Ring view (current arp visualization) | `plugins/gradus/src/ui/` (existing custom view) | Reference implementation -- the new piano-roll view should follow the same VSTGUI custom-view patterns (sub-controller, IDependent if needed, deferred UI updates from audio thread) |
| Custom-view sub-controller pattern | `plugins/gradus/src/controller/` + reusable views in `plugins/shared/src/ui/` | Should reuse -- the piano-roll view will be wired via the same sub-controller / DelegationController pattern used by existing custom views |
| UIViewSwitchContainer for Source = Live vs Sequencer visibility | VSTGUI's built-in `UIViewSwitchContainer` driven by a parameter | Should reuse -- the natural mechanism for "show piano-roll view iff Source = Sequencer" |
| Control disable via template-switch-control / VSTGUI value bindings | VSTGUI conventions | Should reuse -- disabling ArpMode/OctaveRange/OctaveMode/ScaleQuantizeInput in Sequencer mode is a UI-binding concern, not a new abstraction |
| Existing parameter ID block (3741+ start) | `plugins/gradus/src/plugin_ids.h` line 497 (`kArpMidiDelayPlayheadId = 3740`) | Must use -- confirmed end of dense block, next free ID is 3741 |
| Pitch lane additive semantics | `plugins/gradus/src/processor/` (existing pitch lane processor) | Should reuse -- Sequencer mode just feeds it different input pitches; the additive add is unchanged |

**Initial codebase search for key terms:**

```bash
# Existing lane param ID patterns and the current dense-block ceiling
grep -rn "kArpMidiDelayPlayhead\|kArpVelocityLane\|kArpPitchLane\|kMaxSteps" plugins/gradus/src/

# Existing held-note tracking in the audio thread
grep -rn "lastPlayed\|heldNote\|noteStack" plugins/gradus/src/processor/

# Existing piano-roll-like or step-grid custom views (likely none -- net new)
grep -rn "PianoRoll\|StepGrid\|noteGrid" plugins/

# Existing UIViewSwitchContainer usage for similar "mode toggle" UI
grep -rn "UIViewSwitchContainer\|template-switch-control" plugins/gradus/resources/
```

**Search Results Summary**:
- Param ID block for the new lane confirmed to start cleanly at `3741` (line 497 of `plugins/gradus/src/plugin_ids.h` ends the dense block at 3740, `kArpMidiDelayPlayheadId`).
- Existing Gradus lanes establish a clear structural template for the new Sequencer Note lane (Length + 32 step params + per-lane modulators + playhead).
- Held-note / last-played-note tracking is already present in the Gradus processor; the Sequencer source path can read from it rather than introducing parallel held-note state.
- No existing piano-roll-like view in the codebase -- this is net-new UI work, but it follows established VSTGUI custom-view conventions (per `vst-guide` skill).
- No existing "Source mode" toggle -- this is a new VST3 parameter at `kArpSourceModeId` (location: with the other top-level arp params, near `kArpModeId` at 3001).

### Forward Reusability Consideration

**Sibling features at same layer**:
- Future Ruinae alignment: Ruinae shares the arp parameter ID block 3000-3372 with Gradus. If Ruinae ever wants a sequencer source mode, the same Source parameter + Sequencer Note lane parameters could be lifted into the shared block. **However**, this spec is Gradus-scoped -- the Source mode and the new lane are Gradus-only parameters at IDs 3741+ (outside the shared 3000-3372 range), so they will NOT pollute Ruinae's parameter set. This is the right choice: Ruinae and Gradus diverging on Gradus's exclusive feature is fine.
- Future Iterum / Disrumpo step sequencers: If those plugins ever grow step-sequencer functionality, the piano-roll VSTGUI custom view is a candidate for promotion to `plugins/shared/src/ui/`. For v1 it should live in `plugins/gradus/src/ui/` and only be promoted if and when a second consumer appears (per project's "duplicate twice before extracting" guidance).

**Potential shared components** (preliminary, refined in plan.md):
- **Piano-roll VSTGUI custom view** -- initially Gradus-local; promote to shared when a second consumer needs it.
- **"Last-played note stack" abstraction** -- if a future Ruinae source-mode work needs identical semantics, extract to `plugins/shared/src/midi/`.
- **Generic "step-source-mode" toggle pattern** -- likely too feature-specific to share; keep Gradus-local.

## Implementation Verification *(mandatory at completion)*

### Compliance Status

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 (kArpSourceModeId param exists, Live/Sequencer, defaults Live) | 🔄 | To be filled at `/speckit.implement` completion |
| FR-002 (old presets load as Live) | 🔄 | To be filled at `/speckit.implement` completion |
| FR-013 (param IDs at 3741+) | 🔄 | To be filled at `/speckit.implement` completion |
| FR-014 (Live-mode behavior byte-identical to current) | 🔄 | To be filled at `/speckit.implement` completion |
| FR-016 (transposition by heldNote - 60) | 🔄 | To be filled at `/speckit.implement` completion |
| FR-017 (last-played-note wins) | 🔄 | To be filled at `/speckit.implement` completion |
| FR-019 (rests do not emit note-on, playhead advances) | 🔄 | To be filled at `/speckit.implement` completion |
| FR-020 (output-side lanes still apply) | 🔄 | To be filled at `/speckit.implement` completion |
| FR-021 (pitch lane additive in Sequencer mode) | 🔄 | To be filled at `/speckit.implement` completion |
| FR-021a (kArpTransposeId additive in Sequencer mode) | 🔄 | To be filled at `/speckit.implement` completion |
| FR-022 (ArpMode + Latch + Markov + Euclidean + PinNote + RangeMap + ScaleQuantizeInput ignored in Sequencer mode) | 🔄 | To be filled at `/speckit.implement` completion |
| FR-022a (Retrigger Note/Beat semantics preserved in Sequencer mode) | 🔄 | To be filled at `/speckit.implement` completion |
| FR-022b (Spice/Dice/Humanize/per-lane modulators preserved in Sequencer mode) | 🔄 | To be filled at `/speckit.implement` completion |
| FR-025 (no stuck notes on Source toggle, pending MIDI delay echoes tail out) | 🔄 | To be filled at `/speckit.implement` completion |
| FR-025a (base velocity = last-held velocity, fallback 100) | 🔄 | To be filled at `/speckit.implement` completion |
| FR-025b (polymetric Sequencer Note lane clocking) | 🔄 | To be filled at `/speckit.implement` completion |
| FR-027 (piano-roll visible iff Sequencer) | 🔄 | To be filled at `/speckit.implement` completion |
| FR-028 (fixed C2-B5 range) | 🔄 | To be filled at `/speckit.implement` completion |
| FR-034a (real-time playhead cursor driven by Playhead param) | 🔄 | To be filled at `/speckit.implement` completion |
| FR-033 (VSTGUI cross-platform) | 🔄 | To be filled at `/speckit.implement` completion |
| FR-037 (state persistence via existing machinery) | 🔄 | To be filled at `/speckit.implement` completion |
| FR-039a (kCurrentStateVersion bump to 3 + legacy v2 loader) | 🔄 | To be filled at `/speckit.implement` completion |
| FR-039b (v2→v3 migration unit test, byte-identical Live MIDI) | 🔄 | To be filled at `/speckit.implement` completion |
| FR-040 (passes pluginval strictness 5) | 🔄 | To be filled at `/speckit.implement` completion |
| SC-001 (sub-2-minute author-and-play workflow) | 🔄 | To be filled at `/speckit.implement` completion |
| SC-002 (Live vs Sequencer side-by-side post-processor MIDI matches) | 🔄 | To be filled at `/speckit.implement` completion |
| SC-003 (single-held-note transpose exact for 12 root notes) | 🔄 | To be filled at `/speckit.implement` completion |
| SC-004 (old Gradus presets bit-identical MIDI output) | 🔄 | To be filled at `/speckit.implement` completion |
| SC-004b (Ruinae factory presets bit-identical MIDI output post-`kNumLanes` extension) | 🔄 | To be filled at `/speckit.implement` completion |
| SC-005 (preset roundtrip 100%) | 🔄 | To be filled at `/speckit.implement` completion |
| SC-006 (pluginval strictness 5 zero new issues) | 🔄 | To be filled at `/speckit.implement` completion |
| SC-007 (100 Source toggles, zero stuck notes) | 🔄 | To be filled at `/speckit.implement` completion |
| SC-008 (32 rests = zero note-ons) | 🔄 | To be filled at `/speckit.implement` completion |
| SC-009 (cross-platform CI build success) | 🔄 | To be filled at `/speckit.implement` completion |
| SC-010 (last-played root within one audio block) | 🔄 | To be filled at `/speckit.implement` completion |

**Status Key:**
- ✅ MET: Requirement verified against actual code and test output with specific evidence
- ❌ NOT MET: Requirement not satisfied (spec is NOT complete)
- ⚠️ PARTIAL: Partially met with documented gap and specific evidence of what IS met
- 🔄 DEFERRED: Explicitly moved to future work with user approval (or, here: not yet implemented)

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

**Overall Status**: NOT STARTED -- specification phase only.

**If NOT COMPLETE, document gaps**: N/A at spec time.

**Recommendation**: Proceed to `/speckit.clarify` (if any clarifications surface from the checklist) or directly to `/speckit.plan`.
