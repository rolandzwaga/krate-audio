# Implementation Plan: Allpass-Saturator Network

**Branch**: `109-allpass-saturator-network` | **Date**: 2026-01-26 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/109-allpass-saturator-network/spec.md`

## Summary

Layer 2 DSP processor implementing resonant distortion using allpass filters with saturation in feedback loops. The processor creates pitched, self-oscillating resonances that can be excited by input audio. Supports four topologies: SingleAllpass (pitched resonance), AllpassChain (inharmonic bell-like tones), KarplusStrong (plucked string synthesis), and FeedbackMatrix (4x4 Householder matrix for drone generation). All topologies use soft clipping at +/-2.0 in the feedback path to bound self-oscillation while preserving natural dynamics.

## Technical Context

**Language/Version**: C++20 (per constitution)
**Primary Dependencies**:
- Layer 0: math_constants.h, db_utils.h, sigmoid.h
- Layer 1: Biquad (allpass mode), DelayLine, Waveshaper, DCBlocker, OnePoleSmoother, OnePoleLP
**Storage**: N/A (stateful DSP processor, no persistence)
**Testing**: Catch2 (dsp_tests target)
**Target Platform**: Windows, macOS, Linux (cross-platform)
**Project Type**: single (DSP library component)
**Performance Goals**: < 0.5% CPU per instance at 44100Hz (SC-005)
**Constraints**: Zero latency (SC-008), real-time safe (no allocations in process())
**Scale/Scope**: Mono processor, stereo via dual instances

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Pre-Design Constitution Gate:**
- [x] **Principle II (Real-Time Safety)**: Design uses noexcept, no allocations in process()
- [x] **Principle IX (Layered Architecture)**: Layer 2 processor using only Layer 0/1 components
- [x] **Principle X (DSP Constraints)**:
  - Saturation via Waveshaper (existing Layer 1)
  - DC blocking after saturation (DCBlocker)
  - Feedback >100% bounded via soft clipping
  - Allpass interpolation for fixed delays in feedback paths
- [x] **Principle XII (Test-First)**: Tests will be written before implementation
- [x] **Principle XIV (ODR Prevention)**: Searches completed, no conflicts found

**Post-Design Constitution Re-Check (Phase 1 Complete):**
- [x] **Principle II**: API contract shows all process methods as noexcept
- [x] **Principle IX**: Data model confirms Layer 0/1 dependencies only (Biquad, DelayLine, Waveshaper, DCBlocker, OnePoleSmoother, OnePoleLP, sigmoid.h, db_utils.h)
- [x] **Principle X**: Design includes:
  - Waveshaper for saturation (setDrive, setSaturationCurve)
  - DCBlocker in feedback path
  - Soft clipping at +/-2.0 (Sigmoid::tanh(x * 0.5) * 2.0)
  - DelayLine::readAllpass for KarplusStrong fixed delays
- [x] **Principle XII**: quickstart.md and API contract ready for test writing
- [x] **Principle XIV**: All planned types verified unique (AllpassSaturator, SaturatedAllpassStage, HouseholderMatrix, NetworkTopology)

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

**Classes/Structs to be created**: AllpassSaturator, SaturatedAllpassStage, HouseholderMatrix, NetworkTopology (enum)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| AllpassSaturator | `grep -r "class AllpassSaturator" dsp/ plugins/` | No | Create New |
| SaturatedAllpassStage | `grep -r "class SaturatedAllpassStage" dsp/ plugins/` | No | Create New |
| HouseholderMatrix | `grep -r "class HouseholderMatrix\|struct HouseholderMatrix" dsp/ plugins/` | No | Create New |
| NetworkTopology | `grep -r "enum.*NetworkTopology" dsp/ plugins/` | No | Create New |
| AllpassStage | `grep -r "class AllpassStage" dsp/ plugins/` | Yes (diffusion_network.h) | Different purpose - ours has saturation |

**Note**: AllpassStage exists in `diffusion_network.h` but uses Schroeder allpass formulation without saturation. Our `SaturatedAllpassStage` is distinct - it uses Biquad allpass with Waveshaper saturation in the feedback loop.

**Utility Functions to be created**: None - all utilities exist in Layer 0

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| softClip | `grep -r "softClip" dsp/` | Yes | sigmoid.h (Sigmoid::softClipCubic) | Reuse |
| flushDenormal | `grep -r "flushDenormal" dsp/` | Yes | db_utils.h | Reuse |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| Biquad | dsp/include/krate/dsp/primitives/biquad.h | 1 | Allpass filter mode for resonance |
| BiquadCoefficients::calculate | dsp/include/krate/dsp/primitives/biquad.h | 1 | Calculate allpass coefficients |
| FilterType::Allpass | dsp/include/krate/dsp/primitives/biquad.h | 1 | Allpass filter type enum |
| DelayLine | dsp/include/krate/dsp/primitives/delay_line.h | 1 | For KarplusStrong topology |
| Waveshaper | dsp/include/krate/dsp/primitives/waveshaper.h | 1 | Saturation in feedback loops |
| WaveshapeType | dsp/include/krate/dsp/primitives/waveshaper.h | 1 | Saturation curve selection |
| DCBlocker | dsp/include/krate/dsp/primitives/dc_blocker.h | 1 | Remove DC after saturation |
| OnePoleSmoother | dsp/include/krate/dsp/primitives/smoother.h | 1 | 10ms parameter smoothing |
| OnePoleLP | dsp/include/krate/dsp/primitives/one_pole.h | 1 | 6dB/oct lowpass for KarplusStrong |
| flushDenormal | dsp/include/krate/dsp/core/db_utils.h | 0 | Prevent CPU spikes |
| isNaN/isInf | dsp/include/krate/dsp/core/db_utils.h | 0 | Input validation |
| Sigmoid::tanh | dsp/include/krate/dsp/core/sigmoid.h | 0 | Soft clipping at +/-2.0 |
| kPi, kTwoPi | dsp/include/krate/dsp/core/math_constants.h | 0 | Frequency calculations |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (AllpassStage exists but different)
- [x] `specs/_architecture_/` - Component inventory

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: AllpassSaturator and SaturatedAllpassStage are unique names not found in codebase. The existing AllpassStage in diffusion_network.h is a different implementation (Schroeder allpass without saturation). NetworkTopology enum is unique. HouseholderMatrix is unique.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| Biquad | configure | `void configure(FilterType type, float frequency, float Q, float gainDb, float sampleRate) noexcept` | Yes |
| Biquad | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| Biquad | reset | `void reset() noexcept` | Yes |
| BiquadCoefficients | calculate | `static BiquadCoefficients calculate(FilterType, float, float, float, float) noexcept` | Yes |
| DelayLine | prepare | `void prepare(double sampleRate, float maxDelaySeconds) noexcept` | Yes |
| DelayLine | write | `void write(float sample) noexcept` | Yes |
| DelayLine | readAllpass | `[[nodiscard]] float readAllpass(float delaySamples) noexcept` | Yes |
| DelayLine | reset | `void reset() noexcept` | Yes |
| Waveshaper | setType | `void setType(WaveshapeType type) noexcept` | Yes |
| Waveshaper | setDrive | `void setDrive(float drive) noexcept` | Yes |
| Waveshaper | process | `[[nodiscard]] float process(float x) const noexcept` | Yes |
| DCBlocker | prepare | `void prepare(double sampleRate, float cutoffHz = 10.0f) noexcept` | Yes |
| DCBlocker | process | `[[nodiscard]] float process(float x) noexcept` | Yes |
| DCBlocker | reset | `void reset() noexcept` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `ITERUM_NOINLINE void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| OnePoleSmoother | isComplete | `[[nodiscard]] bool isComplete() const noexcept` | Yes |
| OnePoleLP | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| OnePoleLP | setCutoff | `void setCutoff(float hz) noexcept` | Yes |
| OnePoleLP | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| OnePoleLP | reset | `void reset() noexcept` | Yes |
| detail::flushDenormal | - | `[[nodiscard]] inline constexpr float flushDenormal(float x) noexcept` | Yes |
| detail::isNaN | - | `constexpr bool isNaN(float x) noexcept` | Yes |
| detail::isInf | - | `[[nodiscard]] constexpr bool isInf(float x) noexcept` | Yes |
| Sigmoid::tanh | - | `[[nodiscard]] constexpr float tanh(float x) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/biquad.h` - Biquad, BiquadCoefficients, FilterType
- [x] `dsp/include/krate/dsp/primitives/delay_line.h` - DelayLine
- [x] `dsp/include/krate/dsp/primitives/waveshaper.h` - Waveshaper, WaveshapeType
- [x] `dsp/include/krate/dsp/primitives/dc_blocker.h` - DCBlocker
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother
- [x] `dsp/include/krate/dsp/primitives/one_pole.h` - OnePoleLP
- [x] `dsp/include/krate/dsp/core/db_utils.h` - flushDenormal, isNaN, isInf
- [x] `dsp/include/krate/dsp/core/sigmoid.h` - Sigmoid::tanh
- [x] `dsp/include/krate/dsp/core/math_constants.h` - kPi, kTwoPi
- [x] `dsp/include/krate/dsp/processors/karplus_strong.h` - Architecture reference
- [x] `dsp/include/krate/dsp/processors/diffusion_network.h` - AllpassStage reference

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| Biquad | Q parameter affects resonance width, not feedback amount | Use high Q (10-30) for sharp resonance |
| DelayLine::readAllpass | Updates internal state, call order matters | Read before write in feedback |
| OnePoleSmoother | Uses ITERUM_NOINLINE on setTarget | Don't inline setTarget calls |
| OnePoleLP | Must call prepare() before use | Call prepare(sampleRate) in prepare() |
| detail::isNaN | Requires -fno-fast-math | DSP sources have this flag set |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| HouseholderMatrix::multiply | Generic 4x4 Householder matrix multiplication | Could be extracted later | Only this processor for now |

**Decision**: Keep HouseholderMatrix as a private implementation detail within AllpassSaturator. The Householder reflection matrix is specific to this processor's 4-channel feedback topology. If future processors need similar matrix operations, extract then (Rule of Three).

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| frequencyToDelaySamples | One-liner, specific to this processor's topology |
| decayToFeedbackAndCutoff | KarplusStrong-specific calculation, uses class members |
| softClipFeedback | Simple tanh-based clipping at +/-2.0 |

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer** (from roadmap):
- Future feedback network processors
- Comb filter processors
- Physical modeling resonators

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| SaturatedAllpassStage | HIGH | Phaser with saturation, feedback resonators | Keep local, extract after 2nd use |
| HouseholderMatrix | MEDIUM | Dense reverb matrices, spectral processors | Keep local for now |
| NetworkTopology enum | LOW | Specific to this processor | Keep local |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No extraction to Layer 0 | First processor using saturated allpass pattern |
| SaturatedAllpassStage internal | Pattern not yet proven in other contexts |
| Single file implementation | All topologies share common infrastructure |

## Project Structure

### Documentation (this feature)

```text
specs/109-allpass-saturator-network/
├── spec.md              # Feature specification
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
└── contracts/           # Phase 1 output
    └── allpass_saturator_api.h  # API contract
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── processors/
│       └── allpass_saturator.h    # Single header-only implementation
└── tests/
    └── processors/
        └── allpass_saturator_tests.cpp  # Unit tests
```

**Structure Decision**: Single header file in `dsp/include/krate/dsp/processors/`. This is consistent with other Layer 2 processors like `karplus_strong.h`, `diffusion_network.h`. Header-only for simplicity and inlining.

## Complexity Tracking

No constitution violations identified. The design follows all principles:
- Real-time safe (no allocations in process)
- Layered architecture (Layer 2 using only Layer 0/1)
- DSP constraints (saturation, DC blocking, feedback limiting)
- Test-first development
- ODR prevention verified

---

## Phase 0: Research Summary

All clarifications were provided upfront in the spec:

1. **FeedbackMatrix topology**: Householder feedback matrix (unitary, energy-preserving)
2. **Parameter smoothing**: 10ms time constant for frequency, feedback, drive
3. **AllpassChain frequencies**: Prime number ratios (f, 1.5f, 2.33f, 3.67f)
4. **KarplusStrong lowpass**: 1-pole lowpass (6 dB/oct), cutoff from decay parameter
5. **Feedback bounding**: Soft clipping at +/-2.0 via `tanh(x * 0.5) * 2.0`

### Research Decisions

| Decision | Rationale | Alternatives Considered |
|----------|-----------|-------------------------|
| Use Biquad allpass mode | Existing, well-tested, correct frequency response | Custom allpass implementation - unnecessary duplication |
| 4-stage AllpassChain | Matches spec, good balance of complexity/diffusion | 2, 6, 8 stages - 4 provides sufficient inharmonicity |
| Prime number frequency ratios | Avoids coincident resonances, creates bell-like timbre | Equal spacing - too harmonic; Golden ratio - less bell-like |
| Householder matrix | Unitary (energy-preserving), maximally diffusive | Hadamard - also unitary but less dense; Arbitrary - not energy-preserving |
| OnePoleLP for KarplusStrong | 6dB/oct per spec, simple, matches classic algorithm | Biquad lowpass - overkill for this application |

### Householder Matrix Formula

See [data-model.md](data-model.md#householdermatrix-internal-component) for full mathematical derivation and matrix form.

This matrix is:
- Orthogonal (H^T * H = I)
- Energy-preserving (||H*x|| = ||x||)
- Maximally diffusive (all channels mix equally)

### Feedback Soft Clipping

Using `tanh(x * 0.5) * 2.0` provides:
- Bounded output to +/-2.0
- Gradual compression approaching limit
- Preserves natural dynamics at lower levels
- Smooth transition (no discontinuities)
