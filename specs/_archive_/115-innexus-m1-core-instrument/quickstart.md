# Quickstart: Innexus Milestone 1 -- Core Playable Instrument

**Date**: 2026-03-03 | **Branch**: `115-innexus-m1-core-instrument`

## Implementation Order

The implementation follows 9 phases with strict dependency ordering. Each phase builds on the previous ones.

### Phase Dependency Chain

```
Phase 1: Plugin Scaffold (partially done)
  |
  v
Phase 2: Pre-Processing Pipeline
  |
  +---> Phase 3: YIN F0 Tracking
  |
  +---> Phase 4: Dual-Window STFT (needs BlackmanHarris window)
         |
         v
       Phase 5: Partial Detection & Tracking (needs Phase 3 + Phase 4)
         |
         v
       Phase 6: Harmonic Model Builder (needs Phase 5)
         |
         v
       Phase 7: Harmonic Oscillator Bank
         |
         v
       Phase 8: Sample Mode Integration (needs Phases 2-7)
         |
         v
       Phase 9: MIDI Integration & Playback (needs Phase 7 + Phase 8)
```

### Critical Path

The longest dependency chain runs through Phases 1 -> 4 -> 5 -> 6 -> 7 -> 8 -> 9. Phases 2 and 3 can be developed in parallel with Phase 4, but must complete before Phase 5 (which needs F0 from Phase 3 and STFT from Phase 4).

## Getting Started

### Prerequisites

1. **Build the existing plugin scaffold**:
   ```bash
   CMAKE="/c/Program Files/CMake/bin/cmake.exe"
   "$CMAKE" --preset windows-x64-release
   "$CMAKE" --build build/windows-x64-release --config Release --target Innexus
   ```

2. **Verify pluginval passes** (empty processor):
   ```bash
   tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Innexus.vst3"
   ```

3. **Run existing DSP tests** to establish baseline:
   ```bash
   "$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests
   build/windows-x64-release/bin/Release/dsp_tests.exe
   ```

### Development Workflow (per phase)

1. Write failing tests for the phase's components
2. Implement to make tests pass
3. Fix all compiler warnings
4. Verify all tests pass (not just new ones)
5. Run pluginval if plugin code changed
6. Run clang-tidy
7. Commit

### Key Files to Read First

Before implementing, read these existing files to understand the patterns:

| File | Why Read It |
|------|-------------|
| `dsp/include/krate/dsp/processors/particle_oscillator.h` | MCF + SoA pattern for oscillator bank |
| `dsp/include/krate/dsp/primitives/stft.h` | STFT API for dual-window analysis |
| `dsp/include/krate/dsp/primitives/fft_autocorrelation.h` | FFT-accelerated correlation pattern for YIN |
| `dsp/include/krate/dsp/primitives/dc_blocker.h` | DCBlocker2 for pre-processing |
| `dsp/include/krate/dsp/primitives/biquad.h` | Biquad HPF for pre-processing |
| `dsp/include/krate/dsp/processors/envelope_follower.h` | EnvelopeFollower for transient detection |
| `dsp/include/krate/dsp/core/window_functions.h` | Window functions (will add BlackmanHarris) |
| `dsp/include/krate/dsp/primitives/spectral_utils.h` | Spectral utilities (will add parabolicInterpolation) |
| `dsp/include/krate/dsp/core/midi_utils.h` | MIDI note/velocity conversion |
| `plugins/innexus/src/processor/processor.cpp` | Current (empty) processor implementation |
| `plugins/ruinae/CMakeLists.txt` | Build configuration pattern to follow |

### Performance Targets to Keep in Mind

| Metric | Target | How to Verify |
|--------|--------|---------------|
| Oscillator bank CPU | < 0.5% at 44.1kHz stereo, 48 partials | Benchmark test (SC-002) |
| Full plugin CPU | < 5% at 44.1kHz stereo | Benchmark test (SC-004) |
| Analysis speed | < 10s for 10s mono file | Timed test (SC-005) |
| YIN pitch error | < 2% gross error rate | Pitch accuracy test (SC-003) |
| Note-on latency | < 1 buffer duration | Timing test (SC-007) |
| Anti-aliasing | No energy above Nyquist | Spectral analysis test (SC-006) |
| Audio thread allocs | 0 | ASan + code audit (SC-010) |

### Common Pitfalls

1. **Do not allocate on audio thread**: All DSP buffers must be pre-allocated in prepare(). Use std::array with fixed sizes, not std::vector.

2. **MCF oscillator init**: When initializing a new oscillator, set sinState = sin(2*pi*phase) and cosState = cos(2*pi*phase). Using zero-init produces silence.

3. **FFT output format**: pffft ordered output is [DC, Nyquist, Re(1), Im(1), ...]. The FFT class handles conversion to Complex[] format.

4. **WindowType enum**: No BlackmanHarris exists yet. Must add it before implementing Phase 4.

5. **Atomic pointer swap**: Use memory_order_release for publish, memory_order_acquire for read. Never use memory_order_relaxed.

6. **dr_wav**: Define DR_WAV_IMPLEMENTATION in exactly ONE .cpp file. Multiple definitions cause linker errors.

7. **Thread safety of SampleAnalysis**: The object is immutable after publication. Never modify it from any thread after the atomic pointer swap.
