# Concept: **a bass instrument built around “mass” rather than layers**

The name of this synth will be Profundum.

The central idea would be:

> **One bass voice that can change its physical character as it moves from pure sub → resonant bass → harmonic bass → distorted speaker-grabber, while always maintaining a controllable low-frequency core.**

Rather than presenting the user with **Sub + Harmonics + Texture**, I'd make the architecture more like a **single evolving bass organism**.

Something along these lines:

```text
                     ┌───────────────┐
 MIDI ──────────────►│   CORE VOICE  │
                     │               │
                     │  Fundamental  │
                     │  Body         │
                     │  Character    │
                     └───────┬───────┘
                             │
                    ┌────────▼────────┐
                    │    SPECTRAL     │
                    │    TRANSFER     │
                    └────────┬────────┘
                             │
              ┌──────────────┼──────────────┐
              ▼              ▼              ▼
          CLEAN LOW       BODY/MID       EDGE/TEXTURE
              │              │              │
              └──────────────┼──────────────┘
                             ▼
                       MASS PROCESSOR
                             │
                     ┌───────┴───────┐
                     ▼               ▼
                   WIDTH           MONO LOW
                     │               │
                     └───────┬───────┘
                             ▼
                          OUTPUT
```

## 1. The interesting bit: **Core**

Instead of selecting “sine / triangle / saw / square”, give the oscillator a **continuous waveform morphology**.

Imagine a waveform that has three conceptual dimensions:

### **DEPTH**

How much energy is concentrated into the fundamental.

`pure sub ←──────────────→ harmonic`

### **BODY**

Controls the distribution of the first ~8–16 harmonics.

`round ←──────────────→ nasal / hollow`

### **EDGE**

Introduces increasingly high harmonics and nonlinearities.

`smooth ←──────────────→ aggressive`

The three controls could produce a *huge* variety of basses without looking like another conventional synth oscillator.

And importantly, **Depth doesn't just mean EQ**. It actually changes the oscillator's harmonic generation.

So:

> Sine → rounded triangle-ish → saw-ish → asymmetric → clipped → increasingly complex

but continuously, rather than switching between oscillator types.

---

# 2. A dedicated **Sub Anchor**

This is where I'd borrow the philosophy behind Pure SUB, but simplify it.

Have a dedicated **Anchor** oscillator underneath everything.

It can be:

* sine
* triangle
* rounded sine
* asymmetric sine
* octave-down sine

But the interesting feature is:

### **Anchor follows the bass's perceived pitch**

If the main oscillator is heavily distorted, filtered or otherwise manipulated, the Anchor can remain mathematically stable.

So you get:

```text
              MAIN VOICE
                 │
        distortion / filtering
                 │
              chaos
                 │
                 ▼
              OUTPUT
                 ▲
                 │
            clean Anchor
```

This means you can absolutely destroy the bass without losing its fundamental.

But I'd make the Anchor **more clever than Pure SUB's Sub Regen**:

### Anchor Mode

**LOCK**

The fundamental remains completely stable.

**FOLLOW**

The anchor follows the processed voice's perceived fundamental.

**PUNCH**

The anchor becomes temporarily louder during the attack.

**GHOST**

The anchor disappears during the attack and gradually emerges underneath the bass.

That last one could be particularly interesting.

You could make extremely aggressive transient-heavy basses whose *actual* sub appears a few milliseconds later.

---

# 3. The big differentiator: **Spectral Transfer**

This is the feature I'd make the identity of the instrument.

Instead of:

> oscillator → filter

have:

> **oscillator → spectral redistribution**

You get a control called something like:

### **SHIFT**

which moves energy between harmonic regions.

Visually:

```text
SUB       BODY        PRESENCE
│           │             │
██████      ███           █
████        █████         ██
██          ███████       ███
```

Dragging a single point around determines where the bass's energy lives.

For example:

### Heavy

```text
███████████
██████
██
```

### Hollow

```text
████
████████
██
```

### Growl

```text
██
████
██████████
```

But this isn't simply EQ.

The spectral redistribution occurs **inside the oscillator generation**, meaning the harmonics are regenerated rather than merely filtered.

That gives you much more musical behavior when pitch changes.

---

# 4. **Mass**

Here's another unusual feature.

Give the instrument a macro called:

## MASS

This isn't volume.

It controls several psychoacoustic aspects simultaneously:

* fundamental strength
* low-mid harmonic density
* transient duration
* saturation
* compression
* sub/body relationship

So:

```text
MASS 0
│
pure, clean, sine-like
│
│
│
MASS 50
│
thick analogue bass
│
│
│
MASS 100
│
enormous distorted bass
```

But unlike a normal macro, I'd make it **continuous and predictable**.

The user can therefore think:

> "I want 20% more mass."

rather than:

> "Which of these 47 processing parameters do I need?"

That could become a major selling point.

---

# 5. A **Pitch Exciter**

Pure SUB has a dedicated pitch envelope, which is obviously useful for 808s and kick-like basses. ([Arturia][1])

I'd take it somewhere slightly different.

Instead of simply:

`pitch → envelope → pitch`

have:

## **DROP**

with three parameters:

**Amount**

How far the pitch falls.

**Time**

How quickly it falls.

**Shape**

Exponential ↔ linear ↔ logarithmic ↔ elastic.

But then add:

### **REBOUND**

The pitch can briefly overshoot the target pitch before settling.

For example:

```text
       /\
      /  \
─────/    \────────────
```

That would produce some really nice:

* 808 attacks
* cinematic bass impacts
* techno bass plucks
* synthetic kick/bass hybrids

without turning the instrument into an 808 clone.

---

# 6. Don't have a conventional filter section

This is one place I'd intentionally diverge from Pure SUB.

Instead of 40 filter modes, I'd have **three character processors**:

### ROUND

Low-pass-like behavior.

### HOLLOW

Band/reject/resonant behavior.

### BITE

High-frequency emphasis + nonlinear excitation.

Then one continuously variable:

## **SHAPE**

which morphs between them.

Internally you can do much more complicated things.

The user doesn't need to know.

---

# 7. **Harmonic Fold**

This could be particularly good.

A controlled nonlinear stage that doesn't behave like conventional waveshaping.

Imagine:

```text
HARMONIC FOLD

0 ─────────────── 100
```

At low values:

> soft saturation

Middle:

> harmonic compression

High:

> folding / alias-controlled nonlinear generation

But critically, **the sub anchor is excluded**.

So you can do:

```text
                 ┌── harmonic fold ──┐
OSCILLATOR ──────┤                   ├──► OUT
                 └───────────────────┘
                         +
                       ANCHOR
```

That means:

> filthy midrange + pristine sub

which is extremely useful in actual mixes.

---

# 8. Stereo should be frequency-dependent

Rather than a conventional stereo knob:

## **SPREAD**

with a little visual spectrum:

```text
20       100       500       2k       10k
│─────────│─────────│─────────│─────────│
MONO              ←──────→             WIDE
```

The low frequencies are permanently constrained toward mono, while upper harmonics can become increasingly stereo.

But I'd add:

### **MOTION**

The stereo field can slowly rotate / oscillate.

And:

### **GRAVITY**

which determines how aggressively the bass collapses toward mono as frequency decreases.

That gives you Reese-like width without allowing the actual sub to become stupidly unstable.

---

# 9. **Bass Personality**

This could replace having lots of oscillator presets.

Four large macro knobs:

```text
       WEIGHT
          │
          │
  DARK ───┼─── BRIGHT
          │
          │
       VIOLENCE
```

And perhaps:

### WEIGHT

Sub/fundamental/body.

### DARK ↔ BRIGHT

Spectral distribution.

### TIGHT ↔ LOOSE

Envelope, transient and compression behavior.

### CLEAN ↔ VIOLENT

Nonlinear processing.

Those four dimensions could essentially become the instrument's **macro sound space**.

And presets could store these coordinates.

---

# 10. The killer feature: **Bass Morph**

This is where I'd go beyond both reference products.

Let the user store **two complete bass states**:

```text
A                           B

PURE SUB                    DISTORTED REESE
   ●──────────────────────────●
             MORPH
              50%
```

But the morph isn't ordinary parameter interpolation.

The synthesis engine understands what is being morphed.

So:

```text
sine → complex oscillator
clean → nonlinear
mono → stereo
short → long
dark → bright
```

all happen perceptually.

And crucially:

### The Anchor can morph differently.

For example:

```text
A: clean sub
B: dirty sub

MORPH:
main voice     0 → 100%
anchor         100 → 70%
```

So you can automate a bass from:

> **massive clean sub**

to

> **violent distorted monster**

while the fundamental never disappears.

That would be an excellent automation target.

---

# 11. A **Motion** section

I'd keep this much smaller than Pure SUB's modulation architecture.

Three sources:

### ENV

One flexible envelope.

### CYCLE

LFO / synced rhythmic modulation.

### DRIFT

Slow random modulation.

And then **MODULATION DEPTH** becomes a global concept.

For example:

```text
          MOTION
             │
     ┌───────┼────────┐
     │       │        │
    ENV    CYCLE    DRIFT
     │       │        │
     └───────┼────────┘
             │
       modulation bus
```

But here's the fun bit:

## Motion can modulate *Mass*.

That means you can create basses that physically swell and contract.

---

# 12. A very useful **Note Behavior** section

Bass instruments have a few recurring problems.

I'd make these explicit controls:

### RETRIGGER

Oscillator phase behaviour.

### GLIDE

With:

* rate
* constant time
* quantized

### NOTE MEMORY

Controls whether the oscillator/envelope behaves like:

* always retrigger
* legato
* envelope retrigger only
* oscillator continuous

### LOW NOTE GUARD

A particularly useful feature.

As you approach the bottom of the keyboard, the instrument can progressively reduce harmonic density.

So an extremely low C doesn't suddenly become an enormous pile of aliasing and mud.

---

# 13. Processing architecture

I'd keep the processing chain deceptively small:

```text
                 ┌────── Anchor ────────┐
                 │                      │
OSCILLATOR ─────┤                      ├── MIX
                 │                      │
                 └─ Harmonic Engine ───┘
                           │
                       SPECTRAL
                        SHAPER
                           │
                       SATURATOR
                           │
                       CHARACTER
                           │
                       COMPRESSOR
                           │
                     STEREO FIELD
                           │
                         LIMIT
```

And I'd make the **Character** stage the fun one.

Maybe six algorithms:

* Tube
* Fold
* Rectify
* Bite
* Crush
* Fracture

But each has only:

`Amount + Tone`

rather than a giant FX rack.

Pure SUB already has 12 FX slots / 18 effects, so trying to compete there would just make your instrument another general-purpose bass workstation. ([Arturia][1])

---

# 14. The UI I'd actually build

I think this could look *very* different from Pure SUB.

### Main screen

```text
┌─────────────────────────────────────────────────────────────┐
│                                                             │
│                         B A S S                             │
│                                                             │
│                 ┌─────────────────────┐                     │
│                 │                     │                     │
│                 │     SPECTRUM        │                     │
│                 │                     │                     │
│                 │  ▂▃▅████▆▃▂         │                     │
│                 │                     │                     │
│                 └─────────────────────┘                     │
│                                                             │
│       WEIGHT       BODY       EDGE       MASS               │
│        ○────────    ○──────    ○──────    ○────────        │
│                                                             │
│       ANCHOR       SHIFT      FOLD       DROP              │
│        ○────────    ○──────    ○──────    ○────────        │
│                                                             │
│                                                             │
│     CLEAN ───────────── MORPH ───────────── VIOLENT         │
│                                                             │
└─────────────────────────────────────────────────────────────┘
```

Then an **Advanced** page exposes the actual synthesis architecture.

This gives you the Arturia idea of "deep engine underneath a small number of meaningful controls", without copying Pure SUB's three-layer UI.

---

# The conceptual distinction

I'd position the instruments roughly like this:

| Instrument               | Core idea                                                                                       |
| ------------------------ | ----------------------------------------------------------------------------------------------- |
| **Beatsurfing Low Bass** | Fast bass creation / immediate results                                                          |
| **Arturia Pure SUB**     | Dedicated multi-layer bass-design workstation                                                   |
| **Your instrument**      | **One physically coherent bass voice that morphs between clean weight and harmonic aggression** |

That last distinction is important.

I wouldn't sell it as:

> *"Another synth with an oscillator, sub oscillator, filter and distortion."*

I'd sell it as:

> **"A bass synthesizer where the entire sound is one controllable mass."**

And I'd make **Mass + Anchor + Spectral Shift + Morph** the four genuinely proprietary-feeling ideas.

The rest—envelopes, saturation, stereo, glide, distortion, etc.—are there because a bass instrument needs them, not because they're the selling point.

Given the instruments you've already been designing, I also think this would fit your lineup particularly well: **Innexus** is about reconstructing harmonic identity, **Ruinae** is about chaotic/harsh synthesis, **Disrumpo** is about distortion, and this could occupy the opposite territory: **extremely controlled, physical, low-frequency synthesis that can become surprisingly complex without ever losing its center.**

[1]: https://www.arturia.com/products/software-instruments/pure-sub/overview "Arturia - Pure SUB - Bass Design Synthesizer | Arturia"
