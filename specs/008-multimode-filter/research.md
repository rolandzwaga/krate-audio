# Research: Multimode Filter

**Feature**: 008-multimode-filter
**Created**: 2025-12-23
**Status**: Complete

This document captures research decisions for the Multimode Filter implementation.

---

## Decision 1: Slope Implementation Strategy

**Question**: How to implement 12/24/36/48 dB/oct slopes efficiently?

**Decision**: Use existing `BiquadCascade<N>` template with variant/union pattern.

**Rationale**:
- `BiquadCascade<1>` = 12 dB/oct (same as single `Biquad`)
- `BiquadCascade<2>` = 24 dB/oct
- `BiquadCascade<3>` = 36 dB/oct
- `BiquadCascade<4>` = 48 dB/oct
- Existing `butterworthQ()` function calculates proper Q for each stage
- Pre-allocate all 4 stages; only process active stages based on slope setting

**Alternatives Considered**:
1. Dynamic array of biquads - Rejected: Memory allocation, cache unfriendly
2. Single biquad with higher-order coefficients - Rejected: Numerical instability
3. State Variable Filter - Rejected: Would require new Layer 1 primitive

**Implementation Note**: Store all 4 stages as `std::array<Biquad, 4>` (not `BiquadCascade<N>` template) with conditional processing based on slope. This wastes ~200 bytes of state but avoids any runtime allocation and allows runtime slope changes without template instantiation complexity.

---

## Decision 2: Type Switching Strategy

**Question**: How to switch filter types (LP→HP, etc.) without clicks?

**Decision**: Use `SmoothedBiquad` for coefficient interpolation + optional state reset.

**Rationale**:
- `SmoothedBiquad` already handles coefficient interpolation (5-10ms default)
- For radical type changes (LP→HP), interpolating coefficients creates smooth transition
- If artifacts occur, caller can trigger `reset()` before type change (documented behavior)

**Alternatives Considered**:
1. Dual-filter crossfade - Rejected: Doubles CPU cost, complex state management
2. Fade to silence then switch - Rejected: Audible gap in audio
3. Immediate switch with click - Rejected: Unacceptable artifacts

**Implementation Note**: Default behavior uses coefficient smoothing only. Document that for problematic transitions (rare), caller should fade input to silence, switch type, then fade back in.

---

## Decision 3: Drive Saturation Implementation

**Question**: How to implement pre-filter drive with proper aliasing prevention?

**Decision**: Use `Oversampler2xMono` with `tanh()` waveshaper.

**Rationale**:
- Drive is applied BEFORE filter (pre-filter saturation)
- `tanh()` provides smooth saturation curve with even harmonics
- 2x oversampling is sufficient for tanh (gentle harmonic generation)
- Economy mode (IIR, zero latency) suitable for real-time use
- Existing `Oversampler` primitive handles upsample/downsample

**Alternatives Considered**:
1. No oversampling - Rejected: Aliasing artifacts above 50% drive
2. 4x oversampling - Rejected: Overkill for gentle tanh curve, wastes CPU
3. Polynomial approximation - Rejected: Still needs oversampling, less accurate

**Implementation Note**: When drive = 0dB (unity), bypass oversampling entirely for efficiency.

---

## Decision 4: Self-Oscillation Behavior

**Question**: How does biquad-based filter behave at very high Q for self-oscillation?

**Decision**: Allow Q up to 100; document limitations compared to analog ladder filters.

**Research Findings**:
- Standard biquad with Q > 50 becomes unstable (poles approach unit circle)
- At Q ≈ 100, the filter rings significantly but doesn't produce clean sine like analog
- True "self-oscillation" requires Q → ∞ (poles ON unit circle)
- Digital biquads can achieve "ringing" effect but not true analog-style oscillation

**Behavior at High Q**:
- Q = 10: Strong resonance, no oscillation
- Q = 50: Very strong resonance, ringing on impulse
- Q = 100: Barely stable, extended ringing, pitch trackable

**Implementation Note**: Spec SC-005 requires "sine wave at cutoff frequency within 1 semitone accuracy" - achievable with Q ≈ 80-100 when excited by input. Pure silence input may not self-oscillate reliably. Document as P3 feature with caveats.

---

## Decision 5: Shelf/Peak Single-Stage Behavior

**Question**: Why are Shelf and Peak types fixed at single-stage?

**Decision**: Cascading shelf/peak filters produces different effects than "steeper slope."

**Rationale**:
- **LowShelf/HighShelf**: Cascading N stages applies gain N times
  - Single 6dB shelf = 6dB boost
  - Two cascaded 6dB shelves = 12dB boost (NOT steeper transition)
- **Peak (Parametric EQ)**: Cascading narrows bandwidth (higher effective Q)
  - Not equivalent to "steeper slope" concept
- This differs fundamentally from LP/HP/BP/Notch where cascading increases rolloff steepness

**User Expectation**: "48 dB/oct shelf" doesn't make sense - users expect shelves to have controllable gain, not slope.

**Implementation Note**: Slope parameter is ignored for Allpass/Shelf/Peak types. These always use single `Biquad` stage.

---

## Decision 6: Cutoff Modulation Smoothing

**Question**: What smoothing strategy for cutoff frequency modulation?

**Decision**: Use `OnePoleSmoother` for cutoff, with configurable time (default 5ms).

**Rationale**:
- `SmoothedBiquad` smooths coefficients, but we need to smooth the *input parameter*
- Cutoff can change every sample (LFO modulation) - coefficient recalculation is expensive
- Strategy: Smooth cutoff parameter → recalculate coefficients per-block (not per-sample)
- For sample-accurate modulation, use `processSample()` which recalculates per sample

**Performance Tradeoff**:
- `process(buffer)`: Recalculates coefficients once per block, uses smoothed cutoff
- `processSample()`: Recalculates coefficients per sample (expensive but accurate)

**Implementation Note**: Default to block processing with smoothed parameters. Provide `processSample()` for advanced users who need sample-accurate modulation.

---

## Component Reuse Summary

| Component | Location | Usage in MultimodeFilter |
|-----------|----------|--------------------------|
| `FilterType` enum | biquad.h | Reuse directly |
| `Biquad` | biquad.h | Single stage (12dB), Shelf/Peak |
| `BiquadCascade<2-4>` | biquad.h | Multi-stage slopes (24-48dB) |
| `SmoothedBiquad` | biquad.h | Coefficient interpolation |
| `BiquadCoefficients` | biquad.h | Coefficient calculation |
| `butterworthQ()` | biquad.h | Cascade Q calculation |
| `OnePoleSmoother` | smoother.h | Parameter smoothing |
| `Oversampler2xMono` | oversampler.h | Drive saturation |
| `dbToGain()` | db_utils.h | Drive parameter conversion |

---

## Risk Assessment

| Risk | Likelihood | Impact | Mitigation |
|------|------------|--------|------------|
| Type switching clicks | Medium | Medium | SmoothedBiquad + documentation |
| Self-oscillation not clean enough | Medium | Low | P3 feature, document limitations |
| Performance with sample-by-sample processing | Low | Medium | Document as "advanced" API |
| Coefficient instability at Nyquist | Low | High | Clamp cutoff to 0.49 * sampleRate |

---

## Open Questions (None)

All technical questions resolved. Ready for Phase 1 design artifacts.
