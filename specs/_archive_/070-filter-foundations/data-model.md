# Data Model: Filter Foundations

**Feature Branch**: `070-filter-foundations`
**Date**: 2026-01-20

## Entity Definitions

### 1. FormantData (struct)

**File**: `dsp/include/krate/dsp/core/filter_tables.h`
**Layer**: 0 (Core)

```cpp
/// @brief Formant frequency and bandwidth data for a single vowel.
///
/// Contains the first three formant frequencies (F1, F2, F3) and their
/// corresponding bandwidths (BW1, BW2, BW3) for vowel synthesis applications.
struct FormantData {
    float f1;   ///< First formant frequency in Hz (typically 250-800 Hz)
    float f2;   ///< Second formant frequency in Hz (typically 600-2200 Hz)
    float f3;   ///< Third formant frequency in Hz (typically 2200-3000 Hz)
    float bw1;  ///< First formant bandwidth in Hz (typically 40-80 Hz)
    float bw2;  ///< Second formant bandwidth in Hz (typically 60-100 Hz)
    float bw3;  ///< Third formant bandwidth in Hz (typically 100-150 Hz)
};
```

**Validation Rules**:
- All frequencies must be positive (> 0)
- f1 < f2 < f3 (formants are ordered by frequency)
- Bandwidths must be positive (> 0)

**State Transitions**: N/A (immutable data)

### 2. Vowel (enum)

**File**: `dsp/include/krate/dsp/core/filter_tables.h`
**Layer**: 0 (Core)

```cpp
/// @brief Vowel selection for type-safe formant table indexing.
enum class Vowel : uint8_t {
    A = 0,  ///< Open front unrounded vowel [a]
    E = 1,  ///< Close-mid front unrounded vowel [e]
    I = 2,  ///< Close front unrounded vowel [i]
    O = 3,  ///< Close-mid back rounded vowel [o]
    U = 4   ///< Close back rounded vowel [u]
};
```

**Validation Rules**:
- Valid range: 0-4 (5 vowels)
- Cast to size_t for array indexing

### 3. OnePoleLP (class)

**File**: `dsp/include/krate/dsp/primitives/one_pole.h`
**Layer**: 1 (Primitives)

```cpp
/// @brief First-order lowpass filter for audio processing.
///
/// Implements a 6dB/octave lowpass filter using the standard one-pole topology.
/// Unlike OnePoleSmoother (parameter smoothing), this is designed for audio
/// signal processing with proper frequency response characteristics.
class OnePoleLP {
private:
    float coefficient_ = 0.0f;  ///< Filter coefficient 'a' in [0, 1)
    float state_ = 0.0f;        ///< Previous output sample y[n-1]
    float cutoffHz_ = 1000.0f;  ///< Current cutoff frequency
    double sampleRate_ = 44100.0; ///< Current sample rate
    bool prepared_ = false;     ///< True after prepare() called

public:
    void prepare(double sampleRate) noexcept;
    void setCutoff(float hz) noexcept;
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;
    void reset() noexcept;
};
```

**Validation Rules**:
- sampleRate > 0 (clamp to 1000.0 if invalid)
- cutoffHz > 0 (clamp to 1.0 if invalid)
- cutoffHz < Nyquist (clamp to sampleRate * 0.495)

**State Transitions**:
```
[Unprepared] --prepare()--> [Prepared] --reset()--> [Prepared]
                                |
                         setCutoff()
                         process()
                         processBlock()
```

### 4. OnePoleHP (class)

**File**: `dsp/include/krate/dsp/primitives/one_pole.h`
**Layer**: 1 (Primitives)

```cpp
/// @brief First-order highpass filter for audio processing.
///
/// Implements a 6dB/octave highpass filter using the differentiator topology.
/// Useful for DC blocking, bass reduction, and as a building block for
/// crossover networks.
class OnePoleHP {
private:
    float coefficient_ = 0.0f;  ///< Filter coefficient 'a' in [0, 1)
    float inputState_ = 0.0f;   ///< Previous input sample x[n-1]
    float outputState_ = 0.0f;  ///< Previous output sample y[n-1]
    float cutoffHz_ = 100.0f;   ///< Current cutoff frequency
    double sampleRate_ = 44100.0; ///< Current sample rate
    bool prepared_ = false;     ///< True after prepare() called

public:
    void prepare(double sampleRate) noexcept;
    void setCutoff(float hz) noexcept;
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;
    void reset() noexcept;
};
```

**Validation Rules**:
- Same as OnePoleLP

**State Transitions**:
- Same as OnePoleLP

### 5. LeakyIntegrator (class)

**File**: `dsp/include/krate/dsp/primitives/one_pole.h`
**Layer**: 1 (Primitives)

```cpp
/// @brief Simple leaky integrator for envelope detection and smoothing.
///
/// Implements y[n] = x[n] + leak * y[n-1] where leak is typically 0.99-0.9999.
/// Sample-rate independent: no prepare() method required.
class LeakyIntegrator {
private:
    float leak_ = 0.999f;   ///< Leak coefficient in [0, 1)
    float state_ = 0.0f;    ///< Accumulated state y[n-1]

public:
    void setLeak(float a) noexcept;
    [[nodiscard]] float getLeak() const noexcept;
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;
    void reset() noexcept;
};
```

**Validation Rules**:
- leak in range [0, 1) - clamp if outside

**State Transitions**:
```
[Ready] --setLeak()--> [Ready]
        --process()--> [Ready]
        --reset()---> [Ready]
```

---

## Filter Design Utilities

### FilterDesign namespace

**File**: `dsp/include/krate/dsp/core/filter_design.h`
**Layer**: 0 (Core)

All functions are free functions in the `FilterDesign` namespace.

#### prewarpFrequency

```cpp
/// @brief Prewarp frequency for bilinear transform compensation.
/// @param freq Desired digital cutoff frequency in Hz
/// @param sampleRate Sample rate in Hz
/// @return Prewarped analog prototype frequency for filter design
[[nodiscard]] constexpr float prewarpFrequency(float freq, double sampleRate) noexcept;
```

**Formula**: `f_prewarped = (sampleRate / pi) * tan(pi * f / sampleRate)`

#### combFeedbackForRT60

```cpp
/// @brief Calculate comb filter feedback coefficient for desired RT60.
/// @param delayMs Delay time in milliseconds
/// @param rt60Seconds Desired reverb decay time (T60) in seconds
/// @return Feedback coefficient in range [0, 1)
[[nodiscard]] constexpr float combFeedbackForRT60(float delayMs, float rt60Seconds) noexcept;
```

**Formula**: `g = 10^(-3 * delayMs / (1000 * rt60Seconds))`

#### chebyshevQ

```cpp
/// @brief Calculate Q value for Chebyshev Type I filter cascade stage.
/// @param stage 0-indexed stage number
/// @param numStages Total number of biquad stages (filter order / 2)
/// @param rippleDb Passband ripple in dB (e.g., 0.5, 1.0, 3.0)
/// @return Q value for the specified stage
[[nodiscard]] constexpr float chebyshevQ(size_t stage, size_t numStages, float rippleDb) noexcept;
```

#### besselQ

```cpp
/// @brief Get Q value for Bessel filter cascade stage.
/// @param stage 0-indexed stage number
/// @param numStages Total filter order (2-8 supported)
/// @return Q value for the specified stage
[[nodiscard]] constexpr float besselQ(size_t stage, size_t numStages) noexcept;
```

#### butterworthPoleAngle

```cpp
/// @brief Calculate pole angle for Butterworth filter.
/// @param k 0-indexed pole number
/// @param N Filter order
/// @return Pole angle in radians
[[nodiscard]] constexpr float butterworthPoleAngle(size_t k, size_t N) noexcept;
```

**Formula**: `theta_k = pi * (2*k + 1) / (2*N)`

---

## Relationships

```
filter_tables.h
    └── FormantData (struct)
    └── Vowel (enum)
    └── kVowelFormants (constexpr array<FormantData, 5>)

filter_design.h
    └── FilterDesign namespace
        ├── prewarpFrequency()
        ├── combFeedbackForRT60()
        ├── chebyshevQ()
        ├── besselQ()
        └── butterworthPoleAngle()
    └── depends on: math_constants.h (kPi)
    └── depends on: db_utils.h (detail::constexprExp)

one_pole.h
    ├── OnePoleLP (class)
    ├── OnePoleHP (class)
    └── LeakyIntegrator (class)
    └── depends on: math_constants.h (kPi, kTwoPi)
    └── depends on: db_utils.h (detail::flushDenormal, detail::isNaN, detail::isInf)
```

---

## Processing Equations

### OnePoleLP (FR-022)

```
a = exp(-2 * pi * cutoff / sampleRate)
y[n] = (1 - a) * x[n] + a * y[n-1]
```

### OnePoleHP (FR-023)

```
a = exp(-2 * pi * cutoff / sampleRate)
y[n] = ((1 + a) / 2) * (x[n] - x[n-1]) + a * y[n-1]
```

### LeakyIntegrator (FR-015)

```
y[n] = x[n] + leak * y[n-1]
```

Where `leak` is typically 0.99-0.9999 for envelope detection.
