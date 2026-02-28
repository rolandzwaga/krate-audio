# Implementation Plan: Arpeggiator Scale Mode

**Branch**: `084-arp-scale-mode` | **Date**: 2026-02-28 | **Spec**: [spec.md](spec.md)
**Input**: Feature specification from `/specs/084-arp-scale-mode/spec.md`

## Summary

Add an optional scale mode to the Ruinae arpeggiator that allows users to set a root note and scale type, causing the pitch lane to interpret its step values as scale degree offsets instead of chromatic semitone offsets. This requires:

1. **Refactoring** the existing `ScaleHarmonizer` (Layer 0) to support variable-length scales (5-12 notes per octave) via a new `ScaleData` struct, extending the `ScaleType` enum from 9 to 16 values.
2. **Integrating** the refactored `ScaleHarmonizer` into `ArpeggiatorCore::fireStep()` for pitch lane conversion and `noteOn()` for optional input quantization.
3. **Adding** 3 new parameters (Scale Type, Root Note, Scale Quantize Input) with full save/load, registration, and UI.
4. **Updating** the Harmonizer section to expose all 16 scales (side benefit of enum extension).

## Technical Context

**Language/Version**: C++20 (MSVC 2022, Clang 15+, GCC 10+)
**Primary Dependencies**: VST3 SDK 3.7.x, VSTGUI 4.12+, KrateDSP (internal Layer 0-4)
**Storage**: VST3 binary state stream (IBStreamer)
**Testing**: Catch2 (dsp_tests, ruinae_tests)
**Target Platform**: Windows 10/11 (MSVC), macOS 11+ (Clang), Linux (GCC) -- cross-platform
**Project Type**: VST3 plugin monorepo
**Performance Goals**: Scale lookups must be O(1) table reads; zero CPU overhead when Chromatic (passthrough). SC-004 target: no measurable increase beyond existing arp overhead.
**Constraints**: Zero allocations on audio thread. All lookup tables precomputed at compile time (constexpr). Real-time safe (noexcept, no locks).
**Scale/Scope**: 3 new parameters, ~14 files modified, ~400 lines of new code.

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle I (VST3 Architecture Separation):**
- [x] Parameters flow via atomic bridge (ArpeggiatorParams), not direct processor-controller coupling
- [x] Processor works without controller (defaults to Chromatic)
- [x] State syncs via setComponentState (controller reads processor stream)

**Principle II (Real-Time Audio Thread Safety):**
- [x] All scale lookups are constexpr table reads -- zero allocation
- [x] ScaleHarmonizer methods are noexcept with no heap access
- [x] Parameter transfer uses existing atomic pattern (std::memory_order_relaxed)
- [x] No new locks, mutexes, or blocking operations

**Principle III (Modern C++):**
- [x] ScaleData uses std::array, not C-style arrays
- [x] constexpr throughout
- [x] No raw new/delete

**Principle VI (Cross-Platform):**
- [x] No platform-specific UI (VSTGUI dropdowns, toggles, alpha dimming)
- [x] constexpr tables work identically across MSVC/Clang/GCC

**Principle VIII (Testing Discipline):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Principle IX (Layered Architecture):**
- [x] ScaleData/ScaleType in Layer 0 (core) -- no upward dependencies
- [x] ArpeggiatorCore (Layer 2) uses Layer 0 ScaleHarmonizer -- valid downward dependency
- [x] Plugin layer uses Layer 2 ArpeggiatorCore -- valid

**Principle XIII (Test-First):**
- [x] Failing tests written before each implementation change

**Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Principle XVI (Honest Completion):**
- [x] Each FR/SC will be verified against actual code and test output

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: ScaleData

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| ScaleData | `grep -r "struct ScaleData" dsp/ plugins/` | No | Create New in scale_harmonizer.h |

**Utility Functions to be created**: None new. All logic goes into existing ScaleHarmonizer methods or existing arp parameter functions.

**Constants to be created**: kArpScaleTypeCount, kArpRootNoteCount, kArpScaleDisplayOrder, kArpScaleEnumToDisplay

| Planned Constant | Search Command | Existing? | Action |
|---|---|---|---|
| kArpScaleTypeCount | `grep -r "kArpScaleTypeCount" dsp/ plugins/` | No | Create in dropdown_mappings.h |
| kArpRootNoteCount | `grep -r "kArpRootNoteCount" dsp/ plugins/` | No | Create in dropdown_mappings.h |
| kArpScaleDisplayOrder | `grep -r "kArpScaleDisplayOrder" dsp/ plugins/` | No | Create in dropdown_mappings.h |
| kArpScaleEnumToDisplay | `grep -r "kArpScaleEnumToDisplay" dsp/ plugins/` | No | Create in dropdown_mappings.h |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| ScaleHarmonizer | dsp/include/krate/dsp/core/scale_harmonizer.h | 0 | Extended: new ScaleData, 16 ScaleTypes, generalized calculate() |
| ScaleType enum | dsp/include/krate/dsp/core/scale_harmonizer.h | 0 | Extended with 7 new values (9-15) |
| kScaleIntervals | dsp/include/krate/dsp/core/scale_harmonizer.h | 0 | Replaced: std::array<ScaleData, 16> |
| kReverseLookup | dsp/include/krate/dsp/core/scale_harmonizer.h | 0 | Extended: 16 entries (was 8) |
| buildReverseLookup() | dsp/include/krate/dsp/core/scale_harmonizer.h | 0 | Generalized: uses degreeCount |
| ArpeggiatorCore | dsp/include/krate/dsp/processors/arpeggiator_core.h | 2 | Modified: scale-aware fireStep, quantized noteOn |
| ArpeggiatorParams | plugins/ruinae/src/parameters/arpeggiator_params.h | Plugin | Extended: 3 new atomics |
| createDropdownParameter | plugins/ruinae/src/controller/parameter_helpers.h | Plugin | Reused for Scale Type and Root Note dropdowns |
| RuinaeEffectsChain | plugins/ruinae/src/engine/ruinae_effects_chain.h | Plugin | Modified: setHarmonizerScale clamp 0-15 |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities (no ScaleData conflict)
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (no conflicts)
- [x] `dsp/include/krate/dsp/processors/` - Layer 2 (ArpeggiatorCore is the target)
- [x] `specs/_architecture_/` - Component inventory verified
- [x] `plugins/ruinae/src/parameters/` - Arp params, harmonizer params, dropdown mappings

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: The only new type is `ScaleData`, which has no name conflict in the codebase. All other changes are modifications to existing types (`ScaleType`, `ScaleHarmonizer`) or additions of new constants/parameter IDs. The `ScaleData` struct is defined in the `Krate::DSP` namespace alongside the existing `ScaleHarmonizer`.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| ScaleHarmonizer | setKey | `void setKey(int rootNote) noexcept` | Yes |
| ScaleHarmonizer | setScale | `void setScale(ScaleType type) noexcept` | Yes |
| ScaleHarmonizer | getScale | `[[nodiscard]] ScaleType getScale() const noexcept` | Yes |
| ScaleHarmonizer | calculate | `[[nodiscard]] DiatonicInterval calculate(int inputMidiNote, int diatonicSteps) const noexcept` | Yes |
| ScaleHarmonizer | quantizeToScale | `[[nodiscard]] int quantizeToScale(int midiNote) const noexcept` | Yes |
| DiatonicInterval | targetNote | `int targetNote` (field) | Yes |
| DiatonicInterval | semitones | `int semitones` (field) | Yes |
| ArpeggiatorCore | noteOn | `inline void noteOn(uint8_t note, uint8_t velocity) noexcept` | Yes |
| ArpLaneEditor | formatValueText | `[[nodiscard]] std::string formatValueText(float level) const` | Yes |
| createDropdownParameter | (function) | `inline StringListParameter* createDropdownParameter(const TChar* title, ParamID id, std::initializer_list<const TChar*> options)` | Yes |
| RuinaeEffectsChain | setHarmonizerScale | `void setHarmonizerScale(int scaleType) noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/scale_harmonizer.h` - ScaleHarmonizer class, ScaleType enum, detail tables
- [x] `dsp/include/krate/dsp/processors/arpeggiator_core.h` - ArpeggiatorCore class, fireStep, noteOn
- [x] `plugins/ruinae/src/parameters/arpeggiator_params.h` - ArpeggiatorParams struct, all 6 inline functions
- [x] `plugins/ruinae/src/parameters/harmonizer_params.h` - registerHarmonizerParams, handleHarmonizerParamChange
- [x] `plugins/ruinae/src/parameters/dropdown_mappings.h` - All existing dropdown constants
- [x] `plugins/ruinae/src/plugin_ids.h` - Parameter ID allocation
- [x] `plugins/ruinae/src/engine/ruinae_effects_chain.h` - setHarmonizerScale clamp
- [x] `plugins/ruinae/src/processor/processor.cpp` - applyParamsToEngine arp section
- [x] `plugins/ruinae/src/controller/parameter_helpers.h` - createDropdownParameter
- [x] `plugins/shared/src/ui/arp_lane_editor.h` - formatValueText, showValuePopup

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| ScaleHarmonizer::calculate | `diatonicSteps` param is scale degrees, NOT semitones | `calculate(midiNote, pitchLaneOffset)` where offset is in degrees |
| DiatonicInterval | `targetNote` is already clamped to 0-127 | No additional clamping needed after calculate() |
| ArpeggiatorCore setters | Mode/Retrigger setters reset state; only call on change | Use prev-value check pattern like existing `prevArpMode_` |
| kScaleIntervals (new) | Indexed by `static_cast<int>(ScaleType)`, NOT by "non-chromatic index" | `kScaleIntervals[static_cast<int>(scale)]` |
| Arp Scale Type dropdown | UI index 0 = Chromatic, but enum value is 8 | Use kArpScaleDisplayOrder mapping in handleArpParamChange |
| loadArpParams | Returns `true` on partial read (backward compat) | Must `return true` (not false) when new fields fail to read |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| ScaleData struct | Core music theory data structure | scale_harmonizer.h (already Layer 0) | ScaleHarmonizer, ArpeggiatorCore, HarmonizerEngine |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| Scale-aware pitch offset logic | Inline in ArpeggiatorCore::fireStep, single consumer |

**Decision**: ScaleData goes in Layer 0 (scale_harmonizer.h) as it is a core music theory primitive. All scale-aware arp logic stays in ArpeggiatorCore (Layer 2) as it is arp-specific.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | NO | Scale lookup is pure function, no feedback |
| **Data parallelism width** | 1-32 notes per step | Chord mode can fire up to 32 notes, but typical is 1-4 |
| **Branch density in inner loop** | MEDIUM | Per-note: check if chromatic, check if pitchOffset != 0 |
| **Dominant operations** | memory (table lookup) | O(1) array reads, integer modulo/divide |
| **Current CPU budget vs expected usage** | <5% budget vs ~0.001% expected | Scale lookup adds ~5 integer ops per note per step |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: The scale lookup is O(1) per note (constexpr table read + integer modulo for octave wrapping). At most 32 notes per step, firing at arp rate (typically 1-50 Hz), the total CPU impact is negligible -- roughly 160 integer operations per second at maximum. SIMD overhead (data marshalling, lane management) would exceed the computation cost.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Chromatic early return | ~100% for default mode | LOW | YES (already in calculate()) |
| pitchOffset == 0 early return | ~80% for steps with zero pitch | LOW | YES |
| Pre-check scale type once per step | Avoids per-note branch | LOW | YES |

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 0 (ScaleData/ScaleType) + Layer 2 (ArpeggiatorCore integration)

**Related features at same layer**:
- Harmonizer (spec 067): Already uses ScaleType and ScaleHarmonizer -- automatically benefits from 7 new scales
- Scale-constrained randomization (future spice/dice): Could reuse ScaleHarmonizer::quantizeToScale()
- Scale-aware MIDI output (future): Could reuse the extended ScaleType enum

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Extended ScaleType enum (16 values) | HIGH | Harmonizer, future scale features | Already in Layer 0, shared |
| ScaleData struct | HIGH | Any scale-aware feature | Already in Layer 0, shared |
| kArpScaleDisplayOrder mapping | LOW | Only arp UI | Keep in dropdown_mappings.h |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| ScaleData in Layer 0 | Shared by harmonizer and arp; pure music theory |
| Display order mapping in plugin layer | UI-specific concern, not reusable by DSP |
| ScaleHarmonizer in ArpeggiatorCore | ArpCore already at Layer 2, can use Layer 0 |

## Project Structure

### Documentation (this feature)

```text
specs/084-arp-scale-mode/
+-- plan.md              # This file
+-- research.md          # Phase 0 output
+-- data-model.md        # Phase 1 output
+-- quickstart.md        # Phase 1 output
+-- contracts/           # Phase 1 output
|   +-- scale_harmonizer_api.md
|   +-- arpeggiator_core_api.md
|   +-- arp_params_api.md
+-- spec.md              # Feature specification
```

### Source Code (repository root)

```text
dsp/
+-- include/krate/dsp/
|   +-- core/
|   |   +-- scale_harmonizer.h         # MODIFY: ScaleData struct, ScaleType extension,
|   |                                  #   kScaleIntervals refactoring, generalized
|   |                                  #   calculate/buildReverseLookup
|   +-- processors/
|       +-- arpeggiator_core.h         # MODIFY: Add ScaleHarmonizer member, scale-aware
|                                      #   fireStep, quantized noteOn, new setters
+-- tests/unit/
    +-- core/
    |   +-- scale_harmonizer_test.cpp  # MODIFY: Add tests for new scales, ScaleData,
    |                                  #   variable degree counts, octave wrapping
    +-- processors/
        +-- arpeggiator_core_test.cpp  # MODIFY: Add scale degree pitch offset tests,
                                       #   input quantization tests

plugins/ruinae/
+-- src/
|   +-- plugin_ids.h                   # MODIFY: Add 3 IDs (3300-3302), bump kNumParameters
|   +-- parameters/
|   |   +-- arpeggiator_params.h       # MODIFY: Add 3 atomics, extend handleArpParamChange,
|   |   |                              #   registerArpParams, saveArpParams, loadArpParams,
|   |   |                              #   loadArpParamsToController, formatArpParam
|   |   +-- dropdown_mappings.h        # MODIFY: Add arp scale constants, extend harmonizer
|   |   |                              #   scale count from 9 to 16
|   |   +-- harmonizer_params.h        # MODIFY: Extend registerHarmonizerParams dropdown
|   |                                  #   with 7 new scale strings
|   +-- processor/
|   |   +-- processor.cpp             # MODIFY: Apply scale params to ArpeggiatorCore
|   |   +-- processor.h               # MODIFY: Add prev-value tracking for scale params
|   +-- engine/
|   |   +-- ruinae_effects_chain.h    # MODIFY: setHarmonizerScale clamp 0-15
|   +-- controller/
|       +-- controller.cpp            # MODIFY: UI dimming for Root Note and Quantize toggle
+-- resources/
|   +-- editor.uidesc                 # MODIFY: Add Scale Type dropdown, Root Note dropdown,
|                                     #   Quantize Input toggle
+-- tests/
    +-- unit/
    |   +-- parameters/
    |       +-- arpeggiator_params_test.cpp  # MODIFY: Add scale param tests
    +-- integration/
        +-- arp_scale_mode_test.cpp          # NEW: End-to-end scale mode integration tests

plugins/shared/
+-- src/ui/
    +-- arp_lane_editor.h             # MODIFY: Scale-aware popup suffix ("st" vs "deg")
```

## Detailed Implementation Plan

### Phase 1: ScaleHarmonizer Refactoring (Layer 0)

**Goal**: Refactor ScaleHarmonizer to support variable-length scales and extend ScaleType to 16 values. This is a prerequisite for all subsequent phases.

**Files modified**:
- `dsp/include/krate/dsp/core/scale_harmonizer.h`
- `dsp/tests/unit/core/scale_harmonizer_test.cpp`

**Step 1.1: Write failing tests for ScaleData and new scale types**

Add tests in `scale_harmonizer_test.cpp`:
- Test ScaleData struct layout (intervals, degreeCount)
- Test kScaleIntervals has 16 entries with correct interval patterns for all scales
- Test kReverseLookup has 16 entries
- Test calculate() with Major Pentatonic (5-note): degree +5 wraps to next octave
- Test calculate() with Blues (6-note): degree +6 wraps to next octave
- Test calculate() with Diminished W-H (8-note): degree +8 wraps to next octave
- Test calculate() with Whole Tone (6-note): correct intervals
- Test quantizeToScale() with all new scale types
- Test getScaleDegree() with all new scale types
- Test backward compatibility: existing 9 scale types produce identical results
- Test octave wrapping with negative degrees for variable-size scales

**Step 1.2: Implement ScaleData struct**

Add to `scale_harmonizer.h` before the `ScaleType` enum:
```cpp
struct ScaleData {
    std::array<int, 12> intervals{};
    int degreeCount{0};
};
```

**Step 1.3: Extend ScaleType enum**

Append 7 new values after Chromatic=8:
```cpp
Locrian = 9,
MajorPentatonic = 10,
MinorPentatonic = 11,
Blues = 12,
WholeTone = 13,
DiminishedWH = 14,
DiminishedHW = 15,
```

Update constants:
- `kNumDiatonicScales`: Remove (concept no longer applies with variable sizes)
- `kNumScaleTypes`: Change from 9 to 16
- `kNumNonChromaticScales`: Add as 15
- `kDegreesPerScale`: Remove (now per-scale via ScaleData::degreeCount)

**Step 1.4: Replace kScaleIntervals table**

Change from `std::array<std::array<int, 7>, 8>` to `std::array<ScaleData, 16>` with all 16 scale entries. See data-model.md E-003 for exact data.

Remove `kChromaticIntervals` (now part of the main table).

**Step 1.5: Generalize buildReverseLookup()**

Change the inner loop from `for (int d = 0; d < 7; ++d)` to:
```cpp
const auto& scaleData = kScaleIntervals[static_cast<size_t>(scaleIndex)];
for (int d = 0; d < scaleData.degreeCount; ++d) {
    int diff = semitone - scaleData.intervals[static_cast<size_t>(d)];
    // ... rest unchanged
}
```

**Step 1.6: Extend kReverseLookup table**

Change from `std::array<std::array<int, 12>, 8>` to `std::array<std::array<int, 12>, 16>` with 16 entries.

**Step 1.7: Generalize calculate()**

In the diatonic mode section, replace:
- `detail::kScaleIntervals[scaleIdx]` access pattern: now returns `ScaleData`, access `.intervals` and `.degreeCount`
- `intervals[static_cast<size_t>(d)]` becomes `scaleData.intervals[static_cast<size_t>(d)]`
- `totalDegree / 7` becomes `totalDegree / degreeCount`
- `totalDegree % 7` becomes `totalDegree % degreeCount`
- `(totalDegree - 6) / 7` becomes `(totalDegree - (degreeCount - 1)) / degreeCount`
- `((totalDegree % 7) + 7) % 7` becomes `((totalDegree % degreeCount) + degreeCount) % degreeCount`

**Step 1.8: Update getScaleDegree()**

Change inner loop from `for (int d = 0; d < 7; ++d)` to iterate `scaleData.degreeCount`.

**Step 1.9: Update getScaleIntervals()**

Change return type from `std::array<int, 7>` to `ScaleData`. Update the Chromatic branch to return the Chromatic entry from the table.

**Step 1.10: Build and verify all tests pass**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests
build/windows-x64-release/bin/Release/dsp_tests.exe "[scale-harmonizer]"
```

Verify: all existing scale harmonizer tests still pass (backward compatibility). New tests pass.

### Phase 2: ArpeggiatorCore Integration (Layer 2)

**Goal**: Add ScaleHarmonizer to ArpeggiatorCore for scale-aware pitch offset and input quantization.

**Files modified**:
- `dsp/include/krate/dsp/processors/arpeggiator_core.h`
- `dsp/tests/unit/processors/arpeggiator_core_test.cpp`

**Step 2.1: Write failing tests**

Add tests in `arpeggiator_core_test.cpp`:
- Chromatic mode (default): pitch offset +2 on C4 = D4 (identical to current)
- Major scale, root C: pitch offset +2 on C4 = E4 (+4 semitones)
- Minor Pentatonic, root C: pitch offset +1 on C4 = Eb4 (+3 semitones)
- Major, root C: pitch offset +7 on C4 = C5 (octave wrap)
- Major, root C: pitch offset -1 on C4 = B3 (negative wrap)
- Major, root C: pitch offset +24 clips to MIDI 127
- Quantize input ON, Major C: C#4 input -> C4 in note pool
- Quantize input OFF, Major C: C#4 input -> C#4 in note pool
- Quantize input ON, Chromatic: C#4 passes through unchanged
- Pentatonic: pitch offset +6 wraps correctly (5-note octave)

**Step 2.2: Add ScaleHarmonizer member and setters**

In ArpeggiatorCore, add:
```cpp
#include <krate/dsp/core/scale_harmonizer.h>

// Private members:
ScaleHarmonizer scaleHarmonizer_;
bool scaleQuantizeInput_ = false;

// Public setters:
void setScaleType(ScaleType type) noexcept { scaleHarmonizer_.setScale(type); }
void setRootNote(int rootNote) noexcept { scaleHarmonizer_.setKey(rootNote); }
void setScaleQuantizeInput(bool enabled) noexcept { scaleQuantizeInput_ = enabled; }
```

**Step 2.3: Modify noteOn() for input quantization**

Before `heldNotes_.noteOn(note, velocity)`, add:
```cpp
uint8_t effectiveNote = note;
if (scaleQuantizeInput_ && scaleHarmonizer_.getScale() != ScaleType::Chromatic) {
    effectiveNote = static_cast<uint8_t>(
        std::clamp(scaleHarmonizer_.quantizeToScale(static_cast<int>(note)), 0, 127));
}
heldNotes_.noteOn(effectiveNote, velocity);
```

Also update the retrigger/latch logic to use `effectiveNote` where `note` was used.

**Step 2.4: Modify fireStep() for scale-aware pitch offset**

Replace lines 1567-1573 with:
```cpp
// Apply pitch offset to all notes in this step (FR-005, FR-006, FR-008, 084-arp-scale-mode)
for (size_t i = 0; i < result.count; ++i) {
    if (scaleHarmonizer_.getScale() != ScaleType::Chromatic && pitchOffset != 0) {
        // Scale mode: interpret pitchOffset as scale degrees
        auto interval = scaleHarmonizer_.calculate(
            static_cast<int>(result.notes[i]),
            static_cast<int>(pitchOffset));
        result.notes[i] = static_cast<uint8_t>(
            std::clamp(interval.targetNote, 0, 127));
    } else {
        // Chromatic mode or zero offset: direct semitone addition
        int offsetNote = static_cast<int>(result.notes[i]) +
                         static_cast<int>(pitchOffset);
        result.notes[i] = static_cast<uint8_t>(
            std::clamp(offsetNote, 0, 127));
    }
}
```

**Step 2.5: Build and verify**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_tests
build/windows-x64-release/bin/Release/dsp_tests.exe "[arpeggiator]"
```

### Phase 3: Plugin Parameter Layer

**Goal**: Add 3 new parameters with full parameter infrastructure.

**Files modified**:
- `plugins/ruinae/src/plugin_ids.h`
- `plugins/ruinae/src/parameters/arpeggiator_params.h`
- `plugins/ruinae/src/parameters/dropdown_mappings.h`
- `plugins/ruinae/src/processor/processor.cpp`
- `plugins/ruinae/src/processor/processor.h`
- `plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp`

**Step 3.1: Write failing tests**

Add tests in `arpeggiator_params_test.cpp`:
- handleArpParamChange: kArpScaleTypeId normalized 0.0 -> scaleType stores 8 (Chromatic, UI index 0)
- handleArpParamChange: kArpScaleTypeId normalized 1/15 -> scaleType stores 0 (Major, UI index 1)
- handleArpParamChange: kArpRootNoteId normalized 0.0 -> rootNote stores 0 (C)
- handleArpParamChange: kArpScaleQuantizeInputId 0.0 -> false, 1.0 -> true
- Save/load round-trip preserves all 3 values
- Load old preset (no scale fields) defaults to Chromatic/C/OFF

**Step 3.2: Add parameter IDs**

In `plugin_ids.h`, after `kArpConditionPlayheadId = 3299` (the last arp ID in the playhead block):
```cpp
// --- Scale Mode (084-arp-scale-mode, 3300-3302) ---
kArpScaleTypeId           = 3300,
kArpRootNoteId            = 3301,
kArpScaleQuantizeInputId  = 3302,
// 3303-3399: reserved for future arp params
```

Update `kArpEndId = 3302` and `kNumParameters = 3303`.

**Step 3.3: Add dropdown mapping constants**

In `dropdown_mappings.h`, add the arp scale constants (kArpScaleTypeCount, kArpRootNoteCount, kArpScaleDisplayOrder, kArpScaleEnumToDisplay). See data-model.md E-007.

**Step 3.4: Add atomics to ArpeggiatorParams**

Add 3 new fields after `ratchetSwing`. See data-model.md E-004.

**Step 3.5: Extend handleArpParamChange**

Add 3 new cases. Note the display-order mapping for kArpScaleTypeId. See contracts/arp_params_api.md.

**Step 3.6: Extend registerArpParams**

Add 3 new parameter registrations. See contracts/arp_params_api.md.

**Step 3.7: Extend saveArpParams / loadArpParams**

Append 3 new fields at end. Load uses `return true` for backward compatibility when fields are missing. See contracts/arp_params_api.md.

**Step 3.8: Extend loadArpParamsToController**

Map enum value to UI index using kArpScaleEnumToDisplay for the scale type. See contracts/arp_params_api.md.

**Step 3.9: Extend formatArpParam**

Add cases for kArpScaleTypeId, kArpRootNoteId, kArpScaleQuantizeInputId returning `kResultFalse` (let StringListParameter handle display).

**Step 3.10: Apply params in processor.cpp**

In `applyParamsToEngine()`, add after existing arp section:
```cpp
{
    const auto scaleType = static_cast<ScaleType>(
        arpParams_.scaleType.load(std::memory_order_relaxed));
    arpCore_.setScaleType(scaleType);
}
{
    const auto rootNote = arpParams_.rootNote.load(std::memory_order_relaxed);
    arpCore_.setRootNote(rootNote);
}
{
    const auto quantize = arpParams_.scaleQuantizeInput.load(std::memory_order_relaxed);
    arpCore_.setScaleQuantizeInput(quantize);
}
```

Note: These setters do NOT reset arp state, so they can be called unconditionally every block (unlike setMode/setRetrigger).

**Step 3.11: Build and verify**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests
build/windows-x64-release/bin/Release/ruinae_tests.exe "[arp-scale-mode]"
```

### Phase 4: Harmonizer Updates

**Goal**: Update Harmonizer consumers to support 16 scale types.

**Files modified**:
- `plugins/ruinae/src/parameters/dropdown_mappings.h`
- `plugins/ruinae/src/parameters/harmonizer_params.h`
- `plugins/ruinae/src/engine/ruinae_effects_chain.h`

**Step 4.1: Update kHarmonizerScaleCount**

In `dropdown_mappings.h`, change:
```cpp
inline constexpr int kHarmonizerScaleCount = 16;  // was 9
```

Update `kHarmonizerScaleStrings` array to include all 16 scale names in enum order.

**Step 4.2: Extend registerHarmonizerParams dropdown**

Note: Unlike the Arp Scale Type dropdown (which puts Chromatic first via display-order mapping), the Harmonizer Scale dropdown uses the ScaleType enum order directly (Major=0 first, Chromatic=8 in its natural position). No display-order mapping is needed for the Harmonizer.

In `harmonizer_params.h`, update the Scale dropdown from 9 to 16 entries:
```cpp
parameters.addParameter(createDropdownParameter(
    STR16("Harmonizer Scale"), kHarmonizerScaleId,
    {STR16("Major"), STR16("Natural Minor"), STR16("Harmonic Minor"),
     STR16("Melodic Minor"), STR16("Dorian"), STR16("Mixolydian"),
     STR16("Phrygian"), STR16("Lydian"), STR16("Chromatic"),
     STR16("Locrian"), STR16("Major Pentatonic"), STR16("Minor Pentatonic"),
     STR16("Blues"), STR16("Whole Tone"),
     STR16("Diminished (W-H)"), STR16("Diminished (H-W)")}));
```

**Step 4.3: Update setHarmonizerScale clamp**

In `ruinae_effects_chain.h`, change:
```cpp
void setHarmonizerScale(int scaleType) noexcept {
    harmonizer_.setScale(static_cast<ScaleType>(std::clamp(scaleType, 0, 15)));  // was 8
}
```

**Step 4.4: Build and verify**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target ruinae_tests
build/windows-x64-release/bin/Release/ruinae_tests.exe
```

### Phase 5: UI Changes

**Goal**: Add Scale Type dropdown, Root Note dropdown, Scale Quantize Input toggle, dimming logic, and popup suffix adaptation.

**Files modified**:
- `plugins/ruinae/resources/editor.uidesc`
- `plugins/shared/src/ui/arp_lane_editor.h`
- `plugins/ruinae/src/controller/controller.cpp`

**Step 5.1: Add UI controls in editor.uidesc**

Add three new controls in the arpeggiator section:
- Scale Type: `COptionMenu` bound to tag `kArpScaleTypeId`
- Root Note: `COptionMenu` bound to tag `kArpRootNoteId`
- Scale Quantize Input: `COnOffButton` bound to tag `kArpScaleQuantizeInputId`

**Step 5.2: Implement popup suffix adaptation**

In `arp_lane_editor.h`, modify `formatValueText()`:
- Add a `scaleType_` member (`int`, default 8 = Chromatic)
- Add `setScaleType(int type)` setter
- In `formatValueText()` for pitch mode:
  ```cpp
  if (laneType_ == ArpLaneType::kPitch) {
      int semitones = static_cast<int>(std::round((level - 0.5f) * 48.0f));
      const char* suffix = (scaleType_ != 8) ? " deg" : " st";
      if (semitones > 0)
          return "+" + std::to_string(semitones) + suffix;
      return std::to_string(semitones) + suffix;
  }
  ```

**Step 5.3: Implement UI dimming**

In controller.cpp, when Scale Type parameter changes:
- If Chromatic (value maps to enum 8): set Root Note dropdown and Quantize toggle alpha to 0.35, disable mouse
- If non-Chromatic: set alpha to 1.0, enable mouse

This uses the IDependent pattern documented in vst-guide/THREAD-SAFETY.md.

**Step 5.4: Update ArpLaneEditor scale type from controller**

When Scale Type parameter changes, the controller calls `setScaleType()` on the pitch lane editor so popup labels reflect the correct suffix.

**Step 5.5: Build and verify**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target Ruinae
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"
```

### Phase 6: Integration Testing and Cleanup

**Goal**: End-to-end integration tests, pluginval, clang-tidy, architecture docs.

**Files modified/created**:
- `plugins/ruinae/tests/integration/arp_scale_mode_test.cpp` (NEW)
- `specs/_architecture_/` (UPDATE)

**Step 6.1: Write integration tests**

Create `arp_scale_mode_test.cpp` testing:
- Full processor round-trip: set scale params, play notes, verify output
- Preset save/load with scale params
- Old preset backward compatibility (defaults to Chromatic)
- Scale type change mid-playback (new scale on next step)

**Step 6.2: Run pluginval**

```bash
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Ruinae.vst3"
```

**Step 6.3: Run clang-tidy**

```powershell
./tools/run-clang-tidy.ps1 -Target ruinae -BuildDir build/windows-ninja
./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja
```

**Step 6.4: Update architecture docs**

Update `specs/_architecture_/` with:
- ScaleData struct documentation
- Extended ScaleType enum (16 values)
- ArpeggiatorCore scale mode API

## Complexity Tracking

No constitution violations. All changes follow established patterns.
