# Data Model: Additional Modulation Sources

**Spec**: 059-additional-mod-sources | **Date**: 2026-02-16

## Entities

### EnvFollowerParams

**Location**: `plugins/ruinae/src/parameters/env_follower_params.h`
**Namespace**: `Ruinae`

| Field | Type | Range | Default | Storage Format |
|-------|------|-------|---------|----------------|
| `sensitivity` | `std::atomic<float>` | [0, 1] | 0.5 | float (state stream) |
| `attackMs` | `std::atomic<float>` | [0.1, 500] ms | 10.0 | float (state stream) |
| `releaseMs` | `std::atomic<float>` | [1, 5000] ms | 100.0 | float (state stream) |

**Mapping**: Sensitivity is direct [0,1]. Attack uses log mapping: `0.1 * pow(5000, norm)`. Release uses log mapping: `1.0 * pow(5000, norm)`.

**Validation**: All values clamped to their respective ranges in `handleEnvFollowerParamChange()`.

---

### SampleHoldParams

**Location**: `plugins/ruinae/src/parameters/sample_hold_params.h`
**Namespace**: `Ruinae`

| Field | Type | Range | Default | Storage Format |
|-------|------|-------|---------|----------------|
| `rateHz` | `std::atomic<float>` | [0.1, 50] Hz | 4.0 | float (state stream) |
| `sync` | `std::atomic<bool>` | on/off | false | int32 (0/1) |
| `noteValue` | `std::atomic<int>` | [0, 20] (dropdown index) | 10 (1/8 note) | int32 |
| `slewMs` | `std::atomic<float>` | [0, 500] ms | 0.0 | float (state stream) |

**Mapping**: Rate uses `lfoRateFromNormalized()` (shared with LFO, Chaos). Sync is boolean (>=0.5). NoteValue is dropdown index. Slew is linear: `norm * 500`.

**State transitions**: When Sync changes from off to on, the Rate knob hides and NoteValue dropdown shows (visibility switching via `custom-view-name` groups in uidesc).

---

### RandomParams

**Location**: `plugins/ruinae/src/parameters/random_params.h`
**Namespace**: `Ruinae`

| Field | Type | Range | Default | Storage Format |
|-------|------|-------|---------|----------------|
| `rateHz` | `std::atomic<float>` | [0.1, 50] Hz | 4.0 | float (state stream) |
| `sync` | `std::atomic<bool>` | on/off | false | int32 (0/1) |
| `noteValue` | `std::atomic<int>` | [0, 20] (dropdown index) | 10 (1/8 note) | int32 |
| `smoothness` | `std::atomic<float>` | [0, 1] | 0.0 | float (state stream) |

**Mapping**: Rate uses `lfoRateFromNormalized()`. Sync is boolean. NoteValue is dropdown index. Smoothness is direct [0,1].

**State transitions**: Same visibility switching as S&H when Sync toggles.

---

### PitchFollowerParams

**Location**: `plugins/ruinae/src/parameters/pitch_follower_params.h`
**Namespace**: `Ruinae`

| Field | Type | Range | Default | Storage Format |
|-------|------|-------|---------|----------------|
| `minHz` | `std::atomic<float>` | [20, 500] Hz | 80.0 | float (state stream) |
| `maxHz` | `std::atomic<float>` | [200, 5000] Hz | 2000.0 | float (state stream) |
| `confidence` | `std::atomic<float>` | [0, 1] | 0.5 | float (state stream) |
| `speedMs` | `std::atomic<float>` | [10, 300] ms | 50.0 | float (state stream) |

**Mapping**: MinHz: `20 * pow(25, norm)`. MaxHz: `200 * pow(25, norm)`. Confidence is direct [0,1]. Speed is linear: `10 + norm * 290`.

---

### TransientParams

**Location**: `plugins/ruinae/src/parameters/transient_params.h`
**Namespace**: `Ruinae`

| Field | Type | Range | Default | Storage Format |
|-------|------|-------|---------|----------------|
| `sensitivity` | `std::atomic<float>` | [0, 1] | 0.5 | float (state stream) |
| `attackMs` | `std::atomic<float>` | [0.5, 10] ms | 2.0 | float (state stream) |
| `decayMs` | `std::atomic<float>` | [20, 200] ms | 50.0 | float (state stream) |

**Mapping**: Sensitivity is direct [0,1]. Attack is linear: `0.5 + norm * 9.5`. Decay is linear: `20 + norm * 180`.

---

## State Persistence Schema

### Version 15 Format (appended after v14)

```
[v14 data: settings params]
[v15 data:]
  EnvFollower:   sensitivity(float) + attackMs(float) + releaseMs(float)     = 12 bytes
  SampleHold:    rateHz(float) + sync(int32) + noteValue(int32) + slewMs(float) = 16 bytes
  Random:        rateHz(float) + sync(int32) + noteValue(int32) + smoothness(float) = 16 bytes
  PitchFollower: minHz(float) + maxHz(float) + confidence(float) + speedMs(float) = 16 bytes
  Transient:     sensitivity(float) + attackMs(float) + decayMs(float)       = 12 bytes
  Total v15 appended: 72 bytes
```

### Backward Compatibility

When loading presets with version < 15:
- All mod source params use struct defaults (constructed values)
- No migration needed for ModSource enum indices (unchanged from v14)

## Parameter ID Map

| ID | Name | Source | Type |
|----|------|--------|------|
| 2300 | `kEnvFollowerSensitivityId` | Env Follower | Continuous [0,1] |
| 2301 | `kEnvFollowerAttackId` | Env Follower | Continuous (log ms) |
| 2302 | `kEnvFollowerReleaseId` | Env Follower | Continuous (log ms) |
| 2400 | `kSampleHoldRateId` | Sample & Hold | Continuous (log Hz) |
| 2401 | `kSampleHoldSyncId` | Sample & Hold | Boolean (stepCount=1) |
| 2402 | `kSampleHoldNoteValueId` | Sample & Hold | Dropdown (21 entries) |
| 2403 | `kSampleHoldSlewId` | Sample & Hold | Continuous (linear ms) |
| 2500 | `kRandomRateId` | Random | Continuous (log Hz) |
| 2501 | `kRandomSyncId` | Random | Boolean (stepCount=1) |
| 2502 | `kRandomNoteValueId` | Random | Dropdown (21 entries) |
| 2503 | `kRandomSmoothnessId` | Random | Continuous [0,1] |
| 2600 | `kPitchFollowerMinHzId` | Pitch Follower | Continuous (log Hz) |
| 2601 | `kPitchFollowerMaxHzId` | Pitch Follower | Continuous (log Hz) |
| 2602 | `kPitchFollowerConfidenceId` | Pitch Follower | Continuous [0,1] |
| 2603 | `kPitchFollowerSpeedId` | Pitch Follower | Continuous (linear ms) |
| 2700 | `kTransientSensitivityId` | Transient | Continuous [0,1] |
| 2701 | `kTransientAttackId` | Transient | Continuous (linear ms) |
| 2702 | `kTransientDecayId` | Transient | Continuous (linear ms) |
