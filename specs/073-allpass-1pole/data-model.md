# Data Model: First-Order Allpass Filter (Allpass1Pole)

**Date**: 2026-01-21 | **Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)

## Entities

### Allpass1Pole

First-order allpass filter for frequency-dependent phase shifting.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| a_ | float | 0.0f | Filter coefficient, range (-1, +1) exclusive |
| z1_ | float | 0.0f | Input delay state (x[n-1]) |
| y1_ | float | 0.0f | Output feedback state (y[n-1]) |
| sampleRate_ | double | 44100.0 | Sample rate in Hz |

**Size**: 20 bytes (4 + 4 + 4 + 8) < 32 bytes requirement (SC-004)

**Invariants**:
- `a_` is always in range [-0.9999f, +0.9999f]
- `sampleRate_` is always > 0
- After `reset()`, `z1_ == 0.0f && y1_ == 0.0f`

### Relationships

```
Allpass1Pole
    |
    v
[Phaser Effect (Layer 4)] -- cascades 2-12 stages
    |
    v
[CrossoverFilter] -- uses for phase alignment
```

## State Transitions

### Filter State Machine

```
[Unprepared] --prepare(sampleRate)--> [Ready]
    ^                                    |
    |                                    v
    +----------- reset() <--------------+
                                         |
                                         v
                              [Processing]
                                    |
                              process()/processBlock()
                                    |
                                    v
                              [NaN Detected] --reset()--> [Ready]
```

### Coefficient State

```
setFrequency(hz)
    |
    v
coeffFromFrequency(hz, sampleRate_)
    |
    v
[a_ updated]

setCoefficient(a)
    |
    v
clamp(a, -0.9999f, +0.9999f)
    |
    v
[a_ updated]
```

## Validation Rules

### Frequency Clamping (FR-009)

```cpp
// In setFrequency() and coeffFromFrequency()
float clampedFreq = std::clamp(hz, 1.0f, static_cast<float>(sampleRate_) * 0.99f * 0.5f);
```

| Input | Action | Result |
|-------|--------|--------|
| freq < 1.0f | Clamp to minimum | 1.0f |
| freq > Nyquist * 0.99 | Clamp to maximum | sampleRate * 0.495 |
| 1.0f <= freq <= Nyquist * 0.99 | Pass through | freq |

### Coefficient Clamping (FR-008)

```cpp
// In setCoefficient()
float clampedA = std::clamp(a, -0.9999f, 0.9999f);
```

| Input | Action | Result |
|-------|--------|--------|
| a >= 1.0f | Clamp to max | 0.9999f |
| a <= -1.0f | Clamp to min | -0.9999f |
| -1.0f < a < 1.0f | Pass through | a |

### Input Validation (FR-014)

```cpp
// In process()
if (detail::isNaN(input) || detail::isInf(input)) {
    reset();
    return 0.0f;
}

// In processBlock() - early exit
if (detail::isNaN(buffer[0]) || detail::isInf(buffer[0])) {
    std::fill(buffer, buffer + numSamples, 0.0f);
    reset();
    return;
}
```

## Memory Layout

```
Allpass1Pole (20 bytes, aligned to 8 bytes = 24 bytes actual)
+---------------+---------------+---------------+
| a_ (4 bytes)  | z1_ (4 bytes) | y1_ (4 bytes) |
+---------------+---------------+---------------+
| sampleRate_ (8 bytes)                         |
+-----------------------------------------------+
```

## Processing Flow

### process(float input) -> float

```
1. Validate input (NaN/Inf check)
2. If invalid: reset(), return 0.0f
3. Compute: output = a_*input + z1_ - a_*y1_
4. Update state: z1_ = input, y1_ = output
5. Flush denormals: z1_ = flushDenormal(z1_), y1_ = flushDenormal(y1_)
6. Return output
```

### processBlock(float* buffer, size_t numSamples)

```
1. Validate first sample (NaN/Inf check)
2. If invalid: fill buffer with 0.0f, reset(), return
3. For each sample i = 0 to numSamples-1:
   a. output = a_*buffer[i] + z1_ - a_*y1_
   b. z1_ = buffer[i]
   c. y1_ = output
   d. buffer[i] = output
4. Flush denormals once at end: z1_ = flushDenormal(z1_), y1_ = flushDenormal(y1_)
```

## Coefficient Calculation

### coeffFromFrequency(float hz, double sampleRate) -> float

```
Formula: a = (1 - tan(pi * freq / sampleRate)) / (1 + tan(pi * freq / sampleRate))

1. Clamp freq to [1.0f, sampleRate * 0.99 * 0.5]
2. float sr = static_cast<float>(sampleRate)
3. float tan_val = std::tan(kPi * freq / sr)
4. float a = (1.0f - tan_val) / (1.0f + tan_val)
5. Return clamp(a, -0.9999f, 0.9999f)
```

### frequencyFromCoeff(float a, double sampleRate) -> float

```
Formula: freq = sampleRate * atan((1 - a) / (1 + a)) / pi

1. Clamp a to [-0.9999f, 0.9999f] to avoid division by zero
2. float sr = static_cast<float>(sampleRate)
3. float freq = sr * std::atan((1.0f - a) / (1.0f + a)) / kPi
4. Return freq (already in valid range due to coefficient clamping)
```

## Test Data Reference Values

### Unity Magnitude Test Points (SC-001)

| Frequency | Sample Rate | Expected Magnitude |
|-----------|-------------|-------------------|
| 100 Hz | 44100 Hz | 1.0 +/- 0.0001 |
| 1000 Hz | 44100 Hz | 1.0 +/- 0.0001 |
| 10000 Hz | 44100 Hz | 1.0 +/- 0.0001 |

### Phase Response Test Points (SC-002)

| Break Freq | Test Freq | Expected Phase |
|------------|-----------|----------------|
| 1000 Hz | 0 Hz (DC) | 0 degrees |
| 1000 Hz | 1000 Hz | -90 degrees +/- 0.1 |
| 1000 Hz | Nyquist | -180 degrees |

### Coefficient Calculation Reference Values (SC-005)

| Break Freq | Sample Rate | Expected Coefficient |
|------------|-------------|---------------------|
| 1000 Hz | 44100 Hz | ~0.8566 |
| 5000 Hz | 44100 Hz | ~0.3764 |
| 11025 Hz | 44100 Hz | 0.0 (fs/4) |
| 15000 Hz | 44100 Hz | ~-0.4317 |

### Round-Trip Tolerance

```cpp
// coeffFromFrequency -> frequencyFromCoeff
// Original: 1000 Hz at 44100 Hz
// Tolerance: 1e-6 (SC-005)
```
