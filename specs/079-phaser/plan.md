# Implementation Plan: Phaser Effect Processor

**Branch**: `079-phaser` | **Date**: 2026-01-22 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/079-phaser/spec.md`

**Note**: This template is filled in by the `/speckit.plan` command. See `.specify/templates/commands/plan.md` for the execution workflow.

## Summary

A classic phaser effect processor for the KrateDSP library (Phase 10 of the Filter Implementation Roadmap). The phaser cascades 2-12 first-order allpass filter stages with LFO modulation to create characteristic sweeping notches. Key design decisions: mix-before-feedback topology, center/depth frequency API with internal min/max calculation, exponential LFO-to-frequency mapping for perceptually even sweeps, and tanh soft-clipping for feedback stability.

## Technical Context

**Language/Version**: C++20 (per constitution)
**Primary Dependencies**:
- `Allpass1Pole` (Layer 1 - `dsp/include/krate/dsp/primitives/allpass_1pole.h`)
- `LFO` (Layer 1 - `dsp/include/krate/dsp/primitives/lfo.h`)
- `OnePoleSmoother` (Layer 1 - `dsp/include/krate/dsp/primitives/smoother.h`)
- `NoteValue/NoteModifier` (Layer 0 - `dsp/include/krate/dsp/core/note_value.h`)
**Storage**: N/A (stateless processor with internal filter state)
**Testing**: Catch2 (per existing test infrastructure in `dsp/tests/unit/`)
**Target Platform**: Windows, macOS, Linux (cross-platform per constitution VI)
**Project Type**: Single DSP library component
**Performance Goals**: < 0.5% CPU for typical use (SC-001: < 1ms for 1 second at 44.1kHz with 12 stages)
**Constraints**: Real-time safe (no allocations in process), layer 2 dependencies only
**Scale/Scope**: Single header-only processor class

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle II (Real-Time Audio Thread Safety):**
- [x] No memory allocation in process methods
- [x] No locks, mutexes, or blocking primitives
- [x] No file I/O, network ops, or system calls
- [x] No throw/catch exceptions
- [x] Pre-allocate all buffers in prepare()

**Principle III (Modern C++ Standards):**
- [x] C++20 features where SDK-compatible
- [x] RAII for resource management
- [x] Smart pointers where needed
- [x] constexpr and const usage
- [x] Value semantics preferred

**Principle VI (Cross-Platform Compatibility):**
- [x] No platform-specific code
- [x] Uses portable VSTGUI/VST3 abstractions
- [x] Builds on Windows, macOS, Linux

**Principle IX (Layered DSP Architecture):**
- [x] Layer 2 processor depends only on Layers 0-1
- [x] No circular dependencies
- [x] Independently testable

**Principle X (DSP Processing Constraints):**
- [x] Feedback > 100% includes soft limiting (tanh)
- [x] DC blocking handled by allpass nature (unity gain at all frequencies)

**Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Principle XVI (Honest Completion):**
- [x] All FR-xxx and SC-xxx will be verified
- [x] No relaxed thresholds
- [x] No placeholder values

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: `Phaser`

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| Phaser | `grep -r "class Phaser" dsp/ plugins/` | No (only in FLT-ROADMAP.md) | Create New |

**Utility Functions to be created**: None (all utilities exist in Layer 0/1)

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| Allpass1Pole | dsp/include/krate/dsp/primitives/allpass_1pole.h | 1 | Core building block - 2-12 cascaded stages for phase shifting |
| LFO | dsp/include/krate/dsp/primitives/lfo.h | 1 | Modulation source with tempo sync capability |
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | 1 | Parameter smoothing (rate, depth, feedback, mix) |
| Waveform enum | dsp/include/krate/dsp/primitives/lfo.h | 1 | LFO waveform selection (Sine, Triangle, Sawtooth, Square) |
| NoteValue | dsp/include/krate/dsp/core/note_value.h | 0 | Tempo sync note values |
| NoteModifier | dsp/include/krate/dsp/core/note_value.h | 0 | Tempo sync note modifiers (dotted, triplet) |
| kPi | dsp/include/krate/dsp/core/math_constants.h | 0 | Mathematical constant for frequency calculations |
| detail::flushDenormal | dsp/include/krate/dsp/core/db_utils.h | 0 | Denormal flushing in feedback path |
| detail::isNaN/isInf | dsp/include/krate/dsp/core/db_utils.h | 0 | NaN/Inf input detection |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (no existing Phaser)
- [x] `specs/_architecture_/` - Component inventory (README.md for index, layer files for details)
- [x] `specs/FLT-ROADMAP.md` - Filter roadmap (Phase 10 is Phaser)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: No existing `Phaser` class found in codebase. All dependencies are well-established Layer 0/1 components. The Phaser is explicitly planned in the Filter Roadmap as a new Layer 2 processor.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins. Prevents compile-time API mismatch errors.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| Allpass1Pole | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| Allpass1Pole | setFrequency | `void setFrequency(float hz) noexcept` | Yes |
| Allpass1Pole | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| Allpass1Pole | reset | `void reset() noexcept` | Yes |
| LFO | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| LFO | process | `[[nodiscard]] float process() noexcept` | Yes |
| LFO | setFrequency | `void setFrequency(float hz) noexcept` | Yes |
| LFO | setWaveform | `void setWaveform(Waveform waveform) noexcept` | Yes |
| LFO | setPhaseOffset | `void setPhaseOffset(float degrees) noexcept` | Yes |
| LFO | setTempoSync | `void setTempoSync(bool enabled) noexcept` | Yes |
| LFO | setTempo | `void setTempo(float bpm) noexcept` | Yes |
| LFO | setNoteValue | `void setNoteValue(NoteValue value, NoteModifier modifier = NoteModifier::None) noexcept` | Yes |
| LFO | reset | `void reset() noexcept` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `ITERUM_NOINLINE void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| OnePoleSmoother | reset | `void reset() noexcept` | Yes |
| Waveform | enum values | `enum class Waveform : uint8_t { Sine = 0, Triangle, Sawtooth, Square, ... }` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/allpass_1pole.h` - Allpass1Pole class
- [x] `dsp/include/krate/dsp/primitives/lfo.h` - LFO class, Waveform enum
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/core/note_value.h` - NoteValue, NoteModifier enums
- [x] `dsp/include/krate/dsp/core/db_utils.h` - flushDenormal, isNaN, isInf
- [x] `dsp/include/krate/dsp/core/math_constants.h` - kPi constant

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| LFO | Output range is [-1, +1] | Map to [0, 1] for frequency calculation: `(lfoValue + 1.0f) / 2.0f` |
| LFO | Waveform enum includes SampleHold, SmoothRandom | Only expose Sine, Triangle, Sawtooth, Square per FR-011 |
| Allpass1Pole | setFrequency clamps to [1 Hz, Nyquist * 0.99] | Internal clamping handles edge cases |
| OnePoleSmoother | Uses `snapTo()` for immediate value set | Not `snap()` |
| LFO | Phase offset is in degrees, not radians | Use degrees (0-360) for stereo spread |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| expFrequencyMap() | Exponential frequency sweep mapping | Already handled inline | Phaser only (flanger uses linear) |
| - | - | - | - |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| calculateSweepFrequency | One-liner using std::pow, specific to phaser's exponential mapping |
| calculateMinMaxFrequency | Simple centerFreq * (1 +/- depth), only used internally |

**Decision**: No Layer 0 extraction needed. The exponential frequency mapping is simple enough to be inline, and the specific formula `freq = minFreq * pow(maxFreq/minFreq, (lfoValue+1)/2)` is phaser-specific.

**Note (Principle XIII)**: Architecture documentation will be updated in Phase 8 (tasks T105-T107) as the final task before completion, per Constitution Principle XIII requirements.

## Higher-Layer Reusability Analysis

*Forward-looking analysis: What code from THIS feature could be reused by SIBLING features at the same layer?*

### Sibling Features Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer** (from FLT-ROADMAP.md):
- EnvelopeFilter (Phase 9 - already implemented)
- FormantFilter (Phase 8 - already implemented)
- CrossoverFilter (Phase 7 - already implemented)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Stereo LFO phase offset pattern | MEDIUM | Flanger, Chorus (future) | Keep local / document pattern |
| Mix-before-feedback topology | LOW | Phaser-specific | Keep local |

### Detailed Analysis (for MEDIUM potential items)

**Stereo LFO Phase Offset Pattern** provides:
- Using LFO's setPhaseOffset() for channel differentiation
- Configurable spread in degrees

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| Flanger (future) | YES | Same stereo modulation pattern |
| Chorus (future) | YES | Similar stereo LFO usage |

**Recommendation**: Keep in Phaser for now. When Flanger is implemented, evaluate if extraction to common utility is warranted. The pattern is simple enough that duplication is acceptable until proven reuse.

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared base class | Only phaser uses cascaded allpass with LFO modulation at this layer |
| Keep stereo pattern local | First stereo modulation effect - patterns not established |
| Use LFO directly | LFO already has all needed features including tempo sync |

### Review Trigger

After implementing **Flanger** (potential future feature), review this section:
- [ ] Does Flanger need stereo LFO spread? -> Consider shared utility
- [ ] Does Flanger use similar modulation pattern? -> Document shared pattern
- [ ] Any duplicated code? -> Consider shared utilities

## Project Structure

### Documentation (this feature)

```text
specs/079-phaser/
├── plan.md              # This file (/speckit.plan command output)
├── research.md          # Phase 0 output (/speckit.plan command)
├── data-model.md        # Phase 1 output (/speckit.plan command)
├── quickstart.md        # Phase 1 output (/speckit.plan command)
├── contracts/           # Phase 1 output (/speckit.plan command)
└── tasks.md             # Phase 2 output (/speckit.tasks command - NOT created by /speckit.plan)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── processors/
│       └── phaser.h           # NEW: Phaser processor class
└── tests/
    └── unit/
        └── processors/
            └── phaser_test.cpp  # NEW: Phaser unit tests
```

**Structure Decision**: Single header-only implementation in Layer 2 processors directory, following the established pattern of EnvelopeFilter, FormantFilter, etc.

## Complexity Tracking

> **No Constitution Check violations requiring justification.**

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| - | - | - |

---

## Phase 0: Research Output

### Research Questions Resolved

All technical decisions were clarified in the specification:

1. **Feedback topology**: Mix-before-feedback (classic phaser architecture)
   - Rationale: Classic analog phaser behavior where dry+wet are mixed first, then feedback from mixed output

2. **Frequency control API**: Center/depth only
   - Rationale: More intuitive user interface, internal calculation of min/max is straightforward
   - Formula: `minFreq = centerFreq * (1 - depth)`, `maxFreq = centerFreq * (1 + depth)`

3. **LFO-to-frequency mapping**: Exponential (logarithmic)
   - Rationale: Perceptually even sweep across the frequency range
   - Formula: `freq = minFreq * pow(maxFreq/minFreq, (lfoValue+1)/2)`

4. **Feedback stability**: Hyperbolic tangent (tanh)
   - Rationale: Smooth, musical soft-clipping that prevents runaway oscillation
   - Application: `feedbackSignal = tanh(feedbackSignal * feedbackAmount)`

5. **LFO waveforms**: Expose Sine, Triangle, Square, Sawtooth
   - Rationale: Matches classic phaser capabilities
   - Note: LFO already supports these; SampleHold/SmoothRandom not exposed

### Existing Component Fitness

| Component | Fitness for Phaser | Notes |
|-----------|-------------------|-------|
| Allpass1Pole | Perfect | Designed specifically for phasers per spec 073 |
| LFO | Perfect | Has all needed features including tempo sync |
| OnePoleSmoother | Perfect | Standard 5ms smoothing for parameters |

### Classic Phaser Reference Values

From research on classic phasers (MXR Phase 90, Small Stone, etc.):

| Parameter | Classic Range | Our Range |
|-----------|---------------|-----------|
| Stages | 4-8 | 2-12 (more flexible) |
| Rate | 0.1-10 Hz | 0.01-20 Hz (LFO limits) |
| Center Frequency | 500-2000 Hz | User-configurable |
| Feedback | 0-80% | -100% to +100% (bipolar) |

---

## Phase 1: Design Output

### Data Model

See [data-model.md](data-model.md) for complete entity definitions.

**Core Class**: `Phaser`

```cpp
class Phaser {
public:
    // Lifecycle
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;

    // Processing
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, size_t numSamples) noexcept;

    // Stereo processing
    void processStereo(float* left, float* right, size_t numSamples) noexcept;

    // Stage configuration
    void setNumStages(int stages) noexcept;  // 2, 4, 6, 8, 10, 12

    // LFO parameters
    void setRate(float hz) noexcept;              // 0.01-20 Hz
    void setDepth(float amount) noexcept;         // 0.0-1.0
    void setWaveform(Waveform waveform) noexcept; // Sine, Triangle, Square, Sawtooth

    // Frequency range
    void setCenterFrequency(float hz) noexcept;   // Typical: 500-2000 Hz

    // Feedback
    void setFeedback(float amount) noexcept;      // -1.0 to +1.0

    // Stereo
    void setStereoSpread(float degrees) noexcept; // 0-360 degrees

    // Mix
    void setMix(float dryWet) noexcept;           // 0.0-1.0

    // Tempo sync
    void setTempoSync(bool enabled) noexcept;
    void setNoteValue(NoteValue value, NoteModifier modifier = NoteModifier::None) noexcept;
    void setTempo(float bpm) noexcept;

    // Getters
    [[nodiscard]] int getNumStages() const noexcept;
    [[nodiscard]] float getRate() const noexcept;
    [[nodiscard]] float getDepth() const noexcept;
    [[nodiscard]] Waveform getWaveform() const noexcept;
    [[nodiscard]] float getCenterFrequency() const noexcept;
    [[nodiscard]] float getFeedback() const noexcept;
    [[nodiscard]] float getStereoSpread() const noexcept;
    [[nodiscard]] float getMix() const noexcept;
    [[nodiscard]] bool isTempoSyncEnabled() const noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;

private:
    static constexpr int kMaxStages = 12;

    // Allpass filter stages (left and right channels)
    std::array<Allpass1Pole, kMaxStages> stagesL_;
    std::array<Allpass1Pole, kMaxStages> stagesR_;

    // LFO for modulation (separate for L/R due to phase offset)
    LFO lfoL_;
    LFO lfoR_;

    // Parameter smoothers
    OnePoleSmoother rateSmoother_;
    OnePoleSmoother depthSmoother_;
    OnePoleSmoother feedbackSmoother_;
    OnePoleSmoother mixSmoother_;
    OnePoleSmoother centerFreqSmoother_;

    // Feedback state
    float feedbackStateL_ = 0.0f;
    float feedbackStateR_ = 0.0f;

    // Configuration
    double sampleRate_ = 44100.0;
    int numStages_ = 4;
    float centerFrequency_ = 1000.0f;
    float stereoSpread_ = 0.0f;
    bool prepared_ = false;
};
```

### Processing Algorithm

```
Input Signal
    |
    v
[+ Feedback (tanh-limited)] <----- feedbackState * feedback
    |
    v
[Allpass Stage 1] (frequency from LFO)
    |
    v
[Allpass Stage 2]
    |
    v
... (N stages based on numStages_)
    |
    v
[Allpass Stage N]
    |
    v
Wet Signal
    |
    +---> [Mix with Dry] ---> Output
    |
    +---> feedbackState (for next sample)
```

### Frequency Sweep Calculation

```cpp
// LFO output is in range [-1, +1]
// Map to [0, 1] for frequency calculation
float lfoNorm = (lfoValue + 1.0f) * 0.5f;

// Calculate min/max from center and depth
float minFreq = centerFreq * (1.0f - depth);
float maxFreq = centerFreq * (1.0f + depth);

// Clamp min to prevent negative/zero frequencies
minFreq = std::max(minFreq, 20.0f);

// Exponential mapping for perceptually even sweep
float sweepFreq = minFreq * std::pow(maxFreq / minFreq, lfoNorm);
```

### API Contracts

See [contracts/](contracts/) directory for detailed API documentation.

### Quickstart

See [quickstart.md](quickstart.md) for usage examples.

---

## Constitution Re-Check (Post-Design)

**All principles verified post-design:**

- [x] **Principle II**: All process methods are noexcept, no allocations, arrays are fixed-size
- [x] **Principle III**: C++20 features, RAII, constexpr constants
- [x] **Principle VI**: No platform-specific code
- [x] **Principle IX**: Layer 2 depends only on Layers 0-1
- [x] **Principle X**: tanh soft-limiting for feedback > 100%
- [x] **Principle XII**: Test structure defined
- [x] **Principle XIV**: No ODR conflicts
- [x] **Principle XVI**: All requirements addressable

---

## Stop Point

Planning phase complete. This plan covers:

1. **Branch**: `079-phaser`
2. **Implementation Plan Path**: `specs/079-phaser/plan.md`
3. **Generated Artifacts**:
   - `specs/079-phaser/plan.md` (this file)
   - `specs/079-phaser/research.md` (integrated into plan)
   - `specs/079-phaser/data-model.md` (to be generated)
   - `specs/079-phaser/quickstart.md` (to be generated)
   - `specs/079-phaser/contracts/` (to be generated)

**Next Step**: Run `/speckit.tasks` to generate implementation tasks with test-first workflow.
