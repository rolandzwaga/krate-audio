# Data Model: Feedback Distortion Processor

**Feature**: 110-feedback-distortion
**Date**: 2026-01-26

## Entity Overview

### FeedbackDistortion (Layer 2 Processor)

A DSP processor that creates sustained, singing distortion through a feedback delay loop with saturation and soft limiting.

## Class Definition

```cpp
namespace Krate::DSP {

class FeedbackDistortion {
public:
    // === Constants ===

    // Delay Time (FR-004, FR-005)
    static constexpr float kMinDelayMs = 1.0f;
    static constexpr float kMaxDelayMs = 100.0f;
    static constexpr float kDefaultDelayMs = 10.0f;  // 100Hz resonance

    // Feedback (FR-007, FR-008)
    static constexpr float kMinFeedback = 0.0f;
    static constexpr float kMaxFeedback = 1.5f;
    static constexpr float kDefaultFeedback = 0.8f;

    // Drive (FR-013, FR-014)
    static constexpr float kMinDrive = 0.1f;
    static constexpr float kMaxDrive = 10.0f;
    static constexpr float kDefaultDrive = 1.0f;

    // Limiter Threshold (FR-016, FR-017)
    static constexpr float kMinThresholdDb = -24.0f;
    static constexpr float kMaxThresholdDb = 0.0f;
    static constexpr float kDefaultThresholdDb = -6.0f;

    // Tone Frequency (FR-020, FR-022)
    static constexpr float kMinToneHz = 20.0f;
    static constexpr float kMaxToneHz = 20000.0f;
    static constexpr float kDefaultToneHz = 5000.0f;

    // Internal (FR-019a, FR-019b, FR-006)
    static constexpr float kLimiterAttackMs = 0.5f;
    static constexpr float kLimiterReleaseMs = 50.0f;
    static constexpr float kSmoothingTimeMs = 10.0f;
    static constexpr float kDCBlockerCutoffHz = 10.0f;

    // === Lifecycle ===
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;

    // === Processing ===
    [[nodiscard]] float process(float x) noexcept;
    void process(float* buffer, size_t n) noexcept;

    // === Parameters ===
    void setDelayTime(float ms) noexcept;
    void setFeedback(float amount) noexcept;
    void setSaturationCurve(WaveshapeType type) noexcept;
    void setDrive(float drive) noexcept;
    void setLimiterThreshold(float dB) noexcept;
    void setToneFrequency(float hz) noexcept;

    // === Getters ===
    [[nodiscard]] float getDelayTime() const noexcept;
    [[nodiscard]] float getFeedback() const noexcept;
    [[nodiscard]] WaveshapeType getSaturationCurve() const noexcept;
    [[nodiscard]] float getDrive() const noexcept;
    [[nodiscard]] float getLimiterThreshold() const noexcept;
    [[nodiscard]] float getToneFrequency() const noexcept;
    [[nodiscard]] constexpr size_t getLatency() const noexcept { return 0; }

private:
    // Components
    DelayLine delayLine_;
    Waveshaper saturation_;
    Biquad toneFilter_;
    DCBlocker dcBlocker_;
    EnvelopeFollower limiterEnvelope_;

    // Smoothers
    OnePoleSmoother delayTimeSmoother_;
    OnePoleSmoother feedbackSmoother_;
    OnePoleSmoother driveSmoother_;
    OnePoleSmoother thresholdSmoother_;
    OnePoleSmoother toneFreqSmoother_;

    // Parameters
    float delayTimeMs_ = kDefaultDelayMs;
    float feedback_ = kDefaultFeedback;
    float drive_ = kDefaultDrive;
    float limiterThresholdDb_ = kDefaultThresholdDb;
    float toneFrequencyHz_ = kDefaultToneHz;

    // Cached
    float limiterThresholdLinear_ = 0.5f;
    float sampleRate_ = 44100.0f;

    // State
    float feedbackSample_ = 0.0f;
    bool prepared_ = false;
};

} // namespace Krate::DSP
```

## State Transitions

```
┌─────────────┐      prepare()      ┌──────────────┐
│ Unprepared  │ ─────────────────▶  │   Prepared   │
│  (default)  │                     │   (active)   │
└─────────────┘                     └──────────────┘
       ▲                                   │
       │                                   │
       │            reset()                │
       │    (clears state, not buffers)    │
       └───────────────────────────────────┘
```

## Validation Rules

| Parameter | Type | Range | Clamping |
|-----------|------|-------|----------|
| delayTimeMs | float | [1.0, 100.0] | std::clamp |
| feedback | float | [0.0, 1.5] | std::clamp |
| drive | float | [0.1, 10.0] | std::clamp |
| limiterThresholdDb | float | [-24.0, 0.0] | std::clamp |
| toneFrequencyHz | float | [20.0, min(20000.0, sr*0.45)] | std::clamp |
| saturationCurve | WaveshapeType | enum values | none (enum type-safe) |

## Signal Flow

```
Input (x)
    │
    ▼
    ┌───────────────────┐
    │  + feedbackSample │◄───────────────────────────────────┐
    └───────────────────┘                                    │
    │                                                        │
    ▼                                                        │
    ┌───────────────────┐                                    │
    │    DelayLine      │  ◄── readLinear(smoothedDelaySamples)
    │    (write, read)  │                                    │
    └───────────────────┘                                    │
    │                                                        │
    ▼                                                        │
    ┌───────────────────┐                                    │
    │    Waveshaper     │  ◄── smoothedDrive, saturationCurve
    │   (saturation)    │                                    │
    └───────────────────┘                                    │
    │                                                        │
    ▼                                                        │
    ┌───────────────────┐                                    │
    │      Biquad       │  ◄── smoothedToneFreq, Q=0.707     │
    │  (lowpass filter) │                                    │
    └───────────────────┘                                    │
    │                                                        │
    ▼                                                        │
    ┌───────────────────┐                                    │
    │    DCBlocker      │  ◄── removes asymmetric DC         │
    │                   │                                    │
    └───────────────────┘                                    │
    │                                                        │
    ▼                                                        │
    ┌───────────────────┐                                    │
    │  EnvelopeFollower │  ◄── peak mode, 0.5ms atk, 50ms rel
    │    (limiter)      │                                    │
    └───────────────────┘                                    │
    │                                                        │
    ▼                                                        │
    ┌───────────────────┐                                    │
    │  Tanh Soft Clip   │  ◄── smoothedThreshold             │
    │  (gain reduction) │                                    │
    └───────────────────┘                                    │
    │                                                        │
    ├──────────────────────────────────────────────────────▶ │
    │                    * smoothedFeedback                  │
    ▼
Output
```

## Relationships

### Dependencies (Composition)

| Component | Layer | Role |
|-----------|-------|------|
| DelayLine | 1 | Feedback delay (0.1s max at 192kHz) |
| Waveshaper | 1 | Saturation with selectable curve |
| Biquad | 1 | Tone filter (lowpass, Butterworth) |
| DCBlocker | 1 | DC offset removal |
| EnvelopeFollower | 2 | Limiter level tracking |
| OnePoleSmoother | 1 | Parameter smoothing (x5) |

### Layer Compliance

- **This component**: Layer 2 (DSP Processor)
- **Dependencies**: Layer 0-1 primitives + Layer 2 EnvelopeFollower
- **Consumers**: Layer 3+ systems, plugin processor
