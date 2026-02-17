# Feature Specification: Pitch Tracking Robustness

**Feature Branch**: `063-pitch-tracker`
**Plugin**: KrateDSP (Shared DSP Library)
**Created**: 2026-02-17
**Status**: Draft
**Input**: User description: "Phase 3 of the harmonizer roadmap: PitchTracker class (Layer 1, primitives) wrapping PitchDetector with median filtering, hysteresis, confidence gating, minimum note duration, and frequency smoothing for stable harmonizer pitch input."
**Roadmap Reference**: [harmonizer-roadmap.md, Phase 3: Pitch Tracking Robustness](../harmonizer-roadmap.md#phase-3-pitch-tracking-robustness) (lines 464-582)

## Clarifications

### Session 2026-02-18

- Q: When a frame arrives with confidence below the gating threshold, should the raw frequency be added to the median filter ring buffer or skipped? → A: Skip low-confidence frames. Only frames where confidence >= confidenceThreshold_ are added to the ring buffer.
- Q: Does pushBlock() internally call detect() whenever enough samples have accumulated, or is detect() part of the public calling contract? → A: pushBlock() internally calls detect() for each accumulated analysis hop, producing 0..N detect() calls per invocation. detect() is NOT part of the public API. This prevents pitch lag when one host block spans multiple analysis hops.
- Q: What is the default smoothing time constant for the OnePoleSmoother on the frequency output? → A: 25ms default time constant.
- Q: On the first confident detection (initial state or after reset), is hysteresis also bypassed alongside the minimum note duration timer? → A: Yes. Hysteresis is bypassed on the first confident detection. Hysteresis only operates after the tracker has entered a valid tracking state (has a committed note).
- Q: What is the explicit pipeline order for the five post-processing stages? → A: Confidence Gate → Median Filter → Hysteresis → Min Note Duration → Frequency Smoother.

## Background & Motivation

Raw pitch detector output is noisy and can oscillate between adjacent notes, causing a harmony voice to "warble" or produce audible pitch jumps. Commercial harmonizers (Eventide, TC-Helicon, Boss) apply significant smoothing and confidence thresholds to produce stable note decisions. The existing `PitchDetector` provides raw autocorrelation-based pitch detection with confidence output but lacks any post-processing for stability.

The `PitchTracker` wraps `PitchDetector` with five well-established post-processing techniques drawn from the pitch tracking literature, applied in this order:

1. **Confidence gating** -- rejects unreliable frames before they influence any downstream stage; holds the last valid note during unvoiced segments (standard voiced/unvoiced classification in SPICE, YIN, CREPE)
2. **Median filtering** -- eliminates single-frame outlier pitch detections from the stream of confident frames only (de Cheveigne & Kawahara, 2002; Mauch & Dixon, 2014)
3. **Hysteresis** -- prevents oscillation at note boundaries when pitch hovers near a semitone boundary (Daqarta pitch tracking; SiPTH singing transcription, Molina et al. 2014)
4. **Minimum note duration** -- prevents rapid note-switching artifacts ("warbling") by requiring a note to be stable for a configurable duration before committing the transition (standard practice in commercial pitch correction and harmonizer products)
5. **Frequency smoothing** -- applies exponential smoothing via `OnePoleSmoother` for portamento-like transitions between committed notes, avoiding discontinuous frequency jumps in the pitch-shifted output

These techniques are compositional: each can be configured independently, and together they transform a raw, jittery pitch stream into a stable, musically coherent note stream suitable for driving a diatonic harmonizer engine (Phase 4).

### Processing Pipeline

The per-analysis-hop processing order is fixed and non-configurable:

```
pushBlock() → [internal detect() per hop]
    │
    ▼
[1] Confidence Gate
    │  confidence < threshold → hold last committed state, isPitchValid() = false, STOP
    │  confidence >= threshold ↓
    ▼
[2] Median Filter (ring buffer of confident frames only)
    │  add frequency to ring buffer
    │  compute median of available history
    ▼
[3] Hysteresis
    │  no committed note yet → bypass, accept immediately (first detection)
    │  cents deviation from committed note <= hysteresisThreshold_ → STOP (no candidate change)
    │  cents deviation > hysteresisThreshold_ → new candidate proposed
    ▼
[4] Minimum Note Duration
    │  no committed note yet → bypass, commit immediately (first detection)
    │  candidate unchanged, timer < minNoteDurationSamples_ → increment timer, STOP
    │  candidate changed → reset timer, STOP
    │  timer >= minNoteDurationSamples_ → commit candidate as new note
    ▼
[5] Frequency Smoother (OnePoleSmoother, default 25ms time constant)
    │  setTarget(committedNoteFrequency)
    ▼
getFrequency() → smoothed Hz
getMidiNote()  → committed note (integer, NOT derived from smoothed frequency)
```

**Key invariants enforced by this order:**
- Low-confidence frames never enter the median ring buffer (stages 1 and 2 are coupled by design).
- Hysteresis and minimum duration see only median-filtered, confidence-verified pitch data.
- `getMidiNote()` reflects the committed note from stage 4, not back-derived from the smoothed frequency of stage 5. The smoother is output-only and does not affect note decisions.
- On first detection (no committed note), stages 3 and 4 are both bypassed: the note is committed immediately upon the first confident, median-stable pitch.

### Scientific Basis

- **YIN** (de Cheveigne & Kawahara, 2002): Autocorrelation-based F0 estimator using cumulative mean normalized difference function (CMNDF) with an absolute threshold of 0.1. The PitchDetector uses a simpler normalized autocorrelation approach (similar to YIN Steps 1-3 but without CMNDF). Note: YIN's own output is claimed to be smooth enough to require no post-processing, but this does not hold for simpler autocorrelation-based detectors like ours, which are susceptible to octave errors and frame-to-frame jitter. Post-processing is standard practice for non-YIN pitch estimators.
- **pYIN** (Mauch & Dixon, 2014): Probabilistic extension of YIN that outputs multiple pitch candidates with HMM Viterbi decoding for smooth tracking. Uses a beta(2,18) distribution over 100 thresholds and `switch_prob=0.01` for voiced/unvoiced transitions. The PitchTracker achieves comparable smoothness through simpler, real-time-friendly techniques (median filter + hysteresis) without the computational cost of HMM decoding.
- **CREPE** (Kim et al., 2018): Neural pitch estimator that outputs per-frame confidence scores used for voiced/unvoiced gating -- the same principle applied here via confidence thresholding.
- **Median filtering for pitch**: Moving median filters are the standard approach for removing impulsive outliers from pitch tracks while preserving step transitions better than mean filters. MATLAB's pitch tracking example (MathWorks) uses a 3-hop median filter as the final post-processing step. The Cycfi Q library uses a 3-point median in its bias section. The default size of 5 in this spec provides stronger octave-error rejection than 3 at the cost of ~6ms additional latency (2 extra frames at 256 samples/44.1kHz). The filter size is configurable (1-11) so users can reduce to 3 for lower latency or increase for noisier inputs. Critically, the ring buffer is populated only from confident frames (confidence >= threshold), so the latency figure applies only to voiced segments.
- **Hysteresis for note detection** (Daqarta): Daqarta's documentation explicitly recommends 0.5 semitones (50 cents) as the starting point for pitch hysteresis, noting it allows tones "nearly half a semitone sharp or flat to be reported as perfectly tuned." Values exceeding 50 cents risk missing intended note changes and "should be used with caution." This validates the spec's default of 50 cents.
- **Confidence gating**: Universal across all major pitch estimators (YIN, pYIN, CREPE, SPICE). The specific threshold value depends on the detector's confidence output range. YIN uses a CMNDF threshold of 0.1 (inverted scale: lower = more periodic), while neural estimators like CREPE typically threshold confidence at 0.5. The default of 0.5 in this spec is appropriate for our autocorrelation-based PitchDetector's normalized confidence output, but is configurable for tuning. Confidence gating is applied first in the pipeline to prevent unreliable frames from polluting the median ring buffer.
- **Minimum note duration**: No single authoritative value exists in the literature. Physical constraint: pitch detection requires at least 2 periods (~24ms for 82Hz low E, ~50ms for 40Hz). Eventide harmonizer users report 10-40ms processing delays. The default of 50ms is a practical engineering choice that filters sub-50ms glitches while allowing tracking of up to ~10 note changes/second.
- **Frequency smoothing time constant**: 25ms is chosen as the default for the `OnePoleSmoother`. This is fast enough to track note transitions without audible sluggishness (note changes are committed by stage 4 independently of the smoother), yet slow enough to prevent discontinuous frequency jumps from reaching downstream pitch-shift processing. The smoother only affects `getFrequency()`; `getMidiNote()` is always the committed discrete note.

### References

- de Cheveigne, A. & Kawahara, H. (2002). "YIN, a fundamental frequency estimator for speech and music." *J. Acoust. Soc. Am.*, 111(4), 1917-1930.
- Mauch, M. & Dixon, S. (2014). "pYIN: A fundamental frequency estimator using probabilistic threshold distributions." *ICASSP 2014*.
- Kim, J.W. et al. (2018). "CREPE: A Convolutional Representation for Pitch Estimation." *ICASSP 2018*.
- Faghih, B. & Timoney, J. (2022). "Smart-Median: A New Real-Time Algorithm for Smoothing Singing Pitch Contours." *Applied Sciences*, 12(14), 7026.
- Riccardi, R. et al. (2025). "PESTO: Real-Time Pitch Estimation with Self-supervised Transposition-equivariant Objective." *Trans. ISMIR*.
- Daqarta (n.d.). "Pitch Hysteresis." https://www.daqarta.com/dw_qqhh.htm

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Stable Pitch Input for Diatonic Harmonizer (Priority: P1)

As a harmonizer engine (Phase 4), I need to receive a stable, reliable MIDI note from the pitch tracker so that the diatonic interval calculation produces consistent harmony shifts without warbling or clicking.

**Why this priority**: This is the core purpose of the PitchTracker -- without stable pitch input, the entire harmonizer chain produces audible artifacts. Every downstream consumer depends on this.

**Independent Test**: Can be fully tested by feeding known pitch sequences (sine tones, chromatic runs, vibrato signals) into the PitchTracker and verifying that the output MIDI note remains stable and transitions cleanly. No harmonizer engine required for testing.

**Acceptance Scenarios**:

1. **Given** a stable 440Hz sine tone input, **When** the PitchTracker processes multiple consecutive analysis windows, **Then** the output MIDI note is consistently A4 (69) with no fluctuation between adjacent notes.
2. **Given** a sine tone with +/- 10 cents of jitter (simulating natural pitch variation), **When** the PitchTracker processes the signal, **Then** the output note remains A4 (69) without oscillating to G#4 or Bb4 (hysteresis prevents boundary crossing).
3. **Given** a clean transition from A4 (440Hz) to B4 (494Hz), **When** the pitch changes, **Then** the PitchTracker switches from note 69 to note 71 after the minimum note duration timer expires, producing exactly one clean transition.

---

### User Story 2 - Graceful Handling of Unvoiced Segments (Priority: P2)

As a harmonizer engine, I need the pitch tracker to maintain the last valid note during silence, noise, or unvoiced consonants so that the harmony voice does not produce garbage output during vocal breaths or pauses.

**Why this priority**: Vocals frequently contain unvoiced segments (breaths, consonants, silence between phrases). Without confidence gating, the pitch tracker would output random frequencies during these segments, causing the harmony voice to produce chaotic pitch shifts.

**Independent Test**: Can be tested by feeding alternating pitched and silent/noise segments and verifying that the tracker holds the last valid note during unvoiced segments and correctly reports pitch validity status.

**Acceptance Scenarios**:

1. **Given** a pitched signal (A4) followed by silence, **When** the signal transitions to silence, **Then** `isPitchValid()` returns false while `getMidiNote()` continues to return 69 (A4) and `getFrequency()` returns the last valid smoothed frequency.
2. **Given** a pitched signal followed by white noise, **When** confidence drops below the threshold, **Then** the tracker holds the previous note and reports invalid pitch status.
3. **Given** silence followed by a new pitched signal (C5), **When** confidence rises above threshold, **Then** the tracker transitions to the new note after the minimum duration timer and `isPitchValid()` returns true.

---

### User Story 3 - Configurable Tracking Behavior (Priority: P3)

As a sound designer or plugin developer, I need to adjust the pitch tracker's sensitivity, hysteresis, and timing parameters so that I can tune its behavior for different musical contexts (fast monophonic leads vs. slow vocal pads vs. polyphonic-adjacent use).

**Why this priority**: Different musical contexts require different tracking parameters. A fast guitar solo needs lower minimum duration (faster note switching) while a slow vocal pad benefits from more aggressive smoothing and wider hysteresis. Configurability ensures the PitchTracker is useful across the full range of harmonizer applications.

**Independent Test**: Can be tested by verifying that each configuration parameter (median size, hysteresis threshold, confidence threshold, minimum note duration) independently affects tracking behavior in measurable ways.

**Acceptance Scenarios**:

1. **Given** a signal with rapid pitch changes (5 note changes per second), **When** minimum note duration is set to 50ms (default), **Then** the tracker suppresses some rapid transitions. **When** minimum note duration is reduced to 20ms, **Then** the tracker follows more rapid changes.
2. **Given** a signal hovering at the boundary between two notes, **When** hysteresis is set to 50 cents (default), **Then** the tracker does not switch notes. **When** hysteresis is reduced to 10 cents, **Then** the tracker may oscillate between notes.
3. **Given** a noisy signal with intermittent pitch, **When** confidence threshold is set to 0.5 (default), **Then** only confident pitch detections update the note. **When** confidence threshold is lowered to 0.2, **Then** more detections are accepted (including noisier ones).

---

### User Story 4 - Elimination of Single-Frame Outliers (Priority: P2)

As a harmonizer engine, I need the pitch tracker to reject single-frame "octave jump" errors (a common artifact in autocorrelation-based pitch detection where the detector briefly locks onto the second harmonic or a subharmonic) so that the harmony voice does not produce momentary pitch glitches.

**Why this priority**: Autocorrelation pitch detectors are susceptible to octave errors -- briefly detecting a pitch one octave above or below the true pitch. These single-frame errors cause the harmonizer to produce audible "blips." Median filtering is the standard solution. Because only confident frames are added to the ring buffer, octave errors that also have low confidence are rejected at stage 1 before reaching the median filter.

**Independent Test**: Can be tested by injecting a known outlier frequency into an otherwise stable pitch sequence (with high confidence) and verifying the median filter rejects it.

**Acceptance Scenarios**:

1. **Given** a sequence of high-confidence pitch detections [440, 440, 880, 440, 440] (one octave-jump outlier), **When** the median filter (size 5) processes this sequence, **Then** the output for the middle frame is 440Hz (the outlier is rejected).
2. **Given** a sequence with two consecutive high-confidence outliers [440, 880, 880, 440, 440], **When** the median filter (size 5) processes this sequence, **Then** the outliers are partially rejected (median of [440, 880, 880, 440, 440] = 440).

---

### Edge Cases

- What happens when the PitchTracker receives its first audio block after initialization? It should output invalid pitch (no committed note yet) rather than a spurious note. The first confident detection bypasses both hysteresis and minimum duration and is committed immediately (FR-015, FR-003).
- What happens when the input frequency is at the extreme boundaries of the PitchDetector range (50Hz, 4000Hz)? The tracker should respect the detector's range limits and report invalid pitch for out-of-range inputs.
- What happens when the median filter buffer is not yet full (fewer than `medianSize` confident detections)? The tracker should use whatever confident history is available (median of the available confident samples).
- What happens when `prepare()` is called with a new sample rate while the tracker has existing state? All internal state (history buffer, timers, smoother) should be fully reset.
- What happens when the minimum note duration is set to 0ms? The tracker should behave as if note duration gating is disabled (immediate note transitions on any hysteresis-crossing candidate).
- What happens when the hysteresis threshold is set to 0 cents? The tracker should behave as if hysteresis is disabled (any pitch change from the committed note triggers a new candidate).
- What happens when all pitch detections have confidence below the threshold for an extended period? The confidence gate rejects all frames; the median ring buffer is not updated; the tracker holds the last valid committed note indefinitely and continuously reports `isPitchValid() == false`.
- What happens when a host block is larger than one analysis hop (e.g., 512-sample block with 256-sample window)? `pushBlock()` internally triggers two `detect()` calls, producing up to two pitch updates within a single `pushBlock()` invocation. The last detected result is the state visible to the caller after `pushBlock()` returns.
- What happens when a host block is smaller than one analysis hop? `pushBlock()` accumulates samples but triggers zero `detect()` calls. The tracker state is unchanged and `isPitchValid()` reflects the previous frame's result.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The PitchTracker MUST accept audio samples via a `pushBlock()` method. Internally, `pushBlock()` feeds samples to the `PitchDetector` via `push()` (single sample) and runs the 5-stage pipeline once per completed analysis hop, producing 0..N pipeline executions per `pushBlock()` invocation. `detect()` MUST NOT be part of the public API of `PitchTracker` -- detection is triggered internally by `PitchDetector::push()`. This design prevents pitch lag when one host buffer spans multiple analysis hops. (See FR-016 for the accumulation mechanics.)
- **FR-002**: The PitchTracker MUST apply a configurable median filter (size 1-11, default 5) to the pitch frequency values of confident frames only. Only frames where `PitchDetector::getConfidence() >= confidenceThreshold_` are added to the median ring buffer. Low-confidence frames are discarded before reaching the ring buffer and do not affect the median output.
- **FR-003**: The PitchTracker MUST implement hysteresis with a configurable threshold in cents (default 50 cents). A new note candidate is only proposed when the median-filtered pitch deviates from the current committed note center by more than the hysteresis threshold. Hysteresis is bypassed when no committed note exists (initial state or after reset): the first confident, median-filtered detection is accepted as a candidate unconditionally.
- **FR-004**: The PitchTracker MUST implement confidence gating as the first processing stage. When `PitchDetector::getConfidence()` falls below `confidenceThreshold_`, the frame is discarded entirely: the median ring buffer is not updated, no candidate note is proposed, the last committed state is held, and `isPitchValid()` MUST return false.
- **FR-005**: The PitchTracker MUST enforce a configurable minimum note duration (default 50ms). A new note is only committed after the candidate note has been consistently detected for at least this duration. The timer is tracked in samples (converted from milliseconds using the sample rate). The minimum duration check is bypassed when no committed note exists (first detection).
- **FR-006**: The PitchTracker MUST apply frequency smoothing via `OnePoleSmoother` on the committed note's target frequency. The default smoothing time constant MUST be 25ms. `getFrequency()` returns the smoothed Hz value. `getMidiNote()` returns the integer MIDI note of the committed note and MUST NOT be derived from the smoothed frequency output.
- **FR-007**: The PitchTracker MUST provide a `prepare(sampleRate, windowSize)` method that initializes the internal PitchDetector, configures the sample rate for all time-dependent parameters (minimum duration sample count, smoother coefficient), and resets all state.
- **FR-008**: The PitchTracker MUST provide a `reset()` method that clears all internal state (median ring buffer, note hold timer, committed note, smoother state) without changing configuration parameters.
- **FR-009**: The PitchTracker MUST provide read-only query methods: `getFrequency()` (smoothed Hz from stage 5), `getMidiNote()` (committed note integer from stage 4), `getConfidence()` (raw value from underlying PitchDetector), and `isPitchValid()` (true iff last frame passed confidence gate).
- **FR-010**: The PitchTracker MUST provide configuration setter methods: `setMedianFilterSize()`, `setHysteresisThreshold()`, `setConfidenceThreshold()`, `setMinNoteDuration()`. Each setter MUST enforce the following validation:
  - `setMedianFilterSize(size)`: MUST clamp `size` to `[1, kMaxMedianSize]` (i.e., [1, 11]). Values of 0 are treated as 1; values above 11 are treated as 11.
  - `setConfidenceThreshold(threshold)`: MUST clamp `threshold` to `[0.0, 1.0]`.
  - `setHysteresisThreshold(cents)`: MUST clamp `cents` to `[0.0, +inf)`. Negative values are treated as 0 (hysteresis disabled).
  - `setMinNoteDuration(ms)`: MUST clamp `ms` to `[0.0, +inf)`. Negative values are treated as 0 (minimum duration disabled; immediate note transitions).
- **FR-011**: All PitchTracker methods MUST be `noexcept` and perform zero heap allocations in the processing path. All buffers (median history, sample accumulation, etc.) MUST use fixed-size stack-allocated arrays.
- **FR-012**: The PitchTracker MUST reside at Layer 1 (Primitives) in the DSP architecture, depending only on Layer 0 (Core) utilities (`frequencyToMidiNote()` from `pitch_utils.h`, `midiNoteToFrequency()` from `midi_utils.h`) and other Layer 1 components (`PitchDetector`, `OnePoleSmoother`). `frequencyToCentsDeviation()` is available but not used (see FR-014).
- **FR-013**: The median filter MUST use a ring buffer of recent confident pitch values with an insertion-sort-based median computation (no heap allocation for sorting). For a filter size of N, the median is the middle value of the sorted last N confident pitch detections. Entries from low-confidence frames are never written to the ring buffer.
- **FR-014**: The hysteresis comparison MUST use `frequencyToMidiNote()` from `pitch_utils.h` to compute the cents distance between the median-filtered frequency and the current committed note. The correct formula is `std::abs(frequencyToMidiNote(medianFreq) - static_cast<float>(currentNote_)) * 100.0f`, which gives the distance in cents from the committed note's center. `frequencyToCentsDeviation()` MUST NOT be used for this comparison -- that function measures deviation from the nearest chromatic note, not from the specific committed note, and produces incorrect results when the median frequency is more than 50 cents from the committed note.
- **FR-015**: When the PitchTracker has no committed note (initial state or after reset), the first confident pitch detection MUST be accepted immediately, bypassing both the minimum note duration timer (FR-005) and hysteresis (FR-003). This gives immediate musical responsiveness before a committed tracking state is established.
- **FR-016**: The PitchTracker's sample accumulation works by tracking a hop counter that mirrors `PitchDetector`'s internal detection rhythm (`windowSize/4` samples per hop). `pushBlock()` MUST iterate over all `numSamples` input samples, calling `PitchDetector::push()` for each sample and incrementing the hop counter. When the counter reaches `hopSize_`, `runPipeline()` is called and the counter resets. Multiple pipeline executions within a single `pushBlock()` are expected and correct when the host block size exceeds the hop size. A call to `pushBlock()` with `numSamples == 0` MUST be a no-op with no observable side effects. (See FR-001 for the design rationale.)

### Key Entities

- **PitchTracker**: The primary component. Wraps PitchDetector with a fixed five-stage post-processing pipeline. Holds a ring buffer of recent confident pitch values (median filter), the current committed note (hysteresis + min-duration state), a note hold timer (minimum duration), and a frequency smoother (OnePoleSmoother). Lives at Layer 1.
- **Median Filter State**: A fixed-size ring buffer (`std::array<float, kMaxMedianSize>`) storing the most recent N confident pitch frequency values. Only frames that passed the confidence gate (stage 1) are written into this buffer. Entries are overwritten in circular fashion. Median is computed by copying to a scratch array and finding the middle element via insertion sort.
- **Hysteresis State**: The currently committed MIDI note (integer, -1 when no note committed). A new note candidate must deviate by more than the hysteresis threshold (in cents) from the center frequency of this committed note before the tracker proposes a transition. When `currentNote_ == -1`, hysteresis is bypassed entirely.
- **Note Hold Timer**: A sample counter tracking how long the current candidate note has been consistently detected after passing the hysteresis gate. Once this counter exceeds the minimum note duration (converted to samples), the candidate replaces the committed note. Resets when the candidate changes. Bypassed when `currentNote_ == -1`.
- **Frequency Smoother**: A `OnePoleSmoother` instance receiving the committed note's reference frequency as its target whenever a note is committed. `getFrequency()` returns the current smoothed value. Default time constant: 25ms.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Given a stable pitched input (pure sine tone), the PitchTracker MUST output a single, unwavering MIDI note with zero note switches over a 2-second observation window.
- **SC-002**: Given a pitched input with +/- 20 cents of random jitter, the PitchTracker MUST produce zero note switches over a 2-second window when hysteresis is set to the default 50 cents.
- **SC-003**: Given a single-frame octave-jump outlier (high confidence) injected into an otherwise stable pitch sequence, the median filter MUST reject the outlier -- the output note MUST NOT change in response to a single anomalous confident frame.
- **SC-004**: Given a clean A4-to-B4 pitch transition, the PitchTracker MUST produce exactly one note switch (from 69 to 71), occurring within 100ms of the actual transition (accounting for the minimum note duration timer + detection latency).
- **SC-005**: Given alternating 500ms voiced and 500ms silent segments, the PitchTracker MUST hold the last valid note during all silent segments, with `isPitchValid()` returning false during silence and true during voiced segments.
- **SC-006**: Given rapid pitch changes (5 note transitions per second), the PitchTracker with default 50ms minimum note duration MUST suppress at least some transitions, producing fewer output note changes than input transitions.
- **SC-007**: The PitchTracker MUST consume less than 0.1% CPU at 44.1kHz sample rate (Layer 1 performance budget), measured as the incremental cost beyond the underlying PitchDetector.
- **SC-008**: All PitchTracker processing methods MUST perform zero heap allocations, verified by inspection of the implementation (no `new`, `delete`, `malloc`, `free`, `vector::push_back`, or other allocating operations in the processing path).
- **SC-009**: Given a host block larger than the analysis window (e.g., 512-sample block with 256-sample window), a single `pushBlock()` call MUST process both analysis hops and leave the tracker in the state produced by the second (most recent) hop. Verified by observing that the tracker state after the call reflects the result of the second 256-sample hop analysis, not just the first.

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- The underlying `PitchDetector` correctly detects pitch via normalized autocorrelation with 256-sample analysis window and provides a confidence value in the range [0.0, 1.0].
- The `PitchDetector::getDetectedFrequency()` returns frequency in Hz, and `getConfidence()` returns a normalized confidence score where higher values indicate more reliable detection.
- The `PitchDetector` exposes a `push(float sample)` method (single-sample accumulation) that internally triggers `detect()` every `windowSize/4` samples via a private counter. The `PitchTracker` calls `push()` in a loop internally; callers of `PitchTracker` see only `PitchTracker::pushBlock()`. `PitchTracker` does NOT call `detect()` directly -- detection is triggered automatically inside `push()`.
- The `frequencyToMidiNote()` function returns a continuous MIDI note value (e.g., 69.0 for A4, 69.5 for A4+50 cents) and the integer part maps to the nearest note.
- The `frequencyToCentsDeviation()` function returns a value in [-50, +50] cents representing the deviation from the nearest chromatic note center.
- The `OnePoleSmoother` can be configured with a smoothing time in milliseconds and a sample rate, and provides `setTarget()` and `process()` or `getCurrentValue()` methods for exponential smoothing. The default time constant for the frequency smoother is 25ms.
- Sample rates of 44100, 48000, 88200, 96000, and 192000 Hz are all supported.
- The PitchTracker will be called from the audio thread and must satisfy all real-time constraints (no allocations, no locks, no exceptions, no I/O).
- The PitchTracker is consumed by the Phase 4 HarmonizerEngine, which calls `pushBlock()` once per audio block and reads `getMidiNote()` / `getFrequency()` for interval computation.

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that MUST be reused (not reimplemented):**

| Component | Location | Relevance |
|-----------|----------|-----------|
| `PitchDetector` | `dsp/include/krate/dsp/primitives/pitch_detector.h` (L1) | Core pitch detection engine. PitchTracker wraps this; does NOT reimplement pitch detection. PitchTracker calls `push()` (single sample) internally in a loop; `push()` auto-triggers `detect()` every `windowSize/4` samples. `detect()` is not part of PitchTracker's public API. |
| `OnePoleSmoother` | `dsp/include/krate/dsp/primitives/smoother.h` (L1) | Frequency smoothing (stage 5). PitchTracker contains one instance configured with a 25ms default time constant. Uses `setTarget()`, `process()` / `getCurrentValue()`, `configure()`. |
| `frequencyToMidiNote()` | `dsp/include/krate/dsp/core/pitch_utils.h` (L0) | Converts detected Hz to continuous MIDI note for note quantization and hysteresis comparison (see FR-014). |
| `midiNoteToFrequency()` | `dsp/include/krate/dsp/core/midi_utils.h` (L0) | Converts a committed integer MIDI note to its center frequency in Hz, used as the `OnePoleSmoother` target and for snap-to on first detection. |
| `frequencyToCentsDeviation()` | `dsp/include/krate/dsp/core/pitch_utils.h` (L0) | Available in the codebase but NOT used for hysteresis (see FR-014). This function measures deviation from the nearest chromatic note, which is incorrect for comparing against a specific committed note. |

**ODR check performed**: `grep -r "PitchTracker" dsp/ plugins/` returned no results. No existing class with this name in the codebase.

**Search Results Summary**: All four building blocks verified to exist with the expected APIs. No conflicts or duplications found. The `PitchDetector` has `push()` (single sample, auto-triggers `detect()` internally), `getDetectedFrequency()`, `getConfidence()`, and `isPitchValid()` methods as verified in the codebase. The `OnePoleSmoother` has `configure()`, `setTarget()`, `process()`, `advanceSamples()`, `getCurrentValue()`, `snapTo()`, and `reset()` methods. Both pitch utility functions exist with the documented signatures.

### API Reference

The following constants and method signatures reflect all clarified decisions:

```cpp
namespace Krate::DSP {

/// @brief Smoothed pitch tracker with confidence gating, median filtering,
///        hysteresis, minimum note duration, and frequency smoothing (Layer 1).
///
/// Processing pipeline (per internal analysis hop):
///   [1] Confidence gate  →  [2] Median filter (confident frames only)
///   →  [3] Hysteresis  →  [4] Min note duration  →  [5] Frequency smoother
///
/// @par Real-Time Safety
/// All methods are noexcept, no allocations in process path.
class PitchTracker {
public:
    static constexpr std::size_t kDefaultWindowSize           = 256;
    static constexpr std::size_t kMaxMedianSize               = 11;
    static constexpr float       kDefaultHysteresisThreshold  = 50.0f;  // cents
    static constexpr float       kDefaultConfidenceThreshold  = 0.5f;
    static constexpr float       kDefaultMinNoteDurationMs    = 50.0f;
    static constexpr float       kDefaultFrequencySmoothingMs = 25.0f;  // OnePoleSmoother time constant

    PitchTracker() noexcept = default;

    void prepare(double sampleRate, std::size_t windowSize = kDefaultWindowSize) noexcept;
    void reset() noexcept;

    /// Feed audio samples. Internally accumulates samples and triggers
    /// PitchDetector::detect() for each completed analysis hop (0..N calls).
    void pushBlock(const float* samples, std::size_t numSamples) noexcept;

    // Stable output (reflect pipeline stages 4 and 5)
    [[nodiscard]] float getFrequency()   const noexcept;  // stage 5: smoothed Hz
    [[nodiscard]] int   getMidiNote()    const noexcept;  // stage 4: committed note integer
    [[nodiscard]] float getConfidence()  const noexcept;  // raw from PitchDetector
    [[nodiscard]] bool  isPitchValid()   const noexcept;  // true iff last frame passed stage 1

    // Configuration
    void setMedianFilterSize(std::size_t size) noexcept;  // 1-11, default 5
    void setHysteresisThreshold(float cents)   noexcept;  // default 50
    void setConfidenceThreshold(float threshold) noexcept; // default 0.5
    void setMinNoteDuration(float ms)          noexcept;  // default 50ms

private:
    PitchDetector detector_;

    // Stage 2: Median filter (confident frames only)
    std::array<float, kMaxMedianSize> pitchHistory_{};
    std::size_t medianSize_    = 5;
    std::size_t historyIndex_  = 0;
    std::size_t historyCount_  = 0;       // number of confident frames written so far (capped at medianSize_)

    // Stage 3: Hysteresis state
    int   currentNote_           = -1;    // -1 = no committed note; bypasses hysteresis + min duration
    float hysteresisThreshold_   = kDefaultHysteresisThreshold;

    // Stage 1: Confidence gating
    float confidenceThreshold_   = kDefaultConfidenceThreshold;
    bool  pitchValid_            = false;

    // Stage 4: Note hold timer (minimum duration before switching)
    float       minNoteDurationMs_     = kDefaultMinNoteDurationMs;
    std::size_t noteHoldTimer_         = 0;   // in samples
    std::size_t minNoteDurationSamples_= 0;   // computed in prepare()
    int         candidateNote_         = -1;

    // Hop tracking (synchronized with PitchDetector's internal detection rhythm)
    double      sampleRate_            = 44100.0;
    std::size_t hopSize_               = kDefaultWindowSize / 4;  // samples between pipeline runs
    std::size_t samplesSinceLastHop_   = 0;
    std::size_t windowSize_            = kDefaultWindowSize;

    // Stage 5: Smoothed frequency output
    OnePoleSmoother frequencySmoother_;
    float           smoothedFrequency_ = 0.0f;
};

} // namespace Krate::DSP
```

### Forward Reusability Consideration

*The PitchTracker is a Layer 1 primitive. It is designed to be a general-purpose pitch tracking post-processor, not specific to the harmonizer.*

**Downstream consumers (known from roadmap):**
- Phase 4 `HarmonizerEngine` (L3) -- primary consumer, uses PitchTracker for shared input pitch analysis
- Potential future pitch-correction effect (not yet roadmapped but natural reuse)
- Potential future pitch-following modulation source (e.g., pitch-to-filter-cutoff)

**Potential shared components** (preliminary, refined in plan.md):
- The median filter ring buffer pattern could be extracted to a standalone utility if other components need median filtering. However, since the implementation is small (~20 lines) and specific to the pitch tracking use case, inlining it within PitchTracker is appropriate for now.

## Implementation Verification *(mandatory at completion)*

<!--
  CRITICAL: This section MUST be completed when claiming spec completion.
  Constitution Principle XVI: Honest Completion requires explicit verification
  of ALL requirements before claiming "done".

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

*DO NOT mark MET without having just verified the code and test output. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | | |
| FR-002 | | |
| FR-003 | | |
| FR-004 | | |
| FR-005 | | |
| FR-006 | | |
| FR-007 | | |
| FR-008 | | |
| FR-009 | | |
| FR-010 | | |
| FR-011 | | |
| FR-012 | | |
| FR-013 | | |
| FR-014 | | |
| FR-015 | | |
| FR-016 | | |
| SC-001 | | |
| SC-002 | | |
| SC-003 | | |
| SC-004 | | |
| SC-005 | | |
| SC-006 | | |
| SC-007 | | |
| SC-008 | | |
| SC-009 | | |

**Status Key:**
- MET: Requirement verified against actual code and test output with specific evidence
- NOT MET: Requirement not satisfied (spec is NOT complete)
- PARTIAL: Partially met with documented gap and specific evidence of what IS met
- DEFERRED: Explicitly moved to future work with user approval

### Completion Checklist

*All items must be checked before claiming completion:*

- [ ] Each FR-xxx row was verified by re-reading the actual implementation code (not from memory)
- [ ] Each SC-xxx row was verified by running tests or reading actual test output (not assumed)
- [ ] Evidence column contains specific file paths, line numbers, test names, and measured values
- [ ] No evidence column contains only generic claims like "implemented", "works", or "test passes"
- [ ] No test thresholds relaxed from spec requirements
- [ ] No placeholder values or TODO comments in new code
- [ ] No features quietly removed from scope
- [ ] User would NOT feel cheated by this completion claim

### Honest Assessment

**Overall Status**: NOT STARTED

**Recommendation**: Proceed to `/speckit.plan` for implementation planning, then `/speckit.implement` for development.
