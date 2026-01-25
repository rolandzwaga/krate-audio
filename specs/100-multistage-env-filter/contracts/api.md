# API Contract: MultiStageEnvelopeFilter

**Date**: 2026-01-25 | **Spec**: [../spec.md](../spec.md) | **Plan**: [../plan.md](../plan.md)

## Header Location

```cpp
#include <krate/dsp/processors/multistage_env_filter.h>
```

## Namespace

```cpp
namespace Krate::DSP {
```

---

## Class Declaration

```cpp
/// @brief Multi-stage envelope filter with programmable curve shapes
///
/// Provides complex envelope shapes beyond ADSR driving filter movement
/// for evolving pads and textures. Supports up to 8 stages with independent
/// target frequency, transition time, and curve shape.
///
/// @par Features
/// - Up to 8 programmable stages with target, time, and curve
/// - Logarithmic, linear, and exponential curve shapes
/// - Loopable envelope section for rhythmic patterns
/// - Velocity-sensitive modulation depth
/// - Independent release time
///
/// @par Layer
/// Layer 2 (DSP Processor) - depends on Layer 0/1 only
///
/// @par Thread Safety
/// Not thread-safe. Create separate instances for each audio thread.
///
/// @par Real-Time Safety
/// All processing methods are noexcept with zero allocations.
///
/// @example Basic Usage
/// @code
/// MultiStageEnvelopeFilter filter;
/// filter.prepare(44100.0);
///
/// // Configure 4-stage sweep
/// filter.setNumStages(4);
/// filter.setStageTarget(0, 200.0f);   // Stage 0: 200 Hz
/// filter.setStageTarget(1, 2000.0f);  // Stage 1: 2000 Hz
/// filter.setStageTarget(2, 500.0f);   // Stage 2: 500 Hz
/// filter.setStageTarget(3, 800.0f);   // Stage 3: 800 Hz
/// filter.setStageTime(0, 100.0f);     // 100ms each
/// filter.setStageTime(1, 200.0f);
/// filter.setStageTime(2, 150.0f);
/// filter.setStageTime(3, 100.0f);
/// filter.setStageCurve(1, 1.0f);      // Exponential for stage 1
///
/// filter.trigger();  // Start envelope
///
/// // Process audio
/// for (size_t i = 0; i < numSamples; ++i) {
///     buffer[i] = filter.process(buffer[i]);
/// }
/// @endcode
class MultiStageEnvelopeFilter {
public:
    // ... see sections below
};
```

---

## Constants

```cpp
/// Maximum number of envelope stages (FR-002)
static constexpr int kMaxStages = 8;

/// Minimum resonance/Q factor
static constexpr float kMinResonance = 0.1f;

/// Maximum resonance/Q factor
static constexpr float kMaxResonance = 30.0f;

/// Maximum stage transition time in milliseconds (FR-005)
static constexpr float kMaxStageTimeMs = 10000.0f;

/// Maximum release time in milliseconds (FR-017a)
static constexpr float kMaxReleaseTimeMs = 10000.0f;
```

---

## Lifecycle Methods

### prepare (FR-001)

```cpp
/// @brief Prepare the processor for a given sample rate
///
/// Must be called before processing. Initializes internal filter and
/// envelope state. May be called multiple times if sample rate changes.
///
/// @param sampleRate Sample rate in Hz (clamped to >= 1000)
/// @pre None
/// @post Processor is ready for process() calls
/// @post All frequencies are re-validated against Nyquist
void prepare(double sampleRate) noexcept;
```

### reset (FR-007)

```cpp
/// @brief Reset internal state without changing parameters
///
/// Clears envelope state, returns to stage 0, and resets filter.
/// Configuration (stages, loop settings, etc.) is preserved.
///
/// @pre prepare() has been called
/// @post state_ == EnvelopeState::Idle
/// @post currentStage_ == 0
/// @post Filter state cleared
void reset() noexcept;
```

---

## Stage Configuration Methods (FR-003 to FR-006)

### setNumStages (FR-003)

```cpp
/// @brief Set the number of active envelope stages
/// @param stages Number of stages (clamped to [1, kMaxStages])
/// @note Loop bounds are re-validated after this call
void setNumStages(int stages) noexcept;
```

### setStageTarget (FR-004)

```cpp
/// @brief Set the target cutoff frequency for a stage
/// @param stage Stage index (0 to kMaxStages-1, out of range ignored)
/// @param cutoffHz Target frequency in Hz (clamped to [1, sampleRate*0.45])
/// @note Stage 0 is the first TARGET, not initial state. Envelope transitions
///       FROM baseFrequency TO stage 0 target, then through stages 1..N-1.
void setStageTarget(int stage, float cutoffHz) noexcept;
```

### setStageTime (FR-005)

```cpp
/// @brief Set the transition time for a stage
/// @param stage Stage index (0 to kMaxStages-1, out of range ignored)
/// @param ms Transition time in milliseconds (clamped to [0, 10000])
/// @note Time of 0ms performs instant transition to target
void setStageTime(int stage, float ms) noexcept;
```

### setStageCurve (FR-006)

```cpp
/// @brief Set the curve shape for a stage transition
/// @param stage Stage index (0 to kMaxStages-1, out of range ignored)
/// @param curve Curve value (clamped to [-1, +1])
///              -1 = logarithmic (fast start, slow finish)
///               0 = linear (constant rate)
///              +1 = exponential (slow start, fast finish)
void setStageCurve(int stage, float curve) noexcept;
```

---

## Loop Control Methods (FR-008 to FR-010a)

### setLoop (FR-008)

```cpp
/// @brief Enable or disable envelope looping
/// @param enabled true to enable loop, false to disable
/// @note When enabled, envelope loops from loopEnd to loopStart
/// @note Loop is exited when release() is called
void setLoop(bool enabled) noexcept;
```

### setLoopStart (FR-009)

```cpp
/// @brief Set the loop start point
/// @param stage Stage index (clamped to [0, numStages-1])
/// @note If loopEnd < loopStart after this call, loopEnd is clamped
void setLoopStart(int stage) noexcept;
```

### setLoopEnd (FR-010)

```cpp
/// @brief Set the loop end point
/// @param stage Stage index (clamped to [loopStart, numStages-1])
/// @note Loop transition uses loopStart's curve and time (FR-010a)
void setLoopEnd(int stage) noexcept;
```

---

## Filter Configuration Methods (FR-011 to FR-014)

### setResonance (FR-011)

```cpp
/// @brief Set the filter resonance (Q factor)
/// @param q Q value (clamped to [0.1, 30.0])
/// @note 0.7071 is Butterworth (no peak), higher values add resonance
void setResonance(float q) noexcept;
```

### setFilterType (FR-012)

```cpp
/// @brief Set the filter type
/// @param type SVFMode (Lowpass, Bandpass, or Highpass)
/// @note Other SVFMode values are treated as Lowpass
void setFilterType(SVFMode type) noexcept;
```

### setBaseFrequency (FR-013)

```cpp
/// @brief Set the base (minimum) cutoff frequency
/// @param hz Frequency in Hz (clamped to [1, sampleRate*0.45])
/// @note This is the starting point before stage 0 and the release target
void setBaseFrequency(float hz) noexcept;
```

---

## Trigger & Control Methods (FR-015 to FR-018a)

### trigger (FR-015)

```cpp
/// @brief Start the envelope from stage 0
///
/// Triggers the envelope with velocity 1.0. Restarts from stage 0
/// even if envelope is already running.
///
/// @pre prepare() has been called
/// @post state_ == EnvelopeState::Running
/// @post currentStage_ == 0
void trigger() noexcept;
```

### trigger with velocity (FR-016)

```cpp
/// @brief Start the envelope with velocity-sensitive triggering
/// @param velocity Velocity value (clamped to [0.0, 1.0])
/// @note Velocity scales modulation depth based on velocitySensitivity
void trigger(float velocity) noexcept;
```

### release (FR-017)

```cpp
/// @brief Exit loop and begin decay to base frequency
///
/// Immediately begins release phase, decaying from current cutoff
/// to baseFrequency using the configured release time.
///
/// @note Has no effect if envelope is Idle or Complete
/// @note Release time is independent of stage times (FR-017a)
void release() noexcept;
```

### setReleaseTime (FR-017a)

```cpp
/// @brief Set the release decay time
/// @param ms Release time in milliseconds (clamped to [0, 10000])
/// @note This is independent of stage transition times
void setReleaseTime(float ms) noexcept;
```

### setVelocitySensitivity (FR-018, FR-018a)

```cpp
/// @brief Set velocity sensitivity for modulation depth
/// @param amount Sensitivity (clamped to [0.0, 1.0])
///               0.0 = velocity ignored (full depth always)
///               1.0 = velocity directly scales depth
/// @note Velocity scales total range from base to highest target (FR-018a)
void setVelocitySensitivity(float amount) noexcept;
```

---

## Processing Methods (FR-019 to FR-022)

### process (FR-019)

```cpp
/// @brief Process a single audio sample
/// @param input Input audio sample
/// @return Filtered output sample
/// @pre prepare() has been called
/// @note Returns 0 if not prepared
/// @note Returns 0 and resets filter on NaN/Inf input
/// @note Denormals are flushed after processing (FR-022)
[[nodiscard]] float process(float input) noexcept;
```

### processBlock (FR-020)

```cpp
/// @brief Process a block of audio samples in-place
/// @param buffer Audio buffer (modified in-place)
/// @param numSamples Number of samples to process
/// @pre prepare() has been called
/// @note Real-time safe: noexcept, zero allocations (FR-021)
void processBlock(float* buffer, size_t numSamples) noexcept;
```

---

## State Monitoring Methods (FR-023 to FR-027)

### getCurrentCutoff (FR-023)

```cpp
/// @brief Get the current filter cutoff frequency
/// @return Current cutoff in Hz
[[nodiscard]] float getCurrentCutoff() const noexcept;
```

### getCurrentStage (FR-024)

```cpp
/// @brief Get the current envelope stage index
/// @return Stage index (0 to numStages-1)
/// @note Returns 0 when Idle or Complete
/// @note Stage 0 is the first configured TARGET (not baseFrequency).
///       The envelope transitions FROM baseFrequency TO stage 0 on trigger.
[[nodiscard]] int getCurrentStage() const noexcept;
```

### getEnvelopeValue (FR-025)

```cpp
/// @brief Get the current envelope position within stage
/// @return Normalized position [0.0, 1.0]
/// @note Returns 0 when Idle, 1 when Complete
[[nodiscard]] float getEnvelopeValue() const noexcept;
```

### isComplete (FR-026)

```cpp
/// @brief Check if envelope has finished
/// @return true when state is Complete or Idle
[[nodiscard]] bool isComplete() const noexcept;
```

### isRunning (FR-027)

```cpp
/// @brief Check if envelope is actively transitioning
/// @return true when state is Running or Releasing
[[nodiscard]] bool isRunning() const noexcept;
```

---

## Getters (for inspection/testing)

```cpp
[[nodiscard]] int getNumStages() const noexcept;
[[nodiscard]] float getStageTarget(int stage) const noexcept;
[[nodiscard]] float getStageTime(int stage) const noexcept;
[[nodiscard]] float getStageCurve(int stage) const noexcept;
[[nodiscard]] bool getLoop() const noexcept;
[[nodiscard]] int getLoopStart() const noexcept;
[[nodiscard]] int getLoopEnd() const noexcept;
[[nodiscard]] float getResonance() const noexcept;
[[nodiscard]] SVFMode getFilterType() const noexcept;
[[nodiscard]] float getBaseFrequency() const noexcept;
[[nodiscard]] float getReleaseTime() const noexcept;
[[nodiscard]] float getVelocitySensitivity() const noexcept;
[[nodiscard]] bool isPrepared() const noexcept;
```

---

## Error Handling

| Condition | Behavior |
|-----------|----------|
| `process()` before `prepare()` | Returns 0.0f |
| NaN/Inf input | Returns 0.0f, resets filter state |
| Out-of-range stage index | Setter is ignored, getter returns default |
| Invalid parameter values | Clamped to valid range |

---

## Thread Safety

- **Not thread-safe**: Create separate instances per audio thread
- **Parameter changes**: Safe to call setters from any thread (atomic-safe values)
- **Processing**: Must only be called from one thread

---

## Dependencies

```cpp
#include <krate/dsp/primitives/svf.h>       // SVF, SVFMode
#include <krate/dsp/primitives/smoother.h>  // OnePoleSmoother
#include <krate/dsp/core/db_utils.h>        // flushDenormal, isNaN, isInf

#include <algorithm>  // std::clamp, std::max
#include <array>      // std::array
#include <cmath>      // std::pow, std::abs
#include <cstddef>    // size_t
#include <cstdint>    // uint8_t
```
