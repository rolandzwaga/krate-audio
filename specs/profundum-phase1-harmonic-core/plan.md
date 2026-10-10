# Implementation Plan: Profundum Phase 1 — Harmonic Core

**Spec:** `specs/profundum-phase1-harmonic-core/spec.md` (Draft, challenged, clarified 2026-10-09)
**Roadmap:** `specs/Profundum-roadmap.md` Part A → Phase 1 (lines 194–242)
**Status:** Plan (2026-10-09, revised after review the same day). Not yet task-broken. **Nine spec
conflicts/refinements (S1) need ratification before `tasks.md`**: two of them (C-1, C-2) are SC-004 clauses that
no recipe can satisfy, by proof; C-8 (cap taper law, with an SC-001 wording amendment) and C-9 (FR-042 grid
origin) came out of the review.
**Calibration record:** `specs/profundum-phase1-harmonic-core/recipe-model.js`, a double-precision Node model of
the S3 law that re-runs every recipe-side gate in about 6 s. Every recipe constant in S3 came out of it, and S3.6
quotes its output. Re-run it after **any** constant change. The C++ tests remain the pass/fail authority.

Components and layers:

| Component | Layer | Header | New / changed |
|---|---|---|---|
| `SpectralShapeRecipe` | 2 | `dsp/include/krate/dsp/processors/spectral_shape_recipe.h` | new |
| `ProfundumCore` | 3 | `dsp/include/krate/dsp/systems/profundum_core.h` | new |
| `HarmonicOscillatorBank::restoreCenterPan()` | 2 | `dsp/include/krate/dsp/processors/harmonic_oscillator_bank.h` | one append-only method (FR-064) |

---

## S0. Verification ledger — every reused signature below was read this session (2026-10-09)

Paths relative to `dsp/include/krate/dsp/` unless stated.

| Fact | Source (file:line) | Quoted / observed |
|---|---|---|
| Bank class, layer 2 | `processors/harmonic_oscillator_bank.h:75`, `:2` | `class HarmonicOscillatorBank {` — "Layer 2: DSP Processor" |
| Bank constants | `:82`, `:85`, `:88`, `:91` | `kDefaultCrossfadeTimeSec = 0.003f`, `kAmpSmoothTimeSec = 0.002f`, `kAntiAliasFadeStart = 0.8f`, `kOutputClamp = 2.0f` |
| Non-copyable, movable | `:117–120` | deleted copy, defaulted move |
| `prepare` | `:128–129` | `/// @note NOT real-time safe` (`:128`), `void prepare(double sampleRate) noexcept` — computes `ampSmoothCoeff_ = 1 - exp(-1/(0.002·fs))` (`:137–138`), designs two noise-LP biquads (`:145–153`), runs a 1 024 + 32 768-iteration LCG→biquad noise-calibration loop (`kWarmup`/`kMeasure`, `:158–182`; useless here, bandwidth is always 0), `crossfadeThresholdRatio_ = semitonesToRatio(1.0f)` (`:191`), calls `reset()` (`:193`) |
| `reset` | `:202–240` | `/// @note Real-time safe` (`:202`); `sinState_.fill(0)`, `cosState_.fill(1)`, `currentAmplitude_.fill(0)`, `frameLoaded_ = false`, `panPosition_.fill(0)`, function-local `constexpr float kCenterGain = 0.7071067811865476f;` (`:225`), `panLeft_/panRight_.fill(kCenterGain)` (`:226–227`), `detuneMultiplier_.fill(1.0f)` (`:228`), `panBaseValid_ = detuneBaseValid_ = false` (`:231–232`) |
| `loadFrame` | `:255–256` | `void loadFrame(const HarmonicFrame& frame, float targetPitch, bool skipNormalization = false) noexcept` |
| — crossfade trigger | `:260–268` | ratio of `targetPitch / targetPitch_` (inverted if < 1) `> crossfadeThresholdRatio_` → snapshot `lastOutputSample_`, start 3 ms crossfade |
| — phase seeding | `:286–293` | only when `!frameLoaded_`: `sinState_[i] = sin(partial.phase)`, `cosState_[i] = cos(partial.phase)`, `currentAmplitude_[i] = 0` |
| — frequency/AA recompute | `:362–363` | `recalculateFrequencies(); recalculateAntiAliasing();` every call |
| `setTargetPitch` | `:386–402` | `if (!prepared_ \|\| frequencyHz <= 0.0f) return;` (NaN passes), same crossfade trigger, recompute |
| `setDetuneSpread` restore semantics | `:576–601` | cached-base copy only when `detuneBaseValid_ && clamped == detuneSpread_` |
| `stateFinite` | `:623–631` | bit-test via `detail::isFinite` |
| `applyPanOffsets` | `:642–654` | `newPos = clamp(panPosition_[i] + offsets[i], -1, 1)`; `panLeft_[i] = cos(π/4 + newPos·π/4)`, `panRight_[i] = sin(...)` over all `kMaxPartials` |
| `applyExternalFrequencyMultipliers` | `:663–670` | `detuneMultiplier_[i] *= multipliers[i];` for all `kMaxPartials` — **in place** |
| `processStereo` | `:682–795` | SIMD path when `!hasBandwidth_` (`:701–707`); the 1e-8 tail floor applies only to lanes ≥ `activePartials_` (`:755`); crossfade blends the *mono* `crossfadeOldLevel_` into both channels (`:781–787`); clamp ±2 (`:790–791`); `lastOutputSample_ = (left + right) * 0.5f` (`:793`), read only when a crossfade starts (`:265`, `:394`) |
| `processStereoBlock` | `:802–821` | null/zero → return; `!prepared_` → zero-fill; else per-sample `processStereo` |
| `computePartialFrequency` | `:1012–1016` | `(n + deviation·inharmonicityAmount_) · targetPitch_` |
| `recalculateFrequencies` | `:1046–1057` | `eps = 2·sin(π·freq/fs)`, clamped ±1.99 |
| `recalculateAntiAliasing` | `:1061–1092` | `freq = computePartialFrequency(i) · detuneMultiplier_[i]`; `mcfCorrection = cos(π·freq/fs)` (≥ 0); linear fade 0.8·Nyq → Nyq |
| `recalculateDetuneMultipliers` | `:1134–1149` | spread 0 → `detuneMultiplier_[i] = 1.0f` |
| SIMD kernel | `processors/harmonic_oscillator_bank_simd.cpp:80–110` | amplitude smoother `vTargetAA = Mul(vTarget, vAA)`, `vAmp = MulAdd(vCoeff, Sub(vTargetAA, vAmp), vAmp)` (`:91–93`), with no denormal floor; `hn::LoadU`/`StoreU` (unaligned); `vSumL = MulAdd(ampSample, panL, vSumL)`, `vSumR = MulAdd(ampSample, panR, vSumR)` — **identical op sequence per channel**, so `panL == panR` bitwise ⇒ `L == R` bitwise; `epsEff = Clamp(eps·detune, ±1.99)` (`:103`) |
| SIMD declaration | `processors/harmonic_oscillator_bank_simd.h:33–46` | `void processMcfBatchSIMD(float* sinState, ..., float& sumR) noexcept` |
| `kMaxPartials`, `Partial`, `HarmonicFrame` | `processors/harmonic_types.h:21, 36–47, 54–63` | `inline constexpr size_t kMaxPartials = 96;` `Partial{harmonicIndex, frequency, amplitude, phase, relativeFrequency, inharmonicDeviation, stability, age, sourceId, bandwidth}`; `HarmonicFrame{f0, f0Confidence, partials, numPartials, ...}` |
| `detail::isFinite` | `core/db_utils.h:118` | `[[nodiscard]] KRATE_DETAIL_FORCEINLINE constexpr bool isFinite(float x) noexcept` (barrier read) |
| `ScopedDenormalMode` | `core/scoped_denormal_mode.h:27, 37–74` (class `:60`) | RAII FTZ/DAZ guard, constructed at the top of a plugin `process()`; sets MXCSR only when `KRATE_HAS_SSE_DENORMAL_CONTROL` (x86, `:37–43`); on AArch64 it compiles empty and relies on the default FPCR flush (`:27`) |
| Phase-reset idiom | `plugins/membrum/src/dsp/unnatural/mode_inject.h:119–126` | `bank_.reset();` then `bank_.loadFrame(frame_, f0, /*skipNormalization=*/true);` |
| Test helpers | `tests/test_helpers/low_frequency_metrics.h:52, 64, 82, 118, 180` | namespace `Krate::DSP::TestUtils::lowFreq`; `kLowFrequencyFftSize = 262144`; `magnitudeSpectrum(const float*, size_t, std::vector<float>&)`; `harmonicMainLobePower(const std::vector<float>&, double harmonicHz, double binHz)` |
| | `tests/test_helpers/artifact_detection.h:38, 99–130` | `ClickDetectorConfig{sampleRate, frameSize=512, ...}`; `ClickDetector(cfg)`, `prepare()`, `detect(const float*, size_t)` → `std::vector<ClickDetection>` |
| | `tests/test_helpers/allocation_detector.h:111–128` | `AllocationScope` (count captured in the destructor — tests use `AllocationDetector::instance().startTracking()` / `stopTracking()` directly so the count is readable in scope) |
| | `tests/test_helpers/render_fingerprint.h:63–73, 122` | `fingerprintRender(std::span<const float>)`, `compareFingerprints(...)`, `kMetricTolerance = 2.5e-4` |
| Test exes / FTZ | `dsp/tests/CMakeLists.txt:156–158`, `:324`, `:666–1040` | `dsp_test_main.cpp` (shared main, sets FTZ/DAZ) in every exe; `dsp_systems_tests` list; the single `-fno-fast-math -fno-finite-math-only` `set_source_files_properties` block (Clang/GNU only), which already contains `harmonic_oscillator_bank_tests.cpp` (`:903`) |
| Library header lists | `dsp/CMakeLists.txt:128, 159–180` | `KRATE_DSP_PROCESSORS_HEADERS`, `KRATE_DSP_SYSTEMS_HEADERS` (IDE listing; Vorago/Seraphis add theirs with a spec comment) |
| CPU runner filter | `tools/run-cpu-tests.js:56` | `'[performance],[perf],[.perf],[benchmark],[!benchmark],[long]~[vorago-sweep]'` |
| Perf tag convention | `dsp/tests/unit/systems/vorago_perf_test.cpp:812, 971` | `[systems][vorago][.perf]` (hidden) |
| Portability tools | `tools/check-portability.js`, `tools/lint-apple-globals.js`, `tools/lint-simd-aligned-loadstore.js` | present |

ODR sweep (`grep -rn "class X\|struct X" dsp plugins tests`, 2026-10-09): `ProfundumCore`, `SpectralShapeRecipe`,
`restoreCenterPan`, `kPitchUpdateInterval`: **0 hits**. The observers added in review (`maskFrequency`,
`baseFrequency`) are `ProfundumCore` members and cannot collide. The test-support namespace `Krate::DSP::ProfundumTest`:
0 hits. None of the roadmap hazard names is used.

---

## S1. Spec conflicts and refinements — RATIFY BEFORE TASKS

**RATIFIED 2026-10-10:** the user accepted C-1, C-2, C-3, C-4/C-5, C-6, C-8 and C-9 as written below, and the
registration-first task order. Recorded in `spec.md` → Clarifications → "Session 2026-10-10". The amended wording
governs; no alternative listed here is open.

The calibration model found two SC-004 clauses that are unsatisfiable by **any** recipe obeying the FRs (C-1,
C-2). Review then found a cap-taper law that failed SC-011(c) (fixed in the design; its knock-on is an SC-001
wording amendment, C-8) and an FR-042/SC-012 tension (C-9). Each item gives the proof and the amendment this plan
is built against.

**C-1 — SC-004 "Body span ≥ 6 dB at every grid point" is infeasible at 5 of the 27 points.** R_body = E_body/E_total
≤ 0 dB always, so no Body law can move R_body by more than its **headroom** = −R_body(body = 0). At depth 0,
body = edge = 0 the vector is the 1/n saw (shift is inert there, C-2), with E₁ = 1, E_body = Σ₂..₈ 1/n² = 0.5274,
E_pres = Σ₉..₆₄ 1/n² = 0.1020 → headroom 4.90 dB. The model finds headroom < 6 dB at exactly five points, all at
depth 0: edge 0 × shift {−1, 0, +1} (4.90 dB each), edge 0.5 / shift −1 (5.28 dB) and edge 1 / shift −1 (5.59 dB).
The other 22 points have headroom ≥ 6 dB (the four remaining depth-0 points have 18.95–40.6 dB), so the spec's bar
is feasible there and is kept.
*Amendment:* Body stays **strictly monotonic at all 27 points**. The **≥ 6 dB span applies at every grid point
whose headroom is ≥ 6 dB** (22 points; measured minimum 8.65 dB). At the five points with headroom < 6 dB, assert
**span ≥ 0.7 × headroom**. Measured minimum: 0.740 (depth 0, edge 1, shift −1: 4.14 dB of 5.59 dB); the other four
are 0.86, 0.93, 0.997 and 0.999. Both clauses are PASS/FAIL lines in `recipe-model.js` (S3.6). The previous
draft's "≥ 0.8 × headroom, measured ≥ 0.84" was wrong: the model's minimum is 0.740, so 0.8 fails at that point.
*Alternative for the user:* retune the Body law until the worst point reaches 0.8. Not attempted, because every Body
constant also feeds C-3's Lipschitz margin and SC-019.

**C-2 — SC-004 "Shift moves C monotonically by ≥ 1 octave at every grid point" is infeasible at 11 of 27
points.** (a) At body = edge = 0, Shift has nothing to move: FR-016 makes it move the Body/Edge *centres*, and
FR-018/FR-021 require body = edge = 0 to be the pure base law, which is how the anchors are reached. So C is
constant there (3 points). (b) At depth = 1, FR-010 caps the rest at ≤ −30 dB re h1, so
C ≤ log₂64 · 10⁻³ ≈ 0.006 octave whatever Shift does (9 points). The two sets overlap in one point (depth 1,
body = edge = 0), giving 3 + 9 − 1 = 11.
*Amendment:* the Shift gate runs over **depth ∈ {0, 0.5} × (body, edge) ∈ {0, 0.5, 1}² minus body = edge = 0**
(16 points). Measured: strictly monotonic everywhere, minimum span 1.43 octaves. At depth = 1, proof (b) removes
only the **span**, not strictness. The recipe test asserts C **strictly increasing** at the 8 depth-1 points with
body + edge > 0, and C constant within 1e-6 relative at body = edge = 0. The spans there are only 1.7e-8 to
3.2e-5 octave, but on float-rounded vectors C changes by ≥ 2.7e-3 relative per sweep step, far above float
resolution (model line "FR-016/C-2"). The render arm omits the depth-1 points: their n ≥ 2 content totals
≤ −77.6 dB re h1, too little for a float render to resolve a centroid that moves by 1e-8 octave.
*FR-016 amendment (proposed):* "The harmonic centroid C is strictly increasing in `shift` whenever body + edge > 0;
at body = edge = 0 Shift has no envelope to move and C is constant." Proof (a) shows that the unconditional wording
is false at body = edge = 0.
*Alternative for the user:* make Shift also tilt the base law. That breaks FR-016's literal wording, still cannot
fix (b), and is not recommended.

**C-3 — FR-018 interpretation: envelopes stay multiplicative, with a shift-dependent gain.** Body and Edge
multiply the slope (S3.2, `env`) as FR-018 requires. Two calibrated details follow:
(i) the envelope gain carries a compensation factor `2^(3·shift)`. Without it, moving an envelope up the steep
p = 2 slope *lowers* the centroid: the first model measured C falling with Shift at 13 grid points.
(ii) Body enters as `body²`. This strictly increasing reparameterisation leaves every monotonicity and endpoint
gate unchanged, and brings Body's Lipschitz constant from 10.2·√P₀ (over the FR-006 ceiling) to within it
(overall L_c = 7.13·√P₀). Neither is a spec change, but both are design choices the user should see.

**C-4 — FR-070 test-file list is extended**, following the established Vorago split. There are two extra TUs:
`profundum_core_spectral_test.cpp` (the 262 144-point renders) and `profundum_core_nonfinite_test.cpp` (the only
`-fno-fast-math` TU). There are also two test-support headers (S8.1).

**C-5 — Tags.** The perf case is tagged `[.perf]` (hidden, matched by `run-cpu-tests.js:56`, the Vorago
convention) rather than `[performance]`. The listening-render case is `[.listen]` (hidden, writes WAVs) rather than
`[long]`, so the CPU runner does not render WAVs.

**C-6 — Body-colour coordinates are search-calibrated, not hand-voiced.** The five `kBody*` points in S3.5 meet
SC-019 with ≥ 1.27× margin on every bar. Some are musically surprising: `kBodyRound` has body = 0, and
`kBodyHollow` has edge 0.9. If listening (Phase 13) moves them, `recipe-model.js` must still pass.

**C-7 — OQ-3 decision is executed in the build stage, not this plan.** No CPU figure exists yet. S9.3 fixes the
rule; the comply stage records the figures and the outcome in this plan's S9.3 table.

**C-8 — The cap taper (FR-032) is dB-linear and 1 430 cents wide so that SC-011(c) holds; SC-001's measurability
condition is amended to match.** The first draft's raised cosine over 0.5 octave has a dB slope of
0.0455·tan(πx/2) dB per cent, which is unbounded as the factor goes to 0. The model measured a 2.70 dB change
between adjacent 1-cent vectors (48 kHz, 12 231 cents, h2 at −78.3 dB re h1) against SC-011(c)'s 0.1 dB. A taper
meets the 0.1 dB clause only if its slope stays ≤ 0.1 dB/cent until the harmonic is below −80 dB re h1. The highest
pre-mask level of any n ≥ 2 harmonic re h1, over the whole control space, is **+44.28 dB** (depth 0, body 1,
curvature 1, emphasis 0, edge 1, shift 1, n = 6). Every factor of gₙ is monotone in each control, so this corner is
the maximum. The dB-linear part must therefore be ≥ 124.3 dB deep. The law (S3.3) is **0.09 dB/cent** (10 % under
the bar) over 1 420 cents (127.8 dB), then a 10-cent linear-amplitude tail to exactly 0 at capHz. The model
measured it over 14 states (the 6 named coordinates, the 5 colours, mid grid and the +44.28 dB corner), N = 96, MIDI
0–127 in 1-cent steps at 3 rates: worst **0.0900 dB**. L_f falls from 5.24 to **4.78·√P0/oct**, because the
dB-linear top is gentler in amplitude than the raised cosine.
Consequences, for ratification:
(i) The taper starts at capHz / 2.284: 3 861, 7 723, 8 406, 16 811 and 33 623 Hz at 22.05, 44.1, 48, 96 and
192 kHz. At 48 kHz with N = 64, SC-008 still has nothing in the taper at C1–C3 (h64 at C3 = 8 372 Hz, 7 cents below
the start). At 44.1 kHz (SC-017's re-assertion) and at N = 96, harmonics above the start are excluded, as SC-008's
own wording ("nothing in the cap taper") allows.
(ii) **SC-001 amendment (proposed).** At 44.1 kHz no note in [C3, C4) has h64 below the taper start (that needs
f0 < 120.6 Hz), so "each of 16, 32, 64 is measured at ≥ 1 note in every octave C1–C4" would fail. Proposed wording:
a candidate is measured iff n·f0 < capHz **and its delivered (post-mask) gain is ≥ −60 dB re h1**. This measures a
superset of the old set, so the accuracy gate gets stronger, and the coverage clause holds: at 44.1 kHz,
kSawAnchor's h64 is −48.7 dB re h1 at C3 and −57.7 dB at C♯3.
(iii) Rejected alternative: amend SC-011(c) so that taper harmonics are bound only by L_f/1200. That relaxes a
threshold which the roadmap does not show to be wrong.

**C-9 — FR-042's grid origin also restarts at a `Reset` (or first) `noteOn`.** FR-042 counts the control
interval "from a global sample counter since `prepare`/`reset` rather than from the block start". The plan also
zeroes that counter (`intervalPhase_`) on every `Reset` `noteOn` and on the first note (S4.7), because SC-012
requires it. Two `Reset` renders after `noteOn` from different prior states, *including different elapsed time*,
must be bit-identical. If the counter ran from `prepare`, the note would start at a different grid phase in each
render, the first control and pitch updates would land on different samples, and the renders would differ.
`FreeRunning` noteOns leave the grid alone, because SC-012's FreeRunning bit-identity with an uninterrupted render
depends on that. SC-020's block-size independence is unaffected, since the origin never depends on a block boundary.
*FR-042 amendment (proposed):* "… counted from a sample counter that starts at `prepare`/`reset` and restarts at
every `Reset`-policy (or first) `noteOn`, never from the block start …".

---

## S2. Architecture

```
setControls ─┐     (latched at control-interval start, FR-042)
             ▼
   SpectralShapeRecipe::evaluateShape  ──► targetShape_  (P0-normalised, f0-free)
                                             │ slerp step, chord ≤ Δmax  (FR-050)
                                             ▼
                                          shape_  ──► shapeGains()
f0 (setFrequency / f0PerSample, decimated U=16) ─┐
                                             ▼   ▼
   SpectralShapeRecipe::evaluateMask(f0) ──► mask_ ──► applyMask (× and renormalise) ──► delivered_ ──► deliveredGains()
                                                                                          │
                     frame_.partials[n].amplitude ◄──────────────────────────────────────┘
                     detune multipliers (exact ε, S4.6) ──► bank_.applyExternalFrequencyMultipliers
                     bank_.loadFrame(frame_, baseF0_, true)            (every control interval, only when changed)
                     bank_.processStereoBlock(L, R, run)               (runs of ≤ 16 samples)
```

Cadences (fixed constants, all sample rates):

- **Control interval** `kControlInterval = 64` samples: latch, slew step, mask, `loadFrame`. The simulation in
  S9.4 gives control-rate AM sidebands of −69.0 dB worst case (Shift) at I = 64, against SC-010(b)'s −60 dB.
- **Pitch update** `kPitchUpdateInterval = 16` samples (U, FR-043): exact-ε multiplier update. 16 divides 64, so
  every control update is also a pitch update.
- Both grids count from an `intervalPhase_` that is zeroed by `prepare`, `reset` and a `Reset` (or first)
  `noteOn`, never by block boundaries (SC-020(b)). The `noteOn` restart is C-9.

---

## S3. `SpectralShapeRecipe` (Layer 2, `processors/spectral_shape_recipe.h`)

### S3.1 Header, includes, public API

```cpp
#pragma once
#include <krate/dsp/core/db_utils.h>        // detail::isFinite (L0)
#include <krate/dsp/processors/harmonic_types.h>  // kMaxPartials (same layer, no deps)
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <span>

namespace Krate::DSP {

class SpectralShapeRecipe {
public:
    /// FR-002: exactly six fields. Plain aggregate (Phase 8 interpolates it).
    struct Controls {
        float depth = 0.5f;          // [0, 1]
        float body = 0.5f;           // [0, 1]
        float bodyCurvature = 0.5f;  // [0, 1]
        float bodyEmphasis = 0.0f;   // [-1, 0]
        float edge = 0.5f;           // [0, 1]
        float shift = 0.0f;          // [-1, +1]
    };

    // ---- normalisation (FR-005, FR-049) ----
    static constexpr float kCoreOutputRmsDb = -12.0f;
    static constexpr float kCenterPanGain = 0.7071067811865476f;   // == bank reset() literal
    static constexpr float kPowerTarget = 0.25238293779207720f;     // P0 = 4·10^(-1.2)
    // ---- base law (FR-018) ----
    static constexpr float kDepthTriangle = 0.5f;                   // p(0.5) == 2 exactly
    static constexpr float kSineResidual = 1.0e-3f;                 // R(1)
    // ---- envelopes (FR-011..FR-016) ----
    static constexpr float kBodyCentreOct = 1.75f;
    static constexpr float kEdgeCentreOct = 3.80f;
    static constexpr float kShiftCentreOct = 0.75f;
    static constexpr float kShiftGainOct = 3.0f;
    static constexpr float kBodyWidthBroadOct = 0.45f;
    static constexpr float kBodyWidthNarrowOct = 0.15f;
    static constexpr float kBodyWidthRefOct = 0.60f;
    static constexpr float kBodyPeakExponent = 1.5f;
    static constexpr float kEdgeFlankOct = 0.35f;
    static constexpr float kBodyGain = 18.0f;
    static constexpr float kEdgeGain = 50.0f;
    // ---- mask (FR-030, FR-032) ----
    static constexpr float kLowNoteGuardOnsetHz = 32.70f;           // C1
    static constexpr float kGuardSlopePerOctave = 0.5f;
    static constexpr float kNyquistCapFraction = 0.8f;              // == bank kAntiAliasFadeStart
    static constexpr float kCapTaperCents = 1430.0f;                // taper width below capHz (C-8)
    static constexpr float kCapTailCents = 10.0f;                   // final linear-amplitude segment to 0
    static constexpr float kCapTaperDbPerCent = 0.09f;              // dB-linear slope above the tail (SC-011(c) bar 0.1)
    // ---- documented Lipschitz ceilings (FR-006), in units of sqrt(P0) ----
    static constexpr float kLipschitzControlCeiling = 8.0f;         // measured 7.13
    static constexpr float kLipschitzOctaveCeiling = 8.0f;          // measured 4.78

    // Named coordinates (FR-014, FR-020) -- declared here, defined constexpr after the class (S3.5).
    static const Controls kDefaultControls;
    static const Controls kSineAnchor, kTriangleAnchor, kSawAnchor;
    static const Controls kHeavy, kHollow, kGrowl;
    static const Controls kBodyRound, kBodyHollow, kBodyWoody, kBodyNasal, kBodyThick;

    /// Clamp to ranges; any non-finite field -> kDefaultControls' field (FR-002).
    [[nodiscard]] static Controls sanitize(const Controls& c) noexcept;
    /// p(depth) = 1 + 2*depth (FR-018).
    [[nodiscard]] static constexpr float baseExponent(float depth) noexcept { return 1.0f + 2.0f * depth; }
    /// kNyquistCapFraction * fs / 2 -- also ProfundumCore's f0 clamp ceiling (FR-062).
    [[nodiscard]] static float capFrequency(double sampleRate) noexcept;

    /// Stage 1 (FR-051): P0-normalised shape, no guard, no cap, f0-independent. out.size() = N in [1, 96].
    static void evaluateShape(const Controls& c, std::span<float> out) noexcept;
    /// Stage 2 factors in [0, 1]: guard taper x cap taper; h1 factor is always 1 (FR-030, FR-032).
    static void evaluateMask(float f0Hz, double sampleRate, std::span<float> out) noexcept;
    /// out[n] = shape[n] * mask[n], renormalised to P0. out may alias shape.
    static void applyMask(std::span<const float> shape, std::span<const float> mask,
                          std::span<float> out) noexcept;
    /// FR-001 full evaluation, literally evaluateShape -> evaluateMask -> applyMask. The mask
    /// lives in a stack std::array<float, kMaxPartials> (no allocation).
    static void evaluate(const Controls& c, float f0Hz, double sampleRate,
                         std::span<float> out) noexcept;
};

}  // namespace Krate::DSP
```

`evaluate` is *defined* as the composition, so SC-021(a)'s 1e-6 composition check holds by construction
(it is still tested). Every function is `static`, `noexcept`, allocation-free and touches no global state
(FR-001, FR-004). Out-of-range `N` (`out.size()` 0 or > 96) → write nothing / first 96 only.

**Portability trap avoided — nested-aggregate NSDMIs.** `static constexpr Controls kX{...};` *inside*
`SpectralShapeRecipe` uses `Controls`' default member initialisers before the enclosing class is complete. Clang
and GCC reject this ("default member initializer needed within definition of enclosing class"); MSVC accepts it.
So the constants are declared `static const Controls kX;` in-class and defined after the class as
`inline constexpr SpectralShapeRecipe::Controls SpectralShapeRecipe::kX{.depth = …, …};`. A `static const`
declaration with a `constexpr` out-of-class definition is valid C++17/20 and is usable in constant expressions
after the definition. Designated initialisers in declaration order mean no narrowing.

### S3.2 Stage 1 — the shape law (exact formulas)

Sanitise first (S3.1). Let u = log₂ n, computed as `std::log2(static_cast<float>(n))`. Per-harmonic
transcendentals are `float` (`std::exp2`, `std::exp`, `std::log2`); the power sum Σg² is accumulated in `double`.

```
p        = 1 + 2·depth                                       (FR-018; p(0)=1 saw, p(0.5)=2 triangle, p(1)=3)
R        = 1 − (1 − kSineResidual) · S((depth − 0.5)/0.5),   S(x) = x²(3 − 2x) on [0,1], clamped
b        = body²                                             (C-3 ii)
u_B      = kBodyCentreOct + kShiftCentreOct·shift            (Body centre, octaves of n)
u_E      = kEdgeCentreOct + kShiftCentreOct·shift            (Edge shelf corner)
σ_B      = kBodyWidthBroadOct + (kBodyWidthNarrowOct − kBodyWidthBroadOct)·bodyCurvature
comp     = exp2(kShiftGainOct·shift)                         (C-3 i)
peak     = (kBodyWidthRefOct / σ_B)^kBodyPeakExponent         (narrow = peaked, FR-012)
G_B(n)   = exp(−(u − u_B)² / (2σ_B²))                        (Gaussian in log n, FR-011)
G_E(n)   = 1                         if u ≥ u_E              (half-Gaussian shelf: C¹, raises everything above
         = exp(−(u − u_E)²/(2·kEdgeFlankOct²))  otherwise     the corner, FR-015 "raising the high partials")
env(n)   = 1 + comp · (kBodyGain·b·peak·G_B(n) + kEdgeGain·edge·G_E(n))
par(n)   = (n even) ? (1 + bodyEmphasis) : 1                 (whole-vector odd bias, FR-013 / Q5)

g₁ = 1                                                       (h1 floor > 0, FR-005)
gₙ = R · exp2(−p·u) · env(n) · par(n)          n = 2..N
aₙ = gₙ · sqrt(P0 / Σ gₖ²)                                    (FR-005, Σ in double)
```

Properties the formulas give by construction (each is still tested, S8):

- **Anchors are analytic (FR-018/021, SC-006):** body = edge = 0 ⇒ env ≡ 1. Depth 0 gives aₙ ∝ 1/n.
  `p(0.5) == 2.0f` exactly in float (1 + 2·0.5), with R(0.5) = 1, so emphasis −1 gives an odd-only 1/n². Depth 1
  gives R = 10⁻³ and p = 3: rest −77.6 dB re h1 (model).
- **Depth strictly increases h1/rest (FR-010):** for n ≥ 2, gₙ is strictly decreasing in depth, because p rises
  and R is non-increasing, while env and par do not depend on depth. g₁ is fixed.
- **Odd/even floor (FR-023):** par ≤ 1 on evens only, so odd/even ≥ the neutral value. At emphasis −1 every even
  gain is exactly 0.0f (SC-022 trivially ≤ −50 dB).
- **Edge = 0 adds no Edge envelope (FR-015)**, and body = 0 adds no Body envelope.
- **No hard boundary in n (FR-017):** every factor is smooth in u. The shelf is C¹ at u_E.
- **Depth 1 at any B/E/S:** rest ≤ −43.8 dB re h1, worst case over the 81-point B/E/S/curvature grid (FR-010
  needs ≤ −30).

### S3.3 Stage 2 — the mask (guard × cap), and `applyMask`

```
octBelow  = max(0, log2(kLowNoteGuardOnsetHz / f0))           (0 at and above C1 → identity, FR-030/031)
guard(n)  = exp2(−kGuardSlopePerOctave · octBelow · u)         (h1: u=0 → 1; non-decreasing attenuation in n)
capHz     = kNyquistCapFraction · fs/2
y(n)      = 1200 · log2(capHz / (n·f0))                        (cents below the cap; n ≥ 2 only)
D         = kCapTaperDbPerCent · (kCapTaperCents − kCapTailCents)          (= 127.8 dB)
cap(n)    = 1                                              y ≥ kCapTaperCents
          = 10^(−kCapTaperDbPerCent · (kCapTaperCents − y) / 20)   kCapTailCents < y < kCapTaperCents  (dB-linear)
          = 10^(−D/20) · y / kCapTailCents                 0 < y ≤ kCapTailCents   (linear tail, continuous at the join)
          = 0                                              y ≤ 0
cap(1)    = 1                                                  (h1 exempt, FR-032)
mask(n)   = guard(n) · cap(n)
applyMask: oₙ = shapeₙ·maskₙ;  oₙ ← oₙ · sqrt(P0 / Σ o²)        (Σ ≥ shape₁² > 0: shape₁ > 0 always)
```

Cost control: compute `guard` only if `octBelow > 0`. Compute `cap` only for n with
n·f0 > capHz·2^(−kCapTaperCents/1200); below that it is exactly 1.

- **Exact cap (SC-021(c)):** n·f0 ≥ capHz ⇒ y ≤ 0 ⇒ factor exactly `0.0f`.
- **Clamp ceiling (FR-032/062):** f0 = capHz ⇒ every n ≥ 2 has y ≤ −1200 ⇒ h1-only, with a₁² = P0 after
  renormalisation.
- **h1 share never reduced by the guard (FR-030):** guard(1) = 1 and guard(n ≥ 2) ≤ 1, then renormalise.
- **Continuity:** both factors are C⁰ in f0 (the cap is continuous at both joins; the guard has a kink but no step
  at the onset). Model: L_f = 4.78·√P₀ per octave over 1-cent steps from 8 Hz to the clamp ceiling at
  44.1/48/96 kHz (random states), and 4.79·√P₀ over the SC-011(c) sweep.
- **SC-011(c) dB clause (C-8):** in the dB-linear part a harmonic's level re h1 moves by exactly
  kCapTaperDbPerCent per cent of f0. Renormalisation moves every harmonic by at most the same amount in the
  opposite direction, so no harmonic moves by more than 0.09 dB per cent. The guard and the taper never overlap:
  for f0 ≤ 32.7 Hz and N ≤ 96, n·f0 ≤ 3 139 Hz, below the 3 861 Hz taper start even at 22.05 kHz. The dB-linear
  part ends 127.8 dB down, which is below −80 dB re h1 for any harmonic whose pre-mask level is < +47.8 dB re h1;
  the maximum over the control space is +44.28 dB (C-8). In the tail the harmonic is therefore below −80 dB re h1
  and is bound only by |Δa| ≤ L_f/1200, which it meets with a factor of 1 000 to spare. Model: worst 0.0900 dB.
- **Gates:** SC-011(a) reduction one octave below onset is 10.45 dB (saw) and 8.56 dB (mid grid), against a
  ≥ 6 dB bar.
- **Taper headroom (C-8 (i)):** at 48 kHz the taper occupies 8 406–19 200 Hz. With N = 64 at C3 (130.81 Hz) h64
  is 8 372 Hz, so SC-008's "nothing in the taper" holds at C1–C3 for N = 64 at 48 kHz, with 7 cents to spare.
  At 44.1 kHz (start 7 723 Hz; SC-017's re-assertion) h60–h64 at C3 are in the taper, and with **N = 96** every
  harmonic above the start is in it at both rates (h96 at C3 = 12 558 Hz). SC-008 then excludes the taper
  harmonics, as its own wording allows. Recorded for the S9.3 decision.

### S3.4 Why these constants (justification, not tuning folklore)

- `p = 1 + 2·depth`: the simplest law with p(0) = 1 and p(kDepthTriangle) = 2 exact in float, monotone, Lipschitz.
- `R` smoothstep over [0.5, 1] in amplitude: C¹ at 0.5 (R′ = 0 there), so the triangle anchor is untouched. It is
  linear-amplitude rather than dB-linear because a dB-linear residual gave L_c ≈ 14·√P₀ (the dB-form envelope
  trial measured L ≈ 31·√P₀).
- Half-Gaussian Edge shelf rather than a full Gaussian: "raising the high-order partials" (roadmap line 210)
  means everything above the corner rises, which is the buzzy end. A full bump would leave n ≥ 45 untouched.
- `kNyquistCapFraction = 0.8 = kAntiAliasFadeStart`: the recipe, not the bank's fade, defines the spectrum
  (FR-032). E-4's "MIDI 127 at 44.1 kHz is not clamped" uses 0.8.
- dB-linear cap taper at 0.09 dB/cent: SC-011(c) bounds the dB slope (0.1 dB/cent) down to −80 dB re h1, and a
  harmonic can start +44.28 dB above h1, so the taper must be ≥ 124.3 dB deep at ≤ 0.1 dB/cent (C-8). 1 430 cents
  is the narrowest width that gives 0.09 dB/cent (10 % margin) plus a 3.5 dB depth margin and a 10-cent tail, while
  keeping h64 at C3 / 48 kHz out of the taper. Any raised-cosine or polynomial taper has an unbounded dB slope at
  its end and fails SC-011(c).
- Guard `n^(−0.5·octBelow)`: at C0, h9 is −9.5 dB and h2 −3 dB. Upper density thins and the body survives.

### S3.5 Named coordinates (definitions after the class)

All values below are on a 0.05 grid (the model's search grid). Order: depth, body, bodyCurvature,
bodyEmphasis, edge, shift.

| Constant | depth | body | curv | emph | edge | shift | Gate it serves (model margin) |
|---|---|---|---|---|---|---|---|
| `kDefaultControls` | 0.5 | 0.5 | 0.5 | 0 | 0.5 | 0 | = mid grid; FR-002 non-finite default |
| `kSineAnchor` | 1 | 0 | 0.5 | 0 | 0 | 0 | SC-006 rest −77.6 dB |
| `kTriangleAnchor` | 0.5 | 0 | 0.5 | −1 | 0 | 0 | SC-006 exact odd 1/n² |
| `kSawAnchor` | 0 | 0 | 0.5 | 0 | 0 | 0 | SC-006 exact 1/n |
| `kHeavy` | 0.5 | 0.3 | 0.5 | 0 | 0 | −1 | SC-007 sub > body > pres, margin 7.5 dB |
| `kHollow` | 0.5 | 1.0 | 1.0 | 0 | 0 | 0.5 | SC-007 body > sub > pres, margin 25.7 dB |
| `kGrowl` | 0 | 0.5 | 0 | 0 | 1.0 | 1.0 | SC-007 pres > body > sub, margin 12.8 dB |
| `kBodyRound` | 0.2 | 0 | 0.1 | −0.8 | 0.1 | −0.4 | SC-019 lowest C, +0.375 oct |
| `kBodyHollow` | 0 | 0.25 | 0.6 | −1 | 0.9 | 0.85 | SC-019 highest odd/even, +18.9 dB |
| `kBodyWoody` | 0.65 | 0.5 | 0.95 | −0.1 | 0.3 | 0.05 | SC-019 distance |
| `kBodyNasal` | 0.6 | 0.7 | 0.95 | −0.2 | 0.3 | −0.95 | SC-019 smallest σ, +0.064 oct |
| `kBodyThick` | 0.5 | 0.9 | 0.05 | 0 | 0 | −0.05 | SC-019 highest R_body, +1.27 dB |

SC-019 pairwise Body-colour distance: minimum 16.24 dB (bar 6). See C-6.

### S3.6 Calibration record (`node specs/profundum-phase1-harmonic-core/recipe-model.js`, 2026-10-09)

```
PASS  SC-002 depth strictly monotone in h1/rest (27 grid points)
PASS  SC-004 Body strictly monotone (27 points)
PASS  SC-004 Body span >= 6 dB where headroom >= 6 dB (C-1, 22 points)  min 8.65 dB
PASS  SC-004 Body span >= 0.7 x headroom where headroom < 6 dB (C-1, 5 points)  min 0.740 x headroom
PASS  SC-004 Edge strictly monotone, span >= 6 dB (27 points)  min 8.89 dB
PASS  SC-004 Shift strictly monotone, span >= 1 oct (depth<1, body+edge>0; C-2)  min 1.427 oct
PASS  FR-016/C-2 depth 1: C strictly increasing (8 points, float vectors), constant at body=edge=0  min per-step dC/C 2.68e-3
PASS  SC-004 endpoint pairwise distance >= 6 dB  min 7.07 dB
PASS  SC-005 curvature: sigma strictly monotone, span >= 0.25 oct  span -0.375 oct
PASS  SC-005 emphasis: odd/even strictly decreasing, span >= 20 dB  34.8 (2nd point) -> 4.7 dB; first point +inf
PASS  FR-010 depth 1: rest <= -30 dB re h1 at any B/E/S  worst -43.8 dB
PASS  FR-018 depth 1, B=E=0: rest <= -60 dB  -77.6 dB
PASS  SC-007 kHeavy/kHollow/kGrowl ordering, margin >= 3 dB @ C1/C2/C3  min 7.5 / 25.7 / 12.8 dB   (9 lines, condensed)
PASS  SC-019 pairwise Body-colour distance >= 6 dB  min 16.24 dB
PASS  SC-019 extremal orderings (>= 0.05 oct / >= 1 dB)  round C 0.375 oct, hollow o/e 18.88 dB, nasal sigma 0.064 oct, thick Rbody 1.27 dB
PASS  SC-011(a) guard reduces R_pres >= 6 dB one octave below onset  10.45 dB (saw), 8.56 dB (mid grid)   (2 lines, condensed)
PASS  SC-011(c) 1-cent continuity: <= 0.1 dB where both >= -80 dB re h1 (14 states, N=96, 3 rates)  worst 0.0900 dB (max pre-mask level re h1 44.28 dB)
PASS  SC-011(c) 1-cent continuity: |da| <= L_f/1200 elsewhere  worst 0.001 x bound
PASS  FR-006 L_f <= 8 sqrt(P0)/oct over the SC-011(c) sweep  measured 4.79 sqrt(P0)/oct
PASS  FR-006 L_c <= 8 sqrt(P0) (60k samples, N=96)  measured 7.13 sqrt(P0)
PASS  FR-006 L_f <= 8 sqrt(P0)/oct (1-cent steps, 8 Hz..clamp, 3 rates)  measured 4.78 sqrt(P0)/oct

ALL RECIPE-SIDE GATES PASS
```

Wall clock 5.9 s (the SC-011(c) sweep is 14 states × 3 rates × 12 701 steps).

SC-005 curvature: σ *decreases* with curvature (narrower Body concentrates energy). FR-012 asks only for
"monotonically", so the test asserts strict monotonicity in the observed direction (decreasing).

**Thinnest margins**, to watch if float rounding moves them: L_c 7.13 vs 8 (11 %), SC-011(c) 0.0900 vs 0.1 dB (10 %,
set by kCapTaperDbPerCent), C-1 ratio 0.740 vs 0.7, SC-019 nasal σ 0.064 vs 0.05 oct, thick R_body 1.27 vs 1 dB.

---

## S4. `ProfundumCore` (Layer 3, `systems/profundum_core.h`)

### S4.1 Includes and public API

```cpp
#pragma once
#include <krate/dsp/core/db_utils.h>                         // detail::isFinite (L0)
#include <krate/dsp/core/math_constants.h>                   // kPi (L0)
#include <krate/dsp/processors/harmonic_oscillator_bank.h>   // L2
#include <krate/dsp/processors/harmonic_types.h>             // L2
#include <krate/dsp/processors/spectral_shape_recipe.h>      // L2
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace Krate::DSP {

/// @note Precondition: the owner runs processBlock with FTZ/DAZ on (ScopedDenormalMode on x86, the
///       default FPCR on AArch64). Without it, zero-target bank lanes stall as denormals (S7).
class ProfundumCore {
public:
    enum class RetriggerPhase : std::uint8_t { Reset, FreeRunning };   // FR-046

    static constexpr int kDefaultPartials = 64;                 // FR-041 (OQ-3, S9.3)
    static constexpr std::size_t kControlInterval = 64;         // FR-042
    static constexpr std::size_t kPitchUpdateInterval = 16;     // FR-043 cadence U
    static constexpr float kMinF0Hz = 8.0f;                     // FR-062
    static constexpr float kDefaultF0Hz = 55.0f;                // A1, used until the first valid f0
    static constexpr float kMaxGainSlewPerSecOverSqrtP0 = 375.0f; // FR-050 (Q4)
    static constexpr float kSlewChordMargin = 0.999f;           // step chord = 0.999·Δmax (S4.5)
    static constexpr float kMaxBaseStepSemitones = 0.9f;        // < bank's 1-semitone crossfade threshold
    static constexpr double kMinSampleRate = 22050.0;
    static constexpr double kMaxSampleRate = 192000.0;

    ProfundumCore() noexcept = default;
    ProfundumCore(const ProfundumCore&) = delete;               // owns the non-copyable bank
    ProfundumCore& operator=(const ProfundumCore&) = delete;
    ProfundumCore(ProfundumCore&&) noexcept = default;
    ProfundumCore& operator=(ProfundumCore&&) noexcept = default;

    // control thread
    /// @note NOT real-time safe: bank_.prepare (harmonic_oscillator_bank.h:128) runs the bank's
    ///       noise-calibration loop (:158-182).
    void prepare(double sampleRate, int numPartials = kDefaultPartials) noexcept;
    /// @note Control thread; allocation-free and bounded (array fills + one evaluateShape), S4.3.
    ///       The RT-safe way to clear a sounding core is a Reset-policy noteOn.
    void reset() noexcept;
    // audio thread (all noexcept, allocation-free; no-ops before prepare, FR-061)
    void setControls(const SpectralShapeRecipe::Controls& controls) noexcept;
    void setFrequency(float f0Hz) noexcept;
    void setRetriggerPhase(RetriggerPhase policy) noexcept;
    void noteOn(float f0Hz) noexcept;                            // effective at offset 0 of the next block
    void setPartialPanOffsets(const std::array<float, kMaxPartials>& offsets) noexcept;
    void processBlock(float* left, float* right, std::size_t numSamples,
                      const float* f0PerSample = nullptr) noexcept;

    // read-only observers (tests; Phase 3/7/9 diagnostics)
    [[nodiscard]] std::span<const float> shapeGains() const noexcept;      // FR-050, size numPartials
    [[nodiscard]] std::span<const float> deliveredGains() const noexcept;  // FR-051, size numPartials
    [[nodiscard]] float maxShapeStepPerInterval() const noexcept;          // Δmax, S4.5
    [[nodiscard]] float currentFrequency() const noexcept;                 // f0 of the last pitch update
    [[nodiscard]] float maskFrequency() const noexcept;                    // f0 of the last control update (maskF0_)
    [[nodiscard]] float baseFrequency() const noexcept;                    // bank targetPitch (baseF0_), S4.6
    [[nodiscard]] int numPartials() const noexcept;
    [[nodiscard]] bool isPrepared() const noexcept;
    [[nodiscard]] bool stateFinite() const noexcept;                       // bank_.stateFinite() && own arrays finite

private:
    void controlUpdate(float f0) noexcept;        // S4.4 step order
    void pitchUpdate(float f0) noexcept;          // S4.6
    void applyNoteOn() noexcept;                  // S4.7
    void slewShapeTowardTarget() noexcept;        // S4.5
    void computeEpsilonTargets(float f0) noexcept;
    void deliverToBank(bool baseChanged) noexcept;
    [[nodiscard]] float sanitizeF0(float f0, float previous) const noexcept;
    // members: S4.2
};

}  // namespace Krate::DSP
```

### S4.2 State layout (all fixed-size members; about 22 KB per instance)

| Member | Type | Meaning |
|---|---|---|
| `bank_` | `HarmonicOscillatorBank` | the generator |
| `frame_` | `HarmonicFrame` | n = 1..N: `harmonicIndex = n`, `relativeFrequency = n`, `inharmonicDeviation = 0`, `bandwidth = 0`, `phase = 0`, written once in `prepare` (FR-044/045); only `.amplitude` and `.frequency` change afterwards |
| `pendingControls_`, `latchedControls_` | `Controls` | setter target / interval-latched value |
| `controlsDirty_`, `shapeConverged_` | `bool` | FR-042 skip rule |
| `targetShape_`, `shape_`, `mask_`, `delivered_` | `std::array<float, kMaxPartials>` | stage vectors (first N used) |
| `epsBase_` | `std::array<double, kMaxPartials>` | estimate of the bank's ε at `baseF0_` (S4.6) |
| `detuneShadow_`, `multiplierScratch_` | `std::array<float, kMaxPartials>` | bit-exact mirror of the bank's `detuneMultiplier_`; per-update multipliers |
| `heldF0_`, `currentF0_`, `baseF0_`, `maskF0_`, `capHz_` | `float` | scalar target; last pitch-update f0; bank `targetPitch_`; f0 the mask was built at; cap |
| `panOffsets_` | `std::array<float, kMaxPartials>` | FR-048 vector (sanitised) |
| `panNonZero_`, `panDirty_`, `panAppliedNonZero_` | `bool` | FR-048 state machine |
| `policy_` | `RetriggerPhase` | default `Reset` |
| `noteOnPending_`, `noteOnF0_`, `sounding_` | `bool`, `float`, `bool` | FR-046; `sounding_` false until the first note |
| `intervalPhase_` | `std::uint32_t` | 0..63, the grid (S2) |
| `sampleRate_`, `maxStepPerInterval_` | `double`, `float` | Δmax = 375·√P0·64/fs |
| `numPartials_`, `prepared_` | `int`, `bool` | |

### S4.3 `prepare` / `reset` contract

`prepare(sr, N)`:
1. Clamp sr to [22 050, 192 000] and N to [1, 96] (FR-041; `prepare(fs, 128)` ≡ 96, SC-016).
2. `bank_.prepare(sr)` (which resets the bank).
3. Fill `frame_` for n = 1..N as in S4.2 and set `frame_.numPartials = N` (E-9: never shrinks; capped
   partials get amplitude 0).
4. Set `capHz_ = SpectralShapeRecipe::capFrequency(sr)` and `maxStepPerInterval_ = 375·√P0·64/sr`
   (0.25119 at 48 kHz).
5. Set `pendingControls_ = latchedControls_ = kDefaultControls`, evaluate `targetShape_`, copy it to `shape_`
   (converged), and set `heldF0_ = currentF0_ = baseF0_ = kDefaultF0Hz`.
6. `detuneShadow_.fill(1.0f)`; `panOffsets_` keeps its value, with `panDirty_ = panNonZero_`;
   `intervalPhase_ = 0`; `sounding_ = noteOnPending_ = false`; `prepared_ = true`.

`reset()` calls `bank_.reset()` (`:202–240`, `@note Real-time safe`), **not** `bank_.prepare(sr)`, then re-runs
steps 3–6. sr and N are unchanged, so the bank's coefficients, crossfade length and noise calibration from the last
`prepare` stay valid, `frame_` refill is idempotent, and capHz_ and Δmax are unchanged. Step 5 keeps
`pendingControls_` and latches it instead of resetting it to the defaults. The core is silent until the next
`noteOn`.

RT classification (header `@note`s, S4.1): `prepare` is **NOT real-time safe**, because `bank_.prepare` is
annotated so (`:128`) and runs a 1 024 + 32 768-iteration LCG→biquad noise-calibration loop (`:158–182`) that
Profundum never needs (bandwidth is always 0) but cannot skip without a bank change. `reset` is a control-thread
call (FR-041) whose cost is bounded: array fills plus one `evaluateShape`. The only RT-safe clearing path is a
`Reset` `noteOn` (S4.7), which Phase 4/9 voice steal and recovery should reuse.

### S4.4 `processBlock` and `controlUpdate` — normative order

```
processBlock(L, R, n, f0PerSample):
  if (L == nullptr || R == nullptr || n == 0) return;                       // E-2 (no state change)
  if (!prepared_) { zero-fill L, R; return; }                               // FR-061
  if (noteOnPending_) applyNoteOn();                                         // S4.7, at offset 0
  i = 0
  while (i < n):
     if (intervalPhase_ % U == 0):
        f = f0PerSample ? sanitizeF0(f0PerSample[i], currentF0_) : heldF0_   // decimation: read every U-th sample
        (intervalPhase_ == 0) ? controlUpdate(f) : pitchUpdate(f)
     run = min(n − i, U − intervalPhase_ % U)
     bank_.processStereoBlock(L + i, R + i, run)                             // silent until sounding_ (bank frameLoaded_ false)
     i += run;  intervalPhase_ = (intervalPhase_ + run) % kControlInterval
  if (f0PerSample) heldF0_ = sanitizeF0(f0PerSample[n − 1], currentF0_)     // FR-043 "scalar becomes its last value"
```

```
controlUpdate(f):
  1. Latch: if controlsDirty_ { latchedControls_ = pendingControls_; evaluateShape → targetShape_;
     shapeConverged_ = (targetShape_ == shape_ bitwise); controlsDirty_ = false; }
  2. Slew: if !shapeConverged_ → slewShapeTowardTarget()                    (S4.5)
  3. Pitch: currentF0_ = f; baseNew = clamp(f, baseF0_·2^(−0.9/12), baseF0_·2^(+0.9/12))
     (first sounding update / noteOn: baseNew = f, no clamp)
  4. Mask: if (f != maskF0_ bitwise) { evaluateMask(f) → mask_; maskF0_ = f; maskChanged = true }
  5. Deliver: if (shape changed in 2 || maskChanged) applyMask(shape_, mask_) → delivered_;
     flush |deliveredₙ| < 1e-12f to 0.0f (target hygiene only; it does not prevent denormal stalls, S7)
  6. If sounding_ and (delivered_ changed || baseNew != baseF0_):
       computeEpsilonTargets(f) relative to baseNew  → multipliers m = dTarget / detuneShadow_   (S4.6)
       bank_.applyExternalFrequencyMultipliers(m); detuneShadow_[i] = detuneShadow_[i] * m[i]
       frame_.partials[n−1].amplitude = delivered_[n−1]; .frequency = n·baseNew
       bank_.loadFrame(frame_, baseNew, /*skipNormalization=*/true); baseF0_ = baseNew
     else if sounding_ and f changed: pitchUpdate-style multiplier refresh only (S4.6)
  7. Pan (FR-048): if panDirty_ {
       if panNonZero_ { bank_.applyPanOffsets(panOffsets_); panAppliedNonZero_ = true; }
       else if panAppliedNonZero_ { bank_.restoreCenterPan(); panAppliedNonZero_ = false; }
       panDirty_ = false; }
```

**Skip rule (FR-042).** In steady state (no dirty controls, converged slew, f bit-unchanged, pan clean), steps 1–7
do no work beyond comparisons, and `loadFrame` is not called. This is what makes the static CPU cost the bank's
alone.

The multipliers are applied **before** `loadFrame` on purpose. `loadFrame` recomputes the AA gains from
`n·targetPitch_·detuneMultiplier_` (bank `:1072`), so it then sees the new multipliers. No sample is rendered
between the two calls.

**FR-048(b) wording:** a non-zero vector is forwarded at the first control interval after it changes, and again
after any bank `reset()`. Re-forwarding an unchanged vector would be idempotent: `panPosition_` stays 0 because
the core never calls `setStereoSpread`. So this is output-identical to forwarding every interval and saves
96 sin/cos per interval.

### S4.5 Shape-vector step ceiling — spherical step on the P0 sphere (FR-050)

Every shape vector satisfies ‖s‖² = P0, so the core moves `shape_` toward `targetShape_` along the great circle,
in `double`:

```
Δmax   = kMaxGainSlewPerSecOverSqrtP0 · √P0 · kControlInterval / fs          (0.5·√P0 at 48 kHz)
chord  = ‖t − s‖₂
if chord ≤ Δmax:            s ← t;  shapeConverged_ = true
else:
   Ω  = acos(clamp(⟨s,t⟩ / P0, 0, 1))                      (both ≥ 0 ⇒ Ω ∈ [0, π/2])
   φ  = 2·asin(kSlewChordMargin · Δmax / (2√P0))           (chord of the step = 0.999·Δmax)
   s  ← (sin(Ω − φ)·s + sin(φ)·t) / sin(Ω);  renormalise to P0
```

Guarantees: the step's L2 chord is 0.999·Δmax ≥ the max-element change, so SC-010(d) holds with 0.1 % margin
for the renormalisation rounding. The norm is preserved. Non-negativity is preserved because both coefficients
are ≥ 0, so s₁ > 0 and the mask renormalisation never divides by zero. Ω is never tiny on this branch: chord > Δmax
⇒ Ω > φ ≥ 2·asin(0.0625) at 192 kHz.

Full-scale moves take ≥ 2 intervals (≈ 2.7 ms at 48 kHz). Orthogonal states take ≈ 2.83 intervals (√2·√P0 /
0.5√P0), so 3.8 ms.

**`Reset` `noteOn` exemption:** `shape_ = targetShape_` directly (FR-050, S4.7).

### S4.6 Pitch path — exact ε through the existing multiplier API (FR-043, FR-044)

**Mechanism decision (FR-043):** a per-U multiplier via `applyExternalFrequencyMultipliers` (`:663`), not
sub-block `setTargetPitch`.
- `setTargetPitch` costs 2 trig per partial per update (`:1046–1092`). At U = 16 that is 64 · 2 · 3 000/s ≈
  384 k trig/s, an estimated 0.4–0.6 % of a core on its own, against a 1 % total budget.
- A large glide through `setTargetPitch` also triggers the bank's crossfade whenever one update exceeds 1 semitone.
- The `setDetuneSpread(0)` cached-base restore (`:579–594`) is invalidated by every `loadFrame` (`:311`), so it
  would cost a 96·`pow` + AA recompute twice per interval.

The core therefore keeps a **bit-exact shadow** of `detuneMultiplier_` and drives it to an exact target. The
bank's MCF uses `ε_eff = ε_bank · detune` (`simd.cpp:103`). The target is that ε_eff equals the exact ε of the
desired frequency.

```
θ_f        = π·f/fs                      (double)
ε*_n       = 2·sin(n·θ_f)                via the Chebyshev recurrence in double:
             s₀ = 0, s₁ = sin θ, c2 = 2cos θ, s_{k+1} = c2·s_k − s_{k−1}    (1 sin + 1 cos per update)
             for n with n·f ≥ capHz: ε*_n = 2·sin(π·capHz/fs)               (freeze at the cap: no wrapped,
                                                                              aliased ε while a capped partial
                                                                              decays through the 2 ms one-pole)
ε_base_n   = clamp(2·sin(π·n·baseF0_/fs), ±1.99)   (double; recomputed only when baseF0_ changes;
                                                     mirrors the bank's recalculateFrequencies, :1046–1057)
dTarget_n  = |ε_base_n| > 1e-9 ? float(ε*_n / ε_base_n) : 1.0f
m_n        = dTarget_n / detuneShadow_n          (float)
bank_.applyExternalFrequencyMultipliers(m);   detuneShadow_n = detuneShadow_n * m_n   (same single IEEE multiply
                                                                                        → bit-identical mirror)
```

- `pitchUpdate(f)` (U-cadence, non-control samples): sanitise. If f is bit-equal to `currentF0_`, return.
  Otherwise set `currentF0_ = f`, run the recurrence, and apply the multipliers. The mask and base are untouched
  until the next control update; S10 R-6 gives the within-interval bound.
- Static accuracy: ε_eff = float(ε_bank·d), where ε_bank's float rounding is about 1e-7 relative to the double
  estimate. The frequency error is therefore about 1e-7 relative, roughly 2e-4 cent, against SC-001's 0.1 cent.
- **Why the shadow must be bit-exact:** if shadow and bank diverged by one ulp per update, the ratio would
  random-walk (about √(10⁷) · 6e-8 ≈ 2e-4 relative ≈ 0.3 cent after an hour of vibrato). A single `float * float`
  has no reassociation or contraction freedom, even under `/fp:fast`/`-ffast-math`, so the mirror is exact.
  Both reset to exactly 1.0f together (bank `reset()` `:228` and the core's `fill(1.0f)`).
  Plan-internal test `ProfundumCore_DetuneShadowNoDrift` (S8.3) guards it.
- **Base chase:** `loadFrame` triggers the 3 ms crossfade when `targetPitch` moves > 1 semitone since the last
  call (`:260–268`). `baseF0_` therefore moves at most 0.9 semitone per control interval and the multipliers cover
  the remainder, so no glide, drop or `setFrequency` jump ever crossfades (FR-043). The exception is a
  `noteOn` jump, where `baseF0_ = f` directly (E-7).
- **AA and orbit-correction gains are evaluated only at `loadFrame` (stated approximation).** The bank derives
  `antiAliasGain_` from `freq = computePartialFrequency(i) · detuneMultiplier_[i]` and
  `mcfCorrection = cos(π·freq/fs)` (`:1072–1078`), so it treats the multiplier as a *frequency* ratio. The SIMD
  kernel uses it as an *ε* ratio (`simd.cpp:103`). The two readings agree only when the multiplier is 1.
  (i) At every control update outside the R-4 regime (≤ 0.9 semitone of travel per interval, i.e. ≤ 675
  semitones/s at 48 kHz), step 3 sets baseNew = f, so dTarget_n = ε*_n/ε_base_n has both terms evaluated at the
  same f in double and rounds to 1.0f within an ulp. The multipliers are applied before `loadFrame`, so the AA
  and correction gains are computed at the true frequency. The exceptions are capped partials, whose ε is frozen
  at the cap; their target is 0, so any gain error scales a decaying tail.
  (ii) Between control updates, `pitchUpdate` moves the multiplier without `loadFrame`, so the gains are stale
  by the pitch travel within one interval. The relative amplitude error of a partial at frequency φ is about
  (πφ/fs)·tan(πφ/fs)·|Δln f|. At the SC-009 bend rate (100 semitones/s, |Δln f| = 0.0077 per interval at
  48 kHz) that is 0.02 dB at the taper start (φ = 0.175·fs) and 0.03 dB where the taper is 20 dB down. It reaches
  0.26 dB only at the cap, where the partial is ≥ 127.8 dB down. SC-010(b)'s vibrato (62.8 semitones/s peak) gives
  0.63× these figures. The bank's own linear fade never engages for an uncapped partial, because the cap equals the
  fade start.
  (iii) In the R-4 regime `baseF0_` lags f, the multiplier carries the remainder, and the frequency-versus-ε
  mismatch is present at `loadFrame` too; R-4 bounds it.
  The recipe's taper and mask are what keep (ii) and (iii) small: wherever the error is largest, the partial is
  already near zero. Gate: `ProfundumCore_NoCrossfadeOnGlide` asserts `baseFrequency() == maskFrequency()`
  bitwise after every block of the SC-009 bend and the SC-010(b) vibrato, which proves the premise of (i). The
  SC-009 bend and SC-010(b) renders measure the consequence.
- **Cadence:** U = 16 is fixed. SC-020(c) phase bound at 48 kHz for C3→C1: 0.5·(16/48000)·98.1 + 0.02 =
  0.0364 cycle. Pitch-update sidebands sit at k·3000 Hz ± n·f0. With SC-010(b)'s f0 = 140.625 Hz,
  3000/140.625 = 21⅓, so they also fall ≥ f0/3 from every harmonic: SC-010(b)'s f0 needs no re-derivation.

### S4.7 `noteOn` and the retrigger policy (FR-045, FR-046, FR-051(d))

`noteOn(f)` stores `noteOnF0_ = sanitizeF0(f, heldF0_)` and sets `noteOnPending_`; the last call before a block
wins. `setFrequency(f)` sets `heldF0_ = sanitizeF0(f, heldF0_)`.

`applyNoteOn()` runs at offset 0 of the next block:
1. Set `heldF0_ = currentF0_ = noteOnF0_`.
2. Latch pending controls and evaluate `targetShape_`.
3. Branch on the policy:
   - **`Reset`, or the first note after prepare/reset (`!sounding_`).** Call `bank_.reset()` and
     `detuneShadow_.fill(1.0f)`. Set `shape_ = targetShape_` (FR-050 exemption) and `intervalPhase_ = 0`
     (realigns the grid to the note, so SC-012's "different prior states" renders are bit-identical). Mark
     `panDirty_ = panNonZero_`.
   - **`FreeRunning` while sounding.** Keep MCF state, `shape_` and the grid.
4. Compute the mask at the new f0 and deliver it (FR-051(d)).
5. Set `baseF0_ = f`, with **no chase clamp**: a FreeRunning jump > 1 semitone crossfades, as E-7 expects.
6. Apply the multipliers, then `loadFrame`. After a reset `frameLoaded_` is false, so the phases are seeded from
   `partial.phase = 0` (`:286–293`), `sinState = 0`, `cosState = 1`, and the first output sample is exactly 0.0
   (FR-045).
7. Forward the pan if dirty, set `sounding_ = true`, and clear `noteOnPending_`.

A `FreeRunning` `noteOn` at an unchanged f0 and unchanged controls changes no bank state, because every step is
skipped by bit comparisons. SC-012 bit-identity follows.

### S4.8 Sanitising and clamps (FR-062)

- `sanitizeF0(x, prev)`: if `!detail::isFinite(x)`, return `prev`; otherwise clamp x to [8, capHz_]. Denormal
  and negative inputs clamp to 8 Hz.
- `setControls`: for each field, if non-finite keep the *pending* value (FR-062: "keeps its previous value"),
  otherwise clamp to the FR-002 range. `controlsDirty_` is set only if a field changes bitwise.
- `setPartialPanOffsets`: a non-finite element becomes 0.0f. Without this, the bank's `std::clamp` passes NaN and
  `cos(NaN)` reaches the output. `panNonZero_` = any element `!= 0.0f` (−0.0 counts as zero); `panDirty_` is set
  if anything changed.
- `SpectralShapeRecipe::sanitize`: a non-finite field takes `kDefaultControls`' value (FR-002, recipe-level
  default), then is clamped.

### S4.9 RT-safety statement (FR-060)

The audio-thread methods are `setControls`, `setFrequency`, `setRetriggerPhase`, `noteOn`,
`setPartialPanOffsets`, `processBlock` and the observers. None allocates, locks, throws or does I/O. All storage is
member arrays. `bank_.reset()` inside `applyNoteOn` is array fills only (`:203–240`, E-1). `std::span` observers
return views of members. `static_assert(noexcept(...))` covers each (SC-015).

---

## S5. The one shared-header change (FR-064, FR-067)

In `processors/harmonic_oscillator_bank.h`:
1. Hoist the literal to class scope: `static constexpr float kCenterPanGain = 0.7071067811865476f;` in the
   constants block (`:78–105`).
2. In `reset()` (`:225–227`), replace the function-local `kCenterGain` with `kCenterPanGain`. It is the same
   literal, so the output is bit-identical (SC-014(d) proves it).
3. Append one public method after `applyExternalFrequencyMultipliers`:

```cpp
    /// @brief Restore the exact centre pan tables that reset() writes (Profundum FR-064).
    /// Refills panLeft_/panRight_ with kCenterPanGain; touches no phase, amplitude, detune,
    /// panPosition_ or cache state. For callers that never use setStereoSpread().
    /// @note Real-time safe
    void restoreCenterPan() noexcept {
        panLeft_.fill(kCenterPanGain);
        panRight_.fill(kCenterPanGain);
    }
```

Nothing else in the bank, `harmonic_oscillator_bank_simd.h/.cpp`, `harmonic_types.h` or `additive_oscillator.h`
changes (SC-023). `SpectralShapeRecipe::kCenterPanGain` restates the same literal for P0's derivation, because
the recipe (L2) does not include the bank. A test asserts the two are equal.

---

## S6. Algorithm summary — why each choice

| Choice | Justification |
|---|---|
| Additive bank, zero latency | Roadmap rejects the IFFT `AdditiveOscillator` (latency = FFT size). The MCF bank is already SIMD and exact-frequency. |
| Gain vector in harmonic order | FR-031 pitch invariance by construction. The mask is the only f0-dependent stage. |
| Unweighted power normalisation | Q8 / D-2: deterministic and pitch-invariant. P0 is fixed by −12 dBFS (D-4). |
| Great-circle slew | Exact norm preservation plus a hard max-element bound from one chord limit. A per-element clamp plus renormalisation cannot guarantee the bound after renormalisation. |
| Never-slewed mask | Q3: the cap tracks pitch exactly. The bound under motion is L_f × octaves moved. |
| Exact ε via Chebyshev and multipliers | Exact harmonic frequencies at U = 16, one sin/cos per update, no crossfades, no bank change. |
| Base chase ≤ 0.9 semitone per interval | Keeps every `loadFrame` under the bank's crossfade threshold. |
| `body²` | Lipschitz compliance (C-3). |

---

## S7. Determinism and portability

- **Determinism (FR-063):** no RNG. The bank's LCG runs only with bandwidth > 0, which is never the case here
  (FR-044, E-10). Same call sequence ⇒ bit-identical in-process. Cross-toolchain comparisons use tolerances.
- **No `std::isnan`/`std::isfinite`:** only `detail::isFinite` is used.
- **No narrowing in brace init:** constants use designated initialisers with `f` literals; `double`→`float` is
  always an explicit `static_cast`.
- **No new SIMD:** the bank's existing `LoadU`/`StoreU` kernel is reused. `lint-simd-aligned-loadstore.js` is run
  anyway.
- **Apple globals:** no namespace-scope `Rect`/`MAC`-like names. Test helpers live in `Krate::DSP::ProfundumTest`.
  Run `lint-apple-globals.js`.
- **Nested-aggregate constexpr trap:** see S3.1.
- **`std::span` (C++20):** already used in `render_fingerprint.h`.
- **Denormals (E-11): owner FTZ/DAZ is a hard precondition.** The bank's SIMD amplitude smoother is
  `vAmp = MulAdd(vCoeff, Sub(target·aa, vAmp), vAmp)` (`simd.cpp:91–93`), with coeff = 1 − exp(−1/(0.002·fs))
  ≈ 0.0104 at 48 kHz. Toward a 0 target, without FTZ, it does not pass through the denormal range: once
  `currentAmplitude_` = k·2⁻¹⁴⁹ with 0.0104·k < 0.5 (k ≲ 48), each step rounds to zero and the value stays
  denormal for good. Zero targets are routine here: every even partial at emphasis −1 (SC-022) and every capped
  partial, up to about half of the 64–96 lanes. The bank's 1e-8 floor (`:755`) covers only lanes ≥
  `activePartials_`. Correctness (the values stay finite) does not depend on FTZ; CPU does, because x86 denormal
  arithmetic is slow. So the owner must run `processBlock` with FTZ/DAZ on. Plugin processors construct
  `ScopedDenormalMode` (`core/scoped_denormal_mode.h:60`), which sets MXCSR on x86 only (`:37–74`) and relies on
  AArch64's default FPCR flush (`:27`). Test exes set FTZ/DAZ in `dsp_test_main.cpp`. The precondition is in the
  `ProfundumCore` header `@note` (S4.1). The core's flush of |deliveredₙ| < 1e-12 to 0 is target hygiene only: the
  stall is in the decaying state, not the target. The core does **not** toggle MXCSR. The perf test runs under the
  test main's FTZ, as production does, and `ProfundumCore_ZeroTargetLanesUnderFtz` (S8.3) checks the precondition
  in the test environment.

---

## S8. Test plan

### S8.1 Files, targets, tags

| File | Target | Contents | Tags |
|---|---|---|---|
| `dsp/tests/unit/processors/spectral_shape_recipe_test.cpp` | `dsp_processors_tests` | recipe-only SCs (fast) | `[processors][profundum]` |
| `dsp/tests/unit/processors/harmonic_oscillator_bank_tests.cpp` (existing) | `dsp_processors_tests` | `HarmonicOscillatorBank_RestoreCenterPan` | `[processors][harmonic_oscillator_bank]` |
| `dsp/tests/unit/systems/profundum_core_test.cpp` | `dsp_systems_tests` | behaviour, contract, short renders, L==R | `[systems][profundum]` |
| `dsp/tests/unit/systems/profundum_core_spectral_test.cpp` | `dsp_systems_tests` | 262 144-point / BH7 analyses; full grids `[long]` + one per-push smoke case | `[systems][profundum]` (+`[long]`) |
| `dsp/tests/unit/systems/profundum_core_perf_test.cpp` | `dsp_systems_tests` | SC-013, OQ-3 figures | `[systems][profundum][.perf]` |
| `dsp/tests/unit/systems/profundum_core_nonfinite_test.cpp` | `dsp_systems_tests` | SC-016 non-finite, SC-020(d) | `[systems][profundum]` — **in the `-fno-fast-math` block** |
| `dsp/tests/unit/processors/spectral_shape_recipe_test_helpers.h` | — | vector descriptors (S8.2) | — |
| `dsp/tests/unit/systems/profundum_core_test_helpers.h` | — | render helpers, BH7, Goertzel, rendered descriptors, `requireLREqual` | — |

Helpers are `inline` functions in `namespace Krate::DSP::ProfundumTest`. Do not put them in an anonymous
namespace in a header: unused-function warnings would break the zero-warning rule. `[long]` is used only where a
case exceeds about 15 s and its failure mode is toolchain-independent. NaN/Inf and L==R cases are never `[long]`
(CLAUDE.md).

### S8.2 Shared measurement helpers (exact definitions)

- **Vector descriptors** `describe(std::span<const float> a)` returns E_sub, E_body (n 2–8), E_pres (n ≥ 9),
  R_body, R_pres, h1/rest, C, σ, odd/even and odd/even clipped to ±30 dB, as spec "Descriptor definitions"
  defines them. Distances use the spec's 6 dB/oct weighting.
- **Rendered harmonic powers:** render steady state, discarding ≥ 50 ms after the last control change, after
  convergence. Take one 262 144-sample frame of L. Call `lowFreq::magnitudeSpectrum` (`:118`), then
  `harmonicMainLobePower(mags, n·f0, fs/262144)` (`:180`) for every n with n·f0 < fs/2, giving wₙ. Then apply
  `describe` to √wₙ.
- **BH7 window** (`double`, periodic, length L): 7-term Blackman–Harris coefficients
  {0.27105140069342, 0.43329793923448, 0.21812299954311, 0.06592544638803, 0.01081174209837,
  0.00077658482522, 0.00001388721735}. Alternating signs: w[k] = Σⱼ (−1)ʲ aⱼ cos(2πjk/L).
- **Goertzel phase** (SC-001): a complex single-bin DFT in `double` of the BH7-windowed frame at the nominal
  n·f0, returning `atan2`.
- **Parseval-normalised aliased power** (SC-009): bin powers |X_k|² scaled so their sum equals
  mean(x²)/mean(w²). Sum bins 1..L/2 outside the exclusion zones and compare against 0.5 (= 0 dBFS).
- **`requireLREqual(L, R)`:** `std::memcmp` over the whole buffer, `REQUIRE == 0`. Every zero-pan fixture calls
  it (SC-014(a)).
- **Render helper** `renderCore(core, controls, f0, seconds, policy)`: prepare, setControls, noteOn, render in
  64-sample blocks (grid-aligned), and optionally record `shapeGains()`/`deliveredGains()` after each block.
  With a grid-aligned 64-sample block, each block contains exactly one control update at its first sample.

### S8.3 Per-criterion tests

Below, "Recipe" means `spectral_shape_recipe_test.cpp`, "Core" `profundum_core_test.cpp`, "Spectral"
`profundum_core_spectral_test.cpp`, "NonFinite" `profundum_core_nonfinite_test.cpp`, "Perf"
`profundum_core_perf_test.cpp`.

| SC / FR | TEST_CASE (file) | Assertion strategy |
|---|---|---|
| SC-001 | `ProfundumCore_PartialFrequencyAccuracy` (Spectral, `[long]`) | `kSawAnchor`, MIDI 12–60 at 44.1/48/96 kHz, n ∈ {1,2,3,8,16,32,64} measured iff n·f0 < capHz and the delivered (post-mask) gain is ≥ −60 dB re h1 (C-8 (ii) amendment; the old "below the taper start" rule leaves no h64 note in [C3, C4) at 44.1 kHz). Assert n ∈ {1,2,3} measured at every note and 16/32/64 measured ≥ 1 note per octave C1–C4. Two 1 s BH7 frames starting 1 s apart; Δφ wrapped against 2π·n·f0·1 s → Hz → cents; `REQUIRE(abs(cents) < 0.1)`. Smoke arm: MIDI 36, 48 kHz, n = 1, 8. |
| SC-002 | `SpectralShapeRecipe_DepthMonotonic`, `SpectralShapeRecipe_DepthWalksWaveformPath` (Recipe); `ProfundumCore_DepthRenderMonotonic` (Spectral, `[long]`) | 33-point sweep + `kDepthTriangle`, 27 grid points: h1/rest strictly increasing (depth 1 may be +inf; treat it as the largest). Least-squares log-log slope over n = 1..15 at body = edge = 0, emphasis 0: 1 ± 0.05 at depth 0, 2 ± 0.05 at 0.5; rest ≤ −60 dB at depth 1. Render at C1/C2/C3 (grid and sweep stride 4 per-push smoke). |
| SC-003 | `SpectralShapeRecipe_PowerNormalised` (Recipe); `ProfundumCore_DepthLoudnessFlat` (Core) | Recipe: 10 000 random Controls × f0 log-uniform [8, capHz] at 3 rates, \|Σa² − P0\|/P0 ≤ 1e-4 (seeded `std::mt19937` 0x5EED, test-local). Render: per Depth step, RMS over an integer number of f0 periods ≥ 1 s (only that window; no FFT); within ±0.5 dB of the sweep median at every grid point, C1/C2/C3. |
| SC-004 | `ProfundumCore_AudibilityGate_BodyEdgeShift` (Recipe-vector arm in Recipe; render arm in Spectral `[long]`) | As amended (C-1, C-2): Body strictly monotone at all 27 points, span ≥ 6 dB at the 22 points with headroom (−R_body at body = 0, computed in the test) ≥ 6 dB and ≥ 0.7 × headroom at the other 5; Edge strictly monotone with span ≥ 6 dB everywhere; Shift strictly monotone with span ≥ 1 oct on its 16 points, strictly increasing at the 8 depth-1 points with body + edge > 0 and constant (1e-6 relative) at depth 1, body = edge = 0 (FR-016 amendment); endpoints ≥ 6 dB. Render arm: the same descriptors from rendered wₙ at C1/C2/C3 (48 kHz, N = 64: nothing in the cap taper, C-8 (i)), on the same point sets except the depth-1 Shift points (C-2), against **the same bars unchanged** (6 dB, 0.7 × headroom, 1 octave). The spec states main-lobe ratios are exact with the Hann pairing, so no measurement allowance is taken; the recipe margins (8.65 dB, 0.740, 1.427 oct, 7.07 dB) are the only slack. A render miss is a defect to diagnose, not a tolerance to add. |
| SC-005 | `ProfundumCore_AudibilityGate_BodyCurvatureEmphasis` (Spectral) | C2, mid grid, rendered: σ strictly monotonic (decreasing, S3.6) with span ≥ 0.25 oct; odd/even strictly decreasing over emphasis [−1, 0], span ≥ 20 dB (first point measured, not +inf, because rendered even bins sit at the noise floor; assert it exceeds the second point); FR-023 floor at every step. |
| SC-006 | `SpectralShapeRecipe_WaveformAnchors`, `SpectralShapeRecipe_AnchorCoordinatesAnalytic` (Recipe); `ProfundumCore_WaveformAnchorsRendered` (Spectral) | Recipe: per-n dB vs the reference law (±1.5 dB n ≤ 15/16, ±3 dB beyond), evens ≤ −50 dB at the triangle, L2 error ≤ 5 %. `static_assert(kSawAnchor.depth == 0.0f)`, `REQUIRE(baseExponent(kTriangleAnchor.depth) == 2.0f)`, `kSineAnchor.depth == 1.0f`. Render at C1 with the same tolerances, plus the aliasing gate (power outside the SC-009 exclusion zones ≤ −60 dB re total, BH7, same FFT-length rule). |
| SC-007 | `SpectralShapeRecipe_NamedDistributions` (Recipe); `ProfundumCore_NamedDistributionsRendered` (Spectral) | Ordering + ≥ 3 dB dominance at C1/C2/C3; rendered per-harmonic levels (≥ −40 dB re loudest) within ±3 dB of the recipe vector. |
| SC-008 | `ProfundumCore_SpectrumInvariantC1toC3` (Spectral, `[long]`) | 25 semitones × (6 named + 5 colours + mid grid) at 48 kHz, N = 64: every harmonic ≥ −40 dB re h1 and below the taper start within ±0.5 dB of the recipe vector. At 48 kHz / N = 64 nothing is in the taper at C1–C3; the test still asserts that and fails if a harmonic is excluded, so a constant change cannot silently shrink the set. Smoke arm: C1, C2, C3 × mid grid. |
| SC-009 | `ProfundumCore_NoAliasingAllNotes`, `ProfundumCore_NoAliasingDuringBend` (Spectral, `[long]`) | Exactly per spec: BH7, FFT length = next pow2 ≥ max(2¹³, 64·fs/f0), ±8-bin exclusion around n·f0 and bins 0–8, the 50 % non-vacuity guard, Parseval-normalised sum ≤ −96 dBFS. Bend legs of 10 ms per semitone via a `f0PerSample` log-linear ramp (equivalent to the `setFrequency` glide and sample-accurate); frames start ≤ 2·64 samples after each leg. Smoke arm: MIDI 0, 60, 127 at 48 kHz. |
| SC-010(a) | `ProfundumCore_NoZipperControlSweeps`, `ProfundumCore_NoZipperPitchBend`, `ProfundumCore_ClickDetectorPositiveControl` (Core) | **Amended 2026-10-10 (D-1 ruling):** `profundum_core_test_helpers.h` gains `renderIdeal(...)`, a double-precision per-sample additive synthesis of the same control + pitch trajectory (targets evaluated every sample, `Reset` start phases, same frequency law, same mask — the diagnostic reference the D-1 table was built with) and `residual(core, ideal)`. `ClickDetector` with the spec's config, `prepare()`, `detect(residual, n)`: `REQUIRE(empty)` for D/B/E/S 0→1→0 steps (0.5 s holds), 10 ms sweeps, and a ±12-semitone 50 ms bend (C2, mid grid). Residual RMS re core RMS logged with `INFO`. Positive control: splice two `Reset`-start steady renders at one sample, subtract the ideal of the un-spliced step, `REQUIRE(size ≥ 1)`. |
| SC-010(b) | `ProfundumCore_NoZipperControlSweeps` sideband arm (Spectral) | f0 = 140.625 Hz (valid for I = 64 **and** U = 16, S4.6). 2 Hz triangle per control; 5 Hz ±2-semitone vibrato via `f0PerSample` at `kSineAnchor`. BH7, 2¹⁷ frame starting ≥ 1 s in. **Amended 2026-10-10 (D-2 ruling):** the FFT is taken of the residual (core − `renderIdeal`); region and subtraction exactly as specified, with the 25 % guard; `REQUIRE(residual region power ≤ −60 dB re the core's total)`. Positive control: `renderHeldRaw(...)` (ideal targets held for `kControlInterval` samples, no smoothing) minus ideal must read above −60 dB on at least one lever. S9.4's −69 dB simulation was the held-minus-continuous component, i.e. this residual measure. |
| SC-010(c) | `SpectralShapeRecipe_LipschitzBound` (Recipe) | 10 000 random pairs: controls Δ ≤ 0.01 → max\|Δa\|/Δ ≤ 8·√P0 (`kLipschitzControlCeiling`); f0 pairs ≤ 1 cent across onset, taper and clamp at 3 rates → ≤ 8·√P0 per octave. |
| SC-010(d) | `ProfundumCore_ShapeGainStepCeiling` (Core) | Record `shapeGains()` per 64-sample aligned block in every (a)/(b) fixture: max\|Δ\| ≤ `maxShapeStepPerInterval()`; Σa² = P0 ± 1e-4 relative for shape and delivered vectors. Reset `noteOn` exemption asserted separately (first vector = target). |
| SC-011 | `ProfundumCore_LowNoteGuardReducesUpperEnergy` (Spectral); `SpectralShapeRecipe_LowNoteGuardContinuity`, `SpectralShapeRecipe_GuardIsConstant` (Recipe) | (a) rendered R_pres(16.35 Hz) ≤ R_pres(32.70 Hz) − 6 dB at `kSawAnchor` and mid grid (model 10.45/8.56). (b) Vectors for f0 ≥ onset equal the onset vector within 1e-6 relative (nothing in the taper). (c) 1-cent sweep MIDI 0–127 at 3 rates, N = 96, over the 14 `recipe-model.js` states (6 named, 5 colours, mid grid and the +44.28 dB corner {0, 1, 1, 0, 1, 1}): ≤ 0.1 dB where both ≥ −80 dB re h1, else \|Δa\| ≤ 8·√P0/1200 (model: 0.0900 dB, 0.001 × bound). (d) h1 share below onset ≥ at onset. (e) `static_assert(SpectralShapeRecipe::kLowNoteGuardOnsetHz == 32.70f)`; `static_assert(sizeof(Controls) == 6 * sizeof(float))`; concepts `Init6`/`Init7` (`requires { T{0.f,…} }` with six/seven floats) → `static_assert(Init6<Controls> && !Init7<Controls>)`; a six-element structured binding compiles. |
| SC-012 | `ProfundumCore_RetriggerPhasePolicy` (Core) | Reset: `L[0] == 0.0f` exactly after `noteOn`. Two renders after `noteOn` from different prior states compared with `memcmp`. FreeRunning same f0/controls: `memcmp` vs an uninterrupted render. FreeRunning +0.5 semitone: boundary delta ≤ max delta over the preceding 100 ms and `y[k0] != 0.0f`. `noteOn` timed via block alignment so k0 lands where the saw output is ≥ 0.25 × peak (chosen by scanning a pre-render). |
| SC-013 | `ProfundumCore_CpuBudget` (Perf, `[.perf]`) | 48 kHz, block 64, 64 partials, D/B/E/S full-range 2 Hz triangles + 5 Hz vibrato `f0PerSample`; 10 s audio per run, `std::chrono::steady_clock`, median of 5: `REQUIRE(pct ≤ 1.0)`. Also prints (no assert) the 96-partial figure and the static (all-skip) figure. Run alone: `node tools/run-cpu-tests.js dsp_systems_tests`. |
| SC-014 | `ProfundumCore_ZeroPanBitIdentical`, `ProfundumCore_PanHookForwards`, `ProfundumCore_PanReturnToCentreBitIdentical` (Core); `HarmonicOscillatorBank_RestoreCenterPan` (bank tests) | (a) via `requireLREqual` everywhere. (b) non-zero vector → L ≠ R. (c) non-zero → render ≥ 64 → zeros, in grid-aligned 64-sample blocks: from the first control interval after the return, L and R are `memcmp`-equal to each other **and** each is `memcmp`-equal to the same channel of a **twin core** that received every call except the pan excursion. Pan writes only the pan tables, so the twin proves that phase, amplitude and all other bank state were untouched (FR-048(c)). A restore through `bank_.reset()` would zero phases and amplitudes and fail the twin comparison. (d) bank: `applyPanOffsets(non-zero)` then `restoreCenterPan()` → L == R bitwise; a render compared with a twin bank that only had `reset()` is bit-identical; `restoreCenterPan()` mid-render does not change L (pan-only); `kCenterPanGain` equals `SpectralShapeRecipe::kCenterPanGain`. |
| SC-015 | `ProfundumCore_NoAllocationOnAudioThread` (Core), `SpectralShapeRecipe_NoAllocation` (Recipe) | `AllocationDetector::instance().startTracking()` … `stopTracking() == 0` around every audio-thread method (zero and non-zero pan and the return to zero). `static_assert(noexcept(...))` for each method and each recipe function. |
| SC-016 | `ProfundumCore_NonFiniteInputsRejected` (NonFinite); `ProfundumCore_PartialCountExtremes` (Core) | NaN/±Inf/denormal by bit pattern (`std::bit_cast<float>(0x7FC00000u)` etc.) into `setFrequency`, `noteOn`, `setControls` fields and pan offsets: output finite (bit-test), `stateFinite()`. 8 Hz and clamp-ceiling renders are finite; ceiling vector is h1-only with a₁² = P0 ± 1e-4. N = 1, 96; `prepare(fs, 128)` gives `numPartials() == 96`. |
| SC-017 | `ProfundumCore_Deterministic`, `ProfundumCore_SampleRateChange` (Core / Spectral) | Two instances, same calls: `memcmp`. 48 → 96 → 44.1 kHz `prepare`: SC-001 n = 1, 8 and SC-008 mid grid re-asserted at each rate (Spectral; at 44.1 kHz the C3 harmonics above the 7 723 Hz taper start are excluded, C-8 (i)). |
| FR-041 / E-6 | `ProfundumCore_SampleRateExtremes` (Core) | `prepare` at 22 050 and 192 000 Hz: output finite (bit-test), `stateFinite()`; SC-018 level (−12 dBFS ± 0.1 dB, mid grid, C2); SC-021(c) cap exactness over a ±12-semitone 50 ms bend; SC-010(d) step ceiling with `maxShapeStepPerInterval()` == 375·√P0·64/fs within 1e-6 relative. Clamp: `prepare(16000)` gives `maxShapeStepPerInterval()` bit-equal to `prepare(22050)`'s, and `prepare(400000)` bit-equal to `prepare(192000)`'s. |
| SC-018 | `ProfundumCore_OutputLevelConstant` (Core) | RMS of L over an integer number of periods ≥ 1 s: −12 dBFS ± 0.1 dB at 6 named + 5 colours + mid grid × C1/C2/C3 × 3 rates. |
| SC-019 | `ProfundumCore_BodyColoursDistinct` (Recipe-vector arm + Spectral render arm) | Pairwise Body-colour distance ≥ 6 dB; extremal orderings with ≥ 1 dB / 0.05 oct (model: 16.24 dB; 0.375 / 18.9 / 0.064 / 1.27). |
| SC-020 | `ProfundumCore_PitchTrajectoryNullptr`, `ProfundumCore_PitchTrajectoryBlockSizeIndependent`, `ProfundumCore_PitchTrajectorySampleAccurate` (Core); `ProfundumCore_PitchTrajectoryNonFinite` (NonFinite) | (a) `memcmp`. (b) blocks 512 / 64 / 37 → `memcmp`. (c) `kSineAnchor`, C3→C1 log-linear drop inside one 512 block: h1 phase from analytic-signal tracking of L (Goertzel over a 1-period sliding window, ±1 period around each check point), compared with 2π∫f0 dt (double) → \|error\| ≤ 0.5·(16/fs)·\|Δf0\| + 0.02 cycle at drop end and +100 ms. (d) NaN/Inf elements → finite output and f0 held. |
| SC-021 | `SpectralShapeRecipe_ShapeMaskComposition` (Recipe); `ProfundumCore_MaskNeverSlewedBend`, `ProfundumCore_CapTracksPitchExactly`, `ProfundumCore_NoteOnMaskImmediate` (Core) | (a) composition vs `evaluate`, 1e-6 relative. (b) converged controls; bends via `f0PerSample`; per 64-block \|Δdelivered\| ≤ 8·√P0·\|Δlog₂f0_interval\| + 1e-6, where f0_interval = `maskFrequency()` read after each grid-aligned block. That is the f0 the control update at the block's first sample built the mask from. (`currentFrequency()` would return the f0 of the pitch update at sample 48, which gives false failures on a rising bend and misses a lagging cap on a falling one.) (c) delivered == 0.0f exactly for n ≥ 2 with n·f0_interval ≥ capHz, with the same `maskFrequency()` f0_interval. (d) after `noteOn`, the first block's delivered zeros at the cap. (e) Σ = P0 ± 1e-4. |
| SC-022 | `SpectralShapeRecipe_EmphasisActsOnWholeVector` (Recipe); `ProfundumCore_EmphasisOddOnlyRendered` (Spectral) | Recipe: evens exactly 0 at emphasis −1 over grid × edge × curvature. Render at C2: evens ≤ −50 dB re h1 (n ≥ 10 at edge = 1 included); FR-023 floor at each emphasis step. |
| SC-023 | `ProfundumCore_KMaxPartialsUntouched` (Core) + footprint script | `static_assert(kMaxPartials == 96)`. `node specs/profundum-phase1-harmonic-core/check-footprint.js <base>` runs `git diff --numstat` and fails on any line in the four forbidden files. For the bank, every removed line must be one of the two `kCenterGain` lines, and the additions must be exactly the hoist + method. Output quoted in comply. |
| FR-071 (audibility) | covered by SC-004/005/011/012/019 render arms | levers include the retrigger policy (SC-012) and guard (SC-011). |
| Plan-internal | `ProfundumCore_DetuneShadowNoDrift` (Core) | 10⁶ pitch updates of 5 Hz vibrato, then a static note: SC-001 h1 error < 0.01 cent. |
| FR-043 (crossfade clause) | `ProfundumCore_NoCrossfadeOnGlide` (Core) | The bank crossfades when the ratio of successive `loadFrame`/`setTargetPitch` pitches exceeds 1 semitone (`:260–268`), and `baseF0_` is exactly that argument. Over the 24-semitone 10 ms drop (`f0PerSample`), the SC-009 bend legs and the 5 Hz ±2-semitone vibrato, in grid-aligned 64-sample blocks: for every pair of consecutive control intervals with no `noteOn` between them, \|12·log₂(`baseFrequency()`ₖ / `baseFrequency()`ₖ₋₁)\| ≤ `kMaxBaseStepSemitones` (0.9). In the bend and vibrato fixtures (below 675 semitones/s), `baseFrequency() == maskFrequency()` bitwise after every block (the S4.6 (i) premise). Positive control: a `FreeRunning` `noteOn` 2 semitones away is observed as a base step of 2 semitones (± 1e-4) at that interval, so the observer sees jumps the clause forbids. |
| Plan-internal | `ProfundumCore_FirstNoteSeedsPhases` (Core) | First `noteOn` with `FreeRunning` after `prepare`: `L[0] == 0.0f`. |
| FR-010 | `SpectralShapeRecipe_DepthOneSineAtAnyShape` (Recipe) | depth 1 over the 81-point grid body, edge, curvature ∈ {0, 0.5, 1} × shift ∈ {−1, 0, 1}, at emphasis 0 and −1, N = 64 and 96: rest ≤ −30 dB re h1 (model worst −43.8 dB). |
| FR-030 | `SpectralShapeRecipe_GuardMonotone` (Recipe) | `evaluateMask` from the onset down to 8 Hz in 1-cent steps at 22.05 / 48 / 192 kHz, N = 96: mask₁ == 1 exactly; for every n ≥ 2, maskₙ strictly decreasing as f0 falls below the onset and == 1 at and above it (no cap overlap, S3.3); at every f0, maskₙ non-increasing in n (attenuation non-decreasing). h1's share of E_total after `applyMask` is non-decreasing as f0 falls, at mid grid and `kSawAnchor`. |
| FR-002 / FR-003 | `SpectralShapeRecipe_ControlsSanitize` (Recipe); `SpectralShapeRecipe_SanitizeNonFinite` (NonFinite) | Recipe: each field at −10, +10, ±`FLT_MAX` clamps to its range end, and `evaluateShape` of the out-of-range input is bit-identical to that of the range end. aₙ ≥ 0 and finite for every element of the SC-003 10 000-sample set (FR-003). NonFinite: NaN / ±Inf by bit pattern in each field → `sanitize` returns `kDefaultControls`' value for that field and leaves the others unchanged. |
| FR-061 / E-2 | `ProfundumCore_BeforePrepareSilent` (Core) | Unprepared core: `isPrepared() == false`; buffers pre-filled with 1.0f → `processBlock` writes 0.0f to both. Every setter and `noteOn` is called before `prepare`; after `prepare` plus the same post-prepare calls, the render is `memcmp`-equal to a fresh core's (the pre-prepare calls were no-ops). Null buffer or zero length leaves the buffers untouched. |
| E-7 | `ProfundumCore_LargeFreeRunningJumpBounded` (Core) | `FreeRunning` `noteOn` ±12 and ±24 semitones from C2 at `kSawAnchor` and mid grid: every output sample finite (bit-test) with \|y\| ≤ `HarmonicOscillatorBank::kOutputClamp`, `stateFinite()`, and the first delivered vector zero at the new cap (SC-021(d)). Boundedness and finiteness only, as E-7 rules. |
| E-11 | `ProfundumCore_ZeroTargetLanesUnderFtz` (Core) | Checks the S7 precondition in the test environment. On x86 (`#if KRATE_HAS_SSE_DENORMAL_CONTROL`), `REQUIRE(_MM_GET_FLUSH_ZERO_MODE() == _MM_FLUSH_ZERO_ON)` at test start. Then `kTriangleAnchor` (every even target exactly 0) and a C6 note (capped lanes) are rendered for 5 s at 48 kHz: output finite, `stateFinite()`, and every even delivered gain == 0.0f. |
| Evaluation | `ProfundumCore_ListeningRenders` (Spectral, `[.listen]`) | Writes WAVs: C0–C4 static notes at the named coordinates, bends, sweeps, and E1 at 64/96 partials (S9.3), under the build dir. Not a gate. |

### S8.4 Expected runtimes

These are estimates; confirm on the first build and re-tag if any differ.
- Recipe TU: < 5 s (the SC-011(c) sweep is 14 states × 3 rates × 12 701 steps × 96 harmonics).
- Core TU: about 10 s (short renders).
- Spectral per-push smoke: about 10 s.
- Spectral `[long]` set: SC-002/004 render grids (≈ 2 750 frames of 262 144), SC-008 (325 frames), SC-009
  static (384 frames up to 2²⁰). Estimated 2–4 min at 0.3 % RT render cost plus about 15 ms per 2¹⁸ FFT,
  sharded across the shard runner.

---

## S9. Build integration, run order, decisions

### S9.1 CMake changes

1. `dsp/CMakeLists.txt`: add `include/krate/dsp/processors/spectral_shape_recipe.h` to
   `KRATE_DSP_PROCESSORS_HEADERS` (`:128`), and `include/krate/dsp/systems/profundum_core.h` to
   `KRATE_DSP_SYSTEMS_HEADERS` (`:159`), each under a `# Profundum Phase 1 (specs/profundum-phase1-harmonic-core)`
   comment.
2. `dsp/tests/CMakeLists.txt`:
   - `dsp_processors_tests` list: add `unit/processors/spectral_shape_recipe_test.cpp`. The bank test file is
     already listed (`:252`).
   - `dsp_systems_tests` list (before `:562`'s `)`): add the four `profundum_core_*` TUs under a comment block
     mapping TU → SCs, in the Vorago style.
   - `-fno-fast-math` block (`:666`–`:1040`): add `unit/systems/profundum_core_nonfinite_test.cpp` with the
     standard comment. Only that TU is added. The perf TU must stay out so its figures reflect shipping FP mode.
3. No KrateDSP source (`.cpp`) is added; both headers are header-only.

### S9.2 Build/test commands (Windows; full CMake path)

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_processors_tests dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_processors_tests.exe "[profundum]" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_processors_tests.exe "[harmonic_oscillator_bank]" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "[profundum]" 2>&1 | tail -5          # includes [long]
node tools/run-cpu-tests.js dsp_systems_tests                                                       # SC-013, alone
node specs/profundum-phase1-harmonic-core/recipe-model.js                                          # calibration
```

**Shared-header guard (FR-064, SC-023):** the bank change ripples to every consumer. Do one batched rebuild
(per the optimisation rule), then run the suites concurrently. None of them is timing-sensitive, so exclude
`~[performance]~[perf]~[.perf]~[benchmark]~[long]`:
`ruinae_tests`, `seraphis_tests`, `vorago_tests`, `innexus_tests`, `membrum_tests`, plus `dsp_processors_tests`
and `dsp_systems_tests` in full. Membrum is included because `mode_inject.h` consumes the bank. No plugin source
changes, so pluginval is skipped (CLAUDE.md: test/DSP-header-only changes). The rebuilt plugins still compile the
changed header, so the plugin suites are the regression evidence.

Then run `node tools/check-portability.js`, `node tools/lint-apple-globals.js`,
`node tools/lint-simd-aligned-loadstore.js`, and `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja`.

### S9.2a Parallelism

The recipe TU and the bank change are independent of the core. Recipe tests and the bank test can run while the
core is written. The CPU lane (SC-013) runs once, alone, after everything else is green.

### S9.3 OQ-3 — partial-count decision rule (executed in build, recorded here)

Ship **96** only if all three hold:
(1) the SC-013 harness at 96 partials measures ≤ 1.0 % (median of 5, isolated run);
(2) the E1 (41.2 Hz) listening render at 96 vs 64 partials is judged by the user to add audible presence. The
upper 32 partials span 2.64–3.96 kHz at E1;
(3) SC-008 at 96 partials passes with the taper exclusion of S3.3.
Otherwise ship **64**. `kDefaultPartials` stays 64 until all three are recorded.

| Figure | 64 partials | 96 partials |
|---|---|---|
| SC-013 % of one core (median of 5) | **0.510 %** (sweep + vibrato; ≤ 1.0 % PASS) | **0.690 %** (sweep + vibrato; condition (1) met) |
| E1 listening verdict (user, 2026-10-10) | baseline | "in some cases the 96 version has more presence, in others I can't really make out much of a difference" |
| Decision | **64 shipped** (`kDefaultPartials = 64`). Condition (2) is a partial yes, not the clean yes the rule requires; the comply stage's second pinned measurement put 96 at **0.943 %** against the 1.0 % bar (1.625 % unpinned), which would make the SC-013 gate flip with machine state. 96 remains reachable at `prepare` (Phase 9 may expose it); Phases 2 and 5 add the top end by nonlinearity. | |

Measured by T021 on 2026-10-10 at `kControlInterval` = 32, 48 kHz. `ProfundumCore_CpuBudget` ran alone on an idle
machine (≈ 3 % background load), pinned by `tools/pin-perf-cores.ps1` (mask 0xFFFF), the same launch path that
`tools/run-cpu-tests.js` uses. The log is `f:/tmp/cpu_t021_profundum.log`:
`PROFUNDUM_PERF partials=64 load=sweep pct=0.510`, `PROFUNDUM_PERF partials=96 load=sweep pct=0.690`,
`PROFUNDUM_PERF partials=64 load=static pct=0.150`, "All tests passed (8 assertions in 1 test case)".
Condition (3), SC-008 at 96 partials, was not measured by T021.

**Condition (3) measured 2026-10-10** by `ProfundumCore_SpectrumInvariantC1toC3N96` `[long]` (48 kHz, N 96, every
named state, MIDI 24-48, S3.3 taper exclusion): PASS. Worst error 0.00229 dB (midGrid) against the 0.5 dB bar;
kSawAnchor and kGrowl compared 2263 harmonics with 137 excluded in the taper. Conditions (1) and (3) are met.
Condition (2) is still the user's.

**E1 WAVs re-rendered 2026-10-10. The earlier set was invalid.** Before the SC-009 fix, the all-sine-phase-0 stack
railed the bank's ±2.0 output clamp (`HarmonicOscillatorBank::kOutputClamp`) at high-crest states from about
MIDI 20 to 62. E1 is MIDI 28, so the earlier listening WAVs at high-edge states were hard-clipped. The core now hands
the bank amplitudes ÷ `ProfundumCore::kBankHeadroom` (4) and scales the output back by the same factor. Both are
exact powers of two, so any sample that was not clipped is unchanged. The verdict for condition (2) must use the
re-rendered set in `build/windows-x64-release/profundum-listen/E1_N{64,96}_*_48k.wav`.

### S9.4 Pre-implementation evidence for kControlInterval = 64

**SUPERSEDED 2026-10-10 (D-4 ruling, user): `kControlInterval` = 32.** The −69 dB simulation below measured only
the held-minus-continuous sideband component and never the click arm; on the ruled residual measure (D-3) the
64-sample interval leaves 2-click residuals on depth and body after the bank's one-pole, because the 2.7 ms slew is
two sub-steps and a 2 ms one-pole cannot hide a 1.33 ms stair. The fallback below is adopted, extended to the click
arm, together with centring the ideal by the zero-order hold's mean delay ((I−1)/2 on steps, (I+1)/2 on sweeps).
f0 = 140.625 Hz stays valid (1500 / 140.625 = 10⅔); U = 16 unchanged; S9.3/S9.5 CPU is re-measured at 32.

The sideband simulation used the recipe model and the bank's per-partial one-pole (τ = 2 ms) on 2 Hz full-range
triangles at mid grid. It compares interval-held targets with per-sample targets and keeps only the component
fast relative to the interval. Results, as control-rate sideband power re total:

| Lever | I = 64 | I = 32 |
|---|---|---|
| depth | −71.6 dB | −83.5 dB |
| body | −74.7 dB | −86.7 dB |
| edge | −85.4 dB | −97.4 dB |
| shift | −69.0 dB | −80.9 dB |

The worst case (shift, −69.0 dB) is 9 dB inside SC-010(b)'s −60 dB. **Fallback if the real render fails:**
`kControlInterval = 32`. f0 = 140.625 Hz still works (1500/140.625 = 10⅔), so the fixture needs no re-derivation.
The CPU cost of that fallback is re-measured.

### S9.5 CPU expectation (to be replaced by measurement)

| Item | Estimate at 48 kHz, 64 partials | Measured (T021, I = 32) |
|---|---|---|
| Recipe | ≈ 64 × 3–4 float transcendentals ≈ 1.3 µs × 750/s ≈ 0.10 % | — (not measured separately) |
| `loadFrame` | 128 trig ≈ 1.5 µs × 750/s ≈ 0.11 % | — (not measured separately) |
| Pitch updates | 1 sin/cos + 64 recurrence/divides ≈ 0.3 µs × 3 000/s ≈ 0.09 % | — (not measured separately) |
| Slerp + mask | < 0.03 % | — (not measured separately) |
| Bank SIMD per sample (Highway dispatch + 8 AVX2 lanes ×8) | ≈ 0.12–0.20 % | — (not measured separately) |
| **Total under full sweep + vibrato** | **≈ 0.45–0.55 %** (budget 1.0 %) | **0.510 %** (96 partials: 0.690 %) |
| Static | ≈ bank only | **0.150 %** |

---

## S10. Risks and mitigations

| # | Risk | Mitigation |
|---|---|---|
| R-1 | L_c margin is 11 % (7.13 vs 8). float vs double could tip a sampled pair. | SC-010(c) measures the C++ float recipe directly. If it fails, raise `kBodyWidthNarrowOct` 0.15 → 0.17 (the model's next feasible point) and re-run `recipe-model.js`. Never relax the 8·√P0 ceiling. |
| R-2 | Detune shadow divergence → slow pitch drift. | Mirror with one identical `float*float`; reset both to 1.0f together; `ProfundumCore_DetuneShadowNoDrift`. |
| R-3 | Bank crossfade accidentally triggered (base jumps > 1 semitone). | Base chase ≤ 0.9 semitone per interval; only `noteOn` bypasses it; `ProfundumCore_NoCrossfadeOnGlide` asserts every consecutive `baseFrequency()` step ≤ 0.9 semitone directly. |
| R-4 | Base lag during an extreme drop (> 675 semitones/s at 48 kHz). AA/orbit correction is computed by the bank from `n·base·(ε ratio)`, which mis-scales partials whose n·base exceeds the taper start. | Bounded transient, confined to partials with n·f ≥ capHz/1.476 at the 24-semitone/10 ms worst case. Under C-8's taper those sit ≤ 674 cents below the cap, so they are already ≥ 68 dB down. The SC-009 bend (100 semitones/s) never lags. Documented in the header. |
| R-5 | Recipe-render disagreement at high partials (MCF orbit correction). | The bank compensates `1/cos(πf/fs)` (`:1077–1078`); MCF s-amplitude is exactly 1/cos(ω/2) for this variant, and phase-0 seeding gives sₖ = sin(kω)/cos(ω/2). SC-008/SC-018 measure it; the cap at 0.8 Nyq keeps the correction ≥ cos(0.4π) = 0.31. |
| R-6 | Within a control interval a rising partial can exceed the mask's cap frequency (mask is per-interval, pitch per-U). | ε is frozen at capHz for capped partials. For uncapped partials, travel per interval at the SC-009 bend rate is 0.13 semitone, far from Nyquist (cap = 0.8 Nyq ⇒ 3.86 semitones of headroom). The bank's ε clamp at 1.99 keeps any partial < 0.968 Nyq. |
| R-7 | Nested-aggregate constexpr constants fail on Clang/GCC only. | S3.1 pattern; `check-portability.js` + the Linux/macOS CI legs. |
| R-8 | Without FTZ, every zero-target bank lane stalls permanently as a denormal (`simd.cpp:91–93`, S7): costly on x86. | Owner FTZ/DAZ is a documented hard precondition (header `@note`); plugin `ScopedDenormalMode` on x86, default FPCR on AArch64; test main sets FTZ/DAZ; `ProfundumCore_ZeroTargetLanesUnderFtz`; perf measured under FTZ. The 1e-12 target flush is hygiene, not a mitigation. |
| R-9 | `[long]` spectral set too slow for local iteration. | Per-push smoke arm; shard the `[long]` set concurrently with other suites (it is not timing-sensitive); record wall clock after the first run. |
| R-10 | Fast-math folding a finiteness check. | Only `detail::isFinite` (barrier read, `db_utils.h:118`); NaN-injection only in the `-fno-fast-math` TU. |
| R-11 | SC-005 rendered odd/even at emphasis −1 is +∞ in theory, noise-floor-limited in practice. | Assert strict decrease from the measured first point (finite, large); FR-023 floor uses the same measured values. |
| R-12 | Spec conflicts C-1/C-2/C-8/C-9 left unratified → the build stage fails SC-004, SC-001's coverage clause (44.1 kHz) or FR-042's literal wording. | Ratify in clarify/challenge before `tasks.md`; the tests encode the amended point sets and conditions with a comment citing the C-item. |
| R-14 | The wider cap taper (C-8) dims the top octave of the partial set at high notes (from 0.35·Nyquist). | Audible only above C3 at 44.1/48 kHz, where those partials sit above 7.7 kHz; the `[.listen]` renders include C4 at 44.1 kHz for the user to judge. If it is ruled too dark, the only lever that keeps SC-011(c) is a lower +44.28 dB ceiling (weaker Body/Edge gains), not a steeper taper. |
| R-13 | Shared-header change breaks a consumer. | One batched rebuild; five plugin suites + both DSP suites; SC-023 footprint script. |

---

## S11. Implementation order (input to tasks.md)

1. Bank change (S5) and `HarmonicOscillatorBank_RestoreCenterPan`. Write the test first and watch it fail
   (method absent → compile error, then wrong values); then implement.
2. `spectral_shape_recipe.h` and the Recipe TU, test-first per SC. Re-run `recipe-model.js` and compare a printed
   vector at three coordinates (relative difference ≤ 1e-5).
3. `profundum_core.h` skeleton (prepare/reset/processBlock silence) and Core contract tests
   (`ProfundumCore_BeforePrepareSilent` for FR-061, SC-015, SC-016 partial counts, `ProfundumCore_SampleRateExtremes`
   clamp arm, SC-023 static_assert).
4. Control path: latch, slew, mask, delivery (SC-010(d), SC-021, SC-018, SC-012).
5. Pitch path: exact ε, base chase, trajectory (SC-020, plan-internal shadow/crossfade tests).
6. Pan hook (SC-014).
7. Spectral TU (SC-001…SC-011, SC-019, SC-022) and the smoke arm.
8. NonFinite TU and the CMake `-fno-fast-math` entry.
9. Perf TU (SC-013) and the OQ-3 figures (S9.3); listening renders.
10. Regression sweep (S9.2), portability linters, clang-tidy `dsp`, and the footprint script (SC-023).

---

## Review notes (2026-10-09)

All fourteen review issues were applied, with these deviations from the suggested resolutions:

- **Cap taper (blocker):** option (a), a dB-linear taper, was taken, not the SC-011(c) amendment. The suggested
  "≥ ~0.7 oct" width was too narrow: the highest pre-mask level re h1 over the control space is +44.28 dB, not about
  0 dB, so the taper is 1 430 cents. That width has a knock-on effect on SC-001's coverage clause at 44.1 kHz, which
  goes to ratification as C-8 (ii). The amendment strengthens the clause; it does not relax it.
- **C-1 (blocker):** the suggested 0.7 × headroom bound was adopted for the five low-headroom points, with the
  measured 0.740 quoted. Retuning the Body law to reach 0.8 is offered to the user as an alternative, not done.
- **C-2 / FR-016 (minor), rationale partly rejected:** the issue proposed justifying "non-decreasing at depth 1" as
  a float-resolution allowance. The model shows that is not true for recipe vectors: on float-rounded vectors, C
  changes by ≥ 2.7e-3 relative per sweep step at all 8 points with body + edge > 0. The plan therefore asserts
  **strictly increasing** there (stronger than both the draft and the suggestion), and constant at body = edge = 0.
  Only the render arm drops the depth-1 points, for a render-noise reason that C-2 states.
- **AA / orbit-correction mismatch (minor):** resolved by the second suggested route, with a gate. The chase already
  makes `baseF0_` equal to f at every `loadFrame` below 675 semitones/s, so the frequency-versus-ε mismatch is
  absent there. That premise is now asserted (`baseFrequency() == maskFrequency()`), and the within-interval
  staleness is quantified in S4.6 rather than gated separately.
