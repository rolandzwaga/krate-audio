# Implementation Plan: Spice/Dice & Humanize

**Branch**: `077-spice-dice-humanize` | **Date**: 2026-02-23 | **Spec**: `specs/077-spice-dice-humanize/spec.md`
**Input**: Feature specification from `specs/077-spice-dice-humanize/spec.md`

## Summary

Add controlled randomization (Spice/Dice) and timing humanization to the ArpeggiatorCore (DSP Layer 2), enabling organic, evolving patterns. The Spice/Dice system maintains four variation overlay arrays (velocity, gate, ratchet, condition) that blend with original lane values via a user-controlled Spice knob (0-100%). The Dice trigger generates new random overlays using a dedicated `Xorshift32` PRNG (seed 31337). The Humanize system adds per-step random offsets to timing (+/-20ms), velocity (+/-15), and gate (+/-10%) using a separate `Xorshift32` PRNG (seed 48271). Both systems compose additively: Spice modifies lane-read values, then Humanize adds micro-level offsets. Three new VST3 parameters (IDs 3290-3292) are exposed through the Ruinae plugin within the existing reserved range (no sentinel adjustment needed). The `Xorshift32::nextFloat()` method already exists in `random.h` (returning bipolar [-1.0, 1.0]); no changes to Layer 0 are required.

## Technical Context

**Language/Version**: C++20
**Primary Dependencies**: KrateDSP (Layer 0 Xorshift32 with nextFloat/nextUnipolar, Layer 2 ArpeggiatorCore, Layer 1 ArpLane), Steinberg VST3 SDK
**Storage**: VST3 plugin state serialization via IBStreamer (binary read/write)
**Testing**: Catch2 (`dsp_tests` target for DSP unit tests, `ruinae_tests` for plugin integration)
**Target Platform**: Windows (MSVC), macOS (Clang), Linux (GCC)
**Project Type**: Monorepo with shared DSP library + plugin integration
**Performance Goals**: Zero heap allocation in audio path; Spice blend is O(1) per step (4 lerp operations); Humanize is O(1) per step (3 PRNG calls + 3 multiplies); triggerDice() is O(1) (128 PRNG calls + array writes)
**Constraints**: Header-only DSP implementation (arpeggiator_core.h); all overlay and humanize state uses fixed-size member variables; PRNG consumption must be deterministic
**Scale/Scope**: 41 FRs, 15 SCs; touches 1 file in DSP (arpeggiator_core.h), 3 files in plugin integration (plugin_ids.h, arpeggiator_params.h, processor.cpp)

## Constitution Check

*GATE: Must pass before Phase 0 research. Re-check after Phase 1 design.*

**Principle I (VST3 Architecture Separation):**
- [x] Spice/Dice/Humanize parameter IDs defined in `plugin_ids.h` (shared boundary)
- [x] Atomic storage in `ArpeggiatorParams` (processor side)
- [x] Registration in `registerArpParams()` (controller side)
- [x] No cross-inclusion between processor and controller
- [x] Dice trigger consumed via `compare_exchange_strong` (safe cross-thread pattern)

**Principle II (Real-Time Audio Thread Safety):**
- [x] All overlay state uses fixed-size `std::array` members (32 entries each)
- [x] Zero allocation in `fireStep()`, `triggerDice()`, and `processBlock()`
- [x] `Xorshift32::nextFloat()` and `nextUnipolar()` are constexpr, noexcept -- no allocation
- [x] Spice blend is arithmetic only: lerp = `a + (b - a) * t`
- [x] Humanize offsets are arithmetic only: multiply + clamp
- [x] `triggerDice()` performs only PRNG calls and array writes (FR-006)
- [x] No locks, exceptions, or I/O in audio path

**Principle III (Modern C++ Standards):**
- [x] Uses `std::array<float, 32>`, `std::array<uint8_t, 32>`, `std::clamp`, `std::round`
- [x] No raw new/delete
- [x] Member initialization with default values

**Principle IV (SIMD & DSP Optimization):**
- [x] SIMD analysis completed (NOT BENEFICIAL -- see section below)
- [x] Scalar-first workflow applies (no SIMD phase needed)

**Principle VIII (Testing Discipline):**
- [x] Skills auto-load (testing-guide, vst-guide) - no manual context verification needed
- [x] Tests will be written BEFORE implementation code
- [x] Each task group will end with a commit step

**Principle IX (Layered DSP Architecture):**
- [x] Xorshift32 is Layer 0 (core) -- already included in arpeggiator_core.h via `<krate/dsp/core/random.h>`
- [x] ArpeggiatorCore is Layer 2 (processors) -- depends on Layer 0 and Layer 1, allowed
- [x] No new includes needed -- `random.h` already present from Phase 8

**Principle XIV (ODR Prevention):**
- [x] Codebase Research section below is complete
- [x] No duplicate classes/functions will be created

**Principle XV (Pre-Implementation Research):**
- [x] Searched for all planned names -- no conflicts found (confirmed in spec section "Initial codebase search for key terms")

**Principle XVI (Honest Completion):**
- [x] Compliance table will be filled with specific file paths, line numbers, test names, and measured values

## Codebase Research (Principle XIV - ODR Prevention)

*GATE: Must complete BEFORE creating any new classes, structs, or functions.*

### Mandatory Searches Performed

**Classes/Structs to be created**: None (all changes are member additions to existing ArpeggiatorCore)

**New members/methods to be added to ArpeggiatorCore**:

| Planned Name | Search Result | Existing? | Action |
|---|---|---|---|
| `velocityOverlay_` | Not found in codebase | No | Create as new `std::array<float, 32>` member |
| `gateOverlay_` | Not found | No | Create as new `std::array<float, 32>` member |
| `ratchetOverlay_` | Not found | No | Create as new `std::array<uint8_t, 32>` member |
| `conditionOverlay_` | Not found | No | Create as new `std::array<uint8_t, 32>` member |
| `spice_` | Not found in ArpeggiatorCore | No | Create as new float member |
| `humanize_` | Not found in ArpeggiatorCore | No | Create as new float member |
| `spiceDiceRng_` | Not found | No | Create as new Xorshift32 member (seed 31337) |
| `humanizeRng_` | Not found | No | Create as new Xorshift32 member (seed 48271) |
| `setSpice` | Not found | No | Create as public setter |
| `spice` | Not found (getter) | No | Create as public const getter |
| `setHumanize` | Not found | No | Create as public setter |
| `humanize` | Not found (getter) | No | Create as public const getter |
| `triggerDice` | Not found | No | Create as public method |

### Existing Components to Reuse

| Component | Location | Layer | How It Will Be Used |
|---|---|---|---|
| Xorshift32 | `dsp/include/krate/dsp/core/random.h` | 0 | Two new instances: `spiceDiceRng_{31337}` for overlay gen, `humanizeRng_{48271}` for per-step offsets |
| Xorshift32::nextFloat() | `random.h:57-61` | 0 | Bipolar [-1, 1] for humanize offsets (FR-014) |
| Xorshift32::nextUnipolar() | `random.h:65-67` | 0 | Unipolar [0, 1] for Dice overlay velocity/gate (FR-005) |
| Xorshift32::next() | `random.h:48-53` | 0 | Raw uint32_t for Dice overlay ratchet/condition (FR-005) |
| ArpLane<T>::currentStep() | `arp_lane.h:110` | 1 | Read overlay index BEFORE advance for Spice blend (FR-010) |
| ArpeggiatorCore::fireStep() | `arpeggiator_core.h:1105-1534` | 2 | Primary modification target for Spice blend + Humanize |
| ArpeggiatorCore::resetLanes() | `arpeggiator_core.h:1612-1626` | 2 | Extended to NOT reset overlays, Spice, Humanize, PRNGs |
| ArpeggiatorCore::calculateGateDuration() | `arpeggiator_core.h:899-904` | 2 | Called with Spice-blended gate value |
| ArpeggiatorCore::evaluateCondition() | `arpeggiator_core.h:1060-1102` | 2 | Called with Spice-blended condition value |
| ArpeggiatorParams | `plugins/ruinae/src/parameters/arpeggiator_params.h:37-104` | Plugin | Extended with 3 new atomics |
| handleArpParamChange() | `arpeggiator_params.h:112-311` | Plugin | Extended for Spice/Dice/Humanize dispatch |
| registerArpParams() | `arpeggiator_params.h:319-548` | Plugin | Extended for 3 new parameter registrations |
| formatArpParam() | `arpeggiator_params.h:555-793` | Plugin | Extended for Spice/Dice/Humanize display |
| saveArpParams() | `arpeggiator_params.h:799-862` | Plugin | Extended to serialize Spice + Humanize |
| loadArpParams() | `arpeggiator_params.h:870-1009` | Plugin | Extended with EOF-safe backward compat |
| loadArpParamsToController() | `arpeggiator_params.h:1016-1185` | Plugin | Extended to propagate Spice + Humanize |
| applyParamsToEngine() | `processor.cpp:1209-1358` | Plugin | Extended with Spice/Dice/Humanize transfer |

### Files Checked for Conflicts

- [x] `dsp/include/krate/dsp/core/random.h` - Layer 0 Xorshift32 (reuse, **no changes needed** -- `nextFloat()` already exists at line 57-61)
- [x] `dsp/include/krate/dsp/primitives/arp_lane.h` - ArpLane (reuse, no changes)
- [x] `dsp/include/krate/dsp/processors/arpeggiator_core.h` - Primary extension target
- [x] `specs/_architecture_/` - Component inventory
- [x] `plugins/ruinae/src/plugin_ids.h` - Sentinel values: kArpEndId = 3299, kNumParameters = 3300 (no change needed; IDs 3290-3292 within reserved range)
- [x] `plugins/ruinae/src/parameters/arpeggiator_params.h` - Current param struct and serialization
- [x] `plugins/ruinae/src/processor/processor.cpp` - applyParamsToEngine() arp section at lines 1209-1358

### ODR Risk Assessment

**Risk Level**: Low

**Justification**: No new types are introduced. All changes are member additions to existing `ArpeggiatorCore` class and function extensions in existing plugin files. All proposed names (`velocityOverlay_`, `gateOverlay_`, `ratchetOverlay_`, `conditionOverlay_`, `spice_`, `humanize_`, `spiceDiceRng_`, `humanizeRng_`, `triggerDice`, `setSpice`, `setHumanize`) are confirmed unique via the spec's own ODR search (spec.md lines 361-369).

## Dependency API Contracts (Principle XIV Extension)

*GATE: Must complete BEFORE implementation begins.*

### API Signatures to Call

| Dependency | Method/Member | Exact Signature (from header) | Verified? |
|---|---|---|---|
| Xorshift32 | constructor | `explicit constexpr Xorshift32(uint32_t seedValue = 1) noexcept` | Yes |
| Xorshift32 | nextFloat | `[[nodiscard]] constexpr float nextFloat() noexcept` -- returns `static_cast<float>(next()) * kToFloat * 2.0f - 1.0f` ([-1, 1]) | Yes |
| Xorshift32 | nextUnipolar | `[[nodiscard]] constexpr float nextUnipolar() noexcept` -- returns `static_cast<float>(next()) * kToFloat` ([0, 1]) | Yes |
| Xorshift32 | next | `[[nodiscard]] constexpr uint32_t next() noexcept` -- returns raw uint32_t | Yes |
| ArpLane<T> | currentStep | `[[nodiscard]] size_t currentStep() const noexcept` -- returns `position_` | Yes |
| ArpLane<T> | advance | `T advance() noexcept` -- returns value at position_, THEN increments position_ | Yes |
| ArpLane<T> | length | `[[nodiscard]] size_t length() const noexcept` -- returns `length_` | Yes |
| ArpeggiatorCore | resetLanes | `void resetLanes() noexcept` | Yes |
| ArpeggiatorCore | reset | `inline void reset() noexcept` -- calls resetLanes() internally | Yes |
| ArpeggiatorCore | calculateGateDuration | `[[nodiscard]] inline size_t calculateGateDuration(float gateScale) const noexcept` | Yes |
| ArpeggiatorCore | evaluateCondition | `[[nodiscard]] inline bool evaluateCondition(uint8_t condValue) noexcept` | Yes |
| ArpeggiatorCore | fireStep | `inline void fireStep(const BlockContext& ctx, int32_t sampleOffset, std::span<ArpEvent> outputEvents, size_t& eventCount, size_t maxEvents, size_t samplesProcessed, size_t blockSize) noexcept` | Yes |
| std::atomic<bool> | compare_exchange_strong | `bool compare_exchange_strong(bool& expected, bool desired, ...)` | Yes |

### Header Files Read

- [x] `dsp/include/krate/dsp/core/random.h` - Xorshift32 class (full body, lines 39-90). **CRITICAL FINDING**: `nextFloat()` already exists (line 57-61), returning bipolar [-1, 1]. No Layer 0 changes needed. FR-014's request to add `nextFloat()` is already satisfied.
- [x] `dsp/include/krate/dsp/primitives/arp_lane.h` - ArpLane template (lines 51-116). `advance()` returns current value then increments position. `currentStep()` returns current position after advance.
- [x] `dsp/include/krate/dsp/processors/arpeggiator_core.h` - ArpeggiatorCore class (lines 138-1734). Constructor at 158-182, reset at 198-213, resetLanes at 1612-1626, fireStep at 1105-1534, evaluateCondition at 1060-1102, calculateGateDuration at 899-904.
- [x] `plugins/ruinae/src/plugin_ids.h` - kArpEndId = 3299, kNumParameters = 3300 (lines 1047, 1050). IDs 3290-3292 fit within reserved range.
- [x] `plugins/ruinae/src/parameters/arpeggiator_params.h` - All 6 functions read. ArpeggiatorParams struct at 37-104, constructor at 86-103.
- [x] `plugins/ruinae/src/processor/processor.cpp` - applyParamsToEngine() arp section at 1209-1358. Existing pattern for Dice trigger: see Phase 8 `fillToggle` for atomic bool pattern.

### Common Gotchas Documented

| Component | Gotcha | Correct Usage |
|---|---|---|
| Xorshift32::nextFloat() | Already exists -- spec says "add" but it is present | Do NOT modify random.h. Use existing `nextFloat()` for humanize offsets |
| ArpLane::advance() | Returns current value THEN advances position | Capture overlay index BEFORE advance: `size_t velStep = velocityLane_.currentStep()` then call `advance()`. The captured value is the step that produced the returned lane value. |
| ArpLane::currentStep() | Returns `position_` (the next step to fire). Before `advance()` this is the current step; after `advance()` completes, `position_` has been incremented so it points to the NEXT step. | Always capture `currentStep()` BEFORE calling `advance()` for overlay indexing. Using `currentStep()` after `advance()` returns the wrong (next) step index and requires fragile `(pos - 1 + length) % length` arithmetic. |
| std::round() in ratchet blend | Well-defined in C++: half away from zero | Use `std::round()` not `static_cast<int>()` for ratchet Spice blend (FR-008) |
| loadArpParams EOF handling | First new field EOF = backward compat success (return true) | Spice/Humanize fields come AFTER fill toggle. If EOF at first Spice read, return true. |
| Dice trigger edge detection | Must use `compare_exchange_strong` for exactly-once | `bool expected = true; if (diceTrigger.compare_exchange_strong(expected, false)) { ... }` |
| handleArpParamChange Dice trigger | Discrete 2-step parameter: 0 = idle, 1 = trigger | On value >= 0.5 (i.e., normalized == 1.0), set `diceTrigger` to true. Only rising edge matters. |
| Overlay NOT reset in resetLanes() | Overlay is generative state, not playback state | Do NOT clear overlay arrays in resetLanes(). Also do NOT reset spice_, humanize_, spiceDiceRng_, humanizeRng_ |
| Humanize PRNG consumed on skipped steps | Maintains deterministic sequence position | Always consume 3 nextFloat() calls per step, even if step is Euclidean rest, condition fail, or modifier Rest |
| Humanize timing applied to first sub-step only for ratcheted steps | Preserves ratchet rhythm precision | Timing offset modifies the initial sampleOffset; ratchet sub-steps use relative timing from there |

## Layer 0 Candidate Analysis

### Utilities to Extract to Layer 0

| Candidate Function | Why Extract? | Proposed Location | Consumers |
|---|---|---|---|
| -- | -- | -- | -- |

### Utilities to Keep as Member Functions

| Function | Why Keep? |
|---|---|
| `triggerDice()` | Writes to member overlay arrays using member PRNG; only 1 consumer |
| `setSpice()` / `spice()` | Standard getter/setter for member state |
| `setHumanize()` / `humanize()` | Standard getter/setter for member state |

**Decision**: No Layer 0 extraction needed. The `Xorshift32` PRNG (Layer 0) already provides the randomness primitive with `nextFloat()`, `nextUnipolar()`, and `next()`. The ArpeggiatorCore orchestrates their use for overlay generation and humanize offsets. All operations are inline arithmetic on member state.

## SIMD Optimization Analysis

### Algorithm Characteristics

| Property | Assessment | Notes |
|---|---|---|
| **Feedback loops** | NO | Spice blend and humanize are pure forward transforms |
| **Data parallelism width** | 1 (sequential events) | Events must be emitted in strict temporal order |
| **Branch density in inner loop** | MEDIUM | Spice blend is 4 lerps (branchless), humanize is 3 multiply+clamp (branchless), but integrated into branch-heavy fireStep() |
| **Dominant operations** | Float multiply, PRNG calls, integer clamp | O(1) per step |
| **Current CPU budget vs expected usage** | < 0.01% expected | 4 lerps + 3 PRNG calls per arp step adds essentially zero CPU |

### SIMD Viability Verdict

**Verdict**: NOT BENEFICIAL

**Reasoning**: Spice/Dice adds 4 lerp operations per arp step and 128 PRNG calls per Dice trigger. Humanize adds 3 PRNG calls + 3 multiplies per step. Arp steps fire at most ~50 times per second at maximum tempo. The total CPU contribution is negligible. The code is inherently sequential (event ordering matters), and the data width (1 step at a time) provides no SIMD parallelism.

### Alternative Optimizations

| Optimization | Expected Impact | Complexity | Recommended? |
|---|---|---|---|
| Early-out for Spice == 0.0f | Skips all lerp/blend operations | LOW | YES -- check `spice_ > 0.0f` before blend |
| Early-out for Humanize == 0.0f note | PRNG still consumed (FR-018), but multiply by 0 is zero | LOW | NO -- PRNG must be consumed regardless; multiply by 0.0 is trivially fast |
| triggerDice() unrolling | 128 PRNG calls in tight loop; compiler will optimize | LOW | NO -- already optimal; fires rarely (user button press) |

## Higher-Layer Reusability Analysis

**This feature's layer**: Layer 2 (Processors)

**Related features at same layer** (from roadmap):
- Phase 10: Modulation Integration (Spice as modulation destination: `kModDestArpSpice`)
- Phase 11: Arpeggiator UI (Spice knob, Dice button, Humanize knob)
- Phase 12: Presets & Polish (Factory arp presets with generative features)

### Reusability Candidates

| Component | Reuse Potential | Future Consumers | Action |
|---|---|---|---|
| `setSpice()` setter API | HIGH | Phase 10 (mod destination) | Keep as-is; takes float 0-1, perfect for modulation |
| `triggerDice()` mechanism | LOW | Only arp uses Dice | Keep as ArpeggiatorCore method |
| Humanize offset pattern | MEDIUM | Could apply to future sequencer | Keep inline for now; extract if 2nd consumer appears |
| Overlay architecture (4 parallel arrays + blend) | MEDIUM | Could extend to pitch lane overlay | Keep current; expandable without API change |

### Decision Log

| Decision | Rationale |
|---|---|
| No `StepHumanizer` utility class | Only one consumer (ArpeggiatorCore); premature abstraction |
| Overlay arrays as `std::array` members, not allocated | Real-time safety; 4 arrays x 32 entries = 320 bytes total |
| Spice/Humanize as simple float members | Direct parameter mapping; no smoothing needed (step-level, not sample-level) |
| nextFloat() already exists in random.h | Spec FR-014 says "add nextFloat()" but it is already present; no Layer 0 change |

### Review Trigger

After implementing Phase 10 (Modulation Integration):
- [ ] Is `setSpice()` compatible with mod engine value range?
- [ ] Does Humanize need modulation destination? (knob value could be modulated)
- [ ] Any shared "per-step random offset" pattern between Humanize and future features?

## Project Structure

### Documentation (this feature)

```text
specs/077-spice-dice-humanize/
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
    - Add overlay arrays: velocityOverlay_, gateOverlay_, ratchetOverlay_, conditionOverlay_ (FR-001)
    - Add spice_ (float), humanize_ (float) members (FR-003, FR-011)
    - Add spiceDiceRng_ (Xorshift32, seed 31337) member (FR-005)
    - Add humanizeRng_ (Xorshift32, seed 48271) member (FR-013)
    - Add setSpice() / spice() accessors (FR-003, FR-004)
    - Add setHumanize() / humanize() accessors (FR-011, FR-012)
    - Add triggerDice() method (FR-005, FR-006, FR-007)
    - Extend constructor: overlay identity initialization (FR-002)
    - Extend resetLanes(): explicit NOT reset of overlays, Spice, Humanize, PRNGs (FR-025-029)
    - Extend fireStep(): Spice blend after lane advances (FR-008, FR-009, FR-010)
    - Extend fireStep(): Humanize offsets before note emission (FR-014-021)
    - Extend fireStep(): PRNG consumption on skipped steps (FR-023, FR-024)
    - Extend fireStep() defensive branch: humanizeRng_ consumed 3 times (FR-041)

# DSP Layer 2 tests
dsp/tests/unit/processors/arpeggiator_core_test.cpp
    - Spice 0% = Phase 8 identical output (SC-001)
    - Spice 100% = overlay values exclusively (SC-002)
    - Spice 50% = correct interpolation per lane (SC-003)
    - Dice generates different overlays on each trigger (SC-004)
    - Humanize 0% = Spice-only behavior (SC-005)
    - Humanize 100% timing distribution +/-882 samples (SC-006)
    - Humanize 100% velocity distribution +/-15 (SC-007)
    - Humanize 100% gate distribution +/-10% (SC-008)
    - Humanize 50% = half ranges (SC-009)
    - Spice + Humanize compose correctly (SC-015)
    - Humanize PRNG consumed on skipped steps (FR-023)
    - Humanize not applied to tie-sustained steps (FR-024)
    - Ratcheted step humanize interaction (FR-019, FR-020, FR-021)
    - Overlay preserved across reset/resetLanes (FR-025-029)
    - Defensive branch humanize PRNG consumption (FR-041)
    - PRNG distinctness: seeds 48271, 31337, 7919, 42 (SC-014)
    - Zero heap allocation code inspection (SC-012)

# Plugin Integration
plugins/ruinae/src/plugin_ids.h
    - Add kArpSpiceId = 3290
    - Add kArpDiceTriggerId = 3291
    - Add kArpHumanizeId = 3292
    - (kArpEndId = 3299 and kNumParameters = 3300 remain unchanged)

plugins/ruinae/src/parameters/arpeggiator_params.h
    - Extend ArpeggiatorParams struct: spice, diceTrigger, humanize atomics (FR-034)
    - Extend handleArpParamChange(): Spice/Dice/Humanize dispatch (FR-035)
    - Extend registerArpParams(): 3 new parameter registrations (FR-031, FR-032)
    - Extend formatArpParam(): Spice/Dice/Humanize display (FR-039)
    - Extend saveArpParams(): Spice + Humanize serialization after fill toggle (FR-037)
    - Extend loadArpParams(): Spice + Humanize with EOF-safe backward compat (FR-037, FR-038)
    - Extend loadArpParamsToController(): Spice + Humanize controller sync (FR-040)

plugins/ruinae/src/processor/processor.cpp
    - Extend applyParamsToEngine(): setSpice(), Dice compare_exchange_strong, setHumanize() (FR-036)

# Plugin Integration tests
plugins/ruinae/tests/unit/processor/arp_integration_test.cpp
    - State save/load round-trip (SC-010)
    - Phase 8 preset backward compatibility (SC-011)
    - Parameter registration and formatting (SC-013)
```

**Structure Decision**: This feature extends existing files in the established monorepo structure. No new files are created except test cases added to existing test files. All DSP changes are in the header-only `arpeggiator_core.h`. All plugin integration changes follow the exact patterns established by Phases 4-8.

## Complexity Tracking

No constitution violations. All design decisions align with established principles.

---

## Detailed Design Notes

### 1. Xorshift32::nextFloat() -- Already Exists (FR-014 Pre-Satisfied)

The spec's FR-014 says "This method MUST be added to Xorshift32 in `random.h`." However, `nextFloat()` already exists at `random.h:57-61`:

```cpp
[[nodiscard]] constexpr float nextFloat() noexcept {
    return static_cast<float>(next()) * kToFloat * 2.0f - 1.0f;
}
```

This returns bipolar [-1.0, 1.0] using `kToFloat = 1.0 / 4294967295.0`, which matches the spec's requirement exactly. **No changes to Layer 0 are needed.** The implementation agent should verify this before proceeding and note FR-014 as pre-satisfied.

### 2. Overlay Array Members (FR-001, FR-002)

Add after the Condition State section (after `conditionRng_`):

```cpp
// =========================================================================
// Spice/Dice State (077-spice-dice-humanize)
// =========================================================================

/// Variation overlay arrays generated by triggerDice().
/// Indexed by each lane's own step position (polymetric-aware).
std::array<float, 32> velocityOverlay_{};    ///< [0.0, 1.0] velocity scaling
std::array<float, 32> gateOverlay_{};        ///< [0.0, 1.0] gate scaling
std::array<uint8_t, 32> ratchetOverlay_{};   ///< [1, 4] ratchet count
std::array<uint8_t, 32> conditionOverlay_{}; ///< [0, 17] TrigCondition value

float spice_{0.0f};                          ///< Blend amount [0, 1]
float humanize_{0.0f};                       ///< Humanize amount [0, 1]
Xorshift32 spiceDiceRng_{31337};             ///< PRNG for overlay generation
Xorshift32 humanizeRng_{48271};              ///< PRNG for per-step offsets
```

### 3. Overlay Identity Initialization (FR-002)

In the constructor, after the Phase 8 condition lane init:

```cpp
// 077-spice-dice-humanize: initialize overlay arrays to identity (FR-002)
// velocity = 1.0 (full passthrough), gate = 1.0 (full passthrough),
// ratchet = 1 (no subdivision), condition = 0 (Always)
velocityOverlay_.fill(1.0f);
gateOverlay_.fill(1.0f);
ratchetOverlay_.fill(1);
conditionOverlay_.fill(static_cast<uint8_t>(TrigCondition::Always));
```

### 4. Public Accessors (FR-003, FR-004, FR-011, FR-012)

Add new section after condition lane accessors:

```cpp
// =====================================================================
// Spice/Dice & Humanize (077-spice-dice-humanize)
// =====================================================================

/// Set Spice blend amount (0.0 = original, 1.0 = full overlay).
void setSpice(float value) noexcept {
    spice_ = std::clamp(value, 0.0f, 1.0f);
}

/// Get current Spice blend amount.
[[nodiscard]] float spice() const noexcept { return spice_; }

/// Set Humanize amount (0.0 = quantized, 1.0 = max variation).
void setHumanize(float value) noexcept {
    humanize_ = std::clamp(value, 0.0f, 1.0f);
}

/// Get current Humanize amount.
[[nodiscard]] float humanize() const noexcept { return humanize_; }

/// Generate new random overlay values for all four lanes (FR-005).
/// Real-time safe: no allocation, no exceptions, no I/O.
void triggerDice() noexcept {
    // Velocity: 32 unipolar floats in [0.0, 1.0]
    for (auto& v : velocityOverlay_) {
        v = spiceDiceRng_.nextUnipolar();
    }
    // Gate: 32 unipolar floats in [0.0, 1.0]
    for (auto& g : gateOverlay_) {
        g = spiceDiceRng_.nextUnipolar();
    }
    // Ratchet: 32 values in [1, 4]
    for (auto& r : ratchetOverlay_) {
        r = static_cast<uint8_t>(spiceDiceRng_.next() % 4 + 1);
    }
    // Condition: 32 values in [0, 17]
    for (auto& c : conditionOverlay_) {
        c = static_cast<uint8_t>(
            spiceDiceRng_.next() % static_cast<uint32_t>(TrigCondition::kCount));
    }
}
```

### 5. resetLanes() Extension (FR-025, FR-026, FR-027, FR-028, FR-029)

Add a comment block at the end of `resetLanes()` documenting that overlays and PRNGs are intentionally NOT reset:

```cpp
// 077-spice-dice-humanize: overlays/Spice/Humanize intentionally NOT reset (FR-025-029)
// velocityOverlay_, gateOverlay_, ratchetOverlay_, conditionOverlay_ preserved
// spice_, humanize_ preserved (user-controlled parameters)
// spiceDiceRng_, humanizeRng_ preserved (continuous randomness, like conditionRng_)
```

### 6. fireStep() Modification -- Spice Blend (FR-008, FR-009, FR-010)

The key design challenge is capturing overlay indices BEFORE lane advances. The current code advances lanes like:

```cpp
float velScale = velocityLane_.advance();
float gateScale = gateLane_.advance();
```

`advance()` returns value at `position_` then increments `position_`. So the step index that produced `velScale` is `velocityLane_.currentStep()` MINUS 1 (or equivalently, the value of `currentStep()` before the advance call). We need to capture the step index before advancing.

**Implementation approach**: Capture step positions BEFORE lane advances, then use them for overlay indexing.

```cpp
// 077-spice-dice-humanize: capture overlay indices BEFORE lane advances (FR-010)
const size_t velStep = velocityLane_.currentStep();
const size_t gateStep = gateLane_.currentStep();
const size_t ratchetStep = ratchetLane_.currentStep();
const size_t condStep = conditionLane_.currentStep();

// Existing lane advances (unchanged)
float velScale = velocityLane_.advance();
float gateScale = gateLane_.advance();
int8_t pitchOffset = pitchLane_.advance();
uint8_t modifierFlags = modifierLane_.advance();
uint8_t ratchetCount = std::max(uint8_t{1}, ratchetLane_.advance());
uint8_t condValue = conditionLane_.advance();

// 077-spice-dice-humanize: apply Spice blend (FR-008, FR-009)
if (spice_ > 0.0f) {
    // Velocity: linear interpolation (FR-009)
    velScale = velScale + (velocityOverlay_[velStep] - velScale) * spice_;
    // Gate: linear interpolation
    gateScale = gateScale + (gateOverlay_[gateStep] - gateScale) * spice_;
    // Ratchet: lerp + round to integer (FR-008)
    float ratchetBlend = static_cast<float>(ratchetCount)
        + (static_cast<float>(ratchetOverlay_[ratchetStep])
           - static_cast<float>(ratchetCount)) * spice_;
    ratchetCount = static_cast<uint8_t>(
        std::clamp(static_cast<int>(std::round(ratchetBlend)), 1, 4));
    // Condition: threshold blend (FR-008)
    if (spice_ >= 0.5f) {
        condValue = conditionOverlay_[condStep];
    }
}
```

### 7. fireStep() Modification -- Humanize Offsets (FR-014 through FR-021)

After accent application and pitch offset, but BEFORE note emission and gate calculation:

```cpp
// 077-spice-dice-humanize: Humanize offsets (FR-014, FR-022 steps 10-13)
// Always consume 3 PRNG values for deterministic advancement (FR-018)
const float timingRand = humanizeRng_.nextFloat();    // [-1, 1]
const float velocityRand = humanizeRng_.nextFloat();  // [-1, 1]
const float gateRand = humanizeRng_.nextFloat();      // [-1, 1]

// Compute humanized timing offset (FR-015)
const int32_t maxTimingOffsetSamples =
    static_cast<int32_t>(sampleRate_ * 0.020f);  // 20ms
int32_t timingOffsetSamples =
    static_cast<int32_t>(timingRand * static_cast<float>(maxTimingOffsetSamples) * humanize_);
int32_t humanizedSampleOffset = std::clamp(
    sampleOffset + timingOffsetSamples,
    static_cast<int32_t>(0),
    static_cast<int32_t>(blockSize) - 1);

// Compute humanized velocity offset (FR-016)
int velocityOffset = static_cast<int>(velocityRand * 15.0f * humanize_);
// Apply to all notes in result (after accent)
for (size_t i = 0; i < result.count; ++i) {
    int humanizedVel = static_cast<int>(result.velocities[i]) + velocityOffset;
    result.velocities[i] = static_cast<uint8_t>(std::clamp(humanizedVel, 1, 127));
}

// Compute humanized gate offset ratio (FR-017, FR-021)
float gateOffsetRatio = gateRand * 0.10f * humanize_;
```

Then modify gate duration:
```cpp
size_t gateDuration = calculateGateDuration(gateScale);
// Apply humanize gate offset (FR-017)
int32_t humanizedGateDuration = static_cast<int32_t>(gateDuration)
    + static_cast<int32_t>(static_cast<float>(gateDuration) * gateOffsetRatio);
gateDuration = static_cast<size_t>(std::max(int32_t{1}, humanizedGateDuration));
```

And use `humanizedSampleOffset` instead of `sampleOffset` for all noteOn emissions in this step (FR-019).

### 8. Humanize on Skipped Steps (FR-023, FR-024)

At every early return point in fireStep() (Euclidean rest, condition fail, modifier Rest, Tie), the humanize PRNG must be consumed:

```cpp
// 077-spice-dice-humanize: consume humanize PRNG on skipped step (FR-023)
humanizeRng_.nextFloat();  // timing (discarded)
humanizeRng_.nextFloat();  // velocity (discarded)
humanizeRng_.nextFloat();  // gate (discarded)
```

This must be added to:
1. Euclidean rest path (before `return`)
2. Condition fail path (before `return`)
3. Modifier Rest path (before `return`)
4. Modifier Tie path (both branches: has preceding note, and no preceding note)

For Tie (FR-024): PRNG is consumed but offsets are discarded since no new noteOn is emitted.

### 9. Defensive Branch Extension (FR-041)

In the `result.count == 0` defensive branch, AFTER all existing lane advances and loop count increment:

```cpp
// 077-spice-dice-humanize: consume humanize PRNG in defensive branch (FR-041)
// After lane advances, matching normal evaluation order.
humanizeRng_.nextFloat();  // timing (discarded)
humanizeRng_.nextFloat();  // velocity (discarded)
humanizeRng_.nextFloat();  // gate (discarded)
```

### 10. Ratcheted Step Humanize Interaction (FR-019, FR-020, FR-021)

For ratcheted steps (ratchetCount > 1):
- **Timing offset** (FR-019): `humanizedSampleOffset` replaces `sampleOffset` for the first sub-step noteOn emission. Subsequent sub-steps emitted by `processBlock` SubStep handler use their original relative timing.
- **Velocity offset** (FR-020): Applied to `result.velocities[]` (first sub-step only -- accented). Pre-accent velocities for subsequent sub-steps remain unchanged.
- **Gate offset** (FR-021): The humanized gate offset ratio is applied to the sub-step gate duration, so all sub-steps share the same humanized gate ratio.

The sub-step gate calculation for ratcheted steps becomes:
```cpp
size_t subGateDuration = std::max(size_t{1}, static_cast<size_t>(
    static_cast<double>(subStepDuration) *
    static_cast<double>(gateLengthPercent_) / 100.0 *
    static_cast<double>(gateScale)));
// Apply humanize gate offset to sub-step gate
int32_t humanizedSubGate = static_cast<int32_t>(subGateDuration)
    + static_cast<int32_t>(static_cast<float>(subGateDuration) * gateOffsetRatio);
subGateDuration = static_cast<size_t>(std::max(int32_t{1}, humanizedSubGate));
```

And `ratchetGateDuration_` stores this humanized sub-step gate for use by processBlock's sub-step handler.

### 11. Plugin Parameter IDs (FR-031, FR-033)

Add to `plugin_ids.h` between `kArpFillToggleId` and `kArpEndId`:

```cpp
// Spice/Dice & Humanize (077-spice-dice-humanize)
kArpSpiceId               = 3290,   // continuous: 0.0-1.0 (displayed as 0-100%)
kArpDiceTriggerId         = 3291,   // discrete: 0-1 (momentary trigger, edge-detected)
kArpHumanizeId            = 3292,   // continuous: 0.0-1.0 (displayed as 0-100%)
// IDs 3293-3299 reserved for future phases
```

### 12. ArpeggiatorParams Extension (FR-034)

Add to struct after `fillToggle`:

```cpp
// Spice/Dice & Humanize (077-spice-dice-humanize)
std::atomic<float> spice{0.0f};
std::atomic<bool> diceTrigger{false};
std::atomic<float> humanize{0.0f};
```

### 13. handleArpParamChange Extension (FR-035)

Add cases before the `default:` block:

```cpp
// --- Spice/Dice & Humanize (077-spice-dice-humanize) ---
case kArpSpiceId:
    // Continuous: normalized 0-1 maps directly
    params.spice.store(
        std::clamp(static_cast<float>(value), 0.0f, 1.0f),
        std::memory_order_relaxed);
    break;
case kArpDiceTriggerId:
    // Discrete 2-step: 0.0 = idle, 1.0 = trigger
    // Set to true on rising edge (normalized >= 0.5)
    if (value >= 0.5) {
        params.diceTrigger.store(true, std::memory_order_relaxed);
    }
    break;
case kArpHumanizeId:
    // Continuous: normalized 0-1 maps directly
    params.humanize.store(
        std::clamp(static_cast<float>(value), 0.0f, 1.0f),
        std::memory_order_relaxed);
    break;
```

### 14. registerArpParams Extension (FR-031, FR-032)

Add after the Fill toggle registration:

```cpp
// --- Spice/Dice & Humanize (077-spice-dice-humanize) ---

// Spice amount: Continuous 0-1, default 0.0 (0%)
parameters.addParameter(STR16("Arp Spice"), STR16("%"), 0, 0.0,
    ParameterInfo::kCanAutomate, kArpSpiceId);

// Dice trigger: Discrete 2-step (0 = idle, 1 = trigger), default 0
parameters.addParameter(STR16("Arp Dice"), STR16(""), 1, 0.0,
    ParameterInfo::kCanAutomate, kArpDiceTriggerId);

// Humanize amount: Continuous 0-1, default 0.0 (0%)
parameters.addParameter(STR16("Arp Humanize"), STR16("%"), 0, 0.0,
    ParameterInfo::kCanAutomate, kArpHumanizeId);
```

### 15. formatArpParam Extension (FR-039)

Add cases before `default:`:

```cpp
// --- Spice/Dice & Humanize (077-spice-dice-humanize) ---
case kArpSpiceId: {
    char8 text[32];
    snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
    UString(string, 128).fromAscii(text);
    return kResultOk;
}
case kArpDiceTriggerId: {
    UString(string, 128).fromAscii(value >= 0.5 ? "Roll" : "--");
    return kResultOk;
}
case kArpHumanizeId: {
    char8 text[32];
    snprintf(text, sizeof(text), "%.0f%%", value * 100.0);
    UString(string, 128).fromAscii(text);
    return kResultOk;
}
```

### 16. saveArpParams Extension (FR-037)

Add after `fillToggle` write:

```cpp
// --- Spice/Dice & Humanize (077-spice-dice-humanize) ---
streamer.writeFloat(params.spice.load(std::memory_order_relaxed));
streamer.writeFloat(params.humanize.load(std::memory_order_relaxed));
// diceTrigger is NOT serialized (momentary action, FR-030)
// overlay arrays are NOT serialized (ephemeral, FR-030)
```

### 17. loadArpParams Extension (FR-037, FR-038)

Add after the `fillToggle` read:

```cpp
// --- Spice/Dice & Humanize (077-spice-dice-humanize) ---
// EOF-safe: if Spice/Humanize data is missing (Phase 8 preset), keep defaults (FR-038)
if (!streamer.readFloat(floatVal)) return true;  // EOF at first Spice field = Phase 8 compat
params.spice.store(std::clamp(floatVal, 0.0f, 1.0f), std::memory_order_relaxed);

if (!streamer.readFloat(floatVal)) return false;  // Corrupt: spice present but no humanize
params.humanize.store(std::clamp(floatVal, 0.0f, 1.0f), std::memory_order_relaxed);
```

### 18. loadArpParamsToController Extension (FR-040)

Add after the `fillToggle` setParam:

```cpp
// --- Spice/Dice & Humanize (077-spice-dice-humanize) ---
// EOF-safe: if Spice/Humanize data is missing (Phase 8 preset), keep controller defaults
if (!streamer.readFloat(floatVal)) return;
setParam(kArpSpiceId, static_cast<double>(std::clamp(floatVal, 0.0f, 1.0f)));

if (!streamer.readFloat(floatVal)) return;
setParam(kArpHumanizeId, static_cast<double>(std::clamp(floatVal, 0.0f, 1.0f)));
// diceTrigger is NOT synced (transient action)
```

### 19. applyParamsToEngine Extension (FR-036)

Add before `arpCore_.setFillActive(...)` and after condition lane block, or immediately before `setEnabled()` (the final call):

```cpp
// --- Spice/Dice & Humanize (077-spice-dice-humanize) ---
arpCore_.setSpice(arpParams_.spice.load(std::memory_order_relaxed));
// Dice trigger: consume rising edge via compare_exchange_strong (FR-036)
{
    bool expected = true;
    if (arpParams_.diceTrigger.compare_exchange_strong(
            expected, false, std::memory_order_relaxed)) {
        arpCore_.triggerDice();
    }
}
arpCore_.setHumanize(arpParams_.humanize.load(std::memory_order_relaxed));
```

### 20. Evaluation Order Summary (FR-022)

The complete evaluation order in `fireStep()` after all changes (matches spec.md FR-022 step numbering):

0. **Capture overlay indices** (velStep, gateStep, ratchetStep, condStep via `currentStep()` BEFORE any advance)
1. NoteSelector advance (`selector_.advance()`)
2. All lane advances (velocity, gate, pitch, modifier, ratchet, condition)
3. **Spice blend** (velocity, gate, ratchet, condition) -- uses indices captured in step 0
4. Euclidean gating check (with humanize PRNG consumption on rest)
5. Condition lane wrap detection and loopCount_ increment (unchanged from Phase 8; part of lane advance post-processing)
6. Condition evaluation using Spice-blended condValue (with humanize PRNG consumption on fail)
7. Modifier evaluation (Rest/Tie with humanize PRNG consumption; Slide/Accent continue)
8. Velocity scaling (using Spice-blended velScale)
9. Pre-accent velocity capture
10. Accent application
11. Pitch offset application
12. **Humanize PRNG consumption** (timing, velocity, gate -- always 3 calls)
13. **Humanize velocity offset** applied to all notes
14. Gate duration calculation (using Spice-blended gateScale)
15. **Humanize gate offset** applied to gate duration
16. Note emission at **humanized sample offset**
17. Ratcheting (using Spice-blended ratchetCount, humanized gate, humanized first-sub-step onset)

### 21. Test Strategy Summary

**DSP Unit Tests** (arpeggiator_core_test.cpp):

| Test Name | Verifies | Key Assertions |
|---|---|---|
| `SpiceDice_SpiceZero_Phase8Identical` | SC-001 | Run 1000+ steps at 120/140/180 BPM with Spice=0, Dice triggered; compare all events with Phase 8 baseline |
| `SpiceDice_SpiceHundred_OverlayValues` | SC-002 | After Dice, run with Spice=1.0; verify velocities/gates/ratchets/conditions match overlay arrays |
| `SpiceDice_SpiceFifty_Interpolation` | SC-003 | Velocity lane step=1.0, overlay=0.5, Spice=0.5 -> effective=0.75. One case per lane type. |
| `SpiceDice_DiceGeneratesDifferent` | SC-004 | Two triggerDice() calls produce different overlays (>90% elements differ) |
| `Humanize_Zero_NoOffsets` | SC-005 | Humanize=0, Spice=0.5; verify timing/velocity/gate match Spice-only behavior exactly |
| `Humanize_Full_TimingDistribution` | SC-006 | 1000 steps at 44100 Hz; max offset <= 882, mean abs offset > 200 |
| `Humanize_Full_VelocityDistribution` | SC-007 | 1000 steps, base velocity 100; all in [85,115], stddev > 3.0 |
| `Humanize_Full_GateDistribution` | SC-008 | 1000 steps; no deviation > 10%, stddev of ratio > 0.02 |
| `Humanize_Half_ScalesLinearly` | SC-009 | 50% Humanize; max timing ~441, max velocity ~7-8, max gate ~5% |
| `SpiceAndHumanize_ComposeCorrectly` | SC-015 | Both enabled; velocity reflects both Spice blend and Humanize offset |
| `Humanize_PRNGConsumedOnSkip` | FR-023 | Euclidean rest skips step but PRNG state still advances 3 values |
| `Humanize_NotAppliedOnTie` | FR-024 | Tie step: PRNG consumed, but no velocity/timing change |
| `SpiceDice_OverlayPreservedAcrossReset` | FR-025 | triggerDice(), reset(), verify overlays unchanged |
| `SpiceDice_OverlayPreservedAcrossResetLanes` | FR-025 | triggerDice(), resetLanes(), verify overlays unchanged |
| `Humanize_RatchetedStep_TimingFirstOnly` | FR-019 | Timing offset on first sub-step; subsequent sub-steps unshifted |
| `Humanize_RatchetedStep_VelocityFirstOnly` | FR-020 | Velocity offset on first sub-step; subsequent use pre-accent |
| `Humanize_RatchetedStep_GateAllSubSteps` | FR-021 | Gate offset applies to all sub-step durations |
| `Humanize_DefensiveBranch_PRNGConsumed` | FR-041 | Empty held buffer; verify humanizeRng_ advances 3 values |
| `PRNG_DistinctSeeds` | SC-014 | 1000 values from each of 4 PRNGs (seeds 48271, 31337, 7919, 42) differ |
| `Humanize_ConditionSpice_BlendedCondValue` | FR-008 | Spice >= 0.5 uses overlay condition; < 0.5 uses original |

**Plugin Integration Tests** (split across two test files):

| Test Name | File | Verifies | Key Assertions |
|---|---|---|---|
| `SpiceHumanize_StateRoundTrip` | `arp_integration_test.cpp` | SC-010 | Save Spice=0.35, Humanize=0.25; load; verify exact match |
| `SpiceHumanize_Phase8BackwardCompat` | `arp_integration_test.cpp` | SC-011 | Load Phase 8 preset (no Spice/Humanize data); Spice=0, Humanize=0 |
| `SpiceHumanize_ParameterRegistration` | `arpeggiator_params_test.cpp` | SC-013 | All 3 params registered, automatable, display correct strings (see T061-T064) |
