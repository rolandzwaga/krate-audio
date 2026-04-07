# Gradus — User Manual

## Introduction

Gradus is a standalone step arpeggiator and MIDI pattern generator. It takes held notes and transforms them into complex, evolving melodic and rhythmic patterns through nine independent polymetric lanes, each with its own step count, speed, and timing behavior.

Unlike a simple arpeggiator that cycles through held notes in order, Gradus gives you per-step control over velocity, gate length, pitch offset, chord voicing, ratcheting, articulation modifiers, conditional triggers, and MIDI echo delays — all running at independent speeds and lengths to create patterns that never quite repeat.

Gradus outputs standard MIDI events, so it can drive any instrument in your DAW. It also includes a lightweight built-in audition synth for previewing patterns without routing to an external instrument.

### Quick Start

1. Load Gradus as an instrument on a MIDI track
2. Route Gradus's MIDI output to your target instrument (or enable the built-in audition synth)
3. Hold some notes on your MIDI keyboard
4. Explore the factory presets to hear what's possible
5. Edit the step pattern in the lane editors to build your own patterns

---

## Interface Overview

The interface is organized into three zones:

- **Control Rows** (top) — Global arp parameters: pattern mode, octave range, tempo sync, rate, gate length, swing, Euclidean settings, scale quantization, and generative controls
- **Ring Display** (left) — Circular step sequencer showing all 8 lanes as concentric rings with animated playhead highlights
- **Detail Strip** (right) — Lane tab selector and linear step editor for precision editing of the selected lane

---

## Control Row 1

The first control row holds the core arp settings:

- **Pattern** — Selects the arp mode (see [Arp Modes](#arp-modes) below)
- **Octaves** — Octave range (1-4). Higher values spread the held notes across more octaves.
- **Oct Mode** — How octaves are traversed: Sequential (up through each octave), Ping-Pong (up then down), or Random
- **Latch** — Off (notes only play while held), Hold (notes latch on key press, release all to stop), or Add (each new note adds to the held set)
- **Retrigger** — Off (pattern continues), Note (restart pattern on new note), or Beat (restart on beat boundaries)
- **Scale** — Scale quantization type (16 scales from Major to Diminished). Chromatic disables quantization.
- **Root** — Root note for scale quantization (C through B)
- **Voicing** — Chord voicing mode: Close, Drop 2, Spread, or Random
- **Quantize** — Toggle input note quantization to the selected scale

---

## Control Row 2

The second control row holds timing and generative controls:

### Timing
- **Sync** — Toggle between tempo-synced and free-running mode
- **Note Value** — When synced, selects the step rate from 30 note values (1/64T through 4/1D)
- **Rate** — When free-running, sets the step rate from 0.5 to 50 Hz
- **Gate** — Gate length as a percentage of the step duration (1-200%). Values over 100% create overlapping legato notes.
- **Swing** — Global swing amount (0-75%). Skews the timing of even/odd steps.

### Euclidean
- **Euclidean** — Toggle Euclidean rhythm generation on/off
- **Hits** — Number of active hits in the Euclidean pattern (0-32)
- **Steps** — Total steps in the Euclidean pattern (2-32)
- **Rotation** — Rotational offset of the Euclidean pattern (0-31)

### Generative
- **Spice** — Blend amount for random pattern variations (0-100%)
- **Dice** — Re-roll the random variation seed
- **Humanize** — Subtle timing randomization (0-100%) for a more natural feel
- **Fill** — Toggle fill mode for live performance (activates fill-conditional steps)

---

## Arp Modes

Gradus offers 12 arpeggiator modes that determine the order in which held notes are played:

| Mode | Description |
|------|-------------|
| **Up** | Ascend through held notes from lowest to highest |
| **Down** | Descend from highest to lowest |
| **Up/Down** | Ascend then descend (inclusive — top and bottom notes play once) |
| **Down/Up** | Descend then ascend |
| **Converge** | Alternates between the lowest and highest remaining notes, working inward |
| **Diverge** | Starts from the middle and alternates outward |
| **Random** | Pick a random held note each step |
| **Walk** | Random walk: move up or down one note from the current position |
| **As Played** | Play notes in the order they were pressed |
| **Chord** | All held notes play simultaneously on each step |
| **Gravity** | Pick the held note closest in pitch to the last played note — creates smooth stepwise voice-leading |
| **Markov** | Probabilistic mode using a 7x7 transition matrix keyed by scale degree (see [Markov Chain Mode](#markov-chain-mode)) |

---

## Ring Display

The circular ring display provides a visual overview of all 9 lanes simultaneously. Steps are arranged radially around concentric rings:

- **Outer ring** — Velocity (copper) + Gate (sand)
- **Second ring** — Pitch (sage) + Modifier (rose)
- **Third ring** — Condition (steel) + Ratchet (lavender)
- **Fourth ring** — Chord (purple) + Inversion (blue)
- **Inner ring** — MIDI Delay (gold)

Each lane has its own animated playhead highlight that races around at the lane's independent speed, visually communicating the polymetric relationships between lanes.

### Direct Ring Editing

You can edit values directly on the ring:
- **Velocity, Gate, Pitch, Ratchet** — Drag radially to adjust the step value
- **Chord, Inversion, Modifier, Condition** — Click to cycle through values

### Euclidean Visualizer

When Euclidean mode is enabled, a dot pattern in the center of the ring shows the current Euclidean rhythm distribution.

---

## Detail Strip

The detail strip on the right side provides precise linear editing for one lane at a time.

### Lane Tabs

Nine color-coded tabs select the active lane:
- **VEL** (copper) — Velocity per step (0-100%)
- **GATE** (sand) — Gate length per step (0-100%)
- **PITCH** (sage) — Pitch offset per step (-12 to +12 semitones)
- **MOD** (rose) — Per-step modifiers (Active, Tie, Slide, Accent)
- **COND** (steel) — Conditional triggers per step
- **RATCH** (lavender) — Ratchet subdivisions per step (1-4)
- **CHORD** (purple) — Chord type per step (None, Dyad, Triad, 7th, 9th)
- **INV** (blue) — Chord inversion per step (Root, 1st, 2nd, 3rd)
- **DELAY** (gold) — MIDI echo delay parameters per step (see [MIDI Delay Lane](#midi-delay-lane))

### Lane Editor

Each lane editor shows a bar chart of step values. Click and drag to edit values. The lane header shows the lane name, step count (adjustable 1-32), and speed multiplier.

### Per-Lane Controls

When a lane tab is selected, contextual controls appear:

- **Swing** — Per-lane swing amount (0-75%). Each lane can swing independently.
- **Jitter** — Per-lane length jitter (0-4 steps). The lane re-rolls a random length offset each time it wraps, creating evolving non-repeating patterns.

Additional contextual controls appear for specific lanes:

- **Velocity lane** — Curve type (Linear/Exponential/Logarithmic/S-Curve) + Curve amount (0-100%)
- **Pitch lane** — Transpose (-24 to +24), Pin Note (MIDI 0-127), Range Low/High, Range Mode (Wrap/Clamp/Skip), and the Pin Grid editor
- **Ratchet lane** — Decay (0-100%) for velocity falloff across subdivisions, Shuffle for sub-step timing
- **Chord/Inversion lanes** — Strum time (0-100ms) and Strum direction (Up/Down/Random/Alternate)

### Lane Transforms

Right-click a lane (or use the lane header buttons) to access transform operations:
- **Copy / Paste** — Copy step data between lanes
- **Invert** — Flip all values
- **Shift Left / Right** — Rotate the pattern
- **Randomize** — Generate random values

### Per-Lane Speed

Each lane runs at its own clock rate, selectable from the lane header: 0.25x, 0.5x, 0.75x, 1x, 1.25x, 1.5x, 1.75x, 2x, 3x, or 4x. Combined with independent lane lengths, this creates complex polymetric and polyrhythmic patterns.

---

## Speed Curve

Each lane's speed can vary over the course of one loop cycle, creating accelerando (speeding up) and ritardando (slowing down) effects within a pattern.

### Enabling

Click the power icon toggle in the pin-row area (between the per-lane controls and the lane editor). When enabled, additional controls appear:

- **Depth** (knob, 0-100%) — How much the curve affects the speed. At 0% the curve has no effect; at 100% the full curve range is applied.
- **Preset** (dropdown) — Choose from 7 shape presets: Flat, Sine, Triangle, Saw Up, Saw Down, Square, Exponential.

### Curve Editor

When the speed curve is enabled, a semi-transparent overlay appears on the lane editor showing the curve shape. The curve represents speed offset over one loop cycle:

- **X-axis** — Position in the loop (left = start, right = end)
- **Y-axis** — Speed offset (center line = no change, above = faster, below = slower)

### Editing the Curve

- **Double-click** empty space to add a new control point
- **Click** a point to select it (shown with a golden glow)
- **Drag** a point to reposition it (endpoints are locked to x=0 and x=1)
- **Drag** the diamond-shaped bezier handles to adjust curve shape
- **Delete** or **Backspace** removes the selected point (endpoints cannot be removed)
- **Shift+drag** for fine adjustment

### How It Works

The speed curve uses a center+offset model:
- The lane's existing speed multiplier (e.g., 1x) acts as the center
- The curve offsets the speed above and below the center, scaled by the depth knob
- For example: with a 1x speed and 100% depth, a curve that ramps from top to bottom would start at 2x speed and slow down to near 0x (clamped to 0.1x minimum)

---

## Step Pinning

Step Pinning lets you lock specific steps to a fixed MIDI note, creating pedal tones, drone notes, or anchor points within moving patterns.

### Pin Grid

On the Pitch lane tab, a row of toggle cells appears above the step bars. Click a cell to pin/unpin that step. Pinned steps output the global Pin Note value instead of the arp-generated note.

### Parameters

- **Pin Note** (Pitch lane contextual knob) — The MIDI note output by pinned steps (0-127, default C4)
- **Pin Flags** — Per-step toggles (32 parameters, automatable by the host)

Pinned steps bypass pitch offset, transpose, scale quantization, and range mapping.

---

## Note Range Mapping

Constrains all output notes to a MIDI note range:

- **Range Low** — Minimum MIDI note (floor)
- **Range High** — Maximum MIDI note (ceiling)
- **Range Mode** — What happens when a note falls outside the range:
  - **Wrap** — Fold the note back into range by octave transposition
  - **Clamp** — Force the note to the nearest range boundary
  - **Skip** — Drop the note entirely (silence on that step)

---

## MIDI Delay Lane

The MIDI Delay lane is a per-step echo post-processor that generates delayed copies of arp output notes as real MIDI events. Unlike audio delay effects, MIDI delays produce new note-on/off messages — meaning the echoes play through your instrument with full voice allocation, polyphony, and per-note processing.

The delay lane sits at the end of the signal path, after all other lane transforms (velocity, gate, pitch, chords, etc.) have been applied. It operates as the 9th lane in the polymetric engine, with its own independent step count, speed multiplier, swing, and jitter.

### Grid Editor

Selecting the **DELAY** tab opens a multi-knob grid editor instead of the bar-chart editor used by other lanes. Each step column contains 7 controls stacked vertically:

| Row | Control | Range | Description |
|-----|---------|-------|-------------|
| **Active** | Toggle button | On/Off | Enables or disables echo generation for this step. When off, the step's parameter controls are hidden for a cleaner view. |
| **Sync** | Toggle button | Free/Synced | Switches the delay time between millisecond values and tempo-synced note values. |
| **Time** | Arc knob | 10-2000 ms (free) or 1/64T-4/1D (synced) | The delay between the original note and the first echo. When synced, snaps to 30 discrete note values. |
| **Feedback** | Arc knob | 0-16 repeats | Number of echo repetitions. Each repeat applies the velocity decay, pitch shift, and gate scaling. |
| **Vel Decay** | Arc knob | 0-100% | Velocity reduction per echo repeat. 100% means each echo is silent; 0% means all echoes play at full velocity. |
| **Pitch** | Arc knob | -24 to +24 semitones | Pitch transposition applied cumulatively per echo. For example, +7 semitones produces echoes stacked in fifths. |
| **Gate** | Arc knob | 10-200% | Gate length scaling per echo repeat. Values below 100% progressively shorten echoes; values above 100% lengthen them. |

### Scrolling

When the lane step count exceeds 10, the grid scrolls horizontally. Use the mouse wheel over the grid area to scroll, or drag the scrollbar at the bottom. A step number bar runs along the bottom edge with the current playhead step highlighted.

### Per-Step Active Toggle

The active toggle at the top of each column controls whether echoes are generated for that step. Inactive steps pass notes through unchanged. When a step is toggled off, the 6 parameter controls below it are hidden, leaving just the muted toggle button for a cleaner layout.

### How Echo Processing Works

1. The arpeggiator generates an output note (with velocity, gate, pitch, and chord transforms already applied)
2. The delay lane checks the current step's active flag
3. If active, it schedules echo copies at intervals of the step's delay time
4. Each successive echo applies: velocity *= (1 - decay), pitch += pitch shift, gate *= gate scaling
5. Echoes continue until the feedback count is exhausted or velocity drops below 1

The echo scheduler is sample-accurate across process block boundaries. If the echo buffer fills (256 pending echoes), emergency note-off events are sent to prevent stuck notes.

### Tempo-Synced Delays

When Sync is enabled on a step, the Time knob snaps to 30 discrete note values (1/64T through 4/1D). The delay time is calculated from the host's current tempo. This keeps echoes rhythmically locked to the beat regardless of tempo changes.

---

## Markov Chain Mode

When the arp mode is set to **Markov**, note selection is driven by a probabilistic transition matrix instead of a deterministic pattern.

### How It Works

The 7x7 matrix represents transition probabilities between scale degrees (I, ii, iii, IV, V, vi, vii). Each row determines the probability of moving to each degree FROM the current degree. Held notes are mapped to the nearest scale degree based on the active scale type and root note.

### Matrix Editor

When Markov mode is active, an overlay appears on the ring display:
- **7x7 grid** — Click-drag cells vertically to adjust probability (top = 1.0, bottom = 0.0). Brightness encodes probability.
- **Row/column labels** — Roman numerals (I, ii, iii, IV, V, vi, vii)
- **Preset dropdown** — Choose from 5 presets:
  - **Uniform** — Equal probability (equivalent to Random mode)
  - **Jazz** — ii-V-I voice leading with circle-of-fifths turnarounds
  - **Minimal** — Strong self-loops + stepwise motion for meditative patterns
  - **Ambient** — Favors wide jumps (3-5 degrees) for spacious motion
  - **Classical** — I-IV-V-I circle-of-fifths bias
- **Custom** — Activates automatically when any cell is hand-edited
- **Collapse button** ("–") — Minimizes the editor to a 32x32 trigger button

Rows are automatically normalized at sample time, so you don't need to manually balance probabilities.

---

## Audition Synth

Gradus includes a lightweight built-in synthesizer for previewing patterns without routing to an external instrument. The audition synth defaults to off and is session-only — its settings are not saved in presets.

- **Enable** — Toggle the audition synth on/off
- **Volume** — Output level (0-100%)
- **Waveform** — Sine, Sawtooth, or Square
- **Decay** — Envelope decay time (10-2000 ms)

The audition synth is monophonic with a simple linear envelope. For polyphonic playback or richer sounds, route Gradus's MIDI output to your preferred instrument plugin.

---

## Preset Browser

Click the preset name in the header to open the preset browser. Presets are organized into 8 categories:

- **Classic** — Traditional arp patterns
- **Acid** — Acid-style sequences with slides and accents
- **Euclidean** — Mathematically distributed rhythmic patterns
- **Polymetric** — Complex polyrhythmic combinations using different lane lengths and speeds
- **Generative** — Evolving patterns using jitter, spice, and conditional triggers
- **Performance** — Patterns designed for live use with fill mode
- **Chords** — Chord-focused patterns with voicings and strums
- **Advanced** — Complex multi-lane configurations

### Saving Presets

Click the Save button to save the current settings as a user preset. Choose a name and category.

### Preset Sharing with Ruinae

Gradus shares arpeggiator parameter IDs (3000-3387) with Ruinae's arpeggiator section. Presets saved in one plugin can be loaded in the other for cross-plugin pattern exchange.

---

## MIDI Output

Gradus outputs arpeggiated notes as standard VST3 MIDI events. To use Gradus as a pattern source for another instrument:

1. Load Gradus on an instrument track
2. Create a MIDI routing from Gradus's output to your target instrument (method varies by DAW)
3. Hold notes on Gradus's input — the generated pattern drives your instrument

Note-On events include velocity from the velocity lane. Note-Off events are timed by the gate lane. Slide modifiers generate overlapping notes for portamento effects on instruments that support it. When the MIDI Delay lane is active, echo copies are output as additional MIDI events with their own velocity, pitch, and gate values.

---

## Tips and Techniques

### Creating Polymetric Patterns
Set different lane lengths (e.g., Velocity = 7 steps, Gate = 5 steps, Pitch = 3 steps). The different cycle lengths create a pattern that takes many bars to repeat — or never truly repeats if the lengths are coprime.

### Accelerando / Ritardando
Enable the Speed Curve on a lane and select the "Saw Down" preset with 100% depth. The lane will start fast and progressively slow down each cycle, creating a natural ritardando effect.

### Evolving Patterns with Jitter
Set per-lane jitter to 2-3 steps on several lanes. Each lane will subtly shift its effective length on every wrap, causing the pattern to continuously evolve without ever settling into a fixed loop.

### Anchoring with Pin Notes
Pin a few key steps on the Pitch lane to a root or fifth note. The unpinned steps follow the arp pattern while pinned steps provide a harmonic anchor — great for creating ostinato figures with moving voices.

### Probabilistic Melodies with Markov
Select the Jazz Markov preset and hold a chord. The arp will follow common jazz voice-leading patterns (ii-V-I), creating melodies that feel composed rather than random. Edit individual cells to bias specific transitions.

### Bouncing Ball Ratchets
Set ratchet to 4 subdivisions on selected steps and increase the Decay parameter. The rapid-fire notes will fade out naturally, creating a bouncing-ball effect.

### Harmonic Echo Cascades
On the MIDI Delay lane, set Pitch to +7 semitones and Feedback to 3-4. Each echo stacks a fifth above the previous one, creating ascending harmonic cascades that audio delays cannot produce. Try +12 for octave cascades.

### Rhythmic Delay Patterns
Set different steps on the MIDI Delay lane to different synced time values (e.g., step 1 = 1/8, step 2 = 1/8D, step 3 = 1/4T). As the delay lane cycles through steps at its own speed, the echo rhythm constantly shifts, creating complex polyrhythmic textures.

### Fading Canon
Enable the MIDI Delay lane with Sync on, Time set to 1/4 note, Feedback at 4, and Vel Decay around 20%. The arp pattern plays as a self-chasing round with each voice entering one beat later and progressively softer — a MIDI-generated canon effect.

---

## System Requirements

- **Formats** — VST3 (Windows, macOS, Linux), Audio Unit v2/v3 (macOS)
- **Minimum** — Any DAW that supports VST3 instruments with MIDI output
- **Validated** — Passes pluginval strictness level 5 on all platforms
