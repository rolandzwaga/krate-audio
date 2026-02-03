# Quickstart: Phase Accumulator Utilities

**Spec**: 014-phase-accumulation-utils | **Date**: 2026-02-03

---

## What This Feature Delivers

A Layer 0 header file providing centralized phase accumulation utilities for all oscillator development:

**`dsp/include/krate/dsp/core/phase_utils.h`** -- PhaseAccumulator struct and 4 standalone utility functions for phase management, wrapping, wrap detection, and sub-sample offset calculation.

**Note**: This header already exists in the codebase (implemented during spec 013-polyblep-math). Spec 014 formalizes the independent requirement set and closes minor test gaps.

---

## File Locations

```
dsp/include/krate/dsp/core/
  phase_utils.h          # PhaseAccumulator + phase utilities (EXISTING)

dsp/tests/unit/core/
  phase_utils_test.cpp   # Tests for phase_utils.h (EXISTING, extended)
```

---

## Build Integration

No CMakeLists.txt changes needed. The test file and header are already integrated:
- `dsp/tests/CMakeLists.txt` line 44: `unit/core/phase_utils_test.cpp`
- `-fno-fast-math` flag already set for Clang/GCC (line 238)

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

# Run only phase_utils tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[phase_utils]"
```

---

## Usage Examples

### Basic PhaseAccumulator Pattern

```cpp
#include <krate/dsp/core/phase_utils.h>

using namespace Krate::DSP;

PhaseAccumulator acc;
acc.setFrequency(440.0f, 44100.0f);

for (int i = 0; i < numSamples; ++i) {
    // Read phase for waveform generation
    float saw = 2.0f * static_cast<float>(acc.phase) - 1.0f;

    // Advance phase (returns true on wrap)
    bool wrapped = acc.advance();

    output[i] = saw;
}
```

### Anti-Aliased Sawtooth with PolyBLEP

```cpp
#include <krate/dsp/core/polyblep.h>
#include <krate/dsp/core/phase_utils.h>

using namespace Krate::DSP;

PhaseAccumulator acc;
acc.setFrequency(440.0f, 44100.0f);

for (int i = 0; i < numSamples; ++i) {
    // Naive sawtooth
    float saw = 2.0f * static_cast<float>(acc.phase) - 1.0f;

    // Apply PolyBLEP correction at the wrap discontinuity
    float t = static_cast<float>(acc.phase);
    float dt = static_cast<float>(acc.increment);
    saw -= polyBlep(t, dt);

    output[i] = saw;
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
        // Get fractional position [0, 1) within this sample where the wrap occurred
        double offset = subsamplePhaseWrapOffset(acc.phase, acc.increment);
        // offset = 0.6 means the wrap happened 60% through the sample interval
        // Use this for sub-sample-accurate BLEP placement
    }
}
```

### Standalone Phase Utilities

```cpp
// Calculate phase increment
double inc = calculatePhaseIncrement(440.0f, 44100.0f);  // ~0.009977

// Division-by-zero safe
double safe = calculatePhaseIncrement(440.0f, 0.0f);  // returns 0.0

// Wrap phase to [0, 1)
double w1 = wrapPhase(1.3);    // 0.3
double w2 = wrapPhase(-0.2);   // 0.8
double w3 = wrapPhase(0.5);    // 0.5 (unchanged)
double w4 = wrapPhase(1.0);    // 0.0 (exactly at boundary)

// Detect phase wrap (monotonically increasing phase)
bool didWrap = detectPhaseWrap(0.01, 0.99);   // true (current < previous)
bool noWrap = detectPhaseWrap(0.5, 0.4);      // false
```

### Reset Without Losing Frequency

```cpp
PhaseAccumulator acc;
acc.setFrequency(440.0f, 44100.0f);

// Process some samples...
for (int i = 0; i < 1000; ++i) acc.advance();

// Reset phase (e.g., on note retrigger)
acc.reset();  // phase = 0.0, increment preserved

// Continue processing at same frequency
acc.advance();  // continues from phase 0.0 with same increment
```

---

## Implementation Order

Since the implementation already exists, the work consists of closing test gaps:

### Task Group 1: Test Gap Closure
1. Add constexpr static_assert tests for all 4 standalone functions (SC-005)
2. Add exact US3-1 acceptance scenario test (increment=0.1, 10 advances, 1 wrap)
3. Build, verify zero warnings, run tests

### Task Group 2: Header Comment Update
1. Update `phase_utils.h` spec reference from 013 to 014
2. Build, verify zero warnings

### Task Group 3: Validation
1. Run full test suite, verify all SC-xxx criteria met
2. Verify architecture documentation is up to date
3. Run clang-tidy
4. Commit

---

## Key Design Decisions

| Decision | Rationale |
|----------|-----------|
| PhaseAccumulator is a struct | Value type for lightweight composition into oscillators |
| Uses `double` precision | Prevents accumulated rounding over long playback (matches LFO/FM) |
| wrapPhase uses subtraction | Matches existing codebase pattern, avoids std::fmod |
| Standalone functions are constexpr | Pure arithmetic, no std::math needed |
| phase_utils.h has no includes | All functions are pure arithmetic, no stdlib needed |
| advance() uses simple `phase -= 1.0` | Assumes increment < 1.0, matches LFO pattern exactly |

---

## API Quick Reference

```cpp
namespace Krate::DSP {

// Standalone functions (all constexpr noexcept)
[[nodiscard]] constexpr double calculatePhaseIncrement(float frequency, float sampleRate) noexcept;
[[nodiscard]] constexpr double wrapPhase(double phase) noexcept;
[[nodiscard]] constexpr bool detectPhaseWrap(double currentPhase, double previousPhase) noexcept;
[[nodiscard]] constexpr double subsamplePhaseWrapOffset(double phase, double increment) noexcept;

// Phase accumulator
struct PhaseAccumulator {
    double phase = 0.0;
    double increment = 0.0;

    [[nodiscard]] bool advance() noexcept;
    void reset() noexcept;
    void setFrequency(float frequency, float sampleRate) noexcept;
};

}
```
