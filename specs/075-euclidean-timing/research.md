# Research: Euclidean Timing Mode

**Feature**: 075-euclidean-timing | **Date**: 2026-02-22

## Research Tasks

### 1. EuclideanPattern API and Behavior

**Decision**: Reuse existing `EuclideanPattern` class at `dsp/include/krate/dsp/core/euclidean_pattern.h` (Layer 0).

**Rationale**: The class is already implemented, tested (15+ unit tests), and used in production by the TranceGate processor. It provides exactly the three methods needed:
- `generate(pulses, steps, rotation)` -> `uint32_t` bitmask
- `isHit(pattern, position, steps)` -> `bool` (O(1) bit check)
- `countHits(pattern)` -> `int` (for validation)

The class is constexpr, static, noexcept, and allocation-free -- ideal for real-time audio use.

**Alternatives considered**:
- *Implement a new Bjorklund algorithm*: Rejected. The existing Bresenham-style accumulator is functionally equivalent and already verified.
- *Use a runtime-computed std::bitset*: Rejected. `uint32_t` bitmask is simpler, faster, and sufficient for the 2-32 step range.

### 2. Euclidean Integration Approach: Pre-fire Gating Check vs. Dedicated ArpLane

**Decision**: Pre-fire gating check in `fireStep()`, NOT a dedicated `ArpLane<bool>`.

**Rationale**:
- A pre-fire check is a single `isHit()` call (O(1) bit test) per step -- minimal overhead.
- The Euclidean pattern is cached as a bitmask and only regenerated when parameters change, not per-step.
- A dedicated ArpLane would require populating a 32-element array every time the pattern changes, adding unnecessary complexity and memory.
- The Euclidean concept is architecturally distinct from per-step manual programming (lanes). Conflating them would blur the abstraction boundary.
- The TranceGate already uses a similar pattern: generate bitmask, then query per-step.

**Alternatives considered**:
- *ArpLane<bool> auto-populated from Euclidean pattern*: Rejected. The lane would need to be resized and repopulated on every parameter change, and its length would need to match `euclideanSteps_` -- adding complexity to the lane management code. The modifier lane Rest mechanism handles gating differently (per-step manual flags), so reusing it for Euclidean would conflate two distinct concerns.
- *Modifier lane integration (inject Rest flags)*: Rejected. This would make Euclidean timing dependent on modifier lane length and lose the ability to have independent cycling. The spec explicitly requires independent lane lengths.

### 3. Evaluation Order: Euclidean Before Modifiers

**Decision**: Euclidean gating is evaluated BEFORE modifier evaluation. Order: Euclidean -> Modifier (Rest > Tie > Slide > Accent) -> Ratcheting.

**Rationale**:
- Evaluating the cheap bit-check first provides an early-out optimization (most computation in modifier/ratchet paths is skipped for Euclidean rests).
- Modifier evaluation has side effects (tie chain management, accent velocity boosting) that should NOT execute on Euclidean rest steps.
- Semantically, Euclidean timing defines the rhythmic structure (which steps exist), while modifiers refine individual steps within that structure.
- This matches the spec's explicit requirement in FR-006.

**Alternatives considered**:
- *Modifiers before Euclidean*: Rejected. Modifier tie/slide chain management would incorrectly execute on rest steps, potentially causing stuck notes or incorrect state.

### 4. Parameter Value Ranges and Normalization

**Decision**: Use the following parameter registrations:

| Parameter | Range | Default | stepCount | Normalization |
|-----------|-------|---------|-----------|---------------|
| Enabled | 0-1 | 0 (off) | 1 | Toggle (value >= 0.5) |
| Hits | 0-32 | 4 | 32 | `round(value * 32)` |
| Steps | 2-32 | 8 | 30 | `2 + round(value * 30)` |
| Rotation | 0-31 | 0 | 31 | `round(value * 31)` |

**Rationale**: These use `RangeParameter` for discrete integer values, following the same pattern as other arp parameters (velocity lane length, ratchet lane steps, etc.). The Hits minimum is 0 (not 1), per the clarified spec (Q2), allowing a fully silent pattern as a valid configuration.

**Alternatives considered**:
- *Hits range 1-32 (from initial roadmap)*: Rejected. The spec clarification explicitly sets hits minimum to 0. A fully silent pattern (hits=0) is a valid, intentional configuration.

### 5. Serialization Backward Compatibility

**Decision**: Serialize Euclidean data AFTER ratchet lane data. Use EOF-safe pattern for backward compatibility.

**Rationale**: This follows the exact same pattern established by Phases 4, 5, and 6:
- First Euclidean field (enabled) at EOF = Phase 6 backward compat (return true, keep defaults)
- EOF after enabled but before remaining fields = corrupt stream (return false)
- Out-of-range values clamped silently

This ensures presets saved in Phase 6 or earlier load correctly with Euclidean defaulting to disabled.

### 6. Setter Call Order

**Decision**: Required order in `applyParamsToArp()`: `setEuclideanSteps()` -> `setEuclideanHits()` -> `setEuclideanRotation()` -> `setEuclideanEnabled()`.

**Rationale**: Steps must be set first because each setter regenerates the pattern bitmask. If hits is set before steps, the intermediate pattern would be generated with the OLD step count, which could produce an incorrect intermediate clamping. Enabled is set last so the flag activates only after the pattern is fully computed from all three updated parameters. This prevents a brief window where enabled=true but the pattern reflects stale parameter values.

### 7. Tie Chain Breaking on Euclidean Rest (FR-007)

**Decision**: When a Euclidean rest step is reached and a tie chain is active, emit noteOff for all sustained notes and set `tieActive_ = false`.

**Rationale**: This is the same behavior as the existing modifier Rest path but triggered by the Euclidean pattern. Without this, a tie chain started by a preceding hit step would leave notes sounding through rest steps, creating stuck notes. The look-ahead mechanism (which checks the modifier lane only, per Q3) might suppress a gate noteOff on the step preceding a rest, so FR-007 is the correction point that ensures no stuck notes.

### 8. Ratchet Interaction (FR-016, FR-017)

**Decision**: Euclidean rest suppresses ratcheting entirely. No ratcheted sub-steps fire on rest steps.

**Rationale**: The evaluation order (Euclidean -> Modifier -> Ratchet) means the Euclidean rest early-returns from fireStep() before any ratchet logic executes. The ratchet count from the ratchet lane is still consumed (the lane advances), but the value is discarded. This is consistent with how modifier Rest suppresses ratcheting.

### 9. In-Flight Ratchet Sub-Steps on Mode Toggle (Q4)

**Decision**: When Euclidean mode is toggled on, in-flight ratchet sub-steps complete normally. Euclidean gating takes effect from the next full step boundary.

**Rationale**: `setEuclideanEnabled(true)` resets `euclideanPosition_` to 0 but does NOT clear `ratchetSubStepsRemaining_` or other ratchet state. The processBlock() SubStep handler continues firing remaining sub-steps for the current step. The Euclidean gating only applies when fireStep() is called for the next step.

## All NEEDS CLARIFICATION Resolved

All unknowns from the spec were resolved during the clarification session (2026-02-22). No outstanding questions remain:

1. NoteSelector advance on Euclidean rest steps -> Yes, always advances (Q1)
2. Hits minimum value -> 0, not 1 (Q2)
3. Ratchet look-ahead and Euclidean -> Look-ahead checks modifier only, not Euclidean (Q3)
4. In-flight ratchet sub-steps on toggle -> Complete normally (Q4)
5. Setter call order -> steps, hits, rotation, enabled (Q5)
