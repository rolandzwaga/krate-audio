# Data Model: Multi-Stage Envelope Generator

**Date**: 2026-02-07 | **Spec**: specs/033-multi-stage-envelope/spec.md

---

## Entities

### 1. EnvStageConfig (Value Type)

Configuration for a single envelope stage.

| Field | Type | Range | Default | Description |
|-------|------|-------|---------|-------------|
| `targetLevel` | `float` | 0.0 - 1.0 | 0.0 | Target output level at end of stage |
| `timeMs` | `float` | 0.0 - 10000.0 | 100.0 | Transition time in milliseconds |
| `curve` | `EnvCurve` | Exponential/Linear/Logarithmic | Exponential | Curve shape for transition (FR-020) |

**Validation**: `targetLevel` clamped to [0, 1]. `timeMs` clamped to [0, 10000]. Zero timeMs = instant transition (1 sample).

### 2. MultiStageEnvState (Enumeration)

| Value | Description |
|-------|-------------|
| `Idle` | Not active. Output = 0.0. `isActive()` = false. |
| `Running` | Traversing stages sequentially. Gate is on. |
| `Sustaining` | Holding at sustain point level. Gate is on, loop disabled. |
| `Releasing` | Decaying to 0.0 after gate-off. |

**State Transitions**:
```
                    gate on (hard)
               +-----------------+
               |                 |
               v                 |
    +------+  gate on  +---------+   reached sustain   +-----------+
    | Idle | --------> | Running | --(no loop)-------> | Sustaining|
    +------+           +---------+                     +-----------+
       ^                    |                               |
       |                    | gate off                      | gate off
       |                    v                               v
       |              +-----------+                   +-----------+
       +-<idle thresh-| Releasing |<------------------| Releasing |
                      +-----------+                   +-----------+
```

### 3. MultiStageEnvelope (Main Class)

Lives at: `dsp/include/krate/dsp/processors/multi_stage_envelope.h`
Namespace: `Krate::DSP`
Layer: 2 (Processor)

**Configuration fields** (set via public methods):

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `stages_` | `std::array<EnvStageConfig, 8>` | All defaults | Per-stage configuration |
| `numStages_` | `int` | 4 | Active stage count [4, 8] |
| `sustainPoint_` | `int` | `numStages_ - 2` | Sustain hold stage index |
| `loopEnabled_` | `bool` | false | Loop mode on/off |
| `loopStart_` | `int` | 0 | Loop start stage index |
| `loopEnd_` | `int` | 0 | Loop end stage index |
| `releaseTimeMs_` | `float` | 100.0 | Release phase time |
| `retriggerMode_` | `RetriggerMode` | Hard | Retrigger behavior |

**Runtime state fields** (internal):

| Field | Type | Description |
|-------|------|-------------|
| `state_` | `MultiStageEnvState` | Current FSM state |
| `output_` | `float` | Current envelope output value |
| `currentStage_` | `int` | Index of currently active stage |
| `sampleCounter_` | `int` | Samples elapsed in current stage |
| `totalStageSamples_` | `int` | Total samples for current stage |
| `stageStartLevel_` | `float` | Output level at start of current stage |
| `stageCoef_` | `float` | One-pole coefficient for current stage |
| `stageBase_` | `float` | One-pole base for current stage |
| `logPhase_` | `float` | Phase for logarithmic curve [0, 1] |
| `logPhaseInc_` | `float` | Phase increment per sample (log) |
| `releaseCoef_` | `float` | One-pole coefficient for release |
| `releaseBase_` | `float` | One-pole base for release |
| `sustainSmoothCoef_` | `float` | Sustain level smoothing coefficient |
| `sampleRate_` | `float` | Current sample rate |

### 4. Shared Envelope Utilities (envelope_utils.h)

Extracted from ADSREnvelope to be shared between ADSR and MultiStageEnvelope.

**Constants**:
- `kEnvelopeIdleThreshold` (1e-4f)
- `kMinEnvelopeTimeMs` (0.1f)
- `kMaxEnvelopeTimeMs` (10000.0f)
- `kSustainSmoothTimeMs` (5.0f)
- `kDefaultTargetRatioA` (0.3f)
- `kDefaultTargetRatioDR` (0.0001f)
- `kLinearTargetRatio` (100.0f)

**Enumerations**:
- `EnvCurve` (Exponential, Linear, Logarithmic)
- `RetriggerMode` (Hard, Legato)

**Types**:
- `StageCoefficients` { coef, base }

**Functions**:
- `calcCoefficients(timeMs, sampleRate, targetLevel, targetRatio, rising)` -> `StageCoefficients`
- `getAttackTargetRatio(EnvCurve)` -> float
- `getDecayTargetRatio(EnvCurve)` -> float

---

## Relationships

```
envelope_utils.h (Layer 1)
    ^               ^
    |               |
    |               |
ADSREnvelope     MultiStageEnvelope
(Layer 1)        (Layer 2)
    |                |
    v                v
 db_utils.h      db_utils.h
 (Layer 0)       (Layer 0)
```

- `MultiStageEnvelope` depends on `envelope_utils.h` for `EnvCurve`, `RetriggerMode`, `StageCoefficients`, `calcCoefficients()`, and all envelope constants.
- `ADSREnvelope` is modified to also depend on `envelope_utils.h` instead of defining those locally.
- Both depend on `db_utils.h` for `detail::isNaN()`, `detail::constexprExp()`, `detail::flushDenormal()`.

---

## Invariants

1. `numStages_` is always in [4, 8].
2. `sustainPoint_` is always in [0, numStages_ - 1].
3. `loopStart_` <= `loopEnd_`, both in [0, numStages_ - 1].
4. `output_` is in [0.0, 1.0] during normal operation (no negative values, no overshoot).
5. Stage target levels are in [0.0, 1.0].
6. In Idle state, `output_` == 0.0 and `isActive()` == false.
7. `sampleCounter_` <= `totalStageSamples_` during Running state.
8. Release phase coefficient is calculated for exponential curve targeting 0.0.
