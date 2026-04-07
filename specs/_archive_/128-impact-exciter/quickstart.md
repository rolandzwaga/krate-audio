# Quickstart: Impact Exciter (Spec 128)

## What This Feature Does

Adds a physical-modelling impact exciter to the Innexus plugin that generates percussive attack signals (mallet strikes, plucks) from MIDI input. The excitation signal feeds the existing ModalResonatorBank to create struck-object sounds from any analyzed timbre.

## Architecture at a Glance

```
MIDI Note-On
    |
    v
ImpactExciter.trigger(velocity, hardness, mass, brightness, position, f0)
    |
    v (per sample)
ImpactExciter.process()
    |  -- asymmetric pulse + shaped noise
    |  -- SVF lowpass filter
    |  -- strike position comb filter
    |  -- energy capping
    v
ModalResonatorBank.processSample(excitation, decayScale)
    |  -- decayScale from voice choke envelope
    v
PhysicalModelMixer.process(harmonic, residual, physical, mix)
    |
    v
Audio Output
```

## New Files

| File | Layer | Purpose |
|------|-------|---------|
| `dsp/include/krate/dsp/core/xorshift32.h` | 0 | Deterministic PRNG |
| `dsp/include/krate/dsp/processors/impact_exciter.h` | 2 | Impact exciter DSP |
| `dsp/tests/unit/core/xorshift32_test.cpp` | - | XorShift32 unit tests |
| `dsp/tests/unit/processors/impact_exciter_test.cpp` | - | ImpactExciter unit tests |

## Modified Files

| File | Change |
|------|--------|
| `plugins/innexus/src/plugin_ids.h` | Add kExciterTypeId (805), kImpactHardnessId (806-809), ExciterType enum |
| `plugins/innexus/src/processor/innexus_voice.h` | Add ImpactExciter member, choke state fields |
| `plugins/innexus/src/processor/processor.h` | Add atomic params for exciter type + impact settings |
| `plugins/innexus/src/processor/processor.cpp` | Voice loop exciter type switch, parameter routing |
| `plugins/innexus/src/processor/processor_midi.cpp` | Trigger impact exciter on note-on, choke on retrigger |
| `plugins/innexus/src/processor/processor_state.cpp` | Save/load new parameters |
| `plugins/innexus/src/controller/controller.cpp` | Register new parameters |
| `dsp/include/krate/dsp/processors/modal_resonator_bank.h` | Add decayScale overload to processSample() |
| `dsp/CMakeLists.txt` | (only if xorshift32.h needs explicit listing) |

## Key Design Decisions

1. **Hybrid API**: `process()` is the primary per-sample API; `processBlock()` wraps it for testing.
2. **Energy capping in exciter**: Exponential decay accumulator prevents energy explosion from rapid retrigger.
3. **Mallet choke in voice**: Voice owns choke envelope; resonator receives `decayScale` parameter.
4. **Inline comb filter**: Uses `DelayLine` directly rather than `FeedforwardComb` (needs negative gain).
5. **XorShift32 at Layer 0**: Reusable by future exciters (Bow, Phase 4).
6. **One-pole pinking**: Spec-defined `b=0.9f` formula, NOT the existing PinkNoiseFilter.

## Build & Test

```bash
# Build DSP tests (includes new impact_exciter_test.cpp)
cmake --build build/windows-x64-release --config Release --target dsp_tests
build/windows-x64-release/bin/Release/dsp_tests.exe "ImpactExciter*"

# Build Innexus plugin tests
cmake --build build/windows-x64-release --config Release --target innexus_tests
build/windows-x64-release/bin/Release/innexus_tests.exe

# Pluginval
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Innexus.vst3"
```
