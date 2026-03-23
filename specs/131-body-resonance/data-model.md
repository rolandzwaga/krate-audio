# Data Model: Body Resonance (Spec 131)

**Date**: 2026-03-23

## Entities

### 1. BodyModePreset

Constexpr array of 8 modal parameter tuples per preset.

```cpp
struct BodyMode {
    float freq;     // Center frequency in Hz
    float gain;     // Relative gain (negative = anti-phase)
    float qWood;    // Q factor at material=0 (wood)
    float qMetal;   // Q factor at material=1 (metal)
};

static constexpr size_t kBodyModeCount = 8;
static constexpr size_t kBodyPresetCount = 3;  // small, medium, large

// Index: 0=small(violin), 1=medium(guitar), 2=large(cello)
static constexpr std::array<std::array<BodyMode, kBodyModeCount>, kBodyPresetCount> kBodyPresets;
```

**Relationships**: Used by BodyResonance to compute interpolated modal parameters from `size`.

### 2. BodyResonance (Main Processor)

```cpp
class BodyResonance {
    // === Internal State ===

    // Coupling filter (2 biquads: peak EQ + high shelf)
    Biquad couplingPeakEq_;
    Biquad couplingHighShelf_;

    // Modal bank (8 parallel biquads)
    std::array<Biquad, kBodyModeCount> modalBiquads_;
    std::array<float, kBodyModeCount> modalGains_;  // Normalized gains

    // Pole/zero interpolation state (per mode)
    std::array<float, kBodyModeCount> currentR_;
    std::array<float, kBodyModeCount> currentTheta_;
    std::array<float, kBodyModeCount> targetR_;
    std::array<float, kBodyModeCount> targetTheta_;
    std::array<float, kBodyModeCount> currentGain_;
    std::array<float, kBodyModeCount> targetGain_;

    // FDN (4 delay lines)
    static constexpr size_t kFDNLines = 4;
    std::array<std::array<float, 128>, kFDNLines> fdnDelayBuffers_;
    std::array<size_t, kFDNLines> fdnWritePos_;
    std::array<float, kFDNLines> fdnDelayLengths_;     // Fractional
    std::array<float, kFDNLines> fdnAbsorptionState_;   // One-pole state
    std::array<float, kFDNLines> fdnAbsorptionCoeff_;   // p_i
    std::array<float, kFDNLines> fdnAbsorptionGain_;    // g_i

    // First-order crossover state
    float crossoverLpState_ = 0.0f;
    float crossoverAlpha_ = 0.0f;     // exp(-2*pi*fc/sr)

    // Radiation HPF (1 biquad, 12 dB/oct)
    Biquad radiationHpf_;

    // Parameter smoothing
    OnePoleSmoother sizeSmoother_;
    OnePoleSmoother materialSmoother_;
    OnePoleSmoother mixSmoother_;

    // Configuration
    float sampleRate_ = 44100.0f;
    float smoothCoeff_ = 0.0f;  // Per-block R/theta smoothing coefficient
    bool prepared_ = false;
};
```

### 3. VST3 Parameters

| Parameter ID | Name | Type | Range | Default | Unit |
|-------------|------|------|-------|---------|------|
| 850 (kBodySizeId) | Body Size | RangeParameter | 0.0-1.0 | 0.5 | - |
| 851 (kBodyMaterialId) | Material | RangeParameter | 0.0-1.0 | 0.5 | - |
| 852 (kBodyMixId) | Body Mix | RangeParameter | 0.0-1.0 | 0.0 | - |

### 4. Integration Point: InnexusVoice

```cpp
struct InnexusVoice {
    // ... existing fields ...
    Krate::DSP::BodyResonance bodyResonance;  // NEW: per-voice body processor
};
```

## State Transitions

### Parameter Update Flow (per audio block)

```
1. Read smoothed size, material values
2. If size changed since last block:
   a. Compute interpolated modal frequencies (log-linear)
   b. Compute interpolated modal Q factors (linear between wood/metal Q)
   c. Compute target R and theta for each mode
   d. Update FDN delay lengths
   e. Update crossover frequency
   f. Update radiation HPF cutoff
3. If material changed since last block:
   a. Recompute FDN absorption filter coefficients
   b. Recompute coupling filter coefficients
   c. Recompute modal Q factors (material also affects Q)
4. Per-sample processing:
   a. Smooth R, theta toward targets (exponential)
   b. Process through signal chain
   c. Apply mix ramp
```

### Lifecycle

```
prepare(sampleRate) -> [ready]
  - Pre-allocates all buffers
  - Snaps smoothers to defaults
  - Computes initial coefficients

reset() -> [ready]
  - Clears all filter states and delay buffers
  - Does NOT change coefficients or parameters

setParams(size, material, mix) -> [ready]
  - Sets smoother targets
  - Actual coefficient update deferred to next process call

process(input) -> float output  [per-sample]
  - Full signal chain processing

processBlock(input, output, numSamples)  [per-block]
  - Block processing with per-block coefficient updates
```

## Validation Rules

1. **Size**: Clamped to [0.0, 1.0]
2. **Material**: Clamped to [0.0, 1.0]
3. **Mix**: Clamped to [0.0, 1.0]; at 0.0 produces bit-identical bypass
4. **Modal frequencies**: Must stay below Nyquist/2 (i.e., sampleRate/4) for stability margin
5. **Modal R values**: Must be in [0.0, 1.0) -- strictly less than 1.0 for stability
6. **FDN delay lengths**: Minimum 8 samples, scaled by sample rate ratio
7. **Absorption filter gains**: Must be <= 1.0 at all frequencies
8. **Modal gain normalization**: sum(|gain_i|) <= 1.0
