# Quickstart: PolyBLEP Oscillator Implementation

**Branch**: `015-polyblep-oscillator` | **Date**: 2026-02-03

## Files to Create

| File | Purpose |
|------|---------|
| `dsp/include/krate/dsp/primitives/polyblep_oscillator.h` | Header-only oscillator implementation |
| `dsp/tests/unit/primitives/polyblep_oscillator_test.cpp` | Comprehensive test suite |

## Files to Modify

| File | Change |
|------|--------|
| `dsp/tests/CMakeLists.txt` | Add `unit/primitives/polyblep_oscillator_test.cpp` to `dsp_tests` target and `-fno-fast-math` list |
| `specs/_architecture_/layer-1-primitives.md` | Add PolyBlepOscillator section |

## Dependencies (All Existing, Layer 0)

```cpp
#include <krate/dsp/core/polyblep.h>       // polyBlep(t, dt)
#include <krate/dsp/core/phase_utils.h>    // PhaseAccumulator, wrapPhase, calculatePhaseIncrement
#include <krate/dsp/core/math_constants.h> // kPi, kTwoPi
#include <krate/dsp/core/db_utils.h>       // detail::isNaN, detail::isInf (for sanitization)
```

> **FTZ/DAZ Note**: Flush-to-zero (FTZ) and denormals-are-zero (DAZ) CPU flags are set at the **plugin processor level** (`Iterum::Processor`), not by this oscillator. The oscillator assumes these flags are already active. The anti-denormal constant (1e-18f) in the triangle leaky integrator is an additional intra-oscillator safety measure. See constitution Principle II and FR-035.

## Build & Test

```bash
# Set CMake alias (Windows)
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# Configure
"$CMAKE" --preset windows-x64-release

# Build DSP tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests

# Run all DSP tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe

# Run only PolyBLEP oscillator tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[PolyBlepOscillator]"
```

## Implementation Order (Test-First)

### Phase 1: Skeleton + Sine (P2 but simplest, validates lifecycle)

1. Write test: Sine output matches `sin(2*pi*n*f/fs)` within 1e-5 (SC-004)
2. Write test: `prepare()` / `reset()` lifecycle
3. Implement: OscWaveform enum, class skeleton, prepare/reset, sine process
4. Build, verify tests pass

### Phase 2: Sawtooth + Square (P1 core waveforms)

1. Write test: Sawtooth output in range [-1.1, 1.1] (SC-009)
2. Write test: Sawtooth FFT alias suppression >= 40 dB (SC-001)
3. Write test: Square FFT alias suppression >= 40 dB (SC-002)
4. Implement: Sawtooth with polyBlep, Square with polyBlep at both edges
5. Build, verify tests pass

### Phase 3: Pulse + Triangle (P1 remaining)

1. Write test: Pulse PW=0.5 matches Square (SC-007)
2. Write test: Pulse PW=0.25 FFT alias suppression >= 40 dB (SC-003)
3. Write test: Triangle DC offset < 0.01 over 10 seconds (SC-005)
4. Write test: Triangle amplitude consistency +/-20% across 100-10000 Hz (SC-013)
5. Implement: Pulse waveform, Triangle with leaky integrator
6. Build, verify tests pass

### Phase 4: Phase Access + FM/PM (P2)

1. Write test: Phase increases monotonically in [0, 1) (SC-006)
2. Write test: phaseWrapped() fires ~440 times in 44100 samples
3. Write test: resetPhase(0.5) works (SC-011)
4. Write test: PM with 0 radians = unmodulated (acceptance scenario)
5. Write test: FM offset changes effective frequency
6. Implement: phase(), phaseWrapped(), resetPhase(), PM, FM
7. Build, verify tests pass

### Phase 5: Edge Cases + Robustness (P3)

1. Write test: processBlock matches N x process() (SC-008)
2. Write test: Frequency at Nyquist produces valid output (SC-010)
3. Write test: NaN/Inf inputs produce safe output (SC-015)
4. Write test: Waveform switching preserves phase continuity
5. Write test: Output bounds [-1.1, 1.1] across all waveforms (SC-009)
6. Implement: edge case handling, output sanitization
7. Build, verify all tests pass

### Phase 6: Cleanup

1. Fix all compiler warnings (zero warnings required per FR-026)
2. Run clang-tidy
3. Update architecture docs
4. Final compliance table review

## Key Formulas

### Sawtooth (FR-012)
```
naive = 2.0f * phase - 1.0f
output = naive - polyBlep(phase, dt)
```

### Square (FR-013)
```
naive = (phase < 0.5f) ? 1.0f : -1.0f
output = naive - polyBlep(phase, dt) + polyBlep(wrapPhase(phase + 0.5), dt)
```

### Pulse (FR-014)
```
naive = (phase < pw) ? 1.0f : -1.0f
output = naive - polyBlep(phase, dt) + polyBlep(wrapPhase(phase + 1.0 - pw), dt)
```

### Triangle (FR-015)
```
square = computeSquareBLEP(phase, dt)  // PolyBLEP-corrected square
leak = 1.0f - (4.0f * effectiveFreq / sampleRate_)
integrator_ = leak * integrator_ + (4.0f * dt) * square + 1e-18f
output = integrator_
```

### Output Sanitization (FR-036)
```cpp
float sanitize(float x) noexcept {
    // NaN check via bit manipulation (works with -ffast-math)
    const auto bits = std::bit_cast<uint32_t>(x);
    const bool nan = ((bits & 0x7F800000u) == 0x7F800000u) && ((bits & 0x007FFFFFu) != 0);
    x = nan ? 0.0f : x;
    x = (x < -2.0f) ? -2.0f : x;
    x = (x > 2.0f) ? 2.0f : x;
    return x;
}
```

## Test Helper Usage (for FFT tests)

```cpp
#include "test_helpers/spectral_analysis.h"

using namespace Krate::DSP::TestUtils;

// Configure for 1000 Hz at 44100 Hz
AliasingTestConfig config;
config.testFrequencyHz = 1000.0f;
config.sampleRate = 44100.0f;
config.driveGain = 1.0f;  // No additional drive for oscillator
config.fftSize = 4096;
config.maxHarmonic = 30;

// Get bins where aliased harmonics appear
auto aliasedBins = getAliasedBins(config);

// Measure: each aliased bin should be >= 40dB below fundamental
```
