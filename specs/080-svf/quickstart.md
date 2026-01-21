# Quickstart: State Variable Filter (SVF) Implementation

**Feature**: 080-svf | **Date**: 2026-01-21 | **Estimated Effort**: 6-10 hours

## Overview

This guide provides step-by-step implementation instructions for the TPT State Variable Filter.

## Prerequisites

Before starting:
1. Read `spec.md` for all 27 functional requirements and 14 success criteria
2. Read `research.md` for algorithm details and design decisions
3. Read `data-model.md` for entity definitions
4. Read `contracts/svf.h` for the API contract

## Implementation Order

### Phase 1: Core Structure (1-2 hours)

#### Step 1.1: Create Header File

Create `dsp/include/krate/dsp/primitives/svf.h` with:

```cpp
#pragma once

#include <krate/dsp/core/db_utils.h>
#include <krate/dsp/core/math_constants.h>

#include <cmath>
#include <cstddef>
#include <cstdint>

namespace Krate {
namespace DSP {

// SVFMode enum (FR-001) - copy from contracts/svf.h

// SVFOutputs struct (FR-002) - copy from contracts/svf.h

// SVF class declaration (FR-003) - copy from contracts/svf.h

} // namespace DSP
} // namespace Krate
```

#### Step 1.2: Implement Clamping Helpers

```cpp
float SVF::clampCutoff(float hz) const noexcept {
    const float maxFreq = static_cast<float>(sampleRate_) * kMaxCutoffRatio;
    if (hz < kMinCutoff) return kMinCutoff;
    if (hz > maxFreq) return maxFreq;
    return hz;
}

float SVF::clampQ(float q) const noexcept {
    if (q < kMinQ) return kMinQ;
    if (q > kMaxQ) return kMaxQ;
    return q;
}

float SVF::clampGainDb(float dB) const noexcept {
    if (dB < kMinGainDb) return kMinGainDb;
    if (dB > kMaxGainDb) return kMaxGainDb;
    return dB;
}
```

#### Step 1.3: Implement Coefficient Calculation

```cpp
void SVF::updateCoefficients() noexcept {
    // FR-013: g = tan(pi * cutoff / sampleRate)
    g_ = std::tan(kPi * cutoffHz_ / static_cast<float>(sampleRate_));

    // FR-013: k = 1/Q
    k_ = 1.0f / q_;

    // FR-014: Derived coefficients
    a1_ = 1.0f / (1.0f + g_ * (g_ + k_));
    a2_ = g_ * a1_;
    a3_ = g_ * a2_;

    // Update mode mixing (depends on k_ and A_)
    updateMixCoefficients();
}

void SVF::updateMixCoefficients() noexcept {
    // FR-017: Mode mixing coefficients
    switch (mode_) {
        case SVFMode::Lowpass:
            m0_ = 0.0f; m1_ = 0.0f; m2_ = 1.0f;
            break;
        case SVFMode::Highpass:
            m0_ = 1.0f; m1_ = 0.0f; m2_ = 0.0f;
            break;
        case SVFMode::Bandpass:
            m0_ = 0.0f; m1_ = 1.0f; m2_ = 0.0f;
            break;
        case SVFMode::Notch:
            m0_ = 1.0f; m1_ = 0.0f; m2_ = 1.0f;
            break;
        case SVFMode::Allpass:
            m0_ = 1.0f; m1_ = -k_; m2_ = 1.0f;
            break;
        case SVFMode::Peak:
            m0_ = 1.0f; m1_ = 0.0f; m2_ = -1.0f;
            break;
        case SVFMode::LowShelf:
            m0_ = 1.0f;
            m1_ = k_ * (A_ - 1.0f);
            m2_ = A_ * A_;
            break;
        case SVFMode::HighShelf:
            m0_ = A_ * A_;
            m1_ = k_ * (A_ - 1.0f);
            m2_ = 1.0f;
            break;
    }
}
```

### Phase 2: Lifecycle Methods (30 min)

#### Step 2.1: Implement prepare()

```cpp
void SVF::prepare(double sampleRate) noexcept {
    // Clamp sample rate to valid minimum
    sampleRate_ = (sampleRate >= 1000.0) ? sampleRate : 1000.0;
    prepared_ = true;

    // Recalculate all coefficients
    updateCoefficients();
}
```

#### Step 2.2: Implement setters

```cpp
void SVF::setMode(SVFMode mode) noexcept {
    mode_ = mode;
    updateMixCoefficients();
}

void SVF::setCutoff(float hz) noexcept {
    cutoffHz_ = clampCutoff(hz);
    updateCoefficients();  // FR-006: immediate recalculation
}

void SVF::setResonance(float q) noexcept {
    q_ = clampQ(q);
    updateCoefficients();  // FR-007: immediate recalculation
}

void SVF::setGain(float dB) noexcept {
    gainDb_ = clampGainDb(dB);
    // FR-008: Calculate A immediately
    A_ = detail::constexprPow10(gainDb_ / 40.0f);
    updateMixCoefficients();  // m1_, m2_ depend on A_ for shelf modes
}

void SVF::reset() noexcept {
    ic1eq_ = 0.0f;
    ic2eq_ = 0.0f;
}
```

#### Step 2.3: Implement getters

```cpp
SVFMode SVF::getMode() const noexcept { return mode_; }
float SVF::getCutoff() const noexcept { return cutoffHz_; }
float SVF::getResonance() const noexcept { return q_; }
float SVF::getGain() const noexcept { return gainDb_; }
bool SVF::isPrepared() const noexcept { return prepared_; }
```

### Phase 3: Processing Methods (1-2 hours)

#### Step 3.1: Implement processMulti() (FR-012)

```cpp
[[nodiscard]] SVFOutputs SVF::processMulti(float input) noexcept {
    // FR-021: Return zeros if not prepared
    if (!prepared_) {
        return SVFOutputs{0.0f, 0.0f, 0.0f, 0.0f};
    }

    // FR-022: Handle NaN/Inf input
    if (detail::isNaN(input) || detail::isInf(input)) {
        reset();
        return SVFOutputs{0.0f, 0.0f, 0.0f, 0.0f};
    }

    // FR-016: Per-sample computation
    const float v3 = input - ic2eq_;
    const float v1 = a1_ * ic1eq_ + a2_ * v3;
    const float v2 = ic2eq_ + a2_ * ic1eq_ + a3_ * v3;

    // Update integrator states (trapezoidal rule)
    ic1eq_ = 2.0f * v1 - ic1eq_;
    ic2eq_ = 2.0f * v2 - ic2eq_;

    // FR-019: Flush denormals after every process() call
    ic1eq_ = detail::flushDenormal(ic1eq_);
    ic2eq_ = detail::flushDenormal(ic2eq_);

    // Compute all outputs
    SVFOutputs out;
    out.low = v2;
    out.band = v1;
    out.high = v3 - k_ * v1 - v2;
    out.notch = out.low + out.high;

    return out;
}
```

#### Step 3.2: Implement process() (FR-010)

```cpp
[[nodiscard]] float SVF::process(float input) noexcept {
    // FR-021: Return input unchanged if not prepared
    if (!prepared_) {
        return input;
    }

    // FR-022: Handle NaN/Inf input
    if (detail::isNaN(input) || detail::isInf(input)) {
        reset();
        return 0.0f;
    }

    // FR-016: Per-sample computation
    const float v3 = input - ic2eq_;
    const float v1 = a1_ * ic1eq_ + a2_ * v3;
    const float v2 = ic2eq_ + a2_ * ic1eq_ + a3_ * v3;

    // Update integrator states
    ic1eq_ = 2.0f * v1 - ic1eq_;
    ic2eq_ = 2.0f * v2 - ic2eq_;

    // FR-019: Flush denormals
    ic1eq_ = detail::flushDenormal(ic1eq_);
    ic2eq_ = detail::flushDenormal(ic2eq_);

    // FR-016: Compute outputs
    const float low = v2;
    const float band = v1;
    const float high = v3 - k_ * v1 - v2;

    // FR-017: Mode mixing
    return m0_ * high + m1_ * band + m2_ * low;
}
```

#### Step 3.3: Implement processBlock() (FR-011)

```cpp
void SVF::processBlock(float* buffer, size_t numSamples) noexcept {
    // Simple loop - produces bit-identical output to process() calls (SC-012)
    for (size_t i = 0; i < numSamples; ++i) {
        buffer[i] = process(buffer[i]);
    }
}
```

### Phase 4: Testing (2-3 hours)

#### Step 4.1: Create Test File

Create `dsp/tests/primitives/svf_test.cpp`:

```cpp
#include <krate/dsp/primitives/svf.h>
#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>
#include <cmath>
#include <vector>

using namespace Krate::DSP;
using Catch::Approx;

// Test helpers
namespace {
    // Generate sine wave
    std::vector<float> generateSine(float freq, float sampleRate, size_t numSamples) {
        std::vector<float> buffer(numSamples);
        for (size_t i = 0; i < numSamples; ++i) {
            buffer[i] = std::sin(kTwoPi * freq * static_cast<float>(i) / sampleRate);
        }
        return buffer;
    }

    // Measure RMS
    float measureRMS(const std::vector<float>& buffer) {
        float sum = 0.0f;
        for (float sample : buffer) {
            sum += sample * sample;
        }
        return std::sqrt(sum / static_cast<float>(buffer.size()));
    }

    // Convert RMS to dB relative to reference
    float rmsToDb(float rms, float reference = 1.0f) {
        if (rms <= 0.0f) return -144.0f;
        return 20.0f * std::log10(rms / reference);
    }
}
```

#### Step 4.2: Write Tests for Each Requirement

See spec.md for all 27 FRs and 14 SCs. Key test categories:

1. **Lifecycle tests**: prepare(), setMode(), setCutoff(), setResonance(), setGain(), reset()
2. **Parameter clamping tests**: Edge cases for all parameter ranges
3. **Frequency response tests**: SC-001 through SC-010
4. **Modulation stability tests**: SC-011
5. **Block processing tests**: SC-012
6. **Stability tests**: SC-013
7. **Edge case tests**: NaN, Inf, pre-prepare behavior

Example test structure:

```cpp
TEST_CASE("SVF lowpass attenuates high frequencies", "[svf][sc-001]") {
    SVF filter;
    filter.prepare(44100.0);
    filter.setMode(SVFMode::Lowpass);
    filter.setCutoff(1000.0f);
    filter.setResonance(SVF::kButterworthQ);

    // Process 10kHz sine (1 decade above cutoff)
    auto input = generateSine(10000.0f, 44100.0f, 4096);
    float inputRMS = measureRMS(input);

    for (auto& sample : input) {
        sample = filter.process(sample);
    }
    float outputRMS = measureRMS(input);

    float attenuation = rmsToDb(outputRMS, inputRMS);
    REQUIRE(attenuation <= -22.0f);  // SC-001: at least 22 dB attenuation
}
```

### Phase 5: Documentation Update (30 min)

After implementation, update `specs/_architecture_/layer-1-primitives.md` to add the SVF component entry.

## Verification Checklist

Before claiming completion, verify:

- [ ] All 27 FR-xxx requirements implemented
- [ ] All 14 SC-xxx success criteria pass tests
- [ ] No compiler warnings
- [ ] `dsp_tests` target passes
- [ ] `specs/080-svf/spec.md` compliance table filled

## Common Pitfalls

1. **Forgetting to update A_ in setGain()**: Must call `detail::constexprPow10(gainDb_ / 40.0f)`
2. **Forgetting to update mix coefficients**: setMode(), setResonance(), and setGain() all affect m0/m1/m2
3. **Using isNaN from std**: Must use `detail::isNaN()` from db_utils.h for -ffast-math compatibility
4. **Denormal flushing**: Must flush ic1eq_ AND ic2eq_ after every sample
5. **Allpass m1 coefficient**: Is `-k_`, not `k_` (note the negative sign)

## Build Commands

```bash
# Configure
"/c/Program Files/CMake/bin/cmake.exe" --preset windows-x64-release

# Build DSP tests
"/c/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target dsp_tests

# Run tests
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[svf]"
```

## References

- [Cytomic Technical Papers](https://cytomic.com/technical-papers/)
- [SvfLinearTrapOptimised2.pdf](https://cytomic.com/files/dsp/SvfLinearTrapOptimised2.pdf)
- Existing patterns: `biquad.h`, `one_pole.h`
