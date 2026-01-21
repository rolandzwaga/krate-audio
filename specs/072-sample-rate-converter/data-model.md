# Data Model: Sample Rate Converter

**Feature**: 072-sample-rate-converter
**Date**: 2026-01-21

## Entities

### InterpolationType (Enum)

```cpp
/// @brief Interpolation algorithm selection for SampleRateConverter
///
/// Determines the quality/CPU tradeoff for fractional position interpolation.
enum class InterpolationType : uint8_t {
    Linear = 0,   ///< 2-point linear interpolation (fast, lower quality)
    Cubic = 1,    ///< 4-point Hermite interpolation (balanced)
    Lagrange = 2  ///< 4-point Lagrange interpolation (highest quality)
};
```

**Values:**

| Value | Points Used | Quality | CPU Cost | Use Case |
|-------|-------------|---------|----------|----------|
| Linear | 2 | Low | Lowest | Real-time modulation, preview |
| Cubic | 4 | High | Medium | General pitch shifting |
| Lagrange | 4 | Highest | Medium | Critical audio, offline |

**Validation Rules:**
- Must be one of the three defined values
- Default is Linear for performance

---

### SampleRateConverter (Class)

```cpp
/// @brief Layer 1 DSP Primitive - Variable-rate linear buffer playback
///
/// Provides fractional position tracking and high-quality interpolation
/// for playing back linear buffers at variable rates (pitch shifting).
///
/// @par Use Cases
/// - Freeze mode slice playback at different pitches
/// - Simple pitch shifting of captured audio
/// - Granular effect grain playback
/// - Time-stretch building blocks
///
/// @par Thread Safety
/// - All processing methods are noexcept (real-time safe)
/// - No allocations during process() or processBlock()
/// - Single-threaded access assumed (typical audio processing)
///
/// @par Example Usage
/// @code
/// SampleRateConverter converter;
/// converter.prepare(44100.0);
/// converter.setRate(2.0f);  // Octave up
/// converter.setInterpolation(InterpolationType::Cubic);
///
/// // In audio callback:
/// while (!converter.isComplete()) {
///     float sample = converter.process(sliceBuffer, sliceSize);
///     output[i] = sample;
/// }
/// @endcode
class SampleRateConverter;
```

#### Constants

| Constant | Type | Value | Description |
|----------|------|-------|-------------|
| `kMinRate` | `static constexpr float` | `0.25f` | Minimum rate (2 octaves down) |
| `kMaxRate` | `static constexpr float` | `4.0f` | Maximum rate (2 octaves up) |
| `kDefaultRate` | `static constexpr float` | `1.0f` | Normal speed (no pitch change) |

**Rate to Pitch Relationship:**

| Rate | Pitch Effect | Semitones |
|------|--------------|-----------|
| 0.25 | 2 octaves down | -24 |
| 0.5 | 1 octave down | -12 |
| 1.0 | Original pitch | 0 |
| 2.0 | 1 octave up | +12 |
| 4.0 | 2 octaves up | +24 |

#### Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `position_` | `float` | `0.0f` | Current fractional read position in buffer |
| `rate_` | `float` | `1.0f` | Playback rate (clamped to [kMinRate, kMaxRate]) |
| `interpolationType_` | `InterpolationType` | `Linear` | Current interpolation mode |
| `sampleRate_` | `double` | `0.0` | Configured sample rate (for prepare()) |
| `isComplete_` | `bool` | `false` | True when position >= (bufferSize - 1) |

#### State Transitions

```
┌─────────────┐     prepare()      ┌────────────┐
│   Created   │ ──────────────────>│  Prepared  │
└─────────────┘                    └────────────┘
                                         │
                                   reset() or
                                   setPosition(0)
                                         │
                                         v
                                   ┌────────────┐
                            ┌─────>│   Ready    │<─────┐
                            │      └────────────┘      │
                            │            │             │
                       reset()     process() or   setPosition(x)
                            │      processBlock()  where x < end
                            │            │             │
                            │            v             │
                            │      ┌────────────┐      │
                            └──────│  Playing   │──────┘
                                   └────────────┘
                                         │
                                   position >= end
                                         │
                                         v
                                   ┌────────────┐
                            ┌─────>│  Complete  │
                            │      └────────────┘
                            │            │
                            │       reset() or
                            │      setPosition(x)
                            │            │
                            └────────────┘
```

---

## Relationships

```
┌─────────────────────────────────────────────────────────┐
│                   SampleRateConverter                   │
│                                                         │
│  ┌─────────────────────────────────────────────────┐   │
│  │                 Configuration                    │   │
│  │  - rate_: float                                  │   │
│  │  - interpolationType_: InterpolationType         │   │
│  │  - sampleRate_: double                           │   │
│  └─────────────────────────────────────────────────┘   │
│                         │                               │
│                         │ uses                          │
│                         v                               │
│  ┌─────────────────────────────────────────────────┐   │
│  │                    State                         │   │
│  │  - position_: float                              │   │
│  │  - isComplete_: bool                             │   │
│  └─────────────────────────────────────────────────┘   │
│                         │                               │
│                         │ depends on                    │
│                         v                               │
│  ┌─────────────────────────────────────────────────┐   │
│  │            Layer 0: Interpolation                │   │
│  │  - linearInterpolate()                           │   │
│  │  - cubicHermiteInterpolate()                     │   │
│  │  - lagrangeInterpolate()                         │   │
│  └─────────────────────────────────────────────────┘   │
└─────────────────────────────────────────────────────────┘
```

---

## Validation Rules

### Rate Validation

```cpp
void setRate(float rate) noexcept {
    rate_ = std::clamp(rate, kMinRate, kMaxRate);
}
```

| Input | Result | Reason |
|-------|--------|--------|
| `0.0f` | `0.25f` | Clamped to minimum |
| `-1.0f` | `0.25f` | Negative clamped to minimum |
| `0.25f` | `0.25f` | Minimum valid |
| `1.0f` | `1.0f` | Normal speed |
| `4.0f` | `4.0f` | Maximum valid |
| `10.0f` | `4.0f` | Clamped to maximum |

### Position Validation

```cpp
void setPosition(float samples) noexcept {
    // Position clamped to valid range for any future buffer
    position_ = std::max(0.0f, samples);
    isComplete_ = false;  // Allow restart
}
```

| Input | Result | Reason |
|-------|--------|--------|
| `-5.0f` | `0.0f` | Negative clamped to 0 |
| `0.0f` | `0.0f` | Start of buffer |
| `50.5f` | `50.5f` | Valid position |

Note: Position clamping to buffer bounds happens in `process()` where bufferSize is known.

### Edge Sample Reflection

For 4-point interpolation at boundaries:

```cpp
// Internal helper for reflected sample access
auto getSampleReflected = [&](int idx) -> float {
    if (idx < 0) {
        return buffer[0];  // Reflect left edge
    }
    if (idx >= static_cast<int>(bufferSize)) {
        return buffer[bufferSize - 1];  // Reflect right edge
    }
    return buffer[idx];
};
```

| Position | Integer Part | ym1 Index | y0 Index | y1 Index | y2 Index |
|----------|--------------|-----------|----------|----------|----------|
| 0.5 | 0 | 0 (reflected) | 0 | 1 | 2 |
| 1.5 | 1 | 0 | 1 | 2 | 3 |
| 97.5 (N=100) | 97 | 96 | 97 | 98 | 99 |
| 98.5 (N=100) | 98 | 97 | 98 | 99 | 99 (reflected) |

---

## Memory Layout

```cpp
class SampleRateConverter {
private:
    // Configuration (rarely changes)
    double sampleRate_ = 0.0;                           // 8 bytes
    float rate_ = kDefaultRate;                         // 4 bytes
    InterpolationType interpolationType_ = InterpolationType::Linear;  // 1 byte
    // Padding: 3 bytes

    // State (changes every sample)
    float position_ = 0.0f;                             // 4 bytes
    bool isComplete_ = false;                           // 1 byte
    // Padding: 3 bytes

    // Total: ~24 bytes (aligned)
};
```

**No dynamic allocation**: All state is inline. No std::vector or heap usage.

---

## API Summary

### Lifecycle Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `prepare` | `void prepare(double sampleRate) noexcept` | Initialize for sample rate |
| `reset` | `void reset() noexcept` | Reset position to 0, clear complete flag |

### Configuration Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `setRate` | `void setRate(float rate) noexcept` | Set playback rate (clamped) |
| `setInterpolation` | `void setInterpolation(InterpolationType type) noexcept` | Set interpolation mode |
| `setPosition` | `void setPosition(float samples) noexcept` | Set read position |
| `getPosition` | `[[nodiscard]] float getPosition() const noexcept` | Get current position |

### Processing Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| `process` | `[[nodiscard]] float process(const float* buffer, size_t bufferSize) noexcept` | Read one interpolated sample, advance position |
| `processBlock` | `void processBlock(const float* src, size_t srcSize, float* dst, size_t dstSize) noexcept` | Fill output buffer with interpolated samples |
| `isComplete` | `[[nodiscard]] bool isComplete() const noexcept` | Check if end of buffer reached |

---

## Error Handling

| Condition | Behavior | Justification |
|-----------|----------|---------------|
| `buffer == nullptr` | Return `0.0f` | Safe default, no crash |
| `bufferSize == 0` | Return `0.0f`, set `isComplete_ = true` | Empty buffer is complete |
| `process()` before `prepare()` | Return `0.0f` | Safe default |
| Position beyond buffer | Return `0.0f`, set `isComplete_ = true` | Signal end of playback |
| Rate outside [0.25, 4.0] | Clamp to valid range | Prevent extreme behavior |
