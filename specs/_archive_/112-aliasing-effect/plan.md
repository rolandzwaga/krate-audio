# Implementation Plan: AliasingEffect

**Branch**: `112-aliasing-effect` | **Date**: 2026-01-27 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/112-aliasing-effect/spec.md`

## Summary

Implement `AliasingEffect`, a Layer 2 DSP processor for intentional aliasing with band isolation and frequency shifting. The processor creates digital grunge/lo-fi aesthetic by downsampling without anti-aliasing, causing high frequencies to fold back into the audible spectrum. Key features include configurable band isolation (using two-stage cascade bandpass for 24dB/oct), pre-downsample frequency shifting (using existing FrequencyShifter), and fractional sample rate reduction (extending SampleRateReducer to factor 32).

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**:
- SampleRateReducer (Layer 1 primitive - EXTEND kMaxReductionFactor from 8 to 32)
- FrequencyShifter (Layer 2 processor - REUSE with fixed config)
- Biquad/BiquadCascade (Layer 1 primitive - REUSE for band isolation)
- OnePoleSmoother (Layer 1 primitive - REUSE for parameter smoothing)
**Storage**: N/A (stateless except for filter/shifter internal state)
**Testing**: Catch2 (unit tests for DSP correctness, stability, parameter smoothing)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) - cross-platform
**Project Type**: DSP library component (monorepo structure)
**Performance Goals**: < 0.5% CPU per instance at 44100Hz (Layer 2 processor budget)
**Constraints**: Real-time safe (no allocations in process), mono-only processing
**Scale/Scope**: Single processor class, approximately 5-sample latency from FrequencyShifter

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle I (VST3 Architecture Separation):** N/A - This is a DSP-only component, not plugin code.

**Principle II (Real-Time Audio Thread Safety):**
- [x] No memory allocation in process() - uses pre-allocated components
- [x] No locks, mutexes, or blocking - all operations are lock-free
- [x] No file I/O, network ops, or system calls in process
- [x] No exceptions thrown - all methods noexcept
- [x] Pre-allocate in prepare() before processing

**Principle III (Modern C++ Standards):**
- [x] Target C++20
- [x] Use RAII for all resources (Biquads, smoothers auto-manage state)
- [x] No raw new/delete - all stack-allocated or value types
- [x] Use constexpr for constants

**Principle IX (Layered DSP Architecture):**
- [x] Layer 2 processor - can use Layers 0-1
- [x] FrequencyShifter is Layer 2, but used as a composed component (valid at same layer)
- [x] No circular dependencies

**Principle X (DSP Processing Constraints):**
- [x] No oversampling needed (aliasing IS the desired effect)
- [x] DC blocking not needed (bandpass filters inherently block DC)
- [x] No feedback path requiring soft-limiting

**Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Principle XV (Pre-Implementation Research):**
- [x] Searched for existing AliasingEffect - none found
- [x] Verified no naming conflicts with planned types

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: AliasingEffect

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| AliasingEffect | `grep -r "class AliasingEffect" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: None - all utilities exist in Layer 0

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| N/A | N/A | N/A | N/A | N/A |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| SampleRateReducer | dsp/include/krate/dsp/primitives/sample_rate_reducer.h | 1 | Sample-and-hold downsampling (EXTEND max factor from 8 to 32) |
| FrequencyShifter | dsp/include/krate/dsp/processors/frequency_shifter.h | 2 | SSB frequency shifting before downsample (fixed config: Direction=Up, Feedback=0, ModDepth=0, Mix=1.0) |
| Biquad | dsp/include/krate/dsp/primitives/biquad.h | 1 | Bandpass filter for band isolation |
| BiquadCascade<2> | dsp/include/krate/dsp/primitives/biquad.h | 1 | Two-stage cascade for 24dB/oct slopes |
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | 1 | Parameter smoothing (10ms time constant) |
| detail::isNaN | dsp/include/krate/dsp/core/db_utils.h | 0 | NaN input detection |
| detail::isInf | dsp/include/krate/dsp/core/db_utils.h | 0 | Inf input detection |
| detail::flushDenormal | dsp/include/krate/dsp/core/db_utils.h | 0 | Denormal flushing |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (no AliasingEffect exists)
- [x] `specs/_architecture_/` - Component inventory (no AliasingEffect documented)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: The planned AliasingEffect class does not exist in the codebase. All dependencies are well-established primitives and processors that have been verified to exist with documented APIs.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| SampleRateReducer | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| SampleRateReducer | reset | `void reset() noexcept` | Yes |
| SampleRateReducer | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| SampleRateReducer | setReductionFactor | `void setReductionFactor(float factor) noexcept` | Yes |
| SampleRateReducer | kMaxReductionFactor | `static constexpr float kMaxReductionFactor = 8.0f` | Yes (will extend to 32.0f) |
| FrequencyShifter | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| FrequencyShifter | reset | `void reset() noexcept` | Yes |
| FrequencyShifter | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| FrequencyShifter | setShiftAmount | `void setShiftAmount(float hz) noexcept` | Yes |
| FrequencyShifter | setDirection | `void setDirection(ShiftDirection dir) noexcept` | Yes |
| FrequencyShifter | setFeedback | `void setFeedback(float amount) noexcept` | Yes |
| FrequencyShifter | setModDepth | `void setModDepth(float hz) noexcept` | Yes |
| FrequencyShifter | setMix | `void setMix(float dryWet) noexcept` | Yes |
| FrequencyShifter | kMaxShiftHz | `static constexpr float kMaxShiftHz = 5000.0f` | Yes |
| Biquad | configure | `void configure(FilterType type, float frequency, float Q, float gainDb, float sampleRate) noexcept` | Yes |
| Biquad | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| Biquad | reset | `void reset() noexcept` | Yes |
| BiquadCascade<2> | setButterworth | `void setButterworth(FilterType type, float frequency, float sampleRate) noexcept` | Yes |
| BiquadCascade<2> | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| BiquadCascade<2> | reset | `void reset() noexcept` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| OnePoleSmoother | reset | `void reset() noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/sample_rate_reducer.h` - SampleRateReducer class
- [x] `dsp/include/krate/dsp/processors/frequency_shifter.h` - FrequencyShifter class
- [x] `dsp/include/krate/dsp/primitives/biquad.h` - Biquad, BiquadCascade classes
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/core/db_utils.h` - detail::isNaN, isInf, flushDenormal

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| OnePoleSmoother | Uses `snapTo()` not `snap()` | `smoother.snapTo(value)` |
| BiquadCascade | setButterworth takes FilterType enum | `cascade.setButterworth(FilterType::Lowpass, freq, sr)` |
| FrequencyShifter | Not thread-safe | Create separate instances per channel |
| FrequencyShifter | 5-sample latency from Hilbert transform | Document in AliasingEffect |
| Biquad | Bandpass uses center frequency and Q | Q = fc/bandwidth for bandpass width |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| None identified | No new utility functions needed | N/A | N/A |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| Band frequency clamping | Simple inline operation, specific to AliasingEffect parameters |

**Decision**: No new Layer 0 utilities needed. All required utilities (isNaN, isInf, flushDenormal) already exist.

## Higher-Layer Reusability Analysis

*Forward-looking analysis: What code from THIS feature could be reused by SIBLING features at the same layer?*

### Sibling Features Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer** (from DST-ROADMAP.md Priority 8 - Digital Destruction):
- 111-bitwise-mangler - Bit manipulation distortion (completed)
- 113-granular-distortion - Per-grain variable distortion (planned)
- 114-fractal-distortion - Recursive multi-scale distortion (planned)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Extended SampleRateReducer (factor 32) | HIGH | 113-granular-distortion, other lo-fi effects | Already primitive - modification benefits all |
| Band-isolation pattern | MEDIUM | Other frequency-selective effects | Keep local - pattern clear from code |
| FrequencyShifter composition | LOW | Specific to aliasing | Keep local |

### Detailed Analysis (for HIGH potential items)

**Extended SampleRateReducer (kMaxReductionFactor = 32)** provides:
- Higher downsample ratios for extreme lo-fi effects
- More severe aliasing artifacts
- Useful for other digital destruction effects

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| 113-granular-distortion | MAYBE | Might use for per-grain rate reduction |
| 114-fractal-distortion | NO | Different processing paradigm |

**Recommendation**: Extend SampleRateReducer primitive (benefits entire codebase)

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Extend SampleRateReducer to factor 32 | Shared primitive benefits future lo-fi effects |
| Keep band-isolation as internal pattern | First use case - wait for second consumer |
| No shared base class for digital destruction | Sibling features have different processing chains |

### Review Trigger

After implementing **113-granular-distortion**, review this section:
- [ ] Does granular-distortion need band isolation? -> Consider shared utility
- [ ] Does granular-distortion use sample rate reduction? -> Verify extended factor useful
- [ ] Any duplicated parameter smoothing patterns? -> Consider shared utilities

## Project Structure

### Documentation (this feature)

```text
specs/112-aliasing-effect/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
└── contracts/           # Phase 1 output (API contracts)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   ├── primitives/
│   │   └── sample_rate_reducer.h  # MODIFY: Extend kMaxReductionFactor to 32
│   └── processors/
│       └── aliasing_effect.h      # NEW: AliasingEffect processor
└── tests/
    ├── unit/primitives/
    │   └── sample_rate_reducer_test.cpp  # MODIFY: Add tests for factor 32
    └── unit/processors/
        └── aliasing_effect_test.cpp      # NEW: AliasingEffect tests
```

**Structure Decision**: Standard Layer 2 processor layout with header-only implementation (following existing patterns like FrequencyShifter). SampleRateReducer modification is a simple constant change.

## Post-Design Constitution Re-Check

*GATE: Verify design still complies after Phase 1 design completion.*

**Principle II (Real-Time Safety):** PASS
- Design uses only pre-allocated components (BiquadCascade, FrequencyShifter, SampleRateReducer, smoothers)
- All processing methods documented as noexcept
- No new allocations introduced in processing path

**Principle IX (Layer Architecture):** PASS
- AliasingEffect at Layer 2 uses Layer 1 primitives (BiquadCascade, SampleRateReducer, OnePoleSmoother)
- FrequencyShifter composition at same layer is valid (no circular dependencies)

**Principle X (DSP Constraints):** PASS
- No oversampling needed (aliasing is intentional)
- DC blocking handled by bandpass filters
- No feedback path requiring soft-limiting

**Principle XIV (ODR Prevention):** PASS
- AliasingEffect is unique name (verified via grep)
- All dependencies have documented APIs
- No naming conflicts identified

**Design Verification**: All constitution principles satisfied. No violations or deviations required.

## Complexity Tracking

No Constitution violations to justify. Design is straightforward:
- Layer 2 processor composing Layer 1 primitives and one Layer 2 processor
- All dependencies already exist
- No new architectural patterns introduced
