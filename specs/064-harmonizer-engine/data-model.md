# Data Model: Multi-Voice Harmonizer Engine

**Date**: 2026-02-18 | **Spec**: [spec.md](spec.md) | **Plan**: [plan.md](plan.md)

## Entities

### HarmonyMode (Enum)

```cpp
/// Harmony intelligence mode selector.
enum class HarmonyMode : uint8_t {
    Chromatic = 0,   ///< Fixed semitone shift, no pitch tracking or scale awareness
    Scalic = 1,      ///< Diatonic interval in a configured key/scale, with pitch tracking
};
```

**Constraints**: Only two values. Future extensions (Chordal, MIDI) are anticipated but NOT implemented.

---

### Voice (Internal Struct)

```cpp
struct Voice {
    PitchShiftProcessor pitchShifter;   // L2: per-voice pitch shifting
    DelayLine           delayLine;       // L1: per-voice onset delay
    OnePoleSmoother     levelSmoother;   // L1: smooths gain changes (5ms)
    OnePoleSmoother     panSmoother;     // L1: smooths pan changes (5ms)
    OnePoleSmoother     pitchSmoother;   // L1: smooths semitone shift changes (10ms)

    // Configuration (set by public API, read in process)
    int   interval    = 0;       // diatonic steps (Scalic) or raw semitones (Chromatic)
    float levelDb     = 0.0f;    // output level in dB [-60, +6]
    float pan         = 0.0f;    // stereo position [-1.0, +1.0]
    float delayMs     = 0.0f;    // onset delay [0, 50] ms
    float detuneCents = 0.0f;    // micro-detuning [-50, +50] cents

    // Computed (derived from configuration + pitch tracking)
    float targetSemitones = 0.0f; // total semitone shift (interval + detune)
    float linearGain      = 1.0f; // dbToGain(levelDb), 0 if muted
    float delaySamples    = 0.0f; // delayMs * sampleRate / 1000
};
```

**Constraints**:
- `interval` range: [-24, +24]
- `levelDb` range: [-60, +6]. Values <= -60 dB are treated as mute (linearGain = 0)
- `pan` range: [-1.0, +1.0]
- `delayMs` range: [0, 50]
- `detuneCents` range: [-50, +50]
- `PitchShiftProcessor` is non-copyable. Voice struct inherits this constraint.
- All 4 voices are always allocated (pre-allocated in prepare()), even if numActiveVoices < 4.

---

### HarmonizerEngine (Primary Entity)

**Members**:

| Category | Member | Type | Description |
|----------|--------|------|-------------|
| **Shared Analysis** | `pitchTracker_` | `PitchTracker` | Shared pitch detection (Scalic mode only) |
| | `scaleHarmonizer_` | `ScaleHarmonizer` | Shared diatonic interval computation |
| **Voices** | `voices_` | `std::array<Voice, 4>` | 4 pre-allocated harmony voices |
| **Global Config** | `harmonyMode_` | `HarmonyMode` | Chromatic or Scalic |
| | `numActiveVoices_` | `int` | Number of active voices [0, 4] |
| | `pitchShiftMode_` | `PitchMode` | Current pitch shift algorithm |
| | `formantPreserve_` | `bool` | Formant preservation enabled |
| **Smoothers** | `dryLevelSmoother_` | `OnePoleSmoother` | Smooths dry level (10ms) |
| | `wetLevelSmoother_` | `OnePoleSmoother` | Smooths wet level (10ms) |
| **Scratch Buffers** | `delayScratch_` | `std::vector<float>` | Delayed input per voice (pre-allocated) |
| | `voiceScratch_` | `std::vector<float>` | Pitch-shifted voice output (pre-allocated) |
| **State** | `sampleRate_` | `double` | Current sample rate |
| | `maxBlockSize_` | `std::size_t` | Maximum block size |
| | `prepared_` | `bool` | Whether prepare() has been called |
| | `lastDetectedNote_` | `int` | Last valid MIDI note from PitchTracker |

---

## Relationships

```
HarmonizerEngine (1) ----owns----> PitchTracker (1)        [shared, Scalic mode]
HarmonizerEngine (1) ----owns----> ScaleHarmonizer (1)     [shared, Scalic mode]
HarmonizerEngine (1) ----owns----> Voice (4)               [pre-allocated array]
    Voice (1) ----owns----> PitchShiftProcessor (1)        [per-voice pitch shifting]
    Voice (1) ----owns----> DelayLine (1)                  [per-voice onset delay]
    Voice (1) ----owns----> OnePoleSmoother (3)            [level, pan, pitch]
HarmonizerEngine (1) ----owns----> OnePoleSmoother (2)     [dry level, wet level]
```

---

## State Transitions

### Lifecycle States

```
UNINITIALIZED  --prepare()-->  PREPARED  --reset()-->  PREPARED (state cleared)
     |                            |
     +--process()--> SILENCE     +--process()--> AUDIO OUTPUT
     (zero-fills output)         (normal processing)
```

- `UNINITIALIZED`: Default state after construction. `isPrepared()` returns false.
- `PREPARED`: After `prepare()`. All buffers allocated, components initialized. `isPrepared()` returns true.
- `process()` in UNINITIALIZED state: zero-fills outputL and outputR (FR-015 safe no-op).
- `reset()` clears processing state but keeps configuration and prepared state.

### Harmony Mode Transitions

```
CHROMATIC  --setHarmonyMode(Scalic)-->  SCALIC
    |                                       |
    |  No pitch tracking                    |  PitchTracker active
    |  Interval = raw semitones             |  Interval = diatonic steps
    |  PitchTracker NOT fed audio           |  PitchTracker fed per block
    |                                       |
SCALIC  --setHarmonyMode(Chromatic)-->  CHROMATIC
```

- Mode transitions are immediate (take effect on next process() call).
- Voice configuration (intervals, levels, pans) is preserved across mode changes.
- In Scalic mode, when PitchTracker reports invalid pitch, last valid intervals are held.

### Voice Activation

```
numVoices=0: Only dry signal, no voice processing, no pitch tracking
numVoices=1: Voice 0 active
numVoices=2: Voices 0-1 active
numVoices=3: Voices 0-2 active
numVoices=4: Voices 0-3 active (all)
```

- All 4 Voice structures are always allocated.
- Inactive voices are simply skipped in the processing loop.
- Increasing numVoices activates pre-existing voices with their current configuration.

---

## Validation Rules

| Parameter | Range | Clamping | Mute Behavior |
|-----------|-------|----------|---------------|
| `numVoices` | [0, 4] | Clamped | 0 = dry only |
| `voiceInterval` | [-24, +24] | Clamped | N/A |
| `voiceLevel` | [-60, +6] dB | Clamped | <= -60 dB = mute (gain = 0, skip processing) |
| `voicePan` | [-1.0, +1.0] | Clamped | N/A |
| `voiceDelay` | [0, 50] ms | Clamped | 0 = bypass delay line |
| `voiceDetune` | [-50, +50] cents | Clamped | N/A |
| `key` | [0, 11] | Wrapped (mod 12) | N/A |
| `scale` | ScaleType enum | N/A | N/A |
| `dryLevel` | dB (any) | dbToGain() | Negative infinity = 0 gain |
| `wetLevel` | dB (any) | dbToGain() | Negative infinity = 0 gain |

---

## Memory Layout

| Component | Per Voice | 4 Voices | Notes |
|-----------|-----------|----------|-------|
| PitchShiftProcessor (PhaseVocoder) | ~32 KB | ~128 KB | FFT buffers, phase arrays |
| PitchShiftProcessor (Simple) | ~20 KB | ~80 KB | Delay buffers |
| PitchShiftProcessor (PitchSync) | ~5 KB | ~20 KB | Adaptive delay |
| DelayLine (50ms @ 48kHz) | ~10 KB | ~40 KB | Power-of-2 circular buffer |
| OnePoleSmoother x3 | ~72 B | ~288 B | 24 bytes each (3 floats + 2 floats + pad) |
| Scratch buffers (2x maxBlockSize) | - | ~64 KB max | Shared, not per-voice |
| PitchTracker (shared) | - | ~4 KB | PitchDetector + median buffer |
| ScaleHarmonizer (shared) | - | ~8 B | Just rootNote_ + scale_ |
| Dry/Wet smoothers | - | ~48 B | 2 OnePoleSmoother instances |
| **Total (PhaseVocoder mode)** | **~42 KB** | **~236 KB** | All pre-allocated in prepare() |
| **Total (Simple mode)** | **~30 KB** | **~184 KB** | |
