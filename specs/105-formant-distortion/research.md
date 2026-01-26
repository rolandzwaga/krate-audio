# Research: Formant Distortion Processor

**Spec**: 105-formant-distortion | **Date**: 2026-01-26 | **Status**: Complete

## Research Summary

All technical questions were resolved during spec clarification. No additional research required.

## Decisions Made (from Clarifications)

### 1. Envelope Modulation Direction

**Decision**: Unipolar positive modulation

**Rationale**: Matches natural "opening" of vowels with increased intensity. Louder sounds = higher formants = brighter, more open character.

**Formula**: `finalShift = staticShift + (envelope * modRange * amount)`

**Alternatives Considered**:
- Bipolar modulation: Rejected - unnatural, complicates UI, no clear use case
- Negative modulation: Rejected - counterintuitive (louder = darker)

### 2. Mix Stage Location

**Decision**: Post-DC blocker, inside FormantDistortion

**Rationale**: Ensures DC offset is removed before mixing with dry signal. Prevents DC bleed into output.

**Alternatives Considered**:
- Pre-DC blocker: Rejected - DC would contaminate mixed output
- External mix: Rejected - less convenient, user would need separate mix processing

### 3. Parameter Smoothing

**Decision**: Delegate to FormantFilter's internal smoothing (no double-smoothing)

**Rationale**: FormantFilter already implements 5ms default smoothing on formant frequencies. Adding another layer would slow response unnecessarily.

**Alternatives Considered**:
- Smooth in FormantDistortion: Rejected - causes sluggish response (double smoothing)
- No smoothing: Rejected - causes zipper noise on parameter changes

### 4. Vowel Mode State Management

**Decision**: Independent parameters with mode flag

**Rationale**: Both `vowel_` and `vowelBlend_` retain their values. `useBlendMode_` determines active mode. Allows instant switching between modes without value loss.

**Alternatives Considered**:
- Single parameter: Rejected - loses discrete vowel selection when in blend mode
- Linked parameters: Rejected - complex state synchronization

### 5. Envelope Tracking Point

**Decision**: Track raw input (before any processing)

**Rationale**: Provides consistent dynamic response regardless of drive setting. User intent is to modulate based on playing dynamics, not processed signal level.

**Alternatives Considered**:
- Post-drive tracking: Rejected - higher drive would artificially inflate envelope
- Post-formant tracking: Rejected - formant filtering would alter envelope response

## Component API Verification

All composed components have been verified:

| Component | Header Read | API Verified |
|-----------|-------------|--------------|
| FormantFilter | Yes | Full API documented in plan.md |
| Waveshaper | Yes | Full API documented in plan.md |
| EnvelopeFollower | Yes | Full API documented in plan.md |
| DCBlocker | Yes | Full API documented in plan.md |
| OnePoleSmoother | Yes | Full API documented in plan.md |

## No External Research Needed

This feature is pure composition of existing, verified components. No external research (context7 MCP, web search) was required because:

1. All algorithms are implemented in existing components
2. Signal flow is straightforward (serial chain with parallel envelope)
3. No new DSP concepts introduced
4. All parameter ranges match existing component constraints

## References

- FormantFilter: `specs/077-formant-filter/spec.md`
- Waveshaper: `specs/052-waveshaper/spec.md`
- EnvelopeFollower: `specs/010-envelope-follower/spec.md`
- DCBlocker: `specs/051-dc-blocker/spec.md`
