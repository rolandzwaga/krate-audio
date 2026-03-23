# Quickstart: Body Resonance (Spec 131)

## What This Feature Does

Adds a post-resonator body coloring stage to the Innexus physical model. Takes the output of the modal or waveguide resonator and applies the resonant character of an instrument body (guitar, violin, cello) using a hybrid modal bank + FDN architecture. Controlled by three parameters: body size, material, and mix.

## Key Files

### New Files

| File | Purpose |
|------|---------|
| `dsp/include/krate/dsp/processors/body_resonance.h` | Main DSP processor (header-only) |
| `dsp/tests/unit/processors/body_resonance_tests.cpp` | Unit tests for BodyResonance |
| `plugins/innexus/tests/unit/processor/body_resonance_integration_tests.cpp` | Integration tests |

### Modified Files

| File | Change |
|------|--------|
| `plugins/innexus/src/plugin_ids.h` | Add kBodySizeId (850), kBodyMaterialId (851), kBodyMixId (852) |
| `plugins/innexus/src/processor/innexus_voice.h` | Add `BodyResonance bodyResonance` field |
| `plugins/innexus/src/processor/processor.cpp` | Call bodyResonance.process() after physicalSample, before PhysicalModelMixer |
| `plugins/innexus/src/processor/processor.h` | Add atomic params for body size/material/mix |
| `plugins/innexus/src/processor/processor_params.cpp` | Handle body parameter changes |
| `plugins/innexus/src/processor/processor_state.cpp` | Save/load body parameters |
| `plugins/innexus/src/controller/controller.cpp` | Register 3 body parameters |
| `dsp/tests/unit/CMakeLists.txt` | Add body_resonance_tests.cpp |
| `specs/_architecture_/layer-2-processors.md` | Document BodyResonance component |

## Signal Flow Integration Point

In `processor.cpp`, the body resonance inserts between the resonator output and the PhysicalModelMixer:

```cpp
// BEFORE (current code, ~line 1747):
float physicalSample = v.modalResonator.processSample(excitation, v.chokeDecayScale_);

// AFTER (with body resonance):
float physicalSample = v.modalResonator.processSample(excitation, v.chokeDecayScale_);
physicalSample = v.bodyResonance.process(physicalSample);  // NEW
```

The same applies to the waveguide path and crossfade path.

## Build and Test

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Build DSP tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests

# Run body resonance unit tests
build/windows-x64-release/bin/Release/dsp_tests.exe "BodyResonance*"

# Build and run Innexus integration tests
"$CMAKE" --build build/windows-x64-release --config Release --target innexus_tests
build/windows-x64-release/bin/Release/innexus_tests.exe "BodyResonance*"

# Full pluginval validation
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Innexus.vst3"
```

## Architecture at a Glance

```
BodyResonance (Layer 2, header-only)
  Dependencies:
    - Biquad (Layer 1) -- coupling filter, modal bank, radiation HPF
    - OnePoleSmoother (Layer 1) -- parameter smoothing
    - constexpr preset data (Layer 0) -- modal reference tuples

  Internal signal chain:
    input -> coupling EQ (2 biquads)
          -> first-order crossover LP -> 8 parallel modal biquads -> sum
          -> first-order crossover HP -> 4-line Hadamard FDN -> sum
          -> LP output + HP output (complementary recombination)
          -> radiation HPF (1 biquad, 12dB/oct)
          -> dry/wet mix
          -> output
```
