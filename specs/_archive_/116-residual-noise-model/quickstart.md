# Quickstart: Innexus Milestone 2 -- Residual/Noise Model

## Overview

This feature adds SMS (Sinusoidal + Noise Modeling) decomposition to the Innexus additive synthesis engine. After implementation, analyzed samples will produce both harmonic (oscillator bank) and stochastic (shaped noise) output, resulting in a more natural and complete timbre.

## New Files

| File | Type | Description |
|------|------|-------------|
| `dsp/include/krate/dsp/processors/residual_types.h` | Header | `ResidualFrame` struct, band constants, band center/edge accessors |
| `dsp/include/krate/dsp/processors/residual_analyzer.h` | Header | `ResidualAnalyzer` class -- offline spectral subtraction analysis |
| `dsp/include/krate/dsp/processors/residual_synthesizer.h` | Header | `ResidualSynthesizer` class -- real-time noise resynthesis |
| `dsp/tests/unit/processors/residual_types_tests.cpp` | Test | Unit tests for ResidualFrame and band frequency helpers |
| `dsp/tests/unit/processors/residual_analyzer_tests.cpp` | Test | Unit tests for ResidualAnalyzer |
| `dsp/tests/unit/processors/residual_synthesizer_tests.cpp` | Test | Unit tests for ResidualSynthesizer |
| `plugins/innexus/tests/unit/processor/residual_integration_tests.cpp` | Test | Integration tests for residual in processor pipeline |

## Modified Files

| File | Change |
|------|--------|
| `plugins/innexus/src/plugin_ids.h` | Add `kHarmonicLevelId`, `kResidualLevelId`, `kResidualBrightnessId`, `kTransientEmphasisId` |
| `plugins/innexus/src/parameters/innexus_params.h` | Add residual parameter handling (registration, change handling, save/load) |
| `plugins/innexus/src/dsp/sample_analysis.h` | Add `residualFrames` vector, `getResidualFrame()`, `analysisFFTSize`, `analysisHopSize` |
| `plugins/innexus/src/dsp/sample_analyzer.h` | Add `ResidualAnalyzer residualAnalyzer_` member, include `residual_types.h` |
| `plugins/innexus/src/dsp/sample_analyzer.cpp` | Add residual analysis after harmonic analysis in `analyzeOnThread()` |
| `plugins/innexus/src/processor/processor.h` | Add `ResidualSynthesizer`, smoothers, atomic params, new member fields |
| `plugins/innexus/src/processor/processor.cpp` | Sum harmonic + residual output, state versioning (v1 -> v2) |
| `plugins/innexus/src/controller/controller.cpp` | Register new parameters, extend `setComponentState()` |
| `dsp/CMakeLists.txt` | No change needed -- DSP components are header-only |
| `dsp/tests/CMakeLists.txt` | Add `residual_types_tests.cpp`, `residual_analyzer_tests.cpp`, `residual_synthesizer_tests.cpp` to `dsp_tests` target |
| `plugins/innexus/tests/CMakeLists.txt` | Add `residual_integration_tests.cpp` to `innexus_tests` target |

## Build & Test

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Configure
"$CMAKE" --preset windows-x64-release

# Build DSP tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests

# Run DSP tests (includes new residual tests)
build/windows-x64-release/bin/Release/dsp_tests.exe

# Build Innexus plugin tests
"$CMAKE" --build build/windows-x64-release --config Release --target innexus_tests

# Run Innexus tests (includes new integration tests)
build/windows-x64-release/bin/Release/innexus_tests.exe

# Build Innexus plugin
"$CMAKE" --build build/windows-x64-release --config Release --target Innexus

# Run pluginval
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Innexus.vst3"
```

## Key Design Decisions

1. **ResidualFrame lives in KrateDSP Layer 2** (`residual_types.h`) alongside `harmonic_types.h`, not in plugin-local code. This enables reuse by M3-M5 milestones.

2. **Harmonic subtraction uses direct sinusoidal resynthesis from tracked Partial data**, not the streaming HarmonicOscillatorBank. This gives exact phase alignment per frame for tight cancellation.

3. **ResidualSynthesizer uses OverlapAdd with synthesis window** (`applySynthesisWindow = true`). The Hann window prevents boundary discontinuities since IFFT output of random-phase noise lacks the smooth taper of windowed analysis frames.

4. **Harmonic/Residual Mix is two independent gains** (not a crossfade). Both default to 1.0 (unity). Range 0.0-2.0 allows boosting either component above the analyzed level.

5. **State persistence bumps version 1 to 2**. Version 1 states (M1) load cleanly with empty residual frames. Version 2 appends new parameters and serialized ResidualFrame data.

6. **PRNG seed is fixed constant 12345**, reset on every `prepare()`, ensuring deterministic output for approval tests and reproducible playback.

## Architecture Diagram

```
Analysis Pipeline (Background Thread):
  +-----------------+
  | Original Audio  |
  +--------+--------+
           |
  +--------v--------+    +------------------+
  | Pre-Processing   |--->| YIN F0 Detection |
  +-----------------+    +--------+---------+
                                  |
  +-------------------------------v---------+
  | Dual STFT -> PartialTracker -> ModelBuilder |
  +------------------+----------------------+
                     |
           +---------v---------+
           | HarmonicFrame     |
           | (tracked partials)|
           +---------+---------+
                     |
     +---------------v---------------+
     | ResidualAnalyzer (NEW)        |
     | 1. Resynthesize harmonics     |
     |    from tracked partials      |
     | 2. residual = original -      |
     |    harmonics                  |
     | 3. Hann window + FFT of       |
     |    residual (manual, not STFT)|
     | 4. Extract 16-band envelope   |
     | 5. Compute total energy       |
     | 6. Detect transients          |
     +---------------+---------------+
                     |
           +---------v---------+
           | ResidualFrame     |
           | (spectral env,    |
           |  energy, transient)|
           +-------------------+

Synthesis Pipeline (Audio Thread):
  +-------------------+     +---------------------+
  | HarmonicFrame[i]  |     | ResidualFrame[i]    |
  +--------+----------+     +----------+----------+
           |                           |
  +--------v----------+     +----------v----------+
  | HarmonicOscillator|     | ResidualSynthesizer |
  | Bank (existing)   |     | (NEW)               |
  +--------+----------+     +----------+----------+
           |                           |
           | * harmonicLevel           | * residualLevel
           |                           |
           +----------+    +-----------+
                      |    |
                      v    v
                   +--+----+--+
                   |  SUM     |
                   +----+-----+
                        |
                   +----v-----+
                   | Output   |
                   +----------+
```

## Implementation Order

The implementation should proceed in this order to maintain testability at each step:

1. **ResidualFrame data structure** -- standalone, no dependencies beyond stdlib
2. **ResidualAnalyzer** -- depends on FFT, STFT, SpectralTransientDetector
3. **ResidualSynthesizer** -- depends on FFT, OverlapAdd, SpectralBuffer, Xorshift32
4. **SampleAnalysis extension** -- add residualFrames vector
5. **SampleAnalyzer extension** -- integrate ResidualAnalyzer into analysis pipeline
6. **Parameter IDs and registration** -- plugin_ids.h, innexus_params.h, controller
7. **Processor integration** -- ResidualSynthesizer + mixing + smoothing
8. **State persistence** -- version 2 format with residual data
9. **Pluginval validation** -- verify at strictness level 5
