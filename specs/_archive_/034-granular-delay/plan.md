# Implementation Plan: Granular Delay

**Branch**: `034-granular-delay` | **Date**: 2025-12-27 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `specs/034-granular-delay/spec.md`

## Summary

Implement a **Granular Delay** effect that breaks incoming audio into discrete grains (10-500ms) and reassembles them with per-grain pitch shifting, position randomization, reverse playback probability, and density control. Uses a tapped delay line architecture with freeze capability for infinite sustain textures.

The implementation follows a strict 5-layer architecture with reusable components at each level:
- **Layer 0**: Grain envelope lookup tables (new)
- **Layer 1**: GrainPool voice management (new)
- **Layer 2**: GrainScheduler + GrainProcessor (new)
- **Layer 3**: GranularEngine (new)
- **Layer 4**: GranularDelay user feature (new)

## Technical Context

**Language/Version**: C++20 (aligned with existing codebase)
**Primary Dependencies**: VST3 SDK, VSTGUI (existing), Layer 0-3 DSP infrastructure
**Storage**: N/A (real-time audio processing only)
**Testing**: Catch2 via CTest (matches existing test infrastructure)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: Single VST3 plugin
**Performance Goals**: <3% CPU at 44.1kHz stereo with 32 simultaneous grains (SC-005)
**Constraints**: 64 max grains, 2-second delay buffer, real-time safe (no allocations in process)
**Scale/Scope**: Final Layer 4 feature per roadmap

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Safety):**
- [x] No allocations in audio callbacks (pre-allocate in prepare())
- [x] No locks/mutexes in audio thread (use atomics for parameters)
- [x] No exceptions in processing paths (noexcept everywhere)

**Required Check - Principle IX (Layered Architecture):**
- [x] Layer 0 components have no dependencies on higher layers
- [x] Each layer only depends on layers below it
- [x] No circular dependencies between layers

**Required Check - Principle XII (Test-First Development):**
- [x] Tasks will include TESTING-GUIDE.md context verification step
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**:

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| Grain | `grep -r "class Grain" src/` | No | Create New |
| GrainPool | `grep -r "class.*Pool" src/` | No | Create New |
| GrainScheduler | `grep -r "class.*Scheduler" src/` | No | Create New |
| GrainProcessor | `grep -r "GrainProcessor" src/` | No | Create New |
| GranularEngine | `grep -r "GranularEngine" src/` | No | Create New |
| GranularDelay | `grep -r "GranularDelay" src/` | No | Create New |
| GrainEnvelopeType | `grep -r "EnvelopeType\|GrainEnvelope" src/` | Partial (WindowType exists) | Extend WindowType |

**Utility Functions to be created**:

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| generateTrapezoid | `grep -r "generateTrapezoid" src/` | No | - | Create in grain_envelope.h |
| semitonesToRatio | `grep -r "semitonesToRatio" src/` | No | - | Create in pitch_utils.h (Layer 0) |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| DelayLine | dsp/primitives/delay_line.h | 1 | Circular buffer for grain tap reading |
| DelayLine::readLinear | dsp/primitives/delay_line.h | 1 | Fractional delay for pitch-shifted grains |
| OnePoleSmoother | dsp/primitives/smoother.h | 1 | Parameter smoothing (all params) |
| LinearRamp | dsp/primitives/smoother.h | 1 | Freeze crossfade transitions |
| Xorshift32 | dsp/core/random.h | 0 | Grain position/pitch/pan randomization |
| WindowType | dsp/core/window_functions.h | 0 | Hann envelope (via generateHann) |
| Window::generateHann | dsp/core/window_functions.h | 0 | Pre-compute Hann grain envelope |
| flushDenormal | dsp/core/db_utils.h | 0 | Prevent denormal slowdowns |
| dbToGain | dsp/core/db_utils.h | 0 | Output gain parameter |

### Files Checked for Conflicts

- [x] `src/dsp/dsp_utils.h` - No grain-related code
- [x] `src/dsp/core/` - Has random.h, window_functions.h to reuse
- [x] `ARCHITECTURE.md` - No granular components listed
- [x] `src/dsp/primitives/` - No grain-related primitives
- [x] `src/dsp/processors/` - No granular processors
- [x] `src/dsp/features/` - No granular features

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types are unique and not found in codebase. WindowType enum exists but will be used as-is (not duplicated). No existing grain-related code to conflict with.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| DelayLine | prepare | `void prepare(double sampleRate, float maxDelaySeconds) noexcept` | ✓ |
| DelayLine | write | `void write(float sample) noexcept` | ✓ |
| DelayLine | readLinear | `[[nodiscard]] float readLinear(float delaySamples) const noexcept` | ✓ |
| DelayLine | reset | `void reset() noexcept` | ✓ |
| DelayLine | maxDelaySamples | `[[nodiscard]] size_t maxDelaySamples() const noexcept` | ✓ |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | ✓ |
| OnePoleSmoother | setTarget | `ITERUM_NOINLINE void setTarget(float target) noexcept` | ✓ |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | ✓ |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | ✓ |
| LinearRamp | configure | `void configure(float rampTimeMs, float sampleRate) noexcept` | ✓ |
| LinearRamp | setTarget | `ITERUM_NOINLINE void setTarget(float target) noexcept` | ✓ |
| LinearRamp | process | `[[nodiscard]] float process() noexcept` | ✓ |
| Xorshift32 | nextUnipolar | `[[nodiscard]] constexpr float nextUnipolar() noexcept` | ✓ |
| Xorshift32 | nextFloat | `[[nodiscard]] constexpr float nextFloat() noexcept` | ✓ |
| Window::generateHann | - | `void generateHann(float* output, size_t size) noexcept` | ✓ |

### Header Files Read

- [x] `src/dsp/primitives/delay_line.h` - DelayLine class
- [x] `src/dsp/primitives/smoother.h` - OnePoleSmoother, LinearRamp classes
- [x] `src/dsp/core/random.h` - Xorshift32 class
- [x] `src/dsp/core/window_functions.h` - WindowType, Window::generateHann

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| DelayLine | Uses `readLinear()` for fractional, not `read()` | `delay.readLinear(fractionalSamples)` |
| OnePoleSmoother | Uses `snapTo()` not `snap()` | `smoother.snapTo(value)` |
| Xorshift32 | `nextFloat()` returns [-1,1], `nextUnipolar()` returns [0,1] | Use `nextUnipolar()` for probabilities |
| Window::generateHann | Requires pre-allocated buffer | Pre-allocate in prepare() |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| semitonesToRatio | Pure math, reusable for any pitch shifting | pitch_utils.h | GrainProcessor, future pitch effects |
| GrainEnvelope tables | Pre-computed lookup, reusable | grain_envelope.h | GrainProcessor, future granular |
| generateTrapezoid | Window function for grains | grain_envelope.h | GrainProcessor |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| msToSamples | One-liner, each class stores sampleRate_ |
| calculateInteronset | Specific to scheduler timing |

**Decision**: Create `pitch_utils.h` (Layer 0) for pitch math and `grain_envelope.h` (Layer 0) for envelope tables. These are pure, stateless utilities needed by multiple components.

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 4 - User Features

**Related features at same layer** (from ROADMAP.md):
- None planned - this is the FINAL Layer 4 feature per roadmap

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| GrainPool | HIGH | Future particle/cloud effects, sample playback | Keep modular for extraction |
| GrainEnvelope tables | MEDIUM | Future STFT windows, crossfade | Already in Layer 0 |
| Xorshift32 (existing) | HIGH | Already reused | N/A - already shared |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| GrainPool as standalone primitive | Voice management pattern is widely reusable |
| Envelope tables in Layer 0 | Pure math, no state, multiple consumers |
| No shared Layer 4 base class | Final Layer 4 feature - no siblings to share with |

### Review Trigger

N/A - This is the final Layer 4 feature. No future siblings to trigger review.

## Project Structure

### Documentation (this feature)

```text
specs/034-granular-delay/
├── spec.md              # Feature specification
├── plan.md              # This implementation plan
├── research.md          # Granular synthesis research (already created)
├── checklists/
│   └── requirements.md  # Specification quality checklist
└── tasks.md             # Task breakdown (created by /speckit.tasks)
```

### Source Code (repository root)

```text
src/dsp/
├── core/                           # Layer 0: Core Utilities
│   ├── pitch_utils.h               # NEW: semitonesToRatio, ratioToSemitones
│   └── grain_envelope.h            # NEW: GrainEnvelopeType, envelope tables
│
├── primitives/                     # Layer 1: DSP Primitives
│   └── grain_pool.h                # NEW: Pre-allocated grain state + voice stealing
│
├── processors/                     # Layer 2: DSP Processors
│   ├── grain_scheduler.h           # NEW: Timing controller (sync/async)
│   └── grain_processor.h           # NEW: Single grain processing
│
├── systems/                        # Layer 3: System Components
│   └── granular_engine.h           # NEW: Combines pool + scheduler + buffer
│
└── features/                       # Layer 4: User Features
    └── granular_delay.h            # NEW: Complete granular delay effect

tests/unit/
├── core/
│   ├── test_pitch_utils.cpp        # NEW
│   └── test_grain_envelope.cpp     # NEW
├── primitives/
│   └── test_grain_pool.cpp         # NEW
├── processors/
│   ├── test_grain_scheduler.cpp    # NEW
│   └── test_grain_processor.cpp    # NEW
├── systems/
│   └── test_granular_engine.cpp    # NEW
└── features/
    └── test_granular_delay.cpp     # NEW
```

**Structure Decision**: Follow existing layered structure. Each new component gets its own header in the appropriate layer directory with corresponding test file.

## Layered Architecture Design

### Layer 0: Core Utilities (New Files)

#### pitch_utils.h

```cpp
// Layer 0: Core Utility - Pitch Conversion
namespace Iterum::DSP {
    /// Convert semitones to playback rate ratio
    /// +12 semitones = 2.0 (octave up), -12 = 0.5 (octave down)
    [[nodiscard]] constexpr float semitonesToRatio(float semitones) noexcept;

    /// Convert playback rate ratio to semitones
    [[nodiscard]] constexpr float ratioToSemitones(float ratio) noexcept;
}
```

#### grain_envelope.h

```cpp
// Layer 0: Core Utility - Grain Envelope Tables
namespace Iterum::DSP {
    /// Grain envelope types (extends WindowType concept for grain-specific needs)
    enum class GrainEnvelopeType : uint8_t {
        Hann,           ///< Raised cosine (smooth, general purpose)
        Trapezoid,      ///< Attack-sustain-decay (preserves transients)
        Sine,           ///< Half-cosine (better for pitch shifting)
        Blackman        ///< Low sidelobe (less coloration)
    };

    namespace GrainEnvelope {
        /// Pre-compute envelope lookup table (call in prepare, not process)
        void generate(float* output, size_t size, GrainEnvelopeType type,
                      float attackRatio = 0.1f, float releaseRatio = 0.1f) noexcept;

        /// Get envelope value at normalized phase [0, 1]
        [[nodiscard]] float lookup(const float* table, size_t tableSize,
                                    float phase) noexcept;
    }
}
```

### Layer 1: DSP Primitives (New Files)

#### grain_pool.h

```cpp
// Layer 1: DSP Primitive - Grain Pool
namespace Iterum::DSP {

    /// State of a single grain
    struct Grain {
        float readPosition = 0.0f;      ///< Current position in delay buffer (samples)
        float playbackRate = 1.0f;      ///< Samples to advance per output sample
        float envelopePhase = 0.0f;     ///< Progress through envelope [0, 1]
        float envelopeIncrement = 0.0f; ///< Phase advance per sample
        float amplitude = 1.0f;         ///< Grain volume
        float panL = 1.0f;              ///< Left channel gain
        float panR = 1.0f;              ///< Right channel gain
        bool active = false;            ///< Is grain currently playing
        bool reverse = false;           ///< Play backwards
        size_t startSample = 0;         ///< Sample when grain was triggered (for age)
    };

    /// Pre-allocated grain pool with voice stealing
    class GrainPool {
    public:
        static constexpr size_t kMaxGrains = 64;

        void prepare(double sampleRate) noexcept;
        void reset() noexcept;

        /// Acquire a grain from pool. Returns nullptr if none available after stealing.
        [[nodiscard]] Grain* acquireGrain(size_t currentSample) noexcept;

        /// Release a grain back to pool
        void releaseGrain(Grain* grain) noexcept;

        /// Get all active grains for processing
        [[nodiscard]] std::span<Grain*> activeGrains() noexcept;

        /// Get count of active grains
        [[nodiscard]] size_t activeCount() const noexcept;

    private:
        std::array<Grain, kMaxGrains> grains_;
        std::array<Grain*, kMaxGrains> activeList_;
        size_t activeCount_ = 0;
        double sampleRate_ = 44100.0;
    };
}
```

### Layer 2: DSP Processors (New Files)

#### grain_scheduler.h

```cpp
// Layer 2: DSP Processor - Grain Scheduler
namespace Iterum::DSP {

    /// Scheduling mode for grain triggering
    enum class SchedulingMode : uint8_t {
        Asynchronous,   ///< Stochastic timing based on density
        Synchronous     ///< Regular intervals for pitched output
    };

    /// Controls when grains are triggered
    class GrainScheduler {
    public:
        void prepare(double sampleRate) noexcept;
        void reset() noexcept;

        /// Set grain density (grains per second)
        void setDensity(float grainsPerSecond) noexcept;

        /// Set scheduling mode
        void setMode(SchedulingMode mode) noexcept;

        /// Process one sample. Returns true if new grain should be triggered.
        [[nodiscard]] bool process() noexcept;

        /// Seed RNG for reproducible behavior
        void seed(uint32_t seedValue) noexcept;

    private:
        float samplesUntilNextGrain_ = 0.0f;
        float density_ = 10.0f;  // grains/sec
        SchedulingMode mode_ = SchedulingMode::Asynchronous;
        Xorshift32 rng_{12345};
        double sampleRate_ = 44100.0;
    };
}
```

#### grain_processor.h

```cpp
// Layer 2: DSP Processor - Single Grain Processor
namespace Iterum::DSP {

    /// Processes a single grain with envelope, pitch, and panning
    class GrainProcessor {
    public:
        void prepare(double sampleRate, size_t maxEnvelopeSize = 2048) noexcept;
        void reset() noexcept;

        /// Configure grain parameters
        struct GrainParams {
            float grainSizeMs = 100.0f;
            float pitchSemitones = 0.0f;
            float positionSamples = 0.0f;
            float pan = 0.0f;  // -1 = L, 0 = center, +1 = R
            bool reverse = false;
            GrainEnvelopeType envelopeType = GrainEnvelopeType::Hann;
        };

        /// Initialize a grain with parameters
        void initializeGrain(Grain& grain, const GrainParams& params) noexcept;

        /// Process one sample for a grain, reading from delay buffer
        /// Returns {left, right} output
        [[nodiscard]] std::pair<float, float> processGrain(
            Grain& grain,
            const DelayLine& delayBuffer) noexcept;

        /// Check if grain has completed
        [[nodiscard]] bool isGrainComplete(const Grain& grain) const noexcept;

    private:
        std::vector<float> envelopeTable_;
        size_t envelopeTableSize_ = 0;
        double sampleRate_ = 44100.0;
    };
}
```

### Layer 3: System Components (New Files)

#### granular_engine.h

```cpp
// Layer 3: System Component - Granular Engine
namespace Iterum::DSP {

    /// Core granular processing engine combining pool, scheduler, and buffer
    class GranularEngine {
    public:
        void prepare(double sampleRate, float maxDelaySeconds = 2.0f) noexcept;
        void reset() noexcept;

        /// Parameters (call from UI thread, atomic internally)
        void setGrainSize(float ms) noexcept;
        void setDensity(float grainsPerSecond) noexcept;
        void setPitch(float semitones) noexcept;
        void setPitchSpray(float amount) noexcept;  // 0-1
        void setPosition(float ms) noexcept;
        void setPositionSpray(float amount) noexcept;  // 0-1
        void setReverseProbability(float probability) noexcept;  // 0-1
        void setPanSpray(float amount) noexcept;  // 0-1
        void setEnvelopeType(GrainEnvelopeType type) noexcept;
        void setFreeze(bool frozen) noexcept;

        /// Process stereo (most common case)
        void process(float inputL, float inputR,
                     float& outputL, float& outputR) noexcept;

        /// Query active grain count
        [[nodiscard]] size_t activeGrainCount() const noexcept;

    private:
        DelayLine delayL_;
        DelayLine delayR_;
        GrainPool pool_;
        GrainScheduler scheduler_;
        GrainProcessor processor_;
        Xorshift32 rng_{54321};

        // Smoothed parameters
        OnePoleSmoother grainSizeSmoother_;
        OnePoleSmoother pitchSmoother_;
        OnePoleSmoother positionSmoother_;

        // Freeze state
        LinearRamp freezeCrossfade_;
        bool frozen_ = false;

        // Current parameters (raw values)
        float grainSizeMs_ = 100.0f;
        float pitchSemitones_ = 0.0f;
        float pitchSpray_ = 0.0f;
        float positionMs_ = 500.0f;
        float positionSpray_ = 0.0f;
        float reverseProbability_ = 0.0f;
        float panSpray_ = 0.0f;
        GrainEnvelopeType envelopeType_ = GrainEnvelopeType::Hann;

        size_t currentSample_ = 0;
        double sampleRate_ = 44100.0;
    };
}
```

### Layer 4: User Features (New Files)

#### granular_delay.h

```cpp
// Layer 4: User Feature - Granular Delay
namespace Iterum::DSP {

    /// Complete granular delay effect with all parameters
    class GranularDelay {
    public:
        void prepare(double sampleRate) noexcept;
        void reset() noexcept;

        // === Core Parameters ===
        void setGrainSize(float ms) noexcept;         // 10-500ms
        void setDensity(float grainsPerSec) noexcept; // 1-100 Hz
        void setDelayTime(float ms) noexcept;         // 0-2000ms
        void setPositionSpray(float amount) noexcept; // 0-1

        // === Pitch Parameters ===
        void setPitch(float semitones) noexcept;      // -24 to +24
        void setPitchSpray(float amount) noexcept;    // 0-1

        // === Modifiers ===
        void setReverseProbability(float prob) noexcept; // 0-1
        void setPanSpray(float amount) noexcept;      // 0-1
        void setEnvelopeType(GrainEnvelopeType type) noexcept;

        // === Global Controls ===
        void setFreeze(bool frozen) noexcept;
        void setFeedback(float amount) noexcept;      // 0-1.2
        void setDryWet(float mix) noexcept;           // 0-1
        void setOutputGain(float dB) noexcept;        // -inf to +6

        // === Processing ===
        void process(float* leftIn, float* rightIn,
                     float* leftOut, float* rightOut,
                     size_t numSamples) noexcept;

        // === Latency ===
        [[nodiscard]] size_t getLatencySamples() const noexcept;

    private:
        GranularEngine engine_;

        // Feedback path
        float feedbackL_ = 0.0f;
        float feedbackR_ = 0.0f;

        // Smoothed output controls
        OnePoleSmoother feedbackSmoother_;
        OnePoleSmoother dryWetSmoother_;
        OnePoleSmoother gainSmoother_;

        // Raw parameter values
        float feedback_ = 0.0f;
        float dryWet_ = 0.5f;
        float outputGainLinear_ = 1.0f;

        double sampleRate_ = 44100.0;
    };
}
```

## Complexity Tracking

No constitution violations requiring justification. All principles are satisfied:
- Layer separation maintained
- No allocations in process()
- Test-first approach planned
- ODR prevention completed

## Implementation Phases

### Phase 1: Layer 0 Foundations
1. Create `pitch_utils.h` with semitone conversion
2. Create `grain_envelope.h` with envelope generation and lookup
3. Write tests for both

### Phase 2: Layer 1 Voice Management
1. Create `grain_pool.h` with Grain struct and GrainPool class
2. Implement voice stealing (oldest grain)
3. Write comprehensive tests

### Phase 3: Layer 2 Grain Processing
1. Create `grain_scheduler.h` with sync/async modes
2. Create `grain_processor.h` with envelope and pitch shifting
3. Write tests for both

### Phase 4: Layer 3 Engine Integration
1. Create `granular_engine.h` combining all components
2. Implement parameter smoothing
3. Implement freeze mode with crossfade
4. Write integration tests

### Phase 5: Layer 4 User Feature
1. Create `granular_delay.h` with full parameter set
2. Implement feedback path with soft limiting
3. Implement dry/wet mix and output gain
4. Write feature tests

### Phase 6: Integration & Validation
1. Add to processor.cpp
2. Add parameters to controller.cpp
3. Run all tests
4. Create benchmark test for SC-005
5. Update ARCHITECTURE.md

## Next Steps

1. Run `/speckit.tasks` to generate detailed task breakdown
2. Begin Phase 1 implementation following test-first methodology
