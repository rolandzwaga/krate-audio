# Feature Specification: TubeStage Processor

**Feature Branch**: `059-tube-stage`
**Created**: 2026-01-13
**Status**: Draft
**Input**: User description: "TubeStage Layer 2 processor modeling a single triode gain stage with configurable drive, bias, and saturation for warm, musical tube saturation."

## Overview

This specification defines a TubeStage processor for the KrateDSP library. The TubeStage models a single triode tube gain stage, providing warm, musical saturation with configurable input drive, output gain, bias (asymmetry), and saturation amount. It composes Layer 1 primitives (Waveshaper, DCBlocker, OnePoleSmoother) into a cohesive gain stage module.

**Layer**: 2 (Processors)
**Location**: `dsp/include/krate/dsp/processors/tube_stage.h`
**Test**: `dsp/tests/unit/processors/tube_stage_test.cpp`
**Namespace**: `Krate::DSP`

### Motivation

The DST-ROADMAP identifies TubeStage as the first Layer 2 processor (item #11 in Priority 3), describing it as "Tube gain stage" that "models a single triode gain stage."

A TubeStage processor provides:
- Warm, musical saturation characteristic of tube amplifiers
- Input gain (drive) to control how hard the tube is pushed
- Output gain for makeup gain after saturation
- Bias control to adjust the tube's operating point (affecting asymmetry and even harmonics)
- Saturation amount to blend between clean and saturated signal
- Automatic DC blocking to remove offset from asymmetric saturation
- Parameter smoothing to prevent zipper noise during automation

**Design Principles** (per DST-ROADMAP):
- No internal oversampling (handled externally per user preference)
- Automatic DC blocking after saturation (asymmetric saturation introduces DC)
- Composes Layer 1 primitives only (Waveshaper, DCBlocker, OnePoleSmoother)
- Real-time safe processing with no allocations in process()

## User Scenarios & Testing *(mandatory)*

### User Story 1 - DSP Developer Applies Tube Saturation to Audio (Priority: P1)

A DSP developer building a guitar amp or tape emulation needs to add warm, musical saturation to audio signals. They use the TubeStage processor, configure drive and output levels, and process audio blocks.

**Why this priority**: This is the core value proposition - providing a ready-to-use tube gain stage that delivers musical saturation.

**Independent Test**: Can be fully tested by processing audio through the TubeStage and verifying the output exhibits expected harmonic content (even harmonics from asymmetric saturation).

**Acceptance Scenarios**:

1. **Given** a TubeStage prepared at 44.1 kHz, **When** processing a 1 kHz sine wave with input gain +12 dB, **Then** the output contains even harmonics (2nd, 4th) characteristic of tube saturation.

2. **Given** a TubeStage with default settings, **When** processing audio, **Then** the output is warmer (more even harmonics) than the input.

3. **Given** a TubeStage, **When** calling process() on a buffer, **Then** no memory allocation occurs during processing.

---

### User Story 2 - DSP Developer Controls Saturation Intensity via Input Gain (Priority: P1)

A DSP developer needs to control how hard the tube stage is driven. Low input gain produces subtle warmth, while high input gain produces aggressive saturation. They use setInputGain() to control the drive level.

**Why this priority**: Drive control is essential for any saturation effect - it determines the character and intensity of the distortion.

**Independent Test**: Can be tested by sweeping input gain and verifying output transitions from clean to saturated.

**Acceptance Scenarios**:

1. **Given** a TubeStage with input gain 0 dB, **When** processing a sine wave at 0.5 amplitude, **Then** output shows minimal saturation (mostly linear).

2. **Given** a TubeStage with input gain +24 dB, **When** processing a sine wave at 0.5 amplitude, **Then** output shows significant harmonic distortion with visible waveform clipping.

3. **Given** a TubeStage, **When** setting input gain outside [-24, +24] dB range, **Then** the value is clamped to valid range.

---

### User Story 3 - DSP Developer Adjusts Tube Bias for Harmonic Character (Priority: P2)

A DSP developer wants to adjust the tube's operating point to change the harmonic character. Different bias points produce different ratios of even to odd harmonics. They use setBias() to control the asymmetry.

**Why this priority**: Bias control allows fine-tuning the harmonic character, but the feature works without it using the default bias.

**Independent Test**: Can be tested by varying bias and measuring even harmonic content in the output.

**Acceptance Scenarios**:

1. **Given** a TubeStage with bias 0.0 (center), **When** processing a sine wave, **Then** output has balanced even/odd harmonic content.

2. **Given** a TubeStage with bias 0.5 (shifted positive), **When** processing a sine wave, **Then** output has increased even harmonic content due to asymmetry.

3. **Given** a TubeStage with bias -0.5 (shifted negative), **When** processing a sine wave, **Then** output has asymmetric clipping in the opposite direction.

---

### User Story 4 - DSP Developer Uses Saturation Amount for Parallel Processing (Priority: P2)

A DSP developer wants to blend between clean and saturated signal for parallel saturation. They use setSaturationAmount() to control the wet/dry mix of the saturation effect.

**Why this priority**: Saturation amount (mix) enables parallel processing but the processor is fully functional at 100% wet.

**Independent Test**: Can be tested by setting saturation amount to 0.0 and verifying output equals input (bypass).

**Acceptance Scenarios**:

1. **Given** a TubeStage with saturation amount 0.0, **When** processing audio, **Then** output equals input (full bypass of saturation).

2. **Given** a TubeStage with saturation amount 1.0, **When** processing audio, **Then** output is 100% saturated signal.

3. **Given** a TubeStage with saturation amount 0.5, **When** processing audio, **Then** output is 50% dry + 50% wet blend.

---

### User Story 5 - DSP Developer Uses Output Gain for Level Matching (Priority: P2)

A DSP developer needs to compensate for level changes after saturation. Saturation can reduce peak levels, so output gain provides makeup. They use setOutputGain() to adjust the final output level.

**Why this priority**: Output gain is important for gain staging but the processor works at unity output gain.

**Independent Test**: Can be tested by measuring output level changes with different output gain settings.

**Acceptance Scenarios**:

1. **Given** a TubeStage with output gain +6 dB, **When** processing audio, **Then** output amplitude is approximately double the unsaturated output.

2. **Given** a TubeStage with output gain -6 dB, **When** processing audio, **Then** output amplitude is approximately half the unsaturated output.

---

### User Story 6 - DSP Developer Processes Audio Without Zipper Noise (Priority: P3)

A DSP developer automating tube stage parameters (input gain, output gain, saturation amount) needs smooth transitions without audible clicks or zipper noise. The processor smooths parameter changes internally.

**Why this priority**: Parameter smoothing is a quality-of-life feature; the processor works without it but sounds better with it.

**Independent Test**: Can be tested by rapidly changing parameters and verifying no discontinuities in output.

**Acceptance Scenarios**:

1. **Given** a TubeStage processing audio, **When** input gain is suddenly changed from 0 dB to +24 dB, **Then** the gain change is smoothed over approximately 5ms (no clicks).

2. **Given** a TubeStage processing audio, **When** reset() is called, **Then** smoothers snap to current values (no ramp on next process).

---

### Edge Cases

- What happens when input gain is at minimum (-24 dB)? Signal is attenuated significantly, minimal saturation occurs.
- What happens when input gain is at maximum (+24 dB)? Heavy saturation, output approaches hard clipping.
- What happens when bias is at limits (+/- 1.0)? Maximum asymmetry, output has significant DC offset (removed by DC blocker).
- What happens when saturation amount is 0? Full bypass - output equals input.
- What happens when process() is called before prepare()? Undefined behavior - prepare() must be called first.
- What happens with DC input signal? DC blocker removes it; output settles to zero.
- What happens with NaN input? NaN propagates through (real-time safety - no exception).
- What happens with very short buffers (n=0 or n=1)? Must handle gracefully without crashing.

## Requirements *(mandatory)*

### Functional Requirements

#### Lifecycle Methods

- **FR-001**: TubeStage MUST provide a `prepare(double sampleRate, size_t maxBlockSize)` method that configures the processor for the given sample rate and maximum block size.
- **FR-002**: TubeStage MUST provide a `reset()` method that clears all internal state (filter state, smoother state) without reallocation.
- **FR-003**: TubeStage MUST have a default constructor that initializes parameters to safe defaults: input gain 0 dB, output gain 0 dB, bias 0.0, saturation amount 1.0.

#### Parameter Setters

- **FR-004**: TubeStage MUST provide `void setInputGain(float dB) noexcept` to set the pre-saturation drive level.
- **FR-005**: Input gain MUST be clamped to range [-24.0, +24.0] dB.
- **FR-006**: TubeStage MUST provide `void setOutputGain(float dB) noexcept` to set the post-saturation makeup gain.
- **FR-007**: Output gain MUST be clamped to range [-24.0, +24.0] dB.
- **FR-008**: TubeStage MUST provide `void setBias(float bias) noexcept` to set the tube operating point.
- **FR-009**: Bias MUST be clamped to range [-1.0, +1.0].
- **FR-010**: TubeStage MUST provide `void setSaturationAmount(float amount) noexcept` to set the wet/dry mix.
- **FR-011**: Saturation amount MUST be clamped to range [0.0, 1.0].

#### Getter Methods

- **FR-012**: TubeStage MUST provide `[[nodiscard]] float getInputGain() const noexcept` returning input gain in dB.
- **FR-013**: TubeStage MUST provide `[[nodiscard]] float getOutputGain() const noexcept` returning output gain in dB.
- **FR-014**: TubeStage MUST provide `[[nodiscard]] float getBias() const noexcept` returning bias value.
- **FR-015**: TubeStage MUST provide `[[nodiscard]] float getSaturationAmount() const noexcept` returning saturation amount.

#### Processing

- **FR-016**: TubeStage MUST provide `void process(float* buffer, size_t numSamples) noexcept` for in-place block processing.
- **FR-017**: `process()` MUST apply the following signal chain: input gain -> Waveshaper (Tube type with bias) -> DC blocking -> output gain (applied to wet signal only) -> saturation mix blend with dry input.
- **FR-018**: `process()` MUST NOT allocate memory during processing.
- **FR-019**: `process()` MUST handle n=0 gracefully (no-op).
- **FR-020**: When saturation amount is 0.0, `process()` MUST skip both waveshaper AND DC blocker entirely (full bypass - output equals input exactly).

#### Parameter Smoothing

- **FR-021**: Input gain changes MUST be smoothed to prevent clicks (approximately 5ms smoothing time).
- **FR-022**: Output gain changes MUST be smoothed to prevent clicks.
- **FR-023**: Saturation amount changes MUST be smoothed to prevent clicks.
- **FR-024**: Bias changes do NOT require smoothing (changes are applied per-block to Waveshaper asymmetry via 1:1 mapping: bias value directly sets Waveshaper asymmetry parameter).
- **FR-025**: `reset()` MUST snap smoothers to current target values (no ramp on next process).

#### DC Blocking

- **FR-026**: TubeStage MUST apply DC blocking after waveshaping to remove DC offset introduced by asymmetric saturation.
- **FR-027**: DC blocker cutoff frequency MUST be approximately 10 Hz (standard for DC blocking).

#### Component Composition

- **FR-028**: TubeStage MUST use `Waveshaper` (Layer 1) for the saturation curve.
- **FR-029**: TubeStage MUST use `DCBlocker` (Layer 1) for DC offset removal.
- **FR-030**: TubeStage MUST use `OnePoleSmoother` (Layer 1) for parameter smoothing.
- **FR-031**: Waveshaper MUST be configured with `WaveshapeType::Tube` for characteristic tube saturation curve.

#### Architecture & Quality

- **FR-032**: TubeStage MUST be a header-only implementation in `dsp/include/krate/dsp/processors/tube_stage.h`.
- **FR-033**: TubeStage MUST be in namespace `Krate::DSP`.
- **FR-034**: TubeStage MUST only depend on Layer 0 and Layer 1 components (Layer 2 constraint).
- **FR-035**: TubeStage MUST include Doxygen documentation for the class and all public methods.
- **FR-036**: TubeStage MUST follow the established naming conventions (trailing underscore for members, PascalCase for class, camelCase for methods).

### Key Entities

- **TubeStage**: The main processor class providing a complete tube gain stage.
- **Input Gain**: Pre-saturation amplification in dB, controls how hard the tube is driven.
- **Output Gain**: Post-saturation amplification in dB, provides makeup gain.
- **Bias**: Tube operating point [-1, 1], controls asymmetry and even harmonic content.
- **Saturation Amount**: Wet/dry mix [0, 1], enables parallel saturation.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Processing a 1 kHz sine wave with input gain +12 dB produces measurable 2nd harmonic content (at least -30 dB relative to fundamental).
- **SC-002**: Input gain of +24 dB produces THD (Total Harmonic Distortion) greater than 5% for a 0.5 amplitude sine wave.
- **SC-003**: Saturation amount of 0.0 produces output identical to input (bypass - relative error < 1e-6).
- **SC-004**: DC blocker removes DC offset: a constant DC input signal decays to less than 1% within 500ms.
- **SC-005**: Processing 1 million samples produces no unexpected NaN or Infinity outputs when given valid inputs in [-1, 1] range.
- **SC-006**: A 512-sample buffer is processed in under 100 microseconds at 44.1kHz (< 0.5% CPU budget for Layer 2).
- **SC-007**: Unit test coverage reaches 100% of all public methods including edge cases.
- **SC-008**: Parameter changes during processing produce no audible clicks (verified by checking for discontinuities > 0.01 in output).

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Target platforms support IEEE 754 floating-point arithmetic.
- Users include appropriate headers and link against the KrateDSP library.
- C++20 is available for modern language features.
- `prepare()` is called before any processing occurs; behavior is undefined if not.
- Typical audio sample rates are 44100 Hz to 192000 Hz.
- Users understand that oversampling should be applied externally for anti-aliasing if needed.
- Bias changes can be applied per-block without smoothing (changes typically come from static settings, not automation).

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that MUST be reused:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| `Waveshaper` | `primitives/waveshaper.h` | MUST REUSE - provides Tube saturation curve via WaveshapeType::Tube |
| `DCBlocker` | `primitives/dc_blocker.h` | MUST REUSE - provides lightweight DC offset removal |
| `OnePoleSmoother` | `primitives/smoother.h` | MUST REUSE - provides parameter smoothing |
| `dbToGain()` | `core/db_utils.h` | MUST REUSE - dB to linear gain conversion |
| `gainToDb()` | `core/db_utils.h` | MAY REUSE - linear gain to dB conversion |
| `SaturationProcessor` | `processors/saturation_processor.h` | REFERENCE - existing Layer 2 processor pattern |

**Initial codebase search for key terms:**

```bash
# Run these searches to identify existing implementations
grep -r "TubeStage\|tube_stage\|tubeStage" dsp/ plugins/
grep -r "class.*Stage" dsp/ plugins/
```

**Search Results Summary**: No existing TubeStage class found. References to TubeStage exist only in planning documents (DST-ROADMAP.md) and previous spec files that mention TubeStage as a future Layer 2 processor.

### Forward Reusability Consideration

**Sibling features at same layer** (Layer 2 processors from DST-ROADMAP):
- `processors/diode_clipper.h` - Will share DC blocking pattern and gain staging structure
- `processors/fuzz_processor.h` - Will share bias control and DC blocking
- `processors/tape_saturator.h` - Will share gain staging and parameter smoothing patterns
- `processors/wavefolder_processor.h` - Will share DC blocking after nonlinear processing

**Potential shared components** (preliminary, refined in plan.md):
- The input gain -> waveshaper -> DC block -> output gain -> mix pattern may be extracted as a common processor base
- Parameter smoothing configuration (5ms, 3 smoothers) is consistent with SaturationProcessor
- Gain clamping range [-24, +24] dB is consistent with SaturationProcessor

## Out of Scope

- Internal oversampling (handled externally per DST-ROADMAP design principle)
- Internal ADAA/aliasing mitigation (use external oversampling if aliasing is a concern)
- Multi-channel/stereo variants (users create separate instances per channel)
- Grid voltage modeling (advanced tube physics)
- Plate voltage sag modeling (dynamic response)
- Input/output impedance modeling (frequency-dependent loading)
- Multiple tube stages in cascade (users chain multiple TubeStage instances)
- Double-precision overloads (can be added later if needed)
- SIMD/vectorized implementations (can be added later as optimization)

## Clarifications

### Session 2026-01-13

- Q: Bias-to-Asymmetry Mapping: How should TubeStage bias parameter map to Waveshaper asymmetry? → A: Bias maps directly (1:1) to Waveshaper asymmetry; `bias=0.5` sets `asymmetry=0.5`
- Q: Aliasing Mitigation Strategy: How should TubeStage handle aliasing from nonlinear waveshaping? → A: Use existing Waveshaper (WaveshapeType::Tube) with no internal ADAA; aliasing mitigation is deferred to external handling (user applies oversampling externally if needed)
- Q: Bypass Scope at Saturation Amount 0.0: What should TubeStage bypass when saturation amount is 0.0? → A: Full bypass - skip both waveshaper AND DC blocker; output equals input exactly
- Q: Output Gain Application Point: Where should output gain be applied in the signal chain? → A: Output gain applies to wet signal only (before dry/wet blend)

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
| SC-001 | | |
| SC-002 | | |
| SC-003 | | |
| SC-004 | | |
| SC-005 | | |
| SC-006 | | |
| SC-007 | | |
| SC-008 | | |

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
