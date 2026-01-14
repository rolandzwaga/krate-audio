# Feature Specification: AmpChannel

**Feature Branch**: `065-amp-channel`
**Created**: 2026-01-14
**Status**: Draft
**Input**: User description: "AmpChannel - Layer 3 system component that models a complete guitar amp channel with multiple gain stages. It composes TubeStage processors with tone stack, gain staging, and optional oversampling."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Basic Amp Channel Processing (Priority: P1)

A DSP developer wants to apply guitar amplifier saturation to an audio signal with controllable gain staging. They create an AmpChannel, configure the preamp and poweramp gains, and process audio to get warm tube-like distortion with musical harmonics.

**Why this priority**: This is the core functionality - without gain staging and tube saturation, the component has no value. This enables the fundamental use case of guitar amp modeling.

**Independent Test**: Can be fully tested by processing a sine wave through the amp channel with varying gain settings and verifying harmonic content and saturation characteristics.

**Acceptance Scenarios**:

1. **Given** an AmpChannel prepared at 44100 Hz, **When** processing a 1kHz sine wave with preamp gain at +12dB, **Then** output contains odd harmonics (3rd, 5th) characteristic of tube saturation
2. **Given** an AmpChannel with all gains at 0dB (conservative neutral defaults), **When** processing audio, **Then** output is unity gain with minimal coloration (bypass-like behavior)
3. **Given** an AmpChannel with preamp and poweramp both at +12dB, **When** processing audio, **Then** output shows increased harmonic richness compared to single-stage saturation

---

### User Story 2 - Tone Stack Shaping (Priority: P2)

A DSP developer wants to shape the tonal character of the amp channel using bass, mid, treble, and presence controls. They adjust the tone stack parameters to dial in their desired EQ curve either before or after the distortion stages.

**Why this priority**: Tone shaping is essential for usable amp modeling - raw distortion without tonal control is rarely musically useful. This delivers the classic amp EQ interaction.

**Independent Test**: Can be tested by measuring frequency response with different tone stack settings and verifying expected EQ curves.

**Acceptance Scenarios**:

1. **Given** an AmpChannel with bass at maximum and treble at minimum, **When** measuring frequency response, **Then** low frequencies are boosted relative to high frequencies
2. **Given** an AmpChannel with tone stack in pre-distortion position, **When** boosting treble and processing a signal, **Then** high frequencies are driven harder into saturation
3. **Given** an AmpChannel with tone stack in post-distortion position, **When** boosting treble, **Then** high frequencies are boosted after saturation (brighter but not more distorted)
4. **Given** an AmpChannel with Baxandall-style tone stack, **When** adjusting bass/mid/treble independently, **Then** each band affects its target frequency range without significant interaction (independent control)

---

### User Story 3 - Oversampling for Anti-Aliasing (Priority: P3)

A DSP developer wants high-quality output without aliasing artifacts. They enable 2x or 4x oversampling on the amp channel to reduce harmonic aliasing from the nonlinear distortion stages.

**Why this priority**: Anti-aliasing is important for professional quality but the amp channel is functional without it. This is an enhancement for users who need higher fidelity.

**Independent Test**: Can be tested by processing high-frequency content and measuring aliased energy with different oversampling factors.

**Acceptance Scenarios**:

1. **Given** an AmpChannel with 1x oversampling (disabled), **When** processing high-frequency content with heavy distortion, **Then** aliased harmonics may be present
2. **Given** an AmpChannel with 4x oversampling enabled, **When** processing the same content, **Then** aliased harmonics are significantly reduced
3. **Given** an AmpChannel with 2x oversampling, **When** querying latency, **Then** latency reflects oversampler latency contribution
4. **Given** an AmpChannel with oversampling factor changed from 2x to 4x, **When** reset() or prepare() is called, **Then** the new oversampling factor takes effect

---

### User Story 4 - Bright Cap Character (Priority: P4)

A DSP developer wants to add high-frequency emphasis characteristic of bright-cap circuits in vintage amps. They enable the bright cap switch to get a brighter, more cutting tone especially at lower gain settings.

**Why this priority**: This is a character enhancement that adds authenticity but is not essential for core functionality.

**Independent Test**: Can be tested by comparing frequency response with bright cap on vs off at varying input gain settings.

**Acceptance Scenarios**:

1. **Given** an AmpChannel with bright cap enabled and input gain at -24dB, **When** measuring frequency response, **Then** high frequencies show +6dB boost relative to bright cap disabled
2. **Given** an AmpChannel with bright cap enabled and input gain at +12dB, **When** measuring frequency response, **Then** bright cap effect is fully attenuated (0dB boost)
3. **Given** an AmpChannel with bright cap enabled and input gain at -6dB, **When** measuring frequency response, **Then** bright cap boost is approximately +3dB (linear interpolation between extremes)

---

### User Story 5 - Configurable Preamp Stages (Priority: P2)

A DSP developer wants to control the character of the amp by selecting the number of active preamp stages. Fewer stages provide cleaner, more open tones while more stages add complexity and sustain.

**Why this priority**: Stage count significantly affects the amp's character and is essential for modeling different amp types (e.g., 1-stage Fender vs 3-stage high-gain amps).

**Independent Test**: Can be tested by measuring harmonic content and saturation characteristics with different stage counts.

**Acceptance Scenarios**:

1. **Given** an AmpChannel with 1 preamp stage, **When** processing audio with +12dB gain, **Then** output has simpler harmonic content compared to 3 stages
2. **Given** an AmpChannel with 3 preamp stages, **When** processing audio with moderate gain, **Then** output shows richer harmonic complexity and more compression
3. **Given** an AmpChannel, **When** calling setPreampStages(2), **Then** exactly 2 preamp stages are active in the signal chain

---

### Edge Cases

- What happens when all gains are at minimum (-24dB each)? The signal should be heavily attenuated but still processed correctly without artifacts.
- What happens when all gains are at maximum (+24dB each)? The signal should be heavily saturated but remain bounded (no clipping to infinity) due to soft saturation.
- What happens when oversampling factor is changed during processing? The change is deferred until reset() or prepare() is called to take effect safely.
- What happens with DC offset in the input? DC is blocked between internal stages (inter-stage DC blocking) to prevent accumulation; input DC blocking is NOT provided as users may intentionally apply DC for asymmetric distortion.
- What happens at extreme sample rates (192kHz)? The system should scale coefficients appropriately.
- What happens when preamp stage count is changed during processing? Change takes effect immediately with parameter smoothing to prevent clicks.

## Requirements *(mandatory)*

### Functional Requirements

#### Lifecycle

- **FR-001**: System MUST provide `prepare(double sampleRate, size_t maxBlockSize)` to configure for processing
- **FR-002**: System MUST provide `reset()` to clear all internal state without reallocation
- **FR-003**: System MUST be real-time safe after `prepare()` - no allocations in `process()`

#### Gain Staging

- **FR-004**: System MUST provide `setInputGain(float dB)` with range [-24, +24] dB, defaulting to 0dB
- **FR-005**: System MUST provide `setPreampGain(float dB)` with range [-24, +24] dB for preamp stage drive, defaulting to 0dB
- **FR-006**: System MUST provide `setPowerampGain(float dB)` with range [-24, +24] dB for poweramp stage drive, defaulting to 0dB
- **FR-007**: System MUST provide `setMasterVolume(float dB)` with range [-60, +6] dB for final output level, defaulting to 0dB
- **FR-008**: System MUST apply parameter smoothing to all gain parameters to prevent clicks (5ms default)

#### Preamp Stage Configuration

- **FR-009**: System MUST provide `setPreampStages(int count)` with range [1, 3] to select the number of active preamp stages at runtime. Stage count changes apply immediately with parameter smoothing to prevent clicks.
- **FR-010**: System MUST compose up to 3 TubeStage processors for preamp stages plus 1 for poweramp stage
- **FR-011**: System MUST route signal through stages in order: input gain -> preamp stage(s) -> tone stack -> poweramp -> master volume
- **FR-012**: System MUST include DC blocking between tube stages to prevent DC accumulation
- **FR-013**: System MUST default to 2 preamp stages as a balanced starting point

#### Tone Stack

- **FR-014**: System MUST provide `setToneStackPosition(ToneStackPosition pos)` with options Pre (before distortion) and Post (after distortion)
- **FR-015**: System MUST provide `setBass(float value)` with range [0, 1], defaulting to 0.5 (neutral)
- **FR-016**: System MUST provide `setMid(float value)` with range [0, 1], defaulting to 0.5 (neutral)
- **FR-017**: System MUST provide `setTreble(float value)` with range [0, 1], defaulting to 0.5 (neutral)
- **FR-018**: System MUST provide `setPresence(float value)` with range [0, 1], defaulting to 0.5 (neutral). Note: Presence provides +/-6dB range vs +/-12dB for bass/mid/treble.
- **FR-019**: System MUST implement Baxandall-style tone stack with independent bass/treble shelving filters plus parametric mid control
- **FR-020**: Bass and treble shelves MUST operate independently without significant interaction (Baxandall characteristic)
- **FR-021**: Mid control MUST be a parametric (peaking) filter for precise midrange shaping

#### Character Controls

- **FR-022**: System MUST provide `setBrightCap(bool enabled)` to enable/disable high-frequency boost
- **FR-023**: Bright cap MUST provide +6dB boost at 3kHz when input gain is at -24dB
- **FR-024**: Bright cap boost MUST linearly attenuate to 0dB as input gain increases from -24dB to +12dB
- **FR-025**: At input gain >= +12dB, bright cap MUST have no audible effect (0dB boost)

#### Oversampling

- **FR-026**: System MUST provide `setOversamplingFactor(int factor)` accepting values 1, 2, or 4
- **FR-027**: Oversampling factor changes MUST be deferred until `reset()` or `prepare()` is called to take effect
- **FR-028**: System MUST use existing Oversampler primitive for anti-aliasing
- **FR-029**: System MUST report processing latency via `getLatency()` reflecting oversampler contribution
- **FR-030**: Factor 1 MUST bypass oversampling entirely for maximum efficiency

#### Processing

- **FR-031**: System MUST provide `process(float* buffer, size_t n) noexcept` for mono in-place processing
- **FR-032**: System MUST handle `n=0` gracefully (no-op)
- **FR-033**: System MUST handle `nullptr` buffer gracefully (no-op)
- **FR-034**: System MUST clamp output to prevent numerical overflow (soft limiting via tube saturation)

#### Getters

- **FR-035**: System MUST provide getters for all settable parameters
- **FR-036**: System MUST provide `getLatency() const noexcept` returning latency in samples
- **FR-037**: System MUST provide `getPreampStages() const noexcept` returning the current active stage count

### Key Entities

- **AmpChannel**: The main Layer 3 system class composing all components
- **TubeStage**: Existing Layer 2 processor used for preamp and poweramp stages (from `processors/tube_stage.h`)
- **ToneStackPosition**: Enumeration for tone stack placement (Pre, Post)
- **Oversampler**: Existing Layer 1 primitive for anti-aliasing (from `primitives/oversampler.h`)
- **Biquad**: Existing Layer 1 primitive for tone stack implementation - Baxandall-style shelving filters (from `primitives/biquad.h`)

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Processing a 1kHz sine wave at -12dBFS input level with +12dB preamp gain produces measurable 3rd harmonic content (THD > 1%)
- **SC-002**: All parameter changes complete smoothing within 10ms without audible clicks (5ms default smoothing time, 10ms upper bound for verification)
- **SC-003**: With 4x oversampling, aliased harmonic energy is reduced by at least 40dB compared to 1x
- **SC-004**: Full gain staging path (input -> preamp stages -> poweramp -> master) processes 512 samples in under 0.5ms at 44.1kHz on modern x86_64 CPU (e.g., Intel i5-8600K or equivalent)
- **SC-005**: Signal path remains stable (no runaway feedback or DC drift) after processing 10 seconds of continuous audio
- **SC-006**: Tone stack bass/mid/treble controls provide at least +/-12dB of adjustment at their respective frequency bands with independent operation (Baxandall characteristic: adjusting bass affects treble by <2dB and vice versa)
- **SC-007**: Bright cap produces +6dB high-frequency boost at 3kHz when enabled at -24dB input gain, linearly decreasing to 0dB at +12dB input gain
- **SC-008**: System operates correctly at sample rates from 44.1kHz to 192kHz
- **SC-009**: Default parameter values produce unity-gain, neutral-tone output: Input 0dB, Preamp 0dB, Poweramp 0dB, Master 0dB, all tone controls at 0.5
- **SC-010**: Changing oversampling factor only takes effect after reset() or prepare() is called (deferred application verified)
- **SC-011**: Configurable preamp stages (1-3) produce measurably different harmonic complexity at equal gain settings

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- TubeStage processor (spec 059) is complete and working
- Users understand gain staging concepts and will set reasonable gain values
- Mono processing is sufficient for initial implementation (stereo can be achieved by instantiating two channels)
- Default tone stack position will be Post (after distortion) as this is most common in guitar amps
- Bright cap implementation will use a simple high-shelf filter with gain-dependent attenuation
- Baxandall tone stack chosen for its independent bass/treble control, matching modern amp designs

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| TubeStage | `dsp/include/krate/dsp/processors/tube_stage.h` | Direct reuse - compose up to 3 instances for preamp + 1 for poweramp |
| Oversampler | `dsp/include/krate/dsp/primitives/oversampler.h` | Direct reuse - wrap processing for anti-aliasing |
| Biquad | `dsp/include/krate/dsp/primitives/biquad.h` | Direct reuse for Baxandall-style tone stack (shelving + parametric filters) |
| BiquadCascade | `dsp/include/krate/dsp/primitives/biquad.h` | Could use for steeper tone stack filters |
| OnePoleSmoother | `dsp/include/krate/dsp/primitives/smoother.h` | Direct reuse for parameter smoothing |
| DCBlocker | `dsp/include/krate/dsp/primitives/dc_blocker.h` | Direct reuse between tube stages |
| dbToGain | `dsp/include/krate/dsp/core/db_utils.h` | Direct reuse for gain conversions |

**Initial codebase search for key terms:**

```bash
# Run these searches to identify existing implementations
grep -r "AmpChannel" dsp/ plugins/      # No existing implementations found
grep -r "ToneStack" dsp/ plugins/       # Referenced in DST-ROADMAP only
grep -r "BrightCap" dsp/ plugins/       # No existing implementations found
```

**Search Results Summary**: No existing AmpChannel implementation. TubeStage is complete and ready for composition. Tone stack will need to be implemented using existing Biquad components with Baxandall-style independent bass/treble shelves plus parametric mid. Bright cap will be new functionality with gain-dependent attenuation.

### Forward Reusability Consideration

*Note for planning phase: When this is a Layer 2+ feature, consider what new code might be reusable by sibling features at the same layer. The `/speckit.plan` phase will analyze this in detail, but early identification helps.*

**Sibling features at same layer** (if known):
- TapeMachine (spec in DST-ROADMAP) - may share gain staging patterns
- FuzzPedal (spec in DST-ROADMAP) - may share tone stack implementation

**Potential shared components** (preliminary, refined in plan.md):
- Baxandall tone stack implementation (bass/mid/treble/presence) could be extracted as a reusable ToneStack class for use in TapeMachine and other amp-like systems
- Bright cap filter with gain-dependent attenuation could be a reusable BrightCapFilter primitive if other systems need similar functionality
- Gain staging pattern (input -> stages -> master) could inform a base class or template for other multi-stage systems

## Clarifications

### Session 2026-01-14

- Q: What circuit style should the tone stack use? → A: Baxandall-style with independent bass/treble shelves + parametric mid (Option B)
- Q: How many preamp stages should be available? → A: Configurable: 1-3 preamp stages selectable at runtime (Option B)
- Q: When should oversampling factor changes take effect? → A: Deferred: Factor change requires reset() or prepare() to take effect (Option B)
- Q: What should the default parameter values be? → A: Conservative neutral defaults: Input 0dB, Preamp 0dB, Poweramp 0dB, Master 0dB, Tones all at 0.5 (Option B)
- Q: How should bright cap boost behave with respect to gain? → A: Gradual attenuation: +6dB boost at -24dB input gain, linearly decreasing to 0dB boost at +12dB input gain (Option B)

## Out of Scope

- Stereo processing (users instantiate two channels)
- Cabinet/speaker simulation (separate component)
- Reverb/effects loop
- Sag/power supply modeling
- Negative feedback loop modeling

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
| FR-035 | | |
| FR-036 | | |
| FR-037 | | |
| SC-001 | | |
| SC-002 | | |
| SC-003 | | |
| SC-004 | | |
| SC-005 | | |
| SC-006 | | |
| SC-007 | | |
| SC-008 | | |
| SC-009 | | |
| SC-010 | | |
| SC-011 | | |

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
