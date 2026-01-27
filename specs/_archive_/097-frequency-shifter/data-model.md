# Data Model: Frequency Shifter

**Feature**: 097-frequency-shifter
**Date**: 2026-01-24
**Status**: Complete

## Overview

This document defines the data structures, enumerations, and class members for the FrequencyShifter Layer 2 processor.

## Enumerations

### ShiftDirection

```cpp
/// @brief Shift direction for single-sideband modulation
///
/// Determines which sideband(s) appear in the output:
/// - Up: Upper sideband only (input frequency + shift amount)
/// - Down: Lower sideband only (input frequency - shift amount)
/// - Both: Both sidebands (ring modulation effect)
enum class ShiftDirection : uint8_t {
    Up = 0,    ///< Upper sideband only (input + shift)
    Down,      ///< Lower sideband only (input - shift)
    Both       ///< Both sidebands (ring modulation)
};
```

**Mapping**:
| Value | SSB Formula | Effect |
|-------|-------------|--------|
| Up (0) | I*cos - Q*sin | Frequencies shifted upward |
| Down (1) | I*cos + Q*sin | Frequencies shifted downward |
| Both (2) | I*cos | Ring modulation (both sidebands) |

## Class Definition

### FrequencyShifter

```cpp
namespace Krate {
namespace DSP {

/// @brief Frequency shifter using Hilbert transform for SSB modulation.
///
/// Shifts all frequencies by a constant Hz amount (not pitch shifting).
/// Creates inharmonic, metallic effects. Based on the Bode frequency shifter
/// principle using single-sideband modulation.
///
/// @par Layer
/// Layer 2 (Processor) - depends on Layer 0 (core) and Layer 1 (primitives)
///
/// @par Thread Safety
/// Not thread-safe. Create separate instances for each audio channel
/// or use processStereo() for stereo processing.
class FrequencyShifter {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    /// Maximum shift amount in Hz (positive or negative)
    static constexpr float kMaxShiftHz = 5000.0f;

    /// Maximum modulation depth in Hz
    static constexpr float kMaxModDepthHz = 500.0f;

    /// Maximum feedback amount (0.99 to prevent infinite sustain)
    static constexpr float kMaxFeedback = 0.99f;

    /// Oscillator renormalization interval (samples)
    static constexpr int kRenormInterval = 1024;

    // =========================================================================
    // Lifecycle
    // =========================================================================

    FrequencyShifter() noexcept = default;
    ~FrequencyShifter() = default;

    // Non-copyable due to LFO containing vectors
    FrequencyShifter(const FrequencyShifter&) = delete;
    FrequencyShifter& operator=(const FrequencyShifter&) = delete;
    FrequencyShifter(FrequencyShifter&&) noexcept = default;
    FrequencyShifter& operator=(FrequencyShifter&&) noexcept = default;

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // =========================================================================
    // Shift Control
    // =========================================================================

    void setShiftAmount(float hz) noexcept;
    void setDirection(ShiftDirection dir) noexcept;

    // =========================================================================
    // LFO Modulation
    // =========================================================================

    void setModRate(float hz) noexcept;
    void setModDepth(float hz) noexcept;

    // =========================================================================
    // Feedback
    // =========================================================================

    void setFeedback(float amount) noexcept;

    // =========================================================================
    // Mix
    // =========================================================================

    void setMix(float dryWet) noexcept;

    // =========================================================================
    // Processing
    // =========================================================================

    [[nodiscard]] float process(float input) noexcept;
    void processStereo(float& left, float& right) noexcept;

    // =========================================================================
    // Query
    // =========================================================================

    [[nodiscard]] bool isPrepared() const noexcept;
    [[nodiscard]] float getShiftAmount() const noexcept;
    [[nodiscard]] ShiftDirection getDirection() const noexcept;
    [[nodiscard]] float getModRate() const noexcept;
    [[nodiscard]] float getModDepth() const noexcept;
    [[nodiscard]] float getFeedback() const noexcept;
    [[nodiscard]] float getMix() const noexcept;

private:
    // =========================================================================
    // Internal Methods
    // =========================================================================

    void updateOscillator() noexcept;
    void advanceOscillator() noexcept;
    [[nodiscard]] float applySSB(float I, float Q, float shiftSign) noexcept;
    [[nodiscard]] float processInternal(float input, HilbertTransform& hilbert,
                                         float& feedbackState, float shiftSign) noexcept;

    // =========================================================================
    // Member Variables
    // =========================================================================

    // Hilbert transform for analytic signal (separate for stereo)
    HilbertTransform hilbertL_;
    HilbertTransform hilbertR_;

    // Quadrature oscillator state (recurrence relation)
    float cosTheta_ = 1.0f;    ///< cos(current phase)
    float sinTheta_ = 0.0f;    ///< sin(current phase)
    float cosDelta_ = 1.0f;    ///< cos(phase increment)
    float sinDelta_ = 0.0f;    ///< sin(phase increment)
    int renormCounter_ = 0;     ///< Counter for renormalization

    // LFO for modulation
    LFO modLFO_;

    // Feedback state (separate for stereo)
    float feedbackSampleL_ = 0.0f;
    float feedbackSampleR_ = 0.0f;

    // Parameters (raw target values)
    float shiftHz_ = 0.0f;          ///< Base shift amount [-5000, +5000] Hz
    float modDepth_ = 0.0f;         ///< LFO modulation depth [0, 500] Hz
    float feedback_ = 0.0f;         ///< Feedback amount [0, 0.99]
    float mix_ = 1.0f;              ///< Dry/wet mix [0, 1]
    ShiftDirection direction_ = ShiftDirection::Up;

    // Smoothers for click-free parameter changes
    OnePoleSmoother shiftSmoother_;
    OnePoleSmoother feedbackSmoother_;
    OnePoleSmoother mixSmoother_;

    // State
    double sampleRate_ = 44100.0;
    bool prepared_ = false;
};

} // namespace DSP
} // namespace Krate
```

## Member Variable Details

### Hilbert Transform Components

| Member | Type | Initial | Description |
|--------|------|---------|-------------|
| `hilbertL_` | HilbertTransform | default | Hilbert transform for left/mono channel |
| `hilbertR_` | HilbertTransform | default | Hilbert transform for right channel |

### Quadrature Oscillator State

| Member | Type | Initial | Description |
|--------|------|---------|-------------|
| `cosTheta_` | float | 1.0f | Cosine of current oscillator phase |
| `sinTheta_` | float | 0.0f | Sine of current oscillator phase |
| `cosDelta_` | float | 1.0f | Cosine of phase increment per sample |
| `sinDelta_` | float | 0.0f | Sine of phase increment per sample |
| `renormCounter_` | int | 0 | Counter for periodic renormalization |

### Modulation

| Member | Type | Initial | Description |
|--------|------|---------|-------------|
| `modLFO_` | LFO | default | LFO for shift amount modulation |

### Feedback State

| Member | Type | Initial | Description |
|--------|------|---------|-------------|
| `feedbackSampleL_` | float | 0.0f | Previous output for left/mono feedback |
| `feedbackSampleR_` | float | 0.0f | Previous output for right feedback |

### Parameters

| Member | Type | Initial | Range | Description |
|--------|------|---------|-------|-------------|
| `shiftHz_` | float | 0.0f | [-5000, +5000] | Base frequency shift in Hz |
| `modDepth_` | float | 0.0f | [0, 500] | LFO modulation depth in Hz |
| `feedback_` | float | 0.0f | [0, 0.99] | Feedback amount |
| `mix_` | float | 1.0f | [0, 1] | Dry/wet mix (0=dry, 1=wet) |
| `direction_` | ShiftDirection | Up | enum | Shift direction mode |

### Smoothers

| Member | Type | Initial | Description |
|--------|------|---------|-------------|
| `shiftSmoother_` | OnePoleSmoother | default | Smooths shift parameter changes |
| `feedbackSmoother_` | OnePoleSmoother | default | Smooths feedback parameter changes |
| `mixSmoother_` | OnePoleSmoother | default | Smooths mix parameter changes |

### Configuration State

| Member | Type | Initial | Description |
|--------|------|---------|-------------|
| `sampleRate_` | double | 44100.0 | Current sample rate |
| `prepared_` | bool | false | True after prepare() called |

## State Transitions

### Lifecycle States

```
[Uninitialized] --prepare()--> [Prepared] --reset()--> [Prepared]
                                    ^                       |
                                    |_______________________|
```

### Parameter Change Flow

```
setParameter(value)
    |
    v
Store to member (e.g., shiftHz_)
    |
    v
smoother.setTarget(value)
    |
    v
[In process() loop]
    |
    v
smoothedValue = smoother.process()
    |
    v
Use smoothedValue for DSP
```

### Processing Flow (Mono)

```
input
  |
  v
[Add saturated feedback] --> inputWithFeedback = input + tanh(feedbackSample) * feedback
  |
  v
[Hilbert Transform] --> HilbertOutput{I, Q}
  |
  v
[Get LFO value] --> lfoValue = modLFO_.process()
  |
  v
[Calculate effective shift] --> effectiveShift = shiftHz + modDepth * lfoValue
  |
  v
[Update oscillator] --> (uses smoothed effectiveShift)
  |
  v
[Apply SSB modulation] --> wet = applySSB(I, Q, direction)
  |
  v
[Store feedback sample] --> feedbackSample = wet
  |
  v
[Apply mix] --> output = dry * (1-mix) + wet * mix
  |
  v
output
```

### Processing Flow (Stereo)

```
(left, right)
     |
     v
[Process left with +shift] --> leftWet = processInternal(left, hilbertL_, +1)
     |
     v
[Process right with -shift] --> rightWet = processInternal(right, hilbertR_, -1)
     |
     v
[Apply mix to both] --> left = mix blend, right = mix blend
     |
     v
[Advance oscillator once] --> shared between channels
     |
     v
(left, right)
```

## Validation Rules

### Parameter Clamping

| Parameter | Min | Max | Clamping Function |
|-----------|-----|-----|-------------------|
| shiftHz | -5000.0f | +5000.0f | `std::clamp(hz, -kMaxShiftHz, kMaxShiftHz)` |
| modRate | 0.01f | 20.0f | Handled by LFO::setFrequency() |
| modDepth | 0.0f | 500.0f | `std::clamp(hz, 0.0f, kMaxModDepthHz)` |
| feedback | 0.0f | 0.99f | `std::clamp(amount, 0.0f, kMaxFeedback)` |
| mix | 0.0f | 1.0f | `std::clamp(dryWet, 0.0f, 1.0f)` |

### Input Validation

- NaN/Inf inputs: Reset state and return 0.0f (FR-023)
- Denormal outputs: Flush to zero using `detail::flushDenormal()` (FR-024)

## Memory Layout

```
FrequencyShifter object (~17.5 KB estimated):
+0x0000: hilbertL_ (HilbertTransform, ~128 bytes)
+0x0080: hilbertR_ (HilbertTransform, ~128 bytes)
+0x0100: cosTheta_, sinTheta_, cosDelta_, sinDelta_ (16 bytes)
+0x0110: renormCounter_ (4 bytes)
+0x0114: padding (4 bytes)
+0x0118: modLFO_ (LFO, ~16 KB due to wavetables)
+0x4118: feedbackSampleL_, feedbackSampleR_ (8 bytes)
+0x4120: shiftHz_, modDepth_, feedback_, mix_ (16 bytes)
+0x4130: direction_ (1 byte)
+0x4131: padding (7 bytes)
+0x4138: shiftSmoother_ (OnePoleSmoother, ~20 bytes)
+0x414C: feedbackSmoother_ (OnePoleSmoother, ~20 bytes)
+0x4160: mixSmoother_ (OnePoleSmoother, ~20 bytes)
+0x4174: sampleRate_ (8 bytes)
+0x417C: prepared_ (1 byte)
+0x417D: padding (3 bytes)
```

Note: Actual layout depends on compiler padding and alignment. LFO dominates memory usage due to wavetable vectors (~16KB for 4 tables of 2048 floats each).
