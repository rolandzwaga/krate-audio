# Implementation Plan: PolyBLEP Oscillator

**Branch**: `015-polyblep-oscillator` | **Date**: 2026-02-03 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `specs/015-polyblep-oscillator/spec.md`

## Summary

Implement a band-limited audio-rate PolyBLEP oscillator at Layer 1 (primitives/) with sine, sawtooth, square, pulse, and triangle waveforms. The oscillator uses polynomial band-limited step (PolyBLEP) correction from the existing `core/polyblep.h` and phase management from `core/phase_utils.h`. Triangle uses a leaky integrator approach on a PolyBLEP-corrected square wave. The implementation is scalar-only but designed for future SIMD optimization with branchless inner loops, cache-friendly layout, and aligned data.

## Technical Context

**Language/Version**: C++20 (MSVC 2019+, Clang 12+, GCC 10+)
**Primary Dependencies**: `core/polyblep.h` (PolyBLEP math), `core/phase_utils.h` (PhaseAccumulator), `core/math_constants.h` (kPi, kTwoPi), `core/db_utils.h` (NaN/Inf detection)
**Storage**: N/A (no persistent storage)
**Testing**: Catch2 (via `dsp_tests` target), spectral analysis test helpers (`tests/test_helpers/spectral_analysis.h`)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: Header-only DSP library component
**Performance Goals**: ~50 cycles/sample for PolyBLEP waveforms (saw/square/pulse/triangle), ~15-20 cycles/sample for sine
**Constraints**: Zero memory allocation in process(), noexcept, single-threaded model, Layer 1 dependency rules (Layer 0 only)
**Scale/Scope**: Single header file (~300-400 lines), single test file (~600-800 lines), 2 files modified (CMakeLists.txt, architecture docs)

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

### Pre-Design Check

| Principle | Status | Notes |
|-----------|--------|-------|
| I. VST3 Architecture Separation | N/A | Pure DSP library, no plugin code |
| II. Real-Time Audio Thread Safety | PASS | All process() methods are noexcept, no allocation, no locks |
| III. Modern C++ Standards | PASS | C++20, RAII, constexpr, value semantics |
| IV. SIMD & DSP Optimization | PASS | Branchless design, aligned data, scalar-only initial |
| V. VSTGUI Development | N/A | No UI code |
| VI. Cross-Platform Compatibility | PASS | Header-only, no platform-specific code |
| VII. Project Structure & Build System | PASS | Correct layer (primitives/), angle bracket includes |
| VIII. Testing Discipline | PASS | Tests written before implementation |
| IX. Layered DSP Architecture | PASS | Layer 1, depends only on Layer 0 |
| X. DSP Processing Constraints | PASS | No oversampling needed (source oscillator, not nonlinear processing) |
| XI. Performance Budgets | PASS | Layer 1 target < 0.1% CPU |
| XII. Debugging Discipline | PASS | Will follow debug-before-pivot |
| XIII. Test-First Development | PASS | Failing tests first, then implementation |
| XIV. Living Architecture Documentation | PASS | Will update layer-1-primitives.md |
| XV. Pre-Implementation Research (ODR) | PASS | All searches completed, no conflicts found |
| XVI. Honest Completion | PASS | Compliance table required before claiming done |
| XVII. Framework Knowledge | N/A | No VSTGUI/VST3 framework code |
| XVIII. Spec Numbering | PASS | Spec 015, correct |

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

**Classes/Structs to be created**: `PolyBlepOscillator`

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| `PolyBlepOscillator` | `grep -r "class PolyBlepOscillator" dsp/ plugins/` | No | Create New |
| `OscWaveform` | `grep -r "OscWaveform" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: None (all utilities exist in Layer 0)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| (none) | N/A | N/A | N/A | N/A |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `polyBlep(t, dt)` | `dsp/include/krate/dsp/core/polyblep.h` | 0 | PolyBLEP correction for saw, square, pulse |
| `PhaseAccumulator` | `dsp/include/krate/dsp/core/phase_utils.h` | 0 | Phase management (advance, wrap detection) |
| `calculatePhaseIncrement()` | `dsp/include/krate/dsp/core/phase_utils.h` | 0 | Frequency to phase increment conversion |
| `wrapPhase()` | `dsp/include/krate/dsp/core/phase_utils.h` | 0 | Phase wrapping for PM and resetPhase |
| `kPi`, `kTwoPi` | `dsp/include/krate/dsp/core/math_constants.h` | 0 | Sine computation, PM radians conversion |
| `detail::isNaN()` | `dsp/include/krate/dsp/core/db_utils.h` | 0 | Output sanitization (NaN detection) |
| `detail::isInf()` | `dsp/include/krate/dsp/core/db_utils.h` | 0 | Output sanitization (Inf detection) |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities (no conflicts)
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (no `polyblep_oscillator.h` exists)
- [x] `specs/_architecture_/` - Component inventory (no PolyBlepOscillator listed)
- [x] `dsp/include/krate/dsp/primitives/lfo.h` - Existing `Waveform` enum, different name, no conflict
- [x] `dsp/include/krate/dsp/processors/audio_rate_filter_fm.h` - Existing `FMWaveform` enum, different name, no conflict

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: Both `PolyBlepOscillator` and `OscWaveform` are unique names not found anywhere in the codebase. The only similar names are `Waveform` (in `lfo.h`) and `FMWaveform` (in `audio_rate_filter_fm.h`), which are completely separate enums with different values. The new enum name `OscWaveform` was specifically chosen to avoid any conflict per FR-028.

## Dependency API Contracts (Principle XV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| PhaseAccumulator | phase | `double phase = 0.0` (public member) | Yes |
| PhaseAccumulator | increment | `double increment = 0.0` (public member) | Yes |
| PhaseAccumulator | advance() | `[[nodiscard]] bool advance() noexcept` | Yes |
| PhaseAccumulator | reset() | `void reset() noexcept` | Yes |
| PhaseAccumulator | setFrequency() | `void setFrequency(float frequency, float sampleRate) noexcept` | Yes |
| phase_utils | calculatePhaseIncrement | `[[nodiscard]] constexpr double calculatePhaseIncrement(float frequency, float sampleRate) noexcept` | Yes |
| phase_utils | wrapPhase | `[[nodiscard]] constexpr double wrapPhase(double phase) noexcept` | Yes |
| polyblep | polyBlep | `[[nodiscard]] constexpr float polyBlep(float t, float dt) noexcept` | Yes |
| math_constants | kPi | `inline constexpr float kPi = 3.14159265358979323846f` | Yes |
| math_constants | kTwoPi | `inline constexpr float kTwoPi = 2.0f * kPi` | Yes |
| db_utils | detail::isNaN | `constexpr bool isNaN(float x) noexcept` | Yes |
| db_utils | detail::isInf | `[[nodiscard]] constexpr bool isInf(float x) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/phase_utils.h` - PhaseAccumulator struct, utility functions
- [x] `dsp/include/krate/dsp/core/polyblep.h` - polyBlep, polyBlep4, polyBlamp, polyBlamp4
- [x] `dsp/include/krate/dsp/core/math_constants.h` - kPi, kTwoPi, kHalfPi, kPiSquared
- [x] `dsp/include/krate/dsp/core/db_utils.h` - detail::isNaN, detail::isInf, flushDenormal

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| PhaseAccumulator | `advance()` returns bool (wrapped), NOT the new phase | `bool wrapped = phaseAcc_.advance(); float t = static_cast<float>(phaseAcc_.phase);` |
| PhaseAccumulator | `setFrequency()` takes `(float, float)` not `(double, double)` | `phaseAcc_.setFrequency(freq, sampleRate_)` |
| calculatePhaseIncrement | Returns `double`, not `float` | Cast to float for dt: `dt_ = static_cast<float>(calculatePhaseIncrement(...))` |
| wrapPhase | Returns `double`, not `float` | Cast when needed for float computation |
| polyBlep | Precondition: `dt < 0.5` | Must clamp frequency to < sampleRate/2 |
| polyBlep | `t` parameter is phase position [0, 1), NOT time | Use `static_cast<float>(phaseAcc_.phase)` |
| detail::isNaN | In `detail` namespace (not public) | `Krate::DSP::detail::isNaN(x)` |
| kTwoPi | Is `float` (not `double`) | Use for float operations; cast for double phase math |

## Layer 0 Candidate Analysis

*No new Layer 0 utilities are needed. All required functions already exist.*

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| Output sanitization (NaN/clamp) | Specific to oscillator output, only used here. Inline in process(). |
| Leaky integrator step | Part of triangle generation, tightly coupled to oscillator state. |

**Decision**: No Layer 0 extraction needed. All required utilities already exist.

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 1 - DSP Primitives

**Related features at same layer** (from OSC-ROADMAP.md):
- Phase 3: WavetableOscillator (primitives/) -- Different anti-aliasing (mipmap)
- Phase 4: MinBlepTable (primitives/) -- Precomputed table for sync
- Phase 9: NoiseOscillator (primitives/) -- Independent, no overlap

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| `OscWaveform` enum | HIGH | SyncOscillator (Phase 5), SubOscillator (Phase 6), UnisonEngine (Phase 7), Rungler (Phase 15) | Keep in polyblep_oscillator.h for now; extract to shared header if needed by Phase 5 |
| `PolyBlepOscillator` class | HIGH | SyncOscillator (Phase 5, slave oscillator), SubOscillator (Phase 6, master tracking), UnisonEngine (Phase 7, per-voice), Rungler (Phase 15, both oscillators) | Designed for composition -- no changes needed |
| `phase()` / `phaseWrapped()` interface | HIGH | SyncOscillator needs master wrap, SubOscillator needs master wrap | Interface is stable by design |

### Detailed Analysis (for HIGH potential items)

**OscWaveform enum** provides:
- Waveform selection shared across oscillator ecosystem
- Sequential integer values usable as array indices

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| SyncOscillator (Phase 5) | YES | Selects slave waveform |
| SubOscillator (Phase 6) | MAYBE | Has its own SubWaveform enum but may reference OscWaveform |
| UnisonEngine (Phase 7) | YES | Per-voice waveform selection |

**Recommendation**: Keep `OscWaveform` in `polyblep_oscillator.h` for now. If Phase 5 needs it without pulling in the full oscillator, extract to a separate `osc_waveform.h` header at that time.

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep OscWaveform in oscillator header | Only one consumer (this oscillator) until Phase 5. Premature extraction adds unnecessary files. |
| Make PolyBlepOscillator copyable | Value semantics enables UnisonEngine to store oscillator array directly. No heap allocation. |
| Use function pointer dispatch for waveform | Eliminates per-sample branching while allowing runtime waveform selection. |

### Review Trigger

After implementing **Phase 5 (SyncOscillator)**, review this section:
- [ ] Does SyncOscillator need `OscWaveform` without full oscillator header? -> Extract to shared header
- [ ] Does SyncOscillator use same phase interface? -> Document shared pattern
- [ ] Any duplicated code? -> Consider shared utilities

## Project Structure

### Documentation (this feature)

```text
specs/015-polyblep-oscillator/
+-- plan.md              # This file
+-- spec.md              # Feature specification
+-- research.md          # Phase 0 research output
+-- data-model.md        # Phase 1 data model
+-- quickstart.md        # Phase 1 implementation guide
+-- contracts/           # Phase 1 API contracts
|   +-- polyblep_oscillator_api.h
+-- checklists/          # Task checklists (pre-existing)
```

### Source Code (repository root)

```text
dsp/
+-- include/krate/dsp/
|   +-- core/
|   |   +-- polyblep.h          # [EXISTS] PolyBLEP math (dependency)
|   |   +-- phase_utils.h       # [EXISTS] Phase accumulator (dependency)
|   |   +-- math_constants.h    # [EXISTS] kPi, kTwoPi (dependency)
|   |   +-- db_utils.h          # [EXISTS] NaN/Inf detection (dependency)
|   +-- primitives/
|       +-- polyblep_oscillator.h  # [NEW] The oscillator implementation
+-- tests/
    +-- unit/primitives/
        +-- polyblep_oscillator_test.cpp  # [NEW] Test suite
    +-- CMakeLists.txt                     # [MODIFY] Add test file
```

**Structure Decision**: Header-only implementation in a single file at Layer 1 (primitives/). Tests in the standard test location. No new directories needed.

## Complexity Tracking

No constitution violations. No complexity justifications needed.

## Post-Design Constitution Re-Check

| Principle | Status | Notes |
|-----------|--------|-------|
| II. Real-Time Safety | PASS | process() is noexcept, no allocation, no locks. Sanitization is branchless. |
| III. Modern C++ | PASS | C++20 (`std::bit_cast`), `[[nodiscard]]`, constexpr where possible, value semantics |
| IV. SIMD Optimization | PASS | Branchless inner loop, cache-friendly layout, `processBlock()` is SIMD-ready loop |
| IX. Layered Architecture | PASS | Layer 1, depends only on Layer 0 headers. No Layer 1+ dependencies. |
| XIII. Test-First | PASS | Implementation phases write tests before code |
| XV. ODR Prevention | PASS | All planned types verified unique via grep. No conflicts with existing Waveform/FMWaveform enums. |
| XVI. Honest Completion | PASS | All 43 FR-xxx and 15 SC-xxx requirements tracked in spec compliance table |
