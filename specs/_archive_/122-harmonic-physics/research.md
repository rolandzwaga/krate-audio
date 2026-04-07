# Research: Harmonic Physics (122)

**Date**: 2026-03-06 | **Spec**: spec.md

## R1: Warmth Transform - tanh Saturation Behavior

**Decision**: Use `tanh(drive * amp) / tanh(drive)` with `drive = exp(Warmth * ln(8.0))`.

**Rationale**: The tanh function is the standard soft-saturation curve used across DSP. It maps `[0, inf)` to `[0, 1)`, providing natural compression. Dividing by `tanh(drive)` normalizes the output so that an input amplitude of 1.0 maps to exactly 1.0 regardless of drive, preserving the unit range. The exponential drive mapping (`exp(W * ln(8))`) provides perceptually uniform control: low Warmth values cause subtle compression; high values cause aggressive compression.

**Key property - bypass**: When `drive = 1.0`, `tanh(1*x)/tanh(1)` is NOT the identity function. The spec requires bit-exact bypass at Warmth=0, so the implementation MUST short-circuit entirely when Warmth < epsilon (e.g., `1e-6f`), not merely set `drive = 1.0`.

**Alternatives considered**:
- `pow(amp, 1/drive)`: Simpler but doesn't provide the asymptotic compression behavior of tanh.
- `amp / (1 + amp * drive)`: Rational saturation. Similar curve shape but less standard.
- Per-partial soft-clip: Would need to handle each partial differently based on frequency. Overkill for this use case.

## R2: Coupling - Energy Conservation Strategy

**Decision**: Use sum-of-squares normalization after neighbor blending.

**Rationale**: The coupling formula `coupled[i] = amp[i] * (1-c) + (amp[i-1] + amp[i+1]) * 0.5 * c` naturally changes total energy. To conserve energy (SC-002), compute `inputEnergy = sum(amp[i]^2)` and `outputEnergy = sum(coupled[i]^2)`, then scale all outputs by `sqrt(inputEnergy / outputEnergy)`. This preserves the perceptual loudness while allowing the coupling to redistribute energy.

**Implementation detail**: A temporary buffer is required to avoid read-after-write hazard. Use a `std::array<float, 48>` local variable to store coupled amplitudes before writing back.

**Bypass**: When `coupling < epsilon`, short-circuit entirely for bit-exact bypass (FR-007).

**Alternatives considered**:
- Iterative relaxation (Gauss-Seidel style): Simpler but read-after-write creates asymmetric coupling.
- Matrix multiply approach: Exact but O(N^2) instead of O(N).
- Tridiagonal coupling matrix: Mathematically elegant but overkill for nearest-neighbor.

## R3: Dynamics Agent System - State Management

**Decision**: Per-partial agent state stored in fixed-size arrays within a `HarmonicDynamicsProcessor` class.

**Rationale**: With `kMaxPartials = 48`, fixed arrays are ideal -- no dynamic allocation, cache-friendly, and the total state is only ~768 bytes (48 partials * 4 floats * 4 bytes). The agent model uses:
- `agentAmplitude[i]`: Smoothed output amplitude (inertia target)
- `velocity[i]`: Rate of change (for future momentum features, initially delta tracking)
- `persistence[i]`: Stability-over-time measure [0,1] that grows when partial is stable, decays on large changes
- `energyShare[i]`: Reserved for energy budget allocation

**Frame timing**: `processFrame()` is called once per analysis frame (not per audio sample), co-located with `loadFrame()`. The persistence growth/decay rates are computed from hop size in `prepare(sampleRate, hopSize)` to be perceptually consistent across sample rates.

**First-frame initialization**: On the first frame after `reset()`, agent amplitudes are initialized from the input frame (no ramp-from-zero artifact). A `firstFrame_` flag tracks this.

**Alternatives considered**:
- Per-sample dynamics processing: Unnecessary computational cost for frame-rate data.
- Shared agent state across partials: Would lose per-partial temporal identity.
- External agent state (e.g., in processor.h): Would violate encapsulation. Better to keep state in the dynamics processor class.

## R4: Parameter Smoothing Strategy

**Decision**: Reuse existing `Krate::DSP::OnePoleSmoother` with `advanceSamples()` for frame-rate updates.

**Rationale**: The `OnePoleSmoother` is already used throughout Innexus (harmonicLevelSmoother_, stereoSpreadSmoother_, etc.). Its `advanceSamples(blockSize)` method provides O(1) advancement using the closed-form exponential formula, which is perfect for frame-rate processing where we advance by a full audio block at once. The default smoothing time of 5ms is appropriate for zipper-free parameter changes.

**Usage pattern**: In `processParameterChanges()`, store atomic values. In the process loop, set smoother targets from atomics and call `advanceSamples(numSamples)` per block. Read `getCurrentValue()` at frame update points.

**Alternatives considered**:
- Linear ramp smoother: Discontinuity at ramp end.
- Custom frame-rate smoother: Unnecessary when OnePoleSmoother handles this via advanceSamples.

## R5: Processing Chain Order

**Decision**: Coupling -> Warmth -> Dynamics -> loadFrame()

**Rationale**: This order is specified in FR-020 and makes physical sense:
1. **Coupling first**: Redistributes energy between neighbors, establishing the "connected" harmonic field.
2. **Warmth second**: Compresses the coupled result, preventing any single harmonic from dominating after coupling.
3. **Dynamics third**: Applies inertia and decay to the final processed amplitudes, providing temporal smoothing.
4. **loadFrame() last**: Feeds the fully processed frame to the oscillator bank.

This matches the physical analogy: a vibrating body first has sympathetic resonances (coupling), then nonlinear saturation (warmth), then the overall damping/inertia behavior (dynamics).

## R6: Code Location and Architecture

**Decision**: All three processors in a single header `plugins/innexus/src/dsp/harmonic_physics.h` within the `Innexus` namespace.

**Rationale**: Following the existing pattern set by `harmonic_modulator.h` and `evolution_engine.h` -- plugin-local DSP classes that are header-only and live in `plugins/innexus/src/dsp/`. These are NOT placed in the shared KrateDSP library because:
1. They are specific to Innexus's harmonic frame processing paradigm.
2. They depend on `Krate::DSP::HarmonicFrame` which is already in Layer 2.
3. If future plugins need similar functionality, extraction can happen then (YAGNI).

The three processors share the same file because they form a cohesive unit (the "harmonic physics system") and share common includes/dependencies.

**Class structure**:
- `HarmonicPhysics`: Main class that owns all three sub-processors and provides a single `processFrame()` entry point, plus individual setters.
- Warmth and coupling are stateless functions within the class.
- Dynamics uses internal state arrays.

**Alternative considered**: Three separate files. Rejected because the total code is small (~200 lines) and they share dependencies.

## R7: Insertion Points in processor.cpp

**Decision**: Create a single helper method `applyHarmonicPhysics()` and call it at all existing insertion points where `applyModulatorAmplitude()` is called.

**Rationale**: There are 7 distinct code paths in `processor.cpp` that call `applyModulatorAmplitude()` followed by `oscillatorBank_.loadFrame()`. Rather than duplicating physics processing code at each site, a single method `applyHarmonicPhysics()` encapsulates the Coupling -> Warmth -> Dynamics chain. This method is called AFTER `applyModulatorAmplitude()` and BEFORE `loadFrame()`.

**Insertion sites** (lines approximate, all in `processor.cpp`):
1. Line 806 (sample mode, note-on first frame)
2. Line 859 (sidechain live frame)
3. Line 1017 (sample mode, frame advance)
4. Line 1072 (sample mode, sidechain with new frame)
5. Line 1141 (sample mode, another code path)
6. Line 1457 (per-sample live mode)
7. Line 1497 (live mode fallback)

## R8: Energy Budget for Dynamics

**Decision**: Use `globalAmplitude^2` from `HarmonicFrame` as the energy budget.

**Rationale**: `HarmonicFrame::globalAmplitude` is the smoothed RMS of the source signal, already available in every frame. Squaring it gives a power measure that serves as the energy ceiling. When the sum-of-squares of agent amplitudes exceeds this budget, all amplitudes are scaled down by `sqrt(budget / total)`. When `globalAmplitude` is zero, the conservation step is skipped to avoid division by zero. This naturally ties the dynamics system's output energy to the input signal level.

## R9: SIMD Viability

**Decision**: NOT BENEFICIAL for this feature.

**Rationale**: With only 48 partials processed once per analysis frame (not per audio sample), the total computation is negligible. SC-006 targets < 0.5% CPU overhead. The operations are simple arithmetic (tanh, multiply, add) on a tiny dataset that fits entirely in L1 cache. SIMD would add complexity without meaningful performance gain. The `tanh` function call in warmth is the most expensive operation but only runs 48 times per frame.

**Alternative optimizations**:
- Early-out when parameter is 0.0 (bypass): Already required for FR-002/FR-007/FR-013.
- Fast tanh approximation: Only if profiling shows tanh is a bottleneck (unlikely at 48 calls per frame).
