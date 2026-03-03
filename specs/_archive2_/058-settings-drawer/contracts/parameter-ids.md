# Parameter ID Contract: Settings Parameters

**Spec**: 058-settings-drawer | **Date**: 2026-02-16

## ID Range Allocation

```
2200-2299: Settings Parameters
2200: kSettingsPitchBendRangeId   - Pitch bend range [0, 24] semitones
2201: kSettingsVelocityCurveId    - Velocity curve (Linear/Soft/Hard/Fixed)
2202: kSettingsTuningReferenceId  - A4 tuning reference [400, 480] Hz
2203: kSettingsVoiceAllocModeId   - Voice allocation (RoundRobin/Oldest/LowestVelocity/HighestNote)
2204: kSettingsVoiceStealModeId   - Voice steal mode (Hard/Soft)
2205: kSettingsGainCompensationId - Gain compensation on/off
```

Sentinel: `kNumParameters` changes from 2200 to 2300.

## UI Action Tags (Non-Parameter)

```
10020: kActionSettingsToggleTag   - Gear icon toggle (opens/closes drawer)
10021: kActionSettingsOverlayTag  - Click-outside overlay (dismiss gesture)
```

## Control-Tag Bindings (editor.uidesc)

| Control-Tag Name | Tag Value | UI Control |
|-----------------|-----------|------------|
| SettingsPitchBendRange | 2200 | ArcKnob (36x36) |
| SettingsVelocityCurve | 2201 | COptionMenu (140x20) |
| SettingsTuningReference | 2202 | ArcKnob (36x36) |
| SettingsVoiceAllocMode | 2203 | COptionMenu (140x20) |
| SettingsVoiceStealMode | 2204 | COptionMenu (140x20) |
| SettingsGainCompensation | 2205 | ToggleButton (50x20) |
| ActionSettingsToggle | 10020 | ToggleButton/gear (18x18, existing) |
| ActionSettingsOverlay | 10021 | ToggleButton/transparent (925x880, overlay) |

## Parameter Registration Specification

### kSettingsPitchBendRangeId (2200)
- Type: `Parameter` (basic)
- Title: "Pitch Bend Range"
- Unit: "st"
- stepCount: 24 (integer steps 0-24)
- Default (normalized): 0.0833 (= 2/24)
- Flags: `kCanAutomate`
- Display: "X st" (integer, e.g., "2 st", "12 st")

### kSettingsVelocityCurveId (2201)
- Type: `StringListParameter` via `createDropdownParameter()`
- Title: "Velocity Curve"
- Items: "Linear", "Soft", "Hard", "Fixed"
- Default: index 0 (Linear)
- Flags: `kCanAutomate | kIsList`
- Display: dropdown item name

### kSettingsTuningReferenceId (2202)
- Type: `Parameter` (basic)
- Title: "Tuning Reference"
- Unit: "Hz"
- stepCount: 0 (continuous)
- Default (normalized): 0.5 (= 440 Hz)
- Flags: `kCanAutomate`
- Display: "XXX.X Hz" (1 decimal, e.g., "440.0 Hz", "432.0 Hz")

### kSettingsVoiceAllocModeId (2203)
- Type: `StringListParameter` via `createDropdownParameterWithDefault()`
- Title: "Voice Allocation"
- Items: "Round Robin", "Oldest", "Lowest Velocity", "Highest Note"
- Default: index 1 (Oldest)
- Flags: `kCanAutomate | kIsList`
- Display: dropdown item name

### kSettingsVoiceStealModeId (2204)
- Type: `StringListParameter` via `createDropdownParameter()`
- Title: "Voice Steal"
- Items: "Hard", "Soft"
- Default: index 0 (Hard)
- Flags: `kCanAutomate | kIsList`
- Display: dropdown item name

### kSettingsGainCompensationId (2205)
- Type: `Parameter` (basic)
- Title: "Gain Compensation"
- Unit: ""
- stepCount: 1 (boolean)
- Default (normalized): 1.0 (ON for new presets)
- Flags: `kCanAutomate`
- Display: On/Off toggle
