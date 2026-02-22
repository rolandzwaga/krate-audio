# Quickstart: Ratcheting (074)

**Date**: 2026-02-22

## What This Feature Does

Ratcheting subdivides individual arpeggiator steps into rapid retriggered repetitions (1-4 per step). Each step can have a different ratchet count configured via a per-step lane. When a step fires with ratchet count N, it produces N evenly-spaced noteOn/noteOff pairs within the step's duration, creating a "machine gun" or rhythmic roll effect.

## Architecture Overview

```
ArpeggiatorCore (Layer 2, header-only)
    |
    +-- ArpLane<uint8_t> ratchetLane_    // Per-step ratchet counts (1-4)
    |
    +-- Sub-step state members           // Track pending sub-steps across blocks
    |
    +-- processBlock() jump-ahead loop   // NextEvent::SubStep handler
    |
    +-- fireStep()                       // Initialize sub-step state for ratcheted steps
```

## Files to Modify (in implementation order)

### 1. DSP Layer -- ArpeggiatorCore

**File**: `dsp/include/krate/dsp/processors/arpeggiator_core.h`

Changes:
- Add `ratchetLane_` member and accessors (FR-001, FR-006)
- Add sub-step tracking members (FR-011)
- Update `kMaxEvents` from 64 to 128 (FR-037)
- Extend constructor: set ratchet lane step 0 to 1 (FR-003)
- Extend `resetLanes()`: reset ratchet lane + clear sub-step state (FR-005)
- Extend `setEnabled()`: clear sub-step state on disable (FR-026)
- Extend `processBlock()`: add SubStep event type to jump-ahead loop (FR-014, FR-015, FR-016)
- Extend `processBlock()`: clear sub-step state on transport stop (FR-027)
- Extend `fireStep()`: advance ratchet lane, initialize sub-step state (FR-004, FR-007-FR-010, FR-013, FR-017-FR-025)
- Extend defensive branch: advance ratchet lane, clear sub-step state (FR-036)

### 2. Plugin -- Parameter IDs

**File**: `plugins/ruinae/src/plugin_ids.h`

Changes:
- Add 33 parameter IDs: kArpRatchetLaneLengthId (3190) through kArpRatchetLaneStep31Id (3222) (FR-028)
- Update kArpEndId from 3199 to 3299 (FR-029)
- Update kNumParameters from 3200 to 3300 (FR-029)

### 3. Plugin -- Parameter Infrastructure

**File**: `plugins/ruinae/src/parameters/arpeggiator_params.h`

Changes:
- Extend ArpeggiatorParams struct with ratchet atomics (FR-031)
- Extend constructor to initialize ratchet defaults (FR-031)
- Extend handleArpParamChange() for ratchet dispatch (FR-032)
- Extend registerArpParams() for ratchet registration (FR-028, FR-030)
- Extend formatArpParam() for ratchet display (SC-010)
- Extend saveArpParams() for ratchet serialization (FR-033)
- Extend loadArpParams() for ratchet deserialization with backward compat (FR-034)

### 4. Plugin -- Processor Integration

**File**: `plugins/ruinae/src/processor/processor.cpp`

Changes:
- Extend applyParamsToEngine() with ratchet lane expand-write-shrink (FR-035)
- NOTE: arpEvents_ buffer in processor.h is already 128 -- no change needed

### 5. Tests

**File**: `dsp/tests/unit/processors/arpeggiator_core_test.cpp`

New test cases for all 13 success criteria (SC-001 through SC-013).

**File**: `plugins/ruinae/tests/unit/processor/arpeggiator_integration_test.cpp`

New test cases for SC-007, SC-008, SC-010.

## Key Implementation Patterns

### Expand-Write-Shrink (lane parameter transfer)

```cpp
arpCore_.ratchetLane().setLength(32);   // Expand
for (int i = 0; i < 32; ++i) {
    arpCore_.ratchetLane().setStep(i, static_cast<uint8_t>(value));
}
arpCore_.ratchetLane().setLength(actualLength);  // Shrink
```

### EOF-Safe Deserialization (backward compat)

```cpp
if (!streamer.readInt32(intVal)) return true;   // EOF = old preset, keep defaults
// ... read remaining fields ...
if (!streamer.readInt32(intVal)) return false;   // Mid-data EOF = corrupt
```

### Sub-Step Timing

```cpp
size_t subStepDuration = currentStepDuration_ / ratchetCount;  // Integer division
// Sub-step k fires at k * subStepDuration
// Last sub-step absorbs remainder: duration = subStepDuration + (stepDuration % ratchetCount)
```

## Test-First Workflow

For each task group:
1. Write failing test for the specific FR/SC
2. Implement the minimum code to make it pass
3. Build (`cmake --build build/windows-x64-release --config Release`)
4. Verify zero compiler warnings
5. Run all DSP tests
6. Commit

## Gotchas to Watch For

- ArpLane<uint8_t> zero-initializes to 0. Ratchet count 0 is invalid. Always set step 0 to 1.
- Accent stores in ratchetVelocity_ the PRE-accent velocity. First sub-step uses accented velocity from fireStep().
- Look-ahead for "next step is Tie/Slide" applies only to the LAST sub-step, not intermediate ones.
- When bar boundary and sub-step coincide, bar boundary wins and clears sub-step state.
- The existing `calculateGateDuration()` uses `currentStepDuration_` -- sub-step gate must be calculated inline with `subStepDuration`.
