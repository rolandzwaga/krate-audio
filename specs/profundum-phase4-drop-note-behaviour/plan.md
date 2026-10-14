# Implementation Plan: Profundum Phase 4 — Drop & Note Behaviour

**Spec:** `specs/profundum-phase4-drop-note-behaviour/spec.md` (Clarified 2026-10-12)
**Roadmap:** `specs/Profundum-roadmap.md` Part A, Phase 4
**Phase base:** commit `3d8ca19f` on `synth/profundum` (Phase 3 Sub Anchor)
**Status:** Planned 2026-10-12. **Section S1 holds sixteen items to ratify before `tasks.md`.** Eight are flagged
⚠ RE-RULE because the spec as written cannot be met, two of its bars conflict, or the plan changes a spec bar:
- **C-2:** the phase integral has no elementary closed form, and the spec's "integral of the blend is the blend of
  the integrals" is false.
- **C-3:** the excess phase must be predicted on the core's 16-sample pitch staircase and its [8 Hz, cap] clamp. A
  continuous integral misses by up to 37.6°.
- **C-4:** a "nearest integer" landing target can be infeasible.
- **C-5:** SC-013(c)'s float measurement needs a 1-ulp floor on top of 1.05 × the analytic peak.
- **C-6:** SC-013 at T = 10 ms conflicts with SC-015's slope bound in 23 of 384 cells.
- **C-7:** FR-006's "d crosses 0 at t_L" is impossible for the exponential family with Rebound.
- **C-8:** E-4's bound widens to [−ρ_max − 2.7 st, A], and FR-007's bridge length becomes a rate-dependent law.
- **C-9:** the Drop's tail upswings cannot be slope-bounded without breaking SC-003 and FR-010.

The other eight items are refinements with no bar change.

Every number in this plan was **measured this session** by one tool:

| Artifact | What it is | Reproduce |
|---|---|---|
| `drop-model.js` | A double-precision model of the S3 curve, the S5 predictor and correction, a staircase-exact render of the core's h1 excess phase (16-sample hold, [8 Hz, cap] clamp), the S4 glide laws, the SC-015 slope bound and the solver-independent SC-013(c) bound | `node specs/profundum-phase4-drop-note-behaviour/drop-model.js [shape\|integral\|lock2all\|lock3 4\|perSample\|slope\|nyquist\|glide\|bound]` |

The CPU figures in S10.7 are **estimates**, not measurements. Task T001 is a microbenchmark that must run before any
header is written (S13 R-1).

---

## S0. Verification ledger (every fact below was read in this session)

| Fact | Source |
|---|---|
| `MonoHandler` is Layer 2 and includes `db_utils.h`, `midi_utils.h`, `pitch_utils.h` and `smoother.h` | `processors/mono_handler.h:2, 22–25` |
| `enum class PortaMode : uint8_t { Always = 0, LegatoOnly = 1 }` | `mono_handler.h:63–66` |
| `kMaxPortamentoTimeMs = 10000.0f`; the constructor configures `portamentoRamp_` at 0 ms | `mono_handler.h:100, 109–112` |
| `prepare` reconfigures the ramp and keeps the glide position | `mono_handler.h:121–125` |
| `noteOn`: `hadNotesHeld = (stackSize_ > 0)` (152). Legato retrigger suppression (176–179). Glide gating: first note snaps, an overlap always glides, staccato glides only under `PortaMode::Always` (186–199). It calls `updatePortamentoTarget(winner, enableGlide)` (199) | `mono_handler.h:135–206` |
| `noteOff`: a return to a held note calls `updatePortamentoTarget(winner, true)` and retriggers iff `!legato_` | `mono_handler.h:237–256` |
| `noteOff` of the last held key returns `makeInactiveEvent()` (`isNoteOn == false`); releasing a held key that is not the active one returns `isNoteOn = true`, `retrigger = false` and the unchanged active frequency (FR-013). There is no active-note getter, only `hasActiveNote()` | `mono_handler.h:259–262, 265–270, 340–342` |
| `setPortamentoTime` ignores NaN/Inf, clamps to [0, 10000] and calls `portamentoRamp_.configure` | `mono_handler.h:280–286` |
| `processPortamento()` returns `semitoneToFrequency(portamentoRamp_.process())` | `mono_handler.h:291–295` |
| `setMode` re-evaluates the winner and calls `updatePortamentoTarget(winner, true)` | `mono_handler.h:309–321` |
| `reset()` snaps the ramp to its current value and clears `hadPreviousNote_` | `mono_handler.h:349–356` |
| `updatePortamentoTarget`: `setTarget` iff `enableGlide && portamentoTimeMs_ > 0`; otherwise `snapTo` plus `currentFrequency_` | `mono_handler.h:450–457` |
| `semitoneToFrequency(s) = kA4FrequencyHz · semitonesToRatio(s − 69)`, and `semitonesToRatio` is `std::pow(2.0f, s/12.0f)` | `mono_handler.h:461–463`; `core/pitch_utils.h:23–26` |
| `midiNoteToFrequency` uses `constexprExp`, not `pow` | `core/midi_utils.h:71–78` |
| `LinearRamp::configure` recomputes the increment over the remaining distance with the full ramp time | `primitives/smoother.h:329–336` |
| `LinearRamp::setTarget` recomputes the increment through `calculateLinearIncrement` in float | `smoother.h:342–354, 100–108` |
| `LinearRamp::process` does `current_ += increment_` in float, clamps and flushes denormals; `getTarget()` exists | `smoother.h:370–388, 358–361` |
| `ProfundumCore::RetriggerPhase { Reset, FreeRunning }`; `kControlInterval = 32`; `kPitchUpdateInterval = 16`; `kMinF0Hz = 8.0f`; `kMaxBaseStepSemitones = 0.9f` | `systems/profundum_core.h:94–102` |
| `processBlock`: at each grid phase `% 16 == 0` it samples `trajectoryF0` at element `i` (control update at phase 0, pitch update otherwise) and renders a run up to the next boundary | `profundum_core.h:240–275` |
| `trajectoryF0` clamps to [`kMinF0Hz`, `capHz_`] | `profundum_core.h:805–811` |
| `controlUpdate` rebuilds the mask at `f0` only when `f0` moved | `profundum_core.h:476–482` |
| `pitchUpdate` only refreshes the multipliers; the mask stays stale until the next control update | `profundum_core.h:510–516` |
| `applyNoteOn`: the Reset branch (`policy_ == Reset \|\| !sounding_`) does `bank_.reset()`, `intervalPhase_ = 0` (grid restart), `buildMask(f)`, "first sample is 0.0" | `profundum_core.h:519–558` (branch at :528) |
| `sounding_` is cleared only by `reset()` (and so `prepare`) and set by every noteOn, in both the core and the Anchor. A voice whose ADSR has released is still "sounding" to both oscillators, so a FreeRunning noteOn on it keeps both phases | `profundum_core.h:441, 602`; `anchor_oscillator.h:169, 481–483, 517` |
| The FreeRunning branch rebuilds the mask only if f moved | `profundum_core.h:559–600` |
| `computeEpsilonTargets` freezes every partial with `n·f ≥ capHz` at the cap frequency | `profundum_core.h:700–712` |
| Observers: `deliveredGains()`, `currentFrequency()` (f0 of the last pitch update), `maskFrequency()`; no phase observer | `profundum_core.h:290–305` |
| `capFrequency(fs) = 0.8 · fs/2` (`kNyquistCapFraction`) | `processors/spectral_shape_recipe.h:72, 93–95` |
| `evaluateMask`: the mask is exactly 0 at and above the cap, with a dB-linear taper over 1430 cents below it | `spectral_shape_recipe.h:149–190` |
| `kSineAnchor`, `kSawAnchor`, `kGrowl` | `spectral_shape_recipe.h:231–250` |
| `AnchorOscillator`: `RetriggerPhase` "same values as ProfundumCore"; `kPitchUpdateInterval = 16`; `kMinF0Hz = 8` | `systems/anchor_oscillator.h:59, 62–64` |
| `noteOn(f0, rearm = true)` is effective at offset 0 of the next block | `anchor_oscillator.h:275–281` |
| `applyNoteOn`: `fromSilence = !sounding_`, and silence always re-arms | `anchor_oscillator.h:474–483` |
| Anchor observers `modeGain()`, `phase()`, `phaseIncrement()`, `isSounding()` | `anchor_oscillator.h:356–359` |
| `ADSRStage { Idle, Attack, Decay, Sustain, Release }`; `gate(true)` under Hard calls `enterAttack()` | `primitives/adsr_envelope.h:42–48, 86–112` |
| `getStage()`, `isActive()` | `adsr_envelope.h:265–266` |
| `detail::isFinite(float/double)`, `isNaN`, `isInf` are fast-math-immune bit tests | `core/db_utils.h:99–136` |
| `kPi` (float) | `core/math_constants.h:28` |
| `ClickDetector(const ClickDetectorConfig&)`, `prepare()`, `detect(const float*, size_t)` | `tests/test_helpers/artifact_detection.h:99–135` |
| `fingerprintRender(span<const float>)`; `compareFingerprints(actual, reference, metricTolerance = kMetricTolerance, sampleTolerance = kSampleTolerance)` | `tests/test_helpers/render_fingerprint.h:73, 122–124` |
| `AllocationScope` latches its count in the **destructor** (`count_ = …stopTracking()`), so `AllocationScope::getAllocationCount()` reads 0 while the scope is open. An open scope is read through `AllocationDetector::instance().getAllocationCount()`, which returns the live counter | `tests/test_helpers/allocation_detector.h:70–72, 111–123, 145–147` |
| The Phase 3 C¹ checker `anchorCheckC1(span<const double> g, span<const AnchorSegmentSpec> segs, lo, hi)` with a 1.05 tolerance. `AnchorSegmentSpec` has `enum class Kind { Hold, Cosine, Hermite }` and the fields g0, a, gp0, g1, start, len, with **no bound field**. `anchorSegmentD2Bound` computes every bound internally, and the checker takes no bound callback | `dsp/tests/unit/systems/anchor_oscillator_test_helpers.h:118–126, 131–135, 145–160, 165–211` |
| `goertzelPhase(x, L, fs, hz)`, BH7-windowed | `dsp/tests/unit/systems/profundum_core_test_helpers.h:157` |
| CMake: `dsp_primitives_tests` starts at line 87, `dsp_processors_tests` at 158 (where `mono_handler_test.cpp` sits, line 232), `dsp_systems_tests` at 331; the last Phase 3 entry (`anchor_oscillator_perf_test.cpp`) is at line 651, with the closing paren at 652; the `-fno-fast-math` list's last entry is at 1138 and its `PROPERTIES` line at 1139. Tasks re-derive every insertion anchor from the live file | `dsp/tests/CMakeLists.txt` |
| `tools/run-cpu-tests.js`, `tools/check-portability.js`, `tools/lint-apple-globals.js` and `tools/pin-perf-cores.ps1` exist | `tools/` |

---

## S1. Spec conflicts and refinements — RATIFY BEFORE TASKS

**RATIFIED 2026-10-12** (spec Clarifications "Session 2026-10-12 — plan S1 ratification and open questions"): the user ruled
**O-1 = re-rule SC-015 to the rendered aliased-power measurement** (Phase 1 SC-009 estimator, ≤ −96 dBFS) and **O-4 = no
correction ceiling below 48 st**, then accepted C-1 … C-5, C-7 … C-9 and C-11 … C-16 as written. **O-1 supersedes, in this plan:**
C-6 (no γ_cap, no infeasible cells — γ_lim = 48 and every SC-013 cell is gated), C-10 (no σ_up constant, no private floor on
ConstantRate / Quantized; `DropCurve::kMaxUpwardSemitonesPerSample` is not defined), S3.5 step 2 (L_r = L_min always), S4.3's
floor, S5.4's γ_cap bracket (bracket is ±48), S11's SC-015 row (rendered measurement), S14's `landingFeasible()` capability
line, and O-2 / O-3 (moot, recorded). tasks.md carries the per-task consequences under "Rulings applied". **Also 2026-10-13 (T026 full-suite reds): FR-070 / SC-028** —
the shared bank re-rolled partial amplitudes across pitch updates (±11 dB per partial after a 48 st Drop, `evidence/growl-probe.md`);
ruled as an opt-in, append-only bank fix enabled by `ProfundumCore` (spec Clarifications "Session 2026-10-13"). **Also 2026-10-13 (T023
epoch 5): (A) SC-026 scoped to the Anchor's pitch, phase() and mode gain with the output difference gated at ≤ −60 dB re peak; (B)
SC-010(a) / SC-021 count ClickDetector detections differentially against the uninterrupted reference render (user rulings); (B′)
SC-010(b) D from the delivered gains, (C) SC-014 Retrigger dip measured in the same window, (D) SC-019 per-hold mirror under the T005
ruling (ii) (main loop, existing rulings applied)** — spec Clarifications "Session 2026-10-13 — T023 epoch 5 readings". **And after
epoch 6 (four user rulings, "T023 epoch 6 readings"): SC-019 any lever within the existing bars + median-of-three-lanes protocol;
SC-010(a) / SC-021 gated by the sample-delta bound (b), (a) recorded; SC-026 waveform gated with the level feed divided out, raw
excursion recorded for Phase 9; SC-014 Retrigger dip against the Continue one-period RMS.**

### C-1 (refinement, include discipline): one new Layer 0 header, `core/drop_curve.h`

`DropPhaseLock` must evaluate the planned curve at quadrature nodes to solve for its correction (S5.4). A single
integral number cannot provide that. The spec forbids `drop_phase_lock.h` from including `drop_envelope.h`, because
both are Layer 2.

The curve math therefore lives in a **plain-data Layer 0 header**, `core/drop_curve.h`. It defines
`struct DropCurve`, closed-form evaluation and the fixed-panel quadrature. Both Layer 2 components include it
downward.

- The spec's ODR sweep already covered `DropCurve`: 0 hits.
- `DropEnvelope` exposes the FR-056 hook as `plannedCurve()` (the struct) and `phaseIntegralSeconds()`.
- `DropPhaseLock::trigger` takes `const DropCurve&` together with plain numbers. This is the spec's "takes plain
  numbers" in aggregate form.

### C-2 ⚠ RE-RULE (FR-056, FR-057 wording; no bar change): no elementary closed form exists, so the integral is O(1) fixed-panel quadrature

The integral ∫2^{d(t)/12} dt is not elementary over the exponential and logarithmic pieces.
- With d = A·e^{−ku} it is (T/k)·[Ei(a) − Ei(a·e^{−k})], with a = A·ln2/12.
- Over a cubic Hermite piece it is ∫e^{cubic}.

FR-057 also says that "the integral of the blend is the blend of the integrals". That is **false**, because 2^{d/12}
is nonlinear in d.

The plan computes the integral of the blended curve directly with **Gauss–Legendre 8 on fixed panels**:
- 2 panels per piece;
- 4 panels on pieces longer than 0.5·T.

The curve has at most 12 pieces (S3.3). The cost is O(1) and independent of T, which is what FR-057 and SC-025
actually require: no per-sample dry run.

Measured accuracy on the SC-024 grid (`drop-model.js integral`): **8.7e-9 relative** worst case, at A = 48, s = 0,
R = 1, against a 20 000-panel Simpson reference. The bar is 1e-6. GL8 with one panel gives 1.1e-3, which is why the
long piece gets four panels. **Ratify:** "closed-form" in FR-056 and FR-057 is read as "O(1) fixed-node quadrature of
the latched curve".

### C-3 ⚠ RE-RULE (FR-053 refinement, no bar change): Φ_x is predicted on the core's staircase and clamp

The core samples the trajectory once every 16 samples and holds it (`profundum_core.h:240–275`). It also clamps the
trajectory to [8 Hz, cap] (`:805–811`). The excess phase that h1 actually accumulates is therefore a sample-and-hold
sum of clamp(f_res·2^{d_c/12}) − f_res, not ∫ f_res·(2^{d/12} − 1).

Measured (`drop-model.js lock`, `lock2all`):
- The continuous integral with Euler–Maclaurin endpoint terms but **no clamp** misses by up to **37.6°**: E0, A = 48,
  T = 1 s, elastic, R = 1. There the rebound bottom reaches −19.2 semitones, which is 6.8 Hz, below `kMinF0Hz`.
- Clamp-aware but with EM endpoint terms only: 0.70° worst.
- The shipped predictor (S5.3) sums the first 4 pitch updates exactly and integrates the clamp-aware remainder with
  EM endpoint terms. Worst error **0.218°** over the whole SC-013 grid at 44.1, 48 and 96 kHz (`lock3 4`). Using 8 or
  16 exact updates gives the same figure.

The predictor needs the grid phase at trigger: the number of samples until the core's next pitch update. The caller
tracks this (S8). It is 0 after any Reset noteOn, because the core restarts its grid there (`:550`).

### C-4 ⚠ RE-RULE (FR-053 "nearest integer"): the landing target is the neighbouring integer with the smaller correction

The spec says "nearest integer", so |r| ≤ 0.5 cycle. At E0 with short drops, the nearest target often requires
removing phase. That is a dive toward or below the 8 Hz clamp, which no γ can reach (the "infeasible" case).

The plan evaluates both ⌊Φ̂⌋ and ⌈Φ̂⌉ and takes the one whose solved |γ| is smaller. Then |r| < 1 cycle. Over the
SC-013 grid at three rates, **every cell is feasible** with |γ| ≤ 48 (`lock2all`). The θ bar (1°) is unchanged.

### C-5 ⚠ RE-RULE (FR-054 analytic bound; SC-013(c) gains a 1-ulp floor): the correction profile and a solver-independent peak

- **Profile:** c(t) = γ·b(t/D) with b(x) = 16x²(1 − x)², and D = the landing time.
- **Properties:** b(0) = b′(0) = b(1) = b′(1) = 0. Its peak is exactly 1 at x = ½. Its mean over [0, 1] is
  16·(1/3 − 1/2 + 1/5) = **8/15**, so the profile's **peak factor is 15/8**. max|b′| = 3.0792 at x = (3 − √3)/6, and
  max|b″| = 32 at x = 0 and x = 1 (b″ = 16(2 − 12x + 12x²)).

**Analytic peak γ_an (FR-054), derived without the solver.** The inputs are only the residual r, f_res, D, the planned
curve d and the profile b:
- **r = n* − Φ_ref(0).** Φ_ref(0) is the uncorrected excess phase. The test measures it as its own double staircase
  sum over the bypass render (`currentFrequency()` trace, FR-058 bypass), never through `predictedExcessCycles()`.
  n* is the landing integer, and the test asserts |r| < 1 (C-4).
- FR-054's mean frequency offset is |r|/(f_res·D). Through the peak factor, the leading-order (small-γ) peak is
  γ_lin = (12/ln 2)·(15/8)·|r|/(f_res·κ·D), where κ = ∫2^{d/12}·b·𝟙 dt / ∫b dt is the b-weighted mean pitch ratio of
  the planned curve over the samples that stay inside the core's clamp [8 Hz, cap] (`trajectoryF0`,
  `profundum_core.h:805–811`).
- **Clamp-aware, exact-direction form.** L(g) is a lower bound on the excess phase that a correction of size g moves,
  summed over D:
  - unclamped samples with γ > 0 use convexity, 2^{gb/12} − 1 ≥ max(g·b·ln2/12, 𝟙[b ≥ β]·(2^{gβ/12} − 1)) for
    β ∈ {¼, ½, ¾};
  - unclamped samples with γ < 0 use concavity, 1 − 2^{−gb/12} ≥ max(b·(1 − 2^{−g/12}), 𝟙[b ≥ β]·(1 − 2^{−gβ/12}));
  - samples pushed through a clamp by g count their exact saturated move |clamp(f_g) − f_0|;
  - samples already clamped at γ = 0 count 0.

  L is non-decreasing in g, and the true moved phase is ≥ L(g) and monotone. So **γ_an = inf{g : L(g) ≥ |r|}**, found by
  bisection on [0, 48], bounds the true |γ| from above. γ_an is +∞ when L(48) < |r|. The small-g limit of L is
  γ_lin, which is FR-054's mean-offset × peak-factor figure.
- **Gate value:** Γ = min(γ_an, γ_cap) + carry, where γ_cap = min(48, σ_up·D_s/kBumpSlopePeak) is a constant of D_s
  (C-6), not a solver output. The bridged carry term |c₀| + |c₀′|·L·4/27 (S5.5) takes c₀ and c₀′ from the **measured**
  d_c − d trace at the retrigger sample.
- **SC-013(c):** measured peak |d_c − d| ≤ 1.05·Γ + 1 ulp(A).
  - **Re-rule:** the ulp term is an addition to the spec's "1.05 × analytic". Both d and d_c are float casts of doubles
    (S3.4, S5.2). Their difference can be off by ½ ulp each from the double correction, so it carries up to 1 ulp(A)
    even when Γ → 0.
  - The bound grows only with r, which the test measures, and not with the solver's γ. A runaway bracket, a wrong
    candidate or an inflated Φ̂ therefore fails the gate whenever it pushes |γ| past 1.05·Γ.

**Measured (`drop-model.js bound`, SC-013 grid × {44.1, 48, 96} kHz, 1 536 cells):**
- 0 cells with |γ| > 1.05·γ_an. The worst |γ|/γ_an is 1.0192, at C3, 44.1 kHz, A = 48, T = 10 ms, s = 1, R = 1,
  γ = −0.047. That is the staircase/continuous gap, inside the 1.05 tolerance.
- Tightness, min |γ|/γ_an by T: 0.60 (10 ms), 0.85 (60 ms), 0.92 (200 ms), 0.99 (1 s). A solver error that inflates γ
  by more than 1.75× is caught in every cell. At T ≥ 200 ms, an error of more than 1.15× is caught.
- 5 cells have γ_an = +∞, all at T = 10 ms, E0/E1, A = 48, with model |γ| from 24.49 to 47.90. There the gate value
  is γ_cap (C-6). The component's clamp (S5.4) bounds |γ| by γ_cap by construction, so the gate checks that clamp.

Measured |γ| over the SC-013 grid at three rates (`lock2all`):

| T | Cells with \|γ\| ≤ 3 st | 3–12 st | 12–24 st | 24–48 st | Max \|γ\| |
|---|---|---|---|---|---|
| 10 ms | 169 | 140 | 62 | 13 | 47.9 st |
| 60 ms | 339 | 45 | 0 | 0 | 9.78 st |
| 200 ms | 377 | 7 | 0 | 0 | 4.47 st |
| 1000 ms | 384 | 0 | 0 | 0 | 0.48 st |

The spec's orientation figure was "≈ 3 semitones peak for a 60 ms drop". The measured worst case at 60 ms is
9.78 st, at E0, where one cycle of f_res is 48.5 ms. Recorded, not a bar.

### C-6 ⚠ RE-RULE CHOICE (SC-013 × SC-015 at T = 10 ms): the correction must respect the Nyquist slope bound

SC-015 makes any partial past fs/2 a Phase 4 defect, and it includes the corrected curve. The correction's upward
slope must therefore obey the same per-sample bound as every other Phase 4 trajectory (σ_up = 0.15 semitones per
sample, C-10). That requires |γ| ≤ σ_up·D·fs/3.079, where 3.079 = max b′.

Under that cap, **23 of the 384 T = 10 ms cells** have no slope-feasible landing target (`drop-model.js slope`), for
example E0, 44.1 kHz, A = 48, exponential, R = 0, γ = 38.1. No cell at 60 ms, 200 ms or 1 s is affected.

**Options for the user:**
- **(a) Default in this plan:** SC-015 wins. `DropPhaseLock` clamps γ to the slope cap and reports
  `landingFeasible() == false`. SC-013(a) gates θ ≤ 1° on every feasible cell and records θ on infeasible ones. The
  infeasible set is enumerated in evidence, and the gate asserts that the set is exactly the model's.
- **(b)** Drop T = 10 ms from SC-013's grid, so SC-013 starts at 60 ms.
- **(c)** Let the correction ignore the slope bound. This needs SC-015 re-ruled to exclude the corrected curve.

### C-7 ⚠ RE-RULE (FR-006 "crosses 0 at t_L" for R > 0): with Rebound, the fall goes to −ρ_R

Spec FR-006 asks that a family with R > 0 cross 0 **at** t_L with a non-zero slope, into the rebound.

The exponential family is within 5·10⁻⁴·A of the target at u = 0.95, with a slope near 0. No monotone C¹ fall can
then cross 0 steeply at u = 1. A Hermite landing fillet with end slope −3.375 (R = 1) rises *above* p first, which
would be upward pitch motion inside the fall.

**Plan:** with Rebound, the fall runs from A to −ρ_R along the family shape: d = (A + ρ_R)·p(u) − ρ_R.
- It crosses 0 inside the fall with a non-zero negative slope. Measured at R = 1: exponential at u = 0.201, linear at
  0.800, logarithmic at 0.967.
- It reaches −ρ_R exactly at t_L.
- A C¹ return to 0 settles at t_L + kReboundSettleFactor·T.

FR-005's law ρ = R·0.25·A holds **exactly**: ρ is the value of a knot, not a fitted peak. SC-003's settle bar is
unchanged. FR-006's family list (zero-slope landing for R = 0; C¹ everywhere) is unchanged.

### C-8 ⚠ RE-RULE (FR-007 bridge length, E-4 bound; D-14 authority): the bridge is brake → rise → onset fillet

**Two spec bars change and need ratification:**
- **E-4's bound** [−ρ_max, A] becomes **[−ρ_max − 2.7 st, A]** (brake excursion, step 1 below).
- **FR-007's bridge length** "kDropBridgeMs (1 ms, shortened when T is shorter)" becomes the **law L_br + L_r**, with
  L_br and L_r as in steps 1–2. It is rate-dependent when σ_up binds: up to 480 samples (10.9 ms at 44.1 kHz) for a
  0 → 48 st rise.

A single Hermite bridge from the current (d, d′) to (A, p′(0⁺)) fails two ways:
- It overshoots **above A**. Measured: a 1 ms bridge into the exponential family at T = 10 ms peaks at A + 1.8 st.
  The overshoot is unbounded as T → 1 ms, because p′(0) = −8.
- With a steep negative incoming slope, it dips far below the current value.

**Plan (S3.5):**
1. **Brake.** A monotone Hermite takes the incoming slope m₀ to 0 over L_br = clamp(round(3·kBrakeSemitones/|m₀|),
   1, L_min) samples. Its excursion |m₀|·L_br/3 is ≤ kBrakeSemitones = 0.5 st whenever L_br > 1. At the theoretical
   extreme (m₀ = 8 st per sample, L_br = 1) it is ≤ 2.7 st.
2. **Rise.** A smoothstep runs to (A, 0) over L_r = max(L_min, ⌈1.5·ΔA/σ_up⌉) samples, with
   L_min = round(min(kDropBridgeMs, T)·fs/1000).
3. **Onset fillet.** The new fall starts with zero slope through a Hermite over its first kOnsetFilletFraction = 0.05
   of T, ending on the family's value and slope at u = 0.05. This applies to bridged triggers only; snap triggers keep
   p′(0), so SC-001 and SC-002 are untouched.

**E-4's bound becomes [−ρ_max − 2.7 st, A].** The 2.7 st term applies only at T ≈ 1 ms on the steepest family.

**The rise length is rate-dependent when σ_up binds.** Measured (`nyquist`): a bridged trigger from 0 to A = 48 takes
480 samples, which is 10.9 ms at 44.1 kHz, 10.0 ms at 48 kHz and 5.0 ms at 96 kHz. FR-010 and SC-005 are measured on
snap triggers and are unaffected.

### C-9 ⚠ RE-RULE (FR-062 scope): the Drop's tail upswings are not slope-bounded

The elastic and rebound returns rise at up to 0.418 st per sample. That is at E0, 44.1 kHz, A = 48, T = 10 ms, s = 1,
R = 1 (`slope`). Bounding them by σ_up = 0.15 would stretch them past SC-003's settle bars (0.5·T, 2·T) for any
T ≲ 28 ms, and would break FR-010's millisecond invariance.

They are safe on SC-015's grid. During an upswing the pitch is ≤ f_res·2^{q·ε·A/12} = 1.23·f_res. At C3 with 64
partials the top partial stays ≤ 10.3 kHz, against a mask cap of 17.6 kHz at 44.1 kHz. SC-015's rebound-recovery cells
therefore pass the gate as written.

**Ratify:** this is recorded, and handed to Phase 9 (S14). Above roughly f_res = cap/(64·1.23·1.25), which is about
180 Hz at 44.1 kHz, a voice needs a pitch-aware guard.

### C-10 (D-14 realisation): σ_up = 0.15 semitones per sample, and where it applies

Between a mask build (control update) and the next pitch update (16 samples later), f may rise by at most
12·log2(1/0.8) = 3.8631 st before a partial below the cap reaches fs/2 (`nyquist`).

- **Continuous laws:** σ_up = 0.15 limits a 16-sample rise to 2.40 st.
- **Quantized glide:** the output adds at most one extra step, so ⌊2.40⌋ + 1 = 3 st < 3.86. One constant serves both.

Applied to:
- the Drop bridge rise (C-8);
- the `DropPhaseLock` correction (C-6);
- `MonoHandler`'s ConstantRate and Quantized laws, as a private floor on their durations (S4.3). It never binds on
  SC-007, SC-008 or SC-010(c): their fastest case is 0.054 st per sample.

**Not applied to ConstantTime.** FR-021 freezes it bit-identical. A ConstantTime up-glide of Δ st shorter than
Δ/(0.15·fs) can violate SC-015's formula; at 44.1 kHz that is 3.6 ms for Δ = 24. SC-015 does not test ConstantTime.
This is open question O-2.

Note for the user: the core never *renders* a partial above the cap. `computeEpsilonTargets` freezes those partials
at capHz (`:700–712`). SC-015's formula (index × `currentFrequency()`) flags a partial frozen at the cap for ≤ 16
samples, at a mask gain ≤ −73 dB. The alternative ruling (O-1) is to measure the rendered partial frequency. Under it
every slope bound in C-6, C-8 and C-10 becomes unnecessary.

### C-11 (FR-023 / FR-024 refinement): a Quantized retarget starts from the current output note

On a retarget, or on entering Quantized, the underlying ramp restarts **from the current integer output q**, not from
the fractional underlying position. The new ramp runs over T to the target.
- Entering from a fractional pitch p (from ConstantTime or ConstantRate), q is the grid note on the start side:
  ⌊p⌋ when gliding up, ⌈p⌉ when gliding down. The output moves < 1 semitone (FR-024).
- Otherwise a mid-glide retarget from p = 60.4 toward 48 would first step **up** to 61 under FR-023's rule.
- Every glide then makes exactly |target − q| steps at k·T/|Δ| (SC-008 holds for retargets).

### C-12 (D-2 / D-3 constants the spec leaves to the plan)

| Constant | Value |
|---|---|
| `kExpRate` | k = 8. Exponential α = 0.12495, linear 0.49979, logarithmic 0.87338 (`shape`) |
| `kLandingFilletFraction` | δ = 0.05 |
| `kOnsetFilletFraction` | 0.05 |
| `kElasticPeakRatio` | q = 0.45, below kElasticDecay = 0.5 so that a sampled peak ratio cannot exceed 0.5 |
| Elastic knot layout (units of T after landing) | 1.00, 1.15, 1.30, 1.45, 1.60, 1.75, 2.00 |
| `kElasticLandingSlope` | 1.5·ε/0.15 = 1.5 (·A/T) |
| `kMaxUpwardSemitonesPerSample` | σ_up = 0.15 |
| `kBrakeSemitones` | 0.5 |
| `kDeclickMs` | 1.5 (spec) |

Every curve-geometry constant in this table except `kDeclickMs` is **defined once**, as a `static constexpr` member of
the Layer 0 `DropCurve` (S3.3). `makeDropCurve` needs them, and a `core/` header cannot include a Layer 2 header
(`dsp/CLAUDE.md`: Layer 0 is stdlib only). `DropEnvelope` and `DropPhaseLock` re-export them as aliases
(`static constexpr double kExpRate = DropCurve::kExpRate;`). The aliases are not second definitions, so no
`static_assert` is needed to keep copies in step. `kDeclickMs` belongs to `RetriggerDeclick` (L1).

### C-13 (SC-024 "discretisation bound the plan derives"): 2e-5 cycles

The second clause of SC-024 compares `DropPhaseLock`'s predictor, run at update interval 1, with the per-sample
double sum Σ f_res·(2^{d[n]/12} − 1)/fs.

Measured worst case: **1.41e-5 cycles** (0.005°), at C3, 44.1 kHz, A = 48, T = 1 ms, s = 2/3, R = 0.5. That is over
fs × {E0, C3} × A ∈ {1, 12, 48} × T ∈ {1, 20, 200, 4000} ms × s ∈ {0, 1/3, 2/3, 5/6, 1} × R ∈ {0, 0.5, 1}
(`perSample`). **Bar: 2e-5 cycles absolute**, recorded per cell.

### C-14 (FR-055 signature): `DropPhaseLock::trigger` takes the grid phase and the core's clamp

`trigger(curve, fResHz, gridPhase, minF0Hz, capHz, TriggerKind)`. It latches these arguments, and the solve runs
lazily at the next process call (S5.4).

The clamp values are the core's `kMinF0Hz` and `SpectralShapeRecipe::capFrequency(fs)`. A systems TU
`static_assert`s that the test composition passes the core's constants. `DropPhaseLock` itself includes no Layer 3
header. `kPitchUpdateInterval = 16` is restated in `DropPhaseLock`, and the systems TU `static_assert`s it against
`ProfundumCore` and `AnchorOscillator` (the Phase 3 D-6 pattern).

### C-15 (FR-041 sample sequence)

On `arm()` the gain falls as g[k] = g₀·½(1 + cos(π(k+1)/L_d)) for k = 0 … L_d − 1, with L_d = ⌈kDeclickMs·fs/1000⌉.
- The sample k = L_d − 1 is the **switch sample**. It is written as exactly 0.0f.
- `samplesUntilSwitch()` = L_d − 1 − k_done.
- The caller renders that many samples, issues the deferred Reset noteOn, and the next block's offset 0 is the switch
  sample: core output 0.0 (`:557–558`), gain 0.0.
- The gain is 1.0f from the following sample on.

The restart latency is L_d − 1 ≤ ⌈1.5·fs/1000⌉ + 1 (FR-042): 66 samples at 44.1 kHz. Peak slope: S_max = g₀·π/(2L_d)
per sample.

### C-16 (SC-019 feasibility, not yet measured): CPU is the main schedule risk

The budget is 0.05 % of one core at 48 kHz: **10.4 ns per sample for every Phase 4 component together**. The S10.7
estimate is about 5 ns per sample, plus the `DropPhaseLock` solve:
- about 6 µs for a typical trigger, at about 4 predictor passes of a snap curve;
- up to about **21–42 µs worst case** (S5.4: ≤ 17 passes × ≤ 30 panels × 8 nodes ≈ 4 150 integrand evaluations at
  5–10 ns each).

At SC-019's one trigger per 100 ms, the worst case is 0.021–0.042 %. Added to the per-sample 0.024 %, that **can
exceed the 0.05 % bar**. Task T001 therefore benchmarks the S3.4 kernel and the S5.4 solver at their **worst-case**
piece and panel count before any header lands. If the bench misses, S13 R-1 lists the levers. The bar is never
relaxed.

---

## S2. Architecture

```
            MonoHandler (L2, +GlideMode)        NoteMemory (L2, pure resolver)
  MIDI ──►  f_res[n] (Hz, per sample)  ──┐      Actions{phase, envelopeRestart, dropTrigger,
                                         │              dropSnap, anchorRearm, declick}
                                         │                 │
  DropEnvelope (L2) ── d[n] (st) ──► DropPhaseLock (L2) ── d_c[n] (st)
        │ plannedCurve(): DropCurve (L0) ──►┘ (trigger: curve, f_res, gridPhase, clamp)
        ▼
  f_core[n] = f_res[n]·2^{d_c[n]/12}  ──► ProfundumCore::processBlock(L, R, n, f_core)
  f_anchor[n] = f_res[n] (LOCK/PUNCH/GHOST) | f_core[n] (FOLLOW) ──► AnchorOscillator::processBlock
  RetriggerDeclick (L1) gain g[n] ──► (core + anchor)·g·ADSR  (same float on L and R)
```

- Phase 4 ships `DropCurve` (L0), `RetriggerDeclick` (L1), `DropEnvelope`, `DropPhaseLock` and `NoteMemory` (L2), and
  the append-only `MonoHandler` extension (L2).
- The composition is **test-only**: `NoteBehaviourHarness` (S8). Phase 9's `ProfundumVoice` replaces it.
- **Include graph** (downward only): `drop_curve.h` → `<cmath>`, `<array>`, `<cstdint>`, `<algorithm>`,
  `db_utils.h` (L0). It owns every curve-geometry constant (C-12), and the L2 headers alias them. `retrigger_declick.h` → L0; `drop_envelope.h` → `drop_curve.h`, `db_utils.h`;
  `drop_phase_lock.h` → `drop_curve.h`, `db_utils.h`; `note_memory.h` → `<cstdint>`; `mono_handler.h` keeps its four
  includes (lines 22–25). The constant-rate law needs no new header (C-10).

---

## S3. `DropCurve` (L0) and `DropEnvelope` (L2)

### S3.1 Normalised fall p(u; s), u = t/T ∈ [0, 1] (C-12; `drop-model.js shape`)

| Family | Region | Expression |
|---|---|---|
| **Exponential** (id 0) | u ≤ 0.95 | p = e^{−ku}, k = 8 |
| | u ∈ [0.95, 1] | Hermite from (0.95, e^{−7.6}, −8e^{−7.6}) to (1, 0, 0) |
| **Linear** (id 1) | u ≤ 0.95 | p = 1 − u |
| | u ∈ [0.95, 1] | Hermite from (0.95, 0.05, −1) to (1, 0, 0) |
| **Logarithmic** (id 2) | u ≤ 0.95 | p = 1 − p_exp(1 − u), the mirror. Its first 0.05 is the mirrored exponential fillet, so p′(0) = 0: "holds, then falls" |
| | u ∈ [0.95, 1] | Hermite from (0.95, 1 − e^{−0.4}, −8e^{−0.4}) to (1, 0, 0). This is the landing fillet FR-006 asks for |
| **Elastic fall** (id 3) | — | The logarithmic family, except that the fillet ends at slope −kElasticLandingSlope = −1.5 |

- **Blend (FR-004):** s ∈ [0, ⅓] → (0, 1, w = 3s); [⅓, ⅔] → (1, 2, 3s − 1); [⅔, 1] → (2, 3, 3s − 2).
  p = (1 − w)·p_a + w·p_b, and p′ blends the same way.
- **Elastic weight:** w_e = max(0, 3s − 2).
- Every fillet passes the Fritsch–Carlson monotone test. Every family is monotone with min p = 0 on [0, 1].
- **SC-001 metrics, measured:**
  - exponential ln p: R² = 1.000000000, fitted rate 8.0000;
  - logarithmic ln(1 − p): R² = 1.000000000;
  - linear: max |p − (1 − u)| = 0 on [0.05, 0.95];
  - 192-step continuity: max |Δp| = 0.00961 (bar 0.025).

### S3.2 Full planned curve d(t) in semitones (C-7, D-2, D-3)

With ρ_R = 0.25·R·A, ε_A = 0.15·A and D₁ = ρ_R + w_e·ε_A:

- **Fall, u ∈ [0, 1]:** d = (A + ρ_R)·p(u; s) − ρ_R, ending at (−ρ_R, slope −(A + ρ_R)·w_e·1.5 per T).
- **Tail** (present iff ρ_R > 0 or w_e > 0): a chain of cubic Hermite segments through seven knots
  K_i = (t_i, y_i, v_i). Times blend between two layouts: t_i = (1 − w_e)·RLAY_i + w_e·ELAY_i, with
  ELAY = [1, 1.15, 1.30, 1.45, 1.60, 1.75, 2.00] and RLAY = [1, 1, 1.5, 1.5, 1.5, 1.5, 1.5].

  | Knot | y (st) | v (st/T) |
  |---|---|---|
  | K0 | −ρ_R | −(A + ρ_R)·w_e·1.5 |
  | K1 (first undershoot) | −D₁ | 0 |
  | K2 (zero crossing) | 0 | v₂ = min(1.5·D₁/(t₂ − t₁), 1.5·D₂/(t₃ − t₂)) |
  | K3 | +D₂ = q·w_e·ε_A | 0 |
  | K4 (zero crossing) | 0 | v₄ = −min(1.5·D₂/(t₄ − t₃), 1.5·D₃/(t₅ − t₄)) |
  | K5 | −D₃ = −q²·w_e·ε_A | 0 |
  | K6 (end) | 0 | 0 |

  - Zero-length segments are skipped. 0/0 in a slope rule evaluates to 0.
  - Every segment's slope ratio is ≤ 1.875 < 3 (Fritsch–Carlson), so each segment is monotone and **every extremum
    is a knot value**. That makes ρ, ε and the peak ratios exact.
- **Measured (`shape`, A = 1):**
  - ρ equals FR-005's law to 6 decimals in all 15 (s, R) cells;
  - elastic peaks −0.1500, +0.0675, −0.0304 (ratio 0.45);
  - 2 zero crossings after landing;
  - end at u = 1.5 (rebound only), 1.75 (s = 5/6) and 2.0 (s = 1).
- **Landing (FR-053) and analytic end of the planned curve:** t_end = t₆·T, or T when there is no tail. Settle
  (SC-003) is at t_end exactly.

### S3.3 `DropCurve` (L0, `core/drop_curve.h`): data layout

```cpp
namespace Krate::DSP {
struct DropCurve {                                     // plain data, latched at trigger (FR-003, D-4)
    static constexpr int kMaxPieces = 12;
    // Curve-geometry constants: the ONLY definitions (C-12). DropEnvelope / DropPhaseLock alias them.
    static constexpr double kExpRate = 8.0, kLandingFilletFraction = 0.05, kOnsetFilletFraction = 0.05;
    static constexpr double kElasticOvershootFraction = 0.15, kElasticDecay = 0.5, kElasticPeakRatio = 0.45;
    static constexpr double kElasticSettleFactor = 2.0, kElasticLandingSlope = 1.5;
    static constexpr double kMaxReboundFraction = 0.25, kReboundSettleFactor = 0.5;
    static constexpr double kDropBridgeMs = 1.0;
    static constexpr double kMaxUpwardSemitonesPerSample = 0.15, kBrakeSemitones = 0.5;
    static_assert(kElasticPeakRatio < kElasticDecay);
    enum class PieceKind : std::uint8_t { Brake, Rise, OnsetFillet, ExpBlend, Fillet, Tail };
    double amount = 0.0, reboundDepth = 0.0;           // A, ρ_R (st)
    double fallSamples = 1.0;                          // N = T·fs/1000 (double, not rounded)
    double fallStart = 0.0;                            // samples of brake + rise before the fall (0 on snap)
    std::uint8_t famA = 0, famB = 1;  double blendW = 0.0, elasticW = 0.0;
    bool onsetFillet = false;                          // bridged triggers (C-8)
    double brakeLen = 0, brakeY0 = 0, brakeM0 = 0, riseLen = 0, riseY0 = 0;   // samples / st / st per sample
    std::array<double, 7> knotT{}, knotY{}, knotV{};   // tail, in samples from the fall start
    double endSample = 0.0;                            // fallStart + t_end in samples
    [[nodiscard]] double valueAt(double n) const noexcept;    // d at sample position n (closed form, double)
    [[nodiscard]] double slopeAt(double n) const noexcept;    // dd/dn (st per sample)
    template <class F> [[nodiscard]] double integrate(F&& g, double from = 0.0) const noexcept; // GL8 panels (C-2)
};
[[nodiscard]] DropCurve makeDropCurve(double A, double timeMs, double shape, double rebound, double sampleRate,
                                      bool bridged, double dNow, double slopeNow) noexcept;
}
```

- `integrate` visits the piece breaks:
  - 0;
  - the brake and rise ends;
  - the start + 0.05·N (onset fillet, or the mirrored exponential fillet in the logarithmic family);
  - the start + 0.95·N;
  - the start + N;
  - the knot times.

  It applies GL8 with 2 panels per piece and 4 panels on pieces longer than 0.5·N, all in double, with node
  constants in a `constexpr std::array`.
- `F` is a callable of (n, d) returning double, and `integrate` returns ∫g·dn in samples. Making it a template keeps
  the call allocation-free and inlinable.

### S3.4 `DropEnvelope` public API (L2, `processors/drop_envelope.h`)

```cpp
class DropEnvelope {
public:
    static constexpr float kMaxAmountSemitones = 48.0f, kMinTimeMs = 1.0f, kMaxTimeMs = 4000.0f;
    static constexpr double kMinSampleRate = 22050.0, kMaxSampleRate = 192000.0;
    // Aliases of the L0 definitions (C-12), e.g.:
    static constexpr double kExpRate = DropCurve::kExpRate;            // … one alias per DropCurve constant
    static constexpr double kDropBridgeMs = DropCurve::kDropBridgeMs;
    static constexpr double kMaxUpwardSemitonesPerSample = DropCurve::kMaxUpwardSemitonesPerSample;

    void prepare(double sampleRate) noexcept;   // clamp [22050, 192000], then reset()
    void reset() noexcept;                       // d = d' = 0, idle; parameters kept
    // Setter rule (FR-003, SC-017): non-finite (detail::isFinite) → keep the previous value;
    // subnormal (float exponent bits == 0, mantissa != 0) → +0.0f; then clamp to the range.
    void setAmount(float semitones) noexcept;    // [0, 48]; subnormal → A = 0 exactly → d ≡ 0 (E-1)
    void setTime(float ms) noexcept;             // [1, 4000]; subnormal → 0 → clamps to 1
    void setShape(float s) noexcept;             // [0, 1]; subnormal → 0
    void setRebound(float r) noexcept;           // [0, 1]; subnormal → 0
    void trigger(bool snap) noexcept;            // latches; effective at the next process()
    [[nodiscard]] float process() noexcept;
    void processBlock(float* out, std::size_t n) noexcept;   // null or 0: no-op, no state change
    [[nodiscard]] bool isActive() const noexcept;
    [[nodiscard]] float currentOffset() const noexcept;
    [[nodiscard]] const DropCurve& plannedCurve() const noexcept;   // FR-056 hook (C-1)
    [[nodiscard]] double phaseIntegralSeconds() const noexcept;     // ∫(2^{d/12} − 1)dt of the latched curve (C-2)
};
```

**Trigger.**
- `trigger(true)` builds the curve with fallStart = 0.
- `trigger(false)` evaluates the running curve's `valueAt` and `slopeAt` at the **next** sample position (n + 1),
  so the bridge is C¹ in the discrete sense. While idle that is (0, 0). It then builds brake + rise + onset fillet
  (C-8) and resets the running sample counter.
- A trigger with A = 0 builds the zero curve. d ≡ 0 bit-identically (E-1).
- `phaseIntegralSeconds()` = `plannedCurve().integrate([](double, double d){ return std::exp2(d/12.0) - 1.0; }) / fs`.
  It is computed lazily, on call, and never on the audio path of `process()`.

**Per-sample kernel (S10.7 budget).**
- A `uint32_t` sample counter n; u = (n − fallStart)·invN in double, with no accumulation, so the output is
  partition-invariant.
- In the exponential and mirrored regions (u ∈ [0.05, 0.95]), e^{−ku} and e^{−k(1−u)} come from one double geometric
  recurrence per region, seeded at the region's first sample with `std::exp`. The relative error is ≤ n·2.2e-16,
  below 1e-10 at 384 000 samples.
- Elsewhere the cubic Hermite polynomial is evaluated in Horner form.
- The output is `static_cast<float>(d)`.
- At n ≥ endSample the output is exactly 0.0f and the envelope goes idle.

**C¹ (FR-006).** Every piece join matches value and slope by construction (S3.1–S3.2). The SC-004 checker's per-piece
analytic |Δ²| bounds come from `DropCurve`:
- cubic Hermite: the Phase 3 `anchorHermitePeakD2` form;
- exponential: (A + ρ_R)·k²/N² at the piece start;
- mirrored region: the same bound at its end.

The bounds are exposed to tests through a `DropCurve::pieceD2Bound(i)` helper. It is test-facing but header-resident,
because the bound is part of the curve. For a blended piece the bound is the blend of the family bounds,
(A + ρ_R)·((1 − w)·max|p_a″| + w·max|p_b″|)/N², and the linear family contributes 0. The bounds feed the
Drop-specific checker `dropCheckC1` (S11.2), **not** the Phase 3 `anchorCheckC1`. That checker's `AnchorSegmentSpec`
has no bound field, and its kinds (Hold, Cosine, Hermite) cannot express ExpBlend or mirrored pieces
(`anchor_oscillator_test_helpers.h:131–160`).

### S3.5 Bridge (C-8) equations, in samples, with m₀ the incoming slope in st per sample

1. **Brake** (iff m₀ ≠ 0): L_br = clamp(round(1.5/|m₀|), 1, L_min). This is 3·kBrakeSemitones/|m₀|.
   y(τ) = y₀ + m₀·L_br·(τ − τ² + τ³/3), with τ = k/L_br. That is the Hermite with Δ = m₀L_br/3 and end slope 0, so
   y′ = m₀(1 − τ)², which is monotone. Exit at y_k = y₀ + m₀L_br/3, slope 0.
2. **Rise:** L_r = max(L_min, ⌈1.5·(A − y_k)/σ_up⌉). This is a smoothstep, y_k + (A − y_k)(3τ² − 2τ³), with peak slope
   1.5·(A − y_k)/L_r ≤ σ_up. If A < y_k (only possible within the brake excursion), the smoothstep descends.
3. **Onset fillet:** over u ∈ [0, 0.05] of the new fall, a Hermite from (1, 0) to (p(0.05), p′(0.05)). For the
   logarithmic family it reproduces the mirrored exponential fillet exactly.

### S3.6 Lifecycle, robustness, RT safety

- Before `prepare`: `process` returns 0 and every setter and `trigger` is a no-op (FR-002).
- Setters use `detail::isFinite`, flush subnormal input to +0, and then clamp (FR-003, FR-009). Flushing is a
  bit-pattern exponent test, immune to fast-math and independent of the host's FTZ/DAZ. A finite value is still
  "clamped" as FR-003 requires, because a subnormal maps to the nearest representable normal-or-zero value in range.
- Every member is a fixed-size scalar or `std::array`. There is no heap, no static mutable data, no exceptions.
- `prepare` mid-drop resets (E-11).

---

## S4. `MonoHandler` glide modes (append-only, SC-020)

### S4.1 Additions (no line of the current file is removed or edited)

- At namespace scope, after `PortaMode` (line 66):
  `enum class GlideMode : uint8_t { ConstantTime = 0, ConstantRate = 1, Quantized = 2 };`
- Public members: `void setGlideMode(GlideMode mode) noexcept;` and
  `[[nodiscard]] GlideMode glideMode() const noexcept;`. No other public member is added (FR-026, SC-020).
- Private state, appended after `sampleRate_` (line 496):
  `GlideMode glideMode_{GlideMode::ConstantTime}; double gpStart_{0.0}, gpTarget_{0.0}, gpLen_{0.0}; std::uint64_t gpN_{0}; bool gpActive_{false}; int qOut_{0}; float qFreq_{0.0f};`
  and the private constant `static constexpr double kMaxUpwardGlideSemitonesPerSample = 0.15;` (C-10).

**Inserted hook lines.** Each hook is a pure insertion of new lines and is dead under ConstantTime.

| Location | Inserted lines | Under ConstantTime |
|---|---|---|
| top of `processPortamento()` (after line 291) | `if (glideMode_ != GlideMode::ConstantTime) return processPreciseGlide();` | skipped, so the sequence is bit-identical |
| top of `updatePortamentoTarget` (after line 450) | `if (glideMode_ != GlideMode::ConstantTime) { preciseRetarget(targetSemitones, enableGlide && portamentoTimeMs_ > 0.0f); return; }` | skipped. The glide/snap decision is the same expression the existing line 451 evaluates (FR-025) |
| end of `setPortamentoTime` (after line 285) | `if (glideMode_ != GlideMode::ConstantTime) preciseRetime();` | skipped |
| end of `prepare` (after line 124) | `if (glideMode_ != GlideMode::ConstantTime) preciseRetime();` | skipped. The position is kept, as today (E-8) |
| end of `reset()` (after line 355) | `gpActive_ = false; gpStart_ = gpTarget_ = currentPreciseSemitones(); gpN_ = 0;` | writes only the new state |

SC-020's Node check (`check-append-only.js`, S12.3) asserts **0 removed lines** in `git diff 3d8ca19f -- mono_handler.h`.

### S4.2 Laws (D-13: double counter, never float accumulation)

**ConstantRate.**
- r = 12·1000/(T·fs) st per sample, with T = `portamentoTimeMs_` read as ms per octave.
- Upward: r_eff = min(r, 0.15). Downward: r_eff = r.
- Position: p(n) = start + sign(Δ)·min(r_eff·n, |Δ|), evaluated from the counter.
- Output: `semitoneToFrequency(static_cast<float>(p))`.
- `preciseRetarget`: start := current p, n := 0.
- Duration = ⌈|Δ|/r_eff⌉ samples. Measured worst error 0.000 samples against T·|Δ|/12 (`glide`).

**Quantized.**
- Start q₀ (integer, C-11); N = max(T·fs/1000, |Δ|/0.15) samples, Δ = target − q₀.
- Steps completed: k(n) = min(|Δ|, ⌊n·|Δ|/N⌋). Output note q = q₀ + sign(Δ)·k.
- Step k lands at ⌈k·N/|Δ|⌉, within ±1 of round(k·T·fs/(1000·|Δ|)). Measured worst 1 sample (`glide`).
- `qFreq_` is recomputed only when k changes, so a quantized glide costs no `pow` per sample (S10.7).

**Snap.** When `enableGlide && T > 0` is false, both modes snap position and output to the target and set
`currentFrequency_` (E-7).

**`preciseRetime`.** ConstantRate recomputes r from the current p. Quantized restarts from the current output note
over the new T. This mirrors `LinearRamp::configure` (`smoother.h:329–336`).

### S4.3 Mode switch (FR-024)

`setGlideMode(m)` ignores out-of-enum values (E-14) and returns early if m equals the current mode. Otherwise it reads
the current pitch p_now (the ramp's `getCurrentValue()`, the double position, or q) and the current target (the ramp's
`getTarget()` or `gpTarget_`).

| Entering | Action |
|---|---|
| ConstantTime | `portamentoRamp_.snapTo(float(p_now))`; then `setTarget(target)` if p_now ≠ target, so the remainder lasts T |
| ConstantRate | start = p_now (a Quantized source passes q), continue to the target |
| Quantized | q₀ = ⌊p_now⌋ when gliding up or ⌈p_now⌉ when gliding down (p_now if not gliding), then a new ramp over T |

Every switch moves the output by less than 1 semitone.

---

## S5. `DropPhaseLock` (L2, `processors/drop_phase_lock.h`)

### S5.1 API

```cpp
class DropPhaseLock {
public:
    enum class TriggerKind : std::uint8_t { SnapReset, SnapFreeRunning, Bridged };
    static constexpr std::size_t kPitchUpdateInterval = 16;          // == ProfundumCore/AnchorOscillator (C-14)
    static constexpr int kExactLeadUpdates = 4;                       // C-3
    static constexpr double kMaxCorrectionSemitones = 48.0;           // C-4 search bound
    static constexpr double kBumpSlopePeak = 3.0792014356780038;      // max b'(x), b = 16x²(1−x)², at x = (3−√3)/6
    static constexpr double kMaxUpwardSemitonesPerSample = DropCurve::kMaxUpwardSemitonesPerSample;  // alias (C-12)
    static constexpr int kMaxSolveEvaluations = 8;                    // per candidate, incl. 2 bracket ends (S5.4)
    void prepare(double sampleRate) noexcept;
    void reset() noexcept;                                            // accumulator 0, idle, no plan, no pending
    void setBypass(bool follow) noexcept;                             // FR-058: c ≡ 0, d_c = d bit-identical
    void trigger(const DropCurve& curve, float fResHz, std::size_t gridPhase,
                 float minF0Hz, float capHz, TriggerKind kind) noexcept;   // latches + sets pending; NO solve (S5.4)
    [[nodiscard]] float process(float plannedOffset) noexcept;        // returns d_c; runs a pending solve first
    void processBlock(const float* in, float* out, std::size_t n) noexcept;   // pending solve once, then n samples
    [[nodiscard]] double correctionGamma() const noexcept;            // γ (st) of the last completed solve
    [[nodiscard]] double correctionPeakBound() const noexcept;        // |γ| + carry peak; diagnostic, NOT SC-013(c)'s gate (C-5)
    [[nodiscard]] double solveResidualCycles() const noexcept;        // |Φ̂(γ) − n*| at the returned γ (S5.4)
    [[nodiscard]] double predictedExcessCycles() const noexcept;      // Φ̂ before correction
    [[nodiscard]] double targetCycles() const noexcept;               // chosen integer (C-4)
    [[nodiscard]] bool landingFeasible() const noexcept;              // C-6
    [[nodiscard]] double accumulatedExcessCycles() const noexcept;    // staircase mirror (S5.6)
    [[nodiscard]] static double predictExcess(const DropCurve&, double fRes, double fs, std::size_t updateInterval,
                                              std::size_t gridPhase, double minF, double capF,
                                              double gamma, double bumpLen, double carryAt0, double carrySlope,
                                              double carryLen) noexcept;   // S5.3; static so SC-024 can call it at U = 1
};
```

### S5.2 Corrected offset

d_c[n] = d[n] + γ·b(n/D_s) + carry(n). Here D_s = `curve.endSample`, b(x) = 16x²(1 − x)² for 0 < x < 1 and 0
otherwise, and carry is the bridged term of S5.5.
- From D_s on, c = 0 and d_c = d exactly. Since d = 0 there, the output is exactly 0.0f: "no pitch motion after
  landing" (FR-053, SC-013(b)).
- Bypass returns `plannedOffset` unchanged (bitwise).
- Per sample: x = n·invD (double, counter-based), b by Horner (4 FMAs), one add, one float cast.

### S5.3 Predictor of the rendered excess phase (C-3)

Let U = 16 (or 1 for SC-024), h = U/fs, φ = gridPhase. The held frequency is f_h(n) = clamp(f_res·2^{d_c(n)/12},
minF, capF), and g(n) = f_h(n) − f_res.

The core's excess over [0, D_s] is:

Φ = (φ/fs)·g(0) + Σ_{j ≥ 0} h·g(φ + jU)

Here g(0) is the noteOn f0 = the curve's start; the core holds it until the first grid sample.

Prediction:

Φ̂ = (φ/fs)·g(0) + Σ_{j=0}^{3} h·g(φ + jU) + ∫_{t₄}^{D_s} g dt + (h/2)·g(t₄) − (h²/12)·g′(t₄),  with t₄ = φ + 4U

- The integral is `DropCurve::integrate` over the clamped integrand.
- g′(t₄) is analytic: (ln2/12)·f_h·d_c′ when unclamped, 0 when clamped.
- The far endpoint terms vanish, because g = g′ = 0 at the end.

The prediction has a fixed cost: 4 exact evaluations plus one quadrature pass. Measured error on the SC-013 grid at
three rates: **0.218°** (C-3).

### S5.4 Solve (C-4, C-6)

- **Lazy solve.** `trigger()` only copies the curve and the inputs into a pending slot and sets a pending flag. The
  last call wins, as for the declick (FR-043). The solve runs once, at the start of the first `process()` or
  `processBlock()` after one or more triggers. The bridged carry's (c₀, c₀′) is evaluated there. That is the same
  sample position the trigger would have used, because no sample is processed between the trigger and the solve.
  - N triggers at the same sample offset therefore cost **one** solve.
  - **Per-block worst case:** at most one solve per processed sample that follows ≥ 1 trigger. So a host block with
    events at K distinct offsets costs ≤ K solves, and a block with no trigger costs none.
  - The accessors (`correctionGamma()` and the rest) report the last **completed** solve.
- Φ̂(γ) is non-decreasing in γ.
- **Evaluation budget (hard cap).** One predictor evaluation is one S5.3 pass: 4 exact updates plus one quadrature
  pass, which also yields ∂Φ̂/∂γ = ∫ (ln2/12)·b·f_h·[unclamped].
  - 1 shared evaluation of Φ̂(0) picks the candidates n* ∈ {⌊Φ̂(0)⌋, ⌈Φ̂(0)⌉}.
  - Per candidate, γ_lim = min(48, γ_max) with γ_max = σ_up·D_s/kBumpSlopePeak. **2 bracket evaluations**, Φ̂(±γ_lim),
    decide whether a root exists in [−γ_lim, γ_lim]. If no root exists, the candidate is infeasible, and its
    γ = sign·γ_lim, the bracket end nearer the target.
  - Then **at most 6 more evaluations**, each Newton or bisection: a Newton step from the linearised start that leaves
    the current bracket is replaced by a bisection step, and **each counts**. It stops early at |Φ̂ − n*| < 1e-9
    cycles.
  - Total `kMaxSolveEvaluations` = 8 per candidate, so **≤ 17 evaluations per solve**.
- **Cap reached without convergence:** γ = the last iterate inside the final bracket (the bisection midpoint if the last
  step bisected). `solveResidualCycles()` records |Φ̂(γ) − n*|. `landingFeasible()` is unaffected, because θ is gated
  by SC-013(a) on the render. SC-013 records the residual per cell.
- Choose the feasible candidate with the smaller |γ|.
- If neither is feasible, γ = the candidate's root clamped to ±γ_lim, and `landingFeasible()` is false (C-6).
- **Worst-case cost:**
  - Per evaluation: 4 exact updates plus ≤ 30 GL8 panels. ≤ 3 pieces can exceed 0.5·N: the brake (≤ L_min ≤ N), the
    bridge rise, and the fall body (0.9·N). The tail segments are ≤ 0.5·T. That gives 3 × 4 + 9 × 2 = 30 panels.
  - Per evaluation that is 4 + 240 integrand evaluations, each one `exp2`. At 17 evaluations, that is
    **≤ 4 148 per solve**. The coarse upper bound at 12 pieces × 4 panels is 17 × (4 + 384) = 6 596.
  - The worst-case trigger is bridged, at T = 1 ms, A = 48, s = 1, R = 1 (most pieces, longest rise). T001 measures
    that case, not a typical one.
  - The cost is independent of T (SC-025).

### S5.5 Trigger kinds (FR-059)

| Kind | When | Accumulator | Plan |
|---|---|---|---|
| `SnapReset` | Reset noteOn: both oscillators start at phase 0 (core branch `policy_ == Reset \|\| !sounding_`, `profundum_core.h:528`; Anchor `anchor_oscillator.h:481–483`) | **zeroed** (FR-059) | target from Φ̂ alone; φ = 0 |
| `SnapFreeRunning` | not audible, FreeRunning, oscillators still `sounding_` (only `reset()` clears it, `profundum_core.h:441`, `anchor_oscillator.h:169`); the first note after prepare/reset is `SnapReset` | **kept**: neither oscillator resets its phase, so the core keeps whatever h1 offset an interrupted Drop left (for example a 4 s Drop still running when the ADSR finished its 300 ms release) | snap curve (no carry, since d snaps to A), with the caller's φ. **Target:** the integer nearest-feasible to (accumulated + Φ̂_new), as for `Bridged` |
| `Bridged` | retrigger on a sounding voice | kept (S5.6) | below |

Only `SnapReset` zeroes the accumulator. Zeroing it on `SnapFreeRunning` would drop the fractional excess of the
interrupted Drop, and θ at the new landing would be frac(old excess), which can be anything. SC-013(d) gates this case.

**Bridged plan.**
- **Carry:** the old correction's (c₀, c₀′) at the next sample is continued by a Hermite carry from (c₀, c₀′) to
  (0, 0) over the bridge's brake + rise length. It is included in the integrand and in the analytic peak
  (|c₀| + |c₀′|·L·4/27).
- **Target:** the integer nearest-feasible to (accumulated + Φ̂_new).
- Both d and c continue C¹, so d_c and d_c′ never step.

**FOLLOW.** `setBypass(true)` means no plan is made and c ≡ 0 (FR-058, SC-026).

### S5.6 Staircase mirror (the running total of FR-059)

The mirror runs on every processed sample, including after landing and while the voice is inaudible. That way the
accumulator still holds the interrupted Drop's excess when a `SnapFreeRunning` noteOn arrives.

At each sample with (n − φ) % 16 == 0 (φ tracked internally from the trigger), the mirror computes
f_h = clamp(f_res·2^{d_c/12}) **once**. Every sample adds (f_h − f_res)/fs in double.

The cost is one `exp2` per 16 samples, plus one add per sample. `accumulatedExcessCycles()` feeds bridged re-plans and
the tests.

---

## S6. `NoteMemory` (L2, `processors/note_memory.h`)

```cpp
class NoteMemory {
public:
    enum class Mode : std::uint8_t { AlwaysRetrigger = 0, Legato = 1, EnvelopeRetriggerOnly = 2, OscillatorContinuous = 3 };
    enum class Retrigger : std::uint8_t { Reset = 0, Continue = 1 };
    enum class Transition : std::uint8_t { FromSilence = 0, Overlap = 1, Tail = 2, ReturnToHeld = 3, VoiceReuse = 4 };
    enum class PhasePolicy : std::uint8_t { Reset = 0, FreeRunning = 1 };   // mirrors the core/Anchor enums (SC-023)
    struct Actions { bool oscillatorRestart; PhasePolicy phase; bool envelopeRestart; bool dropTrigger;
                     bool dropSnap; bool anchorRearm; bool declick; };
    void setMode(Mode m) noexcept;            // out-of-enum: ignored
    void setRetrigger(Retrigger r) noexcept;  // out-of-enum: ignored
    [[nodiscard]] Mode mode() const noexcept;
    [[nodiscard]] Retrigger retrigger() const noexcept;
    [[nodiscard]] Actions resolve(Transition t, bool voiceAudible) const noexcept;
    [[nodiscard]] static constexpr Actions resolve(Mode, Retrigger, Transition, bool voiceAudible) noexcept;
};
```

- The table is a `static constexpr std::array<std::array<std::uint8_t, 5>, 4>` of the FR-034 cells
  (oscillatorRestart, envelopeRestart).
- Derivation, following FR-033:
  - phase = Reset iff osc ∧ retrigger == Reset;
  - dropTrigger = anchorRearm = envelopeRestart;
  - declick = (phase == Reset) ∧ audible;
  - dropSnap = ¬audible ∨ declick.
- `constexpr`, so SC-009(a)'s 80 cells and the Continue collapse are also `static_assert`ed.
- Default: AlwaysRetrigger + Reset.

**Envelope mapping in the harness (D-10).**
- envelopeRestart = 1 → `ADSREnvelope::gate(true)` (Hard).
- envelopeRestart = 0 → no gate call. Those cells are only Overlap and ReturnToHeld, where the gate is already on.

---

## S7. `RetriggerDeclick` (L1, `primitives/retrigger_declick.h`)

```cpp
class RetriggerDeclick {
public:
    static constexpr float kDeclickMs = 1.5f;
    void prepare(double sampleRate) noexcept;              // L_d = ceil(1.5·fs/1000), clamped fs [22050, 192000]
    void reset() noexcept;                                  // gain 1, idle
    void arm() noexcept;                                    // no-op while active (FR-043: the dip never restarts)
    [[nodiscard]] std::size_t samplesUntilSwitch() const noexcept;   // 0 when idle
    [[nodiscard]] float process() noexcept;                 // gain for this sample (C-15)
    void processBlock(float* gain, std::size_t n) noexcept;
    [[nodiscard]] bool isActive() const noexcept;
};
```

- The cosine is evaluated by a double rotation recurrence: (c, s) advanced by (cos π/L_d, sin π/L_d), seeded at
  `arm()`. The switch sample is written as exactly 0.0f.
- g₀ is the gain at arm time, which is 1.0 because a dip never restarts.
- Inactive: `process()` returns exactly 1.0f. The harness skips the multiply when inactive, so FR-044 holds bitwise.
- The same scalar multiplies L and R of both the core and the Anchor (FR-045, FR-061).

---

## S8. Test composition `NoteBehaviourHarness` (`dsp/tests/unit/systems/profundum_note_behaviour_test_helpers.h`)

**Members.**
- `ProfundumCore` (prepared with 64 partials; excitation off);
- `AnchorOscillator`;
- `ADSREnvelope` (Hard; A 2 ms, D 200 ms, S 0.3, R 300 ms unless a test overrides);
- `MonoHandler` (`setLegato(false)`, D-11);
- `NoteMemory`, `DropEnvelope`, `DropPhaseLock`, `RetriggerDeclick`.

Every array is fixed-size, sized for 512-sample host blocks.

**Event API.** `noteOn(note, vel, offset)`, `noteOff(note, offset)` and
`render(nSamples, hostBlock, Traces&)`. Traces are preallocated `std::vector`s, outside `AllocationScope` regions,
holding L, R, core L and R, Anchor L and R, d, d_c, f_core, `currentFrequency()`, Anchor `phase()`, `modeGain()`, ADSR
stage, gain, and event marks.

**Per event.** The harness runs `MonoHandler` in its default `MonoMode` (last-note priority). `MonoHandler` has no
active-note getter (`mono_handler.h:340–342` offers only `hasActiveNote()`), so the harness keeps `activeNote_`, a
mirror set from every event that changes the active note.
1. Read `wasHeld = mono.hasActiveNote()` and `audible = env.isActive()`.
2. Call `mono.noteOn` / `noteOff`.
3. Classify the event. Not every event is a transition:
   - **noteOn:**
     - ¬audible → FromSilence;
     - wasHeld → Overlap;
     - otherwise → Tail.

     Set `activeNote_ = note`.
   - **noteOff, inactive event** (`!ev.isNoteOn`: the last held key was released, `mono_handler.h:259–262`):
     call `env.gate(false)` so the ADSR enters Release, clear `activeNote_`, and **do not resolve**. The Drop, the lock
     and the oscillators keep running, which is what makes the later Tail and FromSilence transitions reachable.
   - **noteOff of a non-active held key** (`note != activeNote_`; MonoHandler returns `isNoteOn = true`,
     `retrigger = false` and the unchanged frequency, `mono_handler.h:265–270`): **no action**. The harness asserts that
     `ev.frequency` equals the active note's frequency and that `ev.retrigger == false`.
   - **noteOff of the active key with a key still held** (`note == activeNote_ && ev.isNoteOn`) → **ReturnToHeld**.
     Set `activeNote_` to the winner, found by inverting `ev.frequency`, which is exact for integer MIDI notes
     (`midiNoteToFrequency`).
   - POLY scripts call the `VoiceReuse` path directly from a `VoiceAllocator` Hard-steal event list (SC-009(c)).
4. `resolve` (transitions only).
5. If `declick`: `arm()`, store the pending note (the last call wins, FR-043), split the host block at
   `samplesUntilSwitch()`, and apply the actions at the new block's offset 0.
6. Apply the actions, in order:
   - core and Anchor `setRetriggerPhase(phase)`;
   - `drop.trigger(dropSnap)` if dropTrigger;
   - `lock.trigger(drop.plannedCurve(), f_res, gridPhase, kMinF0Hz, capHz, kind)`, which latches only; the solve runs
     at the next `lock.process` (S5.4). kind = `Bridged` if ¬dropSnap; otherwise `SnapReset` if the noteOn takes the
     oscillators' Reset branch (phase == Reset, or the first note since prepare/reset); else `SnapFreeRunning`;
   - `core.noteOn(f_res·2^{d₀/12})`, where d₀ is the next d_c (A on a snap);
   - `anchor.noteOn(f_res, anchorRearm)`;
   - `env.gate(true)` if envelopeRestart.

   All of these happen at the same block boundary (FR-051).
7. **Grid phase:** a counter reset to 0 at each Reset noteOn's offset 0, advanced by rendered samples, and passed as
   (16 − count % 16) % 16.

**Harness self-test (`ProfundumNoteBehaviour_HarnessClassification`).** Each scripted sequence of SC-009(b), SC-010,
SC-011 and SC-014 also carries its expected list of event outcomes, one per event: a `Transition` kind, `Release` or
`NoAction`. The harness records what it actually did, and the test asserts the two lists are equal element by element
before any audio assertion runs. Three positive controls: a script whose last noteOff must yield `Release` and then a
FromSilence after ≥ 1 s of silence; a non-active-key release that must yield `NoAction`; and an injected
misclassification (expected list altered by one element) that must fail.

**Per sample.**
- f_res = `mono.processPortamento()`;
- d = `drop.process()`;
- d_c = `lock.process(d)`;
- f_core = f_res·exp2(d_c/12) in float;
- the Anchor trajectory is f_res (LOCK, PUNCH, GHOST) or f_core (FOLLOW, with `lock.setBypass(true)`).

The core and the Anchor then render the sub-block with their trajectories. out = (core + anchor)·gain·env, where the
anchor's `setCoreH1Amplitude` is fed `deliveredGains()[0]·kBankHeadroom`-scaled h1 as in Phase 3's chain test.

---

## S9. RT safety, determinism, portability, denormals

- No allocation, lock, exception or I/O after `prepare` in any shipped component. SC-018 and SC-025 wrap every event
  and process call in an `AllocationScope`. They read the count through
  `AllocationDetector::instance().getAllocationCount()` **before the scope closes**, because
  `AllocationScope::getAllocationCount()` returns 0 until the destructor latches it (`allocation_detector.h:117–123,
  145–147`). Each test also carries a positive control: one deliberate `new` inside the same scope must read ≥ 1.
- **Determinism:** every component is a pure function of its call sequence. Counters are integers. Positions are
  derived from counters, never accumulated in float. The only seeded artefact is SC-006's script (seed 0x5EED0004).
- **Fast-math:** finiteness only through `detail::isFinite`. NaN/Inf tests live in `drop_note_behaviour_nonfinite_test.cpp`,
  added to the `-fno-fast-math` list (`dsp/tests/CMakeLists.txt` near line 1135).
- **Brace init:** designated initialisers for `Actions` and `DropCurve`. Double → float only through
  `static_cast`. No narrowing.
- **Denormals:**
  - **Denormal input is handled at the setter, not by FTZ.** Subnormal setter input is flushed to +0 by a bit test
    (S3.4, S3.6), so a subnormal Amount gives A = 0 exactly and d ≡ 0 (E-1). A plugin host does not guarantee FTZ/DAZ,
    so nothing relies on the test main's.
  - Given normal-or-zero parameters, the recurrence states are ≥ e^{−7.6} ≈ 5e-4, and the cosine state is bounded. The
    accumulator is a sum. None of them decays toward denormal range. The exponential tail inside the fillet is a
    polynomial, and d is exactly 0.0f from `endSample` on.
  - SC-017 asserts on its corner renders that no output sample of d, d_c or the declick gain is subnormal (bit test).
- No SIMD intrinsics, so the aligned-load lint does not apply. `node tools/check-portability.js` and
  `node tools/lint-apple-globals.js` run on every new or changed TU (SC-022). New namespace-scope names avoid the
  `MAC`/MacTypes collisions; the lint checks.
- The headers have no `static` mutable data. Tests reuse the Phase 3 source scan `anchorFindMutableStatics`.

---

## S10. Model evidence (`drop-model.js`, measured 2026-10-12)

| § | Section | Result |
|---|---|---|
| S10.1 | `shape` | α = 0.12495 / 0.49979 / 0.87338 / 0.87370 (exponential / linear / logarithmic / elastic fall). R² = 1.000000000 for the exponential and logarithmic fits; linear deviation 0. ρ = law (6 dp) in 15 cells. Elastic peaks −0.15, +0.0675, −0.0304 per unit A. 2 zero crossings. Continuity max \|Δp\| 0.00961 |
| S10.2 | `integral` | Relative error against Simpson-20000: GL8×1 1.1e-3; GL8×4 8.7e-9; GL16×4 5.8e-14 |
| S10.3 | `lock2all` | Clamp-aware EM-only predictor 0.70° worst; corrected θ ≤ 0.71°. Census in C-5 |
| S10.4 | `lock3 4` | Hybrid predictor (4 exact updates): 0.218° prediction, 0.2186° corrected θ, three rates. GL8 with 2/4 panels gives the same figure; GL8×1 gives 0.66° (`GLN=8 GLS=2 GLS_LONG=4`) |
| S10.5 | `perSample` | U = 1 predictor against the per-sample sum: 1.41e-5 cycles worst (C-13) |
| S10.6 | `slope`, `nyquist` | 3.8631 st per 16 samples is the bound; σ_up = 0.15 gives 2.40 st (3 st quantized). Uncorrected tail upswing 0.418 st per sample (C-9). 23 of 384 T = 10 ms cells are slope-infeasible (C-6). Bridge rise to A = 48: 480 samples |
| S10.6 | `glide` | Constant-rate duration error 0 samples; quantized step-time error ≤ 1 sample |
| S10.7 | CPU **estimate** (to be measured by T001) | DropEnvelope ≈ 2 ns/sample (1 multiply + Horner + branch); DropPhaseLock ≈ 1.5 ns/sample + one `exp2` per 16 samples; Quantized glide ≈ 1 ns; declick ≈ 0.3 ns averaged at 1.5 ms per 100 ms. Total ≈ 5 ns/sample ≈ 0.024 %. Trigger solves at one per 100 ms: typical ≈ 6 µs (0.006 %), worst case ≈ 4 150 integrand evaluations ≈ 21–42 µs (0.021–0.042 %), S5.4. Total **0.03 % typical, up to ≈ 0.066 % worst case** against a 0.05 % bar, so T001 decides and R-1 applies. E-4's every-sample retrigger costs one solve per sample (≈ 29 % of a core at typical cost). That is a pathological robustness case, recorded in the CPU lane (SC-019 row), not budgeted |
| S10.8 | `bound` | Solver-independent γ_an (C-5) over 1 536 cells: 0 cells with \|γ\| > 1.05·γ_an; worst \|γ\|/γ_an 1.0192; min ratio 0.60 / 0.85 / 0.92 / 0.99 at T = 10 / 60 / 200 / 1000 ms; 5 cells γ_an = +∞ (T = 10 ms, E0/E1, A = 48), gated at γ_cap |

**S10.7 measured.** Measured 2026-10-12, T005, pinned, isolated, median of 5 (`bench/drop-kernel-bench.cpp`,
clang -O2 -ffast-math -march=x86-64-v2, i9-13900HX performance cores, 48 kHz, block 64; full table in
`evidence/run-log/T005.txt`). F = (ns_A·48000 + 10·µs_B·1000)/1e9:

| Stage (R-1 levers, cumulative) | A ns/sample | B µs/solve (evals) | C µs/solve (evals) | Median F |
|---|---|---|---|---|
| 0 plain design | 7.21 | 40.10 (17) | 7.49 (11) | **0.0722 %** |
| 1 + (2) hoisted Hermite coefficients | 7.04 | 37.03 (17) | 6.97 (11) | **0.0708 %** |
| 2 + (3) cached 2^{d/12} per node × 2^{γb/12} | 5.74 | 12.32 (17) | 2.99 (11) | **0.0399 %** |
| 3 + (4) linearised start, 4 passes/candidate | 7.23 | 8.52 (9) | 2.31 (7) | **0.0433 %** |

Decision: the plain S5.4 solve is **out** (0.072 % > 0.05 %). No lever combination reaches the 0.033 % target: the
per-sample load (A, which no R-1 lever touches beyond (2)) alone measures 0.026–0.043 % across runs (5.4–9.0 ns per
sample; split ≈ envelope 2.9, lock + mirror 1.9, Quantized glide 1.2, declick 0.07). With levers (2)+(3)+(4) the
solve term is 0.0085 %, and F's median sits between the 0.033 % target and the 0.05 % bar.
**RULING 2026-10-12 (main loop, T005 outcome; bar unchanged at 0.05 %):** (i) levers (2) and (3) are REQUIRED in
T011 / T015; lever (4) only if T015 proves the solve residual < 1e-9 cycles on every SC-013 cell; (ii) two per-sample
reductions are REQUIRED where they keep the output bit-identical or within the existing bars: Quantized step
boundaries as integer sample indices precomputed at retarget (ceil(k·N/|Δ|), ≤ 128 entries, no per-sample division)
in T009, and the S5.6 mirror accumulating (f_h − f_res) once per 16-sample hold in double in T015 (SC-024 judges);
(iii) T005 is done without a re-run. The real CPU gates are T018 (components alone, pinned) and T028 (lane alone)
at the 0.05 % bar; 0.033 % is recorded as the advisory headroom target.

---

## S11. Test plan (file · TEST_CASE · assertion strategy)

### S11.1 Files and targets

| File | Target | Holds |
|---|---|---|
| `unit/primitives/retrigger_declick_test.cpp` | `dsp_primitives_tests` | `RetriggerDeclick_*` |
| `unit/processors/drop_c1_checker.h` | — | `dropCheckC1` (S11.2); std only plus `drop_curve.h`, included by the processors TUs and by the systems harness helper |
| `unit/processors/drop_envelope_test.cpp` | `dsp_processors_tests` | SC-001–SC-005, SC-024(a), `DropEnvelope_Lifecycle` (FR-002, FR-008) |
| `unit/processors/drop_phase_lock_test.cpp` | `dsp_processors_tests` | SC-024(b), SC-025 allocation, SC-026 bypass, `DropPhaseLock_C1`, `DropPhaseLock_TriggerCoalescing` |
| `unit/processors/note_memory_test.cpp` | `dsp_processors_tests` | SC-009(a) |
| `unit/processors/mono_handler_glide_test.cpp` + `mono_handler_pre_phase4_ref.h` | `dsp_processors_tests` | SC-006–SC-008, FR-024/FR-025/E-7/E-8/E-9, `MonoHandler_GlideModeNoAllocation` |
| `unit/processors/drop_note_behaviour_nonfinite_test.cpp` | `dsp_processors_tests` (`-fno-fast-math`) | SC-017 |
| `unit/processors/drop_note_behaviour_perf_test.cpp` | `dsp_processors_tests` | SC-019, SC-025 cost `[.perf]` |
| `unit/systems/profundum_note_behaviour_test_helpers.h` | — | harness, C¹ adapters, f_zc estimator |
| `unit/systems/profundum_note_behaviour_test.cpp` | `dsp_systems_tests` | `ProfundumNoteBehaviour_HarnessClassification`, `dropCheckC1` ↔ `anchorCheckC1` cross-check, SC-009(b)(c), SC-011, SC-012, SC-016, SC-018, SC-023, SC-026 (Anchor untouched) |
| `unit/systems/profundum_note_behaviour_spectral_test.cpp` | `dsp_systems_tests` | SC-010, SC-013, SC-014, SC-015, SC-021; full grids `[long]` with per-push smoke siblings |

`mono_handler_pre_phase4_ref.h` is produced by `extract-mono-ref.js` (S12.3) from `git show 3d8ca19f:…mono_handler.h`.
It wraps the class, `MonoNoteEvent`, `MonoMode`, `PortaMode` and `NoteEntry` in `namespace Krate::DSP::MonoRefPrePhase4`.
That is a distinct namespace, so there is no ODR clash.

### S11.2 Shared helpers

- **C¹ checker: `dropCheckC1`**, new, in `unit/processors/drop_c1_checker.h`. The Phase 3 `anchorCheckC1` **cannot be
  reused unchanged**: `AnchorSegmentSpec` has no bound field, its kinds are only Hold, Cosine and Hermite, and the
  checker computes every bound itself (`anchor_oscillator_test_helpers.h:131–160, 165`). Editing it would change a
  Phase 3 file.
  - `dropCheckC1(std::span<const double> g, std::span<const DropC1Segment> segs, double lo, double hi)` takes
    `struct DropC1Segment { std::size_t start, len; double bound; }`, with the bound supplied by the caller.
  - It applies the **same rules** as `anchorCheckC1`: interior |Δ²| ≤ 1.05·bound; boundary |Δ²| ≤ 1.05 × the larger
    adjacent bound; a zero bound with a non-zero Δ² fails; the range check is [lo, hi]. A unit test cross-checks it
    against `anchorCheckC1` on Hold, Cosine and Hermite segments in the systems TU, where both are visible, and
    expects identical pass/fail and ratios.
- **Segment bounds for d:** `DropCurve::pieceD2Bound(i)`.
- **Segment bounds for d_c = d + c** (SC-013(c), `DropPhaseLock_C1`): the segment breaks are the union of d's piece
  breaks, D_s and the carry end. Each bound is
  pieceD2Bound(i) + |γ|·32/D_s² + (the carry Hermite's peak Δ² over its span, else 0).
  - The carry Hermite's peak is `dropHermitePeakD2(c₀, c₀′, 0, L)` in `drop_c1_checker.h`. It uses the
    `anchorHermitePeakD2` formula, max(|6Δ − 4m₀|, |6Δ − 2m₀|)/L² (`anchor_oscillator_test_helpers.h:118–126`),
    restated there because `DropPhaseLock_C1` lives in `dsp_processors_tests`. The systems cross-check asserts that
    the two are equal.
  - 32 = max|b″| at x = 0 and 1 (C-5). D_s is in samples.
  - γ comes from `correctionGamma()`. That is acceptable here, because the C¹ check tests smoothness, not magnitude;
    the magnitude gate is C-5's solver-independent Γ.
- **Declick dip** (SC-011): the systems TU passes `anchorSegmentD2Bound` of a Cosine `AnchorSegmentSpec` as the
  segment's bound, reusing that function unchanged.
- **Positive control:** a cornered linear ramp (0 → 1 over 100 samples, then held) **fails**.
- **f_zc estimator (D-15):** the spec's half-period zero-crossing estimator, in double. It has its own self-test
  (`ProfundumNoteBehaviour_PitchEstimatorSelfTest`) against double-generated chirps stored as float, over 4 shapes ×
  A ∈ {12, 48} × T ∈ {10, 1000} ms × {E1, C2} at 48 kHz, with a 1 cent bar.
- **θ measurement:** `goertzelPhase` on core L and Anchor L over 4 periods of f_res from the landing sample. θ is the
  wrapped difference. As in Phase 3, the Anchor's phase reference is its sine output.

### S11.3 Per criterion

| SC | TEST_CASE(s) | Strategy and bars |
|---|---|---|
| SC-001 | `DropEnvelope_ShapeFamilies`, `DropEnvelope_ShapeContinuity` | `DropEnvelope` alone; A = 24, T ∈ {10, 100, 1000} ms, 3 rates, R = 0. α from the sample mean over [0, N]. Least-squares R² on ln p over u ∈ [0.05, 0.8] (exponential) and on ln(1 − p) over [0.2, 0.95] (logarithmic), each ≥ 0.999. Linear: \|α − 0.5\| ≤ 0.005 and max dev ≤ 0.01. Elastic: zero crossings ≥ 2, ε within ±2 % of 0.15·A, peak ratios ≤ 0.5, \|d\| ≤ 1 cent by 2T. Continuity: 64-step α and ε monotonic with adjacent bars; 192-step max \|Δp\| ≤ 0.025. Fitted rate and α recorded via `INFO` |
| SC-002 | `DropEnvelope_AmountTime` | d[0] = A within 1e-5. (a) d is exactly 0.0f and `!isActive()` from t_L + 1. (b) The first \|d\| ≤ 1 cent sample equals n_1c within ±1, where n_1c is computed in the test from `DropCurve::valueAt` in double by sample scan on the analytic curve, plus bisection between samples. Grid as the spec |
| SC-003 | `DropEnvelope_Rebound` | ρ within ±2 % of the law. Settle sample within 0.5·T + 1 after t_L (s ≤ 2/3) or by 2T. ρ = 0 exactly for R = 0, s ≤ 2/3 (min d ≥ 0). Monotone in R |
| SC-004 | `DropEnvelope_C1`, `DropEnvelope_C1CheckerPositiveControl`, `DropEnvelope_RetriggerContinuation` | `dropCheckC1` on every family × R ∈ {0, 1}. Bridged retriggers at mid-fall (u = 0.3), mid-rebound, mid-elastic and idle, over **T ∈ {1, 20, 200, 4000} ms × A ∈ {1, 12, 48}** at 44.1 and 96 kHz. Each bridge's brake and rise are separate checker segments. The test asserts that the grid exercises all four bridge branches, and records which cell hit each: L_br = 1 (steep incoming slope, T = 1 ms); L_br > 1 with excursion ≤ 0.5 st (mid-fall at T ≥ 20 ms); an L_min-bound rise (small ΔA, e.g. A = 1); a σ_up-bound rise (ΔA = 48). E-4 bound asserted as [−ρ_max − 2.7, A] (C-8 re-rule), and the bridge length equals L_br + L_r of S3.5 exactly |
| SC-005 | `DropEnvelope_BlockInvariance`, `DropEnvelope_RateInvariance`, `DropEnvelope_Latching` | Bitwise equality across blocks {1, 37, 512}. p(u) across rates within 1e-4. A mid-drop setter call leaves output and `plannedCurve()` bit-identical (memcmp of the struct) until the next trigger |
| SC-006 | `MonoHandler_GlideModeConstantTimeUnchanged` | Frozen reference against the live class, bitwise on `processPortamento()` and every `MonoNoteEvent` field. Covers the call sequences of `mono_handler_test.cpp` (replayed) and a 10 000-event script (noteOn/noteOff/setMode/setLegato/setPortamentoTime/setPortamentoMode/prepare/reset, seed fixed). The existing TU stays unmodified |
| SC-007 | `MonoHandler_GlideModeConstantRate` | Duration ±1 sample. Least-squares slope of 69 + 12·log2(f/440) (double from the float output) over the interior within 1e-6 relative of 12000/(T·fs). Mid-glide retarget slope, same bar. **σ_up floor pin:** an upward ConstantRate glide with r > 0.15 lasts exactly ⌈\|Δ\|/`DropCurve::kMaxUpwardSemitonesPerSample`⌉ samples. That pins `MonoHandler`'s private restatement (C-10), which cannot be `static_assert`ed because it is private and SC-020 forbids new public members, to the single L0 definition |
| FR-025 | `MonoHandler_GlideModeUnchangedSurfaces` | Replays SC-006's call scripts (the `mono_handler_test.cpp` sequences and the 10 000-event seeded script) under **ConstantRate and Quantized** against the frozen pre-Phase-4 reference running ConstantTime. **Bitwise** on every `MonoNoteEvent` field at every event (priority, stack, legato retrigger flags). At each noteOn/noteOff/setMode event, the glide-vs-snap decision must match the reference. The observable is whether the first `processPortamento()` after the event already equals the settled target frequency. Scripts use T ∈ {0, 50 ms}, so a glide always lasts > 1 sample. Explicit cells: first note ever snaps; staccato under `PortaMode::LegatoOnly` snaps (`mono_handler.h:186–199`); staccato under `Always` glides; overlap glides; after `reset()` the next note snaps. The settled pitch after each glide is bitwise equal to the reference's `semitoneToFrequency(target)`, so S4.2 computes `qFreq_` and the ConstantRate end value through the same `semitoneToFrequency(static_cast<float>(·))` |
| SC-008 | `MonoHandler_GlideModeQuantizedSteps` | Step count and size; step k at round(k·T·fs/(1000·\|Δ\|)) ±1; outputs within 1e-4 of `midiNoteToFrequency`; retarget on the grid (C-11); switch ≤ 1 st. `static_assert` on the enum values and the default |
| SC-009 | `NoteMemory_ResolverTable`, `ProfundumNoteBehaviour_Matrix`, `ProfundumNoteBehaviour_PolyVoiceReuse` | (a) 80 cells, `constexpr` and runtime; the Continue collapse by `memcmp`. (b) Runs only after `ProfundumNoteBehaviour_HarnessClassification` (S8) passes. The harness script per mode × Retrigger with the spec's per-transition assertions (Anchor `phase()` continuity ≤ 1e-9; core 0.0f at the effective sample; h1 `goertzelPhase` against a fresh instance within 1°; bitwise equality for same-note FreeRunning; ADSR stage; Drop `isActive`; `modeGain()`). (c) 2-voice `VoiceAllocator` Hard steal |
| SC-010 | `ProfundumNoteBehaviour_RetriggerClickFree`, `ProfundumNoteBehaviour_GlideClickFree`, `ProfundumNoteBehaviour_DeclickPositiveControl` | `ClickDetector` (defaults) reports 0. Sample-delta bound 1.1·(G_max·D_max + S_max·P_max), with S_max from C-15 and D_max from the analytic Drop slope at the effective sample. Glides per the spec. Three positive controls flagged |
| SC-011 | `RetriggerDeclick_Timing`, `ProfundumNoteBehaviour_DeclickTransparency` | Latency L_d − 1 recorded; gain exactly 0.0f at the switch; dip passes the Cosine C¹ checker; re-arm mid-dip leaves the gain trace bit-identical to a single arm; non-declick transitions bitwise identical with and without the declick; Anchor L == R |
| SC-012 | `ProfundumNoteBehaviour_PhaseReference`, `ProfundumNoteBehaviour_Routing` | As specified. (b) also records the staircase error per cell (≈ 27 cents at 48 st over 60 ms) |
| SC-013 | `ProfundumNoteBehaviour_PostDropLanding` (`[long]`, full grid), `..._PostDropLandingSmoke` (per-push: E1, 48 kHz, A ∈ {12, 48}, T ∈ {60, 200}, 4 shapes, R ∈ {0, 1}), `DropPhaseLock_C1` | (a) \|θ\| ≤ 1° on feasible cells; infeasible cells (C-6) must equal the enumerated set in `evidence/sc013-infeasible.json` and are recorded. 10 s drift per Phase 3 SC-003. (b) d_c exactly 0.0f from D_s; `currentFrequency()` == clamp(f_res); Anchor bitwise against bypass. (c) peak \|d_c − d\| ≤ 1.05·Γ + 1 ulp(A) (C-5 re-rule). Γ = min(γ_an, γ_cap) + measured carry, where γ_an is computed **in the test** from r = n* − Φ_ref(0), f_res, D and `DropCurve::valueAt`, and Φ_ref(0) is the test's own staircase sum over the bypass render. It never reads `correctionGamma()`, `correctionPeakBound()` or `predictedExcessCycles()`. Recorded per T with \|γ\|/Γ; `dropCheckC1` with the d_c bounds of S11.2, plus positive control; `solveResidualCycles()` recorded per cell. (d) bridged retriggers mid-fall, mid-rebound and mid-elastic; Reset zeroes the accumulator; **FreeRunning snap after an interrupted Drop:** T = 4000 ms, A = 48, a 300 ms release, the ADSR idle while the Drop is still in flight, then a FreeRunning snap noteOn (OscillatorContinuous, and Retrigger = Continue) with a second Drop (A = 12, T = 200 ms), at E1 and C3, 48 kHz. Gate \|θ\| ≤ 1° at the second landing and assert the accumulator was **not** zeroed (it equals the pre-trigger mirror value). Glide-in-flight recorded. Negative control: with bypass in LOCK, ≥ 1 cell \|θ\| > 90° (model: 179.9°) |
| SC-014 | `ProfundumNoteBehaviour_Audibility`, `ProfundumNoteBehaviour_PitchEstimatorSelfTest` | Descriptors on d_core plus f_zc after the self-test; bars as in the spec. Glide plateau count on d_core = 24 |
| SC-015 | `ProfundumNoteBehaviour_TrajectoryNyquist` | Block 1; at every sample, (highest k with `deliveredGains()[k] != 0` + 1) × `currentFrequency()` < fs/2. Trajectory and margin CSV to `evidence/`. Note that "T at minimum" for the glides is the smallest positive T, so the C-10 floor binds |
| SC-016 | `ProfundumNoteBehaviour_MonoIdentity` | L == R memcmp for the Anchor and the core in every SC-010 render |
| SC-017 | `DropEnvelope_NonFinite`, `MonoHandler_GlideModeNonFinite` | NaN and ±Inf bit patterns (via `std::bit_cast`) on every setter: **previous value kept**. Subnormal bit patterns (±`bit_cast<float>(1u)`, the largest subnormal): **flushed to 0, then clamped** (S3.4). `setAmount(subnormal)` gives d ≡ 0.0f bit-identically (E-1), `setShape` and `setRebound` give exactly 0, `setTime` gives 1 ms. An out-of-enum `GlideMode`/`Mode` is ignored. 10 s corner renders are finite, and no sample of d, d_c or the declick gain is subnormal (exponent-bit test) |
| SC-018 | `ProfundumNoteBehaviour_NoAllocation` | One `AllocationScope` region covers: harness event and render calls (traces preallocated); `MonoHandler::setGlideMode` for **every ordered pair** of the 3 modes, mid-glide; every `NoteMemory` setter and `resolve`; every `DropEnvelope` setter, `trigger(true/false)`, `process`/`processBlock`; `DropPhaseLock::trigger` + the solving `process`, and `setBypass`; `RetriggerDeclick::arm`/`process`/`processBlock`. The count is read through `AllocationDetector::instance().getAllocationCount()` **before the scope closes** (S9) and must be 0. Positive control: a deliberate `new` in the same kind of scope reads ≥ 1. `MonoHandler_GlideModeNoAllocation` repeats the mode-switch part in `dsp_processors_tests` |
| SC-019 | `ProfundumNoteBehaviour_CpuBudget` `[.perf]` | Spec load, 48 kHz, block 64, 60 s of audio, median of 5, run through `node tools/run-cpu-tests.js dsp_processors_tests` alone; ≤ 0.05 %. Also in the CPU lane: `DropPhaseLock_TriggerBurst` **(gated)**: 64 triggers at one offset followed by one `process()` must cost ≤ 1.5× one trigger plus `process()`, which proves the lazy solve (S5.4). **Recorded only, no bar:** the worst-case solve time (the S5.4 case), and µs per sample of an E-4 every-sample retrigger render of the composition (E-4 is a robustness case). The functional half of the burst test is per-push in `drop_phase_lock_test.cpp`: `DropPhaseLock_TriggerCoalescing` asserts that 64 triggers at one offset give d_c bit-identical to the last trigger alone |
| SC-020 | suites + `check-append-only.js` | See S12.3. The checker fails on any removed line, and on any set of added public members other than exactly {`setGlideMode`, `glideMode`}. Its positive controls must fail |
| SC-021 | `ProfundumNoteBehaviour_UseCaseRenders` | Four presets (S11.4) × {E1, C2}; WAV + CSV to `evidence/`; finite peak ≤ ceiling; 0 clicks; pairwise distinctness bars |
| SC-022 | comply | Output of both tools quoted |
| SC-023 | `NoteMemory_PhasePolicyMirror` (systems TU) | `static_assert` on underlying values, plus `DropPhaseLock::kPitchUpdateInterval == ProfundumCore::kPitchUpdateInterval == AnchorOscillator::kPitchUpdateInterval`. σ_up has one definition (`DropCurve`, C-12); the test `static_assert`s that the `DropEnvelope` and `DropPhaseLock` aliases equal it, and the SC-007 floor pin covers `MonoHandler`'s private copy |
| SC-024 | `DropEnvelope_PhaseIntegral`, `DropPhaseLock_PredictedExcessPhase` | (a) `phaseIntegralSeconds()` against the test's adaptive-Simpson brute force (double, 2·10⁵ panels per piece) within 1e-6 relative. (b) `predictExcess(..., U = 1, …)` against the per-sample double sum within **2e-5 cycles** (C-13) |
| SC-025 | `DropPhaseLock_NoAllocation`, `DropPhaseLock_TriggerCost` `[.perf]` | 0 allocations, read through `AllocationDetector::instance().getAllocationCount()` before the scope closes, with a positive control (S9); median trigger cost (trigger + the solving `process()`) at T = 4000 ms ≤ 2× the cost at 1 ms |
| FR-002, FR-008 | `DropEnvelope_Lifecycle` | Before `prepare`: `process()` returns 0, and `trigger`, every setter and `processBlock` have no effect (after `prepare`, the output equals that of a never-touched instance, memcmp of the trace). `reset()` keeps the parameters: a trigger after reset renders bit-identically to a trigger on an instance given the same setters once. `processBlock(nullptr, n)` and `processBlock(p, 0)` leave the next `process()` output bit-identical to an instance that never received the call. `currentOffset()` equals the last returned sample after every `process()` and after each `processBlock` of {1, 37, 512} |
| SC-026 | `DropPhaseLock_FollowBypass`, `ProfundumNoteBehaviour_AnchorUntouched` | Bitwise d_c == d; Anchor `phase()` and output bitwise between lock on and bypass in LOCK, PUNCH and GHOST |
| SC-027 | comply | The clause in S14 is quoted |

### S11.4 Use-case presets (FR-068)

| Preset | A (st) | T (ms) | s | R | Anchor |
|---|---|---|---|---|---|
| 808 attack | 12 | 120 | 0 | 0 | LOCK |
| Cinematic impact | 36 | 900 | 2/3 | 0.3 | GHOST |
| Techno pluck | 7 | 25 | 1/3 | 0.5 | PUNCH |
| Kick/bass hybrid | 48 | 45 | 1 | 1.0 | LOCK |

Pairwise, these clear at least one SC-021 bar (start pitch ≥ 6 st, t_s ≥ 2×, α ≥ 0.1, ρ ≥ 0.05·A). The test asserts
it.

---

## S12. Build integration

### S12.1 `dsp/tests/CMakeLists.txt` (enumerated lists, each with a Phase 4 comment block)

- **`dsp_primitives_tests`** (line 87 block): add `unit/primitives/retrigger_declick_test.cpp`.
- **`dsp_processors_tests`**, after line 259 (`harmonic_fold_nonfinite_test.cpp`):
  - `drop_envelope_test.cpp`
  - `drop_phase_lock_test.cpp`
  - `note_memory_test.cpp`
  - `mono_handler_glide_test.cpp`
  - `drop_note_behaviour_nonfinite_test.cpp`
  - `drop_note_behaviour_perf_test.cpp`
- **`dsp_systems_tests`**, after the last Phase 3 entry (line 651, before the closing paren at 652): `profundum_note_behaviour_test.cpp` and
  `profundum_note_behaviour_spectral_test.cpp`.
- **`-fno-fast-math` list** (last entry line 1138, `PROPERTIES` line 1139): add `unit/processors/drop_note_behaviour_nonfinite_test.cpp`. The perf
  TU stays out.

### S12.2 Commands and parallelism (Windows, full CMake path)

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
# one rebuild for all three touched exes (and the shared-header consumers, FR-063), parallel:
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_primitives_tests dsp_processors_tests dsp_systems_tests ruinae_tests seraphis_tests vorago_tests innexus_tests -j
# per-push lanes, concurrently (none is timing-sensitive):
build/windows-x64-release/bin/Release/dsp_processors_tests.exe "~[.perf]~[long]" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_primitives_tests.exe "~[.perf]~[long]" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "ProfundumNoteBehaviour*" "~[long]" 2>&1 | tail -5
build/windows-x64-release/bin/Release/ruinae_tests.exe 2>&1 | tail -5     # + seraphis/vorago/innexus, concurrent
# [long] lane, concurrent with the above, sharded by name:
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "ProfundumNoteBehaviour_PostDropLanding" 2>&1 | tail -5
# CPU lane ALONE, after everything else finished and the machine idled:
node tools/run-cpu-tests.js dsp_processors_tests
```

`ctest -R` is not used for single suites (CLAUDE.md).

### S12.3 Node scripts (spec folder)

- `drop-model.js`: this plan's evidence.
- `extract-mono-ref.js`: writes `mono_handler_pre_phase4_ref.h` from `git show 3d8ca19f:dsp/include/krate/dsp/processors/mono_handler.h`.
- `check-append-only.js`: SC-020. It parses `git diff 3d8ca19f -- dsp/include/krate/dsp/processors/mono_handler.h`
  and fails on any `-` line. It then **gates the public surface**:
  - It walks the post-change file, tracking the access section of every class body (`public:`, `protected:`,
    `private:`, with `class` defaulting to private and `struct` to public, and nested braces counted).
  - It extracts the declared name of each added member declaration.
  - It fails unless the set of added names in a public section of `MonoHandler` is **exactly** {`setGlideMode`,
    `glideMode`}. So a public `processPreciseGlide`, `preciseRetarget`, `preciseRetime` or
    `currentPreciseSemitones` fails, and so does a missing `glideMode`.
  - Namespace-scope additions (the `GlideMode` enum) are not members, and are listed.
  - Positive controls (`--self-test`): synthetic diffs that remove a line, add a public helper, or omit
    `glideMode` must each fail.

### S12.4 Expected runtimes

| Lane | Estimate |
|---|---|
| Per-push Phase 4 cases | ≈ 40 s (systems, dominated by SC-010's 4 notes × 3 rates × 3 blocks × 4 modes × 2 states) |
| SC-013 full grid `[long]` | ≈ 1 536 cells × ~12 s of audio (landing + 10 s drift) ≈ 5 h of audio at ≈ 150× real time ≈ 2 min per shard. Shard by note (4 concurrent processes) |
| Shared-header suites | Their known wall clocks, concurrently |

---

## S13. Risks and mitigations

| # | Risk | Mitigation |
|---|---|---|
| R-1 | CPU over 0.05 % (C-16) | **Measured by T005 (2026-10-12, pinned, median of 5): plain 0.0722 %, levers (2)+(3) 0.0399 %, +(4) 0.0433 % (A-noise); main-loop ruling: (2)+(3) required, (4) only if proven, plus integer quantized step indices and per-hold mirror accumulation; T018 / T028 gate at 0.05 %.** T001 microbench first. Levers in order: (1) skip all work while idle (`isActive()` fast path); (2) hoist the Hermite coefficients to the piece entry; (3) amortise the predictor's quadrature by reusing node values between Newton passes (the integrand changes only through γ·b, so cache 2^{d/12} per node and multiply by 2^{γb/12}); (4) cut the Newton passes with the linearised start; (5) the lazy solve (S5.4) already removes same-offset bursts. Never relax the bar |
| R-2 | Bank phase in a fast drop differs from Σ f/fs (base chase, multiplier float chain) | Phase 3 measured ≤ 0.123° over 60 s static. A direct probe in SC-013's smoke checks the model's θ figure (≤ 0.22°) against the C++ render at E1/C3 before the full grid. If the gap is > 0.3°, investigate `refreshMultipliers` (`profundum_core.h:722–731`) before changing the predictor |
| R-3 | Float/double mismatch between the harness's float f_core and the predictor | The predictor mirrors `static_cast<float>` on f_core at the 4 exact updates. Remaining effect ≤ 6e-8 relative × Φ, below 1e-4 cycles |
| R-4 | Newton fails near the clamp (flat Φ̂) | Bracketed, with a bisection fallback (S5.4). Infeasibility is reported, never looped |
| R-5 | Recurrence drift in `DropEnvelope` diverges from `DropCurve::valueAt` | Re-seed per region. SC-024(a) and SC-001 assert against the closed form; a unit check bounds the per-sample difference ≤ 1e-9 st |
| R-6 | `MonoHandler` diff accidentally edits a line (formatter) | `check-append-only.js` in comply; clang-format is not run on the file |
| R-7 | Portability: `std::exp2` on double in a constexpr context; MSVC accepts narrowing in places GCC rejects | No constexpr math; WSL GCC build plus `check-portability.js` before commit |
| R-8 | ODR | New names `DropCurve`, `DropEnvelope`, `DropPhaseLock`, `NoteMemory`, `RetriggerDeclick`, `GlideMode`, `NoteBehaviourHarness`, `MonoRefPrePhase4` are re-swept by T000 (`grep -rnE "(class\|struct\|enum class\|using) <Name>\b" dsp/ plugins/ tests/ tools/`) |
| R-9 | Denormals in the double accumulator, the recurrence or setter input | Setter input flushed at the setter by a bit test (S3.4); states bounded below by construction (S9). Nothing relies on FTZ/DAZ; SC-017 asserts no subnormal output |
| R-10 | `[long]` SC-013 grid too slow | Shard by note across 4 processes; recompute the 10 s drift window only on the 48 kHz subset if a shard exceeds 15 min (recorded, not relaxed: the drift bar applies to every cell rendered) |

---

## S14. Handoff clauses (written into the roadmap by comply)

- **Phase 11 (FR-027, SC-027):** "The single portamento knob is labelled per Glide Mode: milliseconds for
  ConstantTime (the whole glide), milliseconds per octave for ConstantRate, milliseconds for Quantized (the whole
  underlying ramp). `MonoHandler` has no second glide setter (Phase 4 FR-026)."
- **Phase 9 (C-9, C-10, O-2):**
  - A pitch-aware trajectory guard is needed for voices whose 64·f_res·1.25 approaches the core cap (≳ 180 Hz at
    44.1 kHz), because Drop tail upswings and ConstantTime glides are not slope-bounded in Phase 4.
  - The voice tracks the core's pitch-grid phase for `DropPhaseLock::trigger`.
  - `landingFeasible()` false cells (T ≲ 10 ms at E0/E1) are recorded in the capability table.

---

## S15. Implementation order (input to tasks.md)

| Task | Work |
|---|---|
| T000 | ODR re-sweep (R-8) |
| T001 | Microbench (C-16): the S3.4 kernel and the S5.4 solve at its **worst case** (bridged, T = 1 ms, A = 48, s = 1, R = 1, 17 evaluations forced), not a typical trigger |
| T002 | Ratification of S1 |
| T003 | `extract-mono-ref.js` + frozen ref + SC-006 test (red only once the new modes exist; green on ConstantTime throughout) + `check-append-only.js` with its self-test |
| T004 | `MonoHandler` glide modes + SC-007/SC-008/FR-024 + `MonoHandler_GlideModeUnchangedSurfaces` (FR-025) + `MonoHandler_GlideModeNoAllocation` tests (test first) |
| T005 | `drop_curve.h` (owning the C-12 constants) + `drop_c1_checker.h` + `DropEnvelope` + SC-001–SC-005, SC-024(a), `DropEnvelope_Lifecycle` |
| T006 | `NoteMemory` + SC-009(a) |
| T007 | `RetriggerDeclick` + `RetriggerDeclick_Timing` |
| T008 | `DropPhaseLock` (lazy solve, capped evaluations) + SC-024(b), SC-026 bypass, `DropPhaseLock_C1`, `DropPhaseLock_TriggerCoalescing` |
| T009 | Harness (noteOff paths, S8) + `ProfundumNoteBehaviour_HarnessClassification` + `dropCheckC1` cross-check + SC-009(b)(c), SC-011, SC-012, SC-016, SC-018, SC-023 |
| T010 | SC-013 smoke + R-2 probe, then the full grid |
| T011 | SC-010, SC-014, SC-015, SC-021 |
| T012 | Shared-header suites (FR-063) |
| T013 | Portability + lint + clang-tidy (`dsp` target) |
| T014 | CPU lane alone |
| T015 | Comply + handoff clauses |

---

## Open questions for the user

- **O-1 (C-10):** keep SC-015's formula (index × `currentFrequency()`) and the slope bounds it forces, or re-rule
  SC-015 to measure the *rendered* partial frequency? The core already freezes partials at the cap
  (`profundum_core.h:700–712`), and the re-rule would remove C-6 and C-8's rise lengthening.
- **O-2 (C-10):** ConstantTime glides shorter than |Δ|/(0.15·fs) can violate SC-015's formula, and FR-021 forbids
  bounding them. Accept and hand off to Phase 9/11 (for example a minimum non-zero glide time in the Phase 11 range),
  or bound in the composition?
- **O-3 (C-6):** at T = 10 ms, choose (a) gate feasible cells and record the 23 infeasible ones (plan default),
  (b) drop T = 10 ms from SC-013's grid, or (c) let the correction ignore the slope bound.
- **O-4 (C-5):** the correction reaches 47.9 st at 10 ms and 9.78 st at 60 ms on E0. Is that musically acceptable,
  or should a correction ceiling (for example 12 st, beyond which θ is recorded) be ruled?

---

## Review notes

- 2026-10-12 plan stage: S1 written from the `drop-model.js` measurements above. C-2, C-3, C-4, C-6, C-7 and C-9
  change mechanism or wording and need the user's ratification. C-6 also needs a choice (O-3).
- 2026-10-12 plan review, revision 1. Every issue was applied, and none was rejected.
  - **Correction of the earlier note:** the first draft said "no spec bar was relaxed". That was wrong in two places,
    and both are now explicit re-rules:
    - C-5 adds a 1-ulp(A) floor to SC-013(c)'s 1.05 × analytic bar. It was 2 ulp, unlabelled; it is now 1 ulp, derived
      from the two float casts.
    - C-8 widens E-4's bound and replaces FR-007's 1 ms bridge with the L_br + L_r law.
  - **C-5:** SC-013(c)'s analytic peak is now independent of the solver (γ_an from r, f_res, D, the profile's 15/8 peak
    factor and the clamp), and `drop-model.js bound` validated it: 0 of 1 536 cells over. The old gate,
    `correctionPeakBound()` = |γ|, was circular.
  - **S5.5:** `SnapFreeRunning` keeps the accumulator. Only `SnapReset` zeroes it, as FR-059 says. This was verified
    against `sounding_` (`profundum_core.h:441, 528, 602`; `anchor_oscillator.h:169, 481–483`). SC-013(d) gains the
    interrupted-Drop cell.
  - **S8:** the noteOff paths (release, non-active release, ReturnToHeld) now follow `mono_handler.h:259–270`. A
    classification self-test gates the scripted renders.
  - **S11.2:** `anchorCheckC1` is not reused. A Drop-specific `dropCheckC1` takes explicit per-segment bounds,
    including the d_c bound.
  - **S9, SC-018, SC-025:** allocations are read live, with positive controls (`allocation_detector.h:117–123`).
  - **C-12, S3.3:** the curve constants have a single L0 definition.
  - **S5.4:** a lazy, coalesced solve with a hard evaluation cap and a defined fallback. The worst-case count was
    corrected (≤ 4 148), and C-16 now states honestly that the worst case can exceed 0.05 % until T001 measures it.
  - **S3.4, SC-017:** subnormal setter input is flushed to 0, consistent with FR-003's clamp rule. "Keep the previous
    value" would contradict FR-003 for a finite input.
  - `check-bars.js` and `bars-baseline.json` were **dropped**: no FR or SC asks for a baseline comparison. SC-013's
    infeasible-set check already lives in the test (`evidence/sc013-infeasible.json`).
  - The ledger citations for render_fingerprint, allocation_detector and the CMake lines were corrected.
