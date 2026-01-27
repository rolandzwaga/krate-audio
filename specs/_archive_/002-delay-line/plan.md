# Implementation Plan: Delay Line DSP Primitive

**Branch**: `002-delay-line` | **Date**: 2025-12-22 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/002-delay-line/spec.md`

## Summary

Implement a real-time safe circular buffer delay line with fractional sample interpolation as a Layer 1 DSP Primitive. The delay line supports integer delay (no interpolation), linear interpolation for modulated delays, and allpass interpolation for feedback loops. Maximum delay time of 10 seconds at 192kHz. All memory is pre-allocated in `prepare()` to satisfy real-time constraints.

## Technical Context

**Language/Version**: C++20 (constexpr, std::bit_cast, std::span)
**Primary Dependencies**: Standard library only (Layer 0 core utilities for fast math if needed)
**Storage**: N/A (in-memory circular buffer)
**Testing**: Catch2 v3 (unit tests), ApprovalTests.cpp (regression) *(Constitution Principle XII: Test-First Development)*
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: Single VST3 plugin with layered DSP architecture
**Performance Goals**: < 0.1% CPU per instance (Layer 1 budget), < 100ns per sample
**Constraints**: O(1) read/write, zero allocations in process, noexcept guarantee
**Scale/Scope**: Core building block used by all delay-based effects (Layer 2-4)

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Requirement | Status |
|-----------|-------------|--------|
| II. Real-Time Audio Thread Safety | No allocations in read/write, noexcept, pre-allocate in prepare() | PASS |
| III. Modern C++ Standards | C++20, RAII, constexpr where possible, std::array/std::span | PASS |
| IV. SIMD & DSP Optimization | Contiguous memory access, minimal branching in inner loops | PASS |
| VIII. Testing Discipline | Unit tests for all DSP functions, regression tests for output | PASS |
| IX. Layered DSP Architecture | Layer 1 - depends only on Layer 0 (standard library) | PASS |
| X. DSP Processing Constraints | Linear interpolation for modulated, allpass for fixed feedback only | PASS |
| XI. Performance Budgets | < 0.1% CPU per instance | PASS |
| XII. Test-First Development | Tests written before implementation, TESTING-GUIDE.md verified | PASS |

**Required Check - Principle XII (Test-First Development):**
- [x] Tasks will include TESTING-GUIDE.md context verification step
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

## Project Structure

### Documentation (this feature)

```text
specs/002-delay-line/
├── spec.md              # Feature specification
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output (API contracts)
│   └── delay_line.h     # C++ header contract
├── checklists/
│   └── requirements.md  # Quality checklist
└── tasks.md             # Phase 2 output (from /speckit.tasks)
```

### Source Code (repository root)

```text
src/
├── dsp/
│   ├── core/                    # Layer 0: Core utilities
│   │   └── db_utils.h           # Existing: dB conversion
│   ├── primitives/              # Layer 1: DSP primitives
│   │   └── delay_line.h         # NEW: This feature
│   ├── processors/              # Layer 2: DSP processors (future)
│   ├── systems/                 # Layer 3: System components (future)
│   └── features/                # Layer 4: User features (future)
└── ...

tests/
├── unit/
│   ├── core/
│   │   └── db_utils_test.cpp    # Existing
│   └── primitives/
│       └── delay_line_test.cpp  # NEW: This feature
└── regression/
    └── approved/                # Approval test baselines
```

**Structure Decision**: Layer 1 DSP Primitive in `src/dsp/primitives/` with unit tests in `tests/unit/primitives/`. Header-only implementation for inline optimization. No Layer 0 dependencies beyond standard library.

## Complexity Tracking

No constitution violations. Design is straightforward:
- Single header file (`delay_line.h`)
- Single test file (`delay_line_test.cpp`)
- No external dependencies
- Well-established algorithms (circular buffer, linear/allpass interpolation)

## Implementation Approach

### Phase 0: Research (Minimal)

No deep research needed - algorithms are well-established:
- Circular buffer: Standard ring buffer pattern
- Linear interpolation: `y = y0 + frac * (y1 - y0)`
- Allpass interpolation: `y[n] = x[n-D] + a * (y[n-1] - x[n-D+1])` where `a = (1-frac)/(1+frac)`

### Phase 1: Design

1. **API Design**: Single `DelayLine` class with explicit read methods for each interpolation type
2. **Memory Layout**: `std::vector<float>` resized in `prepare()`, never during processing
3. **Write Index**: Single integer tracking current write position, wraps using bitwise AND with power-of-2 mask

### Key Design Decisions

| Decision | Choice | Rationale |
|----------|--------|-----------|
| Interpolation API | Separate methods (`read()`, `readLinear()`, `readAllpass()`) | Clearer intent, no runtime branching |
| Buffer storage | `std::vector<float>` | Resizable in prepare(), contiguous memory |
| Index wraparound | Bitwise AND with power-of-2 mask | O(1), no branching, faster than modulo |
| Allpass state | Single float member | Minimal overhead, correct for first-order allpass |
