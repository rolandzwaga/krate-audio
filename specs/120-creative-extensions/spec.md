# Feature Specification: Innexus Milestone 6 -- Creative Extensions

**Feature Branch**: `120-creative-extensions`
**Plugin**: Innexus (`plugins/innexus/`) and KrateDSP (`dsp/`)
**Created**: 2026-03-05
**Status**: Draft
**Input**: User description: "Innexus Milestone 6 (Phases 17-21): Creative Extensions -- cross-synthesis, stereo partial spread, evolution engine, harmonic modulators, multi-source blending. Full creative extension layer building on M1-M5 infrastructure."

## Clarifications

### Session 2026-03-05

- Q: What is the signal chain ordering when multiple features are active simultaneously (cross-synthesis blend, multi-source blend, evolution, modulators, harmonic filter)? → A: Source stage first (cross-synth blend or multi-source blend produces the base model), then Evolution replaces/overrides the source selection when enabled, then Harmonic Filter applies per-partial amplitude masking, then Harmonic Modulators apply per-partial animation as the last pre-synthesis stage. This mirrors standard synthesis architectures where source selection precedes timbre modification.
- Q: How should the `HarmonicOscillatorBank` stereo output API be designed — replace `process()` or add a new method? → A: Add `processStereo(float& left, float& right)` as a new method alongside the existing `float process()`. Existing mono callers are unaffected. The processor calls `processStereo()` when stereo output is needed. This preserves backward compatibility for the Layer 2 public interface.
- Q: When Evolution + Manual Morph interaction causes the clamp in FR-021 to saturate (evolutionPosition + manualMorphOffset hits 0.0 or 1.0), should there be behavior beyond silent clamping? → A: Silent clamping is fine. The user intuitively understands that pushing morph beyond the range does nothing extra. No special feedback or wrapping needed.
- Q: When are modulator LFO phase accumulators initialized? FR-029 says free-running but doesn't specify the starting point. → A: Initialize LFO phase to 0.0 in `prepare()` / `setupProcessing()`. Phase is never reset on note events. This makes behavior deterministic from plugin activation, which is important for reproducible tests and S&H waveform consistency.
- Q: What happens when both Multi-Source Blend (FR-040) and Evolution Engine (FR-022) are enabled simultaneously, since both claim to override the normal playback path? → A: Multi-source blend takes priority over evolution. When blend is enabled, evolution is ignored (blend produces the model directly from weighted snapshots). The user should disable blend to use evolution. This avoids ambiguous double-override of the source selection path.

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Cross-Synthesis: Carrier-Modulator Timbral Performance (Priority: P1)

A performer loads a vocal sample into Innexus (or routes live vocal via sidechain). The analysis pipeline extracts the vocal's harmonic structure -- formant shapes, vowel transitions, breath textures. The performer then plays their MIDI keyboard, and each note inherits the vocal timbre while sounding at the played pitch. They adjust a **Timbral Blend** control to interpolate between the played pitch's natural harmonic series (pure, synthetic) and the analyzed source's spectral shape (rich, organic). They can swap harmonic models in real time -- morphing from "violin timbre" to "voice timbre" while holding MIDI notes -- by recalling different Harmonic Memory slots or switching the analysis source.

This is architecturally what the core instrument already does (MIDI pitch + analyzed timbre), but cross-synthesis explicitly frames it as a deliberate carrier-modulator performance workflow with dedicated blend controls. It is distinct from a channel vocoder (bandpass filter bank) -- it operates on per-partial frequency and amplitude trajectories rather than band-averaged spectral envelopes [Smith 2011, Ch. "Cross-Synthesis"].

**Why this priority**: Cross-synthesis is the highest-value creative extension because it builds directly on existing infrastructure (the oscillator bank already synthesizes from an abstract harmonic model independent of original audio) and delivers the flagship use case: "play any timbre from MIDI." The primary new work is explicit source-as-modulator controls and real-time source switching with crossfade. Every other extension (stereo spread, evolution, modulators) enriches cross-synthesis output.

**Independent Test**: Can be fully tested by loading a sample, playing MIDI notes, adjusting the Timbral Blend parameter between 0.0 (pure harmonic series) and 1.0 (full source timbre), and verifying the spectral content transitions smoothly. Source switching can be tested by recalling different Memory slots while holding a MIDI note and verifying click-free crossfade.

**Acceptance Scenarios**:

1. **Given** Innexus has an analyzed source (sample or sidechain), **When** the user plays a MIDI note with Timbral Blend at 1.0 (full source timbre), **Then** the oscillator bank synthesizes at the MIDI pitch using the source's spectral shape (normalized amplitudes, relative frequencies, inharmonic deviations) -- identical to current M1-M5 behavior.
2. **Given** Timbral Blend is at 0.0 (pure harmonic series), **When** the user plays a MIDI note, **Then** the oscillator bank synthesizes using a pure harmonic series where `relativeFreq_n = n` (integer multiples), `normalizedAmp_n` follows a natural harmonic rolloff curve (`1/n` rolloff), and inharmonic deviation is zero -- effectively a "clean" additive tone.
3. **Given** Timbral Blend is at 0.5, **When** the user plays a MIDI note, **Then** the oscillator bank synthesizes using a blended model: `effectiveRelativeFreq_n = lerp(n, sourceRelativeFreq_n, 0.5)`, `effectiveAmp_n = lerp(pureAmp_n, sourceNormalizedAmp_n, 0.5)` -- a perceptually halfway timbre between synthetic and source.
4. **Given** the user is holding a MIDI note with Slot 1 recalled (violin timbre), **When** they recall Slot 3 (vocal timbre), **Then** the oscillator bank crossfades from violin to vocal over ~10ms (reusing the existing manual freeze crossfade mechanism). No click or pop is audible during the transition.
5. **Given** Innexus is in sidechain mode with live analysis active, **When** the user switches the source from sidechain to a stored Memory Slot while holding MIDI notes, **Then** the timbre crossfades smoothly from the live-tracked timbre to the recalled snapshot timbre.
6. **Given** a source with significant inharmonicity (e.g., bell), **When** Timbral Blend is swept from 0.0 to 1.0, **Then** inharmonic deviation scales proportionally with the blend amount: `effectiveDeviation_n = sourceDeviation_n * timbralBlend * inharmonicityAmount`.

---

### User Story 2 - Stereo Partial Spread (Priority: P1)

A sound designer wants to create a wide, immersive stereo image from a single harmonic model. They increase the **Stereo Spread** control from 0% to 100%. As the spread increases, odd-numbered partials shift toward one stereo channel and even-numbered partials shift toward the other, creating a rich spatial distribution that transforms the mono reconstruction into a spatially expansive sound. The effect is frequency-dependent -- low partials remain more centered (as is natural for bass frequencies) while higher partials spread wider.

The oscillator bank output becomes stereo (two sum buffers instead of one). Each partial's pan position is computed once per frame update, not per sample -- the cost is one pan coefficient per partial, negligible since each partial already has an amplitude multiplier.

**Why this priority**: Stereo spread is co-equal with cross-synthesis as P1 because the current oscillator bank output is inherently monophonic (all partials sum to a single channel). This is a critical limitation for a modern instrument plugin -- every DAW user expects stereo output. The implementation is architecturally trivial (each partial gains a pan position alongside its amplitude) but has high perceptual impact. Standard additive synthesis technique used in Alchemy, Razor, and most modern additive synths [DSP plan Section 15, Priority 3].

**Independent Test**: Can be tested by synthesizing a known harmonic model at Spread=0% (verify mono center output), then at Spread=100% (verify odd partials in one channel, even in the other), and measuring the inter-channel spectral difference.

**Acceptance Scenarios**:

1. **Given** Stereo Spread is 0% (default), **When** the user plays a MIDI note, **Then** the left and right output channels are identical (mono center). All partials have pan position 0.0 (center).
2. **Given** Stereo Spread is 100%, **When** the user plays a MIDI note with a source containing partials 1-48, **Then** odd partials (1, 3, 5, ...) are panned fully left and even partials (2, 4, 6, ...) are panned fully right, creating maximum stereo width.
3. **Given** Stereo Spread is 50%, **When** the user plays a MIDI note, **Then** odd partials are panned 50% left of center and even partials are panned 50% right of center, creating moderate stereo width.
4. **Given** Stereo Spread is 100%, **When** the user changes spread to 0% while a note is sustaining, **Then** the pan positions smoothly converge to center over a short transition (~5-10ms), with no click or abrupt spatial jump.
5. **Given** a source with only low partials (e.g., bass instrument, partials 1-8), **When** Stereo Spread is 100%, **Then** the stereo effect is present but the fundamental (partial 1) remains centered or only slightly offset -- preserving bass mono compatibility.
6. **Given** Stereo Spread is active, **When** the Harmonic Filter is set to "Odd Only," **Then** the audible output is panned to the odd-partial side. When set to "Even Only," it pans to the even-partial side. The stereo spread and harmonic filter interact correctly.
7. **Given** the oscillator bank produces stereo output, **When** the residual synthesizer is mixed in, **Then** the residual component is panned center (mono) and blended with the stereo harmonic output. The residual does not participate in stereo spread.

---

### User Story 3 - Evolution Engine: Autonomous Timbral Drift (Priority: P2)

An ambient sound designer has captured 4 harmonic snapshots into Memory Slots 1-4 -- a bowed cello, a breathy flute, a glass harmonica, and a choir vowel. They enable the **Evolution Engine** and configure it to slowly drift between these snapshots over time. The evolution LFO (or random walk) drives the morph position through the snapshot sequence, creating a continuously evolving timbre that transforms organically without any manual intervention. They adjust **Evolution Speed** (rate of change) and **Evolution Depth** (how far from the current position the drift can wander).

The Evolution Engine interpolates `normalizedAmps`, `relativeFreqs`, and `residualBands` independently across snapshots -- component-matching across snapshots with unequal partial counts [Tellman, Haken & Holloway 1995]. When one snapshot has 32 partials and another has 48, missing partials are interpolated with zero amplitude (fade in/out at the boundary).

**Why this priority**: The Evolution Engine is P2 because it requires Harmonic Memory (M5) as a prerequisite and is primarily a sound design tool for ambient/cinematic production, not a core playability feature. It is powerful but less universally needed than cross-synthesis and stereo spread. The foundational spectral morphing work is mature [Serra, Rubine & Dannenberg 1990; Caetano & Rodet 2011, IRCAM]. Commercial precedents: Alchemy 4-corner morph pad, Kyma MorphedSpectrum, Cameleon 5000 spectral morphing.

**Independent Test**: Can be tested by populating 2+ Memory Slots with distinct timbral snapshots, enabling Evolution with a known speed, playing a sustained MIDI note, and verifying the output spectrum changes over time (spectral centroid should oscillate between the centroids of the source snapshots).

**Acceptance Scenarios**:

1. **Given** Evolution is enabled with Slots 1 and 3 populated (2 waypoints), **When** the user plays a sustained MIDI note, **Then** the timbre continuously morphs between Slot 1 and Slot 3 at the configured Evolution Speed. The morph is audible as a gradual timbral shift.
2. **Given** Evolution Speed is set to minimum (0.01 Hz), **When** a note sustains for 10 seconds, **Then** the morph position changes by approximately 0.1 (10% of the way through one cycle). The timbral change is barely perceptible.
3. **Given** Evolution Speed is set to maximum (10 Hz), **When** a note sustains, **Then** the morph cycles rapidly, creating a tremolo-like timbral effect.
4. **Given** Evolution Depth is 0%, **When** Evolution is enabled, **Then** the timbre remains static at the current position. Evolution Speed has no audible effect.
5. **Given** Evolution Depth is 100% with 4 populated waypoint slots, **When** a note sustains, **Then** the morph traverses the full range across all 4 snapshots.
6. **Given** the Evolution Engine is driving morph position, **When** the user also adjusts the manual Morph Position parameter, **Then** the manual morph acts as an offset or override (additive or dominant depending on Evolution Mode -- see FR requirements). The two do not fight.
7. **Given** Evolution is active between Slot 1 (32 partials) and Slot 3 (48 partials), **When** the morph position is near Slot 3, **Then** partials 33-48 fade in smoothly from zero amplitude. When moving back toward Slot 1, they fade out. No clicks or pops occur at the partial count boundary.
8. **Given** Evolution is enabled, **When** the user triggers a new MIDI note-on, **Then** the evolution phase continues from its current position (does not reset to 0). Timbral continuity is maintained across note events.

---

### User Story 4 - Harmonic Modulators: LFO-Driven Partial Animation (Priority: P2)

A synthesist wants to add movement and life to a static harmonic snapshot. They enable **Harmonic Modulator 1** and assign it to modulate the amplitude of partials 8-16 with a 2 Hz triangle wave at 50% depth. The upper harmonics now pulse gently, adding a subtle animation to the timbre. They add a second modulator targeting partials 1-4 with a slower sine wave, creating a breathing quality in the fundamental region. The modulators operate independently and can target different partial ranges with different waveforms, rates, and depths.

Additionally, a **Detune Spread** control applies slight frequency offsets to partials, creating perceptual richness and chorus-like effects -- a standard additive technique [Roads 1996]. The detune amount scales with harmonic number (higher partials get more detune) for a natural chorus effect.

**Why this priority**: Harmonic modulators are P2 because they add timbral animation to what would otherwise be static snapshots, but they are not essential for the core instrument to function. They are straightforward to implement since the oscillator bank already exposes per-partial amplitude control. Detune spread is included here because it is a per-partial frequency modulation (conceptually related).

**Independent Test**: Can be tested by loading a static snapshot, enabling a modulator with a known waveform/rate/depth on a known partial range, and verifying the output amplitude of targeted partials oscillates at the configured rate while untargeted partials remain static.

**Acceptance Scenarios**:

1. **Given** Modulator 1 is configured with: target range partials 8-16, waveform Triangle, rate 2 Hz, depth 50%, **When** the user plays a sustained MIDI note, **Then** the amplitudes of partials 8-16 oscillate between 50% and 100% of their model amplitude at 2 Hz. Partials outside the range (1-7, 17-48) are unaffected.
2. **Given** Modulator 1 targets partials 1-8 and Modulator 2 targets partials 9-16, **When** both are active simultaneously, **Then** each modulator operates independently on its assigned range. Overlapping ranges (if configured) multiply their effects.
3. **Given** Modulator depth is 0%, **When** the modulator is enabled, **Then** no amplitude change is audible -- the modulator has no effect.
4. **Given** Modulator depth is 100%, **When** using a sine waveform at 4 Hz, **Then** targeted partial amplitudes sweep from 0% to 100% of their model amplitude at 4 Hz, creating a pronounced tremolo on those partials.
5. **Given** Detune Spread is 0% (default), **When** the user plays a note, **Then** partial frequencies match the harmonic model exactly (no detuning).
6. **Given** Detune Spread is 50%, **When** the user plays a note, **Then** each partial is offset by a small frequency deviation: `detuneOffset_n = detuneSpread * n * kDetuneMaxCents` (scaling with harmonic number). The result is a chorus-like widening without altering the fundamental pitch.
7. **Given** Detune Spread is active and Stereo Spread is also active, **When** the user plays a note, **Then** both effects apply independently: detuned partials are distributed across the stereo field. The combined effect is a rich, wide, animated sound.
8. **Given** a modulator is active, **When** the user changes the modulator rate or depth, **Then** the change is applied smoothly (parameter smoothing) with no click or abrupt transition.

---

### User Story 5 - Multi-Source Blending: Spectral Interpolation Across Sources (Priority: P3)

An advanced sound designer wants to create hybrid timbres by blending the harmonic content of multiple sources simultaneously. They populate Memory Slots 1-4 with different captured timbres and configure the **Multi-Source Blender** with per-source weight controls. Slot 1 (violin) at 60% weight and Slot 3 (choir) at 40% weight produces a hybrid timbre that is part-violin, part-choir. The blender interpolates `normalizedAmps`, `relativeFreqs`, and `residualBands` across the weighted sources using component-matching across snapshots with unequal partial counts [Tellman et al. 1995].

Optionally, the blender can incorporate one live analysis source alongside stored snapshots -- running the live sidechain analysis pipeline while blending its output with stored Memory Slot data. This is the most CPU-intensive configuration (live analysis pipeline + N stored snapshots + interpolation).

**Why this priority**: Multi-source blending is the most complex creative extension and requires robust single-source infrastructure. It multiplies CPU cost when using live sources (each live analysis pipeline is a full YIN + dual STFT + partial tracking chain). The primary value is for studio sound design rather than live performance. Stored snapshots are cheap to blend; live sources are expensive. Limiting to 2 simultaneous live sources + N stored snapshots is recommended [DSP plan Section 15, Priority 6].

**Independent Test**: Can be tested by populating 2+ Memory Slots with spectrally distinct timbres, configuring blend weights, and verifying the output spectral centroid is the weighted average of the source centroids.

**Acceptance Scenarios**:

1. **Given** Slot 1 and Slot 3 contain captured snapshots, **When** the blender is configured with Slot 1 weight=0.5 and Slot 3 weight=0.5, **Then** the oscillator bank plays from a blended harmonic model: `blendedAmp_n = 0.5 * slot1Amp_n + 0.5 * slot3Amp_n`, `blendedRelativeFreq_n = 0.5 * slot1RelFreq_n + 0.5 * slot3RelFreq_n`.
2. **Given** Slot 1 weight=1.0 and Slot 3 weight=0.0, **When** the user plays a note, **Then** the output is identical to recalling Slot 1 alone.
3. **Given** 3 sources with weights that sum to 1.0, **When** the user plays a note, **Then** the blended model is a weighted sum of all 3 sources' spectral data.
4. **Given** Slot 1 has 24 partials and Slot 3 has 48 partials, **When** blended at 50/50, **Then** partials 25-48 have amplitude `0.5 * slot3Amp_n + 0.5 * 0.0` (Slot 1 contributes zero for those partials). No artifacts at the partial count boundary.
5. **Given** the blender is configured with one live sidechain source and one Memory Slot, **When** the user plays MIDI notes, **Then** the live analysis output is blended with the stored snapshot in real time. Changes in the live source are reflected in the blended output at the analysis frame rate.
6. **Given** the blender is active, **When** the user adjusts source weights in real time, **Then** the timbral blend transitions smoothly (weight changes are parameter-smoothed) with no click or discontinuity.

---

### Edge Cases

- **Cross-synthesis with no source loaded**: If Timbral Blend > 0 but no analysis has been run (no sample, no sidechain, no recalled snapshot), the source contribution is zero (pure harmonic series fallback). The output is identical to Timbral Blend = 0.
- **Stereo Spread with mono output bus**: If the host requests mono output, stereo spread has no effect. The oscillator bank sums both channels to mono.
- **Evolution with only 1 populated slot**: Evolution has no effect -- there is nothing to drift between. The timbre remains static at the single snapshot.
- **Evolution with 0 populated slots**: Evolution is disabled (no waypoints).
- **Modulator on partials beyond numPartials**: If the modulator targets partials 40-48 but the current model only has 24 active partials, the modulator has no audible effect (those partials have zero amplitude).
- **Multi-source blending with all weights at 0**: The blended model is empty (zero amplitude for all partials). Output is silence.
- **Multi-source blending weights that don't sum to 1.0**: Weights are normalized internally: `effectiveWeight_i = weight_i / sum(weights)`. If all weights are 0, no normalization occurs (silence).
- **Detune Spread + Inharmonicity Amount interaction**: Detune spread is additive with inharmonic deviation: `finalFreq_n = (n + deviation_n * inharmonicityAmount + detuneOffset_n) * targetPitch`. Both effects stack.
- **Stereo Spread during crossfade (source switch, note steal)**: Pan positions are maintained during crossfade -- the old and new oscillator states both use their respective pan coefficients.
- **Evolution Engine across note boundaries**: Evolution phase is global (not per-note). New notes inherit the current evolution position.
- **Parameter automation of Evolution Speed/Depth**: Host automation applies smoothly via parameter smoothing. No special handling needed.

## Requirements *(mandatory)*

### Functional Requirements

#### Phase 17: Harmonic Cross-Synthesis

- **FR-001**: System MUST provide a **Timbral Blend** parameter (0.0-1.0, default 1.0) that interpolates between a pure harmonic series (0.0) and the analyzed source's spectral shape (1.0). At 0.0, `relativeFreq_n = n`, `normalizedAmp_n = 1/n` rolloff, `inharmonicDeviation_n = 0`. At 1.0, all values come from the current harmonic model.
- **FR-002**: System MUST compute the blended harmonic model per frame as: `effectiveRelativeFreq_n = lerp(n, sourceRelativeFreq_n, timbralBlend)`, `effectiveAmp_n = lerp(pureAmp_n, sourceNormalizedAmp_n, timbralBlend)`, `effectiveDeviation_n = sourceDeviation_n * timbralBlend * inharmonicityAmount`. Note: `inharmonicityAmount` is the existing M5 Inharmonicity Amount parameter (normalized [0,1]) — not a new M6 parameter. At blend=0 the pure reference has zero deviation (FR-001), so the formula correctly produces zero deviation regardless of inharmonicityAmount.
- **FR-003**: System MUST support real-time source switching by recalling different Harmonic Memory slots while MIDI notes are active, with click-free crossfade over ~10ms using the existing manual freeze crossfade mechanism.
- **FR-004**: System MUST generate the pure harmonic series reference using `1/n` amplitude rolloff (natural harmonic decay), L2-normalized to match the source model's normalization. The pure reference is a compile-time or prepare-time constant, not recomputed per frame.
- **FR-005**: The Timbral Blend parameter MUST be smoothed via `OnePoleSmoother` to prevent clicks when automated or adjusted in real time.

#### Phase 18: Stereo Partial Spread

- **FR-006**: System MUST provide a **Stereo Spread** parameter (0.0-1.0, default 0.0) that distributes partials across the stereo field. At 0.0, all partials are center-panned (mono). At 1.0, maximum L/R alternation.
- **FR-007**: The oscillator bank MUST produce stereo output (two sum buffers: left and right) when Stereo Spread > 0. The `process()` method signature MUST be extended to output stereo samples.
- **FR-008**: Per-partial pan position MUST be computed as: odd partials (harmonicIndex 1, 3, 5, ...) pan left by `spread * panAmount`, even partials (harmonicIndex 2, 4, 6, ...) pan right by `spread * panAmount`. Pan law: constant-power (`left = cos(angle)`, `right = sin(angle)` where `angle = pi/4 + panPosition * pi/4`).
- **FR-009**: Partial 1 (fundamental) MUST remain at center or at reduced spread offset (e.g., `fundamentalSpread = spread * 0.25`) to preserve bass mono compatibility.
- **FR-010**: Pan positions MUST be recalculated per frame update (not per sample) -- one multiplication per partial per frame. The cost is negligible.
- **FR-011**: Stereo Spread parameter changes MUST be smoothed via `OnePoleSmoother` (~5-10ms transition) to prevent spatial jumps.
- **FR-012**: The residual synthesizer output MUST remain mono-center and be mixed into both stereo channels equally. The residual does not participate in stereo spread.
- **FR-013**: When the host requests mono output (single-channel bus arrangement), stereo spread MUST have no effect -- both channels are summed to mono.

#### Phase 19: Evolution Engine

- **FR-014**: System MUST provide an **Evolution Enable** toggle parameter (on/off, default off).
- **FR-015**: System MUST provide an **Evolution Speed** parameter (0.01-10.0 Hz, default 0.1 Hz) controlling the rate of autonomous timbral drift.
- **FR-016**: System MUST provide an **Evolution Depth** parameter (0.0-1.0, default 0.5) controlling the amplitude of the morph excursion.
- **FR-017**: System MUST provide an **Evolution Mode** parameter (string list: "Cycle", "PingPong", "Random Walk", default "Cycle") controlling the morph trajectory:
  - **Cycle**: morph position traverses waypoints in order and wraps (1->2->3->4->1->2->...).
  - **PingPong**: morph position bounces at endpoints (1->2->3->4->3->2->1->2->...).
  - **Random Walk**: morph position drifts randomly with configurable step size, constrained within depth range.
- **FR-018**: The Evolution Engine MUST use populated Harmonic Memory slots as waypoints. Only occupied slots participate in the evolution sequence. Empty slots are skipped.
- **FR-019**: Interpolation between waypoints MUST use the existing `lerpHarmonicFrame()` and `lerpResidualFrame()` functions for smooth timbral morphing. Component-matching across snapshots with unequal partial counts: missing partials interpolate with zero amplitude [Tellman et al. 1995].
- **FR-020**: The Evolution Engine phase MUST be global (not per-note). It advances continuously regardless of MIDI note activity. New notes inherit the current evolution position.
- **FR-021**: The Evolution Engine MUST coexist with the manual Morph Position parameter. When Evolution is active, the final morph output is `clamp(evolutionPosition + manualMorphOffset, 0.0, 1.0)` where `manualMorphOffset = morphPosition - 0.5` (centered at 0.5 = no offset). This allows the performer to bias the evolution trajectory via manual control.
- **FR-022**: When Evolution is active, the system MUST bypass the normal freeze/morph interpolation path (State A / State B) and instead interpolate directly between the evolution waypoint snapshots. Manual freeze is overridden while evolution is active.
- **FR-023**: Evolution Speed and Depth changes MUST be parameter-smoothed to prevent abrupt changes in the evolution trajectory.

#### Phase 20: Harmonic Modulators

- **FR-024**: System MUST provide **2 independent harmonic modulators** (Modulator 1 and Modulator 2), each with the following parameters:
  - **Enable** (on/off, default off)
  - **Waveform** (string list: "Sine", "Triangle", "Square", "Saw", "Random S&H"; default "Sine")
  - **Rate** (0.01-20.0 Hz, default 1.0 Hz)
  - **Depth** (0.0-1.0, default 0.0)
  - **Target Range Start** (1-48, default 1) -- first partial affected
  - **Target Range End** (1-48, default 48) -- last partial affected
  - **Target** (string list: "Amplitude", "Frequency", "Pan"; default "Amplitude")
- **FR-025**: Amplitude modulation MUST be multiplicative: `effectiveAmp_n = modelAmp_n * (1.0 - depth + depth * lfoValue)` where `lfoValue` is 0.0-1.0 (unipolar). At depth=0, no modulation. At depth=1.0, amplitude sweeps from 0 to modelAmp_n.
- **FR-026**: Frequency modulation MUST be additive in cents: `effectiveFreq_n = modelFreq_n * pow(2.0, depth * lfoValue * kModMaxCents / 1200.0)` where `kModMaxCents = 50` (maximum +/-50 cents deviation at full depth). The LFO is bipolar (-1.0 to +1.0) for frequency modulation.
- **FR-027**: Pan modulation MUST offset the per-partial pan position: `effectivePan_n = basePan_n + depth * lfoValue * 0.5` (bipolar LFO, +/-0.5 pan range at full depth). Clamped to [-1.0, +1.0]. Only effective when Stereo Spread > 0.
- **FR-028**: When two modulators target overlapping partial ranges with the same target type (e.g., both modulate amplitude of partials 5-12), their effects MUST multiply (for amplitude) or add (for frequency/pan).
- **FR-029**: Each modulator's LFO MUST be free-running (not note-synced). Phase advances continuously regardless of MIDI activity.
- **FR-030**: System MUST provide a **Detune Spread** parameter (0.0-1.0, default 0.0) that applies per-partial frequency offsets for chorus-like richness [Roads 1996]. The offset scales with harmonic number: `detuneOffset_n = detuneSpread * n * kDetuneMaxCents` where `kDetuneMaxCents = 15` cents. Odd and even partials are detuned in opposite directions (+/-) for maximum width.
- **FR-031**: Detune offsets MUST be deterministic and consistent across note events (same detune at same spread value). They are computed once per frame update, not per sample.
- **FR-032**: Detune Spread MUST be additive with inharmonic deviation: `finalFreq_n = (n + deviation_n * inharmonicityAmount) * targetPitch * pow(2.0, detuneOffset_n / 1200.0)`.
- **FR-033**: All modulator parameter changes MUST be smoothed to prevent clicks.

#### Phase 21: Multi-Source Blending

- **FR-034**: System MUST provide a **Multi-Source Blend Enable** toggle (on/off, default off).
- **FR-035**: System MUST provide **per-slot blend weights** for each of the 8 Memory Slots (0.0-1.0 each, default 0.0). Weights are normalized internally before blending: `effectiveWeight_i = weight_i / sum(weights)`.
- **FR-036**: System MUST provide a **Live Source Weight** parameter (0.0-1.0, default 0.0) that blends the current live analysis (sample or sidechain) into the multi-source mix alongside stored snapshots.
- **FR-037**: The blended harmonic model MUST be computed as a weighted sum: `blendedAmp_n = sum(weight_i * sourceAmp_n_i)`, `blendedRelativeFreq_n = sum(weight_i * sourceRelFreq_n_i)`, `blendedResidualBands_k = sum(weight_i * sourceResidualBands_k_i)`.
- **FR-038**: Component-matching across snapshots with unequal partial counts MUST be handled by treating missing partials as zero amplitude: if source A has 24 partials and source B has 48, partials 25-48 in source A contribute zero to the blend.
- **FR-039**: When all weights are zero, the blended model MUST produce silence (no fallback to any single source).
- **FR-040**: Multi-source blending MUST override the normal recall/freeze path. When enabled, the oscillator bank plays from the blended model instead of any single recalled snapshot.
- **FR-041**: Weight parameter changes MUST be smoothed to prevent timbral discontinuities.
- **FR-042**: The system SHOULD limit simultaneous live analysis sources to 1 (the existing LiveAnalysisPipeline). Multiple live sources would require multiple analysis pipelines, which is deferred to a future milestone. Multi-source blending of stored snapshots has no CPU scaling concern.

#### General / Cross-Cutting

- **FR-043**: All new parameters MUST follow the project naming convention: `k{Feature}{Parameter}Id` in `plugin_ids.h`, registered in the controller with appropriate value ranges, and handled in `processParameterChanges()`.
- **FR-044**: All new parameters MUST be included in processor state save/load (`getState()`/`setState()`), with backward compatibility for version 5 (M5) state files (new parameters initialize to defaults when loading old state).
- **FR-045**: All new DSP processing MUST be real-time safe: no allocations, no locks, no exceptions, no I/O on the audio thread.
- **FR-046**: The Stereo Spread and Detune Spread features MUST be implemented in the `HarmonicOscillatorBank` class (Layer 2, KrateDSP) as they are general-purpose additive synthesis features. The Evolution Engine and Harmonic Modulators MUST be plugin-local (`plugins/innexus/src/dsp/`) as they are specific to the Innexus workflow.
- **FR-047**: Cross-synthesis Timbral Blend MUST be implemented in the processor's frame processing pipeline (between model builder output and oscillator bank input), as it modifies the harmonic model before synthesis.
- **FR-048**: Multi-Source Blending MUST be implemented as a new `HarmonicBlender` class in `plugins/innexus/src/dsp/` that accepts multiple `HarmonicSnapshot` inputs and produces a single blended `HarmonicFrame` + `ResidualFrame` for the oscillator bank.
- **FR-049**: The full processing pipeline order when all features are active MUST be: (1) Source selection: Multi-Source Blend (if enabled) OR Cross-Synthesis Timbral Blend (if no blend) OR single recalled/live source → (2) Evolution Engine (if enabled, overrides source with waypoint interpolation; skipped if Multi-Source Blend is active) → (3) Harmonic Filter (per-partial amplitude mask) → (4) Harmonic Modulators (per-partial amplitude/frequency/pan animation) → (5) Oscillator Bank (synthesis with stereo spread and detune).
- **FR-050**: The `HarmonicOscillatorBank` stereo API MUST be added as a new method `processStereo(float& left, float& right)` alongside the existing `float process()`. The existing mono method is preserved for backward compatibility. The processor calls `processStereo()` for stereo output.
- **FR-051**: Modulator LFO phase accumulators MUST be initialized to 0.0 in `prepare()` / `setupProcessing()` and MUST NOT reset on MIDI note events. This ensures deterministic behavior from plugin activation.
- **FR-052**: When Multi-Source Blend (FR-034) is enabled, the Evolution Engine (FR-014) MUST be ignored. Multi-source blend takes priority and produces the model directly from weighted snapshots. The user must disable blend to use evolution.

### Key Entities

- **Timbral Blend State**: Pure harmonic reference (constant) + source model + blend position -> blended model per frame.
- **Stereo Pan Map**: `std::array<float, kMaxPartials>` of per-partial pan positions, recalculated per frame when spread changes.
- **Evolution State**: Current phase (0.0-1.0 across waypoint sequence), direction (for PingPong), waypoint list (populated slot indices), LFO accumulator.
- **Harmonic Modulator**: LFO phase accumulator, waveform generator, target range, depth, rate. Two instances.
- **Detune Map**: `std::array<float, kMaxPartials>` of per-partial frequency offsets in cents, recalculated when detuneSpread changes.
- **HarmonicBlender**: Accepts N weighted `HarmonicSnapshot` inputs + optional live frame, produces a single blended output frame.

### New Parameter IDs (M6 Block: 600-699)

| Parameter | ID | Range | Default | Type |
|-----------|-----|-------|---------|------|
| Timbral Blend | 600 | 0.0-1.0 | 1.0 | RangeParameter |
| Stereo Spread | 601 | 0.0-1.0 | 0.0 | RangeParameter |
| Evolution Enable | 602 | 0/1 | 0 | RangeParameter (stepCount=1) |
| Evolution Speed | 603 | 0.01-10.0 Hz | 0.1 | RangeParameter |
| Evolution Depth | 604 | 0.0-1.0 | 0.5 | RangeParameter |
| Evolution Mode | 605 | 0-2 | 0 | StringListParameter |
| Mod1 Enable | 610 | 0/1 | 0 | RangeParameter (stepCount=1) |
| Mod1 Waveform | 611 | 0-4 | 0 | StringListParameter |
| Mod1 Rate | 612 | 0.01-20.0 Hz | 1.0 | RangeParameter |
| Mod1 Depth | 613 | 0.0-1.0 | 0.0 | RangeParameter |
| Mod1 Range Start | 614 | 1-48 | 1 | RangeParameter (stepCount=47) |
| Mod1 Range End | 615 | 1-48 | 48 | RangeParameter (stepCount=47) |
| Mod1 Target | 616 | 0-2 | 0 | StringListParameter |
| Mod2 Enable | 620 | 0/1 | 0 | RangeParameter (stepCount=1) |
| Mod2 Waveform | 621 | 0-4 | 0 | StringListParameter |
| Mod2 Rate | 622 | 0.01-20.0 Hz | 1.0 | RangeParameter |
| Mod2 Depth | 623 | 0.0-1.0 | 0.0 | RangeParameter |
| Mod2 Range Start | 624 | 1-48 | 1 | RangeParameter (stepCount=47) |
| Mod2 Range End | 625 | 1-48 | 48 | RangeParameter (stepCount=47) |
| Mod2 Target | 626 | 0-2 | 0 | StringListParameter |
| Detune Spread | 630 | 0.0-1.0 | 0.0 | RangeParameter |
| Blend Enable | 640 | 0/1 | 0 | RangeParameter (stepCount=1) |
| Blend Slot 1 Weight | 641 | 0.0-1.0 | 0.0 | RangeParameter |
| Blend Slot 2 Weight | 642 | 0.0-1.0 | 0.0 | RangeParameter |
| Blend Slot 3 Weight | 643 | 0.0-1.0 | 0.0 | RangeParameter |
| Blend Slot 4 Weight | 644 | 0.0-1.0 | 0.0 | RangeParameter |
| Blend Slot 5 Weight | 645 | 0.0-1.0 | 0.0 | RangeParameter |
| Blend Slot 6 Weight | 646 | 0.0-1.0 | 0.0 | RangeParameter |
| Blend Slot 7 Weight | 647 | 0.0-1.0 | 0.0 | RangeParameter |
| Blend Slot 8 Weight | 648 | 0.0-1.0 | 0.0 | RangeParameter |
| Blend Live Weight | 649 | 0.0-1.0 | 0.0 | RangeParameter |

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: Timbral Blend sweep from 0.0 to 1.0 produces a monotonically increasing spectral similarity to the source model, measurable as correlation coefficient > 0.95 between output spectrum and source model at blend=1.0. Correlation is computed as the Pearson correlation coefficient between the output oscillator bank's per-partial amplitude array and the `normalizedAmps` array of the source `HarmonicSnapshot`, using partials 1 through `numPartials`.
- **SC-002**: Stereo Spread at 1.0 produces inter-channel spectral decorrelation > 0.8 (measured as 1 - normalized cross-correlation between left and right channel spectra). At Spread=0.0, decorrelation < 0.01 (mono). Normalized cross-correlation at zero lag is defined as: `NCC = xcorr(L, R) / sqrt(xcorr(L, L) * xcorr(R, R))` where `xcorr(a, b) = sum(a[i] * b[i])` over one analysis window (512 samples minimum). Decorrelation = `1 - NCC`.
- **SC-003**: Evolution Engine produces measurable spectral centroid variation over time: standard deviation of spectral centroid across a 10-second sustained note > 100 Hz when evolving between two spectrally distinct snapshots (e.g., dark vs bright).
- **SC-004**: Harmonic modulator amplitude modulation at 2 Hz produces measurable amplitude variation in targeted partials with depth-proportional modulation index (measured modulation depth within +/-5% of configured depth).
- **SC-005**: Detune Spread at 1.0 produces chorus-like spectral widening measurable as increased spectral bandwidth compared to Detune=0.0. The output remains in tune (fundamental frequency deviation < 1 cent).
- **SC-006**: Multi-source blending with 2 sources at equal weights produces output spectral centroid within +/-10% of the arithmetic mean of the two source centroids.
- **SC-007**: All parameter transitions (Timbral Blend, Stereo Spread, Evolution Speed/Depth, Modulator Rate/Depth, Detune Spread, Blend Weights) MUST be click-free when swept at maximum automation rate. No sample-level discontinuities detectable above -80 dBFS.
- **SC-008**: CPU usage for the full creative extensions layer (all features active: stereo spread, 2 modulators, evolution, detune) MUST be < 1.0% additional CPU at 44.1 kHz beyond the base M5 instrument cost. The oscillator bank + creative extensions combined MUST remain < 2.0% total CPU.
- **SC-009**: All new parameters round-trip through state save/load with floating-point tolerance of 1e-6 per field. Version 5 (M5) state files load without error, with all M6 parameters at defaults.
- **SC-010**: Stereo output is bit-identical to mono when Stereo Spread = 0.0 (left == right channel).
- **SC-011**: Multi-source blending with a single source at weight 1.0 produces output identical (within floating-point tolerance) to recalling that source directly via Memory Recall.

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- M1-M5 are fully implemented and stable: oscillator bank, residual synthesizer, harmonic memory (8 slots), manual freeze, morph interpolation, harmonic filter, live sidechain analysis, sample analysis, and state persistence all function correctly.
- The `HarmonicOscillatorBank` currently outputs mono (single float per `process()` call). Stereo output requires extending this interface.
- The `lerpHarmonicFrame()` and `lerpResidualFrame()` functions in `harmonic_frame_utils.h` correctly handle interpolation of all frame fields including partial count mismatches.
- The existing manual freeze crossfade mechanism (~10ms) is reusable for source switching in cross-synthesis.
- Parameter ID block 600-699 is available and does not conflict with existing IDs (M1: 200-201, M2: 400-403, M3: 500-501, M4: 300-303, M5: 304-306).
- The plugin state version will increment from 5 (M5) to 6 (M6) for backward-compatible state loading.

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| `HarmonicOscillatorBank` | `dsp/include/krate/dsp/processors/harmonic_oscillator_bank.h` | MUST EXTEND for stereo output (FR-007), detune spread (FR-030-032). Currently mono, SoA layout with 48 MCF oscillators. |
| `lerpHarmonicFrame()` | `dsp/include/krate/dsp/processors/harmonic_frame_utils.h` | REUSE for evolution interpolation (FR-019), multi-source blending (FR-037), and cross-synthesis blend (FR-002). |
| `lerpResidualFrame()` | `dsp/include/krate/dsp/processors/harmonic_frame_utils.h` | REUSE for evolution and multi-source residual blending. |
| `applyHarmonicFilter()` | `dsp/include/krate/dsp/processors/harmonic_frame_utils.h` | REUSE -- filter applies after morph/evolution/blend, before oscillator bank. |
| `HarmonicSnapshot` | `dsp/include/krate/dsp/processors/harmonic_snapshot.h` | REUSE as the data format for evolution waypoints and multi-source blend inputs. |
| `MemorySlot` | `dsp/include/krate/dsp/processors/harmonic_snapshot.h` | REUSE -- 8 memory slots serve as evolution waypoints and blend sources. |
| `captureSnapshot()` | `dsp/include/krate/dsp/processors/harmonic_snapshot.h` | REUSE for converting frames to snapshots during evolution. |
| `OnePoleSmoother` | `dsp/include/krate/dsp/primitives/smoother.h` | REUSE for all parameter smoothing (FR-005, FR-011, FR-023, FR-033, FR-041). |
| `HarmonicFrame` / `Partial` | `dsp/include/krate/dsp/processors/harmonic_types.h` | REUSE as the core data types flowing through the pipeline. |
| `ResidualFrame` | `dsp/include/krate/dsp/processors/residual_types.h` | REUSE for residual blending and evolution. |
| Manual freeze crossfade | `plugins/innexus/src/processor/processor.h` | REUSE mechanism for cross-synthesis source switching (FR-003). |
| Morph interpolation pipeline | `plugins/innexus/src/processor/processor.h` | REFERENCE -- evolution overrides this path (FR-022). |
| `Xorshift32` | `dsp/include/krate/dsp/core/random.h` | REUSE for Random Walk evolution mode and S&H modulator waveform. |

**Initial codebase search for key terms:**

```bash
grep -r "stereo" dsp/ plugins/innexus/
grep -r "evolution" dsp/ plugins/innexus/
grep -r "modulator" dsp/ plugins/innexus/
grep -r "detune" dsp/ plugins/innexus/
grep -r "cross.synth" dsp/ plugins/innexus/
grep -r "blender\|blend" dsp/include/ plugins/innexus/
```

**Search Results Summary**: No existing implementations found for stereo spread, evolution engine, harmonic modulators, detune spread, cross-synthesis blend control, or multi-source blender. These are all new components. The `lerpHarmonicFrame` and `lerpResidualFrame` utilities exist and handle the core interpolation math. The `Xorshift32` PRNG exists in core/random.h for Random Walk and S&H waveform generation.

### Forward Reusability Consideration

**Sibling features at same layer** (if known):
- The stereo spread and detune spread extensions to `HarmonicOscillatorBank` are general-purpose additive synthesis features that could benefit any future additive synth plugin in the monorepo.
- The LFO waveform generator (sine, triangle, square, saw, S&H) used by harmonic modulators could be extracted to a shared `dsp/` utility if other plugins need similar functionality. However, per the "no premature abstraction" principle, it should be plugin-local for M6 and promoted to shared if reuse materializes.

**Potential shared components** (preliminary, refined in plan.md):
- `HarmonicOscillatorBank::processStereo()` -- stereo extension is a natural Layer 2 addition
- Per-partial detune map computation -- general additive technique, belongs in oscillator bank
- LFO generator -- could be shared but keep plugin-local for now

## Implementation Verification *(mandatory at completion)*

### Compliance Status

*This section is EMPTY during specification phase and filled during implementation phase when /speckit.implement completes.*

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
| FR-017 | | |
| FR-018 | | |
| FR-019 | | |
| FR-020 | | |
| FR-021 | | |
| FR-022 | | |
| FR-023 | | |
| FR-024 | | |
| FR-025 | | |
| FR-026 | | |
| FR-027 | | |
| FR-028 | | |
| FR-029 | | |
| FR-030 | | |
| FR-031 | | |
| FR-032 | | |
| FR-033 | | |
| FR-034 | | |
| FR-035 | | |
| FR-036 | | |
| FR-037 | | |
| FR-038 | | |
| FR-039 | | |
| FR-040 | | |
| FR-041 | | |
| FR-042 | | |
| FR-043 | | |
| FR-044 | | |
| FR-045 | | |
| FR-046 | | |
| FR-047 | | |
| FR-048 | | |
| FR-049 | | |
| FR-050 | | |
| FR-051 | | |
| FR-052 | | |
| SC-001 | | |
| SC-002 | | |
| SC-003 | | |
| SC-004 | | |
| SC-005 | | |
| SC-006 | | |
| SC-007 | | |
| SC-008 | | |
| SC-009 | | |
| SC-010 | | |
| SC-011 | | |

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

**Recommendation**: Proceed to `/speckit.clarify` then `/speckit.plan`.
