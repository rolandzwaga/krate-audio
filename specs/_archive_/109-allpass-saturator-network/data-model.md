# Data Model: Allpass-Saturator Network

**Feature**: 109-allpass-saturator-network | **Date**: 2026-01-26

## Entities

### NetworkTopology (Enumeration)

Selects the network configuration for the processor.

```cpp
enum class NetworkTopology : uint8_t {
    SingleAllpass = 0,   // Single allpass filter with saturation in feedback
    AllpassChain = 1,    // 4 cascaded allpass filters with saturation
    KarplusStrong = 2,   // Delay + lowpass + saturation (string synthesis)
    FeedbackMatrix = 3   // 4x4 Householder matrix of cross-fed saturators
};
```

| Value | Description | Use Case |
|-------|-------------|----------|
| SingleAllpass | Single resonant stage | Pitched distortion, simple resonance |
| AllpassChain | 4 stages at prime ratios | Bell-like, metallic tones |
| KarplusStrong | Classic string synthesis | Plucked strings, physical modeling |
| FeedbackMatrix | 4-channel dense network | Drones, evolving textures, self-oscillation |

### AllpassSaturator (Main Processor Class)

Primary entity - the Layer 2 processor.

```cpp
class AllpassSaturator {
public:
    // Lifecycle
    void prepare(double sampleRate, size_t maxBlockSize) noexcept;
    void reset() noexcept;

    // Topology Selection
    void setTopology(NetworkTopology topology) noexcept;
    [[nodiscard]] NetworkTopology getTopology() const noexcept;

    // Frequency Control
    void setFrequency(float hz) noexcept;
    [[nodiscard]] float getFrequency() const noexcept;

    // Feedback Control
    void setFeedback(float feedback) noexcept;  // 0.0 to 1.0
    [[nodiscard]] float getFeedback() const noexcept;

    // Saturation Control
    void setSaturationCurve(WaveshapeType type) noexcept;
    [[nodiscard]] WaveshapeType getSaturationCurve() const noexcept;
    void setDrive(float drive) noexcept;  // 0.1 to 10.0
    [[nodiscard]] float getDrive() const noexcept;

    // KarplusStrong-specific
    void setDecay(float seconds) noexcept;  // Only affects KarplusStrong topology
    [[nodiscard]] float getDecay() const noexcept;

    // Processing
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;

private:
    // ... internal state
};
```

#### Member Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| topology_ | NetworkTopology | SingleAllpass | Current network topology |
| sampleRate_ | double | 44100.0 | Current sample rate |
| frequency_ | float | 440.0f | Resonant frequency in Hz |
| feedback_ | float | 0.5f | Feedback amount (0.0-1.0) |
| saturationCurve_ | WaveshapeType | Tanh | Saturation transfer function |
| drive_ | float | 1.0f | Saturation drive (0.1-10.0) |
| decay_ | float | 1.0f | Decay time in seconds (KarplusStrong only) |
| prepared_ | bool | false | Whether prepare() has been called |

### SaturatedAllpassStage (Internal Component)

Single allpass filter with saturation in the feedback loop.

```cpp
class SaturatedAllpassStage {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    void setFrequency(float hz, float sampleRate) noexcept;
    void setDrive(float drive) noexcept;
    void setSaturationCurve(WaveshapeType type) noexcept;

    [[nodiscard]] float process(float input, float feedbackGain) noexcept;

private:
    Biquad allpass_;
    Waveshaper waveshaper_;
    float lastOutput_ = 0.0f;
};
```

**Signal Flow**:
```
input + (lastOutput * feedbackGain) -> allpass -> waveshaper -> softClip -> output
                                                                    |
                                                                    v
                                                              lastOutput
```

### HouseholderMatrix (Internal Component)

4x4 unitary feedback matrix for FeedbackMatrix topology.

```cpp
struct HouseholderMatrix {
    /// Apply Householder reflection to 4-element vector
    static void multiply(const float in[4], float out[4]) noexcept {
        // H = I - 2vv^T where v = [0.5, 0.5, 0.5, 0.5]
        // H[i][j] = (i == j) ? -0.5 : 0.5
        const float sum = in[0] + in[1] + in[2] + in[3];
        out[0] = 0.5f * (sum - 2.0f * in[0]);
        out[1] = 0.5f * (sum - 2.0f * in[1]);
        out[2] = 0.5f * (sum - 2.0f * in[2]);
        out[3] = 0.5f * (sum - 2.0f * in[3]);
    }
};
```

**Matrix Form**:
```
| -0.5   0.5   0.5   0.5 |
|  0.5  -0.5   0.5   0.5 |
|  0.5   0.5  -0.5   0.5 |
|  0.5   0.5   0.5  -0.5 |
```

## Relationships

```
AllpassSaturator
    |
    +-- NetworkTopology (enum: selects processing path)
    |
    +-- [Topology-dependent components]
    |   |
    |   +-- SingleAllpass: 1x SaturatedAllpassStage
    |   |
    |   +-- AllpassChain: 4x Biquad (allpass) + 1x Waveshaper
    |   |
    |   +-- KarplusStrong: 1x DelayLine + 1x OnePoleLP + 1x Waveshaper
    |   |
    |   +-- FeedbackMatrix: 4x SaturatedAllpassStage + HouseholderMatrix
    |
    +-- Parameter Smoothers (3x OnePoleSmoother)
    |   +-- frequencySmoother_
    |   +-- feedbackSmoother_
    |   +-- driveSmoother_
    |
    +-- DCBlocker (in feedback path)
```

## State Transitions

### Topology Change

When `setTopology()` is called:
1. Store new topology value
2. Call `reset()` to clear all internal state
3. Next `process()` call uses new topology

**Rationale**: Prevents artifacts from mismatched state between topologies.

### Parameter Changes

All parameters use 10ms smoothing:
- Target value stored immediately in setter
- Actual value interpolates over ~10ms
- Prevents clicks and pops during automation

### Prepare/Reset

| Method | Memory Allocation | State Reset |
|--------|-------------------|-------------|
| prepare() | Yes (delay buffers) | Yes |
| reset() | No | Yes |

## Validation Rules

| Parameter | Valid Range | Clamping Behavior |
|-----------|-------------|-------------------|
| frequency | [20.0, sampleRate * 0.45] | Clamped silently |
| feedback | [0.0, 0.999] | Clamped silently |
| drive | [0.1, 10.0] | Clamped silently |
| decay | [0.001, 60.0] | Clamped silently |
| saturationCurve | WaveshapeType enum | No validation (enum) |
| topology | NetworkTopology enum | No validation (enum) |

## Edge Cases

| Condition | Behavior |
|-----------|----------|
| NaN/Inf input | Reset state, return 0.0f |
| Unprepared process() | Return input unchanged |
| Topology change mid-block | Reset state, new topology for remaining samples |
| frequency < 20Hz | Clamp to 20Hz |
| frequency > Nyquist/2 | Clamp to sampleRate * 0.45 |
| feedback >= 1.0 | Clamp to 0.999, soft clip handles transients |
| drive = 0 | Waveshaper returns 0 (by design) |

## Memory Layout

```cpp
// Estimated size: ~10KB at 44.1kHz minimum frequency
class AllpassSaturator {
    // Configuration (32 bytes)
    NetworkTopology topology_;        // 1 byte
    // padding                        // 3 bytes
    double sampleRate_;               // 8 bytes
    float frequency_;                 // 4 bytes
    float feedback_;                  // 4 bytes
    float drive_;                     // 4 bytes
    float decay_;                     // 4 bytes
    WaveshapeType saturationCurve_;   // 1 byte
    bool prepared_;                   // 1 byte
    // padding                        // 2 bytes

    // Parameter Smoothers (~60 bytes each = 180 bytes)
    OnePoleSmoother frequencySmoother_;
    OnePoleSmoother feedbackSmoother_;
    OnePoleSmoother driveSmoother_;

    // Shared Components (~300 bytes)
    DCBlocker dcBlocker_;

    // SingleAllpass components (~200 bytes)
    SaturatedAllpassStage singleStage_;

    // AllpassChain components (~800 bytes)
    std::array<Biquad, 4> chainAllpasses_;
    Waveshaper chainWaveshaper_;
    float chainLastOutput_;

    // KarplusStrong components (~8KB for delay buffer)
    DelayLine ksDelay_;
    OnePoleLP ksLowpass_;
    Waveshaper ksWaveshaper_;
    float ksLastOutput_;

    // FeedbackMatrix components (~1KB)
    std::array<SaturatedAllpassStage, 4> matrixStages_;
    std::array<float, 4> matrixLastOutputs_;
};
```
