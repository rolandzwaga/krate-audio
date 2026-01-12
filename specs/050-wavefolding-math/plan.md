# Implementation Plan: Wavefolding Math Library

**Branch**: `050-wavefolding-math` | **Date**: 2026-01-12 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/050-wavefolding-math/spec.md`

## Summary

This plan implements a library of pure, stateless mathematical functions for wavefolding algorithms at Layer 0 (Core Utilities). The library provides three fundamental wavefolding algorithms:
- **Lambert W function**: Transcendental function `W(x)` solving `W*exp(W) = x`, used for theoretical wavefolder design (Lockhart algorithm)
- **Triangle fold**: Symmetric mirror-like folding using modular arithmetic for multi-fold support
- **Sine fold**: `sin(gain * x)` characteristic of Serge modular synthesizers

All functions are header-only, `[[nodiscard]] inline noexcept`, and designed for real-time audio safety.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: `<cmath>` (std::sin, std::exp, std::fmod, std::abs), `<krate/dsp/core/db_utils.h>` (detail::isNaN, detail::isInf)
**Storage**: N/A (stateless functions)
**Testing**: Catch2 unit tests in `dsp/tests/unit/core/test_wavefold_math.cpp`
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: Single library header
**Performance Goals**: Layer 0 primitive < 0.1% CPU per instance
**Constraints**: Real-time safe (no allocation, noexcept), header-only
**Scale/Scope**: 4 public functions (lambertW, lambertWApprox, triangleFold, sineFold)

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

### Pre-Design Check (PASSED)

**Principle II (Real-Time Audio Thread Safety):**
- [x] All functions declared `noexcept`
- [x] No memory allocation (header-only, pure functions)
- [x] No blocking operations
- [x] No exceptions
- [x] Predictable execution time (fixed iteration count for lambertW)

**Principle III (Modern C++ Standards):**
- [x] C++20 compatible (`inline`, `[[nodiscard]]`, `constexpr` where possible)
- [x] `const` and value semantics
- [x] No raw new/delete

**Principle IX (Layered DSP Architecture):**
- [x] Layer 0 placement (only stdlib + core utilities)
- [x] No circular dependencies
- [x] No dependencies on Layer 1+

**Principle XIV (ODR Prevention):**
- [x] Unique namespace: `Krate::DSP::WavefoldMath`
- [x] No duplicate class/function names in codebase (verified via grep)

**Required Check - Principle XII (Test-First Development):**
- [x] Tasks will include TESTING-GUIDE.md context verification step
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

### Post-Design Check (PASSED)

**Re-verified after Phase 1 design completion:**

- [x] **Principle II**: Designed implementations use only `std::exp`, `std::sin`, `std::fmod` - all noexcept in practice
- [x] **Principle IX**: Header includes only `<cmath>`, `<limits>`, and `<krate/dsp/core/db_utils.h>` (Layer 0)
- [x] **Principle XIV**: Function names verified unique: `lambertW`, `lambertWApprox`, `triangleFold`, `sineFold`
- [x] **Principle XII**: Task structure enforces test-first (Task 1 creates tests, Tasks 2-5 implement)
- [x] **Cross-Platform**: No platform-specific code; uses only standard library functions

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: None (namespace with free functions only)

**Utility Functions to be created**:

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| `lambertW` | `grep -r "lambertW" dsp/ plugins/` | No | N/A | Create New |
| `lambertWApprox` | `grep -r "lambertWApprox" dsp/ plugins/` | No | N/A | Create New |
| `triangleFold` | `grep -r "triangleFold" dsp/ plugins/` | No | N/A | Create New |
| `sineFold` | `grep -r "sineFold" dsp/ plugins/` | No | N/A | Create New |
| `wavefold` | `grep -r "wavefold" dsp/ plugins/` | No | N/A | N/A (searching for related terms) |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `detail::isNaN()` | dsp/include/krate/dsp/core/db_utils.h | 0 | NaN detection for special value handling |
| `detail::isInf()` | dsp/include/krate/dsp/core/db_utils.h | 0 | Infinity detection for special value handling |
| `kPi` | dsp/include/krate/dsp/core/math_constants.h | 0 | May be useful for sineFold documentation |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `ARCHITECTURE.md` - Component inventory

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned functions are unique (confirmed via grep). Using existing `detail::isNaN()` and `detail::isInf()` from db_utils.h. New namespace `WavefoldMath` is unique.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| detail::isNaN | isNaN(float) | `constexpr bool isNaN(float x) noexcept` | Yes |
| detail::isInf | isInf(float) | `[[nodiscard]] constexpr bool isInf(float x) noexcept` | Yes |
| std::sin | sin(float) | `float sin(float)` (from `<cmath>`) | Yes |
| std::exp | exp(float) | `float exp(float)` (from `<cmath>`) | Yes |
| std::fmod | fmod(float, float) | `float fmod(float, float)` (from `<cmath>`) | Yes |
| std::abs | abs(float) | `float abs(float)` (from `<cmath>`) | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/db_utils.h` - isNaN, isInf functions
- [x] `dsp/include/krate/dsp/core/math_constants.h` - kPi constant
- [x] `dsp/include/krate/dsp/core/sigmoid.h` - Reference pattern for Layer 0 math library
- [x] `dsp/include/krate/dsp/core/chebyshev.h` - Reference pattern for Layer 0 math library

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| `detail::isNaN` | Located in `Krate::DSP::detail` namespace | `Krate::DSP::detail::isNaN(x)` or bring into scope |
| `std::fmod` | Returns NaN if divisor is 0 | Clamp threshold to minimum 0.01f first |
| `-ffast-math` | Breaks std::isnan(), must use bit manipulation | Use `detail::isNaN()` which uses bit manipulation |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

**This feature IS Layer 0** - no extraction needed. All functions being created are already at the lowest layer.

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

**Decision**: N/A - this is a Layer 0 implementation

## Higher-Layer Reusability Analysis

*Forward-looking analysis: What code from THIS feature could be reused by SIBLING features at the same layer?*

### Sibling Features Analysis

**This feature's layer**: Layer 0 (Core Utilities)

**Related features at same layer** (from ARCHITECTURE.md):
- `core/sigmoid.h` - Saturation transfer functions (already complete)
- `core/chebyshev.h` - Harmonic control polynomials (already complete)
- `core/fast_math.h` - Fast transcendental approximations (already complete)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| `WavefoldMath::sineFold` | HIGH | Layer 1 Wavefolder, Layer 2 WavefolderProcessor | Keep in wavefold_math.h |
| `WavefoldMath::triangleFold` | HIGH | Layer 1 Wavefolder, Layer 2 WavefolderProcessor | Keep in wavefold_math.h |
| `WavefoldMath::lambertW` | MEDIUM | Layer 1 LockartWavefolder | Keep in wavefold_math.h |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Single header `wavefold_math.h` | All functions are cohesive (wavefolding math), follows sigmoid.h pattern |
| Namespace `WavefoldMath` | Clear separation from `Sigmoid` and `Chebyshev` namespaces |
| No internal namespace | All functions are public API, unlike sigmoid.h which has some internal helpers |

### Review Trigger

After implementing **Layer 1 Wavefolder primitive**, review this section:
- [ ] Does Wavefolder need additional math utilities? -> Add to wavefold_math.h
- [ ] Are there shared patterns between Wavefolder and WavefolderProcessor? -> Document for future extraction

## Project Structure

### Documentation (this feature)

```text
specs/050-wavefolding-math/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
└── contracts/           # Phase 1 output
    └── wavefold_math.h  # API contract (function signatures)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── core/
│       └── wavefold_math.h    # NEW: Implementation file
└── tests/
    └── unit/
        └── core/
            └── test_wavefold_math.cpp  # NEW: Unit tests
```

**Structure Decision**: Single header file in `dsp/include/krate/dsp/core/` following existing patterns for Layer 0 math utilities (sigmoid.h, chebyshev.h, fast_math.h).

## Complexity Tracking

> No Constitution Check violations requiring justification.

## Implementation Tasks

### Phase 2: Implementation (Ordered by Dependency)

#### Task 1: Create Test File Structure
**Priority**: P0 - Must be first (test-first development)
**Estimated Time**: 15 minutes
**Dependencies**: None

**Files to Create**:
- `dsp/tests/unit/core/test_wavefold_math.cpp`

**Steps**:
1. Verify `specs/TESTING-GUIDE.md` is in context
2. Create test file with Catch2 boilerplate
3. Add placeholder test cases for all 4 functions
4. Verify build compiles (tests should fail - no implementation yet)

**Acceptance**: Test file compiles, placeholder tests exist

---

#### Task 2: Implement lambertW() with Tests (FR-001)
**Priority**: P1
**Estimated Time**: 45 minutes
**Dependencies**: Task 1

**Files to Modify**:
- `dsp/tests/unit/core/test_wavefold_math.cpp` - Add tests
- `dsp/include/krate/dsp/core/wavefold_math.h` - Create header

**Tests to Write First**:
```cpp
TEST_CASE("lambertW: basic values", "[wavefold_math]") {
    CHECK(WavefoldMath::lambertW(0.0f) == Approx(0.0f).margin(0.001f));
    CHECK(WavefoldMath::lambertW(std::exp(1.0f)) == Approx(1.0f).margin(0.001f));  // W(e) = 1
    CHECK(WavefoldMath::lambertW(0.1f) == Approx(0.0953f).margin(0.001f));
    CHECK(WavefoldMath::lambertW(1.0f) == Approx(0.567f).margin(0.001f));
}

TEST_CASE("lambertW: domain boundary", "[wavefold_math]") {
    float negOneOverE = -1.0f / std::exp(1.0f);  // -0.3679
    CHECK(WavefoldMath::lambertW(negOneOverE) == Approx(-1.0f).margin(0.01f));
    CHECK(std::isnan(WavefoldMath::lambertW(-0.5f)));  // Below domain
}

TEST_CASE("lambertW: special values", "[wavefold_math]") {
    CHECK(std::isnan(WavefoldMath::lambertW(std::numeric_limits<float>::quiet_NaN())));
    CHECK(WavefoldMath::lambertW(std::numeric_limits<float>::infinity()) == std::numeric_limits<float>::infinity());
}
```

**Implementation Algorithm**:
```cpp
[[nodiscard]] inline float lambertW(float x) noexcept {
    constexpr float kNegOneOverE = -0.36787944117144233f;
    if (Krate::DSP::detail::isNaN(x)) return x;
    if (x < kNegOneOverE) return std::numeric_limits<float>::quiet_NaN();
    if (x == 0.0f) return 0.0f;

    // Initial estimate: Halley approximation
    float w = x / (1.0f + x);

    // 4 Newton-Raphson iterations
    for (int i = 0; i < 4; ++i) {
        float ew = std::exp(w);
        float wew = w * ew;
        w = w - (wew - x) / (ew * (w + 1.0f));
    }
    return w;
}
```

**Acceptance Criteria**: SC-002 (within 0.001 tolerance of reference)

---

#### Task 3: Implement lambertWApprox() with Tests (FR-002)
**Priority**: P1
**Estimated Time**: 30 minutes
**Dependencies**: Task 2

**Files to Modify**:
- `dsp/tests/unit/core/test_wavefold_math.cpp`
- `dsp/include/krate/dsp/core/wavefold_math.h`

**Tests to Write First**:
```cpp
TEST_CASE("lambertWApprox: accuracy vs exact", "[wavefold_math]") {
    for (float x = -0.36f; x <= 1.0f; x += 0.1f) {
        float exact = WavefoldMath::lambertW(x);
        float approx = WavefoldMath::lambertWApprox(x);
        float relError = std::abs((approx - exact) / exact);
        CHECK(relError < 0.01f);  // < 1% relative error
    }
}

TEST_CASE("lambertWApprox: domain boundary", "[wavefold_math]") {
    CHECK(std::isnan(WavefoldMath::lambertWApprox(-0.5f)));
}
```

**Implementation Algorithm**:
```cpp
[[nodiscard]] inline float lambertWApprox(float x) noexcept {
    constexpr float kNegOneOverE = -0.36787944117144233f;
    if (Krate::DSP::detail::isNaN(x)) return x;
    if (x < kNegOneOverE) return std::numeric_limits<float>::quiet_NaN();
    if (x == 0.0f) return 0.0f;

    // Initial estimate: Halley approximation
    float w = x / (1.0f + x);

    // Single Newton-Raphson iteration
    float ew = std::exp(w);
    return w - (w * ew - x) / (ew * (w + 1.0f));
}
```

**Acceptance Criteria**: SC-003 (3x faster, < 0.01 relative error)

---

#### Task 4: Implement triangleFold() with Tests (FR-003, FR-004, FR-005)
**Priority**: P1
**Estimated Time**: 45 minutes
**Dependencies**: Task 1

**Files to Modify**:
- `dsp/tests/unit/core/test_wavefold_math.cpp`
- `dsp/include/krate/dsp/core/wavefold_math.h`

**Tests to Write First**:
```cpp
TEST_CASE("triangleFold: no folding within threshold", "[wavefold_math]") {
    CHECK(WavefoldMath::triangleFold(0.5f, 1.0f) == Approx(0.5f).margin(0.001f));
    CHECK(WavefoldMath::triangleFold(1.0f, 1.0f) == Approx(1.0f).margin(0.001f));
    CHECK(WavefoldMath::triangleFold(-0.5f, 1.0f) == Approx(-0.5f).margin(0.001f));
}

TEST_CASE("triangleFold: single fold", "[wavefold_math]") {
    CHECK(WavefoldMath::triangleFold(1.5f, 1.0f) == Approx(0.5f).margin(0.001f));
    CHECK(WavefoldMath::triangleFold(2.0f, 1.0f) == Approx(0.0f).margin(0.001f));
    CHECK(WavefoldMath::triangleFold(3.0f, 1.0f) == Approx(-1.0f).margin(0.001f));
}

TEST_CASE("triangleFold: symmetry", "[wavefold_math]") {
    CHECK(WavefoldMath::triangleFold(1.5f, 1.0f) == -WavefoldMath::triangleFold(-1.5f, 1.0f));
}

TEST_CASE("triangleFold: output bounds", "[wavefold_math]") {
    for (float x = -10.0f; x <= 10.0f; x += 0.5f) {
        float result = WavefoldMath::triangleFold(x, 1.0f);
        CHECK(result >= -1.0f);
        CHECK(result <= 1.0f);
    }
}

TEST_CASE("triangleFold: threshold clamping", "[wavefold_math]") {
    // Should not crash or produce NaN with zero threshold (clamped to 0.01f)
    CHECK_FALSE(std::isnan(WavefoldMath::triangleFold(1.0f, 0.0f)));
}
```

**Implementation Algorithm**:
```cpp
[[nodiscard]] inline float triangleFold(float x, float threshold = 1.0f) noexcept {
    if (Krate::DSP::detail::isNaN(x)) return x;

    threshold = std::max(0.01f, threshold);
    float ax = std::abs(x);

    float period = 4.0f * threshold;
    float phase = std::fmod(ax + threshold, period);

    float result;
    if (phase < 2.0f * threshold) {
        result = phase - threshold;
    } else {
        result = 3.0f * threshold - phase;
    }

    return std::copysign(result, x);
}
```

**Acceptance Criteria**: SC-004 (output within [-threshold, threshold])

---

#### Task 5: Implement sineFold() with Tests (FR-006, FR-007, FR-008)
**Priority**: P1
**Estimated Time**: 30 minutes
**Dependencies**: Task 1

**Files to Modify**:
- `dsp/tests/unit/core/test_wavefold_math.cpp`
- `dsp/include/krate/dsp/core/wavefold_math.h`

**Tests to Write First**:
```cpp
TEST_CASE("sineFold: linear passthrough at gain=0", "[wavefold_math]") {
    CHECK(WavefoldMath::sineFold(0.5f, 0.0f) == Approx(0.5f).margin(0.001f));
    CHECK(WavefoldMath::sineFold(-0.7f, 0.0f) == Approx(-0.7f).margin(0.001f));
}

TEST_CASE("sineFold: basic folding", "[wavefold_math]") {
    float pi = 3.14159265358979f;
    CHECK(WavefoldMath::sineFold(0.5f, pi) == Approx(std::sin(pi * 0.5f)).margin(0.001f));
}

TEST_CASE("sineFold: negative gain treated as abs", "[wavefold_math]") {
    float gain = 2.0f;
    CHECK(WavefoldMath::sineFold(0.5f, -gain) == WavefoldMath::sineFold(0.5f, gain));
}

TEST_CASE("sineFold: output bounds", "[wavefold_math]") {
    for (float x = -10.0f; x <= 10.0f; x += 0.5f) {
        float result = WavefoldMath::sineFold(x, 5.0f);
        CHECK(result >= -1.0f);
        CHECK(result <= 1.0f);
    }
}

TEST_CASE("sineFold: special values", "[wavefold_math]") {
    CHECK(std::isnan(WavefoldMath::sineFold(std::numeric_limits<float>::quiet_NaN(), 1.0f)));
}
```

**Implementation Algorithm**:
```cpp
[[nodiscard]] inline float sineFold(float x, float gain) noexcept {
    if (Krate::DSP::detail::isNaN(x)) return x;

    gain = std::abs(gain);
    if (gain < 0.001f) return x;  // Linear passthrough

    return std::sin(gain * x);
}
```

**Acceptance Criteria**: SC-005 (continuous folding), SC-008 (Serge character)

---

#### Task 6: Performance Benchmark Tests
**Priority**: P2
**Estimated Time**: 30 minutes
**Dependencies**: Tasks 2-5

**Files to Modify**:
- `dsp/tests/unit/core/test_wavefold_math.cpp`

**Tests to Write**:
```cpp
TEST_CASE("lambertWApprox: speedup over lambertW", "[wavefold_math][benchmark]") {
    constexpr int N = 100000;
    float dummy = 0.0f;

    auto startExact = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < N; ++i) {
        dummy += WavefoldMath::lambertW(static_cast<float>(i % 100) * 0.01f);
    }
    auto endExact = std::chrono::high_resolution_clock::now();

    auto startApprox = std::chrono::high_resolution_clock::now();
    for (int i = 0; i < N; ++i) {
        dummy += WavefoldMath::lambertWApprox(static_cast<float>(i % 100) * 0.01f);
    }
    auto endApprox = std::chrono::high_resolution_clock::now();

    auto exactTime = std::chrono::duration_cast<std::chrono::microseconds>(endExact - startExact).count();
    auto approxTime = std::chrono::duration_cast<std::chrono::microseconds>(endApprox - startApprox).count();

    float speedup = static_cast<float>(exactTime) / static_cast<float>(approxTime);
    CHECK(speedup >= 3.0f);  // SC-003: at least 3x faster

    // Prevent optimization
    REQUIRE(dummy != 0.0f);
}
```

**Acceptance Criteria**: SC-003 (3x speedup for approx)

---

#### Task 7: Stress Tests and Edge Cases
**Priority**: P2
**Estimated Time**: 30 minutes
**Dependencies**: Tasks 2-5

**Files to Modify**:
- `dsp/tests/unit/core/test_wavefold_math.cpp`

**Tests to Write**:
```cpp
TEST_CASE("all functions: 1M sample stress test", "[wavefold_math][stress]") {
    constexpr int N = 1000000;
    int nanCount = 0;

    for (int i = 0; i < N; ++i) {
        float x = -10.0f + 20.0f * (static_cast<float>(i) / N);

        // lambertW only valid for x >= -1/e
        if (x >= -0.36f && std::isnan(WavefoldMath::lambertW(x))) nanCount++;
        if (x >= -0.36f && std::isnan(WavefoldMath::lambertWApprox(x))) nanCount++;
        if (std::isnan(WavefoldMath::triangleFold(x, 1.0f))) nanCount++;
        if (std::isnan(WavefoldMath::sineFold(x, 3.14159f))) nanCount++;
    }

    CHECK(nanCount == 0);  // SC-006
}
```

**Acceptance Criteria**: SC-006 (zero NaN outputs for valid inputs)

---

#### Task 8: Documentation and Final Review
**Priority**: P1
**Estimated Time**: 20 minutes
**Dependencies**: Tasks 2-7

**Files to Modify**:
- `dsp/include/krate/dsp/core/wavefold_math.h` - Add Doxygen comments

**Steps**:
1. Add file header with copyright and description
2. Add Doxygen comments to all functions (see contracts/wavefold_math.h)
3. Add @par sections for harmonic character and performance notes
4. Verify FR-013 (documentation) compliance
5. Run full build and all tests
6. Update compliance table in spec.md

**Acceptance Criteria**: FR-013 (complete documentation)

---

#### Task 9: Final Verification and Commit
**Priority**: P0
**Estimated Time**: 15 minutes
**Dependencies**: All previous tasks

**Steps**:
1. Run full build: `cmake --build build --config Release`
2. Run all tests: `ctest --test-dir build -C Release --output-on-failure`
3. Verify zero compiler warnings
4. Fill compliance table in spec.md
5. Update spec.md status to COMPLETE
6. Commit with message: "feat(dsp): add wavefolding math library (050)"

**Acceptance Criteria**: All SC-xxx criteria met, spec marked complete

---

## Task Summary

| Task | Description | Priority | Est. Time | Dependencies |
|------|-------------|----------|-----------|--------------|
| 1 | Create test file structure | P0 | 15m | None |
| 2 | Implement lambertW() | P1 | 45m | Task 1 |
| 3 | Implement lambertWApprox() | P1 | 30m | Task 2 |
| 4 | Implement triangleFold() | P1 | 45m | Task 1 |
| 5 | Implement sineFold() | P1 | 30m | Task 1 |
| 6 | Performance benchmarks | P2 | 30m | Tasks 2-5 |
| 7 | Stress tests | P2 | 30m | Tasks 2-5 |
| 8 | Documentation | P1 | 20m | Tasks 2-7 |
| 9 | Final verification | P0 | 15m | All |

**Total Estimated Time**: ~4.5 hours

## Post-Implementation Review Triggers

After completing this feature:
- [ ] Review if any test utilities should be extracted to shared test helpers
- [ ] Update ARCHITECTURE.md with new Layer 0 component
- [ ] Consider if benchmark tests should be a separate test category
