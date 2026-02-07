# Research: Multi-Stage Envelope Generator

**Date**: 2026-02-07 | **Spec**: specs/033-multi-stage-envelope/spec.md

---

## R-001: Coefficient Calculation Extraction Strategy

**Decision**: Extract `calcCoefficients()`, `StageCoefficients`, constants (`kEnvelopeIdleThreshold`, `kMinEnvelopeTimeMs`, `kMaxEnvelopeTimeMs`, `kSustainSmoothTimeMs`, `kDefaultTargetRatioA`, `kDefaultTargetRatioDR`, `kLinearTargetRatio`), `EnvCurve`, and `RetriggerMode` into a new shared header `dsp/include/krate/dsp/primitives/envelope_utils.h` at Layer 1.

**Rationale**: Both `ADSREnvelope` (Layer 1) and `MultiStageEnvelope` (Layer 2) need the same coefficient calculation formula, constants, and enumerations. Duplicating them would violate DRY and risk divergence. The utilities are pure stateless functions that belong at Layer 1 (primitives) since they depend only on Layer 0 (`db_utils.h` for `constexprExp`). Placing at Layer 0 was considered but rejected because these are envelope-specific domain concepts, not general math utilities.

**Alternatives considered**:
1. **Include `adsr_envelope.h` directly from MultiStageEnvelope**: Rejected because it would pull in the entire `ADSREnvelope` class, creating unnecessary coupling. Layer 2 depending on a specific Layer 1 class rather than shared utilities is a code smell.
2. **Duplicate the code**: Rejected because the EarLevel Engineering formula and target ratio constants must remain in sync between envelope types.
3. **Move to Layer 0 (`core/envelope_math.h`)**: Rejected because these are envelope-domain concepts with audio-specific semantics (target ratios, curve shapes), not general math.

**Migration plan**:
- Create `envelope_utils.h` with the shared types and functions.
- Modify `adsr_envelope.h` to `#include <krate/dsp/primitives/envelope_utils.h>` and remove the moved definitions. The `ADSREnvelope` class itself remains unchanged -- only the location of its dependencies moves.
- All existing tests for `ADSREnvelope` must pass without modification after the refactor.

---

## R-002: Multi-Stage Envelope Curve Implementation Approach

**Decision**: Use the same dual-approach as ADSREnvelope: one-pole iterative method for Exponential and Linear curves, and quadratic phase mapping for Logarithmic curves. However, adapt the approach for arbitrary start/end levels per stage (not just 0->peak or peak->sustain).

**Rationale**: The spec requires each stage to transition from any level to any level (not just the fixed ADSR start/end points). The one-pole method (`output = base + output * coef`) naturally handles any starting level because `output` is initialized to the current level. However, the coefficient calculation in `calcCoefficients()` computes the base assuming a fixed target. For MultiStageEnvelope, the stage's "from" level changes dynamically (it is the output at the moment the stage begins).

**Implementation approach**:
- For **Exponential/Linear**: Calculate `coef` and `base` using `calcCoefficients()` with the stage's target level and direction. At stage entry, record the starting level. The one-pole formula will naturally approach the target from whatever the current output is.
- For **Logarithmic**: Use phase-based quadratic mapping: `output = startLevel + (targetLevel - startLevel) * curvedPhase`. The phase increments linearly from 0 to 1 over the stage duration. For rising stages, `curvedPhase = phase * phase` (convex). For falling stages, `curvedPhase = 1 - (1 - phase)^2` (concave). This is already proven in ADSREnvelope.
- **Stage completion**: All stages complete after the configured time duration in samples (FR-021). The final sample snaps to the exact target level. This prevents drift across stages.

**Key difference from ADSREnvelope**: ADSR uses level-based completion detection (`output >= peakLevel` for attack, `output <= sustainTarget` for decay). MultiStageEnvelope uses **time-based** completion (sample counter reaches the configured duration). This is simpler and more deterministic, matching the spec's clarification that "stage completes after the configured time duration in samples."

**Alternatives considered**:
1. **Phase-based approach for all curves (like MultiStageEnvelopeFilter's `applyCurve()`)**: The filter uses `pow(t, exponent)` for curve shaping. This produces correct shapes but is more expensive (one `pow` per sample). The one-pole method uses only 1 multiply + 1 add. Rejected for per-sample cost.
2. **Always use phase-based (no one-pole)**: Simpler code but loses the natural exponential approach that sounds like analog RC circuits. The one-pole method is the standard for synth envelopes.

---

## R-003: Time-Based vs Level-Based Stage Completion

**Decision**: Use a sample counter for stage completion. Each stage tracks `samplesRemaining_` (or `sampleCount_` with `totalSamples_`). When the counter reaches the total, the stage completes and the output snaps to the target level.

**Rationale**: Per the spec clarification: "Stage completes after the configured time duration in samples, regardless of whether the output has exactly reached the target." This is critical for:
- Deterministic timing (FR-021)
- Predictable looping (no drift at loop boundaries -- SC-005)
- Sample-accurate stage boundaries (SC-001)

**Implementation**:
```
stageTimeSamples = roundToInt(timeMs * 0.001f * sampleRate)
// Minimum 1 sample for instant transitions (0ms stages)
if (stageTimeSamples < 1) stageTimeSamples = 1;
```

**For the one-pole Exponential/Linear curves**: The coefficients are still calculated from the time parameter, so the exponential curve will approach the target within the configured time. The snap at the end just ensures exactness. The curve will be very close to the target by the time the counter expires (within a few LSBs).

---

## R-004: Sustain Smoothing Implementation

**Decision**: Use an inline one-pole smoother coefficient (same as ADSREnvelope line 104: `sustainSmoothCoef_ = exp(-5000.0f / (kSustainSmoothTimeMs * sampleRate_))`). Apply it every sample while in the Sustaining state: `output_ = target + coef * (output_ - target)`.

**Rationale**: The spec (FR-032) says: "Use a separate one-pole smoother (matching ADSREnvelope's approach) that updates every sample during the sustain hold state, independent of the stage mechanism." This matches exactly what ADSREnvelope does at line 229. Using the inline coefficient rather than a `OnePoleSmoother` instance avoids an extra object and is simpler.

---

## R-005: Loop Behavior Design

**Decision**: When the envelope reaches the end of the loop end stage, it jumps back to the loop start stage. The "from" level for the loop start stage becomes the current output level (which is the loop end stage's target). Sustain hold is bypassed when looping is enabled (FR-026).

**Rationale**: This matches the MultiStageEnvelopeFilter's existing loop behavior (line 593-596), which already sets `stageFromFreq_ = currentCutoff_` when looping back. The same pattern applies here but with output levels instead of cutoff frequencies.

**Edge cases resolved**:
- **Loop start == loop end**: Single-stage oscillation. The stage re-enters from its own target level, transitioning to... its own target level. If the "from" level differs (e.g., first iteration starts from a previous stage's level), it creates a decaying oscillation that converges to the target. After one iteration, from == to, so it holds.
- **Sustain inside loop region**: Sustain hold is bypassed per FR-026. The envelope loops continuously.
- **Gate-off during loop**: Immediate release from current output (FR-027). No stage completion.

---

## R-006: State Machine Design

**Decision**: Four states matching the spec (FR-004): `Idle`, `Running`, `Sustaining`, `Releasing`. Named as `MultiStageEnvState` to avoid ODR conflict with `EnvelopeState` in `multistage_env_filter.h`.

**State transitions**:
```
Idle -> Running           (gate on)
Running -> Sustaining     (reached sustain point, gate on, loop disabled)
Running -> Running        (loop wrap-around)
Running -> Releasing      (gate off during any running stage)
Sustaining -> Releasing   (gate off while sustaining)
Releasing -> Idle         (output < kEnvelopeIdleThreshold)
Any -> Running            (gate on, hard retrigger)
Any -> Idle               (reset())
```

**Rationale**: `EnvelopeState` already exists in `multistage_env_filter.h` with values `Idle, Running, Releasing, Complete`. Using a different name (`MultiStageEnvState`) avoids ambiguity and ODR issues. The states differ: MultiStageEnvelope has `Sustaining` (hold at sustain point) instead of `Complete` (envelope finished all stages).

---

## R-007: Real-Time Parameter Change Strategy

**Decision**: Parameters are stored directly (no message queues). Stage parameters (time, level, curve) take effect at the next stage entry. Exceptions:
- **Current stage time change (FR-031)**: Recalculate rate based on remaining time. `newRate = (targetLevel - currentOutput) / remainingSamples`. This requires tracking `samplesRemaining_` and recalculating the coefficients.
- **Sustain level change (FR-032)**: The one-pole smoother handles this automatically -- just update the target.
- **Loop boundary changes (FR-030)**: Take effect on next loop iteration.

**Rationale**: The spec requires no restarts and no discontinuities. Deferred application (take effect at next stage entry) is the simplest approach and covers most cases. The mid-stage time recalculation is the only complex case, but it is tractable because we have the sample counter.

---

## R-008: Release Phase Implementation

**Decision**: The release phase uses a single exponential curve (matching ADSREnvelope), transitioning from the current output level to 0.0 over the configured release time. Use the same one-pole method as ADSREnvelope's release. The release time is a "constant-rate" release: configured for a full 1.0->0.0 transition; releasing from a lower level takes proportionally less time.

**Rationale**: Per the spec clarification: "The release phase always uses an exponential curve." This simplifies the release to a single coefficient calculation, reusing the same `calcCoefficients()` function. The idle threshold check (`< kEnvelopeIdleThreshold`) triggers the transition to Idle.

**Special case (0ms release time)**: Snap to 0.0 immediately (within 1 sample). Use `minTimeSamples = 1`.

---

## R-009: SIMD Viability Assessment

**Decision**: NOT BENEFICIAL for SIMD optimization.

**Rationale**:
- **Feedback loop**: The one-pole formula `output = base + output * coef` is inherently serial -- each sample depends on the previous sample's output.
- **Branch density**: Stage transition checks, state machine switches, and loop boundary checks create moderate branching.
- **Data parallelism width**: A single envelope instance processes one sample at a time. Multi-voice SIMD (processing 4 envelopes in parallel via SSE) is a system-level optimization for the voice allocator, not the envelope itself.
- **Budget**: The per-sample operation is 1 multiply + 1 add (exponential/linear) or 2 multiplies + 1 add (logarithmic) plus a comparison + potential branch for stage transition. This is extremely cheap, well under the 0.05% CPU target.

**Alternative optimizations**: None needed. The algorithm is already minimal. Early-out for Idle state is the only optimization worth implementing (skip processing entirely when inactive).
