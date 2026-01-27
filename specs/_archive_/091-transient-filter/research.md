# Research: TransientAwareFilter

**Feature Branch**: `091-transient-filter`
**Date**: 2026-01-24
**Status**: Complete

## Research Summary

All clarifications from the specification have been resolved. This document captures the technical decisions made during spec clarification.

## Resolved Clarifications

### 1. Sensitivity Control Behavior

**Question**: Should sensitivity scale both threshold and envelope times, or control threshold only with fixed internal envelope constants?

**Decision**: Sensitivity controls threshold only; envelope times are fixed internal constants.

**Rationale**:
- Simpler user mental model (one parameter = one behavior)
- Fixed envelope times (1ms fast, 50ms slow) are well-established in transient detection literature
- Avoids coupling between sensitivity and timing behavior
- Allows predictable transient detection regardless of sensitivity setting

**Alternatives Considered**:
- Scaling both threshold and times: Rejected because it couples parameters unpredictably
- User-configurable envelope times: Rejected as "advanced feature" that adds complexity without clear benefit

### 2. Transient Detection Calculation

**Question**: How should the fast/slow envelope comparison and sensitivity threshold be computed?

**Decision**: Absolute difference with linear threshold: `transient = max(0, fastEnv - slowEnv) > (1.0 - sensitivity)`

**Rationale**:
- `max(0, ...)` ensures only positive differences (attacks, not decays) trigger response
- Linear threshold is intuitive: sensitivity=0 means no detection, sensitivity=1 means detect everything
- Simple to implement and test

**Alternatives Considered**:
- Ratio-based detection (`fastEnv / slowEnv`): More complex, potential division by zero issues
- Squared difference: Adds computation without clear benefit

### 3. Envelope Follower Release Time

**Question**: Should the fast/slow envelope followers use symmetric (release = attack) or asymmetric release times?

**Decision**: Release equals attack (1ms/50ms) - symmetric envelope behavior.

**Rationale**:
- Symmetric envelopes are standard in transient detection
- Asymmetric times would favor either attack or decay detection, but we specifically want attack detection
- Simplifies implementation (one time constant per envelope)
- Matches the EnvelopeFollower API which already supports symmetric configuration

**Alternatives Considered**:
- Asymmetric with longer release: Would blur transient detection over time
- Asymmetric with shorter release: Would create false transients on signal decay

### 4. Transient Detection Normalization

**Question**: How should the raw envelope difference be normalized to [0.0, 1.0] range before threshold comparison?

**Decision**: Divide by slow envelope: `normalized = diff / max(slowEnv, epsilon)` (relative to baseline, level-independent).

**Rationale**:
- **Level-independent detection**: A transient at -20dB triggers the same as one at 0dB
- Division by slow envelope normalizes relative to the current signal level
- Epsilon (1e-6) prevents division by zero during silence
- This is the standard approach in professional transient detection algorithms

**Alternatives Considered**:
- Fixed normalization (divide by constant): Level-dependent, would miss quiet transients
- Peak normalization: Requires lookahead buffer, adds latency
- No normalization: Would require constant threshold adjustment for different input levels

### 5. Response Curve Shape

**Question**: What curve shape should be used for the user-configurable attack/decay response to detected transients?

**Decision**: Exponential (one-pole smoother) - natural decay, reuses OnePoleSmoother.

**Rationale**:
- Exponential curves sound natural (physical systems decay exponentially)
- OnePoleSmoother already exists in the codebase (Layer 1)
- Simple to configure with attack/decay time constants
- No additional implementation required

**Alternatives Considered**:
- Linear ramp: Sounds artificial, unnatural envelope shape
- Bezier/spline: Over-engineered for this use case
- Asymmetric attack/decay: Already supported by separate attack/decay smoothers

## Technical Decisions

### Dual EnvelopeFollower Configuration

| Envelope | Attack (ms) | Release (ms) | Purpose |
|----------|-------------|--------------|---------|
| Fast | 1.0 | 1.0 | Track rapid level changes (transients) |
| Slow | 50.0 | 50.0 | Track average signal level (baseline) |

These values are internal constants (not user-configurable) per FR-005 and FR-006.

### Transient Detection Pipeline

```
Input Signal
    |
    v
+-------+    +-------+
| Fast  |    | Slow  |
| Env   |    | Env   |
| (1ms) |    | (50ms)|
+-------+    +-------+
    |            |
    v            v
+-------------------------+
| diff = max(0, fast-slow)|
+-------------------------+
            |
            v
+-------------------------+
| normalized = diff/slow  |
+-------------------------+
            |
            v
+---------------------------+
| threshold = 1-sensitivity |
| transient = norm>thresh   |
+---------------------------+
            |
            v
+------------------------+
| OnePoleSmoother        |
| (attack/decay times)   |
+------------------------+
            |
            v
    Transient Level [0,1]
```

### Frequency Mapping

Log-space interpolation (exponential mapping) for perceptually linear sweeps:

```cpp
float logIdle = std::log(idleCutoff_);
float logTransient = std::log(transientCutoff_);
float logCutoff = logIdle + transientLevel * (logTransient - logIdle);
float cutoff = std::exp(logCutoff);
```

This ensures equal perceptual change per unit of transient level.

### Resonance Modulation

Linear interpolation with clamping:

```cpp
float totalQ = idleResonance_ + transientLevel * transientQBoost_;
totalQ = std::clamp(totalQ, kMinResonance, kMaxTotalResonance);  // 0.5 to 30.0
```

## Best Practices Research

### Transient Detection in Audio DSP

**Industry Patterns**:
1. Dual envelope comparison is standard (Transient Designer, SPL Transient Designer)
2. Fast/slow ratio of ~50:1 is typical (1ms:50ms)
3. Level-independent normalization is essential for consistent behavior

**SVF for Dynamic Filtering**:
- TPT SVF topology is ideal for modulated filters (no clicks)
- Cytomic reference implementation is used in the codebase
- Mode mixing coefficients allow LP/BP/HP selection

### Real-Time Safety

**Verified Patterns**:
- EnvelopeFollower: noexcept, no allocations
- SVF: noexcept, no allocations
- OnePoleSmoother: noexcept, no allocations

All composed components are already real-time safe.

## Dependency Compatibility

| Component | Version | Compatibility |
|-----------|---------|---------------|
| EnvelopeFollower | Current | Full API verified |
| SVF | Current | Full API verified |
| OnePoleSmoother | Current | Full API verified |

No compatibility issues identified. All APIs are stable and documented in the architecture.

## Open Questions (None)

All clarifications resolved. Ready for Phase 1 design.
