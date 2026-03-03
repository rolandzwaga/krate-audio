# Implementation Plan: Per-Step Modifiers (Slide, Accent, Tie, Rest)

**Branch**: `073-per-step-mods` | **Date**: 2026-02-21 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/073-per-step-mods/spec.md`

## Summary

Add TB-303-inspired per-step modifier flags (Rest, Tie, Slide, Accent) as a bitmask lane (`ArpLane<uint8_t>`) to the ArpeggiatorCore (DSP Layer 2). Extend `ArpEvent` with a `bool legato` field for slide behavior. Extend `RuinaeEngine::noteOn()` with a defaulted `bool legato` parameter to support legato transitions without envelope retrigger. Add per-voice portamento to `RuinaeVoice` for Poly mode slide. Register 35 new VST3 parameters for modifier lane automation. Maintain full backward compatibility with Phase 4 presets.

## Technical Context

**Language/Version**: C++20 (header-only DSP, compiled plugin code)
**Primary Dependencies**: VST3 SDK 3.7.x, VSTGUI 4.12+, Catch2 (testing)
**Storage**: Binary IBStream (VST3 state serialization, EOF-safe loading)
**Testing**: Catch2 (dsp_tests for Layer 2, ruinae_tests for plugin params), pluginval L5
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- cross-platform
**Project Type**: Monorepo (shared DSP lib + multiple plugins)
**Performance Goals**: Modifier evaluation < 0.001% CPU (called 1-50x/sec); total arp < 0.1% CPU
**Constraints**: Zero heap allocation in all audio-thread paths; bit-identical backward compat (SC-002)
**Scale/Scope**: 0 new files, 8-10 modified files, 35 new parameters

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Pre-Design Check (PASSED)**

**Required Check - Principle II (Real-Time Safety):**
- [x] Modifier lane uses ArpLane<uint8_t> with std::array (no heap allocation)
- [x] ArpStepFlags is a uint8_t enum (no dynamic dispatch)
- [x] All modifier evaluation is branchless-friendly bitwise operations (value & kFlag)
- [x] No locks, exceptions, or I/O in any modified path

**Required Check - Principle III (Modern C++):**
- [x] ArpStepFlags uses typed enum with explicit underlying type
- [x] ArpEvent legato field uses default member initializer
- [x] std::clamp used for all value bounds checking

**Required Check - Principle IV (SIMD):**
- [x] SIMD viability analyzed -- NOT BENEFICIAL (see research.md R9)
- [x] Modifier evaluation is per-step (1-50 Hz), not per-sample -- too infrequent for SIMD
- [x] Per-voice portamento ramp is per-sample but single-voice (1 float mul + compare per sample)

**Required Check - Principle IX (Layered Architecture):**
- [x] ArpStepFlags and modifier lane additions are in Layer 2 (processors, arpeggiator_core.h)
- [x] ArpLane<uint8_t> reuse is Layer 1 (primitives, arp_lane.h) -- no changes to ArpLane itself
- [x] RuinaeEngine/RuinaeVoice are plugin-level (not in shared DSP library)
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
- [x] All 34 FRs and 10 SCs will be verified individually

**Post-Design Re-Check (PASSED)**
- [x] No constitution violations in design
- [x] All layer dependencies flow downward
- [x] No platform-specific code required
- [x] Parameter patterns follow established Phase 4 precedent
- [x] RuinaeVoice portamento uses exponential frequency interpolation (perceptually correct)

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: ArpStepFlags (enum only)

| Planned Type | Search Pattern | Existing? | Action |
|---|---|---|---|
| ArpStepFlags | `grep -r "ArpStepFlags" dsp/ plugins/` | No | Create New |
| (no other new types) | -- | -- | -- |

**Utility Functions to be created**: None (all new code is inline methods on existing classes)

| Planned Function | Search Pattern | Existing? | Location | Action |
|---|---|---|---|---|
| setAccentVelocity | `grep -r "setAccentVelocity" dsp/ plugins/` | No | N/A | Create New (method) |
| setSlideTime | `grep -r "setSlideTime" dsp/ plugins/` | No | N/A | Create New (method) |
| dispatchPolyLegatoNoteOn | `grep -r "dispatchPolyLegato" dsp/ plugins/` | No | N/A | Create New (method) |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|---|---|---|---|
| ArpeggiatorCore | `dsp/include/krate/dsp/processors/arpeggiator_core.h` | 2 | Extended with modifier lane, fireStep() modified |
| ArpEvent | `dsp/include/krate/dsp/processors/arpeggiator_core.h:56-63` | 2 | Extended with `bool legato{false}` field |
| ArpLane<uint8_t> | `dsp/include/krate/dsp/primitives/arp_lane.h` | 1 | Reused as modifier lane container. No changes to ArpLane. |
| resetLanes() | `dsp/include/krate/dsp/processors/arpeggiator_core.h:873-877` | 2 | Extended with modifierLane_.reset() + tieActive_=false |
| ArpeggiatorParams | `plugins/ruinae/src/parameters/arpeggiator_params.h` | Plugin | Extended with modifier lane atomic storage |
| handleArpParamChange | `plugins/ruinae/src/parameters/arpeggiator_params.h:80-194` | Plugin | Extended with modifier parameter dispatch |
| registerArpParams | `plugins/ruinae/src/parameters/arpeggiator_params.h:202-329` | Plugin | Extended with modifier parameter registration |
| formatArpParam | `plugins/ruinae/src/parameters/arpeggiator_params.h:336-472` | Plugin | Extended with modifier parameter formatting |
| saveArpParams | `plugins/ruinae/src/parameters/arpeggiator_params.h:478-514` | Plugin | Extended with modifier lane serialization |
| loadArpParams | `plugins/ruinae/src/parameters/arpeggiator_params.h:522-597` | Plugin | Extended with EOF-safe modifier lane deserialization |
| loadArpParamsToController | `plugins/ruinae/src/parameters/arpeggiator_params.h:604-669` | Plugin | Extended with modifier param controller sync |
| RuinaeEngine | `plugins/ruinae/src/engine/ruinae_engine.h` | Plugin | noteOn() extended with legato param, setPortamentoTime() extended |
| RuinaeVoice | `plugins/ruinae/src/engine/ruinae_voice.h` | Plugin | Extended with per-voice portamento |
| MonoHandler | `dsp/include/krate/dsp/processors/mono_handler.h` | 2 | Already has portamento. Slide time routed here for Mono mode. |
| ParameterIDs | `plugins/ruinae/src/plugin_ids.h` | Plugin | Extended with 35 modifier parameter IDs |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities -- no conflicts
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives -- ArpLane reused, no changes
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors -- ArpeggiatorCore to be extended
- [x] `plugins/ruinae/src/parameters/` - Plugin params -- ArpeggiatorParams to be extended
- [x] `plugins/ruinae/src/engine/` - Engine -- RuinaeEngine and RuinaeVoice to be extended
- [x] `plugins/ruinae/src/plugin_ids.h` - Parameter IDs -- range 3140-3181 is unoccupied
- [x] `specs/_architecture_/` - Component inventory

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: The only new type (ArpStepFlags) is a simple enum in the Krate::DSP namespace. It does not exist anywhere in the codebase. All other changes are method additions/modifications on existing classes. No risk of duplicate definitions.

## Dependency API Contracts (Principle XIV Extension)

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|---|---|---|---|
| ArpEvent | type | `Type type{Type::NoteOn}` | Yes |
| ArpEvent | note | `uint8_t note{0}` | Yes |
| ArpEvent | velocity | `uint8_t velocity{0}` | Yes |
| ArpEvent | sampleOffset | `int32_t sampleOffset{0}` | Yes |
| ArpLane<uint8_t> | advance() | `T advance() noexcept` | Yes |
| ArpLane<uint8_t> | reset() | `void reset() noexcept` | Yes |
| ArpLane<uint8_t> | setStep() | `void setStep(size_t index, T value) noexcept` | Yes |
| ArpLane<uint8_t> | setLength() | `void setLength(size_t len) noexcept` | Yes |
| ArpeggiatorCore | velocityLane() | `ArpLane<float>& velocityLane() noexcept` | Yes |
| ArpeggiatorCore | gateLane() | `ArpLane<float>& gateLane() noexcept` | Yes |
| ArpeggiatorCore | pitchLane() | `ArpLane<int8_t>& pitchLane() noexcept` | Yes |
| ArpeggiatorCore | fireStep() | `inline void fireStep(const BlockContext& ctx, int32_t sampleOffset, std::span<ArpEvent> outputEvents, size_t& eventCount, size_t maxEvents, size_t samplesProcessed, size_t blockSize) noexcept` | Yes |
| ArpeggiatorCore | resetLanes() | `void resetLanes() noexcept` | Yes |
| ArpeggiatorCore | currentArpNotes_ | `std::array<uint8_t, 32> currentArpNotes_{}` | Yes |
| ArpeggiatorCore | currentArpNoteCount_ | `size_t currentArpNoteCount_ = 0` | Yes |
| ArpeggiatorCore | calculateGateDuration | `inline size_t calculateGateDuration(float gateScale) const noexcept` | Yes |
| RuinaeEngine | noteOn() | `void noteOn(uint8_t note, uint8_t velocity) noexcept` | Yes |
| RuinaeEngine | setPortamentoTime() | `void setPortamentoTime(float ms) noexcept` | Yes |
| RuinaeEngine | dispatchPolyNoteOn() | `void dispatchPolyNoteOn(uint8_t note, uint8_t velocity) noexcept` | Yes |
| RuinaeEngine | dispatchMonoNoteOn() | `void dispatchMonoNoteOn(uint8_t note, uint8_t velocity) noexcept` | Yes |
| RuinaeEngine | voices_ | `std::array<RuinaeVoice, kMaxPolyphony> voices_` | Yes |
| RuinaeEngine | noteOnTimestamps_ | `std::array<uint64_t, kMaxPolyphony> noteOnTimestamps_` | Yes |
| RuinaeVoice | noteOn() | `void noteOn(float freq, float velocity) noexcept` | Yes |
| RuinaeVoice | setFrequency() | `void setFrequency(float freq) noexcept` | Yes |
| RuinaeVoice | isActive() | `bool isActive() const noexcept` | Yes |
| MonoHandler | setPortamentoTime() | `void setPortamentoTime(float ms) noexcept` | Yes |
| MonoHandler | noteOn() | `MonoEvent noteOn(int note, int velocity)` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/processors/arpeggiator_core.h` - ArpEvent, ArpeggiatorCore, fireStep, resetLanes, member variables
- [x] `dsp/include/krate/dsp/primitives/arp_lane.h` - ArpLane<T> template (full body)
- [x] `dsp/include/krate/dsp/processors/mono_handler.h` - MonoHandler (setPortamentoTime, noteOn)
- [x] `plugins/ruinae/src/parameters/arpeggiator_params.h` - All 6 functions (full bodies)
- [x] `plugins/ruinae/src/plugin_ids.h` - ParameterIDs enum (arp range 3000-3199)
- [x] `plugins/ruinae/src/engine/ruinae_engine.h` - noteOn, dispatchPolyNoteOn, dispatchMonoNoteOn, setPortamentoTime
- [x] `plugins/ruinae/src/engine/ruinae_voice.h` - RuinaeVoice members, noteOn, setFrequency, isActive
- [x] `plugins/ruinae/src/processor/processor.cpp` - Arp event routing, applyParamsToEngine, processEvents

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|---|---|---|
| ArpeggiatorCore | `selector_.reset()` called inside `setMode()` -- setting mode unconditionally every block prevents arp from advancing | Only call `setMode()` when mode actually changes |
| ArpeggiatorCore | `calculateGateDuration()` casts through double then to size_t | Must maintain same cast chain for bit-identical compat |
| ArpLane<T> | `setStep()` clamps index to `length_ - 1` | MUST use expand-write-shrink: call setLength(32) first, write all 32 steps, then call setLength(actualLength). Skipping the expand step silently corrupts data by writing all values to step 0. |
| ArpeggiatorParams | handleArpParamChange dispatches on `id >= kArpBaseId && id <= kArpEndId` | kArpEndId already 3199, no update needed for Phase 5's new IDs (3140-3181). Note: Phase 6 (Ratcheting, IDs 3190-3222) will require expanding kArpEndId and kNumParameters. |
| Processor | Lanes use expand-write-shrink: setLength(32), write steps, setLength(actual) | Follow same pattern for modifier lane -- identical to Phase 4's velocity, gate, and pitch lane patterns |
| loadArpParams | EOF handling differs by position within modifier data | EOF at `modifierLaneLength` read = Phase 4 preset, return `true` (all modifier fields keep defaults). EOF at any subsequent modifier field (steps, accentVelocity, slideTime) = corrupt stream, return `false`. The caller treats `false` as full load failure and restores plugin defaults. |
| RuinaeEngine.noteOn | Currently takes 2 args | Add defaulted 3rd arg `bool legato = false` for backward compat |
| RuinaeVoice.setFrequency | Instantly sets freq and updates oscillators | Must add portamento ramp for smooth slide |
| ArpEvent construction | Uses aggregate initialization without field names | Adding legato{false} at end is backward compatible |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|---|---|---|---|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|---|---|
| setAccentVelocity() | ArpeggiatorCore-specific, single consumer. accentVelocity_ is read by fireStep() to boost step velocity. |
| setSlideTime() | ArpeggiatorCore-specific, single consumer. slideTimeMs_ is stored for API symmetry and future use; the actual portamento routing is via applyParamsToArp() -> engine_.setPortamentoTime(). The value is NOT read by fireStep(). |
| dispatchPolyLegatoNoteOn() | RuinaeEngine-specific, single consumer |

**Decision**: No Layer 0 extractions needed. All new functionality is either in ArpeggiatorCore (Layer 2) or plugin-level engine code. The modifier evaluation logic is too tightly coupled to arp state to be generic.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|---|---|---|
| **Feedback loops** | NO | Modifier evaluation is stateless per step (except tieActive_) |
| **Data parallelism width** | 1 lane | Single uint8_t read + 4 bit checks per step |
| **Branch density in inner loop** | HIGH | Priority chain requires 4 conditional checks |
| **Dominant operations** | bitwise + branch | `(flags & kStepActive)`, `(flags & kStepTie)`, etc. |
| **Current CPU budget vs expected usage** | < 0.1% budget vs < 0.001% expected | Massive headroom |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: Modifier evaluation executes once per arp step (1-50 times per second), not per sample. It consists of a single uint8_t bitmask read followed by 3-4 conditional branches. The total computation is approximately 10-20 instructions per step. SIMD would add complexity with zero measurable benefit. The per-voice portamento ramp (per-sample) is a single float operation, also not worth SIMD.

### Alternative Optimizations

None needed. The algorithm is already minimal.

## Higher-Layer Reusability Analysis

### Sibling Features Analysis

**This feature's layer**: Layer 2 (ArpeggiatorCore extension)

**Related features at same layer** (from arpeggiator-roadmap.md):
- Phase 6: Ratcheting (ArpLane<uint8_t> ratchetLane_, 1-4 sub-divisions per step)
- Phase 7: Euclidean Timing (auto-generates active/rest patterns)
- Phase 8: Conditional Trig (condition enum per step, probability/ratio evaluation)
- Phase 9: Spice/Dice (randomization overlay on lane values)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|---|---|---|---|
| ArpStepFlags enum | LOW | Only this feature | Keep local in arpeggiator_core.h |
| legato field in ArpEvent | MEDIUM | Possible future use by ratchet (legato sub-triggers?) | Keep in ArpEvent |
| tieActive_ state tracking | LOW | Only modifier evaluation | Keep as private member |
| Modifier evaluation pattern in fireStep | MEDIUM | Phase 8 conditions may suppress steps similarly | Document pattern for Phase 8 reference |
| Per-voice portamento on RuinaeVoice | MEDIUM | Could be useful for future voice features | Keep on RuinaeVoice |

### Decision Log

| Decision | Rationale |
|---|---|
| ArpStepFlags in arpeggiator_core.h, not separate file | Small enum (4 values), only used by ArpeggiatorCore's fireStep() |
| legato on ArpEvent, not separate event type | Simpler, more composable, avoids changing all switch statements |
| Per-voice portamento as simple linear/exp ramp | Minimal complexity for Phase 5 scope. Can be enhanced later. |
| Follow expand-write-shrink pattern for modifier lane | Consistency with Phase 4's velocity/gate/pitch lane patterns |

### Review Trigger

After implementing **Phase 6 (Ratcheting)**, review this section:
- [ ] Does Phase 6 need to interact with modifier flags (e.g., ratchet + accent)?
- [ ] Does the fireStep() modifier evaluation pattern scale to ratchet evaluation?
- [ ] Any duplicated step-suppression logic between modifiers and ratchet?

## Project Structure

### Documentation (this feature)

```text
specs/073-per-step-mods/
+-- plan.md              # This file
+-- research.md          # Phase 0 output
+-- data-model.md        # Phase 1 output
+-- quickstart.md        # Phase 1 output
+-- contracts/           # Phase 1 output
|   +-- arpeggiator_core_extension.md
|   +-- parameter_ids.md
|   +-- engine_integration.md
|   +-- state_serialization.md
+-- spec.md              # Feature specification
+-- checklists/          # Setup script output
+-- tasks.md             # Phase 2 output (created by /speckit.tasks)
```

### Source Code (repository root)

```text
dsp/
+-- include/krate/dsp/
|   +-- processors/
|       +-- arpeggiator_core.h      # MODIFIED: ArpStepFlags, ArpEvent.legato, modifier lane, fireStep()
+-- tests/unit/
    +-- processors/
        +-- arpeggiator_core_test.cpp # MODIFIED: 20+ new test cases for modifiers

plugins/ruinae/
+-- src/
|   +-- plugin_ids.h                # MODIFIED: Add 35 modifier parameter IDs (3140-3181)
|   +-- parameters/
|   |   +-- arpeggiator_params.h    # MODIFIED: Extend all 6 functions with modifier data
|   +-- processor/
|   |   +-- processor.cpp           # MODIFIED: Apply modifier params, pass evt.legato to engine
|   +-- engine/
|       +-- ruinae_engine.h         # MODIFIED: noteOn(note, vel, legato), dispatchPolyLegatoNoteOn()
|       +-- ruinae_voice.h          # MODIFIED: Per-voice portamento (setPortamentoTime, ramp)
+-- tests/unit/
    +-- parameters/
    |   +-- arpeggiator_params_test.cpp # MODIFIED: Modifier param tests
    +-- processor/
        +-- arp_integration_test.cpp    # MODIFIED: Legato routing tests

specs/_architecture_/
+-- layer-2-processors.md          # MODIFIED: Document ArpStepFlags, modifier lane
+-- plugin-parameter-system.md     # MODIFIED: Document modifier parameter IDs
+-- plugin-state-persistence.md    # MODIFIED: Document modifier serialization
```

**Structure Decision**: This feature follows the existing monorepo layout. No new files are created -- all changes are extensions to existing files, following established Phase 4 patterns. The ArpStepFlags enum lives in arpeggiator_core.h alongside the ArpEvent struct it relates to.

## Complexity Tracking

No constitution violations to justify. All design decisions align with established patterns.
