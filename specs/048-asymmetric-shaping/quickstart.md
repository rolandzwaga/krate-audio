# Quickstart: Asymmetric Shaping Functions

**Spec**: 048-asymmetric-shaping | **Date**: 2026-01-12

## Implementation Summary

The asymmetric shaping functions are **already implemented** in `dsp/include/krate/dsp/core/sigmoid.h` as part of spec 047 (Sigmoid Functions). This quickstart describes the remaining work to fully satisfy spec 048 requirements.

## Remaining Tasks

### 1. Fix withBias() DC Neutrality (HIGH PRIORITY)

**File**: `dsp/include/krate/dsp/core/sigmoid.h`
**Lines**: 350-353

**Current (incorrect):**
```cpp
template <typename Func>
[[nodiscard]] inline float withBias(float x, float bias, Func func) noexcept {
    return func(x + bias);
}
```

**Corrected (spec-compliant):**
```cpp
template <typename Func>
[[nodiscard]] inline float withBias(float x, float bias, Func func) noexcept {
    return func(x + bias) - func(bias);
}
```

**Rationale**: The subtraction of `func(bias)` ensures DC neutrality - when input is zero, output is zero regardless of bias value.

### 2. Add DC Neutrality Test (MEDIUM PRIORITY)

**File**: `dsp/tests/unit/core/sigmoid_test.cpp`
**Location**: After line 402 (existing withBias tests)

```cpp
TEST_CASE("Asymmetric::withBias() maintains DC neutrality", "[sigmoid][core][US5]") {
    // FR-001: Zero input should produce zero output regardless of bias

    SECTION("zero input returns zero for any bias") {
        std::vector<float> biasValues = {-0.5f, -0.3f, -0.1f, 0.0f, 0.1f, 0.3f, 0.5f};
        for (float bias : biasValues) {
            float out = Asymmetric::withBias(0.0f, bias, Sigmoid::tanh);
            REQUIRE(out == Approx(0.0f).margin(1e-6f));
        }
    }

    SECTION("output is DC neutral for symmetric input patterns") {
        // Average of output over symmetric inputs should be near zero
        float bias = 0.3f;
        float sum = 0.0f;
        const int samples = 1000;
        for (int i = 0; i < samples; ++i) {
            float x = -1.0f + 2.0f * static_cast<float>(i) / static_cast<float>(samples - 1);
            sum += Asymmetric::withBias(x, bias, Sigmoid::tanh);
        }
        // Average should be small (not exactly zero due to asymmetry, but close)
        float avg = sum / static_cast<float>(samples);
        INFO("DC offset: " << avg);
        REQUIRE(std::abs(avg) < 0.1f);  // Reasonable DC, not the large offset from uncompensated bias
    }
}
```

### 3. Add Zero-Crossing Continuity Tests (MEDIUM PRIORITY)

**File**: `dsp/tests/unit/core/sigmoid_test.cpp`

```cpp
TEST_CASE("Asymmetric functions have continuous zero crossing", "[sigmoid][core][SC-003]") {
    // SC-003: No discontinuities at x=0 in transfer function

    const float epsilon = 1e-5f;

    SECTION("tube() is continuous at zero") {
        float atZero = Asymmetric::tube(0.0f);
        float justBelow = Asymmetric::tube(-epsilon);
        float justAbove = Asymmetric::tube(epsilon);

        // Values should be close to each other
        REQUIRE(std::abs(atZero - justBelow) < 0.001f);
        REQUIRE(std::abs(atZero - justAbove) < 0.001f);
    }

    SECTION("diode() is continuous at zero") {
        float atZero = Asymmetric::diode(0.0f);
        float justBelow = Asymmetric::diode(-epsilon);
        float justAbove = Asymmetric::diode(epsilon);

        REQUIRE(std::abs(atZero - justBelow) < 0.001f);
        REQUIRE(std::abs(atZero - justAbove) < 0.001f);
    }

    SECTION("dualCurve() is continuous at zero") {
        float atZero = Asymmetric::dualCurve(0.0f, 2.0f, 1.0f);
        float justBelow = Asymmetric::dualCurve(-epsilon, 2.0f, 1.0f);
        float justAbove = Asymmetric::dualCurve(epsilon, 2.0f, 1.0f);

        REQUIRE(std::abs(atZero - justBelow) < 0.001f);
        REQUIRE(std::abs(atZero - justAbove) < 0.001f);
    }
}
```

### 4. Add Edge Case Tests for diode() (MEDIUM PRIORITY)

**File**: `dsp/tests/unit/core/sigmoid_test.cpp`

```cpp
TEST_CASE("Asymmetric::diode() edge cases", "[sigmoid][core][edge]") {
    // FR-007: Numerical stability

    SECTION("handles denormal input") {
        float denormal = 1e-40f;
        REQUIRE(std::isfinite(Asymmetric::diode(denormal)));
        REQUIRE(std::isfinite(Asymmetric::diode(-denormal)));
    }

    SECTION("handles large positive input") {
        float large = 10.0f;
        float out = Asymmetric::diode(large);
        REQUIRE(out == Approx(1.0f).margin(0.001f));  // Saturates to ~1
    }

    SECTION("handles large negative input") {
        float large = -10.0f;
        float out = Asymmetric::diode(large);
        REQUIRE(std::isfinite(out));
        REQUIRE(out < 0.0f);  // Should be negative
    }
}
```

## Build and Test

```bash
# Build DSP tests
cmake --build build --config Release --target dsp_tests

# Run sigmoid tests
./build/bin/Release/dsp_tests "[sigmoid]"

# Run just asymmetric tests
./build/bin/Release/dsp_tests "[US5]"
```

## Verification Checklist

After implementation:

- [ ] `withBias()` formula matches spec: `func(x + bias) - func(bias)`
- [ ] DC neutrality test passes: zero input produces zero output
- [ ] Zero-crossing continuity tests pass for all functions
- [ ] Edge case tests pass (denormals, large values)
- [ ] All existing sigmoid tests still pass
- [ ] Build completes with zero warnings

## Files Modified

| File | Change |
|------|--------|
| `dsp/include/krate/dsp/core/sigmoid.h` | Fix `withBias()` formula |
| `dsp/tests/unit/core/sigmoid_test.cpp` | Add new tests |

## No Changes Required

The following are already correct:
- `Asymmetric::tube()` implementation
- `Asymmetric::diode()` implementation
- `Asymmetric::dualCurve()` implementation
- `SaturationProcessor` integration (already uses Asymmetric::)
- `ARCHITECTURE.md` documentation

## Success Criteria Verification

| ID | Test | Expected Result |
|----|------|-----------------|
| SC-001 | Harmonic tests | Pass (existing) |
| SC-002 | Bounds tests | Pass (existing) |
| SC-003 | Zero-crossing test | Pass (new) |
| SC-004 | CI on all platforms | Pass |
| SC-005 | SaturationProcessor | Already integrated |
| SC-006 | Performance | Meet Layer 0 budget |
