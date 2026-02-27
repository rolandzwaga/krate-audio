# Data Model: Arpeggiator Presets & Polish (082-presets-polish)

## Entities

### 1. RuinaePresetState (new, in `tools/ruinae_preset_generator.cpp`)

Mirrors the complete Ruinae processor state. Contains default values matching each parameter struct's defaults. The `serialize()` method produces a `std::vector<uint8_t>` byte-for-byte compatible with `Processor::getState()`.

**Key sections** (each must have matching field types/order with the corresponding `save*Params()` function):

| Section | Source File | Approx Fields |
|---------|------------|---------------|
| Global | `global_params.h` | ~6 fields |
| Osc A | `osc_a_params.h` | ~20 fields |
| Osc B | `osc_b_params.h` | ~20 fields |
| Mixer | `mixer_params.h` | ~6 fields |
| Filter | `filter_params.h` | ~12 fields |
| Distortion | `distortion_params.h` | ~6 fields |
| Trance Gate | `trance_gate_params.h` | ~10+ fields |
| Amp Env | `amp_env_params.h` | ~6 fields |
| Filter Env | `filter_env_params.h` | ~6 fields |
| Mod Env | `mod_env_params.h` | ~6 fields |
| LFO 1 | `lfo1_params.h` | ~8 fields |
| LFO 2 | `lfo2_params.h` | ~8 fields |
| Chaos Mod | `chaos_mod_params.h` | ~6 fields |
| Mod Matrix | `mod_matrix_params.h` | ~many |
| Global Filter | `global_filter_params.h` | ~6 fields |
| Delay | `delay_params.h` | ~8 fields |
| Reverb | `reverb_params.h` | ~8 fields |
| Mono Mode | `mono_mode_params.h` | ~4 fields |
| Voice Routes | (inline in processor.cpp) | 16 x 8 fields |
| FX Enable | (inline in processor.cpp) | 2 int8 |
| Phaser | `phaser_params.h` | ~10 fields + 1 enable |
| LFO 1 Extended | `lfo1_params.h` | ~4 fields |
| LFO 2 Extended | `lfo2_params.h` | ~4 fields |
| Macros | `macro_params.h` | ~12 fields |
| Rungler | `rungler_params.h` | ~8 fields |
| Settings | `settings_params.h` | ~4 fields |
| Env Follower | `env_follower_params.h` | ~6 fields |
| Sample Hold | `sample_hold_params.h` | ~6 fields |
| Random | `random_params.h` | ~6 fields |
| Pitch Follower | `pitch_follower_params.h` | ~6 fields |
| Transient | `transient_params.h` | ~4 fields |
| Harmonizer | `harmonizer_params.h` | ~8 fields + 1 enable |
| Arpeggiator | `arpeggiator_params.h` | ~232 fields |

### 2. ArpPresetData (new, in `tools/ruinae_preset_generator.cpp`)

Sub-structure within `RuinaePresetState` specifically for arp parameters. Fields match `saveArpParams()` exactly:

```
enabled: int32 (0 or 1)
mode: int32 (0=Up, 1=Down, 2=UpDown, 3=DownUp, 4=Converge, 5=Diverge, 6=Random, 7=Walk, 8=AsPlayed, 9=Chord)
octaveRange: int32 (1-4)
octaveMode: int32 (0=Sequential, 1=Interleaved)
tempoSync: int32 (0 or 1)
noteValue: int32 (0-20 dropdown index)
freeRate: float (0.5-50.0 Hz)
gateLength: float (1.0-200.0 %)
swing: float (0.0-75.0 %)
latchMode: int32 (0=Off, 1=Hold, 2=Add)
retrigger: int32 (0=Off, 1=Note, 2=Beat)

velocityLaneLength: int32 (1-32)
velocityLaneSteps[32]: float (0.0-1.0)

gateLaneLength: int32 (1-32)
gateLaneSteps[32]: float (0.01-2.0)

pitchLaneLength: int32 (1-32)
pitchLaneSteps[32]: int32 (-24 to +24)

modifierLaneLength: int32 (1-32)
modifierLaneSteps[32]: int32 (0-255 bitmask)
accentVelocity: int32 (0-127)
slideTime: float (0.0-500.0 ms)

ratchetLaneLength: int32 (1-32)
ratchetLaneSteps[32]: int32 (1-4)

euclideanEnabled: int32 (0 or 1)
euclideanHits: int32 (0-32)
euclideanSteps: int32 (2-32)
euclideanRotation: int32 (0-31)

conditionLaneLength: int32 (1-32)
conditionLaneSteps[32]: int32 (0-17 TrigCondition index)
fillToggle: int32 (0 or 1)

spice: float (0.0-1.0)
humanize: float (0.0-1.0)

ratchetSwing: float (50.0-75.0 %)
```

### 3. PresetDef (new, in `tools/ruinae_preset_generator.cpp`)

```
name: string       -- Display name (e.g., "Basic Up 1/16")
category: string   -- Subdirectory name (e.g., "Arp Classic")
state: RuinaePresetState
```

### 4. PresetManagerConfig (extended, `plugins/shared/src/preset/preset_manager_config.h`)

No structural change. Only the `subcategoryNames` vector in `makeRuinaePresetConfig()` is extended from 6 to 12 entries.

## Relationships

- `RuinaePresetState` contains one `ArpPresetData` (composition)
- `PresetDef` contains one `RuinaePresetState` (composition)
- `createAllPresets()` produces a `vector<PresetDef>` (at least 12 entries)
- `RuinaePresetState::serialize()` produces `vector<uint8_t>` matching `Processor::getState()` format
- `writeVstPreset()` wraps serialized data in `.vstpreset` file format

## Validation Rules

- All arp parameter values must be within the ranges defined by `loadArpParams()` clamping
- Mode value must be 0-9 (10 modes)
- Note value index must be 0-20 (21 dropdown entries)
- Modifier step bitmask must have `kStepActive` (0x01) set for active steps
- Ratchet step values must be 1-4
- Condition step values must be 0-17 (18 conditions)
- Lane lengths must be 1-32

## State Transitions

- Preset load: `setState()` -> `loadArpParams()` -> atomic parameter stores -> `applyParamsToEngine()` on next `process()` call
- Transport start: `processBlock()` detects `isPlaying && !wasPlaying_` -> `firstStepPending_ = true` -> fires step 1
- Transport stop: `processBlock()` detects `!isPlaying && wasPlaying_` -> emits all note-offs -> resets lanes
- Arp disable: `setEnabled(false)` -> `needsDisableNoteOff_ = true` -> next `processBlock()` emits note-offs
