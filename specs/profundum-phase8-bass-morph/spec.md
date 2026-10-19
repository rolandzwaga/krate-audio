# Feature Specification: Profundum Phase 8 — Bass Morph

**Spec slug:** `profundum-phase8-bass-morph`
**Status:** Draft (2026-10-19). The requirements are complete. Every bar marked *drafted* is a proposal for the
clarify / plan stages to rule; once a bar is ruled, the ruling goes into its FR / SC row first, then into
Clarifications. The two Open Questions the roadmap deferred to this spec are ruled (2026-10-19, Q1-Q8).
**Roadmap source:** `specs/Profundum-roadmap.md` → Part A → **Phase 8** (lines 573–607). This spec also traces to:
- **Key Design Decision 4** (lines 137–144). "Bass Morph interpolates two complete sound states, globally. A and B
  each hold the full sound-shaping state (core, fold, shape, character, drop, amp envelope, stereo, motion config,
  mass, anchor). Interpolation is per-parameter with the right law (linear / logarithmic for frequencies and times /
  equal-power for mixes / crossfade or algorithm-specific transition for discrete choices); the core's spectrum is
  derived from the *interpolated controls*, never an audio crossfade. The Anchor morphs on its own curve: **Lock**
  (constant), **Blend** (interpolated), **Punch** (designed curve — weight retained early, yielding late). Morph is
  one continuous, automatable performance control affecting all voices together; it is not a preset switcher."
- **Architecture Overview**, lines 101–102: "BASS MORPH (A/B complete states, global, Anchor morphs on its own)".
- **Reuse Inventory, "Morph" row** (line 169). It lists `plugins/disrumpo/src/dsp/morph_engine.h` ("the
  *discrete-choice transition* idea"), `systems/spectral_morph_engine.h` and `processors/harmonic_frame_utils.h`.
  Verdict: "🆕 `MorphState` (plain data) + `BassMorphEngine` (L3). A generic A/B parameter-state interpolator does not
  exist anywhere." The ODR hazard list (lines 174–176) names `MorphEngine`, `MorphNode` and `SpectralState`.
- **Upstream handoffs into this phase**, written into the roadmap by Phases 3–7:
  - Phase 3 (line 323): `anchorFoldFeed` "ships as Phase 9's `anchorFoldFeed` (default 0)", and Phase 8's
    `MorphState` gains the field (Phase 3 spec lines 538, 781).
  - Phase 4 (Phase 4 spec D-4, lines 758–764): Drop Amount / Time / Shape / Rebound are latched at trigger, so
    "Phase 8's Morph … affect[s] the next note's Drop, not the one in flight". D-1 (line 748): "Phase 8 morphs Drop
    times logarithmically, so T must be > 0; the minimum is 1 ms."
  - Phase 5 (lines 433–434): "**Phase 8:** SHAPE, Amount, Tone and the algorithm are the morphable fields;
    transitions only through `setAlgorithm`." Phase 5 FR-057: "Phase 8 switches at a threshold morph position. At
    most two algorithms run at once … No `setAlgorithmBlend`."
  - Phase 6 (line 491): "attack / sustain gains are user-facing (Phase 11, 700–799; defaults +6 / 0 dB), duration is
    Mass-driven". Phase 6 FR-026: "Phase 8 interpolates Mass as a scalar".
  - Phase 7 (lines 533–534): "`Controls` is the morphable set; mode is stepped through the 10 ms side crossfade plus
    the vector slew (measured 9.98 ms)".
- **Phase 11 consumer** (lines 697–707): the morph editing model (Open Question 2), "Copy A → B / B → A and A / A·B / B
  compare are controller-side operations on the two snapshots, not parameters", and "the Morph position ramp time is
  registered if Phase 8 exposes it".
- **Phase 12 consumer** (lines 761–764, 770–772): the Morph component shows "Phase 8's per-parameter laws restated as
  what the user sees"; a user-selectable global "parameter curve" is **not adopted** ("the per-parameter laws are
  fixed by Phase 8's ruling"); the Anchor's own modes and its morph behaviour "are two controls and never share one
  widget or one label set".
- **Cross-Cutting Constraints** (lines 933–958): RT safety; the audibility, mono-compatibility and anti-aliasing
  theme FRs; layer discipline and the ODR sweep; CPU budgets as FRs; close lanes scoped to the change set; no
  bit-exact float goldens; portability; shared-component changes append-only.
- **Open Question 2** (lines 963–965): "Morph editing model on the host surface … Phase 8 for the engine contract,
  Phase 11 for the VST surface."

**Concept source:** `specs/Profundum-concept.md` §10 "The killer feature: Bass Morph" (lines 462–518): two complete
bass states; "the morph isn't ordinary parameter interpolation. The synthesis engine understands what is being
morphed": *sine → complex oscillator, clean → nonlinear, mono → stereo, short → long, dark → bright*; "The Anchor can
morph differently … main voice 0 → 100 %, anchor 100 → 70 %"; "while the fundamental never disappears".

**Layer.** Both new components are Layer 3, header-only, `Krate::DSP`: `MorphState` in `systems/morph_state.h` and
`BassMorphEngine` in `systems/bass_morph_engine.h`. Both names pass the ODR sweep (see "New components").

---

## Overview

Bass Morph lets one performance control travel between two complete bass identities, A and B. It is a control-domain
engine: it holds two `MorphState` snapshots and, once per 32-sample control interval, resolves **one** `MorphState`
from a smoothed morph position. The voice (Phase 9) feeds that resolved state to the components that already exist.
The engine has no audio path. It never crossfades two renders.

1. **`MorphState`** is plain data. It aggregates the `Controls` structs the earlier phases built for exactly this
   purpose (`SpectralShapeRecipe::Controls`, `ProfundumCore::ExcitationControls`, `BassStereoField::Controls`,
   `AnchorOscillator::Controls`), plus the scalar controls of the Fold, Shape, Character, Drop, amp envelope, Motion
   section and Mass. It is validated (`isValidMorphState`) and serialised in a fixed, versioned byte layout.
2. **`BassMorphEngine`** interpolates field by field, with the law the roadmap assigns to each field's kind:
   - **linear** for most controls;
   - **logarithmic** for frequencies and times;
   - **equal-power** for mixes;
   - **stepped** for discrete choices. The step happens at a threshold position, and the component's own click-free
     transition does the crossfade: `BassCharacterStage::setAlgorithm`, the field's image-mode crossfade, and the
     Anchor's waveform crossfade and mode bridge.

   The recipe controls are interpolated in the recipe domain, so the core's spectrum at every position comes from
   the interpolated D / B / E / S. Each of the concept's five perceptual axes has a named law (FR-040).
3. **The Anchor morphs on its own curve.** The engine computes a second position p_a for the Anchor block: **Lock**
   holds A's Anchor, **Blend** follows the main position, and **Punch** follows a designed late-yielding curve. The
   concept's "main voice 0 → 100 %, anchor 100 → 70 %" is Blend with per-endpoint Anchor levels.
4. **The morph is global.** One position and one resolved state per control interval, read by every voice.

## Scope

1. `MorphState` (L3), complete:
   - the field set (FR-001–FR-004);
   - validation (FR-005);
   - serialisation (FR-006, FR-007).
2. `BassMorphEngine` (L3), complete:
   - the interface and lifecycle (FR-010–FR-016);
   - the position ramp (FR-020–FR-022);
   - the per-field interpolation laws (FR-030–FR-039);
   - the five perceptual-axis laws (FR-040);
   - the Anchor morph modes (FR-050–FR-055);
   - the engine contract for the editing model (FR-060–FR-062; Open Question OQ-1).
3. A test fixture, test code only, that applies a resolved state to the existing components, chained as in Phase 7:
   core (+ excitation) → `ShapeMorph` → `HarmonicFold` → `BassCharacterStage` → + Anchor → `MassDynamics` →
   `BassStereoField` safety stage → `DCBlocker` ×2 → `TruePeakLimiter`, plus a per-voice amp envelope and a Drop on
   the pitch. It is a new per-interval render loop that reuses the Phase 6 and 7 helpers' grid driver, dynamics end and
   field tail; those helper headers stay read-only (FR-070).
4. The bars the roadmap leaves to this spec:
   - the loudness-dip bound (SC-008(c));
   - the Lock floor and its tolerance (SC-008(d));
   - the click bound at discrete transitions (SC-004);
   - the monotonicity tolerance for the five axes (SC-008(a)).
5. Tests for:
   - identity, bounded change, click-free transitions, the Anchor curves, idempotence and serialisation;
   - the five-axis trajectory, audibility, mono compatibility and anti-aliasing during a morph;
   - RT safety, non-finite input, determinism, partition invariance, sample rates and CPU.

## Non-Goals (owned by later phases, or deliberately excluded)

| Excluded | Owner | Roadmap |
|---|---|---|
| Wiring the engine into `ProfundumVoice` / `ProfundumEngine`: calling `advanceInterval` per control interval, feeding each voice, and applying the Mass law to the resolved base | Phase 9 | 611–637 |
| The Motion section itself (ENV / CYCLE / DRIFT → `VoiceModRouter`, Motion Depth), and Motion modulating a resolved field. This phase only carries the section's *configuration* as morphable data (OQ-2) | Phase 9 | 629–632 |
| The amp-envelope class (`ADSREnvelope` vs `MultiStageEnvelope`). This phase only carries the envelope's morphable fields (OQ-2) | Phase 9 | 621–622 |
| Parameter IDs (100–199 Morph position, 1000–1099 morph control), the Edit A / Edit B target, normalisation and plugin state | Phases 10, 11 | 679–682, 697–707 |
| Copy A → B / B → A and the A / A·B / B compare. These are controller-side operations on the two snapshots, not engine API (line 702) | Phase 11 | 702–703 |
| The Morph component UI: endpoint names and descriptors, the position readout, the morph-behaviour labels | Phase 12 | 759–764 |
| A user-selectable global "parameter curve" | Never. Not adopted (line 763) | 763–764 |
| Morphing between more than two states, 2-D morph pads, or preset switching | Never. KDD 4 is two states, "not a preset switcher" (lines 137–144) | — |
| An audio crossfade between a render of A and a render of B | Never. The spectrum comes from the interpolated controls (line 589) | 589–590 |
| Any change to `SpectralShapeRecipe`, `ProfundumCore`, `HarmonicFold`, `ShapeMorph`, `BassCharacterStage`, `DropEnvelope`, `BassStereoField`, `MassLaw`, `MassDynamics` or `AnchorOscillator`. If the plan finds it needs one, the change is append-only and the consumer suites gate it (FR-088) | — | 955–956 |

---

## Existing components (verified this session, 2026-10-19)

Paths are relative to `dsp/include/krate/dsp/` unless stated otherwise. I opened every row this session and quote the
real signatures.

### Morphed components (each one's controls become `MorphState` fields)

| Component | Header | Reused for | Verified signature / fact |
|---|---|---|---|
| `SpectralShapeRecipe::Controls` | `processors/spectral_shape_recipe.h:41–48` | The core block (sine → complex, dark → bright) | Layer 2. "FR-002: exactly six fields. Plain aggregate (Phase 8 interpolates it)." `float depth = 0.5f; // [0, 1]`, `body = 0.5f; // [0, 1]`, `bodyCurvature = 0.5f; // [0, 1]`, `bodyEmphasis = 0.0f; // [-1, 0]`, `edge = 0.5f; // [0, 1]`, `shift = 0.0f; // [-1, +1]`.<br>• `[[nodiscard]] static Controls sanitize(const Controls& c) noexcept` (88): clamps; a non-finite field becomes the `kDefaultControls` field.<br>• Named states `kDefaultControls`, `kSineAnchor`, `kTriangleAnchor`, `kSawAnchor`, `kHeavy`, `kHollow`, `kGrowl` (82–85; values 227–240).<br>• It has **no** `isValid…` predicate, so `isValidMorphState` checks these ranges itself. |
| `ProfundumCore` | `systems/profundum_core.h:92` | Receives the resolved recipe and excitation | Layer 3. `kControlInterval = 32` (97).<br>• `void setControls(const SpectralShapeRecipe::Controls& controls) noexcept` (173–193): a non-finite field keeps the pending value; it clamps to the ranges; a bitwise-unchanged set is skipped.<br>• `prepare` sets a per-interval shape-step ceiling, "Δmax = 375·√P0·kControlInterval/fs (0.125594 at 48 kHz)" (142–147). The core therefore slews recipe changes on its own as well.<br>• `struct ExcitationControls { float mix = 0.0f; // [0, 1]  float drive = 0.5f; // [0, 1]  float asymmetry = 0.0f; // [0, 1] }` (334–338).<br>• `void setExcitation(const ExcitationControls& c) noexcept` (356–366). `kExcitationSmoothMs = 5.0f` (348). |
| `HarmonicFold` | `processors/harmonic_fold.h:76` | Fold amount (clean → nonlinear) | Layer 2. `void setAmount(float amount) noexcept` (168–171): "[0, 100]; non-finite keeps the previous value; a denormal becomes 0". `kAmountSmoothMs = 10.0f` (92). |
| `ShapeMorph` | `systems/shape_morph.h:98` | SHAPE (dark → bright) | Layer 3. `void setShape(float position) noexcept` (194–199): [0, 1], raised-cosine 10 ms ramp (`kShapeSmoothMs = 10.0f`, 107; `ShapeRamp shapeSm_`, 616). |
| `BassCharacterStage` | `systems/bass_character_stage.h:76` | Character algorithm, Amount, Tone | Layer 3. `enum class Algorithm : std::uint8_t { Tube, Fold, Rectify, Bite, Crush, Fracture }` (78); `kNumAlgorithms = 6` (80).<br>• `kSwitchCrossfadeMs = 10.0f` (81, D-7), `kParamSmoothMs = 10.0f` (84).<br>• `void setAlgorithm(Algorithm a) noexcept` (241–255). It is the D-7 / C-21 switch machine: while a switch is in flight, the last request is queued (`pending_`). Only one crossfade runs at a time.<br>• `void setAmount(float a) noexcept` (258) and `void setTone(float t) noexcept` (265) take [0, 1]; Tone 0.5 is neutral.<br>• Amount 0 bypasses the stage (header comment, line 13: "Amount-0 bypass (FR-022, FR-054)"). |
| `DropEnvelope` | `processors/drop_envelope.h:29` | Drop (short → long) | Layer 2. `kMaxAmountSemitones = 48.0f`, `kMinTimeMs = 1.0f`, `kMaxTimeMs = 4000.0f` (31–33). Defaults: Amount 0, Time 100 ms, Shape 0, Rebound 0 (36–39).<br>• `void setAmount(float semitones) noexcept`, `setTime(float ms)`, `setShape(float s)`, `setRebound(float r)` (83–98).<br>• "Setter rule … non-finite keeps the previous value … Latched at the next trigger (D-4)" (80–81). |
| `ADSREnvelope` | `primitives/adsr_envelope.h:54` | Amp-envelope field ranges (the class itself is Phase 9's choice) | Layer 1. `setAttack(float ms)`, `setDecay(float ms)`, `setSustain(float level)`, `setRelease(float ms)` (115–136). Times clamp to [`kMinEnvelopeTimeMs = 0.1f`, `kMaxEnvelopeTimeMs = 10000.0f`] (`primitives/envelope_utils.h:51–52`); sustain clamps to [0, 1].<br>• `void setAttackCurve(float amount) noexcept` (160) and its decay / release twins take [−1, +1] ("0=linear, -1=logarithmic, +1=exponential", 157–158). **Each call regenerates a 256-entry table** (`generatePowerCurveTable`, `core/curve_table.h:40, 65–74`). Phase 9 must not call it every interval while the value is unchanged (FR-083 handoff). |
| `MultiStageEnvelope` | `processors/multi_stage_envelope.h:61` | The other amp-envelope candidate (Phase 9) | Layer 2. `kMinStages = 4`, `kMaxStages = 8`, `kMaxStageTimeMs = 10000.0f` (63–65). `setStage(int stage, float level, float ms, EnvCurve curve)` (166), `setSustainPoint(int)` (178), `setReleaseTime(float ms)` (206). It can realise the ADSR field set below with four stages (OQ-2). |
| `BassStereoField::Controls` | `systems/bass_stereo_field.h:99–111` | Image mode / Spread / Gravity / Motion (mono → stereo) | Layer 3. `ImageMode mode = ImageMode::Spread; float spread = 0.5f; float gravity = 0.5f; float crossoverHz = kDefaultCrossoverHz; // [60, 250]  float curve = 0.5f; float motionDepth = 0.0f; float motionRate = 0.3f; // [0, 1] (C-9)  MotionSource motionSource = MotionSource::Drift; bool cycleSync = false; NoteValue cycleNote = NoteValue::Whole; NoteModifier cycleModifier = NoteModifier::None;`<br>• `enum class ImageMode : std::uint8_t { Solid, Spread, Wide }` and `enum class MotionSource : std::uint8_t { Drift, Cycle }` (63–64). `kMinCrossoverHz = 60`, `kMaxCrossoverHz = 250`, `kDefaultCrossoverHz = 120` (70–72). `kModeCrossfadeMs = 10.0f` (91).<br>• `[[nodiscard]] static bool isValidControls(const Controls& c) noexcept` (263–270): "true iff every float is finite and inside its closed range and every enum is declared. The ranges are boxes, so a linear interpolation of two valid sets is valid".<br>• `void setControls(const Controls& c) noexcept` (234) and `void advanceControl() noexcept` (293). |
| `MassLaw` | `systems/mass_law.h:64` | Mass, and the base the resolved fields form | Layer 3. `kMinMass = 0`, `kMaxMass = 100`, `kNeutralMass = 50` (72–74); `kControlInterval = 32` (75); `kMassSweepMs = 40.0f` (77).<br>• `struct Targets { float depth; float body; float transientDurationMs; float foldAmount; float compressionAmount; float anchorLevel; float outputTrimDb; }` (82–90). It "Doubles as the base (the user's own values = Mass 50, D-1)".<br>• `[[nodiscard]] static Targets evaluate(float mass, const Targets& base) noexcept` (142–148) is pure.<br>• `void setMass(float)` (163), `void setBase(const Targets&)` (170), `float advanceInterval() noexcept` (180). The rows bias Depth, Body, Fold amount and **Anchor level** (104–113). The last is why FR-054 exists. |
| `MassDynamics` / `TransientShaper` | `systems/mass_dynamics.h:469`; `processors/transient_shaper.h:75` | Transient attack / sustain gains (Phase 6 handoff 7) | `void setTransientAttackDb(float dB) noexcept` and `setTransientSustainDb(float dB)` (mass_dynamics.h:530–531). The range is [`kMinGainDb = -12`, `kMaxGainDb = 12`] and the defaults are +6 / 0 dB (transient_shaper.h:78–83). |
| `AnchorOscillator::Controls` | `systems/anchor_oscillator.h:99–106` | The Anchor block | Layer 3. `Waveform waveform = Waveform::Sine; Mode mode = Mode::Lock; float level = kDefaultLevel; // [0, 1]  float punchBoostDb = 6.0f; // [0, 12]  float punchTimeMs = 40.0f; // [5, 250]  float ghostTimeMs = 15.0f; // [1, 100]`.<br>• `enum class Waveform : std::uint8_t { Sine, Triangle, RoundedSine, AsymmetricSine, OctaveDownSine }` and `enum class Mode : std::uint8_t { Lock, Follow, Punch, Ghost }` (57–58).<br>• `kWaveformCrossfadeMs = 5.0f` (76, FR-014), `kModeBridgeMs = 5.0f` (79), `kLevelSmoothMs = 5.0f` (75).<br>• `void setControls(const Controls& controls) noexcept` (224–245): an undeclared enum keeps the pending value; floats are sanitised. |

### Pattern and utility sources

| Component | Header | Reused for | Verified signature / fact |
|---|---|---|---|
| `equalPowerGains` | `core/crossfade_utils.h:50, 64` | **Not composed for fields.** It is an audio-gain pair (cos / sin), not a scalar-parameter law. FR-033 defines the scalar law | `inline void equalPowerGains(float position, float& fadeOut, float& fadeIn) noexcept` (50); `crossfadeIncrement(float durationMs, double sampleRate)` (89). |
| `SlewLimiter`, `LinearRamp`, `OnePoleSmoother` | `primitives/smoother.h:468, 305, 134` | The position ramp (FR-020) | `SlewLimiter::configure(float ratePerMs, float sampleRate) noexcept` (512), `setTarget(float)` (519), `process()` (545), `snapTo(float)` (591).<br>• `setTarget` on a NaN **snaps the current value to 0** (519–524). The engine therefore filters non-finite input before the smoother (FR-013).<br>• `LinearRamp::configure(float rampTimeMs, float sampleRate)` (329) spreads any change over a fixed time (`calculateLinearIncrement`, 100–108), so its per-sample step grows with the jump. A rate limit bounds the step regardless of jump size, which SC-003 needs. `MassLaw` uses the same rate limit (mass_law.h:157). |
| `SpectralMorphEngine` / `SpectralState` | `systems/spectral_morph_engine.h:90`; `processors/spectral_state.h:44` | Pattern only: validated endpoint storage and versioned serialisation | `void setState(int slot, const SpectralState& s) noexcept` (299) "Returns immediately, writing NOTHING, if … `!isValidSpectralState(s)`" (285–288); "Assigning an IDENTICAL state is a no-op".<br>• `serializeSpectralState(const SpectralState&, std::byte* dest, std::size_t capacity) noexcept` (238) and `deserializeSpectralState(const std::byte* src, std::size_t size, SpectralState& out) noexcept` (274): fixed offsets, a version byte at offset 0, `std::memcpy` of bit patterns, `static_assert(std::endian::native == std::endian::little, …)` (211–212). The round trip "is EXACT because the bytes are stored values" (270).<br>• Its helper namespace `detail::state_io` (188) is taken, so the new one is `detail::morph_state_io`. |
| Disrumpo `MorphEngine` | `plugins/disrumpo/src/dsp/morph_engine.h:45` | **Not reused. ODR hazard.** Pattern only: the family-crossfade idea (header lines 4–7: "same-family parameter interpolation and cross-family parallel processing with equal-power crossfade") | Plugin-local, namespace `Disrumpo`. It is an audio-domain processor of 2–4 distortion nodes. Phase 5 FR-057 rules out the parallel-algorithm form here: "No `setAlgorithmBlend`", at most two algorithms at once. |
| `harmonic_frame_utils.h` | `processors/harmonic_frame_utils.h:38, 136` | **Not composed.** `lerpHarmonicFrame` / `lerpResidualFrame` interpolate analysis frames, not controls | — |
| `LFO`, `NoteValue`, `BrownianDrift` | `primitives/lfo.h:63`; `core/note_value.h:38, 54`; `processors/brownian_drift.h:94` | Ranges for the Motion-section configuration fields (OQ-2) | `LFO`: `kMinFrequency = 0.01f`, `kMaxFrequency = 20.0f` (34–35); a namespace-scope `enum class Waveform` (47), so the new headers must not declare a namespace-scope `Waveform`. `setTempoSync(bool)` (237), `setNoteValue(NoteValue, NoteModifier)` (255), `setFadeInTime(float ms)` (295).<br>• `BrownianDrift`: `kTauMin = 0.2f`, `kTauMax = 30.0f` (97–99), `setSmoothness(float normalized)` (152), `setDepth(float)` (159). |
| `detail::isFinite` | `core/db_utils.h:118` | Non-finite guards that survive `-ffast-math` | Same use as `ProfundumCore` and `BassStereoField`. |

**Test helpers (verified).**
- `dsp/tests/unit/systems/mass_dynamics_test_helpers.h`: `kGrid` (81), `gridDriver` (103), `ChainConfig` (173),
  `renderChain` (372), `renderPoly` (454), `SubBand` / `subBand` (545, 641), `PeriodDescriptors` (719), `aliasResidualDb`
  (865), `unbiasedH1Amplitude` (288).
- `dsp/tests/unit/systems/bass_stereo_field_test_helpers.h`: `FieldChainConfig` (114), `renderFieldChain` (267),
  `renderFieldPoly` (463), the SC-001 / SC-002 bars `kSubBandQBarDb = 1.0` and `kSubBandRhoBar = 0.99` (95–97),
  `bitEqual` (102).
- `tests/test_helpers/k_weighting.h`: `kWeightedLevelDb` (111). `tests/test_helpers/render_fingerprint.h`:
  `kSampleTolerance`, `kMetricTolerance` (58–61). `tests/test_helpers/allocation_detector.h` (`AllocationScope`).
- **Registration.** `dsp/tests/CMakeLists.txt` lists the systems TUs explicitly, with the Mass law and stereo-field TUs
  at 721–749, and keeps a `-fno-fast-math` block for non-finite TUs. The Phase 8 TUs register in `dsp_systems_tests`
  and carry the `[profundum]` tag. The non-finite TU goes in the `-fno-fast-math` block.

### Reuse-inventory corrections (found while verifying)

- **Most of `MorphState` already exists as component aggregates.** Phases 1, 2, 3 and 7 each built a plain `Controls`
  struct "so Phase 8 can interpolate it". `MorphState` composes those four structs by value. It does not restate their
  fields.
- **Only `BassStereoField` has a static validity predicate.** The recipe has `sanitize`, the excitation and the Anchor
  have private sanitisers, and the scalar controls have only clamping setters. `isValidMorphState` therefore restates
  the ranges for every field except the field block, and cites each range's source constant (FR-005).
- **Phase 7's handoff and the roadmap disagree on the crossover law.** Phase 7's evidence file says crossoverHz
  "interpolate[s] linearly". Its spec (FR-080) says only "continuous". The roadmap rules "logarithmic (frequencies,
  times)" (line 587). This spec follows the roadmap: `crossoverHz` is logarithmic (FR-032, D-11). The field's box stays
  valid under a log law, because the law maps into [min, max] of the endpoints (FR-036).
- **`MassLaw` biases the Anchor level.** Its `AnchorLevel` row (mass_law.h:110) moves the Anchor level with Mass. If
  Mass differs between A and B, the Anchor would move in **Lock** unless the Anchor's Mass input follows the Anchor's
  own position. FR-054 closes that gap.

---

## New components

ODR sweep, run 2026-10-19: `grep -rnE "(class|struct|enum class|enum|using) <Name>\b|\b<Name>\(" dsp/ plugins/ tests/
tools/` (`.h`, `.cpp`, `.hpp`), `grep -rnw <name>` for the free functions and constants, and `find dsp plugins tests
tools -iname "*morph*"`.

| Class / type | Layer | Header | ODR sweep result |
|---|---|---|---|
| `MorphState` (struct) + free functions `isValidMorphState`, `serializeMorphState`, `deserializeMorphState`; constants `kMorphStateFormatVersion`, `kMorphStateBytes`; helper namespace `detail::morph_state_io` | 3 | `systems/morph_state.h` | **0 hits** for each name. No `morph_state.h` exists. Nearest: Seraphis's parameter IDs `kMorphState0Id` … `kMorphState3Id` and `kMorphStateCountId` (`plugins/seraphis/src/parameters/morph_params.h`). These are different identifiers in the plugin namespace, with no collision. |
| `AmpEnvelopeFields`, `MotionConfig` (nested in `MorphState`; OQ-2) | 3 | `systems/morph_state.h` | 0 hits for `AmpEnvelopeFields`, `AmpEnvelopeControls`, `AmpEnvelopeState`, `MotionConfig` and `MotionSectionConfig`. They are nested, so they add no namespace-scope name. |
| `BassMorphEngine`, with nested `Endpoint`, `AnchorMorphMode`, `Law`, `Axis` and `Resolved` | 3 | `systems/bass_morph_engine.h` | **0 hits** for `BassMorphEngine`, `AnchorMorphMode`, `MorphLaw`, `MorphAxis`, `MorphFieldLaw`, `MorphFieldId`, `MorphEndpoint`, `MorphPosition`, `MorphResolver`, `ResolvedMorph` and `ResolvedState`. No `bass_morph*` file exists.<br>**Taken near names, none reused:**<br>• `MorphEngine` (`plugins/disrumpo/src/dsp/morph_engine.h:45`);<br>• `MorphNode` (`plugins/disrumpo/src/dsp/morph_node.h:26`);<br>• `MorphMode` (`plugins/disrumpo/src/dsp/distortion_types.h:377`);<br>• `SpectralState` (`processors/spectral_state.h:44`);<br>• `SpectralMorphEngine` (`systems/spectral_morph_engine.h:90`).<br>Nested types stay inside `BassMorphEngine`. If the plan needs a namespace-scope helper, it sweeps that name first and records the result here. |
| Constants `kMorphRuntimeBytes`, `kMorphRuntimeFormatVersion`, `kMorphLeafFieldCount`; methods `serializeRuntime`, `restoreRuntime` (FR-030, FR-062) | 3 | `systems/morph_state.h`, `systems/bass_morph_engine.h` | **0 hits** each (`grep -rnw`, dsp / plugins / tests / tools, 2026-10-19, review revision). |
| Test-only: namespace `BassMorphTest`, `renderMorphChain`, `renderMorphPoly`, `MorphChainConfig`, `bitEqual(const MorphState&, …)` (FR-005a, FR-070) | test | `dsp/tests/unit/systems/bass_morph_test_helpers.h` | **0 hits** for the first four. `bitEqual` already exists as functions in other test namespaces (`StereoFieldTest::bitEqual`, bass_stereo_field_test_helpers.h:102). The new overloads live in `BassMorphTest` and take `MorphState` / `Resolved`, so there is no collision; calls are qualified. |

**Layer discipline.**
- `morph_state.h` includes L2 (`spectral_shape_recipe.h`, `drop_envelope.h`, `transient_shaper.h`) and L3 peers
  (`profundum_core.h`, `bass_character_stage.h`, `bass_stereo_field.h`, `anchor_oscillator.h`, `mass_law.h`), but
  only for their aggregates, enums and range constants. It never includes an L4 header.
- `bass_morph_engine.h` includes `morph_state.h`, L0 (`db_utils.h`) and L1 (`smoother.h`).
- Neither header calls a component's processing method. The engine returns values; the voice (Phase 9) or the test
  fixture passes them to the components' setters.

---

## Functional Requirements

### Descriptor and fixture definitions (used by FRs and SCs)

- **p** is the main morph position in [0, 1]. **p_a** is the Anchor position (FR-051). Both are the ramped values
  (FR-020), not the targets.
- **θ_up / θ_down** are the step thresholds for discrete fields (FR-034; D-1 ruled 2026-10-19, Q1: θ_up = 0.52,
  θ_down = 0.48, applied only when neither endpoint is silent on that field — see the FR-034 governing-amount rule).
- **Showcase pair "pure weight → harmonic monster"** (SC-008, SC-015, SC-016; A fixed; **B harvested** per B7-2, ruled
  2026-10-19: the drafted B below was red on the f₀ Lock floor at E1 p = 1 (−1.16 dB) with its Anchor at −47 dB, so B's
  recipe, Fold, SHAPE, Tone, Character Amount and Anchor level are chosen by the main-loop harvest
  (evidence/harvest/showcase-*.txt, aggregate in evidence/showcase-harvest-aggregate.txt) as the candidate with the
  largest worst-case floor margin over 11 positions × {E1, C3} × {Lock, Blend} that also meets the SC-015 travel
  bars and Anchor clause; the harvested values are pasted into `showcasePair()` and listed in the Clarifications):
  - **A:**
    - recipe `kSineAnchor`; excitation mix 0; Fold 0; SHAPE 0;
    - Character Tube, Amount 0, Tone 0.5;
    - Drop Amount 0;
    - amp envelope A 1 ms / D 150 ms / S 0 / R 50 ms;
    - field SOLID, Spread 0;
    - Motion Off; **Mass 50**;
    - Anchor Sine / Lock mode / level 1.0, anchorFoldFeed 0.
  - **B:**
    - recipe `kHeavy` with edge 1; excitation mix 1, drive 1, asymmetry 0.5; Fold 80; SHAPE 0.5;
    - Character Fold, Amount 0.5, Tone 0.8;
    - Drop Amount 0;
    - amp envelope A 1 ms / D 2000 ms / S 0.8 / R 800 ms;
    - field WIDE, Spread 1;
    - Motion Off; **Mass 50**;
    - Anchor Sine / Lock mode / level 0.5.

  (Harvested values, B7-2, run 3 candidate 59 of 96: worst floor margin 5.89 dB Lock / 6.91 dB Blend over 11 positions ×
  {E1, C3}; Anchor clause +0.68 dB at both notes; D_cx travel 52 dB, D_nl 14 dB, D_st 119 dB, D_br 0.63 / 0.56 oct;
  Anchor at p = 1 at −9.9 dB. The floor-first leader, candidate 63 (Tone 0.5), was passed over by the main loop because
  its D_br travel cleared the 0.25 oct bar by 0.04 oct and its Anchor clause by 0.2 dB; 59 gives up 1 dB of floor for
  3.5× and 8× those margins. The drafted B — `kGrowl` edge 1, SHAPE 1, Amount 0.8, level 0.7 — is kept in
  evidence/harvest/run1–run3 as candidate 0.)

  The pair moves every one of the five axes in the same direction, so each axis descriptor has a defined monotone
  sense. **Mass is held at the neutral 50 at both ends** (review revision). At Mass 50 every `MassLaw` row returns its
  base unchanged (`evaluateRow`, mass_law.h:136–142: u = (mass − 50)·0.02 = 0, so s = 0), so the effective Anchor
  level is the resolved `anchor.level` itself (1.0 → 0.5), the core receives the resolved recipe unbiased (SC-009(b)),
  and the compressor and output trim do not move along the morph. A Mass 0 → 100 showcase would make the effective
  Anchor level *rise* with p_a through the AnchorLevel row (mass_law.h:110: amountDown 0.5, amountUp 0.5, Linear;
  1.0 − 0.5 = 0.5 at p_a = 0 up to clamp(0.5 + 0.5) = 1.0 at p_a = 1), which inverts the sign of every Anchor-mode
  comparison. Mass's own morph is gated on the separate **Mass-only pair** (SC-005(e)): A and B equal to the
  default `MorphState` except A Mass 0 and B Mass 100.
- **Two render kinds.** Every descriptor names the render it reads; the two kinds never mix.
  - **Steady render** (D_cx, D_nl, D_st, D_br, L_K, E_sub, F_lock): the FR-070 fixture with the amp envelope
    **bypassed** (unity gain, `ampEnvelope = false`), so both endpoints sound at every p, including A, whose sustain
    is 0. The position is snapped (`reset()` at the fixed p). Settle `MassDynamicsTest::kSettleSeconds` = 1 s
    (mass_dynamics_test_helpers.h:87); the read window is the following `TestUtils::kLowFrequencyFftSize` samples
    (`descriptorSeconds`, mass_dynamics_test_helpers.h:90–92), trimmed to whole periods of f₀. The amp envelope is the
    short → long axis and is read only by D_t.
  - **Gate render** (D_t): the fixture with the amp envelope **on**; note-on at sample 0, note-off at T_off = 1.0 s,
    rendered until the output's one-period RMS has fallen ≥ 80 dB below its peak. The render length is extended (in
    1 s steps, up to 12 s) until that holds; a position that never reaches it is a **failure**, not a capped reading,
    so no measured position sits at the cap.
- **Floors.** D_cx and D_nl floor each of their two harmonic powers at **−60 dB below the reference tap's fundamental
  power** w₁ (D_cx: the tap's own; D_nl: the linear core's) before the division, so a descriptor is defined at every p
  (for example on A's pure sine). D_st keeps Phase 7's own floors. Reading refinement by the main loop, 2026-10-19
  (harvest run 2, evidence/harvest/run2/): with the drafted absolute −120 dB floor, A's character tap — a ≈ −100 dBFS
  n ≥ 9 residue of the ROUND stage — read as +11.7 dB of D_nl against a numerically silent linear core, and no B could
  travel the 3 dB bar; the same class of error as Phase 7's G8-1 (−100 dBFS validity read float residue), whose −60 dB
  floor is reused here, relatively, so the descriptors stay scale-free. A floored reading is reported as such.
- **Axis descriptors.** Each is computed offline in double precision over whole periods of f₀, on the render named.
  - **D_cx (sine → complex; steady):** the ratio, in dB, of the core tap's power in harmonics n ≥ 2 to its power in
    h1.
  - **D_nl (clean → nonlinear; steady):** the ratio, in dB, of the n ≥ 9 harmonic power at the character tap to the
    same power at the **linear core**: a twin `ProfundumCore` in the fixture with the same recipe, f₀, pan vector and
    note-on but the edge excitation at mix 0 (`linearCoreL/R` taps). Reading refinement by the main loop, 2026-10-19
    (evidence/showcase-harvest-aggregate.txt, first run): the core tap already carries the Phase 2 excitation
    nonlinearity, so against it the post-core stages read as *less* nonlinear (D_nl travel −0.6 … −10 dB) on every
    one of the 96 harvest candidates, and the "clean → nonlinear" axis could never travel. Against the linear core,
    D_nl counts every nonlinear stage the morph drives (excitation, Fold, SHAPE, Character). The bar is unchanged.
  - **D_st (mono → stereo; steady):** the side-to-mid energy ratio, in dB, above 2·f_c. **For the SC-008(a) monotone
    read it is taken on the clean core tap** (the pan-only width law, Phase 7 SC-003's gated reading; ruled 2026-10-19,
    B10-4) and the output reading is recorded beside it: on the nonlinear WIDE chain the output's side already equals
    its mid (readings around 0 dB) and wobbles ±0.7 dB with position through the cross products Phase 7 ruled
    non-monotone (G8-1 / G10-1) — measured already at the voice-sum tap with the compressor at 0 dB (evidence/run-log/
    close/trajectory-tap-probe.log). The core-tap read carries Phase 7's band validity (G8-1, −60 dBFS): a position
    whose core band above 2·f_c is below it (the near-sine start of the showcase, E1 p ≤ 0.1, where the reading was
    float residue at −19.5 / −23.8 dB) is recorded as invalid and skipped in the SC-008(a) chain (reading refinement,
    2026-10-19). SC-008(b)'s travel and SC-015's travel bar read the output as before.
  - **D_t (short → long; gate):** the envelope's **effective duration** in ms, D_t = Σ_k e_k²·P_k / e_peak², where e_k
    is the one-period RMS of the **pre-dynamics voice-sum** mid signal (L+R)/2 over whole period k, P_k its length in
    ms, and e_peak the largest e_k (ruled 2026-10-19, B10-4: read before `MassDynamics` and the true-peak limiter, like
    E_sub, because the limiter engages from p ≈ 0.8 on the showcase pair — the output falls 2.6 dB from p = 0.75 to 1
    while the sum tap holds level — and shortened the output's D_t by 2–9 % near p = 1; the output reading is
    recorded). It integrates attack, decay, sustain over the fixed 1 s gate, and release, so it stays finite and
    moves across the whole range: no −20 dB crossing is required, and a non-zero sustain cannot saturate it at the
    hold length.
  - **D_br (dark → bright; steady):** the spectral centroid of the output, in octaves above f₀.
- **E_sub(p)** (steady) is the sub-band level of the **pre-dynamics sum tap** (`sumL/R`, the voice sum before
  `MassDynamics`; mass_dynamics_test_helpers.h:307–308; the field chain's copy, processFieldTail, bass_stereo_field_test_helpers.h:232), on its mid signal (L+R)/2: the mean, over the read window, of
  Phase 6's `subBand(...).dftDb` trace (the one-period single-bin DFT at f₀, mass_dynamics_test_helpers.h:546, 687)
  — **the f₀ component, not the 1.5·f₀ low-pass RMS** (`lpRmsDb`). Reading refinement by the main loop, 2026-10-19
  (evidence/coherence-probe.txt, evidence/showcase-harvest.txt): at the monster end h1 is ≈ 35 dB down and the
  4th-order low-pass skirt passes h2, so `lpRmsDb` read 3–5 dB above the fundamental and turned a −1.16 dB Lock-floor
  red at E1 p = 1 into a false +1.9 dB pass; DC was ruled out (≤ −100 dB). The `lpRmsDb` reading is recorded beside
  it. The tap sits before the Mass-driven compressor, output trim, DC blockers
  and limiter (processChainTail, mass_dynamics_test_helpers.h:302–336), so the floor compares quantities on the same
  side of every morph-driven gain stage. The output-tap reading E_sub,out(p) is recorded alongside, not gated.
- **Lock floor F_lock(p)** (steady) is E_sub, read the same way at the same sum tap, of the **Anchor tap alone** in a
  Lock-mode render at the same p. The Anchor's a1 is the resolved recipe's unbiased h1 per interval (FR-070), so the
  Anchor's amplitude follows the morphing recipe even while Lock holds its controls; F_lock is therefore a per-position
  quantity, not one number. It is the weight Lock holds at that p.
- **L_K(p)** (steady) is the integrated K-weighted level of the output (`kWeightedLevelDb`) over the read window.
- **Notes:** E0, E1, E2 and C3, at 44.1, 48 and 96 kHz, as in Phases 2–7. Grids that run longer than about 15 s are
  tagged `[long]`.

### FR-001 series — `MorphState` (roadmap lines 581–584)

- **FR-001 Type.** `struct MorphState`, Layer 3, in `systems/morph_state.h`, header-only, `Krate::DSP`. It is a plain
  aggregate with designated-initialiser defaults, no pointers, no virtuals and no user-declared constructors.
  `static_assert(std::is_trivially_copyable_v<MorphState>)` holds, so copies are RT-safe `memcpy`-class operations.
  The banner states the layer and cites this spec.
- **FR-002 Field set.** Every sound-shaping control the roadmap lists (lines 581–584 and KDD 4, lines 138–139), grouped
  in this order. The sub-structs are composed by value.

  | Block | Field(s) | Type / range | Source of the range |
  |---|---|---|---|
  | Core | `recipe` | `SpectralShapeRecipe::Controls` (6 floats) | spectral_shape_recipe.h:42–47 |
  | Edge excitation | `excitation` | `ProfundumCore::ExcitationControls` {mix, drive, asymmetry} ∈ [0, 1] | profundum_core.h:334–338 |
  | Fold | `foldAmount` | float [0, 100] | harmonic_fold.h:166–168 |
  | Shape | `shape` | float [0, 1] | shape_morph.h:193–194 |
  | Character | `characterAlgorithm`, `characterAmount`, `characterTone` | `BassCharacterStage::Algorithm`; float [0, 1]; float [0, 1] | bass_character_stage.h:78, 257–265 |
  | Drop | `dropAmountSt`, `dropTimeMs`, `dropShape`, `dropRebound` | [0, 48]; [1, 4000]; [0, 1]; [0, 1] | drop_envelope.h:31–33, 83–98 |
  | Amp envelope | `ampEnv` (`AmpEnvelopeFields`: `attackMs`, `decayMs`, `sustain`, `releaseMs`, `attackCurve`, `decayCurve`, `releaseCurve`) | times [0.1, 10000] ms; sustain [0, 1]; curves [−1, +1] | envelope_utils.h:51–52; adsr_envelope.h:127–129, 157–175 (field set fixed in this phase: Q4, ruled 2026-10-19) |
  | Stereo | `field` | `BassStereoField::Controls` | bass_stereo_field.h:99–111 |
  | Motion section | `motion` (`MotionConfig`) | the MotionConfig table below (fixed in this phase: Q4, ruled 2026-10-19) | lfo.h:34–35, 47–54, 226–233, 289–298; note_value.h:38, 54; brownian_drift.h:97–99, 152; envelope_utils.h:51–52 |
  | Mass | `mass`, `transientAttackDb`, `transientSustainDb` | [0, 100]; [−12, 12]; [−12, 12] | mass_law.h:72–73; transient_shaper.h:78–79 |
  | Anchor | `anchor`, `anchorFoldFeed` | `AnchorOscillator::Controls`; float [0, 1] | anchor_oscillator.h:99–106; roadmap line 323 |

  **`MotionConfig` field set** (body requirement; **ruled 2026-10-19, Q4:** the amp-envelope and Motion field sets are fixed
  now, before Phase 9 chooses the envelope class or builds the Motion component; Phase 9 may only append, under a
  format-version bump, FR-007). It covers every Motion control the
  roadmap names: the section's sources, target, amount and depth (lines 629–632, 704–705) and the Phase 12 amendment's
  "source, target, amount, rate, shape, sync + division; phase, retrigger, fade-in and polarity" (lines 765–767). Every
  one is sound-shaping (it changes what the modulation does to the sound), so every one is a field. Nested enums
  (`Source`, `Target`, `Polarity`) stay inside `MotionConfig`; the CYCLE shape reuses LFO's namespace-scope `Waveform`
  (lfo.h:47) by name and declares no new one.

  | Field | Type / range | Default | Law (FR-030) | Source of the range |
  |---|---|---|---|---|
  | `source` | `Source : std::uint8_t { Off, Env, Cycle, Drift }` | Off | Stepped | roadmap lines 629–630, 765 |
  | `target` | `Target : std::uint8_t { Mass, Depth, Body, Edge, Shift }` | Mass | Stepped | roadmap lines 632, 766 ("Mass and the core spectral controls first") |
  | `amount` | float [−1, 1] | 0 | Linear | roadmap line 766 |
  | `depth` | float [0, 1] (the global Motion Depth) | 1 | Linear | roadmap line 631 |
  | `cycleRateHz` | float [0.01, 20] | 1 | Logarithmic | lfo.h:34–35 |
  | `cycleShape` | `Waveform` (Sine … SmoothRandom) | Sine | Stepped | lfo.h:47–54 |
  | `cycleSync` | bool | false | Stepped | lfo.h:237 |
  | `cycleNote`, `cycleModifier` | `NoteValue`, `NoteModifier` | Quarter, None | Stepped | note_value.h:38, 54; lfo.h:255 |
  | `cyclePhaseDeg` | float [0, 360) | 0 | Linear | lfo.h:226–233 (wraps to [0, 360)) |
  | `cycleRetrigger` | bool | true | Stepped | lfo.h:289 |
  | `cycleFadeInMs` | float [0, 5000] (0 = no fade-in; the upper bound is drafted, LFO clamps only below, lfo.h:295–298) | 0 | Logarithmic on (t + 1 ms) (FR-032) | lfo.h:295–298 |
  | `polarity` | `Polarity : std::uint8_t { Bipolar, Unipolar }` | Bipolar | Stepped | roadmap line 767 |
  | `driftSmoothness` | float [0, 1] | 0.5 | Linear | brownian_drift.h:152 |
  | `envAttackMs`, `envDecayMs` | float [0.1, 10000] | 10, 300 | Logarithmic | envelope_utils.h:51–52 |

  The defaults are drafted; Phase 9 confirms them against its Motion component and may only **append** fields under a
  version bump (FR-007). A field Phase 9 finds it cannot realise (for example a float curve amount under
  `MultiStageEnvelope`'s `EnvCurve` enum) is recorded there as a deviation, never silently ignored (Q4 ruling; FR-083
  handoff). SC-019 covers the whole state from this phase on.

- **FR-003 Exclusions.** These are **not** fields (roadmap line 584: "Excluded: note behaviour, output level,
  polyphony"):
  - **Note behaviour:** retrigger phase policy, Note Memory, glide mode and time, mono priority, legato.
  - **Output level:** output gain, the limiter ceiling, and the Mass law's `outputTrimDb`. The trim is a calibrated law
    target, not a user control (Phase 6 G8-5).
  - **Polyphony:** MONO / POLY and the voice count.
  - **Derived or fixed values, and why:**
    - the WIDE per-channel offsets, which the field derives (`wideOffsets`, Phase 7 handoff);
    - the `MassLaw::Targets` bases `transientDurationMs` and `compressionAmount`, which are Mass-driven (Phase 6
      handoff 7, roadmap line 491);
    - the partial count, a prepare-time constant;
    - the Low Note Guard, a fixed property of the recipe (Phase 1 FR-030);
    - the field's drift seed, which belongs to determinism, not to the sound.

  A test enumerates the `MorphState` members and asserts that none of these is present (SC-019).
- **FR-004 Defaults.** `MorphState`'s member defaults equal each component's own defaults:
  - `SpectralShapeRecipe::kDefaultControls`;
  - `ProfundumCore::kDefaultExcitation` (mix 0);
  - Fold 0, SHAPE as `ShapeMorph`'s reset value (the plan reads and cites it);
  - Character Tube / 0 / 0.5;
  - the `DropEnvelope` defaults (Amount 0, Time 100 ms);
  - the `BassStereoField::Controls` and `AnchorOscillator::Controls` defaults;
  - Mass 50, transient gains +6 / 0 dB, `anchorFoldFeed` 0.

  A default-constructed `MorphState` passes `isValidMorphState`.
- **FR-005 Validation.** `[[nodiscard]] bool isValidMorphState(const MorphState& s) noexcept`.
  - It returns true **iff** every float is finite (`detail::isFinite`) and inside its closed range from FR-002, and
    every enum holds a declared enumerator. The field block uses `BassStereoField::isValidControls`.
  - Every range is a closed box, except `motion.cyclePhaseDeg`, whose box is [0, 360) (LFO wraps 360 to 0,
    lfo.h:226–233); an interpolation clamped to [min, max] of two values in [0, 360) stays in it. Every log-law field
    has a strictly positive lower bound, or a declared offset that makes the logged quantity positive (FR-032). A
    law-interpolated state of two valid states is therefore valid (FR-036).
- **FR-005a Bit equality.** "Bitwise equal" for a `MorphState` (and for `Resolved`) means **field-wise** bit equality,
  never `memcmp` of the struct: `MorphState` has padding (for example `BassStereoField::Controls` places
  `ImageMode mode` (uint8) before `float spread`, bass_stereo_field.h:99–101, and `AnchorOscillator::Controls` starts
  with two uint8 enums before its floats, anchor_oscillator.h:99–101), and padding bytes are indeterminate after a
  field-by-field resolve or deserialise. The definition: every float compares equal under `std::bit_cast<std::uint32_t>`,
  every enum and bool by value; for `Resolved`, also the three floats by `std::bit_cast`. Equivalently, two valid
  states are bitwise equal iff their `serializeMorphState` bytes are equal (the layout has no padding). The test helper
  is `BassMorphTest::bitEqual(const MorphState&, const MorphState&)` and its `Resolved` overload, in
  `bass_morph_test_helpers.h`; it is implemented as the serialised-bytes comparison, plus `std::bit_cast` of the three
  `Resolved` floats. Every "bitwise equal" in this spec, including FR-012's no-op rule, uses it.
- **FR-006 Serialisation.** The pair follows `spectral_state.h:178–300`:
  - `[[nodiscard]] std::size_t serializeMorphState(const MorphState& s, std::byte* dest, std::size_t capacity) noexcept`;
  - `[[nodiscard]] bool deserializeMorphState(const std::byte* src, std::size_t size, MorphState& out) noexcept`.

  The layout and behaviour:
  - a version byte `kMorphStateFormatVersion = 1` at offset 0;
  - fixed named offsets in `detail::morph_state_io`;
  - each scalar moves as a `std::memcpy` of its bit pattern; enums and `bool` move as one `std::uint8_t`;
  - `static_assert(std::endian::native == std::endian::little)`;
  - the exact size is `kMorphStateBytes`.

  Serialise returns `kMorphStateBytes`, or returns 0 and writes nothing if `dest` is null, `capacity` is short, or the
  state is invalid. Deserialise returns false and leaves `out` untouched if `src` is null, `size` is short, the
  version is unknown, or the decoded state fails `isValidMorphState`. The round trip is bit-exact. Both functions
  are RT-safe.
- **FR-007 Append-only evolution.** Fields are only ever appended, and each layout change bumps the version. From the
  first version bump onwards, `deserializeMorphState` reads every earlier version and fills the appended fields with
  their FR-004 defaults. In this phase only version 1 exists. Phase 11's `kCurrentStateVersion` wraps these bytes
  and does not reinterpret them.

### FR-010 series — `BassMorphEngine` interface and lifecycle (roadmap lines 585–600)

- **FR-010 Class.** `class BassMorphEngine`, Layer 3, `systems/bass_morph_engine.h`, header-only, `Krate::DSP`.
  - All storage is fixed-size members, with no heap.
  - `static constexpr std::size_t kControlInterval = 32`, `static_assert`ed equal to `ProfundumCore::kControlInterval`,
    `MassLaw::kControlInterval` and `BassStereoField::kControlInterval`.
  - The engine has **no** `processBlock` and no audio buffer (FR-038).
- **FR-011 Lifecycle.**
  - `void prepare(double sampleRate) noexcept` configures the position rate limit at the interval rate fs / 32. It
    accepts 22 050–192 000 Hz and clamps outside that range, like the Anchor's and the Drop's `kMin/kMaxSampleRate`.
  - `void reset() noexcept` snaps p and p_a to their targets and then applies the FR-034 selection rule to the snapped
    values: a selection becomes B if its position is ≥ θ_up, A if ≤ θ_down, and is **kept** inside (θ_down, θ_up).
    A freshly constructed engine's selections are A. `reset()` is therefore never a source of a selection the live
    engine would not hold at the same position and history. It then resolves. (`reset()` is a transport / sample-rate
    operation; the state round trip does not go through it, FR-062.)
  - Before `prepare`, setters store their values and `advanceInterval` snaps p and p_a to their targets (no ramp,
    since no rate exists yet), applies the selection rule above, and returns the resolved state of the stored targets.
- **FR-012 Endpoints** (edit API ruled 2026-10-19, Q8: whole-endpoint `setEndpoint` only; no per-field setter, no
  engine-side ramp of edits). `enum class Endpoint : std::uint8_t { A, B }`.
  - `bool setEndpoint(Endpoint e, const MorphState& s) noexcept` stores `s` by value. It returns false and writes
    nothing if `s` fails `isValidMorphState` or `e` is undeclared. Storing a bitwise-identical state (FR-005a) is a
    no-op.
  - `[[nodiscard]] const MorphState& endpoint(Endpoint e) const noexcept` reads a snapshot back.
  - Both snapshots default to the FR-004 defaults.
- **FR-013 Position.** `void setPosition(float p) noexcept`. A non-finite p keeps the previous target; this filter runs
  **before** the smoother, because `SlewLimiter::setTarget` snaps to 0 on NaN (smoother.h:519–524). Finite values
  clamp to [0, 1]. `[[nodiscard]] float position() const noexcept` returns the ramped p; `targetPosition()` returns
  the target.
- **FR-014 Anchor morph mode.** `enum class AnchorMorphMode : std::uint8_t { Lock, Blend, Punch }` and
  `void setAnchorMorphMode(AnchorMorphMode m) noexcept`; an undeclared value is ignored.
  - This is engine-level morph *control*, not a `MorphState` field. It belongs to the 1000–1099 "morph control
    (anchor morph mode, edit target)" block (roadmap line 682).
  - The default is **Blend** (drafted D-7).
  - The name is distinct from `AnchorOscillator::Mode`, as the roadmap's naming rule requires (lines 770–772).
- **FR-015 Resolution.** `const Resolved& advanceInterval() noexcept` advances p and p_a by one 32-sample interval,
  resolves, and returns `struct Resolved { MorphState state; float position; float anchorPosition; float anchorMass; }`.
  `[[nodiscard]] const Resolved& resolved() const noexcept` reads it without advancing. The caller invokes
  `advanceInterval` once at every absolute sample index that is a multiple of `kControlInterval`, before that
  interval's component updates. This is the same grid contract as `BassStereoField::advanceControl` (Phase 7 handoff
  §2).
- **FR-016 Global.** The engine holds no voice state. Every voice reads the same `Resolved` for the interval, so the
  resolved state does not depend on the number of consumers or the order in which they read it (roadmap line 600:
  "global: one morph position, all voices follow"). Per-voice behaviour comes only from each component's own state.
  For example, a Drop latched at a voice's trigger keeps the values of that trigger (FR-039).

### FR-020 series — The position ramp (roadmap line 586: "a smoothed, automatable morph position")

- **FR-020 Rate-limited ramp.** p follows its target through a `SlewLimiter` at the interval rate, with a full 0 → 1
  traversal time `kDefaultPositionRampMs`. Drafted D-6: 40 ms, the same as `MassLaw::kMassSweepMs` (mass_law.h:77).
  - Every target change, including a 0 → 1 jump, moves p by at most Δp_max = 32 / (rampMs · fs / 1000) per interval.
  - A rate limit is used rather than a fixed-time ramp, so the per-interval step does not grow with the size of the
    jump (smoother.h:100–108).
- **FR-021 Ramp-time control** (ruled 2026-10-19, Q3: the ramp time **is** a user control; Phase 11 registers it as
  "Morph smoothing"; the runtime blob carries it, FR-062).
  - `void setPositionRampMs(float ms) noexcept` takes [`kMinPositionRampMs`, `kMaxPositionRampMs`] = [**5 ms**,
    **2000 ms**]; `kDefaultPositionRampMs` = **40 ms**;
  - a non-finite value keeps the previous one;
  - the change applies to the rate from the next interval, without moving p (SC-022).
  - The range is not tied to the component crossfade lengths. A ramp shorter than a component's transition (for
    example 5 ms against the character stage's one-period pre-roll + 10 ms crossfade) is correct, because each
    discrete field steps once per threshold crossing and the component's own transition, with its queue
    (bass_character_stage.h:241–255), owns the audio; SC-003, SC-004(a, b) and SC-014 run at `kMinPositionRampMs`,
    the default and `kMaxPositionRampMs` to prove it.
- **FR-022 Exact landing.** When p reaches its target, it equals the target bitwise. p = 0 and p = 1 are therefore
  reachable exactly, which the FR-035 identity needs. The same holds for p_a.

### FR-030 series — Per-parameter interpolation laws (roadmap lines 587–590)

- **FR-030 Law table.** One `static constexpr` field-descriptor table assigns every `MorphState` field exactly one
  `Law` and one `Axis`. It lives in `morph_state.h` as `detail::morph_state_io::kFieldTable`, with `Law`, `Axis`,
  `Kind` and `SelectionGroup` nested in `MorphState`; `BassMorphEngine` re-exports them (`BassMorphEngine::Law`,
  `::Axis`, `kLawTable`) so both spellings name the same table (plan C-1, resolved by the main loop 2026-10-19:
  `morph_state.h` cannot see a table declared in the engine header that includes it).
  - `Law` is one of `{ Linear, Logarithmic, EqualPower, Stepped }`.
  - `Axis` is one of `{ SineComplex, CleanNonlinear, MonoStereo, ShortLong, DarkBright, Anchor, Motion }`.
  - Each row also carries the field's log offset (FR-032; 0 except `motion.cycleFadeInMs`) and its serialised size in
    bytes (4 for a float, 1 for an enum or bool). The table is the **field-descriptor table**: the serializer's
    offsets in `detail::morph_state_io` are derived from it, and `static_assert(kMorphStateBytes == 1 + Σ row sizes)`
    (1 = the version byte) and `static_assert(kMorphLeafFieldCount == table size)` hold in `morph_state.h`.
  - The table is the single source the resolver reads. A user-selectable curve does not exist (roadmap line 763).
  - SC-019 proves that every field has a row and that the resolver applies the tabled law.
- **FR-031 Linear.** x(p) = x_A + p·(x_B − x_A). **Every law (FR-031–FR-033) is evaluated in double and rounded once
  to float** (ratified 2026-10-19, plan C-4): float evaluation can err by 2.5 ulp for opposite-sign endpoints, over
  SC-019(b)'s 2-ulp bar. The log law caches log2(x + o) of both endpoints in double at `setEndpoint`. Stored and
  returned values are floats.
- **FR-032 Logarithmic.** x(p) = exp2(log2 x_A + p·(log2 x_B − log2 x_A)). The run-time `std::log2` / `std::exp2` are
  used, not the constexpr series; this is the precedent of the Phase 6 SC-017 refinement. It applies to the
  **frequencies and times**:
  - `dropTimeMs` (min 1 ms, Phase 4 D-1, set for exactly this law);
  - `ampEnv.attackMs`, `decayMs` and `releaseMs` (min 0.1 ms);
  - `field.crossoverHz` (min 60 Hz);
  - `anchor.punchTimeMs` (min 5 ms) and `anchor.ghostTimeMs` (min 1 ms);
  - `motion.cycleRateHz` (min 0.01 Hz), `motion.envAttackMs` and `motion.envDecayMs` (min 0.1 ms);
  - `motion.cycleFadeInMs`, whose valid range starts at 0 (0 = no fade-in, lfo.h:295–298), with a per-field offset
    o = 1 ms: x(p) = exp2(log2(x_A + o) + p·(log2(x_B + o) − log2(x_A + o))) − o. Every other log field has o = 0.
    The offset is a constant column of the FR-030 table, not a fifth law.

  Every lower bound plus its offset is > 0, so the law is defined on the whole valid box. `field.motionRate` is a normalised [0, 1]
  control that the field maps itself (bass_stereo_field.h:106, C-9), so it is **linear**.
- **FR-033 Equal-power.** For a **mix** m ∈ [0, 1], m(p) = √((1 − p)·m_A² + p·m_B²): energy-weighted, constant when
  m_A = m_B, monotone in p. It applies to `excitation.mix` and `anchorFoldFeed` (drafted D-2). This is a scalar law.
  The audio-gain pair `equalPowerGains` (crossfade_utils.h:50) is not reused, because a cos / sin weighting of two
  parameter values would raise a constant mix by up to √2.
- **FR-034 Stepped (with crossfade or algorithm-specific transition).** Discrete fields resolve from one of two
  **selections**: the main selection (driven by p) for every discrete field outside the Anchor block, and the Anchor
  selection (driven by p_a) for `anchor.waveform` and `anchor.mode`; **ratified 2026-10-19 (plan C-2): the main
  selection is held per governing-amount group** — Character, ImageMode, FieldMotion and Motion, each with its own
  state at p, and Anchor at p_a — because rule 2 can force different groups to different sides at the same position
  and rule 3 must remember each. A discrete field resolves to its group's selected
  endpoint's value. **Selection rule (ruled 2026-10-19, Q1: amount-aware selection).** For a field at position q (p or
  p_a), in this order:
  1. **Identity.** q = 0 selects A and q = 1 selects B (FR-035 holds by selection).
  2. **Governing-amount rule.** Each discrete field has a *governing amount*, the continuous field whose value 0 makes
     that side of the field silent (table below). If exactly one endpoint's governing amount is 0, the **other**
     endpoint's choice is selected for every q in (0, 1), whichever way q is moving. The silent side never runs its
     processor: on the showcase pair (A Tube at Amount 0, B Fold at Amount 0.8) the stage is Fold from the first
     interval with p > 0, while its Amount rises from 0, and Tube is never heard mid-morph. The rule is symmetric and
     direction-free, so it is a pure function of the endpoints and q.
  3. **Hysteresis.** Otherwise (both governing amounts zero, or both non-zero) the selection is an `Endpoint` with
     hysteresis (D-1: θ_up = 0.52, θ_down = 0.48): it becomes B when q ≥ θ_up, becomes A when q ≤ θ_down, and is
     unchanged in between. When rule 2 stops applying mid-morph (an endpoint edit makes the zero amount non-zero), rule
     3 takes over from the current q: B if q ≥ θ_up, A if q ≤ θ_down, else the selection rule 2 had forced.

  | Discrete field | Governing amount (0 = that side silent) |
  |---|---|
  | `characterAlgorithm` | `characterAmount` (Amount 0 bypasses the stage, bass_character_stage.h:13) |
  | `field.mode` | `field.spread` (Spread 0 centres every partial and zeroes the WIDE offsets, Phase 7 FR-010 / handoff) |
  | `field.motionSource`, `cycleSync`, `cycleNote`, `cycleModifier` | `field.motionDepth` |
  | `anchor.waveform`, `anchor.mode` | `anchor.level` (Anchor silent at level 0; the mode is a refinement of the Q1 ruling: at level 0 the mode is as inaudible as the waveform) |
  | `motion.source`, `target`, `cycleShape`, `cycleSync`, `cycleNote`, `cycleModifier`, `cycleRetrigger`, `polarity` | `motion.amount` (amount 0 routes nothing) |

  The selections are engine state, and the state round trip carries them (FR-062: the main byte is a 4-bit group
  mask, the Anchor byte 0 / 1); the governing-amount rule adds
  no runtime bytes, because it is recomputed from the stored endpoints.
  - The engine never crossfades audio. The audible transition is each component's own click-free mechanism, reached
    through its existing setter:

    | Discrete field | Transition the step triggers | Source |
    |---|---|---|
    | `characterAlgorithm` | `BassCharacterStage::setAlgorithm`: one-period pre-roll + 10 ms crossfade; the last request is queued while one is in flight | bass_character_stage.h:81, 241–255; Phase 5 FR-057 |
    | `field.mode` | `BassStereoField` FR-052: 10 ms side crossfade plus the vector slew (measured 9.98 ms) | bass_stereo_field.h:91; roadmap line 534 |
    | `field.motionSource`, `cycleSync`, `cycleNote`, `cycleModifier` | the field's own handling, bounded by the FR-015 pan slew (gated by SC-004, not assumed) | Phase 7 handoff §1 |
    | `anchor.waveform` | `AnchorOscillator` FR-014: 5 ms coherent crossfade | anchor_oscillator.h:76 |
    | `anchor.mode` | the Anchor's mode bridge, 5 ms | anchor_oscillator.h:79 |
    | `motion.source`, `target`, `cycleShape`, `cycleSync`, `cycleNote`, `cycleModifier`, `cycleRetrigger`, `polarity` | the Phase 9 Motion component's transition; the click-free gate is a Phase 9 handoff gate (FR-083), because no Motion component exists to render in this phase | FR-002 MotionConfig table |

  - All discrete fields share one threshold pair and one governing-amount table (per-axis staggered thresholds were
    rejected, Q1). The worst-case CPU interval, where every component crossfades at once, is a recorded Phase 9
    handoff measurement (FR-083), not a Phase 8 SC.
- **FR-035 Endpoint identity.**
  - At p = 0, every resolved field is **bitwise** equal to A's. At p = 1, it is bitwise equal to B's, except for the
    Anchor block in Lock (FR-052).
  - A field whose A and B values are bitwise equal resolves to that value at every p.
  - Both rules hold by **selection**, not by arithmetic: the resolver returns the stored value whenever the position
    is exactly 0 or 1, or the endpoints are equal. A float law never has to round-trip an endpoint.
- **FR-036 Validity closure.** For every pair of valid endpoints and every p ∈ [0, 1], each continuous field's resolved
  value lies in [min(x_A, x_B), max(x_A, x_B)]. The resolver clamps to that interval after evaluation, to absorb
  `exp2` / `sqrt` rounding. The resolved state therefore passes `isValidMorphState`.
- **FR-037 Monotone laws.** Each continuous law is monotone in p between its endpoints, so a monotone position sweep
  moves every field monotonically. That is the precondition for the SC-008 axis trajectories.
- **FR-038 Recipe domain, never an audio crossfade.** The core's spectrum at every p comes from the interpolated
  `recipe` that the voice passes to `ProfundumCore::setControls` (profundum_core.h:173). The engine owns no audio
  buffer and renders nothing (FR-010). The core's own per-interval shape-step ceiling (profundum_core.h:142–147)
  stays in force downstream.
- **FR-039 Latched and smoothed receivers.** The engine does not double-smooth what a component already smooths:
  - Fold amount, 10 ms (harmonic_fold.h:92);
  - SHAPE, 10 ms (shape_morph.h:107);
  - Character Amount / Tone, 10 ms (bass_character_stage.h:84);
  - excitation, 5 ms (profundum_core.h:348);
  - Anchor level, 5 ms (anchor_oscillator.h:75);
  - the field's slews.

  The Drop fields are latched at trigger (drop_envelope.h:80–81), so a morph changes the **next** note's Drop
  (Phase 4 D-4). A Drop in flight never steps (SC-023). The plan records which receivers apply a new value
  immediately; the amp envelope is the candidate.

### FR-040 — The five perceptual axes (roadmap lines 592–596; concept §10 lines 478–484)

The engine "understands what is being morphed". Each concept axis has a named law, and every field carries its axis
tag in the FR-030 table.

| Axis | Fields | Law |
|---|---|---|
| **sine → complex oscillator** | `recipe.depth`, `body`, `bodyCurvature`, `bodyEmphasis` | Linear, in the recipe domain (FR-038) |
| **clean → nonlinear** | `excitation.drive`, `excitation.asymmetry`, `foldAmount`, `characterAmount`, `mass` | Linear |
| | `excitation.mix` | Equal-power |
| | `characterAlgorithm` | Stepped, through `setAlgorithm` |
| **mono → stereo** | `field.mode` | Stepped, through FR-052 |
| | `field.spread`, `gravity`, `curve`, `motionDepth`, `motionRate` | Linear |
| | `field.crossoverHz` | Logarithmic |
| | `field.motionSource`, `cycleSync`, `cycleNote`, `cycleModifier` | Stepped |
| **short → long** | `dropTimeMs`, `ampEnv.attackMs` / `decayMs` / `releaseMs` | Logarithmic |
| | `ampEnv.sustain`, the env curves, `dropAmountSt`, `dropShape`, `dropRebound`, `transientAttackDb`, `transientSustainDb` | Linear (the dB gains are linear in dB) |
| **dark → bright** | `recipe.edge`, `recipe.shift`, `shape`, `characterTone` | Linear, each in its own domain |
| *(Anchor)* | `anchor.level`, `punchBoostDb` (linear in dB) | Linear at p_a |
| | `punchTimeMs`, `ghostTimeMs` | Logarithmic at p_a |
| | `anchorFoldFeed` | Equal-power at p_a |
| | `waveform`, `mode` | Stepped at p_a |
| *(Motion)* | `motion.cycleRateHz`, `envAttackMs`, `envDecayMs` | Logarithmic |
| | `motion.cycleFadeInMs` | Logarithmic on (t + 1 ms) (FR-032) |
| | `motion.amount`, `depth`, `cyclePhaseDeg`, `driftSmoothness` | Linear |
| | `motion.source`, `target`, `cycleShape`, `cycleSync`, `cycleNote`, `cycleModifier`, `cycleRetrigger`, `polarity` | Stepped |

### FR-050 series — Anchor morph modes (roadmap lines 142–144, 597–599)

- **FR-050 Own position.** The whole Anchor block (`anchor` and `anchorFoldFeed`) resolves at the Anchor position
  p_a, not at p. p_a's target is c_m(p) for the active mode m.
- **FR-051 Curves.**
  - **Lock:** c(p) ≡ 0. The Anchor keeps A's values at every p ("constant").
  - **Blend:** c(p) = p ("interpolated").
  - **Punch:** c(p) = p^k, with k = `kPunchExponent` = **3**, a fixed constant (ruled 2026-10-19, Q6: no user
    exponent, no new parameter; roadmap line 763 rejects user curves). This is the "designed curve — weight retained
    early, yielding late": c(0) = 0, c(1) = 1, c is monotone, c(p) ≤ p on [0, 1], c′(0) = 0, and c(0.5) = 0.125.

  p_a follows its target through the same rate limit as p (FR-020). A mode change mid-morph therefore moves p_a by
  at most Δp_max per interval and never steps the Anchor.
- **FR-052 Identity under Lock** (ruled 2026-10-19, Q5: the **whole** Anchor block is held at A). In Lock, the
  resolved Anchor block — `anchor.level`, `anchor.waveform`, `anchor.mode`, the punch / ghost times, every other
  `AnchorOscillator::Controls` member, and `anchorFoldFeed` — equals A's at **every** p, including p = 1. This is the
  one documented exception to FR-035's "p = 1 reproduces B". B's Anchor fields are stored and serialised as usual but
  are inaudible while Lock is active; the engine does not route or mirror them (the "shared block" alternative was
  rejected), and surfacing that to the user is a Phase 12 handoff (FR-083). In Blend and Punch, p = 1 reproduces B's
  Anchor exactly.
- **FR-053 The concept example.** With A `anchor.level` 1.0 and B 0.7 in Blend, the resolved Anchor level runs
  linearly 1.0 → 0.7 while every main-voice field travels 0 → 100 % (concept lines 495–505; roadmap lines 597–599).
- **FR-054 Mass at the Anchor's position.** `Resolved::anchorMass` is the Mass field resolved at p_a, linearly. Phase 9
  evaluates `MassLaw`'s `AnchorLevel` row (mass_law.h:110) at `anchorMass` and every other row at the resolved
  `mass`. The Anchor's level then obeys the Anchor morph mode end to end: in Lock, Mass's Anchor bias stays at A's,
  while Mass still moves the main voice. Drafted D-5. The contract is recorded as a Phase 9 handoff (FR-083).
- **FR-055 Discrete Anchor fields.** `anchor.waveform` and `anchor.mode` step by FR-034 against p_a, so in Lock they
  never switch.

### FR-060 series — Engine contract for the editing model (Open Question 2; OQ-1 here)

- **FR-060 Two stored snapshots, one resolved state.**
  - The engine stores A and B by value and runs only the resolved state (roadmap line 698: "the registered sound
    parameters address the selected endpoint via an Edit A / Edit B target, the engine runs the resolved state, both
    snapshots live in state").
  - An edit replaces one whole endpoint through `setEndpoint` (FR-012). Phase 11 keeps a `MorphState` copy per
    endpoint, edits the one field and re-sets the endpoint; one invalid field rejects the whole edit (FR-012). There
    is no per-field setter and no field-ID enumeration (ruled 2026-10-19, Q8). The edit routing (which parameter
    writes which endpoint) is Phase 11's.
  - Copy and compare are controller-side (roadmap line 702). The engine offers no copy, swap or compare API.
- **FR-061 Edits while morphing.** An endpoint edit changes the resolved fields at once, by (Δendpoint × the law's
  weight at p). It is not ramped by the engine; the receiving components' own smoothing applies (FR-039). This is the
  same behaviour as a direct parameter change on an unmorphed instrument (ruled 2026-10-19, Q8: edits are never
  ramped by the engine). A discrete field whose edited value differs
  from the side currently selected switches through its component's transition (FR-034). SC-024 gates both rules.
- **FR-062 Runtime state round trip** (roadmap lines 605–606: "both states and the position serialise round-trip").
  The engine serialises its **live** position state, not just the targets, so a reload reproduces what was sounding:
  - `[[nodiscard]] std::size_t serializeRuntime(std::byte* dest, std::size_t capacity) const noexcept` and
    `[[nodiscard]] bool restoreRuntime(const std::byte* src, std::size_t size) noexcept`, in the FR-006 style:
    a version byte `kMorphRuntimeFormatVersion = 1`, fixed offsets, `std::memcpy` of bit patterns, exact size
    `kMorphRuntimeBytes`.
  - The runtime set is: the position target; the ramped p; the ramped p_a; the Anchor morph mode; the ramp time
    (FR-021); the main selection and the Anchor selection (FR-034), one `std::uint8_t` each — the main byte is a
    bitmask over the four p-driven groups (bit g = 1 means group g selects B; valid 0x00–0x0F), the Anchor byte is
    0 / 1 (plan C-2, ratified 2026-10-19). The p_a target is
    not stored, because it is c_m(target) (FR-051).
  - `restoreRuntime` returns false and changes nothing if the bytes are short, the version is unknown, a float is
    non-finite or out of range, an enum is undeclared, or a selection contradicts the FR-034 rule for the stored
    endpoints and position (B stored with the position ≤ θ_down, A with the position ≥ θ_up, or a side the
    governing-amount rule forbids at that position). On success it installs the values exactly and re-resolves
    **without** advancing and **without** `reset()`.
  - The round trip for Phase 11 is: `serializeMorphState(A)`, `serializeMorphState(B)` and `serializeRuntime()` on the
    live engine; on a fresh engine, `setEndpoint(A)`, `setEndpoint(B)`, `restoreRuntime(...)`. Afterwards
    `resolved()` is bitwise equal (FR-005a) to the live engine's `resolved()`, and every later `advanceInterval` under
    the same automation is too. This holds for a p inside [θ_down, θ_up] reached from either direction (the stored
    selection, not the position, decides) and for a mid-ramp p (target ≠ ramped value). SC-007(c) gates it.

### FR-070 series — Test fixture

- **FR-070 Morph chain fixture: a new per-interval driver (test code only).** Neither existing chain helper can apply
  a per-interval `Resolved`:
  - `renderChain` (mass_dynamics_test_helpers.h:372–447) sets the core from a static `cfg.base` through
    `coreControlsFor(cfg.base, t)` (275) and feeds the Anchor `anchorControlsFor(t.anchorLevel)`, which is Sine / LOCK
    only (banner lines 13–17). It has no ShapeMorph, character stage, amp envelope or Drop. Its
    `a1Unbiased = unbiasedH1Amplitude(cfg.base, …)` is computed once from the static base (396), which goes stale once
    the recipe morphs.
  - `renderFieldChain` (bass_stereo_field_test_helpers.h:267) sets SHAPE, the algorithm, Amount, Tone and the
    excitation once, before prepare (290–311); its only per-interval inputs are `fieldAt` and the Mass.

  `bass_morph_test_helpers.h` therefore adds **a new render loop**, `renderMorphChain(const MorphChainConfig&)` (and
  a 4-voice `renderMorphPoly`). A new chain body is permitted here because it is test-only; the Phase 6 and 7 helper
  headers stay **read-only** and are included, not edited. It reuses `MassDynamicsTest::gridDriver` (103),
  `makeChainDynamics` (348), `prepareChainLaw` (364) and `unbiasedH1Amplitude` (288), and the Phase 7 tail
  `processFieldTail` / `processStageAndOutput` (bass_stereo_field_test_helpers.h:232, 204), through a
  `FieldChainConfig` embedded in `MorphChainConfig` for the tail's options.

  `MorphChainConfig` holds the endpoints, the Anchor morph mode, f₀, fs, length, host block, `noteOnSample`, an
  optional `noteOffSample`, `ampEnvelope` (bool, default true) and `automation`: a list of **sample-stamped events**
  (position, ramp time, mode, endpoint replacement), each applied at the first grid point at or after its stamp.
  At each grid point, in this order:
  1. apply the due automation events; call `engine.advanceInterval()` **exactly once**; record the `Resolved`;
  2. Mass (FR-054 split): `MassLaw::setBase` with depth / body from the resolved recipe, `foldAmount` from the resolved
     Fold, `anchorLevel` from the resolved `anchor.level`, and the two Mass-driven bases at the `Targets{}` defaults;
     `setMass(resolved.mass)`; `advanceInterval()`. The Anchor's level is `MassLaw::evaluateRow` of the AnchorLevel
     row (mass_law.h:110, 136–142) at `resolved.anchorMass` on the resolved `anchor.level`;
  3. `ProfundumCore::setControls(coreControlsFor(resolved.recipe, targets))`; `setExcitation(resolved.excitation)`;
  4. `HarmonicFold::setAmount(targets.foldAmount)`; `ShapeMorph::setShape(resolved.shape)`;
  5. `BassCharacterStage::setAlgorithm` (only when the resolved algorithm differs from the last request),
     `setAmount`, `setTone`;
  6. `AnchorOscillator::setControls(resolved.anchor` with the Mass-biased level`)`; `anchorFoldFeed` applied as in
     the Phase 3 SC-022 fixture;
  7. `DropEnvelope` setters (latched by the component at its next trigger);
  8. `ADSREnvelope` setters (test fixture only; the class choice is Phase 9's);
  9. `BassStereoField::setControls(resolved.field)` then the field's `advanceControl`;
  10. `MassDynamics` transient gains, transient duration, compression amount and output trim from the targets.

  Per rendered block: the Drop's offset sets the per-sample f₀ of the core, the Anchor, ShapeMorph and the character
  stage (the `f0At` pattern, bass_stereo_field_test_helpers.h:130); the Anchor's a1 is
  `unbiasedH1Amplitude(resolved.recipe, f₀, fs)` **recomputed each interval** from the resolved recipe (the Phase 6
  G8-3 rule applied to the morphing recipe), so it never goes stale. The **amp envelope** is one `ADSREnvelope` per
  voice, gated at note-on / note-off, whose gain multiplies the voice's character-stage output **and** its Anchor
  before the sum: the Anchor is part of the voice and is released with it. With `ampEnvelope = false` the gain is
  unity (the steady render). Every setter is called with changed values only, so the 256-entry curve-table rebuild of
  `setAttackCurve(float)` (adsr_envelope.h:160–164) runs only on change. Recorded per interval: the `Resolved`, the
  targets, the Anchor level and a1. Taps: core, character, Anchor, sum, dyn, stage out, out.
- **FR-071 Showcase pair** as defined above (D-8; B harvested, B7-2). It is a named fixture, so Phase 9's audibility table can reuse it.

### FR-080 series — Cross-cutting

- **FR-080 RT safety.** Every method except `prepare` is `noexcept`, allocation-free, lock-free, exception-free and
  free of I/O. That covers the setters, `advanceInterval`, `reset`, `isValidMorphState`, `serializeMorphState` and
  `deserializeMorphState`. `prepare` is RT-safe in the allocation sense but is a configuration call.
- **FR-081 Non-finite safety.** A NaN, an infinity or an out-of-range value can never reach `Resolved`:
  - `setPosition` filters it (FR-013);
  - `setEndpoint` rejects a state with any invalid field (FR-012);
  - `deserializeMorphState` rejects invalid bytes (FR-006).

  All guards use `detail::isFinite` / bit patterns. The non-finite TU builds with `-fno-fast-math`.
- **FR-082 Determinism and partition invariance.** Resolution happens only on the 32-sample grid. Identical endpoint,
  position and mode sequences give bitwise-identical `Resolved` sequences, whatever the host block partition and
  however many engines run them. The engine has no random source.
- **FR-083 Handoffs.** The plan records these for later phases:
  - **Phase 9:**
    - the `advanceInterval` call order. It comes before `BassStereoField::advanceControl` and before every voice's
      component setters in the same interval;
    - the FR-054 Mass split, with base = resolved fields and Mass = resolved `mass` / `anchorMass`;
    - FR-039's change-only setter rule for the amp-envelope curves;
    - **field sets fixed here (Q4):** `AmpEnvelopeFields` and `MotionConfig` are final for version 1; Phase 9 appends
      only, under a version bump, and records any field its envelope class or Motion component cannot realise as a
      deviation in its spec, never by silently ignoring it;
    - **recorded handoff measurement** (moved from SC-018(b), which had no bar): the FR-070 fixture's worst
      interval, with every discrete transition firing at once at 4 voices, measured under the SC-018 protocol and
      recorded against Phase 5's 2.65 % crossfade cell, for Phase 9's CPU budget to rule on;
    - **handoff gate, MotionConfig selectors click-free:** once the Motion component exists, Phase 9 renders every
      ordered pair of `motion.source`, `target`, `cycleShape`, `polarity`, `cycleSync`, `cycleRetrigger` and the
      SC-004 representative `cycleNote` × `cycleModifier` subset through the morph, with `amount` 1 and `depth` 1,
      against the SC-004(a, b) bars. Phase 8 gates only the engine side of these fields (SC-019's selection
      switch at θ_up; SC-004(c)'s hysteresis);
    - Drop morphs act on the next note;
    - **component switch clicks (SC-004(b) record, 2026-10-19):** Phase 5's Fracture → Crush / Rectify at C3 (1–2
      differential clicks against the static ends, slope ratio 1) and Phase 3's OctaveDownSine → Sine / Rounded /
      Asymmetric at E1 with a 5 ms position ramp (1 click): the component's own direct switch carries them, the morph
      adds none (evidence/sc004-algorithm.csv, sc004-anchor-waveform.csv, ref_clicks columns);
    - **HarmonicFold path dependence (SC-006(b) record, 2026-10-19):** above `kSoftEnd` the fold's peak detector
      (release hysteresis 0.5 %, Phase 2 D-4) latches a path-dependent envelope: an A → B → A excursion returns a
      render 0.13 dB off on one harmonic that does not decay in 8 s (evidence/run-log/close/sc006-settle-probe.log);
    - **join-point finding (probe, 2026-10-19):** the nonlinear chain (Fold, SHAPE, Character) regenerates a
      fundamental whose phase against the Anchor rotates with the settings and the note (θ from −149° to +179°,
      magnitude 0.27–8.3× the Anchor across the harvest; one variant cancelled fully at C3, −9.9 dB). Phase 3's
      "add coherently" (its FR-010) holds at the core tap only. Phase 9 decides the Anchor join point / phase
      reference with this in hand (evidence/showcase-harvest.txt).
  - **Phase 11:** `kMorphStateBytes` × 2 + `kMorphRuntimeBytes` (FR-062: position target, ramped p and p_a, Anchor
    morph mode, ramp time, both selections) in state; restore through `restoreRuntime`, never through
    `reset()`; the Edit A / Edit B routing; IDs for position (100–199) and morph control (1000–1099).
  - **Phase 12:** the FR-030 law table and FR-051's curves as "what the user sees while morphing" (roadmap line 737);
    and the Lock UX (Q5): while the Anchor morph mode is Lock, edits to B's Anchor fields are stored but inaudible,
    and the UI shows it (for example by dimming B's Anchor controls) rather than the engine mirroring them.
- **FR-084 Audibility (theme FR).** The morph proves its levers move the sound (SC-015):
  - the position moves all five axis descriptors on the showcase pair;
  - the Anchor morph mode moves E_sub at p = 1.

  Nothing ships inert.
- **FR-085 Mono compatibility (theme FR).** The morph touches stereo through `field`. At every p along the showcase
  morph, including through the SOLID → WIDE step:
  - the Anchor tap stays bitwise L == R;
  - the sub-band meets Phase 7's mono-sum and correlation bars (SC-016).
- **FR-086 Anti-aliasing (theme FR) and no new nonlinearity.** The engine adds no nonlinear stage. The nonlinear
  stages it drives are measured *while morphing*: at E0–E2, through the algorithm crossfade, against the ratified
  −80 dBFS bar (SC-017).
- **FR-087 Layer, ODR, portability.**
  - Includes point downward or to L3 peers only.
  - The names are swept and recorded.
  - Designated initialisers everywhere (no narrowing in brace init).
  - No `std::isnan`; no namespace-scope `Waveform`.
  - `node tools/check-portability.js` and `tools/lint-apple-globals.js` are clean.
- **FR-088 Shared headers.** No existing header changes. If the plan finds a change unavoidable, it is append-only, and
  the full suites of every consumer of that header gate it (cross-cutting rule, lines 947–949 and 955–956).
- **FR-089 Test registration.** Every Phase 8 TU registers in `dsp_systems_tests` with the `[profundum]` tag. The
  non-finite TU goes in the `-fno-fast-math` block. Renders longer than about 15 s that are toolchain-independent get
  `[long]`. The CPU case gets `[performance]`.

---

## Success Criteria

| ID | Criterion (FRs) | Metric, threshold and method (test name sketch) |
|---|---|---|
| **SC-001** | Endpoint identity (FR-035, FR-052, FR-022) | `BassMorph_EndpointIdentity`. 10 000 seeded random valid (A, B) pairs, plus the showcase pair and the named recipe states, in each Anchor morph mode, with the ramp run to the end. At p = 0 the resolved state is bitwise equal (FR-005a, `BassMorphTest::bitEqual`) to A. At p = 1 it equals B, with the Anchor block equal to A in Lock and to B in Blend / Punch. **0 mismatches.** |
| **SC-002** | Equal endpoints are constant (FR-035) | `BassMorph_EqualEndpointsConstant`. With A == B (random, 1 000 states), the resolved state is bitwise equal (FR-005a) to A at p ∈ {0, 0.01, …, 1} and at every interval of a 0 → 1 → 0 ramp. **0 mismatches.** |
| **SC-003** | Bounded per-interval change (FR-020, FR-021, FR-031–FR-033, FR-051) | `BassMorph_BoundedStep`. Target jumps 0 → 1, 1 → 0 and 0.3 → 0.7, plus 30 s of seeded random automation, at 44.1 / 48 / 96 / 192 kHz, at ramp times `kMinPositionRampMs`, the default and `kMaxPositionRampMs` (FR-021). The tolerances are explicit because the `SlewLimiter` step is computed in float (smoother.h:512) and the laws are evaluated in float. (a) Per interval, \|Δp\| ≤ Δp_max·(1 + 4·FLT_EPSILON) **+ 0.5·ulp(max(\|p_old\|, \|p_new\|))**, with Δp_max = 32 / (rampMs·fs/1000), and the same for \|Δp_a\|, including across an Anchor-mode change mid-morph. The ulp term is the float rounding of the position itself (`SlewLimiter::process` adds the step in float, smoother.h:549): it is the same k·ulp form as (b), it is absolute, and it is what makes the bar meetable at 2000 ms / 192 kHz, where Δp_max ≈ 8e-5 and 4·ε·Δp_max ≈ 4e-11 against 0.5·ulp(0.99) = 3e-8 (build ruling B3-1, 2026-10-19). (b) Each continuous resolved field changes by at most the law's analytic bound over a step Δp_max, times (1 + 4·FLT_EPSILON), plus k·ulp(max(\|x_A\|, \|x_B\|)), with k = 2 for linear and equal-power and k = 8 for logarithmic. The bounds: linear \|x_B − x_A\|·Δp; logarithmic (max(x_A, x_B) + o)·\|((x_B + o)/(x_A + o))^Δp − 1\| (o = the FR-032 offset); equal-power √Δp·max(m_A, m_B). (c) A 0 → 1 traversal takes ⌈rampMs·fs / (1000·32)⌉ intervals, ±1. |
| **SC-004** | Discrete transitions are click-free (FR-034, FR-055, FR-021; roadmap line 604) | `BassMorph_DiscreteClickFree` (`[long]` grid). Through the FR-070 fixture at E1 and C3, 48 kHz, steady render, at ramp times `kMinPositionRampMs`, the default and `kMaxPositionRampMs`. Each discrete field differs alone between A and B, and the position ramps 0 → 1. The Anchor morph mode is **Blend** in every case, so p_a = p and the Anchor cases really switch (in Lock they never would, FR-055). The grid covers every stepped field of the FR-030 table that a Phase 8 component renders: all 30 ordered algorithm pairs; the 6 ordered image-mode pairs; the 20 ordered Anchor-waveform pairs; the 12 ordered Anchor-mode pairs; the 2 ordered `field.motionSource` pairs and the 2 ordered `field.cycleSync` pairs (motionDepth 1, so the selector is audible); all 6 ordered `field.cycleModifier` pairs at cycleNote Quarter; and the 6 ordered `field.cycleNote` pairs among {SixtyFourth, Quarter, QuadrupleWhole} (note_value.h:38–49) at modifier None. The cycle cases run with motionSource Cycle, cycleSync on and motionDepth 1. The note × modifier subset is representative, not exhaustive (30 combinations give 870 ordered pairs): every change takes one code path, the field's rate update behind its pan slew, and the extremes bound the size of the rate step. The `MotionConfig` selectors have no Phase 8 renderer; their click-free gate is the FR-083 Phase 9 handoff gate. The **transition window** runs from the interval at which the selection switches to the component's transition end plus one period of f₀; for `field.mode` and the `field.motion*` selectors the transition end is the full pan-vector slew, 334 intervals (`kMaxPanStepPerInterval` 0.003; Phase 7 G7-1 measured SOLID ↔ SPREAD at ≈ 220 ms), not 10 ms (ratified 2026-10-19, tasks deviation 10; transition and reference are read over the same window, so nothing is relaxed). The **reference** is the same fixture with A held static, and the component's setter given B's value directly at the same grid point. (a) **One bound:** max \|y[n] − y[n−1]\| over the window ≤ **10^(0.5/20)** (≈ 1.059) × the reference's maximum over the same window, read with `ShapeCharacterTest::maxDeltaAround` (shape_character_metric_helpers.h:53). (b) The differential click count of the transition, by `differentialClicks(transition, refStart, refEnd, …)` (shape_character_metric_helpers.h:76) with refStart = A held static and refEnd = B held static, is **≤ the same count on the direct-setter reference** at the same grid point (ruled 2026-10-19, B10-1: the morph adds no click of its own, D-9). Both counts are recorded. A click the reference itself carries is the component's own switch and is handed to Phase 9 as a finding against that component's phase (measured: Phase 5's Fracture → Crush / Rectify at C3, 1–2 clicks with slope ratio exactly 1; Phase 3's OctaveDownSine → Sine / Rounded / Asymmetric at E1 with the 5 ms ramp, 1 click, ratio 1). (c) **Hysteresis:** the target is held at θ_up ± 0.01 (approached from below, selection A) and at θ_down ± 0.01 (approached from above, selection B), each under 5 Hz sinusoidal jitter for 2 s at the default ramp. The selection switches **≤ 1** time. **Positive control:** a test-side single-threshold model (θ_up = θ_down = 0.52), applied to the same recorded p sequence at θ_up, switches **≥ 10** times. That proves the fixture exercises chatter. (d) **Governing-amount rule (Q1, FR-034 rule 2):** for each row of the FR-034 governing-amount table that a Phase 8 component renders — `characterAlgorithm` (A Tube / Amount 0 → B Fold / Amount 0.8, and the reverse pair with the zero on B's side), `field.mode` (Spread 0 on one side), `anchor.waveform` and `anchor.mode` (level 0 on one side) — the ramp runs 0 → 1 → 0. The selection is the non-silent side at **every** interval with q in (0, 1) (read from `resolved()`), A at q = 0 and B at q = 1 bitwise; the component's setter is issued exactly once per direction, at the first interval with q in (0, 1) (q rising) and at the landing on 0 (q falling); and the render meets (a) and (b) against **the same morph with the discrete field pre-set to the non-silent side on both endpoints** (no step; the identical governing-amount ramp; reading refinement 2026-10-19 — the C-10 "p held at 0, setter at g_sw" reference is meaningless for a rule-2 row, because the silent side is bypassed and switching it does nothing, so the ratio read the amount ramp against a static sine: 1.84 at E1 with 0 clicks). **ImageMode rows (ruled 2026-10-19, B10-2):** (a) is recorded, not gated, and (b) is gated — the SOLID → WIDE step engages WIDE's per-channel tone offset over the 334-interval ramp (Phase 7 G7-2), so against an instantly engaged no-step reference the darker channel stays brighter for longer (measured ratio R 1.068 / 1.071 against 1.059, L 0.93, 0 clicks); that is the ratified engagement ramp, not a step. Positive control: the same pairs with both amounts non-zero switch at θ_up / θ_down, not at the first interval. |
| **SC-005** | Anchor mode curves measured (FR-050–FR-054) | `BassMorph_AnchorCurves`. A level 1.0, B level 0.7, Mass 50 at both ends, everything else equal, p ∈ {0, 0.05, …, 1}. (a) Lock: the resolved level is bitwise 1.0 at every p, and E_sub(Anchor tap) is constant within 0.05 dB. (b) Blend: \|level(p) − (1 − 0.3p)\| ≤ 1e-6. (c) Punch: \|level(p) − (1 − 0.3p³)\| ≤ 1e-6, and level_Punch(p) ≥ level_Blend(p) at every p. (d) The rendered Anchor w₁ matches 20·log10(level) within 0.1 dB for every mode. (e) **Mass-only pair** (descriptor block). In Lock, the Anchor's effective Mass-law level is bitwise constant at its p = 0 value; that level is `MassLaw::evaluateRow` of the AnchorLevel row (mass_law.h:110, 136–142) at `anchorMass`. **The Mass-only morph is audible by Phase 6's own Mass bar** (ruled 2026-10-19, B9-1; Phase 6 SC-011, `ShapeCharacterTest::describe` / `separate` on the output): between p = 0 and p = 1 the endpoints separate by ≥ 3 dB on any of E_low, E_high, THD or I, or by ≥ 0.25 oct on the centroid C. The drafted "D_nl moves by ≥ 3 dB" clause was a mis-assigned descriptor: Mass 100 puts the Fold row at 61.3, inside HarmonicFold's harmonic-compression regime, which lowers the n ≥ 9 power 6.4 dB below the linear core by design (measured at the Group 8 gate), so the directed axis cannot carry Mass; D_nl is still recorded. In Blend, the effective level **rises** with p_a (from base − 0.5 at Mass 0 to min(base + 0.5, 1) at Mass 100) and is recorded per p. |
| **SC-006** | A → B → A idempotent (FR-022, FR-035, FR-034) | `BassMorph_Idempotent`. Ramps 0 → 1 → 0, and seeded random automation that ends at 0, for random valid pairs and every Anchor morph mode. (a) The final `resolved()` is bitwise equal (FR-005a) to the initial one, and the final `serializeRuntime` bytes equal the initial ones, so both selections are back at A. (b) **Render check.** The random pairs are **constrained** to `field.motionDepth` 0, `anchor.mode` ∈ {Lock, Follow} and Drop Amount 0 at both ends, **and A's Fold to the soft-saturation regime (`foldAmount` ≤ `HarmonicFold::kSoftEnd` = 30)** (ruled 2026-10-19, B10-3: above it, HarmonicFold's windowed-peak detector releases only when the raw peak falls 0.5 % below its envelope — Phase 2 D-4 hysteresis — and latches a path-dependent envelope: pairs with A at Fold 86 / 98 read 0.126 / 0.130 dB on h16 / h11, unchanged from 1 s to 8 s after landing, 0.003 / 0.0004 dB with A at Fold 20, and the Anchor mode changes nothing; pairs the constraint clamps are **also rendered unclamped and recorded**, and the finding goes to Phase 9 via FR-083), on the steady render (amp envelope bypassed). The A → B → A render is compared with a **reference render of A alone**, with the same note-on sample and host blocks. Let t_land be the first sample at which p is back at 0. Both renders are read on the output mid signal (L+R)/2 over the same absolute samples, [t_land + 1 s, t_land + 1 s + `TestUtils::kLowFrequencyFftSize`). Per-harmonic powers n = 1 … 16 (`ProfundumTest::renderedHarmonicPowers`, profundum_core_test_helpers.h:235) agree within **0.1 dB** for every harmonic within 60 dB of the strongest. This is a tolerance, not a bit-exact golden. |
| **SC-007** | Serialisation round trip (FR-006, FR-007, FR-062; roadmap lines 605–606) | `BassMorph_Serialisation`. (a) 10 000 random valid states: serialise → deserialise gives a bitwise-equal state (FR-005a), and the size is exactly `kMorphStateBytes`. (b) A null pointer, a short capacity or size, version ≠ 1, any single field NaN / ±Inf / out of range / undeclared enum, and a truncated buffer: serialise returns 0 and deserialise returns false; the destination bytes and `out` are unchanged. (c) **Live-state round trip** per FR-062. Live engines are driven by seeded automation and stopped at: 200 random positions; positions inside [θ_down, θ_up] reached **from below** (selections A) and **from above** (selections B); mid-ramp points (target ≠ ramped p); and a point with an Anchor-mode change in flight. This runs in each Anchor morph mode, with a non-default ramp time (FR-021), and includes stops where the FR-034 governing-amount rule forces a selection (the showcase pair at p = 0.3, selection B by rule 2). Each stop is saved (`serializeMorphState` × 2 + `serializeRuntime`) and restored into a fresh engine prepared at the same fs (`setEndpoint` × 2 + `restoreRuntime`). The restored `resolved()` is bitwise equal to the **live** engine's `resolved()`, never to a second reset engine's. The next 4 096 `advanceInterval` results under identical automation are bitwise equal too. **0 mismatches.** (d) `restoreRuntime` rejects short bytes, an unknown version, a non-finite or out-of-range float, an undeclared enum and a selection that contradicts its position: it returns false, and `resolved()` is bitwise unchanged. |
| **SC-008** | Perceptual trajectory of the showcase pair (FR-040, FR-033, FR-051; roadmap lines 605–607) | `BassMorph_ShowcaseTrajectory` (`[long]`). 21 static positions, at E1 and C3, 48 kHz, in each Anchor morph mode, each descriptor read on the render it names (descriptor block). (a) **Monotone:** each of D_cx, D_nl, D_st, D_t and D_br is non-decreasing in p within ε (drafted D-8: 0.25 dB for dB descriptors, 0.02 oct for D_br, 2 % for D_t); D_st on the clean core tap and D_t on the pre-dynamics sum (B10-4), with their output readings recorded per position. No position's gate render may hit the 12 s length limit. (b) **Net travel:** each moves by ≥ the SC-015 bar between p = 0 and p = 1. (c) **No loudness dip:** L_K(p) ≥ min(L_K(0), L_K(1)) − **1.5 dB** (ruled 2026-10-19, Q7; K-weighted). A red is remediated in the interpolation laws (FR-030) or the showcase pair (FR-071), **never** by a loudness-compensation output in the engine, which has no audio path (FR-038). (d) **Floor at the resolved Anchor level, in every Anchor morph mode** (ruled 2026-10-19, B7-1): in Lock, E_sub(p) ≥ F_lock(p) − **1.0 dB** at every p; in Blend and Punch, E_sub(p) ≥ F_lock(p)·L_m(p)/L_A − **1.0 dB**, where L_m(p) is the mode's resolved Anchor level (FR-051) and L_A is A's — equivalently, the sum's f₀ component is never more than 1 dB below that render's own Anchor tap. All at the pre-dynamics sum tap, on the f₀ reading. The morph may therefore never push the sub below what the resolved Anchor provides (cancellation by the regenerated fundamental is the hazard); it is not required to hold A's weight in Blend, which contradicted SC-015's "Lock ≥ Blend + 2 dB" for every B level (the two needed ≥ 0.89 and ≤ 0.79). Also, Punch's E_sub(p) ≥ Blend's − 0.1 dB at every p. The expected sign holds because Mass is 50 at both ends: the effective Anchor level is the resolved level, and Punch's 1 − 0.3p³ ≥ Blend's 1 − 0.3p (FR-051). E_sub,out(p) and the dynamics gain E_sub,out − E_sub are recorded per p and mode (D-13; ruled 2026-10-19, Q2: the pre-dynamics sum tap is the only gated tap, the output tap is recorded). |
| **SC-009** | Recipe domain, never an audio crossfade (FR-038) | `BassMorph_RecipeDomain`. (a) Compile-time: `BassMorphEngine` has no member taking an audio buffer. (b) At p ∈ {0.25, 0.5, 0.75} for the showcase recipes. Mass is 50 at both ends, as in the showcase pair, so every `MassLaw` row returns its base unchanged (mass_law.h:136–142) and the core receives the resolved recipe unbiased. The position is held static until the core's shape has converged through its per-interval step ceiling (profundum_core.h:142–147). Then `core.shapeGains()` is **bitwise** equal to `SpectralShapeRecipe::evaluateShape` of the **resolved** recipe (`shapeOf`, spectral_shape_recipe_test_helpers.h:168–171). Bit equality after convergence is the Phase 1 precedent (profundum_core_test.cpp:607, 641, 1729), so the tolerance is 0. Positive control: the 50 / 50 audio crossfade of renders A and B differs from the p = 0.5 render by ≥ 1 dB in at least one harmonic, which proves that the test can tell the two apart. |
| **SC-010** | Global position (FR-015, FR-016) | `BassMorph_Global`. A 4-voice `renderMorphPoly` under a 0 → 1 ramp, at host blocks {7, 512}. The driver calls `advanceInterval` **exactly once per grid point**: the call count, kept by the fixture, equals ⌈total / 32⌉, and each call falls at a multiple of 32 of the absolute sample counter. Every voice receives the same `Resolved` (FR-005a) in each interval. |
| **SC-011** | RT safety (FR-080) | `BassMorph_RealTimeSafety`. `AllocationScope` around every audio-thread method (including `serializeRuntime` / `restoreRuntime`), over 10 s of random automation and endpoint replacement, records **0** allocations. A `static_assert` checks `noexcept` on each method. |
| **SC-012** | Non-finite safety (FR-081) | `BassMorph_NonFinite` (`-fno-fast-math` TU). NaN / ±Inf / denormal positions; per-field NaN / ±Inf / out-of-range / undeclared-enum endpoints; corrupted serialised bytes (state and runtime). The resolved state stays finite and `isValidMorphState` at every interval; invalid endpoints are rejected; the previous position is kept. |
| **SC-013** | Determinism and partition invariance (FR-082) | `BassMorph_Partition`. The automation is a list of **sample-stamped events**, each applied at the first grid point at or after its stamp (FR-070). The host block partition only changes where the driver splits the host buffer. Host block sizes {1, 7, 32, 64, 511, 4096} and two independent engines give bitwise-identical `Resolved` sequences (FR-005a) for the same event list. |
| **SC-014** | Sample rates (FR-011, FR-020, FR-021) | `BassMorph_SampleRates`. At 22.05, 44.1, 48, 96 and 192 kHz, and at ramp times `kMinPositionRampMs`, the default and `kMaxPositionRampMs`: SC-001 identity holds; the traversal time is within one interval of rampMs; out-of-range rates clamp. |
| **SC-015** | Audibility gate (FR-084) | `BassMorph_Audibility`. On the showcase pair, at E1 and C3, between p = 0 and p = 1: D_cx, D_nl and D_st each move ≥ **3 dB** and D_br ≥ **0.25 oct** (steady render), and D_t (gate render) moves by a ratio of ≥ **2** (drafted D-8, the Phase 5 OQ-S bar family). The Anchor morph mode: at p = 1, E_sub in Lock ≥ E_sub in Blend **+ 2 dB** (f₀ reading, sum tap). The expected sign: Lock keeps A's Anchor level 1.0, Blend lands on B's 0.5, and Mass is 50 at both ends; harvested margin +0.68 dB at both notes (the regenerated core fundamental, 0.36 × the Anchor and near in phase on this B, dilutes the 6 dB level difference to ≈ 2.7 dB at the sum). |
| **SC-016** | Mono compatibility along the morph (FR-085) | `BassMorph_MonoSum` (`[long]`). At 21 positions of the showcase morph and through the SOLID → WIDE step, at E1 and C3: the Anchor tap is bitwise L == R at every interval; the sub-band mono-sum Q_sub is within `kSubBandQBarDb` (1.0 dB) and the correlation is ≥ `kSubBandRhoBar` (0.99), using Phase 7's helpers. |
| **SC-017** | Anti-aliasing while morphing (FR-086) | `BassMorph_Aliasing` (`[long]`). A max-nonlinear pair (A Fold 0, Character Tube Amount 0; B Fold 100, Character Fracture Amount 1, excitation mix 1, drive 1), at E0, E1 and E2, at 44.1 / 48 / 96 kHz. **Method: a frozen-position grid.** A stationary spectrum of a parameter sweep counts the sweep's own sidebands as aliasing, and `aliasResidualDb` (mass_dynamics_test_helpers.h:865) has an ideal reference only for a gain modulation. (a) The grid is p ∈ {0, 0.05, …, 1}, plus the two arms of the algorithm crossing (p = θ_up − 0.005 with A's algorithm selected, and p = θ_up with B's). **Ratified 2026-10-19 (plan C-16):** on the max-nonlinear pair the FR-034 governing-amount rule forces B's algorithm for every p in (0, 1) (A's Amount is 0), so the two arms are measured on a second pair, `algorithmCrossingPair` — both sides at the max-nonlinear settings (Fold 100, excitation mix 1, drive 1, Character Amount 1, Tone 0.5), A Tube, B Fracture — where both governing amounts are non-zero and the switch falls at θ_up between two live nonlinear arms; the test asserts the Character selection is A at θ_up − 0.005 reached from below and B at θ_up before reading. The 21-point grid stays on the max-nonlinear pair as worded. The (b) crossfade-window record is placed at the switch grid sample of `algorithmCrossingPair`'s ramp, and the same record is written for the max-nonlinear pair at its first interval with p > 0. Each grid point is a static steady render (snapped, 1 s settle). The aliased power is Phase 5's stationary measure, as `StereoFieldTest::wideAliasedDbfs` applies it (bass_stereo_field_test_helpers.h:1120): `HarmonicFoldTest::aliasedPower` over the last `aliasFftLength(fs, f)` samples, with f = f₀/2 for Fracture and f₀ otherwise. It is read on each channel of the character tap and must be ≤ **−80 dBFS** at every grid point. (b) The crossfade window is covered by composition, not by a stationary read. The stage mixes two separately oversampled branch outputs after their downsample, with fixed C-16 gains (bass_character_stage.h:24–30), so the window adds no nonlinear product beyond the two arms that (a) gates. A 1 s ramp render is recorded as evidence: the non-harmonic in-band energy over a Hann window spanning the pre-roll and the crossfade **and at least 8 periods of f₀** (reading refinement by the main loop, 2026-10-19, T015-4: with T = 1/f₀ + 10 ms the ±2/T exclusion exceeded f₀/4 for every f₀ ≤ 300 Hz and excluded every in-band bin; Phase 7 G10-6 used the same 8-period floor), excluding bins within ±(2 / T_window) of every k·f₀. |
| **SC-018** | CPU (FR-080; drafted D-10) | `BassMorph_Cpu` (`[performance]`, run alone and pinned, median of 3). `advanceInterval` with every field differing costs ≤ **0.05 %** of one core at 48 kHz (drafted; the roadmap sets no Phase 8 bar). This is charged to Phase 9's ≈ 1.5 % globals (roadmap lines 642–643). The worst simultaneous-crossfade interval is a recorded Phase 9 handoff measurement (FR-083), not part of this SC. |
| **SC-019** | Law-table completeness (FR-030, FR-003) | `BassMorph_LawTable`. (a) **Drift detection at compile time.** `morph_state.h` holds `static_assert(kMorphStateBytes == 1 + Σ` row sizes`)` and `static_assert(kMorphLeafFieldCount ==` table size`)` (FR-030). The test TU holds structured-binding arity checks with the exact member count, `auto& [m1, …, mN] = s;`, for `MorphState`, `AmpEnvelopeFields`, `MotionConfig` and the four composed aggregates (`SpectralShapeRecipe::Controls`, `ProfundumCore::ExcitationControls`, `BassStereoField::Controls`, `AnchorOscillator::Controls`), plus a `static_assert` that the sum of the leaf counts equals `kMorphLeafFieldCount`. Adding a member to any of these structs then fails to compile until the binding, the law table and the serializer are updated together; the test's comment says so. (b) **Law per field.** The Anchor morph mode is **Blend**, so p_a = p and the Anchor fields are evaluated at p_a. For each leaf field in turn, B differs from A in that field alone, and the engine is reset at p = 0.25. Exactly that field changes; every other field is bitwise equal to A. Its value equals the tabled law's closed form, evaluated in double, within \|Δ\| ≤ **2 ulp** of max(\|x_A\|, \|x_B\|) for linear and equal-power, and **8 ulp** for logarithmic (float `log2` / `exp2`, FR-032). A stepped field resolves to A at p = θ_up − 0.005 reached from below, and to B at p = θ_up. **Governing-amount rows (Q1):** for each row of the FR-034 table, with the governing amount 0 on A's side (and, separately, on B's side) and the discrete field differing, the field resolves to the non-silent side at p ∈ {0.01, 0.25, θ_up − 0.005, 0.99} and to the identity side at p = 0 and p = 1; with both amounts non-zero, θ applies; with both zero, θ applies. (c) No FR-003-excluded field exists (the bindings name every member). |
| **SC-020** | Validity closure (FR-036, FR-005) | `BassMorph_ValidityClosure`. 100 000 random valid pairs × random p × each mode: `isValidMorphState(resolved)` is true, and every continuous field lies in [min, max] of its endpoints. |
| **SC-021** | Lifecycle (FR-011) | `BassMorph_Lifecycle`. (a) Before `prepare`: after `setEndpoint` × 2 and `setPosition(0.7)`, `advanceInterval` returns a state bitwise equal to a prepared engine's after `reset()` at 0.7, with p == 0.7 exactly (no ramp). (b) The `reset()` selection rule: an engine swept 0 → 0.5 (selection A) keeps A after `reset()` at 0.5, and one swept 1 → 0.5 keeps B. `reset()` at 0.53 gives B, and at 0.47 gives A, from either side. A fresh engine reset at 0.5 gives A. |
| **SC-022** | Ramp-time change (FR-021) | `BassMorph_RampTimeChange`. `setPositionRampMs` is called mid-traversal, and at the range ends: 5 ms, 40 ms and 2000 ms each give a traversal of ⌈rampMs·fs / (1000·32)⌉ ± 1 intervals (SC-003(c)); 4.9 ms clamps to 5 ms and 2001 ms to 2000 ms; NaN keeps the previous value. p at that interval is bitwise equal to p in a reference run without the call, and from the next interval on, \|Δp\| equals the new Δp_max within the SC-003(a) tolerance, i.e. \|\|Δp\| − Δp_max\| ≤ 4·FLT_EPSILON·Δp_max + 0.5·ulp(max(\|p_old\|, \|p_new\|)), until p lands (the landing step is the remainder and is excluded; build ruling B3-1, 2026-10-19). |
| **SC-023** | Drop latched (FR-039; Phase 4 D-4) | `BassMorph_DropLatched`. A: Drop 12 st / 200 ms / Shape 0; B: Drop 36 st / 2000 ms / Shape 1. A note is triggered at p = 0, and the morph then ramps 0 → 1 while the Drop is in flight. `DropEnvelope::currentOffset()` (drop_envelope.h:148), read per sample, is bitwise identical to an unmorphed render of A. The next note-on, after p = 1, follows B's values: its trajectory is bitwise identical to an unmorphed render of B triggered at the same sample. |
| **SC-024** | Endpoint edit mid-morph (FR-061) | `BassMorph_EndpointEditMidMorph`. At p = 0.3, in Blend, B is replaced by B′, which differs in every continuous field. For each continuous field, the resolved change at the next interval equals law(p; A, B′) − law(p; A, B) evaluated in double, within the SC-019(b) ulp tolerances (for the linear law, Δendpoint × p). The change is complete in that one interval (no engine ramp). Discrete case: editing the algorithm of the selected endpoint (A) changes the resolved algorithm at the next interval, the fixture issues `setAlgorithm` exactly once, and the render meets SC-004(a, b) against the direct-setter reference. |

---

## Edge Cases

- **Jumps and automation.** A host jump 0 → 1, or a 0 ↔ 1 square wave, is rate-limited (FR-020). Automation that
  reverses mid-ramp re-targets without a step. A jump that crosses θ several times inside one ramp switches at most
  once per crossing, with hysteresis.
- **One side silent on a discrete field** (Q1). The other side's choice is selected for every q in (0, 1); the
  processor of the silent side never runs mid-morph. An endpoint edit that un-silences that side hands over to the
  hysteresis rule from the current q (FR-034 rule 3).
- **Position held at a threshold.** Hysteresis (θ_down < θ_up) keeps discrete fields from chattering (SC-004(c)).
  `reset()` inside (θ_down, θ_up) keeps the current selection; a fresh engine selects A (FR-011, SC-021).
- **A switch requested while one is in flight.** `BassCharacterStage` queues the last request (bass_character_stage.h:246–252).
  A morph that crosses θ back and forth faster than 10 ms + one period therefore ends on the last side requested,
  never on a third state.
- **Anchor mode change mid-morph.** p_a re-targets and slews (FR-051). Lock → Blend at p = 1 moves the Anchor from A to
  B over the ramp time, without a step.
- **Lock at p = 1.** The Anchor block equals A, by design (FR-052). SC-001 tests that exception explicitly.
- **Equal endpoints.** The output is constant and bitwise equal to A (FR-035, SC-002).
- **Log-law fields at their minima** (1 ms Drop, 0.1 ms envelope, 60 Hz crossover). log2 is defined on every valid box
  (FR-032). A deserialised or set value of 0 is invalid and rejected (FR-005).
- **Equal-power with one endpoint at 0.** m(p) = √p·m_B has an infinite slope at p = 0, but the per-interval change
  is bounded by √Δp_max·m_B (SC-003(b)), and the receiving smoothers (5 ms excitation, Anchor feed) absorb it.
- **Endpoint edited mid-morph.** The resolved fields move at once, and the components' own smoothing applies
  (FR-061). An edit to a discrete field on the currently selected side switches through the component's transition.
- **Drop in flight.** Unaffected. The morph acts on the next trigger (FR-039, Phase 4 D-4).
- **Sample-rate change.** `prepare` reconfigures the rate limit. The ramp time in ms is preserved, and so are the
  endpoints, the position target and the mode. `reset` snaps (FR-011).
- **Before `prepare`.** Setters store; `advanceInterval` resolves the stored targets without ramping (FR-011, SC-021).
- **State reload mid-morph.** `restoreRuntime` reinstates the ramped p and p_a, the target and both selections, so a
  reload inside the hysteresis band or mid-ramp reproduces the live state exactly (FR-062, SC-007(c)).
- **Non-finite or out-of-range input.** It is filtered or rejected at the boundary and never reaches `Resolved`
  (FR-081).
- **Seed determinism.** The engine has no random source. Seeded randomness exists only in the components it feeds
  (`BrownianDrift`, the field's drift seed), which are excluded from `MorphState` (FR-003) and stay deterministic per
  their own phases.
- **POLY.** Every voice reads one resolved state. A voice triggered mid-morph latches its Drop at that interval's
  values; voices triggered earlier keep theirs (FR-016, FR-039).

---

## Decisions (drafted for the clarify / plan stages to rule)

| ID | Decision | Draft | Rationale |
|---|---|---|---|
| D-1 | Step threshold and hysteresis (FR-034) | θ_up = 0.52, θ_down = 0.48, one pair for all discrete fields, **preceded by the governing-amount rule** | Phase 5 FR-057: "switches at a threshold morph position". Hysteresis prevents chatter under automation jitter. **Ruled 2026-10-19 (Q1): amount-aware selection**; the fixed-threshold-only and staggered-threshold alternatives were rejected. |
| D-2 | Equal-power law form and fields (FR-033) | m(p) = √((1 − p)·m_A² + p·m_B²); `excitation.mix`, `anchorFoldFeed` | Constant when m_A = m_B, monotone, exact by selection at the ends. A cos / sin weighting of two parameter values raises a constant mix by up to √2. |
| D-3 | What Lock holds (FR-051, FR-052) | The whole Anchor block at A | "Lock (constant)" (line 142). Holding only the level would still let the waveform and mode switch. **Ruled 2026-10-19 (Q5).** |
| D-4 | Punch curve (FR-051) | c(p) = p³, fixed | Late-yielding (c(0.5) = 0.125), C¹ at 0, monotone, c ≤ p. **Ruled 2026-10-19 (Q6): fixed constant, no user exponent.** |
| D-5 | Mass split at the Anchor (FR-054) | `anchorMass` = Mass at p_a | Without it, Lock is not constant whenever Mass differs between A and B (mass_law.h:110). |
| D-6 | Position ramp (FR-020, FR-021) | Rate limit; 0 → 1 in 40 ms by default; **user control 5–2000 ms (ruled 2026-10-19, Q3)** | Matches `kMassSweepMs`. Correctness does not depend on the ramp outlasting a component transition: each discrete field steps once per threshold crossing and the component's own transition and queue own the audio (FR-021); SC-003, SC-004 and SC-014 run at the minimum, default and maximum ramp times to prove it. |
| D-7 | Default Anchor morph mode (FR-014) | Blend | The concept's example (lines 495–505) is Blend. |
| D-8 | Showcase pair, ε, loudness-dip, Lock-floor and audibility bars (SC-008, SC-015) | As written in the descriptor block and the SC rows | Bar families reuse ratified ones: Phase 5's 3 dB / 0.25 oct distinctness bar and Phase 3 / 7's 1 dB sub-band bar. **The 1.5 dB loudness-dip bar is ruled 2026-10-19 (Q7), remediated in the laws or the pair, never by engine gain compensation.** The ε values and the SC-015 bars stay drafted for the plan stage. The pair holds Mass at 50 at both ends so the Anchor-mode clauses have a defined sign (mass_law.h:110, 136–142); Mass is gated on the Mass-only pair (SC-005(e)). Steady descriptors are read with the amp envelope bypassed, D_t on a gate render as an effective duration, so every descriptor is defined and uncapped at every p. |
| D-9 | Click bound at discrete steps (SC-004) | max |Δy| ≤ 10^(0.5/20) (≈ 1.059) × the same transition issued directly through the component's setter | The morph adds no click of its own. Each component's absolute bound was ratified in its own phase. |
| D-10 | CPU bar (SC-018) | ≤ 0.05 % of one core for `advanceInterval` | The roadmap sets no Phase 8 bar. This reserves a small slice of Phase 9's ≈ 1.5 % globals. |
| D-11 | Crossover law (FR-032) | Logarithmic | Roadmap line 587 ("logarithmic (frequencies, times)") over Phase 7's evidence note ("linearly"). |
| D-12 | Transient attack / sustain gains in `MorphState` (FR-002) | Included, linear in dB | They are user-facing sound controls (Phase 6 handoff 7, line 491), and line 584's exclusion list does not cover them. |
| D-13 | Lock-floor tap and modes (SC-008(d), descriptor block) | Gated in **every** Anchor morph mode, at the pre-dynamics sum tap, against the per-position F_lock(p) (the Lock render's Anchor tap alone); the output reading is recorded | Roadmap lines 606–607 carry no mode qualifier, so no mode gets a weaker floor. The sum tap puts E_sub and F_lock on the same side of the Mass-driven compressor and trim (mass_law.h:108–111). **Ruled 2026-10-19 (Q2): the pre-dynamics sum tap only is gated; the output tap is recorded.** Mass is 50 at both showcase ends, so the dynamics do not move along the morph; gating before the morph-independent gain stages tests the morph, not the compressor. |
| D-14 | `MotionConfig` and `AmpEnvelopeFields` field sets (FR-002) | Fixed in this phase (**ruled 2026-10-19, Q4**): the FR-002 MotionConfig table, including the Phase 12 advanced fields, and the ADSR set with float curves; `cycleFadeInMs` uses the log law on (t + 1 ms); fade-in upper bound 5000 ms; Phase 9 appends only | Covers every Motion control the roadmap names (lines 629–632, 704–705, 765–767); the offset keeps the roadmap's "logarithmic for times" law defined at the LFO's valid 0 ms (lfo.h:295–298). |
| D-15 | Runtime state round trip (FR-062) | Serialise the ramped p, p_a, target, mode, ramp time and both selections; restore without `reset()`; restore re-checks the selections against the FR-034 rule including the governing amounts | A target-only restore flips a selection inside the hysteresis band and loses a mid-ramp position, which breaks roadmap lines 605–606. |

---

## Open Questions (all resolved)

Both questions the roadmap deferred to this spec are ruled (2026-10-19; see Clarifications). No question remains open.

- **OQ-1 — RESOLVED (Q8 and Q3).** (a) Whole-endpoint `setEndpoint` only; edits are not ramped by the engine; copy /
  compare are controller-side (FR-012, FR-060, FR-061, SC-024). (b) The ramp time is a user control, 5–2000 ms,
  default 40 ms (FR-021, SC-022).
- **OQ-2 — RESOLVED (Q4).** Both field sets are fixed now (FR-002, D-14); Phase 9 appends only, under a format-version
  bump (FR-007), and records unrealisable fields as deviations (FR-083). SC-019 covers the whole state.

---

## Traceability

| Roadmap statement | FR / SC |
|---|---|
| `MorphState` "every sound-shaping control …" (lines 581–583) | FR-001, FR-002, FR-004; SC-019 |
| "Excluded: note behaviour, output level, polyphony" (line 584) | FR-003; SC-019 |
| "Serialisable, validated (`isValidMorphState`)" (line 584) | FR-005–FR-007; SC-007, SC-012, SC-020 |
| "holds A and B, resolves one state per control block from a smoothed, automatable morph position" (lines 585–586) | FR-010–FR-015, FR-020–FR-022; SC-003, SC-013, SC-014 |
| "per-parameter interpolation laws: linear; logarithmic (frequencies, times); equal-power (mixes); stepped-with-crossfade or algorithm-specific transition" (lines 587–589) | FR-030–FR-034, FR-039; SC-003, SC-004, SC-019 |
| "the core's spectrum is derived from the *interpolated* D/B/E/S … never an audio crossfade" (lines 589–590) | FR-038; SC-009 |
| "five perceptual axes has a named law" (lines 592–596) | FR-040; SC-008, SC-015 |
| "Anchor morph modes: Lock / Blend / Punch … concept's example … Blend … Punch is the asymmetric-curve generalisation" (lines 597–599) | FR-050–FR-055; SC-005, SC-008(d) |
| "global: one morph position, all voices follow" (line 600) | FR-016; SC-010 |
| SC "morph 0 reproduces A and 1 reproduces B exactly" (line 603) | FR-035, FR-052; SC-001, SC-002 |
| SC "bounded per-block change … including 0 → 1 jumps (the position is ramped)" (lines 603–604) | FR-020; SC-003 |
| SC "discrete transitions click-free (sample delta bound at the crossfade)" (line 604) | FR-034; SC-004; FR-083 (MotionConfig selectors, Phase 9 handoff gate) |
| SC "Anchor mode curves measured" (line 605) | SC-005 |
| SC "A → B → A idempotent" (line 605) | SC-006 |
| SC "both states and the position serialise round-trip" (lines 605–606) | FR-006, FR-062; SC-007(c, d) |
| SC "descriptor trajectory … monotonic on each of the five perceptual axes … no loudness dip ≥ a ruled dB, sub-band energy never below the Lock floor" (lines 606–607) | SC-008 |
| Open Question 2 (lines 963–965); Phase 11 amendment (lines 702–705) | FR-060–FR-062, FR-021; OQ-1; SC-022, SC-024 |
| Lifecycle, latched Drop (FR-011, FR-039; Phase 4 D-4) | SC-021, SC-023 |
| Motion-section configuration (lines 582–583, 765–767) | FR-002 MotionConfig table; D-14; OQ-2 |
| Cross-cutting: audibility, mono, anti-aliasing, RT, layer / ODR, CPU, portability, shared headers (lines 933–958) | FR-080–FR-089; SC-011–SC-018 |

## Assumptions

- Phases 1–7 are complete on `synth/profundum` at 964045a4, and the headers cited above are as read on 2026-10-19.
- Phase 9 consumes `Resolved` on the 32-sample grid that the core, the Mass law and the field already share.
- Phase 11 owns thread hand-off of endpoint edits (UI → audio). The engine's setters are audio-thread calls.

## Clarifications

### Session 2026-10-19 (specify-stage rulings; each was written into its FR / SC row first)

- **Q1 — Where does a discrete field step when one endpoint's governing amount makes that side silent?** Ruled:
  **amount-aware selection.** If exactly one endpoint's governing amount is 0, the other endpoint's choice is selected
  for every position in (0, 1), in both directions; otherwise the θ_up 0.52 / θ_down 0.48 hysteresis applies; identity
  at 0 / 1 holds by selection. The governing-amount table (Character Amount, field Spread, field motionDepth, Anchor
  level, motion amount) is in FR-034; the Anchor *mode* sharing the Anchor level as its governing amount is a
  refinement I resolved (at level 0 the mode is as inaudible as the waveform). Fixed-threshold-only and staggered
  thresholds were rejected. [FR-034, FR-062, SC-004(d), SC-007(c), SC-019(b)]
- **Q2 — At which tap is the Lock floor gated?** Ruled: **the pre-dynamics sum tap only**; the output-tap reading and
  the dynamics gain are recorded, not gated. Mass is 50 at both showcase ends, so this tests the morph, not the
  compressor. [SC-008(d), descriptor block E_sub / F_lock, D-13]
- **Q3 — Is the position ramp time a user control?** Ruled: **yes**, `setPositionRampMs` over **5–2000 ms**, default
  **40 ms**; the runtime blob carries it; Phase 11 registers "Morph smoothing"; SC-003 / SC-004 / SC-014 run at the
  minimum, the default and the maximum. [FR-021, FR-062, SC-003, SC-004, SC-007(c), SC-014, SC-022]
- **Q4 — Are the amp-envelope and Motion-section field sets fixed now?** Ruled: **fix both now** — the ADSR set with
  float curve amounts, and the full `MotionConfig` including the Phase 12 advanced fields. Phase 9 may only append,
  under a format-version bump, and records any field it cannot realise as a deviation. SC-019 covers the whole state
  from this phase. [FR-002, FR-007, FR-083, SC-019]
- **Q5 — What does Lock hold?** Ruled: **the whole Anchor block at A** (level, waveform, mode, punch / ghost times,
  every other Anchor control, and `anchorFoldFeed`). B's Anchor edits are stored but inaudible while Lock is active;
  the engine does not mirror them; the UX goes to Phase 12 as a handoff. [FR-050, FR-052, FR-055, FR-083, SC-001,
  SC-005(a)]
- **Q6 — Is the Punch curve a control?** Ruled: **fixed c(p) = p³**, no new parameter. [FR-051, SC-005(c)]
- **Q7 — Loudness-dip bound and remediation?** Ruled: **1.5 dB** K-weighted below min(L_K(0), L_K(1)); a red is fixed in
  the interpolation laws or the showcase pair, **never** by a loudness-compensation output in the engine. [SC-008(c),
  FR-038, D-8]
- **Q8 — Endpoint-edit API?** Ruled: **whole-endpoint `setEndpoint` only.** Phase 11 keeps a copy, edits one field and
  re-sets the endpoint; one invalid field rejects the whole edit; edits are not ramped by the engine. [FR-012, FR-060,
  FR-061, SC-024]

Drafted values the plan stage still owns (not ruled above): SC-008(a) ε, the SC-015 bars (D-8), the click bound D-9,
the CPU bar D-10, the crossover law D-11 and the transient-gain inclusion D-12. The plan adopted them as written
(plan C-15).

### Session 2026-10-19 (plan-stage rulings; each was written into its FR / SC row first)

- **C-2 — Selection state.** Ratified: **one selection per governing-amount group** (Character, ImageMode, FieldMotion,
  Motion at p; Anchor at p_a). The runtime blob keeps two selection bytes; the main byte is a 4-bit group mask, the
  Anchor byte 0 / 1; restore validates per group. [FR-034, FR-062, SC-007(c), SC-019(b)]
- **C-4 — Law precision.** Ratified: **every law is evaluated in double and rounded once to float**; the log law caches
  the endpoints' log2 at `setEndpoint`. FR-031's "evaluated in float" is amended. [FR-031, FR-032, FR-033, SC-019(b)]
- **C-16 — Anti-aliasing crossing arms.** Ratified: the two arms of the algorithm crossing and the crossfade-window
  record are measured on `algorithmCrossingPair` (both sides max-nonlinear, Amount 1, A Tube, B Fracture); the 21-point
  grid stays on the max-nonlinear pair; dropping the arms was rejected. Same −80 dBFS bar. [SC-017(a), SC-017(b), FR-086]
- **Tasks deviation 10 — SC-004 window for image-mode and field-motion steps.** Ratified: the window runs to the end of
  the full 334-interval pan slew, not 10 ms; reference and transition share the window. [SC-004(a), SC-004(b)]
- **C-1 — Field-table location** (resolved by the main loop; code placement, no behavioural change): the one table is
  `detail::morph_state_io::kFieldTable` in `morph_state.h`, re-exported by the engine. [FR-030, SC-019(a)]
- **Tasks deviation 2 — `dsp/CMakeLists.txt`** (resolved by the main loop): the two new headers are listed there, as
  every earlier phase did; the plan's "no other CMake file changes" is amended. [FR-089]
### Session 2026-10-19 (build rulings; each was written into its SC row first)

- **B3-1 — Per-interval step bar and float rounding of the position (T003 stop).** Ruled: SC-003(a) and SC-022 read
  \|Δp\| ≤ Δp_max·(1 + 4·FLT_EPSILON) **+ 0.5·ulp(max(\|p_old\|, \|p_new\|))** for p and p_a. The rate limiter adds the
  step in float, so each step carries up to half an ulp of the position, which the relative 4·ε term cannot cover
  when Δp_max is small (2000 ms / 192 kHz: 2392 ε relative, 2.4e-8 absolute). The engine stays on `SlewLimiter`
  (FR-020); the alternative (an engine-side step rounded toward the old value) was rejected. Only the two tolerance
  expressions in the test changed. [SC-003(a), SC-022]

- **B7-1 — SC-008(d) floor in Blend and Punch (T008 probe stop).** Ruled: **the floor is at the resolved Anchor
  level** — Lock: E_sub ≥ F_lock(p) − 1 dB; Blend / Punch: E_sub ≥ F_lock(p)·L_m(p)/L_A − 1 dB (the sum's f₀ never more
  than 1 dB below that render's own Anchor tap); Punch ≥ Blend − 0.1 dB kept. The "every mode ≥ F_lock − 1 dB" clause
  needed B's Anchor level ≥ 0.89 while SC-015 needed ≤ 0.79. Gate-Lock-only was rejected. [SC-008(d), D-13]
- **B7-2 — Showcase pair B.** Ruled: **harvest B from the main loop** (Q7's "remediated in the showcase pair"): grid over
  recipe {kGrowl e1, kHeavy e1} × Fold {80, 50, 30} × SHAPE {1, 0.5} × Tone {0.8, 0.5} × Character Amount {0.8, 0.5} ×
  Anchor level {0.7, 0.5}, 11 positions, E1 and C3, Lock and Blend, f₀ reading; the pick is the largest worst-case
  floor margin that meets the SC-015 travel bars and the Anchor clause (≥ 2 dB at the sum tap). No bar changed.
  [FR-071, SC-008, SC-015, descriptor block]
- **Reading refinement (main loop): E_sub and F_lock on the f₀ component** (Phase 6's `dftDb`), not the 1.5·f₀ low-pass
  RMS, which reads h2 leakage at the monster end (false +1.9 dB pass vs −1.16 dB real). [descriptor block, SC-008(d)]
- **B7-2 result (main loop, 2026-10-19).** Three harvest runs (evidence/harvest/run1–run3, aggregate-showcase.js): run 1
  and run 2 had 0 of 96 candidates passing, each time because of one reader (D_nl's core-tap denominator; then the
  absolute floor), both refined below; run 3: 19 of 96 pass, all `kHeavy` at Anchor level 0.5 (every `kGrowl` variant
  fails the Anchor clause by 0.25–4.4 dB and every level-0.7 variant by 0.3–0.7 dB). **Pick: candidate 59** — `kHeavy`
  edge 1, Fold 80, SHAPE 0.5, Tone 0.8, Character Amount 0.5, Anchor level 0.5 (floor 5.89 / 6.91 dB, Anchor clause
  +0.68 dB, D_br travel 0.63 / 0.56 oct, D_nl 14 dB). Deviation from the floor-first tiebreak, flagged for the user:
  candidate 63 (Tone 0.5) led on floor (6.92 dB) but cleared the D_br bar by 0.04 oct and the Anchor clause by 0.2 dB.
  Pasted into `showcasePair()`; the probe re-run on it is evidence/coherence-probe.txt. [FR-071, SC-008, SC-015]
- **B9-1 — SC-005(e) Mass-only morph gate (T015 stop).** Ruled: gate on **Phase 6's Mass audibility bar** (its SC-011:
  ≥ 3 dB on any of E_low, E_high, THD or I, or ≥ 0.25 oct on C, Phase 6's readers), not on D_nl. Mass 100 lands the
  Fold row in the compression regime, which lowers n ≥ 9 by design (measured −6.44 dB travel). The unsigned |ΔD_nl|
  reading the Group 8 fixer had applied (G8-2) was reverted by T015 and is rejected; D_nl stays recorded. [SC-005(e)]
- **Reading refinement (main loop, T015-4): the SC-017(b) record window is at least 8 periods of f₀**, so the ±2/T
  harmonic exclusion leaves in-band bins to read. Recorded clause, no bar. [SC-017(b)]
- **Re-tag decision (main loop, close step 2):** the per-push `[bass-morph]` lane with `-D 15` (evidence/run-log/close/
  perpush-durations.log, 51 cases, 'All tests passed') has one case over 15 s, `BassMorph_SampleRates` at 32.3 s. It
  stays per-push: it is a bounded-grid / sample-rate sentinel of the kind the `[long]` rule never re-tags.
- **B10-1 — SC-004(b) click bar.** Ruled: the transition's differential clicks are **≤ the direct-setter reference's
  own** at the same grid point (D-9: the morph adds no click of its own); both counts recorded; component clicks
  (Fracture → Crush / Rectify at C3; OctaveDownSine → X at E1, 5 ms ramp) handed to Phase 9. [SC-004(b), FR-083]
- **B10-2 — SC-004(a) on the governing-amount rows.** Reading refinement (main loop): the reference is the same morph
  with the discrete field pre-set to the non-silent side (no step). Ruled: for the **ImageMode** rows (a) is recorded
  and (b) gated, because the excess (R 1.068 / 1.071 vs 1.059, 0 clicks) is the G7-2 engagement ramp of WIDE's tone
  offset measured against an instantly engaged reference. [SC-004(d)]
- **B10-3 — SC-006(b) idempotence.** Ruled: the random pairs also constrain **A's Fold to ≤ kSoftEnd (30)**; clamped
  pairs are rendered unclamped and recorded; HarmonicFold's D-4 detector hysteresis (0.13 dB, non-decaying) is a Phase 9
  handoff. Bar 0.1 dB unchanged. [SC-006(b), FR-083]
- **B10-4 — SC-008(a) taps.** Ruled: D_st monotonicity on the **clean core tap**, D_t on the **pre-dynamics sum**; output
  readings recorded; ε and the SC-015 travel bars unchanged. [descriptor block, SC-008(a)]
- **Reading refinement (main loop): D_nl against the linear core.** The fixture runs a twin core with the excitation at
  mix 0; D_nl = n ≥ 9 power at the character tap over the same power of the linear core. Against the core tap (which
  carries the excitation nonlinearity) every harvest candidate read a negative D_nl travel. [descriptor block, SC-008,
  SC-015]
- **Reading refinement (main loop): descriptor floors are relative.** D_cx and D_nl floor each power at −60 dB below
  the reference tap's fundamental, not at −120 dB absolute, which had turned A's −100 dBFS ROUND residue into +11.7 dB of
  D_nl (harvest run 2). D_st keeps Phase 7's floors. [descriptor block, SC-008, SC-015]
- **C-9 and pre-triaged P-2 are not rulings yet:** the T008 sum-tap coherence probe decides whether SC-015's Anchor
  clause and SC-008(d) can be green on the showcase pair; a red stops the build and comes to the user with the
  showcase-pair options (B's Anchor level, Fold amount or SHAPE). No bar is relaxed.

## Review notes

Spec review, 2026-10-19 (fidelity and testability lenses). Every issue was applied. The notes below cover the
places where the resolution differs from the reviewer's suggestion, or accepts it only in part.

- **Round trip (FR-062, SC-007(c)).** Option 1 was adopted: a runtime blob (`serializeRuntime` / `restoreRuntime`)
  carries the ramped p and p_a, the target, the mode, the ramp time and both selections, and restore never calls
  `reset()`. `reset()` was also changed so that it keeps the selection inside the hysteresis band, instead of
  re-deriving p ≥ 0.5 → B. The old rule could select a side the live engine never held. The old edge case "exactly at
  0.5 after `reset()`, B is selected" is withdrawn.
- **Lock floor modes (SC-008(d)).** The floor is gated in every mode, as the roadmap reads. There is no mode-dependent
  departure. D-13 flags only the choice of tap (the pre-dynamics sum) for the clarify stage.
- **D_t and the steady descriptors.** The showcase envelope values are kept. Instead, the steady descriptors are read
  with the amp envelope bypassed, and D_t is redefined as an effective duration on a gate render, with a hard
  render-length limit that fails rather than caps. This was preferred over changing A's sustain to 0.5, because the
  pair keeps its "pure weight" short envelope as the short end of the short → long axis.
- **Showcase Mass.** The pair now holds Mass 50 at both ends, and Mass gets its own Mass-only pair (SC-005(e)). The
  reviewer cited the AnchorLevel row at mass_law.h:109. The row is at **mass_law.h:110** (109 is CompressionAmount,
  108 is FoldAmount), and the spec cites 110. The arithmetic in the issue is correct.
- **Ramp range (FR-021, D-6).** `kMinPositionRampMs` stays at 5 ms (drafted), and the D-6 rationale was corrected
  rather than the minimum raised. A ramp shorter than a component transition is correct by construction (one step per
  crossing, with the component's queue). SC-003, SC-004 and SC-014 now run at the minimum, the default and the
  maximum ramp time to prove it.
- **Anti-aliasing during the crossfade (SC-017(b)).** This was accepted in part. The frozen-position grid, including
  both arms of the algorithm crossing, is gated at −80 dBFS. The 10 ms crossfade window has no stationary reference.
  It is covered by composition, because the stage mixes two separately oversampled, already-downsampled branch
  outputs with fixed gains (bass_character_stage.h:24–30) and adds no new nonlinear product. Its windowed
  non-harmonic energy is therefore recorded as evidence and not gated. A gated stationary read of a 10–34 ms window
  would measure the window, not aliasing. This is not a relaxation of the roadmap bar: the roadmap's anti-aliasing FR
  applies to nonlinear stages, and both arms are gated.
- **MotionConfig selectors (SC-004).** These have no Phase 8 renderer. Their click-free gate is an explicit Phase 9
  handoff gate (FR-083), as the reviewer allowed. The engine side (selection switch and hysteresis) is gated here by
  SC-019(b) and SC-004(c).
- **SC-018(b)** was moved to FR-083 as a recorded handoff measurement, and SC-018 keeps only its gated bar.
