# Changelog

All notable changes to Ruinae will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.9.1] - 2026-02-28

### Added

- **Arpeggiator Scale Mode** — Optional musical scale constraint for the arpeggiator
  - 16 scale types: Chromatic, Major, Natural Minor, Harmonic Minor, Melodic Minor, Dorian, Phrygian, Lydian, Mixolydian, Locrian, Major Pentatonic, Minor Pentatonic, Blues, Whole Tone, Diminished (W-H), Diminished (H-W)
  - 12 root notes (C through B)
  - Pitch lane interprets step values as scale degree offsets when a non-Chromatic scale is active (e.g., +2 in C Major = E, not D)
  - Correct octave wrapping for all scale sizes (5-12 notes)
  - Scale Quantize Input toggle: snaps incoming MIDI notes to the nearest scale note before entering the arp pool
  - Root Note and Scale Quantize Input controls dim when Chromatic is selected
  - Pitch lane popup suffix changes from "st" to "deg" in scale mode
  - Chromatic mode (default) is identical to pre-feature behavior

- **Harmonizer scale extension** — Harmonizer dropdown now exposes all 16 scale types (was 9)

### Changed

- VST3 post-build copy destination changed from `C:/Program Files/Common Files/VST3` to `%LOCALAPPDATA%/Programs/Common/VST3` (no admin required)

## [0.9.0] - 2026-02-27

### Added

- **Preset Browser**
  - Overlay-based preset browser with category and subcategory filtering
  - Case-insensitive name search
  - Persistent preset browser button in the top bar across all tabs

- **Factory Presets** (14 presets across 6 categories)
  - Arp Acid: Acid_Line_303, Acid_Stab
  - Arp Classic: Basic_Up_1-16, Down_1-8, UpDown_1-8T
  - Arp Euclidean: Bossa_E(516), Samba_E(716), Tresillo_E(38)
  - Arp Generative: Chaos_Garden, Spice_Evolver
  - Arp Performance: Fill_Cascade, Probability_Waves
  - Arp Polymetric: 3x5x7_Evolving, 4x5_Shifting

### Fixed

- **Stuck notes with arp slide modifier**: Poly-legato glide changed voice pitch without updating the voice allocator's note tracking, so the subsequent noteOff for the new pitch couldn't find the voice. The allocator note is now updated after glide.

## [0.8.0] - 2026-02-25

### Added

- **Arpeggiator UI** — Full arpeggiator interface in the SEQ tab
  - Specialized lane views for Velocity, Gate, Pitch, Modifier, Ratchet, and Condition
  - `ArpConditionLane` with right-click reset and tooltip descriptions for all 18 condition types
  - `ArpModifierLane` with color-coded step display (Active, Rest, Tie, Slide, Accent)
  - `ArpLaneContainer` managing multiple independent-length lanes
  - Dropdown callbacks for arp mode, note value, latch, and retrigger

### Fixed

- Arp lane rendering bugs: consistent clamping, collapse guard, lane color consistency

## [0.7.0] - 2026-02-23

### Added

- **Arpeggiator Engine** — Full-featured polyphonic arpeggiator
  - 10 modes: Up, Down, UpDown, DownUp, Converge, Diverge, Random, Walk, AsPlayed, Chord
  - 6 independent lanes (each 1-32 steps): Velocity, Gate, Pitch, Modifier, Ratchet, Condition
  - Per-step modifiers: Active, Rest, Tie, Slide, Accent
  - 18 conditional trigger types: probability (10%-90%), cycle-based (every Nth), First, Fill
  - Euclidean timing with configurable hits, steps, and rotation
  - Ratcheting (1-4 subdivisions per step) with swing
  - Spice/Dice randomization and Humanize for timing/velocity variation
  - Swing (0-75%), latch (Off/Hold/Add), 1-4 octave range
  - Tempo sync with 21 note values (1/32T to 4/1) or free rate (0.5-50 Hz)
  - Modulation destinations: Arp Rate, Gate Length, Octave Range, Swing, Spice

### Fixed

- Reverb volume drop when switching parameters (switched to equal-power crossfade)

## [0.6.0] - 2026-02-20

### Added

- **4-Tab UI Layout** — Restructured interface into SOUND, MOD, FX, and SEQ tabs
- **UI resize** from 925x880 to 1200x720 for better screen utilization
- **Oscillator type-specific parameter panels** — Dynamic sub-panels swap when oscillator type changes (10 panel variants per oscillator via UIViewSwitchContainer)
- **Value popup on ArcKnob** — Shows current value on hover
- **Hand cursor on ADSR control points** — Visual feedback for draggable envelope handles

### Changed

- Global Filter merged into the MASTER section (persistent across all tabs)
- Gain compensation now scales with active voice count rather than the polyphony setting

### Fixed

- Settings drawer toggle broken by harmonizer tag range overlap

## [0.5.0] - 2026-02-19

### Added

- **Harmonizer** effect in the FX chain
  - Chromatic and Scalic harmony modes
  - 12 keys and 9 scales (Major, Minor, Harmonic Minor, Dorian, Mixolydian, etc.)
  - Up to 4 simultaneous harmony voices with per-voice interval, level, pan, delay, and detune
  - 4 pitch shift algorithms: Simple, Granular, Phase Vocoder, Pitch Sync
  - Formant preservation toggle
  - Independent dry/wet level controls

- **Trance Gate UI improvements**
  - Right-click to clear individual steps
  - Dropdown-based preset selector (replaced button row)

## [0.4.0] - 2026-02-16

### Added

- **Extended Modulation Sources**
  - Pitch Follower — follows MIDI note pitch as a mod source
  - Transient — transient detection mod source
  - Sample & Hold — sampled random CV source
  - Env Follower — audio-reactive envelope follower

- **Macros** — 4 user-assignable macro knobs (Macro 1-4), host-automatable

- **Rungler** — Buchla-style shift register random CV generator
  - Dual oscillator cross-modulation
  - Configurable shift register length (4-16 bits)
  - Chaos/Loop mode toggle
  - CV smoothing filter

- **Settings Drawer** — Slide-in panel for voice and tuning configuration
  - Pitch bend range (0-24 semitones)
  - Velocity curve (Linear, Soft, Hard, Fixed)
  - Tuning reference (400-480 Hz)
  - Voice allocation and steal mode
  - Gain compensation toggle

- **Mono Mode** with note priority (Last, High, Low) and legato toggle

- **Global Filter** (post-voice-mix, pre-effects) — LP, HP, BP, Notch with cutoff and resonance

- **Trance Gate tempo sync** with note value selector

## [0.3.0] - 2026-02-14

### Added

- **Phaser** effect — 2-12 allpass stages, tempo sync, stereo spread, 4 LFO waveforms
- **Chaos Mod Source** — Lorenz/Rossler attractor as modulation source with rate and depth
- **Master Section** — Master gain, stereo width, voice spread, soft limiter toggle
- **LFO enhancements** — Shape selector (6 shapes), tempo sync, phase offset, retrigger, unipolar/bipolar, fade-in, symmetry, quantize steps

### Fixed

- Chaos mod source evolving ~500x too slowly (was advancing 1 tick per block instead of per sample)
- Filter cutoff modulation scaling producing inaudible results
- Phaser feedback parameter labeled as "Resonance" (renamed to "Feedback")

## [0.2.0] - 2026-02-13

### Added

- **Modulation Matrix** — 8-slot global matrix with source, destination, bipolar amount, curve (Linear/Exp/Log/S-Curve), smoothing, scaling, and bypass per slot
- **Mod Matrix Grid UI** — Visual 8x4 grid with heatmap display and per-slot detail panel
- **Per-voice distortion** — 6 types: Clean, Chaos Waveshaper, Spectral Distortion, Granular Distortion, Wavefolder (Triangle/Sine/Lockhart), Tape Saturator (Simple/Hysteresis)
- **Per-voice filter** — 13 filter types with type-specific controls: SVF variants (LP/HP/BP/Notch/Allpass/Peak/Low Shelf/High Shelf), Ladder (4 slopes), Formant, Comb, Envelope Filter, Self-Oscillating
- **Reverb** — Dattorro plate algorithm with size, damping, width, pre-delay, diffusion, freeze, and modulation
- **ADSR envelope editor** — Draggable control points with per-segment curve amounts and Bezier mode
- **XY Morph Pad** — 2D control for oscillator crossfade/spectral morph position
- **Oscillator type selector** — Per-oscillator type switching with dynamic sub-panel visibility
- **Named color palette** — Extracted from hard-coded values for consistent theming
- **Spectral tilt** as both per-voice and global modulation destination
- **AllVoice Resonance and Envelope Amount** modulation destinations

### Fixed

- SVF filter crackle on note retrigger
- Formant filter being almost inaudible (added Q-based gain compensation)
- Mod amount slider UX and heatmap sync bugs
- Spectral tilt gain clamping and morph pad tag assignment
- State restoration bug causing parameter values to reset

## [0.1.0] - 2026-02-09

### Added

- **Initial plugin skeleton** — VST3 entry point, processor, controller, and VSTGUI editor
- **Synth engine** — Polyphonic engine with up to 16 pre-allocated voices and configurable voice allocation (Round Robin, Oldest, Lowest Velocity, Highest Note)
- **10 oscillator types** (per voice, dual oscillators A + B):
  - PolyBLEP (Sine, Saw, Square, Pulse, Triangle)
  - Wavetable (mipmapped single-cycle)
  - Phase Distortion (8 waveforms)
  - Oscillator Sync (Hard, Reverse, Phase Advance)
  - Additive (1-128 partials, spectral tilt, inharmonicity)
  - Chaos Attractor (Lorenz, Rossler, Chua, Duffing, Van der Pol)
  - Particle (granular cloud, 1-64 density, scatter, drift)
  - Formant (A-E-I-O-U vowel morphing)
  - Spectral Freeze (FFT-based, pitch/tilt/formant shift)
  - Noise (White, Pink, Brown, Blue, Violet, Grey)
- **Dual oscillator mixer** — Crossfade and Spectral Morph modes
- **3 ADSR envelopes** per voice — Amplitude, Filter, Modulation (each with per-segment curve control)
- **Trance Gate** — Step sequencer (2-32 steps), euclidean mode, rate/depth/attack/release
- **Portamento** — 0-5000 ms with Always and Legato Only modes
- **Step Pattern Editor** — Custom VSTGUI control for trance gate step editing
- **ArcKnob** — Custom VSTGUI knob control
- **FieldsetContainer** — Custom VSTGUI grouping control
- **Gain compensation** — Square-root-of-N voice scaling with soft sigmoid limiter
- **NaN/Inf safety flush** on output
