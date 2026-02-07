# Data Model: Note Event Processor (036)

**Date**: 2026-02-07 | **Status**: Complete

## Entities

### VelocityCurve (Enum, Layer 0)

**Location**: `dsp/include/krate/dsp/core/midi_utils.h`

| Value | Integer | Description |
|-------|---------|-------------|
| `Linear` | 0 | `velocity / 127.0` -- uniform mapping |
| `Soft` | 1 | `(velocity / 127.0)^0.5` -- square root, emphasizes low velocities |
| `Hard` | 2 | `(velocity / 127.0)^2.0` -- squared, de-emphasizes low velocities |
| `Fixed` | 3 | `1.0` for any non-zero velocity, ignores dynamics |

**Validation**: Velocity clamped to [0, 127]. Velocity 0 always returns 0.0 regardless of curve.

---

### VelocityOutput (Struct, Layer 2)

**Location**: `dsp/include/krate/dsp/processors/note_processor.h`

| Field | Type | Range | Default | Description |
|-------|------|-------|---------|-------------|
| `amplitude` | `float` | [0.0, 1.0] | 0.0 | Velocity scaled for amplitude destination |
| `filter` | `float` | [0.0, 1.0] | 0.0 | Velocity scaled for filter cutoff destination |
| `envelopeTime` | `float` | [0.0, 1.0] | 0.0 | Velocity scaled for envelope time destination |

**Aggregate**: Simple struct with designated initializer support.

---

### NoteProcessor (Class, Layer 2)

**Location**: `dsp/include/krate/dsp/processors/note_processor.h`

#### Member Variables

| Member | Type | Default | Description |
|--------|------|---------|-------------|
| `bendSmoother_` | `OnePoleSmoother` | 0.0 | Smooths bipolar pitch bend input |
| `currentBendSemitones_` | `float` | 0.0 | Cached: `smoothedBend * pitchBendRange_` |
| `currentBendRatio_` | `float` | 1.0 | Cached: `semitonesToRatio(currentBendSemitones_)` |
| `a4Reference_` | `float` | 440.0 | A4 tuning reference in Hz |
| `pitchBendRange_` | `float` | 2.0 | Pitch bend range in semitones [0, 24] |
| `smoothingTimeMs_` | `float` | 5.0 | Pitch bend smoothing time in ms |
| `velocityCurve_` | `VelocityCurve` | `Linear` | Active velocity curve type |
| `ampVelocityDepth_` | `float` | 1.0 | Amplitude velocity depth [0, 1] |
| `filterVelocityDepth_` | `float` | 0.0 | Filter velocity depth [0, 1] |
| `envTimeVelocityDepth_` | `float` | 0.0 | Envelope time velocity depth [0, 1] |
| `sampleRate_` | `float` | 44100.0 | Current sample rate |

#### State Transitions

```
                  [Construction]
                       |
                       v
             +----> IDLE <----+
             |       |        |
             |    prepare()   |
             |       |        |
             |       v        |
             |    READY ------+-- reset()
             |       |
             |  processPitchBend() (per block)
             |       |
             |       v
             |   PROCESSING
             |       |
             |  getFrequency(note) (per voice)
             |  mapVelocity(velocity) (per note-on)
             |       |
             +-------+
```

Note: States are conceptual. The class does not track an explicit state enum -- `prepare()` configures the smoother and the processor is ready immediately.

## Relationships

```
midi_utils.h (Layer 0)          pitch_utils.h (Layer 0)
  |                                |
  | midiNoteToFrequency()          | semitonesToRatio()
  | velocityToGain*()              |
  | VelocityCurve enum             |
  |                                |
  v                                v
  +---------> NoteProcessor <------+
  (Layer 2)        |
                   | uses
                   v
             smoother.h (Layer 1)
               OnePoleSmoother
```

## Validation Rules

| Input | Validation | Action |
|-------|-----------|--------|
| MIDI note (getFrequency) | uint8_t, inherently [0, 255] | Values 0-127 produce valid frequencies; values >127 produce high but finite frequencies |
| Pitch bend bipolar | float [-1.0, +1.0] | NaN/Inf ignored (FR-020); no explicit clamping (caller responsible for range) |
| A4 reference | float [400, 480] Hz | Finite out-of-range clamped; NaN/Inf reset to 440.0 Hz (FR-002) |
| Pitch bend range | float [0, 24] semitones | Clamped to valid range |
| Velocity | int [0, 127] | Clamped to [0, 127] (FR-016) |
| Velocity depths | float [0.0, 1.0] | Clamped to valid range |
| Smoothing time | float | Delegated to OnePoleSmoother configure (clamped internally) |
| Sample rate | double | Must be > 0; stored as float |
