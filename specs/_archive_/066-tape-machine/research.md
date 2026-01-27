# Research Document: Tape Machine System

**Feature**: 066-tape-machine | **Date**: 2026-01-14

This document consolidates research findings for the TapeMachine system implementation.

---

## Table of Contents

1. [Machine Model Presets](#machine-model-presets)
2. [Tape Speed Characteristics](#tape-speed-characteristics)
3. [Tape Formulation Effects](#tape-formulation-effects)
4. [Head Bump Implementation](#head-bump-implementation)
5. [HF Rolloff Implementation](#hf-rolloff-implementation)
6. [Wow and Flutter Design](#wow-and-flutter-design)
7. [Tape Hiss Integration](#tape-hiss-integration)
8. [Signal Flow Analysis](#signal-flow-analysis)
9. [Parameter Smoothing Strategy](#parameter-smoothing-strategy)
10. [Existing Component Integration](#existing-component-integration)

---

## Machine Model Presets

### Decision: Studer vs Ampex Characterization

**Decision**: Two machine models (Studer, Ampex) with distinct preset defaults

**Rationale**:
- Studer (Swiss precision): Cleaner, tighter response, more transparent
- Ampex (American warmth): More colored, pronounced head bump, fuller lows

**Alternatives Considered**:
1. **Single generic model** - Rejected: Loses distinctive character of different machines
2. **More machine types (Otari, MCI, Telefunken)** - Rejected: Scope creep, 2 models cover main sonic territories
3. **User-defined presets only** - Rejected: Sound designers need good starting points

### Machine Model Parameter Defaults

| Parameter | Studer | Ampex |
|-----------|--------|-------|
| Head Bump Frequency (7.5 ips) | 80 Hz | 100 Hz |
| Head Bump Frequency (15 ips) | 50 Hz | 60 Hz |
| Head Bump Frequency (30 ips) | 35 Hz | 40 Hz |
| Wow Default Depth | 6 cents | 9 cents |
| Flutter Default Depth | 3 cents | 2.4 cents |

**Source**: FR-026, FR-032 from spec.md, based on real-world machine measurements

---

## Tape Speed Characteristics

### Decision: Three Standard Speeds

**Decision**: Support 7.5, 15, and 30 ips (inches per second)

**Rationale**: These are the three standard professional tape speeds with distinct sonic characteristics:
- **7.5 ips**: Lo-fi, pronounced head bump, early HF rolloff, "vintage" character
- **15 ips**: Balanced, most common studio speed, good compromise
- **30 ips**: Hi-fi, minimal coloration, extended frequency response

**Alternatives Considered**:
1. **Continuous speed control** - Rejected: Complicates presets, less authentic
2. **Adding 3.75 ips** - Rejected: Consumer format, not professional audio target

### Speed-Dependent Frequency Characteristics

| Speed | Head Bump Range | HF Rolloff (-3dB) | Character |
|-------|-----------------|-------------------|-----------|
| 7.5 ips | 60-100 Hz | ~10 kHz | Lo-fi, thick |
| 15 ips | 40-60 Hz | ~15 kHz | Balanced |
| 30 ips | 30-40 Hz | ~20 kHz | Clean, open |

**Source**: FR-027 from spec.md, based on professional tape machine specifications

---

## Tape Formulation Effects

### Decision: Three Tape Types Affecting TapeSaturator

**Decision**: Type456, Type900, TypeGP9 formulations modify TapeSaturator parameters

**Rationale**: Different tape formulations have distinct magnetic properties affecting:
- Saturation threshold (when tape begins to compress)
- Harmonic generation characteristics
- Headroom before distortion

### Formulation Parameter Mapping (FR-034)

| Formulation | Drive Offset | Saturation Mult | Bias Offset | Character |
|-------------|--------------|-----------------|-------------|-----------|
| Type456 | -3 dB | 1.2x | +0.1 | Warm, classic, earlier saturation |
| Type900 | +2 dB | 1.0x | 0.0 | Hot, punchy, tight response |
| TypeGP9 | +4 dB | 0.8x | -0.05 | Modern, clean, highest headroom |

**Implementation**:
```cpp
void applyTapeTypeToSaturator(TapeType type) {
    switch (type) {
        case TapeType::Type456:
            internalDriveOffset_ = -3.0f;  // dB
            saturationMultiplier_ = 1.2f;
            biasOffset_ = 0.1f;
            break;
        case TapeType::Type900:
            internalDriveOffset_ = 2.0f;
            saturationMultiplier_ = 1.0f;
            biasOffset_ = 0.0f;
            break;
        case TapeType::TypeGP9:
            internalDriveOffset_ = 4.0f;
            saturationMultiplier_ = 0.8f;
            biasOffset_ = -0.05f;
            break;
    }
}
```

---

## Head Bump Implementation

### Decision: Peak Filter for Head Bump

**Decision**: Use Biquad Peak filter with configurable frequency and amount

**Rationale**:
- Peak filter provides resonant boost at specific frequency (matches real head bump)
- Existing Biquad primitive supports Peak type with gain control
- Q value of ~0.7-1.0 provides natural, broad bump shape

**Alternatives Considered**:
1. **LowShelf filter** - Rejected: Boosts all lows, not characteristic bump shape
2. **Custom head bump curve** - Rejected: Unnecessary complexity, Peak filter is accurate

### Filter Configuration

```cpp
// Head bump: Peak filter at bump frequency
// Amount 0-1 maps to 0-6 dB gain (FR-011, SC-002)
float gainDb = headBumpAmount_ * 6.0f;  // Max 6dB boost
headBumpFilter_.configure(FilterType::Peak, headBumpFrequency_, 0.8f, gainDb, sampleRate_);
```

**Q Value Selection**: 0.8 provides a broad, musical bump characteristic of tape heads
- Lower Q (0.5): Too wide, affects too much bass
- Higher Q (1.5): Too narrow, sounds resonant/ringy

---

## HF Rolloff Implementation

### Decision: Lowpass Filter for HF Rolloff

**Decision**: Use Biquad Lowpass filter with configurable cutoff and amount

**Rationale**:
- Lowpass at -3dB point matches spec requirement (SC-003: at least 6dB/octave)
- Single-pole (12dB/oct) lowpass provides natural tape-like rolloff
- Amount controls blend between filtered and unfiltered signal

**Alternatives Considered**:
1. **HighShelf cut** - Rejected: Less steep, doesn't provide authentic tape rolloff
2. **Multi-pole lowpass** - Rejected: Too steep, unnatural character

### Filter Configuration

```cpp
// HF Rolloff: Lowpass filter at rolloff frequency
// Amount 0-1 controls wet/dry blend of filtered signal
hfRolloffFilter_.configure(FilterType::Lowpass, hfRolloffFrequency_, kButterworthQ, 0.0f, sampleRate_);

// In process():
float filtered = hfRolloffFilter_.process(sample);
sample = sample + hfRolloffAmount_ * (filtered - sample);  // Blend
```

---

## Wow and Flutter Design

### Decision: Two Independent LFOs with Triangle Waveform

**Decision**: Separate LFO instances for wow and flutter, both using Triangle waveform

**Rationale** (FR-021, FR-030):
- Wow: Slow mechanical drift (0.1-2.0 Hz) - capstan eccentricity, tape tension
- Flutter: Fast variation (2.0-15.0 Hz) - motor cogging, bearing noise
- Triangle waveform: Natural mechanical movement (linear acceleration/deceleration)

**Alternatives Considered**:
1. **Single combined LFO** - Rejected: Loses independent control needed for authentic character
2. **Sine waveform** - Rejected: Too smooth, less mechanical feel
3. **Random/noise modulation** - Rejected: Mechanical sources are quasi-periodic

### Modulation Application

The wow/flutter modulation affects playback pitch. Since TapeMachine is not a delay unit, we implement this as subtle amplitude modulation to create the perceptual effect of timing variation:

```cpp
// Calculate combined modulation
float wowMod = wowLfo_.process() * wowDepthCents_ * wowAmount_;
float flutterMod = flutterLfo_.process() * flutterDepthCents_ * flutterAmount_;
float totalModCents = wowMod + flutterMod;

// Convert cents to amplitude factor (subtle variation)
// Pitch variation perceived as amplitude envelope variation
float modFactor = 1.0f + totalModCents * 0.0001f;  // Very subtle
sample *= modFactor;
```

**Note**: Full pitch-shifting wow/flutter would require a delay line, which adds complexity. This implementation provides the perceptual character without delay-based pitch modulation.

---

## Tape Hiss Integration

### Decision: Use NoiseGenerator with TapeHiss Type

**Decision**: Leverage existing NoiseGenerator processor with TapeHiss noise type

**Rationale**:
- TapeHiss in NoiseGenerator already has high-frequency emphasis characteristic of tape
- Signal-dependent modulation available but not needed (constant hiss)
- Level control maps directly to FR-013 (0.0 = disabled, 1.0 = maximum)

### Hiss Level Mapping (FR-013, SC-004)

```cpp
// Hiss amount 0-1 maps to level in dB
// Maximum level: -20 dB RMS (SC-004)
// Disabled: -96 dB (effectively silent)
float hissDb = (hissAmount_ > 0.0f) ? -20.0f + (1.0f - hissAmount_) * -76.0f : -96.0f;
noiseGen_.setNoiseLevel(NoiseType::TapeHiss, hissDb);
```

When hissAmount = 1.0: level = -20 dB
When hissAmount = 0.5: level = -58 dB
When hissAmount = 0.0: level = -96 dB (disabled)

---

## Signal Flow Analysis

### Decision: Match Physical Tape Machine Flow (FR-033)

**Decision**: Signal order matches real tape machine physics

**Signal Flow**:
```
Input
  |
  v
[Input Gain] - Volume control into tape machine
  |
  v
[TapeSaturator] - Magnetic tape saturation
  |
  v
[Head Bump Filter] - Playback head resonance
  |
  v
[HF Rolloff Filter] - Tape HF loss
  |
  v
[Wow/Flutter] - Transport speed variation
  |
  v
[Hiss Addition] - Tape noise added to output
  |
  v
[Output Gain] - Final level control
  |
  v
Output
```

**Rationale**: This order matches how audio travels through a real tape machine:
1. Input stage sets recording level
2. Signal records to tape (saturation)
3. Playback head reads (head bump from head geometry)
4. Playback EQ (HF rolloff from tape properties)
5. Transport adds timing variation (wow/flutter)
6. Tape noise adds during playback

---

## Parameter Smoothing Strategy

### Decision: 5ms OnePoleSmoother for All Parameters

**Decision**: Apply 5ms smoothing to all continuously-variable parameters

**Parameters Requiring Smoothing**:
- Input/Output level (gain changes)
- Head bump amount and frequency
- HF rolloff amount and frequency
- Wow and flutter amounts
- Hiss amount

**Parameters NOT Requiring Smoothing**:
- Machine model (discrete, triggers preset load)
- Tape type (discrete, modifies saturator params)
- Tape speed (discrete, modifies filter defaults)
- Hysteresis solver (discrete)

**Configuration**:
```cpp
static constexpr float kSmoothingTimeMs = 5.0f;  // SC-006
```

---

## Existing Component Integration

### TapeSaturator Integration

The TapeSaturator from spec 062 provides:
- Simple (tanh) and Hysteresis (Jiles-Atherton) saturation models
- Internal DC blocking (10Hz)
- Internal pre/de-emphasis filtering
- Parameter smoothing

**Interface points**:
```cpp
saturator_.prepare(sampleRate, maxBlockSize);
saturator_.setDrive(userDrive_ + internalDriveOffset_);
saturator_.setSaturation(userSaturation_ * saturationMultiplier_);
saturator_.setBias(userBias_ + biasOffset_);
saturator_.setMix(1.0f);  // Always 100% wet for tape path
saturator_.process(buffer, numSamples);
```

### NoiseGenerator Integration

The NoiseGenerator supports TapeHiss type with high-frequency emphasis:
```cpp
noiseGen_.prepare(static_cast<float>(sampleRate), maxBlockSize);
noiseGen_.setNoiseEnabled(NoiseType::TapeHiss, hissAmount_ > 0.0f);
noiseGen_.setNoiseLevel(NoiseType::TapeHiss, hissLevelDb);
noiseGen_.processMix(buffer, buffer, numSamples);  // Adds hiss to signal
```

### LFO Integration

Two LFO instances for wow and flutter:
```cpp
// Configure in prepare()
wowLfo_.prepare(sampleRate);
wowLfo_.setWaveform(Waveform::Triangle);
wowLfo_.setFrequency(wowRate_);  // 0.1-2.0 Hz

flutterLfo_.prepare(sampleRate);
flutterLfo_.setWaveform(Waveform::Triangle);
flutterLfo_.setFrequency(flutterRate_);  // 2.0-15.0 Hz

// In process loop:
float wowValue = wowLfo_.process();      // -1 to +1
float flutterValue = flutterLfo_.process();  // -1 to +1
```

### Biquad Integration

Two Biquad instances for head bump and HF rolloff:
```cpp
// Configure in prepare() or when parameters change
headBumpFilter_.configure(FilterType::Peak, headBumpFreq_, 0.8f, headBumpGainDb, sampleRate);
hfRolloffFilter_.configure(FilterType::Lowpass, hfRolloffFreq_, kButterworthQ, 0.0f, sampleRate);

// In process loop:
sample = headBumpFilter_.process(sample);
float filtered = hfRolloffFilter_.process(sample);
sample = sample + hfRolloffAmount_ * (filtered - sample);
```

---

## Summary of Key Decisions

| Area | Decision | Rationale |
|------|----------|-----------|
| Machine Models | Studer + Ampex | Covers two main sonic territories |
| Tape Speeds | 7.5, 15, 30 ips | Standard professional speeds |
| Tape Types | Type456, Type900, TypeGP9 | Distinct saturation characters |
| Head Bump | Peak filter | Natural resonant boost |
| HF Rolloff | Lowpass filter | Authentic 12dB/oct slope |
| Wow/Flutter | Two Triangle LFOs | Separate control, mechanical feel |
| Hiss | NoiseGenerator TapeHiss | Existing component reuse |
| Signal Flow | Physical tape order | Authentic processing chain |
| Smoothing | 5ms one-pole | Click-free parameter changes |
