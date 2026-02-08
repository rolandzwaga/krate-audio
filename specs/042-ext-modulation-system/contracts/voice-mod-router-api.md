# API Contract: VoiceModRouter Extended API

**Feature**: 042-ext-modulation-system | **Layer**: 3 (Systems)

## Modified Method: computeOffsets()

### Previous Signature
```cpp
void computeOffsets(float env1, float env2, float env3,
                    float lfo, float gate,
                    float velocity, float keyTrack) noexcept;
```

### New Signature (FR-003)
```cpp
void computeOffsets(float env1, float env2, float env3,
                    float lfo, float gate,
                    float velocity, float keyTrack,
                    float aftertouch) noexcept;
```

### Parameters

| Parameter | Range | Description |
|-----------|-------|-------------|
| env1 | [0, 1] | Amplitude envelope (ENV 1) output |
| env2 | [0, 1] | Filter envelope (ENV 2) output |
| env3 | [0, 1] | Modulation envelope (ENV 3) output |
| lfo | [-1, +1] | Per-voice LFO output |
| gate | [0, 1] | TranceGate smoothed output |
| velocity | [0, 1] | Note velocity (constant per note) |
| keyTrack | [-1, +1.1] | Key tracking: (midiNote - 60) / 60 |
| **aftertouch** | **[0, 1]** | **Channel aftertouch (NEW)** |

### Behavior
- Clears all destination offsets to 0.0
- Stores all 8 source values in `sourceValues_[8]` array (indexed by VoiceModSource)
- Iterates all 16 route slots; for each active route, computes `sourceValue * amount` and accumulates to destination offset
- After accumulation, sanitizes all offsets: replaces NaN/Inf with 0.0, flushes denormals (FR-024)

### Postconditions
- `getOffset(dest)` returns the accumulated offset for any VoiceModDest value
- All offsets are finite (no NaN, no Inf, no denormals)
- Offsets for destinations with no routes are 0.0

### Error Handling
- Out-of-range enum values in route.source/destination are silently skipped
- NaN/Inf in source parameter values: propagate through multiplication but sanitized at output

---

## Existing Methods (Unchanged)

### setRoute()
```cpp
void setRoute(int index, VoiceModRoute route) noexcept;
```
- Index range: [0, 15]. Out-of-range silently ignored (FR-005 edge case).
- Amount clamped to [-1.0, +1.0].

### clearRoute()
```cpp
void clearRoute(int index) noexcept;
```

### getOffset()
```cpp
[[nodiscard]] float getOffset(VoiceModDest dest) const noexcept;
```
- Returns 0.0 for NumDestinations or invalid values.
- Now supports 9 destinations (was 7).

---

## Test Verification Matrix

| Test Case | Input | Expected Output |
|-----------|-------|-----------------|
| Single Aftertouch route | Aftertouch=0.6, route amount=+1.0, dest=MorphPosition | MorphPosition offset = 0.6 |
| Aftertouch + Env sum | Aftertouch=0.5, Env2=0.8, both -> FilterCutoff, amounts +0.5 each | FilterCutoff offset = 0.65 |
| Zero aftertouch | Aftertouch=0.0, any route amount | Zero contribution from aftertouch |
| OscALevel route | Env3=0.8, route amount=+1.0, dest=OscALevel | OscALevel offset = 0.8 |
| OscBLevel route | LFO=-0.5, route amount=-0.5, dest=OscBLevel | OscBLevel offset = 0.25 |
| Existing route backward compat | Same as 041 tests with aftertouch=0.0 | Same results as before |
