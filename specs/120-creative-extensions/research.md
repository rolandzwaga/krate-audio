# Research: Innexus M6 -- Creative Extensions

**Branch**: `120-creative-extensions` | **Date**: 2026-03-05

## Research Tasks

### R-001: Stereo Pan Law for Additive Synthesis

**Decision**: Constant-power pan law using `left = cos(angle)`, `right = sin(angle)` where `angle = pi/4 + pan * pi/4`.

**Rationale**: Constant-power panning preserves perceived loudness when partials are distributed across the stereo field. This is the standard approach in additive synthesizers (Alchemy, Razor, Harmor). The `cos/sin` formulation is mathematically equivalent to `sqrt`-based constant power but uses only one trig call pair per partial per frame (not per sample), making it negligible cost.

**Alternatives considered**:
- Linear pan law (`left = 1-pan, right = pan`): Causes ~3dB drop at center. Not suitable for partial-level panning where many partials sum.
- Equal-power with `sqrt`: Equivalent result but `cos/sin` is more conventional in DSP literature and handles the angle-to-coefficient mapping more naturally.
- Pre-computed lookup table for pan coefficients: Unnecessary since pan is recalculated per frame (not per sample) and we have at most 48 partials.

### R-002: LFO Waveform Generation Without Heap Allocation

**Decision**: Lightweight formula-based `ModulatorLfo` class (plugin-local) using direct computation per waveform type. No wavetable, no `std::vector`.

**Rationale**: The existing Layer 1 `LFO` class (`dsp/include/krate/dsp/primitives/lfo.h`) uses `std::vector` for wavetable storage, which means heap allocation in `prepare()`. While this is acceptable for the Layer 1 class (prepare is not real-time), the harmonic modulators need a simpler LFO that can be fully stack-allocated and has zero heap overhead. The five required waveforms (Sine, Triangle, Square, Saw, Random S&H) are all trivially computable from a phase accumulator.

**Formulas**:
- **Sine**: `sin(2 * pi * phase)`
- **Triangle**: `4 * |phase - 0.5| - 1` (bipolar) or `2 * |2 * phase - 1|` (unipolar variant)
- **Square**: `phase < 0.5 ? 1.0 : -1.0`
- **Saw**: `2 * phase - 1`
- **Random S&H**: Hold `Xorshift32::nextFloat()` value, update when phase wraps past 1.0

**Alternatives considered**:
- Reuse existing Layer 1 `LFO`: Rejected due to `std::vector` allocation and feature bloat (tempo sync, symmetry, quantization not needed).
- Wavetable LFO with fixed-size `std::array`: Would work but adds unnecessary memory (256+ floats per waveform x 5 waveforms x 2 modulators = 2560 floats) for waveforms that are trivially computed.
- BLIT-based anti-aliased waveforms: Unnecessary -- these LFOs operate at sub-audio rates (0.01-20 Hz), so aliasing is not a concern.

### R-003: Evolution Engine Interpolation Strategy

**Decision**: Use existing `lerpHarmonicFrame()` and `lerpResidualFrame()` for pairwise interpolation between adjacent waypoints. The evolution engine maps a continuous position [0, 1] to a pair of adjacent waypoints and a local interpolation factor.

**Rationale**: The `lerpHarmonicFrame()` function already handles partial count mismatches (missing partials default to zero amplitude or ideal harmonic ratio), which is exactly the behavior needed for evolution between snapshots with different partial counts. No new interpolation code is needed.

**Implementation detail**: Given N waypoints, the position [0, 1] maps to segment `floor(pos * (N-1))` with local t = `frac(pos * (N-1))`. For Cycle mode with wrapping, use `fmod(phase, 1.0)` to map position and treat waypoints as circular (last waypoint interpolates to first).

**Alternatives considered**:
- Catmull-Rom spline interpolation across 4 waypoints: Smoother trajectories but significantly more complex, and `lerpHarmonicFrame()` would need a 4-point variant. Deferred to future enhancement.
- Direct snapshot blending (weighted sum of all waypoints): This is what Multi-Source Blend does. Evolution is specifically about sequential traversal, not weighted mixing.
- Per-partial phase interpolation: Not needed because the oscillator bank maintains phase continuity internally (MCF phasors are never reset on frame changes).

### R-004: Cross-Synthesis Pure Harmonic Reference

**Decision**: Pre-compute a static `HarmonicSnapshot` at `prepare()` time containing the pure harmonic series with `1/n` amplitude rolloff, L2-normalized. This snapshot is a compile-time constant structure.

**Rationale**: The pure harmonic reference is invariant -- it does not depend on sample rate, MIDI note, or source model. Computing it once at prepare time (or even as a constexpr) avoids per-frame computation. L2-normalization ensures the blend between pure and source models is energy-preserving.

**Formula**: For partial n (1-based):
- `relativeFreq_n = n` (perfect integer harmonics)
- `rawAmp_n = 1.0 / n` (natural harmonic rolloff)
- `inharmonicDeviation_n = 0.0`
- L2 norm: `norm = sqrt(sum(rawAmp_n^2))` for n=1..48
- `normalizedAmp_n = rawAmp_n / norm`

The L2 norm of `1/n` for n=1..48 is `sqrt(H_48^{(2)})` where `H_k^{(2)} = sum(1/n^2, n=1..k)`. This converges to `pi^2/6 ~= 1.6449`. For 48 terms, `H_48^{(2)} ~= 1.6228`, so `norm ~= 1.2739`.

**Alternatives considered**:
- Flat spectrum (equal amplitude): Sounds harsh and unnatural as the "clean" reference.
- `1/n^2` rolloff: Too dark/muffled for a useful reference timbre.
- Per-frame computation: Wasteful since the reference never changes.

### R-005: Detune Spread Implementation

**Decision**: Apply per-partial frequency offset in cents, alternating direction by odd/even harmonic index. Offset scales linearly with harmonic number. Apply as a frequency multiplier: `pow(2.0, detuneOffset / 1200.0)`.

**Rationale**: Linear scaling with harmonic number creates a natural chorus effect where higher partials spread more (as in real acoustic instruments with slight inharmonicity). Alternating odd/even direction maximizes perceived width when combined with stereo spread.

**Formula**: `detuneOffset_n = detuneSpread * n * kDetuneMaxCents * direction_n` where:
- `kDetuneMaxCents = 15` (maximum cents at full spread for harmonic 1)
- `direction_n = (harmonicIndex % 2 == 1) ? +1 : -1`
- Final frequency: `freq_n *= pow(2.0, detuneOffset_n / 1200.0)`

For efficiency, `pow(2.0, x/1200.0)` can be approximated as `1.0 + x * 0.000577623` for small x (|x| < 100 cents). At 15 cents * 48 = 720 cents max, the approximation error is ~0.3%, which is inaudible for a detuning effect. However, for correctness and simplicity, use `std::pow` since this runs once per frame per partial (48 ops per frame, not per sample).

**Alternatives considered**:
- Random per-partial detune offsets: Less controllable, harder to test deterministically.
- Fixed detune pattern (not scaling with harmonic number): Sounds less natural -- higher partials should drift more.
- Per-sample detune modulation: Unnecessary computational cost for a static offset that only changes with the spread parameter.

### R-006: Multi-Source Blending Weight Normalization

**Decision**: Normalize weights by dividing each by the sum of all nonzero weights. If all weights are zero, output silence.

**Rationale**: Weight normalization ensures consistent output level regardless of how many sources are active or how the user sets individual weights. Without normalization, adding a third source would increase the output level.

**Formula**:
```
totalWeight = sum(weight_i for all occupied slots with weight > 0) + liveWeight
if (totalWeight > 0):
    effectiveWeight_i = weight_i / totalWeight
else:
    all effectiveWeights = 0 (silence)
```

**Alternatives considered**:
- No normalization (raw weighted sum): Output level varies with number of sources and total weight. Confusing for users.
- Clamped-sum normalization (only normalize if sum > 1): Allows intentional "overdrive" below 1.0 but asymmetric behavior is confusing.
- Per-partial energy normalization: More perceptually accurate but much more complex and harder to predict.

### R-007: State Versioning Strategy (v5 to v6)

**Decision**: Increment state version from 5 to 6. On `setState()`, detect version: if v5, read all M1-M5 parameters and initialize M6 parameters to defaults. If v6, read all parameters including M6 block.

**Rationale**: This follows the same backward-compatible versioning pattern used for all previous milestones (v1->v2->v3->v4->v5). The version number is the first field in the binary state stream. New parameters are appended at the end of the stream, so old hosts that wrote v5 state will simply have fewer bytes, and `setState()` stops reading after the v5 fields.

**Alternatives considered**:
- Tagged/TLV format: More flexible but breaks the established binary stream pattern used in v1-v5. Would require migrating all existing state loading.
- Separate state chunk for M6: VST3 does not support multiple state chunks per component. The processor has one `getState()`/`setState()` pair.

### R-008: SIMD Viability for M6 Per-Partial Operations

**Decision**: DEFER SIMD optimization. Implement scalar first, optimize only if SC-008 budget is exceeded.

**Rationale**: The M6 per-partial operations (pan coefficient computation, modulator application, detune offset computation, blend interpolation) all operate on arrays of 48 floats. The SoA layout of `HarmonicOscillatorBank` is already SIMD-friendly (alignas(32), contiguous arrays). However, the operations are performed per-frame (not per-sample), so the total per-sample cost is negligible. The main per-sample cost is the MCF oscillator loop in `process()` which already runs at ~0.38% CPU. Adding two multiply-adds per partial for stereo panning adds ~48 * 2 = 96 ops per sample, which is well within budget.

**Alternatives considered**:
- Immediate SIMD implementation: Premature optimization. The per-frame operations (pan, modulator, detune, blend) are too cheap to justify SIMD complexity.
- Google Highway for per-sample stereo: Could vectorize the MCF + pan loop but would require restructuring the inner loop significantly. Only justified if benchmarks show budget exceeded.

### R-009: ODR (One Definition Rule) Conflict Check

**Decision**: All proposed new type names verified unique across the codebase.

**Search results**:
- `ModulatorLfo`: No matches found
- `EvolutionEngine`: No matches found
- `HarmonicModulator`: No matches found
- `HarmonicBlender`: No matches found
- `EvolutionMode`: No matches found (existing `HarmonicFilterType` is in plugin_ids.h, different namespace)
- `ModulatorWaveform`: No matches found
- `ModulatorTarget`: No matches found

All new types will be in the `Innexus` namespace (plugin-local) or defined locally within the class, preventing any ODR violations with KrateDSP types in `Krate::DSP`.
