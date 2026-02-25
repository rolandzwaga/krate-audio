# DSP Code Review Checklist

Detailed review criteria for all code that runs on or supports the audio thread. This covers DSP library code (`dsp/`), processor code (`plugins/*/src/processor/`), synth engines (`plugins/*/src/engine/`), and plugin-local DSP (`plugins/*/src/dsp/`).

---

## 1. Real-Time Safety (CRITICAL)

Every line of code reachable from `Processor::process()` must satisfy hard real-time constraints. A single violation causes audible dropouts.

### Forbidden Operations

| Operation | What to Search For | Severity |
|-----------|--------------------|----------|
| Heap allocation | `new`, `delete`, `malloc`, `free`, `make_unique`, `make_shared` | CRITICAL |
| Resizing containers | `push_back`, `emplace_back`, `resize`, `reserve`, `insert` on vectors/maps | CRITICAL |
| String operations | `std::string` construction, concatenation, `c_str()` | CRITICAL |
| Blocking locks | `std::mutex`, `lock_guard`, `unique_lock`, `condition_variable` | CRITICAL |
| Exceptions | `throw`, `try`, `catch` | CRITICAL |
| I/O | `cout`, `cerr`, `printf`, `fprintf`, file operations | CRITICAL |
| Virtual dispatch in tight loops | `virtual` method calls per-sample (acceptable per-block) | WARNING |

### What IS Allowed

- `std::atomic` loads/stores with relaxed ordering
- Pre-allocated `std::vector` access by index
- `std::array` (stack-allocated, fixed size)
- `std::span` (view, no allocation)
- Arithmetic, bitwise operations
- Inline function calls

### Review Pattern

For each function reachable from `process()`:
1. Trace the call graph — what does it call?
2. Check every called function for forbidden operations
3. Pay special attention to error paths (do they log? throw?)

---

## 2. Layer Architecture

### Dependency Direction

Each DSP layer can ONLY depend on layers below it.

| File in... | Can include from... | CANNOT include from... |
|------------|---------------------|------------------------|
| `core/` (L0) | stdlib only | L1, L2, L3, L4 |
| `primitives/` (L1) | L0 | L2, L3, L4 |
| `processors/` (L2) | L0, L1 | L3, L4 |
| `systems/` (L3) | L0, L1, L2 | L4 |
| `effects/` (L4) | L0, L1, L2, L3 | — |

**Review action:** Check `#include` directives in changed files. Flag any upward dependency.

### ODR (One Definition Rule) Violations

Before approving any new class or struct definition:
1. Search the codebase for existing types with the same name
2. Same name in same namespace = undefined behavior (CRITICAL)
3. Check `dsp_utils.h` and utility headers for name collisions

---

## 3. Numerical Stability

### NaN/Infinity Prevention

| Check | Where | Severity |
|-------|-------|----------|
| Division by zero guards | Any division where denominator could be zero | CRITICAL |
| `std::isnan()` usage | Will break under `-ffast-math`; use bit manipulation | WARNING |
| Feedback loop sanitization | Output of any feedback path should be sanitized | CRITICAL |
| Filter coefficient bounds | Biquad coefficients must be checked for stability | WARNING |
| Log/sqrt of zero/negative | `std::log(x)` where x could be <= 0 | CRITICAL |

### Specific NaN Sources to Watch

```
- 0.0 / 0.0
- sqrt(negative)
- log(0) or log(negative)
- asin/acos outside [-1, 1]
- Inf - Inf
- Inf * 0
- Uninitialized float variables (may be NaN/denormal)
```

### Denormal Prevention

- Is FTZ/DAZ enabled in `setupProcessing()`?
- Feedback loops with very small signals (decaying reverb tails) are denormal hotspots
- IIR filter states that aren't flushed can accumulate denormals

### Floating-Point Cross-Platform

- MSVC and Clang differ at 7th-8th decimal place
- Tests must use `Approx().margin()` not exact equality
- Approval tests must use `std::setprecision(6)` or coarser

---

## 4. Interpolation Correctness

| Use Case | Correct Choice | Common Mistake |
|----------|---------------|----------------|
| Fixed delay in feedback | Allpass | Linear (loses high frequencies) |
| LFO-modulated delay | Linear or Cubic | Allpass (causes ringing with modulation) |
| Pitch shifting | Lagrange or Sinc | Linear (audible aliasing) |
| Sample rate conversion | Sinc | Linear (insufficient quality) |

**Review action:** If the code implements or uses delay interpolation, verify the method matches the use case.

---

## 5. Feedback Safety

### Feedback > 100%

If the parameter range allows feedback > 1.0 (which it does for "self-oscillation" effects):
- There MUST be a soft limiter in the feedback path (`std::tanh`, sigmoid, or custom)
- Without limiting, signal grows exponentially and clips hard

### Feedback Loop Checks

1. Is there a sanitizer (NaN/Inf check) in the loop?
2. Is there a DC blocker if the loop contains asymmetric nonlinearities?
3. Is the feedback delay at least 1 sample? (zero-delay feedback requires special handling)

---

## 6. Signal Quality

### DC Blocking

Required after:
- Asymmetric waveshaping/saturation
- Rectification (half-wave, full-wave)
- In feedback loops with saturation

Cutoff: 5-20 Hz highpass. Lower = less bass impact, higher = faster settling.

### Oversampling

Required for:
- Soft saturation (2x minimum)
- Hard clipping (4x minimum)
- Waveshaping (2-4x depending on curve aggressiveness)
- Bitcrushing (4x minimum)

NOT needed for:
- Linear filters
- Delay lines
- Mixing / gain

### Anti-Aliasing (Oscillators)

For oscillators generating harmonically rich waveforms:
- MinBLEP for step discontinuities (sawtooth, square)
- MinBLAMP for derivative discontinuities (triangle kinks)
- Band-limiting must be appropriate for the output sample rate

---

## 7. Performance

### CPU Budget Compliance

| Component Type | CPU Target | Flag if Exceeds |
|----------------|------------|-----------------|
| Layer 1 primitive | < 0.1% | WARNING |
| Layer 2 processor | < 0.5% | WARNING |
| Layer 3 system | < 1% | WARNING |
| Full plugin | < 5% | CRITICAL |

### Common Performance Issues

| Issue | What to Look For | Severity |
|-------|-----------------|----------|
| Per-sample virtual dispatch | `virtual` methods called inside sample loops | WARNING |
| Unnecessary copies | Passing large structs by value | WARNING |
| Branch misprediction | Data-dependent branches in tight loops | SUGGESTION |
| Cache unfriendly access | Striding through large arrays with poor locality | WARNING |
| Redundant computation | Recalculating constants inside sample loops | WARNING |

### Memory

- Maximum delay: 10 seconds at 192kHz = 1.92M samples/channel
- All buffers must be pre-allocated in `prepare()` / `setupProcessing()`
- Power-of-2 buffer sizes preferred for efficient modulo

---

## 8. State and Initialization

### prepare() / setupProcessing() Completeness

When a DSP component has a `prepare()`, `reset()`, or `setupProcessing()` method:
1. Are ALL internal buffers sized correctly for the given sample rate and block size?
2. Are filter coefficients recalculated for the new sample rate?
3. Are all state variables reset (delay line contents, filter states, phase accumulators)?
4. Is the method called before `process()` is first invoked?

### Parameter Smoothing

- Are discontinuous parameter changes smoothed to avoid clicks?
- Is the smoothing time appropriate (typically 5-50ms)?
- Is smoothing done per-sample, not per-block? (per-block creates staircase artifacts)

---

## 9. Test Coverage (for DSP code changes)

### Required Tests

For any new DSP component or algorithm change:

| Test Type | What It Verifies | Required? |
|-----------|-----------------|-----------|
| Basic I/O | Produces expected output for known input | Yes |
| Edge cases | Zero input, silence, max values, NaN input | Yes |
| Allocation freedom | No heap allocations in process path | Yes (for real-time code) |
| NaN/Inf guard | Output is always valid floats | Yes |
| Numerical bounds | Output stays within expected range | Yes |
| Parameter sweep | Behavior is reasonable across full parameter range | Recommended |

### Test Quality

- Tests should use Arrange-Act-Assert pattern
- No `REQUIRE`/`INFO` inside sample-processing loops (Catch2 performance killer)
- Floating-point assertions use `Approx().margin()` with cross-platform safe tolerances
- Test names describe the behavior being verified, not the implementation
