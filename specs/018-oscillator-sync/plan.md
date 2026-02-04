# Implementation Plan: Oscillator Sync

**Branch**: `018-oscillator-sync` | **Date**: 2026-02-04 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/018-oscillator-sync/spec.md`

## Summary

Implement a Layer 2 `SyncOscillator` processor that composes a master `PhaseAccumulator` with a slave `PolyBlepOscillator` and `MinBlepTable::Residual` to produce band-limited synchronized oscillator output. Supports three sync modes (Hard, Reverse, PhaseAdvance) with a continuous sync amount control. Hard sync uses minBLEP correction for step discontinuities at reset points. Reverse sync uses minBLAMP correction for derivative discontinuities at reversal points. Phase advance provides gradual sync from no effect (0.0) to full hard sync (1.0). Also extends `MinBlepTable` with minBLAMP support as a prerequisite.

## Technical Context

**Language/Version**: C++20 (MSVC, Clang, GCC)
**Primary Dependencies**: `core/phase_utils.h`, `core/math_constants.h`, `core/db_utils.h`, `primitives/polyblep_oscillator.h`, `primitives/minblep_table.h`
**Storage**: N/A (header-only DSP library, no persistent storage)
**Testing**: Catch2 (dsp_tests target) with spectral_analysis.h test helper for FFT-based aliasing measurement *(Constitution Principle XII)*
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- cross-platform header-only
**Project Type**: DSP library (monorepo, shared KrateDSP lib)
**Performance Goals**: ~100-150 cycles/sample per voice (SC-015)
**Constraints**: Real-time safe process() -- no allocation, no exceptions, no blocking, no I/O. Master frequency clamped to sampleRate/2 (at most one phase wrap per sample).
**Scale/Scope**: 1 new header (sync_oscillator.h), 1 modified header (minblep_table.h), 1 new test file, 1 modified test file

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle I (VST3 Architecture Separation):** N/A -- this is a DSP library component, not a plugin component.

**Principle II (Real-Time Audio Thread Safety):**
- [x] `process()` and `processBlock()` are noexcept, no allocation, no locks, no I/O
- [x] `prepare()` is explicitly NOT real-time safe (documented)
- [x] No std::vector operations in the audio path (Residual buffer pre-allocated in prepare)

**Principle III (Modern C++):**
- [x] C++20, constexpr where applicable, [[nodiscard]], noexcept
- [x] No raw new/delete (PolyBlepOscillator embedded by value, Residual uses std::vector internally)
- [x] RAII lifecycle (prepare/reset pattern)

**Principle IX (Layered Architecture):**
- [x] SyncOscillator at Layer 2 (processors/) -- depends only on Layer 0 and Layer 1
- [x] MinBlepTable extension at Layer 1 -- no new dependencies added
- [x] No circular dependencies

**Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Principle XVI (Honest Completion):**
- [x] All SC-xxx will be measured and reported with actual values
- [x] No threshold relaxation from spec requirements

**Post-Design Re-Check:**
- [x] Contract API aligns with constitution principles
- [x] No violations introduced during design
- [x] Layer dependencies strictly maintained

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**:

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| `SyncOscillator` | `grep -r "class SyncOscillator" dsp/ plugins/` | No | Create New |
| `SyncMode` | `grep -r "SyncMode" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**:

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| `evaluateWaveform` | `grep -r "evaluateWaveform" dsp/ plugins/` | No | sync_oscillator.h (private) | Create New |
| `evaluateWaveformDerivative` | `grep -r "evaluateWaveformDerivative" dsp/ plugins/` | No | sync_oscillator.h (private) | Create New |
| `sampleBlamp` | `grep -r "sampleBlamp\|addBlamp" dsp/ plugins/` | No | minblep_table.h (extension) | Create New |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `PolyBlepOscillator` | `primitives/polyblep_oscillator.h` | 1 | Slave oscillator (composed by value) |
| `OscWaveform` | `primitives/polyblep_oscillator.h` | 1 | Waveform enum for slave selection |
| `MinBlepTable` | `primitives/minblep_table.h` | 1 | Discontinuity correction table (const pointer) |
| `MinBlepTable::Residual` | `primitives/minblep_table.h` | 1 | Ring buffer for BLEP/BLAMP corrections |
| `PhaseAccumulator` | `core/phase_utils.h` | 0 | Master phase tracking |
| `calculatePhaseIncrement` | `core/phase_utils.h` | 0 | Frequency to phase increment conversion |
| `wrapPhase` | `core/phase_utils.h` | 0 | Phase wrapping to [0, 1) |
| `subsamplePhaseWrapOffset` | `core/phase_utils.h` | 0 | Sub-sample accuracy for minBLEP placement |
| `kPi`, `kTwoPi` | `core/math_constants.h` | 0 | Sine/cosine in waveform evaluation |
| `detail::isNaN`, `detail::isInf` | `core/db_utils.h` | 0 | Input sanitization (works with -ffast-math) |
| `Interpolation::linearInterpolate` | `core/interpolation.h` | 0 | Used by MinBlepTable::sample (internal) |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities (no conflicts)
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (MinBlepTable will be extended)
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (no existing sync_oscillator)
- [x] `specs/_architecture_/` - Component inventory (SyncOscillator not listed)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (`SyncOscillator`, `SyncMode`) are confirmed absent from the entire codebase via grep search. The `MinBlepTable` extension adds new methods without modifying existing ones. No naming conflicts possible.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| PhaseAccumulator | phase | `double phase = 0.0` | Yes |
| PhaseAccumulator | increment | `double increment = 0.0` | Yes |
| PhaseAccumulator | advance | `[[nodiscard]] bool advance() noexcept` | Yes |
| PhaseAccumulator | reset | `void reset() noexcept` | Yes |
| PhaseAccumulator | setFrequency | `void setFrequency(float frequency, float sampleRate) noexcept` | Yes |
| calculatePhaseIncrement | (free function) | `[[nodiscard]] constexpr double calculatePhaseIncrement(float frequency, float sampleRate) noexcept` | Yes |
| wrapPhase | (free function) | `[[nodiscard]] constexpr double wrapPhase(double phase) noexcept` | Yes |
| subsamplePhaseWrapOffset | (free function) | `[[nodiscard]] constexpr double subsamplePhaseWrapOffset(double phase, double increment) noexcept` | Yes |
| PolyBlepOscillator | prepare | `void prepare(double sampleRate) noexcept` | Yes |
| PolyBlepOscillator | reset | `void reset() noexcept` | Yes |
| PolyBlepOscillator | setFrequency | `void setFrequency(float hz) noexcept` | Yes |
| PolyBlepOscillator | setWaveform | `void setWaveform(OscWaveform waveform) noexcept` | Yes |
| PolyBlepOscillator | setPulseWidth | `void setPulseWidth(float width) noexcept` | Yes |
| PolyBlepOscillator | process | `[[nodiscard]] float process() noexcept` | Yes |
| PolyBlepOscillator | phase | `[[nodiscard]] double phase() const noexcept` | Yes |
| PolyBlepOscillator | resetPhase | `void resetPhase(double newPhase = 0.0) noexcept` | Yes |
| MinBlepTable | isPrepared | `[[nodiscard]] bool isPrepared() const noexcept` | Yes |
| MinBlepTable | length | `[[nodiscard]] size_t length() const noexcept` | Yes |
| MinBlepTable | sample | `[[nodiscard]] float sample(float subsampleOffset, size_t index) const noexcept` | Yes |
| Residual | constructor | `explicit Residual(const MinBlepTable& table)` | Yes |
| Residual | addBlep | `void addBlep(float subsampleOffset, float amplitude) noexcept` | Yes |
| Residual | consume | `[[nodiscard]] float consume() noexcept` | Yes |
| Residual | reset | `void reset() noexcept` | Yes |
| detail::isNaN | (free function) | `constexpr bool isNaN(float x) noexcept` | Yes |
| detail::isInf | (free function) | `[[nodiscard]] constexpr bool isInf(float x) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/phase_utils.h` - PhaseAccumulator, phase utility functions
- [x] `dsp/include/krate/dsp/core/math_constants.h` - kPi, kTwoPi
- [x] `dsp/include/krate/dsp/core/db_utils.h` - detail::isNaN, detail::isInf
- [x] `dsp/include/krate/dsp/core/interpolation.h` - Interpolation::linearInterpolate
- [x] `dsp/include/krate/dsp/core/polyblep.h` - polyBlep4, polyBlamp4 (reference)
- [x] `dsp/include/krate/dsp/primitives/polyblep_oscillator.h` - PolyBlepOscillator, OscWaveform
- [x] `dsp/include/krate/dsp/primitives/minblep_table.h` - MinBlepTable, Residual

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| PhaseAccumulator | `advance()` returns bool (wrapped), increments THEN checks | Call advance(), check return, then read phase |
| PhaseAccumulator | `phase` and `increment` are `double`, not `float` | Use double for phase calculations |
| PolyBlepOscillator | `process()` reads phase THEN advances | Call resetPhase() BEFORE process() for sync reset |
| PolyBlepOscillator | `setWaveform(Triangle)` clears integrator | Waveform switch during processing resets triangle state |
| PolyBlepOscillator | FM/PM offsets reset to 0 after each process() | Not relevant for sync (we do not use FM/PM on the slave) |
| MinBlepTable | `Residual` constructor takes `const MinBlepTable&` (reference) | Must construct after table is prepared |
| MinBlepTable | `addBlep` formula: `correction[i] = amplitude * (table.sample(offset, i) - 1.0)` | The -1.0 accounts for the step function settling to 1.0 |
| subsamplePhaseWrapOffset | Returns `phase / increment` (fraction of sample past wrap) | Offset is in [0, 1) when increment is positive and phase just wrapped |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| `evaluateWaveform` | Naive waveform evaluation at arbitrary phase; could be reused by PhaseDistortionOscillator (Phase 10), SubOscillator (Phase 6) | `core/waveform_utils.h` or keep local | SyncOscillator, possibly Phase 6, Phase 10 |
| `evaluateWaveformDerivative` | Waveform derivative; only needed for reverse sync BLAMP scaling | Keep local | SyncOscillator only |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| `evaluateWaveform` | Only 2 potential consumers. Extract after Phase 6 (SubOscillator) if it needs it. |
| `evaluateWaveformDerivative` | Only 1 consumer (reverse sync). Highly specific to sync. |
| `processHardSync` | Mode-specific logic, internal to SyncOscillator |
| `processReverseSync` | Mode-specific logic, internal to SyncOscillator |
| `processPhaseAdvanceSync` | Mode-specific logic, internal to SyncOscillator |
| `sanitize` | Same pattern as PolyBlepOscillator::sanitize, but duplicated intentionally (static, no shared state) |

**Decision**: All new utility functions kept as private static methods in `SyncOscillator` for now. `evaluateWaveform` is a candidate for extraction to Layer 0 if Phase 6 (SubOscillator) or Phase 10 (PhaseDistortion) need it. The function is pure and stateless, making future extraction trivial.

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer** (from OSC-ROADMAP.md):
- Phase 6: SubOscillator (L2) -- frequency-divided oscillator tracking a master
- Phase 8: FMOperator (L2) -- frequency/phase modulation operator
- Phase 10: PhaseDistortionOscillator (L2) -- PD synthesis with windowed sync variants

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| `SyncMode` enum | LOW | SyncOscillator only | Keep local |
| `evaluateWaveform()` | MEDIUM | Phase 6 (SubOsc), Phase 10 (PD Osc) | Keep local, extract after 2nd use |
| Master-slave phase tracking pattern | MEDIUM | Phase 6 (SubOsc uses master wrap detection) | Document pattern, don't extract yet |
| MinBLAMP extension to MinBlepTable | HIGH | Phase 10 (PD resonant types use windowed sync), any future component with derivative discontinuities | Already in shared Layer 1 component |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep `SyncMode` in sync_oscillator.h | Specific to sync semantics; not reusable by other processors |
| Keep `evaluateWaveform` private static | Only 1 confirmed consumer. SubOsc may not need it (uses flip-flop, not waveform evaluation). Extract if Phase 6 needs it. |
| Add minBLAMP to MinBlepTable (shared Layer 1) | MinBLAMP is a general-purpose correction. Phase 10 (PD resonant types) may need it. Adding to the shared table benefits all consumers. |

### Review Trigger

After implementing **Phase 6 (SubOscillator)**, review this section:
- [ ] Does SubOsc need `evaluateWaveform` or similar? If yes, extract to `core/waveform_utils.h`.
- [ ] Does SubOsc use the same master-slave composition pattern? If yes, document shared pattern.
- [ ] Any duplicated code between SyncOsc and SubOsc? Consider shared utilities.

## Project Structure

### Documentation (this feature)

```text
specs/018-oscillator-sync/
├── plan.md              # This file
├── spec.md              # Feature specification
├── research.md          # Phase 0 research output
├── data-model.md        # Phase 1 data model
├── quickstart.md        # Phase 1 quickstart guide
├── contracts/
│   ├── sync_oscillator.h          # SyncOscillator API contract
│   └── minblep_table_extension.h  # MinBLAMP extension contract
└── checklists/
    └── requirements.md  # Requirements checklist
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   ├── core/
│   │   ├── phase_utils.h          # [REUSE] PhaseAccumulator, phase utilities
│   │   ├── math_constants.h       # [REUSE] kPi, kTwoPi
│   │   ├── db_utils.h             # [REUSE] detail::isNaN, detail::isInf
│   │   └── polyblep.h             # [REFERENCE] polyBlep4, polyBlamp4
│   ├── primitives/
│   │   ├── polyblep_oscillator.h  # [REUSE] PolyBlepOscillator, OscWaveform
│   │   └── minblep_table.h        # [MODIFY] Add minBLAMP table + sampleBlamp + addBlamp
│   └── processors/
│       └── sync_oscillator.h      # [NEW] SyncOscillator + SyncMode enum
└── tests/
    ├── CMakeLists.txt             # [MODIFY] Add sync_oscillator_test.cpp
    └── unit/
        ├── primitives/
        │   └── minblep_table_test.cpp  # [MODIFY] Add minBLAMP tests
        └── processors/
            └── sync_oscillator_test.cpp  # [NEW] All sync oscillator tests
```

**Structure Decision**: Standard DSP library pattern. New header at Layer 2 (processors/). Prerequisite modification at Layer 1 (primitives/minblep_table.h). Test files follow the mirror structure convention.

## Complexity Tracking

No constitution violations. No complexity justifications needed.
