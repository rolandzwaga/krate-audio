---
name: testing-guide
description: VST3 plugin testing patterns and practices. Use when writing tests, debugging test failures, adding new test cases, when tests don't appear after changes, or when working with DSP algorithms that need verification. Covers build verification, DSP testing strategies, Catch2 patterns, and VST3 validation.
allowed-tools: Read, Grep, Glob, Bash
---

# Testing Guide for VST Plugin Development

This skill provides comprehensive guidelines for writing effective tests for this VST3 plugin project.

## Quick Reference

- **Build commands**: See [QUICK-START.md](QUICK-START.md)
- **DSP testing strategies**: See [DSP-TESTING.md](DSP-TESTING.md)
- **VST3-specific testing**: See [VST3-TESTING.md](VST3-TESTING.md)
- **Catch2 patterns & test doubles**: See [PATTERNS.md](PATTERNS.md)
- **Anti-patterns to avoid**: See [ANTI-PATTERNS.md](ANTI-PATTERNS.md)

---

## CRITICAL: Build-Before-Test Discipline

**Tests cannot run if the code doesn't compile. Period.**

### The Mandatory Workflow

After ANY code changes:

1. **Build the test target**:
   ```bash
   cmake --build build --config Debug --target dsp_tests
   ```

2. **Check for compilation errors** - Look for `error` in build output

3. **Fix errors BEFORE running tests**

4. **Only then run tests**

### When Tests Don't Appear

> **STOP. DID YOU CHECK THE BUILD OUTPUT?**
>
> If you just added new tests and they don't appear:
> 1. Your code probably doesn't compile
> 2. Run: `cmake --build build --config Debug --target dsp_tests 2>&1`
> 3. Look for `error` in the output
> 4. **FIX THE COMPILATION ERRORS FIRST**
>
> Do NOT:
> - Try different test filters
> - Blame CMake cache
> - Run `--list-tests` repeatedly

**The build output tells you EXACTLY what's wrong. Read it.**

---

## Test Suite Architecture

The project has **two separate test executables**:

| Executable | Purpose | SDK Dependencies |
|------------|---------|------------------|
| `dsp_tests` | Pure DSP algorithm tests | None (Catch2 only) |
| `vst_tests` | VST3-specific functionality tests | VST3 SDK |

### Why Two Suites?

1. **Build speed**: `dsp_tests` compiles faster without VST3 SDK headers
2. **Isolation**: DSP algorithms remain testable without VST3 infrastructure
3. **Layered architecture**: Enforces that Layer 0-4 DSP code has no VST3 dependencies

### Test Locations

```
tests/
├── unit/
│   ├── core/           # Layer 0 tests (→ dsp_tests)
│   ├── primitives/     # Layer 1 tests (→ dsp_tests)
│   ├── processors/     # Layer 2 tests (→ dsp_tests)
│   ├── systems/        # Layer 3 tests (→ dsp_tests)
│   ├── features/       # Layer 4 tests (→ dsp_tests)
│   └── vst/            # VST3 SDK tests (→ vst_tests)
├── integration/        # Integration tests
└── regression/         # Golden master tests
```

### Test Helpers

- **Signal generators**: `tests/test_helpers/test_signals.h`
- **Buffer comparison**: `tests/test_helpers/buffer_comparison.h`
- **Allocation detector**: `tests/test_helpers/allocation_detector.h`

---

## Core Testing Philosophy

### Test Behavior, Not Implementation

The most critical principle: **tests should verify what the code does, not how it does it**.

```cpp
// BAD: Tests implementation details
TEST_CASE("DelayLine uses circular buffer internally", "[bad]") {
    DelayLine delay(1000);
    REQUIRE(delay.getWriteIndex() == 0);  // Fragile!
}

// GOOD: Tests behavior
TEST_CASE("DelayLine returns samples at specified delay", "[delay]") {
    DelayLine delay(1000);
    delay.write(0.5f);
    for (int i = 0; i < 10; ++i) delay.write(0.0f);
    REQUIRE(delay.read(10) == Approx(0.5f));
}
```

### Benefits

| Aspect | Implementation Tests | Behavioral Tests |
|--------|---------------------|------------------|
| **Refactoring** | Break frequently | Survive changes |
| **Readability** | Obscure intent | Document behavior |
| **Maintenance** | High burden | Low burden |

---

## Test Categories

### Unit Tests
Test individual DSP functions and primitives in isolation.
**Scope:** Layer 0 (Core) and Layer 1 (Primitives)

```cpp
TEST_CASE("dBToLinear converts correctly", "[dsp][unit]") {
    REQUIRE(dBToLinear(0.0f) == Approx(1.0f));
    REQUIRE(dBToLinear(-6.0206f) == Approx(0.5f).margin(0.001f));
}
```

### Component Tests
Test composed DSP processors with their dependencies.
**Scope:** Layer 2 (Processors) and Layer 3 (Systems)

### Integration Tests
Test complete signal paths through the plugin.
**Scope:** Full plugin, VST3 host interaction

### Regression Tests
Capture known-good outputs for comparison (golden masters).

---

## Essential Best Practices

### 1. Use Arrange-Act-Assert Pattern

```cpp
TEST_CASE("Filter attenuates above cutoff", "[dsp][filter]") {
    // ARRANGE
    BiquadFilter filter;
    filter.setLowpass(1000.0f, 44100.0f, 0.707f);
    std::array<float, 1024> buffer;
    generateSineWave(buffer.data(), 1024, 5000.0f, 44100.0f);

    // ACT
    filter.process(buffer.data(), 1024);

    // ASSERT
    float outputPeak = findPeak(buffer.data(), 1024);
    REQUIRE(outputPeak < 0.5f);  // At least -6dB attenuation
}
```

### 2. Use Descriptive Test Names

```cpp
// GOOD
TEST_CASE("OnePoleSmoother reaches target within 5 time constants", "[dsp][smoother]")
TEST_CASE("DelayLine handles zero-length buffer without crash", "[dsp][delay][edge]")

// BAD
TEST_CASE("testSmoother", "[dsp]")
TEST_CASE("test1", "[dsp]")
```

### 3. Test Edge Cases

```cpp
TEST_CASE("DelayLine handles edge cases", "[dsp][delay][edge]") {
    DelayLine delay(100);

    SECTION("zero delay returns current sample") {
        delay.write(0.5f);
        REQUIRE(delay.read(0) == Approx(0.5f));
    }

    SECTION("maximum delay returns oldest sample") {
        for (int i = 0; i < 100; ++i) delay.write(static_cast<float>(i));
        REQUIRE(delay.read(99) == Approx(0.0f));
    }
}
```

### 4. Test Failure Modes

```cpp
TEST_CASE("calculateRMS handles invalid input gracefully", "[dsp][analysis]") {
    SECTION("null pointer returns zero") {
        REQUIRE(calculateRMS(nullptr, 100) == 0.0f);
    }

    SECTION("zero length returns zero") {
        float buffer[4] = {1.0f, 1.0f, 1.0f, 1.0f};
        REQUIRE(calculateRMS(buffer, 0) == 0.0f);
    }
}
```

---

## Test Tags

Use consistent tags for filtering:

```cpp
// Layer tags
[core]       // Layer 0
[primitives] // Layer 1
[processors] // Layer 2
[systems]    // Layer 3
[features]   // Layer 4

// Category tags
[dsp]        // DSP algorithm
[vst3]       // VST3 specific
[state]      // State save/restore
[edge]       // Edge case
[regression] // Regression test

// Speed tags
[fast]       // < 100ms
[slow]       // > 1s
```

Run specific tests:
```bash
./dsp_tests "[dsp][fast]"    # Fast DSP tests
./dsp_tests "~[slow]"        # Exclude slow tests
./dsp_tests "[filter]"       # Filter tests only
```

---

## Floating-Point Comparison

| Situation | Strategy | Example |
|-----------|----------|---------|
| Known exact value | Direct | `REQUIRE(x == 0.0f)` |
| Approximate | Margin | `Approx(expected).margin(1e-5f)` |
| Relative precision | Epsilon | `Approx(expected).epsilon(0.01f)` |
| Near zero | Margin only | `Approx(0.0f).margin(1e-7f)` |

```cpp
// Default epsilon (scale-dependent)
REQUIRE(value == Approx(expected));

// Custom margin (absolute tolerance)
REQUIRE(value == Approx(expected).margin(0.001f));

// For near-zero values, use margin
REQUIRE(nearZeroValue == Approx(0.0f).margin(1e-6f));
```

---

## Guard Rail Tests

Ensure DSP code doesn't produce invalid output:

```cpp
TEST_CASE("DSP output contains no NaN or Inf", "[dsp][safety]") {
    Saturator sat;
    sat.prepare(44100.0, 512);
    sat.setDrive(10.0f);  // Extreme setting

    std::array<float, 512> buffer;

    SECTION("normal input") {
        generateSine(buffer.data(), 512, 440.0f, 44100.0f);
        sat.process(buffer.data(), 512);

        for (float sample : buffer) {
            REQUIRE_FALSE(std::isnan(sample));
            REQUIRE_FALSE(std::isinf(sample));
        }
    }

    SECTION("extreme input") {
        std::fill(buffer.begin(), buffer.end(), 1000.0f);
        sat.process(buffer.data(), 512);

        for (float sample : buffer) {
            REQUIRE_FALSE(std::isnan(sample));
            REQUIRE(std::abs(sample) <= 2.0f);
        }
    }
}
```

---

## Pre-Commit Checklist

Before committing code:
1. All unit tests pass
2. No new compiler warnings
3. `vstvalidator` passes on built plugin (if plugin code changed)
4. Code coverage hasn't decreased

---

## Additional Resources

For detailed information, see the supporting files:

- [QUICK-START.md](QUICK-START.md) - Build commands, run commands, troubleshooting
- [DSP-TESTING.md](DSP-TESTING.md) - Test signals, THD measurement, frequency estimation, deterministic RNG
- [VST3-TESTING.md](VST3-TESTING.md) - Validator, state tests, processor/controller separation
- [PATTERNS.md](PATTERNS.md) - Catch2 patterns, test doubles, UI testing with mocks
- [ANTI-PATTERNS.md](ANTI-PATTERNS.md) - Common mistakes and how to avoid them
