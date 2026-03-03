# Research: Scale & Interval Foundation (ScaleHarmonizer)

**Date**: 2026-02-17 | **Spec**: 060-scale-interval-foundation

## Research Tasks

### R1: Diatonic Interval Calculation Algorithm

**Task**: Determine the correct algorithm for computing diatonic intervals that accounts for variable semitone shifts per scale degree.

**Decision**: Use a pitch-class-to-scale-degree lookup with modular arithmetic.

**Rationale**: The algorithm needs to:
1. Convert MIDI note to pitch class (0-11) via `midiNote % 12`
2. Compute the offset from root: `offset = (pitchClass - rootNote + 12) % 12`
3. Find the scale degree that matches this offset (or nearest, for non-scale notes)
4. Add the diatonic step count to get the target scale degree
5. Look up the target degree's semitone offset
6. Compute the semitone shift accounting for octave crossings

This is a standard approach used in commercial harmonizers (see DSP-HARMONIZER-RESEARCH.md Section 4.1). The key insight is that the scale intervals array maps degree index to semitone offset, so diatonic traversal is simply array index arithmetic with modular wrapping at 7.

**Alternatives considered**:
- **Lookup table (12x12 matrix per scale)**: Pre-compute all pitch-class-to-shift combinations. Rejected because it uses more memory (9 scales x 12 keys x 12 notes x ~20 interval steps = large table) and the algorithmic approach is simple enough to be O(1) with small constant factors.
- **Interval vector approach**: Store intervals between consecutive degrees (W-W-H-W-W-W-H) and accumulate. Rejected because the semitone-offset array is more direct for arbitrary jumps (no need to sum).

### R2: Non-Scale Note Handling (Nearest Scale Degree)

**Task**: Determine how to handle chromatic passing tones that are not in the current scale.

**Decision**: Use semitone distance to find the nearest scale degree, with round-down on ties.

**Rationale**: Build a reverse lookup array of 12 entries (one per pitch class offset from root) that maps each semitone to its nearest scale degree index. For positions exactly between two degrees, the lower degree is preferred per spec requirement FR-004.

This reverse lookup can be precomputed as a `constexpr` array for each scale type. The lookup is then O(1): given a pitch class offset, index into the 12-element array to get the nearest scale degree.

**Algorithm for building the reverse lookup**:
```
For each semitone offset s in [0, 11]:
    Find scale degree d that minimizes |scaleIntervals[d] - s|
    On tie, pick the lower degree (lower index)
    pitchClassToScaleDegree[s] = d
```

**Alternatives considered**:
- **Linear search at runtime**: Walk the 7-element scale array each call. Rejected because O(7) per call vs O(1) with precomputed table. Although 7 is small, this runs on the audio thread per sample.
- **Binary search**: The scale array is sorted, so binary search works. Rejected because the precomputed 12-element lookup is simpler and faster.

### R3: Octave Wrapping for Multi-Octave Intervals

**Task**: Determine correct octave handling for diatonic steps beyond +/-6 (multi-octave intervals).

**Decision**: Use integer division and modulo on diatonic steps.

**Rationale**: For diatonicSteps = N:
```
octaves = N / 7          (integer division, toward zero for C++)
remainingSteps = N % 7   (can be negative for negative N)
```
Then compute the semitone shift for `remainingSteps` and add `octaves * 12`.

For negative steps, C++ integer division truncates toward zero, so:
- `-9 / 7 = -1`, `-9 % 7 = -2` -- means go 1 octave down and then 2 more steps down

The remaining steps can be negative (for downward intervals), which means we traverse the scale downward from the input degree.

**Alternatives considered**:
- **Iterative step-by-step traversal**: Walk one degree at a time, wrapping at boundaries. Rejected because it is O(N) in the step count. The modular approach is O(1).

### R4: Downward (Negative) Interval Traversal

**Task**: Determine correct algorithm for traversing the scale downward.

**Decision**: Handle negative remaining steps by computing the target degree with modular arithmetic.

**Rationale**: Given input scale degree `d` and remaining steps `r` (where r can be negative):
```
targetDegree = (d + r % 7 + 7) % 7   // ensure positive modulo
```
Then compute semitone shift:
```
targetSemitoneOffset = scaleIntervals[targetDegree]
inputSemitoneOffset = scaleIntervals[d]
shift = targetSemitoneOffset - inputSemitoneOffset + octaves * 12
```
The `octaves` value must be adjusted when the target degree wraps around:
- If going up and targetDegree < inputDegree: add +12 (crossed octave boundary upward)
- If going down and targetDegree > inputDegree: subtract -12 (crossed octave boundary downward)

This is a clean mathematical formulation that handles all sign cases correctly.

### R5: MIDI Note Clamping at Boundaries

**Task**: Determine correct behavior when computed target notes exceed MIDI range (0-127).

**Decision**: Clamp the target MIDI note to [0, 127] and recompute the semitone shift from the clamped value.

**Rationale**: Per FR-009, target notes must be clamped. The DiatonicInterval result should reflect the actual achievable shift. So if input is MIDI 120 and the computed target is MIDI 132, clamp to 127 and report `semitones = 127 - 120 = 7` instead of `semitones = 12`.

The `kMinMidiNote` and `kMaxMidiNote` constants from `midi_utils.h` should be reused for the clamp bounds.

### R6: Scale Type Enum and Data Storage

**Task**: Determine the best way to store scale data (constexpr vs runtime tables).

**Decision**: Use `constexpr` arrays accessed via a `static constexpr` method on ScaleHarmonizer, plus a precomputed reverse-lookup table.

**Rationale**: All scale data is known at compile time. Using `constexpr` ensures:
1. Zero runtime initialization cost
2. Compiler can optimize away the function call entirely
3. The data can be used in constant expressions
4. No heap allocations (satisfies FR-014)

The reverse lookup (pitch class to nearest scale degree) can also be constexpr-computed.

**Alternatives considered**:
- **Runtime-computed tables in constructor**: Rejected because it adds unnecessary startup cost and prevents constexpr usage.
- **Separate data file**: Rejected because it would require I/O or linking complexity for 9 small arrays.

### R7: Dependency on Existing Components

**Task**: Determine which existing components to reuse and verify their APIs.

**Decision**: Reuse the following:
1. `frequencyToMidiNote()` from `pitch_utils.h` -- for the `getSemitoneShift()` convenience method
2. `kMinMidiNote` / `kMaxMidiNote` from `midi_utils.h` -- for MIDI note clamping

**Rationale**: These are stable, tested Layer 0 utilities that provide exactly what is needed.

**API Verification**:
- `frequencyToMidiNote(float hz) noexcept` returns `float` (continuous MIDI note). Spec says to round to nearest int. Verified at `pitch_utils.h:126-129`.
- `kMinMidiNote = 0`, `kMaxMidiNote = 127` from `midi_utils.h:38,42`.

### R8: Header-Only vs Compilation Unit

**Task**: Determine if this should be header-only or split header/cpp.

**Decision**: Header-only with all methods defined inline in the header.

**Rationale**:
1. The spec explicitly states "Header-only at `dsp/include/krate/dsp/core/scale_harmonizer.h`"
2. All operations are small, branchless, and suitable for inlining
3. All data is constexpr, no static storage needed
4. Consistent with other Layer 0 components (pitch_utils.h, db_utils.h are header-only)
5. Enables compiler to inline and optimize the hot path

### R9: Thread Safety Model

**Task**: Determine the thread safety model for ScaleHarmonizer.

**Decision**: Immutable-after-configuration model: `setKey()` and `setScale()` are called from the UI/parameter thread; all query methods (`calculate()`, `getScaleDegree()`, etc.) are safe to call concurrently from the audio thread without synchronization.

**Rationale**: Per FR-015, the component must be immutable after configuration. The internal state is just two POD fields (`rootNote_` as `int`, `scale_` as `ScaleType` enum). Since `calculate()` only reads these fields and all scale data is constexpr, there are no data races when reads and writes do not overlap (which is guaranteed by the VST3 parameter change mechanism -- parameter changes are applied between process blocks).

No atomic operations needed. The host ensures `setKey`/`setScale` are not called while `process()` is running.
