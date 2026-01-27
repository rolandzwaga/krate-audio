# Implementation Plan: Pitch-Tracking Filter Processor

**Branch**: `092-pitch-tracking-filter` | **Date**: 2026-01-24 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/092-pitch-tracking-filter/spec.md`

**Note**: This plan follows the `/speckit.plan` command workflow. See `.specify/templates/commands/plan.md` for details.

## Summary

Implement a Layer 2 DSP processor that tracks input pitch and modulates a filter cutoff harmonically. The processor composes existing `PitchDetector` (autocorrelation-based), `SVF` (TPT filter), and `OnePoleSmoother` primitives. Key features include configurable harmonic ratio, semitone offset, adaptive tracking speed for rapid pitch changes (>10 semitones/sec), confidence-gated fallback behavior, and three filter types (LP/BP/HP).

## Technical Context

**Language/Version**: C++20 (as per constitution)
**Primary Dependencies**:
- `primitives/pitch_detector.h` - Autocorrelation pitch detection
- `primitives/svf.h` - TPT State Variable Filter
- `primitives/smoother.h` - OnePoleSmoother for tracking
- `core/db_utils.h` - NaN/Inf handling, denormal flushing
- `core/pitch_utils.h` - `semitonesToRatio()` for semitone offset calculation

**Storage**: N/A (stateless DSP, internal state only)
**Testing**: Catch2 (as per project standards) *(Constitution Principle XIII: Test-First Development)*
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: DSP library component (Layer 2 processor)
**Performance Goals**: < 0.5% CPU at 48kHz mono (per SC-008)
**Constraints**: Real-time safe (noexcept, no allocations in process), < 6ms latency (pitch detector window)
**Scale/Scope**: Single header-only implementation (~400-500 LOC)

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Safety):**
- [x] All processing methods will be noexcept
- [x] No memory allocation in process() calls
- [x] No locks, mutexes, or blocking primitives
- [x] No exceptions in audio thread

**Required Check - Principle IX (Layered DSP Architecture):**
- [x] Layer 2 processor - depends only on Layer 0/1 components
- [x] Proper include pattern: `<krate/dsp/primitives/...>`

**Required Check - Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Required Check - Principle VI (Cross-Platform Compatibility):**
- [x] No platform-specific code required
- [x] Using existing cross-platform utilities from Layer 0

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: `PitchTrackingFilter`, `PitchTrackingFilterMode` (enum)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| PitchTrackingFilter | `grep -r "class PitchTrackingFilter" dsp/ plugins/` | No | Create New |
| PitchTrackingFilterMode | `grep -r "PitchTrackingFilterMode" dsp/ plugins/` | No | Create New |
| HarmonicFilter | `grep -r "class HarmonicFilter" dsp/ plugins/` | No | N/A (not creating) |

**Utility Functions to be created**: None - will use existing `semitonesToRatio()` from `pitch_utils.h`

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| semitonesToRatio | `grep -r "semitonesToRatio" dsp/` | Yes | `core/pitch_utils.h` | Reuse |
| ratioToSemitones | `grep -r "ratioToSemitones" dsp/` | Yes | `core/pitch_utils.h` | N/A (not needed) |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| PitchDetector | dsp/include/krate/dsp/primitives/pitch_detector.h | 1 | Core pitch detection - push samples, get frequency/confidence |
| SVF | dsp/include/krate/dsp/primitives/svf.h | 1 | Audio filtering - setMode, setCutoff, setResonance, process |
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | 1 | Cutoff smoothing for tracking and fallback transitions |
| semitonesToRatio | dsp/include/krate/dsp/core/pitch_utils.h | 0 | Semitone offset to frequency ratio conversion |
| detail::isNaN | dsp/include/krate/dsp/core/db_utils.h | 0 | NaN input detection |
| detail::isInf | dsp/include/krate/dsp/core/db_utils.h | 0 | Inf input detection |
| SVFMode | dsp/include/krate/dsp/primitives/svf.h | 1 | Filter type enum (Lowpass, Bandpass, Highpass) |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (sibling files)
- [x] `specs/_architecture_/` - Component inventory
- [x] `specs/FLT-ROADMAP.md` - Filter roadmap (confirmed PitchTrackingFilter is planned but not implemented)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types are unique and not found in codebase. The `PitchTrackingFilter` name appears only in the filter roadmap as a future component. All utility functions needed already exist in Layer 0.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| PitchDetector | prepare | `void prepare(double sampleRate, std::size_t windowSize = kDefaultWindowSize) noexcept` | Yes |
| PitchDetector | push | `void push(float sample) noexcept` | Yes |
| PitchDetector | pushBlock | `void pushBlock(const float* samples, std::size_t numSamples) noexcept` | Yes |
| PitchDetector | getDetectedFrequency | `[[nodiscard]] float getDetectedFrequency() const noexcept` | Yes |
| PitchDetector | getConfidence | `[[nodiscard]] float getConfidence() const noexcept` | Yes |
| PitchDetector | reset | `void reset() noexcept` | Yes |
| PitchDetector | kMinFrequency | `static constexpr float kMinFrequency = 50.0f` | Yes |
| PitchDetector | kMaxFrequency | `static constexpr float kMaxFrequency = 1000.0f` | Yes |
| PitchDetector | kDefaultWindowSize | `static constexpr std::size_t kDefaultWindowSize = 256` | Yes |
| SVF | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| SVF | setMode | `void setMode(SVFMode mode) noexcept` | Yes |
| SVF | setCutoff | `void setCutoff(float hz) noexcept` | Yes |
| SVF | setResonance | `void setResonance(float q) noexcept` | Yes |
| SVF | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| SVF | reset | `void reset() noexcept` | Yes |
| SVF | kMinQ | `static constexpr float kMinQ = 0.1f` | Yes |
| SVF | kMaxQ | `static constexpr float kMaxQ = 30.0f` | Yes |
| SVF | kButterworthQ | `static constexpr float kButterworthQ = 0.7071067811865476f` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `ITERUM_NOINLINE void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| OnePoleSmoother | reset | `void reset() noexcept` | Yes |
| OnePoleSmoother | getCurrentValue | `[[nodiscard]] float getCurrentValue() const noexcept` | Yes |
| semitonesToRatio | N/A | `[[nodiscard]] inline float semitonesToRatio(float semitones) noexcept` | Yes |
| detail::isNaN | N/A | `constexpr bool isNaN(float x) noexcept` | Yes |
| detail::isInf | N/A | `[[nodiscard]] constexpr bool isInf(float x) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/pitch_detector.h` - PitchDetector class
- [x] `dsp/include/krate/dsp/primitives/svf.h` - SVF class and SVFMode enum
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/core/pitch_utils.h` - semitonesToRatio function
- [x] `dsp/include/krate/dsp/core/db_utils.h` - isNaN, isInf, flushDenormal

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| PitchDetector | Returns frequency in Hz via `getDetectedFrequency()`, not period | Use `getDetectedFrequency()` directly for cutoff calculation |
| PitchDetector | Confidence threshold is internal constant (0.3), but we need configurable threshold | Compare `getConfidence()` against our own threshold parameter |
| PitchDetector | Detection runs every windowSize/4 samples automatically | Just push samples, no need to manually trigger detection |
| SVF | Uses `SVFMode::Lowpass/Bandpass/Highpass` enum values | Map our `PitchTrackingFilterMode` to `SVFMode` |
| OnePoleSmoother | Uses `snapTo()` not `snap()` | `smoother.snapTo(value)` |
| OnePoleSmoother | Returns smoothed value from `process()`, not `getCurrentValue()` | Use `process()` in audio loop |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| - | - | - | - |

**No new Layer 0 utilities needed.** The `semitonesToRatio()` function already exists in `pitch_utils.h` and covers the semitone-to-ratio conversion. The pitch-to-cutoff calculation (`pitch * ratio * 2^(semitones/12)`) is a simple multiplication that doesn't warrant extraction.

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| calculateCutoff | Combines pitch, ratio, and offset - specific to this processor's needs |
| mapFilterType | Simple switch statement mapping our enum to SVFMode |
| clampCutoff | Depends on sampleRate_ member variable |
| detectRapidPitchChange | Requires tracking previous pitch and timestamps |

**Decision**: All utility functions will be kept as private member functions. No Layer 0 extraction needed.

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer** (from FLT-ROADMAP.md):
- spec-090: SidechainFilter - Envelope-controlled filter (complete)
- spec-091: TransientAwareFilter - Transient-aware dynamic filter (complete)
- (future) AudioRateFilterFM - Audio-rate modulation
- (future) FrequencyShifter - Single-sideband shifting

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| PitchTrackingFilterMode enum | LOW | Only this processor | Keep local |
| Confidence-gated fallback pattern | MEDIUM | Similar to SidechainFilter threshold | Keep local (similar pattern exists) |
| Adaptive tracking speed logic | LOW | Specific to pitch tracking | Keep local |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared base class | First pitch-tracking processor; SidechainFilter and TransientAwareFilter use independent implementations |
| Keep FilterMode enum local | Different processors use different enums (TransientFilterMode, SidechainFilterMode, etc.) |
| Reuse SVFMode mapping pattern | Same pattern as sibling processors (mapFilterType private method) |

### Review Trigger

After implementing **FrequencyShifter** or **AudioRateFilterFM**, review:
- [ ] Any shared pitch-related utilities needed?
- [ ] Similar fallback/threshold patterns emerging?

## Project Structure

### Documentation (this feature)

```text
specs/092-pitch-tracking-filter/
├── plan.md              # This file
├── spec.md              # Feature specification
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
└── contracts/           # Phase 1 output
    └── pitch_tracking_filter.h  # API contract
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── processors/
│       └── pitch_tracking_filter.h  # New processor (header-only)
└── tests/
    └── unit/
        └── processors/
            └── pitch_tracking_filter_test.cpp  # Unit tests
```

**Structure Decision**: Single header-only implementation in `processors/` layer, matching sibling filters (TransientAwareFilter, SidechainFilter). Tests in `dsp/tests/unit/processors/`.

## Complexity Tracking

> No constitution violations. Standard Layer 2 processor implementation.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| - | - | - |

---

## Constitution Check - Post-Design Re-Validation

*Re-check after Phase 1 design completion.*

**Principle II (Real-Time Safety) - PASSED:**
- [x] API contract shows all process methods are noexcept
- [x] No std::vector, std::string, or dynamic allocation in processing
- [x] Only composed primitives (PitchDetector, SVF, OnePoleSmoother) all pre-allocated

**Principle IX (Layered DSP Architecture) - PASSED:**
- [x] Layer 2 processor depends only on:
  - Layer 0: `db_utils.h`, `pitch_utils.h`
  - Layer 1: `pitch_detector.h`, `svf.h`, `smoother.h`
- [x] No circular dependencies
- [x] Include pattern follows `<krate/dsp/...>` convention

**Principle XIII (Test-First Development) - READY:**
- [x] Test file location defined: `dsp/tests/unit/processors/pitch_tracking_filter_test.cpp`
- [x] Test cases defined in implementation tasks below

**Principle XIV (ODR Prevention) - VERIFIED:**
- [x] `PitchTrackingFilter` - unique class name (searched codebase)
- [x] `PitchTrackingFilterMode` - unique enum name (searched codebase)
- [x] No new Layer 0 utilities created (using existing `semitonesToRatio`)

**Principle VI (Cross-Platform Compatibility) - PASSED:**
- [x] No platform-specific code in design
- [x] Using cross-platform math: `std::log2`, `std::clamp`, `std::abs`
- [x] No SIMD intrinsics required

---

## Phase 2: Implementation Tasks

### Task Group 1: Test Infrastructure Setup

**Objective**: Set up test file and verify it compiles

| # | Task | File | Test |
|---|------|------|------|
| 1.1 | Create test file with Catch2 includes | `dsp/tests/unit/processors/pitch_tracking_filter_test.cpp` | Compile only |
| 1.2 | Add test file to CMake | `dsp/tests/CMakeLists.txt` | Build succeeds |
| 1.3 | Create minimal header stub | `dsp/include/krate/dsp/processors/pitch_tracking_filter.h` | Include compiles |

**Commit**: "test(dsp): add pitch tracking filter test infrastructure"

---

### Task Group 2: Lifecycle and Defaults

**Objective**: Implement construction, prepare(), reset(), getLatency()

| # | Task | File | Test |
|---|------|------|------|
| 2.1 | Write test: default construction | test file | `REQUIRE(filter.isPrepared() == false)` |
| 2.2 | Write test: prepare() sets prepared | test file | `REQUIRE(filter.isPrepared() == true)` |
| 2.3 | Write test: getLatency() returns window size | test file | `REQUIRE(filter.getLatency() == 256)` |
| 2.4 | Write test: reset() clears state | test file | Monitoring values reset |
| 2.5 | Implement constructor, prepare, reset, getLatency | header | All tests pass |

**Commit**: "feat(dsp): add PitchTrackingFilter lifecycle methods (FR-019, FR-020, FR-021)"

---

### Task Group 3: Parameter Setters and Getters

**Objective**: Implement all configuration methods

| # | Task | File | Test |
|---|------|------|------|
| 3.1 | Write test: setConfidenceThreshold + getter | test file | Round-trip value |
| 3.2 | Write test: setTrackingSpeed + getter | test file | Clamped to [1, 500] |
| 3.3 | Write test: setHarmonicRatio + getter | test file | Clamped to [0.125, 16.0] |
| 3.4 | Write test: setSemitoneOffset + getter | test file | Clamped to [-48, 48] |
| 3.5 | Write test: setResonance + getter | test file | Clamped to [0.5, 30.0] |
| 3.6 | Write test: setFilterType + getter | test file | All three types |
| 3.7 | Write test: setFallbackCutoff + getter | test file | Clamped to [20, Nyquist*0.45] |
| 3.8 | Write test: setFallbackSmoothing + getter | test file | Clamped to [1, 500] |
| 3.9 | Implement all setters and getters | header | All tests pass |

**Commit**: "feat(dsp): add PitchTrackingFilter parameter methods (FR-003 to FR-013)"

---

### Task Group 4: Basic Processing - No Pitch Detection

**Objective**: Implement process() with fallback behavior only

| # | Task | File | Test |
|---|------|------|------|
| 4.1 | Write test: process() returns non-zero for non-zero input | test file | Output != 0 |
| 4.2 | Write test: silence in = silence out | test file | 0.0f -> 0.0f |
| 4.3 | Write test: NaN input returns 0 and resets | test file | NaN -> 0, state reset |
| 4.4 | Write test: Inf input returns 0 and resets | test file | Inf -> 0, state reset |
| 4.5 | Write test: getCurrentCutoff() returns fallback initially | test file | == fallbackCutoff |
| 4.6 | Implement process() with fallback path | header | All tests pass |

**Commit**: "feat(dsp): add PitchTrackingFilter fallback processing (FR-011, FR-016)"

---

### Task Group 5: Pitch Detection Integration

**Objective**: Integrate PitchDetector and implement tracking behavior

| # | Task | File | Test |
|---|------|------|------|
| 5.1 | Write test: sine input updates detectedPitch | test file | getDetectedPitch() > 0 |
| 5.2 | Write test: confidence above threshold triggers tracking | test file | Cutoff follows pitch |
| 5.3 | Write test: confidence below threshold uses fallback | test file | Cutoff == fallback |
| 5.4 | Write test: harmonic ratio scales cutoff | test file | cutoff = pitch * ratio |
| 5.5 | Write test: semitone offset shifts cutoff | test file | +12 semitones = 2x |
| 5.6 | Implement pitch tracking in process() | header | All tests pass |

**Commit**: "feat(dsp): add PitchTrackingFilter pitch tracking (FR-001, FR-005, FR-006)"

---

### Task Group 6: Adaptive Tracking Speed

**Objective**: Implement fast tracking for rapid pitch changes

| # | Task | File | Test |
|---|------|------|------|
| 6.1 | Write test: rapid pitch change uses fast tracking | test file | Cutoff follows quickly |
| 6.2 | Write test: slow pitch change uses normal tracking | test file | Cutoff follows slowly |
| 6.3 | Write test: threshold is 10 semitones/sec | test file | Boundary behavior |
| 6.4 | Implement adaptive tracking detection | header | All tests pass |

**Commit**: "feat(dsp): add PitchTrackingFilter adaptive tracking (FR-004a)"

---

### Task Group 7: Block Processing

**Objective**: Implement processBlock() for efficiency

| # | Task | File | Test |
|---|------|------|------|
| 7.1 | Write test: processBlock produces same result as loop | test file | Compare outputs |
| 7.2 | Write test: processBlock with in-place modification | test file | Buffer modified correctly |
| 7.3 | Implement processBlock() | header | All tests pass |

**Commit**: "feat(dsp): add PitchTrackingFilter block processing (FR-015)"

---

### Task Group 8: Filter Types and Resonance

**Objective**: Verify filter type and resonance affect output correctly

| # | Task | File | Test |
|---|------|------|------|
| 8.1 | Write test: lowpass attenuates high frequencies | test file | Frequency response |
| 8.2 | Write test: highpass attenuates low frequencies | test file | Frequency response |
| 8.3 | Write test: bandpass passes around cutoff | test file | Frequency response |
| 8.4 | Write test: high resonance creates peak | test file | Resonance behavior |
| 8.5 | Verify filter type switching works | header | All tests pass |

**Commit**: "feat(dsp): add PitchTrackingFilter type switching (FR-009, FR-010)"

---

### Task Group 9: Documentation and Architecture Update

**Objective**: Update architecture documentation

| # | Task | File | Test |
|---|------|------|------|
| 9.1 | Add PitchTrackingFilter to layer-2-processors.md | specs/_architecture_/layer-2-processors.md | N/A |
| 9.2 | Update FLT-ROADMAP.md to mark complete | specs/FLT-ROADMAP.md | N/A |
| 9.3 | Final pluginval run if integrated | N/A | No new failures |

**Commit**: "docs: add PitchTrackingFilter to architecture docs"

---

## Phase 2 Summary

**Total Tasks**: 41 (9 task groups)
**Estimated Time**: 4-6 hours
**Key Dependencies**: PitchDetector, SVF, OnePoleSmoother (all Layer 1)

### Test Coverage Targets

| Requirement | Test Coverage |
|-------------|---------------|
| FR-001 to FR-006 | Task Groups 3, 5 |
| FR-009 to FR-013 | Task Groups 3, 8 |
| FR-014 to FR-018 | Task Groups 4, 7 |
| FR-019 to FR-021 | Task Group 2 |
| FR-022 to FR-024 | Task Groups 4, 5 |
| SC-001 to SC-007 | Verified by noexcept, real-time patterns |
| SC-008 | Performance benchmark (optional) |

---

## Generated Artifacts

| Artifact | Path | Status |
|----------|------|--------|
| Implementation Plan | `specs/092-pitch-tracking-filter/plan.md` | Complete |
| Research Document | `specs/092-pitch-tracking-filter/research.md` | Complete |
| Data Model | `specs/092-pitch-tracking-filter/data-model.md` | Complete |
| API Contract | `specs/092-pitch-tracking-filter/contracts/pitch_tracking_filter.h` | Complete |
| Quickstart Guide | `specs/092-pitch-tracking-filter/quickstart.md` | Complete |

**Branch**: `092-pitch-tracking-filter`
**Ready for Phase 2 Implementation**: Yes
