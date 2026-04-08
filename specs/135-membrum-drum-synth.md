# Spec 135 — Membrum: Synthesized Drum Machine

**Status:** Phase 1 Complete (see `specs/136-membrum-phase1-scaffold/`)  
**Plugin:** Membrum  
**Type:** Instrument (`aumu`)  
**Location:** `plugins/membrum/`

## Overview

Membrum is a synthesis-only drum machine plugin. No samples — every sound is generated in real time using modal synthesis, physical modeling, and spectral techniques. The core engine, **Corpus**, uses SIMD-accelerated modal synthesis to model struck and excited resonant bodies, producing drum sounds that range from physically accurate to deliberately unnatural.

The name is Latin for "limb/member," pairing with the engine name Corpus ("body").

## Design Philosophy

Most synth drum machines offer oscillator + noise + filter per voice. Membrum goes deeper: each voice models the physics of an **exciter striking a resonant body**. A mallet hits a membrane. A stick strikes a plate. A brush scrapes a shell. The two-stage model (exciter → body) is intuitive, expressive, and produces sounds with the complexity and variation of real physical interactions.

Then it goes further: the "Unnatural Zone" parameters let you push beyond physics into territory where drums become tonal instruments, evolving textures, or crystalline artifacts.

## Architecture

### Pad Layout

- **32 pads**, mapped to GM drum map (C1/MIDI 36 through G#3/MIDI 67)
- No built-in sequencer — relies on external MIDI (pairs well with Gradus)
- **Separate outputs** per pad (output routing scheme TBD)
- **Kit presets** (all 32 pads) and **per-pad presets**

### Voice Management

- **Configurable max polyphony** (default: 8 voices, range: 4–16)
- Voice stealing policy: configurable (oldest / quietest / priority-based)
- 32 pads provide choice within a bank; not all 32 are expected to sound simultaneously
- **Choke groups**: 16 groups with per-pad priority-based direction control

#### Choke Group Design

Each pad has two choke parameters:
- **Choke Group**: None, 1–16
- **Choke Priority**: 0–31 (higher = dominant)

When a pad triggers, it silences all currently sounding pads in the same group with **equal or lower priority**. Pads with higher priority are untouched.

| Scenario | Setup | Behavior |
|----------|-------|----------|
| Mutual choke (classic) | Open hat: Group 1, Priority 0. Closed hat: Group 1, Priority 0. | Either kills the other |
| One-way choke | Open hat: Group 1, Priority 0. Closed hat: Group 1, Priority 1. | Closed kills open. Open does NOT kill closed. |
| Multi-tier hat | Pedal (P3) > Closed (P2) > Half-open (P1) > Open (P0), all Group 1 | Each level silences everything below it |
| Conga family | Muted (P1) > Open (P0) > Slap (P1), Group 2 | Muted and slap kill open. Muted and slap kill each other. Open doesn't kill anything. |

Implementation: on note-on, iterate active voices in the same choke group. If `triggering_pad.priority >= sounding_pad.priority`, send fast release envelope to the sounding voice. Priority comparison is `>=` so equal-priority pads mutually choke (the standard case).

### Per-Voice Signal Path

```
[Exciter] → [Corpus Body] → [Tone Shaper] → [Amp Envelope] → [Output]
                  ↑                                              │
             [Feedback]←─────────────────────────────────────────┘
                  ↑
         [Cross-Pad Coupling]
```

## Exciter Types

The exciter is what triggers the body. Each type responds to velocity — harder hits produce brighter, more complex excitation, not just louder.

| Exciter      | Description                                | Key Parameters         |
|--------------|--------------------------------------------|------------------------|
| **Impulse**  | Pure click, classic analog drum trigger     | Width, brightness      |
| **Noise Burst** | Shaped noise burst for snares/hats     | Color (LP/HP/BP), decay |
| **Mallet**   | Modeled soft-to-hard striker               | Hardness, mass         |
| **Friction** | Bowed/rubbed excitation (brushes, scraped metal) | Pressure, speed  |
| **FM Impulse** | Short FM burst for metallic transients   | Ratio, depth, decay    |
| **Feedback** | Self-excitation from the body's own output | Drive, character       |

### Velocity Behavior

Velocity does NOT simply scale amplitude. This is grounded in physics: the nonlinear power-law contact force `F = K * delta^alpha` means harder hits compress the mallet felt into stiffer layers, producing shorter contact pulses with wider bandwidth. Measured data shows a **10x bandwidth increase** across the dynamic range (turnover frequency: ~100 Hz at pp, ~1000 Hz at ff).

Map MIDI velocity to **two** parameters simultaneously:
1. **Amplitude** (obvious)
2. **Excitation spectral content** (the critical timbral dimension)

Per exciter type:
- **Impulse**: harder = shorter pulse width → wider bandwidth (f_turnover ~ 1/(pi * tau))
- **Mallet**: harder = higher alpha exponent → snappier contact, brighter spectrum. Contact duration: ~8ms (soft) to ~1ms (hard stick)
- **Noise Burst**: harder = higher lowpass cutoff on noise (200 Hz soft → 5000+ Hz hard)
- **Friction**: harder = more bow pressure → richer overtone content, easier self-oscillation

## Corpus Engine — Modal Synthesis Core

The heart of Membrum. Each body model defines a set of **modal frequencies** (partials) with individual amplitudes and decay rates derived from the physics of the modeled object. Each mode is implemented as a two-pole resonator (Gordon-Smith coupled-form) with frequency-dependent damping following the Chaigne-Lambourg model.

### Body Models

| Body          | Physical Analog            | Mode Pattern        | Character                        |
|---------------|----------------------------|---------------------|----------------------------------|
| **Membrane**  | Circular drum head         | Bessel function zeros | Kicks, toms, congas, tablas     |
| **Plate**     | Rectangular metal/wood plate | f ~ m^2+n^2 (stiffness) | Cowbell, woodblock, claves   |
| **Shell**     | Cylindrical resonator      | Mixed axial/radial  | Deep toms, tubular bells         |
| **String**    | Struck/plucked string      | Karplus-Strong variant | Metallic pings, plucks         |
| **Bell**      | Inharmonic rigid body      | Chladni's law f ~ (m+b)^p | Bells, chimes, gamelan      |
| **Noise Body**| Hybrid: sparse modes + filtered noise | ~21 modes/kHz for plates | Hats, cymbals, shakers |

### Body Parameters

| Parameter          | Description                                           | Range        |
|--------------------|-------------------------------------------------------|--------------|
| **Material**       | Controls damping coefficients b1/b3 (see Physics Reference). Soft/damped (wood, rubber) → hard/resonant (metal, glass) | Continuous |
| **Size**           | Scales fundamental frequency and all mode spacings    | Continuous   |
| **Tension**        | Inharmonicity — stretches/compresses mode ratios relative to the body model's physical ratios | Continuous |
| **Damping**        | Overall decay rate (velocity-sensitive). Maps to b1 in Chaigne model | Continuous   |
| **Strike Position**| Scales per-mode amplitude: A_mn ~ J_m(j_mn * r/a) for membranes, sin(m*pi*x/L) for bars | Continuous |
| **Air Coupling**   | For Membrane body: blends between ideal membrane ratios and timpani-like near-harmonic ratios. 0 = pure Bessel, 1 = air-coupled | Continuous |
| **Nonlinear Pitch**| Velocity-dependent pitch rise from tension modulation. Real membranes go up to 65 cents on hard hits | 0–100 cents |

### Double Membrane Coupling (Batter + Resonant Head)

Available for **Membrane** and **Shell** bodies. Models the physical interaction between the struck batter head, the enclosed air cavity, and the resonant (bottom) head. The air cavity acts as a spring coupling the two membranes — batter displacement compresses the air, pushing the resonant head outward. This splits each mode into symmetric/antisymmetric pairs and creates the characteristic two-headed drum timbre with energy transfer between heads.

**Implementation:** Coupled modal synthesis. Each membrane is a bank of modes; the air cavity couples corresponding mode indices via a spring matrix. Coupling stiffness is proportional to `ρc²/V` where V = cavity volume (depth × area). Each coupled mode pair becomes a 2-DOF oscillator: two second-order resonators with cross-coupling terms (~2 extra muls + 2 adds per mode pair per sample).

**CPU cost:** ~1.6–1.8x a single membrane, NOT 2x. The coupling matrix is sparse (same-index modes couple strongly, others weakly). Reduced mode count for resonant head (8 modes vs 16 for batter) keeps it practical.

| Parameter              | Description                                                      | Range      |
|------------------------|------------------------------------------------------------------|------------|
| **Resonant Head**      | Enable/disable second membrane                                   | On/Off     |
| **Cavity Depth**       | Air coupling strength. Shallow = strong coupling, wide mode splitting, more "bonk." Deep = weak coupling, closer to independent heads | Continuous |
| **Resonant Tension**   | Tension ratio of resonant head vs batter. Equal = clear pitch; unequal = inharmonic, complex tones. The single most musically important coupling parameter | Continuous |
| **Resonant Damping**   | Resonant head decay rate. Heavy = dry attack, light = singing resonance and body | Continuous |

**Strike excitation drives batter head only.** Output is taken from batter head (direct/attack character) with configurable bleed from resonant head (body/sustain character).

**Sources:** Rossing "Science of Percussion Instruments" (2000); Bilbao "Numerical Sound Synthesis" (2009, Ch. 11); Kapur et al. "Digital Tabla" (ICMC 2004)

### Partial Configuration

- **Default: 16 partials** per voice — validated as the perceptual sweet spot for membranes (SMC 2019 study: 20-30 modes indistinguishable from recordings)
- 8 modes: minimum viable, acceptable for lo-fi/electronic sounds
- 16 modes: good quality, recognizable drum character
- 20-32 modes: high quality, diminishing returns beyond 30 for membranes
- Cymbals/metallic: need hybrid approach (20-40 modes + filtered noise) due to ~400 modes below 20 kHz
- Max budget at default: 8 voices × 16 partials = **128 simultaneous partials**

## The Unnatural Zone

Parameters that push beyond physical reality. These are core to Membrum's identity, not extras.

| Parameter         | Description                                                        |
|-------------------|--------------------------------------------------------------------|
| **Mode Stretch**  | Compress or spread modal frequencies. 1.0 = physical. Below = clustered/gong-like (gamelan ombak territory). Above = spread/crystalline. Already exists as `stretch` in ModalResonatorBank. |
| **Mode Inject**   | Add synthetic partial series into the body's mode set. Options: harmonic (integer ratios), FM-derived (Chowning ratios like 1:1.4, 1:sqrt(2), 1:phi), or randomized. Uses HarmonicOscillatorBank. |
| **Decay Skew**    | Inverts the Chaigne b3 coefficient sign: normally high modes decay faster (positive b3). Negative b3 = fundamental dies while shimmer sustains. Continuous control from -1 (inverted) through 0 (flat) to +1 (natural). |
| **Material Morph**| Per-hit automation envelope driving the material parameter (b1/b3 coefficients) over the duration of a single hit — evolving timbre from strike to tail. E.g., metal attack → wood decay. |
| **Nonlinear Amount** | Controls the strength of von Karman mode coupling (see below). 0 = pure linear (bell/chime). 1 = full coupling (cymbal crash). Sets the *potential* for nonlinearity; velocity determines how much activates per hit. | 
| **Cascade Speed** | How fast energy transfers from low modes to high modes. Low = slow gong-like bloom (50–100ms buildup). High = instant cymbal splash. |

## Tone Shaper

Post-body processing per voice:

| Stage              | Description                                    |
|--------------------|------------------------------------------------|
| **Filter**         | LP / HP / BP with dedicated envelope (SVF)     |
| **Drive**          | Soft saturation to hard clipping (ADAA for alias-free processing) |
| **Wavefolder**     | Metallic edge and harmonic complexity           |
| **Pitch Envelope** | Fast exponential pitch sweep. For kicks: f_start=160-500Hz → f_end=40-80Hz over 5-200ms. This is the 808 bridged-T effect: amplitude-dependent frequency shift from diode nonlinearity in the original circuit. |

### Snare Wire Modeling

A special tone shaper stage available on Membrane bodies:

- **Snare amount**: controls mix of snare wire buzz
- Implementation: amplitude-modulated bandpass-filtered noise (1-8 kHz), triggered when membrane displacement exceeds threshold
- **Snare delay**: 0.5-2ms delay between membrane excitation and snare onset (models air cavity propagation)
- **Snare damping**: controls how quickly the wires settle
- Comb filter on noise (delay = snare wire length) for metallic resonance character
- Based on Bilbao's penalty method collision model, simplified for real-time use

## Cross-Pad Coupling (Sympathetic Resonance)

A signature feature: pads can excite each other based on proximity and tuning relationships. Physically modeled after the air-coupled acoustic interaction between instruments in a drum kit.

- **Coupling matrix**: per-pair gain coefficients (0.0 to ~0.05 typical). Each drum's output is bandpass-filtered around the receiving drum's modal frequencies before being fed in as excitation
- **Frequency selectivity**: coupling is strongest when the exciting frequency matches a natural mode of the receiving instrument — this is why tuning toms to different intervals from the snare reduces buzz in real kits
- **Build-up time**: sympathetic response has slower attack than direct excitation (physically accurate)
- Real-world analog: hitting the kick makes snare wires buzz, toms resonate sympathetically
- Configurable intensity (global and per-pair)
- CPU consideration: coupling only active between pads that are currently sounding
- Uses existing SympatheticResonanceSIMD engine from KrateDSP
- Note: IK Multimedia's MODO Drum (the main commercial competitor in this space) exposes this as "Tom Buzz" and "Snare Buzz" knobs — we go further with a full matrix

## Pad Templates

Starting-point configurations for new pads, with physically informed defaults:

| Template   | Exciter Default | Body Default | Key Settings | Character |
|------------|----------------|--------------|-------------|-----------|
| **Kick**   | Impulse        | Membrane     | Pitch env: 160→50Hz/20ms, air coupling 0.3, b1=5, b3=1e-7 | Deep, punchy |
| **Snare**  | Noise Burst    | Membrane     | Snare wires ON, 16 modes, noise cutoff 6kHz | Bright noise + body |
| **Tom**    | Mallet         | Membrane     | Air coupling 0.5 (near-harmonic), pitch env subtle | Pitched, resonant |
| **Hat**    | Noise Burst    | Noise Body   | Hybrid: 20 modes + HP noise at 6kHz, short decay | Metallic sizzle |
| **Cymbal** | Noise Burst    | Noise Body   | Hybrid: 30 modes + broadband noise, long decay, nonlinear coupling | Shimmering wash |
| **Perc**   | Mallet         | Plate        | Square plate ratios, b1=15, medium decay | Cowbell, block, clave |
| **Tonal**  | Mallet         | Bell         | Church bell ratios, b1=0.5, b3=1e-11, long decay | Bells, chimes, gamelan |
| **808**    | Impulse        | Membrane     | Pitch env: 160→50Hz/200ms, high nonlinearity, sine-only (mode 0,1) | Classic electronic |
| **FX**     | FM Impulse     | Shell        | Unnatural zone active, mode stretch=1.5, decay skew=-0.5 | Experimental |

## DSP Implementation Notes

### KrateDSP Integration

The Corpus modal bank will live in the shared DSP library:

- **Layer 2 (processors)** or **Layer 3 (systems)** — TBD based on complexity
- Uses **Gordon-Smith magic circle oscillator** per partial (proven ~30% speedup over sin() in particle oscillator work)
- **Google Highway** SIMD for parallel partial computation
  - SSE: 4 partials per instruction
  - AVX2: 8 partials per instruction (full 16-partial voice in 2 iterations)
  - NEON: 4 partials per instruction (ARM/Apple Silicon)

### Gordon-Smith Per Partial

```
epsilon = 2 * sin(pi * freq / sampleRate)  // precomputed on note-on
s_new = s + epsilon * c
c_new = c - epsilon * s_new                // amplitude-stable (det = 1)
output += amplitude * s_new
```

### Performance Targets

- 8 voices × 16 partials = 128 partials at 44.1 kHz
- Target: < 2% CPU on modern hardware (single core)
- Profiling required to validate; adjust max voices/partials if needed

### Real-Time Safety

Standard Krate rules apply:
- No allocations, locks, exceptions, or I/O on audio thread
- All pad/kit configuration changes via lock-free messaging
- Voice pool pre-allocated at max polyphony

## MIDI

- Standard GM drum map: pad 1 = C1 (MIDI 36) through pad 32 = G#3 (MIDI 67)
- Velocity-sensitive (drives exciter character + amplitude)
- No built-in sequencer

## Microtuning

Full microtuning support for tonal body models (Bell, String, tonal Membrane/Plate). Enables gamelan tunings, just intonation, historical temperaments, and custom scales across the pad map.

### Scala Format Support

- **.scl** (scale definition): List of intervals in cents or ratios defining the scale degrees. Standard format created by Manuel Op de Coul, supported by virtually all synths with microtuning.
- **.kbm** (keyboard mapping): Maps scale degrees to MIDI notes, defines reference note/frequency, and handles unmapped keys. Essential for mapping non-12-tone scales to the 32-pad MIDI range.

### .scl File Format

```
! comment line
Scale name
 N          (number of scale degrees, excluding root)
!
 100.0      (cents) or 3/2 (ratio) per line
 200.0
 ...
 2/1        (octave or period)
```

Ratios are exact (e.g., `3/2` = 701.955 cents). Cents are decimal. The root (1/1 = 0 cents) is implicit and not listed.

### .kbm File Format

```
N            size of map (0 = linear mapping)
first_key    first MIDI note to retune
last_key     last MIDI note to retune
middle_note  MIDI note where reference frequency is mapped
ref_note     MIDI note for reference frequency
ref_freq     reference frequency in Hz (e.g., 440.0)
octave_degree scale degree for formal octave (period)
mapping...   scale degree per key (x = unmapped)
```

### Implementation

- **TuningTable** class in shared DSP library (`dsp/include/krate/dsp/core/tuning_table.h`): pre-computed 128-entry MIDI note → frequency lookup table
- Load .scl + .kbm on the UI thread, compute the full frequency table, send to audio thread via lock-free message (single pointer swap)
- Default: 12-TET at A4 = 440 Hz (backward compatible with existing `midiNoteToFrequency()`)
- **A4 reference frequency** parameter (400–480 Hz) applies on top of any loaded tuning
- Per-pad: each pad's MIDI note maps through the tuning table to get its base frequency, then Size/Tension parameters modify from there
- Kit presets store the tuning (.scl/.kbm file references or inline table data)

### Preset Tunings (Built-In)

| Tuning | Description |
|--------|-------------|
| 12-TET | Standard equal temperament (default) |
| Just Intonation (5-limit) | Pure intervals: 9/8, 5/4, 4/3, 3/2, 5/3, 15/8, 2/1 |
| Pythagorean | Based on pure fifths (3/2): brighter than 12-TET |
| Pelog | Javanese gamelan 7-tone scale (~125, 263, 400, 538, 675, 813, 950 cents) |
| Slendro | Javanese gamelan 5-tone scale (~240, 480, 720, 960, 1200 cents) |
| Harmonic Series | Partials 8–16 of the harmonic series (natural overtone tuning) |
| Wendy Carlos Alpha | 78 cents/step, ~15.385 steps/octave |
| Bohlen-Pierce | 146.3 cents/step, tritave (3/1) period |

These ship as embedded .scl files. Users can load additional .scl/.kbm files from disk.

## Output Routing

Following the industry standard established by Battery, Maschine, Superior Drummer, and others:

- **16 stereo output pairs** declared at plugin load (Main + Aux 1–15)
- **Main Out (stereo)**: default destination for all pads. Per-pad pan position applied here.
- **Aux 1–15 (stereo)**: separate outputs for individual pad routing to DAW mixer channels
- All outputs declared statically in the VST3 bus layout — no dynamic activation
- Unused outputs carry silence; the host decides whether to create mixer channels for them
- Each pad has an **Output Assignment** parameter: Main (default), Aux 1–15
- Pads routed to an aux output are **removed from the main mix** (exclusive routing, not parallel send)
- Internal signal is mono per voice; stereo bus carries the mono signal centered (leaves headroom for DAW stereo processing)

## UI

### Overall Layout — Three Columns

```
┌─────────────────────────────────────────────────────────────────────────┐
│  [Kit Browser ▼]  [Save Kit]  [Tuning ▼]           Kit: "Factory 01"  │
├───────────────┬──────────────────────────────┬──────────────────────────┤
│               │                              │                          │
│   PAD GRID    │       PAD EDITOR             │     KIT-LEVEL            │
│   (4×8)       │   (tabbed, per-pad)          │     CONTROLS             │
│               │                              │                          │
│  ┌───┬───┬───┐│  [Exciter][Body][Unnat.]     │  Cross-Pad Coupling      │
│  │ K │ S │ H ││  [Tone  ][Amp ][Route ]     │    Intensity             │
│  ├───┼───┼───┤│                              │    Matrix view           │
│  │ T │ T │ C ││  ┌─────────────────────┐     │                          │
│  ├───┼───┼───┤│  │                     │     │  Master Volume           │
│  │ P │ P │ F ││  │  (tab content)      │     │  Master Tune             │
│  ├───┼───┼───┤│  │                     │     │                          │
│  │   │   │   ││  │                     │     │  Voice Polyphony         │
│  └───┴───┴───┘│  └─────────────────────┘     │  Stealing Policy         │
│               │                              │                          │
└───────────────┴──────────────────────────────┴──────────────────────────┘
```

- **Left column**: 4×8 pad grid. Click a pad to select it → editor updates.
- **Center column**: Tabbed pad editor for the selected pad. Always visible, shows parameters for whichever pad is selected.
- **Right column**: Kit-level controls (cross-pad coupling, master settings, voice management).

### Pad Grid (Left Column)

Each pad displays:
- **Note name** (e.g., C1, D1) — top-left corner
- **Pad name/label** (user-editable, e.g., "Kick", "Snare") — center
- **Output indicator** — small text or icon showing output assignment (Main, Aux 1–15)
- **Active/Muted state** — icon indicator (e.g., speaker/muted icon)
- **Choke group color** — pad outline/border color. 16 distinct colors for groups 1–16. No outline = no choke group.
- **Velocity animation** — brief flash/glow on MIDI trigger, intensity proportional to velocity
- **Selected state** — highlighted border/background for the pad currently being edited

Interaction:
- **Click** → select pad (editor updates to show this pad's parameters)
- **Right-click** → context menu: Rename, Copy, Paste, Clear, Mute/Unmute

### Pad Editor (Center Column) — Tabbed

Six tabs for the selected pad:

#### Exciter Tab
- **Type selector** dropdown: Impulse, Noise Burst, Mallet, Friction, FM Impulse, Feedback
- Parameters update dynamically based on selected type:
  - Impulse: Width, Brightness
  - Noise Burst: Color (LP/HP/BP), Decay
  - Mallet: Hardness, Mass
  - Friction: Pressure, Speed
  - FM Impulse: Ratio, Depth, Decay
  - Feedback: Drive, Character

#### Body Tab
- **Type selector** dropdown: Membrane, Plate, Shell, String, Bell, Noise Body
- Core parameters: Material, Size, Tension, Damping, Strike Position
- Conditional parameters (shown/hidden based on body type):
  - Membrane: Air Coupling, Nonlinear Pitch
  - Membrane/Shell: Resonant Head toggle + Cavity Depth, Resonant Tension, Resonant Damping (when enabled)
- **Partial count** selector: 8 / 16 / 20 / 24 / 32

#### Unnatural Tab
- Mode Stretch knob
- Mode Inject: type selector (Harmonic / FM-derived / Random) + amount
- Decay Skew knob (-1 to +1)
- Material Morph: envelope shape + depth
- Nonlinear Amount knob (0–1)
- Cascade Speed knob

#### Tone Tab
- **Filter**: Type (LP/HP/BP), Cutoff, Resonance, Envelope amount + ADSR
- **Drive**: Amount, Type selector
- **Wavefolder**: Amount, Type (Triangle/Sine/Lockhart)
- **Pitch Envelope**: Start freq, End freq, Time, Curve

#### Amp Tab
- **Amp Envelope**: ADSR with visual display (uses shared ADSRDisplay component)
- **Velocity curve**: visual editor or preset curves (linear, exponential, logarithmic, S-curve)
- **Level** knob (per-pad volume)

#### Routing Tab
- **Output Assignment**: dropdown (Main, Aux 1–15)
- **Pan**: knob (affects main bus only)
- **Choke Group**: dropdown (None, 1–16)
- **Choke Priority**: slider (0–31)
- **Snare Wires** (Membrane bodies only): Amount, Delay, Damping

### Kit-Level Controls (Right Column)

- **Cross-Pad Coupling**
  - Global intensity knob
  - Compact matrix visualization showing active coupling pairs
  - Per-pair editing: click a cell in the matrix to set coupling gain
- **Master Volume**
- **Master Tune** (A4 reference, 400–480 Hz)
- **Tuning preset** selector (12-TET, Just Intonation, etc.) + Load .scl/.kbm button
- **Voice Polyphony** (4–16)
- **Voice Stealing Policy** (Oldest / Quietest / Priority)

### Kit Browser (Modal Popup)

Opened via the "Kit Browser" button in the top bar. Closes when done.

```
┌──────────────────────────────────────────────────┐
│  Kit Browser                              [✕]    │
├──────────┬───────────────────────────────────────┤
│ Category │  ┌─────┬─────┬─────┬─────┐           │
│──────────│  │ Kick│Snare│ Hat │ Tom │ ...        │
│ Factory  │  │ 808 │Brush│Open │ Hi  │            │
│ User     │  │ Sub │Wire │Clsd│ Lo  │            │
│ Acoustic │  ├─────┴─────┴─────┴─────┘           │
│ Electric │  │                                    │
│ Exotic   │  │ Drag a sound onto any pad in the   │
│ FX       │  │ grid to replace it.                │
│          │  │                                    │
│          │  │ [Load Entire Kit]                   │
│          │  │ [Save Current Kit]                  │
│          │  │ [Save Selected Pad]                 │
└──────────┴───────────────────────────────────────┘
```

- **Left panel**: Category tree (Factory / User / subcategories by sound type)
- **Right panel**: Grid or list of pad presets in selected category
- **Drag-and-drop**: Drag a preset from the browser onto any pad in the grid behind to replace that pad's sound
- **Load Entire Kit**: Replaces all 32 pads with the selected kit preset
- **Save Current Kit**: Saves all 32 pads as a kit preset
- **Save Selected Pad**: Saves the currently selected pad as a reusable pad preset
- **Audition**: clicking a preset in the browser triggers it through the selected pad so you can hear it before committing

## Planned: DMSP Integration (Post-Launch)

Work begins after the core drum synth is functional. The runtime engine stays pure classical DSP — neural/differentiable techniques are used **offline** for parameter discovery and preset creation.

### Overview

DMSP (Differentiable Modal Synthesis for Percussion, Shier 2024) wraps a modal synthesizer in a differentiable framework (PyTorch), then uses gradient descent to fit modal parameters (frequencies, amplitudes, decays) to target recordings. The output is parameter presets — no neural network runs at audio rate.

### Capability 1: Factory Preset Pipeline

An offline Python tool that builds the factory preset library from recorded drum samples:

```
[Recorded drum hit (.wav)] → [DMSP optimizer] → [Modal parameters] → [Membrum pad preset]
```

- Record real drums (kicks, snares, toms, cymbals, percussion)
- DMSP extracts optimal modal frequencies, amplitudes, decay rates, and exciter characteristics
- Export as Membrum pad presets with physically meaningful, user-editable parameters
- Convergence: seconds on GPU, minutes on CPU per sound

This produces presets that are grounded in real acoustic measurements rather than hand-tuned approximations.

### Capability 2: "Match Sound" Feature

A UI feature allowing users to import a recorded drum hit and automatically derive modal parameters:

```
┌──────────────────────────────────────────┐
│  Match Sound                      [✕]    │
│                                          │
│  Drop a drum sample here                 │
│  ┌──────────────────────────────────┐    │
│  │  ♫ my_snare_hit.wav             │    │
│  └──────────────────────────────────┘    │
│                                          │
│  Body type: [Auto-detect ▼]              │
│  Mode count: [16 ▼]                      │
│  Quality: [Standard ▼] (Fast/Standard)   │
│                                          │
│  [Analyze]     ████████░░ 78%            │
│                                          │
│  [Apply to Selected Pad]                 │
└──────────────────────────────────────────┘
```

- User drops a .wav drum hit into the dialog
- DMSP optimization runs (in a background thread or shelled-out Python process)
- Results are applied to the selected pad as editable modal parameters
- User can then tweak the parameters from a physically accurate starting point
- Implementation options: embedded Python/ONNX, or standalone helper process

This is the workflow no other drum synth offers: reverse-engineer any drum sound into a fully editable physical model.

### Capability 3: Neural Parameter Predictor (Optional)

A small MLP (~10KB, 3-4 layers) trained on the DMSP-fitted parameter corpus, using **RTNeural** (lightweight C++ header-only neural inference library, proven in guitar amp plugins):

- Maps intuitive user controls (material, size, strike position) to physically plausible modal parameter sets
- Runs once per note-on, negligible CPU (<0.1%)
- Ensures smooth, physically consistent parameter interpolation across the entire control space
- Training data: large corpus of DMSP-fitted parameters spanning diverse percussion

This is a nice-to-have — good manual interpolation curves between presets may be sufficient without it.

### What's NOT Planned

- **Neural waveform synthesis at audio rate** (RAVE, WaveNet, DDSP inference) — too slow, too much latency (46ms+), or requires GPU. Modal synthesis is already fast and deterministic.
- **Replacing the modal engine with a neural network** — bad trade-off. Modal synthesis is cheap, interpretable, and editable. A neural replacement would lose all three properties.
- **Real-time neural excitation modeling** — the ImpactExciter and BowExciter already model excitation physics well enough.

### Key References

- Shier, J. et al. — "Differentiable Modeling of Percussive Audio with Transient and Spectral Synthesis" (2024)
- Engel, J. et al. — "DDSP: Differentiable Digital Signal Processing" (ICLR, 2020)
- Caracalla, H. & Roebel, A. — "DrumBlender: Neural Drum Synthesis" (Sony CSL / IRCAM)
- Chowdhury, J. — "RTNeural: Fast Neural Inferencing for Real-Time Audio Applications"
- Nistal, J. et al. — "DrumGAN: Synthesis of Drum Sounds With Timbral Feature Conditioning Using GANs" (ISMIR, 2020)
- Caillon, A. & Esling, P. — "RAVE: A Variational Autoencoder for Fast and High-Quality Neural Audio Synthesis" (2021)

## Existing Building Blocks

The KrateDSP library and shared plugin infrastructure already contain the majority of components needed for Membrum. This section maps existing code to Membrum's architecture.

### Corpus Engine — Ready to Use

The modal synthesis core already exists and is SIMD-accelerated:

| Component | Location | What It Does | Membrum Role |
|-----------|----------|-------------|--------------|
| **ModalResonatorBank** | `dsp/include/krate/dsp/processors/modal_resonator_bank.h` | Up to 96 parallel Gordon-Smith coupled-form resonators. SoA layout, 32-byte aligned. Chaigne-Lambourg frequency-dependent damping. Material presets (Wood, Metal, Glass, Ceramic, Nylon). | **THE Corpus body engine.** Direct use for Membrane, Plate, Shell, Bell body models. Already has size, damping, material, inharmonicity (stretch/scatter) params. |
| **ModalResonatorBankSIMD** | `dsp/include/krate/dsp/processors/modal_resonator_bank_simd.h/.cpp` | Highway-accelerated SIMD kernel for modal bank. Processes 4 (SSE) or 8 (AVX2) modes per iteration. | **Core performance engine.** Already proven, runtime ISA dispatch. |
| **ModalResonator** | `dsp/include/krate/dsp/processors/modal_resonator.h` | Simpler single-body modal resonator, up to 32 modes. Impulse-invariant two-pole topology. | Fallback / lightweight body for CPU-constrained voices. |
| **IResonator** | `dsp/include/krate/dsp/processors/iresonator.h` | Abstract resonator interface (frequency, decay, brightness, excitation, energy followers). | **Unified interface** — swap between ModalResonatorBank and WaveguideString without changing voice architecture. |

### Exciter Models — Ready to Use

| Component | Location | Membrum Role |
|-----------|----------|--------------|
| **ImpactExciter** | `dsp/include/krate/dsp/processors/impact_exciter.h` | **Primary exciter.** Asymmetric pulse + shaped noise, SVF-filtered, strike position comb, micro-bounce. Velocity-dependent hardness/brightness/duration. Maps directly to Impulse, Mallet, and Noise Burst exciter types. |
| **BowExciter** | `dsp/include/krate/dsp/processors/bow_exciter.h` | **Friction exciter.** Stick-slip friction model (STK power-law), bow velocity from ADSR, rosin jitter, 2x oversampling. Maps directly to the Friction exciter type. |
| **NoiseGenerator** | `dsp/include/krate/dsp/processors/noise_generator.h` | **Noise component.** 13 noise types (white, pink, brown, blue, violet, velvet, etc.). Noise Burst exciter and Noise Body backing. |
| **FMOperator** | `dsp/include/krate/dsp/processors/fm_operator.h` | **FM Impulse exciter.** DX7-style phase modulation with self-feedback. Short FM burst for metallic transients. |

### Resonator Alternatives — Ready to Use

| Component | Location | Membrum Role |
|-----------|----------|--------------|
| **WaveguideString** | `dsp/include/krate/dsp/processors/waveguide_string.h` | **String body model.** Dispersion allpass cascade, Thiran fractional delay, pick position, stiffness/inharmonicity. Implements IResonator. |
| **KarplusStrong** | `dsp/include/krate/dsp/processors/karplus_strong.h` | **Alternative string body.** Allpass interpolation, brightness control, pick position comb, stretch via allpass cascade, continuous bowing mode. |

### Sympathetic Resonance — Ready to Use

| Component | Location | Membrum Role |
|-----------|----------|--------------|
| **SympatheticResonanceSIMD** | `dsp/systems/sympathetic_resonance_simd.h/.cpp` | **Cross-pad coupling engine.** SIMD-accelerated second-order driven resonators with Chaigne-Lambourg damping and envelope followers. This is exactly the sympathetic resonance feature we designed. |

### Tone Shaping — Ready to Use

| Component | Location | Membrum Role |
|-----------|----------|--------------|
| **SVF** | `dsp/include/krate/dsp/primitives/svf.h` | **Per-voice filter.** TPT state variable filter, 8 modes (LP/HP/BP/Notch/Allpass/Peak/Shelf). Per-sample coefficient smoothing. Modulation-stable. |
| **Waveshaper** | `dsp/include/krate/dsp/primitives/waveshaper.h` | **Drive stage.** 9 transfer functions (Tanh, Atan, Cubic, Diode, Tube, etc.). Drive + asymmetry params. |
| **Wavefolder** | `dsp/include/krate/dsp/primitives/wavefolder.h` | **Fold stage.** Triangle, Sine (Serge), Lockhart (Lambert-W). |
| **SaturationProcessor** | `dsp/include/krate/dsp/processors/saturation_processor.h` | **High-level saturation.** Multiple types, input/output gain, makeup gain. |
| **TanhADAA / HardClipADAA** | `dsp/include/krate/dsp/primitives/tanh_adaa.h`, `hard_clip_adaa.h` | **Alias-free distortion** for feedback loops. |
| **DCBlocker** | `dsp/include/krate/dsp/primitives/dc_blocker.h` | Post-saturation DC offset removal. |

### Envelopes — Ready to Use

| Component | Location | Membrum Role |
|-----------|----------|--------------|
| **ADSREnvelope** | `dsp/include/krate/dsp/primitives/adsr_envelope.h` | **Amplitude & filter envelopes.** Exponential/linear/log curves, velocity scaling, retrigger modes, Bezier curves. |
| **MultiStageEnvelope** | `dsp/include/krate/dsp/processors/multi_stage_envelope.h` | **Complex pitch envelopes.** N-stage with time and level per stage. Perfect for kick pitch sweeps. |
| **EnvelopeFollower** | `dsp/include/krate/dsp/processors/envelope_follower.h` | **Dynamic response.** Separate attack/release, RMS/amplitude modes. For velocity-responsive feedback control. |

### Voice Management — Ready to Use

| Component | Location | Membrum Role |
|-----------|----------|--------------|
| **VoiceAllocator** | `dsp/include/krate/dsp/systems/voice_allocator.h` | **Core voice routing.** Up to 32 voices, multiple stealing strategies (RoundRobin, Oldest, LowestVelocity, HighestNote). Pre-allocated, real-time safe. Configurable max polyphony. |
| **SynthVoice** | `dsp/include/krate/dsp/systems/synth_voice.h` | **Reference voice architecture.** Composites oscillators + filter + envelopes. Adapt for modal drum voice by swapping resonator for oscillator+filter. |

### Oscillators — Available for Sub/Tone Layers

| Component | Location | Membrum Role |
|-----------|----------|--------------|
| **PolyBlepOscillator** | `dsp/include/krate/dsp/primitives/polyblep_oscillator.h` | Sub-oscillator for kick reinforcement. FM/PM inputs for metallic tones. |
| **HarmonicOscillatorBank** | `dsp/include/krate/dsp/processors/harmonic_oscillator_bank.h` | Up to 64 SIMD-accelerated partials. Could supplement modal bank for additive excitation or Mode Inject. |
| **WavetableOscillator** | `dsp/include/krate/dsp/primitives/wavetable_oscillator.h` | Custom percussion wavetables, mipmap anti-aliasing. |

### Delay / Feedback Infrastructure — Ready to Use

| Component | Location | Membrum Role |
|-----------|----------|--------------|
| **DelayLine** | `dsp/include/krate/dsp/primitives/delay_line.h` | Waveguide delay loops, comb filters, feedback paths. Multiple interpolation modes. |
| **FeedbackNetwork** | `dsp/include/krate/dsp/systems/feedback_network.h` | Self-oscillation safe feedback with filter+saturation in loop, freeze mode. For the Feedback exciter type. |
| **CombFilter** | `dsp/include/krate/dsp/primitives/comb_filter.h` | Strike position effects, harmonic enhancement. |

### Core Utilities — Ready to Use

| Component | Location | Membrum Role |
|-----------|----------|--------------|
| **Gordon-Smith phasor** | Proven in `particle_oscillator.h` line 402+ and `modal_resonator_bank_simd.cpp` | 2 muls + 2 adds per partial. 30% faster than sin() lookup. Already SIMD-vectorized. |
| **XorShift32** | `dsp/core/xorshift32.h` | Per-voice deterministic randomness for micro-variation. |
| **FastMath** | `dsp/core/fast_math.h` | Fast tanh, sqrt, sigmoid for feedback loops. |
| **PitchUtils** | `dsp/core/pitch_utils.h` | MIDI note to frequency, semitone/cent calculations. |
| **Interpolation** | `dsp/core/interpolation.h` | Linear, cubic Hermite, Lagrange for sub-sample accuracy. |
| **OnePoleSmoother** | `dsp/include/krate/dsp/primitives/smoother.h` | Click-free parameter updates. |
| **LFO** | `dsp/include/krate/dsp/primitives/lfo.h` | Modulation source for future mod matrix. |
| **ModulationMatrix** | `dsp/systems/modulation_matrix.h` | Full mod routing system for future expansion. |

### Shared Plugin Infrastructure — Ready to Use

| Component | Location | Membrum Role |
|-----------|----------|--------------|
| **PresetManager** | `plugins/shared/src/preset/preset_manager.h` | Kit + per-pad preset save/load/browse. Generalized via PresetManagerConfig. |
| **PresetBrowserView** | `plugins/shared/src/ui/preset_browser_view.h` | Modal preset browser with categories, search, save dialogs. |
| **MidiEventDispatcher** | `plugins/shared/src/midi/midi_event_dispatcher.h` | Template-based VST3 event dispatch. Auto-detects handler signatures (basic/MPE/NoteExpression). Zero-copy. |
| **MidiCCManager** | `plugins/shared/src/midi/midi_cc_manager.h` | CC-to-parameter mapping, MIDI Learn, 14-bit CC support. |
| **ArcKnob** | `plugins/shared/src/ui/arc_knob.h` | Gradient-trail knob with modulation ring and value popup. |
| **ToggleButton** | `plugins/shared/src/ui/toggle_button.h` | Vector-drawn toggle with configurable icons. Mute/solo/choke group toggles. |
| **ActionButton** | `plugins/shared/src/ui/action_button.h` | Press-only button for pad triggers. |
| **StepPatternEditor** | `plugins/shared/src/ui/step_pattern_editor.h` | Step sequencer UI with drag editing, velocity, 32 steps. If we ever add a built-in sequencer. |
| **ADSR Display** | `plugins/shared/src/ui/adsr_display.h` | Visual envelope editor. For per-pad envelope visualization. |
| **XY Morph Pad** | `plugins/shared/src/ui/xy_morph_pad.h` | 2D morphing control. Material Morph or Mode Stretch/Decay Skew control. |
| **Platform Paths** | `plugins/shared/src/platform/preset_paths.h` | Cross-platform preset directory discovery. |

### Plugin Architecture Patterns — Follow Existing Instruments

The instrument plugins (Ruinae, Innexus, Gradus) establish proven patterns:

| Pattern | Reference Plugin | Key Files |
|---------|-----------------|-----------|
| Entry point & factory | Ruinae | `plugins/ruinae/src/entry.cpp` |
| Plugin IDs & param blocks | Ruinae | `plugins/ruinae/src/plugin_ids.h` |
| Param group structs (atomic) | Ruinae | `plugins/ruinae/src/parameters/*.h` |
| Processor (MIDI → voice → audio) | Innexus | `plugins/innexus/src/processor/processor.h` |
| Controller (UI + presets) | Ruinae | `plugins/ruinae/src/controller/controller.h` |
| Voice struct pattern | Innexus | `plugins/innexus/src/processor/` |
| Preset config | Ruinae | `plugins/ruinae/src/preset/ruinae_preset_config.h` |
| AU configuration | Gradus | `plugins/gradus/resources/au-info.plist`, `auv3/audiounitconfig.h` |
| CMake plugin target | Gradus | `plugins/gradus/CMakeLists.txt` |

### Test Infrastructure — Ready to Use

| Component | Location | Membrum Role |
|-----------|----------|--------------|
| **Signal Metrics** | `tests/test_helpers/signal_metrics.h` | SNR, THD, crest factor for sound quality validation. |
| **Spectral Analysis** | `tests/test_helpers/spectral_analysis.h` | FFT-based aliasing measurement, spectral peaks, THD verification. |
| **Artifact Detection** | `tests/test_helpers/artifact_detection.h` | Click/pop/clipping detection for regression testing. |
| **Allocation Detector** | `tests/test_helpers/allocation_detector.h` | Real-time safety verification (no allocs in process). |
| **Test Signals** | `tests/test_helpers/test_signals.h` | Standard sine, sweep, noise, impulse generators. |
| **Parameter Sweep** | `tests/test_helpers/parameter_sweep.h` | Systematic parameter sensitivity analysis. |

### Coverage Summary

| Membrum Feature | Existing Components | New Code Needed |
|----------------|--------------------|-----------------| 
| Corpus body (modal) | ModalResonatorBank + SIMD | Body model presets (mode frequency tables for membrane/plate/shell/bell) |
| Exciter (impulse/mallet) | ImpactExciter | Minimal — parameter mapping |
| Exciter (friction) | BowExciter | Minimal — parameter mapping |
| Exciter (noise burst) | NoiseGenerator + ADSREnvelope | Combine existing components |
| Exciter (FM) | FMOperator | Short-burst envelope wrapper |
| Exciter (feedback) | FeedbackNetwork | Route body output → exciter input |
| String body | WaveguideString / KarplusStrong | Already done |
| Noise body | NoiseGenerator + SVF | Combine existing components |
| Tone shaper | SVF + Waveshaper + Wavefolder | Combine into per-voice chain |
| Envelopes | ADSREnvelope + MultiStageEnvelope | Done |
| Pitch envelope | MultiStageEnvelope | Done |
| Voice management | VoiceAllocator | Configure for drum use (choke groups need new logic) |
| Cross-pad coupling | SympatheticResonanceSIMD | Already SIMD-accelerated |
| Unnatural: Mode Stretch | ModalResonatorBank stretch param | Already exists as "stretch" |
| Unnatural: Mode Inject | HarmonicOscillatorBank | Mix into modal bank's mode set |
| Unnatural: Nonlinear Coupling | ModalResonatorBank | **New**: von Karman quadratic coupling (precomputed H_pqr tables, Stormer-Verlet integration) |
| Unnatural: Decay Skew | ModalResonatorBank | **New**: invert decay-vs-frequency curve |
| Unnatural: Material Morph | ModalResonatorBank material | **New**: per-hit automation envelope for material |
| MIDI dispatch | MidiEventDispatcher | Done |
| Presets | PresetManager + browser | Config struct only |
| UI controls | ArcKnob, ToggleButton, etc. | Done |
| Pad grid UI | — | **New**: custom CView for 4×8 pad grid with choke group colors, velocity animation, drag-drop target |
| Pad editor UI | ArcKnob, ToggleButton, ADSRDisplay | **New**: 6-tab editor layout, dynamic parameter visibility per exciter/body type |
| Kit browser UI | PresetBrowserView (base) | **New**: modal popup with category tree, drag-and-drop pad presets, kit load/save |
| Choke groups | VoiceAllocator (base) | **New**: priority-based choke logic (16 groups, priority 0–31) |
| Output routing | — | **New**: 16 stereo bus layout, per-pad assignment parameter |
| Double membrane | ModalResonatorBank (×2) | **New**: coupled modal synthesis with air cavity spring, 4 params |
| Microtuning | midiNoteToFrequency() (A4 ref only) | **New**: TuningTable class, .scl/.kbm parser, 8 built-in scales, file loading |
| Plugin scaffold | Follow Ruinae/Gradus patterns | Boilerplate from templates |

**Bottom line: ~75% of the DSP engine exists.** The main new work is:
1. Drum voice class compositing existing components
2. Choke group logic (priority-based)
3. Body model preset tables (modal frequency ratios for each body type)
4. Double membrane coupling (air cavity spring between two modal banks)
5. Von Karman nonlinear mode coupling (precomputed coefficients + Stormer-Verlet integration)
6. Decay Skew and Material Morph automation
7. TuningTable + Scala (.scl/.kbm) parser
8. Pad grid UI
9. Output routing (16 stereo buses)
10. Plugin scaffold (entry, IDs, CMake — boilerplate)

## Physical Modeling Reference

This section documents the scientific foundations for each body model and exciter type. All formulas and values are sourced from peer-reviewed acoustics literature.

### Circular Membrane Modes (Bessel Function Zeros)

The vibration modes of a circular membrane clamped at its edge are governed by the 2D wave equation in polar coordinates. Modal frequencies are proportional to zeros of Bessel functions J_m:

```
f_mn = (j_mn / (2*pi*a)) * sqrt(T / sigma)
```

where `a` = radius, `T` = tension, `sigma` = surface density, and `j_mn` is the n-th zero of J_m(x).

**Frequency ratios relative to fundamental (j_mn / j_01):**

| Mode (m,n) | j_mn   | Ratio | Description |
|------------|--------|-------|-------------|
| (0,1)      | 2.4048 | 1.000 | Fundamental |
| (1,1)      | 3.8317 | 1.593 | 1 nodal diameter |
| (2,1)      | 5.1356 | 2.136 | 2 nodal diameters |
| (0,2)      | 5.5201 | 2.296 | 1 nodal circle |
| (3,1)      | 6.3802 | 2.653 | 3 nodal diameters |
| (1,2)      | 7.0156 | 2.918 | 1 dia + 1 circle |
| (4,1)      | 7.5883 | 3.156 | 4 nodal diameters |
| (2,2)      | 8.4172 | 3.501 | 2 dia + 1 circle |
| (0,3)      | 8.6537 | 3.600 | 2 nodal circles |
| (5,1)      | 8.7715 | 3.649 | 5 nodal diameters |
| (3,2)      | 9.7610 | 4.060 | 3 dia + 1 circle |
| (1,3)      | 10.1735| 4.231 | 1 dia + 2 circles |
| (4,2)      | 11.0647| 4.602 | 4 dia + 1 circle |
| (2,3)      | 11.6198| 4.832 | 2 dia + 2 circles |
| (0,4)      | 11.7915| 4.903 | 3 nodal circles |
| (5,2)      | 12.3386| 5.131 | 5 dia + 1 circle |

These ratios are **inharmonic** — none are integer multiples. This is why drums have indefinite pitch.

**Sources:** Abramowitz & Stegun (1964), Wolfram MathWorld, NIST DLMF 10.21

### Timpani: Air Coupling Shifts Toward Harmonicity

Enclosed air in a kettledrum preferentially shifts the (m,1) modes toward a near-harmonic series. The **Air Coupling** parameter in the Membrane body model interpolates between ideal and air-coupled ratios:

| Mode   | Ideal Membrane | Timpani (air-coupled) | Near-harmonic |
|--------|---------------|----------------------|---------------|
| (1,1)  | 1.593         | 1.000 (reference)    | 1.00          |
| (2,1)  | 2.136         | 1.500                | 1.50          |
| (3,1)  | 2.653         | 1.980                | ~2.00         |
| (4,1)  | 3.156         | 2.440                | ~2.50         |
| (5,1)  | 3.649         | ~2.94                | ~3.00         |

The (0,n) axisymmetric modes are suppressed by air coupling. This gives timpani (and kick drums with ports) a clearer sense of pitch.

**Source:** Rossing, "Science of Percussion Instruments" (2000); Sound on Sound timpani synthesis series

### Rectangular Plate Modes (Kirchhoff Theory)

Plates differ from membranes: **bending stiffness** dominates (4th-order biharmonic equation vs 2nd-order wave equation). For a simply-supported rectangular plate:

```
f_mn = (pi/2) * sqrt(D / (rho*h)) * (m^2/a^2 + n^2/b^2)
```

where `D = 2*h^3*E / (3*(1-nu^2))` is bending stiffness. Ratios for a **square plate** (a=b):

| Mode (m,n) | Ratio |
|------------|-------|
| (1,1)      | 1.00  |
| (1,2)=(2,1)| 2.50  |
| (2,2)      | 4.00  |
| (1,3)=(3,1)| 5.00  |
| (2,3)=(3,2)| 6.50  |
| (1,4)=(4,1)| 8.50  |
| (3,3)      | 9.00  |
| (2,4)=(4,2)| 10.00 |

Key difference: plate modes scale as m^2+n^2 (wider spacing at high freq) vs membrane modes scaling as sqrt(m^2+n^2).

**808 Cowbell reference:** Two resonant frequencies at 587 Hz and 845 Hz (ratio 1:1.44), bandpass at 2640 Hz, decay ~50-100ms.

**Source:** Leissa, "Vibration of Plates" (NASA SP-160, 1969)

### Free-Free Beam Modes (Bars, Xylophones, Marimbas)

For the Shell body model and bar-type percussion:

| Mode k | f_k/f_1 ratio |
|--------|--------------|
| 1      | 1.000        |
| 2      | 2.757        |
| 3      | 5.404        |
| 4      | 8.933        |
| 5      | 13.344       |
| 6      | 18.637       |

These are **extremely inharmonic**. Xylophone/marimba makers undercut bars to shift mode 2 toward 3x or 4x the fundamental.

**Tubular bells:** Modes 4-6 approximate 2:3:4 ratio — the ear perceives a virtual pitch one octave below mode 4. The three lowest modes are too weak to contribute to pitch.

**Source:** Fletcher & Rossing, "The Physics of Musical Instruments" (1998)

### Bell Partials (Chladni's Law)

Church bell partials follow a modified Chladni's law: `f_m = C * (m+b)^p`

Named partials relative to the **nominal** (the strike tone):

| Partial     | Ratio to Nominal | Interval              |
|-------------|------------------|-----------------------|
| Hum         | 0.250            | 2 octaves below       |
| Prime       | 0.500            | 1 octave below        |
| Tierce      | 0.600            | Minor 10th below      |
| Quint       | 0.750            | Perfect 5th below     |
| **Nominal** | **1.000**        | **Reference**         |
| Superquint  | ~1.500           | ~Perfect 5th above    |
| Octave Nom. | ~2.000           | ~Octave above         |

Gamelan instruments are intentionally inharmonic with beating between paired instruments (ombak, 5-7 beats/sec).

**Source:** Hibberts, "Partial Frequencies and Chladni's Law" (Open J. Acoustics, 2014); Perrin et al., JASA (1983)

### Frequency-Dependent Damping (Chaigne-Lambourg Model)

The standard model for how modes decay at different rates:

```
R_k = b1 + b3 * f_k^2
T60_k = 6.91 / R_k
```

where:
- `b1` = frequency-independent damping (air drag, viscous losses) [Hz]
- `b3` = frequency-dependent damping (internal material friction) [seconds]
- `f_k` = frequency of mode k [Hz]

**Material parameter values:**

| Material    | b1 (s^-1) | b3 (s)      | Loss Factor (eta) | Q Factor | Character |
|-------------|-----------|-------------|-------------------|----------|-----------|
| Steel       | 0.1-0.5   | 1e-11–1e-10 | 0.0001–0.01      | 100–10000 | Very resonant, long sustain |
| Brass       | 0.2-1.0   | 1e-11–1e-10 | 0.0002–0.001     | 1000–5000 | Bright, good sustain |
| Glass       | 0.1-0.5   | 1e-11–1e-10 | 0.0001–0.005     | 200–10000 | Crystalline, resonant |
| Aluminum    | 0.2-1.0   | 1e-10–1e-9  | 0.0001–0.02      | 50–10000 | Bright, moderate sustain |
| Ceramic     | 1.0-5.0   | 1e-9–1e-8   | 0.001–0.01       | 100–1000 | Medium sustain |
| Wood        | 10-30     | 1e-8–1e-7   | 0.005–0.05       | 20–200   | Short sustain, warm |
| Nylon/Rubber| 50-200    | 1e-6–1e-5   | 0.05–2.0         | 0.5–20   | Very damped, dead thud |

**Key insight:** b1 controls overall sustain length. b3 controls how much faster high modes decay relative to low modes. Metal: low b1 (long), low b3 (even across spectrum). Wood: high b1 (short), moderate b3 (darker decay). Rubber: extreme b1 (almost no sustain).

The **Material** knob in Membrum sweeps through these b1/b3 value pairs continuously.

**Source:** Chaigne & Lambourg, JASA (2001); Chaigne & Askenfelt, JASA (1994); Nathan Ho

### Strike Position Mathematics

The amplitude of mode (m,n) excited by a point strike at position (r_0, theta_0) on a circular membrane:

```
A_mn ~ J_m(j_mn * r_0 / a) * cos(m * theta_0)
```

**Center strike (r_0 = 0):** Only axisymmetric modes (m=0) excited. Boomy, pitched.
**Edge strike (r_0 ~ a):** Many high-m modes excited. Bright, cutting, inharmonic.
**Off-center (r_0 ~ 0.3-0.7 * a):** Broadest range of modes. Typical playing position.

For rectangular plates/bars: `A_mn ~ sin(m*pi*x_0/a) * sin(n*pi*y_0/b)` — much simpler.

Striking at a nodal line of a particular mode suppresses that mode. The (0,2) mode has a nodal circle at r = 0.436*a.

**Source:** Fletcher & Rossing (1998); Kinsler, "Fundamentals of Acoustics"

### Power-Law Contact Force (Mallet-Membrane Interaction)

The mallet-membrane interaction follows a nonlinear power law:

```
F = K * delta^alpha
```

where `delta` = felt compression, `K` = stiffness, `alpha` = nonlinearity exponent.

| Mallet Type         | alpha    | Contact Duration | Character |
|---------------------|----------|------------------|-----------|
| Soft felt           | 1.5–2.5  | 5–8 ms           | Warm, low BW excitation |
| Hard rubber/plastic | 2.5–3.5  | 2–5 ms           | Bright, broadband |
| Wood stick tip      | 3.0–4.0  | 1–3 ms           | Very bright, snappy |

The spectral turnover frequency (where excitation rolls off): `f_turnover ~ 1 / (pi * tau_contact)`

This means harder mallets produce ~10x wider bandwidth excitation than soft ones — the fundamental reason velocity changes timbre, not just volume.

**Implementation:** Generate the excitation as a parametric force pulse (half-cosine or Gaussian) whose width scales inversely with velocity, filtered through a lowpass whose cutoff tracks hardness. This is what ImpactExciter already does.

**Source:** Chaigne & Doutaut (mallet percussion); Hunt-Crossley contact model; Boutillon (piano hammers)

### Nonlinear Tension Modulation (Pitch Glide)

Real drum heads exhibit amplitude-dependent pitch rise: large displacements stretch the membrane, increasing tension and raising pitch. This has been experimentally measured at up to **65 cents** on hard hits.

```
f_k(t) = f_k0 * (1 + alpha * E_total(t))
```

where `E_total(t)` is total instantaneous energy across all modes (decays with the sound). The **Nonlinear Pitch** parameter controls `alpha`. This is distinct from the Pitch Envelope in the tone shaper — it's physics-based and affects all modes simultaneously, proportional to the total energy.

**Source:** Bilbao, Touze; Nathan Ho (modal synthesis)

### FM Synthesis Ratios for Metallic Percussion

When using the FM Impulse exciter, these carrier:modulator ratios produce specific metallic characters:

| Sound Target   | C:M Ratio      | Mod Index | Notes |
|---------------|----------------|-----------|-------|
| Bell/Gong      | 1 : 1.4        | 4–8       | Classic Chowning bell |
| Cymbal/Gong    | 1 : sqrt(2)    | 6–10+     | Maximum inharmonicity |
| Stria (Chowning)| 1 : phi (1.618)| variable  | Maximally non-repeating spectrum |
| Tubular bell   | 2 : 5          | moderate  | Harmonic with gaps |
| Metallic chime | 1 : 2.005      | 3–6       | Near-integer = slow beating |
| Marimba        | 1 : 2.4        | 0.5–2     | Wood-like metallic |
| Wood drum      | 1 : 1.4        | 1–3       | Also 1:0.6875 |

**Key technique:** The modulation index must decay faster than the carrier amplitude — this makes the attack bright (many sidebands) while the tail is pure (few sidebands), mimicking real metallic percussion where high-frequency modes decay faster.

**Source:** Chowning, JAES (1973); CCRMA percussion synthesis tutorials

### Cymbal Synthesis: The Hybrid Approach

Cymbals have ~21 modes/kHz (Weinreich formula), yielding ~400 modes below 20 kHz — far too many for brute-force modal synthesis. The recommended hybrid approach:

1. **Modal component** (20-40 modes): Captures the tonal body and prominent resonances. Use plate mode ratios with frequency-dependent damping.
2. **Noise component**: Filtered noise (bandpass, time-varying center frequency and bandwidth) for the high-frequency wash and shimmer.
3. **Nonlinear coupling**: At high amplitudes, von Karman mode coupling creates energy cascade and spectral broadening (see Von Karman Nonlinear Mode Coupling section below). Additionally drives amplitude-dependent broadening of the noise component.
4. **Spectral evolution**: Energy migrates upward through the spectrum over the first ~100ms, then high frequencies decay first.

**Hi-hat specifics:** Model pedal position as continuous control over damping coefficients of all modes. Closed = extreme damping (10-50ms decay). Open = long sustain (seconds). "Chick" = rapid closure creating short noise burst.

**808 alternative:** Six inharmonic oscillators at ~142, 211, 297, 385, 540, 800 Hz through steep highpass (~6kHz) and envelope shaping. CPU-efficient, iconic electronic sound.

**Source:** Dahl et al., DAFx 2019; Rossing JASA cymbal studies; TR-808 circuit analyses

### Von Karman Nonlinear Mode Coupling

The defining characteristic of cymbals, gongs, and other thin metallic percussion: modes exchange energy through geometric nonlinearity. This is what makes a cymbal sound like a cymbal rather than a bell.

#### The Physics

Standard (Kirchhoff) plate theory assumes small deflections — each mode vibrates independently. **Von Karman plate theory** accounts for large deflections where the plate thickness is comparable to the displacement. At large amplitudes, bending and in-plane stretching become coupled: displacement creates stress, stress modifies displacement. This circular dependency is the nonlinearity.

The result: modes are no longer independent. Energy flows between them through quadratic coupling terms. The coupling between modes (p, q, r) depends on overlap integrals of their mode shapes.

#### The Audible Effect

| Strike Force | Regime | Sound Character |
|---|---|---|
| Soft | Linear | Clear bell-like tones, clean decay |
| Medium | Mildly nonlinear | Tones + shimmer building after attack, pitch warble |
| Hard | Fully nonlinear | Energy cascade → high-frequency shimmer, complex evolving texture |
| Extreme | "Wave turbulence" | Individual modes indistinguishable → near-continuous noise spectrum (crash) |

Key audible behaviors that ONLY come from nonlinear mode coupling:
1. **Spectral evolution** — new frequencies appear *after* the strike, not present in the excitation
2. **Energy cascade** — the rush of high-frequency shimmer that builds up 10-50ms post-strike
3. **Velocity-dependent timbre** — soft and hard hits produce qualitatively different sounds, not just louder/quieter
4. **Pitch bending** — gongs "bend" in pitch when struck hard (internal resonance between coupled mode pairs)
5. **Subharmonic generation** — frequencies *below* the excited modes appear through nonlinear coupling

No commercial synth currently offers musically controllable von Karman nonlinearity. This is a unique differentiator for Membrum.

#### Implementation: Modal Projection (Touze et al.)

Instead of simulating the full plate PDE (millions of grid points), project onto N modes and simulate their coupled evolution:

```
x_p'' + 2*zeta_p*omega_p*x_p' + omega_p^2*x_p + sum_over_q_r(H_pqr * x_q * x_r) = f_p(t)
```

Where:
- `x_p` = amplitude of mode p
- `omega_p` = natural frequency of mode p
- `zeta_p` = damping ratio of mode p
- `H_pqr` = precomputed coupling coefficient between modes p, q, r
- `f_p(t)` = excitation force projected onto mode p

The quadratic coupling terms `H_pqr * x_q * x_r` are negligible at small amplitudes (linear behavior for free) and dominate at large amplitudes (nonlinear regime).

**Precomputation (offline, per body model):**
- Coupling coefficients `H_pqr` depend on mode shape overlap integrals
- For circular plates: analytical expressions from Bessel functions
- Sparse: most triples have negligible coupling. Only need to store/compute significant terms.
- Stored as lookup tables per body model

**Runtime cost:** O(N²) per sample for N modes (iterating significant coupling pairs). With 20-30 modes, this adds ~30-50% to the per-voice modal computation. Manageable within the CPU budget, especially since nonlinear coupling is most relevant for Noise Body (cymbals/hats) where you want the shimmer.

#### Membrum Parameters

| Parameter | Maps To | Effect |
|-----------|---------|--------|
| **Nonlinear Amount** (0–1) | Scales all `H_pqr` coefficients | 0 = linear (bell). 0.3 = subtle shimmer (ride ping). 0.7 = strong cascade (crash). 1.0 = full chaos (gong/tam-tam) |
| **Cascade Speed** (continuous) | Frequency-dependent scaling of coupling — higher speed weights coupling toward high-frequency modes | Low = slow gong bloom (energy migrates upward over 50-100ms). High = instant cymbal splash |

Velocity naturally drives the nonlinearity via amplitude: harder hits → larger `x_q * x_r` products → more coupling activation. The Amount knob sets the *potential*; velocity determines how much is realized per hit.

#### Numerical Integration

The nonlinear ODEs require careful integration to avoid blowup:
- **Stormer-Verlet** (symplectic): energy-conserving, second-order, two evaluations per step. Preferred for stability.
- Clamp mode amplitudes to prevent runaway in extreme parameter combinations
- Energy monitoring: if total energy exceeds excitation energy by threshold, reduce coupling (safety valve)

**Sources:** Touze, Thomas & Chaigne, "Asymptotic Non-Linear Normal Modes" (JSV, 2004); Ducceschi & Bilbao, "Energy-Conserving Schemes for von Karman Plates" (DAFx, 2016); Bilbao, "Numerical Sound Synthesis" (2009, Ch. 12)

### Snare Wire Physics

The snare drum's characteristic buzz comes from snare wires rattling against the bottom membrane in a highly nonlinear collision process.

**Bilbao's penalty method:** `F_collision = K_c * [delta]_+^alpha_c` where `[delta]_+` = max(0, delta) ensures one-sided contact only.

**Simplified real-time model:**
1. Take membrane resonator output
2. Amplitude-modulate a bandpass-filtered noise source (1-8 kHz) — when displacement exceeds threshold, snare noise activates
3. Apply comb filter to noise (delay = snare wire length) for metallic resonance
4. Add 0.5-2ms propagation delay (air cavity coupling)
5. Mix at adjustable snare amount

**Source:** Bilbao, JASA (2012); Torin, Hamilton & Bilbao, DAFx (2014)

### Brush Excitation Physics

Wire brushes (50-200 bristles) produce sound through stochastic friction and micro-impacts.

**LuGre friction model** (most practical for real-time):
```
dz/dt = v - sigma_0 * |v| * z / g(v)
F = sigma_0 * z + sigma_1 * dz/dt + sigma_2 * v
g(v) = F_coulomb + (F_static - F_coulomb) * exp(-(v/v_stribeck)^2)
```

**Simplified approach for Membrum:** Filtered noise as excitation with bandwidth proportional to brush velocity, plus granular micro-impulses at rate proportional to brush speed (modeling bristle snaps). Spectral centroid modulates with pressure.

**Source:** Bilbao et al., IEEE (2022); Avanzini, DAFx (2002)

### Self-Oscillation Conditions

For bowed/sustained percussion (Friction exciter), self-oscillation occurs when:

```
Energy_input_per_cycle >= Energy_lost_per_cycle
```

The negative slope region of the friction curve (Stribeck effect) acts as negative resistance, pumping energy into the system. Minimum bow force for sustained oscillation:

```
F_bow_min ~ (f_k / Q_k) * (v_bow / delta_v_stribeck)
```

Higher modes require more bow pressure to sustain. BowExciter already implements this via STK power-law friction.

**Source:** Essl & Cook, ICMC (1999); "Banded Waveguides for Bowed Bar Percussion"

### Quick Reference: Mode Ratios by Instrument

| Instrument      | Model         | First 8 ratios |
|----------------|---------------|----------------|
| Ideal membrane  | Bessel/j_01   | 1.000, 1.593, 2.136, 2.296, 2.653, 2.918, 3.156, 3.501 |
| Timpani         | Air-coupled   | 1.000, 1.500, 1.980, 2.440, 2.940, 3.430, 3.890, 4.350 |
| Kick drum       | Lightly coupled | Between ideal membrane and timpani |
| Square plate    | Kirchhoff     | 1.000, 2.500, 4.000, 5.000, 6.500, 8.500, 9.000, 10.000 |
| Free-free beam  | Euler-Bernoulli | 1.000, 2.757, 5.404, 8.933, 13.344, 18.637, 24.812, 31.870 |
| Church bell     | Chladni       | 0.250, 0.500, 0.600, 0.750, 1.000, 1.500, 2.000, 2.600 |
| 808 Cowbell     | Two-mode      | 587 Hz, 845 Hz (ratio 1:1.44) |
| Tubular bell    | Modes 4-6     | Perceived pitch at mode4/2, modes 4-6 ≈ 2:3:4 |

### Academic References

1. Rossing, T.D. — "Science of Percussion Instruments" (World Scientific, 2000)
2. Fletcher, N.H. & Rossing, T.D. — "The Physics of Musical Instruments" (Springer, 1998)
3. Chaigne, A. & Lambourg, C. — "Time-domain simulation of damped impacted plates" (JASA, 2001)
4. Chaigne, A. & Askenfelt, A. — "Numerical simulations of piano strings" (JASA, 1994)
5. Bilbao, S. — "Time domain simulation and sound synthesis for the snare drum" (JASA, 2012)
6. Chowning, J.M. — "The Synthesis of Complex Audio Spectra by Means of FM" (JAES, 1973)
7. Essl, G. & Cook, P.R. — "Banded Waveguides for Bowed Bar Percussion" (ICMC, 1999)
8. Dahl, L. et al. — "Real-Time Modal Synthesis of Crash Cymbals" (DAFx, 2019)
9. Leissa, A.W. — "Vibration of Plates" (NASA SP-160, 1969)
10. Hibberts, W.A. — "Partial Frequencies and Chladni's Law in Church Bells" (OJA, 2014)
11. Ho, N. — "Exploring Modal Synthesis" (nathan.ho.name, 2023)
12. Werner, K.J. et al. — "TR-808 Bass Drum Circuit Model" (DAFx, 2014)
13. Delle Monache, S. et al. — "Perceptual Evaluation of Modal Synthesis" (SMC, 2019)
14. Avanzini, F. — "Friction Models for Sound Synthesis" (DAFx, 2002)
15. Shier, J. — "Differentiable Modeling of Percussive Audio" (2024)
16. Touze, C., Thomas, O. & Chaigne, A. — "Asymptotic Non-Linear Normal Modes for Large-Amplitude Vibrations" (JSV, 2004)
17. Ducceschi, M. & Bilbao, S. — "Energy-Conserving Finite Difference Schemes for Nonlinear Plates" (DAFx, 2016)
18. Kapur, A. et al. — "The Digital Tabla" (ICMC, 2004)

## Open Questions / Future Versions

### Resolved by Research
- ~~Exact partial counts~~: **16 default is validated** (SMC 2019 perceptual study). 8 min, 32 max, 20-30 for high quality.
- ~~Cross-pad coupling topology~~: **Bandpass-filtered coupling matrix** with per-pair gain coefficients (0-0.05 range). Already implemented in SympatheticResonanceSIMD.
- ~~Output routing~~: **16 stereo pairs** (industry standard: Battery, Maschine, SD3 all use 16 stereo). Per-pad assignment, exclusive routing, mono signal on stereo bus.
- ~~Choke groups~~: **16 groups with per-pair priority** (0–31). Equal priority = mutual choke. Higher priority silences lower. Two params per pad: group + priority.
- ~~Double membrane coupling~~: **Coupled modal synthesis** with air cavity spring. ~1.6-1.8x CPU (not 2x). 4 params: enable, cavity depth, resonant tension, resonant damping. Membrane/Shell bodies only.
- ~~Microtuning~~: **Full Scala (.scl/.kbm) support.** TuningTable class in shared DSP, 128-entry frequency lookup, lock-free swap to audio thread. 8 built-in tunings + user-loadable files.

### Still Open
- **Sample layer**: v2 could add sample playback for hybrid sounds
- ~~Per-pad effects~~: **No internal effects.** 16 stereo outputs provide per-pad DAW routing for external effects.
- ~~Modulation~~: **No mod matrix / LFOs.** Keep focus on unique drum synthesis; modulation and effects handled externally by the DAW.
- ~~Microtuning~~: **Full Scala support** — see Microtuning section.
- ~~Double membrane coupling~~: **Yes, included** — see Double Membrane Coupling section. ~1.6-1.8x cost, not 2x.
- ~~Nonlinear mode coupling~~: **Two parameters: Nonlinear Amount (0–1) + Cascade Speed.** Von Karman quadratic coupling via modal projection (Touze et al.). Precomputed H_pqr coefficients per body model, O(N²) runtime cost. See Physics Reference section.
- **Cymbal mode count vs CPU**: Need profiling to determine if 30-40 modes + noise is viable for multiple simultaneous cymbals within the voice budget.
- ~~Differentiable synthesis / neural acceleration~~: **Planned post-launch capability.** See Planned: DMSP Integration section.
- **MODO Drum comparison**: IK Multimedia uses samples for cymbals (suggesting modal cymbal synthesis was too expensive for them). Our hybrid approach needs validation.

## Implementation Roadmap

Each phase is a self-contained deliverable suitable for its own speckit spec (FRs, SCs, tasks). Phases are ordered by dependency — each builds on the previous.

### Phase 1 — Plugin Scaffold + Single Voice ✅ Complete

**Goal:** Get sound out. Minimal viable drum synth with one exciter and one body.

**Scope:**
- CMake target, entry.cpp, plugin_ids.h, version.h (follow Gradus/Ruinae patterns)
- Root CMakeLists.txt: add `add_subdirectory(plugins/membrum)`
- Processor: MIDI note-on/off → single drum voice → stereo output
- Controller: minimal parameter registration
- Single voice: Impulse exciter → Membrane body (16 modes, Bessel ratios) → Amp ADSR → output
- Basic velocity → amplitude + exciter brightness mapping
- Hardcoded to pad 1 / MIDI note 36 for testing
- No UI (host-generic editor only)
- AU configuration: `resources/au-info.plist`, `resources/auv3/audiounitconfig.h`
- GitHub Actions: update CI workflows to build/test Membrum alongside existing plugins
- `plugins/membrum/docs/` directory with HTML templates (matching other plugins' doc structure)
- `plugins/membrum/CHANGELOG.md` — initial version entry

**Exit criteria:** Plugin loads in DAW, receives MIDI, produces audible membrane drum sound with velocity response. CI builds and tests Membrum. Pluginval passes at level 5.

**Spec reference:** Architecture, Per-Voice Signal Path, Exciter Types (Impulse only), Corpus Engine (Membrane only), Body Parameters (Material, Size, Damping, Strike Position)

---

### Phase 2 — All Exciters + All Bodies

**Goal:** Complete the voice DSP — any exciter/body combination is playable.

**Scope:**
- All 6 exciter types: Impulse, Noise Burst, Mallet, Friction, FM Impulse, Feedback
- All 6 body models: Membrane, Plate, Shell, String, Bell, Noise Body
- Mode frequency ratio tables for each body model (Bessel zeros, Kirchhoff plate, Euler-Bernoulli beam, Chladni bell)
- Per-exciter velocity behavior (bandwidth scaling, hardness, pressure)
- Body parameters: Material (b1/b3 sweep), Size, Tension, Damping, Strike Position, Air Coupling (Membrane)
- Partial count selector (8/16/20/24/32)
- Noise Body hybrid approach: sparse modes + filtered noise

**Exit criteria:** All 42 exciter×body combinations produce distinct, physically plausible sounds. Each exciter responds to velocity with timbral change, not just amplitude.

**Spec reference:** Exciter Types, Velocity Behavior, Corpus Engine, Body Models, Body Parameters, Physical Modeling Reference (all mode ratio tables)

---

### Phase 3 — Voice Management + Choke Groups

**Goal:** 32 pads with polyphonic voice allocation and choke group behavior.

**Scope:**
- 32 pads mapped to MIDI 36–67 (GM drum map)
- Per-pad configuration: exciter type + params, body type + params, independent settings
- VoiceAllocator integration: configurable max polyphony (4–16), stealing policies (oldest/quietest/priority)
- Priority-based choke groups: 16 groups, priority 0–31, `>=` comparison for mutual/one-way choke
- Choke action: fast release envelope on silenced voices
- Per-pad level and velocity curve

**Exit criteria:** Multiple pads trigger simultaneously with correct polyphony. Choke groups work: mutual choke at equal priority, one-way at unequal. Voice stealing engages at max polyphony.

**Spec reference:** Pad Layout, Voice Management, Choke Group Design, MIDI

---

### Phase 4 — Tone Shaper + Unnatural Zone

**Goal:** Per-voice post-processing and beyond-physics parameters.

**Scope:**
- Tone Shaper chain per voice:
  - SVF filter (LP/HP/BP) with dedicated ADSR envelope
  - Drive (waveshaper with ADAA)
  - Wavefolder (Triangle/Sine/Lockhart)
  - Pitch envelope (start freq → end freq, exponential sweep, configurable time)
- Snare wire modeling on Membrane bodies (AM noise, comb filter, propagation delay)
- Unnatural Zone:
  - Mode Stretch (compress/spread modal frequencies)
  - Mode Inject (harmonic/FM-derived/random partial injection)
  - Decay Skew (-1 inverted → 0 flat → +1 natural)
  - Material Morph (per-hit b1/b3 automation envelope)
  - Nonlinear Amount (von Karman coupling coefficient scaling, 0–1)
  - Cascade Speed (frequency-dependent coupling weighting)
- Von Karman implementation: precomputed H_pqr coupling tables per body model, Stormer-Verlet integration, energy clamping safety valve

**Exit criteria:** Tone shaper audibly processes voice output (filter sweep, drive saturation, pitch drop on kicks). Unnatural Zone: Mode Stretch produces gong-like clustering, Nonlinear Amount creates velocity-dependent shimmer cascade on cymbals. Snare wires buzz on Membrane bodies.

**Spec reference:** Tone Shaper, Snare Wire Modeling, The Unnatural Zone, Von Karman Nonlinear Mode Coupling, Nonlinear Tension Modulation

---

### Phase 5 — Double Membrane + Cross-Pad Coupling

**Goal:** Advanced physical modeling interactions between membrane heads and between pads.

**Scope:**
- Double membrane coupling (Membrane/Shell bodies):
  - Second modal bank (resonant head, 8 modes) coupled via air cavity spring
  - Parameters: Resonant Head (on/off), Cavity Depth, Resonant Tension, Resonant Damping
  - Coupling via sparse spring matrix (same-index mode pairs)
  - Output: batter head direct + configurable resonant head bleed
- Cross-pad sympathetic resonance (SympatheticResonanceSIMD):
  - Global intensity knob
  - Bandpass-filtered coupling: pad output filtered around receiving pad's modal frequencies
  - Coupling only active between currently sounding pads
  - Per-pair gain coefficients (0–0.05 range)

**Exit criteria:** Double membrane adds audible body/sustain to toms. Hitting kick makes snare wires buzz via cross-pad coupling. Toms resonate sympathetically when nearby pads are struck.

**Spec reference:** Double Membrane Coupling, Cross-Pad Coupling (Sympathetic Resonance)

---

### Phase 6 — Output Routing

**Goal:** 16 stereo output buses with per-pad assignment.

**Scope:**
- VST3 bus layout: 1 stereo Main + 15 stereo Aux, all declared at init
- Main Out marked kDefaultActive, aux buses inactive by default
- Per-pad parameters: Output Assignment (Main/Aux 1–15), Pan (affects Main bus only)
- Exclusive routing: pad routed to aux is removed from main mix
- Mono voice signal centered on stereo bus

**Exit criteria:** Pads default to Main stereo out with pan. Assigning a pad to Aux N routes audio exclusively to that bus. Host sees and can activate all 16 stereo outputs.

**Spec reference:** Output Routing

---

### Phase 7 — Microtuning

**Goal:** Full Scala microtuning support for tonal body models.

**Scope:**
- TuningTable class in shared DSP (`dsp/include/krate/dsp/core/tuning_table.h`): 128-entry MIDI note → frequency lookup
- .scl parser (scale definitions: cents and ratios)
- .kbm parser (keyboard mapping: reference note/frequency, unmapped keys)
- Lock-free table swap from UI thread to audio thread
- A4 reference frequency parameter (400–480 Hz) applied on top of loaded tuning
- 8 built-in tunings: 12-TET, Just Intonation (5-limit), Pythagorean, Pelog, Slendro, Harmonic Series, Wendy Carlos Alpha, Bohlen-Pierce
- File loading: user can load .scl/.kbm from disk via controller
- Kit presets store tuning data (file references or inline table)

**Exit criteria:** Loading a Pelog .scl file remaps pad frequencies to Javanese gamelan intervals. Built-in tunings selectable from UI. A4 reference shift works. Tuning persists across kit save/load.

**Spec reference:** Microtuning

---

### Phase 8 — UI

**Goal:** Full VSTGUI interface with 3-column layout.

**Scope:**
- Overall layout: pad grid (left) | tabbed pad editor (center) | kit controls (right)
- Pad grid (custom CView, 4×8):
  - Note name, pad label, output indicator, mute icon
  - Choke group color on border (16 colors)
  - Velocity animation (flash on MIDI trigger)
  - Click to select, right-click context menu (rename, copy, paste, clear, mute)
- Pad editor (6 tabs): Exciter, Body, Unnatural, Tone, Amp, Routing
  - Dynamic parameter visibility based on exciter/body type selection
  - Uses shared UI components (ArcKnob, ToggleButton, ADSRDisplay)
- Kit-level controls: cross-pad coupling matrix view, master volume, master tune, polyphony, stealing policy
- Kit browser (modal popup):
  - Category tree (Factory/User/subcategories)
  - Drag-and-drop pad presets onto grid
  - Load Entire Kit / Save Kit / Save Pad buttons
  - Audition on click
- Pad templates: quick-start configurations (Kick, Snare, Tom, Hat, Cymbal, Perc, Tonal, 808, FX)

**Exit criteria:** All per-pad parameters editable from UI. Pad selection updates editor. Choke groups visually indicated. Kit browser loads/saves presets. Drag-and-drop replaces individual pad sounds.

**Spec reference:** UI (entire section), Pad Templates

---

### Phase 9 — Presets + Polish

**Goal:** Factory content, validation, release readiness.

**Scope:**
- Factory kit presets (target: 8–16 kits covering acoustic, electronic, exotic, FX)
- Factory pad presets (target: 50–100 individual sounds across all templates)
- Per-pad preset save/load via PresetManager
- Kit preset save/load (all 32 pads + kit-level settings + tuning)
- State save/load (full plugin state for DAW session recall)
- Pluginval validation (strictness level 5)
- Clang-tidy clean
- Zero compiler warnings
- Performance profiling: validate <2% CPU target at 8 voices × 16 partials
- Cross-platform build verification (Windows, macOS, Linux)

**Exit criteria:** Pluginval passes at level 5. Zero warnings. CPU target met. Factory presets cover the range from acoustic drum kit to experimental synthesis. State recall is bit-accurate.

**Spec reference:** Performance Targets, Real-Time Safety, Pad Templates

---

### Post-Launch — DMSP Integration

**Goal:** Offline differentiable synthesis tooling for preset creation and sound matching.

**Scope:** See "Planned: DMSP Integration" section. Not part of the initial release.

## Version History

| Version | Date       | Notes                    |
|---------|------------|--------------------------|
| 0.1     | 2026-04-07 | Initial brainstorm spec  |
| 0.2     | 2026-04-07 | Added existing building blocks analysis |
| 0.3     | 2026-04-07 | Added physics reference, refined body models, snare wires, cymbal hybrid, 808 template, nonlinear pitch, FM ratios |
| 0.4     | 2026-04-07 | Output routing: 16 stereo pairs (industry standard), per-pad assignment, exclusive routing |
| 0.5     | 2026-04-07 | No modulation or per-pad effects — focus on synthesis, effects handled externally |
| 0.6     | 2026-04-07 | Choke groups: 16 groups with priority-based direction matrix (0–31), mutual + one-way + multi-tier |
| 0.7     | 2026-04-07 | Double membrane coupling (batter + resonant + air cavity), full Scala microtuning (.scl/.kbm) |
| 0.8     | 2026-04-07 | Von Karman nonlinear mode coupling: two params (Amount + Cascade Speed), modal projection implementation, Stormer-Verlet integration |
| 0.9     | 2026-04-08 | UI design: 3-column layout, 6-tab pad editor, pad grid with choke colors/velocity anim, modal kit browser with drag-and-drop |
| 0.10    | 2026-04-08 | Planned DMSP integration: offline preset pipeline, Match Sound feature, optional neural param predictor |
| 0.11    | 2026-04-08 | Implementation roadmap: 9 phases from scaffold to polish, each suitable for speckit spec |
