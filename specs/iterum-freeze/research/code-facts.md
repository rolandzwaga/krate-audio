# Freeze mode: code facts (PatternFreezeMode, spec 069)

Scope: what the mode does today, read from the code, with file:line. Nothing in this file was changed in the repo.
Method: every claim below was read in the source. Two things were also checked by arithmetic outside the DSP
(a node replay of the `extractSlice` index math, `scratchpad/idx.js`, and closed-form energy sums for the probe levels).
The DSP itself was NOT recompiled or re-run for this report (no compiler on PATH outside the CMake build tree); where a
statement is an inference from the code and the probe table rather than a measurement, it says "inference".

Paths: `H` = `dsp/include/krate/dsp/effects/pattern_freeze_mode.h`, `S` = `.../processors/pattern_scheduler.h`,
`R` = `.../primitives/rolling_capture_buffer.h`, `T` = `.../core/pattern_freeze_types.h`,
`P` = `plugins/iterum/src/processor/processor.cpp`, `F` = `plugins/iterum/src/parameters/freeze_params.h`.

---------------------------------------------------------------------------------------------------------------------

## 0. The short answer to "why do the defaults sound like nothing"

At the plugin's defaults (Euclidean, 200 ms slice, 8 steps / 3 hits, **pattern rate 1/8**, mix 50 %, 120 BPM) the wet path
is: the live input, delayed by about 188 ms, played back in three 200 ms bursts per 2 s bar, at -6 dB relative to the dry
signal that keeps passing at 50 %. On any sustained or repetitive material a delayed copy of the input is the input, so
the effect is a faint, sparse slapback that sounds like a level change. Nothing is held, nothing is transformed. Details
and evidence in findings 1-6.

---------------------------------------------------------------------------------------------------------------------

## 1. Findings by severity

### HIGH

**H1. Freeze never freezes: every pattern plays the newest input, nothing is held.**
- `process()` writes every input sample into the ring unconditionally (H:529-532). Nothing ever snapshots it.
- `onPatternTrigger` cuts the slice at trigger time: `offset = 0` except GranularScatter (H:675-682). In
  `RollingCaptureBuffer::extractSlice` offset 0 is the newest `lengthSamples` (R:141-170: `startOffset = offset + length`,
  `startIndex = writeIndex_ - startOffset`, read forward). So an Euclidean hit plays "the last 200 ms of input", now.
- All three Euclidean hits of a bar therefore play different material only because the input changed; for a steady input
  they are the same sound. No step selects a different part of the buffer (the `step` argument is `[[maybe_unused]]`,
  H:639).
- The processor forces the mode permanently "engaged": `patternFreezeMode_.setFreezeEnabled(true)` every block with the
  comment "always active ... no checkbox needed" (P:766-767). There is no Trigger, no hold, no capture moment.
- Evidence it is not a corner case: probe row Euclidean, best lag 174.8 ms, r = 0.69 (`specs/iterum-freeze/probe/
  before.txt:3`): the wet is correlated with the input 175-200 ms earlier, not with any held snapshot.

**H2. The plugin's defaults are not the DSP defaults the probe measured: the Euclidean step is 1/8, not 1/16.**
- `PatternFreezeMode` defaults to `NoteValue::Sixteenth` (H:1158) and `snapParameters()` applies it (H:237). The probe
  never calls `setNoteValue`, so it ran 1/16 steps (125 ms at 120 BPM).
- The plugin pushes `patternRate` every block (P:781-784). Its default is `Parameters::kNoteValueDefaultIndex`
  (F:44) = 10 (`note_value.h:126`), which `kNoteValueDropdownMapping[10]` maps to `{Eighth, None}` (`note_value.h:165`).
  So the plugin runs 250 ms steps; 8 steps = 2.0 s = one bar of 4/4 at 120 BPM.
- Consequence (derived, not measured): hits at steps 0, 3, 6 (EuclideanPattern::generate(3, 8, 0) walked by hand:
  accumulator 8,0->3,6,9->1->4,7,10->2->5,8; hits at i = 0, 3, 6; `euclidean_pattern.h:95-107`) fall at 0, 750, 1500 ms of
  a 2000 ms cycle. Each slice lasts 200 ms, so the Euclidean wet is active 3 x 200 / 2000 = **30 %** of the time (the
  probe's 59 % is the 1/16 case) and about 3 dB lower in average power than the probe row: roughly -13 dB re input
  instead of -10 dB. The probe's Euclidean row therefore OVERSTATES what the user hears. (Noise Bursts: DSP default
  `noiseBurstRate_ = Eighth` H:1183 equals the plugin default index 10, so that row is representative.)
- The probe must call `setNoteValue(Eighth)` (or the harness must push the same values the processor does) before the
  "after" table can be compared with what a user hears.

**H3. The wet level of -10 dB is NOT gain compensation; it is duty cycle + envelope + the probe's own definition.**
- Gain compensation is `1 / sqrt(activeSliceCount_)` (H:724-726). At the probe's defaults only one slice is ever active
  (hits 375 ms apart, slices 200 ms), so the factor is exactly 1.0. The seed claim "(1/sqrt(n) gain compensation, ...)" is
  refuted for Euclidean.
- Exact decomposition of the -10.1 dB (probe row 3): hits 3 x 200 ms per 1000 ms = 0.60 -> -2.22 dB; envelope energy of
  a 8820-sample slice with 441-sample attack and 4410-sample release over the Hann table, mean w^2 of a half-Hann = 3/8:
  (3969 + 0.375 x 4851) / 8820 = 0.656 -> -1.83 dB; the probe defines wet as `out - 0.5 * in` (probe-case.cpp.txt:3, 39), i.e.
  the wet AFTER the 50 % mix gain -> -6.02 dB. Sum -10.07 dB; measured -10.1 dB.
- So the slice itself plays at about -4 dB re input (peak 0 dB, the envelope plateau), and the mix halves it. Dry passes
  at 0.5 x input (H:589).

**H4. Harmonic Drones and Noise Bursts play BACKWARDS, and, worse, replay ONE host block in reverse over and over.**
Seed fact confirmed, with a consequence the seed did not state.
- Direction: `extractSlice(&l, &r, 1, offset)` with length 1 returns age = `offset` (R:161: `startOffset = offset + 1`,
  index `writeIndex_ - offset - 1`), i.e. offset counts samples BEFORE the newest. Drones pass `readIdx`
  that grows with `droneSlicePos_` (H:931-936); Noise passes `noiseBurstSliceOffset_ + noiseBurstPosition_` (H:1023). Both
  therefore walk further into the past with each output sample: reversed playback. (Euclidean/Granular read a whole slice
  with one `extractSlice` and get it forward, `out[i]` has age `offset + length - 1 - i`, R:359-363.) Drones also wrap
  with `fmod` (H:932) and read with truncation, not the "linear interpolation" the comment at H:934 claims.
- Block-loop consequence (verified by replaying the index arithmetic, `scratchpad/idx.js`): the whole block is written to
  the ring first (H:529-532), then the output loop reads with `writeIndex_` frozen at the END of the block. The age grows
  by 1 per output sample AND the head moves forward by one block per block, so for Noise Bursts and for Drone voice 0
  (ratio ~1) the absolute index read at block k+1 equals the absolute index read at block k. Result (N = 512):
  every block reads absolute indices 17991 down to 17480 (Noise) / 20991 down to 20480 (Drone V0), identical in four
  consecutive blocks. The same N-sample window is played reversed once per host block: a buzz at `sampleRate / blockSize`
  (86 Hz at 512, 689 Hz at 64, 21 Hz at 2048) whose timbre is a fragment of the input, not the input. Drone voice 1
  (ratio 2) reads 2N per block but its start moves back only N per block (20991, 20479, 19967, ...): a discontinuity every
  block as well. Drones re-anchor to the newest material only when `droneSlicePos_` wraps at 8820 samples (H:949-952).
- Probe agreement: Drones `r = 0.03`, Noise `r = 0.05` against the input at any lag within 600 ms (before.txt:7, 9): they
  are not delayed copies of anything; consistent with a block-sized reversed loop (inference for the audible character;
  the arithmetic is verified).
- The same replay explains why the sound depends on the host buffer size.

**H5. The pattern-change crossfade replays one stale block for 500 ms.**
- `setPatternType` copies `outputL_` (the previous block's wet output, `maxBlockSize` long) into `crossfadeOutL_` once
  (H:279-282). `processCrossfade` then mixes `crossfadeOutL_[i]` for `i = 0..numSamples-1` in EVERY block of the
  22050-sample fade (H:1078-1089) while only `crossfadeProgress_` advances. The "old pattern" is a single 512-sample
  chunk looped ~43 times with a falling gain, while the new pattern fades in from silence (`fadeIn = sin(t pi/2)`).
- Audible: a buzz at the block rate for half a second after every Pattern Type change, unless the stored block happened to
  be silent; plus a wet dip while the new pattern fades in. Also fires on the first block after `prepare` when the saved
  type differs from Euclidean, because the processor pushes the type every block (P:768) and `setPatternType` sees a
  change.
- The test that "guards" it checks `maxSample < 2.0f` on a zeroed buffer (test_pattern_freeze_mode.cpp:~700-724): vacuous.

**H6. The pattern is not phased to the host and ignores the transport.**
- `PatternScheduler` is a free-running sample counter: `sampleCounter_` / `currentStep_` (S:199-217). Step 0 starts when
  the DSP first calls `scheduler_.process`, which happens only once the ring holds >= 200 ms (`canProcess`, H:551-556), so
  for a plugin that has just been put in Freeze mode the bar starts ~200 ms (the next block boundary) after entry.
- `BlockContext::isPlaying`, `transportPositionSamples`, `projectTimeMusic` are not read anywhere in the DSP or scheduler
  (grep over H and S: no hit). The probe's playing=1 and playing=0 rows are identical for every type (before.txt:3-10).
- The seed premise "the BlockContext carries ... projectTimeMusic (+ valid) from the host" is **refuted for Iterum**: the
  struct has the fields (`block_context.h:63-67`) but the processor builds the context with only `sampleRate`,
  `blockSize`, `tempoBPM`, `isPlaying` (P:205-210). Position is never read from `ProcessContext`. Bar-aligned triggering
  needs the processor to fill `projectTimeMusic` / `projectTimeMusicValid` (from `ProcessContext::projectTimeMusic`,
  state flag `kProjectTimeMusicValid`) first.
- Tempo fallback: the processor substitutes 120 BPM when the host sends no tempo (P:195-203), so the DSP's "invalid
  tempo -> use last valid / stop Euclidean+Noise" path (H:539-563) is unreachable from the plugin
  (`tempoBPM > 0 && < 1000` is always true). Spec FR-082a ("stop and go silent when DAW stopped or tempo invalid") is
  therefore not what happens: stopped transport keeps running the pattern.
- Step length is recomputed each block with `max(tempo, 20)` and truncated to whole samples (S:226-233); a step shortening
  makes `stepDurationSamples_ - sampleCounter_` wrap (size_t), which only delays the advance by one call, but it means a
  tempo change does not re-align anything.

### MEDIUM

**M1. The Euclidean scheduler also fires in the other three types, with different outcomes.**
- `scheduler_.process` runs for every type (H:569) and `onPatternTrigger` has an explicit GranularScatter branch
  (H:677-680), so under Granular the Euclidean hits ADD 200 ms random-offset slices (full buffer range, no jitter control)
  to the Poisson grains. Steps/Hits/Rotation/Rate and Slice Length have an audible effect in Granular, undocumented.
- Under HarmonicDrones and NoiseBursts the triggered slices are allocated into `activeSlices_` but nothing ever plays or
  frees them (`processHarmonicDrones` / `processNoiseBursts` never touch `activeSlices_`). The 8-slot array fills, then
  recycling takes slot 0 forever (H:649-663: all positions are 0, so `maxPos` never updates and `oldestIdx` stays 0).
  When the user switches back to Euclidean or Granular, the seven stale slices (audio from the drone/noise period) start
  from position 0 simultaneously at `1/sqrt(8)` each (H:724-757): a burst at the switch. Inference for the audible result;
  the leak and the replay path are in the code.

**M2. Granular density is ~1.8x the knob and clustered.**
- H:776-790: after each trigger the accumulator is reduced by 1 and then INCREASED by `-ln(u) * 0.5` (mean +0.5). With
  E[max(0, 1 - 0.5 E)], E ~ Exp(1) = 0.568, the mean inter-grain time is ~0.568 / density: 10 Hz (default) gives about
  17.6 grains/s (derived). When the added term exceeds 1 (P = e^-2 = 13.5 %), the accumulator is still >= 1 on the next
  sample and a grain fires every sample until it is spent: bursts of coincident grains. Spec FR-039a / SC-003 say the
  average must equal the target +/-20 %.
- Grains read forward and are the only type that uses the whole buffer: offset uniform in `[0, jitter * (avail -
  grain)]` (H:866-868), with the default 50 % jitter reaching ~2.9 s back of a ~5.9 s ring (capacity is
  `nextPowerOf2(5 s * sr)` = 262144 samples at 44.1 kHz, R:79-83). Position Jitter = 0 reads the newest input (a near-live
  copy).
- The probe's peak of +2.3 dB for Granular (before.txt:5) shows several grains coincide at full gain (inference).

**M3. Noise Bursts is not the "noise independent of the captured audio" the spec describes.**
- Spec US4 AC3 / SC-011: noise is generated even with no audio / an empty buffer. The code returns early unless the ring
  holds 200 ms (H:975), and starts a burst only if a sampled peak of the newest slice exceeds 0.01 (H:994-1013). Silence in
  = no burst out.
- A burst plays a random-offset slice of the ring REVERSED (H4) plus noise: `(slice + noise * max(|slice|, 0.1) * 0.5) *
  envelope` (H:1043-1050), so the noise is amplitude-modulated by the instantaneous slice level and never louder than half
  of it. The Pink noise generator is mono; the filter (`filterL_` only, `filterR_` is prepared but never used) gets
  `setType` and `setCutoff` called every sample (H:1039-1041). Burst timing uses its own seconds accumulator reset to 0 on
  every burst (H:987-991), not the scheduler: 200 ms bursts on 250 ms intervals -> 80 % active, matching before.txt:9.
- The Filter Sweep scales the cutoff down by `1 - sweep * progress` over the burst (H:1030-1036); Type/Cutoff/Sweep are
  audible, Colour is audible (Pink default, `setNoiseColor` H:444-482).

**M4. Harmonic Drones pulses instead of sustaining, and Voices/Interval/Drift act on a looped fragment.**
- The "drone slice" is one 200 ms (= `sliceLengthMs_`) loop with the shared envelope (H:939): 10 ms attack, 100 ms
  release at defaults, so the amplitude falls for half of every loop and restarts at the wrap: a ~5 Hz pulse
  (8820 samples) on top of the block loop of H4. Spec US3 describes a sustained pad.
- Voice n is `n * interval` semitones up via a rate change of the age read (H:911-933, 4 voices max, gain
  `1/sqrt(voiceCount)` H:900); Drift is +/-50 cents x depth, LFO per voice (H:916-927), applied to voice 0 too. Cost: two
  `std::pow` and one `std::sin` per voice per sample plus `extractSlice` per voice per sample (inference: not measured).
- The probe's -7.8 dB / 99 % active for Drones is consistent with 2 voices x 0.707 gain, duty ~1 and envelope energy 0.656
  (-1.8 dB) minus the 6 dB mix, i.e. it is a level match, not evidence of correct behaviour.

**M5. Slice Mode "Variable" is not implemented (spec table says MET).**
- `sliceMode_` is stored (H:327, 1150) and pushed every block (P:773-774) but read nowhere else in the DSP (grep: only the
  setter, getter, member). The UI control (Fixed/Variable segment, panel spec 2.2) therefore has no audible effect in any
  mode. Archived spec FR-011 is marked "MET: Variable mode support in slice triggering"
  (`specs/_archive_/069-pattern-freeze/spec.md:398`): refuted by the code. Edge case line 126 of that spec ("Variable on a
  pattern that does not support it falls back to Fixed") was the only part honoured, trivially.

**M6. Slices are block-quantised; the effective delay depends on the host buffer size.**
- Scheduler callbacks run after the whole block has been written (H:529-532, 569), so a slice is cut from the ring head at
  the END of the block and played from output sample 0 of the same block (H:743-757), whatever sample the step fell on.
  Delay of a Euclidean echo = `sliceSamples - blockSize` = 8820 - 512 = 8308 samples = **188.4 ms** in the probe, 154 ms at
  a 2048-sample host buffer, and the hit time is quantised to the block (jitter up to N samples).
- Probe lag 174.8 ms vs 188.4 ms derived: the lag search steps 21 samples against an 880 Hz tone whose period is 50.1
  samples; 12 periods = 601 samples = 600 samples of the 7708 vs 8308 difference. So the reported "best lag" is a
  period alias of the true delay (inference; consistent to within one sample step). The probe should correlate a
  non-periodic input (noise or a click train) so the lag is unambiguous.
- Gain compensation uses `activeSliceCount_` per block (H:724, 768): when an overlapping slice ends, the count drops at a
  block boundary and the survivor's gain steps up by up to sqrt(2) mid-slice.

**M7. The mix is not "frozen replaces dry".**
- The class header says input is muted while frozen (H:64) and spec US1 AC3 / FR-083/084 describe engage/disengage. The code
  always keeps `left[i] * (1 - mix) + wet * mix` (H:585-591) with the processor's default 50 % (F:34, registered default
  `0.5`, F:267-273). Dry never goes away below 100 % mix, so a faint echo sits under a full-level input.

**M8. Entering Freeze mode starts from an empty ring.**
- `processMode` runs only for the current mode (and the previous one during the 50 ms mode crossfade, P:235-245), so the
  ring is not recording while another mode is active. A mode switch calls `resetMode` -> `patternFreezeMode_.reset()`
  (P:232, 846), clearing the ring (H:182), and `setActive(true)` does the same (P:126). Nothing can play for the first 200 ms
  (`kMinReadyBufferMs`, T:236), and there is no history to freeze from.

### LOW

- **L1. Envelope Shape names do not match the curves.** `Linear` -> Hann table, `Exponential` -> Blackman table
  (H:351-354), shared by every type; attack is the first half of the table, release the second half (H:623-631). Neither
  is linear or RC-exponential as the doc comments (T:162-163) say.
- **L2. `freezeMixSmoother_` is never advanced.** Only `getCurrentValue()` is read (H:548); `setFreezeEnabled(false)` would
  move the target without effect and Granular/Drones/Noise do not check `freezeEnabled_` (only `onPatternTrigger` does,
  H:641). Unreachable from the plugin (always true).
- **L3. Dead or duplicated members**: `filterR_` (prepared, never used), `previousPatternType_` (set, never read),
  `Slice::envelopePhase` (set, never read; spec FR-087 wants stealing by highest `envelopePhase`; Euclidean steals the
  largest `position` (H:651-656), Granular the shortest remaining (H:831-839)), `isTempoValid()`,
  `getPitchIntervalSemitones` (H:957) duplicating `getIntervalSemitones` (T:96), `plugins/iterum/src/parameters/
  pattern_freeze_params.h` (an unreferenced second copy of the param pack; no include anywhere), the old `FreezeMode`
  class (`effects/freeze_mode.h`: included only by its own test, `dsp/lint_all_headers.cpp` and the install list in
  `dsp/CMakeLists.txt:236`; no plugin uses it).
- **L4. Per-block work in the processor**: `setEnvelopeShape` regenerates the 512-entry table every block (P:816, H:354);
  `setNoiseColor` toggles eight noise sources every block (P:801, H:444-482); `setEuclidean*` rebuild the pattern
  every block. None allocates. Cost not measured.
- **L5. Parameter range comments in `plugin_ids.h` are stale**: Noise Color says "0-2 (White, Pink, Brown)" but there are
  eight colours (T:124-133, F:429-434); Euclidean Hits "1-steps" while the DSP accepts 0 (H:305).
- **L6. Spec deviations still marked MET** (`spec.md:386-491`): FR-014 (default `Legacy`: there is no Legacy type, T:27-32;
  default is Euclidean), FR-073/074 (Pitch -> Diffusion -> Filter -> Decay chain and Shimmer/Diffusion/Filter/Decay
  controls: removed in v0.12, `plugin_ids.h:279-281`), SC-007 (Legacy identical to FreezeMode), FR-011 (M5), FR-039a /
  SC-003 (M2), SC-011 (M3), SC-004's drone level.
- **L7. Tests that cannot fail on this bug**: "outputs audio when freeze enabled" asserts `totalRMS > 0` at mix 100 %
  (test_pattern_freeze_mode.cpp:324-366), satisfied by a delayed input; the Granular "produces output" case is the same
  (435-474); the crossfade "click-free" case is `maxSample < 2.0f` (see H5). `approval_tests` has no Freeze golden
  (`plugins/iterum/tests/approval/approved/` holds smoother, applyGain, softClip only).

---------------------------------------------------------------------------------------------------------------------

## 2. Signal path, as implemented

```
host block (N samples, in-place on the output buffers)         P:182-271, P:765-823
  -> setFreezeEnabled(true), all setters every block
  -> PatternFreezeMode::process                                H:523-592
       1. ring.writeStereo for all N samples                   H:529-532   (ALWAYS, raw input)
       2. outputL_/R_ cleared                                  H:535-536
       3. tempo check, effective ctx                           H:539-563
       4. if buffer >= 200 ms (and tempo ok for Euclid/Noise): H:548-573
            scheduler_.process(N, ctx)  -> onPatternTrigger    (cuts slice from ring head, offset 0 / random)
            processActiveSlices(N)      -> per pattern type    H:699-719
       5. crossfade (if a Pattern Type change is pending)      H:577-579, 1072-1098
       6. out = in*(1-mix) + wet*mix                           H:585-591  (mix smoothed 20 ms)
```
Pattern types, where each reads audio from and how:

| Type | Source in the ring | Direction | Trigger | Level | Citation |
|---|---|---|---|---|---|
| Euclidean | newest `sliceLengthMs` at trigger time (offset 0), copied to a pool slice | forward | scheduler step (1/8 note at plugin default) | `1/sqrt(active slices)` x envelope | H:639-696, 722-759 |
| Granular Scatter | random offset in `[0, jitter*(avail-grain)]`, grain = `grainSize` +/-50 % x sizeJitter | forward | own accumulator (~1.8x density) + also the Euclid scheduler (M1) | `1/sqrt(active)` x envelope | H:762-884 |
| Harmonic Drones | per sample, age = `fmod(pos * ratio, L)` | reverse, one block replayed | free-running 200 ms loop, no trigger | `1/sqrt(voices)` x envelope | H:887-954 |
| Noise Bursts | per sample, age = `randomOffset + pos`, + filtered noise | reverse, one block replayed | own seconds accumulator at the Burst Rate; needs input > 0.01 | slice + 0.5 x noise x max(\|slice\|, 0.1) | H:974-1062 |

"Freeze" holds: nothing. The things that persist across blocks are the 5.9 s ring, each pool slice for its 200 ms, the drone
loop position, and (wrongly) the one-block crossfade copy.

Envelope and gain (H:609-636): attack/release in samples from ms, scaled down proportionally if they exceed the slice
(`(len-1)/(a+r)`), attack = first half of the table (0 -> 1), sustain = 1.0, release = second half. Defaults 10 / 100 ms.
Why "-10 dB": see H3 (it is not the envelope or the compensation alone).

Pattern change (H:270-286, 1072-1098): see H5.

---------------------------------------------------------------------------------------------------------------------

## 3. Every parameter the panel shows, and whether it is audible at the plugin defaults

Defaults from `F:32-65` / `registerFreezeParams` (F:262-): type Euclidean, slice 200 ms, mode Fixed, steps 8, hits 3 (normalised
2/7 -> `1 + 0.2857*7 + 0.5` = 3), rotation 0, rate index 10 (1/8), granular 10 Hz / jitter 0.5 / 0.25 / 100 ms,
drones 2 voices / Octave / drift 0.3 / 0.5 Hz, noise Pink / rate index 10 / LowPass / 2 kHz / sweep 0.5, attack 10 ms,
release 100 ms, shape Linear, mix 50 %.

| ID | Control | Audible at defaults (Euclidean)? | Notes |
|---|---|---|---|
| 1012 Mix | core knob | yes, sets dry/wet | dry is never muted (M7) |
| 1015 Pattern Type | core menu | selects variant | change triggers the H5 crossfade |
| 1016 Slice Length | core knob | yes | sets echo length AND (with block size) delay, 188 ms at 200 ms |
| 1017 Slice Mode | SLICE segment | **no, never** | stored only (M5) |
| 1020 Steps | EUCLIDEAN | yes (cycle length) | 2-32 |
| 1021 Hits | EUCLIDEAN | yes | 1..steps in the UI; DSP clamps 0..steps |
| 1022 Rotation | EUCLIDEAN | shifts which steps hit | 0 at default |
| 1023 Pattern Rate | EUCLIDEAN menu | yes, sets step length | 30 dropdown entries (kNoteValueDropdownCount = 30), 1/8 at default |
| 1030-1033 Granular Density/Pos Jitter/Size Jitter/Grain Size | GRANULAR | only when type = Granular | and Density is ~1.8x (M2); Steps/Rate/Slice also colour Granular (M1) |
| 1040-1043 Voices/Interval/Drift/Drift Rate | DRONES | only when type = Drones | applied to a block-loop (H4) |
| 1050-1054 Colour/Burst Rate/Filter Type/Cutoff/Sweep | NOISE BURSTS | only when type = Noise | needs input signal (M3) |
| 1060 Attack | ENVELOPE | yes but 10 ms is inaudible | |
| 1061 Release | ENVELOPE | yes; at 100 ms of a 200 ms slice half of every echo is a fade | |
| 1062 Envelope Shape | ENVELOPE | barely (Hann vs Blackman table) | L1 |

At defaults the audible controls are Mix, Slice Length, Steps/Hits/Rotation/Rate, Attack/Release; everything else is inert or
belongs to another variant. No control changes WHICH audio is played (no position, no hold).

---------------------------------------------------------------------------------------------------------------------

## 4. The state block and where a new field goes

- The stream has no version field. `Processor::getState` (P:~321-353) writes gain, mode, then the packs in a fixed order
  (Granular, Spectral, **Freeze (P:343)**, Reverse, Shimmer, Tape, BBD, Digital, PingPong, MultiTap) and then the two
  appended tails `saveTapeSyncParams` and `saveMultiTapLengthParams` (the latter is the last, P:~352). `setState` reads in the
  same order (Freeze P:378; tails P:~387-388) and every `readX` that hits end-of-stream leaves the atomic at its initial value
  (F:629-696 pattern: `if (streamer.readFloat(v)) store`).
- The Freeze block itself (F:588-627) is 26 fields (floats and int32) in the order dryWet, patternType, sliceLengthMs,
  sliceMode, steps, hits, rotation, rate, 4 granular, 4 drone, 5 noise, attack, release, shape. **A new Freeze field cannot
  be added inside it** (every later mode's block would shift). It goes at the very END: a new `saveFreeze...Params`
  called after `saveMultiTapLengthParams` (P:~352) and a matching `loadFreeze...Params` after
  `loadMultiTapLengthParams` (P:~388).
- Controller mirror, as the Tape-sync and MultiTap-Length tails did: `syncFreezeParamsToController` is called from
  `setComponentState` (`controller.cpp:292`; the tails at :300-301), and from `loadComponentStateWithNotify`
  (`controller.cpp:1376`; tails :1384-1385) via `loadFreezeParamsToController` (F ~830-870). A new tail needs both
  an `sync...ToController` and a `load...ToController` function, called after the existing tails.
- IDs: block 1000-1099 (`kFreezeEndId = 1099`, `plugin_ids.h:323`). Used: 1012, 1015-1017, 1020-1023, 1030-1033,
  1040-1043, 1050-1054, 1060-1062. 1070-1099 are free (as are 1013-1014, 1018-1019, 1024-1029, 1034-1039, ...; 1001-1011 and
  1013-1014 are noted as removed legacy ids, `plugin_ids.h:279-281`, so do not reuse them for a different meaning). Register in
  `registerFreezeParams` (F:262), denormalise in `handleFreezeParamChange` (F:71), format in `formatFreezeParam`
  (F:~490), push from the Freeze branch (P:765-823).
- What an OLD preset/project loads as: states and the 10 factory `.vstpreset` files in
  `plugins/iterum/resources/presets/Freeze/` (Euclidean_Pulse, Fifths_Drone, Ghost_Pattern, Granular_Cloud, Harmonic_Bed,
  Noise_Texture, Radio_Static, Scatter_Drift, Slice_Machine, Total_Freeze) lack the new tail, so the processor atomics keep
  their initial `FreezeParams` values and the controller keeps the registered default. A new field must therefore have an
  initial value that makes an old preset sound as before, OR the design must state that old presets pick up the new default
  (e.g. an audible new default would change them). The existing roundtrip test is
  `preset_loading_consistency_test.cpp:137-148, 400`.
- Existing-parameter semantics that must stay: IDs, types and ranges (user ruling); the Hits denormalisation depends on the
  Steps value at the time of the change (F:119-125), so Hits is only recomputed when its own value arrives.

---------------------------------------------------------------------------------------------------------------------

## 5. The probe's before table, line by line

Probe: 220 Hz tone whose frequency doubles every second (220 -> 440 -> 880 -> 220), amplitude 0.5, mono-in-stereo, 6 s,
block 512, 120 BPM, mix 50 %, slice 200 ms, 8 steps / 3 hits, DSP defaults otherwise (so 1/16 steps; see H2). Wet = `out -
0.5 in`; level and activity over 10 ms windows from 1.0 s; lag by correlation within 0..600 ms in 21-sample steps over
seconds 2-3 (probe-case.cpp.txt).

| Line | Row | Code explanation |
|---|---|---|
| 3 | Euclidean, playing=1: wet -10.1 dB, peak -5.7 dB, active 59 %, lag 174.8 ms r = 0.69 | -10.1 = -2.22 (3 x 200 ms of 1000 ms) -1.83 (envelope energy) -6.02 (50 % mix, wet defined after the mix gain); peak -5.7 ~ -6.02 (0.5 x slice at envelope 1.0) plus window-phase ripple of a 10 ms window on a tone (inference); active 59 % ~ 60 % duty (steps are 5512 samples at 1/16, hits at 0, 375, 750 ms of 1000); lag = the echo of the newest 200 ms (derived true delay 8820 - 512 = 188.4 ms; reported 174.8 is a period alias, M6); r = 0.69 because 41 % of the window is silent and the envelope reshapes the rest (inference) |
| 4 | Euclidean, playing=0 | identical: `isPlaying` is not read anywhere in the DSP (H6) |
| 5-6 | GranularScatter: -9.5 dB, peak +2.3 dB, active 98 %, lag 286.2 ms r = 0.23 | ~17.6 grains/s x 100 ms (M2) plus the Euclid-scheduler 200 ms slices (M1) keep the wet on 98 % of windows; coincident grains and slices give a peak above the input (inference); random offsets over the ring leave no dominant lag |
| 7-8 | HarmonicDrones: -7.8 dB, peak -3.2 dB, active 99 %, lag 39.5 ms r = 0.03 | free-running loop with duty ~1: 2 voices x 1/sqrt2, envelope energy 0.656 (-1.83 dB), mix -6.02 -> about -7.8 (arithmetic match); r = 0.03 because the signal is a reversed block loop (H4) |
| 9-10 | NoiseBursts: -8.8 dB, peak -5.5 dB, active 80 %, lag 499 ms r = 0.05 | 200 ms bursts every 250 ms (1/8 at 120 BPM): 200/250 = 80 %; level from slice x envelope plus the +/-0.5 x noise term; r = 0.05: random offset + reversed + noise, nothing to correlate with (the 499 ms "best lag" is noise) |
| 2, 11-14 | header, Catch2 footer | seed `2039407612` is irrelevant (the probe has no randomness other than the DSP's fixed `Xorshift32{12345}`, H:1138); assertions: none |

What the table does not show: the plugin-default step length (H2), the true delay (M6), and any behaviour that depends on
block size (H4). A usable "after" probe needs: `setNoteValue(Eighth)` (or push the processor's values), a non-periodic input,
a second block size, and a held-snapshot metric (correlation of the wet with the input at the capture moment rather than a
sliding lag).

---------------------------------------------------------------------------------------------------------------------

## 6. Seed facts in `.claude/workflows/iterum-freeze.js` (header comment), confirmed or refuted

| # | Seed | Verdict | Evidence |
|---|---|---|---|
| 1 | "Freeze never freezes"; ring rolls (FR-004); nothing takes a snapshot; offset 0 = last N samples; slice played at once | **CONFIRMED** | H:529-532, 675-682; R:141-170; H1 |
| 1a | "archived spec described a Trigger button and a captured slice that is held" | **PARTLY CONFIRMED**: the Trigger button appears only inside a clarification question (`spec.md:136`), not in any FR; "freeze engaged / disengaged" is in FR-083/084 and US1 AC3; a HELD slice is implied by FR-004 ("capture new audio for potential re-capture") and SC-001, never stated as an FR | spec.md:136, 144-148, 269-270, 300 |
| 1b | processor makes freeze "always active" (processor.cpp Freeze branch) | **CONFIRMED** | P:766-767 |
| 2 | Drones and Noise Bursts play backwards (extractSlice with growing offset); Drones add a modulo wrap | **CONFIRMED**, and stronger: one host block is replayed reversed every block | H:931-936, 1023; R:161; `scratchpad/idx.js` output; H4 |
| 3 | Scheduler free-runs, never phased to `projectTimeMusic`, `isPlaying` ignored, invalid tempo falls back to the last | **CONFIRMED**; but the premise that the context carries `projectTimeMusic` from the host is **REFUTED for Iterum** (processor never fills it), and the DSP's invalid-tempo path is unreachable (processor supplies 120) | S:199-217; H:539-563; P:195-210 |
| 4 | Every Euclidean hit plays the same material (always the newest slice) | **CONFIRMED** | H:675-682 (`step` unused, H:639) |
| 5 | Existing tests prove nothing about the effect (RMS > 0 satisfied by a delayed input) | **CONFIRMED** (and the crossfade and Granular cases are equally weak) | test_pattern_freeze_mode.cpp:324-366, 435-474, 652-728; L7 |
| 6 | Wet level about -10 dB re input at defaults (1/sqrt(n) gain compensation, 10 ms attack, 50 % mix) | **REFUTED as stated**: compensation is 1.0, the 10 ms attack is negligible (0.04 dB); the -10 dB is duty -2.2 + envelope -1.8 + the probe's post-mix definition -6.0; and the probe used 1/16 steps, not the plugin's 1/8 (H2), so the plugin figure is about -13 dB (derived) | H:724-726; H3; H2 |
| 7 | "Euclidean: r = 0.69 at a 175 ms lag, active 59 % of the time" | **CONFIRMED as a probe reading, MISLEADING as a description**: true delay is 188.4 ms (block-quantised), 175 ms is a period alias; 59 % is the DSP default (1/16), the plugin default gives ~30 % | M6, H2 |
| 8 | Seed in the task: `extractSlice(outL, outR, length, offsetSamples)`: offset = samples BEFORE the newest, slice read forward from offset+length back | **CONFIRMED**: `out[i]` age = `offset + length - 1 - i` (R:359-363) | R:141-170 |
| 9 | "Check whether anything in Iterum still uses FreezeMode (effects/freeze_mode.h)" | **Nothing in any plugin uses it**; only its own test, the header lint and the CMake install list reference it | L3 |

Additional facts the seeds did not have (new, with the finding that carries them): the plugin default step is 1/8 (H2); the
crossfade replays a single stale block (H5); the Euclid scheduler also triggers (and leaks) slices in the other three types
(M1); Granular density is ~1.8x (M2); Slice Mode is dead (M5); the entry into Freeze mode starts with an empty ring (M8);
`processor.cpp` never populates the transport position fields (H6).

---------------------------------------------------------------------------------------------------------------------

## 7. Not verified here (so the design must not assume it)

- The audible character of H4/H5/M1 (derived from verified index arithmetic and code paths, not heard or rendered).
- The Granular density figure (17.6/s) and any DSP number marked "derived" or "inference": they come from closed forms over the
  code, not from a render; the "after" probe should measure them.
- CPU cost of the per-sample filter/pow/sin calls (L4, M3, M4).
- Behaviour at sample rates other than 44.1 kHz and at block sizes other than 512 (only the arithmetic for 128 and 512 was replayed).
