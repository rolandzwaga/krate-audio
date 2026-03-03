# Parameter ID Contract: Additional Modulation Sources

**Spec**: 059-additional-mod-sources | **Date**: 2026-02-16

## ID Allocation

| Range Start | Range End | Source | Count |
|-------------|-----------|--------|-------|
| 2300 | 2302 | Env Follower | 3 |
| 2400 | 2403 | Sample & Hold | 4 |
| 2500 | 2503 | Random | 4 |
| 2600 | 2603 | Pitch Follower | 4 |
| 2700 | 2702 | Transient | 3 |

**Total new parameters**: 18
**New kNumParameters**: 2800 (was 2300)

## Env Follower Parameters (2300-2302)

```cpp
kEnvFollowerBaseId       = 2300,
kEnvFollowerSensitivityId = 2300,  // Continuous [0, 1], default 0.5
kEnvFollowerAttackId      = 2301,  // Log [0.1, 500] ms, default 10 ms
kEnvFollowerReleaseId     = 2302,  // Log [1, 5000] ms, default 100 ms
kEnvFollowerEndId         = 2399,
```

### Mapping Functions

```cpp
// Attack: [0, 1] -> [0.1, 500] ms
inline float envFollowerAttackFromNormalized(double value) {
    return 0.1f * std::pow(5000.0f, static_cast<float>(value));
}
inline double envFollowerAttackToNormalized(float ms) {
    return std::log(ms / 0.1f) / std::log(5000.0f);
}

// Release: [0, 1] -> [1, 5000] ms
inline float envFollowerReleaseFromNormalized(double value) {
    return 1.0f * std::pow(5000.0f, static_cast<float>(value));
}
inline double envFollowerReleaseToNormalized(float ms) {
    return std::log(ms / 1.0f) / std::log(5000.0f);
}
```

## Sample & Hold Parameters (2400-2403)

```cpp
kSampleHoldBaseId      = 2400,
kSampleHoldRateId      = 2400,  // Log [0.1, 50] Hz via lfoRateFromNormalized(), default 4 Hz
kSampleHoldSyncId      = 2401,  // Boolean (stepCount=1), default false
kSampleHoldNoteValueId = 2402,  // Dropdown [0, 20] (21 entries), default 10 (1/8 note)
kSampleHoldSlewId      = 2403,  // Linear [0, 500] ms, default 0 ms
kSampleHoldEndId       = 2499,
```

### Mapping Functions

```cpp
// Rate: reuses lfoRateFromNormalized() from lfo1_params.h
// Slew: linear [0, 500] ms
inline float sampleHoldSlewFromNormalized(double value) {
    return static_cast<float>(value) * 500.0f;
}
inline double sampleHoldSlewToNormalized(float ms) {
    return static_cast<double>(ms / 500.0f);
}
```

### Sync Behavior

When `sync == true`: Rate knob hidden, NoteValue dropdown shown. The processor converts NoteValue + BPM to Hz:
```cpp
float delayMs = dropdownToDelayMs(noteIdx, tempoBPM_);
float rateHz = 1000.0f / delayMs;
engine_.setSampleHoldRate(rateHz);
```

## Random Parameters (2500-2503)

```cpp
kRandomBaseId       = 2500,
kRandomRateId       = 2500,  // Log [0.1, 50] Hz via lfoRateFromNormalized(), default 4 Hz
kRandomSyncId       = 2501,  // Boolean (stepCount=1), default false
kRandomNoteValueId  = 2502,  // Dropdown [0, 20] (21 entries), default 10 (1/8 note)
kRandomSmoothnessId = 2503,  // Continuous [0, 1], default 0.0
kRandomEndId        = 2599,
```

### Sync Behavior

Same pattern as S&H. RandomSource built-in tempo sync is bypassed (`setRandomTempoSync(false)` always). Plugin-level conversion:
```cpp
float delayMs = dropdownToDelayMs(noteIdx, tempoBPM_);
float rateHz = 1000.0f / delayMs;
engine_.setRandomRate(rateHz);
```

## Pitch Follower Parameters (2600-2603)

```cpp
kPitchFollowerBaseId       = 2600,
kPitchFollowerMinHzId      = 2600,  // Log [20, 500] Hz, default 80 Hz
kPitchFollowerMaxHzId      = 2601,  // Log [200, 5000] Hz, default 2000 Hz
kPitchFollowerConfidenceId = 2602,  // Continuous [0, 1], default 0.5
kPitchFollowerSpeedId      = 2603,  // Linear [10, 300] ms, default 50 ms
kPitchFollowerEndId        = 2699,
```

### Mapping Functions

```cpp
// MinHz: [0, 1] -> [20, 500] Hz
inline float pitchFollowerMinHzFromNormalized(double value) {
    return 20.0f * std::pow(25.0f, static_cast<float>(value));
}
inline double pitchFollowerMinHzToNormalized(float hz) {
    return std::log(hz / 20.0f) / std::log(25.0f);
}

// MaxHz: [0, 1] -> [200, 5000] Hz
inline float pitchFollowerMaxHzFromNormalized(double value) {
    return 200.0f * std::pow(25.0f, static_cast<float>(value));
}
inline double pitchFollowerMaxHzToNormalized(float hz) {
    return std::log(hz / 200.0f) / std::log(25.0f);
}

// Speed: linear [10, 300] ms
inline float pitchFollowerSpeedFromNormalized(double value) {
    return 10.0f + static_cast<float>(value) * 290.0f;
}
inline double pitchFollowerSpeedToNormalized(float ms) {
    return static_cast<double>((ms - 10.0f) / 290.0f);
}
```

## Transient Parameters (2700-2702)

```cpp
kTransientBaseId         = 2700,
kTransientSensitivityId  = 2700,  // Continuous [0, 1], default 0.5
kTransientAttackId       = 2701,  // Linear [0.5, 10] ms, default 2 ms
kTransientDecayId        = 2702,  // Linear [20, 200] ms, default 50 ms
kTransientEndId          = 2799,
```

### Mapping Functions

```cpp
// Attack: linear [0.5, 10] ms
inline float transientAttackFromNormalized(double value) {
    return 0.5f + static_cast<float>(value) * 9.5f;
}
inline double transientAttackToNormalized(float ms) {
    return static_cast<double>((ms - 0.5f) / 9.5f);
}

// Decay: linear [20, 200] ms
inline float transientDecayFromNormalized(double value) {
    return 20.0f + static_cast<float>(value) * 180.0f;
}
inline double transientDecayToNormalized(float ms) {
    return static_cast<double>((ms - 20.0f) / 180.0f);
}
```

## State Stream Contract (v15)

Appended after v14 data in exact order:

| Order | Field | Type | Bytes |
|-------|-------|------|-------|
| 1 | envFollower.sensitivity | float | 4 |
| 2 | envFollower.attackMs | float | 4 |
| 3 | envFollower.releaseMs | float | 4 |
| 4 | sampleHold.rateHz | float | 4 |
| 5 | sampleHold.sync | int32 | 4 |
| 6 | sampleHold.noteValue | int32 | 4 |
| 7 | sampleHold.slewMs | float | 4 |
| 8 | random.rateHz | float | 4 |
| 9 | random.sync | int32 | 4 |
| 10 | random.noteValue | int32 | 4 |
| 11 | random.smoothness | float | 4 |
| 12 | pitchFollower.minHz | float | 4 |
| 13 | pitchFollower.maxHz | float | 4 |
| 14 | pitchFollower.confidence | float | 4 |
| 15 | pitchFollower.speedMs | float | 4 |
| 16 | transient.sensitivity | float | 4 |
| 17 | transient.attackMs | float | 4 |
| 18 | transient.decayMs | float | 4 |

**Total appended**: 72 bytes

## Backward Compatibility

- Version < 15: All 18 parameters use struct defaults (no migration)
- ModSource enum indices: Unchanged (no preset migration for mod matrix)
