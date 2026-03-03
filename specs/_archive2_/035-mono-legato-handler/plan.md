# Implementation Plan: Mono/Legato Handler

**Branch**: `035-mono-legato-handler` | **Date**: 2026-02-07 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/035-mono-legato-handler/spec.md`

## Summary

Implement a Layer 2 Processor DSP component for monophonic note handling with legato and portamento. The `MonoHandler` class manages a 16-entry fixed-capacity note stack, implements three note priority modes (LastNote, LowNote, HighNote), provides legato mode for envelope retrigger suppression, and offers constant-time portamento that operates linearly in pitch space (semitones). The portamento engine reuses the existing `LinearRamp` from `smoother.h` operating in semitone space, converting to frequency at output via `semitonesToRatio()`. The component is header-only, real-time safe, and resides at `dsp/include/krate/dsp/processors/mono_handler.h`.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: KrateDSP Layer 0 (midi_utils.h, pitch_utils.h, db_utils.h), Layer 1 (smoother.h LinearRamp)
**Storage**: N/A (no persistent storage, all in-memory)
**Testing**: Catch2 (via dsp_tests target) *(Constitution Principle XII: Test-First Development)*
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- cross-platform
**Project Type**: DSP library component (header-only in monorepo)
**Performance Goals**: noteOn() < 500ns average (SC-009), processPortamento() < 50ns per sample
**Constraints**: Zero heap allocation after construction (FR-031), sizeof(MonoHandler) <= 512 bytes (SC-012), single audio thread only (FR-033-threading)
**Scale/Scope**: Single header file (~300-400 lines), single test file (~800-1000 lines)

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle I (VST3 Architecture Separation):** Not applicable -- this is a DSP-only component at Layer 2, not a plugin component.

**Principle II (Real-Time Audio Thread Safety):**
- [x] No memory allocation in any method after construction
- [x] No locks, mutexes, or blocking primitives
- [x] No file I/O, network ops, or system calls
- [x] No throw/catch (all methods noexcept per FR-032)
- [x] Fixed-size note stack (16 entries) pre-allocated at construction
- [x] LinearRamp pre-allocated at construction

**Principle III (Modern C++ Standards):**
- [x] C++20 with constexpr, const, value semantics
- [x] std::array (no C-style arrays)
- [x] No raw new/delete

**Principle IV (SIMD & DSP Optimization):**
- [x] SIMD analysis completed (see below -- verdict: NOT BENEFICIAL)
- [x] Scalar-first workflow followed

**Principle IX (Layered DSP Architecture):**
- [x] Layer 2 depends only on Layer 0 + Layer 1 (verified)
- [x] No circular dependencies

**Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Principle XV (Pre-Implementation Research):**
- [x] Searched for all planned type names -- no conflicts found
- [x] Checked specs/_architecture_/ -- no existing MonoHandler

**Principle XVI (Honest Completion):**
- [x] All FR-xxx and SC-xxx will be individually verified at completion

**Post-Design Re-check**: All principles remain satisfied. The design uses only Layer 0 and Layer 1 dependencies, returns small aggregates by value, and pre-allocates all state. No constitution violations.

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: MonoNoteEvent, MonoHandler, NoteEntry (internal)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| MonoNoteEvent | `grep -r "struct MonoNoteEvent" dsp/ plugins/` | No | Create New |
| MonoHandler | `grep -r "class MonoHandler" dsp/ plugins/` | No | Create New |
| MonoMode | `grep -r "MonoMode" dsp/ plugins/` | No | Create New |
| PortaMode | `grep -r "PortaMode" dsp/ plugins/` | No | Create New |
| NoteEntry | `grep -r "struct NoteEntry" dsp/ plugins/` | No | Create New (internal, unnamed namespace or private) |

**Utility Functions to be created**: semitoneToFrequency (private helper)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| semitoneToFrequency | `grep -r "semitoneToFrequency" dsp/ plugins/` | No | mono_handler.h (private static) | Create New (wraps existing semitonesToRatio) |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| midiNoteToFrequency() | dsp/include/krate/dsp/core/midi_utils.h | 0 | Convert MIDI note to frequency for MonoNoteEvent.frequency |
| kA4FrequencyHz | dsp/include/krate/dsp/core/midi_utils.h | 0 | Reference frequency for portamento semitone-to-Hz conversion |
| kA4MidiNote | dsp/include/krate/dsp/core/midi_utils.h | 0 | Reference note number for semitone calculations |
| semitonesToRatio() | dsp/include/krate/dsp/core/pitch_utils.h | 0 | Convert portamento semitone position to frequency ratio |
| detail::isNaN() | dsp/include/krate/dsp/core/db_utils.h | 0 | Guard portamento time setter against NaN |
| detail::isInf() | dsp/include/krate/dsp/core/db_utils.h | 0 | Guard portamento time setter against Inf |
| LinearRamp | dsp/include/krate/dsp/primitives/smoother.h | 1 | Portamento glide interpolation in semitone space |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 existing processors (60+ files, none conflict)
- [x] `specs/_architecture_/` - Component inventory (README.md, layer-2-processors.md)

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All five planned type names (MonoNoteEvent, MonoHandler, MonoMode, PortaMode, NoteEntry) are completely unique in the codebase. Grep searches confirmed zero matches. The `NoteEntry` name will be kept private (nested struct) to avoid any future collisions.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| midi_utils.h | midiNoteToFrequency | `[[nodiscard]] constexpr float midiNoteToFrequency(int midiNote, float a4Frequency = kA4FrequencyHz) noexcept` | Yes |
| midi_utils.h | kA4FrequencyHz | `inline constexpr float kA4FrequencyHz = 440.0f` | Yes |
| midi_utils.h | kA4MidiNote | `inline constexpr int kA4MidiNote = 69` | Yes |
| pitch_utils.h | semitonesToRatio | `[[nodiscard]] inline float semitonesToRatio(float semitones) noexcept` | Yes |
| db_utils.h | detail::isNaN | `constexpr bool isNaN(float x) noexcept` | Yes |
| db_utils.h | detail::isInf | `[[nodiscard]] constexpr bool isInf(float x) noexcept` | Yes |
| smoother.h | LinearRamp::configure | `void configure(float rampTimeMs, float sampleRate) noexcept` | Yes |
| smoother.h | LinearRamp::setTarget | `ITERUM_NOINLINE void setTarget(float target) noexcept` | Yes |
| smoother.h | LinearRamp::process | `[[nodiscard]] float process() noexcept` | Yes |
| smoother.h | LinearRamp::getCurrentValue | `[[nodiscard]] float getCurrentValue() const noexcept` | Yes |
| smoother.h | LinearRamp::snapTo | `void snapTo(float value) noexcept` | Yes |
| smoother.h | LinearRamp::isComplete | `[[nodiscard]] bool isComplete() const noexcept` | Yes |
| smoother.h | LinearRamp::setSampleRate | `void setSampleRate(float sampleRate) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/midi_utils.h` - midiNoteToFrequency, kA4FrequencyHz, kA4MidiNote
- [x] `dsp/include/krate/dsp/core/pitch_utils.h` - semitonesToRatio, ratioToSemitones
- [x] `dsp/include/krate/dsp/core/db_utils.h` - detail::isNaN, detail::isInf, detail::flushDenormal
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - LinearRamp full API

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| LinearRamp | `configure()` recalculates increment for in-progress transition | Call after changing portamento time or sample rate |
| LinearRamp | `setTarget()` uses ITERUM_NOINLINE for NaN protection under /fp:fast | Always use setTarget(), never write to internal state directly |
| LinearRamp | When rampTimeMs <= 0, `calculateLinearIncrement` returns delta (instant but not snap) | Use `snapTo()` for truly instant pitch changes (zero portamento) |
| midiNoteToFrequency | Uses `detail::constexprExp()` (Taylor series), not `std::pow` | Accuracy is sufficient for float (< 0.01 Hz at all MIDI notes) |
| semitonesToRatio | Uses `std::pow(2.0f, semitones / 12.0f)` -- runtime function | Fine for per-sample portamento output conversion |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

No new Layer 0 utilities are needed. All required conversions (MIDI-to-frequency, semitone-to-ratio) already exist.

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| findWinner() | Operates on internal note stack state, not a pure function |
| updatePortamentoTarget() | Manages internal LinearRamp + mode logic |
| semitoneToFrequency() | One-liner wrapper around semitonesToRatio, only 1 consumer |

**Decision**: No Layer 0 extraction needed. All new code is class-specific logic.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | NO | No sample-to-sample feedback. Portamento is a simple linear ramp. |
| **Data parallelism width** | 1 | Single monophonic voice. No parallel streams to vectorize. |
| **Branch density in inner loop** | LOW | processPortamento() hot path: 1 ramp process + 1 pow call. No branches. |
| **Dominant operations** | transcendental | std::pow in semitonesToRatio per sample. Everything else is trivial arithmetic. |
| **Current CPU budget vs expected usage** | < 0.5% budget, ~0.01% expected | Single pow per sample + linear ramp addition. Negligible CPU. |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: The MonoHandler processes a single monophonic voice, providing a data parallelism width of 1. There is nothing to vectorize -- no multiple voices, no multi-channel processing, no batch operations. The hot path (processPortamento) is a single linear ramp step plus one std::pow call. Total CPU usage is negligible (~0.01% at 44.1kHz). SIMD would add complexity with zero benefit.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Skip pow when portamento complete | ~100% in steady-state (no glide) | LOW | YES |
| Fast exp2 approximation for semitonesToRatio | ~20-30% of pow call | LOW | DEFER (negligible total CPU) |

**Implementation note**: The `LinearRamp::isComplete()` early-out is already built into `process()`. When no glide is active, `process()` returns immediately with the cached target value. The `semitoneToFrequency()` call still happens per sample, but at ~5-10ns per call, optimizing this is not worthwhile.

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 2 - Processors

**Related features at same layer** (from synth-roadmap.md):
- Phase 2.3: Note Event Processor (pitch bend, velocity mapping -- would consume MonoNoteEvent)
- Other Layer 2 processors (filters, oscillators, etc.) -- unrelated to note handling

**Related features at higher layers**:
- Phase 3.1: Basic Synth Voice (Layer 3) -- would use MonoHandler for mono mode
- Phase 3.2: Polyphonic Synth Engine (Layer 3) -- would compose MonoHandler (mono) + VoiceAllocator (poly)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| MonoNoteEvent struct | HIGH | Note Event Processor (Phase 2.3), Synth Voice (Phase 3.1), Synth Engine (Phase 3.2) | Keep in mono_handler.h for now, extract to shared header if Phase 2.3 needs it independently |
| MonoMode / PortaMode enums | MEDIUM | Synth Engine (Phase 3.2) for mode selection UI parameters | Keep in mono_handler.h |
| Internal note stack | LOW | Possibly arpeggiator or chord memory in far future | Keep as private implementation detail |

### Detailed Analysis (for HIGH potential items)

**MonoNoteEvent** provides:
- Frequency (Hz) for oscillator pitch
- Velocity (0-127) for amplitude/filter envelope
- Retrigger flag for envelope gate control
- isNoteOn flag for note-on/note-off signaling

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| Note Event Processor (Phase 2.3) | YES | Would consume MonoNoteEvent and add pitch bend to frequency |
| Basic Synth Voice (Phase 3.1) | YES | Would receive MonoNoteEvent to control oscillator + envelopes |

**Recommendation**: Keep MonoNoteEvent in `mono_handler.h` for now. When Phase 2.3 or 3.1 implementation begins, evaluate whether to extract to a shared `note_event_types.h` in `core/` or `processors/`.

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep MonoNoteEvent in mono_handler.h | Only one producer (MonoHandler) exists currently. Extract when a second independent producer or consumer at a different layer emerges. |
| No shared NoteStack utility | Arpeggiator and chord memory are speculative future features. Do not abstract prematurely. |

### Review Trigger

After implementing **Phase 2.3 Note Event Processor**, review this section:
- [ ] Does Note Event Processor need MonoNoteEvent independently? -> Extract to shared header
- [ ] Does Note Event Processor use similar note stack logic? -> Consider shared utility
- [ ] Any duplicated code? -> Consider shared utilities

## Project Structure

### Documentation (this feature)

```text
specs/035-mono-legato-handler/
├── plan.md              # This file
├── spec.md              # Feature specification
├── research.md          # Phase 0: Research findings
├── data-model.md        # Phase 1: Entity and state model
├── quickstart.md        # Phase 1: Implementation quickstart guide
├── contracts/           # Phase 1: API contracts
│   └── mono_handler_api.md
├── checklists/          # Pre-existing checklists
└── tasks.md             # Phase 2: Task breakdown (created by /speckit.tasks)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   └── processors/
│       └── mono_handler.h         # NEW: Header-only MonoHandler (Layer 2)
└── tests/
    └── unit/processors/
        └── mono_handler_test.cpp  # NEW: Catch2 unit tests
```

**Files modified:**
- `dsp/CMakeLists.txt` -- add `mono_handler.h` to `KRATE_DSP_PROCESSORS_HEADERS`
- `dsp/tests/CMakeLists.txt` -- add `mono_handler_test.cpp` to test target + `-fno-fast-math` list
- `specs/_architecture_/layer-2-processors.md` -- add MonoHandler documentation

**Structure Decision**: Standard DSP library pattern. Header-only implementation in `dsp/include/krate/dsp/processors/` with tests in `dsp/tests/unit/processors/`. Matches existing Layer 2 components like `multi_stage_envelope.h`, `noise_generator.h`, etc.

## Complexity Tracking

No constitution violations. No complexity tracking needed.
