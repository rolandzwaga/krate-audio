# Quickstart: Gradus Piano-Roll Step Sequencer

**Feature**: 142-gradus-piano-roll-sequencer
**Audience**: Developers implementing this feature; QA reviewers verifying it.

This document provides the shortest path to a working prototype, plus the
verification steps for each user story.

---

## Prerequisites

- Gradus build environment configured (see `CLAUDE.md`).
- Branch: `142-gradus-piano-roll-sequencer` (checked out).
- VST3 host for testing (e.g., REAPER, FL Studio, Live).

---

## Implementation Order (Recommended)

Per Constitution Principle XII (Test-First Development), every step lands its
**failing test first**, then implementation. The order below is the suggested
sequence for `/speckit.tasks` to lay out, optimized for incremental,
independently-testable phases.

### Phase A: Parameter Plumbing (no UI yet)
1. Add IDs to `plugin_ids.h` (FR-013).
2. Add atomics to `ArpeggiatorParams` (extending existing struct).
3. Implement `handleArpParamChange` cases for new IDs.
4. Implement `saveSequencerNoteLaneParams` / `loadSequencerNoteLaneParams`.
5. Implement `registerSequencerNoteLaneParams` (extending `registerArpParams`).
6. Bump `kCurrentStateVersion` to 3 in `plugin_ids.h`.
7. Extend `Processor::setState` to dispatch on version (legacy v2 path + v3 path).
8. **Generate v2 migration fixtures**: on the parent commit (before the version bump), capture binary state from each representative Gradus preset and save to `plugins/gradus/tests/fixtures/gradus_v2_preset_{default,heavy_lanes,midi_delay}.bin`. Run each through the parent-commit Gradus on a fixed 60-second MIDI input and capture MIDI output to `gradus_v2_golden_midi_{...}.txt`. Commit all fixtures.
9. **Tests**: `state_v2_v3_migration_test.cpp` (FR-039a, FR-039b) — load each fixture via v3 `setState`, run same input, assert byte-identical MIDI vs golden file.
10. **Build + pluginval check** to ensure no new warnings (FR-040).

### Phase B: ArpeggiatorCore Lane 10 Extension (grilling-pass pivot — no UI yet)

**Pivot**: We DO extend `ArpeggiatorCore::kNumLanes` from 9 to 10. Lane 10 = Sequencer Note. Lane 10 is conditionally inert when `sourceMode_ == Live`.

1. Add `SourceMode` enum (Live/Sequencer) to `arpeggiator_core.h` alongside `ArpRetriggerMode`.
2. In `ArpeggiatorCore`: bump `kNumLanes` 9→10; add `seqNoteLane_` (`ArpLane<uint8_t>`), `seqRestFlags_[32]` atomic array, `sourceMode_` (default Live), `setSourceMode(SourceMode)` API.
3. Extend per-lane modulator setters to accept lane index 9: `setLaneSpeed(9, ...)`, `setLaneSwing(9, ...)`, `setLaneLengthJitter(9, ...)`, `setLaneSpeedCurveTable(9, ...) / setLaneSpeedCurveDepth(9, ...) / setLaneSpeedCurveEnabled(9, ...)`.
4. In `fireStep`:
   - **Conditional-inert branch**: `if (sourceMode_ == Live) skip lane 10 advance and modulator reads`.
   - **Source-pitch branch (Seq mode)**: read `currentStep = seqNoteLane_.currentStep()`, `pitch = seqNoteLane_.currentValue()`, `isRest = seqRestFlags_[currentStep]`; if rest, skip emission; else compute `transposedPitch = pitch + (heldRoot - 60) + transposeParam + pitchLaneOffset` (clamped) and `baseVelocity = heldNotes_.empty() ? 100 : heldNotes_.byInsertOrder().back().velocity`; emit through the existing modulator-lane pipeline.
   - **Retrigger extension**: existing retrigger code that resets lane playheads via index iteration automatically picks up lane 10 (loop upper bound now `kNumLanes == 10`).
5. In `Processor`:
   - Add `std::atomic<int> sourceMode_` mirror (synced from `arpParams_.sourceMode`).
   - On `sourceMode_` change: emit note-off for any currently-sounding programmed note via `arpCore_`'s panic path; call `arpCore_.setSourceMode(newMode)`. Lane playheads are **NOT** reset (Q5-A).
   - Held-note routing **unchanged** in both modes — all MIDI note-on/off go to `arpCore_.noteOn/noteOff()` exactly as today. No parallel `heldKeys_` buffer.
6. Sync new params into `arpCore_` at the existing `applyParams` sync point (mirrors how other lane params are pushed into the core).
7. **Tests**:
   - `arpeggiator_core_sequencer_test.cpp` — core-level: lane 10 inert in Live; pitch resolution in Seq; rest-skip; retrigger reset.
   - `source_mode_transpose_test.cpp` — FR-016/017/018, SC-003 (12 root notes).
   - `source_mode_toggle_test.cpp` — FR-025, SC-007 (100 toggles, no stuck notes).
   - `sequencer_polymetric_test.cpp` — FR-025b (lane 10 polyrhythm vs other lanes).
   - `sequencer_rests_advance_test.cpp` — FR-019, SC-008.
   - `live_mode_byte_identical_test.cpp` — SC-004 (Gradus v2 preset corpus, byte-identical MIDI).
   - `ruinae_byte_identical_post_lane10_test.cpp` (in `plugins/ruinae/tests/unit/`) — **SC-004b new**: Ruinae factory preset corpus byte-identical post-`kNumLanes` bump.

### Phase C: Piano-Roll View
1. Implement `PianoRollView` class (per contract in `contracts/piano-roll-view.md`).
2. Wire into Controller's `createCustomView` / `verifyView`.
3. **Tests**:
   - `piano_roll_view_test.cpp` — 13 scenarios per contract.
   - `piano_roll_playhead_test.cpp` — FR-034a.

### Phase D: UIDesc Integration (Visibility + Disabled Controls)
1. Add `<control-tag name="ArpSourceMode" tag="3741"/>` etc. to `editor.uidesc`.
2. Add `UIViewSwitchContainer` for piano roll visibility:
   ```xml
   <view class="UIViewSwitchContainer"
         origin="X, Y" size="W, H"
         background-color="panel"
         transparent="false"
         template-names="PianoRollContent,EmptyContent"
         template-switch-control="ArpSourceMode"/>
   ```
   Note: `template-switch-control` value 0 → first template, 1 → second template.
   So we map `Live (0)` → `EmptyContent`, `Sequencer (1)` → `PianoRollContent`.
3. Add Sequencer Note lane modulator knobs to the detail strip (visible when
   the Sequencer Note tab is selected in LaneTabBar).
4. Extend Controller's `syncViewsFromParams` to grey out FR-022 / FR-036
   control set when Source = Sequencer.
5. Add a "Sequencer Note" tab to LaneTabBar.
6. **Tests**:
   - `piano_roll_visibility_test.cpp` — FR-027.

### Phase E: Final Validation
1. Run full `gradus_tests` suite.
2. Run `pluginval --strictness-level 5 --validate` on built Gradus.vst3.
3. Run `tools/run-clang-tidy.ps1 -Target gradus -BuildDir build/windows-ninja`.
4. Manually test the spec's 6 user stories (smoke test).
5. Fill compliance table in `spec.md` with concrete evidence.

---

## User Story Verification (Smoke Tests)

### User Story 1 — Program melody, route through lanes (P1)

1. Open Gradus in a DAW.
2. Click the new "Source" toggle → "Sequencer".
3. Observe piano roll appears.
4. Click cells at (col 0, row C4), (col 1, row E4), (col 2, row G4).
5. Set length param to 4 (or via UI control).
6. Press host transport play.
7. **Expected**: MIDI output emits C4 → E4 → G4 → silence at the configured
   tempo. Existing lane processors (Velocity, Gate, etc.) apply.

### User Story 2 — Held-note transpose (P1)

1. Continue from US1.
2. Hold note 60 (C4) on a MIDI controller while transport plays.
3. **Expected**: Output remains C4 → E4 → G4 (no offset; reference pitch).
4. Release and hold note 62 (D4).
5. **Expected**: Output becomes D4 → F#4 → A4 (+2 semitones).
6. While holding 62, also press 65 (F4).
7. **Expected**: Output becomes F4 → A4 → C5 (+5 semitones; last-played wins).
8. Release 65 (still holding 62).
9. **Expected**: Output reverts to D4 → F#4 → A4.

### User Story 3 — Visual editor (P1)

1. With Source = Sequencer, observe the piano roll's 48 rows (C2 to B5 visible).
2. Right-click step 3.
3. **Expected**: Step 3 renders as a rest (no fill, possibly small dash icon).
4. Drag from col 5 (row C4) to col 10.
5. **Expected**: Cols 5-10 are all painted with C4 (lock-to-start-pitch).

### User Story 4 — Persistence (P1)

1. Save the project / save a Gradus preset.
2. Quit host. Restart host. Reload project / preset.
3. **Expected**: Pattern restored exactly (32 step pitches, 32 rest flags,
   length, modulators, Source = Sequencer).
4. Test backward compat: load a pre-feature Gradus preset (`.vstpreset` from
   Gradus 1.7.x).
5. **Expected**: Source defaults to Live; behavior identical to pre-feature.

### User Story 5 — Disabled lane controls (P2)

1. With Source = Sequencer, observe:
   - ArpMode dropdown: greyed out
   - OctaveRange, OctaveMode, ScaleQuantizeInput: greyed out
   - All Markov / Euclidean / Pin Note / Range Mapping controls: greyed out
2. Toggle Source back to Live.
3. **Expected**: All those controls re-enable and resume normal behavior.

### User Story 6 — Output scale quantize (P3)

1. With Source = Sequencer, set output scale to C minor.
2. Pattern `[60, 64, 67]` plays.
3. **Expected**: Note 64 (E natural) is quantized to E♭ (63). Note 67 stays
   (G is in C minor).

---

## Performance Sanity Check

After implementation, run a CPU profiler against Gradus with:
- Source = Sequencer
- Length = 32 (max)
- Tempo = 200 BPM, note value = 1/32 (highest step density)
- Held note in controller (forcing transposition every step)
- All 9 lanes maxed out (worst-case lane processing)

**Expected**: < 5% single-core CPU @ 44.1 kHz stereo (Constitution Principle XI).
The Sequencer source's added overhead should be tens of microseconds per block
(step-boundary work), well within budget.

---

## Common Pitfalls

1. **Forgetting EOF-safety on first new field**: When loading a v2 preset, the
   `readInt32` for `sourceMode` MUST be allowed to fail without returning
   `false`. Otherwise old presets silently fail to load.

2. **Calling `arpCore_.noteOn` for held MIDI in Sequencer mode**: This would
   cause double note generation. The Sequencer source is the EXCLUSIVE feed
   to `arpCore_` in Sequencer mode; held MIDI updates `heldKeys_` only.

3. **Caching `PianoRollView*` across `UIViewSwitchContainer` swaps**: When
   the user toggles Source = Live, the piano roll is destroyed. Any cached
   pointer becomes dangling. ALWAYS look up via the `verifyView` callback
   each time the editor is constructed.

4. **Hidden params with kCanAutomate=false**: pluginval will flag this.
   Hidden step params still need `kCanAutomate` because they are written
   programmatically by the piano roll (and may be automated by host).

5. **Right-click on Linux with no physical right button**: VSTGUI handles
   Ctrl+Click → kRButton automatically on macOS. On Linux with a single-button
   trackpad, users use the standard trackpad gesture (which is mapped by the
   OS to right-click). No special handling required.

6. **Drag painting OUTSIDE the lane length**: If user drags from col 10 to col
   20 but length = 16, only cols 10-15 should be painted; cols 16-19 ignored.
   The `stepFromX` helper must clamp.

---

## CI Acceptance Checklist

Before merging:

- [ ] All FRs marked ✅ MET in `spec.md` with concrete evidence (file:line, test
      name, measured value).
- [ ] All SCs marked ✅ MET in `spec.md` with concrete evidence.
- [ ] `gradus_tests` passes with zero failures.
- [ ] `dsp_tests` passes with zero failures (defense; should be unchanged).
- [ ] `pluginval --strictness-level 5 --validate` passes with zero new warnings.
- [ ] `tools/run-clang-tidy.ps1 -Target gradus` passes with zero new warnings.
- [ ] Manual verification of US1-US6 above completed.
- [ ] Cross-platform CI (Windows, macOS, Linux) build green.
- [ ] Living architecture doc updated (if `specs/_architecture_/` exists for
      Gradus).
- [ ] No emojis in any source / test / docs file (project convention).
- [ ] Version bump in `version.json` if shipping (e.g., Gradus 1.8.0) + matching
      CHANGELOG entry (per user feedback note).
