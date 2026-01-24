# Data Model: Audio-Rate Filter FM

**Feature**: 095-audio-rate-filter-fm | **Date**: 2026-01-24

## Overview

This document defines the data structures, enumerations, and class design for the AudioRateFilterFM processor.

---

## Enumerations

### FMModSource

Selects the source of the modulation signal.

```cpp
/// @brief Modulation source selection for filter FM
/// @note Defined separately from other modulation enums to avoid confusion
enum class FMModSource : uint8_t {
    Internal = 0,  ///< Built-in wavetable oscillator
    External = 1,  ///< External modulator input (sidechain)
    Self = 2       ///< Filter output feedback (self-modulation)
};
```

| Value | Integer | Description |
|-------|---------|-------------|
| Internal | 0 | Uses internal wavetable oscillator |
| External | 1 | Uses external modulator signal provided to process() |
| Self | 2 | Uses previous filter output (feedback FM) |

### FMFilterType

Selects the carrier filter response type. Maps to SVFMode internally.

```cpp
/// @brief Filter type selection for carrier filter
/// @note Maps to SVFMode: Lowpass, Highpass, Bandpass, Notch
enum class FMFilterType : uint8_t {
    Lowpass = 0,   ///< 12 dB/oct lowpass
    Highpass = 1,  ///< 12 dB/oct highpass
    Bandpass = 2,  ///< Constant 0 dB peak bandpass
    Notch = 3      ///< Band-reject filter
};
```

| Value | Integer | SVFMode | Description |
|-------|---------|---------|-------------|
| Lowpass | 0 | SVFMode::Lowpass | Attenuates above cutoff |
| Highpass | 1 | SVFMode::Highpass | Attenuates below cutoff |
| Bandpass | 2 | SVFMode::Bandpass | Passes only around cutoff |
| Notch | 3 | SVFMode::Notch | Rejects frequencies around cutoff |

### FMWaveform

Selects the internal oscillator waveform shape.

```cpp
/// @brief Internal oscillator waveform selection
/// @note Sine and Triangle are low-distortion; Sawtooth and Square are harmonic-rich
enum class FMWaveform : uint8_t {
    Sine = 0,      ///< Pure sine wave (lowest THD, <0.1%)
    Triangle = 1,  ///< Triangle wave (low THD, <1%)
    Sawtooth = 2,  ///< Sawtooth wave (bright, all harmonics)
    Square = 3     ///< Square wave (hollow, odd harmonics only)
};
```

| Value | Integer | Harmonics | THD Limit |
|-------|---------|-----------|-----------|
| Sine | 0 | None (fundamental only) | <0.1% |
| Triangle | 1 | Odd harmonics, 1/n^2 rolloff | <1% |
| Sawtooth | 2 | All harmonics, 1/n rolloff | No limit |
| Square | 3 | Odd harmonics, 1/n rolloff | No limit |

---

## Class: AudioRateFilterFM

### Class Declaration

```cpp
namespace Krate {
namespace DSP {

/// @brief Audio-rate filter frequency modulation processor
///
/// Modulates SVF filter cutoff at audio rates (20Hz-20kHz) to create
/// metallic, bell-like, ring modulation-style, and aggressive timbres.
///
/// @par Features
/// - Three modulation sources: Internal oscillator, External, Self-modulation
/// - Four filter types: Lowpass, Highpass, Bandpass, Notch
/// - Four internal oscillator waveforms: Sine, Triangle, Sawtooth, Square
/// - Configurable oversampling: 1x, 2x, or 4x for anti-aliasing
/// - FM depth in octaves (0-6) for intuitive control
///
/// @par Real-Time Safety
/// All processing methods are noexcept and allocation-free after prepare().
///
/// @par Thread Safety
/// Not thread-safe. Create separate instances for each audio channel.
///
/// @par Layer
/// Layer 2 (Processor) - depends on Layer 0 (core) and Layer 1 (primitives)
class AudioRateFilterFM {
    // ... (see full declaration below)
};

} // namespace DSP
} // namespace Krate
```

### Member Variables

#### Configuration State

| Member | Type | Default | Range | Description |
|--------|------|---------|-------|-------------|
| baseSampleRate_ | double | 44100.0 | >= 1000 | Original sample rate |
| oversampledRate_ | double | 44100.0 | computed | baseSampleRate_ * factor |
| maxBlockSize_ | size_t | 512 | > 0 | Maximum samples per block |
| oversamplingFactor_ | int | 1 | 1, 2, 4 | Oversampling multiplier |
| prepared_ | bool | false | - | Preparation flag |

#### Carrier Filter Parameters

| Member | Type | Default | Range | Description |
|--------|------|---------|-------|-------------|
| carrierCutoff_ | float | 1000.0f | [20, sr*0.495] | Base cutoff frequency (Hz) |
| carrierQ_ | float | 0.7071f | [0.5, 20.0] | Q factor (resonance) |
| filterType_ | FMFilterType | Lowpass | enum | Filter response type |

#### Modulator Parameters

| Member | Type | Default | Range | Description |
|--------|------|---------|-------|-------------|
| modSource_ | FMModSource | Internal | enum | Modulation source |
| modulatorFreq_ | float | 440.0f | [0.1, 20000] | Internal osc frequency (Hz) |
| waveform_ | FMWaveform | Sine | enum | Internal osc waveform |
| fmDepth_ | float | 1.0f | [0.0, 6.0] | Modulation depth (octaves) |

#### Internal Oscillator State

| Member | Type | Default | Description |
|--------|------|---------|-------------|
| phase_ | double | 0.0 | Current oscillator phase [0, 1) |
| phaseIncrement_ | double | computed | Phase advance per sample |

#### Wavetables

| Member | Type | Size | Description |
|--------|------|------|-------------|
| sineTable_ | std::array<float, 2048> | 8KB | Pre-computed sine wavetable |
| triangleTable_ | std::array<float, 2048> | 8KB | Pre-computed triangle wavetable |
| sawTable_ | std::array<float, 2048> | 8KB | Pre-computed sawtooth wavetable |
| squareTable_ | std::array<float, 2048> | 8KB | Pre-computed square wavetable |

#### Self-Modulation State

| Member | Type | Default | Description |
|--------|------|---------|-------------|
| previousOutput_ | float | 0.0f | Last filter output for feedback |

#### Composed Components

| Member | Type | Description |
|--------|------|-------------|
| svf_ | SVF | State Variable Filter (carrier) |
| oversampler2x_ | Oversampler<2, 1> | 2x mono oversampler |
| oversampler4x_ | Oversampler<4, 1> | 4x mono oversampler |

#### Pre-allocated Buffers

| Member | Type | Size | Description |
|--------|------|------|-------------|
| oversampledBuffer_ | std::vector<float> | maxBlockSize*4 | Upsampled audio buffer |
| modulatorBuffer_ | std::vector<float> | maxBlockSize*4 | Modulator signal buffer |

### Memory Footprint

| Component | Size | Notes |
|-----------|------|-------|
| Wavetables | 32 KB | 4 tables * 2048 * 4 bytes |
| SVF | ~100 bytes | Filter state and coefficients |
| Oversamplers | ~2 KB each | Filter banks for up/downsampling |
| Buffers | 4*maxBlockSize*4 bytes | Pre-allocated per channel |

Total (at maxBlockSize=512): ~40 KB per instance

---

## Relationships

```
AudioRateFilterFM
    |
    +-- SVF (svf_)                    [Layer 1: carrier filter]
    |       |
    |       +-- SVFMode               [enum: filter type mapping]
    |
    +-- Oversampler<2,1> (oversampler2x_)  [Layer 1: 2x anti-aliasing]
    +-- Oversampler<4,1> (oversampler4x_)  [Layer 1: 4x anti-aliasing]
    |
    +-- FMModSource                   [enum: modulation source]
    +-- FMFilterType                  [enum: filter type selection]
    +-- FMWaveform                    [enum: oscillator waveform]
```

---

## State Transitions

### Preparation State

```
[Unprepared] --prepare()--> [Prepared]
     ^                           |
     |                           |
     +---------(dtor)------------+
```

### Processing State (when Prepared)

```
[Idle] --process()--> [Processing] --complete--> [Idle]
   |                       |
   +--setParameter()-------+  (parameters can change during idle or processing)
```

### Internal Oscillator Phase

```
phase_ in [0.0, 1.0)

Each sample:
  phase_ += phaseIncrement_
  if (phase_ >= 1.0) phase_ -= 1.0
```

---

## Validation Rules

### Parameter Clamping

| Parameter | Validation | Notes |
|-----------|------------|-------|
| carrierCutoff | clamp(hz, 20.0f, sr * 0.495f * factor) | Upper bound is oversampled Nyquist |
| carrierQ | clamp(q, 0.5f, 20.0f) | SVF-safe Q range |
| modulatorFreq | clamp(hz, 0.1f, 20000.0f) | Full audio range |
| fmDepth | clamp(octaves, 0.0f, 6.0f) | Maximum +/- 6 octaves |
| oversamplingFactor | clamp(1 or 2 or 4) | See clamping rules |

### Oversampling Factor Clamping

```cpp
// Clamp to nearest valid value (1, 2, or 4)
if (factor <= 1) return 1;
if (factor <= 3) return 2;  // 2 and 3 both map to 2
return 4;                    // 4 and above map to 4
```

### Self-Modulation Clipping

```cpp
// Hard-clip filter output before using as modulator
float selfModValue = std::clamp(previousOutput_, -1.0f, 1.0f);
```

### Modulated Cutoff Bounds

```cpp
// Clamp modulated frequency to safe range
float modulatedCutoff = carrierCutoff_ * std::pow(2.0f, modulator * fmDepth_);
const float maxFreq = static_cast<float>(oversampledRate_) * SVF::kMaxCutoffRatio;
return std::clamp(modulatedCutoff, 20.0f, maxFreq);
```
