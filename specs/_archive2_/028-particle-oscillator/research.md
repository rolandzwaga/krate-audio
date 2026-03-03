# Research: Particle / Swarm Oscillator

**Feature Branch**: `028-particle-oscillator`
**Date**: 2026-02-06

---

## R-001: Particle Pool Management Pattern

**Question**: What pattern should be used for the fixed-size particle pool with voice stealing?

**Decision**: Use a flat `std::array<Particle, 64>` with `active` flag per particle, following the existing `GrainPool` and `FormantOscillator` patterns in this codebase.

**Rationale**: The codebase already uses this pattern successfully in multiple places:
- `GrainPool` (`dsp/include/krate/dsp/primitives/grain_pool.h`): Fixed `std::array<Grain, 64>` with acquire/release/steal-oldest
- `FormantOscillator` (`dsp/include/krate/dsp/processors/formant_oscillator.h`): Fixed `std::array<FOFGrain, 8>` per formant with oldest-grain recycling

The particle pool differs from GrainPool in that particles have different fields (phase accumulator, drift state, scatter offset) and the pool is managed directly by the oscillator rather than being a separate class. This matches the FormantOscillator approach where grain management is internal.

**Alternatives Considered**:
- Extracting a generic template pool class: Premature abstraction; the Grain struct has delay-specific fields (readPosition, playbackRate, reverse) making it non-generic
- Using GrainPool directly: Fields are incompatible (delay buffer positions vs. phase accumulators)
- Separate ParticlePool class: Adds unnecessary indirection for a single consumer

---

## R-002: Spawn Scheduling Implementation

**Question**: Should the GrainScheduler be reused/refactored, or should a new spawn scheduler be implemented?

**Decision**: Implement spawn scheduling directly within ParticleOscillator, using the GrainScheduler's timing logic as a reference pattern. Do NOT refactor GrainScheduler.

**Rationale**: The GrainScheduler is designed for a density-in-grains-per-second model with jitter control. The ParticleOscillator needs a different model:
- **Regular mode**: Interval = lifetime / density (not just 1/density)
- **Random mode**: Poisson-like process at average rate = density / lifetime
- **Burst mode**: Event-driven (triggerBurst()), not time-based at all

The scheduling semantics differ enough that refactoring GrainScheduler to support both would make it more complex without clear benefit. The timing calculation is simple enough (10-20 lines) to implement inline.

**Key Implementation Details**:
- Regular: `interonsetSamples = lifetimeSamples / density`. Counter decrements per sample, triggers spawn at zero.
- Random: Same average rate but randomized intervals. Use `interonsetSamples * (1.0 + rng.nextFloat())` for exponential-like distribution (simpler than true Poisson, similar statistical properties for audio).
- Burst: No scheduler counter. All particles spawned in `triggerBurst()`. No automatic spawning.

**Alternatives Considered**:
- Refactoring GrainScheduler with Burst mode: Would require adding a new SchedulingMode enum value to an existing class that already works for granular delay, risking regression
- Creating a shared SpawnScheduler utility: Only one consumer so far; premature extraction

---

## R-003: Per-Particle Drift Implementation

**Question**: How should the low-pass filtered random walk for frequency drift be implemented?

**Decision**: Each particle stores its own one-pole low-pass filter state. Per sample: generate white noise from Xorshift32, filter through one-pole with ~10 Hz cutoff, scale by drift amount and scatter range.

**Rationale**: The spec requires 5-20 Hz effective cutoff for the drift filter. A one-pole low-pass is the simplest and cheapest filter that achieves this. The existing `OnePoleSmoother` from `dsp/include/krate/dsp/primitives/smoother.h` is too feature-rich (completion detection, NaN safety, snap-to) and would add overhead for 64 particles. A simple inline one-pole is more efficient.

**Key Implementation Details**:
- Per-particle state: `float driftState = 0.0f` (filter state), `float driftFilterCoeff = 0.0f` (computed at prepare)
- Coefficient calculation: `coeff = exp(-2*pi*cutoffHz / sampleRate)` where cutoffHz = 10 Hz (midpoint of 5-20 Hz range)
- Per-sample update: `driftState = coeff * driftState + (1.0 - coeff) * rng.nextFloat()`
- Frequency deviation: `deviationHz = driftState * driftAmount * scatterRange * centerFreq`
- This gives smooth, continuous frequency wandering proportional to both drift amount and scatter setting

**Alternatives Considered**:
- Using OnePoleSmoother per particle: 64 instances of a heavier class with unnecessary features (NaN checks, completion detection)
- Using a shared noise source with per-particle filters: More complex, same result
- Precomputed drift tables: Would create repetitive patterns; per-particle random walk is more organic

---

## R-004: Envelope Table Precomputation Strategy

**Question**: How should envelope tables for all GrainEnvelopeType values be organized?

**Decision**: Pre-allocate a 2D array `std::array<std::array<float, kEnvTableSize>, kNumEnvelopeTypes>` as a class member, computed during `prepare()`. Use an index/pointer to select the current envelope type.

**Rationale**: The spec (FR-021) requires precomputing ALL envelope types during prepare() and switching via pointer/index swap. The existing `GrainEnvelope::generate()` function accepts a `float*` output buffer and size, making it straightforward to fill each sub-array. With 6 envelope types and 256 samples each, the total memory is 6 * 256 * 4 = 6,144 bytes -- trivial.

**Key Implementation Details**:
- Table size: 256 samples (spec minimum, provides smooth linear interpolation via `GrainEnvelope::lookup()`)
- Number of types: 6 (Hann, Trapezoid, Sine, Blackman, Linear, Exponential)
- Storage: `std::array<std::array<float, 256>, 6> envelopeTables_`
- Current type index: `size_t currentEnvType_ = 0` (index into outer array, default Hann)
- Switching: `setEnvelopeType()` just updates the index -- O(1), real-time safe
- Lookup: `GrainEnvelope::lookup(envelopeTables_[currentEnvType_].data(), 256, phase)`

**Alternatives Considered**:
- Generating tables on-demand per type change: Violates real-time safety if called during processing
- Larger table (512, 1024): Diminishing returns for particle synthesis; 256 is sufficient for smooth grain envelopes
- Pointer swap instead of index: Equivalent performance, index is simpler and avoids dangling pointer risk

---

## R-005: Normalization Strategy

**Question**: How exactly should `1/sqrt(density)` normalization be applied?

**Decision**: Store `normFactor = 1.0f / std::sqrt(density)` as a cached member, updated whenever density changes. Multiply the summed output by this factor every sample.

**Rationale**: The spec (FR-016) is explicit: use target density setting, not active count. This means the normalization factor is constant as long as density doesn't change, making it efficient to cache. Using `1/sqrt(N)` for N uncorrelated sine waves provides equal-power summation -- the RMS stays approximately constant regardless of N.

**Key Implementation Details**:
- Cache: `float normFactor_ = 1.0f` (updated in `setDensity()`)
- Computation: `normFactor_ = 1.0f / std::sqrt(std::max(1.0f, density))`
- Application: After summing all particles: `output *= normFactor_`
- Edge case: Density 0 is clamped to minimum (no particles, no output), so normFactor is always well-defined
- Smoothing: Use OnePoleSmoother for density changes to avoid clicks, but the normFactor can snap (it's multiplicative, not additive)

**Alternatives Considered**:
- Active-count normalization: Causes amplitude pumping during spawn/expire transitions (explicitly rejected in spec clarification)
- Hard limit (1/N): Under-normalizes; N=64 would make individual particles barely audible
- Peak normalization: Would require lookahead or tracking, too complex for real-time

---

## R-006: Phase Accumulator and Sine Generation

**Question**: What approach should be used for per-particle sine wave generation?

**Decision**: Use a normalized phase accumulator [0, 1) per particle with `std::sin(kTwoPi * phase)` for sample generation. Phase increment = `frequency / sampleRate`.

**Rationale**: The spec explicitly states (Assumptions section): "The component uses direct `std::sin()` for per-particle phase-to-sample conversion. MSVC's `std::sin` is highly optimized." The FormantOscillator already uses this exact pattern (`std::sin(kTwoPi * grain.phase)` at line 555). For 64 sine evaluations per sample at 44.1kHz, this is well within the Layer 2 budget.

**Key Implementation Details**:
- Per-particle state: `float phase = 0.0f` in [0, 1)
- Per-sample: `phase += phaseIncrement; if (phase >= 1.0f) phase -= 1.0f;`
- Phase increment at spawn: `particleFreqHz / sampleRate`
- Output: `std::sin(kTwoPi * phase) * envelopeValue`

**Alternatives Considered**:
- Wavetable lookup: More complex, no measurable benefit at 64 evaluations per sample
- fastSin: Spec explicitly notes it was slower than `std::sin` on MSVC
- Direct accumulation in radians: Normalized [0,1) is cleaner and matches codebase convention

---

## R-007: Output Sanitization Pattern

**Question**: How should NaN/Inf detection and output clamping be implemented?

**Decision**: Reuse `detail::isNaN()` and `detail::isInf()` from `dsp/include/krate/dsp/core/db_utils.h` for detection. Clamp output to [-2.0, +2.0] per FR-017. This matches the exact pattern in `AdditiveOscillator::sanitizeOutput()`.

**Rationale**: The existing `sanitizeOutput()` in AdditiveOscillator (line 602-610) does exactly what FR-017 requires: NaN/Inf check followed by clamping to [-2, +2]. Reusing this pattern ensures consistency.

**Key Implementation Details**:
```cpp
[[nodiscard]] static float sanitizeOutput(float x) noexcept {
    if (detail::isNaN(x) || detail::isInf(x)) return 0.0f;
    return std::clamp(x, -2.0f, 2.0f);
}
```

---

## R-008: Spawn Timing Under Mode Transitions

**Question**: How should switching between spawn modes be handled safely?

**Decision**: When switching spawn modes, let existing particles expire naturally. Reset the spawn counter. In Burst mode, stop automatic spawning. SC-006 requires no clicks/pops, which is guaranteed because particles always have envelopes.

**Rationale**: Particles are always envelope-shaped (Hann by default), so they fade in and out smoothly regardless of spawn timing. The only risk of clicks is from abrupt silence, which doesn't happen because existing particles continue to completion.

**Key Implementation Details**:
- `setSpawnMode()`: Store new mode, reset `samplesUntilNextSpawn_` to 0 (immediate recalculation on next process)
- Switching TO Burst: Existing particles continue; no new auto-spawns
- Switching FROM Burst to Regular/Random: Counter starts immediately, new spawns begin filling the pool
- No kill-all or reset needed for safe transitions

---

## R-009: Particle Frequency Clamping at Spawn

**Question**: How should out-of-range particle frequencies be handled?

**Decision**: After computing `centerFreq * semitonesToRatio(scatterOffset)`, clamp the result to [1.0, nyquist - 1.0]. Do not skip spawning the particle.

**Rationale**: The spec says frequencies below 1 Hz are clamped and frequencies above Nyquist are clamped. Clamping is simpler than skipping and ensures the density count stays accurate. A particle at 1 Hz or near Nyquist will still be envelope-shaped and contribute to the texture without aliasing issues (single sine at boundary frequencies doesn't alias).

---

## R-010: Deterministic Seed for Testing

**Question**: How should the PRNG be seeded for deterministic testing?

**Decision**: Default constructor uses a fixed seed (e.g., 12345). Provide a `seed(uint32_t)` method that resets the internal Xorshift32.

**Rationale**: The spec (Assumptions section) states "The default random seed produces deterministic output for testing. A `seed()` method is provided for reproducibility control." This matches the GrainScheduler pattern (`seed(uint32_t seedValue)` at line 97).

**Key Implementation Details**:
- Default: `Xorshift32 rng_{12345}`
- Method: `void seed(uint32_t seedValue) noexcept { rng_.seed(seedValue); }`
- SC-005 requires different output after reset with different seed
