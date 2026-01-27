# Data Model: Crossover Filter (Linkwitz-Riley)

**Feature**: 076-crossover-filter | **Layer**: 2 (DSP Processors)

## Overview

This document defines the data structures and state management for the Linkwitz-Riley crossover filter implementation.

---

## Enumerations

### TrackingMode

Coefficient recalculation strategy for frequency smoothing.

```cpp
enum class TrackingMode : uint8_t {
    Efficient,      ///< Recalculate only when frequency changes by >=0.1Hz (default)
    HighAccuracy    ///< Recalculate every sample while smoothing is active
};
```

| Value | Behavior | Use Case |
|-------|----------|----------|
| Efficient | 0.1Hz hysteresis threshold | Default, minimal CPU overhead |
| HighAccuracy | Per-sample coefficient update | Critical modulation precision |

---

## Output Structures

### CrossoverLR4Outputs

Return type for CrossoverLR4::process().

```cpp
struct CrossoverLR4Outputs {
    float low;   ///< Lowpass output (content below crossover frequency)
    float high;  ///< Highpass output (content above crossover frequency)
};
```

**Invariants:**
- `low + high` equals input signal (flat frequency response)
- Both outputs are -6dB at crossover frequency

### Crossover3WayOutputs

Return type for Crossover3Way::process().

```cpp
struct Crossover3WayOutputs {
    float low;   ///< Low band (below lowMidFrequency)
    float mid;   ///< Mid band (lowMidFrequency to midHighFrequency)
    float high;  ///< High band (above midHighFrequency)
};
```

**Invariants:**
- `low + mid + high` equals input signal
- Each band is isolated to its frequency range

### Crossover4WayOutputs

Return type for Crossover4Way::process().

```cpp
struct Crossover4WayOutputs {
    float sub;   ///< Sub band (below subLowFrequency)
    float low;   ///< Low band (subLowFrequency to lowMidFrequency)
    float mid;   ///< Mid band (lowMidFrequency to midHighFrequency)
    float high;  ///< High band (above midHighFrequency)
};
```

**Invariants:**
- `sub + low + mid + high` equals input signal
- Each band is isolated to its frequency range

---

## Class State

### CrossoverLR4 Internal State

```cpp
class CrossoverLR4 {
private:
    // Filter stages (Layer 1 primitives)
    std::array<Biquad, 2> lpStages_;  ///< Two cascaded LP for 24dB/oct lowpass
    std::array<Biquad, 2> hpStages_;  ///< Two cascaded HP for 24dB/oct highpass

    // Parameter smoothing (Layer 1 primitive)
    OnePoleSmoother frequencySmooth_;

    // Atomic parameters (UI thread writes, audio thread reads)
    std::atomic<float> targetFrequency_{1000.0f};
    std::atomic<int> trackingMode_{static_cast<int>(TrackingMode::Efficient)};

    // Non-atomic state (audio thread only)
    float currentFrequency_{1000.0f};  ///< Last coefficient frequency
    float smoothingTimeMs_{5.0f};
    double sampleRate_{44100.0};
    bool prepared_{false};
};
```

**State Invariants:**
- `lpStages_` and `hpStages_` use same frequency and Butterworth Q (0.7071)
- `frequencySmooth_` target matches `targetFrequency_.load()`
- `currentFrequency_` tracks the last frequency used for coefficient calculation
- All filter states are reset when `prepare()` is called

### Crossover3Way Internal State

```cpp
class Crossover3Way {
private:
    // Composed crossovers
    CrossoverLR4 lowMidCrossover_;   ///< Splits input into low + highFromFirst
    CrossoverLR4 midHighCrossover_;  ///< Splits highFromFirst into mid + high

    // Atomic parameters
    std::atomic<float> lowMidFrequency_{300.0f};
    std::atomic<float> midHighFrequency_{3000.0f};

    // Non-atomic state
    float smoothingTimeMs_{5.0f};
    double sampleRate_{44100.0};
    bool prepared_{false};
};
```

**State Invariants:**
- `midHighFrequency_ >= lowMidFrequency_` (enforced by setter auto-clamping)
- Both internal crossovers use the same smoothing time

### Crossover4Way Internal State

```cpp
class Crossover4Way {
private:
    // Composed crossovers
    CrossoverLR4 subLowCrossover_;   ///< Splits input into sub + highFromFirst
    CrossoverLR4 lowMidCrossover_;   ///< Splits highFromFirst into low + highFromSecond
    CrossoverLR4 midHighCrossover_;  ///< Splits highFromSecond into mid + high

    // Atomic parameters
    std::atomic<float> subLowFrequency_{80.0f};
    std::atomic<float> lowMidFrequency_{300.0f};
    std::atomic<float> midHighFrequency_{3000.0f};

    // Non-atomic state
    float smoothingTimeMs_{5.0f};
    double sampleRate_{44100.0};
    bool prepared_{false};
};
```

**State Invariants:**
- `subLowFrequency_ <= lowMidFrequency_ <= midHighFrequency_` (enforced by setter auto-clamping)
- All internal crossovers use the same smoothing time

---

## Constants

```cpp
namespace CrossoverConstants {
    static constexpr float kMinFrequency = 20.0f;           ///< Minimum crossover frequency (Hz)
    static constexpr float kMaxFrequencyRatio = 0.45f;      ///< Max freq = sampleRate * ratio
    static constexpr float kDefaultSmoothingMs = 5.0f;      ///< Default smoothing time (ms)
    static constexpr float kHysteresisThreshold = 0.1f;     ///< TrackingMode::Efficient threshold (Hz)
    static constexpr float kButterworthQ = 0.7071067811865476f;  ///< Q for LR4 stages
}
```

---

## Signal Flow Diagrams

### CrossoverLR4 (2-Way)

```
                          +-----------+   +-----------+
                      +-->| LP Biquad |-->| LP Biquad |--> Low Output
                      |   +-----------+   +-----------+
Input ---+------------+
         |            |   +-----------+   +-----------+
         |            +-->| HP Biquad |-->| HP Biquad |--> High Output
         |                +-----------+   +-----------+
         |
         |  (All 4 biquads use same frequency, Q = 0.7071)
```

### Crossover3Way

```
                                                +----> Low Output
Input ---> CrossoverLR4 #1 (lowMid freq) ------+
           (low-mid boundary)                  |
                                               v
                                    [high from #1]
                                               |
                                               v
                                    CrossoverLR4 #2 (midHigh freq)
                                    (mid-high boundary)
                                               |
                                      +--------+--------+
                                      |                 |
                                      v                 v
                                 Mid Output        High Output
```

### Crossover4Way

```
                                                         +----> Sub Output
Input ---> CrossoverLR4 #1 (subLow freq) ---------------+
           (sub-low boundary)                           |
                                                        v
                                             [high from #1]
                                                        |
                                                        v
                                             CrossoverLR4 #2 (lowMid freq)
                                             (low-mid boundary)
                                                        |
                                               +--------+--------+
                                               |                 |
                                               v                 v
                                          Low Output    [high from #2]
                                                                 |
                                                                 v
                                                      CrossoverLR4 #3 (midHigh freq)
                                                      (mid-high boundary)
                                                                 |
                                                        +--------+--------+
                                                        |                 |
                                                        v                 v
                                                   Mid Output        High Output
```

---

## Thread Safety Model

### Parameter Ownership

| Parameter | Written By | Read By | Synchronization |
|-----------|------------|---------|-----------------|
| targetFrequency_ | UI thread | Audio thread | std::atomic (relaxed) |
| trackingMode_ | UI thread | Audio thread | std::atomic (relaxed) |
| smoothingTimeMs_ | UI thread (via setter) | Audio thread | Setter acquires, audio reads are safe |
| currentFrequency_ | Audio thread only | Audio thread only | None (single writer) |
| Filter states | Audio thread only | Audio thread only | None (single writer) |

### Relaxed Memory Ordering Rationale

Relaxed memory ordering (`std::memory_order_relaxed`) is sufficient because:
1. The OnePoleSmoother provides temporal decoupling (smoothed transitions)
2. No strict sequencing is required between parameter updates
3. Audio thread will eventually see the update within a few samples

---

## Validation Rules

### Frequency Clamping

```cpp
float clampFrequency(float hz, double sampleRate) {
    const float maxFreq = static_cast<float>(sampleRate) * kMaxFrequencyRatio;
    return std::clamp(hz, kMinFrequency, maxFreq);
}
```

### Multi-Way Frequency Ordering

**Crossover3Way:**
```cpp
void setMidHighFrequency(float hz) {
    const float clamped = clampFrequency(hz, sampleRate_);
    const float lowMid = lowMidFrequency_.load(std::memory_order_relaxed);
    // Auto-clamp: midHigh >= lowMid
    midHighFrequency_.store(std::max(clamped, lowMid), std::memory_order_relaxed);
}
```

**Crossover4Way:**
```cpp
void setLowMidFrequency(float hz) {
    const float clamped = clampFrequency(hz, sampleRate_);
    const float subLow = subLowFrequency_.load(std::memory_order_relaxed);
    const float midHigh = midHighFrequency_.load(std::memory_order_relaxed);
    // Auto-clamp: subLow <= lowMid <= midHigh
    lowMidFrequency_.store(std::clamp(clamped, subLow, midHigh), std::memory_order_relaxed);
}
```

---

## State Transitions

### Lifecycle States

```
                     +-------------+
                     | Constructed |
                     +-------------+
                           |
                           | prepare(sampleRate)
                           v
                     +-------------+
          +--------->|  Prepared   |<---------+
          |          +-------------+          |
          |                |                  |
          |                | process() /      |
          |                | processBlock()   |
          |                v                  |
          |          +-------------+          |
          |          | Processing  |          |
          |          +-------------+          |
          |                |                  |
          | reset()        | reset()          | prepare(newSampleRate)
          +----------------+-----------------+
```

### State Reset Behavior

| Method | Filter States | Smoother | Target Frequency | Coefficients |
|--------|---------------|----------|------------------|--------------|
| `prepare()` | Reset | Reset + configure | Unchanged | Recalculated |
| `reset()` | Reset | Unchanged | Unchanged | Unchanged |
| `setCrossoverFrequency()` | Unchanged | Target updated | Updated | On next process |
