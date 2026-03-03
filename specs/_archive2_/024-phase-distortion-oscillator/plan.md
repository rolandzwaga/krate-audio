# Implementation Plan: Phase Distortion Oscillator

**Branch**: `024-phase-distortion-oscillator` | **Date**: 2026-02-05 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `specs/024-phase-distortion-oscillator/spec.md`

## Summary

Implement a Casio CZ-style Phase Distortion oscillator at Layer 2 (processors). The oscillator morphs between a pure sine wave and 8 distinct waveform types (Saw, Square, Pulse, DoubleSine, HalfSine, ResonantSaw, ResonantTriangle, ResonantTrapezoid) using a single distortion (DCW) parameter. The implementation composes an internal `WavetableOscillator` loaded with a mipmapped cosine table for the carrier lookup, while the PD oscillator acts as a "phase shaper" that applies piecewise-linear transfer functions (non-resonant) or windowed sync techniques (resonant) to distort the phase before table lookup.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: Layer 0 (`phase_utils.h`, `math_constants.h`, `db_utils.h`, `interpolation.h`, `wavetable_data.h`), Layer 1 (`wavetable_oscillator.h`, `wavetable_generator.h`)
**Storage**: N/A (no persistent storage)
**Testing**: Catch2 unit tests, FFT-based spectral verification (following `fm_operator_test.cpp` patterns)
**Target Platform**: Windows, macOS, Linux (cross-platform)
**Project Type**: Shared DSP library component
**Performance Goals**: < 0.5% CPU for single oscillator at 44100 Hz (Layer 2 budget), 1 second of audio in < 0.5 ms
**Constraints**: Real-time safe processing (noexcept, no allocation in process()), output bounded to [-2.0, 2.0]
**Scale/Scope**: Single oscillator class (~400 lines), comprehensive test suite (~800 lines)

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle II (Real-Time Safety):**
- [x] All `process()` and `processBlock()` methods will be noexcept
- [x] No memory allocation in audio processing path
- [x] No blocking operations, locks, or I/O in processing
- [x] Phase transfer functions computed per-sample (no allocation)

**Required Check - Principle III (Modern C++):**
- [x] C++20 features: `[[nodiscard]]`, `constexpr`, value semantics
- [x] Smart pointers not needed (composition with value members)
- [x] RAII for wavetable generation in `prepare()`

**Required Check - Principle IX (Layer Architecture):**
- [x] Layer 2 processor depends only on Layers 0-1
- [x] No circular dependencies
- [x] Proper include patterns using `<krate/dsp/...>`

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Required Check - Principle XVI (Honest Completion):**
- [x] Compliance table will be filled with concrete evidence only
- [x] No relaxed thresholds, no removed features without declaration

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: `PhaseDistortionOscillator`, `PDWaveform` (enum)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| PhaseDistortionOscillator | `grep -r "class.*PhaseDistortion" dsp/ plugins/` | No | Create New |
| PDWaveform | `grep -r "PDWaveform" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: None (all utilities exist in Layer 0)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| calculatePhaseIncrement | `grep -r "calculatePhaseIncrement" dsp/` | Yes | `phase_utils.h` | Reuse |
| wrapPhase | `grep -r "wrapPhase" dsp/` | Yes | `phase_utils.h` | Reuse |
| linearInterpolate | `grep -r "linearInterpolate" dsp/` | Yes | `interpolation.h` | Reuse |
| isNaN | `grep -r "isNaN" dsp/` | Yes | `db_utils.h` | Reuse |
| isInf | `grep -r "isInf" dsp/` | Yes | `db_utils.h` | Reuse |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| WavetableOscillator | `dsp/include/krate/dsp/primitives/wavetable_oscillator.h` | 1 | Internal composition for cosine carrier lookup with mipmap anti-aliasing |
| WavetableData | `dsp/include/krate/dsp/core/wavetable_data.h` | 0 | Storage for mipmapped cosine table |
| generateMipmappedFromHarmonics | `dsp/include/krate/dsp/primitives/wavetable_generator.h` | 1 | Generate cosine table (single harmonic) in prepare() |
| PhaseAccumulator | `dsp/include/krate/dsp/core/phase_utils.h` | 0 | Phase tracking for the PD oscillator (not the carrier) |
| calculatePhaseIncrement | `dsp/include/krate/dsp/core/phase_utils.h` | 0 | Frequency to phase increment conversion |
| wrapPhase | `dsp/include/krate/dsp/core/phase_utils.h` | 0 | Phase wrapping to [0, 1) |
| kPi, kTwoPi | `dsp/include/krate/dsp/core/math_constants.h` | 0 | Phase calculations |
| detail::isNaN, detail::isInf | `dsp/include/krate/dsp/core/db_utils.h` | 0 | Input sanitization |
| Interpolation::linearInterpolate | `dsp/include/krate/dsp/core/interpolation.h` | 0 | Phase blending for DoubleSine/HalfSine |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors
- [x] `specs/_architecture_/` - Component inventory

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: `PhaseDistortionOscillator` and `PDWaveform` are unique names not found anywhere in the codebase. All utility functions are reused from existing Layer 0/1 components. The design follows the established `FMOperator` pattern which also composes `WavetableOscillator` internally.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| WavetableOscillator | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| WavetableOscillator | reset | `void reset() noexcept` | Yes |
| WavetableOscillator | setWavetable | `void setWavetable(const WavetableData* table) noexcept` | Yes |
| WavetableOscillator | setFrequency | `void setFrequency(float hz) noexcept` | Yes |
| WavetableOscillator | setPhaseModulation | `void setPhaseModulation(float radians) noexcept` | Yes |
| WavetableOscillator | process | `[[nodiscard]] float process() noexcept` | Yes |
| WavetableOscillator | phase | `[[nodiscard]] double phase() const noexcept` | Yes |
| WavetableOscillator | phaseWrapped | `[[nodiscard]] bool phaseWrapped() const noexcept` | Yes |
| WavetableOscillator | resetPhase | `void resetPhase(double newPhase = 0.0) noexcept` | Yes |
| PhaseAccumulator | phase | `double phase = 0.0` | Yes |
| PhaseAccumulator | increment | `double increment = 0.0` | Yes |
| PhaseAccumulator | advance | `[[nodiscard]] bool advance() noexcept` | Yes |
| PhaseAccumulator | reset | `void reset() noexcept` | Yes |
| PhaseAccumulator | setFrequency | `void setFrequency(float frequency, float sampleRate) noexcept` | Yes |
| generateMipmappedFromHarmonics | N/A | `void generateMipmappedFromHarmonics(WavetableData& data, const float* harmonicAmplitudes, size_t numHarmonics)` | Yes |
| detail::isNaN | N/A | `constexpr bool isNaN(float x) noexcept` | Yes |
| detail::isInf | N/A | `constexpr bool isInf(float x) noexcept` | Yes |
| wrapPhase | N/A | `[[nodiscard]] constexpr double wrapPhase(double phase) noexcept` | Yes |
| Interpolation::linearInterpolate | N/A | `[[nodiscard]] constexpr float linearInterpolate(float y0, float y1, float t) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/wavetable_oscillator.h` - WavetableOscillator class
- [x] `dsp/include/krate/dsp/core/phase_utils.h` - PhaseAccumulator struct, utility functions
- [x] `dsp/include/krate/dsp/primitives/wavetable_generator.h` - generateMipmappedFromHarmonics
- [x] `dsp/include/krate/dsp/core/wavetable_data.h` - WavetableData struct
- [x] `dsp/include/krate/dsp/core/db_utils.h` - isNaN, isInf helpers
- [x] `dsp/include/krate/dsp/core/interpolation.h` - linearInterpolate
- [x] `dsp/include/krate/dsp/core/math_constants.h` - kPi, kTwoPi

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| WavetableOscillator | Phase modulation is in radians, not normalized [0,1] | `setPhaseModulation(radians)` |
| WavetableOscillator | Must call `setWavetable()` before processing | Call in `prepare()` after generating table |
| PhaseAccumulator | Phase is in [0, 1), not radians | Phase distortion operates on normalized phase |
| generateMipmappedFromHarmonics | Index 0 = fundamental (harmonic 1) | For cosine: single element array `{1.0f}` |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| None | Phase transfer functions are specific to PD synthesis | N/A | N/A |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| computeSawPhase | Specific to PD oscillator, trivial math |
| computeSquarePhase | Specific to PD oscillator, trivial math |
| computePulsePhase | Specific to PD oscillator, trivial math |
| computeDoubleSinePhase | Specific to PD oscillator, trivial math |
| computeHalfSinePhase | Specific to PD oscillator, trivial math |
| computeResonantOutput | Specific to PD oscillator, windowed sync algorithm |

**Decision**: All phase transfer functions and resonant window functions are kept as private member functions or inline helpers. They are trivial computations specific to PD synthesis with no broader reuse potential.

## Higher-Layer Reusability Analysis

*Forward-looking analysis: What code from THIS feature could be reused by SIBLING features at the same layer?*

### Sibling Features Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer** (from ROADMAP.md or known plans):
- FMOperator (Phase 8): Also composes WavetableOscillator with phase modification
- SyncOscillator (Phase 5): Uses phase reset which resonant PD waveforms resemble
- AdditiveOscillator (Phase 11): Different approach, may share FFT test helpers

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Cosine wavetable | MEDIUM | FMOperator (already has sine), future carriers | Keep local - FMOperator already generates its own sine table |
| THD calculation (test helper) | HIGH | All oscillator tests | Already exists in fm_operator_test.cpp, reuse pattern |
| Spectral analysis helpers | HIGH | All oscillator tests | Already exists in fm_operator_test.cpp, reuse pattern |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared cosine table | FMOperator already owns its sine table; cosine can be generated or sine+phase offset used. Memory cost is acceptable (~90KB per oscillator instance). |
| Reuse test helpers | Copy pattern from fm_operator_test.cpp for THD, spectral analysis, sideband detection |

## Project Structure

### Documentation (this feature)

```text
specs/024-phase-distortion-oscillator/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
└── contracts/           # Phase 1 output
    └── phase_distortion_oscillator.h  # API contract
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── processors/
│       └── phase_distortion_oscillator.h  # Main implementation (header-only)
└── tests/
    └── unit/
        └── processors/
            └── phase_distortion_oscillator_test.cpp  # Unit tests
```

**Structure Decision**: Header-only implementation in `processors/` following the pattern established by `fm_operator.h`, `noise_generator.h`, and other Layer 2 processors.

## Complexity Tracking

> No Constitution Check violations requiring justification.

---

## Phase 0: Research Summary

### Technical Decisions

**Decision 1: Cosine Table Generation**
- **Decision**: Generate a mipmapped cosine table using `generateMipmappedFromHarmonics` with a single harmonic amplitude of `{1.0f}`, which produces a sine wave. Apply a 0.25 phase offset (90 degrees) when reading to get cosine behavior.
- **Rationale**: Reuses existing wavetable infrastructure. The sine-to-cosine conversion is trivial (phase offset). This avoids creating a new generator function.
- **Alternatives considered**: Creating a dedicated `generateMipmappedCosine()` function - rejected because the existing infrastructure handles it with a phase offset.

**Decision 2: Phase Transfer Function Architecture**
- **Decision**: Implement per-sample branching for piecewise-linear phase transfer functions (Saw, Square, Pulse, DoubleSine, HalfSine).
- **Rationale**: The spec explicitly requires per-sample computation (not tables). Branch prediction handles this efficiently since phase progresses predictably within each cycle. This provides exact mathematical accuracy and minimal memory footprint.
- **Alternatives considered**: Lookup tables - rejected per spec decision; would use more memory without meaningful performance benefit for such simple math.

**Decision 3: Resonant Waveform Implementation**
- **Decision**: Compute resonant waveforms using the windowed sync technique: `output = window(phi) * cos(2*pi*resonanceMultiplier*phi) * normConstant`.
- **Rationale**: This is the spec-defined approach. The window shapes (saw, triangle, trapezoid) are trivial per-sample computations. Normalization constants are precomputed.
- **Alternatives considered**: Using the WavetableOscillator for the resonant cosine - possible optimization but adds complexity. Direct computation is clearer and fast enough.

**Decision 4: Normalization Constants**
- **Decision**: Precompute normalization constants analytically for each resonant waveform type. For ResonantSaw, maximum occurs at phi=0 where window=1 and cos=1, so norm=1.0. For ResonantTriangle, maximum at phi=0.5 where window=1, norm depends on resonance. For ResonantTrapezoid, maximum in flat region.
- **Rationale**: Analytical computation ensures exact normalization. The spec allows these to be computed once and applied as fixed multipliers.
- **Alternatives considered**: Runtime normalization - rejected because it would require tracking peaks which adds state and complexity.

**Decision 5: WavetableOscillator Composition Strategy**
- **Decision**: The PhaseDistortionOscillator manages its own phase accumulator and feeds distorted phase values to the WavetableOscillator exclusively via `setPhaseModulation()`. The WavetableOscillator's internal phase is kept at 0 and the wavetable is used purely as a cosine lookup function with mipmap anti-aliasing.
- **Rationale**: The PD oscillator needs full control over phase distortion. Using `setPhaseModulation()` is the canonical approach (verified in Dependency API Contracts). This separation keeps concerns clean and leverages the wavetable's built-in anti-aliasing.
- **Alternatives considered**: (1) Directly accessing wavetable data with manual interpolation - rejected because it loses mipmap anti-aliasing benefits. (2) Direct phase control via other WavetableOscillator methods - rejected in favor of the cleaner `setPhaseModulation()` interface which is already verified in the API contracts.

### Normalization Constant Derivation

For resonant waveforms, the output is `window(phi) * cos(2*pi*m*phi)` where `m = resonanceMultiplier = 1 + distortion * maxResonanceFactor`.

**ResonantSaw**: `window(phi) = 1 - phi`, maximum at phi=0 where window=1 and cos(0)=1, so max=1.0. Normalization: `kResonantSawNorm = 1.0f`.

**ResonantTriangle**: `window(phi) = 1 - |2*phi - 1|`, maximum at phi=0.5 where window=1. At phi=0.5, `cos(2*pi*m*0.5) = cos(pi*m)`. For worst case with max resonance (m=9), cos(9*pi)=-1, so max absolute = 1.0. Normalization: `kResonantTriangleNorm = 1.0f`.

**ResonantTrapezoid**: Window has flat top=1.0 for phi in [0.25, 0.75]. Maximum absolute cos value in this range depends on resonance. For m=9, we need to find max|cos(2*pi*9*phi)| for phi in [0.25, 0.75]. Since the cosine completes many cycles in this range, max=1.0. Normalization: `kResonantTrapezoidNorm = 1.0f`.

**Note**: The spec says output should stay in [-1.0, 1.0]. Since window is always in [0, 1] and cos is in [-1, 1], the raw product is already in [-1, 1]. The initial normalization constants are all 1.0f (analytically derived). **Adjustment criteria**: If perceptual loudness testing during implementation reveals that resonant waveforms are noticeably louder or quieter than non-resonant waveforms at equivalent distortion settings, measure the RMS of each resonant waveform at distortion=0.5 and adjust constants to match the RMS of Saw at distortion=0.5. Document any adjusted values and their measured RMS ratios in the test file comments.

---

## Phase 1: Design Artifacts

### Data Model

See [data-model.md](data-model.md) for entity definitions.

### API Contract

See [contracts/phase_distortion_oscillator.h](contracts/phase_distortion_oscillator.h) for the complete API specification.

### Quick Start

See [quickstart.md](quickstart.md) for usage examples.

---

## Implementation Strategy

### Phase Transfer Functions (Non-Resonant)

```cpp
// FR-006: Saw phase transfer
float computeSawPhase(float phi, float distortion) const noexcept {
    // d ranges from 0.5 (distortion=0) to 0.01 (distortion=1)
    float d = 0.5f - (distortion * 0.49f);
    if (phi < d) {
        return phi * (0.5f / d);
    } else {
        return 0.5f + (phi - d) * (0.5f / (1.0f - d));
    }
}

// FR-007: Square phase transfer
float computeSquarePhase(float phi, float distortion) const noexcept {
    float d = 0.5f - (distortion * 0.49f);
    if (phi < d) {
        return phi * (0.5f / d);
    } else if (phi < 0.5f) {
        return 0.5f;  // Flat
    } else if (phi < 0.5f + d) {
        return 0.5f + (phi - 0.5f) * (0.5f / d);
    } else {
        return 1.0f;  // Flat (wraps to 0 in cosine)
    }
}

// FR-008: Pulse phase transfer
float computePulsePhase(float phi, float distortion) const noexcept {
    // Duty cycle: 50% at distortion=0, 5% at distortion=1
    float duty = 0.5f - (distortion * 0.45f);
    // Similar to square but with asymmetric duty cycle
    if (phi < duty) {
        return phi * (0.5f / duty);
    } else if (phi < 0.5f) {
        return 0.5f;
    } else if (phi < 0.5f + duty) {
        return 0.5f + (phi - 0.5f) * (0.5f / duty);
    } else {
        return 1.0f;
    }
}

// FR-009: DoubleSine phase transfer
float computeDoubleSinePhase(float phi, float distortion) const noexcept {
    // Blend between linear phase and doubled phase
    float phiDistorted = std::fmod(2.0f * phi, 1.0f);
    return Interpolation::linearInterpolate(phi, phiDistorted, distortion);
}

// FR-010: HalfSine phase transfer
float computeHalfSinePhase(float phi, float distortion) const noexcept {
    // Reflect second half back
    float phiDistorted = (phi < 0.5f) ? phi : (1.0f - phi);
    // Scale to full range for proper cosine output
    phiDistorted *= 2.0f;
    if (phiDistorted > 1.0f) phiDistorted = 1.0f;
    // Actually: phi < 0.5 -> phi, phi >= 0.5 -> 1.0 - (phi - 0.5) * 2 = 2.0 - 2*phi
    // Let's reconsider: at phi=0.5, we want phiDistorted=0.5 (peak of cosine = 0)
    // at phi=0.75, we want phiDistorted=0.25 (same as phi=0.25)
    // So: phiDistorted = phi < 0.5 ? phi : 1.0 - phi (reflected)
    float reflected = (phi < 0.5f) ? phi : (1.0f - phi);
    return Interpolation::linearInterpolate(phi, reflected, distortion);
}
```

### Resonant Waveform Computation

```cpp
// FR-011, FR-012: Resonant Saw
float computeResonantSaw(float phi, float distortion) const noexcept {
    float window = 1.0f - phi;  // Falling sawtooth window
    float resonanceMult = 1.0f + distortion * kMaxResonanceFactor;  // Default 8.0
    float resonantPhase = resonanceMult * phi;
    // Use cosine lookup or compute directly
    float cosValue = std::cos(kTwoPi * resonantPhase);
    return window * cosValue * kResonantSawNorm;
}

// FR-011, FR-013: Resonant Triangle
float computeResonantTriangle(float phi, float distortion) const noexcept {
    float window = 1.0f - std::abs(2.0f * phi - 1.0f);  // Triangle window
    float resonanceMult = 1.0f + distortion * kMaxResonanceFactor;
    float resonantPhase = resonanceMult * phi;
    float cosValue = std::cos(kTwoPi * resonantPhase);
    return window * cosValue * kResonantTriangleNorm;
}

// FR-011, FR-014: Resonant Trapezoid
float computeResonantTrapezoid(float phi, float distortion) const noexcept {
    float window;
    if (phi < 0.25f) {
        window = 4.0f * phi;  // Rising edge
    } else if (phi < 0.75f) {
        window = 1.0f;  // Flat top
    } else {
        window = 4.0f * (1.0f - phi);  // Falling edge
    }
    float resonanceMult = 1.0f + distortion * kMaxResonanceFactor;
    float resonantPhase = resonanceMult * phi;
    float cosValue = std::cos(kTwoPi * resonantPhase);
    return window * cosValue * kResonantTrapezoidNorm;
}
```

### Test Strategy

Following the pattern from `fm_operator_test.cpp`:

1. **Lifecycle Tests** (FR-001, FR-014, FR-016, FR-017, FR-029):
   - Default constructor produces silence before prepare()
   - process() before prepare() returns 0.0
   - reset() preserves configuration but clears phase
   - prepare() at different sample rates works correctly

2. **Waveform Tests** (FR-002 through FR-015):
   - At distortion=0.0, all 8 waveforms produce sine (THD < 0.5%)
   - At distortion=1.0, each waveform has characteristic spectrum
   - Intermediate distortion produces intermediate spectra

3. **API Tests** (FR-018 through FR-026):
   - setFrequency() with various values including edge cases
   - setWaveform() cycles through all types
   - setDistortion() clamps to [0, 1]
   - process(phaseModInput) adds PM correctly
   - processBlock() matches sample-by-sample processing
   - phase(), phaseWrapped(), resetPhase() work correctly

4. **Safety Tests** (FR-027, FR-028):
   - NaN/Infinity inputs are sanitized
   - Output bounded to [-2.0, 2.0]
   - Long-running processing is stable

5. **Success Criteria Tests** (SC-001 through SC-008):
   - THD measurements with specific tolerances
   - Spectral analysis for harmonic content
   - Performance benchmarks
   - Block vs sample-by-sample equivalence

---

## Files to Create

1. **`specs/024-phase-distortion-oscillator/research.md`** - Research documentation
2. **`specs/024-phase-distortion-oscillator/data-model.md`** - Entity definitions
3. **`specs/024-phase-distortion-oscillator/contracts/phase_distortion_oscillator.h`** - API contract
4. **`specs/024-phase-distortion-oscillator/quickstart.md`** - Usage examples
5. **`dsp/include/krate/dsp/processors/phase_distortion_oscillator.h`** - Implementation
6. **`dsp/tests/unit/processors/phase_distortion_oscillator_test.cpp`** - Unit tests

---

## Report

**Branch**: `024-phase-distortion-oscillator`
**Implementation Plan**: `F:\projects\iterum\specs\024-phase-distortion-oscillator\plan.md`
**Spec**: `F:\projects\iterum\specs\024-phase-distortion-oscillator\spec.md`

**Generated Artifacts**:
- Plan complete with Technical Context, Constitution Check, Codebase Research, Dependency API Contracts, Layer 0 Analysis, Higher-Layer Reusability Analysis, Implementation Strategy

**Phase 0 Research**: Complete - all technical decisions documented with rationale and alternatives considered

**Phase 1 Design**: Pending - requires creation of:
- `research.md`
- `data-model.md`
- `contracts/phase_distortion_oscillator.h`
- `quickstart.md`

**Next Steps**: Run `/speckit.tasks` to generate task breakdown for implementation.
