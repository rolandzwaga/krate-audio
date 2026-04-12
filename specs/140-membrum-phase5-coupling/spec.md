# Feature Specification: Membrum Phase 5 -- Cross-Pad Coupling (Sympathetic Resonance)

**Feature Branch**: `140-membrum-phase5-coupling`
**Plugin**: Membrum (`plugins/membrum/`)
**Created**: 2026-04-12
**Status**: Draft
**Input**: Phase 5 scope from Spec 135 (Membrum Synthesized Drum Machine); builds on Phase 4 (`specs/139-membrum-phase4-pads/`)

## Clarifications

### Session 2026-04-12

- Q: What signal feeds the coupling engine as excitation — per-voice output, a per-pad mix, or the global voice mix? → A: Feed mono sum (L+R)/2 of the global voice mix into the coupling engine.
- Q: How is pad category derived from configuration? → A: Priority-ordered rule chain: (1) Membrane + pitch envelope active = Kick, (2) Membrane + noise exciter = Snare, (3) Membrane = Tom, (4) NoiseBody = Hat/Cymbal, (5) else = Perc.
- Q: How does the coefficient resolver merge Tier 1 computed gains with Tier 2 per-pair overrides? → A: Two-layer resolver: matrix stores `{computedGain, overrideGain, hasOverride}` per pair; at parameter-change time, resolver merges into flat `effectiveGain[32][32]`; engine receives resolved scalar via `setAmount()`.
- Q: What serialization format is used for per-pair coefficient overrides in state version 5? → A: Index-value pair list: `uint16 count` + repeated `(uint8 src, uint8 dst, float32 coeff)` — 6 bytes per entry; v4 migration writes count=0.
- Q: Where is the propagation delay placed in the signal chain, and is it global or per-pair? → A: One global `DelayLine` on the mixed voice signal BEFORE the coupling engine input. Signal chain: Stereo Membrum output → Mono sum (pressure) → Global DelayLine (0.5–2 ms) → `SympatheticResonance::process()`. If spatialization is added later, global delay serves as baseline with small per-pair offsets (±0.2 ms) based on distance.

## Background

Phase 4 (spec 139) shipped Membrum v0.4.0 with a complete 32-pad drum machine architecture: per-pad parameters in `PadConfig[32]`, GM-inspired default kit, kit and per-pad presets, 16 stereo output buses, and state version 4. Every pad has its own independent exciter type, body model, and 34 sound parameters. However, pads currently exist in acoustic isolation -- striking the kick has no effect on the snare wires, and toms do not resonate sympathetically when neighboring toms are hit.

Phase 5 introduces **cross-pad coupling**, the physical phenomenon where striking one drum in a kit causes other drums to resonate sympathetically. This is a defining characteristic of acoustic drum kits: the kick makes snare wires buzz, toms resonate when nearby toms are hit, and cymbals shimmer in response to loud strikes. The effect is frequency-selective -- coupling is strongest when the exciting frequency matches a natural mode of the receiving instrument. This is why tuning toms to different intervals from the snare reduces unwanted buzz in real kits.

Phase 5 is scoped to coupling infrastructure, the tiered control system (Tier 1 simple knobs + Tier 2 full matrix data model), integration with the existing `SympatheticResonanceSIMD` engine from KrateDSP, per-pad coupling parameters using the reserved parameter offsets (36-63) in `PadConfig`, and state version 5. Macro controls, Acoustic/Extended UI modes, and the custom VSTGUI editor remain deferred to Phase 6.

## Scientific Foundation

### Sympathetic Resonance Physics

Sympathetic resonance occurs when a vibrating body transfers energy to a second body whose natural frequencies are harmonically related to those of the first. In a drum kit, this manifests through air-coupled acoustic interaction between instruments:

1. **Snare wire buzz**: The kick drum's output excites the snare drum's resonant head, which in turn drives the snare wires into vibration. The effect is strongest when the kick's fundamental or harmonics align with the snare's resonant modes. Snare wires respond to a wide frequency range due to their dense modal structure (each wire is an independent vibrating string), but the amplitude of response is proportional to how closely the exciting frequency matches the snare's resonant frequency. Bilbao's penalty method collision model (`F_collision = K_c * [delta]_+^alpha_c`) describes the one-sided contact force, but for real-time synthesis this is simplified to amplitude-modulated bandpass-filtered noise (1-8 kHz) triggered when membrane displacement exceeds a threshold, with a comb filter on the noise (delay = snare wire length) for metallic resonance character.

2. **Tom sympathetic resonance**: Striking one tom causes neighboring toms to resonate, particularly when tuned to related intervals (octaves, fifths, fourths produce the strongest coupling). The effect is most noticeable when the two bodies share a harmonic relationship -- tuning toms to intervals of fifths (3:2 frequency ratio) produces complementary sympathetic tones that enhance the kit's overall sound. Tuning guides confirm that moving in 5-note intervals makes drums sound "bigger, fuller" with "complementary sympathetic tones," while 3-note increments produce "thinner and drier" sounds due to reduced coupling.

3. **Cymbal shimmer**: At high amplitudes, cymbals exhibit nonlinear mode coupling following von Karman plate theory. Geometric nonlinearity generates coupling between linear eigenmodes, inducing energy transfer from low to high frequency regimes through coupled oscillators with cubic nonlinearities (Touze et al., 2004; Dahl et al., DAFx 2019). This creates the characteristic shimmering, evolving texture of sustained cymbal hits.

4. **Build-up time**: Sympathetic response has a slower attack than direct excitation (physically accurate). The receiving instrument must accumulate energy from multiple cycles of the exciting signal before producing audible output. Air cavity coupling introduces a propagation delay of 0.5-2 ms between the exciting instrument and the receiving instrument.

### Frequency Selectivity

Coupling is strongest when the exciting frequency matches a natural mode of the receiving instrument. For a bandpass-filtered coupling matrix, each drum's output is filtered around the receiving drum's modal frequencies before being fed in as excitation. The coupling gain coefficients are small (0.0 to ~0.05 typical range) to prevent instability while producing audible sympathetic effects.

The frequency-dependent nature of the coupling means:
- Two drums tuned in unison or at octave intervals produce the strongest coupling
- Drums tuned to fifths (3:2) or fourths (4:3) produce moderate coupling
- Drums tuned to dissonant intervals (e.g., tritone) produce minimal coupling
- This matches real-world drum kit behavior where tuning toms to different intervals controls the amount of sympathetic buzz

### Chaigne-Lambourg Damping in Sympathetic Resonators

The existing `SympatheticResonanceSIMD` engine uses second-order driven resonators with damping derived from the Chaigne-Lambourg model (Chaigne & Lambourg, JASA 2001; Chaigne & Askenfelt, JASA 1994):

```
R_k = b1 + b3 * f_k^2
```

where b1 controls frequency-independent damping (air drag, viscous losses) and b3 controls frequency-dependent damping (internal material friction). The resonator's Q-factor is derived from these coefficients, with frequency-dependent Q scaling (lower Q at higher frequencies) matching the physical behavior where high-frequency sympathetic resonances decay faster than low-frequency ones. The existing implementation uses `Q_eff = Q_user * clamp(kQFreqRef / f, kMinQScale, 1.0)` to achieve this.

### Nonlinear Mode Coupling (Von Karman)

The von Karman plate equations describe how geometric nonlinearity creates coupling between linear eigenmodes of thin plates. For cymbals and gongs, this produces:
- Amplitude-dependent vibration behavior
- Energy cascade from low to high frequencies
- Transitions from deterministic to chaotic regimes at high amplitudes
- The characteristic "wash" and "shimmer" of cymbal sustain

Poirot, Bilbao, and Kronland-Martinet (2024) introduced a simplified and controllable model for mode coupling that employs efficient coupled filters for real-time sound synthesis, controlling energy transfer between filters through a coupling matrix. This validates the approach of using a coupling matrix with bandpass-filtered signal paths for cross-instrument interaction.

Note: Full nonlinear mode coupling (von Karman cubic terms) is already implemented as the "Nonlinear Coupling" parameter in the Unnatural Zone (Phase 2). Phase 5 coupling is the *inter-pad* sympathetic resonance (linear driven resonators), not the *intra-pad* nonlinear mode coupling.

### Stability Considerations

Coupling in a feedback-capable system requires stability guards:
- **Velocity scaling**: Coupling gain scales with the exciting instrument's velocity, preventing runaway at high volumes
- **Energy limiter**: An internal energy limiter prevents blow-up when multiple coupling paths create feedback loops
- **CPU caps**: Hard limits on total coupling computation prevent performance degradation with many active pads
- **Gain ceiling**: Per-pair coefficients capped at 0.05 -- well below instability thresholds for second-order driven resonators

## User Scenarios & Testing *(mandatory)*

### User Story 1 -- Snare Buzz from Kick (Priority: P1)

A producer loads Membrum with the default GM kit. They trigger the kick drum (MIDI 36) and hear the characteristic snare wire buzz that occurs naturally in acoustic drum kits. The buzz is subtle -- a secondary texture layered on top of the kick sound, not an overwhelming effect. The producer adjusts the "Snare Buzz" knob to increase or decrease the amount of sympathetic buzz. Setting it to zero eliminates the buzz entirely. Setting it to maximum produces an exaggerated, dramatic buzz effect.

**Why this priority**: Snare buzz from the kick is the single most recognizable and musically important form of sympathetic resonance in a drum kit. It is what makes the difference between a sterile electronic drum machine and a kit that "breathes." This is the minimum viable coupling feature.

**Independent Test**: Trigger the kick with Snare Buzz at various settings. Verify that the snare pad's output contains energy in the snare wire frequency range (1-8 kHz) only when the kick is struck, and that the buzz amplitude scales with the Snare Buzz parameter.

**Acceptance Scenarios**:

1. **Given** the default GM kit with Snare Buzz at 50% and Global Coupling at 100%, **When** MIDI note 36 (kick) is triggered at velocity 100, **Then** the output contains energy in the 1-8 kHz band at the snare's modal frequencies that was not present with coupling disabled.
2. **Given** Snare Buzz at 0%, **When** MIDI note 36 is triggered, **Then** the output is identical to Phase 4 behavior (no coupling contribution from snare buzz path).
3. **Given** Snare Buzz at 100% and Global Coupling at 100%, **When** MIDI note 36 is triggered, **Then** the snare buzz component is audibly present but does not exceed the kick's peak amplitude (coupling gain is bounded).
4. **Given** the kick is triggered repeatedly, **When** the snare pad is not directly triggered, **Then** the buzz component has a slower attack than the kick's direct sound (physically accurate build-up time from propagation delay).

---

### User Story 2 -- Tom Sympathetic Resonance (Priority: P1)

A producer strikes the high tom (MIDI 50) and hears the other toms resonate sympathetically. The sympathetic tones are subtle, adding depth and "aliveness" to the kit. The "Tom Resonance" knob controls the intensity of this effect. The sympathetic response is frequency-selective: toms tuned to harmonically related intervals resonate more strongly than those tuned to dissonant intervals.

**Why this priority**: Tom resonance is the second most important form of sympathetic coupling in a drum kit. Together with snare buzz, it defines the "acoustic" character of a physically modeled kit. Equal P1 with US1.

**Independent Test**: Configure two toms with related tuning (e.g., octave apart by adjusting Size). Strike one and measure spectral energy at the other's fundamental frequency. Compare with two toms at a dissonant interval. Verify frequency-selective coupling.

**Acceptance Scenarios**:

1. **Given** two toms tuned an octave apart (2:1 frequency ratio via Size parameter) with Tom Resonance at 50% and Global Coupling at 100%, **When** one tom is triggered, **Then** the second tom's fundamental frequency appears in the output with measurable energy above the noise floor.
2. **Given** two toms tuned a tritone apart with Tom Resonance at 50%, **When** one tom is triggered, **Then** the coupling energy at the second tom's fundamental is at least 12 dB below the octave-tuned case (frequency selectivity).
3. **Given** Tom Resonance at 0%, **When** any tom is triggered, **Then** no sympathetic energy appears at other toms' frequencies.
4. **Given** the default GM kit with Tom Resonance at 50%, **When** multiple toms are triggered in sequence, **Then** the kit sounds more "alive" and connected compared to Phase 4 behavior.

---

### User Story 3 -- Global Coupling Control (Priority: P1)

A producer wants to adjust the overall intensity of all coupling effects simultaneously. The "Global Coupling" knob acts as a master intensity control that scales all coupling paths (snare buzz, tom resonance, and any per-pair couplings). Setting it to zero disables all coupling with near-zero CPU cost. Setting it to maximum produces the most dramatic sympathetic effects.

**Why this priority**: A master coupling control is essential for quickly toggling the feature on/off and for managing overall CPU impact. It also provides a single point of control for users who do not want to adjust individual coupling paths.

**Independent Test**: Set Global Coupling to 0 and verify no coupling occurs and CPU usage is minimal. Set to 50% and verify coupling at half intensity. Set to 100% and verify maximum coupling.

**Acceptance Scenarios**:

1. **Given** Global Coupling at 0%, **When** any drum is triggered, **Then** the output is identical to Phase 4 behavior (complete bypass).
2. **Given** Global Coupling at 100% and Snare Buzz at 50%, **When** the kick is triggered, **Then** the buzz is at its configured half-intensity.
3. **Given** Global Coupling at 50% and Snare Buzz at 100%, **When** the kick is triggered, **Then** the effective buzz intensity is at 50% (Global scales all paths).
4. **Given** Global Coupling at 0%, **When** processing a 256-sample block with 8 active voices, **Then** the coupling engine adds less than 0.01% CPU overhead (early-out bypass).

---

### User Story 4 -- Per-Pad Coupling Amount (Priority: P2)

A sound designer wants fine-grained control over which pads participate in coupling and how strongly. Each pad has a per-pad "Coupling Amount" parameter (stored in the reserved `PadConfig` offset range) that controls how strongly that pad's output feeds into the coupling matrix as an exciter, and how strongly it responds to other pads' excitation.

**Why this priority**: Per-pad coupling provides the foundation for advanced sound design. Less critical than the Tier 1 knobs for general use, but essential for sound designers who want precise control over which drums interact acoustically.

**Independent Test**: Set one pad's coupling amount to 0 and verify it neither excites nor responds to coupling. Set it to maximum and verify it participates fully.

**Acceptance Scenarios**:

1. **Given** pad 1 (kick) with per-pad coupling amount at 0%, **When** MIDI note 36 is triggered with Snare Buzz and Global Coupling both at 100%, **Then** no coupling energy is sent from this strike (pad excluded as source).
2. **Given** pad 3 (snare) with per-pad coupling amount at 0%, **When** the kick is triggered with Snare Buzz and Global Coupling both at 100%, **Then** no snare buzz occurs (pad excluded as receiver).
3. **Given** two pads with per-pad coupling amounts at 100%, **When** one is triggered, **Then** the sympathetic energy is at the maximum level allowed by Global Coupling and the relevant Tier 1 knob.
4. **Given** per-pad coupling amount saved in a kit preset, **When** the preset is reloaded, **Then** the per-pad coupling amounts round-trip exactly.

---

### User Story 5 -- Coupling Matrix Data Model (Priority: P3)

A power user (or future Phase 6 UI) needs to set individual per-pair coupling coefficients for specific pad combinations. The underlying data model supports a full 32x32 matrix of coupling coefficients. In Phase 5, the matrix is populated by the Tier 1 knobs and pad category logic. In Phase 6, a visual matrix editor will provide direct access to individual coefficients.

**Why this priority**: The matrix data structure must exist in Phase 5 to support the Tier 1 knobs, but direct per-pair control is a Phase 6 UI concern. Phase 5 provides the data model and per-pair coefficient storage.

**Independent Test**: Programmatically set a specific pair's coupling coefficient to a non-zero value. Trigger the source pad. Verify coupling energy appears at the destination pad's frequencies.

**Acceptance Scenarios**:

1. **Given** the coupling matrix with pair (kick -> snare) coefficient at 0.03, **When** the kick is triggered, **Then** sympathetic energy appears in the output scaled by the 0.03 coefficient.
2. **Given** the coupling matrix with all coefficients at 0.0, **When** any drum is triggered, **Then** no coupling occurs regardless of Tier 1 knob settings (matrix zeros override).
3. **Given** per-pair coefficients saved in state, **When** state is reloaded, **Then** all per-pair coefficients round-trip exactly.
4. **Given** 8 pads actively sounding with non-zero coupling, **When** the CPU cap is reached, **Then** the engine gracefully degrades by skipping pairs with the lowest gain coefficients first, without audio artifacts.

---

### Edge Cases

- **All 32 pads with coupling enabled triggered simultaneously**: The CPU cap must limit total coupling computation. The engine skips coupling pairs with the lowest gain coefficients first when the cap is reached.
- **Coupling feedback loop (pad A excites pad B, pad B excites pad A)**: The energy limiter prevents exponential blow-up. Coupling coefficients in the 0-0.05 range naturally limit this, but the energy limiter is the safety net.
- **Pad with no active voices**: A pad that has finished decaying does not participate in coupling (neither as source nor destination). The coupling engine skips pads with no active voices.
- **Coupling with output routing**: Coupling energy is routed to the main output bus only. It represents the ambient acoustic interaction of the full kit, not a per-pad effect.
- **Preset migration (v4 to v5)**: Loading a v4 state blob results in all coupling parameters at their defaults (coupling disabled), preserving Phase 4 behavior exactly.
- **Per-pad coupling amount at 0**: A pad with coupling amount at 0 is completely excluded from both exciter and receiver roles, saving CPU.
- **Sample rate change**: The coupling engine recalculates resonator coefficients when the sample rate changes via `setupProcessing()`.
- **Coupling with choke groups**: When a voice is choked (fast-released), its contribution to the coupling matrix decays with the fast-release envelope (5 ms), not cutting abruptly.
- **Coupling during voice steal**: A stolen voice's coupling contribution is removed via `noteOff()` on the coupling engine, allowing the resonators to ring out naturally.
- **Kit preset with coupling + pad preset without**: Kit presets include per-pad coupling amounts. Per-pad sound presets exclude coupling amounts (coupling is a kit-level concern).

## Requirements *(mandatory)*

### Functional Requirements

#### Coupling Infrastructure

- **FR-001**: The system MUST integrate the existing `Krate::DSP::SympatheticResonance` engine (located at `dsp/include/krate/dsp/systems/sympathetic_resonance.h`) as the core coupling processor. The engine's `noteOn`/`noteOff`/`process` lifecycle maps to Membrum's voice events.
- **FR-002**: The system MUST feed the global voice mix (post-polyphony gain compensation, pre-master output) into the coupling engine as the excitation signal. The excitation signal MUST be formed as the mono sum `(L+R)/2` of the stereo global voice mix (representing a scalar acoustic pressure value). The coupling engine's output MUST be summed additively with the master output.
- **FR-003**: The coupling engine MUST extract the first 4 partials (per `kSympatheticPartialCount = 4`) from each active voice's body model to seed the resonator pool. Partial frequencies MUST account for the pad's Size, Tension/Air Coupling, and Mode Stretch settings.
- **FR-004**: Coupling MUST only be active between pads that are currently sounding (have at least one active voice). Pads with no active voices MUST be excluded from both source and destination roles to save CPU.
- **FR-005**: The coupling engine MUST support frequency-selective coupling: coupling strength between two pads MUST be proportional to how closely the source pad's spectral content matches the destination pad's natural modal frequencies. This is inherently provided by the bandpass-filtered resonator design of `SympatheticResonance`.
- **FR-006**: The system MUST support a propagation delay of 0.5-2 ms between the exciting instrument and the sympathetic response onset, modeling air cavity coupling. A single global `DelayLine` MUST be placed on the mono sum pressure signal BEFORE it enters the coupling engine. The canonical signal chain is: `Stereo Membrum output → Mono sum (L+R)/2 → Global DelayLine (0.5–2 ms) → SympatheticResonance::process()`. The delay MUST be implemented using the existing `DelayLine` primitive. If spatialization is introduced in a future phase, the global delay SHALL serve as the baseline, with small per-pair offsets (±0.2 ms) applied based on distance; no per-pair delays are in scope for Phase 5.

#### Tier 1 Controls (Default User-Facing)

- **FR-010**: The system MUST provide a "Snare Buzz" parameter (0.0 to 1.0) that controls the coupling intensity from kick-category pads to snare-category pads. Default: 0.0 (disabled).
- **FR-011**: The system MUST provide a "Tom Resonance" parameter (0.0 to 1.0) that controls the coupling intensity between all tom-category pads. Default: 0.0 (disabled).
- **FR-012**: The system MUST provide a "Global Coupling" parameter (0.0 to 1.0) that acts as a master scaling factor for all coupling paths. When set to 0.0, all coupling MUST be completely bypassed with minimal CPU cost (early-out). Default: 0.0 (disabled).
- **FR-013**: The Tier 1 knobs MUST map to the underlying coupling matrix coefficients. "Snare Buzz" sets the gain for kick-to-snare pairs. "Tom Resonance" sets the gain for tom-to-tom pairs. Both are scaled by "Global Coupling."
- **FR-014**: The per-sample coupling gain applied for any pair (src, dst) MUST be computed as: `globalCoupling * effectiveGain[src][dst] * padCouplingAmount[src] * padCouplingAmount[dst]`. The `effectiveGain[src][dst]` value is resolved by `CouplingMatrix` and already encodes the Tier 1 knob value (snareBuzz or tomResonance) multiplied by `kMaxCoefficient` (0.05), or the Tier 2 override gain. Applying the Tier 1 knob separately at audio time would double-count it.

#### Per-Pad Coupling Parameters

- **FR-020**: Each pad MUST have a per-pad "Coupling Amount" parameter stored at offset `kPadCouplingAmount = 36` within the pad's reserved parameter range. Default: 0.5 (moderate coupling participation). Range: 0.0 to 1.0.
- **FR-021**: The per-pad coupling amount MUST control both the pad's excitation strength (how much it drives coupling) and its reception sensitivity (how much it responds to coupling).
- **FR-022**: Per-pad coupling amounts MUST be included in kit presets (they are kit-level settings). Per-pad coupling amounts MUST be excluded from per-pad sound presets.
- **FR-023**: When per-pad coupling amount is 0.0, the pad MUST be completely excluded from coupling computation (CPU optimization -- no resonators registered, no signal contribution).

#### Coupling Matrix Data Model

- **FR-030**: The system MUST maintain a coupling coefficient storage that supports per-pair gain values using a two-layer resolver. Each pair MUST store `{computedGain, overrideGain, hasOverride}`. At parameter-change time, a resolver MUST merge these into a flat `effectiveGain[32][32]` array: when `hasOverride` is true for a pair, `effectiveGain = overrideGain`; otherwise `effectiveGain = computedGain`. The coupling engine receives the resolved scalar for each pair via `setAmount()`. The data model MUST support all 32x32 pad pairs, but only non-zero, active pairs are processed at runtime.
- **FR-031**: Per-pair coupling coefficients MUST be in the range [0.0, 0.05]. Values above 0.05 MUST be clamped on load and on set.
- **FR-032**: The coupling matrix MUST be stored as part of the plugin state (state version 5). Default: all per-pair coefficients computed from Tier 1 knob values and pad categories (not individually set).
- **FR-033**: The coupling matrix MUST distinguish pad categories for Tier 1 mapping using the following priority-ordered rule chain evaluated at runtime from pad configuration (not hardcoded to MIDI note):
  1. Membrane body AND pitch envelope active → **Kick**
  2. Membrane body AND noise exciter present → **Snare**
  3. Membrane body (no pitch envelope, no noise exciter) → **Tom**
  4. NoiseBody → **Hat/Cymbal**
  5. Any other configuration → **Perc**
  Rules are evaluated in order; the first matching rule wins.
- **FR-034**: When Tier 1 knobs change, the resolver MUST recompute `computedGain` for all affected pairs and regenerate the flat `effectiveGain[32][32]` array. Per-pair coefficients set directly (Tier 2 / programmatic access) MUST set `overrideGain` and `hasOverride = true` for that specific pair, causing the resolver to use `overrideGain` instead of `computedGain` for that pair only.

#### Stability and Safety

- **FR-040**: The coupling engine MUST include an energy limiter that prevents exponential blow-up from coupling feedback loops. The limiter MUST engage transparently (no audible artifacts) and MUST prevent the total coupling energy from exceeding -20 dBFS relative to full scale.
- **FR-041**: Coupling gain MUST scale with velocity: lower velocity strikes produce proportionally less coupling excitation. This models the physical behavior where louder hits cause more sympathetic resonance.
- **FR-042**: The system MUST enforce a CPU-safe hard cap on total coupling computation. When the number of active resonators exceeds the cap, the engine MUST skip resonators with the lowest gain or envelope values first.
- **FR-043**: The CPU cap MUST default to 64 active resonators (matching `kMaxSympatheticResonators`). The existing eviction logic in `SympatheticResonance` (evict quietest) handles overflow.

#### State Versioning

- **FR-050**: The plugin MUST use state version 5 (`kCurrentStateVersion = 5`).
- **FR-051**: Loading a v4 state blob MUST succeed and apply all Phase 4 parameters. All Phase 5 coupling parameters MUST take their default values: Global Coupling = 0.0, Snare Buzz = 0.0, Tom Resonance = 0.0, per-pad coupling amounts = 0.5, coupling delay = 1.0 ms. This preserves Phase 4 behavior exactly (coupling disabled by default).
- **FR-052**: Loading a v1/v2/v3 state blob MUST succeed via the existing migration chain (v1->v2->v3->v4->v5), with Phase 5 parameters taking defaults at the v4->v5 step.
- **FR-053**: State version 5 MUST include: the four global coupling parameters (Global Coupling, Snare Buzz, Tom Resonance, Coupling Delay), all 32 per-pad coupling amounts, and any per-pair coefficient overrides serialized as an index-value pair list. The override list format is: `uint16 count` followed by `count` entries of `(uint8 src, uint8 dst, float32 coeff)` — 6 bytes per entry, giving a maximum of 32*32 = 1024 entries (6 KB). When migrating from v4 state, the override list MUST be written with `count = 0` (no overrides), defaulting all pairs to Tier 1 computed values.

#### Parameter IDs

- **FR-060**: The following global parameter IDs MUST be added in the Phase 5 range (270-279):
  - `kGlobalCouplingId = 270` -- Global Coupling master knob (RangeParameter, 0.0-1.0, default 0.0)
  - `kSnareBuzzId = 271` -- Snare Buzz coupling intensity (RangeParameter, 0.0-1.0, default 0.0)
  - `kTomResonanceId = 272` -- Tom Resonance coupling intensity (RangeParameter, 0.0-1.0, default 0.0)
  - `kCouplingDelayId = 273` -- Coupling propagation delay in ms (RangeParameter, 0.5-2.0, default 1.0)
- **FR-061**: The per-pad coupling amount MUST use `PadParamOffset::kPadCouplingAmount = 36`, the first offset in the reserved range (36-63). The parameter ID for pad N is: `kPadBaseId + N * kPadParamStride + 36`.
- **FR-062**: Collision guards MUST verify that Phase 5 global IDs (270-279) do not overlap Phase 4 IDs (260) or per-pad ranges (1000+). A `static_assert` MUST enforce: `kSelectedPadId < kGlobalCouplingId`.

#### Integration with Voice Pool

- **FR-070**: When a voice is allocated (`noteOn`), the `VoicePool` MUST extract the first 4 partial frequencies from the allocated voice's body model and register them with the coupling engine via `SympatheticResonance::noteOn(voiceId, partials)`.
- **FR-071**: When a voice is released or choked, the `VoicePool` MUST notify the coupling engine via `SympatheticResonance::noteOff(voiceId)`. For choked voices (fast-release), the coupling resonators ring out naturally (the `SympatheticResonance` engine already handles orphaned resonators via envelope-based reclaim at -96 dB).
- **FR-072**: The coupling engine's `process()` MUST be called once per sample in the main processing loop, after all voices have been rendered for that sample but before the master output gain.
- **FR-073**: Coupling output MUST be routed to the main output bus only. Coupling does not route to auxiliary buses (it represents the ambient acoustic interaction of the full kit, not a per-pad effect).

### Key Entities

- **Coupling Matrix**: A data structure storing per-pair gain coefficients (float, 0.0-0.05). Stored in state. Populated by Tier 1 knobs using pad category logic. Supports direct per-pair overrides for Tier 2.
- **Pad Category**: Derived classification (Kick/Snare/Tom/Hat-Cymbal/Perc) used for Tier 1 knob mapping. Determined by the pad's body model, exciter type, and parameter configuration at runtime.
- **Coupling Engine**: Instance of `Krate::DSP::SympatheticResonance` integrated into the `Processor` class. Manages the resonator pool, processes coupling per-sample, includes SIMD acceleration and anti-mud HPF.
- **Per-Pad Coupling Amount**: A normalized [0.0, 1.0] parameter per pad controlling that pad's participation in coupling (both as source and receiver). Stored at `PadConfig` offset 36.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: When Global Coupling is 0.0, the plugin output MUST be identical to Phase 4 output within floating-point tolerance (less than -120 dBFS difference). Coupling at zero adds no artifacts.
- **SC-002**: When the kick (MIDI 36) is triggered with Snare Buzz at 50% and Global Coupling at 100%, the output MUST contain measurable energy in the 1-8 kHz band at the snare's modal frequencies that was not present in Phase 4 output. The coupling contribution MUST be at least -40 dBFS below the kick's peak level (audible but not dominant).
- **SC-003**: The coupling engine MUST add less than 1.5% single-core CPU at 44.1 kHz with 8 voices active and up to 64 active resonators. This is within the overall Membrum CPU budget.
- **SC-004**: When Global Coupling is 0.0, the coupling engine MUST add less than 0.01% CPU overhead (early-out bypass performance).
- **SC-005**: State version 5 MUST round-trip all coupling parameters (4 global knobs + 32 per-pad amounts + coupling matrix coefficients) with zero loss through save/load cycles.
- **SC-006**: Loading a v4 state blob into a Phase 5 processor MUST produce output identical to Phase 4 behavior (coupling defaults to disabled).
- **SC-007**: The energy limiter MUST prevent coupling output from exceeding -20 dBFS even when all 32 pads are triggered simultaneously at maximum velocity with maximum coupling settings.
- **SC-008**: Two toms tuned an octave apart MUST produce at least 12 dB more coupling energy than two toms tuned a tritone apart, verifying frequency selectivity of the resonator-based coupling.
- **SC-009**: Zero audio-thread memory allocations across a 10-second fuzz test with coupling enabled and random MIDI input across all 32 pads.
- **SC-010**: Pluginval level 5 MUST pass with zero errors.

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- Phase 4 (v0.4.0) is complete and stable. All 32-pad architecture, preset system, and output routing are functioning correctly.
- The existing `SympatheticResonance` / `SympatheticResonanceSIMD` engine in KrateDSP is suitable for cross-pad coupling without modification to its core processing. Its resonator pool (64 slots), SIMD processing, merge/evict logic, and anti-mud HPF are directly applicable. The engine was originally designed for cross-voice sympathetic resonance in Ruinae (spec 132) and is architecturally compatible with Membrum's cross-pad use case.
- The reserved parameter offsets 36-63 in `PadConfig` are available for Phase 5 use, as explicitly documented in the Phase 4 spec ("Offsets 36-63 are reserved for Phase 5+").
- The coupling matrix default values result in coupling being disabled, ensuring Phase 4 behavioral compatibility when loading older state.
- Coupling is applied to the main output bus only. Per-pad auxiliary routing is unaffected by coupling.
- Pad category classification (Kick/Snare/Tom/Hat-Cymbal/Perc) can be derived from the pad's body model type and exciter configuration without requiring explicit user assignment. The `DefaultKit::apply()` archetypes provide the initial category hints.
- The propagation delay (0.5-2 ms) is implemented using the existing `DelayLine` primitive from KrateDSP, requiring only a short buffer (max ~384 samples at 192 kHz).
- The `ModalResonatorBank` or body model can expose the first N partial frequencies for coupling engine registration. If not currently exposed, a lightweight accessor method must be added.

### Existing Codebase Components (Principle XIV)

**Relevant existing components that may be reused or extended:**

| Component | Location | Relevance |
|-----------|----------|-----------|
| `SympatheticResonance` | `dsp/include/krate/dsp/systems/sympathetic_resonance.h` | **Core coupling engine.** SIMD-accelerated second-order driven resonators with Chaigne-Lambourg damping, envelope followers, merge/evict pool management (64 slots), anti-mud HPF. Direct use for cross-pad coupling. |
| `SympatheticResonanceSIMD` | `dsp/include/krate/dsp/systems/sympathetic_resonance_simd.h/.cpp` | **SIMD kernel.** Highway-accelerated batch processing of resonator bank. Called by `SympatheticResonance::process()`. |
| `DelayLine` | `dsp/include/krate/dsp/primitives/delay_line.h` | **Propagation delay.** 0.5-2 ms coupling delay modeling air cavity propagation. Multiple interpolation modes available. |
| `OnePoleSmoother` | `dsp/include/krate/dsp/primitives/smoother.h` | **Parameter smoothing.** Click-free updates for coupling parameters. Already used inside `SympatheticResonance` for amount smoothing. |
| `VoicePool` | `plugins/membrum/src/voice_pool/voice_pool.h` | **Integration point.** Voice allocation/deallocation hooks for coupling engine registration. Needs coupling engine calls added to noteOn/noteOff/processBlock. |
| `PadConfig` | `plugins/membrum/src/dsp/pad_config.h` | **Per-pad parameter storage.** Reserved offsets 36-63 available for per-pad coupling amount at offset 36. |
| `DefaultKit` | `plugins/membrum/src/dsp/default_kit.h` | **Pad category source.** Archetype assignments (Kick/Snare/Tom/Hat/Cymbal/Perc) used to derive pad categories for Tier 1 mapping. |
| `ModalResonatorBank` | `dsp/include/krate/dsp/processors/modal_resonator_bank.h` | **Partial frequency source.** Provides modal frequencies for seeding the coupling engine's resonator pool. May need a lightweight accessor for the first N partial frequencies. |
| `DrumVoice` | `plugins/membrum/src/dsp/drum_voice.h` | **Voice signal path.** Integration point for extracting partial frequencies after body model configuration during noteOn. |

**Initial codebase search for key terms:**

```bash
grep -r "SympatheticResonance" dsp/ plugins/
grep -r "kPadCouplingAmount" plugins/
grep -r "coupling" plugins/membrum/src/
```

**Search Results Summary**: `SympatheticResonance` exists in `dsp/include/krate/dsp/systems/` with full SIMD implementation. `kPadCouplingAmount` does not yet exist (to be added in Phase 5). No coupling-related code exists in `plugins/membrum/` yet.

### Forward Reusability Consideration

**Sibling features at same layer:**
- Phase 6 macro controls will drive Global Coupling as part of the "Complexity" macro
- Phase 6 Acoustic/Extended modes will control Tier 1 vs Tier 2 visibility
- Future snare wire modeling (FR-047 deferred from Phase 2) will interact with the Snare Buzz coupling path -- the Tier 1 "Snare Buzz" knob provides the UX surface for this

**Potential shared components:**
- The pad category classification logic could be reused for other category-aware features (e.g., per-category macro behavior in Phase 6)
- The coupling matrix data model (sparse 32x32 with per-pair coefficients) could be generalized for other cross-pad interactions in future versions

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
| FR-010 | | |
| FR-011 | | |
| FR-012 | | |
| FR-013 | | |
| FR-014 | | |
| FR-020 | | |
| FR-021 | | |
| FR-022 | | |
| FR-023 | | |
| FR-030 | | |
| FR-031 | | |
| FR-032 | | |
| FR-033 | | |
| FR-034 | | |
| FR-040 | | |
| FR-041 | | |
| FR-042 | | |
| FR-043 | | |
| FR-050 | | |
| FR-051 | | |
| FR-052 | | |
| FR-053 | | |
| FR-060 | | |
| FR-061 | | |
| FR-062 | | |
| FR-070 | | |
| FR-071 | | |
| FR-072 | | |
| FR-073 | | |
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

### Completion Checklist

- [ ] Each FR-xxx row was verified by re-reading the actual implementation code (not from memory)
- [ ] Each SC-xxx row was verified by running tests or reading actual test output (not assumed)
- [ ] Evidence column contains specific file paths, line numbers, test names, and measured values
- [ ] No evidence column contains only generic claims like "implemented", "works", or "test passes"
- [ ] No test thresholds relaxed from spec requirements
- [ ] No placeholder values or TODO comments in new code
- [ ] No features quietly removed from scope
- [ ] User would NOT feel cheated by this completion claim

### Honest Assessment

**Overall Status**: PENDING

**Recommendation**: Proceed with `/speckit.plan` to break Phase 5 into implementation phases.
