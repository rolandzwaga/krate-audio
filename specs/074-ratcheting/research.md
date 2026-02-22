# Research: Ratcheting (074)

**Date**: 2026-02-22 | **Status**: Complete

## Research Questions

### Q1: How do hardware sequencers implement ratcheting timing?

**Decision**: Integer division with last-sub-step remainder absorption.

**Rationale**: Sub-step k fires at `k * (stepDuration / N)` samples into the step (integer division). The last sub-step's duration is `subStepDuration + (stepDuration % N)`. This guarantees zero timing drift (sub-step durations sum exactly to stepDuration) and keeps onset arithmetic simple. This matches the Elektron Digitakt approach where ratcheted sub-divisions are evenly spaced within the step grid.

**Alternatives considered**:
- Floating-point division with rounding: Risk of cumulative drift over many steps.
- First-sub-step remainder: Less intuitive; the last note being shorter feels wrong musically.
- Distribute remainder across all sub-steps: Over-complicated for a 1-sample difference maximum (ratchet max is 4).

### Q2: Should ratchet sub-steps be emitted all at once in fireStep() or tracked across blocks?

**Decision**: Track sub-step state in processBlock() jump-ahead loop (Approach 2 from spec).

**Rationale**: The processBlock() jump-ahead loop already handles multiple event types (step boundaries, pending noteOffs, bar boundaries). Adding SubStep as a new event type integrates naturally. This correctly handles sub-steps that span block boundaries -- a block might be 64 samples, but at slow tempos with ratchet 4, a sub-step could be 2000+ samples.

**Alternatives considered**:
- Emit all sub-steps in fireStep(): Simple but breaks when sub-steps cross block boundaries. Would require a different block processing model.

### Q3: How should ratchet interact with existing modifier flags?

**Decision**: Modifiers are evaluated first (Rest > Tie > Slide > Accent), then ratcheting applies if the step emits a noteOn.

**Rationale**: The modifier evaluation priority chain is already established in Phase 5 and must not be disrupted. Rest and Tie suppress the step entirely (no ratcheting). Slide and Accent apply to the first sub-step only:
- Slide: First sub-step is legato (transition from previous note). Subsequent sub-steps are normal retriggers.
- Accent: First sub-step gets velocity boost. Subsequent sub-steps use un-accented velocity. This mirrors real drum roll dynamics.

**Alternatives considered**:
- Accent on all sub-steps: Less musical -- uniform velocity across a roll sounds mechanical.
- Slide on all sub-steps: Nonsensical -- slide is a transition from the previous note, not between sub-steps.

### Q4: How should the "next step is Tie/Slide" look-ahead work with ratcheted steps?

**Decision**: Look-ahead applies only to the LAST sub-step of a ratcheted step.

**Rationale**: Sub-steps 0 through N-2 are internal to the current step. The "next step" modifier only affects the boundary between the current step and the next step, which is the last sub-step's noteOff. Internal sub-steps always schedule their gate-based noteOffs normally.

**Alternatives considered**: None -- this is the only logical interpretation.

### Q5: What event priority should SubStep have relative to existing events?

**Decision**: `BarBoundary > NoteOff > Step > SubStep` (SubStep has lowest priority).

**Rationale**: When BarBoundary and SubStep coincide, the bar boundary must reset all state (including sub-step state) -- the sub-step is discarded. When NoteOff and SubStep coincide, NoteOff fires first (consistent with FR-021 ordering: NoteOff before NoteOn at same offset). When Step and SubStep coincide, Step takes priority because a new step beginning supersedes any remaining sub-steps from the previous step (though this case should not arise in practice since sub-steps end exactly at step boundaries).

**Alternatives considered**: None -- the priority ordering follows from the existing event priority logic.

### Q6: Is the existing ArpLane<uint8_t> template suitable for ratchet counts?

**Decision**: Yes, reuse directly with no changes to ArpLane.

**Rationale**: ArpLane<uint8_t> is already used for the modifier lane. The ratchet lane needs the same operations: setLength, setStep, getStep, advance, reset. The only consideration is that ArpLane zero-initializes to `T{}` which is 0 for uint8_t -- ratchet count 0 is invalid. The constructor must explicitly set step 0 to 1, following the same pattern as the modifier lane (which sets step 0 to `kStepActive`).

**Alternatives considered**: None -- the ArpLane template was designed for exactly this use case.

### Q7: Does kMaxEvents need to increase, and by how much?

**Decision**: Increase kMaxEvents from 64 to 128.

**Rationale**: Worst case for ratcheted Chord mode: 4 sub-steps x 16 held notes x 2 events (noteOn + noteOff) = 128 events. The processor-side buffer (`arpEvents_` in processor.h) is already `std::array<..., 128>`, so only the DSP-side constant needs updating.

**Alternatives considered**:
- Keep at 64: Risk of silent event truncation in Chord mode with ratcheting.
- Increase to 256: Unnecessary -- realistic Chord mode has at most 16 notes (two hands on keyboard).

### Q8: What sentinel values should be used for kArpEndId and kNumParameters?

**Decision**: `kArpEndId = 3299`, `kNumParameters = 3300`.

**Rationale**: Ratchet step 31 = ID 3222 exceeds the current `kArpEndId = 3199`. Updating to 3299/3300 provides headroom for:
- Phase 7 (Euclidean): IDs 3230-3233 (4 parameters)
- Phase 8 (Conditional Trig): IDs 3240-3280 (41 parameters)
- Phase 9 (Spice/Dice): IDs 3290-3292 (3 parameters)

All fit within the 3200-3299 range with room to spare.

**Alternatives considered**:
- Exact fit (3222/3223): Would require another bump in Phase 7. Poor engineering practice.
- Larger range (3399/3400): Unnecessary overhead; 100 IDs of headroom is sufficient.

### Q9: How should per-sub-step gate calculation work?

**Decision**: Inline calculation using `subStepDuration` instead of `currentStepDuration_`.

**Rationale**: The existing `calculateGateDuration()` uses `currentStepDuration_` as its base. For sub-steps, the gate must be relative to `subStepDuration`. Rather than modifying the existing method (which other code paths depend on), the sub-step gate is calculated inline:
```cpp
size_t subGateDuration = std::max(size_t{1}, static_cast<size_t>(
    static_cast<double>(subStepDuration) *
    static_cast<double>(gateLengthPercent_) / 100.0 *
    static_cast<double>(gateLaneValue)));
```

**Alternatives considered**:
- Add overload to calculateGateDuration(size_t baseDuration, float gateLaneValue): Possible but adds API surface for a one-site usage.

### Q10: What happens when ratchet count 0 reaches the DSP?

**Decision**: Clamp to minimum 1 at both the parameter boundary AND the DSP read site.

**Rationale**: Defense in depth. `handleArpParamChange()` clamps the normalized value to [1, 4]. In `fireStep()`, the advance result is also clamped: `std::max(uint8_t{1}, ratchetLane_.advance())`. This ensures that even if a bug bypasses the parameter clamping, ratchet count 0 never reaches the subdivision logic (which would divide by zero).

**Alternatives considered**: None -- defense in depth is standard practice.
