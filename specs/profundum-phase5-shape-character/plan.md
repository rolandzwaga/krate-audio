# Implementation Plan: Profundum Phase 5 — Shape & Character

**Spec:** `specs/profundum-phase5-shape-character/spec.md` (Clarified 2026-10-15)
**Roadmap:** `specs/Profundum-roadmap.md` Part A, Phase 5 (lines 410–435)
**Phase base:** commit `40205b86` on `synth/profundum`
**Status:** Planned 2026-10-15; revised the same day after the plan review (see "Review notes" at the end).
**Section S1: all twenty-two items RATIFIED 2026-10-15** (C-2, C-3, C-4, C-5, C-6, C-7, C-10 ruled by the user, each the recommended option; the rest accepted by the main loop; every ruling is in its spec FR/SC row and in Clarifications "Plan S1 rulings"). The original note follows. Ten are flagged ⚠ RE-RULE or ⚠ RISK, because the
spec as written cannot be met, because the plan changes a ratified definition, or because a gated bar is estimated over
budget:
- **C-2** — SC-012's 3 dB ambiguity rule cannot be met. Under the FR-035 estimator the octave-up side of a
  full-wave-rectified half-wave-symmetric input never gets past +2.1 dB (|sin|) or +1.9 dB (|triangle|). Every cell
  above the flip is therefore ambiguous, for any Amount law.
- **C-3** — the reference estimator puts kGrowl's own pitch at 2·f₀, before any stage acts.
- **C-4** — SC-026(a)'s "gap equals the band width within 0.01" cannot hold for sine and triangle at once.
- **C-5** — the Fracture flip point depends on the input spectrum. No input-agnostic observer can match every fixture,
  and the bias-flip clip alone fails SC-010.
- **C-6** — SC-002's ±2.5 dB on kGrowl conflicts with any low-pass-like ROUND under a static table.
- **C-7** — SC-006's 0.25-octave Tone span is unreachable on kSineAnchor for four algorithms.
- **C-9** — the Fracture SC-010 lattice.
- **C-10** — Tube's drive ceiling. SC-010 and SC-007 conflict at 4×.
- **C-19** — FR-021 says "Rectify composes `Rectifier`"; the stage composes `Rectifier`'s laws, not a running
  `Rectifier`. The FR-021 wording must change.
- **C-20** — SC-015's gated cell must run unlinked (real stereo or nonzero offsets). Scaled by Phase 2's measured
  stereo factor, the estimate is ≈ 2.1–2.8 %, **over** the 2.0 % bar until T001 measures it.

The other twelve items are refinements or records that change no bar.

Every number in this plan was **measured this session** by one tool:

| Artifact | What it is | Reproduce |
|---|---|---|
| `shape-character-model.js` | A double-precision model of the S3–S6 design, built on Phase 2's `evidence/lib.js`: the `PolyphaseHalfbandOversampler` model, the three 2fs notches, `DCBlocker`, the `aliasedPower` mask, the recipe fixtures and the Fold-100 render. It contains a faithful port of the SC-008(r) SHS estimator, static descriptor and separation code using the spec's OQ-S floor, and a ShapeMorph static model | `node specs/profundum-phase5-shape-character/shape-character-model.js shs \| salience \| fracture \| fracvar \| fracflip \| descr \| flip \| shape`; aliasing: `… alias <note> <rate>` |

The model is a feasibility and constants tool; the C++ tests remain the authority. The CPU figures in S10.7 are
**estimates**. Task T001 is a microbenchmark that must run before any header is written (S12 R-1).

---

## S0. Verification ledger (every fact below was read in this session)

Paths are relative to `dsp/include/krate/dsp/` unless stated.

| Fact | Source |
|---|---|
| Layer rule: each layer may include only layers **below** it (L1 includes core only, L2 includes core and primitives, L3 includes core, primitives and processors) | `dsp/CLAUDE.md:8–16` |
| `PolyphaseHalfbandOversampler`: `kMaxFactor = 4`; `setFactor(2\|4)` resets; `upsample(float, float*)` writes `factor()` samples; `downsample(const float*, AtTwoX&&)` applies the hook to each 2fs sample before the outer decimation (for F = 2 that rate is the oversampled rate itself); `resetDownsampler()`; `stateFinite()`. It is heap-free, copyable, IIR, with ≈ 1.9 base samples of group delay per direction | `primitives/polyphase_halfband_oversampler.h:24–34, 49, 64, 70, 84–97, 105` |
| `HarmonicFold`: factor 4× up to 100 kHz and 2× above; three 2fs notches `kNotchRatios{0.50, 0.52, 0.55}`, `kNotchQs{1.5, 2.0, 1.5}`; DC corner 2 Hz; mono link (channel 1 copies channel 0's state at the first differing sanitised sample; it never re-links during processing, only `reset()` sets `linked_ = true` again); sanitise maps non-finite to 0; output `std::clamp` at `kOutputCeiling` 6.0; `preCeilingPeak()` observer; `latencySamples() == 0`. **Access:** `kNotchRatios` and `kNotchQs` are **private** (they follow `private:` at line 297 and the header declares no `friend`), so no test can reference them; the public constants used here are `kCalibrationPeak` (83), `kDcCornerHz` (91) and `kOutputCeiling` (93) | `processors/harmonic_fold.h:83, 91, 93, 126, 130–140, 203–208, 239, 293, 271, 297–301, 382, 438–440` |
| `DCBlocker`: R = exp(−2π·fc/fs), clamped to [0.9, 0.9999], cutoff clamped to [1, fs/4]; `process` flushes denormals | `primitives/dc_blocker.h:190–205, 229, 236` |
| `Biquad::configure(FilterType, float freq, float Q, float gainDb, float sampleRate) noexcept`; `process` resets and returns 0 on a non-finite input (bit test), TDF2, flushes denormals; `getZ1/getZ2` | `primitives/biquad.h:330–337, 352–368, 391` |
| `SVF`: `kMaxQ = 30`, `kMaxCutoffRatio = 0.495`; `setCutoff` computes `tan(π·fc/fs)` once per call; `process` resets on NaN/Inf; `processMulti` returns `SVFOutputs`, whose members are declared in the order **`low, high, band, notch`** (band = k·v1, notch = low + high), so `low + band + high = input`. Plan code reads the members **by name only** (`.band`, `.notch`), never through positional aggregate initialisation | `primitives/svf.h:60–65, 123, 129, 204–212, 353–362, 421–460, 480–482` |
| SVF `HighShelf` mix is m0 = A², m1 = k(A² + A − 1), m2 = 1 (author's own derivation note in the header) — **not used** (S6.1) | `primitives/svf.h:628–650` |
| `OnePoleLP`: a = exp(−2π·fc/fs); `process` resets on NaN/Inf and flushes denormals; cutoff clamped below 0.495·fs | `primitives/one_pole.h:56, 98–115, 135, 139–141` |
| `TanhADAA`: ADAA1 of tanh(drive·x), F1 = ln cosh with an asymptote for \|x\| ≥ 20; `kEpsilon = 1e-5f` | `primitives/tanh_adaa.h:42–43, 66, 150–153, 165` |
| `HardClipADAA`: Order::First / Second; `kEpsilon = 1e-5f` (precedent for C-11) | `primitives/hard_clip_adaa.h:81–84, 210` |
| `Asymmetric::tube(x)` = fastTanh(l + 0.3 l² − 0.15 l³), l = 2·fastTanh(x/2); `Asymmetric::diode(x)` = 1 − e^{−1.5x} (x ≥ 0), x/(1 − 0.5x) (x < 0) | `core/sigmoid.h:333–342, 358–366` |
| `Waveshaper::process` = applyShape(drive·x + asymmetry); Tube → `Asymmetric::tube` | `primitives/waveshaper.h:205–216, 261–262` |
| `TubeStage`: waveshaper Tube; DC blocker 10 Hz in the wet path; 5 ms smoothers; `setInputGain` / `setOutputGain` clamp ±24 dB; per-sample loop; a sample with smoothed saturation < 0.0001 is left untouched | `processors/tube_stage.h:95, 117–145, 171–211, 255–293, 270` |
| `WavefolderProcessor`: `kMinFoldAmount = 0.1`, `kMaxFoldAmount = 10`; DC blocker 10 Hz in the wet path; 5 ms smoothers; Serge = `WavefoldType::Sine` = `sin(gain·x)`; per-sample loop; mix < 0.0001 leaves the sample untouched | `processors/wavefolder_processor.h:122–131, 260–263, 325–378, 345, 387–391`; `core/wavefold_math.h:366–381` |
| `DiodeClipper` uses `DCBlocker2` (2nd-order) at 10 Hz in the wet path, plus five 5 ms smoothers | `processors/diode_clipper.h:147, 392, 432` |
| `SpectralTilt`: non-copyable; setters not `noexcept` but only clamp and set smoother targets; while either smoother runs, `process` recomputes **two biquad designs per sample** | `processors/spectral_tilt.h:164, 215–228, 253–268, 349–380` |
| `BitCrusher`: `levels_ = pow(2, bits) − 1` (fractional bits allowed); symmetric round-and-clamp quantiser; TPDF dither only if `dither_ > 0`; `setSeed`, and `reset()` restores the seed | `primitives/bit_crusher.h:72–81, 91–130, 151–159, 178–187` |
| `polyBlep4` is the integrated cubic B-spline residual: (3u⁴ − 8u³ + 16u)/24 and (2 − u)⁴/24 pieces | `core/polyblep.h:105–160` |
| `equalPowerGains(position, out, in)`; `crossfadeIncrement(ms, fs)` | `core/crossfade_utils.h:50, 89` |
| `OnePoleSmoother` (snaps within `kCompletionThreshold` = 1e-4); `LinearRamp` | `primitives/smoother.h:55, 134, 197–211, 305` |
| `detail::isFinite(float)` and `detail::isFinite(double)`: bit tests behind an opaque-bits barrier, fast-math safe. The comment at 115–116 says to use them instead of local bit-mask clones, because "a plain bit-pattern test is exactly what newer fast-math compilers fold away" | `core/db_utils.h:115–128` |
| Core headers include core headers: `drop_curve.h:29`, `fast_math.h:22` and `filter_design.h:16` include `core/db_utils.h`; `sigmoid.h:22–24` includes `fast_math.h`, `db_utils.h` and `math_constants.h` | as cited |
| Sizes of the reused members, measured this session (`clang++ -std=c++20` over the real headers, `sizeof`): `TubeStage` 128 B, `WavefolderProcessor` 176, `PolyphaseHalfbandOversampler` 296, `SVF` 104, `OnePoleLP` 32, `DCBlocker` 32, `Biquad` 28, `BitCrusher` 20, `OnePoleSmoother` 20, `TanhADAA` 12 | this session |
| Setter costs: `TubeStage::setInputGain` / `setOutputGain` call `dbToGain` (`tube_stage.h:171–184`); a `BitCrusher` bit-depth change computes `levels_ = std::pow(2, bits) − 1` (`bit_crusher.h:182–183`); `SVF::setCutoff` evaluates one `tan` (`svf.h:204–212`) | as cited |
| Phase 2 CPU record: stage cost 1.019 % mono-linked; chain 1.708 % linked vs 2.462 % stereo-forced (record only) | `specs/profundum-phase2-edge-fold/compliance.md:7` |
| `ProfundumCore`: `kControlInterval = 32`, `kPitchUpdateInterval = 16`, `kMinF0Hz = 8`; `processBlock(L, R, n, const float* f0PerSample = nullptr)` samples the pointer at every 16-sample grid phase | `systems/profundum_core.h:97–99, 237–275` |
| `AnchorOscillator`: `kMinFollowRatio = 0.25`, `kMaxFollowRatio = 4`; `setFollowRatio` clamps and ignores non-finite values | `systems/anchor_oscillator.h:87, 255–258` |
| The SC-008(r) estimator `anchorShsF0` works as follows: a Hann frame of `kLowFrequencyFftSize` = 262144; S(f) = Σ_{h=1}^{15} 0.84^{h−1}·M(h·f) on the linearly interpolated magnitude; a 0.05-cent grid over [f₀/2.2, 2.2·f₀]; stage-2 golden-section refinement on the exact DTFT | `dsp/tests/unit/systems/anchor_oscillator_test_helpers.h:213–290` |
| Test helpers: `ProfundumTest::renderedHarmonicPowers`, `aliasFftLength`, `aliasedPower` (mask ±8 bins around n·f₀ and bins 0–8), `rmsDbfs`, `goertzelPhase`; `HarmonicFoldTest::recipeAdditive` (core-equivalent fixture without a core), `calibrationSine`, `integerPeriodLength`, `goertzelPhasor`, `renderFold` | `dsp/tests/unit/systems/profundum_core_test_helpers.h:47, 157, 235–306, 453`; `dsp/tests/unit/processors/harmonic_fold_test_helpers.h:40, 224–297, 362–379` |
| Probe precedent: a `detail::<Name>Probe` struct is forward-declared and befriended in the header, and defined only in the test TU | `systems/bloom_engine.h:160–162, 1666`; `systems/ecosystem_engine.h:98–110, 2510–2511` |
| No `fastExp` / `fastLog` exist (they were removed; MSVC std is faster) | `core/fast_math.h:13` |
| Test registration: enumerated lists for `dsp_processors_tests` and `dsp_systems_tests`; the `-fno-fast-math` property block lists every Profundum nonfinite TU | `dsp/tests/CMakeLists.txt:161–276, 348–690, 1145–1168` |
| ODR sweep, run this session (`grep -rnE "(class\|struct\|enum class\|enum\|using) <Name>\b" dsp/ plugins/ tests/ tools/`): `OversampledResidualPath`, `KeytrackedTilt`, `ClosedFormAdaa2`, `Adaa2`, `Adaa2State`, `BassCharacterStageProbe`, `ShapeMorphProbe`, `RectifierProbe`, `PitchSyncCrush`, `FracturePhasor`, `CharacterTilt` — **0 hits each**. The spec's own sweep (spec lines 197–203) covers `Rectifier`, `ShapeMorph` and `BassCharacterStage` | this session |

---

## S1. Spec conflicts and refinements — RATIFIED 2026-10-15

**Status:** every item below is ratified as its recommended / proposed option (user rulings C-2, C-3, C-4, C-5, C-6, C-7, C-10; main-loop acceptance of the refinements). The spec rows named in each item carry the ruling; this section is the record of the evidence.

### C-1 (refinement, new headers): three helper headers beside the three spec components

The spec names three new components, and FR-064 forbids touching existing headers. A shared structure is still needed
by all three, and the strict layer rule (`dsp/CLAUDE.md:8`) decides where it can live:

| New header | Layer | Class(es) | Why it exists | Includes |
|---|---|---|---|---|
| `core/closed_form_adaa2.h` | 0 | `template <class Law> class ClosedFormAdaa2`, `struct DiodeAdaaLaw`, `diodeH1Coefficient(double)` | Second-order ADAA with closed-form antiderivatives in double (C-11), used by Rectifier, BITE and Bite; plus the one prepare-time c_bite builder that both L3 stages call (S4.1) | `core/db_utils.h` (for `detail::isFinite(double)`), stdlib |
| `processors/keytracked_tilt.h` | 2 | `KeytrackedTilt` | The first-order keytracked tilt used by every Tone and by BITE's emphasis (S6.1). Composes `OnePoleLP` | `primitives/one_pole.h` |
| `processors/oversampled_residual_path.h` | 2 | `OversampledResidualPath` | The 4× round trip with the notched, DC-blocked residual (S3). Used by both L3 stages | `primitives/polyphase_halfband_oversampler.h`, `primitives/biquad.h`, `primitives/dc_blocker.h` |

- `Rectifier` is L2, so it cannot include the L2 path. It composes the same L1 parts itself (≈ 30 lines; S4.3).
  `rectifier_test.cpp` asserts that its notch, DC and factor constants equal `OversampledResidualPath`'s **public**
  constants, so the two copies cannot drift.
- **`HarmonicFold`'s notch constants are private** (`harmonic_fold.h:297–301`, no `friend`), and FR-064 forbids
  exposing them, so no test compares against them. `oversampled_residual_path_test.cpp` asserts the values literally
  ({0.50, 0.52, 0.55} and {1.5, 2.0, 1.5}), with a comment naming `harmonic_fold.h:300–301` as their origin. The only
  `HarmonicFold` equality asserted is the public `kDcCornerHz` (`harmonic_fold.h:91`) against `kResidualDcHz`.
- **Core may include core** (`drop_curve.h:29`, `fast_math.h:22` and `filter_design.h:16` all include
  `core/db_utils.h`; S0). `core/closed_form_adaa2.h` includes `core/db_utils.h` and implements `stateFinite()` with
  `detail::isFinite(double)` (`db_utils.h:124–128`). It never uses `std::isfinite` (broken under `-ffast-math`) or a
  local bit-mask clone (which `db_utils.h:115–116` documents as folded away by fast-math compilers).
- `DiodeAdaaLaw::f` is the law in double, because `Asymmetric::diode` (`sigmoid.h:358–366`) is float and ADAA2 must run
  in double (C-11). Including `sigmoid.h` would be legal but yields only the float law, so the double copy stays and
  `closed_form_adaa2_test.cpp` asserts that `static_cast<float>(DiodeAdaaLaw::f(x))` equals `Asymmetric::diode(x)`
  within 2 ulp over a 10001-point grid in [−8, 8].
- The repo's existing lateral L1 includes (for example `ladder_filter.h` → `oversampler.h`) are not copied.

### C-2 ⚠ RE-RULE (SC-012 ambiguity rule; FR-036 band): 3 dB → 1 dB, with "candidate" defined

**Finding** (`shape-character-model.js shs`, `salience`, faithful SHS port):

| Signal | S(2f₀)/S(f₀) | S(f₀/2)/S(f₀) |
|---|---|---|
| \|sin\| analytic ceiling | +2.09 dB | — |
| \|triangle\| analytic ceiling | +1.86 dB | — |
| kSineAnchor full-wave, E1 / C3 (faithful estimator) | +2.06 / +1.94 dB | — |
| kTriangleAnchor full-wave, E1 / C3 | +2.50 / +2.25 dB | — |
| kSineAnchor raw (any note) | −76.9 dB | **−1.51 dB** |

- The 15-term, 0.84-weighted SHS credits the even harmonics of a rectified half-wave-symmetric input at f₀ with the
  weights 0.84^{2k−1}. Its preference for 2·f₀ therefore tops out near +2 dB.
- At 3 dB no cell above the flip is ever unambiguous, whatever the Amount law. That makes SC-012(b) ("unambiguous cells
  must exist on both sides") and SC-026's band (its upper edge is never reached) unreachable.
- Read literally, the 3 dB rule also marks **every sine cell** ambiguous. The subharmonic f/2 always scores
  S(f/2) = Σ_k 0.84^{2k−1}·A(kf) ≤ 0.84·S(f), which is −1.51 dB on a pure tone.

**Proposed rule.**
- The candidates are f̂ and the octave neighbours {f̂/2, 2·f̂}.
- A cell is ambiguous when the best and second-best candidate saliences differ by less than **1 dB**.
- The subharmonic echo is always ≤ −1.51 dB, so it never makes a cell ambiguous. Real octave content can.
- Under this rule the Rectify flip has finite bands with unambiguous cells on both sides (S10.5). The ambiguity caps
  of SC-012(a), (d) and (e) apply unchanged.

**Alternative:** keep 3 dB and re-word SC-012(b) and SC-026 around the argmax change (0 dB crossing) with a
plan-ruled hysteresis width. This is not recommended, because it leaves the observer's hysteresis tied to nothing
measured.

### C-3 ⚠ RE-RULE (SC-012, SC-024 input reference): gate only cells whose input reads f₀

**Finding** (faithful estimator):
- kGrowl raw reads **2·f₀** at +3.43 dB (E1) and +4.07 dB (C3).
- Fold-100 kGrowl reads 2·f₀ at +1.49 / +1.61 dB. (Phase 3 SC-008(r)'s folded growl had maximum excitation as well.)
- Saw, sine and triangle read f₀, raw and Fold-100.
- A stage with r ≡ 1 therefore fails SC-012(a), (d) and (f) on kGrowl through the fixture alone.

**Proposed rule, clauses (a), (d), (e) and SC-024(a).** A cell is gated only if the stage **input** reads f₀
unambiguously (C-2 rule). Other cells record f̂_in, f̂_out and r_obs.
- The flags are computed per cell by the same estimator.
- Comply reports which cells were recorded and not gated. Expected: every kGrowl cell, raw and Fold-100.
- The core's own kGrowl octave reading is recorded for Phase 9's FOLLOW row.

**Proposed rule, clause (f) (FR-034 r ≡ 1 evidence): every cell is gated, kGrowl included, on the ratio.**
- For Tube, Fold, Bite and every Shape position, FR-034 asks whether the stage **moved** the perceived pitch, so the
  reference is r_ref = f̂_out / f̂_in.
- Every cell of the SC-012(f) grid, on all four fixtures raw and Fold-100, must give r_ref = 1 within 1 cent, with both
  readings unambiguous (clause (f) admits no ambiguous cell).
- This catches, for example, HOLLOW's 3·f₀ notch moving kGrowl's 2·f₀ reading, which input-gating would never see.
- A failing cell triggers the FR-034 contingency (S6.5 for ShapeMorph; the same route for Tube, Fold and Bite).
- **Re-ruled at close (user, 2026-10-16, C-29):** the first full grid showed every nonlinear stage and most SHAPE
  positions turning kGrowl's 2·f₀ reading (margin 1.8–2.6 dB) into f₀; the input gate now applies to (f) as well,
  the growl cells are recorded, and no observer is added.

**Alternative for (a), (d) and (e):** r_ref = f̂_out / f̂_in there too. This is rejected for those clauses because it
fails Rectify on kGrowl at Amount 1, where f̂ goes from 2·f₀ to f₀, so r_ref = 0.5 while the default observer reports 1.
That objection applies only to stages meant to move the pitch, not to clause (f).

### C-4 ⚠ RE-RULE (SC-026(a)): the hysteresis is the intersection band, not each cell's band

At the 1 dB rule the measured bands (S10.5, E0–C3, keytracked) are:

| Tone | kSineAnchor band | kTriangleAnchor band | Intersection |
|---|---|---|---|
| 0 | [0.821, 0.912] | [0.837, 0.913] | [0.837, 0.912] |
| 0.5 | [0.777, 0.877] | [0.787, 0.869] | [0.787, 0.869] |
| 1 | [0.705, 0.829] | [0.723, 0.822] | [0.723, 0.822] |

At Tone 0.5 the widths are 0.100 and 0.082. One hysteresis cannot equal both within 0.01.

**Proposed SC-026(a).** The observer's up-flip and down-flip Amounts are the intersection band's edges,
[max A_lo, min A_hi] over the half-wave-symmetric fixtures × notes, per Tone. Each edge lies inside **every** cell's
band, and the gap equals the intersection width within 0.01. SC-012(b)'s "the observer's step lies within the band"
then holds by construction.

### C-5 ⚠ RE-RULE (FR-050 Fracture definition; FR-033, SC-012(d), SC-024(a) observer)

**Finding 1 — the ratified bias-flip clip alone fails.** With y = tanh(g(x/P + βs)) − tanh(gβs), s = sin(πt):
- at Amount 1, kGrowl reaches only S(f₀/2)/S(f₀) = −2.2 dB (no flip);
- the flips spread from ≈ 0.27 (sine) to ≈ 0.65 (saw);
- on the f₀/2 lattice it aliases at **−69.5 dBFS** (Fold-100 kGrowl, E2, 44.1 kHz, no ADAA).

**Finding 2 — the recommended law.** Polarity alternation, then the ratified bias-flip clip:

  v = g·(x·((1 − a) + a·σ(φ))/P + β·sin(πφ)),  y = (P/g)·(TanhADAA(v) − tanh(g·β·sin(πφ))),
  σ(φ) = tanh(4·sin(πφ))/tanh(4),  a = A,  g = 0.5 + 1.5·A,  β = 0.5·A

- φ ∈ [0, 2) is the ÷2 phasor in periods of f₀.
- It is alias-free: worst −97.9 dBFS on the f₀/2 lattice, which equals the Fold-100 input's own residual (S10.3).
- At Amount 1 all four fixtures reach r = 0.5 **unambiguously**: all power sits on the odd-f₀/2 lattice (0.0 dB re
  total).
- It keeps the ratified asymmetric bias flip as the "fold / clip" character.

**Finding 3 — no input-agnostic observer can match every fixture.** The flip Amount (salience 0 dB crossing) depends on
the input spectrum:

| Input | Flip Amount |
|---|---|
| Raw sine / triangle / saw / growl | 0.13 / 0.17 / 0.38 / 0.64 |
| Fold-100 saw | 0.270, band [0.179, 0.342] |
| Fold-100 sine | 0.383, band [0.312, 0.449] |
| Fold-100 triangle | 0.397, band [0.326, 0.462] |
| Fold-100 growl (reads 2·f₀; excluded by C-3) | 0.528 |

No period-doubling law tested co-locates the flips: bias flip, period gating, gating plus bias, polarity alternation,
and the combined law (`fracvar`, `fracflip`).

**Proposed ruling.**
- (a) Adopt the combined law as the FR-050 definition.
- (b) The observer reports r for **Character's realistic input class: the Fold output** (spec descriptor definitions,
  "Character's realistic worst case"). It uses a hysteresis like Rectify's, with edges equal to the intersection band
  of the Fold-100 fixtures that read f₀: saw, sine and triangle, giving **[0.326, 0.342]** at E1 / Tone 0.5. Final
  edges per Tone and note come from the T030 harvest; if an intersection is empty, this item returns to the user.
- (c) SC-012(d) and SC-024(a) gate the match on Fold-100 saw, sine and triangle. Raw-fixture cells are recorded with
  their deviation, the same pattern as Rectify's default-descriptor cost.
- **Re-ruled at close (user, 2026-10-16, C-31):** the harvest's three-way intersection is empty at Tone 0.75 and 1 (the
  saw flips first); edges = Fold-100 sine ∩ triangle per Tone, the saw recorded in (d) and SC-024(a).
- SC-024(a)'s "non-increasing, below 1 at Amount 1, ≥ 100 cents" holds: the move is 1200 cents.
- SC-024(b) holds: 0.0 dB at Amount 1, none at Amount 0, which is the hard bypass.

**Alternatives.**
- (ii) A voice-supplied input descriptor. The flip correlates with the input's SHS margin
  20·log₁₀(S(f₀)/S(f₀/2)) only with ±0.1 Amount scatter (S10.2), which is too loose for the 1 dB bands.
- (iii) Keep the ratified bias-flip law. It fails SC-010 and never flips kGrowl.

### C-6 ⚠ RE-RULE (SC-002 equal-loudness clause, FR-056): gate kSawAnchor, kTriangleAnchor and kSineAnchor; record kGrowl

**Finding** (`shape` model, E1):
- kGrowl is presence-dominant. A keytracked low-pass that is audible on the saw removes most of its power. Raw level at
  SHAPE 0 relative to input:

  | ROUND cutoff | kGrowl | Saw / triangle / sine |
  |---|---|---|
  | 10·f₀ | −15.7 dB | −1.1 to −1.6 dB |
  | 24·f₀ | −10.9 dB | |
  | 32·f₀ (no audible low-pass left) | −5.0 dB | |

- A static table cannot follow the signal (FR-015, a ratified ruling), so it cannot hold growl within ±2.5 dB and keep
  ROUND low-pass-like.
- With the table calibrated on the mean of saw, triangle and sine, those three hold −1.64…+1.07 dB (bar ±2.5).
  kGrowl swings −11.8…+4.0 dB.

**Proposed ruling.** The table is calibrated on the mean of kSawAnchor, kTriangleAnchor and kSineAnchor. SC-002's
±2.5 dB gates those three, and kGrowl's swing is recorded for Phase 6 (Mass) and Phase 9 listening.

**Alternative:** a voice-supplied static presence descriptor (recipe E_high) selecting a 2-D table (SHAPE × presence).
It is not a level follower, but it adds an API and a calibration dimension.

### C-7 ⚠ RE-RULE (SC-006 span on kSineAnchor): gate the span on kSawAnchor; monotonicity on both

**Finding** (`descr`).
- Each of these outputs is h1-dominated on a sine at Amount 0.5, so no first-order tilt moves the log-frequency
  centroid by 0.25 octave:

  | Algorithm | Span C(Tone 1) − C(Tone 0) on kSineAnchor, worst Amount ∈ {0.5, 1} |
  |---|---|
  | Tube | 0.00–0.02 octave |
  | Fold | 0.03 octave |
  | Rectify | 0.08 octave |
  | Bite | 0.02–0.11 octave |

- These spans hold for every setting tested: pre-tilt pivot 0.5–2·f₀ at ±6–12 dB; post-tilt pivot 2–4·f₀ at ±6–12 dB.
- On kSawAnchor the spans are 0.45–1.17 octave (S10.4), and the coupled tilts of C-8 keep both fixtures monotone.
- Crush (0.25) and Fracture (0.47) pass on the sine as well.

**Proposed ruling.** SC-006's monotonicity (ΔC ≥ −τ, cumulative reversal ≥ −τ) gates both fixtures. The ≥ 0.25-octave
span gates kSawAnchor; the kSineAnchor span is recorded.

**Alternative:** second-order ±18 dB tilts that force the sine span. Tone's extremes then become filter sweeps (h1
−15 dB at Tone 1), and the aliasing margins of S10.3 shrink.

### C-8 (refinement, FR-055 Tube / Bite): the pre-clip tilt plus a coupled post tilt in the same direction

- A pre-clip tilt alone is never monotone on both fixtures for Bite (`descr` sweep):
  - pivot ≤ f₀: saw steps of −0.008 to −0.022 octave;
  - pivot ≥ 2·f₀: kSineAnchor reverses to −0.05…−0.10 octave.
- **Ruling proposed:** Tube and Bite Tone = pre-clip `KeytrackedTilt` (k = 1, ±6 dB) **and** a post `KeytrackedTilt`
  (k = 2, ±6 dB), both driven by the same Tone. The saw is then monotone, with spans 1.03 (Tube) and 0.86 (Bite).
- **SC-028(d) must de-tilt.** A ±6 dB post tilt reweights h2…h16 against h1 and can move THD by several dB with no
  pre-clip tilt at all, so a raw Tone 0 vs Tone 1 comparison could pass with the pre tilt missing or mis-wired. The test
  therefore divides each output's w_n by the post tilt's measured linear response |H_post(n·f₀)|² at that Tone
  (measured on the probe's post-tilt object, as SC-028(c) does for Fold) and computes THD / R_oe on the de-tilted w_n;
  the ≥ 3 dB bar applies to that. A corroborating arm holds the post tilt at 0.5 through `BassCharacterStageProbe` and
  requires the same ≥ 3 dB. **Negative control:** with the pre tilt forced neutral through the probe and the post tilt
  active, the de-tilted Tone 0 vs Tone 1 difference must be < 3 dB, so the gate cannot pass on the post tilt alone
  (S9.3).
- FR-055's other rows stand: Fold, Rectify, Crush and Fracture use a post `KeytrackedTilt` (k = 2, ±12 dB).

### C-9 ⚠ RE-RULE (SC-010 Fracture row; FR-070 pre-authorised by the spec): measure on the n·f₀/2 lattice

The Fracture sidebands at odd multiples of f₀/2 are the definition, so the n·f₀ mask reads −17 to −20 dBFS by
construction (S10.3). SC-010 already anticipates this re-rule.

**Ruling proposed:**
- Fracture's SC-010 gate is A ≤ −80 dBFS with the mask on the n·f₀/2 lattice (`aliasedPower(x, aliasFftLength(fs, f₀/2), fs, f₀/2)`).
- The n·f₀-mask value is recorded.

### C-10 ⚠ RE-RULE CHOICE (SC-010 × SC-007 for Tube): a branch-local extra 2× (8× total)

`TubeStage` has no closed-form ADAA path. The tube law is tanh∘poly∘tanh and has no elementary antiderivative.

| Tube max input gain | 4× worst A | SC-007 on kSawAnchor |
|---|---|---|
| +24 dB | −40.7 dBFS | — |
| +12 dB | −66.6 dBFS | — |
| +6 dB | passes | **fails**: THD moves 1.62 dB and C 0.17 octave, against 3 dB / 0.25 octave |

With a branch-local `PolyphaseHalfbandOversampler` at factor 2 around `TubeStage` (8× at fs ≤ 100 kHz):
- +12 dB reads −85.3 dBFS worst (S10.3, E2 / 44.1 kHz, with the chosen C-8 Tone design);
- SC-007 moves 4.89 dB / 0.40 octave.

+18 dB reads −70.9 dBFS. **Ruling proposed:** Tube at 8×, input gain −6…+12 dB. The cost is recorded in S10.7. The
alternative, replacing `TubeStage` by an ADAA-able curve, changes FR-021.

### C-11 (refinement): ADAA2 in double for Rectify, BITE and Bite, with ε = 1e-5 in argument units

E2 / 44.1 kHz, worst over saw / growl × raw / Fold-100 × Tone:

| Mechanism | Rectify | Bite |
|---|---|---|
| 4× halfband only | −31 dBFS | −40 dBFS |
| + residual notches (S3) | −43 dBFS | −45 dBFS |
| + ADAA1 | −75.7 dBFS | −73.8 dBFS |
| + ADAA2 | **−86.5 dBFS** | **−83.3 dBFS** |

- ADAA2 is computed in double, because float cancellation in the F2 difference quotients is the known ADAA2 failure.
- ε = 1e-7 left E0 / 96 kHz Bite at −77.0 dBFS; ε = 1e-5 reads −114.5 there. This matches `TanhADAA` and
  `HardClipADAA` (`kEpsilon = 1e-5f`).
- 2× instead of 4× reads −44 to −61 dBFS and is not viable.

### C-12 (refinement): Fold θ_max = 2.5 rad at the calibration peak

`WavefolderProcessor` has no ADAA path. At 4× with residual notches:

| θ_max | Worst A (E2 / 44.1 kHz) |
|---|---|
| 3.0 | −79.93 dBFS (fails by 0.07 dB) |
| 2.5 | −85.2 to −85.7 dBFS |

### C-13 (refinement): reused-processor choices the spec left open

Rows 1–4 are permitted by the spec's "or" clauses. Row 5 is not: FR-021 says "Rectify composes `Rectifier`" with no
"or" clause, so row 5 is promoted to **C-19 ⚠ RE-RULE**.

| Spec option | Chosen | Not chosen, because (cited) |
|---|---|---|
| ROUND: `LadderFilter` or `SVF` LP | `SVF` LP + `TanhADAA` drive | `LadderFilter::prepare` allocates and its `processBlock` goes through `Oversampler`'s `std::function` callback (spec existing-components row); its own oversampler would nest inside S3's |
| BITE / Bite emphasis: `SpectralTilt` or `SVF` shelf | `KeytrackedTilt` (`OnePoleLP` split) | `SpectralTilt` redesigns two biquads per sample while its smoothers run (`spectral_tilt.h:253–268`), which happens on every f₀ step during a Drop at the 4× rate. `SVF::HighShelf`'s mix (`svf.h:628–650`) is not the Cytomic shelf. A first-order split is exactly unity at Tone 0.5 |
| Bite clip: `DiodeClipper` or `Asymmetric::*` | `Asymmetric::diode` law via `ClosedFormAdaa2<DiodeAdaaLaw>` | `DiodeClipper` has a 10 Hz **2nd-order** DC blocker in its wet path (`diode_clipper.h:392`) and no ADAA |
| Crush | `BitCrusher` + inline pitch-synchronous hold (FR-051) | `BitcrusherProcessor` / `SampleRateReducer` (FR-051) |
| Rectify in the stage (C-19) | `Rectifier`'s laws, makeup and linear coefficient (S4.2) inside the stage's oversampled domain | Running `Rectifier::processBlock` inside the stage would nest a second 4× round trip (16×), double the delay, and break FR-053's phase-matched dry share |

### C-14 (refinement, SC-027 wording): "the oversampling round trip" is the stage's linear path

- With the wet law set to identity through the probe, the output must equal the input passed through
  `OversampledResidualPath`'s linear decimation alone.
- That reference includes the one-oversampled-sample ADAA2 alignment delay (S4.3), so both sides carry it.
- The raw-dry positive control mixes the base-rate input instead.

### C-15 (record): h1 group delay of each stage

- `OversampledResidualPath` contributes ≈ 3.8 base samples (1.9 per direction).
- The ADAA2 alignment adds 0.25 base samples at 4×.
- Tube's branch 2× adds ≈ 0.47.
- `WavefolderProcessor`'s 10 Hz wet DC blocker rotates h1 by +26.6° at E0. Its analytic one-pole response is
  −0.97 dB / +25.9°; measured values go into FR-040.
- FR-040 records all of these. Phase 9's coherent-sum bound consumes them.

### C-16 (refinement): crossfade laws and lengths

| Transition | Law | Length |
|---|---|---|
| Algorithm switch (D-7) | g_out = cos(πt/2)^α, g_in = sin(πt/2)^α, α = 1.493 (−4.5 dB midpoint) | 10 ms |
| Amount-0 bypass edge (FR-054) | Linear | **5 ms** |
| Shape engage (FR-019) | Linear | **10 ms** |
| SHAPE regimes | Linear weights; the loudness table absorbs the coherence | — |

- The algorithm-switch law bounds the coherent sum at +1.5 dB and the incoherent sum at −1.5 dB at the midpoint. Equal
  power would give +3 dB on a coherent pair (Tube → Bite) and linear −3 dB on an incoherent one (Tube → Fracture).
- SC-008's settle length = max(100 ms, 5 × 10 ms) = **100 ms**.

### C-17 (record): the per-channel Amount bypass

FR-022's Amount 0 means **both** channels' effective Amounts (centre ± offset, clamped) have target 0. With nonzero
offsets and centre Amount 0, one channel can be above 0, so the processed branch runs.

### C-18 (refinement): `perceivedPitchRatio()` reads targets, not smoothed values

- The observer and its hysteresis update in `setAmount`, `setTone`, `setAlgorithm` and `setInputHalfWaveSymmetric`
  from the **centre targets**, so they are independent of audio and block size.
- This matches FR-036 ("computed from the centre Amount and Tone"). D-8's crossfade rule reports the incoming
  algorithm's r from the moment `setAlgorithm` is accepted.

### C-19 ⚠ RE-RULE (FR-021 Rectify row): Rectify composes `Rectifier`'s laws, not a running `Rectifier`

- FR-021 says "**Rectify** composes `Rectifier`" with no "or" clause. The stage (C-13 row 5, S5.4) uses only
  `Rectifier::RampLaw`, `Rectifier::AbsLaw`, `Rectifier::sineMakeup` and `Rectifier::linearCoefficient` inside its own
  oversampled domain, with its own wet / blend mapping from Amount. It never instantiates or runs a `Rectifier`.
- Why: running `Rectifier::processBlock` inside the stage would nest a second 4× round trip (16×), double the delay and
  break FR-053's phase-matched dry share (C-13).
- Consequences:
  - `Rectifier`'s blend smoother, residual DC blocker and ceiling have no consumer in the shipped chain. They are
    verified only through `Rectifier`'s own tests (SC-014 and the SC-010, SC-011, SC-016, SC-018, SC-019 and SC-021
    Rectifier arms, S9.1).
  - Those standalone tests cannot see a defect in the stage's copy of the processing path, so a stage-level test checks
    the law that ships (below).
- **Proposed FR-021 wording:** "**Rectify** composes `Rectifier`'s laws (`RampLaw`, `AbsLaw`, `sineMakeup`,
  `linearCoefficient`) inside the stage's oversampled domain; `Rectifier` itself is the standalone component of
  FR-001–FR-005." On ratification the wording goes into the FR-021 row first, then into Clarifications.
- **Stage-level law test** `BassCharacter_RectifyLawMatchesRectifier` (bass_character_stage_test.cpp), the SC-014(a)
  form applied to the shipped branch:
  - through `BassCharacterStageProbe`, a slow ramp (Δ = 1e-4·P per F-sample, over [−4P, 4P]) is fed straight into the
    Rectify branch at Amounts {0.2, 0.4, 0.55, 0.7, 0.85, 1} with the smoothers snapped;
  - the branch output (before the post tilt, held at 0.5) equals (1 − wet)·x[n−1] + wet·mk(b)·`Rectifier::transfer`(x[n−1], b)
    within 1e-5, with wet, b and mk from S5.4 (on a slow ramp ADAA2 equals the law, as `closed_form_adaa2_test` shows);
  - the branch's lin coefficient equals (1 − wet) + wet·mk(b)·`Rectifier::linearCoefficient`(b) exactly.
- **Alternative:** run a real `Rectifier` at base rate before or after the stage's path. Rejected for the C-13 reasons.

### C-20 ⚠ RISK (SC-015 setup) — SUPERSEDED by the user ruling of 2026-10-15 (Clarifications "T001 CPU bench ruling"): SC-015 gates the mono-LINKED cell (Phase 2 precedent, bar 2.0 % unchanged) and records the unlinked cell for Phases 7 and 9. The text below is the plan's original reading and the evidence that T001 overturned.

- S10.7's estimates were per **mono-linked** voice. The mono link (S2) holds only while the sanitised L/R inputs are
  bit-identical and the offsets are 0. Once unlinked a stage stays unlinked until `reset()` (`harmonic_fold.h:203–208,
  382`). Every stereo source, and every nonzero `setChannelOffsets` (Phase 7 drives them, S13), runs both channels.
- **Ruling proposed:** SC-015's gated cell runs **unlinked**: decorrelated L/R input (kSawAnchor on L, the same fixture
  offset by 0.37 period on R) **and** `setChannelOffsets(0.15, 0.15)` on Character. The test asserts `linked_ == false`
  on both stages through the probes before timing. The linked figure is printed as information only.
- **Feasibility (⚠).** Phase 2's own record multiplies the stage cost by ≈ 1.74 when stereo is forced (chain 1.708 →
  2.462 %, `specs/profundum-phase2-edge-fold/compliance.md:7`). Applied to S10.7's linked 1.2–1.6 %, the unlinked
  estimate is **≈ 2.1–2.8 %, over the 2.0 % bar**. T001 measures it before any header is written. If T001 confirms
  > 2.0 %, the R-1 levers are applied in order and re-measured; if they do not close the gap, the plan returns to the
  user with T001's table. Relaxing the bar, or gating the linked cell, is never the lever.
- Phase 2's precedent differs (its stereo-forced chain was recorded, not gated). SC-015's setup says "stereo" and FR-065
  makes the budget functional, so the unlinked cell is the reading this plan gates.

### C-21 (refinement, SC-029(c) × D-7): a request equal to the target cancels a queued request

- S5.6 as first written kept `pending_` when a request equal to `target_` arrived during a crossfade. Then A → B,
  request C, request B ends with C playing, although the last request was B. D-7 says only the last request is kept.
- **Ruling proposed:** during a crossfade, a request equal to `target_` clears `pending_` and starts nothing; with no
  request pending it is a plain no-op. SC-029(c) ("a request equal to the then-current target is a no-op") is read as
  "starts no crossfade"; cancelling a queued request is D-7's last-request-wins rule, not a new transition.
- The r observer follows `target_` (C-18, D-8), so cancelling a queued request does not change r.
- SC-029(a) adds the orderings C-then-B and C-then-A (S9.3).

### C-22 (refinement, FR-019 / SC-003(c)): ShapeMorph's processed path runs warm while disabled

- SC-003(c) applies the D-11 bound, which holds only when both branches are at steady state. If the processed path were
  reset on engage (as Character's is on leaving its Amount-0 bypass, S5.3 step 3), its halfband and SVF state would
  start from zero and the regime would not be at steady state during the 10 ms ramp.
- **Ruling proposed:** ShapeMorph's processed path runs on every sample after `prepare`, enabled or not. Disabling only
  ramps e to 0; once settled the output is the bit-identical copy (SC-008), selected at the output so the warm path
  cannot leak into it. At engage the regimes are at steady state, so SC-003(c)'s D-11 premise holds literally.
- Cost: a disabled ShapeMorph costs what an enabled one costs. SC-015 records the disabled cell.
- Character is unchanged: its Amount-0 bypass stops and resets the processed path, and that edge is gated by the
  differential click count (SC-021, FR-054), not by D-11.
- **Alternative:** stop the path once settled and reset it on engage. SC-003(c) would then have to re-rule to the
  differential click count under FR-070, which this plan does not propose.

### C-23 (refinement, ruled with T001 on 2026-10-15): linear parts run at the base rate

- Only the nonlinearity of each branch / regime runs at the oversampled rate. Pre-filters (Tube and Bite pre-tilts, BITE's
  emphasis tilt) run at the base rate on the input before `OversampledResidualPath::upsample`; post-filters (every post
  tilt, ROUND's SVF low-pass, HOLLOW's notch and band, which are entirely linear) run at the base rate on the
  **round-tripped** signals after `downsample`, so the dry / wet phase match of FR-053 and the D-11 steady-state premise
  are untouched (a base-rate filter on an un-round-tripped tap would re-introduce the comb SC-027 forbids).
- Linear filters commute with the halfband's flat passband, so the laws of S5.4 / S6.3 are unchanged in the audio band;
  the aliasing figures of S10.3 are re-measured in C++ by the per-algorithm smoke cells (T011–T015) as before.
- HOLLOW therefore never enters the oversampled region: its output is the linear path's decimated signal filtered at base
  rate, weighted by w_H.

### C-33 (user ruling 2026-10-16, close): SHAPE smoothed by a restartable 10 ms raised-cosine ramp

- The 10 ms one-pole (99 % in 10 ms) put its steepest slope at the first sample of every move; on kSawAnchor E0 the
  BITE → HOLLOW blend (0.32 apart at the same instant) then moved 0.010 per sample against references at 0.003 and
  counted as a click (SC-003(a)). `ShapeMorph::ShapeRamp`: from the current value to the target over 10 ms with
  w = (1 − cos πp)/2, restarting from the current value on a new target; snapToTarget under the unsmoothed seam.
  Same probe API (`smoothedShape`). Measured: SC-003(b) ratio 0.70 → 0.24, the 50 ms-ramp cells green.

### C-32 (main-loop defect fix 2026-10-16, close): regime weights after the filters; HOLLOW's inverse always on

- SC-003 [long] (first full run) failed 9 saw cells at the s = 0.5 handover: `lin = w·l`, `res = w·(y − l)` put the
  weight before the shared `OversampledResidualPath` and before ROUND's low-pass, so their memory lagged the falling
  weight and was cut in one sample when `roundOn_` / `biteOn_` dropped (0.088 step on kSawAnchor E0, bar 1.1 × 0.030);
  HOLLOW's `inverse` was switched in and out with BITE and reset at BITE entry (hollowOut +17 % in one sample).
  S6.3 / S6.4 as built now: `nl = path(l, y − l)` unweighted, `z = w_NL · (ROUND ? LP(nl) : nl) + w_H · HOLLOW(inv(lb))`,
  `inv` keyed with HOLLOW at t2 (0 while BITE is off, exact unity) and never reset at BITE entry. Steady state is
  identical (linear scaling commutes); SC-002, SC-003, SC-010 (Shape), SC-025 and SC-015 re-measured.
- Second finding, same case: with the weights moved, 6 E0 saw cells still counted one detection, 3–7 samples after
  the step: HOLLOW's tap chain (hollowDown, inverse, notch, band) was reset on entry and filled from zero in three
  samples while its weight was already 2–8 % (|dy| 0.009 against references at 0.003). HOLLOW is linear and
  weight-scaled, so S6.3's reset-on-entry now applies to ROUND and BITE only; HOLLOW runs warm from prepare (the C-22
  pattern) and is always keyed. Cost: one halfband decimator and three first- / second-order sections at SHAPE 0 and 1,
  where HOLLOW was off; SC-015 re-measured.

### C-31 (user ruling 2026-10-16, close): Fracture observer edges = Fold-100 sine ∩ triangle; saw recorded

- R-6 fired: the harvest's saw bands sit 0.1–0.2 Amount below sine / triangle at Tone ≥ 0.75 (empty intersection at 0.75
  and 1). `kFractureFlipEdges` is harvested over {sine, triangle} × notes per Tone; the saw rows stay in
  `harvest-rlaw.txt` as records; SC-012(d) / SC-024(a) gate sine and triangle only. The harvest's 0.01 across-note
  spread check stops for Rectify and is recorded for Fracture (C-4 refinement: spread up to two bisection steps, the
  intersection over notes still inside every gated band). Phase 9 record: FOLLOW lags Fracture on saw-like voices.
- Harvest precision (main loop, same day): the HARVEST lines print the exact k/128 bisection values (%.8f); four
  decimals had put a pasted edge 0.00004 outside its band. SC-026(a)'s observer sweep walks the 1/1280 lattice.

### C-30 (user ruling 2026-10-16, close): Rectify descriptor rule, h2 re h1 below −20 dB

- SC-012(e)'s premise ("even harmonics present when w₂ ≥ −40 dB → descriptor `false`") was 20 dB off: the BITE →
  Fold-100 triangle leaves h2 at −19.9 dB and still doubles at Amount ≥ 0.8. Probe: 2·f₀ at h2 ≤ −22 dB (Tone 0.5, 1) /
  ≤ −26 dB (Tone 0), f₀ at ≥ −14 dB. FR-031 gains the rule (`true` below −20 dB; −24 … −16 dB uncertainty band
  recorded); `BassCharacter_RLawUpstreamChain` applies it and records in-band cells. Phase 9 computes w₂ for the chain
  it runs.

### C-29 (user ruling 2026-10-16, close): SC-012(f) gates only inputs that read f₀; kGrowl recorded

- R-12 fired on the first full (f) grid: 71 / 72 Character and 27 / 40 ShapeMorph kGrowl cells at −1200 cents, every
  other cell green. Cause: the growl input is 2.6 dB (raw) / 1.8 dB (Fold-100) from the estimator's octave boundary
  and any added f₀-series salience tips it. S6.5's observer route rejected (wrong direction for FOLLOW; three new
  APIs; needs an input classifier). C-3's input gate extends to (f); `BassCharacter_RIsOne` and `ShapeMorph_RIsOne`
  gate `readsF0` inputs and record the rest (csv `gated` column); Phase 9 FOLLOW row carries the finding (S13).

### C-28 (user ruling 2026-10-16, Group 9 gate, round 8): one-period pre-roll with DC prime at start(a) and bypass exit

- S5.6 and S5.3 step 3 amended. A cold-started branch's residual DC blocker (2 Hz, C-19 / Phase 2 convention) starts
  at zero state, so a branch whose residual carries DC (Rectify at Amount 1: 0.43 on a saw) emits that DC into the
  crossfade and drains it over ≈ 80 ms: every switch into Rectify failed SC-009's +1.5 dB window by ≈ 4.5 dB. The
  incoming branch now runs unmixed for P = round(fs / f0) samples while the outgoing branch alone is heard, its slot
  accumulates the blocker input, `OversampledResidualPath::primeDc(mean)` sets the blocker state to
  (x1, x1 − mean) (`DCBlocker::setState`, append-only), and the crossfade starts. Bypass exit pre-rolls the running
  branch(es) the same way with e held at 0. At most two branches run (the pre-rolling one counts). Cost: a switch
  takes 1/f0 + 10 ms, a bypass exit 1/f0 + 5 ms (48 ms + at E0); **Phase 9 handoff** (S13): the voice's algorithm
  changes and Amount-0 exits are not sample-aligned to the request. Probes `prerolling`, `engagePrerolling`,
  `prerollLength`; `crossfading` reports in flight (pre-roll or crossfade).

### C-27 (user ruling 2026-10-16, Group 9 gate, round 7): Crush m_min = 3

- S5.4 / C-26 amended again: m(A) = 64·(64/3)^−A (`kCrushMultipleHi = 3.0f`). Round 2's 2.5 (chosen on SC-007 after
  m = 2 silenced the growl) failed SC-005 Tube vs Crush on the saw: a non-integer m blends two hold counts and neither
  count's nulls survive. At 3: SC-005 900/900, SC-007 saw 3.30 dB (margin 0.3 dB recorded), growl −32 dBFS. The
  `BassCharacter_CrushHoldRing` premises derive m from the stage constants.

### C-26 (user rulings 2026-10-16, Group 8 gate): switch level window, offset ceilings, Bite pre-tilt, Crush m law

- SC-009 level window re-ruled to [min − 2.5 dB, max + 1.5 dB] (C-16 law unchanged; the ideal crossfade alone reads
  +1.04 / −2.14 dB and no α passes ±1 dB both sides). SC-013(b): per-algorithm offset ceiling on {0.05, 0.10} for Rectify
  and Fracture, Phase 7 handoff (S13). C-8 amended: Bite pre tilt ±9 dB (Tube stays ±6 dB). S5.4 amended: Crush
  m(A) = 64·32^−A (m = 2 at Amount 1), hold grid at θ = (k + ½)/m (`kCrushGridOffset = 0.5`, the Group 8 fixer repair of the
  on-edge sampling that failed the 48/96 kHz level, Drop tracking and DC gates). All re-measured by the Group 8 gate.

### C-25 (record, 2026-10-15, Group 2 epoch 2): task-level tolerances set from the measured design

- `ClosedFormAdaa2` tests: diode-vs-sigmoid match in ulp of max(1, |f|); slow-ramp bar 5e-6 away from 0 (double rounding of
  F2 ≈ 51 / h²); the alternating-input fallback is compared against f at the midpoint. `KeytrackedTilt` tests: pivot unity
  ± 0.05 dB (the matched-pole one-pole is not power-complementary: 0.017 dB at C2, 0.035 dB at C3 fully tilted), keytracking
  0.1 dB per harmonic (measured 0.0835 at n = 16; spec SC-028(b) 1.5 dB), symmetry over x ∈ [1/8, 8] centred on the pivot within 0.06 dB
  (the C++ gate measured 0.0524 dB on the matched-pole design; the 0.05 dB first set was the one unmeasured cell).
  No spec bar moved; S4.1 and S6.1 designs unchanged (a TPT split was considered and rejected as a plan change for 0.02 dB).

### C-24 (refinement, ruled with T001 on 2026-10-15): Fracture's phasor functions are not transcendental per sample

- s = sin(πφ), σ(φ) = tanh(4·s)/tanh(4) and the bias term tanh(g·β·s) depend only on the ÷2 phasor. They are produced by a
  rotating phasor (magic-circle or complex rotation, re-normalised on every f₀ change / each block) for s, and a rational or
  polynomial tanh approximation with ≤ 1e-4 absolute error on [−4, 4] for σ and the bias term, evaluated once per
  oversampled sample from the phasor state; `std::sin` / `std::tanh` are called only on re-keying. The ADAA stage stays
  `TanhADAA`.
- The approximation error is a level error ≤ 1e-4 on a modulator at f₀/2, far below every SC-024 and SC-010 margin; the
  C++ Fracture smoke cell (T015) re-measures the n·f₀/2-lattice aliasing.
- T001 epoch 2 prototypes both levers in the bench, adds a `--linked` mode (L == R input, one channel processed and copied,
  which is the mono link of S2) and reports linked and unlinked cells; the main loop runs the lane and proceeds only if the
  linked Total ≤ 2.0 %.

---

## S2. Architecture

```
x (L,R) ─ sanitise ─┬─────────────────────────────────────────── bypass (zero-delay copy) ───────────┐
                    │                                                                                  │ e: engage / Amount-0
                    └─ OversampledResidualPath::upsample ─ u[F] ─► branches at F·fs ─┐                │    crossfade
                                                                                     │                ▼
            per branch b (≤ 2 regimes in ShapeMorph, ≤ 2 algorithms in Character):    │      y = (1 − e)·x + e·(g_L·z)
              y_b = T_post,b( N_b( T_pre,b(u) ) )      (nonlinear law N_b; ADAA / 8×) │                │
              ℓ_b = c_b · u_aligned,b                   (its linear reference)        │          clamp ±6.0
            lin = Σ w_b ℓ_b (+ dry share)     res = Σ w_b (y_b − ℓ_b) ───────────────┘
                    │                                │
            downLin (halfband ↓F)        downRes (halfband ↓F, 3 notches @2fs) ─► DCBlocker 2 Hz
                    └──────────────── z = lin_base + res_base ──────────────────────────────────────────┘
```

**Why a residual split and not a plain round trip.**
- A halfband pair is −3 dB at fs/2. The nonlinear products in 0.50–0.55·fs alias straight back (C-11 row 1).
- HarmonicFold's three 2fs notches fix that (`harmonic_fold.h:300–301`). In the same model a full-band notch would cost
  ≈ −10 dB at 0.45·fs and −1.4 dB at 0.3·fs.
- The split notches only the **residual** y_b − ℓ_b. The linear part, and the dry share, see a flat round trip
  (SC-027: identity law → residual 0 → output = round trip exactly).
- The DC blocker also sits on the residual only. h1 is not rotated by a 2 Hz pole (+5.5° at E0 if it were on the full
  path), and Rectify's DC (−13 dBFS on the calibration sine) is removed: 2 Hz corner, τ = 80 ms, so the 1 s settle
  leaves the start-up DC 109 dB down (SC-011).
- **Any c_b is correct.** lin + res = Σ w_b y_b before filtering. c_b only decides how much in-band linear content
  passes through the notch skirts and the DC pole.
- The plan's c_b values are each branch's h1 coefficient on the calibration sine, aligned to the branch's own delay
  (S5, S6), so the residual is mostly generated harmonics.

**Shared domain.**
- Every branch of a stage runs at the same F·fs in one round trip. Regime and algorithm crossfades therefore never
  mix a delayed path with an undelayed one; that mix would comb (Phase 2 C-5).
- The only zero-delay branch is the bypass. It is crossfaded in or out over 5–10 ms, and the comb during that ramp is
  bounded by the differential click gates.

**Mono link** (both stages and `Rectifier`). HarmonicFold's mechanism (`harmonic_fold.h:199–208`): while the sanitised
L and R inputs and the per-channel parameters are bit-identical, channel 0 runs alone and is copied. At the first
difference channel 1 takes channel 0's state. Channel offsets ≠ 0 unlink at `setChannelOffsets`. Once unlinked, a stage
stays unlinked until `reset()` (`harmonic_fold.h:382`). The link saves CPU for mono sources only; it is not part of the
SC-015 budget, which is gated unlinked (C-20).

---

## S3. `OversampledResidualPath` (Layer 2, `processors/oversampled_residual_path.h`, NEW — C-1)

```cpp
namespace Krate::DSP {
/// One channel of the Phase 5 oversampled residual path (plan S2/S3). Heap-free, copyable, noexcept.
/// Precondition: FTZ/DAZ on (the halfband sections, notches and DC blocker are not flushed per call).
class OversampledResidualPath {
public:
    static constexpr std::size_t kMaxFactor = PolyphaseHalfbandOversampler::kMaxFactor;   // 4
    static constexpr std::array<float, 3> kNotchRatios{0.50f, 0.52f, 0.55f};             // re fs, at 2fs (origin harmonic_fold.h:300, private there; C-1)
    static constexpr std::array<float, 3> kNotchQs{1.5f, 2.0f, 1.5f};                    // (origin harmonic_fold.h:301, private there)
    static constexpr float kResidualDcHz = 2.0f;                                         // (== HarmonicFold::kDcCornerHz, public, harmonic_fold.h:91)
    static constexpr double kFourTimesLimitHz = 100000.0;                                // 4x up to 100 kHz, 2x above (FR-068)

    void prepare(double sampleRate) noexcept;            // factor, notches at 2fs, DC at fs; resets
    void reset() noexcept;
    [[nodiscard]] std::size_t factor() const noexcept;
    void upsample(float x, float* u) noexcept;           // writes factor() samples (up_.upsample)
    /// lin, res: factor() samples each. Returns D(lin) + DC(D_notch(res)).
    [[nodiscard]] float downsample(const float* lin, const float* res) noexcept;
    [[nodiscard]] bool stateFinite() const noexcept;
private:
    PolyphaseHalfbandOversampler up_, downLin_, downRes_;  // up_ upsamples only; down* decimate only
    std::array<Biquad, 3> notch_{};
    DCBlocker dc_;
    float dcIn_ = 0.0f, dcOut_ = 0.0f;                     // mirrors for stateFinite (harmonic_fold.h:313-314 pattern)
};
}
```

- `prepare`:
  - `factor = sampleRate <= 1e5 ? 4 : 2`, then `setFactor(factor)` on all three oversamplers;
  - `notch_[k].configure(FilterType::Notch, kNotchRatios[k]·fs, kNotchQs[k], 0, 2·fs)`;
  - `dc_.prepare(fs, kResidualDcHz)`. The corner is exact up to 96 kHz. At 192 kHz R clamps to 0.9999, an effective
    corner of 3.06 Hz (`dc_blocker.h:236`); recorded.
- `downsample`:
  - `linBase = downLin_.downsample(lin)`;
  - `resBase = downRes_.downsample(res, [this](float v){ for (auto& n : notch_) v = n.process(v); return v; })`;
  - `r = dc_.process(resBase)`;
  - `return linBase + r`.
- The hook runs at 2fs for both factors, as in HarmonicFold.
- **RT:** fixed arrays and three oversamplers. State ≈ 1.0 KB per channel (3 × 296 B oversamplers + 3 × 28 B biquads
  + 32 B DC blocker + mirrors; measured sizes, S0).

---

## S4. `Rectifier` (Layer 2, `processors/rectifier.h`) and `ClosedFormAdaa2` (Layer 0)

### S4.1 `ClosedFormAdaa2<Law>` (`core/closed_form_adaa2.h`; includes `core/db_utils.h`, C-1)

```cpp
/// Law: static double f(double), F1(double), F2(double); F1' = f, F2' = F1, F1(0) = F2(0) = 0.
template <class Law>
class ClosedFormAdaa2 {
public:
    static constexpr double kEpsilon = 1.0e-5;          // C-11; argument units
    void reset() noexcept;                              // x1 = x2 = 0, F2x1 = 0, d1 = Law::F1(0) = 0
    [[nodiscard]] double process(double x) noexcept;    // band-limited f(x[n-1]); one-sample delay
    [[nodiscard]] bool stateFinite() const noexcept;
private:
    double x1_ = 0.0, x2_ = 0.0, f2x1_ = 0.0, d1_ = 0.0;
};
struct DiodeAdaaLaw {                                    // mirrors Asymmetric::diode (sigmoid.h:358-366), C-1
    static double f(double x) noexcept;   // x>=0: 1 - e^{-1.5x}; x<0: x/(1 - 0.5x)
    static double F1(double x) noexcept;  // x>=0: x + e^{-1.5x}/1.5 - 1/1.5; x<0: -4 ln(1 - 0.5x) - 2x
    static double F2(double x) noexcept;  // x>=0: x²/2 - e^{-1.5x}/2.25 - x/1.5 + 1/2.25
                                          // x<0:  8((1-0.5x) ln(1-0.5x) - (1-0.5x)) - x² + 8
};
/// Prepare-time only (never on the audio thread): the h1 Fourier coefficient of DiodeAdaaLaw::f(gain·sin φ) over
/// 256 phase points. BassCharacterStage (c_bite over Amount, S5.2) and ShapeMorph (cBiteShape_ over t2, S6.4) each
/// fill their own 65-point table with it in prepare(); neither L3 class includes the other.
[[nodiscard]] double diodeH1Coefficient(double gain) noexcept;
```

**Exact algorithm** (Bilbao, Esqueda, Parker, Välimäki 2017; ε in argument units):
```
D(a, b) = |a − b| > ε ? (F2(a) − F2(b)) / (a − b) : F1((a + b)/2)
d0 = D(x, x1)
if |x − x2| > ε:   y = 2 (d0 − d1) / (x − x2)
else:              x̄ = (x + x2)/2;  δ = x̄ − x1
                   y = |δ| > ε ? (2/δ)·(F1(x̄) + (F2(x1) − F2(x̄))/δ) : f((x̄ + x1)/2)
x2 = x1; x1 = x; d1 = d0; f2x1 = F2(x)        // F2 cached, one F2 evaluation per sample
```

- The antiderivatives were verified in the model: derivative checks plus continuity at 0.
- The diode's x < 0 branch needs 1 − 0.5x > 0, which holds for every x < 0.
- **Non-finite:** a non-finite x never reaches `process` (the stages sanitise first). `stateFinite()` covers recovery
  after a pathological state. It tests `x1_`, `x2_`, `f2x1_` and `d1_` with `detail::isFinite(double)`
  (`db_utils.h:124–128`, fast-math safe); the stages call it from their own `stateFinite()`.

### S4.2 `Rectifier` public API

```cpp
namespace detail { struct RectifierProbe; }
class Rectifier {
public:
    struct RampLaw { static double f(double x) noexcept { return x > 0 ? x : 0; }           // max(x, 0)
                     static double F1(double x) noexcept { return x > 0 ? 0.5*x*x : 0; }
                     static double F2(double x) noexcept { return x > 0 ? x*x*x/6.0 : 0; } };
    struct AbsLaw  { static double f(double x) noexcept { return x < 0 ? -x : x; }          // |x|
                     static double F1(double x) noexcept { return 0.5*x*(x < 0 ? -x : x); }
                     static double F2(double x) noexcept { return x*x*(x < 0 ? -x : x)/6.0; } };
    static constexpr float kBlendSmoothMs = 10.0f;
    static constexpr float kOutputCeiling = 6.0f;                        // FR-069

    void prepare(double sampleRate, std::size_t maxBlockSize) noexcept;  // maxBlockSize unused (per-sample core)
    void reset() noexcept;
    void setBlend(float b) noexcept;                                     // [0,1]; non-finite keeps previous (FR-066)
    void processBlock(float* left, float* right, std::size_t numSamples) noexcept;  // HarmonicFold convention
    [[nodiscard]] bool stateFinite() const noexcept;
    [[nodiscard]] static constexpr std::size_t latencySamples() noexcept { return 0; }
    [[nodiscard]] float preCeilingPeak() const noexcept;

    /// FR-001/FR-002: the memoryless law, used by the processing path through RampLaw/AbsLaw.
    [[nodiscard]] static float transfer(float x, float b) noexcept { return (1.0f - b) * std::max(x, 0.0f) + b * std::abs(x); }
    /// Odd part of the law: transfer(x, b) = c·x + (1 + b)/2·|x| with c = (1 - b)/2 (exact).
    [[nodiscard]] static constexpr float linearCoefficient(float b) noexcept { return 0.5f * (1.0f - b); }
    /// Level-preserving makeup on a sine (S5.5): sqrt(0.5 / ac²(b)), ac² = (1 + b²)/4 - (1 + b)²/π².
    [[nodiscard]] static float sineMakeup(float b) noexcept;
private:
    friend struct detail::RectifierProbe;
    struct Channel { PolyphaseHalfbandOversampler up, downLin, downRes; std::array<Biquad, 3> notch{};
                     DCBlocker dc; ClosedFormAdaa2<RampLaw> ramp; ClosedFormAdaa2<AbsLaw> abs; float uPrev = 0; /*…*/ };
    std::array<Channel, 2> ch_{}; OnePoleSmoother blend_; bool linked_ = true, prepared_ = false; float peak_ = 0;
};
```

### S4.3 Processing contract (per base sample, per active channel)

1. `x = isFinite(x) ? x : 0` (FR-066), then `b = blend_.process()`.
2. `up.upsample(x, u)`.
3. For each of the F samples:
   - `y = (1 − b)·ramp.process(u_i) + b·abs.process(u_i)`. This is exact by linearity of ADAA in the law, so a moving
     b needs no history recompute.
   - `lin_i = c(b)·uPrev`, where uPrev is the previous oversampled sample: alignment with the ADAA2 delay.
   - `res_i = y − lin_i`.
   - `uPrev = u_i`.
4. `z = D_lin(lin) + DC(D_notch(res))`. This is the S3 structure, duplicated over L1 parts (C-1), so in exact
   arithmetic the residual is (1 + b)/2·|x| and holds every even harmonic plus DC.
5. `out = clamp(z, ±6)`, then update `peak_`.

- **No gain stage.** SC-014(b) is the law's Fourier series, with the DC blocker's recorded attenuation at h2..h8
  (2 Hz pole: < 0.001 dB at C3).
- Edge cases follow the `HarmonicFold` convention: before `prepare`, a null pointer or zero samples is a no-op;
  `right == left` processes one channel.
- **Analytic series used by SC-014(b)**, for x = A·sin θ:
  - h1 = (1 − b)/2·A;
  - h(2k) = (1 + b)/2·A·4/(π(4k² − 1));
  - odd n ≥ 3 → 0.

---

## S5. `BassCharacterStage` (Layer 3, `systems/bass_character_stage.h`)

### S5.1 Public API (FR-020; exact names)

```cpp
namespace detail { struct BassCharacterStageProbe; }
class BassCharacterStage {
public:
    enum class Algorithm : std::uint8_t { Tube, Fold, Rectify, Bite, Crush, Fracture };   // nested (spec ODR row)
    static constexpr std::size_t kNumAlgorithms = 6;
    static constexpr float kSwitchCrossfadeMs = 10.0f;      // D-7
    static constexpr float kBypassCrossfadeMs = 5.0f;       // FR-054, C-16
    static constexpr float kSwitchCrossfadeExponent = 1.493f; // C-16: -4.5 dB midpoint
    static constexpr float kParamSmoothMs = 10.0f;          // Amount, Tone, offsets (OnePoleSmoother)
    static constexpr float kMaxChannelOffset = 0.15f;       // D-12 (gated value; setter clamps to [-1, 1])
    static constexpr float kOutputCeiling = 6.0f;
    static constexpr float kMinFundamentalHz = 8.0f, kMaxFundamentalHz = 20000.0f;
    // Constants of S5.4 (all listed there): kTubeGainLoDb, kTubeGainHiDb, kFoldThetaMin, kFoldThetaMax, kRectHalfEnd,
    // kBiteDriveLoDb, kBiteDriveHiDb, kCrushMultipleLo, kCrushMultipleHi, kCrushBitsLo, kCrushBitsHi, kCrushPreScale,
    // kFractureDriveLo, kFractureDriveHi, kFractureBias, kFractureSquareSharpness, tone pivots and depths (S6.1).

    void prepare(double sampleRate, std::size_t maxBlockSize) noexcept;   // computes the S5.4 tables; resets
    void reset() noexcept;
    void setAlgorithm(Algorithm a) noexcept;
    void setAmount(float a) noexcept;                     // [0,1]
    void setTone(float t) noexcept;                       // [0,1]; 0.5 neutral
    void setChannelOffsets(float amountOffset, float toneOffset) noexcept;  // L = c + o, R = c − o, clamped [0,1]
    void setFundamentalHz(float hz) noexcept;             // [8, 20000]; non-finite keeps previous
    void setInputHalfWaveSymmetric(bool symmetric) noexcept;   // FR-030, D-8; default false
    void processBlock(float* left, float* right, std::size_t numSamples, const float* f0PerSample = nullptr) noexcept;
    [[nodiscard]] float perceivedPitchRatio() const noexcept;  // FR-030/036, C-18
    [[nodiscard]] bool stateFinite() const noexcept;
    [[nodiscard]] static constexpr std::size_t latencySamples() noexcept { return 0; }
    [[nodiscard]] float preCeilingPeak() const noexcept;
    void clearPreCeilingPeak() noexcept;
private:
    friend struct detail::BassCharacterStageProbe;   // SC-027 identity seam, SC-028 tone filters, SC-029 instance count
    /* S5.2 */
};
```

There is no `setAlgorithmBlend` (FR-057, SC-029(e)). Every setter sanitises with HarmonicFold's `sanitizeControl`
rule: non-finite keeps the previous value, a denormal becomes 0, then clamp (`harmonic_fold.h:331–338`).

### S5.2 State layout (fixed-size; ≈ 5.8 KB, from measured member sizes)

- **Per channel** (`std::array<Channel, 2>`):
  - an `OversampledResidualPath` (≈ 1.0 KB);
  - **six per-algorithm member structs**: `TubeBranch tube; FoldBranch fold; RectifyBranch rect; BiteBranch bite;
    CrushBranch crush; FractureBranch frac;`. Each is a plain struct holding only its own algorithm's members. At most
    2 run;
  - the smoothers for effective Amount and Tone;
  - a `uDelay[3]` ring for the lin alignments;
  - a bypass copy flag.
- **The branch structs hold** (sizes from S0's measured `sizeof`; `KeytrackedTilt` ≈ 48 B = `OnePoleLP` 32 + four
  floats; `ClosedFormAdaa2` 32 B = four doubles):

  | Algorithm | State | ≈ Size |
  |---|---|---|
  | Tube | `TubeStage`, `PolyphaseHalfbandOversampler tubeOs` (factor 2), pre and post `KeytrackedTilt` | 520 B |
  | Fold | `WavefolderProcessor`, post `KeytrackedTilt` | 224 B |
  | Rectify | `ClosedFormAdaa2<Rectifier::RampLaw>`, `ClosedFormAdaa2<Rectifier::AbsLaw>`, post tilt | 112 B |
  | Bite | `ClosedFormAdaa2<DiodeAdaaLaw>`, pre and post tilts | 128 B |
  | Crush | two `HoldEngine`s (3-slot step rings, S5.4), two `BitCrusher`s, phasor, post tilt | 190 B |
  | Fracture | `TanhADAA`, ÷2 phasor, `u_prev`, post tilt | 72 B |

  There is **no union and no variant**. Every branch member exists for the life of the stage, is constructed with it,
  and is prepared in `prepare()` (including `tubeOs.setFactor(2)`, `TubeStage::prepare` and
  `WavefolderProcessor::prepare`). `start(a)` only calls the branch's `reset()` and re-keys it with the current f₀
  (S5.3 step 1); nothing is constructed or prepared on the audio thread. All members are default-copyable, which the
  mono link needs (channel 1 copies channel 0).
- **Size:** per channel ≈ 1.0 KB path + ≈ 1.25 KB branches + ≈ 60 B smoothers and ring ≈ 2.3 KB; two channels
  ≈ 4.6 KB; the four 65-point tables ≈ 1.0 KB; switch machine, scratch and the rest < 0.2 KB.
- **Scratch:** `std::array<float, 2·kMaxFactor>` for Tube's 8× sub-block (no heap).
- **Global:**
  - the switch machine (`current_`, `target_`, `pending_` + flag, crossfade position and increment);
  - the bypass ramp `e` (`LinearRamp`, 5 ms);
  - the f₀ state (`f0_` and its bit pattern for change detection);
  - the r-observer state (`flipped_`, the descriptor);
  - the `prepare`-time tables: c_tube[65] and c_bite[65] over Amount (c_bite filled by `diodeH1Coefficient`, S4.1),
    c_frac[65], c_fold[65];
  - `peak_`.
- **`prepare`** prepares every reused processor at the needed rate:
  - `TubeStage` at 2·F·fs, maxBlock 2·F;
  - `WavefolderProcessor` at F·fs, maxBlock F;
  - tilts at F·fs.

  These preparations do not allocate (`tube_stage.h:117`, `wavefolder_processor.h:157` read: no containers), but
  SC-016 asserts it anyway.

### S5.3 Per-base-sample contract

Inside `processBlock`, for sample n:
1. **f₀.** If `f0PerSample`, `f = f0PerSample[n]`; if finite, clamp to [8, 20000], otherwise keep the previous value.
   If the bit pattern differs from `f0Bits_`, store it and push f to every running branch's keytracked members (tilt
   pivots, Crush and Fracture phasor increments). Value-driven updates make the result partition-invariant (SC-018).
   **Re-key rule.** A branch that is not running is not updated. Instead, every path that starts a branch re-keys it:
   `start(a)` (S5.6), leaving bypass (step 3), `reset()` and `prepare()` each reset the branch and then push the
   current `f0_` to it (tilt `OnePoleLP` cutoffs, phasor increments) before its first sample. `setFundamentalHz` stores
   `f0_` and pushes to running branches only. A branch's keytracked coefficients are therefore always a function of
   the current `f0_`, never of a stale note or the prepare default. SC-028(b) and SC-025(a) test this (S9.3).
2. **Sanitise** L and R. Handle the mono link (S2).
3. **Bypass.** If `e == 0` and both effective Amount targets are 0: copy x and `continue`. This is bit-identical
   (FR-022, SC-008) and runs the processed path not at all. On leaving bypass, reset the processed state of every
   running branch and the channel paths, re-key the branches with the current `f0_` (step 1), run them unmixed for
   one period of `f0_` with e held at 0 (the output is still the copy), prime their residual DC blockers to the
   measured residual mean (C-28), then ramp e up over 5 ms. That makes the start deterministic (SC-017, SC-018).
4. **Advance** the parameter smoothers and the switch machine (S5.6). Per active channel: `path.upsample(x, u)`. For
   each running branch b with gain w_b (1, or the crossfade pair), compute its F outputs y_b,i and lin terms
   ℓ_b,i (S5.4), then accumulate `lin_i += w_b·ℓ_b,i` and `res_i += w_b·(y_b,i − ℓ_b,i)`.
5. **Output.** `z = path.downsample(lin, res)`; `out = (1 − e)·x + e·z`; clamp to ±6; update `peak_`.

### S5.4 The six algorithms

These are exact laws. A = the channel's smoothed Amount, T = its smoothed Tone, P = 0.3552343859
(`HarmonicFold::kCalibrationPeak`), and u = the F-rate input.

**Tube** (FR-021 composes `TubeStage`; C-8, C-10):
- `pre.process(u)` (k = 1, ±6 dB), then `tubeOs.upsample`, then `TubeStage::process(buf, 2)`, then
  `tubeOs.downsample`, then `post.process` (k = 2, ±6 dB).
- Settings: `setSaturationAmount(1)` and `setBias(0)` at prepare. `setInputGain(kTubeGainLoDb + (kTubeGainHiDb −
  kTubeGainLoDb)·A)` = −6…+12 dB. `setOutputGain(−20·log₁₀ c_tube(A))`, clamped ±24.
- c_tube(A) is the h1 coefficient of `Asymmetric::tube(g·P·sin θ)/P` over θ, with g = 10^{gainDb/20}. It is computed
  at prepare on 65 Amount points (256 phase points each) and interpolated linearly.
- Saturation is pinned at 1, so the `tube_stage.h:270` bypass threshold is never crossed (spec Edge Case, satisfied by
  construction).
- **ℓ = 1·u[n − 2]**, a two-sample delay at F·fs that approximates the branch 2×'s ≈ 1.9-sample group delay
  (C-15). Any residual mismatch lands in the residual (S2).

**Fold** (composes `WavefolderProcessor`; C-12):
- Model `Serge`, symmetry 0, mix 1 at prepare; `setFoldAmount(θ)` with θ = 0.1 + 2.4·A (≥ `kMinFoldAmount`).
- Per F-rate sample: `buf = u / P`, then `process(buf, 1)`, then `· P / sin(min(θ, π/2))`, then `post.process`
  (k = 2, ±12 dB).
- **ℓ = c_fold(A)·u**, where c_fold is the h1 coefficient of P·mk·sin(θ·sin φ) (table) and mk is the peak-preserving
  makeup. No delay: the law is memoryless.

**Rectify** (composes `Rectifier`'s laws; C-13; FR-053):
- wet = min(1, A/0.4); b = smoothstep((A − 0.4)/0.6); mk = `Rectifier::sineMakeup(b)`.
- y = (1 − wet)·u[n−1] + wet·mk·((1 − b)·ramp(u) + b·abs(u)); then `post` (k = 2, ±12 dB).
- **ℓ = ((1 − wet) + wet·mk·(1 − b)/2)·u[n−1].** The dry share therefore travels the linear path of the same round
  trip (phase-matched, SC-027 via C-14).

**Bite** (C-8, C-11):
- tp = `pre.process(u)` (k = 1, ±6 dB); v = g·tp/P, g = 10^{(−12 + 30A)/20} (−12…+18 dB);
  y = `adaa.process(v)`/c_bite(A); then `post` (k = 2, ±6 dB).
- c_bite(A) = h1 coefficient of diode(g·sin φ) (table filled at prepare by `diodeH1Coefficient(g(A))`, S4.1), so the
  branch's h1 equals the input's.
- **ℓ = 1·u[n−1].**

**Crush** (FR-051, D-6):
- m(A) = 64·(1/16)^A: 64 at A = 0, 4 at A = 1, continuous.
- m_eff = min(m(A), F·fs/(4·f₀)). The m_lo engine's steps are ≥ 4 F-samples apart. The m_hi engine runs at
  m_hi ≤ m_eff + 1, so its spacing F·fs/(f₀·m_hi) is only ≥ 4·m_eff/(m_eff + 1). Over the supported range
  m_eff ≥ F·fs/(4·20 kHz) ≥ 2.205 (44.1 kHz, F = 4; 4.8 at 192 kHz, F = 2), so every engine's spacing is ≥ 2.75
  F-samples. Example: 44.1 kHz, F = 4, f₀ = 20 kHz gives m_eff = 2.205, m_hi = 3, spacing 2.94.
- m_lo = ⌊m_eff⌋, m_hi = m_lo + 1, frac = m_eff − m_lo.
- Bits B(A) = 12 − 8·A, on two `BitCrusher`s with dither 0 (`setDither(0)` at prepare; no randomness used).
- **HoldEngine(m)**, sharing one phasor θ ∈ [0, 1) per channel (cycles of f₀, θ += f₀/(F·fs), reset to 0):
  - when ⌊θ·m⌋ changes inside (θ_prev, θ], the crossing fraction is τ = (⌈θ_prev·m⌉/m − θ_prev)/Δθ ∈ (0, 1];
  - value = u_prev + τ·(u − u_prev);
  - q = P/0.9·BitCrusher.process(0.9·value/P);
  - a step of height q − held is scheduled at time t_k = n − 1 + τ;
  - output = held − Σ_active d_k·(1 − S(n − t_k)), with S the causal integrated cubic B-spline over v ∈ [0, 4)
    (the `polyblep.h:105` polynomial pieces):

  ```
  S(v) = v⁴/24                                 0 ≤ v < 1
         1/2 − (3w⁴ − 8w³ + 16w)/24, w = 2 − v   1 ≤ v < 2
         1/2 + (3w⁴ − 8w³ + 16w)/24, w = v − 2   2 ≤ v < 3
         1 − (4 − v)⁴/24                        3 ≤ v < 4;   1 for v ≥ 4
  ```

  - **Occupancy bound.** A step is active over [t_k, t_k + 4). With spacing ≥ 2 F-samples, at most **2** steps are
    active at once and at most one crossing occurs per F-sample. Each engine keeps a ring of **3** slots, one spare.
  - **Ring-full policy** (unreachable under the bound, defined anyway): the oldest step is retired, which drops its
    remaining correction d·(1 − S). The probe counts retirements, and the Crush tests assert the count is 0.
  - **Crossing detection under a moving m.** Crossings are counted as ⌊θ·m⌋ − ⌊θ_prev·m⌋ with the **current** m on both
    sides (θ unwrapped across the period wrap). A change of m alone therefore never schedules a step; it only
    re-anchors the grid.
  - **Engines keyed by the parity of m.** Engine (m mod 2) runs m. When ⌊m_eff⌋ crosses an integer (frac → 1, then 0),
    the engine that ran m_hi keeps running the same m, now as m_lo, with its state intact. The other engine takes the
    new m_hi at weight frac ≈ 0. No held state is handed between engines.
  - **Unit case** `BassCharacter_CrushHoldRing`: f₀ = 20 kHz at 44.1 kHz, and an Amount ramp that carries m_eff across
    ≥ 3 integers at E1. It asserts ring occupancy ≤ 2 at every F-sample, retirements == 0, and a differential click
    count of 0 across the integer crossings.
- y = (1 − frac)·Hold(m_lo) + frac·Hold(m_hi); then `post` (k = 2, ±12 dB).
- **ℓ = 0.** All output goes to the residual. Its h1 sees the 2 Hz pole (+5.5° at E0, recorded) and the notch skirts
  above 0.3·fs.
- Every product is a harmonic of f₀: the hold instants are periodic in T and the step shape is fixed. The 2-sample
  causal centring is a constant delay. So r ≡ 1 (FR-032).

**Fracture** (C-5 law):
- φ ∈ [0, 2) in periods of f₀, φ += f₀/(F·fs), reset 0.
- σ = tanh(4·sin πφ)/tanh 4; s = sin πφ; a = A; g = 0.5 + 1.5A; β = 0.5A.
- v = g·(u·((1 − a) + a·σ)/P + β·s); y = (P/g)·(TanhADAA[drive 1](v) − tanh(g·β·s_mid)), where s_mid is s at
  φ − Δφ/2 (ADAA1's half-sample alignment). Then `post` (k = 2, ±12 dB).
- **ℓ = (1 − a)·c_frac(A)·(u + u_prev)/2**, where c_frac = the h1 coefficient of (P/g)·tanh(g·sin φ) (table).
- f₀ moves the phasor's rate, never its phase (spec Edge Cases).

### S5.5 Rectify makeup (closed form, used by S4.2 and S5.4)

For x = sin: E[y²] = (1 + b²)/4 and E[y] = (1 + b)/π. Hence ac²(b) = (1 + b²)/4 − (1 + b)²/π² and
mk(b) = √(0.5/ac²). This gives mk(0) = 1.834 and mk(1) = 2.297, which the model reproduces (`rectMakeup`).

### S5.6 Algorithm switch machine (FR-026, FR-057, D-7, C-16)

```
setAlgorithm(a):
  if xfading:  if a == target_ → clear pending_; start nothing (D-7 last request wins; SC-029(c); C-21)
               else pending_ = a (last request wins, D-7)
  else:        if a == current_ → no-op; else start(a)
start(a): target_ = a; reset branch a on both channels and re-key it with f0_ (S5.3 step 1);
          pre-roll (C-28): run branch a unmixed for P = round(fs / f0_) samples (≥ 1), accumulating its slot's
          residual-blocker input; at P prime the blocker to the mean (path.primeDc) and start the crossfade:
          pos = 0; inc = crossfadeIncrement(10, fs)
  "xfading" in setAlgorithm above means in flight: pre-roll or crossfade
per sample while xfading: pos += inc; g_out = cos(πpos/2)^α, g_in = sin(πpos/2)^α (α = 1.493)
  at pos ≥ 1: current_ = target_; xfading = false; if pending_set && pending_ != current_: start(pending_); clear pending
```

- At most two branches ever run, so the probe counts ≤ 2 (SC-029(b)).
- The output during a queued switch equals "A → B, then B → D at the crossfade end" (SC-029(a)).
- The r observer reports `target_`'s r from `start` onward (D-8). A queued (`pending_`) request does not change r
  until it starts. SC-026 tests both (S9.3).

### S5.7 r observer (FR-030–FR-036; C-2–C-5, C-18)

| Algorithm | r |
|---|---|
| Tube, Fold, Bite, Crush | 1 |
| Rectify, `setInputHalfWaveSymmetric(false)` (default) | 1 |
| Rectify, `true` | 2 if `flipped_`, else 1 |
| Fracture | 0.5 if `flippedF_`, else 1 |

- **Hysteresis** (both flips). The edges [A_lo(T), A_hi(T)] are constexpr tables at Tone ∈ {0, 0.25, 0.5, 0.75, 1}
  with linear interpolation. The values are harvested by T030 from the real stage and the real estimator. Model seeds:
  - Rectify: Tone 0 → [0.837, 0.912], 0.5 → [0.787, 0.869], 1 → [0.723, 0.822];
  - Fracture: 0.5 → [0.326, 0.342].
- **Update rule** (on every centre-target change): if `!flipped && A ≥ A_hi` then flipped = true; if
  `flipped && A ≤ A_lo` then flipped = false.
- `reset()` sets flipped = false (SC-026(c)). Offsets never enter (FR-036, SC-026(d)).

---

## S6. `ShapeMorph` (Layer 3, `systems/shape_morph.h`)

### S6.1 `KeytrackedTilt` (Layer 2, `processors/keytracked_tilt.h`, NEW — C-1)

```cpp
class KeytrackedTilt {
public:
    void prepare(double processRate) noexcept;
    void configure(float pivotMultiple, float maxShelfDb) noexcept;
    void setTone(float tone) noexcept;            // G = (2t − 1)·maxShelfDb; gL = 10^{−G/20}·ν, gH = 10^{G/20}·ν,
                                                  // ν = √(2/(gL² + gH²)) (unity magnitude at the pivot)
    void setFundamentalHz(float f0) noexcept;     // lp_.setCutoff(pivotMultiple·f0) (OnePoleLP clamps at 0.495·rate)
    void reset() noexcept;
    [[nodiscard]] float process(float x) noexcept { const float lp = lp_.process(x); return gL_ * lp + gH_ * (x - lp); }
private:
    OnePoleLP lp_; float k_ = 2.0f, maxDb_ = 0.0f, gL_ = 1.0f, gH_ = 1.0f;
};
```

- At Tone 0.5, gL = gH = 1, so the response is flat within rounding (SC-028(a)).
- The response is a function of f/(k·f₀) only, so it is keytracked (SC-028(b)).
- The 10^x pair is recomputed only when `setTone` receives a changed value.

### S6.2 Public API (FR-010)

```cpp
namespace detail { struct ShapeMorphProbe; }
class ShapeMorph {
public:
    static constexpr float kRoundDrive = 0.8f;                 // TanhADAA drive at the calibration peak
    static constexpr float kRoundCutoffLo = 10.0f, kRoundCutoffHi = 20.0f;  // × f0, log-interpolated over SHAPE [0, 0.5]
    static constexpr float kRoundQ = 0.70710678f;
    static constexpr float kHollowNotchMultiple = 3.0f, kHollowNotchQ = 1.0f;
    static constexpr float kHollowPeakMultiple = 9.0f, kHollowPeakQ = 3.0f, kHollowPeakGain = 1.0f;
    static constexpr float kBitePivotMultiple = 4.0f, kBiteMaxEmphasisDb = 12.0f;
    static constexpr float kBiteDriveLoDb = -12.0f, kBiteDriveHiDb = 15.0f;
    static constexpr float kShapeSmoothMs = 10.0f, kEngageMs = 10.0f, kOutputCeiling = 6.0f;
    static constexpr std::array<float, 101> kLoudnessTableDb{ /* T040 harvest; model seed 1.31…3.76 dB, S10.6 */ };

    void prepare(double sampleRate, std::size_t maxBlockSize) noexcept;
    void reset() noexcept;
    void setShape(float position) noexcept;        // [0,1]
    void setEnabled(bool enabled) noexcept;        // D-2
    void setFundamentalHz(float hz) noexcept;
    void processBlock(float* left, float* right, std::size_t numSamples, const float* f0PerSample = nullptr) noexcept;
    [[nodiscard]] bool stateFinite() const noexcept;
    [[nodiscard]] static constexpr std::size_t latencySamples() noexcept { return 0; }
    [[nodiscard]] float preCeilingPeak() const noexcept;
private:
    friend struct detail::ShapeMorphProbe;
};
```

### S6.3 Regimes, at F·fs and sharing the S3 round trip

Let s be the smoothed SHAPE, t = min(1, 2s) and t₂ = max(0, 2s − 1).

| Regime | Weight | Law |
|---|---|---|
| ROUND | w_R = s ≤ ½ ? 1 − t : 0 | y = SVF_LP[k_R·f₀, Q 0.707]((P/0.8)·TanhADAA[0.8](u/P)), k_R = 10·2^t |
| HOLLOW | w_H = s ≤ ½ ? t : 1 − t₂ | y = SVF_A.notch(u_d) + 1.0·SVF_B.band(u_d): notch at 3·f₀, Q 1; band (0 dB peak, `processMulti`) at 9·f₀, Q 3; u_d = u[n−1] |
| BITE | w_B = t₂ | y = DiodeAdaa2(g_B·Tilt[4·f₀, +12·t₂ dB](u)/P)/c_B(t₂), g_B = 10^{(−12 + 27t₂)/20}; c_B = `cBiteShape_` (S6.4) |

- **Alignment.** The ROUND, HOLLOW and BITE delays are ½, 1 and 1 F-sample. ℓ = 1·u[n−1] for all three, because each
  has unity h1 small-signal gain; ROUND's ½-sample offset falls into the residual.
- **Edge Case "SHAPE at 0, 0.5 or 1": regimes are reset on entry.** A regime whose weight goes 0 → > 0 is reset at
  that sample, then re-keyed with the current `f0_` and smoothed s (SVF cutoffs, BITE tilt pivot and gain) before its
  first sample, as S5.3 step 1's rule does for Character branches. Its first output samples have weight ≈ 0.01 per
  step, so the transient is bounded by SC-003(b).
- **Loudness.** z ← z·10^{L(s)/20}, with L from `kLoudnessTableDb` (linear interpolation in s), applied after
  `downsample`.
- **Engage (C-22).** A 10 ms linear ramp e against the zero-delay bypass. The processed path runs **warm** on every
  sample, enabled or not. Disabling only ramps e to 0; once settled the output is the bit-identical copy (SC-008),
  selected at the output. At engage the regimes are at steady state, which is the premise of SC-003(c)'s D-11 bound.
- **Keytracking.** Every multiple is applied through `setCutoff(k·f₀)` on f₀ change (S5.3 step 1) **and on every change
  of the smoothed s**. k_R = 10·2^t, BITE's emphasis gain 12·t₂ dB and g_B all depend on s, so they are recomputed
  whenever s or f₀ changes (per sample during a SHAPE ramp; costed in SC-015, S8). SVF cutoffs clamp at 0.495·(F·fs)
  (`svf.h:129`), continuously in f₀.
- **Regime count (FR-011).** The weights give at most two nonzero regimes at any s (w_R and w_B are never both > 0).
  The probe exposes the running-regime count, asserted ≤ 2 (S9.3 SC-002 and SC-003).

### S6.4 `ShapeMorph` state layout (fixed-size; ≈ 3.1 KB)

- **Per channel** (`std::array<Channel, 2>`), ≈ 1.4 KB:
  - an `OversampledResidualPath` (≈ 1.0 KB);
  - ROUND: `TanhADAA` + `SVF` (≈ 116 B);
  - HOLLOW: two `SVF`s + `u_d` (≈ 212 B);
  - BITE: `ClosedFormAdaa2<DiodeAdaaLaw>` + `KeytrackedTilt` (≈ 80 B);
  - the `uDelay` alignment ring.
- **Global:** the SHAPE ramp (`ShapeRamp`: restartable 10 ms raised-cosine, C-33; was `OnePoleSmoother`, 10 ms); the engage ramp e (`LinearRamp`); `f0_` and its bit
  pattern; `linked_`; `peak_`; and **`std::array<float, 65> cBiteShape_`** over t₂ ∈ [0, 1], filled in `prepare()` by
  `diodeH1Coefficient(g_B(t₂))` (S4.1) and interpolated linearly at run time. `kLoudnessTableDb` is a constexpr
  member, not state.
- **`prepare`** prepares every regime member at F·fs, fills `cBiteShape_` (65 × 256 diode evaluations), resets, and
  re-keys every regime with the default `f0_`. Nothing in `processBlock` evaluates a Fourier integral or fills a table.

### S6.5 FR-034 contingency (the r ≡ 1 evidence fails)

- FR-034 makes ShapeMorph's lack of an observer conditional on SC-012(f) showing r ≡ 1 at every grid cell (C-3's
  ratio rule, kGrowl included).
- **If any (f) cell fails** (for example HOLLOW's 3·f₀ notch moving kGrowl's reading, or a crossfade between two
  regimes), ShapeMorph gains `[[nodiscard]] float perceivedPitchRatio() const noexcept`, computed from the SHAPE target
  as C-18 does for Character, and gated like FR-031 over the failing region. The plan returns to the user with the
  failing cells before that API is written, because its law (which r, over which SHAPE range, for which inputs) is a
  ruling.
- The same route applies to a failing Tube, Fold or Bite cell: their r ≡ 1 row in S5.7 would no longer hold, and the
  plan returns to the user with the cells.
- The default plan (no observer) stands only while every (f) cell passes.

---

## S7. Algorithm choices — why

| Choice | Alternative | Evidence |
|---|---|---|
| Residual-notched 4× round trip (S3) | Plain halfband round trip | C-11 table; Phase 2 S2.2 |
| ADAA2 in double for the piecewise laws (C-11) | ADAA1; 2×; float | −75.7/−73.8 → −86.5/−83.3 dBFS; 2× −44 to −61 |
| Tube at branch 8× (C-10) | Tube at 4× with +6 dB ceiling | SC-007 fails at +6 dB |
| Combined Fracture law (C-5) | Bias-flip clip | −69.5 vs −97.9 dBFS; growl never flips |
| Pitch-synchronous B-spline staircase (Crush) | `SampleRateReducer` | Harmonic by construction (r ≡ 1); −98.3 dBFS worst |
| First-order keytracked tilts (S6.1) | `SpectralTilt`, SVF shelves | C-13 |
| Linear SHAPE weights + static table | Equal-power | Regime outputs are coherent at h1; the table absorbs the rest (FR-056) |

---

## S8. RT safety, determinism, portability, denormals

- **RT (FR-060).**
  - Only `prepare` computes tables (≤ 5 × 65 × 256 evaluations: `BassCharacterStage`'s c_tube, c_bite, c_frac and
    c_fold, and `ShapeMorph`'s `cBiteShape_`) and configures reused processors.
  - **Setters are O(1):** they only sanitise and set smoother targets.
  - **Smoothed parameters drive per-sample coefficient updates, and that cost is measured.** During a 10 ms Amount,
    Tone or SHAPE ramp, and on every `f0PerSample` change during a Drop, the smoothed values change every sample. They
    drive transcendental work:
    - `KeytrackedTilt::setTone` (two 10^x and a sqrt);
    - `TubeStage::setInputGain` / `setOutputGain` (`dbToGain`, `tube_stage.h:171–184`);
    - the `BitCrusher` bit depth (`std::pow`, `bit_crusher.h:182–183`);
    - `OnePoleLP` / `SVF::setCutoff` (one exp or tan, `svf.h:204–212`);
    - ShapeMorph's g_B, k_R = 10·2^t and 10^{L/20}.

    The work is bounded (no allocation, lock or I/O) but not free, so SC-015 records a Drop cell and a ramp cell
    (S9.3). If either exceeds 2.0 %, the per-sample updates are quantised to a fixed control interval anchored to the
    absolute sample count (not to block boundaries, so SC-018's partition invariance still holds). SC-018 is then
    re-run and SC-015 re-measured.
  - No `std::function`, no `Oversampler`, no `LadderFilter`.
  - SC-016's `AllocationScope` covers `processBlock` (with and without f₀ pointer), every setter, `reset`, a switch,
    an engage and leaving bypass, for every algorithm and every Shape anchor. It also covers `Rectifier`'s
    `processBlock`, `setBlend` and `reset` (`Rectifier_NoAllocation`).
- **Determinism (FR-067).**
  - All decisions depend on sample values or setter calls, never block boundaries: f₀ bit-change detection, smoothers,
    crossfade counters, phasors.
  - `BitCrusher` dither is 0, so its RNG is never used. `setSeed(0x5F3759DFu ^ channel)` is still set at prepare so
    a future dither is seeded.
  - `reset()` restores phasors, holds, ADAA histories, tilts, paths, the switch machine (current = target, no
    pending) and the hysteresis.
  - prepare(48k) → prepare(96k) → prepare(48k) rebuilds everything (SC-018).
- **Non-finite (FR-066).**
  - Inputs are sanitised with `detail::isFinite` (bit test) before upsampling, so `TubeStage` and
    `WavefolderProcessor` (whose DC blockers would latch NaN) never see one.
  - `stateFinite()` aggregates the path, ADAA, tilt (`OnePoleLP` state via the probe), SVF, smoother and phasor state.
  - Non-finite setter arguments, and non-finite `f0PerSample` entries, keep the previous value (FR-066, FR-052;
    tested in SC-017, S9.3).
- **Portability (FR-071).**
  - Designated initialisers in brace init; no `std::isnan`.
  - No SIMD is added.
  - `std::bit_cast` for the f₀ change test, as in `harmonic_fold.h:204`.
  - ADAA2 runs in double, so `-ffast-math` contraction differences stay ≪ the ε-scaled terms. A cross-toolchain
    tolerance is used in the tests, never bit-exact goldens.
  - The nonfinite TUs go in the `-fno-fast-math` block.
  - `node tools/check-portability.js` and `tools/lint-apple-globals.js` run before commit.
- **Denormals.** FTZ/DAZ is the host precondition (HarmonicFold banner, `harmonic_fold.h:69–72`). Biquad, SVF,
  `OnePoleLP` and DCBlocker flush their own state; halfband sections do not.

---

## S9. Test plan (file · TEST_CASE · assertion strategy)

### S9.1 Files, targets, tags

| File | Target | Holds |
|---|---|---|
| `dsp/tests/unit/core/closed_form_adaa2_test.cpp` | `dsp_core_tests` | Law antiderivative identities (numeric derivative ≤ 1e-9); `DiodeAdaaLaw::f` ≡ `Asymmetric::diode` within 2 ulp (C-1); ADAA2 on a slow ramp equals f within 1e-6; fallback branches exercised |
| `dsp/tests/unit/processors/keytracked_tilt_test.cpp` | `dsp_processors_tests` | Unity at the pivot; flat at Tone 0.5; response vs n invariant over f₀ |
| `dsp/tests/unit/processors/oversampled_residual_path_test.cpp` | `dsp_processors_tests` | res = 0 ⇒ output = halfband round trip exactly; notch constants asserted literally ({0.50, 0.52, 0.55}, {1.5, 2.0, 1.5}; `HarmonicFold`'s are private, C-1); `kResidualDcHz == HarmonicFold::kDcCornerHz` (public); DC corner record |
| `dsp/tests/unit/processors/rectifier_test.cpp` | `dsp_processors_tests` | SC-014, `Rectifier_AliasingE0E2` `[long]` (SC-010 arm), `Rectifier_DcBound` (SC-011 arm), `Rectifier_RLawEvidence` (SC-012(c)), `Rectifier_BlendStepClickFree` (FR-005, SC-021 arm), `Rectifier_NoAllocation` (SC-016 arm), `Rectifier_PartitionInvariant` (SC-018 arm), `Rectifier_SampleRates` (SC-019 arm), constant equality with `OversampledResidualPath`'s public constants (C-1) |
| `dsp/tests/unit/processors/rectifier_nonfinite_test.cpp` | `dsp_processors_tests` (`-fno-fast-math`) | SC-017 (Rectifier arm: input burst, and NaN / ±Inf passed to `setBlend`) |
| `dsp/tests/unit/systems/shape_character_test_helpers.h` | — | S9.2 |
| `dsp/tests/unit/systems/shape_morph_test.cpp` | `dsp_systems_tests` | SC-003, SC-008 (Shape), SC-013(a) (Shape), lifecycle |
| `dsp/tests/unit/systems/shape_morph_spectral_test.cpp` | `dsp_systems_tests` | SC-001, SC-002 `[long]`, SC-004, SC-010 (Shape) `[long]`, SC-011 (Shape), `ShapeMorph_RIsOne`, `ShapeMorph_LoudnessTableHarvest` `[.calibration]` |
| `dsp/tests/unit/systems/bass_character_stage_test.cpp` | `dsp_systems_tests` | SC-008, SC-009, SC-013, SC-021 `[long]`, SC-026, SC-027, SC-029, `BassCharacter_RectifyLawMatchesRectifier` (C-19), `BassCharacter_CrushHoldRing` (S5.4) |
| `dsp/tests/unit/systems/bass_character_spectral_test.cpp` | `dsp_systems_tests` | SC-005, SC-006 `[long]`, SC-007, SC-010 `[long]`, SC-011, SC-022, SC-028 |
| `dsp/tests/unit/systems/bass_character_rlaw_test.cpp` | `dsp_systems_tests` | SC-012(a)(b)(d)(f)(g), SC-024 — `[long]`, sharded by fixture; `BassCharacter_RLawHarvest` `[.calibration]` |
| `dsp/tests/unit/systems/shape_character_chain_test.cpp` | `dsp_systems_tests` | SC-012(e) `[long]`, SC-019, SC-020, SC-025, SC-016, SC-018 |
| `dsp/tests/unit/systems/shape_character_nonfinite_test.cpp` | `dsp_systems_tests` (`-fno-fast-math`) | SC-017 |
| `dsp/tests/unit/systems/shape_character_perf_test.cpp` | `dsp_systems_tests` `[performance]` | SC-015 |

Tags are `[systems][profundum][shape-character]` (processors: `[processors][profundum][rectifier]`, and
`[processors][profundum][shape-character-parts]` for the `KeytrackedTilt` and `OversampledResidualPath` TUs; core:
`[core][profundum][closed-form-adaa2]`). `[long]` goes
only on multi-minute grids, never on the non-finite, bounded or state cases (root CLAUDE.md).

### S9.2 Shared helpers (`shape_character_test_helpers.h`, namespace `Krate::DSP::ShapeCharacterTest`)

- **`Descriptors describe(const float* y, double fs, double f0)`.**
  - w_n via `ProfundumTest::renderedHarmonicPowers`.
  - E_low, E_high, R_oe (numerator and denominator), THD, I (the `aliasedPower` mask applied to the total, on the
    `aliasFftLength` frame), C over 20 Hz…min(fs/2, 20 kHz) on the BH7 frame, and L via `rmsDbfs` on
    `integerPeriodLength`.
- **`Separation separate(a, b)`.** Implements OQ-S exactly: clamp at −60, count only if one side is > −40 dB re total
  (THD: re w₁). Returns max dB, which descriptor, and |ΔC|. Mirrors `separation()` in the model.
- **`levelMatch(y, refRms)`.**
- **Fixtures.** `fixture(controls, f0, fs, seconds)` = `HarmonicFoldTest::recipeAdditive`. `fold100(...)` =
  `HarmonicFoldTest::renderFold` at amount 100 with `setFundamentalHz(f0)`.
- **`ClickCount differentialClicks(transition, reference, fs, f0)`** (spec "Differential click count"):
  `ClickDetector` with its defaults, N = ⌊period/8⌋. A detection counts only if the reference render has none within
  ±N. The positive-control helpers (hard switch, unsmoothed step; for `Rectifier`, an unsmoothed blend step) are built
  through the probes (`BassCharacterStageProbe`, `ShapeMorphProbe`, `RectifierProbe`).
- **`ShsCandidates shsCandidates(const float* x, double fs, double f0)`.**
  - f̂ = `ProfundumTest::anchorShsF0`.
  - The saliences at f̂/2, f̂ and 2f̂ use the same S(f) definition, each peak-searched ±2 cents on the
    exact-DTFT score (the stage-2 code path).
  - `ambiguous` = best − second < 1 dB (C-2), and `readsF0` (C-3).
- **`d11Ratio(...)`** (D-11) and **`steadyDeltaMax(...)`** (Phase 2 SC-009 form).

### S9.3 Per criterion

| SC | TEST_CASE | Assertion strategy |
|---|---|---|
| SC-001 | `ShapeMorph_RegimesPairwiseDistinct` | SHAPE ∈ {0, 0.5, 1}, `kSawAnchor` and `kGrowl` at E1 and C2, 48 kHz. 6 s render, last 262144 samples, level-matched. `separate()` ≥ 3 dB or ≥ 0.25 octave for all 3 pairs. Model margin ×1.48 (saw HOLLOW–BITE 4.44 dB THD) |
| SC-002 | `ShapeMorph_SweepContinuity` `[long]` | 101 static positions × {E1, C2, C3} × four fixtures. Per-step \|ΔL\| ≤ 0.5 dB, \|ΔC\| ≤ 0.1 octave, \|ΔE_low\|, \|ΔE_high\| ≤ 1 dB on kSawAnchor. ±2.5 dB on saw, triangle and sine (C-6). kGrowl's swing is printed and recorded. FR-011: the probe's running-regime count is ≤ 2 at every sample of every render |
| SC-003 | `ShapeMorph_AutomationClickFree`, `_ClickCountPositiveControl` | Cases and harness per spec. (a) differential count = 0, with both controls flagged (probe: an unsmoothed SHAPE step and a regime hard-switch). (b) the transition's max \|Δy\| ≤ 1.1 × the steady max over the 101-point grid inside the traversed range. (c) `d11Ratio` ≤ 1 at engage and disengage; the warm path (C-22) is asserted through the probe (regime state nonzero and finite just before engage). Running-regime count ≤ 2 throughout (FR-011) |
| SC-004 | `ShapeMorph_HarmonicResponseTracksKeyboard` | \|H(n)\| = w_n(out)/w_n(in), n ≤ 16 where n·f₀ < 0.45·fs at both C1 and C3; \|ΔH\| ≤ 1.5 dB |
| SC-005 | `BassCharacter_AlgorithmsPairwiseDistinct`, `SeparationBar_FloorControl` | 15 pairs at Amount 1 / Tone 0.5, kSawAnchor and kSineAnchor, E1 and C2, level-matched. Model margins ×1.87 (saw, Tube–Bite 5.62 dB R_oe) and ×10.5 (sine). The floor control is literal to the spec (a −70 vs −130 dB tone is not distinct; −30 is) |
| SC-006 | `BassCharacter_ToneMonotonicCentroid` `[long]` | ε per cell = max \|ΔC\| over 8 sub-period start offsets × block sizes {1, 64, 512}; τ = 2ε, capped at 0.005 (a cell with ε > 0.0025 is reported). Every step and the cumulative reversal ≥ −τ on both fixtures. Span ≥ 0.25 octave on kSawAnchor; kSineAnchor span recorded (C-7) |
| SC-007 | `BassCharacter_AmountAudible` | Amount 0 → 1 at Tone 0.5, E1: THD, I or R_oe moves ≥ 3 dB under the floor, or C ≥ 0.25 octave. Model: weakest Tube (saw) 4.89 dB / 0.40 octave. 11-step trajectory printed |
| SC-008 | `BassCharacter_AmountZeroBitIdentical`, `ShapeMorph_DisabledBitIdentical` | `std::memcmp`-equivalent bit comparison from 100 ms after the last change (C-16), all algorithms, both channels |
| SC-009 | `BassCharacter_SwitchClickFree`, `_ClickCountPositiveControl` | 30 ordered pairs × {E1, C2}, switch mid-block. `d11Ratio` ≤ 1 with D measured on each branch alone (probe). Differential count 0. L_tr window within [min − 1, max + 1] dB |
| SC-010 | `BassCharacter_AliasingE0E2` `[long]`, `ShapeMorph_AliasingE0E2` `[long]`, `Rectifier_AliasingE0E2` `[long]`, `ShapeCharacter_AliasGatePositiveControl` | Grid per spec; `aliasedPower` ≤ −80 dBFS. Fracture is measured on the f₀/2 lattice (C-9) and the n·f₀ value is printed. Per cell: A_in, G (h1 power gain), A_added. Positive control: Bite through the probe with ADAA and the path disabled (base-rate naive law), raw kSawAnchor E2 / 44.1 kHz; model −48.0 dBFS > −80. `ShapeMorph_AliasingE0E2` runs SHAPE ∈ {0, 0.25, 0.5, 0.75, 1} × E0–E2 × {44.1, 48, 96} kHz (S10.3b, R-11) |
| SC-011 | `BassCharacter_DcBound`, `ShapeMorph_DcBound`, `Rectifier_DcBound` | Mean over `integerPeriodLength` frames after 1 s on the SC-010 grid at 48 kHz ≤ −80 dBFS |
| SC-012 | `BassCharacter_RLawMatchesReference`, `_RectifyFlipPoint`, `Rectifier_RLawEvidence`, `BassCharacter_RLawUpstreamChain`, `BassCharacter_RIsOne`, `ShapeMorph_RIsOne` | Grids per spec, with the C-2 ambiguity rule and C-3 gating (`readsF0` on the input render). (b) bisects Amount on the reference 0 dB salience crossing to 0.01, and measures [A_lo, A_hi] at ±1 dB. The observer's flip edges must lie inside every cell's band (C-4). Ambiguous-cell fraction ≤ 20 % and ≥ 8 unambiguous cells per (fixture, note), printed. (f) gates r_ref = f̂_out / f̂_in = 1 within 1 cent on **every** cell, all four fixtures raw and Fold-100, kGrowl included, with zero ambiguous cells (C-3); a failing cell triggers S6.5 |
| SC-013 | `BassCharacter_MonoSum`, `ShapeMorph_LREqual` | (a) bit-identical L == R. (b) ±0.15 offsets, E0–E2: band energy below 2·f₀ of (L + R)/2 within 1 dB of the L/R mean. (c) \|ΔTHD\| ≥ 1 dB or \|ΔC\| ≥ 0.05 octave, under the floor |
| SC-014 | `Rectifier_TransferLaw`, `Rectifier_ProcessedHarmonics` | (a) 4001-point grid × 5 blends vs the double law, ≤ 1e-6. (b) S4.3 analytic series ± the recorded DC-pole attenuation, within 0.1 dB where the analytic level is > −60 dB |
| SC-015 | `ShapeCharacter_CpuBudget` `[performance]` | **Unlinked** stereo (C-20): decorrelated L/R kSawAnchor (R offset by 0.37 period) and `setChannelOffsets(0.15, 0.15)` on Character; the probes assert `linked_ == false` on both stages before timing. The sweep covers all six algorithms × SHAPE ∈ {0, 0.25, 0.5, 0.75, 1} (30 cells; Amount 1, Tone 0.5, E1, 48 kHz, no crossfade), gates the **maximum** at ≤ 2.0 % (median of 3) and prints the argmax cell. Recorded, and returned to the user if > 2.0 %: the worst crossfade cell; (a) a 48-st / 60 ms Drop with `f0PerSample` at the argmax cell; (b) a 10 ms Amount + Tone + SHAPE ramp at the argmax cell; ShapeMorph disabled (warm, C-22). The linked figure is printed as information only. Run via `node tools/run-cpu-tests.js dsp_systems_tests`. Per-component figures printed |
| SC-016 | `ShapeCharacter_NoAllocation` | `AllocationScope` count == 0 over the S8 list, for every algorithm and every Shape anchor. The Rectifier arm (`Rectifier_NoAllocation`) covers `processBlock`, `setBlend` and `reset` |
| SC-017 | `ShapeCharacter_NonFiniteRecovery` | A 16-sample NaN / +Inf / −Inf burst; every output finite (bit test); `stateFinite()` within 1 block; descriptors back within 0.1 dB / 0.01 octave within 100 ms. **Setters (FR-066):** NaN, +Inf and −Inf passed to every setter of both stages (and to `Rectifier::setBlend`) leave the output bit-identical to an unchanged render. **f₀ feed (FR-052):** a NaN or Inf entry in `f0PerSample` leaves the output bit-identical to a render whose array holds the previous f₀ at that entry |
| SC-018 | `ShapeCharacter_PartitionInvariant` | Block sizes {1, 17, 32, 64, 512}, with and without the pointer; bit-identical, otherwise ≤ −120 dBFS residual printed. Two renders from `reset()` are bit-identical. The prepare 48k → 96k → 48k identity holds. Rectifier arm (`Rectifier_PartitionInvariant`): the same block sizes, and two renders from `reset()` bit-identical |
| SC-019 | `ShapeCharacter_SampleRates` | 44.1 / 48 / 88.2 / 96 / 192 kHz finite and ≤ 6, every algorithm and Shape anchor. At E1, C within 0.1 octave and L within 0.5 dB between 48 and 96 kHz. Rectifier arm (`Rectifier_SampleRates`): the five rates at blend {0, 0.5, 1}, finite and ≤ 6 |
| SC-020 | `ShapeCharacter_H1TransferRecord` | Prints the FR-040 table (`goertzelPhasor`, `integerPeriodLength`); "h1 absent" below −60 dB (expected for Fracture at Amount 1). Asserts `latencySamples() == 0` for all three |
| SC-021 | `ShapeCharacter_ParameterStepsClickFree` `[long]` | Every stage parameter (Amount, Tone, the offsets) as a step and a 20 ms ramp, plus the bypass edge (FR-054) with its hard-bypass positive control. **Blend** is a `Rectifier` control only (the stage derives b from Amount, S5.4), so its arm is `Rectifier_BlendStepClickFree` (rectifier_test.cpp): b 0 → 1 and 1 → 0 as steps and as 20 ms ramps, at E1 and C2 on kSawAnchor and kGrowl; differential click count 0, with an unsmoothed-blend positive control through `RectifierProbe` that must be flagged (FR-005) |
| SC-022 | `ShapeCharacter_OutputBounded` | \|out\| ≤ 6.0 on the spec inputs. `preCeilingPeak()` is printed (it records whether the clamp ever acted) |
| SC-024 | `BassCharacter_FractureRLawAndSidebands` | (a) per C-5: exact 1 at Amount 0, values in {1, 0.5}, non-increasing, 0.5 at Amount 1 (1200 cents); match gated on Fold-100 saw / sine / triangle; raw cells recorded. (b) odd-f₀/2 power ≥ −40 dB re total at Amount 1 (model 0.0) and ≤ −60 at Amount 0 |
| SC-025 | `ShapeCharacter_DropTracksHarmonicNumbers` | `ProfundumCore` with a 48-st / 60 ms Drop from E2 feeds the post-Drop f₀ array (its own staircase). (a) \|H(n)\| at 5 instants vs steady renders at those f₀, within 1.5 dB. (b) per-block `setFundamentalHz` at block 512 must exceed 1.5 dB for ShapeMorph or Crush. (c) Crush I ≤ steady + 3 dB; Fracture sidebands on the instantaneous f₀/2 lattice. (d) differential count 0. (e) null pointer ≡ constant array, bit-identical. Added cells: (a) also at Tone ∈ {0, 1} for every Character algorithm, where the keytracked tilts are not flat, so a stale pivot (S5.3 re-key rule) would show |
| SC-026 | `BassCharacter_RLawHysteresis` | Per C-4. Steps of 0.005 up and down; ±band/4 dither for 1000 steps; reset; offsets. **Crossfade (FR-030, D-8):** during a Rectify → Fracture crossfade (descriptor `true`, Amount past both flips), `perceivedPitchRatio()` equals Fracture's r from the `setAlgorithm` call onward; a queued request leaves r unchanged until it starts |
| SC-027 | `BassCharacter_DryShareNoComb` | The probe sets the Rectify law to identity. White noise at −12 dBFS RMS (seeded xorshift32). The residual vs `OversampledResidualPath`'s linear reference (C-14) ≤ −100 dBFS. The magnitude response (Welch, 2^14) is flat within 0.1 dB over 20 Hz–0.45·fs. The raw-dry control must show > 0.1 dB ripple |
| SC-028 | `BassCharacter_ToneStructure` | (a) on the probe's `KeytrackedTilt` objects (sine sweeps). (b) **through `BassCharacterStage::processBlock`**: for each algorithm, f₀ is set (C1, then C3) by `setFundamentalHz` and, in a second run, by a constant `f0PerSample` array that differs from the prepare default; then `setAlgorithm`, render, and read the running branch's tilt objects through the probe. Their response vs n at Tone ∈ {0, 0.25, 0.75, 1} differs between C1 and C3 by ≤ 1.5 dB. One case switches algorithm **after** `setFundamentalHz`, one leaves bypass after it, one calls `reset()` after it (S5.3 re-key rule). (c) Fold: output ÷ the measured tilt response at Tone 0 and 1 agrees in w₁…w₁₆ within 0.1 dB. (d) Tube and Bite, Amount 1: each output's w_n is divided by the post tilt's measured linear response at that Tone (measured on the probe's post-tilt object, as in (c)); the de-tilted THD or R_oe at Tone 0 vs 1 differ ≥ 3 dB; corroborated with the post tilt held at 0.5 through the probe. **Negative control:** pre tilt forced neutral through the probe, post active: the de-tilted difference is < 3 dB (C-8) |
| SC-029 | `BassCharacter_AlgorithmQueue` | ≥ 30 sampled triples; bit-identical to the reference schedule; probe instance count ≤ 2; equal-target no-op; SC-009 bounds; an API review line printed. Added orderings (C-21): during A → B, C then B (the request returns to the current target) ends at B with no further crossfade; C then A (the request returns to the outgoing algorithm) ends with B → A at the crossfade end. Each is bit-identical to its reference schedule |
| SC-023 | comply record | S11.2: the **full** `dsp_core_tests`, `dsp_processors_tests` and `dsp_systems_tests` (the timing-sensitive tags excluded; every `[long]` case runs in the long lane), each tail quoted; `git diff` of the reused headers empty; portability and tidy logs |

**Calibration harvests** (`[.calibration]`, hidden; printed constants are pasted into the headers and then gated by
the SCs above):
- `ShapeMorph_LoudnessTableHarvest` (T040): 101 points, mean of saw, triangle and sine (C-6) over E1, C2 and C3.
- `BassCharacter_RLawHarvest` (T030): Rectify and Fracture hysteresis edges at 5 Tones, from the real stage and the
  real estimator.

---

## S10. Model evidence (`shape-character-model.js`, measured 2026-10-15)

### S10.1 SHS salience (faithful stage-1 port, 48 kHz, 262144-sample Hann frame)

| Fixture | Note | Raw f̂/f₀ | Raw S(2f₀)/S(f₀) | Raw S(f₀/2)/S(f₀) | Full-wave f̂/f₀ | Full-wave S(2f₀)/S(f₀) |
|---|---|---|---|---|---|---|
| sine | E1 / C3 | 1.00 | −76.9 / −76.2 | −1.51 / −1.51 | 2.00 | +2.06 / +1.94 |
| triangle | E1 / C3 | 1.00 | — | −1.86 / −1.85 | 2.00 | +2.50 / +2.25 |
| saw | E1 / C3 | 1.00 | −6.17 / −5.86 | −3.54 / −3.63 | 1.00 | −22.2 / −21.4 |
| growl | E1 / C3 | **2.00** | **+3.43 / +4.07** | −8.11 / −8.19 | 1.00 | −2.54 / −2.24 |
| Fold-100 growl | E1 / C3 | **2.00** | +1.49 / +1.61 | −9.63 | — | — |

### S10.2 Fracture laws: salience 0 dB crossings (E1) and sidebands at Amount 1

| Law | sine | triangle | saw | growl | Sidebands @ A = 1 (dB re total) |
|---|---|---|---|---|---|
| Bias-flip clip (ratified text) | ≈ 0.27 | ≈ 0.32 | ≈ 0.65 | never (−2.2 dB @ 1) | −3.9 / −4.1 / −5.0 / −10.2 |
| Gating (x·(1 − dq)) | ≈ 0.33 | ≈ 0.38 | ≈ 0.67 | never (−2.0) | −4.8 / −4.8 / −3.5 / −3.0 |
| Polarity alternation | 0.11 | 0.15 | 0.37 | 0.62 | 0.0 (all on odd f₀/2) |
| **Combined (C-5)** | ≈ 0.13 | ≈ 0.17 | ≈ 0.38 | ≈ 0.64 | **0.0** |
| Combined on Fold-100 input, band at ±1 dB | [0.312, 0.449] | [0.326, 0.462] | [0.179, 0.342] | [0.498, 0.555] (reads 2·f₀) | — |

### S10.3 Aliased power, chosen design (4×, residual notches, ADAA2 ε 1e-5, Tube 8× +12 dB, Fold θ 2.5); worst over Tone {0, 0.5, 1} × {saw, growl} × {raw, Fold-100}

| fs | Note | Rectify | Tube | Fold | Bite | Crush | Fracture (f₀/2 lattice) |
|---|---|---|---|---|---|---|---|
| 44.1 | E0 | −104.6 | −99.1 | −115.3 | −106.6 | −116.6 | −134.8 |
| 44.1 | E1 | −88.0 | −85.5 | −87.0 | −90.8 | −107.1 | −107.5 |
| 44.1 | E2 | −86.5 | −85.3 | −85.7 | **−83.3** | −100.5 | −99.3 |
| 48 | E0 | −103.6 | −101.6 | −121.4 | −109.3 | −117.0 | −139.7 |
| 48 | E1 | −87.4 | −85.9 | −89.1 | −94.4 | −108.7 | −109.5 |
| 48 | E2 | −86.6 | −86.9 | −86.3 | −88.9 | −98.3 | −97.8 |
| 96 | E0 | −112.2 | −118.7 | −110.5 | −114.5 | −120.0 | −173.9 |
| 96 | E1 | −104.6 | −96.2 | −107.6 | −107.6 | −114.8 | −136.4 |
| 96 | E2 | −92.8 | −86.5 | −90.5 | −90.5 | −108.7 | −109.5 |

- Every worst cell is Fold-100 kGrowl except Bite at E0 (Fold-100 saw). The input's own residual is −97.9 dBFS at E2 /
  44.1 kHz.
- **Measurement sensitivity:** the same Tube cell reads −85.3 (FFT 2^16) or −89.1 (FFT 2^18). The C++ gate uses
  `aliasFftLength`; the margins above are the conservative reading.
- Design history (E2 / 44.1 kHz): see C-10, C-11, C-12.
- Naive base-rate positive control (Bite, raw saw, Tone 0.5, Amount 1): −48.0 dBFS.

### S10.3b ShapeMorph aliased power, BITE regime (measured 2026-10-15 in the plan review)

The model's `bite` branch with the BITE regime's settings: pre tilt at 4·f₀, ±12 dB (SHAPE 1) or ±6 dB (SHAPE 0.75);
drive −12 + 27·t₂ dB (+15 dB or +1.5 dB); no post tilt; ADAA2 ε 1e-5; the 4× residual path. Worst over the tilt sign ×
{saw, growl} × {raw, Fold-100}. Reproduce with nine cells running concurrently (37 s wall on 32 cores):
`ALGS=bite VARIANTS=resid PRE_K=4 PRE_DB=12 BITE_SPAN=27 PP_DB=0 node specs/profundum-phase5-shape-character/shape-character-model.js alias <note> <rate>`
(SHAPE 0.75: `PRE_DB=6 BITE_SPAN=13.5`).

| fs (kHz) | SHAPE 1, E0 / E1 / E2 | SHAPE 0.75, E0 / E1 / E2 |
|---|---|---|
| 44.1 | −110.6 / −99.9 / **−89.9** | −117.7 / −101.8 / −95.5 |
| 48 | −112.9 / −99.5 / −90.2 | −117.5 / −102.7 / −93.3 |
| 96 | −118.6 / −111.8 / −99.4 | −123.8 / −116.5 / −102.7 |

- Every worst cell is Fold-100 kGrowl at the positive emphasis. BITE's worst margin (−89.9 dBFS, 9.9 dB) is wider than
  Character Bite's (−83.3) because BITE's drive ceiling is 3 dB lower and it has no post tilt.
- The model applies BITE at full weight. At SHAPE 0.75 the real regime carries w_B = 0.5, so those cells are
  conservative.
- **Not modelled:** SHAPE 0, 0.25 and 0.5 (ROUND's `TanhADAA` at drive 0.8 and HOLLOW's linear SVFs; the only
  nonlinearity there is ROUND's mild ADAA1 tanh). They have no model figure. T020 harvests them in C++ before the
  SC-010 gate (R-11).

### S10.4 Audibility (static line spectra, 48 kHz, E1 = C2 since keytracked)

- **SC-005 weakest pair:** saw Tube–Bite 5.62 dB (R_oe), ×1.87; sine 31.5 dB (E_high).
- **SC-006 span on kSawAnchor (octave):** Tube 1.03, Fold 0.45, Rectify 0.87, Bite 0.86, Crush 0.89, Fracture 1.17.
  All are monotone.
- **SC-006 span on kSineAnchor (octave):** Tube 0.01, Fold 0.03, Rectify 0.08, Bite 0.06, Crush 0.25, Fracture 0.47.
- **SC-007 on saw:** Tube 4.89 dB / 0.40 octave, Fold 15.9 dB, Rectify 11.3 dB, Bite 6.27 dB, Crush 55.7 dB (I),
  Fracture 60 dB (I, floor-capped).

### S10.5 Rectify flip bands at ±1 dB: see C-4 (note-independent within 0.01 from E0 to C3)

### S10.6 ShapeMorph static model (E1, chosen constants S6.2)

| Fixture | Level after table, re own mean | Max per-step \|ΔL\| | Raw level at SHAPE 0 / 0.5 / 1 |
|---|---|---|---|
| saw | −1.60 / +0.37 dB | 0.20 dB | −1.63 / −0.99 / −5.83 |
| triangle | −0.16 / +0.55 dB | 0.18 dB | −1.08 / −0.72 / −2.97 |
| sine | −0.23 / +1.05 dB | 0.18 dB | −1.22 / −0.68 / −2.47 |
| growl (recorded, C-6) | −11.80 / +3.96 dB | — | −15.69 / +0.08 / −5.55 |

- Table seed (dB) at SHAPE 0 / 0.25 / 0.5 / 0.75 / 1: 1.31, 1.26, 0.80, 3.08, 3.76.
- SC-001 separations:
  - saw: ROUND–HOLLOW 10.18 dB (E_high), HOLLOW–BITE 4.44 dB (THD), ROUND–BITE 7.47 dB;
  - growl: ≥ 11.33 dB.

### S10.7 CPU (estimates; T001 measures before any header — R-1)

Phase 2 measured one 4× nonlinear stage at ≈ 0.5 % (excitation + Fold 1.019 % together), mono-linked. Its
stereo-forced chain read 2.462 % against 1.708 % linked (`specs/profundum-phase2-edge-fold/compliance.md:7`), so
forcing both channels multiplied the stage cost by ≈ 1.74. The table gives mono-linked estimates per component at
48 kHz. The gated SC-015 cell is **unlinked** (C-20):

| Component | Estimate |
|---|---|
| `OversampledResidualPath` (1 up, 2 down, 3 notches @2fs) | 0.15–0.20 % per stage |
| Shape worst position (HOLLOW + BITE: 2 SVF + tilt + ADAA2 diode in double) | 0.35–0.50 % |
| Character worst algorithm: Tube (8× `TubeStage` + branch halfband) | 0.45–0.70 % |
| Character worst algorithm: Bite (ADAA2 in double, one exp or log per F-sample) | 0.40–0.55 % |
| Total, Shape + Character, mono-linked (information only) | 1.2–1.6 % |
| **Total, Shape + Character, unlinked stereo (the SC-015 gated cell)** | **≈ 2.1–2.8 %** (× 1.74), against the 2.0 % bar: **over** until T001 measures (C-20, R-1) |

During a crossfade (two algorithms) the cost is recorded, not gated. The Drop and ramp cells (S8) and the disabled
ShapeMorph cell (C-22) are recorded the same way.

**Measured (T001 epoch 1, 2026-10-15, pinned, median of 3, unlinked; `evidence/run-log/T001-bench.txt`).** Base variant:
Tube 2.01 %, Fold 0.89, Rectify 0.78, Bite 1.16, Crush 0.66, Fracture 2.65; Shape 0 1.54, 0.25 1.74, 0.5 0.70, 0.75 1.32,
1 1.08; ramp 4.86, Drop 4.52; **Total 4.40 %** (Fracture + Shape 0.25). `ctrl16` 4.67 %, `ctrl16-rectfloat` 4.44 %: the
R-1 levers do nothing. The estimate above was wrong by ≈ 2×: Tube at 8× alone is 2.0 % unlinked, and Fracture's four
transcendental calls per F-sample were not costed. The user ruled (Clarifications "T001 CPU bench ruling"): gate the
mono-linked cell (Phase 2 precedent), record unlinked, add C-23 / C-24, re-bench (T001 epoch 2) before any header.
Linked is ≈ half of unlinked (≈ 2.2 % before C-23 / C-24).

**Measured (T001 epoch 2, 2026-10-15, pinned, median of 3, with C-23 and C-24 prototyped).** Linked: Tube 0.98 %, Fold
0.46, Rectify 0.38, Bite 0.51, Crush 0.38, Fracture 0.92; Shape 0 0.78, 0.25 0.77, 0.5 0.33, 0.75 0.62, 1 0.56; ramp 2.20,
Drop 1.74; **Total 1.76 % (Tube + Shape 0) — PROCEED** (gated cell per the user ruling; margin 0.24 %). Unlinked: Tube
1.96, Fracture 1.85, Shape 0.25 1.52, ramp 4.05, Drop 3.53, **Total 3.48 % recorded** for Phases 7 and 9. C-23 and C-24
are REQUIRED in T009–T015 as prototyped in `bench/shape-character-bench.cpp`.

---

## S11. Build integration

### S11.1 `dsp/tests/CMakeLists.txt`: enumerated lists, each with a Phase 5 comment block

- `dsp_core_tests`: `unit/core/closed_form_adaa2_test.cpp`.
- `dsp_processors_tests` (after the Phase 4 block, line ≈ 276):
  - `unit/processors/keytracked_tilt_test.cpp`
  - `unit/processors/oversampled_residual_path_test.cpp`
  - `unit/processors/rectifier_test.cpp`
  - `unit/processors/rectifier_nonfinite_test.cpp`
- `dsp_systems_tests` (after line ≈ 677):
  - `shape_morph_test.cpp`, `shape_morph_spectral_test.cpp`
  - `bass_character_stage_test.cpp`, `bass_character_spectral_test.cpp`, `bass_character_rlaw_test.cpp`
  - `shape_character_chain_test.cpp`, `shape_character_nonfinite_test.cpp`, `shape_character_perf_test.cpp`
- The `-fno-fast-math` block (line ≈ 1166) gets `unit/processors/rectifier_nonfinite_test.cpp` and
  `unit/systems/shape_character_nonfinite_test.cpp`, with a "Profundum Phase 5" comment. The perf TU stays out.

### S11.2 Commands (Windows, full CMake path) and parallelism

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
# ONE rebuild for all three touched executables (no shared header changes, FR-064):
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_core_tests dsp_processors_tests dsp_systems_tests
B=build/windows-x64-release/bin/Release
X="~[long]~[performance]~[perf]~[benchmark]~[!benchmark]"
# FULL suites (SC-023 names them whole), CONCURRENT (not timing-sensitive); capture logs, never re-run for output:
$B/dsp_core_tests.exe       "$X" > logs/p5_core.log 2>&1 &
$B/dsp_processors_tests.exe "$X" > logs/p5_proc.log 2>&1 &
$B/dsp_systems_tests.exe    "$X" > logs/p5_sys.log  2>&1 &
# [long] lane, also concurrent: one shard per [long] TEST_CASE. The list is GENERATED from each exe (Catch2 v3.4.0,
# root CMakeLists.txt:303), never hand-kept, so Rectifier_AliasingE0E2, BassCharacter_RectifyFlipPoint,
# BassCharacter_RIsOne, ShapeMorph_RIsOne and the per-fixture r-law cases cannot be missed:
for exe in dsp_core_tests dsp_processors_tests dsp_systems_tests; do
  while IFS= read -r t; do
    [ -n "$t" ] && $B/$exe.exe "$t" > "logs/p5_long_${exe}_${t//[^A-Za-z0-9]/_}.log" 2>&1 &
  done < <($B/$exe.exe --list-tests "[long]~[performance]~[perf]~[benchmark]~[!benchmark]" --verbosity quiet)
done; wait
# CPU lane ALONE, after everything else has finished and the machine has idled:
node tools/run-cpu-tests.js dsp_systems_tests
# gates: node tools/check-portability.js; node tools/lint-apple-globals.js;
# clang-tidy on the new TUs by file list (reconfigure build/windows-ninja first: the test CMake lists changed).
```

### S11.3 Expected runtimes (estimates; recorded after the first run)

- The SHS-based r-law grids cost ≈ 0.2 s per estimate (262144-sample stage-2 DTFT). Phase 5 runs ≈ 3000 estimates,
  ≈ 10 min single-threaded, sharded over ≥ 6 processes.
- The aliasing grids have ≈ 720 renders at ≤ 0.6 s each.
- The per-push lanes are now the full three suites. Their wall clocks are read from the Phase 4 close logs before the
  run and recorded after it; the long lane covers every `[long]` case of the three executables, not only Phase 5's.

---

## S12. Risks and mitigations

| ID | Risk | Mitigation |
|---|---|---|
| R-1 | CPU over 2 %: the unlinked SC-015 cell is estimated at ≈ 2.1–2.8 % (S10.7, C-20); double ADAA2, Tube 8× and the per-sample coefficient updates (S8) are the unknowns | T001 microbenchmark, unlinked, before headers. Levers in order: skip inactive regimes (planned); F2 caching (planned); control-interval quantisation of the smoothed coefficient updates (S8); float ADAA2 for the polynomial Rectify laws only if T001 shows double matters (re-measure SC-010). The **mono link is a lever for mono sources only** and counts for nothing in the budget. If the levers do not close the gap, the plan returns to the user with T001's table. Relaxing the bar, or gating a linked cell, is never the lever |
| R-2 | Aliasing margin at E2 / 44.1 kHz is 3.3 dB (Bite) and FFT-length-sensitive (S10.3) | C++ harvests the true values first (T020). A failure goes through FR-070 with Bite drive or ε evidence, never by loosening the bar |
| R-3 | ADAA2 ill-conditioning: ε = 1e-7 reads −77.0 at E0 / 96 kHz | ε = 1e-5 (C-11); `closed_form_adaa2_test` exercises both fallback branches |
| R-4 | Static-model figures (S10.4–S10.6) ignore oversampling, ADAA delay, smoothing and window effects | Every bar is gated in C++; the harvests fix the tables |
| R-5 | SC-009 level window under the −4.5 dB law with incoherent pairs | The one-period L_tr smooths the midpoint. If it fails, the crossfade length is re-measured (D-7 allows it) before the law changes |
| R-6 | Rectify / Fracture hysteresis intersections empty at some Tone or note in C++ | C-4 / C-5 return to the user with the harvest table |
| R-7 | Denormals in halfband sections during silence | FTZ/DAZ precondition; the tests run with `dsp_test_main.cpp` FTZ (as Phase 2) |
| R-8 | MSVC vs Clang drift in double ADAA2 under fast-math | Tolerance-based assertions; no bit-exact goldens; the nonfinite TUs are IEEE-only |
| R-9 | `TubeStage` / `WavefolderProcessor` DC blockers at 2·F·fs: R clamps at 0.9999 above ≈ 628 kHz (10 Hz corner) | At 192 kHz base with 2 × 2 = 4× the Tube rate is 768 kHz: corner 12.2 Hz. Recorded (SC-019 only bounds) |
| R-10 | kGrowl is octave-ambiguous for every r test | C-3 gating with the counts printed; Phase 9 owns the core-level FOLLOW consequence |
| R-11 | ShapeMorph aliasing: BITE runs a +12 dB pre-emphasis at 4·f₀ into the diode at up to +15 dB; ROUND and the crossfaded positions have no model figure (S10.3b) | BITE modelled at −89.9 dBFS worst (E2 / 44.1 kHz, 9.9 dB margin). T020 harvests `ShapeMorph_AliasingE0E2` at all five SHAPE positions in C++ before the SC-010 gate. A failure goes through FR-070 with BITE drive or emphasis evidence, never by loosening the bar |
| R-12 | A SC-012(f) cell fails under C-3's ratio rule (for example HOLLOW's notch on kGrowl) | S6.5: the plan returns to the user before any observer API is written |

---

## S13. Handoff records (comply writes them into the roadmap)

- **Phase 9:**
  - the core's partial phases after a Drop differ from a fresh note's (waveform up to 1.75 apart on growl at the same phase; Clarifications "Group 9 rulings, round 6"): listening item for every nonlinear stage; a possible Phase 4 amendment;
  - the FR-040 h1 table and the C-15 group delays;
  - kGrowl reads 2·f₀ raw and Fold-100 (C-3);
  - Fracture's r is stated for Fold-output inputs (C-5);
  - the ShapeMorph growl level swing (C-6);
  - the crossfade cost cell.
- **Phase 7:** `setChannelOffsets` semantics; the ±0.15 measurements (SC-013(b)(c), SC-012(g)).
- **Phase 8:** SHAPE, Amount, Tone and the algorithm are the morphable fields; algorithm transitions go only through
  `setAlgorithm` (FR-057).

---

## S14. Implementation order (input to tasks.md)

1. **T001** CPU microbenchmark (`specs/profundum-phase5-shape-character/bench/`): path, ADAA2 in double vs float,
   Tube 8×, HOLLOW + BITE, all **unlinked stereo** (C-20), plus a per-sample coefficient-update cell (S8). Decide R-1
   before headers; if the unlinked figure stays > 2.0 % after the levers, return to the user.
2. `closed_form_adaa2.h` + test (L0).
3. `keytracked_tilt.h`, `oversampled_residual_path.h` + tests (L2).
4. `rectifier.h` + tests: SC-014, SC-010 / 011 Rectifier arms, SC-012(c).
5. `BassCharacterStage` skeleton: lifecycle, bypass, mono link, switch machine (SC-008, SC-029).
6. Algorithms in order Rectify → Bite → Tube → Fold → Crush → Fracture, each with its SC-010 / 011 cells as it lands.
7. **T020** C++ aliasing harvest (R-2): the Character algorithms now, and the five ShapeMorph positions once
   `ShapeMorph` lands in step 9 (R-11). **T030** r-law harvest → hysteresis tables (C-4 / C-5 confirmation).
8. Remaining Character SCs (005–007, 009, 012, 013, 021, 022, 024, 026–028).
9. `ShapeMorph`. **T040** loudness harvest; SC-001–004, 010, 011, 013.
10. Chain TU: SC-012(e), 016–020, 025.
11. Perf lane (SC-015); portability; tidy; comply.

---

## Open questions for the user

1. **C-2:** ambiguity rule at 1 dB with octave candidates (recommended), or 3 dB with an argmax-based re-wording?
2. **C-3:** gate r cells on inputs that read f₀, recording kGrowl (recommended), or another rule?
3. **C-5:** adopt the combined Fracture law, with the observer stated for Fold-output inputs (recommended), or a
   voice-supplied descriptor, or keep the bias-flip law (fails SC-010)?
4. **C-6:** SC-002 ±2.5 dB on saw, triangle and sine with growl recorded (recommended), or a presence-indexed 2-D table?
5. **C-7:** SC-006 span gated on kSawAnchor with the sine span recorded (recommended), or steep second-order tilts?
6. **C-10:** Tube at branch 8× (recommended) vs replacing `TubeStage`.
7. **C-19:** re-word FR-021's Rectify row to "composes `Rectifier`'s laws" (recommended), or run a real `Rectifier`
   with a nested round trip?
8. **C-20:** gate SC-015 on the unlinked stereo cell (recommended; estimated over the bar until T001 measures), or keep
   Phase 2's practice of gating the linked cell and recording stereo?
9. Ratify C-1, C-4, C-8, C-9, C-11 to C-18, C-21 and C-22 as written.

---

## Review notes (plan review, 2026-10-15)

All 21 review issues are accepted. Where a resolution differs from the suggestion, the reason is below.

- **SC-028(b) Tone set.** The review suggested running SC-028(b) at Tone ∈ {0, 1}. The plan uses the spec's own set
  {0, 0.25, 0.75, 1} (a superset of {0, 1}) through `processBlock`. SC-025(a) gets the suggested Tone ∈ {0, 1} cells.
- **SC-028(d).** Both suggested mechanisms are used: de-tilting by the measured post response is the gate, and holding
  the post tilt neutral corroborates it. The negative control keeps the post tilt active, because with both tilts
  neutral Tone 0 and Tone 1 render identically and the control would pass trivially.
- **S10.3 ShapeMorph rows.** The model has no ShapeMorph aliasing renderer. The BITE regime (SHAPE 0.75 and 1, the
  only positions with a strong nonlinearity) was measured with the model's `bite` branch at BITE's settings
  (S10.3b). SHAPE 0, 0.25 and 0.5 (ROUND's mild ADAA1 tanh, HOLLOW linear) are not modelled; R-11 and T020 cover them
  in C++ before the gate. No modelled figure was invented for them.
- **SC-015 unlinked (C-20).** Accepted in full. It turns the estimate from 1.2–1.6 % into ≈ 2.1–2.8 %, over the bar.
  It is flagged ⚠ rather than hidden, and the bar is not relaxed.
- **SC-003(c) (C-22).** Of the two options the review offered, the plan picks the warm path: it keeps D-11's premise
  without re-ruling SC-003(c).
- **Crush ring (S5.4).** Both suggested fixes are combined: the bound is restated (spacing ≥ 4·m_eff/(m_eff + 1) ≥ 2.75),
  and the ring is grown to 3 slots with a defined (unreachable) full policy. Engines keyed by the parity of m remove
  the state hand-off at integer crossings, which neither suggestion covered.
- **Per-sample coefficient updates (S8).** Recorded SC-015 cells were chosen instead of quantising up front. The
  quantisation is the fallback if a recorded cell exceeds 2.0 %.
