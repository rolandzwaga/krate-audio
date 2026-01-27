# Data Model: Hilbert Transform

**Spec**: 094-hilbert-transform | **Date**: 2026-01-24

## Entities

### HilbertOutput

**Purpose**: Return type for single-sample processing, containing both components of the analytic signal.

| Field | Type | Description |
|-------|------|-------------|
| i | float | In-phase component (original signal, delayed to match quadrature path) |
| q | float | Quadrature component (90 degrees phase-shifted) |

**Invariants**:
- Both fields are finite floats (no NaN/Inf propagated)
- Phase difference between i and q is 90 degrees +/- 1 degree within effective bandwidth
- Magnitude of both signals is unity (allpass property)

---

### HilbertTransform

**Purpose**: Main class implementing the Hilbert transform using allpass filter cascade approximation.

#### State Variables

| Field | Type | Initial Value | Description |
|-------|------|---------------|-------------|
| ap1_[4] | Allpass1Pole[4] | Default constructed | Path 1 allpass cascade (in-phase) |
| delay1_ | float | 0.0f | One-sample delay for path alignment |
| ap2_[4] | Allpass1Pole[4] | Default constructed | Path 2 allpass cascade (quadrature) |
| sampleRate_ | double | 44100.0 | Configured sample rate |

#### Configuration Methods

| Method | Parameters | Returns | Description |
|--------|------------|---------|-------------|
| prepare | double sampleRate | void | Initialize for given sample rate, configure allpass coefficients |
| reset | - | void | Clear all internal filter states to zero |

#### Processing Methods

| Method | Parameters | Returns | Description |
|--------|------------|---------|-------------|
| process | float input | HilbertOutput | Process single sample, return I and Q components |
| processBlock | const float* input, float* outI, float* outQ, int numSamples | void | Process block of samples |

#### Query Methods

| Method | Parameters | Returns | Description |
|--------|------------|---------|-------------|
| getSampleRate | - | double | Get configured sample rate |
| getLatencySamples | - | int | Get group delay (always returns 5) |

---

## Constants

> **Note**: The anonymous namespace pattern shown below is the IMPLEMENTATION approach.
> The [contracts/hilbert_transform.h](contracts/hilbert_transform.h) file shows API documentation
> with coefficients in comments. During implementation, coefficients are stored as `constexpr`
> constants in the anonymous namespace of the header file and passed to each `Allpass1Pole`
> instance during `prepare()`.

```cpp
namespace {

/// Minimum valid sample rate (Hz)
constexpr double kMinHilbertSampleRate = 22050.0;

/// Maximum valid sample rate (Hz)
constexpr double kMaxHilbertSampleRate = 192000.0;

/// Fixed latency in samples
constexpr int kHilbertLatencySamples = 5;

/// Path 1 (In-phase) allpass coefficients
/// Olli Niemitalo optimized coefficients for wideband Hilbert transform
constexpr float kHilbertPath1Coeffs[4] = {
    0.6923878f,
    0.9360654322959f,
    0.9882295226860f,
    0.9987488452737f
};

/// Path 2 (Quadrature) allpass coefficients
constexpr float kHilbertPath2Coeffs[4] = {
    0.4021921162426f,
    0.8561710882420f,
    0.9722909545651f,
    0.9952884791278f
};

} // anonymous namespace
```

---

## State Transitions

### Lifecycle

```
                ┌─────────────┐
                │ Constructed │
                │  (Default)  │
                └──────┬──────┘
                       │ prepare(sampleRate)
                       ▼
                ┌─────────────┐
                │   Ready     │ ◄────────────────┐
                │ (Prepared)  │                  │
                └──────┬──────┘                  │
                       │ process() /             │ reset()
                       │ processBlock()          │
                       ▼                         │
                ┌─────────────┐                  │
                │ Processing  │ ─────────────────┘
                │  (Active)   │
                └─────────────┘
```

### State after reset()

| Field | Value |
|-------|-------|
| ap1_[0..3] | All Allpass1Pole internal states = 0 |
| delay1_ | 0.0f |
| ap2_[0..3] | All Allpass1Pole internal states = 0 |
| sampleRate_ | Unchanged |

---

## Processing Flow

### Single Sample (process)

```
Input x[n]
    │
    ├────────────────────────────────────┐
    │                                    │
    ▼                                    ▼
Path 1 (In-phase)                    Path 2 (Quadrature)
    │                                    │
    ▼                                    ▼
ap1_[0].process(x)                   ap2_[0].process(x)
    │                                    │
    ▼                                    ▼
ap1_[1].process(...)                 ap2_[1].process(...)
    │                                    │
    ▼                                    ▼
ap1_[2].process(...)                 ap2_[2].process(...)
    │                                    │
    ▼                                    ▼
ap1_[3].process(...)                 ap2_[3].process(...)
    │                                    │
    ▼                                    │
delay1_ (z^-1)                          │
    │                                    │
    ▼                                    ▼
Output I                             Output Q
    │                                    │
    └────────────────┬───────────────────┘
                     │
                     ▼
              HilbertOutput{i, q}
```

### Block Processing (processBlock)

```cpp
for (int i = 0; i < numSamples; ++i) {
    HilbertOutput out = process(input[i]);
    outI[i] = out.i;
    outQ[i] = out.q;
}
```

Note: processBlock must produce bit-identical results to repeated process() calls (SC-005).

---

## Validation Rules

### Sample Rate

| Condition | Action |
|-----------|--------|
| sampleRate < 22050 | Clamp to 22050 |
| sampleRate > 192000 | Clamp to 192000 |
| sampleRate in [22050, 192000] | Use as-is |

### Input Validation

| Input | Action | Output |
|-------|--------|--------|
| Normal float | Process normally | Valid HilbertOutput |
| NaN | Allpass1Pole handles internally | Both outputs = 0 |
| Inf | Allpass1Pole handles internally | Both outputs = 0 |
| Denormal | Flushed to zero by allpass | Valid output |

---

## Memory Requirements

| Component | Size (bytes) | Notes |
|-----------|--------------|-------|
| Allpass1Pole | ~24 | 3 floats + 1 double |
| ap1_[4] | ~96 | 4 x Allpass1Pole |
| delay1_ | 4 | Single float |
| ap2_[4] | ~96 | 4 x Allpass1Pole |
| sampleRate_ | 8 | Double |
| **Total** | **~204** | Acceptable for Layer 1 |
