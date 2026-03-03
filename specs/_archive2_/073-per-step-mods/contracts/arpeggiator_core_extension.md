# API Contract: ArpeggiatorCore Extension for Per-Step Modifiers

**Date**: 2026-02-21
**File**: `dsp/include/krate/dsp/processors/arpeggiator_core.h`

## New Enum: ArpStepFlags

```cpp
/// @brief Per-step modifier flags stored as bitmask in modifier lane.
/// Multiple flags can be combined on a single step.
/// If kStepActive is not set, the step is a rest (silence).
enum ArpStepFlags : uint8_t {
    kStepActive = 0x01,   ///< Note fires. Off = Rest.
    kStepTie    = 0x02,   ///< Sustain previous note, no retrigger
    kStepSlide  = 0x04,   ///< Legato noteOn, suppress previous noteOff, portamento
    kStepAccent = 0x08,   ///< Velocity boost by accentVelocity_ amount
};
```

## Extended Struct: ArpEvent

```cpp
struct ArpEvent {
    enum class Type : uint8_t { NoteOn, NoteOff };

    Type type{Type::NoteOn};
    uint8_t note{0};          ///< MIDI note number (0-127)
    uint8_t velocity{0};      ///< MIDI velocity (0-127)
    int32_t sampleOffset{0};  ///< Sample position within block [0, blockSize-1]
    bool legato{false};       ///< When true: suppress envelope retrigger, apply portamento
};
```

**Backward compatibility**: The `legato` field has a default initializer of `false`. All existing event construction sites that use aggregate initialization without specifying `legato` will default to `false`, preserving current behavior.

## New Public Methods on ArpeggiatorCore

### Modifier Lane Accessors

```cpp
/// @brief Access the modifier lane for reading/writing step values.
ArpLane<uint8_t>& modifierLane() noexcept { return modifierLane_; }

/// @brief Const access to the modifier lane.
const ArpLane<uint8_t>& modifierLane() const noexcept { return modifierLane_; }
```

### Configuration Setters

```cpp
/// @brief Set the accent velocity boost amount.
/// @param amount Additive velocity boost for accented steps (0-127).
void setAccentVelocity(int amount) noexcept {
    accentVelocity_ = std::clamp(amount, 0, 127);
}

/// @brief Set the slide portamento time.
/// @param ms Portamento duration in milliseconds (0-500).
void setSlideTime(float ms) noexcept {
    slideTimeMs_ = std::clamp(ms, 0.0f, 500.0f);
}
```

## Modified Methods

### resetLanes()

```cpp
void resetLanes() noexcept {
    velocityLane_.reset();
    gateLane_.reset();
    pitchLane_.reset();
    modifierLane_.reset();   // NEW
    tieActive_ = false;       // NEW: reset tie chain state
}
```

### fireStep() -- Behavioral Changes

The `fireStep()` method is substantially modified to evaluate the modifier lane. The high-level changes:

1. Advance all four lanes unconditionally (velocity, gate, pitch, modifier)
2. Evaluate modifier flags with priority: Rest > Tie > Slide > Active
3. For Rest: suppress noteOn, emit noteOff for previous notes
4. For Tie: suppress noteOff AND noteOn (sustain previous)
5. For Slide: suppress previous noteOff, emit legato noteOn (legato=true)
6. For Accent: add accentVelocity_ to velocity after lane scaling
7. In the defensive `result.count == 0` branch, advance all four lanes

### Constructor Initialization

```cpp
// In ArpeggiatorCore constructor (after pitchLane_ init):
// This call is mandatory. ArpLane<uint8_t> zero-initializes all steps (std::array
// default), so step[0] starts as 0x00 = Rest. Without this explicit setStep call,
// the default modifier lane would suppress every arp note, breaking Phase 4 compat.
modifierLane_.setStep(0, static_cast<uint8_t>(kStepActive));
```

## New Private Members

```cpp
// IMPORTANT: ArpLane<uint8_t> default-constructs all steps to 0x00 (std::array
// zero-init). 0x00 = Rest (kStepActive not set). Constructor MUST call
// modifierLane_.setStep(0, static_cast<uint8_t>(kStepActive)) to set the default
// to an active (non-rest) step, ensuring Phase 4-identical behavior by default.

ArpLane<uint8_t> modifierLane_;   ///< Bitmask per step (default: length=1, step[0]=kStepActive)
int accentVelocity_{30};          ///< Additive velocity boost for accented steps (0-127).
                                   ///< Read by fireStep() during accent evaluation.
float slideTimeMs_{60.0f};        ///< Portamento duration (0-500ms). Stored for API symmetry.
                                   ///< NOT read by fireStep(). Routing:
                                   ///< applyParamsToArp() -> engine_.setPortamentoTime(slideTime).
bool tieActive_{false};           ///< True when in a tie chain. Cleared by resetLanes().
                                   ///< Private: test via behavioral proxy (see FR-008).
```
