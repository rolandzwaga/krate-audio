# Implementation Plan: Additive Synthesis Oscillator

**Branch**: `025-additive-oscillator` | **Date**: 2026-02-05 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/025-additive-oscillator/spec.md`

## Summary

Implement an IFFT-based additive synthesis oscillator for Layer 2 (processors/) that generates audio by summing up to 128 sinusoidal partials. The oscillator uses overlap-add resynthesis with Hann windowing at 75% overlap for efficient O(N log N) synthesis independent of partial count. Key features include per-partial amplitude/frequency/phase control, spectral tilt (dB/octave brightness), and piano-string inharmonicity for bell/metallic timbres.

---

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: FFT (`primitives/fft.h`), window functions (`core/window_functions.h`), phase utilities (`core/phase_utils.h`), math constants (`core/math_constants.h`), dB utilities (`core/db_utils.h`)
**Storage**: N/A (in-memory processing only)
**Testing**: Catch2 (test-first development per Constitution Principle XIII)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) - cross-platform
**Project Type**: DSP library component (monorepo structure)
**Performance Goals**: O(N log N) synthesis complexity; < 0.5% CPU per instance (Layer 2 budget)
**Constraints**: Real-time safe processing (no allocations after prepare()); latency = FFT size samples
**Scale/Scope**: Single class (~80 KB memory per instance at FFT 2048)

---

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Pre-Design Check (PASSED)**:

**Required Check - Principle II (Real-Time Safety):**
- [x] prepare() is the only method that allocates memory
- [x] All processing methods are noexcept
- [x] No locks, mutexes, or blocking primitives in audio path
- [x] Output sanitization prevents NaN/Inf propagation

**Required Check - Principle IX (Layered Architecture):**
- [x] AdditiveOscillator is Layer 2 (processors/)
- [x] Only depends on Layer 0 (core/) and Layer 1 (primitives/)
- [x] No circular dependencies

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, dsp-architecture) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Post-Design Check (PASSED)**:
- [x] No constitution violations in design
- [x] All existing components properly reused

---

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: AdditiveOscillator

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| AdditiveOscillator | `grep -r "class AdditiveOscillator" dsp/ plugins/` | No | Create New |
| (none others) | — | — | — |

**Utility Functions to be created**: None (all utilities exist in Layer 0)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| — | — | — | — | — |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| FFT | `dsp/include/krate/dsp/primitives/fft.h` | 1 | IFFT via `inverse()` for time-domain synthesis |
| Complex | `dsp/include/krate/dsp/primitives/fft.h` | 1 | Spectrum bin storage (real + imag) |
| Window::generate() | `dsp/include/krate/dsp/core/window_functions.h` | 0 | Hann window generation in prepare() |
| Window::generateHann() | `dsp/include/krate/dsp/core/window_functions.h` | 0 | Alternative in-place window generation |
| wrapPhase() | `dsp/include/krate/dsp/core/phase_utils.h` | 0 | Phase accumulator wrapping [0, 1) |
| detail::isNaN() | `dsp/include/krate/dsp/core/db_utils.h` | 0 | Input parameter sanitization |
| detail::isInf() | `dsp/include/krate/dsp/core/db_utils.h` | 0 | Input parameter sanitization |
| kTwoPi, kPi | `dsp/include/krate/dsp/core/math_constants.h` | 0 | Phase-to-radians conversion |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities (no AdditiveOscillator)
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (no AdditiveOscillator)
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (no AdditiveOscillator)
- [x] `specs/_architecture_/` - Component inventory (no AdditiveOscillator listed)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: AdditiveOscillator is a completely new class with no existing implementations in the codebase. The grep search for "AdditiveOscillator", "class.*Additive", "setPartialAmplitude", and "inharmonicity" returned no matching classes or synthesis implementations.

---

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| FFT | prepare | `void prepare(size_t fftSize) noexcept` | Yes |
| FFT | inverse | `void inverse(const Complex* input, float* output) noexcept` | Yes |
| FFT | size | `[[nodiscard]] size_t size() const noexcept` | Yes |
| FFT | numBins | `[[nodiscard]] size_t numBins() const noexcept` | Yes |
| FFT | isPrepared | `[[nodiscard]] bool isPrepared() const noexcept` | Yes |
| Complex | real | `float real = 0.0f` | Yes |
| Complex | imag | `float imag = 0.0f` | Yes |
| Window | generate | `[[nodiscard]] inline std::vector<float> generate(WindowType type, size_t size, float kaiserBeta = kDefaultKaiserBeta)` | Yes |
| WindowType | Hann | `enum class WindowType : uint8_t { Hann, ... }` | Yes |
| wrapPhase | — | `[[nodiscard]] constexpr double wrapPhase(double phase) noexcept` | Yes |
| detail::isNaN | — | `constexpr bool isNaN(float x) noexcept` | Yes |
| detail::isInf | — | `[[nodiscard]] constexpr bool isInf(float x) noexcept` | Yes |
| kTwoPi | — | `inline constexpr float kTwoPi = 2.0f * kPi` | Yes |
| kPi | — | `inline constexpr float kPi = 3.14159265358979323846f` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/fft.h` - FFT class, Complex struct
- [x] `dsp/include/krate/dsp/core/window_functions.h` - Window namespace, WindowType enum
- [x] `dsp/include/krate/dsp/core/phase_utils.h` - wrapPhase(), PhaseAccumulator
- [x] `dsp/include/krate/dsp/core/db_utils.h` - detail::isNaN(), detail::isInf()
- [x] `dsp/include/krate/dsp/core/math_constants.h` - kPi, kTwoPi

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| wrapPhase() | Returns double, not float | Cast result if needed: `static_cast<float>(wrapPhase(phase))` |
| FFT::inverse() | Input is Complex*, output is float* | First param is spectrum, second is time-domain |
| Complex | POD struct, not class | Direct member access: `spectrum[bin].real = val;` |
| Window::generate() | Returns std::vector (allocates) | Only call in prepare(), not processBlock() |

---

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| — | — | — | — |

No new Layer 0 utilities identified. All needed utilities already exist in the codebase:
- Phase wrapping: `wrapPhase()` in `phase_utils.h`
- NaN/Inf detection: `detail::isNaN()`, `detail::isInf()` in `db_utils.h`
- Window generation: `Window::generate()` in `window_functions.h`
- Math constants: `kTwoPi`, `kPi` in `math_constants.h`

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| calculatePartialFrequency() | Uses member variables (fundamental_, inharmonicity_, partialRatios_) |
| calculateTiltFactor() | Simple formula, only used within class |
| constructSpectrum() | Complex internal logic, not reusable |
| sanitizeOutput() | Pattern already established in existing oscillators |

**Decision**: No new Layer 0 extraction needed. All internal utilities will be private member functions.

---

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer** (from spec forward reusability section):
- Spectral Morph Filter (spectral domain processing)
- Vocoder (partial-based analysis/synthesis)
- Resynthesis engine (SMS-style analysis/synthesis)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Per-partial amplitude/phase arrays | MEDIUM | Vocoder, Resynthesis | Keep local for now |
| IFFT synthesis loop | MEDIUM | Vocoder, Resynthesis | Keep local for now |
| Inharmonicity formula | LOW | Only AdditiveOscillator | Keep as member function |
| Spectral tilt formula | LOW | Only AdditiveOscillator | Keep as member function |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep partial management local | First additive synthesis feature; patterns not established for extraction |
| No shared PartialBank class | Wait for 2nd consumer before abstracting |
| Inharmonicity as member function | Formula is specific to piano-string model; other models may differ |

### Review Trigger

After implementing **Vocoder** or **Resynthesis engine**, review this section:
- [ ] Does vocoder need partial amplitude/phase arrays? -> Consider shared PartialBank class
- [ ] Does resynthesis use similar IFFT loop? -> Consider shared synthesis primitive
- [ ] Any duplicated spectrum construction code? -> Extract to shared utility

---

## Project Structure

### Documentation (this feature)

```text
specs/025-additive-oscillator/
├── plan.md              # This file
├── research.md          # Research findings (complete)
├── data-model.md        # Data structures (complete)
├── quickstart.md        # Usage examples (complete)
├── contracts/
│   └── additive_oscillator.h  # API contract (complete)
└── tasks.md             # Implementation tasks (NOT created by this command)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── processors/
│       └── additive_oscillator.h    # NEW: Header-only implementation
└── tests/
    └── unit/
        └── processors/
            └── additive_oscillator_test.cpp  # NEW: Unit tests
```

**Structure Decision**: Single header-only implementation in Layer 2 processors, following the pattern established by `phase_distortion_oscillator.h` and other processor classes. Tests in `dsp/tests/unit/processors/`.

---

## Implementation Phases

### Phase 1: Core Infrastructure (Milestone 1)

**Goal**: Basic oscillator lifecycle and single-partial sine generation

**Files to Create**:
- `dsp/include/krate/dsp/processors/additive_oscillator.h`
- `dsp/tests/unit/processors/additive_oscillator_test.cpp`

**Requirements Covered**: FR-001, FR-002, FR-003, FR-004, FR-005, FR-006, FR-007, FR-018a, FR-024, FR-025

**Tasks**:
1. Create test file with lifecycle tests (unprepared state, prepare(), reset())
2. Create header with class skeleton, constants, and member variables
3. Implement prepare() with buffer allocation and FFT initialization
4. Implement reset() and isPrepared()
5. Implement setFundamental() with clamping
6. Implement basic processBlock() that outputs zeros when not prepared
7. Run tests, verify lifecycle behavior

**Acceptance Criteria**:
- isPrepared() returns false before prepare(), true after
- latency() returns FFT size after prepare()
- processBlock() outputs zeros when not prepared
- setFundamental() clamps to valid range

### Phase 2: Single Partial IFFT Synthesis (Milestone 2)

**Goal**: Generate pure sine wave using IFFT synthesis pipeline

**Requirements Covered**: FR-008, FR-009, FR-018, FR-019, FR-020, FR-021, FR-022, FR-023, SC-005, SC-006, SC-007

**Tasks**:
1. Add tests for single partial sine generation (frequency accuracy, amplitude)
2. Implement spectrum construction for single partial
3. Implement IFFT + windowing + overlap-add pipeline
4. Implement output buffer management (circular buffer, pull samples)
5. Test phase continuity across frames
6. Test output amplitude normalization (single partial at amp 1.0 -> peak ~1.0)

**Acceptance Criteria**:
- Single partial at 440 Hz produces 440 Hz output (within FFT bin resolution)
- Single partial at amplitude 1.0 produces peak output in [0.9, 1.1]
- No clicks or discontinuities during 60s playback
- Latency equals FFT size

### Phase 3: Multi-Partial Control (Milestone 3)

**Goal**: Full per-partial amplitude, ratio, and phase control

**Requirements Covered**: FR-010, FR-011, FR-012, FR-013

**Tasks**:
1. Add tests for setPartialAmplitude(), setPartialFrequencyRatio(), setPartialPhase()
2. Add tests for setNumPartials()
3. Implement per-partial setters with 1-based to 0-based index conversion
4. Implement spectrum construction loop for multiple partials
5. Test out-of-range partial numbers are silently ignored
6. Test phase changes only apply at reset()

**Acceptance Criteria**:
- setPartialAmplitude(0, x) and setPartialAmplitude(129, x) are ignored
- setPartialAmplitude(1, x) sets fundamental
- setNumPartials() limits active partials
- Partial phases take effect only at reset()

### Phase 4: Spectral Tilt (Milestone 4)

**Goal**: dB/octave brightness control

**Requirements Covered**: FR-014, FR-015, SC-002

**Tasks**:
1. Add tests for spectral tilt accuracy (+/- 0.5 dB per octave)
2. Implement setSpectralTilt() with clamping
3. Integrate tilt factor calculation into spectrum construction
4. Test -6 dB/octave produces expected rolloff pattern

**Acceptance Criteria**:
- At -6 dB/octave, partial 2 is ~6 dB quieter than partial 1
- At -12 dB/octave, partial 4 (2 octaves up) is ~24 dB quieter than partial 1
- Tilt of 0 dB/octave leaves amplitudes unchanged

### Phase 5: Inharmonicity (Milestone 5)

**Goal**: Piano-string frequency stretching

**Requirements Covered**: FR-016, FR-017, SC-003

**Tasks**:
1. Add tests for inharmonicity formula accuracy (0.1% relative error)
2. Implement setInharmonicity() with clamping
3. Integrate inharmonicity calculation into spectrum construction
4. Test B=0 produces exact integer multiples
5. Test B=0.001 at 440 Hz partial 10 produces ~4614.5 Hz

**Acceptance Criteria**:
- B=0 produces exact harmonic series (f_n = n * f_1)
- B=0.001 at 440 Hz, partial 10: within 0.1% of 4614.5 Hz
- Stretched partials above Nyquist are excluded

### Phase 6: Edge Cases and SC Verification (Milestone 6)

**Goal**: Complete spec compliance and edge case handling

**Requirements Covered**: SC-001, SC-004, SC-008, all edge cases from spec

**Tasks**:
1. Add tests for Nyquist exclusion (< -80 dB above Nyquist)
2. Add tests for 0 Hz fundamental (silence output)
3. Add tests for NaN/Inf input handling
4. Add tests for all partials at amplitude 0 (silence)
5. Add complexity verification test (O(N log N) via operation counting)
6. Test at sample rates 44100, 48000, 96000, 192000 Hz
7. Run pluginval-style stress test (60s continuous playback)

**Acceptance Criteria**:
- All SC-xxx criteria met with measurable evidence
- All edge cases from spec handled correctly
- No crashes or undefined behavior with adversarial inputs

### Phase 7: Documentation and Architecture Update (Milestone 7)

**Goal**: Complete documentation and architecture integration

**Tasks**:
1. Update `specs/_architecture_/layer-2-processors.md` with AdditiveOscillator entry
2. Verify all tests pass
3. Run clang-tidy
4. Final review of compliance table in spec.md

---

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| Spectral leakage at non-integer frequencies | Medium | Low | Accept nearest-bin; document bin resolution |
| Phase drift over very long playback | Low | Low | Double precision accumulators |
| CPU spikes with small block sizes | Low | Medium | O(N log N) IFFT amortizes cost over hop |
| Memory usage at FFT 4096 | Low | Low | ~160 KB per instance (acceptable) |
| Cross-platform floating-point differences | Medium | Low | Use margin-based test assertions |

---

## Complexity Tracking

No constitution violations identified. No complexity tracking needed.

---

## Appendix: Test Helper Functions Needed

The following test helpers should be added to `additive_oscillator_test.cpp`, following patterns from `phase_distortion_oscillator_test.cpp`:

```cpp
// From existing test patterns:
float computeRMS(const float* data, size_t numSamples);
float computePeak(const float* data, size_t numSamples);
float findDominantFrequency(const float* data, size_t numSamples, float sampleRate);
float getHarmonicMagnitudeDb(const float* data, size_t numSamples, float fundamental, int harmonic, float sampleRate);
```

These already exist in `phase_distortion_oscillator_test.cpp` and can be copied/adapted.
