# Feature Specification: Profundum Phase 6 — Mass & Dynamics

**Spec slug:** `profundum-phase6-mass-dynamics`
**Status:** Clarified (2026-10-17). Q1-Q8 and D-3 / D-6 / D-7 were ruled or ratified by the user and are encoded in
the FR / SC rows below and logged in Clarifications. A bar still marked *drafted* has not been ruled and is calibration
or plan territory. Once a bar is ruled, the ruling goes into its FR / SC row first and then into Clarifications.
**Roadmap source:** `specs/Profundum-roadmap.md` → Part A → **Phase 6** (lines 463–486). This spec also traces to:
- **Architecture Overview**, Global block. Line 95: "MASS transient · compression (sub-safe sidechain) · law". Line 97:
  "OUTPUT DC safety · true-peak limit". Line 109: "the Compressor is the Mass dynamics block".
- **Core Philosophy.** Line 49: "Concepts over parameters. Mass, Shift, Shape, Spread … are the surface". Line 54:
  "Nothing ships inert".
- **Reuse Inventory.** The "Dynamics" row (line 166) lists `dynamics_processor.h`, `true_peak_limiter.h`,
  `envelope_follower.h`, `sidechain_filter.h` and `TransientDetector` ("a mod source, not a shaper"). Its verdict:
  "✅ compressor/limiter. 🆕 `TransientShaper` (L2) — none exists." The "Output safety" row (line 171) lists
  `true_peak_limiter.h` and `dc_blocker.h` as "✅ direct reuse". Three facts in these rows are wrong; see
  "Reuse-inventory corrections".
- **Phase 8 consumer** (line 531). `MorphState` holds "Mass" as a morphable field.
- **Phase 9 consumer** (lines 576, 580, 582, 590–593):
  - the engine chain is "voice sum → Mass dynamics → stereo field safety stage → DC safety → `TruePeakLimiter`";
  - "Motion can modulate Mass";
  - Tight ↔ Loose drives "envelope, transient, compression";
  - the globals take about 1.5 % of the whole-engine budget;
  - "Motion → Mass swell measured".
- **Phase 11 consumer** (lines 628–629). Parameter IDs 100–199 hold Mass; 700–799 hold the mass dynamics.
- **Phase 13 consumer** (line 860): "true-peak check at maximum Mass".
- **Cross-Cutting Constraints** (lines 881–901):
  - RT safety;
  - the audibility, mono-compatibility and anti-aliasing theme FRs;
  - "CPU budgets are FRs … global 6–7" (line 893);
  - no bit-exact float goldens;
  - portability;
  - shared-component changes are append-only.

**Upstream contract consumed (Phase 3 handoff, ratified).** From `specs/profundum-phase3-anchor/spec.md`:
- **FR-022** (line 312). The Anchor `level` "is the single control Phase 6 (Mass, 'sub/body relationship (anchor
  level)') and Phase 8 … drive. No other user-facing gain path is exposed."
- **FR-023** (lines 315–321). The voice calls `setCoreH1Amplitude(float a1)` once per control block. a₁ is the peak of
  the core's h1 sine component, clamped to [0, `kMaxCoreH1Amplitude`].
- **FR-025** (lines 328–332): "nothing auto-compensates the core or normalises the combined signal … The summed signal is
  validated later through Phase 6's Mass dynamics and the output safety stages."
- **FR-066** (lines 534–536) wrote that clause into roadmap lines 478–481. This spec owns it.

**Concept source:** `specs/Profundum-concept.md`.
- **§4 "Mass"** (lines 208–256):
  - "This isn't volume. It controls several psychoacoustic aspects simultaneously: fundamental strength · low-mid
    harmonic density · transient duration · saturation · compression · sub/body relationship";
  - "MASS 0 pure, clean, sine-like … MASS 50 thick analogue bass … MASS 100 enormous distorted bass";
  - "continuous and predictable";
  - "I want 20% more mass."
- **§9 Tight ↔ Loose** (lines 448–450): "Envelope, transient and compression behavior."
- **§11** (line 558): "Motion can modulate *Mass*."
- **§13** (lines 601–627) orders the chain CHARACTER → COMPRESSOR → STEREO FIELD → LIMIT.

**Layer.** Two components are named by the roadmap. Both are header-only in `Krate::DSP`:
- `TransientShaper` (L2, `processors/transient_shaper.h`);
- the Mass law (L3, `systems/mass_law.h`).

The bass compressor is a *configuration* of the existing `DynamicsProcessor` (roadmap line 472), not a new
compressor. Every new name passes the ODR sweep (see "New components").

---

## Overview

Phase 6 builds the one control that lets a user think "20 % more mass": a single 0–100 **Mass** value. A documented,
calibrated, monotonic law resolves Mass into six targets spread across the voice and the global chain:

| Mass target (roadmap lines 474–477) | Driven component | Lives in |
|---|---|---|
| fundamental strength | `SpectralShapeRecipe::Controls::depth` bias | voice (core) |
| low-mid harmonic density | `SpectralShapeRecipe::Controls::body` bias | voice (core) |
| transient duration | `TransientShaper` duration | global |
| saturation | `HarmonicFold::setAmount` | voice |
| compression amount | the bass `DynamicsProcessor` configuration | global |
| sub/body relationship | `AnchorOscillator::Controls::level` | voice (Anchor) |

Two global processors carry the dynamics half of Mass:
- **`TransientShaper`** (new) shapes attack gain and sustain gain from a fast / slow `EnvelopeFollower` pair. KrateDSP
  has no transient shaper; `TransientDetector` is a modulation source, not a gain stage.
- **The bass compressor** configures `DynamicsProcessor` for bass:
  - a sidechain high-pass, so the sub never drives the gain computer;
  - a program-dependent release;
  - one gain shared by L and R;
  - zero latency.

Phase 6 also closes the Phase 3 handoff. The core's h1 amplitude reaches the Anchor once per control block. The summed
core + Anchor signal is validated through the Mass dynamics and the output safety stages (`DCBlocker`,
`TruePeakLimiter`). Nothing inside the Anchor compensates for it.

## Scope

1. `TransientShaper` (L2), complete. It covers:
   - attack gain, sustain gain and transient duration, derived from a fast / slow detector pair;
   - one gain shared by L and R;
   - bass-safe detection, with no waveform-rate gain ripple at E0;
   - the lifecycle and non-finite safety.
2. The **bass compressor configuration** of `DynamicsProcessor`. It covers:
   - the sidechain high-pass and the program-dependent release;
   - one gain shared by L and R, and zero latency;
   - a "compression amount" control that the Mass law drives.
3. The **Mass law** (L3, `systems/mass_law.h`). It covers:
   - the documented, monotonic, calibrated six-target map;
   - bounded slew;
   - a pure, stateless evaluation that Phase 8's morph and Phase 9's Motion → Mass can both use.
4. The **validation-chain** test fixture: core + Anchor (a₁ fed from the core) + Fold at the Mass targets → Mass
   dynamics → `DCBlocker` → `TruePeakLimiter`.
5. The two bars the roadmap leaves to this spec:
   - the perceived-level range of the Mass sweep (ruled, OQ-1: FR-028, SC-003);
   - the bound on sub-band gain modulation, i.e. pumping (ruled, OQ-2: FR-019, SC-006).
6. Tests for:
   - target monotonicity, level slope, pumping, transient gains and automation continuity;
   - audibility, mono-sum, anti-aliasing and DC;
   - RT safety, non-finite safety, determinism, sample rates and CPU.

## Non-Goals (owned by later phases, or deliberately excluded)

| Excluded | Owner | Roadmap |
|---|---|---|
| Wiring Mass into `ProfundumVoice` / `ProfundumEngine`. This includes the engine's global chain order and POLY summing into the global dynamics | Phase 9 | 563–576 |
| Motion → Mass routing and the swell measurement | Phase 9 | 577–580, 593 |
| The Personality macros (Tight ↔ Loose driving transient and compression) and `ProfundumMacroMatrix` | Phase 9 | 581–583 |
| Mass as a `MorphState` field, and its interpolation law | Phase 8 | 523–548 |
| The stereo-field safety stage between Mass dynamics and DC safety | Phase 7 | 490–517 |
| Parameter IDs (100–199 Mass, 700–799 mass dynamics), names, normalisation, state and `IMidiMapping` (mod wheel → Mass) | Phases 10, 11 | 628–629, 641–647 |
| The whole-engine CPU budget and the 30 min soak | Phase 9 | 590–595 |
| The preset-level true-peak check at maximum Mass | Phase 13 | 860 |
| UI (the Mass knob, a gain-reduction meter) | Phase 12 | — |
| A sound-shaping seventh Mass target, a multiband compressor, look-ahead dynamics, or a user-facing quality switch | Never (the calibrated output trim of FR-021, added 2026-10-17 by user ruling G8-5, is a level-neutrality calibration, not a sound-shaping target). The roadmap lists six targets (lines 474–477), and the instrument is zero-latency with no quality switch (lines 731–732; the bank's zero latency, line 161) | — |
| Changes to `ProfundumCore`, `AnchorOscillator`, `HarmonicFold` or `SpectralShapeRecipe` | None planned (FR-044) | 900–901 |

---

## Existing components (verified this session, 2026-10-17)

Paths are relative to `dsp/include/krate/dsp/` unless stated. Every row was opened and read this session.

| Component | Header | Reused for | Verified signature / fact |
|---|---|---|---|
| `EnvelopeFollower`, `DetectionMode` | `processors/envelope_follower.h:82, 41` | `TransientShaper`'s fast / slow detector pair. The compressor's detector, inside `DynamicsProcessor` | Layer 2 (line 2). It includes `core/db_utils.h` and `primitives/biquad.h` (25–26).<br>• `enum class DetectionMode : uint8_t { Amplitude = 0, RMS = 1, Peak = 2 }` (41–45).<br>• Ranges: attack `kMinAttackMs = 0.1f` … `kMaxAttackMs = 500.0f`; release `kMinReleaseMs = 1.0f` … `kMaxReleaseMs = 5000.0f`; sidechain HP 20–500 Hz, default 80 Hz (88–96).<br>• `void prepare(double sampleRate, size_t maxBlockSize) noexcept` (106). `maxBlockSize` is unused (107). `void reset() noexcept` (128).<br>• `[[nodiscard]] float processSample(float input) noexcept` (164). The header says it **"Does NOT validate input - caller must ensure no NaN/Inf"** (163).<br>• `setMode` (202), `setAttackTime(float ms)` (220), `setReleaseTime(float ms)` (227), `setSidechainEnabled(bool)` (234), `setSidechainCutoff(float hz)` (240). `getLatency()` returns 0 (286).<br>• The coefficient is `exp(-2π / (t_ms · 0.001 · fs))`. That makes the time a ~99 % settling time, not a 63 % time constant (355–364).<br>• The built-in sidechain is a single `FilterType::Highpass` biquad at `kButterworthQ` (115–121). |
| `DynamicsProcessor`, `DynamicsDetectionMode` | `processors/dynamics_processor.h:63, 38` | The bass compressor (a configuration, roadmap line 472) | Layer 2 "plus peer Layer 2 EnvelopeFollower" (line 10; includes 19–23).<br>• **Mono: one channel per instance.** It holds one `EnvelopeFollower envelopeFollower_` and one `Biquad sidechainFilter_` (466–469).<br>• `enum class DynamicsDetectionMode : uint8_t { RMS = 0, Peak = 1 }` (38–41).<br>• Ranges: threshold −60…0 dB, ratio 1…100, knee 0…24 dB, attack 0.1…500 ms, release 1…5000 ms, makeup ±24 dB, lookahead 0…10 ms, sidechain HP 20…500 Hz (69–99).<br>• `void prepare(double sampleRate, size_t maxBlockSize) noexcept` (121). Gain reduction runs through a fixed **5 ms** `OnePoleSmoother` (140).<br>• `[[nodiscard]] float processSample(float input) noexcept` (166). NaN → 0 (168–170). **±Inf → ±1e10** (171–173). When the sidechain is enabled, detection runs on the filtered copy (176–180). Then `gainToDb(envelope)` → `computeGainReduction` (186–189).<br>• `process(float*, size_t)` (220); `process(const float*, float*, size_t)` (231).<br>• Setters: `setThreshold` (243), `setRatio` (251), `setKneeWidth` (258), `setAttackTime` (265), `setReleaseTime` (272), `setMakeupGain` (279), `setAutoMakeup` (285), `setDetectionMode` (291), `setLookahead` (299), `setSidechainEnabled` (306), `setSidechainCutoff` (312).<br>• `[[nodiscard]] float getCurrentGainReduction() const noexcept` returns the smoothed GR in dB, ≤ 0 (345). `getLatency()` returns the lookahead in samples (355).<br>• The soft knee is quadratic (367–399).<br>• **Auto makeup compensates "at 0 dB input"** (402–408). It is not a level-matching law.<br>• **`setLookahead(ms > 0)` allocates.** It reaches `lookaheadDelay_.prepare(...)` (434), and `DelayLine::prepare` resizes a `std::vector` (`primitives/delay_line.h:264, 284`).<br>• There is **no program-dependent release** and **no external sidechain input**.<br>• Consumers: `effects/digital_delay.h`, `effects/ping_pong_delay.h`, `systems/flexible_feedback_network.h`. |
| `SidechainFilter` | `processors/sidechain_filter.h:102` | **Not composed** (see "Reuse-inventory corrections") | Layer 2 (line 2). The header calls it a "Sidechain-controlled dynamic filter" (65): an envelope-driven SVF cutoff sweep for ducking and auto-wah (66–99).<br>• `enum class SidechainDirection { Up, Down }` (48–51); `SidechainFilterMode { Lowpass, Bandpass, Highpass }` (55–59).<br>• `prepare` is "NOT real-time safe (allocates DelayLine buffer)" (158–159).<br>• Its own HP, `setSidechainFilterCutoff(float hz)`, is a biquad at 20–500 Hz (471–480).<br>It is not a high-pass on a compressor's detector. |
| `TransientDetector` | `processors/transient_detector.h:34` | **Not composed. ODR hazard** (the name is taken) | Layer 2 (line 2). `class TransientDetector : public ModulationSource` (34). Sensitivity 0–1, attack 0.5–10 ms, decay 20–200 ms (36–44). `getCurrentValue()` (117), `getSourceRange()` (121). It is a modulation source, not a gain stage. |
| `TruePeakLimiter` | `processors/true_peak_limiter.h:44` | Output safety in the validation chain (roadmap lines 171, 576) | Layer 2 (line 2). It includes `primitives/oversampler.h` (29). The header says "Zero-latency true-peak limiter (stereo-linked)" (39–43).<br>• `kDefaultCeilingDb = -1.0f`, `kDefaultReleaseMs = 80.0f` (46–47).<br>• `void prepare(double sampleRate, std::size_t maxBlockSize) noexcept` is "NOT real-time safe (allocates)" (58–75; `osBufL_.assign`, 69–70).<br>• `reset()` (78), `setCeilingDb(float)` (84), `setReleaseMs(float)` (90).<br>• `void processBlock(float* left, float* right, int numSamples) noexcept` (104). It splits a long call into chunks of `maxBlockSize` (108–118).<br>• Detection uses a 4× `Oversampler<4,1>`, Economy / ZeroLatency (64–68, 125–126).<br>• **Attack is instantaneous**; release is one-pole (150–155). The header guarantees "|out| <= ceiling exactly" at sample level (12–14).<br>• **No NaN / Inf sanitisation** (comment at 141–145). |
| `DCBlocker` | `primitives/dc_blocker.h:94` | DC safety in the validation chain | Layer 1. `void prepare(double sampleRate, float cutoffHz = 10.0f) noexcept` (135), `void setState(float x1, float y1) noexcept` (165), `[[nodiscard]] float process(float x) noexcept` (200), `void processBlock(float* buffer, size_t numSamples) noexcept` (228). |
| `OnePoleSmoother`, `LinearRamp`, `SlewLimiter` | `primitives/smoother.h:134, 305, 468` | Mass slew; gain smoothing inside the shaper | `OnePoleSmoother`: `configure(float smoothTimeMs, float sampleRate) noexcept` (160), `setTarget(float)` (170), `process()` (197), `snapTo(float)` (263), `reset()` (275).<br>`SlewLimiter`: `configure(float riseRatePerMs, float fallRatePerMs, float sampleRate) noexcept` (501), `configure(float ratePerMs, float sampleRate)` (512), `setTarget` (519), `process()` (545), `snapTo` (591). |
| `gainToDb`, `dbToGain`, `detail::isFinite` / `isNaN` / `isInf` | `core/db_utils.h:313, 289, 118, 99, 136` | Gain-law arithmetic. Non-finite guards that survive `-ffast-math` | `gainToDb` returns `kSilenceFloorDb = -144.0f` for zero, negative or NaN input (33, 306–319). `isNaN` "Survives -ffast-math via the opaqueFloatBits barrier" (132). |
| `applyModCurve`, `ModCurve` | `core/modulation_curves.h:37` | Candidate curve evaluation for the Mass law's rows | `[[nodiscard]] inline float applyModCurve(ModCurve curve, float x) noexcept`. Linear gives x, Exponential x², SCurve x²(3 − 2x), Stepped floor(4x)/3 (23–57). |
| `VoragoMacroMatrix`, `VoragoMacroRow` | `systems/vorago_macro_matrix.h:264, 233` | **Pattern only** (roadmap line 168) for the Mass law's data table | A `constexpr std::array` of `{macro, owner, target, base, amount, curve}` rows (233–262, 315).<br>• `contributionOf` returns `amount · applyModCurve(curve, m)`, signed for a bipolar macro (1160–1176).<br>• `evaluateAll` seeds `base` once per target and sums the contributions, with **no neutral fast path** (1178–1200). |
| `SpectralShapeRecipe::Controls` and the named states | `processors/spectral_shape_recipe.h:41–48, 82–84` | The Depth and Body bias targets. Fixtures | Layer 2.<br>• `struct Controls { float depth = 0.5f; float body = 0.5f; float bodyCurvature = 0.5f; float bodyEmphasis = 0.0f; float edge = 0.5f; float shift = 0.0f; }` (41–48). The ranges are in the comments.<br>• `kCenterPanGain = 0.7071067811865476f` (52), `kPowerTarget` (53).<br>• `static const Controls kDefaultControls; kSineAnchor, kTriangleAnchor, kSawAnchor; kHeavy, kHollow, kGrowl` (82–84), defined at 227–248. `kSineAnchor` is depth 1, body 0, edge 0. `kSawAnchor` is depth 0, body 0, edge 0. |
| `ProfundumCore` | `systems/profundum_core.h:92` | Validation-chain source. The a₁ source | Layer 3.<br>• `kControlInterval = 32` (97).<br>• `void prepare(double sampleRate, int numPartials = kDefaultPartials) noexcept` is not RT-safe (127–129).<br>• `void setControls(const SpectralShapeRecipe::Controls&) noexcept` (173).<br>• `void noteOn(float f0Hz) noexcept` (208).<br>• `void processBlock(float* left, float* right, std::size_t numSamples, const float* f0PerSample = nullptr) noexcept` (237–238).<br>• `[[nodiscard]] std::span<const float> deliveredGains() const noexcept` (298). |
| `AnchorOscillator` | `systems/anchor_oscillator.h:55` | The Anchor `level` target. Validation chain | Layer 3.<br>• `enum class Waveform { Sine, Triangle, RoundedSine, AsymmetricSine, OctaveDownSine }` and `enum class Mode { Lock, Follow, Punch, Ghost }` (57–58).<br>• `kAnchorToH1 = 1.0f`, `kMaxCoreH1Amplitude = 0.3552343859f`, `kDefaultLevel = 0.5f` (70–72).<br>• `struct Controls { Waveform waveform; Mode mode; float level = kDefaultLevel; /* [0,1] */ … }` (99–106).<br>• `void setControls(const Controls&) noexcept` (224).<br>• `void setCoreH1Amplitude(float a1) noexcept` (262). A non-finite value keeps the previous one; otherwise it clamps to [0, `kMaxCoreH1Amplitude`] (261–265).<br>• `void noteOn(float f0Hz, bool rearm = true) noexcept` (276).<br>• `processBlock(float* left, float* right, std::size_t numSamples, const float* f0PerSample = nullptr, …)` (285). |
| `HarmonicFold` | `processors/harmonic_fold.h:76` | The saturation target (Fold amount). Validation chain | Layer 2.<br>• `kCalibrationPeak = 0.3552343859f`, "the core's -12 dBFS RMS sine peak" (83). `kOutputCeiling = 6.0f` (93).<br>• `void prepare(double sampleRate, std::size_t maxBlockSize) noexcept` (121).<br>• `void setAmount(float amount) noexcept` takes [0, 100]. A non-finite value keeps the previous one, and the value is smoothed (166–171).<br>• `void processBlock(float* left, float* right, std::size_t numSamples) noexcept` (188).<br>• `latencySamples() == 0` (271). |
| `CrossoverLR4` | `processors/crossover_filter.h:72` | **Not composed** here. A test may use it for the sub-band split; the plan chooses | A 2-way, flat-sum band split (8). |

**Test helpers (verified).**
- `dsp/tests/unit/systems/profundum_core_test_helpers.h`: `renderedHarmonicPowers` (235), `describeRendered` (252),
  `AliasResult` / `aliasedPower` (265, 273), `rmsDbfs` (453).
- `tests/test_helpers/render_fingerprint.h`: `fingerprintRender` (73), `compareFingerprints` (122).
- `tests/test_helpers/allocation_detector.h`: `AllocationScope` (111).
- `tests/test_helpers/artifact_detection.h`: `ClickDetectorConfig` (38), `ClickDetector` (99).
- **No loudness (BS.1770 / K-weighted) helper exists** under `dsp/`, `tests/` or `plugins/shared/`. A grep this
  session for `lufs|k-weight|1770` found no helper, only comments in unrelated tests. FR-050 adds a test-side helper.
  

**Test registration (verified).** `dsp/tests/CMakeLists.txt` lists TUs explicitly.
- `dsp_processors_tests` starts at line 164 (`harmonic_fold_test.cpp` is at 264).
- `dsp_systems_tests` starts at line 362 (`shape_morph_test.cpp` is at 701, `bass_character_stage_test.cpp` at 703).
- The `TransientShaper` TU registers under processors. The Mass-law, compressor and validation-chain TUs register under
  systems.

### Reuse-inventory corrections (found while verifying)

- **`SidechainFilter` is not a compressor sidechain high-pass.** The roadmap names it for the "sidechain high-pass
  (`SidechainFilter`)" (lines 472–473). The header is an envelope-controlled dynamic SVF (sidechain_filter.h:65–99) that
  allocates in `prepare` (158–159).
  - The sub-safe high-pass the roadmap asks for already exists *inside* `DynamicsProcessor`: `setSidechainEnabled` /
    `setSidechainCutoff` (dynamics_processor.h:306–321), a 2nd-order Butterworth biquad at 20–500 Hz. FR-011 uses it.
  - If SC-006 shows that 12 dB/oct is not enough at E0, the composing code adds a steeper detector filter. The header
    is changed only under FR-044.
- **`TransientDetector` lives in `processors/transient_detector.h:34`.** The roadmap row puts it in
  `core/modulation_source.h` (line 166). It derives from `ModulationSource`, so the verdict (not a shaper) stands.
- **`DynamicsProcessor` is mono and has no program-dependent release.** The roadmap's "configuration" therefore needs
  composing code: one shared gain for L and R (FR-013) and the program-dependent release (FR-012). FR-015 says where
  that code lives.

---

## New components

ODR sweep, run 2026-10-17: `grep -rnE "(class|struct|enum class|enum|using) <Name>\b" dsp/ plugins/ tests/ tools/` and
`find dsp plugins -iname "*<file>*"`.

| Class / type | Layer | Header | ODR sweep result |
|---|---|---|---|
| `TransientShaper` | 2 | `processors/transient_shaper.h` | **0 hits.** No `*transient_shaper*` file exists. These near names ARE taken:<br>• `TransientDetector` (`processors/transient_detector.h:34`);<br>• `SpectralTransientDetector` (`primitives/spectral_transient_detector.h:47`);<br>• `TransientAwareFilter` and `TransientFilterMode` (`processors/transient_filter.h:86, 43`);<br>• Ruinae's `TransientParams` (`plugins/ruinae/src/parameters/transient_params.h:17`);<br>• `TransientState` (`tools/ruinae_preset_format.h:870`).<br>None is reused, and none collides. |
| `MassLaw`, with nested target / row types | 3 | `systems/mass_law.h` | **0 hits.** No `*mass_law*` file exists. `MassTargets` and `MassLawTargets` also have 0 hits, and no `class` or `struct` whose name contains `Mass` exists anywhere in the sweep scope. The nested types stay inside `MassLaw`, so `MassLaw` is the only new namespace-scope `Mass*` name. |
| *Conditional:* `MassDynamics` | 3 | `systems/mass_dynamics.h`, only if FR-015 gives the composition its own header | **0 hits.** `MassDynamics`, `BassCompressor` and `ProgramDependentRelease` each have 0 hits. If the plan needs any other name, it sweeps and records that name before using it. |

**Layer discipline.**
- `TransientShaper` includes only L0, L1 and the peer L2 `EnvelopeFollower`. `DynamicsProcessor` is the precedent for a
  peer L2 include (dynamics_processor.h:10).
- `MassLaw` includes L0–L2, plus L3 peers (`AnchorOscillator`, `ProfundumCore`) for their range constants only. It
  never includes an L4 header.

---

## Functional Requirements

### Descriptor and fixture definitions (used by FRs and SCs)

All descriptors are computed offline in double precision.
- **Phase 5 descriptors.** These are taken unchanged from `specs/profundum-phase5-shape-character/spec.md`, "Descriptor
  definitions", with the same helpers and the same descriptor floor (clamp ≥ −60 dB; a descriptor counts only if one
  side is above −40 dB):
  - **w_n**, the rendered harmonic power of harmonic n;
  - **E_low** = Σ_{n=2..8} w_n / Σ w, and **E_high** = Σ_{n≥9} w_n / Σ w;
  - **THD** = Σ_{n≥2} w_n / w_1;
  - **centroid C**;
  - **aliased power A**;
  - **level L**, from `rmsDbfs` over whole periods.
- **Sub-band amplitude s(t)** is the amplitude of the f₀ component, in dB. It is measured by a sliding single-bin DFT
  over exactly one period of f₀, with a hop of at most period / 8. A second reading catches inharmonic or Anchor
  octave-down content below 2·f₀: the RMS of a low-pass at ≤ 1.5·f₀. The plan names that filter and records its group
  delay.
- **Sub-band modulation depth M_sub** is max(s) − min(s) in dB over a steady window, excluding the first 500 ms after
  note-on.
- **Gain trace g(t)** is the linear gain the processor applied, in dB. It is read either as output / input on a DC-free
  test path or from the processor's own diagnostic tap (FR-006, FR-016).
- **Perceived level P** (ruled, OQ-1 / Q3) is the BS.1770 K-weighted mean-square level over a steady window in dB, from
  the test-side helper of FR-050. The unweighted L is gated alongside it (SC-003), because K-weighting rolls off below
  about 38 Hz and under-reads sub-level drift.
- **Notes:** E0 = 20.60 Hz, E1 = 41.20 Hz, E2 = 82.41 Hz, C3 = 130.81 Hz.
- **Control interval.** "Control block" everywhere in this spec means the fixed internal interval of FR-049
  (32 samples, phase-tracked across `processBlock` calls), never one host `processBlock` call.
- **Duration descriptor D_tr.** The energy of the output over [onset, onset + max(50 ms, 1.5 × the largest duration
  setting in the sweep)] relative to the same-length steady-state energy, in dB. With D-3's 250 ms maximum the window
  is 375 ms. SC-008(c)'s measured transient time is recorded beside it.
- **Compression step descriptor ΔL_step.** On the two-level step fixture (`kSawAnchor`), the difference in L between
  the steady −12 dBFS segment and the steady −24 dBFS segment at the output, in dB (12 dB with the compressor at
  amount 0). Compression shrinks it.
- **Mass reference voice (fixture).** This is the stored base: the user's own values, i.e. the voice at **Mass 50**
  (ruled, D-1 / Q1).
  - `ProfundumCore` at `kDefaultControls`, excitation off;
  - `AnchorOscillator` on Sine, LOCK, `kDefaultLevel`, with a₁ fed per FR-031;
  - `HarmonicFold` at base amount 0;
  - the shaper and the compressor at their Mass-neutral base (D-1). The shaper base is the ruled default (FR-009,
    D-3): **attack gain +6 dB, sustain gain 0 dB**, so the Mass-driven duration lever has a transient excess to
    lengthen. At attack = sustain = 0 dB the shaper is the identity (FR-003) and the duration lever is inert; that
    case is recorded, not gated (FR-009). Mass never drives attack gain (six-target law, FR-021).

  The Mass targets are applied as biases on this base: base + amount · curve, clamped (FR-027). Every "Mass 0" fixture
  in this spec (SC-002, SC-001(b) held values) is therefore Mass 0 applied to this stored base, never a separate
  absolute voice. A second fixture uses `kSawAnchor`, the richest linear base. A third uses `kSineAnchor`, the purest.
- **Burst fixture** (for the transient tests).
  - A sine or `kSawAnchor` tone, gated on at a zero crossing with a 0.5 ms raised-cosine onset and held for 1 s, at the
    −12 dBFS RMS calibration level.
  - A two-level step from −24 to −12 dBFS RMS. For any compressor measurement the tone is `kSawAnchor`, because a
    sine at E0–E2 sits below the sidechain high-pass (FR-011) and would leave the detector starved.
  - A 2 s silence pre-roll arm: the same onset preceded by 2 s of exact zeros after `reset()`.

### FR-001 series — `TransientShaper` (roadmap lines 470–471)

- **FR-001 Interface.** `TransientShaper` (L2, `processors/transient_shaper.h`) provides:
  - `prepare(double sampleRate, std::size_t maxBlockSize) noexcept` and `reset() noexcept`;
  - `processBlock(float* left, float* right, std::size_t numSamples) noexcept`, in place. `right == left` processes one
    channel;
  - setters for **attack gain** (dB), **sustain gain** (dB) and **transient duration** (ms);
  - `static constexpr std::size_t latencySamples() noexcept`, returning 0.

  The ranges are ratified (D-3): attack gain −12…+12 dB, sustain gain −12…+12 dB, transient duration 5…250 ms. Setters
  clamp to them, and a non-finite argument keeps the previous value (SC-025).
- **FR-002 Detector pair.** The transient measure comes from a fast / slow pair of `EnvelopeFollower` instances
  (roadmap lines 470–471).
  - The transient component is the excess of the fast envelope over the slow one, in dB (FR-008).
  - The sustain component is the slow envelope.
  - **Transient duration** is the slow detector's attack time (ruled, Q5). That makes it the "transient duration"
    lever the Mass law drives (line 476). The detector's attack-to-duration mapping is a measured calibration table
    (FR-008, SC-008(c)).
- **FR-003 Gain law.** The applied gain is a smooth, documented function of the detector pair, the attack gain and the
  sustain gain, fixed by FR-008.
  - At attack gain = sustain gain = 0 dB, the shaper is the identity (SC-013).
  - The law is monotonic: raising attack gain never lowers the gain applied during the transient, and raising sustain
    gain never lowers the gain applied in steady state.
- **FR-004 Bass-safe detection.** On a steady note at E0 (period 48.5 ms), the applied gain carries no waveform-rate
  ripple beyond SC-009's bound. The detector may rectify, hold over one period, low-pass, or use a sidechain filter; the
  plan chooses and records why. A fast follower that rides individual cycles is exactly the defect this FR rules out.
  The detector's own steady-state test (plan C-4 / S3, the HarmonicFold D-4 fixture at E1, 50 ms hold, no follower) is gated at a
  held-value ripple of **≤ 1e-4** on the 64-partial saw and **≤ 2.3e-3 (0.02 dB peak-to-peak)** on the wide saw to 0.45 fs (user ruling 2026-10-17, Clarifications "Group 2 gate ruling":
  HarmonicFold's 1e-4 reads its follower, whose 0.5 % release hysteresis absorbs raw ripple up to 5e-3, over a 125 ms hold; the
  raw estimate on Gibbs peaks reads 1.43e-3 / 1.44e-3 / 7.39e-4 at 44.1 / 48 / 96 kHz, one tenth of SC-009's 0.2 dB is the bar).
- **FR-005 One gain for both channels.** One gain is computed per sample from a combined detection of both channels and
  applied identically to L and R. The plan chooses the detection signal: max(|L|, |R|) or the mid. If any high-pass
  sits in the shaper's detector path (for example `EnvelopeFollower::setSidechainEnabled`, whose filter runs on the
  input before detection, envelope_follower.h:164–166), the filter sees a **signed** signal: either the mid, or L and
  R each high-passed before any rectification or max-combining. A rectified signal is never fed to a high-pass (see
  FR-011 for why).
- **FR-006 Diagnostic tap.** `currentGainDb()` (or an equivalent) returns the gain applied to the last sample. It is
  read-only and allocation-free, and serves SC-008 and Phase 12 metering.
- **FR-007 Non-finite input.** `EnvelopeFollower::processSample` does not validate its input (envelope_follower.h:163).
  The shaper therefore tests every input sample with `detail::isFinite` (db_utils.h:118), which survives
  `-ffast-math`. A non-finite sample on a channel is replaced by **0.0f** before detection and before the gain is
  applied, so that channel's output sample is 0.0f. The detector pair, the gain law and the smoothers all see that
  same substituted 0.0f; nothing reads the raw value. The output meets SC-019.
- **FR-008 Normalised differential gain law (ruled, Q5).** With fast_dB and slow_dB the two envelopes in dB and
  c = clamp((fast_dB − slow_dB) / K, 0, 1), the gain applied to both channels is

  `gain_dB = attack_dB · c + sustain_dB · (1 − c)`, with **K = 12 dB** (ratified).

  - The sustain gain applies outside the transient, per the same clamp complement (1 − c).
  - A note onset from silence yields c = 1, so it receives the **full attack gain** (SC-008(b), SC-008(e)). Onsets
    smaller than K dB receive a proportional share.
  - **Duration calibration.** The slow detector's attack time is set from the duration setting through a measured
    calibration table (a harvested table pasted into the header with its evidence file, as FR-022 does for the Mass
    law), so that the measured transient time equals the duration setting within 15 % over the whole 5…250 ms range
    (SC-008(c)); on the E1 sine the 5 ms and 7.5 ms cells are **recorded**, not gated (a 41 Hz sine delivers its first peak
    6.07 ms after onset, so no amplitude detector can report a shorter transient there: plan C-2, extended to the 7.5 ms cell by
    user ruling 2026-10-18, Clarifications "Group 9 close rulings" G9-2, measured +19.17 %); the harvest prints `STOP AND SURFACE`
    for any other cell beyond 15 %, never a retune.
- **FR-009 Attack and sustain gains are user-facing (ruled, Q6).**
  - Attack gain and sustain gain are user-facing parameters, to be registered in Phase 11 in the 700–799 range.
    Defaults: **attack +6 dB, sustain 0 dB**. Ranges per FR-001.
  - Mass drives the transient **duration only**. It never drives attack gain or sustain gain (FR-021).
  - At attack = sustain = 0 dB the shaper is the identity (FR-003) and the Mass duration lever is inert. That case is
    **recorded, not gated**. The audibility of the duration lever is measured at the defaults (SC-011).

### FR-010 series — Bass compressor configuration of `DynamicsProcessor` (roadmap lines 472–473)

- **FR-010 Reuse.** The bass compressor's gain computer is `DynamicsProcessor` (dynamics_processor.h:63), configured
  and not re-implemented. Its envelope follower, knee, ratio, threshold and 5 ms gain smoother are used as they are.
- **FR-011 Sub-safe sidechain high-pass.** Detection runs on a high-passed copy of the signal, so the sub never drives
  the gain computer (roadmap lines 472–473). The high-pass is applied to a **signed** signal, before any rectification
  or max-combining of the channels. Inside `DynamicsProcessor::processSample` the sidechain biquad filters exactly
  the sample it is given (`detectionSample = sidechainFilter_.process(input)`, dynamics_processor.h:176–180), so a
  rectified feed would be filtered after rectification. Rectifying a sub at f₀ gives DC plus 2·f₀, 4·f₀, …: the
  high-pass removes the DC, passes 2·f₀ where 2·f₀ is above the corner (E2: 165 Hz against a 150 Hz corner), so the
  sub still drives the gain computer, and removes nearly everything where 2·f₀ is below it (E0, E1), so the detector
  starves. Both defeat the FR. The admissible forms are fixed in D-6:
  - (a) the mid (L+R)/2, signed, fed to `DynamicsProcessor` with `setSidechainEnabled(true)` and
    `setSidechainCutoff(hz)` (dynamics_processor.h:306–321);
  - (b) L and R each high-passed by composed `Biquad` instances (L1) first, then max(|HP(L)|, |HP(R)|) fed to
    `DynamicsProcessor` with its internal sidechain **off**. This is the form D-6 ratifies as the shared-instance
    detection signal (the "max" combiner is applied to the already high-passed channels).
  - The cutoff is **keytracked** (ruled, D-4 / Q7): 2·f₀ of the lowest sounding voice, set through the engine-facing
    setter of FR-018, smoothed and clamped to 20–500 Hz. SC-006 is the gate, at E0, E1, E2 and C3, on the keytracked
    corner.
  - If the 2nd-order biquad cannot meet SC-006 at E0, the composing code adds a steeper detector filter ahead of the
    compressor's detector. It never modifies the header outside FR-044.
- **FR-012 Program-dependent release.** The release time varies with the programme by a documented, monotone rule.
  - *Ruled (D-5 / Q8):* the release time is interpolated linearly from **50 ms to 500 ms** by the fraction of the
    **last 300 ms** spent beyond **1 dB** of gain reduction (smoothed GR, threshold 1 dB ratified). The fraction is 0
    for a programme that never exceeded 1 dB and 1 for one that spent all 300 ms beyond it. The rule is updated and
    driven through `setReleaseTime` **once per FR-049 control interval** (32 samples), never per sample. `setReleaseTime` clamps to
    [1, 5000] ms and forwards to `EnvelopeFollower` without smoothing (dynamics_processor.h:272–275), which recomputes
    the coefficient with `detail::constexprExp` (envelope_follower.h:355–364). A per-sample update would be one exp per
    sample inside SC-017's budget. An interpolated coefficient table is an allowed alternative if the plan records it;
    SC-017 measures the chosen rate.
  - `DynamicsProcessor` has no such feature (verified), so the rule is composing code (FR-015). SC-007 measures it:
    a 20 ms event and a 1 s event under it must separate in recovery time.
- **FR-013 One gain for both channels.** `DynamicsProcessor` is mono (dynamics_processor.h:466–469). The compressor
  therefore computes one gain from a combined detection signal and applies it identically to L and R.
  - *Ratified mechanism (D-6):* **one shared** `DynamicsProcessor` instance runs on a combined detection signal (the
    FR-011 form (b): max(|HP(L)|, |HP(R)|), high-pass first). **One gain** is applied to both channels. Its `getCurrentGainReduction()` (345), already smoothed, plus makeup is applied to both channels. Its own
    audio output is unused. max(|L|, |R|) of the **unfiltered** channels fed to the internal sidechain is forbidden
    (FR-011); SC-006(e) runs it as a negative control.
  - Two independent instances, one per channel, are forbidden. They would move the stereo image (FR-041).
- **FR-014 Zero latency (ratified, D-6).** Look-ahead stays at 0. `setLookahead` with ms > 0 allocates (dynamics_processor.h:434;
  delay_line.h:284) and adds latency (355), so it is never called with a non-zero value. The compressor reports 0
  latency.
- **FR-015 Where the composition lives.** The shared-gain logic, the program-dependent release rule and the
  compression-amount mapping (FR-016) live in Profundum code, not in `dynamics_processor.h`. The plan chooses between
  two placements and records the choice:
  - (a) the Mass law's header hosts a small composition;
  - (b) a separate L3 `MassDynamics` header (ODR-swept, 0 hits).

  Either way, the order inside the Mass dynamics block is **TransientShaper → compressor**. The plan may reverse it
  only if a measurement shows the reverse order is required for SC-006 / SC-009, and it records that measurement.
- **FR-016 Compression amount.** One scalar in [0, 1], the Mass law's "compression amount" target, maps to threshold,
  ratio, knee and makeup through a documented, monotone table.
  - Amount 0 is a hard bypass: ratio 1, for which `computeGainReduction` returns 0 (dynamics_processor.h:369–371).
    After settling it is bit-identical to the input (SC-013).
  - `setAutoMakeup` is not used. It compensates "at 0 dB input" (402–408), which is not the programme level. Makeup is
    part of the calibrated table instead.
  - **Makeup offsets only the compressor's own gain reduction.** makeup(amount) equals the measured steady-state GR at
    that amount on the Mass reference voice, within **0.5 dB** (SC-022). Makeup never compensates the level added by
    the Fold, Body or Anchor-level targets; that would be an output trim inside the compression row, i.e. a seventh
    target (FR-021; since 2026-10-17 that trim exists as its own harvested row outside the compression row, G8-5). At amount 0 the makeup is 0 dB, consistent with the hard bypass.
- **FR-017 Non-finite input.** `DynamicsProcessor` maps ±Inf to ±1e10 (171–173) and passes it to the output. The
  composition therefore tests every input sample with `detail::isFinite` before the compressor: a non-finite sample on
  a channel is replaced by **0.0f** in both the audio path and the detection signal, so that channel's output sample
  is 0.0f, and the detector, the release rule and the gain smoother see the same substituted 0.0f.
  `DynamicsProcessor`'s own NaN / Inf branch is therefore never reached. SC-019 holds.
- **FR-018 Keytracked sidechain corner (ruled, Q7).** The compressor composition exposes an engine-facing setter
  (name per plan, `setDetectorKeyHz`-style, ODR-swept).
  - The engine calls it **once per FR-049 control interval** with the f₀ of the **lowest sounding voice**. In POLY the
    lowest sounding voice's f₀ is used.
  - The detector high-pass corner is **2·f₀**, **smoothed** (time recorded by the plan), and **clamped to 20–500 Hz**,
    continuously, including across a Drop sweep.
  - When no voice sounds, the corner takes a **fixed fallback value** (recorded by the plan). A non-finite or
    non-positive f₀ means "no voice sounds".
  - The setter is allocation-free (FR-040) and a no-op before `prepare` (FR-047). SC-024 gates it; SC-006 gates the
    pumping bound on it.
- **FR-019 No pumping (ruled, OQ-2 / Q4).** With the compressor at amount 1 on a sustained note:
  - steady-state sub-band modulation depth **M_sub ≤ 0.5 dB peak-to-peak** at E0, E1, E2 **and C3**;
  - an Anchor `level` step 0.25 → 1 (a sub-only change, body unchanged) moves the compressor's steady gain reduction
    by **≤ 1 dB**.

  SC-006 gates both, keeps its positive control, engagement floor and negative control, and reads them through the
  FR-018 keytracked corner.

### FR-020 series — The Mass law (roadmap lines 474–481; concept §4)

- **FR-020 Interface.** `MassLaw` (L3, `systems/mass_law.h`) maps one Mass value in [0, 100], plus the base values of
  its targets (the user's own values), to the six targets of the Overview table. Mass is **bipolar about 50** and all
  six targets are **biases on the user's base** (ruled, Q1; FR-027). Each target is in its component's own units:
  - Depth bias and Body bias, in recipe units;
  - transient duration, in ms;
  - Fold amount, in [0, 100];
  - compression amount, in [0, 1];
  - Anchor level, in [0, 1].

  `MassLaw` does no audio processing.
- **FR-021 Six roadmap targets plus the calibrated output trim (amended).** The law drives the six roadmap targets (lines
  474–477). If calibration cannot meet SC-003 without an extra target, such as an output trim, that is a ruling for the user.
  Nothing is added silently. **That ruling was taken (user, 2026-10-17, Clarifications "Group 8 harvest ruling" G8-5):** the
  law's seventh row is a calibrated **output trim**, `Target::OutputTrim` in dB, applied inside `MassDynamics` after the
  compressor (`setOutputTrimDb`, 5 ms smoothed, exactly 1.0 at 0 dB), 0 dB at Mass 50, monotone non-increasing (FR-024), one
  Linear row harvested so the reference voice's K-weighted level at E2 / C3 is flat from Mass 0 to 50 and does not rise above
  50 (only rises are cancelled); `kNumTargets = 7`. It is a level-neutrality calibration, recorded in SC-011, not a
  sound-shaping target. Measured reason: with every other lever spent (compression at its maximum, Fold at its SC-002 minimum,
  Anchor travel carried by SC-001(c)) the saw fixture's K-weighted span at E2 / C3 read 4.0 / 3.8 dB against 3 dB.
  **Calibration levers (user ruling 2026-10-17, R-6; Clarifications "Group 8 harvest ruling").** The Anchor's level is relative to the
  core's h1 (anchor_oscillator.h:340), so it cannot carry a fundamental the Depth bias removes. The law's Depth and Anchor travel are
  therefore harvested, not seeded, in this order: (0) the largest Anchor travel (amountDown = amountUp) whose unweighted span(L) stays
  ≤ 6 dB on every cell (SC-003); (1) the largest Depth travel, direction unchanged (falling, D-2), each half separately, keeping the
  sum's w₁ within SC-001(c)'s bounded fall with the 0.5 dB harvest margin (G8-4 superseded the round-1 "non-decreasing
  within 0.1 dB"); (2) the smallest Fold / Body amountUp meeting the SC-002 THD steps; (3) the SC-003
  K-weighted check and bends. Mass 100's richness then comes from Fold, Body and compression rather than from Depth → 0. Still six
  targets, no compensation (FR-032), no bar moved.
- **FR-022 Documented and calibrated.** The law is a data table, and the table is the record of the tuning. The
  `VoragoMacroMatrix` row pattern (vorago_macro_matrix.h:233–262) is the template: a base, a signed amount and a curve
  per target.
  - Every target's direction is normative (D-2, ruled).
  - Each row is `base + amount · curve(·)`, clamped to the component's range, with Mass bipolar about 50 (FR-027). The
    Anchor level stays one control plus a Mass offset (Phase 3 FR-022), in the Vorago macro-row pattern.
  - Amounts and curves are calibration. They are harvested by measurement on the Mass reference voice and pasted into
    the header together with their evidence file. The compression-amount table (FR-016) and the shaper duration table
    (FR-008) are calibrated the same way.
- **FR-023 Anchor points.** On the Mass reference voice:
  - Mass 0 is "pure, clean, sine-like";
  - Mass 50 is "thick analogue";
  - Mass 100 is "enormous distorted".

  These come from concept lines 230–246 and roadmap lines 477–478. Each anchor is a descriptor window (SC-002), not a
  listening claim.
- **FR-024 Monotonic targets.** Every target is monotonic in Mass over [0, 100], in its documented direction. This holds
  both for the law's output values and for the rendered descriptor that the target moves (SC-001). Monotonic here is
  non-strict: a clamped segment is allowed, and it is recorded.
  - **Directions (ruled, D-2 / Q2).** The Depth bias **falls** as Mass rises (richer core; Mass 0 is sine-like). The
    Body bias, transient duration, Fold amount, compression amount and Anchor level rise. The FR-021 output trim is
    non-increasing (G8-5, 2026-10-17).
  - **Fundamental strength** is the **absolute sub energy of the core + Anchor sum**, carried by the rising Anchor
    level, not the Depth ratio. Over the joint Mass sweep the sum's w₁ at the sum tap is a **bounded fall**: at every
    Mass point ≥ its Mass 0 value − 6 dB, adjacent steps recorded, and the Anchor tap's own w₁ non-decreasing under
    SC-001(b) (SC-001(c) as re-ruled; user ruling 2026-10-17, Clarifications "Group 8 harvest ruling" G8-4, which
    superseded "non-decreasing": a pure Mass 0 holds about 1.86 × the unbiased a₁ in h1 and Mass 100 about 0.09 × plus
    an Anchor capped at 1 ×). This is a calibration constraint on the table.
- **FR-025 Bounded slew.** Mass is smoothed before evaluation, so every target's change per FR-049 control interval is
  bounded at any automation speed, including a 0 → 100 step (roadmap line 478: "bounded slew").
  - The full-range traversal time is ratified in D-7: a full Mass sweep takes **≥ 20 ms**, through a linear
    `SlewLimiter` (slew smoother). Plan C-7 (user ruling 2026-10-17, Clarifications "Plan S1 rulings"): the 20 ms is the time of a half sweep (base → either end,
    since Mass is bipolar about 50 and some bases sit at a range end), so a 0 → 100 traversal takes 40 ms and SC-004's
    per-interval bound is derived from 20 ms; no row is capped at half range.
  - Some receiving components already smooth their input: the Fold amount, the Anchor level and the core's recipe slew.
    Those targets are not smoothed twice unless SC-005 requires it. The plan records which targets are.
- **FR-026 Pure evaluation.** `MassLaw::evaluate(mass, base)` (name per plan) is a pure function: the same inputs give
  bit-identical outputs. Its only state is the FR-025 smoother, which is separable. Phase 8 interpolates Mass as a
  scalar and Phase 9's Motion modulates it, and both rely on this.
- **FR-027 Bias semantics.** Depth and Body are applied as **biases** on the voice's own `Controls` values (roadmap line
  475: "recipe Depth bias", "Body bias"). The result is clamped to the recipe's ranges (spectral_shape_recipe.h:42–43).
  - **Ruled (D-1 / Q1).** Mass is **bipolar about 50**. **All six** targets (Depth, Body, duration, Fold amount,
    compression amount, Anchor level) are biases on the user's base values: target = clamp(base + amount · curve),
    with the curve evaluated on the signed distance of Mass from 50.
  - **Neutral at 50.** At Mass = 50 every bias is zero and every target equals its base, so the user's sound is
    unchanged (SC-023). Mass 0 and Mass 100 push away from the base, with clamping to each component's range.
  - The user's Anchor-level and Fold controls are never overridden: they are the bases.
- **FR-028 Not a volume knob.** Over its sweep, Mass changes the perceived level by no more than the ruled range
  (OQ-1 / Q3): the K-weighted level span ≤ 3 dB with \|ΔP\| ≤ 0.2 dB per Mass unit, and the unweighted RMS level span
  ≤ 6 dB (SC-003). The compressor's makeup offsets only its own GR (FR-016, SC-022). Any remaining level change from the
  Fold, Body or Anchor-level targets is absorbed by those targets' own calibration (amount and curve), or, if that
  cannot meet SC-003, it goes to the user as a FR-021 ruling. It is never an Anchor compensation (FR-032).
- **FR-029 Input safety.** A non-finite Mass keeps the previous value, and finite values clamp to [0, 100]. A
  non-finite base field keeps that target's previous base.

### FR-030 series — Phase 3 handoff and the validation chain (roadmap lines 478–481)

- **FR-030 Anchor level is the only Anchor lever.** The Mass law's "sub/body relationship" target writes
  `AnchorOscillator::Controls::level` (anchor_oscillator.h:102) and nothing else in the Anchor (Phase 3 FR-022).
- **FR-031 a₁ feed (amended, user ruling 2026-10-17, Clarifications "Group 8 harvest ruling" G8-3).** Every Phase 6 fixture that runs core + Anchor calls
  `setCoreH1Amplitude(a₁)` once per core control block (Phase 3 order) with **a₁ = the recipe's h1 for the voice's own `Controls`,
  before the Mass Depth / Body biases, × `SpectralShapeRecipe::kCenterPanGain`** (`SpectralShapeRecipe::evaluate(base, f₀, fs)[0]`),
  not the Mass-biased `deliveredGains()[0]`. The Anchor therefore holds the voice's fundamental while Mass thins the core's h1; at
  Mass 50 (no bias) the two readings coincide. This is the Phase 9 voice contract (roadmap handoff). Measured reason: with the
  delivered feed the Anchor shrank with everything that thinned h1 (Depth, Body, Fold), so no Mass table could keep the sum's w₁
  non-decreasing (three harvest rounds, G8-1 … G8-3), and the roadmap's "fundamental that cannot be destroyed" did not hold against Mass.
- **FR-032 No compensation.** Neither `MassLaw` nor the Mass dynamics compensates the Anchor or normalises the core +
  Anchor sum (Phase 3 FR-025). The sum is a plain add. Keying the Anchor to the unbiased h1 (FR-031) is a reference definition,
  not a measured gain: nothing reads the core's output or the sum to set the Anchor (user ruling 2026-10-17, Clarifications "Group 8 harvest ruling" G8-3).
- **FR-033 Validation chain.** A test fixture composes the chain from roadmap line 576, minus the Phase 7 stage:
  1. core + Anchor (FR-031);
  2. the Fold at the Mass target;
  3. the sum;
  4. Mass dynamics (shaper → compressor → the FR-021 output trim, G8-5);
  5. `DCBlocker`;
  6. `TruePeakLimiter`.

  SC-014 and SC-015 are measured on it. The fixture is test code; the engine wiring stays Phase 9's.
- **FR-034 The limiter is safety, not tone.** On the Mass reference voice at the calibration level, the calibrated Mass
  law keeps the steady-state signal below the `TruePeakLimiter` ceiling (`kDefaultCeilingDb = -1.0f`). The limiter's
  attack is instantaneous (true_peak_limiter.h:150–155), so it must stay idle on a sustained note at every Mass
  (SC-015). Note attacks may touch it, and that GR is recorded.
  - Once touched, the limiter's gain cannot return to exactly 1.0f. Its release is
    `currentGain_ += (required - currentGain_) * releaseCoeff_` (true_peak_limiter.h:153–155) with
    `releaseCoeff_ = 1 − exp(−1/τ)` (setReleaseMs, :90–95), about 2.6e-4 at 80 ms / 48 kHz. Near 1.0 a step below half
    an ulp rounds away, so the gain settles near 1 − 1.1e-4 (about −0.001 dB). That float floor is expected and SC-015
    tolerates it; it is not a defect.
- **FR-035 Phase 9 handoff record.** Comply records, for Phase 9:
  - the global block's latency (0) and its CPU cell;
  - the Mass-to-target table;
  - the sub-band GR for the poly-sum case (4 voices at Mass 100), recorded, not gated;
  - the limiter GR at note attacks;
  - the keytracked-corner handoff (FR-018): the engine calls the setter once per control interval with the f₀ of the
    lowest sounding voice (in POLY, the lowest sounding voice), and passes "no voice" when none sounds;
  - the shaper's user-facing attack / sustain gain parameters, ranges and defaults (FR-009) for the Phase 11 700–799
    registration.

### FR-040 series — Cross-cutting

- **FR-040 RT safety.** Nothing on the audio path allocates, locks, throws or does I/O (roadmap line 883). That covers
  `processBlock`, every setter, `reset`, the Mass evaluation and the release rule. `TruePeakLimiter::prepare` and
  `ProfundumCore::prepare` allocate, so they are called from `prepare` only. `AllocationScope` asserts this (SC-016).
- **FR-041 Mono compatibility (theme FR).** The Mass dynamics touches stereo, so it ships a mono-sum test (roadmap line
  888; SC-012). Because L and R share one gain, L == R at the input gives L == R at the output, bit-identically. A
  stereo input keeps its side / mid ratio.
- **FR-042 Anti-aliasing (theme FR).** A gain that varies at waveform rate is a nonlinearity. The shaper and the
  compressor are measured at E0–E2 at their fastest settings against SC-010 (roadmap line 890). The `kSawAnchor` input is gated;
  the Fold-100 input is recorded (user ruling 2026-10-18, G9-1; SC-010 row).
- **FR-043 Audibility (theme FR).** Every lever ships a render-descriptor gate (roadmap line 885; SC-011).
- **FR-044 No shared-header change.** No existing header is modified. If the plan finds `DynamicsProcessor`,
  `EnvelopeFollower` or `DCBlocker` unusable, the change is append-only (roadmap lines 900–901; SC-020), and these suites
  stay green:
  - **Ratified exception (user ruling 2026-10-18, Clarifications "Group 9 close rulings" G9-3):** `TruePeakLimiter::processChunk`
    (true_peak_limiter.h:160–164) now clamps its output to ±ceiling, the Group 7 gate fix G7-1 (Clarifications "Group 7
    gate rulings": a sample equal to the detected peak came out one ulp, 5.96e-8, above the ceiling, against the
    header's own exact guarantee; reproduced first in `true_peak_limiter_test.cpp`). It is a modification, not an
    append; made by the gate fixer and ratified by the user. Its consumers (`SeraphisEngine`, `VoragoEngine`,
    the Seraphis and Membrum processors) have their suites re-run green (`dsp_effects_tests`, `seraphis_tests`,
    `vorago_tests`, `membrum_tests`; SC-020 compliance row). `suites.sh` step 3 lists it as an expected change.
  - `dsp_effects_tests` and `dsp_systems_tests`: `DigitalDelay`, `PingPongDelay` and `FlexibleFeedbackNetwork` consume
    `DynamicsProcessor`;
  - Iterum's `plugin_tests` and `approval_tests`;
  - `ruinae_tests`, `seraphis_tests`, `vorago_tests` and `innexus_tests`.
- **FR-045 The CPU budget is an FR.** The budget is ≤ 1 % of one core, global, at 48 kHz (roadmap line 485; SC-017). It
  is never relaxed. Measured on the shipped code (isolated, pinned, 2026-10-17): 1.047 / 1.183 / 1.270 % against the T001
  prototype's 0.769 %; the profile (`evidence/run-log/close/stage-bench.txt`, 2026-10-18) put six per-sample dB conversions,
  the Layer-0 `gainToDb` / `dbToGain` constexpr series evaluated at run time at ≈ 24 ns each, at ≈ 60 % of the cost. Refined
  (main loop, Clarifications "Group 9 close rulings" M-3): the shaper's and the compressor's own per-sample conversions use
  `detail::gainToDbFast` / `dbToGainFast` (`std::log2` / `std::exp2` forms of the same functions, equal within float rounding;
  0 dB still maps to exactly 1), and `setTransientDurationMs` returns early on an unchanged value; the gated cell then
  profiles at 0.802 %. The two discarded `dbToGain` inside `DynamicsProcessor::processSample` stay (FR-044). R-1's TICK4 /
  TP2X fallbacks are not applied.
- **FR-046 Portability.** No `std::isnan`. Designated initialisers. `node tools/check-portability.js` and
  `tools/lint-apple-globals.js` are both clean (roadmap line 897).
- **FR-047 Lifecycle.**
  - Calls before `prepare` are no-ops (the `HarmonicFold` convention).
  - `numSamples == 0` or a null pointer is a no-op.
  - `reset()` clears detector, smoother and compressor state, and snaps smoothers to their targets.
- **FR-048 Failed bars are rulings.** A bar that fails is reported with its measurement and goes to the user. It is
  never relaxed in place.
- **FR-049 Fixed internal control interval.** The Mass slew step, the Mass evaluation and the release-rule update
  (FR-012) run on a fixed internal interval of **32 samples**, the `ProfundumCore::kControlInterval` value
  (profundum_core.h:97). The interval phase is tracked across `processBlock` calls, as `ProfundumCore` does
  (`intervalPhase_`, profundum_core.h:285–286), and is reset by `reset()` / `prepare()`. Updates therefore land on the
  same absolute samples whatever the host's block partition. This is what SC-018 tests and what SC-004 calls a
  "control block".
- **FR-050 K-weighting test helper (ruled, Q3).** A BS.1770 K-weighting helper (the two-stage pre-filter and RLB
  high-pass, then mean-square level) is added as **test-side code** under the tests' helper directory. No product code
  changes. It computes the perceived level P of the Descriptor definitions and is validated against a known reference
  (a 997 Hz sine at a known level reads the expected K-weighted value, and the pre-filter rolls off below about
  38 Hz). SC-003 uses it.

---

## Success Criteria

**Differential click count** (used by SC-005 and SC-008). This adopts the Phase 5 definition unchanged
(`specs/profundum-phase5-shape-character/spec.md`, "Success Criteria" preamble):
- two phase-aligned reference renders, held at the start and the end setting;
- a match window of ±N ≤ period / 8;
- the 1.1× amplitude guard;
- a bar of zero counted detections;
- positive controls (an unsmoothed full-range step and a hard gain switch), flagged per arm and per note.

| ID | Criterion (roadmap line) | Metric, threshold, method | Test name sketch |
|---|---|---|---|
| **SC-001** | Every Mass target is monotonic (483) | **(a) Law.** Each of the seven outputs (the six roadmap targets and the FR-021 output trim, G8-5), over Mass ∈ {0, 0.1, …, 100} (1001 points) on the three fixtures' bases, is monotone in its D-2 direction. Non-strict; clamped runs are recorded.<br>**(b) Rendered.** Each target is driven alone, with the others held at their Mass 0 values (the compression lever: at their Mass 50 base values, the SC-006 / SC-022 calibration voice — main-loop refinement 2026-10-17, Clarifications G8-7: at Mass 0 the Depth bias purifies the step fixture's saw to a voice the 2·f₀ sidechain cannot see, GR 0.03 dB at amount 1), over 101 Mass points (step 1) at E1 and C3. Its descriptor moves monotonically within a tolerance equal to the measured repeat noise (8 start offsets × block sizes {64, 512}, capped at 0.1 dB / 0.005 oct). The descriptors are:<br>• Depth bias → w_1 / Σw;<br>• Body bias → E_low;<br>• duration → D_tr (window max(50 ms, 1.5 × the largest duration), with the D-1 shaper base of attack +6 dB);<br>• Fold → THD;<br>• compression → ΔL_step on the two-level step fixture (non-increasing); steady-state GR is recorded only;<br>• output trim → L of the chain output (non-increasing; FR-021 as amended, G8-5);<br>• Anchor level → the sub energy w_1 of the sum.<br>**(c) Joint sweep, fundamental strength.** All six targets are driven together over the 101-point Mass sweep, on the three fixtures at E0, E1, E2 and C3. The sum's w_1 at the **sum tap** (fold + anchor, FR-033's `sumL`, before the Mass dynamics; user ruling 2026-10-17, Clarifications "Group 8 harvest ruling" G8-2: the compressor's gain is common to every harmonic and is owned by SC-002 / SC-006 / SC-011, and SC-002's GR(100) ≥ 3 dB makes an output-tap w_1 fall by the GR excess on any voice richer than the calibration voice) is at every Mass point **≥ its Mass 0 value − 6 dB**, within the (b) repeat-noise tolerance (a **bounded fall**; user ruling 2026-10-17, Clarifications "Group 8 harvest ruling" G8-4: a sine-like Mass 0 puts the whole calibration into h1, about 1.86 × the unbiased a₁, while at Mass 100 the core's h1 is about 0.09 × and the Anchor's level caps at 1 ×, so the sum's w₁ must end near 1.09 ×, about 4.6 dB below Mass 0 with the dip near Mass 70 about 5.4 dB; no Anchor level ≤ 1 keeps it non-decreasing while Mass 0 is pure). The adjacent-step changes are recorded. The Anchor tap's own w₁ stays non-decreasing under (b). This is the gate D-2's falling Depth bias relies on; the Depth travel that meets it is harvested (FR-021 calibration levers). | `MassLaw_TargetsMonotonic`, `MassLaw_RenderedTargetsMonotonic`, `MassLaw_FundamentalStrengthJointSweep` |
| **SC-002** | Mass anchors (concept 230–246; roadmap 477) | On the Mass reference voice (the stored base = Mass 50, so the Mass 0 and Mass 100 fixtures are the biases of FR-027 applied to that base, not separate absolute voices) at E1 and C3. *Drafted thresholds:*<br>• Mass 0: THD ≤ **−30 dB**, and compressor GR = 0 dB;<br>• Mass 50: THD(50) ≥ THD(0) + **6 dB**, and E_low(50) ≥ E_low(0) + **3 dB**;<br>• Mass 100: THD(100) ≥ THD(50) + **6 dB**, and steady GR(100) ≥ **3 dB**. | `MassLaw_AnchorPoints` |
| **SC-003** | The perceived-level slope is bounded: Mass is "not a volume knob" (483–484) | Over 101 Mass points on the reference and saw fixtures at E0, E1, E2 and C3 (the pure-sine fixture is **recorded**, not gated: user ruling 2026-10-17, Clarifications "Group 8 harvest ruling" G8-2, by D-4 the sidechain high-pass at 2·f₀ cannot see a sub-only voice at any note, so the FR-016 makeup, 0 → +5.84 dB with compression amount, is applied unopposed and spans ≈ 5.7 dB by itself; the recorded reading is the finding that Mass adds up to that much level to a sub-only voice):<br>**Both metrics are gated (ruled, OQ-1 / Q3), each where it measures the signal (plan C-1, user ruling 2026-10-17, Clarifications "Plan S1 rulings"):**<br>• at **E2 and C3**, the span of the K-weighted (BS.1770) level P is ≤ **3 dB** and, between adjacent points, \|ΔP\| ≤ **0.2 dB** per Mass unit; at **E0 and E1** P and its span are **recorded** (the RLB high-pass reads a sine 12.9 dB and a saw 5.9 dB below their unweighted level at E0, 2.4 dB apart at E1, so a Mass that travels from sine-like to rich without the level falling, as SC-001(c) and SC-002 require, spans ≈ 9 dB at E0 and ≈ 3.9 dB at E1 by the metric alone);<br>• at **every note**, the span of the unweighted RMS level L is ≤ **6 dB** (this catches sub-level drift that K-weighting under-reads below about 38 Hz).<br>Harvest outcome (2026-10-18, M-1): with the Linear table the reference voice at C3 read \|ΔP\| 0.2178 dB per unit at Mass 54 → 55 (the Fold row's linear onset); the plan S8.2 bend step, now measured at every Mass unit, adopted **Fold SCurve** (the first candidate): Ref C3 0.1303, Ref E2 0.1204, Saw E2 0.0746, Saw C3 0.0711 dB per unit; spans 1.70 / 1.39 / 1.98 / 1.79 dB; SC-001(c) worst fall −4.66 dB; SC-002 margins kept (`evidence/mass-law-table.txt`). | `MassLaw_LevelSlopeBounded` |
| **SC-004** | Bounded slew (478) | On a Mass 0 → 100 step (plan C-7, user ruling 2026-10-17, Clarifications "Plan S1 rulings": a half sweep, 50 → 100, takes the D-7 minimum of 20 ms; the full 0 → 100 traversal takes **40 ms**; no row is capped):<br>• every target's change per control block (the FR-049 32-sample interval, not the host block) is ≤ its full range × (32 / fs ÷ **20 ms**) + a float margin of 1e-6 × the range (the bound is met with exact equality by construction);<br>• the traversal completes within **40 ms** ± one control interval;<br>• both hold identically at host block sizes {1, 17, 64, 512}. | `MassLaw_SlewBounded` |
| **SC-005** | No discontinuity in any descriptor across 0–100 at automation speed (485–486) | **(a) Clicks.** Mass steps 0 ↔ 100 and 20 ms ramps, at block sizes {64, 512}, on the validation chain at E0, E2 and C3 with `kSawAnchor`: the differential click count is **0**, with the positive controls flagged (the hard +12 dB switch gates; it is applied at the first sample from the step sample on that lies ≥ 2·N from every reference detection and whose forced jump is ≥ 10 × the references' local max \|Δy\|, within four periods of the step — compliance-remediation harness repair 2026-10-17 under the Phase 5 rule "positive controls are harness construction; they are repaired, not re-ruled" (profundum-phase5-shape-character/spec.md, Clarifications "Group 9 rulings"): `kSawAnchor` carries one ClickDetector detection per period on each reference, and on the E2 100 → 0 arms the step sample itself lay within ±N of one, so the control at the step sample read 0 counts; the unsmoothed-Mass step is recorded — main-loop refinement 2026-10-17, Clarifications G8-7: every target is smoothed downstream of the law, so once the harvested amount table replaced the seed an unsmoothed Mass step produced no counted click).<br>**(b) Static lattice.** Between adjacent steps of a 101-point static Mass sweep (step 1, the Phase 5 SC-002 lattice, profundum-phase5-shape-character/spec.md:624), on the same chain and notes: \|ΔL\| ≤ **0.5 dB**, \|ΔC\| ≤ **0.1 oct**, and \|ΔE_low\|, \|ΔE_high\| ≤ **1 dB** (the Phase 5 SC-002 bars). Every other SC-001(b) descriptor (w_1/Σw, THD, w_1 of the sum, GR) has its adjacent-step change recorded, as the slope input to (c).<br>**(c) Automation-speed trajectory.** The 0 → 100 ramp at the D-7 traversal time, and the 20 ms ramp if that differs, rendered on the chain at E0, E2 and C3. Every SC-001(b) descriptor (w_1/Σw, E_low, E_high, THD, C, L, w_1 of the sum, GR) is computed over one-period windows that slide one period at a time along the ramp; each window is band-limited-resampled (Kaiser-windowed sinc, half-width 32 samples, β = 10) onto ⌊P⌋ points before its harmonic DFT (user ruling 2026-10-17, Clarifications "Group 6 gate ruling": a rectangular fractional-period DFT leaks at the window boundary and floors THD near −50 dB on a pure sine, varying with the window start; resampled, the model reads −122 dB and the saw within 1e-4 dB of analytic). The helper's self-test gates L, E_low and THD against `describe()` within 0.1 dB after the OQ-S −60 dB floor clamp on both readings, C within 0.005 oct, and the sine's raw THD ≤ −70 dB. Each per-window change is ≤ the largest adjacent-step change of that descriptor in (b), scaled by the Mass units the ramp traverses per window, plus the measured repeat noise of (b). | `MassLaw_AutomationContinuous`, `MassLaw_AutomationDescriptorTrajectory` |
| **SC-006** | Sub-band gain modulation under compression ≤ the ruled bound: no pumping (484) | Compressor at amount 1 on the Mass reference voice at E0, E1, E2 and C3, through the ratified D-6 detection form (FR-011 (b)) with the keytracked corner of FR-018 (corner = 2·f₀ of the note under test):<br>**(a)** steady-state M_sub ≤ **0.5 dB** peak-to-peak (ruled, OQ-2 / Q4);<br>**(b)** an Anchor level step 0.25 → 1, with the body unchanged, moves steady GR by ≤ **1 dB** (ruled). This shows the sub does not drive the detector;<br>**(c)** positive control: the same cells with the sidechain high-pass disabled exceed the (a) or the (b) bound at E0;<br>**(d)** engagement floor: steady GR ≥ **3 dB** (the SC-002 Mass 100 bar) at every note, so a starved detector cannot pass (a) and (b) vacuously;<br>**(e)** negative control: the forbidden feed, max(\|L\|, \|R\|) of the unfiltered channels into the internal sidechain, must fail (b) at E2 or (d) at E0 / E1. Each note's verdict is flagged. If it passes everywhere, the fixture lacks teeth, and that is reported. | `MassCompressor_NoSubPumping`, `MassCompressor_RectifiedFeedNegativeControl` |
| **SC-007** | Program-dependent release (472) | Compression amount **1**, `kSawAnchor` at E1 and C3. Two events from a steady base at the amount-1 threshold: a 20 ms overshoot and a 1 s sustained overshoot, each **+12 dB above the amount-1 threshold** as read at the detector (after the sidechain high-pass).<br>• Rule under test (ruled, Q8): release = 50 + 450 · f ms, where f is the fraction of the last 300 ms spent beyond 1 dB of smoothed GR, updated once per 32-sample interval. The law tap shows the release at f = 0 is 50 ms and at f = 1 is 500 ms.<br>• Recovery is measured from the **end of each event's own GR plateau** (the last sample within 0.1 dB of its peak GR; plan C-13, user ruling 2026-10-17, Clarifications "Plan S1 rulings": on the 1 s event the peak is reached inside the 10 ms attack and the plateau then carries 0.01–0.04 dB of ripple, so "first sample of the peak" would count the whole plateau as recovery) to the point where GR has recovered by half of that peak (in dB), so the depth each event reached does not drive the result. The short event's recovery time is ≤ **½** (*drafted*) of the sustained one's.<br>• Positive control: the same two events with the rule disabled (a fixed release at the rule's midpoint, 275 ms under D-5) must fail the ½ bar.<br>• The release rule's output is monotone in its programme measure (law tap). | `MassCompressor_ProgramDependentRelease`, `MassCompressor_FixedReleasePositiveControl` |
| **SC-008** | Transient attack / sustain gains match their parameters (485) | Burst fixture, sine and `kSawAnchor`, at E1 and C3:<br>**(a)** over a 5 × 5 grid of (attack, sustain) settings, the steady-state gain is within **±0.25 dB** of the sustain gain;<br>**(b)** at sustain 0 dB, on a note onset from silence, the peak gain during the onset is within **±0.5 dB** of the **full** attack gain (FR-008: c = 1 for an onset from silence);<br>**(c)** at attack **+12 dB**, sustain **0 dB**, over 5 durations across the D-3 range (5…250 ms): the transient time (from onset until the gain falls 3 dB below its onset peak) equals the duration setting within **±15 %** (the 5 ms cell is gated at **C3 only** and recorded at E1: a sine at 41 Hz delivers its first peak 6.07 ms after onset, so no amplitude detector can report a 5 ms transient there, model +30 %; plan C-2, user ruling 2026-10-17, Clarifications "Plan S1 rulings"; the harvested 7.5 ms cell is recorded on the E1 sine for the same reason, +19.17 % measured, its other three cells −7.50 / −1.39 / −7.78 %, user ruling 2026-10-18, Clarifications "Group 9 close rulings" G9-2; every other harvested cell is within ±15 %, `evidence/duration-table.txt`) (ruled, Q5: identity function, realised by the FR-008 measured calibration table, which is harvested and recorded with its evidence file);<br>**(f)** gain law identity: at gains (attack, sustain), the applied gain follows `attack · c + sustain · (1 − c)` with K = 12 dB, read from the diagnostic tap (FR-006) and compared with the law evaluated from the detector tap, within **±0.1 dB** (*drafted*); sustain gain applies outside the transient;<br>**(d)** for a step change of each setting, the differential click count is 0;<br>**(e)** silence pre-roll: on the 2 s silence pre-roll arm, the onset's peak gain is within **±0.5 dB** of the attack gain, and no sample of g(t) exceeds attack gain + 0.5 dB. This shows there is no gain spike after silence. | `TransientShaper_GainsMatchParameters` |
| **SC-009** | Bass-safe shaper detection (FR-004) | Steady sine and `kSawAnchor` at E0, E1 and E2, at every grid setting of SC-008: the steady-state ripple of the gain trace is ≤ **0.2 dB** peak-to-peak (*drafted*). | `TransientShaper_NoWaveformRateRipple` |
| **SC-010** | Anti-aliasing at E0–E2 (890) | Settings: the shaper at its fastest duration with attack at ±max; the compressor at amount 1 with the fastest attack / release the table uses. Inputs: `kSawAnchor` (**gated**) and Fold-100 `kSawAnchor` (**recorded**, FR-048 measurement; user ruling 2026-10-18, Clarifications "Group 9 close rulings" G9-1: a gain that moves within 5 ms on an input whose content reaches Nyquist folds its sidebands back, −63 … −78 dBFS at 44.1 / 48 kHz and −75 dBFS at 96 kHz on the moving windows; and at E2 the 4× true-peak estimate of the Fold-100 wave alternates ≈ 0.27 dB with the sub-sample phase of its 582.47-sample period, so the compressor gain wobbles at f₀ / 2: steady −46 / −55 dBFS at 44.1 / 48 kHz; the plain-saw cells pass with ≥ 14 dB of margin at every rate; the code fix, 2× oversampled gain application plus 8× detectors, costs ≈ +0.5 % of a core against SC-017 and is a Phase 9 chain-budget decision), at E0, E1 and E2, at fs ∈ {44.1, 48, 96} kHz.<br>The gate is on the aliasing the dynamics **adds**: on the same render frame, A_out ≤ max(**−80 dBFS** (the Phase 2 / Phase 5 bar), A_in + **0.5 dB**). The A_in and A_out totals are recorded per cell.<br>Windows (plan C-3, user ruling 2026-10-17, Clarifications "Plan S1 rulings"): a steady window of one `aliasFftLength` frame starting 1 s after onset (the frame is 5.46 s at E0, so [1 s, 2 s] cannot hold it), plus two windows where the gain moves, the burst onset [0, 100 ms] and the two-level step's transition [step, step + 100 ms], each measured as the **residual against an ideal band-limited product of input and gain** (the gain-modulation sidebands any working compressor produces are not aliasing), gated at ≤ −80 dBFS, with a positive control that proves the residual sees real aliasing (the hard 0 → +12 dB step on the **Fold-100** C3 saw; main-loop harness repair 2026-10-18, Clarifications "Group 9 close rulings" M-2: on the plain saw the control read −75.8 / −76.5 / −82.5 dBFS at 44.1 / 48 / 96 kHz, under the −80 dBFS bar at 96 kHz by construction, because the saw's near-Nyquist content there is 20 dB lower); over leading exact-zero input the recorded gain trace is held at its value on the first non-zero sample before band-limiting, because a gain applied to a zero sample is unobservable (main-loop refinement of C-3, 2026-10-17, Clarifications "Group 7 gate rulings": the shaper's silence-branch gain step at onset otherwise rings in the ideal product and reads −67 dBFS on an output that is exactly gain × input). The plan records each window's FFT length and resolution. | `MassDynamics_AliasingE0E2` |
| **SC-011** | Audibility gates (885) | On the Mass reference voice at E1:<br>• Mass 0 → 100 separates the endpoints by the Phase 5 OQ-S bar: ≥ 3 dB on E_low, E_high, THD or I, or ≥ 0.25 oct on C;<br>• each lever alone moves its SC-001(b) / SC-008 descriptor by ≥ **3 dB** (or 0.25 oct). The levers are each Mass target (the FR-021 output trim is a calibration lever and is recorded, not gated, G8-5; the compression lever runs with the other targets at their Mass 50 base values, G8-7), the shaper's attack and sustain gain over their ranges, and compression amount 0 → 1. The duration lever is read as D_tr at the FR-009 defaults (attack +6 dB, sustain 0 dB). The inert case at attack = sustain = 0 dB is recorded, not gated;<br>• compression is gated on a rendered dynamic descriptor, not on the processor's internal GR: on the two-level step fixture, ΔL_step at amount 1 is ≥ **3 dB** smaller than at amount 0. Steady GR (`getCurrentGainReduction`) is recorded only. | `MassLaw_Audibility`, `TransientShaper_Audibility`, `MassCompressor_Audibility` |
| **SC-012** | Mono-sum (888) | **(a)** For the shaper and the compressor, at every grid point: L == R at the input gives L == R at the output, bit-identical.<br>**(b)** Stereo input (a `kSawAnchor` pair at 1 : 0.5 L/R): the applied gain is identical on L and R at every sample, and the side / mid energy ratio stays within **0.01 dB** of the input's. | `MassDynamics_MonoSum` |
| **SC-013** | Transparent at neutral | The shaper at 0 / 0 dB and the compressor at amount 0. From max(100 ms, 5 × the longest smoother time) onward, the output is bit-identical to the input. | `MassDynamics_TransparentAtNeutral` |
| **SC-014** | Validation chain: core + Anchor through the Mass dynamics and the output safety stages (478–481) | FR-033 fixture at Mass ∈ {0, 25, 50, 75, 100}, at E0–E2 and C3, 48 kHz:<br>**(a)** tap assertions: the Anchor's `level` is the only Anchor field Mass writes, and the fed a₁ is Mass-independent (bit-equal across the Mass points of a note) and equals the delivered core h1 at Mass 50 (no bias) within 0.05 dB, with the delivered-to-fed ratio at the other Mass points recorded (FR-031 as amended, user ruling 2026-10-17, Clarifications "Group 8 harvest ruling" G8-3);<br>**(b)** the core's output is bit-identical with the Anchor attached and detached (Phase 3 FR-025);<br>**(c)** the output is finite, and \|out\| ≤ the limiter ceiling at sample level;<br>**(d)** DC ≤ **−80 dBFS**;<br>**(e)** no hidden compensation, measured separably. The Anchor's own output buffer is read from a test tap before the sum, and its RMS ÷ (a₁/√2) is monotone non-decreasing in Mass's Anchor-level target. The summed signal equals the float add of the core and Anchor taps, bit-identically (`std::memcmp`, as Phase 3 SC-024(d)), so nothing between the two taps and the sum rescales either one. The chain-output w_1 is recorded only: in LOCK it mixes the core's h1 and the Anchor with a phase-dependent ratio (Phase 3 SC-009, profundum-phase3-anchor/plan.md:846), and the compressor is nonlinear. | `MassChain_CoreAnchorValidated` |
| **SC-015** | The limiter is idle in steady state (FR-034) | Same fixture, sustained notes, at every Mass on the reference and saw fixtures; the pure-sine fixture is **recorded** (user ruling 2026-10-17, Clarifications G8-6: a sub-only voice is invisible to the 2·f₀ sidechain, so at Mass 100 it carries the full +5.84 dB makeup with no gain reduction on top of the Anchor doubling its fundamental and sits above the ceiling: steady GR −1.43 / −0.22 / −0.12 / −0.50 dB at E0 / E1 / E2 / C3). `TruePeakLimiter` has no GR getter (its only observer is `getCeilingLinear()`, true_peak_limiter.h:98), so GR is read from the limiter's input and output buffers: GR = 20·log₁₀(\|out\| / \|in\|) on samples with \|in\| > 1e-4.<br>**(a)** From 1 s after note-on, GR ≥ **−0.01 dB** on every such sample. This tolerates the release's float floor (FR-034).<br>**(b)** No-touch cells: wherever the limiter never engaged from note-on (no sample with GR < 0, recorded per cell), out == in bit-equal from 1 s on.<br>GR at note attacks (peak and duration) is recorded. | `MassChain_LimiterIdleSteadyState` |
| **SC-016** | RT safety (883) | `AllocationScope` reports 0 allocations across `processBlock`, every setter, `reset`, a Mass step and the release rule, for the shaper, the compressor composition and `MassLaw`. | `MassDynamics_NoAllocation` |
| **SC-017** | CPU ≤ 1 % global (485, 893) | Setup: 48 kHz, stereo. The measured block is the Mass dynamics: the shaper, the compressor composition at amount 1, and the per-block Mass evaluation. It runs in the isolated CPU lane as the single case pinned through `tools/pin-perf-cores.ps1` (tagged `[performance]`; `node tools/run-cpu-tests.js dsp_systems_tests` would also run every `[long]` case of the executable, ≈ 5 h; user ruling 2026-10-17, Clarifications "Plan S1 rulings").<br>The release rule runs at the rate FR-012 / FR-049 fix (once per 32-sample interval, or the plan's recorded alternative), and the measurement uses that rate.<br>Bar: ≤ **1.0 %** of one core. `DCBlocker` + `TruePeakLimiter` are recorded separately for Phase 9. The shipped code first read 1.047–1.270 % (isolated, pinned); the per-sample dB conversions were replaced by their `std::log2` / `std::exp2` forms (FR-045, M-3, 2026-10-18; behaviour unchanged within float rounding) and the gated cell profiles at 0.802 %. | `MassDynamics_CpuBudget` |
| **SC-018** | Determinism and partition invariance | Block sizes {1, 17, 32, 64, 512}, with a Mass step and a release-rule change mid-render:<br>• the output is bit-identical across partitions (FR-049 puts every control update on the same absolute sample);<br>• two renders from `reset()` are bit-identical;<br>• no random source exists. Any that is added has a fixed seed that `reset` restores. | `MassDynamics_PartitionInvariant` |
| **SC-019** | Non-finite safety | A 16-sample NaN / ±Inf burst at the input of the shaper and the compressor, plus non-finite Mass and base values:<br>• every output sample is finite (bit-pattern test), and each output sample of a channel whose input was non-finite is exactly 0.0f (FR-007, FR-017);<br>• the state is finite within 1 block after the burst;<br>• within 100 ms, L returns within **0.1 dB** of the pre-burst steady state for the shaper-alone and compressor-alone arms; for the whole chain (shaper → compressor) the recovery point is **100 ms past the longest D-5 release, 600 ms** after the burst, bar still 0.1 dB, with the 100 ms reading recorded and the bursts spaced 1 s apart; in both arms every burst onset is snapped to the saw's positive peak within the period after its nominal time, the worst-case dropout, so the ruled premise holds at any spacing (user ruling 2026-10-17, Clarifications "Group 7 gate rulings": a NaN burst on the saw's peak is a real dropout; FR-007 makes the detectors see the zeros, the shaper answers +1.47 dB for 60 ms and the compressor's program-dependent release then needs about 235 ms to return within 0.1 dB). | `MassDynamics_NonFinite` |
| **SC-020** | Shared suites, portability, tidy (897–901) | `dsp_processors_tests`, `dsp_systems_tests` and the Profundum suites are green **within the change set** (user ruling 2026-10-18, Clarifications "Group 9 close rulings" G9-4): the per-push lanes of both executables, every `[long]` case of the Phase 6 TUs, and the full suites of every consumer of a shared header the phase modified; `[long]` cases of untouched code are not part of the gate, and a timing-budget assertion of such a case failing under a concurrent lane is recorded as load, not re-run. Either `git diff` shows no change to any reused header, or FR-044's suites are green with their tails quoted. check-portability and clang-tidy are clean. | — (comply) |
| **SC-021** | Sample-rate behaviour | At 44.1, 48, 88.2, 96 and 192 kHz:<br>• the output is finite and bounded;<br>• at E1, the SC-008 gains and the SC-006 M_sub agree with the 48 kHz values within **0.25 dB**;<br>• a render after prepare(48k) → prepare(96k) → prepare(48k) equals a fresh 48 kHz render. | `MassDynamics_SampleRates` |
| **SC-022** | Makeup offsets only the compressor's own GR (FR-016, FR-021) | For every row of the compression-amount table, and for amount ∈ {0, 0.25, 0.5, 0.75, 1}, on the Mass reference voice at E1 and C3: \|makeup(amount) − measured steady GR(amount)\| ≤ **0.5 dB**. Amount 0 has makeup 0 dB. | `MassCompressor_MakeupMatchesGr` |
| **SC-023** | Mass 50 is neutral (FR-027, ruled Q1) | For each of the three fixture bases, `MassLaw::evaluate(50, base)` returns all seven targets bit-equal to the base values (law level). Rendered: the Mass reference voice at Mass 50 through the FR-033 chain with the compressor at base amount 0 matches the same chain with the Mass law detached (every target written directly from the base): the shaper and compressor stages are bit-identical when at neutral, and the voice-side descriptors (w_1/Σw, E_low, THD, L) agree within the SC-001(b) repeat noise. Clamping at the range edges (a base at a range end with a pushing bias) is exercised and recorded. | `MassLaw_NeutralAtFifty` |
| **SC-024** | Keytracked sidechain corner (FR-018; ruled Q7) | **(a)** corner = 2·f₀ of the value passed, within **1 %** after the smoother settles (*drafted*); **(b)** inputs of f₀ below 10 Hz and above 250 Hz clamp the corner to 20 Hz and 500 Hz respectively, and the corner is continuous (no step larger than the smoother allows per control interval) across a 48 st Drop sweep; **(c)** with "no voice" (non-positive or non-finite f₀) the corner settles to the fixed fallback value; **(d)** in a POLY fixture of voices at E1 and C3 the engine-side call passes the lower f₀ (the setter takes one value, and the test asserts the corner is 2 × the lower note); **(e)** calling it at the FR-049 rate, then at any block partition, gives bit-identical output (SC-018). | `MassCompressor_KeytrackedCorner` |
| **SC-025** | Shaper parameter ranges and defaults (FR-001, FR-009; ratified D-3) | Attack gain and sustain gain clamp to −12…+12 dB, duration to 5…250 ms; a non-finite argument keeps the previous value; the defaults are attack +6 dB, sustain 0 dB; at 0 / 0 dB the shaper is the identity (SC-013); no Mass-law output writes the attack or sustain gain (the law's six outputs contain no such field). | `TransientShaper_RangesAndDefaults` |
| **SC-026** | K-weighting helper is correct (FR-050; ruled Q3) | The test-side BS.1770 helper reads a full-scale 997 Hz sine at −3.01 LKFS within **0.1 dB** (the BS.1770 expected value, K-weighting gain at 997 Hz being about 0 dB), and a sine at E1 (41.2 Hz) of the same unweighted level reads at least **1 dB** lower than the 997 Hz sine (the roll-off below about 38 Hz is present). | `KWeighting_ReferenceLevels` |

---

## Edge Cases

- **Silence.** `gainToDb(0)` is −144 dB (db_utils.h:306–319). The compressor then applies 0 dB GR, and the shaper's
  detector pair decays to zero without a gain spike on the next onset. SC-008(e) covers an onset after 2 s of silence.
- **Lowest notes.** At E0 the period (48.5 ms) is longer than the shaper's shortest detector times and the compressor's
  fastest attack. FR-004, SC-006 and SC-009 are the guards. Keytracked cutoffs (D-4) clamp continuously to 20–500 Hz.
- **Drop sweeps.** A 48 st Drop moves f₀ through the sidechain HP corner. GR across a Drop is recorded; Phase 9 owns the
  full-voice case.
- **Mass extremes.**
  - Mass exactly 0 and exactly 100.
  - Clamped bias segments where the base sits near the end of a range (FR-024).
- **Mass during a note.** A mid-note Mass change goes through the FR-025 smoother. The Fold and the Anchor level ride
  their own smoothers. No re-arm or retrigger is issued.
- **Block size.** Scratch buffers are sized at `prepare`, and any longer call is split. `TruePeakLimiter`'s chunking is
  the precedent (true_peak_limiter.h:104–118).
- **Sample-rate change.** A new `prepare` rebuilds all coefficients; `EnvelopeFollower::prepare` recomputes them
  (envelope_follower.h:106–126). SC-021 is the gate.
- **Denormals.** FTZ / DAZ is the host's precondition. `EnvelopeFollower` already flushes denormals
  (envelope_follower.h:184–186).
- **`right == left`.** One channel is processed, with channel 0's state.
- **Poly sum.** Four summed voices raise the detector level. The compression table is calibrated on one voice, and the
  4-voice reading is recorded for Phase 9 (FR-035).
- **Seed determinism.** No component in this phase uses randomness. SC-018 asserts it.

---

## Decisions (ruled or ratified by the user, 2026-10-17)

- **D-1 Neutral point and bias form (RULED, Q1).**
  - Every target is `base + amount · curve(·)`, clamped, the Vorago row pattern. The stored base is the user's own
    voice at **Mass 50** ("thick analogue"), and Mass is bipolar about 50. Mass 0 then actively purifies: a positive
    Depth bias toward sine, with Fold and compression clamped toward 0. Mass 100 thickens. All six targets are
    biases. [FR-020, FR-022, FR-027, SC-002, SC-023]
  - *Rejected alternatives:* neutral at Mass 0 (unipolar); Depth / Body only as biases.
  - SC-002's Mass 0 fixture is relative to the stored base.
- **D-2 Target directions (RULED, Q2).**
  - As Mass rises, the Depth bias **decreases** and the other five targets increase (Body bias,
    transient duration, Fold amount, compression amount, Anchor level). [FR-024, SC-001(c)] The FR-021 output trim added
    on 2026-10-17 (G8-5) is non-increasing.
    - The falling Depth bias lowers h1 dominance as the body grows, so Mass 0 is sine-like.
    - The rising Anchor level makes the absolute sub energy of the sum rise even while Depth's ratio falls. That
      absolute sub energy is the "fundamental strength" the concept means.
    - SC-001(c) gates the sum's w_1 over the joint Mass sweep, where the falling Depth bias and the rising Anchor level
      compete, as a bounded fall (≥ its Mass 0 value − 6 dB; user ruling 2026-10-17, Clarifications "Group 8 harvest
      ruling" G8-4, superseding "non-decreasing").
  - *Rejected alternatives:* a rising Depth bias; fundamental strength as the core's own h1 only.
- **D-3 Shaper ranges (RATIFIED).** Attack gain −12…+12 dB, sustain gain −12…+12 dB, duration 5…250 ms. The shaper
  defaults are attack +6 dB, sustain 0 dB, user-facing (Q6). Mass drives the duration only. [FR-001, FR-009, SC-025]
- **D-4 Sidechain HP (RULED, Q7).** Keytracked to 2·f₀ of the lowest sounding voice, through the FR-018 setter called
  once per control interval, smoothed, clamped 20–500 Hz, fixed fallback corner when no voice sounds. The fixed
  150 Hz corner and the steeper fixed filter are rejected. [FR-011, FR-018, SC-006, SC-024]
- **D-5 Release rule (RULED, Q8).** The release is interpolated between 50 ms and 500 ms by the fraction of the
  last 300 ms spent beyond 1 dB of gain reduction (1 dB ratified), updated once per FR-049 interval. [FR-012, SC-007]
- **D-6 Shared-gain mechanism (RATIFIED).** One shared `DynamicsProcessor` instance; one gain applied to both
  channels; look-ahead 0 (`setLookahead` allocates). The combined detection signal is max(|L|, |R|) taken **after**
  the sidechain high-pass on each channel (FR-011 form (b)); the unfiltered max(|L|, |R|) into the internal sidechain
  stays struck (FR-011). Form (a), the signed mid, is not adopted. SC-006(e) is the negative control.
  [FR-011, FR-013, FR-014, SC-006, SC-012]
- **D-7 Mass slew (RATIFIED).** A full Mass sweep takes ≥ 20 ms, twice Phase 5's 10 ms SHAPE ramp, through a linear
  `SlewLimiter`. [FR-025, SC-004]

## Ruled questions (deferred to this spec by the roadmap)

- **OQ-1 Perceived-level range (RULED, Q3).** Both gated: K-weighted (BS.1770) span ≤ 3 dB with |ΔP| ≤ 0.2 dB per
  Mass unit, and unweighted RMS span ≤ 6 dB. A test-side K-weighting helper is added; no product code changes.
  [FR-028, FR-050, SC-003, SC-026]
- **OQ-2 Pumping bound (RULED, Q4).** Steady-state M_sub ≤ 0.5 dB peak-to-peak at E0, E1, E2 and C3, and a sub-only
  Anchor-level step 0.25 → 1 moves GR by ≤ 1 dB. [FR-019, SC-006]

---

## Traceability

| Roadmap statement (line) | FRs | SCs |
|---|---|---|
| `TransientShaper` from a fast / slow `EnvelopeFollower` pair (470–471) | FR-001–FR-009 | SC-008, SC-009, SC-011, SC-025 |
| Bass compressor configuration of `DynamicsProcessor` (472) | FR-010, FR-013–FR-016, FR-018 | SC-006, SC-024, SC-013, SC-022 |
| Program-dependent release (472) | FR-012, FR-049 | SC-007, SC-017 |
| Sidechain HP so the sub never pumps (472–473) | FR-011, FR-018, FR-019 | SC-006, SC-024 |
| Mass law: documented, monotonic, calibrated, six targets (474–477) | FR-020–FR-024, FR-027 | SC-001, SC-002, SC-023 |
| Continuous, with bounded slew (478) | FR-025, FR-026, FR-049 | SC-004, SC-005, SC-018 |
| The voice supplies a₁; the sum is validated through the Mass dynamics and output safety, with no Anchor compensation (478–481) | FR-030–FR-034 | SC-014, SC-015 |
| Every target monotonic (483) | FR-024, D-2 | SC-001 |
| Perceived-level slope bounded (483–484) | FR-016, FR-028, FR-050 | SC-003, SC-022, SC-026 |
| Sub-band modulation ≤ the ruled bound (484) | FR-011, FR-019 | SC-006 |
| Transient gains measured against parameters (485) | FR-003, FR-008, FR-009 | SC-008 |
| No discontinuity in any descriptor at automation speed (485–486) | FR-025, FR-049 | SC-005 |
| CPU ≤ 1 % global (485, 893) | FR-045 | SC-017 |
| Audibility, mono and anti-aliasing themes (885–891) | FR-041–FR-043 | SC-010–SC-012 |
| RT safety, portability, shared headers (883, 897–901) | FR-040, FR-044, FR-046 | SC-016, SC-020 |

## Assumptions

- The Mass dynamics block is global and sits after the voice sum (roadmap lines 95, 576). Calibration uses one voice.
- Phase 9 supplies the lowest sounding voice’s f₀ to the FR-018 setter once per control interval (in POLY, the lowest sounding voice), and "no voice" when none sounds.
- The calibration input level is the core's −12 dBFS RMS (`HarmonicFold::kCalibrationPeak`).
- Phase 9 evaluates the Mass law once per FR-049 control interval (32 samples). It pushes the voice-owned targets through the components'
  existing setters: the core's `setControls`, the Fold's `setAmount` and the Anchor's `setControls`.

## Clarifications

### Session 2026-10-17

- Q: Where is Mass neutral, and are the six targets biases on the user's values or absolute values? -> A: Bipolar about
  Mass 50; all six targets are biases on the user's base (base + amount * curve, clamped). Mass 50 leaves the user's
  sound unchanged; 0 and 100 push away. Anchor level stays one control plus a Mass offset (Phase 3 FR-022, Vorago
  macro-row pattern). SC-002's Mass-0 fixture is relative to the stored base. [FR-020, FR-022, FR-027, SC-002, SC-023]
- Q: Which way does the Depth bias move, and what is "fundamental strength"? -> A: The Depth bias falls as Mass rises
  (richer core; Mass 0 sine-like). Fundamental strength is the absolute sub energy of the core + Anchor sum, carried
  by the rising Anchor level; the sum's w1 / sub energy is non-decreasing over the sweep (a calibration constraint on
  the table). [FR-024, SC-001(c)]
- Q: What perceived-level metric and span gate "Mass is not a volume knob"? -> A: Both: K-weighted (BS.1770) span
  <= 3 dB with |dP| <= 0.2 dB per Mass unit, and unweighted RMS span <= 6 dB. A BS.1770 test-side helper is added.
  [FR-028, FR-050, SC-003, SC-026]
- Q: What bound rules "no pumping"? -> A: Steady sub-band modulation M_sub <= 0.5 dB peak-to-peak at E0, E1, E2 and C3
  on a sustained note; an Anchor-level step 0.25 -> 1 moves the compressor's GR by <= 1 dB; SC-006 keeps its positive
  controls. [FR-019, SC-006]
- Q: What is the TransientShaper gain law and what does "duration" set? -> A: Normalised differential:
  gain_dB = attack_dB * c + sustain_dB * (1 - c), c = clamp((fast_dB - slow_dB) / K, 0, 1), K = 12 dB (ratified).
  Sustain gain applies outside the transient per the clamp complement. Duration is the slow detector's attack time,
  calibrated (measured table) so the measured transient time equals the setting within 15 %. Full attack gain on a
  note onset from silence. [FR-002, FR-003, FR-008, SC-008(b), SC-008(c), SC-008(e), SC-008(f)]
- Q: Are the shaper's attack / sustain gains user-facing, and does Mass touch attack gain? -> A: User-facing (registered
  in Phase 11, range 700-799), defaults +6 dB attack / 0 dB sustain. Mass drives duration only (six-target law
  stands). The inert case at attack = sustain = 0 dB is recorded, not gated; audibility is measured at the defaults.
  [FR-009, FR-021, FR-035, SC-011, SC-025]
- Q: Fixed or keytracked sidechain high-pass? -> A: Keytracked to 2*f0 of the lowest sounding voice, via an
  engine-facing setter called once per control interval, smoothed, clamped 20-500 Hz, fixed fallback corner when no
  voice sounds. Phase 9 handoff: the engine calls it every control interval; in POLY the lowest sounding voice's f0
  is used. SC-006 is gated at E0-E2 and C3 on it. [FR-011, FR-018, FR-035, SC-006, SC-024]
- Q: What is the program-dependent release rule? -> A: Release interpolated 50 -> 500 ms by the fraction of the last
  300 ms spent beyond 1 dB of GR (1 dB ratified), updated once per 32-sample control interval (FR-049 rate). SC-007
  separates a 20 ms event from a 1 s event under it. [FR-012, FR-049, SC-007]
- Ratified as drafted: D-3 shaper attack and sustain gains each -12..+12 dB, duration 5-250 ms. [FR-001, FR-009,
  SC-025]
- Ratified as drafted: D-6 one shared compressor instance, one gain applied to both channels, look-ahead 0
  (`setLookahead` allocates). The user's wording names the combined detection signal max(|L|,|R|); it is encoded as
  FR-011 form (b), max(|HP(L)|,|HP(R)|) with the high-pass applied first, because the unfiltered max feed is struck by
  FR-011 and SC-006(e). [FR-011, FR-013, FR-014, SC-006, SC-012]
- Ratified as drafted: D-7 a full Mass sweep takes >= 20 ms (slew smoother). [FR-025, SC-004]
- Ratified values: K = 12 dB (Q5) and the 1 dB GR threshold (Q8) are encoded as values in their FR rows; the plan
  calibrates the shaper duration table and the compression-amount table against them. [FR-008, FR-012, FR-016,
  FR-022]

## Review notes

- 2026-10-17 spec review: all eighteen issues were applied. None was rejected and no threshold was relaxed. The two
  reports on the rectified detector feed (FR-011 / D-6) were merged into one resolution: the high-pass sees a signed
  signal, the unfiltered max(|L|, |R|) feed is struck, and SC-006(d)/(e) add the engagement floor and the negative
  control. The makeup issue took the strict form (FR-016, SC-022) instead of an open question.

### Session 2026-10-17 — Plan S1 rulings (user)

Plan S1 flagged five bars ⚠ RE-RULE from its sizing model (S12); the user ruled each the recommended option, and ratified the
refinements C-4 … C-12 and three task-list decisions as drafted. Every ruling is in its row above.
- **C-1 (SC-003):** the K-weighted span and per-unit slope gate at E2 and C3, where BS.1770's RLB high-pass measures the
  signal; E0 / E1 K-weighted levels are recorded; the unweighted 6 dB span gates at every note. Rejected: note-specific
  wider K-weighted bars; keeping 3 dB at E0 (fails by the metric's roll-off).
- **C-2 (SC-008(c)):** the 5 ms duration cell is gated at C3 and recorded at E1 (the E1 sine's first peak arrives 6.07 ms
  after onset). Rejected: dropping 5 ms everywhere; keeping the bar.
- **C-3 (SC-010):** steady window = one aliasFftLength frame from 1 s; moving windows measured as the residual against an
  ideal band-limited input × gain product, −80 dBFS, with a positive control.
- **C-7 (SC-004 / FR-025):** D-7's 20 ms is a half-sweep time; a 0 → 100 traversal takes 40 ms; the per-interval bound
  derives from 20 ms; no row is capped. Rejected: capping Fold and compression at half range; doubling the slew.
- **C-13 (SC-007):** recovery starts at the plateau end (last sample within 0.1 dB of peak GR). Model under the rule: 0.227
  (E1) and 0.201 (C3) against ≤ 0.5; the fixed-release control 1.02 / 1.00 fails as it must.
- **Ratified as drafted:** C-4 `PeriodPeakDetector` as a public copy of HarmonicFold's private detector (HarmonicFold is
  not touched, FR-044; its later adoption is a follow-up); C-5 slow follower ticked once per 8 samples; C-6 detection chain
  (keytracked HP → true-peak → max(L, R) → one-period hold → DynamicsProcessor Peak); C-8 … C-12 as written; SC-017 as the
  single case pinned through pin-perf-cores.ps1; SC-004's 1e-6 × range float margin; harvests at the T019 checkpoint with
  table-dependent tests expected red until then.

### Group 2 gate ruling (build, 2026-10-17)

- **G2-1 (C-4 `PeriodPeakDetector`, D-4 fixture; FR-004 row amended):** the wide-saw arm (481 / 524 / 1048 partials to 0.45 fs) read a
  held-value ripple of 1.43e-3 / 1.44e-3 / 7.39e-4 at 44.1 / 48 / 96 kHz against the 1e-4 copied from `HarmonicFold_DetectorSteadyOnNonIntegerPeriod`;
  the 64-partial arm read ≤ 1.25e-5. HarmonicFold's test reads its follower output at a 125 ms hold, and the follower only releases
  below env·(1 − 0.005) (harmonic_fold.h:99, :433), so it hides raw ripple up to 5e-3; `PeriodPeakDetector` has no follower by design and
  its consumers hold 50 ms (shaper) and one period (compressor; 2.0e-3 at E1, recorded). Ruled (recommended option): the wide arm is gated
  on the raw held value at **≤ 2.3e-3 = 0.02 dB peak-to-peak**, one tenth of SC-009's 0.2 dB gain-trace bound, at the consumers' 50 ms
  hold; the 64-partial arm keeps 1e-4. The 1e-4 figure was never a spec SC. Rejected: gating the wide arm at a 125 ms hold (a
  configuration no Phase 6 consumer uses); recording the wide arm (loses the Gibbs stress case); 8× true-peak oversampling (breaks the
  R-11 line-for-line copy and adds CPU to both consumers, unmeasured).

### Group 6 gate ruling (build, 2026-10-17)

- **G6-1 (T011 `periodDescriptors`, SC-005(c) row amended):** the one-period estimator as drafted (a rectangular single-bin DFT over a
  fractional period with a weighted end sample) leaks the boundary sample into every bin when the period is not a whole number of
  samples. On a pure sine its THD floor reads about −50 dB (E1 −54 … −73 dB, C3 −47 … −53 dB across window starts) while `describe()`
  reads the float floor, so the T011 self-test's 0.1 dB agreement on E_low / THD could not be met on the sine arm; the saw arm and L / C
  passed. The floor would also bias and add window-start noise to T015's THD / E_high trajectories on sine-like voices (Mass 0 allows THD
  anywhere below −30 dB). Ruled (recommended option): keep the spec's one-period windows and resample each fractional period onto ⌊P⌋
  points with a Kaiser-windowed sinc (half-width 32, β = 10) before an exact periodic DFT; the self-test bar is kept on every arm, both
  readings clamped at the OQ-S −60 dB floor, plus a raw sine THD ≤ −70 dB floor check. Model: sine THD −122 dB at E1 and C3 at four window
  starts; saw E_low / E_high / THD within 1e-4 dB of the analytic 1/n² values. The estimator now reads 32 samples either side of the
  window. Rejected: gating L and C only on the sine (leaves the floor in T015); a −60 dB clamp alone (C3 THD still −47 dB); a windowed
  multi-period DFT (conflicts with the one-period windows and the sub-band helper's formula).

### Group 7 gate rulings (build, 2026-10-17)

- **G7-1 (SC-014(c) / SC-021, fixed by the gate fixer, reused header):** `TruePeakLimiter::processChunk` multiplied a sample equal to the
  detected peak by ceiling / tp and float rounding put it one ulp (5.96e-8) above the ceiling, against the header's own exact guarantee.
  The output is now clamped to ±ceiling (a no-op at gain 1); reproduced first in `true_peak_limiter_test.cpp`. FR-044 suites re-run green
  (processors, effects, Seraphis, Membrum, Vorago).
- **G7-2 (SC-010 row amended; main-loop measurement refinement under C-3, told to the user):** the burst-window residual read −67.08 dBFS
  at E1 / 48 kHz. Split by a temporary probe: shaper gain alone −67.09, compressor gain alone −157.98. The shaper's silence branch (plan
  S4.3 step 5) steps the gain 0 → +12 dB at onset while every earlier input sample is exactly 0, so the output is exactly gain × input;
  the band-limited gain trace rings at that step and the ringing lands in the "ideal" product. Refined: `aliasResidualDb` holds the gain
  trace over leading exact-zero input at its value on the first non-zero sample. Output unchanged; the fixer's probe read −120.67 dBFS.
  Rejected: pre-arming the attack gain in silence (deviates from plan S4.3 / FR-008); recording the burst window.
- **G7-3 (SC-019 row amended; user, recommended option):** burst 3 (NaN on both channels, landing on the saw's peak) read −0.42 dB at
  100 ms on the whole chain. FR-007 (detectors see 0.0f) → true-peak overshoot +0.98 dB for one 50 ms hold → shaper +1.47 dB for 60 ms
  (FR-008) → compressor GR 0.75 dB deeper, releasing under D-5 (0.51 dB at 100 ms, within 0.1 dB at about 235 ms). Each stage alone
  passes. Ruled: the chain arm measures at 100 ms past the longest D-5 release (600 ms), bursts 1 s apart, 100 ms recorded; the
  single-stage arms keep 100 ms. Rejected: detectors skipping sanitised samples (changes FR-007, adds audio-path logic); recording the
  arm; a point tied to the measured 235 ms.

### Group 8 harvest ruling (build, 2026-10-17)

- **G8-1 (T019 `MassLaw_TableHarvest` STOP AND SURFACE, R-6 / FR-021; FR-021 row amended):** with the S8.2 seed (Depth ±0.5, Anchor
  0.5 / 0.5) no table met SC-001(c): on the reference voice at every note the sum's w₁ rose from Mass 0 to 30 and then fell 21 dB by
  Mass 100 (worst adjacent step −4.5 dB against 0.1 dB); the harvest drove the Anchor to both interval ends and the Fold amountUp to 0
  without effect, and the THD(100) − THD(50) step still read 17.7 dB against 6.5 because Depth → 0 alone makes the core that rich. Root
  cause: the Phase 3 Anchor's level multiplies the core's h1 amplitude (anchor_oscillator.h:340), so it adds at most (1 + level): +3.5 dB
  over Mass 0–50 and +2.5 dB over 50–100, while the seed's Depth travel removes about 20 dB of h1. Also red on the seed: SC-003
  K-weighted span(P) 3.3 / 3.7 dB at E2 / C3 (bar 3) and the pure-sine E0 unweighted span(L) 8.2 dB (bar 6), 6.0 dB of which is the
  Anchor doubling a 20.6 Hz sine the sidechain high-pass at 2·f₀ cannot see. Ruled (recommended option): Depth and Anchor travel become
  harvest levers in the order the FR-021 row states; directions and the six targets unchanged. Rejected: capping the Depth bias by hand
  (guesswork); re-ruling Q2 to allow w₁ to fall (relaxes the bar); an absolute Anchor level in the chain (contradicts FR-032 and Phase 3,
  cross-phase).
- **G8-2 (second harvest run under the new order; SC-001(c) and SC-003 rows amended):** two structural conflicts remained. (1) By D-4 the
  compressor's sidechain high-pass sits at 2·f₀, so the pure-sine fixture is invisible to it at every note and the FR-016 makeup (0 → +5.84 dB)
  is applied unopposed: Sine span(P) 5.65 dB at E2 / C3 (bar 3), span(L) 6.0 dB; step (0) squeezed the Anchor travel to 0.18. (2) With that
  Anchor the sum's w₁ at the chain output still fell with zero Depth travel: Ref −1.66 dB at Mass 50 (Body and Fold thin h1 faster than the
  Anchor grows) and Saw −2.0 dB at Mass 90 (compression 0.5 → 1: GR grows faster than the makeup calibrated on the reference voice; SC-002
  requires GR(100) ≥ 3 dB, so an output-tap w₁ must fall by the GR excess on richer voices). Ruled (recommended option): SC-001(c) measures
  w₁ at the fold + anchor sum tap; SC-003 gates the reference and saw fixtures and records the pure-sine fixture; the Anchor travel is
  bisected against the Ref / Saw span(L). No numeric bar moves. Rejected: a level-matched makeup (compression would no longer change
  steady level; FR-016 / SC-022 semantics); makeup scaled by the sidechain energy fraction (a new unmeasured law term); shrinking Body /
  Fold travel (SC-002's THD step has 8.2 dB against 6.5; the sine makeup conflict stays).
- **G8-3 (third run, sum tap, Ref / Saw gating; FR-031, FR-032 and SC-014(a) rows amended):** with the Depth travel bisected to zero and
  the Anchor at full travel the sum's w₁ still fell: Ref −1.28 dB at Mass 40 → 50 (Body 0 → 0.5 and Fold 0 → 50 thin h1) and Saw −2.22 dB
  at Mass 90 → 100 (Body 0.5 → 0.9 alone lowers the core's h1 by 4.7 dB); with no Depth travel Mass 0 was no longer pure (Ref THD(0)
  −5.9 dB; SC-002's THD(50) − THD(0) 6.38 against 6.5). Root cause in every round: the Anchor was keyed to the delivered, Mass-biased h1,
  so whatever thinned h1 thinned the Anchor. Ruled (recommended option): the Anchor is keyed to the recipe's h1 for the voice's own
  controls, before the Mass biases (FR-031 amended; a reference definition, not a compensation, FR-032); SC-014(a) checks the fed a₁ is
  Mass-independent and equals the delivered h1 at Mass 50; the harvest re-runs with Depth travel as a lever. Rejected: re-ruling Q2 to
  allow w₁ to fall; an absolute-level mode in the Phase 3 header; accepting zero Depth travel (Mass 0 would not be pure).
- **G8-4 (fourth run, Anchor keyed to the unbiased h1; SC-001(c) row re-ruled):** with the Anchor at full travel and Depth frozen the
  sum's w₁ still fell per step (Ref −0.78 dB at Mass 40 → 50 from Body / Fold thinning; Fold → 100 cost the sine fixture −4.1 dB at
  Mass 100, the Phase 2 −12 dB h1 clause), and the arithmetic shows SC-001(c) as written cannot coexist with SC-002: a sine-like Mass 0
  holds about 1.86 × the unbiased a₁ in h1, Mass 100 about 0.09 × plus an Anchor capped at 1 ×, so the sum ends near 1.09 × (−4.6 dB,
  dip ≈ −5.4 dB near Mass 70). Ruled (recommended option): SC-001(c) becomes a bounded fall, the sum's w₁ at every Mass point ≥ its
  Mass 0 value − 6 dB (harvest margin 0.5 dB), adjacent steps recorded, the Anchor tap's own w₁ non-decreasing under SC-001(b). SC-002's
  pure Mass 0 and the full Depth travel stay. Rejected: a 3 dB bound (predicted infeasible without shrinking Depth / Fold travel, eroding
  SC-002); dropping Mass 0 purity; raising the Anchor's level ceiling above 1 (Phase 3 header, raises the sub level SC-003 and the
  limiter see).
- **G8-5 (fifth run; FR-021, FR-024, D-2, FR-033, SC-001(a)(b), SC-011, SC-023 rows amended):** SC-001(c) passed everywhere (worst fall
  −4.74 dB), SC-002 passed with Mass 0 pure again, SC-003 unweighted passed; the saw fixture's K-weighted span at E2 / C3 read 4.03 / 3.84 dB
  against 3 dB (the reference 2.87 / 2.73) with every lever spent: compression at its maximum, Fold amountUp at its SC-002 minimum (THD step
  6.50), the Anchor travel carried by SC-001(c) (0.76 dB of margin), and bends cannot change an endpoint span. Ruled (recommended option): the
  output trim FR-021 anticipated becomes the law's seventh, calibrated target — a dB gain after the compressor inside `MassDynamics`, 0 dB
  at Mass 50, monotone non-increasing, one Linear row harvested from the reference voice's K-weighted level (only rises cancelled). Rejected:
  gating the K-weighted span on the reference fixture only; harvesting Anchor / Body travel down (SC-001(c) margin); raising the bar.
- **G8-6 (seventh-target build; SC-015 row amended):** on the pure-sine fixture at Mass 100 the limiter stayed engaged (steady GR −1.43 /
  −0.22 / −0.12 / −0.50 dB at E0 / E1 / E2 / C3 against −0.01), the reference and saw idle. Same cause as G8-2: the sub-only voice is
  invisible to the 2·f₀ sidechain and receives the full makeup with no gain reduction, on top of the Anchor doubling its fundamental.
  Ruled (recommended option): the sine fixture is recorded for SC-015 as for SC-003. For the record, the reference and saw at Mass 100 hit
  the limiter at the note attack by 7–9.5 dB for about 0.5 s (recorded per SC-015; a Phase 9 listening item). Rejected: makeup scaled by the
  sidechain energy fraction; capping the Anchor for sine-like voices; lowering the fixtures' calibration level.
- **G8-7 (main-loop measurement refinements told to the user; SC-001(b), SC-005(a) and SC-011 rows amended):** (1) the compression lever's
  "others at Mass 0" voice is a saw whose Depth bias (+0.5 at Mass 0) leaves nothing above the 2·f₀ sidechain corner, so with the harvested
  thresholds the lever read 0.03 dB of GR and 0.03 dB of ΔL_step change; the lever's other targets now sit at their Mass 50 base values (the
  SC-006 / SC-022 calibration voice, on which `MassCompressor_Audibility` reads 6.5 dB). (2) SC-005(a)'s unsmoothed-Mass positive control
  stopped counting a click once the harvested amount table replaced the seed (every target is smoothed downstream of the law); the hard
  +12 dB switch at the step sample is the gated positive control in the smoke and the [long] shards, the unsmoothed step recorded.

### Group 9 close rulings (build, 2026-10-18)

- **G9-1 (SC-010 / FR-042 rows amended; user, recommended option):** the Fold-100 fixture cells failed the −80 dBFS bar at every rate while
  every plain `kSawAnchor` cell passed with ≥ 14 dB of margin. Two causes: (1) the shaper's 5 ms gain motion (up to +17.8 dB) on an input
  whose content reaches Nyquist folds its sidebands back, −63 … −78 dBFS at 44.1 / 48 kHz and −75 dBFS at 96 kHz on the moving windows;
  (2) at E2 the compressor's 4× true-peak estimate of the Fold-100 wave alternates ≈ 0.27 dB with the sub-sample phase of the 582.47-sample
  period (48 kHz), so the gain wobbles ≈ 0.18 dB at f₀ / 2 and the steady window reads −46 / −55 dBFS at 44.1 / 48 kHz (−93 at 96 kHz, where
  the near-Nyquist content is 20 dB lower). The code fix, 2× oversampled gain application in both stages plus 8× detectors, costs ≈ +0.5 % of a
  core against an SC-017 already over its bar. Ruled: the `kSawAnchor` fixture is gated at −80 dBFS; the Fold-100 fixture is recorded (worst
  readings per rate in the row) with a Phase 9 handoff: listen to Fold → dynamics at Mass 100; Phase 9's chain budget decides oversampled gain
  application. No bar moves. Rejected: oversampling the dynamics now (needs an SC-017 re-ruling and a multi-day rebuild of plan S4 / S5 and the
  harvests); a two-period detector hold plus a band-limited shaper gain (fixes at most the steady wobble, slows FR-008, leaves the moving windows
  red).
- **G9-2 (FR-008 and SC-008(c) rows amended; user, recommended option):** the harvested 7.5 ms duration cell read +19.17 % on the E1 sine
  (bar ±15 %), its other three cells −7.50 / −1.39 / −7.78 %; every cell from 10 ms up is within the bar. Same physics as C-2's 5 ms cell (the
  41 Hz sine's first peak lands 6.07 ms after onset). Ruled: C-2 extends to the 7.5 ms cell, recorded on the E1 sine; no retune; the harvest
  prints `STOP AND SURFACE` for any other cell beyond 15 %. Rejected: a minimax retune of the 7.5 ms row (predicted +15.0 / −11.7 / −5.6 / −12.0 %,
  at the bar and worse elsewhere); carrying the cell as an open defect.
- **G9-3 (FR-044 row amended; user, recommended option):** the Group 7 gate fix G7-1 (`TruePeakLimiter::processChunk` clamps its output to
  ±ceiling) is ratified as the one modification of a reused header; every consumer suite re-ran green. Rejected: reverting the clamp (SC-014(c) /
  SC-021 would read one ulp over the ceiling).
- **M-1 (main-loop measurement refinement under plan S8.2 step 3, told to the user):** `MassLaw_LevelSlopeBounded_Ref_C3` read |ΔP| 0.2178 dB
  per unit at Mass 54 → 55 (bar 0.2; a 1.0 dB K-weighted rise over Mass 50 → 58 from the Fold row's linear onset, 0 → ≈ 10) while the harvest's
  step-3 verdict had passed: it measured |ΔP| on the 11-point lattice, a 10-unit average (0.10 dB per unit), so the plan's bend step never
  triggered. Refined: the harvest's |ΔP| check runs at the gate's resolution, every Mass unit on the gated Ref / Saw × E2 / C3 cells, for the
  Linear table and for each bend candidate; the bend order (Fold SCurve, Anchor SCurve, both) and the STOP AND SURFACE rule are unchanged.
  Outcome: the re-run harvest (133 s lane, 1422 renders) measured the Linear table at 0.2178 dB per unit on Ref C3 and adopted the first
  bend, Fold SCurve: Ref C3 0.1303, Ref E2 0.1204, Saw E2 0.0746, Saw C3 0.0711 dB per unit (bar 0.2); SC-001(c) worst fall −4.66 dB;
  SC-002 margins and the trim row unchanged; `mass_law.h` kRows pasted with `FoldAmount … .curve = ModCurve::SCurve`.
- **M-2 (main-loop harness repair under plan C-3, told to the user):** the SC-010 positive control (a hard 0 → +12 dB gain step on a C3 saw)
  read −75.8 / −76.5 / −82.5 dBFS at 44.1 / 48 / 96 kHz, under the −80 dBFS bar it must exceed at 96 kHz, because the saw's content near
  48 kHz is 20 dB below its content near 24 kHz. Positive controls are harness construction (Phase 5 rule: repaired, not re-ruled): the step is
  applied to the Fold-100 C3 saw, whose content reaches Nyquist at every rate; it reads −64.2 / −65.0 / −71.1 dBFS at 44.1 / 48 / 96 kHz
  (evidence/run-log/close/long/dsp_systems_tests__MassDynamics_AliasingE0E2_*.log).
- **M-4 (SC-005(c) smoke proxy; main-loop harness refinement, told to the user):** after the Fold SCurve bend the per-push
  `MassLaw_AutomationDescriptorTrajectorySmoke` read L at 1.05× its bound on the E2 0 → 100 host step while the gated `[long]` E2 shard,
  whose bound comes from the 101-point (b) lattice, read 0.43×. The smoke derived its per-unit slopes from a 9-point lattice (12.5-unit
  averages), which under-reads the bent row's local slope. Refined: the smoke uses a 21-point lattice (5-unit averages); the gated shards
  are unchanged.
- **G9-4 (SC-020 row amended; user, 2026-10-18):** the T022 lane listed every `[long]` case of the two executables (175 shards, 4 h 04 min,
  about 45 of them Phase 6) and its one red was a Vorago case's own 60-minute wall-clock budget (73.9 min under 24 concurrent shards and the
  two per-push lanes; its 15 behavioural assertions passed; it does not include the one shared header Phase 6 modified). The user asked why
  tests no Profundum change can reach ran at all. Ruled: the SC-020 gate is scoped to the change set — per-push lanes of both executables,
  the `[long]` cases of the phase's own TUs, and the full suites of every consumer of a modified shared header (here `true_peak_limiter.h`:
  dsp_effects, Seraphis, Vorago, Membrum, plugin / approval, Ruinae, Innexus, all re-run green 2026-10-17); untouched `[long]` cases are
  not re-run, and `EcosystemEngine_HostileFuzzStaysBounded` is recorded as a load-induced timing red. The isolated re-run I had started
  was stopped. Phase 7's close lane is written to that scope from the start.
- **M-3 (FR-045 / SC-017; main-loop implementation refinement, told to the user):** the shipped `MassDynamics` read 1.047 / 1.183 / 1.270 % in
  three isolated pinned runs (T001's 0.769 % was the clang prototype). A per-stage profile of the shipped headers under the test build's
  MSVC flags (`bench/stage-bench.cpp`, `evidence/run-log/close/stage-bench.txt`) read 1.145 % for the gated cell, of which one `gainToDb` +
  `dbToGain` pair cost 0.235 % per sample (≈ 24 ns each: the Layer-0 functions evaluate their constexpr series at run time) and the two stages
  make six such calls per sample pair. Refined: `detail::gainToDbFast` / `dbToGainFast` (transient_shaper.h; `std::log2` / `std::exp2` forms of
  the same functions, equal within float rounding, 0 dB → exactly 1) replace the three conversions the Phase 6 headers own; the three inside
  `DynamicsProcessor::processSample` stay (FR-044); `setTransientDurationMs` returns early on an unchanged value. Profile after: 0.802 %
  (shaper 0.526 → 0.288 %). R-1 fallbacks (2) TICK4 and (3) TP2X not needed. A run-time fast path inside db_utils.h itself would serve every
  consumer and is a follow-up outside this phase (shared header).
