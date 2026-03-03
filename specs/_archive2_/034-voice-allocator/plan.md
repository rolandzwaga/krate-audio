# Implementation Plan: Voice Allocator

**Branch**: `034-voice-allocator` | **Date**: 2026-02-07 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/034-voice-allocator/spec.md`

## Summary

Implement a polyphonic voice allocator as a Layer 3 system component in KrateDSP (`dsp/include/krate/dsp/systems/voice_allocator.h`). The allocator manages a pool of up to 32 voice slots, mapping MIDI note events to voice indices with configurable allocation modes (RoundRobin, Oldest, LowestVelocity, HighestNote), voice stealing (Hard/Soft), unison support (1-8 voices per note with detune), and thread-safe state queries for UI access. It is a pure routing engine -- no DSP processing -- that produces `VoiceEvent` instructions for a higher-level synth engine to act on. All internal data is pre-allocated. Returns events via `std::span<const VoiceEvent>` for zero-allocation operation.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: Layer 0 only: `midi_utils.h` (midiNoteToFrequency, kA4FrequencyHz), `pitch_utils.h` (semitonesToRatio), `db_utils.h` (detail::isNaN, detail::isInf). Standard library: `<array>`, `<atomic>`, `<span>`, `<cstdint>`, `<cstddef>`, `<algorithm>`.
**Storage**: N/A (no persistence, no file I/O)
**Testing**: Catch2 unit tests at `dsp/tests/unit/systems/voice_allocator_test.cpp` *(Constitution Principle XIII: Test-First Development)*
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- cross-platform C++20
**Project Type**: Header-only DSP library component in monorepo
**Performance Goals**: < 1 microsecond per noteOn() call (SC-008), < 4096 bytes total instance size (SC-009)
**Constraints**: Real-time safe (no alloc, no lock, no exception, no I/O), all methods noexcept
**Scale/Scope**: 1 header file (~500 lines), 1 test file (~1500 lines), 46 FRs + 12 SCs

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle I (VST3 Architecture Separation)**: N/A -- pure DSP library component, no Processor/Controller.

**Principle II (Real-Time Safety)**: PASS -- All methods are real-time safe. No allocation (fixed-size std::array), no locks (atomics for thread-safe queries), no exceptions (noexcept on all methods), no I/O. Pre-allocated event buffer returned via std::span.

**Principle III (Modern C++)**: PASS -- C++20 (std::span, std::atomic, std::array), RAII, constexpr, no raw new/delete.

**Principle IV (SIMD & Optimization)**: PASS -- SIMD analysis completed below (NOT BENEFICIAL).

**Principle VI (Cross-Platform)**: PASS -- Pure C++ with no platform-specific code. std::atomic<uint8_t> is lock-free on all target platforms.

**Principle VII (Project Structure)**: PASS -- Layer 3 system at `dsp/include/krate/dsp/systems/`, tests at `dsp/tests/unit/systems/`.

**Principle VIII (Testing)**: PASS -- Test-first development. All 46 FRs will have corresponding tests.

**Principle IX (Layered Architecture)**: PASS -- Layer 3 depends only on Layer 0 (core). No Layer 1 or Layer 2 dependencies.

**Principle XV (ODR Prevention)**: PASS -- All planned types searched and confirmed unique in codebase.

**Principle XVI (Honest Completion)**: Acknowledged -- compliance table will be filled with specific evidence.

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

**Classes/Structs to be created**: VoiceAllocator, VoiceEvent, VoiceState (enum), AllocationMode (enum), StealMode (enum)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| VoiceAllocator | `grep -r "VoiceAllocator" dsp/ plugins/` | No | Create New |
| VoiceEvent | `grep -r "VoiceEvent" dsp/ plugins/` | No | Create New |
| VoiceState | `grep -r "VoiceState" dsp/ plugins/` | No | Create New |
| AllocationMode | `grep -r "AllocationMode" dsp/ plugins/` | No | Create New |
| StealMode | `grep -r "StealMode" dsp/ plugins/` | No | Create New |

**ODR Verification Output (2026-02-07)**:
```
$ grep -rE "class VoiceAllocator|struct VoiceEvent|enum class VoiceState|enum class AllocationMode|enum class StealMode" dsp/ plugins/
(no output — zero matches in dsp/ and plugins/)
```

All 5 planned types confirmed unique across the entire codebase.

**Utility Functions to be created**: None -- all utility functions reuse existing Layer 0.

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| midiNoteToFrequency() | dsp/include/krate/dsp/core/midi_utils.h | 0 | Compute frequency from MIDI note + tuning reference |
| semitonesToRatio() | dsp/include/krate/dsp/core/pitch_utils.h | 0 | Pitch bend ratio and unison detune offset |
| detail::isNaN() | dsp/include/krate/dsp/core/db_utils.h | 0 | Guard NaN on float setters |
| detail::isInf() | dsp/include/krate/dsp/core/db_utils.h | 0 | Guard Inf on float setters |
| kA4FrequencyHz | dsp/include/krate/dsp/core/midi_utils.h | 0 | Default A4 reference constant |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/systems/` - Layer 3 systems (no voice allocator exists)
- [x] `specs/_architecture_/` - Component inventory (no voice allocator listed)
- [x] `plugins/` - No VoiceAllocator/VoiceEvent/VoiceState types in plugin code

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All 5 planned types (VoiceAllocator, VoiceEvent, VoiceState, AllocationMode, StealMode) are completely unique. No matches found anywhere in the codebase. The names are specific enough to avoid accidental conflicts with future components.

## Dependency API Contracts (Principle XV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| midi_utils.h | midiNoteToFrequency | `[[nodiscard]] constexpr float midiNoteToFrequency(int midiNote, float a4Frequency = kA4FrequencyHz) noexcept` | Yes |
| midi_utils.h | kA4FrequencyHz | `inline constexpr float kA4FrequencyHz = 440.0f` | Yes |
| pitch_utils.h | semitonesToRatio | `[[nodiscard]] inline float semitonesToRatio(float semitones) noexcept` | Yes |
| db_utils.h | detail::isNaN | `constexpr bool isNaN(float x) noexcept` | Yes |
| db_utils.h | detail::isInf | `[[nodiscard]] constexpr bool isInf(float x) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/midi_utils.h` - midiNoteToFrequency, kA4FrequencyHz
- [x] `dsp/include/krate/dsp/core/pitch_utils.h` - semitonesToRatio
- [x] `dsp/include/krate/dsp/core/db_utils.h` - detail::isNaN, detail::isInf

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| midiNoteToFrequency | Takes `int` for note, not `uint8_t` | Cast: `midiNoteToFrequency(static_cast<int>(note), a4Frequency_)` |
| midiNoteToFrequency | Second param is `float a4Frequency`, defaults to 440 | Must pass `a4Frequency_` member for custom tuning |
| semitonesToRatio | Uses `std::pow`, not `constexpr` -- is `inline` not `constexpr` | Fine for runtime use, cannot be constexpr |
| detail::isNaN | In `detail` namespace, not directly in `Krate::DSP` | Must use `detail::isNaN()` not `isNaN()` |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| findIdleVoice() | Voice allocation strategy, specific to this class |
| findStealVictim() | Steal logic, specific to this class |
| computeFrequency() | Trivial wrapper around midiNoteToFrequency + semitonesToRatio |
| pushEvent() | Internal event buffer management |

**Decision**: No Layer 0 extractions needed. All new functionality is voice-management-specific. The allocator composes existing Layer 0 utilities without needing to create new shared functions.

## SIMD Optimization Analysis

*GATE: Must complete during planning. Constitution Principle IV.*

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | NO | Pure event routing, no sample-to-sample dependencies |
| **Data parallelism width** | 32 voices | Could process voice slots in SIMD lanes |
| **Branch density in inner loop** | HIGH | Allocation mode strategy requires per-voice state comparisons, conditional logic |
| **Dominant operations** | Comparison/branching | Finding victims is min/max search with state filtering |
| **Current CPU budget vs expected usage** | < 1% budget vs ~0.01% expected | Already 100x under budget in scalar |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: The voice allocator is an event-driven control component, not a sample-processing DSP algorithm. Its hot path (noteOn) runs once per note event (not per sample), takes ~170ns in scalar, and is dominated by branching logic (state checks, mode-specific comparisons). SIMD excels at uniform arithmetic on many data points, not branch-heavy search operations. The 1-microsecond budget provides 5x headroom with scalar code.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Early-out when idle voice found | ~50% for non-stealing case | LOW | YES |
| State-filtered scan (releasing first, then active) | ~20% for stealing case | LOW | YES |
| Cache-line alignment of VoiceSlot array | Marginal (already L1-resident) | LOW | NO (unnecessary) |

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 3 - System Components

**Related features at same layer** (from synth-roadmap.md):
- Phase 2.2: Mono/Legato Handler -- separate voice management strategy for monophonic mode
- Phase 3.1: Basic Synth Voice -- single voice DSP combining oscillator + envelope + filter
- Phase 3.2: Polyphonic Synth Engine -- primary consumer, composes VoiceAllocator + voice pool

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| VoiceEvent struct | HIGH | Mono/Legato Handler, PolyphonicSynthEngine | Keep in voice_allocator.h for now, extract to voice_types.h if Mono/Legato Handler also uses it |
| VoiceState enum | HIGH | Mono/Legato Handler, SynthVoice, PolyphonicSynthEngine | Same as above |
| AllocationMode enum | LOW | Only VoiceAllocator uses this | Keep local |
| StealMode enum | LOW | Only VoiceAllocator uses this | Keep local |

### Detailed Analysis (for HIGH potential items)

**VoiceEvent** provides:
- Event type classification (NoteOn, NoteOff, Steal)
- Voice index targeting
- Pre-computed frequency for voice DSP

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| Mono/Legato Handler | MAYBE | Could use same event struct, but mono mode may need additional fields (legato flag, portamento) |
| PolyphonicSynthEngine | YES | Direct consumer of VoiceEvent spans |

**VoiceState** provides:
- Three-state lifecycle model (Idle, Active, Releasing)

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| SynthVoice | YES | Needs to track its own state for envelope detection |
| PolyphonicSynthEngine | YES | Queries voice states for display |

**Recommendation**: Keep VoiceEvent and VoiceState in voice_allocator.h for now. When Phase 2.2 (Mono/Legato Handler) is implemented, evaluate whether to extract to a shared `voice_types.h`. Two confirmed consumers would justify extraction.

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep all types in voice_allocator.h | Only one consumer so far. Premature extraction adds files without proven need. |
| VoiceEvent as aggregate struct | Simple, no constructors needed, aggregate initialization, trivially copyable |
| VoiceState as scoped enum (uint8_t) | Fits in std::atomic for thread-safe queries |

### Review Trigger

After implementing **Phase 2.2 (Mono/Legato Handler)**, review this section:
- [ ] Does Mono/Legato Handler need VoiceEvent or similar? -> Extract to voice_types.h
- [ ] Does Mono/Legato Handler use VoiceState? -> Extract to voice_types.h
- [ ] Any duplicated voice management patterns? -> Consider shared base

## Project Structure

### Documentation (this feature)

```text
specs/034-voice-allocator/
├── plan.md              # This file
├── research.md          # Phase 0 research findings
├── data-model.md        # Entity definitions and relationships
├── quickstart.md        # Usage examples and integration guide
├── contracts/
│   └── voice_allocator_api.h  # Public API contract
├── checklists/
│   └── requirements.md  # Specification quality checklist
└── tasks.md             # Phase 2 task breakdown (created by /speckit.tasks)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── systems/
│       └── voice_allocator.h     # Header-only implementation (new)
└── tests/
    └── unit/systems/
        └── voice_allocator_test.cpp  # Catch2 unit tests (new)
```

**Structure Decision**: Header-only implementation in the existing KrateDSP monorepo structure. Single header file at Layer 3 (systems). Single test file at the corresponding test location. Follows the established pattern used by FMVoice, UnisonEngine, and other Layer 3 systems.

**Build integration**: Two files need updating:
1. `dsp/CMakeLists.txt` -- add `voice_allocator.h` to `KRATE_DSP_SYSTEMS_HEADERS`
2. `dsp/tests/CMakeLists.txt` -- add `unit/systems/voice_allocator_test.cpp` to both the source list and the `-fno-fast-math` property list (for NaN guard tests)

## Complexity Tracking

No constitution violations to justify. All design decisions align with the constitution:
- Layer 3 depends only on Layer 0 (Principle IX)
- All methods noexcept, no allocation in audio path (Principle II)
- Header-only in correct directory structure (Principle VII)
- Test-first development (Principle XIII)
- ODR prevention verified (Principle XV)

## Post-Design Constitution Re-Check

Re-evaluated all principles after completing Phase 1 design:

| Principle | Status | Notes |
|-----------|--------|-------|
| I. VST3 Separation | N/A | Pure library component |
| II. Real-Time Safety | PASS | No alloc, no lock, no exception, no I/O. std::atomic for queries. |
| III. Modern C++ | PASS | C++20, std::span, std::array, std::atomic, constexpr, noexcept |
| IV. SIMD | PASS | Evaluated as NOT BENEFICIAL, documented reasoning |
| V. VSTGUI | N/A | No UI |
| VI. Cross-Platform | PASS | Pure C++20, no platform-specific code |
| VII. Structure | PASS | Correct layer/directory |
| VIII. Testing | PASS | Test file planned with comprehensive coverage |
| IX. Layers | PASS | Layer 3 -> Layer 0 only |
| X. DSP Constraints | N/A | Not a sample-processing component |
| XI. Performance | PASS | Well under Layer 3 budget (< 1% CPU) |
| XII. Debug Discipline | Acknowledged | Will investigate before pivoting |
| XIII. Test-First | PASS | Tests before implementation |
| XIV. Architecture Docs | Will update after implementation |
| XV. ODR Prevention | PASS | All names verified unique |
| XVI. Honest Completion | Acknowledged | Will verify each FR/SC individually |

All gates pass. No violations.
