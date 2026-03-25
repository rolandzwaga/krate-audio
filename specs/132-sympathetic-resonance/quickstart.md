# Quickstart: Sympathetic Resonance Implementation

**Feature Branch**: `132-sympathetic-resonance`
**Date**: 2026-03-25

## Overview

Add a global shared resonance field to the Innexus synthesizer that models sympathetic string vibrations. When multiple voices play, their harmonics naturally excite resonators tuned to shared partials, producing a shimmering, halo-like reinforcement on chords.

## Key Files to Create

| File | Layer | Purpose |
|------|-------|---------|
| `dsp/include/krate/dsp/systems/sympathetic_resonance.h` | Layer 3 | Main SympatheticResonance class (header-only scalar impl) |
| `dsp/include/krate/dsp/systems/sympathetic_resonance_simd.h` | Layer 3 | SIMD kernel public API (no Highway headers) |
| `dsp/include/krate/dsp/systems/sympathetic_resonance_simd.cpp` | Layer 3 | SIMD kernel implementation (Highway self-inclusion pattern) |
| `dsp/tests/unit/systems/sympathetic_resonance_test.cpp` | Test | Unit tests for the DSP component |
| `plugins/innexus/tests/unit/processor/sympathetic_resonance_integration_test.cpp` | Test | Integration tests for plugin-level integration |

## Key Files to Modify

| File | Change |
|------|--------|
| `plugins/innexus/src/plugin_ids.h` | Add `kSympatheticAmountId = 860`, `kSympatheticDecayId = 861` |
| `plugins/innexus/src/processor/processor.h` | Add `sympatheticResonance_` member, `sympatheticAmount_`, `sympatheticDecay_` atomics |
| `plugins/innexus/src/processor/processor.cpp` | Integrate into process() loop: post-voice-sum, pre-master-gain |
| `plugins/innexus/src/processor/processor_params.cpp` | Handle kSympatheticAmountId, kSympatheticDecayId |
| `plugins/innexus/src/processor/processor_state.cpp` | Save/load sympathetic parameters |
| `plugins/innexus/src/processor/processor_midi.cpp` | Call sympatheticResonance_.noteOn/noteOff |
| `plugins/innexus/src/controller/controller.cpp` | Register RangeParameter for Amount and Decay |
| `dsp/CMakeLists.txt` | Add sympathetic_resonance_simd.cpp to SOURCES |
| `dsp/tests/CMakeLists.txt` | Add test file |

## Signal Chain Integration Point

In `processor.cpp` process() per-sample loop (around line 1877):

```
BEFORE (current):
    voice accumulation -> polyphony gain comp -> crossfades -> master gain -> soft limiter

AFTER (with sympathetic resonance):
    voice accumulation -> polyphony gain comp -> crossfades
        -> sympatheticResonance_.process(mono_sum) -> add to sampleL/sampleR
        -> master gain -> soft limiter
```

The sympathetic input is the mono sum `(sampleL + sampleR) * 0.5f` after polyphony gain compensation. The sympathetic output is added to both channels equally (mono effect).

## Implementation Order (Scalar-First per Constitution IV)

1. **Phase 1**: `SympatheticResonance` class with scalar processing + full test suite
2. **Phase 2**: SIMD kernel (`sympathetic_resonance_simd.cpp`) behind same API
3. **Phase 3**: Plugin integration (parameters, MIDI events, signal chain)
4. **Phase 4**: Verification against all SC-xxx criteria

## Quick Build & Test

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Build DSP tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests
build/windows-x64-release/bin/Release/dsp_tests.exe "SympatheticResonance*"

# Build plugin tests
"$CMAKE" --build build/windows-x64-release --config Release --target innexus_tests
build/windows-x64-release/bin/Release/innexus_tests.exe "SympatheticResonance*"
```
