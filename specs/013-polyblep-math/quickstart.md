# Quickstart: PolyBLEP Math Foundations

**Spec**: 013-polyblep-math | **Date**: 2026-02-03

---

## What This Feature Delivers

Two new Layer 0 header files providing the mathematical foundation for all future oscillator development:

1. **`dsp/include/krate/dsp/core/polyblep.h`** -- Four constexpr functions for polynomial band-limited correction of waveform discontinuities.
2. **`dsp/include/krate/dsp/core/phase_utils.h`** -- Phase accumulator struct and utility functions for oscillator phase management.

---

## File Locations

```
dsp/include/krate/dsp/core/
  polyblep.h          # PolyBLEP/PolyBLAMP correction functions
  phase_utils.h       # PhaseAccumulator + phase utilities

dsp/tests/unit/core/
  polyblep_test.cpp   # Tests for polyblep.h
  phase_utils_test.cpp # Tests for phase_utils.h
```

---

## Build Integration

Both test files must be added to `dsp/tests/CMakeLists.txt`:
- Under the `# Layer 0: Core` section of `add_executable(dsp_tests ...)`
- Under the `set_source_files_properties(...)` block for `-fno-fast-math` (Clang/GCC)

No CMakeLists.txt changes needed for the headers themselves -- they are header-only and picked up automatically by the `KrateDSP` target's include directory.

---

## How to Build & Test

```bash
# Windows (from repo root)
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Configure (if not already done)
"$CMAKE" --preset windows-x64-release

# Build DSP tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests

# Run all DSP tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe

# Run only polyblep tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[polyblep]"

# Run only phase_utils tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[phase_utils]"
```

---

## Usage Examples

### Anti-Aliased Sawtooth (using polyBlep)

```cpp
#include <krate/dsp/core/polyblep.h>
#include <krate/dsp/core/phase_utils.h>

using namespace Krate::DSP;

PhaseAccumulator acc;
acc.setFrequency(440.0f, 44100.0f);

for (int i = 0; i < numSamples; ++i) {
    // Naive sawtooth: ramp from -1 to +1
    float saw = 2.0f * static_cast<float>(acc.phase) - 1.0f;

    // Apply PolyBLEP correction at the wrap discontinuity
    float t = static_cast<float>(acc.phase);
    float dt = static_cast<float>(acc.increment);
    saw -= polyBlep(t, dt);

    output[i] = saw;
    acc.advance();
}
```

### Anti-Aliased Square Wave (using polyBlep)

```cpp
float duty = 0.5f;  // 50% duty cycle

for (int i = 0; i < numSamples; ++i) {
    float t = static_cast<float>(acc.phase);
    float dt = static_cast<float>(acc.increment);

    // Naive square: +1 for first half, -1 for second half
    float sq = (t < duty) ? 1.0f : -1.0f;

    // Apply PolyBLEP at both edges.
    // Sign convention: polyBlep corrects a downward step (like sawtooth wrap).
    //   - Rising edge at 0 (upward step): ADD correction (opposite direction)
    //   - Falling edge at duty (downward step): SUBTRACT correction (same direction)
    sq += polyBlep(t, dt);                          // rising edge at 0
    sq -= polyBlep(std::fmod(t + 1.0f - duty, 1.0f), dt);  // falling edge at duty

    output[i] = sq;
    acc.advance();
}
```

### Anti-Aliased Triangle (using polyBlamp)

```cpp
for (int i = 0; i < numSamples; ++i) {
    float t = static_cast<float>(acc.phase);
    float dt = static_cast<float>(acc.increment);

    // Naive triangle from integrated square
    float tri;
    if (t < 0.5f) {
        tri = 4.0f * t - 1.0f;   // rising: -1 to +1
    } else {
        tri = 3.0f - 4.0f * t;   // falling: +1 to -1
    }

    // PolyBLAMP corrections at slope change points (t=0 and t=0.5)
    // Slope changes by 8 (from +4 to -4) at t=0.5
    // Slope changes by 8 (from -4 to +4) at t=0 (wrap)
    tri += 4.0f * dt * polyBlamp(t, dt);                    // correction at wrap
    tri -= 4.0f * dt * polyBlamp(std::fmod(t + 0.5f, 1.0f), dt);  // correction at midpoint

    output[i] = tri;
    acc.advance();
}
```

### Phase Wrap Detection with Sub-Sample Offset

```cpp
PhaseAccumulator acc;
acc.setFrequency(1000.0f, 44100.0f);

for (int i = 0; i < numSamples; ++i) {
    bool wrapped = acc.advance();
    if (wrapped) {
        // Get fractional position of wrap within this sample
        double offset = subsamplePhaseWrapOffset(acc.phase, acc.increment);
        // offset in [0, 1) tells us WHERE in the sample the wrap happened
        // Use this for sub-sample-accurate BLEP placement
    }
}
```

### Standalone Phase Utilities

```cpp
// Calculate phase increment
double inc = calculatePhaseIncrement(440.0f, 44100.0f);  // ~0.009977

// Wrap phase
double w1 = wrapPhase(1.3);    // 0.3
double w2 = wrapPhase(-0.2);   // 0.8

// Detect wrap
bool didWrap = detectPhaseWrap(0.01, 0.99);  // true
```

---

## Implementation Order

The recommended implementation order follows the test-first development discipline:

### Task Group 1: polyblep.h (2-point functions)
1. Write tests for `polyBlep` (zero outside region, known values, continuity)
2. Implement `polyBlep`
3. Write tests for `polyBlamp` (zero outside region, known values, cubic shape)
4. Implement `polyBlamp`
5. Build, verify zero warnings, run tests

### Task Group 2: polyblep.h (4-point functions)
1. Write tests for `polyBlep4` (wider correction region, 4th-degree properties)
2. Implement `polyBlep4`
3. Write tests for `polyBlamp4` (wider region, 5th-degree properties)
4. Implement `polyBlamp4`
5. Write quality comparison tests (SC-003: second derivative, symmetry, DC bias)
6. Build, verify zero warnings, run tests

### Task Group 3: phase_utils.h
1. Write tests for `calculatePhaseIncrement`, `wrapPhase`, `detectPhaseWrap`
2. Implement standalone functions
3. Write tests for `subsamplePhaseWrapOffset`
4. Implement `subsamplePhaseWrapOffset`
5. Write tests for `PhaseAccumulator` (advance, wrap count, reset)
6. Implement `PhaseAccumulator`
7. Write LFO compatibility test (SC-009)
8. Build, verify zero warnings, run tests

### Task Group 4: Integration and validation
1. Write constexpr compile-time tests (SC-008)
2. Run full test suite, verify all SC-xxx criteria
3. Update `specs/_architecture_/layer-0-core.md`
4. Run clang-tidy
5. Commit

---

## Key Design Decisions

| Decision | Rationale |
|----------|-----------|
| Functions are `constexpr float` | Pure arithmetic, no std::math needed |
| PhaseAccumulator uses `double` | Matches existing LFO/AudioRateFilterFM pattern |
| wrapPhase uses subtraction, not fmod | Matches existing codebase pattern, faster |
| polyBlamp returns raw correction | Caller scales by slope change and dt |
| polyblep.h is independent of phase_utils.h | Maximum flexibility, no forced coupling |
| No NaN sanitization | Layer 0 convention per interpolation.h |
