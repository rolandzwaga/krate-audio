# Implementation Plan: Filter Feedback Matrix

**Branch**: `096-filter-feedback-matrix` | **Date**: 2026-01-24 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/096-filter-feedback-matrix/spec.md`

## Summary

Implement `FilterFeedbackMatrix<N>`, a Layer 3 System Component that creates a network of 2-4 SVF filters with configurable cross-feedback routing. The design uses:
- Template parameter `N` for compile-time fixed-size arrays with runtime `setActiveFilters(count)` for CPU efficiency
- Per-filter soft clipping (tanh) before feedback routing for stability
- Dual-mono architecture for stereo (independent networks per channel)
- Linear interpolation for feedback path delays
- Per-feedback-path DC blocking after each delay line

This component enables complex resonant textures by routing filter outputs back into other filters with adjustable amounts and delays, similar to Feedback Delay Networks (FDN) but with filters instead of pure delays.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: SVF (Layer 1), DelayLine (Layer 1), DCBlocker (Layer 1), OnePoleSmoother (Layer 1)
**Storage**: N/A (all state is in-memory)
**Testing**: Catch2 via CTest (Constitution Principle XII: Test-First Development)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: DSP library component (monorepo)
**Performance Goals**: <1% CPU single core at 44.1kHz stereo (Layer 3 budget per Constitution Principle XI)
**Constraints**: Zero allocations in process(), noexcept processing, real-time safe
**Scale/Scope**: 2-4 filters (complexity grows as N^2), max 100ms feedback delay

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Pre-Design Gate Check:**

| Principle | Status | Evidence |
|-----------|--------|----------|
| II: Real-Time Safety | PASS | Design uses noexcept, pre-allocated arrays, no allocations in process |
| III: Modern C++ | PASS | Using C++20, templates, RAII, move-only semantics (DelayLine is not copyable) |
| IX: Layered Architecture | PASS | Layer 3 component, depends on Layer 0-1 only (SVF, DelayLine, DCBlocker, OnePoleSmoother) |
| X: DSP Constraints | PASS | Feedback >100% includes soft limiting (tanh), DC blocking in feedback path |
| XI: Performance Budget | PASS | Layer 3 target <1% CPU, will verify with tests |
| XIV: ODR Prevention | PASS | Searched codebase, no FilterFeedbackMatrix exists |
| XVI: Honest Completion | PENDING | Will complete compliance table at implementation end |

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

**Classes/Structs to be created**: FilterFeedbackMatrix, FeedbackPath (internal)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| FilterFeedbackMatrix | `grep -r "class FilterFeedbackMatrix" dsp/ plugins/` | No | Create New |
| FilterMatrix | `grep -r "class FilterMatrix" dsp/ plugins/` | No | Create New (using FilterFeedbackMatrix name) |
| FeedbackPath | `grep -r "struct FeedbackPath" dsp/ plugins/` | No | Create New (internal struct) |

**Utility Functions to be created**: None (all functionality is class methods)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| N/A | N/A | N/A | N/A | N/A |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| SVF | dsp/include/krate/dsp/primitives/svf.h | 1 | TPT SVF for each filter in the matrix |
| DelayLine | dsp/include/krate/dsp/primitives/delay_line.h | 1 | Feedback path delays with linear interpolation |
| DCBlocker | dsp/include/krate/dsp/primitives/dc_blocker.h | 1 | DC blocking after each feedback delay |
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | 1 | Parameter smoothing for feedback amounts, delays |
| flushDenormal | dsp/include/krate/dsp/core/db_utils.h | 0 | Denormal flushing in feedback loop |
| isNaN/isInf | dsp/include/krate/dsp/core/db_utils.h | 0 | Input validation |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/systems/` - Layer 3 systems (checked feedback_network.h, flexible_feedback_network.h)
- [x] `specs/_architecture_/` - Component inventory (README.md, layer-3-systems.md)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: Searched for "FilterFeedbackMatrix", "FilterMatrix", and "feedback.*matrix" patterns. No existing implementations found. The FeedbackNetwork and FlexibleFeedbackNetwork are different components (delay-based, not filter-based). The new class uses a unique name and doesn't conflict with any existing types.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| SVF | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| SVF | setMode | `void setMode(SVFMode mode) noexcept` | Yes |
| SVF | setCutoff | `void setCutoff(float hz) noexcept` | Yes |
| SVF | setResonance | `void setResonance(float q) noexcept` | Yes |
| SVF | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| SVF | reset | `void reset() noexcept` | Yes |
| DelayLine | prepare | `void prepare(double sampleRate, float maxDelaySeconds) noexcept` | Yes |
| DelayLine | write | `void write(float sample) noexcept` | Yes |
| DelayLine | readLinear | `[[nodiscard]] float readLinear(float delaySamples) const noexcept` | Yes |
| DelayLine | reset | `void reset() noexcept` | Yes |
| DelayLine | sampleRate | `[[nodiscard]] double sampleRate() const noexcept` | Yes |
| DCBlocker | prepare | `void prepare(double sampleRate, float cutoffHz = 10.0f) noexcept` | Yes |
| DCBlocker | process | `[[nodiscard]] float process(float x) noexcept` | Yes |
| DCBlocker | reset | `void reset() noexcept` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `ITERUM_NOINLINE void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| OnePoleSmoother | isComplete | `[[nodiscard]] bool isComplete() const noexcept` | Yes |
| detail::flushDenormal | function | `inline constexpr float flushDenormal(float x) noexcept` | Yes |
| detail::isNaN | function | `inline constexpr bool isNaN(float x) noexcept` | Yes |
| detail::isInf | function | `inline constexpr bool isInf(float x) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/svf.h` - SVF class
- [x] `dsp/include/krate/dsp/primitives/delay_line.h` - DelayLine class
- [x] `dsp/include/krate/dsp/primitives/dc_blocker.h` - DCBlocker class
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/core/db_utils.h` - Utility functions
- [x] `dsp/include/krate/dsp/systems/feedback_network.h` - Reference for feedback patterns
- [x] `dsp/include/krate/dsp/systems/flexible_feedback_network.h` - Reference for limiting patterns

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| SVF | Returns input unchanged if prepare() not called | Always call prepare() before processing |
| SVF | kMaxQ is 30.0f, not unbounded | Clamp Q to [0.5, 30.0] per FR-003 |
| DelayLine | readLinear takes samples, not ms | Convert ms to samples: `ms * sampleRate / 1000.0` |
| DelayLine | Need to call write() before read() each sample | Use read-before-write pattern |
| DCBlocker | Default cutoff is 10Hz | Acceptable for feedback path DC blocking |
| OnePoleSmoother | setTarget uses ITERUM_NOINLINE for NaN safety | Works correctly, no special handling needed |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| N/A | No new Layer 0 utilities needed | N/A | N/A |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| msToSamples | One-liner, class stores sampleRate_, used only internally |
| softClip (tanh wrapper) | Single consumer, inline in process loop |

**Decision**: No Layer 0 extractions. All utilities are class-specific and won't benefit other components.

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 3 - System Components

**Related features at same layer** (from spec.md Forward Reusability):
- FilterStepSequencer (Phase 17.1) - could use FilterFeedbackMatrix as processing core
- VowelSequencer (Phase 17.2) - similar matrix-based routing potential
- TimeVaryingCombBank (Phase 18.3) - related feedback network concepts

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| FilterFeedbackMatrix | MEDIUM | FilterStepSequencer (unlikely - different use case) | Keep local |
| Matrix smoothing pattern | LOW | VowelSequencer (different matrix structure) | Keep local |
| Per-filter tanh limiting | MEDIUM | Other high-feedback systems | Keep local, extract if pattern repeats |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared base class | First filter-matrix at this layer - patterns not established |
| No matrix utility extraction | NxN feedback matrix is specific to this component's topology |

### Review Trigger

After implementing **FilterStepSequencer**, review this section:
- [ ] Does FilterStepSequencer need FilterFeedbackMatrix or similar? -> Consider base class
- [ ] Does FilterStepSequencer use same composition pattern? -> Document shared pattern
- [ ] Any duplicated code? -> Consider shared utilities

## Project Structure

### Documentation (this feature)

```text
specs/096-filter-feedback-matrix/
├── spec.md              # Feature specification
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output
│   └── api.h            # Public API contract
└── tasks.md             # Phase 2 output (NOT created by /speckit.plan)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── systems/
│       └── filter_feedback_matrix.h    # Main header (Layer 3)
└── tests/
    └── systems/
        └── filter_feedback_matrix_tests.cpp  # Unit tests
```

**Structure Decision**: Single header in systems/ following the established Layer 3 pattern. Tests mirror source structure. No separate .cpp file needed (header-only template class).

## Complexity Tracking

No constitution violations requiring justification.

---

## Phase 0 Complete - Research Findings

See [research.md](research.md) for detailed findings.

## Phase 1 Complete - Design Artifacts

See:
- [data-model.md](data-model.md) - Entity definitions and state machines
- [contracts/api.h](contracts/api.h) - Public API contract
- [quickstart.md](quickstart.md) - Usage examples

## Post-Design Constitution Re-Check

| Principle | Status | Evidence |
|-----------|--------|----------|
| II: Real-Time Safety | PASS | All process methods noexcept, fixed-size arrays, no allocations |
| III: Modern C++ | PASS | C++20 templates, std::array, move-only semantics (DelayLine is not copyable) |
| IX: Layered Architecture | PASS | Uses only Layer 0-1 dependencies |
| X: DSP Constraints | PASS | tanh soft clipping per filter, DC blocking per path |
| XI: Performance Budget | PASS | <1% CPU target, efficient matrix-free update |
| XIV: ODR Prevention | PASS | Unique class name, no conflicts |
