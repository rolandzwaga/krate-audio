# Plugin Parameter System

[<- Back to Architecture Index](README.md)

**Location**: `plugins/ruinae/src/parameters/` | **Namespace**: `Ruinae`

---

## Parameter Pack Pattern

Each synthesizer section has a self-contained header file in `plugins/ruinae/src/parameters/` providing:

```cpp
// Example: global_params.h
struct GlobalParams {                    // Atomic storage for thread-safe access
    std::atomic<float> masterGain{1.0f};
    std::atomic<int> voiceMode{0};
    // ...
};
void handleGlobalParamChange(...);       // Denormalize 0-1 to real-world values
void registerGlobalParams(...);          // Register in Controller with names/units
tresult formatGlobalParam(...);          // Display formatting ("0.0 dB", "440 Hz")
void saveGlobalParams(...);              // Serialize to IBStreamer
bool loadGlobalParams(...);              // Deserialize from IBStreamer
void loadGlobalParamsToController(...);  // Sync Controller display from state
```

### Data Flow

```
Host param change (normalized 0-1)
  -> Processor::processParameterChanges()
    -> handleXxxParamChange(params, paramId, value)   [denormalize + atomic store]

Processor::process() audio callback
  -> applyParamsToEngine()
    -> engine_.setXxx(params.field.load(relaxed))     [forward to DSP engine]

Controller::initialize()
  -> registerXxxParams(parameters)                    [register with names/units/defaults]

Controller::setComponentState()
  -> loadXxxParamsToController(streamer, setParam)    [sync display from preset]

Controller::getParamStringByValue()
  -> formatXxxParam(id, value, string)                [display formatting]

Processor::getState()
  -> saveXxxParams(params, streamer)                  [serialize to preset stream]

Processor::setState()
  -> loadXxxParams(params, streamer)                  [deserialize from preset stream]
```

---

## Parameter ID Allocation (Flat Ranges)

| Range | Section | File | Count |
|-------|---------|------|-------|
| 0-99 | Global | `global_params.h` | 6 |
| 100-199 | OSC A | `osc_a_params.h` | 5 |
| 200-299 | OSC B | `osc_b_params.h` | 5 |
| 300-399 | Mixer | `mixer_params.h` | 3 |
| 400-499 | Filter | `filter_params.h` | 5 |
| 500-599 | Distortion | `distortion_params.h` | 4 |
| 600-699 | Trance Gate | `trance_gate_params.h` | 44 |
| 700-799 | Amp Envelope | `amp_env_params.h` | 4 |
| 800-899 | Filter Envelope | `filter_env_params.h` | 4 |
| 900-999 | Mod Envelope | `mod_env_params.h` | 4 |
| 1000-1099 | LFO 1 | `lfo1_params.h` | 4 |
| 1100-1199 | LFO 2 | `lfo2_params.h` | 4 |
| 1200-1299 | Chaos Mod | `chaos_mod_params.h` | 3 |
| 1300-1355 | Mod Matrix | `mod_matrix_params.h` | 56 |
| 1400-1499 | Global Filter | `global_filter_params.h` | 4 |
| 1500-1599 | Freeze | `freeze_params.h` | 2 |
| 1600-1699 | Delay | `delay_params.h` | 6 |
| 1700-1799 | Reverb | `reverb_params.h` | 9 |
| 1800-1899 | Mono Mode | `mono_mode_params.h` | 4 |
| 1900-1999 | Phaser | `phaser_params.h` | varies |
| **2000-2099** | **Macros** | **`macro_params.h`** | **4** |
| **2100-2199** | **Rungler** | **`rungler_params.h`** | **6** |
| **2200-2299** | **Settings** | **`settings_params.h`** | **6** |

**Sentinel**: `kNumParameters = 2300`

---

## Macro Parameters (Spec 057)

**File**: `plugins/ruinae/src/parameters/macro_params.h`
**IDs**: 2000-2003

### MacroParams Struct

```cpp
struct MacroParams {
    std::atomic<float> values[4]{0.0f, 0.0f, 0.0f, 0.0f};  // [0, 1]
};
```

### Parameter Details

| ID | Name | Display | Unit | Default (Norm) | Mapping |
|----|------|---------|------|----------------|---------|
| 2000 | Macro 1 | "XX%" | % | 0.0 | Identity (norm = value) |
| 2001 | Macro 2 | "XX%" | % | 0.0 | Identity |
| 2002 | Macro 3 | "XX%" | % | 0.0 | Identity |
| 2003 | Macro 4 | "XX%" | % | 0.0 | Identity |

### Engine Forwarding

```cpp
// In applyParamsToEngine():
for (int i = 0; i < 4; ++i)
    engine_.setMacroValue(i, macroParams_.values[i].load(relaxed));
```

Macro values are forwarded to `ModulationEngine::setMacroValue()`, which stores them in the `MacroConfig` array. The ModulationEngine applies min/max/curve mapping (configurable per-macro) and returns the processed value via `getRawSourceValue(ModSource::MacroN)`.

### State Persistence

Macro state consists of 4 floats written sequentially:
```
[float: values[0]] [float: values[1]] [float: values[2]] [float: values[3]]
```

Values are already normalized [0, 1] so no inverse mapping is needed for controller loading.

---

## Rungler Parameters (Spec 057)

**File**: `plugins/ruinae/src/parameters/rungler_params.h`
**IDs**: 2100-2105

### RunglerParams Struct

```cpp
struct RunglerParams {
    std::atomic<float> osc1FreqHz{2.0f};   // [0.1, 100] Hz
    std::atomic<float> osc2FreqHz{3.0f};   // [0.1, 100] Hz
    std::atomic<float> depth{0.0f};        // [0, 1]
    std::atomic<float> filter{0.0f};       // [0, 1]
    std::atomic<int>   bits{8};            // [4, 16]
    std::atomic<bool>  loopMode{false};    // false = chaos, true = loop
};
```

### Parameter Details

| ID | Name | Display | Unit | Default (Norm) | Mapping |
|----|------|---------|------|----------------|---------|
| 2100 | Rng Osc1 Freq | "X.XX Hz" | Hz | 0.4337 | Log: `0.1 * pow(1000, norm)` |
| 2101 | Rng Osc2 Freq | "X.XX Hz" | Hz | 0.4924 | Log: `0.1 * pow(1000, norm)` |
| 2102 | Rng Depth | "XX%" | % | 0.0 | Linear: `norm` |
| 2103 | Rng Filter | "XX%" | % | 0.0 | Linear: `norm` |
| 2104 | Rng Bits | "X" | - | 0.3333 | Discrete: `4 + round(norm * 12)`, stepCount=12 |
| 2105 | Rng Loop Mode | on/off | - | 0.0 | Boolean: stepCount=1 |

### Frequency Mapping

The Rungler UI uses a 0.1-100 Hz range (modulation-focused), not the full DSP range (0.1-20000 Hz). Logarithmic mapping provides perceptually uniform control across the range:

```cpp
// Normalized [0, 1] -> Hz [0.1, 100]
float runglerFreqFromNormalized(double norm) {
    return clamp(0.1f * pow(1000.0, norm), 0.1f, 100.0f);
}

// Hz [0.1, 100] -> Normalized [0, 1]
double runglerFreqToNormalized(float hz) {
    return clamp(log(hz / 0.1) / log(1000.0), 0.0, 1.0);
}
```

Default frequencies (2.0 Hz and 3.0 Hz) maintain the Rungler's 2:3 frequency ratio, producing 0.5 sec and 0.33 sec periods for perceptible slow chaotic modulation.

### Bits Mapping

```cpp
// Normalized [0, 1] -> Bits [4, 16] (13 discrete values, stepCount=12)
int runglerBitsFromNormalized(double norm) {
    return 4 + clamp(static_cast<int>(norm * 12 + 0.5), 0, 12);
}
```

### Engine Forwarding

```cpp
// In applyParamsToEngine():
engine_.setRunglerOsc1Freq(runglerParams_.osc1FreqHz.load(relaxed));
engine_.setRunglerOsc2Freq(runglerParams_.osc2FreqHz.load(relaxed));
engine_.setRunglerDepth(runglerParams_.depth.load(relaxed));
engine_.setRunglerFilter(runglerParams_.filter.load(relaxed));
engine_.setRunglerBits(static_cast<size_t>(runglerParams_.bits.load(relaxed)));
engine_.setRunglerLoopMode(runglerParams_.loopMode.load(relaxed));
```

These forward through `RuinaeEngine` -> `ModulationEngine` -> `Rungler` (Layer 2 DSP processor).

### State Persistence

Rungler state consists of 4 floats + 2 int32s written sequentially:
```
[float: osc1FreqHz] [float: osc2FreqHz] [float: depth] [float: filter]
[int32: bits] [int32: loopMode (0 or 1)]
```

Controller loading requires inverse mapping for frequency and bits parameters:
- Frequencies: `runglerFreqToNormalized(hz)` converts Hz back to normalized [0, 1]
- Bits: `runglerBitsToNormalized(bits)` converts integer back to normalized [0, 1]
- Depth/Filter: stored as normalized values, no conversion needed
- Loop Mode: stored as int32 (0/1), cast to bool for `setParam()`

---

## Settings Parameters (Spec 058)

**File**: `plugins/ruinae/src/parameters/settings_params.h`
**IDs**: 2200-2205

### SettingsParams Struct

```cpp
struct SettingsParams {
    std::atomic<float> pitchBendRangeSemitones{2.0f};  // [0, 24] semitones
    std::atomic<int> velocityCurve{0};                  // VelocityCurve index (0-3)
    std::atomic<float> tuningReferenceHz{440.0f};       // [400, 480] Hz
    std::atomic<int> voiceAllocMode{1};                 // AllocationMode index (0-3), default=Oldest(1)
    std::atomic<int> voiceStealMode{0};                 // StealMode index (0-1), default=Hard(0)
    std::atomic<bool> gainCompensation{true};           // default=ON for new presets
};
```

### Parameter Details

| ID | Name | Display | Unit | Default (Norm) | Mapping |
|----|------|---------|------|----------------|---------|
| 2200 | Pitch Bend Range | "X st" | st | 0.0833 (= 2/24) | Linear discrete: `round(norm * 24)`, stepCount=24 |
| 2201 | Velocity Curve | StringListParameter | - | 0 (Linear) | Discrete: Linear/Soft/Hard/Fixed (stepCount=3) |
| 2202 | Tuning Reference | "XXX.X Hz" | Hz | 0.5 (= 440 Hz) | Linear: `400 + norm * 80`, continuous |
| 2203 | Voice Allocation | StringListParameter | - | 1 (Oldest) | Discrete: RoundRobin/Oldest/LowestVelocity/HighestNote (stepCount=3, default index=1) |
| 2204 | Voice Steal | StringListParameter | - | 0 (Hard) | Discrete: Hard/Soft (stepCount=1) |
| 2205 | Gain Compensation | on/off | - | 1.0 (ON) | Boolean: stepCount=1 |

### Pitch Bend Range Mapping

```cpp
// Normalized [0, 1] -> Semitones [0, 24] (integer steps)
float pitchBendFromNormalized(double norm) {
    return clamp(static_cast<float>(round(norm * 24.0)), 0.0f, 24.0f);
}
```

Default of 2 semitones (normalized 2/24 = 0.0833) is the standard MIDI pitch bend range.

### Tuning Reference Mapping

```cpp
// Normalized [0, 1] -> Hz [400, 480]
float tuningFromNormalized(double norm) {
    return clamp(400.0f + static_cast<float>(norm) * 80.0f, 400.0f, 480.0f);
}

// Hz [400, 480] -> Normalized [0, 1]
double tuningToNormalized(float hz) {
    return static_cast<double>((hz - 400.0f) / 80.0f);
}
```

Default of 440 Hz (normalized 0.5) is the ISO 16 standard tuning frequency for A4.

### Voice Allocation Default

Voice Allocation defaults to Oldest (index 1), NOT the first list item (Round Robin). Uses `createDropdownParameterWithDefault()` with `defaultIndex=1` to set the initial selection correctly.

### Engine Forwarding

```cpp
// In applyParamsToEngine():
engine_.setPitchBendRange(settingsParams_.pitchBendRangeSemitones.load(relaxed));
engine_.setVelocityCurve(static_cast<VelocityCurve>(settingsParams_.velocityCurve.load(relaxed)));
engine_.setTuningReference(settingsParams_.tuningReferenceHz.load(relaxed));
engine_.setAllocationMode(static_cast<AllocationMode>(settingsParams_.voiceAllocMode.load(relaxed)));
engine_.setStealMode(static_cast<StealMode>(settingsParams_.voiceStealMode.load(relaxed)));
engine_.setGainCompensationEnabled(settingsParams_.gainCompensation.load(relaxed));
```

All 6 engine methods existed before this spec (Spec 048). This spec exposes them as automatable VST parameters instead of using hardcoded values.

**Note**: Spec 058 removed the hardcoded `engine_.setGainCompensationEnabled(false)` from `Processor::initialize()`. Gain compensation is now exclusively driven by the parameter value.

### State Persistence

Settings state consists of 2 floats + 4 int32s written sequentially (24 bytes total):
```
[float: pitchBendRangeSemitones] [int32: velocityCurve] [float: tuningReferenceHz]
[int32: voiceAllocMode] [int32: voiceStealMode] [int32: gainCompensation (0 or 1)]
```

Controller loading requires inverse mapping for continuous parameters:
- Pitch Bend Range: `semitones / 24.0` converts semitones back to normalized [0, 1]
- Tuning Reference: `(hz - 400) / 80` converts Hz back to normalized [0, 1]
- Velocity Curve: `index / 3.0` converts index back to normalized [0, 1]
- Voice Allocation: `index / 3.0` converts index back to normalized [0, 1]
- Voice Steal: `index / 1.0` (identity) converts index to normalized [0, 1]
- Gain Compensation: `int32 != 0 ? 1.0 : 0.0` converts to boolean normalized

### Backward Compatibility (Version < 14)

For presets saved before Spec 058 (version < 14), settings parameters default to pre-spec behavior:

| Parameter | Backward-Compat Default | Rationale |
|-----------|------------------------|-----------|
| Pitch Bend Range | 2 semitones | Standard MIDI default |
| Velocity Curve | Linear (0) | Original behavior |
| Tuning Reference | 440 Hz | Standard tuning |
| Voice Allocation | Oldest (1) | Original behavior |
| Voice Steal | Hard (0) | Original behavior |
| Gain Compensation | **OFF (false)** | Preserves pre-spec behavior (was hardcoded `false`) |

**Important**: Gain compensation defaults to ON for new presets (`gainCompensation{true}` in struct) but is explicitly set to OFF when loading old presets (`version < 14`). This is because gain compensation was previously hardcoded to `false` in `Processor::initialize()`, so old presets must maintain that behavior.

---

## Denormalization Mappings Reference

| Mapping | Parameters | Formula |
|---------|------------|---------|
| Identity | Macro values, Depth, Filter, Spread | `normalized` (1:1) |
| Linear (x2) | Width (0-200%) | `normalized * 2.0` |
| Linear (scaled) | Gain (0-2), Levels, Mix | `normalized * range` |
| Exponential | Filter Cutoff (20-20kHz) | `20 * pow(1000, normalized)` |
| Exponential | LFO Rate (0.01-50Hz) | `0.01 * pow(5000, normalized)` |
| Logarithmic | Rungler Freq (0.1-100Hz) | `0.1 * pow(1000, normalized)` |
| Cubic | Envelope Times (0-10000ms) | `normalized^3 * 10000` |
| Cubic | Portamento Time (0-5000ms) | `normalized^3 * 5000` |
| Bipolar | Tune (-24/+24), Mod Amount (-1/+1) | `normalized * range - offset` |
| Linear (offset+scale) | Tuning Reference (400-480Hz) | `400 + normalized * 80` |
| Discrete (stepped) | Pitch Bend Range (0-24st) | `round(normalized * 24)` |
| Discrete | Rungler Bits (4-16) | `4 + round(normalized * 12)` |
| Boolean | Loop Mode, Enabled flags, Gain Comp | `normalized >= 0.5` |

---

## Adding a New Parameter Pack

To add a new parameter section, follow these steps:

1. **Allocate ID range** in `plugin_ids.h` (e.g., `kMyBaseId = 2300, kMyParam1Id = 2300, ...`)
2. **Create parameter header** `plugins/ruinae/src/parameters/my_params.h` with:
   - `MyParams` struct with `std::atomic<>` fields
   - `handleMyParamChange()` -- denormalize and store
   - `registerMyParams()` -- register with names, units, defaults
   - `formatMyParam()` -- display formatting
   - `saveMyParams()` / `loadMyParams()` -- binary serialization
   - `loadMyParamsToController()` -- controller sync with inverse mappings
3. **Wire to Processor** (`processor.h` + `processor.cpp`):
   - Add `MyParams myParams_` field
   - Handle in `processParameterChanges()` ID range check
   - Forward in `applyParamsToEngine()`
   - Save/load in `getState()` / `setState()` with version guard
4. **Wire to Controller** (`controller.cpp`):
   - Register in `initialize()`
   - Load in `setComponentState()` with version guard
   - Format in `getParamStringByValue()`
5. **Bump state version** in `plugin_ids.h` and add version guard: `if (version >= N) { ... }`
6. **Add control-tags** in `editor.uidesc` for UI binding
7. **Add UI controls** in appropriate template in `editor.uidesc`

---

## UI Integration

### Control-Tags (editor.uidesc)

Each parameter must have a corresponding `<control-tag>` entry:

```xml
<control-tag name="Macro1Value" tag="2000"/>
<control-tag name="RunglerOsc1Freq" tag="2100"/>
<control-tag name="SettingsPitchBendRange" tag="2200"/>
```

### Mod Source Dropdown Integration

The `ModSourceViewMode` StringListParameter drives a UIViewSwitchContainer. Selecting "Macros" or "Rungler" from the dropdown displays the corresponding template:

| Dropdown Entry | Template | Controls |
|----------------|----------|----------|
| Macros | `ModSource_Macros` | 4 ArcKnobs (M1-M4) + 4 CTextLabels |
| Rungler | `ModSource_Rungler` | 4 ArcKnobs (Osc1, Osc2, Depth, Filter) + 1 ArcKnob (Bits) + 1 ToggleButton (Loop) + 6 CTextLabels |

All ArcKnobs use `arc-color="modulation"` and `guide-color="knob-guide"` for visual consistency with the modulation section theme.
