# Data Model: Stochastic Shaper

**Feature**: 106-stochastic-shaper | **Date**: 2026-01-26

## Entity Overview

| Entity | Layer | Purpose |
|--------|-------|---------|
| StochasticShaper | 1 | Main class - waveshaper with stochastic modulation |

---

## StochasticShaper Class

### Composed Dependencies

| Component | Type | Layer | Purpose |
|-----------|------|-------|---------|
| waveshaper_ | Waveshaper | 1 | Delegated waveshaping (all 9 types) |
| rng_ | Xorshift32 | 0 | Deterministic random number generation |
| jitterSmoother_ | OnePoleSmoother | 1 | Smooths signal jitter offset |
| driveSmoother_ | OnePoleSmoother | 1 | Smooths drive modulation |

### Member Variables

| Name | Type | Default | Range | Description |
|------|------|---------|-------|-------------|
| waveshaper_ | Waveshaper | (default) | N/A | Composed waveshaper primitive |
| rng_ | Xorshift32 | seed=1 | N/A | PRNG for random values |
| jitterSmoother_ | OnePoleSmoother | (default) | N/A | Smoother for jitter offset |
| driveSmoother_ | OnePoleSmoother | (default) | N/A | Smoother for drive modulation |
| jitterAmount_ | float | 0.0f | [0.0, 1.0] | Jitter intensity (FR-009) |
| jitterRate_ | float | 10.0f | [0.01, sr/2] | Jitter smoothing rate Hz (FR-012) |
| coefficientNoise_ | float | 0.0f | [0.0, 1.0] | Drive modulation amount (FR-015) |
| baseDrive_ | float | 1.0f | [0, inf) | Base drive before modulation (FR-008b) |
| seed_ | uint32_t | 1 | [0, 2^32) | RNG seed (FR-019) |
| sampleRate_ | double | 44100.0 | [1000, 192000] | Current sample rate |
| prepared_ | bool | false | true/false | Initialization flag |
| currentJitter_ | float | 0.0f | [-0.5, 0.5] | Diagnostic: last jitter offset (FR-035) |
| currentDriveMod_ | float | 1.0f | varies | Diagnostic: last effective drive (FR-036) |

### Constants

| Name | Value | Description |
|------|-------|-------------|
| kDefaultJitterRate | 10.0f | Default jitter rate Hz (FR-014) |
| kMinJitterRate | 0.01f | Minimum jitter rate Hz (FR-012) |
| kMaxJitterOffset | 0.5f | Max jitter at amount=1.0 (FR-011) |
| kDriveModulationRange | 0.5f | +/- 50% at coeffNoise=1.0 (FR-017) |

---

## State Transitions

### Initialization State Machine

```
[Uninitialized] --prepare()--> [Prepared]
[Prepared] --reset()--> [Prepared] (state cleared, config preserved)
[Prepared] --process()--> [Prepared] (normal operation)
[Uninitialized] --process()--> [Uninitialized] (no-op, returns input)
```

### Processing State

Each process() call updates:
1. jitterSmoother_ state (via setTarget + process)
2. driveSmoother_ state (via setTarget + process)
3. rng_ state (via nextFloat x2)
4. currentJitter_ diagnostic
5. currentDriveMod_ diagnostic

---

## Relationships

### Dependency Graph

```
StochasticShaper (Layer 1)
    |
    +-- Waveshaper (Layer 1)
    |       +-- Sigmoid:: functions (Layer 0)
    |       +-- Asymmetric:: functions (Layer 0)
    |
    +-- Xorshift32 (Layer 0)
    |
    +-- OnePoleSmoother x2 (Layer 1)
            +-- detail::flushDenormal (Layer 0)
            +-- detail::isNaN (Layer 0)
            +-- detail::constexprExp (Layer 0)
```

### External Dependencies

| Header | Symbols Used |
|--------|--------------|
| `<krate/dsp/primitives/waveshaper.h>` | Waveshaper, WaveshapeType |
| `<krate/dsp/core/random.h>` | Xorshift32 |
| `<krate/dsp/primitives/smoother.h>` | OnePoleSmoother |
| `<krate/dsp/core/db_utils.h>` | detail::isNaN, detail::isInf |
| `<algorithm>` | std::clamp |
| `<cmath>` | std::abs |
| `<cstdint>` | uint32_t |
| `<cstddef>` | size_t |

---

## Processing Formulas

### Input Sanitization (FR-029, FR-030)

```cpp
float sanitizeInput(float x) const noexcept {
    if (detail::isNaN(x)) return 0.0f;       // FR-029
    if (detail::isInf(x)) {                   // FR-030
        return x > 0.0f ? 1.0f : -1.0f;
    }
    return x;
}
```

### Jitter Offset Calculation (FR-022)

```cpp
// jitterOffset = jitterAmount * smoothedRandom * 0.5
// At jitterAmount=1.0: offset range is [-0.5, +0.5] (FR-011)
float jitterOffset = jitterAmount_ * smoothedJitter * kMaxJitterOffset;
```

### Effective Drive Calculation (FR-023)

```cpp
// effectiveDrive = baseDrive * (1.0 + coeffNoise * smoothedRandom * 0.5)
// At coeffNoise=1.0: drive range is [0.5*base, 1.5*base] (FR-017)
float effectiveDrive = baseDrive_ * (1.0f + coefficientNoise_ * smoothedDriveMod * kDriveModulationRange);
```

### Jitter Rate to Smoothing Time (Research R1)

```cpp
// smoothTimeMs = 800 / jitterRate, clamped to [0.1, 1000]
float calculateSmoothingTime(float rateHz) const noexcept {
    return std::clamp(800.0f / rateHz, kMinSmoothingTimeMs, kMaxSmoothingTimeMs);
}
```

---

## Validation Rules

| Parameter | Rule | FR |
|-----------|------|-----|
| jitterAmount | Clamp to [0.0, 1.0] | FR-009 |
| jitterRate | Clamp to [0.01, sampleRate/2] | FR-012 |
| coefficientNoise | Clamp to [0.0, 1.0] | FR-015 |
| baseDrive | No clamping (Waveshaper handles negative via abs) | FR-008a |
| seed | 0 replaced with default by Xorshift32 | FR-021 |

---

## Thread Safety Notes

| Method | Thread Safety | Notes |
|--------|---------------|-------|
| prepare() | NOT thread-safe | Allocates smoother state |
| reset() | NOT thread-safe | Modifies all state |
| process() | Thread-safe* | *Only from audio thread |
| processBlock() | Thread-safe* | *Only from audio thread |
| set*() | Thread-safe for single writer | No atomic needed |
| get*() | Thread-safe | Read-only |
| getCurrentJitter() | Thread-safe | Read-only diagnostic |
| getCurrentDriveModulation() | Thread-safe | Read-only diagnostic |

**Note**: FR-037 states diagnostics "MUST NOT be used during audio processing". They are for inspection between process calls only.
