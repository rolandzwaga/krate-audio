# MultiTap custom pattern: DSP and state consequences of the time-axis model

Scope: what options A, B and C cost, checked against the code. This report recommends nothing; the designer decides.
All paths are relative to `F:\projects\iterum`. Line numbers were read on 2026-10-29 on branch `ui/iterum-consistency`.

## 0. How the custom pattern works today (the baseline every option is measured against)

- Stored state: 16 time ratios (params 950-965, 0..1) and 16 levels (966-981, 0..1 linear gain), plus Snap (913).
- Time: `applyCustomTimingPattern()` (`dsp/include/krate/dsp/effects/multi_tap_delay.h:852-868`) sets
  `timeMs = clamp(maxDelayMs_ * ratio, kMinDelayMs, maxDelayMs_)`. `maxDelayMs_` is 5000 because the processor calls
  `multiTapDelay_.prepare(sampleRate_, maxBlockSize_, 5000.0f)` (`processor.cpp:96`). The scale is `maxDelayMs_`, not a
  literal. Raising the prepared ceiling would silently rescale every saved custom pattern (finding H-3).
- Level: linear gain, `gainToDb`, below 0.001 becomes -96 dB, clamped to [-96, +6] dB (`multi_tap_delay.h:861-863`).
- Tempo never enters the custom pattern. Tempo reaches the DSP only as `ctx.tempoBPM` in `process()` (`multi_tap_delay.h:603-636`).
- The processor builds `BlockContext` per block from `data.processContext` (`processor.cpp:192-207`). If `kTempoValid` is
  absent it falls back to 120 BPM. `timeSignatureNumerator/Denominator` are NOT filled in, so they stay the 4/4 default.
- The processor pushes custom ratios and levels into the DSP every block, 32 `setCustom*Ratio` calls, each of which calls
  `applyCustomTimingPattern()` while the pattern is Custom (`processor.cpp:729-742`; `multi_tap_delay.h:315-340`).

## 1. Findings that hold under every option (by severity)

### H-1 (high): Copy does not preserve musical meaning today, under any model
`calculatePatternRatios` (`plugins/iterum/src/parameters/pattern_calculator.h:59-129`) divides every time by the LAST tap's
time, so the last tap is always ratio 1.0, i.e. 5000 ms. The controller calls it with only the pattern index and tap count
(`controller.cpp:1143`); no tempo, no note value.
Example: Eighth-note pattern, 6 taps, 120 BPM. Real taps (DSP): 250, 500 ... 1500 ms. Copied to Custom: 1/6 ... 1 of 5000 ms =
833 ... 5000 ms. The copy sounds 3.3x slower and ends on the ceiling.
Rhythmic patterns ignore Note Value entirely in the copy. Mathematical patterns lose their absolute scale
(`baseTimeMs_` = Note Value at tempo, `multi_tap_delay.h:628-636`). The existing unit tests
(`tap_pattern_editor_test.cpp:637-720`) assert only the normalised shape.
Fix cost per option is in sections 2-4. One more uncertainty: `tap_manager.h:694` uses `fibonacci(n) * base * 0.25f` for
TapManager's Fibonacci while `calculatePatternRatios` uses unit Fibonacci; the shape matches after normalisation but an
absolute-time copy must follow the DSP formula: MultiTapDelay calls `tapManager_.loadPatternWithBaseTime(TapPattern::Fibonacci, ...)`
(`multi_tap_delay.h:779-780`), so the 0.25 factor applies and the copy's absolute scale must include it.

### H-2 (high): Snap labels are fractions of 5000 ms, not note values
`getSnapDivisions` (`tap_pattern_editor_logic.h:252-286`) returns a count of equal slices of the 0..1 axis: "1/4" = 4 slices =
1250 ms, "1/16" = 16 slices = 312.5 ms, "1/2 dotted" = 1 slice, "1/1 triplet" = 2 slices, "1/4 dotted" = 3 slices. That equals
the named note value only at 48 BPM (a 4/4 bar is exactly 5000 ms). At 120 BPM a "1/4" snap point at 1250 ms is 2.5 quarter
notes. The snap is a saved state value (param 913, appended to the MultiTap block, `multitap_params.h:413-414`).
Every existing preset carries Snap = 14 ("1/4"), including all 10 factory presets (`tools/preset_generator.cpp:203`).

### H-3 (high): the 5000 scale is `maxDelayMs_`
Any change to the prepared ceiling (to give slow tempos room, finding in section 3) rescales every old pattern unless the
custom scale is decoupled from `maxDelayMs_` first (a literal `kLegacyCustomSpanMs = 5000`). Every option that touches the
ceiling needs this step.

### M-1 (medium): switching a pattern to Custom morphs toward the wrong target
`morphToPattern(Custom)` (processor `processor.cpp:717-723`) calls `calculatePatternTimes(Custom,...)`, whose `default:` branch
gives `n * baseTimeMs_` (`multi_tap_delay.h:1035-1036`), not the custom times. `currentTimingPattern_` stays the OLD pattern
until the morph ends (`multi_tap_delay.h:1052`), so the per-block `setCustomTimeRatio` calls do not apply during the morph
(`:318` and `:336` check `== Custom`). At morph end `updateMorph` snaps taps to `n * baseTimeMs_` (`:1054-1056`) and the next
block's `setCustomTimeRatio` jumps them to the custom positions (through the 20 ms per-tap delay smoother,
`tap_manager.h:806-812`). So "Morph Time" never morphs into the user's custom shape. Tap-count changes do not morph at all
(`processor.cpp:709-716` calls `loadTimingPattern`, immediate). The brief said pattern/tap-count changes "morph over Morph
Time": true for preset-to-preset only.
Consequence for the design: no option may rely on Morph to cushion a custom-pattern change; tap-time changes of the Custom
pattern are applied immediately and smoothed only by the 20 ms per-tap smoother (a large jump is audible as a short pitch
sweep on the delay line).

### M-2 (medium): Custom pattern is re-applied by the note-value path
`process()` line 628 tests `currentTimingPattern_ >= TimingPattern::GoldenRatio`, which includes Custom (enum value 19).
So `setBaseTimeMs` -> `applyCustomTimingPattern` also runs when the Note Value / tempo changes the synced base time
(`:633-635`), although Custom ignores `baseTimeMs_`. Harmless today (idempotent); under B it is the natural hook but must not
double-apply.

### L-1 (low): per-block cost of the push
32 calls x `applyCustomTimingPattern` (16 taps each, one `gainToDb` log per tap) = 512 `setTapEnabled/Time/Level` and 512 logs
per block while the pattern is Custom. Not a correctness problem; any option that adds work to this loop should add a dirty
flag first.

### L-2 (low): `isPlaying` gate
The rhythmic-pattern tempo update only runs when `ctx.isPlaying` (`multi_tap_delay.h:608`), the mathematical-pattern path does
not (`:628`). A tempo-dependent Custom (B) must follow the second form or it will sit at the default 120 BPM while the host is
stopped.

## 2. Option A: keep ratio x 5000 ms, draw the grid where the current tempo's note lengths fall

DSP / processor: no change. `applyCustomTimingPattern` untouched.
State: no change. An old preset loads and sounds identically. No new field.
What changes in meaning: nothing in the sound. Visually, a saved pattern drawn with the old "1/4 = 1250 ms" snap is off-grid at
most tempos (taps sit at 2.5 quarters at 120 BPM). Patterns stay at absolute milliseconds when the host tempo changes
(not synced). The right edge (5000 ms) is a bar line only at 48 BPM.
Grid geometry (note length ms = `noteToDelayMs`, `note_value.h:226`; tempo clamped 20..300):
- 120 BPM: 1/4 = 500 ms = 10 lines across the axis; 1/16T = 166.7 ms = 30 lines; 1/1 = 2000 ms = 2.5 lines.
- 20 BPM: one beat is 3000 ms, 1/1 is 12000 ms, only the origin falls inside 5000 ms; 1/1 snap cannot place any tap but at 0.
- 300 BPM: 1/64T = 8.3 ms. The editor is about 852 px wide (`panels/multitap.md`), 5.9 ms/px, so the line pitch is 1.4 px: line
  culling is needed.
Tempo to the controller: REQUIRED (the grid and the snap need the note length in ms). See section 5.
Copy under A: can be made exact: ratio = n * patternBaseMs(tempo) / 5000, clamped to 1.0 like the DSP (`min(timeMs, maxDelayMs_)`),
for rhythmic patterns the controller needs tempo; for mathematical ones tempo plus params 911/912 (both readable via
`getParameterObject`). The copy matches what is heard at the moment of copying; afterwards the preset pattern follows tempo
and the copy does not.
Files touched (estimate): `tap_pattern_editor_logic.h` (note-length grid, culling, snap), `tap_pattern_editor.{h,cpp}`
(tempo setter, draw), `controller.{h,cpp}` (receive tempo, forward, Copy), `pattern_calculator.h` (Copy with tempo),
processor `.h/.cpp` + messaging (send tempo), tests (editor logic, copy, message), `CHANGELOG.md` = about 10-12 files.
Risks:
1. Snap semantics change for existing presets (H-2): visual only.
2. Taps do not follow tempo; "custom pattern" is a free-time delay at ms positions, which the grid then lies about when
   tempo automation moves.
3. Depends on the tempo message (section 5); with no message the grid must fall back to something (120 BPM like the
   processor, or the old fractions). The fallback must be drawn as a fallback.
4. Slow tempos: note lengths exceed the axis (section 3).

## 3. Option B: tempo-synced custom pattern (ratio x span at the host tempo, converted in the processor)

Definition used here: `timeMs = clamp(ratio * spanBeats * 60000 / bpm, kMinDelayMs, maxDelayMs_)`, where the span is N beats
or N bars (bars need the time signature; the processor does not currently copy `processContext->timeSigNumerator` into
`BlockContext`, `processor.cpp:202-207`, so beats or note values avoid a new read).

DSP change (`multi_tap_delay.h`):
- New member for the span (beats) and a tempo input to `applyCustomTimingPattern()`.
- `process()` must re-apply Custom when |dBPM| > 0.1, currently excluded by `currentTimingPattern_ != Custom` (`:616-619`),
  and must use `ctx.tempoBPM` regardless of `isPlaying` (L-2). Hook point is the existing `>= GoldenRatio` branch (M-2).
- Morph: does not help (M-1). Tempo automation would call `setTapTimeMs` every block it moves by more than 0.1 BPM; the 20 ms
  delay smoother turns this into a continuous pitch glide on the taps.
- `kMinDelayMs` = 1 ms (`:167`); ratio 0 is a 1 ms tap at any span. Unchanged.
- Ceiling at slow tempo: one 4/4 bar is 240000/BPM ms: 5000 ms at 48 BPM, 12000 ms at 20 BPM. Two bars exceed 5000 ms below
  96 BPM, four bars below 192 BPM. Beyond the ceiling `clamp` piles every late tap onto 5000 ms (wrong, silent pile-up) unless
  the effective span is clamped instead, in which case the musical grid scales down. Either way the controller must know which
  happened (needs bpm, or an effective-span value back from the processor).
  Raising the ceiling means raising `kMaxDelayMs` (`multi_tap_delay.h:168`) and the `prepare(..., 5000.0f)` call; delay-line
  memory scales linearly (I did not measure the TapManager/FeedbackNetwork memory), and H-3 must be done first.

State change (append-only): the 16 stored ratios keep their numbers, so they MUST be re-read as "fraction of the new span".
At tempo T with span `beats`, an old pattern changes speed by `5000 / (beats*60000/T) = T / (12 * beats)`:
- 4 beats (1 bar): 120 BPM gives 2.5x faster, 48 BPM identical, 90 BPM 1.9x.
- 16 beats (4 bars): 120 BPM gives 0.625x (slower, and the span 8000 ms exceeds the ceiling, so late taps clamp).
Two ways to preserve old sound:
- B1: append a time-mode field. Absent field = Free ms (5000 ms) = old behaviour; new patterns default to Synced. This is the
  Tape pattern: `saveTapeSyncParams` / `loadTapeSyncParams` (`tape_params.h:510-528`): "A state that ends before them
  comes from a Tape delay that ran on Motor Speed, so it loads as Free". The tail must go AFTER `saveTapeSyncParams` at the very
  end of the stream (`processor.cpp:320`, `:354`), not inside `saveMultiTapParams`: the MultiTap block is followed by the
  Tape sync fields, and Snap (913) was appended inside the block, so extending the block would shift the Tape tail on every
  saved state. The controller mirror is `loadTapeSyncParamsToController` (`controller.cpp:299` and `:1277`) and needs the
  same pairing; the preset generator (`tools/preset_generator.cpp:486-498`) writes no Tape tail either, so factory presets
  end early and must load as Free.
- B2: re-interpret silently. Saved patterns change speed by T/(12*beats) (above). Not recommended by the numbers, but it is the
  only version with no appended field.
The flag needs a VST parameter if it is to be automatable or if the controller is to read it from the stream into a control
(the repo's pattern: `kTapeTimeModeId`). Free IDs inside the MultiTap range that routes to `handleMultiTapParamChange`
(`processor.cpp:446`): 903, 904, 910, 982-999. Adding a parameter is not changing an existing one, but it creates a new
control the panel must carry ("every parameter keeps a control"), so it is a ruling for the user, not an assumption.

Variants of the span source, with the trap in each:
- Span = Snap length (param 913): every existing preset has Snap = 14 ("1/4", `preset_generator.cpp:203`), a value that has had no
  sonic effect so far. Span = one quarter note would make every old Custom pattern 10x shorter at 120 BPM
  (500 ms vs 5000) unless B1's flag also gates it. Snap Off (0) has no length. Snap automation would start changing the
  sound. High risk.
- Span = Note Value + Modifier (911/912): the NOTE VALUE fieldset is visible only for the mathematical patterns
  (`panels/multitap.md`), so Custom would need that fieldset shown; all old presets carry Quarter/None.
- Span = new parameter: that is option C.

Tempo to the controller: NOT required for the grid, because the grid can be fractions of the span in beats (tempo-free).
Required only for a milliseconds readout and for the over-ceiling warning. A missing message degrades to "no ms readout".
Copy under B: the copied preset pattern maps to beats, so a rhythmic pattern at base note length L (in beats) is
`ratio = n * L / spanBeats` with no tempo needed, exact and tempo-independent. Taps beyond the span clamp to 1.0. Mathematical
patterns copy via `baseTimeMs_` = Note Value, also in beats. B is the only option where Copy keeps its meaning across tempo
changes.
Files touched (estimate): `multi_tap_delay.h`, `processor.{h,cpp}`, `multitap_params.h`, `plugin_ids.h` (flag/span param),
`processor.cpp` state tail, `controller.cpp` (register, state tail mirror, forwards, Copy), `pattern_calculator.h`,
editor logic + view, `tools/preset_generator.cpp` (tail), `multitap.xsl` + `editor.uidesc` (new control), DSP test
(`multi_tap_delay_test.cpp`), plugin tests, `approval_tests` (the brief says the DSP formula may change), `CHANGELOG.md`
= about 16-20 files, one of them in `dsp/` so `dsp_effects_tests`, `plugin_tests` and `approval_tests` rebuild/run.
Risks: old presets change speed (B2) or need the flag (B1); ceiling pile-up at slow tempo; tempo automation glides; a "synced"
pattern captured at one tempo stops being a literal delay time (no ms stored); Morph does not cover it (M-1); tempo fallback
120 BPM when the host gives none; more state in the stream tail.

## 4. Option C: a span parameter (ms or bars) that the user sets

C-ms (free milliseconds):
- DSP: `applyCustomTimingPattern` multiplies by `customSpanMs_` instead of `maxDelayMs_` (clamped to `maxDelayMs_`). One setter.
  No tempo anywhere in DSP.
- State: one appended float. Absent = 5000 ms = old behaviour exactly (same absent-means-legacy rule as Tape sync). Old
  presets sound identical. Factory presets (generator writes no tail) load as 5000.
- Controller/editor: the grid can stay "fractions of the span" (the current Snap semantics become honest: 1/4 = a quarter of the
  span, so a user who sets span = one bar gets musical divisions); a milliseconds readout is `ratio * span` with no tempo.
  The editor needs the span via `setParamNormalized` forwarding (the same path as Snap/TapCount, `controller.cpp` setParamNormalized
  forwarding). Tempo message optional (only for a "span = X bars at current tempo" hint).
- Behaviour to design for: changing the span rescales all taps at once ("stretch"), which is a feature if wanted and an accident if
  the knob is bumped; each change is an immediate jump, not a morph (M-1).
- Files (estimate): `multi_tap_delay.h`, processor `.h/.cpp`, `multitap_params.h`, `plugin_ids.h`, `controller.cpp`,
  editor, `multitap.xsl`/`editor.uidesc`, generator, tests, `CHANGELOG.md` = about 12-14 files.
C-bars (span in bars or beats at host tempo): is B with a user-set span; it needs both B's tempo conversion and C's new parameter
and carries all of B's slow-tempo ceiling risk. If the span choice list includes a "Free (5 s)" entry as index 0 and the absent
field loads as index 0, a single parameter replaces B1's separate flag (old state = Free). That is a design option, not a
finding.
New parameter ruling: C needs a new parameter and control under both flavours (IDs 903/904/910/982-999 are free inside the routed
range). Same "every parameter keeps a control" consequence as B.

## 5. How the tempo reaches the controller (needed by A; optional for B and C)

Current state: Iterum has no `connect`, `notify`, `allocateMessage` or `sendMessage` anywhere in `plugins/iterum/src`
(grep: only `controller.cpp`, `tap_pattern_editor.{h,cpp}`, `controller.h` match `notify`, all for parameter edits). The
processor never stores the tempo it computes (a local `double tempoBPM` per block, `processor.cpp:192`); the DSP keeps its own
`bpm_` with a 0.1 BPM threshold but only while playing (`multi_tap_delay.h:608-612`). The controller has no `notify` override and
the processor has no `connect`. VST3 gives a controller no `ProcessContext`.

Existing patterns in this repo:
- Innexus (`plugins/innexus/src/processor/processor_messages.cpp`): `connect()` creates a `DataExchangeHandler` (lines 18-54:
  block size rounded UP to a multiple of the alignment 32, because macOS `aligned_alloc` returns null otherwise; `numBlocks = 8`),
  `disconnect()` tears it down, `sendDisplayData()` throttles to about 30 Hz (`:79-88`), takes `getCurrentOrNewBlock()`,
  `memcpy`, `sendCurrentBlock()` (`:179-187`). No allocation on the audio thread after setup. It also has a shared-memory
  fallback ("Tier 3") for hosts without `connect()`. Controller side: `dataExchangeReceiver_.onMessage(message)` in `notify`
  (Disrumpo `controller.cpp:1960-1976`). `setActive(true)` calls `dataExchange_->onActivate(processSetup)`
  (`disrumpo processor.cpp:166-168`).
- Disrumpo `sendModOffsetsMessage()` (`processor_messaging.cpp:149-165`): `allocateMessage()` + `setInt` + `sendMessage`, called
  ONCE from `setActive(true)` (`processor.cpp:170`), a non-realtime thread. It sends a raw pointer (both components in process);
  the controller stores it (`controller.cpp:1966-1973`). That pattern suits constant data, not tempo that changes.
- Innexus `notify()` replies with `allocateMessage()` on the message thread, not in `process()`.

Candidate paths for tempo:
1. `allocateMessage()` + `sendMessage()` from `process()` on change only. Violates the real-time rule (allocation, and the host
   decides how it queues). Rate limit: send on change > 0.1 BPM plus a periodic resend. Not used anywhere in the repo for this.
2. A message preallocated in `setActive(true)` and re-sent after `setFloat`. The SDK's own host implementation
   (`extern/vst3sdk/public.sdk/source/vst/hosting/hostclasses.cpp:244-249`) overwrites a `std::map` entry (no allocation for a
   short key such as "tempo" with small-string optimisation), but that is the SDK's reference host; other hosts' attribute
   lists are unspecified. Unverified as safe.
3. DataExchange block as Innexus/Disrumpo: allocation-free in `process()`, needs `connect()`/`disconnect()`, the 32-byte rounding,
   `onActivate/onDeactivate`, a receiver in the controller, and the Linux caveat: the SDK DataExchange fallback uses an
   `IMessage` path that needs a `Timer`, which a bare Linux test harness has no run loop for, so tests there cannot receive.
   Needs a re-send on a timer or a controller-to-processor "request tempo" message so a late-opening editor gets a value: the editor
   can open mid-session after the tempo last changed.
4. Hidden read-only output parameter (processor writes `outputParameterChanges`; real-time safe by SDK design). Adds a parameter
   (frozen-ID ruling applies, see section 3) and depends on the host forwarding output parameters to the controller; no
   existing Iterum code uses `outputParameterChanges`.
All paths: the controller must survive a missing message (no tempo yet). Needed behaviours: default to a "no tempo" state,
draw no note-length grid or fall back to 120 BPM like the processor (`processor.cpp:192`), and never block the first draw on a
message.

Headless testing:
- DSP: `BlockContext{.tempoBPM = ..., .isPlaying = true}` is injectable in `dsp_effects_tests` and the processor-level tests
  (`mode_switch_buffer_retention_test.cpp:102` and `mode_crossfade_tests.cpp` build `BlockContext` for DSP calls).
  `ProcessContext`/`processContext` does not appear in any Iterum test (grep, 0 hits), so a processor-level test that goes through
  `Processor::process` with a tempo needs a new `ProcessData` + `ProcessContext` fixture.
- Controller/editor: no tempo setter exists. Tests would call a new view setter directly, or, for the message path, build an
  `IMessage` via the SDK `HostMessage` (used by `plugins/innexus/tests/unit/controller/test_data_exchange_receiver.cpp` and
  `plugins/disrumpo/tests/integration/data_exchange_spectrum_test.cpp`; Iterum's test CMake does not link `hostclasses` today,
  not checked in detail). The editor tests open the controller with `initialize(nullptr)` (`editor_panels_test.cpp:111`),
  so no host and no run loop.

## 6. Side-by-side summary

| | A: ms x 5000, tempo grid | B: tempo-synced span | C-ms: span parameter |
|---|---|---|---|
| DSP change | none | `multi_tap_delay.h` tempo + span, re-apply on tempo change | span member replaces `maxDelayMs_` scale |
| New state | none | span/flag appended at stream end (after Tape sync) | one float appended at end |
| Old preset | identical | identical only with the flag (B1); otherwise T/(12*beats) speed change | identical (absent = 5000) |
| New parameter + control | none | yes (flag and/or span) | yes (span) |
| Tempo to controller | required | optional (ms readout) | optional |
| Copy keeps musical meaning | only at copy time, needs tempo | yes, tempo-free, follows tempo | only if user sets span, tempo-free |
| 5000 ms ceiling | grid exceeds axis at slow tempo | span clamps/pile-up at slow tempo | none (span <= 5000 by definition) |
| Files (rough) | 10-12 | 16-20, includes dsp/ | 12-14, includes dsp/ |
| Extra gates | plugin_tests | + dsp_effects_tests, approval_tests | + dsp_effects_tests, approval_tests |

Findings H-1, H-2, H-3 and M-1 are independent of the choice; the design should state what it does about each.
