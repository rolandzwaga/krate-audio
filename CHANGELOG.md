# Changelog

All notable changes to Iterum will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [0.0.2] - 2025-12-22

### Added

- **Layer 1 DSP Primitive: DelayLine** (`src/dsp/primitives/delay_line.h`)
  - Real-time safe circular buffer delay line with fractional sample interpolation
  - Three read methods for different use cases:
    - `read(size_t)` - Integer delay, no interpolation (fastest)
    - `readLinear(float)` - Linear interpolation for modulated delays (chorus, flanger, vibrato)
    - `readAllpass(float)` - Allpass interpolation for feedback loops (unity gain at all frequencies)
  - Power-of-2 buffer sizing for O(1) bitwise wraparound
  - `prepare(sampleRate, maxDelaySeconds)` - Allocates buffer before processing
  - `reset()` - Clears buffer to silence without reallocation
  - Query methods: `maxDelaySamples()`, `sampleRate()`

- **Utility function**: `Iterum::DSP::nextPowerOf2(size_t)` - Constexpr power-of-2 calculation

- **Comprehensive test suite** (436 assertions across 50 test cases)
  - Basic fixed delay (write/read operations)
  - Linear interpolation accuracy
  - Allpass interpolation unity gain verification
  - Modulation smoothness tests
  - Real-time safety verification (noexcept static_assert)
  - O(1) performance characteristics
  - Edge case coverage (clamping, wrap-around)

### Technical Details

- **Interpolation formulas**:
  - Linear: `y = y0 + frac * (y1 - y0)`
  - Allpass: `y = x0 + a * (state - x1)` where `a = (1 - frac) / (1 + frac)`
- **Buffer sizing**: Next power of 2 >= (maxDelaySamples + 1) for efficient bitwise AND wrap
- **Namespace**: `Iterum::DSP` (Layer 1 DSP primitives)
- **Constitution compliance**: Principles II (RT Safety), III (Modern C++), IX (Layered Architecture), XII (Test-First)

### Usage

```cpp
#include "dsp/primitives/delay_line.h"

Iterum::DSP::DelayLine delay;

// In prepare() - allocates memory
delay.prepare(44100.0, 1.0f);  // 1 second max delay

// In processBlock() - real-time safe
delay.write(inputSample);

// Fixed delay (simple echo)
float echo = delay.read(22050);  // 0.5 second delay

// Modulated delay (chorus with LFO)
float lfoDelay = 500.0f + 20.0f * lfoValue;
float chorus = delay.readLinear(lfoDelay);

// Feedback network (fractional comb filter)
float comb = delay.readAllpass(100.5f);  // Fixed fractional delay
```

---

## [0.0.1] - 2025-12-22

### Added

- **Layer 0 Core Utilities: dB/Linear Conversion** (`src/dsp/core/db_utils.h`)
  - `Iterum::DSP::dbToGain(float dB)` - Convert decibels to linear gain
  - `Iterum::DSP::gainToDb(float gain)` - Convert linear gain to decibels
  - `Iterum::DSP::kSilenceFloorDb` - Silence floor constant (-144 dB)
  - Full C++20 `constexpr` support for compile-time evaluation
  - Real-time safe: no allocation, no exceptions, no I/O
  - NaN handling: `dbToGain(NaN)` returns 0.0f, `gainToDb(NaN)` returns -144 dB

- **Custom constexpr math implementations** (MSVC compatibility)
  - Taylor series `constexprExp()` and `constexprLn()` functions
  - Required because MSVC lacks constexpr `std::pow`/`std::log10`

- **Comprehensive test suite** (146 assertions across 24 test cases)
  - Unit tests for all dB conversion functions
  - Constexpr compile-time evaluation tests
  - Edge case coverage (NaN, infinity, silence)

- **Project infrastructure**
  - Layered DSP architecture (Layer 0-4 hierarchy)
  - Test-first development workflow (Constitution Principle XII)
  - Catch2 testing framework integration

### Technical Details

- **Silence floor**: -144 dB (24-bit dynamic range: 6.02 dB/bit * 24 = ~144 dB)
- **Formulas**:
  - `dbToGain`: gain = 10^(dB/20)
  - `gainToDb`: dB = 20 * log10(gain), clamped to -144 dB floor
- **Namespace**: `Iterum::DSP` (Layer 0 core utilities)

### Usage

```cpp
#include "dsp/core/db_utils.h"

// Runtime conversion
float gain = Iterum::DSP::dbToGain(-6.0f);    // ~0.5
float dB   = Iterum::DSP::gainToDb(0.5f);     // ~-6 dB

// Compile-time lookup tables
constexpr std::array<float, 3> gains = {
    Iterum::DSP::dbToGain(-20.0f),  // 0.1
    Iterum::DSP::dbToGain(0.0f),    // 1.0
    Iterum::DSP::dbToGain(20.0f)    // 10.0
};
```
