# Research: Rungler / Shift Register Oscillator

**Branch**: `029-rungler-oscillator` | **Date**: 2026-02-06

---

## R-001: Triangle Oscillator Implementation Pattern

**Question**: Should we reuse PhaseAccumulator from `core/phase_utils.h` or implement a custom triangle oscillator?

**Decision**: Implement a custom bipolar triangle oscillator inline within the Rungler class.

**Rationale**: The spec (FR-001) describes a bipolar phase accumulator with direction reversal at bounds [-1, +1]. The existing `PhaseAccumulator` from `core/phase_utils.h` is a unipolar [0, 1) phase ramp -- it wraps at 1.0 and does not produce a triangle waveform. Converting its output to a bipolar triangle (e.g., `4 * phase - 1` for the first half) would add complexity without benefit. The triangle oscillator in the spec is 5 lines of code (phase += direction * increment, reflect at bounds). It also needs to track direction for pulse wave derivation. A custom implementation is simpler, more direct, and matches the original Benjolin's analog triangle-core oscillator behavior.

**Alternatives considered**:
- **Reuse PhaseAccumulator + triangle transform**: Extra computation, direction tracking not built-in, and the phase wrapping semantics (unipolar wrap vs bipolar reflect) are fundamentally different.
- **Reuse PolyBlepOscillator**: Spec explicitly forbids this (FR-001 note: "not PolyBLEP anti-aliased"). The Benjolin's gritty character requires a naive triangle core.

---

## R-002: CV Smoothing Filter Component Selection

**Question**: Should we reuse `OnePoleSmoother` from `primitives/smoother.h` or `OnePoleLP` from `primitives/one_pole.h` for the Rungler CV low-pass filter?

**Decision**: Use `OnePoleLP` from `primitives/one_pole.h`.

**Rationale**: The spec (FR-008) requires a one-pole low-pass filter with cutoff frequency mapped from the amount parameter. `OnePoleLP` is designed for audio signal filtering with a `setCutoff(float hz)` API -- exactly what we need. It uses the standard coefficient `a = exp(-2*pi*fc/fs)`, provides `prepare(double sampleRate)` and `reset()`, and handles NaN/Inf. `OnePoleSmoother` is designed for parameter smoothing (setTarget/process paradigm with completion threshold), which does not match our use case of filtering an arbitrary signal.

**Alternatives considered**:
- **OnePoleSmoother**: Wrong abstraction. It smooths toward a target value; we need to filter a continuous signal.
- **Custom one-pole inline**: Possible but unnecessary duplication. `OnePoleLP` is battle-tested and handles edge cases.
- **Biquad lowpass**: Overkill -- a first-order filter is sufficient for CV smoothing per the spec.

---

## R-003: Shift Register Data Type and Bit Manipulation

**Question**: What data type and bit manipulation strategy for the configurable-length shift register?

**Decision**: Use `uint32_t` for the register state with a bitmask of `(1u << N) - 1` where N is the register length.

**Rationale**: The spec (FR-004) requires 4 to 16 bits. A `uint32_t` provides 32 bits, more than enough. Shifting is done with standard bit operations: `register = ((register << 1) | newBit) & mask`. The 3-bit DAC reads from bits N-1 (MSB), N-2, N-3 (LSB) via bit extraction: `(register >> (N-1)) & 1`, etc. When the register length changes, only the mask and bit positions change; the underlying 32-bit value is preserved (per spec Technical Notes).

**Alternatives considered**:
- **std::bitset**: Heap allocation risk, template parameter must be compile-time constant (cannot support runtime-configurable length).
- **uint16_t**: Would work but offers no advantage over uint32_t, and would require explicit narrowing considerations.

---

## R-004: Exponential Frequency Modulation Formula

**Question**: How to implement the exponential (musical) scaling of Rungler CV to oscillator frequency modulation?

**Decision**: Use `std::pow(2.0f, ...)` with the formula from the spec Technical Notes section.

**Rationale**: The spec (FR-003) defines:
```
modulationFactor = pow(2.0, runglerCV * depth * modulationOctaves) / pow(2.0, depth * modulationOctaves * 0.5)
effectiveFreq = baseFreq * modulationFactor
```
This simplifies to: `modulationFactor = pow(2.0, depth * modulationOctaves * (runglerCV - 0.5))`. At runglerCV=0.5, factor=1 (base freq). At runglerCV=0, factor=1/4 (-2 octaves). At runglerCV=1, factor=4 (+2 octaves). With `modulationOctaves = 4` and `depth = 1.0`.

The `std::pow` call happens once per sample per oscillator. Since the Rungler CV changes at shift-register clock rate (much slower than sample rate in typical use), we could optimize by caching, but the per-sample cost of one pow is negligible for a Layer 2 processor (SC-006 target < 0.5%).

**Alternatives considered**:
- **Lookup table for pow(2, x)**: Premature optimization. The entire processor is lightweight arithmetic.
- **Linear frequency scaling**: Spec explicitly requires exponential (musical) scaling.

---

## R-005: NaN/Infinity Sanitization Strategy

**Question**: How to handle NaN/Infinity inputs to setters?

**Decision**: Reuse `detail::isNaN()` and `detail::isInf()` from `core/db_utils.h`.

**Rationale**: The spec (FR-015) requires NaN/Infinity sanitization on frequency setters. The codebase already provides bit-manipulation-based `isNaN()` and `isInf()` in `core/db_utils.h` that work correctly under `-ffast-math`. These are used consistently across the entire DSP library (ChaosOscillator, OnePoleSmoother, etc.).

**Alternatives considered**:
- **std::isnan/std::isinf**: Broken under -ffast-math. Must not use.
- **Custom checks**: Unnecessary duplication of existing utilities.

---

## R-006: Zero-Crossing Detection for Clock

**Question**: How to detect the rising edge of Oscillator 2's triangle wave for shift register clocking?

**Decision**: Track previous triangle value and detect transition from negative to non-negative.

**Rationale**: The spec (FR-006) is explicit: clock event occurs when `(prevTriangle < 0.0) && (currentTriangle >= 0.0)`. This is a standard edge detection pattern. The previous triangle value is stored as a member variable and updated each sample. This gives exactly one clock event per oscillator cycle, which is the correct behavior. The clarification in the spec confirms that zero is treated as non-negative.

**Alternatives considered**:
- **Phase-based detection**: Using PhaseAccumulator wrap detection. Not applicable since we use a bipolar triangle with direction reversal, not a unipolar phase ramp.
- **Dedicated pulse wave comparison**: Could track pulse wave state changes instead, but this adds an extra state variable for no benefit.

---

## R-007: Seed Strategy for Shift Register

**Question**: How should the shift register be seeded on prepare()/reset()?

**Decision**: Use `Xorshift32` from `core/random.h` to generate a non-zero seed.

**Rationale**: The spec (FR-013, FR-023) requires using Xorshift32 for shift register seeding. The PRNG is used only at initialization (not per-sample), so it has no real-time impact. The seed must be non-zero to ensure the system starts producing patterns immediately (an all-zero register in chaos mode would take multiple clock cycles to populate). The `next()` method of Xorshift32 always returns non-zero values (range [1, 2^32-1]).

The Rungler also provides `seed(uint32_t)` (FR-020) for deterministic test output, which directly sets the Xorshift32 seed.

---

## R-008: Existing OnePoleLP Dependency Analysis

**Question**: Does using `OnePoleLP` (Layer 1) create any layer violation for the Rungler (Layer 2)?

**Decision**: No violation. Layer 2 processors can depend on Layer 0 and Layer 1.

**Rationale**: The layer dependency rules state Layer 2 can include Layers 0-1. `OnePoleLP` is at `primitives/one_pole.h` (Layer 1). The Rungler also depends on `core/random.h` (Layer 0), `core/db_utils.h` (Layer 0), and `core/math_constants.h` (Layer 0). All dependencies are at lower layers.

---

## R-009: Output Struct Naming and ODR Safety

**Question**: Is the name `Output` safe for the multi-output struct?

**Decision**: Safe when scoped within the `Rungler` class as `Rungler::Output`.

**Rationale**: The spec explicitly notes this: "The `Output` struct is scoped within the `Rungler` class, preventing ODR conflicts with other processor output structs." A grep for `struct Output` in the processors directory returned no results at the top level. Other processors use different naming or scope their output types within their class.

---

## R-010: Performance Assessment

**Question**: Will the Rungler meet the SC-006 CPU target of < 0.5%?

**Decision**: Yes, with high confidence.

**Rationale**: The Rungler's per-sample operations are:
1. Two triangle phase accumulations: 2 additions + 2 comparisons + occasional direction flip
2. One zero-crossing detection: 1 comparison
3. Per-clock-event (not per-sample): 1 shift, 1 XOR, 1 DAC computation (3 bit extracts + multiply/add)
4. Exponential frequency modulation: 2 calls to `std::pow(2.0f, ...)` per sample
5. One optional one-pole filter: 1 multiply + 1 add per sample
6. One PWM comparison: 1 comparison
7. Mixed output: 1 add + 1 multiply

The most expensive operation is the `std::pow` calls. Even so, this is far lighter than the ChaosOscillator (which uses RK4 with up to 100 substeps) and the ParticleOscillator (64 particles with sine generation). Those are well within budget. The Rungler has no trigonometry, no FFT, no iterative integration, no oversampling.

Estimated CPU: < 0.1% at 44.1 kHz, well within the 0.5% Layer 2 budget.
