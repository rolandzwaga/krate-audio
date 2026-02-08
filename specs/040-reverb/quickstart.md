# Quickstart: Dattorro Plate Reverb

**Feature Branch**: `040-reverb` | **Date**: 2026-02-08

## Overview

Implement the Dattorro plate reverb algorithm as a Layer 4 effect in the KrateDSP library. The reverb provides spatial depth to synthesizer output with configurable room size, damping, diffusion, stereo width, pre-delay, modulation, and freeze mode.

## Files to Create/Modify

### New Files

| File | Description |
|------|-------------|
| `dsp/include/krate/dsp/effects/reverb.h` | Header-only reverb implementation |
| `dsp/tests/unit/effects/reverb_test.cpp` | Catch2 unit tests |

### Files to Modify

| File | Change |
|------|--------|
| `dsp/CMakeLists.txt` | Add `reverb.h` to `KRATE_DSP_EFFECTS_HEADERS` |
| `dsp/tests/CMakeLists.txt` | Add `reverb_test.cpp` to `dsp_tests` and `-fno-fast-math` list |
| `specs/_architecture_/layer-4-features.md` | Add Reverb component documentation |

## Build & Test Commands

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Configure
"$CMAKE" --preset windows-x64-release

# Build DSP tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests

# Run reverb tests only
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[reverb]"

# Run all DSP tests (ensure no regressions)
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe
```

## Usage Example

```cpp
#include <krate/dsp/effects/reverb.h>

using namespace Krate::DSP;

Reverb reverb;
reverb.prepare(44100.0);

ReverbParams params;
params.roomSize = 0.7f;     // Large room
params.damping = 0.4f;      // Moderate HF damping
params.width = 1.0f;        // Full stereo
params.mix = 0.35f;         // 35% wet
params.preDelayMs = 20.0f;  // 20ms pre-delay
params.diffusion = 0.7f;    // Full diffusion
params.modRate = 0.5f;      // Subtle modulation
params.modDepth = 0.3f;
reverb.setParams(params);

// In audio callback:
reverb.processBlock(leftBuffer, rightBuffer, numSamples);

// Freeze mode:
params.freeze = true;
reverb.setParams(params);
// ... frozen texture sustains indefinitely ...
params.freeze = false;
reverb.setParams(params);

// Reset to silence:
reverb.reset();
```

## Key Design Decisions

1. **Header-only**: Consistent with all other Layer 4 effects in the codebase.
2. **Reuses existing primitives**: DelayLine, OnePoleLP, DCBlocker, SchroederAllpass, OnePoleSmoother.
3. **Lightweight LFO**: Manual phase accumulation + `std::sin()` instead of the full LFO class.
4. **SchroederAllpass for all allpasses**: Including modulated stages (SchroederAllpass uses `readLinear()` internally, which is the correct interpolation for modulated delays per FR-019).
5. **10ms parameter smoothing**: Click-free transitions via OnePoleSmoother.

## Dependencies (All Existing)

| Component | Header | Layer |
|-----------|--------|-------|
| DelayLine | `<krate/dsp/primitives/delay_line.h>` | 1 |
| OnePoleLP | `<krate/dsp/primitives/one_pole.h>` | 1 |
| DCBlocker | `<krate/dsp/primitives/dc_blocker.h>` | 1 |
| SchroederAllpass | `<krate/dsp/primitives/comb_filter.h>` | 1 |
| OnePoleSmoother | `<krate/dsp/primitives/smoother.h>` | 1 |
| math_constants | `<krate/dsp/core/math_constants.h>` | 0 |
| db_utils | `<krate/dsp/core/db_utils.h>` | 0 |
