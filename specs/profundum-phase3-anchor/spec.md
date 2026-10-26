# Feature Specification: Profundum Phase 3 — Sub Anchor

**Spec slug:** `profundum-phase3-anchor`
**Status:** Clarified (2026-10-11). Every clarification is decided and encoded (see Clarifications); D-1 to D-8,
OQ-1 and OQ-4 are ratified. Not yet planned. The plan's S1 owes the Anchor-to-h1 calibration figures (k, default level,
summed-level behaviour) for ratification (FR-024, SC-024).
**Roadmap source:** `specs/Profundum-roadmap.md` → Part A → **Phase 3** (lines 305–330). Supporting roadmap
statements this spec traces to:
- **Core Philosophy:** "The fundamental never disappears. A per-voice Sub Anchor sits outside every nonlinear stage by
  default" (line 43) and "Nothing ships inert" (line 53).
- **Key Design Decision 2:** "The Anchor is always centred (no stereo modulation, unison or phase tricks)" (lines
  125–126).
- **Key Design Decision 3:** "the Anchor is per voice, never one shared oscillator" (POLY ruling, Decision 3).
- **Reuse Inventory, "Sub Anchor" row** (line 164): `SubOscillator`, `PhaseAccumulator`, `YinPitchDetector` +
  `SubharmonicValidator` (FOLLOW candidate), `PitchTracker`; verdict "🔶 phase-lock idea from `SubOscillator`.
  🆕 `AnchorOscillator` (L3)". ODR hazard list includes `SubOscillator`.
- **Downstream consumers:** Phase 4 "Retrigger — oscillator and anchor phase behaviour on new notes (reset /
  continue), click-free by construction" (line 351); Phase 6 Mass target "the sub/body relationship (anchor level)" (line 403);
  Phase 8 `MorphState` "Anchor waveform / mode / level" (line 454) and the Anchor morph modes Lock / Blend / Punch
  (line 468); Phase 9 "Anchor in parallel joining after Character" (lines 491–492) and the per-voice budget sum
  ≈ 4.8 % (line 512); Phase 12 "Anchor naming" (line 621).
- **Cross-Cutting Constraints** (lines 786–804) and **Open Questions 1 and 4** (lines 808–816), both assigned to
  Phase 3.

**Concept source:** `specs/Profundum-concept.md` §2 "A dedicated Sub Anchor" (lines 80–138: the waveform list, "Anchor
follows the bass's perceived pitch", the LOCK / FOLLOW / PUNCH / GHOST definitions) and §7 "the sub anchor is excluded"
(lines 370–385).

**Phase 1/2 contracts consumed:** `ProfundumCore` (`systems/profundum_core.h`, Phases 1+2, commit `fe6ebe7d`) and
`HarmonicFold` (`processors/harmonic_fold.h`, Phase 2). Neither header is modified by this phase unless the D-1
fallback is invoked (FR-063).

Every roadmap, concept and header line cited below was read this session (2026-10-11).

**Layer:** one new component, header-only in `Krate::DSP`:

| Component | Layer | Header | Kind |
|---|---|---|---|
| `AnchorOscillator` | 3 (systems) | `dsp/include/krate/dsp/systems/anchor_oscillator.h` | new |

---

## Overview

The Sub Anchor is the fundamental that cannot be destroyed. It is a per-voice, strictly mono oscillator that sits
beside the harmonic core rather than inside it, so no amount of excitation, Fold, Shape or Character processing can
remove the low-frequency centre of a note. It is phase-locked to its voice's core h1, so where both carry the
fundamental they add coherently instead of beating or cancelling.

It offers five waveforms (sine, triangle, rounded sine, asymmetric sine, octave-down sine) and four behaviour modes:
- **LOCK:** constant level and character.
- **FOLLOW:** tracks the processed voice's *perceived* fundamental.
- **PUNCH:** temporarily louder during the attack.
- **GHOST:** absent during the attack, emerging underneath over a settable time of milliseconds to tens of
  milliseconds.

Pure SUB's sub-regen has none of these behaviours.

This phase builds and measures the oscillator alone and next to the Phase 1/2 chain (core → excitation →
`HarmonicFold`). It does not wire it into a voice. Phase 9 does that, joining the Anchor after Character by default.

All behaviour is gated by measurement:
- THD per waveform;
- zero phase drift against the core h1 over 60 s;
- PUNCH boost and duration, and GHOST emergence time, measured against their parameters with C¹ envelopes;
- L == R bit-identity in every configuration;
- the FOLLOW input contract's accuracy and latency, plus the evidence that r ≡ 1 is correct on a heavily folded voice
  (the tracking clause moves to Phase 9 with the first pitch-moving stage, FR-047, FR-066);
- sub energy preserved within 1 dB through maximum Fold;
- ≤ 0.3 % of one core per voice.

## Scope

In scope (roadmap lines 311–330):

1. **`AnchorOscillator` (L3)**:
   - the five waveforms, RMS-calibrated and band-limited;
   - phase lock to the voice's core h1 (D-1);
   - the four modes with C¹ gain envelopes;
   - the FOLLOW input contract: an engine-derived perceived-pitch ratio r (OQ-1, resolved);
   - a level control;
   - the Reset / FreeRunning retrigger phase policy, mirroring the core's;
   - the explicit PUNCH / GHOST re-arm event, decoupled from the phase policy (FR-048);
   - the per-mode pitch-trajectory contract: LOCK holds the resolved note pitch before Drop, FOLLOW tracks the core's
     instantaneous f0 times r (FR-049);
   - a level calibrated relative to the core's h1 amplitude, not to an absolute digital level (FR-023–FR-025);
   - a strictly mono stereo-pair output.
2. **Resolution of Open Question 1** (FOLLOW mechanism) and **Open Question 4** (join-point default and an advanced
   pre-Fold feed), recorded in this spec with their measured basis.
3. **Tests:**
   - unit and render tests for the oscillator alone;
   - chain tests with `ProfundumCore` + edge excitation + `HarmonicFold` (phase lock at the core output and after
     Fold, the r ≡ 1 reference on a folded voice, sub preservation, and the pre-Fold feed if OQ-4 adopts it);
   - audibility, aliasing, DC, mono, RT-safety and CPU gates.

## Non-Goals (owned by later phases, or deliberately excluded)

| Excluded | Owner | Roadmap line |
|---|---|---|
| Wiring the Anchor into a voice, the join point in the signal graph, and the voice amp envelope's application to the Anchor | Phase 9 | 490–495 |
| The "+ Character" half of the sub-preservation criterion (Character does not exist until Phase 5). Phase 3 measures it through max excitation + max Fold, and the full clause moves to Phase 9's chain test (D-7). The handoff is enforced by FR-066 / SC-021 (roadmap amendment) | Phase 9 | 326–329 |
| FOLLOW **tracking** on a voice whose perceived fundamental actually moves. Phase 3 has no pitch-moving stage (OQ-1 evidence), so it proves the input contract and that r ≡ 1 is correct for excitation and Fold (FR-047, SC-008). The r-law deliverables move to Phase 5 and the tracking clause to Phase 9, enforced by FR-066 / SC-021 | Phases 5, 9 | 328, 362–387, 482–516 |
| Choosing when `noteOn` is called and **when the re-arm event is issued** (legato, retrigger, overlapping notes, voice reuse; Note Memory), and producing the glide / Drop trajectories. This phase only accepts a phase policy, a re-arm event and a pitch trajectory, and owns and tests the Anchor's response to them (FR-048, SC-027) | Phase 4 | 349–352 |
| Mass driving the Anchor level (this phase exposes the level; Phase 6 maps to it) | Phase 6 | 403 |
| The Anchor morph modes **Lock / Blend / Punch** and the interpolation of anchor waveform / mode / level between A and B. These are a different control from the Anchor's own LOCK / FOLLOW / PUNCH / GHOST (roadmap line 621) | Phase 8 | 454, 468 |
| Stereo field. The Anchor never enters it | Phase 7 | 433–434 |
| Parameter IDs (300–399 block), names (OQ-9 collision LOCK/PUNCH vs Lock/Punch), UI and presets | Phases 10–13 | 533–536, 621–624 |
| POLY affordability of FOLLOW per voice (OQ-7). This phase measures one instance | Phase 9 | 820–821 |

The Anchor is a generator, not a modulation source, so the `ModulationSource` concept constraint does not apply.

---

## Existing components (verified this session)

Paths are relative to `dsp/include/krate/dsp/`. Every row was opened and read on 2026-10-11.

| Component | Header | Reused for | Verified signature / fact |
|---|---|---|---|
| `ProfundumCore` | `systems/profundum_core.h:92` | **Phase-lock reference** (the h1 the Anchor locks to) and the chain under test | Layer 3 (line 2). `enum class RetriggerPhase : std::uint8_t { Reset, FreeRunning }` (94). `kControlInterval = 32` (97), `kPitchUpdateInterval = 16` (98), `kMinF0Hz = 8.0f` (99), `kDefaultF0Hz = 55.0f` (100), `kMinSampleRate = 22050.0` / `kMaxSampleRate = 192000.0` (104–105). `void prepare(double sampleRate, int numPartials = kDefaultPartials) noexcept`, documented NOT real-time safe (127–129). `void noteOn(float f0Hz) noexcept`, "Effective at offset 0 of the next block" (203–204). `void setRetriggerPhase(RetriggerPhase) noexcept` (197). `void processBlock(float* left, float* right, std::size_t numSamples, const float* f0PerSample = nullptr) noexcept` (233–234). `void setExcitation(const ExcitationControls&) noexcept` (352); `ExcitationControls { float mix = 0.0f; float drive = 0.5f; float asymmetry = 0.0f; }` (330–333). |
| — h1 frequency path | `systems/profundum_core.h:243–289, 511–518, 666–756, 795–811` | D-1 trajectory lock | Pitch is sampled at every multiple of U = 16 on a grid that **restarts at a Reset noteOn** (`intervalPhase_ = 0`, line 547) and is **kept** on a FreeRunning noteOn while sounding. The bank renders in runs that never cross a U boundary (lines 262, 274). f0 is sanitised: non-finite → previous, else clamp to [`kMinF0Hz`, `capHz_`] (795–800). A per-sample trajectory uses the newest finite element (805–811). h1's MCF coefficient is ε₁ = ε_base·d, where ε*₁ = 2·sin(π·f/fs) is computed in double (702–731) and forwarded as a float multiplier on a base that chases f by ≤ 0.9 semitone per control interval (666–688). A Reset noteOn seeds phases from `partial.phase = 0`, "so the first sample is 0.0 (FR-045)" (557–558). |
| `HarmonicOscillatorBank` | `processors/harmonic_oscillator_bank.h:75` | Read-only: how h1 is generated (D-1) | Gordon-Smith MCF: `sNew = s + eps * c; cNew = c - eps * sNew` (lines 16–17, 748–749), effective `eps = std::clamp(epsilon_[i] * detuneMultiplier_[i], -1.99f, 1.99f)` (732). First-frame state `sinState_ = sin(phase)`, `cosState_ = cos(phase)` (292–293). Crossfade on > 1 semitone `targetPitch` jumps captures `lastOutputSample_` as a level, not a phase (261–270, 791–797). **The path ProfundumCore actually takes** is the Highway batch: with no bandwidth modulation (`if (!hasBandwidth_)`, line 712) the bank calls `processMcfBatchSIMD` (713–718), whose recurrence is the same clamp and MCF but fused: `vSinNew = hn::MulAdd(vEpsEff, vCos, vSin)`, `vCosNew = hn::NegMulAdd(vEpsEff, vSinNew, vCos)` (`processors/harmonic_oscillator_bank_simd.cpp:104–105`). Its rounding therefore depends on the runtime-dispatched ISA (SSE2 / AVX2 / AVX-512 / NEON); see D-1. **No accessor for per-partial oscillator state exists** (public API, lines 132–960), so an h1 phase observer would be an append-only change to a shared header (D-1 fallback, FR-063). |
| `HarmonicFold` | `processors/harmonic_fold.h:76` | Chain fixture: "heavily folded voice" (SC-008, SC-009) and the pre-Fold feed measurement (OQ-4) | Layer 2. `void prepare(double sampleRate, std::size_t maxBlockSize) noexcept` (121), `void setAmount(float amount) noexcept` (168, range 0–100), `void setFundamentalHz(float hz) noexcept` (178), `void processBlock(float* left, float* right, std::size_t numSamples) noexcept` (188), `static constexpr std::size_t latencySamples() noexcept { return 0; }` (271). `kCalibrationPeak = 0.3552343859f` (83). Per-channel windowed true-peak detector normalising the compression and folding regimes (banner lines 14–16, constants 95–100). Zero-delay linear base band below `kSplitHz = 500` plus an oversampled residual Φ_fold(u) − c·split(u) (banner lines 18–21, line 89). |
| `PhaseAccumulator` | `core/phase_utils.h:154` | Candidate phase state | Layer 0. `struct PhaseAccumulator { double phase = 0.0; double increment = 0.0; bool advance() noexcept; void reset() noexcept; void setFrequency(float frequency, float sampleRate) noexcept; }` (154–180). `advance` subtracts 1.0 once on wrap (160–168). `calculatePhaseIncrement` (47), `wrapPhase(double)` (74). |
| `SubOscillator` | `processors/sub_oscillator.h:111` | **Idea only** (the divider lock); not composed | Layer 2. `explicit SubOscillator(const MinBlepTable* table = nullptr) noexcept` (122). `[[nodiscard]] float process(bool masterPhaseWrapped, float masterPhaseIncrement) noexcept` (222). It is driven by a **master phase-wrap flag**, which the core's MCF h1 does not produce. Its Sine / Triangle waveforms **hard-reset the sub phase** on the flip-flop rising edge (`subPhase_.phase = 0.0`, lines 273–276), which is a discontinuity whenever the estimate drifts. Its Triangle is naive piecewise-linear starting at −1 (`4·phase − 1`, lines 314–320), so it is neither band-limited nor click-free at onset. `SubOctave` / `SubWaveform` are namespace-scope enums (48, 60). Not composable for FR-010–FR-016 or SC-017. |
| `YinPitchDetector` | `processors/yin_pitch_detector.h:47` | OQ-1 option (a), rejected (Q1) | Layer 2. `explicit YinPitchDetector(size_t windowSize = 2048, float minF0 = 40.0f, float maxF0 = 2000.0f, float confidenceThreshold = 0.3f) noexcept` (54–57). `void prepare(double sampleRate) noexcept` allocates pffft setup/buffers and `std::vector` lag buffers, NOT RT-safe (127–170). `[[nodiscard]] F0Estimate detect(const float* samples, size_t numSamples) noexcept` (185–186) analyses at most `windowSize` samples with lag ≤ N/2 (192–194, `computeMaxTau` 459–463). It is block-analysis, so the caller owns the ring buffer and hop scheduling. |
| `SubharmonicValidator` | `processors/subharmonic_validator.h:47` | OQ-1 option (a), rejected (Q1) | Layer 2. `void prepare(size_t fftSize, double sampleRate) noexcept` (59). `[[nodiscard]] F0Estimate validate(const F0Estimate& estimate, const SpectralBuffer& spectrum) const noexcept` (76–78) needs an STFT magnitude spectrum (a second FFT per hop). `kMaxHarmonics = 12`, `kCorrectionThreshold = 1.4f` (50, 54). |
| `F0Estimate` | `processors/harmonic_types.h:26` | OQ-1 option (a), rejected (Q1) | `struct F0Estimate { float frequency = 0.0f; float confidence = 0.0f; bool voiced = false; };` (26–30). |
| `PitchTracker` | `primitives/pitch_tracker.h:46` | Rejected for FOLLOW | Layer 1. `void prepare(double sampleRate, std::size_t windowSize = kDefaultWindowSize) noexcept` (68), `void pushBlock(const float*, std::size_t) noexcept` (114), `[[nodiscard]] float getFrequency() const noexcept` (132). It wraps `PitchDetector`, whose `kMinFrequency = 50.0f` (`primitives/pitch_detector.h:61`) cannot track anything below G1 (49 Hz), i.e. E0–G1. |
| `OnePoleSmoother`, `LinearRamp` | `primitives/smoother.h:134, 305` | Level smoothing (FR-020) | `configure(float smoothTimeMs, float sampleRate)` (160 / 329), `setTarget(float)` (170 / 342), `[[nodiscard]] float process()` (197 / 370), `snapToTarget()` (257 / 414). |
| `SpectralShapeRecipe` constants | `processors/spectral_shape_recipe.h:51, 53, 72, 93` | Level calibration (D-3), harmonic cap (FR-012) | `kCoreOutputRmsDb = -12.0f` (51), `kPowerTarget` (53), `kNyquistCapFraction = 0.8f` (72), `static float capFrequency(double sampleRate) noexcept` = 0.8·fs/2 (93–95). Named states `kSineAnchor`, `kSawAnchor` etc. (83, 231). |
| `detail::isFinite` | `core/db_utils.h` (used by `profundum_core.h:18, 796`) | Input sanitising (FR-050) | Fast-math-immune bit test. |

**Test helpers (verified).**
- `dsp/tests/unit/systems/profundum_core_test_helpers.h`:
  - `requireLREqual(const float* L, const float* R, std::size_t n)` (61) and `renderCore(...)` (90);
  - `goertzelPhase(const float* x, std::size_t L, double fs, double hz)` (157);
  - `renderedHarmonicPowers(const float* x, double fs, double f0, int N)` (235);
  - `aliasFftLength(double fs, double f0)` (257) and `aliasedPower(const float* x, std::size_t fftLen, double fs, double f0)` (273).
- `tests/test_helpers/render_fingerprint.h`: `fingerprintRender` (73) and `compareFingerprints` (122).
- `tests/test_helpers/allocation_detector.h`: `AllocationScope` (111).
- `tests/test_helpers/artifact_detection.h`: `ClickDetectorConfig` (38) and `ClickDetector` (99).
- `tests/test_helpers/low_frequency_metrics.h`: `calculateCorrelation` (478).

**Test registration (verified).** `dsp/tests/CMakeLists.txt` enumerates TUs explicitly:
- Profundum systems TUs are at lines 569–631; `HarmonicFold`'s are at 257–259.
- The `-fno-fast-math` non-finite block is at lines 1110–1113.
- CPU cases are hidden `[.perf]` and run alone through `node tools/run-cpu-tests.js`, which pins on Windows via
  `tools/pin-perf-cores.ps1` (`tools/run-cpu-tests.js:89–93`).

## New components

| Class / type | Layer | Header | ODR sweep (`grep -rnE "(class\|struct\|enum class\|enum\|using) <Name>\b" dsp/ plugins/ tests/ tools/`, 2026-10-11) |
|---|---|---|---|
| `AnchorOscillator` | 3 | `systems/anchor_oscillator.h` | **0 hits.** No file named `anchor_oscillator*` exists. |
| `AnchorOscillator::Waveform`, `::Mode`, `::Controls`, `::RetriggerPhase` (nested) | — | same | Nested, so there is no namespace-scope exposure. Namespace-scope spellings swept anyway: `AnchorWaveform`, `AnchorControls`, `AnchorEnvelope`, `AnchorFollow`, `AnchorFollower`, `AnchorJoin`, `AnchorJoinPoint`, `SubAnchor`, `Anchor`, `PerceivedPitchEstimator`, `PerceivedFundamental`, `FundamentalTracker`: **0 hits each.** `AnchorMode`: **6 hits, all `ResonanceDriftNetwork::AnchorMode`** (`systems/resonance_drift_network.h:293`, nested `enum class AnchorMode { Free, Keyed, Hybrid }`, and five `using` aliases in its test). That name is therefore **not** used at namespace scope; the Anchor's mode enum is the nested `AnchorOscillator::Mode`. |

**Near-name check.** None of the roadmap's hazard names is used: `CharacterProcessor`, `SubOscillator`, `StereoField`,
`MorphEngine`, `MorphNode`, `TransientDetector`, `HarmonicCloud`, `SpectralState`, `MacroMapper`. `SubOctave` and
`SubWaveform` (`processors/sub_oscillator.h:48, 60`) are namespace-scope enums, so the Anchor's waveform enum must
not be spelled `SubWaveform`. It is nested as `AnchorOscillator::Waveform`. The plan re-sweeps any helper it places at
namespace scope.

**Include discipline.**
- `anchor_oscillator.h` (L3) includes Layer 0–2 headers only.
- It does **not** include `systems/profundum_core.h`, which is the same layer. The Phase 1 CC-layers lesson
  (`specs/profundum-phase1-harmonic-core/spec.md:188–190`) and `dsp/CLAUDE.md:8` ("each may only include from
  layers below it") apply.
- The phase lock is therefore achieved through plain inputs (D-1), and the retrigger enum is mirrored as a nested
  `AnchorOscillator::RetriggerPhase { Reset, FreeRunning }` with the same enumerator spelling and order as
  `ProfundumCore::RetriggerPhase` (`profundum_core.h:94`). Phase 9 maps one to the other.
- OQ-1 resolved to an engine-derived ratio, so no L2 detector header is included (the YIN / `SubharmonicValidator` option is rejected).

---

## Functional Requirements

### Descriptor definitions (used by FRs and SCs)

- **Calibration level:** the Anchor's level is calibrated **relative to the core's fundamental h1**, not to an absolute
  digital level (D-3, FR-023). The core-h1 amplitude a₁ is the peak of the core's h1 sine component, supplied to the
  Anchor per control block. Test fixtures use a₁ = `HarmonicFold::kCalibrationPeak` (0.35521, the peak of a −12 dBFS RMS
  sine) unless a state's own h1 is stated. At `level` = 1, LOCK, the Anchor's per-channel RMS is **k·a₁/√2**, the RMS of a
  sine of amplitude k·a₁, with k the calibrated ratio `kAnchorToH1` chosen by the plan (FR-024). The pre-clarify
  absolute figure (−12 dBFS RMS) is the k = 1, a₁ = 0.35521 case.
- **Harmonic powers** wₙ: `renderedHarmonicPowers(x, fs, f_a, N)` (`profundum_core_test_helpers.h:233–250`) at the
  waveform's fundamental f_a (f_a = f₀, or f₀/2 for octave-down), for n·f_a < fs/2. The helper is a **Hann
  main-lobe** sum (peak bin ± 2, `low_frequency_metrics.h:170–210`) over **one** `kLowFrequencyFftSize` = 262144-sample
  frame (`low_frequency_metrics.h:64`). It takes no window-length argument and returns 0 for a harmonic below bin 5.
  The main-lobe sum is exact at any fractional bin offset (`low_frequency_metrics.h:172–177`), so no bin-centring is
  needed. **Minimum steady-state render** before the frame: 262144 samples, i.e. 5.94 s at 44.1 kHz, 5.46 s at
  48 kHz, 2.73 s at 96 kHz (bin width 0.168 / 0.183 / 0.366 Hz, so bin 5 is ≤ 1.83 Hz, below every f_a in this spec
  including E-1's 4 Hz). **THD** = Σₙ≥₂ wₙ / w₁ (dB). **E_even** = Σ wₙ (n even) / w₁ (dB). **Floor check:** a pure
  float sine rendered at each test note and rate and read through the same helper must give THD ≤ **−100 dB**, which
  proves every −90 dB gate sits above the tool's floor (SC-001).
- **Anchor phase** θ_a(t) is the phase of the anchor's fundamental phasor from `goertzelPhase` over a window of an
  integer number of f_a periods ≥ 1 s centred at t. **Core h1 phase** θ₁(t) is the same measure on the core's
  output. **Lock error** Δθ(t) = wrap(θ_a(t) − θ₁(t)) for f_a = f₀, and wrap(2·θ_a(t) − θ₁(t)) for octave-down.
- **Gain trace** g[n] = y_mode[n] / y_ref[n], where y_ref is the same anchor rendered in LOCK with the same call
  sequence (identical phase by FR-030). It is evaluated only where |y_ref[n]| ≥ 0.1·peak(y_ref), and is also
  exposed directly by the observer `modeGain()` (FR-041).
- **Sub energy** E_sub is w₁ at f_a of the summed output, read by the same `renderedHarmonicPowers` frame (Hann
  main-lobe, 262144 samples, after the same minimum steady-state render). Only `goertzelPhase` (phase) and
  `aliasedPower` use BH7.
- **DC**, **aliased power**: as Phase 2's descriptor definitions (`specs/profundum-phase2-edge-fold/spec.md:232–252`),
  i.e. Phase 1 SC-009's BH7 / Parseval method with the D-6 FFT-length extension.
- **Test notes:** E0 = MIDI 16 (20.60 Hz), E1 = 28 (41.20 Hz), E2 = 40 (82.41 Hz), C2 = 36, C3 = 48. **Rates:**
  44.1 / 48 / 96 kHz unless stated.

### FR-001 series — Interface and lifecycle

- **FR-001 Class.** `AnchorOscillator`, Layer 3, `Krate::DSP`, header-only, in `systems/anchor_oscillator.h`. The
  banner states the layer and cites this spec. Copyable and movable unless a composed member forbids it (the plan
  records which).
- **FR-002 Controls (plain aggregate).** One nested `struct Controls` holds every sound-shaping field as plain,
  interpolable data, so that Phase 8's `MorphState` can carry "Anchor waveform / mode / level" (roadmap line 454):
  `waveform` (`Waveform`), `mode` (`Mode`), `level`, `punchBoostDb`, `punchTimeMs`, `ghostTimeMs`. Ranges and
  defaults are set in D-3 and D-4. Designated initialisers only (portability: no narrowing in brace init).
- **FR-003 Enums.**
  - `enum class Waveform : std::uint8_t { Sine, Triangle, RoundedSine, AsymmetricSine, OctaveDownSine }` (roadmap
    line 313).
  - `enum class Mode : std::uint8_t { Lock, Follow, Punch, Ghost }` (line 316).
  - Both are **append-only**, because Phase 11 persists them.
- **FR-004 Lifecycle.**
  - `prepare(double sampleRate)` clamps the rate to [22050, 192000] (mirroring `ProfundumCore`), sizes everything,
    and resets. It is control-thread only (documented NOT real-time safe if it allocates; the engine-derived FOLLOW of OQ-1 needs no detector allocation).
  - `reset()` is a control-thread call that keeps the pending Controls.
  - Every other method is audio-thread safe. Before `prepare`, every method is a no-op and `processBlock` writes
    zeros.
- **FR-005 Pitch inputs mirror the core's, with a voice-selected trajectory.**
  - `noteOn(float f0Hz, bool rearm = true)` is effective at offset 0 of the next block (`rearm`: FR-048).
    `setFrequency(float f0Hz)` sets the held f0. `setFollowRatio(float r)` sets the held perceived-pitch ratio (FR-045).
  - `processBlock(float* left, float* right, std::size_t numSamples, const float* f0PerSample = nullptr, const float* followRatioPerSample = nullptr)`
    follows the same per-sample trajectory semantics as `ProfundumCore::processBlock` (`profundum_core.h:233–289, 805–811`)
    for both pointers.
  - f0 is sanitised exactly as `ProfundumCore::sanitizeF0` (795–800): non-finite → previous, else clamp to
    [8 Hz, `SpectralShapeRecipe::capFrequency(fs)`].
  - The trajectory is **the one the voice selects per mode** (FR-049): the resolved note pitch before Drop in LOCK /
    PUNCH / GHOST, the core's instantaneous f0 (Drop and Glide included) in FOLLOW. With no Drop in play, the Anchor and the
    core see the same f0 at every sample.
- **FR-006 Retrigger policy and state model.** `setRetriggerPhase(RetriggerPhase)` with the nested
  `RetriggerPhase { Reset, FreeRunning }` follows the semantics of `ProfundumCore` (`applyNoteOn`, lines 520–608) for the
  **phase and the control grid only** (continuous, reset, aligned with the core). The policy does **not** decide whether
  PUNCH / GHOST re-arm: that is the separate re-arm event of FR-048 (Q2).
  The nested enumerators' underlying values equal `ProfundumCore::RetriggerPhase`'s (`Reset` = 0, `FreeRunning` = 1,
  `profundum_core.h:94`). A test TU asserts this with `static_assert` (SC-017), and Phase 9's mapping `static_assert`s it
  again (a recorded Phase 9 obligation, D-6).
  - **Sounding.** Like the core (`sounding_` set at line 602, cleared only by `reset()`/`prepare`, line 441; "the
    RT-safe way to clear a sounding core is a Reset-policy noteOn", line 156), the Anchor has no noteOff. It is
    *sounding* from its first `noteOn` until `reset()` or `prepare`. In a Phase 9 voice every note after the first
    is therefore a noteOn while sounding.
  - **Reset noteOn** (Reset policy, or any noteOn while not sounding): a full restart of the phase state, regardless of
    prior state, mirroring the core's Reset branch (lines 528–557). At offset 0: φ = 0 and the pitch grid restarts; the
    level smoother and the core-h1 amplitude smoothing **snap** to their targets; a pending or running waveform crossfade
    **completes**; the follow-ratio glide snaps to its target. When the noteOn carries a re-arm (FR-048; always the case
    from not sounding) g restarts at its mode's start value (LOCK / FOLLOW 1, PUNCH 1 then the rise to B, GHOST 0); with
    `rearm = false` on a sounding voice g continues without a step. Output after a Reset noteOn carrying a re-arm is
    therefore a function of the pending Controls, the follow ratio, the core-h1 amplitude, f₀ and the policy only.
    Click-freedom of a Reset noteOn **on a sounding voice** is Phase 4's (roadmap line 351, "click-free by
    construction"), as it is for the core's own phase restart.
  - **FreeRunning noteOn while sounding:** φ, the grid, the level smoother and any crossfade continue. g is untouched
    unless the noteOn carries a re-arm, which then runs the bridged arm of FR-048.
  - **"From silence"** in this spec means a Reset noteOn carrying a re-arm as defined here, whether or not the
    instance was sounding.
  - Phase 4 chooses which phase policy is in force and when the re-arm event is issued; this phase implements both.
- **FR-007 Output.** `processBlock` **overwrites** `left` and `right` (Phase 9 sums). An aliased `left == right`
  buffer is valid. `numSamples == 0` or a null pointer is a no-op with no state change.
- **FR-008 Observers (tests, Phase 9 diagnostics).**
  - `[[nodiscard]] float modeGain() const noexcept`: the mode envelope gain at the last rendered sample.
  - `[[nodiscard]] double phase() const noexcept`: the h1-locked phase in cycles [0, 1).
  - `[[nodiscard]] bool stateFinite() const noexcept`.
  - `[[nodiscard]] static constexpr std::size_t latencySamples() noexcept`, which returns 0 (FR-033).

### FR-010 series — Waveforms (roadmap line 313)

- **FR-010 Waveform set.** Sine, triangle, rounded sine, asymmetric sine, octave-down sine, with harmonic
  definitions in D-2. Each is defined as a function of the h1-locked phase φ so that the waveform's fundamental
  component is **in sine phase with core h1**. Where both carry f₀, the Anchor and h1 add coherently.
- **FR-011 RMS calibration (relative to core h1).** At `level` = 1, LOCK, with core-h1 amplitude a₁ supplied (FR-023),
  every waveform's per-channel steady-state RMS equals k·a₁/√2 (FR-024) within ±0.1 dB, and that ratio is the same for
  every waveform. A waveform switch is not a level change.
- **FR-012 Band-limited by construction.**
  - No component of any waveform is generated at or above `SpectralShapeRecipe::capFrequency(fs)` (0.8·fs/2).
  - Waveforms with an infinite series (triangle) are truncated or tapered at the cap, by additive terms, a
    closed form with polyBLAMP-class correction, or a recipe-style taper (plan's choice).
  - SC-010 is the gate. The Anchor contains no nonlinear stage that needs oversampling.
- **FR-013 DC-free by construction.** Every waveform has zero mean over a period (D-2), so no DC blocker sits on
  the Anchor path, which would phase-shift an E0 fundamental. SC-011 is the gate.
- **FR-014 Waveform switch is click-free.**
  - A `waveform` change while sounding crossfades old → new over a documented `static constexpr` time
    (equal-gain; both share φ, so the fundamentals are coherent).
  - This is the "stepped-with-crossfade" behaviour Phase 8 will call for discrete anchor waveforms (roadmap line
    459).
  - The crossfade time is ≤ 10 ms (D-5).
- **FR-015 Octave-down phase.** The octave-down sine uses φ/2. On a Reset noteOn, φ/2 starts at 0. On FreeRunning
  it continues, so the sub's zero crossings coincide with every other rising zero crossing of h1: the divider idea
  of `SubOscillator`, without its phase-reset discontinuity (`sub_oscillator.h:273–276`).
- **FR-016 Click-free onset.** On a Reset noteOn the first output sample is 0.0 for every waveform whose value at
  φ = 0 is 0. Any waveform with a non-zero value at φ = 0 (the asymmetric sine under D-2) applies a documented onset
  ramp to its offending component, so SC-017's onset bound holds.
- **FR-017 Waveform coefficients (Q5, ratified).** Rounded sine = sin φ + 0.25·sin 3φ (h3 at −12.04 dB, odd-only, clearly
  flat-topped, 7.04 dB above the band-limited triangle's h3 of −19.08 dB). Asymmetric sine = sin φ − 0.25·cos 2φ (h2 at
  −12.04 dB, pre-normalisation peaks +1.25 / −0.75). Both have unnormalised RMS √0.53125 = 0.7289; the normalised crest
  factors are 1.223 and 1.715 (FR-055). D-2 is normative and SC-001(c)(d) and SC-012(a) are gated on these values.

### FR-020 series — Level

- **FR-020 Level.** `level` ∈ [0, 1] (D-3). It is linear gain against the calibrated waveform and is smoothed per
  sample with a documented `static constexpr` time constant of **≤ 10 ms** (D-3), so automation is click-free
  (SC-006). A Reset noteOn snaps it (FR-006).
- **FR-021** `level` = 0 produces exact 0.0 output once the smoother has converged.
- **FR-022** The level is the single control Phase 6 (Mass, "sub/body relationship (anchor level)", line 403) and
  Phase 8 (Lock / Blend / Punch anchor morph, line 468) drive. No other user-facing gain path is exposed. The core-h1
  amplitude input of FR-023 is a calibration reference supplied by the voice, not a control.
- **FR-023 Core-h1 reference input (Q6).** The Anchor's level is calibrated against the core's fundamental. `setCoreH1Amplitude(float a1)`
  supplies a₁ (the peak of the core's h1 sine component) per control block, held between calls. The total gain is
  `level` · g · k · a₁, applied to the waveform normalised to unit RMS. a₁ passes through the same smoothing as `level`
  (FR-020), so a per-block change is click-free (SC-006). A non-finite a₁ keeps the previous value; finite values are
  clamped to [0, `kMaxCoreH1Amplitude`] = [0, `HarmonicFold::kCalibrationPeak`]. Before the first call a₁ defaults to
  `kCalibrationPeak` so the Anchor is usable alone. The plan names the source of a₁ (the recipe's h1 amplitude at the
  core's output normalisation); any accessor it adds to a shared header is append-only under FR-063.
- **FR-024 Calibration constants (Q6).** The ratio k = `kAnchorToH1` and the default `level` are chosen by the plan by
  **measurement**: the Anchor-to-h1 ratio and the summed-fundamental level across the representative named recipe states
  (`kSineAnchor`, `kSawAnchor`, `kHeavy`, `kHollow`, `kGrowl`, `kDefaultControls`, mid grid), checking that the default does
  not create a perceptible loudness jump. The plan's S1 proposes the figures for ratification. `level` ∈ [0, 1] with 0
  silent and 1 the calibrated maximum Anchor level; 1.0 is **not** an absolute −12 dBFS RMS independent of the core. The
  in-phase sum rising up to about +6 dB at k = 1 is expected, not a bug. SC-024 is the gate.
- **FR-025 No compensation; shared level convention (Q6).** The Anchor never modifies the core's signal, and nothing
  auto-compensates the core or normalises the combined signal (that would undermine the Anchor Level control). The
  summed signal is validated later through Phase 6's Mass dynamics and the output safety stages (FR-066). The core and the
  Anchor share one output-level convention: both are levelled on the same −12 dBFS-referenced scale through a₁ and k.
  SC-024(d) is the gate.

### FR-030 series — Phase lock (roadmap lines 313–315; D-1)

- **FR-030 Lock.** The Anchor's phase φ is locked to its voice's core h1, given the same `noteOn` / `setFrequency` /
  `f0PerSample` / `setRetriggerPhase` call sequence as the core, **whenever the core is on its resolved trajectory**. During
  a transient Drop under LOCK the Anchor deliberately differs in frequency from h1 (FR-049) and no beating or phase
  criterion applies. The lock holds for:
  - static notes;
  - per-sample pitch trajectories (bends, vibrato, glides);
  - both retrigger policies;
  - > 1-semitone jumps (where the bank crossfades level only, `harmonic_oscillator_bank.h:261–270`).

  SC-003 is the gate.
- **FR-031 Mechanism (D-1, ratified Q7).** **Input-trajectory phase lock**, never phase extraction from the rendered h1
  (nonlinear processing alters h1's phase and the Anchor must stay independent of it). The Anchor reproduces the core's h1
  frequency law from the same trajectory input:
  - the same f0 sanitising;
  - the same U = 16 pitch-update cadence aligned to the same grid (restart on a Reset noteOn, kept on FreeRunning);
  - the same MCF coefficient law ε = 2·sin(π·f/fs) at each update.

  It then advances its own phase, so the lock is structural and needs no include of `ProfundumCore`.

  The core and the Anchor receive a **common phase reference** (FR-036). The Anchor's phase is **continuous** across
  trajectory and mode changes and is never hard-reset by an update (FR-035).
- **FR-032 Fallback (D-1).** If SC-003 cannot be met by FR-031, the plan may add an **append-only** observer to
  `ProfundumCore`, and if necessary to `HarmonicOscillatorBank`, exposing h1's effective per-run coefficient. A
  bank change is a shared-header change: FR-063 applies (Innexus, Ruinae, Seraphis and Vorago suites stay green).
- **FR-033 Zero latency.** The Anchor adds no delay. Its fundamental is time-aligned with core h1 at sample 0 of a
  Reset note.
- **FR-034 Mathematical stability.** The Anchor's waveform depends only on φ, the Controls and the mode envelope.
  It never depends on the processed voice's audio, so it "stays mathematically stable however the main voice is
  processed" (line 315). The Anchor has no audio input (FR-052); its only pitch inputs are the trajectory and the FOLLOW
  ratio.
- **FR-035 Phase continuity and phase convention (Q7).** The Anchor's phase φ is **continuous** across note-on (FreeRunning
  policy), Glide, Drop, LOCK ↔ FOLLOW mode changes, changes of the supplied trajectory and waveform switches: only the
  Reset policy's noteOn restarts φ, and nothing else ever hard-resets it. A **fixed phase offset** and a **waveform-specific
  phase convention** are defined: at a Reset noteOn φ = 0, the core's h1 starts at phase 0 (`profundum_core.h:557–558`) and
  every waveform's fundamental component is sin φ (octave-down: sin(φ/2)), so the Anchor's fundamental is in phase with h1
  at note start (offset 0°). Sharing a phase trajectory does not force the same waveform phase convention as h1; the
  convention above is the contract. SC-026 is the gate.
- **FR-036 Common phase reference (Q7).** The reference is associated with the **resolved note pitch after Glide but before
  Drop**: if Drop modifies the core's phase increment, the pre-Drop phase accumulator is the natural LOCK reference; if
  Drop is a separate pitch-modulation stage, the reference is the pitch trajectory entering that stage. LOCK: the Anchor's
  phase follows that reference (its frequency follows Glide and ignores Drop). FOLLOW: the Anchor follows the core's actual
  post-Drop trajectory times r. PUNCH / GHOST trigger and re-arm stay controlled by the note engine (FR-048). The
  fallback of FR-032 (an append-only h1 observer) is invoked only if SC-003's drift criterion fails on any CI ISA;
  otherwise the shared-header diff is zero (FR-063).

### FR-040 series — Modes (roadmap lines 316–320)

- **FR-040 LOCK.** The mode gain is the constant 1: level and character are constant.
- **FR-041 Mode envelope.**
  - PUNCH and GHOST multiply the waveform by a mode gain g(t). g is armed by a re-arm event (FR-048) and is **C¹** at every
    point, including its start, its end and a re-arm while sounding.
  - **Curve family (D-4, D-5).** A segment that starts at rest (g' = 0: every arm after a Reset noteOn, and every
    segment that follows a completed segment) is a **raised cosine** of height A over N samples:
    g = g₀ + A·½(1 − cos(π·n/N)), peak |Δg| = |A|·π/(2N), peak |Δ²g| = |A|·π²/(2N²). A change of trajectory while g
    may be moving (a FreeRunning re-arm, a mode switch) starts with a **cubic Hermite bridge** from the current
    (g₀, g'₀) to the target value g₁ with zero end slope over N_b samples. With Δ = g₁ − g₀ and m₀ = N_b·g'₀ (g'₀
    per sample), its peak |Δ²g| = max(|6Δ − 4m₀|, |6Δ − 2m₀|)/N_b² and its peak |Δg| is the cubic's analytic maximum.
  - g never leaves [0, max(B, g₀)]: where the incoming slope would make the bridge overshoot (Fritsch–Carlson
    monotonicity condition violated), N_b is **shortened to the largest length whose cubic stays in [0, max(B, g₀)]**
    (plan S1 C-3, ratified 2026-10-11: lengthening raises the overshoot; integer bisection, ≤ 11 closed-form checks per event).
  - `modeGain()` exposes g.
- **FR-042 PUNCH** ("temporarily louder during the attack"). After a **snap arm** (FR-048: a re-arm on a Reset noteOn or from
  not sounding), g rises from 1 to B = 10^(boost/20) by a raised cosine over `kPunchRiseMs` (D-4), then falls back to 1 by a
  raised cosine over `punchTimeMs`. A **bridged arm** (a re-arm on a sounding FreeRunning voice) replaces the rise with a
  Hermite bridge from the current (g, g') to B over `kPunchRiseMs` (shortened per FR-041 if needed), then the same fall.
  Without a re-arm event g is untouched (FR-048). Peak and duration are measured against the parameters (SC-004, SC-027).
- **FR-043 GHOST** ("absent during the attack, emerges underneath over a settable time — milliseconds to tens of
  milliseconds"). After a **snap arm** (FR-048), g starts at 0 and rises to 1 by a raised cosine over `ghostTimeMs`. A
  **bridged arm** (a re-arm on a sounding FreeRunning voice) first descends by a Hermite bridge from the current (g, g')
  to 0 over `kGhostDipMs` (D-5, shortened per FR-041 if needed), then emerges by the same raised cosine. Without a re-arm
  event g is untouched (FR-048). SC-005 is the gate on every arm path, including the no-re-arm row.
- **FR-044 Mode switch while sounding.** A `mode` change never steps g. Arms happen only on a re-arm event (FR-048), so every
  mode's steady state is g = 1: a switch runs a Hermite bridge from the current (g, g') to 1 over `kModeBridgeMs`
  (≤ 10 ms, D-5). A switch to or from FOLLOW additionally starts the r glide of FR-045.
- **FR-045 FOLLOW** ("tracks the processed voice's *perceived* fundamental"; OQ-1 resolved to an **engine-derived ratio**, Q1).
  In FOLLOW the Anchor's frequency is the supplied trajectory (the core's instantaneous f0, Drop and Glide included, FR-049)
  times a **perceived-pitch ratio** r = f_perceived / f₀.
  - Interface: `setFollowRatio(float)` (held) and the optional per-sample pointer `followRatioPerSample` of `processBlock`
    (FR-005); default r = 1. Zero latency, deterministic, no analysis, no allocation.
  - With r = 1, FOLLOW is bit-identical to LOCK on a static note, and on any trajectory when both are fed the same
    trajectory input.
  - r is sanitised like f₀ (non-finite → previous; finite values clamped to a documented range containing [0.25, 4]) and is
    sampled on the same U = 16 pitch grid as f₀, so FOLLOW's frequency law is the trajectory lock's (FR-031) with
    f = r·f₀ (clamped to the cap).
  - A change of r glides the frequency phase-continuously to r·f₀ over `kFollowGlideSamples` ≤ **16** samples
    (`static constexpr`), so a step in r is complete within kFollowGlideSamples + U ≤ 32 samples (SC-008(b)).
  - Level behaviour in FOLLOW is LOCK's (constant).
  - **Inert until a pitch-moving stage exists:** on every Phase 1–3 voice r ≡ 1 (FR-047), so FOLLOW renders LOCK's output
    until a Phase 5 stage (Rectify, Crush, Fracture) publishes an r law. Phase 9's capability table must prove FOLLOW
    audible or flag it inert (FR-066, SC-021).
  - YIN analysis was rejected (latency ≥ 85 ms at E0, allocation, CPU).
  - SC-008 is the gate.
- **FR-046 (withdrawn, Q1).** The YIN + `SubharmonicValidator` analysis mechanism is rejected for FOLLOW (latency ≥ 85 ms at
  E0, allocation, CPU). No analysis input, no detector include, no allocation outside `prepare` and `latencySamples()` is 0
  in every mode (FR-033). The id is kept so cross-references stay stable.
- **FR-047 The r contract is owned by each pitch-moving stage.** Under the resolved OQ-1 mechanism (engine-derived ratio, Q1), the perceived-pitch ratio
  r = f_perceived / f₀ is not estimated by the Anchor. Each stage that can move the perceived fundamental owns and
  documents its r law, and the voice (Phase 9) composes them. Phase 3 records **r ≡ 1 for the core, edge
  excitation and `HarmonicFold`**, with SC-008(r)'s offline harmonic-sum reference on the heavily folded voice as
  the evidence. A later stage that cannot state an r law records r ≡ 1 with its own measured evidence; r ≡ 1 is
  never assumed. FR-066 carries the downstream deliverables into the roadmap.
- **FR-048 Re-arm event (Q2).** PUNCH / GHOST re-arming is its **own explicit event**, not embedded in the phase policy. Phase
  policy governs only phase behaviour and the control grid (FR-006); Phase 4's Note Memory decides **when** the re-arm
  event is issued (legato, retrigger, overlapping notes, voice reuse). Phase 3 owns the Anchor's response and timing to it
  and tests that response; it does not design the re-arm policy.
  - Interface: `noteOn(float f0Hz, bool rearm = true)` (FR-005). `rearm` acts on g only and is independent of the phase
    policy. The plan may add a standalone `rearm()` with identical semantics, effective on the same boundary.
  - **Snap arm:** `rearm = true` and (Reset policy or not sounding): g snaps to its mode's start value (FR-006).
  - **Bridged arm:** `rearm = true`, FreeRunning policy, sounding: Hermite bridge from the current (g, g') (FR-042, FR-043).
  - **No re-arm:** `rearm = false` on a sounding voice (either policy): g is untouched, bit-identical to the uninterrupted
    g trace. A noteOn from not sounding always arms.
  - **Timing:** the response begins at offset 0 of the block after the call and is independent of the host block partition
    (FR-056). The phase behaviour of a noteOn is bit-identical whether `rearm` is true or false.
  - SC-027 is the gate; SC-005 (P3), SC-006 and SC-017 carry the no-re-arm rows.
- **FR-049 Pitch-trajectory contract per mode (Q3, ratified as (ii) amended).** LOCK (and PUNCH / GHOST, which are LOCK-like
  in pitch) holds the **note's resolved pitch**; FOLLOW tracks **the core's instantaneous f0 times r**:

  | Event | LOCK | FOLLOW |
  |---|---|---|
  | Note pitch changes (new noteOn f₀, `setFrequency`) | follows | follows |
  | Glide | follows | follows |
  | Transient Drop | **ignores** | **follows** |

  LOCK's frequency input is the resolved note pitch trajectory after tuning, note-level pitch processing and Glide, but
  **before** the transient Drop trajectory. The Anchor takes its pitch from the single trajectory input that **the voice
  selects per mode** (Phase 4 / Phase 9 route Drop to the core only; FR-005, FR-066). The h1 phase-lock criterion (SC-003)
  is asserted whenever the core is on its resolved trajectory (static notes, glides); during a Drop under LOCK the Anchor
  and h1 legitimately differ in frequency and no beating criterion applies. Re-locking to h1 after a Drop is Phase 4's /
  Phase 9's obligation and is not asserted here. SC-025 is the gate.

### FR-050 series — Mono, per-voice ownership, RT safety, robustness

- **FR-050 Strictly mono.** L == R bit-identically for every waveform, mode, level, policy, host block partition and
  input (finite or not). The Anchor has no stereo modulation, unison, detune, decorrelation or per-channel state
  (roadmap lines 125–126, 321).
- **FR-051 One instance per voice.** The Anchor holds no `static` mutable state and no shared buffers. Two instances
  driven with different notes are independent: interleaved processing equals each processed alone, bit-identically
  (roadmap line 324).
- **FR-052 Excluded from Fold, Shape and Character by default** (line 321). `AnchorOscillator` exposes no input for
  the voice's audio. The exclusion is a
  property of Phase 9's wiring. Phase 3 proves the property it can, in SC-009.
- **FR-053 RT safety.** No allocation, lock, exception or I/O in any audio-thread method. Every audio-thread method is
  `noexcept`. Fixed-size state only.
- **FR-054 Non-finite and denormal inputs.**
  - A non-finite Controls field keeps the previous value; finite fields are clamped to their ranges.
  - A non-finite f0 keeps the previous f0 (FR-005).
  - A non-finite follow ratio (FR-045) or core-h1 amplitude (FR-023) keeps the previous value; finite values are clamped to
    their documented ranges.
  - A finite denormal in a Controls field is flushed to 0.0.
  - Output stays finite and `stateFinite()` true.
  - `std::isnan` is not used (a bit-pattern test is, because of `-ffast-math`). The non-finite TU is registered in
    the `-fno-fast-math` block.
- **FR-055 Bounded output.** |y| ≤ a documented `static constexpr` ceiling = (k·a₁max/√2) × max absolute crest factor ×
  10^(12/20) (PUNCH maximum) × an overshoot margin, `static_assert`ed ≤ 2.0, with a₁max = `kMaxCoreH1Amplitude` =
  `kCalibrationPeak` (FR-023). Absolute crest factors (peak/RMS) under the ratified D-2 (FR-017): sine 1.414, triangle
  1.732 (√3; band-limiting only lowers it), rounded sine 1.223 (peak 0.8910 / RMS 0.7289), asymmetric sine 1.715 (peak
  1.25 / 0.7289), octave-down 1.414. Level ≤ 1, the equal-gain crossfade between two waveforms sharing φ never exceeds the
  larger peak, and FR-041 keeps g ≤ max(B, g₀) ≤ B. At k = 1 the product is 0.2512 × 1.732 × 3.981 = **1.732**; with a
  1 % margin the ceiling is **1.75**, unchanged by the ratified coefficients because the triangle's crest factor (1.732)
  still exceeds the asymmetric sine's (1.715). The ceiling scales linearly with k (1.75·k), so the `static_assert` ≤ 2.0
  bounds the plan's k to ≤ 1.14 unless the plan's S1 ratifies a different ceiling bar.
- **FR-056 Determinism.** The Anchor uses no random source and no seed. Same calls give bit-identical output, and
  output is independent of host block partitioning when no setter is called between blocks.
- **FR-057 FTZ precondition.** As `ProfundumCore` (`profundum_core.h:89–91`): the owner runs `processBlock` with
  FTZ/DAZ on.
- **FR-058 Portability** (roadmap line 800). No narrowing in brace initialisation and no aligned SIMD loads or
  stores (`hn::LoadU`/`StoreU` if any SIMD is added). The code passes `node tools/check-portability.js` and
  `tools/lint-apple-globals.js`, and comply records both outputs (SC-023). As Phase 2's FR-046
  (`specs/profundum-phase2-edge-fold/spec.md:430–431`).

### FR-060 series — Tests, audibility, anti-aliasing, CPU, footprint

- **FR-060 Audibility (theme-level FR, roadmap line 788).** Every lever (waveform, mode, level, punch boost, punch
  time, ghost time, FOLLOW ratio) has a render-descriptor gate (SC-012). Nothing ships inert.
- **FR-061 Anti-aliasing (theme-level FR, line 793).** The asymmetric and rounded sines are harmonic shapes, not
  waveshapers, but every waveform is still measured at E0–E2 against the ruled −80 dBFS bound (SC-010).
- **FR-062 Mono compatibility (theme-level FR, line 791).** SC-007, in every mode.
- **FR-063 Footprint.** No shared DSP header changes, unless the D-1 fallback is invoked, which is append-only and
  keeps the Innexus, Ruinae, Seraphis and Vorago suites green. `profundum_core.h` and `harmonic_fold.h` are
  unchanged unless the D-1 fallback is invoked, in which case Phase 1/2 suites run unmodified.
- **FR-064 CPU budget is an FR** (line 796): SC-018, never relaxed to pass.
- **FR-065 Bar integrity.** The numeric bars in the SC table stand as written. A bar that a measurement proves
  unreachable is re-ruled with the evidence in the Clarifications log, never relaxed silently (SC-020).
- **FR-066 Deferred-clause handoff (roadmap amendment).** Criteria and clarify decisions that cannot be fully measured or
  owned in Phase 3 are written into `specs/Profundum-roadmap.md` by this phase's tasks, and comply proves the text is there
  (SC-021):
  - **Phase 9 success criteria** (the paragraph at roadmap lines 511–515) gain: "Anchor sub energy (w1) within 1 dB
    through maximum Fold + every Character algorithm at maximum Amount, at E0–E2" (D-7); "FOLLOW tracks the perceived
    fundamental of a voice through a real pitch-moving stage (accuracy ±5 cents, latency measured), and the capability
    audibility table carries a FOLLOW row driven by that stage, or FOLLOW is flagged inert" (Q1); and "the Anchor joins
    after Character by default, the direct Anchor path is always present, and Drop is routed to the core only: the Anchor
    receives the resolved note pitch before Drop in LOCK / PUNCH / GHOST and the core's post-Drop f0 in FOLLOW" (Q3, Q4).
  - **Phase 5 `BassCharacterStage` bullet** (roadmap lines 374–379): each of Rectify, Crush and Fracture names an
    **r-law deliverable** (its perceived-pitch ratio r = f_perceived / f₀ as a function of its controls, with a
    measured reference, FR-047), or records r ≡ 1 with evidence.
  - **Phase 4 Note Memory bullet** (roadmap lines 349–352): Note Memory decides when the Anchor's re-arm event (FR-048) is
    issued (legato, retrigger, overlapping notes, voice reuse); and the core and the Anchor share a common phase reference at
    the resolved note pitch before Drop (FR-036).
  - **Phase 6 Mass** (roadmap line 403): the voice supplies the core's h1 amplitude per control block (FR-023), and the
    summed core + Anchor signal is validated through Mass dynamics and the output safety stages, not by compensation in the
    Anchor (FR-025).
  - **Pre-Fold feed (Q4), written only if SC-022 passes:** Phase 9 gains the voice-level send `anchorFoldFeed` ∈ [0, 1],
    default 0; Phase 8's `MorphState` (roadmap line 454) gains the field `anchorFoldFeed`; Phase 11's 300–399 block gains
    the parameter. If SC-022 fails, none of these is written and comply records "feed not shipped".
- **FR-067 Pre-Fold feed fixture (Q4, adopted).** The pre-Fold feed is adopted as a Phase 9 voice-level send `anchorFoldFeed` ∈
  [0, 1], default 0, with the **direct Anchor path always present** (the fundamental cannot be destroyed). It is not an
  `AnchorOscillator` field. A chain fixture core + g·Anchor → `HarmonicFold` → + direct Anchor, with g = `anchorFoldFeed`,
  is built in this phase; the Anchor instance feeding Fold and the direct path is one instance (one render, used twice).
  The feed **ships only if it passes SC-022** (the 3 dB / 0.25 octave audibility gate with sub energy within 1 dB);
  otherwise it is not shipped and FR-066's feed amendments are not written. The default join point is after Character
  (Phase 9 wiring, FR-052, FR-066).

---

## Success Criteria

Renders are at 48 kHz unless stated. "Every waveform" means all five; "every mode" means all four (FOLLOW with
r = 1 unless stated). Every fixture asserts L == R through `requireLREqual`.

| ID | Criterion | Threshold | Method (test-name sketch) |
|---|---|---|---|
| **SC-001** | THD per waveform (roadmap line 326) | At E1, C2, C3, level 1, LOCK. **(a) Sine:** THD ≤ **−90 dB**. **(b) Triangle:** wₙ/w₁ = 1/n⁴ for odd n (−19.1, −27.96, −33.8 dB at n = 3, 5, 7) within **±0.5 dB** for n ≤ 15, even n ≤ **−90 dB**; total THD = −18.3 dB ± 0.3 dB (the band-limited 1/n² odd series). **(c) Rounded sine** and **(d) asymmetric sine:** each harmonic of D-2's ratified definition (a₃ = a₂ = 0.25: rounded sine h3 = −12.04 dB, asymmetric sine h2 = −12.04 dB re h1, FR-017) within **±0.25 dB**, every other n ≤ **−90 dB** re h1; the rounded sine's h3 is ≥ **7.0 dB** above the band-limited triangle's h3 (−19.08 dB). **(e) Octave-down sine:** THD ≤ **−90 dB** re f₀/2, and power at f₀ ≤ **−90 dB** re f₀/2. **(f) Tool floor:** a pure float sine at each note and rate reads THD ≤ **−100 dB** through the same helper, and a deliberately non-bin-centred note (f₀ × 1.0003) reproduces (b)'s triangle ratios within the same ±0.5 dB. Every frame follows the minimum steady-state render of the descriptor definitions (262144 samples). | `AnchorOscillator_WaveformThd`, `_ThdToolFloor`. |
| **SC-002** | RMS calibration relative to core h1 (FR-011, FR-023, FR-024) | With a₁ = `kCalibrationPeak` supplied (and again at a₁ = 0.25·`kCalibrationPeak`), steady RMS per channel = k·a₁/√2 ± **0.1 dB** (k = `kAnchorToH1`, FR-024) for every waveform at E1 and C3, all three rates, and the ratio between any two waveforms is within **0.1 dB**. RMS scales linearly with a₁ within **0.05 dB**; a₁ = 0 gives exact 0.0 once smoothed. | `AnchorOscillator_RmsCalibration`. |
| **SC-003** | Zero phase drift against core h1 over 60 s (line 326) | Anchor and `ProfundumCore` (mid grid, excitation off) driven by identical calls. **(a) Static:** at E0, E1, E2, C3 and all three rates, \|Δθ(1 s)\| ≤ **1°** and \|Δθ(60 s) − Δθ(1 s)\| ≤ **0.5°**. **(b) Trajectory:** with a 5 Hz ±1-semitone `f0PerSample` vibrato and a ±12-semitone 200 ms bend every 5 s for 60 s, the same bounds after each bend settles (window starting 100 ms after the bend). **(c) Policies:** after a Reset noteOn and after a FreeRunning noteOn (same f₀, and a 7-semitone jump), the same bounds. **(d) Octave-down:** Δθ via 2·θ_a, the same bounds. Host blocks 37 and 512. **Scope (FR-030, FR-049):** asserted wherever the core is on its resolved trajectory; no beating or phase criterion applies during a transient Drop under LOCK (SC-025). **(e) Processed voice:** excitation at mix 1, drive 1, asymmetry 1; Δθ measured against h1 of the core's output, and separately against h1 after `HarmonicFold` at 100. At E0, E1, E2, C3 (48 kHz): **(i)** against the core's output h1, \|Δθ(60 s) − Δθ(1 s)\| ≤ **0.5°** (gated; reads ≤ 0.03°); **(ii)** against h1 after Fold 100, **recorded, not gated** (re-ruled 2026-10-12, Clarifications "Session 2026-10-12"): the figure is Phase 1's inter-partial float phase walk seen through the Fold's nonlinearity, not the Anchor's lock — `evidence/drift-probe.cpp` shows the Fold on a periodic input drifts ≤ 0.0001° while the core's partials walk up to 8.35° relative to h1 in 60 s at C3 (T023 read +0.59° at C3, 0.04° at E2, 0.008° at E0). The static offset \|Δθ(1 s)\| is recorded (not gated) beside Phase 2's 0.07° h1 figure (roadmap line 268). **ISA:** the reference h1 is the float, ISA-dispatched, FMA-contracted MCF (D-1). Every bar holds on every CI ISA: the 10 s smoke runs per push on all three OSes, the 60 s cases in the nightly `[long]` lane. A failure on any ISA in (a)–(d) or (e)(i) invokes FR-032, never a re-ruling ((e)(ii) is recorded: its figure is not a property FR-032 can move). | `AnchorOscillator_PhaseLockStatic60s`, `_PhaseLockTrajectory`, `_PhaseLockRetrigger`, `ProfundumAnchor_PhaseLockProcessedVoice` (60 s cases `[long]`; a 10 s E1 smoke per push). |
| **SC-004** | PUNCH boost amount and duration (line 327) | For boost ∈ {3, 6, 12} dB × `punchTimeMs` ∈ {min, 40, max}, at E1 and C3. **(a)** The peak of g = 10^(boost/20) within **±0.1 dB** (observer and gain trace). **(b)** g reaches exactly 1.0 at t = `kPunchRiseMs` + `punchTimeMs` ± **1 sample** (the first sample after the peak with g == 1.0 bitwise; the sample before it is > 1), stays within **0.01 dB** of 1 after it, and is > 1 + 0.1 dB at t = `kPunchRiseMs` + 0.5·`punchTimeMs` (plan S1 C-12, ratified 2026-10-11: a raised-cosine fall is within 0.01 dB about 150 samples before its end, so "first sample within 0.01 dB" read early). **(c)** The gain trace from the render matches the observer within **0.05 dB** at every evaluated sample. | `AnchorOscillator_PunchBoostAndDuration`. |
| **SC-005** | GHOST emergence time (line 327) | For `ghostTimeMs` ∈ {min, 5, 20, max} at E1 and C3, on **every arm path a voice takes** (FR-006, FR-048). **(P1)** A Reset noteOn following ≥ 1 s of a sounding note, with t = 0 at the noteOn; as a control the same note on a fresh instance, bit-identical by SC-017. **(P2)** A bridged arm (`rearm = true` under FreeRunning) while sounding at g = 1, with t = 0 at the end of the `kGhostDipMs` dip, where g has reached 0 (the dip itself is gated by SC-006(a)). **(P3) No re-arm:** a noteOn with `rearm = false` (both policies) while sounding leaves g bit-identical to the uninterrupted render of the same calls, and no emergence restarts (FR-048). On P1 and P2: **(a)** g(0) = 0 and g ≤ **−30 dB** for t ≤ 0.1·T; **(b)** g = 1 (within 0.01 dB) for t ≥ T + 1 sample; **(c)** g monotonic non-decreasing over [0, T]; **(d)** the render's energy over [0, T], relative to the LOCK render's same window (same calls), equals the analytic ratio Σ g_D4[n]²·y_ref[n]² / Σ y_ref[n]² within **±0.25 dB**, with g_D4 D-4's closed-form raised cosine in double and y_ref the LOCK render. Orientation (sine, 48 kHz, P1): E1 −1.63 / −1.88 / −3.68 / −4.41 dB and C3 −1.73 / −5.87 / −4.50 / −4.30 dB at T = 1 / 5 / 20 / 100 ms. The T ≫ period limit is 10·log₁₀(3/8) = −4.26 dB; the T ≪ period limit is ≈ −1.6 dB (re-ruling R-1). | `AnchorOscillator_GhostEmergence` (P1, P2, P3). |
| **SC-006** | C¹ envelopes and click-free transitions (line 327; FR-014, FR-020, FR-041–044) | **Coverage.** (a) covers every g trajectory: the PUNCH and GHOST arms after a Reset noteOn; PUNCH and GHOST bridged arms (`rearm = true`, FreeRunning, sounding) at mid-rise, mid-fall and steady state; `rearm = false` noteOns under both policies mid-envelope (g unchanged, FR-048); LOCK ↔ FOLLOW trajectory-input switches, Glide and Drop-shaped trajectories (FR-035, FR-049); and mode switches between every ordered pair mid-envelope. (b) and (c) cover all of those plus level steps 0 → 1 → 0, core-h1 amplitude steps across [0, `kMaxCoreH1Amplitude`] (FR-023), waveform switches between every ordered pair mid-cycle, and a slow upward f₀ sweep through the cap region (E-2). A Reset noteOn on a sounding voice is not a transition here (it snaps, FR-006; its click-freedom is Phase 4's). **(a) C¹:** per raised-cosine segment of height A over N samples, max \|Δ²g\| ≤ **1.05 × \|A\|·π²/(2N²)**; per Hermite bridge, max \|Δ²g\| ≤ **1.05 ×** its analytic peak max(\|6Δ − 4m₀\|, \|6Δ − 2m₀\|)/N_b² (FR-041). **Slope continuity:** at every segment boundary, \|Δg[n] − Δg[n−1]\| ≤ the larger in-segment bound of the two adjacent segments. g stays in [0, max(B, g₀)] throughout. **Positive control:** a linear-ramp g (slope steps at its ends) fails the same checker. **(b) Clicks:** `ClickDetector` with the documented defaults (`artifact_detection.h:38–44`) reports **zero** clicks across every covered transition. **Positive control:** the same detector reports ≥ 1 click on a spliced instantaneous level step. **(c) Sample-delta bound:** every \|y[n] − y[n−1]\| during a transition ≤ **1.1 × (G_max·D_max + S_max·P_max)**. D_max and P_max are the largest unit-gain steady-state max \|Δw\| and peak \|w\| among the waveforms involved (at the sweep's highest f for E-2). G_max is the largest total gain (level × g) the analytic curve reaches: B·level for any PUNCH arm or re-arm. S_max is the analytic peak per-sample change of the transition's control curve (raised cosine \|A\|·π/(2N), the Hermite bridge's analytic peak slope, the level smoother's documented first step), never read from the measured trace. For a waveform crossfade, the S·P term is Δc_max·max\|w_new − w_old\|, with c the documented crossfade weight. | `AnchorOscillator_EnvelopesAreC1`, `_C1CheckerPositiveControl`, `_TransitionsClickFree`, `_ClickDetectorPositiveControl`, `_TransitionDeltaBound`. |
| **SC-007** | L/R bit-identity (line 328; theme-level mono FR) | Over every SC fixture, every waveform × mode × policy × host blocks {1, 37, 64, 512, 4096}, including non-finite inputs (SC-015): L == R **bit-identically**. Mono sum = 2·L exactly. | `AnchorOscillator_LrBitIdentical` (runs inside every fixture via the helper). |
| **SC-008** | FOLLOW input contract, and r ≡ 1 on a heavily folded voice (line 328; FR-045, FR-047) | Under the resolved OQ-1 mechanism (engine-derived ratio r, Q1). **(r) Reference on the folded voice:** processed voice = core at `kSawAnchor` and `kGrowl` → max excitation (mix 1, drive 1, asymmetry 1) → `HarmonicFold` at 100, at E0, E1, E2, C3. An offline double-precision harmonic-sum perceived-fundamental estimate over the descriptor frame must equal f₀ within **1 cent**. This is FR-047's evidence that r ≡ 1 is correct for these stages, so FOLLOW on this voice correctly renders LOCK's output. It is **not a tracking test**: the Anchor has no audio input, and the tracking clause moves to Phase 9 with the first pitch-moving stage (FR-066, SC-021). **(a) Input-contract accuracy:** static r ∈ {0.5, 0.75, 1.5, 2} at E1, C2, C3 (r·f₀ below the cap): the anchor's fundamental measured from the render is r·f₀ within **1 cent**. Along a ±12-semitone 200 ms f₀ glide at r = 1.5, the instantaneous frequency (below) is within **±5 cents** of r·f₀(t) from 50 ms after the glide's end. **(b) Latency:** instantaneous frequency f_inst[n] = wrap(phase()[n] − phase()[n−1])·fs from the `phase()` observer, rendered in 1-sample blocks (double phase, so 5 cents is resolved per sample). After a step in f₀, f_inst is within 5 cents of the new r·f₀ within ≤ **32 samples** (one `kControlInterval`). After a step in r, within ≤ **kFollowGlideSamples + 16 ≤ 32 samples** (FR-045). f_inst never leaves the interval between the old and new frequency (phase-continuous, no overshoot). **(c)** FOLLOW with r = 1 is bit-identical to LOCK on a static note and, fed the same trajectory input, on every trajectory. **(d)** The Drop / Glide rows of FR-049 are SC-025. | `ProfundumAnchor_FollowReferenceOnFoldedVoice`, `AnchorOscillator_FollowRatioAccuracy`, `_FollowLatency`. |
| **SC-009** | Anchor audible through maximum Fold (line 329) | Sum S = [core (mid grid and `kSawAnchor`) → max excitation (mix 1, drive 1, asymmetry 1) → `HarmonicFold` 100] + Anchor (level 1, LOCK), against reference R = [core, excitation mix 0, Fold 0] + Anchor, same calls. The Anchor's core-h1 amplitude input (FR-023) is the same value in S and R (the unprocessed core's h1), so only the core path differs. **Gated rows:** sine, triangle, rounded sine and asymmetric sine, whose fundamental f₀ coincides with core h1. At E0, E1, E2, two gates (re-ruled 2026-10-12, Clarifications "Session 2026-10-12"): **(i) coherence** — the summed fundamental's change equals the coherent-sum prediction 20·log₁₀(\|1 + ρ·e^{jθ}\|/2) from the recorded core-only h1 amplitude ratio ρ and phase θ within **0.1 dB** (unit-h1 Anchor; the triangle / rounded / asymmetric h1 fractions leave ≤ 0.03 dB residual) — the Anchor path is untouched and phase-coherent; **(ii) bound** — \|E_sub(S) − E_sub(R)\| ≤ **4.1 dB**, the worst case Phase 2's ratified −12 dB h1 clause allows at level 1 (20·log₁₀((1 + 0.251)/2) = −4.08 dB). **Recorded with each gated row:** the core-only h1 change \|w₁(core processed) − w₁(core reference)\| in dB, and core h1's phase relative to the Anchor in both chains (`goertzelPhase`). The comply evidence therefore shows how far Fold moved the core's own fundamental. The original 1 dB bar on the coherent sum was unreachable at the ratified k = 1: Phase 2 Q4 lets the Fold thin h1 to −12 dB "because the Anchor exists", and the T018 run measured the core-only h1 at −3.6 / −4.7 / −5.3 dB (mid grid E0 / E1 / E2) and +2.5 dB (`kSawAnchor` E1 / E2), giving −1.6 … −2.3 dB and +1.3 dB on the sum (bar then 1 dB) while the Anchor's own sub energy was identical in both chains (octave-down rows 0.0000 dB). **Octave-down rows: recorded, not gated** (f₀/2 carries no core content, so E_sub there is the Anchor alone in both chains). The "+ Character" clause is carried to Phase 9 (D-7, FR-066, SC-021). | `ProfundumAnchor_SubPreservedThroughMaxFold`. |
| **SC-010** | Aliasing ≤ −80 dBFS at E0–E2 (theme-level FR) | Every waveform, level 1, PUNCH at +12 dB peak (the loudest state), at E0, E1, E2 and 44.1 / 48 / 96 kHz: aliased power ≤ **−80 dBFS** (Phase 2 SC-005 method). **Positive control:** a naive (uncorrected) piecewise-linear triangle at C4 / 44.1 kHz must read above the bar, proving the gate has teeth. Recorded (not gated): every waveform per octave E2 → C5. | `AnchorOscillator_NoAliasing`, `_AliasGatePositiveControl`. |
| **SC-011** | DC ≤ −80 dBFS | Every waveform (the asymmetric sine included), steady state, integer-period window ≥ 1 s, at E0, E1, C3: DC ≤ **−80 dBFS**. | `AnchorOscillator_NoDc`. |
| **SC-012** | **Audibility gate** (FR-060) | At C2 (and E1): **(a) Waveform:** pairwise distance between the five waveforms' harmonic-power vectors (dB, n ≤ 15, re their own w₁; octave-down by its fundamental's octave) ≥ **6 dB** on at least one harmonic per pair, and octave-down's fundamental is 1 octave below the others. Analytic: rounded sine vs triangle **7.04 dB** at h3 (−12.04 vs −19.08 dB), asymmetric sine vs every other waveform ≥ **12 dB** at h2 (FR-017), so the pairs are separated at h2 / h3 and not by h5 alone. Harmonic dB values are floored at **−90 dB** re w₁ before differencing (a pair is separated only on a harmonic where at least one is above the floor); the sine / octave-down pair is separated by the octave clause; a same-waveform render pair must read *not separated* (plan S1 C-14, ratified 2026-10-11). **(b) Level:** E_sub strictly monotonic over a 33-point `level` sweep, span ≥ **40 dB − 1e-4 dB** between level 0.01 and 1 (plan S1 C-9, re-ruled 2026-10-11: the analytic span is exactly 40.0000000 dB and float rounding of ≈ ±5e-7 dB per product can read 39.9999995; 1e-4 dB is the recorded measurement allowance under FR-065, strict monotonicity unchanged). **(c) Punch boost:** first-(rise + time) window energy strictly monotonic over boost ∈ [0, 12] dB (13 points), span ≥ **6 dB**. **(d) Punch time:** energy in the window [rise, rise + max time] strictly monotonic over 9 time values, span ≥ **3 dB**. **(e) Ghost time:** energy in the first 10 ms strictly monotonic (decreasing) over 9 time values from min to max, span ≥ **10 dB**. **(f) FOLLOW ratio:** r ∈ {0.5, 2} moves the fundamental to r·f₀ within **1 cent**. **(g) Mode:** at the D-4 defaults, each pair is measured over a window tied to its envelope, same calls: GHOST/LOCK over [0, `ghostTimeMs`] ≤ **−3 dB**; PUNCH/LOCK over [0, `kPunchRiseMs` + `punchTimeMs`] ≥ **+3 dB**; PUNCH/GHOST over [0, `kPunchRiseMs` + `punchTimeMs`] ≥ **+3 dB**. Analytic values (sine, 48 kHz) at E1 / C2 / C3: GHOST/LOCK −6.32 / −4.11 / −4.13 dB, PUNCH/LOCK +3.90 / +3.87 / +3.76 dB, PUNCH/GHOST +5.17 / +5.02 / +4.87 dB (re-ruling R-2). | `AnchorOscillator_AudibilityGate`. |
| **SC-013** | Per-voice independence (line 324; FR-051) | Two instances with different notes, policies and modes, processed interleaved in blocks {1, 37, 512}: each output is bit-identical to the same instance processed alone. A source scan finds no `static` non-const data member in `anchor_oscillator.h`. | `AnchorOscillator_InstancesIndependent`. |
| **SC-014** | RT safety (FR-053) | Zero allocations (`AllocationScope`) across `setControls`, `setFrequency`, `setRetriggerPhase`, `noteOn` (both policies), `setFollowRatio` and `processBlock` (blocks 1, 37, 64, 512, 4096; every waveform and mode, transitions active). Every audio-thread method is `noexcept` (`static_assert`). Before `prepare`: output zeros, no state change. | `AnchorOscillator_RealTimeSafe`. |
| **SC-015** | Robustness (FR-054, FR-055) | NaN / ±Inf (by bit pattern) in each Controls field, in `noteOn` / `setFrequency`, in `setFollowRatio` and in `f0PerSample` elements keep the previous value. A finite denormal in a Controls field is flushed to 0.0 and then clamped: it reads back as 0.0 for `level` and `punchBoostDb` and as the field's minimum for `punchTimeMs` (5 ms) and `ghostTimeMs` (1 ms), through `pendingControls()` (plan S1 C-11, ratified 2026-10-11). Output finite, \|y\| ≤ the documented ceiling, `stateFinite()` true. TU in the `-fno-fast-math` block. | `AnchorOscillator_NonFiniteInputs` (in `anchor_oscillator_nonfinite_test.cpp`). |
| **SC-016** | Determinism, block-size independence, rate changes | Two instances, same call sequence: bit-identical. Host-block partitions {512, 64, 37, 1} with no setter calls between blocks: bit-identical, including a pending envelope and an `f0PerSample` vibrato. After `prepare` 48 → 96 → 44.1 → 22.05 → 192 kHz: SC-001 (E1 smoke), SC-002 and SC-003(a) (10 s smoke) hold at each rate. A `render_fingerprint` comparison is used only for the cross-rate smoke, never as a bit-exact golden. | `AnchorOscillator_Deterministic`, `_BlockSizeIndependent`, `_SampleRateChange`. |
| **SC-017** | Retrigger and onset (FR-006, FR-016) | **Reset:** the first sample after `noteOn` is exactly 0.0 for every waveform (the asymmetric sine through its onset ramp). Renders after a Reset noteOn from different prior states are bit-identical in every mode, given the same pending Controls and follow ratio. The prior states cover different f₀ and Controls, a running waveform crossfade, a moving level smoother, g mid-PUNCH and mid-GHOST, and elapsed time; the claim holds because a Reset noteOn snaps every stateful control (FR-006). **Enum mapping:** a test TU `static_assert`s that `AnchorOscillator::RetriggerPhase`'s underlying enumerator values equal `ProfundumCore::RetriggerPhase`'s (FR-006, D-6). **FreeRunning, same f₀ and Controls, LOCK:** bit-identical to an uninterrupted render. **No re-arm (FR-048):** a noteOn with `rearm = false` leaves g bit-identical to the uninterrupted g trace under both policies, and under Reset the phase restart is the only discontinuity; the prior-state bit-identity above holds for noteOns carrying `rearm = true`. **Onset:** the max sample delta over the first 10 ms of every waveform at level 1 ≤ **1.1 ×** its steady-state max delta. | `AnchorOscillator_RetriggerPolicies`, `_OnsetClickFree`. |
| **SC-018** | **CPU ≤ 0.3 % per voice** (line 330) | Worst case over waveform × mode (FOLLOW included, with the engine-derived ratio of OQ-1), with a waveform switch and a PUNCH re-arm every 100 ms and a 5 Hz vibrato `f0PerSample`, host block 64, 48 kHz: time / audio time ≤ **0.3 %** of one core, median of 5, pinned and isolated. The budget is additive to the core's 1.0 % and Phase 2's 1.5 % (Phase 9 sum 4.8 %, line 512). | `AnchorOscillator_CpuBudget` `[.perf]`, `node tools/run-cpu-tests.js dsp_systems_tests`. |
| **SC-019** | Footprint (FR-063) | A scripted `git diff` against the phase base (run in comply, output quoted) shows zero diff lines in every shared DSP header, in `profundum_core.h`, `harmonic_fold.h`, and every Phase 1/2 test TU, **unless** the D-1 fallback was invoked. In that case the diff is append-only, and the Innexus, Ruinae, Seraphis and Vorago suites run green. | `node` diff-check recorded in comply. |
| **SC-020** | Bar integrity (FR-065) | A `node` script extracts every numeric threshold of this table at build start and at comply. Any change cites a Clarifications re-ruling with evidence. | `node` diff-check recorded in comply. |
| **SC-021** | Deferred-clause handoff (FR-066) | A `node` script run in comply (output quoted) checks `specs/Profundum-roadmap.md`. Inside Phase 9's success-criteria paragraph it must find the Anchor-through-Fold-and-Character clause (the strings "within 1 dB", "every Character algorithm" and "E0–E2"), the FOLLOW-tracking clause ("FOLLOW", "pitch-moving stage" and "capability"), and the join / Drop-routing clause ("after Character" and "Drop"). Inside Phase 5's `BassCharacterStage` bullet it must find an r-law deliverable naming each of Rectify, Crush and Fracture. Inside Phase 4's Note Memory bullet it must find "re-arm". Inside Phase 6's Mass text it must find "h1 amplitude". **If SC-022 passes** it must also find `anchorFoldFeed` in Phase 9, in Phase 8's `MorphState` text and in Phase 11's 300–399 block; **if SC-022 fails** it asserts those three are absent and comply records "feed not shipped". Any missing string fails comply. | `node` roadmap check recorded in comply. |
| **SC-022** | Pre-Fold feed (FR-067, adopted by Q4; ships only if it passes) | FR-067's chain at E1 and C2, feed g ∈ {0, 1}, Fold ∈ {65, 100}, Anchor sine, LOCK, level 1. **(1) Audible:** between feed 0 and feed 1, the summed output's THD moves by ≥ **3 dB** or its harmonic centroid by ≥ **0.25 oct**, at both Fold settings; otherwise the feed is not shipped. **(2) Sub preserved (re-ruled 2026-10-12):** E_sub(feed 1) ≥ E_sub(feed 0) − **1 dB** at f₀ (the sub is never lost) **and** E_sub(feed 1) − E_sub(feed 0) ≤ **6.27 dB** (6.02 dB coherent doubling + 0.25 dB: the Fold cannot add more fundamental than the Anchor's own copy; a higher reading would mean h1 generated by the fold). The rise is recorded per cell (T018 measured +3.95 dB at Fold 65 and +4.41 dB at Fold 100, E1 and C2); any level trim when the feed is raised is Phase 9's. **(3) Mono:** L == R bit-identically (`requireLREqual`). **(4) Direct path:** at feed 1 the direct post-Fold Anchor path is present (the fundamental survives Fold 100, covered by (2)). | `ProfundumAnchor_FoldFeedAudible`, `_FoldFeedSubPreserved`, `_FoldFeedMono`. |
| **SC-023** | Portability (FR-058) | `node tools/check-portability.js` and `node tools/lint-apple-globals.js` pass on the phase's files. Both outputs are quoted in comply. | Recorded in comply. |
| **SC-024** | Level calibration relative to core h1 (Q6; FR-023–FR-025) | **(a) Ratio:** for each of `kSineAnchor`, `kSawAnchor`, `kHeavy`, `kHollow`, `kGrowl`, `kDefaultControls` and the mid grid, at E1 and C2 (48 kHz), the Anchor-to-h1 level ratio (Anchor RMS over the RMS of a sine of amplitude a₁, a₁ from the voice's source) equals k within **0.1 dB** and the spread across states is ≤ **0.1 dB** (the calibration is relative, so it cannot depend on the state). **(b) Summed fundamental:** w₁ of core + Anchor (sine, LOCK, in phase) minus the core's w₁ equals 20·log₁₀(1 + k) within **0.25 dB**; the figure is recorded per state (≈ +6.02 dB at k = 1). **(c) Default level:** the summed-signal RMS rise at the default `level` over the core alone is recorded per state and gated at **≤ 3.6 dB** on every listed state (plan S1 C-2, ratified 2026-10-11: k = `kAnchorToH1` = 1.0, default `level` = 0.5; analytic maximum 3.52 dB on `kSineAnchor` where the rise is the coherent sum itself; model 0.00 … 3.52 dB). **(d) No compensation:** the core's output with the Anchor attached is bit-identical to the core alone, and the summed buffer equals core + Anchor sample-wise exactly (a plain float add, no gain or normalisation stage between). | `ProfundumAnchor_LevelCalibration`, `_NoCompensation`. |
| **SC-025** | Pitch-trajectory contract per mode (Q3; FR-049) | At E1 and C2, 48 kHz, the test selects the trajectory the way the voice would. **(1) Note pitch change** (a fifth up): LOCK and FOLLOW (r = 1) reach the new f₀ within **1 cent** by ≤ **32 samples** (per SC-008(b)). **(2) Glide** (200 ms, ±7 semitones): both within **±5 cents** of the trajectory from 50 ms after its end. **(3) Drop** (a −12-semitone dip over 100 ms and return): the core is fed the dropping trajectory; the Anchor in LOCK is fed the pre-Drop resolved trajectory and its instantaneous frequency (from `phase()`) equals the resolved note pitch within **1 cent** throughout; the Anchor in FOLLOW (r = 1.5) is fed the post-Drop trajectory and equals 1.5·f₀(t) within **5 cents** from 32 samples after each update. SC-003's lock is asserted on the windows before the Drop begins, and no lock or beating bar applies during the Drop. | `AnchorOscillator_TrajectoryContract`. |
| **SC-026** | Phase continuity and convention (Q7; FR-035, FR-036) | **(a) Continuity:** across a FreeRunning noteOn (`rearm` true and false), Glide, Drop-shaped trajectories, LOCK ↔ FOLLOW switches, trajectory-input switches and waveform switches, the double-precision `phase()` increments equal the frequency law's expected per-sample increment within **1e-9 cycles** at every sample (no phase jump); only a Reset-policy noteOn restarts φ. **(b) Convention:** at a Reset noteOn the first sample is 0.0 and the fundamental of sine, triangle, rounded sine and asymmetric sine is in phase with core h1 within **1°** (octave-down: 2·θ_a − θ₁ within **1°**), measured by `goertzelPhase` after the first second. **(c) Phase reference:** with the core fed a Drop and the Anchor in LOCK fed the pre-Drop trajectory, the Anchor's `phase()` equals the integral of the pre-Drop trajectory (the common reference) within **1e-6 cycles** over the Drop. | `AnchorOscillator_PhaseContinuity`, `_PhaseConvention`, `_PhaseReference`. |
| **SC-027** | Re-arm event response (Q2; FR-048) | **(a) Timing:** a re-arm's g response begins at offset 0 of the block after the call, and the g trace is bit-identical across host blocks {1, 37, 512}. **(b) PUNCH:** a bridged arm reaches B within **±0.1 dB** and returns to 1 per SC-004(b) timings measured from the arm. **(c) GHOST:** a bridged arm's dip reaches 0 within `kGhostDipMs` + 1 sample and the emergence follows SC-005. **(d) Decoupling:** with `rearm = false`, g is bit-identical to the uninterrupted trace under both policies and all four modes, and the `phase()` trace is bit-identical between `rearm = true` and `rearm = false` calls of the same policy. **(e)** A noteOn from not sounding always arms. | `AnchorOscillator_RearmEvent`. |

---

## Edge Cases

- **E-1 f₀ at the floor.** f₀ = 8 Hz (`kMinF0Hz`): octave-down runs at 4 Hz. SC-003 and SC-011 hold at 8 Hz with a
  window of ≥ 4 periods (recorded, not gated below E0).
- **E-2 f₀ near the cap.** When a waveform's harmonic reaches `capFrequency`, it is omitted or tapered (FR-012). The
  fundamental itself is always below the cap because f0 is clamped to it (FR-005). As f₀ rises through the cap
  region, the triangle loses harmonics continuously, with no step (SC-006(c) applies to a slow upward sweep).
- **E-3 Non-finite / denormal / negative inputs.** As FR-054 and FR-005: negative and denormal f0 clamp to 8 Hz;
  non-finite inputs keep the previous value.
- **E-4 Calls before `prepare`, null buffers, `numSamples == 0`.** No-op (FR-004, FR-007).
- **E-5 FreeRunning `noteOn` before any note.** Behaves as Reset, as in the core (`profundum_core.h:528`).
- **E-6 Re-arm during a GHOST emergence or a PUNCH fall (FreeRunning, `rearm = true`).** g never steps (FR-043, FR-044,
  SC-006). With `rearm = false` g is untouched (FR-048, SC-027).
- **E-7 Waveform switch mid-crossfade.** A new switch starts from the current blended output with no step. The plan
  documents whether it retargets or queues.
- **E-8 `ghostTimeMs` at its minimum.** At the 1 ms GHOST minimum, the curvature bound of SC-006(a) is evaluated per
  segment, and the sample-delta bound SC-006(c) still holds at C3 (the plan verifies at the highest test note). A
  `punchTimeMs` shorter than `kPunchRiseMs` cannot occur: D-4's range keeps `punchTimeMs` ≥ 5 ms > `kPunchRiseMs`
  = 1 ms.
- **E-9 Sample-rate change mid-note.** `prepare` resets (control thread). The next note follows the Reset semantics
  (SC-016).
- **E-10 Very long notes.** The phase state must not lose precision over hours. A `double` phase or an MCF state
  with determinant 1 qualifies. SC-003's 60 s bound is the gate, and the plan states the precision argument for
  ≥ 1 h.
- **E-11 Bank level crossfade on > 1-semitone jumps.** It changes the core's level, not h1's phase
  (`harmonic_oscillator_bank.h:261–270`), so the lock is unaffected (SC-003(c)).
- **E-12 Seed determinism.** Not applicable: no random source (FR-056).
- **E-13 Level 0 with PUNCH.** The output is exactly 0 (the gain multiplies the level).
- **E-14 Drop under LOCK.** The Anchor holds the resolved note pitch and legitimately differs from h1 in frequency
  (FR-049). No beating criterion applies; re-locking after the Drop is Phase 4's / Phase 9's (SC-025).
- **E-15 Trajectory-input or mode switch mid-note.** φ stays continuous and only the frequency changes (a step, or the r
  glide of FR-045); no click (FR-035, SC-006, SC-026).

---

## Decisions taken where the roadmap is silent (ratified in clarify, 2026-10-11)

- **D-1 Phase-lock mechanism.** The roadmap offers "shared `PhaseAccumulator` or locked at note-on — the
  `SubOscillator` divider idea" (line 314).
  - **Ratified (Q7): input-trajectory lock** (FR-031, FR-036), not phase extraction from the rendered h1.
    - The core's h1 is an MCF whose phase advances by exactly θ = 2·asin(ε/2) per sample. ε is set from f at the
      U = 16 cadence (`harmonic_oscillator_bank.h:732, 748–749`; `profundum_core.h:702–731`). Its s-state is
      sin(nθ)/cos(θ/2): exact phase, zero phase offset.
    - An Anchor that reproduces the same f sampling and grid reproduces the phase. The only residual is the float
      rounding of the core's ε = ε_base·d product, about 1e-7 relative. That is ≈ 0.09° over 60 s at E1, inside
      SC-003's 0.5° bar.
    - **The reference h1 is float, ISA-dispatched and FMA-contracted.** ProfundumCore runs the bank's Highway path
      (`harmonic_oscillator_bank.h:712–718`; `harmonic_oscillator_bank_simd.cpp:104–105`), so h1's state update
      rounds differently on SSE2, AVX2, AVX-512 and NEON. The per-sample rotation angle is fixed by ε, so state
      rounding acts like phase noise: a random walk of order √N·1e-7 rad, ≈ 0.01° over 60 s at 48 kHz. That is
      small against 0.5°, but it is an estimate, not a proof. SC-003's bars must hold on every CI ISA, and a
      failure on any ISA invokes FR-032's observer fallback (which reads the bank's actual coefficient instead of
      re-deriving it), never a re-ruling.
  - **Rejected: a `PhaseAccumulator` slaved to the core's phase.** It would need an h1 phase observer that does not
    exist (the bank has no state accessor).
  - **Rejected: composing `SubOscillator`.** It needs master phase-wrap flags and hard-resets its phase
    (`sub_oscillator.h:273–276`).
  - **Fallback:** FR-032.
- **D-2 Waveform definitions** (each zero-mean, sine-phase fundamental, then RMS-normalised, FR-011):
  - sine = sin φ;
  - triangle = (8/π²)·Σ (−1)ᵏ sin((2k+1)φ)/(2k+1)², **k < 16 (n ≤ 31)**, each term weighted by the recipe cap taper at
    (2k+1)·f (plan S1 C-1, ratified 2026-10-11: THD −18.335 dB, omitted tail −52.9 dB re h1, first omitted n = 33 at −60.7 dB;
    the literal "at the cap" is 466 odd terms at E0 / 48 kHz, unaffordable inside SC-018);
  - **rounded sine** = sin φ + a₃·sin 3φ with **a₃ = 0.25** (ratified, Q5: h3 at −12.04 dB, odd-only, clearly flat-topped,
    7.04 dB above the band-limited triangle's h3; "rounded" sits between sine and square, opposite to the triangle's
    sharpened peaks; peak 0.8910, crest 1.223);
  - **asymmetric sine** = sin φ − a₂·cos 2φ with **a₂ = 0.25** (ratified, Q5: h2 at −12.04 dB, peaks +1.25 / −0.75 before
    normalisation, crest 1.715, zero mean by construction, non-zero at φ = 0, hence FR-016's onset ramp);
  - octave-down = sin(φ/2).

  The −12 dB figures (a₃ = a₂ = 0.25) are ratified (Q5); SC-001(c)(d), SC-012(a) and FR-055 use them.
- **D-3 Level (ratified, Q6).** `level` ∈ [0, 1], 0 silent, 1 the calibrated **maximum** Anchor level, calibrated **relative to the
  core's h1 amplitude** (level 1 = a calibrated multiple k of a₁, FR-023, FR-024), not an absolute −12 dBFS RMS. The
  plan chooses k and the default by measurement across the representative named states and proposes the figures in its S1
  for ratification. The in-phase sum rising up to about +6 dB is expected; no auto-compensation or normalisation (FR-025).
  This matches the concept's "anchor 100 → 70 %" phrasing (concept lines 495–505). The smoothing time is a plan constant
  (≤ 10 ms).
- **D-4 Mode parameters and curves.**
  - `punchBoostDb` ∈ [0, 12], default 6.
  - `punchTimeMs` ∈ [5, 250], default 40.
  - `kPunchRiseMs` = 1 ms (constant).
  - `ghostTimeMs` ∈ [1, 100], default 15. This covers "milliseconds to tens of milliseconds" (line 319).
  - Segments that start at rest are raised cosines, with zero slope at both ends. Trajectory changes while g may be
    moving use the cubic Hermite bridges of D-5 and FR-041.
- **D-5 Transition rules.** Every change to g's trajectory (re-arm on a sounding FreeRunning voice, mode switch) starts from the current
  g and g' with no slope step, through a **cubic Hermite bridge** to the new segment's start value with zero end
  slope (FR-041). A raised cosine cannot serve here: its start slope is zero, so it cannot leave a moving g without
  a slope step. The bridge's analytic peak \|Δ²g\| is SC-006(a)'s reference. `kGhostDipMs`, `kModeBridgeMs` and
  the waveform crossfade time are plan constants (≤ 10 ms each); `kFollowGlideSamples` ≤ 16 (FR-045). A Reset
  noteOn is not a transition: it snaps (FR-006).
- **D-6 Retrigger enum mirrored, not included** (Include discipline). The same spelling and order as
  `ProfundumCore::RetriggerPhase` are kept, so Phase 9 maps them with a `static_cast` guarded by `static_assert`.
  This phase already asserts the equal underlying values in a test TU (FR-006, SC-017).
- **D-7 Character clause deferred (ratified).** "Audible through maximum Fold + Character" is measured in Phase 3 through max excitation + max Fold; the full clause with Character is carried to Phase 9's chain test. The roadmap's "anchor audible through maximum Fold + Character" (line 329)
  cannot be measured in Phase 3: `BassCharacterStage` is Phase 5's and does not exist. SC-009 measures it through
  max excitation + max Fold. Phase 9's chain test carries the full clause with the same 1 dB bar. The handoff is
  enforced: FR-066 amends roadmap Phase 9's success criteria, and SC-021 checks the text in comply.
- **D-8 CPU reading.** It is additive (as Phase 2's D-10): the Anchor's 0.3 % is its own cost, and the sum
  1.0 + 1.5 + 0.3 + 2.0 = 4.8 % per voice matches roadmap line 512.
- **D-9 Re-arm decoupled from the phase policy (Q2).** FR-048. The interface is `noteOn(f0, rearm)` (plan may add `rearm()`);
  Phase 4's Note Memory decides when it is issued; Phase 3 tests only the response.
- **D-10 Per-mode trajectory contract (Q3).** FR-049: LOCK ignores Drop, FOLLOW follows it; the voice selects the trajectory.
- **D-11 Engine-derived FOLLOW ratio (Q1).** FR-045: zero latency, inert until a Phase 5 stage publishes an r law.
- **D-12 Pre-Fold feed adopted (Q4).** FR-067, SC-022: ships only if it passes the audibility gate.

---

## Resolved Open Questions (assigned to this spec by the roadmap)

### OQ-1 — Anchor FOLLOW mechanism (roadmap lines 808–810) — RESOLVED (Q1)

**Decision: engine-derived perceived-pitch ratio** r = f_perceived / f₀ (FR-045), supplied by `setFollowRatio` and a
per-sample pointer. Zero latency, deterministic, bit-identical to LOCK at r = 1 on a static note. It is inert until a
Phase 5 stage (Rectify, Crush, Fracture) publishes an r law; Phase 9's capability table must prove FOLLOW audible or flag
it inert (FR-047, FR-066, SC-021).

**Rejected: YIN analysis** (`YinPitchDetector` + `SubharmonicValidator`). Evidence:
- YIN analyses a caller-supplied window with lag ≤ N/2 (`yin_pitch_detector.h:192–194`) and maxTau = ceil(fs/minF0) + 1
  (459–463). Tracking E0 (20.6 Hz) at 48 kHz needs maxTau ≈ 2331, so N ≥ 4664: a 4096 window is too short, and 8192 is
  ≈ 171 ms of analysis. Each hop needs two FFTs of 2N plus `SubharmonicValidator`'s STFT spectrum, and the detector
  allocates in `prepare` (127–170). Latency is ≥ 85 ms at E0 even with half-window hops, three orders of magnitude over
  SC-008(b)'s 32 samples, and plausibly over the 0.3 % CPU budget.
- `PitchTracker` is excluded outright (`PitchDetector::kMinFrequency = 50.0f`).
- Every stage built so far (excitation, `HarmonicFold`) and the core are harmonic and never move the perceived fundamental,
  so on every Phase 1–3 voice the correct FOLLOW output is the LOCK output, and analysis could only add octave errors and
  latency.

**Sub-question (LOCK under Drop / Glide) — RESOLVED (Q3):** FR-049. LOCK holds the resolved note pitch (Glide followed, Drop
ignored); FOLLOW tracks the core's instantaneous f0 (Drop and Glide included) times r.

### OQ-4 — Anchor join point default and an advanced pre-Fold feed (roadmap lines 815–816) — RESOLVED (Q4)

- **Default join point: after Character (ratified).** The roadmap's stated default (lines 321–322, 491–492) and the
  concept's "the sub anchor is excluded" (line 370). It is a Phase 9 wiring property (FR-052, FR-066); Phase 3's SC-009
  measures what it can.
- **Pre-Fold feed: adopted** as the Phase 9 voice-level send `anchorFoldFeed` ∈ [0, 1], default 0, with the direct Anchor
  path always present, so the fundamental cannot be destroyed (FR-067). It ships only if it passes SC-022's 3 dB / 0.25
  octave audibility gate with sub energy within 1 dB.
  - Rationale: `HarmonicFold`'s Φ_fold acts on the whole input u, and its per-channel peak detector normalises the
    compression and folding regimes (`harmonic_fold.h` banner lines 14–21). A sub fed into it intermodulates with the voice
    and moves the detector: a distinct, controllable "the sub drives the fold" colour.
  - Phase 8's `MorphState` and Phase 11's 300–399 block each gain the field (FR-066, SC-021), if the feed ships.

---

## Traceability

| Roadmap statement (line) | FR | SC |
|---|---|---|
| L3 `systems/anchor_oscillator.h` (311) | FR-001 | SC-019 |
| Waveforms: sine, triangle, rounded, asymmetric, octave-down (313) | FR-003, FR-010–FR-016, D-2 | SC-001, SC-002, SC-010, SC-011, SC-012(a) |
| Phase-locked to core h1 (313–315) | FR-030–FR-036, D-1 | SC-003, SC-026 |
| LOCK (316) | FR-040 | SC-007, SC-008(c) |
| FOLLOW; OQ-1 (316–318, 808–810) | FR-045, FR-046 (withdrawn), FR-047, FR-049, FR-066, OQ-1 | SC-008, SC-012(f), SC-021, SC-025 |
| PUNCH (318) | FR-041, FR-042, D-4 | SC-004, SC-006, SC-012(c)(d) |
| GHOST (318–320) | FR-041, FR-043, D-4 | SC-005, SC-006, SC-012(e) |
| Strictly mono, L == R (321; 125–126; 791) | FR-050, FR-062 | SC-007 |
| Excluded from Fold/Shape/Character; join after Character; OQ-4 (321–323, 815–816) | FR-052, FR-067, OQ-4, D-7 | SC-009, SC-022 |
| One instance per voice (324) | FR-051 | SC-013 |
| THD per waveform (326) | FR-010 | SC-001 |
| Zero phase drift over 60 s (326) | FR-030, FR-031, FR-032 | SC-003 (incl. (e) processed voice) |
| PUNCH/GHOST measured, C¹ (327) | FR-041–FR-044 | SC-004, SC-005, SC-006 |
| FOLLOW accuracy and latency on a folded voice (328) | FR-045, FR-047, FR-066 | SC-008 (input contract, r ≡ 1 reference), SC-021 (tracking clause handed to Phase 9) |
| Sub energy within 1 dB through max Fold + Character (329) | FR-052, FR-066, D-7 | SC-009, SC-021 (+ Phase 9) |
| CPU ≤ 0.3 % per voice (330; 796) | FR-064, D-8 | SC-018 |
| Audibility / anti-aliasing theme FRs (788, 793) | FR-060, FR-061 | SC-010, SC-012 |
| RT safety, no bit-exact goldens, portability (786–804) | FR-053–FR-058 | SC-014–SC-016, SC-023 |
| Shared-header changes append-only (803–804) | FR-032, FR-063 | SC-019 |
| PUNCH / GHOST re-arm decoupled from phase policy (Q2) | FR-006, FR-042, FR-043, FR-044, FR-048 | SC-005 (P3), SC-006, SC-017, SC-027 |
| LOCK ignores Drop, FOLLOW follows it (Q3) | FR-005, FR-030, FR-049 | SC-003, SC-025 |
| Pre-Fold feed adopted (Q4) | FR-066, FR-067 | SC-021, SC-022 |
| Waveform coefficients 0.25 / 0.25 (Q5) | FR-017, FR-055 | SC-001, SC-012(a) |
| Level relative to core h1 (Q6) | FR-011, FR-023–FR-025, FR-055 | SC-002, SC-024 |
| Input-trajectory phase lock, continuity, convention (Q7) | FR-031, FR-035, FR-036 | SC-003, SC-026 |
| Character clause measured via max Fold, full clause to Phase 9 (D-7) | FR-066 | SC-009, SC-021 |

## Assumptions

- Phase 9 calls the Anchor and the core with the same resolved-note-pitch inputs and the same phase policy; only the
  transient Drop reaches the core alone (FR-049). The phase lock is defined relative to that call contract (FR-005, FR-030).
- The core's h1 carries the f₀ the Anchor locks to. Recipe states with h1 = 0 do not exist: Depth biases h1 but never
  removes it (Phase 1 recipe).
- Glide reaches the Anchor in every mode and the transient Drop reaches the core (and the Anchor in FOLLOW) only; the voice
  selects the trajectory (FR-049). Phase 4's Note Memory decides when the re-arm event is issued (FR-048).
- The voice amp envelope's application to the Anchor (and whether the Anchor has its own release) is Phase 9's.
  The Anchor's mode envelope is attack-only.

## Clarifications

### Session 2026-10-11 (clarify)

- Q1 (OQ-1, FOLLOW mechanism): engine-derived perceived-pitch ratio r = f_perceived / f₀ via `setFollowRatio` plus a per-sample pointer; zero latency, deterministic, bit-identical to LOCK at r = 1 on a static note; inert until a Phase 5 stage publishes an r law, and Phase 9's capability table must prove FOLLOW audible or flag it inert; YIN analysis rejected (latency ≥ 85 ms at E0, allocation, CPU). [FR-005, FR-045, FR-046, FR-047, FR-066, SC-008, SC-012(f), SC-018, SC-021]
- Q2 (re-arm vs phase policy): decoupled. Re-arm of PUNCH / GHOST is its own explicit event (`noteOn(f0, rearm)` here; a standalone `rearm()` is allowed with identical semantics); the phase policy governs only phase behaviour and the grid; Phase 4's Note Memory decides when the event is issued; Phase 3 defines the interface and tests the response, with no over-designed re-arm policy; FR-006 / FR-042 / FR-043 / SC-005 (P2) / SC-006 / SC-017 gain a no-re-arm row; LOCK / FOLLOW phase behaviour stays independently specified. [FR-005, FR-006, FR-042, FR-043, FR-044, FR-048, FR-066, SC-005, SC-006, SC-017, SC-027]
- Q3 (LOCK under Drop / Glide): LOCK holds the note's resolved pitch, FOLLOW tracks the core's instantaneous f0 (Drop and Glide included) times r; table: note pitch change LOCK follows / FOLLOW follows, Glide follows / follows, transient Drop ignores / follows; LOCK's input is the resolved note pitch trajectory after tuning, note-level pitch processing and Glide but before Drop; the voice selects the trajectory per mode (Phase 4 / 9 route Drop to the core only); SC-003 is asserted whenever the core is on its resolved trajectory and no beating criterion applies during a Drop under LOCK. [FR-005, FR-030, FR-049, FR-066, SC-003, SC-025]
- Q4 (OQ-4, pre-Fold feed): adopted as the Phase 9 voice-level send `anchorFoldFeed` ∈ [0, 1], default 0, with the direct Anchor path always present; ships only if it passes the 3 dB / 0.25 octave audibility gate with sub energy within 1 dB; Phase 8 `MorphState` and the Phase 11 300–399 block each gain the field; default join point after Character ratified. [FR-052, FR-066, FR-067, SC-021, SC-022]
- Q5 (D-2 coefficients): rounded sine h3 at −12 dB (a₃ = 0.25, 7 dB above the triangle's h3) and asymmetric sine h2 at −12 dB (a₂ = 0.25); crest factors recomputed (rounded 1.223, asymmetric 1.715, triangle 1.732 still the maximum), so the 1.75 ceiling is unchanged at k = 1; SC-001(c)(d) and SC-012(a) use the new figures. [FR-017, FR-055, SC-001, SC-012]
- Q6 (level range / default): level ∈ [0, 1], 0 silent, 1 the calibrated maximum Anchor level, calibrated relative to the core's h1 amplitude (supplied per control block) by a multiple k, not an absolute −12 dBFS RMS; the plan chooses k and the default by measuring the Anchor-to-h1 ratio and summed-fundamental level across `kSineAnchor`, `kSawAnchor`, `kHeavy`, `kHollow`, `kGrowl`, `kDefaultControls` and the mid grid, checks the default for a perceptible loudness jump, and proposes the figures in S1 for ratification; the in-phase sum rising up to about +6 dB is expected; no auto-compensation or normalisation of the core or the combined signal (validated later through Phase 6 Mass dynamics and the output safety stages); core and Anchor share one output-level convention. [FR-011, FR-020, FR-023, FR-024, FR-025, FR-055, FR-066, SC-002, SC-024]
- Q7 (D-1 phase lock): input-trajectory phase lock, not phase extraction from the rendered h1; core and Anchor receive a common phase reference at the resolved note pitch after Glide and before Drop (LOCK follows it, FOLLOW follows the core's post-Drop trajectory times r); phase stays continuous when switching trajectories or modes and is never hard-reset on an update; PUNCH / GHOST trigger stays with the note engine; a fixed phase offset and waveform-specific phase convention are defined and continuity is tested across note-on, Glide, Drop and mode changes; the h1 observer fallback only if SC-003 fails on any CI ISA, otherwise zero shared-header diff. [FR-031, FR-032, FR-035, FR-036, FR-063, SC-003, SC-019, SC-026]
- D7 (Character clause): ratified; "audible through maximum Fold + Character" is measured in Phase 3 through max excitation + max Fold, and the full clause with Character is carried to Phase 9's chain test. [FR-066, SC-009, SC-021]

Encoding notes. The OQ-1 option letters in this spec ((a) YIN, (b) engine-derived) differ from the interview's Q1 letters; the decision is the engine-derived ratio. The interview's Q2 example form `noteOn(f0, rearm)` was chosen; a Reset noteOn carrying a re-arm is a full restart, and `rearm = false` on a sounding voice leaves g untouched (FR-006, FR-048).

### Prior review re-rulings

**Review re-rulings (2026-10-11, specify-stage challenge, under FR-065).** Each bar below was shown to be unreachable
or ill-defined against the spec's own D-4 curve or OQ-1 recommendation. None was relaxed to pass a measurement. The
figures were computed in double with a scratch Node script (raised-cosine g, sine y_ref from phase 0, 48 kHz); the
tests recompute the same integrals inside the test binary.
- **R-1 SC-005(d).** Was "onset-window energy ≤ −6 dB re LOCK". D-4's raised cosine has mean g² = 3/8 (−4.26 dB)
  over [0, T]. A Reset onset also starts y_ref at a zero crossing, which weights y_ref² toward the end of the window
  where g ≈ 1, giving ≈ −1.6 dB at T ≪ period. Every grid point failed by construction. Now: the measured ratio
  matches the analytic Σ g_D4²·y_ref² / Σ y_ref² within ±0.25 dB.
- **R-2 SC-012(g).** Was "first-50 ms energies pairwise ≥ 3 dB at defaults". GHOST/LOCK over 50 ms is −0.89 to
  −1.00 dB at defaults, because the 15 ms emergence sits inside a 50 ms window. Now each pair is measured over its
  own envelope's window, where the analytic separations are ≥ 4.11 dB (GHOST/LOCK), ≥ 3.76 dB (PUNCH/LOCK) and
  ≥ 4.87 dB (PUNCH/GHOST). The 3 dB bars stand.
- **R-3 SC-006(a)(c).** (a) lacked the segment height A and was undefined for bridges leaving a moving g. (c) was
  violated by construction by any PUNCH arm, whose peak B (up to 3.98) lies between two g = 1 end states. Now (a)
  uses \|A\|·π²/(2N²) and the Hermite bridge's analytic peak, and (c) uses G_max·D_max + S_max·P_max from the
  analytic curve.
- **R-4 SC-008.** Under OQ-1 (b) the Anchor has no audio input, so "tracking on a heavily folded voice" could only
  measure LOCK. SC-008 now gates the input contract and the r ≡ 1 reference, and the tracking clause is handed to
  Phase 9 (FR-066). The 32-sample latency bar stands, reconciled with FR-045's glide (≤ 16 samples + U).
- **R-5 SC-009.** The anchor-alone phasor clause tested float addition, and the octave-down rows could not fail. The
  clause is replaced by recorded core-h1 evidence, and the octave-down rows are recorded, not gated. The 1 dB bar
  stands.

### Session 2026-10-11 — plan S1 ratification

All fourteen S1 items were ratified by the user on 2026-10-11, each at the plan's recommended resolution:

- C-1 ratified: triangle truncated at 16 odd terms (n ≤ 31), cap-tapered; THD −18.335 dB (bar −18.3 ± 0.3), omitted tail −52.9 dB, kernel CPU 0.14–0.16 % (bar 0.3 %). D-2 wording updated. [D-2, FR-012, SC-001(b), SC-018]
- C-2 ratified: k = `kAnchorToH1` = 1.0, default `level` = 0.5, SC-024(c) bound ≤ 3.6 dB RMS rise at the default on every named state (analytic max 3.52 dB on `kSineAnchor`, +1.33 dB on `kDefaultControls`). Source of a₁ = (a) the recipe's delivered h1 (`core.deliveredGains()[0] · kCenterPanGain`): `kHollow` (+0.03 dB) and `kGrowl` (+0.00 dB) stay unanchored by design — their weight comes from the Mass / Weight macros (Phases 6, 9). No floor, no absolute level. [FR-024, SC-024(c)]
- C-3 re-ruled (wording, no bar change): the bridge is shortened, never lengthened — N_b = the largest length ≤ nominal whose cubic stays in [0, max(B, g₀)], by integer bisection (≤ 11 checks per event at 192 kHz); model 0 out-of-range cases, max |Δ²g| / analytic 0.984–1.000 (bar 1.05). [FR-041, SC-006(a)]
- C-4 ratified: `modeGain()` returns double (a float observer's second difference is rounding noise 75× the 6.4e-9 bar); observers `phaseIncrement()`, `isSounding()`, `pendingControls()`; `mirrorState()` is a TEST-ONLY diagnostic, never serialised, no append-only obligation. [FR-008, SC-006(a), SC-014, SC-015, SC-026]
- C-5 ratified: the h1 coefficient law is mirrored bit-faithfully (base chase, the bank's float ε, the shadow product, the clamp) rather than re-derived in double; model drift 0.000° (literal double law 0.168°, 1-ulp disagreement 0.307°, bar 0.5°); the core's own float MCF walk ≤ 2.6e-3° in 60 s. FR-032's append-only observer fallback only on a red SC-003 on any CI ISA. Residual (recipe slew during a held note, ≤ 2 ulp) recorded for Phase 9's soak. [FR-031, FR-032, SC-003, SC-019]
- C-6 ratified: asymmetric-sine onset ramp = one period of f₀ (raised cosine on the h2 term; onset step ratio 1.0000, bar 1.1; first sample exactly 0.0). [FR-016, SC-017]
- C-7 ratified: silent before the first noteOn, f₀ / ratio bookkeeping kept as the core does. [FR-006, SC-014]
- C-8 ratified: Controls latch at offset 0 of the next block after a pending noteOn; a waveform switch during a crossfade is queued (latest wins); punch boost / time and ghost time are read at arm time; re-arm wins over a mode switch in the same block; no standalone `rearm()` (Phase 4 may add it append-only). [FR-006, FR-048, E-7]
- C-9 re-ruled (measurement allowance, strict monotonicity unchanged): SC-012(b)'s span gate is ≥ 40 dB − 1e-4 dB (analytic 40.0000000 dB; float rounding ±5e-7 dB). [SC-012(b), FR-065]
- C-10 ratified: the `anchorFoldFeed` roadmap amendment goes into Phase 11's paragraph ("registered in the 300–399 anchor block"); Phase 10's block list is unchanged. [FR-066, SC-021]
- C-11 ratified: denormal Controls are flushed then clamped; read-back 0.0 for `level` / `punchBoostDb`, the field minimum (5.0 ms / 1.0 ms) for `punchTimeMs` / `ghostTimeMs`. [FR-054, SC-015]
- C-12 ratified (wording, no bar change): the PUNCH return is the first bitwise 1.0 after the peak at N_rise + N_time ± 1 sample, within 0.01 dB afterwards, > 1 + 0.1 dB at the half-time point. [SC-004(b), FR-065]
- C-13 ratified: "no compensation" is structural in Phase 3 (independent objects, the test performs the sum); the summed-w₁ rise 20·log₁₀(1 + level·k) ± 0.25 dB and the layer grep are the clauses with teeth; the voice-level obligation goes to Phase 9. [SC-024(d), FR-025]
- C-14 ratified (measurement, no bar change): harmonics floored at −90 dB re w₁ before differencing; sine / octave-down separated by the octave clause; a same-waveform pair must read not separated. [SC-012(a)]

### Session 2026-10-12 — chain-gate re-rulings (T018 reds)

Both reds were measurement conflicts with ratified rulings, not Anchor defects: the Anchor's own sub energy was identical in
every S / R pair (octave-down rows 0.0000 dB) and L == R held bitwise on every row.

- SC-009 re-ruled (user, 2026-10-12, option "derive the bar from Phase 2's ruling"): the 1 dB bar on the coherent sum was a bar on
  Phase 2's Fold, which Phase 2 Q4 ratified may thin the core h1 to −12 dB "because the Anchor exists"; at k = 1 the sum can
  only stay within 1 dB if the Fold moves h1 by < 2.1 dB. Measured (48 kHz, max excitation → Fold 100): core-only h1 −3.5758 /
  −4.6555 / −5.3130 dB (mid grid E0 / E1 / E2), +2.5355 / +2.5274 dB (`kSawAnchor` E1 / E2), −5.0735 dB (`kSawAnchor` E0);
  sum −1.6058 … −2.3112 dB and +1.2556 … +1.3184 dB; core h1 phase re Anchor +1.4° … −17.5°. New gates: (i) coherence — the sum's
  change equals 20·log₁₀(\|1 + ρ·e^{jθ}\|/2) within 0.1 dB; (ii) bound — \|ΔE_sub\| ≤ 4.1 dB, derived from Phase 2's −12 dB
  clause at level 1. Rejected: raising k (conflicts with C-2 / Q6), gating the Anchor alone (R-5), reopening Phase 2's Fold.
  The roadmap's Phase 9 "within 1 dB" clause carries the same derivation (plan S13, T021). [SC-009, FR-052, FR-065]
- SC-022(2) re-ruled (user, 2026-10-12, option "one-sided floor plus a coherent ceiling"): at feed 1 the Anchor's h1 passes the
  Fold at about 0.58× (Fold 65) / 0.66× (Fold 100) and adds coherently to its always-present direct copy (FR-067), so the summed
  fundamental rose by +3.9509 / +4.4141 dB (E1, Fold 65 / 100) and +3.9560 / +4.4144 dB (C2); the ±1 dB bar failed upward only
  while (1) audibility passed on all four cells (THD −4.47 … −8.47 dB, centroid −0.38 … −0.86 oct) and (3) mono passed. New
  gate: E_sub(feed 1) ≥ E_sub(feed 0) − 1 dB and ≤ +6.27 dB (coherent doubling + 0.25 dB), rise recorded; the feed ships;
  Phase 9 owns any level trim. Rejected: not shipping the feed; subtracting a modelled Fold response in the test. [SC-022,
  FR-067, FR-065]
- SC-003(e)(ii) re-ruled (user, 2026-10-12, option "record (ii), keep (i) gated"): T023's 60 s `[long]` lane read the Anchor's
  drift against h1 after Fold 100 at C3 as −0.5935° (bar 0.5°; E0 −0.0075°, E1 −0.0064°, E2 −0.0435°) while the drift against the
  excited core's own h1 was ≤ 0.0303° everywhere. The clause's "invokes FR-032, never a re-ruling" assumed an Anchor-side
  failure; the probe `evidence/drift-probe.cpp` attributes the figure: the Fold on a perfectly periodic input drifts ≤ 0.0001°
  at every note, the core's partials walk relative to h1 by −0.53 / −0.54 / +8.35 / −2.46° (h3 … h6, C3, 60 s; each within
  Phase 1's 0.1 cent bar) and the folded h1 follows them (+0.5628° at C3, +0.0266° at E2, −0.0022° at E0). (ii) becomes a recorded
  figure with this attribution; (i) stays gated at 0.5°; FR-032 is not invoked because its premise (the FR-031 mirror failing)
  is false. Rejected: FR-032 anyway (predicted no change, shared-header cost); reopening Phase 1's bank (bars met). User note
  (2026-10-12): the slow inter-partial "breathing" of a held note over minutes is not a defect by default and may be musically
  welcome — the sub stays locked while the mass above it moves; Phase 9's soak records it and the listening pass decides, not a
  bar. [SC-003, FR-031, FR-032, FR-065]
