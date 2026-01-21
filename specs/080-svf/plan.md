# Implementation Plan: State Variable Filter (SVF)

**Branch**: `080-svf` | **Date**: 2026-01-21 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `specs/080-svf/spec.md`

## Summary

Implement a TPT (Topology-Preserving Transform) State Variable Filter based on the Cytomic design. The SVF provides simultaneous lowpass, highpass, bandpass, and notch outputs from a single computation with excellent audio-rate modulation stability. This is a Layer 1 DSP primitive that will be used by higher-layer components like `envelope_filter.h` for auto-wah effects.

**Key differentiators from existing Biquad:**
1. Modulation-stable: Trapezoidal integration handles rapid parameter changes without clicks
2. Multi-output: LP/HP/BP/Notch computed simultaneously in one cycle
3. Orthogonal parameters: Cutoff and Q are truly independent

## Technical Context

**Language/Version**: C++20 (constexpr, value semantics)
**Primary Dependencies**: Layer 0 only (`math_constants.h`, `db_utils.h`)
**Storage**: N/A (stateful filter with 2 integrator variables)
**Testing**: Catch2 (dsp_tests target)
**Target Platform**: Windows, macOS, Linux (cross-platform)
**Project Type**: DSP library (header-only)
**Performance Goals**: < 0.1% CPU per instance (Layer 1 budget)
**Constraints**: Real-time safe (noexcept, zero allocations, flush denormals)
**Scale/Scope**: Single filter instance, typical usage 1-8 instances per plugin

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle I (VST3 Architecture):**
- [x] N/A - This is a DSP primitive, not a VST component

**Required Check - Principle II (Real-Time Safety):**
- [x] All processing methods will be `noexcept`
- [x] No memory allocation in processing path
- [x] Denormal flushing after every process() call (FR-019)
- [x] NaN/Inf input handling with state reset (FR-022)

**Required Check - Principle III (Modern C++):**
- [x] RAII: Filter state managed as value members
- [x] constexpr where applicable (coefficient calculation)
- [x] No raw new/delete

**Required Check - Principle IX (Layered Architecture):**
- [x] Layer 1 primitive: depends only on Layer 0
- [x] Header-only implementation (FR-024)
- [x] Namespace: Krate::DSP (FR-025)

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

**Classes/Structs to be created**: SVF, SVFMode, SVFOutputs

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| SVF | `grep -r "class SVF" dsp/ plugins/` | No | Create New |
| SVFMode | `grep -r "SVFMode" dsp/ plugins/` | No | Create New |
| SVFOutputs | `grep -r "SVFOutputs" dsp/ plugins/` | No | Create New |
| StateVariable | `grep -r "StateVariable" dsp/ plugins/` | No | N/A - not creating |

**Utility Functions to be created**: None (all logic encapsulated in SVF class)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| N/A | — | — | — | — |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| kPi | dsp/include/krate/dsp/core/math_constants.h | 0 | Coefficient: g = tan(pi * fc / fs) |
| detail::flushDenormal() | dsp/include/krate/dsp/core/db_utils.h | 0 | Flush state after every process() |
| detail::isNaN() | dsp/include/krate/dsp/core/db_utils.h | 0 | Input validation (FR-022) |
| detail::isInf() | dsp/include/krate/dsp/core/db_utils.h | 0 | Input validation (FR-022) |
| detail::constexprPow10() | dsp/include/krate/dsp/core/db_utils.h | 0 | Gain: A = 10^(dB/40) for shelf/peak |
| kMinFilterFrequency | dsp/include/krate/dsp/primitives/biquad.h | 1 | Reference only (define own constant) |
| kMinQ, kMaxQ | dsp/include/krate/dsp/primitives/biquad.h | 1 | Could reuse, but SVF has own range [0.1, 30] |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities (no conflicts)
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (no SVF exists)
- [x] `specs/_architecture_/layer-1-primitives.md` - No SVF entry (will add after implementation)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (SVF, SVFMode, SVFOutputs) are unique and not found in codebase. The search for "SVF", "StateVariable", "TPT", and "Trapezoidal" returned no matches in the dsp/ directory.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| math_constants.h | kPi | `inline constexpr float kPi = 3.14159265358979323846f;` | Yes |
| db_utils.h | flushDenormal | `[[nodiscard]] inline constexpr float flushDenormal(float x) noexcept` | Yes |
| db_utils.h | isNaN | `constexpr bool isNaN(float x) noexcept` | Yes |
| db_utils.h | isInf | `[[nodiscard]] constexpr bool isInf(float x) noexcept` | Yes |
| db_utils.h | constexprPow10 | `constexpr float constexprPow10(float x) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/math_constants.h` - kPi constant
- [x] `dsp/include/krate/dsp/core/db_utils.h` - flushDenormal, isNaN, isInf, constexprPow10

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| db_utils.h | isNaN/isInf are in `detail::` namespace | `detail::isNaN(x)`, `detail::isInf(x)` |
| db_utils.h | flushDenormal is in `detail::` namespace | `detail::flushDenormal(x)` |
| db_utils.h | constexprPow10 is in `detail::` namespace | `detail::constexprPow10(x)` |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

**N/A** - This is a Layer 1 primitive. All utilities needed already exist in Layer 0.

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| — | — | — | — |

**Decision**: No new Layer 0 utilities needed. SVF uses existing Layer 0 components.

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 1 - DSP Primitives

**Related features at same layer** (from FLT-ROADMAP.md):
- `ladder_filter.h` (Phase 5) - Moog-style ladder filter
- `allpass_1pole.h` (Phase 4) - First-order allpass

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| SVF class | HIGH | envelope_filter.h (Phase 9), future synth | Keep as-is, designed for composition |
| SVFMode enum | MEDIUM | Could be unified with FilterType in future | Keep separate for now |
| SVFOutputs struct | LOW | SVF-specific | Keep in svf.h |

### Detailed Analysis (for HIGH potential items)

**SVF class** provides:
- Audio-rate modulation stability (trapezoidal integration)
- Simultaneous multi-output processing
- Independent cutoff/Q control
- All 8 standard filter modes

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| envelope_filter.h | YES | Primary use case for auto-wah |
| ladder_filter.h | NO | Different topology (cascade vs SVF) |
| allpass_1pole.h | NO | First-order, different purpose |

**Recommendation**: Keep SVF as standalone Layer 1 primitive. Will be composed by Layer 2/3 components.

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Separate SVFMode from FilterType | SVF has different mode mixing (allpass uses -k) |
| SVFOutputs struct | Efficient multi-output return type |
| No shared base class | Biquad and SVF have fundamentally different topologies |

### Review Trigger

After implementing **ladder_filter.h**, review this section:
- [ ] Does ladder need similar multi-output capability? -> Maybe add LadderOutputs
- [ ] Does ladder use similar coefficient pattern? -> Document if so
- [ ] Any duplicated parameter clamping code? -> Consider shared utilities

## Project Structure

### Documentation (this feature)

```text
specs/080-svf/
├── plan.md              # This file
├── research.md          # Phase 0: Cytomic TPT research
├── data-model.md        # Phase 1: Entity definitions
├── quickstart.md        # Phase 1: Implementation guide
├── contracts/           # Phase 1: API contracts
│   └── svf.h            # API contract header
├── checklists/
│   └── requirements.md  # Pre-existing requirements checklist
└── spec.md              # Feature specification
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── primitives/
│       └── svf.h                # Implementation (header-only)
└── tests/
    └── primitives/
        └── svf_test.cpp         # Unit tests
```

**Structure Decision**: Standard Layer 1 primitive structure. Header-only implementation in `primitives/`, tests mirror directory structure.

## Complexity Tracking

> No Constitution violations. Standard Layer 1 primitive implementation.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| — | — | — |
