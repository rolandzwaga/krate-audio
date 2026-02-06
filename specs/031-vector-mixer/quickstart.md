# Quickstart: Vector Mixer

**Feature**: 031-vector-mixer | **Date**: 2026-02-06

## Files to Create

| File | Location | Description |
|------|----------|-------------|
| `stereo_output.h` | `dsp/include/krate/dsp/core/stereo_output.h` | Layer 0: Extracted StereoOutput struct |
| `vector_mixer.h` | `dsp/include/krate/dsp/systems/vector_mixer.h` | Layer 3: VectorMixer class (header-only) |
| `vector_mixer_tests.cpp` | `dsp/tests/unit/systems/vector_mixer_tests.cpp` | Unit tests |

## Files to Modify

| File | Change |
|------|--------|
| `dsp/include/krate/dsp/systems/unison_engine.h` | Replace local `StereoOutput` definition with `#include <krate/dsp/core/stereo_output.h>` |
| `dsp/CMakeLists.txt` | Add `include/krate/dsp/core/stereo_output.h` to `KRATE_DSP_CORE_HEADERS` and `include/krate/dsp/systems/vector_mixer.h` to `KRATE_DSP_SYSTEMS_HEADERS` |
| `dsp/tests/CMakeLists.txt` | Add `unit/systems/vector_mixer_tests.cpp` to `dsp_tests` target and `-fno-fast-math` list |
| `specs/_architecture_/layer-0-core.md` | Add StereoOutput entry |
| `specs/_architecture_/layer-3-systems.md` | Add VectorMixer entry |

## Implementation Order

### Task Group 0: Prerequisites (StereoOutput extraction)

1. Create `dsp/include/krate/dsp/core/stereo_output.h` with `StereoOutput` struct
2. Update `dsp/include/krate/dsp/systems/unison_engine.h` to include from new location, remove local definition
3. Update `dsp/CMakeLists.txt` to list new header
4. Build and verify all existing tests pass (no regression)
5. Commit

### Task Group 1: Core Weight Computation (FR-005, FR-006, FR-007, FR-008, FR-009, FR-010)

1. Write tests for square topology weights at corners and center (SC-001)
2. Write tests for diamond topology weights at cardinal points and center (SC-004)
3. Write tests for weight invariants (non-negative, sum constraints)
4. Implement `VectorMixer` with `computeSquareWeights()` and `computeDiamondWeights()`
5. Implement `setTopology()`, `setMixingLaw()` for Linear law
6. Build and verify tests pass
7. Commit

### Task Group 2: Mixing Laws (FR-011, FR-012)

1. Write tests for equal-power weights at center and corners (SC-002)
2. Write tests for square-root weights at center
3. Write tests for equal-power sum-of-squares invariant across 100 grid positions (SC-002)
4. Implement `applyMixingLaw()` for EqualPower and SquareRoot
5. Build and verify tests pass
6. Commit

### Task Group 3: Lifecycle and Mono Processing (FR-001, FR-002, FR-003, FR-004, FR-013, FR-014)

1. Write tests for `prepare()`, `reset()`, process-before-prepare safety
2. Write tests for `process()` with known DC inputs (US-1 scenarios)
3. Write tests for `processBlock()` correctness
4. Write tests for XY clamping (edge cases)
5. Implement `prepare()`, `reset()`, `setVectorX/Y/Position()`
6. Implement `process()` and `processBlock()` (mono)
7. Build and verify tests pass
8. Commit

### Task Group 4: Parameter Smoothing (FR-018, FR-019, FR-020)

1. Write tests for smoothing convergence at 10ms/44.1kHz (SC-005)
2. Write tests for instant response with 0ms smoothing (SC-007)
3. Write tests for independent X/Y smoothing
4. Write tests for `getWeights()` returning smoothed weights (FR-017, FR-020)
5. Implement smoothing coefficient computation and per-sample advancement
6. Build and verify tests pass
7. Commit

### Task Group 5: Stereo Processing (FR-015, FR-016)

1. Write tests for stereo `process()` with identical weights on both channels
2. Write tests for stereo `processBlock()`
3. Implement stereo `process()` and `processBlock()`
4. Build and verify tests pass
5. Commit

### Task Group 6: Thread Safety, Edge Cases, Performance (FR-023, FR-024, FR-025, FR-026)

1. Write tests for NaN/Inf input propagation (FR-025)
2. Write tests for 8192-sample block (SC-008)
3. Write tests for randomized XY sweep stability (SC-006)
4. Write CPU performance benchmark test (SC-003)
5. Verify all noexcept and no-allocation guarantees (FR-023)
6. Verify no trigonometric functions in per-sample path (FR-024)
7. Build and verify all tests pass
8. Commit

### Task Group 7: Documentation and Cleanup

1. Update `specs/_architecture_/layer-0-core.md` with StereoOutput entry
2. Update `specs/_architecture_/layer-3-systems.md` with VectorMixer entry
3. Run clang-tidy
4. Final build and full test run
5. Commit

## Build Commands

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Configure
"$CMAKE" --preset windows-x64-release

# Build DSP tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests

# Run DSP tests (vector mixer only)
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "VectorMixer*"

# Run all DSP tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe

# Run all tests via CTest
ctest --test-dir build/windows-x64-release -C Release --output-on-failure
```

## Quick Usage Example

```cpp
#include <krate/dsp/systems/vector_mixer.h>

Krate::DSP::VectorMixer mixer;
mixer.prepare(44100.0);

// Configure
mixer.setTopology(Krate::DSP::Topology::Square);
mixer.setMixingLaw(Krate::DSP::MixingLaw::Linear);
mixer.setSmoothingTimeMs(5.0f);

// Set position (thread-safe, can be called from UI/automation thread)
mixer.setVectorPosition(0.5f, -0.3f);

// Query weights for visual feedback
auto weights = mixer.getWeights();
// weights.a, weights.b, weights.c, weights.d

// Process mono audio
float output = mixer.process(oscA, oscB, oscC, oscD);

// Process stereo block
mixer.processBlock(aL, aR, bL, bR, cL, cR, dL, dR, outL, outR, numSamples);
```
