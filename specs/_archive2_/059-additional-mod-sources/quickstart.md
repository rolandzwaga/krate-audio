# Quickstart: Additional Modulation Sources

**Spec**: 059-additional-mod-sources | **Date**: 2026-02-16

## What This Spec Does

Wires up 5 remaining modulation sources (Env Follower, Sample & Hold, Random, Pitch Follower, Transient Detector) that already have full DSP implementations in the ModulationEngine but need plugin-layer parameter exposure. Adds 18 new automatable parameters and populates 5 empty uidesc view templates with functional controls.

## Prerequisites

- Spec 053 (Mod Source Dropdown) -- completed and merged
- Spec 057 (Macros & Rungler) -- completed and merged (establishes the pattern)
- Spec 058 (Settings Drawer) -- completed and merged (state version 14)

## Implementation Order

### Phase 1: Parameter IDs and Param Files

1. **Add IDs to `plugin_ids.h`**: 5 new ranges (2300-2799), bump `kNumParameters` from 2300 to 2800
2. **Create 5 param files**: `env_follower_params.h`, `sample_hold_params.h`, `random_params.h`, `pitch_follower_params.h`, `transient_params.h` -- each with struct + register + handle + format + save + load + loadToController

### Phase 2: Engine Forwarding

3. **Add 18 forwarding methods to `ruinae_engine.h`**: One-liner methods calling `globalModEngine_.*` for all 5 sources

### Phase 3: Processor Wiring

4. **Extend `processor.h`**: Add 5 param fields, bump `kCurrentStateVersion` from 14 to 15
5. **Extend `processor.cpp`**:
   - `processParameterChanges()`: Handle 5 new ID ranges
   - `applyParamsToEngine()`: Forward all params to engine (S&H sync: NoteValue+BPM->Hz conversion)
   - `getState()`: Save 5 param packs as v15 data
   - `setState()`: Load 5 param packs when version >= 15, defaults when < 15

### Phase 4: Controller Registration

6. **Extend `controller.cpp`**:
   - `initialize()`: Register 5 param groups (18 params total)
   - `setComponentState()`: Load 5 param groups when version >= 15
   - `getParamStringByValue()`: Format 5 param groups

### Phase 5: UI

7. **Add 18 control-tags** to `editor.uidesc`
8. **Populate 5 empty templates** in `editor.uidesc`:
   - EnvFollower: 3 knobs (Sens, Atk, Rel)
   - SampleHold: Rate/NoteValue (switching) + Slew + Sync toggle
   - Random: Rate/NoteValue (switching) + Smooth + Sync toggle
   - PitchFollower: 4 knobs (Min, Max, Conf, Speed)
   - Transient: 3 knobs (Sens, Atk, Decay)
9. **Extend sync visibility switching**: Add SH and Random groups to the existing Chaos switching mechanism

### Phase 6: Verification

10. **Build** with zero warnings
11. **Run all tests** (existing tests must pass, new tests must pass)
12. **Run pluginval** at strictness 5
13. **Update architecture docs**

## Key Patterns to Follow

| Pattern | Reference File | What to Copy |
|---------|---------------|--------------|
| Param file structure | `macro_params.h`, `rungler_params.h` | Struct + 6 inline functions |
| Log frequency mapping | `rungler_params.h:31-40` | `*FromNormalized` / `*ToNormalized` functions |
| LFO rate reuse | `chaos_mod_params.h:30` | `lfoRateFromNormalized(value)` for Rate params |
| Sync + NoteValue | `chaos_mod_params.h:17-23` | Struct fields + handle + register + save/load |
| Sync visibility | `editor.uidesc:1700-1724` (Chaos template) | `custom-view-name` CViewContainer groups |
| Engine forwarding | `ruinae_engine.h:421-426` | One-liner `globalModEngine_.*` methods |
| Processor wiring | `processor.cpp:666-671` | `handleXxxParamChange` in processParameterChanges |
| State persistence | `processor.cpp:404-409` (save), `536-540` (load) | Versioned save/load with backward compat |

## Critical Implementation Details

1. **S&H has NO built-in tempo sync** -- handle at processor level: `rateHz = 1000.0f / dropdownToDelayMs(noteIdx, tempoBPM_)`
2. **Random sync also handled at processor level** -- bypass RandomSource's built-in sync, compute Hz from NoteValue for consistent UX
3. **State version is 14, not 13** -- spec draft was correct; bump to 15
4. **No ModSource enum changes needed** -- all 5 sources already in enum; no preset migration required
5. **18 params total**: EnvFollower(3) + SampleHold(4) + Random(4) + PitchFollower(4) + Transient(3)
