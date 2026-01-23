# Testing Anti-Patterns to Avoid

## 1. The Mockery

**Problem:** Test uses so many mocks that it tests mock behavior, not real code.

```cpp
// BAD: Testing mocks, not the actual implementation
TEST_CASE("Processor processes audio", "[bad]") {
    MockParameterHandler mockParams;
    MockAudioBuffer mockInput;
    MockAudioBuffer mockOutput;
    MockDSPEngine mockDSP;

    EXPECT_CALL(mockParams, getGain()).WillReturn(0.5f);
    EXPECT_CALL(mockDSP, process(_, _)).WillReturn(true);

    Processor proc(mockParams, mockDSP);
    proc.process(mockInput, mockOutput);

    // What did we actually test? Just that mocks were called.
}

// GOOD: Test real DSP with real data
TEST_CASE("Processor applies gain correctly", "[processor]") {
    Processor proc;
    proc.prepare(44100.0, 512);
    proc.setParameter(kGainId, 0.5f);

    std::array<float, 512> buffer;
    std::fill(buffer.begin(), buffer.end(), 1.0f);

    proc.processBlock(buffer.data(), 512);

    REQUIRE(buffer[0] == Approx(0.5f));
}
```

---

## 2. The Inspector

**Problem:** Test violates encapsulation to achieve coverage.

```cpp
// BAD: Accessing private members, testing implementation
TEST_CASE("LFO internal phase increments", "[bad]") {
    LFO lfo;
    lfo.setFrequency(1.0f, 44100.0f);

    REQUIRE(lfo.phase_ == 0.0f);  // Accessing private - fragile!
    lfo.process();
    REQUIRE(lfo.phase_ == Approx(1.0f / 44100.0f));
}

// GOOD: Test observable behavior
TEST_CASE("LFO completes one cycle at specified frequency", "[dsp][lfo]") {
    LFO lfo;
    lfo.setFrequency(1.0f, 100.0f);  // 1 Hz at 100 samples/sec

    float startValue = lfo.process();

    // Process one full cycle
    for (int i = 1; i < 100; ++i) {
        lfo.process();
    }

    // Should return to approximately the same value
    REQUIRE(lfo.process() == Approx(startValue).margin(0.01f));
}
```

---

## 3. The Happy Path Only

**Problem:** Tests only the successful case, missing error conditions.

```cpp
// BAD: Only tests success
TEST_CASE("loadPreset loads preset", "[preset]") {
    PresetManager pm;
    pm.loadPreset("valid_preset.json");
    REQUIRE(pm.isLoaded());
}

// GOOD: Tests error handling too
TEST_CASE("loadPreset handles missing files", "[preset]") {
    PresetManager pm;

    SECTION("returns false for missing file") {
        REQUIRE_FALSE(pm.loadPreset("nonexistent.json"));
    }

    SECTION("returns false for corrupted file") {
        REQUIRE_FALSE(pm.loadPreset("corrupted.json"));
    }

    SECTION("previous state preserved on failure") {
        pm.loadPreset("valid_preset.json");
        float originalValue = pm.getParameter("delay_time");

        pm.loadPreset("nonexistent.json");
        REQUIRE(pm.getParameter("delay_time") == originalValue);
    }
}
```

---

## 4. The Flaky Test

**Problem:** Test fails intermittently due to timing, randomness, or external dependencies.

```cpp
// BAD: Depends on timing
TEST_CASE("async operation completes", "[bad]") {
    AsyncLoader loader;
    loader.startLoading();

    std::this_thread::sleep_for(std::chrono::milliseconds(100));  // Race!

    REQUIRE(loader.isComplete());
}

// GOOD: Use deterministic synchronization or test synchronously
TEST_CASE("loader processes all items", "[loader]") {
    SyncLoader loader;  // Synchronous test double
    loader.loadAll(testItems);

    REQUIRE(loader.processedCount() == testItems.size());
}
```

For DSP with randomness, see [DSP-TESTING.md](DSP-TESTING.md#deterministic-testing-of-dsp-with-randomness).

---

## 5. The Coverage Chaser

**Problem:** Writing tests just to hit coverage numbers, not to verify behavior.

```cpp
// BAD: Meaningless test that inflates coverage
TEST_CASE("constructor constructs", "[bad]") {
    Filter f;
    REQUIRE(true);  // Pointless
}

// BAD: Testing getters/setters with no logic
TEST_CASE("setGain sets gain", "[bad]") {
    Processor p;
    p.setGain(0.5f);
    REQUIRE(p.getGain() == 0.5f);  // Tests nothing meaningful
}

// GOOD: Test meaningful behavior
TEST_CASE("Gain parameter is smoothed to prevent clicks", "[processor]") {
    Processor p;
    p.prepare(44100.0, 64);
    p.setGain(0.0f);
    p.processBlock(silence, 64);  // Let smoother settle

    p.setGain(1.0f);
    std::array<float, 64> output;
    std::fill(output.begin(), output.end(), 1.0f);
    p.processBlock(output.data(), 64);

    // First sample should NOT be at full gain (smoothing)
    REQUIRE(output[0] < 0.5f);
    // Last sample should be closer to target
    REQUIRE(output[63] > output[0]);
}
```

---

## 6. The Copy-Paste Test

**Problem:** Duplicated test code that becomes inconsistent.

```cpp
// BAD: Copy-paste with slight modifications
TEST_CASE("lowpass at 1kHz", "[filter]") {
    Filter f;
    f.setLowpass(1000.0f, 44100.0f);
    // ... 20 lines of test code ...
}

TEST_CASE("lowpass at 2kHz", "[filter]") {
    Filter f;
    f.setLowpass(2000.0f, 44100.0f);  // Only this changed
    // ... same 20 lines of test code ...
}

// GOOD: Parameterized test or shared setup
TEST_CASE("Lowpass attenuates above cutoff", "[filter]") {
    const std::array<float, 3> cutoffs = {500.0f, 1000.0f, 2000.0f};

    for (float cutoff : cutoffs) {
        DYNAMIC_SECTION("cutoff: " << cutoff << " Hz") {
            Filter f;
            f.setLowpass(cutoff, 44100.0f);

            auto response = measureFrequencyResponse(f, cutoff * 2);
            REQUIRE(response < -6.0f);
        }
    }
}
```

---

## 7. The Accumulator

**Problem:** Test accumulates many floating-point operations expecting exact results.

```cpp
// BAD: 48000 float additions, then expecting exact result
TEST_CASE("LFO completes one cycle", "[bad]") {
    constexpr float sampleRate = 48000.0f;
    const float phaseIncrement = kTwoPi / sampleRate;

    float phase = 0.0f;
    for (int i = 0; i < static_cast<int>(sampleRate); ++i) {
        phase += phaseIncrement;  // 48000 additions!
    }

    // FAILS! Error accumulates to ~0.004
    REQUIRE(phase == Approx(kTwoPi).margin(1e-4f));
}

// GOOD: Test single-operation precision instead
TEST_CASE("LFO phase increment is precise", "[dsp][lfo]") {
    constexpr float lfoFreq = 1.0f;
    constexpr float sampleRate = 48000.0f;
    const float phaseIncrement = kTwoPi * lfoFreq / sampleRate;

    // Single operation - tests constant precision
    REQUIRE(phaseIncrement * sampleRate == Approx(kTwoPi).margin(1e-5f));

    // Verify sin/cos at calculated positions
    REQUIRE(std::sin(phaseIncrement * 12000) == Approx(1.0f).margin(1e-5f));
}
```

**Key insight:** Distinguish between testing *constant precision* (single operations) and testing *implementation robustness* (verify code handles accumulation via wrapping).

---

## 8. Mocking Complexity as an Excuse

> **THIS IS NON-NEGOTIABLE**
>
> "Mocking is complex" or "would require mocking VSTGUI" is **NEVER** a valid reason to skip tests.

### If You Find Yourself Thinking:
- "This is just UI code, it doesn't need tests"
- "Testing this would require too much mocking"
- "The logic is simple enough to verify manually"

**STOP. You are about to ship a bug.**

### The Solution Is Always One Of:
1. Extract pure logic into testable functions (humble object pattern)
2. Create minimal mocks/fakes for framework dependencies
3. Write integration tests that exercise real components
4. All of the above

See [PATTERNS.md](PATTERNS.md#the-humble-object-pattern-for-ui) for examples.

---

## 9. Tests That Manually Set State (Framework Anti-Pattern)

**This anti-pattern caused a shipped bug that passed all tests.**

### The Problem

When testing UI code, it's tempting to manually set internal state and then call the method under test. This is **WRONG** because it doesn't simulate real framework behavior.

### Example of BROKEN Test

```cpp
// WRONG: This test passes but the implementation is broken!
TEST_CASE("selection toggle", "[ui]") {
    PresetDataSource dataSource;

    SECTION("clicking already-selected row triggers deselect") {
        // Manually set internal state - BYPASSES REAL FRAMEWORK BEHAVIOR
        dataSource.setPreviousSelectedRowForTesting(2);

        auto result = dataSource.handleMouseDownForTesting(2, false);

        REQUIRE(result.shouldDeselect);  // Test passes!
    }
}
```

**Why this is broken:** In the REAL framework:
1. CDataBrowser calls `setSelectedRow(2)` FIRST
2. This triggers `dbSelectionChanged()` which updates `previousSelectedRow_ = 2`
3. THEN CDataBrowser calls `dbOnMouseDown(row=2)`
4. By step 3, `previousSelectedRow_` is ALREADY 2
5. **Every first click immediately deselects!**

### The CORRECT Approach

```cpp
// CORRECT: Simulate the ACTUAL framework behavior
TEST_CASE("selection toggle with REAL call order", "[ui]") {
    PresetDataSource dataSource;

    SECTION("first click should select, not deselect") {
        // Capture selection BEFORE the click
        dataSource.capturePreClickSelection(-1);

        auto result = dataSource.handleMouseDownForTesting(0, false);

        // First click should allow selection, NOT deselect!
        REQUIRE_FALSE(result.shouldDeselect);
    }
}
```

### How to Avoid This

1. **READ THE FRAMEWORK SOURCE CODE** - Understand actual call order
2. **Document the real call order** in your test file
3. **Add integration points** that match the real flow
4. **When tests pass but manual testing fails, THE TESTS ARE WRONG**

---

## Summary: Test Smells Checklist

Before committing, check for these smells:

| Smell | What to Look For | Fix |
|-------|------------------|-----|
| Mockery | More mock setup than test code | Use fakes or test real code |
| Inspector | Accessing `private_` members | Test observable behavior |
| Happy Path | No SECTION for error cases | Add failure mode tests |
| Flaky | `sleep()` or random values | Use sync or seed RNG |
| Coverage Chaser | `REQUIRE(true)` or trivial getters | Test meaningful behavior |
| Copy-Paste | Duplicated test code | Use DYNAMIC_SECTION |
| Accumulator | Loops with tight tolerance | Test single operations |
| Excuses | "Too complex to test" | Extract pure logic |
| Manual State | `setXForTesting()` before call | Simulate real call order |
| CLI-Hostile Names | Test names starting with `-`, `+`, `--` | Use descriptive words |
| Relaxing on Crash | Loosening thresholds when tests crash | Use `detail::isNaN()`/`detail::isInf()` |
| Using std::isfinite | `std::isnan()`, `std::isfinite()`, `std::isinf()` | Use bit-level checks from `db_utils.h` |
| Build Paranoia | Rebuilding when tests ran successfully | Trust the output; only `--clean-first` if tests don't appear |

---

## 10. The CLI-Hostile Test Name

**Problem:** Test names starting with special characters (`-`, `+`, `--`, `/`) are interpreted as command-line flags by the test runner on some platforms (especially Linux).

```cpp
// BAD: Test names that look like CLI flags
TEST_CASE("-Infinity input handled gracefully", "[edge]") { ... }
TEST_CASE("+Infinity input clamps to threshold", "[edge]") { ... }
TEST_CASE("--verbose mode enables logging", "[config]") { ... }

// On Linux, running these produces:
// Error(s) in input:
//   Unrecognised token: -Infinity
// Run with -? for usage

// GOOD: Use descriptive words instead of symbols
TEST_CASE("Negative infinity input handled gracefully", "[edge]") { ... }
TEST_CASE("Positive infinity input clamps to threshold", "[edge]") { ... }
TEST_CASE("Verbose flag enables logging", "[config]") { ... }
```

### Why This Happens

Catch2 test discovery uses `--list-tests` and test selection uses regex matching. When CTest or the test runner parses test names, leading `-` or `+` characters can be misinterpreted as:
- Command-line options (`-v`, `--help`)
- Numeric arguments (`+5`, `-10`)

This often passes on Windows but fails on Linux/macOS where the shell handles arguments differently.

### Rules for Test Names

1. **Never start with `-`, `+`, `--`, or `/`**
2. **Use words:** "Negative" not "-", "Positive" not "+"
3. **Describe the scenario:** What is being tested, not the literal value
4. **If testing special values, name them:** "NaN", "Infinity", "MinFloat"

### Quick Fix Pattern

| Instead of... | Use... |
|---------------|--------|
| `-Infinity` | `Negative infinity` |
| `+Infinity` | `Positive infinity` |
| `-1.0 input` | `Negative one input` |
| `+0.0 vs -0.0` | `Positive zero vs negative zero` |
| `--flag` | `Double-dash flag` or `Verbose flag`|

---

## 11. Relaxing Test Expectations When Tests Crash

> **THIS IS A CRITICAL MISTAKE**
>
> When a test CRASHES (non-zero exit code, SIGFPE, SIGABRT, etc.), the solution is **NEVER** to relax test expectations or thresholds.

### The Scenario

CI reports a test failure with an exit code (e.g., exit code 8 on macOS). You think "maybe the threshold is too tight on this platform" and relax the test:

```cpp
// WRONG: Test is crashing, so you "fix" it by loosening thresholds
// Before:
REQUIRE(reductionDb >= 20.0f);

// "Fix":
REQUIRE(reductionDb >= 19.5f);  // "margin for float precision"
```

**This is completely wrong.** A crash is not a threshold issue - it's a fatal error in the code.

### Why This Is Wrong

| Symptom | What You Think | What's Actually Happening |
|---------|----------------|---------------------------|
| Exit code 8 on macOS | "Float precision is different" | Code is crashing (SIGFPE, invalid operation) |
| Test "fails" on Linux | "Thresholds need adjustment" | `std::isnan()` doesn't work with `-ffast-math` |
| "Works locally, fails in CI" | "CI runner is slower/different" | Platform-specific undefined behavior |

### The Real Cause: `-ffast-math` and IEEE 754

On macOS/Linux with `-ffast-math`, these functions **DO NOT WORK**:
- `std::isnan()`
- `std::isfinite()`
- `std::isinf()`

This causes NaN/Inf values to propagate through DSP code, eventually causing crashes when used in math operations.

### The Correct Fix: Use Bit-Level Checks (PREFERRED)

**This codebase has bit-level NaN/Inf checks in `db_utils.h` that work with `-ffast-math`:**

```cpp
// In dsp/include/krate/dsp/core/db_utils.h
namespace detail {
    constexpr bool isNaN(float x) noexcept;   // Bit-level NaN check
    constexpr bool isInf(float x) noexcept;   // Bit-level Inf check
}
```

**ALWAYS use these instead of std library functions:**

```cpp
// BAD - breaks with -ffast-math
if (!std::isfinite(input)) { ... }
if (std::isnan(value)) { ... }

// GOOD - works everywhere
if (detail::isNaN(input) || detail::isInf(input)) { ... }
if (detail::isNaN(value)) { ... }
```

**Why this works:** These functions use `std::bit_cast` to examine IEEE 754 bit patterns directly, which works regardless of fast-math optimization flags.

### Alternative Fix: `-fno-fast-math` (FALLBACK)

If you must use `std::isfinite()` etc., add the source file to the `-fno-fast-math` list:

```cmake
# In dsp/tests/CMakeLists.txt
if(NOT MSVC)
    set_source_files_properties(
        unit/processors/spectral_gate_test.cpp
        PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"
    )
endif()
```

**WARNING:** This only works for test files. For DSP implementation headers (which are header-only and get inlined), you MUST use the bit-level checks.

### How to Diagnose

1. **Exit code 8 on macOS** = likely SIGFPE (floating-point exception)
2. **Test crashes but works locally on Windows** = `-ffast-math` issue
3. **Code uses `std::isnan()`, `std::isfinite()`, or `std::isinf()`** = replace with `detail::isNaN()`/`detail::isInf()`

### The Rule

> **When tests CRASH, NEVER relax expectations. Find the root cause.**
>
> - Crashes are NOT precision issues
> - Crashes are NOT "CI is different"
> - Crashes ARE bugs that need fixing
> - **ALWAYS use `detail::isNaN()` and `detail::isInf()` from `db_utils.h`**

### Quick Reference: Safe NaN/Inf Checking

| Instead of... | Use... |
|---------------|--------|
| `std::isnan(x)` | `detail::isNaN(x)` |
| `std::isinf(x)` | `detail::isInf(x)` |
| `std::isfinite(x)` | `!detail::isNaN(x) && !detail::isInf(x)` |
| `!std::isfinite(x)` | `detail::isNaN(x) \|\| detail::isInf(x)` |

**Note:** In Catch2 `REQUIRE()` macros, wrap `&&` expressions in parentheses:
```cpp
REQUIRE((!detail::isNaN(x) && !detail::isInf(x)));  // Extra parens required
```

---

## 12. The Build Paranoia Loop

**Problem:** Tests compile, run, and produce output—but you doubt whether they "really" compiled and start making changes just to verify.

**Signs you're doing this:**
- Tests ran and printed results, but you rebuild "to make sure"
- Adding whitespace or comments to "force" recompilation
- Checking build output multiple times for the same change
- Assuming test results are "stale" when they don't match expectations

**The Rule:**

> **If tests RUN and produce OUTPUT, the build SUCCEEDED. Period.**
>
> Do not second-guess the build system. If Catch2 printed test names and results, your code compiled and linked correctly.

**When to actually suspect stale builds:**

| Symptom | Likely Cause | Fix |
|---------|--------------|-----|
| New test file added but 0 tests appear | File not in CMakeLists.txt | Add to `target_sources()` |
| Code change has zero effect on behavior | Linker used cached `.obj` | `--clean-first` |
| Unexplained crash after header-only change | Stale object files | `--clean-first` |
| Wrong function signature in error | ODR violation (duplicate names) | Search for duplicate class/function |

**The fix when staleness is confirmed:**

```bash
cmake --build build --config Release --target dsp_tests --clean-first
```

**STOP:** Do not rebuild unless one of the above symptoms applies. If tests run and fail, the failure is real—investigate the code, not the build.
