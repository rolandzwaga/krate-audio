# Modal Resonator Bowed Mode Coupling Contract

## Overview (FR-020)

When the bow exciter is used with the modal resonator, 8 "bowed mode" bandpass velocity taps provide the feedback velocity for the friction computation. The bow's excitation force feeds into all modes weighted by harmonic selectivity based on bow position.

## New API on ModalResonatorBank

```cpp
/// Enable/disable bowed-mode velocity taps.
/// When active, getFeedbackVelocity() returns summed bandpass outputs.
void setBowModeActive(bool active) noexcept;

/// Set bow position for harmonic weighting of excitation input.
/// Weight per mode k: sin((k+1) * pi * bowPosition)
void setBowPosition(float position) noexcept;
```

## Bandpass Velocity Tap Design

Each of the 8 bowed modes has a biquad bandpass filter:

```
Q = 50 (narrow)
fc = modeFrequency[k] for k = 0..7 (first 8 active modes)
```

**Coefficients** (standard biquad BPF):
```
w0 = 2 * pi * fc / sampleRate
alpha = sin(w0) / (2 * Q)
b0 = alpha
b1 = 0
b2 = -alpha
a0 = 1 + alpha
a1 = -2 * cos(w0)
a2 = 1 - alpha
// Normalize by a0
```

**Per-sample flow when bowModeActive_**:
1. `processSampleCore()` computes the full 96-mode output as normal
2. The output is fed through each of the 8 bandpass filters
3. Bandpass outputs are summed into `bowedModeSumVelocity_`
4. `getFeedbackVelocity()` returns `bowedModeSumVelocity_`

## Excitation Input Weighting (FR-024)

When `bowModeActive_`, the excitation input to each mode is weighted:

```cpp
for (int k = 0; k < numActiveModes; ++k) {
    float weight = std::sin(static_cast<float>(k + 1) * kPi * bowPosition_);
    inputGainTarget_[k] = baseGain[k] * weight;
}
```

This implements the sinc-like spectral envelope: bowing at position `beta = 1/n` suppresses the nth harmonic.

## Bandpass Filter State (Internal)

```cpp
struct BowedModeBPF {
    float b0{0.0f}, b2{0.0f};  // b1 is always 0 for BPF
    float a1{0.0f}, a2{0.0f};
    float z1{0.0f}, z2{0.0f};

    void setCoefficients(float freq, float q, double sampleRate) noexcept;
    float process(float input) noexcept;
    void reset() noexcept;
};

static constexpr int kNumBowedModes = 8;
std::array<BowedModeBPF, kNumBowedModes> bowedModeFilters_;
```

## CPU Budget

- 8 biquad filters x 5 multiply-adds each = ~40 ops/sample
- This is comparable to a single biquad cascade
- Negligible compared to the 96-mode Gordon-Smith oscillator bank
