# Data Model: Scale & Interval Foundation (ScaleHarmonizer)

**Date**: 2026-02-17 | **Spec**: 060-scale-interval-foundation

## Entities

### ScaleType (Enum)

An enumeration of the 9 supported scale/mode types.

| Value | Name | Semitone Offsets | Interval Pattern |
|-------|------|------------------|------------------|
| 0 | Major (Ionian) | {0, 2, 4, 5, 7, 9, 11} | W-W-H-W-W-W-H |
| 1 | NaturalMinor (Aeolian) | {0, 2, 3, 5, 7, 8, 10} | W-H-W-W-H-W-W |
| 2 | HarmonicMinor | {0, 2, 3, 5, 7, 8, 11} | W-H-W-W-H-A-H |
| 3 | MelodicMinor (ascending) | {0, 2, 3, 5, 7, 9, 11} | W-H-W-W-W-W-H |
| 4 | Dorian | {0, 2, 3, 5, 7, 9, 10} | W-H-W-W-W-H-W |
| 5 | Mixolydian | {0, 2, 4, 5, 7, 9, 10} | W-W-H-W-W-H-W |
| 6 | Phrygian | {0, 1, 3, 5, 7, 8, 10} | H-W-W-W-H-W-W |
| 7 | Lydian | {0, 2, 4, 6, 7, 9, 11} | W-W-W-H-W-W-H |
| 8 | Chromatic | N/A (passthrough) | All 12 semitones |

**Underlying type**: `uint8_t`
**Validation**: Values 0-8 are valid. No runtime validation needed (enum class provides type safety).

### DiatonicInterval (Struct)

The result of a diatonic interval calculation.

| Field | Type | Range | Description |
|-------|------|-------|-------------|
| `semitones` | `int` | -127 to +127 | The actual semitone shift from input to target |
| `targetNote` | `int` | 0 to 127 | The absolute MIDI note of the target (clamped) |
| `scaleDegree` | `int` | -1 to 6 | Target note's scale degree (0=root through 6=7th), -1 for chromatic mode |
| `octaveOffset` | `int` | any | Number of complete octaves traversed by the diatonic interval |

**Invariants**:
- `targetNote = clamp(inputMidiNote + semitones, 0, 127)`
- `semitones = targetNote - inputMidiNote` (after clamping)
- In Chromatic mode: `scaleDegree == -1` always
- In Chromatic mode: `octaveOffset == 0` always (diatonicSteps is a raw semitone count, not a diatonic interval, so octave traversal concepts do not apply)
- For diatonicSteps == 0: `semitones == 0`, `octaveOffset == 0`
- For diatonicSteps == +7: `semitones == +12`, `octaveOffset == 1`
- For diatonicSteps == -7: `semitones == -12`, `octaveOffset == -1`

> **Note**: The diatonicSteps invariants above (for +7 and -7) hold when the target note does not require MIDI boundary clamping. After clamping, `semitones = targetNote - inputMidiNote`, which may differ from the ideal diatonic shift.

### ScaleHarmonizer (Class)

The main calculator class. Configured with a root key and scale type.

**State**:

| Field | Type | Default | Range | Description |
|-------|------|---------|-------|-------------|
| `rootNote_` | `int` | 0 (C) | 0-11 | Root note of the key (0=C, 1=C#, ..., 11=B) |
| `scale_` | `ScaleType` | `Major` | 0-8 | Current scale type |

**Derived data** (constexpr, not stored):

| Data | Type | Description |
|------|------|-------------|
| Scale intervals | `std::array<int, 7>` | Semitone offsets for each of the 7 scale degrees, per scale type |
| Reverse lookup | `std::array<int, 12>` | Maps each pitch class offset (0-11) to nearest scale degree index (0-6) |

## Relationships

```
ScaleHarmonizer
    |-- uses --> ScaleType (configuration)
    |-- produces --> DiatonicInterval (result)
    |-- calls --> frequencyToMidiNote() [pitch_utils.h] (in getSemitoneShift)
    |-- uses --> kMinMidiNote, kMaxMidiNote [midi_utils.h] (for clamping)
```

## State Transitions

ScaleHarmonizer has two states:

1. **Default**: `rootNote_ = 0`, `scale_ = ScaleType::Major`
2. **Configured**: After `setKey()` and/or `setScale()` have been called

All query methods work in both states (default state is valid C Major).

There is no "initialized" vs "uninitialized" distinction -- the default constructor produces a valid, usable object.

## Validation Rules

| Rule | Enforcement |
|------|-------------|
| `rootNote` must be 0-11 | Modulo wrap in `setKey()`: `rootNote_ = rootNote % 12` |
| `scale` must be valid ScaleType | Enum class provides type safety |
| `inputMidiNote` in `calculate()` can be any int | Pitch class extracted via `% 12`, octave via `/ 12` |
| `diatonicSteps` can be any int | Handles multi-octave via `/ 7` and `% 7` |
| Target MIDI note clamped to 0-127 | `std::clamp(targetNote, 0, 127)` in calculate() |

## Internal Algorithm Data Structures

### Scale Interval Tables (constexpr)

```cpp
// Indexed by ScaleType (0-7), returns 7 semitone offsets
constexpr std::array<std::array<int, 7>, 8> kScaleIntervals = {{
    {0, 2, 4, 5, 7, 9, 11},  // Major
    {0, 2, 3, 5, 7, 8, 10},  // NaturalMinor
    {0, 2, 3, 5, 7, 8, 11},  // HarmonicMinor
    {0, 2, 3, 5, 7, 9, 11},  // MelodicMinor
    {0, 2, 3, 5, 7, 9, 10},  // Dorian
    {0, 2, 4, 5, 7, 9, 10},  // Mixolydian
    {0, 1, 3, 5, 7, 8, 10},  // Phrygian
    {0, 2, 4, 6, 7, 9, 11},  // Lydian
}};
```

### Reverse Lookup Tables (constexpr)

For each scale type, maps semitone offset (0-11) from root to nearest scale degree (0-6).
Built at compile time via constexpr function. Used for O(1) non-scale-note resolution.

```cpp
// Example for Major scale: {0, 2, 4, 5, 7, 9, 11}
// offset 0 -> degree 0 (is scale note C)
// offset 1 -> degree 0 (nearest to C, equidistant from C and D, round down)
// offset 2 -> degree 1 (is scale note D)
// offset 3 -> degree 1 (nearest to D: |3-2|=1 vs |3-4|=1, tie, round down to D)
// offset 4 -> degree 2 (is scale note E)
// offset 5 -> degree 3 (is scale note F)
// offset 6 -> degree 3 (nearest to F: |6-5|=1 vs |6-7|=1, tie, round down to F)
// offset 7 -> degree 4 (is scale note G)
// offset 8 -> degree 4 (nearest to G: |8-7|=1 vs |8-9|=1, tie, round down to G)
// offset 9 -> degree 5 (is scale note A)
// offset 10 -> degree 5 (nearest to A: |10-9|=1 vs |10-11|=1, tie, round down to A)
// offset 11 -> degree 6 (is scale note B)
constexpr std::array<int, 12> kMajorReverseLookup = {0, 0, 1, 1, 2, 3, 3, 4, 4, 5, 5, 6};
```
