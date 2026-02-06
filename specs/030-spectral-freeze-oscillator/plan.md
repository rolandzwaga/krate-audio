# Implementation Plan: Spectral Freeze Oscillator

**Branch**: `030-spectral-freeze-oscillator` | **Date**: 2026-02-06 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/030-spectral-freeze-oscillator/spec.md`

## Summary

Implement a Layer 2 DSP processor (`SpectralFreezeOscillator`) that captures a single FFT frame from audio input and continuously resynthesizes it as a frozen spectral drone. The oscillator uses coherent per-bin phase advancement with IFFT overlap-add resynthesis via a custom overlap-add ring buffer (following the `AdditiveOscillator` pattern, not the `OverlapAdd` class) with an explicit Hann synthesis window and 75% overlap. Three spectral manipulation modes are supported: pitch shift via bin shifting with linear interpolation, spectral tilt via multiplicative dB/octave gain slope, and formant shift via cepstral envelope extraction and reapplication. All processing is real-time safe with pre-allocated buffers.

The implementation reuses existing components extensively: `FFT` for transforms, `SpectralBuffer` for working spectrum, `FormantPreserver` for cepstral analysis, `Window::generateHann` for windowing, and utility functions from `spectral_utils.h` for phase advancement and bin interpolation. The `FormantPreserver` class will be extracted from `pitch_shift_processor.h` to its own header as a clean refactoring step.

## Technical Context

**Language/Version**: C++20 (MSVC 2019+, Clang 12+, GCC 10+)
**Primary Dependencies**: KrateDSP (FFT, SpectralBuffer, FormantPreserver, spectral_utils, window_functions)
**Storage**: N/A (all in-memory pre-allocated buffers)
**Testing**: Catch2 (dsp_tests target) *(Constitution Principle XII: Test-First Development)*
**Target Platform**: Windows, macOS, Linux (cross-platform via VST3 SDK)
**Project Type**: Shared DSP library component (header-only, Layer 2 processor)
**Performance Goals**: < 0.5% CPU single core @ 44.1kHz, 512-sample blocks, 2048 FFT (SC-003)
**Constraints**: < 200 KB memory for all buffers at 2048 FFT / 44.1kHz (SC-008); zero allocations in audio thread (FR-023)
**Scale/Scope**: Single class, approximately 500-700 lines of header code + 300-500 lines of tests

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Required Check - Principle I (VST3 Architecture Separation):**
- [x] This is a DSP-only component (no processor/controller involvement)
- [x] No VST3 infrastructure dependencies

**Required Check - Principle II (Real-Time Safety):**
- [x] All audio-path methods will be noexcept with no allocations
- [x] All buffers pre-allocated in prepare()
- [x] No locks, I/O, or exceptions in processBlock/freeze/unfreeze

**Required Check - Principle III (Modern C++):**
- [x] C++20 features used (constexpr, [[nodiscard]], std::clamp)
- [x] RAII for all resource management (vectors, FFT setup)
- [x] No raw new/delete

**Required Check - Principle IV (SIMD):**
- [x] SIMD viability analyzed (verdict: NOT BENEFICIAL)
- [x] Scalar-first workflow followed (no SIMD phase planned)

**Required Check - Principle IX (Layered Architecture):**
- [x] Layer 2 processor depending only on Layer 0-1 (and FormantPreserver at Layer 2)
- [x] No circular dependencies

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Required Check - Principle XVI (Honest Completion):**
- [x] All 28 FRs, 8 SCs, and 9 edge cases will be verified individually
- [x] Evidence will include file paths, line numbers, test names, and measured values

### Post-Design Re-Check

- [x] No constitution violations introduced by design decisions
- [x] FormantPreserver extraction is a non-breaking refactor (existing tests continue to pass)
- [x] Frequency-domain spectral tilt is intentionally different from time-domain SpectralTilt processor (documented in spec assumptions)

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: `SpectralFreezeOscillator`

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| SpectralFreezeOscillator | `grep -r "SpectralFreezeOscillator" dsp/ plugins/` | No | Create New |
| FormantPreserver | `grep -r "class FormantPreserver" dsp/ plugins/` | Yes (pitch_shift_processor.h) | Extract to own header (no code changes) |

**Utility Functions to be created**: None (all utilities already exist in spectral_utils.h, pitch_utils.h, db_utils.h)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| (none planned) | - | - | - | - |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `FFT` | `primitives/fft.h` | 1 | Forward FFT in freeze(), inverse FFT in processBlock() |
| `SpectralBuffer` | `primitives/spectral_buffer.h` | 1 | Working spectrum storage for IFFT input |
| `FormantPreserver` | `processors/pitch_shift_processor.h` (to be extracted) | 2 | Cepstral envelope extraction for formant shift |
| `Window::generateHann` | `core/window_functions.h` | 0 | Generate analysis + synthesis Hann windows |
| `expectedPhaseIncrement` | `primitives/spectral_utils.h` | 1 | Compute per-bin phase advance |
| `interpolateMagnitudeLinear` | `primitives/spectral_utils.h` | 1 | Fractional bin interpolation for pitch shift |
| `binToFrequency` | `primitives/spectral_utils.h` | 1 | Bin-to-frequency for spectral tilt |
| `wrapPhaseFast` | `primitives/spectral_utils.h` | 1 | Phase accumulator wrapping |
| `semitonesToRatio` | `core/pitch_utils.h` | 0 | Convert semitones to frequency ratio |
| `kPi`, `kTwoPi` | `core/math_constants.h` | 0 | Math constants |
| `kMinFFTSize`, `kMaxFFTSize` | `primitives/fft.h` | 1 | FFT size validation |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities (no conflicts)
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (no conflicts)
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (no SpectralFreezeOscillator exists)
- [x] `dsp/include/krate/dsp/effects/` - Layer 4 freeze_mode.h and pattern_freeze_mode.h are unrelated (delay buffer freeze, not spectral freeze)
- [x] `specs/_architecture_/` - Component inventory (no spectral freeze oscillator listed)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: `SpectralFreezeOscillator` is a unique name not found anywhere in the codebase. The FormantPreserver extraction is a move (not duplication) -- the original definition is replaced by an include. No new utility functions are created; all are reused from existing headers.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| FFT | prepare | `void prepare(size_t fftSize) noexcept` | Yes |
| FFT | forward | `void forward(const float* input, Complex* output) noexcept` | Yes |
| FFT | inverse | `void inverse(const Complex* input, float* output) noexcept` | Yes |
| FFT | isPrepared | `[[nodiscard]] bool isPrepared() const noexcept` | Yes |
| FFT | numBins | `[[nodiscard]] size_t numBins() const noexcept` | Yes |
| SpectralBuffer | prepare | `void prepare(size_t fftSize) noexcept` | Yes |
| SpectralBuffer | reset | `void reset() noexcept` | Yes |
| SpectralBuffer | data | `[[nodiscard]] Complex* data() noexcept` | Yes |
| SpectralBuffer | setCartesian | `void setCartesian(size_t bin, float real, float imag) noexcept` | Yes |
| SpectralBuffer | numBins | `[[nodiscard]] size_t numBins() const noexcept` | Yes |
| FormantPreserver | prepare | `void prepare(std::size_t fftSize, double sampleRate) noexcept` | Yes |
| FormantPreserver | extractEnvelope | `void extractEnvelope(const float* magnitudes, float* outputEnvelope) noexcept` | Yes |
| FormantPreserver | applyFormantPreservation | `void applyFormantPreservation(const float* shiftedMagnitudes, const float* originalEnvelope, const float* shiftedEnvelope, float* outputMagnitudes, std::size_t numBins) const noexcept` | Yes |
| FormantPreserver | reset | `void reset() noexcept` | Yes |
| Window | generateHann | `inline void generateHann(float* output, size_t size) noexcept` | Yes |
| spectral_utils | expectedPhaseIncrement | `[[nodiscard]] inline float expectedPhaseIncrement(size_t binIndex, size_t hopSize, size_t fftSize) noexcept` | Yes |
| spectral_utils | interpolateMagnitudeLinear | `[[nodiscard]] inline float interpolateMagnitudeLinear(const float* magnitudes, size_t numBins, float fractionalBin) noexcept` | Yes |
| spectral_utils | binToFrequency | `[[nodiscard]] inline constexpr float binToFrequency(size_t bin, size_t fftSize, float sampleRate) noexcept` | Yes |
| spectral_utils | wrapPhaseFast | `[[nodiscard]] inline float wrapPhaseFast(float phase) noexcept` | Yes |
| pitch_utils | semitonesToRatio | `[[nodiscard]] inline float semitonesToRatio(float semitones) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/fft.h` - FFT class (forward, inverse, prepare, numBins)
- [x] `dsp/include/krate/dsp/primitives/stft.h` - STFT and OverlapAdd classes
- [x] `dsp/include/krate/dsp/primitives/spectral_buffer.h` - SpectralBuffer class
- [x] `dsp/include/krate/dsp/primitives/spectral_utils.h` - Spectral utility functions
- [x] `dsp/include/krate/dsp/processors/pitch_shift_processor.h` - FormantPreserver class
- [x] `dsp/include/krate/dsp/processors/additive_oscillator.h` - Reference IFFT+OLA pattern
- [x] `dsp/include/krate/dsp/core/window_functions.h` - Window generators
- [x] `dsp/include/krate/dsp/core/math_constants.h` - kPi, kTwoPi
- [x] `dsp/include/krate/dsp/core/db_utils.h` - dbToGain, flushDenormal, isNaN
- [x] `dsp/include/krate/dsp/core/pitch_utils.h` - semitonesToRatio

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| FFT | forward() output has N/2+1 Complex bins, not N/2 | Use `fft.numBins()` for array sizing |
| FFT | inverse() does NOT normalize by default -- it applies 1/N internally | No manual normalization needed after inverse() |
| SpectralBuffer | `setCartesian(bin, real, imag)` is the fast path (avoids atan2/sqrt of setMagnitude/setPhase) | Use setCartesian for spectrum construction |
| FormantPreserver | prepare() takes `double sampleRate`, not `float` | Cast if needed: `formant.prepare(fftSize, sampleRate)` |
| FormantPreserver | Internal FFT adds ~16KB memory overhead | Account in SC-008 memory budget |
| wrapPhaseFast | Uses fmod, wraps to [-pi, pi] | Different from phase_utils.h wrapPhase which wraps to [0, 1) |
| OverlapAdd | Does NOT apply synthesis window -- only COLA normalization factor | Must apply Hann window ourselves before overlap-add |
| interpolateMagnitudeLinear | Clamps to first/last bin at boundaries | Safe to call with any fractional bin value |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

No new Layer 0 utilities needed. All required functions already exist in `spectral_utils.h`, `pitch_utils.h`, and `db_utils.h`.

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| `applyPitchShift()` | Tightly coupled to frozen magnitude array and working magnitudes |
| `applySpectralTilt()` | Uses class members (fftSize_, sampleRate_, numBins_) |
| `applyFormantShift()` | Uses class member FormantPreserver and envelope arrays |
| `synthesizeFrame()` | Core synthesis loop, accesses all internal state |

**Decision**: No extraction needed. All new functions are implementation-specific methods of SpectralFreezeOscillator.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | NO | Phase accumulators update forward only, no sample-to-sample feedback |
| **Data parallelism width** | 1025 bins | N/2+1 independent bins can be processed in parallel |
| **Branch density in inner loop** | LOW | Main loop is branchless (multiply, add, cos, sin per bin) |
| **Dominant operations** | Transcendental (cos, sin) | Per-bin cos/sin for Cartesian conversion dominates |
| **Current CPU budget vs expected usage** | 0.5% budget vs ~0.1% expected | Once-per-hop (every 512 samples), well under budget |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: The per-hop processing loop iterates 1025 bins with simple arithmetic and cos/sin calls. Since this executes only once every 512 samples (~86 Hz at 44.1kHz), even scalar code uses minimal CPU. The dominant cost is transcendental functions (cos/sin), which do not benefit from standard SSE/AVX arithmetic SIMD. The FFT (via pffft) is already SIMD-optimized, which is the only computationally intensive operation.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Skip bins with zero frozen magnitude | ~20-40% for sparse spectra | LOW | YES |
| Pre-compute phase increments once | Negligible (already 1 mul per bin) | LOW | YES (for clarity) |
| Fast sin/cos approximation | ~30% on synthesis loop | MEDIUM | DEFER (profile first) |
| Skip tilt/formant when parameters are 0 | ~50% when unused | LOW | YES |

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer** (from OSC-ROADMAP.md or known plans):
- AdditiveOscillator: IFFT-based synthesis with partials (already implemented)
- PhaseVocoderPitchShifter: STFT + phase vocoder + OLA (already implemented)
- Potential future "spectral morph oscillator" or "spectral crossfade" processors

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| FormantPreserver (extracted) | HIGH | PitchShiftProcessor (existing), future vocal processors | Extract now (already used by 2 consumers) |
| Frequency-domain spectral tilt | MEDIUM | Future spectral processors | Keep local, extract if 3rd consumer appears |
| Coherent phase advancement pattern | LOW | Unique to frozen spectrum resynthesis | Keep local |
| Custom OLA ring buffer pattern | MEDIUM | Already similar to AdditiveOscillator | Keep local (different enough to warrant separate impl) |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Extract FormantPreserver to own header | Already 2 consumers (PitchShiftProcessor + SpectralFreezeOscillator) |
| Keep spectral tilt local | Only 1 consumer; SpectralMorphFilter has different pivot and uses SpectralBuffer API |
| Keep OLA buffer local | Different from AdditiveOscillator (frozen spectrum vs partials), different from OverlapAdd (synthesis windowing) |

### Review Trigger

After implementing the next spectral processor at Layer 2, review:
- [ ] Does it need frequency-domain spectral tilt? If so, extract to spectral_utils.h
- [ ] Does it need frozen-spectrum OLA? If so, consider shared base or utility

## Project Structure

### Documentation (this feature)

```text
specs/030-spectral-freeze-oscillator/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output
│   └── spectral_freeze_oscillator.h  # API contract
└── tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── processors/
│       ├── spectral_freeze_oscillator.h  # NEW: Main implementation
│       ├── formant_preserver.h           # NEW: Extracted from pitch_shift_processor.h
│       └── pitch_shift_processor.h       # MODIFIED: Include formant_preserver.h
└── tests/
    └── unit/processors/
        └── spectral_freeze_oscillator_test.cpp  # NEW: Unit tests

# CMake modifications:
dsp/CMakeLists.txt                    # Add new header to KRATE_DSP_PROCESSORS_HEADERS
dsp/tests/CMakeLists.txt              # Add test file + fno-fast-math flag
```

**Structure Decision**: Standard monorepo DSP component. Single header-only implementation at Layer 2 (processors/). Test file at standard test location. FormantPreserver extracted to its own header in the same directory (processors/). No new Layer 0 or Layer 1 files needed.

## Complexity Tracking

No constitution violations. All design decisions comply with the constitution.
