# Implementation Plan: Ratcheting

**Branch**: `074-ratcheting` | **Date**: 2026-02-22 | **Spec**: `specs/074-ratcheting/spec.md`
**Input**: Feature specification from `specs/074-ratcheting/spec.md`

## Summary

Add ratcheting (sub-step retriggering, 1-4 per step) to the ArpeggiatorCore (DSP Layer 2) and expose it through the Ruinae plugin's VST3 parameter system. The implementation adds an `ArpLane<uint8_t> ratchetLane_` member to ArpeggiatorCore, extends `fireStep()` to initialize sub-step tracking state for ratcheted steps, and extends the `processBlock()` jump-ahead loop with a `NextEvent::SubStep` event type for sample-accurate sub-step event emission across block boundaries. Modifier interaction follows the established priority chain: Rest and Tie override ratcheting entirely; Slide and Accent apply to the first sub-step only.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: KrateDSP (Layer 2 processor, Layer 1 ArpLane primitive), Steinberg VST3 SDK
**Storage**: VST3 plugin state serialization via IBStreamer (binary read/write)
**Testing**: Catch2 (`dsp_tests` target for DSP unit tests, `ruinae_tests` for plugin integration)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: Monorepo with shared DSP library + plugin integration
**Performance Goals**: Zero heap allocation in audio path; logic-only (no DSP math), negligible CPU overhead
**Constraints**: Header-only DSP implementation (arpeggiator_core.h); all ratchet state tracking uses fixed-size member variables
**Scale/Scope**: 37 FRs, 13 SCs; touches 4 files in DSP, 4 files in plugin integration

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle I (VST3 Architecture Separation):**
- [x] Ratchet parameter IDs defined in `plugin_ids.h` (shared boundary)
- [x] Atomic storage in `ArpeggiatorParams` (processor side)
- [x] Registration in `registerArpParams()` (controller side)
- [x] No cross-inclusion between processor and controller

**Principle II (Real-Time Audio Thread Safety):**
- [x] All ratchet state tracking uses fixed-size member variables (uint8_t, size_t, std::array)
- [x] Zero allocation in `fireStep()` and `processBlock()` ratchet paths
- [x] No locks, exceptions, or I/O in audio path

**Principle III (Modern C++ Standards):**
- [x] Uses `std::array`, `std::clamp`, `constexpr`, value-initialized members
- [x] No raw new/delete

**Principle IV (SIMD & DSP Optimization):**
- [x] SIMD analysis completed (NOT BENEFICIAL -- see section below)
- [x] Scalar-first workflow applies (no SIMD phase needed)

**Principle VIII (Testing Discipline):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Principle IX (Layered DSP Architecture):**
- [x] ArpLane<T> is Layer 1 (primitives) -- no changes needed
- [x] ArpeggiatorCore is Layer 2 (processors) -- depends on Layer 1 only

**Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Principle XV (Pre-Implementation Research):**
- [x] Searched for all planned names -- no conflicts found

**Principle XVI (Honest Completion):**
- [x] Compliance table will be filled with specific file paths, line numbers, test names, and measured values

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: None. All new code is member variables and logic within existing ArpeggiatorCore.

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| `ratchetLane_` (member) | `grep -r "ratchetLane" dsp/ plugins/` | No | Create as new member |
| `ratchetSubStep` (variables) | `grep -r "ratchetSubStep" dsp/ plugins/` | No | Create as new members |
| `NextEvent::SubStep` (enum value) | `grep -r "SubStep" dsp/include/krate/dsp/processors/` | No | Add to existing local enum |

**Utility Functions to be created**: None. All new logic is inline within existing methods.

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| ArpLane<uint8_t> | `dsp/include/krate/dsp/primitives/arp_lane.h` | 1 | Ratchet lane container (same as modifierLane_) |
| ArpeggiatorCore | `dsp/include/krate/dsp/processors/arpeggiator_core.h` | 2 | Primary extension target |
| calculateGateDuration() | `arpeggiator_core.h:704-709` | 2 | Pattern for sub-step gate calculation |
| addPendingNoteOff() | `arpeggiator_core.h:753-776` | 2 | Schedule sub-step noteOffs |
| PendingNoteOff | `arpeggiator_core.h:620-623` | 2 | Track sub-step noteOff deadlines |
| ArpeggiatorParams | `plugins/ruinae/src/parameters/arpeggiator_params.h` | Plugin | Extended with ratchet atomic storage |
| handleArpParamChange() | `arpeggiator_params.h:92-235` | Plugin | Extended for ratchet params |
| registerArpParams() | `arpeggiator_params.h:243-402` | Plugin | Extended for ratchet param registration |
| formatArpParam() | `arpeggiator_params.h:409-576` | Plugin | Extended for ratchet display formatting |
| saveArpParams() | `arpeggiator_params.h:582-626` | Plugin | Extended to serialize ratchet data |
| loadArpParams() | `arpeggiator_params.h:634-730` | Plugin | Extended to deserialize ratchet data |
| applyParamsToEngine() | `processor.cpp:705-1321` | Plugin | Extended with ratchet lane expand-write-shrink |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/` - Layer 0 core utilities (no conflicts)
- [x] `dsp/include/krate/dsp/primitives/` - Layer 1 DSP primitives (ArpLane reused, no changes)
- [x] `dsp/include/krate/dsp/processors/arpeggiator_core.h` - Primary extension target
- [x] `specs/_architecture_/` - Component inventory confirms ArpLane<uint8_t> planned for ratchet
- [x] `plugins/ruinae/src/plugin_ids.h` - Current sentinel `kArpEndId = 3199`, `kNumParameters = 3200`
- [x] `plugins/ruinae/src/parameters/arpeggiator_params.h` - Current param struct and serialization
- [x] `plugins/ruinae/src/processor/processor.h` - `arpEvents_` already `std::array<..., 128>`
- [x] `plugins/ruinae/src/processor/processor.cpp` - Lane application pattern in `applyParamsToEngine()`

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: No new classes or structs are introduced. All changes are member additions to existing `ArpeggiatorCore` class and function extensions. All planned names (`ratchetLane_`, `ratchetSubStepsRemaining_`, `SubStep`, etc.) are unique in the codebase. The spec's own ODR search confirms this.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| ArpLane<uint8_t> | constructor | `std::array<T, MaxSteps> steps_{}` (zero-initializes to 0) | Yes |
| ArpLane<uint8_t> | setLength | `void setLength(size_t len) noexcept` (clamps to [1, MaxSteps]) | Yes |
| ArpLane<uint8_t> | setStep | `void setStep(size_t index, T value) noexcept` | Yes |
| ArpLane<uint8_t> | getStep | `[[nodiscard]] T getStep(size_t index) const noexcept` | Yes |
| ArpLane<uint8_t> | advance | `T advance() noexcept` (returns current, then increments position) | Yes |
| ArpLane<uint8_t> | reset | `void reset() noexcept` (sets position_ = 0) | Yes |
| ArpLane<uint8_t> | currentStep | `[[nodiscard]] size_t currentStep() const noexcept` | Yes |
| ArpeggiatorCore | calculateGateDuration | `inline size_t calculateGateDuration(float gateLaneValue = 1.0f) const noexcept` | Yes |
| ArpeggiatorCore | addPendingNoteOff | `inline void addPendingNoteOff(uint8_t note, size_t deadline, std::span<ArpEvent> out, size_t& eventCount, size_t maxEvents) noexcept` | Yes |
| ArpeggiatorCore | emitDuePendingNoteOffs | `inline void emitDuePendingNoteOffs(int32_t sampleOffset, std::span<ArpEvent> out, size_t& eventCount, size_t maxEvents) noexcept` | Yes |
| ArpeggiatorCore | cancelPendingNoteOffsForCurrentNotes | `inline void cancelPendingNoteOffsForCurrentNotes() noexcept` | Yes |
| ArpeggiatorCore | decrementPendingNoteOffs | `inline void decrementPendingNoteOffs(size_t samples) noexcept` | Yes |
| ArpeggiatorCore | resetLanes | `void resetLanes() noexcept` | Yes |
| ArpNoteResult | notes | `std::array<uint8_t, 32> notes` | Yes |
| ArpNoteResult | velocities | `std::array<uint8_t, 32> velocities` | Yes |
| ArpNoteResult | count | `size_t count` | Yes |
| PendingNoteOff | note | `uint8_t note` | Yes |
| PendingNoteOff | samplesRemaining | `size_t samplesRemaining` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/primitives/arp_lane.h` - ArpLane<T> template (full body read)
- [x] `dsp/include/krate/dsp/processors/arpeggiator_core.h` - ArpeggiatorCore class (full symbol overview + key method bodies read)
- [x] `plugins/ruinae/src/plugin_ids.h` - ParameterIDs enum (sentinel values verified)
- [x] `plugins/ruinae/src/parameters/arpeggiator_params.h` - All 6 functions read in full
- [x] `plugins/ruinae/src/processor/processor.h` - `arpEvents_` buffer size verified as 128
- [x] `plugins/ruinae/src/processor/processor.cpp` - Lane application pattern and event routing verified

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| ArpLane<uint8_t> | Default-initializes all steps to `T{}` which is `0` for uint8_t. Ratchet count 0 is invalid (must be >= 1). | Constructor must call `ratchetLane_.setStep(0, static_cast<uint8_t>(1))` |
| ArpLane::setStep | Clamps index to `[0, length-1]`. When lane length is 1, `setStep(5, val)` writes to index 0. | Must expand length to 32 before writing all steps (expand-write-shrink pattern) |
| ArpLane::advance | Returns current value THEN increments position. | Call advance() at the same point as other lanes (inside `fireStep()`) |
| calculateGateDuration | Uses `currentStepDuration_` as base. For sub-steps, cannot use this directly. | Compute sub-step gate inline: `max(1, subStepDuration * gatePercent/100 * gateLaneValue)` |
| kMaxEvents | Currently 64; processor buffer already 128. Only DSP constant needs updating. | Change `static constexpr size_t kMaxEvents = 64` to `128` |
| kArpEndId / kNumParameters | Currently 3199 / 3200. Ratchet step 31 = ID 3222 exceeds sentinel. | Update to 3299 / 3300 for Phase 7-8 headroom |
| handleArpParamChange default: block | Existing lane step handlers use range checks. Ratchet steps must be added similarly. | Add `else if (id >= kArpRatchetLaneStep0Id && id <= kArpRatchetLaneStep31Id)` |
| loadArpParams EOF handling | First new field EOF = backward compat success (return true). Mid-field EOF = corrupt (return false). | Follow exact pattern of modifier lane EOF handling |
| processBlock NextEvent priority | Current: `BarBoundary > NoteOff > Step`. Ratchet adds SubStep at lowest priority. | `BarBoundary > NoteOff > Step > SubStep` |
| modifierLane look-ahead | Currently reads `modifierLane_.getStep(currentStep())` for next step. With ratcheting, this applies only to last sub-step. | Set `ratchetIsLastSubStep_` flag; only suppress gate noteOff on last sub-step |
| Accent + Ratchet | Accent applies to first sub-step only. `ratchetVelocity_` stores NON-accented velocity for subsequent sub-steps. | In fireStep: apply accent to result.velocities BEFORE storing ratchetVelocity_ (store pre-accent) |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| Sub-step gate calculation | Inline 1-liner using member variables; only 1 consumer (ratchet in fireStep) |
| Sub-step duration calculation | Simple integer division; only 1 consumer |
| clearRatchetState() helper | Zeroes ratchet member variables; only used within ArpeggiatorCore |

**Decision**: No Layer 0 extraction needed. All ratchet logic is specific to ArpeggiatorCore and has no external consumers. The sub-step timing math (integer division with remainder) is trivial and does not warrant extraction.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | NO | Ratchet is event generation logic, not signal processing |
| **Data parallelism width** | 1 (sequential events) | Events must be emitted in strict temporal order |
| **Branch density in inner loop** | HIGH | Event type selection, modifier priority, ratchet count checks |
| **Dominant operations** | Integer comparisons, branching | No floating-point DSP math |
| **Current CPU budget vs expected usage** | < 0.1% expected | Logic-only, negligible CPU |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: Ratcheting is purely event-scheduling logic with no floating-point signal processing. The processBlock() loop is inherently sequential (events must be emitted in strict temporal order with correct priority). Branch-heavy modifier interaction and per-event state updates make SIMD parallelization counterproductive. CPU usage is negligible (< 0.1%) so optimization is unnecessary.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Early-out when ratchet count is 1 | Avoids sub-step state setup for non-ratcheted steps | LOW | YES -- FR-013 requires this |
| Skip ratchet lane advance when length 1 & value 1 | Marginal; advance() is already O(1) | LOW | NO -- not worth the branch |

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 2 (Processors)

**Related features at same layer** (from roadmap):
- Phase 7: Euclidean Timing (determines which steps are active)
- Phase 8: Conditional Trig (filters which steps fire)
- Phase 9: Spice/Dice (randomizes lane values)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Sub-step tracking pattern | LOW | Only ratcheting needs intra-step subdivision | Keep local |
| `clearRatchetState()` helper | LOW | Only ratcheting uses this state | Keep as private member function |
| `NextEvent::SubStep` enum value | LOW | Only ratcheting adds sub-step events | Keep in local enum |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared sub-step abstraction | Ratcheting is the only feature requiring intra-step subdivision. Euclidean/Conditional/Spice operate at the step level. |
| Sentinel update to 3299/3300 | Provides headroom for Phase 7 (3230-3233) and Phase 8 (3240-3280) without another sentinel bump |

### Review Trigger

After implementing Phase 7 (Euclidean):
- [ ] Does Euclidean need sub-step-like behavior? Unlikely (it modifies step activity, not subdivision)
- [ ] Does Euclidean interact with ratchet? Only via "inactive step = no ratchet" (same as Rest)
- [ ] Any duplicated step-filtering logic? Possibly -- both Rest and Euclidean-inactive suppress notes

## Project Structure

### Documentation (this feature)

```text
specs/074-ratcheting/
+-- plan.md              # This file
+-- research.md          # Phase 0 output
+-- data-model.md        # Phase 1 output
+-- quickstart.md        # Phase 1 output
+-- contracts/           # Phase 1 output
+-- spec.md              # Feature specification
+-- checklists/          # Requirement checklists
```

### Source Code (repository root)

```text
# DSP Layer 2 (primary changes)
dsp/include/krate/dsp/processors/arpeggiator_core.h
    - Add ArpLane<uint8_t> ratchetLane_ member
    - Add ratchetLane() accessor methods (const and non-const)
    - Add ratchet sub-step state tracking members (8 variables + 2 arrays)
    - Extend constructor: ratchetLane_.setStep(0, 1)
    - Extend resetLanes(): ratchetLane_.reset() + clear sub-step state
    - Extend setEnabled(): clear sub-step state on disable
    - Extend reset(): inherits from resetLanes()
    - Extend processBlock(): NextEvent::SubStep in jump-ahead loop
    - Extend fireStep(): ratchet lane advance, sub-step initialization, modifier interaction
    - Update kMaxEvents: 64 -> 128

# DSP Layer 2 tests
dsp/tests/unit/processors/arpeggiator_core_test.cpp
    - Ratchet count 1/2/3/4 event count tests (SC-001)
    - Sub-step timing accuracy tests (SC-002)
    - Phase 5 backward compatibility test (SC-003)
    - Per-sub-step gate tests (SC-004)
    - Modifier interaction tests: Rest, Tie, Slide, Accent (SC-005)
    - Polymetric cycling test (SC-006)
    - Timing drift test (SC-011)
    - Cross-block sub-step test (SC-012)
    - Chord mode ratchet stress test (SC-013)
    - Cleanup: disable mid-ratchet, transport stop mid-ratchet
    - Edge cases: ratchet 0 clamping, swing interaction, gate > 100%

# Plugin Integration
plugins/ruinae/src/plugin_ids.h
    - Add kArpRatchetLaneLengthId = 3190
    - Add kArpRatchetLaneStep0Id = 3191 through kArpRatchetLaneStep31Id = 3222
    - Update kArpEndId: 3199 -> 3299
    - Update kNumParameters: 3200 -> 3300

plugins/ruinae/src/parameters/arpeggiator_params.h
    - Extend ArpeggiatorParams struct: ratchetLaneLength, ratchetLaneSteps[32]
    - Extend ArpeggiatorParams constructor: default ratchet values
    - Extend handleArpParamChange(): ratchet length + step dispatch
    - Extend registerArpParams(): ratchet parameter registration
    - Extend formatArpParam(): ratchet display formatting ("N steps", "Nx")
    - Extend saveArpParams(): ratchet serialization after modifier data
    - Extend loadArpParams(): ratchet deserialization with EOF-safe backward compat

plugins/ruinae/src/processor/processor.cpp
    - Extend applyParamsToEngine(): ratchet lane expand-write-shrink pattern
    (Note: arpEvents_ in processor.h already 128 elements -- no change needed)

plugins/ruinae/src/controller/controller.cpp
    - No direct changes (registerArpParams is called from existing controller init)

# Plugin Integration tests
plugins/ruinae/tests/unit/processor/arpeggiator_integration_test.cpp
    - State save/load round-trip (SC-007)
    - Phase 5 preset backward compatibility (SC-008)
    - Parameter registration and formatting (SC-010)
```

**Structure Decision**: This feature extends existing files in the established monorepo structure. No new files are created except test cases added to existing test files. All DSP changes are in the header-only `arpeggiator_core.h`. All plugin integration changes follow the exact patterns established by Phase 4 (independent lanes) and Phase 5 (per-step modifiers).

## Complexity Tracking

No constitution violations. All design decisions align with established principles.

---

## Detailed Design Notes

### 1. ArpeggiatorCore Member Additions

New lane member (alongside existing lanes at lines 1099-1102):
```cpp
ArpLane<uint8_t> ratchetLane_;    // Per-step ratchet count (1-4)
```

New sub-step state members (new section after modifier state at lines 1108-1110):
```cpp
// Ratchet sub-step state (074-ratcheting)
uint8_t ratchetSubStepsRemaining_{0};  // Sub-steps left to fire (0 = inactive)
size_t ratchetSubStepDuration_{0};     // Duration per sub-step in samples
size_t ratchetSubStepCounter_{0};      // Sample counter within current sub-step
uint8_t ratchetNote_{0};              // MIDI note for retriggers (single note)
uint8_t ratchetVelocity_{0};          // Non-accented velocity for retriggers
size_t ratchetGateDuration_{0};       // Gate duration per sub-step
bool ratchetIsLastSubStep_{false};     // True when firing last sub-step (for look-ahead)
std::array<uint8_t, 32> ratchetNotes_{};      // Chord mode note numbers
std::array<uint8_t, 32> ratchetVelocities_{}; // Chord mode velocities
size_t ratchetNoteCount_{0};                   // Chord mode note count
```

### 2. Constructor Extension

Add after the existing `modifierLane_.setStep(0, ...)` line:
```cpp
// Set ratchet lane default: length=1, step[0]=1 (no ratcheting)
// ArpLane<uint8_t> zero-initializes steps to 0, which for ratchet
// would mean count 0 (invalid). Must explicitly set to 1.
ratchetLane_.setStep(0, static_cast<uint8_t>(1));
```

### 3. resetLanes() Extension

Add to resetLanes() after `tieActive_ = false`:
```cpp
ratchetLane_.reset();                  // 074-ratcheting: reset ratchet lane position
ratchetSubStepsRemaining_ = 0;         // 074-ratcheting: clear sub-step state
ratchetSubStepCounter_ = 0;
```

### 4. setEnabled() Extension

In the `enabled_ && !enabled` branch (disable path), after `needsDisableNoteOff_ = true`:
```cpp
ratchetSubStepsRemaining_ = 0;  // 074-ratcheting: clear pending sub-steps (FR-026)
ratchetSubStepCounter_ = 0;
```

### 5. processBlock() Transport-Stop Extension

In the `!ctx.isPlaying` and `wasPlaying_` branch, after clearing pendingNoteOffCount_:
```cpp
ratchetSubStepsRemaining_ = 0;  // 074-ratcheting: clear sub-step state (FR-027)
ratchetSubStepCounter_ = 0;
```

### 6. processBlock() Jump-Ahead Loop Extension

The jump-ahead loop currently computes `samplesUntilStep`, `samplesUntilNoteOff`, `samplesUntilBar`, and jumps to the minimum. The ratchet extension adds:

**After computing `samplesUntilBar`:**
```cpp
// Ratchet sub-step: how many samples until next sub-step boundary?
size_t samplesUntilSubStep = SIZE_MAX;
if (ratchetSubStepsRemaining_ > 0) {
    samplesUntilSubStep = ratchetSubStepDuration_ - ratchetSubStepCounter_;
}
```

**In the NextEvent priority logic, add SubStep:**
```cpp
enum class NextEvent { BlockEnd, NoteOff, Step, SubStep, BarBoundary };
```

Priority: `BarBoundary > NoteOff > Step > SubStep`

SubStep has the lowest priority. When SubStep coincides with BarBoundary, the bar boundary fires and clears sub-step state. When SubStep coincides with NoteOff, the NoteOff fires first (consistent with FR-021 ordering).

**After advancing time by jump, add sub-step counter advance:**
```cpp
if (ratchetSubStepsRemaining_ > 0) {
    ratchetSubStepCounter_ += jump;
}
```

**Add SubStep event handler:**
When `next == NextEvent::SubStep`:
1. Emit pending NoteOffs due at this sample
2. Emit noteOn(s) for ratcheted note(s) -- using stored `ratchetNote_`/`ratchetVelocity_` (single) or `ratchetNotes_`/`ratchetVelocities_`/`ratchetNoteCount_` (chord)
3. Update `currentArpNotes_`/`currentArpNoteCount_`
4. Determine if this is the last sub-step (`ratchetSubStepsRemaining_ == 1`) and check look-ahead
5. Schedule pending noteOff at `ratchetGateDuration_` (unless suppressed by look-ahead)
6. Decrement `ratchetSubStepsRemaining_`
7. Reset `ratchetSubStepCounter_ = 0`

### 7. fireStep() Ratchet Integration

After modifier evaluation (Rest/Tie return early) and before the existing note emission logic:

```
1. Advance ratchet lane: uint8_t ratchetCount = std::max(uint8_t{1}, ratchetLane_.advance())
2. If ratchetCount == 1: proceed with existing Phase 5 logic (no change)
3. If ratchetCount > 1 AND step is active (not Rest/Tie):
   a. Calculate subStepDuration = currentStepDuration_ / ratchetCount
   b. Calculate sub-step gate: max(1, subStepDuration * gatePercent/100 * gateLaneValue)
   c. Emit first sub-step noteOn (with accent if applicable, with legato if Slide)
   d. Store ratchet state: remaining = ratchetCount - 1, note, velocity (PRE-accent), gate, etc.
   e. For Chord: store all notes and velocities
   f. Set ratchetSubStepCounter_ = 0
   g. Set ratchetIsLastSubStep_ = (ratchetSubStepsRemaining_ == 1) -- true when the current remaining count means the next emission will be the final sub-step (i.e., exactly one sub-step remains after the first sub-step is emitted in fireStep)
   h. Schedule pending noteOff for first sub-step (unless last sub-step with suppressed gate)
```

The key insight is that `fireStep()` emits only the FIRST sub-step. Subsequent sub-steps are emitted by the `processBlock()` loop when `NextEvent::SubStep` fires.

### 8. Defensive Branch Extension (result.count == 0)

In the `else` branch of `fireStep()` where `result.count == 0`:
```cpp
ratchetLane_.advance();        // Keep ratchet lane synchronized (FR-036)
ratchetSubStepsRemaining_ = 0; // Clear any pending sub-steps
```

### 9. Plugin Parameter IDs

```cpp
// Ratchet Lane (074-ratcheting)
kArpRatchetLaneLengthId  = 3190,    // discrete: 1-32
kArpRatchetLaneStep0Id   = 3191,    // discrete: 1-4 (default 1)
kArpRatchetLaneStep1Id   = 3192,
// ... through ...
kArpRatchetLaneStep31Id  = 3222,

// Updated sentinels
kArpEndId = 3299,
kNumParameters = 3300,
```

### 10. Serialization Order

After existing modifier data (accent velocity, slide time), append:
```
ratchetLaneLength (int32)
ratchetLaneStep0 through ratchetLaneStep31 (32 x int32)
```

EOF at `ratchetLaneLength` read = Phase 5 backward compat (return true, keep defaults).
EOF after `ratchetLaneLength` but before all 32 steps = corrupt (return false).

### 11. kMaxEvents Update

```cpp
static constexpr size_t kMaxEvents = 128;  // Was 64; increased for ratcheted Chord mode
```

The processor-side buffer (`std::array<Krate::DSP::ArpEvent, 128> arpEvents_{}` in `processor.h` line 186) is ALREADY 128 elements. No change needed there.
