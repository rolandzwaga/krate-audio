# Data Model: MultiStage Envelope Filter

**Date**: 2026-01-25 | **Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)

## Overview

This document defines the data structures, enums, and state for the MultiStageEnvelopeFilter processor.

---

## Enumerations

### EnvelopeState

Represents the current state of the envelope generator.

```cpp
/// @brief Envelope generator state machine states
enum class EnvelopeState : uint8_t {
    Idle,       ///< Not triggered, sitting at baseFrequency
    Running,    ///< Transitioning through stages
    Releasing,  ///< Decaying to baseFrequency after release()
    Complete    ///< Finished (non-looping) or waiting for retrigger
};
```

### FilterType (via SVFMode)

Reuses existing `SVFMode` from `svf.h` for filter type selection. Supported modes for this processor:

| Mode | Description | Use Case |
|------|-------------|----------|
| `SVFMode::Lowpass` | 12 dB/oct lowpass | Classic filter sweep (default) |
| `SVFMode::Bandpass` | Constant 0 dB peak | Wah-like effects |
| `SVFMode::Highpass` | 12 dB/oct highpass | Bright, opening sweeps |

---

## Structures

### EnvelopeStage

Configuration for a single envelope stage.

```cpp
/// @brief Configuration for a single envelope stage
///
/// Each stage defines a target frequency, transition time, and curve shape.
/// The envelope transitions from the previous stage's target (or baseFrequency
/// for stage 0) to this stage's target using the specified time and curve.
struct EnvelopeStage {
    float targetHz = 1000.0f;   ///< Target cutoff frequency [1, sampleRate*0.45] Hz
    float timeMs = 100.0f;      ///< Transition time [0, 10000] ms (0 = instant)
    float curve = 0.0f;         ///< Curve shape [-1 (log), 0 (linear), +1 (exp)]

    /// @brief Clamp all parameters to valid ranges
    void clamp() noexcept {
        targetHz = std::max(targetHz, 1.0f);  // Upper bound set at runtime via Nyquist
        timeMs = std::clamp(timeMs, 0.0f, 10000.0f);
        curve = std::clamp(curve, -1.0f, 1.0f);
    }
};
```

---

## Class: MultiStageEnvelopeFilter

### Constants

```cpp
static constexpr int kMaxStages = 8;           ///< Maximum configurable stages (FR-002)
static constexpr float kMinResonance = 0.1f;   ///< Minimum Q factor (from SVF)
static constexpr float kMaxResonance = 30.0f;  ///< Maximum Q factor (from SVF)
static constexpr float kMinFrequency = 1.0f;   ///< Minimum frequency Hz
static constexpr float kMaxStageTimeMs = 10000.0f;  ///< Maximum stage time (FR-005)
static constexpr float kMaxReleaseTimeMs = 10000.0f; ///< Maximum release time (FR-017a)
static constexpr float kDefaultSmoothingMs = 20.0f;  ///< Default release smoothing
```

### Member Variables

#### Configuration (set via setters, persist across reset())

```cpp
// Sample rate
double sampleRate_ = 44100.0;

// Stage configuration
std::array<EnvelopeStage, kMaxStages> stages_{};
int numStages_ = 1;

// Loop configuration
bool loopEnabled_ = false;
int loopStart_ = 0;
int loopEnd_ = 0;

// Filter configuration
SVFMode filterType_ = SVFMode::Lowpass;
float resonance_ = SVF::kButterworthQ;  // 0.7071 (no peak)
float baseFrequency_ = 100.0f;          // Starting/minimum frequency

// Modulation configuration
float velocitySensitivity_ = 0.0f;      // [0, 1]
float releaseTimeMs_ = 500.0f;          // Independent release time
```

#### Runtime State (cleared on reset(), set on trigger())

```cpp
// Envelope state
EnvelopeState state_ = EnvelopeState::Idle;
int currentStage_ = 0;
float stagePhase_ = 0.0f;               // [0, 1] position within current stage
float phaseIncrement_ = 0.0f;           // Per-sample phase increment

// Transition state
float stageFromFreq_ = 100.0f;          // Frequency at start of current transition
float stageToFreq_ = 100.0f;            // Target frequency for current transition
float stageCurve_ = 0.0f;               // Curve for current transition

// Velocity state
float currentVelocity_ = 1.0f;          // Last trigger velocity
std::array<float, kMaxStages> effectiveTargets_{}; // Velocity-scaled targets

// Output state
float currentCutoff_ = 100.0f;          // Current filter cutoff in Hz
```

#### Components (composed objects)

```cpp
// Filter (Layer 1)
SVF filter_;

// Release smoother (Layer 1)
OnePoleSmoother releaseSmoother_;

// Prepared flag
bool prepared_ = false;
```

---

## State Transitions

### Trigger Sequence

```
trigger(velocity) called:
1. Store velocity -> currentVelocity_
2. Calculate effectiveTargets_ based on velocity and velocitySensitivity_
3. Set currentStage_ = 0
4. Set stageFromFreq_ = baseFrequency_
5. Set stageToFreq_ = effectiveTargets_[0]
6. Set stageCurve_ = stages_[0].curve
7. Calculate phaseIncrement_ from stages_[0].timeMs
8. Set stagePhase_ = 0
9. Set state_ = EnvelopeState::Running
```

### Stage Advancement Sequence

```
Stage complete (stagePhase_ >= 1.0):
1. If currentStage_ == loopEnd_ AND loopEnabled_:
   - Set currentStage_ = loopStart_
   - Set stageFromFreq_ = currentCutoff_ (current position)
   - Set stageToFreq_ = effectiveTargets_[loopStart_]
   - Use loopStart's curve and time
   - Reset stagePhase_ = 0
2. Else if currentStage_ < numStages_ - 1:
   - Increment currentStage_
   - Set stageFromFreq_ = currentCutoff_
   - Set stageToFreq_ = effectiveTargets_[currentStage_]
   - Use new stage's curve and time
   - Reset stagePhase_ = 0
3. Else (last stage, no loop):
   - Set state_ = EnvelopeState::Complete
```

### Release Sequence

```
release() called:
1. If state_ is Idle or Complete: return (nothing to release)
2. Set loopEnabled_ = false (exit any loop)
3. Configure releaseSmoother_ with releaseTimeMs_
4. Snap releaseSmoother_ to currentCutoff_
5. Set releaseSmoother_ target to baseFrequency_
6. Set state_ = EnvelopeState::Releasing
```

---

## Frequency Calculation

### During Stage Transition

```cpp
// In process(), when state_ == EnvelopeState::Running:
float t = std::clamp(stagePhase_, 0.0f, 1.0f);
float curvedT = applyCurve(t, stageCurve_);
currentCutoff_ = stageFromFreq_ + (stageToFreq_ - stageFromFreq_) * curvedT;
```

### During Release

```cpp
// In process(), when state_ == EnvelopeState::Releasing:
currentCutoff_ = releaseSmoother_.process();
if (releaseSmoother_.isComplete()) {
    state_ = EnvelopeState::Complete;
    currentCutoff_ = baseFrequency_;
}
```

### Applying to Filter

```cpp
// After currentCutoff_ is calculated:
filter_.setCutoff(currentCutoff_);  // SVF clamps to Nyquist internally
float output = filter_.process(input);
```

---

## Velocity Scaling Calculation

```cpp
void calculateEffectiveTargets() noexcept {
    // Find maximum target across all active stages
    float maxTarget = baseFrequency_;
    for (int i = 0; i < numStages_; ++i) {
        maxTarget = std::max(maxTarget, stages_[i].targetHz);
    }

    // Calculate full range
    float fullRange = maxTarget - baseFrequency_;

    // Early exit if no range
    if (fullRange <= 0.0f) {
        for (int i = 0; i < numStages_; ++i) {
            effectiveTargets_[i] = stages_[i].targetHz;
        }
        return;
    }

    // Calculate depth scale factor
    // sensitivity=0 -> depthScale=1 (velocity ignored, full depth always)
    // sensitivity=1, velocity=0 -> depthScale=0 (no modulation)
    // sensitivity=1, velocity=1 -> depthScale=1 (full modulation)
    float depthScale = 1.0f - velocitySensitivity_ * (1.0f - currentVelocity_);

    // Scale each target proportionally
    for (int i = 0; i < numStages_; ++i) {
        float originalOffset = stages_[i].targetHz - baseFrequency_;
        float scaledOffset = originalOffset * depthScale;
        effectiveTargets_[i] = baseFrequency_ + scaledOffset;
    }
}
```

---

## Memory Layout

```
MultiStageEnvelopeFilter object:
+0x00  sampleRate_          (8 bytes, double)
+0x08  stages_              (8 * sizeof(EnvelopeStage) = 8 * 12 = 96 bytes)
+0x68  numStages_           (4 bytes, int)
+0x6C  loopEnabled_         (1 byte, bool + 3 padding)
+0x70  loopStart_           (4 bytes, int)
+0x74  loopEnd_             (4 bytes, int)
+0x78  filterType_          (1 byte + padding)
+0x7C  resonance_           (4 bytes, float)
+0x80  baseFrequency_       (4 bytes, float)
+0x84  velocitySensitivity_ (4 bytes, float)
+0x88  releaseTimeMs_       (4 bytes, float)
+0x8C  state_               (1 byte + padding)
+0x90  currentStage_        (4 bytes, int)
+0x94  stagePhase_          (4 bytes, float)
+0x98  phaseIncrement_      (4 bytes, float)
+0x9C  stageFromFreq_       (4 bytes, float)
+0xA0  stageToFreq_         (4 bytes, float)
+0xA4  stageCurve_          (4 bytes, float)
+0xA8  currentVelocity_     (4 bytes, float)
+0xAC  effectiveTargets_    (8 * 4 = 32 bytes)
+0xCC  currentCutoff_       (4 bytes, float)
+0xD0  filter_              (SVF, ~60 bytes)
+0x110 releaseSmoother_     (OnePoleSmoother, ~20 bytes)
+0x124 prepared_            (1 byte, bool + padding)

Total: ~300 bytes (fits in L1 cache)
```

---

## Invariants

1. `numStages_` is always in range [1, kMaxStages]
2. `loopStart_` <= `loopEnd_` < `numStages_`
3. `currentStage_` is in range [0, numStages_ - 1] when Running
4. `stagePhase_` is in range [0, 1] during processing
5. `currentCutoff_` is always positive and <= sampleRate * 0.45
6. `state_` transitions follow the state machine diagram
7. `effectiveTargets_` is recalculated on every trigger()
