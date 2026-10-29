# Freeze and rhythmic re-sampling: prior art

Scope: how shipping products capture and hold audio ("freeze") and re-sample it rhythmically (beat repeat, stutter), so Iterum's Pattern Freeze (DelayMode::Freeze, spec 069) can be judged against what users expect. Research date 2026-10-29. Every claim carries a URL. A claim marked **[unverified]** came from general knowledge or a source that did not state it, and must not be used as a requirement without a second source. Products I could not document are listed under "Not found".

## Findings, by severity

### HIGH 1. Every product that is audible out of the box either runs continuously or is a pure effect with an obvious default; none of the documented "freeze" features is silent until the user does something *and* leaves the dry signal as the only output

- Clouds / Beads: grains are generated from the live buffer all the time. Freeze is a separate latching button that only *stops recording*; the grain engine keeps playing. So the effect is audible before and after freeze. Evidence: "This latching button stops the recording of incoming audio" and "grains come from the last few seconds of audio kept in memory" (https://pichenette.readthedocs.io/en/latest/modules/clouds/manual/); Beads: FREEZE "disable[s] the recording of the incoming audio signal in the buffer. Existing buffer content can still be played back", and latched grain generation is the power-on default (https://pichenette.readthedocs.io/en/latest/modules/beads/manual/).
- Fracture (Glitchmachines): the buffer "records a small portion of the input signal and loops it a certain number of times... When it has finished looping, it automatically records the current input and starts looping again." No capture action exists; capture is automatic and continuous, SIZE 1 ms to 1 s, REPEATS 1 to 128 (https://glitchmachines.com/wp-content/uploads/FRACTURE_User_Guide.pdf, text extracted locally).
- Effectrix 2: with Clock Source HOST the sequencer "runs with your DAW when you press the play button"; the alternative is a PLAY button that can be driven by MIDI or automation (https://downloads.sugar-bytes.de/manuals/Effectrix2.pdf, pp. 38-39 of the manual).
- Stutter Edit 2: "Auto Mode allows Stutter Edit 2 to act as a standard audio effect, no MIDI triggering required"; otherwise "each MIDI note triggers a different Gesture" (https://www.izotope.com/en/learn/learn-stutter-edit-in-10-minutes).
- Contrast with Iterum Freeze as described in the task: the user hears only the input at defaults. The only products with a "nothing happens until you press it" design are the explicit momentary/latching freeze switches (BigSky, Blackhole, Clouds' button, Ableton Beat Repeat's Repeat button), and in those the button is the one obvious control on the panel and the unfrozen state is still a working effect (reverb, grains, or a pass-through that is documented as such).

Implication: a mode named Freeze must ship either (a) audible by default through a continuous/auto-triggered engine, or (b) with a single prominent capture control. Iterum has neither at defaults, per the user report.

### HIGH 2. "Freeze" has two established meanings and users expect one of them to be chosen explicitly

- Hold / sustain: stop writing new input, keep a loop or tail alive. Clouds/Beads (links above); Strymon BigSky Hold: "Infinite mode keeps the reverb tail sounding and adds each new note to it. Freeze mode also sustains indefinitely, but new notes played over it stay out of the reverb" (https://audient.com/arc/strymon-big-sky, https://www.soundonsound.com/node/4929529); Eventide Blackhole: Freeze mode "holds the effect in stasis, allowing you to play over the reverb tail", Infinite "layers up sounds on top" (https://www.soundonsound.com/reviews/eventide-blackhole-pedal, https://www.bhphotovideo.com/lit_files/690187.pdf); Stutter Edit 2 Freeze "captures part of the audio buffer, determined by the Duration parameter, and repeat[s] it indefinitely" (https://www.izotope.com/en/learn/learn-stutter-edit-in-10-minutes); Valhalla Supermassive freeze = Feedback 100 %, Mod Depth 0, with the note that internal low-pass filtering still dulls it over minutes (https://valhalladsp.com/?p=567197, via search summary; page itself returned 403).
- Rhythmic re-sampling: capture a slice on a grid and repeat or retrigger it. Ableton Beat Repeat, Stutter Edit, Effectrix.

Iterum's Pattern Freeze is the second kind with a rolling pre-capture buffer (the task description: every input sample is written to a 5 s RollingCaptureBuffer and slices are cut from offset 0). Neither a hold switch nor a "Repeat now" switch exists.

### HIGH 3. Ableton Beat Repeat is the closest reference and its control model is explicit and small

Source: https://www.ableton.com/en/manual/live-audio-effect-reference/ (Beat Repeat section).
- Capture trigger: Interval ("how often Beat Repeat captures new material and starts repeating it", tempo-synced 1/32 to 4 bars), Offset ("shifts the Interval point forward"), Chance (probability that a repetition occurs). The Repeat button "bypasses Interval, Offset, Chance, and Gate. It captures and repeats immediately until turned off." This is the momentary/latching manual capture.
- What is held: a slice sized by Grid (a 1/16 grid repeats a sixteenth-note slice); Gate sets the total length of all repetitions in 16ths (4/16 = one beat from the Interval/Offset point); Variation randomises grid size.
- Grid relation: Interval, Offset, Gate and Grid are all in host-tempo note values, so playback is bar/beat aligned.
- Dry/wet: three explicit modes. Mix = original passes plus repeats; Insert = original muted only while repeats play; Gate = only repeats are output (recommended for return tracks). Volume and Decay (fading repeats) are separate.
- Extras: Pitch and Pitch Decay (resampled pitch drop per repeat), filter (LP/HP band), No Triplets.
- Not stated in the manual text I could read: default values, and behaviour with the transport stopped. **[unverified]**: from general knowledge, Beat Repeat's default fires once per bar in Mix mode with the transport running, and is silent on a stopped transport because Interval counts from the song position. Do not treat as sourced.

Implication for Iterum: Insert mode (duck the dry signal only while a repeat plays) is the standard answer to "dry + wet at the same time sounds like nothing happened", and Gate mode is the return-track convention.

### MEDIUM 4. Rhythmic engines run off the host song position and have an explicit non-host fallback

- Effectrix 2: HOST clock mode follows DAW play; PLAY mode is a manual start that "can also be triggered by MIDI or automation"; "Effectrix2 runs at your host bpm, using the Tempo controls you can set the step resolution" (https://downloads.sugar-bytes.de/manuals/Effectrix2.pdf). Two firing modes are documented: MIDI-note-gated ("only applying the effect sequence as long as you hold a note") or running along the song position.
- Stutter Edit 2: Gesture panel controls "how closely the Gesture you chose syncs with your session tempo"; Auto Mode removes the MIDI requirement (https://www.izotope.com/en/learn/learn-stutter-edit-in-10-minutes).
- Arturia Efx FRAGMENTS: Rhythmic release mode follows "the rhythm of the incoming audio or DAW tempo", with a step sequencer; buffer length 1/8 bar to 4 bars (https://www.soundonsound.com/reviews/arturia-efx-fragments).
- Ableton Looper: with the transport running it behaves like a clip and "is subject to launch quantization"; Tempo Control (None / Follow song tempo / Set & Follow song tempo) and Song Control (None / Start Song / Start & Stop Song) decide how it relates to the transport (Ableton manual above, Looper section).
- Behaviour when the host is stopped is documented for none of these beyond the clock-mode choice. Effectrix's answer is a separate PLAY button; Stutter Edit's is Auto Mode. Neither relies on the host transport being active. This matters because the task states Euclidean and NoiseBursts require a valid tempo and the probe measures "transport state".

### MEDIUM 5. Time-based "stutter" tools expose a short list of first-reach controls

- Beat Repeat: Interval, Grid, Gate, Mix mode, Decay, Chance (Ableton manual).
- Stutter Edit 2: Rate (note-value range), Quantize (Step, Mode: Free, Closest, Walk, Skip, Stagger, Random), Gate (Width, Tail: "lower values produce a choppier effect"), Buffer Position / Grid / Movement (Slider, Grid, Random), Jitter, Pan (https://s3.amazonaws.com/izotopedownloads/docs/stutteredit2/en/stutter-and-buffer/index.html).
- Fracture: Size, Repeats, Ratio (playback rate) (Glitchmachines guide).
- Clouds / Beads: Position/Time, Size, Density, Texture/Shape, Blend (dry/wet, spread, feedback, reverb), four quality modes (pichenette manuals above). Density at 12 o'clock generates no grains; the module is silent there by design and the panel labels it so.
- Soundtoys Crystallizer: Mix, Splice, Pitch, Delay (up to 2 s), and a feedback control named Recycle (https://www.soundtoys.com/wp-content/uploads/Crystallizer-Manual.pdf, via search snippets; the PDF was not fetched).
- Efx FRAGMENTS: Intensity and FX macros, Feedback, Grain Volume, Grain Mix, Buffer length; grain capture modes Speed, Offset, Manual Scan (SOS review above).

### MEDIUM 6. Slice envelopes and grain texture are always user-visible

Clouds Texture morphs boxcar, triangle, Hann windows (Clouds manual); Stutter Edit Gate Width/Tail; Beat Repeat Decay; Beads Shape = envelope on repeats. Iterum's fixed 10 ms attack / 100 ms release default (task text) makes a 200 ms slice mostly envelope; compare with the sources above where the envelope is a first-class control and the slice is typically gated, not faded.

### LOW 7. Scrubbing the frozen material is a standard extra

Clouds: Position "selects the part of the recording buffer that grains are played from", works while frozen (Clouds manual). Stutter Edit Buffer Position; FRAGMENTS Manual Scan. Iterum cuts slices from offset 0 (newest) except GranularScatter, which is the weakest variant of this convention.

### LOW 8. Frozen material degrades or evolves unless deliberately fixed

Supermassive: internal filters make a 100 % feedback freeze "duller and more muted over several minutes" (Valhalla tips page via search summary). Clouds: when frozen, output is routed through delays and all-pass filters (Blend), "strange things happen when FREEZE is enabled" (Clouds manual). Users expect a frozen texture to be stable or slowly modulated, not looping a visible seam.

## Per-product table

| Product | Capture trigger | What is held | Relation to host grid | Transport stopped | Dry / wet | Audible with no interaction? | First-reach controls | Source |
|---|---|---|---|---|---|---|---|---|
| Ableton Beat Repeat | Interval/Offset/Chance auto; Repeat button = manual, immediate | Slice of Grid size, repeated for Gate length | Tempo-synced note values | Not documented | Mix / Insert / Gate modes, Volume, Decay | Not stated in manual **[unverified]** | Interval, Grid, Gate, mode, Decay | Ableton manual |
| Ableton Echo | n/a. The Echo section I read has no freeze or hold; it auto-offs ~8 s after silence | n/a | Sync switch, note divisions | Echo shuts off after input stops (8 s) unless Noise/Gate on | Dry/Wet, Equal-Loudness option | n/a | Time, Feedback, Dry/Wet, Gate, Ducking | Ableton manual |
| Ableton Delay (Live 12 text read) | Freeze button | Buffer cycles, "ignores new input until deactivated" (from the fetch summary, text not quoted verbatim; recheck) | Delay times synced or ms | n/a | Dry/Wet | n/a | Freeze | Ableton manual |
| Ableton Looper | Record / Overdub / Play buttons | Loop of recorded length | Launch quantization, Tempo Control, Song Control | Follows song-control choice | Feedback only on overdub; monitor modes | No (needs record) | Record/Overdub/Play, Tempo Control | Ableton manual |
| iZotope Stutter Edit 2 | MIDI note per Gesture, or Auto Mode | Buffer region, Freeze = Duration-length loop | Gesture sync to session tempo; Quantize/Grid in note values | Not documented (Auto Mode exists to remove MIDI need) | Per-Gesture; Gate Width/Tail | Yes in Auto Mode | Rate, Quantize, Gate, Buffer position | izotope.com 10-minutes + help pages |
| NI Replika XT | Freeze function exists | Not documented in the manual pages I could read | Tempo/Sync switch follows DAW tempo | Not documented | Mix = equal-power crossfade | Delay yes; freeze not documented | Time, Feedback, Mix | docs.native-instruments.com Replika XT delay section; freeze behaviour **not found** |
| Valhalla Supermassive | No freeze control; Feedback 100 % + Mod 0 | Reverb/delay tail | Free | Free | Mix | Yes (it is a reverb) | Feedback, Mod Depth | valhalladsp.com Tips and Tricks |
| Eventide Blackhole | Freeze footswitch (Freeze mode) or Infinite mode | Reverb tail; Freeze excludes new input, Infinite stacks | Free | n/a (pedal) | Mix knob | Yes | Mix, Gravity, Size, Feedback | Eventide user guide; SOS review |
| Strymon BigSky | Press and hold preset footswitch = Hold; mode switch picks Infinite or Freeze | Reverb tail | Free | n/a (pedal) | Mix | Yes | Hold switch + mode | Audient, SOS, Morningstar (CC#70 mode, CC#97 hold) |
| Strymon Timeline | Hold A or B footswitch = infinite repeats; Looper mode via Tap hold | Delay loop / 30 s looper | Tap tempo | n/a | Mix | Yes | Time, Repeats, Mix | Strymon quick start via manualsnet.com. No separate freeze found |
| Output Portal | Freeze button on its Reverb module only | Reverb tail | Grain rate 1/64t to 1 bar | Not documented | Per-processor dry/wet plus global | Presets yes | Rate, Spread-type grain controls | musicradar, gearnews, ask.audio |
| Glitchmachines Fracture | None; automatic record-loop-rerecord cycle | 1 ms to 1 s of input, 1 to 128 repeats | Free (LFOs have sync) | Not applicable (free-running) | Not stated in text I read | Yes (auto cycle) | Size, Repeats, Ratio | FRACTURE_User_Guide.pdf |
| Sugar Bytes Effectrix 2 | Sequencer steps; HOST clock or PLAY button/MIDI/automation | Looper A/B, Grain effects | Host bpm with step resolution | PLAY mode runs without host; HOST mode follows DAW play | Per-effect | Only when sequencer has steps | Pattern steps, Looper Size | Effectrix2.pdf |
| Cableguys TimeShaper (ShaperBox 3) | Curve drawn over a tempo grid | Stored incoming audio replayed along a curve | Host tempo grid | Not documented (manual page not retrieved) | Not retrieved | Not retrieved | Curve, snap-to-grid | cableguys.com/timeshaper, rekkerd review. Manual fetch returned 404 |
| Soundtoys Crystallizer | None | Splice of recent input pitch-shifted | Free | Free | Mix | Yes | Mix, Splice, Pitch, Recycle, Delay | Soundtoys manual (snippets) |
| Soundtoys Little AlterBoy | No freeze | n/a | n/a | n/a | Mix | n/a | Pitch, Formant, Mix | Soundtoys user guide (snippets). No freeze or hold feature found |
| Mutable Clouds / Beads | Latching FREEZE button or gate input | Buffer (Clouds up to 8 s lo-fi, Beads grain buffer); grains keep playing | Free (Beads has SEED clock) | n/a | Blend knob (dry/wet, spread, feedback, reverb) | Yes (grains play live input) | Position, Size, Density, Texture, Blend | pichenette docs |
| Arturia Efx FRAGMENTS | Freeze control; release modes Classic/Texture/Rhythmic | Buffer 1/8 bar to 4 bars | Rhythmic mode follows DAW tempo | Not documented | Grain Mix | Presets yes | Intensity, Feedback, Grain Mix, Buffer | SOS review |

## Conventions that recur

| Convention | Seen in |
|---|---|
| The effect does something audible at its default or first preset; the dry signal alone is never the whole output | Clouds, Beads, Fracture, Stutter Edit Auto Mode, Supermassive, Blackhole, FRAGMENTS presets |
| Freeze is a latching switch that stops *recording* and leaves the playback engine running | Clouds, Beads, BigSky Freeze mode, Blackhole Freeze mode |
| A separate manual "do it now" capture that bypasses the grid | Beat Repeat Repeat button, Stutter Edit MIDI note, Effectrix PLAY, Ableton Looper Record |
| Slice length is a tempo-synced note value | Beat Repeat Grid, Stutter Edit Rate, Effectrix step resolution, FRAGMENTS buffer in bars |
| Repetition schedule has its own tempo-synced controls (interval, offset, gate length, chance) | Beat Repeat, Stutter Edit Quantize/Step |
| Dry handling is an explicit choice, not just a mix knob: pass-through, duck while repeating, wet-only | Beat Repeat Mix/Insert/Gate; Replika XT and Blackhole use plain Mix |
| Each repeat can decay or change (pitch, size) | Beat Repeat Decay and Pitch Decay, Effectrix Looper A Size/Pitch change modes, Fracture Ratio |
| A "not synced to host" fallback exists (Auto Mode, PLAY button, free-running) | Stutter Edit, Effectrix, Fracture, Clouds |
| Position/scan control over the held material | Clouds Position, Stutter Edit Buffer Position, FRAGMENTS Manual Scan |
| Grain or slice envelope is a visible control | Clouds Texture, Stutter Edit Gate Tail, Beads Shape |

## Product-specific extras

- Beat Repeat: Chance, Variation (random grid), Pitch Decay, band filter, No Triplets.
- Stutter Edit 2: per-note Gestures, Quantize modes (Walk, Skip, Stagger), Jitter, alternating Pan.
- Effectrix 2: 12 pattern keys switchable by MIDI note, Looper A with ten-ish loop-size change modes (Reverse, Double Reverse, Get Slow, Get Fast, Wonky).
- Clouds / Beads: 4 quality modes, saved frozen buffers, CV freeze gate, reverb inside the grain feedback path.
- Blackhole / BigSky: Infinite vs Freeze selector; MIDI CC mapping for the hold switch.
- Ableton Looper: Song Control (starts and stops the host transport).
- Fracture: automatic re-record after N repeats (the self-refreshing buffer).

## Not found (do not cite as researched)

- Native Instruments Replika XT Freeze and Molekular: the Replika XT manual delay-section page has no Freeze text; only a Dubspot blurb confirms a freeze function exists. Molekular was not found at all.
- Valhalla Freq Echo: no freeze documentation found.
- Glitchmachines Polygon: no documentation found.
- Sugar Bytes Looperator manual: not found (only a Sound On Sound mention that it borrows from Effectrix).
- Cableguys ShaperBox 3 manual: the PDF URL returned 404 on fetch; only the product page and reviews were read.
- Soundtoys Crystallizer / Little AlterBoy manuals: seen only as search snippets.
- Ableton Echo: the manual text read contains no freeze; Live 12 may differ. Ableton "Delay" Freeze came from a summarised fetch, not a verbatim quote.
- Iterum's own default behaviour was not measured for this report.

## Sources

- Ableton Live manual: https://www.ableton.com/en/manual/live-audio-effect-reference/
- iZotope Stutter Edit 2: https://www.izotope.com/en/learn/learn-stutter-edit-in-10-minutes ; https://s3.amazonaws.com/izotopedownloads/docs/stutteredit2/en/stutter-and-buffer/index.html
- NI Replika XT manual: https://docs.native-instruments.com/online-guides/replika-xt-manual/en/delay-section
- Valhalla Supermassive tips: https://valhalladsp.com/?p=567197 (403 on fetch; content from search summary)
- Eventide Blackhole: https://www.bhphotovideo.com/lit_files/690187.pdf ; https://www.soundonsound.com/reviews/eventide-blackhole-pedal
- Strymon BigSky: https://audient.com/arc/strymon-big-sky ; https://www.soundonsound.com/node/4929529 ; https://www.morningstar.io/post/controlling-freeze-infinite-function-on-the-strymon-bigsky-with-a-midi-controller
- Strymon Timeline: https://manualsnet.com/strymon/timeline
- Mutable Clouds: https://pichenette.readthedocs.io/en/latest/modules/clouds/manual/ ; Beads: https://pichenette.readthedocs.io/en/latest/modules/beads/manual/
- Glitchmachines Fracture: https://glitchmachines.com/wp-content/uploads/FRACTURE_User_Guide.pdf
- Sugar Bytes Effectrix 2: https://downloads.sugar-bytes.de/manuals/Effectrix2.pdf
- Cableguys TimeShaper: https://www.cableguys.com/timeshaper ; https://rekkerd.org/review-timeshaper-multi-band-time-manipulation-plugin-by-cableguys/
- Soundtoys: https://www.soundtoys.com/wp-content/uploads/Crystallizer-Manual.pdf ; https://store.soundtoys.com/product/little-alterboy
- Arturia Efx FRAGMENTS: https://www.soundonsound.com/reviews/arturia-efx-fragments
- Output Portal: https://www.musicradar.com/reviews/output-portal
