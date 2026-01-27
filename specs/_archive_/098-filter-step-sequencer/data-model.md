# Data Model: Filter Step Sequencer

**Feature**: 098-filter-step-sequencer | **Date**: 2026-01-25

## Entity Definitions

### SequencerStep

Represents a single step in the sequence with all filter parameters.

```cpp
/// @brief Single step in the filter sequence.
/// All parameters have sensible defaults for immediate use.
struct SequencerStep {
    float cutoffHz = 1000.0f;              ///< Cutoff frequency [20, 20000] Hz
    float q = 0.707f;                      ///< Resonance/Q [0.5, 20.0] (Butterworth default)
    SVFMode type = SVFMode::Lowpass;       ///< Filter type
    float gainDb = 0.0f;                   ///< Gain [-24, +12] dB (only for Peak/Shelf modes)

    /// @brief Apply clamping to all parameters
    void clamp() noexcept {
        cutoffHz = std::clamp(cutoffHz, 20.0f, 20000.0f);
        q = std::clamp(q, 0.5f, 20.0f);
        gainDb = std::clamp(gainDb, -24.0f, 12.0f);
    }
};
```

**Validation Rules**:
- `cutoffHz`: Clamped to [20, 20000] Hz (FR-003)
- `q`: Clamped to [0.5, 20.0] (FR-004)
- `gainDb`: Clamped to [-24, +12] dB (FR-006)
- `type`: Must be valid SVFMode enum value (FR-005)

---

### Direction

Playback direction enumeration.

```cpp
/// @brief Sequencer playback direction modes
enum class Direction : uint8_t {
    Forward = 0,    ///< 0, 1, 2, ..., N-1, 0, 1, ...
    Backward,       ///< N-1, N-2, ..., 0, N-1, ...
    PingPong,       ///< 0, 1, ..., N-1, N-2, ..., 1, 0, 1, ... (endpoints once)
    Random          ///< Random, no immediate repetition
};
```

**State Transitions**:

| From | Event | To |
|------|-------|-----|
| Forward | Direction change | Any |
| Backward | Direction change | Any |
| PingPong | Direction change | Any |
| Random | Direction change | Any |

---

### FilterStepSequencer

Main class orchestrating the step sequencer.

```cpp
/// @brief 16-step filter parameter sequencer synchronized to tempo.
///
/// Composes SVF filter with LinearRamp smoothers to create rhythmic
/// filter sweeps. Supports multiple playback directions, swing timing,
/// glide, and gate length control.
///
/// @par Layer
/// Layer 3 (System) - composes Layer 1 primitives (SVF, LinearRamp)
///
/// @par Thread Safety
/// Not thread-safe. Use separate instances for each audio thread.
///
/// @par Real-Time Safety
/// All processing methods are noexcept with zero allocations.
class FilterStepSequencer {
public:
    // =========================================================================
    // Constants
    // =========================================================================

    static constexpr size_t kMaxSteps = 16;           ///< Maximum programmable steps
    static constexpr float kMinTempoBPM = 20.0f;      ///< Minimum tempo
    static constexpr float kMaxTempoBPM = 300.0f;     ///< Maximum tempo
    static constexpr float kMinGlideMs = 0.0f;        ///< Minimum glide time
    static constexpr float kMaxGlideMs = 500.0f;      ///< Maximum glide time
    static constexpr float kMinSwing = 0.0f;          ///< Minimum swing (0%)
    static constexpr float kMaxSwing = 1.0f;          ///< Maximum swing (100%)
    static constexpr float kMinGateLength = 0.0f;     ///< Minimum gate (0%)
    static constexpr float kMaxGateLength = 1.0f;     ///< Maximum gate (100%)
    static constexpr float kGateCrossfadeMs = 5.0f;   ///< Fixed crossfade duration

    // =========================================================================
    // Lifecycle
    // =========================================================================

    FilterStepSequencer() noexcept = default;

    /// @brief Prepare for processing
    /// @param sampleRate Sample rate in Hz (minimum 1000)
    void prepare(double sampleRate) noexcept;

    /// @brief Reset all state (preserves step configuration)
    void reset() noexcept;

    /// @brief Check if prepared
    [[nodiscard]] bool isPrepared() const noexcept;

    // =========================================================================
    // Step Configuration (FR-001 to FR-006)
    // =========================================================================

    /// @brief Set number of active steps [1, 16]
    void setNumSteps(size_t numSteps) noexcept;

    /// @brief Get number of active steps
    [[nodiscard]] size_t getNumSteps() const noexcept;

    /// @brief Set all parameters for a step
    void setStep(size_t stepIndex, const SequencerStep& step) noexcept;

    /// @brief Get step parameters
    [[nodiscard]] const SequencerStep& getStep(size_t stepIndex) const noexcept;

    /// @brief Set step cutoff frequency
    void setStepCutoff(size_t stepIndex, float hz) noexcept;

    /// @brief Set step resonance/Q
    void setStepQ(size_t stepIndex, float q) noexcept;

    /// @brief Set step filter type
    void setStepType(size_t stepIndex, SVFMode type) noexcept;

    /// @brief Set step gain (for Peak/Shelf modes)
    void setStepGain(size_t stepIndex, float dB) noexcept;

    // =========================================================================
    // Timing Configuration (FR-007 to FR-011)
    // =========================================================================

    /// @brief Set tempo in BPM [20, 300]
    void setTempo(float bpm) noexcept;

    /// @brief Set note value for step duration
    void setNoteValue(NoteValue value, NoteModifier modifier = NoteModifier::None) noexcept;

    /// @brief Set swing amount [0, 1] where 0.5 = 3:1 ratio
    void setSwing(float swing) noexcept;

    /// @brief Set glide time in ms [0, 500]
    void setGlideTime(float ms) noexcept;

    /// @brief Set gate length [0, 1] where 1 = full step active
    void setGateLength(float gateLength) noexcept;

    // =========================================================================
    // Playback Configuration (FR-012)
    // =========================================================================

    /// @brief Set playback direction
    void setDirection(Direction direction) noexcept;

    /// @brief Get current direction
    [[nodiscard]] Direction getDirection() const noexcept;

    // =========================================================================
    // Transport (FR-013, FR-014)
    // =========================================================================

    /// @brief Sync to PPQ position (DAW transport lock)
    void sync(double ppqPosition) noexcept;

    /// @brief Manual step trigger (advances to next step immediately)
    void trigger() noexcept;

    /// @brief Get current step index
    [[nodiscard]] int getCurrentStep() const noexcept;

    // =========================================================================
    // Processing (FR-015 to FR-019)
    // =========================================================================

    /// @brief Process single sample
    /// @param input Input sample
    /// @return Filtered output sample
    [[nodiscard]] float process(float input) noexcept;

    /// @brief Process block of samples with context
    /// @param buffer Audio buffer (modified in-place)
    /// @param numSamples Number of samples
    /// @param ctx Block context with tempo info (optional)
    void processBlock(float* buffer, size_t numSamples,
                      const BlockContext* ctx = nullptr) noexcept;

private:
    // State
    bool prepared_ = false;
    double sampleRate_ = 44100.0;

    // Step configuration
    std::array<SequencerStep, kMaxSteps> steps_{};
    size_t numSteps_ = 4;

    // Timing
    float tempoBPM_ = 120.0f;
    NoteValue noteValue_ = NoteValue::Eighth;
    NoteModifier noteModifier_ = NoteModifier::None;
    float swing_ = 0.0f;
    float glideTimeMs_ = 0.0f;
    float gateLength_ = 1.0f;

    // Direction
    Direction direction_ = Direction::Forward;
    bool pingPongForward_ = true;  // For PingPong mode
    uint32_t rngState_ = 12345;    // For Random mode

    // Processing state
    int currentStep_ = 0;
    int previousStep_ = -1;  // For Random no-repeat
    size_t sampleCounter_ = 0;
    size_t stepDurationSamples_ = 0;
    size_t gateDurationSamples_ = 0;
    bool gateActive_ = true;

    // Components (Layer 1)
    SVF filter_;
    LinearRamp cutoffRamp_;
    LinearRamp qRamp_;
    LinearRamp gainRamp_;
    LinearRamp gateRamp_;  // For 5ms crossfade

    // Internal methods
    void updateStepDuration() noexcept;
    void advanceStep() noexcept;
    int calculateNextStep() noexcept;
    void applyStepParameters(int stepIndex) noexcept;
    float applySwingToStep(int stepIndex, float baseDuration) const noexcept;
};
```

---

## State Relationships

```
FilterStepSequencer
├── steps_[16] : SequencerStep
│   ├── cutoffHz
│   ├── q
│   ├── type
│   └── gainDb
├── filter_ : SVF
│   └── (internal state: ic1eq_, ic2eq_, coefficients)
├── cutoffRamp_ : LinearRamp
│   └── (smooths cutoff changes)
├── qRamp_ : LinearRamp
│   └── (smooths Q changes)
├── gainRamp_ : LinearRamp
│   └── (smooths gain changes)
└── gateRamp_ : LinearRamp
    └── (handles gate crossfade)
```

---

## Parameter Ranges Summary

| Parameter | Min | Max | Default | Unit | Requirement |
|-----------|-----|-----|---------|------|-------------|
| numSteps | 1 | 16 | 4 | steps | FR-001, FR-002 |
| cutoffHz | 20 | 20000 | 1000 | Hz | FR-003 |
| q | 0.5 | 20.0 | 0.707 | unitless | FR-004 |
| type | - | - | Lowpass | SVFMode | FR-005 |
| gainDb | -24 | +12 | 0 | dB | FR-006 |
| tempoBPM | 20 | 300 | 120 | BPM | FR-007 |
| noteValue | SixtyFourth | Whole | Eighth | NoteValue | FR-008 |
| swing | 0 | 1 | 0 | ratio | FR-009 |
| glideTimeMs | 0 | 500 | 0 | ms | FR-010 |
| gateLength | 0 | 1 | 1 | ratio | FR-011 |

---

## Timing Calculations

### Step Duration

```cpp
// Base step duration in milliseconds
float msPerBeat = 60000.0f / tempoBPM;
float beatsPerStep = getBeatsForNote(noteValue_, noteModifier_);
float baseStepMs = msPerBeat * beatsPerStep;
float baseStepSamples = baseStepMs * 0.001f * sampleRate;
```

### Swing Application

```cpp
// For step pair (2k, 2k+1):
bool isOddStep = (stepIndex % 2 == 1);
float swingMultiplier = isOddStep ? (1.0f - swing_) : (1.0f + swing_);
float swungDurationSamples = baseDurationSamples * swingMultiplier;
```

### Gate Duration

```cpp
gateDurationSamples = stepDurationSamples * gateLength_;
```

---

## Processing Flow

```
┌─────────────────────────────────────────────────────────────────────────┐
│                    Per-Sample Processing Flow                           │
├─────────────────────────────────────────────────────────────────────────┤
│                                                                         │
│  1. Check for step boundary:                                            │
│     if (sampleCounter_ >= stepDurationSamples_) {                       │
│         advanceStep();                                                  │
│         sampleCounter_ = 0;                                             │
│     }                                                                   │
│                                                                         │
│  2. Update gate state:                                                  │
│     gateActive_ = (sampleCounter_ < gateDurationSamples_);              │
│                                                                         │
│  3. Process parameter ramps:                                            │
│     float cutoff = cutoffRamp_.process();                               │
│     float q = qRamp_.process();                                         │
│     float gain = gainRamp_.process();                                   │
│                                                                         │
│  4. Apply to filter:                                                    │
│     filter_.setCutoff(cutoff);                                          │
│     filter_.setResonance(q);                                            │
│     filter_.setGain(gain);                                              │
│                                                                         │
│  5. Process filter:                                                     │
│     float wet = filter_.process(input);                                 │
│                                                                         │
│  6. Apply gate crossfade:                                               │
│     float gateGain = gateRamp_.process();                               │
│     float output = wet * gateGain + input * (1.0f - gateGain);          │
│                                                                         │
│  7. Increment counter:                                                  │
│     sampleCounter_++;                                                   │
│                                                                         │
│  8. Return output                                                       │
│                                                                         │
└─────────────────────────────────────────────────────────────────────────┘
```

---

## Direction State Machines

### Forward

```
State: currentStep
Advance: currentStep = (currentStep + 1) % numSteps
```

### Backward

```
State: currentStep
Advance: currentStep = (currentStep - 1 + numSteps) % numSteps
```

### PingPong

```
State: currentStep, pingPongForward
Advance:
  if pingPongForward:
    currentStep++
    if currentStep >= numSteps - 1:
      pingPongForward = false
  else:
    currentStep--
    if currentStep <= 0:
      pingPongForward = true
```

### Random

```
State: currentStep, previousStep, rngState
Advance:
  do:
    rngState = xorshift(rngState)
    candidate = rngState % numSteps
  while candidate == previousStep
  previousStep = currentStep
  currentStep = candidate
```
