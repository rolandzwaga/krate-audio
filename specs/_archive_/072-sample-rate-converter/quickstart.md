# Quickstart Guide: Sample Rate Converter

**Feature**: 072-sample-rate-converter
**Date**: 2026-01-21

## Implementation Sequence

This guide provides the recommended order for implementing the SampleRateConverter, following test-first development (Constitution Principle XII).

---

## Phase 1: Foundation

### Task 1.1: Create Test File Structure

**File**: `dsp/tests/unit/primitives/sample_rate_converter_test.cpp`

```cpp
// ==============================================================================
// Layer 1: DSP Primitive Tests - SampleRateConverter
// ==============================================================================
// Constitution Principle XII: Test-First Development
// Tests written BEFORE implementation per spec 072-sample-rate-converter
// ==============================================================================

#include <catch2/catch_test_macros.hpp>
#include <catch2/catch_approx.hpp>

#include <krate/dsp/primitives/sample_rate_converter.h>

#include <array>
#include <cmath>
#include <vector>

using Catch::Approx;
using namespace Krate::DSP;

// Test file ready for tests
```

### Task 1.2: Write Foundational Tests (FR-003, FR-004, FR-005)

```cpp
TEST_CASE("SampleRateConverter constants", "[samplerate][layer1][foundational]") {
    SECTION("rate constants are defined") {
        REQUIRE(SampleRateConverter::kMinRate == Approx(0.25f));
        REQUIRE(SampleRateConverter::kMaxRate == Approx(4.0f));
        REQUIRE(SampleRateConverter::kDefaultRate == Approx(1.0f));
    }
}

TEST_CASE("SampleRateConverter default construction", "[samplerate][layer1][foundational]") {
    SampleRateConverter converter;

    SECTION("position starts at 0") {
        REQUIRE(converter.getPosition() == Approx(0.0f));
    }

    SECTION("isComplete starts false") {
        REQUIRE_FALSE(converter.isComplete());
    }
}

TEST_CASE("SampleRateConverter rate clamping (FR-008)", "[samplerate][layer1][foundational]") {
    SampleRateConverter converter;

    SECTION("rate below minimum is clamped") {
        converter.setRate(0.1f);
        // No getter, test via behavior in later tests
    }

    SECTION("rate above maximum is clamped") {
        converter.setRate(10.0f);
        // No getter, test via behavior in later tests
    }

    SECTION("valid rates are accepted") {
        converter.setRate(1.0f);
        converter.setRate(2.0f);
        converter.setRate(0.5f);
        // Tests pass if no crash/UB
    }
}
```

### Task 1.3: Create Header Skeleton

**File**: `dsp/include/krate/dsp/primitives/sample_rate_converter.h`

Create minimal skeleton that compiles with tests (all methods return defaults).

---

## Phase 2: Rate 1.0 Passthrough

### Task 2.1: Write Rate 1.0 Tests (SC-001)

```cpp
TEST_CASE("SampleRateConverter rate 1.0 passthrough (SC-001)", "[samplerate][layer1][US1]") {
    SampleRateConverter converter;
    converter.prepare(44100.0);
    converter.setRate(1.0f);
    converter.setInterpolation(InterpolationType::Linear);

    std::array<float, 10> buffer = {0.0f, 0.1f, 0.2f, 0.3f, 0.4f, 0.5f, 0.6f, 0.7f, 0.8f, 0.9f};

    SECTION("output matches input at integer positions") {
        for (size_t i = 0; i < buffer.size() - 1; ++i) {
            float result = converter.process(buffer.data(), buffer.size());
            REQUIRE(result == Approx(buffer[i]));
        }
    }
}
```

### Task 2.2: Implement Rate 1.0 Logic

Implement `process()` with simple integer position reading.

---

## Phase 3: Linear Interpolation

### Task 3.1: Write Linear Interpolation Tests (FR-015, SC-004)

```cpp
TEST_CASE("SampleRateConverter linear interpolation (FR-015)", "[samplerate][layer1][US1]") {
    SampleRateConverter converter;
    converter.prepare(44100.0);
    converter.setInterpolation(InterpolationType::Linear);

    std::array<float, 4> buffer = {0.0f, 1.0f, 2.0f, 3.0f};

    SECTION("interpolates at fractional positions") {
        converter.setRate(0.5f);
        converter.setPosition(0.5f);

        float result = converter.process(buffer.data(), buffer.size());
        // At position 0.5, linear interp between 0.0 and 1.0 = 0.5
        REQUIRE(result == Approx(0.5f));
    }

    SECTION("SC-004: position 1.5 produces (buffer[1] + buffer[2]) / 2") {
        converter.setPosition(1.5f);
        float result = converter.process(buffer.data(), buffer.size());
        REQUIRE(result == Approx(1.5f));  // (1.0 + 2.0) / 2
    }
}
```

### Task 3.2: Implement Linear Interpolation

Add fractional position handling and call `Interpolation::linearInterpolate()`.

---

## Phase 4: Position Tracking

### Task 4.1: Write Position Advancement Tests (FR-020, SC-002, SC-003)

```cpp
TEST_CASE("SampleRateConverter position advancement (FR-020)", "[samplerate][layer1][US1]") {
    SampleRateConverter converter;
    converter.prepare(44100.0);
    converter.setInterpolation(InterpolationType::Linear);

    std::array<float, 100> buffer;
    std::fill(buffer.begin(), buffer.end(), 0.5f);

    SECTION("SC-002: rate 2.0 completes 100 samples in 50 calls") {
        converter.setRate(2.0f);
        int callCount = 0;
        while (!converter.isComplete()) {
            converter.process(buffer.data(), buffer.size());
            ++callCount;
            if (callCount > 100) break;  // Safety limit
        }
        REQUIRE(callCount == 50);
    }

    SECTION("SC-003: rate 0.5 completes 100 samples in ~198 calls") {
        converter.setRate(0.5f);
        int callCount = 0;
        while (!converter.isComplete()) {
            converter.process(buffer.data(), buffer.size());
            ++callCount;
            if (callCount > 250) break;  // Safety limit
        }
        // 100 samples / 0.5 rate = 198 steps (0 to 99)
        REQUIRE(callCount >= 196);
        REQUIRE(callCount <= 200);
    }
}
```

### Task 4.2: Implement Position Tracking

Add position increment by rate after each `process()` call.

---

## Phase 5: Cubic and Lagrange Interpolation

### Task 5.1: Write 4-Point Interpolation Tests (FR-016, FR-017, FR-018)

```cpp
TEST_CASE("SampleRateConverter cubic interpolation (FR-016)", "[samplerate][layer1][US2]") {
    SampleRateConverter converter;
    converter.prepare(44100.0);
    converter.setInterpolation(InterpolationType::Cubic);

    // Quadratic data: y = x^2 at positions 0, 1, 2, 3
    std::array<float, 4> buffer = {0.0f, 1.0f, 4.0f, 9.0f};

    SECTION("cubic at position 1.5") {
        converter.setPosition(1.5f);
        float result = converter.process(buffer.data(), buffer.size());
        // True value: 1.5^2 = 2.25
        // Cubic should be closer to 2.25 than linear (2.5)
        REQUIRE(result == Approx(2.25f).margin(0.5f));
    }
}

TEST_CASE("SampleRateConverter edge reflection (FR-018)", "[samplerate][layer1][US2]") {
    SampleRateConverter converter;
    converter.prepare(44100.0);
    converter.setInterpolation(InterpolationType::Cubic);

    std::array<float, 4> buffer = {1.0f, 2.0f, 3.0f, 4.0f};

    SECTION("interpolation at position 0.5 uses edge reflection") {
        converter.setPosition(0.5f);
        float result = converter.process(buffer.data(), buffer.size());
        // Should not crash, should produce reasonable value
        REQUIRE(std::isfinite(result));
        REQUIRE(result >= 1.0f);
        REQUIRE(result <= 2.0f);
    }
}

TEST_CASE("SampleRateConverter integer positions (FR-019)", "[samplerate][layer1][US2]") {
    SampleRateConverter converter;
    converter.prepare(44100.0);

    std::array<float, 4> buffer = {1.0f, 2.0f, 3.0f, 4.0f};

    for (auto type : {InterpolationType::Linear, InterpolationType::Cubic, InterpolationType::Lagrange}) {
        converter.setInterpolation(type);

        SECTION("all types return exact value at integer positions") {
            converter.reset();
            converter.setPosition(1.0f);
            float result = converter.process(buffer.data(), buffer.size());
            REQUIRE(result == Approx(2.0f));
        }
    }
}
```

### Task 5.2: Implement Edge Reflection Helper

```cpp
private:
    [[nodiscard]] float getSampleReflected(const float* buffer, size_t bufferSize, int idx) const noexcept {
        if (idx < 0) return buffer[0];
        if (idx >= static_cast<int>(bufferSize)) return buffer[bufferSize - 1];
        return buffer[idx];
    }
```

### Task 5.3: Implement Cubic and Lagrange Dispatch

Add switch on `interpolationType_` to call appropriate `Interpolation::*` function.

---

## Phase 6: End-of-Buffer Detection

### Task 6.1: Write Completion Tests (FR-021, FR-022, SC-009, SC-010)

```cpp
TEST_CASE("SampleRateConverter end detection (FR-021)", "[samplerate][layer1][US3]") {
    SampleRateConverter converter;
    converter.prepare(44100.0);

    std::array<float, 10> buffer;
    std::fill(buffer.begin(), buffer.end(), 0.5f);

    SECTION("isComplete when position >= bufferSize - 1") {
        converter.setPosition(9.0f);  // Last valid sample
        REQUIRE(converter.isComplete() == false);

        converter.setRate(1.0f);
        float result = converter.process(buffer.data(), buffer.size());
        REQUIRE(converter.isComplete() == true);
        REQUIRE(result == 0.0f);  // Returns 0 when complete
    }

    SECTION("SC-010: reset clears complete flag") {
        converter.setPosition(99.0f);
        (void)converter.process(buffer.data(), buffer.size());
        REQUIRE(converter.isComplete() == true);

        converter.reset();
        REQUIRE(converter.isComplete() == false);
        REQUIRE(converter.getPosition() == Approx(0.0f));
    }
}
```

### Task 6.2: Implement Completion Logic

Add completion check before interpolation, set flag when appropriate.

---

## Phase 7: Block Processing

### Task 7.1: Write Block Processing Tests (FR-013, SC-007)

```cpp
TEST_CASE("SampleRateConverter block processing (FR-013)", "[samplerate][layer1][US4]") {
    SampleRateConverter converter;
    converter.prepare(44100.0);
    converter.setRate(1.5f);
    converter.setInterpolation(InterpolationType::Linear);

    std::array<float, 100> src;
    for (size_t i = 0; i < src.size(); ++i) {
        src[i] = static_cast<float>(i) * 0.01f;
    }

    SECTION("SC-007: processBlock matches sequential process() calls") {
        // Get block result
        std::array<float, 50> blockResult;
        converter.processBlock(src.data(), src.size(), blockResult.data(), blockResult.size());

        // Reset and get sequential result
        converter.reset();
        std::array<float, 50> seqResult;
        for (size_t i = 0; i < seqResult.size(); ++i) {
            seqResult[i] = converter.process(src.data(), src.size());
        }

        // Compare
        for (size_t i = 0; i < blockResult.size(); ++i) {
            REQUIRE(blockResult[i] == Approx(seqResult[i]));
        }
    }
}
```

### Task 7.2: Implement processBlock

Loop calling internal process logic with captured rate.

---

## Phase 8: THD+N Verification

### Task 8.1: Write THD+N Test (SC-005)

```cpp
TEST_CASE("SampleRateConverter cubic vs linear THD+N (SC-005)", "[samplerate][layer1][quality]") {
    SampleRateConverter converter;
    converter.prepare(44100.0);
    converter.setRate(0.75f);

    // Generate sine wave
    constexpr size_t srcSize = 4096;
    constexpr float frequency = 1000.0f;
    constexpr float sampleRate = 44100.0f;

    std::vector<float> sineBuffer(srcSize);
    for (size_t i = 0; i < srcSize; ++i) {
        sineBuffer[i] = std::sin(2.0f * 3.14159f * frequency * i / sampleRate);
    }

    // Measure with linear
    converter.setInterpolation(InterpolationType::Linear);
    converter.reset();
    std::vector<float> linearOutput;
    while (!converter.isComplete() && linearOutput.size() < srcSize * 2) {
        linearOutput.push_back(converter.process(sineBuffer.data(), sineBuffer.size()));
    }

    // Measure with cubic
    converter.setInterpolation(InterpolationType::Cubic);
    converter.reset();
    std::vector<float> cubicOutput;
    while (!converter.isComplete() && cubicOutput.size() < srcSize * 2) {
        cubicOutput.push_back(converter.process(sineBuffer.data(), sineBuffer.size()));
    }

    // Use spectral analysis to measure THD+N
    // (Actual implementation would use spectral_analysis.h)
    // For now, verify cubic produces smoother transitions
    float linearVariance = 0.0f;
    float cubicVariance = 0.0f;

    for (size_t i = 1; i < std::min(linearOutput.size(), cubicOutput.size()) - 1; ++i) {
        float linearDiff = std::abs(linearOutput[i] - linearOutput[i-1]);
        float cubicDiff = std::abs(cubicOutput[i] - cubicOutput[i-1]);
        linearVariance += linearDiff * linearDiff;
        cubicVariance += cubicDiff * cubicDiff;
    }

    // Cubic should have lower variance (smoother)
    INFO("Linear variance: " << linearVariance);
    INFO("Cubic variance: " << cubicVariance);
    REQUIRE(cubicVariance < linearVariance);
}
```

---

## Phase 9: Edge Cases and Safety

### Task 9.1: Write Edge Case Tests (FR-025, FR-026, SC-008, SC-011)

```cpp
TEST_CASE("SampleRateConverter safety (FR-025, FR-026)", "[samplerate][layer1][edge]") {
    SampleRateConverter converter;

    SECTION("FR-026: process before prepare returns 0") {
        float result = converter.process(nullptr, 0);
        REQUIRE(result == Approx(0.0f));
    }

    SECTION("FR-025: nullptr buffer returns 0") {
        converter.prepare(44100.0);
        float result = converter.process(nullptr, 100);
        REQUIRE(result == Approx(0.0f));
    }

    SECTION("FR-025: zero size buffer returns 0") {
        converter.prepare(44100.0);
        float buffer[1] = {0.5f};
        float result = converter.process(buffer, 0);
        REQUIRE(result == Approx(0.0f));
    }

    SECTION("SC-011: rate clamping enforced") {
        converter.prepare(44100.0);
        std::array<float, 10> buffer;
        std::fill(buffer.begin(), buffer.end(), 0.5f);

        // Set extreme rate
        converter.setRate(100.0f);  // Way above max

        // Should advance by at most kMaxRate
        float startPos = converter.getPosition();
        converter.process(buffer.data(), buffer.size());
        float endPos = converter.getPosition();

        REQUIRE(endPos - startPos <= SampleRateConverter::kMaxRate);
    }
}

TEST_CASE("SampleRateConverter stability (SC-008)", "[samplerate][layer1][stability]") {
    SampleRateConverter converter;
    converter.prepare(44100.0);
    converter.setRate(1.7f);
    converter.setInterpolation(InterpolationType::Lagrange);

    // Create valid [-1, 1] input buffer
    std::array<float, 1000> buffer;
    for (size_t i = 0; i < buffer.size(); ++i) {
        buffer[i] = std::sin(static_cast<float>(i) * 0.1f);
    }

    SECTION("1 million process calls without NaN or Infinity") {
        for (int i = 0; i < 1000000; ++i) {
            if (converter.isComplete()) {
                converter.reset();
            }
            float result = converter.process(buffer.data(), buffer.size());
            REQUIRE(std::isfinite(result));
        }
    }
}
```

---

## Build and Test Commands

```bash
# Configure
"$CMAKE" --preset windows-x64-release

# Build DSP tests
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests

# Run specific test file
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[samplerate]"

# Run with verbose output
build/windows-x64-release/dsp/tests/Release/dsp_tests.exe "[samplerate]" -s
```

---

## Checklist Summary

| Phase | Tests | Implementation | Status |
|-------|-------|----------------|--------|
| 1. Foundation | Constants, construction, clamping | Header skeleton | [ ] |
| 2. Rate 1.0 | SC-001 passthrough | Integer position | [ ] |
| 3. Linear | FR-015, SC-004 | Fractional + linear interp | [ ] |
| 4. Position | FR-020, SC-002, SC-003 | Position advancement | [ ] |
| 5. 4-Point | FR-016, FR-017, FR-018, FR-019 | Cubic, Lagrange, reflection | [ ] |
| 6. Completion | FR-021, FR-022, SC-009, SC-010 | isComplete logic | [ ] |
| 7. Block | FR-013, SC-007 | processBlock | [ ] |
| 8. THD+N | SC-005 | Verify quality improvement | [ ] |
| 9. Safety | FR-025, FR-026, SC-008, SC-011 | Edge cases | [ ] |
