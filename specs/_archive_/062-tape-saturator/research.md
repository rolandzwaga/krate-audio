# Research: TapeSaturator Processor

**Feature Branch**: `062-tape-saturator`
**Date**: 2026-01-14
**Status**: Complete

## Research Tasks

This document consolidates research findings for all NEEDS CLARIFICATION items and technology choices.

---

## 1. Jiles-Atherton Hysteresis Model Implementation

### Decision: Use DAFx/ChowDSP reference implementation approach

### Rationale

The Jiles-Atherton (J-A) model is the industry standard for magnetic hysteresis simulation in audio. The DAFx 2019 paper "Real-time Physical Modelling for Analog Tape Machines" by Jatin Chowdhury provides a proven, real-time audio implementation approach.

### Key Equations

**Main Differential Equation:**
```
dM/dH = (1/(1+c)) * (Man - M) / (delta*k - alpha*(Man - M)) + (c/(1+c)) * dMan/dH
```

Where:
- M = magnetization (state variable)
- H = applied magnetic field (input signal)
- Man = anhysteretic magnetization
- delta = sign of dH/dt (direction of field change)

**Langevin Function (Anhysteretic Magnetization):**
```
Man = Ms * L(He/a)
L(x) = coth(x) - 1/x    for |x| > epsilon
L(x) = x/3              for |x| < epsilon (Taylor approximation to avoid division by zero)
```

**Effective Field:**
```
He = H + alpha * M
```

### Parameters (DAFx/ChowDSP Defaults for Ferric Oxide Tape)

| Parameter | Symbol | Default Value | Description |
|-----------|--------|---------------|-------------|
| Saturation magnetization | Ms | 350000 | Maximum magnetization (A/m) |
| Shape parameter | a | 22 | Controls Langevin function shape |
| Inter-domain coupling | alpha | 1.6e-11 | Domain interaction factor |
| Pinning coefficient | k | 27.0 | Average energy to break pinning (coercivity ~ 27 kA/m) |
| Reversibility | c | 1.7 | Ratio of reversible to irreversible magnetization |

Note: These values are normalized for audio signals in the [-1, 1] range.

### Alternatives Considered

1. **Preisach Model**: More accurate for complex materials but computationally expensive (memory table required). Rejected due to CPU cost.
2. **Simple tanh saturation**: Already exists in SaturationProcessor. Lacks hysteresis memory effect.
3. **Neural network approach**: High CPU cost, training data required. Rejected for Layer 2 processor.

### Sources

- [DAFx 2019 Paper](https://www.dafx.de/paper-archive/2019/DAFx2019_paper_3.pdf)
- [ChowTape GitHub](https://github.com/jatinchowdhury18/AnalogTapeModel)
- [COMSOL J-A Model Documentation](https://doc.comsol.com/5.6/doc/com.comsol.help.acdc/acdc_ug_theory.05.14.html)
- [Jiles-Atherton Model - Wikipedia](https://en.wikipedia.org/wiki/Jiles%E2%80%93Atherton_model)

---

## 2. Numerical Solver Selection

### Decision: Implement RK2, RK4, NR4, NR8 solvers

### Rationale

The J-A model requires numerical ODE solving. Different solvers offer different accuracy/CPU tradeoffs. ChowTape's approach of offering multiple solver options is proven and allows users to optimize for their use case.

### Solver Comparison

| Solver | Accuracy | CPU Cost | Best Use Case |
|--------|----------|----------|---------------|
| RK2 (Runge-Kutta 2nd) | Medium | Low (~2 evals) | Live performance, low-latency |
| RK4 (Runge-Kutta 4th) | High | Medium (~4 evals) | Default, balanced quality/CPU |
| NR4 (Newton-Raphson 4 iter) | High | Medium-High | High-quality mixing |
| NR8 (Newton-Raphson 8 iter) | Highest | High | Mastering, offline rendering |

### Implementation Approach

**Runge-Kutta Methods** (explicit):
```cpp
// RK2 (Heun's method)
k1 = f(H, M)
k2 = f(H + dt, M + dt*k1)
M_new = M + (dt/2) * (k1 + k2)

// RK4 (Classic)
k1 = f(H, M)
k2 = f(H + dt/2, M + dt*k1/2)
k3 = f(H + dt/2, M + dt*k2/2)
k4 = f(H + dt, M + dt*k3)
M_new = M + (dt/6) * (k1 + 2*k2 + 2*k3 + k4)
```

**Newton-Raphson Methods** (implicit, better stability):
```cpp
// Iterative solution: M_new = M + dt * dM/dH
for (int i = 0; i < maxIterations; ++i) {
    residual = computeResidual(M_guess, H_new)
    jacobian = computeJacobian(M_guess, H_new)
    M_guess -= residual / jacobian
    if (converged) break
}
```

### Alternatives Considered

1. **Euler method**: Too unstable for stiff nonlinear equations
2. **Adaptive step-size solvers**: Variable CPU cost problematic for real-time
3. **Lookup table**: Loses hysteresis memory effect

### Sources

- [DAFx 2019 Paper - Solver Comparison](https://www.dafx.de/paper-archive/2019/DAFx2019_paper_3.pdf)
- [Numerical Solving Method for Jiles-Atherton Model](https://www.mdpi.com/2227-7390/10/23/4431)

---

## 3. Pre-Emphasis / De-Emphasis Filters

### Decision: Use High-Shelf Biquad filters at ~3kHz with +9dB boost/cut

### Rationale

Real tape machines use pre-emphasis (boost HF before recording) and de-emphasis (cut HF after playback) to achieve better signal-to-noise ratio. This creates frequency-dependent saturation where high frequencies saturate more readily.

### Implementation

**Pre-Emphasis (before saturation):**
- Type: High-Shelf
- Frequency: ~3000 Hz (adjustable in expert mode if needed)
- Gain: +9 dB
- Q: 0.707 (Butterworth)

**De-Emphasis (after saturation):**
- Type: High-Shelf
- Frequency: ~3000 Hz
- Gain: -9 dB (inverse of pre-emphasis)
- Q: 0.707 (Butterworth)

### Existing Component

Use `Biquad` from Layer 1 (`primitives/biquad.h`) with `FilterType::HighShelf`.

### Alternatives Considered

1. **One-pole filters**: Too gentle rolloff, less accurate
2. **Custom tape EQ curves**: Added complexity without significant benefit for this processor
3. **Separate head bump filter**: Out of scope per spec (no tape head gap simulation)

### Sources

- [DSP-DISTORTION-TECHNIQUES.md - Tape Saturation section](../DSP-DISTORTION-TECHNIQUES.md)
- [ChowTape Manual](https://chowdsp.com/manuals/ChowTapeManual.pdf)

---

## 4. T-Scaling for Sample Rate Independence

### Decision: Scale time-dependent J-A parameters by (44100 / currentSampleRate)

### Rationale

The J-A model differential equation depends on the time step (sample rate). Without compensation, hysteresis behavior changes with sample rate. T-scaling maintains consistent saturation character across 44.1kHz to 192kHz.

### Implementation

Scale the effective "delta t" in the differential equation:
```cpp
float T = 1.0f / sampleRate;
float TScale = 44100.0f / sampleRate;  // Reference: 44.1kHz

// Apply T-scaling to hysteresis computation
float dH = (H_current - H_prev) * TScale;
```

### Verification

Test at 44.1kHz, 48kHz, 88.2kHz, 96kHz, 192kHz with identical input. Output RMS should be within 5% across all sample rates.

### Sources

- [DAFx 2019 Paper](https://www.dafx.de/paper-archive/2019/DAFx2019_paper_3.pdf)

---

## 5. Model Crossfade Strategy

### Decision: 10ms equal-power crossfade when switching between Simple and Hysteresis models

### Rationale

Instantaneous model switching causes audible clicks due to different output levels and waveform shapes. A brief crossfade (10ms = ~441 samples at 44.1kHz) smooths the transition.

### Implementation

```cpp
if (modelChanged) {
    crossfadePosition = 0.0f;
    crossfadeActive = true;
}

if (crossfadeActive) {
    auto [fadeOut, fadeIn] = equalPowerGains(crossfadePosition);
    output = oldModelOutput * fadeOut + newModelOutput * fadeIn;
    crossfadePosition += crossfadeIncrement;
    if (crossfadePosition >= 1.0f) {
        crossfadeActive = false;
    }
}
```

Uses existing `crossfade_utils.h` from Layer 0.

### Alternatives Considered

1. **Linear crossfade**: Causes volume dip in middle
2. **Longer crossfade (50ms+)**: Noticeable delay, wastes CPU
3. **No crossfade**: Clicks on model change

---

## 6. DC Blocking After Hysteresis

### Decision: Use 1st-order DCBlocker at 10Hz

### Rationale

The hysteresis model with non-zero bias creates DC offset. The 1st-order DC blocker from Layer 1 is sufficient since settling time requirements are not critical for tape saturation (tape already has slow dynamics).

### Existing Component

Use `DCBlocker` from Layer 1 (`primitives/dc_blocker.h`) with 10Hz cutoff.

### Alternatives Considered

1. **DCBlocker2 (2nd-order Bessel)**: Faster settling but higher CPU, overkill for tape
2. **Lower cutoff (5Hz)**: Slower settling, risk of DC accumulation
3. **Higher cutoff (20Hz)**: May affect bass response

---

## 7. CPU Budget Verification

### Decision: Simple model < 0.3%, Hysteresis/RK4 < 1.5% at 44.1kHz

### Measurement Methodology

Per SC-005/SC-006, measure cycles/sample at 512-sample blocks normalized to 2.5GHz baseline:
```cpp
// Pseudo-code for benchmark
auto start = __rdtsc();
processor.process(buffer, 512);
auto cycles = __rdtsc() - start;
float cyclesPerSample = cycles / 512.0f;

// Normalize to 2.5GHz: CPU% = (cyclesPerSample * sampleRate) / (2.5e9) * 100
```

### Targets

| Model | Max cycles/sample | CPU% @ 44.1kHz/2.5GHz |
|-------|-------------------|----------------------|
| Simple | ~170 | 0.3% |
| Hysteresis RK4 | ~850 | 1.5% |

---

## Summary

All research tasks resolved. No remaining NEEDS CLARIFICATION items.

| Item | Decision | Rationale |
|------|----------|-----------|
| Hysteresis model | Jiles-Atherton | Industry standard, real-time proven |
| J-A parameters | DAFx defaults | a=22, alpha=1.6e-11, c=1.7, k=27, Ms=350000 |
| Solvers | RK2/RK4/NR4/NR8 | Proven ChowTape approach |
| Pre/de-emphasis | High-shelf @ 3kHz, +9dB | Standard tape recording practice |
| T-scaling | 44.1kHz reference | Sample rate independence |
| Model crossfade | 10ms equal-power | Click-free transitions |
| DC blocking | 1st-order @ 10Hz | Adequate for tape dynamics |
