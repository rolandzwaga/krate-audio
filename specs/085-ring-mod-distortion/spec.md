# Feature Specification: Ring Modulator Distortion

**Feature Branch**: `085-ring-mod-distortion`
**Plugin**: Ruinae / KrateDSP
**Created**: 2026-03-01
**Status**: Draft
**Input**: User description: "Add a Ring Modulator distortion type to the Ruinae synthesizer plugin per-voice distortion slot"

## Clarifications

### Session 2026-03-01

- Q: Which NoiseColor should the Noise carrier waveform use? → A: White noise only, fixed, no user control.
- Q: Should kDistortionRingFreqId use logarithmic or linear mapping for display and automation? → A: Logarithmic, matching all other frequency parameters in Ruinae.
- Q: What are the default parameter values for the five new ring mod parameters? → A: Freq: 440 Hz, Mode: Note Track, Ratio: 2.0, Waveform: Sine, Spread: 0%.
- Q: What is the concrete UI/semantic difference between Note Track and Ratio frequency modes? → A: Merge into two modes only (Free and Note Track). The ratio knob is visible whenever Note Track is active. The Ratio mode is removed entirely.
- Q: Should carrier frequency changes be applied immediately or smoothed to prevent zipper noise? → A: Smoothed via a one-pole smoother with approximately 5 ms time constant.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Ring Modulation with Internal Carrier (Priority: P1)

As a sound designer using Ruinae, I want to select "Ring Mod" as a distortion type and multiply my voice signal with an internal sine carrier at a chosen frequency, so that I can create metallic, bell-like, and inharmonic timbres by generating sum and difference frequency sidebands.

**Why this priority**: This is the core ring modulation behavior. Without the internal carrier multiplying the input signal, no ring modulation effect is possible. A sine carrier is the cleanest and most universally useful starting point, producing exactly two sidebands per input harmonic.

**Independent Test**: Can be fully tested by selecting Ring Mod distortion type, setting carrier frequency to a known value, processing a sine input, and verifying the output contains only the expected sum and difference frequencies.

**Acceptance Scenarios**:

1. **Given** the distortion type is set to "Ring Mod" and the carrier waveform is Sine, **When** the carrier frequency is set to 200 Hz and a 440 Hz sine wave is input, **Then** the output contains spectral energy at 240 Hz (440-200) and 640 Hz (440+200) with the 440 Hz fundamental suppressed.
2. **Given** the distortion type is set to "Ring Mod", **When** the carrier frequency is set to 0.1 Hz, **Then** the output exhibits a slow tremolo-like amplitude modulation (approaching the low-frequency limit of ring modulation).
3. **Given** the distortion type is set to "Ring Mod", **When** the carrier frequency is 20 kHz and the input is a 440 Hz sine, **Then** the sum sideband (20440 Hz) is above audible range and the difference sideband (19560 Hz) produces a high-pitched tone at the edge of hearing.
4. **Given** the distortion type is set to "Ring Mod" with default settings, **When** drive is set to 0 (minimum), **Then** the output is silence or near-silence (carrier amplitude scaled to zero).
5. **Given** the distortion type is set to "Ring Mod" with drive at maximum, **When** a full-scale sine is input, **Then** the output peak amplitude does not exceed the input peak amplitude (ring modulation is gain-neutral at unity carrier amplitude).

---

### User Story 2 - Note-Tracking Carrier Frequency (Priority: P1)

As a keyboardist performing with Ruinae, I want the ring modulator's carrier frequency to optionally track the played note (at a configurable ratio), so that the spectral sidebands maintain a consistent harmonic relationship across the keyboard rather than producing wildly different timbres at different pitches.

**Why this priority**: Equal to Story 1 because without note tracking, the ring modulator is limited to a fixed-frequency carrier, which makes it nearly impossible to play melodically. Note tracking transforms the ring mod from a sound-effect tool into a playable instrument. Integer ratios produce harmonically related sidebands; non-integer ratios produce the classic metallic inharmonic character.

**Independent Test**: Can be tested by enabling note-tracking mode, setting a ratio (e.g., 2.0), playing different notes, and verifying the carrier frequency scales proportionally with the note frequency.

**Acceptance Scenarios**:

1. **Given** frequency mode is "Note Track" and ratio is 1.0, **When** note C4 (261.63 Hz) is played, **Then** the carrier frequency is 261.63 Hz, producing DC + octave doubling (sum = 523.25 Hz, difference = 0 Hz).
2. **Given** frequency mode is "Note Track" and ratio is 2.0 (default), **When** note A4 (440 Hz) is played, **Then** the carrier frequency is 880 Hz, producing harmonically related sidebands at 440 Hz and 1320 Hz.
3. **Given** frequency mode is "Note Track" and ratio is 1.37 (non-integer), **When** any note is played, **Then** the output has an inharmonic, metallic character with sidebands that are not integer multiples of the fundamental.
4. **Given** frequency mode is "Free", **When** different notes are played, **Then** the carrier frequency remains at the user-set value regardless of note pitch.
5. **Given** frequency mode is "Note Track" and ratio is 0.5, **When** note A4 (440 Hz) is played, **Then** the carrier frequency is 220 Hz, producing sidebands at 220 Hz and 660 Hz.

---

### User Story 3 - Carrier Waveform Selection (Priority: P2)

As a sound designer, I want to choose different carrier waveforms (Sine, Triangle, Sawtooth, Square, Noise) so that I can control the density and character of the sideband spectrum, from clean bell-like tones (Sine) to dense aggressive textures (Sawtooth/Square) to textural detuning effects (Noise).

**Why this priority**: The sine carrier from Story 1 covers the most common use case, but waveform variety dramatically expands the sonic palette. Complex waveforms generate many more sidebands (N_carrier x N_input x 2), and noise produces broadband textural effects. This is a major creative differentiator.

**Independent Test**: Can be tested by switching carrier waveforms while processing the same input signal and verifying the spectral density increases from Sine (fewest sidebands) through Triangle, Square, and Sawtooth (most sidebands), with Noise producing broadband output.

**Acceptance Scenarios**:

1. **Given** carrier waveform is Sine, **When** a harmonically rich input is processed, **Then** each input harmonic produces exactly two sidebands (the output has 2N spectral components for N input harmonics).
2. **Given** carrier waveform is Square, **When** a sine input is processed, **Then** the output contains sidebands from the carrier's odd harmonics, producing a buzzy, hollow character.
3. **Given** carrier waveform is Sawtooth, **When** a sine input is processed, **Then** the output is spectrally dense with sidebands from all carrier harmonics, producing an aggressive texture.
4. **Given** carrier waveform is Noise, **When** any signal is processed, **Then** the output has a broadband, textural quality where the input's spectral shape is spread across the frequency spectrum.
5. **Given** the user switches carrier waveform during playback, **Then** the transition occurs without clicks or discontinuities (carrier phase is maintained).

---

### User Story 4 - Stereo Spread (Priority: P3)

As a producer, I want to apply a stereo frequency offset between the left and right carrier oscillators, so that I can create wide stereo ring modulation effects where the two channels produce slightly different sideband frequencies, resulting in spatial movement and width.

**Why this priority**: Stereo spread is a creative enhancement that adds spatial dimension. The core ring modulation (Stories 1-3) is fully functional in mono. This feature adds production polish and is particularly effective in stereo mixes but is not required for the fundamental ring mod sound.

**Independent Test**: Can be tested by enabling stereo spread, processing a mono signal, and verifying the left and right outputs contain different sideband frequencies offset by the spread amount.

**Acceptance Scenarios**:

1. **Given** stereo spread is 0%, **When** a mono signal is processed, **Then** the left and right outputs are identical.
2. **Given** stereo spread is 100%, **When** a mono signal is processed in Free mode with carrier at 500 Hz, **Then** the left and right channels have carrier frequencies offset symmetrically around 500 Hz (e.g., L=475 Hz, R=525 Hz), producing different sideband frequencies per channel.
3. **Given** stereo spread is set to a value, **When** the voice is monophonic (single channel), **Then** the stereo spread parameter has no effect (the ring mod processes the single channel with the center carrier frequency).

---

### User Story 5 - Backward Compatibility (Priority: P1)

As an existing Ruinae user, I want all my existing presets to sound and behave identically after this update, with the new Ring Mod distortion type being a new option that does not alter the behavior of any existing distortion types.

**Why this priority**: Backward compatibility is critical. The Ring Modulator is added as a new enum value in the distortion type selector. Existing presets that do not use Ring Mod must be completely unaffected.

**Independent Test**: Can be tested by loading existing presets (which lack Ring Mod parameters) and verifying all distortion types behave identically to the previous version.

**Acceptance Scenarios**:

1. **Given** an existing preset saved before this feature was added, **When** it is loaded, **Then** the distortion type remains as saved (Clean, ChaosWaveshaper, etc.) and the new Ring Mod parameters default to their initial values without affecting output.
2. **Given** the distortion type is set to any value other than Ring Mod, **When** audio is processed, **Then** the Ring Mod carrier oscillator consumes zero additional CPU (it is not ticked).
3. **Given** a new preset is created with Ring Mod distortion, **When** it is saved and reloaded, **Then** all Ring Mod parameters (carrier frequency, frequency mode, ratio, carrier waveform, stereo spread) are correctly restored.

---

### Edge Cases

- What happens when the carrier frequency equals the input frequency? The difference sideband is at DC (0 Hz) and the sum sideband is at double the frequency. The DC blocker (already in the signal chain post-distortion) removes the DC component.
- What happens when the carrier frequency is extremely low (e.g., 0.1 Hz)? The ring modulation becomes a slow amplitude modulation (tremolo). This is valid and expected behavior.
- What happens when the carrier frequency exceeds Nyquist/2? The carrier oscillator (PolyBLEP for complex waveforms, Gordon-Smith for sine) already handles Nyquist clamping. Sidebands above Nyquist will alias, which is managed by the carrier's band-limiting.
- What happens when Drive is at 0? The carrier amplitude is zero, so multiplication produces silence. This is correct.
- What happens when the voice plays a note at 20 Hz with a carrier at 19 Hz? The difference sideband is at 1 Hz (sub-audible). The DC blocker handles the near-DC content.
- What happens during portamento (pitch glide)? In note-tracking mode, the carrier frequency smoothly follows the gliding note frequency, so sidebands track musically.
- What happens with noise carrier and note tracking? Note tracking is ignored when the carrier waveform is Noise (noise has no meaningful frequency). The noise source runs without a frequency parameter.

## Requirements *(mandatory)*

### Functional Requirements

#### DSP Component (KrateDSP Layer 2 Processor)

- **FR-001**: The system MUST provide a `RingModulator` DSP processor class at Layer 2 (`dsp/include/krate/dsp/processors/`) that multiplies an input signal by an internally generated carrier signal, implementing four-quadrant ring modulation: `output[n] = input[n] * carrier[n] * amplitude`, where amplitude is the drive-controlled carrier level.
- **FR-002**: The `RingModulator` MUST support five carrier waveforms: Sine, Triangle, Sawtooth, Square, and Noise. The Sine carrier MUST use the Gordon-Smith magic circle phasor (2 multiplies + 2 adds per sample, amplitude-stable). The sine carrier MUST initialize with phase = 0 (sinState = 0.0f, cosState = 1.0f), producing zero output on the first sample, which is the correct initialization for click-free voice starts. Non-sine tonal waveforms (Triangle, Sawtooth, Square) MUST use the existing `PolyBlepOscillator` for band-limited generation. The Noise carrier MUST use the existing `NoiseOscillator` configured with `NoiseColor::White` (fixed; noise color is not user-configurable).
- **FR-003**: The `RingModulator` MUST support two frequency modes for the tonal carriers (Sine, Triangle, Sawtooth, Square):
  - **Free**: Carrier frequency is set directly in Hz (range: 0.1 Hz to 20,000 Hz). The ratio parameter is hidden in the UI when this mode is active.
  - **Note Track**: Carrier frequency is computed as `noteFrequency * ratio`, where `noteFrequency` is the per-voice pitch and `ratio` is user-configurable. The ratio knob is visible in the UI whenever this mode is active.
- **FR-004**: The `RingModulator` MUST accept a `ratio` parameter in the range 0.25 to 16.0, used in Note Track frequency mode. The default value is 2.0 (produces an octave-relationship carrier). The ratio parameter has no effect in Free mode and its UI control is hidden when Free mode is active.
- **FR-005**: The `RingModulator` MUST accept an `amplitude` (drive) parameter in the range 0.0 to 1.0 that scales the carrier signal before multiplication. At 0.0, the output is silence. At 1.0, the carrier is at full amplitude (peak-to-peak of 1.0 for tonal waveforms).
- **FR-006**: The `RingModulator` MUST accept a `stereoSpread` parameter in the range 0.0 to 1.0, which offsets the left and right carrier frequencies symmetrically around the center frequency. The maximum offset at spread=1.0 MUST be +/-50 Hz (configurable as a constant). The direction convention is: left channel receives center frequency minus the offset; right channel receives center frequency plus the offset (e.g., at center=500 Hz and spread=1.0: left=450 Hz, right=550 Hz). The default value is 0.0 (no spread; left and right channels carry identical carrier frequencies). When processing a mono signal (single buffer), stereo spread has no effect.
- **FR-007**: The `RingModulator` MUST provide both mono (`processBlock(float* buffer, size_t numSamples)`) and stereo (`processBlock(float* left, float* right, size_t numSamples)`) processing interfaces. The mono interface applies center-frequency carrier. The stereo interface applies spread-offset carriers to left and right channels respectively.
- **FR-008**: The `RingModulator` MUST provide `prepare(double sampleRate, size_t maxBlockSize)` and `reset()` methods following the same lifecycle pattern as other distortion processors in the codebase.
- **FR-009**: When the carrier waveform is Noise, the frequency mode and ratio parameters MUST be ignored. The noise source runs independently of pitch.
- **FR-010**: The `RingModulator` MUST be fully real-time safe: no heap allocations, no exceptions, no blocking, no I/O in `processBlock()`.
- **FR-023**: The `RingModulator` MUST apply a one-pole smoother (5 ms time constant, defined as `kSmoothingTimeMs = 5.0f` in the data model) to the effective carrier frequency before it is applied to the carrier oscillator(s). This applies to all frequency changes: user automation of `kDistortionRingFreqId` in Free mode, ratio or note-pitch changes in Note Track mode, and stereo spread offset changes. The smoother state MUST be reset on `reset()` and re-initialized on `prepare()` (via `snapTo()` after `configure()` so no transient is produced on re-prepare). This prevents zipper noise artifacts during parameter automation and note transitions.
- **FR-024**: The `RingModulator` MUST initialize with the following default parameter values, which are also used as fallback values when loading old presets that predate this feature:
  - Carrier frequency: 440 Hz
  - Frequency mode: Note Track
  - Ratio: 2.0
  - Carrier waveform: Sine
  - Stereo spread: 0.0

#### Ruinae Voice Integration

- **FR-011**: The `RuinaeDistortionType` enum MUST be extended with a new `RingModulator` value (appended after `TapeSaturator` to preserve existing enum ordering and preset compatibility). Any `kDistortionTypeCount` constant in `distortion_params.h` or the controller's `kDistortionTypeId` registration MUST reflect the new total of 7 types. If `kDistortionTypeCount` is derived automatically from `RuinaeDistortionType::NumTypes` (the preferred pattern), extending the enum is sufficient and no manual constant update is required; verify this is the case before implementation begins.
- **FR-012**: The `RuinaeVoice` MUST pre-allocate the `RingModulator` instance in `prepareAllDistortions()`, following the existing pattern of pre-allocating all distortion types for zero-allocation switching.
- **FR-013**: The `processActiveDistortionBlock()` switch statement MUST include a `RingModulator` case that invokes the ring mod's mono `processBlock()` method.
- **FR-014**: The `setActiveDistortionDrive()` method MUST map the normalized drive parameter (0.0-1.0) to the ring mod's amplitude parameter.
- **FR-015**: The `resetActiveDistortion()` method MUST include a case for `RingModulator` that resets the carrier oscillator state.
- **FR-016**: The ring modulator MUST receive the current note frequency from the voice on each `noteOn()` AND on every subsequent portamento or glide frequency update (i.e., whenever the voice pitch changes, not only at note start). This ensures that in Note Track mode the carrier frequency smoothly follows the gliding pitch in real time, maintaining correct sideband relationships throughout a portamento transition.

#### Parameters

- **FR-017**: The following new parameter IDs MUST be added to `plugin_ids.h` in the distortion parameter range (500-599), using IDs 560-564 (next available sub-range after Tape at 550-552):
  - `kDistortionRingFreqId` = 560 -- Carrier frequency in Free mode (0.1 to 20000 Hz); default normalized value maps to 440 Hz
  - `kDistortionRingFreqModeId` = 561 -- Frequency mode selector (Free / NoteTrack); 2 values only
  - `kDistortionRingRatioId` = 562 -- Carrier-to-note frequency ratio (0.25 to 16.0); default 2.0
  - `kDistortionRingWaveformId` = 563 -- Carrier waveform (Sine / Triangle / Sawtooth / Square / Noise); default Sine
  - `kDistortionRingStereoSpreadId` = 564 -- Stereo spread amount (0.0 to 1.0); default 0.0
- **FR-018**: All new parameters MUST be registered in the controller with appropriate types: `RangeParameter` with logarithmic taper for `kDistortionRingFreqId` (range 0.1-20000 Hz, matching the log scaling used by all other frequency parameters in Ruinae), `StringListParameter` for mode (2 entries: "Free", "Note Track") and waveform (5 entries: "Sine", "Triangle", "Sawtooth", "Square", "Noise") selectors, `RangeParameter` with linear taper for ratio (0.25-16.0) and stereo spread (0.0-1.0).
- **FR-019**: All new parameters MUST be included in the processor's state save/load to ensure preset persistence. The fields MUST be serialized in this exact order (after all existing Tape Saturator fields): (1) ringFreq, (2) ringFreqMode, (3) ringRatio, (4) ringWaveform, (5) ringStereoSpread. This ordering must be consistent between save and load and must not change once shipped, as altering it would corrupt existing presets.
- **FR-020**: The Ring Mod-specific parameters (FR-017) MUST only be active/visible in the UI when the distortion type is set to Ring Mod (following the same conditional visibility pattern used by other distortion sub-parameters like Chaos, Spectral, Granular, Wavefolder, and Tape).
- **FR-021**: The existing Drive parameter MUST control the ring modulator's carrier amplitude when Ring Mod is the active distortion type.
- **FR-022**: The existing Mix parameter (wet/dry blend) MUST apply to Ring Mod output identically to how it applies to all other distortion types (handled by existing `processBlock` wet/dry logic in `RuinaeVoice`).

### Key Entities

- **RingModulator**: A Layer 2 DSP processor that multiplies an input signal by an internally generated carrier, producing sum and difference frequency sidebands. Depends on Layer 1 primitives: `PolyBlepOscillator` (complex waveforms), `NoiseOscillator` (noise carrier), and a Gordon-Smith sine oscillator (inline implementation).
- **RingModFreqMode**: An enumeration with two values (Free, NoteTrack) controlling how the carrier frequency is determined. In Free mode the carrier frequency is set directly in Hz. In NoteTrack mode the carrier frequency is computed as `noteFrequency * ratio`. (A third Ratio mode was considered but merged into NoteTrack for simplicity.)
- **Carrier Oscillator**: The internal signal generator within the RingModulator. Its waveform shape determines the density and character of the output spectrum.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: When processing a 440 Hz sine input with a 200 Hz sine carrier at full drive, the output spectrum MUST contain energy peaks at 240 Hz and 640 Hz with the 440 Hz fundamental suppressed by at least 60 dB relative to each sideband peak individually (i.e., both the 240 Hz peak and the 640 Hz peak must individually be at least 60 dB above the residual 440 Hz component).
- **SC-002**: The Ring Modulator processor MUST consume less than 0.3% CPU per voice at 44.1 kHz sample rate with any carrier waveform, measured on the development machine used for MEMORY.md benchmarks. As a relative reference point, the ParticleOscillator (a more complex processor) achieves 0.38% CPU with Gordon-Smith phasor; the RingModulator, being simpler, is expected to land well below 0.3%.
- **SC-003**: Switching between Ring Mod and any other distortion type MUST produce no audible clicks or discontinuities (the wet/dry blend and existing type-switching pattern inherently handle this).
- **SC-004**: All existing presets (those created before this feature) MUST load and produce functionally identical output to the previous version when the distortion type is not Ring Mod. "Functionally identical" means all non-ring-mod distortion types produce perceptually indistinguishable audio; the new Ring Mod code path MUST NOT be invoked and MUST NOT alter the signal path for any other distortion type.
- **SC-005**: In Note Track mode with ratio 1.0 and sine carrier, the output for a sustained note MUST contain a DC component (0 Hz, removed by DC blocker) and a component at exactly 2x the input frequency, verifiable by spectral analysis.
- **SC-006**: The RingModulator MUST pass the real-time safety audit: no allocations, no exceptions, no blocking, no I/O in processBlock(), verifiable by code review and AddressSanitizer testing.
- **SC-007**: Ring Mod parameters MUST round-trip correctly through preset save/load: saved values MUST match loaded values to within floating-point precision (relative error less than 1e-6).

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- The per-voice distortion slot processes a mono signal buffer. Stereo spread will only be utilized in production once a future stereo voice path is added; for now, the mono `processBlock` is used in the voice pipeline. The stereo API (`processBlock(left, right, numSamples)`) is provided for forward compatibility and is validated directly at the DSP class level (not through the voice pipeline). User Story 4 tests exercise the `RingModulator` class directly to confirm the stereo implementation is correct for when the voice path is upgraded.
- The DC blocker already in the voice signal chain (post-distortion, pre-TranceGate) will handle any DC component generated by ring modulation at integer frequency ratios (e.g., carrier = input frequency produces DC).
- The Gordon-Smith magic circle phasor is the preferred sine oscillator implementation, as established in this project's performance optimization work (see MEMORY.md). It provides 2 muls + 2 adds per sample with amplitude stability.
- The existing `OscWaveform` enum in `polyblep_oscillator.h` (Sine, Sawtooth, Square, Pulse, Triangle) is sufficient for carrier waveform generation. The ring mod will use Sine (via magic circle), Triangle, Sawtooth, and Square (via PolyBLEP), plus Noise (via NoiseOscillator). The Pulse waveform is not exposed as a carrier option to keep the UI simple.
- No oversampling is required for the sine carrier (pure multiplication of band-limited signals). For complex carriers (Triangle, Sawtooth, Square), the PolyBLEP oscillator's built-in anti-aliasing is sufficient. The carrier itself is band-limited, and while multiplication can create frequencies above Nyquist, this aliasing is an inherent characteristic of ring modulation that users expect and is consistent with hardware ring modulators.
- The normalized drive parameter (0.0 to 1.0) maps linearly to carrier amplitude (0.0 to 1.0). This differs from other distortion types that use non-linear drive mappings, because ring modulation's carrier amplitude directly controls the effect intensity in a perceptually linear way.
- The carrier frequency parameter (`kDistortionRingFreqId`) uses a logarithmic taper for display and automation, consistent with all other frequency parameters in Ruinae. The denormalized value in Hz is computed as `0.1 * pow(200000.0, normalizedValue)` (mapping 0.0 -> 0.1 Hz, 1.0 -> 20000 Hz).
- A one-pole smoother with ~5 ms time constant is applied to the effective carrier frequency before it reaches the oscillator. This smoother is part of the `RingModulator` class (not in the voice layer) so that it is always active regardless of how the frequency is set (automation, note-on, portamento). The 5 ms constant is chosen to be inaudible as a lag while fast enough to track rapid modulation cleanly.
- The `NoiseOscillator` carrier always uses `NoiseColor::White`. Exposing noise color as a user parameter would add a sixth ring mod parameter with marginal benefit; white noise is universally understood as the canonical carrier noise color.

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| `PolyBlepOscillator` | `dsp/include/krate/dsp/primitives/polyblep_oscillator.h` | Reuse directly for Triangle, Sawtooth, Square carrier waveforms. Supports `setFrequency()`, `process()`, `processBlock()`. Layer 1 primitive. |
| `NoiseOscillator` | `dsp/include/krate/dsp/primitives/noise_oscillator.h` | Reuse directly for Noise carrier waveform. Supports `process()`, `processBlock()`. Layer 1 primitive. |
| `OscWaveform` enum | `dsp/include/krate/dsp/primitives/polyblep_oscillator.h` | Defines Sine, Sawtooth, Square, Pulse, Triangle. Ring mod carrier waveform enum should reference this but define its own enum to exclude Pulse and add Noise. |
| `FrequencyShifter` (Gordon-Smith oscillator pattern) | `dsp/include/krate/dsp/processors/frequency_shifter.h` | Uses cosTheta_/sinTheta_/cosDelta_/sinDelta_ for a Gordon-Smith quadrature oscillator. The sine-only subset of this pattern should be extracted or replicated for the ring mod's sine carrier. |
| `RuinaeDistortionType` enum | `plugins/ruinae/src/ruinae_types.h` | Extend with `RingModulator` value. |
| `RuinaeVoice` distortion infrastructure | `plugins/ruinae/src/engine/ruinae_voice.h` | Follow established pattern: `prepareAllDistortions()`, `resetActiveDistortion()`, `setActiveDistortionDrive()`, `processActiveDistortionBlock()`. |
| `DC Blocker` | `plugins/ruinae/src/engine/ruinae_voice.h` (field `dcBlocker_`) | Already in signal chain post-distortion. Handles DC from carrier=input frequency case. No changes needed. |
| Parameter ID range | `plugins/ruinae/src/plugin_ids.h` lines 248-278 | Distortion parameters use IDs 500-599. IDs 560-569 appear to be the next available sub-range (Tape uses 550-552). |
| Distortion parameter registration | `plugins/ruinae/src/parameters/` or `plugins/ruinae/src/controller/` | Follow existing pattern for registering sub-type parameters. |
| Frequency shifter spec (archived) | `specs/_archive_/097-frequency-shifter/` | Notes that RingModulator could share the quadrature oscillator pattern from FrequencyShifter. |

**Search Results Summary**:
- No existing `RingModulator` class in `dsp/` or `plugins/` (confirmed via search).
- The `FrequencyShifter` at Layer 2 uses a Gordon-Smith oscillator inline; if a second component needs the same pattern, consider extracting to a shared Layer 0/1 utility per the frequency shifter spec's forward-looking note. However, the ring mod's sine oscillator is simple enough (4 state variables, ~6 operations/sample) that inline implementation is acceptable for now, with extraction deferred until a third user emerges.
- The `ring_saturation.h` primitive at Layer 1 is unrelated (it implements a ring-buffer-based saturation effect, not ring modulation).

### Forward Reusability Consideration

*Note for planning phase: The `RingModulator` is a Layer 2 processor. Consider what new code might be reusable by sibling features.*

**Sibling features at same layer** (if known):
- A potential Amplitude Modulation effect (AM is ring mod with a DC bias on the carrier -- the same processor could support both with a bias parameter)
- A potential Vocoder effect (uses ring modulation as a building block in each frequency band)
- The frequency shifter's quadrature oscillator shares the Gordon-Smith pattern

**Potential shared components** (preliminary, refined in plan.md):
- Gordon-Smith sine oscillator could be extracted to a Layer 0/1 utility if a third user appears (FrequencyShifter + RingModulator = 2 users; extract at 3)
- The `RingModFreqMode` enum (Free/NoteTrack) pattern could be reused for any pitch-aware modulation effect

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

*DO NOT mark as met without having just verified the code and test output. DO NOT claim completion if ANY requirement is not met without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | MET | `ring_modulator.h:472-487` -- mono processBlock implements `buffer[i] *= carrier * amplitude_`. Test "RingModulator mono processBlock modifies buffer" passes. |
| FR-002 | MET | `ring_modulator.h:40-46` -- RingModCarrierWaveform enum. Sine: Gordon-Smith at lines 414-431. Init sinState_=0, cosState_=1 (lines 260-263). PolyBLEP at 452-457. Noise at 458-462 with NoiseColor::White. Tests for all 5 waveforms pass. |
| FR-003 | MET | `ring_modulator.h:49-52` -- RingModFreqMode enum. computeEffectiveFrequency() at 402-412: Free returns freqHz_, NoteTrack returns noteFrequency_ * ratio_. Tests pass. |
| FR-004 | MET | `ring_modulator.h:366-374` -- setRatio() clamps [0.25, 16.0]. Default ratio_ = 2.0f. Tests for ratio=0.5, 2.0, 1.37 pass. |
| FR-005 | MET | `ring_modulator.h:376-378` -- setAmplitude() clamps [0, 1]. Tests: amplitude=0 silence, amplitude=1 no exceed input peak. |
| FR-006 | MET | `ring_modulator.h:88` -- kMaxSpreadOffsetHz = 50.0f. setStereoSpread at 380-388. Stereo processBlock at 489-513 applies L-offset, R+offset. Tests pass. |
| FR-007 | MET | `ring_modulator.h:151` mono, line 158 stereo. Both noexcept. Tests pass. |
| FR-008 | MET | `ring_modulator.h:244-287` prepare(), 289-307 reset(), 394 isPrepared(). Tests pass. |
| FR-009 | MET | `ring_modulator.h:403-405` returns 0.0f for Noise. Lines 458-462: Noise ignores frequency. Test passes. |
| FR-010 | MET | Both processBlock noexcept, no alloc/exceptions/blocking/IO. ASan clean (T074a). |
| FR-011 | MET | `ruinae_types.h:65-67` RingModulator=6, NumTypes=7. kDistortionTypeCount auto-derived. |
| FR-012 | MET | `ruinae_voice.h:1327-1328` ringMod_.prepare() in prepareAllDistortions(). ringMod_ at line 1515. |
| FR-013 | MET | `ruinae_voice.h:1405-1407` RingModulator case calls ringMod_.processBlock(). |
| FR-014 | MET | `ruinae_voice.h:1371-1373` RingModulator case calls ringMod_.setAmplitude(drive). |
| FR-015 | MET | `ruinae_voice.h:1346-1348` RingModulator case calls ringMod_.reset(). |
| FR-016 | MET | `ruinae_voice.h:1426-1428` ringMod_.setNoteFrequency() in updateOscFrequencies(), called from noteOn, setFrequency, glideToFrequency, portamento ramp. |
| FR-017 | MET | `plugin_ids.h:279-283` IDs 560-564 assigned. |
| FR-018 | MET | `distortion_params.h:224-238` all registrations correct (log freq, string lists, linear ratio/spread). |
| FR-019 | PARTIAL | Save at distortion_params.h:379-384, load at 434-444. Correct order. No round-trip test (Phase 7 skipped). Pluginval passes. |
| FR-020 | NOT MET | No Dist_RingMod template in editor.uidesc. Ring mod parameters not visible in UI. Task was missing from tasks.md. |
| FR-021 | MET | `ruinae_voice.h:1371-1373` drive maps to ringMod_.setAmplitude(). |
| FR-022 | MET | Wet/dry mix applied by existing voice pipeline equally to all distortion types. |
| FR-023 | MET | `ring_modulator.h:89` kSmoothingTimeMs=5.0f. Smoothers configured in prepare() with snapTo(). Test "re-prepare no transient" passes. |
| FR-024 | MET | `ring_modulator.h:207-211` defaults match spec. `distortion_params.h:57-61` struct defaults match. |
| SC-001 | MET | Test uses REQUIRE(suppression >= 60.0f) matching spec 60 dB. Passes. |
| SC-002 | MET | Test uses REQUIRE(cpuPercent < 0.3). Measured 0.03%. Passes. |
| SC-003 | MET | Switch-case pattern + existing wet/dry. Pluginval passes at strictness 5. |
| SC-004 | DEFERRED | Phase 7 skipped by user. Enum values 0-5 preserved, ring mod path gated by switch-case. |
| SC-005 | MET | Test verifies DC + 2x frequency components with ratio=1.0. Passes. |
| SC-006 | MET | Code review: no alloc/exceptions/blocking/IO in processBlock. ASan clean. |
| SC-007 | DEFERRED | Phase 7 skipped. Save/load infrastructure correct. Pluginval passes. |

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
- [ ] No features quietly removed from scope
- [ ] User would NOT feel cheated by this completion claim

### Honest Assessment

**Overall Status**: PARTIAL

**Gaps:**
- Gap 1: FR-020 NOT MET -- No Ring Mod UI template in editor.uidesc. Ring mod parameters are not visible in the UI when the distortion type is set to Ring Mod. This task was missing from tasks.md entirely.
- Gap 2: FR-019 PARTIAL -- Save/load infrastructure is correct and pluginval passes, but no round-trip test exists because Phase 7 (User Story 5 - Backward Compatibility) was skipped by user.
- Gap 3: SC-004 DEFERRED -- Phase 7 was skipped by user. Enum values 0-5 are preserved and the ring mod code path is gated by the switch-case, but no explicit backward compatibility test was written.
- Gap 4: SC-007 DEFERRED -- Phase 7 was skipped by user. Save/load infrastructure is correct and pluginval passes, but no explicit round-trip precision test was written.

**Build results:** 0 warnings. dsp_tests: 6105 cases, 22,065,840 assertions all pass. ruinae_tests: 612 cases, 14,779 assertions all pass. Pluginval: PASS at strictness 5.

**Recommendation**: To achieve full completion: (1) Add a Dist_RingMod template to editor.uidesc with controls for the five ring mod parameters, following the existing conditional visibility pattern used by other distortion sub-parameters (FR-020). (2) Implement Phase 7 tasks T062-T069 to add state round-trip tests and backward compatibility verification (FR-019 full, SC-004, SC-007).
