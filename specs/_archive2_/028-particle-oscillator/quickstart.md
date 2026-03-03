# Quickstart: Particle / Swarm Oscillator

**Feature Branch**: `028-particle-oscillator`
**Date**: 2026-02-06

---

## Overview

The ParticleOscillator is a Layer 2 processor in KrateDSP that generates complex textural timbres by managing a pool of up to 64 lightweight sine oscillators ("particles"), each with individual frequency offset, drift, lifetime, and envelope shaping.

## File Locations

| Artifact | Path |
|----------|------|
| Header | `dsp/include/krate/dsp/processors/particle_oscillator.h` |
| Test file | `dsp/tests/unit/processors/particle_oscillator_test.cpp` |
| CMake registration | `dsp/tests/CMakeLists.txt` (add test file to `dsp_tests` target) |
| Architecture update | `specs/_architecture_/layer-2-processors.md` |

## Dependencies (Layer 0 only)

```cpp
#include <krate/dsp/core/random.h>           // Xorshift32
#include <krate/dsp/core/grain_envelope.h>   // GrainEnvelope, GrainEnvelopeType
#include <krate/dsp/core/pitch_utils.h>      // semitonesToRatio()
#include <krate/dsp/core/math_constants.h>   // kTwoPi
#include <krate/dsp/core/db_utils.h>         // detail::isNaN, detail::isInf, detail::flushDenormal
```

No Layer 1 dependencies required (drift uses inline one-pole, not OnePoleSmoother).

## Build and Test

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Configure
"$CMAKE" --preset windows-x64-release

# Build DSP tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests

# Run particle oscillator tests only
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[ParticleOscillator]"

# Run all DSP tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe
```

## Implementation Order

### Phase 1: Core Structure and Basic Output (FR-001 to FR-004, FR-010, FR-011, FR-015 to FR-017, FR-019, FR-021)

1. Write test: single particle (density=1, scatter=0) produces sine at center frequency
2. Implement: Particle struct, ParticleOscillator skeleton, prepare(), process(), processBlock()
3. Implement: Envelope table precomputation, phase accumulator, normalization, output sanitization
4. Verify: SC-001 (THD < 1%), SC-007 (timing accuracy)

### Phase 2: Population and Spawn Control (FR-005 to FR-009, FR-022)

1. Write test: multiple particles with scatter produce spectral spread
2. Write test: Regular mode produces evenly spaced onsets
3. Write test: Random mode produces stochastic onsets
4. Write test: Burst mode spawns all on triggerBurst()
5. Implement: Scatter, density, spawn scheduler (Regular/Random/Burst)
6. Verify: SC-002 (amplitude bounded), SC-004 (occupancy > 90%), SC-006 (no clicks)

### Phase 3: Drift and Envelope Types (FR-012 to FR-014)

1. Write test: drift=0 produces constant frequency; drift=1 produces wandering
2. Write test: envelope type switching works
3. Implement: Per-particle drift (one-pole filtered noise), envelope type switching
4. Verify: SC-005 (stochastic behavior), SC-003 (performance)

### Phase 4: Edge Cases and Performance

1. Write tests: density=0, lifetime < 1ms, frequency above Nyquist, NaN inputs
2. Write performance test: 64 particles at 44.1kHz < 0.5% CPU
3. Verify all SC-xxx criteria
4. Update architecture docs

## Key Design Decisions

| Decision | Rationale | See |
|----------|-----------|-----|
| Inline particle pool (not GrainPool) | Different fields (phase, drift vs readPosition, playbackRate) | research.md R-001 |
| Inline spawn scheduler (not GrainScheduler) | Different timing model (lifetime/density vs grains/sec) | research.md R-002 |
| Inline one-pole for drift (not OnePoleSmoother) | 64 instances need minimal overhead | research.md R-003 |
| std::sin() for sine generation | MSVC optimized; spec explicitly allows | research.md R-006 |
| Normalization uses target density | Avoids amplitude pumping (spec clarification) | research.md R-005 |
| All envelope types precomputed | Real-time safe switching (spec clarification) | research.md R-004 |

## Test Tags

Use `[ParticleOscillator]` as the primary Catch2 tag. Additional sub-tags:

- `[lifecycle]` -- prepare, reset, pre-prepare behavior
- `[frequency]` -- center frequency, scatter
- `[population]` -- density, lifetime, spawn modes
- `[drift]` -- frequency drift
- `[envelope]` -- envelope types and switching
- `[output]` -- normalization, sanitization, clamping
- `[performance]` -- CPU budget verification
- `[edge-cases]` -- boundary conditions, NaN, overflow

## CMake Changes Required

Add to `dsp/tests/CMakeLists.txt` in the Layer 2: Processors section:

```cmake
# In add_executable(dsp_tests ...)
    unit/processors/particle_oscillator_test.cpp

# In set_source_files_properties(...) for -fno-fast-math
    unit/processors/particle_oscillator_test.cpp
```
