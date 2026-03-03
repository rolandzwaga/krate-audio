# Implementation Plan: Supersaw / Unison Engine

**Branch**: `020-supersaw-unison-engine` | **Date**: 2026-02-04 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/020-supersaw-unison-engine/spec.md`

## Summary

Implement a Layer 3 DSP system (`UnisonEngine`) that composes up to 16 `PolyBlepOscillator` instances into a multi-voice detuned oscillator with stereo spread. The engine uses a non-linear detune curve based on Adam Szabo's analysis of the Roland JP-8000 supersaw (power curve exponent 1.7), constant-power stereo panning, equal-power center/outer voice blend control, and `1/sqrt(N)` gain compensation. All 16 oscillators are pre-allocated as a fixed-size array with zero heap allocation. The implementation is header-only at `dsp/include/krate/dsp/systems/unison_engine.h`.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: PolyBlepOscillator (Layer 1), pitch_utils.h, crossfade_utils.h, math_constants.h, db_utils.h, random.h (all Layer 0)
**Storage**: N/A (no persistent storage; all state is in-memory)
**Testing**: Catch2 (dsp_tests target) *(Constitution Principle XIII: Test-First Development)*
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- cross-platform
**Project Type**: DSP library component (header-only, no plugin integration)
**Performance Goals**: < 200 cycles/sample for 7 voices at 44100 Hz (SC-012)
**Constraints**: < 2048 bytes memory per instance (SC-013); zero heap allocation; real-time safe process()
**Scale/Scope**: Single header file (~300-400 lines), single test file (~500-700 lines)

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Pre-Design Check** (PASSED):

| Principle | Status | Notes |
|-----------|--------|-------|
| I. VST3 Architecture Separation | N/A | This is a DSP library component, not a plugin component |
| II. Real-Time Audio Thread Safety | PASS | `process()` and `processBlock()` will be noexcept, no alloc, no locks, no I/O, no exceptions |
| III. Modern C++ Standards | PASS | C++20, RAII, constexpr, std::array, no raw new/delete |
| IV. SIMD & DSP Optimization | PASS | Branchless sanitization, pre-computed coefficients, contiguous array |
| V. VSTGUI Development | N/A | No UI component |
| VI. Cross-Platform Compatibility | PASS | No platform-specific code; uses std:: math only |
| VII. Project Structure & Build System | PASS | Header at systems/, test at unit/systems/, CMake integration |
| VIII. Testing Discipline | PASS | Test-first development, Catch2, CI-compatible |
| IX. Layered DSP Architecture | PASS | Layer 3 depends on Layer 0 + Layer 1 only |
| X. DSP Processing Constraints | PASS | No saturation/distortion (no oversampling needed), no feedback loop |
| XI. Performance Budgets | PASS | < 1% CPU target for Layer 3 system |
| XII. Debugging Discipline | PASS | Will investigate before pivoting |
| XIII. Test-First Development | PASS | Tests written before implementation |
| XIV. Living Architecture Documentation | PASS | Will update layer-3-systems.md |
| XV. Pre-Implementation Research | PASS | ODR searches completed (see below) |
| XVI. Honest Completion | PASS | Will verify each FR/SC individually |
| XVII. Framework Knowledge | N/A | No VSTGUI/VST3 framework usage |
| XVIII. Spec Numbering | PASS | Spec 020 confirmed unique |

**Required Check - Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: `UnisonEngine`, `StereoOutput`

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| `UnisonEngine` | `grep -r "class UnisonEngine" dsp/ plugins/` | No | Create New |
| `StereoOutput` | `grep -r "struct StereoOutput" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: None (all required utilities already exist in Layer 0)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| N/A | N/A | N/A | N/A | N/A |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `PolyBlepOscillator` | `primitives/polyblep_oscillator.h` | 1 | Compose 16 instances as voice array |
| `OscWaveform` | `primitives/polyblep_oscillator.h` | 1 | Enum for waveform selection |
| `semitonesToRatio()` | `core/pitch_utils.h` | 0 | Convert detune cents to frequency ratios |
| `equalPowerGains()` | `core/crossfade_utils.h` | 0 | Blend crossfade between center/outer voices |
| `Xorshift32` | `core/random.h` | 0 | Deterministic random phase generation |
| `detail::isNaN()` | `core/db_utils.h` | 0 | NaN detection for parameter guards and sanitization |
| `detail::isInf()` | `core/db_utils.h` | 0 | Infinity detection for parameter guards |
| `kPi, kTwoPi, kHalfPi` | `core/math_constants.h` | 0 | Pan law and phase calculations |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/systems/` - Layer 3 systems (existing 19 headers, none conflict)
- [x] `specs/_architecture_/` - Component inventory (layer-3-systems.md reviewed)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: Both planned types (`UnisonEngine`, `StereoOutput`) are unique and not found anywhere in the codebase. All utility functions are reused from existing Layer 0 headers. No name collisions possible.

## Dependency API Contracts (Principle XV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| PolyBlepOscillator | prepare | `inline void prepare(double sampleRate) noexcept` | Yes |
| PolyBlepOscillator | reset | `inline void reset() noexcept` | Yes |
| PolyBlepOscillator | setFrequency | `inline void setFrequency(float hz) noexcept` | Yes |
| PolyBlepOscillator | setWaveform | `inline void setWaveform(OscWaveform waveform) noexcept` | Yes |
| PolyBlepOscillator | process | `[[nodiscard]] inline float process() noexcept` | Yes |
| PolyBlepOscillator | resetPhase | `inline void resetPhase(double newPhase = 0.0) noexcept` | Yes |
| pitch_utils | semitonesToRatio | `[[nodiscard]] inline float semitonesToRatio(float semitones) noexcept` | Yes |
| crossfade_utils | equalPowerGains | `[[nodiscard]] inline std::pair<float, float> equalPowerGains(float position) noexcept` | Yes |
| Xorshift32 | constructor | `explicit constexpr Xorshift32(uint32_t seedValue = 1) noexcept` | Yes |
| Xorshift32 | seed | `constexpr void seed(uint32_t seedValue) noexcept` | Yes |
| Xorshift32 | nextUnipolar | `[[nodiscard]] constexpr float nextUnipolar() noexcept` | Yes |
| db_utils | isNaN | `constexpr bool isNaN(float x) noexcept` (in `detail` namespace) | Yes |
| db_utils | isInf | `[[nodiscard]] constexpr bool isInf(float x) noexcept` (in `detail` namespace) | Yes |
| math_constants | kHalfPi | `inline constexpr float kHalfPi = kPi / 2.0f` | Yes |
| math_constants | kPi | `inline constexpr float kPi = 3.14159265358979323846f` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/polyblep_oscillator.h` - PolyBlepOscillator class
- [x] `dsp/include/krate/dsp/core/pitch_utils.h` - semitonesToRatio function
- [x] `dsp/include/krate/dsp/core/crossfade_utils.h` - equalPowerGains function
- [x] `dsp/include/krate/dsp/core/random.h` - Xorshift32 class
- [x] `dsp/include/krate/dsp/core/db_utils.h` - isNaN, isInf functions
- [x] `dsp/include/krate/dsp/core/math_constants.h` - kPi, kTwoPi, kHalfPi
- [x] `dsp/include/krate/dsp/core/phase_utils.h` - PhaseAccumulator (reference only)
- [x] `dsp/include/krate/dsp/core/stereo_utils.h` - stereoCrossBlend (NOT used)

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| PolyBlepOscillator | `setFrequency()` clamps to [0, sampleRate/2) and maps NaN/Inf to 0.0 (not "ignore") | Frequencies near Nyquist are safe; clamping is handled inside the oscillator |
| equalPowerGains | Returns `{fadeOut, fadeIn}` = `{cos, sin}` -- map to `{centerGain, outerGain}` | `auto [centerGain, outerGain] = equalPowerGains(blend)` |
| Xorshift32 | Seed 0 is replaced with default seed internally | Use non-zero seed (0x5EEDBA5E) |
| detail::isNaN | In `detail` namespace, NOT Krate::DSP directly | `detail::isNaN(x)` not `isNaN(x)` |
| semitonesToRatio | Input is in semitones (1 semitone = 100 cents) | For 50 cents: `semitonesToRatio(50.0f / 100.0f)` = `semitonesToRatio(0.5f)` |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| Constant-power pan law | Audio algorithm, may be needed by VectorMixer (Phase 17) | `core/stereo_utils.h` | UnisonEngine, VectorMixer (future) |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| `computeVoiceLayout()` | Complex, uses multiple class members, only useful within UnisonEngine |
| `sanitize()` | Follows per-class pattern (PolyBlepOscillator, SubOscillator each have their own) |

**Decision**: Keep the pan law inline in UnisonEngine for now. Only one consumer exists. If Phase 17 (VectorMixer) needs identical panning, extract to `core/stereo_utils.h` at that time.

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 3 - System Components

**Related features at same layer** (from OSC-ROADMAP.md):
- Phase 17: `VectorMixer` (systems/) -- XY vector mixing of 4 signals, stereo output
- Phase 8 (optional): `FMVoice` (systems/) -- FM synthesis voice, stereo output
- Existing: `StereoField` (systems/) -- Stereo processing with delay-based widening

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| `StereoOutput` struct | HIGH | VectorMixer, FMVoice, any stereo Layer 3 system | Keep in unison_engine.h; extract to core/stereo_types.h after 2nd consumer |
| Constant-power pan law | MEDIUM | VectorMixer (if it needs panning) | Keep inline; extract after 2nd use |
| Non-linear detune curve | LOW | Unique to supersaw/unison use case | Keep in UnisonEngine |

### Detailed Analysis (for HIGH potential items)

**StereoOutput** provides:
- Lightweight stereo sample return type
- Simple aggregate with `float left, right`
- Consistent interface for all Layer 3 stereo-output systems

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| VectorMixer | YES | Needs stereo output from 4-signal mixing |
| FMVoice | YES | FM voice outputs stereo |
| StereoField | MAYBE | Already uses float& references for L/R, but could adopt |

**Recommendation**: Keep in `unison_engine.h` for now. When VectorMixer or FMVoice is implemented, extract to `core/stereo_types.h` to avoid circular includes.

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep StereoOutput in unison_engine.h | First consumer; premature extraction adds files without benefit |
| Keep pan law as inline code | Only one consumer; extraction adds API surface without clear need |
| No shared base class for oscillator systems | No common interface pattern established; each system has unique process() signatures |

### Review Trigger

After implementing **VectorMixer (Phase 17)**, review this section:
- [ ] Does VectorMixer need `StereoOutput`? -> Extract to `core/stereo_types.h`
- [ ] Does VectorMixer need constant-power pan law? -> Extract to `core/stereo_utils.h`
- [ ] Any duplicated code? -> Consider shared utilities

## Project Structure

### Documentation (this feature)

```text
specs/020-supersaw-unison-engine/
├── plan.md              # This file
├── spec.md              # Feature specification
├── research.md          # Phase 0: Research findings
├── data-model.md        # Phase 1: Entity model and state transitions
├── quickstart.md        # Phase 1: Implementation quickstart guide
├── contracts/           # Phase 1: API contracts
│   └── unison_engine_api.h
└── tasks.md             # Phase 2: Task breakdown (created by /speckit.tasks)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── systems/
│       └── unison_engine.h      # NEW: Header-only UnisonEngine + StereoOutput
└── tests/
    └── unit/
        └── systems/
            └── unison_engine_test.cpp  # NEW: Catch2 test suite
```

### Files Modified

```text
dsp/tests/CMakeLists.txt                    # Add test source + -fno-fast-math
specs/_architecture_/layer-3-systems.md     # Add UnisonEngine documentation
```

**Structure Decision**: This is a DSP library component following the established monorepo pattern. Header-only implementation at Layer 3 (systems/), tests in dsp/tests/unit/systems/. No plugin code changes needed.

## Post-Design Constitution Re-Check

*GATE: Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| II. Real-Time Safety | PASS | All pre-computed arrays, no allocations in process(), branchless sanitization |
| III. Modern C++ | PASS | std::array, constexpr constants, [[nodiscard]], noexcept |
| IX. Layer Architecture | PASS | Layer 3 depends on Layer 0 + Layer 1 only; no Layer 2 needed |
| XI. Performance Budgets | PASS | Estimated well under 1% CPU; 16 osc process() calls + pan/sum arithmetic |
| XIII. Test-First | PASS | Test file will be written before implementation code |
| XV. ODR Prevention | PASS | UnisonEngine and StereoOutput are unique names; all searches clean |
| XVI. Honest Completion | PASS | Plan includes specific measurable criteria (SC-001 through SC-015) |

No constitution violations found. No complexity tracking needed.

## Complexity Tracking

No violations to justify. All constitution principles are satisfied by the design.
