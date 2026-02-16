# Data Model: Settings Drawer

**Spec**: 058-settings-drawer | **Date**: 2026-02-16

## Entities

### SettingsParams (Plugin-Level Struct)

**Location**: `plugins/ruinae/src/parameters/settings_params.h`

| Field | Type | Range | Default | Notes |
|-------|------|-------|---------|-------|
| `pitchBendRangeSemitones` | `std::atomic<float>` | 0.0 - 24.0 | 2.0 | Integer steps in UI (stepCount=24) |
| `velocityCurve` | `std::atomic<int>` | 0 - 3 | 0 | Maps to VelocityCurve enum |
| `tuningReferenceHz` | `std::atomic<float>` | 400.0 - 480.0 | 440.0 | Continuous |
| `voiceAllocMode` | `std::atomic<int>` | 0 - 3 | 1 | Maps to AllocationMode enum (default=Oldest) |
| `voiceStealMode` | `std::atomic<int>` | 0 - 1 | 0 | Maps to StealMode enum (default=Hard) |
| `gainCompensation` | `std::atomic<bool>` | false/true | true | Default ON for new presets, OFF for old |

### Relationships

```
SettingsParams (plugin-level)
    |
    +-- pitchBendRangeSemitones --> engine_.setPitchBendRange(float)
    |                                   --> noteProcessor_.setPitchBendRange()
    |
    +-- velocityCurve           --> engine_.setVelocityCurve(VelocityCurve)
    |                                   --> noteProcessor_.setVelocityCurve()
    |
    +-- tuningReferenceHz       --> engine_.setTuningReference(float)
    |                                   --> noteProcessor_.setTuningReference()
    |
    +-- voiceAllocMode          --> engine_.setAllocationMode(AllocationMode)
    |                                   --> allocator_.setAllocationMode()
    |
    +-- voiceStealMode          --> engine_.setStealMode(StealMode)
    |                                   --> allocator_.setStealMode()
    |
    +-- gainCompensation        --> engine_.setGainCompensationEnabled(bool)
```

### Enum Mappings

**VelocityCurve** (`dsp/include/krate/dsp/core/midi_utils.h:122-127`):
| Index | Enum Value | UI Label |
|-------|------------|----------|
| 0 | VelocityCurve::Linear | "Linear" |
| 1 | VelocityCurve::Soft | "Soft" |
| 2 | VelocityCurve::Hard | "Hard" |
| 3 | VelocityCurve::Fixed | "Fixed" |

**AllocationMode** (`dsp/include/krate/dsp/systems/voice_allocator.h:55-60`):
| Index | Enum Value | UI Label |
|-------|------------|----------|
| 0 | AllocationMode::RoundRobin | "Round Robin" |
| 1 | AllocationMode::Oldest | "Oldest" |
| 2 | AllocationMode::LowestVelocity | "Lowest Velocity" |
| 3 | AllocationMode::HighestNote | "Highest Note" |

**StealMode** (`dsp/include/krate/dsp/systems/voice_allocator.h:67-70`):
| Index | Enum Value | UI Label |
|-------|------------|----------|
| 0 | StealMode::Hard | "Hard" |
| 1 | StealMode::Soft | "Soft" |

## VST Parameter IDs

| ID | Name | Type | Normalized Default |
|----|------|------|-------------------|
| 2200 | kSettingsPitchBendRangeId | Parameter (stepCount=24) | 0.0833 (= 2/24) |
| 2201 | kSettingsVelocityCurveId | StringListParameter (4 items) | 0.0 (Linear) |
| 2202 | kSettingsTuningReferenceId | Parameter (continuous) | 0.5 (= (440-400)/80) |
| 2203 | kSettingsVoiceAllocModeId | StringListParameter (4 items, default=1) | 0.333 (Oldest) |
| 2204 | kSettingsVoiceStealModeId | StringListParameter (2 items) | 0.0 (Hard) |
| 2205 | kSettingsGainCompensationId | Parameter (stepCount=1) | 1.0 (ON) |

## Normalization Mappings

| Parameter | Normalized -> Denormalized | Denormalized -> Normalized |
|-----------|---------------------------|---------------------------|
| Pitch Bend Range | `round(norm * 24)` semitones | `semitones / 24.0` |
| Velocity Curve | `round(norm * 3)` index | `index / 3.0` |
| Tuning Reference | `400 + norm * 80` Hz | `(hz - 400) / 80` |
| Voice Allocation | `round(norm * 3)` index | `index / 3.0` |
| Voice Steal | `round(norm * 1)` index | `index / 1.0` |
| Gain Compensation | `norm >= 0.5` boolean | `bool ? 1.0 : 0.0` |

## State Persistence Format (Version 14)

Appended after v13 data (macro + rungler params):

```
Offset  Type       Field
0       float32    pitchBendRangeSemitones (denormalized, 0-24)
4       int32      velocityCurve (enum index, 0-3)
8       float32    tuningReferenceHz (denormalized, 400-480)
12      int32      voiceAllocMode (enum index, 0-3)
16      int32      voiceStealMode (enum index, 0-1)
20      int32      gainCompensation (0 or 1)
```

Total: 24 bytes appended to state stream.

## Backward Compatibility (version < 14)

When loading presets with version < 14, the settings params are NOT in the stream. The processor explicitly applies these defaults:

| Parameter | Old Preset Default | Rationale |
|-----------|-------------------|-----------|
| Pitch Bend Range | 2 semitones | Matches hardcoded engine default |
| Velocity Curve | Linear (0) | Matches hardcoded engine default |
| Tuning Reference | 440 Hz | Matches hardcoded engine default |
| Voice Allocation | Oldest (1) | Matches hardcoded engine default |
| Voice Steal | Hard (0) | Matches hardcoded engine default |
| Gain Compensation | OFF (false) | Matches hardcoded `setGainCompensationEnabled(false)` |
