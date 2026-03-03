# Quickstart: Euclidean Timing Mode Implementation

**Feature**: 075-euclidean-timing | **Date**: 2026-02-22

## Overview

This feature adds Euclidean timing mode to the arpeggiator, using the Bjorklund algorithm to determine which steps fire notes (hits) and which are silent (rests). It reuses the existing `EuclideanPattern` class (Layer 0) and extends `ArpeggiatorCore` (Layer 2) with a pre-fire gating check.

## Prerequisites

- Phase 6 (074-ratcheting) must be complete and merged
- Familiarity with `ArpeggiatorCore::fireStep()` flow
- Understanding of the existing lane advance pattern

## Implementation Order

### Task Group 1: DSP Layer -- ArpeggiatorCore Euclidean State

**Files**: `dsp/include/krate/dsp/processors/arpeggiator_core.h`

1. Add `#include <krate/dsp/core/euclidean_pattern.h>` to includes
2. Add 6 Euclidean member variables after ratchet state section
3. Add `regenerateEuclideanPattern()` private helper method
4. Add `setEuclideanSteps/Hits/Rotation/Enabled()` setter methods
5. Add `euclideanEnabled/Hits/Steps/Rotation()` getter methods
6. Add `regenerateEuclideanPattern()` call to constructor
7. Extend `resetLanes()` with `euclideanPosition_ = 0`
8. Extend `reset()` with `regenerateEuclideanPattern()` after `resetLanes()`

**Tests** (write FIRST):
- Getter/setter round-trip
- setEuclideanSteps clamps hits against new step count
- setEuclideanEnabled(true) resets position to 0
- resetLanes() resets euclideanPosition_ to 0
- regenerateEuclideanPattern produces correct bitmask for E(3,8)

### Task Group 2: DSP Layer -- fireStep() Euclidean Gating

**Files**: `dsp/include/krate/dsp/processors/arpeggiator_core.h`

1. Insert Euclidean gating check in `fireStep()` after lane advances, before modifier evaluation
2. Implement Euclidean rest path (noteOff emission, tie chain breaking, early return)
3. Add Euclidean position advance in defensive branch (result.count == 0)

**Tests** (write FIRST):
- E(3,8) tresillo: notes fire on steps 0, 3, 6 only (SC-001)
- E(8,8): every step fires = Euclidean disabled equivalent (SC-001)
- E(0,8): no notes fire (SC-001)
- E(5,8) cinquillo: correct hit pattern (SC-001)
- E(5,16) bossa nova: correct hit pattern (SC-001)
- Rotation produces distinct patterns (SC-002)
- Euclidean disabled = Phase 6 identical output (SC-004)
- Polymetric: steps=5 + velocity lane=3 = cycle of 15 (SC-003)
- Euclidean rest breaks tie chain (SC-006)
- Euclidean rest + modifier Rest = still silent (FR-019)
- Euclidean hit + modifier Rest = silent (FR-019)
- Euclidean rest + modifier Tie = silent, tie broken (FR-020)
- Euclidean hit + modifier Tie = sustain works (FR-020)
- Euclidean rest + ratchet count 4 = no sub-steps (SC-007)
- Euclidean hit + ratchet count 2 = correct sub-steps (SC-007)
- Chord mode: Euclidean hit fires all, rest silences all (FR-021)
- On/off transitions: no stuck notes (SC-005)
- Position reset on retrigger (SC-012)

### Task Group 3: Plugin Integration -- Parameter IDs and Registration

**Files**: `plugins/ruinae/src/plugin_ids.h`, `plugins/ruinae/src/parameters/arpeggiator_params.h`

1. Add 4 parameter IDs (3230-3233) to `plugin_ids.h`
2. Add 4 atomic members to `ArpeggiatorParams` struct
3. Extend `handleArpParamChange()` with 4 new cases
4. Extend `registerArpParams()` with 4 parameter registrations
5. Extend `formatArpParam()` with 4 display formatting cases

**Tests** (write FIRST):
- All 4 parameter IDs registered with kCanAutomate (SC-011)
- formatArpParam() returns correct strings for each parameter (SC-011)
- handleArpParamChange() correctly denormalizes values

### Task Group 4: Plugin Integration -- Serialization and Processor

**Files**: `plugins/ruinae/src/parameters/arpeggiator_params.h`, `plugins/ruinae/src/processor/processor.cpp`

1. Extend `saveArpParams()` with Euclidean serialization
2. Extend `loadArpParams()` with EOF-safe Euclidean deserialization
3. Extend `loadArpParamsToController()` with Euclidean controller sync
4. Extend `applyParamsToEngine()` in processor.cpp with Euclidean param transfer (prescribed setter order)

**Tests** (write FIRST):
- State round-trip: save then load preserves all values (SC-008)
- Phase 6 backward compat: load old preset defaults to disabled (SC-009)
- Controller sync: loaded values propagated correctly (FR-034)

### Task Group 5: Final Verification

1. Build all targets: `dsp_tests`, `ruinae_tests`
2. Run all tests, verify zero failures
3. Fix any compiler warnings
4. Run clang-tidy
5. Fill compliance table in spec.md

## Key Design Decisions

1. **Pre-fire gating check, NOT a dedicated ArpLane** -- simpler, less memory, cleaner separation of concerns
2. **Evaluation order: Euclidean -> Modifier -> Ratchet** -- cheap bit-check first for early-out; modifier side effects skip on rest
3. **Setter call order: steps -> hits -> rotation -> enabled** -- ensures correct clamping and pattern computation before activation
4. **All lanes advance on Euclidean rest steps** -- consistent with modifier Rest behavior, preserves polymetric cycling
5. **Euclidean rest breaks tie chains** -- prevents stuck notes when look-ahead has suppressed gate noteOff

## Common Pitfalls

- **Forgetting to advance `euclideanPosition_` in the defensive branch** (result.count == 0) -- causes position desync
- **Casting `euclideanPosition_` (size_t) to `int` for `isHit()` call** -- parameter type mismatch
- **Not calling `regenerateEuclideanPattern()` in the constructor** -- pattern starts as 0 bitmask instead of E(4,8)
- **Setting enabled before steps/hits/rotation in `applyParamsToEngine()`** -- activates gating with stale pattern
- **EOF handling: confusing Phase 6 compat (return true) with corrupt stream (return false)** -- only the FIRST Euclidean field (enabled) returning EOF is backward compat; subsequent fields missing means corrupt
