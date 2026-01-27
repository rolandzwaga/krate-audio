# Data Model: Waveguide Resonator

**Feature Branch**: `085-waveguide-resonator`
**Date**: 2026-01-22
**Layer**: 2 (Processors)

---

## Class: WaveguideResonator

**Purpose**: Digital waveguide implementing bidirectional wave propagation for flute/pipe-like resonances with configurable end reflections, frequency-dependent loss, and dispersion.

**Location**: `dsp/include/krate/dsp/processors/waveguide_resonator.h`

**Namespace**: `Krate::DSP`

### Public Interface

```cpp
class WaveguideResonator {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// Minimum supported frequency in Hz
    static constexpr float kMinFrequency = 20.0f;

    /// Maximum frequency ratio relative to sample rate (below Nyquist)
    static constexpr float kMaxFrequencyRatio = 0.45f;

    /// Minimum delay in samples (prevents instability at very high frequencies)
    static constexpr size_t kMinDelaySamples = 2;

    /// Minimum reflection coefficient
    static constexpr float kMinReflection = -1.0f;

    /// Maximum reflection coefficient
    static constexpr float kMaxReflection = +1.0f;

    /// Maximum loss value (prevents complete signal zeroing)
    static constexpr float kMaxLoss = 0.9999f;

    /// Default smoothing time for parameters (ms)
    static constexpr float kDefaultSmoothingMs = 20.0f;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    /// Default constructor
    WaveguideResonator() noexcept = default;

    /// @brief Prepare the waveguide for processing.
    /// @param sampleRate Sample rate in Hz
    /// @note FR-020: Allocates delay lines for 20Hz minimum frequency
    void prepare(double sampleRate) noexcept;

    /// @brief Reset all state to silence.
    /// @note FR-021: Clears delay lines, filters, and smoothers
    void reset() noexcept;

    // =========================================================================
    // Frequency Control
    // =========================================================================

    /// @brief Set the resonant frequency.
    /// @param hz Frequency in Hz
    /// @note FR-002, FR-004: Clamped to [20Hz, sampleRate * 0.45]
    void setFrequency(float hz) noexcept;

    /// @brief Get the current frequency setting.
    /// @return Frequency in Hz
    [[nodiscard]] float getFrequency() const noexcept;

    // =========================================================================
    // End Reflection Control
    // =========================================================================

    /// @brief Set both end reflection coefficients.
    /// @param left Left end reflection [-1.0, +1.0]
    /// @param right Right end reflection [-1.0, +1.0]
    /// @note FR-005, FR-006, FR-007: Kelly-Lochbaum impedance-based reflections
    void setEndReflection(float left, float right) noexcept;

    /// @brief Set left end reflection coefficient.
    /// @param coefficient Reflection [-1.0 = open/inverted, +1.0 = closed/positive]
    void setLeftReflection(float coefficient) noexcept;

    /// @brief Set right end reflection coefficient.
    /// @param coefficient Reflection [-1.0 = open/inverted, +1.0 = closed/positive]
    void setRightReflection(float coefficient) noexcept;

    /// @brief Get left end reflection coefficient.
    [[nodiscard]] float getLeftReflection() const noexcept;

    /// @brief Get right end reflection coefficient.
    [[nodiscard]] float getRightReflection() const noexcept;

    // =========================================================================
    // Loss Control
    // =========================================================================

    /// @brief Set the loss amount (frequency-dependent damping).
    /// @param amount Loss [0.0 = no loss, ~1.0 = maximum loss]
    /// @note FR-008, FR-009, FR-010: Controls OnePoleLP cutoff in feedback
    void setLoss(float amount) noexcept;

    /// @brief Get the current loss setting.
    [[nodiscard]] float getLoss() const noexcept;

    // =========================================================================
    // Dispersion Control
    // =========================================================================

    /// @brief Set the dispersion amount (inharmonicity).
    /// @param amount Dispersion [0.0 = harmonic, higher = more inharmonic]
    /// @note FR-011, FR-012, FR-013: Controls Allpass1Pole frequency
    void setDispersion(float amount) noexcept;

    /// @brief Get the current dispersion setting.
    [[nodiscard]] float getDispersion() const noexcept;

    // =========================================================================
    // Excitation Point Control
    // =========================================================================

    /// @brief Set the excitation/output point position along the waveguide.
    /// @param position Position [0.0 = left end, 1.0 = right end, 0.5 = center]
    /// @note FR-014, FR-015, FR-016: Controls input injection and output tap
    void setExcitationPoint(float position) noexcept;

    /// @brief Get the current excitation point position.
    [[nodiscard]] float getExcitationPoint() const noexcept;

    // =========================================================================
    // Processing
    // =========================================================================

    /// @brief Process a single sample.
    /// @param input Input sample (excitation signal)
    /// @return Resonated output sample
    /// @note FR-022, FR-023, FR-024, FR-025, FR-026, FR-027
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process a block of samples in-place.
    /// @param buffer Audio buffer (input, modified to output)
    /// @param numSamples Number of samples
    void processBlock(float* buffer, size_t numSamples) noexcept;

    /// @brief Process a block with separate input/output buffers.
    /// @param input Input buffer
    /// @param output Output buffer
    /// @param numSamples Number of samples
    void processBlock(const float* input, float* output, size_t numSamples) noexcept;

    // =========================================================================
    // Query
    // =========================================================================

    /// @brief Check if the waveguide has been prepared.
    [[nodiscard]] bool isPrepared() const noexcept;

private:
    // =========================================================================
    // Components
    // =========================================================================

    DelayLine rightGoingDelay_;  ///< Right-going wave delay line
    DelayLine leftGoingDelay_;   ///< Left-going wave delay line

    OnePoleLP rightLossFilter_;  ///< Loss filter in right-going path
    OnePoleLP leftLossFilter_;   ///< Loss filter in left-going path

    Allpass1Pole rightDispersionFilter_;  ///< Dispersion in right-going path
    Allpass1Pole leftDispersionFilter_;   ///< Dispersion in left-going path

    DCBlocker dcBlocker_;  ///< DC blocking at output

    OnePoleSmoother frequencySmoother_;   ///< Smooth frequency changes
    OnePoleSmoother lossSmoother_;        ///< Smooth loss changes
    OnePoleSmoother dispersionSmoother_;  ///< Smooth dispersion changes

    // =========================================================================
    // Parameters
    // =========================================================================

    double sampleRate_ = 44100.0;
    float frequency_ = 440.0f;
    float leftReflection_ = -1.0f;   ///< Default: open end (inverted)
    float rightReflection_ = -1.0f;  ///< Default: open end (inverted)
    float loss_ = 0.1f;
    float dispersion_ = 0.0f;
    float excitationPoint_ = 0.5f;   ///< Default: center

    // =========================================================================
    // State
    // =========================================================================

    float rightGoingWave_ = 0.0f;  ///< Current right-going wave sample
    float leftGoingWave_ = 0.0f;   ///< Current left-going wave sample
    float delaySamplesPerDirection_ = 50.0f;  ///< Delay per direction

    bool prepared_ = false;

    // =========================================================================
    // Private Methods
    // =========================================================================

    /// Update delay line lengths based on smoothed frequency
    void updateDelayLength() noexcept;

    /// Update loss filter cutoffs based on smoothed loss
    void updateLossFilters() noexcept;

    /// Update dispersion filter frequencies based on smoothed dispersion
    void updateDispersionFilters() noexcept;
};
```

---

## Parameter Specifications

### Frequency

| Property | Value |
|----------|-------|
| Range | 20 Hz to sampleRate * 0.45 |
| Default | 440.0 Hz |
| Smoothed | Yes (OnePoleSmoother) |
| Unit | Hz |

**Mapping to Delay**:
```cpp
float totalDelaySamples = sampleRate / frequency;
float delayPerDirection = totalDelaySamples / 2.0f;
// Compensate for filter phase delays
delayPerDirection -= (dispersionPhaseDelay + lossPhaseDelay);
```

### End Reflections

| Property | Left | Right |
|----------|------|-------|
| Range | -1.0 to +1.0 | -1.0 to +1.0 |
| Default | -1.0 (open) | -1.0 (open) |
| Smoothed | No | No |

**Physical Interpretation**:
- -1.0: Open end (inverted reflection, like flute embouchure)
- +1.0: Closed end (positive reflection, like closed pipe)
- 0.0: Absorbing end (no reflection)

### Loss

| Property | Value |
|----------|-------|
| Range | 0.0 to 0.9999 |
| Default | 0.1 |
| Smoothed | Yes (OnePoleSmoother) |

**Mapping to Filter Cutoff**:
```cpp
// Higher loss = lower cutoff = faster HF decay
float maxCutoff = sampleRate * 0.45f;
float minCutoff = frequency;  // Don't cut below fundamental
float cutoff = maxCutoff - loss * (maxCutoff - minCutoff);
```

### Dispersion

| Property | Value |
|----------|-------|
| Range | 0.0 to 1.0 (clamped for stability) |
| Default | 0.0 |
| Smoothed | Yes (OnePoleSmoother) |

**Mapping to Allpass Frequency**:
```cpp
// Higher dispersion = lower break frequency = more phase dispersion
float maxFreq = sampleRate * 0.4f;
float minFreq = 100.0f;
float breakFreq = maxFreq - dispersion * (maxFreq - minFreq);
```

### Excitation Point

| Property | Value |
|----------|-------|
| Range | 0.0 to 1.0 |
| Default | 0.5 (center) |
| Smoothed | No |

**Input Distribution**:
```cpp
float rightGoingInjection = input * (1.0f - excitationPoint);
float leftGoingInjection = input * excitationPoint;
```

**Output Tap Position**:
```cpp
// Read from both delay lines at position corresponding to excitation point
size_t rightReadOffset = static_cast<size_t>(excitationPoint * delayPerDirection);
size_t leftReadOffset = static_cast<size_t>((1.0f - excitationPoint) * delayPerDirection);
float output = rightGoingDelay.read(rightReadOffset) + leftGoingDelay.read(leftReadOffset);
```

---

## Processing Algorithm

### Per-Sample Processing (process())

```cpp
float WaveguideResonator::process(float input) noexcept {
    // 1. Input validation
    if (detail::isNaN(input) || detail::isInf(input)) {
        reset();
        return 0.0f;
    }

    // 2. Update smoothed parameters
    float smoothedFreq = frequencySmoother_.process();
    float smoothedLoss = lossSmoother_.process();
    float smoothedDispersion = dispersionSmoother_.process();

    // 3. Update filter coefficients if parameters changed
    if (!frequencySmoother_.isComplete()) {
        updateDelayLength();
    }
    if (!lossSmoother_.isComplete()) {
        updateLossFilters();
    }
    if (!dispersionSmoother_.isComplete()) {
        updateDispersionFilters();
    }

    // 4. Read from delay lines at excitation point position
    float rightGoingAtPoint = rightGoingDelay_.readAllpass(rightReadOffset_);
    float leftGoingAtPoint = leftGoingDelay_.readAllpass(leftReadOffset_);

    // 5. Compute output (sum of waves at excitation point)
    float output = rightGoingAtPoint + leftGoingAtPoint;

    // 6. Read from delay lines at ends
    float rightGoingAtRightEnd = rightGoingDelay_.readAllpass(delaySamplesPerDirection_);
    float leftGoingAtLeftEnd = leftGoingDelay_.readAllpass(delaySamplesPerDirection_);

    // 7. Apply end reflections (Kelly-Lochbaum impedance model)
    float reflectedAtRightEnd = rightReflection_ * rightGoingAtRightEnd;
    float reflectedAtLeftEnd = leftReflection_ * leftGoingAtLeftEnd;

    // 8. Apply loss filters to reflected waves
    float lossedRight = rightLossFilter_.process(reflectedAtRightEnd);
    float lossedLeft = leftLossFilter_.process(reflectedAtLeftEnd);

    // 9. Apply dispersion filters
    float dispersedRight = rightDispersionFilter_.process(lossedRight);
    float dispersedLeft = leftDispersionFilter_.process(lossedLeft);

    // 10. Flush denormals
    dispersedRight = detail::flushDenormal(dispersedRight);
    dispersedLeft = detail::flushDenormal(dispersedLeft);

    // 11. Inject input at excitation point
    float rightGoingInjection = input * (1.0f - excitationPoint_);
    float leftGoingInjection = input * excitationPoint_;

    // 12. Write to delay lines (reflected wave + injection)
    //     After right end reflection, wave goes left -> write to leftGoingDelay
    //     After left end reflection, wave goes right -> write to rightGoingDelay
    leftGoingDelay_.write(dispersedRight + leftGoingInjection);
    rightGoingDelay_.write(dispersedLeft + rightGoingInjection);

    // 13. DC block output
    output = dcBlocker_.process(output);

    return output;
}
```

---

## Dependencies

### Layer 0 (Core)

| Dependency | Header | Usage |
|------------|--------|-------|
| `detail::flushDenormal()` | `core/db_utils.h` | Denormal prevention |
| `detail::isNaN()` | `core/db_utils.h` | Input validation |
| `detail::isInf()` | `core/db_utils.h` | Input validation |
| `kPi`, `kTwoPi` | `core/math_constants.h` | Coefficient calculations |

### Layer 1 (Primitives)

| Dependency | Header | Usage |
|------------|--------|-------|
| `DelayLine` | `primitives/delay_line.h` | Two instances for bidirectional waves |
| `OnePoleLP` | `primitives/one_pole.h` | Two instances for loss filtering |
| `Allpass1Pole` | `primitives/allpass_1pole.h` | Two instances for dispersion |
| `DCBlocker` | `primitives/dc_blocker.h` | One instance for DC blocking |
| `OnePoleSmoother` | `primitives/smoother.h` | Three instances for parameter smoothing |

---

## Test File Structure

**Location**: `dsp/tests/unit/processors/waveguide_resonator_test.cpp`

### Test Categories

1. **Lifecycle Tests**
   - prepare() initializes delay lines
   - reset() clears state
   - Unprepared returns input unchanged

2. **Pitch Accuracy Tests** (SC-002)
   - Verify 1 cent accuracy across frequency range
   - Test open-open configuration fundamental
   - Test open-closed configuration (half frequency)

3. **End Reflection Tests**
   - Open-open produces full harmonic series
   - Closed-closed produces full harmonic series
   - Open-closed produces odd harmonics only
   - Partial reflections produce faster decay

4. **Loss Tests** (SC-005)
   - Zero loss: indefinite resonance
   - Higher loss: faster decay
   - HF decays faster than LF

5. **Dispersion Tests** (SC-006)
   - Zero dispersion: harmonic partials
   - Non-zero dispersion: inharmonic partials

6. **Excitation Point Tests** (SC-007)
   - Center excitation attenuates even harmonics
   - End excitation favors all harmonics

7. **Stability Tests** (SC-009)
   - 30 second continuous operation
   - No NaN/Inf/denormals
   - No DC accumulation

8. **Performance Tests** (SC-001)
   - < 0.5% CPU at 192kHz

9. **Parameter Smoothing Tests** (SC-010)
   - No clicks on frequency changes
   - No clicks on loss changes
   - No clicks on dispersion changes
