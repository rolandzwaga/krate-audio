# Implementation Plan: DiodeClipper Processor

**Branch**: `060-diode-clipper` | **Date**: 2026-01-14 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/060-diode-clipper/spec.md`

## Summary

Layer 2 processor implementing configurable diode clipping circuit modeling with four diode types (Silicon, Germanium, LED, Schottky), three topologies (Symmetric, Asymmetric, SoftHard), and per-instance configurable parameters (forward voltage, knee sharpness, output level). No internal oversampling - users wrap externally with Oversampler<> if needed.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: Layer 0 (db_utils.h, sigmoid.h), Layer 1 (dc_blocker.h, smoother.h)
**Storage**: N/A (stateless except for smoothers and DC blocker)
**Testing**: Catch2 (via dsp_tests executable)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) - cross-platform
**Project Type**: DSP Library (monorepo structure)
**Performance Goals**: < 0.5% CPU per mono instance @ 44.1kHz (Layer 2 budget)
**Constraints**: Real-time safe (noexcept, no allocations in process), zero latency
**Scale/Scope**: Single mono processor, composable with Oversampler externally

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle II (Real-Time Safety):**
- [x] All process methods will be noexcept
- [x] No memory allocation in audio thread (pre-allocate in prepare())
- [x] No blocking operations (mutex, I/O) in process path

**Principle III (Modern C++):**
- [x] Uses C++20 features (constexpr, [[nodiscard]], etc.)
- [x] RAII for resource management
- [x] No raw new/delete

**Principle IX (Layer Architecture):**
- [x] Layer 2 - depends only on Layer 0 and Layer 1
- [x] Uses existing DCBlocker (Layer 1), OnePoleSmoother (Layer 1)
- [x] Uses existing Asymmetric::diode() (Layer 0) as foundation

**Principle X (DSP Constraints):**
- [x] DC blocking after asymmetric saturation (required for asymmetric topology)
- [x] No internal oversampling (users wrap externally per DST-ROADMAP)
  - **Justification**: Constitution X requires "Oversampling (min 2x) for saturation/distortion/waveshaping" but DST-ROADMAP Section 3.1 establishes the "composable anti-aliasing" pattern where Layer 2 processors are "pure" (no internal oversampling) and users wrap with Oversampler<> externally. This provides maximum flexibility: users choose when/how to oversample based on their specific CPU budget and quality needs.

**Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: DiodeClipper, DiodeType (enum), ClipperTopology (enum)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| DiodeClipper | `grep -r "class DiodeClipper" dsp/ plugins/` | No | Create New |
| DiodeType | `grep -r "DiodeType" dsp/ plugins/` | No | Create New |
| ClipperTopology | `grep -r "ClipperTopology" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: None - will use existing Asymmetric::diode() and Sigmoid functions

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| diode clipping math | `grep -r "Asymmetric::diode" dsp/` | Yes | sigmoid.h | Reuse and extend |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| Asymmetric::diode() | dsp/include/krate/dsp/core/sigmoid.h | 0 | Foundation for diode transfer function |
| Sigmoid::hardClip() | dsp/include/krate/dsp/core/sigmoid.h | 0 | Hard clipping for SoftHard topology |
| dbToGain | dsp/include/krate/dsp/core/db_utils.h | 0 | dB to linear conversion for gains |
| DCBlocker | dsp/include/krate/dsp/primitives/dc_blocker.h | 1 | DC offset removal after asymmetric clipping |
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | 1 | Parameter smoothing for click-free changes |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors
- [x] `ARCHITECTURE.md` - Component inventory

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (DiodeClipper, DiodeType, ClipperTopology) are unique and not found in codebase. Existing SaturationType enum in saturation_processor.h has a "Diode" value but this is a different concept (saturation type vs diode clipper type). No namespace collisions.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| DCBlocker | prepare | `void prepare(double sampleRate, float cutoffHz = 10.0f) noexcept` | Yes |
| DCBlocker | reset | `void reset() noexcept` | Yes |
| DCBlocker | process | `[[nodiscard]] float process(float x) noexcept` | Yes |
| DCBlocker | processBlock | `void processBlock(float* buffer, size_t numSamples) noexcept` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `ITERUM_NOINLINE void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| OnePoleSmoother | getCurrentValue | `[[nodiscard]] float getCurrentValue() const noexcept` | Yes |
| OnePoleSmoother | isComplete | `[[nodiscard]] bool isComplete() const noexcept` | Yes |
| Asymmetric::diode | function | `[[nodiscard]] inline float diode(float x) noexcept` | Yes |
| Sigmoid::hardClip | function | `[[nodiscard]] inline constexpr float hardClip(float x, float threshold = 1.0f) noexcept` | Yes |
| dbToGain | function | `[[nodiscard]] constexpr float dbToGain(float dB) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/dc_blocker.h` - DCBlocker class
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/core/sigmoid.h` - Asymmetric::diode(), Sigmoid::hardClip()
- [x] `dsp/include/krate/dsp/core/db_utils.h` - dbToGain()
- [x] `dsp/include/krate/dsp/processors/saturation_processor.h` - Reference pattern
- [x] `dsp/include/krate/dsp/processors/tube_stage.h` - Reference pattern

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| OnePoleSmoother | Uses `snapTo()` not `snap()` | `smoother.snapTo(value)` |
| OnePoleSmoother | Constructor doesn't take config | Call `configure()` after construction |
| DCBlocker | prepare() must be called before process() | Call in prepare(), returns input unchanged if not prepared |
| Asymmetric::diode | Output is unbounded (can exceed [-1, 1]) | May need output limiting/gain compensation |

## Layer 0 Candidate Analysis

*No new Layer 0 utilities needed. Using existing Asymmetric::diode() and will create configurable version inline in the processor.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| - | - | - | - |

**Decision**: No extraction needed. The diode clipping logic will be parameterized within the processor class. If future processors need configurable diode functions, extraction can be considered then.

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer** (from ROADMAP.md or known plans):
- FET Clipper (potential future feature)
- JFET/MOSFET distortion processors
- Tube stage variations (existing TubeStage)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| DiodeType enum | MEDIUM | Other clipping processors | Keep local, extract if needed |
| ClipperTopology enum | HIGH | Any asymmetric processor | Keep local, generalize if 2nd use |
| Configurable diode function | MEDIUM | FET/tube processors | Keep as member, extract if pattern emerges |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep DiodeType local | First use of this concept, no proven reuse yet |
| Keep ClipperTopology local | May generalize to "DistortionTopology" if other processors need it |
| No shared base class | Pattern not established for configurable nonlinear processors |

### Review Trigger

After implementing **FET Clipper** (if any), review this section:
- [ ] Does FET Clipper need DiodeType or similar? -> Consider shared enum
- [ ] Does FET Clipper use same topology concept? -> Extract to common
- [ ] Any duplicated parameter smoothing patterns? -> Consider base class

## Project Structure

### Documentation (this feature)

```text
specs/060-diode-clipper/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output (API contracts)
└── tasks.md             # Phase 2 output (NOT created by plan)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── processors/
│       └── diode_clipper.h     # NEW: DiodeClipper class
└── tests/
    └── unit/
        └── processors/
            └── diode_clipper_test.cpp  # NEW: Unit tests
```

**Structure Decision**: Single header file in Layer 2 processors directory, following SaturationProcessor and TubeStage patterns. Header-only implementation for template/inline efficiency.

## Complexity Tracking

> No Constitution Check violations requiring justification.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| - | - | - |
