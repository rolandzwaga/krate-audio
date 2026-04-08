# Spec 135 — Membrum: Synthesized Drum Machine (Revised)

**Status:** Phase 1 Complete (see `specs/136-membrum-phase1-scaffold/`)  
**Plugin:** Membrum  
**Type:** Instrument (`aumu`)  
**Location:** `plugins/membrum/`

---

## Overview

Membrum is a synthesis-only drum machine plugin. No samples — every sound is generated in real time using modal synthesis, physical modeling, and spectral techniques.

At its core, Membrum models:

> **Exciter → Resonant Body → Interaction**

This produces drum sounds that behave like physical systems, but can also be pushed beyond reality into expressive, synthetic territory.

---

## Design Philosophy

Membrum is designed as **two instruments in one**:

### 1. Acoustic Mode (Default)
- Constrained, physically meaningful controls
- Fast, intuitive sound design
- Realistic drum behavior

### 2. Extended Mode
- Full parameter access
- “Unnatural Zone” unlocked
- Sound design and experimental exploration

This dual-layer approach ensures:
- Beginners get results quickly
- Advanced users get full control

---

## Architecture

### Pad Layout
- 32 pads (GM mapped)
- External MIDI (no sequencer)
- Per-pad + kit presets
- Separate outputs per pad

---

## Voice Management

- Polyphony: 4–16 (default 8)
- Voice stealing: oldest / quietest / priority
- 16 choke groups with priority system

(Choke system unchanged — already optimal)

---

## Per-Voice Signal Path

```
[Exciter] → [Corpus Body] → [Tone Shaper] → [Amp Envelope] → [Output]
                  ↑                                              │
             [Feedback]←─────────────────────────────────────────┘
                  ↑
         [Cross-Pad Coupling]
```

---

## Control Philosophy (NEW)

To prevent parameter overload, Membrum introduces **Macro Controls**:

| Macro        | Internally Controls |
|-------------|--------------------|
| Tightness   | Tension + Damping + Decay Skew |
| Brightness  | Exciter + Mode Weighting + Filter |
| Body Size   | Size + Mode Spacing + Envelope |
| Punch       | Exciter + Pitch Envelope |
| Complexity  | Mode Count + Coupling + Nonlinearity |

These sit **on top of** the detailed parameters, not instead of them.

---

## Exciter Types

(Unchanged, but now grouped with macro influence)

Velocity affects:
- Amplitude
- Spectral content
- Nonlinear behavior (scaled)

---

## Corpus Engine — Modal Synthesis Core

(Unchanged core, but with UX improvements)

### Body Parameters (User-Facing)

Instead of exposing raw physical parameters first, users see:

| Control        | Meaning |
|---------------|--------|
| Size          | Overall pitch and scale |
| Material      | From soft to metallic |
| Resonance     | Decay + damping |
| Strike Point  | Timbre variation |

Advanced panel reveals:
- b1 / b3
- modal tuning
- air coupling

---

## Double Membrane Coupling (Reframed)

Renamed in UI as:

| User Control     | Internal Mapping |
|------------------|-----------------|
| Body Depth       | Cavity depth |
| Resonance Blend  | Batter vs resonant head |
| Boom ↔ Punch     | Tension interaction |

Physics remains identical, but terminology is musical.

---

## The Unnatural Zone (Extended Mode Only)

Unlocked only in Extended Mode.

- Mode Stretch
- Mode Inject
- Decay Skew
- Material Morph
- Nonlinear Amount
- Cascade Speed

### Stability Guard (NEW)

- Nonlinear effects scale with velocity
- Internal limiter prevents energy blow-up
- Optional phase randomization for injected modes

---

## Tone Shaper

Same structure, but:

### Important Change

**Pitch Envelope is promoted to a primary control**, not buried:
- Especially critical for kicks
- Integrated into macro “Punch”

---

## Cross-Pad Coupling (Reworked UX)

### Tiered Control System

#### Basic Mode
- Snare Buzz (from kick)
- Tom Resonance
- Global Coupling

#### Advanced Mode
- Full matrix editor

### Improvements
- Visual feedback (who excites who)
- CPU-safe limits
- Optional solo/debug mode

---

## Snare Wire Modeling

(Unchanged DSP, improved UX naming)

- “Wire Tightness”
- “Wire Response”
- “Buzz Amount”

---

## Partial Configuration

(Unchanged, but hidden behind “Quality” macro)

| Quality | Modes |
|--------|------|
| Eco    | 8    |
| Standard | 16 |
| High   | 24–32 |

---

## UI (Updated Interaction Model)

### Key Change: Instrument-Oriented Workflow

Instead of raw parameter tweaking:
- Start from pad templates
- Adjust macros
- Dive deeper only if needed

### Modes

- **Simple View:** Macros + key controls
- **Advanced View:** Full parameter access

---

## Pad Templates (Enhanced)

Now include macro presets:

Example:

**Kick Template**
- Punch: High
- Body Size: Large
- Tightness: Medium
- Complexity: Low

---

## DSP Implementation Notes

(Unchanged — already solid)

---

## Performance Strategy (NEW)

- Hard caps on coupling complexity
- Adaptive partial reduction under load (optional)
- Deterministic CPU scaling

---

## MIDI

(Unchanged)

---

## Microtuning

(Unchanged — strong feature)

---

## Output Routing

(Unchanged)

---

## UI Layout

(Same structure, but layered controls)

---

## Planned: DMSP Integration (Deferred Priority)

### Strategic Change

DMSP is now:
- **Post-launch feature**
- Not required for core success

### Scope Reduction

Initial version:
- Factory preset generation only

Later:
- “Match Sound” feature

---

## Product Positioning (NEW)

Membrum is positioned as:

> A hybrid between a physical drum modeler and a modal synthesis instrument.

Not:
- A sampler replacement
- A strict acoustic emulator

---

## Final Notes

Key design priorities:

1. Fast sound creation
2. Musical control over physical accuracy
3. Controlled complexity
4. Expressive interaction
