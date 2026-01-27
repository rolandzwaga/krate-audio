# Research: Filter Feedback Matrix

**Feature**: 096-filter-feedback-matrix
**Date**: 2026-01-24

## Executive Summary

This document consolidates research findings for implementing a Filter Feedback Matrix - a network of SVF filters with configurable cross-feedback routing. The design draws heavily from Feedback Delay Network (FDN) theory while adapting it for filter-based rather than delay-based resonance.

## Research Topics

### 1. Feedback Delay Network (FDN) Matrix Design

**Question**: What are the best practices for feedback matrix design in audio DSP?

**Findings**:

From [Feedback Delay Networks (FDN) - Stanford CCRMA](https://ccrma.stanford.edu/~jos/pasp/Feedback_Delay_Networks_FDN.html) and [FDNTB: The Feedback Delay Network Toolbox](https://www.researchgate.net/publication/344467473_FDNTB_The_Feedback_Delay_Network_Toolbox):

1. **Matrix Stability**: An FDN is lossless (stable without decay) if and only if the feedback matrix has unit-modulus eigenvalues. For practical use, the matrix should be **unitary** or **orthogonal** to preserve energy.

2. **Efficient Matrix Computation**: Hadamard matrices are computationally efficient (O(N log N) instead of O(N^2)) and provide good signal distribution. However, for N=4 filters, the full matrix multiplication is acceptable.

3. **Cross-Coupling Benefits**: Individual feedback comb filters yield poor quality, but cross-coupling creates "maximally rich" interaction. This is directly applicable to our filter matrix.

**Decision**: Use a general NxN feedback matrix (not constrained to orthogonal) since we want controllable resonance, not lossless operation. The per-filter soft clipping provides stability instead of matrix constraints.

**Rationale**: Orthogonal matrices are used in reverb to prevent coloration and maintain energy. For a filter matrix effect, we want controlled resonance and coloration, so a general matrix with stability limiting is more appropriate.

**Alternatives Considered**:
- Hadamard matrix only: Rejected - too restrictive for creative control
- Householder reflections: Rejected - dominates local feedback as N increases

---

### 2. Stability Limiting Strategies

**Question**: How should we prevent runaway oscillation in high-feedback systems?

**Findings**:

From [Limiting filter resonance - KVR Audio](https://www.kvraudio.com/forum/viewtopic.php?t=410794) and [Location of tanh(x) in a filter - KVR Audio](https://www.kvraudio.com/forum/viewtopic.php?t=458719):

1. **Tanh Soft Clipping**: Standard approach for analog-style limiting. Computationally expensive but provides smooth saturation. `y = tanh(x)` approximates OTA behavior.

2. **Placement Options**:
   - Per-filter output (before feedback routing) - Spec decision
   - In feedback path only
   - Per-stage within filter

3. **Polynomial Approximation**: For CPU efficiency, `y = 1.5*x - 0.5*x^3` (after hard clipping to [-1, 1]) is a reasonable approximation.

4. **Limiting in Feedback Loops**: Adding a limiter in a filter bank feedback loop is "useful for stability" according to practical experience.

**Decision**: Use `std::tanh()` on each filter's output before feedback routing. This matches the spec requirement (FR-011) and provides consistent limiting regardless of feedback topology.

**Rationale**: Per-filter limiting before routing ensures each filter's contribution is bounded, preventing any single filter from driving the entire network into oscillation. The tanh function provides smooth saturation that sounds musical.

**Alternatives Considered**:
- Hard clipping: Rejected - creates harsh artifacts
- Limiting only on total feedback: Rejected - doesn't prevent individual filter runaway
- Polynomial approximation: Possible optimization if profiling shows tanh is a bottleneck

---

### 3. DC Blocking in Feedback Networks

**Question**: Where should DC blocking be applied in the feedback network?

**Findings**:

From existing codebase analysis (FeedbackNetwork, FlexibleFeedbackNetwork) and [research.md patterns]:

1. **Existing Pattern**: Both FeedbackNetwork and FlexibleFeedbackNetwork use DCBlocker after the delay line in the feedback path.

2. **DC Accumulation Risk**: In feedback networks, even small DC offsets can accumulate over iterations, causing signal drift and potentially clipping.

3. **Cutoff Frequency**: 10Hz is the standard cutoff (default in DCBlocker) for feedback path DC blocking.

**Decision**: Place DCBlocker after each delay line in the feedback path (per FR-020). This follows the established pattern in the codebase.

**Rationale**: DC blocking after the delay line catches any DC that accumulates through the filter/delay chain before it gets scaled by feedback amount. This is the most effective placement.

**Alternatives Considered**:
- DC blocking per filter output: More CPU, less effective
- Single DC blocker on summed feedback: Doesn't catch per-path accumulation
- No DC blocking: Risk of offset accumulation

---

### 4. Delay Line Interpolation for Feedback Paths

**Question**: What interpolation method is best for feedback path delays?

**Findings**:

From spec clarifications and existing DelayLine implementation:

1. **Linear Interpolation**: Good balance of quality and CPU. Suitable for modulated delays.

2. **Allpass Interpolation**: Better phase response for fixed delays, but has state that creates artifacts when delay time is modulated.

3. **Existing Implementation**: DelayLine::readLinear() is already implemented and tested.

**Decision**: Use linear interpolation (DelayLine::readLinear()) as specified in FR-019.

**Rationale**: Feedback path delays may be modulated by the user, so linear interpolation's stability during modulation is valuable. The quality is sufficient for feedback effects where the delay is relatively short (0-100ms).

**Alternatives Considered**:
- Allpass: Rejected - artifacts when delay time changes
- Cubic: More CPU for marginal quality improvement
- No interpolation (integer only): Rejected - limits delay time resolution

---

### 5. Template Parameter vs Runtime Active Count

**Question**: How should filter count be configured?

**Findings**:

From spec clarification and C++ best practices:

1. **Template Parameter for Capacity**: `FilterFeedbackMatrix<4>` allocates fixed arrays at compile time. No runtime allocation needed.

2. **Runtime Active Count**: `setActiveFilters(2)` allows processing only 2 of 4 filters, saving CPU when fewer filters are needed.

3. **Precedent**: std::array uses this pattern - compile-time size, runtime may use less.

**Decision**: Template parameter `N` for maximum filters (2-4), with runtime `setActiveFilters(count)` to set how many are actually processed.

**Rationale**: This gives the best of both worlds - compile-time memory layout for cache efficiency, runtime flexibility for CPU optimization. The loop in process() simply iterates over `activeFilters_` instead of `N`.

**Alternatives Considered**:
- Runtime-only (std::vector): Rejected - allocations, cache-unfriendly
- Compile-time only: Rejected - no CPU savings for simple configurations
- Multiple template specializations: Rejected - code duplication

---

### 6. Stereo Architecture

**Question**: How should stereo processing be structured?

**Findings**:

From spec clarification (dual-mono):

1. **Dual-Mono**: Two completely independent filter networks, one per channel. No cross-channel feedback.

2. **Implementation**: Template the FilterFeedbackMatrix for mono, use two instances for stereo.

3. **Alternatives in the wild**: True stereo (cross-channel feedback) is common in reverbs but not specified here.

**Decision**: Implement mono `process(float input)` and stereo `processStereo(float& left, float& right)` where stereo uses two internal networks.

**Rationale**: Dual-mono is simpler to implement and reason about. It also matches the spec requirement (FR-013). Cross-channel feedback can be added later if needed.

**Alternatives Considered**:
- True stereo with cross-channel: Not specified, more complex
- Single mono class with external duplication: Less convenient API

---

## Summary of Key Decisions

| Topic | Decision | Rationale |
|-------|----------|-----------|
| Matrix Type | General NxN (not constrained) | Creative control over resonance |
| Stability | Per-filter tanh soft clipping | Musical limiting, prevents runaway |
| DC Blocking | Per-path after delay | Follows codebase pattern, effective |
| Interpolation | Linear (readLinear) | Good quality, no modulation artifacts |
| Filter Count | Template + runtime active | Best performance/flexibility balance |
| Stereo | Dual-mono (independent networks) | Matches spec, simpler implementation |

## Sources

- [Feedback Delay Networks (FDN) - Stanford CCRMA](https://ccrma.stanford.edu/~jos/pasp/Feedback_Delay_Networks_FDN.html)
- [FDN Reverberation - DSPRelated](https://www.dsprelated.com/freebooks/pasp/FDN_Reverberation.html)
- [FDNTB: The Feedback Delay Network Toolbox](https://www.researchgate.net/publication/344467473_FDNTB_The_Feedback_Delay_Network_Toolbox)
- [Efficient Optimization of Feedback Delay Networks - arXiv](https://arxiv.org/html/2402.11216v2)
- [Optimizing tiny colorless feedback delay networks - Springer](https://link.springer.com/article/10.1186/s13636-025-00401-w)
- [Limiting filter resonance - KVR Audio](https://www.kvraudio.com/forum/viewtopic.php?t=410794)
- [Location of tanh(x) in a filter - KVR Audio](https://www.kvraudio.com/forum/viewtopic.php?t=458719)
- [Resonant filter - musicdsp.org](https://www.musicdsp.org/en/latest/Filters/29-resonant-filter.html)
- Existing codebase: FeedbackNetwork, FlexibleFeedbackNetwork patterns
