# Research: Spice/Dice & Humanize

**Feature**: 077-spice-dice-humanize | **Date**: 2026-02-23

## Research Tasks

### R1: Does Xorshift32 already have nextFloat()?

**Decision**: YES -- `nextFloat()` already exists in `dsp/include/krate/dsp/core/random.h` at lines 57-61.

**Rationale**: The spec's FR-014 explicitly says "This method MUST be added to Xorshift32 in `dsp/include/krate/dsp/core/random.h`." However, examining the actual code reveals that `nextFloat()` was added in a previous phase (likely Phase 8 for conditional trigs or earlier). The implementation returns `static_cast<float>(next()) * kToFloat * 2.0f - 1.0f` which yields bipolar [-1.0, 1.0] -- exactly what FR-014 requires.

**Alternatives Considered**: None. The existing implementation is correct and complete. No Layer 0 changes are needed. The implementation agent should verify this is present and mark FR-014 as pre-satisfied.

---

### R2: Overlay index capture -- before or after lane advance?

**Decision**: Capture overlay step indices BEFORE calling `advance()`.

**Rationale**: `ArpLane::advance()` (arp_lane.h line 99-104) returns the value at `position_` then increments `position_`. So after `advance()`, `currentStep()` returns the NEXT step (position already advanced). The spec's FR-008 says "velStep is the velocity lane's step index before advance" -- this confirms we must capture `currentStep()` before calling `advance()`.

**Alternatives Considered**:
1. Read `currentStep()` after advance and subtract 1 (mod length): Fragile and error-prone with polymetric wrapping.
2. Use a modified `advance()` that also returns the position: Would require changes to ArpLane (Layer 1), which is undesirable.

**Implementation**: Four `const size_t` variables captured right before the lane advance block:
```cpp
const size_t velStep = velocityLane_.currentStep();
const size_t gateStep = gateLane_.currentStep();
const size_t ratchetStep = ratchetLane_.currentStep();
const size_t condStep = conditionLane_.currentStep();
```

---

### R3: Humanize PRNG consumption on skipped steps -- insertion points

**Decision**: Add 3x `humanizeRng_.nextFloat()` discard calls at each early return point in fireStep().

**Rationale**: FR-023 requires the humanize PRNG to be consumed on every step regardless of whether it fires. This ensures the sequence position depends only on step count, not pattern content. There are exactly 5 early return points in fireStep() where PRNG consumption must be added:

1. **Euclidean rest path** (after `tieActive_ = false; ... return;` in the `!isHitStep` block)
2. **Condition fail path** (after `tieActive_ = false; ... return;` in the `!evaluateCondition()` block)
3. **Modifier Rest path** (after `currentArpNoteCount_ = 0; tieActive_ = false; ... return;`)
4. **Modifier Tie with preceding note** (after `cancelPendingNoteOffsForCurrentNotes(); tieActive_ = true; ... return;`)
5. **Modifier Tie without preceding note** (after `tieActive_ = false; ... return;`)

Plus the **defensive branch** (result.count == 0) per FR-041, which gets 3 discarded calls after all lane advances.

**Alternatives Considered**: Consuming PRNG only on fired steps (rejected: causes sequence shift when pattern gating changes).

---

### R4: Dice trigger edge detection in handleArpParamChange

**Decision**: Set `diceTrigger.store(true)` when normalized value >= 0.5 (i.e., the discrete parameter transitions to "1").

**Rationale**: The Dice trigger is a discrete 2-step parameter (stepCount=1). The host sends normalized 0.0 (idle) or 1.0 (trigger). When the host sends 1.0 (which maps to normalized >= 0.5), we set the atomic bool to true. The `applyParamsToEngine()` method then consumes it via `compare_exchange_strong`, ensuring exactly-once delivery. The host typically sends 0.0 again after the button release, but we do not need to handle the falling edge.

**Alternatives Considered**:
1. Track previous value for edge detection in handleArpParamChange: Adds unnecessary state; the atomic bool + compare_exchange already handles exactly-once semantics.
2. Plain load/store pattern: Has check-then-act race condition if a second rising edge arrives between load and store.

---

### R5: Spice blend for ratchet -- std::round() vs truncation

**Decision**: Use `std::round()` as specified in FR-008.

**Rationale**: `std::round()` is well-defined in C++ (always rounds half away from zero). For the ratchet Spice blend, the lerp result is a float that needs to map to an integer in [1,4]. With `std::round()`, the transitions happen at midpoints (e.g., 1.5 rounds to 2). With truncation, the transition from ratchet 3 to 4 would only happen at the very end of the Spice range, making the knob feel unresponsive at the upper end. The spec clarification (Q4) explicitly confirms `std::round()`.

**Alternatives Considered**: `static_cast<int>()` truncation (rejected: upper-range unresponsiveness).

---

### R6: Serialization order for backward compatibility

**Decision**: Serialize Spice and Humanize AFTER fillToggle (the last Phase 8 field).

**Rationale**: The existing serialization order is strictly append-only: each phase adds new fields after the previous phase's last field. Phase 8's last serialized field is `fillToggle` (a bool stored as int32). Spice (float) and Humanize (float) are appended after it. On load, if EOF is encountered at the first Spice read, it means we are loading a Phase 8 preset -- return true (success) with default Spice=0 and Humanize=0. This maintains the established EOF-safe backward compatibility pattern.

**Alternatives Considered**: None. The append-only pattern is well-established across Phases 4-8.

---

### R7: Condition lane overlay -- threshold vs probabilistic blend

**Decision**: Threshold blend at 50% Spice (spec FR-008).

**Rationale**: TrigCondition is a discrete enum with 18 values. Linear interpolation between two condition values is meaningless (what is "halfway between Prob50 and Ratio_1_2"?). The threshold approach provides clear, predictable behavior: below 50% Spice, the original condition is used; at 50% or above, the overlay condition is used. This keeps Spice behavior consistent across all lanes -- the user understands that increasing Spice gradually transitions from original to random.

**Alternatives Considered**:
1. Probabilistic blend (Spice = probability of using overlay per step): Makes Spice behavior inconsistent across lanes (continuous for velocity/gate, probabilistic for condition).
2. Nearest-enum interpolation: No meaningful ordering exists between condition types.

---

### R8: Humanize timing offset clamping strategy

**Decision**: Post-clamp the final result (FR-015).

**Rationale**: The timing offset is `sampleOffset + timingOffsetSamples`, which is then clamped to `[0, blockSize - 1]`. This means notes near block boundaries have compressed effective ranges. The spec clarification (Q3) explicitly confirms this is an accepted simplification: it only affects notes within 20ms of a block boundary, and at typical block sizes (64-512 samples), the effect is musically indistinguishable.

**Alternatives Considered**: Pre-clamping the raw offset (rejected: breaks symmetric distribution for ALL notes, not just boundary ones).

---

### R9: Humanize interaction with ratcheted steps

**Decision**: Timing offset applies to first sub-step only; velocity offset to first sub-step only; gate offset to all sub-steps (FR-019, FR-020, FR-021).

**Rationale**: Ratcheting is an intentional rhythmic effect (machine-gun retriggering). Humanizing each individual sub-step would destroy the precision that makes ratcheting musically distinctive. The timing offset shifts when the burst starts (organic feel for step onset), velocity offset affects the initial attack (sub-steps already have reduced pre-accent velocities per Phase 6), and gate offset applies uniformly to all sub-steps (affects overall articulation without breaking the subdivision rhythm).

**Alternatives Considered**: Per-sub-step humanization (rejected: destroys ratchet precision and requires N additional PRNG calls per step).
