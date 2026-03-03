# Quickstart: ADSR Envelope Generator

**Feature**: 032-adsr-envelope-generator | **Date**: 2026-02-06

---

## Overview

Layer 1 DSP primitive that generates time-varying amplitude envelopes with five states (Idle, Attack, Decay, Sustain, Release). Uses the EarLevel Engineering one-pole iterative approach for efficient per-sample computation (1 multiply + 1 add).

## Files to Create

| File | Purpose |
|------|---------|
| `dsp/include/krate/dsp/primitives/adsr_envelope.h` | Header-only implementation |
| `dsp/tests/unit/primitives/adsr_envelope_test.cpp` | Catch2 unit tests |

## Files to Modify

| File | Change |
|------|--------|
| `dsp/CMakeLists.txt` | Add `adsr_envelope.h` to `KRATE_DSP_PRIMITIVES_HEADERS` |
| `dsp/tests/CMakeLists.txt` | Add test file to `dsp_tests` executable and `-fno-fast-math` list |
| `dsp/lint_all_headers.cpp` | Add `#include <krate/dsp/primitives/adsr_envelope.h>` |
| `specs/_architecture_/layer-1-primitives.md` | Add ADSREnvelope section |

## Dependencies (Layer 0 Only)

```cpp
#include <krate/dsp/core/db_utils.h>   // isNaN(), flushDenormal()
#include <algorithm>                     // std::clamp
#include <cmath>                         // std::exp, std::log
#include <cstddef>                       // size_t
#include <cstdint>                       // uint8_t
```

## Basic Usage

```cpp
#include <krate/dsp/primitives/adsr_envelope.h>

using namespace Krate::DSP;

// Create and configure
ADSREnvelope env;
env.prepare(44100.0f);
env.setAttack(10.0f);    // 10ms attack
env.setDecay(50.0f);     // 50ms decay
env.setSustain(0.5f);    // 50% sustain level
env.setRelease(100.0f);  // 100ms release

// Note on
env.gate(true);

// Process samples
for (int i = 0; i < numSamples; ++i) {
    float amplitude = env.process();
    output[i] = oscillator.process() * amplitude;
}

// Note off
env.gate(false);

// Continue processing until envelope completes
while (env.isActive()) {
    float amplitude = env.process();
    output[i++] = oscillator.process() * amplitude;
}
```

## Curve Shapes

```cpp
env.setAttackCurve(EnvCurve::Logarithmic);   // Slow start, fast finish
env.setDecayCurve(EnvCurve::Exponential);     // Fast initial drop (default)
env.setReleaseCurve(EnvCurve::Linear);        // Constant rate release
```

## Velocity Scaling

```cpp
env.setVelocityScaling(true);
env.setVelocity(0.7f);  // 70% velocity = 0.7 peak level
env.gate(true);
// Attack now targets 0.7 instead of 1.0
```

## Retrigger Modes

```cpp
// Hard retrigger (default) - restarts attack from current level
env.setRetriggerMode(RetriggerMode::Hard);

// Legato mode - continues from current stage/level
env.setRetriggerMode(RetriggerMode::Legato);
```

## Block Processing

```cpp
float buffer[512];
env.processBlock(buffer, 512);
// Identical to calling env.process() 512 times
```

## Key Implementation Notes

1. **One-pole formula**: `output = base + output * coef` -- single multiply-add per sample
2. **Coefficient calculation**: `coef = exp(-log((1+targetRatio)/targetRatio) / rateSamples)`
3. **Stage transitions**: Threshold-based (not sample-counting)
4. **Sustain smoothing**: 5ms one-pole smoothing for sustain level changes
5. **Denormal-free**: One-pole overshoot/undershoot targets prevent denormals
6. **Real-time safe**: No allocations, no locks, no exceptions, all methods noexcept

## Test Strategy

Tests are organized by user story priority:

1. **P1 - Basic ADSR cycle**: Gate on/off, stage transitions, timing accuracy
2. **P2 - Curve shapes**: Exponential/Linear/Logarithmic verification
3. **P3 - Retrigger modes**: Hard retrigger and legato behavior
4. **P4 - Velocity scaling**: Peak level scaling
5. **P5 - Real-time parameter changes**: Mid-stage parameter modification
6. **Edge cases**: Min/max times, zero sustain, immediate gate-off, reset during active
7. **Performance**: CPU benchmark (target: < 0.01% at 44100Hz)
8. **Multi-sample-rate**: Verify at 44100, 48000, 88200, 96000, 176400, 192000 Hz
