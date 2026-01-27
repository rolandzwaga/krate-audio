# Research: Sample & Hold Filter

**Feature**: 089-sample-hold-filter | **Date**: 2026-01-23

## 1. Trigger System Architecture

### Decision: Unified Trigger Manager with Mode Selection

**Rationale**: Rather than three separate trigger implementations, use a single trigger manager that evaluates based on current mode. This simplifies state management and enables clean mode switching.

**Alternatives Considered**:
1. **Polymorphic TriggerSource base class** - Rejected: Virtual call overhead in audio path, unnecessary complexity for 3 modes
2. **Template-based trigger selection** - Rejected: Cannot change mode at runtime without recompilation
3. **Switch-based mode handling** - Selected: Simple, efficient, runtime-configurable

### Implementation Pattern

```cpp
// Trigger modes as enum
enum class TriggerSource : uint8_t {
    Clock = 0,   // Regular intervals based on hold time
    Audio,       // Transient detection from input signal
    Random       // Probability-based at hold intervals
};

// Single trigger evaluation function
bool shouldTrigger(float inputSample) noexcept {
    switch (triggerSource_) {
        case TriggerSource::Clock:
            return clockTrigger();
        case TriggerSource::Audio:
            return audioTrigger(inputSample);
        case TriggerSource::Random:
            return randomTrigger();
    }
    return false;
}
```

### Mode Switching Behavior (from clarification)

When trigger source changes mid-hold:
- **Behavior**: Sample from new source on next buffer boundary (sample-accurate switch)
- **Implementation**: Set flag to trigger on next process() call after mode change

## 2. Per-Parameter Sample Source Architecture

### Decision: Independent Source Selection per Parameter

**Rationale**: Each sampleable parameter (cutoff, Q, pan) can have its own independent sample source. This provides maximum flexibility for creative sound design.

**Structure**:

```cpp
enum class SampleSource : uint8_t {
    LFO = 0,      // Internal LFO [-1, 1]
    Random,       // Xorshift32 random [-1, 1]
    Envelope,     // EnvelopeFollower [0, 1]
    External      // User-provided value [0, 1]
};

// Per-parameter configuration
struct ParameterConfig {
    bool enabled = false;           // Whether this parameter is being sampled
    SampleSource source = SampleSource::LFO;
    float modulationRange = 0.0f;   // Parameter-specific range
};

// Three parameter configurations
ParameterConfig cutoffConfig_;  // modulationRange in octaves [0, 8]
ParameterConfig qConfig_;       // modulationRange normalized [0, 1]
ParameterConfig panConfig_;     // modulationRange normalized [-1, 1]
```

### Source Value Retrieval

```cpp
float getSampleValue(SampleSource source) noexcept {
    switch (source) {
        case SampleSource::LFO:
            return lfoValue_;  // Already [-1, 1]
        case SampleSource::Random:
            return rng_.nextFloat();  // [-1, 1]
        case SampleSource::Envelope:
            return envelopeValue_ * 2.0f - 1.0f;  // Convert [0,1] to [-1,1]
        case SampleSource::External:
            return externalValue_ * 2.0f - 1.0f;  // Convert [0,1] to [-1,1]
    }
    return 0.0f;
}
```

## 3. Stereo Pan Implementation

### Decision: Symmetric Cutoff Offset

**Rationale**: Pan modulation affects left/right filter cutoff frequencies symmetrically. This creates stereo width through frequency differentiation rather than amplitude panning.

**From clarification**: `pan=-1` means Left lower cutoff, Right higher cutoff; `pan=+1` is the opposite.

### Implementation

```cpp
// Apply pan offset to calculate L/R cutoffs
// pan range: [-1, 1], panOctaveRange_ in octaves [0, 4]
void calculateStereoCutoffs(float baseCutoff, float panValue,
                            float& leftCutoff, float& rightCutoff) noexcept {
    // Octave offset based on pan and modulation range
    const float octaveOffset = panValue * panOctaveRange_;

    // Left channel: lower when pan negative
    // Right channel: higher when pan negative
    leftCutoff = baseCutoff * std::pow(2.0f, -octaveOffset);
    rightCutoff = baseCutoff * std::pow(2.0f, octaveOffset);

    // Clamp to valid range
    const float maxCutoff = static_cast<float>(sampleRate_) * SVF::kMaxCutoffRatio;
    leftCutoff = std::clamp(leftCutoff, SVF::kMinCutoff, maxCutoff);
    rightCutoff = std::clamp(rightCutoff, SVF::kMinCutoff, maxCutoff);
}
```

### Stereo Processing Architecture

```cpp
// Two SVF instances for true stereo processing
SVF filterL_;
SVF filterR_;

void processStereo(float& left, float& right) noexcept {
    // Update trigger/hold state (mono operation)
    updateTriggerState((left + right) * 0.5f);

    // Get smoothed parameters
    const float cutoff = cutoffSmoother_.process();
    const float q = qSmoother_.process();
    const float pan = panSmoother_.process();

    // Calculate stereo cutoffs
    float leftCutoff, rightCutoff;
    calculateStereoCutoffs(cutoff, pan, leftCutoff, rightCutoff);

    // Update and process filters
    filterL_.setCutoff(leftCutoff);
    filterR_.setCutoff(rightCutoff);
    filterL_.setResonance(q);
    filterR_.setResonance(q);

    left = filterL_.process(left);
    right = filterR_.process(right);
}
```

## 4. Clock Trigger Timing

### Decision: Sample-Accurate Counter with Double-Precision

**Rationale**: Hold time can be long (up to 10 seconds) and must be accurate within 1 sample at 192kHz (SC-001). Using double-precision counter prevents drift.

### Implementation

```cpp
// State
double samplesUntilTrigger_ = 0.0;
double holdTimeSamples_ = 0.0;

void setHoldTime(float ms) noexcept {
    holdTimeMs_ = std::clamp(ms, 0.1f, 10000.0f);
    holdTimeSamples_ = (holdTimeMs_ * 0.001) * sampleRate_;
}

bool clockTrigger() noexcept {
    samplesUntilTrigger_ -= 1.0;
    if (samplesUntilTrigger_ <= 0.0) {
        samplesUntilTrigger_ += holdTimeSamples_;
        return true;
    }
    return false;
}
```

## 5. Audio Trigger (Transient Detection)

### Decision: Peak Detection with EnvelopeFollower

**Rationale**: Use existing EnvelopeFollower in Peak mode with fast attack for transient detection. This reuses tested code and provides configurable threshold.

### Implementation

```cpp
// Configuration
float transientThreshold_ = 0.5f;  // Normalized [0, 1]
float previousEnvelope_ = 0.0f;
bool holdingAfterTransient_ = false;
double transientHoldSamples_ = 0.0;

bool audioTrigger(float input) noexcept {
    // Process envelope follower
    const float envelope = envelopeFollower_.processSample(input);

    // Detect rising edge crossing threshold
    bool trigger = false;
    if (!holdingAfterTransient_ &&
        envelope > transientThreshold_ &&
        previousEnvelope_ <= transientThreshold_) {
        trigger = true;
        holdingAfterTransient_ = true;
        transientHoldSamples_ = holdTimeSamples_;  // Start hold period
    }

    // Manage hold period (ignore transients during hold)
    if (holdingAfterTransient_) {
        transientHoldSamples_ -= 1.0;
        if (transientHoldSamples_ <= 0.0) {
            holdingAfterTransient_ = false;
        }
    }

    previousEnvelope_ = envelope;
    return trigger;
}
```

### Response Time (SC-002)

EnvelopeFollower attack time set to 0.1ms (minimum) for near-instant response. At 192kHz, 1ms = 192 samples, so 0.1ms = ~19 samples latency.

## 6. Random Trigger

### Decision: Probability Evaluation at Hold Intervals

**Rationale**: Random trigger evaluates at each potential hold point (not every sample). This provides musically useful randomness while maintaining rhythmic structure.

### Implementation

```cpp
float triggerProbability_ = 1.0f;  // [0, 1]

bool randomTrigger() noexcept {
    // Same timing as clock trigger
    samplesUntilTrigger_ -= 1.0;
    if (samplesUntilTrigger_ <= 0.0) {
        samplesUntilTrigger_ += holdTimeSamples_;

        // Evaluate probability
        const float roll = rng_.nextUnipolar();  // [0, 1]
        return roll < triggerProbability_;
    }
    return false;
}
```

### Statistical Validation (SC-007)

At probability=0.5 over 1000 trials, expect 45-55% triggers (binomial distribution). Test will use fixed seed for reproducibility.

## 7. Slew Limiting Architecture

### Decision: Slew on Sampled Values Only

**Rationale**: Per clarification, slew applies ONLY to sampled modulation values. Base parameter changes (user knob adjustments) are instant.

### Implementation

```cpp
// Smoothers for sampled values only
OnePoleSmoother cutoffModSmoother_;  // Smooths cutoff modulation
OnePoleSmoother qModSmoother_;       // Smooths Q modulation
OnePoleSmoother panModSmoother_;     // Smooths pan modulation

// Slew time configuration
float slewTimeMs_ = 0.0f;  // [0, 500]

void setSlewTime(float ms) noexcept {
    slewTimeMs_ = std::clamp(ms, 0.0f, 500.0f);
    const float sampleRateF = static_cast<float>(sampleRate_);
    cutoffModSmoother_.configure(slewTimeMs_, sampleRateF);
    qModSmoother_.configure(slewTimeMs_, sampleRateF);
    panModSmoother_.configure(slewTimeMs_, sampleRateF);
}

// When trigger occurs, update smoother targets
void onTrigger() noexcept {
    if (cutoffConfig_.enabled) {
        const float modValue = getSampleValue(cutoffConfig_.source);
        cutoffModSmoother_.setTarget(modValue);
    }
    // Similar for Q and pan...
}

// Final cutoff calculation
float calculateFinalCutoff() noexcept {
    // Base cutoff (instant, no smoothing)
    float cutoff = baseCutoffHz_;

    // Apply smoothed modulation
    const float modValue = cutoffModSmoother_.process();
    const float octaveOffset = modValue * cutoffConfig_.modulationRange;
    cutoff *= std::pow(2.0f, octaveOffset);

    return std::clamp(cutoff, SVF::kMinCutoff, maxCutoff_);
}
```

### Slew Target Update Behavior

When slew time exceeds hold time and new target arrives before completion:
- Smoother redirects to new target
- No discontinuity (OnePoleSmoother handles this naturally)

## 8. Reset Behavior

### Decision: Initialize to Base Parameter Values

**Rationale**: Per clarification, held values initialize to base parameter values so filter works immediately after reset.

### Implementation

```cpp
void reset() noexcept {
    // Reset filters
    filterL_.reset();
    filterR_.reset();

    // Reset envelope follower
    envelopeFollower_.reset();

    // Reset LFO
    lfo_.reset();

    // Reset RNG to seed
    rng_.seed(seed_);

    // Reset trigger state
    samplesUntilTrigger_ = holdTimeSamples_;
    holdingAfterTransient_ = false;
    previousEnvelope_ = 0.0f;
    pendingTriggerSourceSwitch_ = false;

    // Initialize smoothers to base values (not modulated)
    cutoffModSmoother_.snapTo(0.0f);  // No modulation offset
    qModSmoother_.snapTo(0.0f);
    panModSmoother_.snapTo(0.0f);
}
```

## 9. Performance Considerations

### CPU Budget

Target: < 0.5% CPU @ 44.1kHz stereo (SC-004)

**Per-sample operations**:
- Trigger evaluation: ~5-10 ops
- 3x smoother process: ~15 ops
- 2x SVF process: ~40 ops (20 each)
- Cutoff calculation: ~10 ops
- **Total**: ~70-75 ops/sample

At 44.1kHz stereo, that's ~3.3M ops/second, well within budget.

### Memory Layout

```cpp
// Hot data grouped together
struct HotState {
    double samplesUntilTrigger_;
    float cutoffModValue_;
    float qModValue_;
    float panModValue_;
    float previousEnvelope_;
    bool holdingAfterTransient_;
} hot_;

// Cold configuration separate
struct Configuration {
    ParameterConfig cutoffConfig_;
    ParameterConfig qConfig_;
    ParameterConfig panConfig_;
    float holdTimeMs_;
    // ... other config
} config_;
```

## 10. Test Strategy

### Unit Test Categories

1. **Trigger System Tests**
   - Clock: Verify exact timing at various hold times
   - Audio: Verify transient detection and hold period
   - Random: Statistical distribution tests with fixed seed

2. **Parameter Sampling Tests**
   - Per-parameter source independence
   - Value range verification for each source
   - Enable/disable behavior

3. **Stereo Processing Tests**
   - Pan offset symmetry verification
   - Independent L/R filter behavior
   - Mono-linked mode (pan disabled)

4. **Slew Limiting Tests**
   - Instant changes when slew=0
   - 99% settling time verification (SC-003)
   - Base parameter instant vs mod slewed

5. **Edge Case Tests**
   - Hold time vs buffer size boundaries
   - Slew time > hold time
   - Very fast hold times (< 1ms)
   - Mode switching during hold

6. **Determinism Tests**
   - Same seed/params/input = identical output (SC-005)

### Approval Tests

None planned - this processor has deterministic behavior that's better verified with unit tests.

## Summary of Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Trigger architecture | Switch-based mode selection | Simple, efficient, runtime-configurable |
| Per-parameter sources | Independent source per param | Maximum creative flexibility |
| Stereo pan | Symmetric cutoff offset | Creates stereo width through frequency |
| Clock timing | Double-precision counter | Prevents drift over long hold times |
| Audio trigger | EnvelopeFollower Peak mode | Reuses tested code, configurable |
| Random trigger | Probability at hold intervals | Musically useful randomness |
| Slew scope | Modulation values only | Per user clarification |
| Reset values | Base parameters | Immediate filter operation |
