# Data Model: Phaser Effect Processor

**Spec**: 079-phaser | **Layer**: 2 (Processors) | **Location**: `dsp/include/krate/dsp/processors/phaser.h`

## Entity Overview

| Entity | Type | Purpose |
|--------|------|---------|
| Phaser | Class | Main processor - cascaded allpass stages with LFO modulation |
| (Allpass1Pole) | Dependency | First-order allpass filter stage (Layer 1) |
| (LFO) | Dependency | Low frequency oscillator for modulation (Layer 1) |
| (OnePoleSmoother) | Dependency | Parameter smoothing (Layer 1) |

## Phaser Class

### Constants

```cpp
// Maximum number of allpass stages (12 stages = 6 notches)
static constexpr int kMaxStages = 12;

// Valid stage counts (even numbers only)
static constexpr int kValidStageCounts[] = {2, 4, 6, 8, 10, 12};

// Parameter ranges
static constexpr float kMinRate = 0.01f;          // Hz (LFO minimum)
static constexpr float kMaxRate = 20.0f;          // Hz (LFO maximum)
static constexpr float kMinDepth = 0.0f;          // No modulation
static constexpr float kMaxDepth = 1.0f;          // Full range modulation
static constexpr float kMinFeedback = -1.0f;      // Maximum negative feedback
static constexpr float kMaxFeedback = 1.0f;       // Maximum positive feedback
static constexpr float kMinMix = 0.0f;            // Dry only
static constexpr float kMaxMix = 1.0f;            // Wet only
static constexpr float kMinCenterFreq = 100.0f;   // Hz
static constexpr float kMaxCenterFreq = 10000.0f; // Hz
static constexpr float kMinStereoSpread = 0.0f;   // Degrees (mono)
static constexpr float kMaxStereoSpread = 360.0f; // Degrees

// Default values
static constexpr int kDefaultStages = 4;
static constexpr float kDefaultRate = 0.5f;       // Hz
static constexpr float kDefaultDepth = 0.5f;
static constexpr float kDefaultFeedback = 0.0f;
static constexpr float kDefaultMix = 0.5f;
static constexpr float kDefaultCenterFreq = 1000.0f;  // Hz
static constexpr float kDefaultStereoSpread = 0.0f;   // Degrees

// Smoothing time for parameter changes
static constexpr float kSmoothingTimeMs = 5.0f;

// Minimum frequency after depth calculation (prevents DC)
static constexpr float kMinSweepFreq = 20.0f;     // Hz
```

### Member Variables

#### Composed Components

| Member | Type | Description |
|--------|------|-------------|
| stagesL_ | std::array<Allpass1Pole, kMaxStages> | Left channel allpass stages |
| stagesR_ | std::array<Allpass1Pole, kMaxStages> | Right channel allpass stages |
| lfoL_ | LFO | Left channel modulation LFO |
| lfoR_ | LFO | Right channel modulation LFO (with phase offset) |

#### Parameter Smoothers

| Member | Type | Smoothed Parameter |
|--------|------|--------------------|
| rateSmoother_ | OnePoleSmoother | LFO rate (Hz) |
| depthSmoother_ | OnePoleSmoother | Modulation depth (0-1) |
| feedbackSmoother_ | OnePoleSmoother | Feedback amount (-1 to +1) |
| mixSmoother_ | OnePoleSmoother | Dry/wet mix (0-1) |
| centerFreqSmoother_ | OnePoleSmoother | Center frequency (Hz) |

#### State Variables

| Member | Type | Description |
|--------|------|-------------|
| feedbackStateL_ | float | Left channel feedback sample |
| feedbackStateR_ | float | Right channel feedback sample |

#### Configuration

| Member | Type | Default | Description |
|--------|------|---------|-------------|
| sampleRate_ | double | 44100.0 | Current sample rate |
| numStages_ | int | 4 | Number of active allpass stages |
| centerFrequency_ | float | 1000.0f | Center of sweep range (Hz) |
| stereoSpread_ | float | 0.0f | LFO phase offset for R channel (degrees) |
| rate_ | float | 0.5f | LFO rate (Hz) |
| depth_ | float | 0.5f | Modulation depth (0-1) |
| feedback_ | float | 0.0f | Feedback amount (-1 to +1) |
| mix_ | float | 0.5f | Dry/wet mix (0-1) |
| waveform_ | Waveform | Sine | LFO waveform |
| tempoSync_ | bool | false | Tempo sync enabled |
| noteValue_ | NoteValue | Quarter | Tempo sync note value |
| noteModifier_ | NoteModifier | None | Tempo sync modifier |
| tempo_ | float | 120.0f | Current tempo (BPM) |
| prepared_ | bool | false | Whether prepare() has been called |

## Relationships

```
Phaser (Layer 2)
    |
    +-- uses --> Allpass1Pole[12] x 2 (Layer 1)
    |               |
    |               +-- for cascaded phase shifting per channel
    |
    +-- uses --> LFO x 2 (Layer 1)
    |               |
    |               +-- for modulation (L/R with phase offset)
    |
    +-- uses --> OnePoleSmoother x 5 (Layer 1)
                    |
                    +-- for parameter smoothing
```

## State Transitions

### Lifecycle States

```
[Uninitialized]
       |
       | prepare(sampleRate)
       v
[Prepared] <----+
       |        |
       | reset()|
       +--------+
       |
       | process() / processBlock() / processStereo()
       v
[Processing] (returns to Prepared after each call)
```

### Parameter Update Flow

```
Parameter Setter Called
       |
       v
[Clamp to Valid Range]
       |
       v
[Update Smoother Target]
       |
       v
(During next process() call)
       |
       v
[Smoother Interpolates to Target]
       |
       v
[Smoothed Value Applied to Processing]
```

## Validation Rules

### Stage Count
- Must be even number: 2, 4, 6, 8, 10, or 12
- Odd numbers clamped to nearest even (down)
- Out of range clamped to [2, 12]

### Frequency Range
- Center frequency: [100 Hz, 10000 Hz]
- After depth calculation, sweep range clamped to [20 Hz, Nyquist * 0.99]

### LFO Rate
- Range: [0.01 Hz, 20 Hz]
- When tempo sync enabled, derived from tempo and note value

### Feedback
- Range: [-1.0, +1.0]
- tanh soft-limiting applied to prevent oscillation

### Mix
- Range: [0.0, 1.0]
- 0.0 = dry only, 1.0 = wet only

### Stereo Spread
- Range: [0, 360] degrees
- Wrapped to valid range automatically

## Memory Layout

```cpp
class Phaser {
    // Allpass stages: 12 * 2 channels * sizeof(Allpass1Pole)
    // Allpass1Pole: float a_, z1_, y1_ + double sampleRate_ = ~20 bytes
    std::array<Allpass1Pole, 12> stagesL_;  // ~240 bytes
    std::array<Allpass1Pole, 12> stagesR_;  // ~240 bytes

    // LFOs: ~512 bytes each (includes wavetables)
    LFO lfoL_;  // ~512 bytes
    LFO lfoR_;  // ~512 bytes

    // Smoothers: ~24 bytes each
    OnePoleSmoother rateSmoother_;         // ~24 bytes
    OnePoleSmoother depthSmoother_;        // ~24 bytes
    OnePoleSmoother feedbackSmoother_;     // ~24 bytes
    OnePoleSmoother mixSmoother_;          // ~24 bytes
    OnePoleSmoother centerFreqSmoother_;   // ~24 bytes

    // State
    float feedbackStateL_;                 // 4 bytes
    float feedbackStateR_;                 // 4 bytes

    // Configuration (small types grouped)
    double sampleRate_;                    // 8 bytes
    int numStages_;                        // 4 bytes
    float centerFrequency_;                // 4 bytes
    float stereoSpread_;                   // 4 bytes
    float rate_;                           // 4 bytes
    float depth_;                          // 4 bytes
    float feedback_;                       // 4 bytes
    float mix_;                            // 4 bytes
    Waveform waveform_;                    // 1 byte
    NoteValue noteValue_;                  // 1 byte
    NoteModifier noteModifier_;            // 1 byte
    bool tempoSync_;                       // 1 byte
    float tempo_;                          // 4 bytes
    bool prepared_;                        // 1 byte
    // Padding for alignment              // ~7 bytes

    // Total: ~1700 bytes (approximate, depends on LFO wavetable size)
};
```

## Thread Safety

- **Not thread-safe**: Create separate instances for each audio thread
- **Parameter setters**: Can be called from any thread (smoothers handle thread safety)
- **Process methods**: Must be called from audio thread only
- **No locks**: All operations are lock-free for real-time safety
