# API Contract: State Serialization for Per-Step Modifiers

**Date**: 2026-02-21

## Save Format Extension

**File**: `plugins/ruinae/src/parameters/arpeggiator_params.h` (saveArpParams)

After the existing pitch lane data, append:

```
// --- Modifier Lane (073-per-step-mods) ---
int32: modifierLaneLength          // 1-32
int32[32]: modifierLaneSteps[0..31] // 0-255 (uint8_t bitmask stored as int32)
int32: accentVelocity              // 0-127
float: slideTime                   // 0.0-500.0 ms
```

## Load Format Extension

**File**: `plugins/ruinae/src/parameters/arpeggiator_params.h` (loadArpParams)

After pitch lane loading, add EOF-safe modifier lane loading. The EOF handling rule is:
- **EOF at `modifierLaneLength` read** = Phase 4 preset; return `true` (success, use defaults)
- **EOF at any subsequent modifier read** = corrupt/truncated stream; return `false` (failure)
- The caller (Processor::setState) MUST treat a `false` return as full load failure and
  restore all plugin parameters to defaults -- partial modifier state is never silently accepted.

```cpp
// --- Modifier Lane (073-per-step-mods) ---
// EOF-safe: if modifier data is missing entirely (Phase 4 preset), keep defaults.
// If modifier data is partially present (truncated after length), return false (corrupt).
if (!streamer.readInt32(intVal)) return true;  // EOF at first modifier field = Phase 4 compat
params.modifierLaneLength.store(std::clamp(intVal, 1, 32), std::memory_order_relaxed);

// From here, EOF signals a corrupt stream (length was present but steps are not)
for (int i = 0; i < 32; ++i) {
    if (!streamer.readInt32(intVal)) return false;  // Corrupt: length present but no step data
    params.modifierLaneSteps[i].store(
        std::clamp(intVal, 0, 255), std::memory_order_relaxed);
}

// Accent velocity
if (!streamer.readInt32(intVal)) return false;  // Corrupt: steps present but no accentVelocity
params.accentVelocity.store(std::clamp(intVal, 0, 127), std::memory_order_relaxed);

// Slide time
if (!streamer.readFloat(floatVal)) return false;  // Corrupt: accentVelocity present but no slideTime
params.slideTime.store(std::clamp(floatVal, 0.0f, 500.0f), std::memory_order_relaxed);
```

## Backward Compatibility

When loading a Phase 4 preset (no modifier data in stream):
- The first `readInt32()` call for `modifierLaneLength` returns `false` (EOF)
- `loadArpParams` returns `true` (previous phase data loaded OK; modifier fields keep defaults)
- All modifier fields retain their default-constructed values:
  - `modifierLaneLength = 1`
  - `modifierLaneSteps[0..31] = 1` (kStepActive)
  - `accentVelocity = 30`
  - `slideTime = 60.0f`

When loading a stream that has `modifierLaneLength` but no step data (corrupt/truncated):
- The first `readInt32()` succeeds (`modifierLaneLength` read OK)
- The next `readInt32()` (for `modifierLaneSteps[0]`) returns `false` (EOF)
- `loadArpParams` returns `false` (corrupt stream; caller MUST restore all plugin defaults)

This produces identical behavior to Phase 4 in the clean backward-compat case because:
- Length 1 with kStepActive means every step is "normal active"
- No Accent/Tie/Slide/Rest flags are set
- Accent velocity and slide time have no effect without their respective flags

## Controller Sync (loadArpParamsToController)

The controller sync function must also be extended to read the new fields from the stream and set the normalized parameter values:

```cpp
// Modifier lane length: int32 -> normalized: (len - 1) / 31
if (!streamer.readInt32(intVal)) return;
setParam(kArpModifierLaneLengthId,
    static_cast<double>(std::clamp(intVal, 1, 32) - 1) / 31.0);

// Modifier lane steps: int32 -> normalized: value / 255
for (int i = 0; i < 32; ++i) {
    if (!streamer.readInt32(intVal)) return;
    setParam(static_cast<ParamID>(kArpModifierLaneStep0Id + i),
        static_cast<double>(std::clamp(intVal, 0, 255)) / 255.0);
}

// Accent velocity: int32 -> normalized: value / 127
if (!streamer.readInt32(intVal)) return;
setParam(kArpAccentVelocityId, static_cast<double>(std::clamp(intVal, 0, 127)) / 127.0);

// Slide time: float -> normalized: value / 500
if (!streamer.readFloat(floatVal)) return;
setParam(kArpSlideTimeId,
    static_cast<double>(std::clamp(floatVal, 0.0f, 500.0f)) / 500.0);
```
