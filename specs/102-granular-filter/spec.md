# Feature Specification: GranularFilter

**Feature Branch**: `102-granular-filter`
**Created**: 2026-01-25
**Status**: Draft
**Input**: User description: "GranularFilter - Layer 3 DSP component extending GranularEngine with per-grain SVF filtering"

## Clarifications

### Session 2026-01-25

- Q: When pitch shifting is applied (which most grains will use), should the filter process AFTER pitch shift (read → pitch → envelope → filter → pan) or BEFORE pitch shift (read → envelope → filter → pitch → pan)? → A: A (AFTER pitch shift - filter shapes the pitch-shifted result)
- Q: Should Q (resonance) be randomizable per-grain like cutoff, or fixed globally for all grains? → A: B (Q is fixed globally for all grains - simpler, fewer parameters)
- Q: Should filter type be global (all grains use same type) or per-grain randomizable (each grain randomly assigned a type)? → A: A (Filter type is global - provides predictable, coherent timbral character)
- Q: What is the reference system for SC-003 CPU budget measurement (< 5% CPU)? → A: Intel i5-8400 (6-core, 2.8GHz base, 2017 midrange desktop CPU)
- Q: For stereo processing, should each grain use one shared filter (mono process both channels identically) or two independent filters (one per channel for true stereo filtering)? → A: B (Two filters per grain - independent L/R filter state for richer stereo imaging)

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Per-Grain Filter Processing (Priority: P1)

A sound designer wants to create rich, evolving granular textures by applying unique filter characteristics to each grain. They load audio into the granular processor, and each grain is filtered independently with its own SVF instance, creating spectral variations that are impossible with post-granular filtering.

**Why this priority**: This is the core value proposition - without per-grain filtering, this is just the existing GranularEngine. Per-grain filtering enables spectral variations that sum to complex, evolving textures.

**Independent Test**: Can be fully tested by processing audio through GranularFilter with filter enabled and verifying each grain has independent filter state. Delivers spectral richness impossible with global filtering.

**Acceptance Scenarios**:

1. **Given** GranularFilter is prepared with sample rate 48000Hz and max grain size 100ms, **When** grains are triggered with filter enabled (LP at 1kHz), **Then** each grain has an independent filter state that does not affect other concurrent grains
2. **Given** multiple grains are active simultaneously, **When** processing audio, **Then** each grain's filter output is independent (verified by comparing single-grain vs multi-grain processing)

---

### User Story 2 - Randomizable Filter Cutoff (Priority: P1)

A producer wants to create evolving, non-static granular clouds by having each grain's filter cutoff randomly vary from a base frequency. They set a base cutoff of 2kHz with 2 octaves of randomization, and each new grain gets a cutoff somewhere between 500Hz and 8kHz, creating rich spectral movement.

**Why this priority**: Randomization is essential for the "cloud" quality of granular synthesis. Without it, all grains would sound identical, defeating the purpose of per-grain filtering.

**Independent Test**: Can be fully tested by triggering multiple grains and verifying their cutoff frequencies are distributed within the specified range. Delivers sonic variety and movement in granular textures.

**Acceptance Scenarios**:

1. **Given** base cutoff is 1000Hz and randomization is 2 octaves, **When** 100 grains are triggered, **Then** grain cutoffs are distributed between 250Hz (1000/4) and 4000Hz (1000*4)
2. **Given** base cutoff is 500Hz and randomization is 0 octaves, **When** grains are triggered, **Then** all grains have exactly 500Hz cutoff (no randomization)
3. **Given** deterministic RNG seed is set, **When** the same grain sequence is triggered twice, **Then** cutoff values are identical (reproducible behavior)

---

### User Story 3 - Filter Type Selection (Priority: P2)

A user wants to shape the overall character of their granular texture by selecting different filter types. They switch between lowpass for warm textures, highpass for airy sounds, bandpass for resonant sweeps, and notch for phasing effects.

**Why this priority**: Different filter types dramatically change the sonic character, but the core functionality (per-grain filtering with randomization) must work first.

**Independent Test**: Can be fully tested by comparing output spectra with different filter types. Each type delivers a distinct timbral character to the granular cloud.

**Acceptance Scenarios**:

1. **Given** filter type is Lowpass and cutoff is 1kHz, **When** processing white noise, **Then** output spectrum shows significant attenuation above 1kHz
2. **Given** filter type is Highpass and cutoff is 1kHz, **When** processing white noise, **Then** output spectrum shows significant attenuation below 1kHz
3. **Given** filter type is Bandpass and cutoff is 1kHz with Q=4, **When** processing white noise, **Then** output spectrum shows a resonant peak at 1kHz

---

### User Story 4 - Filter Resonance Control (Priority: P2)

A user wants to add resonant character to their granular textures by adjusting the filter Q. At low Q values (0.7), the filter is transparent; at high Q values (10+), the filter rings and adds harmonic emphasis at the cutoff frequency.

**Why this priority**: Resonance is a fundamental filter parameter that shapes the tonal character, but it's secondary to getting the basic filtering working.

**Independent Test**: Can be fully tested by measuring resonant peak amplitude at different Q values. Delivers control over the harmonic emphasis of the granular texture.

**Acceptance Scenarios**:

1. **Given** Q is 0.7071 (Butterworth), **When** measuring frequency response, **Then** no resonant peak is present at cutoff
2. **Given** Q is 10, **When** measuring frequency response, **Then** a resonant peak of approximately 20dB is present at cutoff
3. **Given** Q is set to a value outside range 0.5-20, **When** processing, **Then** Q is clamped to valid range (no instability)

---

### User Story 5 - Integration with Existing Granular Parameters (Priority: P3)

A user wants to use all existing GranularEngine features (pitch shift, position spray, reverse probability, density, envelope type) alongside the new filtering capabilities. The filter enhancement should not break or limit any existing functionality.

**Why this priority**: Compatibility with existing features is important for practical use, but the new filtering features must work first.

**Independent Test**: Can be fully tested by verifying all existing GranularEngine parameters work correctly when filter is both enabled and disabled.

**Acceptance Scenarios**:

1. **Given** GranularFilter with pitch set to +12 semitones and filter enabled, **When** processing, **Then** grains are pitch-shifted AND filtered correctly
2. **Given** GranularFilter with reverse probability 0.5, **When** triggering many grains, **Then** approximately 50% are reversed, and all are filtered
3. **Given** all existing GranularEngine parameters are set to various values, **When** processing, **Then** behavior matches GranularEngine plus filter effect

---

### Edge Cases

- What happens when cutoff randomization pushes frequency below 20Hz or above Nyquist/2?
  - Cutoff is clamped to valid range (20Hz to sampleRate * 0.495)
- How does the system handle Q values that could cause instability?
  - Q is clamped to 0.5-20 range before coefficient calculation
- What happens when grain density is very high (100+ grains)?
  - Per-grain filter state is maintained; CPU load scales linearly with active grains
- What happens when filter is disabled mid-processing?
  - Grains currently active complete with their filter state; new grains bypass filtering
- What happens if prepare() is called with a different sample rate while grains are active?
  - All active grains are released, delay buffers are reallocated, all filter states are reset and re-prepared with the new sample rate

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: System MUST provide a `GranularFilter` class in `dsp/include/krate/dsp/systems/granular_filter.h` at Layer 3
- **FR-002**: System MUST allocate two SVF filter instances per grain (one for left channel, one for right channel) in the grain pool (up to 64 grains, total 128 filter instances for true stereo processing)
- **FR-003**: System MUST support filter cutoff randomization specified in octaves (range: 0-4 octaves from base frequency)
- **FR-004**: System MUST support all four basic SVF filter types: Lowpass, Highpass, Bandpass, Notch. Filter type is applied globally to all grains (not randomized per-grain)
- **FR-005**: System MUST support filter resonance (Q) control with range 0.5 to 20. Q is applied globally to all grains (not randomized per-grain)
- **FR-006**: System MUST clamp filter cutoff to valid range (20Hz to sampleRate * 0.495) after randomization
- **FR-007**: System MUST support deterministic seeding for reproducible grain filter assignments
- **FR-008**: System MUST reset filter state when a grain slot is acquired (before grain initialization begins) to prevent state leakage between grains
- **FR-009**: System MUST support all existing GranularEngine parameters: grain size, density, pitch, pitch spray, position, position spray, reverse probability, pan spray, jitter, envelope type, texture, freeze
- **FR-010**: System MUST support stereo processing with independent filter states for left and right channels (true stereo filtering, not mono duplication)
- **FR-011**: System MUST be real-time safe (noexcept, no allocations in process path)
- **FR-012**: System MUST apply filter in the grain processing chain at this position: read sample → pitch shift → envelope → **filter** → pan. Filter processes the pitch-shifted, enveloped audio before panning is applied
- **FR-013**: System MUST support filter bypass mode where filtering is completely skipped
- **FR-014**: System MUST prepare all SVF instances when `prepare()` is called with sample rate
- **FR-015**: System MUST provide a mechanism (e.g., `getGrainSlotIndex()` helper) to map grain pointers to their corresponding filter state array indices using pointer arithmetic

### Key Entities

- **GranularFilter**: Layer 3 system component that extends granular processing with per-grain filtering. Contains GrainPool, GrainScheduler, modified GrainProcessor, and delay buffers.
- **FilteredGrainState**: Parallel state structure containing two SVF filter instances (left and right channels), filter cutoff value, and filterEnabled flag per grain slot. Indexed parallel to GrainPool::grains_[] array
- **SVF**: Existing TPT State Variable Filter primitive from `primitives/svf.h` - two instances per active grain (one for L, one for R)

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Per-grain filtering produces measurably different spectra for grains with different cutoff values (verified by FFT analysis of individual grain outputs)
- **SC-002**: Filter cutoff randomization distributes values across the specified octave range with uniform distribution (verified statistically over 1000+ grains)
- **SC-003**: Processing 64 simultaneous filtered grains completes within real-time constraints at 48kHz sample rate (< 5% CPU on reference system: Intel i5-8400, 6-core, 2.8GHz base). Fallback: If reference hardware unavailable, measure relative overhead vs GranularEngine baseline (filter overhead should be < 25% additional CPU)
- **SC-004**: All existing GranularEngine test cases pass when executed against GranularFilter with filter disabled
- **SC-005**: Filter state is fully reset between grain lifetimes (no audible artifacts from previous grain's filter state)
- **SC-006**: Deterministic seeding produces bit-identical output across multiple runs with same parameters
- **SC-007**: Filter bypass mode produces output identical to GranularEngine (bit-identical when seeded)

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- The existing GranularEngine architecture (pool, scheduler, processor) is a suitable foundation for extension
- 128 simultaneous SVF instances (64 grains × 2 channels) is acceptable for memory and CPU budget
- Per-grain filter state (2 floats × 2 channels for SVF integrators) adds acceptable memory overhead
- Users will primarily use this for creative sound design, not surgical processing

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| GranularEngine | `dsp/include/krate/dsp/systems/granular_engine.h` | Foundation - extend or compose with this engine |
| SVF | `dsp/include/krate/dsp/primitives/svf.h` | Direct reuse - per-grain filter instance |
| Grain (struct) | `dsp/include/krate/dsp/primitives/grain_pool.h` | Extend with filter cutoff field |
| GrainPool | `dsp/include/krate/dsp/primitives/grain_pool.h` | Reuse for grain lifecycle management |
| GrainScheduler | `dsp/include/krate/dsp/processors/grain_scheduler.h` | Reuse for grain triggering |
| GrainProcessor | `dsp/include/krate/dsp/processors/grain_processor.h` | Extend to include filter processing |
| GrainParams (struct) | `dsp/include/krate/dsp/processors/grain_processor.h` | Extend with filter parameters |
| GrainEnvelope | `dsp/include/krate/dsp/core/grain_envelope.h` | Reuse for grain envelopes |
| Xorshift32 | `dsp/include/krate/dsp/core/random.h` | Reuse for cutoff randomization |
| DelayLine | `dsp/include/krate/dsp/primitives/delay_line.h` | Reuse for grain buffer |
| OnePoleSmoother | `dsp/include/krate/dsp/primitives/smoother.h` | Reuse for parameter smoothing |

**Initial codebase search for key terms:**

```bash
# Run these searches to identify existing implementations
grep -r "GranularFilter" dsp/ plugins/
grep -r "per.*grain.*filter" dsp/ plugins/
grep -r "FilteredGrain" dsp/ plugins/
```

**Search Results Summary**: No existing GranularFilter implementation found. The GranularEngine exists and can be extended/composed.

### Forward Reusability Consideration

*Note for planning phase: When this is a Layer 2+ feature, consider what new code might be reusable by sibling features at the same layer. The `/speckit.plan` phase will analyze this in detail, but early identification helps.*

**Sibling features at same layer** (if known):
- TimeVaryingCombBank (Layer 3) - may share grain-based modulation patterns
- FilterFeedbackMatrix (Layer 3) - may share multi-filter management patterns
- VowelSequencer (Layer 3) - different application but similar filter-per-voice concept

**Potential shared components** (preliminary, refined in plan.md):
- The pattern of "per-voice filter instance with randomization" could be extracted as a reusable template
- Filter parameter interpolation during grain lifetime could be shared with other modulating filters

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*Fill this table when claiming completion. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | | |
| FR-002 | | |
| FR-003 | | |
| FR-004 | | |
| FR-005 | | |
| FR-006 | | |
| FR-007 | | |
| FR-008 | | |
| FR-009 | | |
| FR-010 | | |
| FR-011 | | |
| FR-012 | | |
| FR-013 | | |
| FR-014 | | |
| FR-015 | | |
| SC-001 | | |
| SC-002 | | |
| SC-003 | | |
| SC-004 | | |
| SC-005 | | |
| SC-006 | | |
| SC-007 | | |

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
