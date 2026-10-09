# Vorago — User Manual

## Introduction

Vorago is a **dark-ambient drone instrument**. Each voice is not an oscillator but a small
environment: a 64-partial harmonic cloud and a living noise organism, coloured by a network of
wandering resonant peaks, sustained by six coupled feedback loops, and spoken through two blended
acoustic bodies. Underneath every voice runs an **ecosystem** of agents exchanging energy and a
scheduler of **slow events** that fire every 20 to 90 seconds. Nothing in it is scripted or looped:
every movement comes from bounded stochastic processes and from the agents feeding on each other.
The instrument sinks rather than floats: a subharmonic engine, a spectral fog stage and a cavern-scale
space are part of the signal path, not effects appended to it.

Vorago is the dark sibling of Seraphis. Where Seraphis is weightless, upper partials and shimmer,
Vorago is subterranean: subharmonics, stone and steel bodies, pressure, and geological time scales.

**Vorago is slow by design. Hold the note.** With the default envelope the first sound surfaces a few
seconds after the key goes down and the drone keeps swelling for about two and a half minutes
(the four envelope stages add up to 155 seconds). A newcomer who taps a key and hears nothing for
two seconds has not found a fault; they have found the instrument. Thirty-nine of the forty-two
factory presets carry that default; *Sudden Chasm* opens within about ten seconds, *Growth Ring*
in a minute, *Slow Bloom* in two.

**Formats:** VST3 (Windows, macOS, Linux) and Audio Unit (macOS). One MIDI input, one stereo output.

## Signal Flow

```
MIDI in ──────────── notes, CC64 sustain, channel aftertouch
  │
  ▼
Identity layer ───── per voice, control-rate: the ecosystem (up to 48 agents),
  │                  two slow-event schedulers, breathing and tide
  ▼
Harmonic Cloud ───── 64 additive partials + bloom children spawned from the strongest peaks
  + Noise Organism ─ four noise sources, each through resonator → comb → wandering filter
  │
  ▼
Voice Envelope ───── gates the cloud + noise only; nothing downstream is gated
  │
  ▼
Resonance Network ── 12 resonant peaks wandering in frequency, Q, gain and pan
  │
  ▼
Feedback Ecology ─── 6 coupled micro-loops (filter → delay → resonator → DC blocker)
  │
  ▼
Acoustic Bodies ──── two continuous bodies (stone, steel, hull, …) blended per sample
  │
  ▼
Voice sum ────────── + ghost tap: granular memories of the drone, an octave down
  │
  ▼
Subharmonic Engine ─ ÷2, ÷4 and fifth-below, tracking the lowest sounding voice
  │
  ▼
Spectral Smear ───── STFT fog: per-bin magnitude memory + phase decoherence
  │
  ▼
Cavern ───────────── stone early reflections → diffusion → FDN → spectral damping
  │                  → moving dampers → mix (the space is in the path)
  ▼
Output ───────────── master gain → tape saturator → true-peak limiter (always on)
```

The twelve **macros** reach across every stage of this chain at once, and the **identity layer**
runs underneath it for as long as the plugin is alive. Both have their own sections below. The
interactive diagram on the landing page shows every connection.

## Interface Overview

The editor is a fixed 1100 × 760 window built around the life of the sound.

- **The header bar** — the master **Gain** slider, the **Voices** selector, the **Seed** selector,
  the **Saturation** slider and the preset browser button.
- **The concept band** — twelve large macro rings, six on each side of the centre: Darkness, Age,
  Density, Movement, Gravity and Entropy on the left; Pressure, Weight, Fog, Life, Depth and Mass on
  the right. These are the intended playing surface.
- **The ecosystem view** — the centre of the band is a live view of the focus voice's habitat: each
  agent is a glowing point on the torus, energy exchange draws fading links between them, and a small
  readout shows the active voices. With no voice sounding the view shows a dim grid. The view only
  reads; nothing you do in it changes the sound.
- **The page strip** — below the band, seven pages hold every deep parameter: **Cloud, Noise,
  Resonance, Body, Sub / Smear, Space, Life**. Every parameter stays automatable from the host
  whichever page is showing.

Every control carries a tooltip — hover over anything for a one-line description.

## Macros

The twelve macros are the performance surface of Vorago. Each sweeps a curated set of targets across
the whole engine — voice, global chain and cavern at once. Two things are worth knowing:

1. **The deep parameters set the origin the macros travel from.** Moving a page parameter moves the
   *base* a macro's sweep starts at; the macro adds its travel on top, and each target setter clamps
   the sum. The two never fight over a value.
2. **All macros default to neutral** (0 for all, centre for Gravity). At neutral a macro contributes
   exactly nothing — the page parameters alone describe the sound.

Channel aftertouch is added to **Pressure**, so a pressure-sensitive keyboard leans into the
instrument without touching the knob.

The travel columns below start from the factory defaults; a different base travels the same distance
from wherever you set it.

### Darkness — *"Darkens the cloud tilt, the smear and the cavern"*

| Target | Travel at full Darkness |
|---|---|
| Cloud Tilt | −4 → −10 dB/oct |
| Space Darkness | 80 % → 100 % |
| Smear Tilt | 0 → −0.5 (the fog leans low) |

### Age — *"Wear: duller cloud, more body damping, a shorter cavern decay"*

| Target | Travel at full Age |
|---|---|
| Body Damping | 25 % → 80 % |
| Space Decay | 20 s → 2 s |
| Cloud Tilt | −4 → −8 dB/oct |

### Density — *"Richer cloud, louder and more reactive noise, deeper bloom"*

| Target | Travel at full Density |
|---|---|
| Cloud Richness | 70 % → 98 % |
| Noise Wake | 35 % → 100 % |
| Noise Level | −18 → −12 dB |
| Bloom Depth | 60 % → 100 % |

### Movement — *"Drift, wander, breathing and damper motion"*

| Target | Travel at full Movement |
|---|---|
| Cloud Drift | 8 → 50 cents |
| Resonance Wander Rate | 0.03 → 1 Hz |
| Noise Wander Rate | 0.03 → 1 Hz |
| Breathing Depth | 30 % → 100 % |
| Space Damper Depth | 35 % → 80 % |

### Gravity — *"Pulls the resonances toward the harmonic grid"* (bipolar)

Gravity is the one **bipolar** macro: its centre is neutral. Turning it *up* is **stone** — the
twelve resonant peaks are pulled onto the played note's harmonic grid and locked to octaves, heavy
and settled. Turning it *down* is **air** — the peaks are released to float free of the note.

| Target | Air (full down) | Stone (full up) |
|---|---|---|
| Resonance Gravity | −1 | +1 |
| Resonance Octave Lock | off | on |

### Entropy — *"Mutation, inharmonicity, decoherence and irregular breathing"*

| Target | Travel at full Entropy |
|---|---|
| Cloud Mutation | 15 % → 70 % |
| Cloud Inharmonicity | 0.015 → 0.07 |
| Smear Decoherence | 20 % → 80 % |
| Breathing Irregularity | 30 % → 90 % |
| Cloud Drift | 8 → 20 cents |

### Pressure — *"Ecology feedback, output drive and saturation"*

| Target | Travel at full Pressure |
|---|---|
| Ecology Mix | 15 % → 50 % |
| Ecology Loop Gain | 80 % → 98 % |
| Output Saturation | 12 % → 100 % |
| Output Drive | 0 → +18 dB |
| Sub Level | 0 → −12 dB (the floor pulls tight) |

### Weight — *"Sub level, body blend and damping, a heavier spectral tilt"*

| Target | Travel at full Weight |
|---|---|
| Sub Level | 0 → +9 dB |
| Body Blend | 35 % → 80 % (toward material B) |
| Body Damping | 25 % → 50 % |
| Cloud Tilt | −4 → −8 dB/oct |

### Fog — *"Smear, ghosts, blur, tides and a foggier, darker cavern"*

| Target | Travel at full Fog |
|---|---|
| Smear | 20 % → 90 % |
| Smear Decoherence | 20 % → 60 % |
| Ghost Level | +40 % on the base |
| Ghost Blur | 85 % → 100 % |
| Space Fog | 30 % → 100 % |
| Space Darkness | 80 % → 95 % |
| Tidal Depth | 40 % → 80 % |

### Life — *"Ecosystem depth, event rate and bloom spawning"*

| Target | Travel at full Life |
|---|---|
| Eco Depth | 85 % → 100 % |
| Event Rate | 1× → 10× (fast events every 2–9 s) |
| Bloom Rate | one every 4 minutes → one every 40 s |

### Depth — *"Cavern mix, size, decay and width"*

| Target | Travel at full Depth |
|---|---|
| Space Size | 50 % → 95 % |
| Space Decay | 20 s → 45 s |
| Space Mix, Width | already at maximum by default; travel from a lower base |

### Mass — *"Body and resonator level, body resonance and sub tracking"*

| Target | Travel at full Mass |
|---|---|
| Body Resonance | 70 % → 98 % |
| Resonance Mix | 45 % → 15 % (the network recedes into the body) |
| Sub Level | 0 → +6 dB |
| Body Mix, Sub Tracking | already at maximum by default; travel from a lower base |

Age and Depth share the cavern decay: Age shortens it, Depth lengthens it. Weight, Pressure and Mass
all touch the sub level, and their offsets sum.

## Header Controls

| Control | Range | Default | Notes |
|---|---|---|---|
| **Gain** | 0 – 200 % | unity (centre) | Master output level of the whole instrument, smoothed |
| **Voices** | 1 – 6 | 4 | Maximum simultaneous voices. 5 and 6 are offered deliberately as an opt-in cost: a Vorago voice is deep and expensive (see *Performance Notes*) |
| **Seed** | Seed 1 – Seed 16 | Seed 1 | Reseeds every stochastic process — the cloud, noise, resonance, ecology, ecosystem and events of every voice, the ghost tap and the cavern. Changing it re-rolls the texture without moving a parameter |
| **Saturation** | 0 – 100 % | 12 % | Soft tape saturation in front of the final limiter. The Pressure macro rides on top of this base. **The true-peak limiter always stays on** |
| **Presets** | — | — | Opens the preset browser |

## The Cloud Page

The harmonic cloud is the additive core — 64 partials per voice — and the bloom engine that grows
new partials into it.

| Parameter | Range | Default | Description |
|---|---|---|---|
| **Richness** | 0 – 100 % | 70 % | Number of active partials and how fast they roll off |
| **Tilt** | −12 – +12 dB/oct | −4 | Spectral slope: dark below zero, bright above |
| **Mutation** | 0 – 100 % | 15 % | How quickly the partial amplitudes slowly mutate |
| **Inharm** | 0 – 0.1 | 0.015 | Stretches partials off the harmonic grid, toward bell and metal |
| **Drift** | 0 – 50 ct | 8 ct | Depth of the slow per-partial pitch drift |
| **Spread** | 0 – 100 % | 45 % | Stereo spread of the partial cloud |
| **Gravity** | −1 – +1 | +0.1 | Pulls partial energy toward the low (negative) or high (positive) end |
| **Bloom** | 0 – 100 % | 60 % | Depth of the bloom partials |
| **Bloom Rate** | 0 – 0.05 Hz | 1/240 Hz | How often new bloom partials spawn (default: one every four minutes) |

**Bloom** is the minutes-scale partial generator: it reads the strongest current peaks, spawns child
partials next to them, fades each in over tens of seconds, holds it, and lets it wither. Up to
sixteen children live at once. At the default rate a new one buds every four minutes; the Life macro
and the *Bloom Rate* knob bring that down to one every 20 seconds. The ecosystem's *partial* agent
drives bloom depth, and a scheduled event can trigger a bloom outright.

> **Tip:** Bloom needs sounding parents — with Richness below about 50 % there are few peaks to bud
> from. *Slow Bloom* keeps Richness at 70 % for exactly that reason.

## The Noise Page

The noise organism is the second sound source: four semi-independent noise sources, each feeding its
own resonator → comb → wandering-filter chain. It is summed into the cloud *before* the envelope,
so it is gated with the tone and coloured by everything downstream.

| Parameter | Range | Default | Description |
|---|---|---|---|
| **Noise Lvl** | −96 – +12 dB | −18 dB | Level of the noise layer |
| **Wake** | 0 – 100 % | 35 % | How strongly the noise follows the energy of the drone (the resting wake of every source) |
| **Wander** | 0.01 – 100 Hz (log) | 0.03 Hz | Rate of the slow wandering of the noise colour |

Each of the four slots then has its own column:

| Parameter | Range | Default | Description |
|---|---|---|---|
| **Model** | Direct / Filtered Wind / Granular Dust / Metallic Hiss | Wind, Dust, Direct, Hiss | The organism the slot grows: raw colour, a wind through a wandering filter, grains of dust, or a shifted metallic hiss |
| **Type** | White / Pink / Tape Hiss / Vinyl Crackle / Asperity / Brown / Blue / Violet / Grey / Velvet / Vinyl Rumble / Radio Static | Brown | The noise colour feeding the slot |
| **Freq** | 20 – 19845 Hz (log) | 60 Hz | Comb fundamental |
| **Spread** | 0 – 100 % | 35 % | Comb tooth spread |
| **Fdbk** | 0 – 90 % | 55 % (75 % for Metallic Hiss) | Comb feedback |

The ecosystem's four *noise* agents wake and sleep the four sources individually, and the Density
macro raises both the resting wake and the level. Four factory presets show one model each: *Dead
Air* (Direct), *Abyssal Wind* (Filtered Wind), *Spore Drift* (Granular Dust) and *Steam Vent*
(Metallic Hiss).

## The Resonance Page

Two stages share this page: the resonance drift network that colours the excitation, and the
feedback ecology that sustains it.

**Resonance Drift Network** — twelve resonant peaks whose frequency, Q, gain and stereo position
wander on slow, bounded, individually seeded lanes.

| Parameter | Range | Default | Description |
|---|---|---|---|
| **Res Grav** | −1 – +1 | 0 | In Hybrid mode, pulls each peak from its free anchor onto the played note's grid (positive) or mirrors it away from the note (negative) |
| **Res Mix** | 0 – 100 % | 45 % | Level of the resonator bank in the mix |
| **Res Wander** | 0.002 – 1 Hz (log) | 0.03 Hz | Rate at which the resonators wander in pitch |
| **Anchor** | Free / Keyed / Hybrid | Hybrid | Free peaks ignore the note; Keyed peaks are retuned to the played pitch; Hybrid sits between the two, where Res Grav decides how far |

**Feedback Ecology** — six coupled micro-loops. Each is a filter → delay → resonator → DC-blocker
chain fed back into itself and, far more faintly, into its neighbours. An RMS governor holds the
population below self-oscillation at every setting, so the loops sustain without ever running away.

| Parameter | Range | Default | Description |
|---|---|---|---|
| **Eco Mix** | 0 – 100 % | 15 % | Level of the ecology feedback loops |
| **Loop Gain** | 0 – 90 % | 72 % | Feedback gain of the loops |
| **Loop 1–6 Filter** | Lowpass / Bandpass / Highpass | Lowpass | The filter in each loop |

The ecosystem's *resonator* agents wake individual peaks (and lift their level and wander), its
*feedback* agents wake individual loops, and the slow events do the same in sudden swells. The
Pressure macro pushes the ecology forward and tightens the loop gain.

> **Tip:** Different filters in different loops are what turn the ecology from a hum into a
> population. *Feedback Mire* runs Lowpass, Bandpass and Highpass twice each.

## The Body Page

Every voice speaks through **two continuous bodies at once** — physical resonators driven by a
sustained stream rather than struck — blended per sample. Both bodies run at every blend value, so
there is never a switch or a click, only a weighting.

| Parameter | Range | Default | Description |
|---|---|---|---|
| **Material A** | Glass / Strings / Metal Plate / Chamber / Ice / Stone Chamber / Steel Tank / Wooden Hull / Cathedral Column / Cavern Wall / Glass Sphere | Stone Chamber | First body material |
| **Material B** | same list | Steel Tank | Second body material |
| **Blend** | 0 – 100 % | 35 % | Blend between material A (0) and B (100) |
| **Damping** | 0 – 100 % | 25 % | Damps the bodies' high modes, darker and shorter |
| **Resonance** | 0 – 100 % | 70 % | How sharply and how long the bodies ring |
| **Body Mix** | 0 – 100 % | 100 % | Level of the bodies in the mix (down for the raw network) |

The six darker materials — Stone Chamber, Steel Tank, Wooden Hull, Cathedral Column, Cavern Wall and
Glass Sphere — exist for Vorago; the first five are shared with Seraphis. The Weight macro pulls the
blend toward the heavier material B; Mass sharpens the resonance of both.

The voice envelope also lives on this page. It gates **only the excitation** (cloud + noise):
resonance, ecology, bodies, ghosts and the cavern all ring past note-off, and the voice retires on
its own level detector once it has fallen silent.

| Parameter | Range | Default | Description |
|---|---|---|---|
| **Env Mode** | Standard / Growth | Standard | Four timed stages, or one slow logistic swell |
| **Stage 1 – 4** | 0 – 120 s each (log) | 20 s, 30 s, 45 s, 60 s | The four standard stages: the attack, then three plateaus the sound climbs through. 155 s to full by default |
| **Release** | 0 – 120 s (log) | 45 s | Release after note-off |
| **Growth** | 1 – 120 s (log) | 120 s | Length of the Growth-mode rise |

**Growth mode** replaces the staged attack with a single logistic swell — the note *arrives*. It is
the engine behind *Growth Ring*. Velocity scales the whole envelope.

## The Sub / Smear Page

Two global stages, after the voices are summed.

**Subharmonic Engine** — three synchronous dividers tracking the lowest sounding voice (÷2 one
octave down, ÷4 two octaves down, and a fifth below the fundamental), filtered, saturated and
DC-blocked, then *added* to the untouched dry path. When no voice sounds the last pitch is held, so
the floor never jumps.

| Parameter | Range | Default | Description |
|---|---|---|---|
| **Sub Level** | −24 – +24 dB | 0 dB | Level offset of the whole sub layer (where Weight, Mass and Pressure act) |
| **Tracking** | 0 – 100 % | 100 % | How closely the sub follows the played pitch |
| **Oct Down** | −60 – +6 dB | −18 dB | Level of the sub one octave below |
| **2 Oct Down** | −60 – +6 dB | −24 dB | Level of the sub two octaves below |
| **Fifth Down** | −60 – +6 dB | −30 dB | Level of the sub a fifth below |

**Spectral Smear** — the fog stage: an STFT in which each frequency bin remembers its recent
magnitude (lows for longer than highs) and has its phase decorrelated.

| Parameter | Range | Default | Description |
|---|---|---|---|
| **Smear** | 0 – 100 % | 20 % | Amount of spectral smearing |
| **Decohere** | 0 – 100 % | 20 % | Phase decoherence of the smeared spectrum |
| **Smear Tilt** | −1 – +1 | 0 | Weights the smear toward low (negative) or high (positive) bands |

The tide of every voice folds into the smear amount, so the fog thickens and thins on a minutes-long
cycle even with the knob still. The Fog macro is mostly this stage plus the cavern's own fog.

## The Space Page

The cavern: an enormous underground bunker, in the signal path. Sparse stone-flavoured early
reflections feed a diffusion network and an FDN reverb with spectral damping, whose damping filters
wander under Brownian drift — the "moving dampers" that keep a sixty-second tail from ever sounding
like a static room.

| Parameter | Range | Default | Description |
|---|---|---|---|
| **Size** | 0 – 100 % | 50 % | Size of the reverb space |
| **Darkness** | 0 – 100 % | 80 % | High-frequency damping of the space |
| **Decay** | 0.5 – 60 s (log) | 20 s | Reverb decay time |
| **Fog** | 0 – 100 % | 30 % | Diffusion haze inside the reverb |
| **Damp Depth** | 0 – 100 % | 35 % | Depth of the moving dampers |
| **Damp Rate** | 0 – 100 % | 15 % | Rate of the moving dampers |
| **Mix** | 0 – 100 % | 100 % | Wet level of the space (equal-power against the dry) |
| **Width** | 0 – 100 % | 100 % | Stereo width of the late field |
| **Density** | 0 – 100 % | 75 % | Echo density of the reverb |
| **Dimension** | 0 – 100 % | 50 % | Dimensionality of the reverb network |
| **Breath** | 0 – 100 % | 50 % | Slow breathing of the reverb size |
| **Early Size** | 80 – 300 ms (log) | 220 ms | Spread of the twelve early reflections |
| **Early Lvl** | 0 – 100 % | 80 % | Level of the early reflections |
| **Absorb** | 0 – 100 % | 60 % | Stone absorption of the early reflections (each tap has its own) |
| **Early Send** | 0 – 100 % | 70 % | How much of the early reflections feeds the tail |
| **Freeze** | on/off | off | Hold the reverb tail indefinitely, conserving its energy |

Note that **Mix defaults to 100 %** — by default you hear the instrument *through* the cavern. The
space is excited by the early reflections only, so there is a deliberate short gap before the late
field blooms. Freeze is not saved into presets as *on*; it is a gesture you make once the drone has
bloomed (*Cathedral Void* is written for it).

## The Life Page

The identity layer: the ecosystem, the slow events, the ghosts and the life modulators.

**Ecosystem** — up to 48 agents of five kinds (partial, resonator, noise, feedback, ghost) live on a
toroidal habitat, holding energy and exchanging it with each other, with a resource field and with
a pool; nothing is ever created or destroyed. Each agent publishes one *wake* value that the voice
routes into its own destination: a bloom slot, a resonant peak, a noise source, a feedback loop or
the ghost tap. The ecosystem view in the concept band shows this colony live.

| Parameter | Range | Default | Description |
|---|---|---|---|
| **Event Rate** | 0.1 – 10× (log) | 1× | Rate multiplier for the slow events: the fast scheduler fires every 20–90 s and the slow one every 3–10 minutes at 1× |
| **Eco Depth** | 0 – 100 % | 85 % | How strongly the ecosystem agents shape the sound |
| **Eco Sync** | 0 – 0.5 | 0 | How strongly the colony's agents fall into step |
| **Self Affin** | −2 – +2 | −1 | How much each agent kind attracts (positive) or repels (negative) its own kind |

**Slow events** are the "something happens" clock: a seeded scheduler draws a wait of tens of
seconds to minutes, then plays one smooth attack / hold / release swell into one destination — a
bloom trigger, a noise wake, a peak wake, a loop wake or a ghost burst. Two run per voice, a fast one
and a slow one. Raise *Event Rate* or the Life macro and the drone visibly moves more often.

**Ghosts** — a global granular tap that records the raw voice sum and plays back twelve-second
memories of it an octave below, blurred and wide. A ghost is an event, not a wash: its level is
driven by the ecosystem's ghost agent and by ghost-burst events, so with the knob up you hear them
surface and sink rather than sit there.

| Parameter | Range | Default | Description |
|---|---|---|---|
| **Ghost Lvl** | 0 – 100 % | 0 % | Peak level of the ghost echoes (off by default; the Fog macro raises it) |
| **Ghost Blur** | 0 – 100 % | 85 % | How blurred the ghost echoes are |
| **Reverse** | 0 – 100 % | 0 % | Probability that a ghost plays reversed |
| **Ghost Events** | on/off | off | Let spontaneous events trigger ghosts on top of the base density |

**Life modulators** — the slow autonomous processes that animate the voice.

| Parameter | Range | Default | Description |
|---|---|---|---|
| **Breathing** | 0 – 100 % | 30 % | Depth of the breathing modulation (it also breathes the sub layer) |
| **Irregular** | 0 – 100 % | 30 % | Irregularity of the breathing |
| **Tidal** | 0 – 100 % | 40 % | Depth of the slow tidal modulation (it folds into the smear) |

## Presets

Click **Presets** in the header to open the browser. Categories appear as tabs; double-click a preset
to load it. Loading restores every parameter; **Save** writes the complete current state into your
user preset folder:

- **Windows:** `C:\ProgramData\Krate Audio\Vorago\{Category}\`
- **macOS:** `/Library/Application Support/Krate Audio/Vorago/{Category}/`
- **Linux:** `~/.local/share/Krate Audio/Vorago/{Category}/`

Loading a preset never reseeds an unchanged seed, so the drone you are holding is not re-rolled
under you.

### Factory Library

42 presets ship with the plugin across seven categories. Every one is rendered and verified during the
build: each is proven to make sound, stay bounded, be measurably distinct from the others, and to
make its headline feature audible. Each loads with its own note in the browser; all but three need the
same patience — **hold the note; the sound surfaces after a few seconds and keeps swelling for about
two and a half minutes.**

#### Drones
| Preset | Character |
|---|---|
| Crowded Dark | A thin drone that fills to bursting, partials and embers packing the dark shoulder to shoulder |
| Drifting Strata | Layers of drone sliding over one another like slow geological strata under a rolling tide |
| Monolith | A single vast mass of sound, body and sub fused into one unmoving block of stone |
| Slow Bloom | A sparse drone that takes two minutes to open, budding new partials as it breathes |
| Strung Abyss | Vast slack strings stretched across a chasm, humming a low, dark, sustained chord |
| Chamber Drone | A warm, closed wooden chamber filled by one breathing, slowly blooming drone |
| Hull Ark | The creaking wooden hull of an ark rolling on a dark sea, its timbers groaning with the drone |

#### Abyss
| Preset | Character |
|---|---|
| Tectonic Floor | A fifth below the floor: slow plates of sub grind under a dark, close cloud |
| Lightless | A bright cloud pressed down into lightless black, every overtone smothered as it sinks |
| Stone Gravity | Resonant peaks dragged down and locked to octaves, heavy as stone settling in the deep |
| Weighted Deep | Two octaves under the drone a sub swells up, pulling the whole cavern down with it |
| Sudden Chasm | The ground gives way at once: a dark, full drone that opens beneath you within about ten seconds |
| Abyssal Wind | Wind howling up out of a bottomless pit over a deep sub and slowly wandering peaks |

#### Caverns
| Preset | Character |
|---|---|
| Resonant Shaft | A deep vertical shaft whose walls sing back the played notes in slow, drifting peaks |
| Cathedral Void | A vast, slow nave around a wide cloud. Engage Freeze once the drone has bloomed to hold the space |
| Endless Descent | A drone falling into a cavern that keeps opening beneath it, each echo farther down than the last |
| Glass Well | A ringing glass shaft: a bright, barely damped body singing into a pale cavern |
| Column Hymn | Tall stone columns in a sunken cathedral, each one singing its own grave, sustained hymn |
| Cavern Wall | Rough, wet rock walls close around a heavy drone, thudding back a dull, massive resonance |

#### Organisms
| Preset | Character |
|---|---|
| Colony Pulse | A restless colony of small voices, quickening and feeding on each other in the dark |
| Teeming | A dense colony teeming in the dark, many small lives waking slowly and feeding on the drone |
| Bloom Colony | A sparse drone where a hidden colony swells new partials into bloom and lets them wither |
| Singing Colony | Free-floating resonant peaks that a colony wakes into song, one voice rising as another fades |
| Growth Ring | One slow organic growth, a minute long, that keeps budding new partials as it breathes |
| Spore Drift | Clouds of spores sifting through the dark, their grains drifting backwards into humming loops |

#### Machines
| Preset | Character |
|---|---|
| Feedback Mire | Choked feedback loops churning in a sump of rust, each filtered to its own grinding band |
| Hull Resonance | The inside of a vast steel and stone hull, every plate ringing under the drone |
| Entropic Hum | A clean machine hum slowly coming apart, its partials skewing and fraying into noise |
| Pressure Front | A compressed wall of pressure: loops churning and the floor pulled tight into a dense, saturated hum |
| Feeding Loops | Feedback loops that wake and feed on one another, flaring into a grinding chorus and sinking back |
| Iron Plate | A great iron plate struck by the drone, ringing with clanging, inharmonic overtones |
| Steam Vent | Metallic steam hissing from rusted vents over a grinding deep sub and whistling loops |

#### Textures
| Preset | Character |
|---|---|
| Wind Through Basalt | Raw wind and hiss pouring through cracks in black stone over a thin, dark tone |
| Smeared Horizon | A drone dissolved into a wide, blurred haze where every partial bleeds into the next |
| Erosion | Weathered stone crumbling to dust: crackle and grit wearing a ringing drone down to a dull husk |
| Swarm Breath | Hiss and glassy air stirred by a swarm, flaring and settling as the colony breathes |
| Ice Shelf | A glittering shelf of ice, bright shards of tone cracking and shimmering in the cold |

#### Ghosts
| Preset | Character |
|---|---|
| Choir of Absence | Voices that are not there: reversed fragments of the drone surfacing and sinking away |
| Fogbound | Static and rumble drifting through a thick fog, the drone heard only as a ghost of itself |
| Haunted Colony | A dark room where a colony calls up ghosts of the drone in sudden, blurred bursts |
| Glass Sphere | A hollow sphere of glass drifting in fog, ringing faintly with the ghost of a far-off drone |
| Dead Air | A dead channel hissing in an empty room, tape hiss and static where a voice should be |

## Performance Notes

- **Be patient.** Hold the note. With the default envelope the sound surfaces after a few seconds and
  reaches full after about two and a half minutes; the release is 45 seconds, and the cavern, the
  ecology and the ghosts ring on past it. Judge a patch over minutes, not bars.
- **Latency:** Vorago reports a constant latency in every state — the spectral smear's 2048-sample
  STFT plus the cavern's 1024-sample spectral stage, 3072 samples in all. It never changes with
  settings, so the host compensates once and stays correct.
- **Voices are the CPU lever.** A Vorago voice is deliberately deep (a 64-partial cloud, four noise
  organisms, twelve resonators, six feedback loops, two bodies and an ecosystem each). The default of
  4 voices is budgeted at under 30 % of one core; 5 and 6 voices are offered as an opt-in cost. A drone
  instrument is played with one or two held notes — reduce **Voices** before stacking instances.
- The audio path allocates no memory, takes no locks and performs no I/O while running; everything is
  sized up-front when the host prepares the plugin.
- **Everything is bounded.** Every stochastic process is mean-reverting and slew-limited, the
  feedback ecology has an energy governor, the sub engine a backstop gate, and the output always ends
  in a true-peak limiter at −0.3 dB. A drone left running overnight neither dies nor explodes.
- With the editor closed the ecosystem view costs nothing; the processor publishes a frame only while
  a view is attached and only when the habitat has changed.

## Cross-Platform

| Platform | Formats | Notes |
|---|---|---|
| Windows 10+ | VST3 | 64-bit |
| macOS 12+ | VST3, Audio Unit | Universal (Intel + Apple Silicon) |
| Linux | VST3 | x86-64 |

State saved on any platform loads identically on the others.

## Tips & Tricks

- **Start from neutral.** With all twelve macros at rest the pages describe the sound. Design the
  patch there, then use the macros as the performance surface on top — they always travel *from*
  what you set.
- **Seed is a free variation knob.** Same patch, different seed — a genuinely different colony, a
  different set of wanderings, a different cavern — without moving a single parameter.
- **Freeze is a gesture, not a setting.** Hold a chord, wait for the cavern to bloom, engage
  **Freeze**, release — the space holds the chord while you play something new over it.
- **Play Pressure with your hands.** Channel aftertouch is added to the Pressure macro: lean into a
  held note and the loops churn, the drive rises and the floor tightens; ease off and it opens again.
- **Let the events work.** If a patch feels static, do not reach for a modulation knob — raise *Event
  Rate* (or Life) and let the schedulers wake peaks, loops and blooms on their own clock. One event
  every 20–90 s is the default; 10× is a different animal.
- **The two bodies are a crossfade, not a switch.** Set Material A and B to two different worlds
  (Glass Sphere against Cavern Wall) and ride *Blend*, or let Weight pull it for you.
- **Ghosts are off by default.** Turn *Ghost Lvl* up on the Life page, or raise Fog, and the drone
  starts remembering itself — an octave down, blurred, and reversed if you let it.
- **Bipolar knobs rest at centre:** the Gravity macro, Cloud Gravity, Res Grav, Smear Tilt and Self
  Affinity are all neutral in the middle — automation that "does nothing" is usually sitting on one
  of these centres.
- **Long tails stack.** The envelope release (45 s), the cavern (up to 60 s), the ecology and the
  ghosts all overlap; when a patch turns to mud, shorten the cavern decay first, then Age the body.

---

*Vorago is free and open-source software from Krate Audio, released under the MIT license.*
