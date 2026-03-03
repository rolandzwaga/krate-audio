# Quickstart: Scale & Interval Foundation (ScaleHarmonizer)

**Date**: 2026-02-17 | **Spec**: 060-scale-interval-foundation

## Overview

This feature adds a `ScaleHarmonizer` class to Layer 0 (Core) of the KrateDSP library. It computes diatonic intervals for harmonizer effects -- given an input MIDI note, key, scale, and diatonic step count, it returns the musically correct semitone shift.

## Files to Create

| File | Purpose |
|------|---------|
| `dsp/include/krate/dsp/core/scale_harmonizer.h` | Header-only implementation |
| `dsp/tests/unit/core/scale_harmonizer_test.cpp` | Comprehensive test suite |

## Files to Modify

| File | Change |
|------|--------|
| `dsp/tests/CMakeLists.txt` | Add test source to `dsp_tests` target and `-fno-fast-math` list |
| `specs/_architecture_/layer-0-core.md` | Add ScaleHarmonizer to architecture documentation |

## Build and Test

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Configure (if not already done)
"$CMAKE" --preset windows-x64-release

# Build DSP tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests

# Run only ScaleHarmonizer tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[scale-harmonizer]"

# Run all DSP tests (verify no regressions)
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe
```

## Usage Example

```cpp
#include <krate/dsp/core/scale_harmonizer.h>
#include <krate/dsp/core/pitch_utils.h>  // for semitonesToRatio()

using namespace Krate::DSP;

// Create and configure harmonizer
ScaleHarmonizer harm;
harm.setKey(0);                          // Key of C
harm.setScale(ScaleType::Major);         // Major scale

// Compute a "3rd above" for C4 (MIDI 60)
auto result = harm.calculate(60, +2);    // diatonicSteps = +2 means "3rd above"
// result.semitones == +4 (C -> E, a major 3rd)
// result.targetNote == 64 (E4)
// result.scaleDegree == 2 (E is degree 2 in C Major)
// result.octaveOffset == 0

// Compute a "3rd above" for D4 (MIDI 62)
auto result2 = harm.calculate(62, +2);
// result2.semitones == +3 (D -> F, a minor 3rd)
// result2.targetNote == 65 (F4)
// result2.scaleDegree == 3 (F is degree 3 in C Major)

// Use the shift for pitch shifting
float ratio = semitonesToRatio(static_cast<float>(result.semitones));

// Convenience: compute shift directly from frequency
float shift = harm.getSemitoneShift(440.0f, +2);  // A4, 3rd above in C Major

// Query scale membership
int degree = harm.getScaleDegree(60);    // 0 (C is root of C Major)
int degree2 = harm.getScaleDegree(61);   // -1 (C# is not in C Major)

// Quantize to scale
int snapped = harm.quantizeToScale(61);  // 60 (C#4 snaps to C4)

// Chromatic mode (fixed shift, no scale logic)
harm.setScale(ScaleType::Chromatic);
auto result3 = harm.calculate(60, +7);   // Always +7 semitones
// result3.semitones == +7
// result3.scaleDegree == -1 (not applicable)
```

## Algorithm Summary

1. Extract pitch class: `pitchClass = midiNote % 12`
2. Compute offset from root: `offset = (pitchClass - rootNote + 12) % 12`
3. Find nearest scale degree via precomputed reverse lookup table
4. Add diatonic steps to get target degree (with octave wrapping via `/ 7` and `% 7`)
5. Look up target degree's semitone offset
6. Compute semitone shift = targetOffset - inputOffset + octaveAdjustment
7. Clamp target MIDI note to [0, 127]

## Key Design Decisions

- **Header-only**: All methods inline, all data constexpr
- **O(1) algorithm**: No loops in the hot path (precomputed lookup tables)
- **noexcept throughout**: Safe for real-time audio thread
- **Zero allocations**: All data is stack-local or constexpr
- **Immutable after configuration**: Thread-safe for concurrent reads
