# Implementation Plan: Granular Distortion Processor

**Branch**: `113-granular-distortion` | **Date**: 2026-01-27 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `/specs/113-granular-distortion/spec.md`

## Summary

A Layer 2 DSP processor that applies distortion in time-windowed micro-grains. Unlike static waveshaping which applies the same transfer function continuously, GranularDistortion breaks audio into overlapping micro-grains (5-100ms) and applies distortion independently to each grain with per-grain drive variation, algorithm variation, and position jitter. This creates evolving, textured "destruction" effects impossible with traditional distortion.

**Technical approach**: Compose existing granular infrastructure (GrainPool, GrainScheduler, GrainEnvelope) with Waveshaper primitives. Each grain embeds its own Waveshaper instance. A circular input buffer provides source material, and grains read from frozen positions with jitter. Aliasing is accepted as intentional aesthetic (no oversampling per spec clarification).

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**:
- GrainPool (Layer 1) - 64-grain management with voice stealing
- GrainScheduler (Layer 2) - Trigger timing with density control
- GrainEnvelope (Layer 0) - Window functions (Hann default)
- Waveshaper (Layer 1) - 9 distortion algorithms
- Xorshift32 (Layer 0) - Fast RNG for variation
- OnePoleSmoother (Layer 1) - Parameter smoothing

**Storage**: N/A (internal circular buffer, no persistence)
**Testing**: Catch2 (dsp_tests target)
**Target Platform**: Windows, macOS, Linux (cross-platform C++)
**Project Type**: DSP library component (KrateDSP)
**Performance Goals**: Process 1024-sample block in < 10% of block duration at 44100Hz (< 2.3ms) (Layer 2 budget)
**Constraints**: < 256KB memory per instance, zero allocations in process()
**Scale/Scope**: Single mono processor; stereo via two instances

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle II (Real-Time Audio Thread Safety):**
- [x] No allocations in process() - all buffers pre-allocated in prepare()
- [x] No locks/mutexes - grain pool uses fixed arrays
- [x] No exceptions - all methods noexcept
- [x] No I/O - pure DSP processing

**Principle IX (Layered Architecture):**
- [x] Layer 2 processor at correct location: `dsp/include/krate/dsp/processors/`
- [x] Only depends on Layers 0-1 (GrainPool, Waveshaper, etc.)
- [x] GrainScheduler is Layer 2 but same-layer dependencies are allowed

**Principle X (DSP Processing Constraints):**
- [x] **Constitution Exception**: Aliasing is accepted as intentional "Digital Destruction" aesthetic per spec clarification. This is a documented deviation from Principle X's oversampling requirement - the Digital Destruction category (specs 111, 112, 113, 114) intentionally exploits digital artifacts.
- [x] Waveshaper asymmetry = 0.0 (symmetric), no DC offset generated
- [x] Parameter smoothing (10ms) for click-free automation

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

**Classes/Structs to be created**: GranularDistortion, DistortionGrain (internal)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| GranularDistortion | `grep -r "class GranularDistortion" dsp/ plugins/` | No | Create New |
| DistortionGrain | `grep -r "struct DistortionGrain" dsp/ plugins/` | No | Create New (internal struct) |

**Note**: The existing `Grain` struct in grain_pool.h lacks waveshaper-specific fields (drive, algorithm type). We need a specialized `DistortionGrain` that extends or wraps `Grain` with distortion parameters.

**Utility Functions to be created**: None - all math utilities exist in Layer 0

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| N/A | — | — | — | — |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| GrainPool | dsp/include/krate/dsp/primitives/grain_pool.h | 1 | Grain allocation with voice stealing (64 max) |
| GrainScheduler | dsp/include/krate/dsp/processors/grain_scheduler.h | 2 | Trigger timing based on density parameter |
| GrainEnvelope | dsp/include/krate/dsp/core/grain_envelope.h | 0 | Hann window generation and lookup |
| Waveshaper | dsp/include/krate/dsp/primitives/waveshaper.h | 1 | Per-grain distortion processing |
| Xorshift32 | dsp/include/krate/dsp/core/random.h | 0 | RNG for drive/algorithm variation |
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | 1 | Parameter smoothing (drive, mix) |
| flushDenormal | dsp/include/krate/dsp/core/db_utils.h | 0 | Denormal flushing |
| isNaN, isInf | dsp/include/krate/dsp/core/db_utils.h | 0 | Input validation |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities (no conflicts)
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (no conflicts)
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (no existing GranularDistortion)
- [x] `specs/_architecture_/` - Component inventory checked

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: The planned `GranularDistortion` class is unique and not found in codebase. The internal `DistortionGrain` struct will be in an anonymous namespace or as a nested class to prevent external linkage conflicts. All other dependencies are existing components to be reused.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| GrainPool | kMaxGrains | `static constexpr size_t kMaxGrains = 64` | Y |
| GrainPool | prepare | `void prepare(double sampleRate) noexcept` | Y |
| GrainPool | reset | `void reset() noexcept` | Y |
| GrainPool | acquireGrain | `[[nodiscard]] Grain* acquireGrain(size_t currentSample) noexcept` | Y |
| GrainPool | releaseGrain | `void releaseGrain(Grain* grain) noexcept` | Y |
| GrainPool | activeGrains | `[[nodiscard]] std::span<Grain* const> activeGrains() noexcept` | Y |
| GrainScheduler | prepare | `void prepare(double sampleRate) noexcept` | Y |
| GrainScheduler | reset | `void reset() noexcept` | Y |
| GrainScheduler | setDensity | `void setDensity(float grainsPerSecond) noexcept` | Y |
| GrainScheduler | process | `[[nodiscard]] bool process() noexcept` | Y |
| GrainScheduler | seed | `void seed(uint32_t seedValue) noexcept` | Y |
| GrainEnvelope::generate | generate | `inline void generate(float* output, size_t size, GrainEnvelopeType type, float attackRatio = 0.1f, float releaseRatio = 0.1f) noexcept` | Y |
| GrainEnvelope::lookup | lookup | `[[nodiscard]] inline float lookup(const float* table, size_t tableSize, float phase) noexcept` | Y |
| Waveshaper | setType | `void setType(WaveshapeType type) noexcept` | Y |
| Waveshaper | setDrive | `void setDrive(float drive) noexcept` | Y |
| Waveshaper | setAsymmetry | `void setAsymmetry(float bias) noexcept` | Y |
| Waveshaper | process | `[[nodiscard]] float process(float x) const noexcept` | Y |
| Xorshift32 | nextFloat | `[[nodiscard]] constexpr float nextFloat() noexcept` (returns [-1, 1]) | Y |
| Xorshift32 | nextUnipolar | `[[nodiscard]] constexpr float nextUnipolar() noexcept` (returns [0, 1]) | Y |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Y |
| OnePoleSmoother | setTarget | `void setTarget(float target) noexcept` | Y |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Y |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Y |
| Grain | readPosition | `float readPosition = 0.0f` | Y |
| Grain | envelopePhase | `float envelopePhase = 0.0f` | Y |
| Grain | envelopeIncrement | `float envelopeIncrement = 0.0f` | Y |
| Grain | active | `bool active = false` | Y |
| Grain | startSample | `size_t startSample = 0` | Y |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/grain_pool.h` - GrainPool class, Grain struct
- [x] `dsp/include/krate/dsp/processors/grain_scheduler.h` - GrainScheduler class
- [x] `dsp/include/krate/dsp/core/grain_envelope.h` - GrainEnvelope namespace functions
- [x] `dsp/include/krate/dsp/primitives/waveshaper.h` - Waveshaper class, WaveshapeType enum
- [x] `dsp/include/krate/dsp/core/random.h` - Xorshift32 class
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| Xorshift32 | `nextFloat()` returns [-1, 1], `nextUnipolar()` returns [0, 1] | Use `nextFloat()` for bipolar variation |
| GrainScheduler | `setDensity()` takes grains/second, not overlap count | density=4 means 4 grains/sec, need to calculate from grainSize |
| Waveshaper | `setDrive(0)` returns 0.0, not pass-through | Always use drive >= 1.0 |
| OnePoleSmoother | `snapTo()` sets BOTH current AND target | Use for initialization, `setTarget()` for smooth changes |
| Grain | `readPosition` is samples from write head, not absolute | Store as offset from current write position |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| N/A | All needed utilities already exist in Layer 0 | — | — |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| msToSamples | One-liner conversion, only used internally, class stores sampleRate_ |
| calculateGrainDrive | Per-grain formula with variation, specific to this processor |
| selectGrainAlgorithm | Random selection logic, specific to this processor |

**Decision**: No new Layer 0 utilities needed. All math/conversion utilities exist.

## Higher-Layer Reusability Analysis

*Forward-looking analysis: What code from THIS feature could be reused by SIBLING features at the same layer?*

### Sibling Features Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer** (from DST-ROADMAP.md Priority 8 - Digital Destruction):
- 111-bitwise-mangler - Bit manipulation distortion (completed)
- 112-aliasing-effect - Intentional aliasing processor (completed)
- 114-fractal-distortion - Recursive multi-scale distortion (future)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Per-grain distortion pattern | MEDIUM | 114-fractal-distortion could use similar grain-based approach | Keep local |
| Circular input buffer | LOW | Already exists in DelayLine, this is just standard pattern | Keep local |
| Grain + Waveshaper composition | MEDIUM | Pattern is documented but likely diverges per use case | Keep local |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep all code local | This is third "Digital Destruction" processor; patterns are established in 111/112. GranularDistortion is unique enough that extraction is premature. |
| No shared base class | Each destruction processor has different processing topology |

### Review Trigger

After implementing **114-fractal-distortion**, review this section:
- [ ] Does fractal distortion need per-grain processing? -> Consider extraction
- [ ] Any duplicated variation/randomization code? -> Consider shared utilities

## Project Structure

### Documentation (this feature)

```text
specs/113-granular-distortion/
├── spec.md              # Feature specification (exists)
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
└── contracts/           # Phase 1 output (API documentation)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── processors/
│       └── granular_distortion.h    # NEW: Main processor header
└── tests/
    └── processors/
        └── granular_distortion_test.cpp  # NEW: Test file
```

**Structure Decision**: Standard Layer 2 processor structure. Single header file with all implementation (header-only like other processors). Tests in parallel directory structure.

## Complexity Tracking

> **No Constitution violations requiring justification.**

All design decisions align with constitution principles:
- Layer 2 placement is correct (composes Layer 0-1 components)
- No oversampling needed (per spec clarification on aliasing)
- Memory footprint well under 256KB (64 Waveshapers + 19200 sample buffer)

---

## Phase 0: Research

### Research Questions

1. **Grain-to-density mapping**: How to map density parameter [1-8] to GrainScheduler's grains/second?
2. **Buffer size calculation**: Exact buffer size for 100ms at 192kHz?
3. **Position jitter clamping**: How to track available buffer history for dynamic clamping?
4. **Grain completion detection**: Best approach to detect grain completion and release?

### Research Findings

See [research.md](./research.md) for detailed findings.

---

## Phase 1: Design

### Data Model

See [data-model.md](./data-model.md) for entity definitions.

### API Contracts

See [contracts/](./contracts/) for API documentation.

### Quickstart

See [quickstart.md](./quickstart.md) for usage examples.
