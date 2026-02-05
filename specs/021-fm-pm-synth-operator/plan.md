# Implementation Plan: FM/PM Synthesis Operator

**Branch**: `021-fm-pm-synth-operator` | **Date**: 2026-02-05 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `specs/021-fm-pm-synth-operator/spec.md`

## Summary

Single FM operator (oscillator + ratio + feedback + level), the fundamental building block for FM/PM synthesis. Uses phase modulation (Yamaha DX7-style) where the modulator output is added to the carrier's phase, not frequency. Layer 2 processor that wraps a WavetableOscillator reading from a sine WavetableData, with self-modulation feedback using fastTanh limiting.

**Technical Approach:**
- Compose existing `WavetableOscillator` (Layer 1) with a sine `WavetableData` generated via `generateMipmappedFromHarmonics`
- Implement feedback path with `FastMath::fastTanh()` for self-modulation limiting
- Provide `lastRawOutput()` for modulator chaining in future FM Voice (Layer 3)
- All processing real-time safe (noexcept, no allocations)

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**:
- WavetableOscillator (Layer 1) - core oscillator engine
- WavetableData (Layer 0) - mipmapped sine table storage
- generateMipmappedFromHarmonics (Layer 1) - sine table generation
- FastMath::fastTanh (Layer 0) - feedback limiting
- phase_utils.h (Layer 0) - calculatePhaseIncrement, wrapPhase
- math_constants.h (Layer 0) - kTwoPi for radians conversion
- db_utils.h (Layer 0) - detail::isNaN, detail::isInf

**Storage**: N/A (single WavetableData stored internally, ~90 KB)
**Testing**: Catch2 (dsp_tests target)
**Target Platform**: Windows, macOS, Linux (cross-platform)
**Project Type**: DSP library component (header-only, Layer 2)
**Performance Goals**: < 0.5% CPU per operator at 44.1 kHz (Layer 2 budget)
**Constraints**:
- Real-time safe processing (no allocations, no locks, noexcept)
- Output bounded to [-2.0, 2.0] with sanitization
- Feedback bounded via tanh to approximately [-1, 1] radians
**Scale/Scope**: Single operator; FM Voice (4-6 operators) deferred to Phase 9

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle II - Real-Time Audio Thread Safety:**
- [x] process() will be noexcept with no allocations
- [x] prepare() is explicitly NOT real-time safe (generates wavetable)
- [x] reset() will be noexcept with no allocations
- [x] All parameter setters will be noexcept with no allocations

**Principle III - Modern C++ Standards:**
- [x] C++20 required (std::bit_cast for sanitization)
- [x] [[nodiscard]] on process(), lastRawOutput()
- [x] constexpr where applicable
- [x] Value semantics, no raw new/delete

**Principle IX - Layered DSP Architecture:**
- [x] Layer 2 (Processor) - depends only on Layers 0-1
- [x] No circular dependencies (WavetableOscillator, WavetableData, FastMath are all below)

**Principle X - DSP Processing Constraints:**
- [x] Feedback >100% MUST include soft limiting - using fastTanh
- [x] No oversampling needed (sine wave has no aliasing)
- [x] No DC blocking needed (sine is symmetric)

**Principle XII - Test-First Development:**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Principle XIV - ODR Prevention:**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: FMOperator

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| FMOperator | `grep -r "class FMOperator\|struct FMOperator" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: None (all utilities exist in Layer 0)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| fastTanh | `grep -r "fastTanh" dsp/` | Yes | core/fast_math.h | Reuse |
| isNaN | `grep -r "detail::isNaN" dsp/` | Yes | core/db_utils.h | Reuse |
| isInf | `grep -r "detail::isInf" dsp/` | Yes | core/db_utils.h | Reuse |
| calculatePhaseIncrement | `grep -r "calculatePhaseIncrement" dsp/` | Yes | core/phase_utils.h | Reuse |
| wrapPhase | `grep -r "wrapPhase" dsp/` | Yes | core/phase_utils.h | Reuse |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| WavetableOscillator | dsp/include/krate/dsp/primitives/wavetable_oscillator.h | 1 | Internal oscillator engine for sine generation |
| WavetableData | dsp/include/krate/dsp/core/wavetable_data.h | 0 | Storage for mipmapped sine wavetable |
| generateMipmappedFromHarmonics | dsp/include/krate/dsp/primitives/wavetable_generator.h | 1 | Generate sine table (1 harmonic at amplitude 1.0) |
| FastMath::fastTanh | dsp/include/krate/dsp/core/fast_math.h | 0 | Feedback limiting |
| detail::isNaN | dsp/include/krate/dsp/core/db_utils.h | 0 | Input sanitization |
| detail::isInf | dsp/include/krate/dsp/core/db_utils.h | 0 | Input sanitization |
| kTwoPi | dsp/include/krate/dsp/core/math_constants.h | 0 | PM radians to normalized phase conversion |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (no FMOperator exists)
- [x] `specs/_architecture_/` - Component inventory

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: No existing FMOperator class in codebase. All planned utilities already exist and will be reused. The AudioRateFilterFM processor in Layer 2 is unrelated (modulates filter cutoff, not oscillator phase).

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| WavetableOscillator | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| WavetableOscillator | reset | `void reset() noexcept` | Yes |
| WavetableOscillator | setWavetable | `void setWavetable(const WavetableData* table) noexcept` | Yes |
| WavetableOscillator | setFrequency | `void setFrequency(float hz) noexcept` | Yes |
| WavetableOscillator | setPhaseModulation | `void setPhaseModulation(float radians) noexcept` | Yes |
| WavetableOscillator | process | `[[nodiscard]] float process() noexcept` | Yes |
| WavetableOscillator | phase | `[[nodiscard]] double phase() const noexcept` | Yes |
| WavetableOscillator | resetPhase | `void resetPhase(double newPhase = 0.0) noexcept` | Yes |
| WavetableData | setNumLevels | `void setNumLevels(size_t n) noexcept` | Yes |
| WavetableData | getMutableLevel | `float* getMutableLevel(size_t level) noexcept` | Yes |
| generateMipmappedFromHarmonics | (function) | `void generateMipmappedFromHarmonics(WavetableData& data, const float* harmonicAmplitudes, size_t numHarmonics)` | Yes |
| FastMath::fastTanh | (function) | `[[nodiscard]] constexpr float fastTanh(float x) noexcept` | Yes |
| detail::isNaN | (function) | `constexpr bool isNaN(float x) noexcept` | Yes |
| detail::isInf | (function) | `[[nodiscard]] constexpr bool isInf(float x) noexcept` | Yes |
| kTwoPi | (constant) | `inline constexpr float kTwoPi = 2.0f * kPi` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/wavetable_oscillator.h` - WavetableOscillator class
- [x] `dsp/include/krate/dsp/core/wavetable_data.h` - WavetableData struct
- [x] `dsp/include/krate/dsp/primitives/wavetable_generator.h` - generateMipmappedFromHarmonics
- [x] `dsp/include/krate/dsp/core/fast_math.h` - fastTanh function
- [x] `dsp/include/krate/dsp/core/db_utils.h` - isNaN, isInf functions
- [x] `dsp/include/krate/dsp/core/math_constants.h` - kTwoPi constant
- [x] `dsp/include/krate/dsp/core/phase_utils.h` - Phase utilities

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| WavetableOscillator | setPhaseModulation takes radians, not normalized phase | `osc_.setPhaseModulation(totalPmRadians)` |
| WavetableOscillator | pmOffset_ resets to 0 after each process() call | Must set PM before each process() |
| WavetableOscillator | setWavetable takes non-owning pointer | FMOperator must own the WavetableData |
| generateMipmappedFromHarmonics | harmonicAmplitudes[0] = fundamental (harmonic 1) | Single element array {1.0f} for sine |

## Layer 0 Candidate Analysis

*For Layer 2+ features: Identify utility functions that should be extracted to Layer 0 for reuse.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| (none) | All needed utilities already exist in Layer 0 | N/A | N/A |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| Internal frequency clamping | One-liner, specific to FM operator Nyquist handling |
| Output sanitization | Already exists in WavetableOscillator::sanitize(), will follow same pattern |

**Decision**: No new Layer 0 utilities needed. All required functions (fastTanh, isNaN, isInf, phase utilities) already exist.

## Higher-Layer Reusability Analysis

*Forward-looking analysis: What code from THIS feature could be reused by SIBLING features at the same layer?*

### Sibling Features Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer** (from OSC-ROADMAP.md):
- Phase 10: Phase Distortion Oscillator - wraps WavetableOscillator with modified phase
- Phase 5: Sync Oscillator - already implemented, similar composition pattern

**Dependent feature at higher layer:**
- Phase 9: FM Voice (Layer 3) - composes 4-6 FMOperator instances with algorithm routing

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| FMOperator class | HIGH | FM Voice (Phase 9) | Keep in processors/ for Layer 3 consumption |
| Sine table generation pattern | MEDIUM | Phase Distortion Oscillator | Keep local - PD may use different waveforms |
| Feedback with tanh pattern | LOW | Other feedback processors | Keep local - feedback path is FM-specific |

### Detailed Analysis (for HIGH potential items)

**FMOperator class** provides:
- Sine oscillation at frequency * ratio
- Phase modulation input (external + feedback)
- Self-modulation feedback with tanh limiting
- Level-controlled output with raw output access

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| FM Voice (Phase 9) | YES | Core building block - 4-6 instances per voice |
| Phase Distortion Osc | NO | Different concept - phase warping, not PM |
| Sync Oscillator | NO | Already implemented, different approach |

**Recommendation**: Keep FMOperator in processors/ directory. No shared base class needed - the pattern is specific to FM synthesis.

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared base class with PD Oscillator | Different concepts: PM adds to phase, PD warps phase |
| Store WavetableData internally | Simplifies API; multiple operators don't need shared table management |
| Expose lastRawOutput() | Required for FM Voice modulator chaining |

### Review Trigger

After implementing **FM Voice (Phase 9)**, review this section:
- [ ] Does FM Voice need FMOperator modifications? -> Refine API
- [ ] Does FM Voice need shared sine table optimization? -> Add static table option
- [ ] Any duplicated code? -> Consider shared utilities

## Project Structure

### Documentation (this feature)

```text
specs/021-fm-pm-synth-operator/
├── plan.md              # This file
├── research.md          # Phase 0 output - FM synthesis patterns
├── data-model.md        # Phase 1 output - FMOperator entity
├── quickstart.md        # Phase 1 output - usage examples
├── contracts/           # Phase 1 output - API contract
│   └── fm_operator.h    # Public API specification
└── tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── processors/
│       └── fm_operator.h     # NEW: FMOperator class
└── tests/
    └── unit/
        └── processors/
            └── fm_operator_test.cpp  # NEW: Test suite
```

**Structure Decision**: Single header file in Layer 2 processors. Internal WavetableData owned by FMOperator. Tests in processors test directory following existing patterns (sync_oscillator_test.cpp).

## Complexity Tracking

> **No violations - section not applicable**

All requirements fit cleanly within Layer 2 constraints:
- Composition of existing Layer 0-1 components
- No new dependencies needed
- Standard processor pattern matching SyncOscillator, AudioRateFilterFM
