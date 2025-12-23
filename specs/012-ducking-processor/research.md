# Research: Ducking Processor

**Feature**: 012-ducking-processor
**Date**: 2025-12-23
**Status**: Complete

## Overview

This document captures design decisions and rationale for the DuckingProcessor implementation. Research was minimal since ducking is a well-understood DSP pattern and the project already has reference implementations for similar processors.

## Design Decisions

### D1: Composition vs Modification of DynamicsProcessor

**Decision**: Create new DuckingProcessor class, compose EnvelopeFollower

**Rationale**:
- DynamicsProcessor uses internal signal for detection; ducking requires external sidechain
- Ducking uses fixed depth, not ratio-based compression curve
- Hold time feature is unique to ducking (not in DynamicsProcessor)
- Simpler API (no knee, no makeup gain, no lookahead)
- Cleaner to compose EnvelopeFollower directly than to fork/modify DynamicsProcessor

**Alternatives Considered**:
1. Extend DynamicsProcessor with external sidechain input → Rejected: API bloat, confusing dual-purpose class
2. Parameterize DynamicsProcessor with mode flag → Rejected: Violates single responsibility principle
3. Create DuckingProcessor from scratch → Selected: Clean slate with composition

### D2: Envelope Follower vs Direct Detection

**Decision**: Reuse EnvelopeFollower with Peak mode

**Rationale**:
- EnvelopeFollower already handles attack/release timing correctly
- Peak mode is standard for ducking (detect transients)
- Sidechain HPF already built into EnvelopeFollower
- No need to duplicate envelope detection code

**Alternatives Considered**:
1. Implement inline envelope detection → Rejected: Duplicates tested code
2. Use RMS mode → Rejected: Peak is more responsive for ducking triggers

### D3: Hold Time Implementation

**Decision**: Sample counter state machine in DuckingProcessor

**Rationale**:
- EnvelopeFollower doesn't have hold time (not part of its spec)
- Hold is specific to ducking use case, not general envelope detection
- Simple counter reset on re-trigger during hold period
- State machine has 3 states: IDLE, DUCKING, HOLDING

**Implementation**:
```cpp
enum class DuckingState { Idle, Ducking, Holding };
size_t holdSamplesRemaining_ = 0;
DuckingState state_ = DuckingState::Idle;
```

**Alternatives Considered**:
1. Add hold time to EnvelopeFollower → Rejected: Not part of 010 spec, violates scope
2. Post-process envelope with hold logic → Rejected: Less clear than explicit state machine

### D4: Gain Reduction Curve

**Decision**: Proportional attenuation with depth scaling

**Rationale**:
- When sidechain is exactly at threshold → 0 dB reduction
- When sidechain is 10+ dB above threshold → full depth reduction
- Linear interpolation in between provides smooth transitions
- Range parameter clamps maximum reduction

**Formula**:
```
overshoot_dB = sidechainLevel_dB - threshold_dB
factor = clamp(overshoot_dB / 10.0, 0.0, 1.0)
targetReduction_dB = depth_dB * factor
actualReduction_dB = max(targetReduction_dB, range_dB)  // range is negative
```

**Alternatives Considered**:
1. Instant full depth when above threshold → Rejected: Too abrupt for musical ducking
2. Use same ratio-based curve as compressor → Rejected: Ducking is depth-based by convention
3. Exponential curve → Rejected: Linear is standard and predictable

### D5: Attack/Release Behavior with Hold

**Decision**: Attack applies on trigger, Release applies after hold expires

**Rationale**:
- Attack smooths the gain reduction when sidechain triggers
- Hold delays the start of release
- Release smooths the return to unity gain

**Timing Sequence**:
```
1. Sidechain exceeds threshold → Enter DUCKING, attack begins
2. Sidechain drops below threshold → Enter HOLDING, hold timer starts
3. During HOLDING: if sidechain re-triggers → return to DUCKING, reset hold timer
4. Hold timer expires → Enter IDLE, release begins
5. Release completes → gain reduction returns to 0 dB
```

### D6: Dual-Input Processing API

**Decision**: `processSample(float main, float sidechain)` signature

**Rationale**:
- Matches common VST3 sidechain routing patterns
- Explicit separation of main and sidechain signals
- Block processing version takes two separate buffers

**API Pattern**:
```cpp
float processSample(float main, float sidechain) noexcept;
void process(const float* main, const float* sidechain, float* output, size_t numSamples) noexcept;
void process(float* mainInOut, const float* sidechain, size_t numSamples) noexcept;  // In-place
```

### D7: Sidechain HPF Placement

**Decision**: Apply HPF before EnvelopeFollower in DuckingProcessor

**Rationale**:
- EnvelopeFollower has its own sidechain HPF, but we want explicit control
- Apply filter in DuckingProcessor for clearer signal flow
- EnvelopeFollower's HPF can be left disabled

**Alternatives Considered**:
1. Use EnvelopeFollower's built-in HPF → Rejected: Less explicit, harder to test isolation
2. Apply HPF to main signal → Rejected: Wrong! HPF is for detection only

### D8: Parameter Ranges

**Decision**: Use spec-defined ranges matching industry standards

| Parameter | Range | Default | Unit |
|-----------|-------|---------|------|
| Threshold | -60 to 0 | -30 | dB |
| Depth | 0 to -48 | -12 | dB |
| Attack | 0.1 to 500 | 10 | ms |
| Release | 1 to 5000 | 100 | ms |
| Hold | 0 to 1000 | 50 | ms |
| Range | 0 to -48 | 0 (disabled) | dB |
| SC HPF | 20 to 500 | 80 | Hz |

**Rationale**: These ranges match FR-003 through FR-015 in the specification.

## Existing Codebase Patterns

### From DynamicsProcessor

Reusing patterns:
- `getCurrentGainReduction()` metering API
- `prepare(sampleRate, maxBlockSize)` / `reset()` lifecycle
- Component composition (EnvelopeFollower, Biquad, OnePoleSmoother)
- NaN/Inf input sanitization

Not reusing:
- Ratio-based gain reduction curve
- Knee calculation
- Makeup gain
- Lookahead delay

### From EnvelopeFollower

Reusing directly:
- `processSample(float input)` for sidechain level detection
- Attack/release coefficient calculation
- Peak detection mode

Configuring:
- Set to Peak mode for ducking
- Disable internal sidechain filter (handle in DuckingProcessor)

## Open Questions (Resolved)

All design questions have been resolved through analysis of existing implementations and industry standards. No external research was required.
