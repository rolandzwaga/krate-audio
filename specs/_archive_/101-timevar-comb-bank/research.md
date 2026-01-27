# Research: TimeVaryingCombBank

**Branch**: `101-timevar-comb-bank` | **Date**: 2026-01-25

## Research Summary

This document consolidates research findings for implementing a TimeVaryingCombBank - a bank of up to 8 comb filters with independently modulated delay times for evolving metallic/resonant textures.

---

## 1. Existing Component Analysis

### 1.1 FeedbackComb (Layer 1 Primitive)

**Location**: `dsp/include/krate/dsp/primitives/comb_filter.h`

**API Verified**:
```cpp
class FeedbackComb {
    void prepare(double sampleRate, float maxDelaySeconds) noexcept;
    void reset() noexcept;
    void setFeedback(float g) noexcept;        // [-0.9999, 0.9999]
    [[nodiscard]] float getFeedback() const noexcept;
    void setDamping(float d) noexcept;         // [0.0, 1.0] (0=bright, 1=dark)
    [[nodiscard]] float getDamping() const noexcept;
    void setDelaySamples(float samples) noexcept;
    void setDelayMs(float ms) noexcept;
    [[nodiscard]] float getDelaySamples() const noexcept;
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;
};
```

**Key Findings**:
- Implements `y[n] = x[n] + g * LP(y[n-D])` with one-pole lowpass damping
- Uses `readLinear()` for delay interpolation (NOT allpass - correct for modulation per FR-018)
- Has built-in NaN/Inf handling with reset
- Feedback clamped to `kMinCombCoeff`/`kMaxCombCoeff` (+-0.9999)
- Damping range [0.0, 1.0] where 0=bright, 1=dark

**Decision**: Direct reuse. The FeedbackComb already uses linear interpolation, which is exactly what FR-018 requires for modulated delays.

### 1.2 LFO (Layer 1 Primitive)

**Location**: `dsp/include/krate/dsp/primitives/lfo.h`

**API Verified**:
```cpp
class LFO {
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;
    [[nodiscard]] float process() noexcept;
    void setWaveform(Waveform waveform) noexcept;
    void setFrequency(float hz) noexcept;      // [0.01, 20.0] Hz
    void setPhaseOffset(float degrees) noexcept;
    void setTempoSync(bool enabled) noexcept;
    void setTempo(float bpm) noexcept;
    void setRetriggerEnabled(bool enabled) noexcept;
    void retrigger() noexcept;
    [[nodiscard]] Waveform waveform() const noexcept;
    [[nodiscard]] float frequency() const noexcept;
    [[nodiscard]] float phaseOffset() const noexcept;
};
```

**Key Findings**:
- Wavetable-based with sine, triangle, sawtooth, square, sample-hold, smooth-random
- Frequency range [0.01, 20.0] Hz - matches FR-009 exactly
- Phase offset in degrees (0-360)
- Has smooth crossfade for waveform changes
- Output range is [-1, 1] (bipolar)

**Decision**: Direct reuse. One LFO per comb filter for independent modulation with phase offsets.

### 1.3 OnePoleSmoother (Layer 1 Primitive)

**Location**: `dsp/include/krate/dsp/primitives/smoother.h`

**API Verified**:
```cpp
class OnePoleSmoother {
    OnePoleSmoother() noexcept;
    explicit OnePoleSmoother(float initialValue) noexcept;
    void configure(float smoothTimeMs, float sampleRate) noexcept;
    void setTarget(float target) noexcept;
    [[nodiscard]] float getTarget() const noexcept;
    [[nodiscard]] float getCurrentValue() const noexcept;
    [[nodiscard]] float process() noexcept;
    void processBlock(float* output, size_t numSamples) noexcept;
    [[nodiscard]] bool isComplete() const noexcept;
    void snapToTarget() noexcept;
    void snapTo(float value) noexcept;
    void reset() noexcept;
    void setSampleRate(float sampleRate) noexcept;
};
```

**Key Findings**:
- Uses exponential approach (first-order IIR)
- Time constant configurable via `configure(smoothTimeMs, sampleRate)`
- Has NaN/Inf protection in `setTarget()`
- Denormal flushing built-in

**Decision**: Direct reuse. Per FR-019, need different time constants:
- 5ms for gain parameters
- 20ms for delay time
- 10ms for feedback and damping

### 1.4 Xorshift32 (Layer 0 Core)

**Location**: `dsp/include/krate/dsp/core/random.h`

**API Verified**:
```cpp
class Xorshift32 {
    explicit constexpr Xorshift32(uint32_t seedValue = 1) noexcept;
    [[nodiscard]] constexpr uint32_t next() noexcept;
    [[nodiscard]] constexpr float nextFloat() noexcept;    // [-1.0, 1.0]
    [[nodiscard]] constexpr float nextUnipolar() noexcept; // [0.0, 1.0]
    constexpr void seed(uint32_t seedValue) noexcept;
    [[nodiscard]] constexpr uint32_t state() const noexcept;
};
```

**Key Findings**:
- Period of 2^32-1
- Default seed prevents zero-state lock
- All methods constexpr and noexcept
- Real-time safe

**Decision**: Direct reuse. One per comb for random drift modulation (FR-011).

### 1.5 Reference Architecture: FilterFeedbackMatrix (Layer 3)

**Location**: `dsp/include/krate/dsp/systems/filter_feedback_matrix.h`

**Pattern Analysis**:
- Template parameter for array size (compile-time N=2,3,4)
- Per-element configuration via indexed setters
- Smoothers for all modulatable parameters
- NaN/Inf checking at process() entry
- Denormal flushing at output
- `prepare()` allocates, `process()` is allocation-free

**Decision**: Follow this pattern but with runtime-configurable comb count (1-8) per FR-001.

---

## 2. Tuning Calculation Research

### 2.1 Harmonic Tuning (FR-007)

**Formula**: `f[n] = fundamental * (n + 1)` where n is 0-based comb index

**Delay Time Calculation**: `delayMs = 1000.0 / f[n]`

**Example** (100 Hz fundamental, 4 combs):
| Comb | n | Frequency | Delay Time |
|------|---|-----------|------------|
| 0 | 0 | 100 Hz | 10.0 ms |
| 1 | 1 | 200 Hz | 5.0 ms |
| 2 | 2 | 300 Hz | 3.33 ms |
| 3 | 3 | 400 Hz | 2.5 ms |

### 2.2 Inharmonic Tuning (FR-007)

**Formula**: `f[n] = fundamental * sqrt(1 + n * spread)` where:
- n is 0-based comb index
- spread controls inharmonicity (0 = harmonic, 1 = bell-like)

**Example** (100 Hz fundamental, spread=1.0, 4 combs):
| Comb | n | sqrt(1 + n) | Frequency | Delay Time |
|------|---|-------------|-----------|------------|
| 0 | 0 | 1.0 | 100 Hz | 10.0 ms |
| 1 | 1 | 1.414 | 141.4 Hz | 7.07 ms |
| 2 | 2 | 1.732 | 173.2 Hz | 5.77 ms |
| 3 | 3 | 2.0 | 200 Hz | 5.0 ms |

**Decision**: Implement both as inline functions. Consider extracting to Layer 0 if reused by shimmer/Karplus-Strong later.

---

## 3. Stereo Pan Distribution Research

### 3.1 Algorithm for setStereoSpread()

**Formula**: For comb index i with numCombs total, at spread s:
```cpp
// Pan position in [-1, 1]
float pan = (numCombs == 1) ? 0.0f : (2.0f * i / (numCombs - 1) - 1.0f) * spread;
```

**Example** (4 combs, spread=1.0):
| Comb | Index | Pan |
|------|-------|-----|
| 0 | 0 | -1.0 (L) |
| 1 | 1 | -0.33 (L-C) |
| 2 | 2 | +0.33 (R-C) |
| 3 | 3 | +1.0 (R) |

### 3.2 Equal Power Panning

Use constant-power pan law:
```cpp
float leftGain = std::cos((pan + 1.0f) * 0.25f * pi);
float rightGain = std::sin((pan + 1.0f) * 0.25f * pi);
```

**Decision**: Store pan position per comb, compute L/R gains in processStereo().

---

## 4. Modulation Depth Application

### 4.1 Delay Time Modulation (FR-009)

**Formula**:
```cpp
float modulatedDelayMs = baseDelayMs * (1.0f + lfoOutput * modDepth);
```

Where:
- `lfoOutput` is in [-1, 1]
- `modDepth` is in [0, 1] (0% to 100%)
- Result: delay varies by +/- modDepth percent of base

**Example** (base=10ms, depth=0.1):
- LFO at +1: 10ms * 1.1 = 11ms
- LFO at 0: 10ms * 1.0 = 10ms
- LFO at -1: 10ms * 0.9 = 9ms

### 4.2 Random Drift Addition (FR-011)

**Formula**:
```cpp
float drift = xorshift.nextFloat() * randomAmount * modDepth * baseDelayMs;
float finalDelayMs = modulatedDelayMs + drift;
```

**Decision**: Apply random drift as additive offset scaled by modulation depth.

---

## 5. NaN/Inf Handling Strategy (FR-020)

### 5.1 Per-Comb Reset Approach

**Specification**: "Check each comb's output after processing. If a comb produces NaN/Inf, reset that comb's state and substitute 0.0f for that comb's contribution."

**Implementation**:
```cpp
float combOutput = comb.process(input);
if (detail::isNaN(combOutput) || detail::isInf(combOutput)) {
    comb.reset();
    // Also reset associated smoothers
    delaySmoother.snapTo(delaySmoother.getTarget());
    feedbackSmoother.snapTo(feedbackSmoother.getTarget());
    dampingSmoother.snapTo(dampingSmoother.getTarget());
    gainSmoother.snapTo(gainSmoother.getTarget());
    combOutput = 0.0f;
}
```

**Decision**: Check output per comb, reset only the offending comb and its state.

---

## 6. Memory and Performance Considerations

### 6.1 Memory Footprint

Per comb at 50ms max delay, 192kHz:
- DelayLine buffer: 192000 * 0.05 * 4 bytes = ~38KB
- 8 combs: ~304KB total for delay buffers

Additional per comb:
- LFO wavetables: 4 * 2048 * 4 bytes = 32KB (shared)
- Smoothers: 4 * ~24 bytes = 96 bytes
- Xorshift32: 4 bytes

**Total estimate**: ~400KB maximum for full 8-comb configuration

### 6.2 CPU Budget

Per SC-003: "Processing 1 second at 44.1kHz with 8 combs in <10ms"

At 44.1kHz:
- 44100 samples per second
- Target: <10ms wall time
- Budget: ~227ns per sample (all 8 combs)
- Per comb: ~28ns per sample

This is achievable with:
- Single FeedbackComb::process() per comb
- One LFO::process() per comb
- 4 smoother::process() per comb
- Pan calculation (L/R multiply)

---

## 7. API Design Decisions

### 7.1 Class Structure

```cpp
struct CombChannel {
    FeedbackComb comb;
    LFO lfo;
    Xorshift32 rng;
    OnePoleSmoother delaySmoother;    // 20ms
    OnePoleSmoother feedbackSmoother; // 10ms
    OnePoleSmoother dampingSmoother;  // 10ms
    OnePoleSmoother gainSmoother;     // 5ms
    float baseDelayMs = 10.0f;
    float pan = 0.0f;
    float gainLinear = 1.0f;
};

enum class Tuning : uint8_t { Harmonic, Inharmonic, Custom };

class TimeVaryingCombBank {
    static constexpr size_t kMaxCombs = 8;
    // ... methods per spec
};
```

### 7.2 Method Signatures

Based on FR requirements:
```cpp
// Configuration
void prepare(double sampleRate, float maxDelayMs) noexcept;
void reset() noexcept;
void setNumCombs(size_t count) noexcept;                     // FR-001

// Per-comb parameters
void setCombDelay(size_t index, float ms) noexcept;          // FR-002
void setCombFeedback(size_t index, float amount) noexcept;   // FR-003
void setCombDamping(size_t index, float amount) noexcept;    // FR-004
void setCombGain(size_t index, float dB) noexcept;           // FR-005

// Tuning
void setTuningMode(Tuning mode) noexcept;                    // FR-006
void setFundamental(float hz) noexcept;                      // FR-007
void setSpread(float amount) noexcept;                       // FR-008

// Modulation
void setModRate(float hz) noexcept;                          // FR-009
void setModDepth(float percent) noexcept;                    // FR-009
void setModPhaseSpread(float degrees) noexcept;              // FR-010
void setRandomModulation(float amount) noexcept;             // FR-011

// Stereo
void setStereoSpread(float amount) noexcept;                 // FR-012

// Processing
[[nodiscard]] float process(float input) noexcept;           // FR-013
void processStereo(float& left, float& right) noexcept;      // FR-014
```

---

## 8. Unresolved Items

None. All clarifications from the spec session have been addressed:

| Item | Resolution |
|------|------------|
| Memory footprint | No hard limit - allocate in prepare() |
| Inharmonic formula | sqrt(1 + n * spread) from bell physics |
| NaN/Inf handling | Per-comb reset and substitute 0 |
| Smoother time constants | 5ms gain, 20ms delay, 10ms feedback/damping |
| Mod rate/depth ranges | [0.01, 20] Hz, [0, 100]% |

---

## 9. Potential Layer 0 Extractions

### Candidates Identified:

1. **Tuning calculation functions** - Could be useful for Karplus-Strong, shimmer
   - `harmonicFrequency(fundamental, harmonic)`
   - `inharmonicFrequency(fundamental, index, spread)`
   - **Decision**: Keep local for now. Extract after second use case appears.

2. **Pan calculation** - Already exists as pattern in other systems
   - **Decision**: Use inline in this class. Consider extraction later.

---

## 10. References

- FeedbackComb implementation: `dsp/include/krate/dsp/primitives/comb_filter.h`
- LFO implementation: `dsp/include/krate/dsp/primitives/lfo.h`
- OnePoleSmoother: `dsp/include/krate/dsp/primitives/smoother.h`
- Xorshift32: `dsp/include/krate/dsp/core/random.h`
- FilterFeedbackMatrix pattern: `dsp/include/krate/dsp/systems/filter_feedback_matrix.h`
- FeedbackNetwork pattern: `dsp/include/krate/dsp/systems/feedback_network.h`
- Constitution: `.specify/memory/constitution.md`
