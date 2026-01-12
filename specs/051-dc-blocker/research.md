# Research: DC Blocker Primitive

**Feature**: 051-dc-blocker
**Date**: 2026-01-12
**Purpose**: Document algorithm decisions and implementation patterns

---

## Algorithm Research

### DC Blocking Filter Theory

A DC blocker is a first-order highpass filter designed to remove the DC (0 Hz) component from audio signals while minimally affecting audio frequencies.

**Transfer Function:**
```
H(z) = (1 - z^-1) / (1 - R*z^-1)
```

Where R is the pole coefficient (0 < R < 1). Closer to 1 = lower cutoff frequency.

**Difference Equation:**
```
y[n] = x[n] - x[n-1] + R * y[n-1]
```

### Pole Coefficient Calculation

**Decision**: Use exponential formula
**Rationale**: Provides accurate cutoff frequency matching across all sample rates
**Alternatives considered**: Simplified formula (R = 1 - 2*pi*fc/fs) is less accurate at higher cutoff frequencies

**Formula:**
```cpp
R = exp(-2 * pi * cutoffHz / sampleRate)
```

**Clamping Range**: [0.9, 0.9999]
- Lower bound 0.9: Prevents extremely high cutoff (would remove too much audio content)
- Upper bound 0.9999: Prevents filter instability while allowing very low cutoff

**Example values at 44.1 kHz:**
| Cutoff (Hz) | R Value | Time Constant |
|-------------|---------|---------------|
| 5 | 0.99929 | ~140ms |
| 10 | 0.99857 | ~70ms |
| 20 | 0.99716 | ~35ms |

### Existing Codebase Patterns

**Current implementations found:**

1. **FeedbackNetwork inline DCBlocker** (`systems/feedback_network.h:51-76`)
   - Hardcoded R = 0.995 (~10 Hz at 44.1 kHz)
   - No prepare/configure method
   - No sample rate awareness
   - Will be replaced by this primitive

2. **Biquad as DC blocker** (used in SaturationProcessor)
   - Configured as highpass at 10 Hz
   - Works but heavier than needed:
     - 5 coefficients vs 1
     - 2 state variables vs 2 (same)
     - More operations per sample

3. **OnePoleSmoother** (`primitives/smoother.h`)
   - Similar single-pole filter topology
   - Good reference for prepare/reset/process pattern
   - Uses same denormal flushing approach

### Denormal Flushing Strategy

**Decision**: Apply `flushDenormal()` to `y1_` after each sample
**Rationale**: Catches denormals at the source in the recursive feedback path

**Alternative considered**: Flush output only
- Problem: `y1_` feeds back into next sample via `R * y1_`
- Small `y1_` values compound through feedback, staying denormal
- Flushing `y1_` prevents denormal propagation in recursive path

**Implementation:**
```cpp
y1_ = x - x1_ + R_ * y1_;
y1_ = detail::flushDenormal(y1_);  // Flush state, not just output
x1_ = x;
return y1_;
```

### Unprepared State Handling

**Decision**: Use dedicated `prepared_` flag
**Rationale**: Explicit state tracking is clearer and safer than relying on coefficient values

**Alternative considered**: Use R = 1.0 as "passthrough" indicator
- Problem: R = 1.0 is actually unstable (pole on unit circle)
- Problem: Makes coefficient range checking more complex
- Problem: Less explicit about component state

**Implementation:**
```cpp
float process(float x) noexcept {
    if (!prepared_) return x;  // Safe passthrough
    // ... normal processing
}
```

### Performance Comparison (Static Analysis)

**DCBlocker operations per sample:**
- Subtractions: 1 (x - x1_)
- Multiplications: 1 (R_ * y1_)
- Additions: 1 (result + R*y1_)
- Assignments: 2 (x1_ = x, y1_ = y)
- Function call: 1 (flushDenormal - typically inlined)

**Total: 3 arithmetic (1 mul + 1 sub + 1 add) + 2 memory stores + denormal check**

**Biquad operations per sample (TDF2):**
- Multiplications: 5 (b0, b1, b2, a1, a2)
- Additions: 4 (accumulator operations)
- Assignments: 3 (output + 2 state variables)

**Total: 9 arithmetic (5 mul + 4 add) + 3 memory stores**

**Conclusion**: DCBlocker is ~3x lighter than Biquad for DC blocking use case (3 vs 9 arithmetic ops).

---

## Layer 0 Dependencies

### Required from `db_utils.h`

```cpp
namespace detail {
    // Denormal flushing - prevents 100x CPU slowdown
    [[nodiscard]] inline constexpr float flushDenormal(float x) noexcept;

    // NaN detection - works under -ffast-math when compiled with -fno-fast-math
    constexpr bool isNaN(float x) noexcept;

    // Infinity detection
    [[nodiscard]] constexpr bool isInf(float x) noexcept;
}

// Denormal threshold constant
inline constexpr float kDenormalThreshold = 1e-15f;
```

### Required from `<cmath>`

```cpp
std::exp()  // For pole coefficient calculation
```

Note: `std::exp` is used only in `prepare()` and `setCutoff()`, not in the audio processing path, so it doesn't impact real-time performance.

---

## Test Strategy

### Unit Test Categories

1. **Construction/Initialization**
   - Default constructor state
   - Unprepared state behavior

2. **Lifecycle Methods**
   - prepare() with various sample rates
   - reset() clears state
   - setCutoff() recalculates R

3. **DC Removal Verification**
   - Constant DC input decays to ~0
   - SC-001: Decay to <1% within 5 time constants

4. **Audio Passthrough**
   - SC-002: 100 Hz sine <0.5% loss at 10 Hz cutoff
   - SC-003: 20 Hz sine <5% loss at 10 Hz cutoff

5. **Block Processing**
   - SC-005: processBlock matches sequential process() calls
   - Various block sizes

6. **Edge Cases**
   - NaN propagation (FR-016)
   - Infinity handling (FR-017)
   - Denormal flushing (FR-015)

7. **Parameter Validation**
   - Sample rate clamping (FR-011)
   - Cutoff frequency clamping (FR-010)

### Frequency Response Testing Approach

Measure -3dB point by:
1. Generate sine wave at test frequency
2. Process through DCBlocker
3. Measure output amplitude
4. Compare to input amplitude
5. Find frequency where ratio = ~0.707 (-3dB)

---

## Implementation Notes

### Header Organization

Follow pattern from `smoother.h`:
1. File header comment with layer info and constitution compliance
2. Include guards
3. Includes (standard library, then Layer 0)
4. Namespace declaration
5. Doxygen class documentation
6. Class definition
7. End namespace

### Naming Convention Compliance

Per CLAUDE.md naming conventions:
- Class: `DCBlocker` (PascalCase)
- Members: `x1_`, `y1_`, `R_`, `prepared_`, `sampleRate_` (trailing underscore)
- Methods: `prepare()`, `process()`, `reset()` (camelCase)
- Constants: `kDefaultCutoffHz` if needed (kPascalCase)

### Thread Safety Notes

- All processing methods are `noexcept`
- No shared mutable state
- Safe for multiple instances in parallel channels
- Each channel should have its own DCBlocker instance

---

## References

1. Julius O. Smith III, "Introduction to Digital Filters", Chapter on DC Blocker
2. Existing codebase: `dsp/include/krate/dsp/primitives/smoother.h`
3. Existing codebase: `dsp/include/krate/dsp/systems/feedback_network.h`
4. Constitution: `.specify/memory/constitution.md`
