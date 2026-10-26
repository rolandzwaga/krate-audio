# Feature Specification: Profundum Phase 5 — Shape & Character

**Spec slug:** `profundum-phase5-shape-character`
**Status:** Clarified (2026-10-15). The clarify session decided Q1-Q8 and ratified every drafted bar and decision
(D-1 to D-12, OQ-S); see "Clarifications". Every decision is carried by an FR or SC row, never by the log alone.
**Roadmap source:** `specs/Profundum-roadmap.md` → Part A → **Phase 5** (lines 410–435). This spec also traces to the
following roadmap statements:
- **Architecture Overview** (lines 61–79). The per-voice chain is HARMONIC CORE → SHAPE ("ROUND·HOLLOW·BITE", "one
  morph control") → HARMONIC FOLD → CHARACTER ("Tube·Fold·Rectify·Bite·Crush·Fracture", "Amount + Tone each, L/R-capable").
  The Sub Anchor runs in parallel and joins after Character.
- **Key Design Decision 5** (lines 145–147): "No conventional filter section, no FX rack. Three character processors
  (ROUND / HOLLOW / BITE) behind one SHAPE control; six Character algorithms with Amount + Tone each."
- **Core Philosophy** (lines 39–54). It says "character tracks the keyboard" and "Alias-free low end is engineered …
  every nonlinear stage ships oversampled or ADAA with a measured aliasing bound at the lowest notes". It also says
  "Nothing ships inert".
- **Reuse Inventory**, the "Nonlinear primitives" row (line 162) and the "Shape processors" row (line 163). The
  verdicts are 🆕 `Rectifier` (L2), 🆕 `BassCharacterStage` (L3) and 🆕 `ShapeMorph` (L3). The rows also say:
  - "`CharacterProcessor` already exists in `systems/` — do not reuse that name";
  - "No generic `OversampledProcessor` wrapper exists".
- **ODR hazard list** (lines 175–177), which includes `CharacterProcessor` and `MorphEngine`.
- **Phase 7 consumer** (line 486): "WIDE adds small per-channel drive/tone offsets in the character stage. The Anchor
  never enters any of this."
- **Phase 8 consumer** (lines 505–520).
  - `MorphState` holds "Shape, Character algorithm + Amount + Tone".
  - The character algorithm morphs by a "stepped-with-crossfade or algorithm-specific transition".
  - The dark → bright axis covers "Shape / Tone in their own domains".
- **Phase 9 consumer** (lines 543–579).
  - The voice order is "core (+ edge excitation) → Shape → Harmonic Fold → Character".
  - Per-voice vs post-sum placement is decided by CPU measurement (line 546, Open Question 7).
  - The capability table needs "every section's on/off" (line 562).
  - The Anchor sub energy must stay "within 1 dB through maximum Fold + every Character algorithm at maximum Amount,
    at E0–E2" (lines 571–572).
  - FOLLOW must track "through a real pitch-moving stage" (line 576).
- **Cross-Cutting Constraints** (lines 856–877). They cover:
  - the audibility, mono-compatibility and anti-aliasing theme FRs;
  - "CPU budgets are FRs" ("per-voice budgets Phases 1–5", line 869);
  - no bit-exact float goldens;
  - portability.
- **Open Question 5** (line 890): "'Fracture' algorithm definition — Phase 5."
- **Open Question 10** (lines 899–902). The Shape **BITE** / Character **Bite** and Character **Fold** / **Harmonic
  Fold** name collisions are resolved in Phase 11, not here.

**Concept source:** `specs/Profundum-concept.md`.
- **§6 "Don't have a conventional filter section"** (lines 314–342) defines **three character processors**:
  - ROUND, "Low-pass-like behavior";
  - HOLLOW, "Band/reject/resonant behavior";
  - BITE, "High-frequency emphasis + nonlinear excitation".
  It adds one continuously variable **SHAPE** "which morphs between them. Internally you can do much more complicated
  things. The user doesn't need to know."
- **§13 "Processing architecture"** (lines 601–645) orders the chain SPECTRAL SHAPER → SATURATOR → CHARACTER. It makes
  "the **Character** stage the fun one", with six algorithms (Tube, Fold, Rectify, Bite, Crush, Fracture). Each
  algorithm "has only `Amount + Tone` rather than a giant FX rack".

**Upstream contract consumed (Phase 3 handoff, ratified).** From `specs/profundum-phase3-anchor/spec.md`:
- **FR-047** (lines 431–436): "Each stage that can move the perceived fundamental owns and documents its r law … A later
  stage that cannot state an r law records r ≡ 1 with its own measured evidence; r ≡ 1 is never assumed."
- **FR-066** (lines 519–531) wrote the Phase 5 clause into the roadmap (roadmap lines 427–429): "Rectify, Crush and
  Fracture each ship an r-law deliverable: the perceived-pitch ratio r = f_perceived / f0 as a function of the stage's
  controls with a measured reference, or r ≡ 1 recorded with evidence."
- **D-11 / Q1** (lines 689, 778). FOLLOW uses an engine-derived ratio r, supplied in one of two ways:
  - `AnchorOscillator::setFollowRatio(float r)` (`systems/anchor_oscillator.h:255`);
  - a per-sample pointer, `processBlock(…, const float* followRatioPerSample)` (lines 285–286).
  FOLLOW stays inert until a Phase 5 stage publishes an r law.

**Layer.** Three new components, all header-only in `Krate::DSP`, each named by the roadmap:
- `Rectifier` (L2, `processors/rectifier.h`);
- `ShapeMorph` (L3, `systems/shape_morph.h`);
- `BassCharacterStage` (L3, `systems/bass_character_stage.h`).

All three names pass the ODR sweep (see "New components"). This phase modifies no shared header (FR-064).

---

## Overview

Phase 5 replaces the conventional filter section and the FX rack with two stages and one primitive. Phase 9 wires all
three into the voice.

- **`ShapeMorph`** has one continuous **SHAPE** control in [0, 1] that travels ROUND → HOLLOW → BITE.
  - **ROUND** is low-pass-like, built on `LadderFilter` or the `SVF` low-pass, with gentle drive.
  - **HOLLOW** is band / reject / resonant behaviour, built on the `SVF` notch or band-pass with resonance.
  - **BITE** combines high-frequency emphasis (`SpectralTilt` or an `SVF` high shelf) with nonlinear excitation
    (`DiodeClipper`-style asymmetric clipping).
  - Neighbouring regimes are crossfaded with loudness compensation, so the sweep has no level jump and no descriptor
    jump.
  - From the outside it is one knob, and the internals are free to be rich.
- **`BassCharacterStage`** has six algorithms (Tube, Fold, Rectify, Bite, Crush, Fracture). **Amount** and **Tone**
  are the only controls.
  - Switching algorithms is a click-free crossfade.
  - The stage is L/R-capable: per-channel Amount / Tone offsets exist for Phase 7's WIDE mode.
  - Rectify, Crush and Fracture each publish an **r law** (perceived-pitch ratio) for the Anchor's FOLLOW mode.
- **`Rectifier`** blends half-wave and full-wave rectification. It is DC-blocked and anti-aliased. It implements
  Character's Rectify algorithm, and it is new because KrateDSP has no standalone rectifier.

Every nonlinear path is anti-aliased by oversampling or ADAA and measured at E0–E2. That covers ROUND's drive, BITE's
clipper and all six algorithms. Both stages report `latencySamples() == 0`. The change each stage makes to the core's
h1 amplitude and phase is recorded for Phase 9's Anchor coherent-sum bound.

## Scope

1. `Rectifier` (L2), complete. It covers the half/full-wave blend, DC blocking, anti-aliasing, the lifecycle and
   non-finite safety.
2. `ShapeMorph` (L3), complete. It covers:
   - the ROUND / HOLLOW / BITE regimes and the continuous SHAPE control;
   - the loudness-compensated regime crossfade;
   - a click-free engage / bypass;
   - keytracked internal frequencies;
   - anti-aliasing of its nonlinear paths.
3. `BassCharacterStage` (L3), complete. It covers:
   - six algorithms with Amount + Tone, and click-free algorithm switching;
   - per-channel offsets;
   - an r-law observer;
   - anti-aliasing and DC safety for every algorithm.
4. The definition of **Fracture**: roadmap Open Question 5, this spec's OQ-5. Ratified as the period-doubling
   fracture of FR-050.
5. The **descriptor-separation bar** for the pairwise-distinctness audibility gates (OQ-S). The roadmap says the bar
   is "ruled in spec".
6. The r-law deliverables for Rectify, Crush and Fracture. Every other algorithm and every Shape regime records
   r ≡ 1 evidence.
7. Tests, in these groups:
   - audibility gates, Tone monotonicity and SHAPE continuity;
   - switch click bounds, aliasing, DC and the mono-sum check;
   - RT safety, non-finite robustness and CPU.

## Non-Goals (owned by later phases, or deliberately excluded)

| Excluded | Owner | Roadmap |
|---|---|---|
| Wiring Shape and Character into `ProfundumVoice`. This includes the chain order (Shape → Fold → Character) as a voice property and the Anchor bypassing both stages | Phase 9 | 543–548, 577–579 |
| Per-voice vs post-sum placement of Shape / Character in POLY | Phase 9 (OQ-7) | 546, 893 |
| The full-chain gate "Anchor sub energy within 1 dB through max Fold + every Character algorithm" | Phase 9 | 571–573 |
| FOLLOW tracking through a real pitch-moving stage, and the capability-table FOLLOW row | Phase 9 | 575–577 |
| WIDE's per-channel offset values, the mono-sum tolerance of the whole field and the Gravity safety stage | Phase 7 | 466–496 |
| Morph laws for SHAPE, algorithm, Amount and Tone, and the `MorphState` fields | Phase 8 | 505–520 |
| Macro mappings (Dark ↔ Bright, Clean ↔ Violent) onto SHAPE / Character | Phase 9 | 555–559 |
| Parameter IDs (500–599 fold / shape, 600–699 character), names, normalisation and state. This includes the BITE / Bite and Fold / Harmonic Fold label collisions | Phases 10, 11 (OQ-10) | 603–605, 899–902 |
| UI | Phase 12 | — |
| A user-facing oversampling / quality switch | Never ("Not adopted") | 706–709 |
| A seventh algorithm, a second knob per algorithm, an FX-rack ordering, a dry/wet control separate from Amount, or a `setAlgorithmBlend` held two-algorithm blend (FR-057) | Never (KDD 5, concept §13 "only Amount + Tone") | 145–147 |
| Changes to `ProfundumCore`, `AnchorOscillator`, `HarmonicFold` or any reused processor header | None planned (FR-064) | 876–877 |

---

## Existing components (verified this session)

Paths are relative to `dsp/include/krate/dsp/` unless stated. Every row was opened and read on 2026-10-15.

| Component | Header | Reused for | Verified signature / fact |
|---|---|---|---|
| `SVF` | `primitives/svf.h:110` | HOLLOW (notch / band-pass + resonance). Candidate for ROUND (low-pass) and BITE (high shelf) | Layer 1 (line 2). `enum class SVFMode : uint8_t { Lowpass, Highpass, Bandpass, Notch, Allpass, Peak, LowShelf, HighShelf }` (38–47). `void prepare(double sampleRate) noexcept` (161), `void setMode(SVFMode)` (188), `void setCutoff(float hz)` (204), `void setResonance(float q)` (221), `void setGain(float dB)` (261), `[[nodiscard]] float process(float input) noexcept` (353), `void processBlock(float*, size_t)` (402), `[[nodiscard]] SVFOutputs processMulti(float input) noexcept` (421). `kMinQ = 0.1f`, `kMaxQ = 30.0f` (120–123), `kMaxCutoffRatio = 0.495f` (129). Includes L0 only (18–19). |
| `LadderFilter` | `primitives/ladder_filter.h:91` | Candidate for ROUND (low-pass with gentle drive) | Layer 1 (line 2). `enum class LadderModel : uint8_t { Linear, Nonlinear }` (44–47). `void prepare(double sampleRate, int maxBlockSize) noexcept` is documented as "NOT real-time safe (allocates memory)" (140–148). `setModel` (196), `setOversamplingFactor(int)` with 1/2/4 (223–228), `setResonanceCompensation(bool)` (238), `setSlope(int poles)` (247), `setCutoff` (260), `setResonance` (272), `setDrive(float db)` with `kMaxDriveDb = 24.0f` (283, 115). `[[nodiscard]] int getLatency() const noexcept` returns 0 for Linear or 1×, otherwise the oversampler's latency (322–331). `prepare` sets its oversamplers to `OversamplingQuality::High, OversamplingMode::ZeroLatency` (160–163). `oversampler.h` documents that "ZeroLatency mode always uses IIR" with `latencySamples_ = 0`. `processBlock` passes a `[this]` lambda to `Oversampler::process`, whose callback parameter is a `std::function` (`ladder_filter.h:413–417`, `oversampler.h:236–239`). Allocation-freedom is therefore asserted by test (FR-060), not assumed. |
| `MultimodeFilter` | `processors/multimode_filter.h:93` | Candidate for ROUND / BITE. Listed by the roadmap row | Layer 2. Filter types come from `primitives/biquad.h:68–77`: `FilterType { Lowpass, Highpass, Bandpass, Notch, Allpass, LowShelf, HighShelf, Peak }`. `prepare(double, size_t)` (132), `setDrive(float dB)` over 0–24 (276). The drive is `std::tanh` at 2× through `Oversampler` (`applyDrive`, 393–407). `getLatency()` returns the oversampler's latency when drive > 0 (309–314). |
| `SpectralTilt` | `processors/spectral_tilt.h:88` | BITE's high-frequency emphasis. Candidate Tone implementation | Layer 2. `kMinTilt = -12.0f`, `kMaxTilt = +12.0f` dB/oct (98–101). `void prepare(double sampleRate)` (175), `void setTilt(float dBPerOctave)` (215) and `setPivotFrequency(float hz)` (225) are **not** `noexcept`. `process(float) noexcept` (253), `processBlock(float*, int) noexcept` (279). The plan must confirm the setters neither allocate nor throw before calling them per block (FR-060). |
| `DCBlocker`, `DCBlocker2` | `primitives/dc_blocker.h:94, 272` | DC safety for Rectify, BITE and the asymmetric algorithms | Layer 1. `void prepare(double sampleRate, float cutoffHz = 10.0f) noexcept` (135, 298), `[[nodiscard]] float process(float x) noexcept` (190, 328), `processBlock` (218, 346). |
| `DiodeClipper` | `processors/diode_clipper.h:113` | BITE's asymmetric excitation. Candidate for Character Bite | Layer 2. `enum class DiodeType : uint8_t { Silicon, Germanium, LED, Schottky }` (52–57). `enum class ClipperTopology : uint8_t { Symmetric, Asymmetric, SoftHard }` (65–69). `prepare(double, size_t)` (182), `setDrive(float dB)` over −24…+48 (242; 120–123), `setMix` (252), `process(float*, size_t) noexcept` (325). The header says "No internal oversampling (users wrap externally with Oversampler if needed)" (line 90). |
| `TubeStage` | `processors/tube_stage.h:79` | Character **Tube** | Layer 2. `prepare(double, size_t) noexcept` (117), `setInputGain(float dB)` over ±24 (171), `setOutputGain` (182), `setBias(float)` (195), `setSaturationAmount(float)` (208), `process(float*, size_t) noexcept` (255). The waveshaper type is `WaveshapeType::Tube` (123). **When the smoothed saturation amount is < 0.0001, a sample skips both the waveshaper and the DC blocker** (lines 268–273). Crossing that threshold therefore switches the DC blocker in and out (see Edge Cases). No internal anti-aliasing. |
| `WavefolderProcessor` | `processors/wavefolder_processor.h:115` | Character **Fold** | Layer 2. `enum class WavefolderModel : uint8_t { Simple, Serge, Buchla259, Lockhart }` (56–61). `prepare(double, size_t) noexcept` (157), `setModel` (207), `setFoldAmount(float)` clamped to [0.1, 10] (260; 122–125), `setSymmetry(float)` over [−1, 1] (273), `setMix` (285), `process(float*, size_t) noexcept` (325). The header says "No internal oversampling (handled externally per user preference)" (line 92). |
| `BitcrusherProcessor` | `processors/bitcrusher_processor.h:68` | **Not composed** by Crush (FR-051): Crush uses the `BitCrusher` primitive plus an inline pitch-synchronous hold; `SampleRateReducer` is not used | Layer 2. `prepare(double, size_t maxBlockSize) noexcept` resizes `dryBuffer_` to `maxBlockSize` (108–112), so `process` must never receive more samples than that (see Edge Cases). `process(float*, size_t) noexcept` (169). `setBitDepth` over [4, 16] (251; 74–75), `setReductionFactor` over [1, 8] (260; 76–77), `setDitherAmount` (268), `setMix` (294). `getLatency()` returns 0 (363). It composes two primitives:<br>• `BitCrusher` (`primitives/bit_crusher.h:44`): a symmetric quantiser with TPDF dither. The dither comes from an xorshift32 seeded by `setSeed(uint32_t)` (79–81), and `reset()` restores the seed (72–74).<br>• `SampleRateReducer` (`primitives/sample_rate_reducer.h:38`): `Mode { ZOH, Linear }` (48), `kMaxReductionFactor = 32.0f` (45), a fractional hold counter with optional xorshift jitter (73–96). |
| `FeedbackDistortion` | `processors/feedback_distortion.h:82` | Fallback only for Fracture: used only if the period-doubling definition (FR-050) fails SC-005 | Layer 2. It composes a `DelayLine` that allocates in `prepare`: `delayLine_.prepare(sampleRate, 0.1f)`, and `DelayLine` resizes its `std::vector<float> buffer_` in its own `prepare` (`primitives/delay_line.h:264, 284`). Ranges: delay `kMinDelayMs = 1` to `kMaxDelayMs = 100` (90–91); feedback 0–1.5 (97–98); drive 0.1–10 (104–105); limiter threshold −24…0 dB (111–112); tone from 20 Hz up (118–119). `[[nodiscard]] float process(float x) noexcept` (231), `process(float*, size_t)` (345), `setDelayTime(float ms)` (361), `setFeedback` (383), `setDrive` (414), `setToneFrequency` (456), `setFilterType(int)` with 0 = Off … 3 = Highpass (473–478). |
| `FuzzProcessor` | `processors/fuzz_processor.h:100` | **Not reused.** Recorded because the roadmap says the only rectification lives here | Its "octave-up" is `sample = sample * std::abs(sample);  // Full-wave rectification` (line 491). That computes x·\|x\|, which is an **odd**, sign-preserving function. It is not a rectifier and does not double the frequency. KrateDSP therefore has no rectifier at all, and `Rectifier` is new. |
| `PolyphaseHalfbandOversampler` | `primitives/polyphase_halfband_oversampler.h:32` | The oversampler for every nonlinear path in this phase (D-3) | Layer 1, from Profundum Phase 2. The header says: "Zero look-ahead (IIR, no FIR, no latency compensation); low-frequency group delay about 1.9 base-rate samples per direction … Heap-free, copyable" (24–27). `kMaxFactor = 4` (34), `void setFactor(std::size_t)` (49), `factor()` (54), `reset()` (57), `void upsample(float x, float* out) noexcept` (70), `[[nodiscard]] float downsample(const float* in) noexcept` (100), `stateFinite()` (105). One instance handles one channel. |
| `Oversampler<Factor, NumChannels>` | `primitives/oversampler.h:226` | Alternative oversampler. Used inside `LadderFilter` and `MultimodeFilter` | `enum class OversamplingQuality { Economy, Standard, High }` and `OversamplingMode { ZeroLatency, LinearPhase }` (52–62). `prepare(double, size_t, quality, mode)` is documented as "NOT real-time safe (allocates memory)" (283–293). `process(float*, float*, size_t, const StereoCallback&)`, where `using StereoCallback = std::function<void(float*, float*, size_t)>` (236, 322–327). |
| `TanhADAA`, `HardClipADAA` | `primitives/tanh_adaa.h:66`, `primitives/hard_clip_adaa.h:69` | ADAA alternatives for Bite's clipper and Tube's shaper (the plan chooses) | `TanhADAA::setDrive(float)` (101), `process(float) noexcept` (131). `HardClipADAA::setOrder(Order)` (122), `setThreshold(float)` (130), `process(float) noexcept` (163). |
| `Wavefolder` | `primitives/wavefolder.h:101` | Fold math under `WavefolderProcessor` | `setType(WavefoldType)` (131), `setFoldAmount(float)` (146), `[[nodiscard]] float process(float x) const noexcept` (182). |
| `Sigmoid` / `Asymmetric` | `core/sigmoid.h:45, 313` | Clipper curves for BITE and Bite | `Asymmetric::tube(float)` (333), `Asymmetric::diode(float)` (358), `Asymmetric::withBias(float, float, Func)` (392), `Asymmetric::dualCurve(float, float, float)` (414), `Sigmoid::hardClip` (267). |
| `equalPowerGains`, `crossfadeIncrement` | `core/crossfade_utils.h:50, 64, 89` | Regime and algorithm crossfades | `inline void equalPowerGains(float position, float& fadeOut, float& fadeIn) noexcept` (50). `[[nodiscard]] inline float crossfadeIncrement(float durationMs, double sampleRate) noexcept` (89). |
| `HarmonicFold` | `processors/harmonic_fold.h:76` | **Interface precedent**, and a test fixture: Character follows the Fold (roadmap line 543), so the Fold's output is Character's realistic worst-case input | Layer 2. `prepare(double sampleRate, std::size_t maxBlockSize) noexcept` (121). `setAmount(float)` over 0–100 (168). `setFundamentalHz(float hz) noexcept`, clamped to [8, 20000] Hz (178–181; 102–103). `void processBlock(float* left, float* right, std::size_t numSamples) noexcept`, where "right == left processes one channel with channel 0's state" (186–188). `stateFinite()` (247). `static constexpr std::size_t latencySamples() noexcept { return 0; }` (271). `kCalibrationPeak = 0.3552343859f` is "the core's −12 dBFS RMS sine peak" (83). `kOutputCeiling = 6.0f` (93). The factor table is "4× up to 100 kHz, 2× above" (`prepare`, 124). |
| `ProfundumCore` | `systems/profundum_core.h:92` | Test-fixture signal source | `prepare(double sampleRate, int numPartials = kDefaultPartials) noexcept` (129), `noteOn(float f0Hz) noexcept` (208), `processBlock(float* left, float* right, std::size_t numSamples, …)` (237). `kMinF0Hz = 8.0f` (99). Its −12 dBFS RMS calibration peak equals `HarmonicFold::kCalibrationPeak` (867–868). |
| `SpectralShapeRecipe` named states | `processors/spectral_shape_recipe.h:82–85` | Fixtures: `kSineAnchor`, `kTriangleAnchor`, `kSawAnchor` and `kGrowl`. `kTriangleAnchor` is odd-only, so half-wave symmetric, which makes it the worst case for the Rectify r law | `static const Controls kDefaultControls; kSineAnchor, kTriangleAnchor, kSawAnchor; kHeavy, kHollow, kGrowl; kBodyRound … kBodyThick` (82–85). |
| `AnchorOscillator` | `systems/anchor_oscillator.h:55` | Consumer of the r law (Phase 9 wires it) | `void setFollowRatio(float r) noexcept` (255). `processBlock(float* left, float* right, std::size_t numSamples, const float* f0PerSample = nullptr, const float* followRatioPerSample = nullptr)` (285–286). |
| `CharacterProcessor`, `CharacterMode` | `systems/character_processor.h:85, 53` | **Not reused. ODR hazard** | Layer 3 (line 2): a tape / BBD / digital delay character system. Both names are taken at `Krate::DSP` namespace scope. |
| `MorphEngine` (Disrumpo) | `plugins/disrumpo/src/dsp/morph_engine.h:45` | **Not reused. ODR hazard.** The family-crossfade idea belongs to Phase 8 | Plugin-local. |

**Test helpers (verified).**
- `dsp/tests/unit/systems/profundum_core_test_helpers.h`:
  - `renderedHarmonicPowers` (235), `describeRendered` (251), `goertzelPhase` (157), `rmsDbfs` (453);
  - `aliasFftLength` (257);
  - `AliasResult` / `aliasedPower` (265–306). The method is BH7-windowed and Parseval-normalised, and it excludes
    ±8 bins around every n·f0 < fs/2 as well as bins 0–8.
- `dsp/tests/unit/processors/harmonic_fold_test_helpers.h`: `calibrationSine` (252), `recipeAdditive` (268),
  `FoldDescriptors` / `describeFold` (300–314), `integerPeriodLength` (240), `goertzelPhasor` (224; the BH7
  single-bin complex DFT used for the h1 transfer). This header lives under the processors test directory. The new
  systems-layer TUs include it directly, as `profundum_edge_fold_chain_test.cpp` already does for the Phase 2 chain;
  the plan may instead hoist `goertzelPhasor` and `integerPeriodLength` into a shared helper, and records which.
- `tests/test_helpers/low_frequency_metrics.h`: `kLowFrequencyFftSize` = 262144 (64), the Hann frame behind
  `renderedHarmonicPowers`.
- `tests/test_helpers/artifact_detection.h`: `ClickDetectorConfig` (38), `ClickDetector` (99).
- `tests/test_helpers/render_fingerprint.h`: `fingerprintRender` (73), `compareFingerprints` (122).
- `tests/test_helpers/allocation_detector.h`: `AllocationScope` (111).

**Test registration (verified).** `dsp/tests/CMakeLists.txt` lists TUs explicitly.
- `dsp_processors_tests` starts at line 161; `harmonic_fold_test.cpp` is at line 261.
- `dsp_systems_tests` starts at line 348; `profundum_edge_fold_chain_test.cpp` is at line 646.

---

## New components

| Class / type | Layer | Header | ODR sweep (`grep -rnE "(class\|struct\|enum class\|enum\|using) <Name>\b" dsp/ plugins/ tests/ tools/`, 2026-10-15) |
|---|---|---|---|
| `Rectifier` | 2 | `processors/rectifier.h` | **0 hits.** No file named `*rectifier*` exists under `dsp/` or `plugins/`. `RectifierMode` also has 0 hits. The broader sweep `(class\|struct\|enum class) \w*(Rectif\|ShapeMorph\|Fracture)\w*` returns 0 hits. |
| `ShapeMorph` | 3 | `systems/shape_morph.h` | **0 hits.** No `*shape_morph*` file exists. `ShapeRegime`, a candidate nested name, has 0 hits. |
| `BassCharacterStage`, with a nested `enum class Algorithm : std::uint8_t { Tube, Fold, Rectify, Bite, Crush, Fracture }` | 3 | `systems/bass_character_stage.h` | **0 hits.** No `*bass_character_stage*` file exists. `BassCharacterAlgorithm`, `CharacterAlgorithm` and `Fracture` have 0 hits each. **`CharacterMode` is taken** (`systems/character_processor.h:53`), and so is `CharacterProcessor` (`:85`). The algorithm enum is therefore nested inside `BassCharacterStage`, and no namespace-scope `Character*` name is introduced. |
| `ClosedFormAdaa2<Law>`, `DiodeAdaaLaw`, `diodeH1Coefficient` | 0 | `core/closed_form_adaa2.h` | **0 hits** (plan C-1, accepted 2026-10-15): second-order ADAA with closed-form antiderivatives in double, shared by `Rectifier`, BITE and Bite. |
| `KeytrackedTilt` | 2 | `processors/keytracked_tilt.h` | **0 hits** (plan C-1): the first-order keytracked tilt behind every Tone and BITE's emphasis. |
| `OversampledResidualPath` | 2 | `processors/oversampled_residual_path.h` | **0 hits** (plan C-1): the 4× round trip with the notched, DC-blocked nonlinear residual, shared by both L3 stages (`Rectifier`, being L2, composes the same L1 parts itself and a test pins the constants equal). |

**Layer discipline.**
- `Rectifier` includes only L0 and L1: `DCBlocker`, `PolyphaseHalfbandOversampler` and the ADAA primitives.
- `ShapeMorph` and `BassCharacterStage` include only L0–L2.
- `BassCharacterStage` includes `processors/rectifier.h`, which points downward (L3 → L2).
- Neither L3 component includes the other or any other L3 header.

---

## Functional Requirements

### Descriptor definitions (used by FRs and SCs)

All descriptors are computed offline in double precision on steady-state renders. Each descriptor names the helper
that implements it, and the analysis frame and window are that helper's (no single frame/window is shared by all):
- **w_n** is the rendered harmonic power of harmonic n from `renderedHarmonicPowers`
  (`profundum_core_test_helpers.h:235–247`): one fixed `kLowFrequencyFftSize` = 262144-sample frame
  (`tests/test_helpers/low_frequency_metrics.h:64`), Hann window, power summed over the main lobe. w_n = 0 for
  n·f₀ ≥ fs/2. E_low, E_high, R_oe and THD derive from w_n.
- **Level L** is the RMS level in dBFS (`rmsDbfs`, `profundum_core_test_helpers.h:453`) over a frame of whole periods
  (`integerPeriodLength`, `harmonic_fold_test_helpers.h:240`), unwindowed. It is unweighted, the convention Phase 1
  ruled for its loudness normalisation (roadmap line 200, "unweighted power normalisation").
- **Transition level L_tr[k]** (used only where a window is shorter than a whole-period steady frame, i.e. SC-009) is
  the sliding RMS over exactly one period of f₀ (rounded to the nearest sample) centred on sample k, in dBFS.
- **Centroid C** is the power-weighted mean of log₂(f / f₀) over **all** bins from 20 Hz to
  **f_C,max = min(fs/2, 20 kHz)**, in octaves above f₀, on the BH7 `aliasFftLength` frame. The upper bound is fixed
  independently of fs, so C at 48 and 96 kHz measures the same band (SC-006, SC-007, SC-019). Inharmonic content
  counts, so Crush and Fracture are measured honestly.
- **E_low** is Σ_{n=2..8} w_n / Σ w in dB. **E_high** is Σ_{n≥9} w_n / Σ w in dB. Together they are Phase 1's Body /
  Presence split.
- **Odd/even ratio R_oe** is Σ_{odd n≥3} w_n / Σ_{even n≥2} w_n in dB.
- **THD** is Σ_{n≥2} w_n / w_1 in dB.
- **Inharmonic fraction I** is the power outside ±8 bins of every n·f₀ < fs/2, as a fraction of total power, in dB.
  Bins 0–8 are excluded. It is the `aliasedPower` mask (BH7, `aliasFftLength` frame) applied to the total.
- **Aliased power A** is `aliasedPower(…).aliasedDbfs` (`profundum_core_test_helpers.h:265–306`), the Phase 2 method
  (BH7, Parseval-normalised).
- **Descriptor floor (OQ-S).** Every dB descriptor that is a power ratio (E_low, E_high, R_oe, THD, I) is clamped to
  **≥ −60 dB** before any difference is taken (for R_oe, numerator and denominator are each clamped to ≥ −60 dB re
  total first). A descriptor counts toward a separation (SC-001, SC-005), an Amount movement (SC-007) or an offset
  effect (SC-013(c)) **only if at least one of the two compared values, before clamping, is above −40 dB re total**
  (for THD: re w_1). A descriptor below the floor on both sides contributes nothing.
- **h1 transfer** is the complex ratio of the output's h1 phasor to the input's, reported in dB and degrees. It uses
  `goertzelPhasor` (`dsp/tests/unit/processors/harmonic_fold_test_helpers.h:224`), a BH7 single-bin DFT over an
  `integerPeriodLength` frame. When the output's w_1 is more than 60 dB below the input's, the cell is recorded as
  **"h1 absent"**, not as a transfer value.
- **Fixture input** is the core rendering `kSawAnchor`, `kGrowl`, `kTriangleAnchor` or `kSineAnchor` at the
  −12 dBFS RMS calibration level (`HarmonicFold::kCalibrationPeak`). The **Fold-100 input** is the same signal passed
  through `HarmonicFold` at amount 100. It is Character's realistic worst case, because Character follows the Fold.
- **Notes:** E0 = 20.60 Hz, E1 = 41.20 Hz, E2 = 82.41 Hz, C1 = 32.70 Hz, C2 = 65.41 Hz, C3 = 130.81 Hz.

### FR-001 series — `Rectifier` (roadmap lines 423–424: "half/full-wave blend, DC-blocked, anti-aliased")

- **FR-001 Interface.** `Rectifier` provides:
  - `prepare(double sampleRate, std::size_t maxBlockSize) noexcept` and `reset() noexcept`;
  - `setBlend(float) noexcept`, continuous, where 0 is half-wave and 1 is full-wave;
  - `processBlock(float* left, float* right, std::size_t numSamples) noexcept`, which follows `HarmonicFold`'s
    convention: in place, and `right == left` processes one channel;
  - `stateFinite()` and `static constexpr latencySamples()`, which returns 0;
  - `[[nodiscard]] static float transfer(float x, float b) noexcept`, the memoryless law of FR-002. The processing
    path uses this function (or ADAA antiderivatives derived from it), so the law is testable directly, without the
    DC blocker, the oversampler's IIR filters or the ADAA difference quotient in the way (SC-014).

  The plan fixes the exact names.
- **FR-002 Transfer law.** The memoryless law is y = (1 − b)·max(x, 0) + b·|x| for blend b, implemented by
  `transfer(x, b)`. It applies before the DC blocker and any output gain. The plan may add a documented gain stage but may not change the law's shape.
- **FR-003 DC blocking.** Output DC is ≤ −80 dBFS at every blend (SC-011). A `DCBlocker` or `DCBlocker2` does this,
  with its corner below E0's fundamental by a documented margin. The resulting attenuation of h1 at E0 is recorded.
- **FR-004 Anti-aliasing.** The kink at x = 0 is handled by oversampling (`PolyphaseHalfbandOversampler`) or by ADAA,
  since |x| and max(x, 0) have closed-form antiderivatives. SC-010 is the aliasing gate.
- **FR-005 Smoothing.** Blend changes are smoothed, and a full-range blend step produces no click (SC-021).

### FR-010 series — `ShapeMorph` (roadmap lines 417–421; concept §6)

- **FR-010 Interface.** `ShapeMorph` provides:
  - `prepare(double sampleRate, std::size_t maxBlockSize) noexcept` and `reset() noexcept`;
  - `setShape(float position) noexcept` with position ∈ [0, 1];
  - `setEnabled(bool) noexcept` (D-2);
  - `setFundamentalHz(float hz) noexcept` (D-4), the per-block fallback of FR-052;
  - `processBlock(float* left, float* right, std::size_t n, const float* f0PerSample = nullptr) noexcept` (FR-052);
  - `stateFinite()` and `static constexpr latencySamples()`, which returns 0.

  The plan fixes the exact names.
- **FR-011 Regime placement.** ROUND sits at SHAPE 0, HOLLOW at 0.5 and BITE at 1 (D-1). Between two anchors, the
  neighbouring regimes are crossfaded, so at most two regimes run at any position.
- **FR-012 ROUND.** Low-pass-like behaviour with gentle drive, built on `LadderFilter` or the `SVF` low-pass (roadmap
  line 418). The drive is a nonlinear path and is anti-aliased (FR-061).
- **FR-013 HOLLOW.** Band / reject / resonant behaviour, built on the `SVF` notch and/or band-pass with resonance
  (roadmap line 419). Resonance is bounded so the regime stays stable and finite at every SHAPE position and every f₀.
- **FR-014 BITE.** High-frequency emphasis, a keytracked first-order tilt (`KeytrackedTilt`, `OnePoleLP` split) in place of
  `SpectralTilt` or an `SVF` high shelf (C-13, Clarifications "Plan S1 rulings", accepted refinement: `SpectralTilt`
  redesigns two biquads per sample while its smoothers run, and `SVF::HighShelf` is not the Cytomic shelf; plan C-13),
  plus nonlinear excitation with `DiodeClipper`-style asymmetry (roadmap line 420). The clipper is anti-aliased and
  DC-blocked.
- **FR-015 Equal-loudness crossfade.** The crossfade between neighbouring regimes is loudness-compensated, so the level
  L stays within the SC-002 band across the sweep. The roadmap asks for an "Equal-loudness crossfade between regimes"
  (line 420). The compensation is a static per-SHAPE table calibrated on the mean of `kSawAnchor`, `kTriangleAnchor`
  and `kSineAnchor`; those three are gated within the SC-002 band and `kGrowl`'s swing is recorded (C-6, user ruling
  2026-10-15, Clarifications "Plan S1 rulings"; FR-056). It
  does not follow the signal level. A dynamic gain computer would act as a compressor, which is Phase 6's job.
- **FR-016 Continuity.** A SHAPE sweep has no level jump and no descriptor jump between regimes (roadmap line 432;
  SC-002). SHAPE automation at any speed is click-free (SC-003). The SHAPE control is smoothed by a **restartable
  10 ms raised-cosine ramp** from the current value to the target (user ruling 2026-10-16, Clarifications "Group 10 close rulings", C-33; it replaces the plan's 10 ms
  one-pole, whose steepest slope sat at the first sample of every move and counted as a click on `kSawAnchor` E0).
- **FR-017 Regime distinctness (audibility).** ROUND, HOLLOW and BITE are pairwise distinct by the separation bar
  ratified 2026-10-15 (OQ-S): two level-matched renders are distinct when at least one of E_low, E_high, R_oe, THD or I differs by ≥ **3 dB**, or C differs by ≥ **0.25 oct**, with the descriptor floor applied (clamp ≥ −60 dB; counts only if one side is above −40 dB re total) (SC-001).
- **FR-018 Keytracking.** Each regime sets its internal frequencies as multiples of f₀ (D-4). The regime then acts on
  the same harmonic numbers across the keyboard, as the Core Philosophy requires (roadmap line 41). Measured by SC-004.
- **FR-019 Engage.** `setEnabled(false)` bypasses the stage. Engaging and disengaging are click-free crossfades. Once
  disabled and settled, the output is bit-identical to the input (SC-008). The processed path runs warm on every sample
  after `prepare`, enabled or not, and the bypass copy is selected at the output, so at engage the regimes are at steady
  state and SC-003(c)'s D-11 premise holds literally (C-22; a disabled `ShapeMorph` costs what an enabled one costs and
  SC-015 records that cell).

### FR-020 series — `BassCharacterStage` (roadmap lines 422–429; concept §13)

- **FR-020 Interface.** `BassCharacterStage` provides:
  - `prepare(double sampleRate, std::size_t maxBlockSize) noexcept` and `reset() noexcept`;
  - `setAlgorithm(Algorithm) noexcept`;
  - `setAmount(float) noexcept` and `setTone(float) noexcept`, each in [0, 1];
  - `setChannelOffsets(float amountOffset, float toneOffset) noexcept` (FR-027);
  - `setFundamentalHz(float) noexcept`, the per-block fallback of FR-052;
  - `processBlock(float* left, float* right, std::size_t n, const float* f0PerSample = nullptr) noexcept` (FR-052);
  - `[[nodiscard]] float perceivedPitchRatio() const noexcept` (FR-030);
  - `setInputHalfWaveSymmetric(bool) noexcept` (FR-030, D-8): the voice's statement that the stage's input contains
    odd harmonics only. **Default `false`** (the input is assumed to carry even harmonics and keep period T);
  - `stateFinite()` and `static constexpr latencySamples()`, which returns 0.

  The plan fixes the exact names.
- **FR-021 Six algorithms, two controls each.** The algorithms are exactly Tube, Fold, Rectify, Bite, Crush and
  Fracture. Amount and Tone are the only controls (roadmap line 423), and every internal setting is a documented
  function of (Amount, Tone, f₀). D-5 drafts the mapping, and the plan finalises it.
  - **Tube** composes `TubeStage` at a **branch-local extra 2×** (8× total at fs ≤ 100 kHz) with input gain −6 … +12 dB
    (C-10 (user ruling 2026-10-15, Clarifications "Plan S1 rulings": `TubeStage` has no ADAA path; at the shared 4×, +12 dB aliases at −66.6 dBFS and +6 dB fails SC-007).
  - **Fold** composes `WavefolderProcessor`.
  - **Rectify** composes `Rectifier`'s laws (`RampLaw`, `AbsLaw`, `sineMakeup`, `linearCoefficient`) inside the stage's
    oversampled domain; `Rectifier` itself is the standalone component of FR-001–FR-005 (C-19 (user ruling 2026-10-15, Clarifications "Plan S1 rulings": running a
    `Rectifier` inside the stage would nest a second 4× round trip and break FR-053's phase-matched dry share). The
    shipped branch is gated against `Rectifier::transfer` by `BassCharacter_RectifyLawMatchesRectifier`.
  - **Bite** is a tilt plus an asymmetric clip: a `KeytrackedTilt` (C-13, Clarifications "Plan S1 rulings", in place of
    `SpectralTilt` or an `SVF` shelf), with the `Asymmetric::*` diode curve (C-13: `DiodeClipper` carries a 2nd-order
    DC blocker in its wet path and no ADAA).
  - **Crush** is a pitch-synchronous, noiseless sample-hold at m·f₀ composed from `BitCrusher` plus an inline hold
    (FR-051).
  - **Fracture** is the period-doubling fracture of FR-050.
  - Tone is defined per algorithm by FR-055.
- **FR-022 Amount 0 is transparent.** At Amount 0 on both channels, once settled, the stage output is bit-identical to
  its input for every algorithm (SC-008). This gives Phase 9's capability-table on/off cell a clear meaning (roadmap
  line 562). Amount 0 is a separate hard-bypass branch reached through a short crossfade (FR-054).
- **FR-023 Amount moves the sound.** For every algorithm, taking Amount from 0 to 1 moves at least one descriptor by
  at least the separation bar's unit (SC-007). Nothing ships inert (roadmap lines 54, 861).
- **FR-024 Tone.** For every algorithm, spectral centroid C is monotonic in Tone (roadmap line 431), and the span is
  large enough to be audible (SC-006). Tone's structure per algorithm is FR-055 (SC-028).
- **FR-025 Pairwise distinctness.** The six algorithms' signatures are pairwise distinct by the separation bar
  ratified 2026-10-15 (roadmap line 430; OQ-S): two level-matched renders are distinct when at least one of E_low, E_high, R_oe, THD or I differs by ≥ **3 dB**, or C differs by ≥ **0.25 oct**, with the descriptor floor applied (clamp ≥ −60 dB; counts only if one side is above −40 dB re total) (SC-005).
- **FR-026 Click-free algorithm switching.** `setAlgorithm` starts a crossfade from the outgoing algorithm to the
  incoming one (roadmap line 426), and the switch's sample delta is bounded (SC-009).
  - The outgoing algorithm keeps running for the length of the crossfade, so at most two algorithms run at once.
  - **Pre-roll (user ruling 2026-10-16, Clarifications "Group 9 rulings, round 8").** `start(a)` resets and re-keys the incoming branch, then runs it **unmixed for one
    period of the current f₀** (round(fs/f₀) samples, at least 1) while the outgoing algorithm alone is heard; at the
    end of the pre-roll the incoming slot's residual DC blocker is primed to the mean of its pre-blocker residual over
    that period (`OversampledResidualPath::primeDc`), and the 10 ms crossfade begins. A switch therefore takes
    1/f₀ + 10 ms from the request (Phase 9 handoff record). Measured before the ruling: a cold-started Rectify branch
    at Amount 1 carried a residual DC of 0.43 that the 2 Hz blocker drained over ≈ 80 ms, 5 dB above its steady
    one-period level, so every switch into Rectify failed SC-009's level window by ≈ 4.5 dB, and Crush's empty first
    hold period missed the window's floor by 0.07 dB at C2.
  - A request that arrives while a transition is in flight (pre-roll or crossfade) is handled deterministically
    (D-7, FR-057, SC-029).
  - `setAlgorithm` is the only algorithm-transition API (FR-057).
- **FR-027 L/R capability.** The stage processes L and R with independent state (roadmap lines 426–427, 486).
  - `setChannelOffsets` adds the offset to L and subtracts it from R, on both Amount and Tone, clamped to [0, 1].
  - The maximum offset this phase gates is **±0.15** on Amount and on Tone (D-12, ratified 2026-10-15); SC-013(b) and
    SC-012(g) measure at that value. Phase 7 rules the shipped values and re-runs SC-013(b).
  - With zero offsets and L == R input, the output is L == R bit-identical (SC-013(a)).
  - Phase 7 chooses the offset values.
- **FR-028 Anchor bypass.** The stage has no Anchor input and no Anchor-related API, so the Anchor bypasses it by
  construction (roadmap line 427). The voice wiring is Phase 9's.

### FR-030 series — r-law deliverables (roadmap lines 427–429; Phase 3 FR-047, FR-066)

- **FR-030 Observer.** `perceivedPitchRatio()` returns r = f_perceived / f₀ for the stage's current algorithm and the
  centre Amount and Tone. Channel offsets are ignored (FR-036). Where the law depends on the input, it also depends on the input half-wave-symmetry descriptor set
  by `setInputHalfWaveSymmetric(bool)` (FR-020, D-8).
  - **Default.** With no descriptor supplied the value is `false`: the observer reports the law for an input that
    carries even harmonics (period T). For Rectify this is r ≡ 1 at every Amount and Tone, which is also the
    measured truth for every input with audible even harmonics, including the real upstream chain of SC-012(e).
  - With `true`, the observer reports the half-wave-symmetric law of FR-031, including its flip point.
  - During an algorithm crossfade it returns the incoming algorithm's r (D-8).
  - It returns one float. Hysteresis and channel offsets: FR-036.
- **FR-031 Rectify r law.** Rectify publishes r as a function of **the stage's controls**, Amount and Tone (FR-021),
  and of the input half-wave-symmetry descriptor. Blend b and the wet mix are not controls: they are derived values of
  D-5's Amount mapping, and the deliverable records them alongside each Amount value.
  - Physical expectation, which measurement must confirm (it is not assumed):
    - full-wave rectification of a half-wave-symmetric input (odd harmonics only, such as `kSineAnchor` or
      `kTriangleAnchor`) has period T/2, so the wet path tends to r = 2 as b → 1. The dry share of the mix keeps h1,
      so the perceived fundamental of the stage output flips from f₀ to 2·f₀ only at some Amount where the wet 2·f₀
      salience overtakes the dry h1;
    - an input with even harmonics keeps period T, so r ≡ 1.
    - **Descriptor rule (user ruling 2026-10-16, Clarifications "Group 10 close rulings").** The voice supplies `setInputHalfWaveSymmetric(true)` when the Rectify
      input's second harmonic is below **−20 dB re h1**; inputs within **−24 … −16 dB** are the descriptor's documented
      uncertainty band. Measured (triangle + h2 at set levels, Rectify Amount 1, E0 / E1 / C3): the reference reads
      2·f₀ at h2 ≤ −22 dB (Tone 0.5, 1) and ≤ −26 dB (Tone 0), f₀ at h2 ≥ −14 dB, and is ambiguous between. The
      plan's premise "even harmonics present when w₂ ≥ −40 dB" was 20 dB off: the BITE → Fold-100 triangle chain
      leaves h2 at −19.9 dB and still doubles at Amount ≥ 0.8.
  - The deliverable has four parts: the measured r over the Amount × Tone × fixture × note grid of SC-012(a); the law
    that fits it; the **flip point** Amount_flip (per Tone, fixture and note) where the perceived fundamental changes,
    located by bisection to within 0.01 of Amount (SC-012(b)); and the width of the ambiguous band around it.
  - Supporting evidence (not the deliverable): `Rectifier` alone over a b-grid, which isolates the wet path's law
    from the mix (SC-012(c)).
- **FR-032 Crush r law.** Crush publishes its r law. Crush is pitch-synchronous (FR-051), so every product is a
  harmonic of f₀ and r ≡ 1 by construction, recorded with measured evidence (SC-012(d)).
- **FR-033 Fracture r law.** Fracture publishes its r law under the FR-050 definition. Amount moves r from 1 (Amount 0)
  toward 0.5, inside `AnchorOscillator`'s [0.25, 4] clamp; it is gated by SC-012(d) and SC-024. The flip Amount depends
  on the input spectrum (raw sine 0.13 … raw growl 0.64; Fold-100 saw / sine / triangle 0.27 … 0.40), so the observer is
  stated for the **Fold-output input class** with the FR-036 hysteresis; raw-fixture cells are recorded (C-5 (user ruling 2026-10-15, Clarifications "Plan S1 rulings"). Within that class the harvest found the Fold-100 saw flipping 0.1–0.2 Amount before sine and triangle at Tone ≥ 0.75 (empty three-way intersection), so the edges are the **sine ∩ triangle** bands and the saw is recorded: the observer never flips before any fixture's audio has, and on a saw-like voice FOLLOW lags Fracture by up to 0.17 Amount (user ruling 2026-10-16, Clarifications "Group 10 close rulings"; Phase 9 record). If the FR-050 fallback
  is ever taken, the re-ruled law is written here and in SC-024 first (FR-070).
- **FR-034 r ≡ 1 evidence for the rest.** Tube, Fold, Bite and the Shape regimes each record r ≡ 1 with measured
  evidence over the SC-012(f) grid: Tube, Fold and Bite at Amount 1 and Tone ∈ {0, 0.5, 1}; `ShapeMorph` at
  SHAPE ∈ {0, 0.25, 0.5, 0.75, 1}, which includes the crossfaded positions between anchors. Phase 3 FR-047 says
  "r ≡ 1 is never assumed". `ShapeMorph` needs no observer only if that evidence shows r ≡ 1 at every grid point. If
  any point (for example HOLLOW's notch near h1, or a crossfade between two regimes) moves the perceived fundamental,
  `ShapeMorph` gains an observer and its law is gated like FR-031. Measured 2026-10-16 (user ruling 2026-10-16, Clarifications "Group 10 close rulings"): every gated
  cell (input reads f₀) holds r ≡ 1; the `kGrowl` cells, whose input reads 2·f₀ by 1.8–2.6 dB, come out reading f₀
  through every nonlinear stage and most SHAPE positions and are recorded under C-3, not gated; no observer is added.
- **FR-035 Reference estimator.** The measured reference is the offline double-precision harmonic-sum
  perceived-fundamental estimator from Phase 3 SC-008(r), applied to the rendered output.
- **FR-036 Observer with offsets and hysteresis.** `perceivedPitchRatio()` returns one float, computed from the
  centre Amount and Tone. Per-channel offsets (FR-027) are ignored. The L/R mismatch near the Rectify flip point at
  ±0.15 offsets is recorded (SC-012(g)), not gated.
  - For Rectify with `setInputHalfWaveSymmetric(true)`, the observer has a hysteresis band at the flip point whose edges are the
    **intersection band** [max A_lo, min A_hi] of the measured ambiguous bands (SC-012(b)) over the half-wave-symmetric
    fixtures × notes, **per Tone** (C-4 (user ruling 2026-10-15, Clarifications "Plan S1 rulings"); each edge lies inside every cell's band. Raising Amount flips r from 1 to 2 at the band's upper edge, and
    lowering it flips r back at the lower edge. Modulating Amount inside the band never toggles r, so a FOLLOW Anchor
    does not flap between octaves.
  - **Fracture** (C-5 (user ruling 2026-10-15, Clarifications "Plan S1 rulings"): the observer reports r for Character's realistic input class, the Fold output, with a
    hysteresis of the same form whose edges are the intersection band of the **Fold-100 sine and triangle** flip bands
    per Tone over the notes (user ruling 2026-10-16, Clarifications "Group 10 close rulings": the three-way band with the saw is empty at Tone 0.75 and 1; the saw is
    recorded; the across-note spread of the sine / triangle bands, up to two bisection steps, is recorded and the
    intersection over notes keeps each edge inside every gated cell's band). Model seed [0.326, 0.342] at E1 /
    Tone 0.5; final edges from the r-law harvest. If an intersection is empty the item returns to the user; it is
    never widened silently.
  - The hysteresis state is cleared by `reset()`. Gate: SC-026.

### FR-040 series — Phase 9 handoff records (roadmap lines 571–579)

- **FR-040 h1 transfer record.** The comply evidence records the h1 transfer (dB and degrees, descriptor definition
  above) at E0, E1 and E2, on fixtures `kSawAnchor` and `kGrowl` at the calibration level, for:
  - every algorithm at Amount 1, with Tone at 0, 0.5 and 1;
  - every Shape anchor.

  Phase 9's coherent-sum bound for the Anchor + core sum depends on this record (roadmap lines 571–575). It is
  recorded, not gated. A cell whose output h1 is more than 60 dB below the input's is recorded as "h1 absent".
- **FR-041 Zero look-ahead.** Both stages and `Rectifier` report `latencySamples() == 0` (D-3). FR-040's record
  includes their group delay at h1.

### FR-050 series — Decisions of the 2026-10-15 clarify session

- **FR-050 Fracture definition (Q1; OQ-5 closed; law amended by C-5 (user ruling 2026-10-15, Clarifications "Plan S1 rulings").** Fracture is a period-doubling
  fracture: **polarity alternation of every other period of f₀ followed by the asymmetric bias-flip clip**,
  v = g·(x·((1 − a) + a·σ(φ))/P + β·sin(πφ)), y = (P/g)·(TanhADAA(v) − tanh(g·β·sin(πφ))), σ(φ) = tanh(4·sin(πφ))/tanh(4),
  a = Amount, g = 0.5 + 1.5·Amount, β = 0.5·Amount, with φ ∈ [0, 2) the divide-by-2 phasor in periods of f₀ integrated
  from the f₀ feed (FR-052), so the flip rate is f₀/2 and the output's true period is 2T. The bias-flip clip alone was
  measured at −69.5 dBFS and never flips `kGrowl`; the combined law reads −97.9 dBFS worst on the n·f₀/2 lattice and
  reaches r = 0.5 unambiguously on all four fixtures at Amount 1.
  - Amount moves r from 1 toward 0.5, inside `AnchorOscillator`'s [0.25, 4] clamp (FR-033, SC-024).
  - The nonlinear path is oversampled or ADAA (FR-061). Tone is per FR-055.
  - The phasor starts at a fixed phase, `reset()` restores it, and it is continuous in f₀ (no step when f₀ changes).
  - The `FeedbackDistortion` period-locked definition (the former OQ-5(a)) is the fallback only if this definition
    fails SC-005. Failing a bar is never grounds to relax it (FR-070): a fallback is recorded in Clarifications, in
    this row, in FR-033 and in SC-024 before it is built.
- **FR-051 Crush definition (Q2; D-6 ratified).** Crush is a pitch-synchronous sample-hold at m·f₀ (m set by Amount) at
  the oversampled rate, and it is noiseless: dither and jitter are off. It composes the `BitCrusher` primitive plus an
  inline hold; it does not use `BitcrusherProcessor` or `SampleRateReducer`. All products are harmonics of f₀, SC-010's
  Crush row stands as written, and r ≡ 1 by construction (FR-032). The hold multiple runs from m = 64 at Amount 0 to
  **m = 3 at Amount 1** (m(A) = 64·(64/3)^−A; user ruling 2026-10-16, Clarifications "Group 9 rulings, round 7": the Group 9 gate failed SC-005 Tube vs Crush on `kSawAnchor` at the round-2 value m = 2.5 (1.24 dB / 0.065 oct at E1, 1.09 / 0.052 at C2), because a non-integer m blends two hold counts and neither count's nulls survive, so the saw staircase has Tube's smooth 1/n spectrum; at m = 3 SC-005 passes 900/900, SC-007 saw audibility reads 3.30 dB / 0.15 oct (bar 3 dB, margin 0.3 dB recorded), no cell is silent, growl E1 reads −32 dBFS (−38 at 2.5); the growl's ≈ 20 dB drop at full Amount is a Phase 9 listening record. History: round 2 ruled m = 2.5 because at m = 2 the two hold points at ¼ and ¾ of the period sit on the zeros of every even partial of a sine-phase input, so an even-dominated growl was silenced at every synced onset; previously m = 2: user ruling 2026-10-16, Clarifications "Group 8 gate rulings": a 4-point hold of a saw is a saw-shaped staircase and failed SC-007
  on `kSawAnchor` once the grid was conditioned off the waveform edge, while a 2-point hold is square-like); the sample
  points sit at θ = (k + ½)/m, never on the edge (Group 8 fixer repair, plan S5.4 amended), with the 4-bit quantiser
  at Amount 1 unchanged. The quantiser has **per-slot hysteresis** (user ruling 2026-10-16, Clarifications "Crush
  threshold flicker"): each of the m hold slots keeps the held input value v_ref at which it last quantised (and that quantised level) and
  re-quantises only when a new held value differs from v_ref by more than ¼ step (main-loop refinement 2026-10-16,
  Clarifications "Group 9 rulings, round 4": the first form, which compared the new value with the stored level,
  latched a render's warm-up value whenever the steady value then sat within ¾ step of it, and SC-030 caught one onset in
  eight holding a different level), so a hold point within interpolation error of a threshold cannot flicker between
  periods while a warm-up or a note change re-latches at once (measured before the fix: an 8 Hz two-level modulation at −46 dBFS on Fold-100 `kGrowl` at
  E2 with m = 2, read by SC-011 as −65.7 dBFS). Still noiseless: no dither, no jitter.
- **FR-072 Phasor sync to the core (user ruling 2026-10-16, Clarifications "Group 9 rulings").** `BassCharacterStage`
  exposes `syncPhase(float coreCycles)` which sets Crush's hold phasor θ and Fracture's ÷2 phasor φ from the core's h1
  phase in cycles (θ = frac(coreCycles), φ = coreCycles mod 2, both at the hold grid's own convention), so that which part of
  the period is held or polarity-flipped is fixed by the waveform, not by onset alignment. The voice calls it at note-on,
  retrigger and bypass exit (Phase 9); the shared f₀ feed (FR-052) keeps the phasors locked afterwards. A branch start
  (`startBranch`) no longer resets the phasors to 0 when a sync has been supplied since `reset()`; `reset()` clears the
  sync and restores phase 0. Measured before the ruling: Fracture's centroid moved up to 1.97 oct and Crush's level up
  to 5.6 dB between onsets of the same note at different sub-period offsets. Gate: SC-030.
- **FR-052 f₀ feed (Q3).** `ShapeMorph::processBlock` and `BassCharacterStage::processBlock` take an optional
  `const float* f0PerSample` (default `nullptr`) that follows the core's post-Drop f₀ on the core's 16-sample cadence
  (the `ProfundumCore` / `AnchorOscillator` precedent, `anchor_oscillator.h:285-286`).
  - The keytracked frequencies (FR-018, D-4), the Crush hold (FR-051) and the Fracture period (FR-050) follow the
    pointer, so the spectrum stays fixed in harmonic numbers through a Drop (Phase 4 SC-028).
  - `setFundamentalHz` stays as the per-block fallback and is used when the pointer is null.
  - Values clamp as in `setFundamentalHz` ([8, 20000] Hz), and a non-finite entry keeps the previous value (FR-066).
  - The pointer path obeys FR-060 and FR-067. Gate: SC-025.
- **FR-053 Phase-matched dry share (Q5).** Any dry share (Rectify's, per D-5) is tapped after the same up / down
  oversampling round trip as the wet path, so dry and wet are phase-matched and a partial Amount does not
  comb-filter. Gate: SC-027.
- **FR-054 Amount-0 hard bypass (Q5).** Amount 0 selects a separate hard-bypass branch whose output is bit-identical to
  the input (FR-022, SC-008). It is entered and left through a short crossfade, and the plan documents its length. The
  crossfade length counts in SC-008's settle length. **Leaving the bypass pre-rolls** (user ruling 2026-10-16, Clarifications "Group 9 rulings, round 8"): the running
  branch(es) are reset, re-keyed and run unmixed for one period of f₀ while the output is still the bit-identical copy,
  their residual DC blockers are primed to the measured residual mean, and only then does the 5 ms crossfade start; the
  Amount-0 → small-Amount edge therefore takes 1/f₀ + 5 ms. The Amount 0 ↔ small-Amount edge is gated by SC-021's
  differential click count.
- **FR-055 Tone per algorithm (Q7).** Tone is defined per algorithm, with symmetry excluded as a Tone:
  - **Tube and Bite:** a pre-clip keytracked tilt (pivot 1·f₀; ±6 dB for Tube, **±9 dB for Bite** per the user ruling 2026-10-16, Clarifications "Group 8 gate rulings", after SC-028(d) read 2.59 dB against +18 dB of drive with the pre tilt proven wired by its negative control; SC-010, SC-006 and SC-007 re-measured on it) **and** a coupled post keytracked tilt (pivot 2·f₀,
    ±6 dB) in the same direction, both driven by Tone (C-8, plan refinement accepted 2026-10-15: a pre-clip tilt alone is
    not monotone on both fixtures).
  - **Rectify, Crush and Fracture:** a post keytracked tilt (pivot 2·f₀, ±12 dB).
  - **Fold:** a post keytracked tilt (pivot 2·f₀, ±12 dB). Tone never changes the fold's symmetry.
  - Every Tone is keytracked, with its pivot at k·f₀ (and so follows FR-052's f₀ feed).
  - 0.5 is neutral wherever Tone is a tilt.
  - Gate: SC-028; SC-006 stands for all six.
- **FR-056 Equal-loudness table (Q6).** `ShapeMorph`'s equal-loudness compensation is a static per-SHAPE table
  calibrated on the mean of `kSawAnchor`, `kTriangleAnchor` and `kSineAnchor`. Those three are gated within ±2.5 dB
  across the SHAPE sweep (SC-002); `kGrowl` is presence-dominant (a keytracked low-pass ROUND removes 11–16 dB from it
  against 1–1.6 dB from the others) and a static table cannot hold it, so its swing is **recorded** for Phase 6 (Mass)
  and Phase 9 listening (C-6 (user ruling 2026-10-15, Clarifications "Plan S1 rulings"). There is no level follower.
- **FR-057 Algorithm-transition API (Q8).** Algorithm transitions are timed crossfades through `setAlgorithm` only.
  - D-7 stands: the last pending request is queued until the current 10 ms crossfade ends.
  - At most two algorithms run at once (SC-015 holds).
  - There is no `setAlgorithmBlend`.
  - Phase 8 switches at a threshold morph position.
  - Gate: SC-029.

### FR-060 series — Cross-cutting

- **FR-060 RT safety.** Nothing on the audio path allocates, locks, throws or does I/O: not `processBlock`, any setter,
  `reset` or a crossfade. Everything is sized in `prepare` (roadmap line 859). `AllocationScope` asserts this for
  every algorithm, every Shape position, an algorithm switch and an engage (SC-016). Several reused components
  allocate in their own `prepare`: `LadderFilter`, `FeedbackDistortion` (fallback only, through its
  `DelayLine`) and `Oversampler`. They are prepared only from the owning stage's `prepare`.
- **FR-061 Anti-aliasing (theme FR).** Every nonlinear path is oversampled or ADAA and meets SC-010 at E0–E2 (roadmap
  lines 433, 866). That covers ROUND's drive, BITE's clipper and all six algorithms. Three reused processors have no
  internal anti-aliasing, so the owning stage wraps them: `DiodeClipper` (line 90), `WavefolderProcessor` (line 92)
  and `TubeStage`.
- **FR-062 DC safety.** Every algorithm and every Shape regime meets SC-011 (roadmap line 434).
- **FR-063 Mono compatibility (theme FR).** Both stages touch stereo, so each ships a mono-sum test (SC-013; roadmap
  line 864).
- **FR-064 No shared-header change.** No existing header is modified. If the plan finds a reused header unusable, the
  change is append-only, and the Ruinae, Seraphis, Vorago, Innexus and Disrumpo suites stay green (roadmap lines
  876–877; SC-023). **Fallback used once** (user ruling 2026-10-16, Clarifications "Group 10 close rulings"):
  `DCBlocker::setState(x1, y1)` in `primitives/dc_blocker.h` (10 added lines, nothing changed), needed by the FR-026 /
  FR-054 pre-roll prime; the five consumer suites were rebuilt and are green (comply quotes their tails).
- **FR-065 CPU budget is an FR.** SC-015 sets the budget at ≤ 2 % of one core per voice at 48 kHz with Shape and
  Character on (roadmap line 435). It is never relaxed to pass. The gated cell is the mono-linked voice, as in
  Phases 1–4; the unlinked (stereo-forced) cost is recorded for Phases 7 and 9 (SC-015; user ruling 2026-10-15, Clarifications "T001 CPU bench ruling").
- **FR-066 Non-finite safety.** A NaN or Inf input sample yields finite output, and the state recovers (SC-017).
  Non-finite checks use bit patterns, not `std::isnan`, because of the `-ffast-math` rule. A non-finite setter
  argument keeps the previous value.
- **FR-067 Determinism and partition invariance.** Output does not depend on how the input is split into blocks, and
  repeated renders from `reset()` are identical (SC-018). Every random source (dither, jitter) is either disabled or
  seeded deterministically and restored by `reset()`.
- **FR-068 Sample rates.** Every component works at 44.1, 48, 88.2, 96 and 192 kHz.
  - Keytracked frequencies clamp below the reused filters' limits: `SVF::kMaxCutoffRatio = 0.495` and
    `LadderFilter::kMaxCutoffRatio = 0.45`.
  - The oversampling factor follows `HarmonicFold`'s table (4× up to 100 kHz, 2× above) unless the plan measures a
    reason to differ.
- **FR-069 Output bound.** For any input with peak ≤ 1, |output| stays at or below **6.0 linear** (+15.6 dBFS, the value of
  `HarmonicFold::kOutputCeiling`; D-9, ratified 2026-10-15) at every setting.
- **FR-070 Bar integrity.** The bars in the SC table stand as written. If a measurement proves a bar unreachable, it is
  re-ruled with the evidence in Clarifications. A bar is never relaxed silently.
- **FR-071 Portability.**
  - `node tools/check-portability.js` reports clean.
  - Brace initialisation uses designated initialisers.
  - No `std::isnan` under fast-math.
  - Any SIMD added uses unaligned loads.
  - Every new TU is clang-tidy clean.

---

## Success Criteria

**Differential click count (used by SC-003, SC-009, SC-021).** This adopts Phase 4's ruling B
(`specs/profundum-phase4-drop-note-behaviour/spec.md:951–957`). `ClickDetector` with its documented defaults
(`tests/test_helpers/artifact_detection.h:38–44`: 5-σ derivative threshold, frame 512, hop 256) flags the steady,
transition-free core 12–115 times per 0.5 s, because it fires on every band-limited saw or growl edge, and BITE, Bite,
Fold and Rectify sharpen those edges further. A raw count of zero is therefore unreachable on the fixture input alone.
- A detection in the transition render **counts** only if **neither reference render** of the same cell, rendered
  without the transition (the stage held at the transition's **start** setting, and the stage held at its **end**
  setting, each phase-aligned sample for sample with the transition render), has a detection within ±N samples of
  it. N is fixed per cell at ≤ 1/8 of the period of f₀ and recorded with the method in the test. (Main-loop
  refinement of ruling B, 2026-10-16, Clarifications "Group 3 gate rulings": the end setting's steady detections are
  not transition artefacts; with a start-only reference no correct stage can pass a step into a regime whose steady
  output has more detections.)
- **Long ramps** (main-loop refinement 2026-10-16, Clarifications "Group 6 gate ruling"): the count is a per-sample
  comparison against a phase-aligned held render, so it is well posed for a step and for the short ramps the SCs name
  (20 ms in SC-021, 50 ms in SC-003: within the smoother's own settling). For a ramp longer than that on a stage whose
  steady output positions depend on the setting (a pitch-synchronous hold's staircase), no held render is
  phase-aligned with the ramp and the count is ill-posed. Such a ramp is gated by the **sample-delta bound in the
  SC-003(b) form**: every \|Δy\| during the ramp and its settle ≤ 1.1 × the maximum steady-state \|Δy\| over a grid of
  the traversed settings (≥ 21 points, endpoints included), and the differential click count (two-reference form) is
  **recorded**. The SC rows' own step and short-ramp cells are unchanged.
- Same detector, same defaults, in both renders. The detector threshold is never calibrated on the reference.
- **Amplitude guard** (user ruling 2026-10-16, Clarifications "Group 9 rulings, round 2"): a transition detection that passes the reference test counts only if the transition's
  max \|Δy\| within ±N of it also exceeds **1.1 ×** the larger of the two references' max \|Δy\| within the same ±N; a new
  detection whose step is no larger than the steady steps around it is a detector-alignment case, not a click.
- **Crush** (user ruling 2026-10-16, Clarifications "Group 9 rulings, round 2"): its staircase positions depend on the setting, so no held render matches a moving m; the SC-021
  Crush cells (steps, 20 ms ramps and the bypass edge) are gated by the sample-delta bound in the SC-003(b) form over a
  grid of the traversed Amount / Tone settings (≥ 21 points) and the count is recorded.
- **Bar: zero counted detections.**
- **Positive controls (each SC that uses the count):** the same harness must count ≥ 1 detection on (i) a hard
  algorithm / regime switch with no crossfade, and (ii) an unsmoothed full-range Amount (or SHAPE or blend) step. A
  control is gated **per SC arm and per note**: at least one fixture of the arm flags it at every note, and every cell
  is recorded (main-loop refinement 2026-10-16: a pulse-like `kGrowl` at C2 has no snap sample away from its own edges
  that the ±N rule does not mask, so a per-cell reading is unreachable there by construction). If a control is not
  flagged at some note, the SC fails.

| ID | Criterion (roadmap line) | Metric, threshold, method | Test name sketch |
|---|---|---|---|
| **SC-001** | Shape regimes are distinct (417–421, 861) | Compare ROUND (SHAPE 0), HOLLOW (0.5) and BITE (1), level-matched, on `kSawAnchor` and `kGrowl` at E1 and C2. Every pair is separated by the OQ-S bar: at least one of E_low, E_high, R_oe, THD or I differs by ≥ **3 dB**, or C differs by ≥ **0.25 oct**, with the descriptor floor applied (clamp ≥ −60 dB; counts only if one side is above −40 dB re total). | `ShapeMorph_RegimesPairwiseDistinct` |
| **SC-002** | The SHAPE sweep is continuous (432) | Static sweep over 101 positions with `kSawAnchor` at E1, C2 and C3. Between adjacent steps: \|ΔL\| ≤ **0.5 dB**, \|ΔC\| ≤ **0.1 oct**, and \|ΔE_low\|, \|ΔE_high\| ≤ **1 dB**. **Equal loudness (FR-056):** across the whole sweep, for each of `kSawAnchor`, `kTriangleAnchor` and `kSineAnchor` at E1, C2 and C3 over the same 101 positions, L stays within **±2.5 dB** of that fixture's sweep mean (table calibrated on the mean of those three); `kGrowl`'s swing over the same sweep is **recorded**, not gated (C-6 (user ruling 2026-10-15, Clarifications "Plan S1 rulings"). This replaces the saw-only ±1.5 dB clause. The per-step bars above are ratified (D-10). | `ShapeMorph_SweepContinuity` |
| **SC-003** | SHAPE automation is click-free (432) | Cases: SHAPE 0 → 1 and 1 → 0, each as a 50 ms ramp and as a single step applied at a block boundary, at host block sizes {64, 512}; plus engage and disengage at SHAPE ∈ {0, 0.5, 1}; at E0 and E2, `kSawAnchor` and `kGrowl`, 48 kHz. **(a)** Differential click count (above) = **0**, with its two positive controls flagged; for the **single-step** cells a detection that falls inside the 10 ms SHAPE ramp window (step … step + ramp + N) is **recorded**, not counted, and arm (b) is that cell's gate (user ruling 2026-10-16, Clarifications "Group 10 close rulings": the ramp between BITE and HOLLOW, 0.32 apart at the same instant on `kSawAnchor` E0, moves 0.002 per sample of its own on top of E0's native 0.003, over the guard's 1.1 × 0.0034; a crossfade the detector cannot align, the Crush SC-021 precedent; measured (b) ratio 0.24). **(b) Sample-delta bound (SHAPE moves; Phase 2 SC-009 form):** every \|y[k] − y[k−1]\| during the transition is ≤ **1.1 ×** the maximum steady-state \|y[k] − y[k−1]\| over the SHAPE values the transition passes through, measured from steady renders on a 101-point SHAPE grid (endpoints included) within the traversed range, in the same test. **(c) Engage / disengage:** both branches (bypass and the regime at fixed SHAPE) are at steady state, so the D-11 bound applies: measured max \|Δy\| ÷ (g_a·D_a + g_b·D_b + \|Δg_a\|·max\|x_a\| + \|Δg_b\|·max\|x_b\|, as in SC-009) ≤ **1**. | `ShapeMorph_AutomationClickFree`, `ShapeMorph_ClickCountPositiveControl` |
| **SC-004** | Shape keytracks (41) | Input: `kSawAnchor` at the −12 dBFS RMS calibration level. \|H(n)\| = w_n(output) / w_n(input) from the same render (`renderedHarmonicPowers`), for n ≤ 16 at SHAPE ∈ {0, 0.25, 0.5, 0.75, 1}. ROUND's drive and BITE's clipper run at the calibration level, the level they ship at, so their generated components are explicitly included in \|H(n)\|. Wherever n·f₀ < 0.45·fs at both notes, C1 and C3 differ by ≤ **1.5 dB** per harmonic. | `ShapeMorph_HarmonicResponseTracksKeyboard` |
| **SC-005** | Character signatures are pairwise distinct (430) | Six algorithms at Amount 1 and Tone 0.5, level-matched, on `kSawAnchor` and `kSineAnchor` at E1 and C2. All 15 pairs are separated by the OQ-S bar: at least one of E_low, E_high, R_oe, THD or I differs by ≥ **3 dB**, or C differs by ≥ **0.25 oct**, with the descriptor floor applied (clamp ≥ −60 dB; counts only if one side is above −40 dB re total). **Positive control (OQ-S floor):** two renders that differ only below the floor (the same `kSawAnchor` render, one with a −70 dB re total inharmonic tone added and one with a −130 dB one, so I moves by 60 dB while both sides stay below −40 dB) must be judged **not distinct**; and the same pair with the added tone at −30 dB must be judged distinct. | `BassCharacter_AlgorithmsPairwiseDistinct`, `SeparationBar_FloorControl` |
| **SC-006** | Tone is monotonic in centroid (431) | Every algorithm, Tone in 11 steps, Amount ∈ {0.5, 1}, `kSawAnchor` and `kSineAnchor`, E1 and C2. C never decreases. **Tolerance τ is measured, not assumed:** per cell, the measurement noise floor ε is the largest \|ΔC\| between renders of the identical setting started at 8 different sub-period start offsets (window phase) and at block sizes {1, 64, 512}; τ = 2·ε, capped at 0.005 oct (if the measured ε exceeds 0.0025 oct the cell is reported and the harness, not the bar, is fixed). Every step ΔC ≥ −τ, **and** the sum of all negative steps across the 10-step sweep is ≥ −τ (a cumulative reversal cannot exceed one tolerance). ε and τ are recorded per cell. For audibility, the span C(Tone 1) − C(Tone 0) is ≥ **0.25 oct** on `kSawAnchor` for every algorithm; the `kSineAnchor` span is **recorded** (an h1-dominated sine output moves at most 0.11 oct under any first-order tilt; C-7 (user ruling 2026-10-15, Clarifications "Plan S1 rulings"). Monotonicity gates both fixtures. | `BassCharacter_ToneMonotonicCentroid` |
| **SC-007** | Amount is audible (54, 861) | Every algorithm, Amount 0 → 1 at Tone 0.5, at E1, with `kSawAnchor` and `kSineAnchor`. At least one of THD, I and R_oe moves by ≥ **3 dB** under the OQ-S descriptor floor (each value clamped to ≥ −60 dB, and the descriptor counts only if one side is above −40 dB), or C moves by ≥ **0.25 oct**. A THD move from −140 dB to −110 dB therefore does not count. The 11-step trajectory is recorded. | `BassCharacter_AmountAudible` |
| **SC-008** | Transparent when off (562) | Character at Amount 0 and Shape disabled. **Settled** means every sample from max(100 ms, 5 × the longest documented smoother time constant or crossfade length in the stage) after the last control change. From that sample on, the output is bit-identical to the input for every algorithm, on both channels. The settle length used is recorded. | `BassCharacter_AmountZeroBitIdentical`, `ShapeMorph_DisabledBitIdentical` |
| **SC-009** | The algorithm-switch sample delta is bounded (426, 432) | All 30 ordered pairs at Amount 1 and Tone 0.5, at E1 and C2, switching mid-block; the request precedes the crossfade by the FR-026 pre-roll of one period, during which the outgoing algorithm alone is heard, and the bounds below are measured from the request through the crossfade end (user ruling 2026-10-16, Clarifications "Group 9 rulings, round 8"). Both algorithm branches run at fixed settings, so D-11 applies: max \|Δy\| over the crossfade ÷ the analytic bound **g_a·D_a + g_b·D_b + \|Δg_a\|·max\|x_a\| + \|Δg_b\|·max\|x_b\|** ≤ **1**, where y = g_a·x_a + g_b·x_b is the crossfade and D_a, D_b are the steady-state max sample deltas measured on each branch alone (Phase 4 SC-010(b) pattern). Differential click count (above) = **0**, with its two positive controls flagged. **Level:** the transition level L_tr[k] (one-period sliding RMS, descriptor definitions) over the window [switch − T, switch + crossfade + T], T = 1/f₀, stays within [min − **2.5 dB**, max + **1.5 dB**] of the two steady-state levels measured with the same one-period RMS on the outgoing and incoming steady renders (the window runs from one period before the request to one period after the crossfade end), and the midpoint excursion is recorded per pair (user ruling 2026-10-16, Clarifications "Group 8 gate rulings": the ideal fixed-exponent crossfade alone reads +1.04 dB on coherent pairs and −2.14 dB on incoherent ones at α = 1.493, and no exponent meets ±1 dB on both sides; the window is the floor of any fixed law, and no level follower is put on the switch path). | `BassCharacter_SwitchClickFree`, `BassCharacter_ClickCountPositiveControl` |
| **SC-010** | Aliasing ≤ −80 dBFS at E0–E2 (433, 866) | Output A ≤ **−80 dBFS** at E0, E1 and E2, at fs ∈ {44.1, 48, 96} kHz, with the same `aliasedPower` mask for every algorithm, Crush included. Inputs: `kSawAnchor` and `kGrowl`, raw and Fold-100. **Character:** every algorithm at Amount 1 with Tone ∈ {0, 1}. **Shape:** SHAPE ∈ {0, 0.25, 0.5, 0.75, 1}. **Rectifier (standalone):** b ∈ {0, 0.5, 1}, same inputs, notes and rates. **Attribution:** A is also measured at the stage input, and the comply record carries, per cell, A_in, the stage's h1 power gain G, and the stage-added aliasing A_added = 10·log₁₀(max(P_alias,out − G·P_alias,in, 0)). The gate is the absolute output A; if a Fold-100 cell fails only because of the input residual (A_added ≤ −80 dBFS), the cell is re-ruled under FR-070 with that evidence in Clarifications and in this row, never passed silently. **Positive control:** Bite (or Tube) with its oversampling / ADAA removed must read above −80 dBFS on raw `kSawAnchor` at E2 / 44.1 kHz under the same harness. Crush is pitch-synchronous (FR-051), so it is measured with the same mask and this row stands as written for it. **Fracture** (FR-050) is gated at A ≤ **−80 dBFS** with the mask placed on the **n·f₀/2 lattice** (`aliasedPower(x, aliasFftLength(fs, f₀/2), fs, f₀/2)`), because its sidebands at odd multiples of f₀/2 are the definition and read −17 to −20 dBFS on the n·f₀ mask by construction; the n·f₀-mask value is recorded (C-9 (user ruling 2026-10-15, Clarifications "Plan S1 rulings"; the FR-070 re-rule this row anticipated). | `BassCharacter_AliasingE0E2`, `ShapeMorph_AliasingE0E2`, `Rectifier_AliasingE0E2`, `ShapeCharacter_AliasGatePositiveControl` |
| **SC-011** | DC ≤ −80 dBFS (434) | Mean over integer periods after a 1 s settle, on the SC-010 grid at 48 kHz: ≤ **−80 dBFS**. | `BassCharacter_DcBound`, `ShapeMorph_DcBound`, `Rectifier_DcBound` |
| **SC-012** | r-law deliverables (427–429) | All cells at 48 kHz; reference = FR-035 estimator. **Ambiguity rule (every clause; C-2 (user ruling 2026-10-15, Clarifications "Plan S1 rulings"):** the candidates are the estimator's best f̂ and its octave neighbours {f̂/2, 2·f̂}; a cell is ambiguous when the best and second-best candidate saliences differ by less than **1 dB** (20·log₁₀ of the SHS salience ratio). The f/2 echo of a pure tone scores ≤ −1.51 dB and so never makes a cell ambiguous; real octave content can. **Input-gating rule (C-3 (user ruling 2026-10-15, Clarifications "Plan S1 rulings"):** in clauses (a), (d), (e) and SC-024(a) a cell is gated only if the stage **input** reads f₀ unambiguously under this rule; every other cell records f̂_in, f̂_out and r_obs and is listed in comply (expected: every `kGrowl` cell, raw and Fold-100, since the fixture itself reads 2·f₀). Clause (f) gates the cells whose **input** reads f₀ unambiguously on the ratio r_ref = f̂_out / f̂_in and records the rest (user ruling 2026-10-16, Clarifications "Group 10 close rulings": the first full (f) grid passed every non-growl cell and failed 71 of 72 `kGrowl` cells through Tube / Fold / Bite and 27 of 40 through `ShapeMorph` at −1200 cents; the raw growl reads 2·f₀ by only +2.6 dB (Fold-100 +1.8 dB) and every nonlinear stage, and most SHAPE positions, add enough f₀-series salience that the output reads f₀; nothing removes h1 and the anchor at f₀ stays right, so the S6.5 observer route was rejected and the growl cells are recorded for Phase 9's FOLLOW row, which takes r from the engine, never from processed audio). Ambiguous cells are recorded, never silently dropped: in an ambiguous cell `perceivedPitchRatio()`·f₀ must equal **one of the two candidates within 1 cent**. In clauses (a), (d) and (e), per (fixture, note) at most **20 %** of cells may be ambiguous and at least **8** cells must be unambiguous; the fraction is recorded. In clause (f) no gated cell may be ambiguous (r ≡ 1 evidence needs every gated cell). **(a) Rectify over the stage's controls.** `BassCharacterStage` at Rectify, Amount in 11 steps (0, 0.1, …, 1) × Tone ∈ {0, 0.5, 1} × fixtures {`kSineAnchor`, `kTriangleAnchor`, `kSawAnchor`, `kGrowl`} × notes {E0, E1, E2, C2, C3}. The derived b and wet mix are recorded per Amount. Run twice: **with the descriptor** (`true` on sine / triangle, `false` on saw / growl), every unambiguous cell matches the reference within **1 cent**; **without it** (default `false`), every saw / growl cell matches within 1 cent, and the sine / triangle cells' deviation is recorded (the documented cost of the default). **(b) Flip point.** For each half-wave-symmetric fixture × note × Tone, bisect Amount until the reference's salience crossing (f₀ → 2·f₀) is located to **0.01** of Amount. Unambiguous cells must exist on both sides of every flip, and the ambiguous band [A_lo, A_hi] around the flip is measured and its width in Amount recorded per Tone and note (the hysteresis band of FR-036 / SC-026 is this width). The Amount at which the observer's r steps must lie **within the band** [A_lo, A_hi]. **(c) Rectifier supporting evidence (recorded).** `Rectifier` alone at b ∈ {0, 0.25, 0.5, 0.75, 1} × the same fixtures × notes: the reference r per cell, which isolates the wet path's law from the mix. **(d) Crush and Fracture.** Amount ∈ {0, 0.25, 0.5, 0.75, 1} × Tone ∈ {0, 0.5, 1} × fixtures {`kSineAnchor`, `kTriangleAnchor`, `kSawAnchor`, `kGrowl`} × notes {E0, E1, E2, C3}: every unambiguous, input-gated cell's `perceivedPitchRatio()` equals the reference within **1 cent**. For Fracture the gated cells are the **Fold-100 sine and triangle** inputs (the observer is stated for Character's realistic input class, FR-033 / C-5 (user ruling 2026-10-15, Clarifications "Plan S1 rulings"); the Fold-100 **saw** cells are recorded too (user ruling 2026-10-16, Clarifications "Group 10 close rulings": the saw flips 0.1–0.2 Amount earlier than sine and triangle at Tone ≥ 0.75, so no single edge sits inside all three bands) and the raw-fixture cells are recorded with their deviation, the same pattern as Rectify's default-descriptor cost. **(e) Real upstream chain.** Core at `kSawAnchor`, `kGrowl` and `kTriangleAnchor` → `ShapeMorph` at SHAPE 1 (BITE) → `HarmonicFold` at 100 → `BassCharacterStage` at Rectify, Amount in 11 steps × Tone ∈ {0, 0.5, 1} × {E0, E1, E2, C3}, with the descriptor the voice would supply for that chain by the FR-031 descriptor rule (`true` when w₂ at the Rectify input is below −20 dB re w₁, measured by the test; user ruling 2026-10-16, Clarifications "Group 10 close rulings"). Cells whose w₂ lies inside the −24 … −16 dB uncertainty band are recorded, not gated (the BITE → Fold-100 triangle reads −19.9 dB); the observer matches the reference within **1 cent** in every unambiguous gated cell. The drafted premise (`false` whenever w₂ ≥ −40 dB) failed 25 triangle cells: at Amount ≥ 0.8 the audio doubles while the observer said 1. **(f) r ≡ 1 evidence (FR-034).** All four fixtures, raw and Fold-100 × notes {E0, E1, E2, C3}. Tube, Fold and Bite at Amount 1 × Tone ∈ {0, 0.5, 1}; `ShapeMorph` at SHAPE ∈ {0, 0.25, 0.5, 0.75, 1} (anchors and crossfaded positions). In every cell whose input reads f₀ unambiguously, r_ref = f̂_out / f̂_in is 1 within **1 cent** with both readings unambiguous; no gated cell may be ambiguous; the `kGrowl` cells (input reads 2·f₀) record f̂_in, f̂_out and r_ref and are listed in comply (user ruling 2026-10-16, Clarifications "Group 10 close rulings"; the clause was first written to gate `kGrowl` on the ratio and did catch every stage restoring the growl's f₀ series). **(g) L/R mismatch (recorded, FR-036).** At the maximum offsets, **±0.15** on Amount and Tone (D-12, FR-027), the Rectify flip Amount of L and of R is recorded against the centre flip, per Tone and note; it is not gated. | `BassCharacter_RLawMatchesReference`, `BassCharacter_RectifyFlipPoint`, `Rectifier_RLawEvidence`, `BassCharacter_RLawUpstreamChain`, `BassCharacter_RIsOne`, `ShapeMorph_RIsOne` |
| **SC-013** | Mono-sum (864) | **(a)** With zero offsets and L == R input, the output is L == R bit-identical for both stages, every algorithm and every Shape anchor. **(b)** At the maximum channel offsets, **±0.15** on Amount and Tone (D-12, FR-027), at E0–E2, the energy of (L + R)/2 below 2·f₀ is within **1 dB** of the mean L/R energy in that band for Tube, Fold, Bite and Crush. For **Rectify and Fracture**, whose Amount and Tone offsets rotate h1 in opposite directions on L and R (measured −1.40 … −1.46 dB and −1.10 … −1.15 dB at ±0.15), the gate is applied at the **largest offset on {0.05, 0.10} that meets 1 dB** (per algorithm, the per-algorithm offset ceiling), the ±0.15 reading is recorded, and the ceilings are a Phase 7 handoff record that WIDE must not exceed (user ruling 2026-10-16, Clarifications "Group 8 gate rulings"). **(c)** The offsets are not inert: for every algorithm, \|THD(L) − THD(R)\| ≥ **1 dB** (under the OQ-S descriptor floor) or \|C(L) − C(R)\| ≥ 0.05 oct. | `BassCharacter_MonoSum`, `ShapeMorph_LREqual` |
| **SC-014** | The Rectifier law (423–424) | **(a) Law tap.** `Rectifier::transfer(x, b)` (FR-001) over x on a 4001-point grid in [−2, 2] (zero included) × b ∈ {0, 0.25, 0.5, 0.75, 1} matches FR-002's law evaluated in double within **1e-6** absolute. **(b) Processed path, end to end.** `processBlock` on a −12 dBFS RMS sine at C3, 48 kHz, after a 1 s settle, at b ∈ {0, 0.5, 1}: the output's harmonic levels w_n for n = 1…8 match the analytic Fourier series of the law (DC excluded, the documented gain stage and the DC blocker's recorded h1 attenuation applied) within **0.1 dB** wherever the analytic level is above −60 dB re the largest harmonic. (A slow ramp is not used: the DC blocker removes it.) **(c)** Rectifier's own SC-010 row (`Rectifier_AliasingE0E2`) and SC-011 row (`Rectifier_DcBound`) pass. | `Rectifier_TransferLaw`, `Rectifier_ProcessedHarmonics` |
| **SC-015** | CPU ≤ 2 % per voice (435, 869) | Setup: 48 kHz, stereo input with L == R and zero channel offsets, i.e. the **mono-linked** cell, the same per-voice cell Phases 1–4 gated (Phase 2 compliance: 1.708 % linked gated, 2.462 % stereo-forced recorded; user ruling 2026-10-15, Clarifications "T001 CPU bench ruling", which supersedes plan C-20's unlinked reading after T001 measured the unlinked cell at 4.40 % with Tube at the ruled 8× alone costing 2.0 % of it); the test asserts both stages are linked through the probes before timing. The **unlinked** cell (decorrelated L/R input: `kSawAnchor` on L, the same fixture offset by 0.37 period on R, `setChannelOffsets(0.15, 0.15)` on Character) is measured in the same case and **recorded**, not gated, as the Phase 7 input (WIDE offsets force unlinking and Phase 7 must budget that) and a Phase 9 handoff record. Amount 1, E1, no crossfade active. Shape is enabled at its worst position and Character runs its worst algorithm; the sweep finds both, and both are recorded. The worst crossfade cell is recorded as well. Run as the single case pinned and isolated via `pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe <dsp_systems_tests.exe> -ExeArgs "ShapeCharacter_CpuBudget"` (the same pinning `run-cpu-tests.js` applies; the whole-exe lane would also run every `[long]` case) and take the median of three: ≤ **2.0 %** of one core. The cost of each component is recorded. | `ShapeCharacter_CpuBudget` [performance] |
| **SC-016** | RT safety (859) | `AllocationScope` reports 0 allocations across `processBlock` (with and without the `f0PerSample` pointer), every setter, `reset`, an algorithm switch and an engage, for every algorithm and every Shape anchor. | `ShapeCharacter_NoAllocation` |
| **SC-017** | Non-finite safety | A 16-sample NaN / +Inf / −Inf burst at the input, for every algorithm and every Shape anchor. Every output sample is finite (bit-pattern test). `stateFinite()` is true within 1 block after the burst ends. Within 100 ms of the burst's end, L, E_low and E_high return within **0.1 dB** and C within **0.01 oct** of the pre-burst steady state. | `ShapeCharacter_NonFiniteRecovery` |
| **SC-018** | Determinism and partition invariance | Block sizes {1, 17, 32, 64, 512}. The output, with and without the `f0PerSample` pointer, is bit-identical across partitions; if a reused component is block-dependent, the residual is ≤ −120 dBFS and recorded. Two renders from `reset()` are bit-identical. | `ShapeCharacter_PartitionInvariant` |
| **SC-019** | Sample-rate behaviour (FR-068) | At 44.1, 48, 88.2, 96 and 192 kHz, every algorithm and Shape anchor is finite and bounded. At E1, C differs by ≤ **0.1 oct** and L by ≤ **0.5 dB** between 48 and 96 kHz. | `ShapeCharacter_SampleRates` |
| **SC-020** | Phase 9 handoff record (571–575) | Comply quotes FR-040's h1 transfer table and FR-041's `latencySamples() == 0` assertion. | `ShapeCharacter_H1TransferRecord` |
| **SC-021** | Parameters are zipper-free | Amount, Tone, blend and the offsets, each as a full-range step and as a 20 ms ramp, at E1 and C2, `kSawAnchor` and `kGrowl`, every algorithm. Differential click count (above) = **0**, with its two positive controls flagged; the `TubeStage` bypass-threshold crossing (Edge Cases) is one of the covered cells. **Amount-0 bypass edge (FR-054):** the transition between Amount 0 (hard bypass) and a small Amount, in both directions, as a step and as a 20 ms ramp, is covered for every algorithm and gated by the same differential click count, with its positive control (a hard, uncrossfaded bypass switch must be flagged). | `ShapeCharacter_ParameterStepsClickFree` |
| **SC-022** | Output bound (FR-069) | Inputs with peak 1: Fold-100 `kGrowl`, and a full-scale sine at E0 and at C3. \|output\| ≤ **6.0 linear** (+15.6 dBFS; D-9) at every algorithm / Amount / Tone / SHAPE grid point. | `ShapeCharacter_OutputBounded` |
| **SC-023** | Shared suites, portability, tidy (859–877) `dsp_processors_tests`, `dsp_systems_tests` and all Profundum suites are green. **Either** `git diff` shows no change to any reused header, **or**, if FR-064's append-only fallback is used, `ruinae_tests`, `seraphis_tests`, `vorago_tests`, `innexus_tests` and `disrumpo_tests` are all green, with each suite's tail output quoted in comply alongside the diff of the changed header. `check-portability.js` is clean. clang-tidy is clean on the new TUs. | comply record |
| **SC-024** | Fracture definition and r law (FR-050, FR-033; 425–429) | `BassCharacterStage` at Fracture, Amount in 11 steps × Tone ∈ {0, 0.5, 1} × fixtures {`kSineAnchor`, `kTriangleAnchor`, `kSawAnchor`, `kGrowl`} × notes {E0, E1, E2, C3}, 48 kHz. **(a) r law:** `perceivedPitchRatio()` is exactly 1 at Amount 0, lies in [0.5, 1] and [0.25, 4] everywhere, is non-increasing in Amount, and is below 1 at Amount 1. The measured r(Amount) table is recorded and the observer matches the reference within 1 cent in every unambiguous, input-gated cell on the **Fold-100 sine and triangle** inputs; the Fold-100 saw and the raw-fixture cells are recorded with their deviation (user ruling 2026-10-16, Clarifications "Group 10 close rulings") (SC-012(d), C-3, C-5 (user ruling 2026-10-15, Clarifications "Plan S1 rulings"). The size of the move at Amount 1 is measured and recorded; if it is under 100 cents, the definition is judged not to deliver "toward 0.5" and the finding goes to the clarify session (FR-070), not silently accepted. **(b) Mechanism:** the power at odd multiples of f₀/2 is at or below −60 dB re total at Amount 0 and at or above −40 dB re total at Amount 1 (OQ-S floor convention), proving the bias flip is real. **(c) Fallback gate:** if SC-005 fails for Fracture, the `FeedbackDistortion` fallback (FR-050) is written into this row first, and SC-005, SC-010, SC-012(d) and SC-015 are re-run on it. | `BassCharacter_FractureRLawAndSidebands` |
| **SC-025** | f₀ feed and Drop tracking (FR-052; Phase 4 SC-028) | Core Drop of 48 st over 60 ms from E2 (post-Drop f₀ fed as `f0PerSample` on the 16-sample cadence), `kSawAnchor` and `kGrowl`, 48 kHz, host block sizes {64, 512}, `ShapeMorph` at SHAPE ∈ {0, 0.5, 1} and every Character algorithm at Amount 1, Tone 0.5. **(a) Harmonic numbers stay fixed** (user ruling 2026-10-16, Clarifications "Group 9 rulings, round 2"): at 5 instants along the Drop every keytracked member read through the probes (ROUND cutoff, HOLLOW centre, BITE pivot, every Tone pivot, the Crush hold multiple and the Fracture period) equals the value a steady render at that instant's f₀ holds within **0.1 %**; the all-linear HOLLOW regime (SHAPE 0.5) additionally meets \|H(n)\| (SC-004 definition, n ≤ 16 where n·f₀ < 0.45·fs) within **1.5 dB** per harmonic of a steady render at that instant's f₀; for the nonlinear regimes and algorithms the \|H(n)\| deviation is **recorded** (the core's partials above h14 leave a Drop with phases that differ from a fresh note's, h16 by 3.3 rad, so a waveform-dependent stage's per-harmonic transfer is not comparable: ROUND read 16.8 dB at h8 on growl 0.4 s after the pitch settled). **(b) Positive control:** the same Drop rendered with only per-block `setFundamentalHz` at block size 512 must deviate by more than 1.5 dB in at least one harmonic for at least one of `ShapeMorph` and Crush, proving the pointer matters; the deviation is recorded. **(c) Crush and Fracture follow the feed:** during the Drop Crush's hold phasor increment and hold multiple read through the probe equal f₀(t)/(F·fs) and m(Amount) capped at F·fs/(4·f₀(t)) at every sample of the 5 instants' frames within 0.1 % (main-loop refinement 2026-10-16 of the SC-025(a) ruling, Clarifications "Group 9 rulings, round 3": the inharmonic fraction of a broadband staircase projected on an approximate chirped lattice has a floor near −35 dB that the steady −60 dB can never meet, so I is **recorded** per cell), and Fracture's flip rate follows f₀/2 (the SC-024(b) sidebands stay on the f₀/2 lattice of the instantaneous f₀). **(d) Click-free:** differential click count (above) = **0** through the Drop, with its two positive controls flagged; for Crush the sample-delta bound in the SC-003(b) form gates the cell and the count is recorded (the SC-021 Crush ruling applied to a moving f₀; main-loop refinement 2026-10-16): every |Δy| through the fall ≤ 1.1 × the largest steady max |Δy| over a grid of ≥ 21 held f₀ values across the Drop's range **and** the Drop render's own settled pre-fall and post-fall segments (≥ 0.25 s after settle; round-6 refinement: the core's partials leave a Drop as a different waveform from a fresh note's, up to 1.75 apart at the same phase on growl, so a hold of the post-Drop input has steps no fresh-note steady render shows — the stage's f₀ tracking, phasor, bit depth and Tone filter were verified identical). **(e) Null pointer:** with a null pointer and a constant f₀, the output is bit-identical to a render that passes a constant `f0PerSample` array equal to that f₀. | `ShapeCharacter_DropTracksHarmonicNumbers` |
| **SC-026** | Observer hysteresis (FR-036) | Rectify with `setInputHalfWaveSymmetric(true)`, fixtures {`kSineAnchor`, `kTriangleAnchor`}, notes {E1, C3}, Tone ∈ {0, 0.5, 1}. **(a)** Amount swept 0 → 1 then 1 → 0 in steps of 0.005: r steps exactly once per direction, the up-flip Amount is at or above the down-flip Amount, and the up-flip / down-flip Amounts equal the edges of the **intersection band** [max A_lo, min A_hi] of the SC-012(b) bands over sine / triangle × notes per Tone within **0.01**, each edge lying inside every cell's band (C-4 (user ruling 2026-10-15, Clarifications "Plan S1 rulings"). **(b)** With Amount dithered by ±(band / 4) around the flip for 1000 steps, r never toggles. **(c)** `reset()` clears the state: after reset at an Amount inside the band, r reports the value of the side the Amount approached from the default state (r = 1). **(d)** The observer still returns exactly one float, and it is unchanged by `setChannelOffsets`. | `BassCharacter_RLawHysteresis` |
| **SC-027** | Dry share is phase-matched (FR-053) | For every algorithm that mixes a dry share (Rectify per D-5, plus any the plan adds), at **Amount 0.2** (where Rectify's dry share is 0.5; at the drafted 0.5 its wet share is already 1, wet = min(1, A/0.4), so the raw-dry positive control could not comb there; main-loop refinement 2026-10-16, Clarifications "Group 9 rulings"; the 0.5 reading is recorded): with the wet path's nonlinearity set to identity through a test seam, a broadband input (white noise at the calibration level) yields output equal to the input passed through the stage's linear path (`OversampledResidualPath`'s linear decimation, including the one-oversampled-sample ADAA2 alignment delay) alone within **−100 dBFS** residual (C-14), and the magnitude response is flat within **0.1 dB** from 20 Hz to 0.45·fs. A raw-dry (un-round-tripped) mix, the positive control, must show a notch or ripple above 0.1 dB under the same harness. | `BassCharacter_DryShareNoComb` |
| **SC-028** | Tone structure per algorithm (FR-055) | **(a) Neutral:** for each algorithm whose Tone is a tilt, the Tone filter's linear response at Tone 0.5 is within **0.1 dB** of flat from 20 Hz to 0.45·fs. **(b) Keytracked:** the Tone filter's response, as a function of harmonic number n ≤ 16 (n·f₀ < 0.45·fs at both notes), differs between C1 and C3 by at most **1.5 dB** per harmonic at Tone ∈ {0, 0.25, 0.75, 1}. **(c) Fold's Tone is a linear post filter:** for Fold at Amount 1, the output at Tone 0 and Tone 1, each divided by the Tone filter's measured linear response, agree in w_n for n = 1…16 within **0.1 dB** (Tone never alters the fold's symmetry). **(d) Pre-clip placement:** for Tube and Bite, the Tone filter sits before the nonlinearity: with Amount 1, each output's w_n is first divided by the post tilt's measured linear response \|H_post(n·f₀)\|² at that Tone (de-tilted, as (c) does for Fold), then the THD or R_oe at Tone 0 versus Tone 1 differs by at least **3 dB** (OQ-S floor convention). A corroborating arm holds the post tilt at 0.5 through the probe and requires the same ≥ 3 dB. **Negative control:** with the pre tilt forced neutral and the post tilt active, the de-tilted difference must be < 3 dB, so the gate cannot pass on the post tilt alone (C-8). | `BassCharacter_ToneStructure` |
| **SC-029** | Algorithm-transition queue (FR-057, D-7) | During a 10 ms crossfade A → B, requests for C and then D arrive at distinct samples, for A, B, C, D drawn from all six algorithms (sampled orderings, at least 30 ordered triples recorded). **(a)** C is never audible: the output is bit-identical to a reference render where only A → B and then B → D (at the end of the transition in flight: the FR-026 pre-roll plus the crossfade) were requested. **(b)** Through a test seam, the number of simultaneously running algorithm instances is at most **2** at every sample. **(c)** A request equal to the then-current target starts no crossfade; during a crossfade it also clears a queued request (D-7 last-request-wins, C-21), and the orderings C-then-B and C-then-A are included in (a). **(d)** The crossfade in (a) meets SC-009's sample-delta bound and differential click count. **(e)** `BassCharacterStage` exposes no `setAlgorithmBlend` or equivalent (API review, recorded in comply). | `BassCharacter_AlgorithmQueue` |
| **SC-030** | Onset invariance under phasor sync (FR-072) | Crush and Fracture at Amount ∈ {0.5, 1}, Tone 0.5, `kSawAnchor` and `kGrowl`, E1 and C2, 48 kHz: the same note started at 8 sub-period offsets (the fixture shifted by k·T/8, `syncPhase` given the matching core phase) yields descriptors that agree across the offsets: C within **0.005 oct** (Fracture) or **0.02 oct** (Crush: a hysteresis quantiser is path-dependent by one step on a slot whose input sits within ¼ step of a threshold; measured 0.013 / 0.0116 oct on growl at Amount 0.5 with L within 0.07 dB; the per-slot level differences across onsets are recorded; main-loop harness ruling 2026-10-16, round 5), L, E_low and E_high within **0.1 dB**, I within **0.5 dB**; THD and R_oe recorded (main-loop harness ruling 2026-10-16: the SC-006 ε is window-phase noise and reads 1e-8 oct here; THD re h1 is ill-conditioned where Fracture removes h1). For Fracture at Amount > 0 the descriptors are computed on its own **f₀/2 lattice** (period 2T, the SC-011 convention), the integer-lattice values recorded. A silent cell (zero output power) is recorded, never aborts the case; **positive control:** with `syncPhase` not called the spread must exceed those bounds in at least one cell (Fracture Amount 1 is expected to). Steps and bypass-edge click cells of SC-021 give the transition render the same sync as its references. | `BassCharacter_OnsetInvariance` |

---

## Edge Cases

- **f₀ extremes.**
  - f₀ is clamped as in `HarmonicFold::setFundamentalHz`, to [8, 20000] Hz.
  - Keytracked frequencies (D-4) that exceed a filter's ceiling clamp. The clamp is continuous in f₀, with no step at
    the clamp point.
  - At very high f₀ the harmonic multiples exceed Nyquist. The regime then degrades to its clamped form without
    becoming unstable.
- **SHAPE at exactly 0, 0.5 or 1.** Only one regime runs. The neighbouring regime's state is either kept warm or
  reset, whichever makes leaving the anchor click-free (SC-003).
- **Resonance at high f₀ / low fs.** HOLLOW's Q stays ≤ `SVF::kMaxQ`. The regime stays finite at 44.1 kHz with
  f₀ = C3 and maximum resonance.
- **`TubeStage` bypass threshold.** `TubeStage` skips its DC blocker when the smoothed amount is < 0.0001
  (`tube_stage.h:268–273`). Either Tube's Amount mapping never crosses that threshold while the stage is audible, or
  the crossing is shown to be click-free (SC-021).
- **Block size.** The stage's scratch buffers are sized to `maxBlockSize` at `prepare`. The stage splits any longer
  call, so `processBlock` accepts any `numSamples`.
- **Crush hold rate.** Under D-6 the hold rate is m·f₀ at the oversampled rate. When m·f₀ exceeds the processing rate,
  Crush degrades continuously to its bit-depth component alone.
- **Fracture phasor.** The ÷2 phasor (FR-050) is integrated from the f₀ feed. A f₀ change moves its rate, never its
  phase, so there is no step. Bias magnitude is bounded, SC-022 covers the worst case, and `reset()` restores the
  phasor's start phase.
- **Fracture fallback.** Only if FR-050's fallback is taken (`FeedbackDistortion`):
  - feedback above 1 is bounded by its limiter, and SC-022 covers the worst case;
  - the delay is clamped to [1, 100] ms (`kMinDelayMs`, `kMaxDelayMs`). Below 10 Hz or above 1 kHz, a period-locked
    delay leaves that range, and the documented fallback applies: a multiple or sub-multiple of the period.
- **Null or non-finite `f0PerSample`.** A null pointer falls back to `setFundamentalHz` (FR-052). A non-finite entry
  keeps the previous f₀.
- **Amount at the bypass edge.** Amount exactly 0 is the hard-bypass branch (FR-054). Any Amount above 0 runs the
  algorithm, entered by the short crossfade.
- **Switch during a crossfade.** See D-7.
- **`reset()` mid-note.**
  - It clears all filter, oversampler, DC-blocker and crossfade state.
  - Smoothers snap to their targets.
  - Random sources return to their seeds.
- **Sample-rate change.** A new `prepare` at a different rate rebuilds all coefficients. A render after
  prepare(48 k) → prepare(96 k) → prepare(48 k) equals a fresh 48 kHz render (SC-018).
- **Zero-length and null calls.** `numSamples == 0` or a null pointer is a no-op. Processing while unprepared leaves the
  buffer unchanged, following the `HarmonicFold` convention.
- **`right == left`.** One channel is processed, with channel 0's state.
- **Denormals.** FTZ/DAZ is the host's precondition, as for `PolyphaseHalfbandOversampler`. Decaying filter tails may
  rely on it for speed but not for correctness.
- **Seed determinism.** `BitCrusher` dither and `SampleRateReducer` jitter both use xorshift32 state. D-6 disables
  both, so Crush is deterministic without seeds. If the plan keeps either, each channel gets a fixed, documented seed,
  and `reset()` restores it.

---

## Decisions taken where the roadmap is silent (all ratified 2026-10-15)

D-1 to D-12 are ratified as written below, with D-5, D-6, D-7, D-8 and D-10 amended by the clarify session (Q1, Q2,
Q6, Q7, Q8, Q4). Each decision is enforced by the FR / SC rows it cites.

- **D-1 Regime placement.** ROUND sits at SHAPE 0, HOLLOW at 0.5 and BITE at 1. This matches the order in concept §6
  and makes the knob run dark → hollow → aggressive. The plan proposes a default SHAPE value by listening.
- **D-2 Engage control.** SHAPE has no neutral position; the concept describes "one continuously variable SHAPE which
  morphs between them". Two later requirements still need a way to switch it off:
  - Phase 9's capability table needs an on/off for every section (roadmap line 562);
  - the CPU bar is measured "with Shape + Character on" (line 435).

  `ShapeMorph` therefore gains `setEnabled(bool)` with a click-free crossfade. Character needs no flag, because
  Amount 0 is off (FR-022).
- **D-3 Zero look-ahead anti-aliasing.** Every nonlinear path uses `PolyphaseHalfbandOversampler` (IIR, zero
  look-ahead, heap-free) or ADAA, never a FIR oversampler. There are two reasons:
  - roadmap line 706 rules "fixed zero-latency anti-aliasing" for the excitation and the Fold;
  - the Anchor joins after Character on a zero-delay path, so any latency in Shape or Character would offset the
    core's h1 from the Anchor.
- **D-4 Keytracked internal frequencies.** ROUND's cutoff, HOLLOW's centre and BITE's shelf / pivot are set as
  multiples of f₀ through `setFundamentalHz`. "Harmonic 4 is always harmonic 4" (roadmap line 41) then holds through
  Shape. The multiples are functions of SHAPE; for Character, they are also functions of Tone. The alternative, fixed
  frequencies in Hz, would make one SHAPE value colour E0 and C3 differently.
- **D-5 Amount / Tone mapping.** This is a draft; the plan finalises it from measurement.
  - **Tube:** Amount sets `TubeStage`'s input gain and saturation amount; Tone sets a pre-clip tilt (FR-055).
  - **Fold:** Amount sets `WavefolderProcessor`'s fold amount, and the model is fixed (the plan picks it by SC-005 and
    SC-010). Tone sets a post tilt (never the symmetry; FR-055).
  - **Rectify:** Amount sets the wet mix and blend b; Tone sets a post tilt. b and the wet mix are derived values,
    not controls: the stage cannot be placed at an arbitrary (b, wet) pair, so the r law (FR-031, SC-012(a)/(b)) is
    gated over Amount and Tone, and b-grid behaviour is tested on `Rectifier` itself (SC-010, SC-012(c), SC-014).
  - **Bite:** Amount sets the clipper drive; Tone sets the pre-clip tilt.
  - **Crush:** Amount sets the bit depth and the hold multiple m; Tone sets a post low-pass.
  - **Fracture:** per FR-050. Amount sets the bias depth that moves r from 1 toward 0.5; Tone is a post tilt or
    low-pass (FR-055).

  Tone has the same direction for every algorithm: higher Tone gives a higher centroid (SC-006).
- **D-6 Crush is pitch-synchronous, band-limited and noiseless.** **Ratified (Q2); enforced by FR-051.**
  - **Problem.** The roadmap's −80 dBFS aliasing bar (line 433) contradicts a free-running sample-rate reducer. Its
    images at k·f_hold ± n·f₀ are inharmonic and fill the band by design.
  - **Proposal.** The hold rate is m·f₀, with m set by Amount, and the hold runs at the oversampled rate. Every product
    is then a harmonic of f₀, which has three consequences:
    - the aliasing gate stays meaningful;
    - character tracks the keyboard;
    - r ≡ 1 holds by construction.

    Dither and jitter are off.
  - **Implementation consequence.** `BitcrusherProcessor`'s free-running `SampleRateReducer` cannot do this. Crush
    therefore composes `BitCrusher` plus a pitch-synchronous hold inside the stage, with no new class.
  - **Alternative (rejected).** A free-running Crush is not built. SC-010 measures Crush with the same
    `aliasedPower` mask as every other algorithm.
- **D-7 Switch during a crossfade.** A new algorithm request that arrives during a crossfade waits until the current
  crossfade ends, then starts from the then-current algorithm. Only the last pending request is kept, so at most two
  algorithms ever run. The crossfade length is 10 ms; the plan may re-measure it against SC-009. Ratified (Q8): `setAlgorithm` is the only
  transition API and there is no `setAlgorithmBlend` (FR-057, SC-029).
- **D-8 r observer semantics.** r is a function of the centre controls (channel offsets ignored) and, for Rectify, of the hysteresis state at the
  flip point (FR-036). For Rectify it also depends on the input
  half-wave-symmetry descriptor, set through `setInputHalfWaveSymmetric(bool)` (FR-020).
  - The voice derives it from the **whole upstream chain**, not from the recipe alone: Character's input is post-Shape
    and post-Fold, and BITE's asymmetric clipper (FR-014) or an asymmetric excitation adds even harmonics that break
    half-wave symmetry. SC-012(e) tests the observer on that real chain.
  - With no descriptor supplied, the default is `false` (input carries even harmonics, r ≡ 1 for Rectify; FR-030).
    SC-012(a) gates the observer both with and without the descriptor and records what the default costs on a
    half-wave-symmetric input.
  - During a crossfade it reports the incoming algorithm's r; FOLLOW's own 16-sample glide smooths the change.
- **D-9 Output ceiling.** 6.0 linear (+15.6 dBFS), the value of `HarmonicFold::kOutputCeiling` (`harmonic_fold.h:93`).
- **D-10 SHAPE continuity bars (SC-002), ratified.** Per 0.01 of SHAPE: 0.5 dB in level, 0.1 oct in centroid and 1 dB
  in E_low / E_high. Across the sweep: ±2.5 dB on each of the four recipe fixtures (Q6, FR-056; this replaces the
  drafted saw-only ±1.5 dB).
- **D-11 Click bound.** For a crossfade y = g_a·x_a + g_b·x_b, the analytic per-sample bound is
  max\|Δy\| ≤ g_a·D_a + g_b·D_b + \|Δg_a\|·max\|x_a\| + \|Δg_b\|·max\|x_b\|. D_a and D_b are the steady-state max sample
  deltas measured on each branch alone, following the Phase 4 SC-010(b) pattern. The bound is valid only when both
  branches are at fixed settings (steady state), so it gates SC-009 (algorithm switch) and SC-003(c) (engage /
  disengage) with measured / bound ≤ 1. A SHAPE move changes the regimes' own cutoffs, Q and keytracked multiples, so
  its branches are not steady; SC-003(b) uses the Phase 2 SC-009 form instead (≤ 1.1 × the steady-state max delta
  over the SHAPE values traversed).
- **D-12 Offset range for the mono-sum test.** ±0.15 on Amount and Tone, after the roadmap's "small per-channel
  drive/tone offsets" (line 486). Phase 7 rules the real values and re-runs SC-013(b).

---

## Open Questions (deferred to this spec by the roadmap) - resolved 2026-10-15

- **OQ-5 Fracture definition** (roadmap lines 425-426, 890). **Resolved (Q1): period-doubling fracture, FR-050.**
  An asymmetric fold / clip whose bias flips every other period of f0, driven by an internal divide-by-2 phasor
  integrated from the f0 feed; Amount moves r from 1 toward 0.5 (FR-033, SC-024). The `FeedbackDistortion`
  period-locked comb is the fallback only if the definition fails SC-005; a failed bar is never relaxed (FR-070).
- **OQ-S Descriptor-separation bar.** Roadmap line 430 asks for separation ">= the bar ruled in spec".
  **Ratified as written (2026-10-15):** two level-matched renders are **distinct** when at least one of E_low, E_high,
  R_oe, THD or I differs by >= **3 dB**, or C differs by >= **0.25 oct**.
  - **Floor.** Every dB descriptor is clamped to >= -60 dB before differencing, and counts only when at least one of
    the two compared values is above -40 dB re total (THD: re w_1); see "Descriptor floor" in the descriptor
    definitions. `SeparationBar_FloorControl` (SC-005) proves the floor works in both directions.
  - These are the units Phase 3 ratified for its audibility gate (3 dB / 0.25 octave).
  - The bar applies to SC-001 and SC-005, and its unit is used by SC-007.

---

## Traceability

| Roadmap statement (line) | FRs | SCs |
|---|---|---|
| ShapeMorph: ROUND / HOLLOW / BITE behind one SHAPE (417–421) | FR-010–FR-019 | SC-001–SC-004, SC-008 |
| Equal-loudness crossfade (420) | FR-015 | SC-002 |
| Six algorithms, Amount + Tone (422–423) | FR-020–FR-025 | SC-005–SC-008 |
| Rectifier: half/full blend, DC-blocked, anti-aliased (423–424) | FR-001–FR-005 | SC-010 (`Rectifier_AliasingE0E2`), SC-011, SC-012(c), SC-014 |
| Fracture defined in spec (425–426, 890) | FR-021, FR-033, FR-050 | SC-005, SC-012, SC-024 |
| Click-free algorithm switch (426) | FR-026 | SC-009 |
| L/R-capable for WIDE (426–427, 486) | FR-027 | SC-013 |
| Anchor bypasses by default (427) | FR-028 | — (Phase 9) |
| r-law deliverables (427–429; Phase 3 FR-047, FR-066) | FR-030–FR-035 | SC-012 |
| Pairwise distinct ≥ ruled bar (430) | FR-017, FR-025, OQ-S | SC-001, SC-005 |
| Tone monotonic in centroid (431) | FR-024 | SC-006 |
| SHAPE sweep continuous (432) | FR-016 | SC-002, SC-003 |
| Switch sample delta bounded (432) | FR-026 | SC-009 |
| Aliasing ≤ −80 dBFS at E0–E2 (433, 866) | FR-004, FR-061 | SC-010 |
| DC ≤ −80 dBFS (434) | FR-003, FR-062 | SC-011 |
| CPU ≤ 2 % per voice (435, 869) | FR-065 | SC-015 |
| Audibility theme (861) | FR-017, FR-023, FR-024, FR-025 | SC-001, SC-005, SC-006, SC-007, SC-013(c) |
| Mono theme (864) | FR-063 | SC-013 |
| RT safety, portability, shared headers (859–877) | FR-060, FR-064, FR-071 | SC-016, SC-023 |
| Phase 9 Anchor-through-Character clause (571–575) | FR-040, FR-041 | SC-020 |
| Crush pitch-synchronous, noiseless (clarify Q2) | FR-032, FR-051 | SC-010, SC-012(d) |
| f₀ per sample through a Drop (clarify Q3; Phase 4 SC-028) | FR-052 | SC-016, SC-018, SC-025 |
| Observer: one float, offsets ignored, hysteresis (clarify Q4) | FR-030, FR-036 | SC-012(b), SC-012(g), SC-026 |
| Dry share phase-matched; Amount-0 hard bypass (clarify Q5) | FR-022, FR-053, FR-054 | SC-008, SC-021, SC-027 |
| Equal-loudness table on four fixtures (clarify Q6) | FR-015, FR-056 | SC-002 |
| Tone per algorithm (clarify Q7) | FR-024, FR-055 | SC-006, SC-028 |
| Algorithm transitions through `setAlgorithm` only (clarify Q8) | FR-026, FR-057 | SC-009, SC-015, SC-029 |

## Assumptions

- The fixture input level is the core's −12 dBFS RMS calibration, and Amount's drive staging is calibrated to it.
- Phase 9 runs Shape and Character per voice by default (roadmap line 546), so SC-015 is a per-voice figure.
- Phase 9 can supply f₀ per sample through the optional `f0PerSample` pointer (the core's post-Drop f₀ on its
  16-sample cadence, FR-052), and per control block through `setFundamentalHz` when the pointer is null, as it does for
  `HarmonicFold`.

---

## Review notes

Review round 1 (2026-10-15). Every issue was applied; none was rejected. Where a resolution chose between the
reviewer's options, the choice is recorded here.
- **SC-010 attribution (Fold-100 input).** The gate stays the absolute output A ≤ −80 dBFS (roadmap line 433 is
  unconditional). Stage-added aliasing and the input residual are recorded per cell, and a cell that fails only
  because of the input residual must be re-ruled under FR-070. Gating only the added aliasing would have weakened the
  roadmap bar without evidence.
- **SC-014 processed path.** The end-to-end check uses a sine's harmonic levels against the law's Fourier series,
  not a slow ramp, because the DC blocker removes a slow ramp. The 1e-6 bar moved to the `transfer()` tap, where the
  memoryless law actually exists.
- **SC-006 tolerance.** Kept as a tolerance rather than strict ΔC ≥ 0, but derived from a measured noise floor
  (window phase and block size), capped at the drafted 0.005 oct, with the cumulative reversal capped at one
  tolerance. The cap means the measurement can only tighten the bar.
- **Descriptor floor (OQ-S).** Applied to E_low, E_high, R_oe, THD and I. C is in octaves and is a weighted mean
  over all bins, so it needs no dB floor.
- **SC-012 notes.** E0–E2 and C3 follow the Phase 3 SC-008(r) grid (`profundum-phase3-anchor/spec.md:564`), and C2
  is kept for Rectify. The ambiguity cap (≤ 20 %, ≥ 8 unambiguous cells per fixture and note) applies to clauses
  (a), (d) and (e), not only to Crush and Fracture. Clause (f) has only 3–5 cells per fixture and note, so there no
  cell may be ambiguous at all, which is stricter.

---

## Clarifications

### Session 2026-10-15

Every line is the question, the user's decision, and the FR / SC rows that enforce it.

- Q1: Which Fracture definition is ratified (OQ-5)? -> (c) Period-doubling fracture: an asymmetric fold / clip whose
  bias flips every other period of f0, driven by an internal divide-by-2 phasor integrated from the f0 feed. Amount
  moves r from 1 toward 0.5, inside AnchorOscillator's [0.25, 4] clamp. `FeedbackDistortion` is the fallback only if
  (c) fails SC-005; failing a bar is never grounds to relax it. [FR-050, FR-033, FR-070, SC-024, SC-012(d), SC-005, SC-010]
- Q2: Is Crush pitch-synchronous and noiseless (D-6)? -> (a) Pitch-synchronous sample-hold at m*f0 at the oversampled
  rate, noiseless (dither and jitter off), `BitCrusher` plus an inline hold. All products harmonic, SC-010's Crush row
  stands as written, r == 1 by construction. [FR-051, FR-032, SC-010, SC-012(d)]
- Q3: How often do Shape and Character receive f0, and which f0 do they track during a Drop? -> (b) Optional
  `const float* f0PerSample` on `processBlock`, following the core's post-Drop f0 on its 16-sample cadence.
  Keytracked frequencies, the Crush hold and the Fracture period follow it, so the spectrum stays fixed in harmonic
  numbers through a Drop (Phase 4 SC-028). `setFundamentalHz` stays as the per-block fallback when the pointer is
  null. Drop-sweep SCs added. [FR-052, FR-018, FR-050, FR-051, SC-025, SC-016, SC-018]
- Q4: What does `perceivedPitchRatio()` report when L and R differ, and is there hysteresis at the Rectify flip? ->
  (d) One float from the centre Amount / Tone (channel offsets ignored; the L/R mismatch is recorded, not gated),
  with a hysteresis band at the Rectify flip point as wide as the measured ambiguous band. SC-012(b) becomes "within
  the band". [FR-030, FR-036, SC-012(b), SC-012(g), SC-026]
- Q5: How is the dry share mixed against the oversampled wet path? -> (a) The dry share is tapped after the same
  up / down oversampling round trip, so it is phase-matched (no comb). Amount 0 is a separate hard-bypass branch
  (bit-identical) reached through a short crossfade; that edge is added to SC-021 and gated with the differential
  click count. [FR-053, FR-054, FR-022, SC-027, SC-021, SC-008]
- Q6: Which input calibrates ShapeMorph's equal-loudness compensation, and on which fixtures must SC-002 hold? ->
  (b) A static per-SHAPE table calibrated on the mean of the four recipe fixtures; all four fixtures are gated within
  +/-2.5 dB (replaces SC-002's saw-only +/-1.5 dB clause). [FR-056, FR-015, SC-002, D-10]
- Q7: What exactly is Tone for each algorithm? -> (b) with symmetry excluded: pre-clip tilt for Bite and Tube; post
  tilt or low-pass for Rectify, Crush and Fracture; post tilt for Fold (no symmetry-as-Tone). Every Tone is
  keytracked (pivot at k*f0); 0.5 is neutral where Tone is a tilt. [FR-055, FR-024, SC-028, SC-006]
- Q8: What algorithm-transition API does Phase 8 get, and does D-7 stand? -> (a) Timed crossfades through
  `setAlgorithm` only; D-7 stands (the last pending request is queued until the current 10 ms crossfade ends).
  Phase 8 switches at a threshold morph position. At most two algorithms run at once, so SC-015 holds. No
  `setAlgorithmBlend`. [FR-057, FR-026, SC-029, SC-015, SC-009]
- Bars: the user ratified every drafted bar as written. OQ-S distinctness (>= 3 dB on one of E_low, E_high, R_oe, THD
  or I, or >= 0.25 oct on C, with the descriptor floor) [FR-017, FR-025, SC-001, SC-005, SC-007]; D-10 SHAPE
  continuity per 0.01 step 0.5 dB / 0.1 oct / 1 dB, with the level across the sweep as ruled by Q6 [FR-016, SC-002];
  keytracking 1.5 dB per harmonic [FR-018, SC-004]; Tone span >= 0.25 oct [FR-024, SC-006]; D-12 mono-sum within
  1 dB at +/-0.15 offsets [FR-027, FR-063, SC-013]; D-9 output ceiling 6.0 linear [FR-069, SC-022]; D-1, D-2, D-3,
  D-4, D-8, D-11 ratified [FR-011, FR-019, FR-041, FR-018, FR-030, SC-003, SC-009].

### Session 2026-10-15 — Plan S1 rulings (user)

The plan's model (`shape-character-model.js`) measured the ratified bars before any header was written. Seven items change
the scope of a ratified bar on that evidence and were ruled by the user, every one the recommended option; the rest are
refinements accepted by the main loop as following from existing rulings. Each ruling was written into its FR / SC row
first (rows marked "user ruling 2026-10-15"). No bar value was relaxed; where a bar cannot be met on a fixture through the
fixture itself, that fixture is recorded and the bar stands on the others.

- **C-2** — SC-012 ambiguity rule: candidates {f̂/2, f̂, 2·f̂}, ambiguous when best and second-best differ by < 1 dB (the
  3 dB rule left the octave-up side of every Rectify flip ambiguous, ceiling +2.09 dB, and marked every sine cell
  ambiguous through the −1.51 dB f/2 echo) [SC-012, FR-036].
- **C-3** — r-law clauses (a), (d), (e) and SC-024(a) gate only cells whose stage input reads f₀ unambiguously
  (`kGrowl` reads 2·f₀ by itself: +3.4 to +4.1 dB raw, +1.5 dB after Fold 100); clause (f) gates every cell, growl
  included, on r_ref = f̂_out / f̂_in [SC-012, SC-024].
- **C-4** — the observer hysteresis is the intersection band [max A_lo, min A_hi] over fixtures × notes per Tone
  (sine and triangle widths 0.100 vs 0.082 cannot both be matched within 0.01) [FR-036, SC-026(a)].
- **C-5** — Fracture = polarity alternation followed by the bias-flip clip (the clip alone aliased at −69.5 dBFS and
  never flipped growl; the combined law reads −97.9 dBFS and reaches r = 0.5 on all four fixtures); the observer is
  stated for the Fold-output input class with an intersection-band hysteresis, raw fixtures recorded; an empty
  intersection returns to the user [FR-050, FR-033, SC-012(d), SC-024(a)].
- **C-6** — the equal-loudness table is calibrated on saw, triangle and sine and ±2.5 dB gates those three; growl's
  −11.8 … +4.0 dB swing is recorded for Phase 6 and Phase 9 [FR-056, SC-002].
- **C-7** — SC-006's ≥ 0.25 oct span gates `kSawAnchor`; monotonicity gates both fixtures; the sine span (≤ 0.11 oct
  under any first-order tilt on an h1-dominated output) is recorded [SC-006].
- **C-10** — Tube runs at a branch-local extra 2× (8× total), input gain −6 … +12 dB (worst −85.3 dBFS, SC-007
  4.89 dB / 0.40 oct), rather than replacing `TubeStage` [FR-021].
- **Accepted refinements (main loop, follow from existing rulings):** C-1 three helper headers forced by the layer
  rule [New components]; C-8 Tube / Bite pre tilt plus a coupled post tilt, SC-028(d) de-tilts with a negative control
  [FR-055, SC-028]; C-9 Fracture aliasing gated on the n·f₀/2 lattice, the re-rule SC-010 pre-authorised [SC-010];
  C-11 ADAA2 in double, ε = 1e-5; C-12 Fold θ_max = 2.5 rad; C-13 reused-processor choices; C-14 SC-027 reference is
  the stage's linear path [SC-027]; C-15 group-delay record [FR-040]; C-16 crossfade laws and lengths; C-17
  per-channel bypass; C-18 observer reads targets; C-19 Rectify composes `Rectifier`'s laws inside the stage
  [FR-021]; C-20 SC-015 gates the unlinked cell as the single pinned case, T001 measures feasibility first and the bar
  is never the lever [SC-015]; C-21 a request equal to the target cancels a queued request [SC-029(c)]; C-22
  `ShapeMorph`'s processed path runs warm while disabled [FR-019].

### Session 2026-10-15 — T001 CPU bench ruling (user)

T001 measured the plan's unlinked Shape + Character cell before any header was written (pinned, median of three, 48 kHz,
both channels processed; `evidence/run-log/T001-bench.txt`): worst Character + worst Shape = **4.40 %** base, 4.67 % with
16-sample control quantisation, 4.44 % with float Rectify ADAA, against the 2.0 % bar; Tube at the ruled 8× (C-10) reads
2.01 % unlinked by itself, Fracture 2.65 %, Shape 0.25 (ROUND + HOLLOW) 1.74 %. The plan's two levers do nothing, so per
C-20 the item returned to the user.

- **Ruling (recommended option):** SC-015 gates the **mono-linked** cell, the same per-voice cell Phases 1–4 gated
  (Phase 2 precedent), with the 2.0 % bar unchanged; the unlinked cell is recorded in the same case and handed to Phase 7
  (which forces unlinking through WIDE offsets and must budget it) and Phase 9. Plan C-20's unlinked reading was the plan's
  own choice, not a user ruling, and the roadmap's Phase 9 arithmetic (per-voice ≈ 4.8 %) is built on linked figures
  [SC-015, FR-065].
- **With it, two optimisations enter the plan** (C-23, C-24) and T001 re-benches (epoch 2) before any header: Fracture's
  phasor-only functions (sin(πφ), σ(φ), tanh(g·β·sin(πφ))) leave the per-oversampled-sample transcendental calls; and every
  stage's linear parts (pre-tilts, post-tilts, ROUND's low-pass, HOLLOW's notch and band) run at the base rate on the
  phase-matched round-tripped signals, with only the nonlinearity at the oversampled rate. T001 proceeds only if the
  linked cell reads ≤ 2.0 %; otherwise the item returns to the user again. Nothing else was relaxed.

### Session 2026-10-15 — Group 2 task-level tolerances (main loop)

Two executors measured their designs before writing and found task-level test tolerances (set by the plan, not by any FR/SC)
below the designs' numerical floor: `ClosedFormAdaa2` (2 ulp against a float reference that cancels near 0; 1e-6 on a slow
ramp where F2 ≈ 51 / h²) and `KeytrackedTilt` (0.01 dB pivot unity and 0.05 dB keytracking from a matched-pole one-pole;
a symmetry check over a range not centred on the pivot). The main loop set those tolerances from the measured designs
(plan C-25, tasks T003 / T004 epoch 2). No FR/SC bar changed: SC-028(a) (0.1 dB at Tone 0.5) and SC-028(b) (1.5 dB per
harmonic) stand and pass with margin.

### Session 2026-10-16 — Group 3 gate rulings (main loop)

The Rectifier arm of SC-021 (`Rectifier_BlendStepClickFree`) went red on the harness, not the component; the fixer
diagnosed and stopped. Two measurement-scope refinements of Phase 4's ruling B, made by the main loop and recorded in
the descriptor definition above (the bar, zero counted detections, is unchanged):
- **Two references.** Every counted detection in the 1 → 0 cells lay ≥ 5000 samples after the step and coincided with a
  detection in a steady render held at the end setting (the detector's frame-relative 5σ test fires on the half-wave
  output's flat regions). A detection now counts only if neither the start-setting nor the end-setting held render
  has one within ±N. `differentialClicks` gained the three-render form; the one-reference form remains.
- **Positive control per arm and note.** C2 `kGrowl` is pulse-like; every candidate snap sample either sits on an
  edge the reference also flags or is too small to detect. The spec asks for the control per SC, so it is gated per
  SC arm and note (≥ 1 fixture flagged at each note), every cell recorded. SC-003, SC-009 and SC-021 inherit both.

### Session 2026-10-16 — Group 6 gate ruling (main loop)

`BassCharacter_CrushHoldRing` (b), the plan's 0.5 s Amount-ramp unit case on the pitch-synchronous Crush, counted five
detections under the two-reference form. The fixer rendered the stage held at the Amount the ramp was passing through at
each of the five samples and found each one's own detection within ±N, with the ramp's local max |Δy| inside the held
renders' range (steady Crush at intermediate Amounts carries 100–700 staircase detections per 1.25 s). A first attempt
with local grid references counted eight: a hold's staircase positions depend on the exact Amount, so no held render is
phase-aligned with the ramp and the detector count is ill-posed for a long ramp. Ruled (measurement-scope refinement of
ruling B; the SC rows' step and 20 / 50 ms ramp cells are unchanged): long ramps are gated by the sample-delta bound in
the SC-003(b) form over a 21-point grid of the traversed settings, and the count is recorded.

### Session 2026-10-16 — Group 8 gate rulings (user)

The six test TUs (Group 8) found four results that sit on ratified bars or laws; the fixer repaired four other reds at
the root (Fracture's NaN setter re-keyed the rotor; Crush's hold grid sampled on the waveform edge, now at (k + ½)/m, which
fixed the 48/96 kHz level, Drop tracking and DC reds; the positive-control step placement). The user ruled, each the
recommended option; every ruling is in its row above:
- **SC-009 level window** → [min − 2.5 dB, max + 1.5 dB], the fixed-law floor, midpoint excursion recorded per pair; no
  level follower on the switch path [SC-009].
- **SC-013(b)** → ±0.15 gated on Tube, Fold, Bite and Crush; Rectify and Fracture gated at their per-algorithm offset
  ceiling on {0.05, 0.10}, the ±0.15 reading recorded, ceilings handed to Phase 7 [SC-013, FR-027, Phase 7 handoff].
- **SC-028(d) Bite** → design lever, bar stays: Bite's pre-clip tilt ±6 → ±9 dB; SC-010 (E2 / 44.1 kHz margin was
  3.3 dB), SC-006 and SC-007 re-measured on it; a failure returns to the user [FR-055].
- **Crush law** → the conditioned grid stays and the hold multiple reaches m = 2 at Amount 1 (m = 64·32^−A); SC-007,
  SC-010, SC-012 and the Crush CPU cell re-measured [FR-051].
Recorded for the close lane (T025): `ShapeCharacter_DropTracksHarmonicNumbers` (SHAPE 0 cell 2.0 dB at h15),
`ShapeCharacter_ParameterStepsClickFree` (Crush bypass-edge counts 1–2) and `BassCharacter_ToneMonotonicCentroid`
(Crush / Fracture harness ε up to 1.96 oct) were red in the fixer's side run of the [long] cases; T022 / T025 own them.

### Session 2026-10-16 — Crush threshold flicker (user)

With m = 2 the SC-011 cell Crush / Tone 0 / E2 / Fold-100 `kGrowl`
read −65.7 dBFS. Diagnosis (implementer, probe at the
hold output, before and after the 2 Hz blocker): not DC but an 8.06 Hz two-level modulation at −46 dBFS with harmonics at
16, 24 and 34 Hz; the period mean alternated between two levels 0.0264 apart (half a 4-bit step on one of two hold points)
in bursts every 10.3 periods, the cycle of the hold point's sub-sample position (period 2329.90 oversampled samples). A
hold point within interpolation error of a quantiser threshold flickers. One cell of 36 today; luck at any m. Ruled
(recommended option): the quantiser gets per-slot hysteresis of ¼ step; noiseless stays [FR-051]. Cubic interpolation
(rarer, still flickers) and re-ruling SC-011 (hides the artefact) were rejected.

### Session 2026-10-16 — Group 9 rulings

- **SC-027 Amount (main loop):** the row named Amount 0.5, where Rectify's wet share is already 1 (wet = min(1, A/0.4)),
  so the raw-dry positive control could not comb; the row now gates at Amount 0.2 (dry share 0.5) and records 0.5.
- **FR-072 / SC-030 phasor sync (user, recommended option):** Crush's hold phasor and Fracture's ÷2 phasor free-ran from
  phase 0 at every branch start, so the held / flipped part of the period depended on onset alignment (Fracture centroid
  up to 1.97 oct, Crush level up to 5.6 dB between onsets of the same note). `BassCharacterStage::syncPhase(coreCycles)`
  locks them to the core's h1 phase; the voice calls it at note-on, retrigger and bypass exit (Phase 9 handoff); SC-030
  gates onset invariance with a positive control; SC-021's bypass-edge cells sync the transition render like its
  references. Alternatives rejected: accept-and-record (timbre would vary note to note), audio-derived lock (fragile on
  folded inputs; growl reads an octave up).
- **SC-025 (Drop tracks harmonic numbers, [long]):** 456 failed assertions at epoch 1 including SHAPE 0 ROUND cells (growl
  16.8 dB at h8). Diagnostic run ordered before any ruling; not yet ruled.

### Session 2026-10-16 — Group 9 rulings, round 2

Measured after the phasor sync (implementer, `F:/tmp/p5g9b`):
- **Crush m_min (user):** m = 2 silences an even-dominated growl at every synced onset (the ¼ / ¾ hold points are the zeros
  of every even partial); m = 2.5 blends the 2- and 3-point holds (saw 5.75 dB / 0.46 oct, growl −38 dBFS, DC −114, A −117
  dBFS), m = 3 passes the saw by 0.3 dB only. Ruled m_min = 2.5 (m = 64·25.6^−A) [FR-051]; the growl's drop at full
  Amount is a Phase 9 listening record.
- **SC-021 (user):** after the sync the remaining Crush counts are the staircase steps of the intermediate m the 10 ms
  smoother passes through (|Δy| 0.02 against 0.0016 / 7e-5 in the held references; hysteresis off changes nothing); a Bite
  Tone step and the Crush bypass edge counted detections whose steps equal the references' local steps. Ruled: the
  differential count gains an amplitude guard (1.1 × the references' local max |Δy|), and Crush's SC-021 cells are
  gated by the sample-delta bound over the traversed grid with the count recorded [Differential click count, SC-021].
- **SC-025(a) (user):** the core's partials above h14 leave a Drop with different phases from a fresh note (input
  magnitudes match within 0.003 dB; h16 off by 3.3 rad), so a nonlinear stage's per-harmonic transfer against a steady
  render is ill-posed (ROUND 16.8 dB at h8 on growl 0.4 s after the pitch settled; not a floor or frame artefact: five
  orders above the floor, frames checked). Ruled: (a) gates the keytracked coefficients directly through the probes
  (0.1 %) and keeps the 1.5 dB audio bar for the all-linear HOLLOW regime; nonlinear |H(n)| recorded. Fixing the core's
  post-Drop phases (Phase 1 / 4 territory) and loosening the bar were rejected [SC-025].
- **SC-030 (main loop):** fixed descriptor bounds (C 0.005 oct, L / E_low / E_high 0.1 dB, I 0.5 dB; THD, R_oe recorded),
  Fracture read on its f₀/2 lattice, silent cells recorded not aborting. Before this, Fracture Amount 1 showed E_low
  spreads of 1–2 dB on the integer lattice while C, L, E_high and I agreed; the positive control flagged every cell.

### Session 2026-10-16 — Group 9 rulings, round 3 (main loop)

After round 2 the SC-025 [long] case passed clause (a) everywhere (coefficients exact; HOLLOW 0.096 dB) and failed (c) on
Crush in 56 cells (I −34 … −38 dB during the Drop against a floored steady −60 dB) and (d) on Crush (counts 2–4) plus
three positive controls. Measurement-scope refinements of the user's SC-025(a) and SC-021 Crush rulings: (c) gates the
feed-following mechanism directly through the probe (phase increment and hold multiple at every sample of the instants'
frames, 0.1 %), I recorded; (d) Crush gated by the sample-delta bound over a 21-point grid of held f₀ values, count
recorded. The positive controls are harness construction (the switch sample must sit where the forced discontinuity
exceeds the local steady steps, as the Rectifier control does); they are repaired, not re-ruled.

### Session 2026-10-16 — Group 9 rulings, round 4 (main loop)

SC-030 (onset invariance) caught the hysteresis quantiser latching a warm-up level: on C2 growl at Amount 1 one onset in
eight held 0 instead of −0.0526 in one slot, the slot's steady input (−0.0304) sitting 0.58 step from the latched 0, so the
¾-step band never released it. The hysteresis now compares the new held value with the held value at which the slot
last quantised (v_ref), not with the stored level [FR-051]. The ShapeMorph hard-regime positive control at E2 growl is
re-sited like the chain test's controls (largest forced step at least 2N from any reference detection). SC-025(d)'s
Crush delta ratio on growl (1.63 / 1.99 at Tone 0.5 / 1) is under diagnosis: where the largest Drop step sits relative to
the Drop's onset pitch jump decides whether it is the jump (a note-on event, Phase 4's declick domain) or the fall.

### Session 2026-10-16 — Group 9 rulings, round 5 (main loop)

Round 4 removed the warm-up latch (C2 growl Crush Amount 1: 1.52 oct → 2e-8 oct) and re-sited the ShapeMorph positive
control (flags at every note with the guard on). Two Crush growl cells at Amount 0.5 keep a C spread of 0.013 / 0.0116 oct
against the drafted 0.005 oct, with L within 0.07 dB: one quantiser step on one slot whose input sits within ¼ step of a
threshold, the path dependence any hysteresis has. The SC-030 C bound for Crush is set to 0.02 oct with the per-slot
differences recorded (the 0.005 figure was drafted from the SC-006 cap, not measured). The SC-025(d) Crush delta ratios on
growl (1.55 / 1.98 at Tone 0.5 / 1; largest steps spread through and after the fall, none at a pitch jump) are under
diagnosis against the steady E2 render.

### Session 2026-10-16 — Group 9 rulings, round 6 (main loop)

SC-025(d) Crush growl: after the fall the Drop render never settles to the steady E2 render because the core's own output
differs (up to 1.75 at the same core phase; stage state identical: m_eff 2.5, θ, bits, pivot). Crush's hold of that
waveform makes steps of 0.19 / 0.26 that no fresh-note steady render at any grid f₀ reaches (top point 0.124 / 0.133). The
Crush delta bound now also takes the Drop render's own settled pre-fall and post-fall staircases as steady references.
**Core finding handed to Phase 9 (and Phases 1 / 4 if reopened):** a note that has been through a Drop has partial phases,
hence a waveform and crest factor, unlike a fresh note at the same pitch; every waveform-dependent stage downstream (Fold,
Shape, Character) therefore sounds different after a Drop than without one. Not a Phase 5 defect; a Phase 9 listening item
and a possible Phase 4 amendment (phase-coherent partials through a Drop).

### Session 2026-10-16 — Group 9 rulings, round 7 (user)

- **Crush m_min 2.5 → 3 (user, recommended option):** the Group 9 gate failed SC-005 on Tube vs Crush, `kSawAnchor` E1 and
  C2 (1.24 dB / 0.065 oct, 1.09 / 0.052; bar 3 dB or 0.25 oct); the other 58 pair-cells passed. Round 2 ruled m = 2.5 on
  SC-007 alone and SC-005 was not re-measured. At A = 1 the 2.5 law blends the 2-point hold (even partials null) and the
  3-point hold (h3, h6 null) half and half, so neither null survives and Crush's saw (h1..h5 re total −0.7 −12.2 −16.0
  −18.2 −25.2 dB) matches Tube's saturated saw (−0.8 −12.3 −14.5 −17.2 −19.2). The gate's probe passes SC-005 at m = 2, 3
  and 4 and fails only at 2.5. Round 2's own table at m = 3: SC-007 saw 3.30 dB / 0.15 oct (sine 56.6 dB), no silent cell,
  raw growl E1 −32.1 dBFS, DC −112.0 dBFS, aliasing −112.0 dBFS. Ruled m_min = 3, m(A) = 64·(64/3)^−A [FR-051]; SC-007's
  0.3 dB saw margin is recorded; the Group 9 gate re-measures the tree. Alternatives rejected: lower bits at A = 1 with
  2.5 kept (unmeasured, five SCs to re-measure); another non-integer m (the same blend; 3.5 sits between 3.3 and 1.3 dB
  on SC-007); an SC-005 ruling for the pair (bars are not relaxed).

### Session 2026-10-16 — Group 9 rulings, round 8 (user)

- **Switch / bypass-exit pre-roll (user, recommended option):** the first run of `BassCharacter_SwitchClickFree`
  (SC-009 [long], 60 cells) failed every switch **into Rectify** at E1 and C2 by ≈ 4.5 dB over the +1.5 dB level window
  (L_tr max −6.3 dBFS against −10.8 steady) and C2 Rectify → Crush by 0.07 dB under the −2.5 dB floor; 49 cells passed.
  Diagnosis (main loop, cold-start render of each branch): Rectify at Amount 1 on `kSawAnchor` carries a residual DC of
  0.43 at its first period, decaying with the 2 Hz blocker's ≈ 80 ms time constant (period means 0.43, 0.32, 0.23, …;
  one-period level +5.1 dB at the start, still +0.7 dB after 8 periods); the incoming branch is cold-started by S5.3
  step 1, so the thump reaches the output through the crossfade, and the same happens when the Amount-0 bypass is left.
  Crush's cold start is its empty first hold period (−1.1 dB for one period); Tube, Fold, Bite and Fracture settle
  within a period. Ruled: `start(a)` and bypass exit run the incoming / running branch **unmixed for one period of f₀**,
  prime its residual DC blocker to the measured residual mean (`DCBlocker::setState`, `OversampledResidualPath::primeDc`),
  and only then crossfade (FR-026, FR-054 amended; SC-009 and SC-029 measured from the request through the crossfade
  end). A switch now takes 1/f₀ + 10 ms, a bypass exit 1/f₀ + 5 ms (Phase 9 handoff). Rejected: a fast-charging blocker
  (ripples at f₀; cannot average a 24 ms period inside a 10 ms crossfade) and keeping all six branches warm (CPU ×6
  against the 2 % cell). New case `BassCharacter_SwitchPreroll` gates the pre-roll's bit-identity and the primed level.
  Measured after the ruling (main loop, `BassCharacter_SwitchClickFree`, 60 cells): all pass, 2431 assertions; worst level
  excursion +1.13 dB over the steady maximum (bar +1.5) and −2.15 dB under the steady minimum (bar −2.5); every
  differential click count 0.
  **Seams (main loop):** the hard-switch seam (`seamHardSwitch_`) and the hard-bypass seam (`seamHardBypass_`), SC-021's
  positive controls, switch the whole click-free law off, pre-roll included; with the pre-roll left on, the hard bypass
  snapped onto a warmed branch and at Amount 0.05 Tube, Fold and Bite at C2 produced a step too small to count.

### Session 2026-10-16 — Group 10 close rulings

- **SC-012(f) on `kGrowl` (user, recommended option; R-12 / S6.5):** the first full [long] grid passed every non-growl
  cell and failed 71 of 72 `kGrowl` cells through Tube / Fold / Bite and 27 of 40 through `ShapeMorph`, all at
  −1200 cents: the raw growl reads 2·f₀ by +2.6 dB (Fold-100 +1.8 dB) and the processed output reads f₀ (0.1–3.9 dB
  the other way). The stages add f₀-series salience to an even-dominated input; nothing removes h1. Ruled: C-3's
  input gate extends to clause (f); growl cells are recorded in comply with f̂_in / f̂_out; the finding goes to the
  Phase 9 FOLLOW row (FOLLOW takes r from the engine, Q1). Rejected: the S6.5 observer law for growl-class inputs
  (needs the voice to classify the input, three new APIs, and would send a FOLLOW anchor an octave below the core's
  h1) and changing the fixture (a ratified Phase 1 anchor).
- **Fracture observer edges (user, recommended option; R-6 / C-5):** the harvest's Fold-100 bands per Tone: sine and
  triangle agree within 0.03 everywhere, the saw flips earlier (Tone 0.75: saw [0.25, 0.40], sine [0.42, 0.55], triangle
  [0.45, 0.57]), so the ratified three-way intersection is empty at Tone 0.75 and 1. Ruled: edges = sine ∩ triangle per
  Tone over the notes; saw recorded in SC-012(d) and SC-024(a); FR-033 / FR-036 amended. The observer never flips before
  any fixture's audio has; on a saw-like voice FOLLOW lags Fracture by up to 0.17 Amount at Tone ≥ 0.75 (anchor stays at
  f₀, the safe failure; Phase 9 record). Rejected: the saw band (FOLLOW would drop an octave before a sine-like voice
  does) and the union band (contradicts flipped saw cells by construction). Main-loop refinement under C-4: the harvest's
  0.01 across-note spread check stays a stop for Rectify and is recorded for Fracture (sine / triangle spread up to two
  bisection steps, 0.016; the intersection over notes keeps every edge inside every gated band).
- **Rectify descriptor rule (user, recommended option; SC-012(e), FR-031):** the chain case failed 25 triangle cells with
  descriptor `false` (premise: even harmonics present when w₂ ≥ −40 dB; BITE leaves h2 at −19.9 dB) while the audio
  doubled at Amount ≥ 0.8. Probe (triangle + h2 at set levels, Rectify Amount 1): 2·f₀ at h2 ≤ −22 dB (Tone 0.5, 1) /
  ≤ −26 dB (Tone 0), f₀ at ≥ −14 dB, ambiguous between. Ruled: the voice supplies `true` when h2 re h1 is below −20 dB;
  −24 … −16 dB is the documented uncertainty band; SC-012(e) applies the rule and records in-band cells. Rejected: a
  continuous descriptor API (same threshold, one more API and a per-block Phase 9 measurement) and leaving `false`
  with the cells recorded (leaves the −40 dB premise wrong).
- **FR-064 fallback for `dc_blocker.h` (user, recommended option):** the pre-roll prime added the append-only
  `DCBlocker::setState`; FR-064's fallback clause applies (consumer suites green), recorded in the row. Rejected:
  reverting and duplicating the one-pole inside the residual path.
- **SC-003 handover click (main loop, defect fix, C-32):** the first full [long] run of `ShapeMorph_AutomationClickFree`
  failed 9 saw cells at E0 and E2 (steps and 50 ms ramps, both directions): a one-sample step of 0.088 at the
  s = 0.5 handover (3× the steady grid's max |dy| of 0.030 at E0). Per-sample probe: the regime weight was applied
  before ROUND's low-pass and before the shared residual path, so a leaving regime's filter memory (ROUND's low-pass
  tail 0.083 while its weight was already 0.006; BITE's residual through the 2 Hz blocker, 0.058) was cut in one
  sample when the regime switched off, and HOLLOW's emphasis inverse was dropped with BITE (hollowOut +17 % in one
  sample). Fix: weights applied after the shared path and ROUND's low-pass; HOLLOW's inverse runs whenever HOLLOW
  is on, keyed flat (unity) while BITE is off, and is not reset at BITE entry. After that, 6 E0 saw cells still
  counted one detection 3–7 samples after the step: HOLLOW's tap chain was reset on entry and filled from zero in
  three samples at 2–8 % weight. HOLLOW's linear chain now runs warm from prepare and is never reset on entry
  (reset-on-entry stays for ROUND and BITE). No bar changed.
- **SHAPE ramp law (user, recommended option; C-33):** with the handovers fixed, 3 E0 saw cells still counted one
  detection at the first samples of a 1 → 0 move: the plan's `OnePoleSmoother` (99 % in 10 ms) has its steepest slope
  (1 / 104 per sample) at the onset, and BITE and HOLLOW differ by 0.32 at that instant. Ruled: a restartable 10 ms
  raised-cosine ramp (zero slope at both ends; a new target restarts from the current value). Measured: the 50 ms-ramp
  cell passes, the SC-003(b) ratio drops from 0.70 to 0.24, SC-002 (35335 assertions), the ShapeMorph fast set and the
  chain [long] cases stay green. Rejected: keeping the one-pole; a linear ramp (C⁰ corners, unmeasured).
- **SC-003(a) single-step cells (user, recommended option):** on the ramp build two cells stayed red, E0 saw 1 → 0 single
  step at blocks 64 and 512, one detection each, (b) passing at ratio 0.24. The 10 ms crossfade itself moves the waveform
  0.32 in 10 ms (BITE −0.07 vs HOLLOW +0.25 at that instant); its slope term exceeds the guard built from E0's near-flat
  references. Ruled: for single-step cells a detection inside the ramp window is recorded and (b) gates, as for Crush's
  SC-021 cells; detections outside the window still count; positive controls unchanged. Rejected: a law-aware guard
  (allowance within 20 % of the measured dy, unmeasured) and a longer SHAPE ramp (re-rules C-16's 10 ms for a 20 Hz corner).
