# Parameter ID Contract: Step Pattern Editor

## New Parameter IDs (Ruinae plugin_ids.h)

```cpp
// Within the Trance Gate Parameters (600-699) block:

kTranceGateEuclideanEnabledId = 608,  // on/off toggle
kTranceGateEuclideanHitsId = 609,     // 0-32 integer
kTranceGateEuclideanRotationId = 610, // 0-31 integer
kTranceGatePhaseOffsetId = 611,       // 0.0-1.0 continuous

// Step level parameters: contiguous block of 32
kTranceGateStepLevel0Id = 668,
kTranceGateStepLevel1Id = 669,
kTranceGateStepLevel2Id = 670,
// ... (pattern: kTranceGateStepLevel0Id + stepIndex)
kTranceGateStepLevel31Id = 699,
```

## Modified Parameter

```cpp
// BEFORE (dropdown with 3 values):
kTranceGateNumStepsId = 601, // StringListParameter: "8", "16", "32"

// AFTER (integer range):
kTranceGateNumStepsId = 601, // RangeParameter: min=2, max=32, stepCount=30, default=16
```

## Parameter Registration Contract

### Step Level Parameters (32x)
```cpp
// Each step level parameter:
// - Name: "Gate Step N" (N = 1-32, 1-indexed for display)
// - Unit: ""
// - Flags: kCanAutomate | kIsHidden (hidden from generic UI, shown in StepPatternEditor)
// - Default: 1.0 (normalized, maps to level 1.0)
// - Type: RangeParameter with min=0, max=1, stepCount=0 (continuous)

for (int i = 0; i < 32; ++i) {
    char name[32];
    snprintf(name, sizeof(name), "Gate Step %d", i + 1);
    parameters.addParameter(
        new RangeParameter(STR16(name), kTranceGateStepLevel0Id + i,
                          STR16(""), 0.0, 1.0, 1.0));
}
```

### NumSteps Parameter (changed)
```cpp
// BEFORE:
parameters.addParameter(createDropdownParameterWithDefault(
    STR16("Gate Steps"), kTranceGateNumStepsId, 1,
    {STR16("8"), STR16("16"), STR16("32")}
));

// AFTER:
parameters.addParameter(
    new RangeParameter(STR16("Gate Steps"), kTranceGateNumStepsId,
                      STR16(""), 2, 32, 16, 30));
```

### Euclidean Parameters
```cpp
// Euclidean Enabled: boolean toggle
parameters.addParameter(STR16("Gate Euclidean"), STR16(""), 1, 0.0,
    ParameterInfo::kCanAutomate, kTranceGateEuclideanEnabledId);

// Euclidean Hits: integer 0-32
parameters.addParameter(
    new RangeParameter(STR16("Gate Euclidean Hits"), kTranceGateEuclideanHitsId,
                      STR16(""), 0, 32, 4, 32));

// Euclidean Rotation: integer 0-31
parameters.addParameter(
    new RangeParameter(STR16("Gate Euclidean Rotation"), kTranceGateEuclideanRotationId,
                      STR16(""), 0, 31, 0, 31));

// Phase Offset: continuous 0-1
parameters.addParameter(STR16("Gate Phase Offset"), STR16(""), 0, 0.0,
    ParameterInfo::kCanAutomate, kTranceGatePhaseOffsetId);
```

## State Persistence Contract

### Save Order (appended to existing trance gate state)
```
// Existing fields (v1, unchanged):
int32: enabled
int32: numStepsIndex  // NOTE: for v2, this becomes the actual step count
float: rateHz
float: depth
float: attackMs
float: releaseMs
int32: tempoSync
int32: noteValue

// New fields (v2):
int32: stateVersion (2)  // Version marker for migration
int32: euclideanEnabled
int32: euclideanHits
int32: euclideanRotation
float: phaseOffset
float[32]: stepLevels  // All 32 step levels, regardless of active count
```

### Migration from v1 to v2
```
// v1 numStepsIndex mapping:
// 0 -> 8 steps
// 1 -> 16 steps
// 2 -> 32 steps
// All step levels default to 1.0 (not present in v1)
```

## Denormalization Formulas

| Parameter | Formula (normalized -> real) | Formula (real -> normalized) |
|-----------|------------------------------|------------------------------|
| NumSteps | `2 + round(norm * 30)` | `(steps - 2) / 30.0` |
| StepLevel[i] | `norm` (identity) | `level` (identity) |
| EuclideanHits | `round(norm * 32)` | `hits / 32.0` |
| EuclideanRotation | `round(norm * 31)` | `rotation / 31.0` |
| PhaseOffset | `norm` (identity) | `offset` (identity) |
