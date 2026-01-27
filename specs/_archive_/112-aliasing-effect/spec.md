# Feature Specification: AliasingEffect

**Feature Branch**: `112-aliasing-effect`
**Created**: 2026-01-27
**Status**: Draft
**Input**: User description: "AliasingEffect - intentional aliasing processor with downsample, frequency shift, and band isolation for digital grunge aesthetic. Based on DST-ROADMAP.md section 8.2."

## Clarifications

### Session 2026-01-27

- Q: Band filter steepness - should the bandpass filter use single-stage (12dB/oct) or two-stage cascade (24dB/oct) for aliasing band isolation? → A: Two-stage cascade (24dB/oct) for cleaner band separation
- Q: Band filter recombination - should the non-band signal be phase-compensated before summing with processed band signal to avoid comb filtering? → A: Simple summing without phase compensation (phase artifacts contribute to digital destruction aesthetic)
- Q: SampleRateReducer extension strategy - should AliasingEffect extend the existing primitive's max factor from 8 to 32, or implement internal sample-and-hold logic? → A: Extend SampleRateReducer's kMaxReductionFactor from 8 to 32 (benefits other lo-fi effects, maintains single responsibility)
- Q: FrequencyShifter configuration - which parameters should be exposed (direction, feedback, LFO modulation)? → A: Fixed configuration (Direction=Up, Feedback=0, Modulation off with ModDepth=0, Mix=1.0) - expose only shift amount to avoid overwhelming complexity
- Q: Stereo processing - should AliasingEffect support native stereo mode or remain strictly mono? → A: Remain strictly mono (users instantiate two instances for independent L/R control, more creative flexibility)

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Basic Intentional Aliasing (Priority: P1)

A sound designer wants to add digital grunge character to their audio by intentionally folding high frequencies back into the audible range. They load the AliasingEffect, set the downsample factor to 8 (no anti-aliasing filter), and process bright audio material. The result exhibits classic digital aliasing artifacts where frequencies above the reduced Nyquist fold back, creating inharmonic, metallic tones characteristic of early digital samplers.

**Why this priority**: This is the core use case - intentional aliasing without anti-aliasing filtering is the fundamental value proposition. Without this working, the processor has no purpose.

**Independent Test**: Can be fully tested by processing audio with frequencies above the reduced Nyquist and verifying that aliased content appears in the spectrum.

**Acceptance Scenarios**:

1. **Given** AliasingEffect with downsample factor 8 at 44100Hz (reduced Nyquist = 2756Hz), **When** a 5000Hz sine wave is processed, **Then** the output contains energy at the aliased frequency (2756 - (5000 - 2756) = 512Hz approximately).
2. **Given** AliasingEffect with downsample factor 2, **When** compared to factor 16, **Then** factor 16 produces more severe aliasing artifacts (more frequencies folded back).
3. **Given** mix at 0.5, **When** audio is processed, **Then** output is 50% dry signal blended with 50% aliased signal.

---

### User Story 2 - Band-Isolated Aliasing (Priority: P1)

A producer wants to add aliasing artifacts only to specific frequency ranges while leaving other frequencies clean. They set the aliasing band to 2000Hz-8000Hz, so only content in that range gets processed through the aliaser. Low frequencies pass through unaffected, and only the mid-high range exhibits digital grunge.

**Why this priority**: Band isolation is key to making aliasing musically useful. Without it, the effect destroys too much of the signal. This is what distinguishes the AliasingEffect from a simple bitcrusher.

**Independent Test**: Can be fully tested by processing broadband audio and verifying that frequencies outside the aliasing band remain unaffected while the band content shows aliasing artifacts.

**Acceptance Scenarios**:

1. **Given** AliasingEffect with aliasing band [2000Hz, 8000Hz], **When** a 500Hz sine is processed, **Then** the output shows no aliasing artifacts (passes through clean).
2. **Given** AliasingEffect with aliasing band [2000Hz, 8000Hz], **When** a 4000Hz sine is processed with downsample factor 4, **Then** the output shows aliasing artifacts.
3. **Given** aliasing band [1000Hz, 5000Hz] vs [5000Hz, 10000Hz], **When** the same broadband input is processed, **Then** different frequency regions exhibit aliasing.

---

### User Story 3 - Frequency Shift Before Downsample (Priority: P2)

A sound designer wants more control over which frequencies alias and where they fold to. By applying frequency shift before downsampling, they can move spectral content into different aliasing relationships. A +500Hz shift moves all frequencies up before aliasing, causing different folding patterns than a -500Hz shift.

**Why this priority**: Frequency shift provides creative control over the aliasing character. It allows predictable manipulation of where harmonics fold to, expanding the sonic palette significantly.

**Independent Test**: Can be fully tested by comparing aliased output with positive vs negative frequency shift on the same input and verifying different spectral results.

**Acceptance Scenarios**:

1. **Given** AliasingEffect with +500Hz frequency shift and downsample factor 8, **When** a 2000Hz sine is processed, **Then** the aliased frequency differs from processing without shift.
2. **Given** frequency shift of +1000Hz vs -1000Hz, **When** the same input is processed, **Then** the aliasing artifacts appear at different frequencies.
3. **Given** frequency shift of 0Hz, **When** audio is processed, **Then** output matches processing without the frequency shifter active.

---

### User Story 4 - Extreme Digital Destruction (Priority: P2)

A musician producing harsh noise or glitch music wants maximum digital destruction. They set the downsample factor to 32 (maximum), apply frequency shift, and process through the full spectrum aliasing band. The result is extremely degraded audio with severe aliasing artifacts throughout.

**Why this priority**: Demonstrates the full range of the effect from subtle to extreme. Maximum settings should produce usable (if harsh) results without instability.

**Independent Test**: Can be fully tested by applying maximum settings and verifying the processor remains stable and produces extreme but bounded output.

**Acceptance Scenarios**:

1. **Given** downsample factor 32 (maximum), **When** any audio is processed, **Then** output shows severe aliasing but remains stable and bounded.
2. **Given** all parameters at extreme settings, **When** processing continues for 10 seconds, **Then** no NaN/Inf values appear and output remains bounded.
3. **Given** maximum destruction settings, **When** compared to minimal settings, **Then** the difference is dramatic and clearly audible.

---

### User Story 5 - Click-Free Parameter Automation (Priority: P3)

A live performer wants to automate aliasing parameters during performance. When they sweep the downsample factor or frequency shift, the transitions should be smooth without audible clicks or pops.

**Why this priority**: Essential for professional use in both live and studio contexts where parameter automation is common.

**Independent Test**: Can be fully tested by rapidly changing parameters during audio processing and verifying no discontinuities in output.

**Acceptance Scenarios**:

1. **Given** audio processing, **When** downsample factor changes from 2 to 16, **Then** transition is smooth with no audible clicks.
2. **Given** audio processing, **When** frequency shift sweeps from -1000Hz to +1000Hz, **Then** transition is smooth.
3. **Given** audio processing, **When** mix changes from 0% to 100%, **Then** transition is smooth.

---

### Edge Cases

- What happens when downsample factor is set to 2 (minimum)?
  - Produces mild aliasing with Nyquist at half the original sample rate.
- What happens when downsample factor is set to 32 (maximum)?
  - Produces extreme aliasing with very low effective sample rate; processor remains stable.
- What happens when aliasing band low frequency equals high frequency?
  - Band filter becomes extremely narrow notch; minimal signal passes through aliaser.
- What happens when aliasing band covers the full spectrum (20Hz-20000Hz)?
  - Equivalent to processing the entire signal; no band isolation.
- What happens when frequency shift moves content beyond Nyquist before downsample?
  - Expected behavior - contributes to aliasing effect.
- What happens with DC input (constant value)?
  - DC passes through unaffected (below aliasing band minimum).
- What happens with NaN/Inf input?
  - Processor resets internal state and returns 0.0 to prevent corruption.
- What happens when sample rate changes?
  - prepare() must be called to reconfigure; band filter frequencies re-calculate relative to new Nyquist.
- What happens with silence input?
  - Output should be silence (no noise artifacts introduced).

## Requirements *(mandatory)*

### Functional Requirements

**Lifecycle:**
- **FR-001**: System MUST provide `prepare(double sampleRate, size_t maxBlockSize)` to initialize for processing
- **FR-002**: System MUST provide `reset()` to clear all internal state without reallocation
- **FR-003**: System MUST support sample rates from 44100Hz to 192000Hz

**Downsample Control:**
- **FR-004**: System MUST provide `setDownsampleFactor(float factor)` to set the sample rate reduction factor
- **FR-005**: Downsample factor MUST be clamped to range [2.0, 32.0] (no anti-aliasing filter applied)
- **FR-006**: Downsample factor changes MUST be click-free via parameter smoothing with 10ms time constant
- **FR-007**: Downsampling MUST use sample-and-hold (no interpolation, no anti-aliasing) to maximize aliasing artifacts

**Frequency Shift Control:**
- **FR-008**: System MUST provide `setFrequencyShift(float hz)` to set pre-downsample frequency shift
- **FR-009**: Frequency shift MUST be clamped to range [-5000.0, +5000.0] Hz
- **FR-010**: Frequency shift changes MUST be click-free via parameter smoothing with 10ms time constant
- **FR-011**: Frequency shift MUST be applied before downsampling to affect aliasing patterns
- **FR-012**: Frequency shift MUST use single-sideband modulation (SSB) via Hilbert transform to avoid introducing sum/difference frequencies
- **FR-012a**: FrequencyShifter MUST be configured with fixed settings: Direction=Up, Feedback=0.0, ModDepth=0.0, Mix=1.0 (only shift amount exposed to users)

**Aliasing Band Control:**
- **FR-013**: System MUST provide `setAliasingBand(float lowHz, float highHz)` to isolate frequency range for aliasing
- **FR-014**: Aliasing band frequencies MUST be clamped to [20.0, sampleRate * 0.45] Hz
- **FR-015**: Low frequency MUST be constrained to be less than or equal to high frequency
- **FR-016**: Band filter changes MUST be click-free via parameter smoothing with 10ms time constant
- **FR-017**: Band filtering MUST use two-stage cascade bandpass topology (24dB/oct) to isolate content before aliasing
- **FR-018**: Signal outside the aliasing band MUST bypass the aliaser and recombine with processed signal via simple summing (no phase compensation)

**Mix Control:**
- **FR-019**: System MUST provide `setMix(float mix)` to control dry/wet blend
- **FR-020**: Mix MUST be clamped to range [0.0, 1.0] where 0.0 = bypass, 1.0 = full wet
- **FR-021**: Mix changes MUST be click-free via parameter smoothing with 10ms time constant
- **FR-022**: Mix formula MUST be: `output = (1 - mix) * dry + mix * wet`

**Processing:**
- **FR-023**: System MUST provide `process(float* buffer, size_t n) noexcept` for block processing
- **FR-024**: Processing MUST be real-time safe (no allocations, locks, exceptions, or I/O)
- **FR-025**: System MUST handle NaN/Inf inputs by resetting state and returning 0.0
- **FR-026**: System MUST flush denormals to prevent CPU spikes
- **FR-027**: Output values MUST remain in valid float range (no NaN/Inf output)

**Processing Chain:**
- **FR-028**: Processing order MUST be: input -> band isolation -> frequency shift -> downsample (no AA) -> recombine with non-band signal -> mix with dry
- **FR-029**: Band-isolated signal MUST be frequency-shifted before downsampling
- **FR-030**: Non-band signal MUST bypass the aliaser entirely and recombine after

**Mono Processing:**
- **FR-031**: System MUST operate in mono only (stereo processing requires two independent instances)
- **FR-032**: System MUST NOT provide native stereo mode (users instantiate separate instances for L/R channels with independent parameter control)

**Stability:**
- **FR-033**: System MUST remain stable (bounded output) for any valid parameter combination
- **FR-034**: Processing latency from frequency shifter (Hilbert transform) MUST be documented (approximately 5 samples)

### Key Entities

- **AliasingEffect**: Main processor class implementing intentional aliasing with band isolation and frequency shift
- **SampleRateReducer**: Provides sample-and-hold downsampling without anti-aliasing (reuse existing primitive with extended factor range)
- **FrequencyShifter**: Provides SSB frequency shifting before downsample (reuse existing processor)
- **Biquad**: Provides bandpass filter for aliasing band isolation (reuse existing primitive)
- **OnePoleSmoother**: Provides click-free parameter smoothing (reuse existing primitive)

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Downsample factor 8 at 44100Hz produces aliased frequency content when input exceeds 2756Hz (reduced Nyquist)
- **SC-002**: Band isolation with [2000Hz, 8000Hz] passes through 500Hz sine unaffected (less than 0.1dB attenuation outside band after accounting for any filter rolloff)
- **SC-003**: Frequency shift of +500Hz shifts input spectrum measurably before aliasing occurs
- **SC-004**: All parameter changes complete smoothly within 10ms without audible clicks or pops
- **SC-005**: Processor uses less than 0.5% CPU per instance at 44100Hz sample rate (Layer 2 processor budget)
- **SC-006**: Processing latency is approximately 5 samples (from Hilbert transform in frequency shifter)
- **SC-007**: Mix at 0% produces output identical to input (bit-exact dry signal)
- **SC-008**: With maximum downsample factor (32), output remains bounded and stable for 10 seconds of continuous processing
- **SC-009**: Band filter (two-stage cascade) provides at least 24dB/octave rolloff outside the specified aliasing band

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Users understand that intentional aliasing creates inharmonic, non-musical artifacts (this is the desired effect)
- Users will compose with other processors (filters, limiters) as needed for final mixing
- The processor operates in mono; stereo processing requires two independent instances with separate parameter control
- The frequency shifter introduces approximately 5 samples of latency due to Hilbert transform
- The SampleRateReducer's maximum factor range will be extended from 8 to 32 (modifying the existing primitive benefits other future lo-fi effects)
- Band filtering uses two-stage cascade (24dB/oct) for clean band separation
- Band and non-band signals recombine via simple summing without phase compensation (phase artifacts contribute to the digital grunge aesthetic)
- FrequencyShifter is used with fixed internal settings (Direction=Up, Feedback=0, no LFO modulation) - only shift amount is exposed to users
- Band isolation (HP+LP cascade) separates the target frequency range first; frequency shift is then applied only to the isolated band component before downsampling, allowing precise control over which frequencies get aliased and how they fold

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that MUST be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| SampleRateReducer | dsp/include/krate/dsp/primitives/sample_rate_reducer.h | Provides sample-and-hold downsampling - EXTEND kMaxReductionFactor from 8.0f to 32.0f |
| FrequencyShifter | dsp/include/krate/dsp/processors/frequency_shifter.h | Provides SSB frequency shifting via Hilbert transform - REUSE with fixed config (Direction=Up, Feedback=0, ModDepth=0, Mix=1.0) |
| Biquad | dsp/include/krate/dsp/primitives/biquad.h | Provides bandpass filter for aliasing band isolation - REUSE in two-stage cascade (24dB/oct) |
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | Provides parameter smoothing - REUSE directly |

**Initial codebase search for key terms:**

```bash
# Run these searches to identify existing implementations
grep -r "class AliasingEffect" dsp/ plugins/
grep -r "setDownsampleFactor\|setAliasingBand" dsp/ plugins/
grep -r "intentional.*aliasing\|aliasing.*effect" dsp/ plugins/
```

**Search Results Summary**: No existing AliasingEffect found. SampleRateReducer exists with factor range [1, 8] and will be extended to 32 (benefits other lo-fi effects). FrequencyShifter provides the required SSB modulation and will be used with fixed configuration (Direction=Up, Feedback=0, no LFO). Biquad provides bandpass filtering and will be used in two-stage cascade for 24dB/oct rolloff.

### Forward Reusability Consideration

*Note for planning phase: When this is a Layer 2+ feature, consider what new code might be reusable by sibling features at the same layer.*

**Sibling features at same layer** (from DST-ROADMAP.md Priority 8 - Digital Destruction):
- 111-bitwise-mangler - Bit manipulation distortion (recently completed)
- 113-granular-distortion - Per-grain variable distortion (Layer 2)
- 114-fractal-distortion - Recursive multi-scale distortion (Layer 2)

**Potential shared components** (preliminary, refined in plan.md):
- The extended SampleRateReducer (factor up to 32) could be useful for other lo-fi/destruction effects
- The band-isolation-then-process pattern could be extracted as a utility pattern for other frequency-selective effects
- The composition pattern of FrequencyShifter + SampleRateReducer demonstrates processor-to-processor composition at Layer 2

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
| FR-016 | | |
| FR-017 | | |
| FR-018 | | |
| FR-019 | | |
| FR-020 | | |
| FR-021 | | |
| FR-022 | | |
| FR-023 | | |
| FR-024 | | |
| FR-025 | | |
| FR-026 | | |
| FR-027 | | |
| FR-028 | | |
| FR-029 | | |
| FR-030 | | |
| FR-031 | | |
| FR-032 | | |
| FR-033 | | |
| FR-034 | | |
| FR-012a | | |
| SC-001 | | |
| SC-002 | | |
| SC-003 | | |
| SC-004 | | |
| SC-005 | | |
| SC-006 | | |
| SC-007 | | |
| SC-008 | | |
| SC-009 | | |

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
