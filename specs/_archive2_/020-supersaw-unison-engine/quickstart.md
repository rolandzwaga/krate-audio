# Quickstart: Supersaw / Unison Engine

**Feature**: 020-supersaw-unison-engine | **Date**: 2026-02-04

---

## Overview

The `UnisonEngine` is a Layer 3 DSP system that composes up to 16 `PolyBlepOscillator` instances into a multi-voice detuned oscillator with stereo spread. It replicates the iconic Roland JP-8000 supersaw timbre using a non-linear detune curve (Adam Szabo analysis), constant-power stereo panning, and equal-power center/outer voice blend.

## Files to Create

| File | Purpose |
|------|---------|
| `dsp/include/krate/dsp/systems/unison_engine.h` | Header-only implementation |
| `dsp/tests/unit/systems/unison_engine_test.cpp` | Catch2 test suite |

## Files to Modify

| File | Change |
|------|--------|
| `dsp/tests/CMakeLists.txt` | Add test source to `dsp_tests` target + `-fno-fast-math` list |
| `specs/_architecture_/layer-3-systems.md` | Add UnisonEngine documentation entry |

## Build & Test Commands

```bash
# Configure (if not already done)
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --preset windows-x64-release

# Build DSP tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests

# Run all DSP tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe

# Run only UnisonEngine tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[UnisonEngine]"

# Run specific test sections
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[UnisonEngine][US1]"
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[UnisonEngine][US2]"
```

## Basic Usage

```cpp
#include <krate/dsp/systems/unison_engine.h>

using namespace Krate::DSP;

// Create and initialize
UnisonEngine engine;
engine.prepare(44100.0);

// Configure for classic 7-voice supersaw
engine.setNumVoices(7);
engine.setWaveform(OscWaveform::Sawtooth);
engine.setFrequency(440.0f);
engine.setDetune(0.5f);
engine.setStereoSpread(0.8f);
engine.setBlend(0.5f);

// Per-sample processing
StereoOutput out = engine.process();
// out.left and out.right are gain-compensated, sanitized stereo samples

// Block processing (more efficient, bit-identical to per-sample)
std::array<float, 512> left{}, right{};
engine.processBlock(left.data(), right.data(), 512);
```

## Implementation Order (Test-First)

### Phase 1: Core Engine (P1)
1. Write test: `UnisonEngine` with 1 voice produces output equivalent to single `PolyBlepOscillator`
2. Implement: `StereoOutput` struct, `UnisonEngine` skeleton with `prepare()`, `reset()`, `process()`, `processBlock()`
3. Write test: 7-voice detune produces multiple frequency peaks in FFT
4. Implement: Non-linear detune curve, `setDetune()`, voice frequency computation
5. Write test: Gain compensation keeps output within [-2.0, 2.0] for all voice counts
6. Implement: `1/sqrt(N)` gain compensation, output sanitization

### Phase 2: Stereo & Blend (P2)
7. Write test: Spread 0.0 produces identical L/R (mono)
8. Implement: Constant-power pan law, `setStereoSpread()`
9. Write test: Blend 0.0 has dominant center, blend 1.0 has only outer voices
10. Implement: Equal-power crossfade blend, `setBlend()`

### Phase 3: Polish (P3)
11. Write test: Random phases produce complex waveform from first sample
12. Implement: Deterministic phase initialization with Xorshift32
13. Write test: All 5 waveforms produce valid output
14. Implement: `setWaveform()` delegation to all oscillators

### Phase 4: Verification
15. Run full test suite, verify all SC criteria
16. Performance measurement (SC-012: < 200 cycles/sample for 7 voices)
17. Memory size verification (SC-013: < 2048 bytes)

## Key Design Decisions

| Decision | Rationale |
|----------|-----------|
| Header-only implementation | Follows codebase convention for DSP components |
| Pre-allocated 16 oscillators | Zero heap allocation, voice count changes are free |
| Power curve exponent 1.7 | Best match for JP-8000 measured detune distribution |
| Fixed 1/sqrt(N) gain | Predictable headroom, blend crossfade handles power consistency |
| Accept Nyquist aliasing | Preserves symmetric stereo, PolyBLEP handles anti-aliasing |
| Deterministic phase RNG | DAW offline rendering consistency (bit-identical on reset) |

## Test Tags

| Tag | Coverage |
|-----|----------|
| `[UnisonEngine]` | All tests |
| `[UnisonEngine][US1]` | Multi-voice detuned oscillator (P1) |
| `[UnisonEngine][US2]` | Stereo spread panning (P2) |
| `[UnisonEngine][US3]` | Blend control (P2) |
| `[UnisonEngine][US4]` | Random initial phase (P3) |
| `[UnisonEngine][US5]` | Waveform selection (P3) |
| `[UnisonEngine][edge]` | Edge cases |
| `[UnisonEngine][perf]` | Performance measurement |
| `[UnisonEngine][robustness]` | NaN/Inf/denormal handling |
