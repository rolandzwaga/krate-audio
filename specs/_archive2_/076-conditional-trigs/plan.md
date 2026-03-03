# Implementation Plan: Conditional Trig System

**Branch**: `076-conditional-trigs` | **Date**: 2026-02-22 | **Spec**: `specs/076-conditional-trigs/spec.md`
**Input**: Feature specification from `specs/076-conditional-trigs/spec.md`

## Summary

Add an Elektron-inspired conditional trig system to the ArpeggiatorCore (DSP Layer 2), providing per-step condition evaluation (probability, A:B ratios, First-loop-only, Fill/NotFill) that creates patterns evolving over multiple loops. The implementation adds a `conditionLane_` (`ArpLane<uint8_t>`), `loopCount_` (size_t), `fillActive_` (bool), and `conditionRng_` (`Xorshift32`, seed 7919) to ArpeggiatorCore, inserts a condition evaluation check in `fireStep()` between Euclidean gating and modifier evaluation, and reuses the existing `Xorshift32` PRNG from Layer 0 for probability evaluation. The three-layer gating chain becomes: Euclidean -> Condition -> Modifier. 34 new VST3 parameters (IDs 3240-3272 and 3280) are exposed through the Ruinae plugin. No sentinel adjustment is needed (IDs fall within the existing 3234-3299 reserved range).

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: KrateDSP (Layer 0 Xorshift32, Layer 2 ArpeggiatorCore, Layer 1 ArpLane), Steinberg VST3 SDK
**Storage**: VST3 plugin state serialization via IBStreamer (binary read/write)
**Testing**: Catch2 (`dsp_tests` target for DSP unit tests, `ruinae_tests` for plugin integration)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: Monorepo with shared DSP library + plugin integration
**Performance Goals**: Zero heap allocation in audio path; condition evaluation is O(1) per step (switch + PRNG call or modulo) with negligible CPU
**Constraints**: Header-only DSP implementation (arpeggiator_core.h); all condition state uses fixed-size member variables; PRNG consumption must be deterministic for a given pattern configuration
**Scale/Scope**: 48 FRs, 14 SCs; touches 2 files in DSP (arpeggiator_core.h + random.h include), 4 files in plugin integration

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle I (VST3 Architecture Separation):**
- [x] Condition parameter IDs defined in `plugin_ids.h` (shared boundary)
- [x] Atomic storage in `ArpeggiatorParams` (processor side)
- [x] Registration in `registerArpParams()` (controller side)
- [x] No cross-inclusion between processor and controller

**Principle II (Real-Time Audio Thread Safety):**
- [x] All condition state uses fixed-size member variables (ArpLane<uint8_t>, size_t, bool, Xorshift32)
- [x] Zero allocation in `fireStep()` and `processBlock()` condition paths
- [x] `Xorshift32::nextUnipolar()` is constexpr, noexcept -- no allocation
- [x] Condition evaluation is O(1): switch dispatch + single PRNG call or integer modulo
- [x] No locks, exceptions, or I/O in audio path

**Principle III (Modern C++ Standards):**
- [x] Uses `enum class TrigCondition : uint8_t`, `std::clamp`, value-initialized members
- [x] No raw new/delete

**Principle IV (SIMD & DSP Optimization):**
- [x] SIMD analysis completed (NOT BENEFICIAL -- see section below)
- [x] Scalar-first workflow applies (no SIMD phase needed)

**Principle VIII (Testing Discipline):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Principle IX (Layered DSP Architecture):**
- [x] Xorshift32 is Layer 0 (core) -- already included transitively via held_note_buffer.h -> NoteSelector
- [x] ArpeggiatorCore is Layer 2 (processors) -- depends on Layer 0 and Layer 1, allowed
- [x] New include `<krate/dsp/core/random.h>` must be added to `arpeggiator_core.h` for explicit dependency

**Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Principle XV (Pre-Implementation Research):**
- [x] Searched for all planned names -- no conflicts found (confirmed in spec)

**Principle XVI (Honest Completion):**
- [x] Compliance table will be filled with specific file paths, line numbers, test names, and measured values

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: `TrigCondition` (enum class in Krate::DSP namespace)

| Planned Type | Search Command | Existing? | Action |
|--------------|----------------|-----------|--------|
| `TrigCondition` | `grep -r "TrigCondition" dsp/ plugins/` | No | Create as new enum class |
| `conditionLane_` (member) | `grep -r "conditionLane" dsp/ plugins/` | No | Create as new ArpLane<uint8_t> member |
| `loopCount_` (member) | `grep -r "loopCount_" dsp/ plugins/` | No | Create as new size_t member |
| `fillActive_` (member) | `grep -r "fillActive_" dsp/ plugins/` | No | Create as new bool member |
| `conditionRng_` (member) | `grep -r "conditionRng_" dsp/ plugins/` | No | Create as new Xorshift32 member |
| `evaluateCondition` (method) | `grep -r "evaluateCondition" dsp/ plugins/` | No | Create as new private method |

**Utility Functions to be created**: `evaluateCondition` (private inline method on ArpeggiatorCore)

| Planned Function | Search Command | Existing? | Location | Action |
|------------------|----------------|-----------|----------|--------|
| `evaluateCondition` | `grep -r "evaluateCondition" dsp/ plugins/` | No | -- | Create as private inline method |
| `setFillActive` | `grep -r "setFillActive" dsp/ plugins/` | No | -- | Create as public setter |
| `fillActive` | `grep -r "fillActive()" dsp/ plugins/` | No | -- | Create as public getter |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| Xorshift32 | `dsp/include/krate/dsp/core/random.h` | 0 | Dedicated instance `conditionRng_{7919}` for probability evaluation |
| ArpLane<uint8_t> | `dsp/include/krate/dsp/primitives/arp_lane.h` | 1 | `conditionLane_` stores TrigCondition enum values as uint8_t |
| ArpeggiatorCore | `dsp/include/krate/dsp/processors/arpeggiator_core.h` | 2 | Primary extension target |
| ArpeggiatorParams | `plugins/ruinae/src/parameters/arpeggiator_params.h` | Plugin | Extended with condition lane atomics |
| handleArpParamChange() | `arpeggiator_params.h:107-288` | Plugin | Extended for condition param dispatch |
| registerArpParams() | `arpeggiator_params.h:296-500` | Plugin | Extended for condition param registration |
| formatArpParam() | `arpeggiator_params.h:507-719` | Plugin | Extended for condition display formatting |
| saveArpParams() | `arpeggiator_params.h:725-781` | Plugin | Extended to serialize condition data |
| loadArpParams() | `arpeggiator_params.h:789-912` | Plugin | Extended to deserialize condition data |
| loadArpParamsToController() | `arpeggiator_params.h:918-1073` | Plugin | Extended to propagate condition data to controller |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/random.h` - Layer 0 Xorshift32 (reuse, no changes)
- [x] `dsp/include/krate/dsp/primitives/arp_lane.h` - ArpLane (reuse, no changes)
- [x] `dsp/include/krate/dsp/processors/arpeggiator_core.h` - Primary extension target
- [x] `specs/_architecture_/` - Component inventory
- [x] `plugins/ruinae/src/plugin_ids.h` - Sentinel values: kArpEndId = 3299, kNumParameters = 3300 (no change needed)
- [x] `plugins/ruinae/src/parameters/arpeggiator_params.h` - Current param struct and serialization
- [x] `plugins/ruinae/src/processor/processor.cpp` - Lane application pattern in applyParamsToEngine()

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: Only one new type is introduced (`TrigCondition` enum class), which is unique in the codebase. All other changes are member additions to existing `ArpeggiatorCore` class and function extensions in existing plugin files. All planned names (`conditionLane_`, `loopCount_`, `fillActive_`, `conditionRng_`, `evaluateCondition`, `TrigCondition`) are confirmed unique via the spec's own ODR search.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| Xorshift32 | constructor | `explicit constexpr Xorshift32(uint32_t seedValue = 1) noexcept` | Yes |
| Xorshift32 | nextUnipolar | `[[nodiscard]] constexpr float nextUnipolar() noexcept` | Yes |
| ArpLane<uint8_t> | setLength | `void setLength(size_t len) noexcept` | Yes |
| ArpLane<uint8_t> | setStep | `void setStep(size_t index, T value) noexcept` | Yes |
| ArpLane<uint8_t> | getStep | `[[nodiscard]] T getStep(size_t index) const noexcept` | Yes |
| ArpLane<uint8_t> | advance | `T advance() noexcept` | Yes |
| ArpLane<uint8_t> | reset | `void reset() noexcept` | Yes |
| ArpLane<uint8_t> | currentStep | `[[nodiscard]] size_t currentStep() const noexcept` | Yes |
| ArpeggiatorCore | resetLanes | `void resetLanes() noexcept` | Yes |
| ArpeggiatorCore | reset | `inline void reset() noexcept` | Yes |
| ArpeggiatorCore | setEnabled | `inline void setEnabled(bool enabled) noexcept` | Yes |
| ArpeggiatorCore | fireStep | `inline void fireStep(const BlockContext& ctx, int32_t sampleOffset, std::span<ArpEvent> outputEvents, size_t& eventCount, size_t maxEvents, size_t samplesProcessed, size_t blockSize) noexcept` | Yes |
| ArpeggiatorCore | cancelPendingNoteOffsForCurrentNotes | `inline void cancelPendingNoteOffsForCurrentNotes() noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/random.h` - Xorshift32 class (full body read, lines 40-91)
- [x] `dsp/include/krate/dsp/primitives/arp_lane.h` - ArpLane template (full body read, lines 51-119)
- [x] `dsp/include/krate/dsp/processors/arpeggiator_core.h` - ArpeggiatorCore class (full 1558 lines read)
- [x] `plugins/ruinae/src/plugin_ids.h` - ParameterIDs enum (sentinels verified: kArpEndId = 3299, kNumParameters = 3300)
- [x] `plugins/ruinae/src/parameters/arpeggiator_params.h` - All functions read in full (1075 lines)
- [x] `plugins/ruinae/src/processor/processor.h` - Processor class (arpCore_ member verified at line 185)

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| Xorshift32 seed | Seed 0 is replaced with kDefaultSeed internally | Use seed 7919 (non-zero prime) |
| ArpLane<uint8_t> | Zero-initializes steps to 0 | 0 = TrigCondition::Always (correct default), but explicitly set step 0 in constructor for clarity |
| ArpLane::advance() | Returns current value THEN advances position | Loop count check must happen AFTER advance (check if `currentStep() == 0` after advance = lane just wrapped) |
| loadArpParams EOF handling | First new field EOF = backward compat success (return true). Mid-field EOF = corrupt (return false) | Follow exact pattern of Euclidean/ratchet lane EOF handling |
| conditionLane vs euclideanPosition | Euclidean is not a lane (scalar position), condition IS a lane | conditionLane_ cycles via advance() like other lanes; loopCount_ increments on conditionLane_ wrap |
| fillActive_ persistence | NOT reset on resetLanes() or reset() | Performance control preserved across resets. Only changed by setFillActive() |
| conditionRng_ persistence | NOT reset on resetLanes() or reset() | Ensures non-repeating sequences across resets. Only reseeded via constructor |
| processor.cpp expand-write-shrink | Lane application uses setLength(32), write all 32 steps, then setLength(actual) | conditionLane_ follows this exact pattern. The final setLength() must NOT affect loopCount_ |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| `evaluateCondition()` | Private method that reads member state (loopCount_, fillActive_, conditionRng_); only 1 consumer |
| `setFillActive()` / `fillActive()` | Standard getter/setter for member state |

**Decision**: No Layer 0 extraction needed. The `TrigCondition` enum class is defined in `arpeggiator_core.h` alongside the `ArpeggiatorCore` class that uses it (consistent with `ArpStepFlags`, `LatchMode`, `ArpRetriggerMode` defined in the same header). The `Xorshift32` PRNG (Layer 0) already provides the randomness primitive -- the ArpeggiatorCore just orchestrates its use.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | NO | Condition evaluation is purely event-scheduling logic |
| **Data parallelism width** | 1 (sequential events) | Events must be emitted in strict temporal order |
| **Branch density in inner loop** | HIGH | 18-way switch dispatch, probability check, fill check |
| **Dominant operations** | Integer modulo, PRNG call, boolean comparisons | O(1) per step |
| **Current CPU budget vs expected usage** | < 0.01% expected | A single switch + PRNG call per arp step adds essentially zero CPU |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: Condition evaluation adds a single O(1) switch dispatch per arp step, which fires at most ~50 times per second at maximum tempo. The total CPU contribution is negligible. The code path is inherently sequential (event ordering matters), branch-heavy (18-way condition switch), and data-dependent (PRNG state, loop count). SIMD parallelization would be counterproductive.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Early-out for Always condition (value 0) | Skips switch entirely for default steps | LOW | YES |
| PRNG consumed only for probability conditions | Avoids unnecessary state mutation | LOW | YES -- FR-016 requires this |

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 2 (Processors)

**Related features at same layer** (from roadmap):
- Phase 9: Spice/Dice + Humanize (Dice could randomize condition lane step values)
- Phase 10: Modulation Integration (fillActive_ could be a modulation destination)
- Phase 11: Arpeggiator UI (condition step editing requires dropdown per step)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| TrigCondition enum | MEDIUM | Phase 9 (Dice randomizes conditions), Phase 11 (UI dropdown) | Keep in arpeggiator_core.h alongside other arp enums |
| evaluateCondition() | LOW | Only used by ArpeggiatorCore::fireStep() | Keep as private member function |
| fillActive_ pattern | MEDIUM | Template for future performance controls | Document pattern; do not abstract prematurely |
| loopCount_ tracking | MEDIUM | Phase 10 could expose as modulation source | Keep as member; future API if needed |
| Three-layer gating chain | LOW | No additional gating layers planned | Document evaluation order in architecture docs |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| TrigCondition enum in arpeggiator_core.h | Consistent with ArpStepFlags, LatchMode, ArpRetriggerMode placement |
| No shared gating abstraction | Euclidean, Condition, and Modifier have fundamentally different semantics |
| evaluateCondition as separate method | Cleanly separates condition logic from fireStep() flow control |
| conditionRng_ not reset on reset()/resetLanes() | Ensures non-repeating probability sequences; spec FR-035 |

### Review Trigger

After implementing Phase 9 (Spice/Dice + Humanize):
- [ ] Does Dice need to randomize condition lane values? If so, verify uint8_t range [0, 17] is correctly bounded
- [ ] Does Humanize need a separate PRNG from conditionRng_? (Likely yes -- different seed to avoid correlation)
- [ ] Any duplicated gating logic between Condition suppress and Euclidean rest?

## Project Structure

### Documentation (this feature)

```text
specs/076-conditional-trigs/
+-- plan.md              # This file
+-- research.md          # Phase 0 output
+-- data-model.md        # Phase 1 output
+-- quickstart.md        # Phase 1 output
+-- contracts/           # Phase 1 output
+-- spec.md              # Feature specification
```

### Source Code (repository root)

```text
# DSP Layer 2 (primary changes)
dsp/include/krate/dsp/processors/arpeggiator_core.h
    - Add #include <krate/dsp/core/random.h>
    - Add TrigCondition enum class (FR-001, FR-002)
    - Add conditionLane_ (ArpLane<uint8_t>) member (FR-003)
    - Add loopCount_ (size_t) member (FR-008)
    - Add fillActive_ (bool) member (FR-009)
    - Add conditionRng_ (Xorshift32, seed 7919) member (FR-010)
    - Add conditionLane() accessors (FR-007)
    - Add setFillActive() / fillActive() (FR-020, FR-021)
    - Add evaluateCondition() private helper (FR-013)
    - Extend constructor: conditionLane_ default init (FR-005)
    - Extend resetLanes(): conditionLane_.reset(), loopCount_ = 0 (FR-034)
    - Extend fireStep(): condition lane advance + evaluation (FR-006, FR-012)
    - Extend fireStep() Euclidean rest path: advance conditionLane_ (FR-025)
    - Extend fireStep() defensive branch: advance conditionLane_ (FR-037)

# DSP Layer 2 tests
dsp/tests/unit/processors/arpeggiator_core_test.cpp
    - Probability conditions: statistical distribution (SC-001)
    - A:B ratio conditions: deterministic cycling (SC-002)
    - First condition: loop 0 only (SC-003)
    - Fill/NotFill conditions: fill toggle response (SC-004)
    - Default Always = Phase 7 identical output (SC-005)
    - Euclidean + Condition composition (SC-006)
    - Modifier + Condition composition (SC-007)
    - Polymetric condition lane cycling (SC-008)
    - Loop count reset paths (SC-013)
    - PRNG distinctness from NoteSelector (SC-014)
    - Condition + ratchet interaction (SC-007)
    - Condition + chord mode (FR-032)
    - Condition-skipped step breaks tie chain (FR-029)

# Plugin Integration
plugins/ruinae/src/plugin_ids.h
    - Add kArpConditionLaneLengthId = 3240
    - Add kArpConditionLaneStep0Id = 3241 through Step31Id = 3272
    - Add kArpFillToggleId = 3280
    - (kArpEndId = 3299 and kNumParameters = 3300 remain unchanged)

plugins/ruinae/src/parameters/arpeggiator_params.h
    - Extend ArpeggiatorParams struct: conditionLaneLength, conditionLaneSteps[32], fillToggle
    - Extend handleArpParamChange(): condition parameter dispatch (3240-3272, 3280)
    - Extend registerArpParams(): condition parameter registration (34 params)
    - Extend formatArpParam(): condition display formatting
    - Extend saveArpParams(): condition serialization after Euclidean data
    - Extend loadArpParams(): condition deserialization with EOF-safe backward compat
    - Extend loadArpParamsToController(): condition controller state sync

plugins/ruinae/src/processor/processor.cpp
    - Extend applyParamsToEngine(): transfer condition lane data + fill toggle to ArpeggiatorCore
      (expand-write-shrink pattern for lane, setFillActive for toggle)

# Plugin Integration tests
plugins/ruinae/tests/unit/processor/arp_integration_test.cpp
    - State save/load round-trip (SC-009)
    - Phase 7 preset backward compatibility (SC-010)

plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp
    - Parameter registration and formatting (SC-012)
```

**Structure Decision**: This feature extends existing files in the established monorepo structure. No new files are created except test cases added to existing test files. All DSP changes are in the header-only `arpeggiator_core.h`. All plugin integration changes follow the exact patterns established by Phases 4-7.

## Complexity Tracking

No constitution violations. All design decisions align with established principles.

---

## Detailed Design Notes

### 1. TrigCondition Enum (FR-001, FR-002)

Define immediately after the existing `ArpRetriggerMode` enum in `arpeggiator_core.h`:

```cpp
/// @brief Conditional trigger type for per-step condition evaluation.
/// Each arp step has exactly one condition (not a bitmask).
enum class TrigCondition : uint8_t {
    Always = 0,       ///< Step fires unconditionally (default)
    Prob10,           ///< ~10% probability
    Prob25,           ///< ~25% probability
    Prob50,           ///< ~50% probability
    Prob75,           ///< ~75% probability
    Prob90,           ///< ~90% probability
    Ratio_1_2,        ///< Fire on 1st of every 2 loops
    Ratio_2_2,        ///< Fire on 2nd of every 2 loops
    Ratio_1_3,        ///< Fire on 1st of every 3 loops
    Ratio_2_3,        ///< Fire on 2nd of every 3 loops
    Ratio_3_3,        ///< Fire on 3rd of every 3 loops
    Ratio_1_4,        ///< Fire on 1st of every 4 loops
    Ratio_2_4,        ///< Fire on 2nd of every 4 loops
    Ratio_3_4,        ///< Fire on 3rd of every 4 loops
    Ratio_4_4,        ///< Fire on 4th of every 4 loops
    First,            ///< Fire only on first loop (loopCount == 0)
    Fill,             ///< Fire only when fill mode is active
    NotFill,          ///< Fire only when fill mode is NOT active
    kCount            ///< Sentinel (18). Not a valid condition.
};
```

### 2. ArpeggiatorCore Include Addition

Add to the includes (before `<algorithm>`):

```cpp
#include <krate/dsp/core/random.h>
```

### 3. ArpeggiatorCore Member Additions

New condition state members (new section after Euclidean state, before configuration state):

```cpp
// =========================================================================
// Condition State (076-conditional-trigs)
// =========================================================================

ArpLane<uint8_t> conditionLane_;     ///< Per-step condition (TrigCondition as uint8_t)
size_t loopCount_{0};                ///< Condition lane cycle counter
bool fillActive_{false};             ///< Fill mode performance toggle
Xorshift32 conditionRng_{7919};      ///< Dedicated PRNG for probability (prime seed)
```

### 4. Constructor Extension (FR-005)

Add to the constructor after the `regenerateEuclideanPattern()` call:

```cpp
// 076-conditional-trigs: initialize condition lane default (FR-005)
// ArpLane<uint8_t> zero-initializes to 0 = TrigCondition::Always,
// but explicit set for clarity and consistency.
conditionLane_.setStep(0, static_cast<uint8_t>(TrigCondition::Always));
```

### 5. Condition Lane Accessors (FR-007)

Add new accessor section after Euclidean Timing Getters:

```cpp
// =========================================================================
// Condition Lane Accessors (076-conditional-trigs, FR-007)
// =========================================================================

/// @brief Access the condition lane for reading/writing step values.
ArpLane<uint8_t>& conditionLane() noexcept { return conditionLane_; }

/// @brief Const access to the condition lane.
[[nodiscard]] const ArpLane<uint8_t>& conditionLane() const noexcept {
    return conditionLane_;
}
```

### 6. Fill Mode API (FR-020, FR-021)

Add after condition lane accessors:

```cpp
// =========================================================================
// Fill Mode (076-conditional-trigs, FR-020, FR-021)
// =========================================================================

/// @brief Set fill mode active state. Real-time safe, no side effects.
void setFillActive(bool active) noexcept { fillActive_ = active; }

/// @brief Get current fill mode state.
[[nodiscard]] bool fillActive() const noexcept { return fillActive_; }
```

### 7. evaluateCondition() Private Helper (FR-013)

New private inline method:

```cpp
/// @brief Evaluate a TrigCondition for the current step.
/// @param condition The condition to evaluate (TrigCondition enum as uint8_t)
/// @return true if the step should fire, false if it should be treated as rest
inline bool evaluateCondition(uint8_t condition) noexcept {
    const auto cond = static_cast<TrigCondition>(condition);
    switch (cond) {
        case TrigCondition::Always:
            return true;
        case TrigCondition::Prob10:
            return conditionRng_.nextUnipolar() < 0.10f;
        case TrigCondition::Prob25:
            return conditionRng_.nextUnipolar() < 0.25f;
        case TrigCondition::Prob50:
            return conditionRng_.nextUnipolar() < 0.50f;
        case TrigCondition::Prob75:
            return conditionRng_.nextUnipolar() < 0.75f;
        case TrigCondition::Prob90:
            return conditionRng_.nextUnipolar() < 0.90f;
        case TrigCondition::Ratio_1_2:
            return loopCount_ % 2 == 0;
        case TrigCondition::Ratio_2_2:
            return loopCount_ % 2 == 1;
        case TrigCondition::Ratio_1_3:
            return loopCount_ % 3 == 0;
        case TrigCondition::Ratio_2_3:
            return loopCount_ % 3 == 1;
        case TrigCondition::Ratio_3_3:
            return loopCount_ % 3 == 2;
        case TrigCondition::Ratio_1_4:
            return loopCount_ % 4 == 0;
        case TrigCondition::Ratio_2_4:
            return loopCount_ % 4 == 1;
        case TrigCondition::Ratio_3_4:
            return loopCount_ % 4 == 2;
        case TrigCondition::Ratio_4_4:
            return loopCount_ % 4 == 3;
        case TrigCondition::First:
            return loopCount_ == 0;
        case TrigCondition::Fill:
            return fillActive_;
        case TrigCondition::NotFill:
            return !fillActive_;
        default:
            return true;  // Out-of-range: treat as Always (defensive)
    }
}
```

### 8. resetLanes() Extension (FR-034)

Add to `resetLanes()` after the Euclidean reset line:

```cpp
conditionLane_.reset();              // 076-conditional-trigs: reset condition lane position
loopCount_ = 0;                      // 076-conditional-trigs: reset loop counter
// fillActive_ intentionally NOT reset (FR-022: performance control)
// conditionRng_ intentionally NOT reset (FR-035: continuous randomness)
```

### 9. fireStep() Condition Evaluation Integration

The condition evaluation is inserted AFTER Euclidean gating (if enabled) but BEFORE modifier evaluation. The condition lane advances simultaneously with all other lanes.

**Modified fireStep() flow (pseudocode):**

```
1. result = selector_.advance(heldNotes_)
2. if (result.count > 0):
   a. Advance ALL lanes: velocity, gate, pitch, modifier, ratchet, *** condition ***
   b. *** NEW: Check if condition lane wrapped (loopCount_ increment) ***
      - If conditionLane_.currentStep() == 0 after advance, and this is NOT
        the very first advance (position was > 0 before), increment loopCount_
      - Implementation: before advance, save prevPos = conditionLane_.currentStep()
        After all advances, if conditionLane_.currentStep() == 0 && prevPos != 0,
        increment loopCount_. Edge case: length 1 always wraps so always increments.
   c. Euclidean gating check (if enabled) -- rest steps short-circuit BEFORE condition
      NOTE: condition lane already advanced in step 2a (all lanes advance unconditionally)
   d. *** NEW: Condition evaluation ***
      - Read condition value from the PREVIOUS position (the value returned by advance())
      - Call evaluateCondition(conditionValue)
      - If condition FAILS:
        * Cancel pending noteOffs for current notes
        * Emit noteOff for all currently sounding notes
        * Set currentArpNoteCount_ = 0
        * Set tieActive_ = false (break tie chain)
        * Increment swingStepCounter_
        * Recalculate currentStepDuration_
        * return (early exit -- skip modifier evaluation and ratcheting)
   e. Modifier evaluation: Rest > Tie > Slide > Accent (existing code, unchanged)
   f. Ratcheting (existing code, unchanged)
3. else (result.count == 0, defensive branch):
   a. Advance modifier, ratchet, *** condition *** lanes (existing + new)
   b. *** NEW: Check condition lane wrap, increment loopCount_ ***
   c. Advance euclideanPosition_ if enabled (existing)
   d. Existing defensive cleanup
```

**Implementation detail for condition lane advance and loopCount_ tracking:**

The condition lane `advance()` returns the current value and then moves the position forward. To detect wrap, we check if `currentStep() == 0` after advance. However, the initial state (position 0, first ever advance) should NOT count as a wrap. The cleanest approach: since all lanes start at position 0, and `advance()` returns value at position 0 then moves to position 1 (or wraps if length is 1), we detect wrap by checking:

```cpp
// Before advance:
// conditionLane_.currentStep() is at some position P
uint8_t condValue = conditionLane_.advance();
// After advance: position is now (P+1) % length
// If position wrapped to 0 AND this is not the very first step (P was > 0):
// Actually, for length > 1: wrap means position went from length-1 to 0.
// For length == 1: position goes from 0 to 0 (always wraps).
//
// Simplest detection: after advance, if currentStep() == 0, the lane wrapped.
// But on the FIRST advance (position was 0), advance moves to 1 (no wrap) or
// stays at 0 (length 1, wraps). For length 1, we DO want to count this as a wrap
// on every step (per FR-018). For length > 1, first advance goes from 0 to 1,
// so currentStep() == 1, no wrap detected -- correct.
//
// Therefore: simply check currentStep() == 0 after advance.
if (conditionLane_.currentStep() == 0) {
    ++loopCount_;
}
```

Wait -- there is a subtlety. When length is 1, `advance()` reads position 0 then sets position to `(0+1) % 1 = 0`. So after advance, `currentStep() == 0`. This triggers `++loopCount_` on every step, which is correct per FR-018 (length-1 condition lane wraps on every step).

When length is 4, first advance: reads position 0, moves to 1. `currentStep() == 1`, no increment. Second advance: position 1 to 2. Third: 2 to 3. Fourth: 3 to 0. `currentStep() == 0`, increment loopCount_. This is correct.

However, on the very first advance ever (position starts at 0): for length > 1, it goes to 1, no wrap. For length 1, it goes to 0, wrap counted. This is correct -- for length 1, the first step completes one cycle.

### 10. Euclidean Rest Path: Condition Lane Advance (FR-025)

The Euclidean rest path already advances all lanes before checking the hit/rest bit. The condition lane advance is added to the "advance all lanes" section (step 2a above), which happens BEFORE the Euclidean check. So condition lane already advances on Euclidean rest steps -- no additional code needed in the Euclidean rest branch itself.

However, the PRNG is NOT consumed on Euclidean rest steps (FR-016) because condition evaluation does not occur (the step was already gated by Euclidean). This is naturally correct because `evaluateCondition()` is only called if the step passes Euclidean gating.

### 11. Plugin Parameter IDs

```cpp
// 3234-3239: reserved (gap before condition lane; reserved for use before Phase 9)
// --- Condition Lane (076-conditional-trigs, 3240-3272) ---
kArpConditionLaneLengthId  = 3240,    // discrete: 1-32 (RangeParameter, stepCount=31)
kArpConditionLaneStep0Id   = 3241,    // discrete: 0-17 (RangeParameter, stepCount=17)
kArpConditionLaneStep1Id   = 3242,
// ... through ...
kArpConditionLaneStep31Id  = 3272,
// 3273-3279: reserved (gap between condition step IDs and fill toggle; reserved for future condition-lane extensions)
// --- Fill Toggle (076-conditional-trigs, 3280) ---
kArpFillToggleId           = 3280,    // discrete: 0-1 (latching toggle)
```

### 12. ArpeggiatorParams Struct Extension

Add after Euclidean timing members:

```cpp
// --- Condition Lane (076-conditional-trigs) ---
std::atomic<int>   conditionLaneLength{1};       // 1-32
std::array<std::atomic<int>, 32> conditionLaneSteps{};  // 0-17 (TrigCondition, int for lock-free)
std::atomic<bool>  fillToggle{false};            // Fill mode toggle
```

Constructor addition:

```cpp
// conditionLaneSteps default to 0 (TrigCondition::Always) via value-initialization -- correct
// No explicit loop needed since 0 = Always is the correct default.
```

### 13. handleArpParamChange() Extension

Add new cases in the switch statement after Euclidean cases:

```cpp
// --- Condition Lane (076-conditional-trigs) ---
case kArpConditionLaneLengthId:
    // RangeParameter: 0-1 -> 1-32 (stepCount=31)
    params.conditionLaneLength.store(
        std::clamp(static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32),
        std::memory_order_relaxed);
    break;
case kArpFillToggleId:
    params.fillToggle.store(value >= 0.5, std::memory_order_relaxed);
    break;
```

And in the default section (step ID range):

```cpp
// Condition lane steps: 3241-3272
else if (id >= kArpConditionLaneStep0Id && id <= kArpConditionLaneStep31Id) {
    int step = std::clamp(
        static_cast<int>(std::round(value * 17.0)), 0, 17);
    params.conditionLaneSteps[id - kArpConditionLaneStep0Id].store(
        step, std::memory_order_relaxed);
}
```

### 14. registerArpParams() Extension

Add after Euclidean parameter registration:

```cpp
// --- Condition Lane (076-conditional-trigs) ---

// Condition lane length: RangeParameter 1-32, default 1, stepCount 31
parameters.addParameter(
    new RangeParameter(STR16("Arp Cond Lane Len"), kArpConditionLaneLengthId,
                      STR16(""), 1, 32, 1, 31,
                      ParameterInfo::kCanAutomate));

// Condition lane steps: loop 0-31, RangeParameter 0-17, default 0 (Always), stepCount 17
for (int i = 0; i < 32; ++i) {
    char name[48];
    snprintf(name, sizeof(name), "Arp Cond Step %d", i + 1);
    Steinberg::Vst::String128 name16;
    Steinberg::UString(name16, 128).fromAscii(name);
    parameters.addParameter(
        new RangeParameter(name16,
            static_cast<ParamID>(kArpConditionLaneStep0Id + i),
            STR16(""), 0, 17, 0, 17,
            ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden));
}

// Fill toggle: Toggle (0 or 1), default off
parameters.addParameter(STR16("Arp Fill"), STR16(""), 1, 0.0,
    ParameterInfo::kCanAutomate, kArpFillToggleId);
```

### 15. formatArpParam() Extension

Add new cases:

```cpp
case kArpConditionLaneLengthId: {
    char8 text[32];
    int len = std::clamp(static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32);
    snprintf(text, sizeof(text), len == 1 ? "%d step" : "%d steps", len);
    UString(string, 128).fromAscii(text);
    return kResultOk;
}
case kArpFillToggleId: {
    UString(string, 128).fromAscii(value >= 0.5 ? "On" : "Off");
    return kResultOk;
}
```

And in the default section:

```cpp
// Condition lane steps: display TrigCondition name
if (id >= kArpConditionLaneStep0Id && id <= kArpConditionLaneStep31Id) {
    static const char* const kCondNames[] = {
        "Always", "10%", "25%", "50%", "75%", "90%",
        "1:2", "2:2", "1:3", "2:3", "3:3",
        "1:4", "2:4", "3:4", "4:4",
        "1st", "Fill", "!Fill"
    };
    int idx = std::clamp(
        static_cast<int>(std::round(value * 17.0)), 0, 17);
    UString(string, 128).fromAscii(kCondNames[idx]);
    return kResultOk;
}
```

### 16. Serialization Order

After existing Euclidean data (euclideanEnabled, euclideanHits, euclideanSteps, euclideanRotation), append:

```
conditionLaneLength (int32: 1-32)
conditionLaneSteps[0..31] (int32 x 32: 0-17 each)
fillToggle (int32: 0 or 1)
```

### 17. saveArpParams() Extension

Add after Euclidean serialization:

```cpp
// --- Condition Lane (076-conditional-trigs) ---
streamer.writeInt32(params.conditionLaneLength.load(std::memory_order_relaxed));
for (int i = 0; i < 32; ++i) {
    streamer.writeInt32(params.conditionLaneSteps[i].load(std::memory_order_relaxed));
}
streamer.writeInt32(params.fillToggle.load(std::memory_order_relaxed) ? 1 : 0);
```

### 18. loadArpParams() Extension

Add after Euclidean deserialization (replace the final `return true;`):

```cpp
// --- Condition Lane (076-conditional-trigs) ---
// EOF-safe: if condition data is missing entirely (Phase 7 preset), keep defaults.
if (!streamer.readInt32(intVal)) return true;  // EOF at first condition field = Phase 7 compat
params.conditionLaneLength.store(std::clamp(intVal, 1, 32), std::memory_order_relaxed);

// From here, EOF signals a corrupt stream (length was present but steps are not)
for (int i = 0; i < 32; ++i) {
    if (!streamer.readInt32(intVal)) return false;  // Corrupt: length present but no step data
    params.conditionLaneSteps[i].store(
        std::clamp(intVal, 0, 17), std::memory_order_relaxed);
}

// Fill toggle
if (!streamer.readInt32(intVal)) return false;  // Corrupt: steps present but no fill toggle
params.fillToggle.store(intVal != 0, std::memory_order_relaxed);

return true;
```

### 19. loadArpParamsToController() Extension

Add after Euclidean controller sync:

```cpp
// --- Condition Lane (076-conditional-trigs) ---
// EOF-safe: if condition data is missing (Phase 7 preset), keep controller defaults
if (!streamer.readInt32(intVal)) return;
setParam(kArpConditionLaneLengthId,
    static_cast<double>(std::clamp(intVal, 1, 32) - 1) / 31.0);

for (int i = 0; i < 32; ++i) {
    if (!streamer.readInt32(intVal)) return;
    setParam(static_cast<Steinberg::Vst::ParamID>(kArpConditionLaneStep0Id + i),
        static_cast<double>(std::clamp(intVal, 0, 17)) / 17.0);
}

// Fill toggle
if (!streamer.readInt32(intVal)) return;
setParam(kArpFillToggleId, intVal != 0 ? 1.0 : 0.0);
```

### 20. processor.cpp applyParamsToEngine() Extension

Add after Euclidean parameter application:

```cpp
// --- Condition Lane (076-conditional-trigs) ---
{
    const auto condLen = arpParams_.conditionLaneLength.load(std::memory_order_relaxed);
    arpCore_.conditionLane().setLength(32);  // Expand first
    for (int i = 0; i < 32; ++i) {
        int val = std::clamp(
            arpParams_.conditionLaneSteps[i].load(std::memory_order_relaxed), 0, 17);
        arpCore_.conditionLane().setStep(
            static_cast<size_t>(i), static_cast<uint8_t>(val));
    }
    arpCore_.conditionLane().setLength(static_cast<size_t>(condLen));  // Shrink to actual
}
arpCore_.setFillActive(arpParams_.fillToggle.load(std::memory_order_relaxed));
```
