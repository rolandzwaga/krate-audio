# Asymmetric Shaping Functions

## Overview

This specification defines a library of pure, stateless asymmetric waveshaping functions for the KrateDSP library. Unlike symmetric saturation (which generates odd harmonics), asymmetric shaping creates even harmonics by treating positive and negative signal halves differently - a key characteristic of tube amplifiers, diodes, and other analog circuitry.

**Layer**: 0 (Core Utilities)
**Location**: `dsp/include/krate/dsp/core/asymmetric.h`
**Namespace**: `Krate::DSP::Asymmetric`

## Problem Statement

The current `SaturationProcessor` contains tube-style and diode-style distortion algorithms embedded within a stateful processor class. This coupling prevents:

1. **Reuse**: Other processors cannot access the shaping algorithms without depending on `SaturationProcessor`
2. **Testing**: Algorithms cannot be unit-tested in isolation
3. **Composition**: Users cannot combine asymmetric shaping with other processing chains
4. **Optimization**: Pure functions enable compiler optimizations (inlining, constexpr evaluation)

Additionally, the DSP library lacks general-purpose asymmetric shaping primitives for creating even harmonics, limiting the harmonic palette available to higher-layer processors.

## User Scenarios & Testing

### Scenario 1: Creating Tube-Like Warmth

**Given** a producer wants to add subtle tube-like warmth to a clean signal
**When** they apply `tube()` with moderate input levels
**Then** the output exhibits characteristic even-harmonic content with soft compression on peaks

**Acceptance Criteria:**
- Second harmonic (2f) is present in output spectrum
- Output remains bounded for any input value
- No discontinuities in the transfer function
- Gradual onset of saturation (soft knee)

### Scenario 2: Aggressive Diode Clipping

**Given** an audio designer wants harsh, asymmetric clipping for a distortion effect
**When** they apply `diode()` with the input signal
**Then** the output clips asymmetrically with one polarity compressing more than the other

**Acceptance Criteria:**
- Positive and negative half-waves exhibit different saturation curves
- Transfer function is continuous (no hard discontinuities)
- Output is bounded regardless of input amplitude
- Even harmonics dominate the distortion spectrum

### Scenario 3: Custom Dual-Curve Shaping

**Given** a sound designer wants precise control over positive vs negative waveshaping
**When** they use `dualCurve()` with different gain values for each half
**Then** they can independently sculpt the harmonic content of each polarity

**Acceptance Criteria:**
- Positive samples processed with one gain, negative with another
- Transition at zero crossing is seamless (no discontinuity)
- Equal gains produce symmetric output matching base tanh curve
- Identity case (both gains = 1.0) matches standard tanh saturation

### Scenario 4: Simple Bias-Based Asymmetry

**Given** a developer wants to add subtle asymmetry to existing symmetric saturation
**When** they use `withBias()` to add DC bias before symmetric saturation
**Then** the symmetric curve operates on a shifted signal, creating even harmonics

**Acceptance Criteria:**
- DC offset applied before saturation, removed after
- Symmetric saturator becomes asymmetric in effect
- No net DC offset in output
- Works with any symmetric saturation function from Sigmoid library

## Functional Requirements

### FR-001: WithBias Template Function

The library shall provide a `withBias()` function that creates asymmetry by applying a DC bias before symmetric saturation.

**Signature:**
```cpp
template<typename SaturatorFn>
[[nodiscard]] inline float withBias(float input, float bias, SaturatorFn saturator) noexcept;
```

**Behavior:**
- Apply `bias` to input before saturation
- Apply the symmetric `saturator` function
- Remove bias effect from output to maintain DC neutrality
- The formula: `output = saturator(input + bias) - saturator(bias)`

**Constraints:**
- Bias range: any finite float value (typical range -1.0 to 1.0)
- Input/output: unbounded float values
- Must work with any callable that takes float and returns float

### FR-002: DualCurve Function

The library shall provide a `dualCurve()` function that applies different saturation gains to positive and negative half-waves using tanh as the base curve.

**Signature:**
```cpp
[[nodiscard]] inline float dualCurve(float input, float positiveGain, float negativeGain) noexcept;
```

**Behavior:**
- For input >= 0: apply `tanh(input * positiveGain)`
- For input < 0: apply `tanh(input * negativeGain)`
- Uses `Sigmoid::tanh()` as the underlying saturation curve

**Constraints:**
- Gains should be positive values (negative gains flip polarity)
- Zero crossing is seamless when both curves pass through origin
- Equal gains produce symmetric tanh saturation

### FR-003: Diode Clipping Function

The library shall provide a `diode()` function implementing asymmetric diode-style clipping, extracted from the existing `SaturationProcessor` implementation.

**Signature:**
```cpp
[[nodiscard]] inline float diode(float input) noexcept;
```

**Behavior:**
- Implements the existing diode algorithm from `SaturationProcessor`
- Positive half-wave: softer exponential saturation
- Negative half-wave: harder exponential saturation
- Maintains the exact transfer function of the current implementation

**Constraints:**
- Output bounded for any input value
- Must handle denormals and edge cases gracefully
- Behavior must match existing `SaturationProcessor::applyDiodeDistortion()`

### FR-004: Tube Polynomial Function

The library shall provide a `tube()` function implementing tube-style asymmetric saturation, extracted from the existing `SaturationProcessor` implementation.

**Signature:**
```cpp
[[nodiscard]] inline float tube(float input) noexcept;
```

**Behavior:**
- Implements the existing tube polynomial from `SaturationProcessor`
- Uses polynomial with both odd and even-order terms
- Even-order terms create asymmetry and even harmonics
- Odd-order terms provide symmetric compression
- Maintains the exact transfer function of the current implementation

**Constraints:**
- Output bounded for any input value
- Must match existing `SaturationProcessor::applyTubeDistortion()` behavior

### FR-005: Integration with Sigmoid Library

All asymmetric shaping functions shall integrate seamlessly with the `core/sigmoid.h` library.

**Requirements:**
- `withBias()` must accept any Sigmoid function as the saturator parameter
- Functions should follow the same API patterns as Sigmoid (inline, noexcept, [[nodiscard]])
- Examples demonstrating integration must be provided in documentation

### FR-006: Real-Time Safety

All functions shall be safe for use on audio processing threads.

**Requirements:**
- No dynamic memory allocation
- No exceptions thrown
- No blocking operations
- No I/O operations
- Deterministic execution time

### FR-007: Numerical Stability

All functions shall maintain numerical stability across the valid input range.

**Requirements:**
- No NaN output for finite, non-NaN inputs
- NaN input propagates to NaN output
- Infinity inputs produce bounded output (+/-1 or similar)
- Graceful handling of denormal values
- Consistent behavior across platforms (within floating-point tolerance)

## Success Criteria

| ID | Criterion | Measurement |
|----|-----------|-------------|
| SC-001 | Even harmonic generation | Spectral analysis confirms presence of 2nd harmonic in tube() and diode() output |
| SC-002 | Output boundedness | All functions produce output in reasonable range (approx [-1.5, 1.5]) for normalized input |
| SC-003 | Zero-crossing continuity | No discontinuities at x=0 in transfer function |
| SC-004 | Cross-platform consistency | Output matches within 1e-5 tolerance across Windows/macOS/Linux |
| SC-005 | SaturationProcessor compatibility | Refactored SaturationProcessor produces identical output to current implementation |
| SC-006 | Performance parity | Refactored SaturationProcessor performs within 5% of current implementation |

## Key Entities

### Asymmetric Namespace

All functions reside in `Krate::DSP::Asymmetric` namespace, following the pattern established by `Krate::DSP::Sigmoid`.

### Parameter Ranges

| Parameter | Type | Range | Default | Description |
|-----------|------|-------|---------|-------------|
| bias | float | (-inf, inf) | - | DC offset for biased saturation |
| positiveGain | float | [0, inf) | - | Saturation gain for positive half-wave |
| negativeGain | float | [0, inf) | - | Saturation gain for negative half-wave |

## Existing Functionality Analysis

### Code to Extract from SaturationProcessor

The following algorithms in `saturation_processor.h` shall be extracted:

1. **Tube Polynomial** (in `applyTubeDistortion()`):
   - Uses polynomial with x^2 term for asymmetry
   - Creates 2nd harmonic (asymmetric) + 3rd harmonic (symmetric)

2. **Diode Clipping** (in `applyDiodeDistortion()`):
   - Different exponential curves for positive/negative
   - Creates characteristic diode asymmetry

### Refactoring Strategy

After extracting to `core/asymmetric.h`:

1. `SaturationProcessor` internal methods call the new `Asymmetric::` functions
2. All existing tests must continue to pass unchanged
3. No changes to `SaturationProcessor` public API

## Assumptions

1. **Single-precision focus**: Functions target float (single-precision) as the primary type
2. **No SIMD in core layer**: SIMD optimization belongs in higher layers
3. **Sigmoid library is stable**: The recently completed sigmoid library API will not change
4. **Exact algorithm preservation**: Extracted algorithms must produce bit-identical output

## Dependencies

- `core/sigmoid.h` - For `Sigmoid::tanh()` used in `dualCurve()` and `withBias()` examples
- `<cmath>` - For `std::exp`, `std::abs`, `std::copysign`
- C++20 - For consistent behavior

## Out of Scope

- Oversampling (handled by processors, not core utilities)
- DC blocking (separate responsibility, already exists in the library)
- State management (these are pure functions)
- SIMD implementations (optimization layer concern)
- Double-precision overloads (can be added later if needed)
- Anti-aliasing (processor layer responsibility)
- New saturation algorithms (only extracting existing ones)

## Open Questions

None - all design decisions resolved based on existing implementations and sigmoid library patterns.
