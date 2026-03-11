# Changelog

All notable changes to Ruinae will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.9.9] - 2026-03-11

### Fixed

- **Dattorro plate reverb bandwidth filter** — Filter coefficient derivation was inverted, producing a 3.8 Hz lowpass instead of the near-transparent filter specified in the Dattorro paper. The reverb now has proper frequency response and much richer sound
- **Plate reverb decay range** — Recalibrated roomSize-to-decay mapping from [0.75, 0.9995] to [0.50, 0.90] to give musically useful RT60 range (~2-12 seconds) with the corrected bandwidth filter. Previously roomSize=100% produced a 42-minute tail
- **Plate reverb output gain** — Reduced from 3.0 to 0.6 to compensate for correct bandwidth filter energy levels
- **Tank modulation interpolation** — Upgraded DD1 modulated allpass reads from linear to cubic Hermite interpolation, reducing HF loss in the recirculating tank
- **LFO amplitude drift on rate changes** — Gordon-Smith phasor now renormalized to the unit circle when modulation rate changes, preventing amplitude drift
- **Input diffusion stale buffers** — Diffusion allpasses are now always processed (even at diffusion=0) to prevent stale audio bursts when re-enabling diffusion
- **DD2 integer delay read** — Fixed unnecessary linear interpolation on integer-length delays
- **Output tap bounds checking** — Tap positions are now clamped against section lengths in prepare() to prevent buffer overruns at unusual sample rates
- **Per-sample damping overhead** — Damping filter coefficient now cached to avoid redundant exp() calls when cutoff hasn't changed

## [0.9.8] - 2026-03-08

### Added

- **Arpeggiator Chord Lane** — New per-step chord type lane (None/Dyad/Triad/7th/9th) that generates chords by stacking diatonic scale degrees using the arp's scale harmonizer. Transforms the arpeggiator from a single-note sequencer into a harmonic sequencer
- **Arpeggiator Inversion Lane** — Per-step chord inversion control (Root/1st/2nd/3rd) for voice-leading variety across the pattern
- **Global Voicing Mode** — Close, Drop-2, Spread, and Random voicing options that control how chord notes are distributed across registers
- **5 new "Arp Chords" factory presets** — Diatonic Triads, Minor 7th Pulse, Chord Cascade, Spread Ninths, Stab Machine

### Fixed

- **Condition lane popup menu positioning** — COptionMenu popups on condition, chord, and inversion lanes now appear at the clicked step instead of at an incorrect offset

## [0.9.7] - 2026-03-08

### Added

- **Arpeggiator Pitch as global modulation source** — Arp pitch (last played arp note) is now available as a modulation source in the global mod matrix, enabling pitch-following effects like filter tracking or delay time modulation

### Changed

- **Enriched factory presets** — Deeper modulation routing across factory presets for more expressive out-of-the-box sound design

### Fixed

- **Voice mod routes disappearing on click** — Clicking any control in a voice modulation row caused all voice routes to vanish. Root cause: VST3's synchronous `sendMessage→notify` chain caused re-entrant `VoiceModRouteState` responses to overwrite the grid during callback execution
- **Voice mod route delete button not working** — The remove button appeared to do nothing because the processor's synchronous response overwrote the grid mid-loop, undoing the shift-up operation
- **Voice route interactions corrupting global mod parameters** — Voice route detail controls (curve, smooth, scale, bypass, amount) were incorrectly calling `beginEdit`/`performEdit`/`endEdit` on global mod matrix parameter IDs. Voice routes now communicate exclusively via IMessage

## [0.9.6] - 2026-03-07

### Fixed

- **ADSR envelope edits not persisting when switching tabs** — Dragging envelope control points only called `performEdit` (sending values to the host/processor) but did not call `setParamNormalized` to update the controller's internal parameter state. Switching between envelope tabs and back would show stale pre-edit values. Now both calls are made, matching the pattern used by all other custom editors.

## [0.9.5] - 2026-03-05

### Added

- **In-plugin update checker** — Automatically checks for new versions on editor open (24h cooldown) and shows a non-intrusive banner with download link when an update is available
- **"Check for Updates" button** — Manual version check in the settings panel
- **Version label** — Dynamic version display in the top bar
- Version dismiss functionality for update notifications
- **Category dropdown in quick-save preset dialog** — Preset save dialog now includes a category selector

### Changed

- **Removed dynamic voice-count gain compensation** — Eliminated phantom volume boost caused by gain scaling adjusting mid-note when voices overlapped

### Fixed

- **Harmonizer silent after preset load** — Use-after-free crash on preset switch fixed by nulling dangling view pointers
- **Global LFO retrigger not firing on note-on** — LFO phase was not resetting when retrigger was enabled
- **Near-silent chaos presets** — Chaos oscillator presets producing barely audible output due to incorrect gain staging
- **VSTGUI crash during rapid preset switching** — Multiple use-after-free and null-pointer issues in view lifecycle during bulk parameter loads
- **Preset loading race conditions** — RTTransferT lock-free transfer prevents data races on voice routes during preset switches
- **Spectral distortion drive scaling and output buffer clobbering** — Drive parameter had incorrect range mapping; output buffer was being overwritten
- **Inactive spectral distortion controls not dimmed** — Curve dropdown and bits controls now gray out based on spectral mode selection
- **Bass presets missing from preset browser** — Category filtering excluded bass subcategory
- **Euclidean Bells preset** — Opened filter and initialized all partials to full amplitude

## [0.9.4] - 2026-03-02

### Added

- **ARP Save Preset button** — "Save" button on the arpeggiator tab opens the ARP preset browser with the save dialog shown, defaulting to "Arp Classic" subcategory
- **Shared OutlineBrowserButton** — Unified outline button component for preset browser and save dialog, replacing duplicated CTextButton/DialogButton classes

### Changed

- **Distinct FX tab section colors** — Each effect section (Phaser, Delay, Harmonizer, Reverb) now has its own accent color
- **MOD tab layout** — Redistributed controls and fixed mod source dropdown display
- **Preset browser filtering** — ARP presets hidden from synth preset browser via tab label filtering

### Fixed

- **CI AUv3 verify step** — No longer fails when only a subset of plugins changed

## [0.9.3] - 2026-03-02

### Added

- **30 new synth presets** across 6 categories: Basses (5), Leads (5), Pads (5), Rhythmic (5), Textures (5), Experimental (5)
- **Crash-proof preset loading** — RTTransferT lock-free triple-buffer safely transfers preset state to the audio thread, preventing data races on voiceRoutes during preset switches

### Fixed

- **Chaos oscillator producing inaudible DC output** — Lorenz, Rossler, and Chua attractors were evolving at sub-audio rates (0.3-10 Hz instead of 440 Hz) due to incorrect baseDt calibration. Recalibrated all three attractors and adjusted gain compensation. The Chaos Wind preset and all chaos-type oscillator presets now produce proper audible audio.

## [0.9.2] - 2026-03-01

### Added

- **Ring Modulator distortion type** — New per-voice distortion with internal sine oscillator, waveform selection (Sine, Triangle, Square, Saw), fixed/ratio frequency modes, depth control, and stereo spread
- **Ring Modulator DSP processor** (KrateDSP Layer 2) with DC offset removal

### Changed

- **UI layout: taller Row 1 and Row 2** — Added 25px height to each of the first two rows on the Sound tab for better visual spacing
- **Knob label positioning** — Labels beneath ArcKnobs in oscillator, filter, and distortion sub-panels now render below the knob rather than overlapping it
- **Sub-panel heights increased** — Oscillator type-specific panels (32→48px), filter/distortion sub-panels (88→113px) to use the added room

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
