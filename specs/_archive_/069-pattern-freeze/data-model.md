# Pattern Freeze Mode - Data Model

**Feature Branch**: `069-pattern-freeze`
**Created**: 2026-01-16

## Table of Contents

1. [Enumerations](#enumerations)
2. [Core Structures](#core-structures)
3. [Component Classes](#component-classes)
4. [Relationships](#relationships)
5. [State Transitions](#state-transitions)

---

## Enumerations

### PatternType

```cpp
/// Pattern algorithm type for Pattern Freeze Mode
/// @note Maps to UI dropdown and serialization
enum class PatternType : uint8_t {
    Euclidean = 0,      ///< Bjorklund algorithm rhythm patterns
    GranularScatter,    ///< Poisson process random grain triggering
    HarmonicDrones,     ///< Sustained multi-voice playback
    NoiseBursts,        ///< Rhythmic filtered noise generation
    Legacy              ///< Original freeze behavior (default for compatibility)
};

inline constexpr int kPatternTypeCount = 5;
inline constexpr PatternType kDefaultPatternType = PatternType::Legacy;
```

### SliceMode

```cpp
/// Determines how slice length is controlled
enum class SliceMode : uint8_t {
    Fixed = 0,    ///< All slices use the configured slice length
    Variable      ///< Slice length varies with pattern (e.g., Euclidean step position)
};

inline constexpr SliceMode kDefaultSliceMode = SliceMode::Fixed;
```

### PitchInterval

```cpp
/// Musical intervals for Harmonic Drones voices
enum class PitchInterval : uint8_t {
    Unison = 0,     ///< 0 semitones
    MinorThird,     ///< 3 semitones
    MajorThird,     ///< 4 semitones
    Fourth,         ///< 5 semitones (Perfect Fourth)
    Fifth,          ///< 7 semitones (Perfect Fifth)
    Octave          ///< 12 semitones
};

/// Get semitone offset for pitch interval
[[nodiscard]] inline constexpr float getIntervalSemitones(PitchInterval interval) noexcept {
    constexpr float kSemitones[] = { 0.0f, 3.0f, 4.0f, 5.0f, 7.0f, 12.0f };
    return kSemitones[static_cast<size_t>(interval)];
}

inline constexpr int kPitchIntervalCount = 6;
inline constexpr PitchInterval kDefaultPitchInterval = PitchInterval::Octave;
```

### NoiseColor

```cpp
/// Noise spectrum types for Noise Bursts pattern
enum class NoiseColor : uint8_t {
    White = 0,    ///< Flat spectrum (equal energy per Hz)
    Pink,         ///< 1/f spectrum (-3dB/octave)
    Brown         ///< 1/f^2 spectrum (-6dB/octave)
};

inline constexpr int kNoiseColorCount = 3;
inline constexpr NoiseColor kDefaultNoiseColor = NoiseColor::Pink;
```

### EnvelopeShape

```cpp
/// Envelope curve types for slice/grain amplitude shaping
enum class EnvelopeShape : uint8_t {
    Linear = 0,      ///< Triangle/trapezoid with linear attack/release
    Exponential      ///< RC-style curves with punchier attack
};

inline constexpr int kEnvelopeShapeCount = 2;
inline constexpr EnvelopeShape kDefaultEnvelopeShape = EnvelopeShape::Linear;
```

---

## Core Structures

### Slice

Represents a single playback voice in the slice pool.

```cpp
/// Active slice playback state
/// @note All members must be trivially copyable for real-time safety
struct Slice {
    // Playback state
    float readPosition = 0.0f;          ///< Current position in capture buffer (samples from write head)
    float playbackRate = 1.0f;          ///< Playback speed (1.0 = normal, 2.0 = double speed)
    float sliceLengthSamples = 0.0f;    ///< Total slice length in samples

    // Envelope state
    float envelopePhase = 0.0f;         ///< Current envelope position [0, 1]
    float envelopeValue = 0.0f;         ///< Current envelope amplitude [0, 1]
    float attackIncrement = 0.0f;       ///< Per-sample attack phase increment
    float releaseIncrement = 0.0f;      ///< Per-sample release phase increment
    float attackEndPhase = 0.0f;        ///< Phase value where attack ends
    float releaseStartPhase = 0.0f;     ///< Phase value where release begins

    // Panning and amplitude
    float amplitude = 1.0f;             ///< Base amplitude (0-1)
    float panL = 1.0f;                  ///< Left channel pan gain
    float panR = 1.0f;                  ///< Right channel pan gain

    // State flags
    bool active = false;                ///< Is this slice currently playing?
    bool inRelease = false;             ///< Is this slice in release phase?

    // Tracking
    size_t startSample = 0;             ///< Sample number when slice started (for age tracking)
    size_t id = 0;                      ///< Unique ID for debugging

    /// Calculate remaining envelope duration (for voice stealing)
    [[nodiscard]] float getRemainingPhase() const noexcept {
        return 1.0f - envelopePhase;
    }
};
```

### DroneVoice

State for a single Harmonic Drones voice.

```cpp
/// Single voice in Harmonic Drones pattern
struct DroneVoice {
    float pitchSemitones = 0.0f;    ///< Pitch offset from base
    float amplitude = 1.0f;          ///< Voice amplitude (after gain compensation)
    float driftPhase = 0.0f;         ///< LFO phase for drift modulation
    bool active = false;             ///< Is this voice active?
};
```

### EuclideanState

State for Euclidean pattern scheduling.

```cpp
/// Euclidean pattern sequencer state
struct EuclideanState {
    uint32_t pattern = 0;           ///< Bitmask of hits (bit i = step i is a hit)
    int steps = 8;                  ///< Total steps in pattern
    int hits = 3;                   ///< Number of hits
    int rotation = 0;               ///< Pattern rotation offset
    int currentStep = 0;            ///< Current step position [0, steps)
    float samplesPerStep = 0.0f;    ///< Samples between steps (tempo-dependent)
    float stepAccumulator = 0.0f;   ///< Fractional step accumulator
    bool patternDirty = true;       ///< Needs pattern regeneration?
};
```

### GranularState

State for Granular Scatter pattern.

```cpp
/// Granular Scatter pattern state
struct GranularState {
    float density = 10.0f;              ///< Target grains per second
    float samplesUntilNext = 0.0f;      ///< Samples until next grain trigger
    float positionJitter = 0.5f;        ///< Position randomization [0, 1]
    float sizeJitter = 0.25f;           ///< Size randomization [0, 1]
    float grainSizeMs = 100.0f;         ///< Base grain size in milliseconds
};
```

### NoiseBurstState

State for Noise Bursts pattern.

```cpp
/// Noise Bursts pattern state
struct NoiseBurstState {
    float filterCutoff = 2000.0f;       ///< Base filter cutoff frequency (Hz)
    float filterSweep = 0.5f;           ///< Filter envelope modulation depth [0, 1]
    float currentCutoff = 2000.0f;      ///< Current modulated cutoff
    float burstEnvelope = 0.0f;         ///< Current burst envelope value [0, 1]
    float burstPhase = 0.0f;            ///< Current burst envelope phase
    float samplesPerBurst = 0.0f;       ///< Samples between bursts (tempo-dependent)
    float burstAccumulator = 0.0f;      ///< Fractional burst accumulator
};
```

---

## Component Classes

### EuclideanPattern (Layer 0)

```cpp
/// Layer 0: Pure algorithm - no state, no dependencies
/// Implements Bjorklund/Euclidean rhythm generation
class EuclideanPattern {
public:
    static constexpr int kMinSteps = 2;
    static constexpr int kMaxSteps = 32;

    /// Generate pattern bitmask using accumulator method
    /// @param pulses Number of hits/triggers
    /// @param steps Total steps in pattern
    /// @param rotation Pattern rotation offset
    /// @return Bitmask where bit i is set if step i is a hit
    [[nodiscard]] static uint32_t generate(int pulses, int steps, int rotation = 0) noexcept;

    /// Check if position is a hit in pattern
    /// @param pattern Generated pattern bitmask
    /// @param position Current step position
    /// @param steps Total steps (for bounds checking)
    [[nodiscard]] static bool isHit(uint32_t pattern, int position, int steps) noexcept;
};
```

### RollingCaptureBuffer (Layer 1)

```cpp
/// Layer 1: Stereo circular buffer for continuous audio capture
class RollingCaptureBuffer {
public:
    static constexpr float kDefaultMaxSeconds = 5.0f;
    static constexpr float kMinMaxSeconds = 1.0f;
    static constexpr float kMaxMaxSeconds = 10.0f;

    // Lifecycle
    void prepare(double sampleRate, float maxSeconds = kDefaultMaxSeconds) noexcept;
    void reset() noexcept;

    // Recording (always active)
    void write(float left, float right) noexcept;

    // Playback
    [[nodiscard]] float readL(float delaySamples) const noexcept;
    [[nodiscard]] float readR(float delaySamples) const noexcept;
    [[nodiscard]] std::pair<float, float> read(float delaySamples) const noexcept;

    // Queries
    [[nodiscard]] float getFillLevel() const noexcept;        // 0-100%
    [[nodiscard]] size_t maxDelaySamples() const noexcept;
    [[nodiscard]] double sampleRate() const noexcept;
    [[nodiscard]] bool isReady() const noexcept;              // Has at least 200ms recorded

private:
    DelayLine bufferL_;
    DelayLine bufferR_;
    size_t samplesRecorded_ = 0;
    size_t maxSamples_ = 0;
    size_t minReadySamples_ = 0;    // 200ms worth of samples
    double sampleRate_ = 44100.0;
};
```

### SlicePool (Layer 1)

```cpp
/// Layer 1: Voice pool for slice playback with shortest-remaining stealing
class SlicePool {
public:
    static constexpr size_t kMaxSlices = 8;

    // Lifecycle
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // Voice management
    [[nodiscard]] Slice* acquireSlice(size_t currentSample) noexcept;
    void releaseSlice(Slice* slice) noexcept;
    void releaseAllSlices() noexcept;

    // Iteration
    [[nodiscard]] size_t activeCount() const noexcept;

    /// Process all active slices
    /// @tparam Func void(Slice&) callable
    template<typename Func>
    void forEachActive(Func&& func) noexcept;

private:
    Slice* stealShortestRemaining() noexcept;
    void applyMicroFade(Slice* slice) noexcept;

    std::array<Slice, kMaxSlices> slices_{};
    size_t nextId_ = 0;
    double sampleRate_ = 44100.0;
};
```

### PatternScheduler (Layer 2)

```cpp
/// Layer 2: Pattern trigger scheduling
class PatternScheduler {
public:
    // Lifecycle
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // Pattern type
    void setPatternType(PatternType type) noexcept;
    [[nodiscard]] PatternType getPatternType() const noexcept;

    // Euclidean parameters
    void setEuclideanSteps(int steps) noexcept;
    void setEuclideanHits(int hits) noexcept;
    void setEuclideanRotation(int rotation) noexcept;
    void setPatternRate(NoteValue note, NoteModifier mod = NoteModifier::None) noexcept;

    // Granular parameters
    void setGranularDensity(float hz) noexcept;
    void setGranularPositionJitter(float percent) noexcept;
    void setGranularSizeJitter(float percent) noexcept;
    void setGranularGrainSize(float ms) noexcept;

    // Noise burst parameters
    void setNoiseBurstRate(NoteValue note, NoteModifier mod = NoteModifier::None) noexcept;

    // Processing
    struct TriggerResult {
        bool triggered = false;
        float positionJitter = 0.0f;    // For Granular: random position offset
        float sizeMultiplier = 1.0f;    // For Granular: random size multiplier
    };

    [[nodiscard]] TriggerResult process(const BlockContext& ctx) noexcept;

    // State queries
    [[nodiscard]] bool isTempoValid(const BlockContext& ctx) const noexcept;
    [[nodiscard]] bool requiresTempo() const noexcept;

private:
    TriggerResult processEuclidean(const BlockContext& ctx) noexcept;
    TriggerResult processGranular() noexcept;
    TriggerResult processNoiseBursts(const BlockContext& ctx) noexcept;

    void updateEuclideanTiming(const BlockContext& ctx) noexcept;
    void regenerateEuclideanPattern() noexcept;

    PatternType patternType_ = PatternType::Legacy;
    EuclideanState euclidean_;
    GranularState granular_;
    NoiseBurstState noiseBurst_;

    NoteValue patternRate_ = NoteValue::Eighth;
    NoteModifier patternRateMod_ = NoteModifier::None;

    Xorshift32 rng_{12345};
    double sampleRate_ = 44100.0;
};
```

### PatternFreezeMode (Layer 4)

```cpp
/// Layer 4: Main Pattern Freeze Mode effect
class PatternFreezeMode {
public:
    // Constants
    static constexpr float kMinSliceLengthMs = 10.0f;
    static constexpr float kMaxSliceLengthMs = 2000.0f;
    static constexpr float kDefaultSliceLengthMs = 200.0f;
    static constexpr float kCrossfadeTimeMs = 500.0f;

    // Lifecycle
    void prepare(double sampleRate, size_t maxBlockSize, float maxDelayMs) noexcept;
    void reset() noexcept;
    void snapParameters() noexcept;

    // Freeze control
    void setFreezeEnabled(bool enabled) noexcept;
    [[nodiscard]] bool isFreezeEnabled() const noexcept;

    // Pattern type
    void setPatternType(PatternType type) noexcept;
    [[nodiscard]] PatternType getPatternType() const noexcept;

    // Slice parameters
    void setSliceLength(float ms) noexcept;
    void setSliceMode(SliceMode mode) noexcept;

    // Euclidean parameters
    void setEuclideanSteps(int steps) noexcept;
    void setEuclideanHits(int hits) noexcept;
    void setEuclideanRotation(int rotation) noexcept;
    void setPatternRate(NoteValue note, NoteModifier mod = NoteModifier::None) noexcept;

    // Granular parameters
    void setGranularDensity(float hz) noexcept;
    void setGranularPositionJitter(float percent) noexcept;
    void setGranularSizeJitter(float percent) noexcept;
    void setGranularGrainSize(float ms) noexcept;

    // Harmonic Drones parameters
    void setDroneVoiceCount(int count) noexcept;
    void setDroneInterval(PitchInterval interval) noexcept;
    void setDroneDrift(float percent) noexcept;
    void setDroneDriftRate(float hz) noexcept;

    // Noise Bursts parameters
    void setNoiseColor(NoiseColor color) noexcept;
    void setNoiseBurstRate(NoteValue note, NoteModifier mod = NoteModifier::None) noexcept;
    void setNoiseFilterType(FilterType type) noexcept;
    void setNoiseFilterCutoff(float hz) noexcept;
    void setNoiseFilterSweep(float percent) noexcept;

    // Envelope parameters
    void setEnvelopeAttack(float ms) noexcept;
    void setEnvelopeRelease(float ms) noexcept;
    void setEnvelopeShape(EnvelopeShape shape) noexcept;

    // Processing chain parameters (delegate to FreezeFeedbackProcessor)
    void setPitchSemitones(float semitones) noexcept;
    void setPitchCents(float cents) noexcept;
    void setShimmerMix(float percent) noexcept;
    void setDecay(float percent) noexcept;
    void setDiffusionAmount(float percent) noexcept;
    void setDiffusionSize(float percent) noexcept;
    void setFilterEnabled(bool enabled) noexcept;
    void setFilterType(FilterType type) noexcept;
    void setFilterCutoff(float hz) noexcept;
    void setDryWetMix(float percent) noexcept;

    // Queries
    [[nodiscard]] float getCaptureBufferFillLevel() const noexcept;
    [[nodiscard]] size_t getActiveSliceCount() const noexcept;  ///< For debugging/metering UI
    [[nodiscard]] size_t getLatencySamples() const noexcept;

    // Processing
    void process(float* left, float* right, size_t numSamples,
                 const BlockContext& ctx) noexcept;

private:
    void processSlicePlayback(float* left, float* right, size_t numSamples) noexcept;
    void processHarmonicDrones(float* left, float* right, size_t numSamples,
                               const BlockContext& ctx) noexcept;
    void processNoiseBursts(float* left, float* right, size_t numSamples,
                            const BlockContext& ctx) noexcept;
    void processLegacy(float* left, float* right, size_t numSamples,
                       const BlockContext& ctx) noexcept;

    void triggerSlice(const PatternScheduler::TriggerResult& trigger) noexcept;
    void applySliceEnvelope(Slice& slice) noexcept;
    void updateCrossfade(size_t numSamples) noexcept;

    // Components
    RollingCaptureBuffer captureBuffer_;
    PatternScheduler scheduler_;
    SlicePool slicePool_;
    FreezeFeedbackProcessor feedbackProcessor_;
    FreezeMode legacyMode_;

    // Drone components
    std::array<PitchShiftProcessor, 4> droneShifters_;
    std::array<LFO, 4> droneLFOs_;
    int droneVoiceCount_ = 2;
    PitchInterval droneInterval_ = PitchInterval::Octave;
    float droneDrift_ = 0.3f;
    float droneDriftRate_ = 0.5f;

    // Noise burst components
    NoiseGenerator noiseGen_;
    Biquad noiseFilter_;
    NoiseColor noiseColor_ = NoiseColor::Pink;

    // Crossfade for pattern type switching
    LinearRamp patternCrossfade_;
    PatternType previousPatternType_ = PatternType::Legacy;
    bool crossfadeActive_ = false;

    // Parameter smoothers
    OnePoleSmoother sliceLengthSmoother_;
    OnePoleSmoother envelopeAttackSmoother_;
    OnePoleSmoother envelopeReleaseSmoother_;

    // Parameters
    float sliceLengthMs_ = kDefaultSliceLengthMs;
    SliceMode sliceMode_ = SliceMode::Fixed;
    float envelopeAttackMs_ = 10.0f;
    float envelopeReleaseMs_ = 100.0f;
    EnvelopeShape envelopeShape_ = EnvelopeShape::Linear;

    // State
    bool freezeEnabled_ = false;
    PatternType patternType_ = PatternType::Legacy;
    double sampleRate_ = 44100.0;
    size_t maxBlockSize_ = 512;
    size_t currentSample_ = 0;

    // Scratch buffers
    std::vector<float> scratchL_;
    std::vector<float> scratchR_;
    std::vector<float> mixBufferL_;
    std::vector<float> mixBufferR_;
};
```

---

## Relationships

```
                    +------------------+
                    |  PatternFreeze   |  Layer 4
                    |      Mode        |
                    +--------+---------+
                             |
          +------------------+------------------+
          |                  |                  |
          v                  v                  v
+------------------+ +----------------+ +------------------+
|RollingCapture    | |Pattern         | |SlicePool         | Layer 1-2
|Buffer            | |Scheduler       | |                  |
+------------------+ +----------------+ +------------------+
         |                   |                  |
         v                   v                  v
+------------------+ +----------------+ +------------------+
|DelayLine (x2)    | |EuclideanPattern| |Slice (x8)        | Layer 0-1
+------------------+ +----------------+ +------------------+

                    +------------------+
                    |FreezeFeedback    |  Layer 3
                    |Processor         |  (reused)
                    +------------------+

                    +------------------+
                    |FreezeMode        |  Layer 4
                    |(Legacy delegate) |  (reused)
                    +------------------+
```

**Dependencies**:
- PatternFreezeMode owns: RollingCaptureBuffer, PatternScheduler, SlicePool
- PatternFreezeMode references: FreezeFeedbackProcessor, FreezeMode
- PatternScheduler uses: EuclideanPattern (static), Xorshift32
- RollingCaptureBuffer owns: DelayLine (x2)
- SlicePool owns: Slice array

---

## State Transitions

### Freeze Enable/Disable

```
           setFreezeEnabled(true)
    UNFROZEN -----------------> FROZEN
       ^                          |
       |   setFreezeEnabled(false)|
       +---------------------------+
```

**UNFROZEN State**:
- Rolling buffer continues recording
- Audio passes through to normal delay processing
- Pattern scheduler is inactive

**FROZEN State**:
- Rolling buffer continues recording (FR-004)
- Pattern scheduler active, triggering slices
- Slices read from capture buffer

### Pattern Type Change While Frozen

```
    FROZEN                    FROZEN+CROSSFADE               FROZEN
    (Pattern A)               (A -> B transition)            (Pattern B)
        |                           |                            |
        | setPatternType(B)         | crossfade_complete         |
        +-------------------------->+--------------------------->+
```

**Crossfade Behavior**:
- Previous pattern continues at decreasing amplitude
- New pattern starts at increasing amplitude
- Linear crossfade over 500ms
- No clicks during transition

### Slice Lifecycle

```
    INACTIVE         ATTACK           SUSTAIN          RELEASE        INACTIVE
        |               |                |                |              |
        | trigger       | phase>attack   | phase>sustain  | phase>=1.0   |
        +-------------->+--------------->+--------------->+------------->+
```

**Voice Stealing**:
- If all 8 slices are active, find shortest remaining
- Apply micro-fade (~2ms) to stolen slice
- Reuse for new trigger

---

## Validation Rules

### Parameter Ranges

| Parameter | Min | Max | Default | Unit |
|-----------|-----|-----|---------|------|
| Slice Length | 10 | 2000 | 200 | ms |
| Euclidean Steps | 2 | 32 | 8 | steps |
| Euclidean Hits | 1 | steps | 3 | hits |
| Euclidean Rotation | 0 | steps-1 | 0 | steps |
| Granular Density | 1 | 50 | 10 | Hz |
| Position Jitter | 0 | 100 | 50 | % |
| Size Jitter | 0 | 100 | 25 | % |
| Grain Size | 10 | 500 | 100 | ms |
| Drone Voices | 1 | 4 | 2 | voices |
| Drone Drift | 0 | 100 | 30 | % |
| Drift Rate | 0.1 | 2.0 | 0.5 | Hz |
| Noise Filter Cutoff | 20 | 20000 | 2000 | Hz |
| Filter Sweep | 0 | 100 | 50 | % |
| Envelope Attack | 0 | 500 | 10 | ms |
| Envelope Release | 0 | 2000 | 100 | ms |

### Invariants

1. **Euclidean hits <= steps**: Automatically clamped when steps changes
2. **Euclidean rotation < steps**: Automatically wrapped
3. **Slice length <= buffer size**: Clamped to available buffer
4. **Active slices <= kMaxSlices (8)**: Voice stealing enforced
5. **Capture buffer never stops recording**: Even when frozen

---

*Data model created: 2026-01-16*
