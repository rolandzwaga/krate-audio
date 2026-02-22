# Implementation Plan: Euclidean Timing Mode

**Branch**: `075-euclidean-timing` | **Date**: 2026-02-22 | **Spec**: `specs/075-euclidean-timing/spec.md`
**Input**: Feature specification from `specs/075-euclidean-timing/spec.md`

## Summary

Add Euclidean timing mode to the ArpeggiatorCore (DSP Layer 2), providing Bjorklund-algorithm-based rhythmic gating that determines which arp steps fire notes and which are silent rests. The implementation adds 6 member variables to ArpeggiatorCore (`euclideanEnabled_`, `euclideanHits_`, `euclideanSteps_`, `euclideanRotation_`, `euclideanPosition_`, `euclideanPattern_`), inserts a pre-fire gating check in `fireStep()` before modifier evaluation, and reuses the existing `EuclideanPattern` class from Layer 0 for pattern generation. Evaluation order: Euclidean gating -> Modifier priority chain (Rest > Tie > Slide > Accent) -> Ratcheting. Four new VST3 parameters (IDs 3230-3233) are exposed through the Ruinae plugin. No sentinel adjustment is needed (IDs fall within the existing 3223-3299 reserved range established in Phase 6).

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: KrateDSP (Layer 0 EuclideanPattern, Layer 2 ArpeggiatorCore, Layer 1 ArpLane), Steinberg VST3 SDK
**Storage**: VST3 plugin state serialization via IBStreamer (binary read/write)
**Testing**: Catch2 (`dsp_tests` target for DSP unit tests, `ruinae_tests` for plugin integration)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: Monorepo with shared DSP library + plugin integration
**Performance Goals**: Zero heap allocation in audio path; Euclidean gating is a single O(1) bit check per step (negligible CPU)
**Constraints**: Header-only DSP implementation (arpeggiator_core.h); all Euclidean state uses fixed-size member variables; pattern generation is constexpr/static/noexcept
**Scale/Scope**: 35 FRs, 12 SCs; touches 2 files in DSP, 4 files in plugin integration

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle I (VST3 Architecture Separation):**
- [x] Euclidean parameter IDs defined in `plugin_ids.h` (shared boundary)
- [x] Atomic storage in `ArpeggiatorParams` (processor side)
- [x] Registration in `registerArpParams()` (controller side)
- [x] No cross-inclusion between processor and controller

**Principle II (Real-Time Audio Thread Safety):**
- [x] All Euclidean state uses fixed-size member variables (bool, int, size_t, uint32_t)
- [x] Zero allocation in `fireStep()` and `processBlock()` Euclidean paths
- [x] `EuclideanPattern::generate()` is constexpr, static, noexcept -- no allocation
- [x] `EuclideanPattern::isHit()` is an O(1) bit shift and mask -- no allocation
- [x] No locks, exceptions, or I/O in audio path

**Principle III (Modern C++ Standards):**
- [x] Uses `std::clamp`, `constexpr`, value-initialized members
- [x] No raw new/delete

**Principle IV (SIMD & DSP Optimization):**
- [x] SIMD analysis completed (NOT BENEFICIAL -- see section below)
- [x] Scalar-first workflow applies (no SIMD phase needed)

**Principle VIII (Testing Discipline):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Principle IX (Layered DSP Architecture):**
- [x] EuclideanPattern is Layer 0 (core) -- no changes needed to this class
- [x] ArpeggiatorCore is Layer 2 (processors) -- depends on Layer 0, allowed
- [x] New include `<krate/dsp/core/euclidean_pattern.h>` must be added to `arpeggiator_core.h`

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
| `euclideanEnabled_` (member) | `grep -r "euclideanEnabled" dsp/ plugins/` | No | Create as new member |
| `euclideanPattern_` (member) | `grep -r "euclideanPattern_" dsp/ plugins/` | No (class `EuclideanPattern` exists) | Create as new member -- trailing underscore differentiates |
| `euclideanPosition_` (member) | `grep -r "euclideanPosition" dsp/ plugins/` | No | Create as new member |
| `euclideanHits_` (member) | `grep -r "euclideanHits_" dsp/ plugins/` | No | Create as new member |
| `euclideanSteps_` (member) | `grep -r "euclideanSteps_" dsp/ plugins/` | No | Create as new member |
| `euclideanRotation_` (member) | `grep -r "euclideanRotation_" dsp/ plugins/` | No | Create as new member |
| `regenerateEuclideanPattern` (method) | `grep -r "regenerateEuclideanPattern" dsp/ plugins/` | No | Create as new private method |

**Utility Functions to be created**: None. All new logic is inline within existing methods.

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|-----------|----------|-------|---------------------|
| EuclideanPattern | `dsp/include/krate/dsp/core/euclidean_pattern.h` | 0 | `generate(hits, steps, rotation)` for bitmask creation; `isHit(pattern, position, steps)` for per-step gating |
| ArpeggiatorCore | `dsp/include/krate/dsp/processors/arpeggiator_core.h` | 2 | Primary extension target -- add Euclidean members and gating logic |
| ArpeggiatorParams | `plugins/ruinae/src/parameters/arpeggiator_params.h` | Plugin | Extended with 4 atomic Euclidean parameters |
| handleArpParamChange() | `plugins/ruinae/src/parameters/arpeggiator_params.h:100-258` | Plugin | Extended for Euclidean param dispatch |
| registerArpParams() | `plugins/ruinae/src/parameters/arpeggiator_params.h:266-446` | Plugin | Extended for Euclidean param registration |
| formatArpParam() | `plugins/ruinae/src/parameters/arpeggiator_params.h:453-638` | Plugin | Extended for Euclidean display formatting |
| saveArpParams() | `plugins/ruinae/src/parameters/arpeggiator_params.h:644-694` | Plugin | Extended to serialize Euclidean data |
| loadArpParams() | `plugins/ruinae/src/parameters/arpeggiator_params.h:702-810` | Plugin | Extended to deserialize Euclidean data |
| loadArpParamsToController() | `plugins/ruinae/src/parameters/arpeggiator_params.h:817-953` | Plugin | Extended to propagate Euclidean data to controller |
| TranceGate::setEuclidean() | `dsp/include/krate/dsp/processors/trance_gate.h` | 2 | Reference pattern for EuclideanPattern usage in this codebase |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/euclidean_pattern.h` - Layer 0 EuclideanPattern (reuse, no changes)
- [x] `dsp/include/krate/dsp/processors/arpeggiator_core.h` - Primary extension target
- [x] `dsp/include/krate/dsp/primitives/arp_lane.h` - ArpLane (not modified)
- [x] `specs/_architecture_/` - Component inventory
- [x] `plugins/ruinae/src/plugin_ids.h` - Current sentinel `kArpEndId = 3299`, `kNumParameters = 3300` (no change needed)
- [x] `plugins/ruinae/src/parameters/arpeggiator_params.h` - Current param struct and serialization
- [x] `plugins/ruinae/src/processor/processor.cpp` - Lane application pattern in `applyParamsToEngine()`

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: No new classes or structs are introduced. All changes are member additions to existing `ArpeggiatorCore` class and function extensions. All planned names (`euclideanEnabled_`, `euclideanPattern_`, `euclideanPosition_`, `euclideanHits_`, `euclideanSteps_`, `euclideanRotation_`, `regenerateEuclideanPattern`) are unique in the codebase. The spec's own ODR search confirms this.

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|------------|---------------|-------------------------------|-----------|
| EuclideanPattern | kMinSteps | `static constexpr int kMinSteps = 2` | Yes |
| EuclideanPattern | kMaxSteps | `static constexpr int kMaxSteps = 32` | Yes |
| EuclideanPattern | generate | `[[nodiscard]] static constexpr uint32_t generate(int pulses, int steps, int rotation = 0) noexcept` | Yes |
| EuclideanPattern | isHit | `[[nodiscard]] static constexpr bool isHit(uint32_t pattern, int position, int steps) noexcept` | Yes |
| EuclideanPattern | countHits | `[[nodiscard]] static constexpr int countHits(uint32_t pattern) noexcept` | Yes |
| ArpeggiatorCore | resetLanes | `void resetLanes() noexcept` | Yes |
| ArpeggiatorCore | reset | `inline void reset() noexcept` (calls resetLanes) | Yes |
| ArpeggiatorCore | setEnabled | `inline void setEnabled(bool enabled) noexcept` | Yes |
| ArpeggiatorCore | fireStep | `inline void fireStep(const BlockContext& ctx, int32_t sampleOffset, std::span<ArpEvent> outputEvents, size_t& eventCount, size_t maxEvents, size_t samplesProcessed, size_t blockSize) noexcept` | Yes |
| ArpeggiatorCore | cancelPendingNoteOffsForCurrentNotes | `inline void cancelPendingNoteOffsForCurrentNotes() noexcept` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/euclidean_pattern.h` - EuclideanPattern class (full body read, lines 52-150)
- [x] `dsp/include/krate/dsp/processors/arpeggiator_core.h` - ArpeggiatorCore class (full symbol overview + key method bodies read)
- [x] `plugins/ruinae/src/plugin_ids.h` - ParameterIDs enum (sentinel values verified: kArpEndId = 3299, kNumParameters = 3300)
- [x] `plugins/ruinae/src/parameters/arpeggiator_params.h` - All 6 functions read in full

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|-----------|--------|---------------|
| EuclideanPattern::generate | Parameter name is `pulses` not `hits` in the actual signature | `EuclideanPattern::generate(euclideanHits_, euclideanSteps_, euclideanRotation_)` |
| EuclideanPattern::isHit | `position` parameter is `int` not `size_t` -- need a cast from `euclideanPosition_` (size_t) | `isHit(pattern, static_cast<int>(euclideanPosition_), euclideanSteps_)` |
| EuclideanPattern::generate | Clamps steps to [kMinSteps, kMaxSteps] and pulses to [0, steps] internally | Setters still should clamp to prevent storing out-of-range values in members |
| loadArpParams EOF handling | First new field EOF = backward compat success (return true). Mid-field EOF = corrupt (return false). | Follow exact pattern of ratchet lane EOF handling |
| Setter call order | Steps must be set before hits so pattern regeneration clamps hits against correct step count | `setEuclideanSteps()` -> `setEuclideanHits()` -> `setEuclideanRotation()` -> `setEuclideanEnabled()` |
| arpeggiator_core.h includes | Does NOT currently include `euclidean_pattern.h` | Must add `#include <krate/dsp/core/euclidean_pattern.h>` |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|--------------------|--------------|-------------------|-----------|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|----------|-----------|
| `regenerateEuclideanPattern()` | Private helper that reads/writes member variables; only 1 consumer (ArpeggiatorCore) |
| `setEuclideanHits/Steps/Rotation/Enabled` | Setters that manage member state; standard class design |

**Decision**: No Layer 0 extraction needed. All new code is specific to ArpeggiatorCore and operates on member state. The `EuclideanPattern` class (Layer 0) already provides the algorithmic core -- the ArpeggiatorCore just orchestrates its use.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|----------|------------|-------|
| **Feedback loops** | NO | Euclidean gating is purely event-scheduling logic |
| **Data parallelism width** | 1 (sequential events) | Events must be emitted in strict temporal order |
| **Branch density in inner loop** | HIGH | Hit/rest check, modifier priority, ratchet interaction |
| **Dominant operations** | Integer bitwise (single bit check per step) | O(1) shift + mask via `isHit()` |
| **Current CPU budget vs expected usage** | < 0.01% expected | A single bit check per arp step adds essentially zero CPU |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: Euclidean timing adds a single O(1) bit-check (`isHit()`) per arp step, which fires at most ~50 times per second at maximum tempo. The total CPU contribution is negligible. The code path is inherently sequential (event ordering matters) and branch-heavy (modifier interaction). SIMD parallelization would be counterproductive.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|-------------|-----------------|------------|--------------|
| Pre-computed bitmask (already in design) | Avoids re-running Bjorklund algorithm per step | LOW | YES -- FR-008 requires this |
| Early-out when Euclidean disabled | Skips `isHit()` entirely in the common case | LOW | YES -- FR-002 requires this |

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 2 (Processors)

**Related features at same layer** (from roadmap):
- Phase 8: Conditional Trig System (filters which steps fire based on probability/count conditions)
- Phase 9: Spice/Dice + Humanize (randomizes lane values and timing)
- Phase 11: Arpeggiator UI (visual pattern display, knobs)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|-----------|-----------------|------------------|--------|
| Euclidean-before-modifier evaluation pattern | MEDIUM | Phase 8 Conditional Trig | Keep local; Phase 8 will add its own gating check in the same location |
| `euclideanPosition_` management pattern | MEDIUM | Phase 8 condition counter | Keep local; document the pattern for Phase 8 to follow |
| `regenerateEuclideanPattern()` | LOW | Only Euclidean uses this | Keep as private member function |

### Decision Log

| Decision | Rationale |
|----------|-----------|
| No shared pre-step-gating abstraction | Euclidean and Conditional Trig have different semantics (bitmask vs probability). Premature abstraction would be forced. |
| Euclidean position resets in resetLanes() | Consistent with all other lane positions; Phase 8 condition counters will follow the same pattern |
| No sentinel update needed | IDs 3230-3233 fall within existing 3223-3299 reserved range established in Phase 6 |

### Review Trigger

After implementing Phase 8 (Conditional Trig):
- [ ] Does Conditional Trig need a similar pre-fire gating check? If so, document where it slots in the evaluation chain
- [ ] Does Conditional Trig use a similar position counter? If so, consider whether resetLanes() patterns could be generalized
- [ ] Any duplicated step-filtering logic between Euclidean rest and Conditional Trig suppress?

## Project Structure

### Documentation (this feature)

```text
specs/075-euclidean-timing/
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
    - Add #include <krate/dsp/core/euclidean_pattern.h>
    - Add 6 Euclidean member variables (FR-001)
    - Add regenerateEuclideanPattern() private helper (FR-008)
    - Add setEuclideanHits/Steps/Rotation/Enabled setter methods (FR-009, FR-010)
    - Add euclideanEnabled/Hits/Steps/Rotation getter methods (FR-015)
    - Extend resetLanes(): euclideanPosition_ = 0 (FR-013)
    - Extend reset(): inherits from resetLanes() (FR-014)
    - Extend fireStep(): Euclidean gating check before modifier evaluation (FR-011)
    - Extend fireStep() defensive branch: advance euclideanPosition_ (FR-035)

# DSP Layer 2 tests
dsp/tests/unit/processors/arpeggiator_core_test.cpp
    - Euclidean gating: tresillo E(3,8) fires on correct steps (SC-001)
    - Euclidean gating: all-hits E(8,8) = every step fires (SC-001)
    - Euclidean gating: all-rests E(0,8) = no notes (SC-001)
    - Rotation produces distinct patterns (SC-002)
    - Polymetric cycling: Euclidean steps=5 + velocity lane=3 (SC-003)
    - Euclidean disabled = Phase 6 identical output (SC-004)
    - On/off transitions: no stuck notes (SC-005)
    - Tie chain broken by Euclidean rest (SC-006)
    - Ratchet + Euclidean interaction (SC-007)
    - Position reset on retrigger (SC-012)
    - Euclidean rest + modifier Rest/Tie/Slide/Accent interactions
    - Chord mode + Euclidean gating (FR-021)

# Plugin Integration
plugins/ruinae/src/plugin_ids.h
    - Add kArpEuclideanEnabledId = 3230
    - Add kArpEuclideanHitsId = 3231
    - Add kArpEuclideanStepsId = 3232
    - Add kArpEuclideanRotationId = 3233
    - (kArpEndId and kNumParameters remain at 3299/3300 -- no change needed)

plugins/ruinae/src/parameters/arpeggiator_params.h
    - Extend ArpeggiatorParams struct: euclideanEnabled, euclideanHits, euclideanSteps, euclideanRotation
    - Extend handleArpParamChange(): Euclidean parameter dispatch
    - Extend registerArpParams(): Euclidean parameter registration
    - Extend formatArpParam(): Euclidean display formatting
    - Extend saveArpParams(): Euclidean serialization after ratchet data
    - Extend loadArpParams(): Euclidean deserialization with EOF-safe backward compat
    - Extend loadArpParamsToController(): Euclidean controller state sync

plugins/ruinae/src/processor/processor.cpp
    - Extend applyParamsToEngine(): transfer Euclidean params to ArpeggiatorCore
      (setEuclideanSteps -> setEuclideanHits -> setEuclideanRotation -> setEuclideanEnabled)

# Plugin Integration tests
plugins/ruinae/tests/unit/processor/arp_integration_test.cpp
    - State save/load round-trip (SC-008)
    - Phase 6 preset backward compatibility (SC-009)

plugins/ruinae/tests/unit/parameters/arpeggiator_params_test.cpp
    - Parameter registration and formatting (SC-011)
```

**Structure Decision**: This feature extends existing files in the established monorepo structure. No new files are created except test cases added to existing test files. All DSP changes are in the header-only `arpeggiator_core.h`. All plugin integration changes follow the exact patterns established by Phase 4, 5, and 6.

## Complexity Tracking

No constitution violations. All design decisions align with established principles.

---

## Detailed Design Notes

### 1. ArpeggiatorCore Include Addition

Add to the includes at line 19 of `arpeggiator_core.h`:
```cpp
#include <krate/dsp/core/euclidean_pattern.h>
```
This is a Layer 2 -> Layer 0 dependency, which is valid per the layered architecture.

### 2. ArpeggiatorCore Member Additions

New Euclidean state members (new section after ratchet state, before timing/transport state):
```cpp
// Euclidean timing state (075-euclidean-timing)
bool euclideanEnabled_{false};        // Whether Euclidean gating is active
int euclideanHits_{4};                // Number of pulses (k), range [0, 32]
int euclideanSteps_{8};               // Number of steps (n), range [2, 32]
int euclideanRotation_{0};            // Rotation offset, range [0, 31]
size_t euclideanPosition_{0};         // Current position in Euclidean pattern
uint32_t euclideanPattern_{0};        // Pre-computed bitmask from generate()
```

### 3. regenerateEuclideanPattern() Helper

New private inline method:
```cpp
inline void regenerateEuclideanPattern() noexcept {
    euclideanPattern_ = EuclideanPattern::generate(
        euclideanHits_, euclideanSteps_, euclideanRotation_);
}
```

### 4. Setter Methods

```cpp
inline void setEuclideanSteps(int steps) noexcept {
    euclideanSteps_ = std::clamp(steps,
        EuclideanPattern::kMinSteps, EuclideanPattern::kMaxSteps);
    // Re-clamp hits against new step count
    euclideanHits_ = std::clamp(euclideanHits_, 0, euclideanSteps_);
    regenerateEuclideanPattern();
}

inline void setEuclideanHits(int hits) noexcept {
    euclideanHits_ = std::clamp(hits, 0, euclideanSteps_);
    regenerateEuclideanPattern();
}

inline void setEuclideanRotation(int rotation) noexcept {
    euclideanRotation_ = std::clamp(rotation, 0,
        EuclideanPattern::kMaxSteps - 1);
    regenerateEuclideanPattern();
}

inline void setEuclideanEnabled(bool enabled) noexcept {
    if (!euclideanEnabled_ && enabled) {
        // Transitioning from disabled to enabled: reset position (FR-010)
        euclideanPosition_ = 0;
        // Do NOT clear ratchet sub-step state -- in-flight sub-steps complete
    }
    euclideanEnabled_ = enabled;
}
```

### 5. Getter Methods (FR-015)

```cpp
[[nodiscard]] inline bool euclideanEnabled() const noexcept { return euclideanEnabled_; }
[[nodiscard]] inline int euclideanHits() const noexcept { return euclideanHits_; }
[[nodiscard]] inline int euclideanSteps() const noexcept { return euclideanSteps_; }
[[nodiscard]] inline int euclideanRotation() const noexcept { return euclideanRotation_; }
```

### 6. resetLanes() Extension

Add to `resetLanes()` after the ratchet reset lines:
```cpp
euclideanPosition_ = 0;    // 075-euclidean-timing: reset Euclidean position (FR-013)
```

### 7. reset() Extension

The `reset()` method already calls `resetLanes()` which handles `euclideanPosition_`. For FR-014, we also regenerate the pattern from current parameters:
```cpp
// Add after resetLanes() call in reset():
regenerateEuclideanPattern();  // 075-euclidean-timing: regenerate from current params (FR-014)
```

Note: This requires `regenerateEuclideanPattern()` to be defined before `reset()` in the header, OR moved into `resetLanes()` if the compiler encounters ordering issues. Since the header-only class has all methods inline, the order of definition within the class body does not matter for inline methods.

### 8. Constructor Extension

Add to the constructor after the ratchet lane initialization:
```cpp
// 075-euclidean-timing: initialize pattern from defaults (FR-001)
regenerateEuclideanPattern();
```

### 9. fireStep() Euclidean Gating Integration

The Euclidean gating check is inserted AFTER the `selector_.advance()` call and all lane advances, but BEFORE modifier evaluation. The key design principle is: all lanes advance unconditionally (including on Euclidean rest steps), matching existing modifier Rest behavior.

**Modified fireStep() flow (pseudocode):**

```
1. result = selector_.advance(heldNotes_)
2. if (result.count > 0):
   a. Advance ALL lanes: velocity, gate, pitch, modifier, ratchet
   b. *** NEW: Euclidean gating check ***
      - If euclideanEnabled_:
        i.  Read current position: isHitStep = EuclideanPattern::isHit(
              euclideanPattern_, static_cast<int>(euclideanPosition_), euclideanSteps_)
        ii. Advance: euclideanPosition_ = (euclideanPosition_ + 1) % static_cast<size_t>(euclideanSteps_)
        iii. If NOT isHitStep (Euclidean rest):
             - Cancel pending noteOffs for current notes
             - Emit noteOff for all currently sounding notes
             - Set currentArpNoteCount_ = 0
             - Set tieActive_ = false (FR-007: break tie chain)
             - Increment swingStepCounter_
             - Recalculate currentStepDuration_
             - return (early exit -- skip modifier evaluation and ratcheting)
   c. Modifier evaluation: Rest > Tie > Slide > Accent (existing code, unchanged)
   d. Ratcheting (existing code, unchanged)
3. else (result.count == 0, defensive branch):
   a. Advance modifier, ratchet lanes (existing)
   b. *** NEW: Advance euclideanPosition_ if enabled ***
      - If euclideanEnabled_:
        euclideanPosition_ = (euclideanPosition_ + 1) % static_cast<size_t>(euclideanSteps_)
   c. Existing defensive cleanup (noteOffs, etc.)
```

**Critical ordering detail**: The lane advances in step 2a MUST happen before the Euclidean check in step 2b. This is because the spec (FR-011) requires that NoteSelector and ALL lanes advance unconditionally on every step tick, including Euclidean rest steps. The lane values from velocity/gate/pitch are consumed (but discarded on rest steps).

**Implementation detail**: The Euclidean rest path closely mirrors the existing modifier Rest path (cancel pending noteOffs, emit noteOffs, clear currentArpNoteCount_, early return). The key difference is that Euclidean rest also sets `tieActive_ = false` (FR-007) to break any active tie chain.

### 10. Plugin Parameter IDs

```cpp
// Euclidean Timing (075-euclidean-timing)
kArpEuclideanEnabledId   = 3230,    // discrete: 0-1 (on/off toggle)
kArpEuclideanHitsId      = 3231,    // discrete: 0-32
kArpEuclideanStepsId     = 3232,    // discrete: 2-32
kArpEuclideanRotationId  = 3233,    // discrete: 0-31
// 3234-3299: reserved for future arp phases (Conditional Trig, Spice/Dice)
```

No sentinel change: `kArpEndId = 3299` and `kNumParameters = 3300` remain as set in Phase 6.

### 11. ArpeggiatorParams Struct Extension

Add after ratchet lane members:
```cpp
// --- Euclidean Timing (075-euclidean-timing) ---
std::atomic<bool> euclideanEnabled{false};    // default off
std::atomic<int>  euclideanHits{4};           // default 4
std::atomic<int>  euclideanSteps{8};          // default 8
std::atomic<int>  euclideanRotation{0};       // default 0
```

No constructor changes needed -- `std::atomic` value-initializes from the default member initializers.

### 12. handleArpParamChange() Extension

Add new cases in the switch statement:
```cpp
case kArpEuclideanEnabledId:
    params.euclideanEnabled.store(value >= 0.5, std::memory_order_relaxed);
    break;
case kArpEuclideanHitsId:
    // RangeParameter: 0-1 -> 0-32 (stepCount=32)
    params.euclideanHits.store(
        std::clamp(static_cast<int>(std::round(value * 32.0)), 0, 32),
        std::memory_order_relaxed);
    break;
case kArpEuclideanStepsId:
    // RangeParameter: 0-1 -> 2-32 (stepCount=30)
    params.euclideanSteps.store(
        std::clamp(static_cast<int>(2.0 + std::round(value * 30.0)), 2, 32),
        std::memory_order_relaxed);
    break;
case kArpEuclideanRotationId:
    // RangeParameter: 0-1 -> 0-31 (stepCount=31)
    params.euclideanRotation.store(
        std::clamp(static_cast<int>(std::round(value * 31.0)), 0, 31),
        std::memory_order_relaxed);
    break;
```

### 13. registerArpParams() Extension

Add after ratchet lane registration:
```cpp
// --- Euclidean Timing (075-euclidean-timing) ---

// Euclidean enabled: Toggle (0 or 1), default off
parameters.addParameter(STR16("Arp Euclidean"), STR16(""), 1, 0.0,
    ParameterInfo::kCanAutomate, kArpEuclideanEnabledId);

// Euclidean hits: RangeParameter 0-32, default 4, stepCount 32
parameters.addParameter(
    new RangeParameter(STR16("Arp Euclidean Hits"), kArpEuclideanHitsId,
                      STR16(""), 0, 32, 4, 32,
                      ParameterInfo::kCanAutomate));

// Euclidean steps: RangeParameter 2-32, default 8, stepCount 30
parameters.addParameter(
    new RangeParameter(STR16("Arp Euclidean Steps"), kArpEuclideanStepsId,
                      STR16(""), 2, 32, 8, 30,
                      ParameterInfo::kCanAutomate));

// Euclidean rotation: RangeParameter 0-31, default 0, stepCount 31
parameters.addParameter(
    new RangeParameter(STR16("Arp Euclidean Rotation"), kArpEuclideanRotationId,
                      STR16(""), 0, 31, 0, 31,
                      ParameterInfo::kCanAutomate));
```

### 14. formatArpParam() Extension

Add new cases in the switch statement:
```cpp
case kArpEuclideanEnabledId: {
    UString(string, 128).fromAscii(value >= 0.5 ? "On" : "Off");
    return kResultOk;
}
case kArpEuclideanHitsId: {
    char8 text[32];
    int hits = std::clamp(static_cast<int>(std::round(value * 32.0)), 0, 32);
    snprintf(text, sizeof(text), "%d hits", hits);
    UString(string, 128).fromAscii(text);
    return kResultOk;
}
case kArpEuclideanStepsId: {
    char8 text[32];
    int steps = std::clamp(static_cast<int>(2.0 + std::round(value * 30.0)), 2, 32);
    snprintf(text, sizeof(text), "%d steps", steps);
    UString(string, 128).fromAscii(text);
    return kResultOk;
}
case kArpEuclideanRotationId: {
    char8 text[32];
    int rot = std::clamp(static_cast<int>(std::round(value * 31.0)), 0, 31);
    snprintf(text, sizeof(text), "%d", rot);
    UString(string, 128).fromAscii(text);
    return kResultOk;
}
```

### 15. Serialization Order

After existing ratchet lane data (ratchetLaneLength + 32 ratchetLaneSteps), append:
```
euclideanEnabled (int32: 0 or 1)
euclideanHits (int32: 0-32)
euclideanSteps (int32: 2-32)
euclideanRotation (int32: 0-31)
```

### 16. saveArpParams() Extension

Add after ratchet lane serialization:
```cpp
// --- Euclidean Timing (075-euclidean-timing) ---
streamer.writeInt32(params.euclideanEnabled.load(std::memory_order_relaxed) ? 1 : 0);
streamer.writeInt32(params.euclideanHits.load(std::memory_order_relaxed));
streamer.writeInt32(params.euclideanSteps.load(std::memory_order_relaxed));
streamer.writeInt32(params.euclideanRotation.load(std::memory_order_relaxed));
```

### 17. loadArpParams() Extension

Add after ratchet lane deserialization (after the `return true` at end of ratchet section):

Replace the final `return true;` after ratchet with:
```cpp
// --- Euclidean Timing (075-euclidean-timing) ---
// EOF-safe: if Euclidean data is missing entirely (Phase 6 preset), keep defaults.
if (!streamer.readInt32(intVal)) return true;  // EOF at first Euclidean field = Phase 6 compat
params.euclideanEnabled.store(intVal != 0, std::memory_order_relaxed);

// From here, EOF signals a corrupt stream (enabled was present but remaining fields are not)
if (!streamer.readInt32(intVal)) return false;
params.euclideanHits.store(std::clamp(intVal, 0, 32), std::memory_order_relaxed);

if (!streamer.readInt32(intVal)) return false;
params.euclideanSteps.store(std::clamp(intVal, 2, 32), std::memory_order_relaxed);

if (!streamer.readInt32(intVal)) return false;
params.euclideanRotation.store(std::clamp(intVal, 0, 31), std::memory_order_relaxed);

return true;
```

### 18. loadArpParamsToController() Extension

Add after ratchet lane controller sync:
```cpp
// --- Euclidean Timing (075-euclidean-timing) ---
// EOF-safe: if Euclidean data is missing (Phase 6 preset), keep controller defaults
if (!streamer.readInt32(intVal)) return;
setParam(kArpEuclideanEnabledId, intVal != 0 ? 1.0 : 0.0);

if (!streamer.readInt32(intVal)) return;
setParam(kArpEuclideanHitsId,
    static_cast<double>(std::clamp(intVal, 0, 32)) / 32.0);

if (!streamer.readInt32(intVal)) return;
setParam(kArpEuclideanStepsId,
    static_cast<double>(std::clamp(intVal, 2, 32) - 2) / 30.0);

if (!streamer.readInt32(intVal)) return;
setParam(kArpEuclideanRotationId,
    static_cast<double>(std::clamp(intVal, 0, 31)) / 31.0);
```

### 19. processor.cpp applyParamsToEngine() Extension

Add after ratchet lane application (expand-write-shrink pattern is NOT needed for Euclidean -- these are scalar values, not lane arrays):
```cpp
// --- Euclidean Timing (075-euclidean-timing) ---
// Prescribed call order: steps -> hits -> rotation -> enabled (FR-032)
arpCore_.setEuclideanSteps(
    arpParams_.euclideanSteps.load(std::memory_order_relaxed));
arpCore_.setEuclideanHits(
    arpParams_.euclideanHits.load(std::memory_order_relaxed));
arpCore_.setEuclideanRotation(
    arpParams_.euclideanRotation.load(std::memory_order_relaxed));
arpCore_.setEuclideanEnabled(
    arpParams_.euclideanEnabled.load(std::memory_order_relaxed));
```
