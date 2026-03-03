# Quickstart: Conditional Trig System Implementation

**Feature**: 076-conditional-trigs | **Date**: 2026-02-22

## Overview

This feature adds an Elektron-inspired conditional trig system to the arpeggiator, providing per-step condition evaluation (probability, A:B ratios, First-loop-only, Fill/NotFill) that creates patterns evolving over multiple loops. It reuses the existing `Xorshift32` PRNG (Layer 0) and `ArpLane<uint8_t>` (Layer 1) and extends `ArpeggiatorCore` (Layer 2) with a condition evaluation check inserted between Euclidean gating and modifier evaluation.

## Prerequisites

- Phase 7 (075-euclidean-timing) must be complete and merged
- Familiarity with `ArpeggiatorCore::fireStep()` flow (especially the Euclidean gating insertion point)
- Understanding of the existing lane advance pattern and expand-write-shrink parameter application

## Implementation Order

### Task Group 1: DSP Layer -- TrigCondition Enum and Core State

**Files**: `dsp/include/krate/dsp/processors/arpeggiator_core.h`

1. Add `#include <krate/dsp/core/random.h>` to includes
2. Define `TrigCondition` enum class (uint8_t-backed, 18 values + kCount sentinel) after `ArpRetriggerMode`
3. Add 4 condition state members: `conditionLane_`, `loopCount_`, `fillActive_`, `conditionRng_{7919}`
4. Add `conditionLane()` const and non-const accessors
5. Add `setFillActive()` / `fillActive()` getter/setter
6. Add explicit constructor initialization: `conditionLane_.setStep(0, static_cast<uint8_t>(TrigCondition::Always))`
7. Extend `resetLanes()`: add `conditionLane_.reset()` and `loopCount_ = 0` (NOT fillActive_, NOT conditionRng_)

**Tests** (write FIRST):
- TrigCondition enum value count is 18 + sentinel
- conditionLane() default: length 1, step 0 = 0 (Always)
- setFillActive(true) / fillActive() round-trip
- resetLanes() resets conditionLane_ position to 0 and loopCount_ to 0
- resetLanes() does NOT reset fillActive_ or conditionRng_ state
- conditionRng_ produces deterministic sequence (seed 7919)
- conditionRng_ sequence differs from NoteSelector PRNG (seed 42) (SC-014)

### Task Group 2: DSP Layer -- evaluateCondition() and Loop Count

**Files**: `dsp/include/krate/dsp/processors/arpeggiator_core.h`

1. Add private `evaluateCondition(uint8_t condition)` method with 18-way switch
2. Implement probability evaluation: `conditionRng_.nextUnipolar() < threshold`
3. Implement A:B ratio evaluation: `loopCount_ % B == A - 1`
4. Implement First: `loopCount_ == 0`
5. Implement Fill/NotFill: `fillActive_` / `!fillActive_`
6. Default case returns true (defensive fallback for out-of-range values)

**Tests** (write FIRST):
- Always returns true
- Prob50 fires ~50% over 10000 evaluations (+/- 3%) (SC-001)
- Prob10/25/75/90 statistical distribution (SC-001)
- Ratio_1_2 fires on loops {0, 2, 4, ...} (SC-002)
- Ratio_2_2 fires on loops {1, 3, 5, ...} (SC-002)
- All 9 A:B ratios verified across 12+ loops (SC-002)
- First fires only on loop 0 (SC-003)
- Fill fires only when fillActive_ is true (SC-004)
- NotFill fires only when fillActive_ is false (SC-004)
- Out-of-range condition (value 18+) returns true
- Loop count increment on condition lane wrap
- Loop count increment on every step with length-1 condition lane (FR-018 degenerate case)

### Task Group 3: DSP Layer -- fireStep() Integration

**Files**: `dsp/include/krate/dsp/processors/arpeggiator_core.h`

1. Add condition lane advance to the "advance all lanes" section in fireStep()
2. Add loop count wrap detection after condition lane advance
3. Insert condition evaluation after Euclidean gating, before modifier evaluation
4. Implement condition-fail rest path (identical to Euclidean rest path)
5. Add condition lane advance to the defensive branch (result.count == 0)
6. Add loop count wrap detection to the defensive branch

**Tests** (write FIRST, summary -- see tasks.md Phase 3 for complete test list of 21 tests):
- Default condition (Always on all steps) = Phase 7 identical output at 120/140/180 BPM (SC-005)
- Condition-failed step produces rest (no noteOn, noteOff emitted)
- Condition-failed step breaks tie chain (FR-029)
- Euclidean rest + condition = rest, PRNG not consumed (SC-006)
- Euclidean hit + condition fail = rest (SC-006)
- Euclidean hit + condition pass = normal note (SC-006)
- Condition fail + modifier Tie = rest, tie chain broken (SC-007)
- Condition fail + ratchet = no sub-steps (SC-007)
- Condition pass + modifier Rest = rest (modifier still works) (SC-007)
- Condition pass + ratchet = normal sub-steps (SC-007)
- Polymetric: condition lane length 3 + velocity lane length 5 = cycle 15 (SC-008)
- Chord mode: condition applies uniformly to all chord notes (FR-032)
- Loop count resets on retrigger, transport restart, arp re-enable (SC-013)
- Loop count does NOT reset on disable alone (SC-013)
- Fill toggle mid-loop takes effect at next step boundary (SC-004)
- Zero heap allocation in condition paths (SC-011, code inspection)

### Task Group 4: Plugin Integration -- Parameter IDs and Registration

**Files**: `plugins/ruinae/src/plugin_ids.h`, `plugins/ruinae/src/parameters/arpeggiator_params.h`

1. Add 34 parameter IDs to `plugin_ids.h` (3240-3272 for condition lane, 3280 for fill toggle)
2. Add 3 atomic members to `ArpeggiatorParams` struct
3. Extend `handleArpParamChange()` with condition parameter dispatch
4. Extend `registerArpParams()` with 34 parameter registrations (step params hidden, length and fill visible)
5. Extend `formatArpParam()` with condition display formatting

**Tests** (write FIRST):
- All 34 parameter IDs registered with correct flags (kCanAutomate on all; kIsHidden on steps only) (SC-012)
- formatArpParam() returns correct display strings for all 18 condition types (SC-012)
- formatArpParam() returns correct strings for lane length and fill toggle
- handleArpParamChange() correctly denormalizes condition values

### Task Group 5: Plugin Integration -- Serialization and Processor

**Files**: `plugins/ruinae/src/parameters/arpeggiator_params.h`, `plugins/ruinae/src/processor/processor.cpp`

1. Extend `saveArpParams()` with condition serialization (after Euclidean data)
2. Extend `loadArpParams()` with EOF-safe condition deserialization
3. Extend `loadArpParamsToController()` with condition controller state sync
4. Extend `applyParamsToEngine()` in processor.cpp with condition lane transfer (expand-write-shrink) and fill toggle

**Tests** (write FIRST):
- State round-trip: save then load preserves all condition values exactly (SC-009)
- Phase 7 backward compat: load old preset defaults to length 1, all Always, fill off (SC-010)
- Controller sync: loaded values propagated to controller correctly (FR-048)
- Out-of-range values clamped on load: length to [1,32], steps to [0,17]

### Task Group 6: Final Verification

1. Build all targets: `dsp_tests`, `ruinae_tests`
2. Run all tests, verify zero failures
3. Fix any compiler warnings
4. Run clang-tidy
5. Run pluginval (strictness level 5) on Ruinae.vst3
6. Fill compliance table in spec.md with specific evidence

## Key Design Decisions

1. **Condition evaluation AFTER Euclidean, BEFORE modifiers** -- three-layer gating chain: Euclidean (structural) -> Condition (evolutionary) -> Modifier (articulation)
2. **Loop count tied to condition lane wrap, NOT a global pattern length** -- self-contained, predictable, matches Elektron model
3. **Dedicated PRNG instance (seed 7919) separate from NoteSelector (seed 42)** -- independent sequences, no cross-feature coupling
4. **PRNG NOT reset on resetLanes()** -- ensures non-repeating probability sequences across resets
5. **fillActive_ NOT reset on resetLanes()** -- performance control preserved like a sustain pedal
6. **Expand-write-shrink pattern for condition lane application** -- consistent with all other lanes (Phases 4-7)
7. **Always = 0 = default uint8_t value** -- zero-initialization of ArpLane<uint8_t> steps is correct default behavior
8. **Condition-fail path mirrors Euclidean rest path** -- identical cleanup (noteOff, tie break, swing counter update)

## Common Pitfalls

- **Forgetting to advance conditionLane_ in the defensive branch** (result.count == 0) -- causes position desync with other lanes
- **Forgetting loopCount_ wrap detection in the defensive branch** -- loop count drifts
- **Consuming PRNG on Euclidean rest steps** -- condition evaluation must not execute on Euclidean rest
- **Resetting fillActive_ in resetLanes()** -- it is a performance control, NOT a pattern state
- **Resetting conditionRng_ in resetLanes()** -- would produce identical "random" patterns on every restart
- **Incorrect loopCount_ increment for length-1 lane** -- length 1 wraps on every step (correct behavior), so loopCount_ increments on every step
- **EOF handling: confusing Phase 7 compat (return true at conditionLaneLength) with corrupt stream (return false at mid-section)** -- only the FIRST condition field returning EOF is backward compat
- **Missing condition lane advance before Euclidean gating check** -- all lanes must advance unconditionally before any gating checks
- **Not clamping condition step values to [0, 17] in applyParamsToEngine()** -- values >= 18 would hit the defensive default case
- **Missing swing step counter increment and step duration recalculation in condition-fail path** -- causes timing drift if omitted
