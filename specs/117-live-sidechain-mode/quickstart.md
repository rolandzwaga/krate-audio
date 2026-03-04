# Quickstart: Live Sidechain Mode Implementation

**Feature Branch**: `117-live-sidechain-mode`
**Date**: 2026-03-04

## Overview

Add real-time continuous analysis from a sidechain audio input to the Innexus harmonic analysis/synthesis plugin. This enables a performer to route live audio (voice, guitar, synth) into the sidechain and play MIDI keys that inherit the live source's timbral character.

## Architecture Summary

```
Sidechain Audio Bus (stereo, downmixed to mono)
        |
        v
  LiveAnalysisPipeline
    |-- PreProcessingPipeline (existing, reused)
    |-- YinPitchDetector (existing, reused)
    |-- STFT short window (existing, reused)
    |-- STFT long window (existing, optional per latency mode)
    |-- PartialTracker (existing, reused)
    |-- HarmonicModelBuilder (existing, reused)
    |-- SpectralCoringEstimator (NEW)
        |
        v
  HarmonicFrame + ResidualFrame
        |
        v
  Processor (selects sample OR live frames)
    |-- HarmonicOscillatorBank (existing, unchanged)
    |-- ResidualSynthesizer (existing, unchanged)
        |
        v
  Audio Output
```

## New Files to Create

| File | Type | Description |
|------|------|-------------|
| `dsp/include/krate/dsp/processors/spectral_coring_estimator.h` | Header-only | Spectral coring residual estimator (Layer 2) |
| `dsp/tests/unit/processors/spectral_coring_estimator_tests.cpp` | Test | Unit tests for SpectralCoringEstimator |
| `plugins/innexus/src/dsp/live_analysis_pipeline.h` | Header | LiveAnalysisPipeline declaration |
| `plugins/innexus/src/dsp/live_analysis_pipeline.cpp` | Source | LiveAnalysisPipeline implementation |
| `plugins/innexus/tests/unit/processor/live_analysis_pipeline_tests.cpp` | Test | Unit tests for LiveAnalysisPipeline |
| `plugins/innexus/tests/unit/processor/sidechain_integration_tests.cpp` | Test | Integration tests for sidechain mode |

## Existing Files to Modify

| File | Changes |
|------|---------|
| `plugins/innexus/src/plugin_ids.h` | Add InputSource enum, LatencyMode enum, kInputSourceId (500), kLatencyModeId (501) |
| `plugins/innexus/src/processor/processor.h` | Add sidechain fields, LiveAnalysisPipeline member, crossfade state |
| `plugins/innexus/src/processor/processor.cpp` | Add sidechain bus in initialize(), update process() flow, state v3 |
| `plugins/innexus/src/controller/controller.cpp` | Register new parameters, state v3 loading |
| `plugins/innexus/src/parameters/innexus_params.h` | Add input source and latency mode to param handling |
| `plugins/innexus/src/dsp/dual_stft_config.h` | Add low-latency window config constants |
| `plugins/innexus/CMakeLists.txt` | Add new source files |
| `dsp/CMakeLists.txt` | Add spectral coring test |

## Implementation Order

### Phase 1: SpectralCoringEstimator (DSP Layer 2)
1. Write tests for spectral coring residual estimation
2. Implement SpectralCoringEstimator in shared DSP library
3. Verify it produces valid ResidualFrame output

### Phase 2: Sidechain Bus Registration (VST3)
1. Write pluginval test expectation
2. Add sidechain bus in Processor::initialize()
3. Update setBusArrangements()
4. Verify pluginval passes at strictness 5

### Phase 3: New Parameters
1. Add parameter IDs to plugin_ids.h
2. Register parameters in controller
3. Add atomic fields and parameter handling in processor
4. Add state persistence (version 3)
5. Test save/load round-trip

### Phase 4: LiveAnalysisPipeline
1. Write tests for pipeline with known input signals
2. Implement LiveAnalysisPipeline composing existing DSP components
3. Test latency mode switching
4. Test frame output format compatibility

### Phase 5: Processor Integration
1. Add sidechain audio routing (downmix, feed to pipeline)
2. Add source selection logic (sample vs sidechain frames)
3. Add 20ms source crossfade
4. Integrate live residual frames with ResidualSynthesizer
5. Test end-to-end with MIDI + sidechain audio

### Phase 6: Verification
1. Measure analysis-to-synthesis latency (SC-001)
2. Test 40 Hz detection in high-precision mode (SC-002)
3. Profile CPU usage (SC-003, SC-004)
4. Test source switching crossfade (SC-005)
5. Test confidence-gated freeze (SC-006)
6. Test residual output level (SC-007)
7. Run pluginval at strictness 5 (SC-008)

## Key Patterns to Follow

### Reuse existing pipeline components
All analysis components (PreProcessingPipeline, YinPitchDetector, STFT, PartialTracker, HarmonicModelBuilder) already exist and are real-time safe. The LiveAnalysisPipeline simply composes them with the same configuration as SampleAnalyzer but feeds them incrementally.

### Pre-allocate all buffers
Constitution Principle II: All buffers allocated in `prepare()` or `setActive(true)`. The sidechain downmix buffer, YIN circular buffer, and all STFT/spectral buffers must be pre-allocated.

### Follow existing crossfade pattern
The 20ms source crossfade follows the identical pattern as the existing anti-click voice steal crossfade (processor.cpp lines 326-334). Copy the linear ramp approach.

### State version backward compatibility
Version 3 adds data after version 2 data. Older states (v1, v2) load with defaults for new parameters.

## Build and Test

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Build
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests
"$CMAKE" --build build/windows-x64-release --config Release --target innexus_tests

# Test
build/windows-x64-release/bin/Release/dsp_tests.exe "[spectral-coring]"
build/windows-x64-release/bin/Release/innexus_tests.exe

# Pluginval
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Innexus.vst3"
```
