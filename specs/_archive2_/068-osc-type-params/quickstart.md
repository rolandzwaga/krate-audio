# Quickstart: 068-osc-type-params

## Summary

Extend the Ruinae synthesizer's `OscillatorSlot` interface with a `setParam(OscParam, float)` virtual method, implement type-specific parameter dispatch in all 10 oscillator adapters, add 60 new VST parameter IDs (30 per oscillator), wire parameter routing through the voice/engine chain, update UI templates, and implement state persistence.

## Implementation Order

### Phase 1: DSP Interface (No plugin code)

1. Add `OscParam` enum to `oscillator_types.h`
2. Add `virtual void setParam(OscParam, float) noexcept {}` to `OscillatorSlot`
3. Implement `setParam()` override in `OscillatorAdapter<OscT>` via `if constexpr`
4. Fix SyncOscillator adapter to use stored ratio instead of hardcoded 2x
5. Add `setParam()` forwarding to `SelectableOscillator`
6. Write DSP-level tests for all 10 adapter types

### Phase 2: Parameter IDs and Storage

1. Add 60 new parameter IDs to `plugin_ids.h` (30 for OSC A at 110-139, 30 for OSC B at 210-239)
2. Add new dropdown string arrays to `dropdown_mappings.h`
3. Extend `OscAParams` / `OscBParams` with 30 new atomic fields each
4. Extend `handleOscAParamChange()` / `handleOscBParamChange()` with denormalization
5. Extend `registerOscAParams()` / `registerOscBParams()` with parameter registration
6. Extend `formatOscAParam()` / `formatOscBParam()` with display formatting
7. Write parameter handling tests

### Phase 3: Voice/Engine Routing

1. Add `setOscAParam(OscParam, float)` / `setOscBParam(OscParam, float)` to `RuinaeVoice`
2. Add matching methods to `RuinaeEngine` (iterate all voices)
3. Extend `applyParamsToEngine()` with the new parameter forwarding
4. Write routing integration tests

### Phase 4: UI Templates

1. Add control-tag entries for all 60 new parameter IDs in `editor.uidesc`
2. Replace placeholder templates with fully wired templates
3. Implement PW knob disable sub-controller for OSC A and OSC B (FR-016)
4. Test all 20 template switches

### Phase 5: State Persistence

1. Extend `saveOscAParams()` / `loadOscAParams()` (and B equivalents)
2. Implement backward-compatible loading (old presets without new data)
3. Write round-trip save/load tests

### Phase 6: Validation

1. Build and fix all warnings
2. Run all tests
3. Run pluginval at strictness 5
4. Verify compliance table

## Key Files to Modify

| File | Changes |
|------|---------|
| `dsp/include/krate/dsp/systems/oscillator_types.h` | Add `OscParam` enum |
| `dsp/include/krate/dsp/systems/oscillator_slot.h` | Add `setParam()` virtual method |
| `dsp/include/krate/dsp/systems/oscillator_adapters.h` | Implement `setParam()` override, fix Sync ratio |
| `dsp/include/krate/dsp/systems/selectable_oscillator.h` | Add `setParam()` forwarding |
| `plugins/ruinae/src/plugin_ids.h` | Add 60 parameter IDs |
| `plugins/ruinae/src/parameters/dropdown_mappings.h` | Add 9 new dropdown string arrays |
| `plugins/ruinae/src/parameters/osc_a_params.h` | Extend struct, handler, register, format, save/load |
| `plugins/ruinae/src/parameters/osc_b_params.h` | Mirror of osc_a_params changes |
| `plugins/ruinae/src/engine/ruinae_voice.h` | Add `setOscAParam()`/`setOscBParam()` |
| `plugins/ruinae/src/engine/ruinae_engine.h` | Add `setOscAParam()`/`setOscBParam()` |
| `plugins/ruinae/src/processor/processor.cpp` | Extend `applyParamsToEngine()` |
| `plugins/ruinae/resources/editor.uidesc` | Replace placeholder templates, add control-tags |
| `dsp/tests/unit/systems/selectable_oscillator_test.cpp` | DSP-level setParam tests |
| `plugins/ruinae/tests/` | Plugin-level parameter tests |

## Build Commands

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Build DSP tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests
build/windows-x64-release/bin/Release/dsp_tests.exe

# Build Ruinae plugin + tests
"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests
build/windows-x64-release/bin/Release/ruinae_tests.exe

# Pluginval
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"
```
