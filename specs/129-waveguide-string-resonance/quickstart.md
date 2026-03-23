# Quickstart: Waveguide String Resonance

**Feature Branch**: `129-waveguide-string-resonance`

---

## What This Feature Does

Adds a digital waveguide string resonator as an alternative to the existing modal resonator bank in Innexus. The waveguide produces plucked/struck string timbres (guitar, harp, hammered dulcimer) via a delay-line feedback loop with Karplus-Strong/EKS foundations. Users can switch between Modal and Waveguide resonance types with a click-free crossfade.

## Key Files to Create

| File | Layer | Purpose |
|------|-------|---------|
| `dsp/include/krate/dsp/processors/iresonator.h` | 2 | Shared interface for resonator types |
| `dsp/include/krate/dsp/processors/waveguide_string.h` | 2 | Core waveguide string processor |
| `dsp/tests/unit/processors/waveguide_string_test.cpp` | test | Unit tests (pitch accuracy, passivity, energy) |

## Key Files to Modify

| File | Change |
|------|--------|
| `plugins/innexus/src/plugin_ids.h` | Add kResonanceTypeId, kWaveguideStiffnessId, kWaveguidePickPositionId |
| `plugins/innexus/src/processor/innexus_voice.h` | Add WaveguideString member, crossfade state |
| `plugins/innexus/src/processor/processor.cpp` | Route resonance type, handle crossfade in render loop |
| `plugins/innexus/src/processor/processor_midi.cpp` | Call waveguideString.noteOn() on note events |
| `plugins/innexus/src/processor/processor_params.cpp` | Handle new parameter IDs |
| `plugins/innexus/src/processor/processor_state.cpp` | Save/load new parameters |
| `plugins/innexus/src/controller/controller.cpp` | Register new parameters |
| `plugins/innexus/src/parameters/innexus_params.h` | Parameter registration helpers |
| `dsp/include/krate/dsp/processors/modal_resonator_bank.h` | Add IResonator interface methods + energy followers |
| `plugins/innexus/src/dsp/physical_model_mixer.h` | Extend for resonance type crossfade |
| `dsp/CMakeLists.txt` | Add new source files if needed |
| `plugins/innexus/CMakeLists.txt` | No changes expected (header-only additions) |

## Architecture Overview

```
Per Voice:
                                  ┌─────────────────────┐
  Excitation ──────────────────── │                     │
  (Impact/Residual/NoiseBurst)    │   IResonator        │
                                  │                     │
                    ┌─────────────┤  if type == Modal:  │
                    │             │    modalResonator    │
                    │             │  if type == Waveguide│
                    │             │    waveguideString   │
                    │             └─────────────────────┘
                    │                      │
                    │  (crossfade during   │
                    │   type switch)       │
                    v                      v
              physicalSignal ──> PhysicalModelMixer ──> voice output
```

## Signal Flow (WaveguideString internal)

```
excitation ──> (+) ──> soft clip ──> [delay line] ──> [dispersion x4]
                ^                                          |
                │◄── output (velocity wave)                v
                |                                    [tuning allpass]
                |                                          |
                |                                          v
                └──── DC blocker <──── loss filter <───────┘
```

Output is tapped after the summing junction (FR-038), not after the DC blocker.

## Build & Test

```bash
# Build DSP library and tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests

# Run waveguide string tests only
build/windows-x64-release/bin/Release/dsp_tests.exe "WaveguideString*"

# Build and test Innexus plugin
"$CMAKE" --build build/windows-x64-release --config Release --target innexus_tests
build/windows-x64-release/bin/Release/innexus_tests.exe

# Validate plugin
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Innexus.vst3"
```

## Implementation Order

1. **IResonator interface** -- define the shared contract
2. **WaveguideString core** -- delay loop, loss filter, tuning allpass, DC blocker
3. **Dispersion allpass** -- 4-section biquad cascade for inharmonicity
4. **Excitation system** -- noise burst, pick position comb, energy normalisation
5. **ModalResonatorBank adaptation** -- add IResonator methods + energy followers
6. **Plugin integration** -- parameters, voice engine, crossfade, state
7. **Pitch accuracy tests** -- autocorrelation/YIN verification
8. **Passivity and stability tests** -- loop gain verification, DC blocker, energy floor

## Critical Constraints

- **Stiffness frozen at note onset** (FR-010) -- no real-time modulation in Phase 3
- **Pick position frozen at note onset** (FR-015) -- matches real instruments
- **Velocity waves internally** (FR-013) -- not displacement
- **4 dispersion sections fixed** (FR-009) -- not configurable in Phase 3
- **8-voice polyphony** (FR-044) -- matches existing Innexus default
- **WaveguideResonator untouched** -- new class, no refactoring of existing
