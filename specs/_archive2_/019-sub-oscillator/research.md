# Research: Sub-Oscillator (019)

**Date**: 2026-02-04
**Spec**: specs/019-sub-oscillator/spec.md
**Status**: Complete

---

## Research Tasks

### R-001: Flip-Flop Frequency Division Pattern

**Question**: What is the correct implementation of a T-flip-flop frequency divider for sub-oscillator use, and how does a two-stage chain achieve divide-by-4?

**Decision**: Use a boolean state machine modeling a T-flip-flop. The first stage toggles on every master phase wrap. The second stage (for TwoOctaves) toggles on rising edges (false-to-true transitions) of the first stage. This directly models the hardware T-flip-flop chain found in Moog and Sequential synthesizers.

**Rationale**: The T-flip-flop is the standard approach in analog synthesizer sub-oscillator circuits (e.g., Moog Sub 37, Sequential Prophet Rev2). It guarantees exact integer frequency division regardless of master frequency jitter or FM. The boolean state approach requires zero computation per sample when the master does not wrap, making it extremely efficient.

**Alternatives considered**:
- Phase accumulator at half frequency: Simpler but introduces accumulated rounding error and does not model the deterministic nature of analog frequency dividers. Also cannot guarantee exact phase coherence with the master.
- Counter-based division: Equivalent to flip-flop but less idiomatic for DSP code. Same result, different abstraction.

**Key implementation detail**: For TwoOctaves, the second stage must use rising-edge triggering (toggle when first stage transitions false -> true), not level-triggering. This ensures exactly 4 master wraps per sub cycle, matching the analog behavior. The codebase does not have any existing flip-flop implementation to reuse.

---

### R-002: Delta-Phase Tracking for Sine/Triangle Sub Waveforms

**Question**: How should the sub-oscillator derive its frequency for sine and triangle waveform generation from the master oscillator?

**Decision**: Use delta-phase tracking -- read the master's instantaneous phase increment per sample and divide by the octave factor (2 or 4) to derive the sub phase increment. The sub maintains its own PhaseAccumulator whose increment is updated every sample.

**Rationale**: Delta-phase tracking provides immediate frequency response to FM, pitch bend, and any modulation applied to the master. It has zero latency -- the sub frequency updates on the very next sample. This is superior to wrap-interval timing (which would introduce 1-cycle latency) and zero-crossing detection (which fails at high frequencies).

**Alternatives considered**:
- Wrap-interval timing: Measure time between master phase wraps, compute frequency. Introduces 1 master-cycle latency in frequency tracking. Fails during rapid FM. Rejected for latency.
- Zero-crossing detection: Detect when the master output crosses zero. Highly sensitive to waveform shape and DC offset. Rejected for unreliability.
- Direct frequency parameter sharing: Sub reads the master's frequency setting. Does not respond to FM/PM modulation applied per-sample. Rejected for incomplete tracking.

**Key implementation detail**: The sub phase increment is computed as `masterPhaseIncrement / octaveFactor` where octaveFactor is 2 (OneOctave) or 4 (TwoOctaves). The existing `PhaseAccumulator` struct from `phase_utils.h` is used directly; its `phase` and `increment` are public `double` members that can be updated every sample with zero overhead.

---

### R-003: Sub-Sample MinBLEP Offset Computation

**Question**: How should the sub-oscillator compute the sub-sample offset for minBLEP correction at flip-flop toggle points?

**Decision**: Derive the sub-sample offset from the master's phase state. When a phase wrap occurs, the master's new phase represents how far past the wrap point it advanced. The fractional offset within the sample is `phase / increment` (the existing `subsamplePhaseWrapOffset()` function from `phase_utils.h`).

**Rationale**: This is the same approach used by `SyncOscillator` and provides approximately 20 dB improvement in alias rejection over sample-accurate correction. The computation is a single floating-point division per toggle event (not per sample), so the CPU cost is negligible.

**Alternatives considered**:
- Sample-accurate correction (offset = 0): Simplest implementation, but provides significantly worse alias rejection. Rejected per spec requirement for mastering-grade quality.
- Phase interpolation from two samples: Compute offset = (T - phi_prev) / (phi_curr - phi_prev). Mathematically equivalent to the subsamplePhaseWrapOffset approach when T=0 and phi_prev = phi_curr - increment (assuming phi_curr already wrapped). The existing utility function captures this pattern.

**Key implementation detail**: The SubOscillator does NOT need to track phi_prev explicitly. The master's `phaseWrapped` signal tells it a wrap occurred, and the master's current phase (which is `masterPhaseIncrement * fractionalOffset` due to the wrap) gives the offset directly via `subsamplePhaseWrapOffset()`. However, the SubOscillator needs the master's phase increment to call this function, which is why `process()` takes `masterPhaseIncrement` as a parameter.

---

### R-004: Residual Buffer and MinBlepTable Sharing

**Question**: Can the SubOscillator share the same MinBlepTable as the SyncOscillator? What are the memory implications for 128 polyphonic instances?

**Decision**: Yes, the SubOscillator shares the same `MinBlepTable` instance (passed via constructor pointer, read-only after prepare). Each SubOscillator instance creates its own `MinBlepTable::Residual` which owns a buffer sized to `table.length()`. With the standard `prepare(64, 8)` configuration, `table.length() = 16`, so each Residual is 16 floats = 64 bytes.

**Rationale**: The MinBlepTable is immutable after prepare() and contains only lookup data. Sharing it across all oscillator instances (sync, sub, future) eliminates redundant memory. The Residual is per-instance mutable state and must be separate. Using the existing `Residual` struct avoids code duplication and maintains consistency with the SyncOscillator pattern.

**Memory analysis for 128 instances**:
- Residual buffer per instance: 16 floats * 4 bytes = 64 bytes (using standard table config)
- SubOscillator state (flip-flops, phase, params): ~40 bytes
- Total per instance: ~104 bytes (well under the 300-byte spec limit)
- Total for 128 instances: ~13 KB (well under L1 cache boundary)
- If using larger table (zeroCrossings=32, length=64): 256 + 40 = ~296 bytes per instance, 128 * 296 = ~37 KB

**Spec reconciliation**: The spec says "fixed 64-sample float residual buffer (256 bytes)". The existing `MinBlepTable::Residual` uses `std::vector<float>` dynamically sized to `table.length()`. Two options:
1. Use standard table (length=16): Residual is 64 bytes. Exceeds alias rejection needed. Under memory budget.
2. Use larger table (zeroCrossings=32, length=64): Residual is 256 bytes. Maximum alias rejection. At memory budget limit.

**Decision**: Use the standard table configuration (`prepare(64, 8)`, length=16). The sub-oscillator has at most one toggle per master wrap, producing far fewer discontinuities per second than the sync oscillator. The 16-sample minBLEP correction is more than sufficient. The spec's FR-004 mentions that if "the table length exceeds 64 samples, the method MUST set prepared_ to false" -- this is a maximum bound, not a minimum. The SubOscillator validate `table->length() <= 64` in prepare() as a safety check.

**Alternatives considered**:
- Custom fixed-size Residual with `std::array<float, 64>`: Would avoid heap allocation entirely. However, this would be a new type not compatible with existing `MinBlepTable::Residual`, requiring duplicated addBlep/consume logic. The existing Residual allocates its vector only once during prepare() (not real-time), so the heap allocation is acceptable. Rejected to avoid code duplication. Note: `std::vector<float>` does not benefit from Small String Optimization (SSO) — floats always use heap allocation regardless of count.

---

### R-005: Phase Resynchronization for Sine/Triangle Waveforms

**Question**: How and when should the sine/triangle phase accumulator be resynchronized with the flip-flop state?

**Decision**: Reset the sub phase to 0.0 when the output flip-flop transitions from false to true (rising edge). This ensures the sine/triangle waveform cycle starts at the same point relative to the flip-flop division, preventing long-term frequency drift.

**Rationale**: Without resynchronization, accumulated phase error in the sub phase accumulator would cause the sine/triangle waveform to drift relative to the flip-flop division boundary over time. Resynchronizing at the rising edge of the output flip-flop provides a natural sync point that occurs once per sub cycle. This is analogous to how analog VCOs are synchronized.

**Phase discontinuity concern**: Resetting the phase to 0.0 introduces a potential discontinuity in the sine/triangle output. For sine: the output snaps to sin(0) = 0.0. For triangle: the output snaps to triangle(0) = -1.0. In practice, if the sub frequency tracking is accurate, the phase should already be very close to 0.0 at the resync point, so the discontinuity is minimal. No minBLEP correction is applied for sine/triangle waveforms (they have no hard discontinuities in their waveform shape).

**Alternatives considered**:
- No resynchronization: Simplest, but allows unbounded phase drift over time. Rejected.
- Continuous soft sync: Apply a gentle pull toward the target phase. More complex, marginal benefit for this use case. Rejected for simplicity.
- Resync on every master wrap: Too aggressive -- would cause phase jumps in the sine/triangle output on every master cycle. Rejected.

---

### R-006: Output Sanitization Pattern

**Question**: What output sanitization pattern should the SubOscillator use?

**Decision**: Reuse the same branchless sanitization pattern used by `SyncOscillator` and `PolyBlepOscillator`: NaN detection via `std::bit_cast`, clamp to [-2.0, 2.0], replace NaN with 0.0.

**Rationale**: Consistency with existing oscillator implementations. The pattern is proven, real-time safe, and works correctly with `-ffast-math` enabled (since it uses bit manipulation, not `std::isnan()`).

**Implementation**: Copy the `sanitize()` static helper method from `SyncOscillator`:
```cpp
[[nodiscard]] static inline float sanitize(float x) noexcept {
    const auto bits = std::bit_cast<uint32_t>(x);
    const bool isNan = ((bits & 0x7F800000u) == 0x7F800000u) &&
                       ((bits & 0x007FFFFFu) != 0);
    x = isNan ? 0.0f : x;
    x = (x < -2.0f) ? -2.0f : x;
    x = (x > 2.0f) ? 2.0f : x;
    return x;
}
```

---

### R-007: Equal-Power Crossfade Integration

**Question**: How should the `processMixed()` method integrate with the existing `equalPowerGains()` utility?

**Decision**: Use the two-parameter reference overload of `equalPowerGains()` from `crossfade_utils.h`. Cache the gains when `setMix()` is called (not per-sample) to avoid computing cos/sin every sample.

**Rationale**: The mix parameter is not smoothed internally (per spec assumptions). It changes infrequently (UI parameter changes, not per-sample modulation). Computing equal-power gains at set-time rather than process-time saves ~2 trig calls per sample.

**API signature from header**:
```cpp
inline void equalPowerGains(float position, float& fadeOut, float& fadeIn) noexcept;
```
Where `position = mix`, `fadeOut = mainGain`, `fadeIn = subGain`.

**Alternatives considered**:
- Compute gains per-sample in process(): Correct but wasteful. The mix value only changes when setMix() is called.
- Linear crossfade: Simpler but does not maintain constant perceived loudness. Rejected per spec FR-020.

---

### R-008: Deterministic Initialization

**Question**: What state should the SubOscillator be in after construction and prepare() to guarantee deterministic rendering?

**Decision**: All flip-flop states initialized to false (0). Sub phase accumulator initialized to 0.0. Residual buffer cleared. Mix defaults to 0.0 (main only). Waveform defaults to Square. Octave defaults to OneOctave.

**Rationale**: Deterministic initialization is mandatory for DAW offline rendering (bounce-to-disk) consistency. Two render passes must produce bit-identical output. Initializing flip-flops to false means the first transition is always false->true (rising edge), providing a consistent starting behavior.

**Verified against codebase patterns**: The `SyncOscillator` initializes all boolean states (reversed_) to false in constructor and prepare(). The `PolyBlepOscillator` initializes phaseWrapped_ to false. This is the established pattern.

---

## Existing Code Reuse Summary

| Component | File | How Used |
|-----------|------|----------|
| `PhaseAccumulator` | `core/phase_utils.h` | Internal phase tracking for sine/triangle sub waveforms |
| `wrapPhase()` | `core/phase_utils.h` | Phase wrapping for sine/triangle accumulator |
| `subsamplePhaseWrapOffset()` | `core/phase_utils.h` | Sub-sample offset for minBLEP timing |
| `MinBlepTable` | `primitives/minblep_table.h` | Shared table for band-limited step corrections |
| `MinBlepTable::Residual` | `primitives/minblep_table.h` | Per-instance ring buffer for minBLEP corrections |
| `equalPowerGains()` | `core/crossfade_utils.h` | Equal-power crossfade for processMixed() |
| `kTwoPi`, `kPi` | `core/math_constants.h` | Sine waveform computation |
| `detail::isNaN()`, `detail::isInf()` | `core/db_utils.h` | Input/output sanitization |
| `sanitize()` pattern | `processors/sync_oscillator.h` | Output clamping pattern (reimplemented, not called) |

---

## New Code Required

| Component | Description |
|-----------|-------------|
| `SubOctave` enum | File-scope enum: `OneOctave` (0), `TwoOctaves` (1) |
| `SubWaveform` enum | File-scope enum: `Square` (0), `Sine` (1), `Triangle` (2) |
| `SubOscillator` class | Layer 2 processor with flip-flop division, phase tracking, minBLEP, mix |
| `sub_oscillator_test.cpp` | Unit tests covering all FRs and SCs |

No new Layer 0 utilities need to be extracted. All needed utilities already exist.
