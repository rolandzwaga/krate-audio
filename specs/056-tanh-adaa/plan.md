# Implementation Plan: Tanh with ADAA

**Branch**: `056-tanh-adaa` | **Date**: 2026-01-13 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `specs/056-tanh-adaa/spec.md`

## Summary

Implement a first-order ADAA (Antiderivative Anti-Aliasing) primitive for tanh saturation. This Layer 1 component provides CPU-efficient aliasing reduction for tanh waveshaping without the overhead of oversampling. The implementation follows the established HardClipADAA pattern, using `F1(x) = ln(cosh(x))` as the antiderivative with asymptotic approximation for `|x| >= 20.0` to prevent overflow.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: `FastMath::fastTanh()`, `detail::isNaN()`, `detail::isInf()` from Layer 0
**Storage**: N/A (stateful primitive with single float state)
**Testing**: Catch2 with `spectral_analysis.h` for aliasing measurement
**Target Platform**: Windows, macOS, Linux (cross-platform)
**Project Type**: Header-only DSP primitive in `dsp/include/krate/dsp/primitives/`
**Performance Goals**: <= 10x naive tanh (`Sigmoid::tanh`) cost per sample (SC-008)
**Constraints**: Real-time safe (noexcept, no allocations), Layer 1 (only Layer 0 dependencies)
**Scale/Scope**: Single class, ~200 LOC header, ~400 LOC tests

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Pre-Design Check:**

| Principle | Status | Notes |
|-----------|--------|-------|
| II (Real-Time Safety) | PASS | All methods will be noexcept, no allocations |
| III (Modern C++) | PASS | C++20, header-only, constexpr where possible |
| VI (Cross-Platform) | PASS | Pure math, no platform-specific code |
| IX (Layered Architecture) | PASS | Layer 1, depends only on Layer 0 |
| X (DSP Constraints) | PASS | No internal oversampling, no DC blocking (tanh symmetric) |
| XI (Performance Budget) | PASS | Target <= 10x naive, estimated ~6-8x based on HardClipADAA |
| XII (Test-First) | PENDING | Tests will be written before implementation |
| XIV (ODR Prevention) | PASS | Codebase Research complete, no conflicts |

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, dsp-architecture) - no manual context verification needed
- [ ] Tests will be written BEFORE implementation code
- [ ] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: `TanhADAA`

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| TanhADAA | `grep -r "class TanhADAA\|struct TanhADAA" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: `F1(float x, float drive)` (static method)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| F1 (tanh antiderivative) | `grep -r "ln.*cosh\|antiderivative.*tanh" dsp/` | No | N/A | Create New |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| FastMath::fastTanh() | dsp/include/krate/dsp/core/fast_math.h | 0 | Fallback for epsilon case (FR-013) |
| detail::isNaN() | dsp/include/krate/dsp/core/db_utils.h | 0 | NaN propagation (FR-019) |
| detail::isInf() | dsp/include/krate/dsp/core/db_utils.h | 0 | Infinity handling (FR-020) |
| Sigmoid::hardClip() | dsp/include/krate/dsp/core/sigmoid.h | 0 | Reference pattern only |
| HardClipADAA | dsp/include/krate/dsp/primitives/hard_clip_adaa.h | 1 | Reference implementation pattern |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `ARCHITECTURE.md` - Component inventory (no TanhADAA listed)
- [x] `dsp/include/krate/dsp/core/fast_math.h` - Contains fastTanh
- [x] `dsp/include/krate/dsp/core/sigmoid.h` - Contains Sigmoid::tanh wrapper

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: TanhADAA is a unique new type. The name is distinct from existing classes. The F1 function is a static method within the class, not a free function, preventing namespace conflicts.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| FastMath | fastTanh | `[[nodiscard]] constexpr float fastTanh(float x) noexcept` | YES |
| detail | isNaN | `constexpr bool isNaN(float x) noexcept` | YES |
| detail | isInf | `[[nodiscard]] constexpr bool isInf(float x) noexcept` | YES |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/fast_math.h` - FastMath::fastTanh
- [x] `dsp/include/krate/dsp/core/db_utils.h` - detail::isNaN, detail::isInf
- [x] `dsp/include/krate/dsp/primitives/hard_clip_adaa.h` - Reference implementation pattern

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| FastMath | Function is `fastTanh` not `tanh` | `FastMath::fastTanh(x)` |
| detail | Namespace is `Krate::DSP::detail` not `Krate::DSP::FastMath::detail` | `detail::isNaN(x)` |
| HardClipADAA | Uses `hasPreviousSample_` flag | Same pattern for TanhADAA |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0.*

**N/A** - This is a Layer 1 primitive. No new Layer 0 utilities identified.

The F1 function `ln(cosh(x))` is specific to tanh ADAA and not generalizable. If future ADAA implementations need similar patterns, extraction could be considered, but following YAGNI, we keep it as a static method in TanhADAA.

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 1 - DSP Primitives

**Related features at same layer** (from ARCHITECTURE.md):
- `primitives/waveshaper.h` - Unified waveshaping with type selection
- `primitives/hard_clip_adaa.h` - Hard clip ADAA (reference implementation)
- `primitives/dc_blocker.h` - DC blocking filter

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| ADAA epsilon threshold (1e-5f) | MEDIUM | Future ADAA primitives | Keep local (matches HardClipADAA) |
| First-sample fallback pattern | HIGH | Any stateful ADAA | Keep local (2 instances insufficient for extraction) |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared ADAA base class | Only 2 ADAA primitives (HardClipADAA, TanhADAA), patterns differ |
| Keep F1 as static method | Specific to tanh, not generalizable |
| Match HardClipADAA structure | Proven pattern, user familiarity |

## Project Structure

### Documentation (this feature)

```text
specs/056-tanh-adaa/
├── plan.md              # This file
├── research.md          # Phase 0 output (ADAA math research)
├── data-model.md        # Phase 1 output (class design)
├── quickstart.md        # Phase 1 output (usage examples)
├── contracts/           # Phase 1 output (API contracts)
│   └── tanh_adaa.yaml   # OpenAPI-style interface spec
└── tasks.md             # Phase 2 output (implementation tasks)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   ├── core/                          # Layer 0 (dependencies)
│   │   ├── fast_math.h                # fastTanh (REUSE)
│   │   └── db_utils.h                 # isNaN, isInf (REUSE)
│   └── primitives/                    # Layer 1 (target)
│       ├── hard_clip_adaa.h           # Reference implementation
│       └── tanh_adaa.h                # NEW: TanhADAA class
└── tests/
    └── unit/primitives/
        └── tanh_adaa_test.cpp         # NEW: Unit tests

tests/test_helpers/
└── spectral_analysis.h                # REUSE: Aliasing measurement
```

**Structure Decision**: Header-only Layer 1 primitive following established pattern from `hard_clip_adaa.h`.

## Complexity Tracking

> No Constitution violations to justify.

---

## Phase 0 Outputs

See [research.md](research.md) for:
- ADAA mathematical derivation for tanh
- Antiderivative F1(x) = ln(cosh(x)) formula
- Asymptotic approximation for overflow prevention
- Performance considerations

## Phase 1 Outputs

See:
- [data-model.md](data-model.md) - TanhADAA class design
- [contracts/tanh_adaa.yaml](contracts/tanh_adaa.yaml) - API contract
- [quickstart.md](quickstart.md) - Usage examples

---

## Post-Design Constitution Re-Check

*Completed after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| II (Real-Time Safety) | PASS | All methods marked noexcept, no allocations in design |
| III (Modern C++) | PASS | C++20, header-only, [[nodiscard]] on getters |
| VI (Cross-Platform) | PASS | Pure math only (std::log, std::cosh, std::abs) |
| IX (Layered Architecture) | PASS | Layer 1, depends only on Layer 0 (fast_math.h, db_utils.h) |
| X (DSP Constraints) | PASS | No internal oversampling, no DC blocking |
| XI (Performance Budget) | PASS | Design targets <= 10x naive, estimated 6-8x |
| XII (Test-First) | READY | Test plan in data-model.md, tests before implementation |
| XIV (ODR Prevention) | PASS | TanhADAA unique, no conflicts found |

**Agent Context Note**: This project uses a manually-maintained CLAUDE.md with comprehensive DSP/VST3 guidelines. The standard agent context update script should NOT be run as it would corrupt the existing content. The CLAUDE.md already includes all necessary context for this feature (Layer 1 primitives, real-time safety, testing patterns).

---

## Implementation Readiness

**Status**: Ready for Phase 2 (task generation) and implementation.

**Artifacts Generated**:
- `plan.md` - This implementation plan
- `research.md` - ADAA mathematical foundation
- `data-model.md` - TanhADAA class design with full API
- `contracts/tanh_adaa.yaml` - Machine-readable API contract
- `quickstart.md` - Usage examples and documentation

**Next Steps**:
1. Generate `tasks.md` using speckit tasks workflow
2. Write failing tests (test-first per Constitution XII)
3. Implement TanhADAA class
4. Verify all tests pass
5. Update ARCHITECTURE.md with new component
