# Changelog

All notable changes to Gradus will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.5.0] - Unreleased

### Added

- **Concentric ring display** — Replaced horizontal lane stack with a circular step sequencer where steps are arranged radially across 4 concentric rings combining all 8 lanes; per-step colored playhead highlights race around at independent lane speeds, visually communicating polymetric relationships
- **Detail strip with lane tabs** — 8 color-coded tabs (VEL, GATE, PITCH, MOD, COND, RATCH, CHORD, INV) alongside the ring display, each opening a familiar linear step editor for precision editing; bidirectional selection syncs ring and tab
- **Direct ring editing** — Drag radially on velocity, gate, pitch, and ratchet rings to adjust values; click to cycle chord, inversion, modifier, and condition values
- **Euclidean center visualizer** — Bjorklund-algorithm dot ring in the center of the circular display showing the current Euclidean rhythm pattern (hits, steps, rotation)
- **Hover indicators** — Per-lane colored highlight on ring segments when hovering; vertical resize cursor on bar-type lanes in both ring and linear editors
- **Ring playhead trails** — Fading trail behind each lane's playhead highlight, using existing trail alpha levels for visual continuity
- **Ratchet velocity decay** — New "Rtch Dcy" parameter (0-100%) applies exponential velocity falloff across ratchet subdivisions, giving ratcheted notes a natural bouncing-ball feel instead of flat-velocity bursts
- **Strum mode for chords** — New "Strum" (0-100ms) and "Direction" (Up/Down/Random/Alternate) parameters spread chord note-on events in time, producing a guitar-strum effect for generated chords
- **Per-lane swing** — 8 new swing parameters (0-75%), one per lane, shown contextually in the detail strip for the selected lane; each lane's swing independently skews its advance timing, creating polymetric groove interactions on top of the existing per-lane speed multipliers
- **Velocity curve** — New curve shaping (Linear / Exponential / Logarithmic / S-Curve) with an amount knob (0-100%), applied after the velocity lane; shapes the dynamic feel of the whole pattern without redrawing every step. Shown contextually on the Velocity lane.
- **Transpose** — New global transpose knob (-24 to +24 semitones) in control row 1; when a non-chromatic scale is active the transpose snaps through the scale so the result always stays in key.
- **Per-lane length jitter** — 8 new jitter parameters (0-4 steps), one per lane, shown contextually in the detail strip alongside per-lane swing. Each lane independently re-rolls a random offset when it wraps, extending or shortening its effective cycle length — creating evolving non-repeating patterns that differ per lane.

### Changed

- **Window size** — Increased from 900x860 to 1100x816 to accommodate the ring display and detail strip side-by-side
- **UI architecture** — Lane views are now constructed in `didOpen()` and routed to the DetailStrip container; RingDataBridge reads lane data via the existing IArpLane interface without modifying shared code

## [1.0.0] - 2026-03-31

### Added

- **Standalone step arpeggiator** — Extracted from Ruinae's arpeggiator section as an independent VST3 instrument plugin with MIDI output, compatible with any DAW that supports VST3 instruments
- **8 independent polymetric lanes** — Velocity, Gate, Pitch, Chord, Inversion, Ratchet, Modifier, and Condition lanes, each with independent step counts (1-32) for non-repeating pattern evolution
- **Per-lane speed multipliers** — Each lane runs at its own clock rate (0.25x, 0.5x, 0.75x, 1x, 1.25x, 1.5x, 1.75x, 2x, 3x, 4x) using fractional accumulators, adding a second dimension of polyrhythmic complexity on top of independent lane lengths
- **10 arpeggiator modes** — Up, Down, Up/Down, Down/Up, Converge, Diverge, Random, Walk, As Played, and Chord
- **Euclidean rhythm generation** — Mathematically distributed rhythmic patterns with configurable hits (0-32), steps (2-32), and rotation offset (0-31)
- **Conditional triggers** — 18 per-step conditions including probability (10%-90%), ratio patterns (1:2, 2:3, 3:4, etc.), first note, and fill/not-fill triggers for live performance
- **Per-step ratcheting** — 1-4 sub-divisions per step with independent ratchet swing (50-75%) for rapid-fire note bursts
- **Per-step modifiers** — Active, Tie, Slide, and Accent flags per step with configurable accent velocity boost (0-127) and slide portamento time (0-500 ms)
- **Chord generation** — Per-step chord types (None, Dyad, Triad, 7th, 9th) with inversions (Root, 1st, 2nd, 3rd) and voicing modes (Close, Drop 2, Spread, Random)
- **Scale quantization** — 16 scale types (Major, Natural Minor, Harmonic Minor, Melodic Minor, Dorian, Mixolydian, Phrygian, Lydian, Chromatic, Locrian, Major Pentatonic, Minor Pentatonic, Blues, Whole Tone, Diminished W-H, Diminished H-W) with selectable root note (C through B); optional input note quantization
- **Spice & Dice** — Blend random pattern variations (0-100%) on top of the base pattern; re-roll with the Dice button for fresh generative results
- **Humanize** — Subtle timing variations (0-100%) for a more natural, less mechanical feel
- **Built-in audition synth** — Monophonic PolyBLEP oscillator (Sine, Sawtooth, Square) with linear ADSR envelope (adjustable decay 10-2000 ms) and volume control; session-only settings not saved in presets
- **Tempo sync and free rate** — Lock to host tempo with 30 note values (1/64T through 4/1D), or run at a free rate from 0.5-50 Hz; arp clocks independently of DAW transport
- **Latch modes** — Off, Hold, and Add modes for hands-free pattern playback
- **Retrigger modes** — Off, Note, and Beat retrigger for controlling pattern restart behavior
- **Fill mode** — Latching toggle for live performance; activates fill-conditional steps for build-ups and transitions
- **Copy/paste between lanes** — Copy step data from any lane and paste into another for quick pattern duplication and variation
- **Lane transforms** — Invert, shift left, shift right, and randomize operations per lane
- **MIDI output** — Arpeggiated notes output as standard VST3 MIDI events for driving any instrument in the DAW
- **Preset browser** — Browse and save presets with category filtering
- **40 factory presets** across 8 categories — Classic, Acid, Euclidean, Polymetric, Generative, Performance, Chords, and Advanced; all presets use the full parameter space with creative velocity contours, gate variation, pitch intervals, modifiers, ratchets, conditions, chords, scale quantization, and lane speeds
- **Preset sharing with Ruinae** — Arp parameter IDs (3000-3387) are identical between Gradus and Ruinae, enabling cross-plugin preset exchange
- **Full parameter automation** — All parameters are automatable by the host
- **Cross-platform** — VST3 (Windows, macOS, Linux) + Audio Unit v2/v3 (macOS)
- **Pluginval validated** — Passes strictness level 5 on all platforms
