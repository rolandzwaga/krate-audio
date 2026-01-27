# Implementation Plan: Note-Selective Filter

**Branch**: `093-note-selective-filter` | **Date**: 2026-01-24 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/093-note-selective-filter/spec.md`

## Summary

A Layer 2 processor that applies filtering only to audio matching specific note classes (C, C#, D, etc.), passing non-matching notes through dry. Uses pitch detection to identify the current note, then crossfades between dry and filtered signal based on whether the detected note matches the target set. Filter processes continuously (always hot) for click-free transitions.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: PitchDetector, SVF, OnePoleSmoother (all existing Layer 1 primitives)
**Storage**: N/A (DSP processor, no persistence)
**Testing**: Catch2 (via dsp_tests target)
**Target Platform**: Cross-platform (Windows MSVC, macOS Clang, Linux GCC)
**Project Type**: Shared DSP library component
**Performance Goals**: < 0.5% CPU at 44.1kHz (per spec SC-009)
**Constraints**: Real-time safe (noexcept, zero allocations in process), block-rate note matching
**Scale/Scope**: Single monophonic processor, 12 note classes

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Pre-Research Constitution Gates:**

- [x] **Principle II (Real-Time Safety)**: All processing methods will be noexcept with zero allocations
- [x] **Principle III (Modern C++)**: Will use C++20, RAII, constexpr where possible
- [x] **Principle IX (Layered Architecture)**: Layer 2 processor, depends only on Layer 0/1
- [x] **Principle X (DSP Constraints)**: Denormal flushing, filter always hot
- [x] **Principle XIII (Test-First)**: Skills auto-load; tests before implementation

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: `NoteSelectiveFilter`, `NoDetectionMode`

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| NoteSelectiveFilter | `grep -r "NoteSelectiveFilter" dsp/ plugins/` | No | Create New |
| NoDetectionMode | `grep -r "NoDetectionMode" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: `frequencyToNoteClass`, `frequencyToCentsDeviation`

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| frequencyToNoteClass | `grep -r "frequencyToNote" dsp/ plugins/` | No | N/A | Create in pitch_utils.h |
| frequencyToCentsDeviation | `grep -r "frequencyToCents" dsp/ plugins/` | No | N/A | Create in pitch_utils.h |
| noteClass | `grep -r "noteClass" dsp/ plugins/` | No | N/A | Term not used |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| PitchDetector | `dsp/include/krate/dsp/primitives/pitch_detector.h` | 1 | Pitch detection |
| SVF | `dsp/include/krate/dsp/primitives/svf.h` | 1 | Audio filtering |
| OnePoleSmoother | `dsp/include/krate/dsp/primitives/smoother.h` | 1 | Crossfade smoothing |
| semitonesToRatio | `dsp/include/krate/dsp/core/pitch_utils.h` | 0 | Reference for pitch math |
| detail::flushDenormal | `dsp/include/krate/dsp/core/db_utils.h` | 0 | Denormal prevention |
| detail::isNaN/isInf | `dsp/include/krate/dsp/core/db_utils.h` | 0 | Input validation |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/pitch_utils.h` - No frequencyToNoteClass or frequencyToCentsDeviation exists
- [x] `dsp/include/krate/dsp/primitives/` - No NoteSelectiveFilter exists
- [x] `dsp/include/krate/dsp/processors/` - No NoteSelectiveFilter exists
- [x] `dsp/include/krate/dsp/processors/pitch_tracking_filter.h` - Reference pattern

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types are unique and not found in codebase. The utility functions `frequencyToNoteClass` and `frequencyToCentsDeviation` are new and fill gaps in pitch_utils.h. Similar patterns exist in PitchTrackingFilter for composition of PitchDetector + SVF + Smoother.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| PitchDetector | prepare | `void prepare(double sampleRate, std::size_t windowSize = kDefaultWindowSize) noexcept` | Yes |
| PitchDetector | reset | `void reset() noexcept` | Yes |
| PitchDetector | push | `void push(float sample) noexcept` | Yes |
| PitchDetector | pushBlock | `void pushBlock(const float* samples, std::size_t numSamples) noexcept` | Yes |
| PitchDetector | detect | `void detect() noexcept` | Yes |
| PitchDetector | getDetectedFrequency | `[[nodiscard]] float getDetectedFrequency() const noexcept` | Yes |
| PitchDetector | getConfidence | `[[nodiscard]] float getConfidence() const noexcept` | Yes |
| PitchDetector | isPitchValid | `[[nodiscard]] bool isPitchValid() const noexcept` | Yes |
| PitchDetector | kConfidenceThreshold | `static constexpr float kConfidenceThreshold = 0.3f` | Yes |
| PitchDetector | kMinFrequency | `static constexpr float kMinFrequency = 50.0f` | Yes |
| PitchDetector | kMaxFrequency | `static constexpr float kMaxFrequency = 1000.0f` | Yes |
| SVF | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| SVF | reset | `void reset() noexcept` | Yes |
| SVF | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| SVF | setMode | `void setMode(SVFMode mode) noexcept` | Yes |
| SVF | setCutoff | `void setCutoff(float hz) noexcept` | Yes |
| SVF | setResonance | `void setResonance(float q) noexcept` | Yes |
| SVFMode | enum values | `Lowpass, Highpass, Bandpass, Notch, Allpass, Peak, LowShelf, HighShelf` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `ITERUM_NOINLINE void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| OnePoleSmoother | reset | `void reset() noexcept` | Yes |
| OnePoleSmoother | isComplete | `[[nodiscard]] bool isComplete() const noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/pitch_detector.h` - PitchDetector class
- [x] `dsp/include/krate/dsp/primitives/svf.h` - SVF class, SVFMode enum
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/core/pitch_utils.h` - semitonesToRatio, ratioToSemitones
- [x] `dsp/include/krate/dsp/core/db_utils.h` - flushDenormal, isNaN, isInf
- [x] `dsp/include/krate/dsp/processors/pitch_tracking_filter.h` - Reference pattern
- [x] `dsp/include/krate/dsp/processors/crossover_filter.h` - Atomic pattern reference

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| PitchDetector | Detection runs every windowSize/4 samples internally | Call push() every sample, detect() is called internally |
| OnePoleSmoother | Time is 99% settling (5 tau) | 5ms time = ~1ms tau |
| SVF | Filter type via setMode(), not constructor | Call setMode() after prepare() |
| std::atomic | MSVC may not be lock-free for float | Use std::memory_order_relaxed for performance |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| frequencyToNoteClass(float hz) | Reusable pitch-to-note conversion | pitch_utils.h | NoteSelectiveFilter, future scale/harmony features |
| frequencyToCents(float hz, float reference) | Calculate cents deviation | pitch_utils.h | NoteSelectiveFilter tolerance check |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| isNoteMatched() | Internal logic specific to this processor's tolerance/bitset handling |
| calculateCrossfadeTarget() | Depends on processor state (no-detection mode, last state) |

**Decision**: Extract `frequencyToNoteClass(float hz)` to `pitch_utils.h` as it has clear audio semantics (MIDI note class 0-11 from frequency) and will be useful for any pitch-aware processing. Keep tolerance checking internal.

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer**:
- PitchTrackingFilter (Layer 2) - already implemented, similar pattern
- EnvelopeFilter (Layer 2) - amplitude-based filtering
- TransientAwareFilter (Layer 2) - transient-based filtering
- Future: ScaleAwareFilter, ChordDetectionFilter

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| frequencyToNoteClass() | HIGH | Any pitch-to-note conversion | Extract to Layer 0 |
| NoDetectionMode enum | MEDIUM | Future pitch-based processors | Keep local initially |
| Pitch+SVF+Smoother pattern | HIGH (pattern) | Already used by PitchTrackingFilter | Document pattern |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Extract frequencyToNoteClass to Layer 0 | Clear audio semantics, 2+ potential consumers |
| Keep NoDetectionMode local | Only one consumer, may evolve differently for other processors |
| Follow PitchTrackingFilter pattern | Proven composition pattern in this codebase |

## Project Structure

### Documentation (this feature)

```text
specs/093-note-selective-filter/
├── spec.md              # Feature specification (exists)
├── plan.md              # This file
├── research.md          # Phase 0 research findings
├── data-model.md        # Phase 1 data model
├── quickstart.md        # Phase 1 implementation quickstart
└── contracts/           # Phase 1 API contracts
    └── note_selective_filter.h  # Header contract
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   ├── core/
│   │   └── pitch_utils.h              # Add frequencyToNoteClass()
│   └── processors/
│       └── note_selective_filter.h    # New processor (header-only)
└── tests/
    └── unit/
        ├── core/
        │   └── pitch_utils_test.cpp   # Add frequencyToNoteClass tests
        └── processors/
            └── note_selective_filter_test.cpp  # New test file
```

**Structure Decision**: Header-only implementation in processors/ following existing pattern. Utility function added to existing pitch_utils.h. Tests mirror source structure.

## Complexity Tracking

No constitution violations requiring justification. All design decisions follow established patterns.

---

# Phase 0: Research Findings

## Research Task 1: PitchDetector API and Usage Patterns

**Decision**: Use PitchDetector as-is with block-rate detection updates

**Rationale**:
- PitchDetector internally updates every windowSize/4 samples (~64 samples at default 256 window)
- This aligns well with spec's block-rate note matching requirement (~512 samples)
- Provides `getDetectedFrequency()` and `getConfidence()` for note matching
- `kConfidenceThreshold = 0.3f` is the default; we need configurable threshold

**Alternatives considered**:
- Custom pitch detector: Rejected (existing works, avoid duplication)
- Sample-rate detection: Rejected (spec requires block-rate for stability)

## Research Task 2: SVF API for Filter Type

**Decision**: Use SVFMode enum directly, expose same filter types

**Rationale**:
- SVFMode provides: Lowpass, Highpass, Bandpass, Notch, Allpass, Peak, LowShelf, HighShelf
- All relevant for creative filtering applications
- setMode() can be called at any time, no click artifacts due to TPT topology

**Alternatives considered**:
- Limited subset of modes: Rejected (full flexibility is valuable)
- Custom filter: Rejected (SVF is modulation-stable, ideal for this use case)

## Research Task 3: OnePoleSmoother for Crossfade

**Decision**: Use OnePoleSmoother with 5ms default (per spec)

**Rationale**:
- OnePoleSmoother::configure() takes smoothTimeMs as 99% settling time (5 tau)
- Matches spec's "5 time constants for exponential settling" (FR-014)
- isComplete() can detect when transition is done
- snapTo() useful for reset

**Alternatives considered**:
- LinearRamp: Rejected (exponential approach is more natural for crossfades)
- SlewLimiter: Rejected (fixed rate not needed, exponential is preferred)

## Research Task 4: Existing Frequency-to-Note Utilities

**Decision**: Create `frequencyToNoteClass(float hz)` in pitch_utils.h

**Rationale**:
- pitch_utils.h has semitonesToRatio and ratioToSemitones but no frequency-to-note
- Formula: `noteClass = round(12 * log2(freq/440) + 69) mod 12`
- This maps A440 to note class 9 (A), C4 (261.63Hz) to note class 0 (C)

**Implementation**:
```cpp
[[nodiscard]] inline int frequencyToNoteClass(float hz) noexcept {
    if (hz <= 0.0f) return -1;  // Invalid frequency
    // MIDI note number (A440 = 69)
    float midiNote = 12.0f * std::log2(hz / 440.0f) + 69.0f;
    // Note class 0-11 (C=0, C#=1, ..., B=11)
    int noteClass = static_cast<int>(std::round(midiNote)) % 12;
    if (noteClass < 0) noteClass += 12;  // Handle negative modulo
    return noteClass;
}
```

## Research Task 5: Atomic Parameter Patterns

**Decision**: Follow CrossoverFilter pattern with std::atomic<float> and std::memory_order_relaxed

**Rationale**:
- CrossoverFilter uses `std::atomic<float>` for thread-safe UI parameter updates
- Uses `std::memory_order_relaxed` since parameters don't need ordering guarantees
- Pattern: atomic for config values, regular members for state
- Note: For std::bitset<12>, we'll use std::atomic<uint16_t> since bitset isn't atomic

**Implementation pattern**:
```cpp
// Atomic parameters (thread-safe setters from UI)
std::atomic<float> cutoffHz_{1000.0f};
std::atomic<float> resonance_{0.7071f};
std::atomic<float> noteTolerance_{49.0f};
std::atomic<float> crossfadeTimeMs_{5.0f};
std::atomic<uint16_t> targetNotes_{0};  // bitset<12> as uint16_t
std::atomic<int> noDetectionMode_{0};

// Non-atomic state (audio thread only)
float currentCrossfade_ = 0.0f;
int lastDetectedNote_ = -1;
```

---

# Phase 1: Design

## Data Model

See [data-model.md](data-model.md) for complete entity definitions.

### Key Entities

1. **NoteSelectiveFilter** - Main processor class
   - Composes: PitchDetector, SVF, OnePoleSmoother
   - Atomic parameters for thread-safe UI updates
   - Block-rate note matching (configurable, default ~512 samples)

2. **NoDetectionMode** - Enum for behavior when pitch detection fails
   - `Dry`: Pass dry signal (default)
   - `Filtered`: Apply filter regardless
   - `LastState`: Maintain previous filtering state

3. **Note Class** - Integer 0-11 representing pitch class
   - 0=C, 1=C#, 2=D, ..., 11=B
   - Stored as std::bitset<12> internally (atomic as uint16_t)

## API Contract

See [contracts/note_selective_filter.h](contracts/note_selective_filter.h) for full header.

### Public Interface Summary

```cpp
class NoteSelectiveFilter {
public:
    // Lifecycle
    void prepare(double sampleRate, int maxBlockSize) noexcept;
    void reset() noexcept;

    // Note Selection (thread-safe)
    void setTargetNotes(std::bitset<12> notes) noexcept;
    void setTargetNote(int noteClass, bool enabled) noexcept;
    void clearAllNotes() noexcept;
    void setAllNotes() noexcept;

    // Pitch Matching (thread-safe)
    void setNoteTolerance(float cents) noexcept;  // 1-49 cents

    // Crossfade (thread-safe)
    void setCrossfadeTime(float ms) noexcept;  // 0.5-50ms

    // Filter (thread-safe)
    void setCutoff(float hz) noexcept;
    void setResonance(float q) noexcept;
    void setFilterType(SVFMode type) noexcept;

    // Pitch Detection (thread-safe)
    void setDetectionRange(float minHz, float maxHz) noexcept;
    void setConfidenceThreshold(float threshold) noexcept;

    // No-Detection Behavior (thread-safe)
    void setNoDetectionBehavior(NoDetectionMode mode) noexcept;

    // Processing (audio thread only)
    [[nodiscard]] float process(float input) noexcept;
    void processBlock(float* buffer, int numSamples) noexcept;

    // State Query (thread-safe read)
    [[nodiscard]] int getDetectedNoteClass() const noexcept;
    [[nodiscard]] bool isCurrentlyFiltering() const noexcept;
};
```

## Quickstart

See [quickstart.md](quickstart.md) for implementation guide.

### Implementation Outline

1. **Add frequencyToNoteClass to pitch_utils.h**
   - Test: verify A440 -> 9 (A), C4 -> 0 (C)

2. **Create NoteSelectiveFilter header**
   - Include dependencies (PitchDetector, SVF, OnePoleSmoother)
   - Define NoDetectionMode enum
   - Implement class with atomic parameters

3. **Implement core processing**
   - process(): push to pitch detector, check note match, crossfade
   - processBlock(): loop with block-rate detection updates

4. **Test each acceptance scenario**
   - Note matching with tolerance
   - Smooth crossfade transitions
   - No-detection behavior modes

---

## Post-Design Constitution Re-Check

- [x] **Principle II (Real-Time Safety)**: All processing noexcept, zero allocations
- [x] **Principle III (Modern C++)**: C++20, RAII, constexpr constants
- [x] **Principle IX (Layered Architecture)**: Layer 2, only depends on Layer 0/1
- [x] **Principle X (DSP Constraints)**: Denormals flushed, filter always hot
- [x] **Principle XIII (Test-First)**: Test plan defined for each FR/SC
- [x] **Principle XIV (ODR Prevention)**: All types unique, utility added to existing header
- [x] **Principle XVI (Honest Completion)**: Clear acceptance criteria defined

**Gate Status**: PASSED - Ready for task generation
