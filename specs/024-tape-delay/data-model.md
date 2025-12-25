# Data Model: Tape Delay Mode

**Feature**: 024-tape-delay
**Date**: 2025-12-25
**Phase**: 1 (Design)

## Overview

This document defines the data structures, state management, and entity relationships for the Tape Delay Mode feature.

## Entity Definitions

### TapeHead

Represents a single playback head with its configuration.

```cpp
/// @brief Configuration for a single tape playback head
struct TapeHead {
    float ratio = 1.0f;        ///< Timing ratio relative to motor speed (e.g., 1.0, 1.5, 2.0)
    float levelDb = 0.0f;      ///< Output level in dB [-96, +6]
    float pan = 0.0f;          ///< Stereo pan position [-100, +100]
    bool enabled = true;       ///< Whether this head contributes to output

    // Computed values (updated in process)
    size_t tapIndex = 0;       ///< Index into TapManager (0, 1, 2)
};
```

**Constraints:**
- ratio: Must be > 0, typically 1.0 to 3.0
- levelDb: Clamped to [-96, +6] dB
- pan: Clamped to [-100, +100]
- tapIndex: 0-2 (maps to TapManager taps 0-2)

### MotorController

Encapsulates tape motor behavior with inertia-based delay time smoothing.

```cpp
/// @brief Manages delay time with motor inertia simulation
class MotorController {
public:
    // Configuration
    static constexpr float kMinDelayMs = 20.0f;
    static constexpr float kMaxDelayMs = 2000.0f;
    static constexpr float kDefaultInertiaMs = 300.0f;  // 200-500ms range
    static constexpr float kBaseWowRate = 0.5f;         // Hz at 500ms

private:
    // Target state (set by user)
    float targetDelayMs_ = 500.0f;

    // Smoothed state (approaches target via inertia)
    OnePoleSmoother delaySmoother_;
    float currentDelayMs_ = 500.0f;
    float previousDelayMs_ = 500.0f;

    // Configuration
    float inertiaTimeMs_ = kDefaultInertiaMs;
    double sampleRate_ = 44100.0;
};
```

**State transitions:**
```
User sets Motor Speed
        ↓
targetDelayMs_ updates immediately
        ↓
delaySmoother_ ramps currentDelayMs_ over inertiaTimeMs_
        ↓
pitchRatio = previousDelayMs_ / currentDelayMs_
        ↓
Head timings scale: headTime = currentDelayMs_ * headRatio
```

### TapeDelay

Main Layer 4 class composing all subsystems.

```cpp
/// @brief Layer 4 User Feature - Classic Tape Delay Emulation
class TapeDelay {
public:
    // Constants
    static constexpr size_t kNumHeads = 3;
    static constexpr float kHeadRatio1 = 1.0f;
    static constexpr float kHeadRatio2 = 1.5f;
    static constexpr float kHeadRatio3 = 2.0f;

private:
    // Composed Layer 3 systems
    TapManager taps_;
    FeedbackNetwork feedback_;
    CharacterProcessor character_;

    // Motor controller (internal)
    MotorController motor_;

    // Head configuration
    std::array<TapeHead, kNumHeads> heads_;

    // User parameters
    float wearAmount_ = 0.0f;       ///< 0-1: wow/flutter/hiss
    float saturationAmount_ = 0.3f; ///< 0-1: tape drive
    float ageAmount_ = 0.0f;        ///< 0-1: degradation
    float feedbackAmount_ = 0.5f;   ///< 0-1: echo repeats
    float mixAmount_ = 0.5f;        ///< 0-1: dry/wet

    // Runtime state
    double sampleRate_ = 44100.0;
    size_t maxBlockSize_ = 512;
    bool prepared_ = false;
};
```

## Parameter Mapping

### Motor Speed → Delay Time

```
User Control: Motor Speed (0-100%)
                ↓
Linear mapping: 20ms (0%) to 2000ms (100%)
                ↓
MotorController: targetDelayMs_
                ↓
Inertia smoothing: currentDelayMs_ (200-500ms transition)
                ↓
TapManager: setTapTimeMs(head, currentDelayMs_ * headRatio)
```

### Wear → Character Parameters

```
User Control: Wear (0-100%)
                ↓
CharacterProcessor mappings:
  - Wow depth: wear * 1.0 (0-100%)
  - Flutter depth: wear * 0.5 (0-50%)
  - Hiss level: -60dB + wear * 20dB = [-60, -40] dB
```

### Saturation → Tape Drive

```
User Control: Saturation (0-100%)
                ↓
CharacterProcessor: setTapeSaturation(saturation)
  - Already maps 0-1 to appropriate drive range (-17dB to +24dB)
```

### Age → Degradation

```
User Control: Age (0-100%)
                ↓
CharacterProcessor mappings:
  - Rolloff freq: 12000Hz - age * 8000Hz = [12000, 4000] Hz
  - Hiss boost: age * 10dB additional
  - (Future: splice artifact intensity)
```

### Echo Heads → Tap Configuration

```
User Control: Head 1/2/3 enable + level + pan
                ↓
TapManager:
  - setTapEnabled(0/1/2, headEnabled)
  - setTapLevelDb(0/1/2, headLevelDb)
  - setTapPan(0/1/2, headPan)
  - setTapTimeMs(0/1/2, motorSpeed * headRatio)
```

### Feedback → FeedbackNetwork

```
User Control: Feedback (0-100%+)
                ↓
FeedbackNetwork: setFeedbackAmount(feedback / 100.0)
  - 0-100% maps to 0.0-1.0 linear
  - >100% enables self-oscillation (capped at 120%)
```

## State Diagram

```
                    ┌──────────────┐
                    │  Unprepared  │
                    └──────┬───────┘
                           │ prepare()
                           ▼
                    ┌──────────────┐
          ┌─────────│   Prepared   │─────────┐
          │         └──────┬───────┘         │
          │                │                 │
          │ reset()        │ process()       │ setXxx()
          │                ▼                 │
          │         ┌──────────────┐         │
          └────────▶│  Processing  │◀────────┘
                    └──────────────┘

States:
- Unprepared: No memory allocated, cannot process
- Prepared: Memory allocated, ready for process()
- Processing: Active audio processing (same as Prepared)

Transitions:
- prepare(): Allocates buffers, configures subsystems
- reset(): Clears delay lines, snaps smoothers
- setXxx(): Updates parameters (any state)
- process(): Audio processing (only in Prepared state)
```

## Memory Layout

### Pre-allocated Buffers

All memory is allocated in `prepare()`:

| Buffer | Size | Purpose |
|--------|------|---------|
| TapManager delay line | maxDelayMs * sampleRate / 1000 | Shared delay buffer |
| FeedbackNetwork delay lines | maxDelayMs * sampleRate / 1000 * 2 | L/R feedback paths |
| CharacterProcessor work buffers | maxBlockSize * 5 | Processing scratch |

**Total memory estimate at 44.1kHz, 2000ms max:**
- TapManager: 88,200 samples = 345 KB
- FeedbackNetwork: 176,400 samples = 689 KB
- CharacterProcessor: ~10 KB
- **Total: ~1 MB**

### Parameter Storage

All parameters use stack-allocated primitives:

```cpp
// Motor/timing
float targetDelayMs_;      // 4 bytes
float currentDelayMs_;     // 4 bytes
float inertiaTimeMs_;      // 4 bytes

// Character
float wearAmount_;         // 4 bytes
float saturationAmount_;   // 4 bytes
float ageAmount_;          // 4 bytes

// Mix/feedback
float feedbackAmount_;     // 4 bytes
float mixAmount_;          // 4 bytes

// Heads (3x)
std::array<TapeHead, 3>;   // 3 * 20 bytes = 60 bytes

// Total parameters: ~92 bytes
```

## Thread Safety

**Audio thread (process):**
- Reads smoothed parameters
- No allocations
- No locks

**UI thread (setXxx):**
- Writes target parameters
- OnePoleSmoother handles thread-safe transition
- No locks needed (atomic relaxed via smoother internals)

**Parameter flow:**
```
UI Thread                    Audio Thread
    │                             │
setMotorSpeed(ms)                │
    │                             │
    ├──► targetDelayMs_ ─────────┼──► delaySmoother_.setTarget()
    │    (atomic write)          │    (read in process)
    │                             │
    │                        currentDelayMs_ = smoother_.process()
    │                             │
```

## Validation Rules

### Parameter Validation

| Parameter | Valid Range | Invalid Handling |
|-----------|-------------|------------------|
| motorSpeed | 20-2000ms | Clamp to range |
| wear | 0-100% | Clamp to [0,1] |
| saturation | 0-100% | Clamp to [0,1] |
| age | 0-100% | Clamp to [0,1] |
| feedback | 0-120% | Clamp to [0,1.2] |
| mix | 0-100% | Clamp to [0,1] |
| headLevel | -96 to +6 dB | Clamp to range |
| headPan | -100 to +100 | Clamp to range |

### State Validation

| Method | Precondition | Postcondition |
|--------|--------------|---------------|
| prepare() | Any | prepared_ == true |
| process() | prepared_ == true | Output valid |
| reset() | prepared_ == true | Delay cleared, smoothers snapped |

## Relationships

```
TapeDelay (1)
    │
    ├──► MotorController (1)
    │        └──► OnePoleSmoother (1)
    │
    ├──► TapeHead (3)
    │
    ├──► TapManager (1)
    │        └──► DelayLine (1)
    │        └──► Tap (16, but only 3 used)
    │
    ├──► FeedbackNetwork (1)
    │        └──► DelayLine (2: L/R)
    │        └──► MultimodeFilter (2: L/R)
    │        └──► SaturationProcessor (2: L/R)
    │
    └──► CharacterProcessor (1)
             └──► SaturationProcessor (1)
             └──► NoiseGenerator (1)
             └──► MultimodeFilter (1)
             └──► LFO (2: wow/flutter)
```
