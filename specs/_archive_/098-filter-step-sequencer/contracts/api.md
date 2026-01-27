# API Contract: FilterStepSequencer

**Feature**: 098-filter-step-sequencer | **Date**: 2026-01-25

## Overview

Header-only C++ API for the FilterStepSequencer component.

**Location**: `dsp/include/krate/dsp/systems/filter_step_sequencer.h`
**Namespace**: `Krate::DSP`
**Layer**: 3 (Systems)

---

## Public Types

### SequencerStep

```cpp
namespace Krate::DSP {

/// @brief Single step configuration in the filter sequence.
struct SequencerStep {
    float cutoffHz = 1000.0f;          ///< [20, 20000] Hz
    float q = 0.707f;                  ///< [0.5, 20.0]
    SVFMode type = SVFMode::Lowpass;   ///< Filter mode
    float gainDb = 0.0f;               ///< [-24, +12] dB

    /// @brief Clamp all parameters to valid ranges
    void clamp() noexcept;
};

} // namespace Krate::DSP
```

### Direction

```cpp
namespace Krate::DSP {

/// @brief Playback direction for step sequencer
enum class Direction : uint8_t {
    Forward = 0,    ///< Sequential forward
    Backward,       ///< Sequential backward
    PingPong,       ///< Bounce at endpoints (endpoints visited once per cycle)
    Random          ///< Random selection (no immediate repeat)
};

} // namespace Krate::DSP
```

---

## FilterStepSequencer Class

### Constants

```cpp
static constexpr size_t kMaxSteps = 16;
static constexpr float kMinTempoBPM = 20.0f;
static constexpr float kMaxTempoBPM = 300.0f;
static constexpr float kMinGlideMs = 0.0f;
static constexpr float kMaxGlideMs = 500.0f;
static constexpr float kMinSwing = 0.0f;
static constexpr float kMaxSwing = 1.0f;
static constexpr float kMinGateLength = 0.0f;
static constexpr float kMaxGateLength = 1.0f;
static constexpr float kGateCrossfadeMs = 5.0f;
```

### Lifecycle Methods

| Method | Signature | Description |
|--------|-----------|-------------|
| prepare | `void prepare(double sampleRate) noexcept` | Initialize for processing. Must call before process(). |
| reset | `void reset() noexcept` | Clear processing state, preserve step configuration. |
| isPrepared | `[[nodiscard]] bool isPrepared() const noexcept` | Check if ready for processing. |

### Step Configuration

| Method | Signature | Description |
|--------|-----------|-------------|
| setNumSteps | `void setNumSteps(size_t numSteps) noexcept` | Set active steps [1, 16]. |
| getNumSteps | `[[nodiscard]] size_t getNumSteps() const noexcept` | Get active step count. |
| setStep | `void setStep(size_t i, const SequencerStep& step) noexcept` | Set all step parameters. |
| getStep | `[[nodiscard]] const SequencerStep& getStep(size_t i) const noexcept` | Get step parameters. |
| setStepCutoff | `void setStepCutoff(size_t i, float hz) noexcept` | Set step cutoff [20, 20000] Hz. |
| setStepQ | `void setStepQ(size_t i, float q) noexcept` | Set step resonance [0.5, 20.0]. |
| setStepType | `void setStepType(size_t i, SVFMode type) noexcept` | Set step filter type. |
| setStepGain | `void setStepGain(size_t i, float dB) noexcept` | Set step gain [-24, +12] dB. |

### Timing Configuration

| Method | Signature | Description |
|--------|-----------|-------------|
| setTempo | `void setTempo(float bpm) noexcept` | Set tempo [20, 300] BPM. |
| setNoteValue | `void setNoteValue(NoteValue v, NoteModifier m = NoteModifier::None) noexcept` | Set step duration note value. |
| setSwing | `void setSwing(float swing) noexcept` | Set swing [0, 1]. 0.5 = 3:1 ratio. |
| setGlideTime | `void setGlideTime(float ms) noexcept` | Set glide [0, 500] ms. |
| setGateLength | `void setGateLength(float gate) noexcept` | Set gate [0, 1]. 1 = full step. |

### Playback Configuration

| Method | Signature | Description |
|--------|-----------|-------------|
| setDirection | `void setDirection(Direction dir) noexcept` | Set playback direction. |
| getDirection | `[[nodiscard]] Direction getDirection() const noexcept` | Get current direction. |

### Transport

| Method | Signature | Description |
|--------|-----------|-------------|
| sync | `void sync(double ppqPosition) noexcept` | Sync to DAW position (PPQ). |
| trigger | `void trigger() noexcept` | Manual advance to next step. |
| getCurrentStep | `[[nodiscard]] int getCurrentStep() const noexcept` | Get current step index. |

### Processing

| Method | Signature | Description |
|--------|-----------|-------------|
| process | `[[nodiscard]] float process(float input) noexcept` | Process single sample. |
| processBlock | `void processBlock(float* buffer, size_t n, const BlockContext* ctx = nullptr) noexcept` | Process buffer in-place. |

---

## Usage Examples

### Basic Usage

```cpp
#include <krate/dsp/systems/filter_step_sequencer.h>

using namespace Krate::DSP;

// Create and configure
FilterStepSequencer seq;
seq.prepare(44100.0);

// Set up 4 steps
seq.setNumSteps(4);
seq.setStepCutoff(0, 200.0f);
seq.setStepCutoff(1, 800.0f);
seq.setStepCutoff(2, 2000.0f);
seq.setStepCutoff(3, 5000.0f);

// Configure timing
seq.setTempo(120.0f);
seq.setNoteValue(NoteValue::Quarter);

// Process audio
for (size_t i = 0; i < numSamples; ++i) {
    buffer[i] = seq.process(buffer[i]);
}
```

### With BlockContext

```cpp
void processBlock(float* buffer, size_t numSamples, const BlockContext& ctx) {
    // BlockContext provides tempo from host
    seq.processBlock(buffer, numSamples, &ctx);
}
```

### With Glide and Swing

```cpp
seq.setGlideTime(50.0f);   // 50ms glide between steps
seq.setSwing(0.5f);        // 3:1 swing ratio
seq.setGateLength(0.75f);  // Filter active for 75% of step
```

### Playback Directions

```cpp
// Forward: 0, 1, 2, 3, 0, 1, 2, 3, ...
seq.setDirection(Direction::Forward);

// Backward: 3, 2, 1, 0, 3, 2, 1, 0, ...
seq.setDirection(Direction::Backward);

// PingPong: 0, 1, 2, 3, 2, 1, 0, 1, 2, 3, ...
seq.setDirection(Direction::PingPong);

// Random: unpredictable, no immediate repeats
seq.setDirection(Direction::Random);
```

### Transport Sync

```cpp
// Called when DAW transport position changes
void onTransportSeek(double ppqPosition) {
    seq.sync(ppqPosition);
}

// Manual trigger (e.g., from MIDI note)
void onTrigger() {
    seq.trigger();
}
```

---

## Error Handling

All methods are `noexcept`. Invalid parameters are clamped to valid ranges:

| Parameter | Invalid Handling |
|-----------|------------------|
| stepIndex >= kMaxSteps | Ignored silently |
| numSteps = 0 | Clamped to 1 |
| numSteps > kMaxSteps | Clamped to kMaxSteps |
| cutoffHz out of range | Clamped to [20, 20000] |
| q out of range | Clamped to [0.5, 20.0] |
| tempoBPM out of range | Clamped to [20, 300] |
| NaN input to process() | Returns 0, resets filter state |

---

## Threading

**Not thread-safe**. Create separate instances for each audio thread.

---

## Performance

- CPU target: < 0.5% single core @ 48kHz (SC-007)
- Memory: Fixed allocation (~2KB for steps + filter state)
- Zero allocations in process methods
