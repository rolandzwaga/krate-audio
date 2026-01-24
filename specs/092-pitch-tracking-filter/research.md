# Research: Pitch-Tracking Filter Processor

**Feature**: 092-pitch-tracking-filter | **Date**: 2026-01-24

## Executive Summary

This research document resolves all technical unknowns identified during planning. The PitchTrackingFilter will compose existing primitives (PitchDetector, SVF, OnePoleSmoother) following established patterns from sibling processors.

## Research Tasks

### 1. PitchDetector Integration

**Question**: How to integrate PitchDetector's autocorrelation-based detection with our processor?

**Findings**:

The existing `PitchDetector` (Layer 1 primitive) provides:
- Autocorrelation-based pitch detection optimized for low latency
- Detection range: 50-1000 Hz (hardware constrained)
- Analysis window: 256 samples default (~5.8ms at 44.1kHz)
- Detection runs every windowSize/4 samples automatically
- Confidence value [0, 1] for pitch validity
- Internal confidence threshold: 0.3 (hardcoded)

**Integration approach**:
1. Push samples via `push()` in per-sample processing or `pushBlock()` for block processing
2. Query `getDetectedFrequency()` and `getConfidence()` after pushing
3. Apply our own configurable confidence threshold (default 0.5 per spec)
4. Latency = windowSize samples (~6ms at 44.1kHz)

**Decision**: Use PitchDetector as-is. The internal 0.3 threshold doesn't affect us since we query raw confidence and apply our own threshold.

**Rationale**: The primitive is designed for exactly this use case. No modifications needed.

---

### 2. Adaptive Tracking Speed for Rapid Pitch Changes

**Question**: How to detect rapid pitch changes (>10 semitones/sec) and adapt tracking speed?

**Findings**:

Rapid pitch change detection requires:
1. Track previous valid pitch
2. Calculate semitone difference: `12 * log2(currentPitch / previousPitch)`
3. Track time since last valid pitch (in samples)
4. Calculate semitones/second: `semitoneDiff / (timeDiff / sampleRate)`

**Implementation approach**:
```cpp
// State variables
float lastValidPitch_ = 440.0f;       // Hz
size_t samplesSinceLastValid_ = 0;    // Samples since last valid pitch

// In process():
float semitoneDiff = 12.0f * std::log2(currentPitch / lastValidPitch_);
float timeSec = static_cast<float>(samplesSinceLastValid_) / sampleRate_;
float semitonesPerSec = std::abs(semitoneDiff) / timeSec;

bool rapidChange = semitonesPerSec > 10.0f;  // Threshold from spec

// Use faster tracking when rapid change detected
float effectiveTrackingMs = rapidChange ? fastTrackingMs_ : trackingSpeedMs_;
```

**Fast tracking speed**: When rapid pitch change is detected, use a faster smoothing time. Research suggests:
- Normal tracking: User-configurable (default 50ms)
- Fast tracking: ~5-10ms for responsive vibrato following
- Transition between normal/fast should be immediate (no hysteresis needed)

**Decision**: Use 10ms for adaptive fast tracking. This provides responsive following without overly jittery behavior.

**Rationale**: 10ms is fast enough to follow typical vibrato rates (5-7Hz) while still providing some smoothing to prevent noise from causing erratic behavior.

---

### 3. Cutoff Calculation with Harmonic Ratio and Semitone Offset

**Question**: What is the correct formula for calculating cutoff from pitch, ratio, and semitone offset?

**Findings**:

From spec FR-005 and FR-006:
- Cutoff = detectedPitch * harmonicRatio * 2^(semitoneOffset/12)

Using existing `semitonesToRatio()` from `pitch_utils.h`:
```cpp
float calculateCutoff(float detectedPitch) const noexcept {
    // harmonicRatio * 2^(semitones/12) = harmonicRatio * semitonesToRatio(semitones)
    float cutoff = detectedPitch * harmonicRatio_ * semitonesToRatio(semitoneOffset_);
    return clampCutoff(cutoff);
}
```

**Clamping** (from spec FR-007):
- Minimum: 20 Hz
- Maximum: sampleRate * 0.45 (Nyquist-safe)

**Edge cases**:
- harmonicRatio = 0 -> cutoff = 0 -> clamped to 20 Hz
- Very high pitch * high ratio -> clamped to Nyquist-safe max
- Negative semitone offset -> lower cutoff (valid use case)

**Decision**: Use the formula as specified, with `semitonesToRatio()` for the offset calculation.

**Rationale**: Matches spec requirements exactly and reuses existing utility function.

---

### 4. Confidence-Gated Fallback Behavior

**Question**: How should transitions to/from fallback cutoff work?

**Findings**:

From spec FR-011 through FR-013:
- Fallback cutoff: Configurable [20Hz, sampleRate*0.45], default 1000Hz
- Fallback smoothing: Configurable [1-500ms], default 50ms
- Track last valid pitch for smooth transitions

**State machine approach**:
```
State: TRACKING
  - Pitch confidence >= threshold
  - Cutoff = calculateCutoff(detectedPitch)
  - Update lastValidPitch_

State: FALLBACK
  - Pitch confidence < threshold
  - Cutoff smoothly transitions to fallbackCutoff_
  - Use fallbackSmoothingMs_ for transition speed
```

**Smoothing strategy**:
Use a single OnePoleSmoother for the cutoff value. Configure it dynamically:
- When tracking: Use tracking speed (or fast tracking if rapid change)
- When transitioning to fallback: Use fallback smoothing time

This is similar to TransientAwareFilter's approach with dynamic smoother reconfiguration.

**Decision**: Single smoother with dynamic time configuration based on confidence state.

**Rationale**: Simpler than dual smoothers, matches established pattern from TransientAwareFilter.

---

### 5. Filter Type Mapping

**Question**: How to map our filter modes to SVFMode?

**Findings**:

From spec FR-009, we need three types:
| PitchTrackingFilterMode | SVFMode |
|-------------------------|---------|
| Lowpass | SVFMode::Lowpass |
| Bandpass | SVFMode::Bandpass |
| Highpass | SVFMode::Highpass |

This matches the exact pattern used by TransientAwareFilter and SidechainFilter.

**Decision**: Create local `PitchTrackingFilterMode` enum with same pattern as siblings.

**Rationale**: Consistency with existing processors. Each processor defines its own mode enum to allow future divergence if needed.

---

### 6. Latency Calculation

**Question**: What is the processing latency?

**Findings**:

The latency equals the PitchDetector's analysis window:
- Default: 256 samples (~5.8ms at 44.1kHz)
- At 48kHz: ~5.3ms
- At 96kHz: ~2.7ms

Formula: `latency = windowSize / sampleRate`

Note: The audio path through SVF has negligible latency (a few samples for filter settling), so total latency is dominated by pitch detection.

**Decision**: Report latency as `PitchDetector::kDefaultWindowSize` samples.

**Rationale**: Accurate representation of actual processing delay.

---

### 7. Real-Time Safety Verification

**Question**: Are all operations real-time safe?

**Findings**:

Operations in process():
1. PitchDetector::push() - Writes to pre-allocated buffer, O(1)
2. PitchDetector detection - Runs every windowSize/4 samples, O(windowSize^2) for autocorrelation
3. std::log2() - Math library function, no allocation
4. OnePoleSmoother::process() - O(1), no allocation
5. SVF::process() - O(1), no allocation

Potential concerns:
- Autocorrelation is O(windowSize^2) but with small window (256) this is fast
- log2() is used for semitone calculation - could be slow on some platforms

**Mitigation for log2**:
- Only calculate semitone rate when confidence is valid
- Cache previous pitch to avoid repeated calculations
- Consider using a lookup table if profiling shows issues (not needed for initial implementation)

**Decision**: All operations are real-time safe. No changes needed.

**Rationale**: PitchDetector was designed for real-time use. Math operations are acceptable overhead.

---

### 8. Default Parameter Values

**Question**: What should the default parameter values be?

**Findings**:

From spec clarification session and FR requirements:

| Parameter | Default | Range | Rationale |
|-----------|---------|-------|-----------|
| Tracking Speed | 50ms | 1-500ms | Balanced response vs stability |
| Harmonic Ratio | 1.0 | 0.125-16.0 | Unity = cutoff follows pitch |
| Semitone Offset | 0 | -48 to +48 | No offset by default |
| Fallback Cutoff | 1000Hz | 20-Nyquist*0.45 | Neutral midrange |
| Fallback Smoothing | 50ms | 1-500ms | Match tracking speed |
| Resonance | 0.707 | 0.5-30.0 | Butterworth (flat passband) |
| Filter Type | Lowpass | LP/BP/HP | Most common use case |
| Confidence Threshold | 0.5 | 0.0-1.0 | Balanced sensitivity |
| Fast Tracking Speed | 10ms | (internal) | Responsive to vibrato |
| Rapid Change Threshold | 10 semitones/sec | (internal) | Per spec clarification |

**Decision**: Use values from spec exactly.

**Rationale**: Spec values were determined through clarification session with user.

---

## Algorithm Summary

### Processing Flow

```
1. Handle NaN/Inf input (return 0, reset state)

2. Push sample to PitchDetector
   - Detection runs automatically every windowSize/4 samples

3. Query pitch detection results
   - currentPitch = getDetectedFrequency()
   - confidence = getConfidence()

4. Determine if pitch is valid (confidence >= threshold)

5. If valid:
   a. Calculate semitone rate from previous valid pitch
   b. Check for rapid pitch change (>10 semitones/sec)
   c. Select tracking speed (normal or fast)
   d. Calculate target cutoff: pitch * ratio * 2^(offset/12)
   e. Update last valid pitch state

6. If not valid:
   a. Use fallback cutoff as target
   b. Use fallback smoothing time

7. Apply smoothing to cutoff
   - Reconfigure smoother with appropriate time
   - Process to get smoothed cutoff

8. Update SVF parameters
   - setCutoff(smoothedCutoff)
   - (resonance and type set via parameter changes, not per-sample)

9. Filter audio through SVF
   - return filter_.process(input)
```

### State Variables

```cpp
// Configuration
double sampleRate_ = 44100.0;
float trackingSpeedMs_ = 50.0f;
float fastTrackingMs_ = 10.0f;
float harmonicRatio_ = 1.0f;
float semitoneOffset_ = 0.0f;
float fallbackCutoff_ = 1000.0f;
float fallbackSmoothingMs_ = 50.0f;
float confidenceThreshold_ = 0.5f;
float resonance_ = 0.707f;
PitchTrackingFilterMode filterType_ = PitchTrackingFilterMode::Lowpass;

// Internal constants
static constexpr float kRapidChangeThreshold = 10.0f;  // semitones/sec

// Monitoring state
float currentCutoff_ = 1000.0f;
float detectedPitch_ = 0.0f;
float pitchConfidence_ = 0.0f;

// Tracking state
float lastValidPitch_ = 440.0f;
size_t samplesSinceLastValid_ = 0;
bool wasTracking_ = false;  // For direction detection in smoother

// Composed components
PitchDetector pitchDetector_;
SVF filter_;
OnePoleSmoother cutoffSmoother_;
```

---

## Alternatives Considered

### 1. Dual Smoother Approach

**Alternative**: Use separate smoothers for tracking and fallback.

**Rejected because**: More complex, harder to manage crossfades, TransientAwareFilter shows single dynamic smoother works well.

### 2. Dedicated Pitch Rate Calculator

**Alternative**: Extract semitone rate calculation to separate class.

**Rejected because**: Simple calculation, only needed here, overhead of separate class not justified.

### 3. Using PitchDetector's Internal Threshold

**Alternative**: Let PitchDetector's isPitchValid() gate our tracking.

**Rejected because**: Internal threshold (0.3) is too permissive for some use cases. User needs configurable threshold.

### 4. Block-Based Pitch Detection

**Alternative**: Only run pitch detection once per block.

**Rejected because**: PitchDetector already optimizes this (runs every windowSize/4 samples). Forcing per-block could cause latency variations.

---

## References

- PitchDetector implementation: `dsp/include/krate/dsp/primitives/pitch_detector.h`
- SVF implementation: `dsp/include/krate/dsp/primitives/svf.h`
- OnePoleSmoother implementation: `dsp/include/krate/dsp/primitives/smoother.h`
- TransientAwareFilter (reference pattern): `dsp/include/krate/dsp/processors/transient_filter.h`
- SidechainFilter (reference pattern): `dsp/include/krate/dsp/processors/sidechain_filter.h`
- Pitch utilities: `dsp/include/krate/dsp/core/pitch_utils.h`
- Filter roadmap: `specs/FLT-ROADMAP.md` (Phase 15.3)
