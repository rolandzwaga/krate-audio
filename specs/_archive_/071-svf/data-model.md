# Data Model: State Variable Filter (SVF)

**Feature**: 071-svf | **Date**: 2026-01-21 | **Layer**: 1 (Primitive)

## Entities

This document defines the data structures for the TPT State Variable Filter implementation.

---

## SVFMode (Enum)

**Purpose**: Selects which filter response type to output from `process()`.

**Location**: `dsp/include/krate/dsp/primitives/svf.h`

```cpp
/// @brief Filter mode selection for SVF::process() output.
///
/// Determines which linear combination of LP/HP/BP outputs is returned.
/// For simultaneous access to all outputs, use SVF::processMulti() instead.
enum class SVFMode : uint8_t {
    Lowpass,   ///< 12 dB/oct lowpass, -3dB at cutoff
    Highpass,  ///< 12 dB/oct highpass, -3dB at cutoff
    Bandpass,  ///< Constant 0 dB peak gain
    Notch,     ///< Band-reject filter
    Allpass,   ///< Flat magnitude, phase shift
    Peak,      ///< Parametric EQ bell curve (uses gainDb)
    LowShelf,  ///< Boost/cut below cutoff (uses gainDb)
    HighShelf  ///< Boost/cut above cutoff (uses gainDb)
};
```

**Validation Rules**:
- No validation needed (enum class provides type safety)
- All 8 values are valid

**Note**: Separate from `FilterType` in biquad.h because SVF mode mixing differs (e.g., Allpass uses `-k` coefficient).

---

## SVFOutputs (Struct)

**Purpose**: Contains all four simultaneous filter outputs from `processMulti()`.

**Location**: `dsp/include/krate/dsp/primitives/svf.h`

```cpp
/// @brief Simultaneous outputs from SVF::processMulti().
///
/// All four outputs are computed in a single processing cycle with minimal
/// additional overhead compared to single-output processing.
struct SVFOutputs {
    float low;   ///< Lowpass output (12 dB/oct)
    float high;  ///< Highpass output (12 dB/oct)
    float band;  ///< Bandpass output (constant 0 dB peak)
    float notch; ///< Notch (band-reject) output
};
```

**Fields**:

| Field | Type | Range | Description |
|-------|------|-------|-------------|
| low | float | [-inf, +inf] | Lowpass filter output |
| high | float | [-inf, +inf] | Highpass filter output |
| band | float | [-inf, +inf] | Bandpass filter output |
| notch | float | [-inf, +inf] | Notch (band-reject) output |

**Notes**:
- Peak, allpass, and shelf outputs are not included; use `process()` with appropriate `SVFMode` for those
- Notch is computed as `low + high` (equivalent formulation)

---

## SVF (Class)

**Purpose**: TPT/Trapezoidal integrated state variable filter with excellent modulation stability.

**Location**: `dsp/include/krate/dsp/primitives/svf.h`

### Class Declaration

```cpp
/// @brief TPT State Variable Filter with excellent modulation stability.
///
/// Implements the Cytomic TPT (Topology-Preserving Transform) SVF topology.
/// Provides simultaneous lowpass, highpass, bandpass, and notch outputs,
/// plus peak and shelf modes for parametric EQ applications.
///
/// Key advantages over Biquad:
/// - Modulation-stable: No clicks when cutoff/Q change at audio rate
/// - Multi-output: Get LP/HP/BP/Notch in one computation
/// - Orthogonal: Cutoff and Q are truly independent parameters
///
/// @example
/// ```cpp
/// SVF filter;
/// filter.prepare(44100.0);
/// filter.setMode(SVFMode::Lowpass);
/// filter.setCutoff(1000.0f);
/// filter.setResonance(0.7071f);
///
/// // Single output processing
/// float out = filter.process(input);
///
/// // Multi-output processing
/// SVFOutputs outputs = filter.processMulti(input);
/// // outputs.low, outputs.high, outputs.band, outputs.notch all available
/// ```
class SVF {
    // ... see API below
};
```

### Member Variables

| Member | Type | Default | Description |
|--------|------|---------|-------------|
| sampleRate_ | double | 44100.0 | Current sample rate in Hz |
| cutoffHz_ | float | 1000.0f | Cutoff frequency in Hz |
| q_ | float | 0.7071f | Q factor (resonance) |
| gainDb_ | float | 0.0f | Gain in dB for shelf/peak modes |
| mode_ | SVFMode | Lowpass | Current filter mode |
| prepared_ | bool | false | True after prepare() called |
| g_ | float | 0.0f | tan(pi * fc / fs) coefficient |
| k_ | float | 1.0f/q_ | 1/Q damping coefficient |
| a1_ | float | 0.0f | 1 / (1 + g*(g+k)) |
| a2_ | float | 0.0f | g * a1 |
| a3_ | float | 0.0f | g * a2 |
| A_ | float | 1.0f | 10^(gainDb/40) for shelf/peak |
| m0_ | float | 0.0f | Mode mix: high coefficient |
| m1_ | float | 0.0f | Mode mix: band coefficient |
| m2_ | float | 1.0f | Mode mix: low coefficient |
| ic1eq_ | float | 0.0f | Integrator 1 state |
| ic2eq_ | float | 0.0f | Integrator 2 state |

### Parameter Constraints

| Parameter | Min | Max | Clamping Behavior |
|-----------|-----|-----|-------------------|
| sampleRate | 1000.0 | - | Clamp to 1000.0 if below |
| cutoff | 1.0 Hz | sampleRate * 0.495 | Clamp silently |
| Q | 0.1 | 30.0 | Clamp silently |
| gainDb | -24.0 dB | +24.0 dB | Clamp silently |

### State Variables

The filter maintains two integrator state variables:

```cpp
float ic1eq_ = 0.0f;  // Integrator 1 equivalent state
float ic2eq_ = 0.0f;  // Integrator 2 equivalent state
```

These are updated using the trapezoidal integration rule:
```cpp
ic1eq_ = 2.0f * v1 - ic1eq_;
ic2eq_ = 2.0f * v2 - ic2eq_;
```

### Coefficient Relationships

**Primary coefficients** (set by parameters):
```cpp
g_ = std::tan(kPi * cutoffHz_ / sampleRate_);
k_ = 1.0f / q_;
A_ = detail::constexprPow10(gainDb_ / 40.0f);
```

**Derived coefficients** (computed from primary):
```cpp
a1_ = 1.0f / (1.0f + g_ * (g_ + k_));
a2_ = g_ * a1_;
a3_ = g_ * a2_;
```

**Mode mixing coefficients** (depend on mode):

| Mode | m0_ | m1_ | m2_ |
|------|-----|-----|-----|
| Lowpass | 0 | 0 | 1 |
| Highpass | 1 | 0 | 0 |
| Bandpass | 0 | 1 | 0 |
| Notch | 1 | 0 | 1 |
| Allpass | 1 | -k_ | 1 |
| Peak | 1 | 0 | -1 |
| LowShelf | 1 | k_*(A_-1) | A_*A_ |
| HighShelf | A_*A_ | k_*(A_-1) | 1 |

### Processing Algorithm

Per-sample processing (FR-016):

```cpp
// Compute intermediate values
float v3 = input - ic2eq_;                              // HP contribution
float v1 = a1_ * ic1eq_ + a2_ * v3;                     // BP output
float v2 = ic2eq_ + a2_ * ic1eq_ + a3_ * v3;            // LP output

// Update integrator states (trapezoidal rule)
ic1eq_ = 2.0f * v1 - ic1eq_;
ic2eq_ = 2.0f * v2 - ic2eq_;

// Flush denormals
ic1eq_ = detail::flushDenormal(ic1eq_);
ic2eq_ = detail::flushDenormal(ic2eq_);

// Compute outputs
float low = v2;
float band = v1;
float high = v3 - k_ * v1 - v2;
float notch = low + high;  // Or: v3 - k_ * v1

// Mode mixing for process()
float output = m0_ * high + m1_ * band + m2_ * low;
```

---

## Relationships

```
SVFMode (enum)
    |
    +-- Selects --> SVF::mode_
                        |
                        +-- Determines --> m0_, m1_, m2_ (mixing coefficients)

SVFOutputs (struct)
    |
    +-- Returned by --> SVF::processMulti()

SVF (class)
    |
    +-- Uses --> detail::flushDenormal() (Layer 0)
    +-- Uses --> detail::isNaN() (Layer 0)
    +-- Uses --> detail::isInf() (Layer 0)
    +-- Uses --> detail::constexprPow10() (Layer 0)
    +-- Uses --> kPi (Layer 0)
```

---

## Validation Summary

| Entity | Validation | Location |
|--------|------------|----------|
| SVFMode | Enum class (compile-time) | N/A |
| SVFOutputs | No validation needed | N/A |
| SVF parameters | Clamped in setters | setCutoff(), setResonance(), setGain() |
| SVF input | NaN/Inf check | process(), processMulti() |
| SVF state | Denormal flush | process(), processMulti() |

---

## Memory Layout

```cpp
class SVF {
    // Configuration (32 bytes)
    double sampleRate_;     // 8 bytes
    float cutoffHz_;        // 4 bytes
    float q_;               // 4 bytes
    float gainDb_;          // 4 bytes
    SVFMode mode_;          // 1 byte
    bool prepared_;         // 1 byte
    // padding              // 2 bytes (alignment to 4)
    // Subtotal: 24 bytes + 2 padding = 26, aligned to 32

    // Coefficients (44 bytes)
    float g_;               // 4 bytes
    float k_;               // 4 bytes
    float a1_;              // 4 bytes
    float a2_;              // 4 bytes
    float a3_;              // 4 bytes
    float A_;               // 4 bytes
    float m0_;              // 4 bytes
    float m1_;              // 4 bytes
    float m2_;              // 4 bytes
    // Subtotal: 36 bytes

    // State (8 bytes)
    float ic1eq_;           // 4 bytes
    float ic2eq_;           // 4 bytes
};

// Total: ~70 bytes (may vary with alignment)
```

This is well within Layer 1 memory budget (minimal per-instance overhead).
