# Implementation Plan: Unified Waveshaper Primitive

**Branch**: `052-waveshaper` | **Date**: 2026-01-13 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `/specs/052-waveshaper/spec.md`

## Summary

This plan implements a unified Waveshaper primitive for the KrateDSP library that consolidates all saturation curve types into a single Layer 1 component. The Waveshaper provides a common interface for 9 waveshaping algorithms (Tanh, Atan, Cubic, Quintic, ReciprocalSqrt, Erf, HardClip, Diode, Tube) with configurable drive and asymmetry parameters, delegating to existing Layer 0 Sigmoid/Asymmetric functions.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: Layer 0 `core/sigmoid.h` (Sigmoid:: and Asymmetric:: namespaces)
**Storage**: N/A (stateless primitive)
**Testing**: Catch2 (dsp/tests/unit/primitives/)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) - cross-platform VST3
**Project Type**: KrateDSP shared library (header-only primitive)
**Performance Goals**: < 0.1% CPU per instance (Layer 1 budget per Constitution Principle XI)
**Constraints**: Real-time safe (noexcept, no allocations), header-only
**Scale/Scope**: Single Layer 1 primitive with 9 waveshape types

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Applicable Constitution Principles:**

| Principle | Requirement | Pre-Design Status | Post-Design Status |
|-----------|-------------|-------------------|-------------------|
| II. Real-Time Safety | noexcept, no allocations in process | WILL COMPLY | COMPLIANT - all methods noexcept, no allocations |
| III. Modern C++ | C++20, constexpr, RAII | WILL COMPLY | COMPLIANT - C++20, enum class, default ctors |
| IX. Layered DSP | Layer 1 depends only on Layer 0 | WILL COMPLY | COMPLIANT - only includes core/sigmoid.h |
| X. DSP Constraints | No internal oversampling/DC blocking | WILL COMPLY (by design) | COMPLIANT - no OS/DC in contract |
| XI. Performance | < 0.1% CPU per instance | WILL COMPLY | EXPECTED - simple switch + function call |
| XII. Test-First | Tests before implementation | WILL COMPLY | READY - test plan defined |
| XIV. ODR Prevention | No duplicate classes | VERIFIED (no existing Waveshaper) | VERIFIED - no conflicts found |
| XVI. Honest Completion | All FR/SC verified | N/A (pre-implementation) | N/A (pending implementation) |

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

**Classes/Structs to be created**: `Waveshaper`, `WaveshapeType` (enum class)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| Waveshaper | `grep -r "class Waveshaper" dsp/ plugins/` | No | Create New |
| waveshaper | `grep -r "waveshaper" dsp/ plugins/` | No | Create New |
| WaveshapeType | `grep -r "WaveshapeType" dsp/ plugins/` | No | Create New |
| Shaper | `grep -r "class.*Shaper" dsp/ plugins/` | No | Create New |

**Related Existing Types (Reference Only):**

| Existing Type | Location | Relationship |
|--------------|----------|--------------|
| SaturationType | processors/saturation_processor.h | DIFFERENT enum (Tape/Tube/Transistor/Digital/Diode) - Layer 2, no conflict |

**Utility Functions to be created**: None - all processing delegates to existing Sigmoid/Asymmetric functions

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `Sigmoid::tanh()` | dsp/include/krate/dsp/core/sigmoid.h | 0 | Tanh waveshape type |
| `Sigmoid::atan()` | dsp/include/krate/dsp/core/sigmoid.h | 0 | Atan waveshape type |
| `Sigmoid::softClipCubic()` | dsp/include/krate/dsp/core/sigmoid.h | 0 | Cubic waveshape type |
| `Sigmoid::softClipQuintic()` | dsp/include/krate/dsp/core/sigmoid.h | 0 | Quintic waveshape type |
| `Sigmoid::recipSqrt()` | dsp/include/krate/dsp/core/sigmoid.h | 0 | ReciprocalSqrt waveshape type |
| `Sigmoid::erfApprox()` | dsp/include/krate/dsp/core/sigmoid.h | 0 | Erf waveshape type |
| `Sigmoid::hardClip()` | dsp/include/krate/dsp/core/sigmoid.h | 0 | HardClip waveshape type |
| `Asymmetric::diode()` | dsp/include/krate/dsp/core/sigmoid.h | 0 | Diode waveshape type |
| `Asymmetric::tube()` | dsp/include/krate/dsp/core/sigmoid.h | 0 | Tube waveshape type |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities (sigmoid.h verified)
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (no Waveshaper)
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 (saturation_processor.h has SaturationType - different enum, no conflict)
- [x] `ARCHITECTURE.md` - Component inventory (no Waveshaper listed)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All searches confirm no existing Waveshaper or WaveshapeType classes. The SaturationType enum in Layer 2 is a completely different enumeration with different values and purpose - it controls high-level saturation modes in SaturationProcessor, while WaveshapeType controls low-level transfer function selection. No naming conflict or ODR violation possible.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| Sigmoid | tanh | `[[nodiscard]] constexpr float tanh(float x) noexcept` | Y |
| Sigmoid | atan | `[[nodiscard]] inline float atan(float x) noexcept` | Y |
| Sigmoid | softClipCubic | `[[nodiscard]] inline float softClipCubic(float x) noexcept` | Y |
| Sigmoid | softClipQuintic | `[[nodiscard]] inline float softClipQuintic(float x) noexcept` | Y |
| Sigmoid | recipSqrt | `[[nodiscard]] inline float recipSqrt(float x) noexcept` | Y |
| Sigmoid | erfApprox | `[[nodiscard]] inline float erfApprox(float x) noexcept` | Y |
| Sigmoid | hardClip | `[[nodiscard]] inline constexpr float hardClip(float x, float threshold = 1.0f) noexcept` | Y |
| Asymmetric | diode | `[[nodiscard]] inline float diode(float x) noexcept` | Y |
| Asymmetric | tube | `[[nodiscard]] inline float tube(float x) noexcept` | Y |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/sigmoid.h` - All Sigmoid:: and Asymmetric:: functions

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| Sigmoid::hardClip | Has optional `threshold` parameter (default 1.0f) | `Sigmoid::hardClip(x)` for default threshold |
| Diode/Tube | Output is unbounded (can exceed [-1, 1]) | Document for users; no post-limiting in Waveshaper |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

**Not Applicable**: This is a Layer 1 primitive. All utility functions already exist in Layer 0 (sigmoid.h).

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| Internal switch/dispatch | Class-specific routing to waveshape types |

**Decision**: No Layer 0 extraction needed. All mathematical functions already exist in sigmoid.h.

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 1 (Primitives)

**Related features at same layer** (from DST-ROADMAP):
- `primitives/hard_clip_adaa.h` - ADAA hard clipping (may share WaveshapeType enum if extended)
- `primitives/wavefolder.h` - Wavefolding (different algorithm family, no shared code)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| WaveshapeType enum | MEDIUM | hard_clip_adaa (possible), future saturation primitives | Keep in waveshaper.h for now |
| Waveshaper class | HIGH | SaturationProcessor (Layer 2), TubeStage, FuzzProcessor, feedback saturation | This is the point - unified primitive |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep WaveshapeType with Waveshaper | Single primitive, enum only used here currently |
| No shared base class | First unified waveshaper - patterns not yet established |

### Review Trigger

After implementing **hard_clip_adaa** or **wavefolder**, review:
- [ ] Does hard_clip_adaa need WaveshapeType? -> Consider moving enum to separate header
- [ ] Any duplicated transfer function logic? -> Already covered by Sigmoid:: namespace

## Project Structure

### Documentation (this feature)

```text
specs/052-waveshaper/
+-- plan.md              # This file
+-- research.md          # Phase 0 output
+-- data-model.md        # Phase 1 output
+-- quickstart.md        # Phase 1 output
+-- contracts/           # Phase 1 output
    +-- waveshaper.h     # API contract
+-- tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
dsp/
+-- include/krate/dsp/
    +-- primitives/
        +-- waveshaper.h         # Header-only implementation (NEW)
+-- tests/
    +-- unit/primitives/
        +-- waveshaper_test.cpp  # Unit tests (NEW)
```

**Structure Decision**: Standard Layer 1 primitive layout. Header-only implementation in primitives/, unit tests in tests/unit/primitives/.

## Complexity Tracking

> No Constitution violations identified. No complexity exceptions needed.

| Aspect | Complexity Level | Notes |
|--------|------------------|-------|
| Dependencies | Low | Only Layer 0 sigmoid.h |
| API Surface | Low | 3 setters, 3 getters, 2 process methods |
| Implementation | Low | Switch-based dispatch to existing functions |
| Testing | Medium | 9 waveshape types x edge cases |

