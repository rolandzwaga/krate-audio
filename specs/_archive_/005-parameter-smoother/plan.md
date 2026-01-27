# Implementation Plan: Parameter Smoother

**Branch**: `005-parameter-smoother` | **Date**: 2025-12-22 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/005-parameter-smoother/spec.md`

## Summary

This feature implements a Layer 1 DSP Primitive for real-time safe parameter interpolation. Three smoother types are provided:

1. **OnePoleSmoother**: Exponential approach for most parameters (gain, filter cutoff, mix)
2. **LinearRamp**: Constant rate change for delay time (tape-like pitch effects)
3. **SlewLimiter**: Maximum rate limiting with separate rise/fall rates

All smoothers are designed for audio thread safety (zero allocation, no blocking), sample-accurate transitions, and constexpr coefficient calculation where possible.

## Technical Context

**Language/Version**: C++20 (MSVC 2019+, Clang 12+, GCC 10+)
**Primary Dependencies**: Standard library only (Layer 0 primitive - no external DSP dependencies)
**Storage**: N/A (stateless coefficients, minimal per-instance state)
**Testing**: Catch2 v3.5.0 (per Constitution Principle VIII and XII)
**Target Platform**: Windows x64/ARM64, macOS x64/ARM64, Linux x64
**Project Type**: Single project - VST3 plugin
**Performance Goals**: < 10ns per sample processing at 3GHz (Constitution Principle XI: < 0.1% CPU per Layer 1 instance)
**Constraints**: Zero memory allocation, sample-accurate, constexpr where possible
**Scale/Scope**: 3 smoother types, ~400-600 lines of header-only code, ~40-60 test cases

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

### Pre-Design Check (All Pass)

| Principle | Status | Notes |
|-----------|--------|-------|
| II. Real-Time Safety | PASS | Zero allocation, no blocking, no exceptions in processing |
| III. Modern C++ | PASS | C++20, constexpr, RAII, no raw new/delete |
| IV. SIMD & DSP | PASS | Contiguous memory, no branching in tight loops |
| VIII. Testing | PASS | Unit tests for all smoother types, Catch2 framework |
| IX. Layered Architecture | PASS | Layer 1 primitive, depends only on Layer 0 (math utils) |
| X. DSP Constraints | PASS | Sample-accurate transitions, proper coefficient calculation |
| XI. Performance Budgets | PASS | Target < 0.1% CPU per instance |
| XII. Test-First | PASS | Tests written before implementation |
| XIII. Architecture Docs | PASS | ARCHITECTURE.md update included as final task |

**Required Check - Principle XII (Test-First Development):**
- [x] Tasks will include TESTING-GUIDE.md context verification step
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

## Project Structure

### Documentation (this feature)

```text
specs/005-parameter-smoother/
├── spec.md              # Feature specification (complete)
├── plan.md              # This file
├── research.md          # Phase 0 output (smoothing algorithms)
├── data-model.md        # Phase 1 output (class structures)
├── quickstart.md        # Phase 1 output (usage examples)
├── contracts/           # Phase 1 output (API contracts)
│   └── smoother.h       # Public API header contract
├── checklists/
│   └── requirements.md  # Spec quality checklist (complete)
└── tasks.md             # Phase 2 output (/speckit.tasks command)
```

### Source Code (repository root)

```text
src/
└── dsp/
    └── primitives/
        └── smoother.h       # Implementation (Layer 1 primitive)

tests/
└── unit/
    └── primitives/
        └── smoother_test.cpp  # Unit tests
```

**Structure Decision**: Single header file in `src/dsp/primitives/` following the pattern established by `delay_line.h`, `lfo.h`, and `biquad.h`. Header-only for inline optimization.

## Complexity Tracking

> No violations requiring justification. Design follows established Layer 1 primitive patterns.

---

## Phase 0 Complete: See [research.md](research.md)
## Phase 1 Complete: See [data-model.md](data-model.md), [contracts/](contracts/), [quickstart.md](quickstart.md)
