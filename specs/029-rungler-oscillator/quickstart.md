# Quickstart: Rungler / Shift Register Oscillator

**Branch**: `029-rungler-oscillator` | **Date**: 2026-02-06

---

## What This Feature Does

A Benjolin-inspired chaotic stepped-voltage generator. Two cross-modulating triangle oscillators drive an 8-bit shift register with XOR feedback, producing evolving stepped sequences via a 3-bit DAC. Five simultaneous outputs: osc1 triangle, osc2 triangle, rungler CV, PWM comparator, and mixed.

---

## Files to Create

| File | Layer | Purpose |
|------|-------|---------|
| `dsp/include/krate/dsp/processors/rungler.h` | 2 | Header-only Rungler implementation |
| `dsp/tests/unit/processors/rungler_test.cpp` | -- | Test file |

## Files to Modify

| File | Change |
|------|--------|
| `dsp/tests/CMakeLists.txt` | Add `unit/processors/rungler_test.cpp` to test list and `-fno-fast-math` list |
| `specs/_architecture_/layer-2-processors.md` | Add Rungler entry to architecture docs |

---

## Usage Example

```cpp
#include <krate/dsp/processors/rungler.h>

using namespace Krate::DSP;

// Create and prepare
Rungler rungler;
rungler.prepare(44100.0);

// Configure
rungler.setOsc1Frequency(200.0f);   // 200 Hz base
rungler.setOsc2Frequency(300.0f);   // 300 Hz base (clock source)
rungler.setRunglerDepth(0.5f);      // Moderate cross-modulation
rungler.setFilterAmount(0.0f);      // Raw stepped output
rungler.setLoopMode(false);          // Chaos mode (default)

// Process single sample
Rungler::Output out = rungler.process();
// out.osc1    -> Oscillator 1 triangle [-1, +1]
// out.osc2    -> Oscillator 2 triangle [-1, +1]
// out.rungler -> Rungler CV [0, +1] (8-level stepped voltage)
// out.pwm     -> PWM comparator [-1, +1]
// out.mixed   -> (osc1 + osc2) * 0.5 [-1, +1]

// Process block (all outputs)
std::vector<Rungler::Output> buffer(512);
rungler.processBlock(buffer.data(), 512);

// Process block (mixed output only)
std::vector<float> mixedBuffer(512);
rungler.processBlockMixed(mixedBuffer.data(), 512);

// Process block (rungler CV only)
std::vector<float> cvBuffer(512);
rungler.processBlockRungler(cvBuffer.data(), 512);

// Loop mode for repeating patterns
rungler.setLoopMode(true);

// Deterministic output for tests:
// seed() sets the PRNG seed; reset() re-initializes the shift register
// using that seed, producing identical output every time
rungler.seed(12345);
rungler.reset();
```

---

## Key Dependencies

| Component | Include | Used For |
|-----------|---------|----------|
| `Xorshift32` | `<krate/dsp/core/random.h>` | Shift register seeding |
| `OnePoleLP` | `<krate/dsp/primitives/one_pole.h>` | CV smoothing filter |
| `detail::isNaN/isInf` | `<krate/dsp/core/db_utils.h>` | Input sanitization |

---

## Test Strategy

1. **Output bounds**: All outputs within specified ranges for 10 seconds at various parameter combinations (SC-001)
2. **DAC quantization**: Exactly 8 discrete levels in raw output (SC-002)
3. **Loop mode autocorrelation**: Repeating pattern confirmed (SC-003)
4. **Frequency accuracy at depth 0**: Oscillators hit set frequencies within 1% (SC-004)
5. **Spectral broadening at depth 1**: Confirmed frequency modulation effect (SC-005)
6. **CPU performance**: < 0.5% at 44.1 kHz (SC-006)
7. **Bits change safety**: No NaN/Inf/large discontinuity (SC-007)
8. **Seed divergence**: Different seeds produce different outputs (SC-008)

---

## Build and Test

```bash
# Build DSP tests
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests

# Run only Rungler tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "Rungler*"

# Run all DSP tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe
```
