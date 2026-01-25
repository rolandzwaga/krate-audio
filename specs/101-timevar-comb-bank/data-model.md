# Data Model: TimeVaryingCombBank

**Branch**: `101-timevar-comb-bank` | **Date**: 2026-01-25

---

## 1. Entities

### 1.1 CombChannel (Internal Struct)

Internal structure holding per-comb state. Not exposed in public API.

```cpp
/// @brief Internal per-comb state holder.
/// Aggregates the comb filter with its modulation and smoothing components.
struct CombChannel {
    // Core DSP
    FeedbackComb comb;              ///< The actual comb filter (Layer 1)
    LFO lfo;                        ///< Modulation oscillator (Layer 1)
    Xorshift32 rng;                 ///< Random drift generator (Layer 0)

    // Parameter smoothers (Layer 1)
    OnePoleSmoother delaySmoother;    ///< 20ms smoothing for delay time
    OnePoleSmoother feedbackSmoother; ///< 10ms smoothing for feedback
    OnePoleSmoother dampingSmoother;  ///< 10ms smoothing for damping
    OnePoleSmoother gainSmoother;     ///< 5ms smoothing for gain

    // Parameter targets
    float baseDelayMs = 10.0f;      ///< Base delay before modulation [1, maxDelayMs]
    float feedbackTarget = 0.5f;    ///< Target feedback [-0.9999, 0.9999]
    float dampingTarget = 0.0f;     ///< Target damping [0.0, 1.0]
    float gainDb = 0.0f;            ///< Gain in dB
    float gainLinear = 1.0f;        ///< Precomputed linear gain

    // Stereo
    float pan = 0.0f;               ///< Pan position [-1, 1]
    float panLeftGain = 0.707f;     ///< Precomputed left pan gain
    float panRightGain = 0.707f;    ///< Precomputed right pan gain

    // LFO phase
    float lfoPhaseOffset = 0.0f;    ///< Phase offset in degrees [0, 360)
};
```

**Relationships**:
- Owned by TimeVaryingCombBank (composition)
- One CombChannel per active comb filter

**Validation Rules**:
- baseDelayMs: [1.0, maxDelayMs_] - clamped to configured maximum
- feedbackTarget: [-0.9999, 0.9999] - per kMinCombCoeff/kMaxCombCoeff
- dampingTarget: [0.0, 1.0]
- gainDb: unlimited input, linear gain computed via dbToGain()
- pan: [-1.0, 1.0]

---

### 1.2 Tuning (Enum)

Controls how delay times are automatically calculated.

```cpp
/// @brief Tuning mode for automatic delay time calculation.
enum class Tuning : uint8_t {
    Harmonic,    ///< f[n] = fundamental * (n+1) - musical harmonics
    Inharmonic,  ///< f[n] = fundamental * sqrt(1 + n*spread) - bell-like
    Custom       ///< Manual per-comb delay times via setCombDelay()
};
```

**State Transitions**:
```
Any Mode --setTuningMode()--> Any Mode
    |
    +-- If Harmonic or Inharmonic: recalculate all delay times
    +-- If Custom: preserve current delay times (manual control)
```

---

### 1.3 TimeVaryingCombBank (Main Class)

The primary system component aggregating up to 8 modulated comb filters.

```cpp
/// @brief Bank of up to 8 comb filters with independently modulated delay times.
///
/// Creates evolving metallic and resonant textures by modulating each comb
/// filter's delay time with independent LFOs and optional random drift.
///
/// @par Layer 3 Architecture
/// Composes Layer 0-1 components: FeedbackComb, LFO, OnePoleSmoother, Xorshift32
///
/// @par Constitution Compliance
/// - Principle II: Real-time safe (noexcept, no allocations in process)
/// - Principle IX: Layer 3 system component
/// - Principle X: Linear interpolation for modulated delays (FR-018)
class TimeVaryingCombBank {
public:
    // Constants
    static constexpr size_t kMaxCombs = 8;
    static constexpr float kMinFundamental = 20.0f;   ///< Hz (50ms max delay)
    static constexpr float kMaxFundamental = 1000.0f; ///< Hz
    static constexpr float kMinModRate = 0.01f;       ///< Hz
    static constexpr float kMaxModRate = 20.0f;       ///< Hz
    static constexpr float kMinModDepth = 0.0f;       ///< Percent
    static constexpr float kMaxModDepth = 100.0f;     ///< Percent

private:
    // Per-comb state
    std::array<CombChannel, kMaxCombs> channels_;

    // Global parameters
    size_t numCombs_ = 4;              ///< Active comb count [1, kMaxCombs]
    Tuning tuningMode_ = Tuning::Harmonic;
    float fundamental_ = 100.0f;       ///< Hz for tuned modes
    float spread_ = 0.0f;              ///< Inharmonic spread [0, 1]
    float modRate_ = 1.0f;             ///< LFO rate Hz [0.01, 20]
    float modDepth_ = 0.0f;            ///< Modulation depth [0, 1] (0-100%)
    float modPhaseSpread_ = 0.0f;      ///< Phase spread degrees [0, 360)
    float randomModAmount_ = 0.0f;     ///< Random drift amount [0, 1]
    float stereoSpread_ = 0.0f;        ///< Stereo pan spread [0, 1]

    // Runtime state
    double sampleRate_ = 44100.0;
    float maxDelayMs_ = 50.0f;
    bool prepared_ = false;
};
```

**Relationships**:
- Contains array of kMaxCombs CombChannel instances (composition)
- No external references (self-contained system)

---

## 2. Field Specifications

### 2.1 TimeVaryingCombBank Fields

| Field | Type | Default | Range | Description |
|-------|------|---------|-------|-------------|
| numCombs_ | size_t | 4 | [1, 8] | Number of active comb filters |
| tuningMode_ | Tuning | Harmonic | enum | Auto-tuning mode |
| fundamental_ | float | 100.0 | [20, 1000] Hz | Base frequency for tuned modes |
| spread_ | float | 0.0 | [0, 1] | Inharmonic spread factor |
| modRate_ | float | 1.0 | [0.01, 20] Hz | Global LFO rate |
| modDepth_ | float | 0.0 | [0, 1] | Modulation depth (fraction) |
| modPhaseSpread_ | float | 0.0 | [0, 360) deg | Phase offset between adjacent combs |
| randomModAmount_ | float | 0.0 | [0, 1] | Random drift strength |
| stereoSpread_ | float | 0.0 | [0, 1] | Pan distribution width |
| sampleRate_ | double | 44100 | [44100, 192000] | Current sample rate |
| maxDelayMs_ | float | 50.0 | [1, 50] | Maximum delay time |
| prepared_ | bool | false | - | Lifecycle flag |

### 2.2 CombChannel Fields

| Field | Type | Default | Range | Description |
|-------|------|---------|-------|-------------|
| baseDelayMs | float | 10.0 | [1, maxDelayMs] | Unmodulated delay time |
| feedbackTarget | float | 0.5 | [-0.9999, 0.9999] | Feedback amount |
| dampingTarget | float | 0.0 | [0, 1] | HF damping (0=bright) |
| gainDb | float | 0.0 | unlimited | Output gain in dB |
| gainLinear | float | 1.0 | [0, inf) | Computed linear gain |
| pan | float | 0.0 | [-1, 1] | Stereo position |
| panLeftGain | float | 0.707 | [0, 1] | Computed L gain |
| panRightGain | float | 0.707 | [0, 1] | Computed R gain |
| lfoPhaseOffset | float | 0.0 | [0, 360) | LFO phase in degrees |

---

## 3. Smoothing Time Constants (FR-019)

Per spec clarification:

| Parameter | Time Constant | Reason |
|-----------|---------------|--------|
| Gain (gainSmoother) | 5ms | Fast response, low audible artifacts |
| Delay Time (delaySmoother) | 20ms | Avoid pitch jumps, tape-like transitions |
| Feedback (feedbackSmoother) | 10ms | Balance responsiveness and stability |
| Damping (dampingSmoother) | 10ms | Match feedback behavior |

---

## 4. State Transitions

### 4.1 Lifecycle States

```
[Uninitialized] --prepare()--> [Ready] --process()/processStereo()--> [Processing]
                                  ^                                        |
                                  |                                        |
                                  +------------- reset() -----------------+
```

### 4.2 Tuning Mode State Machine

```
                    +------ setFundamental() ------+
                    |                              |
                    v                              v
[Custom] <---> [Harmonic] <---> [Inharmonic]
                    ^                              ^
                    |                              |
                    +------ setSpread() -----------+

Transitions:
- setTuningMode(Harmonic/Inharmonic): Recalculates all comb delay times
- setTuningMode(Custom): Preserves current delay times
- setFundamental(): Only affects Harmonic/Inharmonic modes
- setSpread(): Only affects Inharmonic mode
- setCombDelay(): Implicitly switches to Custom mode
```

---

## 5. Invariants

1. **numCombs_ in [1, kMaxCombs]**: Always at least 1 active comb, never more than 8
2. **prepared_ == true before process()**: Calling process() when unprepared returns 0
3. **Comb delay within bounds**: baseDelayMs always clamped to [1, maxDelayMs_]
4. **Feedback stability**: feedbackTarget always in [-0.9999, 0.9999]
5. **Smooth transitions**: All modulatable parameters go through smoothers
6. **No allocation in process()**: All memory allocated in prepare()

---

## 6. Computed Values

### 6.1 Tuned Delay Calculation

```cpp
float computeHarmonicDelay(size_t combIndex, float fundamental) {
    // f[n] = fundamental * (n + 1)
    float freq = fundamental * static_cast<float>(combIndex + 1);
    return 1000.0f / freq;  // ms
}

float computeInharmonicDelay(size_t combIndex, float fundamental, float spread) {
    // f[n] = fundamental * sqrt(1 + n * spread)
    float freq = fundamental * std::sqrt(1.0f + static_cast<float>(combIndex) * spread);
    return 1000.0f / freq;  // ms
}
```

### 6.2 Modulated Delay Calculation

```cpp
float computeModulatedDelay(
    float baseDelayMs,
    float lfoOutput,      // [-1, 1]
    float modDepth,       // [0, 1]
    float randomDrift     // from rng.nextFloat() * randomAmount
) {
    // LFO modulation: +/- modDepth percent of base
    float lfoMod = baseDelayMs * modDepth * lfoOutput;

    // Random drift: additive, scaled by modDepth and randomAmount
    float drift = randomDrift * modDepth * baseDelayMs;

    return baseDelayMs + lfoMod + drift;
}
```

### 6.3 Pan Gains (Equal Power)

```cpp
void computePanGains(float pan, float& leftGain, float& rightGain) {
    // pan in [-1, 1], where -1 = full left, +1 = full right
    // Equal power: use sin/cos for constant power
    constexpr float kPi = 3.14159265359f;
    float angle = (pan + 1.0f) * 0.25f * kPi;  // [0, pi/2]
    leftGain = std::cos(angle);
    rightGain = std::sin(angle);
}
```

---

## 7. Memory Layout

```
TimeVaryingCombBank (~320KB at 192kHz, 50ms max delay)
|
+-- channels_[8] (CombChannel array)
    |
    +-- channels_[0]
    |   +-- comb (FeedbackComb ~38KB delay buffer)
    |   +-- lfo (LFO ~32KB wavetables, shared reference)
    |   +-- rng (Xorshift32, 4 bytes)
    |   +-- delaySmoother (OnePoleSmoother, ~24 bytes)
    |   +-- feedbackSmoother (~24 bytes)
    |   +-- dampingSmoother (~24 bytes)
    |   +-- gainSmoother (~24 bytes)
    |   +-- parameters (~32 bytes)
    |
    +-- channels_[1..7] (same structure)
```

---

## 8. Validation Rules Summary

| Parameter | Validation | Action on Invalid |
|-----------|------------|-------------------|
| numCombs | [1, 8] | Clamp |
| combIndex | [0, numCombs-1] | Early return (no-op) |
| fundamental | [20, 1000] Hz | Clamp |
| spread | [0, 1] | Clamp |
| modRate | [0.01, 20] Hz | Clamp |
| modDepth | [0, 100] % -> [0, 1] | Clamp |
| phaseSpread | [0, 360) | Modulo wrap |
| randomAmount | [0, 1] | Clamp |
| stereoSpread | [0, 1] | Clamp |
| feedback | [-0.9999, 0.9999] | Clamp |
| damping | [0, 1] | Clamp |
| delayMs | [1, maxDelayMs] | Clamp |
| gainDb | Any float | Convert to linear |
| NaN/Inf input | Detected | Reset affected comb, return 0 |
