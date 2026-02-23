# Data Model: Conditional Trig System

**Feature**: 076-conditional-trigs | **Date**: 2026-02-22

## Entities

### 1. TrigCondition Enum (New, Layer 2)

Defined in `Krate::DSP` namespace at `dsp/include/krate/dsp/processors/arpeggiator_core.h`, immediately after the existing `ArpRetriggerMode` enum.

| Value | Name | Numeric | Description |
|-------|------|---------|-------------|
| `Always` | Always | 0 | Step fires unconditionally (default) |
| `Prob10` | 10% probability | 1 | Fires ~10% of the time |
| `Prob25` | 25% probability | 2 | Fires ~25% of the time |
| `Prob50` | 50% probability | 3 | Fires ~50% of the time |
| `Prob75` | 75% probability | 4 | Fires ~75% of the time |
| `Prob90` | 90% probability | 5 | Fires ~90% of the time |
| `Ratio_1_2` | 1:2 ratio | 6 | Fires on 1st of every 2 loops |
| `Ratio_2_2` | 2:2 ratio | 7 | Fires on 2nd of every 2 loops |
| `Ratio_1_3` | 1:3 ratio | 8 | Fires on 1st of every 3 loops |
| `Ratio_2_3` | 2:3 ratio | 9 | Fires on 2nd of every 3 loops |
| `Ratio_3_3` | 3:3 ratio | 10 | Fires on 3rd of every 3 loops |
| `Ratio_1_4` | 1:4 ratio | 11 | Fires on 1st of every 4 loops |
| `Ratio_2_4` | 2:4 ratio | 12 | Fires on 2nd of every 4 loops |
| `Ratio_3_4` | 3:4 ratio | 13 | Fires on 3rd of every 4 loops |
| `Ratio_4_4` | 4:4 ratio | 14 | Fires on 4th of every 4 loops |
| `First` | First loop only | 15 | Fires only when loopCount == 0 |
| `Fill` | Fill mode | 16 | Fires only when fillActive_ is true |
| `NotFill` | Not fill mode | 17 | Fires only when fillActive_ is false |
| `kCount` | Sentinel | 18 | Not a valid condition; bounds checking |

**Backing type**: `uint8_t` (`enum class TrigCondition : uint8_t`)

**Storage**: Cast to `uint8_t` for storage in `ArpLane<uint8_t>`. Values 0-17 are valid; `kCount` (18) and above are invalid and treated as `Always` defensively.

**Evaluation formulas**:
- Probability: `conditionRng_.nextUnipolar() < threshold` (thresholds: 0.10, 0.25, 0.50, 0.75, 0.90)
- A:B ratio: `loopCount_ % B == A - 1` (e.g., Ratio_1_2: `loopCount_ % 2 == 0`)
- First: `loopCount_ == 0`
- Fill: `fillActive_`
- NotFill: `!fillActive_`

### 2. Condition State (ArpeggiatorCore members)

Added to `Krate::DSP::ArpeggiatorCore` at `dsp/include/krate/dsp/processors/arpeggiator_core.h`.

| Field | Type | Default | Range | Description |
|-------|------|---------|-------|-------------|
| `conditionLane_` | `ArpLane<uint8_t>` | length 1, step 0 = 0 | length [1,32], steps [0,17] | Per-step condition storage |
| `loopCount_` | `size_t` | `0` | [0, SIZE_MAX] | Condition lane cycle counter |
| `fillActive_` | `bool` | `false` | true/false | Fill mode performance toggle |
| `conditionRng_` | `Xorshift32` | seed 7919 | N/A | Dedicated PRNG for probability evaluation |

**Relationships:**
- `conditionLane_` stores `TrigCondition` enum values as `uint8_t`. Each step has exactly one condition (not a bitmask).
- `loopCount_` increments when `conditionLane_` wraps (after `conditionLane_.advance()`, if `conditionLane_.currentStep() == 0`).
- `fillActive_` is read by `evaluateCondition()` for Fill/NotFill conditions.
- `conditionRng_` is consumed once per step for probability conditions only.

**Validation Rules:**
- `conditionLane_` step values clamped to [0, 17] at the plugin boundary (`applyParamsToEngine()`).
- `conditionLane_` length clamped to [1, 32] via `ArpLane::setLength()` (the max is 32 from `ArpLane<T, 32>`).
- Values >= `kCount` (18) are treated as `Always` defensively in `evaluateCondition()`.

**State Transitions:**

| Trigger | conditionLane_ | loopCount_ | fillActive_ | conditionRng_ |
|---------|---------------|------------|-------------|---------------|
| Construction | length 1, step 0 = Always | 0 | false | seed 7919 |
| `reset()` | Position to 0 (via resetLanes) | 0 | **Preserved** | **Preserved** |
| `resetLanes()` | Position to 0 | 0 | **Preserved** | **Preserved** |
| `setEnabled(false)` | **Preserved** | **Preserved** | **Preserved** | **Preserved** |
| `setEnabled(true)` | Position to 0 (via resetLanes) | 0 | **Preserved** | **Preserved** |
| Lane length change | Position clamped by ArpLane | **Preserved** | N/A | N/A |
| Step advance | advance() + wrap detection | ++loopCount_ on wrap | N/A | Consumed (prob only) |
| `setFillActive()` | N/A | N/A | Updated | N/A |

### 3. Condition Parameter Storage (ArpeggiatorParams members)

Added to `Ruinae::ArpeggiatorParams` at `plugins/ruinae/src/parameters/arpeggiator_params.h`.

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `conditionLaneLength` | `std::atomic<int>` | `1` | Condition lane length (1-32) |
| `conditionLaneSteps` | `std::array<std::atomic<int>, 32>` | all `0` | Per-step condition (0-17 = TrigCondition) |
| `fillToggle` | `std::atomic<bool>` | `false` | Fill mode on/off |

**Thread Safety**: All fields are `std::atomic` for lock-free access between the processor thread (reads via `applyParamsToEngine()`) and parameter change callbacks (writes via `handleArpParamChange()`). `int` is used for step values instead of `uint8_t` to guarantee lock-free atomics on all platforms.

### 4. VST3 Parameter IDs

Added to `Ruinae::ParameterIDs` enum at `plugins/ruinae/src/plugin_ids.h`.

| Parameter | ID | Type | Min | Max | Default | stepCount | Flags |
|-----------|----|------|-----|-----|---------|-----------|-------|
| `kArpConditionLaneLengthId` | 3240 | RangeParameter | 1 | 32 | 1 | 31 | kCanAutomate |
| `kArpConditionLaneStep0Id` | 3241 | RangeParameter | 0 | 17 | 0 | 17 | kCanAutomate, kIsHidden |
| `kArpConditionLaneStep1Id` | 3242 | RangeParameter | 0 | 17 | 0 | 17 | kCanAutomate, kIsHidden |
| ... | ... | ... | ... | ... | ... | ... | ... |
| `kArpConditionLaneStep31Id` | 3272 | RangeParameter | 0 | 17 | 0 | 17 | kCanAutomate, kIsHidden |
| `kArpFillToggleId` | 3280 | Toggle | 0 | 1 | 0 | 1 | kCanAutomate |

All 32 step parameters (3241-3272) have `kIsHidden` to avoid cluttering the host's parameter list. The length parameter (3240) and fill toggle (3280) are visible.

**Note**: `kArpEndId` remains 3299 and `kNumParameters` remains 3300. No sentinel change needed.

### 5. Serialization Format

**Binary stream order** (appended after Euclidean timing data):

| Offset (relative) | Type | Field | Backward Compat |
|--------------------|------|-------|-----------------|
| +0 | int32 | conditionLaneLength (1-32) | EOF here = Phase 7 compat (return true) |
| +4 | int32 | conditionLaneStep[0] (0-17) | EOF here = corrupt (return false) |
| +8 | int32 | conditionLaneStep[1] (0-17) | EOF here = corrupt (return false) |
| ... | ... | ... | ... |
| +128 | int32 | conditionLaneStep[31] (0-17) | EOF here = corrupt (return false) |
| +132 | int32 | fillToggle (0 or 1) | EOF here = corrupt (return false) |

Total: 136 bytes appended to the existing stream (34 int32 values).

### 6. Existing Components (No Changes)

#### Xorshift32 (Layer 0)

`Krate::DSP::Xorshift32` at `dsp/include/krate/dsp/core/random.h`.

| Method | Signature | Used For |
|--------|-----------|----------|
| Constructor | `explicit constexpr Xorshift32(uint32_t seedValue = 1) noexcept` | Construct with seed 7919 |
| `nextUnipolar` | `[[nodiscard]] constexpr float nextUnipolar() noexcept` | Probability evaluation (returns [0.0, 1.0]) |

#### ArpLane<uint8_t> (Layer 1)

`Krate::DSP::ArpLane<uint8_t>` at `dsp/include/krate/dsp/primitives/arp_lane.h`.

| Method | Signature | Used For |
|--------|-----------|----------|
| `setLength` | `void setLength(size_t len) noexcept` | Set condition lane length |
| `setStep` | `void setStep(size_t index, T value) noexcept` | Set per-step condition value |
| `getStep` | `[[nodiscard]] T getStep(size_t index) const noexcept` | Read per-step condition value |
| `advance` | `T advance() noexcept` | Advance lane position, return current value |
| `reset` | `void reset() noexcept` | Reset lane position to 0 |
| `currentStep` | `[[nodiscard]] size_t currentStep() const noexcept` | Wrap detection after advance |

## Entity Relationships

```
Xorshift32 (Layer 0, random.h)
    |
    +-- nextUnipolar() called by --> evaluateCondition() in ArpeggiatorCore

ArpLane<uint8_t> (Layer 1, arp_lane.h)
    |
    +-- Template reused as conditionLane_ member of ArpeggiatorCore
    +-- advance() returns condition value + moves position
    +-- currentStep() == 0 after advance = wrap detection

ArpeggiatorCore (Layer 2, arpeggiator_core.h)
    |
    +-- conditionLane_ ----> stores TrigCondition values (uint8_t)
    +-- loopCount_ ---------> increments on conditionLane_ wrap
    +-- fillActive_ --------> queried by Fill/NotFill conditions
    +-- conditionRng_ ------> consumed by probability conditions
    +-- evaluateCondition() -> dispatch: switch on TrigCondition
    +-- fireStep() ---------> condition evaluation between Euclidean and Modifier
    +-- resetLanes() -------> resets conditionLane_ position + loopCount_

ArpeggiatorParams (Plugin layer, arpeggiator_params.h)
    |
    +-- conditionLaneLength/Steps/fillToggle --> transferred to ArpeggiatorCore
    +-- serialized/deserialized in state save/load
    +-- synced to controller via loadArpParamsToController()

Three-Layer Gating Chain (fireStep evaluation order):
    Euclidean (structural rhythm)
        |
        +-- rest: short-circuit (condition NOT evaluated, PRNG NOT consumed)
        +-- hit: proceed to condition
            |
            Condition (evolutionary/probabilistic)
                |
                +-- fail: rest (modifier NOT evaluated, ratchet NOT applied)
                +-- pass: proceed to modifier
                    |
                    Modifier (per-step articulation: Rest > Tie > Slide > Accent)
                        |
                        +-- Ratcheting (sub-step emission)
```
