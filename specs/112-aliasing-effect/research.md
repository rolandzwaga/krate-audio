# Research: AliasingEffect

**Feature**: 112-aliasing-effect | **Date**: 2026-01-27

## 1. Two-Stage Cascade Bandpass Filter (24dB/oct)

### Background

The spec requires band isolation using a two-stage cascade bandpass filter to achieve 24dB/octave rolloff. This provides clean separation between the aliasing band and bypassed frequencies.

### Research Findings

A bandpass filter can be implemented by cascading a highpass and lowpass filter:
- Highpass at low band frequency
- Lowpass at high band frequency

For 24dB/octave (4-pole) response, each filter needs two cascaded biquad stages.

**Butterworth Q Values for 2-Stage Cascade:**

The `butterworthQ(stageIndex, totalStages)` function in `biquad.h` calculates the correct Q for each stage:
- Stage 0: Q = 0.5412 (Q = 1/(2*cos(pi*1/8)))
- Stage 1: Q = 1.3066 (Q = 1/(2*cos(pi*3/8)))

Reference: [Cascading filters - EarLevel Engineering](https://www.earlevel.com/main/2016/09/29/cascading-filters/)

### Design Decision

**Decision**: Use two BiquadCascade<2> instances for band isolation:
1. `bandHighpass_` - BiquadCascade<2> configured as Butterworth highpass at low band frequency
2. `bandLowpass_` - BiquadCascade<2> configured as Butterworth lowpass at high band frequency

**Rationale**:
- BiquadCascade<2> already exists and handles Butterworth Q calculations
- Cascading HP then LP gives bandpass response
- 24dB/oct slope on both edges provides clean band separation

**Alternatives Considered**:
1. Single bandpass biquad (12dB/oct) - Rejected: Spec requires 24dB/oct
2. Custom bandpass cascade - Rejected: HP+LP cascade is simpler and reuses existing code
3. State-variable filter - Rejected: Overkill for this use case

### Implementation Notes

```cpp
// Band isolation using cascaded HP+LP
BiquadCascade<2> bandHighpass_;  // Isolates low edge
BiquadCascade<2> bandLowpass_;   // Isolates high edge

void updateBandFilters(float lowHz, float highHz, float sampleRate) {
    bandHighpass_.setButterworth(FilterType::Highpass, lowHz, sampleRate);
    bandLowpass_.setButterworth(FilterType::Lowpass, highHz, sampleRate);
}

// Processing: input -> HP -> LP gives band-isolated signal
float bandSignal = bandLowpass_.process(bandHighpass_.process(input));
```

## 2. Band Recombination Strategy

### Background

The spec requires that signal outside the aliasing band bypasses the aliaser and recombines with the processed band signal via "simple summing (no phase compensation)."

### Research Findings

Phase compensation would require:
- Allpass filters to match phase response
- Significant implementation complexity
- May not be necessary for digital destruction aesthetic

The spec explicitly states phase artifacts contribute to the desired digital grunge effect.

### Design Decision

**Decision**: Simple subtraction and addition for band splitting/recombining.

**Rationale**:
- Per spec clarification: "phase artifacts contribute to digital destruction aesthetic"
- Simpler implementation
- Lower CPU usage
- Comb filtering artifacts are acceptable for this effect type

**Implementation Notes**:

```cpp
// Split signal into band and non-band components
float bandSignal = bandLowpass_.process(bandHighpass_.process(input));
float nonBandSignal = input - bandSignal;  // Everything outside the band

// Process band through aliaser (frequency shift + downsample)
float processedBand = /* aliasing chain */;

// Recombine with simple addition
float wetOutput = nonBandSignal + processedBand;
```

**Note**: This subtraction approach may introduce some phase artifacts at the band edges, but this is acceptable per the spec requirements.

## 3. SampleRateReducer Extension (Factor 8 to 32)

### Background

The existing SampleRateReducer has `kMaxReductionFactor = 8.0f`. The spec requires factor up to 32 for extreme digital destruction.

### Research Findings

The SampleRateReducer uses sample-and-hold algorithm:
```cpp
if (holdCounter_ >= reductionFactor_) {
    holdValue_ = input;
    holdCounter_ -= reductionFactor_;  // Fractional accuracy
}
```

This algorithm works correctly for any reduction factor. The limit of 8 is purely a design constraint, not a technical limitation.

At factor 32 with 44100Hz sample rate:
- Effective sample rate: 44100/32 = 1378.125 Hz
- Reduced Nyquist: ~689 Hz
- Any frequency above 689 Hz will alias

### Design Decision

**Decision**: Change `kMaxReductionFactor` from 8.0f to 32.0f in sample_rate_reducer.h

**Rationale**:
- Algorithm already supports any factor
- Benefits other future lo-fi effects
- Single line change
- No breaking changes (old range still valid)

**Alternatives Considered**:
1. Internal sample-and-hold in AliasingEffect - Rejected: Duplicates functionality, violates DRY
2. Subclass SampleRateReducer - Rejected: Overkill for constant change
3. Template parameter for max factor - Rejected: Unnecessarily complex

### Implementation Notes

```cpp
// In sample_rate_reducer.h
static constexpr float kMaxReductionFactor = 32.0f;  // Changed from 8.0f
```

Add tests for new factor range in sample_rate_reducer_test.cpp.

## 4. FrequencyShifter Fixed Configuration

### Background

The spec requires FrequencyShifter with fixed settings:
- Direction = Up
- Feedback = 0.0
- ModDepth = 0.0
- Mix = 1.0 (full wet for internal processing)

Only the shift amount (Hz) is exposed to users.

### Research Findings

FrequencyShifter API supports all required configuration:
```cpp
void setDirection(ShiftDirection dir) noexcept;  // ShiftDirection::Up
void setFeedback(float amount) noexcept;          // 0.0
void setModDepth(float hz) noexcept;              // 0.0
void setMix(float dryWet) noexcept;               // 1.0
void setShiftAmount(float hz) noexcept;           // User-controlled
```

The FrequencyShifter has approximately 5 samples of latency from the Hilbert transform.

### Design Decision

**Decision**: Configure FrequencyShifter with fixed values in `prepare()`, only expose `setFrequencyShift(float hz)` on AliasingEffect.

**Rationale**:
- Simplifies user-facing API
- Prevents user confusion from excessive parameters
- Internal frequency shifting is a means to an end (affecting aliasing patterns)

**Implementation Notes**:

```cpp
void prepare(double sampleRate, size_t maxBlockSize) noexcept {
    frequencyShifter_.prepare(sampleRate);
    frequencyShifter_.setDirection(ShiftDirection::Up);
    frequencyShifter_.setFeedback(0.0f);
    frequencyShifter_.setModDepth(0.0f);
    frequencyShifter_.setMix(1.0f);  // Full wet internally
    // Shift amount set via setFrequencyShift()
}
```

## 5. Processing Chain Order

### Background

The spec defines processing order (FR-028):
```
input -> band isolation -> frequency shift -> downsample (no AA) -> recombine with non-band signal -> mix with dry
```

### Research Findings

This order makes sense:
1. **Band isolation first**: Defines what content will be aliased
2. **Frequency shift before downsample**: Moves spectral content to affect aliasing patterns
3. **Downsample without anti-aliasing**: Creates the aliasing artifacts
4. **Recombine**: Preserves non-band content
5. **Mix**: User control over wet/dry balance

The frequency shift before downsample is key - it allows creative control over where harmonics fold to.

### Design Decision

**Decision**: Implement processing chain exactly as specified.

**Rationale**: The order is acoustically correct for the intended effect.

**Implementation Notes**:

```cpp
float process(float input) noexcept {
    // 1. Band isolation
    float bandSignal = bandLowpass_.process(bandHighpass_.process(input));
    float nonBandSignal = input - bandSignal;

    // 2. Frequency shift (applied to band only)
    float shiftedBand = frequencyShifter_.process(bandSignal);

    // 3. Downsample (no anti-aliasing)
    float aliasedBand = sampleRateReducer_.process(shiftedBand);

    // 4. Recombine
    float wetSignal = nonBandSignal + aliasedBand;

    // 5. Mix with dry
    float mixValue = mixSmoother_.process();
    return (1.0f - mixValue) * input + mixValue * wetSignal;
}
```

## 6. Parameter Smoothing Strategy

### Background

The spec requires 10ms smoothing time constant for all parameter changes to ensure click-free automation.

### Research Findings

OnePoleSmoother is designed for this exact use case:
```cpp
void configure(float smoothTimeMs, float sampleRate) noexcept;
```

Parameters requiring smoothing:
- Downsample factor
- Frequency shift amount
- Band low/high frequencies
- Mix

### Design Decision

**Decision**: Use OnePoleSmoother for all continuously-variable parameters with 10ms time constant.

**Rationale**:
- Proven pattern in codebase
- 10ms is fast enough for responsive feel, slow enough for click prevention

**Filter coefficient smoothing consideration**:
The band filters use BiquadCascade which does NOT have built-in coefficient smoothing. Rapid band frequency changes could cause clicks. Two options:
1. Use SmoothedBiquad (exists but doesn't have cascade version)
2. Smooth the frequency parameters before passing to filter recalculation

**Decision**: Smooth the frequency parameters, recalculate filter coefficients per-sample only when parameters are changing. When smoothers report `isComplete()`, skip recalculation.

**Implementation Notes**:

```cpp
static constexpr float kSmoothingTimeMs = 10.0f;

OnePoleSmoother downsampleSmoother_;
OnePoleSmoother shiftSmoother_;
OnePoleSmoother bandLowSmoother_;
OnePoleSmoother bandHighSmoother_;
OnePoleSmoother mixSmoother_;

void prepare(double sampleRate, size_t /*maxBlockSize*/) noexcept {
    float sr = static_cast<float>(sampleRate);
    downsampleSmoother_.configure(kSmoothingTimeMs, sr);
    shiftSmoother_.configure(kSmoothingTimeMs, sr);
    bandLowSmoother_.configure(kSmoothingTimeMs, sr);
    bandHighSmoother_.configure(kSmoothingTimeMs, sr);
    mixSmoother_.configure(kSmoothingTimeMs, sr);
}
```

## 7. NaN/Inf Handling

### Background

The spec requires (FR-025): "System MUST handle NaN/Inf inputs by resetting state and returning 0.0"

### Research Findings

The detail namespace in db_utils.h provides bit-manipulation-based checks:
```cpp
detail::isNaN(float x) noexcept;
detail::isInf(float x) noexcept;
```

These work correctly even with `-ffast-math` enabled (which the VST3 SDK uses).

### Design Decision

**Decision**: Check input at start of process(), reset all state and return 0.0f if invalid.

**Implementation Notes**:

```cpp
[[nodiscard]] float process(float input) noexcept {
    if (detail::isNaN(input) || detail::isInf(input)) {
        reset();
        return 0.0f;
    }
    // ... rest of processing
}
```

## 8. Latency Consideration

### Background

The spec requires documenting the latency from the FrequencyShifter's Hilbert transform.

### Research Findings

From frequency_shifter.h documentation:
```
/// @par Latency
/// Fixed 5-sample latency from Hilbert transform. Not compensated in output.
```

### Design Decision

**Decision**: Document approximately 5 samples latency in AliasingEffect header, do not compensate.

**Rationale**:
- 5 samples at 44100Hz = ~0.113ms - imperceptible
- Latency compensation would add complexity
- Consistent with FrequencyShifter behavior

## Summary of Key Decisions

| Topic | Decision | Rationale |
|-------|----------|-----------|
| Band filter topology | HP cascade + LP cascade | Reuses existing BiquadCascade<2>, provides 24dB/oct |
| Band recombination | Simple sum (no phase compensation) | Per spec - phase artifacts are acceptable |
| SampleRateReducer extension | Change kMaxReductionFactor to 32 | Single-line change, benefits future effects |
| FrequencyShifter config | Fixed internal settings | Simplifies user API |
| Processing order | Band -> Shift -> Downsample -> Recombine -> Mix | Per spec, acoustically correct |
| Parameter smoothing | OnePoleSmoother with 10ms | Proven pattern, click-free automation |
| Filter coefficient update | Recalculate when smoothers active | Smooth coefficient transitions |
| NaN/Inf handling | Check input, reset and return 0 | Per spec FR-025 |
| Latency | ~5 samples, not compensated | Imperceptible, consistent with dependencies |

## References

- [Cascading filters - EarLevel Engineering](https://www.earlevel.com/main/2016/09/29/cascading-filters/)
- [IIR Bandpass Filters Using Cascaded Biquads - DSPRelated](https://www.dsprelated.com/showarticle/1257.php)
- [Design IIR Filters Using Cascaded Biquads - DSPRelated](https://www.dsprelated.com/showarticle/1137.php)
- [Butterworth filter - Wikipedia](https://en.wikipedia.org/wiki/Butterworth_filter)
