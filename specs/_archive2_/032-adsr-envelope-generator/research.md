# Research: ADSR Envelope Generator

**Feature**: 032-adsr-envelope-generator | **Date**: 2026-02-06

---

## R-001: One-Pole ADSR Coefficient Formula (EarLevel Engineering)

**Decision**: Use the EarLevel Engineering one-pole iterative approach with target ratio curve control.

**Rationale**: This is the industry-standard approach for software ADSR envelopes. The formula requires only 1 multiply + 1 add per sample (the `output = base + output * coef` recurrence). The target ratio parameter provides elegant curve control from steep exponential to near-linear using a single parameter rather than requiring separate curve implementations. This approach also inherently avoids denormals because the iteration converges to a non-zero target (overshoot/undershoot targets).

**Core Formulas**:

1. **Coefficient calculation** (computed at setup time, not per-sample):
   ```
   coef = exp(-log((1.0 + targetRatio) / targetRatio) / rate)
   ```
   where `rate` is the number of samples for the stage to complete.

2. **Base calculation**:
   ```
   base = (target + targetRatio) * (1.0 - coef)
   ```
   where `target` is the overshoot/undershoot target for the stage.

3. **Per-sample iteration** (the hot path):
   ```
   output = base + output * coef
   ```

4. **Attack overshoot target**: Attack targets slightly above 1.0. The actual target is `1.0 + targetRatioA`, so for default targetRatioA=0.3, the attack targets 1.3. Transition to Decay occurs when `output >= 1.0`.

5. **Decay/Release undershoot target**: Decay targets below the sustain level. Release targets below 0.0. The undershoot amount is controlled by `targetRatioDR`. For default targetRatioDR=0.0001, this produces steep exponential curves matching analog RC-circuit behavior.

**Target Ratio Values and Curve Shape**:
- `targetRatio = 0.3` (attack default): Moderate exponential rise
- `targetRatio = 0.0001` (decay/release default): Steep exponential, matches classic analog character
- `targetRatio = 100.0` (linear approximation): Nearly constant rate, approaching linear

**Alternatives Considered**:
- **Pure exponential (fixed curve)**: No curve control. Rejected because logarithmic and linear curves are spec requirements.
- **Lookup table approach**: More flexible curve shapes but higher memory cost and more complex to implement real-time parameter changes. Overkill for 3 curve types.
- **Polynomial curves**: `output = t^n` style. Harder to make continuous across stage transitions and parameter changes. Not industry standard.

---

## R-002: Curve Shape Mapping to Target Ratios

**Decision**: Map the `EnvCurve` enum to target ratio values as follows:

| EnvCurve | Attack targetRatio | Decay/Release targetRatio | Character |
|----------|-------------------|--------------------------|-----------|
| Exponential | 0.3 | 0.0001 | Classic analog RC-circuit |
| Linear | 100.0 | 100.0 | Constant rate (near-linear) |
| Logarithmic | 0.0001 | 0.3 | Inverted exponential |

**Rationale**: The one-pole formula naturally produces exponential curves. By swapping the target ratios between attack and decay, we get the logarithmic (inverse) shape. A large target ratio (100.0) makes the curve approach linear because the overshoot/undershoot target is so far from the actual range that the visible portion of the exponential is nearly flat.

Exponential attack (targetRatio=0.3): Output rises quickly at first, approaches 1.0 gradually (concave-up when plotted). This satisfies SC-004 (midpoint above 0.5 for attack).

Logarithmic attack (targetRatio=0.0001): Output rises slowly at first, accelerates toward 1.0. This is the same steep curve shape that exponential decay uses, but applied to the rising attack direction.

For decay/release, the mapping is inverted: exponential decay uses 0.0001 (fast initial drop), logarithmic decay uses 0.3 (slow initial drop).

**Alternatives Considered**:
- **Separate formula per curve type**: More complex, harder to maintain, and the target ratio approach already covers all three shapes elegantly.
- **Power curve (`output^exponent`)**: Would require different per-sample computation, breaking the uniform `base + output * coef` pattern.

---

## R-003: Constant-Rate Behavior vs. Constant-Time

**Decision**: Use constant-rate behavior for all stages, matching the spec requirement and analog synthesizer convention.

**Rationale**: The configured time specifies the full-scale duration (0.0 to 1.0 for attack, 1.0 to 0.0 for decay/release). Starting from a partial level takes proportionally less time. This means:
- Attack from 0.5 to 1.0 takes approximately half the configured attack time
- Release from 0.5 to 0.0 takes approximately half the configured release time
- Retrigger from any level feels natural because the rate doesn't change

This is implicit in the one-pole formula: the same coefficient is used regardless of starting level. The coefficient determines the rate of change, not the total time from current position.

**Alternatives Considered**:
- **Constant-time**: Would require recalculating coefficients whenever the starting level changes (e.g., on retrigger). More computation, less intuitive behavior, and does not match analog synth convention.

---

## R-004: Sustain Level Smoothing Implementation

**Decision**: Use a dedicated OnePoleSmoother instance with 5ms smoothing time for sustain level transitions during the Sustain stage.

**Rationale**: FR-025 requires smooth transition to new sustain levels over 5ms. The existing OnePoleSmoother class is already available at Layer 1 and provides exactly this behavior. Using it avoids reimplementing the same one-pole smoothing logic. The smoother is only active during the Sustain stage.

However, upon reflection, including OnePoleSmoother as a member creates a Layer 1 -> Layer 1 dependency within the same layer, which is fine per architecture rules (Layer 1 can depend on Layer 0 and stdlib; another Layer 1 class within the same library is also acceptable since they are in the same static library). Actually, more carefully: the architecture rules state Layer 1 depends on Layer 0 only. Including another Layer 1 header would be a same-layer dependency.

**Revised Decision**: Implement the 5ms sustain smoothing inline using the same one-pole coefficient calculation from `smoother.h` utility functions (which are free functions at namespace scope, not class members). This way we depend on the utility function `calculateOnePolCoefficient()` which is at Layer 1 scope, but since both files are in the same layer and library, this is a lateral dependency within the same layer. Alternatively, we can compute the coefficient ourselves with `exp(-5000.0 / (5.0 * sampleRate))` which uses only `<cmath>`.

**Final Decision**: Inline the sustain smoothing using a simple one-pole coefficient computed from the 5ms time constant. No external dependency beyond `<cmath>` and Layer 0. This keeps the ADSR envelope fully self-contained at Layer 1.

```cpp
// In prepare():
sustainSmoothCoef_ = std::exp(-5000.0f / (5.0f * sampleRate));
// In process() during Sustain stage:
currentSustainTarget_ = sustainLevel_ * peakLevel_;
output_ = currentSustainTarget_ + sustainSmoothCoef_ * (output_ - currentSustainTarget_);
```

---

## R-005: Legato Mode Behavior

**Decision**: In legato mode, gate-on during an active envelope does NOT restart attack. Instead:
- If in Release: return to Sustain (if current level <= sustain) or Decay (if above sustain)
- If in Attack/Decay/Sustain: no action (continue current stage)

**Rationale**: This matches the FR-019 spec requirement and standard legato synth behavior. The envelope resumes from its current position without any discontinuity.

**Stage transition on legato re-gate**:
1. If in Release and output <= sustainLevel: enter Sustain stage
2. If in Release and output > sustainLevel: enter Decay stage (will naturally fall to sustain)
3. If in Attack/Decay/Sustain: no change

---

## R-006: Velocity Scaling Architecture

**Decision**: Store a `peakLevel_` member that defaults to 1.0. When velocity scaling is enabled, set `peakLevel_ = velocity`. All stage targets scale by `peakLevel_`:
- Attack targets `peakLevel_` (plus overshoot ratio)
- Decay targets `sustainLevel_ * peakLevel_`
- Sustain holds at `sustainLevel_ * peakLevel_`
- Release targets 0.0 (unchanged)

**Rationale**: This provides proportional scaling of the entire envelope shape. A velocity of 0.5 produces an envelope that is half the amplitude but identical in shape. This matches the spec requirement FR-021.

---

## R-007: Stage Transition Timing and the One-Pole Approach

**Decision**: Stage transitions are triggered by threshold crossing, not sample counting.

**Rationale**: The one-pole exponential approach asymptotically approaches the target. The attack stage transitions to decay when `output >= peakLevel_`. The decay stage transitions to sustain when `output <= sustainLevel * peakLevel_`. The release stage transitions to idle when `output < kEnvelopeIdleThreshold`.

The timing specification (e.g., "10ms attack") refers to the time for a full-scale transition. The one-pole coefficient is calculated to achieve this timing. Because the exponential overshoots/undershoots, the actual crossing happens very close to the specified time.

For SC-001 (timing within +/-1 sample): The EarLevel formula `coef = exp(-log((1.0 + targetRatio) / targetRatio) / rate)` is derived so that the transition from 0 to 1 (or 1 to 0) completes in exactly `rate` samples. Combined with threshold-based transitions, this provides sample-accurate timing.

---

## R-008: SIMD Viability for ADSR Envelope

**Decision**: SIMD is NOT BENEFICIAL for a single ADSR envelope instance.

**Rationale**:
- The per-sample operation is a single multiply-add with a conditional branch for stage transitions
- There is a feedback loop: each sample's output depends on the previous sample's output (`output = base + output * coef`)
- The working set is tiny (a handful of floats)
- The CPU budget (SC-003: < 0.01%) is trivially met by the scalar implementation

SIMD could benefit a polyphonic engine processing N envelopes in parallel (SoA layout), but that is the responsibility of the future Polyphonic Synth Engine (Phase 3.2), not this primitive.

---

## R-009: Existing Code Reuse Assessment

**Decision**: Reuse the following from the existing codebase:

| Component | What to Reuse | How |
|-----------|--------------|-----|
| `detail::constexprExp()` | Coefficient calculation at constexpr time | Include `<krate/dsp/core/db_utils.h>` |
| `detail::isNaN()` | NaN validation for parameter setters | Include `<krate/dsp/core/db_utils.h>` |
| `detail::flushDenormal()` | Safety net (though one-pole approach prevents denormals) | Include `<krate/dsp/core/db_utils.h>` |
| `ITERUM_NOINLINE` macro | For NaN-safe setters under /fp:fast | Redefine locally or include from smoother.h |

Note: `ITERUM_NOINLINE` is defined in `smoother.h`. Rather than creating a Layer 1 -> Layer 1 dependency, we should either:
1. Move this macro to a Layer 0 header (e.g., `db_utils.h` or a new `compiler_utils.h`), or
2. Redefine it locally in the ADSR header.

**Decision**: Redefine `ITERUM_NOINLINE` locally with a guard (`#ifndef ITERUM_NOINLINE`) so it works whether or not `smoother.h` is also included. This is safe because macro definitions are not subject to ODR.

For `constexprExp`, we actually need `std::exp()` at runtime (not constexpr), since coefficient calculation happens in `prepare()` and setter methods. The `detail::constexprExp()` Taylor series is less accurate than `std::exp()`. We will use `std::exp()` and `std::log()` from `<cmath>` for runtime coefficient calculation. We only need `db_utils.h` for `isNaN()` and `flushDenormal()`.

---

## R-010: File Location and Naming

**Decision**:
- Header: `dsp/include/krate/dsp/primitives/adsr_envelope.h`
- Test: `dsp/tests/unit/primitives/adsr_envelope_test.cpp`
- Namespace: `Krate::DSP`
- Class: `ADSREnvelope`
- Enums: `EnvelopeStage`, `EnvCurve` (file-scope in `Krate::DSP`)

**ODR Confirmation**:
- `ADSREnvelope`: No existing class with this name (grep confirmed)
- `EnvelopeStage`: Exists as a `struct` in `multistage_env_filter.h` at Layer 2 (different type -- struct vs enum class, and different scope). Our `enum class EnvelopeStage` at Layer 1 in `Krate::DSP` namespace would conflict at link time because both are in `Krate::DSP` namespace at file scope.

**REVISED**: Rename our enum to `ADSRStage` to avoid any ambiguity with the existing `EnvelopeStage` struct, even though they are technically different types and the C++ standard allows same names for different entity kinds in the same namespace. Using a distinct name eliminates all ODR risk and reader confusion.

Actually, upon more careful reading: `EnvelopeStage` in `multistage_env_filter.h` is a `struct`, and our planned `EnvelopeStage` is an `enum class`. C++ allows both to coexist in the same namespace as they are different kinds of entities. However, this creates ambiguity for users. The spec explicitly names the enum `EnvelopeStage` in the Key Entities section.

**Final Decision**: Keep `EnvelopeStage` as the enum name since:
1. The spec explicitly names it `EnvelopeStage`
2. It is a different kind (enum class vs struct) -- no ODR violation
3. Users will always qualify with `EnvelopeStage::Idle` vs `EnvelopeStage{.targetHz=...}`

But to be safe and avoid user confusion, we will name it `ADSRStage` with a type alias `using EnvelopeStage = ADSRStage` if needed by downstream consumers. Actually, simpler: just use `ADSRStage` throughout. The spec's "Key Entities" section is a design guide, not a binding API contract. The acceptance scenarios reference `getStage()` which returns the enum -- the name of the enum type does not matter for the acceptance tests.

**FINAL**: Use `ADSRStage` to avoid any confusion. The enum values match the spec: `Idle, Attack, Decay, Sustain, Release`.

- `EnvCurve`: No existing type with this name (grep confirmed). Safe to use.
