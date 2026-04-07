# Research: Bow Model Exciter (Spec 130)

**Date**: 2026-03-23 | **Branch**: `130-bow-model-exciter`

## R1: ImpactExciter Process Signature Refactoring Scope

**Decision**: Refactor `ImpactExciter::process()` from `float process() noexcept` to `float process(float /*feedbackVelocity*/) noexcept`. The parameter is accepted but ignored.

**Rationale**: FR-015 requires a unified exciter interface `process(float feedbackVelocity)`. The current ImpactExciter has:
- `float process() noexcept` at line 226 of `impact_exciter.h` -- no parameters
- `processBlock(float*, int)` at line 307 calls `process()` internally
- 13 call sites in `impact_exciter_test.cpp` using `.process()`
- 1 call site in `processor.cpp:1631` using `v.impactExciter.process()`

**Impact Assessment**:
- `processBlock` internal call: update to `process(0.0f)`
- 13 test call sites: update to `process(0.0f)` -- trivial search-and-replace
- 1 plugin call site: update to pass `feedbackVelocity` from resonator (even though ImpactExciter ignores it)

**Alternatives considered**:
- A. Virtual base class `IExciter` with virtual `process(float)` -- rejected because virtual calls in tight audio loops violate Constitution Principle IV
- B. Template-based interface -- rejected as overengineered for 3 exciter types
- C. Simple signature alignment (chosen) -- each exciter has `process(float feedbackVelocity)` without virtual dispatch; voice engine uses switch/if on exciter type enum

## R2: ResidualSynthesizer Process Signature

**Decision**: ResidualSynthesizer uses `float process() noexcept` (no arguments, line 178). Per FR-015, it should also accept `float feedbackVelocity` but ignore it.

**Rationale**: The unified interface requires all three exciters to share `process(float feedbackVelocity)`. ResidualSynthesizer is a buffer playback synthesizer that has no use for feedback velocity.

**Impact Assessment**:
- ResidualSynthesizer call sites in `processor.cpp:1635`: `v.residualSynth.process()` -- update to `v.residualSynth.process(feedbackVelocity)`
- Test files: `residual_synthesizer_tests.cpp` -- update call sites to `process(0.0f)`

**Alternatives considered**:
- Leave ResidualSynthesizer unchanged and only unify Impact+Bow -- rejected because FR-015 explicitly says "All exciter types"

## R3: WaveguideString Bow Junction Integration

**Decision**: The bow junction will be integrated into `WaveguideString::process()` by adding a new processing path when bow excitation is active. The existing split delay lines (`nutSideDelay_`, `bridgeSideDelay_`) already implement the two-segment topology. The bow junction will:
1. Read incoming waves from nut and bridge delay outputs
2. Sum to get string velocity at bow point
3. Route through external bow exciter (via callback or returned from exciter)
4. Inject friction force into both delay segments

**Rationale**: WaveguideString already has:
- `nutSideDelay_` (line 712) and `bridgeSideDelay_` (line 713) -- two delay lines for split topology
- `feedbackVelocity_` (line 753) -- already stores the feedback velocity from the bridge output
- `dcBlocker_` (line 720) -- must be relocated per FR-021
- `pickPosition_` (line 737) -- controls delay split ratio, analogous to bow position
- `setPickPosition()` (line 260) -- already sets up the delay split

**Key Design Decision**: Rather than adding the friction computation inside WaveguideString (which would create a Layer 2 -> Layer 2 dependency), the bow junction protocol is:
1. WaveguideString exposes `getFeedbackVelocity()` (already exists)
2. External code (InnexusVoice) calls `bowExciter.process(feedbackVelocity)` to get excitation force
3. WaveguideString's `process(excitation)` injects the force

This matches the existing pattern and keeps the bow exciter as an independent Layer 2 processor.

**DC Blocker Relocation (FR-021)**: Currently at line 197 in the process loop, after loss filter. The spec requires relocation to after friction junction output. In the bow path, the junction output IS the signal before it re-enters delay lines, so the DC blocker stays in roughly the same position but applies to the combined friction + waveguide output rather than just the loss filter output.

**Alternatives considered**:
- A. Embed BowExciter inside WaveguideString -- rejected (Layer violation, tight coupling)
- B. Create a new WaveguideBowedString class -- rejected (code duplication, WaveguideString already has the topology)
- C. Use existing external coupling pattern (chosen) -- clean separation, matches existing exciter pattern

## R4: ModalResonatorBank Bowed Mode Coupling

**Decision**: Add 8 biquad bandpass filters (Q ~50) as bowed-mode velocity taps to ModalResonatorBank. These are feedforward taps on the resonator output, not additional delay lines or oscillators.

**Rationale**: The spec (FR-020) requires:
- 8 bowed modes with narrow bandpass velocity taps (Q ~50)
- Center frequency = mode frequency for each tap
- Summed taps provide scalar feedback velocity for friction computation
- Bow excitation force feeds into ALL modes weighted by `sin((n+1) * pi * bowPosition)`

ModalResonatorBank has:
- `kMaxModes = 96` modes (line 27)
- SoA layout with `sinState_`, `cosState_`, `epsilon_`, `radius_`, `inputGain_` arrays
- `processSample()` returns sum of all mode outputs
- `getFeedbackVelocity()` already exists (line 238, currently returns 0.0f)

Implementation approach:
- Add 8 biquad bandpass filter states inside ModalResonatorBank
- In `processSample()`, the output already sums all modes -- apply the 8 bandpass filters to this output
- Update `getFeedbackVelocity()` to return the summed bandpass outputs
- Add `setBowPosition(float)` to update input gains with `sin((n+1) * pi * bowPosition)` weighting

**CPU Note**: 8 biquad filters at Q=50 = ~40 multiply-adds per sample, comparable to a single biquad cascade. This is negligible compared to the 96-mode bank computation.

**Alternatives considered**:
- A. Per-mode delay lines for velocity -- rejected (spec explicitly says no per-mode delay lines)
- B. Single broadband velocity tap -- rejected (spec requires narrow Q~50 for each of 8 modes)
- C. Separate BowedModeBank class -- rejected (bowed modes are intrinsically tied to the resonator's mode frequencies and state)

## R5: BowExciter Component Design

**Decision**: Implement BowExciter as a standalone Layer 2 processor at `dsp/include/krate/dsp/processors/bow_exciter.h`.

**Internal Components (reused from codebase)**:
- `OnePoleLP` (Layer 1, `primitives/one_pole.h`) -- for bow hair width LPF at ~8 kHz (FR-009)
- `LFO` (Layer 1, `primitives/lfo.h`) -- for rosin character slow drift at 0.7 Hz (FR-008)

**Internal Components (new, embedded)**:
- Noise generator for rosin character high-frequency jitter (FR-008) -- simple linear congruential RNG + highpass, too small to extract
- Bow table friction function (FR-002) -- inline, memoryless nonlinearity
- Energy-aware gain control (FR-010) -- simple ratio computation

**Key Per-Sample Flow**:
1. Read ADSR envelope value (external, from InnexusVoice)
2. Compute bow acceleration from envelope, integrate to velocity (FR-004)
3. Clamp velocity to `maxVelocity * speed` (FR-005)
4. Compute `deltaV = bowVelocity - feedbackVelocity` (FR-006)
5. Apply rosin jitter (LFO + noise offset) (FR-008)
6. Compute reflection coefficient via bow table (FR-002, FR-003)
7. Compute excitation force `deltaV * reflectionCoeff` (FR-006)
8. Apply position impedance scaling (FR-007)
9. Apply bow hair LPF at 8 kHz (FR-009)
10. Apply energy-aware gain control (FR-010)
11. Return excitation force

**Alternatives considered**:
- A. Use lookup table for bow table -- deferred as optional optimization; the inline computation is only 4 muls + 1 fabs + 1 clamp per sample (SC-013)
- B. Add BowExciter at Layer 3 -- rejected; it depends only on Layer 0/1 primitives, making it a proper Layer 2 processor

## R6: Oversampling Strategy (FR-022, FR-023)

**Decision**: Implement minimal 2x oversampling for the bow-resonator junction as a switchable path controlled by `kBowOversamplingId` VST parameter.

**Approach**:
- Only the nonlinear friction junction (bow table evaluation + immediate neighbors) runs at 2x rate
- Full delay lines remain at 1x with adjusted delay lengths
- Use simple linear interpolation for upsampling, single-pole lowpass for downsampling anti-aliasing
- Default off (1x mode)

**Rationale**: The friction nonlinearity's bandwidth expansion is the primary source of aliasing. At high pitches (>1 kHz) with high pressure (>0.7), the STK bow table's steep slope generates significant harmonic energy above Nyquist. Running only the junction at 2x rate is the most CPU-efficient approach.

**Built-in mitigations that are always active (FR-023)**:
- Smooth power-law bow table (no discontinuities)
- String loss filter attenuates high frequencies
- Bow hair LPF at 8 kHz
- DC blocker

**Alternatives considered**:
- A. Full 2x oversampling of entire waveguide -- rejected (doubles delay line memory and CPU for minimal benefit beyond the junction)
- B. 4x oversampling -- rejected (diminishing returns vs CPU cost)
- C. No oversampling option -- rejected (spec explicitly requires it as a user-facing parameter)

## R7: ADSR-Driven Bow Acceleration (FR-004)

**Decision**: The ADSR envelope's output drives bow acceleration (not velocity directly). Velocity is the running integral of acceleration, clamped by speed parameter.

**Key insight**: This provides Guettler-compliant attack transients without separate transient modelling:
- Short ADSR attack = high acceleration = fast velocity ramp = potentially scratchy onset
- Long ADSR attack = low acceleration = slow velocity ramp = clean, smooth onset
- ADSR sustain = constant acceleration = velocity climbs to ceiling = steady-state bowing

**Implementation**:
```
acceleration = adsrValue * maxAcceleration
bowVelocity += acceleration * (1.0 / sampleRate)
bowVelocity = clamp(bowVelocity, 0, maxVelocity * speed)
```

Where `maxVelocity` is set from MIDI velocity at note-on, and `maxAcceleration` is a tuned constant.

**Alternatives considered**:
- A. ADSR drives velocity directly -- rejected (no acceleration control, no Guettler transients)
- B. Separate attack envelope -- rejected (ADSR already has attack shape control)

## R8: Existing LFO Suitability for Rosin Jitter

**Decision**: Reuse `Krate::DSP::LFO` for the 0.7 Hz slow drift component of rosin character.

**Rationale**: The LFO class at `primitives/lfo.h`:
- Has `prepare(sampleRate)`, `setFrequency(hz)`, `setWaveform(type)`, `process()` -- all needed
- Supports sine waveform (appropriate for slow drift)
- 208 bytes -- acceptable memory overhead per voice
- `retrigger()` can reset phase on note-on for deterministic behavior

**Concern**: LFO is somewhat heavyweight (wavetable-based, crossfading, symmetry, quantization features) for a simple 0.7 Hz sine. However, reusing it avoids creating a duplicate oscillator class (Constitution Principle XV - ODR Prevention, code reuse).

**Alternative**: A minimal Gordon-Smith phasor (from MEMORY.md DSP optimization insights) could be used instead -- only 2 muls + 2 adds per sample vs the LFO's wavetable lookup. But code reuse is more important than micro-optimization for a 0.7 Hz oscillator (called once per sample, not per-voice-per-mode).

**Decision**: Use the existing LFO class. If profiling shows it's a bottleneck (unlikely at 0.7 Hz), replace with Gordon-Smith phasor later.

## R9: OnePoleLP Suitability for Bow Hair LPF

**Decision**: Reuse `Krate::DSP::OnePoleLP` for the bow hair width filter at ~8 kHz.

**Rationale**: OnePoleLP at `primitives/one_pole.h`:
- `prepare(sampleRate)`, `setCutoff(hz)`, `process(sample)` -- all needed
- 32 bytes -- minimal memory overhead
- NaN/Inf handling with state reset
- Denormal flushing built in
- 6 dB/octave rolloff is appropriate for a gentle bow hair smoothing filter

This is exactly the right component -- no need to create a new filter.
