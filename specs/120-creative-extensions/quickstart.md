# Quickstart: Innexus M6 -- Creative Extensions

**Branch**: `120-creative-extensions` | **Date**: 2026-03-05

## Prerequisites

- M1-M5 fully implemented and passing all tests
- Build tools: CMake 3.20+, MSVC 2022 / Clang 15+ / GCC 10+
- Catch2 v3 (already in project)

## Build & Test

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Configure
"$CMAKE" --preset windows-x64-release

# Build all targets
"$CMAKE" --build build/windows-x64-release --config Release

# Run DSP tests (includes oscillator bank stereo/detune tests)
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests
build/windows-x64-release/bin/Release/dsp_tests.exe

# Run Innexus plugin tests (includes M6 integration tests)
"$CMAKE" --build build/windows-x64-release --config Release --target innexus_tests
build/windows-x64-release/bin/Release/innexus_tests.exe

# Pluginval validation
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Innexus.vst3"
```

## Implementation Order

### Phase 1: HarmonicOscillatorBank Stereo + Detune (Layer 2, KrateDSP)

**Files to modify**:
- `dsp/include/krate/dsp/processors/harmonic_oscillator_bank.h` -- add stereo arrays, processStereo(), setStereoSpread(), setDetuneSpread()

**Files to create**:
- `dsp/tests/unit/processors/test_harmonic_oscillator_bank_stereo.cpp` -- stereo output tests

**Key steps**:
1. Write failing tests for `processStereo()` at spread=0 (mono), spread=1.0 (odd/even split)
2. Add `panLeft_`, `panRight_`, `panPosition_`, `detuneMultiplier_` SoA arrays
3. Implement `processStereo(float& left, float& right)` with per-partial pan coefficients
4. Implement `setStereoSpread(float)` and `setDetuneSpread(float)`
5. Verify SC-010 (stereo == mono at spread=0)

### Phase 2: Cross-Synthesis Timbral Blend (Processor)

**Files to modify**:
- `plugins/innexus/src/processor/processor.h` -- add timbralBlend atomic, smoother, pure reference
- `plugins/innexus/src/processor/processor.cpp` -- blend logic before oscillator bank
- `plugins/innexus/src/plugin_ids.h` -- add kTimbralBlendId = 600
- `plugins/innexus/src/controller/controller.cpp` -- register parameter

**Files to create**:
- `plugins/innexus/tests/unit/processor/test_cross_synthesis.cpp`

### Phase 3: Evolution Engine (Plugin-local DSP)

**Files to create**:
- `plugins/innexus/src/dsp/evolution_engine.h`
- `plugins/innexus/tests/unit/processor/test_evolution_engine.cpp`

**Files to modify**:
- `plugins/innexus/src/processor/processor.h` -- add EvolutionEngine member
- `plugins/innexus/src/processor/processor.cpp` -- integrate into pipeline
- `plugins/innexus/src/plugin_ids.h` -- add evolution parameter IDs 602-605
- `plugins/innexus/src/controller/controller.cpp` -- register parameters

### Phase 4: Harmonic Modulators (Plugin-local DSP)

**Files to create**:
- `plugins/innexus/src/dsp/harmonic_modulator.h`
- `plugins/innexus/tests/unit/processor/test_harmonic_modulator.cpp`

**Files to modify**:
- `plugins/innexus/src/processor/processor.h` -- add 2x HarmonicModulator members
- `plugins/innexus/src/processor/processor.cpp` -- integrate into pipeline
- `plugins/innexus/src/plugin_ids.h` -- add modulator parameter IDs 610-626
- `plugins/innexus/src/controller/controller.cpp` -- register parameters

### Phase 5: Multi-Source Blending (Plugin-local DSP)

**Files to create**:
- `plugins/innexus/src/dsp/harmonic_blender.h`
- `plugins/innexus/tests/unit/processor/test_harmonic_blender.cpp`

**Files to modify**:
- `plugins/innexus/src/processor/processor.h` -- add HarmonicBlender member
- `plugins/innexus/src/processor/processor.cpp` -- integrate into pipeline
- `plugins/innexus/src/plugin_ids.h` -- add blend parameter IDs 640-649
- `plugins/innexus/src/controller/controller.cpp` -- register parameters

### Phase 6: State Persistence + Integration

**Files to modify**:
- `plugins/innexus/src/processor/processor.cpp` -- getState()/setState() v6

**Files to create/modify**:
- `plugins/innexus/tests/unit/vst/test_state_v6.cpp` -- state round-trip tests

## Key Patterns

### Adding processStereo() to HarmonicOscillatorBank

```cpp
// In harmonic_oscillator_bank.h, add alongside existing process():
void processStereo(float& left, float& right) noexcept {
    if (!prepared_ || !frameLoaded_) {
        left = right = 0.0f;
        return;
    }
    float sumL = 0.0f, sumR = 0.0f;
    // ... same MCF loop as process() but:
    // sumL += s * currentAmplitude_[i] * panLeft_[i];
    // sumR += s * currentAmplitude_[i] * panRight_[i];
}
```

### Applying Timbral Blend in Processor Pipeline

```cpp
// After frame selection, before filter:
if (timbralBlend < 1.0f) {
    // Blend current model with pure harmonic reference
    currentFrame = lerpHarmonicFrame(pureHarmonicFrame_, currentFrame, timbralBlend);
}
```

### Evolution Engine Usage

```cpp
// In process(), after source selection:
if (evolutionEnabled && !blendEnabled) {
    float pos = evolutionEngine_.getPosition();
    // Map position to waypoint pair + local t
    // Use lerpHarmonicFrame() between adjacent waypoints
    evolutionEngine_.advance(); // per-sample phase update
}
```

## Testing Checklist

- [ ] Stereo spread=0 produces identical L/R (SC-010)
- [ ] Stereo spread=1 separates odd/even partials (SC-002)
- [ ] Timbral blend=0 produces pure harmonics
- [ ] Timbral blend=1 matches source model (SC-001)
- [ ] Evolution produces spectral centroid variation (SC-003)
- [ ] Modulator produces measurable amplitude modulation (SC-004)
- [ ] Detune spread preserves fundamental pitch (SC-005)
- [ ] Multi-source blend at equal weights = mean of centroids (SC-006)
- [ ] All parameter sweeps are click-free (SC-007)
- [ ] Total M6 CPU < 1% additional (SC-008)
- [ ] State v5 loads with M6 defaults (SC-009)
- [ ] Single-source blend = direct recall (SC-011)
