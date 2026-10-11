# Feature Specification: Profundum Phase 2 — Edge Excitation & Harmonic Fold

**Spec slug:** `profundum-phase2-edge-fold`
**Status:** Clarified (2026-10-10). Specify, challenge round 1 and clarify are complete: every clarification question and open decision is decided and encoded in the FR/SC below (see ## Clarifications). Not yet planned.
**Roadmap source:** `specs/Profundum-roadmap.md` → Part A → **Phase 2** (lines 258–283). Supporting roadmap
statements this spec traces to:
- **Core Philosophy:** "The fundamental never disappears" (lines 43–44), "Alias-free low end is engineered"
  (lines 51–52) and "Nothing ships inert" (lines 53–54).
- **Key Design Decision 1:** "nonlinear excitation (oversampled soft clip, blend tied to Edge) is a separate
  phase" (lines 123–124).
- **The Phase 1 statements that hand work to this phase:** Edge, "Phase 2's excitation extends it to
  aggressive" (lines 222–223), and "The *asymmetric → clipped → increasingly complex* tail of that path is
  Phase 2's" (lines 232–234).
- **The Reuse Inventory "Nonlinear primitives" row** (line 162) and the **ODR note** (lines 174–176).
- **The downstream consumers:**
  - Phase 3: "Excluded from Fold … by default" (lines 303–305) and "anchor audible through maximum Fold"
    (line 311).
  - Phase 6: the Mass target "saturation (Fold amount)" (line 384).
  - Phase 8: the `MorphState` fields "edge excitation, Fold" (line 435) and the "clean → nonlinear" axis
    (line 447).
  - Phase 9: the chain "core (+ edge excitation) → Shape → Harmonic Fold → Character" (lines 472–476) and its
    per-voice budget arithmetic "≈ 4.8 %" (line 494).
- **The Shape → Fold order ruling** (lines 621–622) and the **Cross-Cutting Constraints** (lines 590–610).

The roadmap defers exactly one definition to this spec: the Fold's *harmonic compression* regime (line 273,
"defined in spec"). It lists no Open Question for Phase 2.

**Concept source:** `specs/Profundum-concept.md`:
- §1 Core (lines 44–76), especially the progression "Sine → rounded triangle-ish → saw-ish → asymmetric →
  clipped → increasingly complex" (line 74).
- §7 Harmonic Fold (lines 344–388).
- §13 Processing architecture (lines 601–645).

**Phase 1 contract consumed:** `specs/profundum-phase1-harmonic-core/spec.md` (status COMPLETE,
`compliance.md`, commit `47e43918`) and the shipped headers `systems/profundum_core.h` and
`processors/spectral_shape_recipe.h`.

Every roadmap, concept and header line cited below was read this session (2026-10-10).

**Layer:** one new component and one extension, both header-only in `Krate::DSP`:

| Component | Layer | Header | Kind |
|---|---|---|---|
| Edge excitation | 3 (inside `ProfundumCore`) | `dsp/include/krate/dsp/systems/profundum_core.h` | extension of a Profundum-owned header |
| `HarmonicFold` | 2 (processors) | `dsp/include/krate/dsp/processors/harmonic_fold.h` | new |

No shared (multi-plugin) DSP header changes in this phase (FR-048).

---

## Overview

Phase 1 shipped a linear additive core. It can raise existing harmonics but cannot create new ones. It cannot
produce even harmonics beyond a saw's natural parity (Phase 1 FR-023), and it stops the concept's waveform
path at *saw-ish*. Phase 2 adds the two nonlinear generators the roadmap assigns to it.

**Edge excitation** is a soft-clip stage *inside* `ProfundumCore`, applied to the core's own output. It is
optional, continuously blendable and anti-aliased. Its drive is coupled to the recipe's **Edge** control,
which gives Edge a second capability: generating harmonics, not only raising existing ones. An **asymmetry**
control adds even harmonics and **drive** adds clipping. Together they complete the concept's *asymmetric →
clipped → increasingly complex* tail. At zero mix the core is bit-identical to Phase 1.

**`HarmonicFold`** is a Layer 2 stereo processor with one 0–100 control. That control crosses three regimes
with continuous output: soft saturation (low) → **harmonic compression** (middle) → alias-controlled folding
(high). The middle regime is what separates it from ordinary waveshaping: harmonic density rises before level
or crest factor change, so its descriptor trajectory differs measurably from a plain tanh drive sweep. The
Fold is DC-safe and anti-aliased. It never sees the Sub Anchor: Phase 3 composes that exclusion and Phase 9
wires it.

Both stages are gated by measurement:
- Harmonic count and THD are monotonic in their levers, and even-harmonic energy is monotonic in asymmetry.
- Aliasing is ≤ −80 dBFS at E0–E2 at maximum settings, and DC is ≤ −80 dBFS.
- There is no level or spectral jump across the Fold's regimes, and both stages are transparent at zero.
- The two stages together cost ≤ 1.5 % of one core per voice, on top of the core's own ≤ 1.0 % (whole chain ≤ 2.5 %).

## Scope

In scope (roadmap lines 258–283):

1. **Edge excitation inside `ProfundumCore`:**
   - plain-data excitation controls (mix, drive, asymmetry);
   - the Edge → drive coupling;
   - the anti-aliased soft-clip transfer with asymmetry;
   - DC removal and phase-coherent blending;
   - Reset-policy state handling;
   - zero-mix bit-identity to Phase 1.
2. **`HarmonicFold`** (L2):
   - the three-regime 0–100 transfer with C⁰-continuous output;
   - the harmonic compression regime as this spec defines it (OQ-A, resolved in clarify: FR-024, FR-035 to FR-037);
   - anti-aliasing and DC safety;
   - stereo processing with L/R bit-identity for identical inputs;
   - transparency at zero.
3. **Tests for both stages, alone and chained (core → excitation → Fold):** unit tests, render-descriptor
   audibility gates, aliasing / DC / continuity / transparency gates, and the CPU budget test.

## Non-Goals (owned by later phases, or deliberately excluded)

| Excluded | Owner | Roadmap line |
|---|---|---|
| Sub Anchor. Its exclusion from Fold is a property of the Phase 3/9 wiring, not of `HarmonicFold`'s API (which simply has no anchor input) | Phase 3, Phase 9 | 287–311, 472–476 |
| Drop, glide, Note Memory, and choosing the retrigger phase policy | Phase 4 | 316–340 |
| Shape (ROUND / HOLLOW / BITE) and the six Character algorithms, including the Character **Fold** algorithm (`WavefolderProcessor`) and the Shape **BITE** `DiodeClipper` asymmetry | Phase 5 | 344–367 |
| The Mass law (its "saturation (Fold amount)" target only *consumes* `HarmonicFold::setAmount`) | Phase 6 | 382–386 |
| Per-channel drive/tone offsets for WIDE (those live in the Character stage) | Phase 7 | 415–416 |
| `MorphState` and the interpolation laws for the excitation controls and Fold amount (this phase only keeps them plain, interpolable data) | Phase 8 | 434–447 |
| Placing excitation/Fold per voice vs post-sum in POLY (OQ-7) | Phase 9 | 475–476, 626–627 |
| Parameter IDs, names (OQ-9: Character "Fold" vs "Harmonic Fold"), UI and presets | Phases 10–13 | 501–570, 630–633 |
| Any change to `SpectralShapeRecipe::Controls` (Phase 1 FR-002: six fields, asserted by Phase 1 SC-011(e)) | — | — |

Phase 2 adds **no modulation source**, so the `ModulationSource` concept constraint does not apply here.

---

## Existing components (verified this session)

Paths are relative to `dsp/include/krate/dsp/`. Every row was opened and read on 2026-10-10, and the
signature column quotes the header.

| Component | Header | Reused for | Verified signature / fact |
|---|---|---|---|
| `ProfundumCore` | `systems/profundum_core.h:40` | Host of the edge excitation | Layer 3 (line 2). `void prepare(double sampleRate, int numPartials = kDefaultPartials) noexcept` (line 77, documented "NOT real-time safe", lines 75–76). `void setControls(const SpectralShapeRecipe::Controls&) noexcept` (108). `void noteOn(float f0Hz) noexcept` (143, "Effective at offset 0 of the next block"). `void processBlock(float* left, float* right, std::size_t numSamples, const float* f0PerSample = nullptr) noexcept` (172). It renders in runs that never cross a `kPitchUpdateInterval` = 16 boundary (lines 46, 183–211), inside `kControlInterval` = 32 control intervals (line 45), and multiplies the bank output by `kBankHeadroom` = 4 after the loop (lines 62, 212–219). `RetriggerPhase { Reset, FreeRunning }` (line 42). `[[nodiscard]] bool stateFinite() const noexcept` (238). Phase 1 FR-040 says the core owns "no heap allocation anywhere; all storage fixed-size members" (header comment lines 11–12); see D-1. |
| — output level and peaks | `systems/profundum_core.h:54–61`, `processors/spectral_shape_recipe.h:51–53` | Drive calibration (FR-011, FR-031) | `kCoreOutputRmsDb = -12.0f`, `kPowerTarget = 0.25238293779207720f` (P₀). The core's per-channel RMS is −12 dBFS in every state (Phase 1 FR-049, SC-018). Its **peak**, however, is bounded only by √(N·P₀) ≤ 4.92 per channel, and the SC-009 high-edge state peaks above 2.0 (header comment lines 54–61). |
| — FTZ precondition | `systems/profundum_core.h:37–38` | E-11 | "the owner runs processBlock with FTZ/DAZ on". |
| `SpectralShapeRecipe::Controls` and named coordinates | `processors/spectral_shape_recipe.h:41–48, 227–264` | Test inputs; `edge` feeds the Edge → drive law (FR-004) | `struct Controls { float depth = 0.5f; float body = 0.5f; float bodyCurvature = 0.5f; float bodyEmphasis = 0.0f; float edge = 0.5f; float shift = 0.0f; }`. Named coordinates: `kSineAnchor` (depth 1, body 0, edge 0), `kTriangleAnchor` (depth 0.5, emphasis −1), `kSawAnchor` (depth 0), `kHeavy`, `kHollow`, `kGrowl` (edge 1, shift 1), `kDefaultControls` (= mid grid). |
| `TanhADAA` | `primitives/tanh_adaa.h:66` | Candidate soft-clip transfer (roadmap line 267) | Layer 1. `void setDrive(float drive) noexcept` (101; negative becomes absolute, line 188). `void reset() noexcept` (109). `[[nodiscard]] float process(float x) noexcept` (131). `void processBlock(float* buffer, size_t n) noexcept` (142). First-order ADAA in `float`: `y = (F1(x·d) − F1(x₁·d)) / (d·(x − x₁))` with `F1(x) = \|x\| − ln2 + log1p(exp(−2\|x\|))` (lines 200–221, 260–266). When `\|x − x₁\| < kEpsilon = 1e-5f` it falls back to `FastMath::fastTanh` of the midpoint (lines 165, 256–259, the `if (absDx < kEpsilon)` block). The first sample after `reset()` is a naive `fastTanh` (lines 244–249, FR-018 in that header). **No asymmetry and no DC handling** (header line 12: "no internal oversampling/DC blocking"). See E-4. |
| `FastMath::fastTanh` / `Sigmoid::tanh` | `core/fast_math.h:65`, `core/sigmoid.h:68` | Candidate soft-clip transfer. SC-007's plain-tanh reference sweep uses `std::tanh` in `double`, not this | Padé (5,4), "Maximum error: 0.05% for \|x\| < 3.5" (fast_math.h:53). It **returns exactly ±1.0 for \|x\| ≥ 3.5** (lines 76–81), a step of 1 − tanh(3.5) ≈ 1.8e-3 in the transfer. `Sigmoid::tanh(x)` is `FastMath::fastTanh(x)` (sigmoid.h:68–70). See E-5. |
| `Sigmoid::Asymmetric::withBias` | `core/sigmoid.h:392` | Asymmetry idiom (FR-005) | `template <typename Func> [[nodiscard]] inline float withBias(float x, float bias, Func func) noexcept { return func(x + bias); }`, documented "Caller MUST DC-block the output" (lines 375–388). `tube`, `diode` and `dualCurve` (lines 333, 358, 414) are alternative asymmetric transfers. |
| `Waveshaper` | `primitives/waveshaper.h:108` | Not composed; reference for the drive + bias convention | `process(x)` = `applyShape(drive_ * x + asymmetry_)` (lines 205–213). `setAsymmetry(float bias)` clamps to [−1, 1] (167–170). Drive 0 returns 0 (208–209). No anti-aliasing. |
| `HardClipADAA` | `primitives/hard_clip_adaa.h:69` | Candidate for the folding / clipping arms if the plan needs a hard-knee ADAA | `setOrder(Order)` (122), `setThreshold(float)` (130), `process(float)` (163). First- and second-order ADAA (lines 217–220), `kEpsilon = 1e-5f` (210). |
| `Oversampler<Factor, NumChannels>` | `primitives/oversampler.h:226` | Oversampling for both stages (roadmap lines 162, 266–267, 275) | `template<size_t Factor = 2, size_t NumChannels = 2>`, with `Factor` ∈ {2, 4} (line 228). `void prepare(double sampleRate, size_t maxBlockSize, OversamplingQuality quality = Economy, OversamplingMode mode = ZeroLatency) noexcept` (288–293) is documented "NOT real-time safe (allocates memory)" (287): its `std::vector` buffers are sized `maxBlockSize·Factor·NumChannels` (383–384, 473–479). `void process(float* left, float* right, size_t numSamples, const StereoCallback& callback) noexcept` (322–327), with `using StereoCallback = std::function<void(float*, float*, size_t)>` (236). **`process` returns without touching the buffers when `numSamples > maxBlockSize_`** (lines 843–845). Low-level `upsample` / `downsample` (344, 347). Economy / ZeroLatency uses an IIR `BiquadCascade<4>` Butterworth low-pass at **0.45 × base fs** in every 2× stage, with zero latency (lines 53, 445–449, 491–524). Standard / High + LinearPhase uses a 31- / 63-tap halfband FIR with **15 / 31 base-rate samples of latency at 2×, and 30 / 62 at 4×** (lines 85–86, 104–105, 451–470). |
| `Wavefolder`, `WavefoldType` | `primitives/wavefolder.h:101, 52` | Folding-regime math (roadmap line 274) | Stateless; `process(float)` is `const` (182). `Triangle` → `WavefoldMath::triangleFold(x, 1/foldAmount)`, and it **returns 0 for `foldAmount < 0.001`** (251–257). `Sine` → `sineFold` (263–266). `Lockhart` → `tanh(lambertW(exp(x·a))) − 0.5138683936`, with the comment "so that Lockhart(0) = 0 … causing clicks when crossfading" (270–284). `setFoldAmount` clamps to [0, 10] (146–155). NaN propagates (186–188). |
| `WavefoldMath::triangleFold`, `sineFold` | `core/wavefold_math.h:286, 366` | Folding-regime math | `triangleFold(float x, float threshold = 1.0f)` uses modular reflection, with the threshold floored at `kMinThreshold = 0.01f` (lines 37, 286–318). `sineFold(float x, float gain)` returns **x** for `gain < kSineFoldGainEpsilon = 0.001f` and `sin(gain·x)` above it (lines 45, 366–381), so the output jumps from x to ≈ 0.001·x at the threshold. |
| `WavefolderProcessor` | `processors/wavefolder_processor.h:115` | **Not composed as-is.** Its symmetry-as-offset idiom is a reference | Layer 2, **mono** `void process(float* buffer, size_t numSamples) noexcept` (325). Chain: `wet = dry + sym·(1/foldAmt)` → fold → `DCBlocker` (10 Hz, line 131) → `dry·(1−mix) + wet·mix` (351–378). `setFoldAmount` clamps to **[0.1, 10]** (122–125, 260–263). Below `mixAmt < 0.0001f` it skips the fold **and the DC blocker state** (345–347). No oversampling or ADAA. These make it neither C⁰ at zero nor alias-controlled (E-6). Phase 5 uses it for the Character "Fold" algorithm (roadmap line 357). |
| `DCBlocker`, `DCBlocker2` | `primitives/dc_blocker.h:94, 272` | DC removal candidates (FR-009, FR-026) | `void prepare(double sampleRate, float cutoffHz = 10.0f) noexcept` (135 / 298). `[[nodiscard]] float process(float x) noexcept` (190 / 328). 1st order: `y = x − x₁ + R·y₁` (198), "Settling time: ~40ms at 10Hz". 2nd-order Bessel: −12 dB/oct, "~13ms at 10Hz" (header lines 6–14). At the 10 Hz default either one attenuates and phase-shifts a 20.6 Hz E0 fundamental; see FR-010 / SC-012. |
| `EnvelopeFollower` | `processors/envelope_follower.h:82` | Candidate level detector for the harmonic-compression regime (OQ-A) | `void prepare(double sampleRate, size_t maxBlockSize) noexcept` (106). `[[nodiscard]] float processSample(float input) noexcept` (164). `setAttackTime(float ms)` / `setReleaseTime(float ms)` (220, 227). `DetectionMode` Amplitude / RMS / Peak (41). Layer 2, so a Layer 2 `HarmonicFold` cannot include it (see Include discipline). |
| `ChebyshevShaper` | `primitives/chebyshev_shaper.h:86` | Not planned; listed by the roadmap row | `kMaxHarmonics = 8` (95). Explicit per-harmonic polynomial levels (127–143). It is exact only for a single full-scale sine, so it cannot provide level-independent density on a multi-harmonic bass. |
| `SaturationProcessor` | `processors/saturation_processor.h:98` | Not composed | Mono `process(float*, size_t)` (175), five static curves (343–387), 10 Hz DC blocker (107). |
| `OnePoleSmoother`, `LinearRamp` | `primitives/smoother.h:134, 305` | Per-sample control smoothing (FR-007, FR-034) | `configure(float smoothTimeMs, float sampleRate)` (160 / 329), `setTarget(float)` (170 / 342), `[[nodiscard]] float process()` (197 / 370), `snapToTarget()` (257 / 414). |
| `detail::isFinite` | `core/db_utils.h:118` | Input sanitising (FR-043) | `[[nodiscard]] KRATE_DETAIL_FORCEINLINE constexpr bool isFinite(float x) noexcept`, fast-math-immune. |

**Test helpers (verified).** Phase 1's `dsp/tests/unit/systems/profundum_core_test_helpers.h` (namespace
`Krate::DSP::ProfundumTest`, line 47) provides:
- `requireLREqual(const float* L, const float* R, std::size_t n)` (61) and `renderCore(...)` (90);
- `goertzelPhase(const float* x, std::size_t L, double fs, double hz)` (157, BH7);
- `bh7PowerSpectrum(const float* x, std::size_t L)` (206);
- `renderedHarmonicPowers(const float* x, double fs, double f0, int N)` (235);
- `aliasFftLength(double fs, double f0)` (257): the smallest power of two ≥ max(2¹³, 64·fs/f₀);
- `aliasedPower(const float* x, std::size_t fftLen, double fs, double f0)` (273): BH7, Parseval-normalised,
  dB re 0.5, excluding ±8 bins around every n·f₀ < fs/2 and bins 0–8, and returning `excludedFraction`;
- `residual(...)` (464) and `renderReference(...)` (498).

Shared helpers:
- `artifact_detection.h`: `ClickDetectorConfig` (38) and `ClickDetector` (99).
- `allocation_detector.h`: `AllocationScope` (111).
- `render_fingerprint.h`: `fingerprintRender(std::span<const float>)` (73) and `compareFingerprints(...)`
  (122).
- `low_frequency_metrics.h`: `magnitudeSpectrum(...)` (118) and
  `calculateCorrelation(const float*, const float*, std::size_t)` (478).

The `testing-dsp-analysis` skill's `measureAliasing` (`tests/test_helpers/spectral_analysis.h:287`) cannot
serve as the gate:
- it drives a **single sine** through a `float(float)` callable;
- it windows with **Hann** (lines 307–309);
- it counts only predicted aliases up to `maxHarmonic` (`AliasingTestConfig`, line 112).

It therefore cannot reach −80 dBFS on a multi-harmonic bass input, so SC-005 uses the Phase 1 BH7 method with
the D-6 extension. The roadmap names the `testing-dsp-analysis` method for this gate (line 281), so this
replacement is a recorded method deviation (D-6), not a silent substitution. The skill's `compareAliasing`
pattern (line 415) survives as the positive-control idea.

**Test registration (verified).**
- `dsp/tests/CMakeLists.txt` enumerates every TU ("ENUMERATED, not globbed", line 565).
- The Phase 1 non-finite TU is the only Profundum entry in the `-fno-fast-math` block (lines 1075–1077).
- Hidden `[.perf]` cases run through `node tools/run-cpu-tests.js`, whose filter includes `[.perf]`
  (`tools/run-cpu-tests.js:56`).
- Phase 1's pinned CPU figure for the core alone is **0.602 %** at 64 partials. An unpinned run after a long
  suite read 1.045 % (both at `specs/profundum-phase1-harmonic-core/compliance.md:81`). CPU gates here
  therefore run pinned and isolated (SC-018).

## New components

| Class / type | Layer | Header | ODR sweep (`grep -rnE "(class\|struct\|enum class\|enum\|using) <Name>\b" dsp/ plugins/ tests/ tools/`, 2026-10-10) |
|---|---|---|---|
| `HarmonicFold` | 2 | `processors/harmonic_fold.h` | **0 hits.** The file does not exist, and `grep -rn "harmonic_fold\|HarmonicFold" dsp plugins` returns nothing. |
| `ProfundumCore::ExcitationControls` (nested struct) | — | `systems/profundum_core.h` | Nested, so no namespace-scope exposure. The namespace-scope spellings were swept anyway: `ExcitationControls`, `EdgeExcitationControls`, `EdgeExcitation`, `EdgeExciter`: **0 hits each.** |
| Optional plan-internal helpers (names are the plan's; candidates listed) | ≤ 3 | — | `FoldRegime`, `FoldControls`, `HarmonicCompressor`, `HarmonicCompression`, `HarmonicExciter`, `Exciter`: **0 hits each.** The plan re-sweeps any helper it puts at namespace scope. |
| Test-only: namespace `Krate::DSP::ProfundumTest::Phase1Ref` in `dsp/tests/unit/systems/profundum_phase1_core_ref.h` (SC-004(a)); static members `excitationDriveLaw`, `edgeDriveGain` (FR-004) | test / — | — | `grep -rn "Phase1Ref\|profundum_phase1_core_ref\|excitationDriveLaw\|edgeDriveGain" dsp plugins tests tools` (2026-10-10): **0 hits.** |

**Near-name check.**
- `Wavefolder` / `WavefoldType` (`primitives/wavefolder.h:101, 52`) and `WavefolderProcessor` /
  `WavefolderModel` (`processors/wavefolder_processor.h:115, 56`) exist and do not collide.
- The `*Exciter*` family is crowded:
  - `BowExciter` (`processors/bow_exciter.h:60`) and `ImpactExciter` (`processors/impact_exciter.h:33`);
  - `ExciterType`, an enum class in both `plugins/innexus/src/plugin_ids.h:214` and
    `plugins/membrum/src/dsp/exciter_type.h:9`;
  - `ExciterBank` and seven Membrum `*Exciter` structs.

  This spec therefore uses **no** namespace-scope `*Exciter*` name.
- None of the roadmap's hazard names is used: `CharacterProcessor`, `SubOscillator`, `StereoField`,
  `MorphEngine`, `MorphNode`, `TransientDetector`, `HarmonicCloud`, `SpectralState`, `MacroMapper` (lines
  174–176).

**Include discipline.**
- `harmonic_fold.h` (L2) includes Layer 0 (`core/`) and Layer 1 (`primitives/`) only, with no same-layer
  `processors/` include (the Phase 1 CC-layers lesson). `EnvelopeFollower` is Layer 2, so OQ-A's detector, if
  adopted, is built from Layer 0/1 parts, or the plan records a decision to move the Fold to Layer 3.
- `profundum_core.h` (L3) gains Layer 1 includes (`primitives/oversampler.h`, `primitives/tanh_adaa.h` or
  equivalents). It may include `processors/harmonic_fold.h` only if the plan composes it there. It does not
  need to, because Phase 9 composes the chain.
- Nothing points upward.

---

## Functional Requirements

### Descriptor definitions (used by FRs and SCs)

All descriptors are measured per channel on steady state unless an SC says otherwise. **Steady state** means
at least t_settle = max(500 ms, 8·τ_slow) after the last control change (and after note start), where
τ_slow = 1/(2π·f_c) of the slowest linear filter in the stage under test (its DC remover, if any) or the
longest smoother time constant, whichever is larger. Every such corner and time constant is a documented
`static constexpr` of the stage (FR-009, FR-026, FR-034), so the fixture computes t_settle from the header
rather than assuming it. Example: a first-order DC remover at 0.72 Hz (the corner that keeps ∠T₁ within 2° at
E0) has τ ≈ 0.22 s, so t_settle ≈ 1.77 s and an initial −36 dBFS DC transient has decayed by e⁻⁸ ≈ −69.5 dB.
Harmonic powers wₙ are measured with `renderedHarmonicPowers` (BH7 frame) for n·f₀ < fs/2.

**Sine input** is one of:
- the core at `kSineAnchor` (rest ≤ −60 dB re h1, Phase 1 FR-018); or
- for `HarmonicFold`'s Layer 2 tests, a test-generated `double` sine at the **calibration level**, −12 dBFS RMS
  (`SpectralShapeRecipe::kCoreOutputRmsDb`), i.e. peak 0.35521.

**Descriptors:**
- **THD** = Σₙ≥₂ wₙ / w₁ (dB). **Generated distortion** G = THD_out − THD_in, taken on linear power ratios and
  expressed in dB, for stages whose input already carries harmonics.
- **Harmonic count** H = the number of n ≥ 2 with n·f₀ < fs/2 and wₙ ≥ w₁·10⁻⁶. The −60 dB re h1 threshold is
  Phase 1's "measured" threshold (Phase 1 SC-001).
- **Even energy** E_even = Σ wₙ (n even) / w₁ (dB). The **odd/even ratio** is as in Phase 1.
- **Level** L = output RMS re input RMS (dB), over an integer number of f₀ periods ≥ 1 s.
- **Crest factor** CF = 20·log₁₀(peak / RMS) (dB), over the same window.
- **Harmonic centroid** C = Σ log₂(n)·wₙ / Σ wₙ (octaves above h1), as in Phase 1, over every n·f₀ < fs/2.
- **h1 transfer** T₁ = (output h1 phasor) / (input h1 phasor), from a `goertzelPhase`-style BH7 Goertzel at
  f₀. It has a magnitude |T₁| (dB) and an angle ∠T₁ (degrees). For every criterion on the excitation inside
  `ProfundumCore` (whose input is not observable), the "input" is a **second core instance** run with the same
  call sequence, f₀, recipe state and `ExcitationControls` except `mix = 0`; its output is the mix-0
  reference. For `HarmonicFold` the input is the buffer fed to it.
- **Even-harmonic phasors** Yₙ are the complex BH7 Goertzel values at n·f₀ (n even, n·f₀ < fs/2). **Generated
  even energy** relative to a reference render R is E_gen = Σₙ even |Yₙ − Yₙ^R|² / w₁ (dB): the even-harmonic
  content the lever adds, free of phase cancellation against the input's own even content.
- **DC** = 20·log₁₀(|mean|) re a full-scale DC of 1.0 (dBFS), over an integer number of f₀ periods ≥ 1 s.
- **Aliased power** uses Phase 1 SC-009's definition: BH7, Parseval-normalised, dB re 0.5 (a full-scale sine's
  mean square), summed over every bin 1…fs/2 outside ±8 bins of every n·f₀ < fs/2 and outside bins 0–8. It adds
  the D-6 FFT-length extension so that no alias falls inside an exclusion zone.

**Test notes and rates:**
- E0 = MIDI 16 (20.60 Hz), E1 = MIDI 28 (41.20 Hz), E2 = MIDI 40 (82.41 Hz) (roadmap line 281).
- C2 = MIDI 36 for lever gates (Phase 1's audibility pitch), and C0–C5 where an SC says so.
- Rates are 44.1 / 48 / 96 kHz unless stated (Phase 1's set).

### FR-001 series — Edge excitation inside `ProfundumCore`

- **FR-001 Placement.** The excitation is a stage of `ProfundumCore::processBlock`. It is applied per channel
  to the bank output after the `kBankHeadroom` rescale (`profundum_core.h:212–219`) and before the block
  returns. It is part of the core (roadmap line 266, "inside `ProfundumCore`"), not a separate public class. A
  plan-internal helper is allowed if it is Layer ≤ 3, ODR-swept and private to the core's implementation.
- **FR-002 Controls.** A nested plain aggregate `ProfundumCore::ExcitationControls { float mix; float drive;
  float asymmetry; }`, each field in [0, 1]:
  - set by `void setExcitation(const ExcitationControls&) noexcept`;
  - read back by `[[nodiscard]] ExcitationControls excitation() const noexcept`;
  - defaults (D-5): `mix = 0` (stage off), `drive = 0.5`, `asymmetry = 0`.

  Out-of-range values are clamped, and a non-finite field keeps its previous value (FR-043). The struct is
  plain data so Phase 8 can interpolate it (roadmap line 435). `SpectralShapeRecipe::Controls` is **not**
  changed (Non-Goals).
- **FR-003 Transfer.** An anti-aliased soft-clip transfer (roadmap lines 266–267: "oversampled soft-clip stage
  (`TanhADAA` / `Oversampler`)"). As effective drive rises, the output moves from near-linear toward clipped
  ("drive adds clipping", line 269). The transfer is odd-symmetric at `asymmetry = 0`.
- **FR-004 Edge → drive coupling.** The effective drive is D_eff = d(drive) · g(edge), where:
  - `edge` is the recipe `Controls.edge` the core latched for the same control interval;
  - d is a documented, continuous, strictly increasing drive law, published as
    `[[nodiscard]] static float excitationDriveLaw(float drive) noexcept` (`constexpr` where the math allows);
  - g is **dB-linear** in edge (D-4): g(edge) = 10^(`kEdgeDriveRangeDb`·(edge − 1)/20), with
    `kEdgeDriveRangeDb` = 12 dB, published as `[[nodiscard]] static float edgeDriveGain(float edge) noexcept`.
    Hence g(1) = 1 and g(0) = `kEdgeDriveFloor` = 10^(−12/20) ≈ 0.25119 (both documented
    `static constexpr` members). Equivalently D_eff[dB] = d(drive)[dB] + 12·(edge − 1).

  The coupling is always on: at `edge = 0` and `mix > 0` the stage still excites, 12 dB gentler (SC-001, SC-002(c)). `mix` remains an independent blend: Edge never scales, gates or offsets `mix` (SC-002(c)). It gives Edge its second capability, generating harmonics rather than only raising existing ones (roadmap lines 222–223, 267–268). The 12 dB range is fixed; it may be revisited only with a recorded measurement (FR-055). The core exposes D_eff read-only (`[[nodiscard]] float effectiveExcitationDrive() const noexcept`) so SC-002 can assert the law. The accessor returns the **latched target** D_eff for the current control interval (the pre-smoothing value computed from the latched `drive` and `edge`), not the per-sample smoothed drive (FR-007).
- **FR-005 Asymmetry.** `asymmetry` > 0 makes the transfer asymmetric, so it adds even harmonics ("an asymmetry
  control adds even harmonics", line 269). E_even is strictly increasing in `asymmetry` (SC-003). The bias
  idiom is `Sigmoid::Asymmetric::withBias` (`sigmoid.h:392`) or an equivalent documented asymmetric transfer.
- **FR-006 Blend and transparency at zero.** The stage is "continuously blendable" (line 266): its output
  blends the dry core signal and the excited signal by `mix`.
  - **At `mix = 0` the core output is bit-identical to the Phase 1 core** for the same call sequence, whatever
    `drive` and `asymmetry` hold. "The Phase 1 core" is the commit `47e43918` implementation, pinned
    in-process as a test reference (SC-004(a)), not the modified header's own default. Every Phase 1 test therefore passes with its source unmodified, and Phase 1
    SC-013's 1.0 % core budget still holds with the stage off.
  - The dry and excited paths are phase-coherent: along any `mix` sweep, h1 never dips below the lower endpoint
    (SC-012(c)).
- **FR-007 Control timing.** Excitation controls are latched at the core's control interval
  (`kControlInterval` = 32, counted from the global sample counter as in Phase 1 FR-042) and smoothed per
  sample. The render is therefore host-block-size independent (SC-017) and zipper-free (SC-009). Engaging from
  `mix = 0` (where FR-006 requires the Phase 1 output) and returning to it are click-free: whatever filter or
  ADAA state the anti-aliasing needs is valid when the first non-zero `mix` sample is produced.
- **FR-008 Anti-aliasing.** Oversampling (`Oversampler`, with the factor the plan chooses; 4× is affordable within SC-018) and/or ADAA, such that SC-005 holds. **The excitation adds zero base-rate latency (D-2, decided)**: only the IIR Economy / ZeroLatency oversampling configuration and/or ADAA are used, and no FIR mode (Standard / High quality, LinearPhase) is selected (SC-026). The core's latency stays 0 with the stage at any setting, and SC-012(a)'s ±2° bound at E0–E2 holds. The roadmap names the worst case: "the lowest notes at maximum drive" (line 270). Both channels of the core's stereo pair are processed with identical arithmetic.
- **FR-009 DC.** Steady-state DC of the core output is ≤ −80 dBFS at every excitation setting (SC-011). The
  static transfer maps 0 → 0 exactly at every `drive` and `asymmetry`, because the bias offset is compensated
  statically (for example f(x + b) − f(b)). A silent input then produces exactly 0.0, and no DC-blocker step
  occurs at onset (D-13). If the stage uses a DC-removal filter, its corner frequency is a documented
  `static constexpr` of the core, so tests derive the steady-state settle time from it (Descriptor
  definitions).
- **FR-010 Fundamental preservation.** The excitation's linear path (oversampling filters, DC removal) is flat
  at the fundamental (D-8):
  - at `drive = 0`, `mix = 1` (near-linear), |T₁| stays within ±0.1 dB and ∠T₁ within ±2° of the `mix = 0`
    output at E0–E2;
  - at every `drive` / `asymmetry` / `mix`, ∠T₁ stays within ±2°.

  This lets Phase 3's anchor, which is phase-locked to the core's h1 (roadmap lines 295–297), sum with the
  excited voice without cancellation. It also rules out DC removal that bites at E0: a 10 Hz `DCBlocker` turns
  a 20.6 Hz fundamental by tens of degrees.
- **FR-011 Level law.** At `mix = 1` the excitation output RMS stays within ±1.5 dB of its input RMS across the
  full `drive` and `asymmetry` ranges, at the calibration level. A **static** documented makeup law does this
  (no dynamic AGC), so drive is a character control rather than a volume knob. This extends the Phase 1 FR-005
  principle (D-3).
- **FR-012 Stereo.** With an all-zero pan-offset vector the core output stays L == R bit-identically with the
  excitation at any setting (extends Phase 1 FR-072 / SC-014). With a non-zero vector each channel is excited
  independently with identical arithmetic, and the mono sum shows no h1 cancellation (SC-013).
- **FR-013 Reset policy.** A `Reset`-policy `noteOn` clears every excitation state at the same point where the
  bank is reset: oversampler filter states, ADAA histories, DC-removal state, and the control smoothers
  (snapped to target). Phase 1 SC-012's `Reset` clauses then hold with the excitation on: the first sample is
  exactly 0.0, and renders after `noteOn` from different prior states are bit-identical. A `FreeRunning`
  `noteOn` keeps the excitation state.
- **FR-014 Waveform progression completed.** With the recipe on its sine → triangle → saw path (Phase 1
  FR-018), the excitation reaches the concept's tail (concept line 74; roadmap lines 232–234):
  - `asymmetry` makes the odd-only triangle anchor carry even harmonics (*asymmetric*);
  - `drive` clips (*clipped*);
  - the Fold (FR-020 series) continues to *increasingly complex*.

  SC-003(b) gates the even-harmonic half, which the recipe provably cannot do (Phase 1 FR-023).

### FR-020 series — `HarmonicFold` (Layer 2)

- **FR-020 Component.** `HarmonicFold` is Layer 2, header-only, at `processors/harmonic_fold.h` (roadmap
  line 271), in namespace `Krate::DSP`. It is non-copyable if it owns an `Oversampler` (which is
  non-copyable, `oversampler.h:270–271`), and movable.
- **FR-021 API.**
  - `void prepare(double sampleRate, std::size_t maxBlockSize) noexcept` and `void reset() noexcept`:
    control thread; `prepare` may allocate (D-1).
  - `void setAmount(float amount) noexcept`: amount in **[0, 100]** (line 271), clamped; a non-finite value
    keeps the previous one. Default amount 0.
  - `[[nodiscard]] float amount() const noexcept`.
  - `void processBlock(float* left, float* right, std::size_t numSamples) noexcept`: in place, stereo,
    **any `numSamples`**. It chunks internally, so the `Oversampler` silent no-op for
    `numSamples > maxBlockSize_` (`oversampler.h:843–845`) can never be reached.
  - `[[nodiscard]] bool stateFinite() const noexcept`.
  - `[[nodiscard]] static constexpr std::size_t latencySamples() noexcept`, returning 0 (D-2).
- **FR-022 Three regimes, one control, C⁰-continuous output.** The amount ranges map to three regimes
  (roadmap lines 271–274; concept lines 358–368):
  - [0, `kSoftEnd`) is **soft saturation**;
  - [`kSoftEnd`, `kCompressionEnd`) is **harmonic compression**;
  - [`kCompressionEnd`, 100] is **alias-controlled folding**.

  The boundaries are documented constants (D-12 proposes 30 and 65). The output is a continuous function of
  amount everywhere — at both boundaries and at 0 (FR-028) — so a slow automation produces no level or spectral
  jump (SC-008).
- **FR-023 Soft-saturation regime.** A smooth, odd-symmetric saturation whose drive rises with amount (concept line 360, "soft saturation"). It is **static and level-dependent**, like a saturator: no level detector acts in this regime, and its THD grows with input level (FR-036, SC-025(a)).
- **FR-024 Harmonic-compression regime (decided, clarify Q2).** Roadmap line 273 defers the definition to this spec; it is a regime in which:
  - harmonic density (H, THD) rises across the regime while L and CF hold within ±1 dB (lines 275–277, SC-007(a));
  - the upper-harmonic power relative to h1 is limited dynamically by a level detector shared by both channels (FR-033), so THD is approximately independent of input level: ≤ 3 dB over −24…−6 dBFS RMS, against ≥ 12 dB for a static tanh measured in the same test (SC-007(b));
  - its descriptor trajectory is ≥ 3 dB (median) from a plain tanh sweep (SC-007(c)).

  A static waveshaper behaves differently: its THD climbs with level. The mechanism that realises this is chosen by FR-035; the detector timing is FR-037.
- **FR-025 Folding regime.** Folding built on the `Wavefolder` / `WavefoldMath` / `WavefolderProcessor` math (line 274) and alias-controlled (FR-027). It is **level-normalised** (FR-036, SC-025(b)). Its endpoint at amount 100 is the maximum-complexity state that SC-005 measures.
- **FR-026 DC-safe.** Steady-state output DC is ≤ −80 dBFS at every amount, **including for inputs that carry
  even harmonics**: an odd transfer applied to a waveform without half-wave symmetry produces DC. The obvious
  case is the excitation's output at maximum asymmetry (SC-011). The transfer maps 0 → 0 exactly (D-13): after
  `reset()`, an all-zero input block yields an exactly 0.0 output at every amount (SC-011). Any DC-removal
  corner is a documented `static constexpr` (Descriptor definitions).
- **FR-027 Anti-aliasing.** Oversampled or ADAA (line 275), within D-2, such that SC-005 holds at amount 100
  and at both regime boundaries.
- **FR-028 Transparent at zero.** At amount 0 the output nulls against the input within SC-010's tolerance.
  The approach to 0 is continuous (FR-022), so the mechanism — an exact bypass with a crossfade, or a matched
  linear path — must not create a step between 0 and the smallest non-zero amount.
- **FR-029 Not conventional waveshaping.** The descriptor trajectory over 0–100 differs measurably from a plain
  tanh drive sweep (line 277; concept line 348). Gated by SC-007 with OQ-A's thresholds.
- **FR-030 Monotonic levers.** Over 0–100, THD is strictly increasing and H non-decreasing on sine input
  (line 279: "harmonic count and THD monotonic with amount for both stages").
- **FR-031 Calibration and bounds.** The Fold is calibrated at the calibration level (−12 dBFS RMS, the core's
  fixed output level), which is the level the Phase 9 chain feeds it before Shape. Inputs at other levels are
  processed without error. Level dependence is per regime (FR-036): the soft regime is level-dependent, and the compression and folding regimes are level-normalised. The output
  magnitude never exceeds the documented constant `kOutputCeiling` for any finite input, including the core's
  √(N·P₀) ≈ 4.92 peak bound (SC-019).
- **FR-032 Fundamental preservation (decided, clarify Q4).** Across the whole 0–100 sweep, up to and including amount 100, h1 keeps its polarity and phase: ∠T₁ stays within ±5° at E0–E2 on sine and `kSawAnchor` inputs, with no polarity flip (D-8).
  - |T₁| ≥ −6 dB for amounts in [0, `kCompressionEnd`), and **≥ −12 dB** in the folding regime [`kCompressionEnd`, 100]. The relaxed magnitude lets the Fold thin h1 deliberately; the Sub Anchor exists to carry the sub through it.
  - A fold that flips h1 would cancel the Phase 3 anchor at the join. One example is a sine fold past the first Bessel zero, where 2·J₁(g) changes sign.
  - Phase 3 still owns its "sub-band energy preserved within 1 dB" gate (roadmap line 311) through the Anchor; the core philosophy "the fundamental never disappears" (line 43) holds because h1 is thinned, never inverted.
- **FR-033 Stereo.** Identical L and R inputs give bit-identical L and R outputs. Any dynamic element (OQ-A's
  detector) drives both channels from one shared control signal, so a stereo input's image does not wander
  with level (D-9). **C-4 (ratified 2026-10-10) replaced this with per-channel detectors of identical constants and
  instant attack**; SC-013(c) gates them. The mono sum of a stereo input shows no h1
  cancellation (SC-013).
- **FR-034 Control timing.** Amount changes are smoothed per sample, and the smoother and detector time constants are documented `static constexpr` values (the detector's attack and release rule: FR-037). The output is bit-identical for any host block partition of the same input and automation (SC-017).
- **FR-035 Compression mechanism selection (decided, clarify Q2).** The plan picks the compression mechanism by measurement against SC-007. The envelope-normalised waveshaper (x divided by env(x), rescaled) is the expected first candidate; the split-band limiter and the parallel generator (OQ-A (b), (c)) are fallbacks. The plan keeps the cheapest mechanism that passes SC-007 inside the SC-018 budget, and records every candidate's measured SC-007 figures and CPU cost (SC-023).
- **FR-036 Level behaviour per regime (decided, clarify Q3).** The soft regime is static and level-dependent, like saturation (no detector). The compression and folding regimes are level-normalised: their harmonic character is approximately independent of input level (SC-007(b) for compression, SC-025(b) for folding). The normalisation enters continuously at `kSoftEnd`, with no level or spectral step (FR-022, SC-008).
- **FR-037 Detector timing (decided, clarify Q3).** Attack is a documented `static constexpr` constant (`kDetectorAttackMs`). Release is **period-aware**: at least `kDetectorReleasePeriods` = N periods of the played f₀ (N fixed by the plan and documented as a `static constexpr`), so the normalising gain is never modulated within a cycle at E0–E2 (SC-024). How the Fold learns f₀ is the plan's choice: a setter such as `setFundamentalHz` that Phase 9 drives from the core's f₀ (non-finite keeps the previous value per FR-043; default sized for E0), or a release constant sized for the lowest handled f₀. Either way SC-024 holds.

### FR-040 series — Real-time safety, robustness, determinism, portability (both stages)

- **FR-040 Real-time safety.** No allocation, lock, exception or I/O in any audio-thread method; every such
  method is `noexcept`. `Oversampler::process` takes a `const std::function&` (`oversampler.h:236, 322–327`), so
  no `std::function` may be constructed on the audio thread. The plan either stores one at `prepare` or calls
  `upsample` / `downsample` (`oversampler.h:344, 347`) directly. SC-015 gates this with `AllocationScope`.
- **FR-041 Core heap rule (amends Phase 1 FR-040, D-1; decided).** Phase 1's heap-free rule for `ProfundumCore` is relaxed to the project rule: no allocation, lock, exception or I/O on the audio thread; prepare-time allocation is allowed. The excitation reuses the existing `Oversampler` (no duplicate oversampling code; `profundum_core.h` includes `primitives/oversampler.h`). The `Oversampler` process callback is constructed at `prepare`, never per block. The allocation detector gates `processBlock` (SC-015). The audio thread still never allocates. `prepare` stays a control-thread call; it
  is already documented as not real-time safe (`profundum_core.h:75–76`).
- **FR-042 Before prepare.** The core keeps Phase 1 FR-061 (silence). `HarmonicFold::processBlock` before
  `prepare` leaves the buffers unchanged, and its setters store their sanitised values.
- **FR-043 Non-finite input.**
  - Every setter rejects non-finite values with `detail::isFinite` (`core/db_utils.h:118`), never
    `std::isnan`, and keeps the previous value. A finite **denormal** setter value (|x| < `FLT_MIN`, x ≠ 0,
    detected by bit pattern) is flushed to 0.0 before clamping, so the stored and read-back value is exactly
    0.0.
  - `HarmonicFold` replaces a non-finite input sample by 0.0 before it reaches any state, so its IIR and ADAA
    states are never poisoned. This matters because `Wavefolder` and `TanhADAA` both propagate NaN
    (`wavefolder.h:186–188`, `tanh_adaa.h:230–232`). `stateFinite()` stays true.
  - The core extends its `stateFinite()` to cover the excitation state.
- **FR-044 Determinism.** No RNG and no seed. Two instances given the same call sequence produce bit-identical
  output in-process. Cross-toolchain comparisons use tolerances only (`render_fingerprint.h`), never bit-exact
  float goldens.
- **FR-045 Sample rates.** 22.05–192 kHz (Phase 1 FR-041's range). `prepare` at a new rate re-derives the
  oversampling configuration, the smoothers and the DC removal. The oversampling factor may depend on the
  rate, and the plan documents the table.
- **FR-046 Portability.** No narrowing in brace initialisation and no aligned SIMD loads. The code passes
  `node tools/check-portability.js` and `tools/lint-apple-globals.js`.
- **FR-047 Naming** per CLAUDE.md: PascalCase classes, camelCase functions, trailing-underscore members,
  `kPascalCase` constants.
- **FR-048 Shared-header footprint.** Phase 2 edits exactly one existing header, `systems/profundum_core.h`
  (Profundum-owned), and adds `processors/harmonic_fold.h`.
  - There are zero diff lines in every shared header, in particular: `tanh_adaa.h`, `hard_clip_adaa.h`,
    `oversampler.h`, `biquad.h`, `wavefolder.h`, `wavefold_math.h`, `wavefolder_processor.h`, `sigmoid.h`,
    `fast_math.h`, `dc_blocker.h`, `smoother.h`, `envelope_follower.h`, `harmonic_oscillator_bank.h`,
    `harmonic_oscillator_bank_simd.h` and `harmonic_types.h`. The same holds for `spectral_shape_recipe.h`.
  - If the plan finds a shared primitive unfit (E-4, E-5), it has two options. It may build the
    Profundum-local alternative inside the two Phase 2 headers rather than edit the shared one. Or it may
    record an append-only shared addition, with the Ruinae / Seraphis / Vorago / Innexus / Membrum suites
    kept green (roadmap lines 609–610).

### FR-050 series — Tests, audibility, mono, anti-aliasing, CPU

- **FR-050 Tests.**
  - TU placement: `HarmonicFold` TUs go under `dsp/tests/unit/processors/` (→ `dsp_processors_tests`);
    excitation and chain TUs go under `dsp/tests/unit/systems/` (→ `dsp_systems_tests`). Each TU is
    enumerated in `dsp/tests/CMakeLists.txt`.
  - Tags (the Phase 1 pattern): multi-second renders are `[long]` with per-push smoke siblings, and perf TUs
    are `[.perf]`.
  - FP mode: NaN/Inf-injection TUs go in the `-fno-fast-math` block; perf TUs do not.
  - Never `[long]`: NaN/Inf guards, L == R identity and the zero-mix bit-identity.
  - Inputs: the `HarmonicFold` TUs drive it with test-generated `double` inputs (sine, additive
    multi-harmonic). The chained core → excitation → Fold tests live in `dsp_systems_tests`.
- **FR-051 Audibility (theme-level FR, roadmap lines 594–596).** Every lever has a render-descriptor gate on
  rendered audio (SC-001, SC-002, SC-003, SC-006, SC-007): excitation `mix`, `drive` and `asymmetry`, the
  Edge → drive coupling, and the Fold amount in each of its three regimes.
- **FR-052 Mono compatibility (theme-level FR, lines 597–598).** Both stages touch stereo: SC-013.
- **FR-053 Anti-aliasing (theme-level FR, lines 599–600).** Both stages are nonlinear: SC-005, at E0–E2, worst case, ≤ −80 dBFS, with a positive control that proves the measurement can fail. The gate is Phase 1's BH7 total off-grid power on the real multi-harmonic inputs through the whole chain, with the FFT lengthened so every alias sits ≥ 16 bins from the harmonic grid (the test fails if it cannot). This deviates from the roadmap's named `testing-dsp-analysis` method; the deviation is recorded in D-6 (decided, clarify Q8).
- **FR-054 CPU budget is an FR** (lines 602–603): SC-018, never relaxed to pass. The budget is **additive** (decided, clarify Q1): excitation + Fold ≤ 1.5 % of one core per voice on top of the core's own ≤ 1.0 %, whole chain ≤ 2.5 % (matches the roadmap Phase 9 sum 1.0 + 1.5 + 0.3 + 2.0 = 4.8). The Phase 1 close note's 0.4 % reading is superseded.
- **FR-055 Bar integrity (decided, clarify "Bars").** The numeric bars stand as drafted: Edge-to-drive range 12 dB; Fold regime boundaries `kSoftEnd` = 30 and `kCompressionEnd` = 65; audibility bars THD +10 dB and harmonic count +50 %; fundamental preservation ±2° (excitation) and ±5° with no polarity flip and ≥ −12 dB in the folding regime (Fold). A bar that a measurement proves unreachable is re-ruled with the evidence in the main loop and recorded in the Clarifications log, never relaxed silently (SC-027).

---

## Success Criteria

**Notation.**
- "Sweep" means 33 evenly spaced values over the field's range for the excitation controls, and 401 values
  (step 0.25) over [0, 100] for the Fold amount.
- "Max excitation" means `mix = 1`, `drive = 1`, `asymmetry = 1`.
- **Core input states:**
  - `kSineAnchor`, `kTriangleAnchor`, `kSawAnchor`, `kGrowl`, mid grid (`kDefaultControls`);
  - **"SC-009 state"**, Phase 1's aliasing state: `kSawAnchor` with `edge = 1`, `shift = +1`.
- Every fixture with an all-zero pan-offset vector asserts L == R bit-identically through Phase 1's
  `requireLREqual` (SC-013(a)).
- Renders are 48 kHz unless stated. Fold amounts named in an SC include `kSoftEnd` and `kCompressionEnd`
  (D-12).

| ID | Criterion | Threshold | Method (test-name sketch) |
|---|---|---|---|
| **SC-001** | Excitation harmonic count and THD monotonic with drive and mix; **audibility gate** | **Drive sweep** (mix 1, asymmetry 0) on `kSineAnchor` with recipe `edge` ∈ {0, 1}: THD strictly increasing, H non-decreasing, THD(drive 1) − THD(drive 0) ≥ **20 dB**. **Mix sweep** (drive 1, asymmetry 0): THD strictly increasing in `mix` over [0, 1] (THD(mix 1/32) > THD(mix 0) included), THD(mix 1) − THD(mix 0) ≥ **20 dB**. At E0, E1, E2 and C2. | `ProfundumCore_ExcitationDriveMonotonic`, `ProfundumCore_ExcitationMixMonotonic`. |
| **SC-002** | Edge drives the excitation (FR-004) | **(a) Law:** `kEdgeDriveFloor` equals 10^(−12/20) ≈ 0.25119 within 1e-6 relative; `edgeDriveGain(0)` == `kEdgeDriveFloor`, `edgeDriveGain(1)` == 1 (both within 1e-6 relative); over a 33-point `edge` sweep `edgeDriveGain` is strictly increasing and **dB-linear**: 20·log₁₀(`edgeDriveGain`(e)) = 12·(e − 1) within 1e-4 dB at every point. `excitationDriveLaw` is strictly increasing over a 33-point `drive` sweep. For every (drive, edge) on a 5 × 5 grid, `effectiveExcitationDrive()` read at any sample offset of the control interval after the latch equals `excitationDriveLaw(drive)·edgeDriveGain(edge)` within 1e-6 relative (the accessor returns the latched target, FR-004). **(b) Rendered:** at drive 0.5, mix 1, asymmetry 0, recipe `kSineAnchor` with `edge` swept 0 → 1 (33 points), generated distortion G is strictly increasing, and G(edge 1) − G(edge 0) ≥ **6 dB**. At E1 and C2. G subtracts the recipe's own Edge contribution, which is ≤ −30 dB re h1 at depth 1 (Phase 1 FR-010). **(c) Always on, mix independent of Edge (decided, clarify Q7):** at recipe `edge = 0`, mix 1, drive 1, THD(out) − THD(mix-0 reference) ≥ **20 dB** at E1 and C2 (Edge 0 still excites); and at `edge` ∈ {0, 1}, mix 0.5, drive 0.5, the steady output matches 0.5·(mix-0 render + mix-1 render) with residual ≤ **−60 dB** re the output power (mix is a plain blend, unscaled by Edge). | `ProfundumCore_EdgeDriveLaw`, `ProfundumCore_EdgeDrivesExcitationRendered`, `ProfundumCore_EdgeZeroStillExcites`, `ProfundumCore_MixIndependentOfEdge`. |
| **SC-003** | Even harmonics monotonic with asymmetry; the *asymmetric* tail reached | **(a)** Asymmetry sweep at drive 0.5, mix 1, recipe `kSineAnchor` with `edge = 0.5`. The input already carries even harmonics: the recipe's sine residual (`spectral_shape_recipe.h:56, 107, 132`) puts input E_even at ≈ −78 dB re h1, so the gate measures what the lever **adds**. **Generated** even energy E_gen against the asymmetry-0 render (same drive, mix, f₀) is strictly increasing over the 32 non-zero sweep points, and E_gen(asymmetry 1) ≥ **−30 dB** re h1. **Symmetry at asymmetry 0:** E_even(out) ≤ E_even(mix-0 reference) + **3 dB** (an odd transfer creates no even harmonics; it only remaps the input's even content through odd-order intermodulation, D-7). **(b)** At `kTriangleAnchor` (every even n ≤ −50 dB re h1, Phase 1 SC-006), max excitation lowers the odd/even ratio by ≥ **20 dB** and lifts E_even to ≥ −30 dB re h1, which the recipe alone cannot do (Phase 1 FR-023). At E0, E1, E2 and C2. | `ProfundumCore_ExcitationEvenHarmonicsMonotonic`, `ProfundumCore_ExcitationTriangleTail`. |
| **SC-004** | Excitation transparent at zero; Phase 1 untouched | **(a)** With `mix = 0`, renders with (drive, asymmetry) ∈ {(0, 0), (0.5, 0), (1, 1)} are bit-identical to each other and to the **pinned Phase 1 reference**: `profundum_core.h` as of commit `47e43918`, copied into the test tree as `dsp/tests/unit/systems/profundum_phase1_core_ref.h` by a scripted `git show 47e43918:…` extraction whose only change is the enclosing namespace (`Krate::DSP::ProfundumTest::Phase1Ref`, ODR-swept). The copy's diff against the commit (namespace lines only) is quoted in comply. Fixtures: all named states, a D/B/E/S sweep with vibrato, a `Reset` and a `FreeRunning` `noteOn`, host blocks 37 and 512. **(b)** Every Phase 1 TU compiles and passes with **zero diff lines** in its source (SC-021). | `ProfundumCore_ExcitationZeroMixBitIdentical`; Phase 1 suites run as-is. |
| **SC-005** | Aliasing ≤ −80 dBFS at E0–E2, maximum settings (roadmap line 281) | Aliased power ≤ **−80 dBFS** per channel, using the method in the "SC-005 method" note below. | `ProfundumCore_ExcitationNoAliasing`, `HarmonicFold_NoAliasing`, `ProfundumEdgeFold_ChainNoAliasing`, `ProfundumEdgeFold_AliasGatePositiveControl` (full grid `[long]`, per-push smoke at E1 / 48 kHz). |
| **SC-006** | Fold harmonic count and THD monotonic with amount; **audibility gate** per regime | Calibration sine at E0, E1, E2 and C2 (44.1 / 96 kHz in `[long]`), amount sweep: THD strictly increasing over [0, 100] (THD(0.25) > THD(0) included), H non-decreasing over [0, 100]. Audibility: THD rises ≥ **6 dB** within each of the three regimes, and H(100) ≥ **4 × H(`kSoftEnd`)**. On the core's mid grid at C2, C rises by ≥ **0.5 octave** from amount 0 to 100. | `HarmonicFold_ThdAndCountMonotonic`, `HarmonicFold_RegimesAudible`. |
| **SC-007** | Fold is not conventional waveshaping (FR-024, FR-029); **thresholds from OQ-A** | **(a) In-regime:** on the calibration sine at E1 and C2, across [`kSoftEnd`, `kCompressionEnd`], THD rises ≥ **10 dB** and H by ≥ **50 %**, while L stays within **±1 dB** and CF within **±1 dB** of their values at `kSoftEnd`. **(b) Level independence:** at the regime midpoint, input levels −24 / −18 / −12 / −6 dBFS RMS move THD by ≤ **3 dB** in total. A `std::tanh(g·x)` reference (in `double`, with g chosen to give the same THD at −12 dBFS) is computed in the same test and must move by ≥ **12 dB**, so the gate proves its own teeth. **(c) Trajectory:** see the "SC-007(c) method" note below. The distinction from an RMS-matched (level-compensated) tanh is carried by (b): THD is invariant to an output gain k(g), so (b)'s tanh reference stands for the plain and the RMS-matched tanh alike. | `HarmonicFold_CompressionRegimeDensityBeforeLevel`, `HarmonicFold_CompressionLevelIndependent`, `HarmonicFold_TrajectoryDistinctFromTanh`. |
| **SC-008** | Regime transitions produce no level or spectral jump (roadmap line 282) | **(a) Static continuity:** on the amount sweep, adjacent points differ by \|ΔL\| ≤ **0.25 dB**, \|ΔCF\| ≤ **0.25 dB**, \|ΔC\| ≤ **0.05 oct** for every adjacent pair, including the pairs straddling `kSoftEnd`, `kCompressionEnd` and 0 → 0.25. \|ΔTHD\| ≤ max(3 × the median adjacent \|ΔTHD\| over the pairs it applies to, 1 dB) for every adjacent pair whose two points both have THD ≥ **−60 dB** re h1, including the boundary pairs. Below −60 dB the dB step is set by the analysis floor and by the a² growth of any transfer that is analytic and transparent at zero (6 dB at 0.25 → 0.5), not by a jump; continuity there is proved by (b) and SC-010. **(b) C⁰ in amount:** for every grid amount a and ε = 0.05, RMS(y(a+ε) − y(a)) / RMS(y(a)) ≤ **0.02**, with steady-state renders from the same `reset()`. **(c) Under automation:** for a 2 s 0 → 100 → 0 sweep (evaluated as its two monotone segments), steps 0 ↔ 5, and ±2 around each boundary with 0.5 s holds: **sample delta** — no sample-to-sample delta exceeds **1.1 ×** the maximum steady-state \|y[k] − y[k−1]\| over every grid amount the segment or step passes through (endpoints included), measured from steady renders on the 0.25-step grid in the same test. **Frame level** — frames of **4 f₀ periods** (rounded to whole samples; ≈ 97 ms at E1, ≈ 61 ms at C2) hopped by one period: frame-to-frame RMS change ≤ J + max(**1 dB**, 1.5 × the \|ΔL\| that the (a) grid accumulates over the amount range traversed in one hop), where J is the maximum frame-to-frame RMS change of the steady renders of the two segment endpoints under the same framing. No per-frame centroid clause: C needs ≥ 8 periods to resolve harmonics under BH7, and spectral continuity is gated by (a) and (b). Inputs: calibration sine at E1 and C2, and the core's mid grid at C2. | `HarmonicFold_RegimeContinuityStatic`, `HarmonicFold_ContinuousInAmount`, `HarmonicFold_AutomationNoJump`. |
| **SC-009** | Excitation control changes are click-free, including engage from `mix = 0` | Steps 0 → 1 → 0 of each of `mix`, `drive`, `asymmetry` and recipe `edge`, with 0.5 s holds; 10 ms full-range sweeps of each; and `mix` 0 ↔ 0.05. Every sample delta during the transition is ≤ **1.1 ×** the maximum steady-state \|y[k] − y[k−1]\| over every value of the stepped control the transition passes through (endpoints included), measured from steady renders on that control's 33-point grid within the traversed range in the same test. A 0 → 1 → 0 step is evaluated as its two monotone segments. At C2 and E1, mid grid and `kSawAnchor`. | `ProfundumCore_ExcitationControlStepsNoClick`, `ProfundumCore_ExcitationEngageNoClick`. |
| **SC-010** | Fold transparent at zero (roadmap line 282) | At amount 0, the residual power of (output − input) is ≤ **−90 dB** re the input power, on the calibration sine and the core at `kSawAnchor` and the SC-009 state, at C0, C1, C2, C3, C4 and C5, at 44.1 / 48 / 96 kHz. Continuity from 0 into the soft regime is SC-008(a)(b). | `HarmonicFold_TransparentAtZero`. |
| **SC-011** | DC ≤ −80 dBFS (roadmap line 282) | Steady-state DC ≤ **−80 dBFS** per channel, measured over a ≥ 1 s integer-period window starting at t_settle (Descriptor definitions: max(500 ms, 8·τ_slow) from the documented corners), in each of these settings: excitation with (drive, asymmetry) ∈ {0.5, 1}², mix 1; Fold at amounts {`kSoftEnd`, `kCompressionEnd`, 100} on the calibration sine **and** on the max-excitation output (asymmetric input, FR-026); the chain. Inputs `kSineAnchor` and `kSawAnchor`, at E0, E1 and E2. **Zero in, zero out (FR-026, D-13):** `HarmonicFold`, after `reset()`, given an all-zero block of 4096 samples at amounts {0, `kSoftEnd`, `kCompressionEnd`, 100} (amount snapped before the block), outputs exactly 0.0 on both channels. | `ProfundumCore_ExcitationDcFree`, `HarmonicFold_DcFreeWithEvenHarmonicInput`, `ProfundumEdgeFold_ChainDcFree`, `HarmonicFold_ZeroInZeroOut`. |
| **SC-012** | The fundamental survives both stages (FR-010, FR-032) | **(a)** Excitation at drive 0, mix 1: \|T₁\| within **±0.1 dB** and ∠T₁ within **±2°** of the mix-0 output, at E0–E2 and 44.1 / 48 / 96 kHz. **(b)** Over drive × asymmetry × mix ∈ {0, 0.5, 1}³ on `kSineAnchor` and `kSawAnchor`, ∠T₁ is within ±2°. **(c)** Along a mix sweep at max drive and asymmetry, \|T₁\| never falls more than **0.5 dB** below the lower of its endpoint values (no comb dip). **(d)** Fold over the full sweep 0–100 at E0–E2, on sine and `kSawAnchor`: ∠T₁ within **±5°** and no polarity flip at every amount up to and including 100; \|T₁\| ≥ **−6 dB** for amounts below `kCompressionEnd` and ≥ **−12 dB** for amounts in [`kCompressionEnd`, 100] (decided, clarify Q4). In (a)–(c) the T₁ "input" is the mix-0 reference: a second core instance with the same call sequence, f₀, recipe state and `ExcitationControls` except `mix = 0` (Descriptor definitions). | `ProfundumCore_ExcitationFundamentalPreserved`, `HarmonicFold_FundamentalPreserved`. |
| **SC-013** | Mono compatibility (theme-level FR) | **(a)** With all-zero pan offsets, every excitation fixture has L == R bit-identically; `HarmonicFold` given L ≡ R input returns L ≡ R output bit-identically. **(b)** With a non-zero pan vector (+0.5 on even n, −0.5 on odd n ≥ 3, 0 on h1), at max excitation, at E1 and C2: the inter-channel h1 phase difference is ≤ **2°**, and h1 power of (L+R)/2 is within **0.5 dB** of the mean of the L and R h1 powers. **(c)** The same holds for `HarmonicFold` at amount 100 fed that panned core output. **Per-channel detectors (FR-033; C-4 RATIFIED 2026-10-10, replacing D-9's shared detector):** identical constants, instant attack (`kDetectorAttackMs` = 0), detector input \|x_ch\| per channel. `HarmonicFold` fed a calibration sine at E1 with R 6 dB below L, and a level step on both channels from −24 to −6 dBFS RMS (L) with 1 s holds: the L/R output ratio is within **±0.1 dB** of the input ratio at amounts {47.5, 82.5, 100}, at both levels and on 4-period frames hopped by one period through the step. (The shared detector reads 0.53 dB against the input's 6.02 dB, the test's teeth.) | `ProfundumCore_ExcitationLREqualAtZeroPan`, `HarmonicFold_LREqualForIdenticalInput`, `ProfundumCore_ExcitationMonoSum`, `HarmonicFold_MonoSum`, `HarmonicFold_StereoDetectorPerChannel`. |
| **SC-014** | Retrigger policy with the excitation on (FR-013) | Phase 1 SC-012's clauses at max excitation. **`Reset`:** the first output sample after `noteOn` is exactly 0.0, and renders after `noteOn` from different prior states (f₀, Controls, ExcitationControls, elapsed time) are bit-identical. **`FreeRunning`, same f₀ and controls:** bit-identical to an uninterrupted render. | `ProfundumCore_ExcitationRetriggerPolicy`. |
| **SC-015** | RT safety | Zero allocations (`AllocationScope`; the allocation detector gates `processBlock`, FR-041) across: `setExcitation`, `setControls`, `noteOn` (both policies), `processBlock` (stage off, engaging and on; host blocks 1, 37, 64, 512, 4096); `HarmonicFold::setAmount` and `processBlock`, including `numSamples > maxBlockSize`. Every audio-thread method is `noexcept` (static_assert). | `ProfundumCore_ExcitationNoAllocation`, `HarmonicFold_NoAllocation`. |
| **SC-016** | Robustness | NaN / ±Inf values (by bit pattern) in each `ExcitationControls` field and in `setAmount` keep the previous value. A finite denormal (e.g. bit pattern 0x00000001 and 1e-40f) in each of those is flushed: the read-back value is exactly 0.0 (FR-043), and the output stays finite and bounded. NaN/Inf samples inside a Fold input block give finite output with `stateFinite()` true, and the block is bit-identical to one with 0.0 at those positions. Out-of-range values clamp (amount −5 → 0, 250 → 100; mix 2 → 1). `prepare` at 22.05 and 192 kHz renders finite and bounded at max settings. `processBlock(…, 0)` and null buffers are no-ops. | `ProfundumCore_ExcitationNonFinite`, `HarmonicFold_NonFinite` (both in the `-fno-fast-math` list), `HarmonicFold_RangeAndRates`. |
| **SC-017** | Determinism and block-size independence | Two instances, same call sequence: bit-identical in-process. With no setter calls between blocks, host-block partitions {512, 64, 37, 1} are bit-identical, for the core at max excitation with an `f0PerSample` vibrato and for `HarmonicFold` across a pending amount ramp. After a rate change 48 → 96 → 44.1 kHz via `prepare`, SC-005 (E1 smoke) and SC-012(a) hold again at the new rate. | `ProfundumCore_ExcitationDeterministic`, `HarmonicFold_BlockSizeIndependent`, `ProfundumEdgeFold_SampleRateChange`. |
| **SC-018** | **CPU budget** (roadmap line 283) | **≤ 1.5 % of one core per voice** @ 48 kHz for **both stages on, additive on top of the core's own ≤ 1.0 %** (decided, clarify Q1), where stage cost = [time(core + excitation + Fold) − time(core, mix 0)] / audio time, with host block 64 and 64 partials. Load: the core runs Phase 1 SC-013's load (D/B/E/S sweep + 5 Hz vibrato); max excitation with a 0.5 Hz drive automation; Fold with a 0.5 Hz 0–100 sweep. The excitation-alone and Fold-alone figures and the total are recorded. The core alone (mix 0) still meets Phase 1 SC-013's **≤ 1.0 %**, and the whole chain time(core + excitation + Fold) / audio time is **≤ 2.5 %** per voice (gated, same load, pinned). See D-10. | `ProfundumEdgeFold_CpuBudget` `[.perf]`, run alone and pinned (`node tools/run-cpu-tests.js dsp_systems_tests` / `tools/pin-perf-cores.ps1`), median of 5. |
| **SC-019** | Output bounded | Across every fixture of this spec, plus the Phase 1 SC-009 state at every MIDI note 12–60 at max settings: \|y\| ≤ `kOutputCeiling` for `HarmonicFold`, and ≤ the core's documented excitation ceiling. Both constants are `static_assert`ed below the core's effective clamp of 2·`kBankHeadroom` = 8. | `HarmonicFold_OutputBounded`, `ProfundumCore_ExcitationOutputBounded`. |
| **SC-020** | Excitation level law (FR-011, D-3) | At mix 1, across a drive sweep × asymmetry ∈ {0, 0.5, 1}: output RMS within **±1.5 dB** of input RMS, on `kSineAnchor`, mid grid and `kSawAnchor`, at E1 and C2. | `ProfundumCore_ExcitationLevelLaw`. |
| **SC-021** | Footprint (FR-048) | A scripted `git diff` against the phase base `47e43918` (run in comply, output quoted) shows: zero diff lines in every shared header FR-048 lists, in `spectral_shape_recipe.h`, and in every Phase 1 test TU (`profundum_core_test.cpp`, `profundum_core_spectral_test.cpp`, `profundum_core_nonfinite_test.cpp`, `profundum_core_perf_test.cpp`, `spectral_shape_recipe_test.cpp` and both Phase 1 helper headers); every Phase 1 public declaration of `ProfundumCore` still present verbatim; `dsp_processors_tests` and `dsp_systems_tests` green. | `node` diff-check script recorded in comply. |
| **SC-022** | Recorded figures (**recorded, not gated**; E-8, E-9, E-10) | Each figure is computed by a test, printed, and quoted in comply: **(a)** SC-005's aliased power, per octave from E2 to C5, for the excitation, the Fold at 100 and the chain (`[long]`); **(b)** SC-005's aliased power at 22.05 and 192 kHz at E1; **(c)** ∠T₁ and \|T₁\| of the excitation (drive 0, mix 1) at f₀ = 8 Hz, the core's f₀ floor; **(d)** the SC-007(c) median distance against an RMS-matched tanh. A missing figure fails the test; its value does not. | `ProfundumEdgeFold_RecordedAliasingByOctave` `[long]`, `ProfundumEdgeFold_RecordedAliasingRateExtremes`, `ProfundumCore_ExcitationRecordedPhaseAt8Hz`, (d) printed by `HarmonicFold_TrajectoryDistinctFromTanh`. |
| **SC-023** | Compression mechanism record (FR-035) | `plan.md` records, for every candidate mechanism tried, the measured SC-007(a)(b)(c) figures and the SC-018 cost; the chosen mechanism passes SC-007 and is the cheapest that does inside the SC-018 budget. Comply quotes that table and the final SC-007 and SC-018 logs. | `node` doc-check recorded in comply; SC-007 and SC-018 tests. |
| **SC-024** | Period-aware detector (FR-037) | **(a)** `kDetectorAttackMs` and `kDetectorReleasePeriods` exist as `static constexpr`, and the release in effect at f₀ = 20.6 Hz is ≥ `kDetectorReleasePeriods` / 20.6 s. **(b)** Calibration sine at f₀ ∈ {20, 40, 80} Hz at 48 kHz (integer periods 2400 / 1200 / 600 samples) at the compression-regime midpoint and the folding-regime midpoint, steady state, before and after a −24 → −6 dBFS step: periodicity residual RMS(y[k+P] − y[k]) / RMS(y) ≤ **−50 dB** over a ≥ 1 s window (no in-cycle gain modulation). A reference x / env(x) with a 1 ms release computed in `double` in the same test reads **> −30 dB**, so the gate proves its own teeth. | `HarmonicFold_DetectorTimingConstants`, `HarmonicFold_NoInCycleGainModulation`. |
| **SC-025** | Level behaviour per regime (FR-023, FR-025, FR-036) | **(a) Soft regime level-dependent:** at amount `kSoftEnd`/2, calibration sine at E1 and C2, input levels −24 / −18 / −12 / −6 dBFS RMS move THD by ≥ **12 dB** in total. **(b) Folding regime level-normalised:** at amount (`kCompressionEnd` + 100)/2, the same levels move THD by ≤ **3 dB** in total. **(c)** Continuity where normalisation enters at `kSoftEnd` is gated by SC-008(a)(b)(c). | `HarmonicFold_SoftRegimeLevelDependent`, `HarmonicFold_FoldRegimeLevelNormalised`. |
| **SC-026** | Zero base-rate latency, both stages (FR-008, FR-021, FR-027; decided, clarify Q5) | `HarmonicFold::latencySamples()` is `static_assert`ed 0. Cross-correlation lag between input and output is **0 samples** for the Fold at amount 2 on the calibration sine. For the excitation (drive 0, mix 1) against the mix-0 render of the core, at E1 and C2 (**amended 2026-10-11, build defect D-2:** the ratified C-5 split bypass delays only the band above `kSplitHz`, which the f²-weighted full-band argmax reads as −4 on the mid grid; the requirement protects the Anchor's h1 lock and the attack, so it is measured there): (a) `kSineAnchor` full-band argmax **0**; (b) mid grid h1 |∠T₁| ≤ **0.5°** and the argmax of both renders through an identical test-side 4th-order **125 Hz** low-pass **0** (**D-3**, 2026-10-11: the split's own 2nd-order 500 Hz crossover delays 131 Hz by 0.37 and 196 Hz by 0.83 base samples by construction, so 125 Hz is the highest cutoff where the design promises less than half a sample; the 250 Hz-band argmax D-2 first named is recorded, not gated); (c) mid grid full-band argmax, 250 Hz-band argmax and high-band group delay at 1 kHz and 4 kHz recorded as SC-022(i), the full-band and high-band figures ≤ **8** base samples. A scripted grep shows neither Phase 2 header references an FIR quality or mode (`Standard`, `High`, `LinearPhase`). | `HarmonicFold_ZeroLatency`, `ProfundumCore_ExcitationZeroLatency`; grep recorded in comply. |
| **SC-027** | Bar integrity (FR-055) | A `node` script extracts every numeric threshold of the SC table at the start of build and again at comply. Any difference is allowed only if it cites a re-ruling entry in the Clarifications log with the measurement evidence. The comply report quotes the script output. | `node` diff-check script recorded in comply. |

**SC-005 method.**
- **Fixtures:**
  - excitation at max excitation and at (drive 1, asymmetry 0), mix 1;
  - `HarmonicFold` at amounts {`kSoftEnd`, `kCompressionEnd`, 100}, fed the core with the excitation off;
  - the chain: max excitation → Fold 100.
- **Inputs:** the SC-009 state, `kGrowl`, `kSawAnchor`, mid grid and `kSineAnchor`.
- **Notes and rates:** E0, E1 and E2, at 44.1 / 48 / 96 kHz. Each frame starts at t_settle after note start
  (Descriptor definitions).
- **FFT length:** the smallest power of two that satisfies Phase 1's rule (`aliasFftLength`) **and** places
  every first- to fourth-order alias ≥ 16 bins from the harmonic grid (D-6). The offset of m·fs from the
  nearest multiple of f₀ must be ≥ 16 bins for m = 1…4.
- **Guards:**
  - the excluded fraction is ≤ 50 %;
  - the alias-offset guard fails the test if the rule cannot be met.
- **Positive control:** the same measurement on a naive base-rate `std::tanh` at the same effective drive,
  with the same input at E1 / 48 kHz, must read **> −80 dBFS**.

**SC-007(c) method.** A plain `std::tanh(g·x)` reference sweep (in `double`) runs on the same calibration
input, with g log-spaced **0.1…100** over 401 points, so its THD range starts far below any plausible regime
start (g = 1 already reads −39.8 dB THD at peak 0.35521). The sweep is parameterised by THD. Over the THD
interval that the Fold's compression regime covers, take 33 THD-matched points and compute √(ΔL² + ΔCF²) at
each. The median distance must be ≥ **3 dB**. If any of the 33 Fold THD values lies outside the reference's
THD range, the test fails (no extrapolation).

An RMS-matched tanh (`std::tanh(g·x)·k(g)`) is not gated here: RMS matching makes ΔL = 0 by construction, and
its crest factor stays within ≈ 0.6 dB of a sine's 3.01 dB until THD ≈ −23 dB (scratch Node script this
session, peak 0.35521: g = 0.1 / 1 / 2 / 3 / 5 → THD −79.6 / −39.8 / −28.5 / −22.6 / −16.4 dB, CF 3.01 / 2.92 /
2.69 / 2.38 / 1.76 dB), so a compliant Fold that holds CF within ±1 dB (SC-007(a)) would sit
inside 1 dB of it. What separates harmonic compression from a level-compensated tanh is level independence,
which SC-007(b) gates against a THD-matched tanh with its own ≥ 12 dB teeth (THD is unchanged by any output
gain k(g), so that contrast covers the RMS-matched tanh too). The RMS-matched median distance
is recorded in SC-022's report for information.

Evaluation (roadmap line 283, "per voice both stages on"): `[.listen]` renders at E0, E1, E2 and C2 for:
- the excitation drive, asymmetry and edge sweeps;
- a Fold 0 → 100 sweep on the six named states;
- the chain at maximum settings.

The renders are written for listening; the gates above decide pass/fail.

---

## Edge Cases

- **E-1 RT boundaries.**
  - `prepare` on either component runs on the control thread and may allocate (`Oversampler::prepare`,
    `oversampler.h:287–294`).
  - A `Reset` `noteOn` clears the excitation state from the audio thread (FR-013). This is allowed only
    because it is array/filter-state fills with no allocation (SC-015).
  - `HarmonicFold::reset()` is a control-thread call. Its RT-safe use inside a voice is the plan's to state.
- **E-2 Oversampler block ceiling.** `Oversampler::process` silently leaves its buffers untouched when
  `numSamples > maxBlockSize_` (`oversampler.h:843–845`). Nothing would fail; the stage would simply vanish for
  that block.
  - The core can prepare its oversampler with `maxBlockSize = kControlInterval`, because it already renders
    in ≤ 16-sample runs (`profundum_core.h:183–211`).
  - `HarmonicFold` chunks every call (FR-021).
  - SC-015 runs host blocks up to 4096.
- **E-3 `std::function` callback.** `Oversampler::StereoCallback` is a `std::function`
  (`oversampler.h:236`). Building one from a capturing lambda at call time can allocate once the capture
  exceeds the library's small-buffer size. FR-040 forbids constructing one on the audio thread.
- **E-4 `TanhADAA` numerics at bass frequencies.** First-order ADAA in `float` divides a difference of `F1`
  values (magnitude ≈ |x·d|) by `d·(x − x₁)`.
  - At E0 under 4× oversampling, a −12 dBFS sine moves only ≈ 1e-4 to 2e-4 per sample. Float cancellation in
    the numerator then becomes a broadband error floor.
  - Near waveform peaks |dx| < `kEpsilon` = 1e-5 (`tanh_adaa.h:165`), and the code switches to
    `fastTanh(midpoint)` (lines 256–259), a different function with ±0.05 % error. The switching residual is
    also broadband.
  - Both errors land **off** the harmonic grid, so SC-005 counts them as aliasing.
  - The plan must measure `TanhADAA` against SC-005 before adopting it. If it fails, the plan uses a
    Profundum-local evaluation (for example `double` ADAA, or oversampling with an exact smooth transfer)
    inside the Phase 2 headers (FR-048).
- **E-5 `fastTanh` saturation step.** `FastMath::fastTanh` returns exactly ±1 for |x| ≥ 3.5
  (`fast_math.h:76–81`), and `Sigmoid::tanh` is the same function (`sigmoid.h:68–70`). The step of ≈ 1.8e-3
  (≈ −55 dB) is a C⁰ discontinuity in the transfer. At high drive every waveform crosses it, which generates
  broadband aliasing that oversampling only partly removes. No transfer used at maximum drive may contain such a
  step.
- **E-6 Folding primitives are not C⁰ at zero.** `WavefoldMath::sineFold` jumps from x to ≈ 0.001·x at
  `gain = 0.001` (`wavefold_math.h:375–380`). `Wavefolder`'s Triangle returns 0 below `foldAmount = 0.001`
  (`wavefolder.h:251–254`). `WavefolderProcessor` clamps its fold amount to ≥ 0.1 and skips the DC blocker
  below `mix = 1e-4` (`wavefolder_processor.h:122, 260–263, 345–347`). FR-022 / FR-028 therefore require the
  folding regime to be entered from the compression regime's output continuously. These primitives cannot be
  driven from amount 0 directly.
- **E-7 Input peaks.** The core's RMS is fixed, but its peak reaches ≈ 4.92 per channel in high-edge states
  (`profundum_core.h:54–61`). Drive laws, ADAA arguments and `kOutputCeiling` must stay bounded there
  (SC-019).
- **E-8 Rate extremes.**
  - At 22.05 kHz the core's cap is 0.8 × 11 025 = 8.82 kHz.
  - At 192 kHz the oversampling factor may be lower (FR-045).
  - The aliasing gate runs at 44.1 / 48 / 96 kHz. At 22.05 and 192 kHz the output is only asserted finite and
    bounded (SC-016), with the aliasing figure recorded (SC-022(b)).
- **E-9 f₀ below E0.** The core clamps f₀ at 8 Hz (Phase 1 FR-062).
  - FR-010's ±2° h1 bound and SC-005 are gated at E0–E2 (the roadmap's range).
  - Below E0 the DC removal's effect on h1 is recorded, not gated.
  - A DC remover that meets SC-012 at 20.6 Hz will still turn an 8 Hz fundamental, and the comply report says
    how far (SC-022(c)).
- **E-10 Notes above E2.** As notes rise, the core's 64 partials reach the cap (C4 × 64 ≈ 16.7 kHz), and
  intermodulation of high partials can alias. The roadmap rules the bound at E0–E2 (line 281), so above E2
  the aliasing figure is recorded per octave to C5 in the `[long]` set, not gated (SC-022(a)).
- **E-11 Denormals.** Phase 1's FTZ/DAZ precondition (`profundum_core.h:37–38`) extends to the excitation and
  Fold filters, whose tails decay toward denormals. Tests run with `enable_ftz_daz.h`, as in Phase 1.
- **E-12 Seed determinism.** Not applicable: neither stage has an RNG (FR-044).
- **E-13 Large pitch jumps with the excitation on.** A `FreeRunning` `noteOn` more than 1 semitone away
  triggers the bank's 3 ms crossfade (Phase 1 E-7). With the excitation on, the result is asserted bounded
  and finite only; Phase 4 owns click-free note transitions.
- **E-14 Asymmetric input to the Fold.** The excitation's even harmonics reach the Fold in the Phase 9 chain.
  SC-011 tests exactly that input for DC.
- **E-15 Alias measurement blind spot.** Every aliased product of a harmonic input lands at |k·f₀ − m·fs|,
  which is offset from the harmonic grid by the distance from m·fs to the nearest multiple of f₀. That offset is
  the same for every k. With Phase 1's FFT-length rule it falls inside the ±8-bin exclusion in three of the
  nine E0–E2 × {44.1, 48, 96} kHz cells:

  | Rate | Note | m | Offset | Bins |
  |---|---|---|---|---|
  | 48 kHz | E1 | 1 | 2.01 Hz | 5.5 |
  | 48 kHz | E2 | 2 | 4.03 Hz | 5.5 |
  | 96 kHz | E2 | 1 | 4.03 Hz | 5.5 |

  An unextended gate would be blind to the dominant first fold in exactly those cells. The figures were computed
  this session by a scratch Node script. D-6's FFT-length extension and the SC-005 offset guard close the gap.

---

## Decisions taken where the roadmap is silent (ratified in the clarify session, 2026-10-10)

Every value below is also written into the FR/SC it governs. This list records the rationale and the
alternative for clarify; it is not the only place any value appears.

- **D-1 Heap storage in the core** (FR-041). **Decided: proposal adopted.**
  - Background: composing `Oversampler` adds prepare-time `std::vector` storage (`oversampler.h:383–384,
    473–479`), which contradicts Phase 1 FR-040's "no heap allocation anywhere".
  - Proposed: amend that rule to "no audio-thread allocation; prepare-time allocation permitted". The core's
    `prepare` is already documented as not real-time safe.
  - Alternative: keep the core heap-free with a fixed-capacity oversampling path. That is new code duplicating
    `Oversampler`, against the reuse-first rule.
- **D-2 Zero added latency** (FR-021, FR-027, FR-008, SC-026). **Decided (Q5): zero for both stages, proposal adopted.**
  - Proposed: both stages add no latency at the base rate. The roadmap rejected latency for this bass (line 161:
    "must be sample-accurate on attack"), and Phase 3 phase-locks the anchor to the core's h1 (lines 295–297),
    so a delayed voice path would misalign the join.
  - Consequence: the FIR LinearPhase modes are excluded (15–62 base-rate samples of latency,
    `oversampler.h:85–86, 104–105, 451–470`). The IIR Economy filters (8-pole Butterworth at 0.45·fs,
    `oversampler.h:491–524`) attenuate content just above the base Nyquist by only ≈ 14 dB at 0.55·fs, so
    meeting SC-005 at zero latency probably needs ADAA, a higher factor, or both. Proving it is the plan's work.
  - Alternative: allow FIR and expose `latencySamples()`, with Phase 9 delaying the anchor to match.
- **D-3 Excitation level law** (FR-011, SC-020). **Decided (Q6): static makeup, ±1.5 dB, proposal adopted.**
  - Proposed: a static makeup law, with no dynamic AGC, holding the output RMS within ±1.5 dB of the input RMS
    across drive and asymmetry at mix 1. Drive is then not a volume knob, following Phase 1 FR-005's
    principle.
  - Alternative: uncompensated, so level rises with drive and Phase 6 / Phase 9 compensate.
- **D-4 Edge → drive law** (FR-004, SC-002). **Decided (Q7): proposal adopted, 12 dB range, always on.**
  - Proposed: dB-linear, D_eff[dB] = d(drive)[dB] + `kEdgeDriveRangeDb`·(edge − 1), with
    `kEdgeDriveRangeDb` = 12 dB. Then g(0) = −12 dB (`kEdgeDriveFloor` = 10^(−12/20) ≈ 0.25119) and g(1) = 1.
    At Edge 0 the excitation still works, but 12 dB gentler. The law and the 12 dB range are written into
    FR-004 and asserted by SC-002(a) (floor value within 1e-6, dB-linearity within 1e-4 dB at 33 points).
  - The 6 dB rendered bar of SC-002(b) is set below what 12 dB of drive change produces at drive 0.5; the two
    values move together, so clarify changing the range must revisit the bar.
  - Alternative: a different range (e.g. 6 or 18 dB) or a linear-amplitude g.
- **D-5 Ranges and defaults.**
  - Excitation `mix`, `drive` and `asymmetry` are in [0, 1], with defaults 0, 0.5 and 0. `asymmetry` is
    unipolar: the roadmap's control "adds even harmonics" (line 269), and the sign of the bias only flips the
    waveform's polarity asymmetry.
  - The Fold amount is in [0, 100] (line 271), default 0.
- **D-6 Aliasing measure and method deviation** (SC-005, E-15). **Decided (Q8): replacement adopted; the deviation from the roadmap's named method is recorded here.**
  - The roadmap reads "aliasing ≤ −80 dBFS at E0–E2 with maximum drive/fold (`testing-dsp-analysis` method)"
    (line 281), so it names a method.
  - Proposed: **replace** that skill's `measureAliasing` (`tests/test_helpers/spectral_analysis.h:287`) with
    Phase 1 SC-009's BH7 method: **total** off-grid power summed over the band, re a full-scale sine, rather
    than the largest single component. Reasons: `measureAliasing` drives a single sine through a `float(float)`
    callable, so it cannot measure a stateful stereo stage on a multi-harmonic bass input; it windows with Hann
    (lines 307–309), whose sidelobes leak harmonic energy above −80 dBFS at bass f₀; and it counts only
    predicted alias bins up to `maxHarmonic` (line 112), missing intermodulation and numerical floors (E-4).
    The replacement is the stricter of the two: it counts everything off the grid. The skill's
    `compareAliasing` idea survives as SC-005's positive control.
  - Alternative: use `measureAliasing` as the roadmap names it, on a single test sine per stage, accepting that
    it cannot see the core's multi-harmonic input or the chain.
  - The FFT length is extended so that every m = 1…4 alias sits ≥ 16 bins off the grid (E-15). For example, at
    48 kHz / E1 the length grows from 2¹⁷ to 2¹⁹.
  - DC in dBFS is 20·log₁₀(|mean|) re 1.0.
- **D-7 Audibility bars.**
  - Excitation: drive and mix spans ≥ 20 dB THD; asymmetry generates ≥ −30 dB re h1 of even energy
    (generated, against the asymmetry-0 render) with E_even at asymmetry 0 ≤ the input's + 3 dB; Edge coupling
    ≥ 6 dB generated distortion; triangle tail ≥ 20 dB odd/even drop.
  - Why the asymmetry-0 bar is relative: the recipe's sine residual R(1) = 1e-3 (`spectral_shape_recipe.h:56`,
    applied as `residual·2^(−p·u)` with p = 3 at depth 1, lines 107, 132) puts h2 at 1.25e-4 (−78 dB re h1)
    before any excitation, so an absolute −80 dB bar fails every implementation. The +3 dB margin covers
    odd-order intermodulation, which remaps input even content (h1·h1·h2 → h2, h4) at a level that scales with
    THD (≈ −30 dB at drive 0.5). Alternative for clarify: +1 dB.
  - Fold: ≥ 6 dB THD rise per regime; H(100) ≥ 4·H(`kSoftEnd`); ≥ 0.5 octave centroid rise on mid grid.
  - The roadmap says "monotonic" without a figure; these follow Phase 1's D-7 scale (6 / 20 dB).
- **D-8 Fundamental-preservation bars** (FR-010, FR-032, SC-012).
  - Excitation: ±0.1 dB / ±2° small-signal; ±2° at every setting; ≤ 0.5 dB mix-sweep dip.
  - Fold: ±5°, ≥ −6 dB, no polarity flip.
  - Rationale: Phase 3 must keep "sub-band energy preserved within 1 dB" through maximum Fold + Character
    (line 311), and the anchor joins in phase with the core's h1.
- **D-9 Fold stereo linking** (FR-033; superseded by plan C-4, ratified 2026-10-10: per-channel detectors, identical
  constants, instant attack; SC-013(c) now gates the ±0.1 dB ratio clause). Original text: any dynamic detector in the Fold is shared across L and R, so a
  stereo input's image does not wander with level. SC-013(c) gates it: a 6 dB L/R input ratio survives within
  ±1 dB, and the ratio stays inside its pre/post-step band ± 0.1 dB through a −24 → −6 dBFS step.
  Alternative: independent per-channel detectors.
- **D-10 CPU budget reading** (SC-018). **Decided (Q1): additive.**
  - Proposed: the roadmap's "CPU ≤ 1.5 % per voice both stages on" (line 283) is the cost of the two Phase 2
    stages, **on top of** the core's own ≤ 1.0 % (Phase 1 SC-013). This is the only reading consistent with
    Phase 9's "per-voice budgets ≈ 4.8 %" (line 494): 1.0 (core) + 1.5 (Phase 2) + 0.3 (anchor) + 2.0 (Shape +
    Character) = 4.8.
  - Resolved (clarify Q1): the additive reading is decided. The session note that relayed Phase 1's close ("measured against the 0.4 % of headroom left under the 1 % budget") was wrong and is superseded. Excitation + Fold ≤ 1.5 % on top of the core's 1.0 % (FR-054, SC-018).
- **D-11 Calibration level** (FR-031). The Fold is calibrated at −12 dBFS RMS, the core's fixed output level.
  Phase 5's Shape sits between them in the Phase 9 chain and may change the level; Phase 9 measures the
  combination.
- **D-12 Regime boundaries.** Decided (keep as drafted, FR-055): `kSoftEnd` = 30 and `kCompressionEnd` = 65: low / middle / high per the
  concept (lines 358–368), with the folding regime given the widest span because it is the most
  level-sensitive. The plan may move them with a recorded measurement.
- **D-13 Zero in, zero out.** Both static transfers map 0 → 0 exactly at every setting, by static offset
  compensation of any bias. Without it:
  - a `Reset` note's first sample is not 0.0 (breaking Phase 1 SC-012 with the excitation on);
  - a DC blocker freshly reset at onset emits a step of f(b) — the click `wavefolder.h:278–283` documents for
    Lockhart.

## Open Questions (deferred to this spec by the roadmap)

- **OQ-A Harmonic compression: definition. RESOLVED (clarify Q2, 2026-10-10).** Roadmap line 273 defers the definition to this spec. The definition is encoded in FR-024 and gated by SC-007 with the thresholds as drafted (THD +10 dB, H +50 %, L and CF ±1 dB, ≤ 3 dB over −24…−6 dBFS against ≥ 12 dB for a static tanh in the same test, ≥ 3 dB median trajectory distance from a plain tanh sweep). The mechanism is the plan's choice by measurement (FR-035, SC-023); candidates: (a) envelope-normalised waveshaper (expected first), (b) split-band limiter, (c) parallel harmonic generator. Detector timing: FR-037.

## Traceability

| Roadmap statement (line) | FR | SC |
|---|---|---|
| Edge excitation inside the core, oversampled soft clip, `TanhADAA` / `Oversampler` (266–267, 123–124) | FR-001, FR-003, FR-008, FR-041 | SC-005, SC-015 |
| Continuously blendable, optional (266) | FR-002, FR-006, FR-007 | SC-001, SC-004, SC-009, SC-012(c) |
| Drive influenced by Edge; Edge generates harmonics (266–268, 222–223) | FR-004 | SC-002 |
| Asymmetry adds even harmonics, drive adds clipping; the *asymmetric → clipped → increasingly complex* tail (268–269, 232–234) | FR-003, FR-005, FR-014 | SC-001, SC-003 |
| Band-limited; worst case lowest notes at max drive (269–270) | FR-008 | SC-005 |
| `HarmonicFold` L2, one 0–100 control, three regimes, C⁰ (271–274) | FR-020, FR-021, FR-022, FR-023, FR-025 | SC-006, SC-008 |
| Harmonic compression, defined in spec (273–274) | FR-024, OQ-A | SC-007 |
| Oversampled or ADAA, DC-safe (275) | FR-026, FR-027 | SC-005, SC-011 |
| The Anchor never passes through it (275–276, 303–305) | Non-Goals; FR-032 (h1 kept for the join) | SC-012(d) |
| Must not behave like conventional waveshaping; distinct from a tanh sweep (276–277) | FR-024, FR-029 | SC-007 |
| Harmonic count and THD monotonic, both stages (279) | FR-030, FR-003 | SC-001, SC-006 |
| Even-harmonic energy monotonic with asymmetry (279–280) | FR-005 | SC-003 |
| Aliasing ≤ −80 dBFS at E0–E2, max drive/fold, `testing-dsp-analysis` method (280–281); method deviation: D-6 | FR-008, FR-027, FR-053 | SC-005, SC-022 |
| No level or spectral jump at regime transitions (281–282) | FR-022 | SC-008 |
| DC ≤ −80 dBFS (282) | FR-009, FR-026 | SC-011 |
| Transparent at zero (282) | FR-006, FR-028 | SC-004, SC-010 |
| CPU ≤ 1.5 % per voice, both stages on, additive to the core's 1.0 % (283; 494) | FR-054, D-10 | SC-018 |
| Fundamental never disappears; anchor through max Fold (43, 311) | FR-010, FR-032 | SC-012 |
| Compression regime defined; mechanism by measurement (273–274) | FR-024, FR-035, FR-036, FR-037 | SC-007, SC-023, SC-024, SC-025 |
| Zero latency; anchor join stays sample-aligned (161, 295–297) | FR-008, FR-021, FR-027 | SC-026 |
| Bars held; re-ruled only with evidence (590–610) | FR-055 | SC-027 |
| Morph-ready plain controls (435, 447) | FR-002, FR-021 | — |
| Cross-cutting: RT, audibility, mono, anti-aliasing, layers, ODR, goldens, portability, shared headers (590–610) | FR-040–FR-048, FR-050–FR-053 | SC-013–SC-017, SC-019, SC-021 |

## Assumptions

- Phase 9 feeds `HarmonicFold` from the core (+ excitation) through Phase 5's Shape, per voice by default
  (OQ-7). The Fold's calibration at −12 dBFS RMS (D-11) is the reference, and Phase 9 measures the
  combination.
- Phase 3 phase-locks the anchor to the core's h1 and joins after Character. FR-010 and FR-032 keep h1's
  phase and polarity so that join does not cancel. Phase 3 still owns its own "within 1 dB" gate.
- Phase 6's Mass law drives `HarmonicFold::setAmount`, and Phase 8 interpolates `ExcitationControls` and the
  Fold amount as plain floats. Neither needs more from Phase 2 than the contracts above.
- The core's per-channel RMS stays −12 dBFS in every recipe state (Phase 1 SC-018), so the excitation's drive
  is calibrated against a known level. Its peak varies with state (E-7).

## Review notes

Challenge round 1 (2026-10-10): every issue was applied; none was rejected. Three resolutions chose among the
reviewer's options or refined them, recorded here so clarify can see why:

- **SC-003(a) symmetry margin +3 dB, not +1 dB.** The reviewer offered "≤ E_even(mix-0) + 1 dB". An odd
  transfer still remaps the input's −78 dB even content through odd-order intermodulation at a level that
  scales with THD, so +1 dB risks failing a correct odd transfer at drive 0.5. +3 dB is proposed with +1 dB
  listed as the D-7 alternative. Monotonicity uses the complex-difference E_gen so that a generated h2 in
  anti-phase with the input's h2 cannot fake a dip.
- **SC-007(c) RMS-matched clause removed from the gate rather than given a density coordinate.** Adding ΔC or
  ΔH at matched THD would impose a spectral-shape constraint that OQ-A's candidate (a) (an envelope-normalised
  tanh) cannot meet by design, without a roadmap basis. SC-007(b) already separates the regime from any
  level-compensated tanh; the RMS-matched distance is recorded (SC-022(d)). The plain-tanh sweep now starts at
  g = 0.1 and fails on an unmatched point, as suggested.
- **SC-008(c) frame-RMS bound** adds, beyond the reviewer's "1 dB above the endpoints' own jitter", an
  allowance of 1.5 × the static-grid ΔL traversed in one hop. A 4-period frame hopped by one period at the
  sweep's 100 amount/s traverses ≈ 2.4 amount units at E1, so a design meeting SC-008(a)'s 0.25 dB per 0.25
  step could legitimately change level by more than 1 dB per hop; the bound measures jumps, not slopes. The
  floor stays 1 dB. The per-frame centroid clause is dropped (BH7 needs ≥ 8 periods to resolve harmonics);
  SC-008(a)(b) carry spectral continuity.

## Clarifications

### Session 2026-10-10

Each line: question, the user's decision, and the enforcing ids. Every behavioural clause is carried by the FR/SC named in brackets; the body above is authoritative.

- Q1 CPU budget reading: additive. Excitation + Fold ≤ 1.5 % of one core on top of the core's own 1.0 % (total ≤ 2.5 % per voice), matching the roadmap Phase 9 sum 1.0 + 1.5 + 0.3 + 2.0 = 4.8; the Phase 1 close note's 0.4 % reading was wrong and is superseded; 4× oversampling for both stages is affordable. [FR-054, FR-008, SC-018]
- Q2 What is harmonic compression: adopt the definition and SC-007 thresholds as written; the plan picks the mechanism by measurement against SC-007; the envelope-normalised waveshaper (x / env(x), rescaled) is the expected first candidate; keep the cheapest passing mechanism inside the Q1 budget. [FR-024, FR-035, SC-007, SC-023]
- Q3 Detector scope and timing: the middle (compression) and folding regimes are level-normalised; soft saturation stays static and level-dependent; release is period-aware (at least N f0 periods, N fixed in the plan) so E0 is never gain-modulated within a cycle; attack is a documented constant. [FR-023, FR-025, FR-036, FR-037, SC-024, SC-025]
- Q4 Fold fundamental preservation: h1 keeps polarity and phase within ±5° all the way to amount 100 with no flip; the magnitude bound in the folding regime is relaxed to ≥ −12 dB (was −6 dB); the Anchor exists so the Fold can thin h1. [FR-032, SC-012(d)]
- Q5 Latency: zero base-rate latency for both stages, IIR Economy oversampling and/or ADAA only, no FIR linear-phase modes; the Phase 3/9 Anchor join stays sample-aligned. [FR-008, FR-021, FR-027, SC-026]
- Q6 Excitation level law: static makeup holds output RMS within ±1.5 dB of input across drive and asymmetry at mix 1; drive is not a volume knob. [FR-011, SC-020]
- Q7 Edge drive law: drive gain is dB-linear in Edge with a 12 dB range and always on (Edge 0 still excites at mix above 0); mix remains an independent blend; the range is revisited only with a recorded measurement. [FR-004, FR-055, SC-002(a)(c)]
- Q8 Aliasing method: the gate is Phase 1's BH7 total off-grid power on real multi-harmonic inputs through the whole chain, FFT lengthened so every alias sits ≥ 16 bins from the harmonic grid (test fails if it cannot), plus a positive control; the deviation from the roadmap's named method is recorded. [FR-053, SC-005, D-6]
- D-1 Core heap rule: relaxed to the project rule (no audio-thread allocation, locks, exceptions or IO; prepare-time allocation allowed); the excitation reuses the existing Oversampler; the process callback is constructed at prepare, never per block; the allocation detector gates processBlock. [FR-040, FR-041, SC-015]
- D-2 Latency: resolved by Q5. [FR-008, SC-026]
- D-3 Excitation level law: resolved by Q6. [FR-011, SC-020]
- D-10 CPU budget reading: resolved by Q1. [FR-054, SC-018]
- OQ-A Compression definition: resolved by Q2. [FR-024, FR-035, SC-007]
- Bars: kept as drafted (Edge-to-drive 12 dB; regime boundaries 30 and 65; THD +10 dB and H +50 %; ±2° excitation; ±5° and ≥ −12 dB with no flip for the Fold); a bar proven unreachable is re-ruled with evidence in the main loop, never relaxed silently. [FR-055, SC-027, SC-002, SC-007, SC-012]

### Session 2026-10-10 — plan S1 ratification

The user ratified every plan S1 item after reading its measured evidence (model, evidence scripts and C++
benchmarks in this spec folder). The amended wording governs; no alternative listed in S1 is open.

- C-1 — The shared `Oversampler` is unfit (2.23 % per 4× stereo stage, −15.6 dB at 0.55·fs, 3–6.5 samples of h1
  delay). → **Accepted:** new Layer 1 `PolyphaseHalfbandOversampler` with the allpass section inlined (core/ includes
  only); FR-041 and FR-048 amended accordingly; the core stays heap-free, so D-1's relaxation is withdrawn.
- C-2 — An asymmetric transfer rotates h1 on multi-harmonic inputs (physics). → **Accepted:** SC-012(b) ±2° at
  asymmetry 0 and always on the sine, ≤ 20° on the saw at asymmetry > 0; SC-013(b) inter-channel h1 ≤ 2° at
  asymmetry 0 and ≤ 15° at asymmetry 1; SC-022(e) records ∠T₁ at max asymmetry for Phase 3.
- C-3 — SC-024(b)'s teeth claim fails by construction. → **Accepted:** residual clause kept without the teeth claim;
  (b′) `detectorLevel()` in-cycle ripple ≤ 1e-4 at 20/40/80 Hz with an x/env(x) 1 ms-release reference ≥ 0.1.
- C-4 — Detector topology. → **Per-channel detectors ratified** (identical constants, instant attack); FR-033 and
  SC-013(c)'s detector clause re-ruled to "L/R output ratio within ±0.1 dB of the input ratio at amounts
  {47.5, 82.5, 100}, both levels, through the step".
- C-5 — Split bypass (h1 and everything below 500 Hz on a zero-delay linear path). → **Accepted.**
- C-6 — Fixture inputs where the spec was open; positive control on the SC-009 state. → **Accepted.**
- C-7 — Excitation drive range −12…+12 dB. → **Accepted.**
- C-8 — Fold level on non-sine inputs recorded as SC-022(f). → **Accepted.**
- C-9 — SC-002(c) mix blend evaluated between non-zero mixes (0.1…1), Edge 0 vs Edge 1 compared on the same residual;
  as-written figure recorded as SC-022(g). → **Accepted.**
- C-10 — SC-013(c) first clause re-ruled to the three-part form: (i) Fold at mix 0 adds ≤ 2° inter-channel h1;
  (ii) at max excitation (L+R)/2 h1 power within 0.5 dB of the coherent sum; (iii) phase and imbalance recorded as
  SC-022(h). The mid/side Fold alternative was declined. → **Accepted.**
- C-11 — `kFoldIndexMax` 3.0 (2.7 failed SC-006's mid-grid centroid clause). → **Accepted.**

### Session 2026-10-11 — D-1 fold-index law (main-loop ruling, Group 6 gate)

- D-1 (tasks.md "Defects found") — `HarmonicFold_RegimeContinuityStaticSmoke` (SC-008(a)) read the calibration
  sine's centroid step |ΔC| 0.0750 oct at amount 99.75 → 100 (bar 0.05) under plan S4.3's linear fold-index law.
  → **Ruled:** the fold-index law over (65, 100] becomes the ease-out θ(a) = θmax·(1 − (1 − t)^p), t = (a − 65)/35,
  zero slope at 100; θ(100) = `kFoldIndexMax` = 3.0 unchanged (C-11 stands); no bar moves. p is the smallest candidate
  in {1.5, 2, 3} (smoothstep fallback) meeting |ΔC| ≤ 0.04 oct on every 0.25 pair with every other Fold bar intact.
  Measured (`evidence/d1law.js`, plan S4.3): **p = 1.5** (`HarmonicFold::kFoldIndexEaseExponent`) — sine worst
  |ΔC| 0.0319 oct, |ΔL| = |ΔCF| 0.109 dB, min THD step +0.069 dB; mid grid C2 |ΔC| 0.016 oct, centroid rise 0.600 oct;
  p = 2, 3 and smoothstep fail SC-006's +0.05 dB minimum THD step at 100. [FR-022, SC-006, SC-008]

### Session 2026-10-11 — build defects D-1 / D-2 (tasks.md "Defects found")

- D-1 — SC-008(a) |ΔC| ≤ 0.05 oct failed on the calibration sine at amounts 97.5–100 (0.075) with the linear fold-index
  law; three fixer rounds proved no θmax satisfies both this bar and SC-006's mid-grid rise. → **Ease-out law**
  θ(a) = θmax·(1 − (1 − t)^p), t = (a − 65)/35, **p = 1.5** chosen by model measurement (worst sine |ΔC| 0.0319, min THD
  step +0.069 dB; p = 2, 3 and smoothstep fail the +0.05 dB THD-step floor); θ(100) = 3.0 unchanged; no bar moved.
  [SC-006, SC-008(a), C-11]
- D-2 — SC-026's full-band cross-correlation read lag −4 on the mid grid because ratified C-5 delays the band above
  500 Hz by the halfband round trip (≈ 5 samples) while h1 is delayed 0.09 samples. → **SC-026 excitation arm
  re-ruled (re-scoped)** to what FR-008 protects: sine argmax 0; mid grid h1 ≤ 0.5° and low-passed argmax 0; full-band lag and
  high-band group delay recorded as SC-022(i), ≤ 8 base samples. No design change. [SC-026, FR-008, C-5]
- D-3 — SC-026(b)'s 250 Hz-band argmax read −1 at C2 (0 at E1; model sub-sample peak 0.61 / −0.02). The split's
  2nd-order Butterworth crossover is not a brick wall: its linear path LP·(1 − Dos) + Dos delays 131 Hz by 0.37,
  196 Hz by 0.83 and 262 Hz by 1.48 base samples (model `scratch sc026-lowband.js` on `evidence/lib.js ExcStage`), and
  the excitation's generated harmonics set the phase of the weak mid-grid partials (h2 −6.8 dB, h7 +13 dB at C2), so a
  250 Hz band weighs h3/h4 at C2, which the ratified design delays 0.7–0.9 samples. → **SC-026(b) re-ruled: low band = 125 Hz**, the highest
  cutoff where the design promises < 0.5 sample (model peak −0.02 at both notes); the 250 Hz figure is recorded as
  SC-022(i). h1 lock unchanged (0.07° at C2). No design change; the same linear path carries a +1.5 dB hump at
  400–700 Hz at mix 1 (model), a C-5 property no bar covers, noted for Phase 9 listening. [SC-026, FR-008, C-5]
