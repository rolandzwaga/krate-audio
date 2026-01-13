# Implementation Plan: ChebyshevShaper Primitive

**Branch**: `058-chebyshev-shaper` | **Date**: 2026-01-13 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `/specs/058-chebyshev-shaper/spec.md`

## Summary

ChebyshevShaper is a Layer 1 primitive that provides class-based harmonic control via Chebyshev polynomial mixing. It wraps the Layer 0 `Chebyshev::harmonicMix()` function with a convenient API for setting individual harmonics (1-8), bulk setting all harmonics, and processing audio sample-by-sample or in blocks. This enables sound designers to craft specific harmonic spectra by controlling the level of each harmonic independently.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: Layer 0 `core/chebyshev.h` (Chebyshev::harmonicMix)
**Storage**: N/A (stateless except harmonic level settings)
**Testing**: Catch2 (dsp_tests target)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: Header-only DSP primitive in monorepo structure
**Performance Goals**: < 0.1% CPU per instance (Layer 1 budget), < 50us for 512-sample block
**Constraints**: Real-time safe (noexcept, no allocations), sizeof <= 40 bytes

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Safety):**
- [x] No memory allocation in processing methods
- [x] All processing methods marked noexcept
- [x] No exceptions, locks, or I/O in audio path

**Required Check - Principle IX (Layered Architecture):**
- [x] Layer 1 primitive depends only on Layer 0 (core/chebyshev.h)
- [x] No circular dependencies

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Required Check - Principle XVI (Honest Completion):**
- [ ] All FR-xxx and SC-xxx requirements will be verified
- [ ] Compliance table will be filled with evidence

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: ChebyshevShaper

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| ChebyshevShaper | `grep -r "ChebyshevShaper" dsp/ plugins/` | No | Create New |
| class.*Shaper | `grep -r "class.*Shaper" dsp/ plugins/` | Yes (Waveshaper) | Reference pattern only |

**Utility Functions to be created**: None (delegates to existing harmonicMix)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| harmonicMix | `grep -r "harmonicMix" dsp/` | Yes | core/chebyshev.h | Reuse |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| Chebyshev::harmonicMix | dsp/include/krate/dsp/core/chebyshev.h | 0 | Direct delegation from process() |
| Chebyshev::T1-T8 | dsp/include/krate/dsp/core/chebyshev.h | 0 | Reference for verification tests |
| Chebyshev::kMaxHarmonics | dsp/include/krate/dsp/core/chebyshev.h | 0 | Reference constant (32), ChebyshevShaper uses 8 |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `ARCHITECTURE.md` - Component inventory
- [x] `specs/` directory - Feature specifications

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: No existing ChebyshevShaper class found in codebase. The only related components are:
1. `Chebyshev` namespace in core/chebyshev.h (Layer 0 math functions) - different namespace level
2. `Waveshaper` class in primitives/waveshaper.h (different purpose, reference pattern only)

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| Chebyshev | harmonicMix | `[[nodiscard]] inline float harmonicMix(float x, const float* weights, std::size_t numHarmonics) noexcept` | Yes |
| Chebyshev | kMaxHarmonics | `constexpr int kMaxHarmonics = 32` | Yes |
| Chebyshev | T1-T8 | `[[nodiscard]] constexpr float T1(float x) noexcept` (pattern for T1-T8) | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/chebyshev.h` - Chebyshev namespace

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| Chebyshev::harmonicMix | weights[0] = T1 (not T0) | `harmonicMix(x, weights, numHarmonics)` where weights[0] is T1 weight |
| Chebyshev::harmonicMix | Returns 0.0 for null weights | Pass valid array pointer |
| Chebyshev::harmonicMix | numHarmonics clamped to 32 | ChebyshevShaper uses 8, well within limit |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| - | - | - | - |

No new Layer 0 utilities needed. ChebyshevShaper is a thin wrapper around existing Layer 0 functionality.

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| setHarmonicLevel | Class-specific state management |
| setAllHarmonics | Class-specific state management |
| getHarmonicLevel | Class-specific state accessor |
| process | Thin wrapper calling harmonicMix |
| processBlock | Thin wrapper iterating process |

**Decision**: All methods are class-specific state management or thin delegation wrappers. No extraction needed.

## Higher-Layer Reusability Analysis

*Forward-looking analysis: What code from THIS feature could be reused by SIBLING features at the same layer?*

### Sibling Features Analysis

**This feature's layer**: Layer 1 (Primitives)

**Related features at same layer** (from ROADMAP.md or known plans):
- Waveshaper (052) - already implemented, similar pattern
- Wavefolder (057) - already implemented, similar pattern
- Future waveshaping primitives may follow same interface pattern

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| process()/processBlock() pattern | HIGH | All Layer 1 primitives | Keep local (pattern, not code) |
| Harmonic level array pattern | MEDIUM | Future multi-parameter primitives | Keep local until 2nd use |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared base class | Waveshaper/Wavefolder/ChebyshevShaper have different parameters and state |
| Keep process/processBlock inline | Simple loop, no benefit to extraction |

## Project Structure

### Documentation (this feature)

```text
specs/058-chebyshev-shaper/
├── spec.md              # Feature specification
├── plan.md              # This file
├── checklists/          # Requirements checklist
└── (no additional artifacts needed - simple primitive)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   ├── core/
│   │   └── chebyshev.h           # Existing - Layer 0 dependency
│   └── primitives/
│       ├── waveshaper.h          # Reference pattern
│       ├── wavefolder.h          # Reference pattern
│       └── chebyshev_shaper.h    # NEW - ChebyshevShaper class
└── tests/
    └── unit/
        └── primitives/
            └── chebyshev_shaper_test.cpp  # NEW - Unit tests
```

**Structure Decision**: Standard Layer 1 primitive structure following Waveshaper/Wavefolder pattern.

## Implementation Design

### Class Design

```cpp
// Header: dsp/include/krate/dsp/primitives/chebyshev_shaper.h
namespace Krate::DSP {

class ChebyshevShaper {
public:
    /// Maximum harmonics supported (1-8)
    static constexpr int kMaxHarmonics = 8;

    /// Default constructor - all harmonics at 0.0
    ChebyshevShaper() noexcept = default;

    // Default copy/move (trivially copyable)
    ChebyshevShaper(const ChebyshevShaper&) = default;
    ChebyshevShaper& operator=(const ChebyshevShaper&) = default;
    ChebyshevShaper(ChebyshevShaper&&) noexcept = default;
    ChebyshevShaper& operator=(ChebyshevShaper&&) noexcept = default;
    ~ChebyshevShaper() = default;

    // Setters
    void setHarmonicLevel(int harmonic, float level) noexcept;
    void setAllHarmonics(const std::array<float, kMaxHarmonics>& levels) noexcept;

    // Getters
    [[nodiscard]] float getHarmonicLevel(int harmonic) const noexcept;
    [[nodiscard]] const std::array<float, kMaxHarmonics>& getHarmonicLevels() const noexcept;

    // Processing
    [[nodiscard]] float process(float x) const noexcept;
    void processBlock(float* buffer, size_t n) const noexcept;

private:
    std::array<float, kMaxHarmonics> harmonicLevels_{};  // Zero-initialized
};

// Size verification (SC-007)
static_assert(sizeof(ChebyshevShaper) <= 40, "SC-007: ChebyshevShaper must be <= 40 bytes");

} // namespace Krate::DSP
```

### Key Implementation Details

1. **process()** delegates directly to `Chebyshev::harmonicMix(x, harmonicLevels_.data(), kMaxHarmonics)`

2. **setHarmonicLevel()** validates `harmonic` in range [1, 8], maps to array index `harmonic - 1`

3. **getHarmonicLevel()** returns 0.0 for out-of-range indices (FR-010)

4. **processBlock()** iterates calling process() - produces bit-identical output (FR-017)

5. **Size**: `std::array<float, 8>` = 32 bytes, total class <= 40 bytes with alignment

### Test Strategy

Tests organized by User Story from spec:

1. **US1 - Custom Harmonic Spectrum**: Verify output matches harmonicMix with equivalent weights
2. **US2 - Individual Harmonic Control**: Test setHarmonicLevel() affects only specified harmonic
3. **US3 - Bulk Harmonic Setting**: Test setAllHarmonics() replaces all values
4. **US4 - Block Processing**: Verify bit-identical to sequential process() calls

Edge cases:
- NaN propagation
- Infinity handling
- All harmonics at 0.0 returns 0.0
- Out-of-range harmonic indices safely ignored

## Complexity Tracking

No constitution violations. Simple Layer 1 primitive following established patterns.

## Research Summary

### Phase 0 Research Completed

No external research needed. All dependencies are internal:
- `Chebyshev::harmonicMix()` already implemented and tested (spec 049)
- Waveshaper/Wavefolder patterns already established (specs 052, 057)

### Key Technical Decisions

| Decision | Rationale | Alternatives Considered |
|----------|-----------|------------------------|
| 8 harmonics (not 32) | Sufficient for musical applications, manageable API | 32 (too many parameters), variable (more complex) |
| No input clamping | Per spec clarification - pass through to harmonicMix | Clamp to [-1, 1] (rejected - mathematically valid outside) |
| 1e-5 relative tolerance | Cross-platform FP variance between MSVC/Clang | 1e-6 (too strict), 1e-4 (too loose) |
| Not thread-safe | Per spec - users synchronize externally | Thread-safe with atomics (overhead not needed for typical use) |

### Dependencies Verified

- `core/chebyshev.h` - All required functions present and tested
- Catch2 test framework - Standard project test infrastructure
- std::array - C++ standard library, no external dependency

## Implementation Tasks Preview

Implementation will follow the canonical todo list:

1. **Phase 1 - Test Setup**: Create test file, write failing tests for FR-001 through FR-003 (construction)
2. **Phase 2 - Core Implementation**: Implement class skeleton to pass construction tests
3. **Phase 3 - Setter/Getter Tests**: Write tests for FR-004 through FR-011
4. **Phase 4 - Setter/Getter Implementation**: Implement setters and getters
5. **Phase 5 - Processing Tests**: Write tests for FR-012 through FR-019
6. **Phase 6 - Processing Implementation**: Implement process() and processBlock()
7. **Phase 7 - Edge Cases**: Tests and implementation for NaN, Infinity, edge cases
8. **Phase 8 - Success Criteria**: Verify SC-001 through SC-007
9. **Phase 9 - Documentation**: Update ARCHITECTURE.md, final review

Each phase ends with build verification and commit.

---

**Plan Status**: Complete
**Ready for Implementation**: Yes
**Blockers**: None
