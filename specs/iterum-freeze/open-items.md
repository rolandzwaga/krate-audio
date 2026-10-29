# Open items: Iterum Freeze design

User rulings of 2026-10-29 (given as multiple choice, the recommended option taken in every case), followed by the
seventeen challenge findings the design stage left unresolved after two revisions. The reconcile stage resolves every
item in design.md and appends "Resolved: <section(s)>" (or "Open: <reason>") under each one here.

## Rulings on design.md section 8 and on the open findings

- **R1 Hold keeps the window that is already playing** (design's A). Hold stops re-capture; under Insert it mutes the
  input through the hold gate. It does not grab the last Capture length at the press.
  Resolved: design.md 0, 2.3 (first paragraph), 8.1 (R1).
- **R2 Harmonic Drones use the Shimmer pitch shifter**: one `PitchShiftProcessor` (dsp/include/krate/dsp/processors/
  pitch_shift_processor.h, with today's correlation-searched splice) per voice, fed from the held window loop, instead
  of the two crossfading varispeed heads. Its 5-10 ms latency is accepted for a drone; its CPU is measured in the build
  (a processor case with the 4-voice Octave setting against the mode's existing CPU budget, or a stated new one). The
  band-limiting filters of 2.5.3 go away with the heads. Findings 9, 10 and 14 below are resolved by this ruling, except
  for the test material they ask for (keep it).
  Resolved: design.md 0, 1, 2.1, 2.5.3 (rewritten), 2.6, 3.1, 3.4, 5.2, 5.4, 6.2, 7 (real-time rules), D8, P11 (CPU < 5 % of one core), 7.3, 8.1 (R2), 8.3 (the shifter's pass-through band and +/- 24 range).
- **R3 The tail is infinite for every mode**: `getTailSamples()` returns `kInfiniteTail` whatever the mode, no
  restartComponent on a mode edge. This answers open question 4 and finding 22.
  Resolved: design.md 0, 1, 2.9, 5.4, P10, 8.1 (R3).
- **R4 Memory accepted**: three 5 s cycle buffers (5.3 MB at 44.1 kHz, 23 MB at 192 kHz). kMaxCaptureLengthSeconds
  stays 5.
  Resolved: design.md 2.1 (memory, plus the burst buffers and shifters the reconcile adds), 3.1, 8.1 (R4).
- **R5 Hold sound: slices sustain until the next hit** (Beat Repeat's Gate). While Hold is engaged, each Euclidean hit's
  slice keeps looping (crossfaded at its loop point) until the next hit replaces it, so a held bar is continuous sound
  with the rhythm on top. Define the same rule for Noise Bursts (the burst sustains) and state that Granular and Drones
  are already continuous. Resolves finding 6; add its 6.2 row (active fraction with Hold on at the fresh defaults, bar
  at least 80 %).
  Resolved: design.md 0, 2.3, 2.5.1, 2.5.4, 2.6, 2.7, 2.10 (loop seam), 6.1, 6.2 (Insert hold line, active >= 80 %), D5 (5)-(6), D9 (6), 8.1 (R5).
- **R6 Audibility at the fresh defaults is measured on three inputs** (the probe's pitch-per-bar tone, a one-bar drum
  loop, a sustained chord at a non-commensurate pitch such as 233 Hz), at Mix 100 + Insert, as 6.2 rows: energy of
  (out - in) relative to the input and the fraction of 10 ms windows where |out - in| > -20 dB re in, each with a bar.
  If the sustained-chord row cannot meet its bar with Slice Mode Fixed, the fresh default of Slice Mode (1017) becomes
  Variable (its ID, type and range unchanged) and section 0 says so. Resolves finding 13.
  Resolved: design.md 0, 3.3, 6.1 (audibility rows), 6.2 (three rows with bars and predictions; the phase-sensitivity note and the ungated level column), 8.1 (R6).
- **R7 Factory presets stay as they are in this build** (they load as Dry = Mix). A follow-up after the audition extends
  tools/preset_generator.cpp with the missing state tails and sets Dry = Insert on the eight rhythmic / granular / noise
  presets; record that follow-up in section 5.2. Resolves open question 2.
  Resolved: design.md 5.2 (the follow-up paragraph; seven presets, not eight: the generator defines ten), 8.1 (R7).
- **R8 (2026-10-29) The drum-window drone bar is relative**: at least 6 dB below the drum window's own p95/p5 depth.
- **R9 (2026-10-29) After a play edge the span plays the first music after play**, as after reset(); the bar-1
  `out == in` case is withdrawn.
- **R10 (2026-10-29) The preset follow-up sets Insert on seven presets**; Total Freeze keeps Mix.

## Unresolved challenge findings

Ruling for every item below: **accept the challenger's suggested fix as written**, unless the ruling line under the
item says otherwise. Each needs its sections, implementation rows and Catch2 cases in design.md updated, and the
expected numbers of section 6 re-derived where the mechanism changed.

### Item 6

[challenge, unresolved after 2 revisions] design.md 2.3 (Hold x Dry table, Insert / Hold on), 0 ('pressing Hold freezes the sound'), 6.2 'Euclidean Insert hold line': At the fresh defaults, pressing Hold under Insert forces g = 1, so out = wet. The wet is three 200 ms slices per 2 s bar, about 30 % duty by 2.7's own numbers. Pressing Hold therefore removes the music and leaves three blips with about 1.4 s of silence per bar. A producer pressing a button on a mode called Freeze expects the sound to keep going, as Clouds/Beads and BigSky/Blackhole Freeze do (prior-art HIGH 1/2). The 6.2 row checks only that the input share is below -60 dB, so a mostly silent hold passes. (suggested: Decide what a held sound is at the defaults and measure it. Options: (a) under Hold, Euclidean slices sustain or repeat until the next hit, gate-to-next-step like Beat Repeat's Gate; (b) Hold under Insert ducks the dry only by the voice gate gv, not to 1, and the press itself retriggers the pattern; (c) change the fresh defaults so a held pattern is continuous. Add a 6.2 row for the output's active fraction with Hold on at the fresh defaults, with a bar (for example at least 80 %).)

Ruling: resolved by R5 (slices sustain until the next hit). Add the 6.2 active-fraction row with Hold on.

Resolved: design.md 2.3, 2.5.1, 2.5.4, 2.10, 6.2 (Insert hold line, active >= 80 %), D5 (6), 8.2.

### Item 7

[challenge, unresolved after 2 revisions] design.md 2.3 'While Hold is engaged: Nothing is stored into rec_' and 'On release'; 2.2 dirtying events: Recording stops during Hold and the release dirties the cycle in progress. So after a release the next boundary does not capture, and the held window keeps stuttering for the rest of that cycle plus one more full cycle. That is up to 2 bars at 1/1 and up to 8 s at 2/1 / 120 BPM, punched into the live music under Insert. The momentary use the design promotes (hold over a fill, release on the drop) ends with the drop stuttered by the fill for 1-2 bars. Beat Repeat stops repeating at release. Stopping recording buys nothing: rec_ is neither play_ nor spare_, so writing it during Hold cannot disturb what plays. (suggested: Keep writing rec_ while held and block only the capture, so a release leaves the cycle complete and the next boundary replaces the window (at most 1 cycle stale). Better still, suppress hits, bursts and the duck between the release and the next capture, so the live input returns at once. Add a D5 case: release at 5 s, and no held-tone hit sounds after 6 s (or after the release).)

Ruling: accept. Keep writing rec_ while held and block only the capture; suppress hits, bursts and the duck between the release and the next capture so the live input returns at once; add the D5 release case.

Resolved: design.md 2.2 (a release no longer dirties), 2.3 (recording during Hold; the Insert release gap; Mix keeps the held window until the capture), 2.5.1, 4.3, D5 (1)-(2), D10 (3), 8.2, 8.3.

### Item 8

[challenge, unresolved after 2 revisions] design.md 2.2 'Silence is captured', 2.7 duck gate (Euclidean: max envelope over active slices), 2.5.1 'if no window or span is valid, skip', 4.1 play edge: A silent window is valid, so hits still allocate slices, and their envelopes drive gv, which ducks the live dry. Under the fresh Insert default, every bar after a silent bar loses its input at each hit, the step-0 downbeat included. Examples: the first bar of a drop after a one-bar break; the first bar after pressing play when the transport sat stopped on silent input for more than 2 cycles (free-run captured silence). The producer hears dropouts, and the drop's own downbeat is muted. D4 (4) and P4 (3) measure this only at Dry = Mix, so no case can catch it. (suggested: Do not duck for silence. Either scale a voice's gate contribution by its source level (skip the hit, or treat its gate as 0, when the copied region is below kCaptureSilenceFloor), or keep heard_-style protection per window (a window whose peak is below the floor counts as 'no window' for triggering). Add a D10 case: Insert 100, music for 2 s, silence for 2 s, music from 4 s; the output equals the input within -60 dB over [4, 6 s) except where non-silent material plays.)

Ruling: accept. Never duck for silence: latch a per-window peak at capture; a window below kCaptureSilenceFloor counts as "no window" for triggering and for the duck gate. Add the D10 case.

Resolved: design.md 2.1 (peak latch), 2.2, 2.4 (per-cycle onset for Fixed), 2.5.4, 2.7 (no duck for a silent window or a silent copy), D1 (2), D4 (9), D10 (6)-(7), 8.2.

### Item 9

[challenge, unresolved after 2 revisions] design.md 2.5.3 (two heads offset D/2, sin/cos windows 'equal power'), D8 (1)/(2), 6.1 probe input 220/440/880 Hz: The two heads of a voice read the same tone D/2 apart. On tonal material they sum coherently with phase difference phi = omega * D/2, so the amplitude squared is 1 + |sin 2 pi x| * cos phi. At phi = pi the drone cancels completely twice per pass (every 125 ms at D = 500 ms). At most other pitches it is a deep tremolo. The equal-power argument holds only for uncorrelated heads. The test material hides this: at 120 BPM and 44.1 kHz, D/2 = 11025 samples, exactly 55.000 periods of 220 Hz (and an integer number of 440 and 880 Hz periods). D8 (2) 'Drones sustain', D8 (1) and every probe row therefore measure the one in-phase case, and none of them can fail. A pad held at, say, 233 Hz would pump. (suggested: Use a head scheme that does not cancel: align each new pass start to the outgoing head (WSOLA / correlation search over plus or minus one period), or use three or more heads with jittered pass starts so cancellation is not periodic. Re-specify D8 (2) as a sweep, for example 200-260 Hz in 1 Hz steps plus 233 Hz and 311 Hz, requiring worst-case 10 ms depth of at most 3 dB. Move the probe tones off commensurate frequencies.)

Ruling: superseded by R2 (PitchShiftProcessor per voice). Keep the test it asks for as the drone level case: a 200-260 Hz sweep in 1 Hz steps plus 233 Hz and 311 Hz, worst-case 10 ms depth of at most 3 dB; move the probe tones off commensurate frequencies.

Resolved: design.md 2.5.3 (aligned source heads; research/drone-source-sim.js), 6.1 (probe tones 233 / 311 / 415 Hz), 6.2 (Drones sweep row), D8 (2), 8.2.

### Item 10

[challenge, unresolved after 2 revisions] design.md 2.5.3 (Fixed: Ph = min(P, length - Dh) = 0 every pass; head 1 at D/2), D8 (3) prediction '~7 dB': At the defaults on rhythmic input, Harmonic Drones re-read the downbeat from the same anchor at every pass of both heads. With D = 1 beat this brings the kick back every 250 ms at unison, and every 125 ms in the octave voice (which passes twice as fast). The design predicts about 7 dB of 10 ms modulation and D8 (3) accepts it as long as a hard loop is worse. That is an 8th/16th-note pulse, not a drone: the type does not sound like its name on the most common input (a drum or full mix). The 'sine window suppresses the transient' argument covers only the attack sample; at 80 ms into a 500 ms pass the gain is already 0.48. (suggested: Decorrelate the passes. Give each pass start a random or jittered offset across the window (Fixed = around the downbeat within the window, Variable = around the bar position). Use more than two heads, and/or a loop length longer than one beat (for example the whole window). Set the D8 (3) bar as an absolute ceiling (for example p95/p5 depth of at most 3 dB on the drum window), not 'reference minus 6 dB'.)

Ruling: resolved by R2 for the head scheme; the loop the shifter reads defaults to the whole window (not one beat) and each pass start is jittered as suggested, so rhythmic input does not pulse. Set the D8 bar as an absolute ceiling (p95/p5 depth of at most 3 dB on the drum window).

Resolved: design.md 2.5.3 (four aligned Hann heads over the whole window, uniform pass starts), 3.4, 6.2, D8 (3), 8.2.
Formerly open (ruled R8 below): the ruled bar (p95/p5 <= 3 dB on the drum window) could not be met by the R2 mechanism: research/drone-source-sim.js measures 16.0 dB for the chosen source (13.8-20.7 dB for 4 to 128 heads, 22.3 dB for the drum window itself), and the design may not widen a bar. The user picks a relative bar, a bar at the measured level, a spectral-freeze source, or dropping the row (design.md 8.4). Until then D8 (3) keeps the ruled bar and is expected red.

Ruled R8 (2026-10-29): the bar is relative, the drone's depth at least 6 dB below the drum window's own (predicted 16.0 against 22.3 dB). Resolved: 2.5.3, 6.1, 6.2 (Drones drum row), D8 (3), 8.1 R8, 8.2 row 10.

### Item 11

[challenge, unresolved after 2 revisions] design.md 2.5.2 (start P + (2u-1) * jitter * R / 2 'wrapped inside the window', R = min(Slice Length, ...)), 2.4 'a wrap is a splice inside one copy', 3.4 Position Jitter row: (1) In Fixed at the defaults P = 0, so about half of all grains get a negative start and wrap to the last 50 ms of the window. They then cross the end-to-start splice partway through a 100 ms grain, under a non-zero envelope. That is a click on roughly every other grain unless the material happens to be loop-continuous, and it always clicks on a partial window. The same unfaded splice hits Euclidean / Noise slices whenever P + Slice Length passes the window end (Variable with 1/4 Capture, or slices longer than the window). (2) The scatter shrinks from today's ~2.5 s (the critique's 'one pattern that does roughly what its name says', 98 % active) to plus or minus 50 ms around the downbeat transient, 63 % active. On drums that is a cloud of kick attacks. This type gets worse than today. (suggested: Clamp the read region inside [from, length) as the drones do: shift the scatter region so that region start plus grain length is at most length, and clamp slice P so P + n is at most length. Alternatively crossfade any wrap over a few ms. Widen the default scatter, for example let Position Jitter 100 % reach the whole window with Slice Length as a minimum, or centre Fixed on a point after the downbeat transient. Add cases: no sample step above X at the wrap, and grain starts never inside the last grain length of the window.)

Ruling: accept. Clamp every read region inside [from, length - grainLen]; crossfade any wrap over a few ms; Position Jitter 100 % reaches the whole window with Slice Length as the minimum region; Fixed centres its region on the downbeat where the window allows.

Resolved: design.md 2.4 (read rules), 2.5.2 (region from Slice Length to the whole window; Fixed centred on the downbeat where spare_ allows; no start in the last grain length), 2.10, 3.4, 5.2, D7 (2)-(4), D17, 8.2.

### Item 12

[challenge, unresolved after 2 revisions] design.md 2.5.4 'a burst fires on silence too' vs 0 ('Freeze follows its input... falls silent with its input'), 2.9 ('The output ends at most two capture cycles plus one slice ... after the input'), P10 kInfiniteTail: These rules contradict each other. Noise Bursts plays -18 dBFS noise at every 1/8 forever: before any music, on a track with no audio, and with the transport stopped (the clocks free-run). Because Freeze reports kInfiniteTail, the host never suspends it. A producer who stops the DAW hears Iterum keep hissing in eighths. 2.9's tail statement is false for this type. (suggested: Limit noise-on-silence. For example, fire noise-only bursts only while the transport is playing, or for N cycles after the last above-floor input, or only once heard_ is set. Alternatively state the exception in 0 / 2.9 and justify it. Add a D9 case for the chosen rule (for example stopped transport + silent input for 10 s: wet below -90 dBFS after the limit).)

Ruling: accept. Noise-only bursts fire only once heard_ is set and only while the transport is playing or within two capture cycles of the last above-floor input; state the exception in sections 0 and 2.9; add the D9 case (stopped transport + silent input for 10 s: wet below -90 dBFS after the limit).

Resolved: design.md 0, 2.2, 2.5.4 (noise-only bursts), 2.7, 2.9, 6.1, 6.2 (silence rows), D9 (1), 8.2.

### Item 13

[challenge, unresolved after 2 revisions] design.md 6.1 'Measurement: at Mix 100 %, Dry = Mix', 6.2 table, 0 ('three times per bar the held downbeat punches in'): The user's complaint is about what a fresh instance outputs, but no row measures the fresh default (Mix 100 + Insert) output against its input. The probe measures the wet alone at Dry = Mix, and its input changes pitch every bar, which is the most favourable case. Under Insert the step-0 hit replaces a bar's downbeat with the previous bar's downbeat. On a one-bar loop, or any stationary pad or drone, that is like for like: inaudible apart from crossfade phase dips. So on much real material only two punches per bar differ, and on sustained material almost nothing does. The user could once again 'only hear the input'. (suggested: Add 6.2 rows at the fresh defaults (Mix 100, Insert). Measure the energy of (out - in) relative to the input, and the fraction of 10 ms windows where |out - in| > -20 dB re in. Use three inputs: the probe's pitch-per-bar input, a one-bar drum loop, and a sustained chord at a non-commensurate pitch. Set a bar each must meet. If the sustained case fails, say so in 0 and consider a default that is audible on it (for example Variable).)

Ruling: resolved by R6.

Resolved: design.md 6.1, 6.2 (the R6 audibility rows), 3.3, 8.1 (R6), 8.2.

### Item 14

[challenge, unresolved after 2 revisions] design.md 2.5.3 (two heads, head 1 starts at D/2, w = sin(pi x)); 6.2 HarmonicDrones row; D8 (2), (4): Both heads of a voice read the same region [Ph, Ph+Dh), half a pass apart. At every output sample the voice therefore sums x(p) and x(p - D/2). The windows are w0 = sin(pi x) and w1 = |cos(pi x)|, and equal-power windows are only flat for uncorrelated material. For a tone the level is A^2 (1 + |sin 2pi x| cos phi), with phi = 2 pi f (D/2) / fs. On the probe's own material (220/440/880 Hz, D = 22050 at 120 BPM, D/2 = 0.25 s = 55 whole cycles) phi = 0. Each voice then swells to +3 dB twice per pass and averages +2.1 dB, so the wet reads about +2.1 dB re input with a +3 dB peak. The 6.2 bars are -1.5 +/- 2 dB (at most +0.5) and peak <= +1 dB, so they cannot be met by this mechanism, and a missed row goes back to the design. At other pitches the swell becomes a dip: C4 = 261.6 Hz gives about -7 dB twice per pass, and any f = 4k+2 Hz nulls completely. That is a 4 Hz tremolo, the same pulsing that problem 13 is meant to remove. D8 (2) ('no 10 ms window below -6 dB') and D8 (4) are both run at 220 Hz, the one in-phase case, so neither can catch this. (suggested: Choose the crossfade so that correlated material stays level. Options: (a) at each head latch, search for the second head's offset or the loop length D in a bounded range around D/2 (for example +/- 10 ms, computed outside the per-sample loop) for maximum normalised correlation (WSOLA / pitch-synchronous splice), and use equal-gain sin^2 windows; or (b) use more heads with Hann windows at 75 % overlap and state the residual depth. Re-derive the 6.2 Drones row from the chosen mechanism. Add a D8 case at non-aligned pitches (222 Hz and 261.63 Hz, 1 and 2 voices) that requires 10 ms RMS p95/p5 depth < 1.5 dB.)

Ruling: superseded by R2 (same mechanism as finding 9); keep the level bars of 6.2 for the drones and re-derive them for the shifter.

Resolved: design.md 2.5.3 ("Level"), 6.2 (Drones level row 0 +/- 2 dB, peak <= +2 dB; sweep row), 8.2.

### Item 15

[challenge, unresolved after 2 revisions] 2.4 (reads wrap inside [from, length)), 2.5.2 (start P +/- jitter*R/2 'wrapped inside the window'), 2.5.1 Variable, 2.5.4: A complete window's last sample (the end of cycle N-1) and its sample 0 (the downbeat of cycle N-1) are not contiguous audio, so every wrap is a hard splice. At the Granular defaults (Fixed, so P = 0; Position Jitter 50 %, so R = 200 ms and starts fall within +/- 50 ms; grains 100 ms) every grain that starts in [-50, 0) ms wraps to the end of the bar and then jumps to the downbeat in the middle of the grain. That is about half of all grains, each with a click under full envelope. 'A cloud around the downbeat' is therefore half made of end-of-bar audio. Variable Euclidean and Noise slices do the same whenever step position + Slice Length passes the window end (for example 16 x 1/16, step 15 at beat 3.75 with a 200 ms slice). No test looks for these splices. The Granular rows only check level and the dominant tone, and the bar end is the same tone. (suggested: Read the continuation from real contiguous audio instead of wrapping. Past the end of play_, read rec_ from offset 0: it holds the cycle that follows play_ and is already written up to the trigger. Before the start of play_, read the tail of spare_ when spare_ is the complete previous cycle. Otherwise clamp the region inside [from, length - grainLen] (one-sided at the downbeat). Add a case: Granular defaults on a 220 Hz bar whose tone changes at the bar line; the maximum sample-to-sample step of the wet stays within 2x that of a clean grain.)

Ruling: accept. Read the continuation from contiguous audio (rec_ past the end of play_, the tail of spare_ before its start) instead of wrapping; otherwise clamp one-sided inside the window; crossfade any remaining splice; add a splice-click case (maximum sample step against the peak).

Resolved: design.md 2.1 (contiguity flags), 2.4 (contiguous continuation from rec_ / spare_, one-sided clamp, crossfaded splice only when nothing fits), 2.5, 2.6, 2.10, D7 (4), D17, 8.2.

### Item 16

[challenge, unresolved after 2 revisions] 2.2 'Silence is captured', 2.7 duck gate table, 4.1 'Transport stopped' free-run, 2.2 'never a silent first bar': While the transport is stopped the clocks free-run and silent cycles are captured, because heard_ stays true after the first onset. The common audition loop of play, stop, then play from bar 1 therefore starts on a silent window. The Insert gate is the slice envelope, not the slice content. At the fresh defaults (Mix 100 %, Insert) bar 1 after every play start gets three 200 ms holes punched into the dry by silent stutters, and in Drones the dry is muted for the whole bar. Under Dry = Mix (every old preset) the first bar is silent. This contradicts 2.2's 'never a silent first bar', which holds only after reset(), and it is exactly the 'no audible effect' complaint again. (suggested: Latch a per-window peak at capture (a running max while recording, O(1) per sample). Treat a window below kCaptureSilenceFloor as 'no window' for triggers and for the duck gate. The wet stays silent, so 'Freeze follows its input' still holds, but the dry is never ducked by silence. Optionally also clear heard_ on a play edge (isPlaying false -> true), so the span plays the first music after play. Add a case: stop with silent input for two cycles, play from beat 0 with music, Insert 100: out == in in bar 1 within 1e-6.)

Ruling: accept. Per-window peak latch (as finding 8); a silent window is "no window" for triggers and the duck; clear heard_ on a play edge so the span plays the first music after play; add the case (stop with silent input for two cycles, play from beat 0 with music, Insert 100: out == in in bar 1 within 1e-6).

Resolved: design.md 2.1, 2.2 (silent window is no window; a play edge that finds a window clears heard_ and drops it), 2.3, 4.1, D4 (10)-(11), D14 (5), 8.2, 8.3 (how the case and "the span plays the first music after play" were joined).

Ruled R9 (2026-10-29): the two halves of the ruling could not both hold; after a play edge the span plays the first music after play, as after reset(), and the bar-1 `out == in` case is withdrawn. Resolved: 2.2, 4.1, D4 (10) (11), D14 (5), 8.1 R9, 8.3.

### Item 17

[challenge, unresolved after 2 revisions] 2.7 gate table (gv '2 ms one-pole', gh 'time constant 20 ms'), 3.1 kDuckSmoothingMs / kHoldDuckSmoothingMs, D10 (5), 6.2 'Insert hold line: 10 time constants of gh by 2.2 s': The design names the smoothers like the existing OnePoleSmoother members (duckSmoother_, holdDuckSmoother_ beside dryWetSmoother_), but OnePoleSmoother::configure(ms) takes the 99 % settling time: coeff = exp(-5000 / (ms * sr)) (smoother.h:88-90), so tau = ms / 5. D10 (5) bounds the per-sample step of the dry gain by 1 - exp(-1 / (0.002 sr)) + 1e-4, which is a true tau of 2 ms. Built with OnePoleSmoother(2 ms) the first step is about 5.5 % at 44.1 kHz against a 1.1 % bound, so the case goes red. Built with a true tau of 2 ms, the -60 dB plateau and out == in gap conditions of D10 (1) need about 28 ms of margin after each slice, which the case does not state. The hold gate has the same ambiguity ('20 ms' against '10 time constants by 2.2 s'). (suggested: State the coefficient explicitly for gv and gh: either OnePoleSmoother with the 99 %-time semantics (and rewrite the D10 (5) bound as 1 - exp(-5 / (0.002 sr)) + 1e-4), or a hand-written one-pole with tau given. In D10 (1) and the 6.2 Insert line, define 'gaps' and 'plateau' with the settling margin, for example excluding 7 tau after each envelope edge.)

Ruling: accept. State every smoother coefficient explicitly (OnePoleSmoother 99 %-time semantics, or a hand-written one-pole with tau given); rewrite the D10 (5) bound accordingly; define "gap" and "plateau" with the settling margin (exclude 7 tau after each envelope edge).

Resolved: design.md 2.7 (coefficients of all three OnePoleSmoothers; gap 10 tau, plateau 7 tau, with the reason for 10), 3.1, 6.2, D10, 8.2.

### Item 18

[challenge, unresolved after 2 revisions] 4.1 jump rule (|pos0 - expectedPos_| > 1.5 / spb, tempo clamped 20..300), 2.2 'A ritardando or accelerando changes the cycle length and nothing else', D14 (6): expectedPos_ advances at the block-start tempo, so within a tempo ramp the host position departs from it quadratically in the block length. For 120 -> 60 BPM over two bars (10 BPM/s) at block 2048 / 44.1 kHz the error is about 1.8e-4 beats against a 5.1e-5 threshold: every block is a 'jump'. A non-boundary jump dirties the cycle, so no capture happens for the whole ritardando and the window goes stale. A host tempo above 300 or below 20 BPM does the same on every block, because spb uses the clamped tempo while the host position moves at the real one (processor.cpp:199 passes the raw tempo). D14 (6) only tests a step change with consistent positions, so it cannot catch either case. (suggested: Use the unclamped host tempo for spb, or derive the beat rate from consecutive host positions. Make the continuation test tolerant to the in-block tempo drift: when |delta| is below a musical threshold (for example 1/64 beat or 5 ms), re-anchor the clocks to the host position without dirtying the cycle, and dirty only on real locates or loop wraps. Add D14 cases: a linear ramp 120 -> 60 BPM at block 2048 must keep one capture per boundary, and a host tempo of 320 BPM must capture.)

Ruling: accept. Use the unclamped host tempo for spb (or derive the beat rate from consecutive host positions); re-anchor the clocks without dirtying the cycle when the delta is below 1/64 beat or 5 ms; dirty only on real locates or loop wraps; add the ramp (120 -> 60 BPM at block 2048, one capture per boundary) and the 320 BPM cases.

Resolved: design.md 4.1 (host-tempo spb, 20..1000 BPM; re-anchor under 1/64 beat or 5 ms), D14 (9)-(10), 8.2.

### Item 19

[challenge, unresolved after 2 revisions] 4.3 'collects the block's events ... at most a few per block', 4.2 processFromPosition fires the callback, 7 real-time rules: The step callback fires inside processFromPosition, before the capture at the same offset and before the segment's input is written. To honour the 4.3 ordering (capture, then hits, then bursts) every event has to be queued and then sorted, but the queue's capacity is never given. 'At most a few' is wrong by an order of magnitude: Pattern Rate goes down to 1/64T (1/24 beat, note_value_ui.h:22). At 300 BPM and 44.1 kHz that is a step every 368 samples, so 11 events in a 4096-sample block and 22 at 8192, plus bursts and the capture. An implementer following the text either drops events or grows a vector on the audio thread. (suggested: Specify a fixed event array sized in prepare(): ceil(maxBlockSize / minStepSamples(300 BPM, 1/64T, sr)) + 2 per clock, or a time-ordered merge of the three clocks that needs no buffer. Say what happens on overflow (it cannot happen if sized as above). Add a case at 1/64T, 300 BPM, block 8192 that counts every hit.)

Ruling: accept. A fixed event array per clock sized in prepare() as ceil(maxBlockSize / minStepSamples(300 BPM, 1/64T, sr)) + 2 (overflow impossible by construction, say so), or a time-ordered merge of the three clocks; add the 1/64T, 300 BPM, block 8192 case counting every hit.

Resolved: design.md 4.3 (three fixed event arrays, sized at 1000 BPM so finding 18 cannot overflow them; the proof), 7, D16, 8.2.

### Item 20

[challenge, unresolved after 2 revisions] 2.5.4 ('slice = window[P + i]', 'never allocates from the slice pool') vs 2.6 ('Euclidean, Granular and Noise voices are copies made at their trigger'): The source of a Noise burst is contradictory and unbuildable as written. Bursts do not use the pool, yet 2.6 calls them trigger-time copies, and no burst buffer is given (up to 2 s x 2 ch, 1.5 MB at 192 kHz). If the burst reads the window live instead, a capture in the middle of a burst switches it to another bar mid-envelope (a click). With a latched buffer, a 2 s burst against a 1/4 capture at 300 BPM (0.2 s cycles) reads a buffer that has become rec_ and is being overwritten two captures later. Overlapping bursts are not defined either: Radio Static (1/16 = 125 ms rate, 150 ms slices) restarts the single burst state in the middle of the previous envelope, the same truncation click as today. (suggested: Pick one model and state it. Either (a) a preallocated burst buffer of kMaxSliceLengthMs, copied at the trigger like the slices, or (b) a latched (buffer, from, length, P) reader, covered by the 2.6 forced-fade rule. Allow two burst voices, or fade the old burst over 5 ms when a new one starts. Add a case: Radio Static rate with 150 ms slices, maximum sample step < 0.05 x peak.)

Ruling: accept option (a): a preallocated burst buffer of kMaxSliceLengthMs copied at the trigger like the slices; two burst voices, the older fading over 5 ms when a new one starts; add the Radio Static case (150 ms slices at 1/16, maximum sample step < 0.05 x peak). Under R5 a burst sustains while Hold is on.

Resolved: design.md 2.1 (memory), 2.5.4 (two burst voices with preallocated buffers, 5 ms fade, sustain under Hold), 2.6, 3.1, D9 (4)-(6), 8.2.

### Item 21

[challenge, unresolved after 2 revisions] D6 (1) Variable sub-case; 2.2 rule 3; 2.4 'In a partial window or the span, Variable behaves as Fixed': The first window after reset() is always produced by rule 3, so it is a partial window, and in a partial window Variable falls back to Fixed. During bar 2 every hit therefore reads the onset, which is 220 Hz. The case 'hits at steps 0, 3 and 6 of bar 2 read as 220, 330 and 550' cannot pass as written, and a fixer would have to change the case rather than the code. (suggested: Feed the four-tone bar repeatedly and assert on bar 3, where the window is the complete bar 2. Add an assertion that bar 2 under Variable reads the onset (the partial-window rule).)

Ruling: accept. Feed the four-tone bar repeatedly and assert the Variable reads on bar 3; assert that bar 2 under Variable reads the onset (the partial-window rule).

Resolved: design.md D6 (1), 8.2.

### Item 22

[challenge, unresolved after 2 revisions] 2.9 / P10 getTailSamples = kInfiniteTail 'while the active mode is Freeze': getTailSamples is a [UI-thread & Setup Done] query (ivstaudioprocessor.h:349-360) and the SDK has no tail-changed restart flag. Hosts read the tail at activation. Switching to Freeze after activation, or loading a project whose mode later changes by automation, leaves the host with the old kNoTail. A host that suspends silent plugins then cuts the Hold-on case off, which is exactly what P10 exists to prevent. The design gives no notification path, and P10's test only calls the getter. (suggested: Either report a mode-independent tail (kInfiniteTail, or a large finite value, for every mode; this also answers open question 4), or have the controller call restartComponent(kLatencyChanged) on a mode edge into or out of Freeze. State the cost: many hosts reactivate on that flag. Write down which one is chosen and why.)

Ruling: resolved by R3 (infinite tail for every mode, no restart).

Resolved: design.md 2.9, P10, 8.1 (R3), 8.2.

## The five design questions of section 8

1. Hold semantics: R1.
   Resolved: design.md 2.3, 8.1 (R1).
2. Factory presets: R7. Ruled R10 (2026-10-29): seven presets in the follow-up, Total Freeze keeps Mix 100 % wet-only (the check objected to the design deciding it alone). Resolved: 5.2, 8.1 R7 and R10.
   Resolved: design.md 5.2, 8.1 (R7).
3. Drones' pitch method: R2.
   Resolved: design.md 2.5.3, 8.1 (R2).
4. Tail: R3.
   Resolved: design.md 2.9, P10, 8.1 (R3).
5. Memory: R4.
   Resolved: design.md 2.1, 8.1 (R4).

## Implementability check (reconcile stage, 2026-10-29)

Eight items. 1 (drum bar), 2 (preset set) and 8 (play edge) were ruled R8, R10 and R9 above. 3 (D17 (2) held a partial window), 4 (D7 (2) needed a complete `spare_`), 5 (the widened scheduler callback and the legacy `process()` offset), 6 (`isCaptureReady` and every case that calls a removed API) were applied to design.md by the main loop: D17 (2), D7 (2), 4.2 and D3, 3.4 and D13 (2). 7 (tagging the expected-red D8 (3) out of the gate) is moot under R8.
