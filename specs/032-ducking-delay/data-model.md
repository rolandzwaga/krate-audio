# Data Model: Ducking Delay

**Feature**: 032-ducking-delay
**Date**: 2025-12-26

## Components Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                     DuckingDelay (Layer 4)                      │
│  User-facing API for ducking delay functionality                │
├─────────────────────────────────────────────────────────────────┤
│  - setDuckingEnabled(bool)                                      │
│  - setThreshold(float dB)                                       │
│  - setDuckAmount(float percent)                                 │
│  - setAttackTime(float ms)                                      │
│  - setReleaseTime(float ms)                                     │
│  - setHoldTime(float ms)                                        │
│  - setDuckTarget(DuckTarget)                                    │
│  - setSidechainFilterEnabled(bool)                              │
│  - setSidechainFilterCutoff(float hz)                           │
│  - setDryWetMix(float percent)                                  │
│  - getGainReduction() const                                     │
└──────────────────────────┬──────────────────────────────────────┘
                           │ composes
                           v
┌─────────────────────────────────────────────────────────────────┐
│          FlexibleFeedbackNetwork (Layer 3) [REUSE]              │
│  Core delay + feedback loop with filter support                 │
├─────────────────────────────────────────────────────────────────┤
│  - setDelayTimeMs(float)                                        │
│  - setFeedbackAmount(float)                                     │
│  - setFilterEnabled(bool) / setFilterCutoff(float)              │
│  - process(left, right, numSamples, ctx)                        │
└──────────────────────────┬──────────────────────────────────────┘
                           │
      ┌────────────────────┴────────────────────┐
      │                                         │
      v                                         v
┌─────────────────────────┐       ┌─────────────────────────┐
│ DuckingProcessor (L2)   │       │ DuckingProcessor (L2)   │
│ [OUTPUT DUCKING]        │       │ [FEEDBACK DUCKING]      │
│ (optional per target)   │       │ (optional per target)   │
├─────────────────────────┤       ├─────────────────────────┤
│ setThreshold(dB)        │       │ setThreshold(dB)        │
│ setDepth(dB)            │       │ setDepth(dB)            │
│ setAttackTime(ms)       │       │ setAttackTime(ms)       │
│ setReleaseTime(ms)      │       │ setReleaseTime(ms)      │
│ setHoldTime(ms)         │       │ setHoldTime(ms)         │
│ setSidechainFilter*()   │       │ setSidechainFilter*()   │
│ processSample(main, sc) │       │ processSample(main, sc) │
└─────────────────────────┘       └─────────────────────────┘
```

## Class Definitions

### DuckTarget Enumeration

**Purpose**: Specifies which signal path to apply ducking to.

**Location**: `src/dsp/features/ducking_delay.h`

**Definition**:
```cpp
enum class DuckTarget : uint8_t {
    Output = 0,    ///< Duck the delay output before dry/wet mix (FR-011)
    Feedback = 1,  ///< Duck the feedback path only (FR-012)
    Both = 2       ///< Duck both output and feedback (FR-013)
};
```

### DuckingDelay

**Purpose**: Layer 4 user-facing feature that composes FlexibleFeedbackNetwork with DuckingProcessor(s).

**Location**: `src/dsp/features/ducking_delay.h`

**Interface**:
```cpp
class DuckingDelay {
public:
    // Constants
    static constexpr float kMinDelayMs = 10.0f;
    static constexpr float kMaxDelayMs = 5000.0f;
    static constexpr float kDefaultDelayMs = 500.0f;

    static constexpr float kMinThreshold = -60.0f;    // dB
    static constexpr float kMaxThreshold = 0.0f;      // dB
    static constexpr float kDefaultThreshold = -30.0f;

    static constexpr float kMinDuckAmount = 0.0f;     // % (no ducking)
    static constexpr float kMaxDuckAmount = 100.0f;   // % (full attenuation)
    static constexpr float kDefaultDuckAmount = 50.0f;

    static constexpr float kMinAttackMs = 0.1f;
    static constexpr float kMaxAttackMs = 100.0f;
    static constexpr float kDefaultAttackMs = 10.0f;

    static constexpr float kMinReleaseMs = 10.0f;
    static constexpr float kMaxReleaseMs = 2000.0f;
    static constexpr float kDefaultReleaseMs = 200.0f;

    static constexpr float kMinHoldMs = 0.0f;
    static constexpr float kMaxHoldMs = 500.0f;
    static constexpr float kDefaultHoldMs = 50.0f;

    static constexpr float kMinSidechainHz = 20.0f;
    static constexpr float kMaxSidechainHz = 500.0f;
    static constexpr float kDefaultSidechainHz = 80.0f;

    // Lifecycle
    void prepare(double sampleRate, std::size_t maxBlockSize) noexcept;
    void reset() noexcept;
    void snapParameters() noexcept;

    // Ducking control (FR-001 to FR-009)
    void setDuckingEnabled(bool enabled) noexcept;
    [[nodiscard]] bool isDuckingEnabled() const noexcept;
    void setThreshold(float dB) noexcept;
    void setDuckAmount(float percent) noexcept;
    void setAttackTime(float ms) noexcept;
    void setReleaseTime(float ms) noexcept;
    void setHoldTime(float ms) noexcept;

    // Target selection (FR-010 to FR-013)
    void setDuckTarget(DuckTarget target) noexcept;
    [[nodiscard]] DuckTarget getDuckTarget() const noexcept;

    // Sidechain filter (FR-014 to FR-016)
    void setSidechainFilterEnabled(bool enabled) noexcept;
    void setSidechainFilterCutoff(float hz) noexcept;

    // Delay configuration
    void setDelayTimeMs(float ms) noexcept;
    void setTimeMode(TimeMode mode) noexcept;
    void setNoteValue(NoteValue note, NoteModifier mod = NoteModifier::None) noexcept;
    void setFeedbackAmount(float percent) noexcept;

    // Filter (in feedback path)
    void setFilterEnabled(bool enabled) noexcept;
    void setFilterType(FilterType type) noexcept;
    void setFilterCutoff(float hz) noexcept;

    // Output (FR-020, FR-021)
    void setDryWetMix(float percent) noexcept;
    void setOutputGainDb(float dB) noexcept;

    // Metering (FR-022)
    [[nodiscard]] float getGainReduction() const noexcept;

    // Query
    [[nodiscard]] std::size_t getLatencySamples() const noexcept;

    // Processing
    void process(float* left, float* right, std::size_t numSamples,
                 const BlockContext& ctx) noexcept;

private:
    double sampleRate_ = 44100.0;
    std::size_t maxBlockSize_ = 512;
    bool prepared_ = false;

    // Core components
    FlexibleFeedbackNetwork feedbackNetwork_;
    DuckingProcessor outputDucker_;   // For Output/Both modes
    DuckingProcessor feedbackDucker_; // For Feedback/Both modes

    // Parameter smoothers
    OnePoleSmoother dryWetSmoother_;
    OnePoleSmoother outputGainSmoother_;
    OnePoleSmoother delaySmoother_;

    // Parameters
    bool duckingEnabled_ = true;
    DuckTarget duckTarget_ = DuckTarget::Output;
    float delayTimeMs_ = kDefaultDelayMs;
    float dryWetMix_ = 50.0f;
    float outputGainDb_ = 0.0f;
    float feedbackAmount_ = 50.0f;

    // Ducking parameters (shared by both processors)
    float thresholdDb_ = kDefaultThreshold;
    float duckAmountPercent_ = kDefaultDuckAmount;
    float attackTimeMs_ = kDefaultAttackMs;
    float releaseTimeMs_ = kDefaultReleaseMs;
    float holdTimeMs_ = kDefaultHoldMs;
    bool sidechainFilterEnabled_ = false;
    float sidechainFilterCutoffHz_ = kDefaultSidechainHz;

    // Scratch buffers
    std::vector<float> dryBufferL_;
    std::vector<float> dryBufferR_;
    std::vector<float> inputCopyL_;  // For sidechain (input copy)
    std::vector<float> inputCopyR_;
    std::vector<float> unduckedL_;   // For Feedback-Only mode
    std::vector<float> unduckedR_;

    // Internal helpers
    void updateDuckingProcessors() noexcept;
    float percentToDepth(float percent) const noexcept;
};
```

## Parameter Mapping

| User Parameter | Range | Internal Representation | Notes |
|----------------|-------|-------------------------|-------|
| Ducking Enable | on/off | bool | Master ducking bypass |
| Threshold | -60 to 0 dB | float dB | Direct to DuckingProcessor |
| Duck Amount | 0-100% | float dB (-48 to 0) | Converted: `depth = -48 * (percent/100)` |
| Attack Time | 0.1-100 ms | float ms | Clamped from DP's 0.1-500ms |
| Release Time | 10-2000 ms | float ms | Clamped from DP's 1-5000ms |
| Hold Time | 0-500 ms | float ms | Clamped from DP's 0-1000ms |
| Duck Target | Output/Feedback/Both | DuckTarget enum | Determines which processor(s) active |
| SC Filter On | on/off | bool | Via DuckingProcessor |
| SC Filter Cutoff | 20-500 Hz | float Hz | Via DuckingProcessor |
| Delay Time | 10-5000 ms | float ms | Via FFN |
| Feedback | 0-120% | float 0-1.2 | Via FFN |
| Filter On | on/off | bool | Via FFN |
| Filter Type | LP/HP/BP | FilterType enum | Via FFN |
| Filter Cutoff | 20-20000 Hz | float Hz | Via FFN |
| Dry/Wet | 0-100% | float 0-1 | Output mix |
| Output Gain | -inf to +6 dB | float dB | Via dbToGain() |

## Signal Flow

### Output Only Mode (FR-011)
```
Input ─────┬────────────► [FlexibleFeedbackNetwork] ──► [DuckingProcessor] ──► Dry/Wet Mix ──► Output
           │                                                    ▲
           │                                                    │
           └──────────────────────────────────────────► (sidechain)
```

### Feedback Only Mode (FR-012)
```
Input ─────┬────────────► [FlexibleFeedbackNetwork] ──┬──► Copy to unducked ──► Dry/Wet Mix ──► Output
           │                                          │
           │                                          ▼
           │                               [DuckingProcessor]
           │                                          │
           │                                          ▼
           │                               (feeds back into FFN)
           │                                          ▲
           └──────────────────────────────────────────┴──► (sidechain)
```

### Both Mode (FR-013)
```
Input ─────┬────────────► [FlexibleFeedbackNetwork] ──► [Output Ducker] ──► Dry/Wet Mix ──► Output
           │                       ▲                          ▲
           │                       │                          │
           │               [Feedback Ducker]                  │
           │                       ▲                          │
           │                       │                          │
           └───────────────────────┴──────────────────────────┴──► (sidechain)
```

## State Machine

The DuckingProcessor already implements the ducking state machine:

```
                    Sidechain >= Threshold
    ┌─────────────────────────────────────────┐
    │                                         v
┌───────────┐                           ┌───────────┐
│   Idle    │                           │  Ducking  │
│           │                           │           │
│ GR = 0dB  │                           │ GR = depth│
└───────────┘                           └───────────┘
    ^                                         │
    │                                         │ Sidechain < Threshold
    │         Hold Time                       v
    │      ┌──────────────┐            ┌───────────┐
    └──────┤   Holding    │◄───────────┤           │
           │              │            │  (if hold>0)
           │ GR = depth   │            └───────────┘
           └──────────────┘

Transitions smoothed via gainSmoother_ (5ms)
```

## Test Categories

1. **Basic Ducking**: Enable/disable, threshold triggering, amount verification
2. **Target Selection**: Verify Output, Feedback, Both modes work correctly
3. **Envelope Timing**: Attack, release, hold time accuracy
4. **Sidechain Filter**: HP filter effect on ducking trigger
5. **Integration**: Works with delay modes, feedback, filter
6. **Metering**: Gain reduction meter accuracy
7. **Edge Cases**: Very fast attack, DC offset, empty buffer
8. **Performance**: CPU overhead less than 1%
