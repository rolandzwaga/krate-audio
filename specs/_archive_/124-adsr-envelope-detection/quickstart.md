# Quickstart: Innexus ADSR Envelope Detection

**Branch**: `124-adsr-envelope-detection` | **Date**: 2026-03-08

## Overview

This feature adds automatic ADSR envelope detection to Innexus's sample analysis pipeline, with 9 user-editable parameters and global envelope application to the synthesizer output.

## Implementation Order

> **Note**: The phase grouping below is a logical orientation aid only. `tasks.md` is the authoritative implementation order. Key difference: UI integration (ADSRDisplay wiring) occurs in **Phase 4 of tasks.md** alongside parameter registration — not as a final step as the sequential listing below might imply. Always follow `tasks.md` when implementing.

### Phase 1: Envelope Detector (analysis-time DSP)
- Create `EnvelopeDetector` class in `plugins/innexus/src/dsp/envelope_detector.h`
- Add `DetectedADSR` result struct
- Implement peak-finding + O(1) rolling least-squares steady-state detection
- Unit tests with synthetic amplitude contours

### Phase 2: Parameter Registration & Processor Atomics
- Add 9 parameter IDs (720-728) to `plugin_ids.h`
- Add 9 `std::atomic<float>` fields to `Processor`
- Add `Krate::DSP::ADSREnvelope adsr_` and `OnePoleSmoother adsrAmountSmoother_` to Processor
- Handle in `processParameterChanges()`
- Register 9 `RangeParameter`s in `Controller::initialize()`

### Phase 3: Processor Integration
- Wire `adsr_.gate(true)` in `handleNoteOn()`, `gate(false)` in `handleNoteOff()`
- In `process()`: compute envelope gain, apply to output
- `prepare()` / `setActive()`: call `adsr_.prepare(sampleRate)`
- `checkForNewAnalysis()`: read `detectedADSR`, set parameter atomics, notify controller

### Phase 4: Analysis Pipeline Integration
- Add `DetectedADSR detectedADSR` to `SampleAnalysis` struct
- Call `EnvelopeDetector::detect()` at end of `SampleAnalyzer::analyzeOnThread()`
- Skip detection when input source is sidechain (FR-022)

### Phase 5: Memory Slot Extension
- Add 9 ADSR fields to `Krate::DSP::MemorySlot`
- Extend capture: store current ADSR params into slot
- Extend recall: restore ADSR params from slot
- Extend morph: geometric mean for times, linear for levels/curves

### Phase 6: Evolution Engine ADSR Interpolation
- Extend `EvolutionEngine::getInterpolatedFrame()` to output interpolated ADSR values
- Use geometric mean for time params, linear for others
- Wire interpolated ADSR back to processor

### Phase 7: State Serialization v9
- Increment version to 9
- Write: 9 global ADSR floats + per-slot ADSR data (8 slots x 9 floats)
- Read: `if (version >= 9)` block with defaults for v1-v8

### Phase 8: UI Integration
- Add ADSRDisplay to `editor.uidesc` with custom-view-name
- Wire in `Controller::createCustomView()` using `setAdsrBaseParamId()` / `setCurveBaseParamId()`
- Wire playback dot via `setPlaybackStatePointers()`
- Register 9 new knobs/controls in uidesc

## Key Files to Modify

| File | Changes |
|------|---------|
| `plugins/innexus/src/plugin_ids.h` | Add 9 parameter IDs (720-728) |
| `plugins/innexus/src/processor/processor.h` | Add ADSR atomics, ADSREnvelope instance, smoother |
| `plugins/innexus/src/processor/processor.cpp` | process(), handleNoteOn/Off, checkForNewAnalysis |
| `plugins/innexus/src/processor/processor_state.cpp` | v9 serialization |
| `plugins/innexus/src/controller/controller.cpp` | Register params, createCustomView for ADSRDisplay |
| `plugins/innexus/src/dsp/sample_analysis.h` | Add DetectedADSR field |
| `plugins/innexus/src/dsp/sample_analyzer.cpp` | Call EnvelopeDetector after analysis |
| `dsp/include/krate/dsp/processors/harmonic_snapshot.h` | Extend MemorySlot with 9 fields |
| `plugins/innexus/src/dsp/evolution_engine.h` | Extend getInterpolatedFrame() for ADSR |
| `plugins/innexus/resources/editor.uidesc` | Add ADSRDisplay and knobs |

## New Files

| File | Purpose |
|------|---------|
| `plugins/innexus/src/dsp/envelope_detector.h` | EnvelopeDetector class + DetectedADSR struct |
| `plugins/innexus/tests/unit/processor/test_envelope_detector.cpp` | Unit tests for detection algorithm |
| `plugins/innexus/tests/integration/test_adsr_envelope.cpp` | Integration tests for full ADSR pipeline |

## Build & Test

```bash
# Build
"$CMAKE" --build build/windows-x64-release --config Release --target innexus_tests

# Run tests
build/windows-x64-release/bin/Release/innexus_tests.exe "EnvelopeDetector*"
build/windows-x64-release/bin/Release/innexus_tests.exe "ADSR*"

# Full test suite
build/windows-x64-release/bin/Release/innexus_tests.exe

# Pluginval
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Innexus.vst3"
```

## Critical Constraints

1. **Bit-exact bypass**: When Amount=0.0, output MUST be identical to pre-feature behavior
2. **No audio-thread allocations**: ADSREnvelope is stack-allocated, all buffers pre-allocated
3. **Hard retrigger**: New note-on during active note resets envelope immediately
4. **Monophonic**: Single shared ADSR instance, not per-voice
5. **Backward compatible**: v1-v8 states load with Amount=0.0, curves=0.0
6. **Cross-platform**: No platform-specific code
