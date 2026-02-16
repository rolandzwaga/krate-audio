# Plugin State Persistence

[<- Back to Architecture Index](README.md)

**Location**: `plugins/ruinae/src/processor/processor.cpp` | **Format**: Binary IBStream

---

## Overview

Plugin state is persisted as a versioned binary stream using Steinberg's `IBStreamer`. The stream begins with an `int32` version number, followed by parameter packs serialized in a fixed order. New parameter packs are always appended at the end to maintain backward compatibility.

---

## State Version History

| Version | Spec | Changes |
|---------|------|---------|
| 1 | 045 | Initial: Global, OscA, OscB, Mixer, Filter, Distortion, TranceGate, AmpEnv, FilterEnv, ModEnv, LFO1, LFO2, ChaosMod, ModMatrix (24 params), GlobalFilter, Freeze, Delay, Reverb, MonoMode |
| 2 | 049 | ModMatrix extended to 56 params (adds Curve/Smooth/Scale/Bypass per slot) |
| 3 | 042 | Voice routes appended (16 x VoiceModRoute, 14 bytes each = 224 bytes) |
| 4-11 | various | TranceGate v2 format, envelope bezier, LFO extended params, etc. |
| 12 | 055 | LFO extended params (fade-in, symmetry, quantize) |
| 13 | 057 | Macro params (4 floats) + Rungler params (4 floats + 2 int32s) + ModSource enum migration |
| **14** | **058** | **Settings params (2 floats + 4 int32s = 24 bytes): pitch bend range, velocity curve, tuning reference, voice alloc mode, voice steal mode, gain compensation** |

---

## Stream Format (Version 14)

```
[int32: stateVersion = 14]

--- Existing packs (v1 through v12) ---
[GlobalParams]        // masterGain, voiceMode, polyphony, softLimit, width, spread
[OscAParams]          // type, tune, fine, level, phase
[OscBParams]          // type, tune, fine, level, phase
[MixerParams]         // mode, position, tilt
[FilterParams]        // type, cutoff, resonance, envAmount, keyTrack
[DistortionParams]    // type, drive, character, mix
[TranceGateParams]    // enabled, numSteps, rate, depth, attack, release, sync, noteValue,
                      // euclidean, hits, rotation, phaseOffset, 32 step levels (v2 format)
[AmpEnvParams]        // attack, decay, sustain, release + curves + bezier (v4+)
[FilterEnvParams]     // attack, decay, sustain, release + curves + bezier (v4+)
[ModEnvParams]        // attack, decay, sustain, release + curves + bezier (v4+)
[LFO1Params]          // rate, shape, depth, sync + extended (v12: fadeIn, symmetry, quantize)
[LFO2Params]          // rate, shape, depth, sync + extended (v12: fadeIn, symmetry, quantize)
[ChaosModParams]      // model, speed, coupling + tempoSync, noteValue
[ModMatrixParams]     // 8 slots x (source, dest, amount) + (curve, smooth, scale, bypass) (v2)
[VoiceRoutes]         // 16 x VoiceModRoute (v3+)
[GlobalFilterParams]  // enabled, type, cutoff, resonance
[FreezeParams]        // enabled, toggle
[DelayParams]         // type, time, feedback, mix, sync, noteValue
[ReverbParams]        // size, damping, width, mix, preDelay, diffusion, freeze, modRate, modDepth
[MonoModeParams]      // priority, legato, portamento, portaMode

--- New in v13 (Spec 057) ---
[MacroParams]         // 4 floats: values[0], values[1], values[2], values[3]
[RunglerParams]       // 4 floats + 2 int32s: osc1FreqHz, osc2FreqHz, depth, filter, bits, loopMode

--- New in v14 (Spec 058) ---
[SettingsParams]      // 2 floats + 4 int32s (24 bytes): pitchBendRangeSemitones, velocityCurve,
                      // tuningReferenceHz, voiceAllocMode, voiceStealMode, gainCompensation
```

---

## Version Guard Pattern

New packs are loaded only when the stream version is sufficient:

```cpp
// In Processor::setState():
if (version >= 13) {
    loadMacroParams(macroParams_, streamer);
    loadRunglerParams(runglerParams_, streamer);
}
// If version < 13: macroParams_ and runglerParams_ keep their struct defaults
// (macros = 0.0, rungler = 2.0 Hz / 3.0 Hz / depth 0 / filter 0 / 8 bits / chaos mode)

if (version >= 14) {
    loadSettingsParams(settingsParams_, streamer);
} else {
    // Backward compatibility: old presets get pre-spec defaults
    settingsParams_.pitchBendRangeSemitones.store(2.0f, relaxed);
    settingsParams_.velocityCurve.store(0, relaxed);         // Linear
    settingsParams_.tuningReferenceHz.store(440.0f, relaxed);
    settingsParams_.voiceAllocMode.store(1, relaxed);        // Oldest
    settingsParams_.voiceStealMode.store(0, relaxed);        // Hard
    settingsParams_.gainCompensation.store(false, relaxed);  // OFF for old presets
}

// In Controller::setComponentState():
if (version >= 13) {
    loadMacroParamsToController(streamer, setParam);
    loadRunglerParamsToController(streamer, setParam);
}
if (version >= 14) {
    loadSettingsParamsToController(streamer, setParam);
}
if (version < 14) {
    setParam(kSettingsGainCompensationId, 0.0);  // OFF for pre-spec-058 presets
}
```

---

## ModSource Enum Migration (Version < 13 -> 13)

### Background

Spec 057 inserted `ModSource::Rungler = 10` into the `ModSource` enum, shifting three existing values:

| Source | Old Value (v < 13) | New Value (v >= 13) |
|--------|-------------------|---------------------|
| SampleHold | 10 | 11 |
| PitchFollower | 11 | 12 |
| Transient | 12 | 13 |
| Rungler | (did not exist) | 10 |

### Migration Algorithm

When loading a preset with `version < 13`, any mod matrix source values >= 10 must be incremented by 1 to account for the Rungler insertion:

```cpp
// In Processor::setState(), after loading mod matrix params:
if (version < 13) {
    // Migrate global mod matrix slots
    for (auto& slot : modMatrixParams_.slots) {
        int src = slot.source.load(std::memory_order_relaxed);
        if (src >= 10) {
            slot.source.store(src + 1, std::memory_order_relaxed);
        }
    }
}
```

The same migration logic is applied in `Controller::setComponentState()` to keep the UI in sync.

### Migration Scope

Two data structures contain ModSource enum values that need migration:

1. **Global mod matrix slots** (`modMatrixParams_.slots[i].source`): 8 slots, stored as atomic int
2. **Voice routes** (voice route source fields): 16 routes per voice, stored as int8_t

Values 0-9 are unchanged (None through Chaos). Values 10+ shift by +1 (SampleHold, PitchFollower, Transient). The new Rungler value (10) only appears in presets saved with version >= 13.

---

## Settings Parameters Backward Compatibility (Version < 14 -> 14)

### Background

Spec 058 added 6 global settings parameters (IDs 2200-2205) as a new parameter pack appended after the Rungler parameters. Before this spec, these engine behaviors were either hardcoded or not user-controllable:

| Setting | Pre-Spec Behavior | Spec 058 Behavior |
|---------|-------------------|-------------------|
| Pitch Bend Range | Hardcoded 2 semitones | Parameter-driven (0-24 st) |
| Velocity Curve | Hardcoded Linear | Parameter-driven (4 curves) |
| Tuning Reference | Hardcoded 440 Hz | Parameter-driven (400-480 Hz) |
| Voice Allocation | Hardcoded Oldest | Parameter-driven (4 modes) |
| Voice Steal | Hardcoded Hard | Parameter-driven (Hard/Soft) |
| Gain Compensation | **Hardcoded OFF** | Parameter-driven (default ON for new presets) |

### Gain Compensation Default Asymmetry

Gain compensation has different defaults for new vs old presets:

- **New presets** (version >= 14): `gainCompensation{true}` (ON) -- struct initializer default
- **Old presets** (version < 14): Explicitly set to `false` (OFF) -- matches hardcoded `setGainCompensationEnabled(false)` that was removed

This asymmetry is intentional. The hardcoded `false` in `Processor::initialize()` was removed because gain compensation is now parameter-driven. Old presets must preserve the original behavior (OFF) while new presets use the improved default (ON).

The controller also explicitly sets gain compensation to OFF for old presets: `setParam(kSettingsGainCompensationId, 0.0)` when `version < 14`.

---

## Backward Compatibility Rules

1. **New packs are always appended** at the end of the stream, never inserted between existing packs
2. **Version guards** (`if (version >= N)`) protect loading of new data
3. **Default values** are used when version is too old (struct member initializers)
4. **Enum migrations** are applied during load when enum values are renumbered
5. **EOF-safe loading** (soft read without failure) is used when fields are appended to an existing pack
6. **Forward compatibility**: Unknown future versions fail with safe defaults (all packs keep default values)
7. **Truncated streams**: Load what is available, keep defaults for the rest

### EOF-Safe Loading Pattern

When appending new fields to an existing parameter pack (rather than creating a new pack):

```cpp
// Hard read (existing required field):
if (!streamer.readFloat(floatVal)) return false;  // Abort on failure

// Soft read (new optional field added at end of pack):
if (streamer.readFloat(floatVal))
    params.newField.store(floatVal, std::memory_order_relaxed);
// else: keep default value from struct initializer
```

This pattern was used in Spec 054 (Width/Spread appended to GlobalParams) and should be used whenever fields are added to the end of an existing pack rather than creating a new version-guarded pack.

---

## Adding New State Data

To add new persistent parameters:

1. **Bump version** in `plugin_ids.h`: `kCurrentStateVersion = N+1`
2. **Add save function call** in `Processor::getState()` after existing saves
3. **Add load with version guard** in `Processor::setState()`:
   ```cpp
   if (version >= N+1) {
       loadNewParams(newParams_, streamer);
   }
   ```
4. **Add controller load** in `Controller::setComponentState()` with same version guard
5. **If renumbering enums**: Add migration block for `version < N+1` (see ModSource migration above)
6. **Test round-trip**: Save state, load state, verify all values restored
7. **Test backward compatibility**: Load old preset (version < N+1), verify defaults applied correctly
