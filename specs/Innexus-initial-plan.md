# Adaptive Harmonic Oscillator — Concept & Architecture Document

## Purpose

This document consolidates the full conceptual and architectural plan discussed so far into a coherent design reference. It describes the **core idea**, **DSP architecture**, **instrument behavior**, and **design principles** for a VST instrument that derives its oscillator structure from the harmonics of another sound source.

This is **not yet a software roadmap**.
It is a **conceptual and system-design foundation** intended to guide later engineering and product decisions.

---

# 1. Core Concept

## 1.1 The Central Idea

Create an oscillator whose harmonic structure is derived from another sound source rather than from a predefined waveform.

Traditional synthesis:

```
oscillator → produces harmonics
```

Proposed system:

```
external sound → harmonic analysis → oscillator definition
```

The oscillator becomes **a dynamic model shaped by sound**, not a static waveform generator.

---

## 1.2 Supported Operating Modes

The instrument operates as a VST with two input paradigms:

### Mode A — Live Input (Sidechain Driven)

* Sidechain audio continuously influences timbre.
* MIDI controls pitch.
* Sound source acts as a behavioral driver.

### Mode B — Sample Analysis

* A sample is analyzed offline or during loading.
* Extracted harmonic identity becomes a playable oscillator.

Both modes share the same internal harmonic model.

---

## 1.3 Design Goal

Separate:

* **Pitch** → controlled by MIDI
* **Timbre** → derived from analyzed sound

This transforms arbitrary audio into a playable instrument.

---

# 2. High-Level System Overview

```
Audio Input (Live or Sample)
        ↓
Harmonic Analysis Engine
        ↓
Normalized Harmonic Model
        ↓
Spectral Envelope Model
        ↓
Adaptive Synth Engine
        ↓
Audio Output
```

Key principle:

> Analysis defines identity.
> Performance defines behavior.

---

# 3. Harmonic Tracking Architecture

## 3.1 Signal Model

A harmonic signal is approximated as:

```
x(t) = Σ A_n(t) sin(2π n f0(t)t + φ_n(t))
```

Tracked parameters:

* fundamental frequency (f₀)
* harmonic index
* amplitude
* phase
* temporal evolution

---

## 3.2 Processing Pipeline

```
Input Audio
    ↓
Windowing
    ↓
STFT
    ↓
Spectral Peak Detection
    ↓
Fundamental Estimation
    ↓
Harmonic Assignment
    ↓
Partial Tracking (state estimation)
```

---

## 3.3 Tracking Philosophy

The system performs **estimation**, not measurement.

```
prediction → comparison → correction
```

Harmonics are persistent objects with memory rather than frame-by-frame detections.

---

## 3.4 Harmonic State Model

Each harmonic stores:

* frequency estimate
* amplitude
* phase
* velocity (change rate)
* confidence score
* age/history

Rules:

* never trust a single frame
* require persistence across frames
* fade harmonics instead of deleting instantly

---

## 3.5 Prediction-Based Tracking

Before analyzing a new frame:

```
predicted_freq = previous_freq + velocity * dt
```

Analysis searches locally near predictions instead of globally.

Benefits:

* stability
* lower CPU
* identity preservation

---

# 4. CPU-Efficient Design

## 4.1 Optimization Principles

1. Do not rediscover harmonics each frame.
2. Analyze only expected regions.
3. Separate analysis rate from audio rate.
4. Replace oscillator banks when possible.

---

## 4.2 Key Optimizations

### Targeted Spectral Search

Search only near predicted harmonic locations.

### Smaller FFT + Interpolation

Use moderate FFT size with peak interpolation.

### Reduced Analysis Rate

Analysis updates at ~50–100 Hz.

### Harmonic Pruning

Ignore very low-energy harmonics.

### Wavetable Reconstruction

Rebuild a single-cycle waveform periodically instead of running many oscillators.

---

# 5. Pitch-Independent Harmonic Representation

## 5.1 Normalization

Convert frequencies into harmonic index space:

```
harmonic_index = frequency / f0
```

Store:

* harmonic number
* amplitude
* relative phase

No absolute frequencies retained.

---

## 5.2 Playback

For MIDI pitch P:

```
harmonic_frequency = n * P
```

Same timbre, arbitrary pitch.

---

## 5.3 Spectral Envelope Extraction

Rather than raw partial amplitudes, compute a smooth spectral envelope representing timbre.

Advantages:

* robustness
* pitch independence
* stability for live input

---

# 6. Emergent Synthesis Behavior

## 6.1 Stateful System

The harmonic model contains memory:

```
analysis → state → synthesis → analysis
```

This creates a dynamical system.

---

## 6.2 Harmonics as Agents

Each harmonic behaves like an adaptive entity:

* competing for energy
* stabilizing through persistence
* fading through entropy

Result:

* evolving timbre
* attractor states
* self-organizing spectra

---

## 6.3 Optional Feedback Loop

Closed-loop mode:

```
analysis_input =
    sidechain * (1 − feedback)
  + synth_output * feedback
```

Produces emergent sonic evolution.

Constraints required:

* energy limits
* inertia
* slow decay (entropy leak)

---

# 7. Instrument Control Model

Expose behavioral controls instead of DSP parameters.

## Core Macro Controls

### Identity

How strongly input reshapes the sound.

### Memory

How long harmonic identity persists.

### Stability

Resistance to change.

### Focus

Spectral complexity vs diffusion.

### Energy

Responsiveness to excitation/transients.

---

## Workflow Controls

### Learn Mode

Input reshapes harmonic identity.

### Play Mode

Identity stabilizes for performance.

### Freeze Button

Stops analysis updates while synthesis continues.

---

## UI Principles

* 5–8 controls maximum
* macro parameters map to multiple internal variables
* emotional descriptors over technical terminology

---

# 8. Avoiding “FFT Synth Mush”

Spectral synthesis sounds artificial when too perfect.

Realism requires controlled imperfection.

---

## 8.1 Harmonic Coupling

Allow neighboring harmonics to share energy.

Creates spectral viscosity and physical behavior.

---

## 8.2 Micro Instability

Add extremely small frequency drift per harmonic.

Restores acoustic beating.

---

## 8.3 Phase Imperfection

Maintain continuity but introduce tiny stochastic variation.

Prevents metallic artifacts.

---

## 8.4 Transient Rule Breaking

During attacks:

* loosen harmonic constraints
* inject broadband energy
* re-stabilize afterward

---

## 8.5 Residual Noise Layer

Model signal as:

```
signal = harmonic_part + residual_noise
```

Resynthesize both components.

Essential for realism.

---

## 8.6 Nonlinear Energy Mapping

Apply gentle compression-like behavior to harmonic amplitudes.

Simulates acoustic saturation.

---

## 8.7 Time Asymmetry

Ensure:

* fast attacks
* slower decays

Matches physical excitation.

---

# 9. Acoustic Illusion Stack

Realistic output emerges from layered behavior:

| Layer             | Role         |
| ----------------- | ------------ |
| Harmonic model    | Identity     |
| Spectral envelope | Timbre       |
| Residual noise    | Texture      |
| Drift             | Life         |
| Coupling          | Physicality  |
| Transient chaos   | Articulation |

---

# 10. Conceptual Identity of the Instrument

The result is **not**:

* a sampler
* a vocoder
* additive synthesis
* physical modeling

It is best described as:

> **An adaptive spectral instrument**
> — a stateful oscillator shaped by sound interaction.

Users influence behavior rather than directly controlling parameters.

---

# 11. Guiding Design Principles

1. Stability over accuracy.
2. Prediction over detection.
3. Behavior over parameters.
4. Imperfection over mathematical purity.
5. Memory enables musicality.
6. Timbre and pitch must remain separable.
7. Analysis nudges — it does not dictate.

---

# 12. Next Phase (Outside This Document)

This document enables future work on:

* software architecture roadmap
* module boundaries
* incremental implementation plan
* performance budgeting
* plugin UX specification

---

**End of Concept Document**
