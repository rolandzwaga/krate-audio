# Research: Chaos Attractor Waveshaper

**Feature**: 104-chaos-waveshaper
**Date**: 2026-01-26

## Overview

This document captures research findings for implementing chaos attractor-based waveshaping in the KrateDSP library. The goal is a Layer 1 primitive that uses mathematical chaos systems to modulate waveshaping drive, creating time-varying distortion.

---

## 1. Chaos Attractor Selection

### Why These Four Models?

| Model | Type | Character | Use Case |
|-------|------|-----------|----------|
| Lorenz | 3D continuous | Swirling, unpredictable | Classic chaos, general use |
| Rossler | 3D continuous | Smoother, spiraling | Gentler evolution |
| Chua | 3D continuous | Double-scroll, bi-modal | Electronic/circuit character |
| Henon | 2D discrete | Sharp transitions | Rhythmic, percussive |

### Parameter Choices

All parameters use standard "chaotic regime" values from literature. These ensure bounded, non-periodic behavior.

---

## 2. Lorenz Attractor

### Equations

```
dx/dt = sigma * (y - x)
dy/dt = x * (rho - z) - y
dz/dt = x * y - beta * z
```

### Standard Parameters

| Parameter | Value | Notes |
|-----------|-------|-------|
| sigma | 10.0 | Prandtl number |
| rho | 28.0 | Rayleigh number |
| beta | 8/3 (2.6667) | Geometric factor |

### State Bounds

| Variable | Typical Range | Safe Bound |
|----------|---------------|------------|
| x | [-20, 20] | +/- 50 |
| y | [-30, 30] | +/- 50 |
| z | [0, 50] | +/- 50 |

### Integration

- **Method**: Euler (sufficient for audio-rate chaos)
- **Base timestep**: dt = 0.005 at 44100 Hz
- **Perturbation scale**: 0.1

### Initial Conditions

```cpp
// Classic starting point with small perturbation
x = 1.0f;
y = 0.0f;
z = 0.0f;
```

---

## 3. Rossler Attractor

### Equations

```
dx/dt = -y - z
dy/dt = x + a * y
dz/dt = b + z * (x - c)
```

### Standard Parameters

| Parameter | Value | Notes |
|-----------|-------|-------|
| a | 0.2 | Controls spiral tightness |
| b | 0.2 | Vertical displacement |
| c | 5.7 | Controls folding behavior |

### State Bounds

| Variable | Typical Range | Safe Bound |
|----------|---------------|------------|
| x | [-10, 10] | +/- 20 |
| y | [-10, 10] | +/- 20 |
| z | [0, 20] | +/- 20 |

### Integration

- **Base timestep**: dt = 0.02 at 44100 Hz (4x faster than Lorenz due to slower dynamics)
- **Perturbation scale**: 0.1

### Initial Conditions

```cpp
x = 0.1f;
y = 0.0f;
z = 0.0f;
```

---

## 4. Chua Attractor

### Equations

```
dx/dt = alpha * (y - x - h(x))
dy/dt = x - y + z
dz/dt = -beta * y
```

Where h(x) is the Chua diode (piecewise-linear):

```cpp
float chuaDiode(float x) {
    // h(x) = m1*x + 0.5*(m0-m1)*(|x+1| - |x-1|)
    return m1 * x + 0.5f * (m0 - m1) * (std::abs(x + 1.0f) - std::abs(x - 1.0f));
}
```

### Standard Parameters

| Parameter | Value | Notes |
|-----------|-------|-------|
| alpha | 15.6 | Capacitor ratio C2/C1 |
| beta | 28.0 | Damping/capacitor ratio |
| m0 | -1.143 | Inner slope of diode |
| m1 | -0.714 | Outer slope of diode |

### State Bounds

| Variable | Typical Range | Safe Bound |
|----------|---------------|------------|
| x | [-3, 3] | +/- 10 |
| y | [-1, 1] | +/- 10 |
| z | [-5, 5] | +/- 10 |

### Integration

- **Base timestep**: dt = 0.01 at 44100 Hz
- **Perturbation scale**: 0.08 (smaller due to sensitive dynamics)

### Initial Conditions

```cpp
x = 0.1f;
y = 0.0f;
z = 0.0f;
```

### Character

The double-scroll attractor produces bi-modal behavior - the state jumps between two "lobes" creating distinct audio character.

---

## 5. Henon Map

### Equations

```
x[n+1] = 1 - a * x[n]^2 + y[n]
y[n+1] = b * x[n]
```

### Standard Parameters

| Parameter | Value | Notes |
|-----------|-------|-------|
| a | 1.4 | Quadratic nonlinearity |
| b | 0.3 | Contraction factor |

### State Bounds

| Variable | Typical Range | Safe Bound |
|----------|---------------|------------|
| x | [-1.5, 1.5] | +/- 5 |
| y | [-0.5, 0.5] | +/- 5 |

### Integration

- **Discrete map**: One iteration per control update (not continuous)
- **Timestep**: N/A (discrete)
- **Perturbation scale**: 0.05

### Initial Conditions

```cpp
x = 0.0f;
y = 0.0f;
```

### Interpolation for Audio

Since Henon is discrete, we need interpolation for smooth audio output:

```cpp
// Track previous value for interpolation
float prevX = state_.x;

// Compute next iteration
float newX = 1.0f - a * state_.x * state_.x + state_.y;
float newY = b * state_.x;
state_.x = newX;
state_.y = newY;

// Use fractional phase for interpolation
normalizedX_ = std::lerp(prevX / 1.5f, state_.x / 1.5f, fractionalPhase_);
```

---

## 6. Waveshaping Approach

### Transfer Function

The chaos modulates the drive parameter of a tanh-based soft clipper:

```cpp
output = tanh(drive * input)
```

### Drive Mapping

| Attractor State | Normalized | Drive |
|-----------------|------------|-------|
| Minimum (-bound) | -1.0 | 0.5 (soft) |
| Zero | 0.0 | 2.25 (moderate) |
| Maximum (+bound) | +1.0 | 4.0 (aggressive) |

```cpp
const float kMinDrive = 0.5f;
const float kMaxDrive = 4.0f;

float normalizedX = std::clamp(state_.x / normalizationFactor_, -1.0f, 1.0f);
float drive = kMinDrive + (normalizedX * 0.5f + 0.5f) * (kMaxDrive - kMinDrive);
```

### Normalization Factors

| Model | Typical X Range | Normalization Factor |
|-------|-----------------|----------------------|
| Lorenz | [-20, 20] | 20.0 |
| Rossler | [-10, 10] | 10.0 |
| Chua | [-3, 3] | 5.0 (conservative) |
| Henon | [-1.5, 1.5] | 1.5 |

---

## 7. Sample Rate Compensation

To maintain consistent attractor evolution speed across sample rates:

```cpp
float compensatedDt = baseDt * (44100.0f / sampleRate_) * attractorSpeed_;
```

This ensures that at 96kHz, the attractor evolves at the same "wall clock" rate as at 44.1kHz.

---

## 8. Input Coupling

Input coupling allows the audio signal to perturb the attractor state:

```cpp
if (inputCoupling_ > 0.0f) {
    float inputEnvelope = std::abs(inputSample);
    float perturbation = inputCoupling_ * inputEnvelope * perturbationScale_;

    // Perturb X more, Y less, Z minimal
    state_.x += perturbation;
    state_.y += perturbation * 0.5f;
    // Z left unperturbed to maintain attractor stability
}
```

### Perturbation Scales (Per Model)

| Model | Scale | Rationale |
|-------|-------|-----------|
| Lorenz | 0.1 | Moderate sensitivity |
| Rossler | 0.1 | Similar to Lorenz |
| Chua | 0.08 | More sensitive dynamics |
| Henon | 0.05 | Discrete, needs smaller perturbation |

---

## 9. Numerical Stability

### Denormal Prevention

```cpp
state_.x = detail::flushDenormal(state_.x);
state_.y = detail::flushDenormal(state_.y);
state_.z = detail::flushDenormal(state_.z);
```

### Divergence Detection and Recovery

```cpp
bool isStateValid() const noexcept {
    return !detail::isNaN(state_.x) && !detail::isInf(state_.x) &&
           !detail::isNaN(state_.y) && !detail::isInf(state_.y) &&
           !detail::isNaN(state_.z) && !detail::isInf(state_.z);
}

void checkAndResetIfDiverged() noexcept {
    bool diverged = !isStateValid() ||
                    std::abs(state_.x) > safeBound_ ||
                    std::abs(state_.y) > safeBound_ ||
                    std::abs(state_.z) > safeBound_;

    if (diverged) {
        resetToInitialConditions();
    }
}
```

---

## 10. Performance Considerations

### Control Rate Processing

Attractor integration is computationally cheap but doesn't need sample-rate updates:

```cpp
static constexpr size_t kControlRateInterval = 32;  // ~1.37kHz at 44.1kHz
```

This gives 1378 attractor updates per second at 44.1kHz - sufficient for smooth evolution while saving CPU.

### CPU Budget

Layer 1 primitive budget: < 0.1% CPU at 44.1kHz

Expected actual usage:
- Attractor update: ~5-10 ops every 32 samples
- Waveshaping: 1 tanh per sample (fast approximation)
- Mix: 1 lerp per sample

Total: Well under budget.

---

## 11. References

1. Lorenz, E.N. (1963). "Deterministic nonperiodic flow"
2. Rossler, O.E. (1976). "An equation for continuous chaos"
3. Chua, L.O. (1993). "Chua's circuit: a paradigm for chaos"
4. Henon, M. (1976). "A two-dimensional mapping with a strange attractor"
5. Sprott, J.C. (2010). "Elegant Chaos: Algebraically Simple Chaotic Flows"

---

## 12. Decisions Summary

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Integration method | Euler | Simple, sufficient for audio chaos |
| Control rate | 32 samples | Balance between smoothness and CPU |
| Waveshaping | Variable-drive tanh | Proven, efficient, bounded output |
| Input coupling | X > Y > Z | Prioritize main output variable |
| Normalization | Per-model factors | Consistent [-1, 1] output range |
