# Implementation Plan: ADSR Envelope Generator

**Branch**: `032-adsr-envelope-generator` | **Date**: 2026-02-06 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/032-adsr-envelope-generator/spec.md`

## Summary

Implement a Layer 1 DSP primitive ADSR envelope generator (`ADSREnvelope`) that produces time-varying amplitude envelopes with five states (Idle, Attack, Decay, Sustain, Release). Uses the EarLevel Engineering one-pole iterative approach (`output = base + output * coef`) for efficient per-sample computation requiring only 1 multiply + 1 add. Supports three curve shapes (Exponential, Linear, Logarithmic) via target ratio parameter, hard retrigger and legato modes, optional velocity scaling, and real-time safe parameter changes. This is Phase 1.1 of the synth roadmap -- the foundational building block for all future synth voice and polyphonic engine components.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: Layer 0 only (`<krate/dsp/core/db_utils.h>` for `isNaN()`, `flushDenormal()`), `<cmath>` for `std::exp`/`std::log`
**Storage**: N/A (stateless primitive, all state is in-memory)
**Testing**: Catch2 (dsp_tests executable) *(Constitution Principle XII: Test-First Development)*
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: DSP library component (header-only in `dsp/include/krate/dsp/primitives/`)
**Performance Goals**: < 0.01% CPU at 44,100Hz (SC-003); 1 multiply + 1 add per sample
**Constraints**: Zero allocations in process path; all methods noexcept; real-time safe
**Scale/Scope**: Single header-only class (~400 lines) + test file (~800 lines)

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Pre-Design Check**: PASSED

**Required Check - Principle II (Real-Time Audio Thread Safety):**
- [x] No memory allocations in process path
- [x] No locks, mutexes, or blocking primitives
- [x] No file I/O, network ops, or system calls
- [x] No throw/catch exceptions
- [x] All buffers pre-allocated (none needed -- pure scalar computation)

**Required Check - Principle IX (Layered DSP Architecture):**
- [x] Component at Layer 1 (primitives)
- [x] Dependencies only on Layer 0 (core utilities) and stdlib
- [x] No circular dependencies

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Required Check - Principle IV (SIMD & DSP Optimization):**
- [x] SIMD viability analysis completed (verdict: NOT BENEFICIAL)
- [x] Scalar-first workflow will be followed

**Post-Design Re-Check**: PASSED
- All design decisions comply with constitution
- No violations requiring justification

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: `ADSREnvelope`, `ADSRStage` (enum), `EnvCurve` (enum), `RetriggerMode` (enum)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| ADSREnvelope | `grep -r "class ADSREnvelope" dsp/ plugins/` | No | Create New |
| ADSRStage | `grep -r "ADSRStage" dsp/ plugins/` | No | Create New |
| EnvCurve | `grep -r "EnvCurve" dsp/ plugins/` | No | Create New |
| RetriggerMode | `grep -r "RetriggerMode" dsp/ plugins/` | No | Create New |
| EnvelopeStage | `grep -r "EnvelopeStage" dsp/ plugins/` | Yes (struct in multistage_env_filter.h, Layer 2) | Avoid -- use ADSRStage instead |

**Utility Functions to be created**: `calcCoefficients` (private static method)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| calcCoefficients | `grep -r "calcCoefficients" dsp/ plugins/` | No | — | Create New (private to ADSREnvelope) |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `detail::isNaN()` | dsp/include/krate/dsp/core/db_utils.h | 0 | NaN validation in parameter setters |
| `detail::flushDenormal()` | dsp/include/krate/dsp/core/db_utils.h | 0 | Safety net denormal flush (belt-and-suspenders) |
| `ITERUM_NOINLINE` macro | dsp/include/krate/dsp/primitives/smoother.h | 1 | Redefine locally with `#ifndef` guard to avoid cross-Layer-1 include |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities (no conflicts)
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (no existing ADSR or envelope generator)
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 (EnvelopeStage struct exists as different type in multistage_env_filter.h; EnvelopeFollower is analysis, not generation)
- [x] `specs/_architecture_/` - Component inventory confirmed no ADSR primitive exists
- [x] `plugins/disrumpo/src/dsp/sweep_envelope.h` - Different namespace (Disrumpo), different purpose (envelope follower wrapper)
- [x] `dsp/include/krate/dsp/core/grain_envelope.h` - Different purpose (lookup table grain envelopes), no naming conflict

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (ADSREnvelope, ADSRStage, EnvCurve, RetriggerMode) are unique and not found in the codebase. The existing `EnvelopeStage` struct in `multistage_env_filter.h` is avoided by using the distinct name `ADSRStage` for our enum class. The `ITERUM_NOINLINE` macro is guarded with `#ifndef` to coexist safely.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| db_utils.h | isNaN | `constexpr bool isNaN(float x) noexcept` (in `detail` namespace) | Yes |
| db_utils.h | flushDenormal | `[[nodiscard]] inline constexpr float flushDenormal(float x) noexcept` (in `detail` namespace) | Yes |
| db_utils.h | kDenormalThreshold | `inline constexpr float kDenormalThreshold = 1e-15f` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/db_utils.h` - isNaN(), flushDenormal(), kDenormalThreshold
- [x] `dsp/include/krate/dsp/core/math_constants.h` - kPi (not needed for ADSR)
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - ITERUM_NOINLINE macro, OnePoleSmoother patterns
- [x] `dsp/include/krate/dsp/primitives/lfo.h` - Layer 1 lifecycle pattern reference (prepare/reset)

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| db_utils.h | isNaN and flushDenormal are in `detail` namespace | `detail::isNaN(x)`, `detail::flushDenormal(x)` |
| db_utils.h | flushDenormal uses kDenormalThreshold=1e-15, our idle threshold is 1e-4 | Use kEnvelopeIdleThreshold for stage transitions, flushDenormal only as safety net |
| smoother.h | ITERUM_NOINLINE requires `/fp:fast` disabled source file for NaN checks | Use `#ifndef ITERUM_NOINLINE` guard to redefine locally |

## Layer 0 Candidate Analysis

*This is a Layer 1 primitive -- not applicable for Layer 0 extraction.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| `ITERUM_NOINLINE` macro | Used by 2+ Layer 1 primitives (smoother.h, adsr_envelope.h) | `core/compiler_utils.h` | smoother.h, adsr_envelope.h, future primitives |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| `calcCoefficients()` | ADSR-specific target ratio formula; only this class uses it |

**Decision**: Keep `calcCoefficients()` as a private static method in ADSREnvelope. The `ITERUM_NOINLINE` macro could be extracted to Layer 0 in a future cleanup spec, but for now we will redefine it with a `#ifndef` guard, which is safe since macros are not subject to ODR.

## SIMD Optimization Analysis

*GATE: Must complete during planning. Constitution Principle IV requires evaluating SIMD viability for all DSP features.*

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | YES | Each sample depends on previous: `output = base + output * coef` |
| **Data parallelism width** | 1 (single instance) | One envelope per voice; parallelism is at the voice level, not within a single envelope |
| **Branch density in inner loop** | LOW | 1 conditional per sample (stage transition check) |
| **Dominant operations** | Arithmetic (1 mul + 1 add) | Trivially cheap per sample |
| **Current CPU budget vs expected usage** | 0.01% budget vs ~0.001% expected | 100x headroom; optimization unnecessary |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: The per-sample operation is a single multiply-add with a feedback dependency (each sample's output feeds into the next sample's computation). This serial dependency prevents SIMD parallelization within a single envelope instance. The algorithm is already trivially cheap at ~0.001% CPU. SIMD parallelization across multiple voices (SoA layout for N envelopes) is a concern for the future Polyphonic Synth Engine (Phase 3.2), not for this Layer 1 primitive.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Early-out in processBlock when Idle | Skip all computation when envelope inactive | LOW | YES |
| Branch-free stage check | Replace if-chain with function pointer per stage | LOW | DEFER (measure first) |

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 1 - DSP Primitives

**Related features at same layer** (from synth-roadmap.md and existing primitives):
- Phase 1.2: Multi-Stage Envelope (Layer 2 processor that could reuse coefficient calculation)
- LFO (existing Layer 1 primitive -- lifecycle pattern reference only, no code sharing)
- OnePoleSmoother (existing Layer 1 primitive -- one-pole formula reference, different API)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| `calcCoefficients()` (target ratio formula) | MEDIUM | Multi-Stage Envelope (Phase 1.2) | Keep local for now; extract if Phase 1.2 needs identical formula |
| `ADSRStage` enum | LOW | Specific to ADSR; Multi-Stage uses different state model | Keep local |
| `EnvCurve` enum | MEDIUM | Multi-Stage Envelope may reuse same curve types | Keep local; extract to Layer 0 if 2nd consumer appears |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep calcCoefficients private | Only one consumer (ADSREnvelope); Multi-Stage Envelope may use different formula |
| Keep EnvCurve in adsr_envelope.h | No second consumer yet; easy to extract later if needed |
| Use ADSRStage not EnvelopeStage | Avoid confusion with existing EnvelopeStage struct in multistage_env_filter.h |

### Review Trigger

After implementing **Multi-Stage Envelope (Phase 1.2)**, review this section:
- [ ] Does Multi-Stage Envelope need `calcCoefficients` or similar? -> Extract to shared location
- [ ] Does Multi-Stage Envelope use `EnvCurve`? -> Extract to Layer 0 `envelope_types.h`
- [ ] Any duplicated one-pole coefficient code? -> Consider shared utility

## Project Structure

### Documentation (this feature)

```text
specs/032-adsr-envelope-generator/
├── plan.md              # This file
├── research.md          # Phase 0 output - research decisions
├── data-model.md        # Phase 1 output - entity definitions
├── quickstart.md        # Phase 1 output - implementation guide
├── contracts/           # Phase 1 output - API contracts
│   └── adsr_envelope.h  # Public API contract
└── tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── primitives/
│       └── adsr_envelope.h       # NEW: Header-only ADSR envelope implementation
├── tests/
│   └── unit/primitives/
│       └── adsr_envelope_test.cpp # NEW: Catch2 unit tests
├── CMakeLists.txt                 # MODIFY: Add adsr_envelope.h to KRATE_DSP_PRIMITIVES_HEADERS
└── lint_all_headers.cpp           # MODIFY: Add #include <krate/dsp/primitives/adsr_envelope.h>

dsp/tests/CMakeLists.txt           # MODIFY: Add test file to dsp_tests + -fno-fast-math list

specs/_architecture_/
└── layer-1-primitives.md          # MODIFY: Add ADSREnvelope section
```

**Structure Decision**: Standard KrateDSP library structure. Header-only implementation at Layer 1 (primitives). Single test file for all ADSR test cases. No plugin code changes needed (ADSR is a pure DSP primitive).

## Complexity Tracking

No constitution violations. No complexity justifications needed.
