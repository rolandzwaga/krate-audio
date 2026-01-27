# Implementation Plan: DC Blocker Primitive

**Branch**: `051-dc-blocker` | **Date**: 2026-01-12 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/051-dc-blocker/spec.md`

## Summary

This feature implements a Layer 1 DSP Primitive for DC offset removal. The DCBlocker is a lightweight first-order highpass filter specifically optimized for removing DC offset from audio signals.

**Use Cases:**
1. Remove DC offset after asymmetric saturation/waveshaping
2. Prevent DC accumulation in feedback loops (quantization errors, IIR round-off)
3. General signal conditioning before further processing

**Key Design Decisions:**
- Exponential formula for pole coefficient: `R = exp(-2*pi*cutoffHz / sampleRate)`
- `prepared_` flag for safe unprepared state (returns input unchanged)
- Denormal flushing on `y1_` state variable after each sample
- Performance verification via static analysis (operation count in test comments)

## Technical Context

**Language/Version**: C++20 (MSVC 2019+, Clang 12+, GCC 10+)
**Primary Dependencies**: Layer 0 only (`<krate/dsp/core/db_utils.h>` for `detail::flushDenormal()`, `detail::isNaN()`)
**Storage**: N/A (minimal per-instance state: R_, x1_, y1_, prepared_, sampleRate_)
**Testing**: Catch2 v3.5.0 (per Constitution Principle VIII and XII)
**Target Platform**: Windows x64/ARM64, macOS x64/ARM64, Linux x64
**Project Type**: Monorepo - KrateDSP shared library
**Performance Goals**: < 0.1% CPU per instance (Constitution Principle XI for Layer 1)
**Constraints**: Zero memory allocation, noexcept processing, header-only
**Scale/Scope**: Single class, ~150-200 lines of header-only code, ~30-40 test cases

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

### Pre-Design Check (All Pass)

| Principle | Status | Notes |
|-----------|--------|-------|
| II. Real-Time Safety | PASS | Zero allocation, no blocking, no exceptions in processing |
| III. Modern C++ | PASS | C++20, constexpr, noexcept, no raw new/delete |
| IV. SIMD & DSP | PASS | Simple scalar operations, no branching in core loop |
| VIII. Testing | PASS | Unit tests for all public methods, Catch2 framework |
| IX. Layered Architecture | PASS | Layer 1 primitive, depends only on Layer 0 |
| X. DSP Constraints | PASS | DC blocking after saturation per constitution |
| XI. Performance Budgets | PASS | Target < 0.1% CPU per instance |
| XII. Test-First | PASS | Tests written before implementation |
| XIV. Architecture Docs | PASS | ARCHITECTURE.md update included as final task |
| XV. ODR Prevention | PASS | Replaces inline DCBlocker in feedback_network.h |

**Required Check - Principle XII (Test-First Development):**
- [x] Tasks will include testing-guide context verification step
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

### ODR Analysis

**Existing `DCBlocker` in codebase:**
- Location: `dsp/include/krate/dsp/systems/feedback_network.h` (lines 51-76)
- Type: Inline class within FeedbackNetwork header
- Usage: Used only by FeedbackNetwork class internally

**Migration Strategy:**
1. Create new `DCBlocker` in `dsp/include/krate/dsp/primitives/dc_blocker.h`
2. Update FeedbackNetwork to include the new primitive
3. Remove inline DCBlocker class from feedback_network.h
4. This eliminates ODR risk since there will be only one DCBlocker definition

## Project Structure

### Documentation (this feature)

```text
specs/051-dc-blocker/
├── spec.md              # Feature specification (complete)
├── plan.md              # This file
├── research.md          # Phase 0 output (algorithm research)
├── data-model.md        # Phase 1 output (class structure)
├── quickstart.md        # Phase 1 output (usage examples)
├── contracts/           # Phase 1 output (API contracts)
│   └── dc_blocker.h     # Public API header contract
├── checklists/
│   └── requirements.md  # Spec quality checklist
└── tasks.md             # Phase 2 output (/speckit.tasks command)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── primitives/
│       └── dc_blocker.h     # Implementation (Layer 1 primitive)
└── tests/
    └── unit/
        └── primitives/
            └── dc_blocker_test.cpp  # Unit tests
```

**Structure Decision**: Single header file in `dsp/include/krate/dsp/primitives/` following the pattern established by `smoother.h`, `delay_line.h`, `lfo.h`, and `biquad.h`. Header-only for inline optimization.

## Complexity Tracking

### Constitution Deviation: None

Design follows established Layer 1 primitive patterns. No deviations required.

### Performance Budget Analysis

DCBlocker operations per sample:
- 1 multiply (x - x1_)
- 1 multiply (R_ * y1_)
- 2 adds (x - x1_ + R_ * y1_)
- 2 assignments (x1_ = x, y1_ = y)
- 1 denormal flush (flushDenormal)

Total: ~3 multiplies + 2 adds + denormal check
Compare to Biquad: 5 multiplies + 4 adds + 2 state updates

**Verified**: DCBlocker is lighter weight than Biquad for DC blocking use case.

---

## Phase 0 Complete: See [research.md](research.md)
## Phase 1 Complete: See [data-model.md](data-model.md), [contracts/](contracts/), [quickstart.md](quickstart.md)
