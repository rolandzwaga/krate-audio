# Implementation Plan: Rungler / Shift Register Oscillator

**Branch**: `029-rungler-oscillator` | **Date**: 2026-02-06 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/029-rungler-oscillator/spec.md`

## Summary

Implement a Benjolin-inspired Rungler / Shift Register Oscillator as a Layer 2 DSP processor. The component contains two cross-modulating triangle oscillators and an 8-bit shift register with XOR feedback, producing chaotic stepped sequences via a 3-bit DAC. It is entirely header-only at `dsp/include/krate/dsp/processors/rungler.h`, depends on `Xorshift32` (Layer 0), `OnePoleLP` (Layer 1), and `detail::isNaN/isInf` (Layer 0). Five simultaneous outputs: osc1 triangle, osc2 triangle, rungler CV, PWM comparator, and mixed.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: KrateDSP library (Xorshift32, OnePoleLP, detail::isNaN/isInf)
**Storage**: N/A (stateless between process calls; all state in member variables)
**Testing**: Catch2 via `dsp_tests` executable (Constitution Principle XIII: Test-First Development)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- cross-platform
**Project Type**: Header-only DSP component in monorepo shared library
**Performance Goals**: < 0.5% CPU per instance at 44.1 kHz (Layer 2 budget per SC-006)
**Constraints**: Real-time safe (no allocations, no locks, no exceptions, no I/O in process path). noexcept on all setters and processing methods.
**Scale/Scope**: Single header file (~300-400 lines), one test file (~500-700 lines)

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle I (VST3 Architecture Separation)**: N/A -- this is a DSP library component, not a plugin component.

**Principle II (Real-Time Audio Thread Safety)**:
- [x] All processing methods are `noexcept`
- [x] No allocations in process path (all state pre-allocated)
- [x] No locks, no exceptions, no I/O
- [x] OnePoleLP filter is pre-prepared in `prepare()`

**Principle III (Modern C++ Standards)**:
- [x] C++20 targeted
- [x] No raw new/delete
- [x] constexpr where possible, [[nodiscard]] on getters and process()
- [x] Value semantics for Output struct

**Principle IV (SIMD & DSP Optimization)**:
- [x] No SIMD needed -- lightweight arithmetic only
- [x] Sequential memory access patterns
- [x] No virtual functions in processing loop

**Principle VI (Cross-Platform Compatibility)**:
- [x] No platform-specific code
- [x] Uses bit-manipulation NaN check (works under -ffast-math)
- [x] Uses std::pow (available on all platforms)

**Principle VIII (Testing Discipline)**:
- [x] DSP algorithm testable without VST infrastructure
- [x] Tests cover all functional and success criteria requirements

**Principle IX (Layered DSP Architecture)**:
- [x] Layer 2 processor at `dsp/include/krate/dsp/processors/`
- [x] Dependencies: Layer 0 (random.h, db_utils.h), Layer 1 (one_pole.h)
- [x] No circular dependencies

**Principle XV (ODR Prevention)**:
- [x] Searched for Rungler, ShiftRegister, RunglerMode -- no conflicts
- [x] Output struct scoped within Rungler class

**Required Check - Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

---

**POST-DESIGN RE-CHECK**: All constitution checks pass. No violations detected. The design is a clean Layer 2 processor with no platform-specific code, no real-time violations, and no ODR risks.

## Codebase Research (Principle XV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: Rungler, Rungler::Output

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| Rungler | `grep -r "class Rungler" dsp/ plugins/` | No | Create New |
| Rungler::Output | `grep -r "struct Output" dsp/include/krate/dsp/processors/` | No (at class scope) | Create New (scoped in Rungler class) |

**Utility Functions to be created**: None -- all utilities already exist in the codebase.

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| Xorshift32 | `dsp/include/krate/dsp/core/random.h` | 0 | Shift register seeding on prepare()/reset() |
| OnePoleLP | `dsp/include/krate/dsp/primitives/one_pole.h` | 1 | CV smoothing filter for Rungler output |
| detail::isNaN | `dsp/include/krate/dsp/core/db_utils.h` | 0 | NaN sanitization on frequency setters |
| detail::isInf | `dsp/include/krate/dsp/core/db_utils.h` | 0 | Infinity sanitization on frequency setters |
| detail::flushDenormal | `dsp/include/krate/dsp/core/db_utils.h` | 0 | Denormal flushing in oscillator output |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (checked for Output struct conflicts)
- [x] `specs/_architecture_/` - Component inventory (README.md for index, layer files for details)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (Rungler, Rungler::Output) are unique and not found anywhere in the codebase. The Output struct is scoped within the Rungler class, preventing namespace-level conflicts. No existing classes need modification.

## Dependency API Contracts (Principle XV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| Xorshift32 | constructor | `explicit constexpr Xorshift32(uint32_t seedValue = 1) noexcept` | Yes |
| Xorshift32 | next | `[[nodiscard]] constexpr uint32_t next() noexcept` | Yes |
| Xorshift32 | seed | `constexpr void seed(uint32_t seedValue) noexcept` | Yes |
| OnePoleLP | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| OnePoleLP | setCutoff | `void setCutoff(float hz) noexcept` | Yes |
| OnePoleLP | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| OnePoleLP | reset | `void reset() noexcept` | Yes |
| detail | isNaN | `constexpr bool isNaN(float x) noexcept` | Yes |
| detail | isInf | `[[nodiscard]] constexpr bool isInf(float x) noexcept` | Yes |
| detail | flushDenormal | `[[nodiscard]] inline constexpr float flushDenormal(float x) noexcept` | Yes |

### Public API Methods (from contract)

| Method | Signature | Notes |
|--------|-----------|-------|
| prepare | `void prepare(double sampleRate) noexcept` | Initializes state, seeds register |
| reset | `void reset() noexcept` | Re-initializes state, preserves params |
| seed | `void seed(uint32_t seedValue) noexcept` | Sets PRNG seed for deterministic output (FR-020) |
| process | `[[nodiscard]] Output process() noexcept` | Single-sample processing |
| processBlock | `void processBlock(Output* output, size_t numSamples) noexcept` | Block processing |
| processBlockMixed | `void processBlockMixed(float* output, size_t numSamples) noexcept` | Mixed-only block |
| processBlockRungler | `void processBlockRungler(float* output, size_t numSamples) noexcept` | CV-only block |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/random.h` - Xorshift32 class (all methods verified)
- [x] `dsp/include/krate/dsp/primitives/one_pole.h` - OnePoleLP class (prepare, setCutoff, process, reset verified)
- [x] `dsp/include/krate/dsp/core/db_utils.h` - detail::isNaN, detail::isInf, detail::flushDenormal verified
- [x] `dsp/include/krate/dsp/core/math_constants.h` - kPi, kTwoPi verified (not needed for Rungler)

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| OnePoleLP | `prepare()` takes `double` not `float` | `cvFilter_.prepare(static_cast<double>(sampleRate_))` or pass double directly |
| OnePoleLP | Unprepared filter returns input unchanged | Must call `cvFilter_.prepare()` in Rungler's `prepare()` |
| Xorshift32 | Seed of 0 is replaced with default seed | This is correct behavior -- ensures non-zero output |
| Xorshift32 | `next()` returns [1, 2^32-1], never 0 | Good -- guarantees non-zero shift register seed |
| detail::isNaN | Must compile test file with `-fno-fast-math` | Add to CMakeLists.txt -fno-fast-math list |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

No candidates. The Rungler uses only existing Layer 0/1 utilities. The triangle oscillator is too specific (bipolar ramp with direction reversal, needs pulse derivation) to warrant extraction. The shift register logic is Rungler-specific.

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| Triangle phase accumulation | 5 lines, specific to bipolar triangle-core with direction tracking |
| Shift register clock/data | Rungler-specific XOR feedback logic |
| 3-bit DAC computation | 1-line formula, specific to Rungler |
| Frequency modulation formula | Specific to exponential cross-modulation pattern |

**Decision**: No extractions to Layer 0. All internal functions are Rungler-specific and too small/specialized to warrant shared utility status.

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer** (from existing processors):
- ChaosOscillator (spec 026): Chaos attractor oscillator with RK4 integration
- ParticleOscillator (spec 028): Particle swarm oscillator
- FormantOscillator (spec 027): Formant synthesis oscillator
- Future: Potential other alternative oscillator types

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Internal triangle oscillator | LOW | Unlikely -- PolyBlepOscillator serves most needs | Keep local |
| Shift register with XOR feedback | LOW | Specific to Rungler topology | Keep local |
| Exponential frequency modulation formula | MEDIUM | Could be useful for other cross-modulating systems | Keep local, extract if 2nd use appears |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared base class for oscillator processors | Each oscillator processor has fundamentally different internals (RK4 vs phase accumulation vs particle swarm). Shared base class would be empty or force artificial commonality. |
| Keep triangle oscillator internal | Naive triangle (no anti-aliasing) is intentionally non-standard. PolyBlepOscillator serves the general use case. |
| Keep exponential modulation formula internal | Only one consumer so far. If a second cross-modulating system appears, extract to Layer 0. |

### Review Trigger

After implementing **next oscillator processor**, review this section:
- [ ] Does the next oscillator need a bipolar triangle primitive? If so, extract from Rungler.
- [ ] Does any future feature need exponential cross-modulation? If so, extract formula.

## Project Structure

### Documentation (this feature)

```text
specs/029-rungler-oscillator/
├── plan.md              # This file
├── spec.md              # Feature specification
├── research.md          # Phase 0: research decisions
├── data-model.md        # Phase 1: entity model
├── quickstart.md        # Phase 1: usage guide
├── contracts/           # Phase 1: API contract
│   └── rungler-api.h    # Public API definition
└── tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── processors/
│       └── rungler.h              # NEW: Header-only Rungler implementation
└── tests/
    └── unit/processors/
        └── rungler_test.cpp       # NEW: Catch2 tests
```

### Files Modified

```text
dsp/tests/CMakeLists.txt           # Add rungler_test.cpp to test list
specs/_architecture_/layer-2-processors.md  # Add Rungler entry
```

**Structure Decision**: Standard KrateDSP header-only pattern. Single header in `processors/` (Layer 2), single test file in `tests/unit/processors/`. No new directories needed.

## Complexity Tracking

No constitution violations. No complexity tracking needed.
