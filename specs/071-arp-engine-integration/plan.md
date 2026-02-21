# Implementation Plan: Arpeggiator Engine Integration

**Branch**: `071-arp-engine-integration` | **Date**: 2026-02-21 | **Spec**: `specs/071-arp-engine-integration/spec.md`
**Input**: Feature specification from `/specs/071-arp-engine-integration/spec.md`

## Summary

Integrate the ArpeggiatorCore (spec 070) into the Ruinae synthesizer plugin by: defining 11 parameter IDs (3000-3010), creating an `ArpeggiatorParams` atomic struct following the `trance_gate_params.h` pattern, modifying the processor to route MIDI through the arp when enabled, extending state serialization, adding basic UI controls in the SEQ tab, and registering all parameters for host automation. This is a pure integration task with zero new DSP code -- all algorithmic work was completed in spec 070.

## Technical Context

**Language/Version**: C++20 (MSVC 2022)
**Primary Dependencies**: VST3 SDK 3.7.x, VSTGUI 4.12+, ArpeggiatorCore (spec 070, Layer 2), BlockContext (Layer 0)
**Storage**: IBStream binary state serialization (VST3 standard)
**Testing**: Catch2 via `ruinae_tests` target *(Constitution Principle XIII)*
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- cross-platform required
**Project Type**: VST3 plugin monorepo
**Performance Goals**: Zero heap allocations on audio thread; parameter changes within one audio block
**Constraints**: Real-time audio thread safety (Constitution Principle II); pre-allocated 128-entry ArpEvent buffer
**Scale/Scope**: 11 new parameters, 1 new file, 6 modified files, 2 new test files

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle I (VST3 Architecture Separation):**
- [x] Processor and Controller remain separate -- no cross-includes
- [x] Processor functions without controller (arp defaults to disabled)
- [x] State flows Host -> Processor -> Controller via setComponentState()

**Principle II (Real-Time Audio Thread Safety):**
- [x] No heap allocations on audio thread -- ArpEvent buffer is `std::array<..., 128>` (stack)
- [x] No locks/mutexes -- atomic params with memory_order_relaxed
- [x] ArpeggiatorCore is pre-allocated, prepare() called in setupProcessing()

**Principle V (VSTGUI Development):**
- [x] UIDescription XML for layout
- [x] All controls bound via control-tags (automatic parameter binding)
- [x] UI thread never accesses audio data directly

**Principle VI (Cross-Platform Compatibility):**
- [x] No platform-specific APIs -- pure VSTGUI abstractions
- [x] `std::atomic` verified lock-free for used types (bool, int, float)

**Principle VIII (Testing Discipline):**
- [x] Tests written BEFORE implementation
- [x] DSP algorithm (ArpeggiatorCore) already has full test suite from spec 070
- [x] Integration tests will verify MIDI routing and parameter round-trips

**Required Check - Principle XIII (Test-First Development):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Required Check - Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Post-Design Re-Check:**
- [x] All design decisions comply with constitution
- [x] No new violations introduced

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: `ArpeggiatorParams`

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| `ArpeggiatorParams` | `grep -r "struct ArpeggiatorParams" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: `handleArpParamChange`, `registerArpParams`, `formatArpParam`, `saveArpParams`, `loadArpParams`, `loadArpParamsToController`

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| `handleArpParamChange` | `grep -r "handleArpParamChange" dsp/ plugins/` | No | N/A | Create New |
| `registerArpParams` | `grep -r "registerArpParams" dsp/ plugins/` | No | N/A | Create New |
| `formatArpParam` | `grep -r "formatArpParam" dsp/ plugins/` | No | N/A | Create New |
| `saveArpParams` | `grep -r "saveArpParams" dsp/ plugins/` | No | N/A | Create New |
| `loadArpParams` | `grep -r "loadArpParams" dsp/ plugins/` | No | N/A | Create New |
| `loadArpParamsToController` | `grep -r "loadArpParamsToController" dsp/ plugins/` | No | N/A | Create New |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| `ArpeggiatorCore` | `dsp/include/krate/dsp/processors/arpeggiator_core.h` | 2 | Composed directly into Processor as member |
| `ArpEvent` | `dsp/include/krate/dsp/processors/arpeggiator_core.h` | 2 | Output event struct from processBlock() |
| `ArpMode` | `dsp/include/krate/dsp/primitives/held_note_buffer.h` | 1 | Enum cast from atomic int param |
| `OctaveMode` | `dsp/include/krate/dsp/primitives/held_note_buffer.h` | 1 | Enum cast from atomic int param |
| `LatchMode` | `dsp/include/krate/dsp/processors/arpeggiator_core.h` | 2 | Enum cast from atomic int param |
| `ArpRetriggerMode` | `dsp/include/krate/dsp/processors/arpeggiator_core.h` | 2 | Enum cast from atomic int param |
| `BlockContext` | `dsp/include/krate/dsp/core/block_context.h` | 0 | Passed to arpCore_.processBlock() |
| `getNoteValueFromDropdown()` | `dsp/include/krate/dsp/core/note_value.h` | 0 | Map dropdown index to NoteValue+NoteModifier |
| `kNoteValueDropdownStrings` | `plugins/ruinae/src/parameters/note_value_ui.h` | N/A | Reuse for arp note value dropdown |
| `createNoteValueDropdown()` | `plugins/ruinae/src/controller/parameter_helpers.h` | N/A | Reuse for arp note value parameter |
| `RuinaeTranceGateParams` | `plugins/ruinae/src/parameters/trance_gate_params.h` | N/A | Reference pattern for all 6 functions |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities (no conflicts)
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (ArpMode, OctaveMode exist, will reuse)
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 (ArpeggiatorCore, LatchMode, ArpRetriggerMode exist, will reuse)
- [x] `plugins/ruinae/src/parameters/` - All existing param files checked, no `ArpeggiatorParams` exists
- [x] `specs/_architecture_/` - Component inventory checked

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: `ArpeggiatorParams` is a unique struct name not found anywhere in the codebase. All enum types (`ArpMode`, `OctaveMode`, `LatchMode`, `ArpRetriggerMode`) are in `Krate::DSP` namespace and are reused, not duplicated. All new functions are `inline` in a header within `Ruinae` namespace, following the established pattern.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| BlockContext | sampleRate | `double sampleRate = 44100.0` | Yes |
| BlockContext | blockSize | `size_t blockSize = 512` | Yes |
| BlockContext | tempoBPM | `double tempoBPM = 120.0` | Yes |
| BlockContext | isPlaying | `bool isPlaying = false` | Yes |
| BlockContext | transportPositionSamples | `int64_t transportPositionSamples = 0` | Yes |
| ArpeggiatorCore | prepare | `void prepare(double sampleRate, size_t maxBlockSize)` | Yes |
| ArpeggiatorCore | reset | `void reset()` | Yes |
| ArpeggiatorCore | noteOn | `void noteOn(uint8_t note, uint8_t velocity)` | Yes |
| ArpeggiatorCore | noteOff | `void noteOff(uint8_t note)` | Yes |
| ArpeggiatorCore | setEnabled | `void setEnabled(bool enabled)` | Yes |
| ArpeggiatorCore | setMode | `void setMode(ArpMode mode)` | Yes |
| ArpeggiatorCore | setOctaveRange | `void setOctaveRange(int range)` | Yes |
| ArpeggiatorCore | setOctaveMode | `void setOctaveMode(OctaveMode mode)` | Yes |
| ArpeggiatorCore | setTempoSync | `void setTempoSync(bool sync)` | Yes |
| ArpeggiatorCore | setNoteValue | `void setNoteValue(NoteValue note, NoteModifier mod)` | Yes |
| ArpeggiatorCore | setFreeRate | `void setFreeRate(float hz)` | Yes |
| ArpeggiatorCore | setGateLength | `void setGateLength(float percent)` | Yes |
| ArpeggiatorCore | setSwing | `void setSwing(float percent)` | Yes |
| ArpeggiatorCore | setLatchMode | `void setLatchMode(LatchMode mode)` | Yes |
| ArpeggiatorCore | setRetrigger | `void setRetrigger(ArpRetriggerMode mode)` | Yes |
| ArpeggiatorCore | processBlock | `size_t processBlock(const BlockContext& ctx, std::span<ArpEvent> outputEvents) noexcept` | Yes |
| ArpEvent | type | `Type type` (enum: NoteOn, NoteOff) | Yes |
| ArpEvent | note | `uint8_t note` | Yes |
| ArpEvent | velocity | `uint8_t velocity` | Yes |
| ArpEvent | sampleOffset | `int32_t sampleOffset` | Yes |
| getNoteValueFromDropdown | - | `NoteValueMapping getNoteValueFromDropdown(int dropdownIndex)` | Yes |

**Note on processBlock**: Verified from header. Takes `std::span<ArpEvent>` (not raw pointer + size). The `std::array<ArpEvent, 128>` member implicitly converts to `std::span<ArpEvent>`. Returns number of events written.

### Header Files Read

- [x] `dsp/include/krate/dsp/core/block_context.h` - BlockContext struct (all fields verified)
- [x] `dsp/include/krate/dsp/processors/arpeggiator_core.h` - ArpeggiatorCore class (all setters + processBlock verified)
- [x] `dsp/include/krate/dsp/primitives/held_note_buffer.h` - ArpMode, OctaveMode enums
- [x] `dsp/include/krate/dsp/core/note_value.h` - getNoteValueFromDropdown, kNoteValueDropdownMapping
- [x] `plugins/ruinae/src/parameters/trance_gate_params.h` - Reference pattern for all functions
- [x] `plugins/ruinae/src/parameters/note_value_ui.h` - Dropdown strings
- [x] `plugins/ruinae/src/controller/parameter_helpers.h` - createNoteValueDropdown helper

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| BlockContext | Member is `tempoBPM` not `tempo` | `ctx.tempoBPM` |
| ArpeggiatorCore | `setEnabled()` may queue cleanup note-offs | Call LAST in applyParamsToEngine so all other params are set first |
| ArpeggiatorCore | `reset()` preserves held-note buffer | Correct for transport stop/start per FR-018 |
| ArpeggiatorCore | `processBlock()` takes `std::span<ArpEvent>`, returns event count | `size_t numEvents = arpCore_.processBlock(ctx, arpEvents_)` -- std::array converts to span |
| NoteValue dropdown | Default index is 10 (1/8 note), NOT 7 (1/16) | `Parameters::kNoteValueDefaultIndex` resolves to 10 |
| Swing | `setSwing()` takes 0-75 percent, NOT 0-1 | Pass raw float from atomic, not normalized |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| All 6 arp param functions | Plugin-specific parameter glue, not reusable DSP |

**Decision**: No Layer 0 extractions needed. This feature creates only plugin-level integration code (parameter handling, MIDI routing), not reusable DSP utilities.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | N/A | This is integration code, not a DSP algorithm |
| **Data parallelism width** | N/A | No sample-level processing -- event-level only |
| **Branch density in inner loop** | N/A | Event routing is O(events-per-block), not O(samples) |
| **Dominant operations** | Memory/branching | Atomic loads, enum casts, function calls |
| **Current CPU budget vs expected usage** | N/A | Negligible CPU cost -- at most ~128 function calls per block |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: This feature contains no DSP processing whatsoever. It is purely integration code: reading atomic parameters, routing MIDI events, and calling ArpeggiatorCore methods. The per-block work is O(parameters + events), which is negligible compared to the synth engine's per-sample processing. SIMD analysis is not applicable to event-level code.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Early-out when arp disabled | Skip all arp processing | LOW | YES (already in design) |
| Cache arp enabled state | Avoid repeated atomic load | LOW | NO (compiler likely optimizes) |

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Plugin integration (Ruinae processor/controller)

**Related features at same layer** (from spec roadmap):
- Phase 4: Independent Lane Architecture -- extends ArpeggiatorParams with lane step parameters
- Phase 5: Slide/Legato -- extends MIDI routing with legato flag on ArpEvent
- Phase 10: Modulation Integration -- exposes arp params as mod destinations
- Phase 11: Full Arp UI -- replaces basic UI with lane editor

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| `ArpeggiatorParams` struct | HIGH | Phase 4 will EXTEND (not replace) with lane params | Keep local, design for extension |
| `arpeggiator_params.h` functions | HIGH | Phase 4 will add new params in same file | Keep local, leave room for growth |
| MIDI routing pattern in processEvents | MEDIUM | Phase 5 may add legato flag routing | Keep simple, document pattern |
| Arp section in editor.uidesc | LOW | Phase 11 will replace entirely | Basic layout is disposable |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| Keep ArpeggiatorParams extensible | Phase 4 will add lane step parameters (3020-3199 range) |
| Use simple if/else for arp routing | Phase 5 may add complexity but current spec only needs on/off branching |
| Basic UI is intentionally minimal | Phase 11 replaces it; investing in polish now wastes effort |
| kArpEndId = 3099 (not 3010) | Reserve range for Phase 4 lane expansion |

### Review Trigger

After implementing **Phase 4 (Independent Lane Architecture)**, review this section:
- [ ] Does Phase 4 need to extend ArpeggiatorParams? -> Yes, add lane step fields
- [ ] Does Phase 4 reuse the param handler pattern? -> Yes, extend handleArpParamChange
- [ ] Any duplicated code? -> Check if note value mapping can be shared

## Project Structure

### Documentation (this feature)

```text
specs/071-arp-engine-integration/
+-- plan.md              # This file
+-- research.md          # Phase 0 output
+-- data-model.md        # Phase 1 output
+-- quickstart.md        # Phase 1 output
+-- contracts/           # Phase 1 output
|   +-- arpeggiator_params_contract.h
|   +-- processor_integration_contract.md
+-- tasks.md             # Phase 2 output (NOT created by plan)
```

### Source Code (repository root)

```text
plugins/ruinae/
+-- src/
|   +-- plugin_ids.h                           # MODIFY: add arp IDs 3000-3010
|   +-- parameters/
|   |   +-- arpeggiator_params.h               # CREATE: atomic struct + 6 functions
|   +-- processor/
|   |   +-- processor.h                        # MODIFY: add arp members
|   |   +-- processor.cpp                      # MODIFY: MIDI routing, params, state
|   +-- controller/
|       +-- controller.h                       # MODIFY: add visibility group pointers
|       +-- controller.cpp                     # MODIFY: register, format, state, visibility
+-- resources/
|   +-- editor.uidesc                          # MODIFY: add arp section to Tab_Seq
+-- tests/
    +-- unit/
        +-- parameters/
        |   +-- arpeggiator_params_test.cpp    # CREATE: param tests
        +-- processor/
            +-- arp_integration_test.cpp       # CREATE: integration tests
```

**Structure Decision**: VST3 plugin monorepo. All changes are within the `plugins/ruinae/` subtree. One new parameter header file, six modified source files, two new test files.

## Complexity Tracking

No constitution violations to justify. All design decisions comply with the constitution.
