# Implementation Plan: Wavefolder Primitive

**Branch**: `057-wavefolder` | **Date**: 2026-01-13 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/057-wavefolder/spec.md`

## Summary

Implement a stateless wavefolding primitive (Layer 1) with three selectable algorithms: Triangle fold (modular arithmetic), Sine fold (Serge-style), and Lockhart fold (Lambert-W based). The class provides a unified interface for applying wavefolding with configurable fold intensity, delegating to existing `WavefoldMath` functions from Layer 0. Key design decisions: use accurate `lambertW()` for Lockhart, clamp foldAmount to [0.0, 10.0], and follow the Waveshaper class pattern for consistency.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**:
- `core/wavefold_math.h` (WavefoldMath::triangleFold, sineFold, lambertW)
- `core/fast_math.h` (FastMath::fastTanh)
- `core/db_utils.h` (detail::isNaN, detail::isInf)
**Storage**: N/A (stateless primitive)
**Testing**: Catch2 (dsp/tests/unit/primitives/)
**Target Platform**: Windows, macOS, Linux (cross-platform)
**Project Type**: DSP Library (monorepo)
**Performance Goals**: < 50 microseconds for 512-sample buffer at 44.1kHz (SC-003)
**Constraints**: Zero memory allocations in process(), sizeof < 16 bytes (SC-007)
**Scale/Scope**: Single class + enum, ~200 lines

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Safety):**
- [x] No memory allocation in process methods
- [x] All processing methods marked noexcept
- [x] O(1) complexity per sample

**Required Check - Principle IX (Layered Architecture):**
- [x] Layer 1 primitive depends only on Layer 0
- [x] No circular dependencies

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Required Check - Principle XVI (Honest Completion):**
- [x] All FR-xxx and SC-xxx requirements identified
- [x] Compliance table will be filled at completion

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: Wavefolder, WavefoldType

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| Wavefolder | `grep -r "class Wavefolder" dsp/ plugins/` | No | Create New |
| WavefoldType | `grep -r "enum.*WavefoldType" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: None (all math delegated to WavefoldMath)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| (none) | N/A | N/A | N/A | N/A |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| WavefoldMath::triangleFold | dsp/include/krate/dsp/core/wavefold_math.h | 0 | Triangle fold algorithm |
| WavefoldMath::sineFold | dsp/include/krate/dsp/core/wavefold_math.h | 0 | Sine fold algorithm |
| WavefoldMath::lambertW | dsp/include/krate/dsp/core/wavefold_math.h | 0 | Lockhart fold component |
| FastMath::fastTanh | dsp/include/krate/dsp/core/fast_math.h | 0 | Lockhart fold saturation |
| detail::isNaN | dsp/include/krate/dsp/core/db_utils.h | 0 | NaN detection |
| detail::isInf | dsp/include/krate/dsp/core/db_utils.h | 0 | Infinity detection |
| Waveshaper (pattern) | dsp/include/krate/dsp/primitives/waveshaper.h | 1 | Reference for class structure |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/wavefold_math.h` - Layer 0, math functions
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `ARCHITECTURE.md` - Component inventory
- [x] `dsp/include/krate/dsp/primitives/waveshaper.h` - Pattern reference

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: Wavefolder and WavefoldType are unique names not found in codebase. The WavefoldMath namespace exists but uses different naming (no collision). Search results confirm no existing Wavefolder class.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| WavefoldMath | triangleFold | `[[nodiscard]] inline float triangleFold(float x, float threshold = 1.0f) noexcept` | Yes |
| WavefoldMath | sineFold | `[[nodiscard]] inline float sineFold(float x, float gain) noexcept` | Yes |
| WavefoldMath | lambertW | `[[nodiscard]] inline float lambertW(float x) noexcept` | Yes |
| FastMath | fastTanh | `[[nodiscard]] constexpr float fastTanh(float x) noexcept` | Yes |
| detail | isNaN | `[[nodiscard]] constexpr bool isNaN(float x) noexcept` | Yes |
| detail | isInf | `[[nodiscard]] constexpr bool isInf(float x) noexcept` | Yes |
| WavefoldMath | kMinThreshold | `constexpr float kMinThreshold = 0.01f` | Yes |
| WavefoldMath | kSineFoldGainEpsilon | `constexpr float kSineFoldGainEpsilon = 0.001f` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/wavefold_math.h` - WavefoldMath namespace
- [x] `dsp/include/krate/dsp/core/fast_math.h` - FastMath::fastTanh
- [x] `dsp/include/krate/dsp/core/db_utils.h` - detail::isNaN, detail::isInf
- [x] `dsp/include/krate/dsp/primitives/waveshaper.h` - Pattern reference

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| triangleFold | threshold has minimum 0.01 (kMinThreshold) | Handled internally by triangleFold |
| sineFold | gain < 0.001 returns input (passthrough) | Handled internally by sineFold |
| lambertW | Returns NaN for x < -1/e | Lockhart exp() always produces positive values |
| fastTanh | Is constexpr, not inline | Can be used in constexpr contexts |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| (none) | All math already in Layer 0 | N/A | N/A |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| applyTriangle/Sine/Lockhart | Internal dispatch, type-specific |

**Decision**: No new Layer 0 utilities needed. All mathematical functions already exist in WavefoldMath.

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 1 - Primitives

**Related features at same layer** (from ARCHITECTURE.md):
- Waveshaper (052) - similar pattern, already implemented
- HardClipADAA (053) - anti-aliased clipping primitive
- TanhADAA - anti-aliased tanh saturation

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| WavefoldType enum | LOW | Only this class | Keep local |
| Process pattern | MEDIUM | Similar primitives | Pattern documented |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Follow Waveshaper pattern exactly | Consistency with existing Layer 1 primitives |
| No shared base class | Each primitive is independent, stateless |
| Keep WavefoldType local | Specific to this algorithm family |

### Review Trigger

After implementing next waveshaping primitive:
- [ ] Does it need similar type enum? -> Consider shared WaveshapeCategory
- [ ] Does it use same processing pattern? -> Document in CLAUDE.md
- [ ] Any duplicated edge case handling? -> Consider shared utility

## Project Structure

### Documentation (this feature)

```text
specs/057-wavefolder/
├── plan.md              # This file
├── research.md          # Phase 0 output (complete)
├── data-model.md        # Phase 1 output (complete)
├── quickstart.md        # Phase 1 output (complete)
├── contracts/           # Phase 1 output (complete)
│   └── wavefolder.h     # API contract
└── tasks.md             # Phase 2 output (NOT created by plan)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── primitives/
│       └── wavefolder.h          # Header + inline implementation
└── tests/
    └── unit/
        └── primitives/
            └── wavefolder_tests.cpp  # Unit tests
```

**Structure Decision**: Single header-only implementation in Layer 1 primitives, following the established pattern from waveshaper.h and hard_clip_adaa.h.

## Implementation Approach

### Phase 1: Core Implementation

1. **Create header file** `dsp/include/krate/dsp/primitives/wavefolder.h`
   - WavefoldType enum (uint8_t underlying)
   - Wavefolder class with members type_, foldAmount_
   - Inline implementations for all methods

2. **Implement setters/getters**
   - setType(): Direct assignment
   - setFoldAmount(): abs() then clamp to [0.0, 10.0]
   - getType(), getFoldAmount(): Simple returns

3. **Implement process()**
   - Check for NaN (propagate)
   - Check for infinity (type-specific handling)
   - Switch on type_ and delegate to WavefoldMath
   - Triangle: threshold = 1.0 / max(foldAmount_, 0.001f)
   - Sine: Direct delegation with foldAmount_ as gain
   - Lockhart: tanh(lambertW(exp(x * foldAmount_)))

4. **Implement processBlock()**
   - Simple loop calling process() for each sample

### Phase 2: Testing

1. **Unit tests** for each requirement
   - Construction defaults (FR-003, FR-004)
   - Setter behavior (FR-005 to FR-007)
   - Getter accuracy (FR-008, FR-009)
   - Algorithm delegation (FR-010 to FR-022)
   - Edge cases (NaN, Infinity, foldAmount=0)
   - Success criteria (SC-001 to SC-008)

2. **Performance test**
   - Process 512 samples, verify < 50 us

### Key Implementation Details

**Triangle fold mapping**:
```cpp
// threshold = 1.0 / foldAmount, but avoid division by zero
float threshold = foldAmount_ > 0.001f ? 1.0f / foldAmount_ : 1000.0f;
return WavefoldMath::triangleFold(x, threshold);
```

**Lockhart fold implementation**:
```cpp
// Formula: tanh(lambertW(exp(x * foldAmount)))
float scaled = x * foldAmount_;
float expValue = std::exp(scaled);  // Always positive, valid for lambertW
float w = WavefoldMath::lambertW(expValue);
return FastMath::fastTanh(w);
```

**Infinity handling**:
```cpp
if (detail::isInf(x)) {
    switch (type_) {
        case WavefoldType::Triangle:
            return x > 0.0f ? (1.0f / foldAmount_) : -(1.0f / foldAmount_);
        case WavefoldType::Sine:
            return x > 0.0f ? 1.0f : -1.0f;
        case WavefoldType::Lockhart:
            return std::numeric_limits<float>::quiet_NaN();
    }
}
```

## Complexity Tracking

> **No Constitution Check violations requiring justification.**

All design decisions comply with:
- Principle II: Real-time safety (noexcept, no allocations)
- Principle IX: Layer architecture (Layer 1 uses Layer 0)
- Principle X: No internal oversampling/DC blocking (by design)
- Principle XI: Performance budget (documented Lockhart caveat)

## Generated Artifacts

| File | Status | Description |
|------|--------|-------------|
| `specs/057-wavefolder/research.md` | Complete | Algorithm analysis, decisions |
| `specs/057-wavefolder/data-model.md` | Complete | Entity definitions, state model |
| `specs/057-wavefolder/contracts/wavefolder.h` | Complete | API contract |
| `specs/057-wavefolder/quickstart.md` | Complete | Usage guide |
| `specs/057-wavefolder/plan.md` | Complete | This file |
