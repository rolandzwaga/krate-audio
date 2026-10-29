# Freeze: DSP, state and UI consequences of each capture model

Scope: what each candidate costs against the code as it stands on branch `ui/iterum-consistency` (working tree of
2026-10-29). Nothing here is a recommendation. Evidence is `file:line` or the probe output in
`specs/iterum-freeze/probe/before.txt`. "By reading" means I traced the code and did not run it; the probe extension
that would confirm it is named in each case.

## 0. Findings about the current signal path (they set the baseline every option is measured against)

### HIGH

**F1. The wet path is a 200 ms-late copy of the input, mixed at 50 % with the undelayed input, so a steady input sounds
unchanged.**
- Euclidean cuts every slice with offset 0 (`pattern_freeze_mode.h:675-683`: `offset` stays 0 unless GranularScatter),
  i.e. the newest `sliceLengthMs_` ending at the trigger, and plays it forward from the trigger. `extractSlice` anchors
  from the end of the slice (`rolling_capture_buffer.h:161-162`). The wet is therefore what the input did 0-200 ms
  earlier: an echo, not a freeze. Probe: Euclidean best lag 174.8 ms, r = 0.69 against the input.
- The input is never muted. The class comment (`:62-76`, "Input (muted)") is false: the final mix is
  `in*(1-w) + wet*w` (`:589-590`) with w = 0.5 by default (`freeze_params.h:30`, registration default 0.5 at `:271`;
  DSP `kDefaultDryWetMix` 50, `:99`). Dry stays at 50 % whatever happens.
- Probe, 50 % mix, 120 BPM, 220 Hz tone stepping an octave each second: wet is -10.1 dB re input, active in 59 % of
  10 ms windows (Euclidean). On sustained material (pads, a held note, a loop) the late copy is the same signal, so the
  user hears "only the input". There is no state in which the output differs from "input plus an echo".
- Evidence: `before.txt` lines 1-2; code lines above.

**F2. There is no freeze action at all. The mode is named Freeze and has no freeze.**
- `Processor` calls `setFreezeEnabled(true)` every block (`processor.cpp:767`); `freezeEnabled_` is true after
  `reset()` (`pattern_freeze_mode.h:214`). The capture buffer keeps writing every sample (`:530-532`), so nothing is
  ever held. Every parameter that suggests holding (Slice Length, Slice Mode, the four variants) acts on a buffer that
  is replaced as fast as it is read.
- Consequence for the options: without a hold, "slice selection per step" has nothing stable to select from.

**F3. Harmonic Drones and Noise Bursts read the rolling buffer by age while the write head moves one block at a time,
so they play a block-sized reversed fragment instead of the captured material (by reading; probe agrees: Drones
r = 0.03, Noise Bursts r = 0.05, no lag correlates).**
- `process()` writes the whole block first (`:530-532`), then the pattern code runs. Drones read
  `extractSlice(&l, &r, 1, readIdx)` with `readIdx = fmod(droneSlicePos_ * ratio, len)` (`:931-936`); Noise Bursts read
  `extractSlice(..., noiseBurstSliceOffset_ + noiseBurstPosition_)` (`:1023`). Both pass the position as an AGE
  (samples before the newest). Age grows by 1 per output sample while the head stays still inside the block and jumps by N
  between blocks, so the absolute index is `W - 1 - pos`, which is constant at each block start (W and pos both advance N)
  and runs BACKWARD inside the block. Voice 0 of a Drone (ratio 1) therefore repeats the same N-sample fragment,
  reversed, until `droneSlicePos_` wraps at the slice length (8820 samples at 44.1 kHz is not a multiple of 512, so the
  fragment shifts at each wrap). Noise Bursts do the same for the 200 ms of each burst.
- This is block-size dependent (a 64-sample host buzzes at 690 Hz, a 512-sample host at 86 Hz).
- Not a "pitch shift" either: `pitchRatio` only scales the age step (`:931`); there is no interpolation (`size_t`
  truncation at `:932`), no loop crossfade (the envelope at `:939` shapes the loop seam but the read is not forward), and the
  `Octave` interval default means voice 1 reads at 2x age rate.
- To confirm: extend `PROBE Freeze signal path at defaults` to render Drones at block 64 and 512 and print the wet
  spectrum peak; the peak should move with the block size.

### MEDIUM

**F4. `Slice Mode` (Fixed / Variable) does nothing.** `sliceMode_` is stored and returned (`:327-328`, `:1150`) and read
nowhere. Factory preset "Granular Cloud" and "Scatter Drift" set it to Variable. Parameter 1017 is a dead control.

**F5. The host's musical position never reaches Freeze, and neither does the time signature.** The processor builds the
`BlockContext` with only `sampleRate`, `blockSize`, `tempoBPM`, `isPlaying` (`processor.cpp:206-210`).
`projectTimeMusic`, `projectTimeMusicValid`, `transportPositionSamples`, `timeSignatureNumerator/Denominator` keep their
defaults (0, false, 0, 4/4). The scheduler is a free-running sample counter that restarts at step 0 on every `reset()`
(`pattern_scheduler.h:131-135`, `:240`), so the Euclidean pattern is not phased to the bar and drifts against the song
whenever the mode is re-entered. Option B cannot work until the processor fills these fields. The tempo fallback is 120
BPM when `kTempoValid` is absent (`processor.cpp:196-200`), so `tempoValid_` in the DSP (`:539-545`) can never be false in
the plugin and its "stop when tempo is lost" branch is unreachable there.

**F6. Switching to Freeze starts with an empty buffer.** `patternFreezeMode_.process` runs only while the mode is
Freeze (case at `processor.cpp:765`), and `reset()` on mode change clears the buffer (`:846`, `:181-186`). The wet is
silent for `kMinReadyBufferMs` = 200 ms and a slice of length L needs L ms of history (`:641`, `:888`, `:975`), up to
2 s at the maximum slice length. A momentary or latched hold pressed in that window has nothing to hold.

**F7. The pattern-type crossfade replays one stale block.** `setPatternType` copies `outputL_` once into
`crossfadeOutL_` (`:279-282`); `processCrossfade` then mixes that single saved block against every block of the next
500 ms (`:1086-1089`). The old pattern is not continued; its last block loops for 500 ms (by reading).

**F8. Per-block work on the audio thread that only needs to happen on change.** `setEnvelopeShape` regenerates the
512-entry window every block (`processor.cpp:816-817`, `pattern_freeze_mode.h:348-355`); `setEuclidean*` regenerate the
bit pattern three times per block (`:296-314`). Cheap, but it is a trig table rebuild per block.

### LOW

**F9. Old class `FreezeMode` (`effects/freeze_mode.h`) is not used by Iterum.** Only `dsp/CMakeLists.txt:236`,
`dsp/lint_all_headers.cpp:207` and `dsp/tests/unit/effects/freeze_mode_test.cpp:15` reference it. Nothing in
`plugins/` includes it. It is an infinite-sustain feedback network (`setFreezeEnabled` forwards to a feedback network,
`:675-676`); it is a source of ideas for a "hold" voice, not a dependency.

**F10. Gain compensation steps per block.** Euclidean/Granular compute `1/sqrt(activeSliceCount_)` once per block from the
count at block start (`:724`, `:768`), so a slice that starts mid-block is added at full gain and the next block steps.

**F11. Approval goldens do not cover Freeze.** `plugins/iterum/tests/approval/approved/` holds three files
(OnePoleSmoother, applyGain, softClip); `approval/test_approvals_main.cpp` names no Freeze case; `tests/CMakeLists.txt`
mentions Freeze only for the param test and the (untracked) screenshot test. A Freeze DSP change cannot change a golden;
nothing needs re-approving. The Freeze regression nets are `test_pattern_freeze_mode.cpp` (23 cases), the Iterum
parameter/panel tests and the factory presets.

## 1. Pieces every option shares (priced once, referenced below as S1-S5)

**S1. Parameter plumbing for one new parameter** (IDs from 1070; 1070-1099 free, `kFreezeEndId` = 1099 so
`processor.cpp:483` and the controller range check at `controller.cpp:470` already cover them):
`plugin_ids.h` (id), `freeze_params.h` (atomic in `FreezeParams`, case in `handleFreezeParamChange`, registration in
`registerFreezeParams`, text in `formatFreezeParam`, save + load tail, `syncFreezeParamsToController` tail,
`loadFreezeParamsToController` tail), `processor.cpp` (push at the Freeze case `:765-822`; two lines in
`getState`/`setState` after `saveMultiTapLengthParams` `:353` / `:388`), `controller.cpp` (two lines after
`syncMultiTapLengthParamsToController` `:301` and `loadMultiTapLengthParamsToController` `:1387`). 7 source files per
batch of new parameters; adding a second parameter in the same batch costs about 1 extra function body per file, not
extra files.

**S2. State tail rule.** The stream has no version field; the last field today is the MultiTap Custom Length int
(`multitap_params.h:723-735`) after the Tape sync pair (`tape_params.h:515-528`). A Freeze tail goes after that. The
loader pattern already in the tree: first `readInt32` failing means "saved before this existed" and stores the LEGACY
value, which may differ from the new registered default (Tape sync: default on, old state loads Free,
`tape_params.h:520-528`). For Freeze the legacy value must be "behaves exactly as today" (rolling capture, no hold).
Order hazard: tails are positional, so a later feature must append after the Freeze tail; the factory presets written
by `tools/preset_generator.cpp:476-495` (`buildComponentState`) write none of the tails, so every shipped preset loads
the legacy value of every tail field (this is the cause of the 0.15.1 "missing Digital fields" fix, commit b8ee8fb6).
If the designer wants a factory preset to set a new parameter the generator must write the pack through the Tape and
MultiTap tails first (it writes neither today), or the tail bytes land in the wrong fields.

**S3. Panel room.** `panels/freeze.md` 2.2: SLICE (12, 96, 284 x 180) holds only Slice Mode at y = 24; "rows 76 and 128 of
SLICE are empty (bordered space, principle 6)". A segment cell (label + 24 pt segment) needs 44 pt, a slider cell 42 pt,
a toggle/button cell about 24-44 pt: two cells fit at y = 76 and y = 128 with no layout change. The core strip is full
(Slice Length, Pattern, Mix, 3 x 284). Going further than two cells means either relabelling the SLICE fieldset (e.g.
"CAPTURE") or a new fieldset, which breaks the "6 FieldsetContainers" structure count of panels/freeze.md section 4 and
the `editor_panels_test.cpp` Freeze structure case (`:417-444`). XSLT: `specs/iterum-ui-redesign/xslt/freeze.xsl`
(235 lines) with `cells.xsl`; every new parameter also needs a `control-tag` entry and a control (rule: every parameter
keeps one).

**S4. Test harness.** A `dsp_effects_tests` case can set `BlockContext{.projectTimeMusic, .projectTimeMusicValid,
.isPlaying, .timeSignatureNumerator ...}` directly (fields exist, `block_context.h:56-67`). A `plugin_tests` processor
case needs the processor to fill them from `ProcessContext` first (F5); then `multitap_tempo_output_test.cpp`'s
`ProcessorRig` (heap-allocated `Processor`, `initialize`, `setupProcessing`, `setActive`, `HostProcessData`, a
`ProcessContext` it already owns as `context_`) can set `projectTimeMusic`, `state |= kProjectTimeMusicValid | kPlaying`
and `timeSigNumerator`. Probe: the hidden `PROBE Freeze signal path at defaults` (`test_pattern_freeze_mode.cpp:924`)
already prints wet level, active fraction and best lag per pattern type and transport state; extending it to hold
states needs no new instrument. The `playing=0/1` rows are identical today (F5), which is itself a measurement.

**S5. Tests that will need edits whatever is chosen.** `freeze_params_test.cpp` (id list, defaults, round trip),
`editor_panels_test.cpp` (Freeze controls), `readout_format_test.cpp` (formatter), `preset_*` tests that enumerate Freeze
presets (`preset_manager_test.cpp:30`), the 23 DSP cases of which "outputs audio when freeze enabled" (`:324`),
"mix parameters" (`:280`), "captures incoming audio" (`:203`), "Harmonic Drones produces output" (`:510`),
"Noise Bursts requires captured audio content" (`:589`) and "pattern crossfade" (`:652`) assert behaviour the hold
changes; `mix_scale_consistency_test.cpp:99`; the untracked `freeze_screenshot_test.cpp`. CHANGELOG bullets go in the open
1.0.0 section; `specs/iterum-ui-redesign/design-system.md` 7.10 and `panels/freeze.md` need an updated mapping table.

## 2. Hold mechanics shared by A, B, C, D (DSP)

Three ways to hold, each with its own cost.

| Hold method | Memory | Audio-thread cost | What breaks |
|---|---|---|---|
| H1. Stop writing (`captureBuffer_.writeStereo` skipped while held) | 0 | 0 | the dry input is never read from the buffer, so dry stays live: good. Auto recapture (B) cannot use H1 alone (needs to record the next window while playing the held one). Held content is whatever the ring holds: up to 5 s (262 144 samples/ch at 48 kHz; rounded to a power of two, 1 048 576 at 192 kHz). |
| H2. Snapshot into a second buffer of the same size on the hold edge | +1 stereo buffer: 2 MB at 48 kHz, 8 MB at 192 kHz (float, 2 ch, power-of-two capacity), allocated in `prepare()` (cap: `RollingCaptureBuffer::prepare` already allocates the live one) | one memcpy of up to 2 MB at the edge (about 0.1-0.3 ms; can be amortised over blocks) | the live ring keeps recording, so release can resume instantly, and B can capture the next window while the old one plays. |
| H3. Snapshot only the last N bars / N ms into a small buffer | N * stereo | small copy | needs the capture length from D; the loop is the same length as the snapshot, so wrap/crossfade is exact. |

Common requirements whatever the hold method:
- **Forward, frozen-geometry reads for Drones and Noise Bursts.** Today's age-based reads (F3) are only meaningful if the
  head is stationary AND the code converts position to age as `age = len - 1 - pos` (forward). With H1/H2/H3 the head is
  stationary, so the fix is local: replace the age index at `:931-936` and `:1023` with a forward read of the held buffer
  (`readStereoLinear`-style interpolation exists: `rolling_capture_buffer.h:353`, with the `extractSlice` identity
  `out[i] == readStereoLinear(O + L - 1 - i)` documented at `:363`).
- **Real pitch shift for Drones.** Two ways, both preallocated in `prepare()`:
  (i) varispeed read of the held loop: `readPos += ratio` per sample with linear interpolation and a wrap crossfade.
  Zero extra memory, no latency, the pitch and speed change together, which on a looping drone is inaudible as a speed
  change. This is what `droneSlicePos_ * pitchRatio` was trying to be. Aliasing for ratio 2 (Octave default) unless the
  source is band-limited: a 2x read folds content above fs/4.
  (ii) `PitchShiftProcessor` (what Shimmer uses: `shimmer_delay.h:73-74,88-89`, one instance per channel; default mode
  `PitchSync`, `shimmer_delay.h:523`). Cost: voices 1..3 each need a shifter pair, so up to 6 `PitchShiftProcessor`
  instances (Simple 0 latency but audible artifacts; PitchSync 5-10 ms; Granular ~46 ms; PhaseVocoder ~116 ms plus FFT
  frames). Latency of a looped, held source does not matter, so PhaseVocoder quality is affordable in latency but the
  CPU/memory of 6 STFTs is not small; the Freeze mode CPU budget is not stated anywhere in the spec 069 headers.
  Whichever is chosen, the drift LFO (`:916-927`) maps to `setCents` (ii) or to ratio modulation (i).
- **Slice selection per step** (needed as soon as the buffer is held, otherwise each Euclidean hit replays the same
  `offset 0` slice, F1/F2): options are (a) sequential walk, `offset = (hitIndex * L) mod heldLen`; (b) the step index
  chooses the slice (`onPatternTrigger(int step)` already receives `step`, ignored at `:639`), which makes Rotation and
  Hits audible as a rhythm over the content; (c) random as GranularScatter does. `Slice Mode` (F4) is the natural switch:
  Fixed = same slice every hit, Variable = walk. No extra memory (slices are copied into the preallocated `SlicePool`,
  8 slices x 2 s).
- **Dry/wet and the hold** (Section 5).
- **Gate for empty captures**: holding a buffer that is silent (F6, or a silent bar) yields silence. Every option needs a
  rule for "nothing worth holding yet" (do not accept a hold edge until `isReady(minLength)` and a level over a floor).

## 3. The four capture models

### Option A: Freeze trigger parameter that snapshots the rolling buffer

Variants: A1 latched toggle, A2 momentary, A3 both (one parameter, two UI behaviours).

- **Parameters.** One boolean (id e.g. 1070, 2 states, `kCanAutomate`). DSP behaviour of A1 and A2 is identical (the
  parameter value is the hold); the difference is the control: a latching `COnOffButton` versus a press-and-hold
  `CKickButton`-style control. A3 needs either two controls bound to one tag (not permitted by the one-control-per-parameter
  convention, the editor structure test counts controls) or a second parameter (a Mode enum, which is Option D).
- **DSP.** H1 or H2 on the edge; edge handling: on press, require `isReady`; on release, either resume writing
  (H1: the ring continues; the stale tail of the held block remains in the ring) or discard the snapshot (H2). Slices
  already in flight keep playing (they are copies, `onPatternTrigger` copies into the pool) and Drones/Noise Bursts
  switch to the forward held read. The wet must not stop when released: either fade out the pattern over the release time
  or revert to the legacy rolling echo (which is today's behaviour, F1).
- **Scheduler.** No change required; the free-running counter is untouched. Optional: reset to step 0 on the press edge
  so the first hit lands on the press (a phase-to-press, not phase-to-bar).
- **Plugin.** S1 for one parameter; the control needs a place (S3: SLICE rows 76 or 128, or the SLICE title becomes
  "CAPTURE"). A MIDI/DAW-mapped momentary is just the host automating the 0/1 value.
- **State.** Tail of one int. **Do not persist the hold as a live state**: the 5 s buffer is not in the stream, so a
  restored `hold = 1` would hold an empty ring (F6) and the user would hear silence. Options: save it and load it as 0, or
  not save it at all (then no tail is needed and S2 shrinks to nothing; the parameter would be a pure
  controller-to-processor parameter that the host automates). Old preset loads as: hold off = exactly today's sound.
- **Presets.** None needs to change (hold off = legacy). Factory presets cannot "start frozen" for the reason above.
- **Tests.** DSP: hold edge freezes content (feed a ramp then silence, assert the wet still carries the ramp's spectrum 2 s
  later; this case does not exist), release resumes, hold before ready is ignored. Processor: round trip. Panel: new control.
- **Files touched.** About 12: `pattern_freeze_mode.h`, `plugin_ids.h`, `freeze_params.h`, `processor.cpp`,
  `controller.cpp` (only if persisted), `editor.uidesc` via `freeze.xsl` (+ the Freeze `control-tag`), `freeze_params_test.cpp`,
  `editor_panels_test.cpp`, `test_pattern_freeze_mode.cpp`, `CHANGELOG.md`, `panels/freeze.md`, `design-system.md`.
- **Risks.** (1) Nothing happens until the user presses it, and the user's complaint is exactly "defaults do not produce any
  audible effect": A leaves the default sound unchanged (F1) unless the default pattern/mix also changes. (2) Hold edge
  during silence. (3) Latched hold sits through a transport restart in the DAW: the buffer is stale but the parameter
  still reads "on". (4) A momentary needs real press/release semantics; VSTGUI has `CKickButton` but a mapped host
  parameter has no release unless the control sends 0 on mouse-up.

### Option B: Automatic re-capture every N bars / beats, phased to `ctx.projectTimeMusic`

- **Parameters.** Capture Length (N, in a note-value-like unit: 1/2, 1, 2, 4 bars or beats; reuse the 21-entry
  note-value dropdown from `note_value_ui.h`, or a short custom list). One parameter minimum, two with an on/off.
- **DSP.** Needs H2 (the ring must keep recording while the previous snapshot plays) or a double-buffered pair so the new
  window is written while the old one is read; H1 alone fails. Snapshot at each bar boundary; the in-flight pattern must
  crossfade between the old and the new snapshot (the existing 500 ms `processCrossfade` is not usable as is, F7:
  it replays one saved block). Capture the PREVIOUS N bars at the boundary (available immediately) so the effect starts
  after the first boundary, not after N bars of recording.
- **Scheduler.** The big one. Replace the free-running `sampleCounter_` with a position derived from `projectTimeMusic`:
  `core/transport_sync.h` already has `calculateMusicalStepPosition(ppq, note, modifier, numSteps)`, used by
  `trance_gate.h:338` and `arpeggiator_core.h:1028`. Needed changes: (1) the processor must pass `projectTimeMusic`,
  `projectTimeMusicValid`, `transportPositionSamples`, time signature (F5); (2) a sub-block step search because a step
  boundary can fall inside a block (the present scheduler already splits blocks at `pattern_scheduler.h:216-232`); (3)
  `isPlaying == false` or `projectTimeMusicValid == false`: fall back to the sample counter, and at transport start resync
  to the PPQ phase; (4) reposition/loop jumps (Gradus detects `ppq < prev` at `plugins/gradus/src/processor/processor.cpp:227`);
  a stopped transport that still reports a constant PPQ must not freeze the scheduler; (5) the tempo fallback 120 BPM
  when `kTempoValid` is missing (`processor.cpp:196-200`).
- **Plugin.** S1 for the parameter(s) plus the processor ctx change (a processor edit that touches every mode's context;
  no other Iterum mode reads those fields, so no behaviour change elsewhere, but `mode_crossfade_tests` and the other
  processor tests build the same ctx).
- **State.** Tail of one or two ints. Old preset loads as: Capture Length = "Off" (legacy rolling echo), or the new
  default for new instances only, as Tape sync did.
- **Presets.** The 10 factory Freeze presets would get whatever the new default is unless the generator is extended
  (S2).
- **Tests.** DSP: boundary at 4 beats of PPQ with `isPlaying` true and false, jump back, block that straddles a boundary.
  Processor case with a `ProcessContext` carrying the PPQ (S4). New: a golden-free "same bar twice gives the same render"
  check.
- **Files touched.** About 15: those of A plus `transport_sync.h` use in `pattern_scheduler.h` (a Layer 2 file, so
  `header-consumers` reaches more executables: the scheduler is included by `pattern_freeze_mode.h` and by
  `pattern_scheduler` tests), `processor.cpp` ctx build, a processor test, and a transport test for the scheduler.
- **Risks.** (1) Works with no interaction, which answers "default does nothing", but only on a host that supplies PPQ;
  standalone/offline hosts and some CLAP/VST3 wrappers do not. (2) First N bars of any session are silent or legacy. (3)
  A snapshot taken during a break in the music holds silence; a gate (Option C) would protect it. (4) Time-signature
  bar length needs `timeSigNumerator/Denominator`; without it, "bar" must be defined as 4 beats. (5) Bar-phased snapshots
  copy up to 2-8 MB at the boundary on the audio thread (H2): schedule the copy across blocks.

### Option C: Input gate (capture when input exceeds a threshold, hold while quiet)

- **Parameters.** Threshold (dB) minimum; hold/hang time as a second if wanted. Optional: re-arm.
- **DSP.** An envelope follower on the input (`dsp/include/krate/dsp/...` has envelope followers, not re-listed here)
  controlling H1: write while the follower is above the threshold plus a hangover, stop writing below. The ring then
  always contains the last loud passage; the effect needs no transport and no button. Two states of the ring: "recording"
  and "held". Slices and held reads are the same as A.
- **Scheduler.** None.
- **Plugin.** S1 for one or two parameters. The threshold needs a meter or at least a readout.
- **State.** Tail of one or two floats (threshold in dB is a float: a float field instead of an int in the tail; the
  normalised mapping goes in `loadFreezeParamsToController`, as each float there does). Old preset loads as: gate off
  (threshold at its minimum, always writing).
- **Presets.** None needs to change.
- **Tests.** DSP: loud burst then silence, wet continues to play the burst 3 s later; threshold above the level, ring
  untouched. Level-dependent, so it needs a level calibration test (test signals at -20 and -50 dBFS).
- **Files touched.** About 11: as A, minus nothing, plus an envelope follower include.
- **Risks.** (1) Material with no clear gaps (sustained pads, a mixed bus) never lets the gate close, so the ring rolls
  and the effect reverts to F1. (2) A fixed threshold is level- and track-dependent: a bad default makes it silent or inert.
  (3) A gate decides what to HOLD, not when to START playing: with C alone the pattern still plays the rolling ring while
  the input is loud, which is again indistinguishable from the input (F1); C is a capture-quality feature and does not
  by itself make the wet different from the dry. (4) Chatter on percussive input at the threshold.

### Option D: Combination (Auto / Latch / Momentary Mode parameter + Capture Length)

- **Parameters.** (1) Capture Mode enum {Live (= today), Latch, Momentary, Auto}; (2) Hold (toggle), used by Latch and
  Momentary; (3) Capture Length (bars or beats), used by Auto and optionally by the Latch/Momentary snapshots to limit
  their length (H3). The Mode enum is where the "both" of A3 lives: Mode picks the interaction, Hold is the button.
  Optionally (4) Gate threshold (Option C), giving a fourth mode "Gate". 3-4 parameters: IDs 1070-1073.
- **DSP.** H2 required (B needs it); H3 for the capture length. One state machine over {Live, Held, Auto-window} with a
  pending-edge flag so the snapshot is taken at block start; crossfade between old and new snapshots (F7 fix is a
  prerequisite). The forward held read, per-step slice selection and the Drones pitch shift of Section 2 are common.
- **Scheduler.** As B for Auto only; Latch/Momentary use the free-running counter. Two scheduler paths to test.
- **Plugin.** S1 for 3-4 parameters; **S3 is exceeded**: Mode (segment cell, 44 pt), Hold (button), Capture Length (slider
  or menu, 42-44 pt), plus Gate threshold = more than the two free SLICE rows. Needs a re-lay-out of the SLICE fieldset
  (rename CAPTURE, three or four cells at y = 24, 76, 128 and a fourth at y ≈ 180 which exceeds the fieldset's 180 pt
  height; so two columns of cells or a taller fieldset by shrinking ENVELOPE), an edit of `panels/freeze.md` geometry
  checks and the 6-fieldset structure count, an update of `design-system.md` 7.10/8.2, and `freeze.xsl`. Conditional
  visibility (Capture Length only in Auto, Hold only in Latch/Momentary) would add a fifth `ContainerVisibilityController`
  member in `controller.cpp` (pattern at `:923-955`).
- **State.** Tail of 3-4 fields: Mode (int), Hold (do not persist, or persist as 0), Capture Length (int index), Threshold
  (float). Old preset loads as: Mode = Live, Hold off, Capture Length = default, threshold = off: sounds exactly as today.
- **Presets.** Factory Freeze presets load as Live unless the generator is extended (and then needs the Tape and MultiTap
  tails first, S2). To make the presets demonstrate Auto/Latch, ten presets are rewritten in `preset_generator.cpp`
  (1260-1420) and ten `.vstpreset` files regenerated and committed.
- **Tests.** Everything of A, B and C that is in the chosen subset, plus mode-change transitions (Live to Auto with a
  snapshot in flight, Latch to Momentary), which are the usual click and stale-state sources.
- **Files touched.** About 20 to 25: A + B + C files, the SLICE re-layout in `editor.uidesc` / `freeze.xsl`,
  `controller.cpp` visibility, 10 regenerated presets, `panels/freeze.md`, `design-system.md`.
- **Risks.** (1) Largest surface: three behaviours in one object, each with the silent-capture and stale-state failure
  modes above. (2) Parameter count in a 580 x 372 variant area that has no free half-columns (each variant has 4-5 cells)
  pushes new controls into the 284-wide column 1, which has two free rows (S3). (3) Tail grows to 4 fields; the order and
  legacy values must be right in four places (processor load, controller sync, controller load-with-notify,
  generator). (4) Mode transitions in a host that automates the Mode parameter.

## 4. Cross-option table

| | A | B | C | D |
|---|---|---|---|---|
| New parameters | 1 (bool) | 1-2 | 1-2 | 3-4 |
| Hold method | H1 or H2 | H2 mandatory | H1 | H2 + H3 |
| Extra memory | 0 or 2 MB (48 kHz) / 8 MB (192 kHz) | 2 / 8 MB | 0 | 2-4 / 8-16 MB |
| Scheduler change | none | PPQ-phased steps, transport fallback | none | PPQ path for Auto only |
| Processor ctx change | none | PPQ + time signature + position | none | PPQ + time signature + position |
| Needs user action to hear the effect | yes | no | no (but see C risk 3) | depends on default mode |
| Panel fits free rows (S3) | yes | yes (1 cell) | yes (1-2 cells) | no, re-layout |
| State tail | 0-1 int | 1-2 ints | 1-2 floats | 3-4 fields |
| Files touched (approx.) | 12 | 15 | 11 | 20-25 |
| Presets regenerated | none | optional | none | optional, 10 files |
| Approval goldens affected | none | none | none | none (F11) |
| Existing DSP cases that assert changed behaviour | 5-6 | 5-6 | 3 | 6+ |

Prerequisites no option avoids: the forward held read for Drones and Noise Bursts (F3), per-step slice selection
(Section 2), a rule for empty captures (F6), `Slice Mode` either wired or removed (F4, no ID/type change allowed).

## 5. The mix question: 50 % vs 100 % wet vs gate/insert, in terms of the existing Mix parameter

Frozen: ID 1012, type, range 0-1, default 0.5 as registered (`freeze_params.h:271`). Saved state stores the float
(`freeze_params.h` save/load; factory presets use 0.3, 0.4, 0.45, 0.5, 0.55, 0.6, 0.7, 0.8, 1.0:
`preset_generator.cpp:1268,1285,...`). The present meaning is a crossfade: `dry*(1-m) + wet*m` (`:589-590`).

- **Keep as is (m = crossfade) and change only the default for NEW instances** (registered default 1.0 or 0.7; the
  `FreezeParams::dryWet` initial value and the DSP `kDefaultDryWetMix` follow). Saved presets and projects carry their float, so
  nothing saved changes. Cost: 3 constants (`freeze_params.h:30`, `:271`, `pattern_freeze_mode.h:99`) and the tests
  that assert 0.5 (`freeze_params_test.cpp`, `mix_scale_consistency_test.cpp:99`, the DSP "mix parameters" case). Risk:
  at 100 % the output is the wet only, so with F1 unfixed the user hears only a late copy (the first 200 ms after the
  switch are silent: F6).
- **100 % wet as the meaning of the hold** (the dry is muted while a hold is active, as the class comment already
  claims): new behaviour tied to the hold state, not to the Mix value; presets with m < 1 would suddenly be silent
  dry during a hold. Cost: one gain stage in `process()`. Risk: changes the sound of every saved Mix < 1 preset only when
  hold is on, which cannot occur in an old preset (hold off), so no saved sound changes.
- **Insert / gate mode: `out = in + wet*m` (dry always full, Mix as wet level).** Different law on the SAME parameter, so
  it must not replace the crossfade for saved states: every preset with Mix 0.3-0.8 would get louder (dry no longer
  attenuated). A selector is needed (a Mix-law int appended to the tail; legacy = crossfade; the Mode parameter of
  Option D can carry it). Cost: 1 parameter, 7 files (S1), the Mix readout wording ("Mix" vs "Wet level"), panel space. Risk:
  gain staging, the Gain parameter, and clipping when wet + dry add (needs headroom or the existing
  `1/sqrt(count)` compensation re-derived).
- **Gate mode (dry ducked while a slice plays).** Needs a side-chain from the slice envelope to the dry gain: touches
  `processEuclideanSlices` and the three other pattern processors (each would have to publish an envelope), plus a
  smoothing time. Highest cost of the mix options; no state or ID problem if it is an appended parameter.

Constraint common to all: existing Mix automation and presets must keep their crossfade meaning (Section 1 S2 "legacy
value").

## 6. Risk list that is independent of the option

1. **Real-time rules.** H2 copies and Drone pitch shifters must be preallocated in `prepare()`; the snapshot copy cannot
   allocate, lock or throw in `process()`.
2. **Cross-platform fast-math.** Any new finiteness test goes through `Krate::DSP::detail::isFinite`; no
   `numeric_limits::infinity()` seeds; pow/sqrt/log arguments clamped at the domain edge (CLAUDE.md macOS leg). The
   present Drones code uses `std::pow` and `std::fmod` per sample per voice (`:913,926,932`), which a rewrite should hoist.
3. **Mode switch.** `reset()` on mode change clears the ring (F6) and a held buffer; make the choice explicit (clear or
   keep across a mode switch; keeping needs the processor to feed the ring while another mode is active, which it
   does not today).
4. **Bit-exact comparisons between separately compiled code are forbidden** (CLAUDE.md): the "same bar twice" and
   "release restores legacy" checks must use tolerances.
5. **Wall-clock tags.** A render-guard test of several seconds must carry `[wallclock]` (tools/lint-wallclock-tags.js).
6. **Run set for any option.** `node tools/header-consumers.js --tag '[iterum]' dsp/include/krate/dsp/effects/pattern_freeze_mode.h`
   reaches `dsp_effects_tests` and `plugin_tests` / `approval_tests` (per the task); `pattern_scheduler.h` (Option B/D) adds
   the processor-layer tests; the processor ctx change reaches every Iterum processor test. Pluginval (strictness 5)
   after any plugin source change.

## 7. Files read for this report

`dsp/include/krate/dsp/effects/pattern_freeze_mode.h`, `.../processors/pattern_scheduler.h`,
`.../primitives/rolling_capture_buffer.h`, `.../core/pattern_freeze_types.h`, `.../core/block_context.h`,
`.../core/transport_sync.h`, `.../processors/pitch_shift_processor.h` (headers), `plugins/iterum/src/parameters/freeze_params.h`,
`tape_params.h` (sync tail), `multitap_params.h` (length tail), `processor/processor.cpp` (ctx build, Freeze case, get/setState),
`controller/controller.cpp` (sync, load-with-notify, visibility), `tools/preset_generator.cpp`,
`specs/iterum-ui-redesign/panels/freeze.md`, `specs/iterum-freeze/probe/before.txt`, `test_pattern_freeze_mode.cpp`
(probe), `multitap_tempo_output_test.cpp` (rig). Nothing was built or run for this report; F1 numbers are the
existing probe output.
