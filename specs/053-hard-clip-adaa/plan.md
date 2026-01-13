# Implementation Plan: Hard Clip with ADAA

**Branch**: `053-hard-clip-adaa` | **Date**: 2026-01-13 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/053-hard-clip-adaa/spec.md`

## Summary

Implement a Layer 1 DSP primitive providing Anti-Derivative Anti-Aliasing (ADAA) for hard clipping. ADAA reduces aliasing artifacts by 12-30 dB compared to naive hard clipping, with ~6-15x CPU overhead (well under the 10x budget for first-order). The implementation reuses `Sigmoid::hardClip()` for fallback and follows established Layer 1 patterns from DCBlocker and Waveshaper.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**:
- Layer 0: `core/sigmoid.h` (Sigmoid::hardClip for fallback)
- Layer 0: `core/db_utils.h` (detail::isNaN, detail::isInf)
- stdlib: `<cmath>`, `<algorithm>`, `<cstddef>`, `<cstdint>`

**Storage**: N/A (stateless except for previous sample history)
**Testing**: Catch2 via dsp_tests (skills auto-load: testing-guide)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) - cross-platform
**Project Type**: Single library (dsp/)
**Performance Goals**: <= 10x naive hard clip cost per sample (SC-009)
**Constraints**: Real-time safe (noexcept, no allocations), header-only
**Scale/Scope**: Single class with ~150-200 lines

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle II - Real-Time Safety:**
- [x] All processing methods will be `noexcept`
- [x] No memory allocation in process()
- [x] No locks, I/O, or exceptions in audio path

**Principle III - Modern C++:**
- [x] C++20 target
- [x] `constexpr` where possible (static F1/F2 functions)
- [x] RAII pattern (trivially copyable, no resources)

**Principle IX - Layered DSP Architecture:**
- [x] Layer 1 primitive depending only on Layer 0 and stdlib
- [x] No circular dependencies

**Principle X - DSP Processing Constraints:**
- [x] No internal oversampling (ADAA is an alternative)
- [x] No internal DC blocking (hard clip is symmetric)

**Principle XI - Performance Budget:**
- [x] Target < 10x naive hard clip per sample
- [x] First-order ADAA estimated at 6-8x (passes)
- [x] Second-order ADAA estimated at 12-15x (may exceed, documented)

**Principle XIV - ODR Prevention:**
- [x] Codebase search performed - no existing HardClipADAA
- [x] No duplicate classes/functions will be created

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: HardClipADAA, Order (enum class)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| HardClipADAA | `grep -r "class HardClipADAA" dsp/ plugins/` | No | Create New |
| Order (in HardClipADAA) | N/A (nested enum) | N/A | Create New |

**Utility Functions to be created**: F1, F2 (static members)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| F1 (antiderivative) | `grep -r "::F1\|F1(" dsp/` | No | N/A | Create as static member |
| F2 (antiderivative) | `grep -r "::F2\|F2(" dsp/` | No | N/A | Create as static member |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `Sigmoid::hardClip(x, t)` | dsp/include/krate/dsp/core/sigmoid.h | 0 | Fallback for first sample and epsilon case |
| `detail::isNaN(x)` | dsp/include/krate/dsp/core/db_utils.h | 0 | NaN input detection |
| `detail::isInf(x)` | dsp/include/krate/dsp/core/db_utils.h | 0 | Infinity input detection |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `ARCHITECTURE.md` - Component inventory
- [x] `dsp/include/krate/dsp/core/sigmoid.h` - Existing hard clip function

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types are unique and not found in codebase. The class name "HardClipADAA" is specific and descriptive. Static antiderivative functions F1/F2 are scoped within the class.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| Sigmoid | hardClip | `[[nodiscard]] inline constexpr float hardClip(float x, float threshold = 1.0f) noexcept` | Yes |
| detail | isNaN | `constexpr bool isNaN(float x) noexcept` | Yes |
| detail | isInf | `[[nodiscard]] constexpr bool isInf(float x) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/sigmoid.h` - Sigmoid::hardClip
- [x] `dsp/include/krate/dsp/core/db_utils.h` - detail::isNaN, detail::isInf

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| Sigmoid::hardClip | Second param is `threshold`, not `level` | `Sigmoid::hardClip(x, threshold_)` |
| detail::isNaN | Works with -fno-fast-math only | Include note in header about build requirements |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

**Not applicable** - This is a Layer 1 primitive. No new Layer 0 utilities needed.

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| F1() | Specific to hard clip ADAA, static member is appropriate |
| F2() | Specific to hard clip ADAA, static member is appropriate |
| processFirstOrder() | Internal implementation detail |
| processSecondOrder() | Internal implementation detail |

**Decision**: All functions are specific to HardClipADAA and don't warrant Layer 0 extraction. The antiderivative functions could potentially be reused by a future TanhADAA primitive but the formulas would be completely different.

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 1 (Primitives)

**Related features at same layer** (from ROADMAP.md or known plans):
- `primitives/tanh_adaa.h` - Future tanh ADAA primitive (different formulas)
- `primitives/wavefolder.h` - May use ADAA in the future

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| ADAA epsilon constant | LOW | tanh_adaa | Keep local, copy if needed |
| ADAA fallback pattern | MEDIUM | tanh_adaa | Document pattern, don't abstract |

**Decision**: No shared base class or ADAA utility extraction. Each ADAA primitive has completely different antiderivative formulas. The pattern (epsilon check, fallback to midpoint) is simple enough to duplicate if needed.

### Review Trigger

After implementing **tanh_adaa** (if it happens), review:
- [ ] Does tanh_adaa use similar epsilon/fallback logic? -> Consider shared pattern
- [ ] Any duplicated code? -> Evaluate extraction

## Project Structure

### Documentation (this feature)

```text
specs/053-hard-clip-adaa/
├── spec.md              # Feature specification (exists)
├── plan.md              # This file
├── research.md          # ADAA algorithm research (complete)
├── data-model.md        # Class structure, state variables (complete)
├── quickstart.md        # Usage examples (complete)
├── contracts/
│   └── hard_clip_adaa.h # API contract header (complete)
└── checklists/          # Clarification logs (exists)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── primitives/
│       └── hard_clip_adaa.h    # Implementation (to create)
└── tests/
    └── unit/
        └── primitives/
            └── hard_clip_adaa_test.cpp  # Unit tests (to create)
```

**Structure Decision**: Standard Layer 1 primitive layout. Header-only implementation in `primitives/`, tests in `tests/unit/primitives/`.

## Implementation Phases

### Phase 1: Core Implementation

**Goal**: Implement HardClipADAA class with first-order ADAA

**Tasks**:
1. Write failing tests for F1() antiderivative function
2. Implement F1() to pass tests
3. Write failing tests for first-order ADAA processing
4. Implement Order enum, constructor, setters/getters
5. Implement process() with first-order ADAA
6. Write edge case tests (epsilon fallback, first sample, reset)
7. Implement edge case handling
8. Verify all tests pass
9. Commit: "Add HardClipADAA first-order ADAA implementation (#053)"

**Files Modified**:
- `dsp/include/krate/dsp/primitives/hard_clip_adaa.h` (create)
- `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp` (create)
- `dsp/tests/CMakeLists.txt` (add test file)

### Phase 2: Second-Order ADAA

**Goal**: Add second-order ADAA support

**Tasks**:
1. Write failing tests for F2() antiderivative function
2. Implement F2() to pass tests
3. Write failing tests for second-order ADAA processing
4. Implement processSecondOrder()
5. Update process() to dispatch based on order_
6. Verify all tests pass
7. Commit: "Add HardClipADAA second-order ADAA support (#053)"

**Files Modified**:
- `dsp/include/krate/dsp/primitives/hard_clip_adaa.h` (update)
- `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp` (update)

### Phase 3: Block Processing & Edge Cases

**Goal**: Add block processing and handle all edge cases

**Tasks**:
1. Write failing tests for processBlock()
2. Implement processBlock() (delegates to process())
3. Write tests verifying block == N sequential process calls
4. Write tests for NaN propagation
5. Write tests for infinity handling
6. Write tests for threshold = 0 case
7. Implement edge case handling if not already done
8. Verify all tests pass
9. Commit: "Add HardClipADAA block processing and edge cases (#053)"

**Files Modified**:
- `dsp/include/krate/dsp/primitives/hard_clip_adaa.h` (update)
- `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp` (update)

### Phase 4: Aliasing Measurement Tests

**Goal**: Verify aliasing reduction meets spec requirements

**Tasks**:
1. Write test measuring aliasing of naive hard clip (baseline)
2. Write test measuring aliasing of ADAA first-order
3. Verify >= 12 dB reduction (SC-001)
4. Write test measuring aliasing of ADAA second-order
5. Verify >= 6 dB additional reduction over first-order (SC-002)
6. Write performance benchmark test (< 10x naive)
7. Verify all tests pass
8. Commit: "Add HardClipADAA aliasing and performance tests (#053)"

**Files Modified**:
- `dsp/tests/unit/primitives/hard_clip_adaa_test.cpp` (update)

### Phase 5: Documentation & Finalization

**Goal**: Complete documentation and update architecture

**Tasks**:
1. Verify all Doxygen documentation in header
2. Update ARCHITECTURE.md with HardClipADAA entry
3. Final review of all FR-xxx and SC-xxx requirements
4. Fill compliance table in spec.md
5. Commit: "Add HardClipADAA documentation and architecture update (#053)"

**Files Modified**:
- `dsp/include/krate/dsp/primitives/hard_clip_adaa.h` (documentation)
- `ARCHITECTURE.md` (add entry)
- `specs/053-hard-clip-adaa/spec.md` (compliance table)

## Test Strategy

### Unit Tests (per spec)

| Test Category | Tests |
|---------------|-------|
| F1() function | Verify formulas for x < -t, -t <= x <= t, x > t |
| F2() function | Verify formulas for all three regions |
| First-order ADAA | Process sine wave, measure output |
| Second-order ADAA | Process sine wave, compare to first-order |
| Edge cases | First sample, reset, epsilon fallback |
| Block processing | Compare to sequential process() calls |
| Configuration | setOrder, setThreshold, getters |
| Special values | NaN propagation, infinity handling, threshold = 0 |
| Aliasing measurement | SC-001 (12 dB), SC-002 (6 dB additional) |
| Performance | SC-009 (< 10x naive) |

### Test File Structure

```cpp
// hard_clip_adaa_test.cpp
TEST_CASE("HardClipADAA F1 antiderivative", "[primitives][adaa]") { ... }
TEST_CASE("HardClipADAA F2 antiderivative", "[primitives][adaa]") { ... }
TEST_CASE("HardClipADAA first-order processing", "[primitives][adaa]") { ... }
TEST_CASE("HardClipADAA second-order processing", "[primitives][adaa]") { ... }
TEST_CASE("HardClipADAA edge cases", "[primitives][adaa]") { ... }
TEST_CASE("HardClipADAA block processing", "[primitives][adaa]") { ... }
TEST_CASE("HardClipADAA configuration", "[primitives][adaa]") { ... }
TEST_CASE("HardClipADAA special values", "[primitives][adaa]") { ... }
TEST_CASE("HardClipADAA aliasing reduction", "[primitives][adaa]") { ... }
TEST_CASE("HardClipADAA performance", "[primitives][adaa][.benchmark]") { ... }
```

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| Second-order exceeds 10x budget | Medium | Low | Document as expected; first-order is primary recommendation |
| Aliasing tests are environment-dependent | Low | Medium | Use relative measurements, generous margins |
| Float precision in antiderivative | Low | Low | Use f suffix consistently, test boundary cases |

## Complexity Tracking

> **No Constitution Check violations that must be justified**

## Success Criteria Mapping

| Criterion | Phase | Test |
|-----------|-------|------|
| SC-001: 12 dB aliasing reduction (first-order) | 4 | Aliasing measurement test |
| SC-002: 6 dB additional (second-order) | 4 | Aliasing measurement test |
| SC-003: Linear region accuracy | 1 | First-order ADAA test |
| SC-004: F1/F2 correctness | 1, 2 | Antiderivative tests |
| SC-005: Block == sequential | 3 | Block processing test |
| SC-006: No unexpected NaN/Inf | 3 | Special values test |
| SC-007: 100% test coverage | 3 | All tests combined |
| SC-008: Steady-state convergence | 1 | First-order ADAA test |
| SC-009: <= 10x naive cost | 4 | Performance benchmark |
