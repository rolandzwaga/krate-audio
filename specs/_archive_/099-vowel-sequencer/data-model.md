# Data Model: VowelSequencer with SequencerCore Refactor

**Date**: 2026-01-25 | **Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)

## Entity Overview

This feature introduces three key entities:

1. **SequencerCore** (Layer 1) - Reusable timing/direction engine
2. **VowelStep** - Per-step vowel configuration
3. **VowelSequencer** (Layer 3) - Vowel sequencing system

## SequencerCore (Layer 1 Primitive)

**Location**: `dsp/include/krate/dsp/primitives/sequencer_core.h`

### Direction Enum

```cpp
/// @brief Playback direction for step sequencers
enum class Direction : uint8_t {
    Forward = 0,    ///< Sequential: 0, 1, 2, ..., N-1, 0, 1, ...
    Backward,       ///< Reverse: N-1, N-2, ..., 0, N-1, ...
    PingPong,       ///< Bounce: 0, 1, ..., N-1, N-2, ..., 1, 0, 1, ...
    Random          ///< Random (no immediate repeat)
};
```

### SequencerCore Class

```cpp
/// @brief Reusable timing engine for step sequencers (Layer 1 Primitive)
///
/// Provides:
/// - Step timing with tempo sync (NoteValue/NoteModifier)
/// - Swing timing (even steps longer, odd steps shorter)
/// - Direction modes (Forward, Backward, PingPong, Random)
/// - PPQ transport sync
/// - Gate length control
///
/// Consumers (FilterStepSequencer, VowelSequencer) compose this class
/// and handle their own parameter interpolation based on step changes.
class SequencerCore {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr size_t kMaxSteps = 16;
    static constexpr float kMinTempoBPM = 20.0f;
    static constexpr float kMaxTempoBPM = 300.0f;
    static constexpr float kMinSwing = 0.0f;
    static constexpr float kMaxSwing = 1.0f;
    static constexpr float kMinGateLength = 0.0f;
    static constexpr float kMaxGateLength = 1.0f;
    static constexpr float kGateCrossfadeMs = 5.0f;

    // =========================================================================
    // Member Variables
    // =========================================================================

private:
    // State
    bool prepared_ = false;
    double sampleRate_ = 44100.0;

    // Step configuration
    size_t numSteps_ = 4;

    // Timing
    float tempoBPM_ = 120.0f;
    NoteValue noteValue_ = NoteValue::Eighth;
    NoteModifier noteModifier_ = NoteModifier::None;
    float swing_ = 0.0f;
    float gateLength_ = 1.0f;

    // Direction
    Direction direction_ = Direction::Forward;
    bool pingPongForward_ = true;
    uint32_t rngState_ = 12345;  // xorshift PRNG

    // Processing state
    int currentStep_ = 0;
    int previousStep_ = -1;      // Internal only - used for Random no-repeat; consumers track their own previous state if needed
    size_t sampleCounter_ = 0;
    size_t stepDurationSamples_ = 0;
    size_t gateDurationSamples_ = 0;
    bool gateActive_ = true;

    // Gate ramp (5ms crossfade)
    LinearRamp gateRamp_;
};
```

### State Transitions

```
                         +------------------+
                         |   Unprepared     |
                         +--------+---------+
                                  |
                            prepare()
                                  |
                                  v
                         +--------+---------+
      +----------------->|    Prepared      |<----------------+
      |                  +--------+---------+                 |
      |                           |                           |
      |                      tick()                           |
      |                           |                           |
      |                           v                           |
      |                  +--------+---------+                 |
      |     false <------|  Step Changed?   |------> true     |
      |                  +--------+---------+        |        |
      |                           |                  |        |
      |                           |          getCurrentStep() |
      |                           |                  |        |
      |                           |                  v        |
      |                           |         Apply step params |
      |                           |                  |        |
      |                           +<-----------------+        |
      |                           |                           |
      |                     reset()                           |
      +---------------------------+---------------------------+
```

## VowelStep Struct

**Location**: `dsp/include/krate/dsp/systems/vowel_sequencer.h`

```cpp
/// @brief Single step configuration for vowel sequencer
struct VowelStep {
    Vowel vowel = Vowel::A;           ///< Vowel sound (A, E, I, O, U)
    float formantShift = 0.0f;        ///< Formant shift in semitones [-24, +24]

    /// @brief Clamp formant shift to valid range
    void clamp() noexcept {
        formantShift = std::clamp(formantShift, -24.0f, 24.0f);
    }
};
```

### VowelStep Validation

| Field | Type | Range | Default | Clamping |
|-------|------|-------|---------|----------|
| vowel | Vowel | A, E, I, O, U | A | None (enum) |
| formantShift | float | [-24, +24] | 0.0f | clamp() |

## VowelSequencer Class (Layer 3 System)

**Location**: `dsp/include/krate/dsp/systems/vowel_sequencer.h`

```cpp
/// @brief 8-step vowel sequencer with tempo sync (Layer 3 System)
///
/// Composes SequencerCore (timing) + FormantFilter (sound) to create
/// rhythmic "talking" vowel effects. Gate uses bypass-safe design.
class VowelSequencer {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr size_t kMaxSteps = 8;
    static constexpr float kMinMorphTimeMs = 0.0f;
    static constexpr float kMaxMorphTimeMs = 500.0f;

    // =========================================================================
    // Member Variables
    // =========================================================================

private:
    // State
    bool prepared_ = false;
    double sampleRate_ = 44100.0;

    // Step configuration (FR-015a: initialized to A,E,I,O,U,O,I,E in constructor)
    std::array<VowelStep, kMaxSteps> steps_{};

    // Timing (delegated to SequencerCore)
    SequencerCore core_;

    // Morph time (FR-020)
    float morphTimeMs_ = 50.0f;

    // Processing components
    FormantFilter filter_;

    // Previous step tracking for change detection
    int lastAppliedStep_ = -1;
};
```

### VowelSequencer Composition

```
VowelSequencer
    |
    +-- SequencerCore (timing/direction)
    |       |
    |       +-- Direction enum
    |       +-- NoteValue/NoteModifier (via include)
    |
    +-- FormantFilter (vowel sound)
    |       |
    |       +-- 3x Biquad (F1, F2, F3)
    |       +-- 6x OnePoleSmoother (freq + BW smoothing)
    |
    +-- LinearRamp (morph position)
    |
    +-- LinearRamp (gate crossfade)
```

### Default Step Pattern

Per spec clarification, default initialization uses palindrome A,E,I,O,U,O,I,E:

| Step | Vowel | FormantShift |
|------|-------|--------------|
| 0 | A | 0.0 |
| 1 | E | 0.0 |
| 2 | I | 0.0 |
| 3 | O | 0.0 |
| 4 | U | 0.0 |
| 5 | O | 0.0 |
| 6 | I | 0.0 |
| 7 | E | 0.0 |

## Preset Patterns

| Preset Name | Pattern | numSteps |
|-------------|---------|----------|
| "aeiou" | A, E, I, O, U | 5 |
| "wow" | O, A, O | 3 |
| "yeah" | I, E, A | 3 |

Preset loading behavior (per spec clarification):
- numSteps updates to match preset length
- Remaining steps (beyond preset) preserve previous values

## Relationships Diagram

```
                    +------------------+
                    |    BlockContext  |
                    |  - tempoBPM      |
                    |  - isPlaying     |
                    +--------+---------+
                             |
                             | (passed to processBlock)
                             |
                             v
+----------------+   +-------+---------+   +------------------+
|  NoteValue     |   | SequencerCore   |   |    Direction     |
|  NoteModifier  |-->| (Layer 1)       |-->|  - Forward       |
+----------------+   |                 |   |  - Backward      |
                     | - timing        |   |  - PingPong      |
                     | - swing         |   |  - Random        |
                     | - gate          |   +------------------+
                     +--------+--------+
                              |
          +-------------------+-------------------+
          |                                       |
          v                                       v
+---------+---------+               +-------------+------------+
| FilterStepSequencer|              |    VowelSequencer        |
| (Layer 3)          |              |    (Layer 3)             |
|                    |              |                          |
| - SequencerCore    |              | - SequencerCore          |
| - SVF              |              | - FormantFilter          |
| - LinearRamp x5    |              | - LinearRamp x2          |
+--------------------+              | - VowelStep[8]           |
                                    +--------------------------+
                                               |
                                               v
                                    +----------+-----------+
                                    |    Vowel (enum)      |
                                    |  A=0, E=1, I=2, O=3, |
                                    |  U=4                 |
                                    +----------------------+
```

## Parameter Ranges Summary

### SequencerCore Parameters

| Parameter | Type | Range | Default | Unit |
|-----------|------|-------|---------|------|
| numSteps | size_t | [1, 16] | 4 | steps |
| tempoBPM | float | [20, 300] | 120.0 | BPM |
| noteValue | NoteValue | enum | Eighth | - |
| noteModifier | NoteModifier | enum | None | - |
| swing | float | [0.0, 1.0] | 0.0 | ratio |
| gateLength | float | [0.0, 1.0] | 1.0 | ratio |
| direction | Direction | enum | Forward | - |

### VowelSequencer Parameters

| Parameter | Type | Range | Default | Unit |
|-----------|------|-------|---------|------|
| numSteps | size_t | [1, 8] | 8 | steps |
| morphTimeMs | float | [0, 500] | 50.0 | ms |
| step[i].vowel | Vowel | enum | A,E,I,O,U,O,I,E | - |
| step[i].formantShift | float | [-24, +24] | 0.0 | semitones |

### Inherited from SequencerCore

All timing parameters (tempo, noteValue, swing, gateLength, direction) are delegated to SequencerCore.

## Real-Time Safety

### Allocation-Free Operations

The following operations are guaranteed allocation-free:

| Class | Method | Notes |
|-------|--------|-------|
| SequencerCore | tick() | Fixed-size state only |
| SequencerCore | sync() | No allocations |
| SequencerCore | trigger() | No allocations |
| VowelSequencer | process() | Uses pre-allocated components |
| VowelSequencer | processBlock() | Uses pre-allocated components |

### Pre-Allocated Resources

| Resource | Allocation Point | Size |
|----------|------------------|------|
| VowelStep[8] | Compile-time (std::array) | 8 * sizeof(VowelStep) |
| FormantFilter | Object construction | ~200 bytes |
| LinearRamp x2 | Object construction | ~40 bytes each |
| SequencerCore | Object construction | ~80 bytes |
