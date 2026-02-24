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
| **2300-2399** | **Env Follower** | **`env_follower_params.h`** | **3** |
| **2400-2499** | **Sample & Hold** | **`sample_hold_params.h`** | **4** |
| **2500-2599** | **Random** | **`random_params.h`** | **4** |
| **2600-2699** | **Pitch Follower** | **`pitch_follower_params.h`** | **4** |
| **2700-2799** | **Transient** | **`transient_params.h`** | **3** |
| **3000-3010** | **Arpeggiator (base)** | **`arpeggiator_params.h`** | **11** |
| **3020-3132** | **Arpeggiator Lanes** | **`arpeggiator_params.h`** | **99** |
| **3140-3181** | **Arpeggiator Modifiers** | **`arpeggiator_params.h`** | **35** |
| **3190-3222** | **Arpeggiator Ratchet Lane** | **`arpeggiator_params.h`** | **33** |
| **3230-3233** | **Arpeggiator Euclidean** | **`arpeggiator_params.h`** | **4** |
| **3240-3272** | **Arpeggiator Condition Lane** | **`arpeggiator_params.h`** | **33** |
| **3280** | **Arpeggiator Fill Toggle** | **`arpeggiator_params.h`** | **1** |
| **3290-3292** | **Arpeggiator Spice/Dice/Humanize** | **`arpeggiator_params.h`** | **3** |

**Reserved gaps**: 3011-3019 (future base arp params), 3053-3059 (velocity lane metadata), 3093-3099 (gate lane metadata), 3133-3139 (reserved), 3173-3179 (reserved), 3182-3189 (reserved), 3223-3229 (reserved), 3234-3239 (reserved gap before condition lane; reserved for use before Phase 9), 3273-3279 (reserved gap between condition step IDs and fill toggle; reserved for future condition-lane extensions), 3281-3289 (reserved), 3293-3299 (reserved for future arp phases)

**Sentinel**: `kArpEndId = 3299`, `kNumParameters = 3300` (updated in Spec 074 to accommodate ratchet lane IDs 3190-3222; unchanged by Specs 075, 076, and 077)

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

## Envelope Follower Parameters (Spec 059)

**File**: `plugins/ruinae/src/parameters/env_follower_params.h`
**IDs**: 2300-2302

### EnvFollowerParams Struct

```cpp
struct EnvFollowerParams {
    std::atomic<float> sensitivity{0.5f};   // [0, 1]
    std::atomic<float> attackMs{10.0f};     // [0.1, 500] ms
    std::atomic<float> releaseMs{100.0f};   // [1, 5000] ms
};
```

### Parameter Details

| ID | Name | Display | Unit | Default (Norm) | Mapping |
|----|------|---------|------|----------------|---------|
| 2300 | EF Sensitivity | "XX%" | % | 0.5 | Identity: `norm` |
| 2301 | EF Attack | "X.X ms" / "XXX ms" | ms | 0.5406 | Log: `0.1 * pow(5000, norm)` |
| 2302 | EF Release | "X.X ms" / "XXXX ms" | ms | 0.5406 | Log: `1.0 * pow(5000, norm)` |

### Attack/Release Logarithmic Mappings

```cpp
// Attack: Normalized [0, 1] -> ms [0.1, 500]
float envFollowerAttackFromNormalized(double norm) {
    return clamp(0.1f * pow(5000.0, norm), 0.1f, 500.0f);
}

// Release: Normalized [0, 1] -> ms [1, 5000]
float envFollowerReleaseFromNormalized(double norm) {
    return clamp(1.0f * pow(5000.0, norm), 1.0f, 5000.0f);
}
```

Default attack (10 ms) and release (100 ms): `norm = log(100) / log(5000) = 0.5406`.

### Engine Forwarding

```cpp
// In applyParamsToEngine():
engine_.setEnvFollowerSensitivity(envFollowerParams_.sensitivity.load(relaxed));
engine_.setEnvFollowerAttack(envFollowerParams_.attackMs.load(relaxed));
engine_.setEnvFollowerRelease(envFollowerParams_.releaseMs.load(relaxed));
```

These forward through `RuinaeEngine` -> `ModulationEngine` -> `EnvelopeFollower` (Layer 2). Sensitivity scales the follower output before routing.

### State Persistence

Env Follower state consists of 3 floats written sequentially (12 bytes):
```
[float: sensitivity] [float: attackMs] [float: releaseMs]
```

Controller loading requires inverse log mapping for attack/release:
- Sensitivity: stored as normalized [0, 1], no conversion needed
- Attack: `log(ms / 0.1) / log(5000)` converts ms back to normalized
- Release: `log(ms / 1.0) / log(5000)` converts ms back to normalized

---

## Sample & Hold Parameters (Spec 059)

**File**: `plugins/ruinae/src/parameters/sample_hold_params.h`
**IDs**: 2400-2403

### SampleHoldParams Struct

```cpp
struct SampleHoldParams {
    std::atomic<float> rateHz{4.0f};    // [0.1, 50] Hz
    std::atomic<bool>  sync{false};     // Tempo sync on/off
    std::atomic<int>   noteValue{10};   // Note value dropdown index (default 1/8)
    std::atomic<float> slewMs{0.0f};    // [0, 500] ms
};
```

### Parameter Details

| ID | Name | Display | Unit | Default (Norm) | Mapping |
|----|------|---------|------|----------------|---------|
| 2400 | S&H Rate | "X.XX Hz" | Hz | 0.702 | Log: `lfoRateFromNormalized()` clamped to [0.1, 50] Hz |
| 2401 | S&H Sync | on/off | - | 0.0 | Boolean: stepCount=1 |
| 2402 | S&H Note Value | dropdown | - | 0.5 | Discrete: `createNoteValueDropdown()` |
| 2403 | S&H Slew | "X ms" | ms | 0.0 | Linear: `norm * 500` |

### Sync Logic in applyParamsToEngine()

S&H has no built-in tempo sync in the DSP class. Sync is handled entirely at the processor level:

```cpp
// In applyParamsToEngine():
if (sampleHoldParams_.sync.load(relaxed)) {
    int noteIdx = sampleHoldParams_.noteValue.load(relaxed);
    float delayMs = dropdownToDelayMs(noteIdx, tempoBPM_);
    float rateHz = (delayMs > 0.0f) ? (1000.0f / delayMs) : 4.0f;
    engine_.setSampleHoldRate(rateHz);
} else {
    engine_.setSampleHoldRate(sampleHoldParams_.rateHz.load(relaxed));
}
engine_.setSampleHoldSlew(sampleHoldParams_.slewMs.load(relaxed));
```

When Sync is ON, the NoteValue dropdown index is converted to a delay in ms via `dropdownToDelayMs()`, then to Hz. If the tempo is invalid (0 BPM) or delay is <= 0, a 4 Hz fallback is used. When Sync is OFF, the Rate knob value is used directly.

### State Persistence

S&H state consists of 2 floats + 2 int32s written sequentially (16 bytes):
```
[float: rateHz] [int32: sync (0 or 1)] [int32: noteValue] [float: slewMs]
```

Controller loading requires inverse mapping:
- Rate: `lfoRateToNormalized(hz)` converts Hz back to normalized
- Sync: `int32 != 0 ? 1.0 : 0.0` converts to boolean normalized
- Note Value: `index / stepCount` converts index to normalized
- Slew: `ms / 500.0` converts ms back to normalized

---

## Random Parameters (Spec 059)

**File**: `plugins/ruinae/src/parameters/random_params.h`
**IDs**: 2500-2503

### RandomParams Struct

```cpp
struct RandomParams {
    std::atomic<float> rateHz{4.0f};      // [0.1, 50] Hz
    std::atomic<bool>  sync{false};       // Tempo sync on/off
    std::atomic<int>   noteValue{10};     // Note value dropdown index (default 1/8)
    std::atomic<float> smoothness{0.0f};  // [0, 1]
};
```

### Parameter Details

| ID | Name | Display | Unit | Default (Norm) | Mapping |
|----|------|---------|------|----------------|---------|
| 2500 | Rnd Rate | "X.XX Hz" | Hz | 0.702 | Log: `lfoRateFromNormalized()` clamped to [0.1, 50] Hz |
| 2501 | Rnd Sync | on/off | - | 0.0 | Boolean: stepCount=1 |
| 2502 | Rnd Note Value | dropdown | - | 0.5 | Discrete: `createNoteValueDropdown()` |
| 2503 | Rnd Smoothness | "XX%" | % | 0.0 | Identity: `norm` |

### Sync Logic in applyParamsToEngine()

Random sync uses the same processor-level NoteValue-to-Hz conversion as S&H, bypassing RandomSource's built-in `setTempoSync()`/`setTempo()` methods for consistent UX:

```cpp
// In applyParamsToEngine():
if (randomParams_.sync.load(relaxed)) {
    int noteIdx = randomParams_.noteValue.load(relaxed);
    float delayMs = dropdownToDelayMs(noteIdx, tempoBPM_);
    float rateHz = (delayMs > 0.0f) ? (1000.0f / delayMs) : 4.0f;
    engine_.setRandomRate(rateHz);
} else {
    engine_.setRandomRate(randomParams_.rateHz.load(relaxed));
}
engine_.setRandomSmoothness(randomParams_.smoothness.load(relaxed));
```

**Design decision**: RandomSource has built-in `setTempoSync(bool)` and `setTempo(float bpm)` methods, but they only support BPM-based rate scaling without NoteValue selection. To maintain consistent UX across all tempo-syncable sources (LFO, Chaos, S&H, Random), the plugin layer bypasses the built-in sync and computes Hz directly from NoteValue + BPM. The `setRandomTempoSync()` and `setRandomTempo()` methods are NOT forwarded through RuinaeEngine.

### State Persistence

Random state consists of 2 floats + 2 int32s written sequentially (16 bytes):
```
[float: rateHz] [int32: sync (0 or 1)] [int32: noteValue] [float: smoothness]
```

Controller loading follows the same pattern as S&H.

---

## Pitch Follower Parameters (Spec 059)

**File**: `plugins/ruinae/src/parameters/pitch_follower_params.h`
**IDs**: 2600-2603

### PitchFollowerParams Struct

```cpp
struct PitchFollowerParams {
    std::atomic<float> minHz{80.0f};       // [20, 500] Hz
    std::atomic<float> maxHz{2000.0f};     // [200, 5000] Hz
    std::atomic<float> confidence{0.5f};   // [0, 1]
    std::atomic<float> speedMs{50.0f};     // [10, 300] ms
};
```

### Parameter Details

| ID | Name | Display | Unit | Default (Norm) | Mapping |
|----|------|---------|------|----------------|---------|
| 2600 | PF Min Hz | "XXX Hz" | Hz | 0.4307 | Log: `20 * pow(25, norm)` |
| 2601 | PF Max Hz | "XXXX Hz" | Hz | 0.7153 | Log: `200 * pow(25, norm)` |
| 2602 | PF Confidence | "XX%" | % | 0.5 | Identity: `norm` |
| 2603 | PF Speed | "XX ms" | ms | 0.1379 | Linear: `10 + norm * 290` |

### Frequency Mappings

```cpp
// Min Hz: Normalized [0, 1] -> Hz [20, 500]
float pitchFollowerMinHzFromNormalized(double norm) {
    return clamp(20.0f * pow(25.0, norm), 20.0f, 500.0f);
}

// Max Hz: Normalized [0, 1] -> Hz [200, 5000]
float pitchFollowerMaxHzFromNormalized(double norm) {
    return clamp(200.0f * pow(25.0, norm), 200.0f, 5000.0f);
}
```

Default min (80 Hz): `norm = log(80/20) / log(25) = 0.4307`. Default max (2000 Hz): `norm = log(2000/200) / log(25) = 0.7153`.

### Engine Forwarding

```cpp
// In applyParamsToEngine():
engine_.setPitchFollowerMinHz(pitchFollowerParams_.minHz.load(relaxed));
engine_.setPitchFollowerMaxHz(pitchFollowerParams_.maxHz.load(relaxed));
engine_.setPitchFollowerConfidence(pitchFollowerParams_.confidence.load(relaxed));
engine_.setPitchFollowerTrackingSpeed(pitchFollowerParams_.speedMs.load(relaxed));
```

These forward through `RuinaeEngine` -> `ModulationEngine` -> `PitchFollowerSource` (Layer 2). Min/Max Hz define the pitch detection frequency range. Confidence sets the minimum detection threshold to update output. Speed controls smoothing for pitch changes.

### State Persistence

Pitch Follower state consists of 4 floats written sequentially (16 bytes):
```
[float: minHz] [float: maxHz] [float: confidence] [float: speedMs]
```

Controller loading requires inverse mapping:
- Min Hz: `log(hz / 20) / log(25)` converts Hz back to normalized
- Max Hz: `log(hz / 200) / log(25)` converts Hz back to normalized
- Confidence: stored as normalized [0, 1], no conversion needed
- Speed: `(ms - 10) / 290` converts ms back to normalized

---

## Transient Detector Parameters (Spec 059)

**File**: `plugins/ruinae/src/parameters/transient_params.h`
**IDs**: 2700-2702

### TransientParams Struct

```cpp
struct TransientParams {
    std::atomic<float> sensitivity{0.5f};  // [0, 1]
    std::atomic<float> attackMs{2.0f};     // [0.5, 10] ms
    std::atomic<float> decayMs{50.0f};     // [20, 200] ms
};
```

### Parameter Details

| ID | Name | Display | Unit | Default (Norm) | Mapping |
|----|------|---------|------|----------------|---------|
| 2700 | Trn Sensitivity | "XX%" | % | 0.5 | Identity: `norm` |
| 2701 | Trn Attack | "X.X ms" | ms | 0.1579 | Linear: `0.5 + norm * 9.5` |
| 2702 | Trn Decay | "XXX ms" | ms | 0.1667 | Linear: `20 + norm * 180` |

### Linear Mappings

```cpp
// Attack: Normalized [0, 1] -> ms [0.5, 10]
float transientAttackFromNormalized(double norm) {
    return clamp(0.5f + static_cast<float>(norm) * 9.5f, 0.5f, 10.0f);
}

// Decay: Normalized [0, 1] -> ms [20, 200]
float transientDecayFromNormalized(double norm) {
    return clamp(20.0f + static_cast<float>(norm) * 180.0f, 20.0f, 200.0f);
}
```

Default attack (2 ms): `norm = (2 - 0.5) / 9.5 = 0.1579`. Default decay (50 ms): `norm = (50 - 20) / 180 = 0.1667`.

### Engine Forwarding

```cpp
// In applyParamsToEngine():
engine_.setTransientSensitivity(transientParams_.sensitivity.load(relaxed));
engine_.setTransientAttack(transientParams_.attackMs.load(relaxed));
engine_.setTransientDecay(transientParams_.decayMs.load(relaxed));
```

These forward through `RuinaeEngine` -> `ModulationEngine` -> `TransientDetector` (Layer 2). Sensitivity controls how easily transients are detected. Attack/Decay shape the output envelope.

### State Persistence

Transient state consists of 3 floats written sequentially (12 bytes):
```
[float: sensitivity] [float: attackMs] [float: decayMs]
```

Controller loading requires inverse mapping:
- Sensitivity: stored as normalized [0, 1], no conversion needed
- Attack: `(ms - 0.5) / 9.5` converts ms back to normalized
- Decay: `(ms - 20) / 180` converts ms back to normalized

---

## Arpeggiator Parameters (Spec 071)

**File**: `plugins/ruinae/src/parameters/arpeggiator_params.h`
**IDs**: 3000-3010 (base), 3020-3132 (lanes) -- see [Lane Parameters section](#arpeggiator-lane-parameters-spec-072) below

### Purpose

Atomic thread-safe bridge for arpeggiator parameters between the UI/host thread (writes normalized values via `processParameterChanges`) and the audio thread (reads plain values in `applyParamsToEngine` and forwards them to `ArpeggiatorCore`). Follows the same parameter pack pattern as all other Ruinae sections (`trance_gate_params.h` is the direct reference pattern).

### ArpeggiatorParams Struct

```cpp
struct ArpeggiatorParams {
    std::atomic<bool>  enabled{false};
    std::atomic<int>   mode{0};              // 0=Up..9=Chord (ArpMode enum)
    std::atomic<int>   octaveRange{1};       // 1-4
    std::atomic<int>   octaveMode{0};        // 0=Sequential, 1=Interleaved (OctaveMode enum)
    std::atomic<bool>  tempoSync{true};
    std::atomic<int>   noteValue{10};        // Index into note value dropdown (default 1/8 note)
    std::atomic<float> freeRate{4.0f};       // 0.5-50 Hz
    std::atomic<float> gateLength{80.0f};    // 1-200%
    std::atomic<float> swing{0.0f};          // 0-75%
    std::atomic<int>   latchMode{0};         // 0=Off, 1=Hold, 2=Add (LatchMode enum)
    std::atomic<int>   retrigger{0};         // 0=Off, 1=Note, 2=Beat (ArpRetriggerMode enum)
};
```

### Public API (6 Functions)

| Function | Purpose | Called From |
|----------|---------|-------------|
| `handleArpParamChange(params, id, value)` | Denormalize VST 0-1 to plain values, store with `memory_order_relaxed` | `Processor::processParameterChanges()` |
| `registerArpParams(parameters)` | Register all 11 parameters with `kCanAutomate` flag and readable names | `Controller::initialize()` |
| `formatArpParam(id, valueNormalized, string)` | Produce human-readable display strings for each parameter | `Controller::getParamStringByValue()` |
| `saveArpParams(params, streamer)` | Serialize 11 fields to IBStreamer in fixed order | `Processor::getState()` |
| `loadArpParams(params, streamer)` | Deserialize 11 fields from IBStreamer; returns false gracefully on truncated stream (backward compat) | `Processor::setState()` |
| `loadArpParamsToController(streamer, setParam)` | Read 11 fields and call `setParam(id, normalizedValue)` for each, converting plain to normalized | `Controller::setComponentState()` |

### Parameter Details

| ID | Name | Display | Type | Default (Norm) | Mapping |
|----|------|---------|------|----------------|---------|
| 3000 | Arp Enabled | on/off | ToggleParameter | 0.0 (off) | Boolean: `norm >= 0.5` |
| 3001 | Arp Mode | StringListParameter | Up/Down/.../Chord | 0.0 (Up) | Discrete: 10 entries, stepCount=9 |
| 3002 | Arp Octave Range | "1"/"2"/"3"/"4" | RangeParameter | 0.0 (1 oct) | Discrete: `1 + round(norm * 3)`, stepCount=3 |
| 3003 | Arp Octave Mode | StringListParameter | Sequential/Interleaved | 0.0 (Sequential) | Discrete: 2 entries, stepCount=1 |
| 3004 | Arp Tempo Sync | on/off | ToggleParameter | 1.0 (on) | Boolean: `norm >= 0.5` |
| 3005 | Arp Note Value | dropdown | `createNoteValueDropdown()` | 0.5 (1/8 note) | Discrete: 21 entries, stepCount=20 |
| 3006 | Arp Free Rate | "X.X Hz" | Parameter | 0.0707 (4 Hz) | Linear: `0.5 + norm * 49.5` |
| 3007 | Arp Gate Length | "XX%" | Parameter | 0.3960 (80%) | Linear: `1 + norm * 199` |
| 3008 | Arp Swing | "X%" | Parameter | 0.0 (0%) | Linear: `norm * 75` |
| 3009 | Arp Latch Mode | StringListParameter | Off/Hold/Add | 0.0 (Off) | Discrete: 3 entries, stepCount=2 |
| 3010 | Arp Retrigger | StringListParameter | Off/Note/Beat | 0.0 (Off) | Discrete: 3 entries, stepCount=2 |

### Format Strings

| Parameter | Examples |
|-----------|----------|
| Mode | "Up", "Down", "UpDown", "DownUp", "Converge", "Diverge", "Random", "Walk", "AsPlayed", "Chord" |
| Octave Range | "1", "2", "3", "4" |
| Octave Mode | "Sequential", "Interleaved" |
| Free Rate | "4.0 Hz" |
| Gate Length | "80%" |
| Swing | "0%" |
| Latch Mode | "Off", "Hold", "Add" |
| Retrigger | "Off", "Note", "Beat" |
| Note Value | Same strings as trance gate (from `note_value_ui.h`) |

### Engine Forwarding

```cpp
// In applyParamsToEngine():
arpCore_.setMode(static_cast<Krate::DSP::ArpMode>(arpParams_.mode.load(relaxed)));
arpCore_.setOctaveRange(arpParams_.octaveRange.load(relaxed));
arpCore_.setOctaveMode(static_cast<Krate::DSP::OctaveMode>(arpParams_.octaveMode.load(relaxed)));
arpCore_.setTempoSync(arpParams_.tempoSync.load(relaxed));
auto noteMapping = Krate::DSP::getNoteValueFromDropdown(arpParams_.noteValue.load(relaxed));
arpCore_.setNoteValue(noteMapping.noteValue, noteMapping.modifier);
arpCore_.setFreeRate(arpParams_.freeRate.load(relaxed));
arpCore_.setGateLength(arpParams_.gateLength.load(relaxed));
arpCore_.setSwing(arpParams_.swing.load(relaxed));  // Takes 0-75 percent as-is, NOT normalized
arpCore_.setLatchMode(static_cast<Krate::DSP::LatchMode>(arpParams_.latchMode.load(relaxed)));
arpCore_.setRetrigger(static_cast<Krate::DSP::ArpRetriggerMode>(arpParams_.retrigger.load(relaxed)));
arpCore_.setEnabled(arpParams_.enabled.load(relaxed));  // MUST be called LAST (may queue cleanup note-offs)
```

### State Persistence

Arpeggiator state consists of 7 int32s + 3 floats + 1 int32 written sequentially:
```
[int32: enabled] [int32: mode] [int32: octaveRange] [int32: octaveMode]
[int32: tempoSync] [int32: noteValue] [float: freeRate] [float: gateLength]
[float: swing] [int32: latchMode] [int32: retrigger]
```

`loadArpParams()` returns `false` without corrupting state when the stream ends early (backward compatibility with presets saved before spec 071). The arp params are appended after the last existing state data (harmonizer enable flag).

Controller loading requires inverse mapping:
- Enabled/TempoSync: `bool ? 1.0 : 0.0` converts to boolean normalized
- Mode: `index / 9.0` converts index to normalized [0, 1]
- Octave Range: `(range - 1) / 3.0` converts 1-4 to normalized [0, 1]
- Octave Mode: `index / 1.0` (identity) converts index to normalized
- Note Value: `index / 20.0` converts index to normalized
- Free Rate: `(hz - 0.5) / 49.5` converts Hz back to normalized
- Gate Length: `(pct - 1.0) / 199.0` converts percentage back to normalized
- Swing: `pct / 75.0` converts percentage back to normalized
- Latch Mode: `index / 2.0` converts index to normalized
- Retrigger: `index / 2.0` converts index to normalized

### When to Use This

Extend `ArpeggiatorParams` when adding new arpeggiator features:
- **Phase 4 (Independent Lane Architecture)**: Lane step parameters added in 3020-3132 ID range (done -- see below)
- **Phase 5 (Per-Step Modifiers)**: Modifier lane parameters added in 3140-3181 ID range (done -- see [Modifier Parameters section](#arpeggiator-modifier-parameters-spec-073) below)
- **Phase 6 (Ratcheting)**: Ratchet lane parameters added in 3190-3222 ID range (done -- see [Ratchet Lane Parameters section](#arpeggiator-ratchet-lane-parameters-spec-074) below)
- **Phase 7 (Euclidean Timing)**: Euclidean parameters added in 3230-3233 ID range (done -- see [Euclidean Parameters section](#arpeggiator-euclidean-parameters-spec-075) below)
- **Phase 8 (Conditional Trig)**: Condition lane parameters added in 3240-3272 ID range + fill toggle at 3280 (done -- see [Condition Lane Parameters section](#arpeggiator-condition-lane-parameters-spec-076) below)
- **Phase 9 (Spice/Dice + Humanize)**: Spice/Dice/Humanize parameters added in 3290-3292 ID range (done -- see [Spice/Dice/Humanize Parameters section](#arpeggiator-spicedicehumanize-parameters-spec-077) below)
- **Phase 9.5 (Ratchet Swing)**: Ratchet swing parameter added at ID 3293 (done -- see [Ratchet Swing Parameter section](#arpeggiator-ratchet-swing-parameter) below)
- **Phase 10 (Modulation Integration)**: Expose arp params as modulation destinations (done -- no new parameter IDs; 5 arp params exposed as mod destinations via RuinaeModDest enum extension + processor-side mod offset application in `applyParamsToEngine()`; see [Arp Modulation Destination Pattern](plugin-architecture.md#arp-modulation-destination-pattern-spec-078))
- **Phase 11 (Full Arp UI)**: UI changes only, no parameter pack changes expected

The sentinel `kArpEndId = 3299` accommodates future lane parameters and arp extensions without requiring an ID allocation change.

---

## Arpeggiator Lane Parameters (Spec 072)

**File**: `plugins/ruinae/src/parameters/arpeggiator_params.h`
**IDs**: 3020-3132 (99 parameters across 3 lanes)

### Purpose

Per-step lane parameters for independent polymetric lane cycling in the arpeggiator. Each lane has a length parameter (how many steps before cycling) and 32 step value parameters. The lanes advance independently, creating polymetric patterns when lanes have different lengths.

Follows the `trance_gate_params.h` pattern of registering per-step parameters in a loop with `kCanAutomate | kIsHidden` flags (step params are automated but not shown in generic host UIs).

### Lane Parameter ID Allocation

| Range | Lane | Length ID | Step 0 ID | Step 31 ID | Count |
|-------|------|-----------|-----------|------------|-------|
| 3020-3052 | Velocity | 3020 | 3021 | 3052 | 33 |
| 3053-3059 | *(reserved for velocity lane metadata)* | - | - | - | 0 |
| 3060-3092 | Gate | 3060 | 3061 | 3092 | 33 |
| 3093-3099 | *(reserved for gate lane metadata)* | - | - | - | 0 |
| 3100-3132 | Pitch | 3100 | 3101 | 3132 | 33 |
| 3133-3199 | *(reserved for future phases 5-8)* | - | - | - | 0 |

**Note**: ID 3100 was formerly the `kNumParameters` sentinel value. The sentinel was simultaneously updated to 3200 when pitch lane IDs were allocated, so there is no collision.

### Velocity Lane Parameters (3020-3052)

| ID | Name | Type | Range | Default | Flags |
|----|------|------|-------|---------|-------|
| 3020 | Arp Vel Lane Len | Discrete (int) | 1-32 | 1 | `kCanAutomate` |
| 3021-3052 | Arp Vel Step 1-32 | Continuous (float) | 0.0-1.0 | 1.0 | `kCanAutomate \| kIsHidden` |

**Denormalization**: Length: `1 + round(norm * 31)`, stepCount=31. Steps: identity (norm = value).

### Gate Lane Parameters (3060-3092)

| ID | Name | Type | Range | Default | Flags |
|----|------|------|-------|---------|-------|
| 3060 | Arp Gate Lane Len | Discrete (int) | 1-32 | 1 | `kCanAutomate` |
| 3061-3092 | Arp Gate Step 1-32 | Continuous (float) | 0.01-2.0 | 1.0 | `kCanAutomate \| kIsHidden` |

**Denormalization**: Length: `1 + round(norm * 31)`, stepCount=31. Steps: `0.01 + norm * 1.99`, stepCount=0 (continuous).

### Pitch Lane Parameters (3100-3132)

| ID | Name | Type | Range | Default | Flags |
|----|------|------|-------|---------|-------|
| 3100 | Arp Pitch Lane Len | Discrete (int) | 1-32 | 1 | `kCanAutomate` |
| 3101-3132 | Arp Pitch Step 1-32 | Discrete (int) | -24 to +24 | 0 | `kCanAutomate \| kIsHidden` |

**Denormalization**: Length: `1 + round(norm * 31)`, stepCount=31. Steps: `-24 + round(norm * 48)`, stepCount=48.

### ArpeggiatorParams Struct Extension

```cpp
struct ArpeggiatorParams {
    // ... existing 11 base arp fields (Spec 071) ...

    // Velocity lane (Spec 072)
    std::atomic<int> velocityLaneLength{1};               // [1, 32]
    std::array<std::atomic<float>, 32> velocityLaneSteps{};  // [0.0, 1.0], init to 1.0f

    // Gate lane (Spec 072)
    std::atomic<int> gateLaneLength{1};                   // [1, 32]
    std::array<std::atomic<float>, 32> gateLaneSteps{};   // [0.01, 2.0], init to 1.0f

    // Pitch lane (Spec 072)
    std::atomic<int> pitchLaneLength{1};                  // [1, 32]
    std::array<std::atomic<int>, 32> pitchLaneSteps{};    // [-24, +24], init to 0
};
```

**Note:** Pitch lane steps use `std::atomic<int>` (not `std::atomic<int8_t>`) for guaranteed lock-free operation. The conversion to `int8_t` for `ArpLane<int8_t>::setStep()` happens at the DSP boundary in `processor.cpp::applyParamsToArp()`.

### Format Strings

| Parameter | Examples |
|-----------|----------|
| Velocity Lane Length | "1 steps", "4 steps", "32 steps" |
| Velocity Lane Steps | "100%", "70%", "30%" |
| Gate Lane Length | "1 steps", "3 steps" |
| Gate Lane Steps | "0.50x", "1.00x", "1.50x" |
| Pitch Lane Length | "1 steps", "7 steps" |
| Pitch Lane Steps | "+7 st", "-5 st", "0 st" |

### Engine Forwarding

```cpp
// In applyParamsToArp():
int velLen = arpParams_.velocityLaneLength.load(relaxed);
arp_.velocityLane().setLength(static_cast<size_t>(velLen));
for (int i = 0; i < 32; ++i)
    arp_.velocityLane().setStep(i, arpParams_.velocityLaneSteps[i].load(relaxed));

int gateLen = arpParams_.gateLaneLength.load(relaxed);
arp_.gateLane().setLength(static_cast<size_t>(gateLen));
for (int i = 0; i < 32; ++i)
    arp_.gateLane().setStep(i, arpParams_.gateLaneSteps[i].load(relaxed));

int pitchLen = arpParams_.pitchLaneLength.load(relaxed);
arp_.pitchLane().setLength(static_cast<size_t>(pitchLen));
for (int i = 0; i < 32; ++i) {
    int val = std::clamp(arpParams_.pitchLaneSteps[i].load(relaxed), -24, 24);
    arp_.pitchLane().setStep(i, static_cast<int8_t>(val));
}
```

### State Persistence

Lane data (396 bytes) is appended after the 11 base arp parameters:
```
[int32: velocityLaneLength] [float x32: velocityLaneSteps]    // 132 bytes
[int32: gateLaneLength]     [float x32: gateLaneSteps]        // 132 bytes
[int32: pitchLaneLength]    [int32 x32: pitchLaneSteps]       // 132 bytes
```

Loading uses EOF-safe pattern: if the stream ends mid-lane, remaining lanes keep their struct defaults (velocity/gate steps = 1.0, pitch steps = 0). Phase 3 presets (no lane data) load with all lanes at identity values automatically.

---

## Arpeggiator Modifier Parameters (Spec 073)

**File**: `plugins/ruinae/src/parameters/arpeggiator_params.h`
**IDs**: 3140-3181 (35 parameters: 1 length + 32 steps + 2 config)

### Purpose

Per-step modifier flags for TB-303-inspired Rest, Tie, Slide, and Accent behavior. Each step stores a `uint8_t` bitmask of `ArpStepFlags` values. The modifier lane advances independently of the velocity, gate, and pitch lanes, enabling polymetric modifier patterns.

### Modifier Parameter ID Allocation

| ID | Name | Type | Range | Default | Flags |
|----|------|------|-------|---------|-------|
| 3140 | Arp Mod Lane Len | Discrete (int) | 1-32 | 1 | `kCanAutomate` |
| 3141-3172 | Arp Mod Step 0-31 | Discrete (int) | 0-255 | 1 (kStepActive) | `kCanAutomate \| kIsHidden` |
| 3173-3179 | *(reserved)* | - | - | - | - |
| 3180 | Arp Accent Velocity | Discrete (int) | 0-127 | 30 | `kCanAutomate` |
| 3181 | Arp Slide Time | Continuous (float) | 0-500 ms | 60 ms (norm: 0.12) | `kCanAutomate` |
| 3182-3189 | *(reserved)* | - | - | - | - |

**Note**: 35 new parameters total. `kArpEndId = 3199` and `kNumParameters = 3200` are unchanged -- all modifier IDs fit within the existing reserved range (3133-3199). Step parameters (3141-3172) have `kIsHidden` (not shown in generic host UIs, consistent with Phase 4 lane step params). Length (3140) and config params (3180, 3181) are visible.

### Denormalization

| Parameter | Formula | Step Count |
|-----------|---------|------------|
| Modifier Lane Length | `1 + round(norm * 31)` | 31 |
| Modifier Lane Steps | `round(norm * 255)` | 255 |
| Accent Velocity | `round(norm * 127)` | 127 |
| Slide Time | `norm * 500.0f` | 0 (continuous) |

### ArpeggiatorParams Struct Extension

```cpp
struct ArpeggiatorParams {
    // ... existing 11 base arp fields (Spec 071) ...
    // ... existing velocity/gate/pitch lane fields (Spec 072) ...

    // Modifier lane (Spec 073)
    std::atomic<int> modifierLaneLength{1};                  // [1, 32]
    std::atomic<int> modifierLaneSteps[32];                  // [0, 255], init to 1 (kStepActive)
    std::atomic<int> accentVelocity{30};                     // [0, 127]
    std::atomic<float> slideTime{60.0f};                     // [0.0, 500.0] ms
};
```

**Note:** Modifier lane steps use `std::atomic<int>` (not `std::atomic<uint8_t>`) for guaranteed lock-free operation. The conversion to `uint8_t` for `ArpLane<uint8_t>::setStep()` happens at the DSP boundary in `processor.cpp::applyParamsToArp()`.

### Format Strings

| Parameter | Examples |
|-----------|----------|
| Modifier Lane Length | "1 steps", "4 steps", "32 steps" |
| Modifier Lane Steps | "0x01", "0x0F", "0x00" |
| Accent Velocity | "0", "30", "127" |
| Slide Time | "0 ms", "60 ms", "500 ms" |

### Engine Forwarding

```cpp
// In applyParamsToArp():
// Expand-write-shrink pattern (same as velocity/gate/pitch lanes):
arp_.modifierLane().setLength(32);  // expand to allow writing all 32 indices
for (int i = 0; i < 32; ++i)
    arp_.modifierLane().setStep(i, static_cast<uint8_t>(arpParams_.modifierLaneSteps[i].load(relaxed)));
arp_.modifierLane().setLength(static_cast<size_t>(arpParams_.modifierLaneLength.load(relaxed)));  // shrink

arp_.setAccentVelocity(arpParams_.accentVelocity.load(relaxed));
arp_.setSlideTime(arpParams_.slideTime.load(relaxed));
engine_.setPortamentoTime(arpParams_.slideTime.load(relaxed));  // forwarded to engine unconditionally
```

---

## Arpeggiator Ratchet Lane Parameters (Spec 074)

**File**: `plugins/ruinae/src/parameters/arpeggiator_params.h`
**IDs**: 3190-3222 (33 parameters: 1 length + 32 steps)

### Purpose

Per-step ratchet counts for sub-step retriggering within arp steps. Each step stores a subdivision count (1-4), where 1 means normal playback and 2-4 produce rapid retriggered repetitions within the step's duration. The ratchet lane advances independently of all other lanes, participating in the polymetric lane system.

### Ratchet Parameter ID Allocation

| ID | Name | Type | Range | Default | Flags |
|----|------|------|-------|---------|-------|
| 3190 | Arp Ratchet Lane Len | Discrete (int) | 1-32 | 1 | `kCanAutomate` |
| 3191-3222 | Arp Ratch Step 0-31 | Discrete (int) | 1-4 | 1 | `kCanAutomate \| kIsHidden` |

**Denormalization**: Length: `1 + round(norm * 31)`, stepCount=31. Steps: `1 + round(norm * 3)`, stepCount=3.

### ArpeggiatorParams Struct Extension

```cpp
struct ArpeggiatorParams {
    // ... existing base + velocity/gate/pitch lane + modifier lane fields ...

    // Ratchet lane (Spec 074)
    std::atomic<int> ratchetLaneLength{1};                  // [1, 32]
    std::atomic<int> ratchetLaneSteps[32];                  // [1, 4], init to 1
};
```

### Format Strings

| Parameter | Examples |
|-----------|----------|
| Ratchet Lane Length | "1 steps", "4 steps", "32 steps" |
| Ratchet Lane Steps | "1x", "2x", "3x", "4x" |

### Engine Forwarding

```cpp
// In applyParamsToArp():
// Expand-write-shrink pattern (same as other lanes):
arp_.ratchetLane().setLength(32);
for (int i = 0; i < 32; ++i) {
    int val = std::clamp(arpParams_.ratchetLaneSteps[i].load(relaxed), 1, 4);
    arp_.ratchetLane().setStep(i, static_cast<uint8_t>(val));
}
arp_.ratchetLane().setLength(static_cast<size_t>(arpParams_.ratchetLaneLength.load(relaxed)));
```

---

## Arpeggiator Ratchet Swing Parameter

**File**: `plugins/ruinae/src/parameters/arpeggiator_params.h`
**ID**: 3293

### Purpose

A continuous "ratchet swing" parameter that applies a long-short ratio to consecutive pairs of ratcheted sub-steps. At 50% (default), sub-steps are equally spaced (identical to pre-swing behavior). At 67%, the feel is triplet-like. At 75%, the feel is dotted-note-like. Only affects steps with ratchet count >= 2; ratchet count 1 is unaffected.

### Parameter ID

| ID | Name | Type | Range | Default (Norm) | Flags |
|----|------|------|-------|----------------|-------|
| 3293 | Arp Ratchet Swing | Continuous | 50%-75% | 0.0 (= 50%) | `kCanAutomate` |

**Denormalization**: `50.0 + value * 25.0` (maps normalized 0.0-1.0 to 50%-75%).

### ArpeggiatorParams Struct Extension

```cpp
struct ArpeggiatorParams {
    // ... existing fields ...

    // Ratchet swing
    std::atomic<float> ratchetSwing{50.0f};  // [50.0, 75.0] percent
};
```

### Format String

| Parameter | Examples |
|-----------|----------|
| Ratchet Swing | "50%", "67%", "75%" |

### Engine Forwarding

```cpp
// In applyParamsToArp():
arpCore_.setRatchetSwing(arpParams_.ratchetSwing.load(std::memory_order_relaxed));
```

### Core Math

For a pair of sub-steps with combined duration `pairDuration = 2 * baseDuration`:
- **Long** sub-step (even index): `round(pairDuration * swingRatio)`
- **Short** sub-step (odd index): `pairDuration - longDuration` (exact complement)

Per ratchet count:
| Count | Sub-step durations |
|-------|--------------------|
| 1 | No effect |
| 2 | `[long, short]` — one pair |
| 3 | `[long, short, base]` — one pair + unpaired remainder |
| 4 | `[long, short, long, short]` — two pairs |

---

## Arpeggiator Euclidean Parameters (Spec 075)

**File**: `plugins/ruinae/src/parameters/arpeggiator_params.h`
**IDs**: 3230-3233 (4 parameters)

### Purpose

Euclidean timing controls for Bjorklund-algorithm rhythmic gating. The Euclidean pattern determines which arp steps fire notes (hits) and which are silent (rests). Three parameters (Hits, Steps, Rotation) define the pattern; a fourth (Enabled) toggles the mode on/off. When disabled, all steps fire normally (Phase 6 behavior).

### Euclidean Parameter ID Allocation

| ID | Name | Type | Range | Default (Norm) | Flags |
|----|------|------|-------|----------------|-------|
| 3230 | Arp Euclidean | Toggle | 0-1 | 0.0 (Off) | `kCanAutomate` |
| 3231 | Arp Euclidean Hits | RangeParameter | 0-32 | 4 (norm: 4/32) | `kCanAutomate` |
| 3232 | Arp Euclidean Steps | RangeParameter | 2-32 | 8 (norm: 6/30) | `kCanAutomate` |
| 3233 | Arp Euclidean Rotation | RangeParameter | 0-31 | 0 (norm: 0.0) | `kCanAutomate` |

All 4 parameters have `kCanAutomate` and none have `kIsHidden` -- all are user-facing controls. The UI for Euclidean controls (Hits/Steps/Rotation knobs with visual pattern display) is deferred to Phase 11.

### Denormalization

| Parameter | Formula | Step Count |
|-----------|---------|------------|
| Euclidean Enabled | `norm >= 0.5` | 1 |
| Euclidean Hits | `clamp(round(norm * 32), 0, 32)` | 32 |
| Euclidean Steps | `clamp(2 + round(norm * 30), 2, 32)` | 30 |
| Euclidean Rotation | `clamp(round(norm * 31), 0, 31)` | 31 |

### ArpeggiatorParams Struct Extension

```cpp
struct ArpeggiatorParams {
    // ... existing base + lane + modifier + ratchet fields ...

    // Euclidean Timing (Spec 075)
    std::atomic<bool> euclideanEnabled{false};    // default off
    std::atomic<int>  euclideanHits{4};           // default 4
    std::atomic<int>  euclideanSteps{8};          // default 8
    std::atomic<int>  euclideanRotation{0};       // default 0
};
```

### Format Strings

| Parameter | Examples |
|-----------|----------|
| Euclidean Enabled | "Off", "On" |
| Euclidean Hits | "0 hits", "3 hits", "5 hits", "32 hits" |
| Euclidean Steps | "2 steps", "8 steps", "16 steps", "32 steps" |
| Euclidean Rotation | "0", "3", "7", "31" |

### Engine Forwarding

```cpp
// In applyParamsToEngine():
// Prescribed call order: steps -> hits -> rotation -> enabled (FR-032)
// Steps must be set before hits so clamping uses the correct step count.
// Enabled must be set last so gating activates only after pattern is fully computed.
arpCore_.setEuclideanSteps(arpParams_.euclideanSteps.load(relaxed));
arpCore_.setEuclideanHits(arpParams_.euclideanHits.load(relaxed));
arpCore_.setEuclideanRotation(arpParams_.euclideanRotation.load(relaxed));
arpCore_.setEuclideanEnabled(arpParams_.euclideanEnabled.load(relaxed));
```

---

## Arpeggiator Condition Lane Parameters (Spec 076)

**File**: `plugins/ruinae/src/parameters/arpeggiator_params.h`
**IDs**: 3240-3272 (condition lane: 1 length + 32 steps), 3280 (fill toggle) -- 34 parameters total

### Purpose

Per-step conditional trigger parameters for Elektron-inspired pattern evolution. Each step stores a `TrigCondition` enum value (0-17) determining whether the step fires. The condition lane cycles independently of all other lanes, enabling polymetric conditional patterns. A fill toggle parameter provides real-time performance control for Fill/NotFill conditions.

### Condition Lane Parameter ID Allocation

| ID | Name | Type | Range | Default | Flags |
|----|------|------|-------|---------|-------|
| 3240 | Arp Cond Lane Len | Discrete (int) | 1-32 | 1 | `kCanAutomate` |
| 3241-3272 | Arp Cond Step 0-31 | Discrete (int) | 0-17 | 0 (Always) | `kCanAutomate \| kIsHidden` |
| 3280 | Arp Fill | Toggle | 0-1 | 0 (Off) | `kCanAutomate` |

**Reserved gaps**:
- 3234-3239: Reserved gap before condition lane (reserved for use before Phase 9)
- 3273-3279: Reserved gap between condition step IDs and fill toggle (reserved for future condition-lane extensions)

### Denormalization

| Parameter | Formula | Step Count |
|-----------|---------|------------|
| Condition Lane Length | `1 + round(norm * 31)` | 31 |
| Condition Lane Steps | `round(norm * 17)` | 17 |
| Fill Toggle | `norm >= 0.5` | 1 |

### ArpeggiatorParams Struct Extension

```cpp
struct ArpeggiatorParams {
    // ... existing base + lane + modifier + ratchet + euclidean fields ...

    // Condition Lane (Spec 076)
    std::atomic<int>   conditionLaneLength{1};              // [1, 32]
    std::array<std::atomic<int>, 32> conditionLaneSteps{};  // [0, 17] (TrigCondition, int for lock-free)
    std::atomic<bool>  fillToggle{false};                   // Fill mode toggle
};
```

**Note:** Condition lane steps use `std::atomic<int>` (not `std::atomic<uint8_t>`) for guaranteed lock-free operation. The conversion to `uint8_t` for `ArpLane<uint8_t>::setStep()` happens at the DSP boundary in `processor.cpp::applyParamsToEngine()`, with clamping to [0, 17].

### Format Strings

| Parameter | Examples |
|-----------|----------|
| Condition Lane Length | "1 step", "4 steps", "32 steps" (singular "step" when length == 1) |
| Condition Lane Steps | "Always", "10%", "25%", "50%", "75%", "90%", "1:2", "2:2", "1:3", "2:3", "3:3", "1:4", "2:4", "3:4", "4:4", "1st", "Fill", "!Fill" |
| Fill Toggle | "Off", "On" |

The 18 condition display strings are stored in a static `const char* const kCondNames[]` array (stack-local, allocation-free).

### Engine Forwarding

```cpp
// In applyParamsToEngine():
// Condition lane: expand-write-shrink pattern (same as other lanes)
{
    const auto condLen = arpParams_.conditionLaneLength.load(relaxed);
    arpCore_.conditionLane().setLength(32);  // Expand first
    for (int i = 0; i < 32; ++i) {
        int val = std::clamp(arpParams_.conditionLaneSteps[i].load(relaxed), 0, 17);
        arpCore_.conditionLane().setStep(static_cast<size_t>(i), static_cast<uint8_t>(val));
    }
    arpCore_.conditionLane().setLength(static_cast<size_t>(condLen));  // Shrink to actual
}
arpCore_.setFillActive(arpParams_.fillToggle.load(relaxed));
```

---

## Arpeggiator Spice/Dice/Humanize Parameters (Spec 077)

**File**: `plugins/ruinae/src/parameters/arpeggiator_params.h`
**IDs**: 3290-3292 (3 parameters: Spice, Dice trigger, Humanize)

### Purpose

Controlled randomization (Spice/Dice) and timing humanization for the arpeggiator. Spice blends between original lane values and a random variation overlay. The Dice trigger generates new random overlay values. Humanize adds per-step random offsets to timing, velocity, and gate for organic feel. All three parameters are automatable.

### Spice/Dice/Humanize Parameter ID Allocation

| ID | Name | Type | Range | Default (Norm) | Flags |
|----|------|------|-------|----------------|-------|
| 3290 | Arp Spice | Continuous (float) | 0.0-1.0 | 0.0 (0%) | `kCanAutomate` |
| 3291 | Arp Dice | Discrete (int) | 0-1 | 0 (idle) | `kCanAutomate` |
| 3292 | Arp Humanize | Continuous (float) | 0.0-1.0 | 0.0 (0%) | `kCanAutomate` |

All 3 parameters have `kCanAutomate` and none have `kIsHidden` -- all are user-facing controls. The UI for Spice knob, Dice button, and Humanize knob is deferred to Phase 11 (Arpeggiator UI). IDs 3293-3299 are reserved for future phases.

### Denormalization

| Parameter | Formula | Step Count |
|-----------|---------|------------|
| Spice | Identity: `norm` (0-1 maps directly to 0-100%) | 0 (continuous) |
| Dice Trigger | `norm >= 0.5` triggers action | 1 (discrete 2-step) |
| Humanize | Identity: `norm` (0-1 maps directly to 0-100%) | 0 (continuous) |

### ArpeggiatorParams Struct Extension

```cpp
struct ArpeggiatorParams {
    // ... existing base + lane + modifier + ratchet + euclidean + condition fields ...

    // Spice/Dice & Humanize (Spec 077)
    std::atomic<float> spice{0.0f};           // [0.0, 1.0] blend amount
    std::atomic<bool>  diceTrigger{false};    // Rising-edge trigger (momentary)
    std::atomic<float> humanize{0.0f};        // [0.0, 1.0] humanize amount
};
```

**Note:** The Dice trigger uses `std::atomic<bool>` (not `std::atomic<int>`) because it is a momentary edge-detected action, not a stored parameter value. The `compare_exchange_strong` pattern in `applyParamsToEngine()` guarantees exactly-once consumption per rising edge.

### Format Strings

| Parameter | Examples |
|-----------|----------|
| Spice | "0%", "50%", "100%" |
| Dice Trigger | "--" (idle), "Roll" (triggered) |
| Humanize | "0%", "50%", "100%" |

### handleArpParamChange() Dispatch

```cpp
case kArpSpiceId:
    params.spice.store(std::clamp(static_cast<float>(value), 0.0f, 1.0f), relaxed);
    break;
case kArpDiceTriggerId:
    if (value >= 0.5) params.diceTrigger.store(true, relaxed);  // Rising edge only
    break;
case kArpHumanizeId:
    params.humanize.store(std::clamp(static_cast<float>(value), 0.0f, 1.0f), relaxed);
    break;
```

### Engine Forwarding

```cpp
// In applyParamsToEngine():
arpCore_.setSpice(arpParams_.spice.load(relaxed));

// Dice trigger: consume rising edge via compare_exchange_strong
{
    bool expected = true;
    if (arpParams_.diceTrigger.compare_exchange_strong(expected, false, relaxed)) {
        arpCore_.triggerDice();
    }
}

arpCore_.setHumanize(arpParams_.humanize.load(relaxed));
```

The Dice trigger uses `compare_exchange_strong` (not plain load/store) to guarantee exactly-once consumption per rising edge and eliminate check-then-act races.

### State Persistence

Spice and Humanize are serialized as 2 floats (8 bytes) appended after the Phase 8 `fillToggle` field:
```
[float: spice]       // 4 bytes (0.0-1.0)
[float: humanize]    // 4 bytes (0.0-1.0)
```

The Dice trigger (`diceTrigger`) is NOT serialized -- it is a momentary action, not stored state. The random overlay arrays are NOT serialized -- they are ephemeral and revert to identity defaults on load.

### Backward Compatibility

| Preset Source | Spice/Humanize Data Present? | Behavior |
|---------------|------------------------------|----------|
| Phase 8 (Spec 076, no Spice/Humanize) | No | EOF at first Spice read = return true. Spice=0%, Humanize=0%. Arp output identical to Phase 8. |
| Phase 9 (Spec 077, with Spice/Humanize) | Yes | Both values fully restored. |
| Corrupt (Spice present, Humanize missing) | Partial | Spice read succeeds, Humanize EOF = return false (corrupt stream). |

---

## Denormalization Mappings Reference

| Mapping | Parameters | Formula |
|---------|------------|---------|
| Identity | Macro values, Depth, Filter, Spread, EF Sensitivity, PF Confidence, Trn Sensitivity, Rnd Smoothness | `normalized` (1:1) |
| Linear (x2) | Width (0-200%) | `normalized * 2.0` |
| Linear (scaled) | Gain (0-2), Levels, Mix, S&H Slew (0-500ms) | `normalized * range` |
| Linear (offset+scale) | Tuning Reference (400-480Hz), PF Speed (10-300ms), Trn Attack (0.5-10ms), Trn Decay (20-200ms) | `offset + normalized * range` |
| Exponential | Filter Cutoff (20-20kHz) | `20 * pow(1000, normalized)` |
| Exponential | LFO Rate (0.01-50Hz), S&H Rate, Rnd Rate | `0.01 * pow(5000, normalized)` |
| Logarithmic | Rungler Freq (0.1-100Hz) | `0.1 * pow(1000, normalized)` |
| Logarithmic | EF Attack (0.1-500ms) | `0.1 * pow(5000, normalized)` |
| Logarithmic | EF Release (1-5000ms) | `1.0 * pow(5000, normalized)` |
| Logarithmic | PF Min Hz (20-500Hz) | `20 * pow(25, normalized)` |
| Logarithmic | PF Max Hz (200-5000Hz) | `200 * pow(25, normalized)` |
| Cubic | Envelope Times (0-10000ms) | `normalized^3 * 10000` |
| Cubic | Portamento Time (0-5000ms) | `normalized^3 * 5000` |
| Bipolar | Tune (-24/+24), Mod Amount (-1/+1) | `normalized * range - offset` |
| Discrete (stepped) | Pitch Bend Range (0-24st) | `round(normalized * 24)` |
| Discrete | Rungler Bits (4-16) | `4 + round(normalized * 12)` |
| Linear (offset+scale) | Arp Free Rate (0.5-50Hz), Arp Gate Length (1-200%), Arp Swing (0-75%) | `offset + normalized * range` |
| Discrete (offset+scale) | Arp Euclidean Hits (0-32), Ratchet Steps (1-4) | `round(normalized * range)` or `offset + round(normalized * range)` |
| Discrete (offset+scale) | Arp Euclidean Steps (2-32) | `2 + round(normalized * 30)` |
| Discrete (scaled) | Arp Euclidean Rotation (0-31), Arp Condition Steps (0-17) | `round(normalized * 31)` or `round(normalized * 17)` |
| Identity (continuous) | Arp Spice (0-100%), Arp Humanize (0-100%) | `normalized` (1:1) |
| Boolean | Loop Mode, Enabled flags, Gain Comp, S&H Sync, Rnd Sync, Arp Enabled, Arp Tempo Sync, Arp Euclidean Enabled, Arp Fill Toggle | `normalized >= 0.5` |
| Boolean (edge-detected) | Arp Dice Trigger | `normalized >= 0.5` sets `diceTrigger = true` (rising edge only) |

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
<control-tag name="EnvFollowerSensitivity" tag="2300"/>
<control-tag name="SampleHoldRate" tag="2400"/>
<control-tag name="RandomRate" tag="2500"/>
<control-tag name="PitchFollowerMinHz" tag="2600"/>
<control-tag name="TransientSensitivity" tag="2700"/>
```

### Mod Source Dropdown Integration

The `ModSourceViewMode` StringListParameter drives a UIViewSwitchContainer. Selecting a source from the dropdown displays the corresponding template:

| Dropdown Entry | Template | Controls |
|----------------|----------|----------|
| Macros | `ModSource_Macros` | 4 ArcKnobs (M1-M4) + 4 CTextLabels |
| Rungler | `ModSource_Rungler` | 4 ArcKnobs (Osc1, Osc2, Depth, Filter) + 1 ArcKnob (Bits) + 1 ToggleButton (Loop) + 6 CTextLabels |
| Env Follower | `ModSource_EnvFollower` | 3 ArcKnobs (Sens, Atk, Rel) + 3 CTextLabels |
| S&H | `ModSource_SampleHold` | Rate/NoteValue switching groups + 1 ArcKnob (Slew) + 1 ToggleButton (Sync) + CTextLabels |
| Random | `ModSource_Random` | Rate/NoteValue switching groups + 1 ArcKnob (Smooth) + 1 ToggleButton (Sync) + CTextLabels |
| Pitch Follower | `ModSource_PitchFollower` | 4 ArcKnobs (Min, Max, Conf, Speed) + 4 CTextLabels |
| Transient | `ModSource_Transient` | 3 ArcKnobs (Sens, Atk, Decay) + 3 CTextLabels |

All ArcKnobs use `arc-color="modulation"` and `guide-color="knob-guide"` for visual consistency with the modulation section theme.

S&H and Random templates use the sync visibility switching pattern (see [Plugin UI Patterns](plugin-ui-patterns.md)) to toggle between Rate knob and NoteValue dropdown based on the Sync toggle state.
