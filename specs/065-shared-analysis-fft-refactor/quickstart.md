# Quickstart: Shared-Analysis FFT Refactor

**Date**: 2026-02-18 | **Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)

## Overview

This refactor eliminates 75% of redundant forward FFT computation in the HarmonizerEngine's PhaseVocoder mode by sharing a single forward FFT analysis across all 4 voices.

## Key Files to Modify

| File | Layer | Changes |
|------|-------|---------|
| `dsp/include/krate/dsp/processors/pitch_shift_processor.h` | L2 | Refactor `processFrame()` signature; add `processWithSharedAnalysis()`, `pullOutputSamples()`, `outputSamplesAvailable()` to PhaseVocoderPitchShifter; add shared-analysis delegation methods + FFT/hop accessors to PitchShiftProcessor |
| `dsp/include/krate/dsp/systems/harmonizer_engine.h` | L3 | Add `sharedStft_`, `sharedAnalysisSpectrum_`, `pvVoiceScratch_` members; modify `prepare()`, `reset()`, `process()` for PhaseVocoder path |
| `dsp/tests/unit/processors/pitch_shift_processor_test.cpp` | Tests | Add equivalence tests for `processWithSharedAnalysis()` vs `process()` |
| `dsp/tests/unit/systems/harmonizer_engine_test.cpp` | Tests | Add shared-analysis tests, OLA isolation tests, benchmark updates |

## Implementation Order

### Phase 1: Layer 2 -- PhaseVocoderPitchShifter

1. Refactor `processFrame()` to accept `const SpectralBuffer& analysis` and `SpectralBuffer& synthesis` parameters
2. Update `process()` to call refactored `processFrame(analysisSpectrum_, synthesisSpectrum_, pitchRatio)`
3. Verify all existing tests still pass (backward compatibility)
4. Add `processWithSharedAnalysis()`, `pullOutputSamples()`, `outputSamplesAvailable()`
5. Add equivalence tests: standard path vs shared-analysis path

### Phase 2: Layer 2 -- PitchShiftProcessor (pImpl)

1. Add `processWithSharedAnalysis()`, `pullSharedAnalysisOutput()`, `sharedAnalysisSamplesAvailable()` to public API
2. Add `getPhaseVocoderFFTSize()`, `getPhaseVocoderHopSize()` static constexpr accessors
3. Add Impl delegation methods
4. Add tests for delegation and mode-guarding

### Phase 3: Layer 3 -- HarmonizerEngine

1. Add `sharedStft_`, `sharedAnalysisSpectrum_`, `pvVoiceScratch_` members
2. Modify `prepare()` and `reset()` to initialize shared resources
3. Modify `process()` to use shared-analysis path when mode is PhaseVocoder
4. Add PhaseVocoder-mode specific tests
5. Run benchmark, verify SC-001 (<18% CPU) and SC-008 (PitchSync re-benchmark)

### Phase 4: Verification and Cleanup

1. Run full dsp_tests suite (SC-004: zero regressions)
2. Verify output equivalence (SC-002: RMS < 1e-5)
3. Code inspection for zero heap allocations (SC-005)
4. Run clang-tidy
5. Update architecture documentation

## Build and Test

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Build
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests

# Run all DSP tests
build/windows-x64-release/bin/Release/dsp_tests.exe

# Run specific test sections
build/windows-x64-release/bin/Release/dsp_tests.exe "[processors]"
build/windows-x64-release/bin/Release/dsp_tests.exe "[systems][harmonizer]"
build/windows-x64-release/bin/Release/dsp_tests.exe "[benchmark]"
```

## Critical Design Decisions

1. **processFrame accepts const SpectralBuffer&**: The shared analysis spectrum is passed by const reference, not copied. This saves ~11 MB/sec bandwidth and enforces read-only access at compile time. (FR-023)

2. **Per-voice delay moves post-pitch in PhaseVocoder mode**: Delays are 0-50ms onset delays for humanization. Moving them after OLA output is audibly transparent and enables sharing the forward FFT across voices with different delays. (R-004)

3. **Frame-at-a-time processing model**: `processWithSharedAnalysis()` processes exactly one frame per call. Output pulling is separate. This matches the frame-driven shared STFT cadence. (FR-007)

4. **All deferred optimizations stay deferred**: Shared peak detection (FR-020), shared transient detection (FR-021), and shared envelope extraction (FR-022) are NOT implemented. The clean separation (shared analysis, independent voice interpretation) is established first. Measure, then optimize.
