# Research: Dual Reverb System

**Date**: 2026-03-11 | **Spec**: specs/125-dual-reverb/spec.md

## R1: Householder Feedback Matrix for N=8

**Decision**: Use Householder matrix `A = I - (2/N) * u * u^T` where `u = [1,1,...,1]^T`

**Rationale**: For N=8, the Householder matrix gives:
- Diagonal entries: `1 - 2/8 = 0.75`
- Off-diagonal entries: `-2/8 = -0.25`
- All entries nonzero (every delay feeds back to every other)
- Computation: `y_i = x_i - (2/N) * sum(all x)` = N additions for the sum + 1 multiply to scale by 2/N + N subtractions for the output = O(N) per call
- Total for N=8: 8 additions (sum) + 1 multiply (scale) + 8 subtractions (output) = 17 arithmetic operations per sample
- Much cheaper than general N^2 matrix or even N*log2(N) Hadamard

**Implementation**:
```cpp
// Householder: y[i] = x[i] - (2/N) * sum
float sum = 0.0f;
for (int i = 0; i < 8; ++i) sum += x[i];
float scaled = sum * (2.0f / 8.0f);  // = sum * 0.25
for (int i = 0; i < 8; ++i) y[i] = x[i] - scaled;
```

**SIMD approach**: With Highway, the sum can be computed via `hn::ReduceSum()`, then broadcast and subtract. For N=8 with 4-wide SIMD (SSE), this is 2 loads + 2 horizontal sums + 2 broadcast-subtract-stores.

**Alternatives considered**:
- Hadamard: More complex butterfly structure, O(N*log2N). Better mixing for large N but Householder is sufficient and cheaper for N=8.
- Unitary random: N^2 multiplies, no advantage.

**Source**: [CCRMA Householder Feedback Matrix](https://ccrma.stanford.edu/~jos/pasp/Householder_Feedback_Matrix.html)

## R2: Hadamard Diffuser for Feedforward Stage

**Decision**: Use Fast Walsh-Hadamard Transform (FWHT) butterfly for the 8-channel diffuser

**Rationale**: The feedforward diffuser stage (before the feedback loop) needs to create echo density. Each diffusion step multiplies echo count by N. The FDN uses exactly 4 cascaded diffuser steps (FR-008, `kNumDiffuserSteps = 4`). Each step reads from a dedicated diffuser delay section, then applies one FWHT invocation. The FWHT itself consists of 3 butterfly stages (log2(8) = 3):
- 3 butterfly stages for N=8 (log2(8) = 3) — this is the stage count within one FWHT call
- Each stage: N/2 = 4 add/subtract pairs = 8 operations
- Total per FWHT call: 3 * 8 = 24 additions (no multiplies needed)
- Normalization: divide by sqrt(N) = sqrt(8) = 2*sqrt(2) once per FWHT call
- Total diffuser: 4 steps × 1 delay read/write + 4 FWHT calls

**Clarification**: FR-008 specifies 4 cascaded *steps*, not 3-4. The "3" refers to the butterfly stage count per FWHT invocation (fixed by log2(8)), not the number of diffuser steps.

**Implementation (in-place FWHT for N=8)**:
```cpp
// Stage 1: stride=4
for (int i = 0; i < 4; ++i) {
    float a = x[i], b = x[i+4];
    x[i] = a + b; x[i+4] = a - b;
}
// Stage 2: stride=2
for (int k = 0; k < 8; k += 4) {
    for (int i = 0; i < 2; ++i) {
        float a = x[k+i], b = x[k+i+2];
        x[k+i] = a + b; x[k+i+2] = a - b;
    }
}
// Stage 3: stride=1
for (int k = 0; k < 8; k += 2) {
    float a = x[k], b = x[k+1];
    x[k] = a + b; x[k+1] = a - b;
}
// Normalize
float norm = 1.0f / std::sqrt(8.0f);
for (int i = 0; i < 8; ++i) x[i] *= norm;
```

**SIMD approach**: Stages 1 and 2 map naturally to 4-wide SIMD (load 4 values, add/subtract paired vectors). Stage 3 requires interleave/deinterleave but can use `hn::InterleaveLower`/`hn::InterleaveUpper` or manual shuffle.

**Alternatives considered**:
- Direct matrix multiply: 64 multiplies + 56 additions. Much slower.
- Skip diffuser entirely: Poor echo density, metallic sound.

**Source**: [Signalsmith - Let's Write A Reverb](https://signalsmith-audio.co.uk/writing/2021/lets-write-a-reverb/)

## R3: FDN Delay Line Length Selection

**Decision**: Use exponential spacing with coprimality constraints per FR-009

**Rationale**: The spec provides exact reference values at 48kHz: `base=149, r=1.27 -> [149, 189, 240, 305, 387, 492, 625, 794]` samples. These satisfy:
- 3ms minimum (149/48000 = 3.1ms) and 20ms maximum (794/48000 = 16.5ms)
- Coprimality: verified no pairwise GCD exceeds 8
- Anti-ringing: checked short feedback cycles

At other sample rates, scale proportionally: `d_i(sr) = round(d_i(48k) * sr / 48000)`

**Validation tool**: A compile-time or design-time check computes arrival-time histograms for 1ms bins within 50ms. The spec requires >= 50 unique occupied bins. This can be a unit test.

**Alternatives considered**:
- Prime number delays: Good coprimality but spacing is irregular
- Fibonacci-based: Limited control over range
- Random: Unpredictable density distribution

## R4: Gordon-Smith Phasor for Reverb LFO

**Decision**: Replace `std::sin(lfoPhase_)` / `std::cos(lfoPhase_)` with Gordon-Smith magic circle

**Rationale**: Proven pattern in this codebase (`particle_oscillator.h`). For the reverb LFO:
- Current: 2x `std::sin`/`std::cos` calls per sample (when modulation active)
- Optimized: 2 multiplies + 2 adds per sample
- Identical to particle oscillator pattern:
  ```
  epsilon = 2 * sin(pi * freq / sampleRate)
  s_new = s + epsilon * c
  c_new = c - epsilon * s_new
  ```
- Initialize: `sinState = sin(2*pi*phase)`, `cosState = cos(2*pi*phase)`
- For quadrature output (current reverb uses sin and cos): both states are directly available as quadrature pair

**Integration detail**: The Dattorro reverb uses quadrature LFO (sin for tank A, cos for tank B). Gordon-Smith provides both s and c states naturally as a quadrature pair. No additional computation needed.

**LFO phase initialization by context**:
- **Dattorro reverb**: Single LFO initialized at phase 0 (`sinState = 0.0f, cosState = 1.0f`). The two tanks use the sin and cos states of the same oscillator as a natural quadrature pair.
- **FDN reverb**: 4 independent LFO channels, each initialized at a different starting phase (`lfoSinState_[j] = sin(j * kPi/2), lfoCosState_[j] = cos(j * kPi/2)` for j = 0..3). This spreads the 4 modulated channels 90° apart at startup, maximizing temporal decorrelation between modulated delay lines.

**Alternatives considered**:
- Wavetable LFO: Requires table storage, interpolation. More complex, proven slower in particle oscillator tests.
- Keep std::sin/cos: Works but measurably slower.

## R5: Block-Rate Parameter Smoothing

**Decision**: Process smoothers at 16-sample sub-block rate, hold values constant within each sub-block

**Rationale**: The existing `process()` method calls 9 smoothers every sample. At block rate (every 16 samples):
- 9 smoother calls reduced from N to N/16 per block
- Damping filter `setCutoff()` called every 16 samples instead of every sample (involves `std::exp` internally)
- Input diffusion `setCoefficient()` called every 16 samples
- Sub-block boundary latching: parameter changes arriving mid-block are applied at next 16-sample boundary
- At 44.1kHz, 16 samples = 0.36ms latency -- well within acceptable range

**Implementation pattern**:
```cpp
void processBlock(float* left, float* right, size_t numSamples) {
    constexpr size_t kSubBlockSize = 16;
    size_t offset = 0;
    while (offset < numSamples) {
        size_t blockLen = std::min(kSubBlockSize, numSamples - offset);
        // Update smoothers once per sub-block
        float decay = decaySmoother_.process(); // advance by 1 sample
        decaySmoother_.advanceSamples(blockLen - 1); // skip remaining
        // ... same for all other smoothers
        // Update filters with new coefficients
        tankADamping_.setCutoff(dampCutoff);
        // Process blockLen samples with held values
        for (size_t i = 0; i < blockLen; ++i) {
            processOneSample(left[offset+i], right[offset+i], decay, ...);
        }
        offset += blockLen;
    }
}
```

**Note**: `OnePoleSmoother::advanceSamples(N)` uses closed-form `coeff^N` to advance in O(1).

**Alternatives considered**:
- Per-sample smoothing: Current approach, more CPU.
- 32-sample blocks: Too coarse, may cause audible zippering on fast parameter sweeps.

## R6: Contiguous Delay Buffer Allocation

**Decision**: Allocate a single `std::vector<float>` in `prepare()` and partition into 13 sub-regions using offsets

**Rationale**: The current Dattorro reverb uses 13 separate `DelayLine` objects, each with its own `std::vector`. This causes:
- 13 separate heap allocations
- Non-contiguous memory layout (cache misses when reading across delay lines)
- Individual power-of-2 rounding wastes memory

A contiguous buffer with computed offsets:
- 1 allocation instead of 13
- Better spatial locality (all delay data in one cache-friendly region)
- Total size computed dynamically from sample rate

**Implementation detail**: The existing `DelayLine` class uses power-of-2 buffers for efficient masking. For a contiguous approach, we need either:
1. Keep individual `DelayLine` objects but point them at sub-regions (requires DelayLine API change -- invasive)
2. Use raw circular buffer logic with modular arithmetic per section

Option 1 is too invasive (changes Layer 1 primitive API). Option 2 adds complexity but is self-contained.

**Decision revision**: Rather than modifying `DelayLine`, create a lightweight `ContiguousDelayBuffer` helper within the reverb class that manages a single allocation and provides per-section read/write access. Each section still uses power-of-2 sizing internally for efficient masking, but the underlying memory is allocated as one block.

**Alternatives considered**:
- Keep separate allocations: Simpler but misses cache optimization opportunity.
- Pool allocator: Over-engineered for this use case.

## R7: Highway SIMD for 8-Channel FDN Processing

**Decision**: Use Highway for (a) 8-channel one-pole filter bank, (b) Householder matrix, (c) Hadamard butterfly

**Rationale**: The FDN processes 8 channels in parallel -- a natural fit for SIMD:

### Constitution IV Compliance: Scalar-First Workflow
Per Constitution Principle IV, SIMD implementation follows a two-phase approach:
- **Phase 1**: Implement full FDN with scalar code + complete test suite + CPU baseline
- **Phase 2**: Add Highway SIMD behind the same API, keeping scalar as fallback

### SIMD Viability Analysis

| Property | Assessment | Notes |
|----------|------------|-------|
| Feedback loops | YES | 8 delay lines feed back through Householder matrix. But parallelism is ACROSS channels, not across samples. Each sample step is serial (feedback), but the 8 channels at each step are independent. |
| Data parallelism width | 8 channels | Perfect for 8-wide AVX (float32) or 2x 4-wide SSE |
| Branch density | LOW | No conditionals in inner loop (freeze handled by coefficient choice) |
| Dominant operations | Arithmetic (add, mul) + filter | Filter bank, matrix multiply, gain application |

**Verdict: BENEFICIAL** -- 8 independent channels at each sample step maps directly to SIMD lanes.

### Highway API patterns (from existing spectral_simd.cpp):
- `hn::ScalableTag<float> d` for type/width declaration
- `hn::Load(d, ptr)` / `hn::Store(vec, d, ptr)` for memory access
- `hn::Mul(a, b)`, `hn::Add(a, b)`, `hn::Sub(a, b)` for arithmetic
- `hn::MulAdd(a, b, c)` for fused multiply-add (a*b + c)
- `hn::ReduceSum(d, vec)` for horizontal sum (needed for Householder)
- `hn::Set(d, scalar)` for broadcast
- Self-inclusion pattern: `foreach_target.h` + `HWY_BEFORE_NAMESPACE()` + `HWY_EXPORT` / `HWY_DYNAMIC_DISPATCH`

### Note on 8-channel with 4-wide SIMD
SSE has 4 float lanes. For 8 channels, process as 2 consecutive 4-wide operations. On AVX (8 lanes), it maps directly. Highway's `ScalableTag` handles this automatically -- if lanes >= 8, one operation; if 4, two operations with a loop.

**Alternatives considered**:
- Manual SSE intrinsics: Not portable (no ARM/NEON), Highway handles dispatch.
- No SIMD: Viable for the Householder (simple scalar math) but filter bank benefits significantly.
- Process-per-sample scalar: Works as Phase 1 implementation.

## R8: FDN Reverb Parameter Mapping

**Decision**: Map `ReverbParams` to FDN-specific internal parameters

**Rationale**: The FDN reverb must accept the same `ReverbParams` as the Dattorro reverb but has different internal semantics:

| ReverbParams field | Dattorro mapping | FDN mapping |
|-------------------|-----------------|-------------|
| roomSize (0-1) | decay coefficient 0.75-0.9995 | feedback gain 0.75-0.9995 (same range works) |
| damping (0-1) | LP cutoff 200Hz-20kHz | LP cutoff per channel (same mapping) |
| width (0-1) | mid/side stereo | channel decorrelation + stereo spread |
| mix (0-1) | dry/wet blend | dry/wet blend (same) |
| preDelayMs (0-100) | pre-delay line | pre-delay line (reuse same approach) |
| diffusion (0-1) | input allpass coefficients | diffuser step count or coefficient |
| freeze | decay=1, input=0 | feedback=1, input=0 (same principle) |
| modRate (0-2Hz) | LFO rate for tank allpass | LFO rate for modulated delays |
| modDepth (0-1) | LFO excursion in samples | LFO excursion scaled to delay lengths |

**Key insight**: The mapping ranges are largely similar. The FDN will produce a different sonic character not from different parameter ranges but from its fundamentally different architecture (parallel delays vs figure-eight topology).

## R9: Crossfade Switching Between Reverb Types

**Decision**: Reuse the delay crossfade pattern from `RuinaeEffectsChain` adapted for reverb

**Rationale**: The existing delay type switching uses:
- Linear crossfade with per-sample alpha increment
- Pre-warm phase to fill incoming delay buffer
- `crossfadeIncrement()` from `crossfade_utils.h`
- 30ms crossfade duration (within spec range of 25-50ms)

For reverb switching, the approach is slightly different:
- No pre-warm needed (reverb builds up from input, not from delay buffer)
- Outgoing reverb continues processing during crossfade (its tail fades out)
- Equal-power crossfade (`equalPowerGains()`) per spec FR-025
- On completion, outgoing reverb is `reset()` and goes idle
- Duration: 30ms (matching existing delay crossfade)

**Implementation**: The effects chain adds crossfade state for reverb (separate from delay crossfade):
- `reverbCrossfading_`, `reverbCrossfadeAlpha_`, `reverbCrossfadeIncrement_`
- Two `float*` temp buffers already exist (`crossfadeOutL_`, `crossfadeOutR_`) -- can be reused since delay and reverb crossfades don't overlap (they are sequential in the chain)

**Alternatives considered**:
- Linear crossfade: Simpler but can cause a perceived volume dip at midpoint.
- Instant switch with fade-out tail: Harsh transition of the incoming reverb.

## R10: FTZ/DAZ and Denormal Removal

**Decision**: Remove explicit `flushDenormal()` calls from Dattorro reverb, rely on FTZ/DAZ mode

**Rationale**: FR-005 specifies removing redundant denormal flushing. The VST3 framework sets FTZ/DAZ mode at process entry on x86. The Dattorro reverb currently calls `detail::flushDenormal()` on `tankAOut_` and `tankBOut_` every sample.

**Risk mitigation**:
- FTZ/DAZ is reliably set on x86 (SSE control register)
- On ARM (Apple Silicon), denormals may still be fast (no 100x penalty)
- The FDN reverb can start without explicit denormal flushing since it's new code
- If any platform shows denormal issues, add targeted flushing back

**Alternatives considered**:
- Keep all flushDenormal calls: Safe but unnecessary CPU overhead.
- Add global denormal guard class: Over-engineered for this case.
