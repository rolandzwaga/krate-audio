# Implementation Plan: Sub-Oscillator

**Branch**: `019-sub-oscillator` | **Date**: 2026-02-04 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/019-sub-oscillator/spec.md`

## Summary

Implement a Layer 2 DSP processor (`SubOscillator`) that tracks a master oscillator via flip-flop frequency division, replicating the classic analog sub-oscillator behavior found in Moog, Sequential, and Oberheim hardware synthesizers. The processor supports three waveforms (square with minBLEP, sine, triangle) at one-octave (divide-by-2) or two-octave (divide-by-4) depths, with an equal-power mix control for blending with the main oscillator output.

**Technical approach**: The core mechanism is a T-flip-flop state machine that toggles on master phase wraps. The square waveform is derived directly from the flip-flop state with sub-sample-accurate minBLEP correction for mastering-grade alias rejection. Sine and triangle waveforms use a separate `PhaseAccumulator` with delta-phase tracking (reading the master's phase increment per sample) for zero-latency frequency response to FM and pitch modulation. All existing Layer 0 and Layer 1 components are reused; no new shared utilities are needed.

## Technical Context

**Language/Version**: C++20 (MSVC 2019+, Clang, GCC)
**Primary Dependencies**: MinBlepTable (Layer 1), PhaseAccumulator (Layer 0), crossfade_utils (Layer 0), phase_utils (Layer 0), math_constants (Layer 0), db_utils (Layer 0)
**Storage**: N/A (stateless header-only processor with minimal per-instance state)
**Testing**: Catch2 via CMake/CTest *(Constitution Principle XIII: Test-First Development)*
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- cross-platform
**Project Type**: Shared DSP library (monorepo: `dsp/`)
**Performance Goals**: < 50 cycles/sample per instance; 128 concurrent instances at 96 kHz
**Constraints**: <= 300 bytes per instance (excluding shared MinBlepTable); real-time safe process(); L1 cache friendly for 128 polyphonic voices
**Scale/Scope**: Single header file (~250 LOC), single test file (~500 LOC)

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Pre-design check**: PASSED

**Required Check - Principle II (Real-Time Safety):**
- [x] process() and processMixed() will be noexcept, no allocation, no locks, no I/O
- [x] All buffers (Residual) pre-allocated in prepare(), not in process()
- [x] No std::vector operations in audio path

**Required Check - Principle III (Modern C++):**
- [x] C++20 features: [[nodiscard]], std::bit_cast, enum class
- [x] RAII: Residual buffer managed by MinBlepTable::Residual
- [x] No raw new/delete

**Required Check - Principle IX (Layer Architecture):**
- [x] Layer 2 processor depends only on Layer 0 (core/) and Layer 1 (primitives/)
- [x] No circular dependencies
- [x] File location: `dsp/include/krate/dsp/processors/sub_oscillator.h`

**Required Check - Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Required Check - Principle XVI (Honest Completion):**
- [x] All SC-xxx have measurable thresholds defined in spec
- [x] Compliance table format matches constitution requirements (Status + Evidence columns)
- [x] Verification instructions include file paths, line numbers, test names, measured values
- [x] Compliance table will use actual measured values from test output
- [x] No generic claims ("implemented", "works") permitted in Evidence column

**Post-design check**: PASSED (re-checked after Phase 1 design)
- [x] Layer compliance confirmed: only Layer 0 and Layer 1 includes
- [x] Memory budget confirmed: ~112 bytes per instance with standard table (well under 300)
- [x] No constitution violations in the design

## Codebase Research (Principle XV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: SubOscillator, SubOctave, SubWaveform

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| SubOscillator | `grep -r "class SubOscillator\|struct SubOscillator" dsp/ plugins/` | No | Create New |
| SubOctave | `grep -r "SubOctave" dsp/ plugins/` | No | Create New |
| SubWaveform | `grep -r "SubWaveform" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: None. All utilities already exist.

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| sanitize() | (private static method, not namespace-scoped) | Pattern exists | `sync_oscillator.h:407` | Reimplement as private static (same pattern) |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `PhaseAccumulator` | `dsp/include/krate/dsp/core/phase_utils.h` | 0 | Internal phase tracking for sine/triangle sub waveforms |
| `wrapPhase()` | `dsp/include/krate/dsp/core/phase_utils.h` | 0 | Phase wrapping for sub phase accumulator |
| `subsamplePhaseWrapOffset()` | `dsp/include/krate/dsp/core/phase_utils.h` | 0 | Sub-sample offset for minBLEP timing |
| `equalPowerGains()` | `dsp/include/krate/dsp/core/crossfade_utils.h` | 0 | Equal-power crossfade gains for processMixed() |
| `kTwoPi` | `dsp/include/krate/dsp/core/math_constants.h` | 0 | Sine waveform computation |
| `detail::isNaN()` | `dsp/include/krate/dsp/core/db_utils.h` | 0 | NaN sanitization for setMix() |
| `detail::isInf()` | `dsp/include/krate/dsp/core/db_utils.h` | 0 | Inf sanitization for setMix() |
| `MinBlepTable` | `dsp/include/krate/dsp/primitives/minblep_table.h` | 1 | Shared table for band-limited step corrections |
| `MinBlepTable::Residual` | `dsp/include/krate/dsp/primitives/minblep_table.h` | 1 | Per-instance ring buffer for minBLEP corrections |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities (no SubOscillator-related types)
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (no SubOscillator-related types)
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (sync_oscillator.h exists, no conflict)
- [x] `specs/_architecture_/layer-2-processors.md` - Component inventory (no SubOscillator)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All three planned types (SubOscillator, SubOctave, SubWaveform) are unique names not found anywhere in the codebase. The `sanitize()` function is a private static method inside the class, not a namespace-level function, so no ODR risk. All reused components are in lower layers with stable APIs.

## Dependency API Contracts (Principle XV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| PhaseAccumulator | phase | `double phase = 0.0;` (public member) | Yes |
| PhaseAccumulator | increment | `double increment = 0.0;` (public member) | Yes |
| PhaseAccumulator | advance | `[[nodiscard]] bool advance() noexcept` | Yes |
| PhaseAccumulator | reset | `void reset() noexcept` | Yes |
| wrapPhase | call | `[[nodiscard]] constexpr double wrapPhase(double phase) noexcept` | Yes |
| subsamplePhaseWrapOffset | call | `[[nodiscard]] constexpr double subsamplePhaseWrapOffset(double phase, double increment) noexcept` | Yes |
| equalPowerGains | call (ref) | `inline void equalPowerGains(float position, float& fadeOut, float& fadeIn) noexcept` | Yes |
| MinBlepTable | isPrepared | `[[nodiscard]] inline bool isPrepared() const noexcept` | Yes |
| MinBlepTable | length | `[[nodiscard]] inline size_t length() const noexcept` | Yes |
| MinBlepTable::Residual | constructor | `explicit Residual(const MinBlepTable& table)` | Yes |
| MinBlepTable::Residual | addBlep | `inline void addBlep(float subsampleOffset, float amplitude) noexcept` | Yes |
| MinBlepTable::Residual | consume | `[[nodiscard]] inline float consume() noexcept` | Yes |
| MinBlepTable::Residual | reset | `inline void reset() noexcept` | Yes |
| detail::isNaN | call | `constexpr bool isNaN(float x) noexcept` | Yes |
| detail::isInf | call | `[[nodiscard]] constexpr bool isInf(float x) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/phase_utils.h` - PhaseAccumulator struct, wrapPhase(), subsamplePhaseWrapOffset()
- [x] `dsp/include/krate/dsp/core/crossfade_utils.h` - equalPowerGains() both overloads
- [x] `dsp/include/krate/dsp/core/math_constants.h` - kTwoPi, kPi, kHalfPi
- [x] `dsp/include/krate/dsp/core/db_utils.h` - detail::isNaN(), detail::isInf()
- [x] `dsp/include/krate/dsp/primitives/minblep_table.h` - MinBlepTable, Residual
- [x] `dsp/include/krate/dsp/processors/sync_oscillator.h` - Reference pattern for constructor, prepare, sanitize

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| PhaseAccumulator | Members are `double`, not `float` | `subPhase_.phase` is double, cast when needed |
| subsamplePhaseWrapOffset | Takes phase and increment as doubles | Cast float masterPhaseIncrement to double |
| equalPowerGains | position=0 gives fadeOut=1, fadeIn=0 | mix=0 means main only (fadeOut=mainGain, fadeIn=subGain) |
| Residual constructor | Takes `const MinBlepTable&` (reference), not pointer | `residual_ = MinBlepTable::Residual(*table_)` |
| MinBlepTable::Residual | Default constructor exists, consume() returns 0.0 | Safe to use before prepare() sets up the real Residual |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

No new Layer 0 utilities are needed. All required utility functions already exist in the codebase.

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| `sanitize()` | Private static helper, same pattern as SyncOscillator/PolyBlepOscillator. Only 1 consumer (SubOscillator). Each oscillator has its own copy for self-containment. |
| `evaluateTriangle()` | Inline sub-expression, specific to triangle waveform math. Single consumer. |
| `evaluateSine()` | Inline sub-expression, single consumer. |

**Decision**: No extraction needed. All new code is specific to the SubOscillator class. The sanitize() pattern is duplicated across oscillators by design (each oscillator is a self-contained header).

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer** (from OSC-ROADMAP):
- Phase 5: `SyncOscillator` (processors/) -- Already implemented
- Phase 7: `UnisonEngine` (processors/) -- Multiple detuned voices
- Phase 8: `FMOperator` (processors/) -- FM synthesis operator

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| `SubOctave` enum | LOW | Only SubOscillator | Keep local |
| `SubWaveform` enum | LOW | Only SubOscillator | Keep local |
| Flip-flop divider | LOW | Possibly clock dividers in future | Keep as member state (no extraction) |
| `sanitize()` pattern | MEDIUM | Already in SyncOscillator, PolyBlepOscillator | Keep as private static (established pattern) |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared base class for oscillators | Each oscillator (PolyBlep, Sync, Sub) has fundamentally different architecture. Forced polymorphism would add virtual call overhead in audio path (Constitution IV). |
| Keep flip-flop as member booleans | Simplest representation. Extracting to a separate class would add indirection for no benefit with only one consumer. |
| Duplicate sanitize() pattern | Established codebase convention: each oscillator header is self-contained. Avoids adding a shared dependency for a 6-line function. |

### Review Trigger

After implementing **Phase 7 UnisonEngine**, review this section:
- [ ] Does UnisonEngine need sub-oscillator per voice? If yes, SubOscillator is already reusable as-is (composition).
- [ ] Any shared patterns between Sub and UnisonEngine? Likely not -- different concerns.

## Project Structure

### Documentation (this feature)

```text
specs/019-sub-oscillator/
+-- plan.md              # This file
+-- research.md          # Phase 0 output (complete)
+-- data-model.md        # Phase 1 output (complete)
+-- quickstart.md        # Phase 1 output (complete)
+-- contracts/
|   +-- sub_oscillator.h # API contract header
+-- checklists/          # Implementation checklists
+-- spec.md              # Feature specification
```

### Source Code (repository root)

```text
dsp/
+-- include/krate/dsp/
|   +-- processors/
|       +-- sub_oscillator.h       # NEW: Header-only SubOscillator implementation
+-- tests/
    +-- unit/processors/
        +-- sub_oscillator_test.cpp # NEW: Unit tests (Catch2)
    +-- CMakeLists.txt              # MODIFIED: Add test file + -fno-fast-math
```

**Structure Decision**: Single header file in `dsp/include/krate/dsp/processors/` following the established Layer 2 pattern. No separate .cpp file needed (header-only, matching SyncOscillator pattern). One test file in the standard test directory.

## Complexity Tracking

No constitution violations. No complexity tracking needed.
