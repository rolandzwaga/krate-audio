# Feature Specification: Reverse Delay Mode

**Feature Branch**: `030-reverse-delay`
**Created**: 2025-12-26
**Status**: Draft
**Input**: User description: "Reverse Delay mode - a delay effect that plays back audio in reverse, creating backwards/reversed echoes. Based on roadmap section 4.7: uses Reverse Buffer (capture and playback reversed chunks), Delay Line, and Feedback Network. Features include chunk size control for reverse segment length, crossfade for smooth transitions between chunks, feedback for multiple reversed repetitions, and optional filtering in feedback path."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Basic Reverse Echo (Priority: P1)

A musician wants to add backwards echo effects to their track. They enable reverse delay and hear their audio played back in reverse after a set time interval. The effect creates an ethereal, otherworldly quality where notes seem to swell into existence rather than decay away.

**Why this priority**: This is the core functionality - without reverse playback, the feature has no purpose. Every other feature builds on this foundation.

**Independent Test**: Can be fully tested by sending an impulse or short sound through the effect and verifying the output is time-reversed within the buffer window. Delivers the fundamental reverse delay experience.

**Acceptance Scenarios**:

1. **Given** the reverse delay is enabled with 500ms chunk size, **When** the user plays a short percussive sound, **Then** the delayed output plays the sound backwards, starting from the end of the captured chunk
2. **Given** the reverse delay is processing audio, **When** each chunk boundary is reached, **Then** the next reversed chunk begins without audible gaps or clicks
3. **Given** the reverse delay has 50% feedback, **When** audio is processed, **Then** multiple reversed repetitions occur, each quieter than the last

---

### User Story 2 - Smooth Crossfade Transitions (Priority: P2)

A producer notices clicks or abrupt transitions between reverse chunks at certain settings. They adjust the crossfade control to blend chunk boundaries smoothly. Higher crossfade values create more overlap between chunks, producing a smoother, more continuous reverse texture.

**Why this priority**: Without smooth crossfading, the effect has audible artifacts at chunk boundaries that make it unusable in professional contexts. This is essential for production-quality output.

**Independent Test**: Can be tested by processing continuous audio and measuring the amplitude at chunk boundaries - properly crossfaded output should have no sudden level changes.

**Acceptance Scenarios**:

1. **Given** crossfade is set to 0%, **When** chunk boundaries occur, **Then** there is a hard cut between chunks (acceptable for some creative uses)
2. **Given** crossfade is set to 50%, **When** chunk boundaries occur, **Then** the ending chunk fades out while the next chunk fades in, overlapping for smooth transition
3. **Given** crossfade is set to 100%, **When** chunks transition, **Then** maximum overlap occurs with gradual blending between consecutive reversed segments

---

### User Story 3 - Playback Mode Selection (Priority: P3)

A sound designer wants different flavors of reverse effect. They select between Full Reverse (every chunk reversed), Alternating (forward, reverse, forward, reverse), and Random (each chunk randomly chooses direction). Each mode creates distinctly different textures from the same source material.

**Why this priority**: Multiple modes expand creative possibilities significantly but the core effect (P1) and quality (P2) must work first.

**Independent Test**: Can be tested by processing a known pattern and verifying correct forward/reverse sequence for each mode setting.

**Acceptance Scenarios**:

1. **Given** Full Reverse mode is selected, **When** audio is processed, **Then** every chunk is played backwards
2. **Given** Alternating mode is selected, **When** audio is processed, **Then** chunks alternate between forward and reverse playback
3. **Given** Random mode is selected, **When** audio is processed, **Then** each chunk's direction is independently randomized

---

### User Story 4 - Feedback with Filtering (Priority: P4)

A musician using the reverse delay wants the feedback to darken over time like an old tape machine. They enable the feedback filter and adjust the cutoff to remove high frequencies from each repetition. The result is reversed echoes that become progressively warmer and more diffuse.

**Why this priority**: Feedback filtering is an enhancement that makes the effect more versatile, but the core reverse functionality must work first.

**Independent Test**: Can be tested by measuring frequency content of successive feedback iterations - energy above the filter cutoff should decrease with each pass.

**Acceptance Scenarios**:

1. **Given** filter is enabled with 2kHz lowpass cutoff and 80% feedback, **When** audio circulates through feedback, **Then** each iteration loses high frequency content above 2kHz
2. **Given** filter is disabled, **When** audio circulates through feedback, **Then** frequency content remains unchanged between iterations

---

### Edge Cases

- What happens when chunk size is very short (10-50ms)? Effect becomes granular/glitchy, which is musically useful
- What happens when chunk size exceeds the maximum buffer? System clamps to maximum available buffer
- What happens when feedback is 100% or higher? Feedback limiter prevents runaway oscillation
- What happens when input is silent during a chunk capture? Silent chunk is reversed (still silent), no artifacts
- What happens at chunk boundaries during extreme parameter changes? Crossfade smooths transitions
- How does tempo sync interact with chunk size? Chunk size locks to musical divisions (1/4, 1/8, 1/16 notes, etc.)

## Requirements *(mandatory)*

### Functional Requirements

**Core Reverse Engine**:
- **FR-001**: System MUST capture incoming audio into a buffer of configurable size (10ms to 2000ms)
- **FR-002**: System MUST play back captured audio in reverse order within the buffer window
- **FR-003**: System MUST support continuous operation with seamless buffer recycling
- **FR-004**: System MUST process stereo audio with independent left/right reverse buffers

**Chunk Control**:
- **FR-005**: System MUST provide chunk size control from 10ms to 2000ms
- **FR-006**: System MUST support tempo-synchronized chunk sizes (1/32 to 1/1 note values)
- **FR-007**: System MUST provide smooth parameter changes for chunk size without audio glitches

**Crossfade Control**:
- **FR-008**: System MUST provide crossfade control from 0% to 100%
- **FR-009**: System MUST implement equal-power crossfade for smooth level transitions
- **FR-010**: Crossfade at 100% MUST create maximum overlap between consecutive chunks

**Playback Modes**:
- **FR-011**: System MUST support Full Reverse mode (all chunks reversed)
- **FR-012**: System MUST support Alternating mode (forward, reverse, forward, reverse)
- **FR-013**: System MUST support Random mode (each chunk randomly forward or reverse)
- **FR-014**: Mode changes MUST take effect at next chunk boundary, not mid-chunk

**Feedback Path**:
- **FR-015**: System MUST use FlexibleFeedbackNetwork (Layer 3) for feedback management
- **FR-016**: System MUST support feedback amount from 0% to 120%
- **FR-017**: System MUST apply soft limiting when feedback exceeds 100% to prevent runaway oscillation
- **FR-018**: System MUST support optional filter in feedback path (lowpass/highpass/bandpass)
- **FR-019**: Feedback filter cutoff MUST be controllable from 20Hz to 20kHz

**Mixing and Output**:
- **FR-020**: System MUST provide dry/wet mix control from 0% to 100%
- **FR-021**: System MUST provide output gain control from -infinity to +6dB
- **FR-022**: All parameter changes MUST be smoothed to prevent zipper noise

**Real-Time Safety**:
- **FR-023**: All processing MUST be real-time safe (no allocations in audio callback)
- **FR-024**: System MUST report latency to host for plugin delay compensation
- **FR-025**: System MUST reset cleanly without audio artifacts

### Key Entities

- **ReverseBuffer**: Layer 1 primitive - double-buffer that captures audio and provides reversed playback with crossfade
- **ReverseFeedbackProcessor**: Layer 2 processor - implements IFeedbackProcessor, wraps stereo ReverseBuffer pair, manages chunk boundaries and playback mode logic
- **ReverseDelay**: Layer 4 feature - composes ReverseFeedbackProcessor with FlexibleFeedbackNetwork for complete reverse delay effect

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Reverse playback is sample-accurate (first sample of input becomes last sample of reversed output within each chunk)
- **SC-002**: Crossfade transitions produce no clicks or pops (peak difference at boundaries < 0.01 when crossfade > 0%)
- **SC-003**: Chunk size changes complete within 50ms without audible artifacts
- **SC-004**: Full Reverse, Alternating, and Random modes produce correct playback patterns as specified
- **SC-005**: Feedback at 100% sustains indefinitely without runaway (output level bounded)
- **SC-006**: All parameter smoothing completes within 20ms (no zipper noise audible)
- **SC-007**: Effect introduces no more than one chunk of latency (chunk_size samples)
- **SC-008**: CPU usage remains below 1% per instance at 44.1kHz stereo

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Audio is provided as stereo float buffers at standard sample rates (44.1kHz to 192kHz)
- Host provides tempo information for tempo-synced chunk sizes
- Maximum chunk size of 2 seconds is sufficient for musical applications
- Users understand that reverse delay inherently introduces latency equal to chunk size

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| FlexibleFeedbackNetwork | src/dsp/systems/flexible_feedback_network.h | MUST reuse - provides feedback, filtering, limiting |
| DelayLine | src/dsp/primitives/delay_line.h | Reference for circular buffer patterns |
| OnePoleSmoother | src/dsp/primitives/smoother.h | MUST reuse for parameter smoothing |
| MultimodeFilter | src/dsp/processors/multimode_filter.h | Already integrated in FlexibleFeedbackNetwork |
| BlockContext | src/dsp/types/block_context.h | MUST reuse for tempo sync |

**Initial codebase search for key terms:**

```bash
grep -r "reverse" src/
grep -r "ReverseBuffer" src/
grep -r "chunk" src/
```

**Search Results Summary**: No existing ReverseBuffer or chunk-based playback components found. Need to create new Layer 1 primitive (ReverseBuffer) and Layer 4 feature (ReverseDelay).

### Forward Reusability Consideration

**Sibling features at same layer**:
- Granular Delay (4.8) - may share chunk/grain capture concepts
- Freeze Mode (4.11) - already uses FlexibleFeedbackNetwork pattern

**Potential shared components**:
- ReverseBuffer primitive could be useful for granular delay's time-domain grains
- Crossfade logic is generic and could be extracted to Layer 0 if needed elsewhere
- ChunkManager pattern may apply to other buffer-based effects

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*Fill this table when claiming completion. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | | |
| FR-002 | | |
| SC-001 | | |
| SC-002 | | |

**Status Key:**
- MET: Requirement fully satisfied with test evidence
- NOT MET: Requirement not satisfied (spec is NOT complete)
- PARTIAL: Partially met with documented gap
- DEFERRED: Explicitly moved to future work with user approval

### Completion Checklist

*All items must be checked before claiming completion:*

- [ ] All FR-xxx requirements verified against implementation
- [ ] All SC-xxx success criteria measured and documented
- [ ] No test thresholds relaxed from spec requirements
- [ ] No placeholder values or TODO comments in new code
- [ ] No features quietly removed from scope
- [ ] User would NOT feel cheated by this completion claim

### Honest Assessment

**Overall Status**: [COMPLETE / NOT COMPLETE / PARTIAL]

**If NOT COMPLETE, document gaps:**
- [Gap 1: FR-xxx not met because...]
- [Gap 2: SC-xxx achieves X instead of Y because...]

**Recommendation**: [What needs to happen to achieve completion]
