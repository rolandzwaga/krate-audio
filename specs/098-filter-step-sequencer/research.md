# Research: Filter Step Sequencer

**Feature**: 098-filter-step-sequencer | **Date**: 2026-01-25

## Overview

This document consolidates research findings for implementing the FilterStepSequencer. All NEEDS CLARIFICATION items from the Technical Context have been resolved.

## Research Tasks

### Task 1: Glide Implementation Strategy

**Question**: Which smoother type (OnePoleSmoother vs LinearRamp) is best for parameter glide?

**Research**:
- **OnePoleSmoother**: Exponential approach, good for natural feel but asymptotic (never exactly reaches target)
- **LinearRamp**: Constant rate, reaches target exactly, predictable timing

**Decision**: Use **LinearRamp** for all glide parameters
**Rationale**:
1. FR-010 requires "glide MUST be truncated to reach target value exactly at next step boundary"
2. LinearRamp reaches exact target values, enabling precise truncation
3. Predictable timing is essential for tempo-synced effects
4. The "mechanical" linear character fits rhythmic sequencer aesthetics

**Alternatives considered**:
- OnePoleSmoother: Rejected because asymptotic behavior makes exact boundary targeting complex
- Custom exponential with snap: More complex, LinearRamp is simpler and meets requirements

---

### Task 2: Glide Truncation Implementation

**Question**: How to implement glide truncation when step duration < glide time?

**Research**:
The spec states: "Glide is truncated; target value reached exactly at next step boundary (no drift)"

**Decision**: Calculate effective glide rate at step transition based on remaining samples
**Implementation**:
```cpp
void startGlide(float targetValue, size_t samplesToStepBoundary) noexcept {
    float currentValue = cutoffRamp_.getCurrentValue();
    float delta = targetValue - currentValue;

    // Calculate effective ramp time to reach target at boundary
    float effectiveTimeMs = static_cast<float>(samplesToStepBoundary) / sampleRate_ * 1000.0f;

    // Clamp to configured glide time if step is longer than glide
    effectiveTimeMs = std::min(effectiveTimeMs, glideTimeMs_);

    cutoffRamp_.configure(effectiveTimeMs, sampleRate_);
    cutoffRamp_.setTarget(targetValue);
}
```

**Rationale**: This ensures target is reached exactly at step boundary regardless of whether step is shorter or longer than configured glide time.

---

### Task 3: Swing/Shuffle Implementation

**Question**: How does swing affect step timing?

**Research**:
Standard swing redistributes pairs of steps (odd/even):
- At 0% swing: all steps equal duration
- At 50% swing: odd steps are 3x length of even steps (3:1 ratio)
- At 100% swing: extreme swing (approaches dotted-note feel)

**Decision**: Apply swing to alternating step pairs
**Implementation**:
```cpp
// For step pair (2k, 2k+1):
// baseStepDuration = step duration without swing
// swing = 0.0 to 1.0 (representing 0% to 100%)

// Even step (2k) gets longer:
float evenMultiplier = 1.0f + swing * 0.5f;  // 1.0 to 1.5
// Odd step (2k+1) gets shorter:
float oddMultiplier = 1.0f - swing * 0.5f;   // 1.0 to 0.5

// At swing=0.5 (50%): even=1.25, odd=0.75, ratio=1.25/0.75=1.67
// Wait, spec says 50% swing = 3:1 ratio

// Corrected formula for 50% = 3:1:
float evenMultiplier = 1.0f + swing;         // At 50% swing: 1.5
float oddMultiplier = 1.0f - swing * 0.5f;   // At 50% swing: 0.75
// But 1.5/0.75 = 2:1, not 3:1

// For 3:1 at 50%:
// even = 1.5, odd = 0.5 -> ratio = 3:1
// even + odd must sum to 2 (for pair to equal 2 base steps)
// 3x + x = 2 base -> 4x = 2 base -> x = 0.5 base, 3x = 1.5 base
// So at 50% swing: evenMultiplier = 1.5, oddMultiplier = 0.5

// General formula:
float swingAmount = swing;  // 0.0 to 1.0
float evenMultiplier = 1.0f + swingAmount;       // 1.0 to 2.0
float oddMultiplier = 1.0f - swingAmount * 0.5f; // Hmm...

// Actually: ratio = (1 + s) / (1 - s/2) at s=0.5 = 1.5/0.75 = 2:1

// For proper 3:1 at 50%:
// Let swing parameter represent ratio directly
// At 50%: want ratio 3:1 -> even=1.5, odd=0.5 (sum=2)
// evenDuration = baseDuration * (1 + swing)  ; at 0.5 swing: 1.5
// oddDuration = baseDuration * (1 - swing)   ; at 0.5 swing: 0.5
// BUT sum = 2 * baseDuration * (1 + swing/2 - swing/2) = 2 base  // good!
// And ratio = (1+swing)/(1-swing) at swing=0.5 = 1.5/0.5 = 3:1  // correct!
```

**Final Formula**:
```cpp
float evenMultiplier = 1.0f + swing;  // swing [0,1] -> multiplier [1,2]
float oddMultiplier = 1.0f - swing;   // swing [0,1] -> multiplier [1,0]
// Pair total always = 2 base steps
// Ratio at swing=0.5: (1+0.5)/(1-0.5) = 1.5/0.5 = 3:1 (matches SC-004)
```

**Rationale**: This formula ensures the pair total remains constant (preserving overall pattern length) while achieving the standard 3:1 ratio at 50% swing.

### Task 3a: Swing + Glide Interaction (Worked Example)

**Question**: How does swing affect glide truncation behavior?

**Worked Example**:
```
Configuration:
- Tempo: 120 BPM
- Note value: 1/4 note (base step = 500ms)
- Swing: 50%
- Glide time: 100ms

Step durations with swing:
- Even steps (0, 2, 4...): 500ms * (1 + 0.5) = 750ms
- Odd steps (1, 3, 5...):  500ms * (1 - 0.5) = 250ms
- Pair total: 750 + 250 = 1000ms = 2 beats ✓

Glide behavior per step:
- Step 0 (even, 750ms): glide completes in 100ms (< 750ms), normal glide
- Step 1 (odd, 250ms):  glide completes in 100ms (< 250ms), normal glide
- Step 2 (even, 750ms): glide completes in 100ms (< 750ms), normal glide

With faster tempo (200 BPM) and 1/16 notes:
- Base step = 75ms
- Even steps: 75ms * 1.5 = 112.5ms
- Odd steps:  75ms * 0.5 = 37.5ms
- Glide 100ms on even step: normal glide (112.5ms > 100ms)
- Glide 100ms on odd step: TRUNCATED to 37.5ms (reaches target at boundary)
```

**Processing Order**:
1. Calculate base step duration from tempo + note value
2. Apply swing multiplier to get actual step duration
3. Check if glide time < step duration
4. If step duration < glide time: truncate glide to reach target at boundary
5. Start glide with effective duration

**Key Insight**: Swing is applied BEFORE glide truncation check. This means swung short steps may truncate glide even when swung long steps do not, creating asymmetric parameter transitions that follow the groove.

---

### Task 4: PingPong Direction State Machine

**Question**: How to implement PingPong with endpoints visited once?

**Research**:
Pattern for 4 steps: `0, 1, 2, 3, 2, 1, 0, 1, 2, 3, 2, 1, 0...`
- Endpoints (0 and 3) visited once per cycle
- Middle steps (1 and 2) visited twice per cycle
- Cycle length = 2 * (N - 1) = 6 for N=4

**Decision**: Track direction and flip at endpoints
**Implementation**:
```cpp
enum class PingPongDirection : uint8_t { Forward, Backward };

struct PingPongState {
    int step = 0;
    PingPongDirection direction = PingPongDirection::Forward;
};

int advancePingPong(PingPongState& state, int numSteps) noexcept {
    int currentStep = state.step;

    if (state.direction == PingPongDirection::Forward) {
        state.step++;
        if (state.step >= numSteps - 1) {
            // Reached end, reverse
            state.direction = PingPongDirection::Backward;
        }
    } else {
        state.step--;
        if (state.step <= 0) {
            // Reached start, reverse
            state.direction = PingPongDirection::Forward;
        }
    }

    return currentStep;  // Return the step we were at before advancing
}
```

**Test validation**: For numSteps=4, sequence should be: 0,1,2,3,2,1,0,1,2,3,2,1...

---

### Task 5: Random Mode Without Repetition

**Question**: How to implement random step selection without immediate repetition?

**Research**:
Requirements:
- Next step must differ from current step (FR-012b)
- All N steps visited within 10*N iterations (SC-006)

**Decision**: Simple rejection sampling with previous step exclusion
**Implementation**:
```cpp
int nextRandomStep(int currentStep, int numSteps, uint32_t& rngState) noexcept {
    if (numSteps <= 1) return 0;

    int nextStep;
    do {
        // Simple xorshift PRNG
        rngState ^= rngState << 13;
        rngState ^= rngState >> 17;
        rngState ^= rngState << 5;
        nextStep = static_cast<int>(rngState % static_cast<uint32_t>(numSteps));
    } while (nextStep == currentStep);

    return nextStep;
}
```

**Rationale**:
- Rejection sampling is simple and has expected 1-2 iterations per call
- With numSteps >= 2, each non-current step has equal probability
- Statistical fairness (SC-006) is naturally achieved by uniform distribution

**Alternatives considered**:
- Shuffle bag: More complex, overkill for simple no-repeat requirement
- Weighted selection: Unnecessary complexity for uniform distribution

---

### Task 6: Gate Crossfade Implementation

**Question**: How to implement 5ms crossfade for gate transitions?

**Research**:
Requirements:
- Fixed 5ms crossfade duration (FR-011a)
- Prevents clicks on gate on/off transitions (SC-009)
- Must work at any sample rate

**Decision**: Linear crossfade with pre-calculated sample count
**Implementation**:
```cpp
static constexpr float kGateCrossfadeMs = 5.0f;

class GateCrossfader {
    float fadeGain_ = 1.0f;       // 1.0 = full wet, 0.0 = full dry
    float fadeIncrement_ = 0.0f;  // Per-sample change
    bool fadingIn_ = false;
    bool fadingOut_ = false;

public:
    void prepare(float sampleRate) noexcept {
        float crossfadeSamples = kGateCrossfadeMs * 0.001f * sampleRate;
        fadeIncrement_ = 1.0f / crossfadeSamples;
    }

    void startFadeOut() noexcept {
        fadingOut_ = true;
        fadingIn_ = false;
    }

    void startFadeIn() noexcept {
        fadingIn_ = true;
        fadingOut_ = false;
    }

    float process(float wet, float dry) noexcept {
        if (fadingOut_) {
            fadeGain_ -= fadeIncrement_;
            if (fadeGain_ <= 0.0f) {
                fadeGain_ = 0.0f;
                fadingOut_ = false;
            }
        } else if (fadingIn_) {
            fadeGain_ += fadeIncrement_;
            if (fadeGain_ >= 1.0f) {
                fadeGain_ = 1.0f;
                fadingIn_ = false;
            }
        }

        return wet * fadeGain_ + dry * (1.0f - fadeGain_);
    }
};
```

**Rationale**: Linear crossfade is simple, predictable, and sufficient for 5ms duration (not audibly different from equal-power at such short duration).

---

### Task 7: PPQ Sync Implementation

**Question**: How to implement sync() for DAW transport lock?

**Research**:
PPQ (Pulses Per Quarter note) position indicates where in the timeline we are.
- 0.0 = start of song
- 1.0 = one beat (quarter note) in
- 2.0 = two beats in

**Decision**: Calculate current step and phase from PPQ position
**Implementation**:
```cpp
void sync(double ppqPosition) noexcept {
    // Calculate beats per step based on note value
    float beatsPerStep = getBeatsForNote(noteValue_, noteModifier_);

    // Calculate which step we should be at
    double stepsIntoPattern = ppqPosition / static_cast<double>(beatsPerStep);

    // Handle direction
    int effectiveStep;
    switch (direction_) {
        case Direction::Forward:
            effectiveStep = static_cast<int>(stepsIntoPattern) % numSteps_;
            break;
        case Direction::Backward:
            effectiveStep = (numSteps_ - 1) - (static_cast<int>(stepsIntoPattern) % numSteps_);
            break;
        case Direction::PingPong:
            // More complex - need to calculate position in ping-pong cycle
            effectiveStep = calculatePingPongStep(stepsIntoPattern);
            break;
        case Direction::Random:
            // Can't sync random - just use current step
            effectiveStep = currentStep_;
            break;
    }

    // Calculate phase within current step
    double fractionalStep = std::fmod(stepsIntoPattern, 1.0);
    sampleCounter_ = static_cast<size_t>(fractionalStep * stepDurationSamples_);

    // Update state
    currentStep_ = effectiveStep;
    updateFilterForStep(currentStep_);
}
```

**Rationale**: Direct calculation from PPQ ensures sample-accurate sync (SC-008).

---

### Task 8: Filter Type Change Without Clicks

**Question**: How does filter type change work with glide?

**Research**:
Per FR-010a (updated): "filter type changes use 5ms crossfade between old and new filter outputs"

**Original approach** (instant switch):
- SVF preserves internal state across mode changes
- However, different filter types produce very different outputs for the same state
- LP→HP transition causes audible transients even with state preservation

**Improved approach** (dual-SVF crossfade):
- Run two SVF filters in parallel during type transitions
- Crossfade output from old filter to new filter over 5ms
- Eliminates transients completely

**Decision**: Crossfade between old and new filter outputs over 5ms
**Implementation**:
```cpp
void applyStepParameters(int stepIndex) noexcept {
    const auto& step = steps_[stepIndex];

    SVFMode currentType = filter_.getMode();
    if (step.type != currentType && prepared_) {
        // Copy current filter (state + mode) to old filter
        filterOld_ = filter_;

        // Set new type on primary filter
        filter_.setMode(step.type);

        // Start 5ms crossfade from old (0) to new (1)
        typeCrossfadeRamp_.snapTo(0.0f);
        typeCrossfadeRamp_.setTarget(1.0f);
        isTypeCrossfading_ = true;
    }

    // Continuous parameters GLIDE
    cutoffRamp_.setTarget(step.cutoffHz);
    qRamp_.setTarget(step.q);
    gainRamp_.setTarget(step.gainDb);
}

float process(float input) noexcept {
    // ... parameter processing ...

    if (isTypeCrossfading_) {
        float wetNew = filter_.process(input);
        float wetOld = filterOld_.process(input);
        float crossfade = typeCrossfadeRamp_.process();
        wet = wetNew * crossfade + wetOld * (1.0f - crossfade);

        if (crossfade >= 1.0f) isTypeCrossfading_ = false;
    } else {
        wet = filter_.process(input);
    }
}
```

**Rationale**: The dual-SVF crossfade completely eliminates type-change transients at the cost of briefly running two filters (only during 5ms transitions). Test verifies maxDiff < 0.5 for type changes.

---

### Task 9: Performance Considerations

**Question**: How to meet CPU budget (SC-007: < 0.5% @ 48kHz)?

**Research**:
At 48kHz, 1 second = 48000 samples.
0.5% CPU = can process 200x real-time (0.5% of time for 100% of samples).

Per-sample operations:
- 3 LinearRamp::process() calls (~10 ops each)
- 1 SVF::process() call (~20 ops)
- 1 gate crossfade (~5 ops)
- Step advancement check (~5 ops)
- Total: ~50 ops per sample

At 48kHz: 48000 * 50 = 2.4M ops/second
Modern CPU: ~3GHz = 3B ops/second
Ratio: 0.08% - well within budget

**Decision**: No special optimization needed; straightforward implementation meets requirements
**Rationale**: The composed components (SVF, LinearRamp) are already optimized. The per-sample overhead is minimal.

**Optimizations available if needed**:
- Process filter in blocks between step transitions (avoid per-sample branching)
- SIMD for ramp updates (unlikely to help for 3 parameters)
- Pre-compute step durations at tempo change instead of per-block

---

## Summary of Decisions

| Topic | Decision | Rationale |
|-------|----------|-----------|
| Glide type | LinearRamp | Exact target reaching, predictable timing |
| Glide truncation | Rate adjustment | Ensures exact boundary targeting |
| Swing formula | (1+swing)/(1-swing) | Achieves 3:1 at 50%, preserves pattern length |
| PingPong | Direction flip at endpoints | Endpoints visited once, correct cycle length |
| Random | Rejection sampling | Simple, fair, no immediate repeats |
| Gate crossfade | Linear 5ms | Simple, effective, no audible difference from equal-power |
| PPQ sync | Direct calculation | Sample-accurate positioning |
| Filter type change | Dual-SVF 5ms crossfade | Eliminates type-change transients completely |
| Performance | Straightforward implementation | Well within CPU budget |

## Unresolved Questions

None. All technical questions have been resolved.
