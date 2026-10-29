# Iterum Freeze: design

Status: design stage output (2026-10-29), revised the same day against 20 challenges (section 9), then reconciled
the same day against the user's rulings R1-R7 and the seventeen open challenge findings of `open-items.md`.
Section 8 records every ruling and what it changed. The reconcile left one item open (the drum-window bar of
finding 10) and its implementability check raised seven more; the user ruled them R8-R10 on 2026-10-29 and the rest
were applied in place (8.1, open-items.md). Nothing is open: section 8.4.
Inputs: `research/code-facts.md`, `research/critique.md`, `research/prior-art.md`, `research/consequences.md`,
`research/drone-source-sim.js` (added by the reconcile), `probe/before.txt`, `shots/before/*.png`, the archived spec
`specs/_archive_/069-pattern-freeze/spec.md`, the DSP and plugin sources, `specs/iterum-ui-redesign/design-system.md`
and `panels/freeze.md`. File:line references were read in the working tree of branch `ui/iterum-consistency`.

Shorthands: `H` = `dsp/include/krate/dsp/effects/pattern_freeze_mode.h`, `S` = `.../processors/pattern_scheduler.h`,
`T` = `.../core/pattern_freeze_types.h`, `GE` = `.../core/grain_envelope.h`,
`PS` = `.../processors/pitch_shift_processor.h`, `SM` = `.../primitives/smoother.h`,
`P` = `plugins/iterum/src/processor/processor.cpp`, `F` = `plugins/iterum/src/parameters/freeze_params.h`,
`C` = `plugins/iterum/src/controller/controller.cpp`.

## 0. The design in one paragraph

Freeze becomes a bar-synchronised re-sampler with a real hold. Every **Capture** cycle (a note value, default 1/1 =
four beats, phased to the host's song position, or to a free-running clock when the transport is stopped or gives no
position) is recorded into its own buffer from its first sample; at the next cycle boundary that buffer becomes the
**window** the four pattern types play from. The window's sample 0 is always the cycle's downbeat, and its length is
exactly the samples that elapsed, so nothing depends on the host buffer size, on rounding or on how old the audio is.
The pattern types never track the live input. Freeze follows its input. A silent bar replaces the window like any
other bar, so the effect falls silent with its input one cycle later, and a silent window never ducks the dry (it
counts as no window for every trigger and for the duck). Keeping the music is what **Hold** is for (R1). Hold is a
toggle beside Mix, automatable, so a MIDI controller makes it momentary. It stops re-capture, so the window that is
playing stays, while the input keeps being recorded underneath. Under Insert it also mutes the input while it is on,
and each Euclidean slice or noise burst sustains until the next hit replaces it (R5), so pressing Hold freezes the
sound into a continuous held bar with the rhythm on top. Releasing Hold under Insert lets the live input back at once,
and the next capture boundary replaces the window. **Slice Mode** gets its meaning. Fixed replays the window's
downbeat. Variable plays the slice at the current musical position of the window, so Rotation and Hits are audible as
a rhythm over the held bar. A new **Dry** parameter adds Beat Repeat's Insert law: the dry is ducked only while a
slice, burst or drone of non-silent material plays (Granular leaves it alone). A fresh instance starts at Mix 100 % +
Insert, so the music keeps going and three times per bar the held downbeat punches in. That is a stutter a producer
hears at once, with the transport running or stopped; section 6.2 measures it on three inputs (R6). The other types
are fixed too:
- Drones become one shared, decorrelated read of the whole window (four aligned, Hann-windowed heads) that feeds
  one Shimmer pitch shifter (`PitchShiftProcessor`, PitchSync) per upper voice (R2).
- Noise Bursts play noise on silence too, but only after the first music and only while the transport plays or
  within two capture cycles of the last input (finding 12).
- Granular's Poisson clock averages its Density; Slice Length sets its smallest scatter region and Position Jitter
  widens it to the whole window.
- Every read takes contiguous audio across a window edge where it exists and is clamped or crossfaded where it does
  not, so no read splices under a non-zero envelope.
- The pattern-change crossfade no longer replays a stale block.
- The Envelope Shape curves are the ones the labels say, and both end at exactly 0.
- The Hits readout agrees with the DSP.

Exception to "falls silent with its input": a Noise Bursts instance keeps playing noise while the transport plays,
after the first music (2.5.4, 2.9). Every mode reports an infinite tail (R3).

Three new parameters (1070, 1071, 1072), one state tail of three int32 after the MultiTap Length field, no
ID/type/range change, no golden changes. Slice Mode keeps its fresh default Fixed unless the sustained-chord row of
6.2 misses its bar with Fixed; then the fresh default becomes Variable (R6; ID, type and range unchanged).

---

## 1. Problems addressed

Each row names the research evidence. "Fix" points to the section that specifies it.

| # | Problem | Evidence | Fix |
|---|---|---|---|
| 1 | Nothing is held: every Euclidean hit re-cuts the newest 200 ms of input, so the wet is the live input ~188 ms late and at steady material the user hears only the input | code-facts H1 (H:529-532, 675-683), critique C1, consequences F1/F2, `probe/before.txt:3` (r = 0.69 at 175 ms) | 2.1, 2.2: each capture cycle is recorded into its own buffer and played as the window during the next cycle |
| 2 | No hold / capture / retrigger control; the processor forces `setFreezeEnabled(true)` every block | critique C2, code-facts H1 (P:766-767), prior-art HIGH 1/2 (Clouds, Beat Repeat, Blackhole all have a hold switch) | 2.3, 3.1: `Hold` (1070), audible at its edge under Insert |
| 3 | Harmonic Drones and Noise Bursts replay one host block reversed: a buzz at fs / blockSize (689 Hz at 64, 86 Hz at 512); Drones' "octave" is reverse playback at the original pitch | code-facts H4, critique C3 (measured autocorrelation 0.90-1.00 at lag = block), consequences F3 | 2.5.3, 2.5.4: forward reads from the window, a pitch shifter per upper voice (R2), block-size invariance test |
| 4 | Noise Bursts is silent when the input is silent (gate on captured level > 0.01), noise amplitude-modulated by the slice, five of eight colours 8 dB quieter | critique C3 (measured wet 0 on silent input), code-facts M3 | 2.5.4 |
| 5 | The pattern is not phased to the bar: step 0 is wherever the mode was entered; Rotation is inaudible; hits land up to one block early; `isPlaying` / `projectTimeMusic` never reach the DSP | code-facts H6, critique H1, consequences F5 | 4 |
| 6 | The Euclidean scheduler fires in all four types: hidden Steps/Hits/Rate change Granular (measured max diff 0.89); in Drones/Noise the slices leak into the 8-slot pool and burst out on the next switch | critique H2, code-facts M1 | 2.5: the hit callback is honoured in Euclidean only; the other types never allocate from the Euclidean trigger |
| 7 | Slice Mode does nothing (stored, never read; measured difference 0) | code-facts M5, critique H3, consequences F4 | 2.4: Fixed / Variable select the slice position |
| 8 | Dry at full parity buries the wet: `in·(1-m) + wet·m` at m = 0.5 with no ducking | critique M1, consequences section 5, prior-art HIGH 3 (Beat Repeat Insert) | 2.7, 3.1: `Dry` (1072) = Mix / Insert; fresh default Mix 100 % + Insert |
| 9 | Pattern-type change replays one stale 512-sample block for 500 ms (`crossfadeOutL_` copied once, read from index 0 every block) | code-facts H5, critique M2, consequences F7 | 2.6: slices finish, the drone/burst release; no stored block |
| 10 | Granular Density is ~1.8x the knob and clustered (`-ln(u)·0.5` added after a trigger) | code-facts M2, critique M3 | 2.5.2 |
| 11 | Envelope Shape "Linear" is a Hann half, "Exponential" a Blackman half; the GrainEnvelope Linear/Exponential types written for spec 069 are unused | code-facts L1, critique H3, `GE:97-150` | 2.8 |
| 12 | The Hits readout disagrees with the DSP after a Steps change (readout recounts, the atomic keeps the old absolute value) | critique H3 (F:119-125, C:605-611) | 3.4 |
| 13 | Drones pulse at the slice rate (the shared attack/release envelope restarts at every loop wrap) and `fmod`/`pow`/`sin` per voice per sample | code-facts M4 | 2.5.3: four aligned Hann heads over the whole window, parameters on a 64-sample chunk grid |
| 14 | Per-block table regeneration and pattern rebuilds (`setEnvelopeShape`, `setNoiseColor`, `setEuclidean*` every block) | code-facts L4, consequences F8 | 7, row D13 |
| 15 | The spec's "silent when tempo invalid" path is unreachable (the processor substitutes 120 BPM) and the probe's playing=0 rows are identical to playing=1 | code-facts H6, consequences F5 | 4: the rule is written for what the plugin can do |
| 16 | The existing tests cannot fail on any of the above (`totalRMS > 0`, `maxSample < 2.0f`) | code-facts L7, critique M4 | 7: every row has a case that holds |

Deliberately left alone, and why:

- **The old `FreezeMode` class** (`effects/freeze_mode.h`). No plugin includes it (code-facts L3, consequences F9).
  Touching it buys nothing for this complaint.
- **`RollingCaptureBuffer`** (`primitives/rolling_capture_buffer.h`). Freeze stops using it (2.1) and drops the
  include. The class is unchanged: AtmosphereEngine and the Seraphis engine use it.
- **`GE` itself.** `ParticleOscillator` (`processors/particle_oscillator.h:179-181`) generates both Linear and
  Exponential tables with the default ratios, so the shared generator is not changed. Freeze post-processes its own
  table (2.8).
- **`plugins/iterum/src/parameters/pattern_freeze_params.h`**, an unreferenced second copy of the parameter pack
  (code-facts L3). Dead, noted, not deleted (CLAUDE.md: mention unrelated dead code, do not remove it).
- **A Position / Offset / scan control** (prior-art LOW 7). Fixed/Variable plus a short Capture length cover the named
  problems ("catch a word": set Capture 1/4 and press Hold). An extra parameter would be speculative.
- **Chance, Decay, pitch-per-repeat** (Beat Repeat extras, prior-art section "Product-specific extras"). No research
  finding names their absence as a problem.
- **A time-signature-aware "1 bar"**. Capture lengths are note values in beats (1/1 = 4 beats), like every other
  tempo-synced control in Iterum, so a 3/4 user gets 2 or 4 beats, not 3. The processor does not pass the time
  signature today and no finding asks for it.
- **The mono noise path** (one generator, one filter, same sample to both channels). Audible as a centred noise; no
  finding ranks it above low. `filterR_` stays prepared and unused as today (a cleanup row would be unrelated).
- **The archived spec's compliance table** (critique M4). It is archived; this document supersedes it for Freeze.
- **The processor's 120 BPM tempo fallback** (P:195-203). Correct for a host without tempo; the DSP stops pretending
  it can see an invalid tempo (section 4).
- **The shared `PitchShiftProcessor`** (`PS`). Freeze uses it as Shimmer does (R2). R13 (2026-10-29) changes it in
  one place: the PitchSync click when the detected grain shrinks after a tone change (row D18). Two of
  its properties shape 2.5.3 instead: its pass-through at `|ratio - 1| < 1e-4` (`PS:674-679`, which also stops
  writing the shifter's own buffer) and its +/- 24 semitone range (`PS:1315`, ratio clamp 0.25..4 at `PS:681`).
- **CPU budget**: no figure exists for Freeze (no `[performance]` case names it). R2 asks for one: row P11 measures
  the Drones at 4 voices x Octave through the processor against the full-plugin budget of the `dsp-architecture`
  skill, < 5 % of one core (`SKILL.md:93`). The per-block regenerations go away (row D13).

---

## 2. Capture model

### 2.1 What is held: three cycle buffers

The ring goes. `PatternFreezeMode` owns **three cycle buffers**, `buf_[3]`, each a stereo pair of
`capacity_ = ceil(kMaxCaptureLengthSeconds · sr)` floats (5 s), allocated in `prepare()`. This is a private nested
`struct CycleBuffer { std::vector<float> l, r; }`, so no namespace-level name is added (ODR search: no `CycleBuffer`
in `dsp/` or `plugins/`). The three buffers have three roles that rotate:

| Role | Index | What it holds |
|---|---|---|
| `rec_` | the cycle being recorded | input sample `t` of the current cycle is stored at `t` (cycle offset since the last capture boundary) while `t < capacity_` |
| `play_` | the window | the last captured cycle: what every reader plays |
| `spare_` | the retiring window | the window before `play_`; drone heads that latched it before the last capture finish their pass from it (2.6) |

A window is: `buffer` (one of the three), `from` (the first valid sample, 0 for a complete cycle), `length` (the end:
valid samples are `[from, length)`). The window's sample 0 is the downbeat of the cycle that recorded it, at every
tempo and every capture length, because recording restarts at offset 0 at each boundary. A cycle longer than 5 s
(2/1 below 96 BPM, 1/1 below 48 BPM) stores its first 5 s only. The window then starts on its downbeat, is
`capacity_` long, and does not reach the end of its cycle.

Why three buffers rather than one large ring (challenge 14): a window is read for up to a whole cycle after it was
captured, and a cycle can be 24 s (2/1 at 20 BPM). A ring would have to hold two such cycles plus a block, and a
window would still go stale at long cycles. With cycle buffers a window cannot go stale, there is no "too old" rule,
and no capture is forced. Memory (accepted by R4; `kMaxCaptureLengthSeconds` stays 5): 3 x 5 s x 2 ch x 4 B =
5.3 MB at 44.1 kHz, 5.8 MB at 48 kHz, 11.5 MB at 96 kHz and 23.0 MB at 192 kHz. Today's ring takes 2.1 / 2.1 / 4.2 /
8.4 MB, and the 16 s ring that challenge 14 proposed would take 8.4 MB at 44.1 kHz and 33.5 MB at 192 kHz. The
reconcile adds two burst buffers (2.5.4: 2 x 2 s x 2 ch x 4 B = 1.4 MB at 44.1 kHz, 6.1 MB at 192 kHz) and up to six
`PitchShiftProcessor`s (2.5.3; each prepares all four of its internal shifters, the PhaseVocoder's 4096-point STFT
included, `PS:1216-1229`), whose size the build records in `probe/after.txt`. The existing slice pool (8 x 2 s x
2 ch, `H:136-138`) is unchanged.

Per buffer, two facts are latched while it records (O(1) per sample, no extra pass):
- `peak` = the running maximum of `max(|l|, |r|)` over the samples from `max(recFrom_, 0)` on. A window whose
  `peak < kCaptureSilenceFloor` is **silent** (findings 8 and 16).
- the contiguity of the roles: `playFollowsSpare_` (spare_ is the complete, uncapped cycle that ended where play_
  begins) and `recFollowsPlay_` (rec_ is recording the cycle that began where play_ ended, and play_ is uncapped).
  Both are set at a capture rotation and cleared by any boundary that does not capture and by every dirtying event.
  2.4 uses them to read across a window edge without a splice (finding 15).

Every reader (2.5) reads **forward** from the window at a cycle offset under the read rules of 2.4, so the read
never depends on the host block or on the recording position. That removes problem 3 at its root.

### 2.2 When it is captured

The capture clock has boundaries at `j · Lb` beats of the musical position `pos` (section 4 says where `pos` comes
from), where `Lb` is the Capture length in beats (1, 2, 4 or 8; 3.1). The clock fires every boundary at its exact
sample offset (4.1, 4.3). State:

| Field | Meaning |
|---|---|
| `cycleOffset_` (int64) | samples since the last fired capture boundary; counts while held too |
| `recFrom_` (int64, or `kNone`) | the first cycle offset from which `rec_` holds unbroken input in this cycle; `0` = the cycle was recorded from its downbeat |
| `onsetOffset_` (int64, or `kNone`) | the cycle offset of the first above-floor sample of this cycle (latched into the window at its capture as the window's onset) |
| `heard_` (bool) | an input sample with `max(|l|, |r|) >= kCaptureSilenceFloor` (1e-3, -60 dBFS) has been written since `reset()` or the last play edge that dropped a window |
| `sinceAboveFloor_` (int64) | samples since the last input sample at or above the floor (counts while held too); 2.5.4 |
| `captureCount_` (uint64) | completed captures since `reset()` (test accessor `getCaptureCount()`) |

**A cycle is dirty** when one of these happens inside it, strictly after its first sample: `reset()` or mode entry, a
jump that does not land on a capture boundary (4.1; a re-anchor is not a jump), a Capture length change, or the
first onset (the first above-floor sample after `reset()` or after a play edge that dropped a window). The event sets
`recFrom_` to its cycle offset and `heard_ = true`. Independently, the first above-floor sample of every cycle sets
`onsetOffset_`.
A Hold release is **not** a dirtying event (finding 7): `rec_` keeps recording while Hold is engaged (2.3), so the
cycle in progress at the release is complete.

**A play edge** (`ctx.isPlaying` false in the previous block and true in this one) that finds a window, silent or
not, and Hold not engaged, clears `heard_` and drops the window (the roles keep their buffers; voices in flight
finish) (finding 16, R9). The mode is then as after `reset()`: from the first onset after play `rec_` holds the
span (below), so the hits of bar 1 that follow the onset by `needSpan` (steps 3 and 6 of the default E(3,8))
stutter the first music heard after play, nothing stale and nothing silent, and bar 2 stutters bar 1. A play edge
that finds no window (a fresh instance, or one that never heard music) changes nothing. A play edge is also a jump
(4.1).

**At a capture boundary**, which fires after the samples before it have been written (4.3), let
`elapsed = cycleOffset_`, `stored = min(elapsed, capacity_)` and `half = 0.5 · Lb · spb` (at the current tempo):

1. While Hold is engaged (2.3): nothing is captured. `rec_` was recorded all the same.
2. **A non-silent window exists.** Capture when `recFrom_ == 0`, `heard_`, and `elapsed >= half`. The new window is
   `{rec_, from 0, length stored}`. Otherwise the window in force keeps playing.
3. **No window, or a silent one** (the span, below). Capture when `heard_` and `stored - max(recFrom_, 0) >=
   needSpan`. The new window is `{rec_, from max(recFrom_, 0), length stored}`. This is a **partial window**. Under
   this rule a cycle that holds `needSpan` of music after a silent window becomes the window at its boundary, even
   when the music began mid-cycle.
4. A capture rotates the roles: `spare_ <- play_`, `play_ <- rec_`, `rec_ <- old spare_`, `captureCount_++`.
5. Every boundary, capture or not, resets the cycle: `cycleOffset_ = 0`, `recFrom_ = heard_ ? 0 : kNone`,
   `onsetOffset_ = kNone`.

Here `needSpan = min(max(kMinReadyBufferMs, Slice Length, Grain Size · (1 + Size Jitter / 2)), half, capacity_)`,
converted to samples. It is the material a partial window must hold so that the longest read of the current settings
fits, but never more than half a cycle.

What the rules give:

- **No equality test against a recomputed length** (challenges 5 and 11). A complete cycle is whatever elapsed between
  two fired boundaries. At 128 BPM and 44.1 kHz the cycles are 82687 or 82688 samples, and each one is captured. A
  ritardando or accelerando changes the cycle length and nothing else. The `half` test only rejects cycles that a
  jump cut short (4.1), and a tempo change cannot cut a cycle below half its length.
- **The window always starts on its downbeat** (challenge 6). At 80 BPM with 2/1 the cycle is 6 s, and the window is
  its first 5 s, downbeat at sample 0.
- **Silence is captured** (challenge 2). After the first onset, a complete silent cycle replaces the window like any
  other cycle, and Freeze falls silent with its input one cycle later. Before the first above-floor sample after
  `reset()` or a play edge, nothing is captured, so a fresh instance or a project load that sits on silence never
  builds a window of silence. To keep the music through a rest, use Hold. 2.9 gives the tail this implies.
- **A silent window is no window for the readers** (findings 8 and 16). No hit, grain, burst slice or drone reads
  it, and nothing it would have played opens the duck gate (2.7). So the bar after a silent bar, or after a
  silent stop, passes the input untouched under Insert, its downbeat included, instead of being punched by silent
  stutters. The wet is silent there, as "Freeze follows its input" says.
- **Entry and jumps** dirty the cycle in progress. The next boundary keeps the window in force, and the first
  complete cycle after it replaces the window. **A Hold release does not**: the next boundary captures (2.3).
- **Capture length change**: the capture clock re-bases at the block of the change (4.1) and the cycle in progress is
  dirty. Its start is not a boundary of the new length.

**The first window (the span).** After `reset()` or mode entry no window exists (after a play edge that dropped a
window none exists either, and the span plays again from the first onset: see "A play edge" above, R9). From the first onset on, `rec_` holds the input
from `max(recFrom_, 0)` up to `min(cycleOffset_, capacity_)`. Once that region holds `needSpan` samples, it plays as a
provisional partial window, the **span**: `{rec_, from max(recFrom_, 0), length min(cycleOffset_, capacity_)}`, which
grows every block. Readers latch its current end when they cut, and only ever read samples already written. The span
starts at the cycle offset where recording became unbroken. Its Fixed point is the onset (2.4), so a project that sat
on 20 s of silence plays the first music, never silence and never stale audio (challenges 4 and 12). The span ends
in one of three ways:

- At a boundary it becomes the window (rule 3).
- A dirtying event (a jump or a Capture change) promotes it to the window while it holds `needSpan`, so what is
  playing is never cut off.
- It is discarded if it never reached `needSpan`. Nothing was playing from it.

The consequence at the plugin defaults is the same wherever in a bar the mode is entered. About 200 ms after the
first music, the next hit of the pattern stutters the first thing played. From the first boundary on, the hits stutter
the captured material. From the first complete bar on, they stutter the previous bar's downbeat. The first stutter
comes at most 200 ms plus one step gap (750 ms in the tresillo) after the music starts, never a silent first bar.
This holds after `reset()`, mode entry and a play edge (R9).
Entering 0.1 beat after a bar line works the same way: the span is valid at beat 0.5, and the hit at beat 1.5 plays
from the onset (challenge 13; the old rule "the first downbeat recorded after entry" is gone).

### 2.3 Hold

`setHold(bool)`. R1: Hold keeps the window that is already playing; it does not grab the last Capture length at the
press. Hold is **engaged** when `hold_` is on and a non-silent window exists. While Hold is engaged:

- `rec_` keeps recording the input (finding 7). `rec_` is neither `play_` nor `spare_`, so writing it cannot disturb
  what plays. `cycleOffset_`, the onset and `heard_` update as usual.
- No boundary captures (rule 1 of 2.2). Each boundary still resets the cycle, so `rec_` always holds the cycle in
  progress, recorded from its downbeat.
- Every reader keeps playing the same window. **Hold keeps what you hear**: Clouds' and Beads' FREEZE "stops the
  recording of incoming audio; the grain engine keeps playing" (prior-art HIGH 1/2).
- **Slices sustain until the next hit** (R5, Beat Repeat's Gate; finding 6). A Euclidean slice that starts or is
  playing while Hold is engaged does not enter its release at `length - release`: it loops its copy (the seam of
  2.10, aligned and crossfaded) until the next hit, and only then releases while the new slice attacks. A noise burst
  does the same: its slice part loops, its noise runs on and its envelope holds the sustain level until the next
  burst (2.5.4). Granular (a Poisson cloud of overlapping grains) and Drones (a continuous source) are already
  continuous and change nothing. A held bar is therefore continuous sound with the rhythm on top.
- **Under Dry = Insert the input is muted**: the hold gate `gh` opens (2.7). Pressing Hold freezes the sound, and
  releasing it lets the music back in (challenge 3). Under Dry = Mix the dry keeps its crossfade share.

**A Hold that arrives before any non-silent window** waits. Recording continues, the span plays as in 2.2 (after
`reset()` or mode entry), and Hold engages at the first capture of non-silent material (rule 3: the first boundary
whose cycle holds `needSpan` from the onset). A Hold restored from a project with Hold saved On therefore holds the
first thing played, up to the next bar line, and it holds at least `max(200 ms, Slice Length, the longest grain)`.
That is never less than the reads of the current settings need, so a restored Hold with 500 ms slices is audible
(challenges 4 and 13). A Hold pressed while the window in force is silent waits the same way, so Hold never mutes
the input into silence.

**On release** (finding 7):
- The window stays, and the **next boundary captures** the cycle in progress, which `rec_` recorded from its
  downbeat during the hold. The window is at most one cycle stale after the release.
- Sustained slices and bursts enter their release at the release sample.
- **Under Dry = Insert, the release gap**: from the release until the next capture, no type triggers (no hit, grain
  or burst; the drone envelope releases) and the duck gate is held at 0, so the live input returns at once, over the
  hold gate's settling (2.7) and the release of what was sounding. The type resumes at the capture, from the new
  window. Under Dry = Mix there is no gap (it would leave a silent wet): the held window keeps playing until the
  capture replaces it.

Hold x Dry, at Mix 100 %:

| Dry | Hold off | Hold on (engaged) | Just released, until the next capture |
|---|---|---|---|
| Mix | wet only; the window follows the input one cycle late | wet only; the window is frozen; slices and bursts sustain | wet only; the held window, slices no longer sustained |
| Insert | the input plays, ducked only under each hit, burst or drone of non-silent material (Granular: never) | the input is muted; only the frozen, sustained pattern plays | the input only (plus the release of what was sounding) |

Hold is a normal automatable parameter, so a MIDI controller mapped to it gives press/release (momentary) and a
button gives latch; no second control is needed (consequences option A3 is answered by the host). The momentary use
"hold over a fill, release on the drop" therefore plays the drop untouched under Insert; from the next capture
boundary on, the pattern stutters the cycle the release fell in (a release on the drop's downbeat: the drop's first
bar).

### 2.4 The playhead: Fixed and Variable

Every reader takes its material from a **playhead** `P`, a cycle offset in the window:

- **Fixed** (Slice Mode 0):
  - In a complete window, `P = 0`: the window's downbeat.
  - In a partial window or the span, `P` is the window's onset if it lies in the window (`onsetOffset_` latched at the
    capture), otherwise `from`. So the first music window after a silent one (rule 3) stutters its first music, not
    the silence before it.

  Every hit, grain, burst and drone pass comes from the same place: a stutter of one moment.
- **Variable** (Slice Mode 1):
  - In a complete window, `P = round(theta · spb) mod length`, where `theta` is the musical phase (`pos mod Lb`)
    **at the moment the reader cuts** (a hit's boundary, a grain's start, a burst's start, a drone head's pass
    start). The window is replayed in place: step k of the pattern plays the audio that was at step k of the captured
    bar, so Rotation and Hits select which beats of the held bar come back, and with the transport running the
    re-sampled bar sits on the grid.
  - If the cycle was longer than the 5 s window, `P` is clamped by the read rules below (it no longer wraps).
  - In a partial window or the span, Variable behaves as Fixed: a partial window has no downbeat to be in phase with.

**Read rules** (findings 11 and 15). A complete window's last sample and its sample 0 are not contiguous audio, so a
read never wraps inside the window. Slices, grains and bursts are copies made at their trigger, of `n` samples
starting at a cycle offset `s` (which may be negative or reach past `length`):
1. **Contiguous continuation first.** The readable range is `[lo, hi)` with
   - `lo = from`, or `lo = -(spare_ length)` when `playFollowsSpare_` (offsets below 0 read the tail of `spare_`,
     the cycle that ended where `play_` begins);
   - `hi = length`, or `hi = length + t` when `recFollowsPlay_`, where `t` is the trigger's own cycle offset (offsets
     at or past `length` read `rec_` from 0, the cycle that follows `play_`, written up to the trigger and never
     beyond it, so the read does not depend on the host block).
2. **Clamp otherwise.** If `[s, s + n)` leaves `[lo, hi)`, `s` is moved inside: `s = clamp(s, lo, hi - n)`. A
   Fixed read that would start before the downbeat without a contiguous `spare_` therefore starts at `from`
   (one-sided at the downbeat).
3. **Splice only when nothing else fits.** If `n > hi - lo` (a slice longer than all readable audio), the copy
   reads `[lo, hi)` and continues from `lo` through a splice found and crossfaded by 2.10. No other read splices.

Drone heads follow the same rules inside their latched buffer and never cross a window edge (2.5.3).

`Slice Mode` keeps its ID (1017), type (2-entry list) and strings ("Fixed", "Variable"); it was a no-op, so giving it
this meaning changes no saved sound for the worse. The archived FR-011 ("duration controlled by the pattern engine")
is replaced: duration is the Slice Length knob in both modes.

Steps, Hits, Rotation and Pattern Rate affect **Euclidean only** (problem 6). `theta` needs no steps.

### 2.5 How the four pattern types read the window

All reads are forward, from the window's buffer at cycle offsets (`readWindow(off)`, `readWindowLinear(double off)`
interpolating towards the forward neighbour, `copyWindow(dst, n, s, t)` under the read rules of 2.4). No reader
touches `rec_` at or past the trigger's cycle offset, no type triggers from another type's clock, no type triggers
from a silent window (2.2), and under Insert no type triggers in the release gap after a Hold release (2.3).

#### 2.5.1 Euclidean

- Clock: the step boundaries `k · stepBeats` (Pattern Rate note value, `getBeatsForNote`), fired at their exact sample
  offset inside the block (section 4). A boundary fires when `EuclideanPattern::isHit(pattern, ((k mod steps) + steps)
  mod steps, steps)` (non-negative for pre-roll positions).
- On a hit at offset `o`: skip if no non-silent window or span is valid, or in the release gap (2.3). Otherwise:
  - Allocate a `SlicePool` slice. When the pool is empty, steal the slice with the largest position, as today.
  - Copy `sliceSamples = Slice Length · sr` from `P` under the read rules of 2.4 (trigger offset `t` = the cycle
    offset at `o`).
  - Set its gain `g_s = 1 / sqrt(activeCount + 1)` **once at the trigger** (no per-block step, code-facts M6 /
    consequences F10).
  - Start it at output sample `o`. While Hold is engaged the slice is marked **sustained** (2.3).
  - A sustained slice already playing enters its release at `o` (the new hit replaces it).
- Playback: `out += slice[pos] · env(pos) · g_s`, `env` = the attack / sustain / release envelope of
  `calculateEnvelope` (2.8). A sustained slice holds `env` at its sustain level and loops its copy at the seam of
  2.10 until it is released (next hit, Hold release, type change or `reset()`); its release then starts from the
  current loop position. A finished slice returns to the pool.
- Level at the plugin defaults (200 ms slices, 1/8 steps, 3 of 8, no overlap): each slice at unity, envelope plateau
  1.0, 10 ms attack, 100 ms release.

#### 2.5.2 Granular Scatter

- Clock: a Poisson process at Density. After each grain, draw `interval = -ln(u) / density` seconds (`u` uniform in
  (0.001, 1]), count it down in samples, and fire. Mean rate = Density (spec FR-039a / SC-003: within 20 %). The
  Euclidean scheduler does not fire grains (problem 6).
- Grain: length `g = grainSize · (1 + (2u - 1) · 0.5 · sizeJitter)` ms (as today, minimum 10 ms).
- **Scatter region** (findings 11 and 15, challenge 18). With `Wn = length - from` (the window's own length) and
  `SL` = Slice Length:
  - width `R = min(Wn, SL + positionJitter · max(0, Wn - SL))`. Slice Length is the smallest region (Position
    Jitter 0 %) and Position Jitter 100 % reaches the whole window. At the defaults (SL 200 ms, jitter 50 %, a 2 s
    bar) `R = 1.1 s`.
  - placement: centred on the playhead `P` where the readable range allows, then shifted inside `[lo, hi - g]`.
    Fixed centres on the downbeat: with a contiguous `spare_` the region is `[-R/2, R/2)`, otherwise it starts at
    `from` (one-sided). Variable centres on the bar position.
  - start: uniform in the region, then `s = min(s, hi - g)`, so **no grain starts inside the last grain length of the
    readable range** and no grain crosses a window edge except into contiguous audio.
  - Slice Length keeps a meaning in Granular: the width of the moment being granulated at low jitter. The archived
    FR-032 ("100 % = anywhere in the buffer") comes back as Position Jitter 100 % = anywhere in the window.
  - The grain is copied into a pool slice (stealing the shortest remaining, as today), with gain
    `clamp(1.41 - S, 0.2, 1)` at the trigger, where `S` is the sum of the gains of the grains still playing, and the
    shared envelope. The earlier `1 / sqrt(activeCount + 1)` summed to 1.707 (+4.6 dB) for two in-phase grains of a
    stationary tone and measured +4.0 dB against the +3 dB peak bar of 6.2; the summed-gain cap keeps two coherent
    grains at 1.41 (+3 dB).
- Fixed: a cloud around the downbeat. Variable: a cloud that follows the bar.

#### 2.5.3 Harmonic Drones

R2 replaces the two crossfading varispeed heads of the earlier design: the upper voices are pitched by the Shimmer
pitch shifter, and the band-limiting Biquads go with the heads. Findings 9, 10 and 14 (two heads of the same tone
D/2 apart cancel or swell; one anchor re-read every pass pulses on drums) are answered by the source below. Problems
3 and 13 and challenges 15 and 16 still shape it.

**The drone source** (one per drone, shared by every voice; stereo):
- **Four source heads** (`kDroneSourceHeads = 4`), each reading the window **forward at rate 1** for a pass of
  `Lp = clamp(Slice Length, kDroneMinPassMs = 100 ms, span)` samples under the Hann window
  `w(x) = sin^2(pi x)`. Head `h` begins its first pass `h · Lp / 4` after the drone starts, so four windows overlap
  at 75 % and sum to 2; the source is `0.5 · sum_h w · x`. Slice Length sets the pass length (Harmonic Bed 500 ms,
  Fifths Drone 600 ms keep theirs; the default 200 ms gives a denser wash).
- **The loop is the whole window** (finding 10's ruling). At each pass start a head draws its start uniformly over
  `[from, length - Lp]` of `play_` (the latched buffer, `from`, `length`). The draw is not centred on the playhead:
  with the region at the whole window, Fixed and Variable sound alike in Drones (Slice Mode keeps its control; it is
  inert here, as it was before).
- **Aligned starts** (the WSOLA step of finding 9's suggestion). The drawn start moves by the offset in
  `+/- kDroneAlignMs` (5 ms) that maximises the normalised correlation of the head's first 5 ms (L + R) with the sum of
  the other heads over the same 5 ms of output (2.10's search, run once per pass start, outside the per-sample loop).
  The first head of a drone has nothing to align to and starts as drawn.
  - Why: on a stationary tone four unaligned heads at random starts interfere at random phase. The simulation in
    `research/drone-source-sim.js` (run 2026-10-29) gives a 10 ms max/min depth of 21-36 dB unaligned and 0.0-0.6 dB
    aligned, at 200, 222, 233, 247, 260, 261.63 and 311 Hz, with the level 0.0 dB re the window when aligned.
- **Every head re-anchors only at its own pass start, where its window is 0** (challenge 15). A re-capture, a
  Slice Length change and a tempo change take effect per head at its next pass start. No read position ever jumps
  under a non-zero gain. A head pass lasts exactly `Lp <= span` output samples (rate 1), so it ends before its buffer
  is reused unless the cycle shrank below `Lp` between two captures; only then the forced 5 ms fade of 2.6 applies.
- A silent window is not read (2.2): the drone envelope releases while the window in force is silent and attacks
  again when a non-silent window arrives.

**The voices** `v = 0 .. voices-1`, each `1 / sqrt(voices)`:
- **Voice 0 (the root) plays the source directly**: no shifter, no latency, exact pitch, no drift.
- **Voice v >= 1** runs its own pair of `PitchShiftProcessor`s (left, right), `PitchMode::PitchSync`, the mode
  Shimmer uses (`effects/shimmer_delay.h:523`): pitch-synchronous grains of twice the detected period, 10-30 ms
  (`PS:614-623, 761-773`), spliced at a correlation-searched point (`PS:755-758`). Each shifter is prepared in
  `prepare()` and reset when the drone starts or the voice joins. The voices therefore lag the root by the shifter's
  grain, 10-30 ms: `getLatencySamples()` returns the grain, clamped to 10-30 ms (`PS:617-620, 718-720`); the
  "5-10 ms" of R2 and of the class comment (`PS:595`) is shorter than the code allows. R2 accepts the latency for a
  drone; the reconcile reads it as accepting the 10-30 ms the code gives.
- **Interval stack** `s_v = v · semis`, `semis = getIntervalSemitones(interval)` (`T:96-100`: 0, 3, 4, 5, 7, 12). The
  shifter's range is +/- 24 semitones (`PS:1315`; its ratio is clamped to 0.25..4 at `PS:681`), and only
  Octave x 4 voices exceeds it (`v = 3`: 36). A stack above +24 folds down four octaves: `s_v > 24 -> s_v - 48`, so
  Octave x 4 plays 0, +12, +24 and -12 (four octaves, no duplicate). Every other setting is unchanged, Harmonic Bed
  (3 x Octave: 0, 12, 24) and Fifths Drone (4 x Fifth: 0, 7, 14, 21) included.
- **Drift** in cents, `phi_v(0) = v / voices`, advanced at Drift Rate:
  - `s_v` neither 0 nor 24: `c_v = 50 · Drift · sin(2 pi phi_v)` (the documented +/- 50 cents x Drift);
  - `s_v = 24`: `c_v = -50 · Drift · (0.5 + 0.5 sin(2 pi phi_v))` (flat only: above +24 the ratio clamp would clip the
    sharp half);
  - `s_v = 0` (Unison, `v >= 1`): `c_v = sigma_v · (1 + 49 · Drift · (0.5 + 0.5 sin(2 pi phi_v)))`, `sigma_v = +1`
    for odd `v`, `-1` for even. The shifter passes its input through untouched when `|ratio - 1| < 1e-4`, about
    0.17 cent (`PS:674-679`), and does not write its own buffer while it does, so a voice whose ratio crossed that
    band would jump by one grain twice per drift cycle. A Unison voice therefore never comes closer than 1 cent to the
    root, on one side per voice; at Drift 0 it is a slow chorus against the root (1 cent at 233 Hz beats at 0.13 Hz)
    rather than a louder copy of it.
  - Voices 1 therefore has no drift (the root does not drift); the Drift and Drift Rate controls stay.
- `setSemitones(s_v)` and `setCents(c_v)` are sent at every chunk start (below); the shifter smooths them over its
  own 10 ms (`PS:1226-1229`), so an Interval change glides over about 10 ms.
- A voice that joins (Voices raised) fades in over `kDroneVoiceFadeMs` (20 ms, `OnePoleSmoother`, 99 % time)
  starting one maximum grain (30 ms) after its shifter's reset; a voice that leaves fades out over the same 20 ms and
  then stops. The `1 / sqrt(voices)` gain moves with the same smoother.

**Chunks** (block-size invariance, challenge 10). The drone renders in chunks of `kDroneChunkSamples = 64` samples
(= `PitchShiftProcessor::kSmoothingSubBlockSize`, `PS:165`), on the grid `sampleCounter_ % 64 == 0` of the mode's own
sample counter (counted since `reset()`), into a 64-sample output FIFO per channel that the per-sample render reads.
- Its source is the window, which exists ahead of time, so rendering up to 63 samples ahead reads nothing that is not
  yet there.
- Each shifter call is exactly one 64-sample chunk, i.e. one internal sub-block, so the shifter's smoother steps and
  its pitch-ratio grid are the same at every host block size and every segment split (4.3).
- Drift, ratios, `Lp` and the voice count are read at chunk starts: one `sin` per voice per 64 samples.

**Envelope and gate.** Gain `1 / sqrt(voices)`. One global drone envelope: it attacks when the drone starts (a
non-silent window or span becomes valid, or the type is switched to Drones) and releases when the type leaves Drones,
when the window in force becomes silent, in the release gap (2.3) or on `reset()`. Attack shapes only this onset;
the source's seams are the Hann overlaps, independent of Attack (challenge 16). The envelope is applied per output
sample, after the FIFO.

**Level.** On a stationary tone the aligned source is at the window's level (simulation above); voice 0 is the source
and an upper voice is the source transposed, so `voices` voices of different pitch sum in power to
`voices · (1 / sqrt(voices))^2 = 1`: the drone is at 0 dB re the window, give or take the shifter's own splices
(6.2).

**Rhythmic input** (finding 10). The source replays four 200 ms (default) pieces of the whole window at a time, aligned
on their first 5 ms, instead of re-reading one anchor every pass. The simulation gives a 10 ms RMS p95/p5 depth of
16.0 dB for the D8 (3) drum window (aligned, four heads, 200 ms), against 22.3 dB for the drum window itself and
13.8-20.7 dB for every other head count and pass length tried (8 to 128 heads, 200 and 500 ms). No read of this
material that keeps the kick in it reaches the absolute 3 dB first ruled, so the bar is relative (R8, 8.1): the
drone's depth is at least 6 dB below the drum window's own (predicted 16.0 against 22.3 dB).

**Cost.** Up to three shifter pairs (6 `PitchShiftProcessor`s, each with its own `PitchDetector`) at Voices 4, plus
four head reads per channel per sample and one 5 ms alignment search per pass start (2.10). Measured by row P11.
The planned remedy if P11 misses its budget: one shared `PitchDetector` per channel on the source, fed to every
voice through `PitchShiftProcessor::processWithSharedPitch` (`PS:1394-1427`), since every voice shifts the same
source (6 detectors become 2).

Drones never allocate from the slice pool and never run the Euclidean callback.

#### 2.5.4 Noise Bursts

- Clock: burst boundaries at `k · burstBeats` (Burst Rate note value), fired at their exact sample offset, phased like
  the Euclidean steps (section 4). No input-level gate on a burst over a non-silent window or span (problem 4; spec
  SC-011).
- **Noise-only bursts** (finding 12). A burst whose slice source is silent (no window, or a silent one) is
  noise-only. It fires only once `heard_` is set, and only while the transport is playing (`ctx.isPlaying`) or
  within two capture cycles (`2 · Lb · spb`) of the last above-floor input (`sinceAboveFloor_`). So a fresh instance
  on a silent track plays nothing, and a stopped DAW stops hissing two cycles after its last audio. A noise-only
  burst contributes nothing to the duck gate (2.7): the noise lies over the dry like the Granular cloud. In the
  release gap after a Hold release no burst fires (2.3).
- **Burst voices** (finding 20, option a). Two burst voices, each with a buffer of `kMaxSliceLengthMs` (2 s) x 2 ch,
  allocated in `prepare()`. At its trigger a burst copies its slice, `S = Slice Length · sr` samples from `P`, into
  its voice's buffer under the read rules of 2.4 (zeros for a noise-only burst), like a Euclidean slice. A capture
  during the burst therefore cannot switch it to another bar, and no buffer it reads is rotated into `rec_`.
  - A new burst starts in the voice that is not the newest. If that voice is still sounding, it fades out over 5 ms
    (`kForcedHeadFadeMs`) first, without the old burst being truncated under its envelope. Two bursts closer than
    5 ms apart (1/64T above 500 BPM) cut the older fade short; nothing slower can.
- A burst:
  - `noise = filter(generator())`. In `prepare()` the generator's eight colours are all set to **-10.0 dB** (`kNoiseLevelDb`; R14 raised it from -12 dB (-31.5 dBFS) to -10.5 dB (-30.03 dBFS, active 75.9 %, both bars missed) to -10.0 dB, the smallest 0.5 dB step meeting the -30 floor and the 76-84 % bar). Today
    White, Pink and Brown are at -12 and the rest at -20 (problem 4).
  - The filter type is set at the burst start. The swept cutoff `cutoff · (1 - sweep · i / S)` (floor 20 Hz) is
    applied at every `i` with `i % 32 == 0`, counted from the burst's first sample and not from the block start, so
    the result does not depend on the block size. One generator and one filter are shared: two overlapping bursts
    share the noise stream, and the filter takes the newer burst's settings at its start.
  - Output per channel `(slice_ch + 0.5 · noise) · env(i)`. The same mono noise goes to both channels (left alone,
    section 1).
  - **While Hold is engaged the burst sustains** (R5): `env` holds its sustain level, the slice part loops at the seam
    of 2.10, the noise and the sweep (held at its value at `S`) run on, and the burst releases at the next burst, at
    the Hold release or on a type change.
- The noise level no longer follows `|slice|`: at defaults the noise sits about -18 dBFS under the slice (0.5 x
  -12 dB), audible on its own when the slice is silent.
- Noise Bursts never allocates from the slice pool.

### 2.6 Crossfades

- **On re-capture**, nothing fades:
  - Euclidean, Granular and Noise voices are copies made at their trigger (slices and grains in the pool, bursts in
    their own buffers, 2.5.4), so a voice in flight finishes from the old material and the next trigger cuts from
    the new window. A sustained slice or burst (Hold, R5) loops its own copy, so it is not affected either.
  - Drone source heads keep reading the buffer they latched, which after the rotation is `spare_`. They move to the
    new window at their own next pass start, where their gain is 0 (2.5.3).
  - `spare_` becomes `rec_`, and is overwritten, only at the following capture. A head pass lasts exactly `Lp`
    output samples (rate 1; the pitch is the shifter's), and `Lp` is at most the window's length, so a head finishes
    before its buffer is reused unless the cycle shrank below `Lp` between two captures (a tempo jump, a shorter
    Capture, or a jump-shortened cycle).
  - In that case only, at the rotation, a head still reading the buffer that becomes `rec_` fades out over 5 ms
    (`kForcedHeadFadeMs`) and re-latches. The other forced fade is the older burst voice's (2.5.4).
- **Contiguous reads across an edge** (2.4) read `spare_` or `rec_` only inside the copy made at the trigger, so a
  later rotation never changes what a voice in flight plays.
- **On pattern-type change**: `processCrossfade`, `crossfadeOutL_/R_`, `crossfadeActive_`, `crossfadeProgress_`,
  `crossfadeSamples_`, `previousPatternType_`, `isCrossfading()` and `kPatternCrossfadeMs` are removed. Instead:
  - the old type stops triggering;
  - its slices in flight finish through their envelopes, and pool slices of the old type are not freed early;
  - a running drone releases over `Release` ms (the global drone envelope; its shifters keep running until the
    envelope is 0);
  - an active noise burst finishes its envelope, and a sustained slice or burst enters its release;
  - the new type starts triggering at once (its first voice rises over `Attack`).

  This is click-free by construction and block-size independent (the proof is row D11).
- **On Hold on/off**: the window does not change (2.3). Under Insert the dry fades out or in over the hold gate `gh`
  (2.7). Sustained slices and bursts release at the Hold release.
- **Mix and the duck**: `dryWetSmoother_` (unchanged, `OnePoleSmoother` configured 20 ms), `gv` and `gh` as given in
  2.7, with their coefficients.

### 2.7 Level rule and Mix semantics

Wet path level: a slice plays its samples at unity (`g_s = 1` when it does not overlap another), times the envelope.
Drones `1 / sqrt(voices)`, Noise as 2.5.4. At the plugin defaults the wet is three 200 ms slices per 2 s bar with the
10 / 100 ms envelope: duty 0.30 (-5.2 dB), envelope energy 0.656 (-1.8 dB), so **about -7 dB RMS re a stationary
input, peak 0 dB** when measured wet-only (section 6).

Mix: `m` = Mix (1012), smoothed by `dryWetSmoother_` (`OnePoleSmoother`, 20 ms). Two laws, chosen by `Dry` (1072):

| Dry | Law | Meaning |
|---|---|---|
| Mix (0) | `out = in · (1 - m) + wet · m` | today's crossfade, byte for byte; every saved preset loads this |
| Insert (1) | `out = in · (1 - m · g) + wet · m` | the dry is replaced only while the gate `g` is open |

The choice of law is itself smoothed: `insertWeight_` (`OnePoleSmoother`, 20 ms, target 1 for Insert, 0 for Mix,
snapped in `reset()` / `snapParameters()` and on `setDryMode` before the first block) blends them,
`out = in · ((1 - w)(1 - m) + w(1 - m · g)) + wet · m`, so a toggle or an automation lane of Dry never steps the dry
gain; at `w = 0` or `1` it is exactly one law, so the D10 cases hold.

The duck gate is `g = max(gv, gh)`, per sample. It is defined per type (challenge 8):

| Source | Gate value | Smoother |
|---|---|---|
| Euclidean slices | the maximum envelope value over the active slices | `gv` (`duckSmoother_`, so Attack 0 does not cut the dry with a click) |
| Noise burst | the burst envelope, for a burst over non-silent material; **0** for a noise-only burst (2.5.4) | `gv` |
| Drones | the global drone envelope | `gv` |
| Granular grains | **0**: the cloud sits over the full dry and never gates it per grain | n/a |
| Hold | 1 while Hold is engaged (2.3), else 0 | `gh` (`holdDuckSmoother_`) |
| Release gap (Insert, 2.3) | the `gv` target is forced to 0 | `gv` |

A voice contributes by the type that created it, so grains still in flight after a switch to Euclidean do not gate.
A silent window triggers nothing (2.2), so it never opens the gate: no duck for silence (findings 8 and 16). The same
holds for a single copy: a slice or burst whose own copied samples peak below `kCaptureSilenceFloor` (the peak is
taken while copying) plays but contributes 0 to `gv`, so a hit on a silent stretch of a non-silent window (music
that began mid-cycle) does not duck the live dry either.

**Smoother coefficients** (finding 17). All three are `OnePoleSmoother` (`SM:160-164`), whose `configure(ms, sr)`
takes the time to 99 %: `coeff = exp(-5000 / (ms · sr))` (`SM:77-92`), so `tau = ms / 5`. Per sample
`y += (1 - coeff) · (target - y)`, and the smoother snaps to its target once `|y - target| < 1e-4`
(`kCompletionThreshold`, `SM:55, 199-202`), which takes `ln(1e4) = 9.2 tau`.

| Smoother | `configure` | `tau` | `coeff` at 44.1 kHz | -60 dB (`6.9 tau`) | exact (snap, `9.2 tau`) |
|---|---|---|---|---|---|
| `dryWetSmoother_` (`m`) | `kSmoothingTimeMs` 20 ms (`H:100`) | 4 ms | `exp(-250 / sr)` = 0.99435 | 28 ms | 37 ms |
| `duckSmoother_` (`gv`) | `kDuckSmoothingMs` 2 ms | 0.4 ms | `exp(-2500 / sr)` = 0.94489 | 2.8 ms | 3.7 ms |
| `holdDuckSmoother_` (`gh`) | `kHoldDuckSmoothingMs` 20 ms | 4 ms | `exp(-250 / sr)` = 0.99435 | 28 ms | 37 ms |

A step of the gate target by `delta` therefore moves the dry gain by at most `(1 - coeff) · |delta|` per sample:
`1 - exp(-2500 / sr)` = 0.0551 at 44.1 kHz for `gv` (row D10 (5)).

**Gap and plateau** (the words the cases use):
- a **plateau** sample is one where the gate target is 1 and has been 1 for at least `7 tau` of its smoother
  (2.8 ms for `gv`, 28 ms for `gh`): the dry share there is below -60 dB re `m`;
- a **gap** sample is one where the gate target is 0, no voice that gates has a non-zero envelope, and at least
  `10 tau` (4 ms for `gv`, 40 ms for `gh`) have passed since the target last left 0. The smoother has snapped there,
  so `out == in · (1 - m · 0) + wet · m` exactly.

At `m = 1`, the dry disappears exactly for the plateau of each slice and comes back through the slice's release.
Between slices the music passes untouched. Under Drones at Insert / 100 %, the dry is gone while the drone sounds
(the "freeze replaces input" of critique M1 and the class comment at H:64). Under Granular at Insert / 100 %,
`out = in + wet`: a cloud over the music, about +1 dB over the input at the defaults. Under Hold at Insert / 100 %,
`out = wet`, and with slices sustaining (R5) the wet is continuous.

Fresh-instance defaults: **Mix 100 %, Dry Insert** (3.3). With Dry = Mix at 100 % the output is the wet alone (gaps
are silent), which is the old "Total Freeze" preset's intent and stays available.

### 2.8 Envelope

`calculateEnvelope` keeps its attack / sustain / release geometry and the proportional scale-down when
`attack + release >= length` (H:609-636). Three things change (challenge 17).

**The table** (512 entries, regenerated only when the shape changes) is
`GrainEnvelope::generate(table, 512, type, 0.5f, 0.5f)`, where `type` is the shape the label names
(`GrainEnvelopeType::Linear` or `::Exponential`, `GE:97-150`).
- The explicit `0.5f, 0.5f` make the attack fill the first half of the table and the release the second half, which
  is the half that `calculateEnvelope` reads for each phase. The defaults `0.1f` would leave a trapezoid that reaches
  1.0 after 20 % of the attack.
- For Exponential, Freeze then rewrites the release half itself:
  `table[256 + j] = (exp(-4 j / 255) - exp(-4)) / (1 - exp(-4))` for `j = 0..255`. The release then starts at 1 and
  ends at exactly 0. `GE`'s own release ends at `exp(-4) = 0.018`, a 1.8 % step at the end of every slice, grain,
  burst and duck.
- `GE` is not changed (section 1: ParticleOscillator uses it).

**The release phase** becomes `(pos - releaseStart) / (releaseSamples - 1)` (0 when `releaseSamples < 2`), so the
last sample of a slice reads table index 511, which is 0 for both shapes. The attack phase stays `pos /
attackSamples`, so the first sample reads 0.

**The values that follow.** Linear at mid-attack is 0.5. Exponential at mid-attack is
`(1 - e^-2) / (1 - e^-4) = 0.881`. The duck gate inherits the zero end, so no step is left.

### 2.9 Transport stop / start / tempo loss, and the tail

The rules are in section 4. In short: when the transport is stopped or gives no song position, the clocks free-run
from where they were, so the pattern, the captures and the hold all keep working. On play, a loop wrap or a locate,
the clocks re-base to the host position. A boundary at the jump position (within half a sample) fires there, hits and
capture included.

The output after the input stops:
- **Hold off.** The window follows the input, so the output dies with it. The cycle the input went silent in plays
  during the next cycle, and the following capture is silent. The output ends at most two capture cycles plus one
  slice and its release after the input, whatever the transport does. That is 4 s plus 0.3 s at the defaults
  (1/1 at 120 BPM), and at most 48 s at the extreme (2/1 at 20 BPM).
- **The exception: Noise Bursts** (finding 12). Once the window is silent, noise-only bursts go on while the
  transport plays, and stop two capture cycles after the last above-floor input when it is stopped (2.5.4). With
  the transport playing over a silent track, Noise Bursts therefore keeps hissing in its rhythm after the first
  music: it is a noise generator by design (spec SC-011), and the rule bounds it to a running song. With the
  transport stopped the output ends at most two capture cycles plus one burst and its release after the input, as
  above.
- **Hold on.** The output lasts as long as Hold is on.

The processor reports **`getTailSamples() = kInfiniteTail` for every mode** (R3; P10), and never calls
`restartComponent` on a mode edge. `getTailSamples` is a `[UI-thread & Setup Done]` query that hosts read at
activation (`ivstaudioprocessor.h:349-360`) and the SDK has no tail-changed restart flag, so a mode-dependent tail
could not reach a host after a mode change (finding 22). An infinite tail keeps a host from suspending Iterum on
silent input in any mode, which also covers the feedback tails of the other nine modes (former open question 4).
Iterum overrides no `getTailSamples` today, so every mode reports the SDK default `kNoTail` (`vstaudioeffect.h:84`).

### 2.10 Splices and loop seams

Three places join two pieces of audio that are not contiguous: a read longer than all readable audio (2.4 rule 3),
the loop of a sustained slice or burst under Hold (R5), and a drone source head's start (2.5.3). All three use one
private helper, run at the trigger or the pass start, never per sample:

- `findSplice(buffer, target, candidates [c0, c1], span F)`: the candidate start `c` that maximises the normalised
  correlation of `x[c, c + F)` (L + R) with the audio it will be crossfaded against over the same `F` samples.
  `F = kSpliceSpanMs` (5 ms) and the candidates lie within `+/- kSpliceSearchMs` (5 ms) of the nominal point. The
  search and the correlation span both step by `max(1, round(sr / 44100))` samples, then the best candidate is refined
  `+/-` one step at full rate, so the cost is about the same at every sample rate: 441 candidates x 220 samples x 2
  (correlation and energy), about 0.2 M multiply-adds per search at 44.1 kHz.
- The join is an equal-gain Hann crossfade (`sin^2` / `cos^2`) over `kSpliceFadeMs` (5 ms). After the alignment the
  two sides are correlated, and equal gain keeps correlated material level (an equal-power fade would swell by up to
  3 dB on it).
- **Loop seam of a sustained slice or burst** (R5): the copy of `n` samples loops over `[0, n - F - k*)`, where
  `k*` is chosen by `findSplice` over `[0, min(5 ms, n / 4)]` so that the samples after the loop end match the
  copy's first `F`; the seam crossfades over `F`. A copy shorter than `4 · F` (20 ms) loops without the search, with a
  fade of `n / 4`.
- Constants (`T`): `kSpliceSpanMs = 5`, `kSpliceSearchMs = 5`, `kSpliceFadeMs = 5`; `kDroneAlignMs` (2.5.3) is
  `kSpliceSearchMs`.

---

## 3. Parameters

### 3.1 New parameters (IDs from 1070, `plugins/iterum/src/plugin_ids.h` block 1000-1099; 1013-1014 and 1001-1011 stay reserved)

| ID | Enum | Name (host) | Type | Range / strings | Default (fresh) | Legacy (absent tail) | What it does |
|---|---|---|---|---|---|---|---|
| 1070 | `kFreezeHoldId` | "Freeze Hold" | toggle (`stepCount 1`, `kCanAutomate`) | Off / On; readout "Off" / "On" | Off | Off | 2.3: on = capture stops (the input is still recorded); what plays stays and slices sustain; under Insert the input is muted |
| 1071 | `kFreezeCaptureLengthId` | "Freeze Capture" | list (`createDropdownParameterWithDefault`, 4 entries) | "1/4", "1/2", "1/1", "2/1" = 1, 2, 4, 8 beats | index 2 ("1/1") | index 2 | 2.2: the capture cycle and so the window length |
| 1072 | `kFreezeDryModeId` | "Freeze Dry Mode" | list (2 entries) | "Mix", "Insert" | index 1 ("Insert") | index 0 ("Mix") | 2.7: the mix law |

Denormalisation (`handleFreezeParamChange`): Hold `v >= 0.5`; Capture `int(v · 3 + 0.5)` (0-3); Dry `v >= 0.5 ? 1 : 0`.
Normalisation (sync): Hold `0 / 1`, Capture `index / 3.0`, Dry `0 / 1`.
`formatFreezeParam`: Hold prints "On" / "Off" (the Granular/Spectral Freeze precedent, `granular_params.h:456`); the two
lists are `StringListParameter`s and format themselves.

DSP types (`T`; names checked: no `CaptureLength`, `DryMode`, `kCaptureLengthCount` or `kDryModeCount` exists in
`dsp/include` or `plugins/*/src`):

```cpp
enum class CaptureLength : uint8_t { Quarter = 0, Half, Whole, DoubleWhole };   // 1, 2, 4, 8 beats
inline constexpr int kCaptureLengthCount = 4;
inline constexpr CaptureLength kDefaultCaptureLength = CaptureLength::Whole;
[[nodiscard]] inline constexpr double getCaptureLengthBeats(CaptureLength) noexcept;   // 1.0, 2.0, 4.0, 8.0
[[nodiscard]] inline constexpr const char* getCaptureLengthName(CaptureLength) noexcept; // "1/4" "1/2" "1/1" "2/1"
enum class DryMode : uint8_t { Mix = 0, Insert };
inline constexpr int kDryModeCount = 2;
inline constexpr DryMode kDefaultDryMode = DryMode::Insert;
namespace PatternFreezeConstants {
    inline constexpr float kMaxCaptureLengthSeconds = 5.0f;        // each cycle buffer (2.1; R4)
    inline constexpr float kCaptureSilenceFloor = 1e-3f;           // -60 dBFS: the onset and a silent window (2.2)
    inline constexpr float kDuckSmoothingMs = 2.0f;                // gv: OnePoleSmoother 99 % time, tau 0.4 ms (2.7)
    inline constexpr float kHoldDuckSmoothingMs = 20.0f;           // gh: OnePoleSmoother 99 % time, tau 4 ms (2.7)
    inline constexpr float kNoiseLevelDb = -10.5f;                 // all eight colours (2.5.4; R14)
    inline constexpr int kDroneSourceHeads = 4;                    // drone source heads, 75 % overlap (2.5.3)
    inline constexpr float kDroneMinPassMs = 100.0f;               // shortest source pass (2.5.3)
    inline constexpr int kDroneChunkSamples = 64;                  // == PitchShiftProcessor::kSmoothingSubBlockSize (2.5.3)
    inline constexpr float kDroneVoiceFadeMs = 20.0f;              // a voice joining or leaving (2.5.3)
    inline constexpr float kSpliceSpanMs = 5.0f;                   // findSplice correlation span (2.10)
    inline constexpr float kSpliceSearchMs = 5.0f;                 // findSplice search radius; kDroneAlignMs (2.10)
    inline constexpr float kSpliceFadeMs = 5.0f;                   // equal-gain splice / seam fade (2.10)
    inline constexpr int kBurstVoices = 2;                         // burst voices, each kMaxSliceLengthMs x 2 ch (2.5.4)
    inline constexpr float kForcedHeadFadeMs = 5.0f;               // 2.6, and the older burst voice (2.5.4)
    inline constexpr double kMaxClockTempoBPM = 1000.0;            // spb tempo ceiling and event-array sizing (4.1, 4.3)
    // removed: kPatternCrossfadeMs (2.6) and kDefaultCaptureBufferSeconds (the ring is gone, 2.1);
    // kMinReadyBufferMs stays (needSpan, 2.2); kMin/kMaxCaptureBufferSeconds stay (already read by nothing but
    // pattern_freeze_types_test.cpp; pre-existing, noted, not removed)
}
```
`PatternFreezeMode::kDefaultDryWetMix` becomes `100.0f`.

### 3.2 Controls (geometry of `panels/freeze.md`, cell vocabulary of `cells.xsl`)

**Core 3 (604, 12, 284, 72): Mix knob + the Hold toggle in the toggle slot.** `knob-cell` already supports it
(`cells.xsl:46-51`: `ToggleButton origin="200, 32" size="76, 22" title-position="right" icon-style="power"`), the
Granular and Spectral Freeze toggles sit in exactly this slot (design-system 4.1, 4.8). In `freeze.xsl` the core-3
call gains `<xsl:with-param name="toggle-tag" select="'FreezeHold'"/>` and
`<xsl:with-param name="toggle-title" select="'Hold'"/>`. Title "Hold", not the dictionary's "Freeze" (6.2), because
the mode is itself named Freeze; the host-visible parameter name is "Freeze Hold".

**Column 1 row 1 fieldset (12, 96, 284, 180): title "CAPTURE" (was "SLICE"), three rows:**

| Row y | Cell | control-tag | Label | Widget |
|---|---|---|---|---|
| 24 | menu cell, full (12, 24, 260) | FreezeCaptureLength (1071) | Capture | `COptionMenu` (4 entries from the parameter) at (12, 44, 260, 24) |
| 76 | segment cell, full | FreezeSliceMode (1017) | Slice Mode | `CSegmentButton` "Fixed,Variable" at (12, 96, 260, 24) (moved from row 24) |
| 128 | segment cell, full | FreezeDryMode (1072) | Dry | `CSegmentButton` "Mix,Insert" at (12, 148, 260, 24) |

Last extent y = 148 + 24 = 172 <= 180, x = 272 <= 284. ENVELOPE (12, 288) and the four variants are unchanged, so the
fieldset count stays 6 (`editor_panels_test.cpp:416` `checkPanel({"FreezePanel", 9, ...})` gains three rows:
`{kFreezeHoldId, W::T}, {kFreezeCaptureLengthId, W::M}, {kFreezeDryModeId, W::SEG}`). No new visibility rule: the
three controls are visible in every pattern type.

`<control-tags>` of `plugins/iterum/resources/editor.uidesc` gains `FreezeHold` 1070, `FreezeCaptureLength` 1071,
`FreezeDryMode` 1072 (next to `FreezeEnvelopeShape` 1062, `editor.uidesc:165`).

XSLT: `specs/iterum-freeze/xslt/freeze-controls.xsl` is an XSLT 3.0 sheet with
`<xsl:include href="../../iterum-ui-redesign/xslt/cells.xsl"/>`, an identity transform, a template for
`template[@name='FreezePanel']` (`freeze.xsl`'s with the three changes above) and a template for `control-tags` that
copies its children and appends the three `<control-tag>` elements.
1. Apply it with
   `npx xslt3 -xsl:specs/iterum-freeze/xslt/freeze-controls.xsl -s:plugins/iterum/resources/editor.uidesc -o:F:/tmp/iterum-freeze/editor.uidesc`.
2. Check the result with
   `node tools/uidesc-inventory.js F:/tmp/iterum-freeze/editor.uidesc --check --baseline specs/iterum-ui-redesign/inventory-before.json`:
   no overlap, no out-of-bounds, no lost binding or control in FreezePanel. The three tags are additions, not
   changes. If the tool reports an added tag as a changed tag for FreezePanel, the baseline gains those three entries
   and nothing else.
3. Copy the result over the uidesc.
4. Mirror the same three edits into `specs/iterum-ui-redesign/xslt/freeze.xsl`, `panels/freeze.md` (2.1 core 3 row
   3d: the toggle; 2.2 renamed CAPTURE with the three rows; mapping table 3: 27 of 27 tags) and `design-system.md`
   7.10 / 7.11 (24 -> 27 Freeze tags, 134 -> 137), so a full rebuild reproduces the panel.

### 3.3 Registered defaults of a fresh instance (every Freeze parameter)

| Parameter | Fresh default | Changed? |
|---|---|---|
| Mix 1012 | **100 %** (`F` `dryWet{1.0f}`, registration 1.0, `H` `kDefaultDryWetMix 100`) | yes, was 50 % (problem 8) |
| Pattern 1015 | Euclidean | no |
| Slice Length 1016 | 200 ms | no (meaning per type: 3.4) |
| Slice Mode 1017 | Fixed | no (meaning: 2.4). R6: becomes Variable (ID, type and range unchanged; `F` default index and registration, `H` default) only if the sustained-chord row of 6.2 misses its bar with Fixed; sections 0 and 5.4 then say so |
| Steps / Hits / Rotation 1020-1022 | 8 / 3 / 0 | no |
| Pattern Rate 1023 | 1/8 (index 10) | no; the DSP default `noteValue_` becomes `Eighth` to match (`H:1158` is `Sixteenth`, which only the probe saw) |
| Granular 1030-1033 | 10 Hz / 50 % / 25 % / 100 ms | no |
| Drones 1040-1043 | 2 / Octave / 30 % / 0.5 Hz | no |
| Noise 1050-1054 | Pink / 1/8 / LowPass / 2 kHz / 50 % | no |
| Envelope 1060-1062 | 10 ms / 100 ms / Linear | no (Linear is now linear) |
| Hold 1070 | Off | new |
| Capture 1071 | 1/1 | new |
| Dry 1072 | Insert | new |

Tests that assert the old Mix default (`mode_defaults_test.cpp` if it lists Freeze, the DSP "mix parameters" case,
`mix_scale_consistency_test.cpp:99` only checks the 0-1 scale) are updated to 1.0 / 100.

### 3.4 Existing parameters whose meaning changes (IDs, types, ranges and strings unchanged)

| Parameter | Today | New | Why allowed |
|---|---|---|---|
| Slice Mode 1017 | no-op | playhead rule 2.4 | was inert; a dead control may take any meaning |
| Slice Length 1016 | slice duration in Euclidean / Noise; in Granular and Drones it acted only through the leaked Euclidean slices (problem 6) | Euclidean / Noise: slice duration (the loop length of a sustained slice under Hold); **Granular: the smallest scatter region** (2.5.2); **Drones: the source pass length, 100 ms at least** (2.5.3) | without this, fixing problem 6 would leave the core-1 hero knob dead in two of four types (challenge 18) |
| Mix 1012 | crossfade | crossfade under Dry = Mix (every saved preset), Insert law under Dry = Insert | the law is selected by a new parameter whose legacy value keeps the old law |
| Rotation 1022 | shifts a free-running pattern | rotates the bar-phased pattern (audible against the downbeat), and in Variable mode selects which beats of the held bar play | the parameter always meant rotation; it only had no reference |
| Drone Interval 1041 | read-rate change of a reversed block | real interval via one PitchSync shifter pair per upper voice (2.5.3, R2); Octave x 4 voices plays 0, +12, +24, -12 (the shifter's +/- 24 range) | the strings already promise intervals |
| Drone Drift / Rate 1042-1043 | inaudible | +/- 50 cents x Drift at Drift Rate on the upper voices (flat only at +24; Unison voices stay 1 to 50 cents from the root, one side each); the root does not drift (2.5.3) | same; the shifter's pass-through band and range set the exceptions |
| Envelope Attack 1060 in Drones | shaped every loop pass (and set the seam) | shapes only the drone's onset; the seams are the source heads' Hann overlaps | the per-pass envelope was problem 13 |
| Slice Mode 1017 in Drones | no-op | still inert: the source reads the whole window whatever the playhead (2.5.3) | finding 10's ruling (the whole window) |
| Position Jitter 1031 | offset backwards from the newest sample over the whole ring | widens the scatter region from Slice Length (0 %) to the whole readable window (100 %) around the playhead (2.5.2) | the window replaces the ring as the material (finding 11's ruling) |
| Envelope Shape 1062 | Hann / Blackman halves | Linear / Exponential tables that end at 0 (2.8) | the labels were wrong |
| Hits 1021 | the DSP keeps the old absolute count after a Steps change | recomputed from its normalized value whenever Steps changes (`FreezeParams` gains non-persisted `euclideanHitsNorm` and `euclideanRotationNorm`; the Steps case of `handleFreezeParamChange` recomputes both; `loadFreezeParams` sets the norms from the loaded absolute values: `(hits - 1) / (steps - 1)`, `rotation / (steps - 1)`) | the readout already shows the recomputed value; the DSP now agrees |
| Pattern Rate 1023, Burst Rate 1051 | free-running intervals from entry | grid lines phased to the song position (section 4) | tempo-synced controls are expected to sit on the grid |

Removed from the DSP API: `setFreezeEnabled`, `isFreezeEnabled`, `freezeMixSmoother_`, `isCrossfading`,
`isTempoValid`, `tempoValid_`, `lastValidTempo_`, `getPitchIntervalSemitones` (use `getIntervalSemitones` of `T`),
`isCaptureReady(float)` (it read `captureBuffer_`; "a window exists" is `hasWindow()` of D4), `captureBuffer_` and the
`rolling_capture_buffer.h` include. D13 (2) lists every existing case that calls one of these and its fate. `Slice::envelopePhase` stays in `slice_pool.h` (Layer 1,
other users) but Freeze no longer sets it. The processor's `setFreezeEnabled(true)` line goes.

---

## 4. Scheduler and transport

### 4.1 The musical position

The processor fills the context for every mode (one place, `P:205-210`):

```cpp
Krate::DSP::BlockContext ctx{
    .sampleRate = sampleRate_, .blockSize = n, .tempoBPM = tempoBPM, .isPlaying = isPlaying,
    .transportPositionSamples = pc ? pc->projectTimeSamples : 0,
    .projectTimeMusic = pc ? pc->projectTimeMusic : 0.0,
    .projectTimeMusicValid = pc && (pc->state & ProcessContext::kProjectTimeMusicValid) != 0,
};
```
No other Iterum mode reads these fields (grep confirms), so no other mode changes. `tempoBPM` keeps its 120 fallback.

In `PatternFreezeMode::process`, with three clocks `c` (capture `I = Lb`; steps `I = stepB`, Euclidean only; bursts
`I = burstB`, Noise only), each holding `lastIdx` (int64):

```
tempo = isFinite(ctx.tempoBPM) ? clamp(ctx.tempoBPM, kMinTempoBPM, kMaxClockTempoBPM) : 120   // 20..1000 (finding 18)
spb   = sr * 60 / tempo                                         // samples per beat (double)
tol   = 0.5 / spb                                               // half a sample, in beats
synced = ctx.isPlaying && ctx.projectTimeMusicValid
playEdge = ctx.isPlaying && !wasPlaying_                       // 2.2 "A play edge"
pos0  = synced ? ctx.projectTimeMusic : freeRunPos_             // beats at the block's first sample
delta = pos0 - expectedPos_
drift = |delta| < max(1.0 / 64, 0.005 * sr / spb)              // under 1/64 beat or 5 ms: tempo drift, not a jump
jump  = firstBlockAfterReset || playEdge || !drift
rebase(c, pos0):  c.lastIdx = ceil((pos0 - tol) / c.I) - 1      // the boundaries at or after pos0 - tol stay to fire
if (jump) rebase every clock and dirty the cycle unless pos0 is on a capture boundary (2.2)
else      re-anchor: keep every c.lastIdx, use pos0 as the block's position, dirty nothing;
          rebase only a clock whose interval changed this block (Capture, Pattern Rate, Burst Rate)
events: for each clock, every k > c.lastIdx with k * c.I < pos0 + (n - 0.5) / spb,
        at offset o = clamp(round((k * c.I - pos0) * spb), 0, n - 1); then c.lastIdx = k
freeRunPos_ = expectedPos_ = pos0 + n / spb;  wasPlaying_ = ctx.isPlaying
```
`isFinite` is `Krate::DSP::detail::isFinite` (`core/db_utils.h`). `spb` uses the host tempo, not the 300 BPM clamp of
`BlockContext::tempoToSamples` (`block_context.h:32, 117`), so the in-block positions move at the rate the host's
positions do; `kMaxClockTempoBPM` (1000) only keeps `spb` and the event arrays of 4.3 finite.

**Re-anchoring** (finding 18). Within a tempo ramp `expectedPos_` advances at the block-start tempo, so the host
position departs from it by a little every block (about 1.8e-4 beat per 2048-sample block for 120 -> 60 BPM over two
bars). Such a block is not a jump: the clocks take the host position, keep their fired indices, and the cycle stays
clean, so a ritardando captures at every boundary. A forward re-anchor across an unfired boundary fires it at offset
0; a backward one never fires a boundary twice (`lastIdx` holds). Only a real locate, loop wrap or play edge
(`|delta|` of 1/64 beat and 5 ms or more) re-bases and dirties.
Boundaries are fired by index (`k` as int64), never by comparing floating phases. In a continuous run, the previous
block fired exactly the boundaries below `pos0 - tol`, so `rebase` would be a no-op there: a block that starts on a
boundary fires it once, at offset 0, and never twice.

Challenges 1 and 9: a rebase **does not swallow a boundary at the jump position**. `ceil(...) - 1` leaves every
boundary at or after `pos0 - tol` to fire, so a boundary at the jump sample, or within half a sample of it on either
side, fires at offset 0. Negative positions (pre-roll) work through `ceil`, and the step index is taken
non-negatively (2.5.1).

Rules:

- **Transport running with a song position**: everything sits on the host grid. Step 0 of an 8 x 1/8 pattern is the
  bar line (beat 0, 4, 8, ...), and capture boundaries come every `Lb` beats from song start. A hit falls at its exact
  sample, which meets problem 5, the critique's "up to one block early" and spec SC-002 (< 10 ms): the error is
  <= 0.5 sample.
- **Transport stopped, or no song position** (`kProjectTimeMusicValid` absent, standalone hosts, some wrappers): the
  clocks continue from `freeRunPos_`, which is the host position where play stopped (or 0 after reset). The pattern,
  the captures and the hold keep working (the critique's "runs with the transport stopped: keep that"; the task's
  "ideally stopped too"). The archived FR-082a ("silent when stopped or tempo invalid") is withdrawn: the plugin never
  delivers an invalid tempo. Silent input still empties the window within two cycles (2.2, 2.9).
- **Play edge, loop wrap, locate**: a `jump` re-bases every clock to the new position. A boundary at the jump position
  fires there, so playing from a bar line, a loop wrapping to its start bar and a locate to a bar all fire step 0 and
  the capture boundary at offset 0. After a loop wrap or a locate the window in force keeps playing until a capture
  replaces it; after a play edge that dropped the window (2.2), step 0 fires with no window to read and sounds
  nothing, its boundary at beat 0 captures nothing (`heard_` was just cleared), and the later hits of bar 1 read the
  span of the first music after play (R9) (Gradus's
  pattern at
  `plugins/gradus/src/processor/processor.cpp:227` detects `ppq < prev` and resyncs; here a jump in either direction
  is the same case). The cycle that ends at the jump:
  - **A loop wrap that lands on a capture boundary** ends the cycle in progress, and that cycle is complete (2.2) when
    it was recorded from its downbeat. A 4-bar loop wrapping 16.0 -> 0.0 therefore captures beats 12-16, the bar just
    played, at the wrap. The cycle's `elapsed` must be at least half a capture length: a locate that cuts a cycle
    short (beat 5.3 -> 8.0, 1.3 beats) leaves the window in force.
  - **A jump that does not land on a boundary** dirties the cycle in progress, so the next boundary keeps the window
    in force, and promotes a span that holds `needSpan` (2.2).
  - Hosts that do not split a block at the loop point deliver the wrap one block late. The plugin sees a block that
    starts after the bar line and cannot fire that downbeat. That is a host limitation, stated, not worked around.
- **Tempo change**: `spb` is per block (from the host tempo, 20..1000 BPM), and boundaries move with it from the next
  block; a ramp re-anchors every block without dirtying (above). A cycle's `elapsed` is
  whatever passed: 2 beats at 200 BPM and 2 at 100 BPM give 0.6 s + 1.2 s = 1.8 s, more than half a 1/1 cycle at the
  new tempo (1.2 s), so it is captured. The window keeps its sample length. `theta` positions use the current tempo,
  and a slice that no longer fits the window is placed by the read rules (2.4).
- **Interval change** (Capture, Pattern Rate, Burst Rate): that clock re-bases at the block of the change. A Capture
  change also dirties the cycle in progress (2.2).
- **Silence after the DAW starts**: a play edge that finds a window clears `heard_` and the window (2.2). Before the
  first onset after it nothing is captured and nothing plays. After it, a silent bar is a bar like any other: Freeze falls silent with
  its input, and the silent window ducks nothing.

### 4.2 `PatternScheduler` (Layer 2, `S`)

The scheduler gains two methods and a wider callback. The existing `process(numSamples, ctx)` path stays for any
other user (there is none today besides Freeze and its own test); it fires the callback with `sampleOffset = 0`
(`checkAndTrigger` passes 0). Its tests stay: the six `setTriggerCallback([&recorder](int step) ...)` lambdas of
`dsp/tests/unit/processors/test_pattern_scheduler.cpp` (lines 113, 144, 181, 214, 329, 355) and the usage comment
in the header (line 47) take `(int step, size_t)` and ignore the offset. There is one callback type, not two:

```cpp
using TriggerCallback = std::function<void(int step, size_t sampleOffset)>;   // offset within the block
/// Fires the callback for every step boundary k·beatsPerStep with k > the last fired index and
/// k·beatsPerStep < posBeats + (numSamples - 0.5)·beatsPerSample, at offset clamp(round(...), 0, numSamples - 1),
/// when ((k mod steps) + steps) mod steps is a hit.
/// rebase(posBeats, beatsPerSample) sets the last fired index to ceil((posBeats - 0.5·beatsPerSample) / beatsPerStep) - 1
/// without firing: a boundary at posBeats (within half a sample) fires in the next processFromPosition call.
void processFromPosition(double posBeats, double beatsPerSample, size_t numSamples) noexcept;
void rebase(double posBeats, double beatsPerSample) noexcept;
[[nodiscard]] int64_t getLastFiredIndex() const noexcept;
```
`beatsPerStep = getBeatsForNote(noteValue_, noteModifier_)`; `currentStep_` reports `k mod steps` of the last fired
boundary. The capture and burst clocks use the same arithmetic inside `PatternFreezeMode` (a small private
`MusicalClock { double interval; int64_t lastIdx; }` with `rebase` and `nextEventsInBlock`), not three scheduler
instances.

### 4.3 Segment processing

`process()` collects the block's events (capture boundaries, step hits, burst starts) into **three fixed event
arrays, one per clock**, sized in `prepare()` (finding 19). Each holds `kEventCapacity = ceil(maxBlockSize /
minStepSamples) + 2` entries, with `minStepSamples = sr · 60 / (kMaxClockTempoBPM · 24)`: 1/64T, a twenty-fourth of a
beat (index 0 of `kNoteValueDropdownStrings`, `plugins/shared/src/ui/note_value_ui.h:22`), is the shortest interval Pattern Rate and Burst
Rate offer, and Capture's shortest is one beat. At 44.1 kHz that is 110.25 samples, so 77 entries for an 8192-sample
block. **Overflow cannot happen**: a block of `n <= maxBlockSize` samples spans `n / spb` beats, and a clock of interval
`I` fires at most `floor(n / (I · spb)) + 2` boundaries in it (the interior ones plus one within the half-sample
tolerance at each end), with `I · spb >= minStepSamples`. The step clock's callback (4.2) appends to its array only
on a hit; a full array is therefore unreachable, and the append does no bounds-growing.
The three arrays are each in offset order already; `process()` merges them (no sort), and at equal offsets takes the
capture first, then hits, then bursts, so a hit on a bar line reads the window captured on that bar line. It then
processes the block as segments `[a, b)`:
1. Store the segment's input into `rec_` (Hold or not, 2.3) and update `cycleOffset_`, the window peak, the onset,
   `heard_` and `sinceAboveFloor_`.
2. Render all active voices for `[a, b)`, per sample. The Granular Poisson countdown fires inside this loop at its
   own sample (it is not an event-array clock); a grain at cycle offset `t` reads `rec_` only below `t` (2.4). The
   drone renders a 64-sample chunk whenever `sampleCounter_ % 64 == 0` (2.5.3).
3. Handle the events at `b`.

Everything a trigger reads therefore exists exactly up to the trigger sample, and a slice starts at its own sample
(problem 5). Events at offset 0 are handled before the first segment.

---

## 5. State and compatibility

### 5.1 The tail

`F` gains, after the existing functions:

```cpp
// Capture tail: the LAST fields of the state, after saveMultiTapLengthParams (multitap_params.h). A state without
// it was saved before the Freeze redesign: it loads as Hold off, Capture 1/1, Dry = Mix, which keeps the saved
// Mix value's crossfade meaning.
inline void saveFreezeCaptureParams(const FreezeParams& p, Steinberg::IBStreamer& s);   // int32 hold, int32 captureLength (0-3), int32 dryMode (0-1)
inline void loadFreezeCaptureParams(FreezeParams& p, Steinberg::IBStreamer& s);         // absent: hold 0, captureLength 2, dryMode 0; present: clamp to range
template <typename SetParamFunc> inline void loadFreezeCaptureParamsToController(Steinberg::IBStreamer& s, SetParamFunc setParam); // absent: (1070, 0.0), (1071, 2/3), (1072, 0.0)
inline void syncFreezeCaptureParamsToController(Steinberg::IBStreamer& s, Steinberg::Vst::EditControllerEx1& c);
```
`FreezeParams` gains `std::atomic<int> hold{0}`, `captureLength{2}`, `dryMode{1}` (the fresh defaults: Insert) plus the
two non-persisted norms of 3.4. Call sites, each the last call of its list:

| Where | After | Call |
|---|---|---|
| `Processor::getState` (P:352) | `saveMultiTapLengthParams` | `saveFreezeCaptureParams(freezeParams_, streamer)` |
| `Processor::setState` (P:388) | `loadMultiTapLengthParams` | `loadFreezeCaptureParams(freezeParams_, streamer)` |
| `Controller::setComponentState` (C:301) | `syncMultiTapLengthParamsToController` | `syncFreezeCaptureParamsToController(streamer, *this)` |
| `Controller::loadComponentStateWithNotify` (C:1385) | `loadMultiTapLengthParamsToController` | `loadFreezeCaptureParamsToController(streamer, setParamWithNotify)` |

The tail is positional (no version field): any later feature appends **after** these three int32.

A state saved with Hold On loads with Hold On. Hold waits for the first capture after the reload (2.3) and then holds
the first music played, up to the next bar line: never silence and never stale audio.

### 5.2 What a saved Freeze state from before loads as

Every field of the Freeze block loads as saved (unchanged format, F:588-692). The absent tail gives Hold off, Capture
1/1, **Dry = Mix**, so the saved Mix keeps its crossfade law. The engine semantics are new, so every old Freeze
preset sounds different: it now plays held material instead of a late copy of the input. Concretely, the ten factory
presets (`plugins/iterum/resources/presets/Freeze/*.vstpreset`, generated by `tools/preset_generator.cpp:1260-1420`,
which writes no tail: consequences S2):

| Preset | Saved | Loads as (new engine, Dry = Mix) |
|---|---|---|
| Euclidean Pulse | Euclidean 3/8 at 1/8, 150 ms, Mix 70 % | the previous bar's downbeat, 150 ms, three times per bar under a -10 dB dry |
| Ghost Pattern | Euclidean 5/16 at 1/16, rotation 3, 100 ms, Mix 30 % | five short downbeat stutters per bar, quiet; Rotation now audible |
| Total Freeze | Euclidean 4/4 at 1/4, 250 ms, Mix 100 % | four downbeat stutters per bar, wet only (silence between) |
| Slice Machine | Euclidean 12/32 at 1/32, Variable, 80 ms, Mix 80 % | the held bar re-sampled in 80 ms slices on a 12-of-32 grid (Variable now walks the bar) |
| Granular Cloud | Granular, Variable, Slice 300 ms, 15 Hz, jitter 70 % / 40 %, grains 120 ms, Mix 50 % | a grain cloud that follows the held bar, grains within +/- 745 ms of the bar position (R = 0.3 + 0.7 x 1.7 s at 120 BPM, 1/1) |
| Scatter Drift | Granular, Variable, Slice 400 ms, 6 Hz, jitter 90 % / 60 %, grains 200 ms, Mix 55 % | sparse grains within +/- 920 ms of the bar position, nearly the whole held bar (formerly from anywhere in the ring) |
| Harmonic Bed | Drones 3 voices Octave, 500 ms, Mix 60 % | a sustained wash of the whole held bar (four aligned 500 ms source passes from anywhere in it), the root direct, +12 and +24 through PitchSync shifters (the +24 voice drifts flat only) |
| Fifths Drone | Drones 4 voices Fifth, 600 ms, Mix 65 % | the same with 600 ms passes, root, +7, +14 and +21 semitones |
| Noise Texture | Noise, Pink, 1/8, LP 3 kHz sweep 60 %, 200 ms, Mix 45 % | eighth-note bursts of the held downbeat + pink noise; on silence, noise alone while the transport plays (2.5.4) |
| Radio Static | Noise, Radio, 1/16, BP 5 kHz sweep 80 %, 150 ms, Mix 40 % | sixteenth-note bursts with radio-static noise |

R7: the preset files are not regenerated by this build; they load as Dry = Mix, as the table says.
`preset_loading_consistency_test.cpp:137-148, 400` keep passing: the Freeze block is unchanged and the tail is read
after it.

**Follow-up after the audition (R7, not part of this build).** Extend `tools/preset_generator.cpp` with the state
tails it does not write today (the Tape sync tail, the MultiTap Length tail and this Freeze capture tail, in that
order, after `writeFreezeState`'s block sequence), set Dry = Insert on the rhythmic / granular / noise presets,
regenerate the ten files and commit them. It waits for the user to judge the new sound first. R7 and the former open
question 2 say "eight"; `preset_generator.cpp:1260-1430` defines ten Freeze presets, and the user ruled on
2026-10-29 (R10) that Total Freeze keeps Mix 100 % wet-only, as its name promises, so with the two Drones presets
excluded as well seven remain: Euclidean Pulse, Ghost Pattern, Slice Machine, Granular Cloud,
Scatter Drift, Noise Texture and Radio Static. The follow-up sets those seven.

**Done (2026-10-29, after the audition).** The user accepted an 11-preset set: the ten names re-tuned plus Stutter Fill (Euclidean 4 of 4 at 1/16, Capture 1/4, a beat-repeat roll). Every preset sets Hold, Capture and Dry; Insert on the rhythmic, granular and noise presets (R10); Total Freeze keeps Mix 100 % (R10) and now loads with Hold on, so the first bar of music loops; Fifths Drone also loads held. `tools/preset_generator.cpp` writes the three tails, so the 90 non-Freeze presets gain exactly the 24 bytes they already loaded as (Tape Free, MultiTap Free, Hold off, Capture 1/1, Dry = Mix), checked chunk by chunk. The hidden case "PROBE Freeze factory presets" renders each preset on the tone, drum and chord inputs (`probe/presets.txt`). It found the 32-step Euclidean mask bug (`core/euclidean_pattern.h`: `(1u << 32) - 1u` gave an empty mask, so Slice Machine, 12 of 32 rotated, never fired), fixed test-first.

### 5.3 Approval goldens

None. `plugins/iterum/tests/approval/approved/` holds OnePoleSmoother, applyGain and softClip only (consequences
F11); `test_approvals_main.cpp` names no Freeze case. No golden may be re-approved under this design; an
`approval_tests` red is a regression.

### 5.4 Changelog (for the close stage, open 1.0.0 entry of `plugins/iterum/CHANGELOG.md`)

- Fixed: **Freeze produced no audible effect at its defaults**. Each hit played the newest 200 ms of input, so the wet
  was the input itself, 190 ms late, under an equal dry. Freeze now records every bar (Capture, 1/4 to 2/1) and
  stutters the previous bar's downbeat on the Euclidean pattern. A fresh instance is 100 % wet in Insert, so the
  music keeps going and the stutter punches in. Saved presets keep their Mix law and their settings, and now play
  held audio.
- Fixed: **Harmonic Drones and Noise Bursts buzzed at the host's buffer rate** (they looped one audio block
  backwards). Both now play forward from the held audio. Drones are a sustained wash of the whole held bar, with the
  upper voices pitched by the Shimmer pitch shifter at real intervals. Noise Bursts play their noise on silence too
  while the song plays.
- Added: **Hold** beside Mix (automatable: momentary from a controller). It keeps what plays, and in Insert it mutes
  the input while held and each slice sustains until the next hit, so the held bar keeps sounding. Releasing it
  brings the input straight back. Also added: **Capture** length, and **Dry** = Mix / Insert. Slice Mode now does
  something: Fixed repeats one slice, Variable replays the held bar in place. In Granular, Position Jitter now
  reaches the whole held bar and Slice Length sets the smallest region. The pattern sits on the song's bar lines,
  retriggers at loop wraps, and keeps running when the transport stops. It follows the input to silence unless Hold
  is on, and silence never cuts holes in the input.
- Changed: Iterum reports an infinite tail in every mode, so hosts no longer suspend it while a delay or a held
  Freeze is still sounding.
- Fixed: Envelope Shape curves match their names and end at silence; the Hits readout follows a Steps change;
  Granular Density averages what it says; changing Pattern no longer buzzes for half a second.

No other plugin includes `pattern_freeze_mode.h` (checked: `grep -rl pattern_freeze_mode.h plugins/*/src` lists only
Iterum). `pattern_freeze_types.h` is included by Membrum, Ruinae and Vorago code (section 7.3 builds and runs them).

---

## 6. Expected measurements

### 6.1 The probe

`PROBE Freeze signal path at defaults` (`dsp/tests/unit/effects/test_pattern_freeze_mode.cpp`, hidden
`[.][probe][freeze-path]`) is rewritten, not duplicated. Base setup:
- **Render**: 44.1 kHz, block 512, 120 BPM, 6 s.
- **Input**: 233 Hz for [0, 2 s), 311 Hz for [2, 4 s), 415 Hz for [4, 6 s), amplitude 0.5, both channels. One pitch
  per bar, so the held bar and the live bar are always a different note. The tones are off the commensurate
  220 / 440 / 880 of the earlier probe (finding 9): at 120 BPM no beat or half beat holds a whole number of their
  periods (233 x 0.25 s = 58.25 cycles, 311 x 0.25 = 77.75, 415 x 0.25 = 103.75), so no in-phase case hides a
  cancellation.
- **Settings**: the plugin's fresh defaults (3.3), with the pattern rate pushed explicitly
  (`setNoteValue(NoteValue::Eighth)`).
- **Measurement**: at **Mix 100 %, Dry = Mix**, so the output is the wet alone. A second render at Dry = Insert
  gives the "Insert" lines and the audibility rows.
- **Transport**: for `playing=1` the context carries `isPlaying = true, projectTimeMusicValid = true,
  projectTimeMusic = sample / 22050.0` from the first block (a play edge that finds no window: the span plays as
  after `reset()`, 2.2); for `playing=0` neither (free-run).

Per row it prints:

- `wet` RMS re the input over 1-6 s, `peak` (max 10 ms window ratio), `active` fraction of 10 ms windows above -40 dB;
- `held` = the dominant tone of the wet in [2.5 s, 3.5 s) (Goertzel at 233 / 311 / 415 and, for Drones, 466; the
  input there is 311);
- `r` = the best cross-correlation of the wet against the input within 0..600 ms lag over [2, 3 s) (the old column:
  today 0.69);
- `captures` = `getCaptureCount()` at 6 s;
- for Euclidean:
  - a `hold` row: `setHold(true)` at 1.9 s, the dominant tone in [4.5, 5.5 s) (input 415), and `getCaptureCount()`
    before and after the 4 s boundary;
  - an `Insert` line: the output's input share over the gaps and over the slice plateaus (2.7's definitions);
  - an `Insert hold` line: the same with Hold on from 1.9 s, plus the active fraction of the output over [2.2, 6 s)
    (R5);
- the **audibility rows** (R6): at the fresh defaults (Mix 100 %, Insert), for three inputs, each 6 s with the
  transport playing:
  - `tone`: the pitch-per-bar input above;
  - `drum`: the one-bar drum loop of D8 (3) (kick on every beat, hat on every off-beat, a 233 Hz pad), repeated;
  - `chord`: a sustained chord at 233.0, 293.7 and 349.2 Hz, 0.2 each, constant over the 6 s;

  each printing `diff` = the energy of `(out - in)` re the energy of `in` over [2, 6 s), and `moved` = the fraction
  of 10 ms windows in [2, 6 s) where the RMS of `(out - in)` exceeds -20 dB re the RMS of `in` over [2, 6 s), plus
  `level` = the median over those windows of `|20 log10(RMS(out) / RMS(in))|` (printed, not gated: see 6.2);
- for Granular, an `Insert` line: the 10 ms modulation depth of the dry gain `(out - wet) / in`, as p95/p5 in dB,
  where `wet` comes from the Dry = Mix render;
- for Drones:
  - a `block` line: the same render at block 64 and 512, RMS of the difference re the RMS of the 512 render;
  - a `sweep` line: Voices 1 and 2, Octave, Drift 0, over a stationary tone window at every 1 Hz from 200 to 260 Hz
    plus 233 and 311 Hz: the worst-case (max/min) 10 ms RMS depth of the wet over [3, 6 s);
  - a `drum` line: the 10 ms RMS p95/p5 depth (dB) of the drone on the drum window of D8 (3), the same measure of
    the drum window itself, and the difference (R8);
- for Noise, a `block` line and a `silence` row: 233 Hz for [0, 2 s), then zero input; the wet RMS in dBFS over
  [4.5, 6 s) with the transport playing, and over [6.5, 12 s) with it stopped (a 12 s render);
- a `capped` row: 80 BPM, Capture 2/1, a unit impulse on the first sample of every 2/1 cycle over a 233 Hz bed. It
  prints `getWindowLengthSamples()` and `peekWindow(0)` after the first complete cycle;
- a `128 BPM` row: Capture 1/1 over 8 bars, `getCaptureCount()` and the set of window lengths.

### 6.2 The table the build must produce

Derived from section 2; tolerances are the bars the audio review applies. The numbers are predictions, the implementer
records the measured ones in `probe/after.txt`. A row that misses its bar goes back to the design, never to a wider
bar.

| Row | wet (dB re in) | peak | active | held tone [2.5, 3.5 s) | r (0-600 ms) | Notes |
|---|---|---|---|---|---|---|
| Euclidean playing=1 | -7.0 +/- 1.0 | 0 +/- 1 | 30 +/- 4 % | **233 Hz** (input 311) | printed (R11) | duty 3 x 200 / 2000, envelope energy 0.656; captures 2 (2 s partial, 4 s complete; the 6 s boundary is the first sample after the render) |
| Euclidean playing=0 | same within 0.2 dB / 1 % | | | 233 | printed (R11) | free-run from entry = PPQ from 0 |
| Euclidean hold (1.9 s) | | | | [4.5, 5.5 s): **233 Hz** (input 415); `getCaptureCount()` equal before and after 4 s | | the 4 s boundary does not capture |
| Euclidean Insert line | over the gaps `out == in` within 1e-6; over the slices' plateaus the input share is < -60 dB | | | | | 2.7 (gap: 10 tau of `gv` after the envelope reaches 0; plateau: 7 tau after it reaches 1) |
| Euclidean Insert hold line | from 2.2 s the input share is < -60 dB everywhere, gaps included | | **>= 80 %** over [2.2, 6 s) (prediction about 99 %) | | | 2.3, R5: Hold engages at the 2 s capture; 7 tau of `gh` = 28 ms; slices sustain until the next hit, so the held bar is continuous |
| **Audibility `tone`** (Mix 100, Insert) | `diff` **>= -10 dB** (prediction about -4.0) | | `moved` **>= 12 %** (prediction about 29 %) | | | every slice plays the previous bar's note: `2 x 0.30 x 0.656` = 0.39 of the input energy; `moved` = the slice time with `env > 0.07` |
| **Audibility `drum`** | `diff` **>= -10 dB** (prediction about -6) | | `moved` **>= 12 %** (prediction about 17 %) | | | the bar is a loop, so the step-0 slice is like for like; the step-3 slice puts the downbeat kick on the off-beat, the step-6 slice differs only by the pad's phase |
| **Audibility `chord`** | `diff` **>= -10 dB** (prediction about -4.5) | | `moved` **>= 12 %** (prediction about 25 %) | | | the replayed chord differs only in phase (233 x 0.75 s = 174.75 cycles at step 3); R6: if this row misses with Fixed, Slice Mode's fresh default becomes Variable |
| GranularScatter playing=1/0 | -5 +/- 3 | <= +3 | 60 +/- 12 % | 233 | printed (R11) | 10 Hz x 100 ms: P(any grain) = 1 - e^-1 = 63 %; R = 1.1 s, all inside the first held bar |
| GranularScatter Insert line | dry gain constant: modulation depth < 0.1 dB | | | | | g = 0 (2.7) |
| HarmonicDrones playing=1/0 | 0 +/- 2 | <= +2 | >= 95 % | 233 **and** 466 (voice 1 at 2x), 415 absent (> 30 dB below 233); 466 measured by the R12 peak search (within 3 cents on a Drift 0 line; the default Drift 30 % swings the voice +/- 15 cents by design) | printed (R11) | 2 voices x 1/sqrt 2 of different pitch sum to 0 dB; the aligned source is at the window's level (2.5.3) |
| HarmonicDrones block | RMS(diff 64 vs 512) / RMS < 1e-3 | | | | | 64-sample chunk grid |
| HarmonicDrones sweep | worst-case 10 ms depth **<= 3 dB** over 200-260 Hz (1 Hz steps), 233, 311 Hz, Voices 1 and 2 | | | | | finding 9; the source alone simulates at 0.0-0.6 dB aligned; the shifter's splices add the rest |
| HarmonicDrones drum | p95/p5 10 ms depth of the drone **<= the drum window's own depth - 6 dB** (R8; prediction 16.0 against 22.3 dB) | | | | | the relative bar of R8; the probe prints both depths and the difference |
| NoiseBursts playing=1/0 | -5 +/- 3 | <= +1 | 80 +/- 4 % | 233 (slice part) | printed (R11) | 200 ms bursts every 250 ms |
| NoiseBursts block | RMS(diff 64 vs 512) / RMS < 1e-3 | | | | | same RNG sequence per sample; cutoff grid per burst |
| NoiseBursts silence, playing | wet over [4.5, 6 s) between -30 and -14 dBFS | | 80 +/- 4 % | | | noise-only bursts: 0.5 x -10.0 dB x envelope (2.5.4, R14; -10.5 dB measured -30.03 dBFS and 75.9 %; -12 dB measured -31.5 dBFS and 75.6 %) |
| NoiseBursts silence, stopped | wet over [6.5, 12 s) **< -90 dBFS** | | | | | two cycles after the last input (2 s + 4 s), plus one burst and its release |
| capped (80 BPM, 2/1) | | | | | | window length 220500 exactly; `peekWindow(0)` = the impulse +/- 1e-6 |
| 128 BPM, 8 bars | | | | | | captures 8; complete windows 82687 or 82688 samples |

Semantics for a held loop: once a window has been captured the wet is a function of the window, not of the live
input, so `r` (the old 0.69) must fall near zero in every row, and the `held` tone must be the previous bar's.

About the audibility rows (R6). `diff` and `moved` compare samples, so they count a phase-shifted copy of the same
material as different. On the `chord` input the replayed downbeat is the same chord at another phase, and the rows
are predicted to pass with Fixed although the ear hears mostly the crossfade dips. The `level` column (the median
per-window level change) is printed beside them, not gated, so the audition can judge how much of `diff` is change of
sound rather than change of phase. The bars and the R6 rule are as ruled.

---

## 7. Implementation plan

Order: D-rows first (DSP implementer; `dsp_*` suites, the probe), then P-rows (plugin implementer; `plugin_tests`,
`approval_tests`, `Iterum`, the screenshots). Every row names its proving case. The Catch2 file for the
`PatternFreezeMode` rows is `dsp/tests/unit/effects/test_pattern_freeze_mode.cpp` (tags
`[effects][pattern_freeze][layer4]`, plus `[freeze-held]`, `[freeze-transport]`, `[freeze-drone]` for the new groups);
other rows name their file. Real-time rules for every D-row:
- no allocation, lock, exception or I/O in `process()`;
- everything (the three cycle buffers, the pool, the two burst buffers, the three event arrays, the six
  `PitchShiftProcessor`s and the drone FIFOs) sized in `prepare()`; the event arrays cannot overflow (4.3);
- finiteness via `Krate::DSP::detail::isFinite` only: the host tempo of 4.1 is the one planned check;
- no `numeric_limits::infinity()` seeds; `findSplice` seeds its best score with `std::numeric_limits<float>::lowest()`;
- the shifters get `setSemitones` within +/- 24 and `setCents` within +/- 50 (2.5.3), never a ratio inside their
  pass-through band;
- `findSplice` and the drone head latch run at a trigger or a pass start, outside the per-sample loop, bounded by
  2.10's search size.

Shared test helper for D4-D11: a fresh `PatternFreezeMode` per render, `setDryWetMix` before `prepare()` so the Mix
smoother starts on its target. `wet` is taken from a Mix 100 % / Dry = Mix render, which is deterministic for the same
settings and input. This is how D10 sees the wet on its own without a new API (challenge 20).

### 7.1 DSP

| Row | File / function | Change | Case |
|---|---|---|---|
| D1 | `H`: the private nested `CycleBuffer` x3 of 2.1 (`capacity_ = ceil(5 s · sr)`, plus its latched `peak`), `readWindow`, `readWindowLinear` (forward neighbour), `copyWindow` (the read rules of 2.4), `playFollowsSpare_` / `recFollowsPlay_`, and the test accessors `getWindowFrom()`, `getWindowLengthSamples()`, `getWindowPeak()`, `isWindowSilent()`, `peekWindow(size_t off, float& l, float& r) const`; remove `captureBuffer_` and the `rolling_capture_buffer.h` include | cycle buffers | (1) "Cycle buffers store each cycle from its downbeat and read forward": 1/4 capture at 120 BPM over a ramp input (`x[t] = t / 1e6`): after the second boundary `getWindowFrom() == 0`, `getWindowLengthSamples() == 22050` (one beat), `peekWindow(k) == ramp at boundary + k` for k = 0, 1, 22049 within 1e-7, `readWindowLinear(100.5)` = the mean of samples 100 and 101. (2) "A window latches its peak": 1/4 capture, a 0.5 sine for two beats then zero input: the window captured at beat 2 has `getWindowPeak()` 0.5 +/- 1e-3 and `isWindowSilent() == false`; the one captured at beat 3 has `isWindowSilent() == true` |
| D2 | `T`: `CaptureLength`, `DryMode`, names, beats, constants of 3.1; remove `kPatternCrossfadeMs` and `kDefaultCaptureBufferSeconds`. `dsp/tests/unit/core/pattern_freeze_types_test.cpp`: delete the SECTION "Pattern crossfade is positive" (:241-243) and the two `kDefaultCaptureBufferSeconds` REQUIREs (:236-237) | types | in `pattern_freeze_types_test.cpp` (dsp_core_tests): "Capture length names and beats" (4 entries: 1/2/4/8 beats, "1/4" "1/2" "1/1" "2/1", default Whole) and "Dry mode defaults to Insert" |
| D3 | `S` `PatternScheduler::processFromPosition`, `rebase(pos, beatsPerSample)`, `getLastFiredIndex`, the two-argument callback (the six test lambdas and the header comment widened to `(int step, size_t)`; legacy `process()` passes offset 0; see 4.2) | bar-phased steps with offsets | `dsp/tests/unit/processors/test_pattern_scheduler.cpp`, "processFromPosition fires hits at their sample offset once", 3 of 8 at 1/8, 120 BPM, 44.1 kHz (`bps = 1/22050`). (a) `rebase(7.9, bps)`, then a **4096-sample** block at 7.9 (it spans [7.9, 8.0858)): fires step 0 (k = 16) at offset round(0.1 x 22050) = 2205, exactly once. (b) The next 4096-sample block at 7.9 + 4096/22050: fires nothing (8.5 lies past 8.2715). (c) `rebase(100.0, bps)`, then a block at 100.0: fires step 0 (k = 200) at offset 0. (d) `rebase(8.00001, bps)` (0.22 sample after the boundary): k = 16 fires at offset 0; `rebase(8.0001, bps)` (2.2 samples after): it does not, and the next fire is k = 17. (e) Backward loop: continuous 512-sample blocks up to 16.0, then `rebase(0.0, bps)` and a block at 0.0: step 0 (k = 0) fires at offset 0. (f) Pre-roll: `rebase(-2.0, bps)`, blocks from -2.0: k = -2 (step 6, a hit) at 22050 samples and k = 0 at 44100, each +/- 0 |
| D4 | `H`: the capture rule of 2.2 (`cycleOffset_`, `recFrom_`, `onsetOffset_`, `heard_`, `sinceAboveFloor_`, `captureCount_`, dirtying events, `needSpan`, rotation, silent windows, the play edge), the span, the capture `MusicalClock`, segment processing of 4.3, `process()` rewritten around it; `setCaptureLength`, `hasWindow`, `getCaptureCount`, `getSliceTriggerCount` (test accessor: slices started since `reset()`) | the held window | `[freeze-held]`, each at the defaults of 3.3 with Mix 100 / Dry Mix unless it says Insert. (1) "holds the previous capture cycle": 233 Hz for 2 s then 311 Hz; Goertzel power of the wet in [2.5, 3.5 s) at 233 > 10 x at 311 (R11: this carries "held, not live"); `r` against the input over [2, 3 s) is printed, not asserted (a held pure tone lines up in phase with the input of the bar before, so ideal output reads about 0.288). (2) "captures at every boundary at 128 BPM": 8 bars of 233 Hz, `getCaptureCount() == 8` after beat 32 + one block; every complete window's length is 82687 or 82688. (3) "a capped cycle starts on its downbeat": 80 BPM, 2/1, the impulse input of 6.1: `getWindowLengthSamples() == 220500` and `peekWindow(0)` = the impulse. (4) "a silent cycle replaces the window": 1/1, music [0, 2 s), silence [2, 4 s): the count rises at 4 s, `isWindowSilent()` from 4 s, and the wet RMS over [4.4, 6 s) is < -90 dBFS. The same with Hold on at 1.9 s: the wet in [4.4, 6 s) is still 233 Hz. The same at 2/1 (music 4 s, then silence): the window goes silent at the 8 s boundary. (5) "20 s of silence, then music": free-run, zero input for 20 s, then 330 Hz: no wet before the onset; the first hit after the onset + 200 ms has its Goertzel peak at 330; `getCaptureCount()` stays 0 through the silence. The same with 30 s stopped and then `playing=1` from beat 0 (a play edge that finds no window): the span plays, and the wet comes only from post-play audio. (6) "entry 0.1 beat after a bar line": `projectTimeMusic` from 0.1: the hit at beat 1.5 sounds (RMS > -20 dB re input) and reads the onset. (7) "a Capture change mid-cycle dirties it": 1/1 -> 2/1 at beat 2 (after a complete window exists): the boundary at beat 8 does not capture; the one at 16 does. (8) "no capture before the onset": 10 s of zero input: `hasWindow() == false`, count 0. (9) "a silent window triggers nothing" (findings 8, 16): music [0, 2 s), silence [2, 4 s), music from 4 s: `getSliceTriggerCount()` does not rise over [4, 6 s). (10) "play after a silent stop stutters the new music, never the old or silence" (finding 16's case under R9): Insert 100; stopped (free-run) 233 Hz for 2 s, then zero input for two cycles (4 s), then a play edge at `projectTimeMusic = 0.0` with 311 Hz: `hasWindow()` is false right after the play edge; in bar 1 ([0, 2 s) after play) the wet (`out - in`) is < -90 dBFS until the first hit that follows the onset by `needSpan`, and every slice that plays in bar 1 (steps 3 and 6 of E(3,8), beats 1.5 and 3.0) has its Goertzel peak at 311 with nothing at 233 (> 30 dB below); the hits of bar 2 have their Goertzel peak at 311. (11) "play after music, without a silent stop": the same with the stop lasting 0.5 s (the window in force is the 233 Hz music): the play edge dropped that window, so bar 1's slices read 311 (nothing at 233) and bar 2 stutters 311 |
| D5 | `H` `setHold` / `isHeld` / `isHoldEngaged`; recording during Hold, the blocked capture, the waiting hold (no window or a silent one), the release rule and the Insert release gap of 2.3, slice and burst sustain (R5) with the loop seam of 2.10 | Hold | `[freeze-held]`. (1) "Hold freezes the window": hold at 1.9 s: `getCaptureCount()` is equal before and after the 4 s boundary, and the wet tone in [4.5, 5.5 s) is 233 with input 415; release at 5 s: the 6 s boundary captures (count + 1) and the hits from 6 s read the [4, 6 s) bar (Goertzel peak 415). (2) "the release returns the input at once" (finding 7): Insert 100, the same hold and release: over [5.14 s, 6 s) (the 100 ms release of the sounding slice plus 10 tau of `gh`) `out == in` within 1e-6, and the Goertzel power at 233 of `(out - in)` over [5.14 s, 8 s) is < -60 dB re the input's (no held-tone hit after the release). (3) "Hold at 2/1 respects the release rule": 2/1 at 120 BPM, hold at 3.9 s (it engages at the 4 s capture), release at 9 s: the 8 s boundary does not capture, the 12 s one does. (4) "Hold before any window waits for the first capture": Hold on from reset, 233 Hz from 0 s, 311 Hz from 2 s: the wet plays from 0.75 s; `getCaptureCount()` becomes 1 at 2 s and stays 1 through 8 s; the wet after 2.5 s is 233 Hz. (5) "a restored Hold with 500 ms slices is audible": the same with Slice Length 500 ms: the active fraction over [2, 6 s) is > 80 % (slices sustain, R5), and every hit's Goertzel peak is 233. (6) "Hold sustains slices until the next hit" (R5, finding 6): Insert 100 at the fresh defaults, Hold at 1.9 s: the active fraction of the output (10 ms windows above -40 dB re in) over [2.2, 6 s) is >= 80 %, and the maximum sample-to-sample step of the output there is <= 2 x the maximum step of the held 233 Hz material (0.5 x 2 pi x 233 / 44100). (7) "Hold waits on a silent window": music [0, 2 s), silence [2, 4 s), Hold on at 4.1 s with music again from 4 s: `isHoldEngaged()` is false until the 6 s capture, and under Insert `out == in` within 1e-6 over [4.1, 6 s) (no mute into silence) |
| D6 | `H` `onPatternTrigger(int step, size_t offset)`: Euclidean only, the playhead of 2.4, `copyWindow` from `P` under the read rules, per-slice gain at trigger, start at `offset`; `processEuclideanSlices` per segment, with the sustain of R5 | Fixed / Variable | (1) "Fixed replays one slice, Variable plays the slice at the step's position" (finding 21). The input is a bar whose four beats are 220 / 330 / 440 / 550 Hz tones, fed repeatedly from reset. Bar 2 (the window is the first, partial one): under both Fixed and Variable all three hits read the onset, 220 Hz (the partial-window rule of 2.4). Bar 3 (the window is the complete bar 2): Fixed: the three hits are sample-identical copies (max abs diff < 1e-6 over their plateaus, all 220 Hz); Variable: the hits at steps 0, 3 and 6 read as 220, 330 (step 3 = beat 1.5, the second beat's tone) and 550 (step 6 = beat 3, the fourth beat's tone) by Goertzel. (2) "Euclidean hits fire in no other pattern type": Granular with Hits 3 vs Hits 0, same seed: identical output |
| D7 | `H` `processGranularScatter` / `triggerGranularGrain`: the Poisson countdown, the scatter region and placement of 2.5.2, the read rules of 2.4, gain at trigger, `grainTriggerCount_` + `getGrainTriggerCount()`, `getLastGrainStart()` (test accessor: the cycle offset of the last grain, negative inside `spare_`) | Density, region | (1) "Granular density averages the set rate within 20 %": 10 Hz for 20 s gives 160..240 triggers. (2) "Position Jitter 0 scatters inside a Slice Length region": jitter 0, Fixed, Slice Length 200 ms, 400 grains on a steady tone: for every grain triggered after the third capture (from 6 s on; by 2.1 `playFollowsSpare_` needs `spare_` to be a complete, uncapped cycle, and the first window is partial from the onset) the start lies in [-4410, +4410) samples around the downbeat; for every grain triggered before that (no contiguous `spare_`) the start lies in [0, 8820). (3) "Position Jitter widens the region to the whole window, and no grain starts in the last grain length": Fixed, Slice Length 200 ms, 400 grains, a 2 s window: at jitter 50 % the starts' standard deviation is `R / sqrt(12)` +/- 15 % with `R = 0.2 + 0.5 x 1.8 = 1.1 s`; at jitter 100 % the starts reach within 5 % of both ends of the readable range; at every jitter no start is later than `hi - grainLength`. (4) "no grain splices under its envelope" (findings 11, 15): Granular defaults, input 233 Hz for one bar then 311 Hz, phase-continuous at the bar line, for 8 s: the maximum sample-to-sample step of the wet is <= 2 x the maximum step of a clean 311 Hz grain at the same level (0.5 x 2 pi x 311 / 44100) |
| D8 | `H` `processHarmonicDrones`: the drone source of 2.5.3 (four Hann heads at rate 1, pass `Lp`, uniform starts over the whole window, `findSplice` alignment, per-head latch, forced fade), the voices (root direct; a `PitchShiftProcessor` pair per upper voice in `PitchMode::PitchSync`; the +24 fold; the drift rules), the 64-sample chunk grid and FIFO, the voice fades, the global envelope; `<krate/dsp/processors/pitch_shift_processor.h>` included | Drones | `[freeze-drone]`. (1) "Drones are in tune and independent of the block size": a 233 Hz window, Octave, 2 voices: the upper voice's peak, found by a Goertzel search within +/- 10 cents of 466 Hz in 0.5-cent steps (R12), lies within 3 cents of 466 Hz and its power is within 1.5 dB of the peak at 233; at the default Drift 30 %, RMS(diff 64 vs 512) / RMS < 1e-3. (2) "the drone level is steady" (finding 9's sweep): Octave, Drift 0, Fixed, Voices 1 and 2; for every window tone f = 200, 201, ..., 260 Hz plus 233 and 311 Hz (amplitude 0.5, stationary): the worst-case 10 ms RMS depth (max / min) of the wet over [3, 6 s) is <= 3 dB, and the wet level is 0 +/- 2 dB re the window. (3) "Drones on a drum window" (finding 10): the window is the D8 drum bar (per beat a kick: 60 Hz, 80 ms exponential decay, peak 0.8; on the off-beat a hat: white noise, 20 ms decay, peak 0.25; a 233 Hz pad at 0.05); Voices 1, Drift 0, Fixed: the 10 ms RMS p95/p5 depth of the wet over [3, 6 s) is at least 6 dB below the same measure of the drum window itself (R8; predicted 16.0 against 22.3 dB); the case prints both depths. (4) "no seam click at Attack 0": Attack 0, 233 Hz window, 2 voices: the maximum sample-to-sample step of the wet is <= 2 x the maximum step of the ideal two-tone sum. (5) "re-anchoring never clicks": Variable, Capture 1/4 (a re-capture every beat) on the four-tone bar of D6: the maximum sample-to-sample step of the wet over 8 s is <= 2 x the maximum step of the ideal output (the root plus each upper voice's exact interval of the window's tone at the same levels; R13: an octave at 550 Hz alone has step / peak 0.118, above the former 0.1 bar). (6) "Drift 100 % swings the upper voice +/- 50 cents": Voices 2, Octave, Drift 100 %, Rate 0.5 Hz, 233 Hz window: the instantaneous frequency of the 466 Hz component (two cascaded `Biquad` bandpasses at 466 Hz, Q 8, then zero crossings) over 4 s spans 466 x 2^(+/-50/1200) within +/- 10 cents at both ends, and the 233 Hz component does not move (+/- 2 cents). (7) "Octave x 4 voices folds the top voice down": 233 Hz window, Octave, 4 voices, Drift 0: peaks found by the R12 search (+/- 10 cents in 0.5-cent steps) at 116.5, 233, 466 and 932 Hz, each within 3 cents of its interval and within 3 dB of the 233 Hz power, and nothing at 1864 Hz (< -30 dB re 233). (8) "a Unison voice never reaches the shifter's pass-through": Unison, 2 voices, 233 Hz window, Drift automated 0 -> 100 % -> 0 in 2 s ramps over 10 s: the maximum sample-to-sample step of the wet is <= 2 x the maximum step of the ideal output (root plus the voice at its current detune, same levels; R13) (no one-grain jump) |
| D9 | `H` `processNoiseBursts`: bar-phased bursts with offsets, two burst voices with their preallocated buffers (copy at the trigger under the read rules), the older voice's 5 ms fade, noise at `kNoiseLevelDb` (-10.0 dB, R14) for all colours, filter set per burst, cutoff every 32 samples from the burst start, the noise-only rule of 2.5.4, the sustain under Hold (R5) | Noise | (1) "Noise-only bursts follow the song" (finding 12): 233 Hz for [0, 2 s), then zero input. Transport playing: the wet RMS over [4.5, 12 s) is between -30 and -14 dBFS, active 80 +/- 4 %. Transport stopped: the wet over [6.5, 12 s) is < -90 dBFS. From reset with zero input and the transport playing for 10 s: the wet is < -90 dBFS throughout (`heard_` never set). (2) "Noise Bursts play the slice forward and block-size independent": chirp window, rising zero-crossing rate within each burst; RMS(diff 64 vs 512) / RMS < 1e-3. (3) "every colour plays at `kNoiseLevelDb`" (-10.0 dB, R14; the case reads the constant, not a literal): one 10 ms click at 0.5 at t = 0 (it sets `heard_`), then zero input, transport playing, LowPass at its maximum cutoff, Sweep 0. For each of the eight colours, the wet RMS over [4.5, 8 s) re White's is within 1 dB of the same ratio measured on a standalone `NoiseGenerator` with only that colour enabled at -12 dB (today five colours are 8 dB low). (4) "overlapping bursts do not click" (finding 20, Radio Static rate): Burst Rate 1/16, Slice Length 150 ms, a 233 Hz window, 8 s: the maximum sample-to-sample step of the wet is < 0.05 x its peak. (5) "a capture during a burst does not switch it": Slice Length 2000 ms, Burst Rate 1/1, Capture 1/4 (four captures during each burst), input changing tone every beat: within each burst the slice part's Goertzel peak stays the tone of the burst's trigger-time window. (6) "a held burst sustains": Hold at 1.9 s, Insert 100: the active fraction of the output over [2.2, 6 s) is >= 80 % |
| D10 | `H` `process()` mix stage: `setDryMode`, the per-type gate `gv` (noise-only bursts 0, release-gap target 0), `duckSmoother_` (`OnePoleSmoother`, `configure(kDuckSmoothingMs = 2)`: `coeff = exp(-2500 / sr)`, tau 0.4 ms), the hold gate `gh` and `holdDuckSmoother_` (`configure(kHoldDuckSmoothingMs = 20)`: `coeff = exp(-250 / sr)`, tau 4 ms), the two laws of 2.7; `kDefaultDryWetMix = 100` | Dry | `wet` from the helper's Dry = Mix render; gap and plateau as defined in 2.7 (gap: at least 10 tau after the gate target last left 0; plateau: at least 7 tau after it reached 1). (1) "Insert ducks the dry only while a voice plays": Mix 100, Insert: in the gaps `out == in` within 1e-6; over a slice's plateau `out == wet` (input share < -60 dB). (2) "Dry = Mix is the crossfade": `out == in · 0.7 + wet · 0.3` within 1e-6 at m = 0.3. (3) "Hold mutes the input under Insert": Insert 100, Hold on at 1.9 s (it engages at the 2 s capture): from 2.028 s (7 tau of `gh`) the input share `(out - wet) / in` is < -60 dB in the gaps too; Hold off at 4.5 s (the release gap runs to the 6 s capture): over [4.64 s, 6 s) (the 100 ms release of the sustained slice plus 10 tau of `gh`) `out == in` within 1e-6. (4) "Granular leaves the dry alone under Insert": Granular, Insert 100: `out - wet == in` within 1e-6 everywhere. (5) "the duck never steps at Attack 0": constant input 0.5, Attack 0, Insert 100: the dry gain `(out - wet) / 0.5` changes by at most `1 - exp(-2500 / sr) + 1e-4` (0.0552 at 44.1 kHz) per sample. (6) "silence never ducks the dry" (finding 8's case): Insert 100, music for [0, 2 s), silence for [2, 4 s), music from 4 s: `out == in` within -60 dB re in over [4, 6 s) (the window there is the silent bar, so nothing non-silent plays). (7) "a noise-only burst does not duck": Noise Bursts, Insert 100, the window silent (music [0, 2 s), silence [2, 4 s), music from 4 s), transport playing: over [4, 6 s) `out - wet == in` within 1e-6 |
| D11 | `H` `setPatternType`: remove the stored-block crossfade; release / finish rules of 2.6 | pattern change | "Pattern change leaves no block-rate artefact": Drones -> Euclidean at sample **132608** (= 259 x 512 = 2072 x 64, on both block grids); RMS(diff 64 vs 512) / RMS over [132608, +26460) < 1e-3; the maximum sample-to-sample step of the wet <= 2 x the maximum step of the ideal output over the same span (the drone's exact two-voice octave fading out over the release, then the Euclidean slice; R13; the former 0.05 x peak bar was below the ideal octave's 3·a·w) (sine window) |
| D12 | `H` `setEnvelopeShape`: the table of 2.8 (`generate(..., 0.5f, 0.5f)`, Exponential release rewritten to end at 0), the release phase `/(releaseSamples - 1)`, regenerate on change only | envelope | (1) "Linear envelope is linear": window = constant 0.5, attack 100 ms, Fixed: the wet at 50 ms into a hit = 0.25 +/- 0.01; Exponential: 0.44 +/- 0.01. (2) "every slice ends at 0": for both shapes, release 100 ms, the last output sample of a hit is <= 1e-7 in magnitude, and so is the last sample of a grain and of a burst |
| D13 | `H` setters: `setNoiseColor`, `setEuclidean*`, `setNoteValue`, `setEnvelopeShape` act only when the value changes; remove `setFreezeEnabled`, `freezeMixSmoother_`, tempo-valid state, `getPitchIntervalSemitones`; `noteValue_` default `Eighth` | cleanup | (1) "re-sending unchanged parameters every block changes nothing": render 4 s at the defaults once with every setter called once, and once with every setter called before every block with the same values: the outputs are identical within 1e-7. This holds the observable half of "act only on change"; the CPU half is not observable and is left to the review's profile. (2) Every existing case of `test_pattern_freeze_mode.cpp` that calls a removed API (3.4), by its line on 2026-10-29. Deleted, their subject no longer exists: "is always enabled" (:170), "processes without freeze" (:302), "outputs audio when freeze enabled" (:324), "pattern crossfade" (:652), "handles invalid tempo (edge case 5)" (:749) and "Non-tempo-synced patterns continue with invalid tempo (edge case 5b)" (:779); the held-content cases of D4 and the Insert cases of D10 replace their RMS > 0 checks. Rewritten, keeping their name and assertions: "reset clears state" (:78) replaces its `isFreezeEnabled` line (:95) with `hasWindow() == false` after `reset()`; "captures incoming audio" (:203) replaces `isCaptureReady(100.0f)` (:227) with `hasWindow()` after the first boundary of 2.2 (one complete cycle fed at 120 BPM, Capture 1/1); "Granular Scatter produces output" (:435), "Harmonic Drones produces output" (:510), "Noise Bursts requires captured audio content" (:589), edge case 1 (:733) and the Linear / Exponential envelope-shape SECTIONs (:851, :886) drop their `setFreezeEnabled(true)` and `isCaptureReady` lines and feed one complete cycle with the transport playing before they measure, so a window exists; the hidden probe (:924) drops its `setFreezeEnabled(true)` line (the probe of 6.1 replaces it) |
| D14 | `H` transport rules of 4.1 (`freeRunPos_`, `expectedPos_`, `wasPlaying_`, the host-tempo `spb` clamped to 20..1000 BPM, the re-anchor below 1/64 beat or 5 ms, the `ceil - 1` rebase of all three clocks on a jump, interval-change rebase) | transport | `[freeze-transport]`, Attack 0, a window ready that was built **with the transport playing** (so the case's own position changes are locates, not play edges, unless it says otherwise). (1) "Hits land at the step's sample, phased to projectTimeMusic": continuous 512-sample blocks from `projectTimeMusic = 7.5`, playing, valid: the first non-zero wet sample is 11025 +/- 1 samples after the first block's start. (2) "Transport stop keeps the pattern running": playing, then stopped blocks: the phase continues (the next hit arrives 750 ms after the previous one) and the window keeps playing. (3) "a locate re-bases without a false trigger": playing at beat 40, then a block at `projectTimeMusic = 100.3`, still playing: no hit at the jump block's sample 0 (the re-base sets the last fired index to ceil(200.6 - tol) - 1 = 200), and the next hit is step 3 at beat 101.5, 26460 samples after the jump block's first sample (steps 1 and 2 at 100.5 and 101.0 are not hits of 3-of-8). (4) "a locate to a bar line fires the downbeat": playing at 37.21, then a block at 0.0, still playing: step 0 sounds at offset 0 of the jump block, and the capture boundary fires there (the 1.21-beat cycle is under half a capture, so `getCaptureCount()` is unchanged). (5) "a play edge fires step 0 and sounds nothing": stopped (free-run at 37.21), then `projectTimeMusic = 0.0`, playing: `getLastFiredIndex()` of the step clock is 0 after the jump block (step 0 fired at offset 0), `hasWindow()` is false, and with zero input after the jump the wet of bar 1 is silent (< -90 dBFS) under Dry = Mix (no onset, so no span: R9). (6) "a loop wrap retriggers and captures the bar just played": a four-bar loop with bars at 220 / 330 / 440 / 550 Hz, continuous blocks ending exactly at 16.0, then a block at 0.0: step 0 sounds at offset 0, `getCaptureCount()` rises by one at the wrap, and the hits of the next pass have their Goertzel peak at 550. (7) "pre-roll fires on the grid": a locate to `projectTimeMusic = -2.0`: hits at beat -1.0 (step 6) and 0.0 (step 0), 22050 and 44100 +/- 1 samples after the start. (8) "a tempo change keeps the captures": 200 BPM for two beats of a bar, then 100 BPM (consistent positions): `getCaptureCount()` rises at each of the next three boundaries, and the hits follow the new step length. (9) "a ritardando keeps one capture per boundary" (finding 18): a linear ramp 120 -> 60 BPM over two bars, block 2048, positions integrated from the ramp: `getCaptureCount()` rises by exactly one at each boundary crossed, and no cycle is dirtied (`recFrom_` stays 0: test accessor `getRecFrom()`). (10) "a host tempo above 300 BPM captures": 320 BPM with consistent positions, 1/1: `getCaptureCount()` rises at every boundary over 8 bars, and the hits land at the 320 BPM step positions +/- 1 sample |
| D15 | probe rewrite of 6.1 | measurement | `PROBE Freeze signal path at defaults` prints the rows of 6.2 |
| D16 | `H` the three event arrays of 4.3 (`kEventCapacity` from `maxBlockSize`, `kMaxClockTempoBPM` and 1/64T, sized in `prepare()`), the merge with the capture / hit / burst order; `getHitCount()` (test accessor: Euclidean hits fired since `reset()`) | events (finding 19) | `[freeze-transport]` "every hit fires at 1/64T and 300 BPM in 8192-sample blocks": Steps 8, Hits 8 (every step a hit), Pattern Rate 1/64T, 300 BPM, 44.1 kHz, transport playing from beat 0, 20 blocks of 8192: `getHitCount()` equals the number of 1/24-beat boundaries in `[0, 20 x 8192 / 8820)` beats (446 +/- 0: 22.3 per block); the same at 1000 BPM (`kMaxClockTempoBPM`) is exact too |
| D17 | `H` `copyWindow` under the read rules of 2.4 (contiguous `spare_` / `rec_`, clamp, splice), `findSplice` and the seams of 2.10 | reads (findings 11, 15) | `[freeze-held]`. (1) "a slice past the window's end reads the following cycle": Variable, 16 x 1/16, input 233 Hz for one bar then 311 Hz, phase-continuous at the bar line: the step-15 slice (beat 3.75, 200 ms) of the third bar carries no step larger than 2 x the clean maximum (0.5 x 2 pi x 311 / 44100) and its last 75 ms read the tone of the bar after the window. (2) "without contiguous audio a read is clamped": the same with Hold pressed at 3.9 s so it engages at the 4 s capture and holds the complete bar [2, 4 s) (a Hold at the 2 s capture would hold the first, partial window, where Variable reads as Fixed by 2.4 and the clamp is unreachable); the held boundary at 6 s clears `recFollowsPlay_`: in the bar [6, 8 s) the step-15 slice starts at `length - n` (test accessor `getLastSliceStart()`), never past it, and carries no step larger than 2 x the clean maximum. (3) "a slice longer than the window splices once, quietly": Capture 1/4 (window 0.5 s at 120 BPM), Slice Length 2000 ms, a 233 Hz input: the maximum sample-to-sample step of the wet is <= 2 x the clean maximum (0.5 x 2 pi x 233 / 44100) |
| D18 | `PS` (`dsp/include/krate/dsp/processors/pitch_shift_processor.h`, PitchSync mode): the click when the detected grain shrinks after a tone change (the DSP implementer traced a 0.371 jump at 2.667 s of D8 (5) to the read tap being clamped when the grain shrinks; confirm the cause by reading the code and a trace before changing it). The fix keeps the read continuous across a grain change (for example: crossfade from the old tap to the clamped one over the new grain, or move the tap gradually), and changes nothing while the grain is steady | R13: the shared shifter's click | Failing first, in the shifter's own test file (`dsp/tests/unit/processors/`, the file that tests `PitchShiftProcessor`): "PitchSync stays click-free when the input's pitch drops": +12 and -12 semitones (the code review asked for the downward shift, where a tap above the shrunken grain grows until its splice takes it out), a 550 Hz sine for 1 s then a phase-continuous 233 Hz sine for 1 s (and the reverse: 233 then 550), plus 130 -> 440 Hz and 440 -> 130 Hz (the 550 / 233 pairs both sit at the 10 ms minimum grain, so the grain never changes and they pass on the unfixed shifter; 130 -> 440 read 0.2396 against 0.1253 before the fix), block 512: the maximum sample-to-sample step of the output is <= 2 x the maximum step of an ideal shifted sine at the output's level. Then D8 (5) and (8) pass with the Freeze code unchanged, and every existing Shimmer and shifter case (including the approval goldens) passes unchanged |

Mode-entry rule (unchanged): the processor's `resetMode` calls `reset()`. `reset()` clears the three cycle buffers'
roles (no window) and their peaks, the contiguity flags, `cycleOffset_`, `recFrom_`, `heard_`, `sinceAboveFloor_`,
`wasPlaying_` and the count, the clocks and their event arrays, the engaged state of the hold (not `hold_` itself,
which is a parameter, so a held Freeze waits again: 2.3), the pool, the drone source heads, FIFOs and voice fades,
the six shifters (`PitchShiftProcessor::reset`), both burst voices and `sampleCounter_`. Entering Freeze starts a new
span.

### 7.2 Plugin

| Row | File / function | Change | Case |
|---|---|---|---|
| P1 | `plugins/iterum/src/plugin_ids.h`: `kFreezeHoldId = 1070`, `kFreezeCaptureLengthId = 1071`, `kFreezeDryModeId = 1072` (comments: ranges) | IDs | `freeze_params_test.cpp` "Freeze capture parameters are registered as design 3.1 lists them": `registerFreezeParams` into a `ParameterContainer`; 1070 `stepCount 1`, default 0, `kCanAutomate`; 1071 `stepCount 3`, default 2/3, `kIsList`, string of index 2 is "1/1"; 1072 `stepCount 1`, default 1.0, `kIsList`, strings "Mix" / "Insert"; 1012 default 1.0 |
| P2 | `F` `FreezeParams`: `hold{0}`, `captureLength{2}`, `dryMode{1}`, `euclideanHitsNorm`, `euclideanRotationNorm`, `dryWet{1.0f}`; `handleFreezeParamChange` cases; `registerFreezeParams` entries; `formatFreezeParam` Hold "On"/"Off" | handling | "Freeze capture parameters denormalise" (0/0.49/0.5/1 for Hold and Dry; 0, 0.34, 0.67, 1 -> 0..3 for Capture); "Steps change recomputes Hits and Rotation from their normalized values": steps 8, hits 2/7 (3), rotation 3/7 (3) -> steps 16 -> hits 5, rotation 6, and `formatFreezeParam` prints the same |
| P3 | `F` the tail of 5.1 (four functions) | state | "Freeze capture tail: absent loads Hold off / 1/1 / Mix, present round-trips": modelled on `multitap_params_test.cpp:545-605` (`makeTail` writing the Freeze block, Tape sync, MultiTap length, then optionally the three int32); absent -> 0 / 2 / 0; every combination round-trips; the three fields are the last 12 bytes; out-of-range clamps; "Freeze capture controller sync": absent -> (1070, 0.0), (1071, 2/3), (1072, 0.0); present -> values / 3.0 and 0/1 (modelled on `:607-660`) |
| P4 | `P` `process()`: the context fields of 4.1; `case DelayMode::Freeze`: remove `setFreezeEnabled`, push `setHold`, `setCaptureLength`, `setDryMode`; `getState` / `setState` tail calls | processor | New file `plugins/iterum/tests/unit/processor/freeze_transport_test.cpp` (`ProcessorRig` of `multitap_tempo_output_test.cpp`, heap-allocated Processor, 48 kHz, block 64). (1) "Freeze passes the song position: a hit lands at the step's sample": Mode = Freeze (`kModeId` 9/9), Mix 1.0, Dry = Mix, attack 0, a 440 Hz input for 3 s with `projectTimeMusic = sample / 24000.0` and `state |= kProjectTimeMusicValid`. The first bar's second hit (beat 1.5 = sample 36000) starts within 64 samples of 36000, and the first hit of bar 2 at 96000 +/- 64. The hit's onset is the first output sample of a block whose RMS exceeds the previous block's by 20 dB, or where the output differs from `in` by the slice. (2) "Freeze state tail round-trips through getState / setState": set 1070 = 1, 1071 = 1/3, 1072 = 0 via `queue`, run one block, `getState`, `setState` on a fresh rig, `getState` again: identical bytes, and the tail's last 12 bytes read 1, 1, 0. (3) "a project restored with Hold On holds the first music": `setState` with Hold On, 2 s of silence, then 330 Hz while playing from beat 0: output is silent until the onset and then carries 330 Hz hits; after a 440 Hz input from beat 8, the hits stay 330 |
| P5 | `C` `setComponentState` and `loadComponentStateWithNotify`: the two tail calls of 5.1 | controller | `freeze_params_test.cpp` "Freeze capture tail reaches the controller" (an `EditControllerEx1` with `registerFreezeParams`, `syncFreezeCaptureParamsToController` on a stream with and without the tail: `getParamNormalized(1072)` 1.0 / 0.0) |
| P6 | XSLT `specs/iterum-freeze/xslt/freeze-controls.xsl`, applied to the uidesc; `freeze.xsl`, `panels/freeze.md`, `design-system.md` 7.10 / 7.11 mirrored | panel | `editor_panels_test.cpp:416` Freeze row: + `{kFreezeHoldId, W::T}, {kFreezeCaptureLengthId, W::M}, {kFreezeDryModeId, W::SEG}`, fieldsets 6; the existing "exactly one pattern variant fieldset is visible" case unchanged; `uidesc-inventory.js --check` clean for FreezePanel |
| P7 | `plugins/iterum/tests/unit/vst/freeze_screenshot_test.cpp`: unchanged states (the 4 shots show the new column-1 fieldset and the Hold toggle) | review | hidden `Iterum_FreezeScreenshots`: 4 PNGs; the visual review checks core 3 (Hold toggle at 200, 32 of the cell), CAPTURE rows at 24 / 76 / 128 |
| P8 | tests asserting the old defaults (`mode_defaults_test.cpp` if it lists Freeze Mix; the DSP "mix parameters" default) | defaults | updated to 1.0 |
| P9 | `plugins/iterum/CHANGELOG.md` open 1.0.0 entry: the bullets of 5.4 | close | n/a |
| P10 | `plugins/iterum/src/processor/processor.h/.cpp`: `getTailSamples()` override returning `kInfiniteTail` for every mode (R3); no `restartComponent` on a mode change | tail (2.9) | in `freeze_transport_test.cpp`: "Iterum reports an infinite tail in every mode": for each of the ten modes (`kModeId` k/9), `getTailSamples() == Steinberg::Vst::kInfiniteTail` |
| P11 | none (measurement); the budget of R2 | CPU (R2) | New `[performance]` case in `freeze_transport_test.cpp`, "Freeze Harmonic Drones at 4 voices x Octave stays inside the plugin budget": the `ProcessorRig` at 44.1 kHz, block 512, Mode = Freeze, Pattern Harmonic Drones, Voices 4, Interval Octave, Drift 30 %, a 233 Hz input held as the window, 30 s of audio after a 5 s warm-up: the processing time is < 5 % of the audio time (the full-plugin budget, `dsp-architecture` `SKILL.md:93`; no Freeze budget existed). It runs only through `node tools/run-cpu-tests.js plugin_tests`, alone, never in a gate. If it misses: the shared-detector remedy of 2.5.3, then a re-measure; never a wider budget |

### 7.3 Build / run set

Run `node tools/header-consumers.js --tag '[iterum]' dsp/include/krate/dsp/core/pattern_freeze_types.h
dsp/include/krate/dsp/effects/pattern_freeze_mode.h dsp/include/krate/dsp/processors/pattern_scheduler.h` over the
modified shared headers. Its last line names the executables, and the gate runs those executables with
`run-gate.js`. Read on 2026-10-29 for `pattern_freeze_types.h` alone, the line is: `dsp_core_tests`, `dsp_effects_tests`,
`dsp_primitives_tests`, `dsp_processors_tests`, `dsp_systems_tests`, `plugin_tests` / `approval_tests`, `membrum_tests`,
`ruinae_tests` and `vorago_tests`. The type header reaches `noise_oscillator.h`, `ring_modulator.h`,
`noise_organism.h` and Membrum's `noise_layer.h`, `noise_burst_exciter.h`, `noise_body.h`, `click_layer.h` and
`feedback_exciter.h`. The other files:
- `pattern_freeze_types_test.cpp` is edited (D2) and runs in `dsp_core_tests`.
- `pattern_freeze_mode.h` reaches `dsp_effects_tests` and `plugin_tests` / `approval_tests`.
- `pattern_scheduler.h` reaches `dsp_processors_tests`.
- `processor.cpp` / `processor.h` reach every Iterum processor test.
- `pitch_shift_processor.h` is modified by D18 (R13), so its consumers join the set: `node tools/header-consumers.js`
  over it names them (Shimmer's tests in `dsp_processors_tests`, `dsp_effects_tests` and `plugin_tests` /
  `approval_tests` at least); the gate runs every executable it names, and Shimmer's approval goldens must not move
  outside the click itself (a golden that moves is a regression unless the D18 case explains it).
- `rolling_capture_buffer.h` and `grain_envelope.h` are not modified (Freeze stops
  including the first and starts including `pitch_shift_processor.h`, R2; `biquad.h` is no longer needed now that the band-limited
  heads are gone), so their other consumers are not in the set. Including `pitch_shift_processor.h` adds it to the
  includes of `pattern_freeze_mode.h`'s consumers, which are already in the set.
- Every executable named above is built in one command (`--target` list) before the gate.
- P11 and any other `[performance]` case are excluded from the gate and run once, alone, through
  `tools/run-cpu-tests.js`.

Pluginval strictness 5 after the plugin rows. clang-tidy on the modified files (`dsp`, `iterum`).

---

## 8. Rulings

The user's rulings of 2026-10-29 (`open-items.md`; in each case the recommended option), and the challenge findings
the design stage had left open, each with what it changed. The former open questions are answered: 1 Hold semantics
by R1, 2 factory presets by R7, 3 the drones' pitch method by R2, 4 the tail by R3, and memory by R4.

### 8.1 Rulings R1-R7

| Ruling | What it changed |
|---|---|
| **R1** Hold keeps the window that is already playing (the former option A). Under Insert it mutes the input through the hold gate; it does not grab the last Capture length at the press | 0, 2.3 (first paragraph). The former open question 1 is closed; option B is not built |
| **R2** Harmonic Drones use the Shimmer pitch shifter, one `PitchShiftProcessor` (PitchSync) per upper voice, fed from the held window; its latency is accepted; its CPU is measured against a stated budget; the band-limiting filters go | 0, 1 (rows 3 and 13, "left alone", CPU budget), 2.1 (memory), 2.5.3 (rewritten: drone source, voices, chunks, cost), 2.6, 3.1 (constants), 3.4 (Interval, Drift, Attack rows), 5.2 (Harmonic Bed, Fifths Drone), 5.4, 6.2 (Drones rows re-derived), 7 (real-time rules), D8 (rewritten), P11 (new: the CPU case, budget < 5 % of one core), 7.3. A shifter is mono, so a voice takes a pair (left, right); the root voice plays the source directly, so Voices 4 uses six shifters |
| **R3** The tail is infinite for every mode, no `restartComponent` on a mode edge | 0, 1 ("left alone": the tail bullet removed), 2.9, 5.4 (a Changed bullet), P10 (every mode returns `kInfiniteTail`). Answers the former open question 4 and finding 22 |
| **R4** Memory accepted: three 5 s cycle buffers (5.3 MB at 44.1 kHz, 23 MB at 192 kHz); `kMaxCaptureLengthSeconds` stays 5 | 2.1 (memory paragraph, plus what the reconcile adds: two burst buffers, six shifters), 3.1 |
| **R5** Under Hold, each Euclidean slice sustains (looped, crossfaded at its loop point) until the next hit; a noise burst sustains likewise; Granular and Drones are already continuous | 0, 2.3 (sustain bullet), 2.5.1 (sustained slices), 2.5.4 (sustained bursts), 2.6, 2.7, 2.10 (the loop seam), 6.1 and 6.2 (Insert hold line: active >= 80 %), D5 (5) and (6), D9 (6). Resolves finding 6 |
| **R6** Audibility at the fresh defaults is measured on three inputs (pitch-per-bar tone, one-bar drum loop, sustained chord at 233 Hz) at Mix 100 + Insert, as 6.2 rows with bars; if the chord row misses with Fixed, Slice Mode's fresh default becomes Variable | 0 (last paragraph), 3.3 (Slice Mode row), 6.1 (the audibility rows), 6.2 (three rows, bars `diff >= -10 dB` and `moved >= 12 %`, with predictions). Resolves finding 13. Note in 6.2: the two sample-difference metrics count a phase-shifted copy as different, so the chord row is predicted to pass with Fixed; a `level` column is printed beside them, not gated |
| **R7** Factory presets stay as they are in this build (they load as Dry = Mix); a follow-up after the audition extends `tools/preset_generator.cpp` with the missing tails and sets Dry = Insert on the rhythmic / granular / noise presets | 5.2 (the follow-up paragraph). R7 says "eight" presets; the generator defines ten; R10 keeps Total Freeze on Mix, so the follow-up sets the seven named in 5.2 |
| **R8** (2026-10-29, on the item 8.4 left open) The drum-window drone bar is relative: the drone's 10 ms RMS p95/p5 depth on the D8 drum window is at least 6 dB below the window's own depth (predicted 16.0 against 22.3 dB) | 2.5.3, 6.1 (the `drum` line prints both depths), 6.2 (Drones drum row), D8 (3), 8.2 row 10 |
| **R9** (2026-10-29, on 8.3 "Play edge") After a play edge the mode is as after `reset()`: the play edge drops the window in force and clears `heard_`, the span plays the first music after play, bar 1's hits stutter that music; the bar-1 `out == in` case is withdrawn | 2.2 ("A play edge", the span), 4.1, D4 (10) (11), D14 (5), 8.2 row 16, 8.3 |
| **R10** (2026-10-29, on the implementability check) The preset follow-up sets Dry = Insert on seven presets; Total Freeze keeps Mix 100 % wet-only | 5.2, R7 row |
| **R11** (2026-10-29, on the DSP build's red D4 (1)) "Held, not live" is carried by the held-note Goertzel check; `r` is printed in the probe and the cases, never asserted (a held pure tone lines up in phase with the bar before: ideal output reads about 0.288) | D4 (1), 6.2 (the `r` column) |
| **R12** (2026-10-29, on D8 (1), (7)) Drone pitch is measured by a peak search within +/- 10 cents (0.5-cent steps) and must lie within 3 cents of its interval, its level within the row's dB bar; PitchSync measured +1.2 cents at +12 and +1.9 at +24, inaudible | D8 (1), (7), 6.2 (Drones row) |
| **R13** (2026-10-29, on D8 (5), (8), D11) Step bars are relative: at most 2 x the largest step of the ideal output (the former 0.1 and 0.05 x peak bars failed for a perfect octave drone); the real click inside the shared `PitchShiftProcessor` (PitchSync, a shrinking grain after a tone change) is fixed at its source with a failing test first, and Shimmer's suites join the gate | 1 (the shifter note), 7.1 D8 (5) (8), D11, D18 (new), 7.3 |
| **R14** (2026-10-29, on D9 (1)) The code meets the designed level and the bar stays: the noise level rises from -12 dB to -10.5 dB, and if D9 (1) still misses its bar (measured at -10.5 dB: -30.03 dBFS against -30, active 75.9 % against 76-84 %), `kNoiseLevelDb` rises in further 0.5 dB steps to the smallest value that meets both; D9 (3) follows the constant | 2.5.4, 3.1 (`kNoiseLevelDb`), 6.2 (Noise silence row), D9 |
| **Close-out** (2026-10-29, the code review's minors) `processFromPosition` starts at the first boundary of its own block, so a jump without a rebase can never walk thousands of boundaries in one callback (case "processFromPosition fires only its own block's boundaries after a jump without rebase"); a burst deferred by the forced fade does not start after the type left Noise Bursts or in a release gap; the unused right-channel noise filter is gone | 4.2, 2.5.4 |

### 8.2 The challenge findings left open by the design stage

| Finding | Ruling | What it changed |
|---|---|---|
| 6 Hold leaves three blips per bar | R5 | as R5; the 6.2 Insert hold line gains the active-fraction bar (>= 80 %, prediction about 99 %); D5 (6) |
| 7 the release stutters the drop for 1-2 bars | accepted | 2.2 (a Hold release is no longer a dirtying event), 2.3 (`rec_` records while held; the next boundary captures; the Insert release gap suppresses every trigger and holds the duck at 0 until that capture), 2.5.1, 4.3 (step 1), D5 (1) and (2) (the release case: no held-tone hit after the release, `out == in` from 5.14 s), D10 (3). The release gap applies under Insert only: under Dry = Mix it would leave a silent wet, so the held window keeps playing until the capture |
| 8 silent windows duck the live dry | accepted | 2.1 (the per-window peak latch), 2.2 (a silent window is no window for triggers and for the duck), 2.5.4 (a noise-only burst does not gate), 2.7, D1 (2), D4 (9), D10 (6) (the finding's case) and (7). Hold engages only on a non-silent window (2.3), so it never mutes the input into silence: D5 (7) |
| 9 two heads of one tone cancel | superseded by R2; its test kept | 2.5.3 (the source's aligned starts; the simulation in `research/drone-source-sim.js`: 21-36 dB unaligned, 0.0-0.6 dB aligned), 6.1 (probe tones 233 / 311 / 415 Hz, off the commensurate ones), 6.2 (Drones sweep row), D8 (2) (the sweep, worst-case 10 ms depth <= 3 dB) |
| 10 drones pulse on rhythmic input | resolved by R2 for the head scheme; the loop is the whole window, pass starts jittered; bar: p95/p5 <= 3 dB on the drum window | 2.5.3 (four Hann heads over the whole window, uniform starts, aligned), 3.4 (Slice Mode inert in Drones), 6.2 (Drones drum row), D8 (3). The absolute 3 dB bar was replaced by the relative bar of R8 (8.1) on 2026-10-29 |
| 11 Granular wraps click; the scatter shrank | accepted | 2.4 (read rules: no wrap inside the window, clamp inside `[lo, hi - n]`, crossfaded splice only when nothing fits), 2.5.2 (region from Slice Length at 0 % to the whole window at 100 %; Fixed centred on the downbeat where `spare_` allows; no start in the last grain length), 2.10, 3.4 (Position Jitter row), 5.2 (Granular Cloud, Scatter Drift), D7 (2)-(4), D17 |
| 12 Noise Bursts hiss forever | accepted | 0 (the stated exception), 2.2 (`sinceAboveFloor_`), 2.5.4 (noise-only bursts: after `heard_`, while playing or within two capture cycles of the last above-floor input), 2.9 (the exception), 6.1 and 6.2 (silence rows, playing and stopped), D9 (1) (the finding's case: stopped + silent -> wet < -90 dBFS after the limit; from reset nothing at all) |
| 13 no row measures the fresh default | R6 | as R6 |
| 14 equal-power heads swell or null on tones | superseded by R2; the drone level bars kept and re-derived | 2.5.3 ("Level"), 6.2 (Drones row: 0 +/- 2 dB, peak <= +2 dB; the sweep row) |
| 15 every wrap is a hard splice | accepted | 2.1 (`playFollowsSpare_`, `recFollowsPlay_`), 2.4 (contiguous continuation from `rec_` past the end and from `spare_` before the start; otherwise a one-sided clamp; a crossfaded splice only when a read is longer than all readable audio), 2.5, 2.6, 2.10, D7 (4) and D17 (the splice-click cases: maximum sample step against the clean maximum) |
| 16 bar 1 after play is punched by silent stutters | accepted | 2.1 (peak latch), 2.2 (a silent window is no window; a play edge clears `heard_` and drops the window when one exists and Hold is not engaged), 4.1 (`wasPlaying_`), D4 (10) (the span plays the first music after play, R9) and (11), D14 (5). The bar-1 `out == in` half of the ruling was withdrawn by R9 (8.1) on 2026-10-29 |
| 17 smoother semantics ambiguous | accepted | 2.7 (all three smoothers are `OnePoleSmoother` with its 99 %-time `configure`; `coeff`, `tau`, the -60 dB and snap times tabled; gap = 10 tau after the target leaves 0, plateau = 7 tau after it reaches 1), 3.1 (constant comments), 6.2 (Insert lines), D10 (bound `1 - exp(-2500 / sr) + 1e-4`). The gap margin is 10 tau, not the 7 tau of the ruling: 7 tau leaves `e^-7 = 9e-4` of the duck, far above the 1e-6 the gap cases assert; `OnePoleSmoother` snaps to its target below 1e-4 (`SM:199-202`), after 9.2 tau, so 10 tau makes the gap exact. The plateau, which asserts -60 dB, keeps 7 tau |
| 18 tempo ramps and tempos above 300 BPM dirty every block | accepted | 4.1 (`spb` from the host tempo, finite, clamped to 20..1000 BPM; a delta under 1/64 beat or 5 ms re-anchors without dirtying; only real locates, loop wraps and play edges dirty), D14 (9) (the ramp) and (10) (320 BPM) |
| 19 the event queue has no capacity | accepted (fixed arrays) | 4.3 (three fixed arrays, `ceil(maxBlockSize / minStepSamples) + 2`, merged, overflow impossible with the proof), 7 (real-time rules), D16 (the finding's case at 1/64T, 300 BPM, block 8192: every hit counted). The arrays are sized at `kMaxClockTempoBPM` (1000 BPM), not 300: finding 18's ruling lets `spb` follow the host tempo above 300, and arrays sized at 300 BPM could then overflow |
| 20 the burst source is unbuildable | accepted, option (a) | 2.5.4 (two burst voices with preallocated `kMaxSliceLengthMs` x 2 ch buffers, copied at the trigger; the older voice fades over 5 ms when a new one starts; sustain under Hold), 2.6, 2.1 (memory: 1.4 MB at 44.1 kHz, 6.1 MB at 192 kHz; the finding's "1.5 MB at 192 kHz" for one buffer is 3.1 MB), 3.1 (`kBurstVoices`), D9 (4) (Radio Static: max step < 0.05 x peak), (5), (6) |
| 21 D6 asserts Variable on a partial window | accepted | D6 (1): the four-tone bar repeats; bar 2 reads the onset under Variable too (the partial-window rule); bar 3 carries the Variable assertions |
| 22 a mode-dependent tail never reaches the host | R3 | as R3 |

### 8.3 Consequences decided in this reconcile

The rulings left these choices to the design. Each is decided here, with its reason, and can be changed by the user
without touching the rest.

- **Drone source (R2 with finding 10).** The ruling reads "the loop the shifter reads defaults to the whole window
  and each pass start is jittered". Read literally as one reader traversing the whole window, a drum bar would come
  back as itself, a drum loop. The source is therefore four Hann heads with uniform starts over the whole window,
  each aligned on its first 5 ms (2.5.3), which is the "more than two heads" and the WSOLA step of findings 9 and 10.
  Slice Length sets the pass length (100 ms at least), so the hero knob stays alive in Drones.
- **The shifter's limits (R2).** `PitchShiftProcessor` passes its input through when the ratio is within 1e-4 of 1
  and stops writing its own buffer while it does (`PS:674-679`), and it shifts at most +/- 24 semitones (`PS:681,
  1315`). Hence: the root voice is not shifted and does not drift; Unison voices stay 1-50 cents from the root;
  the +24 voice drifts flat only; Octave x 4 voices folds its fourth voice to -12. Harmonic Bed and Fifths Drone are
  not affected by the fold.
- **Shifter latency (R2).** R2 accepts "5-10 ms"; the PitchSync shifter's grain, which is its latency, is clamped
  to 10-30 ms in the code (`PS:617-620, 718-720`). The design takes the code's figure as what R2 accepts; the upper
  voices lag the root by it.
- **Block-size invariance with a shifter (R2).** The shifter smooths its pitch on 64-sample sub-blocks counted from
  each call (`PS:1252-1302`), so the drone runs it in 64-sample chunks on the mode's own sample grid (2.5.3).
- **Play edge (finding 16, R9).** The ruling asked both to clear `heard_` on a play edge "so the span plays the
  first music after play" and for a case where bar 1 after play equals the input within 1e-6; a span playing in
  bar 1 stutters it at steps 3 and 6, so the two could not both hold. The user ruled R9 on 2026-10-29: a play edge
  that finds a window drops it and clears `heard_` (a stale window must not stutter over a new intro), and the mode
  is then as after `reset()`: the span plays the first music after play, so bar 1 stutters the new music, never the
  old window and never silence. The bar-1 `out == in` case is replaced (D4 (10), (11)).
- **The release gap applies under Insert only (finding 7).** Under Dry = Mix the input is not in the output, so a gap
  would be a silent wet until the next capture.
- **Hold engages only on a non-silent window** (findings 8 and 16), so Hold never mutes the input into silence.
- **Noise-only bursts do not gate the duck** (finding 8's "never duck for silence", applied to the burst's silent
  slice part).
- **Event arrays at 1000 BPM** (findings 18 and 19), as 8.2 says.
- **Gap margin 10 tau** (finding 17), as 8.2 says.
- **Position Jitter region uses the window's own length** (finding 11): `R` grows from Slice Length to the window
  length `length - from`, while placement may use contiguous `spare_` / `rec_` audio.

### 8.4 Open

Nothing. The drum-window bar was ruled R8, the play edge R9 and the preset set R10 on 2026-10-29 (8.1).

Everything in this document is decided. Where the challengers disagree, the reasons are in the row they
challenge and in section 9.

---

## 9. Challenges considered

The 20 challenges of 2026-10-29, with the sections that now answer them. Where a ruling of section 8 replaced the answer, the row says so.

| # | Subject | Resolved in |
|---|---|---|
| 1, 9 | a rebase swallowed the boundary at the jump (play from a bar, loop wrap, locate); D3 unbuildable | 4.1 (`ceil - 1`, half-sample tolerance, the loop-wrap capture), 4.2, D3 (4096-sample block, cases d-f), D14 (4)-(7) |
| 2 | Hold behaviour while Hold is off; ring-capacity-dependent cut-off; no tail | 2.2 (silence is captured; only `heard_` protects), 2.9 and P10 (tail), D4 (4), D5 (2) |
| 3 | Hold inaudible at the fresh defaults | 2.3 (the input is muted under Insert), 2.7 (`gh`), D10 (3), 6.2 Insert hold line |
| 4, 12 | the span anchored at `entryIndex` read silence or overwritten slots | 2.1 (cycle buffers), 2.2 (span from unbroken recording, Fixed at the onset), 2.3 (a waiting Hold engages at the first capture), D4 (5), P4 (3) |
| 5, 11 | capture by equality with a recomputed `L` skipped every other bar | 2.2 (a complete cycle is what elapsed between two fired boundaries), D4 (2) |
| 6 | the capped window did not start on its downbeat | 2.1 (recording restarts at offset 0 per cycle), 2.4, D4 (3), 6.1 capped row |
| 7 | Drones on rhythmic material pulsed and aliased | superseded by R2 (8.1): 2.5.3 (aligned source heads over the whole window; no varispeed, so no fold-over to filter), D8 (3) (the relative bar of R8), 5.2 |
| 8 | Insert gated Granular per grain | 2.7 (per-type gate, Granular 0), D10 (4), 6.2 |
| 10 | per-block drone ratios broke block-size invariance; D11 switched off-grid | 2.5.3 (64-sample chunk grid of `sampleCounter_`, since R2), D8 (1), D11 (sample 132608 = 2072 x 64) |
| 13 | short spans skipped every Fixed hit; a restored Hold with long slices stayed silent | 2.2 (`needSpan`), 2.3, D4 (6), D5 (4) |
| 14 | the "too old" rule fired at every boundary for long `L` | 2.1 (no ring, no staleness, no forced capture), D4 (4) at 2/1, D5 (2) |
| 15 | voices 1-3 jumped at voice 0's re-anchor | 2.5.3 (each source head latches at its own zero; the voices all shift one source), 2.6, D8 (5) |
| 16 | Attack 0 removed the seam crossfade | 2.5.3 (the source's Hann overlaps, independent of Attack), D8 (4) |
| 17 | trapezoid table; Exponential ended at 0.018 | 2.8, D12 |
| 18 | Slice Length dead in Granular | 2.5.2, 3.4, D7 (3) |
| 19 | the type header's consumers were missing from the run set | 7.3, D2 |
| 20 | rules without a holding case | D1, D4 (2)-(11), D5, D7 (2)-(4), D8 (2)-(8), D9, D10 (3)-(7), D13 (1), D14 (3)-(10), D16, D17, and the helper of 7 for D10 |

Points taken differently from the challenge, with the reason:

- **2, keep the silence protection after play start or a Hold release.** Not taken. Once there has been music, the
  rule is "Freeze follows its input, Hold keeps it". A protected first capture after play or release would bring
  back the Hold-while-Hold-is-off behaviour that the challenge objects to. (Since the reconcile a silent window
  triggers and ducks nothing, and a play edge drops the window: findings 8 and 16, 8.2.)
- **4, load a saved Hold as Off.** Not taken. A saved Hold now waits for the first capture of real music (2.3), so
  restoring it is safe, and the user's setting is kept.
- **7, take the drone from the window's "loudness-stable middle".** Not taken. Finding that point needs an analysis
  pass over up to 5 s at capture time. Since R2 the source reads the whole window at uniform, aligned starts, so no
  single anchor exists to choose (2.5.3).
- **14, check window validity every block.** Not needed. With cycle buffers a window is never overwritten while it
  is `play_` or `spare_` (2.6), so there is nothing to check.
- **19, run the Seraphis suite.** Not taken. `header-consumers.js` does not name `seraphis_tests` for
  `pattern_freeze_types.h` (run 2026-10-29). It names `dsp_*`, `plugin_tests` / `approval_tests`, `membrum_tests`,
  `ruinae_tests` and `vorago_tests`, and the gate runs what the tool names.
- **20, a case for the forced "too old" capture (2.2c).** Not added: the rule no longer exists (2.1).
- **20, jitter 50 % within +/- window/4.** Replaced by the region of 2.5.2, which since finding 11 grows from Slice
  Length (0 %) to the whole window (100 %). D7 (3) checks its spread.
