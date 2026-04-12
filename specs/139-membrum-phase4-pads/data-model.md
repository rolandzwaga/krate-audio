# Data Model: Membrum Phase 4

**Date**: 2026-04-12

## Entities

### 1. PadConfig

A pre-allocated structure holding one pad's complete configuration. 32 instances stored in the processor/voice pool. No dynamic allocation.

```
PadConfig
    exciterType     : ExciterType (enum, 6 values)          -- offset 0
    bodyModel       : BodyModelType (enum, 6 values)        -- offset 1
    material        : float [0.0, 1.0]                      -- offset 2
    size            : float [0.0, 1.0]                      -- offset 3
    decay           : float [0.0, 1.0]                      -- offset 4
    strikePosition  : float [0.0, 1.0]                      -- offset 5
    level           : float [0.0, 1.0]                      -- offset 6

    -- Tone Shaper (14 params, offsets 7-20)
    tsFilterType       : int [0-2] (LP/HP/BP)               -- offset 7
    tsFilterCutoff     : float [0.0, 1.0] (norm)            -- offset 8
    tsFilterResonance  : float [0.0, 1.0]                   -- offset 9
    tsFilterEnvAmount  : float [0.0, 1.0] (norm of -1..+1)  -- offset 10
    tsDriveAmount      : float [0.0, 1.0]                   -- offset 11
    tsFoldAmount       : float [0.0, 1.0]                   -- offset 12
    tsPitchEnvStart    : float [0.0, 1.0] (norm)            -- offset 13
    tsPitchEnvEnd      : float [0.0, 1.0] (norm)            -- offset 14
    tsPitchEnvTime     : float [0.0, 1.0] (norm)            -- offset 15
    tsPitchEnvCurve    : int [0-1] (Exp/Lin)                -- offset 16
    tsFilterEnvAttack  : float [0.0, 1.0] (norm)            -- offset 17
    tsFilterEnvDecay   : float [0.0, 1.0] (norm)            -- offset 18
    tsFilterEnvSustain : float [0.0, 1.0]                   -- offset 19
    tsFilterEnvRelease : float [0.0, 1.0] (norm)            -- offset 20

    -- Unnatural Zone (4 params)
    modeStretch        : float [0.0, 1.0] (norm of 0.5..2.0) -- offset 21
    decaySkew          : float [0.0, 1.0] (norm of -1..+1)   -- offset 22
    modeInjectAmount   : float [0.0, 1.0]                    -- offset 23
    nonlinearCoupling  : float [0.0, 1.0]                    -- offset 24

    -- Material Morph (5 params)
    morphEnabled       : int [0-1]                           -- offset 25
    morphStart         : float [0.0, 1.0]                    -- offset 26
    morphEnd           : float [0.0, 1.0]                    -- offset 27
    morphDuration      : float [0.0, 1.0] (norm)             -- offset 28
    morphCurve         : int [0-1] (Lin/Exp)                 -- offset 29

    -- Kit-level per-pad settings
    chokeGroup         : uint8 [0-8]                         -- offset 30
    outputBus          : uint8 [0-15]                        -- offset 31

    -- Exciter secondary params (per-pad, offsets 32-35)
    fmRatio            : float [0.0, 1.0]                    -- offset 32
    feedbackAmount     : float [0.0, 1.0]                    -- offset 33
    noiseBurstDuration : float [0.0, 1.0] (norm)             -- offset 34
    frictionPressure   : float [0.0, 1.0]                    -- offset 35
```

Exciter secondary parameters (FM Ratio, Feedback Amount, NoiseBurst Duration, Friction Pressure) are per-pad parameters stored at PadConfig offsets 32-35. They were moved from global-only atomics in Phase 3 to per-pad storage in Phase 4 so that each pad can have an independently tuned exciter character.

**Validation Rules:**
- All float values clamped to [0.0, 1.0] on load (state deserialization)
- Enum values clamped to valid range on load
- `chokeGroup` clamped to [0, 8]
- `outputBus` clamped to [0, 15]; falls back to 0 if assigned bus is inactive

**Relationships:**
- PadConfig[32] owned by VoicePool (or Processor)
- Each voice references a PadConfig at noteOn time (read-only during voice lifetime)
- Per-pad parameters in controller (IDs 1000-3047) map 1:1 to PadConfig fields (36 active offsets per pad, offsets 36-63 reserved)

**Serialization Note**: Sound parameter values are stored in-memory as `float` but serialized as `float64` (double) for full precision in preset files. On load, values are read as double and cast to float.

### 2. Per-Pad Parameter ID Scheme

```
Parameter ID = kPadBaseId + padIndex * kPadParamStride + paramOffset

Where:
    kPadBaseId       = 1000
    kPadParamStride  = 64
    padIndex         = [0, 31]
    paramOffset      = [0, 35] (active); [36, 63] reserved

Pad 0:  IDs 1000-1063  (1000-1035 active, 1036-1063 reserved)
Pad 1:  IDs 1064-1127
...
Pad 31: IDs 2984-3047
```

### 3. Global / Proxy Parameter Mapping

```
Global ID (100-252)  <-->  Per-Pad Offset  <-->  PadConfig field
--------------------------------------------------------------
kMaterialId (100)          offset 2              material
kSizeId (101)              offset 3              size
kDecayId (102)             offset 4              decay
kStrikePositionId (103)    offset 5              strikePosition
kLevelId (104)             offset 6              level
kExciterTypeId (200)       offset 0              exciterType
kBodyModelId (201)         offset 1              bodyModel
kExciterFMRatioId (202)    offset 32             fmRatio
kExciterFeedbackAmountId (203)  offset 33        feedbackAmount
kExciterNoiseBurstDurationId (204)  offset 34    noiseBurstDuration
kExciterFrictionPressureId (205)    offset 35    frictionPressure
...
kToneShaperFilterTypeId (210)      offset 7    tsFilterType
kToneShaperFilterCutoffId (211)    offset 8    tsFilterCutoff
...
kChokeGroupId (252)        offset 30             chokeGroup
kSelectedPadId (260)       N/A -- controller-only pad selector

Note: Output Bus (offset 31) is NOT proxied through a global parameter ID. It can only be
set via the per-pad parameter directly. This is intentional: output routing is a kit-level
concern, not a per-pad sound parameter.
```

Exciter secondary parameters (FM Ratio, Feedback Amount, NoiseBurst Duration, Friction Pressure) are now per-pad parameters stored at PadConfig offsets 32-35. Each pad independently stores its own exciter secondary settings, enabling per-pad exciter character (e.g., different noise burst durations for snare vs. hat pads).

### 4. Output Bus Map

```
OutputBusMap
    busAssignment[32] : uint8  -- derived from PadConfig[i].outputBus
    busActive[16]     : bool   -- tracked via activateBus() notifications

Invariant: busAssignment[i] refers to a bus index [0, 15]
           busActive[0] = true always (main)
           busActive[1..15] set by host
```

### 5. State v4 Binary Layout

```
Offset  Size     Field
------  ----     -----
0       4        int32: version = 4
4       4        int32: maxPolyphony [4, 16]
8       4        int32: voiceStealingPolicy [0, 2]

-- Pad 0 (offset 12):
12      4        int32: exciterType
16      4        int32: bodyModel
20      272      34 x float64: sound params (offsets 2-35 from PadConfig, as normalized float64)
292     1        uint8: chokeGroup
293     1        uint8: outputBus

-- Pad 1 (offset 294):
294     282      (same layout as pad 0: 4+4+272+1+1 = 282 bytes)

-- ...pad 31 ends at offset 12 + 32 * 282 = 9036

9036    4        int32: selectedPadIndex [0, 31]

Total: 9040 bytes
```

### 6. Kit Preset Binary Layout

Same as state v4 but WITHOUT the `selectedPadIndex` at the end:

```
Total: 9036 bytes (state v4 minus final 4 bytes)
```

### 7. Per-Pad Preset Binary Layout

```
Offset  Size     Field
------  ----     -----
0       4        int32: version = 1
4       4        int32: exciterType
8       4        int32: bodyModel
12      272      34 x float64: sound params (offsets 2-35)

Total: 284 bytes
```

### 8. GM Default Template Table

| Template | Exciter           | Body       | Material | Size  | Decay | StrikePos | Level | PitchEnv           |
|----------|-------------------|------------|----------|-------|-------|-----------|-------|--------------------|
| Kick     | Impulse (0)       | Membrane(0)| 0.3      | 0.8   | 0.3   | 0.3       | 0.8   | 160->50Hz, 20ms    |
| Snare    | NoiseBurst (2)    | Membrane(0)| 0.5      | 0.5   | 0.4   | 0.3       | 0.8   | off                |
| Tom      | Mallet (1)        | Membrane(0)| 0.4      | varies| 0.5   | 0.3       | 0.8   | off                |
| Hat      | NoiseBurst (2)    | NoiseBody(5)| 0.9     | 0.15  | 0.1   | 0.3       | 0.8   | off                |
| Cymbal   | NoiseBurst (2)    | NoiseBody(5)| 0.95    | 0.3   | 0.8   | 0.3       | 0.8   | off                |
| Perc     | Mallet (1)        | Plate (1)  | 0.7      | 0.3   | 0.3   | 0.3       | 0.8   | off                |
