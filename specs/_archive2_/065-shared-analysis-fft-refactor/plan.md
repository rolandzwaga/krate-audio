# Implementation Plan: Shared-Analysis FFT Refactor

**Branch**: `065-shared-analysis-fft-refactor` | **Date**: 2026-02-18 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/065-shared-analysis-fft-refactor/spec.md`

## Summary

Refactor PhaseVocoderPitchShifter (Layer 2) and HarmonizerEngine (Layer 3) to share a single forward FFT analysis across all 4 harmony voices. Currently, each voice runs its own forward FFT on the identical input, wasting 75% of forward FFT computation and contributing to PhaseVocoder mode measuring ~24% CPU vs a 15% budget. The refactor introduces a `processWithSharedAnalysis()` method at Layer 2 that accepts a pre-computed `const SpectralBuffer&` analysis spectrum, performs only per-voice synthesis (phase rotation, iFFT, OLA), and integrates with a shared STFT instance owned by HarmonizerEngine at Layer 3. All per-voice state (phase accumulators, OLA buffers, formant/transient/phase-locking state) remains strictly independent. The existing `process()` API is preserved unchanged.

## Technical Context

**Language/Version**: C++20 (header-only DSP components)
**Primary Dependencies**: KrateDSP (STFT, SpectralBuffer, OverlapAdd, PhaseVocoderPitchShifter, PitchShiftProcessor, HarmonizerEngine), pffft (FFT backend)
**Storage**: N/A (in-memory audio processing only)
**Testing**: Catch2 (dsp_tests target) *(Constitution Principle XII: Test-First Development)*
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- cross-platform required
**Project Type**: Shared DSP library (header-only, monorepo)
**Performance Goals**: PhaseVocoder 4-voice CPU < 18% at 44.1kHz/256 (SC-001), down from ~24% baseline
**Constraints**: Zero heap allocations during `process()` (SC-005), real-time audio thread safety (Constitution II), header-only components
**Scale/Scope**: 2 modified classes (PhaseVocoderPitchShifter, HarmonizerEngine), 1 wrapper class (PitchShiftProcessor), ~300 lines of new/modified code, 0 new types

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Pre-Design Check (PASSED):**

| Principle | Status | Notes |
|-----------|--------|-------|
| I. VST3 Architecture Separation | N/A | DSP-only refactor, no processor/controller changes |
| II. Real-Time Audio Thread Safety | PASS | No new allocations in audio path. All new buffers allocated in `prepare()`. `const SpectralBuffer&` is a pointer-size parameter. |
| III. Modern C++ Standards | PASS | Uses `constexpr`, `const`, `noexcept`, `std::fill`, smart pointers unchanged |
| IV. SIMD & DSP Optimization | PASS | SIMD analysis completed below. Algorithmic optimization (shared FFT) is the primary strategy. |
| V. VSTGUI Development | N/A | No UI changes |
| VI. Cross-Platform Compatibility | PASS | No platform-specific code. Pure C++ with standard library only. |
| VII. Project Structure & Build | PASS | Modifies existing files in correct layers. No new files needed. |
| VIII. Testing Discipline | PASS | Test-first workflow planned. Existing tests must pass unchanged (SC-004). |
| IX. Layered DSP Architecture | PASS | Layer 2 (processors) changes do not depend on Layer 3. Layer 3 (systems) composes Layer 2 through public API. No upward dependencies. |
| X. DSP Processing Constraints | N/A | No new saturation, delay, or interpolation. |
| XI. Performance Budgets | PASS | Target: <18% CPU for 4-voice PhaseVocoder (within Layer 3 <5% total plugin budget including all overhead) |
| XII. Debugging Discipline | PASS | Framework code (STFT, OLA, SpectralBuffer) is reused as-is, not modified. |
| XIII. Test-First Development | PASS | Failing tests written before implementation per workflow. |
| XIV. Living Architecture Documentation | PASS | Architecture docs updated in final phase. |
| XV. Pre-Implementation Research (ODR) | PASS | No new types created. ODR search completed below. |
| XVI. Honest Completion | PASS | Compliance table with specific evidence required. |

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: NONE. This refactor adds methods to existing classes only.

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| (none) | N/A | N/A | N/A |

**Utility Functions to be created**: New methods on existing classes only.

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| `processWithSharedAnalysis` | `grep -r "processWithSharedAnalysis" dsp/ plugins/` | No | N/A | Add to PhaseVocoderPitchShifter + PitchShiftProcessor |
| `pullOutputSamples` | `grep -r "pullOutputSamples" dsp/ plugins/` | No | N/A | Add to PhaseVocoderPitchShifter |
| `outputSamplesAvailable` | `grep -r "outputSamplesAvailable" dsp/ plugins/` | No | N/A | Add to PhaseVocoderPitchShifter |
| `pullSharedAnalysisOutput` | `grep -r "pullSharedAnalysisOutput" dsp/ plugins/` | No | N/A | Add to PitchShiftProcessor |
| `sharedAnalysisSamplesAvailable` | `grep -r "sharedAnalysisSamplesAvailable" dsp/ plugins/` | No | N/A | Add to PitchShiftProcessor |
| `getPhaseVocoderFFTSize` | `grep -r "getPhaseVocoderFFTSize" dsp/ plugins/` | No | N/A | Add to PitchShiftProcessor |
| `getPhaseVocoderHopSize` | `grep -r "getPhaseVocoderHopSize" dsp/ plugins/` | No | N/A | Add to PitchShiftProcessor |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `PhaseVocoderPitchShifter` | `dsp/include/krate/dsp/processors/pitch_shift_processor.h` (L936-1433) | 2 | Modified: add `processWithSharedAnalysis()`, refactor `processFrame()` signature |
| `PitchShiftProcessor` | `dsp/include/krate/dsp/processors/pitch_shift_processor.h` (L98-295) | 2 | Modified: add shared-analysis delegation methods + FFT/hop accessors |
| `PitchShiftProcessor::Impl` | `dsp/include/krate/dsp/processors/pitch_shift_processor.h` (L1440-1459) | 2 | Modified: add delegation methods in Impl struct |
| `STFT` | `dsp/include/krate/dsp/primitives/stft.h` (L34-173) | 1 | Reused as-is: HarmonizerEngine owns a shared instance |
| `SpectralBuffer` | `dsp/include/krate/dsp/primitives/spectral_buffer.h` (L44-216) | 1 | Reused as-is: holds shared analysis spectrum, passed by `const` ref |
| `OverlapAdd` | `dsp/include/krate/dsp/primitives/stft.h` (L180-324) | 1 | Reused as-is: per-voice OLA buffers inside PhaseVocoderPitchShifter |
| `HarmonizerEngine` | `dsp/include/krate/dsp/systems/harmonizer_engine.h` (L66-483) | 3 | Modified: add shared STFT + SpectralBuffer members, PhaseVocoder-mode process path |
| `FormantPreserver` | `dsp/include/krate/dsp/processors/pitch_shift_processor.h` | 2 | Unchanged: operates within PhaseVocoderPitchShifter |
| `SpectralTransientDetector` | `dsp/include/krate/dsp/processors/spectral_transient_detector.h` | 2 | Unchanged: operates within PhaseVocoderPitchShifter |
| `semitonesToRatio` | `dsp/include/krate/dsp/core/pitch_utils.h` (or similar) | 0 | Reused: compute pitch ratio from semitones in HarmonizerEngine |
| `wrapPhase` | `dsp/include/krate/dsp/primitives/spectral_utils.h` (L173) | 1 | Reused: phase wrapping in processFrame (unchanged) |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities -- no conflicts
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives -- STFT and SpectralBuffer reused as-is
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors -- PhaseVocoderPitchShifter and PitchShiftProcessor modified
- [x] `dsp/include/krate/dsp/systems/` - Layer 3 systems -- HarmonizerEngine modified
- [x] `specs/_architecture_/` - Component inventory checked

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: No new types (classes, structs, enums) are created. All changes are method additions and member additions to existing classes. The method names (`processWithSharedAnalysis`, `pullOutputSamples`, etc.) are unique and do not conflict with any existing method in the codebase. Verified via grep search.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| `STFT` | `prepare` | `void prepare(size_t fftSize, size_t hopSize, WindowType window = WindowType::Hann, float kaiserBeta = 9.0f) noexcept` | Yes |
| `STFT` | `reset` | `void reset() noexcept` | Yes |
| `STFT` | `pushSamples` | `void pushSamples(const float* input, size_t numSamples) noexcept` | Yes |
| `STFT` | `canAnalyze` | `bool canAnalyze() const noexcept` | Yes |
| `STFT` | `analyze` | `void analyze(SpectralBuffer& output) noexcept` | Yes |
| `SpectralBuffer` | `prepare` | `void prepare(size_t fftSize) noexcept` (creates numBins = fftSize/2+1) | Yes |
| `SpectralBuffer` | `reset` | `void reset() noexcept` | Yes |
| `SpectralBuffer` | `getMagnitude` | `float getMagnitude(size_t bin) const noexcept` | Yes |
| `SpectralBuffer` | `getPhase` | `float getPhase(size_t bin) const noexcept` | Yes |
| `SpectralBuffer` | `numBins` | `size_t numBins() const noexcept` | Yes |
| `SpectralBuffer` | `isPrepared` | `bool isPrepared() const noexcept` | Yes |
| `SpectralBuffer` | `setCartesian` | `void setCartesian(size_t bin, float real, float imag) noexcept` | Yes |
| `OverlapAdd` | `synthesize` | `void synthesize(const SpectralBuffer& input) noexcept` | Yes |
| `OverlapAdd` | `samplesAvailable` | `size_t samplesAvailable() const noexcept` | Yes |
| `OverlapAdd` | `pullSamples` | `void pullSamples(float* output, size_t numSamples) noexcept` | Yes |
| `PhaseVocoderPitchShifter` | `kFFTSize` | `static constexpr std::size_t kFFTSize = 4096` | Yes |
| `PhaseVocoderPitchShifter` | `kHopSize` | `static constexpr std::size_t kHopSize = 1024` | Yes |
| `PhaseVocoderPitchShifter` | `kMaxBins` | `static constexpr std::size_t kMaxBins = kFFTSize / 2 + 1` (= 2049) | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/stft.h` - STFT and OverlapAdd classes
- [x] `dsp/include/krate/dsp/primitives/spectral_buffer.h` - SpectralBuffer class
- [x] `dsp/include/krate/dsp/processors/pitch_shift_processor.h` - PhaseVocoderPitchShifter + PitchShiftProcessor + Impl
- [x] `dsp/include/krate/dsp/systems/harmonizer_engine.h` - HarmonizerEngine class
- [x] `dsp/include/krate/dsp/primitives/spectral_utils.h` - wrapPhase function

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| `SpectralBuffer` | All internal data is `mutable` -- `const` reference still allows lazy polar/Cartesian conversion | Pass as `const SpectralBuffer&` for read access; `getMagnitude()`/`getPhase()` work through const ref |
| `SpectralBuffer` | `prepare(fftSize)` creates `numBins = fftSize/2+1` bins, NOT `fftSize` bins | Check `numBins()` returns 2049 for FFT size 4096 |
| `STFT` | `canAnalyze()` requires `samplesAvailable_ >= fftSize_` (not hopSize) for first frame | First frame needs full FFT window; subsequent frames need hopSize new samples |
| `OverlapAdd` | `pullSamples()` returns void, not size_t. Asserts `numSamples <= samplesReady_` | Always check `samplesAvailable()` before calling `pullSamples()` |
| `PitchShiftProcessor::process` | Uses sub-block parameter smoothing with `kSmoothingSubBlockSize` | The shared-analysis path bypasses this smoothing; HarmonizerEngine handles smoothing externally |
| `PhaseVocoderPitchShifter::process` | Unity pitch bypass at `abs(ratio - 1.0) < 0.0001` | The shared-analysis path does NOT auto-bypass at unity; caller must handle |
| `PhaseVocoderPitchShifter` | `kMaxBins = 4097` (max FFT 8192) but actual numBins at runtime = `kFFTSize/2+1 = 2049` | FR-008 validation uses `kFFTSize / 2 + 1` (= 2049), NOT `kMaxBins` (= 4097). Using `kMaxBins` in the validation check would silently accept spectra from a larger FFT size, causing wrong-length spectrum processing. The contract error-handling table has been updated to use `kFFTSize / 2 + 1` explicitly. |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| All new methods | They are class methods on existing Layer 2/3 classes, not standalone utilities |

**Decision**: No new Layer 0 utilities needed. This refactor adds methods to existing classes and does not create reusable standalone functions.

## SIMD Optimization Analysis

*GATE: Must complete during planning. Constitution Principle IV requires evaluating SIMD viability.*

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | YES (per-voice) | `synthPhase_[]` accumulates across frames. However, this is per-voice, not per-sample within a frame. |
| **Data parallelism width** | 4 voices | 4 independent voice processing streams with identical analysis input |
| **Branch density in inner loop** | MEDIUM | Phase locking has peak/non-peak branching. Formant preservation has conditional application. |
| **Dominant operations** | transcendental (sin/cos per bin) + memory (spectrum reads) | Each voice calls sin/cos for 2049 bins. FFT itself uses pffft (already SIMD-optimized). |
| **Current CPU budget vs expected usage** | 18% budget vs ~24% current, target ~15-17% after shared FFT | After shared FFT saves ~6%, further savings would need SIMD voice processing |

### SIMD Viability Verdict

**Verdict**: MARGINAL -- DEFER

**Reasoning**: The 4-voice width is ideal for 128-bit SIMD (4 floats). Processing all 4 voices' `processFrame()` in parallel (same analysis spectrum, different pitch ratios) could theoretically 4x the per-bin synthesis throughput. However: (1) The primary optimization (shared FFT) is algorithmic, not SIMD, and already targets the measured bottleneck. (2) Per-voice `synthPhase_[]` accumulation creates cross-frame dependencies that complicate SIMD. (3) The phase locking branch pattern differs per voice (different pitch ratios produce different peak mappings), requiring SIMD lane masking. (4) pffft already uses SIMD for the FFT itself. The marginal benefit does not justify the complexity before measuring the post-shared-FFT baseline.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Shared forward FFT (this spec) | ~25% reduction in PhaseVocoder CPU (3/4 FFTs eliminated) | LOW | YES (primary strategy) |
| SIMD across-voices in processFrame | ~10-20% additional reduction (speculative) | HIGH | DEFER to post-benchmark |
| Fast sin/cos approximation in synthesis | ~5-10% in processFrame loop | MEDIUM | DEFER to post-benchmark |

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 2 (Processors) + Layer 3 (Systems)

**Related features at same layer**:
- Spectral freeze (potential future Layer 3 system)
- Multi-voice chorus with spectral processing (potential future Layer 3 system)
- Shared pitch detection for PitchSync mode (potential future Layer 2/3 optimization)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| `processWithSharedAnalysis()` API pattern | HIGH | Spectral freeze, multi-voice chorus | Keep as method on PhaseVocoderPitchShifter |
| `pullOutputSamples()` / `outputSamplesAvailable()` | MEDIUM | Any component that drives OLA externally | Keep local; extract if 2nd consumer appears |
| "Shared STFT + per-voice synthesis" pattern | HIGH | Any multi-voice spectral processor | Document pattern; do not abstract prematurely |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep shared STFT pattern in HarmonizerEngine | Only one consumer (HarmonizerEngine). Extract to utility if a second multi-voice spectral system appears. |
| No new abstract base class for "shared-analysis voice" | Premature abstraction. The pattern is simple enough to duplicate if needed. |
| `processWithSharedAnalysis()` on existing class (not new interface) | Adding a virtual interface would add vtable overhead in the audio path. Method on concrete class is sufficient. |

### Review Trigger

After implementing **spectral freeze or multi-voice chorus**:
- [ ] Does the new feature need shared FFT analysis across multiple consumers? If so, extract the "shared STFT + per-consumer synthesis" pattern into a reusable utility.
- [ ] Does the new feature use `processWithSharedAnalysis()`? If so, the API design is validated.

## Project Structure

### Documentation (this feature)

```text
specs/065-shared-analysis-fft-refactor/
+-- plan.md              # This file
+-- research.md          # Phase 0: research findings
+-- data-model.md        # Phase 1: data model and data flow
+-- quickstart.md        # Phase 1: implementation quickstart
+-- contracts/           # Phase 1: API contracts
|   +-- phase-vocoder-pitch-shifter-api.md
|   +-- pitch-shift-processor-api.md
|   +-- harmonizer-engine-api.md
+-- tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (modified files)

```text
dsp/
+-- include/krate/dsp/
|   +-- processors/
|   |   +-- pitch_shift_processor.h   # PhaseVocoderPitchShifter + PitchShiftProcessor modifications
|   +-- systems/
|       +-- harmonizer_engine.h       # HarmonizerEngine shared-analysis integration
+-- tests/unit/
    +-- processors/
    |   +-- pitch_shift_processor_test.cpp  # New equivalence + shared-analysis tests
    +-- systems/
        +-- harmonizer_engine_test.cpp     # New shared-analysis path + benchmark tests
```

**Structure Decision**: No new files created. All modifications are to existing files in the established monorepo structure. This is a refactor, not a new feature.

## Complexity Tracking

No constitution violations. No complexity justifications needed.

## Post-Design Constitution Re-Check (PASSED)

| Principle | Status | Notes |
|-----------|--------|-------|
| II. Real-Time Safety | PASS | `processWithSharedAnalysis()` is `noexcept`, takes `const&` (no copy), uses pre-allocated OLA buffers. `sharedStft_` and `sharedAnalysisSpectrum_` allocated in `prepare()`. `pvVoiceScratch_` allocated in `prepare()`. Zero new allocations in audio path. |
| IV. SIMD | PASS | SIMD deferred per analysis. Algorithmic optimization is primary. pffft already handles SIMD for FFT. |
| VIII. Testing | PASS | Equivalence tests (SC-002, SC-003), OLA isolation (SC-007), backward compat (SC-004), benchmarks (SC-001, SC-008) all planned. Pre-refactor golden reference capture (T003a) required for SC-002. |
| IX. Layered Architecture | PASS | Layer 2 changes: `processFrame()` signature + new methods. No Layer 3 dependency introduced. Layer 3 composes Layer 2 via new public methods. |
| XV. ODR Prevention | PASS | No new types. Method names verified unique. |
