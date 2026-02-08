# API Contract: RuinaeVoice Extensions

**Feature**: 042-ext-modulation-system | **Layer**: 3 (Systems)

## New Method: setAftertouch() (FR-010)

```cpp
void setAftertouch(float value) noexcept {
    if (detail::isNaN(value) || detail::isInf(value)) return;
    aftertouch_ = std::clamp(value, 0.0f, 1.0f);
}
```

### Parameters
| Parameter | Range | Description |
|-----------|-------|-------------|
| value | [0, 1] | Channel aftertouch pressure (0 = no pressure) |

### Behavior
- Stores aftertouch value for use in subsequent processBlock() calls
- NaN/Inf values are silently ignored (value unchanged)
- Clamped to [0, 1]
- Persists until next setAftertouch() call
- Default: 0.0 (no aftertouch)
- Passed to modRouter_.computeOffsets() as the 8th parameter

### Thread Safety
- Must be called from audio thread (same thread as processBlock)
- Safe to call at any time (before noteOn, during processing, etc.)

---

## Modified Behavior: processBlock() (FR-004)

### OscALevel/OscBLevel Application

**Before mixing**, the following computation occurs at **block start** (once per block, not per sample). The base level 1.0 is the pre-VCA oscillator amplitude — offsets are applied to oscABuffer_/oscBBuffer_ BEFORE the final amplitude envelope (VCA) multiplication:
```cpp
// Compute modulation offsets at block start (using envelope/LFO values at block start)
const float oscALevelOffset = modRouter_.getOffset(VoiceModDest::OscALevel);
const float oscBLevelOffset = modRouter_.getOffset(VoiceModDest::OscBLevel);
const float effectiveOscALevel = std::clamp(1.0f + oscALevelOffset, 0.0f, 1.0f);
const float effectiveOscBLevel = std::clamp(1.0f + oscBLevelOffset, 0.0f, 1.0f);

// Apply level modulation to oscillator buffers
if (effectiveOscALevel != 1.0f) {
    for (size_t i = 0; i < numSamples; ++i) {
        oscABuffer_[i] *= effectiveOscALevel;
    }
}
if (effectiveOscBLevel != 1.0f) {
    for (size_t i = 0; i < numSamples; ++i) {
        oscBBuffer_[i] *= effectiveOscBLevel;
    }
}
```

### Application Formula
```
effectiveLevel = clamp(baseLevel + offset, 0.0, 1.0)
where baseLevel = 1.0 (fixed constant, NOT user-adjustable)
```

### Behavior
- When no routes target OscALevel/OscBLevel, offset = 0.0, effectiveLevel = 1.0 (no change)
- When offset = -1.0, effectiveLevel = 0.0 (silence)
- When offset = +0.5, effectiveLevel = 1.0 (clamped, cannot exceed unity)
- When offset = -0.5, effectiveLevel = 0.5 (half volume)

### Aftertouch in computeOffsets()

The per-sample loop now passes `aftertouch_` to computeOffsets():
```cpp
modRouter_.computeOffsets(
    ampEnvVal, filterEnvVal, modEnvVal,
    lfoVal, getGateValue(),
    velocity_, keyTrackValue,
    aftertouch_  // NEW 8th parameter
);
```

---

## New Member Variable

```cpp
float aftertouch_{0.0f};  ///< Channel aftertouch [0, 1]
```

---

## Test Verification Matrix

| Test Case | Setup | Expected |
|-----------|-------|----------|
| No OscLevel routes | Process with default voice | OSC A and B at unity, no amplitude change |
| OscALevel = -1.0 | Route Env3->OscALevel amount=+1.0, Env3=0 (attack start) | OSC A at base 1.0, offset = 0.0 -> unity |
| OscALevel crossfade | Route Env1->OscALevel=-0.5, Env1->OscBLevel=+0.5 at Env1=0.5 | OSC A attenuated, OSC B at unity (clamped) |
| Aftertouch to MorphPosition | setAftertouch(0.6), route AT->MorphPos amount=+1.0 | MorphPosition offset = 0.6 |
| Aftertouch default zero | No setAftertouch() call, route AT->any, amount=1.0 | Zero contribution |
| Backward compat | No OscLevel routes, no aftertouch | Identical to 041 behavior |
