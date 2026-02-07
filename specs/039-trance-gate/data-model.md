# Data Model: Trance Gate (039)

**Date**: 2026-02-07 | **Layer**: 2 (Processor) | **Namespace**: `Krate::DSP`

## Entities

### GateStep

A single step in the gate pattern. Holds a float gain level, not a boolean.

| Field | Type | Range | Default | Description |
|-------|------|-------|---------|-------------|
| `level` | `float` | [0.0, 1.0] | 1.0 | Gain level. 0.0 = silence, 1.0 = full, intermediate = ghost notes/accents |

**Validation**: Level is clamped to [0.0, 1.0] on assignment.

**Notes**: Using float instead of boolean enables expressive patterns with ghost notes (0.2-0.4), accents (0.8-1.0), and full silence (0.0).

---

### TranceGateParams

Configuration parameter struct for the TranceGate processor.

| Field | Type | Range | Default | Description |
|-------|------|-------|---------|-------------|
| `numSteps` | `int` | [2, 32] | 16 | Number of active steps in pattern |
| `rateHz` | `float` | [0.1, 100.0] | 4.0 | Free-run step rate in Hz |
| `depth` | `float` | [0.0, 1.0] | 1.0 | Wet/dry depth. 0 = bypass, 1 = full gate |
| `attackMs` | `float` | [1.0, 20.0] | 2.0 | Attack ramp time (lower-to-higher level transition) |
| `releaseMs` | `float` | [1.0, 50.0] | 10.0 | Release ramp time (higher-to-lower level transition) |
| `phaseOffset` | `float` | [0.0, 1.0] | 0.0 | Pattern rotation. 0.5 on 16-step = start at step 8 |
| `tempoSync` | `bool` | true/false | true | Tempo sync mode vs free-run |
| `noteValue` | `NoteValue` | enum | `NoteValue::Sixteenth` | Step duration note value |
| `noteModifier` | `NoteModifier` | enum | `NoteModifier::None` | Dotted/Triplet modifier |
| `perVoice` | `bool` | true/false | true | Per-voice (reset on noteOn) vs global clock |

**Validation rules**:
- `numSteps` is clamped to [2, 32]
- `attackMs` clamped to [1.0, 20.0]
- `releaseMs` clamped to [1.0, 50.0]
- `depth` clamped to [0.0, 1.0]
- `phaseOffset` clamped to [0.0, 1.0]
- `rateHz` clamped to [0.1, 100.0]

**Dependencies**: `NoteValue` and `NoteModifier` from `<krate/dsp/core/note_value.h>` (Layer 0).

---

### TranceGate (Internal State)

Internal state of the TranceGate processor. Not exposed as a public struct.

| Field | Type | Description |
|-------|------|-------------|
| `pattern_` | `std::array<float, 32>` | Step levels (32 slots, only numSteps_ active) |
| `numSteps_` | `int` | Current number of active steps |
| `currentStep_` | `int` | Current step index [0, numSteps_-1] |
| `sampleCounter_` | `size_t` | Sample counter within current step |
| `samplesPerStep_` | `size_t` | Calculated step duration in samples |
| `sampleRate_` | `double` | Configured sample rate |
| `tempoBPM_` | `double` | Current tempo in BPM |
| `attackSmoother_` | `OnePoleSmoother` | Smoother for rising transitions |
| `releaseSmoother_` | `OnePoleSmoother` | Smoother for falling transitions |
| `currentGainValue_` | `float` | Last computed gain value (for getGateValue()) |
| `params_` | `TranceGateParams` | Current configuration |
| `prepared_` | `bool` | Whether prepare() has been called |
| `rotationOffset_` | `int` | Computed step read offset from phaseOffset |

---

## Relationships

```
TranceGateParams (1) -----> (1) TranceGate
     |                              |
     | configures                   | contains 32x
     v                              v
  NoteValue (L0)              GateStep.level (float)
  NoteModifier (L0)
```

### Dependency Graph

```
Layer 0                    Layer 1                    Layer 2
---------                  ---------                  ---------
EuclideanPattern --------> (used by)           ----> TranceGate
NoteValue/NoteModifier --> (used by params)    ----> TranceGate
getBeatsForNote() -------> (used by timing)    ----> TranceGate
                           OnePoleSmoother --> (composed by) TranceGate
```

---

## State Transitions

### Step Advancement

```
[Current Step N] ---(sampleCounter >= samplesPerStep)---> [Current Step (N+1) % numSteps]
                                                           |
                                                           +--> Update smoother target to new step level
                                                           +--> Reset sampleCounter to 0
```

### Smoother Direction Selection

```
[New target > Current value] ---> Use attackSmoother_ (rising)
                                  Sync releaseSmoother_.snapTo(output)

[New target < Current value] ---> Use releaseSmoother_ (falling)
                                  Sync attackSmoother_.snapTo(output)

[New target == Current value] --> No smoothing needed (at target)
```

### Reset Behavior

```
[reset() called]
  |
  +-- perVoice == true:  Reset sampleCounter_ = 0, currentStep_ = 0,
  |                      snap smoothers to pattern_[0] level
  |
  +-- perVoice == false: No-op (clock continues uninterrupted)
```

---

## Gain Computation Pipeline (per sample)

```
1. Advance timing: sampleCounter_++
2. Check step boundary: if sampleCounter_ >= samplesPerStep_
     -> advance currentStep_, reset counter
3. Read effective step: effectiveStep = (currentStep_ + rotationOffset_) % numSteps_
4. Get step level: targetLevel = pattern_[effectiveStep]
5. Select smoother direction:
     if targetLevel > currentSmoothed: use attackSmoother_
     else: use releaseSmoother_
6. Process smoother: smoothedGain = activeSmoother.process()
7. Sync inactive smoother: inactiveSmoother.snapTo(smoothedGain)
8. Apply depth: finalGain = 1.0f + (smoothedGain - 1.0f) * depth
     (equivalent to lerp(1.0, smoothedGain, depth))
9. Store: currentGainValue_ = finalGain
10. Return: input * finalGain
```
