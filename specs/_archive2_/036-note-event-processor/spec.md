# Feature Specification: Note Event Processor

**Feature Branch**: `036-note-event-processor`
**Created**: 2026-02-07
**Status**: Draft
**Input**: User description: "MIDI note processing with pitch bend and velocity mapping"

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Note-to-Frequency Conversion with Tunable Reference (Priority: P1)

A synthesizer engine receives MIDI note-on events and needs to convert them into frequencies for oscillator pitch control. The conversion must use the standard 12-tone equal temperament (12-TET) formula with a configurable A4 tuning reference, allowing musicians to match orchestral tuning standards that deviate from 440 Hz.

**Why this priority**: Note-to-frequency conversion is the foundational operation without which no pitched sound can be produced. Every other feature (pitch bend, velocity) depends on this working correctly first.

**Independent Test**: Can be fully tested by sending known MIDI note numbers and verifying the returned frequencies match the 12-TET formula within floating-point precision.

**Acceptance Scenarios**:

1. **Given** a NoteProcessor with default settings (A4 = 440 Hz), **When** note 69 (A4) is queried, **Then** the returned frequency is 440.0 Hz.
2. **Given** a NoteProcessor with default settings, **When** note 60 (C4, middle C) is queried, **Then** the returned frequency is approximately 261.626 Hz.
3. **Given** a NoteProcessor with A4 set to 442 Hz, **When** note 69 is queried, **Then** the returned frequency is 442.0 Hz.
4. **Given** a NoteProcessor with A4 set to 432 Hz, **When** note 60 is queried, **Then** the returned frequency is approximately 256.869 Hz (= 432 * 2^((60-69)/12)).

---

### User Story 2 - Pitch Bend with Smoothing (Priority: P1)

A performer uses the pitch bend wheel on their MIDI controller to expressively bend notes up or down. The pitch bend input arrives as a bipolar value (-1.0 to +1.0, derived from the MIDI 14-bit range of 0-16383 with center at 8192) and must be scaled by a configurable range (default +/-2 semitones). The pitch bend must be smoothed sample-by-sample to prevent audible stepping artifacts ("zipper noise") when the bend value changes.

**Why this priority**: Pitch bend is the most common real-time pitch modulation in synthesizers and must be smooth and artifact-free for professional use.

**Independent Test**: Can be tested by setting a pitch bend value and verifying that the resulting frequency offset matches the configured range, and that rapid pitch bend changes produce a smooth output without discontinuities.

**Acceptance Scenarios**:

1. **Given** a NoteProcessor with pitch bend range set to 2 semitones, **When** pitch bend is set to +1.0 (maximum up) and note 69 is queried, **Then** the resulting frequency is approximately 493.88 Hz (A4 up 2 semitones = B4).
2. **Given** a NoteProcessor with pitch bend range set to 2 semitones, **When** pitch bend is set to -1.0 (maximum down) and note 69 is queried, **Then** the resulting frequency is approximately 392.00 Hz (A4 down 2 semitones = G4).
3. **Given** a NoteProcessor with pitch bend range set to 12 semitones (one octave), **When** pitch bend is set to +1.0, **Then** the bend offset is exactly 12 semitones.
4. **Given** a NoteProcessor with smoothing enabled, **When** pitch bend jumps from 0.0 to +1.0 instantaneously, **Then** the smoothed output approaches +1.0 exponentially over the configured smoothing time rather than jumping immediately.
5. **Given** a NoteProcessor with pitch bend at 0.0, **When** pitch bend is set to 0.0 (center/neutral), **Then** the frequency equals the unmodified note frequency with zero offset.

---

### User Story 3 - Velocity Curve Mapping (Priority: P2)

A sound designer configures velocity sensitivity for a synthesizer patch. Different velocity curves allow the same physical playing dynamics to produce different musical responses: a linear curve for predictable control, a soft (concave) curve for easier dynamics at low velocities, a hard (convex) curve for emphasis on strong hits, or a fixed curve that ignores velocity entirely.

**Why this priority**: Velocity mapping shapes the musical expressiveness of the instrument. While not required for basic pitch generation, it is essential for any playable synthesizer to feel responsive and musical.

**Independent Test**: Can be tested by mapping all 128 velocity values (0-127) through each curve type and verifying the output ranges and curve shapes match their mathematical definitions.

**Acceptance Scenarios**:

1. **Given** a NoteProcessor with Linear velocity curve, **When** velocity 127 is mapped, **Then** the output is 1.0 (full level).
2. **Given** a NoteProcessor with Linear velocity curve, **When** velocity 64 is mapped, **Then** the output is approximately 0.504 (64/127).
3. **Given** a NoteProcessor with Soft velocity curve, **When** velocity 64 is mapped, **Then** the output is greater than 0.504 (the curve emphasizes lower velocities, making the instrument easier to play loudly).
4. **Given** a NoteProcessor with Hard velocity curve, **When** velocity 64 is mapped, **Then** the output is less than 0.504 (the curve requires stronger hits for high output, emphasizing dynamic range).
5. **Given** a NoteProcessor with Fixed velocity curve, **When** any velocity (1-127) is mapped, **Then** the output is always 1.0.
6. **Given** any velocity curve, **When** velocity 0 is mapped, **Then** the output is 0.0 (silence).

---

### User Story 4 - Multi-Destination Velocity Routing (Priority: P3)

A synthesizer voice needs velocity-derived values for multiple destinations simultaneously: amplitude (how loud), filter cutoff (how bright), and envelope time scaling (how fast the attack). The NoteProcessor provides pre-computed velocity values scaled for each destination so the voice engine can apply them directly.

**Why this priority**: Multi-destination velocity routing adds depth to sound design but builds on top of the basic velocity mapping. It is an enhancement that requires the core velocity curve to be working first.

**Independent Test**: Can be tested by mapping a single velocity value and verifying that each destination output is independently scaled according to its configured depth.

**Acceptance Scenarios**:

1. **Given** a NoteProcessor with amplitude velocity depth at 100%, **When** velocity 64 is mapped, **Then** the amplitude velocity output equals the curve-mapped velocity value.
2. **Given** a NoteProcessor with filter velocity depth at 50%, **When** velocity 127 is mapped, **Then** the filter velocity output is 0.5 (half the maximum modulation depth).
3. **Given** a NoteProcessor with envelope time velocity depth at 0%, **When** any velocity is mapped, **Then** the envelope time velocity output is 0.0 (velocity has no effect on envelope timing).

---

### Edge Cases

- What happens when MIDI note 0 (C-1, ~8.18 Hz) is converted with maximum downward pitch bend? The frequency must remain positive and finite.
- What happens when MIDI note 127 (G9, ~12543.85 Hz) is converted with maximum upward pitch bend (+24 semitones range)? The frequency must remain finite and not overflow.
- What happens when velocity 0 is received? It must always map to 0.0 regardless of curve type.
- What happens when the tuning reference is set to an extreme value (e.g., 220 Hz or 880 Hz)? The system must clamp finite out-of-range values to the nearest bound (400 Hz or 480 Hz). NaN or infinity inputs must reset to 440.0 Hz.
- What happens when pitch bend range is set to 0 semitones? The pitch bend wheel must have no effect on frequency.
- What happens when pitch bend smoothing is bypassed (time = 0 ms)? Changes must be instantaneous with no latency.
- What happens when `prepare()` is called with a different sample rate while pitch bend is mid-transition? The smoothing must preserve the current smoothed value and target, recalculating only the coefficient to maintain continuity without audible discontinuity.

## Clarifications

### Session 2026-02-07

- Q: When NaN or Inf pitch bend input is received, what should the NoteProcessor do? → A: Ignore the invalid value and maintain the last valid smoothed state (consistent with OnePoleSmoother behavior, prevents discontinuities and state contamination).
- Q: When `prepare()` is called with a new sample rate while pitch bend is actively smoothing, what should happen to the smoother's internal state? → A: Preserve current smoothed value and target, recalculate coefficient only (maintains continuity, no audible pop, aligns with professional DSP pattern of state invariance across sample rate changes).
- Q: When the tuning reference is set to NaN or Inf, what specific value should it be clamped to? → A: 440.0 Hz (ISO standard default). NaN/Inf are category errors distinct from out-of-range numeric values; reset to known-good default rather than arbitrary bound, maintains deterministic behavior and prevents garbage propagation.
- Q: In a polyphonic context with multiple active voices, how often should the calling voice engine invoke `processPitchBend()` per audio block? → A: Once per block, shared by all voices. Pitch bend is a global controller; all voices must bend identically to avoid pitch divergence. Provides semantic correctness, determinism, and O(1) performance regardless of voice count.
- Q: Does the SC-006 performance budget (<0.1% CPU at 44.1 kHz) apply to `getFrequency()` alone, or should it include the amortized cost of `processPitchBend()` per voice? → A: `getFrequency()` alone. The smoother advancement is O(1) per block with negligible amortized per-voice cost. Measuring `getFrequency()` in isolation provides a stable, repeatable benchmark for the per-voice workload that scales with polyphony.

## Requirements *(mandatory)*

### Functional Requirements

**Note-to-Frequency Conversion**:

- **FR-001**: System MUST convert MIDI note numbers (0-127) to frequencies using the 12-tone equal temperament formula: `frequency = a4Reference * 2^((midiNote - 69) / 12)`, where 69 is the MIDI note number for A4.
- **FR-002**: System MUST support a configurable A4 tuning reference frequency, defaulting to 440 Hz (ISO 16:1975 standard). The accepted range is 400 Hz to 480 Hz, covering all practical concert pitch variations including 432 Hz (alternative tuning), 440 Hz (ISO standard), 442 Hz (US orchestral), 443 Hz (European orchestral), and 444 Hz (Vienna Philharmonic). Finite values outside this range must be clamped to the nearest bound (400 Hz or 480 Hz). NaN or infinity inputs must be reset to 440.0 Hz (the ISO standard default), treating these as category errors distinct from out-of-range numeric values.
- **FR-003**: System MUST accept the `prepare(double sampleRate)` initialization call to configure sample-rate-dependent internal state (pitch bend smoother coefficients). If called during an active pitch bend transition, the system must preserve the current smoothed value and target, recalculating only the coefficient to maintain continuity without audible discontinuity.

**Pitch Bend**:

- **FR-004**: System MUST accept pitch bend input as a bipolar float in the range [-1.0, +1.0], where -1.0 represents maximum downward bend, 0.0 represents center (no bend), and +1.0 represents maximum upward bend. This maps to the MIDI pitch bend wheel's 14-bit range (0-16383, center 8192). The bipolar-to-MIDI conversion is: `bipolar = (midiPitchBend - 8192) / 8192.0`. Values outside [-1.0, +1.0] are accepted but may produce bend offsets exceeding the configured range; clamping to the normalized range is the caller's responsibility.
- **FR-005**: System MUST support a configurable pitch bend range in semitones, defaulting to 2 semitones (the standard MIDI pitch bend sensitivity defined by RPN 0,0). The accepted range is 0 to 24 semitones (up to 2 octaves), covering all practical synthesizer configurations.
- **FR-006**: System MUST compute the pitch bend offset in semitones as: `bendSemitones = bipolarInput * pitchBendRange`. The bent frequency is then: `bentFrequency = baseFrequency * 2^(bendSemitones / 12)`.
- **FR-007**: System MUST smooth the pitch bend value using a one-pole exponential filter to prevent zipper noise. The smoothing time is configurable, defaulting to 5 ms (matching the project's standard smoother default). The smoothed value must converge to within 1% of the target within the configured smoothing time.
- **FR-008**: System MUST provide a per-sample `processPitchBend()` method that advances the pitch bend smoother by one sample and returns the current smoothed pitch bend value. This enables sample-accurate pitch bend for the calling voice engine. In polyphonic contexts, this method should be invoked once per block (shared state), not per-voice, to ensure all voices bend identically and avoid pitch divergence.
- **FR-009**: System MUST provide a `getFrequency(uint8_t note)` method that returns the frequency for a given MIDI note with the current smoothed pitch bend and tuning reference applied.

**Velocity Mapping**:

- **FR-010**: System MUST support four velocity curve types: Linear, Soft, Hard, and Fixed.
- **FR-011**: The **Linear** curve MUST map velocity to gain as: `output = velocity / 127.0`. This provides a uniform mapping where velocity 64 produces approximately 0.504 output.
- **FR-012**: The **Soft** (concave) curve MUST map velocity using a square root power function: `output = (velocity / 127.0)^0.5`. This curve emphasizes lower velocities, producing higher output at medium playing dynamics (velocity 64 produces approximately 0.710). The square root function is used because it closely models the perceived loudness relationship for MIDI velocity (per Dannenberg, "The Interpretation of MIDI Velocity", ICMC 2006).
- **FR-013**: The **Hard** (convex) curve MUST map velocity using a squared power function: `output = (velocity / 127.0)^2.0`. This curve de-emphasizes lower velocities, requiring stronger playing for high output (velocity 64 produces approximately 0.254). This is commonly used for instruments where dynamic expression demands physical effort.
- **FR-014**: The **Fixed** curve MUST return 1.0 for any non-zero velocity (1-127), ignoring velocity entirely. This is used for patches where consistent output level is desired regardless of playing dynamics (organ patches, pads, etc.).
- **FR-015**: All velocity curves MUST return 0.0 for velocity 0 (note-off in running status).
- **FR-016**: System MUST clamp velocity input to the valid MIDI range [0, 127].

**Multi-Destination Velocity**:

- **FR-017**: System MUST provide configurable velocity depth for three destinations: amplitude, filter, and envelope time. Each depth is a normalized value in the range [0.0, 1.0], defaulting to 1.0 for amplitude, 0.0 for filter, and 0.0 for envelope time. Depths are mutable at runtime via `setAmplitudeVelocityDepth()`, `setFilterVelocityDepth()`, and `setEnvelopeTimeVelocityDepth()` methods, taking effect on the next `mapVelocity()` call.
- **FR-018**: The velocity output for each destination MUST be computed as: `destinationVelocity = curvedVelocity * depth`, where `curvedVelocity` is the output of the selected velocity curve and `depth` is the destination-specific depth parameter.

**Reset and State**:

- **FR-019**: System MUST provide a `reset()` method that clears all internal state: snaps the pitch bend smoother to 0.0 (center), resets the current smoothed bend value.
- **FR-020**: System MUST handle invalid inputs (NaN, infinity) for pitch bend by ignoring them and maintaining the last valid smoothed state, preventing discontinuities and state contamination. For tuning reference, NaN and infinity must be clamped to the valid range [400 Hz, 480 Hz]. This is consistent with the project's established pattern in `OnePoleSmoother` (which ignores invalid setTarget() inputs) and ensures numerical stability in real-time processing.

### Key Entities

- **NoteProcessor**: The central processor that converts MIDI note data into synthesis-ready frequency and velocity values. Maintains pitch bend state with smoothing, tuning reference, velocity curve configuration, and multi-destination velocity depths.
- **VelocityCurve**: An enumeration of four curve types (Linear, Soft, Hard, Fixed) that determines how MIDI velocity (0-127) is mapped to a normalized gain value (0.0-1.0).
- **VelocityOutput**: A lightweight aggregate containing pre-computed velocity values for three destinations (amplitude, filter, envelope time), ready for direct use by the voice engine.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Note-to-frequency conversion for all 128 MIDI notes (0-127) at default tuning (A4 = 440 Hz) must match the 12-TET formula within 0.01 Hz across the full range (C-1 at ~8.18 Hz through G9 at ~12543.85 Hz).
- **SC-002**: Pitch bend at the configured range endpoints (+/-1.0 bipolar) must shift the frequency by exactly the configured number of semitones, verified within 0.1 Hz at A4 (440 Hz) for a 2-semitone range (expected: 493.88 Hz up, 392.00 Hz down).
- **SC-003**: Pitch bend smoothing must eliminate step artifacts: after an instantaneous pitch bend jump from 0.0 to 1.0, the smoothed output must reach 99% of the target (0.99) within the configured smoothing time, with no individual sample-to-sample jump exceeding 10% of the total range when smoothing time is 5 ms or greater.
- **SC-004**: All four velocity curves must produce mathematically correct output for the full velocity range (0-127), verified within 0.001 tolerance against their defining formulas.
- **SC-005**: Tuning reference changes must be reflected immediately in all subsequent `getFrequency()` calls, verified at A4 = 432 Hz, 440 Hz, 442 Hz, 443 Hz, and 444 Hz.
- **SC-006**: The NoteProcessor must process a single `getFrequency()` call in under 0.1% CPU at 44.1 kHz sample rate, consistent with Layer 2 performance budgets. This measurement applies to `getFrequency()` alone (the per-voice workload that scales with polyphony); the cost of `processPitchBend()` is O(1) per block and contributes negligible amortized per-voice overhead.
- **SC-007**: All operations must be real-time safe: no memory allocations, no blocking waits, no error interrupts, and no I/O operations during processing, verified by code review.

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- The calling voice engine (VoiceAllocator or MonoHandler) is responsible for converting the raw 14-bit MIDI pitch bend message (0-16383) to the bipolar float (-1.0 to +1.0) before passing it to NoteProcessor. The NoteProcessor operates on the normalized bipolar value only.
- The NoteProcessor is a Layer 2 processor that is composed into higher-level systems (VoiceAllocator, MonoHandler, or a future synth voice). It does not receive MIDI events directly; it provides computation services.
- In polyphonic contexts, the NoteProcessor is shared across all voices. The calling voice manager invokes `processPitchBend()` once per block (not per-voice) to advance the shared pitch bend smoother state. All voices then call `getFrequency()` with their respective note numbers to obtain frequencies with the same smoothed pitch bend applied, ensuring semantic correctness and avoiding pitch divergence between voices.
- Only 12-TET (12-tone equal temperament) tuning is supported. Microtuning, just intonation, and other temperaments are out of scope.
- The velocity curve selection is per-patch (global for the NoteProcessor instance), not per-note. Different velocity curves per destination are not supported; all destinations share the same curve with independent depth scaling.
- The pitch bend smoother uses the existing `OnePoleSmoother` from `smoother.h` to maintain consistency with the project's established smoothing pattern and avoid code duplication.

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| `midiNoteToFrequency()` | `dsp/include/krate/dsp/core/midi_utils.h` | **MUST reuse** -- Already implements the 12-TET formula with configurable A4 reference. Constexpr, real-time safe. |
| `velocityToGain()` | `dsp/include/krate/dsp/core/midi_utils.h` | **Reference only** -- Currently implements linear mapping only. NoteProcessor extends this with multiple curve types. New curves should be added to `midi_utils.h` or a companion file, not duplicated. |
| `semitonesToRatio()` | `dsp/include/krate/dsp/core/pitch_utils.h` | **MUST reuse** -- Converts semitone offset to frequency ratio. Used for applying pitch bend. |
| `OnePoleSmoother` | `dsp/include/krate/dsp/primitives/smoother.h` | **MUST reuse** -- Exponential parameter smoother for pitch bend. Provides `configure()`, `setTarget()`, `process()`, `snapTo()`, NaN/Inf safety. |
| `kA4FrequencyHz`, `kA4MidiNote` | `dsp/include/krate/dsp/core/midi_utils.h` | **MUST reuse** -- Standard constants for A4 = 440 Hz and MIDI note 69. |
| `VoiceAllocator::computeFrequency()` | `dsp/include/krate/dsp/systems/voice_allocator.h` | **Reference** -- Shows the existing pattern for computing frequency with pitch bend and tuning. NoteProcessor extracts and generalizes this computation. |
| `VoiceAllocator::setPitchBend()` | `dsp/include/krate/dsp/systems/voice_allocator.h` | **Reference** -- Currently applies pitch bend without smoothing. NoteProcessor adds the smoothing layer. |
| `MonoHandler` | `dsp/include/krate/dsp/processors/mono_handler.h` | **Reference** -- Shows the existing Layer 2 processor pattern with `prepare()`, `reset()`, and smoother usage. |

**Search Results Summary**: The codebase already has `midiNoteToFrequency()` and `velocityToGain()` (linear only) in `midi_utils.h`, plus `semitonesToRatio()` in `pitch_utils.h`. The `VoiceAllocator` has pitch bend and tuning support but without smoothing and with only linear velocity mapping. No existing velocity curve implementations beyond linear were found. No existing `NoteProcessor` class exists.

### Forward Reusability Consideration

*Note for planning phase: When this is a Layer 2+ feature, consider what new code might be reusable by sibling features at the same layer. The `/speckit.plan` phase will analyze this in detail, but early identification helps.*

**Sibling features at same layer** (if known):
- MonoHandler (035) -- already uses `midiNoteToFrequency()` but lacks pitch bend smoothing and velocity curves. Could compose NoteProcessor for these features.
- Future polyphonic voice wrapper -- would compose NoteProcessor for per-voice pitch bend and velocity handling.

**Potential shared components** (preliminary, refined in plan.md):
- The velocity curve functions (Soft, Hard, Fixed) added to Layer 0 (`midi_utils.h` or a new companion) would be reusable by VoiceAllocator, MonoHandler, and any future voice engine.
- The pitch bend smoothing pattern (smoother wrapping a bipolar input scaled by a range) could be extracted as a reusable utility if other controllers (aftertouch, mod wheel) need similar treatment.

**Refactoring opportunity**: The `VoiceAllocator` currently computes `bendRatio = semitonesToRatio(pitchBendSemitones_)` inline without smoothing. After NoteProcessor is implemented, the VoiceAllocator could delegate pitch bend computation to NoteProcessor, eliminating the duplicated bend-to-ratio logic and gaining smoothing for free. Similarly, `velocityToGain()` in `midi_utils.h` could be extended or complemented with the new curve types so both VoiceAllocator and NoteProcessor share the same velocity mapping functions.

## Implementation Verification *(mandatory at completion)*

<!--
  CRITICAL: This section MUST be completed when claiming spec completion.
  Constitution Principle XVI: Honest Completion requires explicit verification
  of ALL requirements before claiming "done".

  DO NOT fill this table from memory or assumptions. Each row requires you to
  re-read the actual implementation code and actual test output RIGHT NOW,
  then record what you found with specific file paths, line numbers, and
  measured values. Generic evidence like "implemented" or "test passes" is
  NOT acceptable — it must be verifiable by a human reader.

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

*DO NOT mark checkmarks without having just verified the code and test output. DO NOT claim completion if ANY requirement is NOT MET without explicit user approval.*

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | MET | `note_processor.h` line 167: `midiNoteToFrequency(static_cast<int>(note), a4Reference_)` uses 12-TET formula from `midi_utils.h` line 71-80. Test "NoteProcessor getFrequency default tuning A4=440Hz" section "Full MIDI range 0-127 within tolerance" verifies all 128 notes against `a4 * 2^((note-69)/12)` formula (128 assertions, all pass within 0.01 Hz). |
| FR-002 | MET | `note_processor.h` line 59: default `a4Reference_(kA4FrequencyHz)` = 440 Hz. Line 145-151: `setTuningReference()` clamps finite to [400,480] (line 150), resets NaN/Inf to 440 Hz (line 147). Test "NoteProcessor setTuningReference and getTuningReference" verifies 432, 442, 400, 480 Hz accepted; NaN/Inf reset to 440. Test "tuning reference edge cases" verifies 300->400, 600->480 clamping. |
| FR-003 | MET | `note_processor.h` line 80-84: `prepare()` calls `bendSmoother_.setSampleRate()` which preserves current/target and recalculates coefficient only (smoother.h line 262-265). Test "NoteProcessor prepare mid-transition preserves state (FR-003)" verifies mid-transition value is preserved after `prepare(96000.0)` -- afterPrepare > 0.0f and <= 1.0f. |
| FR-004 | MET | `note_processor.h` line 101: `setPitchBend(float bipolar)` accepts bipolar float. Line 106: `bendSmoother_.setTarget(bipolar)`. Test "NoteProcessor setPitchBend stores target" sets 0.5f, converges, verifies frequency matches 1-semitone offset. Test "pitch bend NaN/Inf ignored" tests +0.5, -0.3 bipolar values. No internal clamping to [-1,+1] per spec ("values outside [-1,+1] are accepted"). |
| FR-005 | MET | `note_processor.h` line 60: default `pitchBendRange_(2.0f)`. Line 126-128: `setPitchBendRange()` clamps to [0.0, 24.0]. Test "setPitchBendRange clamps to [0, 24]" verifies: 12.0 accepted, -5.0 clamped to 0 (no effect on frequency), 48.0 clamped to 24. |
| FR-006 | MET | `note_processor.h` line 115: `currentBendSemitones_ = smoothedBend * pitchBendRange_`. Line 116: `currentBendRatio_ = semitonesToRatio(currentBendSemitones_)`. Line 168: `baseFreq * currentBendRatio_`. Test "getFrequency with pitch bend at endpoints" verifies +1.0 bipolar at 2-semitone range produces 493.8833 Hz (actual) vs 493.8833 Hz (expected), and -1.0 produces 391.9954 Hz (actual) vs 391.9954 Hz (expected). |
| FR-007 | MET | `note_processor.h` line 70: `bendSmoother_.configure(smoothingTimeMs_, sampleRate_)` initializes OnePoleSmoother. Line 61: default 5.0ms. Line 114: `bendSmoother_.process()` advances one-pole exponential filter. Test "pitch bend smoothing convergence" measures: smoothedVal=0.99319 >= 0.99 after 220 samples (5ms), maxJump=0.02242 <= 0.1. |
| FR-008 | MET | `note_processor.h` line 113-118: `processPitchBend()` advances smoother by one sample via `bendSmoother_.process()`, caches `currentBendRatio_`, returns smoothed bipolar. Test "processPitchBend returns smoothed value" verifies: initial=0.0, after setPitchBend(1.0) first sample > 0 and < 1.0 (smoothing active). |
| FR-009 | MET | `note_processor.h` line 166-169: `getFrequency(uint8_t note)` returns `baseFreq * currentBendRatio_` with current tuning and smoothed pitch bend. Test "getFrequency with pitch bend at endpoints" and "pitch bend 12 semitone range" verify correct bent frequencies. |
| FR-010 | MET | `midi_utils.h` line 122-127: `VelocityCurve` enum with Linear=0, Soft=1, Hard=2, Fixed=3. Test "VelocityCurve enum values" verifies all four values. `note_processor.h` line 177: `setVelocityCurve()` sets active curve. |
| FR-011 | MET | `midi_utils.h` line 160-161: `case VelocityCurve::Linear: return normalized;` where normalized=velocity/127.0. Test "mapVelocity Linear curve" verifies: vel 127->1.0, vel 64->0.50394 (64/127=0.50394), vel 0->0.0. Test "NoteProcessor mapVelocity Linear curve" verifies same via member function. |
| FR-012 | MET | `midi_utils.h` line 162-163: `case VelocityCurve::Soft: return std::sqrt(normalized);`. Test "mapVelocity Soft curve" verifies vel 64 produces sqrt(64/127)=0.70981, and soft(64) > linear(64). |
| FR-013 | MET | `midi_utils.h` line 164-165: `case VelocityCurve::Hard: return normalized * normalized;`. Test "mapVelocity Hard curve" verifies vel 64 produces (64/127)^2=0.25396, and hard(64) < linear(64). |
| FR-014 | MET | `midi_utils.h` line 166-167: `case VelocityCurve::Fixed: return 1.0f;` (only reached when clamped > 0 due to early return at line 153-155). Test "mapVelocity Fixed curve" verifies vel 0->0.0, vel 1->1.0, vel 64->1.0, vel 127->1.0. |
| FR-015 | MET | `midi_utils.h` line 153-155: `if (clamped == 0) { return 0.0f; }` before any curve evaluation. Test "mapVelocity velocity 0 always returns 0" verifies 0.0 for all four curves. Test "NoteProcessor velocity edge cases" section "velocity 0 always maps to 0" re-verifies via member function. |
| FR-016 | MET | `midi_utils.h` line 148-150: clamping via ternary to [kMinMidiVelocity, kMaxMidiVelocity] = [0, 127]. Test "mapVelocity clamps out-of-range input" verifies: -1->0.0, -100->0.0, 128->1.0, 255->1.0. Test "NoteProcessor velocity edge cases" section "out-of-range velocities clamped" re-verifies via member function. |
| FR-017 | MET | `note_processor.h` line 30-34: `VelocityOutput` struct with amplitude (default 0.0f), filter (default 0.0f), envelopeTime (default 0.0f). Line 65-67: defaults ampVelocityDepth_=1.0, filterVelocityDepth_=0.0, envTimeVelocityDepth_=0.0. Lines 198-211: three setter methods clamp to [0,1]. Tests "setAmplitudeVelocityDepth/setFilterVelocityDepth/setEnvelopeTimeVelocityDepth clamps to [0, 1]" verify clamping and runtime mutability. |
| FR-018 | MET | `note_processor.h` line 190-193: `out.amplitude = curvedVel * ampVelocityDepth_`, `out.filter = curvedVel * filterVelocityDepth_`, `out.envelopeTime = curvedVel * envTimeVelocityDepth_`. Test "multi-destination independent scaling" verifies: vel 127 with depths (1.0, 0.5, 0.0) -> amplitude=1.0, filter=0.5, envelopeTime=0.0. Also verifies vel 64 with same depths. |
| FR-019 | MET | `note_processor.h` line 88-92: `reset()` calls `bendSmoother_.snapTo(0.0f)`, sets `currentBendSemitones_=0.0f`, `currentBendRatio_=1.0f`. Test "reset snaps pitch bend to zero" verifies: after converging to 1.0 bend, reset makes getFrequency(69)==440.0 Hz and processPitchBend()==0.0. |
| FR-020 | MET | `note_processor.h` line 102-105: `setPitchBend()` checks `detail::isNaN(bipolar) || detail::isInf(bipolar)` BEFORE calling `setTarget()`, returns early to preserve state. Line 146-148: `setTuningReference()` resets NaN/Inf to 440 Hz. Tests: "pitch bend NaN/Inf ignored" verifies NaN and Inf do not change frequency. "NaN/Inf guard ordering (FR-020, R10)" verifies that after converging to 0.5, sending NaN keeps smoothedAfter==0.5 (not reset to 0). |
| SC-001 | MET | Test "NoteProcessor getFrequency default tuning A4=440Hz" section "Full MIDI range 0-127 within tolerance": all 128 notes verified against 12-TET formula within margin 0.01 Hz. Actual output for boundary notes: note 0=8.17579 Hz (expected 8.17580), note 69=440.0 Hz, note 127=12543.854 Hz (expected 12543.855). All 128 assertions pass. Tolerance matches spec: 0.01 Hz. |
| SC-002 | MET | Test "getFrequency with pitch bend at endpoints": +1.0 bipolar at 2-semitone range produces actual 493.8833 Hz (expected 493.8833, within 0.1 Hz margin). -1.0 produces actual 391.99542 Hz (expected 391.9954, within 0.1 Hz margin). Both within spec threshold of 0.1 Hz. |
| SC-003 | MET | Test "pitch bend smoothing convergence": after jump from 0.0 to 1.0, smoothedVal=0.99319 after 220 samples (5ms at 44.1kHz). Spec requires >= 0.99. maxJump=0.02242. Spec requires <= 0.1. Both criteria met with margin. |
| SC-004 | MET | Tests "mapVelocity Linear/Soft/Hard/Fixed curve" verify all four curves within 0.001 tolerance (spec requires 0.001): Linear vel 64: actual=64/127=0.50394. Soft vel 64: actual=sqrt(64/127)=0.70981. Hard vel 64: actual=(64/127)^2=0.25396. Fixed vel 1-127: actual=1.0. All pass. |
| SC-005 | MET | Test "getFrequency with various A4 references" verifies: A4=432 Hz -> getFrequency(69)=432.0 Hz, getFrequency(60)=256.8687 Hz. A4=442 Hz -> 442.0 Hz. A4=443 Hz -> 443.0 Hz. A4=444 Hz -> 444.0 Hz. All within 0.01 Hz. Changes take effect immediately (no smoothing on tuning reference). |
| SC-006 | MET | Test "NoteProcessor getFrequency performance (SC-006)": 1M iterations of getFrequency() in 13.06 ms. Per call: 13.06 ns. CPU at 44.1 kHz: 0.0576% (budget: <0.1%). Passes with 42% margin. Measured on Release build. |
| SC-007 | MET | Code review of `note_processor.h`: All methods marked `noexcept`. No `new`/`delete`/`malloc`/`free`. No `std::vector`/`std::string` members. No `std::mutex`/locks. No `throw`/`catch`. No I/O (`cout`/`printf`). Only stack variables and inline computation. OnePoleSmoother is value type, no heap allocation. VelocityOutput returned by value. All operations are arithmetic on floats. |

**Status Key:**
- MET: Requirement verified against actual code and test output with specific evidence
- NOT MET: Requirement not satisfied (spec is NOT complete)
- PARTIAL: Partially met with documented gap and specific evidence of what IS met
- DEFERRED: Explicitly moved to future work with user approval

### Completion Checklist

*All items must be checked before claiming completion:*

- [x] Each FR-xxx row was verified by re-reading the actual implementation code (not from memory)
- [x] Each SC-xxx row was verified by running tests or reading actual test output (not assumed)
- [x] Evidence column contains specific file paths, line numbers, test names, and measured values
- [x] No evidence column contains only generic claims like "implemented", "works", or "test passes"
- [x] No test thresholds relaxed from spec requirements
- [x] No placeholder values or TODO comments in new code
- [x] No features quietly removed from scope
- [x] User would NOT feel cheated by this completion claim

### Honest Assessment

**Overall Status**: COMPLETE

All 20 functional requirements (FR-001 through FR-020) and all 7 success criteria (SC-001 through SC-007) are met with concrete, verifiable evidence. No gaps, no relaxed thresholds, no removed features.

**Verification summary:**
- 37 test cases, 255 assertions, all passing
- 0 compiler warnings (MSVC Release build)
- 0 clang-tidy errors or warnings (195 files analyzed)
- getFrequency() CPU: 0.058% at 44.1 kHz (budget: <0.1%)
- All 128 MIDI notes within 0.01 Hz of 12-TET formula
- Pitch bend endpoints within 0.1 Hz of expected values
- Smoothing convergence: 99.3% at 5ms (spec: 99%), max jump 2.2% (spec: 10%)
