# Data Model: Pitch-Tracking Filter Processor

**Feature**: 092-pitch-tracking-filter | **Date**: 2026-01-24

## Entities

### PitchTrackingFilterMode (Enum)

Filter response type selection for PitchTrackingFilter.

| Value | Numeric | SVFMode Mapping | Description |
|-------|---------|-----------------|-------------|
| Lowpass | 0 | SVFMode::Lowpass | 12 dB/oct lowpass response |
| Bandpass | 1 | SVFMode::Bandpass | Constant 0 dB peak bandpass |
| Highpass | 2 | SVFMode::Highpass | 12 dB/oct highpass response |

### PitchTrackingFilter (Class)

Layer 2 DSP Processor that tracks input pitch and modulates filter cutoff harmonically.

#### Configuration Parameters

| Field | Type | Range | Default | Description |
|-------|------|-------|---------|-------------|
| sampleRate_ | double | >= 1000.0 | 44100.0 | Audio sample rate in Hz |
| trackingSpeedMs_ | float | 1-500 | 50.0 | Tracking smoothing time (ms) |
| harmonicRatio_ | float | 0.125-16.0 | 1.0 | Cutoff = pitch * ratio |
| semitoneOffset_ | float | -48 to +48 | 0.0 | Additional semitone offset |
| fallbackCutoff_ | float | 20-Nyquist*0.45 | 1000.0 | Cutoff when pitch uncertain (Hz) |
| fallbackSmoothingMs_ | float | 1-500 | 50.0 | Fallback transition time (ms) |
| confidenceThreshold_ | float | 0.0-1.0 | 0.5 | Minimum confidence for tracking |
| resonance_ | float | 0.5-30.0 | 0.707 | Filter Q factor |
| filterType_ | PitchTrackingFilterMode | enum | Lowpass | Filter response type |

#### Internal Constants

| Constant | Type | Value | Description |
|----------|------|-------|-------------|
| kRapidChangeThreshold | float | 10.0 | Semitones/sec threshold for adaptive tracking |
| kFastTrackingMs | float | 10.0 | Fast tracking speed for rapid pitch changes |
| kMinCutoffHz | float | 20.0 | Minimum cutoff frequency |
| kMinResonance | float | 0.5 | Minimum Q value |
| kMaxResonance | float | 30.0 | Maximum Q value |
| kMinTrackingMs | float | 1.0 | Minimum tracking speed |
| kMaxTrackingMs | float | 500.0 | Maximum tracking speed |
| kMinHarmonicRatio | float | 0.125 | Minimum harmonic ratio (3 octaves down) |
| kMaxHarmonicRatio | float | 16.0 | Maximum harmonic ratio (4 octaves up) |
| kMinSemitoneOffset | float | -48.0 | Minimum semitone offset (4 octaves down) |
| kMaxSemitoneOffset | float | 48.0 | Maximum semitone offset (4 octaves up) |

#### Monitoring State

| Field | Type | Description |
|-------|------|-------------|
| currentCutoff_ | float | Current filter cutoff frequency (Hz) |
| detectedPitch_ | float | Current detected pitch (Hz) |
| pitchConfidence_ | float | Current pitch detection confidence [0,1] |

#### Tracking State

| Field | Type | Initial | Description |
|-------|------|---------|-------------|
| lastValidPitch_ | float | 440.0 | Last valid detected pitch (Hz) |
| samplesSinceLastValid_ | size_t | 0 | Samples since last valid detection |
| wasTracking_ | bool | false | Previous frame was tracking (for smoother direction) |
| prepared_ | bool | false | Processor has been prepared |

#### Composed Components

| Component | Type | Description |
|-----------|------|-------------|
| pitchDetector_ | PitchDetector | Autocorrelation pitch detection (Layer 1) |
| filter_ | SVF | TPT State Variable Filter (Layer 1) |
| cutoffSmoother_ | OnePoleSmoother | Cutoff smoothing (Layer 1) |

## State Transitions

### Tracking State Machine

```
                    +-----------------+
                    |    UNPREPARED   |
                    +-----------------+
                           |
                           | prepare()
                           v
      +----------> +-----------------+
      |            |    FALLBACK     |
      |            | (low confidence)|
      |            +-----------------+
      |                 |     ^
      |    confidence   |     | confidence
      |    >= threshold |     | < threshold
      |                 v     |
      |            +-----------------+
      +----------- |    TRACKING     |
        reset()    | (high confidence|
                   +-----------------+
```

### Smoothing Behavior

| Transition | Smoothing Time | Rationale |
|------------|----------------|-----------|
| FALLBACK -> TRACKING | trackingSpeedMs_ | Smooth onset of tracking |
| TRACKING -> TRACKING (normal) | trackingSpeedMs_ | Standard tracking |
| TRACKING -> TRACKING (rapid) | kFastTrackingMs | Fast follow for vibrato |
| TRACKING -> FALLBACK | fallbackSmoothingMs_ | Smooth transition to fallback |
| FALLBACK -> FALLBACK | fallbackSmoothingMs_ | Maintain fallback smoothly |

## Validation Rules

### Parameter Validation

| Parameter | Validation | On Invalid |
|-----------|------------|------------|
| sampleRate | >= 1000.0 | Clamp to 1000.0 |
| trackingSpeedMs | [1, 500] | Clamp to range |
| harmonicRatio | [0.125, 16.0] | Clamp to range |
| semitoneOffset | [-48, 48] | Clamp to range |
| fallbackCutoff | [20, sampleRate*0.45] | Clamp to range |
| fallbackSmoothingMs | [1, 500] | Clamp to range |
| confidenceThreshold | [0.0, 1.0] | Clamp to range |
| resonance | [0.5, 30.0] | Clamp to range |

### Input Validation

| Input | Check | Action |
|-------|-------|--------|
| NaN | detail::isNaN() | Return 0.0f, reset state |
| Inf | detail::isInf() | Return 0.0f, reset state |

### Cutoff Clamping

```cpp
float clampCutoff(float hz) const noexcept {
    const float maxCutoff = static_cast<float>(sampleRate_) * 0.45f;
    return std::clamp(hz, kMinCutoffHz, maxCutoff);
}
```

## Key Calculations

### Cutoff from Pitch

```cpp
// FR-005, FR-006: cutoff = pitch * ratio * 2^(semitones/12)
float calculateCutoff(float pitch) const noexcept {
    const float offsetMultiplier = semitonesToRatio(semitoneOffset_);
    const float cutoff = pitch * harmonicRatio_ * offsetMultiplier;
    return clampCutoff(cutoff);
}
```

### Semitone Rate Detection (FR-004a)

```cpp
// Detect rapid pitch changes for adaptive tracking
float calculateSemitoneRate(float currentPitch, size_t sampleDelta) const noexcept {
    if (sampleDelta == 0) return 0.0f;

    const float semitoneDiff = 12.0f * std::log2(currentPitch / lastValidPitch_);
    const float timeSec = static_cast<float>(sampleDelta) / static_cast<float>(sampleRate_);

    return std::abs(semitoneDiff) / timeSec;
}
```

### Latency

```cpp
// FR-021: Latency equals pitch detector window
[[nodiscard]] size_t getLatency() const noexcept {
    return PitchDetector::kDefaultWindowSize;  // 256 samples
}
```

## Memory Layout

```cpp
class PitchTrackingFilter {
    // Configuration (56 bytes)
    double sampleRate_;              // 8 bytes
    float trackingSpeedMs_;          // 4 bytes
    float harmonicRatio_;            // 4 bytes
    float semitoneOffset_;           // 4 bytes
    float fallbackCutoff_;           // 4 bytes
    float fallbackSmoothingMs_;      // 4 bytes
    float confidenceThreshold_;      // 4 bytes
    float resonance_;                // 4 bytes
    PitchTrackingFilterMode filterType_;  // 1 byte + padding
    bool prepared_;                  // 1 byte + padding

    // Monitoring (12 bytes)
    float currentCutoff_;            // 4 bytes
    float detectedPitch_;            // 4 bytes
    float pitchConfidence_;          // 4 bytes

    // Tracking state (16 bytes)
    float lastValidPitch_;           // 4 bytes
    size_t samplesSinceLastValid_;   // 8 bytes
    bool wasTracking_;               // 1 byte + padding

    // Composed components (variable)
    PitchDetector pitchDetector_;    // ~2.5KB (buffers)
    SVF filter_;                     // ~100 bytes
    OnePoleSmoother cutoffSmoother_; // ~24 bytes
};
```

**Total estimated size**: ~2.7KB (dominated by PitchDetector buffers)

## Thread Safety

- **Not thread-safe**: Create separate instances per audio thread
- All processing methods are **noexcept**
- No locks or mutexes
- No memory allocation in process()
