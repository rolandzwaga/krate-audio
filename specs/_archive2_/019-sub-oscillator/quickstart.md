# Quickstart: Sub-Oscillator (019)

**Date**: 2026-02-04
**Spec**: specs/019-sub-oscillator/spec.md
**Status**: Ready for implementation

---

## Overview

The Sub-Oscillator is a Layer 2 DSP processor that tracks a master oscillator via flip-flop frequency division, replicating the classic analog sub-oscillator behavior found in hardware synthesizers like the Moog Sub 37 and Sequential Prophet. It supports square (with minBLEP anti-aliasing), sine, and triangle waveforms at one or two octaves below the master, plus an equal-power mix control.

---

## Files to Create

| File | Purpose |
|------|---------|
| `dsp/include/krate/dsp/processors/sub_oscillator.h` | Header-only implementation |
| `dsp/tests/unit/processors/sub_oscillator_test.cpp` | Unit tests |

## Files to Modify

| File | Change |
|------|--------|
| `dsp/tests/CMakeLists.txt` | Add test file to `dsp_tests` target and `-fno-fast-math` list |
| `specs/_architecture_/layer-2-processors.md` | Add SubOscillator documentation |

---

## Implementation Order

### Phase 1: Core Flip-Flop + Square Sub (User Story 1 -- P1)

1. Write test: SubOscillator constructor accepts MinBlepTable pointer (FR-003)
2. Write test: prepare() initializes state, validates table (FR-004)
3. Write test: reset() clears state preserves config (FR-005)
4. Write test: OneOctave square produces 220 Hz from 440 Hz master (SC-001)
5. Write test: MinBLEP alias rejection >= 40 dB (SC-003)
6. Write test: Output range [-2.0, 2.0] for all frequencies (SC-008)
7. Write test: No NaN/Inf in output (SC-009)
8. Implement SubOscillator class with Square waveform + OneOctave
9. Build and verify all tests pass
10. Fix compiler warnings

### Phase 2: Two-Octave Division (User Story 2 -- P2)

1. Write test: TwoOctaves square produces 110 Hz from 440 Hz master (SC-002)
2. Write test: Flip-flop chain toggles correctly (every 2 master wraps)
3. Write test: OneOctave to TwoOctaves mid-stream switch
4. Implement TwoOctaves support (second-stage flip-flop)
5. Build and verify all tests pass

### Phase 3: Sine and Triangle Waveforms (User Story 3 -- P2)

1. Write test: Sine sub at 220 Hz from 440 Hz master (SC-004)
2. Write test: Triangle sub at 220 Hz with odd harmonics (SC-005)
3. Write test: Frequency tracking during master frequency change (SC-011)
4. Implement sine/triangle via delta-phase tracking + phase accumulator
5. Build and verify all tests pass

### Phase 4: Mixed Output (User Story 4 -- P2)

1. Write test: mix=0 outputs main only (SC-006)
2. Write test: mix=1 outputs sub only (SC-006)
3. Write test: mix=0.5 equal-power RMS (SC-007)
4. Implement processMixed() with equalPowerGains()
5. Build and verify all tests pass

### Phase 5: Performance and Robustness

1. Write test: 128 concurrent instances at 96 kHz (SC-014)
2. Write test: CPU cost < 50 cycles/sample (SC-012)
3. Write test: Memory footprint <= 300 bytes (SC-013)
4. Write test: Deterministic rendering (FR-031)
5. Verify all SC-xxx criteria met
6. Run clang-tidy
7. Update architecture docs

---

## Key Code Patterns

### Basic Usage (Square Sub at One Octave)

```cpp
#include <krate/dsp/processors/sub_oscillator.h>
#include <krate/dsp/primitives/polyblep_oscillator.h>
#include <krate/dsp/primitives/minblep_table.h>

using namespace Krate::DSP;

// Setup (once)
MinBlepTable table;
table.prepare(64, 8);

PolyBlepOscillator master;
master.prepare(44100.0);
master.setFrequency(440.0f);
master.setWaveform(OscWaveform::Sawtooth);

SubOscillator sub(&table);
sub.prepare(44100.0);
sub.setOctave(SubOctave::OneOctave);
sub.setWaveform(SubWaveform::Square);

// Process loop
// phaseInc = frequency / sampleRate (recompute if master frequency changes)
float phaseInc = 440.0f / 44100.0f;
for (int i = 0; i < numSamples; ++i) {
    // If master frequency changes mid-stream, update phaseInc:
    // phaseInc = newFrequency / 44100.0f;
    float mainOut = master.process();
    bool wrapped = master.phaseWrapped();
    float subOut = sub.process(wrapped, phaseInc);
    output[i] = mainOut + subOut;  // or use processMixed()
}
```

### Mixed Output with Equal-Power Crossfade

```cpp
SubOscillator sub(&table);
sub.prepare(44100.0);
sub.setMix(0.5f);  // Equal blend

for (int i = 0; i < numSamples; ++i) {
    float mainOut = master.process();
    output[i] = sub.processMixed(mainOut, master.phaseWrapped(), phaseInc);
}
```

### Polyphonic Usage (128 Voices Sharing One Table)

```cpp
MinBlepTable sharedTable;
sharedTable.prepare(64, 8);

std::array<SubOscillator, 128> subs;
for (auto& sub : subs) {
    sub = SubOscillator(&sharedTable);
    sub.prepare(96000.0);
}
// Each sub maintains its own state; table is read-only shared
```

---

## Build Commands

```bash
# Configure
"/c/Program Files/CMake/bin/cmake.exe" --preset windows-x64-release

# Build DSP tests
"/c/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests

# Run all DSP tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe

# Run only sub-oscillator tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[SubOscillator]"
```

---

## Test Tag Convention

All sub-oscillator tests use the tag `[SubOscillator]` plus story-specific tags:

| Tag | Scope |
|-----|-------|
| `[SubOscillator]` | All sub-oscillator tests |
| `[square]` | Square waveform tests |
| `[sine]` | Sine waveform tests |
| `[triangle]` | Triangle waveform tests |
| `[twooctaves]` | Two-octave division tests |
| `[mix]` | processMixed() tests |
| `[perf]` | Performance tests |
| `[robustness]` | NaN/Inf/range tests |

---

## Critical Implementation Notes

1. **The SubOscillator does NOT own a PolyBlepOscillator.** It receives `masterPhaseWrapped` and `masterPhaseIncrement` as parameters to `process()`. The caller is responsible for running the master oscillator and passing these values.

2. **The flip-flop toggle drives the square waveform AND the sine/triangle resync.** When the output flip-flop transitions false->true, the sine/triangle phase accumulator is reset to 0.0.

3. **MinBLEP correction only applies to the Square waveform.** Sine and triangle are smooth waveforms that do not need discontinuity correction.

4. **The mix parameter gains are cached at setMix() time**, not computed per-sample. This saves two trig operations per sample.

5. **The phase increment parameter to process() is a float**, matching the PolyBlepOscillator's internal `dt_` precision. The SubOscillator converts it to double for the PhaseAccumulator.
