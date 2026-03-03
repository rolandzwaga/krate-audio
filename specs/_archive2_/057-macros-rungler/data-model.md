# Data Model: Macros & Rungler UI Exposure

**Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md) | **Date**: 2026-02-15

## Entities

### MacroParams

**Purpose**: Stores the 4 macro knob values as atomic fields for thread-safe access between the audio and UI threads.

**Location**: `plugins/ruinae/src/parameters/macro_params.h` (NEW)

| Field | Type | Default | Range | Description |
|-------|------|---------|-------|-------------|
| `values[0]` | `std::atomic<float>` | 0.0f | [0.0, 1.0] | Macro 1 knob position |
| `values[1]` | `std::atomic<float>` | 0.0f | [0.0, 1.0] | Macro 2 knob position |
| `values[2]` | `std::atomic<float>` | 0.0f | [0.0, 1.0] | Macro 3 knob position |
| `values[3]` | `std::atomic<float>` | 0.0f | [0.0, 1.0] | Macro 4 knob position |

**Validation**: Values clamped to [0.0, 1.0] via `std::clamp` in `handleMacroParamChange()`.

**State transitions**: None. Macro values are stateless -- they are set by the host/UI and forwarded to the engine immediately.

**Persistence**: Saved as 4 floats in order (values[0] through values[3]). EOF-safe loading with defaults when loading pre-v13 presets.

---

### RunglerParams

**Purpose**: Stores the 6 Rungler configuration parameters as atomic fields for thread-safe access.

**Location**: `plugins/ruinae/src/parameters/rungler_params.h` (NEW)

| Field | Type | Default | Range | Unit | Description |
|-------|------|---------|-------|------|-------------|
| `osc1FreqHz` | `std::atomic<float>` | 2.0f | [0.1, 100.0] Hz | Hz | Oscillator 1 frequency |
| `osc2FreqHz` | `std::atomic<float>` | 3.0f | [0.1, 100.0] Hz | Hz | Oscillator 2 frequency |
| `depth` | `std::atomic<float>` | 0.0f | [0.0, 1.0] | % | Cross-modulation depth |
| `filter` | `std::atomic<float>` | 0.0f | [0.0, 1.0] | % | CV smoothing filter amount |
| `bits` | `std::atomic<int>` | 8 | [4, 16] | -- | Shift register bit count |
| `loopMode` | `std::atomic<bool>` | false | {false, true} | -- | false=chaos, true=loop |

**Validation**:
- Frequency: Logarithmic denormalization from [0,1] to [0.1, 100] Hz via `hz = 0.1 * pow(1000.0, normalized)`
- Depth/Filter: Linear [0, 1], clamped via `std::clamp`
- Bits: Discrete, `stepCount=12` (13 values mapping to 4-16), formula: `bits = 4 + round(normalized * 12)`
- Loop Mode: Boolean, `value >= 0.5` = true

**State transitions**: None. Parameters are set by host/UI and forwarded to the engine.

**Persistence**: Saved as: osc1FreqHz (float), osc2FreqHz (float), depth (float), filter (float), bits (int32), loopMode (int32). EOF-safe loading with defaults for pre-v13 presets.

---

### ModSource Enum (Modified)

**Purpose**: Identifies each modulation source in the mod matrix routing system.

**Location**: `dsp/include/krate/dsp/core/modulation_types.h` (MODIFIED)

| Value | Name | Change |
|-------|------|--------|
| 0 | None | Unchanged |
| 1 | LFO1 | Unchanged |
| 2 | LFO2 | Unchanged |
| 3 | EnvFollower | Unchanged |
| 4 | Random | Unchanged |
| 5 | Macro1 | Unchanged |
| 6 | Macro2 | Unchanged |
| 7 | Macro3 | Unchanged |
| 8 | Macro4 | Unchanged |
| 9 | Chaos | Unchanged |
| **10** | **Rungler** | **NEW** |
| 11 | SampleHold | Was 10 |
| 12 | PitchFollower | Was 11 |
| 13 | Transient | Was 12 |

**Related constants**:
- `kModSourceCount`: 13 -> 14 (in `modulation_types.h`)
- `kNumGlobalSources`: 12 -> 13 (in `mod_matrix_types.h`)

---

## Relationships

```
MacroParams (plugin layer)
    |
    v [processParameterChanges -> applyParamsToEngine]
RuinaeEngine::setMacroValue(index, value)
    |
    v [forwards to globalModEngine_]
ModulationEngine::setMacroValue(index, value)
    |
    v [stores to macros_[index].value]
MacroConfig.value  -->  getMacroOutput()  -->  getRawSourceValue(ModSource::Macro1..4)
    |
    v [mod matrix routing]
Destination parameter offset


RunglerParams (plugin layer)
    |
    v [processParameterChanges -> applyParamsToEngine]
RuinaeEngine::setRunglerXxx(value)
    |
    v [forwards to globalModEngine_]
ModulationEngine::setRunglerXxx(value)
    |
    v [forwards to rungler_ field]
Rungler::setOsc1Frequency / setOsc2Frequency / setRunglerDepth / setFilterAmount / setRunglerBits / setLoopMode
    |
    v [processBlock when sourceActive_]
Rungler::getCurrentValue()  -->  getRawSourceValue(ModSource::Rungler)
    |
    v [mod matrix routing]
Destination parameter offset
```

## Parameter ID Mapping

| Parameter Name | VST Param ID | Normalized Range | Denormalized Range | Display Format |
|----------------|-------------|------------------|-------------------|----------------|
| Macro 1 Value | 2000 | [0, 1] | [0, 1] (identity) | "XX%" |
| Macro 2 Value | 2001 | [0, 1] | [0, 1] (identity) | "XX%" |
| Macro 3 Value | 2002 | [0, 1] | [0, 1] (identity) | "XX%" |
| Macro 4 Value | 2003 | [0, 1] | [0, 1] (identity) | "XX%" |
| Rungler Osc1 Freq | 2100 | [0, 1] | [0.1, 100] Hz (log) | "X.XX Hz" |
| Rungler Osc2 Freq | 2101 | [0, 1] | [0.1, 100] Hz (log) | "X.XX Hz" |
| Rungler Depth | 2102 | [0, 1] | [0, 1] (identity) | "XX%" |
| Rungler Filter | 2103 | [0, 1] | [0, 1] (identity) | "XX%" |
| Rungler Bits | 2104 | [0, 1] step=12 | [4, 16] integer | "X" |
| Rungler Loop Mode | 2105 | [0, 1] step=1 | {off, on} | toggle |

## State Stream Format (v13)

Appended after v12 extended LFO params:

```
// v13 block:
float   macroValues[0]      // Macro 1 value
float   macroValues[1]      // Macro 2 value
float   macroValues[2]      // Macro 3 value
float   macroValues[3]      // Macro 4 value
float   runglerOsc1FreqHz   // Osc1 frequency in Hz
float   runglerOsc2FreqHz   // Osc2 frequency in Hz
float   runglerDepth        // Cross-mod depth [0,1]
float   runglerFilter       // CV filter amount [0,1]
int32   runglerBits         // Shift register bits [4,16]
int32   runglerLoopMode     // 0=chaos, 1=loop
```

Total: 6 floats + 2 int32s = 32 bytes added per preset.

## Preset Migration (v < 13)

When loading presets with state version < 13:

1. **New params default**: MacroParams values all default to 0.0f. RunglerParams default to struct defaults (osc1=2.0, osc2=3.0, depth=0, filter=0, bits=8, loop=false).

2. **ModSource enum migration**: For each mod matrix slot source value and voice route source value:
   - If `source >= 10`: `source += 1` (shifts SampleHold 10->11, PitchFollower 11->12, Transient 12->13)
   - If `source < 10`: unchanged
