# Data Model: Comb Filters

**Feature**: 074-comb-filter | **Date**: 2026-01-21

## Entity Overview

```
+-------------------+     +-------------------+     +-------------------+
| FeedforwardComb   |     | FeedbackComb      |     | SchroederAllpass  |
|-------------------|     |-------------------|     |-------------------|
| - delay_          |     | - delay_          |     | - delay_          |
| - gain_           |     | - feedback_       |     | - coefficient_    |
| - delaySamples_   |     | - damping_        |     | - feedbackState_  |
| - sampleRate_     |     | - dampingState_   |     | - delaySamples_   |
+-------------------+     | - delaySamples_   |     | - sampleRate_     |
         |                | - sampleRate_     |     +-------------------+
         |                +-------------------+              |
         |                         |                         |
         v                         v                         v
+---------------------------------------------------------------+
|                         DelayLine                             |
|---------------------------------------------------------------|
| - buffer_       : std::vector<float>                          |
| - mask_         : size_t                                      |
| - writeIndex_   : size_t                                      |
| - sampleRate_   : double                                      |
| - maxDelaySamples_ : size_t                                   |
+---------------------------------------------------------------+
```

---

## FeedforwardComb (FIR Comb Filter)

### Purpose
Creates spectral notches by combining the input with a delayed copy. Used for flanger, chorus, and doubling effects.

### Difference Equation
```
y[n] = x[n] + g * x[n-D]
```

### Members

| Member | Type | Default | Range | Description |
|--------|------|---------|-------|-------------|
| delay_ | DelayLine | - | - | Internal delay buffer |
| gain_ | float | 0.5f | [0.0, 1.0] | Feedforward gain coefficient |
| delaySamples_ | float | 1.0f | [1.0, maxDelay] | Current delay in samples |
| sampleRate_ | double | 0.0 | [8000, 192000] | Sample rate (0 = not prepared) |

### Methods

```cpp
// Lifecycle
void prepare(double sampleRate, float maxDelaySeconds) noexcept;
void reset() noexcept;

// Configuration
void setGain(float g) noexcept;              // Clamps to [0.0, 1.0]
void setDelaySamples(float samples) noexcept; // Clamps to [1.0, maxDelaySamples]
void setDelayMs(float ms) noexcept;          // Converts to samples internally

// Processing
[[nodiscard]] float process(float input) noexcept;
void processBlock(float* buffer, size_t numSamples) noexcept;
```

### State Transitions

```
UNPREPARED --prepare()--> PREPARED --reset()--> PREPARED (cleared)
     ^                        |
     |                        |
     +--------prepare()-------+
```

### Invariants
- `gain_` always in [0.0, 1.0]
- `delaySamples_` always in [1.0, maxDelaySamples]
- If `sampleRate_ == 0`, `process()` returns input unchanged (bypass)

---

## FeedbackComb (IIR Comb Filter)

### Purpose
Creates spectral peaks (resonances) through feedback. Used for Karplus-Strong synthesis and reverb comb banks. Optional damping provides natural high-frequency decay.

### Difference Equation
```
Without damping: y[n] = x[n] + g * y[n-D]
With damping:    y[n] = x[n] + g * LP(y[n-D])

Where LP is one-pole lowpass:
LP(x) = (1-d)*x + d*LP_prev
```

### Members

| Member | Type | Default | Range | Description |
|--------|------|---------|-------|-------------|
| delay_ | DelayLine | - | - | Internal delay buffer |
| feedback_ | float | 0.5f | [-0.9999, 0.9999] | Feedback gain coefficient |
| damping_ | float | 0.0f | [0.0, 1.0] | Damping amount (0=bright, 1=dark) |
| dampingState_ | float | 0.0f | - | One-pole LP state (flushed for denormals) |
| delaySamples_ | float | 1.0f | [1.0, maxDelay] | Current delay in samples |
| sampleRate_ | double | 0.0 | [8000, 192000] | Sample rate (0 = not prepared) |

### Methods

```cpp
// Lifecycle
void prepare(double sampleRate, float maxDelaySeconds) noexcept;
void reset() noexcept;

// Configuration
void setFeedback(float g) noexcept;          // Clamps to [-0.9999, 0.9999]
void setDamping(float d) noexcept;           // Clamps to [0.0, 1.0]
void setDelaySamples(float samples) noexcept;
void setDelayMs(float ms) noexcept;

// Processing
[[nodiscard]] float process(float input) noexcept;
void processBlock(float* buffer, size_t numSamples) noexcept;
```

### Damping Behavior

| damping_ | Effect | Use Case |
|----------|--------|----------|
| 0.0 | No damping, bright sound | Metallic resonances |
| 0.3 | Light damping | Guitar strings |
| 0.5 | Medium damping | General reverb |
| 0.7 | Heavy damping | Warm reverb |
| 1.0 | Maximum damping (DC only) | Extreme effect |

### Invariants
- `feedback_` always in [-0.9999f, 0.9999f] (stability)
- `damping_` always in [0.0, 1.0]
- `dampingState_` flushed to 0 if denormal
- If `sampleRate_ == 0`, `process()` returns input unchanged (bypass)

---

## SchroederAllpass

### Purpose
Provides unity magnitude response (flat frequency) while dispersing phase. Used for reverb diffusion networks to spread transients without altering tonal balance.

### Difference Equation
```
y[n] = -g*x[n] + x[n-D] + g*y[n-D]
```

### Members

| Member | Type | Default | Range | Description |
|--------|------|---------|-------|-------------|
| delay_ | DelayLine | - | - | Internal delay buffer |
| coefficient_ | float | 0.7f | [-0.9999, 0.9999] | Allpass coefficient |
| feedbackState_ | float | 0.0f | - | y[n-D] state (flushed for denormals) |
| delaySamples_ | float | 1.0f | [1.0, maxDelay] | Current delay in samples |
| sampleRate_ | double | 0.0 | [8000, 192000] | Sample rate (0 = not prepared) |

### Methods

```cpp
// Lifecycle
void prepare(double sampleRate, float maxDelaySeconds) noexcept;
void reset() noexcept;

// Configuration
void setCoefficient(float g) noexcept;       // Clamps to [-0.9999, 0.9999]
void setDelaySamples(float samples) noexcept;
void setDelayMs(float ms) noexcept;

// Processing
[[nodiscard]] float process(float input) noexcept;
void processBlock(float* buffer, size_t numSamples) noexcept;
```

### Coefficient Behavior

| coefficient_ | Diffusion | Impulse Character |
|--------------|-----------|-------------------|
| 0.0 | No diffusion | Single echo at D samples |
| 0.5 | Moderate diffusion | Balanced decay |
| 0.7 | High diffusion (typical) | Dense, smooth decay |
| 0.9 | Very high diffusion | Long, washy tail |

### Invariants
- `coefficient_` always in [-0.9999f, 0.9999f] (stability)
- `feedbackState_` flushed to 0 if denormal
- Magnitude response is unity at all frequencies (within 0.01 dB)
- If `sampleRate_ == 0`, `process()` returns input unchanged (bypass)

---

## Common Edge Case Handling (All Three Types)

### NaN/Infinity Input (FR-021)

```cpp
float process(float input) noexcept {
    // Check for invalid input
    if (detail::isNaN(input) || detail::isInf(input)) {
        reset();  // Clear state
        return 0.0f;
    }
    // ... normal processing
}
```

### Unprepared State (FR-018 equivalent)

```cpp
float process(float input) noexcept {
    if (sampleRate_ == 0.0) {
        return input;  // Bypass
    }
    // ... normal processing
}
```

### Delay Clamping

```cpp
void setDelaySamples(float samples) noexcept {
    delaySamples_ = std::clamp(samples, 1.0f,
        static_cast<float>(delay_.maxDelaySamples()));
}
```

---

## Memory Layout Summary

| Class | sizeof (estimated) | Notes |
|-------|-------------------|-------|
| FeedforwardComb | ~120 bytes | DelayLine (~100) + members (~20) |
| FeedbackComb | ~128 bytes | DelayLine (~100) + members (~28) |
| SchroederAllpass | ~124 bytes | DelayLine (~100) + members (~24) |

Note: DelayLine contains a `std::vector<float>` so the actual buffer memory is heap-allocated based on `maxDelaySeconds`.
