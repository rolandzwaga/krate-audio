# Research: Pitch Tracking Robustness (063)

**Date**: 2026-02-18
**Status**: Complete

## Research Task 1: PitchDetector Internal Detection Triggering

**Question**: How does PitchDetector trigger detection internally, and how should PitchTracker intercept post-detect results?

**Finding**: `PitchDetector::push(sample)` automatically calls `detect()` every `windowSize_/4` samples via a counter (`samplesSinceLastDetect_`). The `pushBlock()` method simply iterates over samples calling `push()`. This means the PitchTracker does NOT need to manage its own accumulation buffer or call `detect()` explicitly.

**Design Decision**: PitchTracker will feed samples to PitchDetector via `push()` one-by-one. After each `push()` that triggers detection (detected by checking `samplesSinceLastDetect_ == 0` -- but this is private), we need to run the 5-stage pipeline.

**Revised Approach**: Since we cannot observe when PitchDetector internally runs `detect()` (the counter is private), PitchTracker should manage its own hop counting. It will:
1. Track samples pushed since the last pipeline execution
2. Every `windowSize/4` samples (the hop interval used by PitchDetector), read the detector's current state and run the pipeline
3. This mirrors the PitchDetector's internal rhythm without needing to access private state

**Alternative Considered**: Modifying PitchDetector to expose a callback or detection flag. Rejected because it changes an existing, tested API.

**Alternative Considered**: Having PitchTracker bypass PitchDetector's `push()` and directly call `pushBlock()` + `detect()`. This would work since `pushBlock()` just calls `push()` in a loop, and `push()` calls `detect()` internally. The tracker can simply push all samples and read state after each hop boundary. Since `push()` already calls `detect()` at the right intervals, the tracker just needs to check after every `windowSize/4` samples fed.

**Decision**: PitchTracker will maintain its own sample counter matching PitchDetector's hop interval (`windowSize/4`). After each group of `hopSize` samples is pushed to the detector, the tracker reads `getDetectedFrequency()`, `getConfidence()`, and runs the 5-stage pipeline. This approach does NOT require changing PitchDetector's API.

**Rationale**: Clean composition -- PitchTracker wraps PitchDetector without modifying it.

---

## Research Task 2: Hysteresis Cents Comparison

**Question**: How should the hysteresis stage compare cents deviation from the committed note using existing utilities?

**Finding**: The spec's FR-014 references `frequencyToCentsDeviation()` from `pitch_utils.h`, but this function computes deviation from the NEAREST chromatic note to the input frequency, NOT from a specific committed note.

Example: If committed note is A4 (69, 440 Hz) and median frequency is 475 Hz, `frequencyToCentsDeviation(475)` returns ~32 cents (deviation from Bb4, the nearest note to 475 Hz). But the actual deviation from the committed A4 is ~132 cents.

**Decision**: Use `frequencyToMidiNote(medianFreq)` to get a continuous MIDI note value, then compute cents distance from committed note:
```cpp
float centsFromCommitted = std::abs(frequencyToMidiNote(medianFreq)
                                    - static_cast<float>(currentNote_)) * 100.0f;
```

This correctly measures the cents distance from the committed note's center, not from the nearest note to the input frequency. Both `frequencyToMidiNote()` (from `pitch_utils.h`) and the formula are mathematically equivalent to `1200 * log2(f_median / f_committed)`.

**Rationale**: `frequencyToCentsDeviation()` measures deviation from NEAREST note (useful for tuner-like applications). For hysteresis against a SPECIFIC committed note, the continuous MIDI difference * 100 gives the correct cents distance.

**Note on `midiNoteToFrequency`**: This function exists in `midi_utils.h` (Layer 0) with signature `constexpr float midiNoteToFrequency(int midiNote, float a4Frequency = kA4FrequencyHz)`. It could also be used to compute committed note center frequency, but the `frequencyToMidiNote()` approach is simpler and avoids an additional include.

---

## Research Task 3: Median Filter Implementation

**Question**: What is the best real-time-safe median filter implementation for small fixed-size windows (1-11)?

**Finding**: For N <= 11, the standard approach is:
1. Ring buffer of size N storing recent confident pitch values
2. Copy to scratch array
3. Insertion sort (optimal for small N, O(N^2) but N <= 11 makes this fast)
4. Return middle element

**Decision**: Use `std::array<float, kMaxMedianSize>` for ring buffer and `std::array<float, kMaxMedianSize>` for scratch/sort array. Insertion sort for median computation. No heap allocation.

**Rationale**: For N <= 11, insertion sort is faster than more complex algorithms due to cache locality and no overhead. `std::nth_element` would also work but involves more complex partitioning logic unnecessary for tiny arrays.

---

## Research Task 4: OnePoleSmoother Integration Pattern

**Question**: How should the frequency smoother be integrated?

**Finding**: `OnePoleSmoother` API:
- `configure(smoothTimeMs, sampleRate)` -- sets coefficient
- `setTarget(value)` -- sets target value
- `process()` -- returns next smoothed value (advances one sample)
- `getCurrentValue()` -- returns current value without advancing
- `snapTo(value)` -- instantly sets both current and target
- `reset()` -- sets current and target to 0

**Design Decision**:
- In `prepare()`: call `frequencySmoother_.configure(25.0f, sampleRate)` and `reset()`
- When a note is committed: call `frequencySmoother_.setTarget(midiNoteToFrequency(committedNote))`
- On first detection: call `frequencySmoother_.snapTo(frequency)` to avoid smoothing from 0 Hz
- `getFrequency()` returns `frequencySmoother_.getCurrentValue()`
- The smoother needs to be advanced: call `process()` once per hop to advance the smoother. Since the hop interval is `windowSize/4` samples, we should call `advanceSamples(hopSize)` to advance the smoother by the correct number of samples.

**Rationale**: `advanceSamples()` provides the correct time progression for the smoother between hops. `getCurrentValue()` provides the last computed value for queries.

---

## Research Task 5: Sample Accumulation and Hop Alignment

**Question**: How does the PitchTracker ensure its pipeline runs in sync with PitchDetector's internal detection?

**Finding**: PitchDetector runs `detect()` every `windowSize/4` samples inside `push()`. The hop size is `windowSize / 4`.

**Design Decision**: PitchTracker maintains a counter `samplesSinceLastHop_` that increments with each sample pushed. Every time it reaches `hopSize_` (= `windowSize/4`), the tracker:
1. Reads detector state (`getDetectedFrequency()`, `getConfidence()`)
2. Runs the 5-stage pipeline
3. Advances the smoother by `hopSize_` samples
4. Resets the counter

This counter naturally stays in sync with the detector's internal `samplesSinceLastDetect_` because both start at 0 after prepare/reset and both count the same samples.

**Rationale**: This approach ensures the pipeline runs exactly when the detector has fresh results, without modifying the detector's API.

---

## Research Task 6: FR-011 Zero Heap Allocations Assessment

**Question**: Can the PitchTracker satisfy FR-011 (zero heap allocations in processing path) given that PitchDetector uses `std::vector`?

**Finding**: `PitchDetector::buffer_` and `autocorr_` are `std::vector<float>`, allocated during `prepare()`. The processing methods (`push()`, `detect()`, `pushBlock()`) do NOT allocate -- they only write to pre-allocated vector storage.

**Decision**: FR-011 is satisfiable. The PitchDetector's vectors are allocated in `prepare()` (not on the audio thread). The PitchTracker's own state uses `std::array` (stack-allocated). No allocations occur in `pushBlock()` or any processing method.

**Rationale**: The "processing path" refers to `pushBlock()` and query methods, not `prepare()`. Both PitchDetector and PitchTracker allocate only during `prepare()`.

---

## Research Task 7: Note Candidate vs Committed Note State Machine

**Question**: What is the precise state machine for note transitions?

**Finding**: The state machine has these states:
1. **No committed note** (`currentNote_ == -1`): Initial state. First confident detection bypasses hysteresis and min duration, commits immediately.
2. **Tracking** (`currentNote_ >= 0, candidateNote_ == -1`): Committed note, no candidate yet. Hysteresis prevents small deviations from creating candidates.
3. **Candidate pending** (`currentNote_ >= 0, candidateNote_ >= 0`): A new note passed hysteresis. Timer counting up. If timer reaches `minNoteDurationSamples_`, candidate becomes committed. If candidate changes, timer resets.

**Transitions**:
- State 1 -> State 2: First confident, median-filtered detection arrives. Commit immediately.
- State 2 -> State 3: Median-filtered note deviates from committed note by > hysteresis threshold.
- State 3 -> State 2: Timer expires, candidate becomes committed note. `candidateNote_` resets to -1.
- State 3 -> State 3: Candidate changes (different note from previous candidate). Timer resets.
- State 3 -> State 2: Candidate matches committed note (pitch returned to committed note). Cancel candidate.

**Decision**: Implement as documented. `candidateNote_` is the MIDI note (integer) of the proposed new note. `noteHoldTimer_` counts samples. Timer advances by `hopSize` per pipeline execution (not by 1).

**Rationale**: Timer should advance by hop size because the pipeline runs once per hop, not once per sample. Each pipeline execution represents `hopSize` samples of audio.

---

## Research Task 8: SIMD Viability

**Question**: Is SIMD optimization beneficial for the PitchTracker?

**Finding**: The PitchTracker is a thin wrapper around PitchDetector with simple scalar operations:
- Confidence comparison (1 branch)
- Ring buffer write (1 array write)
- Insertion sort of <= 11 elements (tiny)
- Cents comparison (1 log2, 1 subtraction)
- Timer comparison (1 branch)
- Smoother advance (1 multiplication)

The hot path is PitchDetector's autocorrelation, which is already in the detector itself (not in PitchTracker's scope). PitchTracker's overhead is negligible.

**Decision**: NOT BENEFICIAL. The PitchTracker adds approximately 50-100 scalar operations per hop interval (~64 samples at 256 window size). This is far under the 0.1% CPU budget.

**Rationale**: No data parallelism, tiny working set, branch-dominated logic. The performance budget is trivially met.
