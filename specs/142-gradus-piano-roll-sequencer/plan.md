# Implementation Plan: Gradus Piano-Roll Step Sequencer Mode

**Branch**: `142-gradus-piano-roll-sequencer` | **Date**: 2026-05-23 | **Spec**: `specs/142-gradus-piano-roll-sequencer/spec.md`
**Input**: Feature specification from `/specs/142-gradus-piano-roll-sequencer/spec.md`

## Summary

Add a "Sequencer" note-source mode to the Gradus standalone arpeggiator alongside the existing live MIDI input mode. A new VST3 `kArpSourceModeId` parameter (Live/Sequencer) selects between sources. When Sequencer is active, a new "Sequencer Note" lane (Length + 32 per-step pitches + 32 per-step rest flags + Speed/Swing/Jitter/SpeedCurveDepth/Playhead, structurally analogous to existing Gradus lanes) advances polymetrically and feeds programmed pitches into the existing arp pipeline. Held MIDI notes act as a transposition root (last-played wins), pitch lane offsets stack additively, and most note-source-shaping params (ArpMode/Octave/Latch/Markov/Euclidean/Pin/Range/ScaleQuantizeInput) are inert. A new VSTGUI piano-roll custom view (fixed 4-octave C2-B5 grid, visible only in Sequencer mode via `UIViewSwitchContainer`) lets the user paint patterns with click/drag/right-click. State versioning bumps from v2 to v3 with an explicit legacy loader so v2 presets load byte-identical Live-mode MIDI.

Technical approach: **The Sequencer Note lane is added as a true 10th lane inside `ArpeggiatorCore` (`kNumLanes: 9 → 10`).** This reuses the existing polymetric clocking, retrigger logic, swing/jitter infrastructure, and step-firing path — no duplication. Lane 10 is conditionally inert: `fireStep` skips its advancement and its modulator reads when `sourceMode_ == Live`, so Ruinae (which has no Sequencer mode) and Gradus Live-mode behavior is byte-identical to today (see SC-004 and new SC-004b). In Sequencer mode, `fireStep` branches early: it reads lane-10's current step pitch + rest flag (with rest=skip-emission semantics), bypasses ArpMode/Octave/etc. traversal, and emits the chosen pitch through the existing modulator-lane pipeline (Velocity/Gate/Pitch/Modifier/Ratchet/Condition/Chord/Inversion/MIDI Delay). Held MIDI notes populate `HeldNoteBuffer` in both modes (single source of truth); in Sequencer mode the buffer's last entry provides the transposition root + base velocity (fallback 100 when empty) and the chord lane still reads it for voicing context. Source toggle fires a clean note-off (panic) for any currently-sounding note; lane playheads are unchanged across the toggle. The piano roll is a new VSTGUI `CView` subclass `PianoRollView` that follows the established lane-editor pattern (CView/CViewContainer + `IControlListener`, IDependent on the 64 step parameters + Playhead). A `UIViewSwitchContainer` (template-switch-control = `ArpSourceMode`) swaps a dedicated UI slot between a Live-mode placeholder and the piano roll content; the editor window size is unchanged and lane editors remain visible in both modes. Existing pitch lane / global transpose / output scale quantize remain unchanged.

## Technical Context

**Language/Version**: C++20 (project standard)
**Primary Dependencies**: VST3 SDK 3.7.x, VSTGUI 4.12+, KrateDSP (in-repo Layer 0-4 library), Catch2 (tests)
**Storage**: Sequential binary state stream via `Steinberg::IBStreamer` (existing Gradus `getState`/`setState`); preset files via existing `Krate::Plugins::PresetManager`
**Testing**: Catch2 via `gradus_tests.exe` (unit/integration); `dsp_tests.exe` for any new Layer 2 DSP component; `pluginval --strictness-level 5` for VST3 compliance *(Constitution Principle XII: Test-First Development)*
**Target Platform**: Windows (MSVC), macOS (Clang/Xcode, AU/AUv3 wrapper), Linux (GCC) — VSTGUI-only UI (no Win32/Cocoa/GTK natives) per Principle VI
**Project Type**: VST3 plugin in monorepo — code lives under `plugins/gradus/` with shared library at `plugins/shared/`, DSP at `dsp/`
**Performance Goals**: < 5% single-core CPU @ 44.1 kHz stereo (project budget); no allocations on audio thread; `processBlock` completes within buffer duration
**Constraints**:
- Hard real-time safety on audio thread (no `new`/`delete`/`malloc`/locks/I/O/exceptions)
- Cross-platform UI must use VSTGUI only
- Backward compat: v2 presets MUST load byte-identical Live-mode MIDI output (SC-004)
- Strict parameter ID monotonicity: new params at 3741+, never touch the Ruinae-shared 3000-3372 block
- `pluginval --strictness-level 5` must pass with zero new failures/warnings (SC-006, FR-040)
**Scale/Scope**: 1 new top-level param (`kArpSourceModeId`) + 1 new lane (Length + 32 pitches + 32 rest flags + 4 modulators + 1 playhead = 70 params), `kNumLanes` extended 9 → 10 inside `ArpeggiatorCore` (lane 10 = Sequencer Note), 1 new VSTGUI custom view (`PianoRollView` ~600 LOC), 1 new audio-thread state field (`sourceMode_`) on Processor, 1 legacy `setState` path with v2→v3 binary fixture tests. Total spec: 40+ functional requirements (grilling pass added FR-021a/022a/022b/025a/025b/039a/039b), 10+ success criteria (added SC-004b for Ruinae preset byte-identical regression).

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide, dsp-architecture) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code (each phase in tasks.md will lead with a failing test)
- [x] Each task group will end with a commit step (per project conventions)

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete (searched class names, param ID block, held-note infrastructure)
- [x] No duplicate classes/functions will be created — `PianoRollView` is a net-new name; `SequencerNoteSource` was dropped (grilling-pass pivot, lane 10 inside `ArpeggiatorCore`); existing `ArpLane<T>`, `HeldNoteBuffer`, `ArpeggiatorCore` are reused

**Principle I (VST3 Architecture Separation):** Processor and Controller already separate in Gradus; new code respects the split. The Sequencer Note lane (lane 10 inside `ArpeggiatorCore`) lives audio-thread-only inside Processor; piano roll view is UI-thread-only inside Controller; state sync via existing `setComponentState()` + `IBStreamer`.

**Principle II (Real-Time Audio Thread Safety):** The Sequencer Note lane's step-pitch read and rest-flag check (inside `ArpeggiatorCore::fireStep`) are O(1), branchless on the hot path, and use atomic reads from `ArpeggiatorParams` (already established pattern). No allocations, no locks, no exceptions. Transposition root comes from `HeldNoteBuffer::byInsertOrder().back()` — no separate fixed-size stack needed.

**Principle III (Modern C++):** Use `std::array`, `std::span`, `constexpr`, `[[nodiscard]]` per project standards.

**Principle V (VSTGUI):** Piano roll is a `CView` subclass; uses `UIDescription` XML and `UIViewSwitchContainer` for mode visibility (template-switch-control pattern, already proven in Iterum and Ruinae).

**Principle VI (Cross-Platform):** All UI in VSTGUI — no Win32/Cocoa/GTK natives (Constitution Principle VI, Non-Negotiable). Right-click handled via `CView::onMouseDown` + `kRButton` (VSTGUI's cross-platform mouse button enum, identical pattern to `MidiDelayLaneEditor`). Any platform-specific UI solution is FORBIDDEN without explicit user approval.

**Principle VII (Project Structure & Build):** No new top-level directories. New files go under `plugins/gradus/src/dsp/`, `plugins/gradus/src/ui/`, `plugins/gradus/tests/unit/{ui,processor,vst}/`.

**Principle VIII (Testing Discipline):** Failing tests written first for every FR/SC. New tests added to `gradus_tests` CMake target.

**Principle IX (Layered DSP Architecture):** The Sequencer Note lane logic lives inside `ArpeggiatorCore` (Layer 2), composing `ArpLane<uint8_t>` (Layer 1). No reverse dependencies. Audio-thread integration happens inside the Gradus processor (plugin layer); `ArpeggiatorCore` remains in KrateDSP with no Gradus-specific coupling.

**Principle X (DSP Processing Constraints):** N/A — no oversampling, DC blocking, or feedback paths involved (this is MIDI-only logic).

**Principle XI (Performance Budgets):** Per-block overhead in Sequencer mode is bounded by O(32) step lookups + O(K) transposition stack operations where K ≤ 32. Comfortably within Layer 2 budget (< 0.5% CPU).

**Principle XII (Anti-Pivot Debugging):** N/A at plan time.

**Principle XIII (Test-First):** Captured in tasks.md template — every functional change ships with its failing test first.

**Principle XIV (Living Architecture Documentation):** Final task in tasks.md will update `specs/_architecture_/` if such a layered doc exists for Gradus (verify during /speckit.tasks).

**Principle XV (ODR Prevention):** Confirmed via grep — no `PianoRollView`, `kArpSourceModeId`, or `kArpSequencerNote*` symbols exist anywhere in `dsp/`, `plugins/`. (Note: `SequencerNoteSource` was dropped from the plan after the grilling-pass pivot — the lane lives natively in `ArpeggiatorCore`.)

**Principle XVI (Honest Completion):** Compliance table in spec.md will be filled with concrete file:line + test name evidence post-implementation. No relaxed thresholds (SC-003 = exact `heldNote - 60` offset, SC-004 = byte-identical MIDI, SC-006 = zero new pluginval issues).

**Principle XVII (Framework Knowledge):** vst-guide skill auto-loads. UIViewSwitchContainer pattern (template-switch-control) already documented and proven in this codebase.

**Principle XVIII (Spec Numbering):** Spec 142 is correct (highest existing in `specs/` directory + 1).

**No violations.** No Complexity Tracking entries required.

## Grilling-Pass Architectural Pivot (2026-05-23)

The initial plan proposed running the Sequencer Note lane **outside** `ArpeggiatorCore` with a Gradus-local clock. A grilling pass identified that this would duplicate:
- Step-clock state (sample counter, swing accumulator, jitter state, speed-curve table lookup)
- Retrigger logic (Note/Beat → playhead reset)
- Polymetric advancement formula (must stay byte-identical to other lanes)

After grilling, the architecture pivoted to **extending `kNumLanes` from 9 to 10** with lane 10 = Sequencer Note. This reuses 100% of the existing polymetric infrastructure (and the established Layer 2 DSP component `SequencerNoteSource` is no longer needed — the lane lives natively inside the core).

### Pivot decisions and rationale

| Decision | Choice | Why |
|----------|--------|-----|
| **Lane 10 location** | Inside `ArpeggiatorCore` (`kNumLanes = 10`) | Reuses polymetric clocking + retrigger + swing/jitter — no duplicate logic, no drift risk. |
| **Ruinae safety** | Lane 10 is **conditionally inert**: `fireStep` skips advance + modulator reads when `sourceMode_ == Live` | Default `sourceMode = Live`. Ruinae never sets it to Sequencer, so lane 10 has zero observable effect on Ruinae MIDI output. SC-004b (new) verifies this with a Ruinae preset corpus byte-identical regression test. |
| **Source pitch resolution in `fireStep`** | Early branch on `sourceMode_`: in Seq mode, read `seqNoteLane_.currentValue()` (pitch) + rest flag; skip ArpMode/Octave/etc. traversal entirely | Cleanest path; no synthesis of fake held-note pools; no callback indirection. |
| **Held-note routing in Seq mode** | Held notes populate `HeldNoteBuffer` in both modes (single source of truth) | ArpMode traversal is the only consumer that is mode-gated. Transposition root, base velocity, and chord-lane voicing all still read `HeldNoteBuffer::byInsertOrder()` normally. |
| **Source toggle stuck notes** | Note-off panic on any sounding note at toggle; lane playheads unchanged | No restart-jarring; no stuck notes; pending MIDI delay echoes survive naturally (MidiNoteDelay is downstream and source-agnostic). |
| **v2→v3 migration test** | Checked-in binary fixtures in `plugins/gradus/tests/fixtures/` + paired golden MIDI files; test asserts byte-identical MIDI from migrated state | Deterministic. Real v2 binary captured pre-bump. |
| **Piano roll UI placement** | Dedicated slot in `editor.uidesc`, swapped between empty placeholder (Live) and piano roll (Sequencer) via `UIViewSwitchContainer`. Editor size unchanged. Lane editors always visible. | Lanes still apply in both modes (FR-020) so they must stay reachable. Editor resize is complex; overlay obscures lanes; tabs hide lanes from Live users unnecessarily. |

### Files affected by pivot (vs prior plan)

- `dsp/include/krate/dsp/processors/arpeggiator_core.h` — MODIFIED (was unchanged in prior plan):
  - `kNumLanes`: 9 → 10
  - Add `seqNoteLane_` (`ArpLane<uint8_t>` storing pitch per step) + parallel `seqRestFlags_[32]` atomic array read at firing time (MIDI delay pattern)
  - Add `sourceMode_` member + `setSourceMode(SourceMode)` API
  - `fireStep`: conditional inert branch (`if (sourceMode_ == Live) skip lane 10 advance`); early-branch source-pitch resolution in Seq mode
  - `setRetrigger`: now also resets lane 10's playhead in Seq mode (Note/Beat behavior)
- `plugins/gradus/src/dsp/sequencer_note_source.h` — **REMOVED FROM PLAN** (no longer needed)
- `plugins/gradus/tests/unit/processor/sequencer_note_source_test.cpp` — **REPLACED BY** `arpeggiator_core_sequencer_test.cpp` (tests the core's Seq mode behavior, including lane 10 inert in Live)
- `plugins/gradus/tests/fixtures/` — **NEW DIRECTORY**: `gradus_v2_preset_{N}.bin` + `gradus_v2_golden_midi_{N}.txt` (one fixture per representative preset)
- `plugins/ruinae/tests/unit/` — **NEW TEST**: `ruinae_byte_identical_post_lane10_test.cpp` (SC-004b — verifies Ruinae preset corpus produces unchanged MIDI after `kNumLanes` bump)

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Completed before plan finalization.*

### Mandatory Searches Performed

**Classes/Structs to be created:**

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| `PianoRollView` (class) | `grep -r "class PianoRoll\\|class.*PianoRoll" plugins/` | No | Create New (`plugins/gradus/src/ui/piano_roll_view.h`) |
| `SourceMode` enum (Live/Sequencer) | `grep -r "enum.*SourceMode" dsp/ plugins/` | No | Add to `arpeggiator_core.h` (lives alongside `ArpRetriggerMode`); 2-value enum |
| `SequencerNoteParams` (struct, lane subset of `ArpeggiatorParams`) | n/a | n/a | Extend existing `ArpeggiatorParams` with new atomics (mirrors how MidiDelay was added). No new struct. |
| ~~`SequencerNoteSource`~~ | — | — | **DROPPED** after grilling pivot — Sequencer Note is now lane 10 inside `ArpeggiatorCore`. |
| ~~`TransposeRootStack`~~ | — | — | **DROPPED** — `HeldNoteBuffer::byInsertOrder()` provides the same data; no separate stack needed. |

**Utility Functions to be created:**

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| `saveSequencerNoteLaneParams` / `loadSequencerNoteLaneParams` | n/a | No | `parameters/arpeggiator_params.h` (alongside existing save/load helpers) | Create New (mirrors `saveArpParams`/`loadArpParams` style) |
| `loadV2LegacyState` (legacy preset loader) | n/a | No | `processor/processor.cpp` (private static helper) | Create New (called when `version == 2` is read from stream) |
| `registerSequencerNoteLaneParams` | n/a | No | `parameters/arpeggiator_params.h` | Create New (mirrors existing per-lane register helpers in `registerArpParams`) |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `ArpLane<uint8_t>` | `dsp/include/krate/dsp/primitives/arp_lane.h` | 1 | Underlies the Sequencer Note pitch lane (32 uint8_t step values) and rest-flag lane (32 uint8_t values, 0=play/1=rest). Already supports Length + Speed + Swing + Jitter + SpeedCurveDepth + Playhead via the same advancement model used by `velocityLane_` / `gateLane_` etc. |
| `HeldNoteBuffer` (insertion-ordered) | `dsp/include/krate/dsp/primitives/held_note_buffer.h` | 1 | Already tracks held notes in insertion order; `byInsertOrder()` gives us the last-played note (back of the span) for free. We read its current state at each step to determine the transposition root — no new "last-played" abstraction needed. |
| `ArpeggiatorCore` | `dsp/include/krate/dsp/processors/arpeggiator_core.h` | 2 | **MODIFIED**: `kNumLanes` 9→10; new lane 10 = Sequencer Note (`ArpLane<uint8_t>` for pitch + parallel `seqRestFlags_[32]` atomic array, MIDI-delay pattern). New `sourceMode_` + `setSourceMode()`. `fireStep` gets a conditional inert branch (skip lane 10 when sourceMode==Live, byte-identical Ruinae preserved) and an early-branch pitch resolution in Seq mode (read lane 10's step pitch + rest flag, skip ArpMode/Octave traversal). All other modulator-lane pipeline (Velocity/Gate/Pitch/Modifier/Ratchet/Condition/Chord/Inversion/MIDI Delay) unchanged. |
| `ArpeggiatorParams` | `plugins/gradus/src/parameters/arpeggiator_params.h` | plugin | Extended with `sourceMode`, `seqNoteLaneLength`, `seqNoteLanePitches[32]`, `seqNoteLaneRestFlags[32]`, `seqNoteLaneSpeed`, `seqNoteLaneSwing`, `seqNoteLaneJitter`, `seqNoteLaneSpeedCurveDepth`. Same atomic-storage pattern as MIDI delay lane. |
| `ArpLane<T>` per-lane modulators (Speed/Swing/Jitter/SpeedCurveDepth) | `arpeggiator_core.h` setters: `setLaneSpeed(idx, ...)`, `setLaneSwing`, `setLaneLengthJitter`, `setLaneSpeedCurveDepth/Table/Enabled` | 2 | **Grilling-pass pivot**: we **DO extend `kNumLanes` from 9 to 10**. Lane 10 = Sequencer Note (`ArpLane<uint8_t>` for pitch storage; rest flags stored separately in a `seqRestFlags_[32]` atomic array per MIDI-delay precedent). Lane 10 is **conditionally inert in Live mode**: `fireStep` short-circuits its advance + modulator reads when `sourceMode_ == Live`. SC-004 (Gradus Live byte-identical) preserved; new SC-004b verifies the same for Ruinae preset corpus. Setters extended `setLaneSpeed(9, ...)`, etc. Storage cost: one extra `ArpLane<uint8_t>` instance per core + 32 atomic bytes. |
| State save/load infrastructure | `Processor::getState`, `Processor::setState`, `saveArpParams`, `loadArpParams` | plugin | Reused. `getState` writes `kCurrentStateVersion = 3` then calls existing `saveArpParams` + new `saveSequencerNoteLaneParams`. `setState` reads version, branches on `version == 2` (legacy loader, leaves Sequencer params at defaults) vs `version >= 3` (full load). |
| Preset save/load | `plugins/shared/src/preset/preset_manager.{h,cpp}` + `plugins/gradus/src/preset/gradus_preset_config.h` | plugin | Reused unchanged — presets travel via the state stream, so new params persist automatically once added to `saveArpParams`/`loadArpParams` and registered with the host. |
| `UIViewSwitchContainer` (template-switch-control) | VSTGUI built-in, used in Iterum's `editor.uidesc` (line 635: `<view class="UIViewSwitchContainer" ... template-switch-control="Mode"/>`) | UI | Reused as the visibility mechanism for the piano roll view. `template-switch-control="ArpSourceMode"` with two templates: `LiveModeContent` (current main area, empty/blank) and `SequencerModeContent` (containing the piano roll). |
| `VisibilityController` (IDependent pattern) | `plugins/iterum/src/controller/visibility_controller.h` | UI | Pattern reference (not direct reuse — VisibilityController is Iterum-local). Gradus's controller already has its own deferred-update pattern (`viewDirtyFlags_` + `syncViewsFromParams()`); we extend it to grey out the FR-022/FR-036 control set when Source = Sequencer. |
| `ArpLane<T>` step parameter binding via `IDependent` / `updateView` | Existing lane editor pattern in `midi_delay_lane_editor.h` + `controller_view_sync.cpp` | UI | Pattern reference — `PianoRollView` will register `IDependent` on the 64 step params + Playhead param via the same mechanism. |
| Right-click / `kRButton` handling in custom views | `midi_delay_lane_editor.h:435-479` (`onMouseDown` + `buttons & kRButton`) | UI | Direct pattern reuse for the rest-flag-set gesture. |
| Per-lane swing/jitter/speed-curve UI patterns (knobs in detail strip) | `plugins/gradus/src/ui/detail_strip.h` + existing per-lane knob arrays in `controller.h` | UI | Pattern reuse for the Sequencer Note lane's modulator knobs (Speed, Swing, Jitter, SpeedCurveDepth). |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities → no conflicts (no overlap with new code)
- [x] `dsp/include/krate/dsp/primitives/` - `arp_lane.h`, `held_note_buffer.h` reused as-is
- [x] `dsp/include/krate/dsp/processors/` - `arpeggiator_core.h` unchanged
- [x] `plugins/gradus/src/plugin_ids.h` - new IDs start at 3741, no collision
- [x] `plugins/gradus/src/parameters/arpeggiator_params.h` - extending existing struct, no new top-level type
- [x] `plugins/gradus/src/ui/` - no existing `PianoRoll*`, `StepGrid*`, `NoteGrid*` symbols (verified by user-provided initial codebase search in spec, plus our own search)
- [x] `plugins/gradus/src/dsp/` - only `audition_voice.h` lives here; no naming conflict (note: `sequencer_note_source.h` was dropped in the grilling-pass pivot — lane 10 lives inside `ArpeggiatorCore`)
- [x] `specs/_architecture_/` - not present in repo root for Gradus arch docs (would be created if it exists; no conflict)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: The new type name `PianoRollView` is unique across `dsp/` and `plugins/`. `SourceMode` enum is added inside `arpeggiator_core.h` (no ODR risk). No standalone `SequencerNoteSource` type exists (grilling-pass pivot). Parameter IDs allocated in a previously unused range (3741-3812 approx). Extensions to `ArpeggiatorParams` are additive atomic members (same pattern proven 8+ times in this struct). State serialization additions are appended after `midiDelayLaneSpeedCurveDepth` and gated by `version == 3` on read. No risk of duplicate-symbol or stream-format collision.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Completed before implementation begins. Prevents compile-time API mismatch errors.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| `ArpLane<T>` (Layer 1) | `setStep(size_t, T)` | `void setStep(size_t step, T value) noexcept` | ✓ (read from `arp_lane.h`) |
| `ArpLane<T>` | `getStep(size_t)` | `[[nodiscard]] T getStep(size_t step) const noexcept` | ✓ |
| `ArpLane<T>` | `setLength(int)` | `void setLength(int len) noexcept` | ✓ |
| `ArpLane<T>` | `currentStep()` | `[[nodiscard]] size_t currentStep() const noexcept` | ✓ |
| `ArpLane<T>` | `advance()` (per-tick advancement via ArpeggiatorCore's per-lane clock) | The Sequencer Note lane is lane 10 inside `ArpeggiatorCore`. Advancement is handled by `advanceLaneBySpeed(seqNoteLane_, 9)` inside `fireStep`, gated by `sourceMode_ == Sequencer` — identical to how lanes 0-8 advance. **No independent per-lane clock runs in Gradus's Processor** (grilling-pass pivot resolved this). | Verified by reading `arpeggiator_core.h:540-548` and `processor.cpp:174-180` (MIDI delay polymetric model) |
| `HeldNoteBuffer` | `byInsertOrder()` | `[[nodiscard]] std::span<const HeldNote> byInsertOrder() const noexcept` | ✓ — last element is the most-recently-played still-held note (FR-017) |
| `HeldNoteBuffer` | `empty()` | `[[nodiscard]] bool empty() const noexcept` | ✓ |
| `HeldNoteBuffer::HeldNote` (struct) | `note`, `velocity` | `uint8_t note; uint8_t velocity; uint16_t insertOrder;` | ✓ |
| `ArpeggiatorCore` | `noteOn(uint8_t, uint8_t)` | `inline void noteOn(uint8_t note, uint8_t velocity) noexcept` | ✓ |
| `ArpeggiatorCore` | `noteOff(uint8_t)` | `inline void noteOff(uint8_t note) noexcept` | ✓ |
| `ArpeggiatorCore` | `processBlock(BlockContext&, std::span<ArpEvent>)` | `inline size_t processBlock(const BlockContext& ctx, std::span<ArpEvent> outputEvents) noexcept` | ✓ |
| `Steinberg::IBStreamer` | `writeInt32` / `readInt32` / `writeFloat` / `readFloat` | All `bool` return (`true` on success) | ✓ — already used throughout `saveArpParams`/`loadArpParams` |
| `VSTGUI::CView::onMouseDown` | onMouseDown override signature | `CMouseEventResult onMouseDown(CPoint& where, const CButtonState& buttons) override` | ✓ (from `midi_delay_lane_editor.h:435`) |
| `VSTGUI::CButtonState` | `kLButton`, `kRButton` | `enum CButton { ..., kLButton = 1 << 1, kRButton = 1 << 2, ... };` | ✓ (VSTGUI standard) |
| `VST3Editor` template-switch-control | `<view class="UIViewSwitchContainer" template-names="A,B" template-switch-control="ParamName"/>` XML | Confirmed working in Iterum (`resources/editor.uidesc:635`) | ✓ |
| `Steinberg::Vst::Parameter::deferUpdate()` | trigger initial UI update | `void deferUpdate()` | ✓ (used in `visibility_controller.h:57`) |
| `Steinberg::Vst::Parameter::addDependent(IDependent*)` | IDependent registration | `void addDependent(IDependent* dep)` | ✓ |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/arp_lane.h` — confirmed `ArpLane<T>` API
- [x] `dsp/include/krate/dsp/primitives/held_note_buffer.h` — confirmed `byInsertOrder()` semantics
- [x] `dsp/include/krate/dsp/processors/arpeggiator_core.h` — confirmed `kNumLanes = 9`, `noteOn`/`noteOff`/`processBlock` signatures
- [x] `plugins/gradus/src/plugin_ids.h` — confirmed next free ID = 3741 (after `kArpMidiDelayPlayheadId = 3740`)
- [x] `plugins/gradus/src/parameters/arpeggiator_params.h` — confirmed atomic storage pattern + register/save/load helpers
- [x] `plugins/gradus/src/processor/processor.cpp` (lines 90-332) — confirmed audio-thread flow, current `setState` is single-version (no migration yet)
- [x] `plugins/gradus/src/controller/controller.h` — confirmed `viewDirtyFlags_`, `viewSyncTimer_`, `kDirtyArpMode` dirty bit pattern → we'll add `kDirtyArpSourceMode` + `kDirtySequencerNoteLane`
- [x] `plugins/iterum/src/controller/visibility_controller.h` — confirmed `IDependent` deferred-update pattern for controls
- [x] `plugins/gradus/src/ui/midi_delay_lane_editor.h` (lines 1-100, 435-479) — confirmed `kRButton` handling + `CViewContainer + IControlListener + IArpLane` composition pattern
- [x] `plugins/iterum/resources/editor.uidesc` (line 635) — confirmed `UIViewSwitchContainer` XML syntax for `template-switch-control`

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| `ArpeggiatorCore::kNumLanes` | Touches ~30 call sites and runs in shared core (Gradus + Ruinae). | **Extend `kNumLanes` 9→10 (grilling-pass decision)**. Lane 10 = Sequencer Note. Conditionally inert in Live mode via `if (sourceMode_ == Live) skip` branch in `fireStep`. Verify Ruinae byte-identical via SC-004b regression test on preset corpus. Reuses polymetric clocking and retrigger logic instead of duplicating them. |
| `setState` legacy migration | Current Gradus `setState` reads version but discards it (`(void)version;` line 306). The v2 stream has no Sequencer params at the end. | Branch on `version`: `if (version >= 3) loadSequencerNoteLaneParams(...)` — for v2, leave new params at struct defaults (rest=1, pitch=60, length=16). This is exactly the EOF-safe pattern already used for v1.5 features in `loadArpParams`. |
| `setParamNormalized` threading | Called on ANY thread (host automation, state load). Cannot touch VSTGUI controls directly. | Use existing `viewDirtyFlags_` atomic + `syncViewsFromParams()` timer pattern. Add `kDirtyArpSourceMode` + `kDirtySequencerNoteLane` flags. |
| `UIViewSwitchContainer` control destruction | Template switching destroys/recreates child controls — cached pointers go dangling. | `PianoRollView` registers `IDependent` in its constructor and unregisters in destructor; do NOT cache its pointer in Controller across mode switches. The IDependent + parameter binding pattern survives template swaps because parameters outlive the view. |
| Right-click on macOS | Some macOS users use Ctrl+Click (no physical right button on some trackpads). | VSTGUI's `CButtonState` normalizes this automatically — `buttons & kRButton` is true for both right-click and Ctrl+left-click on macOS. No special-casing needed. |
| `getState` ordering | Stream is positional; appending new params at the end is the only safe way without breaking v2 reads of v2-vintage streams. v3 readers handle v3-vintage streams (read everything); v3 readers handle v2-vintage streams (EOF after old data → use defaults). v2 readers of v3 streams: not a concern (old Gradus won't be re-installed). | Write order in v3 `getState`: existing 1080+ existing fields, then `sourceMode`, then full Sequencer Note lane block. Append-only. |
| `ArpLane<uint8_t>` for pitches | uint8_t is 0-255 but MIDI pitch range is 0-127; rest flag is 0/1. | Both fit comfortably in `uint8_t`. Stored normalized in atomic via existing `params.seqNoteLaneSteps[i]` pattern. |
| `processParameterChanges` range check | Current range check at line 444-447 handles `kArpBaseId..kArpEndId` + speed curve + delay range. The new IDs at 3741+ need a NEW range check. | Add explicit range check `(id >= kArpSourceModeId && id <= kArpSeqNoteLaneEndId)` before falling through to audition param handling. |
| ~~Polymetric clocking from outside `ArpeggiatorCore`~~ | **Resolved by grilling-pass pivot** — lane 10 is now inside the core, reusing its existing polymetric clocking. No Gradus-local sample counter needed. | N/A — `advanceLaneBySpeed(seqNoteLane_, 9)` is added alongside the existing 8 lane advancements inside `fireStep`, gated by `sourceMode_ == Sequencer`. |
| Source toggle stuck notes | Toggling Source while a note is sounding could orphan the note (note-on emitted, never note-off). | On any `setSourceMode()` call where `oldMode != newMode`, emit note-off for the currently-sounding note (panic) before switching. Lane playheads are NOT reset (Q5-A); the new source picks up at the current playhead position. |
| Held-note routing in Sequencer mode | Could route held notes to a separate transposition stack to keep `HeldNoteBuffer` clean; but that loses chord-lane voicing context. | Held notes always populate `HeldNoteBuffer` (single source of truth). In Seq mode, the ArpMode/Octave traversal is skipped in `fireStep`, but the buffer still serves transposition root (`back()` of `byInsertOrder()`), base velocity, and chord-lane voicing. |
| Base velocity sourcing in Sequencer mode | The pattern has no per-step velocity. fireStep needs a velocity to emit. | In Seq-mode `fireStep`: read `heldNotes_.byInsertOrder().back().velocity` if `!heldNotes_.empty()`, else use constant `100`. Then Velocity lane scales as normal. |
| Lane 10 retrigger behavior | Retrigger=Note resets all lane playheads on note-on. Lane 10 must be included. | Existing retrigger code in `fireStep` already iterates lanes via index. Extend the iteration upper bound from 9 to 10. No structural change. |
| v2 binary-fixture generation | Need real v2 state bytes; can't synthesize after the version bump. | Generate fixtures on the parent commit before `kCurrentStateVersion` bumps. Commit them to `plugins/gradus/tests/fixtures/gradus_v2_preset_{N}.bin`. Pair with golden MIDI captured from running the v2 binary against a fixed input sequence. |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| (none) | Held-note stack semantics are already encapsulated in Layer 1 `HeldNoteBuffer::byInsertOrder()`. No new pure utility emerges from this feature. | — | — |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| (No equivalent function) | The step-pitch read + rest-flag check logic lives inside `ArpeggiatorCore::fireStep` after the grilling-pass pivot. Not a standalone member function — inlined into the core's Sequencer-mode branch. |
| `Processor::loadV2LegacyState()` | Legacy migration is a Processor concern; no reuse value. |
| `PianoRollView::stepFromX` / `pitchFromY` helpers | UI geometry only; no DSP value. |

**Decision**: Nothing extracted to Layer 0. This feature is fundamentally plugin-local (Gradus-specific source-mode switch). The Sequencer Note lane lives natively inside `ArpeggiatorCore` (lane 10, grilling-pass pivot) — there is no standalone `SequencerNoteSource` DSP component to promote. If Ruinae ever wants a sequencer source mode, the relevant `ArpeggiatorCore` extensions could be exposed via a shared API at that time — but not pre-emptively per project's "duplicate twice before extracting" guidance.

## SIMD Optimization Analysis

*GATE: Constitution Principle IV.*

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | NO | Step lookup is stateless per call; the only "state" is the playhead position, which is integer-incremented |
| **Data parallelism width** | 1 (monophonic) | One pitch + one rest flag emitted per step. No parallel voices. |
| **Branch density in inner loop** | LOW | Single `if (restFlag)` per step boundary. No tight inner loop — this is event-driven, not sample-by-sample. |
| **Dominant operations** | integer arithmetic + atomic load | Step lookup is `params.seqNoteLanePitches[step].load()` — cycle-counted in tens of cycles, not thousands. |
| **Current CPU budget vs expected usage** | Layer 2 budget < 0.5%; expected usage well under 0.1% | Step boundaries fire at most a few times per audio block (at musical-step rate, ~1/4 to 1/32 of buffer); per-block cost is negligible. |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: Sequencer note-source logic is fundamentally event-driven (one event per musical step, not per audio sample) and monophonic (single pitch per step). There is no inner-loop parallelism to exploit. Expected CPU well under 0.1% — there is no performance problem to optimize. SIMD lanes would be 100% wasted.

### Implementation Workflow

(Skipped — verdict is NOT BENEFICIAL.)

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Cache step lookup inside per-block scope to avoid repeated atomic loads | ~0% (already negligible) | LOW | NO — premature |
| Early-out when all steps are rest (avoid noteOn loop iteration) | Tiny — only saves cycles when nothing fires | LOW | NO — already optimal |

**No optimizations needed.** Correctness is the priority.

## Higher-Layer Reusability Analysis

*Forward-looking analysis: What code from THIS feature could be reused by SIBLING features at the same layer?*

### Sibling Features Analysis

**This feature's layer**: Plugin-level feature (Layer 4-equivalent for Gradus's plugin code, plus a Layer 2 DSP component in `plugins/gradus/src/dsp/`).

**Related features at same layer**:
- **Ruinae arpeggiator**: shares the 3000-3372 arp param block with Gradus. If Ruinae ever grows a sequencer source mode, the params at 3741+ would need to be lifted into shared territory (Ruinae cannot use Gradus-exclusive IDs as-is). Spec explicitly defers this — Gradus is the sole consumer of `kArpSourceModeId` and the Sequencer Note lane params.
- **Iterum / Disrumpo step sequencers** (hypothetical): could reuse the piano-roll view if either ever grows a step-sequencer panel.

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| `PianoRollView` (VSTGUI custom view) | MEDIUM | Hypothetical Ruinae sequencer mode, hypothetical Iterum/Disrumpo step sequencers | Keep local in `plugins/gradus/src/ui/`. Promote to `plugins/shared/src/ui/` only when a second consumer appears (project's "duplicate twice before extracting" guidance). |
| ~~`SequencerNoteSource`~~ (DROPPED — grilling-pass pivot) | N/A | N/A | Not applicable — the Sequencer Note lane lives natively inside `ArpeggiatorCore` (lane 10); there is no standalone `SequencerNoteSource` component. |
| Last-played-note tracking | LOW — already lives in Layer 1 `HeldNoteBuffer::byInsertOrder()` | n/a | Already shared at the right layer (KrateDSP). |
| State-versioning migration pattern (read version, branch on legacy loader) | MEDIUM — Gradus is the first multi-version Gradus state stream; the pattern could inform other plugins' future state migrations. | Possibly other plugins | Document the pattern in code comments + write a clear unit test (FR-039b) that other plugins can crib from. No abstract base class needed (over-engineering). |

### Detailed Analysis (for HIGH potential items)

(No HIGH-potential items.)

### Decision Log

| Decision | Rationale |
|----------|-----------|
| `PianoRollView` stays local in Gradus | Only one consumer (Gradus); promotion is cheap when a second consumer appears |
| ~~`SequencerNoteSource`~~ does NOT exist (grilling-pass pivot) | Lane 10 inside `ArpeggiatorCore` is the implementation; no separate `SequencerNoteSource` component exists or should be created |
| State-migration logic stays in `processor.cpp` private functions | One-off concern; abstraction would be premature |
| ~~`kNumLanes = 9` NOT extended to 10~~ | **REVERSED by grilling-pass pivot.** `kNumLanes` extended 9→10. Lane 10 conditionally inert in Live mode. Reuses polymetric infrastructure; avoids duplicate clocking/retrigger logic; SC-004 (Gradus Live byte-identical) preserved via the inert branch; SC-004b (Ruinae preset corpus byte-identical) added to verify Ruinae unaffected. |

### Review Trigger

After implementing a hypothetical **Ruinae sequencer-source mode** OR a second step-sequencer panel in any plugin:
- [ ] Does the consumer need `PianoRollView` or similar? → Promote to `plugins/shared/src/ui/`
- [ ] Does the consumer use the same `SequencerNoteSource` shape? → Promote to `plugins/shared/src/sequencer/` or KrateDSP if it generalizes
- [ ] Any duplicated state-versioning code? → Consider a tiny `loadVersionedState(stream, version, legacyLoaderFn, currentLoaderFn)` helper

## Project Structure

### Documentation (this feature)

```text
specs/142-gradus-piano-roll-sequencer/
├── plan.md              # This file (/speckit.plan output)
├── spec.md              # Feature specification (already exists)
├── research.md          # Phase 0 output (this command)
├── data-model.md        # Phase 1 output (this command)
├── quickstart.md        # Phase 1 output (this command)
├── contracts/           # Phase 1 output (this command)
│   ├── param-ids.md     # Full param ID assignments at 3741+
│   ├── state-stream-v3.md   # Binary stream layout for v3 + v2 legacy
│   └── piano-roll-view.md   # Public API of PianoRollView (custom view contract)
└── tasks.md             # Phase 2 output (/speckit.tasks — NOT created here)
```

### Source Code (repository root)

```text
plugins/gradus/
├── src/
│   ├── plugin_ids.h                          # MODIFIED: add kArpSourceModeId (3741),
│   │                                         #   kArpSequencerNoteLaneLengthId (3742),
│   │                                         #   kArpSequencerNoteLaneStep0Id..Step31Id (3743..3774) (pitches),
│   │                                         #   kArpSequencerNoteLaneRestStep0Id..Step31Id (3775..3806) (rest flags),
│   │                                         #   kArpSequencerNoteLaneSpeedId (3807),
│   │                                         #   kArpSequencerNoteLaneSwingId (3808),
│   │                                         #   kArpSequencerNoteLaneJitterId (3809),
│   │                                         #   kArpSequencerNoteLaneSpeedCurveDepthId (3810),
│   │                                         #   kArpSequencerNoteLanePlayheadId (3811),
│   │                                         #   kArpSequencerNoteLaneEndId = 3811;
│   │                                         #   bump kCurrentStateVersion from 2 to 3
│   ├── parameters/
│   │   └── arpeggiator_params.h              # MODIFIED: add atomics for new params,
│   │                                         #   handleArpParamChange cases,
│   │                                         #   registerSequencerNoteLaneParams(),
│   │                                         #   saveSequencerNoteLaneParams(),
│   │                                         #   loadSequencerNoteLaneParams()
│   ├── dsp/
│   │   └── audition_voice.h                  # unchanged
│   │                                         # NOTE: sequencer_note_source.h DROPPED — Sequencer Note
│   │                                         #   is now lane 10 inside ArpeggiatorCore (grilling-pass pivot).
│   ├── processor/
│   │   ├── processor.h                       # MODIFIED: add std::atomic<int> sourceMode_; expose
│   │   │                                     #   setter that calls arpCore_.setSourceMode() under
│   │   │                                     #   a clean note-off panic.
│   │   └── processor.cpp                     # MODIFIED:
│   │                                         #   - process(): held-note input populates HeldNoteBuffer
│   │                                         #     in both modes (no input-side branch).
│   │                                         #   - On sourceMode_ change: emit note-off for any sounding
│   │                                         #     note via arpCore_'s panic path, then call
│   │                                         #     arpCore_.setSourceMode(new). Lane playheads unchanged.
│   │                                         #   - getState: bump version to 3, append Sequencer block
│   │                                         #     (source mode + 70 lane params)
│   │                                         #   - setState: branch on version (v2 legacy path leaves
│   │                                         #     Source=Live and all rest flags at default 1)
│   │                                         #   - All step firing / pitch resolution / retrigger /
│   │                                         #     polymetric clocking lives inside ArpeggiatorCore now —
│   │                                         #     no Gradus-local sample counter needed (pivot from prior plan).
│   ├── controller/
│   │   ├── controller.h                      # MODIFIED: add kDirtyArpSourceMode + kDirtySequencerNoteLane
│   │   │                                     #   to DirtyFlags enum; add pianoRollView_ pointer; add
│   │   │                                     #   visibility/disable wiring for FR-022/FR-036 control set
│   │   ├── controller.cpp                    # MODIFIED:
│   │   │                                     #   - initialize(): register new params
│   │   │                                     #   - setParamNormalized: handle new IDs, set dirty flags
│   │   │                                     #   - syncViewsFromParams: handle new dirty flags
│   │   ├── controller_arp.cpp                # MODIFIED: lane construction + IDependent wiring for new lane
│   │   ├── controller_view_sync.cpp          # MODIFIED: piano roll re-sync; disable/enable rules for
│   │   │                                     #   FR-022/FR-036 control set when sourceMode changes
│   │   ├── controller_verify_view.cpp        # MODIFIED: capture PianoRollView pointer in verifyView
│   │   └── controller_presets.cpp            # MODIFIED: persist/restore sequencer lane in preset path
│   │                                         #   (uses existing state-streaming infrastructure → minimal change)
│   ├── ui/
│   │   ├── ring_data_bridge.h                # unchanged
│   │   ├── ring_display.h                    # unchanged (FR-035: ring view untouched)
│   │   ├── piano_roll_view.h                 # NEW: VSTGUI CView subclass
│   │   │                                     #   class PianoRollView : public CView,
│   │   │                                     #                          public IDependent {
│   │   │                                     #     - bind(controller, baseStepParamIds, restFlagBaseId, lengthParamId,
│   │   │                                     #            playheadParamId);
│   │   │                                     #     - draw(): grid + notes + playhead column
│   │   │                                     #     - onMouseDown/onMouseMoved/onMouseUp:
│   │                                     #       click → toggle/replace; right-click → set rest;
│   │   │                                     #       drag → lock-to-start-pitch paint
│   │   │                                     #     - update(IDependent): UI-thread refresh on param change
│   │   │                                     #     - stepFromX / pitchFromY geometry helpers
│   │   │                                     #   }
│   │   └── piano_roll_view.cpp               # (only if non-template-heavy code is extracted; otherwise
│   │                                         #   header-only is acceptable per existing UI conventions)
│   ├── preset/
│   │   └── gradus_preset_config.h            # MODIFIED: bump version, ensure new lane fields included in
│   │                                         #   default-preset generation (if any) — likely zero-change
│   │                                         #   because presets use the state stream verbatim
│   └── entry.cpp / version.h                 # MODIFIED only for version bump if shipping as 1.8.0
├── resources/
│   └── editor.uidesc                         # MODIFIED:
│                                             #   - add control-tag for ArpSourceMode (3741)
│                                             #   - add UIViewSwitchContainer (template-switch-control="ArpSourceMode")
│                                             #     wrapping the main work area, with two templates:
│                                             #     LiveModeContent (existing arp lanes layout) and
│                                             #     SequencerModeContent (which contains the new PianoRollView
│                                             #     ALONGSIDE the same arp lanes — because lanes still
│                                             #     post-process in both modes)
│                                             #   - actually: simpler approach — the piano roll is added as
│                                             #     a NEW view that is visible/hidden based on sourceMode_
│                                             #     via a smaller UIViewSwitchContainer placed in the layout
│                                             #     where the piano roll belongs; existing lanes/main area
│                                             #     remain visible in both modes (since lanes still apply
│                                             #     in Sequencer mode per FR-020). See research.md for the
│                                             #     final UI layout decision.
│                                             #   - add Sequencer Note lane modulator knobs in detail strip
│                                             #     (when Sequencer Note lane is the selected lane)
│                                             #   - register new control-tags for the 70 new params
└── tests/
    ├── CMakeLists.txt                        # MODIFIED: add new test sources below
    ├── fixtures/                             # NEW DIRECTORY
    │   ├── gradus_v2_preset_default.bin      # NEW: real v2 state bytes, generated on parent commit
    │   ├── gradus_v2_preset_heavy_lanes.bin  # NEW: same, with all modulator lanes populated
    │   ├── gradus_v2_preset_midi_delay.bin   # NEW: same, with MIDI delay lane active
    │   ├── gradus_v2_golden_midi_default.txt # NEW: golden MIDI events from v2 binary on fixed input
    │   ├── gradus_v2_golden_midi_heavy.txt   # NEW
    │   └── gradus_v2_golden_midi_delay.txt   # NEW
    └── unit/
        ├── processor/
        │   ├── arpeggiator_core_sequencer_test.cpp  # NEW: core-level test of lane 10 semantics —
        │   │                                          #   Live mode inert (skip lane 10), Seq mode pitch
        │   │                                          #   resolution, rest-skip behavior, retrigger lane 10
        │   │                                          #   playhead reset.
        │   ├── source_mode_transpose_test.cpp     # NEW: held-note transpose + last-played semantics
        │   │                                          #   (FR-016/017/018) — drives the integrated processor
        │   ├── source_mode_toggle_test.cpp        # NEW: mid-playback toggle, no stuck notes, playheads
        │   │                                          #   unchanged across toggle (FR-025, SC-007)
        │   ├── live_mode_byte_identical_test.cpp  # NEW: v2 fixture → byte-identical Gradus Live MIDI
        │   │                                          #   (SC-004, FR-039b) using golden MIDI files
        │   ├── sequencer_polymetric_test.cpp      # NEW: lane 10 length≠other lane lengths, polyrhythmic
        │   │                                          #   verification (FR-025b)
        │   └── sequencer_rests_advance_test.cpp   # NEW: all-rest pattern, playhead advances, zero
        │                                              #   note-ons (SC-008)
        ├── vst/
        │   ├── gradus_vst_tests.cpp               # MODIFIED: add new param coverage (3741-3811)
        │   └── state_v2_v3_migration_test.cpp     # NEW: load each v2 fixture via v3 setState; assert
        │                                              #   Source=Live, all rest=1, all pitch=60, Length=16
        │                                              #   (FR-039a)
        └── ui/
            ├── piano_roll_view_test.cpp           # NEW: click/drag/right-click semantics
            │                                          #   (FR-030/031/032 — uses a mock controller)
            ├── piano_roll_visibility_test.cpp     # NEW: visibility toggles with source mode (FR-027)
            └── piano_roll_playhead_test.cpp       # NEW: playhead cursor follows Playhead param (FR-034a)

plugins/ruinae/tests/unit/
└── ruinae_byte_identical_post_lane10_test.cpp     # NEW: SC-004b — load all shipped Ruinae factory
                                                     #   presets and assert MIDI output is byte-identical
                                                     #   to a pre-bump golden capture (proves lane 10's
                                                     #   conditional inert branch doesn't perturb Ruinae)

dsp/
└── (unchanged — no new DSP files in shared KrateDSP for this feature)
```

**Structure Decision**: Single-plugin (Gradus) feature. All code lives under `plugins/gradus/`. One new VSTGUI view in `plugins/gradus/src/ui/piano_roll_view.h`. The Sequencer Note lane is implemented as lane 10 inside `ArpeggiatorCore` (grilling-pass pivot) — there is NO `sequencer_note_source.h` Layer 2 component. Existing `dsp/include/krate/dsp/` libraries are reused unchanged. New tests added to existing `gradus_tests` CMake target (no new test executable).

## Complexity Tracking

> No Constitution violations to justify. This section is empty by design.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| — | — | — |
