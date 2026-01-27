# Quickstart: Filter Foundations

**Feature Branch**: `070-filter-foundations`
**Date**: 2026-01-20

## Overview

This guide covers implementing the Filter Foundations feature: three header files providing formant data, filter design utilities, and one-pole audio filters.

## Prerequisites

Before implementation:
1. Read `specs/070-filter-foundations/spec.md` for all requirements
2. Read `specs/070-filter-foundations/research.md` for formulas and values
3. Understand Layer 0/1 architecture from `.claude/skills/dsp-architecture/`

## Implementation Order

Follow this order to satisfy dependencies:

1. **filter_tables.h** (Layer 0) - No dependencies, pure data
2. **filter_design.h** (Layer 0) - Depends on math_constants.h, db_utils.h
3. **one_pole.h** (Layer 1) - Depends on Layer 0 files

For each file: **Write tests first, then implement.**

---

## 1. filter_tables.h

### Location
`dsp/include/krate/dsp/core/filter_tables.h`

### Test File
`dsp/tests/core/filter_tables_test.cpp`

### Key Points

- All data is constexpr
- No dependencies on other DSP files
- FormantData struct with 6 floats
- Vowel enum with 5 values
- kVowelFormants array indexed by Vowel

### Implementation Pattern

```cpp
#pragma once

#include <array>
#include <cstdint>

namespace Krate {
namespace DSP {

/// Vowel selection for formant table indexing.
enum class Vowel : uint8_t { A = 0, E = 1, I = 2, O = 3, U = 4 };

/// Formant frequency and bandwidth data.
struct FormantData {
    float f1, f2, f3;    // Formant frequencies (Hz)
    float bw1, bw2, bw3; // Bandwidths (Hz)
};

/// Formant table: bass male voice (Csound standard).
inline constexpr std::array<FormantData, 5> kVowelFormants = {{
    {600.0f, 1040.0f, 2250.0f, 60.0f, 70.0f, 110.0f}, // A
    {400.0f, 1620.0f, 2400.0f, 40.0f, 80.0f, 100.0f}, // E
    {250.0f, 1750.0f, 2600.0f, 60.0f, 90.0f, 100.0f}, // I
    {400.0f,  750.0f, 2400.0f, 40.0f, 80.0f, 100.0f}, // O
    {350.0f,  600.0f, 2400.0f, 40.0f, 80.0f, 100.0f}, // U
}};

/// Helper function for type-safe access.
[[nodiscard]] inline constexpr const FormantData& getFormant(Vowel v) noexcept {
    return kVowelFormants[static_cast<size_t>(v)];
}

} // namespace DSP
} // namespace Krate
```

### Test Cases

1. Verify all data is constexpr (static_assert)
2. Verify F1 values within 10% of research values
3. Verify vowel 'a' matches SC-008 criteria
4. Verify array size is 5
5. Verify getFormant() returns correct data

---

## 2. filter_design.h

### Location
`dsp/include/krate/dsp/core/filter_design.h`

### Test File
`dsp/tests/core/filter_design_test.cpp`

### Key Points

- All functions are constexpr where possible
- Uses kPi from math_constants.h
- Uses detail::constexprExp from db_utils.h
- Functions are in FilterDesign namespace

### Implementation Pattern

```cpp
#pragma once

#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/db_utils.h>
#include <cmath>
#include <cstddef>

namespace Krate {
namespace DSP {
namespace FilterDesign {

/// Prewarp frequency for bilinear transform.
[[nodiscard]] inline float prewarpFrequency(float freq, double sampleRate) noexcept {
    if (sampleRate <= 0.0 || freq <= 0.0f) return freq;

    const float omega = kPi * freq / static_cast<float>(sampleRate);
    // Clamp to avoid tan approaching infinity near pi/2
    const float clampedOmega = (omega > 1.5f) ? 1.5f : omega;

    return (static_cast<float>(sampleRate) / kPi) * std::tan(clampedOmega);
}

/// Calculate feedback coefficient for desired RT60.
[[nodiscard]] inline float combFeedbackForRT60(float delayMs, float rt60Seconds) noexcept {
    if (delayMs <= 0.0f || rt60Seconds <= 0.0f) return 0.0f;

    const float rt60Ms = rt60Seconds * 1000.0f;
    const float exponent = -3.0f * delayMs / rt60Ms;

    // 10^x = e^(x * ln(10)), ln(10) = 2.302585093f
    return detail::constexprExp(exponent * 2.302585093f);
}

/// Butterworth pole angle.
[[nodiscard]] constexpr float butterworthPoleAngle(size_t k, size_t N) noexcept {
    if (N == 0) return 0.0f;
    return kPi * (2.0f * static_cast<float>(k) + 1.0f) / (2.0f * static_cast<float>(N));
}

/// Bessel Q lookup table (orders 2-8).
namespace detail {
    inline constexpr float besselQTable[7][4] = {
        {0.57735f,  0.0f,     0.0f,     0.0f},
        {0.69105f,  0.0f,     0.0f,     0.0f},
        {0.80554f,  0.52193f, 0.0f,     0.0f},
        {0.91648f,  0.56354f, 0.0f,     0.0f},
        {1.02331f,  0.61119f, 0.51032f, 0.0f},
        {1.12626f,  0.66082f, 0.53236f, 0.0f},
        {1.22567f,  0.71085f, 0.55961f, 0.50599f},
    };
}

/// Bessel Q value lookup.
[[nodiscard]] constexpr float besselQ(size_t stage, size_t numStages) noexcept {
    if (numStages < 2 || numStages > 8) return 0.7071f;
    const size_t numBiquads = numStages / 2;
    if (stage >= numBiquads) return 0.7071f;
    return detail::besselQTable[numStages - 2][stage];
}

/// Chebyshev Type I Q calculation.
[[nodiscard]] inline float chebyshevQ(size_t stage, size_t numStages, float rippleDb) noexcept {
    // Implementation with sinh/cosh/asinh - see research.md
    // Falls back to Butterworth when ripple <= 0
}

} // namespace FilterDesign
} // namespace DSP
} // namespace Krate
```

### Test Cases

1. **prewarpFrequency**: SC-006 - verify 1000Hz at 44100Hz within 1% of tan(pi*1000/44100)
2. **combFeedbackForRT60**: SC-007 - verify 50ms/2s returns ~0.841
3. **besselQ**: Verify table values for orders 2-8
4. **chebyshevQ**: Verify against known values for 1dB ripple
5. **butterworthPoleAngle**: Verify pi/4 for k=0, N=2
6. Edge cases: zero/negative inputs, out-of-range stages

---

## 3. one_pole.h

### Location
`dsp/include/krate/dsp/primitives/one_pole.h`

### Test File
`dsp/tests/primitives/one_pole_test.cpp`

### Key Points

- Similar pattern to OnePoleSmoother but for audio processing
- Must handle NaN/Inf inputs (FR-027a)
- Must flush denormals (FR-024)
- Must work before prepare() is called (FR-027)
- All processing methods are noexcept

### Implementation Pattern

```cpp
#pragma once

#include <krate/dsp/core/math_constants.h>
#include <krate/dsp/core/db_utils.h>
#include <cmath>
#include <cstddef>

namespace Krate {
namespace DSP {

class OnePoleLP {
public:
    void prepare(double sampleRate) noexcept {
        sampleRate_ = (sampleRate > 0.0) ? sampleRate : 44100.0;
        prepared_ = true;
        updateCoefficient();
    }

    void setCutoff(float hz) noexcept {
        cutoffHz_ = clampCutoff(hz);
        updateCoefficient();
    }

    [[nodiscard]] float process(float input) noexcept {
        // Handle unprepared state
        if (!prepared_) return input;

        // Handle NaN/Inf
        if (detail::isNaN(input) || detail::isInf(input)) {
            reset();
            return 0.0f;
        }

        // y[n] = (1-a)*x[n] + a*y[n-1]
        state_ = (1.0f - coefficient_) * input + coefficient_ * state_;
        state_ = detail::flushDenormal(state_);
        return state_;
    }

    void processBlock(float* buffer, size_t numSamples) noexcept {
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = process(buffer[i]);
        }
    }

    void reset() noexcept {
        state_ = 0.0f;
    }

private:
    float clampCutoff(float hz) const noexcept {
        if (hz <= 0.0f) return 1.0f;
        const float nyquist = static_cast<float>(sampleRate_) * 0.495f;
        return (hz > nyquist) ? nyquist : hz;
    }

    void updateCoefficient() noexcept {
        // a = exp(-2*pi*fc/fs)
        coefficient_ = std::exp(-kTwoPi * cutoffHz_ / static_cast<float>(sampleRate_));
    }

    float coefficient_ = 0.0f;
    float state_ = 0.0f;
    float cutoffHz_ = 1000.0f;
    double sampleRate_ = 44100.0;
    bool prepared_ = false;
};

// OnePoleHP and LeakyIntegrator follow similar patterns

} // namespace DSP
} // namespace Krate
```

### Test Cases

1. **SC-001**: 1000Hz cutoff attenuates 10kHz by ~20dB
2. **SC-002**: 1000Hz cutoff passes 100Hz within 0.5dB
3. **SC-003/004**: OnePoleHP corresponding tests
4. **SC-005**: LeakyIntegrator time constant ~22ms with leak=0.999
5. **SC-009**: processBlock matches process() output
6. **SC-010**: No NaN/Inf from 1M valid samples
7. **FR-027**: Process before prepare returns input unchanged
8. **FR-027a**: NaN input returns 0 and resets state
9. **SC-012**: Verify constexpr where applicable

---

## Build and Test

```bash
# Configure
"/c/Program Files/CMake/bin/cmake.exe" --preset windows-x64-release

# Build DSP library and tests
"/c/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests

# Run tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe

# Or run specific tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[filter_tables]"
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[filter_design]"
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[one_pole]"
```

---

## Common Pitfalls

1. **Forgetting -fno-fast-math**: Add to test file CMakeLists for NaN tests
2. **Using std::tan in constexpr**: Use runtime std::tan, mark function inline not constexpr
3. **Forgetting denormal flush**: Always call `detail::flushDenormal()` on state
4. **Incorrect HP formula**: Use (1+a)/2 not (1-a)/2
5. **Bessel Q indexing**: Table is indexed by [order-2][stage], order 2-8 only

---

## Checklist Before Completion

- [ ] All FR-xxx requirements implemented
- [ ] All SC-xxx success criteria verified with tests
- [ ] No compiler warnings
- [ ] processBlock produces same output as process() loops
- [ ] Edge cases tested (zero/negative/NaN/Inf/before-prepare)
- [ ] Doxygen documentation on all public API
- [ ] Tests run without failures
