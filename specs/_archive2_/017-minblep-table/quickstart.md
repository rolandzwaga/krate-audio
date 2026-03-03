# Quickstart: MinBLEP Table

**Spec**: 017-minblep-table | **Date**: 2026-02-04

## What is Being Built

A precomputed minimum-phase band-limited step (minBLEP) function table at Layer 1 (`primitives/minblep_table.h`) in the `Krate::DSP` namespace. This is Phase 4 of the oscillator roadmap, providing the foundation for Phase 5 (Sync Oscillator).

## Files to Create

| File | Purpose |
|------|---------|
| `dsp/include/krate/dsp/primitives/minblep_table.h` | Header-only implementation |
| `dsp/tests/unit/primitives/minblep_table_test.cpp` | Catch2 unit tests |

## Files to Modify

| File | Change |
|------|--------|
| `dsp/tests/CMakeLists.txt` | Add `unit/primitives/minblep_table_test.cpp` to `dsp_tests` target |
| `specs/_architecture_/layer-1-primitives.md` | Add MinBlepTable documentation section |

## Dependencies (All Already Exist)

| Dependency | Include Path | Layer |
|------------|-------------|-------|
| `FFT`, `Complex` | `<krate/dsp/primitives/fft.h>` | 1 |
| `Window::generateBlackman()` | `<krate/dsp/core/window_functions.h>` | 0 |
| `Interpolation::linearInterpolate()` | `<krate/dsp/core/interpolation.h>` | 0 |
| `kPi`, `kTwoPi` | `<krate/dsp/core/math_constants.h>` | 0 |
| `detail::isNaN()`, `detail::isInf()` | `<krate/dsp/core/db_utils.h>` | 0 |

## Build & Test Commands

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Configure
"$CMAKE" --preset windows-x64-release

# Build DSP tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests

# Run all DSP tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe

# Run only MinBLEP tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[minblep]"
```

## Key API Usage

### Generate Table (init-time, NOT real-time safe)

```cpp
#include <krate/dsp/primitives/minblep_table.h>

Krate::DSP::MinBlepTable table;
table.prepare();  // Default: 64x oversampling, 8 zero crossings
// table.length() == 16 (= 8 * 2)
// table.isPrepared() == true
```

### Query Table (real-time safe)

```cpp
// sample(subsampleOffset, index)
float val0 = table.sample(0.0f, 0);       // Near 0.0 (start of step)
float val15 = table.sample(0.0f, 15);     // Exactly 1.0 (settled)
float valBeyond = table.sample(0.0f, 20); // Exactly 1.0 (beyond table)
float interp = table.sample(0.5f, 5);     // Interpolated sub-sample value
```

### Apply Corrections via Residual (real-time safe)

```cpp
Krate::DSP::MinBlepTable::Residual residual(table);

// When a discontinuity occurs (e.g., hard sync reset):
float subsampleOffset = 0.3f;  // Where in the sample the reset happened
float amplitude = -1.0f;       // Height of the discontinuity
residual.addBlep(subsampleOffset, amplitude);

// Each sample thereafter:
for (size_t i = 0; i < blockSize; ++i) {
    float naive = generateNaiveOutput();  // Includes hard discontinuity
    output[i] = naive + residual.consume();  // Band-limited correction
}
```

### Shared Table, Independent Residuals (polyphonic)

```cpp
// One table, multiple voices
Krate::DSP::MinBlepTable sharedTable;
sharedTable.prepare();

Krate::DSP::MinBlepTable::Residual voice1(sharedTable);
Krate::DSP::MinBlepTable::Residual voice2(sharedTable);
// voice1 and voice2 operate independently
```

## Test Strategy

Tests follow the Catch2 pattern used in `fft_test.cpp` and `polyblep_oscillator_test.cpp`.

| Test Category | Tag | What It Verifies |
|---------------|-----|-----------------|
| Table generation | `[minblep][US1]` | Length, boundaries, step function properties |
| Table query | `[minblep][US2]` | Interpolation, clamping, edge cases |
| Residual buffer | `[minblep][US3]` | addBlep, consume, reset, accumulation |
| Shared table | `[minblep][US4]` | Independent residuals from shared table |
| Custom parameters | `[minblep][US5]` | Different oversampling/zeroCrossings values |
| Minimum-phase property | `[minblep][US1]` | Energy front-loading (SC-011) |
| Alias rejection | `[minblep][SC012]` | FFT analysis of corrected sawtooth (SC-012) |
| Robustness | `[minblep][edge]` | NaN, infinity, out-of-range, pre-prepare |
| Numerical stability | `[minblep][edge]` | 10,000 random calls with no NaN/Inf (SC-014) |

## Implementation Order

1. **Tests first**: Write failing tests covering SC-001 through SC-015
2. **MinBlepTable class shell**: Default constructor, length(), isPrepared(), sample() stubs
3. **prepare() algorithm**: Windowed sinc, minimum-phase transform, integration, normalization
4. **sample() method**: Polyphase lookup with linear interpolation
5. **Residual struct**: Ring buffer with addBlep(), consume(), reset()
6. **Edge cases**: Invalid parameters, NaN/Inf safety, pre-prepare state
7. **Build, verify zero warnings, run all tests**
8. **Architecture docs update**

## Key Design Decisions

1. **Header-only**: Matches pattern of all other Layer 0/1 components in this codebase
2. **Flat polyphase storage**: `table_[index * oversamplingFactor + subIndex]` for cache locality
3. **Non-owning Residual pointer**: Same pattern as WavetableOscillator -> WavetableData
4. **Self-extinguishing residual**: Formula `amplitude * (table[i] - 1.0)` naturally decays to zero
5. **Linear interpolation**: Sufficient given 64x oversampling (adjacent entries very close)
6. **No power-of-2 enforcement**: Default length is 16 (already power of 2); non-standard configs use standard modulo
