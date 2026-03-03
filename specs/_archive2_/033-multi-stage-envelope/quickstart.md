# Quickstart: Multi-Stage Envelope Generator

**Date**: 2026-02-07 | **Spec**: specs/033-multi-stage-envelope/spec.md

---

## File Locations

### New Files

| File | Layer | Purpose |
|------|-------|---------|
| `dsp/include/krate/dsp/primitives/envelope_utils.h` | 1 | Shared envelope constants, enums, coefficient calculation |
| `dsp/include/krate/dsp/processors/multi_stage_envelope.h` | 2 | MultiStageEnvelope class |
| `dsp/tests/unit/processors/multi_stage_envelope_test.cpp` | - | Unit tests |

### Modified Files

| File | Change |
|------|--------|
| `dsp/include/krate/dsp/primitives/adsr_envelope.h` | Replace local constants/enums/calcCoefficients with `#include <krate/dsp/primitives/envelope_utils.h>` |
| `dsp/tests/CMakeLists.txt` | Add test file entry |

---

## Build & Test

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Build
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests

# Run all tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe

# Run only multi-stage envelope tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "MultiStageEnvelope*"

# Verify ADSR tests still pass (critical after refactor)
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "ADSREnvelope*"
```

---

## Usage Examples

### Basic Multi-Stage Envelope (6 stages, brass "spit" contour)

```cpp
#include <krate/dsp/processors/multi_stage_envelope.h>

using namespace Krate::DSP;

MultiStageEnvelope env;
env.prepare(44100.0f);

// Configure 6 stages: attack peak, dip, secondary rise, sustain hold, (post-sustain), (unused)
env.setNumStages(6);
env.setStage(0, 1.0f,  10.0f, EnvCurve::Exponential);   // Fast attack to peak
env.setStage(1, 0.6f,  30.0f, EnvCurve::Exponential);   // Dip
env.setStage(2, 0.8f,  50.0f, EnvCurve::Linear);        // Secondary rise
env.setStage(3, 0.7f,  40.0f, EnvCurve::Exponential);   // Settle to sustain
env.setStage(4, 0.3f, 100.0f, EnvCurve::Exponential);   // Post-sustain (only if loop passes through)
env.setStage(5, 0.0f, 200.0f, EnvCurve::Exponential);   // Final decay

env.setSustainPoint(3);        // Hold at stage 3 (level 0.7)
env.setReleaseTime(200.0f);   // 200ms release

// Note on
env.gate(true);

// Process audio block
float buffer[256];
env.processBlock(buffer, 256);

// Note off
env.gate(false);

// Process until idle
while (env.isActive()) {
    float sample = env.process();
    // ... use sample as modulation source
}
```

### LFO-Like Looping Envelope

```cpp
MultiStageEnvelope env;
env.prepare(44100.0f);

// Triangle-like loop between stages 0 and 1
env.setNumStages(4);
env.setStage(0, 1.0f, 100.0f, EnvCurve::Linear);   // Rise to 1.0 in 100ms
env.setStage(1, 0.0f, 100.0f, EnvCurve::Linear);   // Fall to 0.0 in 100ms
env.setStage(2, 0.5f, 50.0f,  EnvCurve::Linear);   // (unused during loop)
env.setStage(3, 0.5f, 50.0f,  EnvCurve::Linear);   // (unused during loop)

env.setLoopEnabled(true);
env.setLoopStart(0);
env.setLoopEnd(1);
env.setReleaseTime(50.0f);

env.gate(true);
// Envelope now cycles 0->1->0->1->... at 5 Hz (200ms period)

env.gate(false);
// Immediately exits loop, releases from current level to 0.0
```

### Legato Retrigger

```cpp
MultiStageEnvelope env;
env.prepare(44100.0f);
env.setRetriggerMode(RetriggerMode::Legato);

// First note
env.gate(true);
// ... process through stages to sustain ...

// Second note (overlapping) - envelope continues, no restart
env.gate(true);  // No effect -- still sustaining

// Release
env.gate(false);
// In release, new note returns to sustain
env.gate(true);  // Returns to sustain point smoothly
```

---

## Implementation Order

### Task Group 0: Envelope Utilities Extraction (Refactor)

1. Create `dsp/include/krate/dsp/primitives/envelope_utils.h`
2. Move constants, enums, `StageCoefficients`, `calcCoefficients()`, target ratio helpers from `adsr_envelope.h`
3. Update `adsr_envelope.h` to include `envelope_utils.h`
4. Build and verify ALL existing ADSR tests pass unchanged

### Task Group 1: Core Multi-Stage Traversal (P1 - FR-001 to FR-011)

1. Write tests for stage traversal, timing, state machine
2. Implement `MultiStageEnvelope` with Idle/Running states
3. Implement `process()` and `processBlock()`
4. Verify SC-001 (timing accuracy) and SC-002 (continuity)

### Task Group 2: Sustain Point (P1 - FR-012 to FR-015)

1. Write tests for sustain hold and gate-off behavior
2. Implement Sustaining state and sustain point selection
3. Verify sustain hold and release transition

### Task Group 3: Per-Stage Curves (P2 - FR-016 to FR-021)

1. Write tests for exponential, linear, logarithmic curves
2. Implement per-stage curve application
3. Verify SC-004 (curve shape differentiation)

### Task Group 4: Loop Points (P3 - FR-022 to FR-027)

1. Write tests for loop cycling, gate-off during loop
2. Implement loop wrap-around logic
3. Verify SC-005 (100 cycles without drift)

### Task Group 5: Retrigger Modes (P5 - FR-028, FR-029)

1. Write tests for hard retrigger and legato
2. Implement retrigger behavior
3. Verify SC-006 (no clicks on retrigger)

### Task Group 6: Real-Time Parameter Changes (P6 - FR-030 to FR-032)

1. Write tests for mid-stage parameter changes
2. Implement parameter recalculation logic
3. Verify no discontinuities

### Task Group 7: Final Verification and Architecture Update

1. Run full test suite
2. Run clang-tidy
3. Update `specs/_architecture_/layer-2-processors.md`
4. Fill compliance table
