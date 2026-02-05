# Data Model: FOF Formant Oscillator

**Date**: 2026-02-05 | **Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)

## Overview

This document defines the data structures and class interfaces for the FOF Formant Oscillator.

---

## Entities

### FormantData5

Extended formant data with 5 formants (F1-F5) for complete vocal synthesis.

```cpp
/// @brief Extended formant data with 5 formants (F1-F5).
///
/// Extends the existing FormantData (3 formants) to include F4 and F5
/// for more complete vocal synthesis. F4 and F5 add subtle presence
/// and speaker characteristics.
struct FormantData5 {
    std::array<float, 5> frequencies;  ///< F1-F5 center frequencies in Hz
    std::array<float, 5> bandwidths;   ///< BW1-BW5 in Hz
};
```

**Relationships:**
- Used by `FormantOscillator` to store vowel preset data
- Parallel structure to existing `FormantData` for API consistency

### FOFGrain

Internal structure representing a single FOF grain state.

```cpp
/// @brief State of a single FOF grain (damped sinusoidal burst).
///
/// Each grain generates a damped sinusoid at the formant frequency,
/// with a shaped attack envelope and exponential decay.
struct FOFGrain {
    // Sinusoid state
    float phase = 0.0f;           ///< Current sinusoid phase [0, 1)
    float phaseIncrement = 0.0f;  ///< Phase advance per sample

    // Envelope state
    float envelope = 0.0f;        ///< Current envelope amplitude
    float decayFactor = 0.0f;     ///< Exponential decay multiplier (per sample)
    float amplitude = 1.0f;       ///< Base amplitude (from formant amplitude)

    // Timing state
    size_t attackSamples = 0;     ///< Attack phase duration (samples)
    size_t durationSamples = 0;   ///< Total grain duration (samples)
    size_t sampleCounter = 0;     ///< Current position in grain
    size_t age = 0;               ///< Samples since trigger (for recycling)

    // Status
    bool active = false;          ///< Is grain currently sounding
};
```

**State Transitions:**
- **Inactive -> Active**: Triggered at fundamental zero-crossing
- **Active (Attack)**: sampleCounter < attackSamples
- **Active (Decay)**: attackSamples <= sampleCounter < durationSamples
- **Active -> Inactive**: sampleCounter >= durationSamples OR recycled

### FormantGenerator

Per-formant generator containing grain pool and parameters.

```cpp
/// @brief Generator for a single formant (F1, F2, F3, F4, or F5).
///
/// Manages a fixed pool of 8 FOF grains and current formant parameters.
struct FormantGenerator {
    static constexpr size_t kGrainsPerFormant = 8;

    std::array<FOFGrain, kGrainsPerFormant> grains;  ///< Fixed-size grain pool

    // Current parameters (may differ from preset during morphing)
    float frequency = 600.0f;   ///< Current formant center frequency (Hz)
    float bandwidth = 60.0f;    ///< Current bandwidth (Hz)
    float amplitude = 1.0f;     ///< Current amplitude [0, 1]
};
```

**Relationships:**
- Contains 8 `FOFGrain` instances
- Managed by `FormantOscillator`

---

## Vowel Preset Tables

### Extended 5-Formant Vowel Table

```cpp
/// @brief 5-formant vowel data for bass male voice.
///
/// Extends kVowelFormants to include F4 and F5 from Csound formant table.
inline constexpr std::array<FormantData5, kNumVowels> kVowelFormants5 = {{
    // Vowel A: /a/ as in "father" (Vowel::A = 0)
    {{{600.0f, 1040.0f, 2250.0f, 2450.0f, 2750.0f}},   // frequencies
     {{60.0f, 70.0f, 110.0f, 120.0f, 130.0f}}},        // bandwidths

    // Vowel E: /e/ as in "bed" (Vowel::E = 1)
    {{{400.0f, 1620.0f, 2400.0f, 2800.0f, 3100.0f}},
     {{40.0f, 80.0f, 100.0f, 120.0f, 120.0f}}},

    // Vowel I: /i/ as in "see" (Vowel::I = 2)
    {{{250.0f, 1750.0f, 2600.0f, 3050.0f, 3340.0f}},
     {{60.0f, 90.0f, 100.0f, 120.0f, 120.0f}}},

    // Vowel O: /o/ as in "go" (Vowel::O = 3)
    {{{400.0f, 750.0f, 2400.0f, 2600.0f, 2900.0f}},
     {{40.0f, 80.0f, 100.0f, 120.0f, 120.0f}}},

    // Vowel U: /u/ as in "boot" (Vowel::U = 4)
    {{{350.0f, 600.0f, 2400.0f, 2675.0f, 2950.0f}},
     {{40.0f, 80.0f, 100.0f, 120.0f, 120.0f}}},
}};
```

### Default Formant Amplitudes

```cpp
/// @brief Default amplitude scaling for each formant.
///
/// Approximates natural voice spectral rolloff (FR-006).
inline constexpr std::array<float, 5> kDefaultFormantAmplitudes = {
    1.0f,  ///< F1: full amplitude
    0.8f,  ///< F2: slightly reduced
    0.5f,  ///< F3: moderate
    0.3f,  ///< F4: quieter
    0.2f   ///< F5: quietest (adds subtle presence)
};
```

---

## Main Class: FormantOscillator

### Class Declaration

```cpp
/// @brief FOF-based formant oscillator for vowel-like synthesis.
///
/// Generates formant-rich waveforms through summed damped sinusoids
/// (FOF grains) synchronized to the fundamental frequency. Unlike
/// FormantFilter which applies resonances to an input signal, this
/// oscillator generates audio directly.
///
/// @par Layer: 2 (processors/)
/// @par Dependencies: core/phase_utils.h, core/filter_tables.h, core/math_constants.h
///
/// @par Memory Model
/// All grain pools are fixed-size. No allocations in processing.
///
/// @par Thread Safety
/// Single-threaded. All methods must be called from same thread.
///
/// @par Real-Time Safety
/// - prepare(): Allocation-free (just configuration)
/// - All other methods: Real-time safe (noexcept, no allocations)
class FormantOscillator {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr size_t kNumFormants = 5;        ///< Number of formant generators
    static constexpr size_t kGrainsPerFormant = 8;   ///< Grains per formant pool
    static constexpr float kAttackMs = 3.0f;         ///< Attack rise time (ms)
    static constexpr float kGrainDurationMs = 20.0f; ///< Total grain duration (ms)
    static constexpr float kMasterGain = 0.4f;       ///< Output normalization gain

    static constexpr float kMinFundamental = 20.0f;    ///< Minimum fundamental (Hz)
    static constexpr float kMaxFundamental = 2000.0f;  ///< Maximum fundamental (Hz)
    static constexpr float kMinFormantFreq = 20.0f;    ///< Minimum formant frequency (Hz)
    static constexpr float kMinBandwidth = 10.0f;      ///< Minimum bandwidth (Hz)
    static constexpr float kMaxBandwidth = 500.0f;     ///< Maximum bandwidth (Hz)

    // =========================================================================
    // Lifecycle (FR-015, FR-016)
    // =========================================================================

    /// @brief Default constructor.
    FormantOscillator() noexcept;

    /// @brief Destructor.
    ~FormantOscillator() noexcept = default;

    // Non-copyable, movable
    FormantOscillator(const FormantOscillator&) = delete;
    FormantOscillator& operator=(const FormantOscillator&) = delete;
    FormantOscillator(FormantOscillator&&) noexcept = default;
    FormantOscillator& operator=(FormantOscillator&&) noexcept = default;

    /// @brief Initialize for processing at given sample rate (FR-015).
    void prepare(double sampleRate) noexcept;

    /// @brief Reset all grain states without reconfiguring sample rate (FR-016).
    void reset() noexcept;

    // =========================================================================
    // Fundamental Frequency (FR-012, FR-013)
    // =========================================================================

    /// @brief Set the fundamental (pitch) frequency (FR-012).
    ///
    /// @param hz Frequency in Hz, clamped to [20, 2000]
    void setFundamental(float hz) noexcept;

    /// @brief Get current fundamental frequency.
    [[nodiscard]] float getFundamental() const noexcept;

    // =========================================================================
    // Vowel Selection (FR-005, FR-017)
    // =========================================================================

    /// @brief Set discrete vowel preset (FR-017).
    ///
    /// @param vowel Vowel to select (A, E, I, O, U)
    void setVowel(Vowel vowel) noexcept;

    /// @brief Get currently selected vowel.
    [[nodiscard]] Vowel getVowel() const noexcept;

    // =========================================================================
    // Vowel Morphing (FR-007, FR-008)
    // =========================================================================

    /// @brief Morph between two vowels (FR-007).
    ///
    /// @param from Source vowel (at mix=0)
    /// @param to Target vowel (at mix=1)
    /// @param mix Blend position [0, 1]
    void morphVowels(Vowel from, Vowel to, float mix) noexcept;

    /// @brief Set position-based vowel morph (FR-008).
    ///
    /// Position mapping: 0.0=A, 1.0=E, 2.0=I, 3.0=O, 4.0=U
    /// Fractional positions interpolate between adjacent vowels.
    ///
    /// @param position Morph position [0, 4]
    void setMorphPosition(float position) noexcept;

    /// @brief Get current morph position.
    [[nodiscard]] float getMorphPosition() const noexcept;

    // =========================================================================
    // Per-Formant Control (FR-009, FR-010, FR-011)
    // =========================================================================

    /// @brief Set formant center frequency (FR-009).
    ///
    /// @param index Formant index [0-4] (0=F1, 4=F5)
    /// @param hz Frequency in Hz, clamped to [20, 0.45*sampleRate]
    void setFormantFrequency(size_t index, float hz) noexcept;

    /// @brief Set formant bandwidth (FR-010).
    ///
    /// @param index Formant index [0-4]
    /// @param hz Bandwidth in Hz, clamped to [10, 500]
    void setFormantBandwidth(size_t index, float hz) noexcept;

    /// @brief Set formant amplitude (FR-011).
    ///
    /// @param index Formant index [0-4]
    /// @param amp Amplitude [0, 1], 0 disables the formant
    void setFormantAmplitude(size_t index, float amp) noexcept;

    /// @brief Get formant frequency.
    [[nodiscard]] float getFormantFrequency(size_t index) const noexcept;

    /// @brief Get formant bandwidth.
    [[nodiscard]] float getFormantBandwidth(size_t index) const noexcept;

    /// @brief Get formant amplitude.
    [[nodiscard]] float getFormantAmplitude(size_t index) const noexcept;

    // =========================================================================
    // Processing (FR-018, FR-019)
    // =========================================================================

    /// @brief Generate single output sample (FR-018).
    ///
    /// @return Output sample, normalized by master gain (0.4)
    [[nodiscard]] float process() noexcept;

    /// @brief Generate block of output samples (FR-019).
    ///
    /// @param output Destination buffer
    /// @param numSamples Number of samples to generate
    void processBlock(float* output, size_t numSamples) noexcept;

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Check if oscillator is prepared.
    [[nodiscard]] bool isPrepared() const noexcept;

    /// @brief Get current sample rate.
    [[nodiscard]] double getSampleRate() const noexcept;

private:
    // =========================================================================
    // Private Methods
    // =========================================================================

    /// @brief Trigger new grains in all formants.
    void triggerGrains() noexcept;

    /// @brief Find oldest active grain in a formant for recycling.
    [[nodiscard]] FOFGrain* findOldestGrain(FormantGenerator& formant) noexcept;

    /// @brief Initialize a grain with current formant parameters.
    void initializeGrain(FOFGrain& grain, size_t formantIndex) noexcept;

    /// @brief Process a single grain, returns its contribution.
    [[nodiscard]] float processGrain(FOFGrain& grain) noexcept;

    /// @brief Process all grains in a formant, returns summed output.
    [[nodiscard]] float processFormant(FormantGenerator& formant) noexcept;

    /// @brief Apply vowel preset to formant parameters.
    void applyVowelPreset(Vowel vowel) noexcept;

    /// @brief Interpolate between two vowel presets.
    void interpolateVowels(Vowel from, Vowel to, float mix) noexcept;

    /// @brief Clamp formant frequency to valid range.
    [[nodiscard]] float clampFormantFrequency(float hz) const noexcept;

    // =========================================================================
    // Members
    // =========================================================================

    // Formant generators
    std::array<FormantGenerator, kNumFormants> formants_;

    // Fundamental frequency tracking
    PhaseAccumulator fundamentalPhase_;
    float fundamental_ = 110.0f;  ///< Current fundamental frequency (Hz)

    // Vowel state
    Vowel currentVowel_ = Vowel::A;
    float morphPosition_ = 0.0f;
    bool useMorphMode_ = false;

    // Configuration
    double sampleRate_ = 44100.0;
    size_t attackSamples_ = 0;     ///< Attack duration in samples
    size_t durationSamples_ = 0;   ///< Grain duration in samples
    bool prepared_ = false;
};
```

---

## Validation Rules

### Parameter Clamping

| Parameter | Min | Max | Default |
|-----------|-----|-----|---------|
| Fundamental (Hz) | 20 | 2000 | 110 |
| Formant Frequency (Hz) | 20 | 0.45 * sampleRate | (from preset) |
| Bandwidth (Hz) | 10 | 500 | (from preset) |
| Amplitude | 0 | 1 | (from kDefaultFormantAmplitudes) |
| Morph Position | 0 | 4 | 0 |
| Morph Mix | 0 | 1 | 0 |

### State Invariants

1. Exactly 5 formant generators always exist
2. Each formant has exactly 8 grain slots
3. Sample rate must be set before processing
4. At most 40 grains can be active simultaneously (5 * 8)

---

## Memory Layout

```
FormantOscillator (estimated size)
├── formants_[5]                 // 5 * FormantGenerator
│   ├── grains[8]                //   8 * FOFGrain (~48 bytes each)
│   ├── frequency                //   4 bytes
│   ├── bandwidth                //   4 bytes
│   └── amplitude                //   4 bytes
├── fundamentalPhase_            // 16 bytes (PhaseAccumulator)
├── fundamental_                 // 4 bytes
├── currentVowel_                // 1 byte
├── morphPosition_               // 4 bytes
├── useMorphMode_                // 1 byte
├── sampleRate_                  // 8 bytes
├── attackSamples_               // 8 bytes
├── durationSamples_             // 8 bytes
└── prepared_                    // 1 byte

Estimated total: ~2.5 KB
```

All state is inline (no heap allocations).
