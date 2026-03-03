# Research: Chaos Attractor Oscillator

**Feature**: 026-chaos-attractor-oscillator
**Date**: 2026-02-05
**Status**: Complete

## Overview

This document captures research findings for implementing an audio-rate chaos oscillator with RK4 adaptive substepping. The research addresses three main areas: RK4 implementation patterns, adaptive substepping without heap allocation, and testing strategies for chaotic systems.

---

## 1. RK4 Implementation Patterns for Audio DSP

### Background

Fourth-order Runge-Kutta (RK4) is the standard numerical integration method for ODEs requiring higher accuracy than Euler integration. For chaotic systems, RK4's O(h^4) error is essential because small integration errors are amplified exponentially (positive Lyapunov exponent).

### Standard RK4 Formula

For a system dy/dt = f(t, y):

```
k1 = f(t, y)
k2 = f(t + dt/2, y + dt*k1/2)
k3 = f(t + dt/2, y + dt*k2/2)
k4 = f(t + dt, y + dt*k3)
y_new = y + dt * (k1 + 2*k2 + 2*k3 + k4) / 6
```

### Implementation Decisions

**Decision: Inline RK4 per attractor type**

**Rationale:**
- Virtual function calls for derivative computation would add overhead (function pointer dereference)
- Each attractor has only 2-3 state variables; passing/returning structs is efficient
- Modern compilers inline small functions aggressively
- Code duplication is minimal (RK4 loop is ~20 lines per attractor)

**Alternatives Rejected:**
- Generic templated RK4 with functor: Added complexity, marginal benefit
- Function pointer for derivatives: Runtime overhead, prevents optimization

**Memory Layout:**
- Use struct with named members (x, y, z) rather than array
- Named members are clearer for attractor equations
- Compiler optimizes equally well

```cpp
struct AttractorState {
    float x = 0.0f;
    float y = 0.0f;
    float z = 0.0f;  // Used as 'v' for 2D oscillators (Duffing, VanDerPol)
};
```

### Existing ChaosModSource Analysis

The existing `ChaosModSource` (processors/chaos_mod_source.h) uses:
- Euler integration (first-order, O(h) error)
- Control-rate update (every 32 samples)
- Simple state perturbation for coupling

**Why RK4 is needed for audio-rate:**
- Euler at audio rate (dt ~= 0.001) with speed=10 gives effective dt ~= 0.01
- Chaos amplifies truncation error exponentially
- ChaosModSource works because it updates every 32 samples (effectively dt_euler * 32 per visible change)
- At audio rate, every sample is an integration step - much stricter accuracy requirement

---

## 2. Adaptive Substepping Without Heap Allocation

### Problem Statement

At low frequencies, the integration timestep dt may exceed stability thresholds (dtMax). Adaptive substepping divides large dt into smaller stable steps. The challenge is implementing this without heap allocation for real-time safety.

### Solution: Fixed Maximum Substeps

**Decision: Cap at 100 substeps maximum**

**Implementation:**
```cpp
// Calculate substeps needed
int numSubsteps = static_cast<int>(std::ceil(dt / dtMax_));

// Cap to prevent CPU spike (100 substeps handles down to ~0.5Hz at worst case)
numSubsteps = std::min(numSubsteps, 100);

// Compute actual substep dt
float dtSubstep = dt / static_cast<float>(numSubsteps);

// RK4 loop
for (int i = 0; i < numSubsteps; ++i) {
    rk4Step(dtSubstep);
}
```

**Rationale:**
- At 44.1kHz with Lorenz (dtMax=0.001), baseDt=0.01, refFreq=100Hz:
  - At 100Hz: dt = 0.01 * (100/100) / 44100 = 2.27e-7 -> 1 substep
  - At 20Hz: dt = 0.01 * (20/100) / 44100 = 4.5e-8 -> 1 substep
  - At 1Hz: dt = 0.01 * (1/100) / 44100 = 2.27e-9 -> 1 substep
- Substepping only kicks in when requested dt exceeds dtMax
- 100 substeps is a safety cap, typically 1-10 substeps needed

**CPU Budget Verification:**
- RK4 step: ~50 floating-point ops per attractor
- At 100 substeps * 44100 samples/sec = 4.41M RK4 steps/sec (worst case)
- Modern CPU handles ~10 GFLOPS, so 220 MFLOPS is ~2.2% of single core
- Typical case (1-2 substeps) is well under 1% (SC-007 target)

**Alternatives Rejected:**
- Dynamic vector of substeps: Heap allocation in audio thread
- Fixed-size std::array: Unnecessary, just iterate with loop counter

### Per-Attractor dtMax Values (from spec clarifications)

| Attractor | dtMax | Rationale |
|-----------|-------|-----------|
| Lorenz | 0.001 | Standard for butterfly attractor |
| Rossler | 0.002 | Larger - single smooth nonlinearity |
| Chua | 0.0005 | Smallest - piecewise-linear discontinuity |
| Duffing | 0.001 | Similar to Lorenz complexity |
| VanDerPol | 0.001 | Relaxation dynamics |

---

## 3. Testing Strategies for Chaotic Systems

### Challenge

Chaotic systems are deterministic but unpredictable - small initial condition differences lead to exponentially diverging trajectories. Traditional "expected output" testing is problematic.

### Testing Approaches

#### 3.1 Bounded Output Verification (Primary - SC-001)

**Strategy:** Verify output stays in [-1.0, +1.0] for extended duration.

```cpp
TEST_CASE("SC-001: Output bounded for 10 seconds") {
    ChaosOscillator osc;
    osc.prepare(44100.0);
    // Test each attractor
    for (auto attractor : allAttractors) {
        osc.setAttractor(attractor);
        osc.setFrequency(440.0f);
        for (int i = 0; i < 441000; ++i) {
            float sample = osc.process();
            REQUIRE(sample >= -1.0f);
            REQUIRE(sample <= 1.0f);
            REQUIRE(!std::isnan(sample));
            REQUIRE(!std::isinf(sample));
        }
    }
}
```

#### 3.2 Divergence Detection and Recovery (SC-002)

**Strategy:** Force divergence, measure recovery time.

```cpp
TEST_CASE("SC-002: Divergence recovery within 1ms") {
    ChaosOscillator osc;
    osc.prepare(44100.0);
    // Inject bad state (implementation detail - may need friend access)
    // Process and count samples until valid output
    // Verify < 44 samples at 44.1kHz (1ms)
}
```

#### 3.3 Spectral Analysis for Timbre (SC-005, SC-006)

**Strategy:** Use FFT to compute spectral centroid, verify chaos parameter and attractor type affect it.

```cpp
TEST_CASE("SC-005: Chaos parameter affects spectral centroid") {
    ChaosOscillator osc;
    osc.prepare(44100.0);

    auto computeCentroid = [&](float chaos) {
        osc.setChaos(chaos);
        osc.reset();
        // Generate 1 second of audio
        // FFT
        // Compute centroid = sum(f * magnitude) / sum(magnitude)
        return centroid;
    };

    float centroidLow = computeCentroid(0.0f);
    float centroidHigh = computeCentroid(1.0f);

    // Verify >10% shift
    float shift = std::abs(centroidHigh - centroidLow) / centroidLow;
    REQUIRE(shift > 0.10f);
}

TEST_CASE("SC-006: Attractor types have distinct spectra") {
    // Similar approach, compare centroids across attractors
    // Verify >20% difference between any pair
}
```

#### 3.4 Approximate Frequency Verification (SC-008)

**Strategy:** Zero-crossing analysis or autocorrelation to find fundamental period.

```cpp
TEST_CASE("SC-008: Frequency tracking within +/-50%") {
    ChaosOscillator osc;
    osc.prepare(44100.0);
    osc.setFrequency(440.0f);

    // Generate audio
    // Autocorrelation to find period
    // or FFT and find peak

    float detectedFreq = ...;
    REQUIRE(detectedFreq >= 220.0f);  // -50%
    REQUIRE(detectedFreq <= 660.0f);  // +50%
}
```

#### 3.5 Numerical Stability (SC-003)

**Strategy:** Test across frequency range, verify no NaN/Inf.

```cpp
TEST_CASE("SC-003: Numerical stability 20Hz-2000Hz") {
    ChaosOscillator osc;
    osc.prepare(44100.0);

    for (float freq : {20.0f, 100.0f, 440.0f, 1000.0f, 2000.0f}) {
        osc.setFrequency(freq);
        osc.reset();
        for (int i = 0; i < 44100; ++i) {
            float s = osc.process();
            REQUIRE(!std::isnan(s));
            REQUIRE(!std::isinf(s));
        }
    }
}
```

#### 3.6 DC Blocker Settling (SC-004)

**Strategy:** Step response test.

```cpp
TEST_CASE("SC-004: DC blocker settles within 100ms") {
    ChaosOscillator osc;
    osc.prepare(44100.0);

    // Generate 100ms of audio
    float dcSum = 0.0f;
    int sampleCount = 4410;  // 100ms
    for (int i = 0; i < sampleCount; ++i) {
        dcSum += osc.process();
    }

    // Generate another 100ms and measure DC
    float dcAfter = 0.0f;
    for (int i = 0; i < sampleCount; ++i) {
        dcAfter += osc.process();
    }
    float dcLevel = std::abs(dcAfter / sampleCount);

    REQUIRE(dcLevel < 0.01f);  // <1% of peak
}
```

---

## 4. Per-Attractor Implementation Details

### 4.1 Lorenz System

```cpp
// dx/dt = sigma * (y - x)
// dy/dt = x * (rho - z) - y
// dz/dt = x * y - beta * z
constexpr float sigma = 10.0f;
constexpr float rho = 28.0f;  // chaos parameter maps to [20, 28]
constexpr float beta = 8.0f / 3.0f;
```

- **Initial state**: (1, 1, 1) - near attractor
- **Safe bounds**: 500 (spec)
- **Normalization**: x/20, y/20, z/30

### 4.2 Rossler System

```cpp
// dx/dt = -y - z
// dy/dt = x + a * y
// dz/dt = b + z * (x - c)
constexpr float a = 0.2f;
constexpr float b = 0.2f;
constexpr float c = 5.7f;  // chaos parameter maps to [4, 8]
```

- **Initial state**: (0.1, 0, 0)
- **Safe bounds**: 300
- **Normalization**: x/12, y/12, z/20

### 4.3 Chua Circuit

```cpp
// dx/dt = alpha * (y - x - h(x))
// dy/dt = x - y + z
// dz/dt = -beta * y
// h(x) = m1*x + 0.5*(m0-m1)*(|x+1| - |x-1|)
constexpr float alpha = 15.6f;  // chaos parameter maps to [12, 18]
constexpr float beta = 28.0f;
constexpr float m0 = -1.143f;
constexpr float m1 = -0.714f;
```

- **Initial state**: (0.7, 0, 0)
- **Safe bounds**: 50
- **Normalization**: x/2.5, y/1.5, z/1.5

### 4.4 Duffing Oscillator

```cpp
// dx/dt = v
// dv/dt = x - x^3 - gamma * v + A * cos(omega * phase)
// phase += omega * dt_substep  (attractor time, not real time)
constexpr float gamma = 0.1f;
constexpr float A = 0.35f;  // chaos parameter maps to [0.2, 0.5]
constexpr float omega = 1.4f;
```

- **Initial state**: (0.5, 0) with phase reset
- **Safe bounds**: 10
- **Normalization**: x/2, v/2

**Important**: Phase accumulates in attractor time (`phase += omega * dt_substep`) to preserve the 1:1.4 frequency ratio critical for chaos.

### 4.5 Van der Pol Oscillator

```cpp
// dx/dt = v
// dv/dt = mu * (1 - x^2) * v - x
constexpr float mu = 1.0f;  // chaos parameter maps to [0.5, 5]
```

- **Initial state**: (0.5, 0)
- **Safe bounds**: 10
- **Normalization**: x/2.5, v depends on mu

**Note**: Classic Van der Pol is not chaotic (limit cycle), but adding forcing or high mu produces near-chaotic behavior with rich harmonics suitable for audio.

---

## 5. External Coupling Implementation

### Additive Forcing to x-Derivative

Per spec clarification, external coupling is applied as additive forcing to dx/dt:

```cpp
// In RK4 derivative computation:
float dxdt = originalDerivative + coupling_ * externalInput;
```

This approach:
- Preserves attractor geometry
- Allows synchronization and entrainment
- Integrates naturally with RK4 (affects all k1-k4 evaluations)

### Coupling Applied Within RK4

The coupling input must be constant during one audio sample but is applied to each RK4 substep:

```cpp
void rk4StepWithCoupling(float dtSubstep, float extInput) {
    float coupling = coupling_ * extInput;

    // k1
    float dx1 = computeDxDt(state_) + coupling;
    // ... rest of derivatives

    // k2
    AttractorState s2 = {
        state_.x + dtSubstep * dx1 / 2,
        // ...
    };
    float dx2 = computeDxDt(s2) + coupling;
    // ... etc
}
```

---

## 6. Summary of Key Decisions

| Decision | Rationale | Alternatives Rejected |
|----------|-----------|----------------------|
| Inline RK4 per attractor | Best performance, type-specific | Generic templated RK4 |
| Fixed maximum 100 substeps | Bounded CPU, sufficient for audio range | Unlimited substepping |
| DCBlocker at 10Hz | Standard, matches existing usage | Higher cutoff |
| Named struct members | Clarity for attractor equations | Array-based state |
| fastTanh for normalization | 3x faster than std::tanh | std::tanh (slower) |
| Separate ChaosAttractor enum | Different sets than ChaosModel | Extend ChaosModel |
| Attractor-time Duffing phase | Preserves chaos regime across frequencies | Real-time phase |
