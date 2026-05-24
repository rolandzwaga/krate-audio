# Phase 0 Research: Gradus Piano-Roll Step Sequencer

**Feature**: 142-gradus-piano-roll-sequencer
**Date**: 2026-05-23
**Status**: Complete — all NEEDS CLARIFICATION resolved (spec had 16 prior clarification answers; no open questions remain). **Grilling-pass pivot applied 2026-05-23:** Decisions 1, 2, 3, 8, 9, 10 have been revised after a `/grill-me` review surfaced duplication risk in the original external-clock architecture. The Sequencer Note lane is now **lane 10 inside `ArpeggiatorCore`** (conditionally inert in Live mode). See each Decision section's "Grilling-Pass Revision" notes.

## Research Tasks Completed

All design questions for this feature were resolved during two `/speckit.clarify` sessions
(2026-05-23, captured in `spec.md` Clarifications section). The remaining research
focuses on **how** to implement those decisions cleanly inside the existing Gradus
architecture — not **what** to do.

---

## Decision 1: Sequencer Note Lane — Internal vs External to `ArpeggiatorCore`

**Grilling-Pass Revision (2026-05-23):** Original decision REVERSED. We now extend `ArpeggiatorCore::kNumLanes` from 9 to 10.

**Decision (final)**: Extend `ArpeggiatorCore::kNumLanes` from 9 to 10. Lane 10 = Sequencer Note. Stores pitch in `seqNoteLane_` (`ArpLane<uint8_t>`) plus a parallel `seqRestFlags_[32]` atomic array read at firing time (mirrors the MIDI delay pattern, which stores 6 per-step parameters in separate atomic arrays alongside its `midiDelayLane_`). Lane 10 is **conditionally inert in Live mode** — `fireStep` short-circuits its advance + modulator reads when `sourceMode_ == Live`. Default `sourceMode_ = Live`, so Ruinae (which never sets it to Sequencer) is byte-identical to today.

**Rationale (revised)**:
- The original external-clock approach would duplicate the core's step-clock state (sample counter, swing accumulator, jitter state), retrigger handling (Note/Beat playhead reset), and speed-curve table lookup. Two parallel implementations of polymetric clocking risk drift between the Sequencer Note lane and the modulator lanes — a hard-to-detect, hard-to-test class of bug.
- The original "trivially-provable SC-004" guarantee was illusory: the inert-branch approach is also trivially provable (test that `setSourceMode(Live)` produces byte-identical MIDI to pre-feature code) and provides the same correctness guarantee with far less code.
- Ruinae safety is preserved by the conditional-inert branch. SC-004b (new) verifies this with a full Ruinae factory preset corpus regression.
- All existing fireStep infrastructure (polymetric advance, retrigger, swing/jitter, speed curves) is reused at the cost of ~30 call sites that already iterate up to `kNumLanes` (which now is 10 instead of 9). Most of these are simple `for (i = 0; i < kNumLanes; ++i)` loops that need no per-lane logic update.

**Alternatives Considered (revised)**:
1. **External clock in Processor (original plan)**: REJECTED post-grill — duplicates clocking + retrigger logic; correctness depends on keeping two implementations in sync forever; drift risk.
2. **Compile-time template `kNumLanes` differing per plugin**: Rejected — invasive (touches every call site), and the conditional-inert branch is cheaper.
3. **Runtime constructor flag**: Rejected — adds variable-cost branches without meaningful benefit over the inert-branch.

**Implementation Note (revised)**:
- `ArpeggiatorCore`: `kNumLanes` becomes 10. Add `seqNoteLane_` member, parallel `seqRestFlags_[32]` atomic array, `sourceMode_` member, `setSourceMode(SourceMode)` API.
- `fireStep` gets two new branches:
  - **Inert branch**: `if (sourceMode_ == Live) { /* skip lane 10 advance and modulator reads */ }`. This is the only path Ruinae ever takes.
  - **Source-pitch branch**: at the point where the held-note pool is consulted for the current emission pitch, `if (sourceMode_ == Sequencer) { use seqNoteLane_.currentValue() + seqRestFlags_[seqNoteLane_.currentStep()] }` else use existing ArpMode/Octave traversal.
- `SequencerNoteSource` (the Layer 2 plugin-local component planned originally) is **DROPPED** — the lane lives natively in the core.

---

## Decision 2: Transposition Root Tracking — Reuse `HeldNoteBuffer` vs New State

**Grilling-Pass Revision (2026-05-23):** Decision simplified — no parallel held-note copy. Held notes populate `HeldNoteBuffer` in both modes (single source of truth).

**Decision (final)**: Held MIDI notes populate `ArpeggiatorCore::heldNotes_` (the existing `HeldNoteBuffer`) in both Live and Sequencer modes. No parallel copy is maintained. The mode-gated divergence happens **inside `fireStep`** only: in Live mode, ArpMode/Octave traversal consumes the buffer to pick the next emission pitch. In Sequencer mode, ArpMode/Octave traversal is skipped — but `HeldNoteBuffer::byInsertOrder().back()` still provides:
- The transposition root (`back().note - 60` semitones offset).
- The base velocity (`back().velocity`; fallback `100` when buffer empty).
- The voicing context that the Chord lane consults (unchanged from Live).

**Rationale (revised)**:
- A parallel `heldKeys_` copy was rejected because it duplicates state and creates two sources of truth that can drift (e.g., chord lane reads the core's buffer; transposition reads the parallel buffer; a forgot-to-mirror bug becomes possible).
- With ArpMode/Octave traversal skipped in Seq mode (Decision 1 revised), the core's buffer does NOT double-count the held notes as a parallel arpeggio source — there's no traversal to run.
- This keeps the input-stage code identical in both modes: the existing `processor.cpp:97-112` MIDI input handling stays as-is.

**Implementation Detail (revised)**:
- The Processor's MIDI input handling is **unchanged**: it always calls `arpCore_.noteOn/noteOff()`.
- Source-mode toggle handling: on any `setSourceMode()` call where `oldMode != newMode`, the Processor invokes `arpCore_.panicNoteOff(currentSoundingNote)` for the active programmed note (if any), then calls `arpCore_.setSourceMode(newMode)`. Lane playheads are **NOT** reset (Q5-A).
- Notes held when the toggle happens stay in the buffer and continue to serve their role in both modes (root + velocity + chord voicing in Seq mode; arpeggio source in Live mode). This matches the spec's "Held note + Sequencer mode toggled to Live" edge case more faithfully than the prior parallel-buffer approach.

**Alternatives Considered (revised)**:
1. **Parallel `heldKeys_` array in Processor** (original): REJECTED — two-state-source drift risk; chord lane consistency loss in Seq mode.
2. **Add a `lastPlayedNote()` accessor to ArpeggiatorCore**: Not needed; `HeldNoteBuffer::byInsertOrder().back()` already provides this.
3. **`TransposeRootStack` Layer 1 component**: Not needed; same reason.

---

## Decision 3: State Migration v2 → v3 Implementation Strategy

**Grilling-Pass Revision (2026-05-23):** Test fixture strategy made concrete.

**Decision (final)**: Single-source-of-truth approach. `getState` always writes v3 format. `setState` reads the version field, then dispatches:
- `version == 2`: call existing `loadArpParams()` (which is EOF-safe for the v2 parameter block); the new Sequencer params remain at struct defaults (Source=Live, all rests=1, all pitches=60, length=16, modulators=neutral).
- `version >= 3`: call existing `loadArpParams()` THEN call new `loadSequencerNoteLaneParams()` (which reads the appended v3 block).

**Test Fixture Strategy (FR-039b — added in grilling pass)**:
- Generate 3-5 representative v2 state binary fixtures on the parent commit (before bumping `kCurrentStateVersion`). Cover: default state, all-modulator-lanes-active state, MIDI-delay-active state.
- Commit them as `plugins/gradus/tests/fixtures/gradus_v2_preset_{default,heavy_lanes,midi_delay}.bin`.
- For each fixture, run the parent-commit Gradus binary against a fixed 60-second MIDI input and capture the MIDI output to `plugins/gradus/tests/fixtures/gradus_v2_golden_midi_{default,heavy_lanes,midi_delay}.txt`.
- The migration test (`state_v2_v3_migration_test.cpp`) loads each `.bin` via the v3 `setState`, runs the same 60-second input, and asserts the emitted MIDI matches the golden file byte-for-byte.

**Rationale**:
- The current Gradus state stream is already EOF-safe for the v2 lane additions that happened during Gradus development (see `loadArpParams` lines 2082-2173 for the established pattern: `if (!streamer.readInt32(intVal)) return true; // EOF means defaults`). We piggyback on the same pattern.
- Bumping the version field (currently `2`, becomes `3`) is purely a **defensive marker**: it signals to future code that this stream contains the Sequencer block, and lets us reject malformed forward-incompatible streams cleanly. The EOF-safe reads already handle the "v2 stream loaded by v3 code" case — the version bump just makes the intent explicit per the spec's FR-039a requirement.
- The fixture approach is deterministic (no host process needed at test time) and exercises real v2 binary streams (not synthesized).

**Rejected**: A formal `LegacyV2Loader` class. The complexity isn't justified — the existing EOF-safe sequential read already implements legacy compatibility for free.

---

## Decision 4: UI — UIViewSwitchContainer vs IDependent Visibility

**Decision**: Use **`UIViewSwitchContainer` with `template-switch-control="ArpSourceMode"`**
for the **piano-roll view's** visibility (FR-027). Use the existing **IDependent
+ `viewDirtyFlags_` deferred update** pattern for the **grey-out / disable** of the
FR-022 / FR-036 control set.

**Rationale**:
- `UIViewSwitchContainer` is the proven way to swap entire view subtrees based on
  a parameter (FR-027 explicitly says "visible if and only if Source = Sequencer";
  the piano roll appears/disappears as a whole). Iterum uses this pattern (line 635
  of `iterum/resources/editor.uidesc`) successfully across 10+ delay modes.
- Greying out / disabling individual controls (FR-036) is a per-control attribute
  toggle, not a view swap. The existing Gradus `viewDirtyFlags_` +
  `syncViewsFromParams()` infrastructure is the right tool. Each disabled control
  gets its `setMouseEnabled(false)` + `setAlphaValue(0.4)` toggled when the
  source-mode dirty flag fires. (Existing pattern: see how Markov controls grey out
  when ArpMode != Markov in `controller_view_sync.cpp`.)
- VSTGUI controls bound to disabled params still pass `setParamNormalized` through
  to the host — the param keeps its value, the audio thread just ignores it
  (FR-022). The UI grey-out is purely a UX-honesty signal.

**Alternatives Considered**:
1. **Use `UIViewSwitchContainer` for the entire main work area**, with two templates:
   `LiveModeMain` and `SequencerModeMain`, each containing a copy of the existing
   ring view + lane controls. **Rejected**: Duplicates the lane controls between
   the two templates; lanes apply in both modes (FR-020), so the lane controls
   should NOT be inside the swap. Maintenance nightmare.
2. **Use a manual `setVisible(true/false)` on the piano roll view from the
   controller's `syncViewsFromParams()`**: Rejected — the UIViewSwitchContainer
   approach is more idiomatic, automatically handles template-init binding for
   the piano roll's child params, and is exactly what VSTGUI is designed for.

**UI Layout Decision (grilling-pass confirmed Q7-A)**:
- The piano roll lives in a **dedicated swappable slot** in `editor.uidesc`. The slot is wrapped by a `UIViewSwitchContainer` with `template-switch-control="ArpSourceMode"`, swapping between two templates:
  - **LiveModePlaceholder**: empty / blank (or a small "Source: Live — switch to Sequencer to author patterns" hint label).
  - **SequencerModeContent**: contains the `PianoRollView` instance.
- **Editor window size is unchanged.** The slot occupies a fixed region in the layout (sized for ~600x200 px piano roll content).
- The lane controls, ring view, and detail strip remain visible in BOTH modes, because lanes still process the Sequencer-emitted notes (FR-020). They are NOT inside the swappable region.
- The Sequencer Note lane gets a tab in the existing `LaneTabBar` (alongside Velocity / Gate / Pitch / Modifier / Ratchet / Condition / Chord / Inversion / MIDI Delay). When the Sequencer Note tab is selected, the detail strip shows the lane's Speed / Swing / Jitter / SpeedCurveDepth knobs (mirroring the other lanes' detail strips).
- Rejected layouts (per Q7): tab toggle (hides lanes from Live users), editor resize (complex, runtime resize edge cases), overlay (obscures lanes that still apply audio-side).

---

## Decision 5: Piano-Roll View Architecture (Painting vs Sub-Controls)

**Decision**: Implement `PianoRollView` as a **single CView subclass that draws the
entire grid in `draw()`** and handles all mouse events via `onMouseDown` /
`onMouseMoved` / `onMouseUp` overrides. Do NOT use 32×48 = 1536 individual `CControl`
sub-views (e.g., one button per cell).

**Rationale**:
- 1536 sub-controls would blow up the VSTGUI control tree and slow down hit-testing.
- Custom-draw is straightforward: the grid is a 32 × 48 lattice (32 step columns,
  48 pitch rows for C2-B5). Drawing is O(32 × 48) = ~1500 simple rect operations
  per `draw()` call, well within VSTGUI's redraw budget.
- The mouse-down / drag / right-click handling is more concise as math (hit-test
  cell from coordinates, modify the corresponding param) than as 1536 listener
  callbacks.
- This mirrors how `MidiDelayLaneEditor`'s playhead overlay is implemented (custom
  draw of a column highlight).

**Subordinate concerns**:
- **Real-time playhead cursor (FR-034a)**: A separate column-highlight is drawn
  on top of the grid, driven by the `kArpSequencerNoteLanePlayheadId` parameter's
  value (received via `IDependent::update`). Same pattern as `PlayheadOverlayView`
  in `midi_delay_lane_editor.h`.
- **External param changes (FR-034)**: `PianoRollView` registers as an `IDependent`
  on the 64 step params (32 pitches + 32 rest flags) + length param + playhead param.
  On `update()`, calls `invalid()` to trigger a redraw.
- **Click → set param**: Uses the same `editParamWithNotify` path the rest of
  Gradus's UI uses (consults `Controller::editParamWithNotify(paramId, value)`),
  so the change is reflected in host automation lanes / preset state immediately.

**Geometry**:
- Default view size: ~600px wide × ~400px tall.
- Column width = `viewWidth / activeSteps` where `activeSteps = lane.length`.
- Row height = `viewHeight / 48` (4 octaves × 12 semitones).
- Hit-test: `step = floor((x - originX) / colW)`, `pitchRow = 83 - floor((y - originY) / rowH)`
  → MIDI 36-83 mapped to rows 47 (top) to 0 (bottom), so MIDI 83 = top row.

---

## Decision 6: Right-Click as Rest Gesture — Cross-Platform Equivalents

**Decision**: Use VSTGUI's `CButtonState & kRButton` check in `onMouseDown`.
This is automatically `true` for:
- Windows: right mouse button click.
- macOS: right-click OR Ctrl+Click (VSTGUI normalizes this).
- Linux: right mouse button click (per VSTGUI's GTK backend).

**No special-casing needed.** The same single check covers all three platforms.

**Reference**: `plugins/gradus/src/ui/midi_delay_lane_editor.h:435-479` already
uses this pattern.

---

## Decision 7: Drag Semantics — Lock-to-Start-Pitch Implementation

**Decision** (resolved in clarifications): On `onMouseDown`, capture the start row's
MIDI pitch into a member `dragPitch_`. On `onMouseMoved` while `dragging_ == true`:
- Compute the **step column** from current X position (clamped to [0, activeSteps-1]).
- Always paint the captured `dragPitch_` at that step (set pitch param, clear rest
  flag). **Ignore vertical mouse position** for the duration of the drag.
- Track `lastPaintedStep_` to avoid redundant param writes when the mouse moves
  within the same column.

**`onMouseUp`**: Reset `dragging_ = false`. If the drag never moved past the initial
step (i.e., `lastPaintedStep_` was never updated), treat it as a **single click**
and apply toggle/replace semantics per FR-030. If it did move, no further action
(painting already happened during `onMouseMoved`).

**Right-click during drag**: Per FR-031, drag is paint-only, never rest-toggle.
But right-click (`kRButton`) at any moment sets that step's rest flag = 1. We do
not enter drag mode on `kRButton`-only down events.

---

## Decision 8: Sequencer Polymetric Clock — Implementation

**Grilling-Pass Revision (2026-05-23):** This Decision is **substantially simplified**. The Sequencer Note lane no longer needs an external clock — it lives natively inside `ArpeggiatorCore` and uses the same polymetric clocking the existing 9 lanes use. No formulas to re-implement.

**Decision (final)**: Lane 10 (Sequencer Note) advances inside `ArpeggiatorCore::fireStep` via the existing `advanceLaneBySpeed(seqNoteLane_, 9)` call alongside the other 8 polymetric lane advances. Speed/Swing/Jitter/SpeedCurveDepth all reuse the established per-lane infrastructure:
- `setLaneSpeed(9, ...)`, `setLaneSwing(9, ...)`, `setLaneLengthJitter(9, ...)`, `setLaneSpeedCurveTable(9, ...) / setLaneSpeedCurveDepth(9, ...) / setLaneSpeedCurveEnabled(9, ...)` are extended call sites that already exist for lanes 0-8.
- Lane 10's playhead is reported via `seqNoteLane_.currentStep()`, exposed via `kArpSequencerNoteLanePlayheadId` like every other lane.

**Conditional inert in Live mode**: `fireStep` wraps the lane 10 advance in `if (sourceMode_ == Sequencer) advanceLaneBySpeed(seqNoteLane_, 9);`. In Live mode the lane stays at its last position (typically step 0) and consumes zero cycles.

**Source pitch resolution in `fireStep` (Sequencer mode only)**:
1. Read `currentStep = seqNoteLane_.currentStep()`.
2. Read `pitch = seqNoteLane_.currentValue()` (the stored uint8_t at currentStep) and `isRest = seqRestFlags_[currentStep].load(std::memory_order_relaxed)`.
3. If `isRest`: skip emission (no `noteOn`), but the lane has already been advanced (step 5 happens before this check OR after — see exact placement in implementation).
4. Else:
   - Compute `transposedPitch = pitch + (heldNoteRoot - 60) + globalTransposeParam + pitchLaneOffset`, clamped to [0, 127] per FR-024 (same policy as existing pitch lane).
   - `heldNoteRoot = heldNotes_.empty() ? 60 : heldNotes_.byInsertOrder().back().note`.
   - `baseVelocity = heldNotes_.empty() ? 100 : heldNotes_.byInsertOrder().back().velocity`.
   - Use `transposedPitch` + `baseVelocity` as the emission. All subsequent fireStep machinery (modulator lanes, chord lane voicing, MIDI delay scheduling) runs exactly as today on this single source note.
5. The ArpMode/Octave traversal that picks `currentNote` from `heldNotes_` in Live mode is replaced by the above lookup in Sequencer mode (early-branch).

**Retrigger (FR-022a)**: `setRetrigger(Note)` already resets all lane playheads on note-on. The retrigger code iterates lanes via index; extending the upper bound from `kNumLanes==9` to `kNumLanes==10` automatically includes lane 10. Same for `setRetrigger(Beat)` and its bar-boundary handling.

**Tested via**:
- `arpeggiator_core_sequencer_test.cpp` — core-level tests of lane 10's Live-mode inert behavior and Sequencer-mode pitch resolution.
- `sequencer_polymetric_test.cpp` — lane 10 length≠other lane lengths produces verifiable polyrhythm.
- `source_mode_transpose_test.cpp` — SC-003 (exact `heldNote - 60` offset across 12 root notes).
- `sequencer_rests_advance_test.cpp` — SC-008 (rest steps consume time, emit no notes).

---

## Decision 9: Backward Compatibility — Existing Lanes' Behavior In Sequencer Mode

**Grilling-Pass Revision (2026-05-23):** Same outcome (lanes 0-8 unchanged), but the mechanism shifts from "feed Sequencer-emitted notes into the core via noteOn" to "fireStep substitutes the source pitch internally."

**Decision (final)**: Lanes 0-8 (Velocity, Gate, Pitch, Modifier, Ratchet, Condition, Chord, Inversion, MIDI Delay) are functionally unchanged in Sequencer mode. The only diff is what `fireStep` reads as the "current source pitch":
- **Live mode**: ArpMode/Octave traversal picks current note from `heldNotes_`.
- **Sequencer mode**: `seqNoteLane_.currentValue()` provides the pitch (subject to rest-flag check and transposition formula in Decision 8).

Both modes feed the same downstream pipeline:
- **Velocity lane**: scales `baseVelocity` per-step (lane's own playhead).
- **Gate lane**: scales note duration via the existing gate-length pipeline.
- **Pitch lane**: applies additive offset (per FR-021). Already part of the transposition formula in Decision 8.
- **Modifier lane**: applies kStepActive / Tie / Slide / Accent flags.
- **Ratchet lane**: subdivides note-on events.
- **Condition lane**: probability gating + fill mode.
- **Chord lane**: builds chord on top of the single Sequencer note → emits multi-note `arpEvents_`.
- **Inversion lane**: re-voices the chord.
- **MIDI Delay lane**: feeds `midiDelay_` post-processor as it does for live notes.

This is structurally the same as the original plan, just with the source-pitch substitution happening inside `fireStep` rather than at the Processor's input stage. No `arpCore_.noteOn` indirection from outside; no Processor-level note-on/noteOff queue. Cleaner.

---

## Decision 10: Pending MIDI Delay Echoes Across Source Toggles

**Decision** (resolved in clarifications, FR-025): Pending echoes are scheduled
inside `midiDelay_` (an instance of `Krate::DSP::MidiNoteDelay`). The echo
scheduling is pitch-based; it doesn't care whether the original triggering note-on
came from live input or the sequencer.

When `sourceMode_` toggles:
- We emit clean note-offs for the previous source's currently-sounding programmed
  note (if any), but we do NOT call `midiDelay_.reset()`. The pending echoes
  scheduled before the toggle continue to fire as scheduled.
- New incoming notes (from the new source) start new echo chains as normal.

**No code change to `midiDelay_` required.** This behavior is automatic given how
`midiDelay_` is decoupled from source identity.

---

## Best Practices: VSTGUI Custom Views

Per the `vst-guide` skill and the existing `MidiDelayLaneEditor` reference:

1. **Real-time-safe drawing**: `draw()` runs on UI thread; allocations OK but
   minimize. Cache cell-rect calculations if profiling shows draw-time bottlenecks.
2. **IDependent lifecycle**: Register in constructor / `attached()`, unregister in
   `removed()` / destructor. Failure to unregister → use-after-free when the
   editor closes.
3. **Mouse event return values**: `kMouseEventHandled` for events we consumed;
   `kMouseEventNotHandled` to let the container handle them.
4. **`setMouseEnabled(false)`** disables a view (no mouse events delivered). Use
   this to make the piano roll non-interactive when Source = Live (defense-in-depth;
   the `UIViewSwitchContainer` already hides it, but extra safety is cheap).

---

## Best Practices: VST3 State Versioning

Per VST3 SDK docs and the project's own history (already proven 8+ times in
`loadArpParams` EOF-safe sections):

1. **Always write version FIRST** in `getState()` (already done at processor.cpp:287).
2. **Read version FIRST** in `setState()` (already done at processor.cpp:303), then
   dispatch to loader functions per version.
3. **Append-only** stream format: new params go at the end. Never reorder existing
   fields.
4. **EOF-safe reads** for legacy compat: `if (!streamer.readInt32(intVal)) return true; // EOF means defaults`.
5. **Test the migration**: a unit test that captures the byte-stream of a v2
   `getState()` (from a pinned old-Gradus binary, or from a reference byte array
   in the test) and feeds it to v3 `setState()`, verifying MIDI output is
   byte-identical to the old Gradus over a deterministic 60-second test sequence
   (per SC-004, FR-039b).

---

## Best Practices: pluginval Compliance

Per FR-040 / SC-006 and project history with this tool:

1. **All new params must declare**: kCanAutomate flag (unless explicitly hidden
   like Playhead), reasonable default value, valid title (≤ 128 char UTF-16),
   `unitId = kRootUnitId`.
2. **Hidden params (Playhead)**: use `kIsHidden | kCanAutomate` (per existing
   playhead pattern in `arpeggiator_params.h`).
3. **Read-only output params** (Playhead): host still expects setParamNormalized
   to be respondable; don't reject writes — accept them but treat as advisory
   (the audio thread overwrites every block).
4. **Stress test**: pluginval at strictness 5 exercises rapid param changes,
   automation, state save/load, host transport edge cases. Run this in CI after
   any param-related change.

---

## Open Questions

**None.** All design decisions are committed.

## References

- **Spec**: `specs/142-gradus-piano-roll-sequencer/spec.md`
- **Constitution**: `.specify/memory/constitution.md`
- **CLAUDE.md**: project root
- **vst-guide skill**: `.claude/skills/vst-guide/`
- **testing-guide skill**: `.claude/skills/testing-guide/`
- **dsp-architecture skill**: `.claude/skills/dsp-architecture/`
- **Existing patterns**:
  - State versioning: `plugins/gradus/src/parameters/arpeggiator_params.h:2082-2173` (EOF-safe pattern)
  - UIViewSwitchContainer: `plugins/iterum/resources/editor.uidesc:635`
  - IDependent visibility: `plugins/iterum/src/controller/visibility_controller.h`
  - Custom view with playhead overlay: `plugins/gradus/src/ui/midi_delay_lane_editor.h`
  - Lane primitive: `dsp/include/krate/dsp/primitives/arp_lane.h`
  - Held-note buffer: `dsp/include/krate/dsp/primitives/held_note_buffer.h`
