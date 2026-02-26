# Transform Operations Contract

## Transform Types

```cpp
enum class TransformType { kInvert = 0, kShiftLeft = 1, kShiftRight = 2, kRandomize = 3 };
```

## Per-Lane Semantics

### Velocity Lane (ArpLaneEditor, kVelocity)

| Transform | Formula | Example |
|-----------|---------|---------|
| Invert | `new = 1.0 - old` | [1.0, 0.5, 0.0, 0.75] -> [0.0, 0.5, 1.0, 0.25] |
| Shift Left | `new[i] = old[(i+1) % N]` | [A, B, C, D] -> [B, C, D, A] |
| Shift Right | `new[i] = old[(i-1+N) % N]` | [A, B, C, D] -> [D, A, B, C] |
| Randomize | `new = uniform_real(0.0, 1.0)` | [0.42, 0.87, 0.13, 0.65] |

### Gate Lane (ArpLaneEditor, kGate)

Same as Velocity (both use 0.0-1.0 normalized range).

### Pitch Lane (ArpLaneEditor, kPitch)

| Transform | Formula (on normalized 0-1) | Semitone Equivalent |
|-----------|----------------------------|---------------------|
| Invert | `new = 1.0 - old` (mirrors around 0.5 = 0 semitones) | Negate: +12 -> -12 |
| Shift Left | Same as velocity | Same rotation |
| Shift Right | Same as velocity | Same rotation |
| Randomize | `snap to semitone(uniform_real(0.0, 1.0))` | Random -24 to +24 integer |

### Ratchet Lane (ArpLaneEditor, kRatchet)

| Transform | Formula (on normalized 0-1) | Discrete Equivalent |
|-----------|----------------------------|---------------------|
| Invert | `new = 1.0 - old` | 1->4, 2->3, 3->2, 4->1 (mirror around 2.5) |
| Shift Left | Same as velocity | Same rotation |
| Shift Right | Same as velocity | Same rotation |
| Randomize | `(uniform_int(0, 3)) / 3.0` | Random 1-4 discrete |

### Modifier Lane (ArpModifierLane)

| Transform | Formula (on bitmask 0-15) | Description |
|-----------|--------------------------|-------------|
| Invert | `new = (~old) & 0x0F` | Toggle all 4 flags |
| Shift Left | `new[i] = old[(i+1) % N]` | Rotate bitmask pattern |
| Shift Right | `new[i] = old[(i-1+N) % N]` | Rotate bitmask pattern |
| Randomize | `uniform_int(0, 15)` | Each flag independently 50% |

### Condition Lane (ArpConditionLane)

| Transform | Formula (on index 0-17) | Description |
|-----------|------------------------|-------------|
| Invert | Probability inversion table | 10%<->90%, 25%<->75%, 50% stays, non-prob unchanged |
| Shift Left | `new[i] = old[(i+1) % N]` | Rotate condition pattern |
| Shift Right | `new[i] = old[(i-1+N) % N]` | Rotate condition pattern |
| Randomize | `uniform_int(0, 17)` | Random condition from all 18 |

### Condition Inversion Table

```
Index 0 (Always)    -> 0 (Always)     -- unchanged
Index 1 (10%)       -> 5 (90%)
Index 2 (25%)       -> 4 (75%)
Index 3 (50%)       -> 3 (50%)        -- unchanged (self-inverse)
Index 4 (75%)       -> 2 (25%)
Index 5 (90%)       -> 1 (10%)
Index 6-14 (ratio)  -> unchanged      -- ratio conditions are not invertible
Index 15 (First)    -> 15 (First)     -- unchanged
Index 16 (Fill)     -> 17 (Not Fill)
Index 17 (Not Fill) -> 16 (Fill)
```

## Parameter Update Protocol (FR-019)

For each step `i` modified by a transform:

```cpp
uint32_t paramId = stepBaseParamId + i;
controller->beginEdit(paramId);
controller->performEdit(paramId, newNormalizedValue);
controller->setParamNormalized(paramId, newNormalizedValue);
controller->endEdit(paramId);
```

This ensures:
1. Host undo records each step change
2. Automation reflects the new values
3. The processor receives parameter updates via IParameterChanges

## Edge Cases

- Lane with 1 step: Shift Left/Right are no-ops
- Lane with 0 steps: All transforms are no-ops (should not occur, min is 1)
- Randomize uses `std::mt19937` seeded from `std::random_device` (already used in StepPatternEditor for Euclidean regen)
