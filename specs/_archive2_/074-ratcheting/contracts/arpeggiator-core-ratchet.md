# API Contract: ArpeggiatorCore Ratchet Extension

**Date**: 2026-02-22

## New Public API

### ratchetLane() Accessors

```cpp
// Non-const accessor (for configuration)
ArpLane<uint8_t>& ratchetLane() noexcept { return ratchetLane_; }

// Const accessor (for inspection)
const ArpLane<uint8_t>& ratchetLane() const noexcept { return ratchetLane_; }
```

**Pattern**: Identical to `velocityLane()`, `gateLane()`, `pitchLane()`, `modifierLane()`.

## Modified Constants

```cpp
static constexpr size_t kMaxEvents = 128;  // Was 64
```

## Modified Methods

### ArpeggiatorCore() Constructor

**Before**:
```cpp
ArpeggiatorCore() noexcept {
    velocityLane_.setStep(0, 1.0f);
    gateLane_.setStep(0, 1.0f);
    modifierLane_.setStep(0, static_cast<uint8_t>(kStepActive));
}
```

**After** (append):
```cpp
ArpeggiatorCore() noexcept {
    velocityLane_.setStep(0, 1.0f);
    gateLane_.setStep(0, 1.0f);
    modifierLane_.setStep(0, static_cast<uint8_t>(kStepActive));
    ratchetLane_.setStep(0, static_cast<uint8_t>(1));  // 074-ratcheting: default count 1
}
```

### resetLanes()

**Before**:
```cpp
void resetLanes() noexcept {
    velocityLane_.reset();
    gateLane_.reset();
    pitchLane_.reset();
    modifierLane_.reset();
    tieActive_ = false;
}
```

**After** (append):
```cpp
void resetLanes() noexcept {
    velocityLane_.reset();
    gateLane_.reset();
    pitchLane_.reset();
    modifierLane_.reset();
    tieActive_ = false;
    ratchetLane_.reset();          // 074-ratcheting
    ratchetSubStepsRemaining_ = 0; // 074-ratcheting: clear sub-step state
    ratchetSubStepCounter_ = 0;
}
```

### setEnabled()

**Before** (disable branch):
```cpp
if (enabled_ && !enabled) {
    needsDisableNoteOff_ = true;
}
```

**After** (append to disable branch):
```cpp
if (enabled_ && !enabled) {
    needsDisableNoteOff_ = true;
    ratchetSubStepsRemaining_ = 0;  // 074-ratcheting: clear pending sub-steps (FR-026)
    ratchetSubStepCounter_ = 0;
}
```

### processBlock() -- NextEvent Enum

**Before**:
```cpp
enum class NextEvent { BlockEnd, NoteOff, Step, BarBoundary };
```

**After**:
```cpp
enum class NextEvent { BlockEnd, NoteOff, Step, SubStep, BarBoundary };
```

### processBlock() -- Jump Calculation

New jump candidate after `samplesUntilBar`:
```cpp
size_t samplesUntilSubStep = SIZE_MAX;
if (ratchetSubStepsRemaining_ > 0) {
    samplesUntilSubStep = ratchetSubStepDuration_ - ratchetSubStepCounter_;
}
```

SubStep is lowest priority (below Step).

### processBlock() -- Time Advance

After `decrementPendingNoteOffs(jump)`:
```cpp
if (ratchetSubStepsRemaining_ > 0) {
    ratchetSubStepCounter_ += jump;
}
```

### processBlock() -- SubStep Handler

New handler when `next == NextEvent::SubStep`:
1. Emit pending NoteOffs due at this sample offset (MUST precede the noteOn to satisfy FR-021 ordering when a gate noteOff and the next sub-step noteOn coincide at the same sample)
2. Emit noteOn for ratcheted note(s) (single or chord)
3. Update currentArpNotes_/currentArpNoteCount_
4. Schedule pending noteOff at ratchetGateDuration_ (unless last sub-step with look-ahead suppression)
5. Decrement ratchetSubStepsRemaining_
6. Reset ratchetSubStepCounter_ = 0

Note: SubStep cannot coincide with Step in normal operation. Step has higher priority in the `NextEvent` ordering (`BarBoundary > NoteOff > Step > SubStep`), so when both would fire at the same sample, Step fires first. The Step handler initializes a new ratchet sequence (or leaves ratchetSubStepsRemaining_ = 0 for ratchet 1), which preempts any stale sub-step state from the previous step. In practice, sub-steps end exactly at step boundaries by construction (sub-step durations sum to stepDuration), so the counter reaches zero at the same sample as the next Step event -- Step fires and restarts cleanly. Additionally, after a `NextEvent::NoteOff` is processed, check whether `ratchetSubStepCounter_ >= ratchetSubStepDuration_` and `ratchetSubStepsRemaining_ > 0`; if true, fire the SubStep handler at the same sampleOffset (matching the NoteOff-coincident-Step pattern).

### processBlock() -- Transport Stop

Add to the `!ctx.isPlaying && wasPlaying_` branch:
```cpp
ratchetSubStepsRemaining_ = 0;  // 074-ratcheting (FR-027)
ratchetSubStepCounter_ = 0;
```

### fireStep()

**Ratchet lane advance** (added alongside other lane advances):
```cpp
uint8_t ratchetCount = std::max(uint8_t{1}, ratchetLane_.advance());
```

**Ratchet initialization** (after modifier evaluation, before note emission):
When `ratchetCount > 1` AND step is active (not Rest, not Tie):
- Calculate sub-step duration and gate
- Emit first sub-step noteOn (with modifier effects: slide legato, accent velocity)
- Store ratchet state for remaining sub-steps
- Set ratchetSubStepsRemaining_ = ratchetCount - 1

**Defensive branch** (result.count == 0):
Add `ratchetLane_.advance()` and `ratchetSubStepsRemaining_ = 0`.

## New Private Members

```cpp
// Lane
ArpLane<uint8_t> ratchetLane_;

// Sub-step state
uint8_t ratchetSubStepsRemaining_{0};
size_t ratchetSubStepDuration_{0};
size_t ratchetSubStepCounter_{0};
uint8_t ratchetNote_{0};
uint8_t ratchetVelocity_{0};
size_t ratchetGateDuration_{0};
bool ratchetIsLastSubStep_{false};

// Chord mode sub-step state
std::array<uint8_t, 32> ratchetNotes_{};
std::array<uint8_t, 32> ratchetVelocities_{};
size_t ratchetNoteCount_{0};
```
