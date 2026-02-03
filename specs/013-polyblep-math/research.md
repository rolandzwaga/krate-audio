# Research: PolyBLEP Math Foundations

**Spec**: 013-polyblep-math | **Date**: 2026-02-03

---

## R1: 2-Point PolyBLEP Polynomial Formulation

**Decision**: Use the standard 2nd-degree polynomial from Valimaki & Pekonen (2012), parameterized by normalized phase `t` in [0,1) and normalized frequency `dt` = frequency/sampleRate.

**Rationale**: This is the established formulation used by virtually all PolyBLEP implementations (Martin Finke, Tale/KVR, Paul Batchelor sndkit). It is computationally minimal (one branch, one squaring) and provides the C1 continuity documented in the spec.

**Formulation**:

```
polyBlep(t, dt):
  if t < dt:
    x = t / dt                    // normalize to [0, 1)
    return -(x * x - 2.0 * x + 1.0)   // equivalent to -(x-1)^2
  if t > 1.0 - dt:
    x = (t - 1.0) / dt            // normalize to (-1, 0]
    return x * x + 2.0 * x + 1.0  // equivalent to (x+1)^2
  return 0.0
```

Equivalently (Martin Finke form):
- Near 0: `-(t/dt - 1)^2`
- Near 1: `((t-1)/dt + 1)^2`

**Alternatives Considered**:
- DPW (Differentiated Parabolic Wave): Simpler but has DC offset issues and cannot handle pulse width modulation. Rejected.
- minBLEP (Minimum-phase BLEP via FFT): Higher quality but requires precomputed table, FFT, and is stateful. Rejected for Layer 0 pure-math scope.

**Sources**:
- Valimaki & Pekonen, "Perceptually informed synthesis of bandlimited classical waveforms using integrated polynomial interpolation" (2012)
- Martin Finke PolyBLEP implementation (GitHub: martinfinke/PolyBLEP)
- Paul Batchelor sndkit/blep

---

## R2: 4-Point PolyBLEP Polynomial Formulation

**Decision**: Use integrated 3rd-order B-spline basis functions (4th-degree polynomial segments) from the ryukau filter_notes reference, which provides a 4-sample correction kernel with C³ continuity. Note: `polyBlep4` uses these 4th-degree segments directly; `polyBlamp4` (the integral) uses 5th-degree segments.

**Rationale**: The 4-point variant uses the B-spline approach where integrating the 3rd-order B-spline basis yields 4th-degree polynomial segments. This is the standard higher-order polyBLEP documented in the academic literature (Pekonen, Esqueda et al.). The correction region extends to 2*dt on each side of the discontinuity, matching the spec's FR-008.

**Formulation** (using integrated B-spline residuals, t normalized to [0, dt*2) or [1-dt*2, 1)):

The 4-point polyBLEP correction uses 4 polynomial segments over the interval [-2, 2] in normalized sample units. When parameterized by phase t and increment dt:

```
polyBlep4(t, dt):
  dt2 = 2.0 * dt
  if t < dt2:
    // Correction near phase = 0 (after wrap)
    // Map t from [0, dt2) to [0, 2) normalized sample units
    u = t / dt     // u in [0, 2)
    if u < 1.0:
      // Segment 0: u in [0, 1) -> polynomial from JB4,2
      return -(u^4/8 - u^3/6 - u^2/4 - u/6 - 1/24) [adjusted for sign]
    else:
      // Segment 1: u in [1, 2) -> polynomial from JB4,3
      ...
  if t > 1.0 - dt2:
    // Correction near phase = 1 (before wrap), symmetric
    ...
  return 0.0
```

The exact coefficients from the integrated B-spline basis (JB4,0 through JB4,3):
- JB4,0(u) = -u^4/24 + u^3/6 - u^2/4 + u/6 - 1/24 (u in [-2, -1))
- JB4,1(u) = u^4/8 - u^3/3 + 2u/3 - 1/2 (u in [-1, 0))
- JB4,2(u) = -u^4/8 + u^3/6 + u^2/4 + u/6 + 1/24 (u in [0, 1))
- JB4,3(u) = u^4/24 (u in [1, 2))

For the oscillator use case, these are applied symmetrically: the "before wrap" region uses JB4,0 and JB4,1, the "after wrap" region uses JB4,2 and JB4,3.

**Alternatives Considered**:
- 7th-order 4-point polyBLEP (from "Better polybleps polynomials" KVR thread): Higher suppression but significantly more computation. Not clearly documented in peer-reviewed literature. Rejected for initial implementation; could be added later.
- 6-point or 8-point variants: Even higher quality but increase the correction region (3*dt, 4*dt) and add more branching. Out of scope.

**Sources**:
- ryukau filter_notes polyblep_residual (https://ryukau.github.io/filter_notes/polyblep_residual/)
- Pekonen et al., higher-order B-spline PolyBLEP methods
- KVR Audio Forum PolyBLEP oscillators thread

---

## R3: 2-Point PolyBLAMP Polynomial Formulation

**Decision**: Use the integrated polyBLEP (cubic polynomial) from Martin Finke's implementation, which is the standard 2-point PolyBLAMP.

**Rationale**: PolyBLAMP is derived by integrating the polyBLEP function once more. This produces a cubic (3rd-degree) polynomial that corrects derivative discontinuities (ramp discontinuities) such as triangle wave peaks. The Martin Finke formulation is simple and matches the 2-point pattern.

**Formulation**:

```
polyBlamp(t, dt):
  if t < dt:
    x = t / dt - 1.0           // normalize to [-1, 0)
    return -(1.0/3.0) * x^3    // = -(1/3)(t/dt - 1)^3
  if t > 1.0 - dt:
    x = (t - 1.0) / dt + 1.0   // normalize to (0, 1]
    return (1.0/3.0) * x^3     // = (1/3)((t-1)/dt + 1)^3
  return 0.0
```

Note: The BLAMP correction must be scaled by `dt` when applied, because the derivative discontinuity magnitude is in units of amplitude/sample.

**Alternatives Considered**:
- ADAA (Anti-Derivative Anti-Aliasing): More accurate for static waveshaping but requires computing antiderivatives. Already used in `hard_clip_adaa.h` and `tanh_adaa.h` for different purposes. PolyBLAMP is preferred for oscillator context.

**Sources**:
- Esqueda, Valimaki, Bilbao, "Rounding Corners with BLAMP" (DAFx-16, 2016)
- Martin Finke PolyBLEP implementation (blamp function)

---

## R4: 4-Point PolyBLAMP Polynomial Formulation

**Decision**: Use the 4-point polyBLAMP residual from the DAFx-16 paper by Esqueda, Valimaki, Bilbao (Table 1), which is already partially implemented in `hard_clip_polyblamp.h`.

**Rationale**: The existing `HardClipPolyBLAMP::blampResidual()` in the codebase already implements the exact 4-point polyBLAMP residual from the DAFx-16 paper. However, that implementation is parameterized differently (by fractional delay `d` and sample index `n`) and is specific to the hard-clip context. The new `polyBlamp4(t, dt)` will use the same mathematical foundation but with the standard (t, dt) oscillator parameterization. This ensures no ODR conflict while providing a general-purpose interface.

**Formulation** (from DAFx-16 Table 1, adapted to (t, dt) parameterization):

The 4-point BLAMP residual for sample positions -2, -1, 0, +1 relative to the discontinuity, where d is the fractional sample position [0, 1):

- n=-2: d^5/120
- n=-1: -d^5/40 + d^4/24 + d^3/12 + d^2/12 + d/24 + 1/120
- n=0:  d^5/40 - d^4/12 + d^2/3 - d/2 + 7/30
- n=+1: -d^5/120 + d^4/24 - d^3/12 + d^2/12 - d/24 + 1/120

For the general oscillator (t, dt) interface, d is computed from the phase position relative to the discontinuity, and the four correction values are summed into a single returned value (pre-wrap and post-wrap regions).

**Relationship to existing HardClipPolyBLAMP**: The existing `blampResidual(d, n)` is used internally by `HardClipPolyBLAMP::applyCorrection()` where it applies corrections across a 4-sample buffer. The new `polyBlamp4(t, dt)` is a stateless free function returning a single correction value, suitable for the standard oscillator pattern where you add the correction at the current sample time.

**Alternatives Considered**:
- Reusing `HardClipPolyBLAMP::blampResidual()` directly: Not suitable because it requires buffer management and has a different API contract (sample index n, fractional delay d). The oscillator use case needs a simple (t, dt) -> float interface.
- Extracting shared residual math: Could be done but would create a dependency from Layer 1 to Layer 0 that does not currently exist for this component, and the parameterization differs enough that the code would not be simpler.

**Sources**:
- Esqueda, Valimaki, Bilbao, "Rounding Corners with BLAMP" (DAFx-16, 2016) - Table 1
- Existing codebase: `dsp/include/krate/dsp/primitives/hard_clip_polyblamp.h` lines 195-224

---

## R5: Phase Accumulator Design Pattern

**Decision**: Implement `PhaseAccumulator` as a lightweight POD-like struct with public members (`phase`, `increment`) and methods (`advance()`, `reset()`, `setFrequency()`). Use `double` precision for both phase and increment.

**Rationale**: The existing codebase pattern in `lfo.h` (lines 449-450) and `audio_rate_filter_fm.h` (line 464) uses `double phase_` and `double phaseIncrement_` with the wrapping pattern `if (phase_ >= 1.0) phase_ -= 1.0`. The new `PhaseAccumulator` must be drop-in compatible with this pattern to enable future refactoring.

**Design**:
```cpp
struct PhaseAccumulator {
    double phase = 0.0;      // Current phase [0, 1)
    double increment = 0.0;  // Phase advance per sample

    [[nodiscard]] bool advance() noexcept;  // Returns true on wrap
    void reset() noexcept;                   // Reset phase to 0
    void setFrequency(float frequency, float sampleRate) noexcept;
};
```

**Alternatives Considered**:
- Class with private members and getters: Rejected because the spec explicitly requires "a value type (struct with public members), not a class with encapsulated state" (Assumption section).
- Template for float/double: Rejected because the spec explicitly requires double precision (FR-021), and all existing oscillators use double.
- Including sync/reset functionality: Out of scope for Phase 1; will be added in Phase 5 (Sync Oscillator).

---

## R6: wrapPhase Naming Conflict Analysis

**Decision**: The new `wrapPhase(double)` in `phase_utils.h` will NOT conflict with the existing `wrapPhase(float)` in `spectral_utils.h` because: (a) different parameter type (double vs float), (b) different semantics (wraps to [0, 1) vs wraps to [-pi, pi]), and (c) different namespace context (Krate::DSP vs Krate::DSP for spectral_utils.h, but the function is in the spectral utilities context).

**Rationale**: The existing `wrapPhase(float phase)` in `dsp/include/krate/dsp/primitives/spectral_utils.h` wraps phase to [-pi, pi] for spectral processing. The new `wrapPhase(double phase)` in `dsp/include/krate/dsp/core/phase_utils.h` wraps to [0, 1) for oscillator phase accumulation. These serve fundamentally different purposes. The overload resolution is unambiguous due to the different parameter types (float vs double).

**Risk Mitigation**: Document the semantic difference clearly in the header comments. If both headers are included in the same translation unit, the compiler will select the correct overload based on argument type. This is safe C++ overload resolution.

**Note**: The `calculatePhaseIncrement` in `multistage_env_filter.h` is a private member function with a completely different signature (takes `timeMs`, returns `1.0/timeSamples`). No conflict.

---

## R7: constexpr Compatibility for PolyBLEP/BLAMP Functions

**Decision**: All four PolyBLEP/BLAMP functions can be `constexpr` because they use only arithmetic operations (+, -, *, /), comparisons, and float literals. No `std::` math functions (sin, cos, sqrt, etc.) are needed.

**Rationale**: The polynomial formulations are pure arithmetic. Unlike `sigmoid.h` which needs `std::tanh` (non-constexpr on MSVC), the polyBLEP functions only perform additions, multiplications, and comparisons. This makes them trivially constexpr-compatible on all compilers.

**Verification**: The existing `interpolation.h` provides the reference pattern -- all three interpolation functions are `[[nodiscard]] constexpr float ... noexcept` using only arithmetic, which compiles cleanly on MSVC, Clang, and GCC.

---

## R8: Test Strategy for Mathematical Validation

**Decision**: Use a three-tier testing approach: (1) exact value verification at known points, (2) property-based testing for mathematical invariants, and (3) statistical testing over random distributions.

**Rationale**: The spec's success criteria require both specific value checks (SC-001, SC-002, SC-004) and large-scale statistical validation (SC-001's 10,000+ random values, SC-006's 10,000+ wrap values).

**Test Categories**:

1. **Exact value tests**: polyBlep(0, 0.01) should return a specific non-zero value; polyBlep(0.5, 0.01) should return exactly 0.0f.

2. **Property tests**:
   - Zero outside correction region (SC-001)
   - Continuity: no jumps > dt across phase sweep (SC-002)
   - Symmetry: correction is symmetric around discontinuity (SC-003)
   - Zero DC bias: integral of correction over full phase = 0 (SC-003)
   - 4-point has lower peak second derivative than 2-point (SC-003)

3. **Statistical tests**:
   - Random phase/increment verification (SC-001)
   - Phase accumulator wrap count (SC-005)
   - wrapPhase range verification (SC-006)
   - subsamplePhaseWrapOffset accuracy (SC-007)

4. **Compatibility tests**:
   - PhaseAccumulator vs LFO phase trajectory (SC-009)
   - constexpr evaluation (SC-008)

**Testing Framework**: Catch2 (consistent with all existing tests).

---

## R9: Scaling Convention for PolyBLAMP

**Decision**: The polyBLAMP functions will return the raw polynomial correction value WITHOUT scaling by dt. The caller is responsible for scaling by the derivative discontinuity magnitude (which typically involves dt).

**Rationale**: This matches the convention used by Martin Finke's implementation and keeps the functions as pure mathematical primitives. In a triangle wave oscillator, the correction is: `output -= slope_change * dt * polyBlamp(t, dt)`, where `slope_change` is the magnitude of the derivative discontinuity. Baking the dt scaling into the function would couple it to a specific use case.

The existing `HardClipPolyBLAMP` also follows this pattern -- `applyCorrection` multiplies by `slopeChange` externally.

**Alternatives Considered**:
- Pre-multiplying by dt: Would simplify the calling code slightly but violates the "pure math" principle and makes the function less general.
