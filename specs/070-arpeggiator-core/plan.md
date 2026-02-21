# Implementation Plan: Arpeggiator Core -- Timing & Event Generation

**Branch**: `070-arpeggiator-core` | **Date**: 2026-02-20 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/070-arpeggiator-core/spec.md`

## Summary

ArpeggiatorCore is a DSP Layer 2 processor that combines HeldNoteBuffer + NoteSelector (Phase 1, Layer 1) with integer sample-accurate timing to produce ArpEvent sequences. It receives MIDI noteOn/noteOff input, applies latch and retrigger logic, manages step timing (tempo-synced or free rate), gate length, and swing, and outputs up to 64 sample-accurate events per processing block. The implementation is header-only, zero-allocation, and fully real-time safe.

## Technical Context

**Language/Version**: C++20 (header-only DSP component)
**Primary Dependencies**: HeldNoteBuffer, NoteSelector (Layer 1), BlockContext, NoteValue/NoteModifier, getBeatsForNote() (Layer 0)
**Storage**: N/A (no persistent storage)
**Testing**: Catch2 via `dsp_tests` target
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC) -- cross-platform
**Project Type**: Monorepo DSP library component
**Performance Goals**: < 0.5% CPU for Layer 2 processor. Arp is logic-only (no audio DSP), so expected overhead is negligible.
**Constraints**: Zero heap allocation in processBlock/noteOn/noteOff/setters. noexcept on all methods. Integer timing accumulator for zero drift.
**Scale/Scope**: Single header file (~500-700 lines), single test file (~800-1200 lines)

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

| Principle | Status | Notes |
|-----------|--------|-------|
| I. VST3 Architecture Separation | N/A | DSP-only component, no VST3 dependency |
| II. Real-Time Audio Thread Safety | PASS | All methods noexcept, zero allocation, no locks/IO/exceptions |
| III. Modern C++ Standards | PASS | C++20, std::span, std::array, enum class, constexpr |
| IV. SIMD & DSP Optimization | PASS | Not applicable -- logic-only, no audio math. See SIMD section. |
| V. VSTGUI Development | N/A | No UI in this phase |
| VI. Cross-Platform Compatibility | PASS | Pure C++ with no platform-specific code |
| VII. Project Structure & Build System | PASS | Header at processors/, tests at unit/processors/ |
| VIII. Testing Discipline | PASS | Test-first, Catch2, all SC-xxx criteria verified |
| IX. Layered DSP Architecture | PASS | Layer 2, depends only on Layer 0 + Layer 1 |
| X. DSP Processing Constraints | N/A | No audio processing (no saturation, interpolation, etc.) |
| XI. Performance Budgets | PASS | < 0.5% CPU target for Layer 2 |
| XII. Debugging Discipline | PASS | N/A at planning stage |
| XIII. Test-First Development | PASS | Tests written before implementation |
| XIV. Living Architecture | PASS | Update specs/_architecture_/layer-2-processors.md |
| XV. Pre-Implementation Research | PASS | ODR search completed (see below) |
| XVI. Honest Completion | PASS | Compliance table filled from code + test output |

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

**Classes/Structs to be created**: ArpEvent, ArpeggiatorCore, PendingNoteOff (internal)

| Planned Type | Search Command | Existing? | Action |
|---|---|---|---|
| ArpEvent | `grep -r "ArpEvent\|struct ArpEvent" dsp/ plugins/` | No | Create New |
| ArpeggiatorCore | `grep -r "ArpeggiatorCore\|class ArpeggiatorCore" dsp/ plugins/` | No | Create New |
| PendingNoteOff | `grep -r "PendingNoteOff\|struct PendingNoteOff" dsp/ plugins/` | No | Create New (internal to header) |
| LatchMode | `grep -r "LatchMode\|enum.*LatchMode" dsp/ plugins/` | No | Create New |
| ArpRetriggerMode | `grep -r "ArpRetriggerMode\|enum.*ArpRetriggerMode" dsp/ plugins/` | No | Create New |

**Utility Functions to be created**: None (all timing math is inline within ArpeggiatorCore)

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|---|---|---|---|
| HeldNoteBuffer | dsp/include/krate/dsp/primitives/held_note_buffer.h | 1 | Owned member, holds MIDI notes |
| NoteSelector | dsp/include/krate/dsp/primitives/held_note_buffer.h | 1 | Owned member, selects next note(s) |
| ArpMode | dsp/include/krate/dsp/primitives/held_note_buffer.h | 1 | Enum parameter for setMode() |
| OctaveMode | dsp/include/krate/dsp/primitives/held_note_buffer.h | 1 | Enum parameter for setOctaveMode() |
| ArpNoteResult | dsp/include/krate/dsp/primitives/held_note_buffer.h | 1 | Return type from NoteSelector::advance() |
| BlockContext | dsp/include/krate/dsp/core/block_context.h | 0 | Input to processBlock() |
| NoteValue | dsp/include/krate/dsp/core/note_value.h | 0 | Tempo sync note value enum |
| NoteModifier | dsp/include/krate/dsp/core/note_value.h | 0 | Tempo sync modifier enum |
| getBeatsForNote() | dsp/include/krate/dsp/core/note_value.h | 0 | Beats-per-step calculation |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 processors (no arpeggiator exists)
- [x] `dsp/include/krate/dsp/primitives/envelope_utils.h` - RetriggerMode ODR hazard confirmed
- [x] `specs/_architecture_/` - Component inventory checked

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: All planned types (ArpEvent, ArpeggiatorCore, LatchMode, ArpRetriggerMode, PendingNoteOff) are unique and not found in the codebase. The only ODR hazard is the existing `RetriggerMode` in envelope_utils.h, which is avoided by using the distinct name `ArpRetriggerMode`.

## Dependency API Contracts (Principle XV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|---|---|---|---|
| BlockContext | sampleRate | `double sampleRate = 44100.0` | Yes |
| BlockContext | blockSize | `size_t blockSize = 512` | Yes |
| BlockContext | tempoBPM | `double tempoBPM = 120.0` | Yes |
| BlockContext | isPlaying | `bool isPlaying = false` | Yes |
| BlockContext | transportPositionSamples | `int64_t transportPositionSamples = 0` | Yes |
| BlockContext | timeSignatureNumerator | `uint8_t timeSignatureNumerator = 4` | Yes |
| BlockContext | timeSignatureDenominator | `uint8_t timeSignatureDenominator = 4` | Yes |
| BlockContext | tempoToSamples() | `constexpr size_t tempoToSamples(NoteValue, NoteModifier) const noexcept` | Yes |
| BlockContext | samplesPerBar() | `constexpr size_t samplesPerBar() const noexcept` | Yes |
| HeldNoteBuffer | kMaxNotes | `static constexpr size_t kMaxNotes = 32` | Yes |
| HeldNoteBuffer | noteOn | `void noteOn(uint8_t note, uint8_t velocity) noexcept` | Yes |
| HeldNoteBuffer | noteOff | `void noteOff(uint8_t note) noexcept` | Yes |
| HeldNoteBuffer | clear | `void clear() noexcept` | Yes |
| HeldNoteBuffer | size | `size_t size() const noexcept` | Yes |
| HeldNoteBuffer | empty | `bool empty() const noexcept` | Yes |
| NoteSelector | setMode | `void setMode(ArpMode mode) noexcept` | Yes |
| NoteSelector | setOctaveRange | `void setOctaveRange(int octaves) noexcept` | Yes |
| NoteSelector | setOctaveMode | `void setOctaveMode(OctaveMode mode) noexcept` | Yes |
| NoteSelector | advance | `ArpNoteResult advance(const HeldNoteBuffer& held) noexcept` | Yes |
| NoteSelector | reset | `void reset() noexcept` | Yes |
| getBeatsForNote | function | `constexpr float getBeatsForNote(NoteValue, NoteModifier) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/block_context.h` - BlockContext struct (152 lines)
- [x] `dsp/include/krate/dsp/core/note_value.h` - NoteValue, NoteModifier, getBeatsForNote (250 lines)
- [x] `dsp/include/krate/dsp/primitives/held_note_buffer.h` - HeldNoteBuffer, NoteSelector, ArpNoteResult (612 lines)
- [x] `dsp/include/krate/dsp/primitives/sequencer_core.h` - SequencerCore timing patterns (587 lines)
- [x] `dsp/include/krate/dsp/primitives/envelope_utils.h` - RetriggerMode ODR check (80 lines read)
- [x] `dsp/include/krate/dsp/processors/trance_gate.h` - Layer 2 pattern reference (397 lines)

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|---|---|---|
| BlockContext | Member is `tempoBPM` not `tempo` | `ctx.tempoBPM` |
| BlockContext | `isPlaying` defaults to `false` | Must set `ctx.isPlaying = true` in tests |
| NoteSelector | `setMode()` calls `reset()` internally | No need to call `reset()` after `setMode()` |
| NoteSelector | Constructor takes optional PRNG seed | `NoteSelector(42)` for deterministic tests |
| HeldNoteBuffer | `noteOn()` for existing note updates velocity | Not a duplicate; velocity changes in-place |
| getBeatsForNote | Returns `float`, not `double` | Cast to double for `size_t` calculation precision |
| SequencerCore | Swing range is 0.0-1.0 (not 0.0-0.75) | ArpeggiatorCore uses 0.0-0.75 (spec FR-016) |
| BlockContext | `tempoToSamples()` clamps tempo internally | No need to clamp before calling |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|---|---|---|---|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|---|---|
| Step duration calculation | Inline in processBlock, only 2 consumers, patterns differ |
| Swing formula | Identical to SequencerCore but inline, not worth extracting for 2 uses |
| Bar boundary detection | Specific to retrigger Beat, single consumer |

**Decision**: No Layer 0 extractions needed. The timing math is 3 lines of arithmetic, and extracting it would add a dependency for minimal benefit. If a third consumer appears (e.g., Phase 7 Euclidean timing), revisit.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|---|---|---|
| **Feedback loops** | NO | No sample-to-sample feedback |
| **Data parallelism width** | 1 | Single arp instance, no parallel streams |
| **Branch density in inner loop** | HIGH | Step boundary check, gate check, transport check per iteration |
| **Dominant operations** | integer comparison + increment | Counter increment and comparison, almost zero arithmetic |
| **Current CPU budget vs expected usage** | <0.5% vs ~0.01% | Logic-only, negligible CPU usage |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: ArpeggiatorCore is a logic processor, not an audio DSP processor. It performs integer comparisons and increments on a single counter, with frequent branching (step boundary, gate deadline, transport state). There are no parallel data streams to exploit, and the total CPU usage is negligible (<0.01%). SIMD would add complexity for zero measurable benefit.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|---|---|---|---|
| Jump-ahead iteration | ~2-10x fewer loop iterations | LOW | YES |

**Jump-ahead iteration**: Instead of iterating sample-by-sample through the block, calculate the next event time (step boundary or pending NoteOff deadline) and jump directly to it. This reduces iterations from `blockSize` (e.g., 512) to `numEvents` (typically 0-4). This is the primary optimization strategy.

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 2 - Processors

**Related features at same layer** (from arpeggiator-roadmap.md):
- Phase 4: ArpLane<T> extension to ArpeggiatorCore
- Phase 5: Per-step modifiers (Slide, Accent, Tie, Rest)
- Phase 6: Ratcheting subdivisions
- Phase 7: Euclidean timing mode

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|---|---|---|---|
| ArpEvent struct | HIGH | Phase 3 (integration), Phase 5 (legato flag) | Keep in arpeggiator_core.h, extend in Phase 5 |
| LatchMode enum | HIGH | Phase 3 (parameter mapping) | Keep in arpeggiator_core.h |
| ArpRetriggerMode enum | HIGH | Phase 3 (parameter mapping) | Keep in arpeggiator_core.h |
| PendingNoteOff tracking | MEDIUM | Possibly Phase 6 (ratchet NoteOffs) | Keep as internal implementation detail |
| Integer timing accumulator | LOW | Specific to ArpeggiatorCore's block-oriented model | Keep as private members |

### Decision Log

| Decision | Rationale |
|---|---|
| Keep all new types in arpeggiator_core.h | Single header simplifies Phase 3 integration; no separate enum header needed |
| No shared timing base class | SequencerCore and ArpeggiatorCore have different processing models (per-sample vs block-oriented) |
| PendingNoteOff is internal struct | No external consumers anticipated; ratcheting in Phase 6 extends the existing mechanism |

### Review Trigger

After implementing **Phase 4 (ArpLane)**, review:
- [ ] Does ArpLane need ArpEvent? -> Yes, used by ArpeggiatorCore
- [ ] Does ArpLane introduce new timing? -> No, it modifies per-step values
- [ ] Any duplicated code between ArpeggiatorCore and SequencerCore? -> Evaluate shared timing utility

## Project Structure

### Documentation (this feature)

```text
specs/070-arpeggiator-core/
+-- plan.md              # This file
+-- research.md          # Phase 0 research decisions
+-- data-model.md        # Entity model and API contracts
+-- quickstart.md        # Implementation guide
+-- contracts/
|   +-- arpeggiator_core_api.h  # API contract reference
+-- spec.md              # Feature specification
```

### Source Code (repository root)

```text
dsp/
+-- include/krate/dsp/
|   +-- processors/
|       +-- arpeggiator_core.h    # NEW: Main implementation (header-only)
+-- tests/
    +-- unit/processors/
        +-- arpeggiator_core_test.cpp  # NEW: Unit tests
```

**Structure Decision**: Single header-only file in the existing Layer 2 processors directory, consistent with all other processor components in the codebase (trance_gate.h, multimode_filter.h, etc.).

## Detailed Implementation Design

### 1. processBlock() Algorithm (Jump-Ahead Strategy)

The core algorithm uses a "jump to next event" approach rather than sample-by-sample iteration:

```
processBlock(ctx, outputEvents):
    // Guards
    if blockSize == 0: return 0                          // FR-032
    if !enabled_: return handleDisableTransition()       // FR-008
    if !ctx.isPlaying: return handleTransportStop()      // FR-031

    // Handle transport restart
    if ctx.isPlaying && !wasPlaying_:
        handleTransportRestart()
    wasPlaying_ = ctx.isPlaying

    eventCount = 0
    samplesProcessed = 0

    while samplesProcessed < blockSize:
        // Find next event: minimum of (step boundary, pending NoteOff, block end)
        nextStepBoundarySample = sampleCounter_ remaining to currentStepDuration_
        nextNoteOffSample = minimum pending NoteOff samplesRemaining
        nextBarBoundarySample = (if retrigger Beat) next bar boundary offset
        samplesUntilBlockEnd = blockSize - samplesProcessed

        nextEventSample = min(all of the above, samplesUntilBlockEnd)

        // Advance time to next event
        sampleCounter_ += nextEventSample
        decrement all pending NoteOff samplesRemaining by nextEventSample
        samplesProcessed += nextEventSample

        // Process events at this sample
        sampleOffset = samplesProcessed - 1  (or the actual offset)

        // Emit pending NoteOffs that are due
        for each pending NoteOff with samplesRemaining == 0:
            emit NoteOff event

        // Check bar boundary (retrigger Beat)
        if at bar boundary:
            selector_.reset()
            swingStepCounter_ = 0

        // Check step boundary
        if sampleCounter_ >= currentStepDuration_:
            sampleCounter_ = 0
            fire new step:
                advance NoteSelector
                emit NoteOn event(s)
                schedule NoteOff(s)
                swingStepCounter_++
                recalculate currentStepDuration_ with swing

    return eventCount
```

### 2. Latch Mode Implementation

```
noteOn(note, velocity):
    physicalKeysHeld_++

    if latchMode_ == Hold && latchActive_:
        // New key while latched: replace entire pattern
        heldNotes_.clear()
        latchActive_ = false

    heldNotes_.noteOn(note, velocity)

    if retriggerMode_ == Note:
        selector_.reset()
        swingStepCounter_ = 0

noteOff(note):
    if physicalKeysHeld_ > 0:
        physicalKeysHeld_--

    switch latchMode_:
        Off:
            heldNotes_.noteOff(note)
            // If buffer empty, need to emit NoteOff for current arp note
        Hold:
            // Do NOT remove from heldNotes_
            if physicalKeysHeld_ == 0:
                latchActive_ = true
        Add:
            // Do NOT remove from heldNotes_
            // Notes accumulate indefinitely
```

### 3. Transport Stop/Start Handling (FR-031)

```
// In processBlock():
if !ctx.isPlaying:
    if wasPlaying_:
        // Transport just stopped -- emit NoteOff for current note(s)
        emitCurrentArpNoteOffs(outputEvents, eventCount, 0)
        // Also emit any pending NoteOffs
        emitAllPendingNoteOffs(outputEvents, eventCount, 0)
    wasPlaying_ = false
    return eventCount  // 0 or just the NoteOffs

if ctx.isPlaying && !wasPlaying_:
    // Transport just started -- resume from current state
    wasPlaying_ = true
    // Don't reset pattern (latched notes preserved per FR-031)
    // Timing accumulator continues from where it was
```

### 4. Swing Implementation Details

```
calculateStepDuration(ctx):
    if tempoSync_:
        beatsPerStep = getBeatsForNote(noteValue_, noteModifier_)
        secondsPerBeat = 60.0 / clamp(ctx.tempoBPM, 20.0, 300.0)
        baseDuration = static_cast<size_t>(secondsPerBeat * beatsPerStep * ctx.sampleRate)
    else:
        baseDuration = static_cast<size_t>(ctx.sampleRate / freeRateHz_)

    // Apply swing (swing_ is 0.0 to 0.75)
    if swingStepCounter_ % 2 == 0:
        // Even step: lengthen
        swungDuration = static_cast<size_t>(baseDuration * (1.0 + swing_))
    else:
        // Odd step: shorten
        swungDuration = static_cast<size_t>(baseDuration * (1.0 - swing_))

    return max(1, swungDuration)
```

Swing step counter reset conditions (FR-020):
- `reset()` call
- `setMode()` call (because NoteSelector resets)
- Retrigger Note event (noteOn with retrigger == Note)
- Retrigger Beat event (bar boundary)

### 5. Gate Length and Pending NoteOff Tracking

```
// PendingNoteOff -- authoritative definition in data-model.md
struct PendingNoteOff {
    uint8_t note{0};
    size_t samplesRemaining{0};
};

// When a step fires NoteOn at sampleOffset S:
gateDuration = static_cast<size_t>(currentStepDuration_ * gateLengthPercent_ / 100.0f)
gateDuration = max(1, gateDuration)

// If NoteOff falls within current block:
noteOffOffset = S + gateDuration
if noteOffOffset < blockSize:
    emit NoteOff at noteOffOffset directly
else:
    // Schedule for future block
    add PendingNoteOff{note, gateDuration - (blockSize - S)}

// At start of processBlock, check pending NoteOffs:
// IMPORTANT: use strictly < blockSize (NOT <=). When samplesRemaining == blockSize,
// the event falls at sampleOffset 0 of the NEXT block -- defer it. Using <= would
// produce sampleOffset = blockSize which violates FR-002. See data-model.md PendingNoteOff.
for each pending:
    if samplesRemaining < blockSize:
        emit NoteOff at sampleOffset = samplesRemaining
    else:
        samplesRemaining -= blockSize
```

### 6. Retrigger Beat: Bar Boundary Detection

```
// In processBlock(), check for bar boundaries within the block:
barSamples = ctx.samplesPerBar()
if barSamples == 0: skip  // prevent division by zero

blockStart = ctx.transportPositionSamples
blockEnd = blockStart + static_cast<int64_t>(ctx.blockSize)

// Find if a bar boundary falls within [blockStart, blockEnd)
if blockStart >= 0:  // Only when transport position is valid
    remainder = blockStart % static_cast<int64_t>(barSamples)
    if remainder == 0:
        barBoundarySample = 0  // At block start
    else:
        barBoundarySample = static_cast<int64_t>(barSamples) - remainder

    if barBoundarySample < ctx.blockSize:
        // Bar boundary at this sample offset
        selector_.reset()
        swingStepCounter_ = 0
```

### 7. Enable/Disable Transitions (FR-008)

```
setEnabled(enabled):
    if enabled_ && !enabled:
        needsDisableNoteOff_ = true  // Will emit NoteOff in next processBlock
    enabled_ = enabled

// In processBlock():
if !enabled_:
    if needsDisableNoteOff_:
        emit NoteOff for all current arp notes at sampleOffset 0
        emit all pending NoteOffs at sampleOffset 0
        needsDisableNoteOff_ = false
    return eventCount
```

### 8. Chord Mode Handling (FR-022)

```
// When NoteSelector::advance() returns count > 1 (Chord mode):
ArpNoteResult result = selector_.advance(heldNotes_)

if result.count > 1:
    // Emit NoteOff for ALL previously sounding notes
    for each note in currentArpNotes_:
        emit NoteOff at sampleOffset

    // Emit NoteOn for ALL chord notes
    for i in 0..result.count-1:
        emit NoteOn{result.notes[i], result.velocities[i], sampleOffset}

    // Track all as current arp notes
    currentArpNoteCount_ = result.count
    for i in 0..result.count-1:
        currentArpNotes_[i] = result.notes[i]

    // Schedule NoteOff for all chord notes
    gateDuration = calculateGateDuration()
    for i in 0..result.count-1:
        addPendingNoteOff(result.notes[i], gateDuration)
```

## Test Strategy

### Test Organization by Success Criteria

| SC | Test Cases | Approach |
|---|---|---|
| SC-001 | Timing accuracy at 60/120/200 BPM, 1/4/1/8/1/16/1/8T | Run 100+ steps, verify each sampleOffset within 1 sample |
| SC-002 | Gate length at 1%/50%/100%/150%/200% | Verify NoteOff offset = NoteOn offset + stepDuration * gate/100 |
| SC-003 | Zero allocation | Code inspection: no new/delete/malloc/free/vector/string in header |
| SC-004 | Latch Off/Hold/Add (3+ tests each) + transport stop in Hold/Add | Simulate key press/release sequences |
| SC-005 | Retrigger Off/Note/Beat (2+ tests each) | Verify pattern position after retrigger events |
| SC-006 | Swing at 0%/25%/50%/75% + setMode() reset test | Measure even/odd step durations |
| SC-007 | Gate >100% produces legato overlap | Verify NoteOn before NoteOff in event buffer |
| SC-008 | 1000-step drift test | Sum all inter-event gaps, compare to 1000 * stepDuration |
| SC-009 | Cross-platform CI | Windows build + test in this spec; macOS/Linux in CI |
| SC-010 | Edge cases: empty buffer, single note, enable/disable, zero blockSize | Dedicated tests for each |

### Test Helpers

```cpp
// Helper to run arp for N blocks and collect all events
std::vector<ArpEvent> collectEvents(ArpeggiatorCore& arp, BlockContext& ctx,
                                     size_t numBlocks) {
    std::vector<ArpEvent> allEvents;
    std::array<ArpEvent, 64> blockEvents;
    for (size_t b = 0; b < numBlocks; ++b) {
        size_t count = arp.processBlock(ctx, blockEvents);
        for (size_t i = 0; i < count; ++i) {
            // Adjust sampleOffset to absolute position
            blockEvents[i].sampleOffset += static_cast<int32_t>(b * ctx.blockSize);
            allEvents.push_back(blockEvents[i]);
        }
        ctx.transportPositionSamples += static_cast<int64_t>(ctx.blockSize);
    }
    return allEvents;
}

// Helper to find the N-th NoteOn event
const ArpEvent& findNthNoteOn(const std::vector<ArpEvent>& events, size_t n);
```

## CMake Integration

### dsp/CMakeLists.txt

Add to `KRATE_DSP_PROCESSORS_HEADERS`:
```cmake
include/krate/dsp/processors/arpeggiator_core.h
```

### dsp/tests/CMakeLists.txt

Add to `dsp_tests` source list:
```cmake
unit/processors/arpeggiator_core_test.cpp
```

Add to `-fno-fast-math` list (for Clang/GCC):
```cmake
unit/processors/arpeggiator_core_test.cpp
```

### dsp/lint_all_headers.cpp

Add include:
```cpp
#include <krate/dsp/processors/arpeggiator_core.h>
```

## Complexity Tracking

No constitution violations detected. All design decisions comply with established principles.
