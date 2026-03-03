# Quickstart: Ring Modulator Distortion

**Feature**: 085-ring-mod-distortion
**Branch**: `085-ring-mod-distortion`

## What This Feature Does

Adds a Ring Modulator as a new distortion type to the Ruinae synthesizer plugin. Ring modulation multiplies the voice signal by an internal carrier oscillator, producing sum and difference frequency sidebands that create metallic, bell-like, and inharmonic timbres.

## Files to Create

| File | Description |
|------|-------------|
| `dsp/include/krate/dsp/processors/ring_modulator.h` | RingModulator Layer 2 processor (header-only) |
| `dsp/tests/unit/processors/ring_modulator_test.cpp` | Unit tests for RingModulator |

## Files to Modify

| File | Change |
|------|--------|
| `plugins/ruinae/src/ruinae_types.h` | Add `RingModulator` to `RuinaeDistortionType` enum |
| `plugins/ruinae/src/plugin_ids.h` | Add 5 new parameter IDs (560-564) |
| `plugins/ruinae/src/parameters/distortion_params.h` | Add ring mod fields to struct, handler, register, save/load, format |
| `plugins/ruinae/src/parameters/dropdown_mappings.h` | Add "Ring Mod" to distortion type strings |
| `plugins/ruinae/src/engine/ruinae_voice.h` | Add ring mod instance, integration in distortion methods, note freq forwarding |
| `plugins/ruinae/src/engine/ruinae_engine.h` | Add ring mod parameter forwarding methods |
| `plugins/ruinae/src/processor/processor.cpp` | Add ring mod parameter dispatching |
| `plugins/ruinae/src/controller/controller.cpp` | (Already handled by distortion_params.h registration) |
| `dsp/tests/CMakeLists.txt` | Add ring_modulator_test.cpp |
| `specs/_architecture_/layer-2-processors.md` | Document RingModulator component |

## Build and Test Commands

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Build DSP tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests
build/windows-x64-release/bin/Release/dsp_tests.exe -c "RingModulator*"

# Build Ruinae plugin + tests
"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests
build/windows-x64-release/bin/Release/ruinae_tests.exe

# Pluginval
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"
```

## Implementation Order

1. **DSP Component** (Layer 2 processor): Create `RingModulator` class with Gordon-Smith sine, PolyBLEP complex waveforms, Noise carrier, frequency smoothing, mono/stereo processing
2. **DSP Tests**: Unit tests for all carrier waveforms, frequency modes, smoothing, stereo spread, edge cases, performance
3. **Plugin Integration**: Enum extension, parameter IDs, parameter infrastructure, voice integration, engine forwarding, processor dispatching
4. **Plugin Tests**: State round-trip, backward compatibility, parameter registration
5. **Validation**: Pluginval, clang-tidy, architecture docs

## Key Design Decisions

- **Sine carrier**: Gordon-Smith magic circle (inline, not shared with FrequencyShifter yet -- extract at 3rd user)
- **Complex carriers**: Reuse existing `PolyBlepOscillator` directly
- **Noise carrier**: Reuse existing `NoiseOscillator` with White fixed
- **Frequency smoothing**: `OnePoleSmoother` with 5ms time constant
- **No oversampling**: Ring modulation is linear multiplication, not waveshaping
- **Drive = carrier amplitude**: Linear 0-1 mapping (perceptually linear for ring mod)
- **Stereo spread**: +/-50 Hz max offset, no effect in current mono voice context
