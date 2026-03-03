# Quickstart: Spice/Dice & Humanize Implementation

**Feature**: 077-spice-dice-humanize | **Date**: 2026-02-23

## Prerequisites

- Phase 8 (076-conditional-trigs) is complete and merged to main
- Build environment configured per CLAUDE.md (CMake 3.20+, MSVC/Clang/GCC)
- Catch2 test framework available via `dsp_tests` and `ruinae_tests` targets

## Implementation Order

### Task Group 1: DSP Core -- Overlay Arrays & Spice API

**Files**:
- `dsp/include/krate/dsp/processors/arpeggiator_core.h`
- `dsp/tests/unit/processors/arpeggiator_core_test.cpp`

**Steps**:
1. Write tests for overlay initialization (FR-002), setSpice/spice getters (FR-003, FR-004), and triggerDice() basic behavior (FR-005, FR-007)
2. Add overlay arrays, spice_ member, and spiceDiceRng_ to ArpeggiatorCore
3. Add constructor initialization (fill with identity values)
4. Add setSpice(), spice(), triggerDice() methods
5. Build and verify tests pass

### Task Group 2: DSP Core -- Spice Blend in fireStep()

**Files**:
- `dsp/include/krate/dsp/processors/arpeggiator_core.h`
- `dsp/tests/unit/processors/arpeggiator_core_test.cpp`

**Steps**:
1. Write tests: Spice 0% = Phase 8 identical (SC-001), Spice 100% = overlay (SC-002), Spice 50% interpolation (SC-003)
2. Add overlay index capture before lane advances
3. Add Spice blend block after lane advances (velocity lerp, gate lerp, ratchet round, condition threshold)
4. Update resetLanes() with comments documenting overlay preservation (FR-025-029)
5. Build and verify tests pass

### Task Group 3: DSP Core -- Humanize System

**Files**:
- `dsp/include/krate/dsp/processors/arpeggiator_core.h`
- `dsp/tests/unit/processors/arpeggiator_core_test.cpp`

**Steps**:
1. Write tests: Humanize 0% = no offsets (SC-005), Humanize 100% timing/velocity/gate distributions (SC-006/007/008), Humanize 50% linear scaling (SC-009)
2. Add humanize_ member and humanizeRng_ (seed 48271)
3. Add setHumanize(), humanize() accessors
4. Add humanize offset computation in fireStep() (after accent, before note emission)
5. Add humanize PRNG consumption at all 5 skip points + defensive branch (FR-023, FR-024, FR-041)
6. Handle ratcheted step interactions (FR-019, FR-020, FR-021)
7. Build and verify tests pass

### Task Group 4: DSP Core -- Composition & Edge Cases

**Files**:
- `dsp/tests/unit/processors/arpeggiator_core_test.cpp`

**Steps**:
1. Write and verify: Spice + Humanize compose correctly (SC-015)
2. Write and verify: PRNG distinctness across all 4 seeds (SC-014)
3. Write and verify: Overlay preserved across reset (FR-025)
4. Write and verify: Zero heap allocation inspection (SC-012)
5. Build and verify ALL existing tests still pass (regression check)

### Task Group 5: Plugin Integration -- Parameters

**Files**:
- `plugins/ruinae/src/plugin_ids.h`
- `plugins/ruinae/src/parameters/arpeggiator_params.h`
- `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`

**Steps**:
1. Write test for parameter registration and formatting (SC-013)
2. Add parameter IDs (3290-3292) to plugin_ids.h
3. Add atomic fields to ArpeggiatorParams struct
4. Extend handleArpParamChange() with Spice/Dice/Humanize cases
5. Extend registerArpParams() with 3 new parameters
6. Extend formatArpParam() with display strings
7. Build and verify tests pass

### Task Group 6: Plugin Integration -- Serialization & Engine Transfer

**Files**:
- `plugins/ruinae/src/parameters/arpeggiator_params.h`
- `plugins/ruinae/src/processor/processor.cpp`
- `plugins/ruinae/tests/unit/processor/arp_integration_test.cpp`

**Steps**:
1. Write test for state round-trip (SC-010) and Phase 8 backward compat (SC-011)
2. Extend saveArpParams() -- append Spice + Humanize after fillToggle
3. Extend loadArpParams() -- EOF-safe Spice + Humanize read
4. Extend loadArpParamsToController() -- Spice + Humanize controller sync
5. Extend applyParamsToEngine() -- setSpice, Dice compare_exchange, setHumanize
6. Build and verify tests pass

### Task Group 7: Final Validation

**Steps**:
1. Full build (zero warnings)
2. Run all DSP tests (`dsp_tests`)
3. Run all plugin tests (`ruinae_tests`)
4. Run pluginval at strictness level 5
5. Run clang-tidy
6. Fill compliance table with specific evidence
7. Commit

## Build Commands

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Configure
"$CMAKE" --preset windows-x64-release

# Build DSP tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests

# Run DSP tests
build/windows-x64-release/bin/Release/dsp_tests.exe

# Build plugin tests
"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests

# Run plugin tests
build/windows-x64-release/bin/Release/ruinae_tests.exe

# Build full plugin
"$CMAKE" --build build/windows-x64-release --config Release

# Pluginval
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"
```

## Key Files Quick Reference

| Purpose | Path |
|---|---|
| ArpeggiatorCore (DSP) | `dsp/include/krate/dsp/processors/arpeggiator_core.h` |
| Xorshift32 PRNG | `dsp/include/krate/dsp/core/random.h` |
| ArpLane<T> | `dsp/include/krate/dsp/primitives/arp_lane.h` |
| DSP tests | `dsp/tests/unit/processors/arpeggiator_core_test.cpp` |
| Parameter IDs | `plugins/ruinae/src/plugin_ids.h` |
| Param storage/dispatch | `plugins/ruinae/src/parameters/arpeggiator_params.h` |
| Processor integration | `plugins/ruinae/src/processor/processor.cpp` |
| Plugin tests | `plugins/ruinae/tests/unit/processor/arp_integration_test.cpp` |
| Param tests | `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp` |

## Critical Implementation Notes

1. **nextFloat() already exists** in random.h -- do NOT modify Layer 0
2. **Capture overlay indices BEFORE lane advances** -- `currentStep()` gives pre-advance position
3. **Humanize PRNG consumed on EVERY step** including skips (5 return points + defensive branch)
4. **Dice trigger uses compare_exchange_strong** -- NOT plain load/store
5. **Overlay NOT reset** in reset()/resetLanes() -- generative state persists
6. **Overlay NOT serialized** -- only Spice and Humanize amounts are saved
7. **Ratchet humanize**: timing=first sub-step only, velocity=first only, gate=all sub-steps
8. **Serialization order**: Spice float, Humanize float -- appended after Phase 8 fillToggle
