# Freeze: a musician's critique

Written from the chair of a producer who picks the **Freeze** tab (snowflake icon) and expects the delay to freeze or
stutter the audio. I cannot listen. The evidence is the probe table (`specs/iterum-freeze/probe/before.txt`), the four
panel shots (`specs/iterum-freeze/shots/before/*.png`), the archived spec (`specs/_archive_/069-pattern-freeze/spec.md`),
the code, and a scratch probe I compiled against `pattern_freeze_mode.h` (not committed). The scratch probe measured
what the committed probe cannot: the plugin's real default rate, the host-block-size dependence, and which controls
change the output. Line numbers refer to `dsp/include/krate/dsp/effects/pattern_freeze_mode.h` unless a file is named.

**Probe caveat.** `before.txt` runs the DSP class with its own default step rate, 1/16 (`noteValue_ = Sixteenth`,
:1158). It never calls `setNoteValue`. The plugin default is **1/8** (`kNoteValueDefaultIndex = 10`,
`freeze_params.h` `patternRate`). At the plugin's real defaults the Euclidean wet is sparser than the table shows. The
scratch probe measured wet-only output at -6.9 dB re input, present in **31 %** of 10 ms windows (the table says 59 %).

---

## What I hear at defaults, and why it reads as "only the input"

Defaults: Euclidean, 8 steps, 3 hits, 1/8 rate, 200 ms slices, 10 / 100 ms envelope, Mix 50 %, at 120 BPM.

- **For 69 % of every bar I hear the dry signal and nothing else**, at -6 dB because Mix 50 % halves it. The tresillo
  E(3,8) at 1/8 gives three 200 ms windows per 2 s bar (31 % measured).
- **For the other 31 %, the wet is the input I am playing right now, delayed by under 200 ms.** Each hit cuts the newest
  200 ms from the rolling buffer (`onPatternTrigger`, offset 0 at :676, `extractSlice` at :682) and plays it at once. The
  probe correlates the wet against the input at r = 0.69, lag 175 ms. At 50 % mix that copy sits about -13 dB re input,
  under a -6 dB dry. On a pad, chord or vocal it sounds like a faint flam or comb colouring. On drums it sounds like a
  weak slapback that comes and goes.
- **Nothing repeats, so nothing stutters.** A stutter or freeze needs the same fragment played more than once. Here
  every hit cuts a fresh slice from live input, so no slice is ever heard twice.

The user's report ("I only hear the input") is an accurate description of this design, not a bug in a corner of it.

---

## Critical

### C1. Freeze never freezes: Euclidean is a gated short delay of the live input
- **Evidence:** the capture is written every block, unconditionally (:530-532). Every trigger slices from offset 0, the
  newest audio (:675-683); only Granular uses a different offset. There is no snapshot state anywhere in the class. The
  probe shows the wet tracking the input with a fixed ~175 ms lag, at r = 0.69.
- **What it costs:** the tab name, the snowflake icon and the spec's User Story 1 ("captured slices are triggered in the
  classic tresillo rhythm") all promise a held moment played in rhythm. What you get is the input, slightly late,
  through a gate. You cannot freeze a chord and play over it, or catch a word and stutter it.
- **What I would expect:** a capture moment (on entry, on a button, or on the bar). Hits then replay **that** material
  until I recapture, with the dry able to duck or mute while it is held.

### C2. There is no way to hold, capture or retrigger
- **Evidence:** `processor.cpp` Freeze branch calls `setFreezeEnabled(true)` every block ("Pattern Freeze is always
  active ... no checkbox needed"). No parameter in 1000-1099 is a freeze, hold, capture or trigger control
  (`plugin_ids.h` 279-323). The archived spec's clarification describes a "Freeze Mode selector and **Trigger button**".
  User Story 5 / FR-013 describe a Legacy pattern that mutes the input and loops the buffer. Neither exists now. The
  old `FreezeMode` class (`effects/freeze_mode.h`) is not referenced anywhere in `plugins/iterum/src`.
- **What it costs:** the most basic freeze gestures are missing: press to hold, release to let go, automate a capture
  on beat 1, punch a stutter into a fill. A freeze effect without a freeze control is a pattern gate.

### C3. Harmonic Drones and Noise Bursts loop one host buffer: the pitch is the DAW's buffer size
- **Evidence (code):** both patterns read the capture one sample at a time with an age that grows with position:
  `extractSlice(&l, &r, 1, readIdx)` in Drones (:931-936) and `extractSlice(..., 1, offset + position)` in Noise
  (:1023). The whole block is written to the capture **before** these reads (:530). Within a block the write head is
  therefore fixed, and the read walks backwards. At the next block the head jumps forward by exactly the samples just
  consumed, so the read restarts at the same audio. Result: one host block of audio, played reversed and looped at
  the block rate, for each 200 ms slice period.
- **Evidence (measured, scratch probe, wet only):**

  | pattern | block | autocorr at lag = block | spectral peak | fs / block |
  |---|---|---|---|---|
  | Noise Bursts | 64 | 1.00 | 689 Hz | 689 Hz |
  | Noise Bursts | 256 | 0.97 | 172 Hz | 172 Hz |
  | Noise Bursts | 512 | 0.90 | 258 Hz (3rd harmonic of 86) | 86 Hz |
  | Drones, 1 voice, unison, no drift | 128 / 512 | 1.000 / 1.000 | - | - |
  | Drones, defaults (2 voices, octave) | 64 | 0.34 | 689 Hz | 689 Hz |

- **What it costs:** Drones produce a buzz whose pitch is set by the host's buffer setting (86 Hz at 512, 689 Hz at 64),
  with the material re-cut every 200 ms. The same project sounds different at a different latency setting. The
  "octave" voice plays at age rate 2, which comes out as reverse playback at the **original** pitch, not an octave up
  (spec US3: "one at original pitch and one an octave higher"). A modulo wrap (:932) also cuts it mid-slice with no
  envelope, which clicks at 5 Hz. None of this is a drone: nothing sustains, and the source keeps following the last
  200 ms of input.
- **Noise Bursts** gives the same block-loop buzz, with a quiet noise layer. It is **silent when the input is silent**
  (measured: wet RMS 0 with silent input). The burst is gated on captured level > 0.01 (:1005). Spec SC-011 promised
  "noise generated independently". The two "named" sound-design modes do not deliver their names.

---

## High

### H1. Cannot lock to the bar; Rotation has nothing to rotate against
- **Evidence:** `PatternScheduler::process` counts samples from its last `reset()` (`pattern_scheduler.h` 187-218). It
  never reads `projectTimeMusic` or the transport position. `reset()` runs when the mode is entered
  (`processor.cpp` 232 -> `resetMode` -> `patternFreezeMode_.reset()` -> `scheduler_.reset()`, :184). The probe gives
  identical numbers with the transport playing and stopped.
- **What it costs:** step 1 lands wherever I happened to click the Freeze tab, not on the downbeat. Rotation then only
  moves a free-running pattern in time. Measured: rotation 3 is rotation 0 shifted by 1487 ms (r = 0.66), the same
  rhythm with an arbitrary phase I cannot hear against the bar. A Euclidean pattern without a downbeat is a random
  pulse train.
- **Timing slop on top:** the trigger callback carries no sample offset (`checkAndTrigger`, `pattern_scheduler.h`
  236-241), and every new slice renders from output index 0 (`processEuclideanSlices` loop from `i = 0`, :743). A hit
  that falls mid-block therefore plays up to one block early: 11.6 ms at 512, 46 ms at 2048. Spec SC-002 promised
  under 10 ms.
- **What works:** it runs with the transport stopped, as long as the host reports a tempo. That is useful for sound
  design and should stay.

### H2. Hidden controls are live in modes that hide them
- **Evidence:** `scheduler_.process` runs for **every** pattern type (:569). The trigger always allocates a slice
  (`onPatternTrigger`). In Granular, that slice plays alongside the grains, taken from a random point anywhere in the
  last 5 s (:677-680, ignoring Position Jitter). Measured: in Granular, Euclidean Hits 3 vs 0 changes the output (max
  |diff| 0.89; RMS 0.110 -> 0.120). The panel hides Steps, Hits, Rotation and Pattern Rate in Granular (shot
  `freeze-granular.png`), yet they shape the sound.
- In Drones and Noise Bursts the triggers fill the 8-slice pool with slices that are never rendered
  (`processHarmonicDrones` / `processNoiseBursts` never touch `activeSlices_`). Switching back to Euclidean or Granular
  then starts those stale slices together.
- **What it costs:** a preset that sounds one way in Granular changes when someone edits a control they cannot see.

### H3. Controls that change nothing, or something other than their label
Of the 24 Freeze controls:

| control | what it actually does | evidence |
|---|---|---|
| **Slice Mode** (Fixed / Variable) | nothing | stored at :327, never read; measured max \|Fixed - Variable\| = 0. Spec FR-011 is marked MET. |
| **Rotation** | shifts a free-running pattern; inaudible without a bar reference | H1, measured shift-match r = 0.66 |
| **Envelope Shape** (Linear / Exponential) | Hann vs Blackman window halves, both bell curves; neither is linear nor exponential | :351-354 |
| **Drone Interval** | changes the reverse-read speed of voices 2-4 (3rd -> 0.19x, 5th -> 0.50x, octave -> 1.0x reversed); no interval is ever produced | :912-936 |
| **Drone Drift / Drift Rate** | detunes the read rate inside a block loop by +/-0.9 % at 30 %: no audible pitch drift | :916-927, C3 |
| **Noise Color** | works, but Blue, Violet, Grey, Velvet and Radio sit at the generator's -20 dB default while White / Pink / Brown are set to -12 dB (:152-154), so five of the eight colours are 8 dB quieter | `noise_generator.h` `kDefaultLevelDb = -20` |
| **Noise Filter Type / Cutoff / Sweep** | audible, but only on the noise layer, which sits under the block-loop buzz; the noise is mono (`filterR_` is prepared but never processed) | :1026-1050 |
| **Euclidean Hits readout** | after a Steps change it shows a different number than the DSP plays (Steps 8 -> 16: readout 5, DSP still 3) | `freeze_params.h` hits handler stores an absolute count; `formatFreezeParam` recounts against the new Steps; `controller.cpp` 605-611 only refreshes the text |
| **Slice Length** (core hero knob) | in Granular it sets the length of the hidden Euclidean slices, not the grains (Grain Size does that) | H2 |

These work as labelled: Mix, Pattern, Steps, Hits (when touched), Pattern Rate, Attack, Release, Density (see M3),
Position Jitter, Size Jitter, Grain Size, Voices (changes the gain and adds reverse voices), and Burst Rate.

---

## Medium

### M1. Mix 50 % with the dry at full parity buries the effect
The dry never ducks (:585-591). When the wet is a near-copy of the dry (C1), equal parts cannot be told apart. A
stutter or freeze is normally heard instead of the input, or on top of a ducked input. The default should let the
effect lead. A hold should be able to mute the input (the old Legacy behaviour, spec US5).

### M2. Changing Pattern plays a frozen one-block snapshot for 500 ms
`setPatternType` copies the last output block once (:279-282). `processCrossfade` then reads `crossfadeOutL_[i]` with
`i` restarting at 0 every block (:1088), so the "old pattern" fading out is one block repeated at the block rate.
Measured on a Drones -> Euclidean switch: autocorrelation 0.56 at lag = block across the crossfade. The measurement
is confounded by the stale slices of H2 starting at the same moment, but the code is unambiguous. The audible result
is a buzzy tail on every pattern change.

### M3. Granular density is not what the readout says
After each grain the accumulator gets `-log(u) * 0.5` **added** (:785), which pulls the next grain earlier. My
estimate from the distribution: the mean interval is about 0.57 / density, so about 1.8x the set rate. In about 13 %
of grains a second grain follows on the next sample. This is not the Poisson process of FR-039a. It is a code-derived
estimate, not measured.

### M4. The archived spec says everything was delivered
The 069 compliance table marks **MET** for FR-011 (Variable slices), FR-073 / FR-074 (pitch, diffusion, filter and
decay chain after the pattern; no such parameters exist in 1000-1099), SC-002 (< 10 ms timing), SC-011 (noise with
an empty buffer) and US3 (octave-up voice). None of them hold. Anyone reading the spec to learn what Freeze does is
misled. The seed fact applies here too: the existing test "outputs audio when freeze enabled" only checks RMS > 0,
which a delayed input passes.

---

## Low

- **CPU waste (inaudible):** the processor calls `setEnvelopeShape` every block, which regenerates the 512-point table
  each time (:348-355). `setNoiseColor` toggles eight enable flags every block. Drones call `std::pow` per voice per
  sample (:913, :926). The noise filter has `setType` / `setCutoff` called per sample (:1039-1040).
- **Euclidean gain compensation** is computed once per block from the slice count (:724-726). When slices overlap
  (1/16 steps with 200 ms slices), the first slice's gain steps down at a block boundary. This does not occur at the
  1/8 default.
- **Entering the mode** resets the capture (`processor.cpp` 232), so the first 200 ms after selecting Freeze is
  always dry. That is correct for "no ghost audio", but it means you cannot freeze what played just before the
  switch.

---

## What the panel implies that the DSP does not deliver
(shots `freeze-euclidean.png`, `freeze-granular.png`, `freeze-drones.png`, `freeze-noise.png`)

- The **Freeze** tab with its snowflake implies a held sound. There is no hold, capture or trigger control, and no
  indicator of what has been captured (C1, C2).
- The **SLICE** group holds one control, the dead Slice Mode, above a large empty area. That is the panel's biggest
  box, given to a no-op (H3).
- **Slice Length** is the core's hero knob in all four variants, but in Granular the grains follow Grain Size (H2,
  H3).
- **EUCLIDEAN** shows Rotation as if a downbeat existed (H1), and shows no step display. Steps and Hits are bare
  sliders, so I cannot see the rhythm I set. After a Steps change, the Hits readout disagrees with the DSP (H3).
- **HARMONIC DRONES** offers musical intervals and drift. The DSP delivers neither (C3).
- **NOISE BURSTS** offers a full noise-and-filter section. The audible result is dominated by the block-loop buzz,
  and the section falls silent when the input stops (C3).

---

## What is fine

- **Granular Scatter is the one pattern that does roughly what its name says.** It cuts real forward grains from a
  random point in the last ~2.5 s (Position Jitter 50 % of the 5 s buffer, :863-871) and plays them with an envelope.
  The probe shows 98 % activity and a low input correlation (r = 0.23), i.e. a scattered texture rather than a copy.
  It still never holds a moment, and it carries the hidden Euclidean slices (H2), but it is usable now.
- **The Euclidean engine itself is sound.** Bjorklund generation gives the tresillo for 3 of 8. Step length follows
  host tempo and the rate dropdown, the tempo fallback keeps it running, and Attack and Release shape the slices
  audibly and click-free at the defaults.
- **It runs with the transport stopped**, as long as the host reports a tempo. Keep that.
- **Continuous 5 s capture with allocation in `prepare()`, a mode-entry reset, and a ~500 ms pattern crossfade** are
  the right building blocks. What is missing is the moment of capture and holding on to it.
- **The panel layout is clean and consistent with the other modes.** The variant switching works, and the readouts
  carry units.

---

## Ranking in one line each

1. C1: no captured moment, so Euclidean is a gated echo of the live input. At defaults it is the input.
2. C2: no hold, capture or retrigger control.
3. C3: Drones and Noise loop one host block. The pitch depends on the DAW buffer, no intervals, no drone, Noise goes
   silent with silent input.
4. H1: no bar lock; Rotation is meaningless; hits up to one block early.
5. H2: hidden Euclidean controls shape Granular; stale slices from Drones and Noise.
6. H3: Slice Mode dead; Envelope Shape, Interval and Drift mislabelled; Hits readout wrong after a Steps change.
7. M1-M4: dry-heavy default mix, a buzzing pattern crossfade, Granular density about 1.8x, a spec that claims all of
   it was delivered.

## Seed facts, checked afterwards
- "Freeze never freezes" and "every hit plays the newest slice": **confirmed** (C1).
- "Drones and Noise play backwards": **confirmed but incomplete.** Because the block is written before the per-sample
  reads, they play backwards inside a block and rewind at every block. The result is a block-rate loop whose pitch is
  the host buffer size (C3, measured). The seed does not mention this, and it is the bigger problem.
- "Scheduler free-runs, isPlaying ignored": **confirmed.** I would add that it restarts at mode entry and that hits
  play up to a block early (H1).
- "Wet about -10 dB at defaults": **true for the probe's 1/16 rate only.** At the plugin's 1/8 default the wet is
  present 31 % of the time, not 59 % (probe caveat above).
- **Not in the seeds:** the hidden-control coupling (H2), the dead Slice Mode (H3), the Hits readout mismatch (H3),
  the frozen-snapshot crossfade (M2), the Granular density error (M3), and Noise Bursts going silent with silent
  input (C3).
