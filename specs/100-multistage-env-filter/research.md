# Research: MultiStage Envelope Filter

**Date**: 2026-01-25 | **Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)

## Research Questions Resolved

### 1. Curve Interpolation Algorithms

**Question**: What algorithm produces perceptually correct logarithmic/linear/exponential curves for filter frequency transitions?

**Decision**: Power-based curve shaping with symmetric mapping around linear (curve=0)

**Rationale**:
- Power functions are computationally efficient (single `std::pow` call)
- Symmetric mapping (-1 to +1) provides intuitive control
- Perceptually correct for frequency changes when applied to normalized phase

**Alternatives Considered**:

| Approach | Pros | Cons | Why Rejected |
|----------|------|------|--------------|
| `std::pow(t, exponent)` | Simple, efficient | Only handles positive curves | Need both log and exp |
| Bezier curves | Smooth, customizable | More complex, needs control points | Overkill for simple curve shapes |
| Polynomial approximation | Fast | Less intuitive parameters | Harder to tune |
| Table lookup | Very fast | Memory overhead, interpolation needed | Not worth complexity for 1 call/sample |

**Final Algorithm**:

```cpp
/// @brief Apply curve shaping to linear phase
/// @param t Normalized phase [0, 1]
/// @param curve Curve shape [-1 (log), 0 (linear), +1 (exp)]
/// @return Curved phase [0, 1]
[[nodiscard]] inline float applyCurve(float t, float curve) noexcept {
    // Clamp t to valid range
    t = std::clamp(t, 0.0f, 1.0f);

    // Linear case (optimization)
    if (std::abs(curve) < 0.001f) {
        return t;
    }

    // k controls the steepness of the curve
    // k=3 provides a good range: at curve=+/-1, exponent is 4.0
    constexpr float k = 3.0f;

    if (curve >= 0.0f) {
        // Exponential: slow start, fast finish
        // pow(t, 1 + curve * k)
        const float exponent = 1.0f + curve * k;
        return std::pow(t, exponent);
    } else {
        // Logarithmic: fast start, slow finish
        // 1 - pow(1-t, 1 + |curve| * k)
        const float exponent = 1.0f + (-curve) * k;
        return 1.0f - std::pow(1.0f - t, exponent);
    }
}
```

**Curve Behavior Visualization**:

```
Cutoff
  ^
  |         curve = -1.0 (log)
  |     _______________
  |   /
  |  /
  | /
  |/
  +-------------------------> Time

  |         curve = 0.0 (linear)
  |              /
  |            /
  |          /
  |        /
  |      /
  +-------------------------> Time

  |         curve = +1.0 (exp)
  |                    /
  |                   /
  |                 /
  |              /
  |___________/
  +-------------------------> Time
```

---

### 2. State Machine Design

**Question**: How should envelope states transition, especially with looping and release?

**Decision**: Four-state machine with explicit transitions

**States**:

| State | Description | Transitions Out |
|-------|-------------|-----------------|
| Idle | Not triggered, at baseFrequency | -> Running (on trigger) |
| Running | Progressing through stages | -> Releasing (on release), -> Complete (last stage, no loop) |
| Releasing | Decaying to baseFrequency | -> Complete (decay finished) |
| Complete | Envelope finished | -> Running (on retrigger) |

**State Transition Logic**:

```cpp
void processStateMachine() noexcept {
    switch (state_) {
        case EnvelopeState::Idle:
            // Waiting for trigger - no processing
            break;

        case EnvelopeState::Running:
            // Advance phase within current stage
            stagePhase_ += phaseIncrement_;

            if (stagePhase_ >= 1.0f) {
                // Stage complete - advance to next
                if (currentStage_ == loopEnd_ && loopEnabled_) {
                    // Loop back
                    currentStage_ = loopStart_;
                    startStageTransition(loopStart_);
                } else if (currentStage_ >= numStages_ - 1) {
                    // Last stage, no loop
                    state_ = EnvelopeState::Complete;
                } else {
                    // Next stage
                    currentStage_++;
                    startStageTransition(currentStage_);
                }
            }
            break;

        case EnvelopeState::Releasing:
            // Use OnePoleSmoother to decay to baseFrequency
            if (releaseSmoother_.isComplete()) {
                state_ = EnvelopeState::Complete;
            }
            break;

        case EnvelopeState::Complete:
            // Waiting for retrigger - no processing
            break;
    }
}
```

---

### 3. Velocity Scaling Mathematics

**Question**: How should velocity scale the modulation depth?

**Decision**: Scale the total modulation range from baseFrequency

**Formula**:

```cpp
// Given:
// - baseFrequency_: Starting frequency (e.g., 100 Hz)
// - stageTargets: Array of target frequencies (e.g., [200, 2000, 500, 800] Hz)
// - velocity: Trigger velocity [0, 1]
// - velocitySensitivity_: How much velocity affects depth [0, 1]

// Calculate the maximum target across all stages
float maxTarget = *std::max_element(stageTargets, stageTargets + numStages_);
float fullRange = maxTarget - baseFrequency_;  // e.g., 2000 - 100 = 1900 Hz

// Calculate effective range based on velocity
// When sensitivity=1 and velocity=0.5: effectiveRange = 0.5 * fullRange
// When sensitivity=0: effectiveRange = fullRange (velocity ignored)
float depthScale = 1.0f - velocitySensitivity_ * (1.0f - velocity);
float effectiveRange = fullRange * depthScale;

// Scale each stage target proportionally
// The ratio of (target - base) / fullRange is preserved
for (int i = 0; i < numStages_; ++i) {
    float originalOffset = stages_[i].targetHz - baseFrequency_;
    float scaledOffset = originalOffset * (effectiveRange / fullRange);
    effectiveTargets_[i] = baseFrequency_ + scaledOffset;
}
```

**Example**:

```
baseFrequency = 100 Hz
stages = [200, 2000, 500, 800] Hz
maxTarget = 2000 Hz
fullRange = 1900 Hz

With velocity = 0.5, sensitivity = 1.0:
  velocityFactor = 0.5
  effectiveRange = 950 Hz

  Stage 0: 100 + (200-100) * 0.5 = 150 Hz
  Stage 1: 100 + (2000-100) * 0.5 = 1050 Hz
  Stage 2: 100 + (500-100) * 0.5 = 300 Hz
  Stage 3: 100 + (800-100) * 0.5 = 450 Hz

With velocity = 1.0, sensitivity = 1.0:
  velocityFactor = 1.0
  All targets unchanged: [200, 2000, 500, 800] Hz
```

---

### 4. Loop Transition Smoothness

**Question**: How to ensure seamless loop transitions without clicks?

**Decision**: Use loopStart stage's curve and time for the wrap-around transition

**Rationale**:
- Consistent with how stage 0 uses baseFrequency as "from" value
- The loop transition is essentially starting a new stage
- No special-case code needed - treat loopStart like any other stage start

**Implementation**:

```cpp
void handleLoopWrap() noexcept {
    // We're at the end of loopEnd stage
    // currentCutoff_ is at loopEnd's target

    // Now start transition to loopStart's target
    currentStage_ = loopStart_;

    // The "from" frequency is the current cutoff (loopEnd's target)
    stageFromFreq_ = currentCutoff_;

    // The "to" frequency is loopStart's target
    stageToFreq_ = getEffectiveTarget(loopStart_);

    // Use loopStart's curve and time
    stageCurve_ = stages_[loopStart_].curve;
    setStageTime(stages_[loopStart_].timeMs);

    // Reset phase for new transition
    stagePhase_ = 0.0f;
}
```

This ensures:
1. No discontinuity in cutoff frequency
2. Smooth transition using the destination stage's characteristics
3. Rhythmic patterns loop naturally

---

### 5. Release Phase Implementation

**Question**: How should release interact with the stage system?

**Decision**: Use OnePoleSmoother for exponential decay, independent of stage times

**Implementation**:

```cpp
void release() noexcept {
    if (state_ == EnvelopeState::Idle || state_ == EnvelopeState::Complete) {
        return;  // Nothing to release
    }

    // Save current cutoff as release start point
    releaseFromFreq_ = currentCutoff_;

    // Configure smoother for release decay
    releaseSmoother_.configure(releaseTimeMs_, static_cast<float>(sampleRate_));
    releaseSmoother_.snapTo(releaseFromFreq_);
    releaseSmoother_.setTarget(baseFrequency_);

    // Enter release state
    state_ = EnvelopeState::Releasing;
    loopEnabled_ = false;  // Exit any loop
}

// In process():
if (state_ == EnvelopeState::Releasing) {
    currentCutoff_ = releaseSmoother_.process();
    if (releaseSmoother_.isComplete()) {
        state_ = EnvelopeState::Complete;
        currentCutoff_ = baseFrequency_;
    }
}
```

**Why OnePoleSmoother for release?**
- Natural exponential decay (sounds musical)
- Already exists in Layer 1 (no new code)
- Handles denormals internally
- Completion detection built-in

---

### 6. Sample-Accurate Timing

**Question**: How to ensure stage timing accuracy within 1% (SC-002)?

**Decision**: Per-sample phase increment with floating-point accumulation

**Implementation**:

```cpp
void setStageTime(float timeMs) noexcept {
    float timeSamples = timeMs * 0.001f * static_cast<float>(sampleRate_);

    if (timeSamples <= 0.0f) {
        // Instant transition: phaseIncrement=1.0 completes in exactly 1 sample
        // The curve IS still applied (t goes 0->1 in one step), so the final
        // value is reached immediately without discontinuity artifacts
        phaseIncrement_ = 1.0f;
    } else {
        phaseIncrement_ = 1.0f / timeSamples;
    }
}

// In process():
stagePhase_ += phaseIncrement_;

// Calculate curved phase and interpolate frequency
float curvedPhase = applyCurve(std::clamp(stagePhase_, 0.0f, 1.0f), stageCurve_);
currentCutoff_ = stageFromFreq_ + (stageToFreq_ - stageFromFreq_) * curvedPhase;
```

**Accuracy Analysis**:

At 44.1kHz, 100ms stage = 4410 samples
- Phase increment = 1/4410 = 0.000226757...
- Accumulated phase error after 4410 samples: < 1e-5 (floating point precision)
- Timing accuracy: > 99.999%

At 96kHz, 100ms stage = 9600 samples
- Same analysis yields similar accuracy

---

### 7. Edge Case Handling

**Question**: How to handle edge cases specified in the spec?

| Edge Case | Handling |
|-----------|----------|
| `numStages = 0` | Clamp to 1, use default stage at baseFrequency |
| `stageTime = 0ms` | Set phaseIncrement to 1.0 (completes in 1 sample with curve still applied) |
| `loopStart > loopEnd` | Clamp loopStart to loopEnd (effectively disable loop) |
| `loopEnd >= numStages` | Clamp loopEnd to numStages - 1 |
| `trigger()` while running | Reset to stage 0, restart envelope |
| NaN/Inf input | Return 0, reset filter state |
| Frequency > Nyquist | Clamp via SVF's internal handling |
| Q outside range | Clamp via SVF's internal handling |

**Implementation**:

```cpp
void setNumStages(int stages) noexcept {
    numStages_ = std::clamp(stages, 1, static_cast<int>(kMaxStages));
    // Re-validate loop bounds
    loopEnd_ = std::clamp(loopEnd_, loopStart_, numStages_ - 1);
}

void setLoopStart(int stage) noexcept {
    loopStart_ = std::clamp(stage, 0, numStages_ - 1);
    // Ensure loopEnd >= loopStart
    if (loopEnd_ < loopStart_) {
        loopEnd_ = loopStart_;
    }
}

void setLoopEnd(int stage) noexcept {
    loopEnd_ = std::clamp(stage, loopStart_, numStages_ - 1);
}
```

---

## Technology Stack Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Filter type | SVF (existing Layer 1) | Modulation-stable, proven |
| Curve algorithm | Power-based | Simple, efficient, intuitive |
| Release smoothing | OnePoleSmoother (existing Layer 1) | Natural decay, built-in |
| State representation | Enum + switch | Clear, debuggable |
| Phase accumulation | Float [0, 1] | Sample-accurate timing |
| Stage storage | std::array<EnvelopeStage, 8> | Fixed size, no allocations |

---

## Performance Considerations

### Per-Sample Operations (in process())

1. State machine check (branch) - ~1 cycle
2. Phase increment and check - ~2 cycles
3. Curve calculation (std::pow) - ~20-40 cycles
4. Frequency interpolation - ~2 cycles
5. SVF setCutoff - ~5 cycles (coefficient update)
6. SVF process - ~15 cycles
7. Total: ~50-65 cycles per sample

At 96kHz with 1024-sample block:
- Total cycles: ~65,000 cycles
- Modern CPU at 3GHz: ~0.02ms
- Target 0.5% of buffer time (10.67ms at 96kHz/1024): 0.05ms budget
- **Margin: 2.5x under budget**

### Optimization Opportunities (if needed)

1. Cache curvedPhase calculation (only update on stage change)
2. Use polynomial approximation for curve instead of std::pow
3. Update SVF cutoff at control rate instead of audio rate

**Decision**: Implement straightforward approach first, profile, optimize only if needed.

---

## References

- Cytomic SVF whitepaper (filter topology)
- ADSR envelope implementations (state machine patterns)
- Granular synthesis literature (curve shaping)
- Existing codebase: `FilterStepSequencer`, `GrainEnvelope`
