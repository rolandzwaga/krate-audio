# Research: Modal Resonator

**Feature**: 086-modal-resonator
**Date**: 2026-01-23
**Status**: Complete

## Research Questions

### RQ1: Impulse-Invariant Transform Implementation for Two-Pole Resonator

**Question**: How do we implement the impulse-invariant transform to create accurate two-pole sinusoidal oscillators for modal synthesis?

**Finding**: The impulse-invariant transform maps continuous-time analog filter poles to discrete-time digital filter poles via the transformation z = e^(sT), where s is the analog pole and T is the sampling interval. For modal synthesis, this creates a two-pole resonator with accurate frequency and decay characteristics.

**Key Formula - Two-Pole Resonator Difference Equation**:
```
y[n] = input * amplitude + a1 * y[n-1] - a2 * y[n-2]
```

Where:
- `a1 = 2 * R * cos(theta)`
- `a2 = R^2`
- `R` = pole radius (determines decay rate)
- `theta` = pole angle (determines frequency)

**Pole Mapping**:
- Angular frequency: `theta = 2 * pi * frequency / sampleRate`
- Pole radius from T60: `R = exp(-6.91 / (T60 * sampleRate))`

The constant 6.91 comes from the definition of T60 (time to decay 60 dB), which equals approximately 6.91 time constants (ln(1000) = 6.908).

**Sources**:
- [CCRMA: Impulse Invariant Method](https://ccrma.stanford.edu/~jos/pasp/Impulse_Invariant_Method.html)
- [CCRMA: Two-Pole Filters](https://ccrma.stanford.edu/~jos/fp/Two_Pole.html)

---

### RQ2: Frequency-Dependent Decay Formula

**Question**: How should frequency-dependent decay be modeled for realistic material behavior?

**Finding**: Physical materials exhibit frequency-dependent damping where higher frequencies decay faster than lower frequencies. This is modeled using the loss factor formula:

```
R_k = b_1 + b_3 * f_k^2
```

Where:
- `R_k` = loss factor for mode k (inverse of time constant, in Hz)
- `b_1` = constant/global damping term (Hz)
- `b_3` = frequency-dependent damping term (seconds)
- `f_k` = frequency of mode k (Hz)

**T60 Conversion**:
```
T60_k = 6.91 / R_k
```

**Effect of Parameters**:
- Higher `b_1` = faster overall decay (shorter T60)
- Higher `b_3` = faster high-frequency decay relative to low frequencies
- Lower `b_3` = more sustained high frequencies (metallic character)

**Extended Model** (for pitch-independent decay):
```
R_k = b_1 + b_3 * f_k^2 / f_0^2 + b_0 * f_0^2
```

This decouples the frequency-dependent decay from the absolute pitch, making low and high notes decay similarly.

**Source**:
- [Nathan Ho: Exploring Modal Synthesis](https://nathan.ho.name/posts/exploring-modal-synthesis/)

---

### RQ3: Material Preset Coefficient Values

**Question**: What are appropriate coefficient values (b_1, b_3) for different material presets?

**Finding**: Literature provides limited specific values. Piano strings use b_1=0.5 Hz, b_3=1.58e-10 s. We derive material presets empirically based on perceptual material characteristics:

**Derived Material Presets** (base frequency: 440 Hz):

| Material | b_1 (Hz) | b_3 (s) | T60 @ 440Hz | T60 @ 2kHz | Character |
|----------|----------|---------|-------------|------------|-----------|
| Wood | 2.0 | 1.0e-7 | ~3.4s | ~0.9s | Warm, quick HF decay |
| Metal | 0.3 | 1.0e-9 | ~23s | ~17s | Bright, sustained |
| Glass | 0.5 | 5.0e-8 | ~13s | ~3.2s | Bright, ringing |
| Ceramic | 1.5 | 8.0e-8 | ~4.5s | ~1.4s | Warm/bright, medium |
| Nylon | 4.0 | 2.0e-7 | ~1.7s | ~0.6s | Dull, heavily damped |

**Design Rationale**:
- **Wood** (marimba, xylophone): Fast high-frequency loss, moderate sustain
- **Metal** (bells, vibraphone): Very slow decay, minimal frequency-dependent loss
- **Glass** (glass bowls, wine glasses): Long sustain with noticeable HF rolloff
- **Ceramic** (clay pots, tiles): Between wood and metal characteristics
- **Nylon** (guitar strings, damped objects): Heavy damping, short sustain

**Mode Frequency Ratios**:
For simplicity, use harmonic ratios (1, 2, 3, 4, ...) as default. Real materials have inharmonic partials which can be achieved by overriding individual mode frequencies.

**Source**:
- [Mutable Instruments Rings Manual](https://pichenettes.github.io/mutable-instruments-documentation/modules/rings/manual/)

---

### RQ4: SIMD Optimization Strategy for 32 Parallel Modes

**Question**: How should the implementation be structured for potential SIMD optimization?

**Finding**: Structure data in arrays suitable for SIMD vectorization, but implement scalar code first for correctness.

**Data Layout for SIMD**:
```cpp
// Structure of Arrays (SoA) layout - SIMD friendly
std::array<float, 32> y1_;      // y[n-1] state for all modes
std::array<float, 32> y2_;      // y[n-2] state for all modes
std::array<float, 32> a1_;      // 2*R*cos(theta) coefficients
std::array<float, 32> a2_;      // R^2 coefficients
std::array<float, 32> gains_;   // Mode amplitudes
std::array<bool, 32> enabled_;  // Mode enable flags
```

**Vectorization Potential**:
- SSE: 4 floats per register = 8 iterations for 32 modes
- AVX: 8 floats per register = 4 iterations for 32 modes
- AVX-512: 16 floats per register = 2 iterations for 32 modes

**Implementation Strategy**:
1. Implement scalar version first with correct behavior
2. Profile to establish baseline
3. Add SIMD only if performance target (1% CPU) not met
4. Use `alignas(32)` on arrays for AVX alignment

**Performance Budget**:
- Target: 1% CPU @ 192kHz = ~26.7 microseconds per 512-sample block
- Per sample: ~52 nanoseconds
- Per mode per sample: ~1.6 nanoseconds (if 32 modes)

This is achievable with scalar code but leaves room for SIMD if needed.

**Source**:
- [DSPRelated: Time-Varying Two-Pole Filters](https://www.dsprelated.com/freebooks/filters/Time_Varying_Two_Pole_Filters.html)

---

## Alternatives Considered

### Alternative 1: Biquad Bandpass Filters (ResonatorBank approach)

**Description**: Use biquad bandpass filters as in the existing ResonatorBank implementation.

**Rejected Because**:
- Less accurate T60 decay specification
- Different excitation response (bandpass vs true oscillator)
- Cannot directly specify decay time; must convert via Q factor
- Less phase coherent for strike excitation

### Alternative 2: State-Variable Filters

**Description**: Use state-variable filter topology which provides simultaneous lowpass, highpass, and bandpass outputs.

**Rejected Because**:
- More complex than needed for modal synthesis
- No clear advantage for decaying sinusoid generation
- Higher computational cost per mode

### Alternative 3: Coupled Form Oscillator

**Description**: Use the coupled form digital oscillator with separate sine/cosine outputs.

**Rejected Because**:
- Numerically less stable for long decays
- Requires renormalization to prevent amplitude drift
- More complex coefficient updates during parameter changes

---

## Resolved Clarifications

| Question | Resolution |
|----------|------------|
| ModalData structure fields | `struct ModalData { float frequency; float t60; float amplitude; };` |
| Smoothing time API | Constructor parameter: `ModalResonator(float smoothingTimeMs = 20.0f)` |
| Strike during resonance | Energy accumulates (adds to existing oscillator state) |
| Performance measurement | Average/max microseconds per 512-sample block; 1% @ 192kHz = ~26.7 us |
| Material preset base frequency | 440 Hz (A4), scalable via setSize() |

---

## Implementation Notes

### Coefficient Calculation (Complete Formula)

```cpp
/// Calculate mode coefficients from frequency and T60 decay time
void calculateModeCoefficients(
    float frequency,
    float t60,
    float sampleRate,
    float& a1,
    float& a2
) {
    // Angular frequency (pole angle)
    const float theta = kTwoPi * frequency / sampleRate;

    // Pole radius from T60
    // T60 = time to decay 60 dB = 6.91 time constants
    // tau = T60 / 6.91 (time constant in seconds)
    // R = exp(-1 / (tau * sampleRate)) = exp(-6.91 / (T60 * sampleRate))
    const float R = std::exp(-6.91f / (t60 * sampleRate));

    // Two-pole coefficients
    a1 = 2.0f * R * std::cos(theta);
    a2 = R * R;
}
```

### Frequency-Dependent Decay Calculation

```cpp
/// Calculate T60 from material coefficients and frequency
float calculateMaterialT60(float frequency, float b1, float b3) {
    // Loss factor: R_k = b_1 + b_3 * f_k^2
    const float lossRate = b1 + b3 * frequency * frequency;

    // T60 = 6.91 / loss_rate
    // Clamp to valid range [0.001, 30] seconds
    const float t60 = 6.91f / lossRate;
    return std::clamp(t60, 0.001f, 30.0f);
}
```

### Strike Implementation

```cpp
/// Excite all modes with an impulse
void strike(float velocity) noexcept {
    velocity = std::clamp(velocity, 0.0f, 1.0f);

    // Add impulse energy to each enabled mode
    // The impulse is added to y1_[k], which propagates on next process()
    for (size_t k = 0; k < kMaxModes; ++k) {
        if (enabled_[k]) {
            // Energy accumulates (adds to existing state)
            y1_[k] += velocity * gains_[k];
        }
    }
}
```
