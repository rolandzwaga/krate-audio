# Research: Band Management

**Feature**: 002-band-management
**Date**: 2026-01-28
**Status**: Complete

## Overview

This document consolidates research findings for the band management implementation. Most technical questions were pre-clarified during spec creation (see spec.md Clarifications section).

## Research Tasks Completed

### 1. CrossoverLR4 API Investigation

**Task**: Verify existing KrateDSP CrossoverLR4 meets requirements.

**Finding**: The `CrossoverLR4` class in `dsp/include/krate/dsp/processors/crossover_filter.h` provides:
- Phase-coherent LR4 (24dB/octave) crossover
- Thread-safe atomic parameter setters
- Built-in frequency smoothing (default 5ms)
- Sample-by-sample processing via `process(float input)` returning `CrossoverLR4Outputs{low, high}`
- Flat sum verified within 0.1dB tolerance (existing unit tests confirm)

**Decision**: Reuse CrossoverLR4 without modification. Wrap in CrossoverNetwork for N-band support.

**Rationale**: Constitution Principle XIV mandates reuse of existing components. CrossoverLR4 is well-tested and meets all crossover requirements.

### 2. Cascaded Crossover Topology

**Task**: Research optimal topology for N-band splitting.

**Finding**: Two common approaches:
1. **Parallel splitting**: All crossovers process input simultaneously
2. **Cascaded splitting**: Serial chain where each crossover splits remainder

**Decision**: Use cascaded topology (FR-012).

**Rationale**:
- Cascaded is simpler to implement with variable band count
- Each crossover naturally produces one band + remainder for next stage
- Existing Crossover3Way/Crossover4Way in KrateDSP use this topology
- Phase coherence preserved when bands summed

**Alternatives Considered**:
- Parallel topology: More complex band boundary management, no clear benefit
- Tree topology: Higher latency, more complex state management

### 3. Stereo Processing Approach

**Task**: Determine L/R channel processing strategy.

**Finding**: Three options:
1. Dual CrossoverNetwork instances (one per channel)
2. Single CrossoverNetwork with internal dual state
3. Mid-side processing

**Decision**: Dual internal state within single CrossoverNetwork (FR-001b).

**Rationale**:
- Simplest API (one prepare/reset call)
- Preserves stereo imaging for per-band pan controls
- No inter-channel dependency needed for multiband distortion use case

**Alternatives Considered**:
- Mid-side: Not beneficial for distortion application
- Separate instances: Works but more verbose API

### 4. Parameter Smoothing Strategy

**Task**: Research smoothing approach for solo/mute/bypass transitions.

**Finding**: OnePoleSmoother already exists with:
- Configurable time constant
- Thread-safe target setting
- Completion detection via `isComplete()`
- NaN/Inf protection

**Decision**: Use OnePoleSmoother with 10ms default for all band transitions (FR-027a).

**Rationale**:
- 10ms provides click-free transitions without noticeable lag
- Consistent with existing KrateDSP patterns
- Hidden parameter allows tuning without UI complexity

**Alternatives Considered**:
- LinearRamp: Less natural envelope for audio parameter changes
- SlewLimiter: Overkill for simple gain fades

### 5. Band Count Change Behavior

**Task**: Clarify crossover redistribution when band count changes.

**Finding**: Two schools of thought:
1. Recalculate all crossovers logarithmically
2. Preserve existing crossovers, insert new ones at midpoints

**Decision**: Preserve existing + insert at logarithmic midpoints (FR-011a).

**Rationale**:
- Preserves user's manual adjustments when adding bands
- More predictable behavior from user perspective
- Logarithmic midpoints maintain perceptually even spacing

**Algorithm**:
```
When increasing from N to N+M bands:
1. Keep existing N-1 crossover frequencies
2. For each gap between existing frequencies:
   - Calculate logarithmic midpoint: sqrt(f_low * f_high)
   - Insert new crossover at midpoint
3. Apply smoothing to new frequencies to prevent clicks
```

### 6. Equal-Power Pan Law Verification

**Task**: Verify pan law formula from spec.

**Finding**: FR-022 specifies:
```cpp
leftGain = cos(pan * PI/4 + PI/4)
rightGain = sin(pan * PI/4 + PI/4)
```

**Verification**:
- At pan = -1.0 (full left): leftGain = cos(0) = 1.0, rightGain = sin(0) = 0.0
- At pan = 0.0 (center): leftGain = cos(PI/4) = 0.707, rightGain = sin(PI/4) = 0.707
- At pan = +1.0 (full right): leftGain = cos(PI/2) = 0.0, rightGain = sin(PI/2) = 1.0

**Decision**: Use formula as specified.

**Rationale**: Standard equal-power pan law. Total power constant across pan range (cos^2 + sin^2 = 1).

### 7. Solo/Mute Interaction

**Task**: Clarify solo/mute precedence.

**Status**: Resolved in spec.md Clarifications section (2026-01-28).

**Summary**: Mute is independent and always suppresses output. When any solo is active, a band contributes if and only if (band.solo == true AND band.mute == false).

See [spec.md Clarifications](spec.md#clarifications) and FR-025a for the full logic table and implementation details in [plan.md](plan.md) (`shouldBandContribute()` helper).

## Unresolved Questions

None. All technical questions were clarified during spec creation or resolved through this research.

## References

- `dsp/include/krate/dsp/processors/crossover_filter.h` - CrossoverLR4 implementation
- `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother implementation
- `specs/Disrumpo/dsp-details.md` - BandState structure, parameter encoding
- `specs/Disrumpo/plan.md` - System architecture context
