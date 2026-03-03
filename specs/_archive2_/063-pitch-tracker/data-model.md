# Data Model: Pitch Tracking Robustness (063)

**Date**: 2026-02-18

## Entities

### PitchTracker

**Layer**: 1 (Primitives)
**Location**: `dsp/include/krate/dsp/primitives/pitch_tracker.h`
**Namespace**: `Krate::DSP`

#### Fields

| Field | Type | Default | Description |
|-------|------|---------|-------------|
| `detector_` | `PitchDetector` | default-constructed | Wrapped pitch detection engine |
| `pitchHistory_` | `std::array<float, kMaxMedianSize>` | `{}` | Ring buffer of confident pitch values for median filter |
| `medianSize_` | `std::size_t` | `5` | Active median filter window size (1-11) |
| `historyIndex_` | `std::size_t` | `0` | Write position in ring buffer |
| `historyCount_` | `std::size_t` | `0` | Number of valid entries in ring buffer (capped at medianSize_) |
| `currentNote_` | `int` | `-1` | Currently committed MIDI note (-1 = no committed note) |
| `hysteresisThreshold_` | `float` | `50.0f` | Hysteresis threshold in cents |
| `confidenceThreshold_` | `float` | `0.5f` | Minimum confidence for pitch acceptance |
| `pitchValid_` | `bool` | `false` | Whether last detection frame passed confidence gate |
| `minNoteDurationMs_` | `float` | `50.0f` | Minimum note duration in milliseconds |
| `noteHoldTimer_` | `std::size_t` | `0` | Timer counting samples candidate has been stable |
| `minNoteDurationSamples_` | `std::size_t` | `0` | Duration threshold in samples (computed in prepare) |
| `candidateNote_` | `int` | `-1` | Proposed new MIDI note (-1 = no active candidate) |
| `sampleRate_` | `double` | `44100.0` | Current sample rate |
| `frequencySmoother_` | `OnePoleSmoother` | default-constructed | Exponential smoother for frequency output |
| `smoothedFrequency_` | `float` | `0.0f` | Last smoothed frequency value |
| `hopSize_` | `std::size_t` | `64` | Samples between pipeline executions (windowSize/4) |
| `samplesSinceLastHop_` | `std::size_t` | `0` | Counter for hop alignment with PitchDetector |
| `windowSize_` | `std::size_t` | `256` | Analysis window size stored for hop calculation |

#### Constants

| Constant | Type | Value | Description |
|----------|------|-------|-------------|
| `kDefaultWindowSize` | `std::size_t` | `256` | Default analysis window size |
| `kMaxMedianSize` | `std::size_t` | `11` | Maximum median filter window |
| `kDefaultHysteresisThreshold` | `float` | `50.0f` | Default hysteresis in cents |
| `kDefaultConfidenceThreshold` | `float` | `0.5f` | Default confidence gate |
| `kDefaultMinNoteDurationMs` | `float` | `50.0f` | Default min note duration |
| `kDefaultFrequencySmoothingMs` | `float` | `25.0f` | Default smoother time constant |

#### State Machine

```
                  first confident detection
   [NO NOTE] ──────────────────────────────> [TRACKING]
   currentNote_=-1                            currentNote_>=0
                                              candidateNote_=-1
                                                    │
                          hysteresis exceeded       │
                    ┌───────────────────────────────┘
                    │
                    v
              [CANDIDATE PENDING]
              currentNote_>=0
              candidateNote_>=0
              noteHoldTimer_ counting
                    │
          ┌─────────┼──────────┐
          │         │          │
          v         v          v
     timer expires  candidate   candidate matches
     commit new     changes     committed note
     note           reset       cancel candidate
          │         timer       │
          v         │          v
     [TRACKING]     └──>[CANDIDATE  [TRACKING]
                        PENDING]
```

### Dependencies (Read-Only, Not Owned)

| Component | Location | API Used |
|-----------|----------|----------|
| `PitchDetector` | `primitives/pitch_detector.h` | `prepare()`, `reset()`, `push()`, `getDetectedFrequency()`, `getConfidence()` |
| `OnePoleSmoother` | `primitives/smoother.h` | `configure()`, `setTarget()`, `advanceSamples()`, `getCurrentValue()`, `snapTo()`, `reset()` |
| `frequencyToMidiNote()` | `core/pitch_utils.h` | `frequencyToMidiNote(hz)` -> continuous MIDI note |
| `midiNoteToFrequency()` | `core/midi_utils.h` | `midiNoteToFrequency(note)` -> Hz center of note |

### Validation Rules

| Rule | Applies To | Constraint |
|------|-----------|------------|
| Range | `medianSize_` | Must be 1-11, odd preferred but not required |
| Range | `confidenceThreshold_` | Must be 0.0-1.0 |
| Range | `hysteresisThreshold_` | Must be >= 0.0 |
| Range | `minNoteDurationMs_` | Must be >= 0.0 |
| Invariant | `historyCount_` | Never exceeds `medianSize_` |
| Invariant | `historyIndex_` | Always < `kMaxMedianSize` |
| Invariant | Processing path | Zero heap allocations |
| Invariant | All methods | `noexcept` |

### Relationships

```
PitchTracker (Layer 1)
    ├── contains: PitchDetector (Layer 1)     [composition, owns]
    ├── contains: OnePoleSmoother (Layer 1)    [composition, owns]
    ├── uses: frequencyToMidiNote() (Layer 0)  [free function]
    └── uses: midiNoteToFrequency() (Layer 0)  [free function]
```

### Memory Layout (Approximate)

- `PitchDetector`: ~112 bytes + heap vectors allocated in prepare()
- `OnePoleSmoother`: ~20 bytes
- `pitchHistory_`: 44 bytes (11 floats)
- Scalar fields: ~40 bytes
- **Total PitchTracker**: ~216 bytes stack + PitchDetector heap allocations from prepare()
