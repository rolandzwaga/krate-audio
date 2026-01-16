# Implementation Plan: Pattern Freeze Mode

**Feature Branch**: `069-pattern-freeze`
**Created**: 2026-01-16
**Status**: Planning Complete

## Table of Contents

1. [Research Findings](#research-findings)
2. [Technical Context](#technical-context)
3. [Constitution Check](#constitution-check)
4. [Architecture Decisions](#architecture-decisions)
5. [Component Design](#component-design)
6. [Implementation Phases](#implementation-phases)
7. [Testing Strategy](#testing-strategy)
8. [Risk Assessment](#risk-assessment)

---

## Research Findings

### 1. Euclidean/Bjorklund Algorithm

**Decision**: Implement a stateful accumulator-based approach for the Euclidean pattern generator.

**Rationale**:
- The [Bjorklund algorithm](https://gist.github.com/unohee/d4f32b3222b42de84a5f) provides mathematically optimal distribution of hits across steps
- The [accumulator method from sndkit](https://paulbatchelor.github.io/sndkit/euclid/) is simpler and more suitable for real-time use
- Formula: `(((pulses * (i + rotation)) % steps) + pulses) >= steps` determines if step `i` is a hit

**Implementation**:
```cpp
// Stateless check - can be precomputed into a bitmask
bool isHit(int pulses, int steps, int rotation, int position) {
    return (((pulses * (position + rotation)) % steps) + pulses) >= steps;
}

// Pre-generate pattern as bitmask for efficient lookup
uint32_t generatePattern(int pulses, int steps, int rotation) {
    uint32_t pattern = 0;
    for (int i = 0; i < steps; ++i) {
        if (isHit(pulses, steps, rotation, i)) {
            pattern |= (1u << i);
        }
    }
    return pattern;
}
```

**Alternatives Considered**:
- Full recursive Bjorklund: More complex, no runtime advantage, requires dynamic allocation
- Bresenham line drawing: Similar simplicity but less established in music software

**Sources**:
- [Toussaint's Original Paper (McGill)](https://cgm.cs.mcgill.ca/~godfried/publications/banff.pdf)
- [Paul Batchelor's sndkit Implementation](https://paulbatchelor.github.io/sndkit/euclid/)
- [Bjorklund C++ Gist](https://gist.github.com/unohee/d4f32b3222b42de84a5f)

---

### 2. Poisson Process for Granular Scatter

**Decision**: Use exponential distribution with inverse transform sampling for grain triggering.

**Rationale**:
- [Poisson processes](https://en.wikipedia.org/wiki/Granular_synthesis) create organic, irregular grain clouds that avoid mechanical regularity
- The inter-arrival times of a Poisson process follow an exponential distribution
- Can be computed efficiently from uniform random numbers: `interval = -ln(U) / lambda`

**Implementation**:
```cpp
// Generate next interval using exponential distribution
// lambda = target density (grains per second)
float generateNextInterval(float lambda, Xorshift32& rng) {
    float u = rng.nextUnipolar();
    // Avoid log(0) by clamping minimum
    u = std::max(u, 1e-7f);
    // Exponential: -ln(U) / lambda, convert to samples
    return -std::log(u) / lambda * sampleRate_;
}
```

**Key Properties**:
- Average rate matches target density
- Variance creates natural "clumping" and gaps
- Memory-less: each interval is independent

**Alternatives Considered**:
- Fixed intervals with jitter: Less natural, regular pattern still audible
- Gaussian intervals: Can produce negative values, requires clamping

**Sources**:
- [C++ std::exponential_distribution](https://en.cppreference.com/w/cpp/numeric/random/exponential_distribution.html)
- [Granular Synthesis Wikipedia](https://en.wikipedia.org/wiki/Granular_synthesis)
- [DSP Concepts Granular Module](https://documentation.dspconcepts.com/awe-designer/8.D.2.6/granular-synthesis-module)

---

### 3. Rolling Circular Buffer

**Decision**: Reuse existing `DelayLine` primitive for rolling capture buffer.

**Rationale**:
- The existing [DelayLine](../dsp/include/krate/dsp/primitives/delay_line.h) already provides:
  - Power-of-2 buffer sizing for efficient wraparound
  - Linear interpolation for fractional reads
  - Real-time safe write/read operations
- No need for lock-free complexity since Pattern Freeze is single-producer (audio thread) single-consumer (audio thread)
- 5 seconds @ 192kHz = 960,000 samples, fits well within `DelayLine` capacity (10s max)

**Implementation**:
```cpp
// RollingCaptureBuffer wraps two DelayLines for stereo
class RollingCaptureBuffer {
    DelayLine bufferL_;
    DelayLine bufferR_;
    size_t samplesRecorded_ = 0;

    void prepare(double sampleRate, float maxSeconds = 5.0f) {
        bufferL_.prepare(sampleRate, maxSeconds);
        bufferR_.prepare(sampleRate, maxSeconds);
    }

    void write(float left, float right) {
        bufferL_.write(left);
        bufferR_.write(right);
        if (samplesRecorded_ < maxSamples_) ++samplesRecorded_;
    }

    float getFillLevel() const {
        return static_cast<float>(samplesRecorded_) / maxSamples_ * 100.0f;
    }
};
```

**Alternatives Considered**:
- Custom lock-free ring buffer: Unnecessary complexity for single-thread access
- [TPCircularBuffer](https://github.com/michaeltyson/TPCircularBuffer): External dependency, virtual memory tricks not needed

**Sources**:
- [Lock-free ring buffer discussion](https://kmdreko.github.io/posts/20191003/a-simple-lock-free-ring-buffer/)
- [A simple circular buffer for audio](https://atastypixel.com/a-simple-fast-circular-buffer-implementation-for-audio-processing/)
- Existing codebase: `dsp/include/krate/dsp/primitives/delay_line.h`

---

### 4. Voice Stealing Strategy

**Decision**: Implement "shortest remaining" voice stealing for grain polyphony.

**Rationale**:
- [Voice stealing](https://electronicmusic.fandom.com/wiki/Voice_stealing) is essential when exceeding max polyphony
- "Shortest remaining" (grain closest to completing envelope) is least disruptive to texture
- The spec explicitly requires this: FR-087a

**Implementation**:
```cpp
Grain* stealShortestRemaining(std::array<Grain, kMaxGrains>& grains) {
    Grain* victim = nullptr;
    float highestPhase = -1.0f;

    for (auto& grain : grains) {
        if (grain.active && grain.envelopePhase > highestPhase) {
            highestPhase = grain.envelopePhase;
            victim = &grain;
        }
    }
    return victim;
}
```

**Key Insight**: Since we use envelope phase [0, 1], the grain with highest phase is closest to completion. This differs from "oldest" which uses start time.

**Alternatives Considered**:
- Oldest (existing GrainPool default): More disruptive, may cut grains mid-sustain
- Lowest amplitude: Requires additional tracking, less predictable
- Random: Unpredictable, may cut prominent grains

**Sources**:
- [KVR Voice Stealing Discussion](https://www.kvraudio.com/forum/viewtopic.php?t=91557)
- [Voice allocation - Electronic Music Wiki](https://electronicmusic.fandom.com/wiki/Voice_allocation)
- [JUCE Voice Steal Pops Discussion](https://forum.juce.com/t/voice-steal-pops/30923)

---

### 5. Grain Envelope Shaping

**Decision**: Support Linear and Exponential envelope shapes using the existing `GrainEnvelope` infrastructure.

**Rationale**:
- [Grain envelopes](https://www.granularsynthesis.com/hthesis/envelope.html) are critical for click-free playback
- Existing `GrainEnvelope::generate()` supports Hann, Trapezoid, Sine, Blackman
- Spec requires Linear and Exponential (FR-070)
- Linear = Trapezoid with equal attack/sustain/decay
- Exponential = RC-style curves for punchier attack/smoother decay

**Implementation** (extend `GrainEnvelopeType` enum):
```cpp
enum class GrainEnvelopeType : uint8_t {
    Hann,       // Existing
    Trapezoid,  // Existing - use for Linear with attackRatio = releaseRatio
    Sine,       // Existing
    Blackman,   // Existing
    Linear,     // New: Triangle with configurable attack/release
    Exponential // New: RC curve for punchier transients
};

// For Exponential envelope:
// Attack: 1 - exp(-t / tau_attack)
// Release: exp(-t / tau_release)
```

**Key Parameters**:
- Attack time: 0-500ms (default 10ms)
- Release time: 0-2000ms (default 100ms)
- Minimum 10ms attack/release prevents clicks (FR-065, FR-067)

**Sources**:
- [Grain Envelope Design](https://www.granularsynthesis.com/hthesis/envelope.html)
- [Window Functions for Audio](https://music.arts.uci.edu/dobrian/maxcookbook/generate-window-function-use-amplitude-envelope)
- [Ross Bencina's Granular Synthesis](http://www.rossbencina.com/writings)

---

### 6. Multi-Voice Pitch Shifting for Drones

**Decision**: Use multiple instances of existing `PitchShiftProcessor` with different pitch intervals.

**Rationale**:
- Existing [PitchShiftProcessor](../dsp/include/krate/dsp/processors/pitch_shift_processor.h) provides high-quality pitch shifting
- Harmonic Drones requires up to 4 simultaneous voices (FR-041)
- Each voice can have independent pitch interval (Unison, Octave, Fifth, etc.)
- Drift modulation uses existing [LFO](../dsp/include/krate/dsp/primitives/lfo.h) primitive

**Implementation**:
```cpp
class DroneVoice {
    PitchShiftProcessor pitchShifter_;
    LFO driftLFO_;          // For subtle pitch/amplitude modulation
    float baseInterval_;     // Pitch interval in semitones (0, 12, 7, 5, 4, 3)
    float amplitude_ = 1.0f;

    void process(const float* input, float* output, size_t n, float driftAmount) {
        float pitchMod = driftLFO_.process() * driftAmount * 0.5f; // Max +/- 50 cents
        pitchShifter_.setSemitones(baseInterval_);
        pitchShifter_.setCents(pitchMod * 100.0f);
        pitchShifter_.process(input, output, n);
    }
};
```

**Pitch Intervals** (semitones):
- Unison: 0
- Octave: 12
- Fifth: 7
- Fourth: 5
- Major Third: 4
- Minor Third: 3

**Gain Compensation**: `1 / sqrt(voiceCount)` prevents level explosion (FR-088)

**Sources**:
- [Pitchometry Multi-Voice Plugin](https://aegeanmusic.com/pitchometry)
- [Time Stretching Overview by Bernsee](http://blogs.zynaptiq.com/bernsee/time-pitch-overview/)
- Existing codebase: `dsp/include/krate/dsp/processors/pitch_shift_processor.h`

---

## Technical Context

### Existing Components to Reuse

| Component | Location | Usage in Pattern Freeze |
|-----------|----------|------------------------|
| `FreezeMode` | `dsp/effects/freeze_mode.h` | Base class, Legacy pattern implementation |
| `FreezeFeedbackProcessor` | `dsp/effects/freeze_mode.h` | Processing chain (pitch, diffusion, filter, decay) |
| `DelayLine` | `dsp/primitives/delay_line.h` | Rolling capture buffer |
| `GrainPool` | `dsp/primitives/grain_pool.h` | Adapt for slice polyphony |
| `GrainProcessor` | `dsp/processors/grain_processor.h` | Slice/grain playback |
| `GrainScheduler` | `dsp/processors/grain_scheduler.h` | Reference for timing |
| `GrainEnvelope` | `dsp/core/grain_envelope.h` | Envelope generation |
| `NoiseGenerator` | `dsp/processors/noise_generator.h` | Noise Bursts pattern |
| `PitchShiftProcessor` | `dsp/processors/pitch_shift_processor.h` | Harmonic Drones pitch |
| `LFO` | `dsp/primitives/lfo.h` | Drift modulation |
| `OnePoleSmoother` | `dsp/primitives/smoother.h` | Parameter smoothing |
| `LinearRamp` | `dsp/primitives/smoother.h` | Crossfade transitions |
| `Biquad` | `dsp/primitives/biquad.h` | Noise filter |
| `NoteValue` | `dsp/core/note_value.h` | Tempo sync |
| `BlockContext` | `dsp/core/block_context.h` | Tempo/timing info |
| `Xorshift32` | `dsp/core/random.h` | Random number generation |

### New Components Required

| Component | Layer | Location | Purpose |
|-----------|-------|----------|---------|
| `EuclideanPattern` | 0 | `dsp/core/euclidean_pattern.h` | Bjorklund rhythm generation |
| `RollingCaptureBuffer` | 1 | `dsp/primitives/rolling_capture_buffer.h` | Stereo continuous recording |
| `SlicePool` | 1 | `dsp/primitives/slice_pool.h` | Voice management with shortest-remaining stealing |
| `PatternScheduler` | 2 | `dsp/processors/pattern_scheduler.h` | Pattern-based trigger timing |
| `SlicePlayer` | 2 | `dsp/processors/slice_player.h` | Slice playback with envelope |
| `NoiseBurstGenerator` | 2 | `dsp/processors/noise_burst_generator.h` | Rhythmic filtered noise |
| `DroneVoiceBank` | 3 | `dsp/systems/drone_voice_bank.h` | Multi-voice pitch shifting |
| `PatternFreezeMode` | 4 | `dsp/effects/pattern_freeze_mode.h` | Main feature class |

---

## Constitution Check

### Pre-Design Validation

| Principle | Status | Notes |
|-----------|--------|-------|
| I. VST3 Separation | PASS | PatternFreezeMode is DSP-only, no controller coupling |
| II. Real-Time Safety | PASS | All new components use pre-allocation, noexcept |
| III. Modern C++ | PASS | C++20 features, RAII, smart pointers |
| IV. SIMD Optimization | N/A | No inner-loop SIMD required for MVP |
| VI. Cross-Platform | PASS | No platform-specific code |
| VII. Project Structure | PASS | Follows monorepo layout |
| VIII. Testing | PASS | Test-first development mandated |
| IX. Layer Architecture | PASS | Components placed at appropriate layers |
| X. DSP Constraints | PASS | Oversampling not needed; envelope smoothing included |
| XI. Performance Budget | TBD | Will verify < 5% CPU target |
| XV. ODR Prevention | PASS | Searched codebase; no name conflicts found |

### Gate Evaluation

**GATE: FR-001 to FR-085** - All functional requirements are addressable with proposed architecture.

**GATE: SC-001 to SC-012** - All success criteria have clear implementation paths:
- SC-002 (tempo sync): Use BlockContext.tempoToSamples()
- SC-003 (grain density): Poisson process with measured variance
- SC-007 (Legacy compatibility): Delegate to existing FreezeMode
- SC-010 (CPU < 5%): Profile during implementation

---

## Architecture Decisions

### Decision 1: Composition over Inheritance

**Decision**: `PatternFreezeMode` composes rather than inherits from `FreezeMode`.

**Rationale**:
- FreezeMode's internal architecture (FlexibleFeedbackNetwork) doesn't fit pattern-based playback
- Legacy pattern can delegate to a FreezeMode instance
- Cleaner separation of concerns
- Avoids complex inheritance hierarchies

```cpp
class PatternFreezeMode {
    FreezeMode legacyMode_;           // For Legacy pattern type
    RollingCaptureBuffer captureBuffer_;
    PatternScheduler scheduler_;
    SlicePool slicePool_;
    // ... pattern-specific components
};
```

### Decision 2: Pattern Type as Strategy

**Decision**: Each pattern type is implemented as a separate strategy/handler.

**Rationale**:
- Clear separation of pattern-specific logic
- Easy to add new pattern types
- Avoids giant switch statements in process()

```cpp
class IPatternHandler {
    virtual void process(float* left, float* right, size_t n, const BlockContext& ctx) = 0;
    virtual void setParameter(PatternParameter param, float value) = 0;
};

class EuclideanHandler : public IPatternHandler { ... };
class GranularScatterHandler : public IPatternHandler { ... };
class HarmonicDronesHandler : public IPatternHandler { ... };
class NoiseBurstsHandler : public IPatternHandler { ... };
```

### Decision 3: Slice vs Grain Terminology

**Decision**: Use "Slice" for Pattern Freeze to distinguish from existing Granular system.

**Rationale**:
- Slices are larger chunks (10-2000ms) vs grains (10-500ms)
- Slices may have different envelope/playback requirements
- Avoids confusion with GranularEngine/GrainPool
- Enables independent evolution

### Decision 4: Processing Chain Reuse

**Decision**: Reuse `FreezeFeedbackProcessor` for the post-pattern processing chain.

**Rationale**:
- Existing implementation handles pitch shift, diffusion, filter, decay
- Spec requires same processing chain (FR-073)
- Avoids code duplication
- Tested and proven

---

## Component Design

### 1. EuclideanPattern (Layer 0)

```cpp
// dsp/include/krate/dsp/core/euclidean_pattern.h
namespace Krate::DSP {

class EuclideanPattern {
public:
    static constexpr int kMaxSteps = 32;

    /// Generate pattern bitmask
    static uint32_t generate(int pulses, int steps, int rotation = 0) noexcept;

    /// Check if position is a hit in pattern
    static bool isHit(uint32_t pattern, int position, int steps) noexcept;
};

} // namespace Krate::DSP
```

### 2. RollingCaptureBuffer (Layer 1)

```cpp
// dsp/include/krate/dsp/primitives/rolling_capture_buffer.h
namespace Krate::DSP {

class RollingCaptureBuffer {
public:
    static constexpr float kDefaultMaxSeconds = 5.0f;

    void prepare(double sampleRate, float maxSeconds = kDefaultMaxSeconds) noexcept;
    void reset() noexcept;

    /// Write stereo sample (always records, even when frozen)
    void write(float left, float right) noexcept;

    /// Read at delay time with interpolation
    std::pair<float, float> read(float delaySamples) const noexcept;

    /// Get buffer fill level (0-100%)
    [[nodiscard]] float getFillLevel() const noexcept;

    /// Get max delay in samples
    [[nodiscard]] size_t maxDelaySamples() const noexcept;

private:
    DelayLine bufferL_;
    DelayLine bufferR_;
    size_t samplesRecorded_ = 0;
    size_t maxSamples_ = 0;
};

} // namespace Krate::DSP
```

### 3. SlicePool (Layer 1)

```cpp
// dsp/include/krate/dsp/primitives/slice_pool.h
namespace Krate::DSP {

struct Slice {
    float readPosition = 0.0f;
    float playbackRate = 1.0f;
    float envelopePhase = 0.0f;
    float envelopeIncrement = 0.0f;
    float attackIncrement = 0.0f;
    float releaseIncrement = 0.0f;
    float amplitude = 1.0f;
    float panL = 1.0f;
    float panR = 1.0f;
    bool active = false;
    bool inRelease = false;
    size_t startSample = 0;
    float sliceLengthSamples = 0.0f;
};

class SlicePool {
public:
    static constexpr size_t kMaxSlices = 8;  // FR-086

    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    /// Acquire slice, using shortest-remaining stealing if needed
    [[nodiscard]] Slice* acquireSlice(size_t currentSample) noexcept;

    /// Release completed slice
    void releaseSlice(Slice* slice) noexcept;

    /// Get active slices for processing
    [[nodiscard]] std::span<Slice* const> activeSlices() noexcept;

    [[nodiscard]] size_t activeCount() const noexcept;

private:
    std::array<Slice, kMaxSlices> slices_{};
    std::array<Slice*, kMaxSlices> activeList_{};
    size_t activeCount_ = 0;
};

} // namespace Krate::DSP
```

### 4. PatternScheduler (Layer 2)

```cpp
// dsp/include/krate/dsp/processors/pattern_scheduler.h
namespace Krate::DSP {

enum class PatternType : uint8_t {
    Euclidean,
    GranularScatter,
    HarmonicDrones,
    NoiseBursts,
    Legacy
};

class PatternScheduler {
public:
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // Pattern type
    void setPatternType(PatternType type) noexcept;

    // Euclidean parameters
    void setEuclideanParams(int steps, int hits, int rotation) noexcept;
    void setPatternRate(NoteValue note, NoteModifier mod = NoteModifier::None) noexcept;

    // Granular parameters
    void setGranularDensity(float hz) noexcept;

    // Process one sample, returns true if trigger
    [[nodiscard]] bool process(const BlockContext& ctx) noexcept;

    // Check if tempo is valid for synced patterns
    [[nodiscard]] bool isTempoValid(const BlockContext& ctx) const noexcept;

private:
    PatternType type_ = PatternType::Legacy;

    // Euclidean state
    uint32_t euclideanPattern_ = 0;
    int euclideanSteps_ = 8;
    int euclideanHits_ = 3;
    int euclideanRotation_ = 0;
    int currentStep_ = 0;
    float samplesPerStep_ = 0.0f;
    float stepAccumulator_ = 0.0f;

    // Granular state
    float granularDensity_ = 10.0f;
    float samplesUntilNextGrain_ = 0.0f;
    Xorshift32 rng_{12345};

    double sampleRate_ = 44100.0;
};

} // namespace Krate::DSP
```

### 5. PatternFreezeMode (Layer 4)

```cpp
// dsp/include/krate/dsp/effects/pattern_freeze_mode.h
namespace Krate::DSP {

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

    // Query
    [[nodiscard]] float getCaptureBufferFillLevel() const noexcept;
    [[nodiscard]] size_t getLatencySamples() const noexcept;

    // Processing
    void process(float* left, float* right, size_t numSamples,
                 const BlockContext& ctx) noexcept;

private:
    // Components
    RollingCaptureBuffer captureBuffer_;
    PatternScheduler scheduler_;
    SlicePool slicePool_;
    FreezeFeedbackProcessor feedbackProcessor_;
    FreezeMode legacyMode_;  // For Legacy pattern type

    // Drone-specific components
    std::array<PitchShiftProcessor, 4> droneShifters_;
    std::array<LFO, 4> droneLFOs_;

    // Noise burst components
    NoiseGenerator noiseGen_;
    Biquad noiseFilter_;

    // Crossfade for pattern type switching
    LinearRamp patternCrossfade_;
    PatternType previousPatternType_ = PatternType::Legacy;

    // Parameter smoothers
    OnePoleSmoother sliceLengthSmoother_;
    OnePoleSmoother envelopeAttackSmoother_;
    OnePoleSmoother envelopeReleaseSmoother_;

    // State
    bool freezeEnabled_ = false;
    PatternType patternType_ = PatternType::Legacy;
    double sampleRate_ = 44100.0;
    size_t maxBlockSize_ = 512;

    // Scratch buffers
    std::vector<float> scratchL_;
    std::vector<float> scratchR_;
    std::vector<float> mixBufferL_;
    std::vector<float> mixBufferR_;
};

} // namespace Krate::DSP
```

---

## Implementation Phases

### Phase 1: Core Infrastructure (Week 1)

**Tasks**:
1. [ ] Implement `EuclideanPattern` (Layer 0)
   - Unit tests for pattern generation
   - Verify classic patterns: E(3,8) = tresillo, E(5,8) = cinquillo

2. [ ] Implement `RollingCaptureBuffer` (Layer 1)
   - Unit tests for continuous recording
   - Test fill level tracking
   - Test stereo read with interpolation

3. [ ] Implement `SlicePool` (Layer 1)
   - Unit tests for voice acquisition
   - Test shortest-remaining voice stealing
   - Verify max 8 slices (FR-086)

**Deliverables**:
- `dsp/include/krate/dsp/core/euclidean_pattern.h`
- `dsp/include/krate/dsp/primitives/rolling_capture_buffer.h`
- `dsp/include/krate/dsp/primitives/slice_pool.h`
- Corresponding unit tests

### Phase 2: Pattern Scheduling (Week 2)

**Tasks**:
1. [ ] Implement `PatternScheduler` (Layer 2)
   - Euclidean tempo-synced scheduling
   - Poisson process for Granular Scatter
   - Unit tests for trigger timing accuracy

2. [ ] Implement `SlicePlayer` (Layer 2)
   - Slice playback with configurable envelope
   - Linear and Exponential envelope shapes
   - Unit tests for click-free playback

3. [ ] Extend `GrainEnvelope` with Linear/Exponential types

**Deliverables**:
- `dsp/include/krate/dsp/processors/pattern_scheduler.h`
- `dsp/include/krate/dsp/processors/slice_player.h`
- Extended `grain_envelope.h`
- Unit tests

### Phase 3: Pattern Handlers (Week 3)

**Tasks**:
1. [ ] Implement Euclidean pattern handler
   - Integration with PatternScheduler
   - Tempo sync verification

2. [ ] Implement Granular Scatter handler
   - Poisson triggering
   - Position/size jitter

3. [ ] Implement Harmonic Drones handler
   - Multi-voice pitch shifting
   - Drift modulation

4. [ ] Implement Noise Bursts handler
   - Rhythmic noise generation
   - Filter sweep

**Deliverables**:
- Pattern handler implementations within PatternFreezeMode
- Integration tests for each pattern type

### Phase 4: Integration & Legacy (Week 4)

**Tasks**:
1. [ ] Implement `PatternFreezeMode` (Layer 4)
   - Compose all components
   - Pattern type crossfade (FR-015)
   - Processing chain integration (FR-073)

2. [ ] Legacy pattern implementation
   - Delegate to existing FreezeMode
   - Verify identical behavior (SC-007)

3. [ ] Parameter smoothing for all controls

4. [ ] Edge case handling
   - Tempo invalid behavior
   - Buffer underflow
   - Pattern type transitions

**Deliverables**:
- `dsp/include/krate/dsp/effects/pattern_freeze_mode.h`
- Full integration tests
- Approval tests for Legacy compatibility

### Phase 5: Plugin Integration (Week 5)

**Tasks**:
1. [ ] Add parameters to `plugin_ids.h`
2. [ ] Integrate into `Processor`
3. [ ] Add UI controls to `editor.uidesc`
4. [ ] Register parameters in `Controller`
5. [ ] Run pluginval validation

**Deliverables**:
- Plugin parameter IDs
- UI layout
- Pluginval report

### Phase 6: Testing & Polish (Week 6)

**Tasks**:
1. [ ] Performance profiling (SC-010: < 5% CPU)
2. [ ] Latency verification (SC-009: < 3ms)
3. [ ] Memory usage check (SC-008: < 7.68 MB)
4. [ ] Complete compliance table
5. [ ] Documentation

**Deliverables**:
- Performance report
- Compliance verification
- Updated architecture docs

---

## Testing Strategy

### Unit Tests

| Component | Test Focus |
|-----------|------------|
| EuclideanPattern | Pattern generation, rotation, edge cases |
| RollingCaptureBuffer | Fill level, stereo read, max capacity |
| SlicePool | Acquisition, shortest-remaining stealing |
| PatternScheduler | Trigger timing, Poisson distribution |
| SlicePlayer | Envelope shapes, click-free playback |

### Integration Tests

| Test | Requirement | Method |
|------|-------------|--------|
| Euclidean tempo sync | SC-002 | Measure trigger accuracy vs BlockContext |
| Granular density | SC-003 | 10-second average, verify +/- 20% |
| Drone level | SC-004 | Multi-voice output vs single-voice |
| Pattern crossfade | SC-005 | Detect clicks during transition |
| Slice boundaries | SC-006 | Verify no clicks at 10ms envelope |
| Legacy identical | SC-007 | Approval test vs FreezeMode output |

### Approval Tests

```cpp
// Legacy compatibility test
TEST_CASE("PatternFreezeMode Legacy matches FreezeMode") {
    PatternFreezeMode patternFreeze;
    FreezeMode legacyFreeze;

    // Configure identically
    // Process same input
    // Compare output within floating-point tolerance
    REQUIRE_THAT(patternOutput, Catch::Matchers::Approx(legacyOutput).margin(1e-5f));
}
```

### Performance Tests

```cpp
TEST_CASE("PatternFreezeMode CPU usage") {
    // Process 10 seconds of audio
    // Measure wall clock time
    // Verify < 5% of real-time at 44.1kHz
}
```

---

## Risk Assessment

### High Risk

| Risk | Mitigation |
|------|------------|
| CPU budget exceeded with 8 grains | Profile early; optimize envelope lookup; consider SIMD for mixing |
| Poisson triggering too irregular | Add density smoothing; allow user control of regularity |
| Legacy compatibility issues | Early approval tests; parallel development with FreezeMode |

### Medium Risk

| Risk | Mitigation |
|------|------------|
| Pattern crossfade audible | Test with various pattern transitions; adjust crossfade time |
| Tempo sync drift | Use high-precision timing; sync at bar boundaries |
| Voice stealing clicks | Apply micro-fade on stolen voices |

### Low Risk

| Risk | Mitigation |
|------|------------|
| Buffer memory usage | Already calculated: 7.68 MB max is acceptable |
| Parameter smoothing glitches | Reuse proven OnePoleSmoother |

---

## Sources

### Algorithm References
- [Toussaint's Euclidean Rhythms Paper](https://cgm.cs.mcgill.ca/~godfried/publications/banff.pdf)
- [Paul Batchelor's sndkit](https://paulbatchelor.github.io/sndkit/euclid/)
- [Ross Bencina's Granular Synthesis](http://www.rossbencina.com/writings)
- [Grain Envelope Design](https://www.granularsynthesis.com/hthesis/envelope.html)

### Implementation References
- [Bjorklund C++ Gist](https://gist.github.com/unohee/d4f32b3222b42de84a5f)
- [C++ exponential_distribution](https://en.cppreference.com/w/cpp/numeric/random/exponential_distribution.html)
- [Lock-free Ring Buffer](https://kmdreko.github.io/posts/20191003/a-simple-lock-free-ring-buffer/)
- [Voice Stealing Discussion](https://www.kvraudio.com/forum/viewtopic.php?t=91557)

### Audio/Music References
- [Granular Synthesis Wikipedia](https://en.wikipedia.org/wiki/Granular_synthesis)
- [Native Instruments Granular Guide](https://blog.native-instruments.com/granular-synthesis/)
- [Pitchometry Plugin](https://aegeanmusic.com/pitchometry)

---

## Appendix: Parameter IDs

Following CLAUDE.md naming convention `k{Mode}{Parameter}Id`:

```cpp
// Pattern Freeze Parameters (in plugin_ids.h)

// Pattern Type & Core
kFreezePatternTypeId,           // PatternType enum (0-4)
kFreezeSliceLengthId,           // 10-2000 ms
kFreezeSliceModeId,             // Fixed/Variable

// Euclidean Parameters
kFreezeEuclideanStepsId,        // 2-32
kFreezeEuclideanHitsId,         // 1-steps
kFreezeEuclideanRotationId,     // 0-(steps-1)
kFreezePatternRateId,           // NoteValue dropdown

// Granular Scatter Parameters
kFreezeGranularDensityId,       // 1-50 Hz
kFreezeGranularPositionJitterId,// 0-100%
kFreezeGranularSizeJitterId,    // 0-100%
kFreezeGranularGrainSizeId,     // 10-500 ms

// Harmonic Drones Parameters
kFreezeDroneVoiceCountId,       // 1-4
kFreezeDroneIntervalId,         // PitchInterval enum
kFreezeDroneDriftId,            // 0-100%
kFreezeDroneDriftRateId,        // 0.1-2.0 Hz

// Noise Bursts Parameters
kFreezeNoiseColorId,            // NoiseColor enum
kFreezeNoiseBurstRateId,        // NoteValue dropdown
kFreezeNoiseFilterTypeId,       // FilterType enum
kFreezeNoiseFilterCutoffId,     // 20-20000 Hz
kFreezeNoiseFilterSweepId,      // 0-100%

// Envelope Parameters
kFreezeEnvelopeAttackId,        // 0-500 ms
kFreezeEnvelopeReleaseId,       // 0-2000 ms
kFreezeEnvelopeShapeId,         // EnvelopeShape enum
```

---

*Plan generated: 2026-01-16*
*Plan version: 1.0*
