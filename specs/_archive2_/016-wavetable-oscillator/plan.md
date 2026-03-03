# Implementation Plan: Wavetable Oscillator with Mipmapping

**Branch**: `016-wavetable-oscillator` | **Date**: 2026-02-04 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `specs/016-wavetable-oscillator/spec.md`

## Summary

Implement a wavetable oscillator system with mipmapped anti-aliasing across three components: `core/wavetable_data.h` (Layer 0) for data storage and mipmap level selection, `primitives/wavetable_generator.h` (Layer 1) for band-limited table generation via FFT/IFFT, and `primitives/wavetable_oscillator.h` (Layer 1) for playback with cubic Hermite interpolation and automatic mipmap crossfading. This is Phase 3 from the OSC-ROADMAP, building on existing infrastructure (phase_utils.h, interpolation.h, fft.h) and following the same interface pattern as the recently completed PolyBlepOscillator (spec 015).

## Technical Context

**Language/Version**: C++20 (MSVC 2019+, Clang 12+, GCC 10+)
**Primary Dependencies**: `core/phase_utils.h` (PhaseAccumulator), `core/interpolation.h` (cubicHermiteInterpolate), `core/math_constants.h` (kPi, kTwoPi), `core/db_utils.h` (NaN/Inf detection), `primitives/fft.h` (FFT forward/inverse)
**Storage**: N/A (fixed-size struct ~90 KB per WavetableData, no heap allocation for the data itself)
**Testing**: Catch2 (via `dsp_tests` target), spectral analysis test helpers (`tests/test_helpers/spectral_analysis.h`)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: Header-only DSP library components (3 headers)
**Performance Goals**: Layer 1 < 0.1% CPU. Oscillator `process()` target ~30-40 cycles/sample (table lookup + cubic Hermite + mipmap crossfade). Generator functions are init-time only, no performance target.
**Constraints**: Zero memory allocation in process()/processBlock(), noexcept, single-threaded model, Layer 0/1 dependency rules. WavetableData is ~90 KB (11 levels * 2052 floats * 4 bytes) -- acceptable for audio.
**Scale/Scope**: 3 new header files (~200-300 lines each), 3 new test files (~500-800 lines each), modifications to 2 CMakeLists.txt files and architecture docs.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

### Pre-Design Check

| Principle | Status | Notes |
|-----------|--------|-------|
| I. VST3 Architecture Separation | N/A | Pure DSP library, no plugin code |
| II. Real-Time Audio Thread Safety | PASS | process()/processBlock() are noexcept, no allocation, no locks. Generator functions are explicitly init-time only. |
| III. Modern C++ Standards | PASS | C++20, RAII, constexpr, value semantics for WavetableData |
| IV. SIMD & DSP Optimization | PASS | Guard samples enable branchless interpolation. Aligned data layout. |
| V. VSTGUI Development | N/A | No UI code |
| VI. Cross-Platform Compatibility | PASS | Header-only, no platform-specific code |
| VII. Project Structure & Build System | PASS | Correct layers (core/, primitives/), angle bracket includes |
| VIII. Testing Discipline | PASS | Tests written before implementation |
| IX. Layered DSP Architecture | PASS | wavetable_data.h at Layer 0 (stdlib only), generator and oscillator at Layer 1 |
| X. DSP Processing Constraints | PASS | Cubic Hermite for table interpolation (modulated read), no oversampling needed (mipmap handles aliasing) |
| XI. Performance Budgets | PASS | Layer 1 target < 0.1% CPU |
| XII. Debugging Discipline | PASS | Will follow debug-before-pivot |
| XIII. Test-First Development | PASS | Failing tests first, then implementation |
| XIV. Living Architecture Documentation | PASS | Will update layer-0-core.md and layer-1-primitives.md |
| XV. Pre-Implementation Research (ODR) | PASS | All searches completed, no conflicts found (see below) |
| XVI. Honest Completion | PASS | Compliance table required before claiming done |
| XVII. Framework Knowledge | N/A | No VSTGUI/VST3 framework code |
| XVIII. Spec Numbering | PASS | Spec 016, correct |

**Required Check - Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: `WavetableData`, `WavetableOscillator`

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| `WavetableData` | `grep -r "struct WavetableData" dsp/ plugins/` | No | Create New |
| `WavetableOscillator` | `grep -r "class WavetableOscillator" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: `selectMipmapLevel`, `selectMipmapLevelFractional`, `generateMipmappedSaw`, `generateMipmappedSquare`, `generateMipmappedTriangle`, `generateMipmappedFromHarmonics`, `generateMipmappedFromSamples`

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| `selectMipmapLevel` | `grep -r "selectMipmapLevel" dsp/ plugins/` | No | N/A | Create New |
| `selectMipmapLevelFractional` | `grep -r "selectMipmapLevelFractional" dsp/ plugins/` | No | N/A | Create New |
| `generateMipmappedSaw` | `grep -r "generateMipmapped" dsp/ plugins/` | No | N/A | Create New |
| `generateMipmappedSquare` | (same search) | No | N/A | Create New |
| `generateMipmappedTriangle` | (same search) | No | N/A | Create New |
| `generateMipmappedFromHarmonics` | (same search) | No | N/A | Create New |
| `generateMipmappedFromSamples` | (same search) | No | N/A | Create New |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `PhaseAccumulator` | `dsp/include/krate/dsp/core/phase_utils.h` | 0 | Phase management in WavetableOscillator (advance, wrap detection) |
| `calculatePhaseIncrement()` | `dsp/include/krate/dsp/core/phase_utils.h` | 0 | Frequency-to-increment conversion in WavetableOscillator |
| `wrapPhase()` | `dsp/include/krate/dsp/core/phase_utils.h` | 0 | Phase wrapping for resetPhase() and PM |
| `cubicHermiteInterpolate()` | `dsp/include/krate/dsp/core/interpolation.h` | 0 | 4-point interpolation for wavetable reading |
| `linearInterpolate()` | `dsp/include/krate/dsp/core/interpolation.h` | 0 | Mipmap level crossfading |
| `kPi`, `kTwoPi` | `dsp/include/krate/dsp/core/math_constants.h` | 0 | PM radians-to-normalized conversion, harmonic generation |
| `detail::isNaN()` | `dsp/include/krate/dsp/core/db_utils.h` | 0 | Output sanitization (NaN detection) |
| `detail::isInf()` | `dsp/include/krate/dsp/core/db_utils.h` | 0 | Output sanitization (Inf detection) |
| `FFT` class | `dsp/include/krate/dsp/primitives/fft.h` | 1 | Forward/inverse FFT for wavetable generation |
| `Complex` struct | `dsp/include/krate/dsp/primitives/fft.h` | 1 | FFT bin manipulation in generator |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities (no wavetable-related types)
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 primitives (no wavetable oscillator)
- [x] `specs/_architecture_/` - Component inventory (no WavetableData or WavetableOscillator listed)
- [x] `dsp/include/krate/dsp/primitives/lfo.h` - Has inline wavetable arrays (private members, no ODR conflict)
- [x] `dsp/include/krate/dsp/processors/audio_rate_filter_fm.h` - Has inline wavetable arrays (private members, no ODR conflict)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (`WavetableData`, `WavetableOscillator`) and all planned functions (`selectMipmapLevel`, `generateMipmapped*`) are completely unique names not found anywhere in the codebase. The term "wavetable" appears only as inline array members in `lfo.h` and `audio_rate_filter_fm.h`, which are not named types and present no ODR conflict.

## Dependency API Contracts (Principle XV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| PhaseAccumulator | phase | `double phase = 0.0` (public member) | Yes |
| PhaseAccumulator | increment | `double increment = 0.0` (public member) | Yes |
| PhaseAccumulator | advance() | `[[nodiscard]] bool advance() noexcept` | Yes |
| PhaseAccumulator | reset() | `void reset() noexcept` | Yes |
| PhaseAccumulator | setFrequency() | `void setFrequency(float frequency, float sampleRate) noexcept` | Yes |
| phase_utils | calculatePhaseIncrement | `[[nodiscard]] constexpr double calculatePhaseIncrement(float frequency, float sampleRate) noexcept` | Yes |
| phase_utils | wrapPhase | `[[nodiscard]] constexpr double wrapPhase(double phase) noexcept` | Yes |
| Interpolation | cubicHermiteInterpolate | `[[nodiscard]] constexpr float cubicHermiteInterpolate(float ym1, float y0, float y1, float y2, float t) noexcept` | Yes |
| Interpolation | linearInterpolate | `[[nodiscard]] constexpr float linearInterpolate(float y0, float y1, float t) noexcept` | Yes |
| math_constants | kPi | `inline constexpr float kPi = 3.14159265358979323846f` | Yes |
| math_constants | kTwoPi | `inline constexpr float kTwoPi = 2.0f * kPi` | Yes |
| db_utils | detail::isNaN | `constexpr bool isNaN(float x) noexcept` | Yes |
| db_utils | detail::isInf | `[[nodiscard]] constexpr bool isInf(float x) noexcept` | Yes |
| FFT | prepare | `void prepare(size_t fftSize) noexcept` | Yes |
| FFT | forward | `void forward(const float* input, Complex* output) noexcept` | Yes |
| FFT | inverse | `void inverse(const Complex* input, float* output) noexcept` | Yes |
| FFT | isPrepared | `[[nodiscard]] bool isPrepared() const noexcept` | Yes |
| FFT | size | `[[nodiscard]] size_t size() const noexcept` | Yes |
| FFT | numBins | `[[nodiscard]] size_t numBins() const noexcept` | Yes |
| Complex | real | `float real = 0.0f` (public member) | Yes |
| Complex | imag | `float imag = 0.0f` (public member) | Yes |
| Complex | magnitude | `[[nodiscard]] float magnitude() const noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/phase_utils.h` - PhaseAccumulator struct, utility functions
- [x] `dsp/include/krate/dsp/core/interpolation.h` - linearInterpolate, cubicHermiteInterpolate, lagrangeInterpolate
- [x] `dsp/include/krate/dsp/core/math_constants.h` - kPi, kTwoPi, kHalfPi, kPiSquared
- [x] `dsp/include/krate/dsp/core/db_utils.h` - detail::isNaN, detail::isInf
- [x] `dsp/include/krate/dsp/primitives/fft.h` - FFT class, Complex struct
- [x] `dsp/include/krate/dsp/primitives/polyblep_oscillator.h` - PolyBlepOscillator (interface reference)

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| PhaseAccumulator | `advance()` returns bool (wrapped), NOT the new phase | `bool wrapped = phaseAcc_.advance();` |
| PhaseAccumulator | `setFrequency()` takes `(float, float)` not `(double, double)` | `phaseAcc_.setFrequency(freq, sampleRate_)` |
| calculatePhaseIncrement | Returns `double`, not `float` | Cast to float for table index: `float(...)` |
| cubicHermiteInterpolate | Parameter order: ym1, y0, y1, y2, t (NOT y0, y1, y2, y3) | `cubicHermiteInterpolate(p[-1], p[0], p[1], p[2], frac)` |
| cubicHermiteInterpolate | In `Interpolation` namespace, not `Krate::DSP` directly | `Interpolation::cubicHermiteInterpolate(...)` |
| linearInterpolate | Also in `Interpolation` namespace | `Interpolation::linearInterpolate(...)` |
| FFT | `forward()` outputs N/2+1 Complex bins (not N) | `numBins = fftSize / 2 + 1` |
| FFT | `inverse()` takes N/2+1 Complex bins as input | Must pass same count as forward outputs |
| FFT | Non-copyable, movable only | Use as local variable in generator functions, not stored |
| kTwoPi | Is `float` (not `double`) | Use for float operations; cast for double phase math |
| detail::isNaN | In `detail` namespace (not public) | `Krate::DSP::detail::isNaN(x)` |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| `selectMipmapLevel()` | Pure math function, no DSP state. Needed by oscillator (Layer 1) and potentially future spectral components. | `core/wavetable_data.h` | WavetableOscillator, future FM Operator, PD Oscillator |
| `selectMipmapLevelFractional()` | Same rationale as above, used for crossfading. | `core/wavetable_data.h` | WavetableOscillator |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| Output sanitization (NaN/clamp) | Specific to oscillator output, same pattern as PolyBlepOscillator. |
| Guard sample setup | Part of generator function, called once during generation. |

**Decision**: `selectMipmapLevel` and `selectMipmapLevelFractional` are standalone free functions at Layer 0 in `wavetable_data.h`. `WavetableData` is a value type at Layer 0. All generator functions and the oscillator class are at Layer 1.

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 0 (WavetableData) + Layer 1 (Generator, Oscillator)

**Related features at same layer** (from OSC-ROADMAP.md):
- Phase 4: MinBlepTable (Layer 1) -- Different domain, no overlap
- Phase 9: NoiseOscillator (Layer 1) -- Independent, no overlap
- Phase 8: FM Operator (Layer 2) -- Direct consumer of WavetableOscillator
- Phase 10: PD Oscillator (Layer 2) -- Direct consumer of WavetableOscillator

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| `WavetableData` struct | HIGH | FM Operator, PD Oscillator, Additive Oscillator, Vector Mixer | Extract to Layer 0 now (3+ clear consumers) |
| `selectMipmapLevel` functions | HIGH | FM Operator (mipmap selection for sine carrier), any wavetable-based component | Keep in wavetable_data.h as standalone functions |
| Generator functions | HIGH | FM Operator (sine table), PD Oscillator (custom tables), user-loaded wavetables | Keep in wavetable_generator.h |
| `WavetableOscillator` class | HIGH | FM Operator (carrier/modulator), PD Oscillator (underlying oscillator), Vector Mixer | Designed for composition via non-owning pointer |

### Detailed Analysis (for HIGH potential items)

**WavetableData** provides:
- Standardized storage for mipmapped waveform data
- Mipmap level selection functions
- Guard samples for branchless interpolation
- Immutable-after-generation contract for thread safety

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| FM Operator (Phase 8) | YES | Needs sine wavetable as carrier/modulator |
| PD Oscillator (Phase 10) | YES | Needs underlying sine table with distorted phase readout |
| Additive Oscillator (Phase 11) | MAYBE | May use IFFT directly to wavetable storage |
| Vector Mixer (Phase 17) | YES | Mixes outputs from WavetableOscillator instances |

**Recommendation**: Extract WavetableData to Layer 0 now -- 3+ clear consumers confirmed in the roadmap.

### Decision Log

| Decision | Rationale |
|----------|-----------|
| WavetableData at Layer 0 | Required by spec (FR-011), and has 3+ downstream consumers per roadmap |
| Generator at Layer 1 | Depends on FFT (Layer 1). Cannot be Layer 0. |
| Oscillator at Layer 1 | Depends on WavetableData (L0), interpolation (L0), phase_utils (L0). Layer 1 is correct. |
| Non-owning pointer for wavetable | Enables sharing across polyphonic voices without copying ~90 KB per voice |
| Fixed-size storage in WavetableData | Avoids heap allocation, makes it a value type. ~90 KB is acceptable for audio. |

### Review Trigger

After implementing **Phase 8 (FM Operator)**, review this section:
- [ ] Does FM Operator need additional WavetableData features (e.g., sine-specific optimization)?
- [ ] Does FM Operator use the same phase interface? -> Document shared pattern
- [ ] Any duplicated code between wavetable and polyblep oscillators? -> Consider shared base

## Project Structure

### Documentation (this feature)

```text
specs/016-wavetable-oscillator/
+-- plan.md              # This file
+-- spec.md              # Feature specification
+-- research.md          # Phase 0 research output
+-- data-model.md        # Phase 1 data model
+-- quickstart.md        # Phase 1 implementation guide
+-- contracts/           # Phase 1 API contracts
|   +-- wavetable_data_api.h
|   +-- wavetable_generator_api.h
|   +-- wavetable_oscillator_api.h
```

### Source Code (repository root)

```text
dsp/
+-- include/krate/dsp/
|   +-- core/
|   |   +-- wavetable_data.h           # [NEW] Layer 0: data struct + mipmap selection
|   |   +-- phase_utils.h              # [EXISTS] dependency
|   |   +-- interpolation.h            # [EXISTS] dependency
|   |   +-- math_constants.h           # [EXISTS] dependency
|   |   +-- db_utils.h                 # [EXISTS] dependency
|   +-- primitives/
|       +-- wavetable_generator.h      # [NEW] Layer 1: mipmap generation via FFT
|       +-- wavetable_oscillator.h     # [NEW] Layer 1: playback with cubic Hermite
|       +-- fft.h                      # [EXISTS] dependency
+-- tests/
    +-- unit/core/
    |   +-- wavetable_data_test.cpp    # [NEW] WavetableData + selectMipmapLevel tests
    +-- unit/primitives/
    |   +-- wavetable_generator_test.cpp   # [NEW] Generator tests with FFT verification
    |   +-- wavetable_oscillator_test.cpp  # [NEW] Oscillator playback + aliasing tests
    +-- CMakeLists.txt                     # [MODIFY] Add 3 test files + -fno-fast-math
+-- CMakeLists.txt                         # [MODIFY] Add headers to IDE visibility lists
```

**Structure Decision**: Three header-only files across two layers. Tests mirror the header structure. No new directories needed.

## Complexity Tracking

No constitution violations. No complexity justifications needed.

## Post-Design Constitution Re-Check

| Principle | Status | Notes |
|-----------|--------|-------|
| II. Real-Time Safety | PASS | process()/processBlock() are noexcept, no allocation, no locks. Guard samples enable branchless interpolation. Generator functions are explicitly NOT real-time safe (documented). |
| III. Modern C++ | PASS | C++20, `[[nodiscard]]`, constexpr level selection, `std::array` for fixed storage |
| IV. SIMD Optimization | PASS | Guard samples enable branchless pointer arithmetic. Data is 32-byte aligned. processBlock() is a tight loop. |
| IX. Layered Architecture | PASS | wavetable_data.h (L0, stdlib only), generator (L1, depends on L0 + fft), oscillator (L1, depends on L0 only) |
| XIII. Test-First | PASS | Implementation phases write tests before code |
| XV. ODR Prevention | PASS | All planned types verified unique. No conflicts with lfo.h or audio_rate_filter_fm.h wavetable arrays. |
| XVI. Honest Completion | PASS | All 52 FR-xxx and 20 SC-xxx requirements tracked in spec compliance table |
