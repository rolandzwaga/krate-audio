# Quickstart: Ruinae Flanger Effect

**Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)

## Overview

This feature adds a Flanger DSP processor to the KrateDSP library and integrates it into the Ruinae synthesizer's modulation effects slot as an alternative to the Phaser. The implementation touches 3 layers:

1. **DSP Layer** (shared library): New `Flanger` class at Layer 2
2. **Plugin Layer** (Ruinae): Parameter helpers, effects chain integration, processor/controller changes
3. **State Layer** (Ruinae): Preset save/load with backward-compatible migration

## Key Files

| File | Action | Purpose |
|------|--------|---------|
| `dsp/include/krate/dsp/processors/flanger.h` | CREATE | Flanger DSP class |
| `dsp/tests/unit/processors/flanger_test.cpp` | CREATE | Flanger unit tests |
| `plugins/ruinae/src/plugin_ids.h` | MODIFY | Add kFlanger* IDs (1910-1919) |
| `plugins/ruinae/src/parameters/flanger_params.h` | CREATE | Parameter helpers |
| `plugins/ruinae/src/engine/ruinae_effects_chain.h` | MODIFY | Modulation slot + crossfade |
| `plugins/ruinae/src/processor/processor.cpp` | MODIFY | Param handling, state, migration |
| `plugins/ruinae/src/controller/controller.cpp` | MODIFY | Register flanger params |

## Implementation Order

### Phase 1: DSP Core (Flanger class)
1. Write `flanger.h` with full API (see `contracts/flanger-api.h`)
2. Write `flanger_test.cpp` covering:
   - Basic processing (output differs from input when active)
   - Mix behavior (0.0 = dry, 1.0 = wet, true crossfade)
   - Feedback positive/negative character
   - Stereo spread (L/R phase offset)
   - Waveform selection
   - Tempo sync
   - Stability at extreme feedback
   - Parameter smoothing (no clicks)
   - Reset clears state
   - Prepare re-allocates

### Phase 2: Plugin Integration
1. Add parameter IDs to `plugin_ids.h`
2. Create `flanger_params.h` (register, handle, format, save, load)
3. Add `ModulationType` enum + crossfade to `RuinaeEffectsChain`
4. Wire up processor param handling + state save/load
5. Register params in controller
6. Add preset migration path

### Phase 3: Validation
1. Build and fix all warnings
2. Run dsp_tests and ruinae_tests
3. Run pluginval at strictness 5
4. Run clang-tidy

## Critical Design Decisions

1. **True crossfade mix**: `(1-mix)*dry + mix*wet` -- different from Phaser's additive topology
2. **Linear interpolation**: `DelayLine::readLinear()` for LFO-modulated delay reading
3. **30ms linear ramp crossfade** for modulation slot switching
4. **Retire `phaserEnabled_`**: New `ModulationType` selector (None/Phaser/Flanger) with migration
5. **Delay sweep**: 0.3ms (Depth=0) to 4.0ms (Depth=1), no static center offset
6. **Feedback clamp**: Internal +/-0.98 with tanh soft-clipping
7. **Parameter IDs**: 1910-1918 range, confirmed free

## Testing Strategy

### DSP Tests (dsp_tests target)
- Flanger unit tests in `dsp/tests/unit/processors/flanger_test.cpp`
- Test categories: lifecycle, parameters, processing, stereo, feedback stability, tempo sync

### Plugin Tests (ruinae_tests target)
- State migration test: old preset with phaserEnabled_ loads correctly
- State round-trip: save/load preserves all flanger params
- Parameter change handling: all flanger IDs dispatch correctly

## Dependencies (all existing, no new external deps)

```
Flanger (Layer 2)
  +-- DelayLine (Layer 1) -- delay_line.h
  +-- LFO (Layer 1) -- lfo.h
  +-- OnePoleSmoother (Layer 1) -- smoother.h
  +-- flushDenormal (Layer 0) -- db_utils.h
  +-- isNaN/isInf (Layer 0) -- db_utils.h
  +-- NoteValue (Layer 0) -- note_value.h
```
