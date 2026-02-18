# Feature Specification: Multi-Voice Harmonizer Engine

**Feature Branch**: `064-harmonizer-engine`
**Plugin**: KrateDSP (Shared DSP Library)
**Created**: 2026-02-18
**Status**: Draft
**Input**: User description: "HarmonizerEngine class -- a Layer 3 (Systems) multi-voice harmonizer engine that orchestrates existing DSP components to generate up to 4 pitch-shifted harmony voices with diatonic and chromatic modes, per-voice level/pan/delay/detune, shared pitch tracking, click-free transitions, and mono-to-stereo constant-power panning."
**Roadmap Reference**: [harmonizer-roadmap.md, Phase 4: Multi-Voice Harmonizer Engine](../harmonizer-roadmap.md#phase-4-multi-voice-harmonizer-engine) (lines 586-826)

## Clarifications

### Session 2026-02-18

- Q: What is the correct range for `setVoiceDetune()`? FR-003 says [-50, +50] cents; roadmap API comment shows [-100, +100]. → A: [-50, +50] cents. Half-semitone maximum keeps the parameter a true ensemble-width detuning effect; beyond 50 cents a voice begins to sound like a distinct pitch rather than a natural imprecision offset.
- Q: Does `wetLevel` scale the summed harmony bus after all voice accumulation, or is it folded into each voice's gain before accumulation? → A: wetLevel scales the entire summed harmony bus after all voices are accumulated (bus-level master fader). Per-voice level controls voice-to-voice balance; wetLevel is the single master return for all harmony output.
- Q: Should `dryLevel` and `wetLevel` be smoothed, and if so with what time constant and approach? → A: Yes, both MUST be smoothed at 10ms (slower than the 5ms per-voice level, because dry/wet swings span the full 0→1 range and affect entire signal energy including latency-induced comb filtering). Each gets its own independent `OnePoleSmoother` applied to the linear gain value per-sample; smoothing a single mix ratio via lerp is forbidden. Complete smoothing time constant table: pitch shift = 10ms, per-voice level/pan = 5ms, dryLevel/wetLevel = 10ms.
- Q: Should SC-008 cover all pitch shift modes, and are there architectural mandates for PhaseVocoder multi-voice efficiency? → A: (1) SC-008 is replaced by a per-mode CPU budget table: Simple < 1%, PitchSync < 3%, Granular < 5%, PhaseVocoder < 15% (all at 44.1kHz, block 256, 4 voices). (2) PhaseVocoder mode MUST use shared-analysis architecture: forward FFT runs once per block and the analysis spectrum is shared read-only across all voices; only phase state, modified spectrum buffer, synthesis iFFT scratch, and OLA buffer are per-voice (FR-020). (3) Each voice MUST have its own independent OLA buffer — sharing OLA across voices violates COLA and produces comb-filtering and metallic artifacts (FR-021).
- Q: What should `process()` do when called before `prepare()`? → A: Zero-fill `outputL` and `outputR` for `numSamples` samples and return immediately (safe no-op). This prevents undefined behavior in hosts that construct audio components before calling the setup lifecycle. `isPrepared()` is available for callers who need to check readiness explicitly.

## Background & Motivation

A harmonizer generates one or more pitch-shifted copies of an input signal, blending them with the original to create musical harmonies (Eventide, 1975). The HarmonizerEngine is the orchestration layer that composes all of the building blocks developed in Phases 1-3 into a complete, usable multi-voice harmony system.

The existing KrateDSP library already provides all required DSP components:

- **ScaleHarmonizer** (Phase 1, Layer 0): Diatonic interval computation for 8 scales + chromatic
- **PitchTracker** (Phase 3, Layer 1): Smoothed pitch detection with hysteresis and confidence gating
- **PitchShiftProcessor** (Layer 2): Per-voice pitch shifting with 4 quality modes (Simple, Granular, PitchSync, PhaseVocoder), including identity phase locking (Phase 2A) and spectral transient detection with phase reset (Phase 2B)
- **FormantPreserver** (Layer 2): Cepstral formant correction integrated within PitchShiftProcessor
- **OnePoleSmoother** (Layer 1): Exponential parameter smoothing for click-free transitions
- **DelayLine** (Layer 1): Per-voice onset delay offset

The HarmonizerEngine does not introduce new DSP algorithms. Its role is to orchestrate these components into a coherent multi-voice system with two harmony intelligence modes (Chromatic and Scalic), per-voice configuration, mono-to-stereo constant-power panning, and seamless real-time parameter changes.

### Architecture

The signal flow is mono input to stereo output. A single shared PitchTracker analyzes the input pitch. A shared ScaleHarmonizer computes diatonic intervals per voice. Each voice has its own PitchShiftProcessor, optional DelayLine, and smoothers for click-free level/pan/pitch transitions. Voice outputs are panned into stereo using the constant-power pan law and summed.

In PhaseVocoder mode, the forward FFT analysis of the input is performed once and the resulting spectrum is shared across all voices (see FR-020). Each voice then applies its own phase rotation and runs its own synthesis iFFT and OLA buffer independently (see FR-021).

```
Input ────────────────────────────────────────────────────── Dry Path ──> Mix
  |                                                                        |
  +──> PitchTracker (shared) ──> ScaleHarmonizer (shared)                  |
  |                                                                        |
  +──> [FFT Analysis: shared, once per block] ──> Analysis Spectrum        |
  |         |                                          |                   |
  |         v                                          v                   |
  +──> Voice 0: [DelayLine] -> [Phase Rotate + iFFT + OLA] -> [Level/Pan] ──>   |
  +──> Voice 1: [DelayLine] -> [Phase Rotate + iFFT + OLA] -> [Level/Pan] ──>  Sum
  +──> Voice 2: [DelayLine] -> [Phase Rotate + iFFT + OLA] -> [Level/Pan] ──>   |
  +──> Voice 3: [DelayLine] -> [Phase Rotate + iFFT + OLA] -> [Level/Pan] ──>   |
```

> **Note (FR-020 / E2)**: The shared FFT analysis path shown above is deferred to a follow-up spec (see FR-020 and plan.md R-001). Phase 1 implementation uses independent per-voice `PitchShiftProcessor` instances; each voice runs its own forward FFT. The diagram represents the target architecture when the shared-analysis optimization is implemented.

(In Simple, Granular, and PitchSync modes the shared FFT analysis path is unused; each PitchShiftProcessor operates independently on its own input copy as in the standard per-voice model.)

### Design Rationale

**Why Layer 3 (Systems)?** The HarmonizerEngine composes Layer 0, 1, and 2 components. Per the project's layered architecture, a system that orchestrates processors belongs at Layer 3. The existing UnisonEngine follows this same pattern.

**Why 4 voices maximum?** Four harmony voices cover the vast majority of musical harmony use cases (unison, thirds, fifths, octaves). The memory footprint for 4 voices in PhaseVocoder mode is approximately 168 KB (all pre-allocated in `prepare()`), which is well within acceptable limits. More voices would increase CPU cost linearly since each voice requires its own PitchShiftProcessor.

**Why mono input to stereo output?** A harmonizer processes a single monophonic source (voice, guitar lead) and generates stereo output via per-voice panning. This matches the signal flow of all commercial harmonizers (Eventide H3000, TC-Helicon VoiceTone, Boss VE-20). Stereo input could be supported by summing to mono before processing, but this is left to the caller.

**Why constant-power panning?** Constant-power (equal-power) panning maintains perceived loudness across the stereo field. The formula `leftGain = cos(angle)`, `rightGain = sin(angle)` where `angle = (pan + 1) * pi/4` ensures that `leftGain^2 + rightGain^2 = 1` at all pan positions. This produces -3 dB at center and 0 dB at extremes, matching the existing UnisonEngine's implementation. This is the standard approach derived from the trigonometric identity `cos^2(x) + sin^2(x) = 1` applied over a quarter-period sweep from 0 to pi/2.

**Why OnePoleSmoother for pitch transitions?** When the PitchTracker detects a new note in Scalic mode, the computed diatonic interval may change discretely (e.g., from +3 to +4 semitones). Applying this change instantaneously would cause an audible click in the pitch-shifted output. A OnePoleSmoother with a 10ms time constant provides a fast but artifact-free glide between intervals. This approach is standard in commercial harmonizers -- TC-Helicon's "Hybrid Shift" technology specifically uses smoothing algorithms to break up the mechanical precision when transitions between scale notes occur.

**Why micro-detuning?** When multiple voices share the same interval or are close together, slight pitch offsets (a few cents) create natural beating between voices, simulating the imprecision of multiple human singers. The beat frequency equals the frequency difference: at 660Hz, a 10-cent offset produces approximately 3.8Hz beating -- perceived as gentle ensemble width rather than as two distinct pitches. This is the same principle behind the Eventide H3000's celebrated "micropitch detuning" effect.

**Why shared-analysis FFT in PhaseVocoder mode?** All voices analyze the same input signal; running a forward FFT once and sharing the analysis spectrum eliminates 75% of forward FFT work for 4 voices. Additionally, all voices derive from identical analysis frames, ensuring perfect phase coherence, better stereo image, and consistent transient alignment across voices. Scratch buffers during synthesis remain strictly per-voice because each voice writes different phase-rotated output. Sharing them would produce data races and incorrect output. The rule is: share everything immutable (plans, lookup tables, analysis spectrum); duplicate only mutable per-voice state (phase accumulators, modified spectrum, iFFT scratch, OLA buffer).

**Why per-voice OLA buffers?** The Constant Overlap-Add (COLA) condition — which guarantees artifact-free reconstruction — only holds when overlap-adding frames from a single coherent signal. Each voice has a distinct pitch ratio and therefore distinct synthesis timing. Sharing an OLA buffer across voices violates COLA, producing phase interference, time-varying comb filtering at the hop rate, amplitude instability, and metallic smearing on attacks. The memory cost (approximately 16 KB for 4 voices) is trivial relative to the spectral buffers (~128 KB in PhaseVocoder mode), so there is no engineering justification for sharing OLA buffers.

### References

- Laroche & Dolson (1999), "New phase-vocoder techniques for pitch-shifting, harmonizing and other exotic effects", IEEE WASPAA
- de Cheveigne & Kawahara (2002), "YIN, a fundamental frequency estimator for speech and music", JASA
- CMU Loudness Concepts & Pan Laws (Oland & Dannenberg): constant-power panning derivation
- TC-Helicon VoiceTone Harmony documentation: multi-voice harmony design patterns
- Eventide H3000 Ultra-Harmonizer: micropitch detuning and diatonic harmony architecture

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Chromatic Harmony Generation (Priority: P1)

A plugin developer configures the HarmonizerEngine in Chromatic mode with one or more voices at fixed semitone intervals. The engine pitch-shifts the input by the specified intervals without any pitch tracking or scale awareness. This is the simplest operating mode and the foundation of all harmonizer functionality.

**Why this priority**: Chromatic mode exercises the core per-voice pitch-shifting pipeline (PitchShiftProcessor, level, pan, mixing) without depending on PitchTracker or ScaleHarmonizer. It validates the fundamental voice processing architecture and is independently useful for fixed-interval effects (octave up, perfect fifth, etc.).

**Independent Test**: Can be fully tested by feeding a known sinusoidal input at a specific frequency, setting voice intervals in chromatic mode, and measuring the output frequencies via spectral analysis. No pitch tracking or scale configuration required.

**Acceptance Scenarios**:

1. **Given** a HarmonizerEngine in Chromatic mode with 1 voice at +7 semitones (perfect fifth), **When** processing a 440Hz sine tone, **Then** the output contains a component at approximately 659Hz (440 * 2^(7/12)).
2. **Given** a HarmonizerEngine in Chromatic mode with 2 voices at +4 and +7 semitones, **When** processing a 440Hz sine tone, **Then** the output contains components at approximately 554Hz and 659Hz.
3. **Given** a HarmonizerEngine in Chromatic mode with 0 active voices, **When** processing input, **Then** the output contains only the dry signal (if dry level is enabled) or silence (if dry level is off).
4. **Given** a HarmonizerEngine in Chromatic mode, **When** the input note changes from A4 to C5, **Then** the voice intervals remain constant (e.g., always +7 semitones) regardless of input pitch.

---

### User Story 2 - Scalic (Diatonic) Harmony Generation (Priority: P1)

A plugin developer configures the HarmonizerEngine in Scalic mode with a key, scale, and per-voice diatonic intervals. The engine detects the input pitch, computes scale-correct intervals, and generates harmonies that are musically correct within the specified key and scale.

**Why this priority**: Scalic mode is the defining feature of a harmonizer (as opposed to a simple pitch shifter). It exercises the full pipeline including PitchTracker and ScaleHarmonizer integration. This is equally critical as Chromatic mode because it delivers the musical intelligence that makes a harmonizer useful.

**Independent Test**: Can be tested by feeding known pitched input (sine tones at specific note frequencies), configuring a key/scale, and verifying that the output pitch matches the expected diatonic interval for each input note.

**Acceptance Scenarios**:

1. **Given** a HarmonizerEngine in Scalic mode (C Major, 1 voice, 3rd above = diatonicSteps +2), **When** processing a 440Hz (A4) input, **Then** the harmony voice outputs approximately 523Hz (C5, +3 semitones from A4 in C Major).
2. **Given** a HarmonizerEngine in Scalic mode (C Major, 1 voice, 3rd above), **When** processing a 261.6Hz (C4) input, **Then** the harmony voice outputs approximately 329.6Hz (E4, +4 semitones from C4 in C Major).
3. **Given** a HarmonizerEngine in Scalic mode (C Major, 2 voices: 3rd above + 5th above), **When** processing a 440Hz (A4) input, **Then** the output contains components at approximately 523Hz (C5) and 659Hz (E5).
4. **Given** a HarmonizerEngine in Scalic mode, **When** the input pitch changes from A4 to B4, **Then** the harmony interval adjusts accordingly (3rd above B4 in C Major = D5, +3 semitones), with the transition occurring smoothly without clicks.

---

### User Story 3 - Per-Voice Pan and Stereo Output (Priority: P2)

A plugin developer configures per-voice pan positions to create spatial separation between harmony voices. The engine produces stereo output with each voice panned to its configured position using constant-power panning.

**Why this priority**: Stereo separation is essential for a usable harmonizer product. Overlapping harmony voices sound muddled in mono; panning creates clarity and spatial interest. This is the second priority because it enhances the usability of the core harmony generation.

**Independent Test**: Can be tested by setting distinct pan positions for multiple voices and measuring the left/right channel amplitude ratios. A voice panned hard left should appear only in the left channel; a centered voice should appear equally in both channels at -3 dB.

**Acceptance Scenarios**:

1. **Given** a voice panned hard left (pan = -1.0), **When** processing input, **Then** the voice output appears only in the left channel with zero signal in the right channel.
2. **Given** a voice panned hard right (pan = +1.0), **When** processing input, **Then** the voice output appears only in the right channel with zero signal in the left channel.
3. **Given** a voice panned to center (pan = 0.0), **When** processing input, **Then** the voice output appears equally in both channels, each at approximately -3 dB relative to a hard-panned signal (constant-power law).
4. **Given** 2 voices panned left (-0.5) and right (+0.5) respectively, **When** processing the same input, **Then** the left channel is dominated by voice 0 and the right channel by voice 1, with partial overlap in both.

---

### User Story 4 - Per-Voice Level and Dry/Wet Mix (Priority: P2)

A plugin developer configures per-voice levels (in dB) and global dry/wet levels to control the balance between harmony voices and the original signal.

**Why this priority**: Level control determines the musical balance of the harmony. Too loud and the harmonies overwhelm the original; too quiet and they are inaudible. Dry/wet mix is essential for parallel blending.

**Independent Test**: Can be tested by setting specific voice levels and dry/wet levels, then measuring the RMS amplitude of each component in the output.

**Acceptance Scenarios**:

1. **Given** a voice at 0 dB level, **When** processing input, **Then** the voice output amplitude matches the input amplitude (unity gain, subject to pitch-shifting processing gain).
2. **Given** a voice at -6 dB level, **When** processing input, **Then** the voice output amplitude is approximately half (-6 dB) of the 0 dB case.
3. **Given** dry level at 0 dB and wet level at 0 dB, **When** processing input with one voice, **Then** both the dry signal and the harmony voice are present at unity.
4. **Given** dry level at negative infinity (muted) and wet level at 0 dB, **When** processing input, **Then** only the harmony voices are audible with no dry signal.

---

### User Story 5 - Click-Free Transitions on Note Changes (Priority: P2)

When the input pitch changes in Scalic mode (causing diatonic intervals to change), or when the user adjusts voice parameters at runtime (interval, level, pan), the transitions must be smooth and free of audible clicks or discontinuities.

**Why this priority**: Clicks on note changes are the most common artifact in harmonizers and directly impact usability. Smoothed transitions are essential for production-quality output.

**Independent Test**: Can be tested by feeding a pitch sequence that causes interval changes and verifying that the output waveform contains no discontinuities (measured via peak sample-to-sample delta or by listening).

**Acceptance Scenarios**:

1. **Given** a Scalic mode voice tracking a note transition that changes the diatonic interval (e.g., C4 to D4 in C Major with a 3rd above -- +4 semitones changing to +3 semitones), **When** the PitchTracker commits the note change, **Then** the pitch shift smoothly transitions between the old and new semitone values with no sample-to-sample discontinuity exceeding normal signal variation.
2. **Given** a runtime change of voice level from 0 dB to -12 dB, **When** processing continues, **Then** the output level ramps smoothly over approximately 5-10ms with no audible click.
3. **Given** a runtime change of voice pan from -1.0 to +1.0, **When** processing continues, **Then** the stereo position sweeps smoothly with no discontinuity.

---

### User Story 6 - Per-Voice Micro-Detuning for Ensemble Width (Priority: P3)

A plugin developer configures per-voice micro-detuning (in cents) to create a natural ensemble width effect. When multiple voices share the same interval, small detuning offsets create beating that simulates the imprecision of multiple human singers.

**Why this priority**: Micro-detuning adds naturalness and width to harmonies, making them sound less robotic. It is lower priority because it is an enhancement over basic harmony generation, not a core requirement.

**Independent Test**: Can be tested by setting a voice to the same interval as another voice but with +5 cents detune, and verifying a slight frequency offset that creates periodic beating.

**Acceptance Scenarios**:

1. **Given** a voice at +7 semitones with +5 cents detune, **When** processing a 440Hz input, **Then** the output frequency is approximately 659Hz * 2^(5/1200), slightly higher than a non-detuned +7 semitone voice.
2. **Given** two voices both at +7 semitones, one with 0 cents detune and one with +10 cents detune, **When** processing input, **Then** the combined output exhibits periodic amplitude modulation (beating) at a rate proportional to the frequency difference.

---

### User Story 7 - Per-Voice Onset Delay (Priority: P3)

A plugin developer configures per-voice onset delay (0-50ms) to simulate natural timing differences between singers. This creates a more realistic ensemble effect by staggering the attack of each harmony voice.

**Why this priority**: Onset delay adds realism but is a secondary enhancement. The harmonizer functions well without it.

**Independent Test**: Can be tested by measuring the time offset between the dry signal and a delayed voice's output onset.

**Acceptance Scenarios**:

1. **Given** a voice with 10ms onset delay, **When** processing a transient input, **Then** the voice output is delayed by approximately 10ms relative to the dry signal (measured at the sample level as sampleRate * 0.01 samples).
2. **Given** a voice with 0ms onset delay, **When** processing input, **Then** the voice output is time-aligned with the input (subject only to the pitch shifter's inherent latency).

---

### User Story 8 - Latency Reporting (Priority: P3)

The engine reports its total latency in samples so that the host DAW can compensate for plugin delay. The latency is determined by the selected pitch shift mode and is consistent across all voices.

**Why this priority**: Correct latency reporting is required for DAW delay compensation but does not affect the core DSP functionality.

**Independent Test**: Can be tested by querying `getLatencySamples()` for each pitch shift mode and verifying the returned value matches the underlying PitchShiftProcessor's latency.

**Acceptance Scenarios**:

1. **Given** PitchShiftMode set to Simple, **When** querying latency, **Then** `getLatencySamples()` returns 0.
2. **Given** PitchShiftMode set to PhaseVocoder, **When** querying latency, **Then** `getLatencySamples()` returns the PhaseVocoder latency (approximately 5120 samples at 44.1kHz).
3. **Given** a mode change from Simple to PhaseVocoder, **When** querying latency after the change, **Then** the reported latency updates to reflect the new mode.

---

### User Story 9 - Pitch Detection Feedback for UI (Priority: P4)

The engine exposes the current detected pitch, MIDI note, and confidence value so that a plugin UI can display pitch information to the user (e.g., a tuner display or note name indicator).

**Why this priority**: UI feedback is a nice-to-have that improves the user experience but does not affect audio processing.

**Independent Test**: Can be tested by feeding known pitched input and verifying that `getDetectedPitch()`, `getDetectedNote()`, and `getPitchConfidence()` return expected values.

**Acceptance Scenarios**:

1. **Given** a 440Hz input in Scalic mode, **When** querying detected pitch, **Then** `getDetectedPitch()` returns approximately 440Hz and `getDetectedNote()` returns 69 (A4).
2. **Given** silence input, **When** querying pitch confidence, **Then** `getPitchConfidence()` returns a low value (below 0.5).

---

### Edge Cases

- What happens when `numVoices` is set to 0? The engine produces only the dry signal (scaled by dry level) with no harmony processing. The PitchTracker is not fed audio in Scalic mode (optimization: skip pitch analysis when no voices are active).
- What happens when all voices have their level set to negative infinity (muted)? The wet output is silence; only the dry signal passes through.
- What happens when the input is silence (all zeros)? No artifacts, no NaN values, no denormals. The PitchTracker reports invalid pitch and all voices produce silence.
- What happens when the PitchTracker has low confidence (unvoiced input) in Scalic mode? The last committed note is held (per PitchTracker spec), so harmony voices continue at their last valid interval. This prevents chaotic pitch jumps during breaths or unvoiced consonants.
- What happens when the pitch shift mode is changed at runtime? All per-voice PitchShiftProcessors are reconfigured, `reset()` is called on each, and latency reporting updates. There may be a brief audio discontinuity during mode transitions, which is acceptable.
- What happens when `prepare()` is called with different sample rate or block size? All internal components are re-prepared and state is reset.
- What happens when `setNumVoices()` increases from 2 to 4 at runtime? Voices 2 and 3 are activated with their current configuration. Their PitchShiftProcessors are already pre-allocated (all 4 are always allocated in `prepare()`), so no runtime allocation occurs.
- What happens when the key or scale changes at runtime in Scalic mode? The ScaleHarmonizer is reconfigured. The next PitchTracker commit triggers recomputation of diatonic intervals. The pitch smoother handles the transition click-free.
- What happens when a voice interval is changed at runtime? The new interval takes effect on the next processing block. In Scalic mode, the diatonic shift is recomputed; in Chromatic mode, the raw semitone value updates. The pitch smoother handles the transition.
- What happens when the input frequency is outside the PitchDetector range (below 50Hz or above 4000Hz) in Scalic mode? The PitchTracker reports invalid pitch and the last valid note is held.
- What happens when `process()` is called before `prepare()`? The method zero-fills both output buffers for `numSamples` samples and returns immediately without accessing any internal state. This is the safe no-op contract (see FR-015). `isPrepared()` returns false in this state and callers may check it explicitly.

## Requirements *(mandatory)*

### Functional Requirements

- **FR-001**: The HarmonizerEngine MUST support up to 4 independent harmony voices (`kMaxVoices = 4`). The number of active voices MUST be configurable at runtime via `setNumVoices(count)` where count is clamped to [0, 4]. All 4 voice structures MUST be pre-allocated in `prepare()`. A `getNumVoices()` query method MUST be provided so callers can inspect the current active voice count without storing it externally.
- **FR-002**: The HarmonizerEngine MUST support two harmony modes via `setHarmonyMode()`:
  - **Chromatic**: Each voice's interval is treated as a fixed semitone shift. PitchTracker and ScaleHarmonizer are bypassed. No pitch detection overhead.
  - **Scalic**: The shared PitchTracker analyzes input pitch per block. The shared ScaleHarmonizer computes the diatonic interval for each active voice based on the detected note, configured key, and scale. The semitone shift varies per input note to maintain scale correctness.
- **FR-003**: Each voice MUST have the following independently configurable parameters:
  - **Interval** (`setVoiceInterval(voice, diatonicSteps)`): Diatonic steps in Scalic mode, raw semitones in Chromatic mode. Range: [-24, +24].
  - **Level** (`setVoiceLevel(voice, dB)`): Output level in decibels. Range: [-60, +6] dB. Values at or below -60 dB are treated as mute (zero gain).
  - **Pan** (`setVoicePan(voice, pan)`): Stereo position. Range: [-1.0, +1.0] where -1.0 = hard left, 0.0 = center, +1.0 = hard right.
  - **Delay** (`setVoiceDelay(voice, ms)`): Onset delay offset in milliseconds. Range: [0, 50] ms.
  - **Detune** (`setVoiceDetune(voice, cents)`): Micro-detuning in cents, added on top of the computed interval. Range: [-50, +50] cents.
- **FR-004**: The HarmonizerEngine MUST accept mono input and produce stereo output via `process(input, outputL, outputR, numSamples)`. The output buffers MUST be independent (not aliased with input or each other).
- **FR-005**: Per-voice stereo panning MUST use the constant-power pan law: `leftGain = cos((pan + 1) * pi/4)`, `rightGain = sin((pan + 1) * pi/4)`. This formula ensures that `leftGain^2 + rightGain^2 = 1` for all pan values, maintaining constant perceived loudness across the stereo field.
- **FR-006**: The HarmonizerEngine MUST provide global configuration:
  - `setKey(rootNote)`: Root note for Scalic mode (0=C through 11=B).
  - `setScale(ScaleType)`: Scale type for Scalic mode (Major, NaturalMinor, HarmonicMinor, MelodicMinor, Dorian, Mixolydian, Phrygian, Lydian, Chromatic).
  - `setPitchShiftMode(PitchMode)`: Pitch shifting algorithm for all voices (Simple, Granular, PitchSync, PhaseVocoder).
  - `setFormantPreserve(bool)`: Enable/disable formant preservation for all voices (effective in Granular and PhaseVocoder modes).
  - `setDryLevel(dB)`: Dry signal level in decibels.
  - `setWetLevel(dB)`: Wet (harmony) signal level in decibels.
  - For `setDryLevel` and `setWetLevel`, any value converted to a linear gain below approximately 0.001 (approximately -60 dB) is effectively inaudible. There is no hard mute threshold for global levels; `dbToGain()` handles the conversion.
- **FR-007**: All parameter changes (interval, level, pan, dry/wet, pitch shift amount) MUST be smoothed using `OnePoleSmoother` instances to prevent audible clicks or discontinuities. The smoothing time constants are:
  - **Pitch shift transitions**: 10ms (diatonic interval changes on note events)
  - **Per-voice level and pan**: 5ms (voice-to-voice balance adjustments are typically small in magnitude)
  - **dryLevel and wetLevel**: 10ms (dry/wet swings affect the entire signal energy and span the full 0→1 range; faster smoothing causes roughness during automation and moving comb filtering between latency-mismatched dry and processed paths)
  - `dryLevel` and `wetLevel` MUST be smoothed independently via their own `OnePoleSmoother` instances applied to their linear gain values. The pattern is: `dryGain = dryLevelSmoother.process(); wetGain = wetLevelSmoother.process(); output = dryGain * dry + wetGain * wet`. Smoothing a single mix ratio (`lerp(dry, wet, mix)`) is FORBIDDEN as it makes equal-power compensation harder and automation nonlinear.
  - Voice onset delay (`setVoiceDelay`) is NOT smoothed; changes take effect immediately on the next process() call. Delay changes are infrequent user interactions, not automation targets, and the `DelayLine`'s linear interpolation already prevents sample-level discontinuities at the output.
- **FR-008**: In Scalic mode, the HarmonizerEngine MUST feed the input audio block to the shared PitchTracker via `pushBlock()` once per processing call. The detected MIDI note MUST be used with `ScaleHarmonizer::calculate()` to compute per-voice semitone shifts. When the PitchTracker reports invalid pitch (low confidence), the last valid intervals MUST be held.
- **FR-009**: In Chromatic mode, the PitchTracker MUST NOT be fed audio (no unnecessary CPU cost). Voice intervals are used directly as semitone shifts without pitch detection or scale computation.
- **FR-010**: Per-voice micro-detuning (in cents) MUST be added on top of the computed pitch shift (diatonic or chromatic) before being applied to the PitchShiftProcessor. The total shift applied is `computedSemitones + (detuneCents / 100.0)`.
- **FR-011**: Per-voice onset delay MUST be implemented using a DelayLine per voice. The delay is configurable from 0 to 50ms. When delay is 0ms, the DelayLine is bypassed (no processing overhead for the zero-delay case).
- **FR-012**: The HarmonizerEngine MUST report its latency in samples via `getLatencySamples()`. The latency is determined by the pitch shift mode and MUST match the value returned by the underlying PitchShiftProcessor for the configured mode.
- **FR-013**: The HarmonizerEngine MUST provide read-only query methods for UI feedback:
  - `getDetectedPitch()`: Returns the smoothed detected frequency in Hz from the PitchTracker.
  - `getDetectedNote()`: Returns the committed MIDI note number from the PitchTracker.
  - `getPitchConfidence()`: Returns the raw confidence value from the PitchTracker.
- **FR-014**: The HarmonizerEngine MUST provide `prepare(sampleRate, maxBlockSize)` and `reset()` lifecycle methods. `prepare()` MUST initialize all internal components (PitchTracker, ScaleHarmonizer, all 4 PitchShiftProcessors, all 4 DelayLines, all per-voice smoothers for level/pan/pitch, and the two global smoothers for dryLevel and wetLevel) and pre-allocate scratch buffers. `reset()` MUST clear all processing state without changing configuration.
- **FR-015**: The `process()` method MUST be fully real-time safe: zero heap allocations, no locks, no exceptions, no I/O. All buffers MUST be pre-allocated in `prepare()`. If `process()` is called before `prepare()` (i.e., `isPrepared()` returns false), it MUST zero-fill `outputL` and `outputR` for `numSamples` samples and return immediately without accessing any uninitialized internal state. This safe no-op contract prevents undefined behavior in hosts that construct audio components before calling the setup lifecycle.
- **FR-016**: The HarmonizerEngine MUST reside at Layer 3 (Systems) of the DSP architecture, depending only on Layer 0 (ScaleHarmonizer, pitch_utils), Layer 1 (PitchTracker, OnePoleSmoother, DelayLine), and Layer 2 (PitchShiftProcessor) components. No Layer 4 dependencies.
- **FR-017**: The processing flow for each block MUST follow this order:
  1. Push input to PitchTracker (Scalic mode only).
  2. For each active voice, compute the semitone shift (from ScaleHarmonizer in Scalic mode, or directly from the interval in Chromatic mode), add micro-detune, and smooth the total shift.
  3. For each active voice, process input through the voice's DelayLine (if delay > 0).
  4. For each active voice, process the delayed input through the voice's PitchShiftProcessor. The pitch smoother target is set once per block (after interval computation in step 2). The smoothed semitone value at the start of the block is passed to `PitchShiftProcessor::setSemitones()` once per block. Per-sample smoother advancement for pitch inside the block loop is not required because `PitchShiftProcessor` has its own internal 10ms smoother that handles intra-block interpolation (see research.md R-003).
  5. For each active voice, apply smoothed level gain and constant-power pan, and accumulate into stereo output buffers.
  6. Advance the dryLevel smoother one step and apply the smoothed dry gain to both output channels: `outputL += input[s] * dryGain; outputR += input[s] * dryGain`.
  7. Advance the wetLevel smoother one step and apply the smoothed wet gain to the entire summed harmony bus (outputL and outputR as accumulated across all voices in step 5). wetLevel is a single master fader over all harmony output; it does NOT multiply per-voice before accumulation. Both smoothers are advanced per-sample inside the block loop, not once per block.
- **FR-018**: When `numVoices` is set to 0, the engine MUST produce only the dry signal without executing any voice processing or pitch tracking. This is an optimization to avoid unnecessary CPU consumption.
- **FR-019**: The `PitchShiftProcessor` instances are non-copyable but movable. The HarmonizerEngine MUST store them in a way compatible with this constraint (e.g., `std::array` of `PitchShiftProcessor` is valid since the array is default-constructed and elements are not copied).
- **FR-020**: In PhaseVocoder mode, the HarmonizerEngine MUST use a shared-analysis architecture to avoid redundant FFT work. The forward FFT analysis of the input block MUST be performed exactly once per block and the resulting analysis spectrum MUST be shared across all active voices. Only the per-voice phase state and synthesis iFFT are duplicated. Specifically:
  - **SHARE across all voices**: FFT plan, lookup tables, twiddle factors, analysis spectrum (the result of the single forward FFT).
  - **PER-VOICE (never shared)**: phase accumulator state, modified spectrum buffer (each voice applies its own pitch-ratio phase rotation), synthesis iFFT scratch buffer, OLA (overlap-add) buffer.
  - Scratch buffers used during synthesis transforms MUST NOT be shared because concurrent per-voice synthesis overwrites them.
  - This architecture eliminates 75% of the forward FFT cost for 4 voices and reduces cache pressure significantly. It also ensures all voices derive from identical analysis frames, producing better phase coherence, stereo image, and transient alignment.
- **FR-021**: Each voice MUST have its own independent OLA (overlap-add) buffer. Sharing an OLA buffer across voices is FORBIDDEN. The COLA (Constant Overlap-Add) condition only holds per-signal; each voice has a distinct pitch ratio, phase evolution, and synthesis timing. Sharing an OLA buffer causes phase interference, time-varying comb filtering, unstable amplitude, and metallic artifacts. The memory cost of per-voice OLA buffers is trivial (approximately 1024 samples × 4 bytes × 4 voices = 16 KB). The golden rule: FFT analysis can be shared; time-domain reconstruction (OLA) must remain per-voice.

### Key Entities

- **HarmonizerEngine**: The primary Layer 3 system component. Orchestrates shared pitch analysis, per-voice interval computation, per-voice pitch shifting, level/pan mixing, and mono-to-stereo output. Manages lifecycle of all sub-components.
- **HarmonyMode**: An enumeration with two values -- Chromatic (fixed semitone shifts, no pitch tracking) and Scalic (diatonic intervals in a configured key/scale, with pitch tracking).
- **Voice**: An internal structure within HarmonizerEngine representing one harmony voice. Contains a PitchShiftProcessor, a DelayLine, smoothers for level/pan/pitch, per-voice configuration (interval, level, pan, delay, detune), and in PhaseVocoder mode: its own phase accumulator state, modified spectrum buffer, synthesis iFFT scratch buffer, and OLA buffer. Not directly exposed in the public API.
- **Shared Analysis Engine** (PhaseVocoder mode only): The single forward FFT pass applied to the input block once per processing call. The resulting analysis spectrum is read-only after creation and is referenced by all active voices for their phase rotation step. This component is managed by HarmonizerEngine directly, not by any individual Voice.
- **Processing Pipeline**: The fixed-order sequence of operations per audio block: pitch analysis, (PhaseVocoder: shared FFT analysis), per-voice interval computation, delay, pitch shift / phase rotation + iFFT + OLA, level/pan application, stereo mixing, dry/wet blending.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: In Chromatic mode with 1 voice at +7 semitones, a 440Hz input produces a harmony output within 2Hz of the expected 659.3Hz (440 * 2^(7/12)), measured via peak frequency in the output spectrum.
- **SC-002**: In Scalic mode (C Major, 3rd above), an A4 (440Hz) input produces a harmony output within 2Hz of C5 (523.3Hz, +3 semitones from A in C Major), and a C4 (261.6Hz) input produces harmony within 2Hz of E4 (329.6Hz, +4 semitones).
- **SC-003**: Multi-voice output (2 voices at different intervals) contains both expected frequency components in the output spectrum, each within 2Hz of expected values.
- **SC-004**: A voice panned hard left (pan = -1.0) produces zero signal in the right channel (below -80 dB relative to left channel). A voice panned hard right (pan = +1.0) produces zero signal in the left channel.
- **SC-005**: A voice panned to center (pan = 0.0) produces equal amplitude in both channels, with each channel at -3 dB (+/- 0.5 dB) relative to a hard-panned signal.
- **SC-006**: Pitch transition smoothing: when the input changes from one note to another in Scalic mode, the maximum sample-to-sample delta in the output does not exceed 2x the maximum delta of a steady-state signal (no click artifacts).
- **SC-007**: Level smoothing: when voice level changes from 0 dB to -12 dB, the output amplitude ramps over at least 200 samples (approximately 4.5ms at 44.1kHz) with no instantaneous step.
- **SC-008**: CPU cost is a property of the pitch-shift algorithm times voice count and MUST be measured per mode. The following per-mode budgets apply with 4 active voices at 44.1kHz, block size 256:

  | Mode | Algorithm Class | CPU Budget (4 voices) | Notes |
  |------|----------------|-----------------------|-------|
  | Simple | Time-domain | < 1% | Lowest quality |
  | PitchSync | Time-domain | < 3% | Real-time default |
  | Granular | Hybrid | < 5% | Mid-quality |
  | PhaseVocoder | FFT | < 15% | Quality mode; mandates shared-analysis architecture (see FR-020) |

  Each mode MUST be benchmarked independently. The orchestration overhead of the engine itself (pitch tracking, interval computation, panning, smoothing) MUST account for less than 1% CPU regardless of mode.
- **SC-009**: All processing methods perform zero heap allocations, verified by inspection (no `new`, `delete`, `malloc`, `free`, `vector::push_back`, or other allocating operations in the `process()` path).
- **SC-010**: `getLatencySamples()` returns a value matching the underlying PitchShiftProcessor's reported latency for the configured mode (verified by comparing against a standalone PitchShiftProcessor configured with the same mode).
- **SC-011**: Feeding silence (all-zero input) to the engine produces all-zero output with no NaN, infinity, or denormal values in either output channel.
- **SC-012**: Micro-detune of +10 cents on a voice at +7 semitones produces an output frequency approximately 3.8Hz higher than a non-detuned +7 semitone voice at 440Hz input (expected: 659.3 * (2^(10/1200) - 1) = approximately 3.8Hz difference).

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- The PitchShiftProcessor supports all 4 modes (Simple, Granular, PitchSync, PhaseVocoder) as documented, with `setSemitones()` accepting float values that encompass both whole semitones and fractional cents.
- The PitchShiftProcessor's `process()` accepts separate input/output buffers and supports in-place processing.
- The PitchShiftProcessor is non-copyable but movable, and uses a pImpl pattern. Default construction is valid (creates an uninitialized instance that requires `prepare()` before use).
- The FormantPreserver is integrated within PitchShiftProcessor (controlled via `setFormantPreserve(bool)`) rather than being a separate processing step at the HarmonizerEngine level.
- The PitchTracker's `pushBlock()` internally triggers detection at the appropriate hop intervals. The HarmonizerEngine calls `pushBlock()` once per audio block with the full input buffer.
- The ScaleHarmonizer's `calculate()` method accepts an integer MIDI note and diatonic steps, returning a `DiatonicInterval` with the semitone shift. The `getSemitoneShift()` convenience method accepts frequency in Hz.
- The OnePoleSmoother's `configure()` accepts a time in milliseconds and a sample rate. `setTarget()` sets the target value, and `process()` returns the next smoothed sample.
- The DelayLine's `prepare()` accepts sample rate and maximum delay time in seconds. `write()` and `read()`/`readLinear()` operate on individual samples.
- Sample rates of 44100, 48000, 88200, 96000, and 192000 Hz are all supported.
- Maximum block size of 8192 samples is supported (matching PitchShiftProcessor's constraint).
- The dry signal path adds the input equally to both stereo output channels (mono-to-dual-mono for the dry path).
- dB-to-linear conversion uses the standard formula: `linear = 10^(dB/20)` for amplitude, or equivalently `std::pow(10.0f, dB / 20.0f)`.
- All parameter setters are safe to call between `process()` calls from the same thread (typical audio thread usage). Thread safety across threads is not required -- the host/plugin framework ensures parameter changes are serialized with processing.
- The default harmony mode after construction is `HarmonyMode::Chromatic`. Callers must explicitly call `setHarmonyMode(HarmonyMode::Scalic)` to enable pitch-tracked diatonic harmony.

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that MUST be reused (not reimplemented):**

| Component | Location | Relevance |
|-----------|----------|-----------|
| `PitchShiftProcessor` | `dsp/include/krate/dsp/processors/pitch_shift_processor.h` (L2) | Per-voice pitch shifting. 4 modes: Simple, Granular, PitchSync, PhaseVocoder. Non-copyable, movable. Uses pImpl. Includes `setFormantPreserve()`, `getLatencySamples()`. |
| `PitchTracker` | `dsp/include/krate/dsp/primitives/pitch_tracker.h` (L1) | Shared pitch detection with 5-stage pipeline. `pushBlock()`, `getMidiNote()`, `getFrequency()`, `getConfidence()`, `isPitchValid()`. |
| `ScaleHarmonizer` | `dsp/include/krate/dsp/core/scale_harmonizer.h` (L0) | Diatonic interval computation. `setKey()`, `setScale()`, `calculate()`, `getSemitoneShift()`. 8 scales + chromatic. |
| `OnePoleSmoother` | `dsp/include/krate/dsp/primitives/smoother.h` (L1) | Click-free parameter transitions. Per-voice smoothers for pitch, level, pan. `configure()`, `setTarget()`, `process()`. |
| `DelayLine` | `dsp/include/krate/dsp/primitives/delay_line.h` (L1) | Per-voice onset delay. `prepare()`, `write()`, `readLinear()`. Non-copyable, movable. |
| `PitchMode` enum | `dsp/include/krate/dsp/processors/pitch_shift_processor.h` (L2) | Pitch shift mode selector (Simple, Granular, PitchSync, PhaseVocoder). Reuse directly. |
| `ScaleType` enum | `dsp/include/krate/dsp/core/scale_harmonizer.h` (L0) | Scale type selector (Major, NaturalMinor, ..., Chromatic). Reuse directly. |
| `UnisonEngine` | `dsp/include/krate/dsp/systems/unison_engine.h` (L3) | **Reference pattern** for multi-voice processing, constant-power panning (`cos/sin` with `(pan+1)*pi/4` angle), and gain compensation. Not composed directly. |
| `semitonesToRatio()` | `dsp/include/krate/dsp/core/pitch_utils.h` (L0) | Not needed directly since PitchShiftProcessor accepts semitones via `setSemitones()`. |
| `frequencyToMidiNote()` | `dsp/include/krate/dsp/core/pitch_utils.h` (L0) | Not needed directly; PitchTracker handles Hz-to-MIDI internally. |

**ODR check performed**: Searched for `HarmonizerEngine` and `HarmonyMode` across all code files. No existing implementations found in source code. All matches are in specification/research documents only. No ODR risk.

**Search Results Summary**: No existing `HarmonizerEngine` or `HarmonyMode` types exist in the codebase. The file path `dsp/include/krate/dsp/systems/harmonizer_engine.h` is unused. The `PitchMode` and `ScaleType` enums already exist and will be reused directly.

### Forward Reusability Consideration

*This is a Layer 3 (Systems) component. It orchestrates lower-layer components into a complete system.*

**Downstream consumers (known from roadmap):**
- Iterum plugin (Layer 4 effect) -- will wrap HarmonizerEngine for the harmonizer delay mode
- Potential standalone harmonizer plugin
- Any future pitch-harmony effect

**Potential shared components** (preliminary, refined in plan.md):
- The `HarmonyMode` enum could be extracted if multiple Layer 3/4 systems need harmony mode selection
- The constant-power panning utility (cos/sin formula) is already used in UnisonEngine and TapManager; a shared `constantPowerPan()` utility could be extracted to Layer 0 if a fourth consumer appears

**Sibling features at same layer** (if known):
- `UnisonEngine` (L3) -- similar multi-voice architecture, reference for panning and voice management patterns
- `GranularEngine` (L3) -- similar multi-voice scheduling, different use case

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
| FR-001 | MET | `harmonizer_engine.h:72` -- `kMaxVoices = 4`; line 301-303 `setNumVoices()` clamps to [0,4]; `std::array<Voice, kMaxVoices>` pre-allocates all 4. Test `FR-001 getNumVoices clamps correctly` passes (5 assertions). |
| FR-002 | MET | `harmonizer_engine.h:44-47` -- `HarmonyMode` enum with `Chromatic=0, Scalic=1`; lines 197-202 Scalic feeds PitchTracker; lines 215-218 Chromatic uses raw semitones. Tests SC-001 (chromatic) and SC-002 (scalic) pass. |
| FR-003 | MET | `harmonizer_engine.h:354-398` -- setVoiceInterval clamps [-24,+24], setVoiceLevel clamps [-60,+6] with mute threshold, setVoicePan clamps [-1,+1], setVoiceDelay clamps [0,50], setVoiceDetune clamps [-50,+50]. Clamping tests pass. |
| FR-004 | MET | `harmonizer_engine.h:181` -- `process(const float* input, float* outputL, float* outputR, std::size_t numSamples)` mono-in stereo-out. All tests use this signature. |
| FR-005 | MET | `harmonizer_engine.h:270-272` -- `angle = (pan+1)*kPi*0.25f; leftGain = cos(angle); rightGain = sin(angle)`. Tests SC-004 (hard left/right below -80dB) and SC-005 (center at -3dB +/-0.5dB) pass. |
| FR-006 | MET | `harmonizer_engine.h:296-347` -- All global setters: setHarmonyMode, setNumVoices, setKey, setScale, setPitchShiftMode, setFormantPreserve, setDryLevel, setWetLevel. All exercised in tests. |
| FR-007 | MET | `harmonizer_engine.h:84-87` -- kPitchSmoothTimeMs=10, kLevelSmoothTimeMs=5, kPanSmoothTimeMs=5, kDryWetSmoothTimeMs=10. Per-voice smoothers configured at lines 118-125; dry/wet smoothers at 124-125. Tests SC-007 pass. |
| FR-008 | MET | `harmonizer_engine.h:197-201` -- PitchTracker fed once per block in Scalic mode; lastDetectedNote_ holds last valid note. Test `FR-008 hold last note on silence` passes. |
| FR-009 | MET | `harmonizer_engine.h:197` -- PitchTracker only fed when `harmonyMode_ == Scalic`. Test `Chromatic mode getDetectedPitch returns 0` passes: getDetectedPitch()=0, getDetectedNote()=-1. |
| FR-010 | MET | `harmonizer_engine.h:218,224-225` -- detuneCents/100 added to interval shift. Test SC-012 passes: measured offset within 1Hz of expected 3.81Hz. |
| FR-011 | MET | `harmonizer_engine.h:249-258` -- DelayLine write/readLinear when delayMs>0; std::copy bypass when 0. Test `onset delay 10ms delays by ~441 samples` and `0ms time-aligned` pass. |
| FR-012 | MET | `harmonizer_engine.h:425-428` -- returns `voices_[0].pitchShifter.getLatencySamples()` if prepared, 0 otherwise. Tests SC-010 pass for all 4 modes. |
| FR-013 | MET | `harmonizer_engine.h:406-420` -- getDetectedPitch/getDetectedNote/getPitchConfidence delegate to PitchTracker. Test `FR-013 query methods after Scalic processing` passes. |
| FR-014 | MET | `harmonizer_engine.h:97-132` -- prepare() initializes all components, sets prepared_=true. reset() at 135-164 resets all components. Tests: isPrepared, reset, re-prepare all pass. |
| FR-015 | MET | `harmonizer_engine.h:183-188` -- zero-fills outputs and returns if !prepared_. Test `process before prepare zero-fills outputs (FR-015)` passes. |
| FR-016 | MET | `harmonizer_engine.h:22-28` -- includes only L0 (scale_harmonizer, db_utils, math_constants), L1 (pitch_tracker, smoother, delay_line), L2 (pitch_shift_processor). No L3/L4/external headers. |
| FR-017 | MET | `harmonizer_engine.h:196-288` -- Processing order: PitchTracker push -> interval compute -> delay -> pitch shift -> level/pan accumulate -> dry/wet blend per-sample. Wet applied to harmony bus after accumulation. |
| FR-018 | MET | `harmonizer_engine.h:195` -- `if (numActiveVoices_ > 0)` gates all voice processing and pitch tracking. Tests `FR-018 numVoices=0` and edge case test both pass. |
| FR-019 | MET | `harmonizer_engine.h:463` -- `std::array<Voice, kMaxVoices>` stores PitchShiftProcessors directly. No copying occurs. Compilation succeeds. |
| FR-020 | DEFERRED | Per plan.md R-001: shared-analysis FFT deferred. PitchShiftProcessor pImpl API doesn't support external analysis injection. Follow-up spec required for Layer 2 API change. See research.md R-011. |
| FR-021 | DEFERRED-COUPLED | Constrains FR-020's architecture. Each voice has own PitchShiftProcessor with own OLA buffer. Follow-up spec for FR-020 MUST include OLA isolation tests. |
| SC-001 | MET | Test `SC-001 chromatic +7 semitones 440Hz produces 659Hz` passes. Threshold: within 2Hz (matches spec). |
| SC-002 | MET | Test `SC-002 scalic C Major 3rd above A4 produces C5` passes (523.25Hz within 2Hz). Test `SC-002 scalic C Major 3rd above C4 produces E4` passes (329.63Hz within 2Hz). |
| SC-003 | MET | Test `SC-003 two voices produce two frequency components` passes. Both frequencies within 2Hz of expected. |
| SC-004 | MET | Tests `SC-004 hard left pan right channel below -80dB` and `hard right pan left channel below -80dB` pass. Threshold: -80dB (matches spec). |
| SC-005 | MET | Test `SC-005 center pan both channels equal at -3dB` passes. Left/right equal within 0.01f; ratio -3dB +/-0.5dB (matches spec). |
| SC-006 | MET | Test `SC-006 pitch transition C4 to D4 smooth` passes. transMaxDelta <= 2x steadyMaxDelta (matches spec). |
| SC-007 | MET | Test `SC-007 level change ramps over 200+ samples` passes. Test `pan change ramps smoothly` passes. |
| SC-008 | PARTIAL | Simple: 0.7% (MET, <1%). Granular: <5% (MET). PhaseVocoder: ~6.9% (MET, <15%). Orchestration: ~0.04% (MET, <1%). **PitchSync: ~26.4% (NOT MET, budget <3%)**. PitchSync overage is Layer 2 per-voice YIN autocorrelation. See research.md R-011. |
| SC-009 | MET | Code inspection of process() body: no new/delete/malloc/free/push_back/resize/reserve. All buffers pre-allocated in prepare(). Test `SC-009 zero allocations` documents verification. |
| SC-010 | MET | Test `SC-010 getLatencySamples matches PitchShiftProcessor` passes for all 4 modes. Unprepared returns 0. |
| SC-011 | MET | Test `SC-011 silence input produces silence output` passes: no NaN, no Inf, no denormals, RMS < 1e-6. |
| SC-012 | MET | Test `SC-012 detune +10 cents frequency offset` passes: measured within 1Hz of expected 3.81Hz. Test `detune beating` passes: modulation depth >0.3, 4+ zero crossings. |

**Status Key:**
- MET: Requirement verified against actual code and test output with specific evidence
- NOT MET: Requirement not satisfied (spec is NOT complete)
- PARTIAL: Partially met with documented gap and specific evidence of what IS met
- DEFERRED: Explicitly moved to future work with user approval

### Completion Checklist

*All items must be checked before claiming completion:*

- [X] Each FR-xxx row was verified by re-reading the actual implementation code (not from memory)
- [X] Each SC-xxx row was verified by running tests or reading actual test output (not assumed)
- [X] Evidence column contains specific file paths, line numbers, test names, and measured values
- [X] No evidence column contains only generic claims like "implemented", "works", or "test passes"
- [X] No test thresholds relaxed from spec requirements
- [X] No placeholder values or TODO comments in new code
- [X] No features quietly removed from scope
- [X] User would NOT feel cheated by this completion claim

### Honest Assessment

**Overall Status**: PARTIAL

**Documented gaps:**
- Gap 1: SC-008 PitchSync mode measures ~26.4% CPU vs <3% budget. The overage is caused by Layer 2 per-voice YIN autocorrelation inside PitchShiftProcessor, not optimizable at the HarmonizerEngine (Layer 3) level. See research.md R-011.
- Gap 2: FR-020 DEFERRED -- shared-analysis FFT requires a Layer 2 API change to PitchShiftProcessor's pImpl interface. Independent per-voice instances are functionally correct. Follow-up spec required per plan.md R-001.
- Gap 3: FR-021 DEFERRED-COUPLED -- constrains FR-020's architecture. Will be verified when FR-020 is implemented. Follow-up spec MUST include OLA isolation tests.

**Recommendation**: (1) File a follow-up spec to optimize PitchSync mode's YIN autocorrelation at Layer 2, bringing SC-008 PitchSync within the <3% budget. (2) File a follow-up spec for FR-020 shared-analysis FFT architecture, including FR-021 OLA isolation tests. Both are Layer 2 concerns outside HarmonizerEngine's scope.
