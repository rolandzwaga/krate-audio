# Parameter ID Contract: Macros & Rungler

**Spec**: [../spec.md](../spec.md) | **Plan**: [../plan.md](../plan.md) | **Date**: 2026-02-15

## Parameter ID Allocation

### Macro Parameters (Range 2000-2099)

```cpp
// In plugins/ruinae/src/plugin_ids.h

// ==========================================================================
// Macro Parameters (2000-2099)
// ==========================================================================
kMacroBaseId = 2000,
kMacro1ValueId = 2000,     // Macro 1 knob value [0, 1] (default 0)
kMacro2ValueId = 2001,     // Macro 2 knob value [0, 1] (default 0)
kMacro3ValueId = 2002,     // Macro 3 knob value [0, 1] (default 0)
kMacro4ValueId = 2003,     // Macro 4 knob value [0, 1] (default 0)
kMacroEndId = 2099,
```

### Rungler Parameters (Range 2100-2199)

```cpp
// In plugins/ruinae/src/plugin_ids.h

// ==========================================================================
// Rungler Parameters (2100-2199)
// ==========================================================================
kRunglerBaseId = 2100,
kRunglerOsc1FreqId = 2100, // Osc1 frequency [0.1, 100] Hz (default 2.0 Hz)
kRunglerOsc2FreqId = 2101, // Osc2 frequency [0.1, 100] Hz (default 3.0 Hz)
kRunglerDepthId = 2102,    // Cross-modulation depth [0, 1] (default 0)
kRunglerFilterId = 2103,   // CV filter amount [0, 1] (default 0)
kRunglerBitsId = 2104,     // Shift register bits [4, 16] (default 8)
kRunglerLoopModeId = 2105, // Loop mode on/off (default off = chaos)
kRunglerEndId = 2199,
```

### Sentinel

```cpp
kNumParameters = 2200,     // Updated from 2000
```

## VST Parameter Registration

### Macro Parameters

| ID | Title | Unit | stepCount | Default (norm) | Flags |
|----|-------|------|-----------|-----------------|-------|
| 2000 | "Macro 1" | "%" | 0 (continuous) | 0.0 | kCanAutomate |
| 2001 | "Macro 2" | "%" | 0 (continuous) | 0.0 | kCanAutomate |
| 2002 | "Macro 3" | "%" | 0 (continuous) | 0.0 | kCanAutomate |
| 2003 | "Macro 4" | "%" | 0 (continuous) | 0.0 | kCanAutomate |

### Rungler Parameters

**Note**: Titles use "Rng" abbreviation for UI space efficiency (mod source view limited to 158px width).

| ID | Title | Unit | stepCount | Default (norm) | Flags | Mapping |
|----|-------|------|-----------|-----------------|-------|---------|
| 2100 | "Rng Osc1 Freq" | "Hz" | 0 (continuous) | 0.4337 | kCanAutomate | Log: hz = 0.1 * pow(1000, norm) |
| 2101 | "Rng Osc2 Freq" | "Hz" | 0 (continuous) | 0.4924 | kCanAutomate | Log: hz = 0.1 * pow(1000, norm) |
| 2102 | "Rng Depth" | "%" | 0 (continuous) | 0.0 | kCanAutomate | Linear: depth = norm |
| 2103 | "Rng Filter" | "%" | 0 (continuous) | 0.0 | kCanAutomate | Linear: filter = norm |
| 2104 | "Rng Bits" | "" | 12 | 0.3333 | kCanAutomate | Step: bits = 4 + round(norm * 12) |
| 2105 | "Rng Loop Mode" | "" | 1 | 0.0 | kCanAutomate | Bool: loop = norm >= 0.5 |

## Normalization/Denormalization Functions

### Rungler Frequency (Logarithmic)

```cpp
// Normalized [0, 1] -> Hz [0.1, 100]
inline float runglerFreqFromNormalized(double normalized) {
    return static_cast<float>(0.1 * std::pow(1000.0, normalized));
}

// Hz [0.1, 100] -> Normalized [0, 1]
inline double runglerFreqToNormalized(float hz) {
    return std::log(static_cast<double>(hz) / 0.1) / std::log(1000.0);
}
```

### Rungler Bits (Discrete)

```cpp
// Normalized [0, 1] -> Bits [4, 16] (stepCount = 12)
inline int runglerBitsFromNormalized(double normalized) {
    return 4 + std::clamp(static_cast<int>(normalized * 12 + 0.5), 0, 12);
}

// Bits [4, 16] -> Normalized [0, 1]
inline double runglerBitsToNormalized(int bits) {
    return static_cast<double>(std::clamp(bits, 4, 16) - 4) / 12.0;
}
```

## Display Formatting

| ID Range | Format | Example |
|----------|--------|---------|
| 2000-2003 | `"%.0f%%", value * 100.0` | "75%" |
| 2100-2101 | `"%.2f Hz", runglerFreqFromNormalized(value)` | "2.00 Hz" |
| 2102-2103 | `"%.0f%%", value * 100.0` | "50%" |
| 2104 | `"%d", runglerBitsFromNormalized(value)` | "8" |
| 2105 | (framework handles on/off) | "On" / "Off" |

## Control-Tags (UIDESC)

```xml
<control-tag name="Macro1Value" tag="2000"/>
<control-tag name="Macro2Value" tag="2001"/>
<control-tag name="Macro3Value" tag="2002"/>
<control-tag name="Macro4Value" tag="2003"/>
<control-tag name="RunglerOsc1Freq" tag="2100"/>
<control-tag name="RunglerOsc2Freq" tag="2101"/>
<control-tag name="RunglerDepth" tag="2102"/>
<control-tag name="RunglerFilter" tag="2103"/>
<control-tag name="RunglerBits" tag="2104"/>
<control-tag name="RunglerLoopMode" tag="2105"/>
```

## ModSource Enum Contract

### Before (v12)

```cpp
enum class ModSource : uint8_t {
    None = 0, LFO1 = 1, LFO2 = 2, EnvFollower = 3, Random = 4,
    Macro1 = 5, Macro2 = 6, Macro3 = 7, Macro4 = 8,
    Chaos = 9, SampleHold = 10, PitchFollower = 11, Transient = 12
};
inline constexpr uint8_t kModSourceCount = 13;
```

### After (v13)

```cpp
enum class ModSource : uint8_t {
    None = 0, LFO1 = 1, LFO2 = 2, EnvFollower = 3, Random = 4,
    Macro1 = 5, Macro2 = 6, Macro3 = 7, Macro4 = 8,
    Chaos = 9, Rungler = 10,
    SampleHold = 11, PitchFollower = 12, Transient = 13
};
inline constexpr uint8_t kModSourceCount = 14;
```

### Migration Rule

For presets with `stateVersion < 13`:
- ModSource values `0-9`: unchanged
- ModSource values `10+`: increment by 1
- Applied to: mod matrix slot sources, voice route sources
