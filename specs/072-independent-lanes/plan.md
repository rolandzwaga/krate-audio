# Implementation Plan: Independent Lane Architecture

**Branch**: `072-independent-lanes` | **Date**: 2026-02-21 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/072-independent-lanes/spec.md`

## Summary

Add three independent-length step lanes (velocity, gate, pitch) to the ArpeggiatorCore via a generic `ArpLane<T>` template container (DSP Layer 1 primitive). Each lane cycles at its own configured length, producing polymetric arpeggiator patterns. The system adds 99 VST3 parameters for per-step host automation, extends the state serialization with full backward compatibility, and maintains bit-identical output when all lanes are at default values.

## Technical Context

**Language/Version**: C++20 (header-only DSP, compiled plugin code)
**Primary Dependencies**: VST3 SDK 3.7.x, VSTGUI 4.12+, Catch2 (testing)
**Storage**: Binary IBStream (VST3 state serialization, EOF-safe loading)
**Testing**: Catch2 (dsp_tests for Layer 1/2, ruinae_tests for plugin params), pluginval L5
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- cross-platform
**Project Type**: Monorepo (shared DSP lib + multiple plugins)
**Performance Goals**: ArpLane::advance() < 0.001% CPU (called 1-50x/sec); total arp < 0.1% CPU
**Constraints**: Zero heap allocation in all audio-thread paths; bit-identical backward compat (SC-002)
**Scale/Scope**: 1 new file (arp_lane.h), 1 new test file (arp_lane_test.cpp), 5-7 modified files

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Pre-Design Check (PASSED)**

**Required Check - Principle II (Real-Time Safety):**
- [x] ArpLane uses std::array (no heap allocation)
- [x] All methods marked noexcept
- [x] No locks, exceptions, or I/O in any path

**Required Check - Principle III (Modern C++):**
- [x] Template class with value semantics
- [x] std::array over C-style arrays
- [x] constexpr where applicable

**Required Check - Principle IV (SIMD):**
- [x] SIMD viability analyzed -- NOT BENEFICIAL (see research.md R9)
- [x] Lane operations are per-step (1-50 Hz), not per-sample -- too infrequent for SIMD

**Required Check - Principle IX (Layered Architecture):**
- [x] ArpLane is Layer 1 (primitives) -- depends only on standard library
- [x] ArpeggiatorCore is Layer 2 (processors) -- composes Layer 1 ArpLane
- [x] No circular dependencies

**Required Check - Principle XII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Required Check - Principle XVI (Honest Completion):**
- [x] SC-002 (bit-identical) will use strict comparison, no tolerance relaxation
- [x] All 34 FRs and 7 SCs will be verified individually

**Post-Design Re-Check (PASSED)**
- [x] No constitution violations in design
- [x] All layer dependencies flow downward
- [x] No platform-specific code required
- [x] Parameter patterns follow established trance_gate_params.h precedent

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: ArpLane<T>

| Planned Type | Search Pattern | Existing? | Action |
|--------------|----------------|-----------|--------|
| ArpLane | `grep -r "ArpLane" dsp/ plugins/` | No | Create New |
| (no other new types) | -- | -- | -- |

**Utility Functions to be created**: resetLanes (private method in ArpeggiatorCore)

| Planned Function | Search Pattern | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| resetLanes | `grep -r "resetLanes" dsp/ plugins/` | No | N/A | Create New (private method) |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| ArpeggiatorCore | `dsp/include/krate/dsp/processors/arpeggiator_core.h` | 2 | Extended with lane members and modified fireStep() |
| HeldNoteBuffer | `dsp/include/krate/dsp/primitives/held_note_buffer.h` | 1 | Design pattern reference (fixed-capacity, array-backed) |
| NoteSelector | `dsp/include/krate/dsp/primitives/held_note_buffer.h` | 1 | Called in fireStep() -- provides base notes/velocities |
| ArpNoteResult | `dsp/include/krate/dsp/primitives/held_note_buffer.h` | 1 | Modified in fireStep() with lane values |
| ArpeggiatorParams | `plugins/ruinae/src/parameters/arpeggiator_params.h` | Plugin | Extended with lane atomic storage |
| RuinaeTranceGateParams | `plugins/ruinae/src/parameters/trance_gate_params.h` | Plugin | Pattern reference for per-step parameter handling |
| ParameterIDs enum | `plugins/ruinae/src/plugin_ids.h` | Plugin | Extended with lane parameter IDs |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities -- no conflicts
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives -- no "ArpLane" exists
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors -- ArpeggiatorCore to be extended
- [x] `plugins/ruinae/src/parameters/` - Plugin params -- ArpeggiatorParams to be extended
- [x] `plugins/ruinae/src/plugin_ids.h` - Parameter IDs -- range 3020-3132 is unoccupied
- [x] `specs/_architecture_/` - Component inventory

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: The only new type (ArpLane) does not exist anywhere in the codebase. It lives in the `Krate::DSP` namespace at Layer 1. All modifications to existing types (ArpeggiatorCore, ArpeggiatorParams, ParameterIDs) are extensions, not replacements.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| ArpeggiatorCore | reset() | `inline void reset() noexcept` | Yes |
| ArpeggiatorCore | fireStep() | `inline void fireStep(const BlockContext& ctx, int32_t sampleOffset, std::span<ArpEvent> outputEvents, size_t& eventCount, size_t maxEvents, size_t samplesProcessed, size_t blockSize) noexcept` | Yes |
| ArpeggiatorCore | calculateGateDuration() | `inline size_t calculateGateDuration() const noexcept` | Yes |
| ArpeggiatorCore | noteOn() | `inline void noteOn(uint8_t note, uint8_t velocity) noexcept` | Yes |
| NoteSelector | advance() | `ArpNoteResult advance(const HeldNoteBuffer& held)` (inferred from usage) | Yes |
| NoteSelector | reset() | `void reset()` (inferred from usage) | Yes |
| ArpNoteResult | notes | `std::array<uint8_t, 32> notes{}` | Yes |
| ArpNoteResult | velocities | `std::array<uint8_t, 32> velocities{}` | Yes |
| ArpNoteResult | count | `size_t count{0}` | Yes |
| ArpeggiatorParams | enabled | `std::atomic<bool> enabled{false}` | Yes |
| ArpeggiatorParams | gateLength | `std::atomic<float> gateLength{80.0f}` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/processors/arpeggiator_core.h` - Full file (849 lines)
- [x] `dsp/include/krate/dsp/primitives/held_note_buffer.h` - ArpNoteResult, NoteSelector
- [x] `plugins/ruinae/src/parameters/arpeggiator_params.h` - Full file (434 lines)
- [x] `plugins/ruinae/src/parameters/trance_gate_params.h` - Full file (395 lines)
- [x] `plugins/ruinae/src/plugin_ids.h` - Full file (866 lines)
- [x] `plugins/ruinae/src/processor/processor.cpp` - Arp integration sections
- [x] `plugins/ruinae/src/processor/processor.h` - Arp member declarations
- [x] `plugins/ruinae/src/controller/controller.cpp` - setComponentState, formatArpParam

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| ArpeggiatorCore | `selector_.reset()` is called inside `setMode()` -- setting mode unconditionally every block prevents arp from advancing | Only call `setMode()` when mode actually changes |
| ArpeggiatorCore | `calculateGateDuration()` casts through double then to size_t | Must maintain same cast chain when adding lane multiplier for bit-identical compat |
| ArpeggiatorParams | handleArpParamChange dispatches on `id >= kArpBaseId && id <= kArpEndId` | kArpEndId must be updated from 3099 to 3199 |
| Plugin processor | `applyParamsToArp` only sets values when they change (to avoid resetting arp state) | Lane step values can be set unconditionally (they don't trigger resets) |
| State persistence | loadArpParams returns false on EOF (backward compat) | Lane data loading must also handle false returns gracefully |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| resetLanes() | Private to ArpeggiatorCore, only 1 consumer |
| calculateGateDuration(float) | Extension of existing method, private to ArpeggiatorCore |

**Decision**: No Layer 0 extractions needed. All new functionality is either in the ArpLane template (Layer 1) or private modifications to ArpeggiatorCore (Layer 2).

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | NO | Lane state is read-only per step; no sample-to-sample feedback |
| **Data parallelism width** | 3 lanes | Only 3 independent lanes, and they have different types (float, float, int8_t) |
| **Branch density in inner loop** | N/A | advance() is called 1-50x/sec, not in a per-sample loop |
| **Dominant operations** | memory (array read + index increment) | ~4 instructions per advance() call |
| **Current CPU budget vs expected usage** | < 0.1% budget vs < 0.001% expected | Massive headroom |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: Lane operations execute 1-50 times per second (once per arp step), not per sample. Each advance() is a single array read and index modulo -- about 4 instructions. The entire arp including lanes is well under 0.1% CPU. SIMD would add complexity with zero measurable benefit.

### Alternative Optimizations

None needed. The algorithm is already minimal.

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 1 (ArpLane) + Layer 2 (ArpeggiatorCore extension)

**Related features at same layer** (from arpeggiator-roadmap.md):
- Phase 5: Per-Step Modifiers (Rest/Tie/Slide/Accent bitmask lane)
- Phase 6: Ratcheting (ratchet count lane, 1-4)
- Phase 7: Euclidean Timing (may generate patterns into lanes)
- Phase 8: Conditional Trig (condition enum lane)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| ArpLane<T> | HIGH | Phase 5 (uint8_t), Phase 6 (uint8_t), Phase 8 (uint8_t) | Extract now -- it IS the shared component |
| resetLanes() | HIGH | Phase 5-8 (each adds a lane that must reset) | Keep as private method, extend per phase |
| Lane parameter registration pattern | HIGH | Phase 5-8 (similar per-step params) | Document pattern, replicate per phase |

### Detailed Analysis (for HIGH potential items)

**ArpLane<T, MaxSteps>** provides:
- Fixed-capacity step storage with configurable length
- Independent position tracking with wrap
- Reset capability
- Random access to step values

| Sibling Feature | Would Reuse? | Notes |
|-----------------|--------------|-------|
| Phase 5 (Modifiers) | YES | `ArpLane<uint8_t>` for bitmask (Rest/Tie/Slide/Accent flags) |
| Phase 6 (Ratcheting) | YES | `ArpLane<uint8_t>` for ratchet count (1-4) |
| Phase 7 (Euclidean) | MAYBE | May use ArpLane as output target for generated patterns |
| Phase 8 (Conditions) | YES | `ArpLane<uint8_t>` for TrigCondition enum values |

**Recommendation**: ArpLane IS the shared component. It is already designed as a template to support all future lane types. No further extraction needed -- Phases 5-8 will instantiate `ArpLane<uint8_t>` directly.

### Decision Log

| Decision | Rationale |
|----------|-----------|
| ArpLane is a template, not a base class | Avoids virtual dispatch in audio path; each phase instantiates the template with its value type |
| resetLanes() is a private method, not a virtual hook | Future phases add one line to resetLanes() -- simple and explicit |
| Lane parameter registration uses a loop pattern | Matches trance_gate_params.h; easy to replicate for new lane types |

### Review Trigger

After implementing **Phase 5 (Per-Step Modifiers)**, review this section:
- [ ] Does Phase 5 use ArpLane<uint8_t> as designed? Or does it need a different interface?
- [ ] Does resetLanes() scale cleanly with a 4th lane?
- [ ] Any parameter registration code that should be shared as a helper?

## Project Structure

### Documentation (this feature)

```text
specs/072-independent-lanes/
+-- plan.md              # This file
+-- research.md          # Phase 0 output
+-- data-model.md        # Phase 1 output
+-- quickstart.md        # Phase 1 output
+-- contracts/           # Phase 1 output
|   +-- arp_lane.h       # ArpLane API contract
|   +-- arpeggiator_core_extension.md
|   +-- parameter_ids.md
+-- spec.md              # Feature specification
+-- checklists/          # Setup script output
+-- tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
dsp/
+-- include/krate/dsp/
|   +-- primitives/
|   |   +-- arp_lane.h              # NEW: ArpLane<T> template (Layer 1)
|   +-- processors/
|       +-- arpeggiator_core.h      # MODIFIED: Add lanes to fireStep/reset
+-- tests/unit/
    +-- primitives/
    |   +-- arp_lane_test.cpp        # NEW: ArpLane unit tests
    +-- processors/
        +-- arpeggiator_core_test.cpp # MODIFIED: Lane integration tests

plugins/ruinae/
+-- src/
|   +-- plugin_ids.h                # MODIFIED: Add 99 lane parameter IDs
|   +-- parameters/
|   |   +-- arpeggiator_params.h    # MODIFIED: Extend with lane storage + 6 functions
|   +-- processor/
|   |   +-- processor.cpp           # MODIFIED: Apply lane params in applyParamsToArp
|   +-- controller/
|       +-- controller.cpp          # MODIFIED: Lane param formatting + dispatch
+-- tests/unit/
    +-- parameters/
    |   +-- arpeggiator_params_test.cpp # MODIFIED: Lane param tests
    +-- processor/
        +-- arp_integration_test.cpp    # MODIFIED: Lane integration tests

specs/_architecture_/
+-- layer-1-primitives.md          # MODIFIED: Document ArpLane
+-- layer-2-processors.md          # MODIFIED: Update ArpeggiatorCore
+-- plugin-parameter-system.md     # MODIFIED: Document lane parameter IDs
+-- plugin-state-persistence.md    # MODIFIED: Document lane serialization
```

**Structure Decision**: This feature follows the existing monorepo layout. ArpLane is a new Layer 1 primitive with its own header and test file. All other changes are extensions to existing files, following established patterns.

## Complexity Tracking

No constitution violations to justify. All design decisions align with established patterns.
