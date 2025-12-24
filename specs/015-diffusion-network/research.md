# Research: Diffusion Network

**Feature**: 015-diffusion-network
**Date**: 2025-12-24

## Research Summary

All technical decisions have been made with no outstanding clarifications needed.

## Decisions Made

### 1. Allpass Diffusion Algorithm

**Decision**: Schroeder-style first-order allpass

**Formula**:
```
y[n] = -g * x[n] + x[n-D] + g * y[n-D]
```

**Parameters**:
- `g` = 0.618 (golden ratio inverse, 1/φ)
- `D` = delay time in samples (varies per stage)

**Rationale**: Standard proven structure from Schroeder's 1961 reverb design. The golden ratio inverse provides good diffusion without excessive "ringing".

**Alternatives Rejected**:
- Nested allpass (Gerzon): Too complex for our use case
- FDN (Feedback Delay Network): Overkill for simple diffusion
- Lattice allpass: Harder to modulate smoothly

### 2. Delay Time Ratios

**Decision**: Mutually irrational ratios based on square roots and golden ratio

| Stage | Ratio | Base (ms) | Derivation |
|-------|-------|-----------|------------|
| 1 | 1.000 | 3.2 | Base unit |
| 2 | 1.127 | 3.6 | √(1.27) |
| 3 | 1.414 | 4.5 | √2 |
| 4 | 1.732 | 5.5 | √3 |
| 5 | 2.236 | 7.2 | √5 |
| 6 | 2.828 | 9.0 | √8 (2√2) |
| 7 | 3.317 | 10.6 | √11 |
| 8 | 4.123 | 13.2 | √17 |

**Total delay at size=100%**: ~57ms

**Rationale**: Irrational ratios prevent comb filtering. Square roots of primes are mutually irrational.

### 3. Stereo Decorrelation

**Decision**: Multiply right channel ratios by 1.127 (√1.27)

**Effect**: ~12-15% time offset between L/R per stage

**Rationale**: Simple, consistent offset. Avoids phase correlation without introducing comb filtering between channels.

### 4. Modulation Strategy

**Decision**: Single LFO with per-stage phase offset

- **LFO shape**: Sine (smooth, no artifacts)
- **Rate range**: 0.1Hz - 5Hz
- **Depth range**: 0 - 2ms per stage
- **Phase offset**: `stage_index * (2π/8)` = 45° per stage

**Rationale**: Phase offsets ensure asynchronous modulation. Single LFO saves CPU. Sine avoids modulation artifacts.

### 5. Density Implementation

**Decision**: Smooth crossfade using per-stage enable smoother

| Density | Active Stages |
|---------|---------------|
| 0-12% | 1 only |
| 12-25% | 1-2 |
| 25-37% | 1-3 |
| 37-50% | 1-4 |
| 50-62% | 1-5 |
| 62-75% | 1-6 |
| 75-87% | 1-7 |
| 87-100% | 1-8 |

**Crossfade**: Each stage has its own OnePoleSmoother (0→1) for enable state.

### 6. Width Implementation

**Decision**: Blend between decorrelated and mono output

- **width=0%**: L_out = R_out = (L_diffused + R_diffused) / 2
- **width=100%**: L_out = L_diffused, R_out = R_diffused
- **Intermediate**: Linear blend

## Existing Components to Reuse

| Component | Location | Purpose |
|-----------|----------|---------|
| DelayLine | dsp/primitives/delay_line.h | Delay buffer per stage |
| LFO | dsp/primitives/lfo.h | Modulation source |
| OnePoleSmoother | dsp/primitives/smoother.h | Parameter smoothing |

## Memory Requirements

- 8 stages × 2 channels = 16 DelayLine instances
- Max delay per stage: ~15ms at size=100%
- At 192kHz: 15ms = 2880 samples per delay line
- Power-of-2 buffer: 4096 samples each
- Total: 16 × 4096 × 4 bytes = 256KB max

## CPU Considerations

- 8 stages × 2 channels = 16 allpass operations per sample
- Each operation: 1 multiply-add, 1 delay read (interpolated), 1 delay write
- LFO: 1 sine lookup per sample (shared)
- Parameter smoothing: 5 smoothers (size, density, width, mod depth, mod rate)

Estimated: < 0.5% CPU at 44.1kHz stereo (within Layer 2 budget)
