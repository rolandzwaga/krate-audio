# Implementation Plan: Temporal Distortion Processor

**Branch**: `107-temporal-distortion` | **Date**: 2026-01-26 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/107-temporal-distortion/spec.md`

## Summary

Implement a Layer 2 DSP processor that creates dynamics-aware distortion by modulating waveshaper drive based on signal history. Four temporal modes are supported:

1. **EnvelopeFollow**: Drive increases with amplitude (guitar amp behavior)
2. **InverseEnvelope**: Drive increases as amplitude decreases (expansion effect)
3. **Derivative**: Drive modulated by rate of change (transient emphasis)
4. **Hysteresis**: Drive depends on signal trajectory (path-dependent behavior)

**Technical approach**: Compose existing components (EnvelopeFollower, Waveshaper, OnePoleHP, OnePoleSmoother) with mode-specific drive calculation logic.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: KrateDSP library (EnvelopeFollower, Waveshaper, OnePoleHP, OnePoleSmoother)
**Storage**: N/A (real-time audio processing)
**Testing**: Catch2 via CTest *(Constitution Principle XIII: Test-First Development)*
**Target Platform**: Windows, macOS, Linux (VST3 plugin)
**Project Type**: Single DSP library component
**Performance Goals**: < 0.5% CPU at 44.1kHz stereo (Layer 2 budget)
**Constraints**: Zero allocation in audio thread, noexcept processing, zero latency
**Scale/Scope**: Single processor class (~200 bytes instance)

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Pre-Design Check (PASSED):**

**Required Check - Principle II (Real-Time Safety):**
- [x] No allocations in processing path
- [x] All processing methods will be noexcept
- [x] No locks, I/O, or exceptions in audio thread

**Required Check - Principle IX (Layered Architecture):**
- [x] TemporalDistortion is Layer 2 (Processor)
- [x] Dependencies are only Layer 0-1 components
- [x] No circular dependencies

**Required Check - Principle X (DSP Constraints):**
- [x] No internal oversampling (handled externally)
- [x] No internal DC blocking (handled externally per spec assumption)
- [x] Denormal flushing included

**Required Check - Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Post-Design Re-Check (PASSED):**
- [x] No new constitution violations introduced
- [x] All design decisions comply with principles

## Codebase Research (Principle XV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**:

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| TemporalDistortion | `grep -r "class TemporalDistortion" dsp/ plugins/` | No | Create New |
| TemporalMode | `grep -r "enum.*TemporalMode" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: None - all logic is internal to TemporalDistortion class.

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| EnvelopeFollower | `dsp/include/krate/dsp/processors/envelope_follower.h` | 2 | Amplitude envelope tracking (RMS mode) |
| Waveshaper | `dsp/include/krate/dsp/primitives/waveshaper.h` | 1 | Saturation with variable drive |
| WaveshapeType | `dsp/include/krate/dsp/primitives/waveshaper.h` | 1 | Enum for curve selection |
| OnePoleHP | `dsp/include/krate/dsp/primitives/one_pole.h` | 1 | Derivative calculation (highpass on envelope) |
| OnePoleSmoother | `dsp/include/krate/dsp/primitives/smoother.h` | 1 | Drive smoothing (zipper prevention) |
| detail::isNaN | `dsp/include/krate/dsp/core/db_utils.h` | 0 | NaN detection |
| detail::isInf | `dsp/include/krate/dsp/core/db_utils.h` | 0 | Infinity detection |
| detail::flushDenormal | `dsp/include/krate/dsp/core/db_utils.h` | 0 | Denormal flushing |
| detail::constexprExp | `dsp/include/krate/dsp/core/db_utils.h` | 0 | Coefficient calculation |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors
- [x] `specs/_architecture_/` - Component inventory
- [x] `specs/_architecture_/layer-2-processors.md` - Existing processors

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: TemporalDistortion and TemporalMode are unique names not found in codebase. All reused components are well-established with stable APIs. No name collisions detected.

## Dependency API Contracts (Principle XV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| EnvelopeFollower | prepare | `void prepare(double sampleRate, size_t maxBlockSize) noexcept` | Yes |
| EnvelopeFollower | reset | `void reset() noexcept` | Yes |
| EnvelopeFollower | processSample | `[[nodiscard]] float processSample(float input) noexcept` | Yes |
| EnvelopeFollower | setMode | `void setMode(DetectionMode mode) noexcept` | Yes |
| EnvelopeFollower | setAttackTime | `void setAttackTime(float ms) noexcept` | Yes |
| EnvelopeFollower | setReleaseTime | `void setReleaseTime(float ms) noexcept` | Yes |
| EnvelopeFollower | getCurrentValue | `[[nodiscard]] float getCurrentValue() const noexcept` | Yes |
| Waveshaper | setType | `void setType(WaveshapeType type) noexcept` | Yes |
| Waveshaper | setDrive | `void setDrive(float drive) noexcept` | Yes |
| Waveshaper | process | `[[nodiscard]] float process(float x) const noexcept` | Yes |
| Waveshaper | getType | `[[nodiscard]] WaveshapeType getType() const noexcept` | Yes |
| OnePoleHP | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| OnePoleHP | setCutoff | `void setCutoff(float hz) noexcept` | Yes (use 10 Hz per kDerivativeFilterHz) |
| OnePoleHP | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| OnePoleHP | reset | `void reset() noexcept` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| OnePoleSmoother | reset | `void reset() noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/processors/envelope_follower.h` - EnvelopeFollower class
- [x] `dsp/include/krate/dsp/primitives/waveshaper.h` - Waveshaper class, WaveshapeType enum
- [x] `dsp/include/krate/dsp/primitives/one_pole.h` - OnePoleHP, OnePoleLP classes
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/core/db_utils.h` - detail namespace utilities

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| EnvelopeFollower | Detection mode enum is `DetectionMode` not `EnvelopeDetectionMode` | `envelope_.setMode(DetectionMode::RMS)` |
| Waveshaper | Drive of 0.0 returns 0.0 output | Check before processing if needed |
| Waveshaper | setDrive takes absolute value of negative inputs | Pass positive values |
| OnePoleSmoother | Uses `snapTo()` not `snap()` for instant value set | `smoother.snapTo(value)` |
| OnePoleHP | Returns input unchanged if `prepare()` not called | Always call prepare first |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| kReferenceLevel (-12 dBFS) | Audio standard reference | db_utils.h | TemporalDistortion, future dynamics |
| - | - | - | - |

**Decision**: Keep kReferenceLevel as class constant for now. If future dynamics processors need it, extract to db_utils.h.

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| calculateEffectiveDrive | Mode-specific logic, internal only |
| updateHysteresisCoefficient | Instance-specific, uses member sampleRate_ |

**Decision**: All new utility logic stays internal to TemporalDistortion class. No Layer 0 extraction needed.

## Higher-Layer Reusability Analysis

*Forward-looking analysis: What code from THIS feature could be reused by SIBLING features at the same layer?*

### Sibling Features Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer** (from existing Layer 2 processors):
- TapeSaturator: Has hysteresis model (Jiles-Atherton) - different algorithm
- FuzzProcessor: Has dynamics interaction - different approach
- DynamicsProcessor: Envelope-based - different purpose (compression/expansion)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Drive modulation patterns | LOW | None identified | Keep local |
| Hysteresis state model | LOW | TapeSaturator uses J-A model | Keep local |
| TemporalMode enum | LOW | Specific to this processor | Keep local |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared base class | Drive modulation is unique to temporal distortion concept |
| Keep hysteresis local | TapeSaturator uses physical J-A model, we use simple decay |
| No utility extraction | All code is specific to temporal distortion behavior |

### Review Trigger

After implementing next distortion-related processor, review:
- [ ] Does new processor need envelope-driven drive? Consider extracting pattern
- [ ] Does new processor need hysteresis? Evaluate if simple decay model fits

## Project Structure

### Documentation (this feature)

```text
specs/107-temporal-distortion/
├── spec.md              # Feature specification
├── plan.md              # This file
├── research.md          # Phase 0 research findings
├── data-model.md        # Phase 1 data structures
├── quickstart.md        # Phase 1 usage guide
├── contracts/           # Phase 1 API contracts
│   └── temporal_distortion_api.h
└── tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── processors/
│       └── temporal_distortion.h    # NEW: Header implementation
└── tests/
    └── unit/
        └── processors/
            └── temporal_distortion_test.cpp  # NEW: Unit tests
```

**Structure Decision**: Standard KrateDSP monorepo layout. Single header file in processors/ with corresponding test file.

## Complexity Tracking

No constitution violations requiring justification. Design follows all principles.

## Phase 0 Output: Research

See [research.md](research.md) for detailed findings including:
- Existing component API analysis
- Drive modulation formulas for each mode
- Mode switching approach
- Performance estimates
- Edge case handling
- Alternatives considered

## Phase 1 Output: Design

See the following documents:
- [data-model.md](data-model.md) - Entity definitions, state management, relationships
- [contracts/temporal_distortion_api.h](contracts/temporal_distortion_api.h) - Complete API contract
- [quickstart.md](quickstart.md) - Usage examples and integration guide

## Implementation Summary

### Files to Create

| File | Purpose | Lines (est.) |
|------|---------|--------------|
| `dsp/include/krate/dsp/processors/temporal_distortion.h` | Header-only implementation | ~350 |
| `dsp/tests/unit/processors/temporal_distortion_test.cpp` | Unit tests | ~500 |

### Key Implementation Details

1. **Envelope tracking**: Use EnvelopeFollower in RMS mode
2. **Drive smoothing**: OnePoleSmoother with 5ms time constant
3. **Derivative calculation**: OnePoleHP at 10 Hz on envelope signal (chosen from 5-20 Hz range for optimal transient detection with noise rejection; see research.md section 2.4)
4. **Hysteresis model**: Simple exponential decay state memory
5. **Reference level**: -12 dBFS RMS = 0.251189 linear

### Test Strategy

1. Mode-specific behavior tests (SC-001 to SC-004)
2. Timing tests for attack/release (SC-005, SC-006)
3. Mode switching artifact test (SC-007)
4. Block vs sample equivalence (SC-008)
5. Edge case tests (FR-027, FR-028, FR-029)
6. Performance benchmark (SC-010)

## Next Steps

1. Run `/speckit.tasks` to generate detailed task breakdown
2. Implement tests first (Principle XIII)
3. Implement TemporalDistortion class
4. Update `specs/_architecture_/layer-2-processors.md`
