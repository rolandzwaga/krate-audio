# Implementation Plan: Profundum Phase 2 — Edge Excitation & Harmonic Fold

**Spec:** `specs/profundum-phase2-edge-fold/spec.md` (Clarified 2026-10-10)
**Roadmap:** `specs/Profundum-roadmap.md` Part A, Phase 2 (lines 258–283)
**Phase base:** commit `47e43918` (Phase 1 complete)
**Status:** Planned 2026-10-10; revised the same day after review round 2 (see Review notes). **Section S1 holds eleven
items that need ratification before `tasks.md`.** Five of them (C-1, C-2, C-3, C-9 and C-10) change a spec clause and are
flagged ⚠ RE-RULE. C-11 changes a plan constant (`kFoldIndexMax` 2.7 → 3.0) because the 2.7 calibration fails SC-006's
mid-grid centroid clause.

Every number in this plan was **measured this session** by one of three tools:

| Artifact | What it is | Reproduce |
|---|---|---|
| `edge-fold-model.js` | Double-precision model of the plan design. It contains the polyphase oversampler, the split bypass, both static maps, BH7 aliased power with the D-6 FFT rule, and the Phase 1 recipe spectra via `../profundum-phase1-harmonic-core/recipe-model.js` | `node specs/profundum-phase2-edge-fold/edge-fold-model.js [phase\|exc\|fold\|alias [note] [rate]]` |
| `halfband-design.js` | Valenzuela–Constantinides polyphase IIR halfband coefficient design, with numeric verification | `node specs/profundum-phase2-edge-fold/halfband-design.js` |
| `bench/*.cpp` | Standalone C++ microbenchmarks against the real `dsp/include` headers | Compile line at the top of each file; run through `tools/pin-perf-cores.ps1` |

The model's environment overrides (`OS_KIND=econ`, `PRE_KIND`, `NOTCHES`, `SPLIT_HZ`, `DRIVE_MAX_DB`, `G_CONT`, `THETA_MAX`,
`STEEP`) reproduce every rejected alternative in S1 and S9. The model's `THETA_MAX` default is now 3.0 (C-11); `THETA_MAX=2.7`
reproduces the round-1 figures.

---

## S0. Verification ledger

Every reused signature below was read this session. Paths are relative to `dsp/include/krate/dsp/` unless stated.

| Component | File:line | Fact relied on |
|---|---|---|
| `ProfundumCore` | `systems/profundum_core.h:40–702` | Banner :5–6 says "Includes Layer 0 and Layer 2 only" (S5.1 updates it). FTZ/DAZ precondition note :37–38 (class-level, covers the core's own state only). Class at :40. `RetriggerPhase` :42; `kControlInterval = 32` :45; `kPitchUpdateInterval = 16` :46; `kMinF0Hz = 8` :47; `kBankHeadroom = 4` :62. `prepare(double, int)` :77–95 is documented NOT RT-safe :75–76; it calls `initialiseState` :94. `reset()` :99–104 calls `bank_.reset()` and `initialiseState(pendingControls_)`. `setControls` :108–128 uses `sanitizeField` :633–635: non-finite keeps the previous value, else clamp, **no denormal flush**. `noteOn` :143–148. `setPartialPanOffsets` :153–170. `processBlock(float*, float*, std::size_t, const float* = nullptr)` :172–221: run loop :191–210, `bank_.processStereoBlock(left + i, right + i, run)` :206, headroom rescale **after** the loop with the aliased `right == left` guard :212–217. `stateFinite()` :238–256 checks only the bank, the Phase 1 per-partial arrays, the pan offsets and the f₀ scalars (S5.6 extends it). `controlUpdate` step 1 latch :314–319. `applyNoteOn` Reset branch :386–411, FreeRunning branch :412–442. `panAppliedNonZero_` :687. `sameBits` :628–630. Header comment :11 says "no heap allocation: every buffer is a fixed-size member" |
| `SpectralShapeRecipe` | `processors/spectral_shape_recipe.h` (as quoted by the spec, :41–48, :51–53) | `Controls{depth, body, bodyCurvature, bodyEmphasis, edge, shift}`, `kCoreOutputRmsDb = -12`, `kPowerTarget`. Named coordinates used in tests: `kSineAnchor`, `kTriangleAnchor` (edge 0), `kSawAnchor` (edge 0), `kGrowl` (edge 1), `kDefaultControls`. Their values are cross-checked against `specs/profundum-phase1-harmonic-core/recipe-model.js:131–132` |
| `Oversampler<F, C>` | `primitives/oversampler.h` | `prepare` allocates :287–293, :472–479. `process(…, const StereoCallback&)` :322–327 and `std::function` callback :236. Silent no-op when `numSamples > maxBlockSize_` :843–845. Economy uses `BiquadCascade<4>` Butterworth at `0.45 × base fs` in every 2× stage :491–528. Zero-stuff gain ×2, then filter :594–627. Downsample: filter, then keep the even samples :641–671. `latencySamples_ = 0` for IIR :448–450 (nominal; the measured group delay is in S1 C-1). **Not composed** (C-1) |
| `Biquad` | `primitives/biquad.h` | TDF2 `process` :352–369: **a non-finite input resets the state and returns 0** :354–357; flushes both states :365–366. `processBlock` :374–378. `configure(FilterType, float freq, float Q, float gainDb, float sampleRate)` :330. RBJ Lowpass :685–692, Notch :712–718. `clampFrequency` caps at `0.495 × rate` :88, :155–165. `kMinQ = 0.1`, `kMaxQ = 30` :50–53. `getZ1()`/`getZ2()` :391–394. Copyable plain class :396–400 |
| `BiquadCascade<N>` | `primitives/biquad.h:414–475` | `setButterworth` with `butterworthQ(i, N)` :427–436, :208–219. Used only by the model's Economy reference |
| `OnePoleAllpass` | `primitives/one_pole_allpass.h:82–200` | `y = a·x + x₁ − a·y₁` :139–158, with a per-call NaN/Inf reset and two `flushDenormal` calls. `setCoefficient` clamps to ±0.9999 :113–116. No deleted copy members, so it is copyable. **Same recurrence** as the polyphase sections of S3. Not included there because of layer discipline (C-1(b)) |
| `DCBlocker` | `primitives/dc_blocker.h` | `prepare(double, float cutoffHz)` :135–147. `process`: `y = x − x₁ + R·y₁` :190–205. **Cutoff clamped to [1, fs/4] and R clamped to [0.9, 0.9999]** :227–237, so the effective corner is ≥ 1e-4·fs/(2π): 3.06 Hz at 192 kHz |
| `OnePoleSmoother` | `primitives/smoother.h` | `kDefaultSmoothingTimeMs = 5` :52. `kCompletionThreshold = 1e-4` :55. Coefficient `exp(−5000/(ms·fs))` :77–92. `configure(ms, fs)` :160–164. `setTarget` treats NaN as 0 :170–181. `process()` **snaps exactly to target** within 1e-4 :197–211. `snapToTarget()` :257 |
| `Sigmoid::recipSqrt` | `core/sigmoid.h:188–194` | `x / std::sqrt(x*x + 1.0f)`, with an `isInf` guard |
| `Sigmoid::Asymmetric::withBias` | `core/sigmoid.h:391–394` | `func(x + bias)`. The caller must DC-block, :375–388 |
| `WavefoldMath::sineFold` | `core/wavefold_math.h:366–381` | NaN propagates. Returns `x` when `gain < kSineFoldGainEpsilon (0.001)`, else `sin(gain·x)` |
| `TanhADAA` | `primitives/tanh_adaa.h:165, 244–259` | `kEpsilon = 1e-5` midpoint `fastTanh` fallback, and a naive first sample after reset. **Not composed** (S6) |
| `FastMath::fastTanh` | `core/fast_math.h:65–89` | Returns exactly ±1 for \|x\| ≥ 3.5 (E-5). **Not composed** |
| `detail::isFinite`, `flushDenormal` | `core/db_utils.h:118`, `:263` | Fast-math-immune finite test; denormal flush |
| Phase 1 test helpers | `dsp/tests/unit/systems/profundum_core_test_helpers.h` | Includes `<krate/dsp/systems/profundum_core.h>` :32 (Layer 3) and `../processors/spectral_shape_recipe_test_helpers.h` :24, so a processors-layer TU must not include it (S8.1). Namespace :47. `requireLREqual` :61. `renderCore` :90–124. `goertzelPhase` :157. `bh7PowerSpectrum` :206–233 (double FFT). `renderedHarmonicPowers` :235–249 (Hann main-lobe sums of a `kLowFrequencyFftSize = 262144` frame). `aliasFftLength` :257–263. `aliasedPower` :273–305 |
| Shared test helpers | `tests/test_helpers/` | `render_fingerprint.h:73` `fingerprintRender`, `:122` `compareFingerprints`. `allocation_detector.h:111` `AllocationScope`. `enable_ftz_daz.h:26`. `low_frequency_metrics.h:64` `kLowFrequencyFftSize`, `:478` `calculateCorrelation` |
| Test registration | `dsp/tests/CMakeLists.txt` | Primitives list :87–100; the shared main does FTZ/DAZ setup (:89). Processors Profundum entry :253–254. Systems Profundum block :564–597. `-fno-fast-math` block ends :1056–1078; a processors nonfinite TU sits in that block :1035 |
| Processors-layer test helper | `dsp/tests/unit/processors/spectral_shape_recipe_test_helpers.h` | Includes only `core/db_utils.h` and `processors/spectral_shape_recipe.h` (:23–24). `struct Descriptors` :77 (`centroidOct` = Σ log₂(n)·wₙ / Σ wₙ), `describePowers` :92. Namespace `Krate::DSP::ProfundumTest` :35 |
| CPU runner | `tools/run-cpu-tests.js:56`, `tools/pin-perf-cores.ps1` | `FILTER = '[performance],[perf],[.perf],[benchmark],[!benchmark],[long]~[vorago-sweep]'`: it selects every `[long]` case as well as the perf cases (S10.4). The pin script pins to the highest efficiency-class cores (mask `0xFFFF` on this machine) |
| Layer rule | `dsp/CLAUDE.md:8–16` | Layer 1 may include **core only**; Layer 2 may include core and primitives |

---

## S1. Spec conflicts and refinements — RATIFY BEFORE TASKS

**RATIFIED 2026-10-10:** the user accepted C-1 (proposal (a), inline allpass (b-i)), C-2, C-3, C-5, C-6, C-7, C-8,
C-9, C-10 (three-part clause; mid/side Fold declined) and C-11 as written below, and ruled **C-4 as per-channel
detectors** (the recommendation, not the as-written shared detector). Recorded in `spec.md` → Clarifications →
"Session 2026-10-10 — plan S1 ratification". The amended wording governs; no alternative below is open.

### C-1 ⚠ RE-RULE (FR-041, FR-048, D-1): `Oversampler` is unfit. Replace it with a polyphase IIR halfband oversampler

**Evidence.** Measured this session. The model reproduces the exact `oversampler.h` and `biquad.h` filter code; the
microbenchmarks link the real headers.

| Finding | Economy `Oversampler` | Plan design (S3) |
|---|---|---|
| **CPU, one stereo stage, 48 kHz, oversampling filters only** | `<4,2>` 2.23–2.28 %; `<2,2>` 0.61–0.67 % (`bench/economy-oversampler-bench.cpp`, 3 runs) | 4× 0.40–0.48 %; 2× 0.21–0.23 % (halfband from `OnePoleAllpass`) |
| **CPU, both stages complete, stereo, pinned P-cores** | not viable: one stage at 4× with pre-filter is 2.69–2.94 % | **1.37–1.45 %** (`bench/candidate-design-bench.cpp`) against the 1.5 % budget |
| **Aliasing, max excitation, E2/48 kHz, SC-009 state** | 4× + ADAA −67.5 dBFS; no pre-filter variant passes. The weak band is 0.5–0.55·fs: −8.7 dB at fs/2 and −15.6 dB at 0.55·fs (model S9.1) | **−95.2 dBFS** (S9.4) |
| **Stopband at 0.55·fs** | −15.6 dB (4× first-stage filter) | −106.6 dB (outer halfband) |
| **Low-frequency group delay** | 3.00 (2×) / 6.47 (4×) base samples. h1 phase at E2: −4.36° at 44.1 kHz, 4× (S9.1). **Fails SC-012(a) ±2° and SC-026 lag 0** | ≈ 1.9 base samples per direction. h1 itself never passes the oversampled path (S2 split bypass), so SC-012(a) and SC-026 hold (S9.3) |

The clarify premise for Q1 ("4× oversampling for both stages is affordable") is **false for the shared `Oversampler`**.
One 4× stereo stage alone exceeds the whole 1.5 % budget.

**Proposal (a), recommended:**
- Add a new Layer 1 primitive `primitives/polyphase_halfband_oversampler.h` (S3). It is a per-channel, copyable, heap-free 2×/4×
  cascade of polyphase allpass halfbands with a template hook at the 2fs stage.
- Amend FR-041 from "reuses the existing `Oversampler`" to "reuses `Biquad`, `DCBlocker`, `OnePoleSmoother`, `Sigmoid` and
  `WavefoldMath`, with oversampling by the new `PolyphaseHalfbandOversampler` (L1)".
- Amend FR-048 so that Phase 2 adds two headers (`harmonic_fold.h` and `polyphase_halfband_oversampler.h`) and edits one
  (`profundum_core.h`).
- Side effect: **D-1's heap relaxation becomes unnecessary.** Nothing allocates, not even in `prepare`, so Phase 1's
  "no heap allocation anywhere" rule (`profundum_core.h:11`) survives unchanged. That is stronger than FR-041.
- FR-008, FR-027, SC-026 and D-2 are unchanged. No FIR is used, and the header names no `Standard`, `High` or `LinearPhase`.

**(b) Layer discipline inside the new primitive.**
- `dsp/CLAUDE.md:8–16` forbids an L1 → L1 include. The repo has precedent for breaking it (`oversampler.h → biquad.h`,
  `ladder_filter.h → oversampler.h`, `two_pole_lp.h → biquad.h`), but this phase's constraints say "includes point downward only".
- So the primitive implements the first-order section `y = a·(x − y₁) + x₁` inline, with only `core/` includes. It does not
  include `one_pole_allpass.h`.
- This is the same recurrence as `OnePoleAllpass::process` (`one_pole_allpass.h:139–158`). Dropping that function's per-call
  NaN reset is also a correctness requirement: a reset in one polyphase branch alone desynchronises the pair.
- Ratify (b-i) inline, as planned, or (b-ii) include `one_pole_allpass.h` under the existing L1 → L1 precedent.

**Alternative (c):** place the class inside `harmonic_fold.h` (FR-048's escape hatch) and have `profundum_core.h` include it.
That needs no new file, but it still contradicts FR-041's "reuses the existing `Oversampler`", and it makes the Fold header
the owner of a generic primitive that Phases 5 and 6 will also want.

### C-2 ⚠ RE-RULE (SC-012(b), SC-013(b)): an asymmetric transfer rotates h1 on multi-harmonic inputs. This is physics, not design

**Cause.** The core renders all partials in sine phase 0, so x(−t) = −x(t). An odd transfer keeps the output odd in time:
h1 stays sine-phase, so ∠T₁ ∈ {0°, 180°}. A bias adds even-order terms, and an even-order term turns the partial
pair (h1, h2) into a **quadrature h1**: sin θ · sin 2θ = ½(cos θ − cos 3θ). Every asymmetric static map does this,
`withBias` included.

**Measured on the static map alone, no filters, C2.** Asymmetry 0 gives exactly 0.00° in every case.

| Input | asym 0.5, drive 1 | asym 1, drive 1 |
|---|---|---|
| `kSawAnchor` | −7.73° | −14.88° |
| mid grid | −4.83° | −10.10° |
| SC-009 state | −60.26° | −74.99° (h1 +17.2 dB: an intermodulation-born fundamental) |

The full rendered design reads −15.04° on the saw at E2/44.1 kHz (S9.3). The filters add ≤ 0.13°.

SC-013(b) has the same root cause. With the pan vector of SC-013(b), L and R carry different even/odd mixes, so the
inter-channel h1 difference at asymmetry 1 is 6.22° (saw) and 9.36° (mid). The power clause still passes: cos²(4.7°) costs
0.03 dB.

**Proposal:**
- **SC-012(b):** ±2° at asymmetry 0 on both inputs, and at every setting on `kSineAnchor`. For `kSawAnchor` at asymmetry > 0,
  |∠T₁| ≤ **20°**: cos 20° = 0.94, ≤ 0.54 dB at a phase-locked Anchor join. Measured maximum 15.04°.
- **SC-013(b):** inter-channel h1 phase ≤ 2° at asymmetry 0 and ≤ **15°** at asymmetry 1 (measured 9.36°). The 0.5 dB
  mono-sum power clause is unchanged.
- Add a recorded figure, SC-022(e): ∠T₁ at max asymmetry for the five SC-005 inputs, so Phase 3 sees the Anchor-join cost.
- Alternative: keep ±2° by making asymmetry act on h1 alone (bias shaped only on a low-passed copy), which needs a new
  even-harmonic generator that the roadmap does not describe. Not recommended.

### C-3 ⚠ RE-RULE (SC-024(b)): the periodicity residual cannot see in-cycle gain modulation, so its self-check fails by construction

In steady state, any time-invariant system driven by a periodic input produces a periodic output. **The reference
`x/env(x)` with a 1 ms release is also exactly periodic.** It therefore reads near the float floor (≈ −130 dB), not
"> −30 dB", and the gate's own teeth assertion fails whatever the Fold does. In-cycle gain modulation of a periodic input is
periodic waveshaping, and RMS(y[k+P] − y[k]) is blind to it.

**Proposal:** keep (b)'s residual clause (the design passes it) and drop its teeth claim. Add (b′):
- The Fold exposes the read-only `detectorLevel()` (S4.1).
- The test renders 1-sample blocks, which is legal by SC-017, and records `detectorLevel()` over one steady period at
  f₀ ∈ {20, 40, 80} Hz, at the compression and folding midpoints, before and after the step.
- Gate: in-cycle ripple (max − min)/mean ≤ **1e-4**. The design is a constant windowed peak (S4.4): the ripple is exactly 0
  up to float rounding.
- Teeth: an `x/env(x)` reference with a 1 ms release, in `double` in the same test, must show ripple ≥ **0.1**.

### C-4 (FR-033, D-9, SC-013(c)) — the shared detector passes, narrowly, and narrows the image at high Fold

The spec assumes per-channel normalisation would collapse a 6 dB L/R ratio toward 0 dB. For an **envelope-normalised and
rescaled** map (`F(G·x)/G`) the reverse holds: per-channel normalisation is homogeneous per channel, so it preserves the
ratio exactly. The shared detector drives the quieter channel less hard and changes the ratio.

Measured on the static map (S9.6). The input ratio is 6.02 dB and the result is identical at −24 / −12 / −6 dBFS:

| Amount | Shared (FR-033) | Per-channel |
|---|---|---|
| 15 (soft, no normalisation) | 6.01 / 5.91 / 5.61 dB | same |
| **47.5 (the SC-013(c) test point)** | **5.04 dB** (bar 6.02 ± 1 → passes with 0.02 dB margin) | 6.02 dB |
| 82.5 (fold midpoint) | **1.51 dB** at `kFoldIndexMax` 3.0 (1.76 dB at 2.7; **0.53 dB** under the D-1 ease-out law, S4.3): the image collapses toward the centre | 6.02 dB |

**The step clause (review round 2).** SC-013(c) also requires the L/R ratio on 4-period frames hopped by one period to stay
inside the pre/post steady band ± 0.1 dB through the −24 → −6 dBFS step. `evidence/sc013step.js` models the exact fixture: E1,
amount 47.5, R 6 dB below L, 1 s holds, frames of round(4·fs/f₀) samples, the S4.4 detector in the time domain with
`setFundamentalHz(E1)`. Results:

| Detector attack | Steady pre / post | Frame-ratio range through the step | Worst excursion outside the band |
|---|---|---|---|
| 2 ms (round-1 plan) | 5.039 / 5.039 dB | 4.486 … 5.039 dB | **0.453 dB: FAILS** (0.467 at 44.1 kHz) |
| 0.5 ms | 5.039 / 5.039 | 4.886 … 5.039 | 0.053 dB: fails |
| **Instant (env := raw when raw > env), planned** | 5.039 / 5.039 | 5.024 … 5.039 | **0.000 dB: passes** |
| Per-channel detectors, 2 ms (C-4 recommendation) | 6.021 / 6.021 | 6.021 … 6.021 | 0.000 dB |

- Cause: with a 2 ms attack, G = P/env overshoots by up to 18 dB for the first milliseconds after the step. Both channels
  are then overdriven, and harder drive compresses the shared-detector ratio (the 47.5 → 82.5 trend above).
- **Fix adopted in S4.4: `kDetectorAttackMs = 0`, meaning instant attack.** The windowed peak `raw` is a running maximum of
  |x|, so it is continuous and rises no faster than the waveform. With env = raw on a rising segment, G·|x| never exceeds P
  and G stays continuous, so no step reaches the output. FR-037 only requires a documented `static constexpr` attack
  constant, which 0 satisfies. In steady state env = raw = the windowed peak, as before, so C-3's ripple stays exactly 0.
- The per-channel alternative is exact at every amount and level (C-4 recommendation below).

- **The plan implements FR-033 as written (shared).**
- Recommendation for ratification: switch to per-channel detectors with identical constants. The image is then preserved
  exactly at every amount and level, which serves D-9's intent ("image does not wander") better than the letter.
- If ratified: re-rule SC-013(c) to "L/R output ratio within ±0.1 dB of the input ratio at amounts {47.5, 82.5, 100}, both
  levels, through the step". The code change is one line in S4.4: detect per channel instead of on max(|L|, |R|).
- If not ratified, the shared detector passes the steady clause by 0.02 dB and the step clause by the instant attack. The
  steady margin stays the thin one (R-4).

### C-5 (refinement, no bar change) — zero latency against IIR group delay is resolved by the split bypass (S2)

**D-2 note (2026-10-11, build):** the split bypass delays the band above `kSplitHz` by ≈ 5 base samples (4.40 at 500 Hz,
5.35 at 1 kHz, 4.95 at 4 kHz); the full-band cross-correlation therefore reads −4 on the mid grid. SC-026's excitation
arm was re-scoped in the main loop to h1 / low-band lag 0 with the high-band delay recorded ≤ 8 samples (spec SC-026,
Clarifications "Session 2026-10-11"). The design is unchanged.
**D-3 note (2026-10-11, build):** the crossover is a 2nd-order Butterworth, so "everything below `kSplitHz` on a zero-delay
path" holds only well below it: the linear path LP·(1 − Dos) + Dos delays 131 Hz by 0.37, 196 Hz by 0.83, 262 Hz by 1.48
and 458 Hz by 3.65 base samples, with a +1.5 dB magnitude hump at 400–700 Hz at mix 1 (modelled on `evidence/lib.js`).
SC-026(b)'s low band is therefore the 125 Hz 4th-order low-pass (model peak −0.02 samples at E1 and C2); the 250 Hz
figure is recorded. The hump is a property of the split no bar covers; it is noted for Phase 9 listening.
**User review (2026-10-11):** Split-hump characterisation (user instruction 2026-10-11; investigate now, tonal decision deferred to Phase 9; no EQ, no split change). **Origin: the split's recombination, not the nonlinearity.** `evidence/splithump.js` runs the plan model's excitation stage with the waveshaper replaced by identity and reproduces the hump: +1.24 (mix 1) to +1.60 dB (mix 0.25) peaking at 500–550 Hz at 44.1/48 kHz, +0.66 to +0.80 dB at 96 kHz; the closed form H = LP·(1 − Dos) + Dos (LP the 500 Hz 2nd-order split, Dos the halfband round trip of ≈ 4.3 samples) gives +1.57 dB (48 kHz) / +0.83 dB (96 kHz) at 500–550 Hz. It is a linear transfer: independent of pitch; full strength at any mix above the 0.1 engage ramp (+0.74 dB at mix 0.05); drive enters only through the nonlinear path's linear gain g. **On the real core** (`ProfundumCore_ExcitationRecordedSplitHump`, SC-022(j), 48 cells): `kSawAnchor` at 48 kHz, mix 1 shows the linear hump alone, +1.69 dB (drive −12 dB), +1.53 (0 dB), +0.31 (+12 dB) at 523 Hz; on the mid grid the nonlinear path's generated harmonics dominate the same band (E1 h8 330 Hz +6.58 / +13.22 dB at drive 0 / +12 dB, mix 1; C2 h7 458 Hz +8.22 / +17.92 dB), with h1 within −0.2 dB everywhere. **Reproduce:** `node specs/profundum-phase2-edge-fold/evidence/splithump.js` (three sections, ≈ 20 s) and `dsp_systems_tests "ProfundumCore_ExcitationRecordedSplitHump"`.

- Any causal anti-aliasing filter delays h1. The Economy chain needs 3–6.5 samples; the halfband needs ≈ 1.9 per direction.
- The plan keeps h1 (everything below `kSplitHz = 500 Hz`) on a **zero-delay linear path**; only the nonlinear residual and
  the high band travel through the oversampled path.
- Measured in the rendered design (S9.3):
  - excitation at drive 0, mix 1: |T₁| ≤ 0.06 dB and ∠T₁ ≤ 0.13° at E1, E2 and C2, 44.1 kHz;
  - Fold at amount 100 (`kFoldIndexMax` 3.0): ∠T₁ ≤ 1.16° on the saw, 1.06° on the mid grid and 0.04° on the sine at 44.1 kHz;
    ≤ 1.58° (saw) at 96 kHz (`evidence/validate.js 44100|48000|96000`).
- **Why a split and not a full-band bypass.** A full-band bypass (c·x zero-delay plus a delayed residual) comb-filters the
  upper partials. The max-excitation output then peaked at 3.34 instead of being clipped. The split design peaks at 0.56–0.64
  for the same inputs.

### C-6 (refinement) — inputs where the spec leaves them open

| SC | Input chosen |
|---|---|
| SC-012(a) | `kSineAnchor`, mid grid and `kSawAnchor` |
| SC-026 (excitation) | `kSineAnchor` and mid grid |
| SC-008 (a)(b)(c) | The trailing "Inputs:" line applies to all three clauses |
| SC-005 positive control | **The SC-009 state** at the fixture's own maximum effective drive (D_eff = 3.98, edge 1). The model reads −48.0 dBFS there. `kSawAnchor` has edge 0, so it only reaches D_eff = 1.0 and reads −133 dBFS, which has no teeth |

### C-7 (refinement) — the excitation drive range is −12…+12 dB, not wider

- `excitationDriveLaw(d) = 10^((−12 + 24·d)/20)` ∈ [0.2512, 3.981]. With the Edge floor, D_eff ∈ [0.0631, 3.981]
  (argument units: the calibration peak maps to D_eff).
- +24 dB was tried first: the SC-009 state then drives the transfer's argument to ≈ 90, and 4× + ADAA aliases at −80.8 dBFS
  even with the residual pre-filter (S9.4 history).
- At +12 dB every bar keeps margin (S9.2): SC-001 spans 44.4 dB (Edge 0) and 31.4 dB (Edge 1); SC-002(b) rises 20.7 dB;
  SC-003(a) generates −17.6 dB; SC-003(b) gives −19.2 dB; the SC-020 worst case is −0.95 dB.

### C-8 (record, no gate) — Fold level on non-sine inputs

- The Fold's makeup is **peak-preserving** (S4.3): output peak equals input peak in steady state for any waveform.
- Level therefore changes only with crest factor. At amount 100 with `kFoldIndexMax` 3.0 (C-11): saw +1.70, mid grid +2.71 and
  SC-009 state +6.51 dB (static, C2; `edge-fold-model.js fold`). At 2.7 the figures were +3.2 / +3.7 / +6.5 dB.
- RMS makeup was tried and rejected: the saw rose +7 dB at 100.
- No spec bar covers this. The figure is added as recorded SC-022(f), for Phase 6's Mass law, which drives `setAmount`.

### C-9 ⚠ RE-RULE (SC-002(c) mix clause, FR-004): a split-bypass stage cannot be an exact blend between mix 0 and mix > 0

**Cause.** At mix 0 the stage is idle (S2.1), so y(0) = x bit-identically (SC-004(a) requires that). At any m ≥
`kExcitationSplitEngageMix` the engage weight is w = 1, so the high band above 500 Hz travels through the oversampler and the
2 Hz DC blocker. Every S5.4 term is affine in m once w = 1. So y(0.5) − ½(y(0) + y(1)) = ½(D_c(hi) − hi), where D_c is the
halfband round trip (≈ 3.8 samples) plus the DC blocker's phase. That residual is set by the structure, not by Edge.

**Measured** (`evidence/mixlin.js`: full S5.4 stage, `kSineAnchor`, drive 0.5, asymmetry 0, steady 16-period window):

| fs | E1, Edge 0 / 1 | C2, Edge 0 / 1 |
|---|---|---|
| 44.1 kHz | −58.4 / −58.4 dB | −57.8 / −57.8 dB |
| 48 kHz | −57.5 / −57.5 | −60.4 / −60.4 |
| 96 kHz | −53.6 / −53.6 | −60.5 / −60.5 |

As written, the clause fails at 44.1 and 96 kHz and passes at 48 kHz/C2 by 0.4 dB. Edge 0 and Edge 1 read identically,
which is the property the clause exists to prove ("mix is a plain blend, unscaled by Edge").

**Design alternative, rejected.** Delay-matching the linear high band at every m, including m = 0, makes the stage an exact
blend. However, it delays the mix-0 output and breaks SC-004(a) bit-identity, FR-006 and the idle CPU saving.

**Proposal.** Evaluate the blend between non-zero mixes at or above `kExcitationSplitEngageMix`:
- at `edge` ∈ {0, 1}, drive 0.5, y(0.55) matches ½(y(0.1) + y(1)) with residual ≤ **−60 dB** re the output power (bar
  unchanged);
- measured in double: −300.9 to −311.0 dB in every cell above, so in float the floor is rounding;
- "mix unscaled by Edge" is the Edge-0 vs Edge-1 comparison of that same residual (both must pass).

The mix 0 → 0.1 segment, where w ramps, stays covered by SC-001's strict mix monotonicity, SC-009's mix 0 ↔ 0.05 click
gate and SC-004(a). The as-written residual is recorded as SC-022(g).

### C-10 ⚠ RE-RULE (SC-013(c) first clause): the Fold at 100 cannot keep the input's h1 phase and L/R balance on two different waveforms

SC-013(c) asks that the Fold at amount 100, fed the SC-013(b) panned core output at max excitation, keep the inter-channel h1
phase difference ≤ 2° and the h1 power of (L+R)/2 within 0.5 dB of the mean of the L and R h1 powers. Its input already
carries 6.22° (saw) and 8.94–9.46° (mid) at asymmetry 1 (C-2).

**Measured** (`evidence/foldpan.js`, `evidence/foldpan2.js`: full rendered design per channel, 48 kHz, shared detector
env = max(peak L, peak R), `kFoldIndexMax` 3.0; the 2.7 figures are in brackets):

| Note, input, Fold source | Inter-channel h1 in → out | Added by the Fold | h1 L/R imbalance out | Mono clause as written | (L+R)/2 re coherent sum |
|---|---|---|---|---|---|
| E1 mid, exc max asym 0 | −0.52° → −16.25° | −15.74° [−5.59°] | −22.34 dB [−12.71] | −2.42 dB [−1.44] | −0.023 dB |
| E1 mid, exc max asym 1 | 8.94° → 10.51° | 1.57° [0.00°] | −17.01 dB | −1.97 dB [−1.08] | −0.016 dB |
| E1 saw, exc max asym 1 | 6.22° → 22.25° | 16.03° [10.59°] | 2.86 dB | −0.27 dB | −0.160 dB |
| C2 mid, exc max asym 0 | 0.13° → −19.64° | −19.77° [−6.75°] | −21.69 dB | −2.39 dB [−1.41] | −0.036 dB |
| C2 saw, exc max asym 1 | 6.22° → 19.97° | 13.75° [9.07°] | 2.79 dB | −0.24 dB | −0.129 dB |
| E1 mid, **core only (mix 0)** | 0.00° → −1.95° | −1.95° [−1.09°] | −19.59 dB | −2.19 dB [−1.26] | −0.000 dB |
| C2 mid, core only (mix 0) | 0.00° → 0.46° | 0.46° [0.27°] | −19.55 dB | −2.19 dB | −0.000 dB |
| C2 saw, core only (mix 0) | 0.00° → −0.05° | −0.05° | 2.80 dB | −0.11 dB | −0.000 dB |

Findings:
- **No h1 cancellation anywhere.** (L+R)/2 loses at most 0.16 dB against the coherent sum ((|h1L| + |h1R|)/2)².
- **The as-written mono clause fails because of L/R imbalance, not cancellation.** At amount 100 the fold's h1 gain depends
  strongly on waveform shape (|T₁| at 100 is −9.4 dB on the sine and +2.5 dB on the saw, S9.3). The pan vector gives L and R
  different shapes, so their h1 levels differ by up to 22 dB. For an imbalance r, the clause reads 10·log₁₀((1+r)²/(2(1+r²)))
  even when the phases are equal.
- **The phase clause fails even at asymmetry 0 and even with per-channel detectors** (foldpan2.js: per-channel moves every
  figure by ≤ 0.15°). The excitation output is no longer sine-phase (its high band is delayed), and heavy folding turns that
  into an intermodulation-born h1 rotation.
- Suggestion (b) of the review, defining the input at asymmetry 0, therefore does not rescue the clause: −15.74° / −2.42 dB.

**Proposal for SC-013(c) first clause:**
- (c-i) On the panned core output at **mix 0** (the Fold's own contribution, no excitation), the Fold at 100 adds ≤ **2°**
  to the input's inter-channel h1 difference. Measured worst 1.95° at 3.0 (E1 mid), which is a 0.05° margin; it was 1.09° at
  2.7. That margin is R-11.
- (c-ii) At max excitation, asymmetry 0 and 1, E1 and C2, mid and saw: the h1 power of (L+R)/2 is within **0.5 dB** of the
  coherent sum ((|h1L| + |h1R|)/2)², which is the cancellation measure FR-052 asks for. Measured worst 0.16 dB.
- (c-iii) Record the inter-channel h1 phase and the L/R h1 imbalance at max excitation as SC-022(h). Phase 3's Anchor join
  sees it.

**Alternative, not modelled: a mid/side Fold.** Fold M = (L+R)/2 and pass S = (L−R)/2 linearly through c·S. With the
SC-013(b) pan vector, h1 sits only in M, so L and R h1 would match exactly and the as-written clause would pass. The cost is
that every harmonic the Fold generates becomes mono, and FR-033's "identical arithmetic per channel" reading would change. It
also roughly halves the Fold's oversampled cost. It is a sound-design decision for the main loop, not a plan default.

### C-11 (constant change, no bar change): `kFoldIndexMax` 2.7 fails SC-006's mid-grid centroid clause; 3.0 passes

SC-006 requires the core's mid grid at C2 to rise ≥ 0.5 octave in centroid C from amount 0 to 100. The round-1 plan gave no
figure for it. `evidence/foldmid.js` evaluates the steady Fold on the mid grid: G = (1 − β) + β·P/peak, one-period 16384-point
FFT, C over n·f₀ < fs/2.

| `kFoldIndexMax` | C(0) | C(100) | Rise (bar 0.5 oct) | Cal sine: worst ΔL = ΔCF (bar 0.25) | Min \|T₁\| (bar −12) | Chain alias E2/44.1 (bar −80) |
|---|---|---|---|---|---|---|
| 2.7 (round 1) | 1.270 | 1.572 | **0.302: FAILS** | 0.153 dB | −3.59 dB | −87.9 dBFS |
| 2.9 | 1.270 | 1.756 | 0.486: fails | — | — | — |
| 2.95 | 1.270 | 1.811 | 0.541 | — | — | — |
| **3.0 (adopted)** | 1.270 | 1.870 | **0.600** | **0.191 dB** | **−9.46 dB** | **−84.8 dBFS** |

- C falls through the soft and compression regimes (1.270 → 1.218 at 65): saturation lifts h1 relative to the body. Only the
  fold regime raises it, and the mid grid's peak-normalised waveform spends most of each cycle below the calibration peak.
- At 3.0 every other Fold bar still holds, re-measured this round: SC-006 THD strictly increasing (min step +0.079 dB at 65),
  H(100) = 17 ≥ 4; SC-008(a) on the sine 0.191 dB; on the mid grid |ΔL| = |ΔCF| 0.081 dB, |ΔC| 0.033 oct; aliasing worst
  −84.8 dBFS (S9.4); the Fold's ∠T₁ at 100 ≤ 1.58° on the saw, mid grid and sine at 44.1/48/96 kHz (S9.3); pre-clamp peak ≤ 2.40 (S5.5).
- Cost: the chain aliasing margin falls from 7.9 to 4.8 dB, and SC-013(c)'s figures worsen (C-10). Both are recorded in R-11.
- The mid-grid THD is not monotonic in amount (foldmid.js): SC-006 gates THD monotonicity on the calibration sine only.

---

## S2. Architecture

```
ProfundumCore::processBlock (L3)                              HarmonicFold::processBlock (L2)
 bank run (≤16) → ×kBankHeadroom ─┐                            x (L,R) → sanitise ─┬→ detector (max|L|,|R|) → G
                                  ▼                                                │
              ┌──────── EXCITATION STAGE (per channel) ───────┐   ┌──────── FOLD STAGE (per channel) ─────────┐
 x ──┬──► LPb(500 Hz) ─► base = c·(lo + (1−w)(x−lo)) ──┐      │   │ same structure, Φ = Φ_fold, c = c_f(a),   │
     │                                                  (+)─► clamp ─► out    w = w(a), DC 2 Hz, clamp kOutputCeiling │
     └──► U (halfband ↑F) ─► Φ(u) − c·(LPo(u) + (1−w)(u − LPo(u)))  │   └────────────────────────────────────────────┘
                              ─► ↓ (inner halfband) ─► 3 notches @2fs ─► ↓ (outer halfband) ─► DC 2 Hz ─┘
```

### S2.1 The generic stage (both components)

Notation:
- x[n] is the base-rate input of one channel; F ∈ {2, 4} is the oversampling factor.
- U is the polyphase upsampler (F outputs per input sample), D the matching downsampler, and N the notch hook at the 2fs
  stage (S3.3).
- LPb and LPo are the 2nd-order Butterworth low-pass at `kSplitHz` (500 Hz), designed with `Biquad::configure(Lowpass, 500, 0.70710678, 0, rate)`
  at fs and at F·fs respectively. DC is a `DCBlocker` at 2 Hz.
- Φ is the stage's static map, evaluated per oversampled sample; c is its h1 coefficient on the calibration sine; w ∈ [0, 1]
  is the split-engage weight.

```
lo   = LPb(x[n])
base = c · (lo + (1 − w)·(x[n] − lo))                     // zero-delay linear path
u[i] = U(x[n])_i,  i = 0…F−1
r[i] = Φ(u[i]) − c · (LPo(u[i]) + (1 − w)·(u[i] − LPo(u[i])))   // nonlinear residual + (w·)high band
y[n] = clamp(base + DC(D(r; N)), ±ceiling)
```

**Linear analysis.** At a component of frequency f, with H_c(f) the U·D chain response (≈ e^{−j2πfτ}, τ ≈ 2–4 samples) and
Φ linearised as c:

```
y(f) ≈ c · [ H_lo(f) + (1 − w)·H_hi(f) + w·H_hi(f)·H_c(f) ]     with H_hi = 1 − H_lo
```

- The low band (h1) is zero-delay.
- At w = 1 the high band is delayed **coherently with the nonlinear products**, so there is no comb.
- The h1 error term is c·w·H_hi(f₁)·(H_c(f₁) − 1). Its magnitude is ≤ √2·(f₁/500)·2π·f₁·τ/fs ≈ 0.011 rad (0.6°) at
  E2/44.1 kHz with τ = 4.

**Engage weight.** At w = 0 the stage is a full-band bypass. That is exact and continuous when Φ ≈ c·u, i.e. near the
transparent end:
- Fold: w = smoothstep(a / `kSplitEngageAmount` = 10).
- Excitation: w = smoothstep(m / `kExcitationSplitEngageMix` = 0.1).

This keeps SC-008(b) continuity at a = 0 → 0.05 and SC-009's engage clean. The high-band delay appears gradually, while the
residual is still small.

**Idle.**
- Fold: when the smoothed amount is exactly 0 and the target is 0 (`OnePoleSmoother::process` snaps, smoother.h:197–211), the
  output is the input, bit-exact.
- Excitation: when the smoothed mix is exactly 0 and the target is 0, the oversampled path is skipped entirely.
  This gives FR-006 / SC-004(a) bit-identity and costs no CPU at mix 0.
- Consequence: y(0) carries a zero-delay high band and y(m ≥ 0.1) a delayed one, so mix is an exact blend only between
  non-zero mixes (C-9).
- **Engaging from idle resets the residual-path state** (oversampler, LPo, notches, DC, ADAA history). That keeps the
  render independent of stale state (SC-017).

### S2.2 Why each piece exists (evidence in S9)

| Piece | Without it |
|---|---|
| Split bypass | SC-026 lag ≠ 0 and SC-012(a) fails (C-1); a full-band bypass comb-colours the upper partials (C-5) |
| Residual notch pre-filter at 2fs: {0.50, 0.52, 0.55}·fs, Q {1.5, 2.0, 1.5} | Halfband alone: −59.5 dBFS (excitation) and −53.0 (Fold) at E2/48 kHz. The halfband is −3 dB at fs/2 and passes the 0.5–0.55·fs residual into the band |
| 4× (not 2×) for the excitation | 2× floors at −73.3 to −76.5 dBFS whatever the pre-filter: aliasing inside the oversampled domain |
| 4× for the Fold | At θmax 2.7: at 2× the chain at E2/44.1 kHz reads −82.3 (2.3 dB margin); at 4×, −87.9. At 3.0 (C-11) the 4× chain reads −84.8; the 2× variant was not re-run |
| ADAA1 in the excitation | 4×, Economy, no pre-filter: −56.8 → −67.5 dBFS with ADAA. Closed form, so no ill-conditioning (S5.4) |
| 3rd notch | At θmax 2.7: two notches, chain at E2/44.1 kHz reads −82.6 (2.6 dB margin); three: −87.9. At 3.0 three notches read −84.8; two were not re-run |

---

## S3. `PolyphaseHalfbandOversampler` (Layer 1, NEW — pending C-1)

### S3.1 Header, includes, API

`dsp/include/krate/dsp/primitives/polyphase_halfband_oversampler.h`. Includes `core/db_utils.h` (L0) and `<array>`,
`<cstddef>`, `<algorithm>` only.

```cpp
namespace Krate::DSP {

/// One channel of a 2x/4x polyphase IIR halfband oversampler (Valenzuela-Constantinides). Zero
/// look-ahead (IIR, no FIR, no latency compensation); low-frequency group delay about 1.9 base-rate
/// samples per direction (measured in its test). Heap-free, copyable (copying continues the state),
/// all methods noexcept and allocation-free. Precondition: FTZ/DAZ on (sections are not flushed).
class PolyphaseHalfbandOversampler {
public:
    static constexpr std::size_t kMaxFactor = 4;
    /// Outer stage (fs <-> 2fs): N = 8, transition 0.05 of the 2fs rate (halfband-design.js).
    static constexpr std::array<float, 8> kOuterCoefficients{
        0.0358327884f, 0.134090142f, 0.272040143f, 0.424324871f,
        0.572057197f, 0.706292142f, 0.827124762f, 0.941503094f};
    /// Inner stage (2fs <-> 4fs): N = 4, transition 0.25 of the 4fs rate.
    static constexpr std::array<float, 4> kInnerCoefficients{
        0.0424547099f, 0.170739850f, 0.393319893f, 0.745713589f};

    void setFactor(std::size_t factor) noexcept;          // 2 or 4 (anything else -> 4); resets state
    [[nodiscard]] std::size_t factor() const noexcept;
    void reset() noexcept;                                 // zero all section state
    /// Writes factor() samples at the oversampled rate.
    void upsample(float x, float* out) noexcept;
    /// Reads factor() samples; `atTwoX(float) -> float` is applied to each 2fs-rate sample
    /// before the outer decimation (for F = 2 that is the oversampled rate itself).
    template <class AtTwoX>
    [[nodiscard]] float downsample(const float* in, AtTwoX&& atTwoX) noexcept;
    [[nodiscard]] float downsample(const float* in) noexcept;   // identity hook
    [[nodiscard]] bool stateFinite() const noexcept;

private:
    struct Section { float a = 0.0f, x1 = 0.0f, y1 = 0.0f;
        float process(float x) noexcept { const float y = a * (x - y1) + x1; x1 = x; y1 = y; return y; } };
    template <std::size_t N> struct Stage2x {          // N/2 sections per branch, up and down separate
        std::array<Section, N / 2> upA, upB, dnA, dnB;
        // up:   outA = x through upA (coefs c0,c2,..), outB = x through upB (c1,c3,..); emit {outA, outB}
        // down: a = in[1] through dnA, b = in[0] through dnB; return 0.5f * (a + b)
    };
    Stage2x<8> outer_;
    Stage2x<4> inner_;
    std::size_t factor_ = 4;
};
}
```

### S3.2 Coefficients and verification

- Designed by `halfband-design.js`: `design(8, 0.05)` and `design(4, 0.25)`. The implementer pastes the printed 9-digit values.
  The section form `y = a(x − y₁) + x₁` is that script's `AP.p` exactly.
- Measured responses (model; the C++ test re-measures them):
  - outer: passband flat to 0.44·fs, −3.01 dB at fs/2 (power complementary), **≥ 106.6 dB** stopband from 0.55·fs;
  - inner: **≥ 116.8 dB** from 1.5·fs (0.375 of the 4fs rate), with a flat passband to 0.5·fs.
- Branch assignment: even-index coefficients form branch A, odd-index branch B. The downsampler feeds in[1] to A and in[0]
  to B. That is the order that produced the verified responses (`halfband-design.js` `Up2`/`Down2`).

### S3.3 4× composition

- `upsample`: the outer stage turns x into {p, q}; the inner stage turns p into out[0..1] and q into out[2..3].
- `downsample`: a = inner.down(in[0], in[1]), b = inner.down(in[2], in[3]); then a = hook(a), b = hook(b); return
  outer.down(a, b).
- For F = 2: a = hook(in[0]), b = hook(in[1]), outer.down(a, b).
- The hook is where both stages run their three notches (S2.2). Notch frequencies are set relative to the base rate
  (0.50 / 0.52 / 0.55·fs) at the 2fs rate, which is below the `0.495·rate` clamp (biquad.h:88).

**Factor table (FR-045):** F = 4 for fs ≤ 100 kHz, F = 2 above. The gated rates (44.1, 48 and 96 kHz) all run at 4×.
2× at 96 kHz was measured and rejected: 3.6 and 2.8 dB of margin (S9.4). 192 kHz only needs a finite, bounded output
(SC-016); its aliasing figure is recorded (SC-022(b)).

---

## S4. `HarmonicFold` (Layer 2, NEW)

### S4.1 Header, includes, public API

`dsp/include/krate/dsp/processors/harmonic_fold.h`. It includes only L0 and L1:
- `core/db_utils.h`, `core/sigmoid.h`, `core/wavefold_math.h`;
- `primitives/biquad.h`, `primitives/dc_blocker.h`, `primitives/smoother.h`, `primitives/polyphase_halfband_oversampler.h`.

```cpp
namespace Krate::DSP {
/// @note Precondition: the owner runs processBlock with FTZ/DAZ on (ScopedDenormalMode on x86, the default
///       FPCR on AArch64), mirroring profundum_core.h:37-38. The halfband sections, split/notch biquads and
///       DC blocker are not flushed. The detector env is flushed explicitly (S4.4), so silence cannot stall it.
class HarmonicFold {
public:
    // Regimes (D-12, FR-022)
    static constexpr float kSoftEnd = 30.0f;
    static constexpr float kCompressionEnd = 65.0f;
    static constexpr float kNormalisationRampEnd = 40.0f;       // beta: 0 at kSoftEnd -> 1 here (FR-036)
    // Static map (S4.3; calibrated by edge-fold-model.js)
    static constexpr float kCalibrationPeak = 0.3552343859f;     // sqrt(2)·10^(-12/20), the core's -12 dBFS RMS sine peak
    static constexpr float kSoftDriveAtSoftEnd = 0.43f;          // g(kSoftEnd)
    static constexpr float kCompressionDriveAtEnd = 1.20f;       // g(kCompressionEnd); g keeps its geometric law to 100
    static constexpr float kFoldIndexMax = 3.0f;                 // theta(100), rad at the calibration peak (C-11)
    // Structure (S2)
    static constexpr float kSplitHz = 500.0f;
    static constexpr float kSplitEngageAmount = 10.0f;
    static constexpr float kDcCornerHz = 2.0f;                   // <= 96 kHz: exact (DCBlocker R clamp, S0)
    static constexpr float kAmountSmoothMs = 10.0f;
    static constexpr float kOutputCeiling = 6.0f;
    // Detector (FR-037, S4.4)
    static constexpr float kDetectorHoldMs = 125.0f;             // one period of the core's 8 Hz floor
    static constexpr std::size_t kDetectorSegments = 8;
    static constexpr float kDetectorAttackMs = 0.0f;             // 0 = instant: env := raw on a rising raw (C-4 step clause)
    static constexpr float kDetectorReleasePeriods = 4.0f;
    static constexpr float kDefaultFundamentalHz = 20.6017223f;  // E0 (FR-037 "default sized for E0")
    static constexpr float kMinFundamentalHz = 8.0f;
    static constexpr float kMaxFundamentalHz = 20000.0f;
    static constexpr float kDetectorFloor = 1.0e-6f;
    static constexpr std::size_t kTablePoints = 401;             // amount grid, step 0.25

    // control thread (allocation-free; prepare computes the tables, S4.3)
    void prepare(double sampleRate, std::size_t maxBlockSize) noexcept;  // maxBlockSize unused: per-sample core
    void reset() noexcept;
    // audio thread
    void setAmount(float amount) noexcept;               // [0,100]; non-finite keeps; denormal -> 0 (FR-043)
    [[nodiscard]] float amount() const noexcept;         // the sanitised target
    void setFundamentalHz(float hz) noexcept;            // release = kDetectorReleasePeriods / hz (FR-037)
    [[nodiscard]] float fundamentalHz() const noexcept;
    void processBlock(float* left, float* right, std::size_t numSamples) noexcept;  // any numSamples
    [[nodiscard]] bool stateFinite() const noexcept;
    [[nodiscard]] static constexpr std::size_t latencySamples() noexcept { return 0; }
    // read-only observers (tests; Phase 6/9 diagnostics)
    [[nodiscard]] float detectorLevel() const noexcept;          // env (S4.4; C-3 gate)
    [[nodiscard]] float releaseSeconds() const noexcept;         // SC-024(a)
    [[nodiscard]] std::size_t oversamplingFactor() const noexcept;
    /// Largest |y| before the kOutputCeiling clamp since prepare / reset / clearPreCeilingPeak (SC-019: the test
    /// asserts it stays below the ceiling, so a design that relies on the clamp fails). One max per sample.
    [[nodiscard]] float preCeilingPeak() const noexcept;
    void clearPreCeilingPeak() noexcept;
};
namespace detail {   // S4.3 lookup, free and constexpr so it can be static_asserted and unit-tested
[[nodiscard]] constexpr std::size_t amountTableIndex(float pos) noexcept;   // min(size_t(pos), kTablePoints - 2)
[[nodiscard]] inline float lerpAmountTable(const std::array<float, HarmonicFold::kTablePoints>& t, float a) noexcept;
}
}
```

The class is copyable and movable (FR-020's non-copyability only applied to an owned `Oversampler`).

### S4.2 State layout (fixed-size, ≈ 6 KB)

- Per channel (×2): `PolyphaseHalfbandOversampler os`; `Biquad splitBase, splitOs`; `std::array<Biquad,3> notch`;
  `DCBlocker dc`; `std::array<float, 4> buf`.
- Shared:
  - `OnePoleSmoother amountSmoother_`;
  - tables `std::array<float, kTablePoints>` for g, θ, V, makeup and c (5 × 1.6 KB);
  - detector: `std::array<float, 8> ring_`, `segMax_`, `segCount_`, `segLen_`, `ringPos_`, `env_`, `attackCoeff_`,
    `releaseCoeff_`;
  - `fundamentalHz_`, `pendingAmount_`, `sampleRate_`, `prepared_`, `idle_`, `preCeilingPeak_`.
- There is no heap anywhere.

### S4.3 The static map (exact math; the model is `edge-fold-model.js` `rawMap`/`foldMap`)

P = `kCalibrationPeak`, a = smoothed amount, q = G·u (normalised domain).

```
g(a) = gS · a/30                           a ≤ 30          gS = 0.43, gC = 1.20
     = gS · (gC/gS)^((a − 30)/35)          a > 30          (continues to g(100) = 3.349: C¹-like THD slope at 65)
θ(a) = 0                                   a ≤ 65
     = θmax · (1 − (1 − t)^p), t = (a − 65)/35   a > 65    θmax = 3.0 (C-11), p = 1.5 (D-1, ease-out)
sat(q)  = q                                if g < 1e-6
        = (P/g) · Sigmoid::recipSqrt(g·q/P)                 (unity small-signal gain)
V(a)    = sat(P)                                            (value at the calibration peak)
z(q)    = sat(q)                           if θ < WavefoldMath::kSineFoldGainEpsilon (0.001)
        = (V/θ) · WavefoldMath::sineFold(sat(q)/V, θ)        (C⁰: (V/θ)·sin(θ·s/V) → s as θ → 0)
mk(a)   = P / max_{0 ≤ q ≤ P} |z(q)|                        peak-preserving static makeup
c(a)    = mk · (2/K)·Σ_k z(P·sin φ_k)·sin φ_k / P,  K = 512  h1 gain on the calibration sine
Φ_fold(u) = mk(a) · z(G·u) / G
```

- **Why the geometric g continues past 65.** With g frozen at 65 and θ starting at 0, THD rose only 0.0014 dB per 0.25 step
  just after 65. SC-006's "strictly increasing" would have been decided by measurement noise. With g continuing, the minimum
  step is +0.079 dB (S9.3).
- **Why θmax = 3.0 (C-11).**
  - SC-006's mid-grid centroid clause sets the floor: the rise from 0 to 100 at C2 is 0.302 oct at 2.7, 0.486 at 2.9,
    0.541 at 2.95 and **0.600 at 3.0** (bar 0.5).
  - 3.0 keeps every sine bar: minimum |T₁| −9.46 dB (bar −12), worst step ΔL = ΔCF = 0.191 dB (bar 0.25), and H(100) = 17
    against 4·H(30) = 4.
  - 2.7 had more sine margin (−3.59 dB, 0.153 dB, H 15) but fails the centroid clause, so it is rejected.
- **Why the ease-out θ law, p = 1.5 (tasks.md D-1, ruled 2026-10-11).** The round-2 law was linear,
  θ = θmax·(a − 65)/35. The C++ gate `HarmonicFold_RegimeContinuityStaticSmoke` then read the calibration sine's centroid
  step |ΔC| 0.0750 oct at 99.75 → 100 (bar 0.05; the model reproduces it, 0.0751): the sine's centroid moves fastest near
  θmax, which the round-2 evidence never measured (it measured |ΔC| on the mid grid only). The ruling keeps θ(100) = θmax
  and gives θ zero slope at 100: θ = θmax·s(t), s(t) = 1 − (1 − t)^p, with p the smallest candidate in {1.5, 2, 3}
  (smoothstep as a fallback) for which every 0.25 pair has |ΔC| ≤ 0.04 oct (20 % under the bar) and every other Fold bar
  holds. Measured with `evidence/d1law.js` (one process per law, concurrent; ≈ 4 s each):

  | Law | Sine worst \|ΔC\| (oct) | Sine worst \|ΔL\| = \|ΔCF\| (dB) | Sine min THD step (dB, bar +0.05) | Mid C2 worst \|ΔC\| / \|ΔL\| | Mid centroid rise (oct) | Verdict |
  |---|---|---|---|---|---|---|
  | linear (round 2) | 0.0751 @ 100 | 0.191 @ 99.5 | +0.0790 @ 65 | 0.0331 / 0.081 | 0.600 | fails \|ΔC\| |
  | **p = 1.5** | **0.0319 @ 96** | **0.109 @ 93.25** | **+0.0693 @ 100** | **0.0161 / 0.072** | **0.600** | **PASS** |
  | p = 2 | 0.0252 @ 91.75 | 0.095 @ 88.25 | +0.0210 @ 100 | 0.0125 / 0.084 | 0.600 | fails THD step |
  | p = 3 | 0.0226 @ 85 | 0.093 @ 81.75 | +0.0165 @ 100 | 0.0095 / 0.110 | 0.600 | fails THD step |
  | smoothstep | 0.0393 @ 95 | 0.146 @ 93 | +0.0298 @ 100 | 0.0183 / 0.085 | 0.600 | fails THD step |

  The sine figures are identical at E0/E1/E2/C2 × 44.1/48/96 kHz (the static map is note-independent; only the harmonic
  count changes). With p = 1.5, at every one of those twelve cells: THD strictly increasing, H non-decreasing,
  H(30) = 1 → H(100) = 17, regime rises 42.3 / 14.0 / 24.1 dB, |∠T₁| 0° and min |T₁| −9.46 dB (at 100) on the sine and
  −6.27 dB (E0) on the saw anchor (static map). Larger p flattens θ near 100 so far that THD's last steps rest on g alone.
  Also re-measured under p = 1.5: SC-008(b) static worst RMS ratio 0.0055 (sine) / 0.0040 (mid) at ε = 0.05 (linear 0.0099 /
  0.0063, bar 0.02); SC-013(c) teeth (shared detector at 82.5) L/R 0.53 dB vs 6.02 (linear 1.51); a ≤ 65 and a = 100 are
  unchanged, so SC-007, SC-011, SC-025(a), S9.4 aliasing and every a = 100 figure keep their round-2 values.
- **Tables.** `prepare` computes the five tables in `double` over a = 0, 0.25, …, 100. `mk` uses 4096 points on
  q ∈ [0, P]; c uses 512 phase points. Per sample, all five are interpolated linearly at index a·4, so they are C⁰ in a.
- **Lookup, exactly.** a is the smoothed amount, already clamped to [0, 100] by `setAmount`. The smoother snaps to its target
  (smoother.h:197–201), so a = 100 is reached exactly.
  ```cpp
  constexpr std::size_t amountTableIndex(float pos) noexcept {          // pos = a * 4 in [0, 400]
      return std::min<std::size_t>(static_cast<std::size_t>(pos), HarmonicFold::kTablePoints - 2);   // <= 399
  }
  inline float lerpAmountTable(const std::array<float, HarmonicFold::kTablePoints>& T, float a) noexcept {
      const float pos = a * 4.0f;
      const std::size_t i = amountTableIndex(pos);
      const float t = pos - static_cast<float>(i);                     // a = 100 -> i = 399, t = 1
      return (1.0f - t) * T[i] + t * T[i + 1];                         // t = 1 -> 0·T[399] + T[400], exactly T[400]
  }
  static_assert(amountTableIndex(400.0f) == HarmonicFold::kTablePoints - 2);
  static_assert(amountTableIndex(0.0f) == 0);
  ```
  - Without the clamp, a = 100 gives i = 400 and reads T[401], one past the end.
  - Test `HarmonicFold_TableLookupAtEndpoint` calls `lerpAmountTable` on a table that is the first member of a test struct,
    followed by a quiet-NaN `float` sentinel (no padding between them). An unclamped read then gives 1·T[400] + 0·NaN = NaN,
    so the test fails deterministically rather than by luck. The test TU sits in the `-fno-fast-math` block, so the NaN
    propagates and `detail::isFinite` sees it.
  - It checks a = 100 → exactly T[400], 99.75 → T[399] and 0 → T[0]. It also renders a Fold at amount 100 past the smoother
    and asserts `stateFinite()`.
- **β and w** are computed in closed form, without tables:
  - β = smoothstep((a − 30)/(40 − 30));
  - w = smoothstep(a / 10);
  - smoothstep(t) = t²(3 − 2t), clamped.

### S4.4 Detector (FR-033 shared, FR-037)

```
d       = max(|xL|, |xR|)                                   (sanitised samples; C-4 per-channel variant: d = |x_ch|)
segMax  = max(segMax, d); if (++segCount == segLen) { ring[ringPos] = segMax; ringPos = (ringPos+1) & 7; segMax = 0; segCount = 0; }
raw     = max(segMax, max(ring[0..7]))                      window ∈ [8·segLen, 9·segLen] ≥ hold
env     = (raw > env) ? raw : flushDenormal(env + αr · (raw − env))   (instant attack: kDetectorAttackMs = 0)
G       = (1 − β) + β · P / max(env, kDetectorFloor)
segLen  = ceil(round(kDetectorHoldMs·1e-3·fs) / 8)
αr      = 1 − exp(−fundamentalHz / (kDetectorReleasePeriods·fs))   (τr = 4/f₀: 194 ms at E0; ≥ N/f₀ exactly, SC-024(a))
```

- **Attack is instant (review round 2).** The round-1 2 ms one-pole attack let G overshoot by up to 18 dB after the SC-013(c)
  step and failed its frame clause by 0.45 dB (C-4). `raw` is a running maximum of |x| over the window, so on a rising
  segment env = raw keeps G·|x| ≤ P and G continuous. `kDetectorAttackMs = 0` is the documented constant FR-037 requires;
  `static_assert(kDetectorAttackMs == 0.0f)` sits next to the branch, so a later non-zero value cannot be silently ignored.
- **Denormals.** `env` decays geometrically toward 0 on silence and runs even while idle, so it is flushed with
  `detail::flushDenormal` (db_utils.h:263) on every release step: one scalar per sample. The halfband, biquad and DC states
  rely on the class-level FTZ/DAZ precondition (S4.1).

- **In steady state the windowed maximum of a periodic signal over ≥ one period is the global peak: a constant.**
- `env` converges to it, so G is constant and there is no in-cycle modulation (C-3 ripple 0).
- The detector runs whenever the Fold is prepared, idle included, so engaging from 0 sees a settled G.
- `setFundamentalHz`: a non-finite value keeps the previous one; a denormal becomes 0; the result is clamped to
  [8, 20000] Hz, and αr is recomputed (one `std::exp`, RT-safe).

### S4.5 `processBlock` contract

For each sample:
1. Replace a non-finite input on either channel by 0.0 (FR-043).
2. Advance the detector, then a = amountSmoother_.process().
3. If idle (a == 0 and target == 0), the output equals the sanitised input.
4. Otherwise, per channel, apply the S2.1 stage with Φ_fold, c = c(a), w(a), the hook = 3 notches, and DC.
5. `preCeilingPeak_ = max(preCeilingPeak_, |y|)`, then clamp to ±`kOutputCeiling`.

Other rules:
- The amount target is set by `setAmount` immediately (smoothed per sample), so the render is block-partition independent
  (SC-017).
- Leaving idle resets the per-channel residual state (os, splitOs, notches, dc). `splitBase` keeps running while idle, so
  `lo` is valid at engage.
- Before `prepare`, `processBlock` returns with the buffers untouched (FR-042), and the setters store sanitised values.
- `right == left`: process one channel, using channel 0's state.
- `prepare` sets `idle_ = true` and snaps the smoother to the pending amount (default 0), then computes the tables, the
  factor and the filters.
- `reset()` zeroes all state, including `preCeilingPeak_`, and keeps the pending amount and fundamental.

### S4.6 Regime figures this map produces (calibration sine, `kFoldIndexMax` 3.0, `edge-fold-model.js fold`)

| a | THD dB | H | L dB | CF dB | \|T₁\| dB |
|---|---|---|---|---|---|
| 0 | −282.9 | 0 | 0.00 | 3.01 | 0.00 |
| 2.5 | −75.9 | 0 | 0.00 | 3.01 | 0.00 |
| 15 | −45.0 | 1 | 0.05 | 2.96 | 0.05 |
| 30 | −33.6 | 1 | 0.18 | 2.83 | 0.18 |
| 47.5 | −25.9 | 2 | 0.43 | 2.58 | 0.42 |
| 65 | −19.6 | 3 | 0.86 | 2.15 | 0.81 |
| 82.5 | −8.95 | 7 | 2.12 | 0.89 | 1.60 |
| 100 | +4.5 | 17 | −3.63 | 6.64 | −9.46 |

Bars these figures meet:
- **SC-006:** THD rises 42.3 / 14.0 / 24.1 dB within the three regimes (bar 6); H(100) = 17 ≥ 4·H(30) = 4. Mid-grid centroid
  at C2 rises 0.600 oct (bar 0.5; C-11, S9.3).
- **SC-007(a):** THD +14.0 dB (bar 10), H 1 → 3 (+200 %), ΔL +0.68, ΔCF −0.68 (bars ±1).
- **SC-007(c):** median distance from a plain tanh sweep 5.49 dB (bar 3).
- **Count margins** (`evidence/hmargin.js`): h5 at a = 30 is −63.6 dB (3.6 dB under the −60 dB count threshold); h9 at 65 is
  −64.8 dB; h15 at 82.5 is −52.8 dB under the D-1 law (7.2 dB over: counted; −59.1 dB, 0.9 dB over, under the
  round-2 linear law, R-3). The 82.5 row is the D-1 law (p = 1.5); every other row is unchanged by it.

---

## S5. Edge excitation inside `ProfundumCore` (Layer 3, extension)

### S5.1 Added includes and public API (append-only; every Phase 1 declaration stays verbatim, SC-021)

Includes: `core/sigmoid.h`, `primitives/biquad.h`, `primitives/dc_blocker.h`, `primitives/smoother.h`,
`primitives/polyphase_halfband_oversampler.h`. It does **not** include `harmonic_fold.h`; Phase 9 composes the chain.

**Header banner.** The four Layer 1 includes are legal for Layer 3, but they make the banner at `profundum_core.h:5–6` ("Includes
Layer 0 and Layer 2 only") false. Phase 2 rewrites those two comment lines to "Includes Layer 0, Layer 1 and Layer 2 only"
and adds a `Phase 2: specs/profundum-phase2-edge-fold` line to the banner. `check-footprint.js` (S10.3) allows exactly these
banner lines; every other removed line must sit inside the body allow-list of S5.6.

```cpp
// inside class ProfundumCore, public:
struct ExcitationControls { float mix = 0.0f; float drive = 0.5f; float asymmetry = 0.0f; };   // FR-002, D-5
static constexpr ExcitationControls kDefaultExcitation{};
static constexpr float kEdgeDriveRangeDb = 12.0f;                  // FR-004
static constexpr float kEdgeDriveFloor = 0.251188643f;             // 10^(-12/20)
static constexpr float kExcitationDriveMinDb = -12.0f;             // C-7
static constexpr float kExcitationDriveMaxDb = 12.0f;
static constexpr float kExcitationAsymmetryBias = 0.5f;            // bias at asymmetry 1 (argument units)
static constexpr float kExcitationSplitHz = 500.0f;
static constexpr float kExcitationSplitEngageMix = 0.1f;
static constexpr float kExcitationDcCornerHz = 2.0f;
static constexpr float kExcitationSmoothMs = 5.0f;
static constexpr float kExcitationCeiling = 6.0f;
static_assert(kExcitationCeiling < 2.0f * kBankHeadroom, "SC-019");

void setExcitation(const ExcitationControls& c) noexcept;          // no-op before prepare (Phase 1 FR-061 pattern)
[[nodiscard]] ExcitationControls excitation() const noexcept;      // pending, sanitised
[[nodiscard]] float effectiveExcitationDrive() const noexcept;     // latched target D_eff (FR-004)
[[nodiscard]] static float excitationDriveLaw(float drive) noexcept;   // 10^((-12 + 24·d)/20), computed in double
[[nodiscard]] static float edgeDriveGain(float edge) noexcept;          // 10^(12·(e − 1)/20), computed in double
[[nodiscard]] std::size_t excitationOversamplingFactor() const noexcept;
/// Largest |excitation output| before the kExcitationCeiling clamp since prepare / reset /
/// clearExcitationPreCeilingPeak (SC-019; one max per sample while the stage runs).
[[nodiscard]] float excitationPreCeilingPeak() const noexcept;
void clearExcitationPreCeilingPeak() noexcept;
```

**Sanitising (FR-043).** A new private `sanitizeExcitationField(x, prev, lo, hi)`:
- non-finite → prev;
- `(bits & 0x7F800000) == 0` → 0.0f (denormal flush);
- otherwise clamp.

Phase 1's `sanitizeField` is untouched.

### S5.2 State (fixed-size, ≈ 4.5 KB added)

- Per channel (×2): `PolyphaseHalfbandOversampler`; `Biquad splitBase, splitOs`; `std::array<Biquad,3> notch`; `DCBlocker dc`;
  ADAA history `v1, s1`; `std::array<float,4> buf`.
- Shared:
  - `pendingExcitation_`, `latchedExcitation_`, `excitationDirty_`, `excitationIdle_`, `excitationPreCeilingPeak_`;
  - `OnePoleSmoother mixS_, log2DriveS_, biasS_`: the three smoothed **table coordinates** (S5.3). Makeup M and h1 gain c
    are not smoothed; they are looked up from the smoothed coordinates every sample;
  - `latchedDriveTarget_` (what `effectiveExcitationDrive()` returns);
  - tables `std::array<float, 49*9> excMakeup_, excH1_`.

### S5.3 Control timing (FR-007)

**`controlUpdate` gains step 1b**, after step 1 at `profundum_core.h:314–319`:
```
if (excitationDirty_ || controlsLatchedThisUpdate):
    latchedExcitation_ = pendingExcitation_
    D = excitationDriveLaw(latched.drive) · edgeDriveGain(latchedControls_.edge)   → latchedDriveTarget_
    b = latched.asymmetry · kExcitationAsymmetryBias
    set targets: mixS_ ← mix, log2DriveS_ ← log2(D), biasS_ ← b          // coordinates only; no M or c here
    if excitationIdle_ and mix > 0: reset the per-channel excitation state, excitationIdle_ = false
```
"Controls latched this update" means step 1 re-latched the recipe (Edge may have changed).

**Smooth the coordinates, not the outputs (review round 2).** The round-1 plan smoothed D, b, M and c with independent
one-poles. M(D) is strongly nonlinear: S9.2 gives 5.638 / 1.447 / 0.464 / 0.289 at D = 0.063 / 0.251 / 1.0 / 3.981. Midway
through a drive step the smoothed pair (D, M) therefore sits far off the static law, so FR-011 held only at the endpoints.
`evidence/ctlphase.js` measured that design against SC-009 at the worst of 16 step phases (asymmetry 1, mix 1, 48 kHz):

| Step | Round-1 "smooth outputs": SC-009 ratio / level bump | **Planned "smooth coordinates": ratio / bump** |
|---|---|---|
| E1 mid, drive 0 → 1 | 1.936 / +12.26 dB | **0.999 / +0.78 dB** |
| E1 saw, drive 0 → 1 | 2.285 / +11.52 dB | **0.999 / +0.55 dB** |
| C2 mid, drive 0 → 1 | 3.219 / +12.56 dB | **1.017 / +0.63 dB** |
| C2 saw, drive 0 → 1 | 3.974 / +13.77 dB | **1.038 / +0.90 dB** |
| E1 mid, edge 0 → 1 | 1.190 / +3.71 dB | **1.000 / +0.40 dB** |
| C2 saw, edge 0 → 1 | 1.460 / +4.44 dB | **1.037 / +0.64 dB** |

(SC-009 bar: ratio ≤ 1.1. The ratio is the transition's max |Δy| over the max steady |Δy| on the 33-point grid; the bump is
the max |y| in the 20 ms after the step, re the endpoints' steady max |y|. All eight note × input × control cells are in the
log; the worst planned ratio is 1.038, a 6 % margin. Down-steps 1 → 0 read ≤ 0.85 in both designs, `evidence/ctlsteps.js`.)
The edge rows hold the core's recipe fixed at the input's own Edge, so they isolate the excitation's part of an Edge step.
The recipe's own Edge transition is Phase 1's smoothed shape path, which Phase 1's SC tests already gate.

**Per-sample lookup.** In `exciteRun`, M and c come from the bilinear table at the smoothed (log₂ D, b) on every sample, and
D = `std::exp2` of the smoothed log₂ D. The table interpolation error against the exact static law is ≤ 0.021 dB in M over
481 × 5 off-grid points (ctlsteps.js), far inside SC-020's ±1.5 dB. Bilinear lookup with both axes clamped:
```cpp
const float pi = std::clamp((l2 + 4.0f) * 8.0f, 0.0f, 48.0f);    // log2 D in [-4, 2] -> [0, 48]
const float pj = std::clamp(b * 16.0f, 0.0f, 8.0f);               // b in [0, 0.5]    -> [0, 8]
const std::size_t i = std::min<std::size_t>(static_cast<std::size_t>(pi), 47);   // <= 47: reads column i + 1 <= 48
const std::size_t j = std::min<std::size_t>(static_cast<std::size_t>(pj), 7);    // <= 7:  reads row j + 1 <= 8
const float ti = pi - static_cast<float>(i), tj = pj - static_cast<float>(j);
// (1 - t)·a + t·b form on both axes, so the far edges return the table value exactly
```
Both index expressions are `constexpr` helpers with `static_assert`s at the far corner (index 47 and 7 at pi = 48, pj = 8),
and `ProfundumCore_ExcitationTableLookupAtCorners` covers the four corners with the same NaN-sentinel layout as S4.3.

**`applyNoteOn`:**
- Reset branch: perform the same latch, then `snapToTarget()` on all three smoothers and reset every per-channel excitation
  state, with ADAA `v1 = b`, `s1 = sqrt(1 + b·b)` (FR-013).
- FreeRunning branch: latch only if dirty, with no snap and no reset (SC-014).

Nothing in step 1b touches bank state, so Phase 1 renders are unchanged.

**`processBlock`:**
- The headroom rescale moves **into** the run loop: after `bank_.processStereoBlock(left + i, right + i, run)`, multiply that
  run by `kBankHeadroom`, keeping the same `right != left` guard. This is the same single multiply per sample, so it is
  bit-identical; the pinned Phase 1 reference proves it (SC-004(a)).
- Then, unless `excitationIdle_`, call `exciteRun(left + i, right + i, run)`.
- When the mix smoother reaches exactly 0 with target 0 (snap), set `excitationIdle_ = true` at the end of that run.

### S5.4 `exciteRun` — per sample (both channels share the smoothed coordinates and the looked-up M, c)

```
m = mixS_.process(); l2 = log2DriveS_.process(); b = biasS_.process()
D = std::exp2(l2); M = bilinear(excMakeup_, l2, b); ce = bilinear(excH1_, l2, b)      // S5.3 clamped lookup
cm = 1 − m + m·ce;   w = smoothstep(m / 0.1)
sb = std::sqrt(1.0f + b*b);   fb = (b + b) / (sb + sb)          // the exact ADAA expression at v = v1 = b
per channel x:
  lo = splitBase(x); base = cm·(lo + (1 − w)(x − lo))
  os.upsample(x, buf)
  for i < F:  v = D·buf[i]·(1/P) + b;   s = std::sqrt(1.0f + v*v)
              y = (v + v1) / (s + s1);   v1 = v; s1 = s          // closed-form ADAA1 of v/sqrt(1+v²): F1 = sqrt(1+v²)
              ul = splitOs(buf[i])
              buf[i] = m·M·(y − fb) + (1 − m)·buf[i] − cm·(ul + (1 − w)(buf[i] − ul))
  r = os.downsample(buf, notches)
  pre = base + dc.process(r);   excitationPreCeilingPeak_ = max(excitationPreCeilingPeak_, |pre|)
  out = clamp(pre, ±kExcitationCeiling)
```

**Why this ADAA.** (F1(v) − F1(v₁))/(v − v₁) with F1 = √(1 + v²) equals (v + v₁)/(√(1+v²) + √(1+v₁²)) algebraically.
That has **no cancellation and no ε branch** (E-4 does not apply). With v = v₁ = b it reduces to b/√(1+b²) exactly, and
FR-009's static offset is computed by the *same expression*, so a silent input gives exactly 0.0 (D-13, SC-011, SC-014).

**Tables (computed in `prepare`, in `double`, no heap).**
- 49 drive points over log₂ D ∈ [−4, 2] (D ∈ [0.0625, 4] covers D_eff ∈ [0.0631, 3.981]), × 9 bias points over [0, 0.5].
- Per cell, over K = 512 phases φ_k, compute y_k = f(D·sin φ_k + b) − f(b) with f = `Sigmoid::recipSqrt`. Remove its mean.
- M = (P/√2) / rms(y), so the output RMS equals the calibration RMS (FR-011 static makeup).
- c = M·(2/K)·Σ y_k sin φ_k / P.

**Stereo (FR-012).** Identical arithmetic per channel. All state is reset together and starts identical, so with a zero pan
vector L == R stays bit-identical. With `right == left`, only channel 0 runs.

### S5.6 Body edits to existing Phase 1 members (declarations unchanged, SC-021)

Every Phase 1 declaration stays verbatim. These bodies change, and only these; `check-footprint.js` allows removed lines only
inside them and in the S5.1 banner:

| Member | Change |
|---|---|
| `prepare()` (:77–95) | After the existing steps: configure the per-channel split, notch and DC filters for `sr`; choose the factor (S3.3) with `setFactor`; compute `excMakeup_` / `excH1_` in double; `configure(kExcitationSmoothMs, sr)` the three smoothers and snap them to the pending excitation's coordinates; reset all excitation state; `excitationIdle_ = true` (the mix target is the pending mix, 0 by default); `excitationPreCeilingPeak_ = 0`. Still documented NOT RT-safe (:75–76) |
| `reset()` (:99–104) | After `initialiseState`: reset the oversamplers, split, notch and DC filters, the ADAA history (`v1 = b`, `s1 = sqrt(1 + b·b)` from the pending bias), snap the three smoothers to the pending excitation's coordinates, `excitationIdle_ = (pending mix == 0)`, `excitationPreCeilingPeak_ = 0`. `pendingExcitation_` is kept, as Phase 1 keeps `pendingControls_` |
| `stateFinite()` (:238–256) | Also requires `detail::isFinite` of: each channel's `PolyphaseHalfbandOversampler::stateFinite()`; `getZ1()`/`getZ2()` of `splitBase`, `splitOs` and the three notches; the DC blocker's state. `DCBlocker` has no accessor (its `x1_`, `y1_` are private, dc_blocker.h:240–241), so each channel keeps `dcIn_`/`dcOut_`, copies of the last value passed to and returned by `dc.process`, which equal x₁/y₁. Also `v1`, `s1`, `buf`, and the three smoothers' `getCurrentValue()` (smoother.h:191). The existing return expression gains these terms; no Phase 1 term is removed |
| `controlUpdate` (:314–319 onward) | Step 1b (S5.3) |
| `applyNoteOn` (:386–442) | S5.3 |
| `processBlock` (:172–221) | Headroom rescale moved into the run loop; `exciteRun` call; idle detection (S5.3) |

SC-016's `stateFinite()` assertion therefore sees the new state, and SC-017's partition and reset-determinism tests see a
`reset()` that clears it.

### S5.5 Figures this produces (model S9.2/S9.3; drive law −12…+12 dB)

| Clause | Figure (bar) |
|---|---|
| SC-001 drive span, `kSineAnchor` | Edge 0: −66.1 → −21.6 dB (44.4; bar 20). Edge 1: −42.4 → −11.0 dB (31.4) |
| SC-002(b) G rise, drive 0.5, Edge 0 → 1 | 20.7 dB (bar 6) |
| SC-002(c) Edge 0, drive 1 | THD −21.6 against the reference's ≈ −78 (bar 20 dB) |
| SC-002(c) mix blend, as written: y(0.5) vs ½(y(0) + y(1)) | −53.6 to −60.5 dB (bar −60): **fails** at 44.1 and 96 kHz by construction (C-9) |
| SC-002(c) mix blend, C-9 re-rule: y(0.55) vs ½(y(0.1) + y(1)) | −300.9 to −311.0 dB in double, Edge 0 and 1 identical (bar −60; `evidence/mixlin.js`) |
| SC-009 drive / edge steps, coordinate smoothing | Worst ratio 1.038 (C2 saw, drive 0 → 1, worst of 16 phases); bar 1.1, a 6 % margin. The round-1 output smoothing read 3.974 (S5.3) |
| SC-003(a) E_gen, drive 0.5 / Edge 0.5 (D_eff 0.5) | 1/32: −46.3; 0.25: −28.3; 0.5: −22.6; 1: −17.6 dB (bar ≥ −30, monotonic) |
| SC-003(b) triangle (Edge 0 → D_eff 1.0) | E_even −19.2 dB re h1 (bar ≥ −30) |
| SC-020 level law, mix 1 | Worst −0.95 dB (mid grid, drive 1); saw ≤ ±0.26; sine ±0.00 (bar ±1.5) |
| SC-012(a), drive 0 / mix 1, rendered, 44.1 kHz | Worst 0.06 dB / 0.13° (sine, mid, saw; E1, E2, C2) |
| SC-019 pre-clamp peaks, rendered (`evidence/validate.js` at 44.1 / 48 / 96 kHz, E1 / E2 / C2, the four inputs) | Excitation ≤ 1.42 (SC-009 state, d0, 44.1 kHz); Fold at 65 / 100 ≤ 2.40 (SC-009 state, 96 kHz, `kFoldIndexMax` 3.0); ceilings 6.0. The C++ test asserts the pre-clamp peaks (S8.2), not only the clamped output |

---

## S6. Algorithm choices — why

| Choice | Rejected alternative and the measurement that decided it |
|---|---|
| `Sigmoid::recipSqrt` + closed-form ADAA1 | `TanhADAA` (float, ε = 1e-5 fallback to `fastTanh` midpoint, naive first sample: E-4) and `fastTanh` (±1 step at 3.5: E-5). recipSqrt has no step, needs one sqrt, and its ADAA1 is exact and well-conditioned. `std::tanh` at 4× costs ≈ 1 % alone |
| Post-drive bias `v = D·x̂ + b` (the `withBias` idiom) | A drive-scaled bias was considered; the post-drive bias meets SC-003 at both measured points (S5.5) |
| Envelope-normalised waveshaper (OQ-A candidate (a)) for compression | Split-band limiter and parallel generator. Candidate (a) passes SC-007 with margin and is the cheapest, so per FR-035 they were not built (S9.7) |
| Smoothing the excitation's table coordinates (log₂ D, b) and looking up M, c per sample | Independent one-poles on D, b, M, c: worst SC-009 ratio 3.974 and a +13.8 dB transient bump on a drive step (S5.3, `evidence/ctlphase.js`) |
| Instant detector attack (`kDetectorAttackMs = 0`) | 2 ms attack: SC-013(c) step clause fails by 0.453 dB; 0.5 ms fails by 0.053 dB (C-4, `evidence/sc013step.js`) |
| Windowed-peak detector (8-segment ring) + attack/release | An RMS one-pole has ripple in steady state, and `EnvelopeFollower` is L2 (not includable from L2). The windowed peak is exactly constant in steady state (C-3) |
| Peak normalisation + peak-preserving makeup | RMS makeup: saw +7 dB at amount 100. Peak normalisation also bounds the sine-fold index for every waveform, so h1 never flips (saw ∠T₁ 0.00°) |
| Sine fold via `WavefoldMath::sineFold` on the saturator output | Triangle fold (corners alias) and `WavefolderProcessor` (not C⁰ at zero, mono, 10 Hz DC blocker: E-6). The sine fold is entire and continuous at θ → 0 |
| Polyphase halfband (S3) | Economy `Oversampler` (C-1) |
| 3 notches at 2fs as the residual pre-filter | An 8-pole Butterworth at 0.33·fs is equally effective (−94.3) but costs 2× the biquads; a 4-pole fails (−73 to −83) |
| 500 Hz split, engage weights | Full-band bypass (comb, C-5); plain oversampling (SC-026 lag, SC-012(a)) |

---

## S7. RT safety, determinism, portability

- **RT.**
  - All audio-thread methods are `noexcept`, and `static_assert(noexcept(...))` covers each.
  - No heap anywhere: the oversampler, the tables and the ring are fixed-size `std::array` members.
  - No `std::function`, no locks, no I/O. `std::exp` runs only in `setFundamentalHz` and `prepare`; per sample there are
    only `std::sqrt`, `std::sin` and one `std::exp2` (the excitation's smoothed log₂ D, S5.4).
  - Every table read uses a clamped index (S4.3, S5.3), with `static_assert`s at the far corner and a NaN-sentinel test.
  - `prepare`/`reset` are control-thread calls. For the core they are already documented NOT RT-safe (`:75–76`). The RT-safe
    clear of a sounding core is a Reset-policy `noteOn` (array fills only).
- **Denormals.** Each component carries its own precondition; none inherits another's:
  - `ProfundumCore`: its existing class note (`profundum_core.h:37–38`) stays verbatim and gains one added line naming the
    excitation state it now owns (halfband sections, split, notch and DC filters, ADAA history);
  - `HarmonicFold`: its own class-level `@note` (S4.1). The Fold is a standalone L2 class that the core neither includes nor
    owns, so the core's note cannot cover it;
  - `PolyphaseHalfbandOversampler`: its doc comment (S3.1).
  Section, biquad and DC states are not flushed. The Fold's detector `env`, which decays on silence while the Fold idles, is
  flushed explicitly (S4.4). Tests run under the shared main's FTZ/DAZ (`CMakeLists.txt:89`).
- **Determinism.** No RNG. Engaging from idle resets state, and every control is latched on the global grid (core) or set
  per sample (Fold). Renders are bit-identical in-process for any host partition (SC-017). Cross-toolchain comparisons use
  `render_fingerprint.h` only.
- **Fast-math.**
  - Every finite test uses `detail::isFinite`; denormal setter values are detected by bit pattern.
  - The NaN/Inf tests sit in the `-fno-fast-math` block.
  - The zero-in-zero-out equality depends on `fb` and the per-sample ADAA sharing one expression. If a compiler contracts the
    two sites differently, SC-011 and SC-014 fail loudly. Fallback: route both through one `static` helper and compare bits
    in a unit test.
- **Portability.**
  - No SIMD loads (scalar code); no narrowing in brace init (designated initialisers).
  - No namespace-scope `Rect`/`MAC`-style names (`tools/lint-apple-globals.js`).
  - `node tools/check-portability.js` runs before commit.

---

## S8. Test plan

### S8.1 Files, targets, tags

| TU | Target | Content |
|---|---|---|
| `dsp/tests/unit/primitives/polyphase_halfband_oversampler_test.cpp` | `dsp_primitives_tests` | Response, group delay, state rules |
| `dsp/tests/unit/processors/harmonic_fold_test_helpers.h` | — | Calibration sine and recipe additive renders in `double`. Namespace `Krate::DSP::HarmonicFoldTest` (ODR-swept: no match in `dsp/` or `plugins/`). Includes only `spectral_shape_recipe_test_helpers.h` (processors: `describePowers`, `Descriptors`), the layer-neutral `low_frequency_metrics.h` and `<krate/dsp/processors/harmonic_fold.h>`, so **no `systems/` header**. It holds its own copies of `bh7Window`, `fftDouble`, `bh7PowerSpectrum`, `goertzelPhase`, `renderedHarmonicPowers`, `aliasFftLength` and `aliasedPower`, transcribed from `profundum_core_test_helpers.h` (Phase 1 helpers stay untouched, SC-021) |
| `dsp/tests/unit/processors/harmonic_fold_test.cpp` | `dsp_processors_tests` | Fold API, regimes, continuity, detector, stereo, RT |
| `dsp/tests/unit/processors/harmonic_fold_nonfinite_test.cpp` | `dsp_processors_tests`, **`-fno-fast-math`** | SC-016 Fold; `HarmonicFold_TableLookupAtEndpoint` (S4.3) |
| `dsp/tests/unit/systems/profundum_phase1_core_ref.h` | — | **Generated** by `node specs/profundum-phase2-edge-fold/extract-phase1-ref.js`: `git show 47e43918:dsp/include/krate/dsp/systems/profundum_core.h`, with `namespace Krate::DSP {` replaced by `namespace Krate::DSP::ProfundumTest::Phase1Ref {`. Includes stay outside. The script prints the diff (namespace lines only) for comply |
| `dsp/tests/unit/systems/profundum_excitation_test_helpers.h` | — | `aliasSafeFftLength`, `generatedEvenEnergy`, `t1Transfer`, twin-core mix-0 references |
| `dsp/tests/unit/systems/profundum_core_excitation_test.cpp` | `dsp_systems_tests` | API, law, bit-identity, clicks, stereo, retrigger, RT, determinism |
| `dsp/tests/unit/systems/profundum_core_excitation_spectral_test.cpp` | `dsp_systems_tests` | Excitation descriptor gates |
| `dsp/tests/unit/systems/profundum_edge_fold_chain_test.cpp` | `dsp_systems_tests` | SC-005 grid and positive control, core-input arms, recorded figures, `ProfundumEdgeFold_HelperParity` (S8.1), `[.listen]` |
| `dsp/tests/unit/systems/profundum_core_excitation_nonfinite_test.cpp` | `dsp_systems_tests`, **`-fno-fast-math`** | SC-016 excitation; `ProfundumCore_ExcitationTableLookupAtCorners` (S5.3) |
| `dsp/tests/unit/systems/profundum_edge_fold_perf_test.cpp` | `dsp_systems_tests`, `[.perf]` | SC-018 |

Layer rule for the test tree (review round 2): **no TU or helper under `dsp/tests/unit/processors/` includes any `systems/`
header**, directly or through a helper. The round-1 plan had `harmonic_fold_test_helpers.h` include the Phase 1 systems
helper, which pulls in `profundum_core.h` (L3) and would have made `dsp_processors_tests` depend on a Layer 3 component. That
is what dsp/CLAUDE.md:8–16's layer-mirrored tree forbids, and it has no precedent under `dsp/tests/unit/processors/`
(references there are comments only). The descriptor functions are duplicated rather than moved, because moving them would
edit a Phase 1 helper that SC-021 freezes. A drift guard covers the duplicates: `ProfundumEdgeFold_HelperParity`
(systems, `profundum_edge_fold_chain_test.cpp`, which may include both helpers since the include points downward) runs each
duplicated function and its Phase 1 original on the same fixtures (1 kHz sine, the SC-009 state render, the alias fixture)
and requires bit-identical results. `check-footprint.js` also greps the processors-layer Phase 2 files for `systems/`.

Tag rules:
- `[profundum][edge-fold]` on every case. Multi-second renders are `[long]`, each with a per-push `…Smoke` sibling.
- **Never `[long]`:** NaN/Inf guards, L == R identity, zero-mix bit-identity, zero-in-zero-out.
- Every steady-state window starts at t_settle = max(0.5 s, 8·τ_slow). τ_slow comes from the header constants:
  - DC 2 Hz → τ = 79.6 ms → t_settle = 0.637 s;
  - detector release at E0 → 194 ms → 1.55 s (Fold dynamic tests);
  - smoothers ≤ 10 ms.

### S8.2 Per-criterion tests

Notation: "cal sine" is the `double` sine at P = 0.3552343859; "core input" is a `ProfundumCore` render at the stated state;
all rates are 44.1 / 48 / 96 kHz unless stated.

| SC | TEST_CASE (TU) | Assertion strategy |
|---|---|---|
| — (S3) | `PolyphaseHalfband_DesignResponse`, `_UnityPassband`, `_ZeroInZeroOut`, `_CopyContinuesIdentically`, `_ResetDeterministic`, `_GroupDelayRecorded` (primitives) | Impulse response through up and down, FFT in double. Passband \|H\| within 0.01 dB to 0.40·fs; −3.01 ± 0.05 dB at fs/2. Outer stopband ≤ −100 dB from 0.55·fs; inner ≤ −100 dB from 1.5·fs (F = 4). A 1 kHz sine survives up → down within 0.01 dB. A zero block gives exact zeros. A copy continues bit-identically. Group delay at 20/80 Hz is printed |
| SC-001 | `ProfundumCore_ExcitationDriveMonotonic`, `_ExcitationMixMonotonic` (spectral) | 33-point sweeps; THD and H from `renderedHarmonicPowers`. Strict THD increase, H non-decreasing, span ≥ 20 dB at E0, E1, E2 and C2, Edge ∈ {0, 1}. `[long]` grid with a C2 smoke |
| SC-002 | `ProfundumCore_EdgeDriveLaw` (excitation) | Closed-form equalities within 1e-6 relative and dB-linearity within 1e-4 dB. `effectiveExcitationDrive()` is read at offsets 0, 7 and 31 after a grid latch on a 5×5 grid |
| SC-002 | `_EdgeDrivesExcitationRendered`, `_EdgeZeroStillExcites`, `_MixIndependentOfEdge` (spectral) | G = THD_out ⊖ THD_ref (linear power); 33-point Edge sweep strictly increasing, rise ≥ 6 dB. For (c)'s blend clause, **per the ratified C-9**: at Edge 0 and Edge 1 separately, drive 0.5, `kSineAnchor`, E1 and C2, steady 16-period window, the residual of y(0.55) − ½(y(0.1) + y(1)) is ≤ −60 dB re the output power (model ≤ −300.9 dB in double). The as-written y(0.5) − ½(y(0) + y(1)) residual is printed as SC-022(g) (model −53.6 to −60.5 dB) |
| SC-003 | `_ExcitationEvenHarmonicsMonotonic`, `_ExcitationTriangleTail` (spectral) | E_gen from complex BH7 Goertzel phasors against the asymmetry-0 render; strict increase over 32 points; ≥ −30 dB. Symmetry clause ≤ ref + 3 dB. Triangle: odd/even drop ≥ 20 dB, E_even ≥ −30 dB, at E0 to C2 |
| SC-004(a) | `ProfundumCore_ExcitationZeroMixBitIdentical` (excitation) | `Phase1Ref::ProfundumCore` against the new core, identical call sequences, every fixture of the SC (named states, D/B/E/S sweep with vibrato, both policies, blocks 37 and 512). `std::memcmp == 0` on both channels |
| SC-005 | `ProfundumCore_ExcitationNoAliasing`, `HarmonicFold_NoAliasing`, `ProfundumEdgeFold_ChainNoAliasing`, `ProfundumEdgeFold_AliasGatePositiveControl` (chain; `[long]` 3 notes × 3 rates; smoke E1/48 kHz) | `aliasSafeFftLength` = the smallest 2^k ≥ `aliasFftLength` with every m = 1…4 offset ≥ 16 bins; `REQUIRE` if > 2²². `aliasedPower(...)` on the last L samples after t_settle; `excludedFraction ≤ 0.5`. Bar ≤ −80 dBFS. The positive control is a naive base-rate `std::tanh(D_eff·x/P)` at D_eff = 3.98 on the SC-009 state, E1/48 kHz, and must read > −80 (model: −48.0) |
| SC-006 | `HarmonicFold_ThdAndCountMonotonic` (`[long]` 401 points + 41-point smoke), `HarmonicFold_RegimesAudible` (fold) | Cal sine at E0, E1, E2, C2; strictly increasing THD (model minimum step +0.079 dB), H non-decreasing; per-regime rise ≥ 6 dB; H(100) ≥ 4·H(30). Centroid: the recipe mid grid at C2 (additive in double, `SpectralShapeRecipe::evaluateShape` + `evaluateMask`) rises ≥ 0.5 oct. Model at `kFoldIndexMax` 3.0: C 1.270 → 1.870, a rise of 0.600 oct (`evidence/foldmid.js`; at 2.7 it was 0.302 and failed, C-11) |
| SC-007 | `HarmonicFold_CompressionRegimeDensityBeforeLevel`, `_CompressionLevelIndependent`, `_TrajectoryDistinctFromTanh` (fold) | As specified. The tanh references are in double in the same test. (c) prints the RMS-matched distance (SC-022(d)) |
| SC-008 | `HarmonicFold_RegimeContinuityStatic` (`[long]` + smoke), `_ContinuousInAmount`, `_AutomationNoJump` (fold) | The spec's algorithm verbatim, on the cal sine (E1, C2) and the recipe mid grid (C2). Model, `kFoldIndexMax` 3.0, 0.25 grid: cal sine worst \|ΔL\| = \|ΔCF\| = 0.191 dB (`evidence/steps.js`); mid grid at C2 worst \|ΔL\| = \|ΔCF\| = 0.081 dB, \|ΔC\| = 0.033 oct, \|ΔTHD\| 0.267 dB against max(3 × median, 1 dB) = 1 dB over 400 pairs (`evidence/foldmid.js`). Bars 0.25 dB / 0.25 dB / 0.05 oct |
| SC-009 | `ProfundumCore_ExcitationControlStepsNoClick`, `_ExcitationEngageNoClick` (excitation) | Steady 33-point-grid delta ceilings computed in the test; transition deltas ≤ 1.1×; two monotone segments. Each step is repeated at 16 step phases spread over one period (each on the 32-sample latch grid), and the worst phase is gated. Model with coordinate smoothing: worst 1.038 (C2 saw, drive 0 → 1; S5.3) |
| SC-010 | `HarmonicFold_TransparentAtZero` (fold: cal sine; chain: core `kSawAnchor` and SC-009 state at C0–C5) | Residual ≤ −90 dB. Idle is exact, so the expected value is exactly 0 |
| SC-011 | `ProfundumCore_ExcitationDcFree` (spectral), `HarmonicFold_DcFreeWithEvenHarmonicInput`, `ProfundumEdgeFold_ChainDcFree` (chain), `HarmonicFold_ZeroInZeroOut` (fold) | Mean over a ≥ 1 s integer-period window from t_settle ≤ −80 dBFS. Zero block → exact 0.0 |
| SC-012 | `ProfundumCore_ExcitationFundamentalPreserved` (spectral), `HarmonicFold_FundamentalPreserved` (fold: cal sine + recipe saw; chain: core saw) | Goertzel T₁ against the twin-core mix-0 reference (excitation) or the input (Fold). **Bars per the ratified C-2.** The model's worst case at asymmetry 0 is 0.13° |
| SC-013 | `ProfundumCore_ExcitationLREqualAtZeroPan`, `HarmonicFold_LREqualForIdenticalInput` (`requireLREqual`); `ProfundumCore_ExcitationMonoSum`, `HarmonicFold_MonoSum` (chain); `HarmonicFold_StereoDetectorShared` (fold) | Pan vector per the spec. The (b) phase bars follow C-2. (c), first clause, follows C-10: (c-i) Fold-added inter-channel h1 phase ≤ 2° on the mix-0 panned core (model worst 1.95°); (c-ii) at max excitation, asymmetry 0 and 1, mid and saw, E1 and C2: h1 power of (L+R)/2 within 0.5 dB of ((\|h1L\| + \|h1R\|)/2)² (model worst 0.16 dB); (c-iii) phase and L/R imbalance printed as SC-022(h). The shared-detector sub-clause follows C-4: steady L/R ratio within ±1 dB of 6.02 dB (model 5.04 dB); through the step, with the S4.4 instant attack, the 4-period frame ratio stays inside the pre/post band ± 0.1 dB (model: 0.000 dB excursion; frames 5.024–5.039 dB) |
| SC-014 | `ProfundumCore_ExcitationRetriggerPolicy` (excitation) | First sample `== 0.0f`. Renders after `noteOn` from four different prior states are `memcmp`-equal |
| SC-015 | `ProfundumCore_ExcitationNoAllocation`, `HarmonicFold_NoAllocation` | `AllocationScope` count == 0 across the listed calls and blocks 1, 37, 64, 512 and 4096 (Fold up to 8192), plus `static_assert(noexcept(...))` |
| SC-016 | `ProfundumCore_ExcitationNonFinite`, `HarmonicFold_NonFinite` (`-fno-fast-math`); `HarmonicFold_RangeAndRates` (fold) | Bit-pattern NaN/±Inf/denormal; read-back `== 0.0f` after a denormal; the injected block is `memcmp`-equal to its zero-substituted twin; `stateFinite()`; 22.05 and 192 kHz bounded |
| SC-017 | `ProfundumCore_ExcitationDeterministic`, `HarmonicFold_BlockSizeIndependent` (excitation / fold); `ProfundumEdgeFold_SampleRateChange` (chain) | `memcmp` across partitions {512, 64, 37, 1}. After a rate change, the SC-005 E1 smoke and SC-012(a) are re-run |
| SC-018 | `ProfundumEdgeFold_CpuBudget` (perf) | The Phase 1 perf harness pattern: 48 kHz, host block 64, 64 partials, 10 s, warm-up plus median of 5. **Load (spec SC-018, verbatim):** the core runs Phase 1 SC-013's load (D/B/E/S sweep + 5 Hz vibrato); max excitation (mix 1, asymmetry 1) with a 0.5 Hz drive automation; the Fold with a 0.5 Hz 0–100 amount sweep. **Stage cost** = [time(core + excitation + Fold) − time(core, mix 0)] / audio time, the two runs taking the same core load; gate ≤ 1.5 %. Also gated: core alone (mix 0) ≤ 1.0 %; whole chain time(core + excitation + Fold) / audio time ≤ 2.5 %. Prints excitation-alone and Fold-alone stage costs (same formula with the other stage off). Also **prints a stereo-forced variant** (pan vector non-zero; the Fold fed L ≠ R) for the record (S11 R-1) |
| SC-019 | `HarmonicFold_OutputBounded` (fold), `ProfundumCore_ExcitationOutputBounded` (chain: SC-009 state, MIDI 12–60, max settings) | \|y\| ≤ ceilings, and the **pre-clamp** peaks `preCeilingPeak()` / `excitationPreCeilingPeak()`, cleared at the start of each fixture, stay strictly below `kOutputCeiling` / `kExcitationCeiling`. The test fails if the design ever relies on the clamp. Both are printed (model ≤ 2.40 / ≤ 1.42, S5.5). `static_assert` both ceilings < 8 |
| — (S4.3, S5.3) | `HarmonicFold_TableLookupAtEndpoint`, `ProfundumCore_ExcitationTableLookupAtCorners` (both in the `-fno-fast-math` nonfinite TUs) | NaN-sentinel layout: amount 100 → exactly T[400], 99.75 → T[399], 0 → T[0]; excitation corners (log₂ D, b) ∈ {−4, 2} × {0, 0.5} → exactly the corner cells. `static_assert`s on the index helpers. A render at amount 100 / max drive with `stateFinite()` |
| SC-020 | `ProfundumCore_ExcitationLevelLaw` (spectral) | RMS over integer periods, ±1.5 dB |
| SC-022 | `ProfundumEdgeFold_RecordedAliasingByOctave` (`[long]`), `_RecordedAliasingRateExtremes`, `ProfundumCore_ExcitationRecordedPhaseAt8Hz`, plus (e) asymmetry ∠T₁, (f) Fold level on non-sine inputs, (g) the as-written SC-002(c) blend residual and (h) the Fold-at-100 inter-channel h1 phase and L/R h1 imbalance at max excitation (C-2, C-8, C-9, C-10) | Print, `REQUIRE` the value is finite, no threshold |
| SC-024 | `HarmonicFold_DetectorTimingConstants`, `HarmonicFold_NoInCycleGainModulation` (fold) | (a) `releaseSeconds()` at the default ≥ 4/20.6; `kDetectorAttackMs` exists (value 0, instant). (b) As written, without the teeth claim, plus the C-3 ripple clause with its double reference |
| SC-025 | `HarmonicFold_SoftRegimeLevelDependent`, `_FoldRegimeLevelNormalised` (fold) | As specified |
| SC-026 | `HarmonicFold_ZeroLatency` (fold), `ProfundumCore_ExcitationZeroLatency` (excitation) | `static_assert(latencySamples() == 0)`. Integer-lag cross-correlation argmax == 0 (lags −8…+8) |
| SC-021, 23, 27 | `node` scripts (S10.3) | Output quoted in comply |

**Listening renders** (`[.listen]`, not a gate): excitation drive, asymmetry and Edge sweeps; a Fold 0 → 100 sweep on the six
named states; the chain at maximum. E0, E1, E2 and C2, written as WAV to `build/…/profundum-phase2-listen/`.

---

## S9. Calibration and evidence record (measured 2026-10-10)

### S9.1 Economy `Oversampler` chain (model of oversampler.h:491–672 / biquad.h)

| fs | 2×: group delay, h1 phase E0 / E1 / E2 | 4×: group delay, h1 phase E0 / E1 / E2 |
|---|---|---|
| 44.1 kHz | 3.00 smp, −0.50 / −1.01 / −2.02° | 6.47 smp, −1.09 / −2.18 / −4.36° |
| 48 kHz | 3.00 smp, −0.46 / −0.93 / −1.85° | 6.47 smp, −1.00 / −2.00 / −4.00° |
| 96 kHz | 3.00 smp, −0.23 / −0.46 / −0.93° | 6.47 smp, −0.50 / −1.00 / −2.00° |

The first downsampling filter attenuates by 11.3 / 21.9 / 33.2 dB (2×) and 8.7 / 15.6 / 22.5 dB (4×) at 0.5 / 0.55 / 0.6·fs.

### S9.2 Excitation static law (calibration sine; `node … exc`)

| Drive, Edge | D_eff | THD (asym 0) | Makeup (asym 0 / 1) | c (asym 0 / 1) |
|---|---|---|---|---|
| 0, 0 | 0.063 | −66.08 | 5.638 / 7.867 | 1.000 / 1.000 |
| 0, 1 | 0.251 | −42.36 | 1.447 / 1.972 | 1.000 / 0.997 |
| 0.5, 1 | 1.000 | −21.64 | 0.464 / 0.526 | 0.997 / 0.982 |
| 1, 0 | 1.000 | −21.64 | 0.464 / 0.526 | 0.997 / 0.982 |
| 1, 1 | 3.981 | −10.99 | 0.289 / 0.290 | 0.962 / 0.959 |

Level law on core inputs at mix 1 (dB re input), for d0a0 / d0a1 / d.5a0 / d.5a1 / d1a0 / d1a1:
- mid: −0.04 / 0.01 / −0.42 / −0.01 / −0.95 / −0.93;
- saw: −0.00 / 0.00 / −0.04 / 0.01 / −0.26 / −0.15;
- sine: ±0.00.

### S9.3 Rendered design (split bypass, both stages 4×, 44.1 kHz, `kFoldIndexMax` 3.0; `node evidence/validate.js 44100`)

| Input, note | Excitation d0: \|T₁\| / ∠T₁ | Excitation max: L / ∠T₁ | Fold 100: \|T₁\| / ∠T₁ / L | Peak (Fold 100) |
|---|---|---|---|---|
| sine E2 | 0.05 dB / −0.13° | 0.05 dB / −0.13° | −9.41 / −0.04° / −3.62 | 0.36 |
| mid E2 | 0.04 / −0.13° | −0.35 / **−10.21°** (C-2) | 0.43 / −1.06° / 2.78 | 0.64 |
| saw E2 | 0.06 / −0.13° | 0.15 / **−15.04°** (C-2) | 2.51 / −1.16° / 1.71 | 0.52 |
| SC-009 E2 | −1.34 / −1.66° | −6.75 / −76.56° | 18.27 / −3.52° / 6.52 | 2.26 |

The round-1 (2.7) Fold columns were −3.53 / 3.17 / 4.68 / 17.24 dB in \|T₁\|. The excitation columns do not depend on
θmax. Across 44.1 / 48 / 96 kHz (`validate.js 48000|96000`), the Fold at 100 keeps ∠T₁ ≤ 1.58° on saw, mid and sine.

Fold continuity on the 0.25 grid (cal sine, `evidence/steps.js`, D-1 law p = 1.5): worst ΔL = ΔCF = 0.109 dB at 93.25,
minimum ΔTHD +0.0693 dB at 100, minimum |T₁| −9.46 dB; worst |ΔC| 0.0319 oct at 96 (`evidence/d1law.js`, bar 0.05, D-1
margin 0.04). Round-2 linear law: 0.191 dB at 99.5, +0.079 dB at 65, −9.46 dB, |ΔC| 0.0751 oct at 100 (the D-1 failure).
At 2.7 these were 0.153 dB, +0.079 dB and −3.59 dB.
Harmonic margins (`evidence/hmargin.js`, D-1 law): at a = 30, h3 −33.6 and h5 −63.6 dB; at 65, h7 −50.4 and h9 −64.8 dB; at
82.5, h13 −45.5 and h15 −52.8 dB (linear law: −51.4 / −59.1); at 100, h15 −17.0 dB.

**Mid grid at C2 (SC-006 centroid, SC-008(a); `evidence/foldmid.js`, 44.1 and 48 kHz identical to the printed precision):**

| `kFoldIndexMax` | C(0) / C(100) oct | Rise (bar ≥ 0.5) | Worst \|ΔL\| / \|ΔCF\| (bar 0.25 dB) | Worst \|ΔC\| (bar 0.05 oct) | Worst \|ΔTHD\| (THD ≥ −60 pairs) vs max(3 × median, 1 dB) | L(100) − L(0) |
|---|---|---|---|---|---|---|
| 2.7 | 1.270 / 1.572 | 0.302: fails | 0.066 / 0.066 dB | 0.020 | 0.126 vs 1.000 dB | +3.72 dB |
| 3.0, linear θ (round 2) | 1.270 / 1.870 | 0.600 | 0.081 / 0.081 dB | 0.033 | 0.267 vs 1.000 dB | +2.71 dB |
| **3.0, ease-out p = 1.5 (D-1)** | 1.270 / 1.870 | **0.600** | **0.072 / 0.072 dB** (@ 78.75) | **0.016** (@ 96.75) | **0.115 vs 1.000 dB** (@ 97.5) | +2.71 dB |

Under the linear law all worst steps sat at the last pair (99.75 → 100); under D-1 they move inside the fold regime
(`evidence/d1law.js`). The steady model uses the windowed-peak detector's steady value (the
waveform peak), so it does not cover SC-008(b)(c), which the C++ test measures directly.

### S9.4 Aliased power, plan design (`node … alias`, worst over SC-009 / Growl / saw / mid / sine; `kFoldIndexMax` 3.0, all nine cells re-run this round)

| Cell | Excitation max | Excitation sym | Fold 30 / 65 / 100 | Chain |
|---|---|---|---|---|
| E0 44.1 kHz | −126.7 | −126.7 | −170.9 / −169.9 / −127.5 | −110.5 |
| E0 48 kHz | −127.8 | −127.8 | −171.4 / −170.1 / −132.1 | −111.0 |
| E0 96 kHz | −140.2 | −140.2 | −171.0 / −169.6 / −164.9 | −117.3 |
| E1 44.1 kHz | −112.0 | −111.9 | −171.8 / −168.1 / −103.3 | −99.9 |
| E1 48 kHz | −114.3 | −114.7 | −172.8 / −171.2 / −104.8 | −101.8 |
| E1 96 kHz | −126.6 | −127.2 | −171.6 / −170.4 / −129.6 | −107.0 |
| E2 44.1 kHz | −93.6 | −93.6 | −166.7 / −128.5 / −97.9 | **−84.8** |
| E2 48 kHz | −95.2 | −95.2 | −170.8 / −132.6 / −95.1 | −89.7 |
| E2 96 kHz | −114.3 | −114.7 | −173.6 / −171.9 / −104.8 | −93.2 |

- Positive control: −48.0 dBFS (must read > −80).
- Worst margin: **4.8 dB** (chain, E2/44.1 kHz, SC-009 state). At 2.7 it was 7.9 dB (−87.9); the Fold-100 and chain columns
  at 2.7 were 0.2–3.2 dB lower throughout (`THETA_MAX=2.7 node … alias`).

History of the rejected configurations at E2/48 kHz:

| Configuration | Excitation | Fold | Chain |
|---|---|---|---|
| Economy 4×, no pre-filter | −67.5 | −65.4 | — |
| Economy 4× + 8-pole pre-filter at 0.40·fs | −80.8 | −83.8 | — |
| Halfband, no pre-filter | −59.3 | −53.0 | — |
| Halfband 2×, 3 notches | −73.5 | — | — |
| 2 notches, Fold at 2× | — | — | −82.6 (44.1 kHz) |
| +24 dB drive ceiling | −80.8 | — | — |

### S9.5 CPU (C++ microbenchmarks, clang −O2 −ffast-math; `bench/`)

| Measurement | Result |
|---|---|
| Economy `Oversampler<4,2>` up + down, stereo | 2.226 / 2.264 / 2.282 % (unpinned) |
| Economy `<2,2>` up + down, stereo | 0.655 / 0.611 / 0.666 % |
| Halfband (`OnePoleAllpass`), stereo, 4× / 2× | 0.40–0.48 % / 0.21–0.23 % |
| **Plan design, both stages, stereo, pinned P-cores (`tools/pin-perf-cores.ps1`, mask 0xFFFF)** | 1.454 / 1.410 / 1.370 % (excitation share 0.63–0.64 %, Fold 0.79–0.85 %) |
| The same with two notches | 1.362 / 1.417 / 1.336 % |
| **Round 2, `bench/candidate-design-bench-v2.cpp`, pinned, mask 0xFFFF, 3 reps** (adds what v1 omitted: the excitation's three coordinate smoothers, per-sample `exp2` and bilinear (M, c) lookup, smoothstep w and the ADAA offset; the Fold's amount smoother, five clamped table lerps, β/w, the windowed-peak detector with instant attack and env flush; SC-018's 0.5 Hz drive automation and 0.5 Hz 0–100 Fold sweep) | **both 1.308 / 1.315 / 1.321 %**; excitation 0.629 / 0.688 / 0.637 %; Fold 0.688 / 0.728 / 0.739 % |
| **Re-run 2026-10-10, T005, pinned, isolated, median of 5** (`bench/candidate-design-bench-v2.cpp`, clang 21.1.8, the file's compile line; 5 runs 20 s apart via `tools/pin-perf-cores.ps1`, mask 0xFFFF; no build, suite or `node` process running, total CPU 0.2–1.7 % idle after; each run's figure is the median of its 3 reps) | runs 1–5: **both 1.467 / 1.408 / 1.462 / 1.469 / 1.531 %**; excitation 0.741 / 0.715 / 0.718 / 0.751 / 0.621 %; Fold 0.775 / 0.790 / 0.785 / 0.824 / 0.747 %. **Median: both 1.467 %, excitation 0.718 %, Fold 0.785 %.** Single-rep range: both 1.377–1.581 %. Above the 1.45 % flag line: early R-1 warning for T013 (SC-018 C++ gate) |

These are **estimates, not the SC-018 gate**:
- The gate runs in the MSVC Release build through `run-cpu-tests.js`.
- The benches use `OnePoleAllpass`, which carries per-call checks, so S3's lean sections can only be cheaper.
- v2 reads lower than v1 although it does more work. The sinusoidal Fold sweep spends ≈ 60 % of each cycle at a ≤ 65 (½ + asin(0.3)/π), where θ < 0.001
  skips the `std::sin` (S4.3), and v2's input differs (8 sines, not 1). A static amount of 100 costs more than the sweep.
  SC-018 specifies the sweep, so v2 is the like-for-like estimate; R-1 keeps the static-100 figure in view.
- Neither bench includes the core, so neither measures the core-alone or whole-chain clauses; those are measured only by the
  C++ gate.
- Run conditions: run alone in this session, no other build or suite executing; one 3-rep run, not the isolated median-of-5
  protocol. Before tasks are cut, v2 is re-run once more under `run-cpu-tests.js`-equivalent isolation and its figure pasted
  here (S12 step 2).

### S9.6 SC-013(c) figures

- Static shared-detector L/R ratio: C-4 table (`evidence/sc013c.js`).
- Step clause, time-domain detector: C-4 step table (`evidence/sc013step.js`). Worst excursion 0.453 dB with the round-1 2 ms
  attack, 0.000 dB with the planned instant attack.
- Fold at 100 on the panned max-excitation output (phase, mono-sum as written, coherent-sum cancellation, L/R imbalance):
  C-10 table (`evidence/foldpan.js`, `evidence/foldpan2.js`), at E1 and C2, mid and saw, asymmetry 0 and 1, plus the mix-0 core
  input. Worst values at 3.0: Fold-added phase 19.77° at max excitation and 1.95° on the core alone; mono clause as written
  −2.42 dB; against the coherent sum −0.16 dB.

### S9.7 SC-023 — compression mechanism record (FR-035)

| Candidate | SC-007(a): THD / H / ΔL / ΔCF | (b) | (c) median | Cost | Status |
|---|---|---|---|---|---|
| (a) envelope-normalised waveshaper (`x·G` → saturate → `/G`, β = 1 from a = 40) | +13.99 dB / 1 → 3 / +0.68 / −0.68 | Homogeneous by construction; the C++ gate measures it | 5.49 dB | Fold total 0.79–0.85 % (S9.5) | **Chosen**: passes and is the cheapest |
| (b) split-band limiter | — | — | — | — | Not built: (a) passes (FR-035 "keep the cheapest that passes") |
| (c) parallel harmonic generator | — | — | — | — | Not built, same reason |
| (a) as built, C++ gate (T018 per-push `dsp_processors_tests`, 2026-10-11, D-1 p = 1.5 and D-4 detector in place) | E1: +13.99 dB / 1 → 3 / 0.681 / 0.681; C2: +13.99 dB / 1 → 3 / 0.681 / 0.671 (bars ≥ 10 dB, ≥ 1.5×, ±1 dB) | Fold THD spread over −24…−6 dBFS 0.00 dB at E1 (−25.89) and C2 (−25.93), bar 3; tanh teeth 31.66 / 31.68 dB, bar 12 | 5.534 dB at E1 (bar 3); SC-022(d) RMS-matched tanh 0.374 dB | SC-018 in C++, T020 isolated lane 2026-10-11, before the cost fix below: stage cost 1.350 % (bar 1.5), Fold alone 0.890 %, excitation alone 0.361 %, chain 1.985 % (bar 2.5), core 0.634 % (bar 1.0). Comply re-runs (pinned, not isolated) read stage cost 1.76 / 1.42 / 1.55 %, so the 10 % margin did not hold | Measured: passes SC-007 in C++; SC-018 marginal |
| (a) as built after the comply cost fix (2026-10-11): `HarmonicFold` reuses the detector's 4x interpolation for the residual at 4x, and runs channel 1 only once its input differs from channel 0's (mono link). The output is unchanged except for the first samples after engaging from idle, where the interpolator is now warm, as the core excitation's is | Unchanged: the SC-007 tests pass on the new build (`dsp_processors_tests`, `[edge-fold]`) | Unchanged | Unchanged | SC-018 in C++, pinned with `tools/pin-perf-cores.ps1`, 3 runs 20 s apart (log below): stage cost **0.848 / 0.961 / 0.899 %** (bar 1.5), Fold alone 0.455 / 0.453 / 0.459 %, chain 1.519 / 1.587 / 1.558 % (bar 2.5), core 0.670 / 0.626 / 0.659 % (bar 1.0) | **Chosen**: passes SC-007 and fits inside SC-018 with about 35 % margin. Confirmed alone on pinned cores at the phase close (2026-10-11, `dsp_systems_tests "[profundum][.perf]"`, nothing else running): core alone 0.689 % (bar 1.0), stage cost 1.019 % (bar 1.5), chain 1.708 % (bar 2.5); excitation alone 0.494, Fold alone 0.561; stereo-forced chain 2.462 (record only); Phase 1 core 0.771 / 1.001 (96 partials) / 0.336 static |

**C++ SC-018 log** (`ProfundumEdgeFold_CpuBudget`, `profundum_edge_fold_perf_test.cpp`; % of one core per voice, median of 5).

| Run | Code | Core alone (A, bar 1.0) | Excitation alone | Fold alone | Stage cost D − A (bar 1.5) | Chain D (bar 2.5) | Stereo-forced stage cost (record) |
|---|---|---|---|---|---|---|---|
| T020 lane, isolated, `run-cpu-tests.js`, 2026-10-11 (`f:/tmp/cpu_dsp_systems_tests.log`) | before the fix | 0.634 | 0.361 | 0.890 | 1.350 | 1.985 | 1.311 |
| Comply, pinned, 3 runs, no other test process (reported by comply) | before the fix | 0.694 (run 2) | — | — | 1.760 / 1.419 / 1.550 | 2.521 / 2.113 / 2.296 | 1.690 / 1.532 (runs 2–3) |
| Fix session, pinned, 3 runs 20 s apart, no build or test process in `tasklist` at start | before the fix | 0.540 / 0.664 / 0.650 | 0.547 / 0.449 / 0.435 | 1.030 / 0.984 / 0.949 | 1.621 / 1.444 / 1.435 | 2.161 / 2.108 / 2.085 | 1.629 / 1.540 / 1.413 |
| Same conditions | shared interpolator only | — | 0.474 / 0.475 / 0.466 | 0.845 / 0.786 / 0.795 | 1.312 / 1.267 / 1.266 | 1.954 / 1.940 / 1.920 | 1.409 / 1.383 / 1.468 |
| Same conditions | shared interpolator + mono link (shipped) | 0.670 / 0.626 / 0.659 | 0.406 / 0.347 / 0.454 | 0.455 / 0.453 / 0.459 | **0.848 / 0.961 / 0.899** | **1.519 / 1.587 / 1.558** | 1.196 / 1.388 / 1.371 |
| Re-verify 2026-10-11, pinned (`pin-perf-cores.ps1`, mask 0xFFFF), alone: 3 runs 20 s apart, no build or test process at each start, `dsp_systems_tests "ProfundumEdgeFold_CpuBudget"` | shipped | 0.971 / 0.949 / 0.910 | 0.547 / 0.607 / 0.584 | 0.534 / 0.523 / 0.466 | **1.224 / 1.144 / 1.275** | **2.194 / 2.093 / 2.185** | 1.932 / 2.012 / 1.814 |

The fix-session runs were pinned but not run through `run-cpu-tests.js`, and other workflow agents may have been
running. They are A/B readings taken minutes apart, not the gate. The gate is the isolated lane
(`node tools/run-cpu-tests.js dsp_systems_tests`, or `dsp_systems_tests "[profundum][.perf]"` alone and pinned),
which must be re-run on the shipped code before the phase commits.

---

## S10. Build integration

### S10.1 CMake (`dsp/tests/CMakeLists.txt`, enumerated lists)

| Location | Change |
|---|---|
| `dsp_primitives_tests` list (after `oversampler_test.cpp`, ≈ :99) | Add `unit/primitives/polyphase_halfband_oversampler_test.cpp` |
| `dsp_processors_tests` list (after `spectral_shape_recipe_test.cpp`, :254) | Comment block, then `unit/processors/harmonic_fold_test.cpp` and `unit/processors/harmonic_fold_nonfinite_test.cpp` |
| `dsp_systems_tests` Profundum block (:564–597) | Comment block, then `profundum_core_excitation_test.cpp`, `profundum_core_excitation_spectral_test.cpp`, `profundum_edge_fold_chain_test.cpp`, `profundum_core_excitation_nonfinite_test.cpp`, `profundum_edge_fold_perf_test.cpp` |
| `-fno-fast-math` block (≈ :1056–1078) | Add both `*_nonfinite_test.cpp` with a one-line reason each. The perf TU stays out |

No production CMake changes: the DSP library is header-only. No plugin target is touched.

### S10.2 Commands (Windows, full CMake path) and parallelism

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_primitives_tests dsp_processors_tests dsp_systems_tests
# per-push lanes, run CONCURRENTLY (not timing-sensitive); capture to logs (never re-run a slow suite for output)
build/windows-x64-release/bin/Release/dsp_primitives_tests.exe "[polyphase-halfband]" > p.log 2>&1 &
build/windows-x64-release/bin/Release/dsp_processors_tests.exe "~[long]~[.perf]~[.listen]" > pr.log 2>&1 &
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "~[long]~[.perf]~[.listen]" > s.log 2>&1 &
# [long] lane, also concurrent: the SC-005 grid shards by note
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "[edge-fold][long]" > sl.log 2>&1 &
wait
# CPU lane, ALONE, after everything else has finished and the machine has idled:
node tools/run-cpu-tests.js dsp_systems_tests
```

- One rebuild covers all three targets.
- The Phase 1 suites run unmodified inside the same executables (SC-004(b), SC-021).
- No shared header changes (C-1 adds a new file only), so the Ruinae, Seraphis, Vorago and Innexus suites are not
  affected. Comply records `git diff --stat 47e43918 -- dsp/include` to prove it.

### S10.3 Scripts (Node, in the spec folder)

| Script | Purpose |
|---|---|
| `extract-phase1-ref.js` | SC-004(a) reference generation plus the namespace-only diff print |
| `check-footprint.js` | SC-021, following Phase 1's allow-list pattern (`specs/profundum-phase1-harmonic-core/check-footprint.js`: per-file added/removed line sets). Zero diff lines in the FR-048 shared list, `spectral_shape_recipe.h`, every Phase 1 test TU and both Phase 1 helpers. For `profundum_core.h`: every Phase 1 public declaration present verbatim; removed lines allowed only (i) the two banner lines at :5–6 (S5.1) and (ii) lines inside the member bodies listed in S5.6, located by brace-matching from each member's signature; the FTZ note :37–38 must survive verbatim (S7). Also: the SC-026 grep for `Standard`, `High` and `LinearPhase` in the two Phase 2 headers, and the S8.1 grep that no file under `dsp/tests/unit/processors/` added by Phase 2 includes `systems/` |
| `check-bars.js` | SC-027 (extracts every SC threshold at build start and at comply, diffs them, and requires a Clarifications re-ruling entry for each difference) and the SC-023 table presence check |

### S10.4 Expected runtimes

| Lane | Estimate |
|---|---|
| Per-push lanes | < 60 s each |
| SC-005 `[long]` grid | 9 cells × 5 inputs × 6 fixtures, FFT up to 2¹⁹ in double: estimated 3–6 min, sharded by note into three concurrent processes |
| SC-006/008 `[long]` 401-point sweeps | ≈ 1–2 min |
| CPU lane, `node tools/run-cpu-tests.js dsp_systems_tests` | **Not 3 min.** `run-cpu-tests.js:56` filters `[performance],[perf],[.perf],[benchmark],[!benchmark],[long]~[vorago-sweep]`, so it runs the SC-018 perf case **and every `[long]` case in `dsp_systems_tests`**, serially: Phase 2's SC-005 grid and recorded-aliasing cases (3–6 min sharded, so longer serial), Phase 1's `[long]` cases, and every other plugin-independent `[long]` system test. Estimate it from the previous `run-cpu-tests.js` log's wall clock for that target before launching, not from this table |
| SC-018 gate alone (planned) | `pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe build/windows-x64-release/bin/Release/dsp_systems_tests.exe -ExeArgs "[.perf][edge-fold]"`: the one perf case, pinned, ≈ 1–2 min (5 × 10 s renders × 4 configurations + warm-up). This is the S12 step 6 early measurement and the comply gate. The full CPU lane runs once at phase close, since it repeats the `[long]` renders the concurrent `[edge-fold][long]` lane already ran |

The first real run records the wall-clock figures in tasks.md for the next estimate.

---

## S11. Risks and mitigations

| # | Risk | Mitigation |
|---|---|---|
| R-1 | **CPU margin is thin**: 1.31–1.32 % estimated with the full per-sample control path under the SC-018 automation (v2 bench), 1.37–1.45 % in v1, against 1.5 %. MSVC may differ, and a static amount of 100 costs more than the sweep | Measure SC-018 first thing after the stages compile (S12 step 6). Ladder, applied in order and re-measured after each step; never relax the bar: (1) run the split LPo at the 2fs stage instead of F·fs (the subtraction moves after the inner decimation; error at h1 ≤ 0.3°, re-verify SC-012); (2) when the core's pan is centred (`!panAppliedNonZero_`, `profundum_core.h:687`, L == R guaranteed) process channel 0 and copy its state and output to channel 1 (all state is copyable); the Fold does the same when its input block is bitwise L == R; (3) 2× for the Fold, with the chain margin re-measured (2× measured −82.3 at θmax 2.7 and was not re-run at 3.0, so this applies only if the model at 3.0 and the C++ gate both show margin). If still over: stop and re-rule in the main loop with the measurements |
| R-2 | Zero-in-zero-out exactness under fast-math (S7) | SC-011 and SC-014 catch it. Fallback: a single `static` helper for both expressions |
| R-3 | H-count fragility (−60 dB threshold) | Model margins of 3.6 dB (h5 at 30) and 4.8 dB (h9 at 65); h15 at 82.5 sits 0.9 dB above the threshold at 3.0. If C++ measurement noise flips a count, retune `kSoftDriveAtSoftEnd` (0.43 → 0.40 gains 1.3 dB) with the model and record it |
| R-4 | SC-013(c) steady clause passes by 0.02 dB with the shared detector | C-4. The step clause needed the instant attack (S4.4). If per-channel detection is not ratified and the C++ figure misses, the evidence goes to the main loop |
| R-5 | Biquad's NaN reset (`biquad.h:354–357`) inside the residual path | Inputs are sanitised before any stage, so a reset can never fire. `stateFinite()` covers `getZ1`/`getZ2` |
| R-6 | `DCBlocker` R clamp (0.9999) | At 192 kHz the effective corner is 3.06 Hz, documented. Gated rates ≤ 96 kHz use the exact 2 Hz |
| R-7 | Denormal stalls in the halfband sections | Per-component FTZ/DAZ preconditions (S7); the Fold detector env is flushed explicitly. The allocation/CPU test runs under the FTZ main. If an FTZ-less host path appears in Phase 10, add `flushDenormal` on the section state then; it costs about 2 % of the stage |
| R-8 | Excitation h1 phase at asymmetry > 0 (C-2) feeding the Phase 3 Anchor join | Recorded SC-022(e); Phase 3 owns its 1 dB gate |
| R-9 | Large pitch jumps (E-13) | Bounded and finite only (asserted in `ProfundumCore_ExcitationOutputBounded`) |
| R-10 | Model–C++ divergence (model in double, C++ in float) | Every gate is re-measured in C++. The model is evidence for feasibility, not compliance |
| R-11 | `kFoldIndexMax` 3.0 margins (C-11) | Mid-grid centroid rise 0.600 oct (bar 0.5); 2.95 gives 0.541, so the window above the bar is narrow. Chain aliasing margin 4.8 dB (was 7.9). C-10's (c-i) Fold-added phase on the mix-0 core is 1.95° against 2° (1.09° at 2.7). If the C++ centroid misses, θ moves up in 0.05 steps, re-running the whole S9.3/S9.4 evidence each time. If (c-i) misses, the measurements go to the main loop together with C-10's mid/side alternative. The two pull in opposite directions, so θ is never tuned against one bar alone |
| R-12 | SC-009 margin 6 % with coordinate smoothing (worst 1.038, S5.3) | The test sweeps 16 step phases, as the model did. If C++ misses, lengthen `kExcitationSmoothMs` (5 → 10 ms), re-model with `ctlphase.js` and record it; never relax the 1.1× bar |

---

## S12. Implementation order (input to tasks.md)

1. **Ratify S1** (C-1 to C-11) in the main loop. Encode the re-rulings into spec.md's Clarifications log. `check-bars.js`
   then baselines the bars.
2. Reproduce the evidence: `node …/edge-fold-model.js` (all reports), `halfband-design.js` and every `evidence/*.js` script
   (the round-2 ones: `mixlin`, `ctlsteps`, `ctlphase`, `foldpan`, `foldpan2`, `sc013step`, `foldmid`, `foldmidC`). Run them
   as concurrent processes: they are independent, and `ctlphase.js` alone takes ≈ 5 min. Re-run
   `bench/candidate-design-bench-v2.cpp` alone and pinned, and paste its figure into S9.5. Paste the outputs into tasks.
3. `PolyphaseHalfbandOversampler` + test (primitives). Build `dsp_primitives_tests`; the response tests must pass first.
4. `HarmonicFold` header, tables and detector; tests first (failing), then implementation. Gates: SC-006, 007, 008, 010,
   011, 013(a)(c) shared-detector clause, 015, 016, 017, 019 (pre-clamp), 024, 025, 026, and the S4.3 table-endpoint test.
   SC-013(c)'s first clause needs the core, so it runs in step 7.
5. `extract-phase1-ref.js` → `profundum_phase1_core_ref.h`. Then the core excitation: tests first, then implementation.
   Gates: SC-001, 002 (C-9 form), 003, 004, 009 (16 step phases), 011, 012(a)–(c), 013(a)(b), 014, 015, 016 (extended
   `stateFinite`, S5.6), 017, 019 (pre-clamp), 020, 026, and the S5.3 corner test.
6. **SC-018 early**: `ProfundumEdgeFold_CpuBudget` alone and pinned by name (S10.4), with the full SC-018 load. Then the R-1
   ladder if needed.
7. The chain TU: SC-005 grid and positive control, SC-010/011/012(d) core arms, SC-013(c) first clause (C-10), SC-017 rate
   change, SC-019 MIDI sweep, SC-022 records (e)–(h), `ProfundumEdgeFold_HelperParity`, listening renders.
8. `check-footprint.js`, `check-bars.js`, `node tools/check-portability.js`, `tools/lint-apple-globals.js`, and clang-tidy
   `-Target dsp`, all concurrently. Then the full CPU lane once, alone (S10.4). Then comply.

## Review notes

Plan written 2026-10-10. Rejected alternatives and their numbers are kept in S9 so the build need not re-derive them.
Every plan number is reproducible from the spec folder:
- `edge-fold-model.js`: S9.1, S9.2, the S4.6 sweep and S9.4;
- `evidence/validate.js`: S9.3 and S5.5;
- `evidence/staticphase.js` and `evidence/panphase.js`: C-2;
- `evidence/sc013c.js`: C-4 and S9.6;
- `evidence/steps.js` and `evidence/hmargin.js`: the S9.3 continuity and margin lines;
- `evidence/posctl.js`: C-6;
- `bench/`: S9.5;
- round 2: `evidence/mixlin.js` (C-9), `ctlsteps.js` and `ctlphase.js` (S5.3), `foldpan.js` and `foldpan2.js` (C-10),
  `sc013step.js` (C-4 step clause), `foldmid.js` and `foldmidC.js` (C-11, S9.3 mid grid), `bench/candidate-design-bench-v2.cpp`
  (S9.5). `evidence/lib.js` loads the model internals for them and adds the per-sample S5.4 stage (`ExcStage`).

The evidence wrappers load the model with its final defaults (`DRIVE_MAX_DB=12`, halfband, 3 notches, 500 Hz split,
`G_CONT=1`, `THETA_MAX=3.0`).

### Review round 2 (2026-10-10): disposition of the thirteen issues

Every issue was accepted; none was rejected. Two of them turned out larger than reported, and one uncovered a failure the
review did not name:

| Issue | Disposition |
|---|---|
| Blocker: SC-002(c) mix blend cannot meet −60 dB | Confirmed by model (−53.6 to −60.5 dB). Resolution (a) adopted as **C-9 ⚠ RE-RULE**: blend measured between non-zero mixes (y(0.55) vs ½(y(0.1) + y(1)), ≤ −300.9 dB in double), Edge 0 vs Edge 1, bar unchanged. Option (b) rejected: it breaks SC-004(a). S8.2 SC-002 row and S5.5 updated; the as-written figure becomes SC-022(g) |
| Major: independent smoothing of D and M | Confirmed and worse than estimated: worst-phase SC-009 ratio 3.974, +13.8 dB bump. Fixed in S5.2–S5.4: smooth (log₂ D, b), look up M and c per sample. Worst 1.038 (R-12). S9.5 re-benched with the full control path |
| Major: SC-013(c) Fold on panned input | Modelled (C-10). The as-written clause fails at both θ values and even at asymmetry 0, so the review's option (b) does not rescue it. The mono failure is L/R h1 imbalance, not cancellation (coherent-sum loss ≤ 0.16 dB). **C-10 ⚠ RE-RULE** proposed, with a mid/side Fold as the design alternative |
| Major: SC-013(c) step clause | Confirmed: 0.453 dB excursion with the 2 ms attack. Fixed with an instant attack (`kDetectorAttackMs = 0`), modelled excursion 0.000 dB (C-4, S4.4) |
| Minor: clamp hides SC-019 | Pre-clamp peak observers added (S4.1, S5.1) and asserted below the ceilings (S8.2) |
| Minor: SC-018 row omits the load | Row now carries the spec's load and stage-cost formula verbatim. The bench was re-run with the automation (S9.5) |
| Minor: mid-grid evidence for SC-006 / SC-008 | Added (S9.3). **This found a hard failure**: at θmax 2.7 the mid-grid centroid rises 0.302 oct against the 0.5 bar. Fixed by **C-11**, θmax 3.0 (0.600 oct). All Fold evidence was re-run at 3.0: S4.6, S9.3, all nine S9.4 cells, S5.5 peaks, C-8 |
| Major: table index OOB at amount 100 | Clamped index helper, `static_assert`s, `(1 − t)·a + t·b` lerp so t = 1 returns the endpoint exactly, and a NaN-sentinel test (S4.3). The same for the excitation's bilinear table (S5.3) |
| Major: processors helper includes a systems header | Fixed (S8.1): the descriptor functions are duplicated into `HarmonicFoldTest` (the Phase 1 helper stays frozen per SC-021), with a systems-level bit-parity test and a footprint grep |
| Minor: Fold FTZ precondition | Own `@note` on `HarmonicFold`; S7 reworded per component; detector env flushed explicitly (S4.4) |
| Minor: `profundum_core.h:5–6` banner | Banner update scheduled (S5.1) and allow-listed in `check-footprint.js` (S10.3); the :37–38 FTZ note stays verbatim, with one line added |
| Minor: CPU lane runtime | Confirmed at `run-cpu-tests.js:56`: the lane includes every `[long]` case. S10.4 restated; the SC-018 gate runs by name, pinned |
| Minor: `stateFinite` / `prepare` / `reset` bodies | S5.6 lists every Phase 1 body edit. `DCBlocker` exposes no state accessor (dc_blocker.h:240–241), so the core mirrors its last input and output |

Not done in this round, deliberately: the mid/side Fold alternative of C-10 was not modelled. It is a sound-design choice that
changes FR-033's reading, and it belongs to the ratification discussion, not to a plan revision.
