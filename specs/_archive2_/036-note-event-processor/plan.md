# Implementation Plan: Note Event Processor

**Branch**: `036-note-event-processor` | **Date**: 2026-02-07 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/036-note-event-processor/spec.md`

## Summary

NoteProcessor is a Layer 2 DSP processor that converts MIDI note numbers to oscillator frequencies with configurable A4 tuning (400-480 Hz), smoothed pitch bend (0-24 semitones range via OnePoleSmoother), and velocity curve mapping (Linear/Soft/Hard/Fixed) with multi-destination depth scaling (amplitude, filter, envelope time). It reuses `midiNoteToFrequency()` from `midi_utils.h`, `semitonesToRatio()` from `pitch_utils.h`, and `OnePoleSmoother` from `smoother.h`. New Layer 0 additions include a `VelocityCurve` enum and `mapVelocity()` free function in `midi_utils.h`.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: KrateDSP Layer 0 (`midi_utils.h`, `pitch_utils.h`, `db_utils.h`), Layer 1 (`smoother.h`)
**Storage**: N/A (stateful but no persistent storage)
**Testing**: Catch2 (via `dsp_tests` target) -- Constitution Principle XII: Test-First Development
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- cross-platform required
**Project Type**: Shared DSP library (header-only class)
**Performance Goals**: `getFrequency()` < 0.1% CPU at 44.1 kHz (Layer 2 budget)
**Constraints**: Real-time safe (zero allocations, no locks, noexcept), all operations O(1)
**Scale/Scope**: Single header file, ~200 lines of implementation

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle I (VST3 Architecture Separation):** N/A -- This is a DSP library component, not a plugin component.

**Principle II (Real-Time Audio Thread Safety):**
- [x] No memory allocations in any processing method
- [x] No locks, mutexes, or blocking primitives
- [x] No exceptions (all methods noexcept)
- [x] No file I/O or system calls
- [x] All buffers pre-allocated at construction

**Principle III (Modern C++ Standards):**
- [x] C++20 features used (designated initializers)
- [x] RAII for all resources (OnePoleSmoother member by value)
- [x] constexpr/const used aggressively
- [x] No raw new/delete

**Principle IV (SIMD & DSP Optimization):**
- [x] SIMD viability analysis completed (see below -- NOT BENEFICIAL)
- [x] Scalar-first workflow applies (no Phase 2 SIMD)

**Principle VI (Cross-Platform Compatibility):**
- [x] No platform-specific code
- [x] `std::pow` used via `semitonesToRatio()` (not constexpr, but fine for runtime)
- [x] `detail::isNaN()`/`detail::isInf()` from `db_utils.h` for NaN handling (works with -ffast-math)

**Principle VIII (Testing Discipline):**
- [x] DSP algorithm is pure function testable without VST infrastructure
- [x] Unit tests cover all DSP algorithms with known input/output pairs

**Principle IX (Layered DSP Architecture):**
- [x] Layer 2 processor depends only on Layer 0 and Layer 1
- [x] No circular dependencies

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Post-Design Re-Check:**
- [x] All design decisions comply with constitution
- [x] No principle violations detected
- [x] New Layer 0 additions (`VelocityCurve`, `mapVelocity()`) are pure stateless functions consistent with existing `midi_utils.h` patterns

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: `NoteProcessor`, `VelocityOutput`, `VelocityCurve`

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| `NoteProcessor` | `grep -r "class NoteProcessor" dsp/ plugins/` | No | Create New |
| `VelocityOutput` | `grep -r "struct VelocityOutput" dsp/ plugins/` | No | Create New |
| `VelocityCurve` | `grep -r "VelocityCurve" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: `mapVelocity`

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| `mapVelocity` | `grep -r "mapVelocity" dsp/ plugins/` | No | midi_utils.h | Create New |
| `velocityToGain` | `grep -r "velocityToGain" dsp/ plugins/` | Yes | midi_utils.h | Reuse (is Linear curve) |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `midiNoteToFrequency()` | `dsp/include/krate/dsp/core/midi_utils.h` | 0 | 12-TET frequency conversion in `getFrequency()` |
| `velocityToGain()` | `dsp/include/krate/dsp/core/midi_utils.h` | 0 | Linear velocity curve (called by `mapVelocity()`) |
| `kA4FrequencyHz` | `dsp/include/krate/dsp/core/midi_utils.h` | 0 | Default A4 reference constant |
| `kA4MidiNote` | `dsp/include/krate/dsp/core/midi_utils.h` | 0 | MIDI note 69 constant |
| `kMinMidiVelocity` | `dsp/include/krate/dsp/core/midi_utils.h` | 0 | Velocity range constant |
| `kMaxMidiVelocity` | `dsp/include/krate/dsp/core/midi_utils.h` | 0 | Velocity range constant |
| `semitonesToRatio()` | `dsp/include/krate/dsp/core/pitch_utils.h` | 0 | Pitch bend ratio computation |
| `OnePoleSmoother` | `dsp/include/krate/dsp/primitives/smoother.h` | 1 | Pitch bend exponential smoothing |
| `detail::isNaN()` | `dsp/include/krate/dsp/core/db_utils.h` | 0 | NaN input validation |
| `detail::isInf()` | `dsp/include/krate/dsp/core/db_utils.h` | 0 | Inf input validation |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities -- no NoteProcessor, VelocityCurve, or VelocityOutput found
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives -- no conflicts
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors -- no note_processor.h exists; MonoHandler is a sibling
- [x] `specs/_architecture_/` - Component inventory -- NoteProcessor not listed

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All three planned types (`NoteProcessor`, `VelocityOutput`, `VelocityCurve`) are unique and not found anywhere in the codebase. The only existing velocity function (`velocityToGain()`) will be complemented, not duplicated. The `mapVelocity()` function name is also unique.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| `midi_utils.h` | `midiNoteToFrequency` | `[[nodiscard]] constexpr float midiNoteToFrequency(int midiNote, float a4Frequency = kA4FrequencyHz) noexcept` | Yes |
| `midi_utils.h` | `velocityToGain` | `[[nodiscard]] constexpr float velocityToGain(int velocity) noexcept` | Yes |
| `midi_utils.h` | `kA4FrequencyHz` | `inline constexpr float kA4FrequencyHz = 440.0f` | Yes |
| `midi_utils.h` | `kA4MidiNote` | `inline constexpr int kA4MidiNote = 69` | Yes |
| `midi_utils.h` | `kMinMidiVelocity` | `inline constexpr int kMinMidiVelocity = 0` | Yes |
| `midi_utils.h` | `kMaxMidiVelocity` | `inline constexpr int kMaxMidiVelocity = 127` | Yes |
| `pitch_utils.h` | `semitonesToRatio` | `[[nodiscard]] inline float semitonesToRatio(float semitones) noexcept` | Yes |
| `smoother.h` | `OnePoleSmoother()` | `OnePoleSmoother() noexcept` | Yes |
| `smoother.h` | `configure` | `void configure(float smoothTimeMs, float sampleRate) noexcept` | Yes |
| `smoother.h` | `setTarget` | `ITERUM_NOINLINE void setTarget(float target) noexcept` | Yes |
| `smoother.h` | `process` | `[[nodiscard]] float process() noexcept` | Yes |
| `smoother.h` | `snapTo` | `void snapTo(float value) noexcept` | Yes |
| `smoother.h` | `setSampleRate` | `void setSampleRate(float sampleRate) noexcept` | Yes |
| `smoother.h` | `getCurrentValue` | `[[nodiscard]] float getCurrentValue() const noexcept` | Yes |
| `smoother.h` | `reset` | `void reset() noexcept` | Yes |
| `db_utils.h` | `detail::isNaN` | `constexpr bool isNaN(float x) noexcept` | Yes |
| `db_utils.h` | `detail::isInf` | `[[nodiscard]] constexpr bool isInf(float x) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/midi_utils.h` - midiNoteToFrequency, velocityToGain, constants
- [x] `dsp/include/krate/dsp/core/pitch_utils.h` - semitonesToRatio
- [x] `dsp/include/krate/dsp/primitives/smoother.h` - OnePoleSmoother full API
- [x] `dsp/include/krate/dsp/core/db_utils.h` - detail::isNaN, detail::isInf
- [x] `dsp/include/krate/dsp/processors/mono_handler.h` - Reference for Layer 2 processor pattern
- [x] `dsp/include/krate/dsp/systems/voice_allocator.h` - Reference for computeFrequency pattern

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| `OnePoleSmoother` | `setTarget(NaN)` resets both target AND current to 0.0 | Must guard NaN/Inf *before* calling setTarget for FR-020 |
| `OnePoleSmoother` | `snapTo()` sets both current and target, `snapToTarget()` only sets current | Use `snapTo(0.0f)` for reset, `snapToTarget()` for instant convergence |
| `OnePoleSmoother` | `setSampleRate()` preserves current & target, recalculates coefficient only | Correct behavior for FR-003 mid-transition sample rate change |
| `semitonesToRatio` | Uses `std::pow(2.0f, semitones / 12.0f)` -- NOT constexpr | Fine for runtime; do not mark NoteProcessor methods constexpr |
| `midiNoteToFrequency` | Takes `int` not `uint8_t` for midiNote parameter | Cast uint8_t to int when calling |
| `velocityToGain` | Uses manual min/max clamp, NOT `std::clamp` (constexpr compatible) | Follow same pattern in new velocity functions if constexpr needed |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| `VelocityCurve` enum | General MIDI concept, reusable | `midi_utils.h` | NoteProcessor, VoiceAllocator, MonoHandler |
| `mapVelocity(int, VelocityCurve)` | Pure stateless velocity mapping | `midi_utils.h` | NoteProcessor, VoiceAllocator, future voice engines |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| `getFrequency()` | Depends on instance state (smoother, tuning, bend ratio) |
| `processPitchBend()` | Advances smoother state |
| `mapVelocity(int)` (member) | Wraps Layer 0 `mapVelocity()` + applies depth scaling -- requires instance state |

**Decision**: Extract `VelocityCurve` enum and `mapVelocity(int, VelocityCurve)` free function to `midi_utils.h`. NoteProcessor's member `mapVelocity(int)` delegates to the Layer 0 function and applies per-destination depth scaling.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | NO | Pitch bend smoother has sample-to-sample dependency but is O(1) per block, not per voice |
| **Data parallelism width** | 1 | `getFrequency()` processes one note at a time; caller iterates voices |
| **Branch density in inner loop** | LOW | `getFrequency()` is branchless (1 function call + 1 multiply) |
| **Dominant operations** | Transcendental | `midiNoteToFrequency` (constexprExp), `semitonesToRatio` (std::pow) |
| **Current CPU budget vs expected usage** | <0.1% vs ~0.01% | Well under budget; `getFrequency()` is 2 function calls + 1 multiply |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: `getFrequency()` processes a single scalar value per call (one note per voice). There is no data parallelism to exploit unless the caller batches multiple notes, which would require an API change. The per-call cost is dominated by two transcendental function evaluations, and the total CPU usage is far below the 0.1% Layer 2 budget. Optimization effort would be wasted.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Pre-cache `bendRatio_` in `processPitchBend()` | Eliminates 1 `semitonesToRatio()` call per voice | LOW | YES |
| Early-out when bend range is 0 | Skip bend computation entirely | LOW | YES (trivial) |
| Fast exp2 approximation for `midiNoteToFrequency` | ~20% per call | MEDIUM | NO (already uses constexprExp, under budget) |

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 2 - DSP Processors

**Related features at same layer** (from roadmap and existing codebase):
- MonoHandler (spec 035): Monophonic note handling with portamento
- Future polyphonic synth voice processor
- Self-oscillating filter (spec 088): Uses `midiNoteToFrequency` for key tracking

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| `VelocityCurve` enum (Layer 0) | HIGH | VoiceAllocator, MonoHandler, any voice engine | Extract to midi_utils.h now |
| `mapVelocity()` free function (Layer 0) | HIGH | Same as above | Extract to midi_utils.h now |
| `NoteProcessor` class (Layer 2) | HIGH | MonoHandler (compose), VoiceAllocator (compose), future poly voice | Keep in own header |

### Detailed Analysis (for HIGH potential items)

**`VelocityCurve` enum + `mapVelocity()` function** provides:
- Four standard velocity curve types used across synthesizer designs
- Pure stateless velocity-to-gain mapping reusable by any voice engine

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| VoiceAllocator | YES | Currently only uses linear velocity; could adopt VelocityCurve |
| MonoHandler | YES | Returns raw velocity; caller could use mapVelocity for processing |
| Future synth voice | YES | Standard velocity processing |

**Recommendation**: Extract to `midi_utils.h` now -- 3+ clear consumers.

**`NoteProcessor` class** provides:
- Smoothed pitch bend with range configuration
- Tuning reference management with validation
- Multi-destination velocity routing

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| MonoHandler | MAYBE | Could compose NoteProcessor for pitch bend smoothing (currently has none) |
| VoiceAllocator | MAYBE | Could delegate frequency computation to NoteProcessor |
| Future synth voice | YES | Would compose NoteProcessor for per-voice frequency + velocity |

**Recommendation**: Keep in own header. Refactoring VoiceAllocator/MonoHandler to compose NoteProcessor is a separate task.

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Extract VelocityCurve + mapVelocity to Layer 0 | 3+ consumers identified, pure stateless functions |
| Keep NoteProcessor in Layer 2 | Stateful class, specific composition pattern |
| Do not refactor VoiceAllocator now | Would be a separate spec; current implementation works correctly |

### Review Trigger

After implementing **future synth voice system**, review this section:
- [ ] Does the synth voice compose NoteProcessor? If so, document shared pattern
- [ ] Should VoiceAllocator delegate to NoteProcessor? Evaluate code duplication
- [ ] Should MonoHandler gain pitch bend smoothing via NoteProcessor composition?

## Project Structure

### Documentation (this feature)

```text
specs/036-note-event-processor/
├── plan.md              # This file
├── research.md          # Phase 0 output -- research decisions
├── data-model.md        # Phase 1 output -- entity definitions
├── quickstart.md        # Phase 1 output -- usage guide
├── contracts/           # Phase 1 output -- API contract
│   └── note_processor_api.h
└── tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
dsp/
├── include/krate/dsp/
│   ├── core/
│   │   └── midi_utils.h            # MODIFIED: Add VelocityCurve enum + mapVelocity()
│   └── processors/
│       └── note_processor.h        # NEW: NoteProcessor class + VelocityOutput struct
├── tests/
│   └── unit/processors/
│       └── note_processor_test.cpp  # NEW: Unit tests
├── CMakeLists.txt                   # MODIFIED: Add note_processor.h to header list
└── tests/CMakeLists.txt             # MODIFIED: Add test file + -fno-fast-math flag

specs/_architecture_/
└── layer-2-processors.md            # MODIFIED: Add NoteProcessor documentation
```

**Structure Decision**: Standard KrateDSP monorepo layout. Header-only processor in `dsp/include/krate/dsp/processors/`, tests in `dsp/tests/unit/processors/`. Layer 0 additions go in existing `midi_utils.h`.

## Complexity Tracking

No constitution violations detected. No complexity tracking needed.
