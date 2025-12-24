# Feature Specification: Diffusion Network

**Feature Branch**: `015-diffusion-network`
**Created**: 2025-12-24
**Status**: Draft
**Layer**: 2 (DSP Processor)
**Input**: User description: "Layer 2 DSP Processor - Allpass diffusion network for creating smeared, reverb-like textures in delay feedback paths. Cascade of 4-8 allpass filters with prime-related delay times. Parameters: Size (scales all delay times), Density (number of active stages), Modulation (LFO on delay times). Composes DelayLine and Biquad (allpass mode) from Layer 1. Used by Shimmer mode and ambient delay effects. Real-time safe, no allocations in process()."

## Overview

A Diffusion Network creates temporal smearing by passing audio through a cascade of allpass filters with carefully chosen delay times. Unlike regular filters, allpass filters preserve the frequency spectrum while altering the phase relationship of different frequencies. When cascaded with mutually irrational delay times, this creates a "smeared" or "diffused" sound without the metallic artifacts of comb filtering.

**Primary use cases:**
- Shimmer delay mode (diffused feedback path)
- Ambient/atmospheric delay textures
- Reverb-like decay without full reverb complexity
- Smoothing transients in feedback loops

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Basic Diffusion Processing (Priority: P1)

As a sound designer, I want to pass audio through a diffusion network so that transients are smeared into a smooth, ambient texture while preserving the overall frequency balance.

**Why this priority**: This is the core functionality - without basic diffusion processing, no other features work. The allpass cascade is the foundation of the entire processor.

**Independent Test**: Can be fully tested by processing an impulse and verifying the output spreads the energy over time while maintaining flat frequency response.

**Acceptance Scenarios**:

1. **Given** a diffusion network with default settings (size=50%, density=100%), **When** an impulse is processed, **Then** the output energy is spread over time (not concentrated at a single sample)
2. **Given** a diffusion network at any setting, **When** white noise is processed, **Then** the output spectrum matches the input spectrum (within 0.5dB across audible range)
3. **Given** a diffusion network prepared at 44.1kHz, **When** processing a block of audio, **Then** no memory allocations occur during process()

---

### User Story 2 - Size Control (Priority: P1)

As a sound designer, I want to control the size of the diffusion so that I can create anything from subtle smearing to long reverb-like tails.

**Why this priority**: Size is the most important creative control - it determines the character of the diffusion from tight/focused to expansive/washy.

**Independent Test**: Can be tested by processing impulses at different size settings and measuring the decay envelope duration.

**Acceptance Scenarios**:

1. **Given** a diffusion network with size=0%, **When** audio is processed, **Then** output equals input (bypass behavior)
2. **Given** a diffusion network with size=50%, **When** an impulse is processed, **Then** energy spreads over approximately 25-50ms
3. **Given** a diffusion network with size=100%, **When** an impulse is processed, **Then** energy spreads over approximately 50-100ms
4. **Given** size parameter changes during processing, **When** audio is processed, **Then** transitions are smooth without clicks or artifacts

---

### User Story 3 - Density Control (Priority: P2)

As a sound designer, I want to control how many diffusion stages are active so that I can balance CPU usage against diffusion quality.

**Why this priority**: Density allows trading off quality vs CPU when needed, but default full-density works for most cases.

**Independent Test**: Can be tested by comparing diffusion quality metrics at different density settings.

**Acceptance Scenarios**:

1. **Given** density=25% (2 stages), **When** an impulse is processed, **Then** output shows clear discrete echoes with some smearing
2. **Given** density=50% (4 stages), **When** an impulse is processed, **Then** output shows moderate diffusion with slight echo definition
3. **Given** density=100% (8 stages), **When** an impulse is processed, **Then** output shows smooth, continuous diffusion with no discrete echoes
4. **Given** density changes during processing, **When** audio is processed, **Then** transitions are smooth without clicks

---

### User Story 4 - Modulation (Priority: P2)

As a sound designer, I want to modulate the diffusion delay times with an LFO so that the texture has subtle movement and avoids static/metallic artifacts.

**Why this priority**: Modulation adds life to the diffusion but is optional for basic functionality.

**Independent Test**: Can be tested by measuring frequency deviation in the output when modulation is enabled.

**Acceptance Scenarios**:

1. **Given** modulation depth=0%, **When** audio is processed, **Then** output has no pitch/time modulation artifacts
2. **Given** modulation depth=50%, **When** a sustained tone is processed, **Then** output has subtle chorus-like movement
3. **Given** modulation depth=100%, **When** audio is processed, **Then** output has noticeable pitch warble within acceptable range
4. **Given** modulation rate parameter, **When** rate is set from 0.1Hz to 5Hz, **Then** modulation speed changes accordingly

---

### User Story 5 - Stereo Processing (Priority: P2)

As a sound designer, I want stereo diffusion with decorrelated left/right processing so that the output has width and doesn't collapse to mono.

**Why this priority**: Stereo decorrelation is essential for professional-quality diffusion, but mono processing is a valid MVP.

**Independent Test**: Can be tested by processing mono content and measuring stereo decorrelation in output.

**Acceptance Scenarios**:

1. **Given** a mono input (L=R), **When** processed through stereo diffusion, **Then** output L and R are decorrelated (cross-correlation < 0.5)
2. **Given** a stereo input, **When** processed, **Then** stereo image is preserved or widened (not narrowed)
3. **Given** width parameter at 0%, **When** processed, **Then** L and R outputs are identical (mono)
4. **Given** width parameter at 100%, **When** processed, **Then** maximum decorrelation is applied

---

### User Story 6 - Real-Time Safety (Priority: P1)

As a plugin developer, I want the diffusion network to be fully real-time safe so that it can be used in the audio processing callback without risking dropouts.

**Why this priority**: Real-time safety is a constitutional requirement (Principle II) - violations are unacceptable.

**Independent Test**: Can be tested by static analysis and runtime memory allocation detection.

**Acceptance Scenarios**:

1. **Given** a prepared diffusion network, **When** process() is called, **Then** no memory allocations occur
2. **Given** any parameter change, **When** setter is called, **Then** no memory allocations occur
3. **Given** the DiffusionNetwork class, **When** compiled, **Then** process() method is marked noexcept
4. **Given** block sizes from 1 to 8192 samples, **When** processed, **Then** all complete successfully

---

### Edge Cases

- **Zero-length input**: process() with numSamples=0 should return immediately without error
- **In-place processing**: Input and output buffers may be the same memory
- **Sample rate changes**: prepare() may be called with different sample rates
- **NaN/Infinity input**: Should handle gracefully without corrupting internal state
- **Extreme size values**: Values outside 0-100% should be clamped, not rejected
- **Rapid parameter automation**: Fast parameter changes should not cause artifacts

## Requirements *(mandatory)*

### Functional Requirements

#### Core Processing

- **FR-001**: System MUST implement allpass filtering that preserves input frequency spectrum (within 0.5dB)
- **FR-002**: System MUST cascade 8 allpass stages with independently configurable delay times
- **FR-003**: System MUST use mutually irrational delay time ratios to avoid comb filtering artifacts
- **FR-004**: System MUST support stereo processing with decorrelated left/right paths
- **FR-005**: System MUST support in-place processing (input buffer == output buffer)

#### Size Parameter

- **FR-006**: System MUST provide size parameter [0%, 100%] that scales all delay times proportionally
- **FR-007**: Size=0% MUST produce bypass behavior (output equals input)
- **FR-008**: Size=100% MUST produce maximum diffusion spread (50-100ms at 44.1kHz)
- **FR-009**: Size parameter changes MUST be smoothed to prevent clicks (10ms smoothing time)

#### Density Parameter

- **FR-010**: System MUST provide density parameter [0%, 100%] controlling active stage count
- **FR-011**: Density maps to active stages: 0%=0 (bypass), 25%=2, 50%=4, 75%=6, 100%=8
- **FR-012**: Inactive stages MUST be bypassed (not processed) for CPU efficiency
- **FR-013**: Density parameter changes MUST be smoothed to prevent clicks

#### Modulation

- **FR-014**: System MUST provide modulation depth parameter [0%, 100%]
- **FR-015**: System MUST provide modulation rate parameter [0.1Hz, 5Hz]
- **FR-016**: Modulation MUST apply phase-offset modulation to each stage's delay time (single shared LFO with per-stage phase offsets)
- **FR-017**: Each stage MUST have a unique phase offset (45° per stage) for decorrelation
- **FR-018**: Modulation depth=0% MUST produce no pitch/time artifacts

#### Stereo

- **FR-019**: System MUST provide stereo width parameter [0%, 100%]
- **FR-020**: Width=0% MUST produce mono output (L=R)
- **FR-021**: Width=100% MUST produce maximum stereo decorrelation
- **FR-022**: Left and right channels MUST use different delay time sets for decorrelation

#### Real-Time Safety

- **FR-023**: All processing methods MUST be noexcept
- **FR-024**: process() MUST NOT allocate memory
- **FR-025**: All parameter setters MUST NOT allocate memory
- **FR-026**: prepare() MUST pre-allocate all required buffers

#### Lifecycle

- **FR-027**: System MUST provide prepare(sampleRate, maxBlockSize) method
- **FR-028**: System MUST provide reset() method to clear internal state
- **FR-029**: System MUST support sample rates from 44.1kHz to 192kHz
- **FR-030**: System MUST support block sizes from 1 to 8192 samples

### Key Entities

- **AllpassStage**: A single allpass filter with configurable delay time, part of the cascade
- **DiffusionNetwork**: The complete processor containing 8 stereo allpass stages
- **ModulationSource**: LFO providing time modulation to delay times

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Diffusion network preserves frequency spectrum within 0.5dB across 20Hz-20kHz
- **SC-002**: Processing an impulse at size=100% spreads energy over 50-100ms
- **SC-003**: Stereo processing achieves L/R cross-correlation below 0.5 for mono input
- **SC-004**: CPU usage is less than 1% per instance at 44.1kHz stereo
- **SC-005**: No memory allocations during process() (verified by allocation tracking)
- **SC-006**: Parameter changes complete smoothly within 10ms without audible artifacts
- **SC-007**: Test suite achieves 100% coverage of functional requirements
- **SC-008**: All 8 density levels (1-8 stages) produce distinct diffusion characteristics

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Sample rates will be standard audio rates (44.1, 48, 88.2, 96, 176.4, 192 kHz)
- Maximum delay time per stage is 50ms (sufficient for diffusion, not reverb)
- Irrational delay ratios based on square roots: {1.0, 1.127, 1.414, 1.732, 2.236, 2.828, 3.317, 4.123} multiplied by base time (similar to classic Lexicon reverbs)
- Default smoothing time for parameters is 10ms (consistent with other processors)
- Modulation depth range of 2ms is sufficient for subtle movement

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component        | Location                       | Relevance                                |
|------------------|--------------------------------|------------------------------------------|
| DelayLine        | dsp/primitives/delay_line.h    | Use for allpass delay buffers            |
| Biquad           | dsp/primitives/biquad.h        | Reference for allpass coefficients       |
| LFO              | dsp/primitives/lfo.h           | Use for modulation source                |
| OnePoleSmoother  | dsp/primitives/smoother.h      | Use for parameter smoothing              |
| dbToGain/gainToDb| dsp/core/db_utils.h            | May use for gain calculations            |

**Initial codebase search for key terms:**

```bash
# Run these searches to identify existing implementations
grep -r "allpass" src/
grep -r "diffusion" src/
grep -r "Diffuser" src/
```

**Search Results Summary**: To be completed during planning phase.

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*Fill this table when claiming completion. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001      |        |          |
| FR-002      |        |          |
| FR-003      |        |          |
| ...         |        |          |
| SC-001      |        |          |
| SC-002      |        |          |
| ...         |        |          |

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
