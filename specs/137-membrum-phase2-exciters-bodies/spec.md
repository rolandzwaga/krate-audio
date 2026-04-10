# Feature Specification: Membrum Phase 2 — 5 Exciter Types + 5 Body Models (Swap-In Architecture)

**Feature Branch**: `137-membrum-phase2-exciters-bodies`
**Plugin**: Membrum (`plugins/membrum/`)
**Created**: 2026-04-10
**Status**: Draft
**Input**: Phase 2 scope from Spec 135 (Membrum Synthesized Drum Machine); builds on Phase 1 (`specs/136-membrum-phase1-scaffold/`)

## Background

Phase 1 (spec 136) delivered a minimal Membrum scaffold: a single `DrumVoice` hardcoded to MIDI note 36, wiring `ImpactExciter` into `ModalResonatorBank` with 16 Bessel membrane modes, amp ADSR, 5 host-generic parameters, and no UI. Phase 2 expands the sonic palette of that single voice from *one exciter class + one body model* to **6 exciter types + 6 body models**, all runtime-selectable on the same single voice, still on MIDI note 36, still without a custom UI. Phase 2 also introduces the first Unnatural Zone controls and the Tone Shaper — both single-voice properties that do not depend on multi-voice allocation (Phase 3) or sustained bowed excitation (Phase 4).

Phase 2 is deliberately scoped **within a single voice**. Voice allocation, pad routing, cross-pad coupling, choke groups, presets, pad templates, macro controls, Acoustic/Extended mode gating, and custom UI are all deferred to later phases. See "Deferred to Later Phases" section at the end.

## Clarifications

### Session 2026-04-10

- Q: Which dispatch mechanism does Phase 2 mandate for the exciter and body hot path — virtual `IExciter`/`IBody` interface, `std::variant` + `std::visit` / index-based `switch`, or function-pointer table? → A: `std::variant<...>` + `std::visit` or index-based `switch` per block. Virtual interfaces are explicitly excluded from the audio hot path.
- Q: FR-044 and FR-080 describe Pitch Envelope Start/End as 'semitones offset or Hz' — pick one unit: A) Absolute Hz, B) Semitones offset, C) Normalized 0-1 mapped per-body. → A: Absolute Hz. Pitch Envelope Start and End are expressed in absolute Hz (range 20–2000 Hz), matching SC-009's 160 Hz → 50 Hz reference example directly without any per-body mapping or semitone conversion.
- Q: BodyBank sharing policy for modal bodies — A) one shared `ModalResonatorBank` + deferred-to-note-on switching, B) per-body banks + cross-fade, or C) primary + secondary cross-fade bank? → A: One shared `ModalResonatorBank` instance owned by `DrumVoice`. Body-model switches while sounding are always deferred to the next note-on; the bank is reconfigured and cleared at that boundary. Mid-note cross-fading is explicitly out of scope for Phase 2.

---

## User Scenarios & Testing *(mandatory)*

### User Story 1 - Select Any of 6 Exciter Types on the Single Voice (Priority: P1)

A sound designer opens Membrum in their DAW. A new **Exciter Type** parameter exposes 6 choices: Impulse, Mallet, Noise Burst, Friction, FM Impulse, Feedback. Switching between them — while sending repeated MIDI note 36 triggers — produces audibly distinct excitation characters driving the same body model: sharp click (Impulse), softer rounded hit (Mallet), hissy attack (Noise Burst), scraped/squealy (Friction), bell-like metallic transient (FM Impulse), and a self-resonating howl (Feedback).

**Why this priority**: The exciter is half of Membrum's identity (the other half is the body). Without multiple exciters, Membrum is just a membrane drum with knobs. This story is the P1 deliverable of Phase 2.

**Independent Test**: For each of the 6 exciter types, trigger MIDI note 36 and record the first 100 ms. Verify measurable spectral centroid and temporal-envelope differences between all 6 types, and verify that each type's output is non-silent, contains no NaN/Inf, and decays without blowing up.

**Acceptance Scenarios**:

1. **Given** Exciter Type = Impulse, **When** MIDI note 36 is triggered at velocity 100, **Then** the output shows a short sharp click transient (< 5 ms) followed by body resonance.
2. **Given** Exciter Type = Mallet, **When** MIDI note 36 is triggered at velocity 100, **Then** the initial transient is measurably rounder than Impulse (lower spectral centroid in the first 2 ms, wider time-domain pulse).
3. **Given** Exciter Type = Noise Burst, **When** MIDI note 36 is triggered at velocity 100, **Then** the first 20 ms is dominated by broadband noise with spectral centroid > 2x the Impulse case.
4. **Given** Exciter Type = Friction, **When** MIDI note 36 is triggered at velocity 100, **Then** the output contains a stick-slip signature (non-monotonic energy envelope, with characteristic friction noise).
5. **Given** Exciter Type = FM Impulse, **When** MIDI note 36 is triggered at velocity 100, **Then** the first 50 ms contains clearly inharmonic high-frequency sidebands distinct from any body resonance.
6. **Given** Exciter Type = Feedback, **When** MIDI note 36 is triggered at velocity 100 with a resonant body model selected, **Then** the voice self-sustains longer than the direct body decay alone and never exceeds 0 dBFS peak (stability guard engaged).
7. **Given** any exciter type selected, **When** the DAW saves and reloads plugin state, **Then** the Exciter Type selection round-trips exactly.

---

### User Story 2 - Select Any of 6 Body Models on the Single Voice (Priority: P1)

A sound designer cycles through a new **Body Model** parameter: Membrane, Plate, Shell (free-free beam), String, Bell, Noise Body. Triggering MIDI note 36 on each body model produces audibly distinct resonances driven by physically-grounded modal frequency ratios: boomy drum (Membrane), tonal metallic (Plate), xylophone-like bar (Shell), pitched string (String), inharmonic bell (Bell), and broadband wash with sparse modes (Noise Body).

**Why this priority**: The body is the other half of Membrum's identity. Without multiple body models, Membrum cannot produce anything other than membrane drums. Equal P1 priority to US1.

**Independent Test**: For each of the 6 body models, trigger MIDI note 36 with a fixed exciter (Impulse) at fixed velocity and measure the first few spectral peaks. Verify each body model's partial ratios match the published physics to within a documented tolerance.

**Acceptance Scenarios**:

1. **Given** Body Model = Membrane (ideal), **When** a note triggers at Size=0.5, **Then** the first 8 measured spectral peaks match Bessel ratios {1.000, 1.593, 2.136, 2.296, 2.653, 2.918, 3.156, 3.501} within ±2% tolerance relative to the fundamental.
2. **Given** Body Model = Plate (square Kirchhoff), **When** a note triggers, **Then** the first 8 measured spectral peaks match square-plate ratios {1.000, 2.500, 4.000, 5.000, 6.500, 8.500, 9.000, 10.000} within ±3% tolerance.
3. **Given** Body Model = Shell (free-free beam, untuned), **When** a note triggers, **Then** the first 6 measured peaks match Euler-Bernoulli free-free ratios {1.000, 2.757, 5.404, 8.933, 13.344, 18.637} within ±3% tolerance.
4. **Given** Body Model = String, **When** a note triggers, **Then** the output has clearly harmonic partials (integer-multiple spacing within ±1%) consistent with a plucked/struck string spectrum (existing `WaveguideString` or `KarplusStrong` behaviour).
5. **Given** Body Model = Bell, **When** a note triggers, **Then** the first 5 measured peaks match Chladni-law church-bell ratios {0.250, 0.500, 0.600, 0.750, 1.000} (hum, prime, tierce, quint, nominal) within ±3% tolerance relative to the nominal.
6. **Given** Body Model = Noise Body, **When** a note triggers, **Then** the output is a hybrid of a sparse modal component and a filtered-noise component with a spectral density characteristic of cymbals/hats (broadband high-frequency energy with at least one clear modal peak).
7. **Given** any body model selected, **When** the DAW saves and reloads plugin state, **Then** the Body Model selection round-trips exactly.

---

### User Story 3 - Exciter and Body Are Independently Swappable (Priority: P1)

A sound designer can combine any exciter type with any body model. All 6 × 6 = 36 combinations produce audio — none crash, none produce NaN/Inf, none exceed 0 dBFS peak. Some combinations make musical sense (Mallet + Membrane = tom), others are experimental (Feedback + Bell = evolving drone), but all must work as a swap-in architecture.

**Why this priority**: The "swap-in architecture" requirement from the Phase 1 plan ("Phase 2 can add polymorphism when needed") is the architectural deliverable of Phase 2. Without independent swapping, the plugin collapses back into a fixed set of drum types.

**Independent Test**: A parameterized test iterates through all 36 exciter/body pairs, triggers MIDI note 36 at velocity 100, processes 500 ms of audio, and asserts: (a) output is non-silent, (b) no NaN/Inf/denormal values, (c) peak ≤ 0 dBFS, (d) no allocations in the audio path.

**Acceptance Scenarios**:

1. **Given** any of 36 exciter/body combinations, **When** MIDI note 36 is triggered at velocity 100, **Then** audio is produced with peak in (-30, 0) dBFS.
2. **Given** any of 36 combinations, **When** 500 ms of audio is processed, **Then** no sample contains NaN, Inf, or denormal values.
3. **Given** the Exciter Type parameter is changed while a voice is ringing out, **When** a new note is triggered, **Then** the new exciter applies to the new note without affecting the tail of the previous note, and the swap happens without clicks or allocations.
4. **Given** the Body Model parameter is changed while a voice is silent, **When** a new note is triggered, **Then** the new body model immediately applies with the new mode set.
5. **Given** the Body Model parameter is changed while a voice is ringing out, **When** the change is received, **Then** the body-model change is deferred to the next note-on (mid-note cross-fading is out of scope for Phase 2). The plugin MUST NOT crash, allocate, or produce NaN/Inf when the deferral occurs, and the ringing tail of the previous body model continues uninterrupted until natural decay or note-off.

---

### User Story 4 - Per-Body Parameter Mappings Are Musically Meaningful (Priority: P2)

For each body model, the existing five Phase 1 parameters (Material, Size, Decay, Strike Position, Level) continue to work and produce a musically sensible, per-body-specific sweep. For example: on Membrane, Size scales the fundamental from 500 Hz to 50 Hz exactly as in Phase 1; on Plate, Size scales the plate dimension (fundamental range adjusted for plate mode spacing); on Bell, Size maps to casting size / strike tone; on String, Size maps to string length. Each body model defines its own per-parameter mapping that is documented in the per-body parameter-mapping helper.

**Why this priority**: Without per-body parameter mapping, every body sweeps the same abstract "fundamental frequency" knob and users can't dial in a specific drum type. P2 because US1/US2/US3 are structurally required first; meaningful mapping is the quality layer on top.

**Independent Test**: For each body model, sweep each of the 5 parameters from 0.0 to 1.0 at fixed other parameters and measure the output characteristic that each parameter targets (e.g., fundamental frequency for Size, decay time for Decay, spectral centroid for Strike Position where applicable). Verify the sweep spans the documented range per body.

**Acceptance Scenarios**:

1. **Given** any body model, **When** Size is swept 0.0 → 1.0, **Then** the measured fundamental frequency changes monotonically by at least 1 octave (and spans the body-specific documented range).
2. **Given** any body model, **When** Decay is swept 0.0 → 1.0, **Then** the measured RT60 changes monotonically by at least a factor of 3.
3. **Given** Membrane, Plate, or Shell body, **When** Strike Position is swept 0.0 → 1.0, **Then** the first-5-peaks spectral weighting changes measurably, consistent with the body-specific strike-position math (Bessel J_m for circular, sin(m·pi·x/L) for rectangular/bar).
4. **Given** any body model, **When** Material is swept 0.0 → 1.0, **Then** the measured decay tilt changes monotonically (high modes decay faster at low Material, more even decay at high Material).
5. **Given** any body model, **When** Level is set to 0.0, **Then** the output is silent.

---

### User Story 5 - Tone Shaper Post-Body Chain (Priority: P2)

A sound designer activates the per-voice Tone Shaper: a post-body chain with (a) SVF filter with dedicated envelope, (b) Drive (waveshaper) stage, (c) Wavefolder stage, and (d) Pitch Envelope (multi-stage, exponential) applied at the body level for kick-style pitch sweeps. Each stage is defeatable (wet/dry or enable toggle), and all stages combined must produce the classic "808 kick" pitch-sweep character when dialed in.

**Why this priority**: The pitch envelope alone is "identity-defining for kicks" (spec 135 explicit quote). The tone shaper bundles the essential single-voice post-processing, distinguishing Phase 2 output from raw modal synthesis.

**Independent Test**: Trigger Impulse + Membrane with Pitch Envelope set for a kick (160 Hz → 50 Hz over 20 ms). Measure the pitch trajectory of the fundamental over the first 50 ms via short-time FFT. Verify the measured pitch glide matches the envelope target within ±10%.

**Acceptance Scenarios**:

1. **Given** Pitch Envelope is configured for a kick (start 160 Hz, end 50 Hz, 20 ms), **When** a note triggers, **Then** the measured fundamental pitch glides from ~160 Hz to ~50 Hz with the exponential envelope shape.
2. **Given** SVF Filter = Lowpass with cutoff envelope sweep, **When** a note triggers, **Then** the spectral centroid follows the filter envelope trajectory.
3. **Given** Drive is increased from 0 to max, **When** a note triggers, **Then** the harmonic content (THD) increases monotonically, with output peak staying below 0 dBFS (no hard clipping unless explicitly configured).
4. **Given** Wavefolder is increased, **When** a note triggers, **Then** the output contains new odd-order harmonics characteristic of wavefolding.
5. **Given** all Tone Shaper stages are bypassed / at zero, **When** a note triggers, **Then** the output is bit-identical (or within -120 dBFS noise floor) to the raw body output.

---

### User Story 6 - Unnatural Zone: Push Beyond Physics (Priority: P3)

An experimental sound designer enables Unnatural Zone parameters on the single voice: Mode Stretch (compress/spread partial spacing), Decay Skew (invert natural high-mode damping so shimmer sustains while fundamental dies), Mode Inject (add synthetic partial series with phase randomization), Nonlinear Coupling (amplitude-dependent mode energy exchange), and Material Morph (per-hit automation of Material). With these engaged at their default-on values, the single voice produces evolving, non-physical timbres that still sound related to the underlying body.

**Why this priority**: P3 because US1-US3 deliver the core palette; US4-US5 deliver musical sculpting; US6 is the "identity and differentiation" layer. Spec 135 says "these are core to Membrum's identity, not extras," but within Phase 2 they must be built on top of the other work and are not blockers for Phase 2 validation.

**Independent Test**: For each Unnatural Zone parameter, set it to a non-zero value and compare the output spectrogram to the same patch with the parameter at zero. Verify a measurable deterministic difference. Verify no NaN/Inf and no clipping (stability guards engaged).

**Acceptance Scenarios**:

1. **Given** Mode Stretch = 1.5, **When** a note triggers on Membrane, **Then** the measured partial ratios are multiplied by a factor > 1 (spread apart) relative to the Mode Stretch = 1.0 reference.
2. **Given** Decay Skew = -1.0, **When** a note triggers, **Then** the fundamental mode decays faster than the highest mode (inverted from the natural Chaigne-Lambourg decay profile).
3. **Given** Mode Inject = 0.5 with harmonic preset, **When** a note triggers, **Then** additional spectral peaks at integer-ratio frequencies are present alongside the body's natural peaks, AND phase randomization is applied (verify by triggering the same note twice and observing phase differences in the injected partials only).
4. **Given** Nonlinear Coupling = 0.5, **When** a note triggers at high velocity on a plate/cymbal-like body, **Then** the mode energy distribution evolves over time (time-varying spectral centroid), AND the stability guard (internal energy limiter) prevents peak from exceeding 0 dBFS.
5. **Given** Material Morph is configured to morph from metal (1.0) at t=0 to wood (0.0) at t=500ms, **When** a note triggers, **Then** the measured decay-tilt envelope changes over the hit duration.
6. **Given** all Unnatural Zone parameters at their default off-values (Mode Stretch=1.0, Decay Skew=0.0, Mode Inject=0.0, Nonlinear Coupling=0.0, Material Morph disabled), **When** a note triggers, **Then** the output is identical to the equivalent Phase-1-style patch (within -120 dBFS noise floor for deterministic inputs).

---

### User Story 7 - CPU Budget Stays Within Spec Across All Combinations (Priority: P2)

For every combination of the 6 exciter types × 6 body models × (tone shaper on/off) × (unnatural zone on/off), the single voice stays within the per-voice CPU budget defined by spec 135's performance targets. The worst-case combination (identified empirically — likely Noise Body + FM Impulse + full Unnatural Zone, or String body with feedback exciter and full tone shaper) must still leave headroom for Phase 3's 8-voice target.

**Why this priority**: Spec 135 explicitly flags "Cymbal mode count vs CPU" as an open risk. Phase 2 must not ship a single-voice combination that cannot scale. P2 because it is a validation gate, not a user-facing feature.

**Independent Test**: A benchmark iterates all (exciter × body × toneshaper × unnatural) combinations, measures single-voice CPU at 44.1 kHz for 10 seconds of ringing audio, and asserts each combination is ≤ **1.25%** single-core CPU (leaving 8× headroom below a 10% total-plugin target for Phase 3's 8-voice pool). SIMD-accelerated modal bank (`ModalResonatorBankSIMD`) is **not** required in Phase 2 — the scalar path must meet the budget on its own.

**Acceptance Scenarios**:

1. **Given** any of 6 × 6 × 2 × 2 = 144 test combinations at default parameter values, **When** 10 seconds of audio at 44.1 kHz is processed, **Then** measured single-voice CPU is ≤ 1.25% on a modern desktop x86-64 CPU (the Phase 1 SC-003 baseline × 2.5 headroom multiplier).
2. **Given** the cymbal-hybrid Noise Body with its maximum mode count (documented — see FR-062 below), **When** processed, **Then** it meets the 1.25% budget on the scalar path.
3. **Given** all 36 exciter/body pairs at maximum Unnatural Zone settings, **When** processed, **Then** no combination exceeds 2.0% single-voice CPU (hard ceiling; any combination exceeding 1.25% is documented as a known risk for Phase 3 polyphony).

---

### Edge Cases

- **All exciters silent-to-silent**: Triggering with velocity 1 (minimum non-zero) on every exciter + body combination must produce audible output (above -60 dBFS peak) or silence, never NaN/Inf.
- **Rapid exciter-type switching during voice life**: Changing Exciter Type every 5 ms while a voice is ringing must not allocate, must not crash, and must not emit clicks above -30 dBFS.
- **Rapid body-model switching during voice life**: Rapid Body Model changes while a voice is sounding are deferred to the next note-on (cross-fading is out of scope for Phase 2). The plugin must not crash, allocate, or produce NaN/Inf regardless of how rapidly the parameter is changed.
- **Feedback exciter with high-Q body**: The Feedback exciter with a long-sustaining body (String, Bell) must engage the energy limiter to prevent runaway. Peak must never exceed 0 dBFS.
- **Nonlinear Coupling at max velocity + max coupling strength on cymbal body**: Must engage the stability guard (internal energy limiter). Peak ≤ 0 dBFS, no NaN/Inf.
- **Mode Inject with many injected partials**: Adding up to the body's remaining mode budget (limited by `kMaxModes=96` in `ModalResonatorBank`) must not overflow.
- **Material Morph envelope shorter than process block**: Envelope completes within a single process block — must not hang or produce static timbre.
- **Zero-length Tone Shaper Pitch Envelope**: Zero-duration glide must degrade gracefully to a static frequency offset, not divide by zero.
- **Switching from a Mode-Injected body back to a pure body**: Previously injected modes must not persist in the new mode set.
- **Extreme sample rates (22050, 96000, 192000 Hz)**: All body models must compute correct modal frequencies relative to the sample rate. No aliasing beyond the established per-body tolerance.
- **Body Model = String with Feedback exciter**: Must not produce Larsen oscillation that escapes the energy limiter.

## Requirements *(mandatory)*

### Functional Requirements

#### Swap-In Architecture (Core of Phase 2)

- **FR-001**: System MUST refactor `Membrum::DrumVoice` so that the exciter and body are independent, runtime-selectable components. The refactor MUST eliminate the Phase 1 hardcoded `ImpactExciter` + `ModalResonatorBank` composition in favour of **`std::variant<...>` + `std::visit` (or equivalent index-based `switch`)** dispatch. Virtual-call interfaces (`IExciter`/`IBody`) are explicitly **not acceptable** for the audio hot path because they introduce runtime virtual dispatch that violates the zero-virtual-call-on-hot-path requirement. The implementation MUST use `std::variant` holding pre-allocated instances of all exciter (or body) backend types, dispatched per-block via `std::visit` or an index-based `switch`. Rationale MUST be recorded in the Phase 2 `plan.md`.
- **FR-002**: The `std::variant` dispatch mechanism MUST allow the audio thread to call `exciter_.process()` and `body_.processSample(excitation)` without allocations, without exceptions, and without runtime type-ID branches inside the per-sample inner loop (branch-per-block via `std::visit` or `switch` is acceptable; per-sample branching is not).
- **FR-003**: Exciter Type and Body Model MUST each be exposed as a host-visible integer-valued parameter (`StringListParameter` per VST3 guidance) with 6 discrete options each.
- **FR-004**: Changing Exciter Type or Body Model while the voice is **silent** MUST take effect immediately for the next note-on.
- **FR-005**: Changing Body Model while the voice is **sounding** MUST be deferred until the next note-on. Mid-note cross-fading of body models is explicitly out of scope for Phase 2. Changing Exciter Type while the voice is sounding follows the same deferral policy: the new exciter applies only at the next note-on. In both cases the implementation MUST NOT crash, allocate, or produce NaN/Inf during or after the deferral.
- **FR-006**: All exciter and body instances MUST be pre-allocated at plugin instantiation and/or in `setupProcessing()`/`setActive()`. No instance construction on the audio thread.
- **FR-007**: The `DrumVoice` interface (public methods `prepare`, `noteOn(velocity)`, `noteOff()`, `process()`, `isActive()`, parameter setters) MUST remain behaviourally compatible with Phase 1 — existing Phase 1 tests that exercise the Membrane/Impulse path MUST continue to pass without modification to the test source.

#### Exciter Types — 6 total (Phase 1 + 5 new)

- **FR-010**: System MUST expose an **Impulse** exciter type backed by the existing `ImpactExciter` in its Phase 1 default configuration (hardness ~0.5, no noise burst). This is the carry-over from Phase 1 and MUST continue to produce the Phase 1 membrane drum sound on the default patch.
- **FR-011**: System MUST expose a **Mallet** exciter type backed by `ImpactExciter` configured for soft-to-hard striker behaviour per spec 135's power-law contact force model. Parameters MUST map velocity to: contact duration 8 ms (soft, velocity ~0.0) → 1 ms (hard, velocity ~1.0), mallet hardness exponent alpha in the documented 1.5–4.0 range, and SVF brightness rising with velocity. This type SHOULD use the same `ImpactExciter` class with different parameter envelopes — no new exciter class required if `ImpactExciter` can deliver both characters; otherwise a small wrapper is acceptable.
- **FR-012**: System MUST expose a **Noise Burst** exciter type backed by a combination of the existing `NoiseOscillator` (Layer 1 per-sample primitive — `NoiseGenerator` is block-oriented and not used here; see research.md §2) shaped by a short decaying amplitude envelope and an SVF bandpass/lowpass filter. Velocity MUST drive the bandpass/lowpass cutoff (200 Hz soft → 5000+ Hz hard per spec 135), the noise color, and the burst duration (~2–15 ms). The exciter MUST be real-time safe and MUST not allocate on trigger.
- **FR-013**: System MUST expose a **Friction** exciter type backed by the existing `BowExciter`. Phase 2 uses Friction in **transient (struck) mode only** — the bow is released within a documented short envelope (≤ 50 ms). Sustained / continuous bowing (infinite pressure with steady-state self-oscillation) is explicitly deferred to Phase 4. Velocity MUST drive bow pressure and bow velocity per `BowExciter`'s existing interface.
- **FR-014**: System MUST expose an **FM Impulse** exciter type backed by the existing `FMOperator`. A short amplitude envelope (≤ 100 ms) gates the FM burst. The carrier:modulator ratio MUST default to 1:1.4 (Chowning bell-like; see Scientific Verification Notes) and MUST be exposable as a secondary parameter for experimentation. Modulation index MUST decay faster than carrier amplitude, per spec 135 ("modulation index must decay faster than the carrier amplitude").
- **FR-015**: System MUST expose a **Feedback** exciter type that routes the body's output back into the exciter input through the existing `FeedbackNetwork`. The feedback path MUST include (a) a filter stage (SVF), (b) a saturation stage (tanhADAA or waveshaper), and (c) an internal energy limiter that clamps total feedback path energy to prevent Larsen runaway. Peak output MUST remain ≤ 0 dBFS regardless of feedback amount or body Q.
- **FR-016**: Each of the 6 exciter types MUST implement velocity-driven spectral behaviour per spec 135: velocity drives amplitude AND spectral content (brightness/bandwidth). A test MUST demonstrate that for each exciter type, velocity 127 produces measurably higher spectral centroid than velocity 30 (the Phase 1 SC-005 standard, extended to every exciter type).
- **FR-017**: All 6 exciter types MUST be selectable at runtime via the Exciter Type parameter (FR-003). State save/load MUST round-trip the selection exactly.

#### Body Models — 6 total (Phase 1 + 5 new)

- **FR-020**: System MUST retain the **Membrane** body model from Phase 1 (circular drum head, 16 Bessel modes, `kMembraneRatios` in `membrane_modes.h`). All Phase 1 Membrane tests MUST continue to pass.
- **FR-021**: System MUST add a **Plate** body model using Kirchhoff-theory rectangular-plate mode ratios. The default configuration is the square-plate case with ratios `{1.000, 2.500, 4.000, 5.000, 6.500, 8.500, 9.000, 10.000, ...}` (from the (m²+n²) formula for a/b=1 — see spec 135 "Rectangular Plate Modes"). The Plate body MUST implement 16 modes by default. Strike-position math MUST use `sin(m·pi·x_0/a)·sin(n·pi·y_0/b)` rather than Bessel functions. The Plate body table MUST be stored in a new `plate_modes.h` file alongside `membrane_modes.h`.
- **FR-022**: System MUST add a **Shell** body model interpreted as a **free-free beam** (untuned Euler-Bernoulli) with ratios `{1.000, 2.757, 5.404, 8.933, 13.344, 18.637, 24.812, 31.870, ...}`. The Shell body MUST implement up to 12 modes by default (fewer than other bodies because free-free modes are extremely sparse and inharmonic). The ratios MUST be stored in a new `shell_modes.h` file.
- **FR-023**: System MUST add a **String** body model backed by the existing `WaveguideString` (preferred — implements `IResonator`) or `KarplusStrong` as a fallback. The String body MUST produce harmonic partials (integer ratios within ±1%) at the chosen fundamental, with string-length/pluck-position control mapped from Size/Strike Position. The String body does NOT use `ModalResonatorBank`; it uses its own delay-line backend, which is the first demonstration of the FR-001 swap-in architecture beyond modal bodies.
- **FR-024**: System MUST add a **Bell** body model using Chladni-law church-bell partial ratios `{0.250, 0.500, 0.600, 0.750, 1.000, 1.500, 2.000, 2.600, ...}` (hum, prime, tierce, quint, nominal, superquint, octave, etc. — per spec 135 "Bell Partials"). The Bell body MUST implement 16 modes. The Bell body table MUST be stored in a new `bell_modes.h` file. Damping MUST default to low b1 / very-low b3 (metallic, long ring; Steel/Brass preset range from Chaigne-Lambourg table).
- **FR-025**: System MUST add a **Noise Body** (cymbal/hi-hat hybrid) consisting of (a) a sparse modal component using plate-mode ratios with **up to 40 modes** (see FR-062 for the CPU-budget justification) and (b) a time-varying bandpass-filtered noise component. The modal component uses the existing `ModalResonatorBank`; the noise component uses `NoiseGenerator` + `SVF`. The two components are mixed at a body-specific ratio. This is the first body model that composes TWO existing processors rather than wrapping one.
- **FR-026**: All modal body models (Membrane, Plate, Shell, Bell, and the modal portion of Noise Body) MUST share a single `ModalResonatorBank` instance owned by the `DrumVoice`. On note-on, if the active body model has changed since the previous note-on, the bank MUST be reconfigured (mode frequencies, Qs, and amplitudes rewritten) and its resonator state cleared to zero before processing begins. The String body uses `WaveguideString`/`KarplusStrong` (not `ModalResonatorBank`); the Noise Body's noise portion uses `NoiseGenerator`+`SVF`. No new resonator DSP classes are introduced in Phase 2 — body model diversity comes from different configuration of existing processors.
- **FR-027**: Each body model MUST be selectable at runtime via the Body Model parameter (FR-003). State save/load MUST round-trip the selection exactly.
- **FR-028**: Each body model MUST document its Size-to-fundamental mapping, its Strike-Position mathematics, and its damping defaults in a per-body comment block in the corresponding `*_modes.h` (or equivalent) file. These mappings are summarized in the per-body parameter-mapping helper (FR-030).

#### Per-Body Parameter Mapping (extracted helpers)

- **FR-030**: System MUST extract the Phase 1 parameter-mapping logic (currently inline in `DrumVoice::noteOn` and `DrumVoice::updateModalParameters`) into per-body parameter-mapping helpers. Each body model MUST have its own helper (function or small class) that takes the 5 common parameters (Material, Size, Decay, Strike Position, Level) plus any body-specific state and produces the arguments for the underlying resonator's setup method. The Phase 1 plan notes: *"Parameter mapping in DrumVoice, not separate helper. Only one consumer. Phase 2 may extract if patterns emerge."* — Phase 2 has 6 consumers, so the extraction is mandatory.
- **FR-031**: The Membrane mapping helper MUST produce bit-identical output to the Phase 1 inline code for the same (Material, Size, Decay, Strike Position) input. This is a regression guarantee on the Phase 1 sound.
- **FR-032**: The Plate, Shell, Bell, and Noise-Body mapping helpers MUST each define a per-body Size → fundamental mapping appropriate to the body. Suggested defaults (may be refined in `plan.md`):
  - **Plate**: `f0 = 800 · 0.1^size` → 800 Hz (small, e.g. cowbell) down to 80 Hz (large metal sheet)
  - **Shell (free-free beam)**: `f0 = 1500 · 0.1^size` → 1500 Hz (short bar, e.g. xylophone) down to 150 Hz (long bar, e.g. marimba)
  - **Bell**: `f0_nominal = 800 · 0.1^size` → 800 Hz (small handbell) down to 80 Hz (large church bell)
  - **Noise Body**: `f0 = 1500 · 0.1^size` → 1500 Hz (small hat) down to 150 Hz (large cymbal)
  - **Membrane**: unchanged from Phase 1 `f0 = 500 · 0.1^size`
  - **String**: `f0 = 800 · 0.1^size` → 800 Hz (short high string) down to 80 Hz (long low string), passed as frequency to `WaveguideString`/`KarplusStrong`.
- **FR-033**: Each mapping helper MUST define per-body Material → brightness/damping (b1/b3) mapping. Membrane keeps the Phase 1 formula. Plate, Bell, and Shell MUST default to the metallic end of the Chaigne-Lambourg material table (long sustain, low HF damping) since those bodies sound dead without resonance. Noise Body MUST default to moderate damping. The specific formulas are implementation details left to `plan.md`, but MUST be unit-tested.
- **FR-034**: Each mapping helper MUST define per-body Strike Position mathematics. Circular bodies (Membrane, cymbals-as-disks) use `A_mn ∝ |J_m(j_mn · r/a)|`. Rectangular bodies (Plate) use `A_mn ∝ sin(m·pi·x_0/a)·sin(n·pi·y_0/b)` with `x_0, y_0` mapped from a single Strike Position scalar. Beam bodies (Shell) use `A_k ∝ sin(k·pi·x_0/L)` or the equivalent free-free beam mode shape. Bell and String MUST define their own Strike Position mapping (bell: strike location on the rim/waist; string: pluck position along the string length).
- **FR-035**: The mapping helper system MUST NOT depend on virtual inheritance in the hot path. A `std::variant<MembraneMapper, PlateMapper, ...>` dispatched via `std::visit` or an index-based `switch` on the body type enum is the mandated approach (consistent with FR-001). The choice MUST be documented in `plan.md`.

#### Tone Shaper (post-body chain)

- **FR-040**: System MUST add a post-body Tone Shaper chain applied to the body's output on every voice, before the amp ADSR and level. The chain order is: body output → Drive (Waveshaper, ADAA) → Wavefolder → DC Blocker → SVF Filter (with its own ADSR envelope) → amp ADSR → Level. (Stage order justified in research.md §8 — west-coast Buchla signal flow; SVF placed last smooths aliasing residues from the Wavefolder. This is an intentional deviation from the earlier draft of this FR; plan.md, tone_shaper_contract.md, and tasks.md all honor this order.) The Pitch Envelope is NOT an audio-rate stage in this chain — it is a control-plane signal sampled per-sample and fed to the body's frequency parameters (see FR-044).
- **FR-041**: The **SVF Filter** stage MUST use the existing `Krate::DSP::SVF` with a dedicated filter ADSR envelope (separate from the amp envelope). Exposed parameters: filter type (LP/HP/BP), cutoff, resonance, env amount, env ADSR (A/D/S/R in ms). When env amount = 0, the filter is static (no envelope sweep).
- **FR-042**: The **Drive** stage MUST use the existing `Krate::DSP::Waveshaper` or `Krate::DSP::SaturationProcessor`. Exposed parameters: drive amount, wet/dry mix. Drive MUST use alias-safe processing (ADAA or oversampling) to keep the nonlinear stage clean at the documented aliasing tolerance.
- **FR-043**: The **Wavefolder** stage MUST use the existing `Krate::DSP::Wavefolder`. Exposed parameters: fold amount, wet/dry mix.
- **FR-044**: The **Pitch Envelope** MUST use the existing `Krate::DSP::MultiStageEnvelope` (N-stage, exponential per segment). It MUST be applied at the body-frequency level (i.e., it modulates the fundamental passed to the body's configure method) rather than as an audio-rate post-processor. Exposed parameters: **Pitch Envelope Start** (absolute Hz, range 20–2000 Hz, default 160 Hz), **Pitch Envelope End** (absolute Hz, range 20–2000 Hz, default 50 Hz), time (ms), curve (exponential/linear). Both start and end are expressed in absolute Hz — there is no semitone-offset or normalized representation. Spec 135: "This is identity-defining for kicks — promoted to a primary voice control in the UI, not buried in tone shaping" — the Pitch Envelope MUST be a top-level primary parameter on the Tone Shaper section and MUST be sufficient to produce a classic 808 kick sweep (160 Hz → 50 Hz over ≤ 20 ms) on the Impulse + Membrane combination, matching SC-009 directly with no unit conversion required.
- **FR-045**: Each Tone Shaper stage MUST have a bypass / zero-mix state in which it has no audible effect on the signal (within a documented noise-floor tolerance, ≤ -120 dBFS deviation from raw body output for deterministic inputs). This guarantees that Phase 2's default-off tone shaper does not retroactively change the Phase 1 sound.
- **FR-046**: The Tone Shaper as a whole MUST be real-time safe: no allocations, no exceptions, no I/O, all sub-processors pre-allocated.
- **FR-047**: Snare wire modeling (spec 135 "Snare Wire Modeling" section) is **deferred to a later phase** (see Deferred section). Phase 2 does NOT implement snare wires, even though they are a Tone-Shaper-adjacent feature, because (a) they only apply to Membrane bodies and (b) they require a new collision-model implementation not present in KrateDSP.

#### Unnatural Zone (single-voice)

- **FR-050**: System MUST expose **Mode Stretch** as a parameter directly wired to `ModalResonatorBank::stretch` (which already exists — spec 135: "Already exists as stretch in ModalResonatorBank"). Default value 1.0 (physical). Range ~0.5 to ~2.0. Mode Stretch applies to modal bodies only; for String, Mode Stretch MUST map to the existing string inharmonicity / dispersion parameter. For Bell, Mode Stretch behaves as a general stretch on all partials.
- **FR-051**: System MUST add a **Decay Skew** parameter that inverts or flattens the natural Chaigne-Lambourg frequency-dependent damping. Decay Skew = 0.0 is natural (high modes decay faster, positive b3). Decay Skew = +1.0 is maximum natural tilt. Decay Skew = -1.0 inverts the tilt so the fundamental dies while high modes sustain (negative b3, per spec 135: "normally high modes decay faster (positive b3). Negative b3 = fundamental dies while shimmer sustains"). This parameter requires a new code path in the Membrum voice that computes per-mode damping with a user-controlled sign on the b3 term. This does NOT require modifying `ModalResonatorBank` — the implementation can either (a) pass a modified `b3` to the existing `setModes/updateModes` API if that API exposes per-mode damping, or (b) pre-compute per-mode decay-time overrides that achieve the inverted tilt. The chosen approach MUST be documented in `plan.md`.
- **FR-052**: System MUST add a **Mode Inject** parameter that mixes a synthetic partial series into the body's mode set. The injection source MUST be the existing `HarmonicOscillatorBank`. Phase 2 supports at minimum one preset injection mode: **harmonic** (integer-ratio partials at 1, 2, 3, 4, 5, 6, 7, 8). Additional modes (FM-derived Chowning ratios, randomized) MAY be added. **Phase randomization** MUST be applied to injected partials per spec 135: "Phase randomization required to avoid cancellation/inconsistent transients between injected and physical modes." Mode Inject = 0.0 means no injection (the injection bank is either not summed or its output is gain-zeroed — there must be no audible leak).
- **FR-053**: System MUST add a **Nonlinear Coupling** parameter that implements velocity-scaled cubic mode coupling inspired by von Karman plate theory (see spec 135 "Nonlinear Coupling" and "Cymbal Synthesis: The Hybrid Approach" + Scientific Verification Notes below). Phase 2's Nonlinear Coupling implementation MAY be a simplified stand-in — for example, velocity-dependent amplitude-modulated mode cross-talk driven by an envelope follower — as long as it (a) produces an audibly time-varying spectral character, (b) is musically controllable from 0 to 1, and (c) engages a stability guard that clamps total internal energy to prevent blow-up at high settings. The **energy limiter is mandatory**: peak output MUST stay ≤ 0 dBFS at Nonlinear Coupling = 1.0 with any body model at any velocity.
- **FR-054**: System MUST add **Material Morph** as a per-hit automation envelope on the `Material` parameter (b1/b3 coefficients) that evolves during a single note. Phase 2 supports at minimum a 2-point envelope: `material(t=0)` and `material(t=duration)` with linear or exponential interpolation over a user-specified duration (range 10–2000 ms). The morph MUST trigger on each note-on and MUST complete naturally before the amp envelope release phase. Setting Material Morph duration to 0 or enabling a "Disabled" flag MUST make Material Morph equivalent to the static Material value (no morph).
- **FR-055**: All 5 Unnatural Zone parameters at their default off-values MUST leave the voice's output bit-identical (within -120 dBFS noise floor on deterministic inputs) to the equivalent Phase 2 patch with Unnatural Zone disabled.
- **FR-056**: Mode Inject and Nonlinear Coupling MUST each be real-time safe. No allocations on trigger, no runaway in the inner loop, and stability guards (phase randomization for Mode Inject, energy limiter for Nonlinear Coupling) are mandatory per spec 135.

#### Velocity Mapping (preserved and extended)

- **FR-060**: Phase 1's velocity-to-exciter mapping (FR-037 from spec 136) MUST continue to hold for the Impulse and Mallet exciter types. Velocity → amplitude, hardness, brightness, etc., as documented in the Phase 1 spec.
- **FR-061**: The other 4 exciter types (Noise Burst, Friction, FM Impulse, Feedback) MUST each define their own velocity → timbre mapping documented per exciter:
  - **Noise Burst**: velocity → lowpass/bandpass cutoff (200 Hz → 5000+ Hz), burst duration (longer soft, shorter hard), amplitude.
  - **Friction**: velocity → bow pressure and bow velocity envelope (per `BowExciter` interface), burst duration.
  - **FM Impulse**: velocity → modulation index (higher velocity → brighter/more inharmonic), amplitude, burst duration.
  - **Feedback**: velocity → drive amount into the feedback path, amplitude. The energy limiter is velocity-independent.
- **FR-062**: The Noise Body model's **maximum mode count** (the cymbal-hybrid modal component) MUST be selected to satisfy the CPU budget of FR-070/SC-003 while delivering a convincing hybrid sound. Spec 135 calls out "20–40 modes + filtered noise" as the recommended range, with "Cymbal mode count vs CPU" explicitly flagged as an open question. Phase 2 MUST resolve this by measurement: start at 40 modes, profile, reduce if the single-voice CPU exceeds the 1.25% budget. The final chosen count MUST be documented in `plan.md` with the measured CPU cost.

#### CPU Budget & Real-Time Safety

- **FR-070**: The single voice MUST remain within a **1.25% single-core CPU** budget on a modern desktop x86-64 CPU at 44.1 kHz, for every combination of (exciter type × body model × tone shaper on/off × unnatural zone on/off). This is 2.5× the Phase 1 SC-003 budget, leaving 8× headroom below a 10% total-plugin target for Phase 3's 8-voice pool.
- **FR-071**: Phase 2 MUST use the **scalar** `ModalResonatorBank` and the **scalar** `WaveguideString`/`KarplusStrong`. SIMD acceleration (`ModalResonatorBankSIMD`) is **deferred to Phase 3** unless the scalar path cannot meet the 1.25% single-voice budget, in which case Phase 2 MUST switch to SIMD and document the change. The default Phase 2 plan is: scalar-only, measure first, optimize only if measurement requires it.
- **FR-072**: All DSP on the audio thread MUST remain allocation-free, lock-free, and exception-free. `DrumVoice::process()`, `DrumVoice::noteOn()`, and all exciter/body `process()` methods MUST be verified by the `allocation_detector` test helper.
- **FR-073**: The Tone Shaper and Unnatural Zone additions MUST NOT introduce any audio-thread allocations, even when enabled and modulated.
- **FR-074**: FTZ/DAZ denormal protection MUST remain enabled (carryover from Phase 1).

#### Controller / Parameter Registration

- **FR-080**: Controller MUST register the following NEW parameters in addition to the 5 Phase 1 parameters. Exact IDs, names, ranges, and defaults are implementation details deferred to `plan.md`; the MINIMUM parameter list Phase 2 exposes to the host is:
  - **Exciter Type** (StringListParameter, 6 choices: Impulse, Mallet, NoiseBurst, Friction, FMImpulse, Feedback)
  - **Body Model** (StringListParameter, 6 choices: Membrane, Plate, Shell, String, Bell, NoiseBody)
  - **Mode Stretch** (0.5–2.0, default 1.0)
  - **Decay Skew** (-1.0–1.0, default 0.0)
  - **Mode Inject** (0.0–1.0, default 0.0)
  - **Nonlinear Coupling** (0.0–1.0, default 0.0)
  - **Material Morph Enable** (0/1 toggle)
  - **Material Morph Start** (0.0–1.0, default 1.0 = current Material)
  - **Material Morph End** (0.0–1.0, default 0.0)
  - **Material Morph Duration** (10–2000 ms, default 200 ms)
  - **Pitch Envelope Start** (absolute Hz, 20–2000 Hz, default 160 Hz)
  - **Pitch Envelope End** (absolute Hz, 20–2000 Hz, default 50 Hz)
  - **Pitch Envelope Time** (0–500 ms, default 0 = disabled)
  - **Filter Type** (StringListParameter: LP/HP/BP)
  - **Filter Cutoff** (20 Hz–20 kHz, default 20 kHz = bypass)
  - **Filter Resonance** (0.0–1.0, default 0.0)
  - **Filter Env Amount** (-1.0–1.0, default 0.0)
  - **Filter Env Attack / Decay / Sustain / Release** (ms and 0-1)
  - **Drive Amount** (0.0–1.0, default 0.0 = bypass)
  - **Fold Amount** (0.0–1.0, default 0.0 = bypass)
  - **Noise Burst Duration** (2–15 ms, default 5 ms)
  - **Friction Bow Pressure** (0.0–1.0, default 0.3)
  - Additional secondary parameters per exciter as needed (e.g., FM ratio, feedback amount).
- **FR-081**: All new parameters MUST follow the Iterum project parameter ID naming convention documented in `CLAUDE.md` (`k{Section}{Parameter}Id`). Suggested section prefixes: `kExciter…Id`, `kBody…Id`, `kMorph…Id`, `kToneShaper…Id`, `kUnnatural…Id`.
- **FR-082**: State save/load MUST round-trip ALL new Phase 2 parameters exactly (bit-identical normalized values), extending the Phase 1 state format. The state version field MUST be bumped from Phase 1's value and backward compatibility MUST be preserved: loading a Phase 1 state file into Phase 2 MUST produce a valid voice with Phase-2-default values for the new parameters.
- **FR-083**: Controller MUST continue to provide only the host-generic editor (no uidesc, no custom VSTGUI views). Custom UI is deferred to Phase 5 (see Deferred section).

#### Testing & Validation

- **FR-090**: A parameterized unit test MUST iterate all 6 × 6 exciter/body combinations, trigger MIDI note 36 at velocity 100, process 500 ms, and assert: output is non-silent (peak > -60 dBFS), no NaN/Inf/denormals, peak ≤ 0 dBFS, and no allocations.
- **FR-091**: For each of the 6 body models, a spectral verification test MUST measure the first N partial frequencies of the body's output (driven by an impulse at low Material for long decay) and compare them to the physical reference ratios, within the tolerance documented per body in the acceptance scenarios of US2.
- **FR-092**: For each of the 6 exciter types, a spectral verification test MUST measure the excitation signal's spectral centroid at velocity 30 and velocity 127 and assert the centroid ratio is ≥ 2.0 (the Phase 1 SC-005 standard).
- **FR-093**: A CPU benchmark test MUST run all (exciter × body × tone_shaper × unnatural) combinations and log the single-voice CPU cost. The test MUST fail any combination exceeding the 1.25% budget (FR-070 / SC-003) on the canonical CI reference machine.
- **FR-094**: A state-round-trip test MUST verify that all new Phase 2 parameters (FR-080) round-trip bit-exactly through save/load, and MUST verify Phase-1 → Phase-2 backward compatibility per FR-082.
- **FR-095**: A regression test MUST verify that the Phase 1 default patch (Exciter=Impulse, Body=Membrane, Tone Shaper off, Unnatural Zone at defaults) produces the same waveform as the Phase 1 reference output (within the existing Phase 1 approval-test tolerance, if any; otherwise introduce a new Phase 2 golden reference).
- **FR-096**: Pluginval MUST continue to pass at strictness level 5 on Windows with zero errors and zero warnings.
- **FR-097**: `auval` MUST continue to pass on macOS (`auval -v aumu Mbrm KrAt`).

#### ODR Prevention & Codebase Reuse

- **FR-100**: No new class name MUST collide with existing classes in the Membrum, Krate::DSP, or plugins/ namespaces. Before creating any new class (per Principle XIV), a search (`grep -r "class X" dsp/ plugins/`) MUST be performed. Expected new names: `Membrum::ExciterBank`, `Membrum::BodyBank`, `Membrum::ToneShaper`, `Membrum::UnnaturalZone`, `Membrum::MaterialMorph`, per-body mapper types in the `Membrum::Bodies::` sub-namespace (e.g., `Membrum::Bodies::PlateMapper`, `Membrum::Bodies::ShellMapper`, etc.). All exact names MUST be confirmed in `plan.md`.
- **FR-101**: Phase 2 MUST NOT modify the shared KrateDSP library (`dsp/`) unless strictly necessary. All new code lives in `plugins/membrum/src/dsp/`, `plugins/membrum/src/voice/`, or similar plugin-local paths. Any KrateDSP changes MUST be justified, isolated, and reviewed as part of Phase 2 implementation review.
- **FR-102**: Phase 2 MUST reuse the following existing KrateDSP components (not reimplement them): `ImpactExciter`, `BowExciter`, `NoiseGenerator`, `FMOperator`, `ModalResonatorBank`, `WaveguideString` or `KarplusStrong`, `HarmonicOscillatorBank`, `ADSREnvelope`, `MultiStageEnvelope`, `EnvelopeFollower`, `SVF`, `Waveshaper`, `Wavefolder`, `SaturationProcessor`, `TanhADAA` / `HardClipADAA`, `DCBlocker`, `LFO`, `XorShift32`, `FastMath`, `PitchUtils`, `Interpolation`, `OnePoleSmoother`. Spec 135 calls this out: "~80% of the DSP engine exists."
`FeedbackNetwork` is NOT reused — it is block-oriented stereo delay feedback (wrong shape for per-sample voice feedback); replaced by plugin-local `Membrum::FeedbackExciter` per research.md §3.

### Key Entities

- **DrumVoice (refactored)**: Single voice owning an `ExciterBank`, a `BodyBank`, a `ToneShaper`, an `UnnaturalZone` module, a `MaterialMorph` module, and an amp `ADSREnvelope`. Replaces Phase 1's hardcoded `ImpactExciter + ModalResonatorBank` composition.
- **ExciterBank**: Holds pre-allocated instances of all 6 exciter backends (ImpactExciter for Impulse+Mallet, NoiseBurst wrapper, BowExciter for Friction, FMOperator wrapper for FM Impulse, FeedbackNetwork wrapper for Feedback). Routes `process()` calls to the currently selected backend via `std::variant<ImpactExciter, NoiseBurstExciter, BowExciter, FMImpulseExciter, FeedbackExciter>` dispatched with `std::visit` or an index-based `switch` per block (mandated by FR-001).
- **BodyBank**: Holds pre-allocated backends for all 6 body models: one shared `ModalResonatorBank` instance (reconfigured at note-on for whichever modal body — Membrane, Plate, Shell, Bell, or Noise Body modal portion — is active), one `WaveguideString` (or `KarplusStrong`) for the String body, and the noise-path components (`NoiseGenerator` + `SVF`) for Noise Body's filtered-noise layer. Body-model switching while a voice is sounding is deferred to next note-on; the shared bank is cleared and reconfigured at that boundary. Routes `processSample()` calls via `std::variant` dispatch (consistent with FR-001).
- **ToneShaper**: Per-voice post-body chain containing an `SVF` + filter envelope, a `Waveshaper`/`SaturationProcessor`, a `Wavefolder`, and a `MultiStageEnvelope` for the Pitch Envelope (applied at the body-frequency level).
- **UnnaturalZone**: Container for Mode Stretch, Decay Skew, Mode Inject, Nonlinear Coupling, and Material Morph modules. Each Unnatural Zone feature is an independent toggleable addition to the base voice signal path.
- **MaterialMorph**: Per-voice envelope that animates the `Material` parameter during a single note via a 2-point (or multi-point) interpolation curve.
- **Per-Body Mapper helpers**: One small type per body model that converts the common 5 normalized parameters into body-specific configuration (mode frequencies, amplitudes, damping, strike-position weights). Replaces the Phase 1 inline mapping in `DrumVoice::noteOn` / `updateModalParameters`.
- **Mode ratio tables**: `membrane_modes.h` (existing from Phase 1), `plate_modes.h`, `shell_modes.h`, `bell_modes.h` — each with `constexpr` arrays of physically-grounded ratios, strike-position math, and documentation of the source paper.

## Success Criteria *(mandatory)*

### Measurable Outcomes

- **SC-001**: All 6 exciter types and all 6 body models are selectable via host-visible parameters and each combination produces audible, non-NaN, non-clipping output across 500 ms of processing at 44.1 kHz.
- **SC-002**: Each of the 6 body models' measured first-N-partial ratios match the physical reference (Bessel for Membrane, Kirchhoff square-plate for Plate, free-free Euler-Bernoulli for Shell, harmonic series for String, Chladni for Bell, broadband+sparse for Noise Body) within the per-body tolerances: ±2% for Membrane, ±3% for Plate, ±3% for Shell, ±1% for String, ±3% for Bell. Noise Body's modal portion meets ±3% plate ratios.
- **SC-003**: Single-voice CPU at 44.1 kHz is ≤ **1.25%** on a modern desktop x86-64 CPU for every combination of (exciter type × body model × tone shaper on/off × unnatural zone on/off). The worst-case combination is recorded with its measured value.
- **SC-004**: Each of the 6 exciter types shows a spectral-centroid ratio ≥ **2.0** between velocity 30 and velocity 127 (Phase 1 SC-005 standard, extended to every exciter type).
- **SC-005**: The Phase 1 default patch (Impulse + Membrane + Tone Shaper bypassed + Unnatural Zone off) produces output that matches the Phase 1 reference within a **-90 dBFS RMS difference** on the first 500 ms of MIDI note 36 at velocity 100. (Regression guarantee.)
- **SC-006**: State save/load round-trips all Phase 2 parameters (20+ new parameters plus 5 Phase 1 parameters) bit-exactly in the normalized float representation. Phase-1 state files load successfully with Phase-2 defaults for new parameters.
- **SC-007**: Output contains no NaN, Inf, or denormal values across all 36 exciter/body combinations at default parameter values, at extreme-low and extreme-high parameter values, and across sample rates {22050, 44100, 48000, 96000, 192000} Hz.
- **SC-008**: The Feedback exciter with every body at max feedback drive, and the Nonlinear Coupling parameter at 1.0 on every body at velocity 127, both keep peak output ≤ 0 dBFS (stability guards engaged).
- **SC-009**: The 808-kick pitch-envelope test (Pitch Envelope: 160 Hz → 50 Hz over 20 ms, Impulse + Membrane) measures a fundamental glide that reaches the target frequency within ±10% at t=20 ms.
- **SC-010**: Pluginval passes at strictness level 5 with zero errors on Windows. `auval -v aumu Mbrm KrAt` passes on macOS. CI builds succeed on Windows x64, macOS universal, and Linux x64 with zero compiler warnings.
- **SC-011**: Zero allocations are reported by the `allocation_detector` helper for `DrumVoice::noteOn()`, `DrumVoice::noteOff()`, `DrumVoice::process()`, and every exciter/body backend's `process()` method, across all 36 combinations.

## Assumptions & Existing Components *(mandatory)*

### Assumptions

- The existing KrateDSP components listed in FR-102 are stable, tested, and suitable for direct reuse. Phase 2 wires them together with thin wrappers and per-body mapping helpers — it does not reimplement core DSP.
- `ModalResonatorBank`'s `setModes/updateModes` API (as documented in Phase 1 `plan.md`) exposes enough per-mode control to implement Decay Skew without modifying the bank itself; if not, FR-051 allows a per-mode damping override pre-computed in the Membrum voice. The exact mechanism is resolved in `plan.md` after reading the header.
- `WaveguideString` and/or `KarplusStrong` already implement `IResonator` or an equivalent `processSample(float excitation) -> float` interface that makes them drop-in for the String body model.
- `BowExciter` supports a transient (non-sustained) trigger mode suitable for FR-013's struck-Friction use case; if not, a short-envelope wrapper (≤ 50 ms bow-pressure ramp) is added in the Friction exciter wrapper. Sustained bowed excitation is explicitly deferred to Phase 4.
- `FMOperator` and `FeedbackNetwork` have `process()` / `reset()` APIs compatible with the per-voice real-time contract.
- The Phase 1 `DrumVoice` tests use inspection of output characteristics (peak, spectral centroid, mode frequencies, etc.) and do NOT depend on internal implementation details that the swap-in refactor changes. Any Phase 1 test that does depend on internal implementation will be adapted, not weakened — behavioural acceptance criteria MUST remain equivalent or stricter.
- The CI reference machine's CPU performance is consistent enough between runs that a 1.25% budget can be measured reliably with a ±20% margin. Spot-check profiling on developer machines is acceptable; absolute CPU numbers are recorded in the Phase 2 `plan.md` so future phases can track regressions.
- No custom UI is required for Phase 2 validation. The host-generic editor is sufficient to expose all new parameters for testing. Custom UI is Phase 5.

### Existing Codebase Components (Principle XIV)

*GATE: Must identify before `/speckit.plan` to prevent ODR violations.*

**Relevant existing components that MUST be reused (not re-implemented):**

| Component | Location | Relevance |
|-----------|----------|-----------|
| ImpactExciter | `dsp/include/krate/dsp/processors/impact_exciter.h` | Phase 1 Impulse exciter; Phase 2 Impulse + Mallet types |
| BowExciter | `dsp/include/krate/dsp/processors/bow_exciter.h` | Phase 2 Friction exciter type (transient mode only) |
| NoiseGenerator | `dsp/include/krate/dsp/processors/noise_generator.h` | NOT used in hot path — replaced by `NoiseOscillator`; see research.md §2 |
| FMOperator | `dsp/include/krate/dsp/processors/fm_operator.h` | Phase 2 FM Impulse exciter type |
| FeedbackNetwork | `dsp/include/krate/dsp/systems/feedback_network.h` | Phase 2 Feedback exciter type |
| ModalResonatorBank | `dsp/include/krate/dsp/processors/modal_resonator_bank.h` | Membrane, Plate, Shell, Bell, Noise Body modal portions |
| WaveguideString | `dsp/include/krate/dsp/processors/waveguide_string.h` | Phase 2 String body (primary) |
| KarplusStrong | `dsp/include/krate/dsp/processors/karplus_strong.h` | Phase 2 String body (fallback) |
| HarmonicOscillatorBank | `dsp/include/krate/dsp/processors/harmonic_oscillator_bank.h` | Mode Inject source |
| SVF | `dsp/include/krate/dsp/primitives/svf.h` | Tone Shaper filter, Noise Burst coloring, Noise Body noise filter |
| Waveshaper / SaturationProcessor | `dsp/include/krate/dsp/primitives/waveshaper.h`, `processors/saturation_processor.h` | Tone Shaper Drive stage |
| Wavefolder | `dsp/include/krate/dsp/primitives/wavefolder.h` | Tone Shaper Wavefolder stage |
| TanhADAA / HardClipADAA | `dsp/include/krate/dsp/primitives/tanh_adaa.h`, `hard_clip_adaa.h` | Alias-free saturation inside Feedback exciter path |
| DCBlocker | `dsp/include/krate/dsp/primitives/dc_blocker.h` | Post-saturation DC offset removal |
| ADSREnvelope | `dsp/include/krate/dsp/primitives/adsr_envelope.h` | Amp envelope (Phase 1 reuse), filter envelope (Phase 2 new) |
| MultiStageEnvelope | `dsp/include/krate/dsp/processors/multi_stage_envelope.h` | Pitch Envelope in Tone Shaper |
| EnvelopeFollower | `dsp/include/krate/dsp/processors/envelope_follower.h` | Nonlinear Coupling velocity/energy driver |
| IResonator | `dsp/include/krate/dsp/processors/iresonator.h` | NOT used for hot-path dispatch (virtual call forbidden per FR-001); may be used for off-thread configuration only |
| XorShift32 | `dsp/core/xorshift32.h` | Phase randomization for Mode Inject |
| FastMath | `dsp/core/fast_math.h` | Fast tanh/sqrt in Feedback exciter and energy limiter |
| OnePoleSmoother | `dsp/include/krate/dsp/primitives/smoother.h` | Click-free parameter transitions on body/exciter swap |
| membrane_modes.h (plugin-local) | `plugins/membrum/src/dsp/membrane_modes.h` | Reused as-is; new per-body tables added alongside |
| DrumVoice (plugin-local) | `plugins/membrum/src/dsp/drum_voice.h` | **Refactored** by Phase 2 to use ExciterBank + BodyBank |

**Initial codebase searches to perform at `/speckit.plan` time:**

```bash
# Confirm the existing components exist and identify their public APIs
grep -r "class ImpactExciter" dsp/
grep -r "class BowExciter" dsp/
grep -r "class NoiseGenerator" dsp/
grep -r "class FMOperator" dsp/
grep -r "class FeedbackNetwork" dsp/
grep -r "class ModalResonatorBank " dsp/
grep -r "class WaveguideString" dsp/
grep -r "class KarplusStrong" dsp/
grep -r "class HarmonicOscillatorBank" dsp/
grep -r "class SVF " dsp/
grep -r "class Waveshaper" dsp/
grep -r "class Wavefolder" dsp/
grep -r "class MultiStageEnvelope" dsp/
grep -r "class EnvelopeFollower" dsp/
grep -r "class IResonator" dsp/

# Confirm no name collisions for planned new types
grep -r "class ExciterBank" .
grep -r "class BodyBank" .
grep -r "class ToneShaper" .
grep -r "class UnnaturalZone" .
grep -r "class MaterialMorph" .
grep -r "namespace Bodies" plugins/
```

**Search Results Summary**: Phase 1 already verified `ImpactExciter`, `ModalResonatorBank`, `ADSREnvelope` exist (see spec 136 line 224–230). Spec 135 "Existing Building Blocks" table (lines 262–397) enumerates all the other components above as existing and ready-to-use. No new KrateDSP-level classes are introduced by Phase 2.

### Forward Reusability Consideration

**Sibling features at same layer:**
- **Phase 3** (32-pad voice allocation): Will instantiate 8 DrumVoice instances in a voice pool. The `DrumVoice` refactored in Phase 2 must be cheap to duplicate and must work without any global state — each DrumVoice owns its own ExciterBank, BodyBank, ToneShaper, and UnnaturalZone instances.
- **Phase 4** (bowed/sustained excitation): Will extend the Friction exciter (and possibly introduce new sustained-mode exciter classes) with continuous bow pressure. Phase 2's `ExciterBank` must not make short-transient assumptions that prevent later sustained modes.
- **Phase 5+** (Acoustic/Extended UI modes, macro controls, presets, pad templates, cross-pad coupling, snare wires, custom UI): Phase 2's parameter set must be exposed in a way that UI phases can group and re-label without reworking the processor.

**Potential shared components:**
- The per-body parameter-mapping helpers (FR-030–FR-035) are strong candidates for promotion to a shared helper library if Phase 3 or 4 adds more body variants or per-voice parameter reshaping.
- The Mode Inject + Nonlinear Coupling + Material Morph modules in the Unnatural Zone could, in principle, be reused by a future Membrum-adjacent instrument (e.g., a tuned-percussion synth). Phase 2 keeps them plugin-local; promotion is considered post-Phase 3 once usage patterns stabilize.
- The `ExciterBank` / `BodyBank` dispatch pattern is likely to recur in any polyphonic physical-modeling instrument. If Phase 3 shows the pattern works, it may be extracted to a `plugins/shared/` helper.

## Scientific Verification Notes *(mandatory — per user requirement)*

Spec 135's "Physical Modeling Reference" section makes claims about the physics of each body model and exciter type. Below is the verification status of each key claim, cross-checked against published acoustics literature via web search on 2026-04-10. Sources are cited inline. **If any discrepancy was found, it is flagged here rather than silently "corrected" in Phase 2 — the user should review and decide.**

### Verified and consistent with spec 135

1. **Circular membrane mode ratios (Bessel zeros j_{m,n} / j_{0,1})** — The ratios listed in spec 135 (1.000, 1.593, 2.136, 2.296, 2.653, 2.918, 3.156, 3.501, ...) are the standard Bessel-function-zero ratios used in drum acoustics literature. Confirmed against Euphonics 3.6.1 "Vibration modes of a circular drum," Acoustics/Bessel Functions and the Kettledrum (Wikibooks), Wikipedia "Vibration of a circular membrane," and NIST DLMF 10.21 (standard reference). Phase 1 already encodes these in `membrane_modes.h`. **Status: VERIFIED.**

2. **Timpani air-loading shifts toward near-harmonic series** — Spec 135 claims that air coupling in a kettledrum produces near-harmonic ratios approximately {1, 1.5, 2, 2.5, 3} for the (1,1),(2,1),(3,1),(4,1),(5,1) modes. This matches Rossing's published findings: "The proper adjustment of the preferred modes can produce frequencies nearly in the ratios of 1 : 1.5 : 2 : 2.5 : 3 : 3.5 to that of a pitch with a missing fundamental" and the air-coupling research paper "Effects of air loading on timpani membrane vibrations" (JASA 1984). Rossing's papers are referenced in "The Well-Tempered Timpani" online resource. **Status: VERIFIED.** Note: Phase 2 retains Membrane with the **ideal** (non-air-coupled) ratios by default; the air-coupled variant is exposed as a future "Air Coupling" parameter (not in Phase 2 scope — spec 135 lists it as a body parameter, but Phase 2 keeps the simpler ideal-membrane for now).

3. **Free-free beam Euler-Bernoulli ratios {1.000, 2.757, 5.404, 8.933, 13.344, 18.637}** — These are the **untuned** free-free beam ratios from Euler-Bernoulli theory, confirmed by the search result "The second and third modes of a xylophone bar produce frequencies that are 2.76 and 5.4 times the fundamental" (from physics-of-xylophones references on euphonics.org and supermediocre.org). **Status: VERIFIED.** **IMPORTANT CLARIFICATION**: Tuned xylophone/marimba bars have modified ratios (commonly 1:3:6 for xylophone and 1:4:10 for marimba, achieved by undercutting the bar). Phase 2 implements the **untuned** free-free beam ratios for the Shell body because that matches spec 135's "For the Shell body model and bar-type percussion" reference table. A future body-model variant ("Tuned Bar") could add the tuned ratios — this is **not in Phase 2 scope**. This clarification is worth flagging to the user: Phase 2's Shell body will sound like an **untuned raw bar**, not like a polished xylophone. If the user wants xylophone/marimba-like tuned ratios, we should add a "Tuned Bar" variant or a "tuning" parameter on the Shell body — but this is beyond current Phase 2 scope.

4. **Chaigne-Lambourg frequency-dependent damping model** — The model `R_k = b1 + b3 · f_k²` and its application to time-domain plate simulation is verified. Chaigne & Lambourg published "Time-domain simulation of damped impacted plates. I. Theory and experiments" and "II. Numerical model and results" in JASA 2001 (volume 109, pp. 1422 and 1433). The damping model includes three mechanisms (thermoelasticity, viscoelasticity, radiation) compressed into the b1/b3 form for modal synthesis use. **Status: VERIFIED.** Phase 1's `ModalResonatorBank` already implements this.

5. **Chladni's law for church bell partials, with hum/prime/tierce/quint/nominal ratios {0.25, 0.50, 0.60, 0.75, 1.00}** — Verified against "Partial Frequencies and Chladni's Law in Church Bells" by Hibberts (Open Journal of Acoustics, 2014). The paper confirms the generalized Chladni formula `f_{m,n} = C(m + bn)^p` and lists these five true-harmonic bell partial ratios. **Status: VERIFIED.**

6. **Chowning FM synthesis bell-like inharmonic ratios (1:1.4)** — Verified. Chowning's 1973 JAES paper "The Synthesis of Complex Audio Spectra by Means of FM" is the original source. FM ratios around 1:1.4, 1:sqrt(2) ≈ 1:1.414, and similar non-integer ratios produce bell-like metallic inharmonic spectra. Multiple sources confirm that "Inharmonic bell-like and percussive spectra can be created" via non-integer carrier:modulator ratios. **Status: VERIFIED.** Spec 135's "The modulation index must decay faster than carrier amplitude" technique is also a standard Chowning recommendation for bell synthesis.

7. **Power-law contact force F = K · δ^α for mallet-membrane interaction** — Verified. This is the Hertzian/Hunt-Crossley contact model, standard in contact mechanics. The power-law relationship between force and compression (δ^α with α in the 1.5–4 range depending on mallet hardness) is the foundation of virtual-analog percussion excitation models (Chaigne & Doutaut; Hunt-Crossley; Boutillon piano hammers). **Status: VERIFIED**, though the specific alpha-range values in spec 135's table (1.5–2.5 for soft felt, 2.5–3.5 for hard rubber, 3.0–4.0 for wood stick tip) are typical values from the sound-synthesis literature; search did not return the primary paper for these specific numbers. Phase 2 takes these ranges as spec-135-authoritative and uses them as defaults; they are empirically tweakable.

8. **Nonlinear tension modulation → 65 cents pitch glide** — Verified. "Experimental data show pitch glides can reach a relative shift of 65 cents at high amplitudes in drum membranes" is a result from Bilbao, Torin, and Touzé's work on nonlinear membrane drums. Confirmed via "Nonlinear Effects in Drum Membranes," "Numerical Experiments with Non-linear Double Membrane Drums" (Torin & Bilbao), and "Energy Based Synthesis of Tension Modulation in Membranes." The 65-cent figure appears in multiple publications on tom-tom pitch glides. **Status: VERIFIED.** Note: Phase 2 does NOT implement the "Nonlinear Pitch" parameter (it is distinct from Pitch Envelope in the Tone Shaper). This is documented as future work in the Deferred section below — the existing Pitch Envelope covers the 808-kick use case, which is the P2-priority kick-sweep scenario.

9. **SMC 2019 perceptual study on partial counts** — Spec 135 cites "Delle Monache et al. SMC 2019" for the "16 default" decision. A paper titled "Perceptual Evaluation of Modal Synthesis for Impact-Based Sounds" was presented at SMC 2019 (search result: https://www.smc2019.uma.es/articles/P1/P1_05_SMC2019_paper.pdf, GitHub: adrianbarahona/SMC-Conference-2019_Perceptual-Evaluation-of-Modal-Synthesis-for-Impact-Based-Sounds). The paper is real. **Status: VERIFIED — paper exists.** **CAVEAT**: Web search did not return enough of the paper's content to independently verify the specific claim "20-30 modes indistinguishable from recordings." The claim appears in spec 135 and is consistent with modal-synthesis practice (16 modes is generally considered the sweet spot for membranes), but a direct paper-content quote is not confirmed. Recommendation: the user may wish to download the paper PDF and confirm the exact figure. For Phase 2, 16 modes remains the Membrane default (unchanged from Phase 1).

10. **von Karman plate theory, cubic nonlinear mode coupling for cymbal synthesis** — Verified. "Modes couple in order to yield a cubic-type nonlinearity... a series of coupled oscillators with cubic nonlinearities" — confirmed against Chaigne, Touzé, Thomas "Nonlinear vibrations and chaos in gongs and cymbals" (Journal of Sound and Vibration 2015), "Nonlinear Vibrations of Thin Rectangular Plates" (Touzé doctoral thesis), and Ducceschi et al. "Real-Time Physical Model of a Cymbal Using the Full von Kármán Plate Equations" (2017). The cubic mode-coupling form is the standard expansion of the von Karman plate equations over linear eigenmodes. **Status: VERIFIED.** Phase 2's FR-053 explicitly allows a simplified stand-in implementation rather than the full VK equations (which is acceptable because spec 135 itself acknowledges the full model is CPU-expensive and Phase 2 needs to stay within the 1.25% voice budget).

### Verified but with a CPU-feasibility caveat (flagged for user awareness)

11. **Cymbal hybrid modal + filtered noise (20-40 modes)** — Verified as an established technique. Spec 135 cites "Dahl et al., DAFx 2019" for this. The actual DAFx 2019 paper (https://www.dafx.de/paper-archive/2019/DAFx2019_paper_48.pdf) titled (paraphrasing from search result) "Real-Time Modal Synthesis of Crash Cymbals with Nonlinear Approximations, Using a GPU" uses **over 2000 modes per cymbal on a GPU**, not 20-40. The hybrid approach (modal + noise) is real and well-established in general — see earlier CCRMA tutorials and Sound-on-Sound "Synthesizing Realistic Cymbals" articles — but the specific "20-40 modes is sufficient" claim is NOT what the cited Dahl 2019 paper argues. **Status: PARTIALLY VERIFIED — FLAG RAISED.**
    - **Impact on Phase 2**: spec 135's open question "Cymbal mode count vs CPU: Need profiling to determine if 30-40 modes + noise is viable" remains valid. Phase 2 resolves it **empirically** via FR-062: start at 40 modes, profile on the scalar path, and reduce if the 1.25% budget is blown. The final chosen count is documented in `plan.md`. A convincing cymbal sound with only 20-40 modes is a well-known compromise in CPU-constrained synths (MODO Drum, Sensel Morph, various VSTs use samples for cymbals specifically because modal synthesis is expensive). Phase 2 expects the hybrid (modal + filtered noise) to sound "good enough" at 30-40 modes, but this is a **known quality risk**, and the user may want to accept that Membrum's Noise Body will not sound as detailed as a 2000-mode GPU-accelerated cymbal. If Phase 2's Noise Body turns out to sound unconvincing, the remediation options are: (a) increase modes to ~60 and profile CPU, (b) improve the noise portion, (c) add a second noise layer with different filter envelopes, (d) accept the compromise until Phase 6+ adds SIMD and enables higher mode counts per voice.
    - **Citation correction recommendation**: Spec 135 cites "Dahl et al., DAFx 2019" for the 20-40 mode hybrid claim; the actual DAFx 2019 paper does not support that specific claim. A more accurate citation for the hybrid approach is probably Dahl's earlier work or Sound-on-Sound's "Practical Cymbal Synthesis" series. We recommend the user update the citation in spec 135 when convenient, but Phase 2 does NOT modify spec 135 unilaterally.

### Not verified in this session (noted transparently)

12. **Bilbao penalty method for snare wire collisions** — Confirmed via "Time domain simulation and sound synthesis for the snare drum" (Bilbao, JASA 2012, vol. 131, pp. 914-925) and "An Energy Conserving Finite Difference Scheme for the Simulation of Collisions in Snare Drums" (Torin, Hamilton, Bilbao). The penalty-method collision model is real and published. **Status: VERIFIED**, but **NOT RELEVANT TO PHASE 2**: snare wire modeling is deferred (FR-047, see Deferred section). Listed here only for completeness.

13. **LuGre friction model for brushes** — Not independently verified in this session because spec 135 refers to Bilbao IEEE 2022 and Avanzini DAFx 2002 for brush excitation, which are not in Phase 2 scope (brush excitation is a specialized type of friction excitation that is a subset of the generic Friction exciter, and Phase 2 uses `BowExciter`'s existing STK power-law friction model rather than LuGre). **Status: NOT VERIFIED — NOT IN SCOPE.**

### Summary

Of the 13 scientific claims cross-checked, **12 are consistent with published literature** (Bessel membrane modes, timpani air coupling, free-free beam ratios, Chaigne-Lambourg damping, Chladni bell partials, Chowning FM ratios, Hertzian contact power law, nonlinear tension modulation pitch glide, von Karman plate cubic coupling, Bilbao snare collisions, SMC 2019 paper exists, modal-synthesis partial-count guidance). **Two caveats exist**:

1. **Shell body = untuned free-free beam, NOT tuned xylophone** — Spec 135's ratios are the raw Euler-Bernoulli ratios (1, 2.757, 5.404, ...), which sound correct for an untuned bar but not for a tuned xylophone/marimba (1:3:6 / 1:4:10). Phase 2 implements the untuned ratios. A future "Tuned Bar" body may be added.
2. **Dahl DAFx 2019 citation for the "20-40 mode cymbal hybrid" claim may be inaccurate** — the cited paper uses 2000+ modes on GPU. The hybrid technique is real and well-established, but the specific low-mode-count claim is not sourced from that paper. Phase 2 proceeds with the hybrid approach and empirically selects the mode count to meet the CPU budget.

No discrepancy invalidates the Phase 2 plan. Both caveats are documented here for user awareness.

## Deferred to Later Phases *(mandatory)*

The following features from spec 135 are **consciously out of scope for Phase 2**, with the phase they belong to:

| Feature | Deferred to | Reason |
|---------|-------------|--------|
| 32-pad voice allocation (VoiceAllocator, voice stealing, polyphony 4–16) | Phase 3 | Phase 2 is single-voice; MIDI note 36 only |
| Choke groups (hat open/closed, etc.) | Phase 3 | Depends on voice allocation |
| Per-pad parameter storage (32 pads × full parameter set) | Phase 3 | Depends on voice allocation |
| Cross-pad coupling (sympathetic resonance, coupling matrix, snare buzz, tom resonance) | Phase 3 | Requires multiple voices to couple between |
| Pad templates (Kick, Snare, Tom, Hat, Cymbal, Perc, Tonal, 808, FX) | Phase 3 | Templates require the pad system and preset infrastructure |
| Sustained / continuous bowed excitation (Friction with indefinite bow pressure) | Phase 4 | Phase 4 is explicitly "bowed/sustained excitation" |
| Snare wire modeling (membrane-specific post-body noise with displacement threshold) | Phase 4 or dedicated phase | Requires new collision-model code not present in KrateDSP; only applies to Membrane |
| Acoustic vs Extended mode gating of parameters | Phase 5 (UI phase) | Pure UI-layer feature; processor exposes everything |
| Macro Controls (Tightness, Brightness, Body Size, Punch, Complexity) | Phase 5 (UI phase) | Macros aggregate lower-level parameters, belong with UI design |
| Tier 1 coupling UX ("Snare Buzz", "Tom Resonance", "Global Coupling" knobs) | Phase 5 | Requires cross-pad coupling engine from Phase 3 |
| Custom VSTGUI UI (4×8 pad grid, per-pad editor, XY morph pad) | Phase 5 | Explicit phase for UI design |
| Preset Manager integration (kit presets, per-pad presets, preset browser) | Phase 5 or 6 | Requires pad system; Phase 1 deferred presets to Phase 9 |
| Per-pad effects (reverb, delay sends) | Phase 6+ | Requires pad routing; may rely on DAW routing |
| Output routing (separate outputs per pad, bus assignment) | Phase 6+ | Requires pad system |
| Modulation (LFOs, mod matrix per pad) | Phase 6+ | Depends on per-pad params |
| Microtuning / custom tuning tables for tonal body models | Phase 6+ | Nice-to-have |
| Double membrane coupling (batter + resonant heads with air cavity spring) | Phase 7+ | Doubles modal computation; open question in spec 135 |
| `ModalResonatorBankSIMD` acceleration | Phase 3 (or earlier if Phase 2 CPU budget is blown) | Scalar is sufficient for Phase 2 single voice; SIMD is critical for Phase 3's 8 voices |
| Air Coupling parameter for Membrane (timpani-like) | Future Phase | Phase 2 keeps ideal-membrane Bessel ratios |
| Nonlinear Pitch parameter (velocity-dependent pitch rise from tension modulation) | Future Phase | Distinct from Tone Shaper's Pitch Envelope; Phase 2 uses Pitch Envelope for kick sweeps |
| Tuned bar body variant (xylophone/marimba 1:3:6 / 1:4:10 ratios) | Future Phase | Phase 2 Shell body uses untuned free-free beam ratios |
| Hi-hat pedal position continuous control | Phase 3 (when Noise Body is used for hi-hat pad) | Requires per-pad state |
| 808-specific oscillator bank (six inharmonic oscillators at ~142, 211, 297, ...) | Future Phase | Phase 2 808 kick sound comes from Impulse + Membrane + Pitch Envelope |

Anything not listed here and not explicitly covered by Phase 2's FRs is **implicitly deferred** and should be discussed before expanding scope.

## Implementation Verification *(mandatory at completion — FILLED DURING /speckit.implement)*

*This section is left EMPTY during the specification phase. It will be filled during `/speckit.implement` when every FR and SC is verified against actual code and test output per Principle XVI.*

### Compliance Status

| Requirement | Status | Evidence |
|-------------|--------|----------|
| FR-001 | — | (fill during implementation) |
| FR-002 | — | (fill during implementation) |
| ... | ... | ... |
| SC-001 | — | (fill during implementation) |
| ... | ... | ... |

**Status Key:**
- ✅ MET: Requirement verified against actual code and test output with specific evidence
- ❌ NOT MET: Requirement not satisfied (spec is NOT complete)
- ⚠️ PARTIAL: Partially met with documented gap and specific evidence of what IS met
- 🔄 DEFERRED: Explicitly moved to future work with user approval

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

**Overall Status**: (fill during implementation)
