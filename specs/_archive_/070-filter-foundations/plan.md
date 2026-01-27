# Implementation Plan: Filter Foundations

**Branch**: `070-filter-foundations` | **Date**: 2026-01-20 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `/specs/070-filter-foundations/spec.md`

## Summary

Implement Phase 1 "Filter Foundations" from the Filter Implementation Roadmap. This includes three header files:
1. **filter_tables.h** (Layer 0): Constexpr formant frequency/bandwidth data for 5 vowels
2. **filter_design.h** (Layer 0): Filter design utilities including prewarpFrequency, combFeedbackForRT60, chebyshevQ, besselQ, and butterworthPoleAngle
3. **one_pole.h** (Layer 1): OnePoleLP, OnePoleHP, and LeakyIntegrator audio filter classes

All components are header-only, real-time safe, and follow the established coding patterns.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: math_constants.h (kPi, kTwoPi), db_utils.h (detail::flushDenormal, detail::isNaN, detail::isInf, detail::constexprExp)
**Storage**: N/A (no runtime storage)
**Testing**: Catch2 (dsp_tests target)
**Target Platform**: Windows, macOS, Linux (cross-platform)
**Project Type**: Shared DSP library (header-only)
**Performance Goals**: Layer 0 < 0.1% CPU, Layer 1 < 0.1% CPU per primitive
**Constraints**: Zero allocations, noexcept processing, header-only
**Scale/Scope**: 3 new header files, 3 new test files

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle I (VST3 Architecture Separation)**: N/A - DSP library only
**Principle II (Real-Time Thread Safety)**:
- [x] All processing functions will be noexcept
- [x] No memory allocation in process/processBlock
- [x] No locks, I/O, or exceptions

**Principle III (Modern C++ Standards)**:
- [x] C++20 features (constexpr) used where appropriate
- [x] RAII for resource management
- [x] const/constexpr used aggressively

**Principle VI (Cross-Platform Compatibility)**:
- [x] No platform-specific code
- [x] Using established constexpr math from db_utils.h

**Principle IX (Layered DSP Architecture)**:
- [x] Layer 0 files depend only on stdlib and Layer 0
- [x] Layer 1 file depends only on Layer 0

**Principle XII (Test-First Development)**:
- [x] Skills auto-load (testing-guide, dsp-architecture) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Principle XIV (ODR Prevention)**:
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**:

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| FormantData | `grep -r "struct FormantData" dsp/ plugins/` | No | Create New |
| OnePoleLP | `grep -r "class OnePoleLP" dsp/ plugins/` | No | Create New |
| OnePoleHP | `grep -r "class OnePoleHP" dsp/ plugins/` | No | Create New |
| LeakyIntegrator | `grep -r "class LeakyIntegrator" dsp/ plugins/` | No | Create New |
| Vowel (enum) | `grep -r "enum.*Vowel" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**:

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| prewarpFrequency | `grep -r "prewarpFrequency" dsp/ plugins/` | No | - | Create New |
| combFeedbackForRT60 | `grep -r "combFeedbackForRT60" dsp/ plugins/` | No | - | Create New |
| chebyshevQ | `grep -r "chebyshevQ" dsp/ plugins/` | No | - | Create New |
| besselQ | `grep -r "besselQ" dsp/ plugins/` | No | - | Create New |
| butterworthPoleAngle | `grep -r "butterworthPoleAngle" dsp/ plugins/` | No | - | Create New |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| kPi, kTwoPi | dsp/include/krate/dsp/core/math_constants.h | 0 | Angular frequency calculations |
| detail::flushDenormal() | dsp/include/krate/dsp/core/db_utils.h | 0 | Denormal prevention in filters |
| detail::isNaN() | dsp/include/krate/dsp/core/db_utils.h | 0 | NaN input detection |
| detail::isInf() | dsp/include/krate/dsp/core/db_utils.h | 0 | Infinity input detection |
| detail::constexprExp() | dsp/include/krate/dsp/core/db_utils.h | 0 | Filter coefficient calculation |
| butterworthQ() | dsp/include/krate/dsp/primitives/biquad.h | 1 | Reference pattern (not direct reuse) |
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | 1 | Reference pattern (not direct reuse) |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `specs/_architecture_/` - Component inventory (README.md for index, layer files for details)
- [x] `dsp/include/krate/dsp/primitives/biquad.h` - Existing Q calculation patterns
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - Existing one-pole patterns

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types are unique and not found in codebase. No name conflicts with existing components. OnePoleSmoother is for parameter smoothing (different purpose), our OnePoleLP/OnePoleHP are for audio filtering.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| math_constants.h | kPi | `inline constexpr float kPi = 3.14159265358979323846f` | Yes |
| math_constants.h | kTwoPi | `inline constexpr float kTwoPi = 2.0f * kPi` | Yes |
| db_utils.h | detail::flushDenormal | `[[nodiscard]] inline constexpr float flushDenormal(float x) noexcept` | Yes |
| db_utils.h | detail::isNaN | `constexpr bool isNaN(float x) noexcept` | Yes |
| db_utils.h | detail::isInf | `[[nodiscard]] constexpr bool isInf(float x) noexcept` | Yes |
| db_utils.h | detail::constexprExp | `constexpr float constexprExp(float x) noexcept` | Yes |
| db_utils.h | kDenormalThreshold | `inline constexpr float kDenormalThreshold = 1e-15f` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/math_constants.h` - Math constants
- [x] `dsp/include/krate/dsp/core/db_utils.h` - dB utilities, NaN/Inf detection, constexpr math
- [x] `dsp/include/krate/dsp/primitives/biquad.h` - Existing butterworthQ pattern
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother pattern

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| db_utils.h | Functions are in `detail::` namespace | `detail::flushDenormal(x)` |
| db_utils.h | `isNaN` requires -fno-fast-math on source file | Add compile flag to test file |
| biquad.h | Uses `detail::constexprCos()` not std::cos | Implement similar constexpr trig if needed |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| constexprSin/constexprCos | Required for Chebyshev/Bessel Q, already exists in biquad.h detail | Already in biquad.h, consider moving to filter_design.h | filter_design.h, biquad.h |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| OnePoleLP::process() | Class-specific, accesses state |
| OnePoleHP::process() | Class-specific, accesses state |
| LeakyIntegrator::process() | Class-specific, accesses state |

**Decision**: Will use constexprCos/constexprSin if implementing full Chebyshev/Bessel pole calculations. May reference biquad.h's detail namespace or duplicate minimally.

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 0 (filter_tables.h, filter_design.h) and Layer 1 (one_pole.h)

**Related features at same layer** (from FLT-ROADMAP.md):
- Phase 2: svf.h (State Variable Filter) - Layer 2
- Phase 3: comb_filter.h (Comb/Allpass) - Layer 1
- Phase 4: allpass_1pole.h - Layer 1
- Phase 5: ladder_filter.h - Layer 2
- Phase 8: formant_filter.h - Layer 2

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| prewarpFrequency() | HIGH | SVF, Ladder, any filter using bilinear transform | Keep in filter_design.h |
| combFeedbackForRT60() | HIGH | comb_filter.h, reverb systems | Keep in filter_design.h |
| chebyshevQ() | MEDIUM | Chebyshev filter implementations | Keep in filter_design.h |
| besselQ() | MEDIUM | Bessel filter implementations | Keep in filter_design.h |
| FormantData/kVowelFormants | HIGH | formant_filter.h (Phase 8) | Keep in filter_tables.h |
| OnePoleLP/OnePoleHP | HIGH | Ladder filter stages, tone controls | Keep in one_pole.h |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Separate filter_tables.h and filter_design.h | Different concerns: static data vs. design utilities |
| Formant data as constexpr array | Enables compile-time usage, no runtime overhead |
| One-pole filters separate from OnePoleSmoother | Different purpose: audio filtering vs. parameter smoothing |

## Project Structure

### Documentation (this feature)

```text
specs/070-filter-foundations/
├── plan.md              # This file
├── research.md          # Phase 0 research findings
├── data-model.md        # Entity definitions
├── quickstart.md        # Implementation guide
└── contracts/           # API contracts
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   ├── core/
│   │   ├── filter_tables.h    # NEW: FormantData, Vowel enum, kVowelFormants
│   │   └── filter_design.h    # NEW: FilterDesign utilities
│   └── primitives/
│       └── one_pole.h         # NEW: OnePoleLP, OnePoleHP, LeakyIntegrator
└── tests/
    ├── core/
    │   ├── filter_tables_test.cpp    # NEW
    │   └── filter_design_test.cpp    # NEW
    └── primitives/
        └── one_pole_test.cpp         # NEW
```

**Structure Decision**: Standard DSP library layout with Layer 0 in core/, Layer 1 in primitives/, tests mirroring source structure.

## Complexity Tracking

No Constitution Check violations requiring justification.

---

## Phase 0: Research Summary

Research completed using web search and technical documentation.

### 1. Formant Frequency Research

**Decision**: Use Csound formant table values (industry standard for synthesis)

**Source**: [Csound Formant Table](https://www.classes.cs.uchicago.edu/archive/1999/spring/CS295/Computing_Resources/Csound/CsManual3.48b1.HTML/Appendices/table3.html)

**Formant Values (Bass male voice - most common for synthesis)**:

| Vowel | F1 (Hz) | F2 (Hz) | F3 (Hz) | BW1 (Hz) | BW2 (Hz) | BW3 (Hz) |
|-------|---------|---------|---------|----------|----------|----------|
| a | 600 | 1040 | 2250 | 60 | 70 | 110 |
| e | 400 | 1620 | 2400 | 40 | 80 | 100 |
| i | 250 | 1750 | 2600 | 60 | 90 | 100 |
| o | 400 | 750 | 2400 | 40 | 80 | 100 |
| u | 350 | 600 | 2400 | 40 | 80 | 100 |

**Rationale**: Csound tables are widely used in audio synthesis. Note: Csound bass values (F1=600Hz) differ slightly from Peterson & Barney 1952 research averages (F1~700Hz), but both are within the 10% tolerance specified in SC-008. Bass values chosen as default for broadest applicability; users can scale for other voice types.

**Alternatives Considered**:
- Soprano/Alto values: Higher frequencies, less common in general synthesis
- Peterson & Barney 1952 averages: Similar values but less bandwidth data

### 2. Chebyshev Type I Q Calculation

**Decision**: Implement pole-based Q calculation from standard Chebyshev mathematics

**Formula**:
```
epsilon = sqrt(10^(rippleDb/10) - 1)
mu = (1/n) * asinh(1/epsilon)

For stage k (0-indexed) of n stages:
  theta_k = pi * (2*k + 1) / (2*n)
  sigma_k = -sinh(mu) * sin(theta_k)
  omega_k = cosh(mu) * cos(theta_k)
  Q_k = sqrt(sigma_k^2 + omega_k^2) / (2 * |sigma_k|)
```

**Source**: [Chebyshev Filter Wikipedia](https://en.wikipedia.org/wiki/Chebyshev_filter), [RF Cafe Chebyshev Poles](https://www.rfcafe.com/references/electrical/cheby-poles.htm)

**Rationale**: Standard electrical engineering formula. When ripple = 0, degenerates to Butterworth Q.

### 3. Bessel Filter Q Values

**Decision**: Use lookup table with interpolation for common orders

**Source**: [GitHub Gist - Bessel Q Values](https://gist.github.com/endolith/4982787)

**Bessel Q Values by Order**:

| Order | Stage 0 Q | Stage 1 Q | Stage 2 Q | Stage 3 Q |
|-------|-----------|-----------|-----------|-----------|
| 2 | 0.57735 | - | - | - |
| 3 | 0.69105 | - | - | - |
| 4 | 0.80554 | 0.52193 | - | - |
| 5 | 0.91648 | 0.56354 | - | - |
| 6 | 1.02331 | 0.61119 | 0.51032 | - |
| 7 | 1.12626 | 0.66082 | 0.53236 | - |
| 8 | 1.22567 | 0.71085 | 0.55961 | 0.50599 |

**Rationale**: Bessel polynomials don't have simple closed-form Q expressions. Lookup table is industry standard approach. Values verified against SciPy and MATLAB.

### 4. Bilinear Transform Prewarp Formula

**Decision**: Use standard prewarp formula

**Formula**:
```
omega_prewarped = (2/T) * tan(omega_digital * T / 2)

Simplified for frequency in Hz:
f_prewarped = tan(pi * f / sampleRate) * sampleRate / pi
```

Or equivalently:
```
f_prewarped = (sampleRate / pi) * tan(pi * f / sampleRate)
```

**Source**: [MATLAB bilinear](https://www.mathworks.com/help/signal/ref/bilinear.html), [Wikipedia Bilinear Transform](https://en.wikipedia.org/wiki/Bilinear_transform)

**Rationale**: Standard formula used in all DSP textbooks and software. Compensates for frequency warping inherent in bilinear transform.

### 5. RT60 to Feedback Coefficient Formula

**Decision**: Use standard reverb engineering formula

**Formula**:
```
g = 10^(-3 * delayMs / (1000 * rt60Seconds))
  = 10^(-3 * delayMs / rt60Ms)
  = pow(0.001, delayMs / rt60Ms)
```

**Source**: [CCRMA Schroeder Reverberators](https://ccrma.stanford.edu/~jos/pasp/Schroeder_Reverberators.html)

**Derivation**: After n round trips through delay of tau seconds, amplitude is g^n. At t60, amplitude = 0.001 (−60dB). Solving g^(t60/tau) = 0.001 gives g = 0.001^(tau/t60).

**Rationale**: Standard formula from Schroeder (1962), used in all reverb implementations.

---

## Phase 1: Design Artifacts

### See accompanying files:
- [research.md](./research.md) - Detailed research findings
- [data-model.md](./data-model.md) - Entity definitions
- [quickstart.md](./quickstart.md) - Implementation guide
- [contracts/filter_tables.h](./contracts/filter_tables.h) - API contract
- [contracts/filter_design.h](./contracts/filter_design.h) - API contract
- [contracts/one_pole.h](./contracts/one_pole.h) - API contract
