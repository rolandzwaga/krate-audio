# Feature Specification: Ducking Processor

**Feature Branch**: `012-ducking-processor`
**Created**: 2025-12-23
**Status**: Draft
**Input**: User description: "Ducking Processor - A Layer 2 DSP processor that attenuates audio based on an external sidechain signal level. Uses EnvelopeFollower to track the sidechain amplitude and applies gain reduction to the main signal when sidechain exceeds threshold."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Basic Ducking with Threshold and Depth (Priority: P1)

Audio engineers need to automatically reduce the level of one audio source when another source becomes active. The ducking processor monitors a sidechain signal and applies gain reduction to the main signal when the sidechain level exceeds a configurable threshold. The amount of attenuation is controlled by a depth parameter.

**Why this priority**: This is the core functionality - without threshold and depth control, the processor cannot perform its primary function of ducking audio.

**Independent Test**: Can be fully tested by feeding a sidechain signal above threshold and verifying the main signal is attenuated by the specified depth amount.

**Acceptance Scenarios**:

1. **Given** sidechain signal at -20 dB and threshold at -30 dB, **When** processing main audio, **Then** main signal is attenuated by the depth amount (e.g., -12 dB)
2. **Given** sidechain signal at -40 dB and threshold at -30 dB, **When** processing main audio, **Then** main signal passes through unattenuated (0 dB gain reduction)
3. **Given** depth set to -6 dB, **When** sidechain exceeds threshold, **Then** main signal is reduced by exactly 6 dB

---

### User Story 2 - Attack and Release Timing (Priority: P2)

Audio engineers need precise control over how quickly ducking engages (attack) and how quickly the signal recovers (release) to achieve natural-sounding results. Fast attack catches transients, slow attack allows punch through, fast release creates pumping effects, slow release sounds smooth.

**Why this priority**: Timing controls are essential for musical and natural-sounding ducking. Without them, the ducking would have abrupt, unpleasant transitions.

**Independent Test**: Can be fully tested by measuring the time it takes for gain reduction to reach target depth (attack) and return to unity (release).

**Acceptance Scenarios**:

1. **Given** attack time of 10ms, **When** sidechain suddenly exceeds threshold, **Then** gain reduction reaches ~63% of target within 10ms
2. **Given** release time of 100ms, **When** sidechain drops below threshold, **Then** gain recovers to ~63% of original within 100ms
3. **Given** fast attack (1ms), **When** transient appears in sidechain, **Then** ducking engages immediately without overshoot

---

### User Story 3 - Hold Time Control (Priority: P3)

Audio engineers need a hold time control to keep the signal ducked for a minimum duration, preventing rapid on/off switching (chattering) when the sidechain signal hovers around the threshold. This is essential for voiceover applications where breath pauses shouldn't cause the music to surge back.

**Why this priority**: Hold time prevents distracting pumping artifacts in many common use cases, but basic ducking works without it.

**Independent Test**: Can be fully tested by sending a sidechain signal that briefly exceeds then drops below threshold, verifying the hold time delays release.

**Acceptance Scenarios**:

1. **Given** hold time of 50ms, **When** sidechain exceeds threshold for 10ms then drops, **Then** release doesn't begin until 50ms after sidechain dropped
2. **Given** hold time of 0ms, **When** sidechain drops below threshold, **Then** release begins immediately
3. **Given** sidechain hovering around threshold, **When** hold time is set appropriately, **Then** output remains smoothly ducked without chattering

---

### User Story 4 - Range/Maximum Attenuation Control (Priority: P4)

Audio engineers need to limit the maximum amount of gain reduction to prevent complete silencing of the main signal. The range control sets a floor on how much the signal can be attenuated, ensuring the ducked signal remains audible.

**Why this priority**: Range control provides creative flexibility but basic ducking works with depth alone.

**Independent Test**: Can be fully tested by setting range to limit maximum attenuation and verifying the output level never drops below the range floor.

**Acceptance Scenarios**:

1. **Given** depth of -24 dB and range of -12 dB, **When** sidechain exceeds threshold, **Then** maximum attenuation is limited to -12 dB
2. **Given** range of -infinity (disabled), **When** sidechain exceeds threshold, **Then** full depth attenuation is applied
3. **Given** range equal to depth, **When** processing, **Then** output behaves as if range is disabled

---

### User Story 5 - Sidechain Highpass Filter (Priority: P5)

Audio engineers need to filter the sidechain signal to prevent low-frequency content (bass, kick drums) from triggering unwanted ducking. A highpass filter on the sidechain detection path allows focusing on specific frequency content for trigger detection.

**Why this priority**: Sidechain filtering is a common feature but optional for basic ducking operation.

**Independent Test**: Can be fully tested by sending bass-heavy sidechain signal with filter enabled and verifying reduced/no triggering compared to unfiltered.

**Acceptance Scenarios**:

1. **Given** sidechain HPF at 200 Hz, **When** 50 Hz bass signal in sidechain, **Then** bass content is attenuated before level detection, reducing false triggers
2. **Given** sidechain HPF disabled, **When** processing, **Then** full-bandwidth sidechain triggers ducking
3. **Given** voice + music sidechain, **When** HPF removes music bass, **Then** ducking responds primarily to voice frequencies

---

### User Story 6 - Gain Reduction Metering (Priority: P6)

Audio engineers need visual feedback showing the current amount of gain reduction being applied. This allows monitoring and adjusting ducking parameters in real-time.

**Why this priority**: Metering is valuable for user experience but not required for core ducking functionality.

**Independent Test**: Can be fully tested by verifying the metering value matches the actual gain reduction applied to the signal.

**Acceptance Scenarios**:

1. **Given** active ducking with -8 dB reduction, **When** querying gain reduction, **Then** metering reports -8 dB (within 0.5 dB)
2. **Given** no ducking active, **When** querying gain reduction, **Then** metering reports 0 dB
3. **Given** varying sidechain levels, **When** monitoring in real-time, **Then** metering smoothly tracks actual gain reduction

---

### Edge Cases

- What happens when sidechain input is silent (zero samples)?
  - No gain reduction applied; output equals input
- What happens when sidechain contains NaN or infinity values?
  - Invalid values should be sanitized to prevent undefined behavior
- What happens when attack time is set to minimum (near-instant)?
  - Gain reduction should still be smooth without digital artifacts
- What happens when depth exceeds range limit?
  - Range should cap the actual attenuation applied
- What happens when hold time exceeds release time?
  - Hold completes before release begins; parameters are independent

## Requirements *(mandatory)*

### Functional Requirements

**Core Ducking:**
- **FR-001**: System MUST apply gain reduction to main signal when sidechain level exceeds threshold
- **FR-002**: System MUST NOT apply gain reduction when sidechain level is below threshold
- **FR-003**: Threshold MUST be adjustable from -60 dB to 0 dB
- **FR-004**: Depth MUST be adjustable from 0 dB to -48 dB (amount of attenuation when fully ducked)

**Timing Controls:**
- **FR-005**: Attack time MUST be adjustable from 0.1 ms to 500 ms
- **FR-006**: Release time MUST be adjustable from 1 ms to 5000 ms
- **FR-007**: System MUST use envelope detection for smooth level tracking

**Hold Time:**
- **FR-008**: Hold time MUST be adjustable from 0 ms to 1000 ms
- **FR-009**: Hold time MUST delay the start of release after sidechain drops below threshold
- **FR-010**: Hold timer MUST reset if sidechain re-triggers during hold period

**Range Control:**
- **FR-011**: Range MUST be adjustable from 0 dB to -48 dB (maximum allowed attenuation)
- **FR-012**: When range is less than depth, actual attenuation MUST be limited to range value
- **FR-013**: When range equals 0 dB (or disabled), no limiting shall occur

**Sidechain Filtering:**
- **FR-014**: Sidechain highpass filter MUST be adjustable from 20 Hz to 500 Hz
- **FR-015**: Sidechain filter MUST be bypassable (enable/disable)
- **FR-016**: Sidechain filter MUST only affect detection path, not main audio path

**Processing Interface:**
- **FR-017**: System MUST provide dual-input processing (main audio + sidechain)
- **FR-018**: System MUST support per-sample processing with separate main and sidechain inputs
- **FR-019**: System MUST support block processing for efficiency

**Real-Time Safety:**
- **FR-020**: All processing functions MUST be declared noexcept
- **FR-021**: Processing MUST NOT allocate memory during audio callback
- **FR-022**: System MUST handle NaN and infinity inputs gracefully

**Lifecycle:**
- **FR-023**: System MUST provide prepare(sampleRate, maxBlockSize) for initialization
- **FR-024**: System MUST provide reset() to clear internal state

**Metering:**
- **FR-025**: System MUST provide getCurrentGainReduction() returning current attenuation in dB (negative value)

### Key Entities

- **DuckingProcessor**: The main processor class that performs sidechain-triggered gain reduction
- **Sidechain Signal**: External audio input used to control ducking behavior
- **Main Signal**: The audio being processed and attenuated
- **Gain Reduction**: The computed attenuation amount based on sidechain level and parameters

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Ducking accuracy within 0.5 dB of target depth when sidechain exceeds threshold by 10+ dB
- **SC-002**: Attack and release timing within 10% of specified time constants
- **SC-003**: Hold time accuracy within 5 ms of specified value
- **SC-004**: No audible clicks or discontinuities during ducking transitions (maximum sample-to-sample delta under control)
- **SC-005**: Sidechain filter reduces bass trigger response by at least 12 dB/octave below cutoff
- **SC-006**: Gain reduction metering matches actual attenuation within 0.5 dB
- **SC-007**: Processing adds less than 1% CPU overhead at 44.1 kHz stereo (512-sample blocks, measured as processing time / buffer duration)
- **SC-008**: Zero latency (no lookahead required for ducking)

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Sidechain signal is provided externally by the host or routing system
- Sample rates between 44.1 kHz and 192 kHz are supported
- Mono processing is sufficient (stereo ducking would use two instances or linked stereo mode)
- Host manages sidechain routing; processor receives sidechain as parameter to process function
- Parameters use standard audio ranges and units (dB, ms, Hz)

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| EnvelopeFollower | processors/envelope_follower.h | MUST reuse for sidechain level detection |
| OnePoleSmoother | primitives/smoother.h | Should reuse for gain reduction smoothing |
| Biquad | primitives/biquad.h | Should reuse for sidechain highpass filter |
| dbToGain/gainToDb | core/db_utils.h | MUST reuse for dB conversions |
| DynamicsProcessor | processors/dynamics_processor.h | Reference for gain reduction patterns |

**Initial codebase search for key terms:**

```bash
# Run these searches to identify existing implementations
grep -r "Ducker\|Ducking\|Duck" src/
grep -r "sidechain" src/
```

**Search Results Summary**: EnvelopeFollower and DynamicsProcessor provide reference implementations for envelope detection and gain reduction. No existing Ducking processor found.

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*Fill this table when claiming completion. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | | |
| FR-002 | | |
| ... | | |

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

**Overall Status**: [To be filled at completion]

**If NOT COMPLETE, document gaps:**
- [To be filled at completion]

**Recommendation**: [To be filled at completion]
