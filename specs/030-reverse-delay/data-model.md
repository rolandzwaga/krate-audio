# Data Model: Reverse Delay Mode

**Feature**: 030-reverse-delay
**Date**: 2025-12-26

## Component Design

### ReverseBuffer (Layer 1 Primitive)

**Purpose**: Capture audio and provide reversed playback with crossfade support.

**Location**: `src/dsp/primitives/reverse_buffer.h`

```cpp
// ==============================================================================
// Layer 1: DSP Primitive - ReverseBuffer
// ==============================================================================
// Double-buffer system for capturing audio and playing back in reverse.
// Supports smooth crossfade transitions between chunks.
// ==============================================================================

class ReverseBuffer {
public:
    // Lifecycle
    void prepare(double sampleRate, float maxChunkMs) noexcept;
    void reset() noexcept;

    // Configuration
    void setChunkSizeMs(float ms) noexcept;
    void setCrossfadeMs(float ms) noexcept;
    void setReversed(bool reversed) noexcept;  // Set at chunk boundary

    // Processing
    float process(float input) noexcept;  // Returns reversed output

    // State queries
    [[nodiscard]] bool isAtChunkBoundary() const noexcept;
    [[nodiscard]] float getChunkSizeMs() const noexcept;
    [[nodiscard]] size_t getLatencySamples() const noexcept;

private:
    // Double buffers
    std::vector<float> bufferA_;
    std::vector<float> bufferB_;

    // State
    size_t writePos_ = 0;
    size_t readPos_ = 0;
    size_t chunkSizeSamples_ = 0;
    bool activeBufferIsA_ = true;
    bool reversed_ = true;

    // Crossfade
    float crossfadeMs_ = 20.0f;
    size_t crossfadeSamples_ = 0;
    size_t crossfadePos_ = 0;

    double sampleRate_ = 44100.0;
};
```

**Key Behaviors**:
- Write to active capture buffer at writePos_
- Read from playback buffer at readPos_ (forward or reverse based on reversed_ flag)
- When writePos_ reaches chunkSizeSamples_, swap buffers
- During swap, crossfade between old and new playback buffers

### ReverseFeedbackProcessor (Layer 2 Processor)

**Purpose**: Implements IFeedbackProcessor to provide reverse processing in feedback path. Wraps stereo ReverseBuffer pair.

**Location**: `src/dsp/processors/reverse_feedback_processor.h`

```cpp
// ==============================================================================
// Layer 2: DSP Processor - ReverseFeedbackProcessor
// ==============================================================================
// Implements IFeedbackProcessor for injection into FlexibleFeedbackNetwork.
// Provides stereo reverse processing with crossfade.
// ==============================================================================

class ReverseFeedbackProcessor : public IFeedbackProcessor {
public:
    // IFeedbackProcessor interface
    void prepare(double sampleRate, std::size_t maxBlockSize) noexcept override;
    void process(float* left, float* right, std::size_t numSamples) noexcept override;
    void reset() noexcept override;
    [[nodiscard]] std::size_t getLatencySamples() const noexcept override;

    // Configuration
    void setChunkSizeMs(float ms) noexcept;
    void setCrossfadeMs(float ms) noexcept;
    void setReversed(bool reversed) noexcept;
    void setPlaybackMode(PlaybackMode mode) noexcept;

    // State queries
    [[nodiscard]] float getChunkSizeMs() const noexcept;
    [[nodiscard]] PlaybackMode getPlaybackMode() const noexcept;

private:
    ReverseBuffer bufferL_;
    ReverseBuffer bufferR_;
    PlaybackMode mode_ = PlaybackMode::FullReverse;
    std::size_t chunkCounter_ = 0;
    std::minstd_rand rng_;

    bool shouldReverseNextChunk() noexcept;
};
```

**Key Behaviors**:
- Implements IFeedbackProcessor for FlexibleFeedbackNetwork injection
- Wraps stereo ReverseBuffer pair
- Manages playback mode logic (FullReverse, Alternating, Random)
- Reports latency equal to chunk size

### PlaybackMode Enum

**Location**: `src/dsp/processors/reverse_feedback_processor.h`

```cpp
/// @brief Playback direction modes for reverse delay
enum class PlaybackMode : uint8_t {
    FullReverse,   ///< Every chunk plays reversed
    Alternating,   ///< Alternates: reverse, forward, reverse, forward...
    Random         ///< Random direction per chunk (50/50)
};
```

### ReverseDelay (Layer 4 Feature)

**Purpose**: Complete reverse delay effect using FlexibleFeedbackNetwork with ReverseFeedbackProcessor.

**Location**: `src/dsp/features/reverse_delay.h`

```cpp
// ==============================================================================
// Layer 4: User Feature - ReverseDelay
// ==============================================================================
// Reverse delay effect using FlexibleFeedbackNetwork with injected
// ReverseFeedbackProcessor. Follows the ShimmerDelay architectural pattern.
// ==============================================================================

class ReverseDelay {
public:
    // Constants
    static constexpr float kMinChunkMs = 10.0f;
    static constexpr float kMaxChunkMs = 2000.0f;
    static constexpr float kMinCrossfade = 0.0f;
    static constexpr float kMaxCrossfade = 100.0f;
    static constexpr float kMinFeedback = 0.0f;
    static constexpr float kMaxFeedback = 1.2f;  // 120%

    // Lifecycle
    void prepare(double sampleRate, size_t maxBlockSize, float maxChunkMs) noexcept;
    void reset() noexcept;

    // Processing
    void process(float* left, float* right, size_t numSamples,
                 const BlockContext& ctx) noexcept;

    // Chunk Configuration (FR-001, FR-005, FR-006)
    void setChunkSizeMs(float ms) noexcept;
    void setNoteValue(NoteValue note, NoteModifier modifier) noexcept;
    void setTimeMode(TimeMode mode) noexcept;

    // Crossfade (FR-008, FR-009, FR-010)
    void setCrossfadePercent(float percent) noexcept;

    // Playback Mode (FR-011, FR-012, FR-013)
    void setPlaybackMode(PlaybackMode mode) noexcept;
    [[nodiscard]] PlaybackMode getPlaybackMode() const noexcept;

    // Feedback (FR-016, FR-017) - delegated to FlexibleFeedbackNetwork
    void setFeedbackAmount(float amount) noexcept;

    // Filter (FR-018, FR-019) - delegated to FlexibleFeedbackNetwork
    void setFilterEnabled(bool enabled) noexcept;
    void setFilterCutoff(float hz) noexcept;
    void setFilterType(FilterType type) noexcept;

    // Mixing (FR-020, FR-021)
    void setDryWetMix(float percent) noexcept;
    void setOutputGainDb(float dB) noexcept;

    // State queries
    [[nodiscard]] size_t getLatencySamples() const noexcept;
    [[nodiscard]] float getCurrentChunkMs() const noexcept;

    // Parameter snapping
    void snapParameters() noexcept;

private:
    // Core components (FR-015: use FlexibleFeedbackNetwork)
    FlexibleFeedbackNetwork feedbackNetwork_;
    ReverseFeedbackProcessor reverseProcessor_;

    // Parameter smoothers (local to ReverseDelay)
    OnePoleSmoother chunkSizeSmoother_;
    OnePoleSmoother crossfadeSmoother_;
    OnePoleSmoother dryWetSmoother_;
    OnePoleSmoother outputGainSmoother_;

    // Dry signal buffers
    std::vector<float> dryBufferL_;
    std::vector<float> dryBufferR_;

    // State
    TimeMode timeMode_ = TimeMode::Free;
    NoteValue noteValue_ = NoteValue::Quarter;
    NoteModifier noteModifier_ = NoteModifier::None;

    // Parameters
    float chunkSizeMs_ = 500.0f;
    float crossfadePercent_ = 50.0f;
    float dryWetMix_ = 50.0f;
    float outputGainDb_ = 0.0f;
    float feedbackAmount_ = 0.0f;
    float filterCutoffHz_ = 4000.0f;
    bool filterEnabled_ = false;

    double sampleRate_ = 44100.0;
    size_t maxBlockSize_ = 512;
    bool prepared_ = false;

    // Helper methods
    float calculateTempoSyncedChunk(const BlockContext& ctx) const noexcept;
};
```

**Key Integration Points**:
- `feedbackNetwork_.setProcessor(&reverseProcessor_)` - inject reverse processor
- `feedbackNetwork_.setDelayTimeMs(kMinDelayMs)` - minimal delay (reverse provides timing)
- `feedbackNetwork_.setProcessorMix(100.0f)` - 100% processed (always reverse)
- `reverseProcessor_.setChunkSizeMs()` - controls reverse chunk size
- Feedback, filter, and limiting delegated to FlexibleFeedbackNetwork

## Signal Flow

```
Input L/R
    │
    ▼
┌─────────────────────────────────────────────────────────────────┐
│                   FlexibleFeedbackNetwork                        │
│  ┌─────────────────────────────────────────────────────────┐    │
│  │            ReverseFeedbackProcessor (injected)           │    │
│  │  ┌──────────────────┐    ┌──────────────────┐           │    │
│  │  │   ReverseBuffer  │    │   ReverseBuffer  │           │    │
│  │  │   (Left channel) │    │  (Right channel) │           │    │
│  │  │  Capture↔Playback│    │  Capture↔Playback│           │    │
│  │  └──────────────────┘    └──────────────────┘           │    │
│  │              ↓ crossfade          ↓ crossfade            │    │
│  └─────────────────────────────────────────────────────────┘    │
│                              │                                   │
│                              ▼                                   │
│                    ┌─────────────────┐                          │
│                    │ MultimodeFilter │ ← if enabled             │
│                    └────────┬────────┘                          │
│                              │                                   │
│                              ▼                                   │
│                    ┌─────────────────┐                          │
│                    │ DynamicsProc    │ ← soft limit >100%       │
│                    │ + tanh limiter  │                          │
│                    └────────┬────────┘                          │
│                              │                                   │
│                              └──► Feedback × amount ──► Delay ─┐│
│                                                                 ││
│  Input ──► Delay ──► + ◄────────────────────────────────────────┘│
└─────────────────────────────────────────────────────────────────┘
    │
    ▼
Reversed Output
    │
    ├────────────────────┐
    │                    │
    ▼                    ▼
┌─────────┐         ┌─────────┐
│ Dry/Wet │         │ Output  │
│   Mix   │◄────────│  Gain   │
└────┬────┘         └─────────┘
     │
     ▼
Output L/R
```

## State Transitions

### Chunk Boundary State Machine

```
┌───────────────────────────────────────────────────────┐
│                     CAPTURING                         │
│  - Write samples to active buffer                     │
│  - Read from playback buffer (may be in crossfade)    │
│  - Increment writePos_                                │
└───────────────────────────┬───────────────────────────┘
                            │ writePos_ >= chunkSizeSamples_
                            ▼
┌───────────────────────────────────────────────────────┐
│                   CHUNK BOUNDARY                      │
│  - Determine next chunk direction (mode-dependent)    │
│  - Swap capture/playback buffers                      │
│  - Reset writePos_ = 0                                │
│  - Start crossfade if enabled                         │
│  - Increment chunkCounter_                            │
└───────────────────────────┬───────────────────────────┘
                            │
                            ▼
                       CAPTURING (new chunk)
```

### Crossfade State

```
crossfadePos_ < crossfadeSamples_:
    output = oldBuffer[pos] * cos(phase) + newBuffer[pos] * sin(phase)
    where phase = (crossfadePos_ / crossfadeSamples_) * (PI / 2)

crossfadePos_ >= crossfadeSamples_:
    output = newBuffer[pos]  (no crossfade)
```

## Parameter Ranges

| Parameter | Min | Max | Default | Unit |
|-----------|-----|-----|---------|------|
| Chunk Size | 10 | 2000 | 500 | ms |
| Crossfade | 0 | 100 | 50 | % |
| Feedback | 0 | 120 | 0 | % |
| Filter Cutoff | 20 | 20000 | 4000 | Hz |
| Dry/Wet | 0 | 100 | 50 | % |
| Output Gain | -inf | +6 | 0 | dB |

## Test Scenarios

See [quickstart.md](quickstart.md) for verification test cases.
