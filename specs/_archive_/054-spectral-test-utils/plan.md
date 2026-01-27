# Implementation Plan: Spectral Test Utilities

**Branch**: `054-spectral-test-utils` | **Date**: 2026-01-13 | **Spec**: [spec.md](./spec.md)
**Input**: Feature specification from `/specs/054-spectral-test-utils/spec.md`

## Summary

Create FFT-based aliasing measurement utilities for quantitative verification of anti-aliasing success criteria. This enables SC-001/SC-002 verification for ADAA specs by measuring aliasing power in dB using the existing `Krate::DSP::FFT` class. Target location: `tests/test_utils/spectral_analysis.h`.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: Krate::DSP::FFT, Krate::DSP::Window (existing)
**Storage**: N/A (test utilities only)
**Testing**: Catch2 (integrated) *(Constitution Principle XII: Test-First Development)*
**Target Platform**: Windows, macOS, Linux (cross-platform)
**Project Type**: Test infrastructure (not production DSP)
**Performance Goals**: N/A (test-time only, not real-time critical)
**Constraints**: Must use existing FFT class (no external library), header-only preferred
**Scale/Scope**: ~250 lines of code, 2 new files

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle II (Real-Time Safety):** N/A - Test infrastructure, not audio thread code

**Principle VI (Cross-Platform):** PASS - Uses standard C++20 and existing cross-platform DSP code

**Principle VIII (Testing Discipline):** PASS - This IS test infrastructure, will have unit tests

**Principle IX (Layered Architecture):** N/A - Test code, not part of Layer 0-4 hierarchy

**Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: AliasingMeasurement, AliasingTestConfig

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| AliasingMeasurement | `grep -r "AliasingMeasurement" dsp/ plugins/ tests/` | No | Create New |
| AliasingTestConfig | `grep -r "AliasingTestConfig" dsp/ plugins/ tests/` | No | Create New |

**Utility Functions to be created**: measureAliasing, frequencyToBin, calculateAliasedFrequency, willAlias, getAliasedBins, getHarmonicBins, compareAliasing

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| measureAliasing | `grep -r "measureAliasing" dsp/ plugins/ tests/` | No | N/A | Create New |
| frequencyToBin | `grep -r "frequencyToBin" dsp/ plugins/ tests/` | No | N/A | Create New |
| calculateAliasedFrequency | `grep -r "calculateAliasedFrequency" dsp/ plugins/ tests/` | No | N/A | Create New |
| willAlias | `grep -r "willAlias" dsp/ plugins/ tests/` | No | N/A | Create New |
| compareAliasing | `grep -r "compareAliasing" dsp/ plugins/ tests/` | No | N/A | Create New |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| FFT | dsp/include/krate/dsp/primitives/fft.h | 1 | Forward transform for spectrum analysis |
| Complex | dsp/include/krate/dsp/primitives/fft.h | 1 | Magnitude extraction from FFT bins |
| Window::generateHann | dsp/include/krate/dsp/core/window_functions.h | 0 | Windowing before FFT |
| kTwoPi | dsp/include/krate/dsp/core/math_constants.h | 0 | Test signal generation |
| TestHelpers::generateSine | tests/test_helpers/test_signals.h | - | Test signal generation |
| Sigmoid::hardClip | dsp/include/krate/dsp/core/sigmoid.h | 0 | Reference naive clipper |
| HardClipADAA | dsp/include/krate/dsp/primitives/hard_clip_adaa.h | 1 | ADAA clipper for integration tests |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `tests/test_helpers/` - Existing test utilities
- [x] No existing `spectral_analysis.h` or aliasing measurement code

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types and functions are unique and not found in codebase. The namespace `Krate::DSP::TestUtils` does not exist yet and is appropriate for test utilities that use production DSP code.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| FFT | prepare | `void prepare(size_t fftSize) noexcept` | Yes |
| FFT | forward | `void forward(const float* input, Complex* output) noexcept` | Yes |
| FFT | numBins | `[[nodiscard]] size_t numBins() const noexcept` | Yes |
| FFT | size | `[[nodiscard]] size_t size() const noexcept` | Yes |
| Complex | magnitude | `[[nodiscard]] float magnitude() const noexcept` | Yes |
| Complex | real/imag | `float real = 0.0f; float imag = 0.0f;` | Yes |
| Window::generateHann | generateHann | `inline void generateHann(float* output, size_t size) noexcept` | Yes |
| kTwoPi | constant | `inline constexpr float kTwoPi = 2.0f * kPi;` | Yes |
| HardClipADAA | setOrder | `void setOrder(Order order) noexcept;` | Yes |
| HardClipADAA | process | `[[nodiscard]] float process(float x) noexcept;` | Yes |
| HardClipADAA | reset | `void reset() noexcept;` | Yes |
| HardClipADAA::Order | enum values | `enum class Order : uint8_t { First = 0, Second = 1 };` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/fft.h` - FFT class, Complex struct, kMinFFTSize, kMaxFFTSize
- [x] `dsp/include/krate/dsp/core/window_functions.h` - Window namespace, generateHann
- [x] `dsp/include/krate/dsp/core/math_constants.h` - kPi, kTwoPi
- [x] `dsp/include/krate/dsp/primitives/hard_clip_adaa.h` - HardClipADAA class
- [x] `dsp/include/krate/dsp/core/sigmoid.h` - hardClip function

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| FFT | Output has N/2+1 bins, not N/2 | Use `fft.numBins()` |
| FFT | prepare() must be called before forward() | Check `isPrepared()` |
| Complex | No division operator | Compute `1/magnitude()` manually |
| Window | generateHann takes raw pointer and size | Pass buffer.data() and buffer.size() |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

**Not applicable** - This is test infrastructure, not part of the DSP layer hierarchy. All utilities are test-specific and belong in `tests/test_utils/`.

## Higher-Layer Reusability Analysis

**This feature's layer**: Test Infrastructure (outside Layer 0-4)

**Related test utilities at same layer**:
- `test_signals.h` - Signal generators
- `buffer_comparison.h` - Buffer comparison utilities
- `allocation_detector.h` - Allocation detection

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| measureAliasing | HIGH | 053-hard-clip-adaa tests, future ADAA tests, oversampler tests | Keep in spectral_analysis.h |
| frequencyToBin | MEDIUM | May be useful for spectral delay tests | Keep in spectral_analysis.h |
| AliasingTestConfig | HIGH | All aliasing-related tests | Keep in spectral_analysis.h |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Single header file | Matches existing test_helpers pattern |
| Namespace Krate::DSP::TestUtils | Distinguishes from TestHelpers (external) and allows DSP header usage |
| Header-only implementation | Matches test_helpers pattern, simple integration |

## Project Structure

### Documentation (this feature)

```text
specs/054-spectral-test-utils/
|-- plan.md              # This file
|-- research.md          # Phase 0 research output
|-- data-model.md        # Struct definitions
|-- quickstart.md        # Usage examples
|-- contracts/           # API contracts
    |-- spectral_analysis.h  # API contract header
```

### Source Code (repository root)

```text
tests/
|-- test_helpers/
    |-- CMakeLists.txt           # MODIFY: Add spectral_analysis.h dependency on KrateDSP
    |-- spectral_analysis.h      # NEW: Main implementation (~150 lines)
    |-- spectral_analysis_test.cpp  # NEW: Unit tests (~100 lines)

dsp/tests/
|-- CMakeLists.txt               # MODIFY: Add spectral_analysis_test.cpp if needed
|-- unit/primitives/
    |-- hard_clip_adaa_test.cpp  # MODIFY: Add SC-001/SC-002 tests using spectral_analysis.h
```

**Structure Decision**: Header-only in existing test_helpers directory, matching existing patterns.

## Complexity Tracking

No constitution violations identified. Implementation is straightforward test utility code.

## Phase 0: Research Output

See [research.md](./research.md) for:
- Aliased frequency calculation formula
- FFT bin mapping approach
- Power measurement in dB methodology
- Window function selection rationale

## Phase 1: Design Output

See [data-model.md](./data-model.md) for:
- AliasingMeasurement struct definition
- AliasingTestConfig struct definition
- Helper function signatures

See [contracts/spectral_analysis.h](./contracts/spectral_analysis.h) for:
- Complete API contract

See [quickstart.md](./quickstart.md) for:
- Usage examples
- Integration with HardClipADAA tests

## Implementation Tasks Summary

### Task Group 1: Core Measurement Infrastructure
1. Create `tests/test_helpers/spectral_analysis.h` with header guards
2. Implement `frequencyToBin()` helper
3. Implement `calculateAliasedFrequency()` helper
4. Implement `willAlias()` helper
5. Implement `getHarmonicBins()` and `getAliasedBins()`

### Task Group 2: Main Measurement Function
1. Implement `AliasingMeasurement` struct
2. Implement `AliasingTestConfig` struct with defaults
3. Implement `measureAliasing<Processor>()` template function
4. Implement `compareAliasing()` convenience function

### Task Group 3: Unit Tests
1. Create `spectral_analysis_test.cpp` (may be in test_helpers or dsp/tests)
2. Test `frequencyToBin()` with known values
3. Test `calculateAliasedFrequency()` against spec examples
4. Test `measureAliasing()` with known signal (pure sine)
5. Test that fundamental power is detected correctly

### Task Group 4: Integration Tests
1. Add SC-001 test to `hard_clip_adaa_test.cpp` (first-order vs naive)
2. Add SC-002 test to `hard_clip_adaa_test.cpp` (second-order vs first-order)
3. Verify tests pass with expected dB reduction margins
4. Update spec compliance tables

### Task Group 5: CMake Integration
1. Update `tests/test_helpers/CMakeLists.txt` to link KrateDSP
2. Add test file to appropriate CMakeLists.txt
3. Verify build succeeds on Windows

## Success Criteria Verification

| SC | Requirement | Verification Method |
|----|-------------|---------------------|
| SC-001 | measureAliasing() returns valid measurements | Unit test with pure sine |
| SC-002 | Aliased bins correctly identified | Unit test with 5kHz example from spec |
| SC-003 | Integration tests pass for hard_clip_adaa | SC-001/SC-002 tests pass |
| SC-004 | Works with any callable | Unit test with lambda, function pointer |
