# Quickstart: Modulation System Implementation

**Feature**: 008-modulation-system
**Date**: 2026-01-29
**Estimated Tasks**: T9.1-T9.27 per roadmap.md

---

## Prerequisites

Before starting implementation, verify:
1. Branch `008-modulation-system` exists (created by setup script)
2. Build passes: `cmake --preset windows-x64-release && cmake --build build/windows-x64-release --config Release`
3. All existing tests pass: `ctest --test-dir build/windows-x64-release -C Release --output-on-failure`

---

## Implementation Order

### Phase 1: Layer 0 - Types and Curves (T9.1-T9.3)

**Step 1.1: Create modulation types**

File: `dsp/include/krate/dsp/core/modulation_types.h`

```cpp
// ModSource enum (13 values)
// ModCurve enum (4 values)
// ModRouting struct (source, destParamId, amount, curve, active)
// MacroConfig struct (value, min, max, curve)
// EnvFollowerSourceType enum
// SampleHoldInputType enum
// Constants: kModSourceCount, kModCurveCount, kMaxModRoutings, kMaxMacros
```

Test file: `dsp/tests/unit/core/modulation_types_test.cpp`
- Verify enum value counts
- Verify ModRouting default construction
- Verify amount clamping behavior

**Step 1.2: Extract ModulationSource interface to Layer 0**

File: `dsp/include/krate/dsp/core/modulation_source.h`

Extract the `ModulationSource` abstract class from `modulation_matrix.h` to Layer 0 so Layer 2 processors can implement it. Update `modulation_matrix.h` to include from the new location.

Note: This is a non-breaking refactor. Existing tests must still pass.

**Step 1.3: Create modulation curve functions**

File: `dsp/include/krate/dsp/core/modulation_curves.h`

```cpp
float applyModCurve(ModCurve curve, float x) noexcept;
float applyBipolarModulation(ModCurve curve, float sourceValue, float amount) noexcept;
```

Test file: `dsp/tests/unit/core/modulation_curves_test.cpp`
- Test each curve at positions 0.0, 0.25, 0.5, 0.75, 1.0 (SC-003)
- Test bipolar handling: verify -100% inverts +100% (SC-004)
- Test edge cases: NaN, infinity, out-of-range input

Build and verify: All existing tests still pass after Layer 0 changes.

---

### Phase 2: Layer 2 - Modulation Sources (T9.10-T9.16)

Each source follows the same pattern:
1. Write failing tests
2. Implement to pass tests
3. Build, fix warnings
4. Verify all tests pass

**Step 2.1: RandomSource** (simplest, good warm-up)

File: `dsp/include/krate/dsp/processors/random_source.h`
Test: `dsp/tests/unit/processors/random_source_test.cpp`

Key tests:
- Output stays in [-1, +1]
- Rate controls how often value changes
- Smoothness parameter smooths transitions
- Statistical distribution (SC-016)

**Step 2.2: TransientDetector**

File: `dsp/include/krate/dsp/processors/transient_detector.h`
Test: `dsp/tests/unit/processors/transient_detector_test.cpp`

Key tests:
- Fires on >12dB step input within 2ms (SC-009)
- Does NOT fire on steady-state signal (FR-092)
- Retrigger from current level (FR-053)
- Attack/decay timing accuracy

**Step 2.3: ChaosModSource**

File: `dsp/include/krate/dsp/processors/chaos_mod_source.h`
Test: `dsp/tests/unit/processors/chaos_mod_source_test.cpp`

Key tests:
- Output stays in [-1, +1] after 10 seconds for all 4 models (SC-007)
- Speed parameter affects evolution rate
- Coupling perturbs attractor from audio input
- Model switch resets state correctly

**Step 2.4: SampleHoldSource**

File: `dsp/include/krate/dsp/processors/sample_hold_source.h`
Test: `dsp/tests/unit/processors/sample_hold_source_test.cpp`

Key tests:
- Holds value between samples
- Rate controls sampling frequency
- Slew smooths transitions (SC-017)
- All 4 input sources work correctly

**Step 2.5: PitchFollowerSource**

File: `dsp/include/krate/dsp/processors/pitch_follower_source.h`
Test: `dsp/tests/unit/processors/pitch_follower_source_test.cpp`

Key tests:
- Maps 440Hz to expected value within 5% (SC-008)
- Min/Max Hz range works correctly
- Holds last value when confidence is low
- Tracking speed smooths output

---

### Phase 3: Layer 3 - ModulationEngine (T9.17-T9.21)

**Step 3.1: Create ModulationEngine**

File: `dsp/include/krate/dsp/systems/modulation_engine.h`
Test: `dsp/tests/unit/systems/modulation_engine_test.cpp`

Implement the routing loop:
```
For each block:
  1. Update tempo from BlockContext
  2. Process audio-dependent sources per-sample
  3. Process all other sources per-block
  4. For each active routing:
     a. Get source value
     b. Apply abs() + curve
     c. Multiply by smoothed amount
     d. Accumulate to destination offset
  5. Clamp each offset to [-1, +1]
```

Key tests:
- Single routing: LFO to destination (FR-085)
- Bipolar: negative amount inverts (SC-004, FR-086)
- Multi-source summation with clamping (SC-005, FR-087)
- All 4 curves produce correct output (FR-088)
- 32 routings can be active simultaneously (FR-004)
- Real-time safety: no allocations in process

**Step 3.2: Integration test**

File: `dsp/tests/integration/modulation_integration_test.cpp`

Key test:
- LFO to Morph X produces expected position changes over time (FR-093)
- Envelope Follower responds to level changes (FR-089)

---

### Phase 4: Plugin Integration (T9.22-T9.24)

**Step 4.1: Parameter IDs**

File: `plugins/Disrumpo/src/plugin_ids.h`

Add all 178 parameter IDs (200-445) per the spec parameter table.

**Step 4.2: Processor integration**

File: `plugins/Disrumpo/src/processor/processor.cpp`

```cpp
// In initialize():
modulationEngine_.prepare(processSetup.sampleRate, processSetup.maxSamplesPerBlock);

// In process():
modulationEngine_.process(blockCtx, inputL, inputR, numSamples);
// Apply modulated values to bands
float modulatedMorphX = modulationEngine_.getModulatedValue(kBand0MorphXId, baseMorphX);
```

**Step 4.3: Controller integration**

File: `plugins/Disrumpo/src/controller/controller.cpp`

Register all modulation parameters with appropriate ranges, defaults, and display names.

**Step 4.4: UI panels**

File: `plugins/Disrumpo/resources/editor.uidesc`

Add:
- Sources panel with controls for all 12 sources (FR-065 to FR-073)
- Routing matrix panel (FR-074 to FR-078)
- Macros panel (FR-079 to FR-081)

---

### Phase 5: Testing and Validation (T9.25-T9.27)

1. Run all unit tests
2. Run all integration tests
3. Run pluginval: `tools/pluginval.exe --strictness-level 5 --validate "build/VST3/Release/Disrumpo.vst3"`
4. Run clang-tidy: `./tools/run-clang-tidy.ps1 -Target all`
5. Fill compliance table in spec.md

---

## Key Implementation Patterns

### Source Processing Pattern

All sources follow this pattern:
```cpp
class MySource : public ModulationSource {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    void process() noexcept;  // or process(float sample) for audio-dependent

    [[nodiscard]] float getCurrentValue() const noexcept override {
        return currentValue_;
    }

    [[nodiscard]] std::pair<float, float> getSourceRange() const noexcept override {
        return {-1.0f, 1.0f};  // or {0.0f, 1.0f} for unipolar
    }

private:
    float currentValue_ = 0.0f;
};
```

### Bipolar Modulation Application

```cpp
// Per spec FR-059
float rawSource = source.getCurrentValue();       // [-1, +1]
float absSource = std::abs(rawSource);             // [0, +1]
float curved = applyModCurve(routing.curve, absSource);  // [0, +1]
float output = curved * routing.amount;            // amount carries sign
```

### Multi-Source Summation

```cpp
// Per spec FR-060, FR-061, FR-062
float offset = 0.0f;
for (const auto& routing : routings_) {
    if (routing.active && routing.destParamId == destId) {
        offset += applyBipolarModulation(routing.curve,
                                          getRawSourceValue(routing.source),
                                          routing.amount);
    }
}
offset = std::clamp(offset, -1.0f, 1.0f);         // FR-061
float finalValue = std::clamp(base + offset, 0.0f, 1.0f);  // FR-062
```

### LFO Unipolar Conversion

```cpp
// Per spec FR-013
float lfoOutput = lfo_.process();  // [-1, +1]
if (lfoUnipolar_) {
    lfoOutput = (lfoOutput + 1.0f) * 0.5f;  // [0, +1]
}
```

### Macro Processing

```cpp
// Per spec FR-028, FR-029
float mapped = macro.minOutput + macro.value * (macro.maxOutput - macro.minOutput);
float output = applyModCurve(macro.curve, mapped);
```

---

## Build Commands

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Configure
"$CMAKE" --preset windows-x64-release

# Build DSP tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests

# Run DSP tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe

# Build all
"$CMAKE" --build build/windows-x64-release --config Release

# Run all tests
ctest --test-dir build/windows-x64-release -C Release --output-on-failure
```

---

## Files to Create (Summary)

| File | Layer | Purpose |
|------|-------|---------|
| `dsp/include/krate/dsp/core/modulation_types.h` | 0 | Enums and structs |
| `dsp/include/krate/dsp/core/modulation_source.h` | 0 | Interface (extracted from modulation_matrix.h) |
| `dsp/include/krate/dsp/core/modulation_curves.h` | 0 | Curve functions |
| `dsp/include/krate/dsp/processors/random_source.h` | 2 | Random source |
| `dsp/include/krate/dsp/processors/transient_detector.h` | 2 | Transient detector |
| `dsp/include/krate/dsp/processors/chaos_mod_source.h` | 2 | Chaos source |
| `dsp/include/krate/dsp/processors/sample_hold_source.h` | 2 | S&H source |
| `dsp/include/krate/dsp/processors/pitch_follower_source.h` | 2 | Pitch follower |
| `dsp/include/krate/dsp/systems/modulation_engine.h` | 3 | Engine |
| `dsp/tests/unit/core/modulation_types_test.cpp` | - | Type tests |
| `dsp/tests/unit/core/modulation_curves_test.cpp` | - | Curve tests |
| `dsp/tests/unit/processors/random_source_test.cpp` | - | Random tests |
| `dsp/tests/unit/processors/transient_detector_test.cpp` | - | Transient tests |
| `dsp/tests/unit/processors/chaos_mod_source_test.cpp` | - | Chaos tests |
| `dsp/tests/unit/processors/sample_hold_source_test.cpp` | - | S&H tests |
| `dsp/tests/unit/processors/pitch_follower_source_test.cpp` | - | Pitch tests |
| `dsp/tests/unit/systems/modulation_engine_test.cpp` | - | Engine tests |
| `dsp/tests/integration/modulation_integration_test.cpp` | - | Integration tests |

**Files to Modify:**
| File | Change |
|------|--------|
| `dsp/include/krate/dsp/systems/modulation_matrix.h` | Include modulation_source.h from Layer 0 instead of defining inline |
| `plugins/Disrumpo/src/plugin_ids.h` | Add parameter IDs 200-445 |
| `plugins/Disrumpo/src/processor/processor.cpp` | Integrate ModulationEngine |
| `plugins/Disrumpo/src/controller/controller.cpp` | Register modulation parameters |
| `plugins/Disrumpo/resources/editor.uidesc` | Add modulation UI panels |
| CMakeLists.txt (dsp) | Add new source files to build |
