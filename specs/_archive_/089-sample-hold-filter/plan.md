# Implementation Plan: Sample & Hold Filter

**Branch**: `089-sample-hold-filter` | **Date**: 2026-01-23 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/089-sample-hold-filter/spec.md`

## Summary

A Layer 2 DSP processor that samples and holds filter parameters (cutoff, Q, pan) at configurable intervals, creating stepped modulation effects. The processor supports three trigger modes (Clock, Audio, Random), four sample sources per parameter (LFO, Random, Envelope, External), stereo processing with symmetric pan-based cutoff offsets, and slew-limited transitions for smooth modulation.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**:
- SVF (Layer 1) - TPT State Variable Filter for audio filtering
- LFO (Layer 1) - Internal LFO modulation source
- OnePoleSmoother (Layer 1) - Slew limiting for held values
- EnvelopeFollower (Layer 2) - Envelope sample source
- Xorshift32 (Layer 0) - Random value generation and trigger probability

**Storage**: N/A
**Testing**: Catch2 (test-first per Constitution Principle XIII)
**Target Platform**: Cross-platform (Windows, macOS, Linux)
**Project Type**: DSP Library (monorepo)
**Performance Goals**: < 0.5% CPU per instance @ 44.1kHz stereo (SC-004)
**Constraints**:
- Zero allocations in audio thread (Constitution Principle II)
- Sample-accurate timing within 1 sample @ 192kHz (SC-001)
- Deterministic output given same seed/parameters/input (SC-005)
**Scale/Scope**: Single processor class, ~500-800 lines estimated

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

### Pre-Design Check (PASSED)

**Principle II (Real-Time Safety):**
- [x] All process methods will be noexcept
- [x] No memory allocation in audio path
- [x] Pre-allocate all buffers in prepare()
- [x] Using existing real-time-safe components (SVF, LFO, OnePoleSmoother, Xorshift32)

**Principle III (Modern C++):**
- [x] C++20 target
- [x] RAII for all resources
- [x] constexpr where applicable
- [x] Value semantics for small types

**Principle IX (Layered Architecture):**
- [x] Layer 2 Processor (depends on Layers 0-1 primitives)
- [x] Will use Layer 1: SVF, LFO, OnePoleSmoother
- [x] Will use Layer 0: Xorshift32
- [x] Will use Layer 2: EnvelopeFollower (same layer, composition allowed)

**Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Principle VI (Cross-Platform):**
- [x] No platform-specific code needed
- [x] Using only cross-platform components

### Post-Design Re-Check (PASSED)

After completing Phase 1 design (research.md, data-model.md, contracts/, quickstart.md):

**Principle II (Real-Time Safety) - Verified:**
- [x] API contract specifies all process methods as noexcept
- [x] Data model shows no dynamic allocation (fixed member variables)
- [x] Composition uses only real-time-safe components (verified via header review)
- [x] Double-precision timing counter prevents drift without allocation

**Principle IX (Layered Architecture) - Verified:**
- [x] data-model.md confirms Layer 2 placement
- [x] All dependencies are Layer 0-2 (no upward dependencies)
- [x] EnvelopeFollower composition is valid (same-layer composition allowed)

**Principle X (DSP Constraints) - Verified:**
- [x] research.md documents slew limiting via OnePoleSmoother
- [x] No feedback loops requiring soft limiting (S&H is feedforward)
- [x] Denormal handling via existing flushDenormal utility

**Principle XIV (ODR Prevention) - Verified:**
- [x] All new types (TriggerSource, SampleSource, SampleHoldFilter) are unique
- [x] No conflicts with existing Waveform::SampleHold (different namespace/concept)

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: SampleHoldFilter, TriggerSource (enum), SampleSource (enum), ParameterState (internal struct)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| SampleHoldFilter | `grep -r "class SampleHoldFilter" dsp/ plugins/` | No | Create New |
| TriggerSource | `grep -r "TriggerSource" dsp/ plugins/` | No | Create New |
| SampleSource | `grep -r "SampleSource" dsp/ plugins/` | No | Create New |
| ParameterState | `grep -r "ParameterState" dsp/ plugins/` | No | Create New (internal) |

**Note**: `Waveform::SampleHold` exists in LFO (different concept - LFO waveform type vs. filter parameter S&H). No conflict.

**Utility Functions to be created**: None planned (using existing Layer 0 utilities)

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| SVF | `dsp/include/krate/dsp/primitives/svf.h` | 1 | Core filter for audio processing |
| LFO | `dsp/include/krate/dsp/primitives/lfo.h` | 1 | Internal LFO modulation source |
| OnePoleSmoother | `dsp/include/krate/dsp/primitives/smoother.h` | 1 | Slew limiting for held values |
| Xorshift32 | `dsp/include/krate/dsp/core/random.h` | 0 | Random value generation and trigger probability |
| EnvelopeFollower | `dsp/include/krate/dsp/processors/envelope_follower.h` | 2 | Envelope sample source (Peak mode for transients) |
| flushDenormal | `dsp/include/krate/dsp/core/db_utils.h` | 0 | Denormal prevention |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities (no conflicts)
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (no conflicts)
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (no conflicts)
- [x] `specs/_architecture_/` - Component inventory checked

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types are unique and not found in codebase. Similar pattern exists in StochasticFilter (reference implementation) which validates the architecture approach. No naming conflicts with existing Waveform::SampleHold (different concept, different namespace scope).

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| SVF | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| SVF | setMode | `void setMode(SVFMode mode) noexcept` | Yes |
| SVF | setCutoff | `void setCutoff(float hz) noexcept` | Yes |
| SVF | setResonance | `void setResonance(float q) noexcept` | Yes |
| SVF | process | `[[nodiscard]] float process(float input) noexcept` | Yes |
| SVF | reset | `void reset() noexcept` | Yes |
| SVF | kMinCutoff | `static constexpr float kMinCutoff = 1.0f` | Yes |
| SVF | kMaxCutoffRatio | `static constexpr float kMaxCutoffRatio = 0.495f` | Yes |
| SVF | kMinQ | `static constexpr float kMinQ = 0.1f` | Yes |
| SVF | kMaxQ | `static constexpr float kMaxQ = 30.0f` | Yes |
| LFO | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| LFO | process | `[[nodiscard]] float process() noexcept` | Yes |
| LFO | setFrequency | `void setFrequency(float hz) noexcept` | Yes |
| LFO | setWaveform | `void setWaveform(Waveform waveform) noexcept` | Yes |
| LFO | reset | `void reset() noexcept` | Yes |
| OnePoleSmoother | configure | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| OnePoleSmoother | setTarget | `ITERUM_NOINLINE void setTarget(float target) noexcept` | Yes |
| OnePoleSmoother | process | `[[nodiscard]] float process() noexcept` | Yes |
| OnePoleSmoother | snapTo | `void snapTo(float value) noexcept` | Yes |
| OnePoleSmoother | isComplete | `[[nodiscard]] bool isComplete() const noexcept` | Yes |
| OnePoleSmoother | getCurrentValue | `[[nodiscard]] float getCurrentValue() const noexcept` | Yes |
| Xorshift32 | constructor | `explicit constexpr Xorshift32(uint32_t seedValue = 1) noexcept` | Yes |
| Xorshift32 | nextFloat | `[[nodiscard]] constexpr float nextFloat() noexcept` (returns [-1,1]) | Yes |
| Xorshift32 | nextUnipolar | `[[nodiscard]] constexpr float nextUnipolar() noexcept` (returns [0,1]) | Yes |
| Xorshift32 | seed | `constexpr void seed(uint32_t seedValue) noexcept` | Yes |
| EnvelopeFollower | prepare | `void prepare(double sampleRate, size_t maxBlockSize) noexcept` | Yes |
| EnvelopeFollower | processSample | `[[nodiscard]] float processSample(float input) noexcept` | Yes |
| EnvelopeFollower | setMode | `void setMode(DetectionMode mode) noexcept` | Yes |
| EnvelopeFollower | setAttackTime | `void setAttackTime(float ms) noexcept` | Yes |
| EnvelopeFollower | setReleaseTime | `void setReleaseTime(float ms) noexcept` | Yes |
| EnvelopeFollower | getCurrentValue | `[[nodiscard]] float getCurrentValue() const noexcept` | Yes |
| EnvelopeFollower | reset | `void reset() noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/svf.h` - SVF class
- [x] `dsp/include/krate/dsp/primitives/lfo.h` - LFO class
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother class
- [x] `dsp/include/krate/dsp/core/random.h` - Xorshift32 class
- [x] `dsp/include/krate/dsp/processors/envelope_follower.h` - EnvelopeFollower class
- [x] `dsp/include/krate/dsp/processors/stochastic_filter.h` - Reference implementation

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| Xorshift32 | `nextFloat()` returns [-1, 1], `nextUnipolar()` returns [0, 1] | Use `nextFloat()` for bipolar, `nextUnipolar()` for probability |
| OnePoleSmoother | `snapTo()` sets both current AND target | Use for initialization, not for target changes |
| SVF | `kMaxCutoffRatio` is 0.495 (not Nyquist) | Max cutoff = sampleRate * 0.495 |
| EnvelopeFollower | `prepare()` takes maxBlockSize (unused but required) | Pass 0 or buffer size |
| LFO | `process()` advances phase each call | Don't call multiple times per sample |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| None identified | N/A | N/A | N/A |

**Decision**: No new Layer 0 utilities needed. All required math/conversion functions exist in db_utils.h and math_constants.h.

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| calculateTriggerInterval | Specific to S&H timing, only one consumer |
| applyPanOffset | Specific to stereo cutoff offset, only one consumer |

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer** (from ROADMAP.md or known plans):
- 087-stochastic-filter - Similar filter modulation, different random source model
- 088-self-oscillating-filter - Same SVF dependency, different modulation
- Future: rhythm-synced effects (clock trigger logic potentially reusable)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| TriggerSource enum | MEDIUM | Rhythm-synced processors | Keep local; extract after 2nd use |
| SampleSource enum | LOW | S&H-specific concept | Keep local |
| Per-parameter source selection pattern | MEDIUM | Multi-source modulators | Keep local; document pattern |
| Clock trigger timing logic | MEDIUM | Arpeggiators, sequencers | Keep local; extract after 2nd use |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared base class with StochasticFilter | Different modulation paradigm (S&H vs continuous random); composition preferred over inheritance |
| Keep trigger logic internal | First feature needing this pattern; wait for concrete reuse need |
| Use EnvelopeFollower composition | Already exists at Layer 2, proper separation of concerns |

### Review Trigger

After implementing **next tempo-synced processor**, review this section:
- [ ] Does it need clock trigger logic? -> Extract to shared utility
- [ ] Does it use similar per-parameter source selection? -> Document shared pattern

## Project Structure

### Documentation (this feature)

```text
specs/089-sample-hold-filter/
├── plan.md              # This file
├── research.md          # Phase 0 output
├── data-model.md        # Phase 1 output
├── quickstart.md        # Phase 1 output
├── contracts/           # Phase 1 output (API contracts)
└── tasks.md             # Phase 2 output (/speckit.tasks command)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── processors/
│       └── sample_hold_filter.h    # Main implementation (header-only)
└── tests/
    └── unit/
        └── processors/
            └── sample_hold_filter_test.cpp  # Catch2 tests
```

**Structure Decision**: Single header-only implementation in Layer 2 processors, following established pattern from StochasticFilter.

## Complexity Tracking

> No Constitution Check violations identified.

| Violation | Why Needed | Simpler Alternative Rejected Because |
|-----------|------------|-------------------------------------|
| None | N/A | N/A |
