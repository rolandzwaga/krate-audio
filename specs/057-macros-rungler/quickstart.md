# Quickstart: Macros & Rungler UI Exposure

**Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md) | **Date**: 2026-02-15

## What This Spec Does

Exposes Macro 1-4 knobs and Rungler configuration as VST parameters, integrates the Rungler DSP class into the ModulationEngine, and populates the existing empty mod source dropdown views with functional UI controls. After this spec, users can:

1. Select "Macros" from the mod source dropdown and adjust four knobs (M1-M4) that feed into the mod matrix as sources.
2. Select "Rungler" from the mod source dropdown and configure the Benjolin-inspired chaotic oscillator (Osc1 Freq, Osc2 Freq, Depth, Filter, Bits, Loop Mode).
3. Route the Rungler as a modulation source in the mod matrix.
4. Save/load presets with all 10 new parameters.
5. Automate all parameters from their DAW.

## Files Changed

### New Files (2)

| File | Purpose |
|------|---------|
| `plugins/ruinae/src/parameters/macro_params.h` | MacroParams struct + register/handle/format/save/load functions |
| `plugins/ruinae/src/parameters/rungler_params.h` | RunglerParams struct + register/handle/format/save/load functions |

### Modified Files - DSP Layer (2)

| File | Change |
|------|--------|
| `dsp/include/krate/dsp/core/modulation_types.h` | Insert `ModSource::Rungler = 10`, renumber SampleHold/PitchFollower/Transient, update `kModSourceCount` 13->14 |
| `dsp/include/krate/dsp/systems/modulation_engine.h` | Add `Rungler` field, setter methods, integrate in prepare/reset/process/getRawSourceValue |

### Modified Files - Plugin Layer (7)

| File | Change |
|------|--------|
| `plugins/ruinae/src/plugin_ids.h` | Add macro (2000-2003) and rungler (2100-2105) param IDs, update `kNumParameters` 2000->2200 |
| `plugins/ruinae/src/parameters/dropdown_mappings.h` | Insert "Rungler" in `kModSourceStrings` at index 10 |
| `plugins/ruinae/src/engine/ruinae_engine.h` | Add 6 rungler setter methods forwarding to `globalModEngine_` |
| `plugins/ruinae/src/processor/processor.h` | Add `MacroParams` and `RunglerParams` fields |
| `plugins/ruinae/src/processor/processor.cpp` | Wire params in processParameterChanges, applyParamsToEngine, getState (v13), setState (v13 + migration) |
| `plugins/ruinae/src/controller/controller.cpp` | Register params in initialize, sync in setComponentState (v13 + migration), format in getParamStringByValue |
| `plugins/shared/src/ui/mod_matrix_types.h` | Update `kNumGlobalSources` 12->13 |

### Modified Files - UI (1)

| File | Change |
|------|--------|
| `plugins/ruinae/resources/editor.uidesc` | Add 10 control-tags, populate ModSource_Macros and ModSource_Rungler templates |

### Modified Files - Tests (1)

| File | Change |
|------|--------|
| `plugins/ruinae/tests/integration/mod_matrix_grid_test.cpp` | Update `kNumGlobalSources` assertion 12->13 |

## Implementation Order

The 9 task groups should be implemented in this order:

1. **Parameter IDs and Enum Changes** (TG1) - Foundation for everything else
2. **ModulationEngine Rungler Integration** (TG2) - DSP integration
3. **RuinaeEngine Forwarding** (TG3) - Plugin-to-DSP bridge
4. **Parameter Files** (TG4) - New param structs
5. **Processor Wiring** (TG5) - Connect params to engine
6. **Preset Migration** (TG6) - Backward compatibility
7. **Controller Registration** (TG7) - UI param registration
8. **UI Templates** (TG8) - Visual controls
9. **Architecture Docs** (TG9) - Documentation update

## Key Design Decisions

1. **Rungler frequency range**: UI uses 0.1-100 Hz (modulation range), not full DSP range (0.1-20000 Hz). Defaults are 2.0/3.0 Hz (0.5 sec and 0.33 sec periods), which are perceptible as slow modulation and create interesting chaotic patterns from the incommensurate frequency ratio. The DSP class defaults (200/300 Hz) are audio-rate defaults for standalone Rungler use but fall outside the modulation-focused UI range.

2. **ModSource enum renumbering**: Rungler inserted at position 10 (after Chaos), with SampleHold/PitchFollower/Transient shifted +1. Preset migration applied during state load for version < 13.

3. **Macro simplicity**: Only the value knob is exposed. MacroConfig's minOutput/maxOutput/curve are deferred to a future spec.

4. **State version**: Bumped from 12 to 13. New data appended at end of stream for backward compatibility.

## Build & Test

```bash
# Build
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release

# Run DSP tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe

# Run plugin tests
"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests
build/windows-x64-release/plugins/ruinae/tests/Release/ruinae_tests.exe

# Run pluginval
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"
```

## Verification Checklist

- [ ] All 10 parameters visible in DAW automation lanes
- [ ] Macros view shows 4 knobs when "Macros" selected in dropdown
- [ ] Rungler view shows 6 controls when "Rungler" selected in dropdown
- [ ] Rungler produces chaotic modulation when routed in mod matrix
- [ ] Preset save/load round-trips all 10 parameters
- [ ] Old presets load with correct defaults (macros=0, rungler defaults)
- [ ] Old presets with SampleHold/PitchFollower/Transient routes still work (enum migration)
- [ ] pluginval passes at strictness 5
- [ ] Zero compiler warnings
- [ ] All existing tests pass unchanged (except kNumGlobalSources assertion update)
