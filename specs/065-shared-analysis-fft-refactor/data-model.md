# Data Model: Shared-Analysis FFT Refactor

**Date**: 2026-02-18 | **Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)

## Entities

This refactor does not introduce new entities. It modifies the ownership and data flow of existing entities.

### Modified Entities

#### PhaseVocoderPitchShifter (Layer 2)

**Location**: `dsp/include/krate/dsp/processors/pitch_shift_processor.h` (lines 936-1433)

**Existing members (unchanged)**:
| Member | Type | Purpose |
|--------|------|---------|
| `stft_` | `STFT` | Internal forward FFT analysis (used by standard `process()` path only) |
| `ola_` | `OverlapAdd` | Per-voice synthesis overlap-add buffer |
| `analysisSpectrum_` | `SpectralBuffer` | Internal analysis result (used by standard `process()` path only) |
| `synthesisSpectrum_` | `SpectralBuffer` | Per-voice modified spectrum for iFFT |
| `prevPhase_[]` | `std::vector<float>` | Per-voice previous frame analysis phases |
| `synthPhase_[]` | `std::vector<float>` | Per-voice accumulated synthesis phases |
| `magnitude_[]` | `std::vector<float>` | Per-voice intermediate computation |
| `frequency_[]` | `std::vector<float>` | Per-voice intermediate computation |
| `formantPreserver_` | `FormantPreserver` | Per-voice formant analysis |
| `transientDetector_` | `SpectralTransientDetector` | Per-voice transient detection |
| Phase locking state | Various arrays | Per-voice peak detection/assignment |

**New public methods**:
| Method | Signature | Purpose |
|--------|-----------|---------|
| `processWithSharedAnalysis` | `void processWithSharedAnalysis(const SpectralBuffer& analysis, float pitchRatio) noexcept` | Process one frame using externally provided analysis spectrum |
| `pullOutputSamples` | `std::size_t pullOutputSamples(float* output, std::size_t maxSamples) noexcept` | Pull processed samples from OLA buffer |
| `outputSamplesAvailable` | `std::size_t outputSamplesAvailable() const noexcept` | Query available output samples |

**Modified methods**:
| Method | Change |
|--------|--------|
| `processFrame` | Signature changes from `void processFrame(float pitchRatio)` to `void processFrame(const SpectralBuffer& analysis, SpectralBuffer& synthesis, float pitchRatio) noexcept`. Reads from `analysis` parameter instead of `analysisSpectrum_` member. Writes to `synthesis` parameter instead of `synthesisSpectrum_` member. |

#### PitchShiftProcessor (Layer 2 -- pImpl wrapper)

**Location**: `dsp/include/krate/dsp/processors/pitch_shift_processor.h` (lines 98-295)

**New public methods**:
| Method | Signature | Purpose |
|--------|-----------|---------|
| `processWithSharedAnalysis` | `void processWithSharedAnalysis(const SpectralBuffer& analysis, float pitchRatio) noexcept` | Delegate to PhaseVocoderPitchShifter or zero-fill for other modes |
| `pullSharedAnalysisOutput` | `std::size_t pullSharedAnalysisOutput(float* output, std::size_t maxSamples) noexcept` | Pull OLA output from PhaseVocoder |
| `sharedAnalysisSamplesAvailable` | `std::size_t sharedAnalysisSamplesAvailable() const noexcept` | Query available output |
| `getPhaseVocoderFFTSize` | `static constexpr std::size_t getPhaseVocoderFFTSize() noexcept` | Return FFT size (4096) |
| `getPhaseVocoderHopSize` | `static constexpr std::size_t getPhaseVocoderHopSize() noexcept` | Return hop size (1024) |

#### HarmonizerEngine (Layer 3)

**Location**: `dsp/include/krate/dsp/systems/harmonizer_engine.h` (lines 66-483)

**New members**:
| Member | Type | Purpose | Ownership |
|--------|------|---------|-----------|
| `sharedStft_` | `STFT` | Shared forward FFT analysis instance | Engine-owned, allocated in `prepare()` |
| `sharedAnalysisSpectrum_` | `SpectralBuffer` | Shared analysis result buffer | Engine-owned, allocated in `prepare()` |
| `pvVoiceScratch_` | `std::vector<float>` | Per-voice output scratch buffer for PhaseVocoder path | Engine-owned, allocated in `prepare()` |

**Modified methods**:
| Method | Change |
|--------|--------|
| `prepare()` | Additionally prepares `sharedStft_` and `sharedAnalysisSpectrum_` with PhaseVocoder FFT/hop parameters |
| `reset()` | Additionally resets `sharedStft_` and `sharedAnalysisSpectrum_` |
| `process()` | Adds PhaseVocoder-specific path that uses shared analysis instead of per-voice `process()` |

## Data Flow Diagrams

### Pre-Refactor: Per-Voice Independent FFT

```
HarmonizerEngine::process(input, outL, outR, N)
  |
  for each voice v:
    input -> delay[v] -> delayScratch_
    delayScratch_ -> PitchShiftProcessor::process()
                        |
                        -> PhaseVocoderPitchShifter::process()
                            |
                            stft_.pushSamples(input, N)
                            while canAnalyze():
                              stft_.analyze(analysisSpectrum_)   <-- FFT #v
                              processFrame(pitchRatio)
                              ola_.synthesize(synthesisSpectrum_)
                            ola_.pullSamples(output, N)
                        |
                     -> voiceScratch_
    voiceScratch_ * level * pan -> outputL[], outputR[]
```

### Post-Refactor: Shared Analysis FFT

```
HarmonizerEngine::process(input, outL, outR, N)
  |
  if PhaseVocoder mode:
    input -> sharedStft_.pushSamples(input, N)
    while sharedStft_.canAnalyze():
      sharedStft_.analyze(sharedAnalysisSpectrum_)   <-- FFT x1 (shared)
      |
      for each active voice v:
        sharedAnalysisSpectrum_ -> voice[v].processWithSharedAnalysis(spectrum, ratio)
                                     |
                                     processFrame(spectrum, synthesis, ratio)  <-- synthesis only
                                     ola_.synthesize(synthesis)
    |
    for each active voice v:
      voice[v].pullSharedAnalysisOutput(pvVoiceScratch_, N)
      pvVoiceScratch_ -> delay[v] -> voiceScratch_
      voiceScratch_ * level * pan -> outputL[], outputR[]
  |
  else (Simple/Granular/PitchSync):
    [unchanged per-voice process() path]
```

## Shared vs Per-Voice Resource Map

| Resource | Shared/Per-Voice | Owner | Lifetime |
|----------|-----------------|-------|----------|
| `sharedStft_` | Shared | HarmonizerEngine | Engine lifetime |
| `sharedAnalysisSpectrum_` | Shared (read-only after analyze) | HarmonizerEngine | Engine lifetime, valid for one frame |
| `ola_` | Per-voice | PhaseVocoderPitchShifter | Voice lifetime |
| `synthesisSpectrum_` | Per-voice | PhaseVocoderPitchShifter | Voice lifetime |
| `prevPhase_[]` | Per-voice | PhaseVocoderPitchShifter | Voice lifetime |
| `synthPhase_[]` | Per-voice | PhaseVocoderPitchShifter | Voice lifetime |
| `magnitude_[]` | Per-voice | PhaseVocoderPitchShifter | Voice lifetime |
| `frequency_[]` | Per-voice | PhaseVocoderPitchShifter | Voice lifetime |
| `formantPreserver_` | Per-voice | PhaseVocoderPitchShifter | Voice lifetime |
| `transientDetector_` | Per-voice | PhaseVocoderPitchShifter | Voice lifetime |
| Phase locking arrays | Per-voice | PhaseVocoderPitchShifter | Voice lifetime |

## Validation Rules

1. **Spectrum size match**: `sharedAnalysisSpectrum_.numBins()` MUST equal `PhaseVocoderPitchShifter::kFFTSize / 2 + 1` (= 2049). Assertion in debug, no-op + zero-fill in release.
2. **Const-correctness**: Analysis spectrum passed as `const SpectralBuffer&`. Compile-time enforcement of read-only access.
3. **No spectrum retention**: Voices MUST NOT store pointers/references to the shared spectrum beyond a single `processFrame()` call (FR-024).
4. **OLA independence**: Each voice's OverlapAdd buffer is independent. No shared OLA state.

## State Transitions

### HarmonizerEngine Mode Transitions

```
                setPitchShiftMode()
Unprepared ----prepare()----> Ready(Any mode)
                                |
                    setPitchShiftMode(PhaseVocoder)
                                |
                         Ready(PhaseVocoder)
                         - sharedStft_ active
                         - shared analysis path in process()
                                |
                    setPitchShiftMode(other)
                                |
                         Ready(Non-PhaseVocoder)
                         - sharedStft_ idle
                         - per-voice process() path
```

Mode switching is instantaneous. The shared STFT is always prepared but only used in PhaseVocoder mode. Switching modes does not require re-preparation.
