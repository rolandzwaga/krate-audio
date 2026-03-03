# API Contract: ArpeggiatorCore Conditional Trig System

**Feature**: 076-conditional-trigs | **Date**: 2026-02-22

## TrigCondition Enum

```cpp
namespace Krate::DSP {

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

} // namespace Krate::DSP
```

## ArpeggiatorCore Public API Additions

### Condition Lane Accessors

```cpp
/// @brief Access the condition lane for reading/writing step values.
/// Condition values are TrigCondition enum cast to uint8_t (range 0-17).
ArpLane<uint8_t>& conditionLane() noexcept;

/// @brief Const access to the condition lane.
[[nodiscard]] const ArpLane<uint8_t>& conditionLane() const noexcept;
```

### Fill Mode API

```cpp
/// @brief Set fill mode active state. Real-time safe, no side effects.
/// Called from processor's applyParamsToEngine() on every block.
/// Preserved across reset()/resetLanes() -- only changed by this setter.
void setFillActive(bool active) noexcept;

/// @brief Get current fill mode state.
[[nodiscard]] bool fillActive() const noexcept;
```

### Private Helper (Not Public API, documented for contract completeness)

```cpp
/// @brief Evaluate a TrigCondition for the current step.
/// @param condition The condition to evaluate (TrigCondition enum as uint8_t)
/// @return true if the step should fire, false if it should be treated as rest
/// Consumes conditionRng_ only for probability conditions (Prob10-Prob90).
/// Uses loopCount_ for A:B ratio and First conditions.
/// Uses fillActive_ for Fill/NotFill conditions.
/// Values >= kCount are treated as Always (defensive fallback).
inline bool evaluateCondition(uint8_t condition) noexcept;
```

## VST3 Parameter Contract

### Parameter Registration

| ID | Name | Type | Min | Max | Default | stepCount | Flags |
|----|------|------|-----|-----|---------|-----------|-------|
| 3240 | "Arp Cond Lane Len" | RangeParameter | 1 | 32 | 1 | 31 | kCanAutomate |
| 3241 | "Arp Cond Step 1" | RangeParameter | 0 | 17 | 0 | 17 | kCanAutomate, kIsHidden |
| 3242 | "Arp Cond Step 2" | RangeParameter | 0 | 17 | 0 | 17 | kCanAutomate, kIsHidden |
| ... | ... | ... | ... | ... | ... | ... | ... |
| 3272 | "Arp Cond Step 32" | RangeParameter | 0 | 17 | 0 | 17 | kCanAutomate, kIsHidden |
| 3280 | "Arp Fill" | Toggle | 0 | 1 | 0 | 1 | kCanAutomate |

### Normalization Formulas

**Condition Lane Length (discrete 1-32):**
- Normalized -> Plain: `clamp(1 + round(value * 31), 1, 32)`
- Plain -> Normalized: `(length - 1) / 31.0`

**Condition Lane Step (discrete 0-17):**
- Normalized -> Plain: `clamp(round(value * 17), 0, 17)`
- Plain -> Normalized: `conditionValue / 17.0`

**Fill Toggle (boolean):**
- Normalized -> Plain: `value >= 0.5 ? true : false`
- Plain -> Normalized: `fill ? 1.0 : 0.0`

### Display Formatting

| Parameter | Format | Examples |
|-----------|--------|---------|
| Condition Lane Length | len == 1 ? "%d step" : "%d steps" | "1 step", "8 steps", "32 steps" |
| Condition Lane Step | TrigCondition name | "Always", "50%", "1:2", "1st", "Fill", "!Fill" |
| Fill Toggle | "Off" / "On" | "Off", "On" |

**Complete step display mapping:**

| Value | Display |
|-------|---------|
| 0 | "Always" |
| 1 | "10%" |
| 2 | "25%" |
| 3 | "50%" |
| 4 | "75%" |
| 5 | "90%" |
| 6 | "1:2" |
| 7 | "2:2" |
| 8 | "1:3" |
| 9 | "2:3" |
| 10 | "3:3" |
| 11 | "1:4" |
| 12 | "2:4" |
| 13 | "3:4" |
| 14 | "4:4" |
| 15 | "1st" |
| 16 | "Fill" |
| 17 | "!Fill" |

## Serialization Contract

### Stream Format (appended after Euclidean timing data)

```
[existing Euclidean data: enabled, hits, steps, rotation]
int32: conditionLaneLength (1-32)
int32: conditionLaneStep[0] (0-17)
int32: conditionLaneStep[1] (0-17)
...
int32: conditionLaneStep[31] (0-17)
int32: fillToggle (0 or 1)
```

### Backward Compatibility Rules

| EOF Point | Interpretation | Action |
|-----------|---------------|--------|
| Before conditionLaneLength | Phase 7 preset | return true, keep defaults (length 1, all Always, fill off) |
| After conditionLaneLength but before all 32 steps read | Corrupt stream | return false |
| After all 32 steps but before fillToggle | Corrupt stream | return false |
| After all 34 fields | Complete load | return true |

### Value Clamping on Load

| Field | Clamp Range |
|-------|-------------|
| conditionLaneLength | [1, 32] |
| conditionLaneStep[i] | [0, 17] |
| fillToggle | bool (intVal != 0) |

## fireStep() Behavioral Contract

### Evaluation Order (Updated from Phase 7)

```
1. selector_.advance() (NoteSelector -- always advances)
2. Lane advances: velocity, gate, pitch, modifier, ratchet, *condition* (always advance)
3. Loop count detection: if conditionLane_.currentStep() == 0 after advance, ++loopCount_
4. Euclidean gating check (if enabled):
   a. Read isHit at current position
   b. Advance euclideanPosition_
   c. If rest: emit noteOffs, break tie chain, return
5. *Condition evaluation* (NEW):
   a. Read condition value (returned by conditionLane_.advance() in step 2)
   b. Call evaluateCondition(conditionValue)
   c. If condition fails:
      - Cancel pending noteOffs for current notes
      - Emit noteOff for all currently sounding notes
      - Set currentArpNoteCount_ = 0
      - Set tieActive_ = false (break tie chain)
      - Increment swingStepCounter_
      - Recalculate currentStepDuration_
      - Return (skip modifier evaluation and ratcheting)
6. Modifier evaluation: Rest > Tie > Slide > Accent (existing)
7. Ratcheting (existing)
```

### Condition Fail Behavior

When a condition evaluates to false, the step is treated identically to a Euclidean rest:
- Cancel pending noteOffs for current notes
- Emit noteOff for all currently sounding notes
- Set `currentArpNoteCount_ = 0`
- Set `tieActive_ = false` (break any tie chain)
- Increment `swingStepCounter_`
- Recalculate `currentStepDuration_`
- Return early (skip modifier evaluation and ratcheting)

### Defensive Branch (result.count == 0)

When the held note buffer is empty:
- Advance condition lane (along with modifier and ratchet lanes)
- Check condition lane wrap, increment `loopCount_` if wrapped
- Advance `euclideanPosition_` if enabled
- Existing defensive cleanup

### Interaction Matrix

| Euclidean | Condition | Modifier | Result |
|-----------|-----------|----------|--------|
| Rest | (not evaluated) | (not evaluated) | Silent -- Euclidean overrides all |
| Hit | Fail | (not evaluated) | Silent -- condition gates step |
| Hit | Pass (Always) | Active | Normal note emission |
| Hit | Pass (Always) | Rest | Silent -- modifier Rest still works |
| Hit | Pass (Always) | Tie | Sustain previous note |
| Hit | Pass (Prob50) | Active | Note fires ~50% of the time |
| Hit | Pass (Ratio_1_2) | Active | Note fires on odd loops only |
| Hit | Pass (Fill) | Active | Note fires only when Fill toggle is on |
| Hit | Pass (First) | Active | Note fires only on first loop |
| Disabled | Fail | (not evaluated) | Silent -- condition gates step |
| Disabled | Pass | Active | Normal note emission |
| Disabled | Pass | Rest | Silent |

### PRNG Consumption Rules

| Scenario | PRNG Consumed? | Reason |
|----------|---------------|--------|
| Euclidean rest step | NO | Condition evaluation skipped entirely |
| Probability condition (Prob10-Prob90) | YES (once) | `conditionRng_.nextUnipolar()` called |
| Non-probability condition (Always, A:B, First, Fill, NotFill) | NO | No randomness needed |
| Defensive branch (no held notes) | NO | Condition evaluation skipped |

## Parameter Application Order

When transferring condition parameters from `ArpeggiatorParams` to `ArpeggiatorCore` in `applyParamsToEngine()`:

```cpp
// 1. Expand-write-shrink pattern for condition lane
arpCore_.conditionLane().setLength(32);         // Expand to max
for (int i = 0; i < 32; ++i) {
    int val = clamp(conditionLaneSteps[i], 0, 17);
    arpCore_.conditionLane().setStep(i, static_cast<uint8_t>(val));
}
arpCore_.conditionLane().setLength(actualLength); // Shrink to actual

// 2. Fill toggle (simple setter)
arpCore_.setFillActive(fillToggle);
```

**Note**: The expand-write-shrink pattern does NOT affect `loopCount_`. The `ArpLane::setLength()` call only clamps the lane position if it exceeds the new length. The loop counter is a separate member of `ArpeggiatorCore`.
