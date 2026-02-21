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
| 14 | 058 | Settings params (2 floats + 4 int32s = 24 bytes): pitch bend range, velocity curve, tuning reference, voice alloc mode, voice steal mode, gain compensation |
| **15** | **059** | **Mod source params (3+4+4+4+3 = 18 values, 72 bytes): Env Follower (3 floats), S&H (2 floats + 2 int32s), Random (2 floats + 2 int32s), Pitch Follower (4 floats), Transient (3 floats)** |
| **--** | **071** | **Arpeggiator params (7 int32s + 3 floats + 1 int32 = 44 bytes): enabled, mode, octaveRange, octaveMode, tempoSync, noteValue, freeRate, gateLength, swing, latchMode, retrigger. EOF-safe loading (no version bump), appended after harmonizer enable flag** |
| **--** | **072** | **Arpeggiator lane data (396 bytes appended after base arp params): velocity lane (int32 length + 32 floats), gate lane (int32 length + 32 floats), pitch lane (int32 length + 32 int32s). EOF-safe loading (no version bump)** |

---

## Stream Format (Version 15)

```
[int32: stateVersion = 15]

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

--- New in v15 (Spec 059) ---
[EnvFollowerParams]   // 3 floats (12 bytes): sensitivity, attackMs, releaseMs
[SampleHoldParams]    // 2 floats + 2 int32s (16 bytes): rateHz, sync, noteValue, slewMs
[RandomParams]        // 2 floats + 2 int32s (16 bytes): rateHz, sync, noteValue, smoothness
[PitchFollowerParams] // 4 floats (16 bytes): minHz, maxHz, confidence, speedMs
[TransientParams]     // 3 floats (12 bytes): sensitivity, attackMs, decayMs

--- New in Spec 071 (EOF-safe, no version bump) ---
[ArpeggiatorParams]   // 7 int32s + 3 floats + 1 int32 (44 bytes): enabled, mode, octaveRange,
                      // octaveMode, tempoSync, noteValue, freeRate, gateLength, swing,
                      // latchMode, retrigger. loadArpParams() returns false on truncated
                      // stream -- arp defaults preserved (disabled, Up mode, 1 octave, etc.)

--- New in Spec 072 (EOF-safe, no version bump, appended after arp base params) ---
[ArpLaneData]         // 396 bytes total, appended after ArpeggiatorParams:
                      //   [int32: velocityLaneLength] [float x32: velocityLaneSteps]  (132 bytes)
                      //   [int32: gateLaneLength]     [float x32: gateLaneSteps]      (132 bytes)
                      //   [int32: pitchLaneLength]    [int32 x32: pitchLaneSteps]     (132 bytes)
                      // loadArpParams() continues EOF-safe reading after base params;
                      // if stream ends mid-lane, remaining lanes keep defaults

--- New in Spec 073 (EOF-safe, no version bump, appended after pitch lane data) ---
[ArpModifierLaneData] // 140 bytes total, appended after ArpLaneData:
                      //   [int32: modifierLaneLength]                                 (4 bytes)
                      //   [int32 x32: modifierLaneSteps]                              (128 bytes)
                      //   [int32: accentVelocity]                                     (4 bytes)
                      //   [float: slideTime]                                          (4 bytes)
                      // EOF-safe backward-compatible loading:
                      //   - EOF at modifierLaneLength read = Phase 4 preset, return true
                      //     (all modifier fields retain defaults: length=1, steps=kStepActive,
                      //      accentVelocity=30, slideTime=60.0f)
                      //   - EOF after modifierLaneLength (partial modifier data) = corrupt
                      //     stream, return false
                      // Phase 4 presets load with all modifier defaults automatically
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

// v15: Mod source params
if (version >= 15) {
    loadEnvFollowerParams(envFollowerParams_, streamer);
    loadSampleHoldParams(sampleHoldParams_, streamer);
    loadRandomParams(randomParams_, streamer);
    loadPitchFollowerParams(pitchFollowerParams_, streamer);
    loadTransientParams(transientParams_, streamer);
}
// If version < 15: all mod source params keep their struct defaults:
// Env Follower: Sensitivity=0.5, Attack=10ms, Release=100ms
// S&H: Rate=4Hz, Sync=off, NoteValue=1/8, Slew=0ms
// Random: Rate=4Hz, Sync=off, NoteValue=1/8, Smoothness=0
// Pitch Follower: MinHz=80, MaxHz=2000, Confidence=0.5, Speed=50ms
// Transient: Sensitivity=0.5, Attack=2ms, Decay=50ms

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
if (version >= 15) {
    loadEnvFollowerParamsToController(streamer, setParam);
    loadSampleHoldParamsToController(streamer, setParam);
    loadRandomParamsToController(streamer, setParam);
    loadPitchFollowerParamsToController(streamer, setParam);
    loadTransientParamsToController(streamer, setParam);
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

## Mod Source Parameters Backward Compatibility (Version < 15 -> 15)

### Background

Spec 059 added 18 parameters across 5 modulation sources (IDs 2300-2799) as 5 new parameter packs appended after the Settings parameters. Before this spec, all 5 DSP processors were fully implemented in the ModulationEngine but had no plugin-layer parameter exposure -- they used their constructor defaults.

### Backward Compatibility

For presets saved before Spec 059 (version < 15), all mod source parameters default to their DSP class constructor defaults:

| Source | Parameter | Default | Notes |
|--------|-----------|---------|-------|
| Env Follower | Sensitivity | 0.5 | Mid-range sensitivity |
| Env Follower | Attack | 10 ms | Fast response |
| Env Follower | Release | 100 ms | Medium decay |
| S&H | Rate | 4 Hz | Medium stepping rate |
| S&H | Sync | OFF | Free-running |
| S&H | Note Value | 1/8 (index 10) | Standard subdivision |
| S&H | Slew | 0 ms | Instant transitions |
| Random | Rate | 4 Hz | Medium generation rate |
| Random | Sync | OFF | Free-running |
| Random | Note Value | 1/8 (index 10) | Standard subdivision |
| Random | Smoothness | 0 | Instant transitions |
| Pitch Follower | Min Hz | 80 Hz | Below typical vocal range |
| Pitch Follower | Max Hz | 2000 Hz | Above typical vocal range |
| Pitch Follower | Confidence | 0.5 | Mid-range threshold |
| Pitch Follower | Speed | 50 ms | Medium tracking speed |
| Transient | Sensitivity | 0.5 | Mid-range detection |
| Transient | Attack | 2 ms | Fast attack envelope |
| Transient | Decay | 50 ms | Medium decay envelope |

No explicit backward-compatibility overrides are needed (unlike Settings/Gain Compensation). The struct member initializers provide correct defaults for old presets.

---

## Arpeggiator Parameters Backward Compatibility (Spec 071)

### Background

Spec 071 added 11 arpeggiator parameters (IDs 3000-3010) as a new parameter pack appended after the harmonizer enable flag. Unlike previous parameter packs, the arpeggiator uses EOF-safe loading without a version bump -- `loadArpParams()` returns `false` gracefully on truncated streams, leaving all arp params at their struct defaults.

### Backward Compatibility

For presets saved before Spec 071 (stream ends before arp data), all arp parameters default to their struct initializer values:

| Parameter | Default | Notes |
|-----------|---------|-------|
| Enabled | false (OFF) | Arp disabled -- no behavior change for old presets |
| Mode | 0 (Up) | Standard ascending pattern |
| Octave Range | 1 | Single octave |
| Octave Mode | 0 (Sequential) | Standard octave traversal |
| Tempo Sync | true (ON) | Sync to host tempo by default |
| Note Value | 10 (1/8 note) | Standard eighth note subdivision |
| Free Rate | 4.0 Hz | Medium rate for free-running mode |
| Gate Length | 80% | Standard gate length |
| Swing | 0% | No swing |
| Latch Mode | 0 (Off) | No latch |
| Retrigger | 0 (Off) | No retrigger |

No explicit backward-compatibility overrides are needed. Since `enabled` defaults to `false`, old presets behave identically to before -- the arpeggiator is invisible and inactive.

---

## Arpeggiator Lane Data Backward Compatibility (Spec 072)

### Background

Spec 072 added 99 lane parameters (IDs 3020-3132) to the arpeggiator parameter pack, serialized as 396 bytes appended after the 11 base arp parameters. Like the base arp params (Spec 071), lane data uses EOF-safe loading without a version bump.

### Serialization Format (396 bytes)

```
[int32: velocityLaneLength]            // 4 bytes
[float x32: velocityLaneSteps[0..31]]  // 128 bytes
[int32: gateLaneLength]                // 4 bytes
[float x32: gateLaneSteps[0..31]]      // 128 bytes
[int32: pitchLaneLength]               // 4 bytes
[int32 x32: pitchLaneSteps[0..31]]     // 128 bytes
                                       // Total: 396 bytes
```

All 32 step values are written regardless of the active lane length. This ensures round-trip fidelity: step values beyond the active length are preserved through save/load cycles, not reset to defaults.

### EOF-Safe Loading Pattern

Lane loading continues the EOF-safe pattern established by the base arp params. Each lane's data is read sequentially; if any read fails, the remaining lanes keep their struct defaults:

```cpp
// After loading base 11 arp params...

// Velocity lane (EOF-safe)
int32 velLen;
if (!streamer.readInt32(velLen)) return true;  // No lane data = Phase 3 preset, keep defaults
params.velocityLaneLength.store(std::clamp(velLen, 1, 32), relaxed);
for (int i = 0; i < 32; ++i) {
    float val;
    if (!streamer.readFloat(val)) return true;  // Partial = keep remaining defaults
    params.velocityLaneSteps[i].store(std::clamp(val, 0.0f, 1.0f), relaxed);
}

// Gate lane (EOF-safe)
// ... same pattern ...

// Pitch lane (EOF-safe)
// ... same pattern, reading int32 values for steps ...
```

### Backward Compatibility

| Preset Source | Lane Data Present? | Behavior |
|---------------|-------------------|----------|
| Phase 3 (Spec 071, no lanes) | No | All lanes at identity defaults: vel length=1/step=1.0, gate length=1/step=1.0, pitch length=1/step=0 |
| Phase 4 (Spec 072, velocity only) | Partial (velocity only) | Velocity lane restored, gate/pitch lanes at defaults |
| Phase 4 (Spec 072, all lanes) | Yes | All lanes fully restored |

Lane identity defaults ensure that presets without lane data produce bit-identical output to Phase 3: velocity scale 1.0 = no change, gate multiplier 1.0 = no change, pitch offset 0 = no change.

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
