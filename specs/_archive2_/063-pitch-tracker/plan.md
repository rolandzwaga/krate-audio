# Implementation Plan: Pitch Tracking Robustness

**Branch**: `063-pitch-tracker` | **Date**: 2026-02-18 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/063-pitch-tracker/spec.md`

## Summary

Implement a `PitchTracker` class (Layer 1, Primitives) that wraps the existing `PitchDetector` with a 5-stage post-processing pipeline: confidence gating, median filtering, hysteresis, minimum note duration, and frequency smoothing. This transforms raw, jittery pitch detection into stable MIDI note decisions suitable for the Phase 4 HarmonizerEngine. The implementation composes four existing components (`PitchDetector`, `OnePoleSmoother`, `frequencyToMidiNote()`, `midiNoteToFrequency()`) without reimplementing any detection logic. The class is header-only, noexcept throughout, and performs zero heap allocations in the processing path.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: PitchDetector (L1), OnePoleSmoother (L1), pitch_utils.h (L0), midi_utils.h (L0)
**Storage**: N/A (in-memory state only, stack-allocated arrays)
**Testing**: Catch2 (dsp_tests target) *(Constitution Principle VIII: Testing Discipline)*
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- cross-platform
**Project Type**: Shared DSP library (KrateDSP)
**Performance Goals**: < 0.1% CPU at 44.1kHz (Layer 1 budget), zero allocations in audio path
**Constraints**: Real-time safe (no alloc, no locks, no exceptions, no I/O), noexcept on all methods
**Scale/Scope**: Single header-only class (~200-300 lines) + test file (~400-600 lines)

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle I (VST3 Architecture Separation)**: N/A -- this is a DSP library component, not a plugin component.

**Principle II (Real-Time Audio Thread Safety)**: PASS -- all processing methods are noexcept, use only stack-allocated arrays (`std::array`), no locks, no I/O. `prepare()` allocates via PitchDetector but is called from setup, not audio thread.

**Principle III (Modern C++ Standards)**: PASS -- uses `std::array`, `constexpr`, `[[nodiscard]]`, `noexcept`, RAII. No raw new/delete.

**Principle IV (SIMD & DSP Optimization)**: PASS -- SIMD analysis performed, verdict is NOT BENEFICIAL (see SIMD section below).

**Principle VI (Cross-Platform Compatibility)**: PASS -- no platform-specific code. Uses standard C++20 and existing cross-platform components.

**Principle VII (Project Structure & Build System)**: PASS -- follows monorepo layout, header at `dsp/include/krate/dsp/primitives/`, tests at `dsp/tests/unit/primitives/`.

**Principle VIII (Testing Discipline)**: PASS -- tests written before implementation per canonical todo list.

**Principle IX (Layered DSP Architecture)**: PASS -- Layer 1 depending only on Layer 0 (`pitch_utils.h`, `midi_utils.h`) and Layer 1 (`PitchDetector`, `OnePoleSmoother`). Layer 1 can depend on Layer 0 and other Layer 1 components.

**Principle XV (Pre-Implementation Research / ODR Prevention)**: PASS -- `grep -r "class PitchTracker" dsp/ plugins/` returned no results. No existing class with this name.

**Principle XVI (Honest Completion)**: Acknowledged -- compliance table will be filled with specific file paths, line numbers, and test output.

**Required Check - Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) -- no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: `PitchTracker`

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| `PitchTracker` | `grep -r "class PitchTracker" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: None -- all utilities already exist in the codebase.

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| N/A | N/A | N/A | N/A | N/A |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `PitchDetector` | `dsp/include/krate/dsp/primitives/pitch_detector.h` | 1 | Wrapped as member; `push()`, `getDetectedFrequency()`, `getConfidence()` called internally |
| `OnePoleSmoother` | `dsp/include/krate/dsp/primitives/smoother.h` | 1 | Stage 5 frequency smoothing; `configure()`, `setTarget()`, `advanceSamples()`, `getCurrentValue()`, `snapTo()`, `reset()` |
| `frequencyToMidiNote()` | `dsp/include/krate/dsp/core/pitch_utils.h` | 0 | Convert Hz to continuous MIDI note for hysteresis comparison |
| `midiNoteToFrequency()` | `dsp/include/krate/dsp/core/midi_utils.h` | 0 | Convert committed note integer to center Hz for smoother target |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` -- Layer 0 core utilities (checked pitch_utils.h, midi_utils.h)
- [x] `dsp/include/krate/dsp/primitives/` -- Layer 1 DSP primitives (no PitchTracker exists)
- [x] `specs/_architecture_/` -- Component inventory (PitchDetector documented in layer-1-primitives.md)
- [x] `dsp/include/krate/dsp/core/pitch_utils.h` -- verified `frequencyToMidiNote()` and `frequencyToCentsDeviation()` signatures

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: The only new type (`PitchTracker`) does not exist anywhere in the codebase. All utility functions already exist and will be reused, not duplicated. No new free functions are created.

## Dependency API Contracts (Principle XV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| PitchDetector | prepare | `void prepare(double sampleRate, std::size_t windowSize = kDefaultWindowSize) noexcept` | Yes |
| PitchDetector | reset | `void reset() noexcept` | Yes |
| PitchDetector | push | `void push(float sample) noexcept` | Yes |
| PitchDetector | getDetectedFrequency | `float getDetectedFrequency() const noexcept` | Yes |
| PitchDetector | getConfidence | `float getConfidence() const noexcept` | Yes |
| PitchDetector | kDefaultWindowSize | `static constexpr std::size_t kDefaultWindowSize = 256` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `ITERUM_NOINLINE void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | advanceSamples | `void advanceSamples(std::size_t numSamples) noexcept` | Yes |
| OnePoleSmoother | getCurrentValue | `float getCurrentValue() const noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| OnePoleSmoother | reset | `void reset() noexcept` | Yes |
| pitch_utils | frequencyToMidiNote | `inline float frequencyToMidiNote(float hz) noexcept` | Yes |
| midi_utils | midiNoteToFrequency | `constexpr float midiNoteToFrequency(int midiNote, float a4Frequency = kA4FrequencyHz) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/pitch_detector.h` -- PitchDetector class (L48-L270)
- [x] `dsp/include/krate/dsp/primitives/smoother.h` -- OnePoleSmoother class (L133-L292)
- [x] `dsp/include/krate/dsp/core/pitch_utils.h` -- frequencyToMidiNote, frequencyToCentsDeviation
- [x] `dsp/include/krate/dsp/core/midi_utils.h` -- midiNoteToFrequency

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| PitchDetector | `push()` auto-calls `detect()` every `windowSize_/4` samples via internal counter | Do NOT call `detect()` separately; just call `push()` |
| PitchDetector | `buffer_` and `autocorr_` are `std::vector<float>` (heap-allocated in `prepare()`) | Allocations happen in `prepare()`, not in audio path -- this is acceptable |
| OnePoleSmoother | Uses `snapTo()` not `snap()` | `smoother.snapTo(value)` for instant jump |
| OnePoleSmoother | `advanceSamples()` exists for multi-sample advancement | Use `advanceSamples(hopSize)` per pipeline tick, not `process()` in a loop |
| frequencyToCentsDeviation | Returns deviation from NEAREST note, NOT from a specific note | For hysteresis comparison against committed note, use `std::abs(frequencyToMidiNote(hz) - committedNote) * 100.0f` instead (use `std::abs`, not unqualified `abs`, to avoid the C `abs(int)` overload) |
| midiNoteToFrequency | Uses `constexprExp` approximation, not `std::pow` | Good for constexpr contexts; slight precision difference vs `440 * pow(2, (n-69)/12)` |

## Layer 0 Candidate Analysis

*This is a Layer 1 feature. Layer 0 candidate extraction is less applicable but still evaluated.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| None | N/A | N/A | N/A |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| `runPipeline()` | Internal method, tightly coupled to PitchTracker state machine |
| `computeMedian()` | Small helper (~10 lines), only used by PitchTracker |

**Decision**: No new Layer 0 utilities needed. The median filter ring buffer pattern is small (~20 lines) and specific to this use case. If a second consumer emerges, it can be extracted then.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | NO | Pipeline stages are sequential but not recursive |
| **Data parallelism width** | 1 | Single pitch stream, no multi-voice processing |
| **Branch density in inner loop** | HIGH | 5 stages each with conditional branching |
| **Dominant operations** | Scalar arithmetic + 1 log2 | One `std::log2` call per hop for MIDI conversion |
| **Current CPU budget vs expected usage** | 0.1% budget vs ~0.01% expected | PitchTracker adds ~50-100 scalar ops per hop (every 64 samples) |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: The PitchTracker processes a single pitch stream with high branch density across 5 sequential stages. There is no data parallelism to exploit. The dominant cost is the underlying PitchDetector's autocorrelation (which is inside PitchDetector, not in scope here). The tracker's overhead is ~50-100 scalar operations per hop interval, well under the 0.1% CPU budget.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Early-out when confidence < threshold | Saves stages 2-5 for unvoiced frames | LOW | YES (already part of pipeline design) |
| Skip median sort when medianSize_ == 1 | Avoids unnecessary copy+sort | LOW | YES |

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 1 -- DSP Primitives

**Related features at same layer** (from harmonizer roadmap):
- PitchDetector (already exists, Layer 1) -- PitchTracker wraps this
- No other pitch-related primitives planned at Layer 1

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| `PitchTracker` | HIGH | Phase 4 HarmonizerEngine (L3), potential pitch-correction effect, pitch-following mod source | Keep at Layer 1 as planned |
| Median filter logic | LOW | No known second consumer | Keep inline in PitchTracker |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| PitchTracker at Layer 1 | General-purpose pitch tracking post-processor, not specific to harmonizer |
| Median filter inline | Only ~20 lines, single consumer, extracting would add complexity without benefit |
| No shared base class | Only one pitch tracking variant needed |

### Review Trigger

After implementing **Phase 4 HarmonizerEngine**, review:
- [ ] Does HarmonizerEngine need additional pitch tracking features? -> Extend PitchTracker
- [ ] Does HarmonizerEngine use PitchTracker as-is? -> Confirm API sufficiency
- [ ] Any duplicated pitch logic? -> Consider extracting to PitchTracker

## Project Structure

### Documentation (this feature)

```text
specs/063-pitch-tracker/
+-- plan.md              # This file
+-- spec.md              # Feature specification
+-- research.md          # Phase 0 research output
+-- data-model.md        # Phase 1 data model
+-- quickstart.md        # Phase 1 quickstart guide
+-- contracts/           # Phase 1 API contracts
|   +-- pitch_tracker_api.h
+-- tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
dsp/
+-- include/krate/dsp/
|   +-- primitives/
|       +-- pitch_tracker.h          # NEW: Header-only PitchTracker class
+-- tests/
    +-- unit/
        +-- primitives/
            +-- pitch_tracker_test.cpp  # NEW: Catch2 unit tests
```

### Files Modified

```text
dsp/CMakeLists.txt                      # Add pitch_tracker.h to KRATE_DSP_PRIMITIVES_HEADERS
dsp/tests/CMakeLists.txt                # Add pitch_tracker_test.cpp to sources + fno-fast-math list
specs/_architecture_/layer-1-primitives.md  # Add PitchTracker documentation
```

**Structure Decision**: Standard KrateDSP monorepo pattern. Header-only primitive at Layer 1 with Catch2 unit tests. No new directories needed.

## Complexity Tracking

No constitution violations to justify. All design decisions comply with the constitution.
