# API Contract: Parameter ID Allocation

**Spec**: 072-independent-lanes
**File**: `plugins/ruinae/src/plugin_ids.h` (modified)

---

## New Parameter IDs

### Velocity Lane (3020-3052)

| ID | Name | Type | Range | Default | Flags |
|----|------|------|-------|---------|-------|
| 3020 | kArpVelocityLaneLengthId | Discrete (int) | 1-32 | 1 | kCanAutomate |
| 3021 | kArpVelocityLaneStep0Id | Continuous (float) | 0.0-1.0 | 1.0 | kCanAutomate, kIsHidden |
| 3022 | kArpVelocityLaneStep1Id | Continuous (float) | 0.0-1.0 | 1.0 | kCanAutomate, kIsHidden |
| ... | ... | ... | ... | ... | ... |
| 3052 | kArpVelocityLaneStep31Id | Continuous (float) | 0.0-1.0 | 1.0 | kCanAutomate, kIsHidden |

### Gate Lane (3060-3092)

| ID | Name | Type | Range | Default | Flags |
|----|------|------|-------|---------|-------|
| 3060 | kArpGateLaneLengthId | Discrete (int) | 1-32 | 1 | kCanAutomate |
| 3061 | kArpGateLaneStep0Id | Continuous (float) | 0.01-2.0 | 1.0 | kCanAutomate, kIsHidden |
| 3062 | kArpGateLaneStep1Id | Continuous (float) | 0.01-2.0 | 1.0 | kCanAutomate, kIsHidden |
| ... | ... | ... | ... | ... | ... |
| 3092 | kArpGateLaneStep31Id | Continuous (float) | 0.01-2.0 | 1.0 | kCanAutomate, kIsHidden |

### Pitch Lane (3100-3132)

| ID | Name | Type | Range | Default | Flags |
|----|------|------|-------|---------|-------|
| 3100 | kArpPitchLaneLengthId | Discrete (int) | 1-32 | 1 | kCanAutomate |
| 3101 | kArpPitchLaneStep0Id | Discrete (int) | -24 to +24 | 0 | kCanAutomate, kIsHidden |
| 3102 | kArpPitchLaneStep1Id | Discrete (int) | -24 to +24 | 0 | kCanAutomate, kIsHidden |
| ... | ... | ... | ... | ... | ... |
| 3132 | kArpPitchLaneStep31Id | Discrete (int) | -24 to +24 | 0 | kCanAutomate, kIsHidden |

---

## Modified Constants

```cpp
kArpEndId = 3199,       // was 3099
kNumParameters = 3200,  // was 3100
```

---

## Registration Pattern

Follows `registerTranceGateParams()` loop pattern:

```cpp
// Velocity lane length: RangeParameter 1-32, default 1, stepCount 31
parameters.addParameter(
    new RangeParameter(STR16("Arp Vel Lane Len"), kArpVelocityLaneLengthId,
                      STR16(""), 1, 32, 1, 31,
                      ParameterInfo::kCanAutomate));

// Velocity lane steps: loop 0-31
for (int i = 0; i < 32; ++i) {
    char name[48];
    snprintf(name, sizeof(name), "Arp Vel Step %d", i + 1);
    Steinberg::Vst::String128 name16;
    Steinberg::UString(name16, 128).fromAscii(name);
    parameters.addParameter(
        new RangeParameter(name16,
            static_cast<ParamID>(kArpVelocityLaneStep0Id + i),
            STR16(""), 0.0, 1.0, 1.0, 0,
            ParameterInfo::kCanAutomate | ParameterInfo::kIsHidden));
}

// Gate lane length: RangeParameter 1-32, default 1, stepCount 31
// Gate lane steps: RangeParameter 0.01-2.0, default 1.0, stepCount 0

// Pitch lane length: RangeParameter 1-32, default 1, stepCount 31
// Pitch lane steps: RangeParameter -24 to +24, default 0, stepCount 48
```

---

## Denormalization Formulas

### handleArpParamChange() additions

```cpp
// Velocity lane length: 0-1 -> 1-32 (stepCount=31)
// Formula: 1 + round(value * 31)
int len = std::clamp(static_cast<int>(1.0 + std::round(value * 31.0)), 1, 32);

// Velocity lane steps: 0-1 -> 0.0-1.0 (direct, no denormalization)
float vel = std::clamp(static_cast<float>(value), 0.0f, 1.0f);

// Gate lane length: same as velocity lane length
// Gate lane steps: 0-1 -> 0.01-2.0
float gate = std::clamp(static_cast<float>(0.01 + value * 1.99), 0.01f, 2.0f);

// Pitch lane length: same as velocity lane length
// Pitch lane steps: 0-1 -> -24 to +24 (stepCount=48)
int pitch = std::clamp(static_cast<int>(-24.0 + std::round(value * 48.0)), -24, 24);
```

---

## Serialization Format

Appended after existing 11 arp params in the state stream:

```
[existing 11 arp params: 44 bytes]
--- Lane data (new) ---
[int32: velocityLaneLength]
[32 x float: velocityLaneSteps]
[int32: gateLaneLength]
[32 x float: gateLaneSteps]
[int32: pitchLaneLength]
[32 x int32: pitchLaneSteps]
```

Total lane data: 3 x int32 (12 bytes) + 64 x float (256 bytes) + 32 x int32 (128 bytes) = 396 bytes.

If stream ends before lane data (Phase 3 preset), all lanes default to length=1 with default step values.
