# Parameter ID Contract: Band Management

**Component**: Per-band VST3 Parameters
**Location**: `plugins/disrumpo/src/plugin_ids.h`

## Encoding Scheme

Per `specs/Disrumpo/dsp-details.md`:

```
Bit Layout (16-bit ParamID):
+--------+--------+--------+
| 15..12 | 11..8  |  7..0  |
|  node  |  band  | param  |
+--------+--------+--------+

For band-level parameters: node = 0xF
For band 0-7, param type in lower 8 bits
```

## Parameter IDs

### Helper Function

```cpp
// Add to plugin_ids.h
enum BandParamType : uint8_t {
    kBandGain   = 0x00,
    kBandPan    = 0x01,
    kBandSolo   = 0x02,
    kBandBypass = 0x03,
    kBandMute   = 0x04,
};

constexpr Steinberg::Vst::ParamID makeBandParamId(uint8_t band, BandParamType param) {
    return static_cast<Steinberg::Vst::ParamID>((0xF << 12) | (band << 8) | param);
}
```

### Generated IDs

| Band | Gain | Pan | Solo | Bypass | Mute |
|------|------|-----|------|--------|------|
| 0 | 0xF000 | 0xF001 | 0xF002 | 0xF003 | 0xF004 |
| 1 | 0xF100 | 0xF101 | 0xF102 | 0xF103 | 0xF104 |
| 2 | 0xF200 | 0xF201 | 0xF202 | 0xF203 | 0xF204 |
| 3 | 0xF300 | 0xF301 | 0xF302 | 0xF303 | 0xF304 |
| 4 | 0xF400 | 0xF401 | 0xF402 | 0xF403 | 0xF404 |
| 5 | 0xF500 | 0xF501 | 0xF502 | 0xF503 | 0xF504 |
| 6 | 0xF600 | 0xF601 | 0xF602 | 0xF603 | 0xF604 |
| 7 | 0xF700 | 0xF701 | 0xF702 | 0xF703 | 0xF704 |

### Crossover Frequency IDs

For N-1 crossover frequencies (N bands):

```cpp
enum CrossoverParamType : uint8_t {
    kCrossoverFreq = 0x10,  // Base for crossover frequencies
};

// Crossover frequencies use band slot for index
// 0xF010 = Crossover 0 (between band 0 and 1)
// 0xF110 = Crossover 1 (between band 1 and 2)
// etc.

constexpr Steinberg::Vst::ParamID makeCrossoverParamId(uint8_t index) {
    return static_cast<Steinberg::Vst::ParamID>((0xF << 12) | (index << 8) | kCrossoverFreq);
}
```

| Crossover | ID |
|-----------|-----|
| 0 | 0xF010 |
| 1 | 0xF110 |
| 2 | 0xF210 |
| 3 | 0xF310 |
| 4 | 0xF410 |
| 5 | 0xF510 |
| 6 | 0xF610 |

### Global Parameters (Existing)

| Parameter | ID | Notes |
|-----------|-----|-------|
| Input Gain | 0x0F00 | Existing |
| Output Gain | 0x0F01 | Existing |
| Global Mix | 0x0F02 | Existing |
| Band Count | 0x0F03 | New |

## Normalization

All VST3 parameters are normalized 0.0 to 1.0 at the boundary.

| Parameter | Physical Range | Normalization |
|-----------|---------------|---------------|
| Band Gain | -24 to +24 dB | Linear: `norm * 48 - 24` |
| Band Pan | -1.0 to +1.0 | Linear: `norm * 2 - 1` |
| Band Solo | false/true | `norm >= 0.5` |
| Band Bypass | false/true | `norm >= 0.5` |
| Band Mute | false/true | `norm >= 0.5` |
| Crossover Freq | 20 to 20000 Hz | Logarithmic (see below) |
| Band Count | 1 to 8 | `round(norm * 7 + 1)` |

### Logarithmic Crossover Normalization

```cpp
// Normalize Hz to 0-1
float freqToNorm(float hz) {
    float logHz = std::log10(hz);
    float logMin = std::log10(20.0f);
    float logMax = std::log10(20000.0f);
    return (logHz - logMin) / (logMax - logMin);
}

// Denormalize 0-1 to Hz
float normToFreq(float norm) {
    float logMin = std::log10(20.0f);
    float logMax = std::log10(20000.0f);
    float logHz = norm * (logMax - logMin) + logMin;
    return std::pow(10.0f, logHz);
}
```

## Controller Registration

Per FR-034:

```cpp
// In Controller::initialize()
for (int band = 0; band < 8; ++band) {
    // Gain: RangeParameter
    parameters->addParameter(new RangeParameter(
        USTRING("Band Gain"),
        makeBandParamId(band, kBandGain),
        USTRING("dB"),
        -24.0, 24.0, 0.0  // min, max, default
    ));

    // Pan: RangeParameter
    parameters->addParameter(new RangeParameter(
        USTRING("Band Pan"),
        makeBandParamId(band, kBandPan),
        USTRING(""),
        -1.0, 1.0, 0.0
    ));

    // Solo/Bypass/Mute: Parameter (boolean)
    parameters->addParameter(new Parameter(
        USTRING("Band Solo"),
        makeBandParamId(band, kBandSolo)
    ));
    // ... similar for bypass, mute
}

// Band count: RangeParameter
parameters->addParameter(new RangeParameter(
    USTRING("Band Count"),
    kBandCountId,
    USTRING(""),
    1.0, 8.0, 4.0  // default 4 bands
));
```
