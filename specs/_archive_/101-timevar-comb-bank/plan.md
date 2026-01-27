# Implementation Plan: TimeVaryingCombBank

**Branch**: `101-timevar-comb-bank` | **Date**: 2026-01-25 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/101-timevar-comb-bank/spec.md`

## Summary

Implement a Layer 3 DSP system component - TimeVaryingCombBank - that aggregates up to 8 FeedbackComb filters with independently modulated delay times. The component creates evolving metallic and resonant textures by modulating each comb's delay time with per-comb LFOs and optional random drift. Supports automatic harmonic/inharmonic tuning from a fundamental frequency and stereo pan distribution.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: FeedbackComb (L1), LFO (L1), OnePoleSmoother (L1), Xorshift32 (L0)
**Storage**: N/A (stateful DSP, no persistence)
**Testing**: Catch2 (via CMake dsp_tests target)
**Target Platform**: Windows, macOS, Linux (cross-platform VST3)
**Project Type**: DSP library component (monorepo - `dsp/` directory)
**Performance Goals**: <10ms to process 1 second at 44.1kHz with 8 combs (SC-003)
**Constraints**: Real-time safe (noexcept, no allocations in process), Layer 3 dependencies only
**Scale/Scope**: Single header file, ~400 lines implementation

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Pre-Design Check (PASSED)**:
- [x] Principle II (Real-Time Safety): Design uses noexcept, pre-allocation in prepare()
- [x] Principle III (Modern C++): C++20, RAII, std::array, value semantics
- [x] Principle IX (Layered Architecture): Layer 3, depends only on L0-L1 components
- [x] Principle X (DSP Constraints): Linear interpolation for modulated delays (not allpass)
- [x] Principle XI (Performance Budget): <1% CPU target, achievable with ~28ns per comb per sample
- [x] Principle XII (Test-First): Tests planned before implementation
- [x] Principle XIV (ODR Prevention): Searched codebase, no existing TimeVaryingCombBank or CombBank

**Post-Design Check (PASSED)**:
- [x] API design follows existing Layer 3 patterns (FilterFeedbackMatrix, FeedbackNetwork)
- [x] All FR requirements mappable to API methods
- [x] All SC requirements have measurable test strategies
- [x] No constitution violations in design

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: TimeVaryingCombBank, CombChannel, Tuning

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| TimeVaryingCombBank | `grep -r "class TimeVaryingCombBank" dsp/ plugins/` | No | Create New |
| CombChannel | `grep -r "struct CombChannel" dsp/ plugins/` | No | Create New (internal) |
| Tuning | `grep -r "enum.*Tuning" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: None (using existing db_utils.h)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| dbToGain | `grep -r "dbToGain" dsp/` | Yes | core/db_utils.h | Reuse |
| isNaN | `grep -r "isNaN" dsp/` | Yes | core/db_utils.h | Reuse |
| isInf | `grep -r "isInf" dsp/` | Yes | core/db_utils.h | Reuse |
| flushDenormal | `grep -r "flushDenormal" dsp/` | Yes | core/db_utils.h | Reuse |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| FeedbackComb | dsp/include/krate/dsp/primitives/comb_filter.h | 1 | Core comb filter (8 instances) |
| LFO | dsp/include/krate/dsp/primitives/lfo.h | 1 | Per-comb modulation (8 instances) |
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | 1 | Parameter smoothing (32 instances) |
| Xorshift32 | dsp/include/krate/dsp/core/random.h | 0 | Per-comb random drift (8 instances) |
| dbToGain | dsp/include/krate/dsp/core/db_utils.h | 0 | Convert gain dB to linear |
| detail::isNaN | dsp/include/krate/dsp/core/db_utils.h | 0 | NaN detection |
| detail::isInf | dsp/include/krate/dsp/core/db_utils.h | 0 | Inf detection |
| detail::flushDenormal | dsp/include/krate/dsp/core/db_utils.h | 0 | Denormal prevention |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/systems/` - Layer 3 systems (pattern reference)
- [x] `specs/_architecture_/` - Component inventory
- [x] `specs/FLT-ROADMAP.md` - Contains "CombBank" mention but no implementation

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types are unique and not found in codebase. The only mention of "CombBank" is in a roadmap document as future work, with no existing implementation. All utility functions come from existing Layer 0 headers.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| FeedbackComb | prepare | `void prepare(double sampleRate, float maxDelaySeconds) noexcept` | Yes |
| FeedbackComb | reset | `void reset() noexcept` | Yes |
| FeedbackComb | setFeedback | `void setFeedback(float g) noexcept` | Yes |
| FeedbackComb | setDamping | `void setDamping(float d) noexcept` | Yes |
| FeedbackComb | setDelaySamples | `void setDelaySamples(float samples) noexcept` | Yes |
| FeedbackComb | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| LFO | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| LFO | reset | `void reset() noexcept` | Yes |
| LFO | process | `[[nodiscard]] float process() noexcept` | Yes |
| LFO | setFrequency | `void setFrequency(float hz) noexcept` | Yes |
| LFO | setPhaseOffset | `void setPhaseOffset(float degrees) noexcept` | Yes |
| LFO | setWaveform | `void setWaveform(Waveform waveform) noexcept` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `ITERUM_NOINLINE void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| Xorshift32 | constructor | `explicit constexpr Xorshift32(uint32_t seedValue = 1) noexcept` | Yes |
| Xorshift32 | nextFloat | `[[nodiscard]] constexpr float nextFloat() noexcept` | Yes |
| Xorshift32 | seed | `constexpr void seed(uint32_t seedValue) noexcept` | Yes |
| dbToGain | function | `[[nodiscard]] constexpr float dbToGain(float dB) noexcept` | Yes |
| detail::isNaN | function | `constexpr bool isNaN(float x) noexcept` | Yes |
| detail::isInf | function | `[[nodiscard]] constexpr bool isInf(float x) noexcept` | Yes |
| detail::flushDenormal | function | `[[nodiscard]] inline constexpr float flushDenormal(float x) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/comb_filter.h` - FeedbackComb class
- [x] `dsp/include/krate/dsp/primitives/lfo.h` - LFO class
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/core/random.h` - Xorshift32 class
- [x] `dsp/include/krate/dsp/core/db_utils.h` - Utility functions

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| FeedbackComb | Uses `maxDelaySeconds` not `maxDelayMs` | `comb.prepare(sr, maxDelayMs / 1000.0f)` |
| FeedbackComb | `setDelaySamples()` not `setDelayMs()` | Convert ms to samples: `ms * sr / 1000.0` |
| OnePoleSmoother | Uses `snapTo()` not `snap()` | `smoother.snapTo(value)` |
| LFO | Output is bipolar [-1, 1] | Scale: `base * (1 + lfo * depth)` |
| LFO | Phase offset in degrees, not radians | `lfo.setPhaseOffset(45.0f)` for 45 degrees |
| Xorshift32 | Seed of 0 uses default seed | Pass non-zero for custom seed |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| harmonicFrequency | Potentially reusable for Karplus-Strong | tuning_utils.h | Future string synthesis |
| inharmonicFrequency | Potentially reusable for bell synthesis | tuning_utils.h | Future physical modeling |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| computeHarmonicDelay | Only one consumer so far, simple formula |
| computeInharmonicDelay | Only one consumer so far, simple formula |
| recalculatePanPositions | Specific to this class's stereo spread model |
| recalculateLfoPhases | Specific to this class's phase spread model |

**Decision**: Keep all utility functions as private member functions for now. Extract to Layer 0 (tuning_utils.h) when a second consumer appears (e.g., Karplus-Strong synthesis, shimmer effect).

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 3 - System Components

**Related features at same layer** (from ROADMAP.md or known plans):
- Shimmer effect: Could use pitch-shifted comb banks
- Karplus-Strong synthesis: Uses similar comb-with-damping pattern
- Physical modeling resonators: Tuned resonances

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Tuning enum | MEDIUM | Karplus-Strong, resonator | Keep local for now |
| Harmonic delay calculation | MEDIUM | Karplus-Strong | Keep local, extract later |
| Inharmonic delay calculation | LOW | Physical modeling | Keep local |
| Pan distribution algorithm | LOW | Other stereo spreaders | Keep local |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared base class | First comb bank at this layer - patterns not established |
| Keep tuning calculations local | Only one consumer so far |
| Use std::array not std::vector | Fixed kMaxCombs=8 at compile time |

### Review Trigger

After implementing **Karplus-Strong synthesis**, review this section:
- [ ] Does Karplus-Strong need harmonic tuning calculation? -> Extract to tuning_utils.h
- [ ] Does Karplus-Strong use same comb composition pattern? -> Consider shared base

## Project Structure

### Documentation (this feature)

```text
specs/101-timevar-comb-bank/
├── plan.md              # This file
├── research.md          # Phase 0 output - component analysis
├── data-model.md        # Phase 1 output - entity definitions
├── quickstart.md        # Phase 1 output - usage examples
├── contracts/           # Phase 1 output - API contract
│   └── timevar_comb_bank.h  # Header interface definition
└── tasks.md             # Phase 2 output (NOT created by /speckit.plan)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── systems/
│       └── timevar_comb_bank.h    # Implementation (header-only)
└── tests/
    └── systems/
        └── timevar_comb_bank_tests.cpp  # Catch2 tests
```

**Structure Decision**: Header-only implementation in Layer 3 systems directory, following the pattern established by FilterFeedbackMatrix.

## Complexity Tracking

> No constitution violations detected. All design decisions follow established patterns.

## Phase 0 Output

See [research.md](research.md) for:
- Existing component API analysis (FeedbackComb, LFO, OnePoleSmoother, Xorshift32)
- Tuning calculation formulas (harmonic, inharmonic)
- Stereo pan distribution algorithm
- Modulation depth application
- NaN/Inf handling strategy
- Memory and performance estimates

## Phase 1 Output

See:
- [data-model.md](data-model.md) - Entity definitions, field specifications, state transitions
- [contracts/timevar_comb_bank.h](contracts/timevar_comb_bank.h) - Full API contract
- [quickstart.md](quickstart.md) - Usage examples and parameter reference

## Implementation Files Summary

| Artifact | Location | Description |
|----------|----------|-------------|
| Implementation Header | `dsp/include/krate/dsp/systems/timevar_comb_bank.h` | Full implementation |
| Tests | `dsp/tests/systems/timevar_comb_bank_tests.cpp` | Catch2 test suite |
| API Contract | `specs/101-timevar-comb-bank/contracts/timevar_comb_bank.h` | Interface reference |
| Research | `specs/101-timevar-comb-bank/research.md` | Technical analysis |
| Data Model | `specs/101-timevar-comb-bank/data-model.md` | Entity definitions |
| Quickstart | `specs/101-timevar-comb-bank/quickstart.md` | Usage guide |

## Requirement Mapping

| Requirement | API Method | Test Strategy |
|-------------|------------|---------------|
| FR-001 | `setNumCombs()` | Test 1-8 range, verify inactive combs not processed |
| FR-002 | `setCombDelay()` | Test delay times match expected values |
| FR-003 | `setCombFeedback()` | Test feedback clamping, verify resonance |
| FR-004 | `setCombDamping()` | Test damping effect on frequency content |
| FR-005 | `setCombGain()` | Test dB conversion, output level |
| FR-006 | `setTuningMode()` | Test mode switching, delay recalculation |
| FR-007 | `setFundamental()` | Test harmonic/inharmonic delay calculation |
| FR-008 | `setSpread()` | Test inharmonic spread effect |
| FR-009 | `setModRate()`, `setModDepth()` | Test modulation range, depth scaling |
| FR-010 | `setModPhaseSpread()` | Test phase offset per comb |
| FR-011 | `setRandomModulation()` | Test deterministic random with seed |
| FR-012 | `setStereoSpread()` | Test pan distribution |
| FR-013 | `process()` | Test mono output sum |
| FR-014 | `processStereo()` | Test L/R pan distribution |
| FR-015 | `prepare()` | Test initialization, buffer allocation |
| FR-016 | `reset()` | Test state clearing |
| FR-017 | All process methods | Verify noexcept, no allocations |
| FR-018 | Internal | Verify linear interpolation used |
| FR-019 | Internal | Test smoothing time constants |
| FR-020 | Internal | Test per-comb NaN/Inf reset |
| SC-001 | Test | Verify harmonic frequencies within 1 cent |
| SC-002 | Test | Verify no clicks with modulation |
| SC-003 | Test | Benchmark <10ms for 1s at 44.1kHz |
| SC-004 | Test | Verify deterministic random output |
| SC-005 | Test | Verify smooth parameter transitions |
| SC-006 | Test | Verify stereo decorrelation |
