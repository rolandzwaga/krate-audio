# Gradus Audit — Implementation Plan

## Scope

This plan converts the adversarially-verified Gradus audit findings into an ordered, task-by-task fix program executable by a less-capable model with no memory of the audit. It covers the Gradus step-arpeggiator plugin (`plugins/gradus/`), its Gradus-local controller/processor/UI code, and the **shared** arp engine it consumes (`dsp/include/krate/dsp/processors/arpeggiator_core.*` and `midi_note_delay.h`, plus `plugins/shared/`). Every task follows the repo's mandatory workflow (failing test → implement → build → run → gate → commit). **CONFIRMED** findings are fixed outright. **PLAUSIBLE** findings are "verify first, then fix": write the failing test first; if it *passes* against current code, the finding is refuted — skip the fix, keep the test, record the refutation. Any task touching `dsp/include/krate/dsp/processors/` or `plugins/shared/` is flagged **CROSS-PLUGIN IMPACT** because Ruinae compiles the same headers: Ruinae's tests must stay green and its saved-state must remain loadable.

Do not relax any threshold, do not delete a feature to make a test pass, and do not amend commits. One commit per task (per phase is acceptable where noted). Build command everywhere:

```
"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target gradus_tests
```

Run command everywhere:

```
build/windows-x64-release/bin/Release/gradus_tests.exe 2>&1 | tail -5
```

For CROSS-PLUGIN tasks additionally build+run `dsp_processors_tests` and `ruinae_tests`. Run pluginval (strictness 5) on `Gradus.vst3` and `./tools/run-clang-tidy.ps1 -Target gradus -BuildDir build/windows-ninja` (and `-Target dsp` for engine edits) before each commit. Skip pluginval only for test-only tasks.

---

## Findings-to-Phases Mapping

| ID | Title | Severity | Verdict | Phase |
|----|-------|----------|---------|-------|
| F3 | Live-mode Play edge orphans sounding arp/echo notes | high | CONFIRMED | 1 |
| F11 | Ring editing writes to wrong lane (Modifier/Condition/Ratchet cross-wire) | high | CONFIRMED | 1 |
| F12 | Initial ring geometry step counts seeded in wrong lane order | low | CONFIRMED | 1 (bundled with F11) |
| F1 | Data race: `laneSpeedCurveDepths_` non-atomic, cross-thread | high→medium | CONFIRMED | 2 (CROSS-PLUGIN) |
| F4 | `setActive(false)` never flushes sounding notes | medium | CONFIRMED | 2 |
| F6 | Echo NoteOff dropped at 512-event output cap (stranded note) | medium | CONFIRMED | 2 (CROSS-PLUGIN) |
| F14 | MIDI-delay lane metadata params 3703–3706 never applied to engine | medium | CONFIRMED | 2 |
| F5 | Delay-echo events appended unsorted → non-monotonic sampleOffsets | medium | PLAUSIBLE | 3 (CROSS-PLUGIN) |
| F7 | `currentArpNotes_` write precedes its bounds guard (OOB at count==32) | low | PLAUSIBLE | 3 (CROSS-PLUGIN) |
| F13 | `seqNoteLane_` ctor writes steps 16–31 into index 15 (no-op) | low | PLAUSIBLE | 3 (CROSS-PLUGIN) |
| F2 | Audition NaN/Inf sanitizer compiled out by SDK `-ffast-math` (macOS) | medium | PLAUSIBLE | 3 |
| F18 | `silenceFlags` from `isActive()` outside `auditionEnabled_` guard | medium | PLAUSIBLE | 3 |
| F19 | `tempoBPM` assigned without `kTempoValid`; fallback only guards stopped branch | low | PLAUSIBLE | 3 |
| F8/F15 | Controller discards state version, no `{2,3}` gate (dup findings) | medium/low | PLAUSIBLE | 3 |
| F17 | Controller "ArpSkip" IMessage handler is dead (no sender) | low | CONFIRMED | 4 |
| F10 | Nearest-`kLaneSpeedValues`-index snap duplicated with drift | low | PLAUSIBLE | 4 |
| F9 | Migration tests never exercise speed-curve >64 pts / MIDI-delay active-flag EOF | low | CONFIRMED | 5 |
| F16 | SC-008 all-rest test never asserts the playhead advanced | high→medium | PLAUSIBLE | 5 |

Notes on consolidation: **F8 and F15 are the same defect** (controller does not apply the processor's `{2,3}` version gate) — implement once (Phase 3), fix the stale comment as part of it. **F11 and F12 share one root cause** (UI-order vs `getArpLane`-order lane indexing) — fix together in Phase 1.

---

## Phase 1 — High-severity correctness bugs (CONFIRMED)

### Task 1.1 — F3: Stop the Live-mode Play-edge from orphaning sounding notes

**Root cause.** `processor.cpp:140-143` runs `arpCore_.reset(); midiDelay_.reset();` on every rising transport-play edge `if (isPlaying && !wasTransportPlaying_)`, with no source-mode guard. In Live mode the arp free-runs (`blockCtx.isPlaying` forced `true` at `processor.cpp:162`) and emits NoteOns to the host while the transport is stopped. `ArpeggiatorCore::reset()` (`arpeggiator_core.h:241-257`) clears `currentArpNoteCount_`, `pendingNoteOffCount_`, `heldNotes_`, `physicalKeysHeld_` **without emitting any NoteOff**; `midiDelay_.reset()` (`midi_note_delay.h:100-104`) discards pending echoes. Result: every sounding arp note + pending echo hangs, and the held chord is forgotten so the arp goes silent. In Sequencer mode the same reset is *correct* (pattern restart; sequencer is transport-gated). The codebase already knows the right pattern — the source-mode toggle uses `arpCore_.requestPanicNoteOff()` (`processor.cpp:788-795`; helper at `arpeggiator_core.h:615-619`) to flush note-offs on the next block before clearing.

1. **Write failing test.** Extend `plugins/gradus/tests/unit/processor/` — create `live_mode_play_edge_test.cpp`, `TEST_CASE("Live mode Play-edge flushes sounding notes and preserves held chord")`. Drive the processor in Live source-mode, transport stopped: feed a NoteOn chord, run ~8 blocks capturing emitted output events (collect the set of pitches with an outstanding NoteOn). Then present a ProcessContext with `kPlaying` newly set (rising edge) and run one more block. Assert: (a) for every pitch still sounding before the edge, a matching NoteOff is emitted in the block(s) at/after the edge; (b) no NoteOn is left unmatched at end of run; (c) the held chord is preserved (the arp keeps producing notes after the edge rather than falling silent — assert at least one NoteOn is emitted in the blocks following the edge without re-pressing keys). Confirm it FAILS (notes hang, arp silent) before the fix.

2. **Implement fix.** In `processor.cpp` around lines 140-143, gate the destructive reset on Sequencer mode only. Compute `isSequencer` (already derived at ~155-156) *before* the play-edge branch, or move the branch after it. For Sequencer mode keep `arpCore_.reset(); midiDelay_.reset();` (pattern restart). For Live mode, on the rising play edge do **not** call `reset()`; instead call `arpCore_.requestPanicNoteOff()` so the next `processBlock` emits a NoteOff at sample 0 for every entry in `currentArpNotes_` before continuing, and flush the delay lane's pending echoes via a note-off-emitting path rather than a silent `midiDelay_.reset()` (if no such method exists, add an emergency-flush that queues note-offs for pending echoes with `noteOnEmitted && !noteOffEmitted`; see Task 2.2 which adds exactly this mechanism — sequence Task 2.2 before finalizing this if the flush helper is reused). Preserve `heldNotes_`/`physicalKeysHeld_` in the Live branch.

3. Build. 4. Run `gradus_tests`. 5. pluginval + clang-tidy (`gradus`), then commit.

**Sequencing note:** if you implement Live-mode echo flushing via a new `midi_note_delay.h` helper, that is CROSS-PLUGIN and Task 2.2 must land first (or be co-implemented) — build+run `dsp_processors_tests` and `ruinae_tests` in that case.

### Task 1.2 — F11 + F12: Fix ring lane-index cross-wire (edit routing and geometry seeding)

**Root cause.** The RingRenderer indexes lanes in **UI order** (`ring_geometry.h:62-75 subZoneToLaneIndex`; `ring_renderer.h`: 3=Modifier, 4=Condition, 5=Ratchet). But `getArpLaneStepBaseParamId` (`controller_arp.cpp:48-58`) is in **`getArpLane` order** (3=Ratchet, 4=Modifier, 5=Condition). Positions 0,1,2,6,7 coincide; 3/4/5 are cross-wired.

- **F11 (high):** the ring edit callbacks at `controller_verify_view.cpp:1117-1135` pass the UI-order `laneIndex` straight into `getArpLaneStepBaseParamId(laneIndex)+step`, so editing Modifier (UI 3) writes the Ratchet param, Condition (UI 4) writes Modifier, Ratchet (UI 5) writes Condition — and a bar-type Ratchet drag pushes continuous normalized values into the discrete Condition lane. Every other path (`ringDataBridge_.setLane` at `verify_view.cpp:1081-1088`, playhead entries at `controller_view_sync.cpp:337-347`, `kDepthParamIds` at `verify_view.cpp:969-978`) already uses UI order — `kDepthParamIds` is explicitly re-ordered into UI order, proving the authors knew the orders differ.
- **F12 (low, same root cause):** `constructArpLanes` seeds geometry with `geo.setLaneStepCount(i, getArpLane(i)->getActiveLength())` (`controller_verify_view.cpp:1097-1101`) in `getArpLane` order, while `RingGeometry::hitTest` (`ring_geometry.h:386-413`) reads `laneStepCounts_[3..5]` in UI order. Benign at defaults (Modifier/Ratchet/Condition all default length 16) and self-heals on the first length edit (`kDirtyLaneLengths` re-seeds in UI order at `controller_view_sync.cpp:413-428`), but the init path disagrees with the update path.

1. **Write failing tests** (controller-level, in `plugins/gradus/tests/unit/` — create `ring_edit_routing_test.cpp`):
   - `TEST_CASE("Ring edit routes UI lane index to matching step param")`: invoke the wired value-change callback with `laneIndex=3` and assert the edited ParamID is `kArpModifierLaneStep0Id` (NOT `kArpRatchetLaneStep0Id`); with `laneIndex=4` assert `kArpConditionLaneStep0Id`; with `laneIndex=5` assert `kArpRatchetLaneStep0Id`. Confirm it FAILS today (asserts the swapped IDs).
   - `TEST_CASE("Ring geometry init seeds step counts in UI order")`: set Modifier length and Ratchet length to *different* values, construct the arp lanes/geometry, and assert `geometry.laneStepCount(3) == modifierLength` and `geometry.laneStepCount(5) == ratchetLength`. Confirm it FAILS today.

2. **Implement fix.**
   - F11: In the three lambdas at `controller_verify_view.cpp:1117-1135` (`setBeginEditCallback`/`setValueChangeCallback`/`setEndEditCallback`), stop calling `getArpLaneStepBaseParamId(laneIndex)`. Add a UI-order step-base lookup (a small local table or helper mirroring `kDepthParamIds`' UI ordering: index 3→`kArpModifierLaneStep0Id`, 4→`kArpConditionLaneStep0Id`, 5→`kArpRatchetLaneStep0Id`; indices 0,1,2,6,7 unchanged) and use it for the `+step` param id. Do not touch `getArpLaneStepBaseParamId` itself (Ruinae/other callers rely on its `getArpLane` order).
   - F12: In `constructArpLanes` (`controller_verify_view.cpp:1097-1101`), seed geometry in UI order — i.e. `geo.setLaneStepCount(i, <the lane drawn at ring index i>->getActiveLength())`, using the same lane pointers `ringDataBridge_.setLane(...)` binds (Modifier→3, Condition→4, Ratchet→5), not `getArpLane(i)`.

3. Build. 4. Run `gradus_tests`. 5. pluginval + clang-tidy (`gradus`), then commit.

---

## Phase 2 — Medium-severity correctness bugs (CONFIRMED)

### Task 2.1 — F1: Make `laneSpeedCurveDepths_` atomic (data race) — CROSS-PLUGIN IMPACT

**Root cause.** `laneSpeedCurveDepths_` is a plain `std::array<float,kNumLanes>` (`arpeggiator_core.h:1503`). `setLaneSpeedCurveDepth` (`arpeggiator_core.cpp:676-679`) writes it directly. Gradus's `notify()` "SpeedCurveTable" handler calls it on the **message thread** for lanes 0-7 (`processor.cpp:407`), while the **audio thread** reads the same member every block in `advanceLaneBySpeed` (`arpeggiator_core.cpp:522-523`, `:532`). Depth for lanes 0-7 is set only via `notify()` (`applyParamsToEngine` sets depth only for lane 9, `processor.cpp:832`). This is unsynchronized cross-thread access — a formal data race / UB (the sibling `laneSpeedCurveEnabled_` is `std::atomic<bool>` and the table uses atomic dirty-flag staging; depth is the lone exception). Practical worst case is a one-block-stale but valid clamped depth, but it is TSan-flaggable UB and undermines the engine's determinism guarantees.

**CROSS-PLUGIN:** Ruinae also compiles `arpeggiator_core.h` and reads/writes this member.

1. **Write failing test.** In `dsp/tests/unit/processors/` add `arpeggiator_core_speed_curve_depth_race_test.cpp`, `TEST_CASE("laneSpeedCurveDepth store/load is atomic and consistent")`. A pure single-threaded functional test cannot fail on the race, so make this a *type/behaviour* guard: set depth via `setLaneSpeedCurveDepth`, read it back through the audio path, assert round-trip and clamping `[0,1]` on write and `[0.1,8.0]` effect after use. If a ThreadSanitizer build lane exists (editor-lifecycle harness under TSan), add a concurrency case that spins `setLaneSpeedCurveDepth` on one thread while `processBlock` runs on another and assert no TSan report. If TSan is unavailable, note in the test comment that the primary guarantee is the atomic type change; the functional test locks the round-trip behaviour.

2. **Implement fix.** Change `laneSpeedCurveDepths_` at `arpeggiator_core.h:1503` to `std::array<std::atomic<float>, kNumLanes>`. Update `setLaneSpeedCurveDepth` (`arpeggiator_core.cpp:676-679`) to `.store(value, std::memory_order_relaxed)` and the audio-thread reads at `:522-523`/`:532` to `.load(std::memory_order_relaxed)` — mirror exactly how `laneSpeedCurveEnabled_` (atomic bool) is handled. Preserve existing clamping. Verify no aggregate-init or `= {}` breaks with atomics (atomics are not copyable — initialize element-wise in the ctor or via default member init).

3. Build `gradus_tests` **and** `dsp_processors_tests`. 4. Run both; **also build+run `ruinae_tests`**. 5. pluginval on Gradus + clang-tidy (`gradus` and `dsp`), then commit. Confirm Ruinae saved-state is unaffected (this is runtime state, not serialized — no byte-golden impact).

### Task 2.2 — F6: Queue an emergency NoteOff when an echo is dropped at the output cap — CROSS-PLUGIN IMPACT

**Root cause.** `emitDueEchoes` stops emitting once `outCount == maxOutput` (guards at `midi_note_delay.h:257` and `:271`), so a due echo NoteOff can be skipped when the 512-slot `combinedEvents_` output span is full. `advanceAndCompact` then decrements countdowns and, at `:307` (`if (echo.noteOffRemaining < -blockSize) continue;`), unconditionally drops the echo — including one with `noteOnEmitted && !noteOffEmitted` — with no re-queue. The emergency-note-off mechanism (`addPendingEcho` steal path `:327-338`, drained by `emitEmergencyNoteOffs` `:347-361`) covers only pending-buffer overflow, not the output-span cap. Under two consecutive output-saturated blocks starving the same echo, its NoteOff is lost and the NoteOn hangs.

**CROSS-PLUGIN:** shared engine header; Ruinae's delay lane consumes it.

1. **Write failing test.** In `dsp/tests/unit/processors/midi_note_delay_test.cpp` add `TEST_CASE("Output-span saturation never strands an echo NoteOff")`. Use a *small* `maxOutput` (e.g. 64) so the cap is easy to saturate, schedule enough simultaneously-due echoes that emission is truncated for at least two consecutive blocks, run many blocks, and assert every emitted echo NoteOn eventually receives a matching NoteOff (balance == 0 at end). Confirm it FAILS today (an unmatched NoteOn remains).

2. **Implement fix.** In `advanceAndCompact` (`midi_note_delay.h` ~`:299-309`), before the unconditional drop at `:307`, detect `echo.noteOnEmitted && !echo.noteOffEmitted` and queue an emergency NoteOff (same array/mechanism used by the `addPendingEcho` steal path at `:327-338`, drained by `emitEmergencyNoteOffs`) so the next block flushes it. Do not change the drop threshold; only add the note-off obligation before discarding. Ensure the emergency array cannot itself overflow silently (bound-check consistent with the existing steal path).

3. Build `gradus_tests` **and** `dsp_processors_tests`. 4. Run both; **build+run `ruinae_tests`**. 5. pluginval on Gradus + clang-tidy (`dsp`, `gradus`), then commit.

### Task 2.3 — F14: Apply MIDI-delay lane metadata (Speed/Swing/Jitter/Curve-Depth) to the engine

**Root cause.** The MIDI-delay lane is lane index 8. `applyParamsToEngine`'s MIDI Delay Lane block (`processor.cpp:748-777`) calls only `arpCore_.midiDelayLane().setLength()` and per-step `midiDelay_` config. It never calls `setLaneSpeed(8,…)`, `setLaneSwing(8,…)`, `setLaneLengthJitter(8,…)`, or `setLaneSpeedCurveDepth(8,…)`, so params `midiDelayLaneSpeed` (3703), `Swing` (3704), `Jitter` (3705), `SpeedCurveDepth` (3706) never reach the engine (they default 1.0x/0/0/0). The atomics exist and are stored (`arpeggiator_params.h:767-780`), saved (`:2111-2114`), loaded (`:2485-2491`), and mirrored to UI (`controller_state_helpers.h:127-147`) — the whole persistence/UI chain implies they work, but they are inert. Lane 8 advances via `advanceLaneBySpeed(midiDelayLane_, 8)` (`arpeggiator_core.cpp:602`), which reads exactly these slots.

Nuance to respect: `SpeedCurveDepth(3706)` is additionally moot unless lane 8's curve is enabled (`setLaneSpeedCurveEnabled` is only looped for lanes 0-7 at `processor.cpp:701-704`, and depth is gated at `arpeggiator_core.cpp:522` on the enabled flag). Speed/Swing/Jitter (3 of 4) are unambiguously consumed and unambiguously inert today. Wire all four; if you want 3706 to have effect, also enable lane 8's curve — but do not regress lanes 0-7.

1. **Write failing test.** In `plugins/gradus/tests/unit/processor/` add `midi_delay_lane_metadata_test.cpp`, `TEST_CASE("MIDI-delay lane speed metadata advances lane 8 polymetrically")`. Set `midiDelayLaneSpeed` (3703) to a non-1.0 value; over N blocks assert the delay lane's step index advances at a different rate than a base lane (or that `currentDelayStep` phase relative to a base lane changes). Confirm it FAILS today (lane always clocks at base rate).

2. **Implement fix.** In the MIDI Delay Lane block of `applyParamsToEngine()` (`processor.cpp:748-777`) add `arpCore_.setLaneSpeed(8, …)`, `setLaneSwing(8, …)`, `setLaneLengthJitter(8, …)`, and `setLaneSpeedCurveDepth(8, …)`, reading the four `midiDelayLane*` atomics, mirroring the lane-9 block at `processor.cpp:826-833`. These are Gradus setters on the shared engine (not header edits), so this is Gradus-local. Route depth through the same prev-value-cache convention the surrounding setters use if applicable.

3. Build. 4. Run `gradus_tests`. 5. pluginval + clang-tidy (`gradus`), then commit.

### Task 2.4 — F4: Guarantee note-offs across `setActive(false)`

**Root cause.** `setActive()` (`processor.cpp:56-63`) only does work when `state==true` (resets `arpCore_`/`midiDelay_`); on `state==false` it falls straight through to `AudioEffect::setActive` with no note-off flush. `terminate()` (`:51-54`) and the defaulted destructor do nothing. The engine genuinely holds notes across blocks (`pendingNoteOffs_` with per-note `samplesRemaining`, `arpeggiator_core.h:993-994`, `:1138-1202`; legato/slide/tie suppress offs). Gradus emits its own arp/echo NoteOns to a MIDI **output** bus that the host cannot recall, so deactivating while notes sound strands them downstream.

**VST3 constraint the executor MUST respect:** `process()` is not called after `setActive(false)` returns, so you *cannot* emit MIDI directly from `setActive(false)`. Two acceptable implementations — pick the one the test can pin:
- (Preferred, testable) On `setActive(false)`, register the flush obligation on the engine (`arpCore_.requestPanicNoteOff()` + queue emergency echo note-offs), and change `setActive(true)` so that on the next activation it does **not** silently `reset()` away outstanding note-offs — instead let the first `processBlock` after reactivation emit them before clearing. Acceptance contract: after `setActive(false)` then `setActive(true)` then one `process()`, every previously-sounding pitch's NoteOff is emitted.
- (Fallback, if the above proves infeasible without a large refactor) Document explicitly in code that Gradus relies on the host all-notes-off contract for the deactivate-mid-note case, and at minimum silence the internal `auditionVoice_` on deactivate. Only take this path if the preferred one cannot be made to pass a test — record the reason in the commit message.

1. **Write failing test.** In `plugins/gradus/tests/unit/processor/` add `set_active_flush_test.cpp`, `TEST_CASE("setActive(false) does not strand sounding arp notes")`. Activate, emit a run of notes (capture outstanding NoteOns), call `setActive(false)`, then `setActive(true)`, then one `process()` block; assert every outstanding pitch's NoteOff is emitted (balance == 0). Confirm it FAILS today.

2. **Implement fix** per the preferred approach above, editing `processor.cpp:56-63` (add the `state==false` branch) and the `state==true` reactivation ordering. Keep Sequencer/Live behaviour otherwise unchanged.

3. Build. 4. Run `gradus_tests`. 5. pluginval + clang-tidy (`gradus`), then commit.

---

## Phase 3 — Lower-severity & PLAUSIBLE correctness (verify first, then fix)

For every task in this phase: **write the test first. If it PASSES against unmodified code, the finding is refuted — do not implement the fix, keep the test as a regression guard, and record "refuted: test passed on current code" in the commit message.** If it FAILS, implement the fix and re-run.

### Task 3.1 — F7: Move `currentArpNotes_` write inside its bounds guard — CROSS-PLUGIN IMPACT

**Claim.** `arpeggiator_core.cpp:1200` writes `currentArpNotes_[currentArpNoteCount_] = result.notes[0]` *before* the `if (currentArpNoteCount_ < 32) ++currentArpNoteCount_` guard at `:1201-1203`. `currentArpNotes_` is `std::array<uint8_t,32>` (`arpeggiator_core.h:1638`); a write at index 32 is OOB and (per one verifier) corrupts the adjacent `currentArpNoteCount_`. One skeptic argued index 32 is unreachable under clamped gate values; the other found a reachable Chord→single-note transition path. Regardless of reachability, moving the store inside the guard is unconditionally correct and cheap.

**CROSS-PLUGIN:** shared engine; Ruinae compiles it.

1. **Write failing test.** In `dsp/tests/unit/processors/` add to the arpeggiator-core sequencer/chord test file `TEST_CASE("currentArpNoteCount never exceeds 32 across chord->single-note transition")`: drive Chord mode with 32 held notes and a gate >100% (within the ~200% clamp) so pending offs are not yet due, then switch to a single-note arp mode and fire a step; assert `currentArpNoteCount()` stays ≤ 32 and no memory corruption (e.g. count is not clobbered to a MIDI-note value). This is the reachability probe: if it passes, F7 is refuted-as-unreachable — still apply the fix (belt-and-suspenders is correct) OR keep the test and note refutation per your policy; because the fix is a zero-risk one-liner, prefer to apply it.

2. **Implement fix.** Change `arpeggiator_core.cpp:1200-1203` to `if (currentArpNoteCount_ < 32) { currentArpNotes_[currentArpNoteCount_++] = result.notes[0]; }`.

3. Build `gradus_tests` + `dsp_processors_tests`. 4. Run both + `ruinae_tests`. 5. clang-tidy (`dsp`), pluginval (Gradus), commit.

### Task 3.2 — F13: Fix `seqNoteLane_` constructor init span — CROSS-PLUGIN IMPACT

**Claim.** Ctor does `seqNoteLane_.setLength(16)` then `setStep(i,60)` for `i=0..31` (`arpeggiator_core.h:220-224`); `ArpLane::setStep` clamps index to `length_-1 = 15` (`arp_lane.h:78-82`), so steps 16-31 never get 60 and stay 0. Masked in Gradus (processor repopulates all 32 steps each block, `processor.cpp:806-814`) and the documented invariant (`step[0]=60`, all rest flags=1) *is* satisfied — so one verifier called it refuted-as-harmless. The fix (set length 32, fill, restore length) matches the processor's own `syncFloatLane`/`syncIntLane` idiom and is safe.

**CROSS-PLUGIN:** shared engine; Ruinae compiles it.

1. **Write failing test.** In `dsp/tests/unit/processors/` add `TEST_CASE("bare ArpeggiatorCore seqNote step 20 defaults to C4 when length expanded")`: construct a bare core, expand seqNote length to 32, clear rest flags on steps 16-31, and assert the emitted pitch on a high step (e.g. 20) is 60, not 0. If it passes today, F13 is refuted — record and keep the test; the fix is still harmless to apply.

2. **Implement fix.** In the ctor (`arpeggiator_core.h:220-224`) set `seqNoteLane_.setLength(32)` before the fill loop, then `setLength(16)` (the intended default) afterward.

3. Build `gradus_tests` + `dsp_processors_tests`. 4. Run both + `ruinae_tests`. 5. clang-tidy (`dsp`), pluginval (Gradus), commit.

### Task 3.3 — F5: Emit MIDI output events in non-decreasing sampleOffset order — CROSS-PLUGIN IMPACT

**Claim.** `midi_note_delay.h` `process()` (`:132-151`) appends pass-through arp events, then emergency note-offs at offset 0 (`:142`), then due echoes in pending-array order (`:145`/`emitDueEchoes :253-284`), and `processor.cpp:190-225` forwards `combinedEvents_` in raw order with no sort — producing non-monotonic sampleOffsets within a block. Both verifiers confirmed the ordering fact; both also confirmed a per-echo NoteOff cannot precede its own NoteOn (so the *stuck-note* consequence is NOT reachable), and one noted VST3 mandates monotonic order for host→plugin *input*, not plugin *output*, and Gradus passes pluginval s5. So the real defect is cross-event timing-glitch risk in strict hosts, not hanging notes — genuine but medium-at-most.

**Verify-first framing:** the test asserts monotonic output ordering (the claimed defect), NOT stuck notes.

1. **Write failing test.** In `dsp/tests/unit/processors/midi_note_delay_test.cpp` add `TEST_CASE("MIDI-delay output events are monotonic in sampleOffset")`: schedule a due echo whose emission offset precedes a later pass-through arp event in the same block, run one block, and assert the emitted event stream has non-decreasing sampleOffset. If it passes today, refuted — keep test, record.

2. **Implement fix (only if it fails).** Stable-sort `combinedEvents_[0..count)` by `sampleOffset` (tie-break NoteOff-before-NoteOn at equal offset) before `processor.cpp` routes them, OR merge echoes into the pass-through stream in time order inside `midi_note_delay.h`. The sort must be allocation-free on the audio thread (in-place on the fixed `combinedEvents_` buffer; `std::stable_sort` allocates — prefer an in-place insertion sort over the small fixed span, or a fixed scratch buffer sized to `combinedEvents_`). Do not change per-echo NoteOn/NoteOff pairing.

3. Build `gradus_tests` + `dsp_processors_tests`. 4. Run both + `ruinae_tests`. 5. clang-tidy (`dsp`), pluginval (Gradus), commit.

### Task 3.4 — F2: Isolate the audition NaN/Inf sanitizer into a fast-math-off TU

**Claim.** `audition_voice.h:99` guards output with `if (!std::isfinite(sample)) sample = 0.0f;`. Compiled into the Gradus processor TU, and on the **macOS Xcode** build the VST3 SDK adds `-ffast-math` globally (`extern/vst3sdk/cmake/modules/SMTG_PlatformToolset.cmake:41-46`, inside the `if(XCODE)` branch), so `std::isfinite` folds to `true` and the guard is dead-code-eliminated. One skeptic refuted the finding on two grounds: (a) the Linux (Ninja) build is NOT Xcode so does not add `-ffast-math` — the finding's Linux claim is false; (b) whether the SDK directory-scoped `add_compile_options` even propagates to the sibling-scoped `plugins/gradus` target is unestablished, and there is no reachable NaN source in `AuditionVoice` today. The other skeptic confirmed the macOS elision as a real, project-pattern-matching defense-in-depth gap. Net: verify propagation before investing.

**Verify-first framing:** because Windows/MSVC does not elide the guard, a Windows-only `gradus_tests` run cannot demonstrate the defect. This task's "test" is a **build/scope investigation** plus a portable guard test.

1. **Verify propagation (do this first, it decides the whole task).** Confirm on macOS whether `-ffast-math` actually reaches the Gradus processor TU: inspect the generated Xcode compile flags for `processor.cpp` (or add a temporary `#if defined(__FAST_MATH__)` static_assert in a Gradus TU and see if a macOS build trips it — if you cannot build macOS locally, rely on CI). If `-ffast-math` does NOT reach the Gradus target, the finding is **refuted** — record "refuted: SDK fast-math is directory-scoped to extern/vst3sdk and does not propagate to plugins/gradus" and stop. Also correct any plan/comment that claims Linux is affected — it is not (Ninja, non-Xcode).

2. **Write test (if propagation confirmed).** In `plugins/gradus/tests/` add `audition_voice_nan_guard_test.cpp`, `TEST_CASE("AuditionVoice sanitizes bit-pattern NaN sample")`, constructing a NaN via a `volatile` bit-pattern sink (see MEMORY reference "Injecting NaN/Inf in tests under -ffast-math"), feeding it through the sanitize path, asserting output is finite. Compile the *guard implementation* it exercises with fast-math OFF.

3. **Implement fix (if confirmed).** Move the non-finite clamp out of the header-inline path into a small `.cpp` (e.g. `plugins/gradus/src/dsp/audition_float_guard.cpp` + `.h`) compiled with fast-math disabled via `set_source_files_properties(... PROPERTIES COMPILE_OPTIONS "-fno-fast-math;-fno-finite-math-only")`, mirroring `plugins/membrum/CMakeLists.txt:85-91` (`membrum_float_guard`). Alternatively replace `std::isfinite` with a bit-pattern exponent check inside that TU. Wire it into `plugins/gradus/CMakeLists.txt`. Have `AuditionVoice::processBlock` call the guard function.

4. Build. 5. Run `gradus_tests`; if CI has a macOS lane, ensure it is green there. clang-tidy (`gradus`), pluginval, commit.

### Task 3.5 — F18: Compute `silenceFlags` inside the `auditionEnabled_` guard

**Claim.** Buffers are cleared (`processor.cpp:263-267`), the audition voice advances only when `auditionEnabled_` (`:270-275`), but `silenceFlags` is set from `auditionVoice_.isActive()` unconditionally at `:277`. `active_` clears only inside `processBlock` (`audition_voice.h:91-92`), so toggling audition off mid-envelope freezes the voice active forever → `silenceFlags` reports non-silent (0) on a genuinely zeroed buffer indefinitely. Both verifiers confirmed the mechanism but noted: the misreport is in the **safe** VST3 direction (host processes zeros, no wrong audio), there is **no stuck host MIDI note** (arp note-off is emitted unconditionally at `:213-220`), and it self-heals on re-enable — so one voted "none"/refute, the other "downgrade". Fix is a small correctness/clarity improvement.

1. **Write failing test.** In `plugins/gradus/tests/unit/processor/` add `silence_flags_audition_toggle_test.cpp`, `TEST_CASE("silenceFlags reports silent after audition disabled mid-note")`: enable audition, note-on, run a block (voice active, `silenceFlags==0`), disable audition mid-envelope, run a block, assert `data.outputs[0].silenceFlags == 0x3`. If it passes today, refuted — keep test, record.

2. **Implement fix.** In `processor.cpp` set `silenceFlags` inside/consistent with the `auditionEnabled_` state: `data.outputs[0].silenceFlags = (auditionEnabled_ && auditionVoice_.isActive()) ? 0 : 0x3;` (replace the unconditional `:277` assignment). Optionally release/advance the voice envelope on the disable transition to avoid a truncated tail; keep this minimal and do not change MIDI emission.

3. Build. 4. Run `gradus_tests`. 5. pluginval + clang-tidy (`gradus`), commit.

### Task 3.6 — F19: Apply the tempo fallback on the playing branch too

**Claim.** `hasTempo` (`kTempoValid`, `processor.cpp:122`) only sets `hostSupportsTransport_` (`:125-127`) and never gates tempo consumption. `blockCtx.tempoBPM` is set from `data.processContext->tempo` at `:160` with no validity check, and the `tempo>0 ? tempo : 120` fallback (`:164-167`) runs only inside `if (!transportPlaying)` — so a host reporting `kPlaying` with tempo 0 leaves `blockCtx.tempoBPM == 0`. Synced step math divides `60.0/tempoBPM` (`arpeggiator_core.h:1012`). Both verifiers confirmed the "backwards" fallback placement; both noted the downstream clamp at `arpeggiator_core.h:1035` (`>0 ? swungDuration : 1`) and the MIDI-delay path's tempo clamp in `note_value.h:232-233` bound the worst case to a degenerate 1-sample step (not NaN/crash) and that conformant hosts always supply valid tempo — hence low/rare. Still a real backwards guard worth flipping.

1. **Write failing test.** In `plugins/gradus/tests/unit/processor/` add `tempo_validity_test.cpp`, `TEST_CASE("kPlaying with tempo==0 yields finite synced step timing")`: present a ProcessContext with `kPlaying` set and `tempo==0`, run a block, and assert `blockCtx`/engine synced step duration is finite and > 0 (i.e. the 120 fallback applied). If it passes today, refuted — keep test, record.

2. **Implement fix.** In `processor.cpp` apply the `tempo > 0 ? tempo : 120.0` fallback unconditionally when assigning `blockCtx.tempoBPM` (at ~`:160`), or gate on `hasTempo`, rather than only inside the `!transportPlaying` branch (`:164-167`). Keep the stopped-branch behaviour identical.

3. Build. 4. Run `gradus_tests`. 5. pluginval + clang-tidy (`gradus`), commit.

### Task 3.7 — F8/F15: Apply the processor's `{2,3}` version gate in the controller and fix the stale comment

**Claim (single defect, two findings).** `Controller::setComponentState` (`controller.cpp:99-101`) and `loadComponentStateWithNotify` (`controller/controller_presets.cpp:116-118`) read the leading int32 version, `(void)version`, then unconditionally call `loadFullState` — with the stale comment "single version, no migration needed" (`controller.cpp:97`). The processor's `setState` (`processor.cpp:321-322`) rejects any version not in `{2,3}`. So for an out-of-range version the processor keeps defaults while the controller parses the payload into the UI cache — a processor/controller divergence. Multiple verifiers noted this is unreachable for any stream Gradus produces (getState always writes v3; VST3 hosts round-trip the plugin's own state; append-only versioning keeps v>3 prefixes readable), and the per-field EOF/clamp guards make the controller's tolerant read benign — hence the downgrade/refute votes. The mirror-the-gate fix is cheap, removes the asymmetry, and corrects a false comment.

1. **Write failing test.** In `plugins/gradus/tests/unit/vst/` (near `state_v2_v3_migration_test.cpp`) add `TEST_CASE("Controller rejects out-of-range state version like the processor")`: build a synthetic stream with version 4 (valid-looking v3 payload after it), call `setComponentState`, and assert the controller leaves parameters at defaults (does not push the payload into the param cache). If it passes today, refuted — keep test, record.

2. **Implement fix.** In both `controller.cpp:99-101` and `controller_presets.cpp:116-118`, after reading `version`, `return` (leaving controller defaults) when `version != 2 && version != 3`, mirroring `processor.cpp:321-322`, before calling `loadFullState`. Update the comment at `controller.cpp:97` (and the twin) to state that v2/v3 are accepted and the appendix is EOF-tolerant — remove the false "single version, no migration needed" wording.

3. Build. 4. Run `gradus_tests`. 5. pluginval + clang-tidy (`gradus`), commit.

---

## Phase 4 — Duplication / dead-code cleanups

### Task 4.1 — F17: Remove (or wire) the dead "ArpSkip" IMessage handler

**Claim (CONFIRMED).** `Controller::notify()` branches on message id "ArpSkip" and calls `handleArpSkipEvent()` (`controller.cpp:228-237`; def `controller_arp.cpp:15`), but the Gradus processor sends no IMessage at all (no `setMessageID`/`sendMessage` in `plugins/gradus/src/processor`), and the routing loop (`processor.cpp:199-224`) forwards only NoteOn/NoteOff, dropping `kSkip`. The branch is unreachable dead code; the ring skip-visualization is non-functional. The Ruinae sender used a different id ("ArpSkipEvent"), so no cross-plugin accident triggers it. Decide with the maintainer's intent: this is a *removal* task unless skip visualization is a desired feature — the audit frames deletion as the low-risk default.

1. **Write test.** In `plugins/gradus/tests/` add `TEST_CASE("Gradus processor emits no ArpSkip IMessage / handler unreachable")`. If deleting: assert (grep-level or by wiring) that no "ArpSkip" sender exists; primarily this task is guarded by the build still linking after removal. If instead wiring is chosen (maintainer wants the feature), write `TEST_CASE("Rest step triggers ArpSkip notification to controller")` feeding a rest-producing pattern and asserting the controller receives a skip notification — this must FAIL first.

2. **Implement (default: delete).** Remove the dead branch at `controller.cpp:228-237` and the now-orphaned `handleArpSkipEvent()` (`controller_arp.cpp:15`) and its declaration (`controller.h:86`). Remove only YOUR orphans; do not touch unrelated notify branches. (Alternative, only if wiring is explicitly requested: emit an "ArpSkip" IMessage from the `processor.cpp:213` else-branch when a `kSkip` event is dropped, and add the sender plumbing — this is a larger change; prefer deletion unless told otherwise.)

3. Build. 4. Run `gradus_tests`. 5. pluginval + clang-tidy (`gradus`), commit.

### Task 4.2 — F10: De-duplicate the nearest-`kLaneSpeedValues`-index snap (verify first)

**Claim (PLAUSIBLE, likely refuted on consequence).** The value→nearest-index snap loop is copy-pasted at `controller_state_helpers.h:128-138`, `:199-209`, and `parameters/arpeggiator_params.h:2828-2835`, with cosmetic drift (`bestDist` seed `99.0f` vs `999.0f`; `bestIdx` seed literal `3` vs named `kLaneSpeedDefault`). One verifier proved the seed is **dead** — the first loop iteration (`i=0`, value clamped to `[0.25,4.0]`, max distance 3.75 < 99) always overwrites `bestIdx`/`bestDist`, so all copies produce identical correct results and the "silent divergence if `kLaneSpeedDefault` is retuned" consequence cannot manifest. So the stated hazard is refuted; only trivial duplication remains. Treat this as an optional consistency cleanup — do it, but do not claim it fixes a bug.

1. **Write test.** In `plugins/gradus/tests/` add `TEST_CASE("speedValueToNormalized snaps to nearest kLaneSpeedValues index")`: for several stored speed multipliers assert the returned normalized position equals the true nearest index / (count-1), including boundary values. This also functions as the refutation check: run it against the three current inline copies (by exercising the load paths) — if all already agree with the helper, the "drift hazard" is refuted (record it), and the change is a pure refactor.

2. **Implement (refactor only).** Add one helper `speedValueToNormalized(float)` (returning normalized double) alongside `kLaneSpeedValues` in `parameters/arpeggiator_params.h`, and call it from all Gradus sites (`controller_state_helpers.h:128-138`, `:199-209`, `parameters/arpeggiator_params.h:2828-2835`). Replace any literal `3` seed with `kLaneSpeedDefault`. **Do NOT edit Ruinae's copy** (`plugins/ruinae/.../arpeggiator_params.h:1719-1723`) in this task — that is a separate plugin's file; leave it and mention it as remaining duplication. Confirm byte-identical behaviour (this must not change any snapped output).

3. Build. 4. Run `gradus_tests`. 5. pluginval + clang-tidy (`gradus`), commit.

---

## Phase 5 — Test-gap fills

### Task 5.1 — F9: Cover speed-curve >64-point desync and MIDI-delay active-flag EOF backward-compat

**Claim (CONFIRMED).** `state_v2_v3_migration_test.cpp` round-trips the v3 Sequencer Note appendix and spot-checks the v2 base block but has no case that (a) serializes a speed curve with >64 points and asserts the MIDI-delay/sequencer tail survives, nor (b) loads a MIDI-delay block lacking active flags. The underlying save/load asymmetry is real and reachable: save writes the point count uncapped and all points (`arpeggiator_params.h:2076-2084`); load clamps count to `[0,64]` and reads only that many (`:2431-2437`), so a >64-point curve leaves stragglers and desyncs the tail. The editor can insert unbounded points (`speed_curve_editor.h:238-251`; `SpeedCurveData::points` is an unbounded vector). The active-flag EOF handler (`:2479-2482`) is untested.

**Note:** this is a test-gap task, but the >64-point case may expose a *real save/load desync bug*. If the round-trip test fails because the tail is corrupted (not because of a test error), that is a genuine defect — fix it by capping the *saved* point count to 64 at the write site (`arpeggiator_params.h:2076-2084`) to match the loader, or by making the loader consume exactly the written count. **CROSS-PLUGIN IMPACT** if you edit `arpeggiator_params.h` save/load that Ruinae shares — build+run `ruinae_tests` and, if the shared save prefix in `plugins/shared/src/parameters/arp_params_common.h` is touched, run the cross-plugin byte-golden test.

1. **Write tests** in `plugins/gradus/tests/unit/vst/state_v2_v3_migration_test.cpp`:
   - `TEST_CASE("State round-trip preserves MIDI-delay and sequencer tail with a 65-point speed curve")`: build params with a 65-point speed curve plus distinct MIDI-delay + sequencer values, `getState`→`setState`, assert the MIDI-delay and sequencer fields survive unchanged. This must FAIL if a desync exists.
   - `TEST_CASE("MIDI-delay block without active flags loads with lane metadata retained")`: craft a stream whose MIDI-delay block ends before the active-flag appendix (EOF), load it, assert lane metadata is retained and defaults applied for the missing flags (exercises `:2479-2482`).

2. **Implement (only if a test reveals a real desync).** Cap saved point count to 64 at the write site, or align reader/writer counts. If both tests pass against current code, no production change — the tests stand as the regression guard.

3. Build (`gradus_tests`, plus `dsp_processors_tests`/`ruinae_tests` if `arpeggiator_params.h` was edited). 4. Run. 5. clang-tidy + pluginval only if production code changed; commit.

### Task 5.2 — F16: Assert the playhead advances in the SC-008 all-rest test

**Claim (PLAUSIBLE, downgraded to medium).** `sequencer_rests_advance_test.cpp:56-89` — named "all-rest pattern emits zero noteOns while playhead advances" — has exactly one live assertion (`REQUIRE(countNoteOns(events) == 0)`, `:79`); the playhead-advance verification (`:80-88`) is commented-out prose. A frozen playhead would pass unchanged. One verifier showed advancement IS covered elsewhere (FR-019 mixed-rest test at `:95-143` requires emission at odd steps; `arpeggiator_core_sequencer_test.cpp:150-174` asserts `currentStep() != 0`), so the "ships undetected" claim is overstated — but the SC-008 case itself is genuinely a hollow guard. Strengthen it; do not claim it caught a shipped bug.

**Verify-first framing:** this is a test-hardening task. There is no production fix; the "failing test" is the injected-bug check.

1. **Strengthen the test.** In `sequencer_rests_advance_test.cpp` (the SC-008 `TEST_CASE` at `:56-89`), add live assertions replacing the commented prose: sample `arp.seqNoteLane().currentStep()` at several points across the 400-block run (or track the max observed step) and `REQUIRE` it changes / wraps at least once, while keeping `REQUIRE(countNoteOns(events) == 0)`. `seqNoteLane().currentStep()` is public (used at `arpeggiator_core_sequencer_test.cpp:147`).

2. **Verify the new assertion has teeth.** Temporarily inject a stall (e.g. locally force the seqNote playhead to not advance) and confirm the new assertion FAILS; revert the injection. Record this in the commit message. No production code change ships.

3. Build `gradus_tests`. 4. Run. 5. clang-tidy not required for test-only; no pluginval. Commit.

---

## Completion Checklist (Completion Honesty — verify each, no relaxed thresholds)

Do not fill this from memory. For every row, open the cited code / run the cited test **now** and paste concrete evidence (file:line, test name, actual output). A ✅ without just-verified evidence is worse than an honest ❌.

- [ ] **F3** — `processor.cpp:140-143` play-edge branch is Sequencer-gated; Live-mode path flushes note-offs (panic + echo flush) and preserves `heldNotes_`. Evidence: new test in `live_mode_play_edge_test.cpp` passes; every pre-edge sounding pitch gets a NoteOff; arp not silent post-edge. Cite the assertion lines.
- [ ] **F11/F12** — Ring edit callbacks (`controller_verify_view.cpp:1117-1135`) use a UI-order step-base lookup; `laneIndex=3→kArpModifierLaneStep0Id`, `4→Condition`, `5→Ratchet`. Geometry init (`:1097-1101`) seeds in UI order. Evidence: `ring_edit_routing_test.cpp` passes for all three indices + geometry seeding.
- [ ] **F1** — `laneSpeedCurveDepths_` is `std::array<std::atomic<float>,kNumLanes>` (`arpeggiator_core.h:1503`); store/load relaxed at `.cpp:676-679` / `:522-523,:532`. **CROSS-PLUGIN**: `dsp_processors_tests` + `ruinae_tests` green; Ruinae state unaffected (runtime-only). Cite tail output.
- [ ] **F4** — `setActive(false)` registers a flush; first block after reactivation emits all outstanding NoteOffs (or documented host-contract fallback with reason). Evidence: `set_active_flush_test.cpp` balance == 0.
- [ ] **F6** — `advanceAndCompact` queues an emergency NoteOff before dropping an echo with `noteOnEmitted && !noteOffEmitted` (`midi_note_delay.h ~:299-309`). **CROSS-PLUGIN**: `dsp_processors_tests` + `ruinae_tests` green. Evidence: saturation test balance == 0.
- [ ] **F14** — `applyParamsToEngine` MIDI-delay block (`processor.cpp:748-777`) calls `setLaneSpeed/Swing/LengthJitter/SpeedCurveDepth(8, …)`. Evidence: `midi_delay_lane_metadata_test.cpp` shows lane-8 advances polymetrically.
- [ ] **F5** — output events monotonic in sampleOffset (allocation-free sort/merge). Verify-first outcome recorded (fixed / refuted). **CROSS-PLUGIN** if engine edited. Evidence: monotonic-order test.
- [ ] **F7** — write moved inside the `<32` guard (`arpeggiator_core.cpp:1200-1203`). **CROSS-PLUGIN**. Evidence: chord→single-note count-cap test; `ruinae_tests` green.
- [ ] **F13** — ctor sets length 32 → fill → restore (`arpeggiator_core.h:220-224`). **CROSS-PLUGIN**. Verify-first outcome recorded. Evidence: step-20 C4 test; `ruinae_tests` green.
- [ ] **F2** — propagation verified first; if confirmed, non-finite clamp moved to a `-fno-fast-math` TU wired in `plugins/gradus/CMakeLists.txt`; if refuted, recorded with the directory-scope reasoning. Linux-affected claim corrected. Evidence: build flags / bit-pattern NaN test.
- [ ] **F18** — `silenceFlags = (auditionEnabled_ && isActive()) ? 0 : 0x3` (`processor.cpp:277`). Verify-first outcome recorded. Evidence: toggle test asserts `0x3`.
- [ ] **F19** — tempo fallback applied unconditionally / gated on `hasTempo` (`processor.cpp ~:160`). Verify-first outcome recorded. Evidence: `kPlaying`+tempo0 finite-timing test.
- [ ] **F8/F15** — controller applies `{2,3}` gate in `controller.cpp:99-101` and `controller_presets.cpp:116-118`; stale comment corrected. Verify-first outcome recorded. Evidence: v4-rejection test.
- [ ] **F17** — dead "ArpSkip" branch + `handleArpSkipEvent` removed (or wired, if explicitly requested); build links clean. Evidence: grep shows no orphan; test present.
- [ ] **F10** — single `speedValueToNormalized` helper called from all three Gradus sites; literal-3 seed replaced with `kLaneSpeedDefault`; Ruinae copy left untouched and noted. Behaviour byte-identical. Evidence: snap test.
- [ ] **F9** — 65-point round-trip test + MIDI-delay-no-active-flags EOF test present and passing; if a real save/load desync was found, the write-site cap fix landed. **CROSS-PLUGIN** if `arpeggiator_params.h`/shared-save edited (byte-golden + `ruinae_tests` green).
- [ ] **F16** — SC-008 test now asserts `seqNoteLane().currentStep()` changes; injected-stall check confirmed the assertion fails, then reverted. Evidence: assertion lines + injection note.
- [ ] Global: zero compiler warnings on all touched targets; clang-tidy (`gradus`, and `dsp` for engine edits) clean; pluginval strictness 5 passes on `Gradus.vst3` for every task that changed plugin/engine source; no threshold relaxed, no feature quietly removed; one non-amended commit per task (or per phase where stated).
