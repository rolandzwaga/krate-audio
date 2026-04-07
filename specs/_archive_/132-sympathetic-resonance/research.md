# Research: Sympathetic Resonance

**Feature Branch**: `132-sympathetic-resonance`
**Date**: 2026-03-25

## Research Topics

### R-001: Second-Order Driven Resonator Recurrence Relation

**Decision**: Use the recurrence `y[n] = 2r*cos(omega)*y[n-1] - r^2*y[n-2] + x[n]` where
`r = exp(-pi * delta_f / sampleRate)`, `omega = 2*pi*f/sampleRate`, and `delta_f = f / Q`.

**Rationale**: This is a standard driven second-order resonator (biquad in direct form II transposed, with b0=1, b1=0, b2=0, a1=-2r*cos(omega), a2=r^2). It is:
- Computationally minimal: 2 multiplies, 2 adds per sample per resonator (plus the input)
- Numerically stable for the Q range 100-1000 at sample rates up to 192 kHz
- SIMD-friendly: no cross-resonator dependencies, only a 2-sample time dependency within each resonator
- Equivalent to the coupled-form used in `ModalResonatorBankSIMD` but optimized for driven (not free) oscillation

**Alternatives considered**:
- Biquad (TDF2): More coefficients (5 instead of 3), same computation, no benefit for pure resonance
- State-variable filter: Better for real-time modulation of frequency, but overkill for resonators that change infrequently (only on noteOn/noteOff)
- Coupled-form (Gordon-Smith): Better for free oscillation (amplitude-stable without radius), but the driven case already has natural damping from r < 1

### R-002: Google Highway SIMD Pattern for Parallel Resonators

**Decision**: Follow the existing `modal_resonator_bank_simd.cpp` pattern using Highway's `foreach_target.h` / `HWY_EXPORT` / `HWY_DYNAMIC_DISPATCH` mechanism.

**Rationale**: The project already has an established Highway SIMD pattern:
1. A `.cpp` file using the `#undef HWY_TARGET_INCLUDE` / `#include "hwy/foreach_target.h"` self-inclusion pattern
2. Kernels defined in `HWY_NAMESPACE` that are compiled once per ISA target
3. `HWY_EXPORT` + `HWY_DYNAMIC_DISPATCH` for runtime ISA selection
4. A clean `.h` public API with no Highway headers exposed
5. Linked PRIVATE to KrateDSP

The sympathetic resonator SIMD kernel will follow this exact pattern. The key difference from `ModalResonatorBankSIMD` is that we process driven resonators (input + 2 feedback taps) rather than free oscillation (coupled-form advance only).

**Vectorization strategy**: Process N resonators in parallel per SIMD lane (4 SSE / 8 AVX2), not across time. Each resonator has a 2-sample time dependency (y[n-1], y[n-2]) that prevents time-axis vectorization. Cross-resonator independence enables full lane utilization.

**Alternatives considered**:
- Raw SSE/AVX intrinsics: Less portable, harder to maintain, no ARM/NEON support
- `xsimd` library: Not already in the project; Highway already integrated and tested
- No SIMD (scalar only): Constitution Principle IV requires evaluation; the resonator count (32-64) provides excellent parallelism width

### R-003: Resonator Pool Management (Add/Merge/Evict/Reclaim)

**Decision**: Use a fixed-capacity `std::array<ResonatorState, kMaxSympatheticResonators>` with an active count, unsorted; linear scan is cache-friendly for 64 elements. Merge/evict/reclaim operations manipulate this array without dynamic allocation.

**Rationale**:
- Fixed-size array satisfies Constitution Principle II (no allocation on audio thread)
- Pool operations (add, merge, evict, reclaim) happen on noteOn/noteOff events (not per-sample), so O(n) array operations on max 64 elements are acceptable
- Each resonator stores: frequency, r, omega, y[n-1], y[n-2], voiceId, partialNumber, gain, envelopeLevel
- Merge threshold of ~0.3 Hz preserves near-unison beating while avoiding redundant resonators for the same partial across voices playing the same note

**Alternatives considered**:
- `std::vector` with reserve: Constitution Principle II forbids `push_back` on audio thread
- Linked list: Poor cache locality, allocation issues
- Hash map by frequency: Over-engineered for 64 elements; linear scan is cache-friendly

### R-004: Anti-Mud Filter Design

**Decision**: Two-mechanism system:
1. Output HPF: `gain(f) = 1 / (1 + (f_ref / f)^2)` where f_ref ~= 100 Hz, implemented as a simple one-pole highpass (6 dB/oct rolloff)
2. Frequency-dependent Q: `Q_eff = Q_user * clamp(f_ref / f, 0.5, 1.0)` where f_ref ~= 500 Hz, applied per-resonator at coefficient computation time

**Rationale**:
- The output HPF prevents sub-bass buildup from low-frequency resonators; the damping curve provides a smooth rolloff that preserves musical bass content while removing rumble
- Frequency-dependent Q models real soundboard absorption: high partials are absorbed faster, producing a natural decay profile. The 0.5x minimum clamp prevents total suppression of shimmer
- Using the existing `Biquad` primitive for the output HPF integrates cleanly with the Layer 1 architecture

**Alternatives considered**:
- Higher-order HPF (12 dB/oct, 24 dB/oct): Steeper cutoff but more artifacts and phase distortion in the pass-band; 6 dB/oct is musically transparent
- Per-resonator output HPF: Too many filter instances (64x), much more expensive than the damping-curve approach
- Fixed Q for all resonators: Produces muddy low-end buildup on dense chords -- the primary failure mode cited in literature

### R-005: Parameter Smoothing Strategy

**Decision**: Reuse `Krate::DSP::OnePoleSmoother` for the coupling amount parameter, with 5ms default smoothing time.

**Rationale**: The project already uses `OnePoleSmoother` extensively throughout the Innexus processor for parameter smoothing. The coupling amount controls the input gain to the resonator pool and must be smoothed to prevent clicks at the transition from 0.0 (bypassed) to non-zero values.

The Q parameter (Sympathetic Decay) does NOT need per-sample smoothing -- it controls resonator coefficients that are recomputed on noteOn events only. Changing Q during playback affects only newly added resonators; existing resonators keep their coefficients (which is physically correct: a real string's Q doesn't change mid-vibration).

**Alternatives considered**:
- `LinearRamp`: Constant-rate smoothing, but OnePoleSmoother's exponential approach is more natural for gain parameters
- Per-sample coefficient update: Overkill since Q changes are rare events

### R-006: Envelope Follower for Reclaim Threshold

**Decision**: Simple peak follower with fast attack (instant) and slow release, updated every sample:
```
env = max(abs(y[n]), env * release_coeff)
```
where `release_coeff = exp(-1 / (tau * sampleRate))` with tau ~= 10ms.

**Rationale**:
- Per-resonator amplitude tracking enables immediate reclaim when amplitude drops below -96 dB
- The peak follower with instant attack / slow release prevents premature reclaim during zero-crossings
- Updated every sample (no block-boundary latency) for responsive reclaim
- -96 dB threshold = 1.585e-5 in linear scale

**Alternatives considered**:
- RMS follower: More computation per sample, slower response
- Block-rate check: Introduces up to 256-sample latency before reclaim, wastes pool capacity

### R-007: Inharmonicity Formula

**Decision**: Use `f_n = n * f0 * sqrt(1 + B * n^2)` where B is the inharmonicity coefficient from the voice system.

**Rationale**: This is the standard piano string inharmonicity formula. The Innexus voice system already computes inharmonicity-adjusted partial frequencies (the `inharmonicityAmount_` parameter). The sympathetic resonance receives pre-computed partial frequencies via the `noteOn(voiceId, partials)` API, so no additional inharmonicity computation is needed in the SympatheticResonance class itself -- the caller passes already-adjusted frequencies.

**Alternatives considered**:
- Recomputing inharmonicity inside SympatheticResonance: Duplicates logic, breaks separation of concerns
- Ignoring inharmonicity: Physically incorrect, partials wouldn't match actual voice output

### R-008: Constitution Principle IV - Scalar-First Workflow

**Decision**: Phase 1 implements scalar resonator processing with full test suite and CPU baseline. Phase 2 adds SIMD behind the same API, reusing Phase 1 tests as the correctness oracle.

**Rationale**: Constitution Principle IV mandates scalar-first. The scalar version also serves as:
1. A reference for validating SIMD correctness
2. A fallback for platforms without the target ISA
3. A debugging baseline for SIMD-specific issues

The SIMD implementation file (`sympathetic_resonance_simd.cpp`) follows the established Highway pattern and is added to `dsp/CMakeLists.txt` alongside existing SIMD files.
