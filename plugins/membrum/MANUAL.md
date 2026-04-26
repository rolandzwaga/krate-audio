# Membrum — User Manual

## Introduction

Membrum is a 32-pad drum machine that synthesises every hit from first principles — there are no samples. Each pad pairs one of six excitation models (impulse, mallet, noise burst, friction, FM impulse, feedback) with one of six body models (membrane, plate, shell, string, bell, noise body), giving 36 base voice fabrics that you then sculpt with parallel noise + click layers, per-mode damping, air loading, head/shell coupling, nonlinear tension modulation, and an Unnatural Zone of inharmonic distortions. Five per-pad macros (Tightness, Brightness, Body Size, Punch, Complexity) collapse all that into single-knob sound shaping when you don't want to dive into the underlying parameters.

The 32 pads are laid out as a 4×8 grid mapped to MIDI notes 36–67 with up to 16-voice polyphony, three voice-stealing policies, eight choke groups, sixteen output buses, and global cross-pad sympathetic resonance. 20 factory kits across Acoustic, Electronic, Percussive, and Unnatural subcategories ship out of the box.

### Quick Start

1. Load Membrum as an instrument on a MIDI track in your DAW.
2. Open the **Kit Browser** (right column) and pick one of the 20 factory kits — start with *Acoustic Studio Kit* or *808 Electronic Kit*.
3. Trigger pads with a MIDI keyboard (notes 36–67) or by clicking pads in the grid. The pad grid is laid out C1 = bottom-left, MIDI 36; row by row left-to-right; up to MIDI 67 at top-right.
4. Click any pad to **select** it — the selected-pad column shows its parameters.
5. Toggle **Acoustic / Extended** in the top-right of the selected-pad panel to switch between the focused natural-drum subset and the full extended parameter set.
6. Drag any of the five **macro** knobs to morph the sound holistically; underlying parameters update in real time.

---

## Signal Flow

Understanding the signal path makes parameter interactions predictable.

```
MIDI Input (notes 36-67, velocity)
        |
        v
  Pad Dispatch (early-exit if pad disabled)
        |
        v
  Choke Group Table (8 groups, fast-release on conflict)
        |
        v
  Voice Allocator (3 stealing policies, 5 ms exp fast-release)
        |
        v
  +------------ Per-Voice DSP (DrumVoice) -----------------+
  |                                                        |
  |  Exciter (Impulse / Mallet / Noise / Friction / FM /   |
  |           Feedback)  -- velocity-controlled spectrum   |
  |                  |                                     |
  |                  v                                     |
  |  Body (Membrane / Plate / Shell / String /             |
  |        Bell / Noise Body)  -- modal resonator bank     |
  |   + Per-mode damping (Phase 8A)                        |
  |   + Air loading (Phase 8C)                             |
  |   + Head/shell coupling (Phase 8D, secondary bank)     |
  |   + Tension modulation (Phase 8E, energy follower)     |
  |                  |                                     |
  |                  v                                     |
  |  Mode Inject -> Nonlinear Coupling                     |
  |                  |                                     |
  |                  v                                     |
  |  Tone Shaper: Drive -> Wavefolder -> DC Block          |
  |               -> SVF (LP/HP/BP) + Filter Envelope      |
  |   (Pitch envelope updates body fundamental per-sample) |
  |                  |                                     |
  |                  v                                     |
  |  Noise Layer + Click Layer (added in parallel)         |
  |                  |                                     |
  |                  v                                     |
  |  Amp Envelope (decoupled from voice lifetime)          |
  |                                                        |
  +-----------------------+--------------------------------+
                          |
                          v
       Output Bus Selector (per-pad, 0 = main, 1-15 = aux)
                          |
              +-----------+-----------+
              |                       |
              v                       v
        Main Bus L/R          Aux Bus 1-15 L/R
              |                       |
              v                       |
  Sympathetic Resonance               |
  (mono sum -> coupling delay         |
   -> matrix [global + snare buzz     |
    + tom resonance + per-pad amt]    |
   -> energy limiter -> mix back)     |
              |                       |
              v                       |
        Master Gain                   |
        (-24 .. +12 dB)               |
              |                       |
              v                       v
        Audio Output           Audio Output
        (Main Stereo)          (Aux 1-15, pre-master)
```

A more detailed graphical version is available as the [Signal Flow Diagram](signal-flow.html).

---

## Interface Overview

The window is divided into three columns, each scrollable on small displays:

- **Left — Pad Grid** — 4×8 pad grid (32 pads). Click to select; the grid glows per pad to reflect voice activity, level, and category color.
- **Centre — Selected Pad** — All parameters for the currently selected pad. The Acoustic/Extended toggle in the top-right switches between two layouts (see [Selected Pad Panel](#selected-pad-panel)).
- **Right — Kit, Voices, Coupling, Meter, Master** — Kit and per-pad preset browsers, voice management, cross-pad coupling, output meter, and master gain.

The top-right of the selected-pad column carries an **Acoustic / Extended** toggle that filters the displayed parameter set. The toggle is *session-scoped* — it does not persist with plugin state and does not affect DSP or automation. All parameters remain reachable via host automation regardless of which mode is active.

---

## Pad Grid

The pad grid maps the bottom-left pad to MIDI note 36 (C1), row by row, left to right, up to MIDI 67 (top-right). On a hardware controller this matches the standard 4×8 drum-pad layout used by most pad controllers.

| Action | Result |
|--------|--------|
| **Click pad** | Select the pad — its parameters appear in the centre column |
| **MIDI note 36–67** | Trigger the corresponding pad with the incoming velocity |
| **Click bottom-right power glyph** | Toggle the pad's *enabled* state (Phase 8F) — disabled pads silently ignore note-on without touching parameters |

### Pad Glow

Each pad glows in real time to reflect:
- **Brightness** — the current voice envelope level (decays as the voice releases)
- **Color** — the pad's *category* (kick, snare, hi-hat, tom, cymbal, perc, fx) — used by the kit-browser preview and the coupling system to dispatch sympathetic resonance edges (kick → snare for *Snare Buzz*, tom → tom for *Tom Resonance*).

---

## Selected Pad Panel

The selected-pad column has two layouts driven by the **Acoustic / Extended** toggle. Both layouts manipulate the same underlying parameters; the Acoustic layout simply hides the deeper synthesis controls so a natural-drum workflow stays uncluttered.

### Acoustic Mode

| Fieldset | Controls |
|----------|----------|
| **Macros** | Tightness, Brightness, Body Size, Punch, Complexity |
| **Body** | Material, Size, Decay, Strike Position, Level |
| **Pitch Envelope** | XY editor for the Hz-domain envelope (start, end, time, curve) |
| **Routing** | Output Bus selector, Choke Group selector |
| **Source** | Exciter type, Body model |

This is the recommended starting point for most kit work — the five macros plus the five Body knobs cover ~80% of typical sound design moves.

### Extended Mode

The Extended layout exposes everything in Acoustic mode plus:

| Fieldset | Controls |
|----------|----------|
| **Character** | Mode Stretch, Mode Inject, Decay Skew, Nonlinear Coupling |
| **Material Morph** | XY morph pad (start ↔ end), Duration, Curve |
| **Source** | Exciter type + secondary params (FM ratio, Feedback amount, Burst duration, Friction pressure), Body model |
| **Physics** | Material, Size, Decay, Strike, Level, Coupling Amount |
| **Tone Shaper** | Drive, Wavefolder, Filter (LP/HP/BP) cutoff/resonance/env amount, Filter ADSR |
| **Parallel Layers** | Noise Layer mix/cutoff/resonance/color/decay; Click Layer mix/contact/brightness |
| **Pitch Envelope** | (same as Acoustic) |
| **Routing** | (same as Acoustic) |

Phase 8 extended physics (per-mode damping, air loading, head/shell coupling, tension modulation) are surfaced as additional knobs in the Physics fieldset; see [Phase 8 Modal Augmentations](#phase-8-modal-augmentations).

---

## Macros

Five macro knobs per pad collapse multiple underlying DSP parameters into single sweeps. Each macro applies an *incremental delta* relative to the previous macro position, so neutral macros (0.5) produce zero adjustment. This means a freshly loaded preset is left untouched by neutral macros, and macros stay independent of host automation on the underlying parameters.

| Macro | Underlying Parameters Touched |
|-------|------------------------------|
| **Tightness** | Material (toward woodier), Decay (shorter), Decay Skew (faster high-mode falloff) |
| **Brightness** | Filter Cutoff (higher), Mode Inject (more upper-mode energy) |
| **Body Size** | Size, Air Loading, Secondary Size |
| **Punch** | Click Layer Mix, Click Brightness, Pitch-envelope amount |
| **Complexity** | Mode Scatter, Nonlinear Coupling, Coupling Amount, Tension Modulation |

The macro mapping is hand-curated per macro to walk through musically useful combinations rather than just lerping each underlying knob in isolation.

---

## Body Models

Each body model is a parallel modal resonator bank tuned to research-backed mode ratios. The exciter feeds energy into the bank; the bank's modes ring at frequencies determined by Size, Material, and Phase 8 corrections.

| Body | Modes | Mode Ratios | Typical Use |
|------|-------|-------------|-------------|
| **Membrane** | 48 | Bessel (circular drum head) | Kicks, snares, toms, hand drums |
| **Plate** | 48 | Vibrating-plate ratios | Side sticks, claves, cajón fronts |
| **Shell** | 32 | Cylindrical-shell modes | Side sticks, cajóns, secondary bank |
| **String** | — | Digital waveguide | Tubular bells, mbiras, drones |
| **Bell** | 16 | Chladni patterns | Cowbells, agogos, crotales, singing bowls |
| **Noise Body** | 40 + broadband | Inharmonic, dense | Hi-hats, crashes, rides |

### Body Parameters

| Control | Description |
|---------|-------------|
| **Material** | Sweeps the per-mode damping law from woody (high HF damping, fast decay of upper modes) to metallic (uniform damping, sustained ring) |
| **Size** | Exponential frequency scaling of the entire mode set (large = lower fundamental, small = higher) |
| **Decay** | Global decay-time scaling that preserves Material character |
| **Strike Position** | Center strike (0) excites fundamental modes; edge strike (1) excites higher-order modes for complex timbres |
| **Level** | Per-pad output gain |

---

## Exciters

The exciter shapes the energy input to the body. All exciters carry a velocity-controlled spectral response: soft hits are darker, hard hits are brighter with wider-band excitation.

| Exciter | Secondary Parameter | Character |
|---------|--------------------|-----------|
| **Impulse** | — | Single-sample impulse — sharp, clean attack. Reference exciter for kicks and rim shots |
| **Mallet** | Burst Duration | Raised-cosine contact pulse — longer, softer attack with more body tone. Toms, timpani |
| **Noise Burst** | Burst Duration | Filtered noise burst — the basis for snares, hi-hats, brushes |
| **Friction** | Pressure | Bowed/scraped excitation with controllable pressure — drones, brushes, singing bowls |
| **FM Impulse** | FM Ratio | Two-operator FM impulse — metallic, bell, digital cowbell |
| **Feedback** | Feedback Amount | Self-exciting loop with energy limiter — chaotic runaway-resonance percussion |

The exciter type is a **silent switch** when the pad is idle. If a voice is currently sounding, the type change is deferred to the next note-on so no audible click/discontinuity occurs.

---

## Phase 8 Modal Augmentations

Phase 8 introduced four orthogonal physics layers on top of the base modal resonator. They're exposed as per-pad knobs and stack independently of the Tone Shaper, Unnatural Zone, and Parallel Layers.

### Per-mode Damping (Phase 8A)

Two knobs (`b1`, `b3`) drive the Chaigne-Askenfelt (1993) damping law `R_k = b1 + b3·f²` per mode. The same body can be dialled from metallic ring (low `b3`) to woody thump (high `b3`) without touching its fundamental. Legacy `Decay` and `Material` still work as convenience derivations when the Phase 8A knobs are at their default position.

### Air Loading + Mode Scatter (Phase 8C)

Air loading applies a Rossing-tabulated low-mode frequency depression (5% at the fundamental, tapering toward zero by mode 12). It closes the *whistly / detuned-bar* failure mode on Membrane bodies and lets the lowest modes drop into realistic kick/tom sub-bass territory.

Mode scatter adds a small sinusoidal frequency dither to each mode for *natural imperfection* — useful on cymbals where pure harmonic ratios sound synthetic.

| Control | Description |
|---------|-------------|
| **Air Loading** | 0.0 = pure Bessel ratios; 1.0 = full Rossing curve |
| **Mode Scatter** | 0.0 = pure ratios; 1.0 = maximum dither |

### Head ↔ Shell Coupling (Phase 8D)

DrumVoice carries a second 24-mode Modal Resonator Bank (the *shell*) that runs in parallel with the primary bank and exchanges energy bidirectionally at block rate. The two-bank feedback loop is stability-clamped to a 0.25 effective maximum so its eigenvalue stays below 1 across all decay combinations.

| Control | Description |
|---------|-------------|
| **Coupling Strength** | 0 = primary only; up to 0.25 effective |
| **Secondary Enabled** | Toggle to bypass the secondary bank entirely |
| **Secondary Size** | Size of the secondary (shell) bank — typically smaller than the primary |
| **Secondary Material** | Material of the secondary bank — typically wood (~0.3) for acoustic kicks/toms or metallic (~0.85) for FM-bell hybrids |

### Tension Modulation (Phase 8E)

A 20 ms one-pole energy follower drives a block-rate frequency scale on the modal bank, reproducing the energy-dependent pitch glide observed in real toms and 808 kicks (Kirby & Sandler 2021, Avanzini & Rocchesso 2012). Depth scales by velocity² at note-on, so soft hits sound the same and hard hits bend up to ~2 semitones during the note.

| Control | Description |
|---------|-------------|
| **Tension Mod Amount** | 0 = no glide; 1 = full ~2-semitone glide on hard hits |

Tension modulation is **orthogonal to the scripted Pitch Envelope** — Pitch Envelope drives the fundamental on a fixed time curve regardless of energy, while Tension Modulation tracks the actual modal energy.

---

## Parallel Layers (Phase 7)

Two always-on layers run alongside the modal body and add together at the per-voice mix bus. Both are essential for realism — the modal body alone sounds *glass tap*; the noise + click layers restore the stochastic residual that real drum recordings carry.

### Noise Layer

A filtered-noise generator running parallel to the modal body. Useful for snare wires, hi-hat sizzle, cymbal hash, brush rasp.

| Control | Description |
|---------|-------------|
| **Mix** | 0 = silent; 1 = full level |
| **Cutoff** | SVF lowpass cutoff |
| **Resonance** | SVF Q |
| **Color** | Spectral tilt of the noise source (dark → bright) |
| **Decay** | Envelope decay time |

### Click Layer

A 2–5 ms raised-cosine filtered-noise burst that fires at note-on. The attack click that turns *body tone* into *drum hit*.

| Control | Description |
|---------|-------------|
| **Mix** | 0 = no click; 1 = prominent click |
| **Contact (ms)** | Click duration (longer = softer, mallet-like attack) |
| **Brightness** | Spectral content of the click (dark thud → bright stick) |

---

## Tone Shaper

A per-pad signal-shaping chain that processes the modal body output before the amp envelope. All controls are bypass-identity at their defaults.

```
body output -> Drive -> Wavefolder -> DC Blocker -> SVF Filter (with envelope) -> output
```

| Control | Description |
|---------|-------------|
| **Drive** | Alias-safe waveshaper — soft saturation through hard clip |
| **Wavefolder** | Waveshaping fold for harmonic richness; particularly effective on FM bodies |
| **Filter Type** | LP / HP / BP |
| **Filter Cutoff** | SVF cutoff frequency |
| **Filter Resonance** | SVF Q |
| **Filter Env Amount** | Envelope modulation depth applied to cutoff |
| **Filter Env A/D/S/R** | Dedicated 4-stage envelope for the filter |

### Pitch Envelope

A separate absolute-Hz envelope (start, end, time, exp/lin curve) drives the body fundamental for 808-style transient pitch sweeps. The XY display lets you drag the start and end points directly.

| Control | Description |
|---------|-------------|
| **Start (Hz)** | Initial fundamental at note-on |
| **End (Hz)** | Steady-state fundamental |
| **Time (ms)** | Sweep duration |
| **Curve** | Exponential (snap) ↔ Linear (audible glide) |

---

## Unnatural Zone

Per-pad inharmonic distortions of the modal body. All controls are bypass-identity at their defaults.

| Control | Description |
|---------|-------------|
| **Mode Stretch** | 0.5–2.0; stretches inter-mode frequency ratios — 1.0 is natural, > 1.0 is harmonically tighter, < 1.0 is sparser |
| **Decay Skew** | ±1.0; skews decay between low and high modes — positive = bright sustain, negative = dark sustain |
| **Mode Inject** | Phase-randomised injection bank — adds extra modal density on top of the main bank |
| **Nonlinear Coupling** | Energy-limited cross-mode coupling — produces FM-like sidebands that grow and decay with envelope energy |

### Material Morph

A 2-point envelope (10–2000 ms) that morphs the body's material parameter from a *start* state to an *end* state at note-on. Useful for evolving cymbals (dark stick attack → bright shimmer tail) and snare brush sweeps.

| Control | Description |
|---------|-------------|
| **Enabled** | Power toggle for the morph layer |
| **Start / End** | XY pad — the start point is at note-on, end is reached after `Duration` |
| **Duration** | 10–2000 ms |
| **Curve** | Linear, exponential, logarithmic, S-curve |

---

## Routing

Per-pad routing controls.

| Control | Description |
|---------|-------------|
| **Output Bus** | 0 = main stereo; 1–15 = aux stereo buses for separate DAW processing |
| **Choke Group** | 0 = no choke; 1–8 = group membership. Pads sharing a non-zero choke group cut each other on note-on (SoundFont 2.04 Exclusive Class semantics). 5 ms exponential fast-release prevents click |
| **Coupling Amount** | 0–1; per-pad participation in the global sympathetic resonance system. Higher = pad receives more sympathetic excitation from other pads |

---

## Kit Browser & Pad Browser

The right column carries two preset browsers:

| Browser | Loads | Affects |
|---------|-------|---------|
| **Kit Browser** | A full 32-pad kit | All pads, all globals, choke groups, output buses, coupling, master gain, optional UI mode |
| **Pad Browser** | A single pad's sound | Only the *selected* pad's sound parameters. Routing (choke, output bus, coupling amount) and macros are preserved on the destination pad |

Both browsers organise presets into subcategory subdirectories. The Kit Browser shows the four hardcoded subcategories (Acoustic, Electronic, Percussive, Unnatural). The Pad Browser shows drum-type subcategories (Kick, Snare, HiHat, Tom, Cymbal, Perc, FX).

User presets save into the same directory tree alongside the factory presets.

---

## Voices

Voice management section in the right column.

| Control | Range | Description |
|---------|-------|-------------|
| **Max Polyphony** | 4–16 | Number of active voice slots. Idle slots consume zero CPU |
| **Voice Stealing** | Oldest / Quietest / Priority | Which slot to steal when all are busy |

### Voice-Stealing Policies

| Policy | Selection Rule |
|--------|----------------|
| **Oldest** | The longest-running voice gets stolen. Cheapest (no per-block scan) and most musical for typical drum patterns |
| **Quietest** | The voice with the lowest current envelope level gets stolen. Best for sustained kits where you want long tails to decay rather than getting cut |
| **Priority** | The highest-pitched voice gets stolen first; protects kick/snare. Best for mixed-pitch kits where the low-end is musically critical |

When a voice is stolen, a 5 ms exponential fast-release envelope cross-fades the victim out so the click peak stays below -30 dBFS.

---

## Coupling

Cross-pad sympathetic resonance — when one pad is hit, its mono output is mixed into other pads' excitation paths according to a coupling matrix. Models physical phenomena like snare wires buzzing when the kick is hit, or toms ringing in sympathy with each other.

| Control | Range | Description |
|---------|-------|-------------|
| **Global Coupling** | 0–100% | Master amount for the entire coupling system |
| **Snare Buzz** | 0–100% | Amount of kick/tom energy routed to snare-category pads |
| **Tom Resonance** | 0–100% | Amount of tom energy routed to other tom-category pads |
| **Coupling Delay** | 0.5–2.0 ms | Propagation delay between hit and sympathetic excitation |

Per-pad **Coupling Amount** (in the Routing fieldset) controls each pad's individual participation. Disabled pads (Phase 8F) receive no coupling regardless of the matrix.

The coupling output passes through an energy limiter that caps it below -20 dBFS to prevent runaway feedback.

---

## Master & Meter

| Control | Range | Description |
|---------|-------|-------------|
| **Master Gain** | -24 to +12 dB | Final stereo bus gain. **Main bus only** — aux buses 1–15 are pre-master |
| **Meter** | Peak + RMS | Output level meter for the main stereo bus |

Default master gain is -6 dB so the kit hits peak ~-10 dBFS, leaving 6 dB of headroom for layering and bus processing.

---

## Factory Kits

20 factory kits ship with the plugin, organised into the four hardcoded browser subcategories.

### Acoustic (5)

| Kit | Character |
|-----|-----------|
| **Acoustic Studio Kit** | Mallet kick, wired snare, side stick, hi-hats, six toms, ride/crash. The reference acoustic kit |
| **Jazz Brushes** | Brush sweep + tap snare, soft mallet kick, ride-led cymbals, mid-pitched toms |
| **Rock Big Room** | Maxed punchy kick, snappy snare, big toms, bright cymbals, pronounced shell coupling |
| **Vintage Wood** | Wood-shell kick + rim-shot snare, smaller toms, woodblocks, cowbell. Tape-y drive throughout |
| **Orchestral** | Timpani toms with strong tension mod, bass drum, gongs, triangle, tubular bell, suspended cymbal roll |

### Electronic (5)

| Kit | Character |
|-----|-----------|
| **808 Electronic Kit** | Iconic 808 kick (boom-glide), 808 snares, hats, FM-bell perc, mallet toms with pitch sweep |
| **909 Drum Machine** | Tight 909 kick, snappy snare, sizzly hats, bright crash, clap |
| **LinnDrum CR-78** | Early-digital PCM-style hits + FM bell perc, cabasa, clave, dirty hats |
| **Modular West Coast** | Feedback kick, FM Plate snare, FM Bell perc, friction string drone, generative coupling |
| **Trap Modern** | Massive sub-808 kick (max tension mod), crispy layered snare, trap hat variants for fast rolls |

### Percussive (5)

| Kit | Character |
|-----|-----------|
| **Hand Drums** | Conga lo/hi/slap, bongo hi/lo, djembe bass/slap, cajón bass/slap, frame drum, shaker, wood block |
| **Latin Perc** | Claves, cowbells, agogos, timbales, cabasa, maracas, tambourine, triangle, guiro, vibraslap |
| **Tabla** | Bayan + dayan with strong tension-mod pitch bends, full bols (Tha, Na, Ge, Ka, Tete), tanpura drone |
| **World Metal** | Kalimba (8 pitches), mbira (4), bell tree, crotales, singing bowl, wood blocks, tingsha, temple bell |
| **Cajon and Frames** | Cajón bass/slap, snare side, frame drum, bodhran, dholak hi/lo, riq, pandeiro |

### Unnatural (5)

| Kit | Character |
|-----|-----------|
| **Experimental FX Kit** | FM kick with shell coupling, feedback snare, friction FX, metal hats with morph, inharmonic plate toms |
| **Glass Bell Garden** | 16 bell-bodied tones with metallic damping, graded fmRatio, friction-bowed sustains |
| **Drone and Sustain** | Friction strings + feedback drones with long decays for pad-like sustained tones |
| **Chaos Engine** | Maxed nonlinear coupling + tension mod + mode inject across 14 pads — intentionally chaotic |
| **Ghost Bones** | Sub-bell tones with very inharmonic mode stretch, sustain-dominant decay skew |

---

## Performance Notes

- **CPU budget**: ~1.25% per voice at 44.1 kHz (single-voice baseline). 8-voice worst-case is ~6%.
- **Real-time safety**: Zero allocations on the audio thread. ExciterBank and BodyBank pre-allocate every variant up front; voice switches via tagged-union dispatch with no virtual calls or per-sample branching.
- **Pluginval**: Strictness 5 verified on Windows.
- **macOS Audio Unit**: `auval -v aumu Mbrm KrAt` verified in CI.

---

## Cross-Platform

| Platform | Format | Install Path |
|----------|--------|--------------|
| Windows | VST3 | `C:\Program Files\Common Files\VST3\Krate Audio\Membrum\Membrum.vst3` |
| macOS | VST3 | `/Library/Audio/Plug-Ins/VST3/Membrum.vst3` |
| macOS | Audio Unit | `/Library/Audio/Plug-Ins/Components/Membrum.component` |
| Linux | VST3 | `~/.vst3/Membrum.vst3` |

---

## Tips & Tricks

- **Disable rather than delete**. The Phase 8F per-pad enable toggle silences a pad without losing its parameters, choke group, or output bus assignment. Useful for A/B-ing kit subsets without losing your work.
- **Macros first, knobs second**. The five macros are designed to walk through musically useful combinations. Sweep them first to find a sound you like, *then* dive into individual parameters to refine.
- **Acoustic mode for kit work, Extended for sound design**. The Acoustic mode hides ~70% of the parameters but keeps everything you need to shape natural drum sounds. Switch to Extended when you want to dial in air loading, head/shell coupling, mode stretch, etc.
- **Output Bus 1+ for parallel processing**. Route the ride or crash to bus 1 if you want to apply a separate reverb/EQ chain in your DAW without affecting the rest of the kit. Aux buses are pre-master, so your DAW's bus chain sees the unattenuated voice output.
- **Tension Modulation for natural toms**. Even at 0.2, the Phase 8E energy follower adds the classic "kerthump" pitch dip that distinguishes a real tom from a synthetic one. Combine with a short Pitch Envelope (~50 ms, ~+20 cents) for the iconic 808 boom-glide.
- **Coupling Delay > 1.5 ms for "big room" feel**. Longer coupling delays simulate larger physical rooms where sympathetic resonance has to travel further between drums.
- **Master Gain at -6 dB is intentional**. Don't push it back to 0 dB unless you're checking levels in isolation — the headroom matters once the kit is sitting in a busy mix with bus processing on top.
