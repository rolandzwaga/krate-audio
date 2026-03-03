# Feature Specification: Scale & Interval Foundation (ScaleHarmonizer)

**Feature Branch**: `060-scale-interval-foundation`
**Plugin**: KrateDSP (Shared DSP Library)
**Created**: 2026-02-17
**Status**: Complete
**Input**: User description: "ScaleHarmonizer class -- a Layer 0 (Core) component in the KrateDSP shared library that computes diatonic intervals for a harmonizer effect. Given an input note, a key/scale, and a desired diatonic interval (e.g., '3rd above'), compute the correct semitone shift."

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Compute Diatonic Interval for a Scale Degree (Priority: P1)

A plugin developer building a harmonizer effect needs to determine the correct semitone shift for a given input note, key, scale, and desired diatonic interval. For example: "Input note D in C Major, harmony = 3rd above" must return +3 semitones (to F), while "Input note C in C Major, harmony = 3rd above" must return +4 semitones (to E). The shift varies per input note to maintain scale-correctness -- this is the fundamental distinction between a harmonizer and a simple pitch shifter.

**Why this priority**: This is the core purpose of the entire component. Without correct diatonic interval calculation, no harmonizer feature can function. Every other user story depends on this capability.

**Independent Test**: Can be fully tested by providing known input MIDI notes, a key/scale configuration, and diatonic step values, then verifying the returned semitone shifts against a music theory reference table. Delivers the musical intelligence required for any scale-aware harmonizer voice.

**Acceptance Scenarios**:

1. **Given** a ScaleHarmonizer configured for C Major, **When** calculating a 3rd above (diatonicSteps = +2) for input MIDI note 60 (C4), **Then** the result is +4 semitones (target note E4, MIDI 64, scale degree 2).
2. **Given** a ScaleHarmonizer configured for C Major, **When** calculating a 3rd above (diatonicSteps = +2) for input MIDI note 62 (D4), **Then** the result is +3 semitones (target note F4, MIDI 65, scale degree 3).
3. **Given** a ScaleHarmonizer configured for C Major, **When** calculating a 3rd above for all seven scale degrees (C, D, E, F, G, A, B), **Then** the results match: C->E(+4), D->F(+3), E->G(+3), F->A(+4), G->B(+4), A->C(+3), B->D(+3).
4. **Given** a ScaleHarmonizer configured for any key and any of the 8 supported scale types, **When** calculating any diatonic interval, **Then** the result is musically correct according to the scale's interval structure.

---

### User Story 2 - Handle Non-Scale (Chromatic Passing) Notes (Priority: P2)

When the input note does not belong to the current scale (e.g., C# in C Major), the system must still produce a musically reasonable interval by treating the note as if it were the nearest scale degree. This handles real-world scenarios where singers or instruments play chromatic passing tones, grace notes, or blue notes that fall between scale degrees.

**Why this priority**: Real audio input is rarely perfectly on-scale. Without robust handling of non-scale notes, the harmonizer would produce undefined or musically wrong results for a significant portion of real-world input.

**Independent Test**: Can be tested by providing chromatic (non-scale) MIDI notes to a configured ScaleHarmonizer and verifying the returned interval matches the nearest scale degree's interval.

**Acceptance Scenarios**:

1. **Given** a ScaleHarmonizer configured for C Major, **When** calculating a 3rd above for input MIDI note 61 (C#4, not in C Major), **Then** the system uses the nearest scale degree (C) and returns the same interval as for C (+4 semitones).
2. **Given** a ScaleHarmonizer configured for C Major, **When** calculating a 3rd above for input MIDI note 63 (Eb4, not in C Major), **Then** the system uses the nearest scale degree (D or E, whichever is closer by semitone distance) and returns that degree's interval.
3. **Given** any chromatic note between two scale degrees equidistant from both, **When** calculating any interval, **Then** the system deterministically rounds down (toward the lower scale degree) to ensure consistent behavior.

---

### User Story 3 - Chromatic (Fixed Shift) Mode (Priority: P3)

A plugin developer needs a bypass mode where the diatonic step value is interpreted as raw semitones rather than scale degrees. This enables the harmonizer to operate as a simple pitch shifter when scale awareness is not desired, using the same component interface.

**Why this priority**: Chromatic mode provides a simpler, lower-overhead operating mode and is essential for the HarmonizerEngine's Chromatic harmony mode (Phase 4). It is lower priority because it requires no scale logic -- it is a passthrough behavior.

**Independent Test**: Can be tested by configuring ScaleType::Chromatic and verifying that diatonicSteps is returned directly as the semitone shift, regardless of input note or key.

**Acceptance Scenarios**:

1. **Given** a ScaleHarmonizer configured for Chromatic mode, **When** calculating with diatonicSteps = +7 for any input MIDI note, **Then** the result is always +7 semitones (a perfect fifth).
2. **Given** a ScaleHarmonizer configured for Chromatic mode, **When** calculating with diatonicSteps = -5 for any input MIDI note, **Then** the result is always -5 semitones.
3. **Given** a ScaleHarmonizer in Chromatic mode, **When** changing the key (rootNote), **Then** the key setting has no effect on the computed interval.

---

### User Story 4 - Query Scale Membership and Quantization (Priority: P4)

A plugin developer or downstream component (such as a PitchTracker or UI pitch display) needs to query whether a given MIDI note belongs to the current scale, and to quantize arbitrary MIDI notes to the nearest scale degree. This supports pitch display, visual feedback, and pre-processing before interval calculation.

**Why this priority**: These are utility queries that support UI feedback and downstream components but are not strictly required for the core interval calculation.

**Independent Test**: Can be tested by querying scale degree membership for all 12 pitch classes in a given key/scale, and by verifying quantization snaps chromatic notes to the nearest scale degree.

**Acceptance Scenarios**:

1. **Given** a ScaleHarmonizer configured for C Major, **When** querying the scale degree of MIDI note 60 (C4), **Then** the result is 0 (the root).
2. **Given** a ScaleHarmonizer configured for C Major, **When** querying the scale degree of MIDI note 61 (C#4), **Then** the result is -1 (not in scale).
3. **Given** a ScaleHarmonizer configured for C Major, **When** quantizing MIDI note 61 (C#4) to the scale, **Then** the result is MIDI note 60 (C4) or 62 (D4), whichever is nearer.

---

### User Story 5 - Support All 8 Scale Types Plus Chromatic (Priority: P5)

The component must support all 8 diatonic scale types commonly used in Western music production (Major, Natural Minor, Harmonic Minor, Melodic Minor, Dorian, Mixolydian, Phrygian, Lydian) plus a Chromatic passthrough mode. Each scale must be stored as its correct set of 7 semitone offsets from the root.

**Why this priority**: Multiple scale types are essential for musical versatility but the core algorithm is the same for all scales -- only the lookup table differs. This is a completeness requirement.

**Independent Test**: Can be tested by verifying that each scale type returns the correct 7 semitone offsets from the root, cross-referenced against music theory.

**Acceptance Scenarios**:

1. **Given** ScaleType::Major, **When** querying scale intervals, **Then** the result is {0, 2, 4, 5, 7, 9, 11}.
2. **Given** ScaleType::NaturalMinor, **When** querying scale intervals, **Then** the result is {0, 2, 3, 5, 7, 8, 10}.
3. **Given** ScaleType::HarmonicMinor, **When** querying scale intervals, **Then** the result is {0, 2, 3, 5, 7, 8, 11}.
4. **Given** ScaleType::MelodicMinor, **When** querying scale intervals, **Then** the result is {0, 2, 3, 5, 7, 9, 11}.
5. **Given** ScaleType::Dorian, **When** querying scale intervals, **Then** the result is {0, 2, 3, 5, 7, 9, 10}.
6. **Given** ScaleType::Mixolydian, **When** querying scale intervals, **Then** the result is {0, 2, 4, 5, 7, 9, 10}.
7. **Given** ScaleType::Phrygian, **When** querying scale intervals, **Then** the result is {0, 1, 3, 5, 7, 8, 10}.
8. **Given** ScaleType::Lydian, **When** querying scale intervals, **Then** the result is {0, 2, 4, 6, 7, 9, 11}.

---

### User Story 6 - Negative Intervals (Harmony Below) (Priority: P6)

A plugin developer needs to compute harmonies below the input note (e.g., "3rd below"). Negative diatonic steps must traverse the scale downward, correctly handling octave boundaries.

**Why this priority**: Harmony below is a common musical use case but is functionally the inverse of harmony above, using the same core algorithm with negative traversal.

**Independent Test**: Can be tested by providing negative diatonicSteps and verifying the result against known descending intervals.

**Acceptance Scenarios**:

1. **Given** a ScaleHarmonizer configured for C Major, **When** calculating diatonicSteps = -2 (3rd below) for input MIDI note 64 (E4), **Then** the result is -4 semitones (target note C4, MIDI 60).
2. **Given** a ScaleHarmonizer configured for C Major, **When** calculating diatonicSteps = -2 (3rd below) for input MIDI note 60 (C4), **Then** the result is -3 semitones (target note A3, MIDI 57), correctly wrapping below the octave.
3. **Given** a ScaleHarmonizer configured for C Major, **When** calculating diatonicSteps = -7 (octave below) for input MIDI note 72 (C5), **Then** the result is -12 semitones (target note C4, MIDI 60).

---

### Edge Cases

- What happens when the input MIDI note is 0 (lowest possible) and a negative interval is requested? The target note may be below the valid MIDI range (0-127). The system must clamp the target note to the valid range. After clamping, the semitones field is recomputed as targetNote − inputMidiNote to reflect the achievable shift.
- What happens when the input MIDI note is 127 (highest possible) and a large positive interval is requested? The target note may exceed the valid MIDI range. The system must clamp the target note to 127. After clamping, the semitones field is recomputed as targetNote − inputMidiNote to reflect the achievable shift.
- What happens when diatonicSteps is 0 (unison)? The result must be 0 semitones (no shift).
- What happens when diatonicSteps is +7 or -7 (full octave in a 7-note scale)? The result must be exactly +12 or -12 semitones.
- What happens when diatonicSteps exceeds +/-7 (multi-octave intervals, e.g., +9 = 2nd in next octave)? The system must correctly compute the interval across multiple octaves.
- How does the system handle all 12 root keys (not just C)? The algorithm must be transposition-invariant -- setting root to any value (0-11) must shift the scale pattern accordingly.

## Clarifications

### Session 2026-02-17

- Q: For non-scale notes equidistant between two scale degrees (e.g., C# in C Major, which is 1 semitone from both C and D), which distance metric should determine the "nearest" scale degree? → A: Semitone distance (chromatic), round down when equidistant
- Q: When calculate() is called with a non-scale input note (e.g., C# in C Major treated as C, then shifted +2 to E), what should DiatonicInterval.scaleDegree contain? → A: Return the target scale degree after interval shift (e.g., 2 for E)
- Q: What does octaveOffset represent when an interval crosses octave boundaries (e.g., C4 + 7th above → B4 vs C4 + octave → C5 vs C4 + 9th → D5)? → A: Number of complete octaves traversed by the diatonic interval
- Q: In Chromatic mode, what should DiatonicInterval.scaleDegree be when there are no scale degrees (e.g., input MIDI 60, diatonicSteps = +7)? → A: Always -1 (indicating "not applicable" / no scale)
- Q: In Chromatic mode, what should DiatonicInterval.octaveOffset be? → A: Always 0. In Chromatic mode, diatonicSteps is a raw semitone count, not a diatonic interval, so octave traversal concepts do not apply.
- Q: How should getSemitoneShift() handle fractional MIDI notes from frequencyToMidiNote() (e.g., 440.5 Hz → MIDI 69.019...) before calling calculate()? → A: Round to nearest integer MIDI note

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The component MUST compute the correct semitone shift for any diatonic interval (positive, negative, or zero) given an input MIDI note, root key, and scale type. The shift must be musically correct according to the scale's interval structure.
- **FR-002**: The component MUST support all 8 diatonic scale types with the following semitone offsets from root:
  - Major (Ionian): {0, 2, 4, 5, 7, 9, 11}
  - Natural Minor (Aeolian): {0, 2, 3, 5, 7, 8, 10}
  - Harmonic Minor: {0, 2, 3, 5, 7, 8, 11}
  - Melodic Minor (ascending): {0, 2, 3, 5, 7, 9, 11}
  - Dorian: {0, 2, 3, 5, 7, 9, 10}
  - Mixolydian: {0, 2, 4, 5, 7, 9, 10}
  - Phrygian: {0, 1, 3, 5, 7, 8, 10}
  - Lydian: {0, 2, 4, 6, 7, 9, 11}
- **FR-003**: The component MUST support a Chromatic mode where diatonicSteps is interpreted as raw semitones with no scale logic applied, regardless of key or input note. In this mode, the scaleDegree field MUST be set to -1 to indicate "not applicable".
- **FR-004**: For input notes that do not belong to the current scale (chromatic passing tones), the component MUST use the interval of the nearest scale degree. When equidistant between two scale degrees, the component MUST round down (toward the lower degree) for deterministic behavior.
- **FR-005**: The component MUST support all 12 root keys (0=C through 11=B). The algorithm must be transposition-invariant.
- **FR-006**: The component MUST return a result structure containing: the semitone shift, the absolute target MIDI note, the target scale degree (0-6 for the target note's scale position in diatonic mode, -1 in Chromatic mode), and the octave offset (number of complete octaves traversed by the diatonic interval). For non-scale input notes, scaleDegree represents the target note's scale degree after interval calculation.
- **FR-007**: The component MUST correctly handle octave wrapping for intervals that cross octave boundaries, both upward and downward.
- **FR-008**: The component MUST correctly handle multi-octave intervals (diatonicSteps beyond +/-6), computing the correct number of octave wraps plus the remaining scale degrees.
- **FR-009**: The component MUST clamp target MIDI notes to the valid range (0-127) when the computed target would fall outside this range.
- **FR-010**: The component MUST provide a method to query the scale degree (0-6) of any MIDI note, returning -1 for notes not in the current scale.
- **FR-011**: The component MUST provide a method to quantize any MIDI note to the nearest scale degree in the current key/scale.
- **FR-012**: The component MUST provide a convenience method to compute the semitone shift directly from an input frequency (in Hz) and diatonic steps, using the existing `frequencyToMidiNote()` utility. Fractional MIDI note values MUST be rounded to the nearest integer before interval calculation. The return type is float (not int) to accommodate potential future sub-semitone adjustments; for the current implementation, the value is always a whole number.
- **FR-013**: The component MUST provide a static constexpr method to retrieve the 7 semitone offsets for any scale type. For `ScaleType::Chromatic`, `getScaleIntervals()` returns `{0, 1, 2, 3, 4, 5, 6}` as a degenerate 7-element subset (not meaningful for interval calculation).
- **FR-014**: All methods MUST be noexcept and perform zero heap allocations, making them safe for use on a real-time audio thread.
- **FR-015**: The component MUST be immutable after configuration (setKey/setScale), making it safe for concurrent reads from the audio thread without synchronization.
- **FR-016**: The component MUST reside in Layer 0 (Core) of the DSP architecture, depending only on the standard library and other Layer 0 utilities.

### Key Entities

- **ScaleType**: An enumeration of the 9 supported scale/mode types (8 diatonic + Chromatic). Each diatonic type maps to a fixed array of 7 semitone offsets from the root.
- **DiatonicInterval**: A result structure representing a computed interval. Contains the semitone shift, absolute target MIDI note, target scale degree (0-6 for the target note's scale position, or -1 in chromatic mode), and octave offset (number of complete octaves traversed by the diatonic interval).
- **ScaleHarmonizer**: The main calculator class. Configured with a root key (0-11) and scale type, it computes diatonic intervals for any input MIDI note. Stateless after configuration -- no per-call side effects.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: All 7 scale degrees in C Major produce the correct 3rd-above intervals matching the reference table: C->E(+4), D->F(+3), E->G(+3), F->A(+4), G->B(+4), A->C(+3), B->D(+3). This table is verified against standard music theory.
- **SC-002**: All 8 diatonic scale types pass exhaustive correctness tests covering 2nd, 3rd, 5th, and octave intervals for all 12 root keys (8 scales x 12 keys x 4 interval types x 7 degrees = 2,688 test cases, all correct).
- **SC-003**: Non-scale input notes produce the same interval as their nearest scale degree for 100% of chromatic passing tone test cases.
- **SC-004**: Negative intervals (harmony below) produce musically correct results for all test cases, including octave boundary wrapping.
- **SC-005**: Chromatic mode returns the diatonicSteps value directly as the semitone shift for 100% of test cases, regardless of input note or key.
- **SC-006**: Multi-octave intervals (diatonicSteps = +9, +14, -9, etc.) produce correct results with proper octave offset computation.
- **SC-007**: MIDI note boundary clamping correctly handles target notes that would fall below 0 or above 127.
- **SC-008**: The component introduces zero heap allocations and all methods are noexcept, as verified by static analysis or inspection.

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- All scale types use 12-tone equal temperament (12-TET). Microtonal or non-Western tuning systems are out of scope.
- The Melodic Minor scale uses the ascending form only ({0, 2, 3, 5, 7, 9, 11}). The descending melodic minor (which reverts to natural minor) is not modeled as a separate behavior -- the ascending form is used for both directions, which is the standard approach in harmonizers and electronic music contexts.
- MIDI note numbers follow the standard convention where middle C = MIDI 60, A4 = MIDI 69 (440 Hz).
- The "nearest scale degree" for non-scale notes uses semitone distance (not diatonic distance). When equidistant, the lower degree is preferred for deterministic behavior.
- This component performs pure computation -- it does not produce audio, manage buffers, or interact with the audio processing chain. It is used by downstream components (e.g., HarmonizerEngine in Phase 4) to determine pitch shift amounts.
- The `diatonicSteps` parameter uses the convention where +1 = "2nd above" (one scale step up), +2 = "3rd above" (two scale steps up), +6 = "7th above", +7 = "octave above". This follows standard diatonic interval naming where the interval number is diatonicSteps + 1.

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| `frequencyToMidiNote()` | `dsp/include/krate/dsp/core/pitch_utils.h` | **Should reuse** -- converts Hz to continuous MIDI note. Used by `getSemitoneShift()` convenience method. |
| `frequencyToNoteClass()` | `dsp/include/krate/dsp/core/pitch_utils.h` | **Should reuse** -- extracts pitch class (0-11) from frequency. Useful for scale degree lookup from Hz input. |
| `semitonesToRatio()` | `dsp/include/krate/dsp/core/pitch_utils.h` | **Reference** -- downstream consumers will use this to convert ScaleHarmonizer's semitone output to pitch shift ratios. Not directly used by ScaleHarmonizer. |
| `quantizePitch()` (PitchQuantMode::Scale) | `dsp/include/krate/dsp/core/pitch_utils.h` | **Partial overlap** -- existing function snaps to major scale degrees but only supports C Major (hardcoded root=0) and does not compute intervals. ScaleHarmonizer supersedes this for harmonizer use cases. The existing function should NOT be modified or removed, as it serves a different purpose (pitch quantization for the Shimmer effect's pitch shifter). |
| `PitchQuantMode` enum | `dsp/include/krate/dsp/core/pitch_utils.h` | **No conflict** -- this enum controls pitch quantization modes. ScaleHarmonizer introduces a separate `ScaleType` enum for scale selection, which is semantically distinct. |

**Initial codebase search for key terms:**

Searched for `ScaleHarmonizer`, `ScaleType`, `DiatonicInterval`, and `scale_harmonizer` across all code files. Results: No existing implementations found in source code. All matches are in specification/research documents only (`specs/harmonizer-roadmap.md`, `specs/DSP-HARMONIZER-RESEARCH.md`).

**Search Results Summary**: No existing `ScaleHarmonizer`, `ScaleType`, or `DiatonicInterval` types exist in the codebase. The file path `dsp/include/krate/dsp/core/scale_harmonizer.h` is unused. No ODR risk.

### Forward Reusability Consideration

*This is a Layer 0 (Core) component, making it available to all higher layers.*

**Components that will consume ScaleHarmonizer (from harmonizer roadmap):**
- HarmonizerEngine (Layer 3, Phase 4) -- primary consumer for per-voice diatonic interval computation
- Any future pitch-aware effect that needs scale-correct transposition

**Potential shared components** (preliminary, refined in plan.md):
- The `ScaleType` enum and scale interval tables could potentially be reused by a future "Scale Quantize" processor or a chord recognition system
- The `quantizeToScale()` method could serve as a more capable replacement for the existing `quantizePitch(PitchQuantMode::Scale)` in contexts where configurable key/scale is needed

## Implementation Verification *(mandatory at completion)*

<!--
  This section is EMPTY during specification phase and filled during
  implementation phase when /speckit.implement completes.
-->

### Compliance Status

*For EACH row below, you MUST perform these steps before writing the status:*
1. *Re-read the requirement from the spec*
2. *Open the implementation file and find the code that satisfies it -- record the file path and line number*
3. *Run or read the test that proves it -- record the test name and its actual output/result*
4. *For numeric thresholds (SC-xxx): record the actual measured value vs the spec target*
5. *Only then write the status and evidence*

*DO NOT mark as MET without having just verified the code and test output. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | MET | `scale_harmonizer.h:211-285` -- `calculate()` computes correct semitone shifts for positive, negative, zero intervals. Tests: "C Major 3rd above" (7 cases), "exhaustive multi-scale" (2688 cases), "negative intervals" (72 assertions). |
| FR-002 | MET | `scale_harmonizer.h:80-89` -- `kScaleIntervals` constexpr table, all 8 scales match spec. Test: "getScaleIntervals exhaustive truth table" (373 assertions, 9 scales verified). |
| FR-003 | MET | `scale_harmonizer.h:213-221` -- Chromatic branch returns diatonicSteps directly, scaleDegree=-1, octaveOffset=0. Tests: 259 assertions in 2 US3 test cases. |
| FR-004 | MET | `scale_harmonizer.h:97-124` -- `buildReverseLookup()` iterates degrees 0-6, `distance < bestDistance` keeps lower degree on ties. Test: "tie-breaking equidistant notes round DOWN" (26 assertions). |
| FR-005 | MET | `scale_harmonizer.h:170-172` -- `setKey()` uses `((rootNote % 12) + 12) % 12`. Test: "exhaustive multi-scale" covers all 12 root keys (2688 cases). |
| FR-006 | MET | `scale_harmonizer.h:65-70` -- `DiatonicInterval{semitones, targetNote, scaleDegree, octaveOffset}`. All 4 fields verified across all test cases. Chromatic scaleDegree=-1 verified. |
| FR-007 | MET | `scale_harmonizer.h:248-264` -- Octave wrapping via `totalDegree / 7` and `% 7`. Tests: "octave wrapping" (positive), "octave-exact negative" (both directions verified). |
| FR-008 | MET | `scale_harmonizer.h:254-264` -- Multi-octave via integer division. Tests verify diatonicSteps=+8, +9, +14, -9, -14 with correct octaveOffset. |
| FR-009 | MET | `scale_harmonizer.h:273-277` -- `std::clamp(targetNote, kMinMidiNote, kMaxMidiNote)` + semitone recomputation. Test: "MIDI boundary clamping" (1773 assertions). |
| FR-010 | MET | `scale_harmonizer.h:314-327` -- `getScaleDegree()` returns 0-6 for scale notes, -1 for non-scale/chromatic. Test: "getScaleDegree" (107 assertions, all 12 pitch classes, multiple keys). |
| FR-011 | MET | `scale_harmonizer.h:338-350` -- `quantizeToScale()` uses reverse lookup + snap offset. Chromatic returns input unchanged. Test: "quantizeToScale" (107 assertions). |
| FR-012 | MET | `scale_harmonizer.h:300-303` -- `getSemitoneShift()` calls `frequencyToMidiNote()`, rounds via `std::round()`. Test: "getSemitoneShift" (6 sections: exact/fractional/boundary frequencies). |
| FR-013 | MET | `scale_harmonizer.h:357-362` -- `getScaleIntervals()` is `static constexpr`. Chromatic returns {0,1,2,3,4,5,6}. Verified constexpr via `static_assert`. Test: truth table for all 9 types. |
| FR-014 | MET | All 9 public methods marked `noexcept`. Test: 9 `static_assert(noexcept(...))` compile-time checks. Zero heap allocations: no `new/delete/malloc/vector/string` in implementation. |
| FR-015 | MET | All query methods are `const noexcept`. No `mutable` members, no lazy caches. Thread-safety rationale documented in test file (lines 13-33). |
| FR-016 | MET | Includes only: `<algorithm>`, `<array>`, `<cmath>`, `<cstdint>` (stdlib) + `midi_utils.h`, `pitch_utils.h` (Layer 0). No Layer 1+ headers. Documented in test file (lines 36-52). |
| SC-001 | MET | Test "C Major 3rd above": C->E(+4), D->F(+3), E->G(+3), F->A(+4), G->B(+4), A->C(+3), B->D(+3) -- exact match to reference table. |
| SC-002 | MET | Test "exhaustive multi-scale": 8 scales x 12 keys x 4 intervals x 7 degrees = 2688 cases, asserted via `REQUIRE(totalCases == 2688)`. All pass. |
| SC-003 | MET | Test "non-scale notes": all 5 chromatic passing tones in C Major produce same interval as nearest scale degree. 100% correct. |
| SC-004 | MET | Test "negative intervals": E4-2->C4(-4), C4-2->A3(-3 with octave wrap), all 7 degrees 3rd below, octave-exact -7=-12. All correct. |
| SC-005 | MET | Test "Chromatic mode": +7 always +7, -5 always -5, key independent, steps -12 to +12 all passthrough. 100% correct. |
| SC-006 | MET | Test "multi-octave negative": steps -9, -14 produce correct results with proper octaveOffset. Positive +9, +14 also verified. |
| SC-007 | MET | Test "MIDI boundary clamping": MIDI 127+octave clamps to 127, MIDI 0-octave clamps to 0, semitones recomputed. |
| SC-008 | MET | 9 `static_assert(noexcept(...))` checks pass. Code inspection: zero allocating containers, no `new`/`delete`/`malloc`. |

**Status Key:**
- MET: Requirement verified against actual code and test output with specific evidence
- NOT MET: Requirement not satisfied (spec is NOT complete)
- PARTIAL: Partially met with documented gap and specific evidence of what IS met
- DEFERRED: Explicitly moved to future work with user approval

### Completion Checklist

*All items must be checked before claiming completion:*

- [x] Each FR-xxx row was verified by re-reading the actual implementation code
- [x] Each SC-xxx row was verified by running tests and reading actual test output
- [x] Evidence column contains specific file paths, line numbers, test names, and measured values
- [x] No evidence column contains only generic claims
- [x] No test thresholds relaxed from spec requirements
- [x] No placeholder values or TODO comments in new code
- [x] No features quietly removed from scope
- [x] User would NOT feel cheated by this completion claim

### Honest Assessment

**Overall Status**: COMPLETE

All 16 functional requirements (FR-001 through FR-016) and all 8 success criteria (SC-001 through SC-008) are MET with specific evidence.
