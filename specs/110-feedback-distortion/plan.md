# Implementation Plan: Feedback Distortion Processor

**Branch**: `110-feedback-distortion` | **Date**: 2026-01-26 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/110-feedback-distortion/spec.md`

## Summary

Implement a Layer 2 DSP Processor that creates sustained, singing distortion through a feedback delay loop with saturation and soft limiting. The core technical approach uses:
- Existing primitives (DelayLine, Waveshaper, Biquad, DCBlocker, OnePoleSmoother) composed into a feedback loop
- EnvelopeFollower in Peak mode for fast limiter envelope tracking (0.5ms attack, 50ms release)
- Tanh-based soft clipping for the limiter
- 10ms parameter smoothing for all user-adjustable parameters
- Linear interpolation for delay line reads (safe for smoothed delay time modulation)

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: KrateDSP Layer 0-2 components (DelayLine, Waveshaper, Biquad, DCBlocker, EnvelopeFollower, OnePoleSmoother), Steinberg VST3 SDK
**Storage**: N/A (audio processor, no persistent storage)
**Testing**: Catch2 (see dsp/tests/)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) - all 64-bit
**Project Type**: DSP library component (monorepo with plugins)
**Performance Goals**: < 0.5% CPU per instance at 44100Hz sample rate (SC-005)
**Constraints**: Zero-latency processing (SC-007), real-time safe (no allocations, locks, exceptions in process())
**Scale/Scope**: Single mono DSP processor class, ~300-400 lines of code

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Pre-Design Check:**

- [x] **Principle II (Real-Time Safety)**: Design uses noexcept, pre-allocation in prepare(), no locks in process()
- [x] **Principle III (Modern C++)**: Using C++20 features, RAII, value semantics
- [x] **Principle IX (Layered Architecture)**: Layer 2 processor depending only on Layers 0-1 + peer Layer 2 (EnvelopeFollower)
- [x] **Principle X (DSP Constraints)**: Soft limiting for feedback > 100%, DC blocking after saturation
- [x] **Principle XII (Test-First)**: Tests will be written before implementation
- [x] **Principle XIV (ODR Prevention)**: Searched for FeedbackDistortion - not found
- [x] **Principle XV (Pre-Implementation Research)**: research.md complete with all decisions documented

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide, dsp-architecture) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: FeedbackDistortion

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| FeedbackDistortion | `grep -r "class FeedbackDistortion" dsp/ plugins/` | No (only in specs/) | Create New |

**Utility Functions to be created**: None (all functionality from existing components)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| N/A | N/A | N/A | N/A | N/A |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| DelayLine | dsp/include/krate/dsp/primitives/delay_line.h | 1 | Feedback delay path (0.1s max) |
| Waveshaper | dsp/include/krate/dsp/primitives/waveshaper.h | 1 | Saturation with selectable curve |
| WaveshapeType | dsp/include/krate/dsp/primitives/waveshaper.h | 1 | Enum for saturation selection |
| Biquad | dsp/include/krate/dsp/primitives/biquad.h | 1 | Tone filter (lowpass, Butterworth Q) |
| DCBlocker | dsp/include/krate/dsp/primitives/dc_blocker.h | 1 | DC offset removal after saturation |
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | 1 | Parameter smoothing (x5) |
| EnvelopeFollower | dsp/include/krate/dsp/processors/envelope_follower.h | 2 | Limiter level tracking (Peak mode) |
| dbToGain | dsp/include/krate/dsp/core/db_utils.h | 0 | Threshold dB to linear conversion |
| gainToDb | dsp/include/krate/dsp/core/db_utils.h | 0 | Level to dB conversion |
| kButterworthQ | dsp/include/krate/dsp/primitives/biquad.h | 1 | Tone filter Q value (0.707) |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 DSP processors
- [x] `specs/_architecture_/` - Component inventory

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: FeedbackDistortion is a new unique class. All planned functionality uses existing primitives via composition.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| DelayLine | prepare | `void prepare(double sampleRate, float maxDelaySeconds) noexcept` | Yes |
| DelayLine | reset | `void reset() noexcept` | Yes |
| DelayLine | write | `void write(float sample) noexcept` | Yes |
| DelayLine | readLinear | `[[nodiscard]] float readLinear(float delaySamples) const noexcept` | Yes |
| Waveshaper | setType | `void setType(WaveshapeType type) noexcept` | Yes |
| Waveshaper | setDrive | `void setDrive(float drive) noexcept` | Yes |
| Waveshaper | process | `[[nodiscard]] float process(float x) const noexcept` | Yes |
| Biquad | configure | `void configure(FilterType type, float frequency, float Q, float gainDb, float sampleRate) noexcept` | Yes |
| Biquad | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| Biquad | reset | `void reset() noexcept` | Yes |
| DCBlocker | prepare | `void prepare(double sampleRate, float cutoffHz = 10.0f) noexcept` | Yes |
| DCBlocker | process | `[[nodiscard]] float process(float x) noexcept` | Yes |
| DCBlocker | reset | `void reset() noexcept` | Yes |
| EnvelopeFollower | prepare | `void prepare(double sampleRate, size_t maxBlockSize) noexcept` | Yes |
| EnvelopeFollower | setMode | `void setMode(DetectionMode mode) noexcept` | Yes |
| EnvelopeFollower | setAttackTime | `void setAttackTime(float ms) noexcept` | Yes |
| EnvelopeFollower | setReleaseTime | `void setReleaseTime(float ms) noexcept` | Yes |
| EnvelopeFollower | processSample | `[[nodiscard]] float processSample(float input) noexcept` | Yes |
| EnvelopeFollower | reset | `void reset() noexcept` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| dbToGain | function | `[[nodiscard]] constexpr float dbToGain(float dB) noexcept` | Yes |
| detail::flushDenormal | function | `[[nodiscard]] inline constexpr float flushDenormal(float x) noexcept` | Yes |
| kButterworthQ | constant | `inline constexpr float kButterworthQ = 0.7071067811865476f` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/delay_line.h` - DelayLine class
- [x] `dsp/include/krate/dsp/primitives/waveshaper.h` - Waveshaper class
- [x] `dsp/include/krate/dsp/primitives/biquad.h` - Biquad class
- [x] `dsp/include/krate/dsp/primitives/dc_blocker.h` - DCBlocker class
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/processors/envelope_follower.h` - EnvelopeFollower class
- [x] `dsp/include/krate/dsp/core/db_utils.h` - dB conversion utilities

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| OnePoleSmoother | Uses `snapTo()` not `snap()` | `smoother.snapTo(value)` |
| DelayLine | `readLinear` takes samples, not ms | Convert: `ms * sampleRate / 1000.0f` |
| Biquad | configure() needs sampleRate as float | Cast: `static_cast<float>(sampleRate_)` |
| EnvelopeFollower | Returns linear amplitude, not dB | Use `gainToDb()` if needed |
| detail::flushDenormal | In `detail` namespace | `detail::flushDenormal(value)` |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| None identified | All functionality exists in primitives | N/A | N/A |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| Soft limiter logic | Specific to this processor, uses EnvelopeFollower state |

**Decision**: No Layer 0 extractions needed. All utility functionality already exists in the layered architecture.

## Higher-Layer Reusability Analysis

*Forward-looking analysis: What code from THIS feature could be reused by SIBLING features at the same layer?*

### Sibling Features Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer** (from DST-ROADMAP.md Priority 7):
- 108-ring-saturation - Self-modulation distortion (already implemented)
- 109-allpass-saturator-network - Resonant distortion networks (already implemented)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Soft limiter logic | MEDIUM | Other feedback processors | Keep local (first use) |
| Feedback loop pattern | LOW | Already in AllpassSaturator | Keep local |

### Detailed Analysis (for HIGH potential items)

No HIGH reuse potential items identified. The soft limiter logic is specific to this use case (envelope-tracked tanh-based limiting) and is better kept as internal implementation.

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep soft limiter internal | First use, pattern may differ for other processors |
| No shared base class | Each distortion processor has unique signal flow |

### Review Trigger

After implementing next feedback-based processor, review this section:
- [ ] Does it need envelope-tracked limiting? Consider extracting SoftLimiter primitive
- [ ] Does it use similar feedback loop pattern? Document shared pattern

## Project Structure

### Documentation (this feature)

```text
specs/110-feedback-distortion/
├── plan.md              # This file
├── research.md          # Research findings (complete)
├── data-model.md        # Entity design (complete)
├── quickstart.md        # Quick reference (to create)
├── contracts/           # API contracts
│   └── feedback_distortion.h  # (complete)
├── checklists/
│   └── requirements.md  # Requirement tracking
└── tasks.md             # Task breakdown (Phase 2 - NOT created by plan)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── processors/
│       └── feedback_distortion.h    # NEW: Header with full implementation
└── tests/
    └── unit/
        └── processors/
            └── feedback_distortion_test.cpp  # NEW: Unit tests
```

**Structure Decision**: Single header in `dsp/include/krate/dsp/processors/` following the existing Layer 2 processor pattern (e.g., saturation_processor.h, envelope_follower.h). Implementation is header-only inline for consistency with other DSP components.

## Complexity Tracking

> No Constitution Check violations requiring justification.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| None | N/A | N/A |

---

## Implementation Artifacts

The following Phase 1 artifacts have been generated:

### Phase 0: Research
- **research.md** - Complete with decisions on soft limiter algorithm, feedback loop stability, DC blocking placement, parameter smoothing, and tone filter design

### Phase 1: Design
- **data-model.md** - Complete with FeedbackDistortion class definition, state transitions, validation rules, signal flow diagram, and component relationships
- **contracts/feedback_distortion.h** - Complete API contract with full documentation, constants, lifecycle methods, processing methods, and parameter accessors

### Remaining Artifacts (Phase 1)
- **quickstart.md** - To be created below

---

## Signal Flow Summary

```
Input (x)
    |
    v
[+ feedbackSample_]  <-- scaled by smoothedFeedback
    |
    v
DelayLine.write() --> DelayLine.readLinear(smoothedDelaySamples)
    |
    v
Waveshaper.process(delayed, smoothedDrive)  <-- saturationCurve type
    |
    v
Biquad.process(saturated)  <-- lowpass, smoothedToneFreq, Q=0.707
    |
    v
DCBlocker.process(filtered)  <-- removes asymmetric saturation DC
    |
    v
EnvelopeFollower.processSample(dcBlocked)  <-- Peak mode, 0.5ms atk, 50ms rel
    |
    v
Soft Limiter: if envelope > threshold, apply tanh-based gain reduction
    |
    v
feedbackSample_ = output * smoothedFeedback
    |
    v
Output
```

## Test Strategy Summary

### Unit Tests (Catch2)

1. **Lifecycle Tests**
   - `prepare()` initializes all components
   - `reset()` clears state without crashing
   - Sample rate range 44100-192000 Hz

2. **Parameter Tests**
   - Each setter clamps to valid range
   - Getters return set values
   - Default values match constants

3. **Processing Tests**
   - NaN/Inf input handling (FR-026)
   - Resonance frequency matches delay time (SC-008: 10ms = ~100Hz)
   - Natural decay with feedback < 1.0 (SC-001)
   - Sustained signal with feedback >= 1.0 (SC-002)
   - Limiter bounds output (SC-003)
   - DC offset < 0.01 (SC-006)

4. **Smoothing Tests**
   - Parameter changes complete within 10ms (SC-004)
   - No audible clicks during transitions

5. **Performance Tests**
   - CPU < 0.5% at 44100Hz (SC-005)
   - Zero latency (SC-007)

## Risk Assessment

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| Limiter doesn't catch extreme feedback | Low | High | Test with feedback=1.5 for extended duration |
| DC accumulation in feedback loop | Low | Medium | DCBlocker after saturation, verified in tests |
| Click artifacts during parameter changes | Medium | Medium | 10ms smoothing on all parameters, verified in tests |
| CPU budget exceeded | Low | Medium | Profiling during implementation, optimize inner loop |
| Resonance frequency drift | Low | Low | Use linear interpolation, verify in tests |

---

## Next Steps

This plan is complete. The next phase is `/speckit.tasks` to generate the task breakdown in `tasks.md`.
