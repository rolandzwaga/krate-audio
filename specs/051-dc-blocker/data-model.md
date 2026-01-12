# Data Model: DC Blocker Primitive

**Feature**: 051-dc-blocker
**Date**: 2026-01-12
**Purpose**: Define class structure and relationships for DCBlocker primitive

---

## Entity Overview

```
+----------------------------------------------------------------------+
|                         Krate::DSP Namespace                          |
+----------------------------------------------------------------------+
|                                                                        |
|  +------------------------------------------------------------------+ |
|  |                          DCBlocker                                | |
|  |                                                                   | |
|  |  State Variables:                                                 | |
|  |    - R_          : float   (pole coefficient, 0.9-0.9999)        | |
|  |    - x1_         : float   (previous input sample)               | |
|  |    - y1_         : float   (previous output sample)              | |
|  |    - prepared_   : bool    (initialization flag)                 | |
|  |    - sampleRate_ : double  (stored for setCutoff)                | |
|  |                                                                   | |
|  |  Lifecycle Methods:                                               | |
|  |    + prepare(sampleRate, cutoffHz)                               | |
|  |    + reset()                                                      | |
|  |    + setCutoff(cutoffHz)                                         | |
|  |                                                                   | |
|  |  Processing Methods:                                              | |
|  |    + process(x) -> float       [noexcept, [[nodiscard]]]         | |
|  |    + processBlock(buffer, n)   [noexcept]                        | |
|  |                                                                   | |
|  +------------------------------------------------------------------+ |
|                                                                        |
|  Layer 0 Dependencies:                                                 |
|  +------------------------------------------------------------------+ |
|  |  detail::flushDenormal(float)   from <krate/dsp/core/db_utils.h> | |
|  |  detail::isNaN(float)           from <krate/dsp/core/db_utils.h> | |
|  +------------------------------------------------------------------+ |
|                                                                        |
+----------------------------------------------------------------------+
```

---

## Entity Definition

### DCBlocker

A lightweight first-order highpass filter optimized for removing DC offset from audio signals.

| Field | Type | Description | Constraints |
|-------|------|-------------|-------------|
| `R_` | `float` | Pole coefficient controlling cutoff | Clamped to [0.9, 0.9999] |
| `x1_` | `float` | Previous input sample | Any float |
| `y1_` | `float` | Previous output sample (state) | Flushed to prevent denormals |
| `prepared_` | `bool` | Whether prepare() has been called | true/false |
| `sampleRate_` | `double` | Stored sample rate for setCutoff() | Clamped >= 1000.0 |

**Default Values (before prepare() is called):**
```cpp
R_ = 0.0f;             // Placeholder - actual value set by prepare()
x1_ = 0.0f;
y1_ = 0.0f;
prepared_ = false;     // CRITICAL: process() returns input unchanged when false
sampleRate_ = 0.0;     // Set by prepare()
```
**Note**: The default R_ value is irrelevant because `prepared_ = false` causes `process()` to return input unchanged. R_ is only meaningful after `prepare()` is called.

**State Transitions:**

```
                     +------------------+
                     |   Unprepared     |
                     | (prepared_=false)|
                     +--------+---------+
                              |
                              | prepare()
                              v
                     +------------------+
            +------->|     Ready        |<-------+
            |        | (prepared_=true) |        |
            |        +--------+---------+        |
            |                 |                  |
            | reset()         | process()        | setCutoff()
            |                 v                  |
            |        +------------------+        |
            +--------+   Processing     +--------+
                     | (state updated)  |
                     +------------------+
```

- **Unprepared -> Ready**: `prepare()` called with valid sample rate
- **Ready -> Processing**: `process()` or `processBlock()` called
- **Processing -> Ready**: Implicit (ready for more processing)
- **Any -> Ready**: `reset()` clears state, preserves R_ and prepared_
- **Ready -> Ready**: `setCutoff()` recalculates R_ without resetting state

---

## Method Signatures

### Lifecycle Methods

```cpp
/// @brief Configure the filter for processing
/// @param sampleRate Sample rate in Hz (clamped to >= 1000)
/// @param cutoffHz Cutoff frequency in Hz (clamped to [1, sampleRate/4])
void prepare(double sampleRate, float cutoffHz = 10.0f) noexcept;

/// @brief Clear all state to zero (preserves configuration)
void reset() noexcept;

/// @brief Change cutoff frequency without full re-preparation
/// @param cutoffHz New cutoff frequency in Hz
void setCutoff(float cutoffHz) noexcept;
```

### Processing Methods

```cpp
/// @brief Process a single sample
/// @param x Input sample
/// @return DC-blocked output (input if not prepared)
[[nodiscard]] float process(float x) noexcept;

/// @brief Process a block of samples in-place
/// @param buffer Audio buffer (modified in-place)
/// @param numSamples Number of samples to process
void processBlock(float* buffer, size_t numSamples) noexcept;
```

---

## Validation Rules

| Rule | Condition | Action |
|------|-----------|--------|
| Sample rate minimum | `sampleRate < 1000` | Clamp to 1000.0 |
| Cutoff minimum | `cutoffHz < 1.0f` | Clamp to 1.0f |
| Cutoff maximum | `cutoffHz > sampleRate/4` | Clamp to sampleRate/4 |
| R coefficient | `R < 0.9 or R > 0.9999` | Clamp to [0.9, 0.9999] |
| Unprepared call | `process() before prepare()` | Return input unchanged |
| Denormal prevention | After computing y1_ | Apply flushDenormal() |

---

## Algorithm Implementation

### Pole Coefficient Calculation

```cpp
// In prepare() and setCutoff():
float clampedCutoff = std::clamp(cutoffHz, 1.0f, static_cast<float>(sampleRate_ / 4.0));
R_ = std::exp(-2.0f * kPi * clampedCutoff / static_cast<float>(sampleRate_));
R_ = std::clamp(R_, 0.9f, 0.9999f);
```

### Filter Processing

```cpp
// In process():
if (!prepared_) return x;

// Difference equation: y[n] = x[n] - x[n-1] + R * y[n-1]
float y = x - x1_ + R_ * y1_;

// Flush denormals on state variable (catches them at source)
y1_ = detail::flushDenormal(y);
x1_ = x;

return y1_;
```

---

## Memory Layout

| Field | Size (bytes) | Offset |
|-------|--------------|--------|
| R_ | 4 | 0 |
| x1_ | 4 | 4 |
| y1_ | 4 | 8 |
| prepared_ | 1 | 12 |
| (padding) | 3 | 13 |
| sampleRate_ | 8 | 16 |
| **Total** | **24** | - |

**Note**: Actual size may vary due to alignment. All fields fit within a single cache line (64 bytes).

---

## Relationships

### Usage Contexts (Consumers)

```
+-----------------------------------------------------------------------+
|                          Usage Contexts                                |
+-----------------------------------------------------------------------+
|                                                                        |
|  FeedbackNetwork (Layer 3) - MIGRATION TARGET                         |
|       Currently: Inline DCBlocker class                                |
|       After: #include <krate/dsp/primitives/dc_blocker.h>             |
|                                                                        |
|  SaturationProcessor (Layer 2) - OPTIONAL REFACTOR                    |
|       Currently: Uses Biquad as highpass                               |
|       Potential: Replace with DCBlocker for lighter weight            |
|                                                                        |
|  Future Layer 2 Processors:                                            |
|       - TubeStage                                                      |
|       - DiodeClipper                                                   |
|       - WavefolderProcessor                                            |
|       - FuzzProcessor                                                  |
|                                                                        |
+-----------------------------------------------------------------------+
```

### Layer Dependencies

```
DCBlocker (Layer 1)
    |
    +-- db_utils.h (Layer 0)
    |       |
    |       +-- detail::flushDenormal()
    |       +-- detail::isNaN()
    |       +-- detail::isInf()
    |       +-- kDenormalThreshold
    |
    +-- <cmath> (stdlib)
    |       |
    |       +-- std::exp() [prepare/setCutoff only]
    |       +-- std::clamp()
    |
    +-- <cstddef> (stdlib)
            |
            +-- size_t
```

---

## Test Coverage Matrix

| Method | Unit Test | Edge Case Test | Performance Test |
|--------|-----------|----------------|------------------|
| Constructor | Yes | - | - |
| prepare() | Yes | Sample rate bounds | - |
| reset() | Yes | - | - |
| setCutoff() | Yes | Cutoff bounds | - |
| process() | Yes | NaN, Inf, unprepared | Operation count |
| processBlock() | Yes | Empty buffer | - |

---

## Comparison with Existing DCBlocker

| Aspect | Inline (feedback_network.h) | New Primitive |
|--------|----------------------------|---------------|
| Location | Nested in system header | Standalone primitive |
| Reusability | None | Full |
| Sample rate aware | No (hardcoded R) | Yes |
| Configurable cutoff | No | Yes |
| prepare() method | No | Yes |
| setCutoff() method | No | Yes |
| Denormal handling | No | Yes |
| Unprepared safety | No | Yes |
| Doxygen docs | Minimal | Full |
| Test coverage | Via FeedbackNetwork | Dedicated tests |
