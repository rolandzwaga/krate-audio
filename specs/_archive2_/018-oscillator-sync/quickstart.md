# Quickstart: Oscillator Sync Implementation

**Feature**: 018-oscillator-sync | **Date**: 2026-02-04

---

## Overview

This feature implements a Layer 2 `SyncOscillator` processor that provides band-limited oscillator synchronization with three modes: Hard, Reverse, and PhaseAdvance. It also extends the existing `MinBlepTable` with minBLAMP (band-limited ramp) support for derivative discontinuity correction.

---

## Files to Create

| File | Layer | Purpose |
|------|-------|---------|
| `dsp/include/krate/dsp/processors/sync_oscillator.h` | L2 | SyncOscillator class + SyncMode enum |
| `dsp/tests/unit/processors/sync_oscillator_test.cpp` | Test | All test cases for SyncOscillator |

## Files to Modify

| File | Change |
|------|--------|
| `dsp/include/krate/dsp/primitives/minblep_table.h` | Add minBLAMP table, `sampleBlamp()`, `Residual::addBlamp()` |
| `dsp/tests/unit/primitives/minblep_table_test.cpp` | Add tests for minBLAMP support |
| `dsp/tests/CMakeLists.txt` | Add `sync_oscillator_test.cpp` to test sources + `-fno-fast-math` list |
| `specs/_architecture_/layer-2-processors.md` | Add SyncOscillator documentation |

---

## Build and Test Commands

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Configure (if not already done)
"$CMAKE" --preset windows-x64-release

# Build DSP tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests

# Run all DSP tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe

# Run only sync oscillator tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[SyncOscillator]"

# Run only minBLAMP extension tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[minblamp]"
```

---

## Implementation Order

### Phase N-1.0: MinBLAMP Extension (prerequisite)

1. Write tests for `MinBlepTable::sampleBlamp()` and `Residual::addBlamp()`.
2. Implement minBLAMP table generation in `MinBlepTable::prepare()`.
3. Implement `sampleBlamp()` method.
4. Implement `Residual::addBlamp()` method.
5. Build, verify all existing minBLEP tests still pass, new tests pass.

### Phase N-1.1: SyncOscillator Hard Sync (P1)

1. Write tests for hard sync (SC-001, SC-002, SC-003, SC-004, SC-008).
2. Create `sync_oscillator.h` with class skeleton.
3. Implement `prepare()`, `reset()`, parameter setters.
4. Implement `process()` with master phase tracking and hard sync logic.
5. Implement `processBlock()` as loop over `process()`.
6. Implement `evaluateWaveform()` helper for discontinuity computation.
7. Build, run tests, verify all pass.

### Phase N-1.2: Reverse Sync (P2)

1. Write tests for reverse sync (SC-005).
2. Implement reverse sync logic in `process()`.
3. Implement `evaluateWaveformDerivative()` helper.
4. Add minBLAMP correction at reversal points.
5. Build, run tests, verify all pass.

### Phase N-1.3: Phase Advance Sync (P2)

1. Write tests for phase advance sync (SC-006, SC-007).
2. Implement phase advance logic in `process()`.
3. Build, run tests, verify all pass.

### Phase N-1.4: Sync Amount Control (P2)

1. Write tests for sync amount crossfading (SC-008, SC-014).
2. Implement sync amount interpolation for all three modes.
3. Build, run tests, verify all pass.

### Phase N-1.5: Multiple Waveforms & Edge Cases (P3)

1. Write tests for multiple slave waveforms (SC-012).
2. Write tests for edge cases (SC-009, SC-010, SC-013).
3. Verify all waveforms work correctly with all sync modes.
4. Build, run tests, verify all pass.

### Phase N-1.6: Quality Gates

1. Run clang-tidy.
2. Verify zero compiler warnings.
3. Measure CPU performance (SC-015).
4. Update architecture docs.

---

## Key Dependencies

```
sync_oscillator.h
    |
    +-- core/phase_utils.h         (PhaseAccumulator, calculatePhaseIncrement,
    |                                wrapPhase, subsamplePhaseWrapOffset)
    +-- core/math_constants.h       (kPi, kTwoPi)
    +-- core/db_utils.h             (detail::isNaN, detail::isInf)
    +-- primitives/polyblep_oscillator.h  (PolyBlepOscillator, OscWaveform)
    +-- primitives/minblep_table.h  (MinBlepTable, Residual, addBlep, addBlamp)
```

---

## API Quick Reference

```cpp
// Setup
MinBlepTable table;
table.prepare();  // 64x oversampling, 8 zero crossings

SyncOscillator osc(&table);
osc.prepare(44100.0);

// Configure
osc.setMasterFrequency(220.0f);
osc.setSlaveFrequency(660.0f);
osc.setSlaveWaveform(OscWaveform::Sawtooth);
osc.setSyncMode(SyncMode::Hard);
osc.setSyncAmount(1.0f);

// Process
float sample = osc.process();

// Block process
float buffer[512];
osc.processBlock(buffer, 512);

// Reset
osc.reset();
```
