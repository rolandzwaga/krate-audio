# Changelog

All notable changes to Gradus will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.7.0] - 2026-04-07

### Added

- **MIDI Delay lane** — New 9th lane that generates delayed echo copies of arp output notes as MIDI events. Each of the 32 steps has 7 independent parameters: Active (on/off toggle), Sync (free/synced), Delay Time (10-2000ms or note value 1/64T-4/1D), Feedback (0-16 repeats), Velocity Decay (0-100%), Pitch Shift (-24 to +24 semitones per repeat), and Gate Scaling (10-200% per repeat). Delay processing sits at the end of the signal path after all other lane transforms.
- **Multi-knob grid editor** — The DELAY tab shows a scrollable grid of ArcKnob and ToggleButton controls (7 rows x N steps). Horizontal scrolling activates when step count exceeds 10, with mouse wheel support and a draggable scrollbar. Step number bar at the bottom with playhead-highlighted current step.
- **Per-step active toggle** — Each step column has an ACTIVE power button at the top. When off, the 6 parameter controls below are hidden for a cleaner UI. When on, all controls appear and echoes are generated for that step.
- **Tempo-synced delay** — Time knob snaps to 30 discrete note values (1/64T through 4/1D) when Sync is on, with note value labels in the value popup. Free mode shows milliseconds.
- **Pitch cascade echoes** — Pitch Shift parameter transposes each successive echo by N semitones, enabling stacked intervals (fifths, octaves, etc.) that audio delay cannot produce.
- **MidiNoteDelay DSP processor** — Real-time safe echo scheduler with 256-entry fixed-size pending buffer, emergency NoteOff on overflow, iterative velocity/gate decay (no std::pow on audio thread), and sample-accurate timing across process block boundaries.
- **Delay lane integrated into ArpeggiatorCore** — The delay lane is the 9th lane inside the arpeggiator engine, advancing with the same polymetric speed/swing/jitter infrastructure as the other 8 lanes. Resets correctly on retrigger.

### Changed

- **Lane tab bar expanded to 9 tabs** — New "DELAY" tab with warm gold accent color (#D4A856).
- **Audition sound defaults to OFF** — Previously defaulted to on, causing the built-in synth to play even when routed to an external instrument.
- **Chord/inversion playhead parameter defaults** — Fixed from 1.0 to 0.0 to prevent the playhead indicator from being stuck out of range on initial load.
- **Detail strip invalidates lane view on tab switch** — Ensures playhead state renders immediately when switching to a lane that received updates while hidden.

## [1.6.0] - 2026-04-05

### Added

- **Pin grid editor UI** — Completes the v1.5 Step Pinning feature. A new inline 32-cell pin toggle row appears above the pitch bars on the Pitch lane tab, column-aligned 1:1 with the pitch step bars (the row shrinks to match the lane's active length from 1 to 32 steps). Click a cell to toggle pin state for that step without dropping into host automation. Cell alignment matches the pitch lane's bar-area left margin (`kStepContentLeftMargin` = 40px) so each pin cell sits directly above its corresponding pitch bar regardless of the chosen lane length.
- **Pin row host sync** — Preset loads, DAW automation, and undo/redo all mirror back into the pin row widget without echo loops. Host-side updates use `setDirty(true)` rather than `invalid()` for thread-safe display refresh when automation arrives on a non-UI thread.
- **Markov Chain arp mode** — New 12th arp pattern mode that picks the next held note by sampling a 7×7 transition probability matrix keyed by scale degree (I/ii/iii/IV/V/vi/vii°). Held notes are mapped to degrees using the active scale type and root note; in Chromatic mode it falls back to held-note-index indexing. When the sampled degree isn't held, the nearest held degree wins. Row-stochasticity is enforced on the fly so user-edited cells don't need manual balancing.
- **5 hardcoded Markov preset matrices** — **Uniform** (flat baseline, equivalent to Random), **Jazz** (ii→V→I voice leading with circle-of-fifths turnarounds), **Minimal** (strong self-loops + ±1 step motion for meditative patterns), **Ambient** (favors wide jumps 3-5 degrees away for spacious non-stepwise motion), and **Classical** (I–IV–V–I circle-of-fifths bias). Plus a **Custom** sentinel that activates automatically whenever any matrix cell is hand-edited.
- **Editable 7×7 matrix editor** — Custom overlay view anchored in the top-left corner of the ring display, directly below the Pattern dropdown that activates Markov mode. Click-drag cells vertically like mini sliders (top = 1.0, bottom = 0.0); brightness encodes each cell's probability. Row and column labels show Roman numerals for scale degrees. Visible only when Markov mode is the active arp pattern.
- **Collapsible Markov editor** — The matrix editor can be minimized to a compact 32×32 trigger button (with a mini 3×3 dot-grid icon) via the "–" button in its top-right corner. Click the trigger button to re-expand. The expand/collapse state is session-only and lets you hide the editor when you want full visibility of the ring display while Markov mode keeps playing.

- **Per-lane speed curve** — Each lane's speed multiplier can now vary over one loop cycle via a free-form Bezier curve. A toggle button, depth knob (0-100%), and shape preset dropdown (Flat, Sine, Triangle, Saw Up/Down, Square, Exponential) appear in the pin-row area when enabled. The curve editor overlays the lane's step bars: double-click to add points, click to select, Delete/Backspace to remove, drag handles for curvature. The center+offset model means the existing speed multiplier acts as center, and the curve offsets around it scaled by depth — creating accelerando/ritardando effects within each loop cycle.
- **Dynamic version label** — The UI now renders the plugin version from `version.h` instead of a hardcoded "v1.0.0" string.

### Changed

- **Arp mode display names use single source of truth** — Deleted 5 redundant display formatters in `formatArpParam` that duplicated strings already registered with their `StringListParameter`. Adding or renaming any arp-mode entry now requires updating exactly one location (the `createDropdownParameter` call). This also fixes a latent class of bug where display scaling formulas fell out of sync with dropdown entry counts — a regression test now guards against it.
- **Playhead indicator visibility** — All 8 lane editors now use a white-lighten overlay (instead of accent-color alpha) for the current-step playhead, ensuring visibility at all bar heights and colors.

### Fixed

- **Arp mode dropdown duplicate entry** — Extending the arp mode list to 12 entries exposed a duplicate "Diverge" label caused by a stale `value * 10.0 + 0.5` scaling formula in a hand-written display formatter. Root-caused via the refactor above.
- **Missing playhead indicator on VEL/GATE lanes** — The velocity and gate lane editors were missing the playback step overlay because they relied on a base-class `isPlaying_` flag that was never set. Now all lanes use a consistent overlay that only requires a valid step index.

## [1.5.0] - 2026-04-05

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
- **Gravity arp mode** — New 11th arp pattern mode. Instead of traversing held notes in pitch order, Gravity picks the held note closest in pitch to the last played note, creating smooth stepwise motion through chords. Especially musical for voice-leading and legato-style arpeggios.
- **Note Range Mapping** — New global Range Low / Range High knobs (MIDI note floor/ceiling) and a Range Mode dropdown (Wrap / Clamp / Skip). When the arp + octave + pitch processing would push a note outside the range, it's folded back (Wrap), clamped to the limits (Clamp), or dropped entirely (Skip). Added to control row 1 next to the Scale controls — this is a true global effect on all output.
- **Step Pinning** — Per-step pin flags (32 automation parameters) plus a global Pin Note value. When a step is pinned, it outputs the fixed Pin Note instead of the arp-pattern note, bypassing pitch offset, transpose, scale quantization, and range mapping. Creates pedal tones, drone notes, and anchor points within moving patterns. The Pin Note knob lives contextually on the Pitch lane tab; per-step pin flags are editable via host automation (a dedicated pin-grid UI is planned for a later release).

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
