# Implementation Plan: Profundum Phase 3 — Sub Anchor

**Spec:** `specs/profundum-phase3-anchor/spec.md` (Clarified 2026-10-11)
**Roadmap:** `specs/Profundum-roadmap.md` Part A, Phase 3 (lines 305–330)
**Phase base:** commit `9b1a4cf5` on `synth/profundum` (Phase 2 code at `fe6ebe7d`, then two roadmap-docs commits)
**Status:** Planned 2026-10-11. **Section S1 holds fourteen items that need ratification before `tasks.md`** (C-11 to C-14 were added by the plan-stage
review: flush-then-clamp and the `pendingControls()` read-back, SC-004(b)'s return measured as a bitwise 1.0, SC-024(d)
recorded as structural in Phase 3, and SC-012(a)'s noise floor). C-2 carries
the calibration figures the spec's S1 obligation asks for (FR-024, SC-024(c)). Two items are flagged ⚠ RE-RULE: C-3
(FR-041's "N_b is lengthened" is the wrong direction) and C-9 (SC-012(b)'s 40 dB bar is the exact analytic value). C-4
changes the FR-008 `modeGain()` return type (float cannot resolve SC-006(a)). The rest are refinements with no bar
change.

Every number in this plan was **measured this session** by one of two tools:

| Artifact | What it is | Reproduce |
|---|---|---|
| `anchor-model.js` | Double-precision and `Math.fround` float32 model of the plan design: the S5 coefficient mirror against the bank's float chain, a float MCF state-rounding walk (plain and FMA-emulated), the calibration figures from the Phase 1 recipe model, triangle truncation, the asymmetric-sine onset ramp, the Hermite bridge length rule, and the alias positive control | `node specs/profundum-phase3-anchor/anchor-model.js [eps\|mcf\|cal\|tri\|onset\|bridge\|alias\|all]` |
| `bench/anchor-kernel-bench.cpp` | Standalone C++ microbenchmark of the S4 per-sample kernel under SC-018's worst-case load, against the real `dsp/include` headers | Compile line at the top of the file; run through `tools/pin-perf-cores.ps1` |

---

## S0. Verification ledger

Every reused signature below was read this session (2026-10-11). Paths are relative to `dsp/include/krate/dsp/` unless
stated.

| Component | File:line | Fact relied on |
|---|---|---|
| `ProfundumCore` | `systems/profundum_core.h` | FTZ/DAZ precondition :89–91. `enum class RetriggerPhase : std::uint8_t { Reset, FreeRunning }` :94. `kControlInterval = 32` :97, `kPitchUpdateInterval = 16` :98, `kMinF0Hz = 8.0f` :99, `kDefaultF0Hz = 55.0f` :100, `kMinSampleRate = 22050.0` :104 (max 192000 :105). `prepare(double, int)` :129 clamps the rate :131 and calls `bank_.prepare(sr)` :136. `reset()` :157. `setFrequency` :191–195 (`heldF0_ = sanitizeF0(f0Hz, heldF0_)`). `setRetriggerPhase` :197. `noteOn(float)` :204–209, "Effective at offset 0 of the next block". `processBlock(float*, float*, std::size_t, const float* = nullptr)` :233: E-2 no-op :235–236, `applyNoteOn` at offset 0 :242–243, grid loop :248–284 (pitch sample at every multiple of U :251–260, `controlUpdate` when `intervalPhase_ == 0` else `pitchUpdate` :261–264, runs never cross a U boundary :266), trajectory end-of-block bookkeeping :285–287. Observers `deliveredGains()` :294, `currentFrequency()` :298, `baseFrequency()` :300. `ExcitationControls` :330, `setExcitation` :352. `initialiseState` sets `heldF0_ = prevSampleF0_ = currentF0_ = baseF0_ = noteOnF0_ = kDefaultF0Hz`, `maskF0_ = 0`, `detuneShadow_.fill(1.0f)` :437, `intervalPhase_ = 0` :440, `sounding_ = false` :441. `controlUpdate` :448–507: `fChanged` bit test, `baseNew = sounding_ ? chaseBase(f0) : f0`, mask rebuilt iff `f0` differs bitwise from `maskF0_`, then `if (deliveredChanged \|\| baseChanged) { if (baseChanged) setBase(baseNew); refreshMultipliers(f0); deliverToBank(baseChanged); } else if (fChanged) refreshMultipliers(f0);`. `pitchUpdate` :511–517 (bit-equal f returns; else `refreshMultipliers`). `applyNoteOn` :520–604: Reset branch (`policy_ == Reset \|\| !sounding_`) resets the bank, `detuneShadow_.fill(1.0f)` :544, `intervalPhase_ = 0` :547, `buildMask(f)`, `setBase(f)`, `refreshMultipliers(f)`, `deliverToBank(true)`; FreeRunning branch keeps the grid and shadow, rebuilds the mask iff `f` ≠ `maskF0_`, `setBase(f)` with **no chase clamp** iff `f` ≠ `baseF0_`; `sounding_ = true` :602. `chaseBase` :666–687 (clamp to `baseF0_·2^(±0.9/12)` in double, float bound rounded toward the base with `nextafter`). `setBase` :690–697 (`epsBase_[k] = clamp(2·sin((k+1)·π·b/fs), ±1.99)` in double). `computeEpsilonTargets` :702–731 (ε*₁ = 2·sin(θ), θ = π·f/fs in double; frozen at 2·sin(π·capHz/fs) when `(k+1)·f >= capHz`; `dTarget = |eb| > 1e-9 ? float(ε*/eb) : 1.0f`; `multiplierScratch_[k] = dTarget / detuneShadow_[k]`). `refreshMultipliers` :733–739 (`detuneShadow_[k] = detuneShadow_[k] * multiplierScratch_[k]`, bit-identical to the bank's `detuneMultiplier_ *=`). `deliverToBank` :743–751 (`loadFrame(frame_, baseF0_, true)`). `sanitizeF0` :795–800. `trajectoryF0` :805–811. `kMaxBankEpsilon = 1.99` :814. `sounding_` :849, `intervalPhase_` :851. Partials carry `harmonicIndex = k + 1`, `inharmonicDeviation = 0` :405–407 |
| `HarmonicOscillatorBank` | `processors/harmonic_oscillator_bank.h` | Includes `core/math_constants.h` :36 (float `kPi`). `inverseSampleRate_ = 1.0f / static_cast<float>(sampleRate)` :135. Non-copyable :117–118. `loadFrame` :257: > `crossfadeThresholdRatio_` jump captures `crossfadeOldLevel_ = lastOutputSample_` (a level, not a phase) :262–269; first frame seeds `sinState_ = sin(phase)`, `cosState_ = cos(phase)`, `epsilon_[i] = 2.0f * std::sin(kPi * freq * inverseSampleRate_)` :289–294; every load calls `recalculateFrequencies()` :364. `applyExternalFrequencyMultipliers` :665–672 (`detuneMultiplier_[i] *= multipliers[i]`). Highway path when `!hasBandwidth_` :712–718; scalar effective coefficient `std::clamp(epsilon_[i] * detuneMultiplier_[i], -1.99f, 1.99f)` :732. `computePartialFrequency` :1025–1029 = `(n + deviation·inharmonicityAmount_)·targetPitch_`. `recalculateFrequencies` :1057–1067: `eps = 2.0f * std::sin(kPi * freq * inverseSampleRate_)`, clamped to ±1.99f |
| Bank SIMD kernel | `processors/harmonic_oscillator_bank_simd.cpp:79–106` | `hn::LoadU` throughout; `vEpsEff = hn::Clamp(hn::Mul(vEps, vDetune), ±1.99)` (a plain product, the same value as the scalar path); `vSinNew = hn::MulAdd(vEpsEff, vCos, vSin)`, `vCosNew = hn::NegMulAdd(vEpsEff, vSinNew, vCos)` (FMA-contracted state update) |
| `HarmonicFrame` / `Partial` | `processors/harmonic_types.h:21, 36–47, 52–61` | `kMaxPartials = 96`; `Partial::inharmonicDeviation = 0.0f` default |
| `SpectralShapeRecipe` | `processors/spectral_shape_recipe.h` | `Controls` :40–47. `kCenterPanGain = 0.7071067811865476f` :51, `kPowerTarget = 0.25238293779207720f` :52. `kNyquistCapFraction = 0.8f`, `kCapTaperCents = 1430`, `kCapTailCents = 10`, `kCapTaperDbPerCent = 0.09` :71–74. `capFrequency(double)` :94–96. `evaluateMask` :148–183: h1 exempt; per harmonic `nf >= capHz → 0`, `nf > taperStartHz` → dB-linear taper, linear tail over the last 10 cents; also applies the low-note guard below C1 :154–167 (the Anchor must not inherit it, S4.5). Named states :210–247 (`kDefaultControls` "= mid grid" :210) |
| `HarmonicFold` | `processors/harmonic_fold.h` | `kCalibrationPeak = 0.3552343859f` :83. `prepare(double, std::size_t)` :121, `reset()` :157, `setAmount(float)` :168, `setFundamentalHz(float)` :178, `processBlock(float*, float*, std::size_t)` :188, `latencySamples()` :271 = 0 |
| `PhaseAccumulator` | `core/phase_utils.h:154–180` | `double phase` wrapped at 1.0 by one subtraction. **Not composed** (S6: the Anchor needs a two-cycle wrap and a coefficient-mirrored increment) |
| `OnePoleSmoother` | `primitives/smoother.h` | `kCompletionThreshold = 0.0001f` :55 (absolute). `calculateOnePolCoefficient` = `exp(−5000/(ms·fs))` :77–92, i.e. 99.3 % at `ms`. `configure(float ms, float fs)` :160–164. `setTarget` (NaN → 0, Inf → ±1e10) :170–181, `ITERUM_NOINLINE`. `process()` snaps exactly to the target within 1e-4 :197–211. `snapToTarget()` :257. Plain copyable class |
| `detail::isFinite`, `flushDenormal` | `core/db_utils.h:118–129, 263–265` | Fast-math-immune bit test; `flushDenormal` zeroes \|x\| < `kDenormalThreshold = 1e-15f` :38 |
| `kPi` | `core/math_constants.h:28` | `inline constexpr float kPi = 3.14159265358979323846f` (the bank's operand) |
| `SubOscillator` | `processors/sub_oscillator.h:111, 273–276` | Idea only; hard phase reset on flip-flop edges. Not composed |
| Phase 1/2 test helpers | `dsp/tests/unit/systems/profundum_core_test_helpers.h` | Includes `systems/profundum_core.h` :32, namespace `Krate::DSP::ProfundumTest` :47. `requireLREqual` :61, `Render` :78, `renderCore` :90, `goertzelPhase` (BH7 single-bin, `atan2`) :157–171, `bh7PowerSpectrum` :206, `renderedHarmonicPowers` (Hann main lobe, 262144 frame) :235–249, `aliasFftLength` :257–263, `aliasedPower` (BH7, ±8-bin exclusion around every n·f₀ < fs/2, dB re 0.5) :273–305. `profundum_excitation_test_helpers.h:82` `renderExcited` |
| Shared test helpers | `tests/test_helpers/` | `render_fingerprint.h:73` `fingerprintRender`, `:122` `compareFingerprints`. `allocation_detector.h:111` `AllocationScope`. `artifact_detection.h:38–44` `ClickDetectorConfig` defaults, `:101` `ClickDetector(const ClickDetectorConfig&)`, `:130` `detect` |
| Test registration | `dsp/tests/CMakeLists.txt` | Systems Profundum block :569–631 (enumerated). `-fno-fast-math` block ending :1114 (Profundum entries :1108–1113) |
| Spectral test metrics | `tests/test_helpers/low_frequency_metrics.h` | `harmonicMainLobePower` :180–210 (peak bin ± 2 power sum, Hann main lobe; "the ratio of two such sums [is] exact" :172–177). `amplitudeFromMainLobePower` :219: sum = (A·N)²·3/32, √sum = 0.30619·A·N (:214–216), so w₁ is not A²/2 (SC-024(a) measures by ratio) |
| Test-main FTZ/DAZ | `tests/test_helpers/enable_ftz_daz.h:26–28`; `dsp/tests/unit/systems/profundum_core_test.cpp:23, 830–833` | `enableFTZDAZ()` sets `_MM_SET_FLUSH_ZERO_MODE(_MM_FLUSH_ZERO_ON)`. The Phase 1 TU includes `scoped_denormal_mode.h` only for `KRATE_HAS_SSE_DENORMAL_CONTROL` and asserts FTZ is on under the test main (S8.1) |
| Smoother timing | `primitives/smoother.h:55, 77–92, 197–211` | τ = `kLevelSmoothMs`/5 = 1 ms; exact snap below 1e-4 absolute (S4.9 settle times) |
| CPU runner | `tools/run-cpu-tests.js:56, 69` | `FILTER` selects `[.perf]` and `[long]`; `SETTLE_MS` default 60000 |
| Bank base commit facts | `git rev-parse --short HEAD` = `9b1a4cf5`, branch `synth/profundum` | Phase base for SC-019 / SC-020 |

ODR sweep (2026-10-11, `grep -rnE "AnchorOscillator|renderAnchor|kAnchorToH1|anchorFoldFeed|AnchorTest" dsp/ plugins/ tests/ tools/`):
**0 hits.** The spec's sweep (spec "New components") covers the remaining spellings. Every new type is nested in
`AnchorOscillator`; the test helpers are free functions in the existing namespace `Krate::DSP::ProfundumTest` with the
`anchor` prefix (S8.2), re-swept by T001.

---

## S1. Spec conflicts and refinements — RATIFY BEFORE TASKS

### C-1 (refinement of D-2 / FR-012, no bar change): the triangle is truncated at n ≤ 31 and cap-tapered

D-2 writes the triangle "band-limited at the cap". Taken literally that is every odd harmonic below 0.8·fs/2: 466 odd terms
at E0 / 48 kHz, which no per-sample additive evaluation can afford inside 0.3 %. The plan evaluates the series with a
Clenshaw recurrence (S4.5) over **`kTriangleTerms` = 16 odd terms (n ≤ 31)**, each multiplied by the recipe's cap taper at
n·f_T, where f_T is the clamped frequency the Anchor actually runs at (r_eff·f in FOLLOW, S4.4). So above f_T ≈ 270 Hz at
48 kHz the cap, not the count, ends the series.

| Truncation | THD (dB) | Omitted tail re h1 | First omitted harmonic | Crest | Kernel CPU (S9.8) |
|---|---|---|---|---|---|
| none (D-2 literal) | −18.333 | — | — | 1.732 | not feasible |
| K = 32 (n ≤ 63) | −18.334 | −62.0 dB | n = 65 at −72.5 dB | 1.7211 | 0.24–0.28 % |
| **K = 16 (n ≤ 31)** | **−18.335** | **−52.9 dB** | **n = 33 at −60.7 dB** | **1.7101** | **0.14–0.16 %** |

Every SC bar is unaffected: SC-001(b) checks n ≤ 15 within ±0.5 dB and total THD −18.3 ± 0.3 dB (−18.335 passes); even
harmonics are zero by construction; SC-010 sees no component above the cap at all; FR-055's ceiling uses crest 1.732,
which the truncated triangle stays under. K = 32 passes every bar too but leaves a 4–20 % margin against SC-018's 0.3 % in
a clang build, before MSVC's slower code generation. **Proposed wording for D-2:** "triangle = (8/π²)·Σ (−1)ᵏ
sin((2k+1)φ)/(2k+1)², k < 16, each term weighted by the recipe cap taper at (2k+1)·f."

### C-2 (FR-024 / SC-024(c) — the calibration figures the spec asks S1 to propose)

Measured with `anchor-model.js cal` (Phase 1 recipe model, 64 partials, 48 kHz; E1 and C2 give identical figures because
both sit above the C1 guard onset and far below the cap). ρ = a₁²/(P₀/2) is h1's share of the core's power:

| State | a₁ (per channel) | ρ | h1 rise, level 1 | RMS rise @ level 0.5 | @ 0.7 | @ 1 |
|---|---|---|---|---|---|---|
| `kSineAnchor` | 0.35523 | 1.000 | +6.02 dB | **+3.52** | +4.61 | +6.02 |
| `kSawAnchor` | 0.27829 | 0.614 | +6.02 dB | +2.47 | +3.34 | +4.53 |
| `kHeavy` | 0.32749 | 0.850 | +6.02 dB | +3.14 | +4.16 | +5.50 |
| `kHollow` | 0.01835 | 0.003 | +6.02 dB | +0.01 | +0.02 | +0.03 |
| `kGrowl` | 0.00444 | 0.000 | +6.02 dB | +0.00 | +0.00 | +0.00 |
| `kDefaultControls` (= mid grid) | 0.19008 | 0.286 | +6.02 dB | +1.33 | +1.88 | +2.69 |

**Proposal.**
- **k = `kAnchorToH1` = 1.0.** At level 1 the Anchor's fundamental equals the core's h1: the in-phase sum is +6.02 dB at
  f₀ on every state. FR-055's ceiling is then 1.75 ≤ 2.0 (S4.9).
- **Default `level` = 0.5.** It adds +3.52 dB at f₀ and at most +3.52 dB RMS (the sine state, ρ = 1). At level 0.25 it would
  be ≤ +1.94 dB.
- **SC-024(c) bound: the RMS rise at the default level is ≤ 3.6 dB on every listed state** (analytic maximum 3.52 dB,
  where the core is a pure sine and the rise is the coherent sum itself).

**A finding the user should see before ratifying.** Q6 calibrates the Anchor relative to the recipe's h1. On states whose
recipe deliberately thins h1 (`kHollow` −25.7 dB, `kGrowl` −38.1 dB below the sine anchor), the Anchor at level 1 is just as
thin: it adds +0.03 dB and +0.00 dB to the voice. It does not "anchor" those states. Fold cannot reduce a₁ (a₁ is the
recipe's pre-excitation value, FR-023), so this is not a processing loss. It follows from the ruled calibration. The class
API is the same either way, because `setCoreH1Amplitude` takes whatever the voice supplies. The choice belongs to Phase 9's
source of a₁:
- **(a) As ruled (default):** a₁ = `core.deliveredGains()[0] · SpectralShapeRecipe::kCenterPanGain`.
- **(b) A floor at the voice:** a₁ = max(that, a floor such as `kMaxCoreH1Amplitude`·10^(−12/20)).
- **(c) Absolute:** a₁ = `kMaxCoreH1Amplitude` always. Q6 rejected this.

The plan builds (a). This is recorded as an open question.

### C-3 ⚠ RE-RULE (FR-041 wording, no bar change): the overshoot rule shortens the bridge; it never lengthens it

FR-041 says "where the incoming slope would make the bridge overshoot (Fritsch–Carlson monotonicity condition violated),
N_b is lengthened." The bridge's start tangent is m₀ = N_b·g′₀ (g′₀ per sample, fixed by the curve it leaves). Fritsch–Carlson
for a zero end slope needs α = m₀/Δ ∈ [0, 3], i.e. **N_b ≤ 3Δ/g′₀**. Lengthening raises α and makes the overshoot worse.
The same holds for an opposite-sign slope, where the excursion beyond g₀ grows with m₀.

**Rule (S4.7):** N_b = the largest integer N ≤ N_nominal for which the cubic's interior extrema (closed form, roots of a
quadratic) stay in [0, max(B, g₀)]. N is found by integer bisection: at most 1 + ⌈log₂ N_nominal⌉ closed-form evaluations, once per event.
The longest nominal bridge is N_modeBridge = round(5 ms · 192 kHz) = 960 samples, so the bound is ≤ 11 evaluations at
192 kHz (≤ 9 at 48 kHz, N_nom = 240). N_b ≥ 1.

`anchor-model.js bridge` evaluates the rule on every sample point of the curves a bridge can leave (48 kHz):

| Case | Shortest N_b used | Out of range | max \|Δ²g\| / SC-006(a) analytic | max \|Δ²g\| / the incoming raised cosine's peak |
|---|---|---|---|---|
| PUNCH bridged re-arm from any point of a 1 ms, +12 dB rise | 1 (at the very top, Δ → 0) | 0 | 0.997 | 1.14 |
| GHOST bridged re-arm from any point of a 1 ms emergence (dip 2 ms) | 95 of 96 | 0 | 0.984 | 0.79 |
| Mode switch from any point of a 5 ms, +12 dB PUNCH fall | 240 of 240 | 0 | 1.000 | 1.20 |

SC-006(a)'s bar (≤ 1.05 × the bridge's analytic peak with the N_b actually used) stands. **Wording change:** "N_b is
shortened to the largest length whose cubic stays in [0, max(B, g₀)]."

### C-4 (FR-008 signature): `modeGain()` returns `double`

SC-006(a) bounds Δ²g by 1.05·\|A\|·π²/(2N²). For a 250 ms PUNCH fall at 192 kHz (N = 48000, A = −2.98), that is 6.4e-9. A
float g near 4 has an ulp of 4.8e-7, so a float observer's second difference is rounding noise 75× the bar, and the checker
fails by construction. g is computed in double (S4.7), and **`modeGain()` returns `double`**. `phase()` is already double.
The plan also adds four observers beyond FR-008. Each is listed here for ratification with its consumer:
- `phaseIncrement()` (double): SC-026(a).
- `isSounding()`: C-7's before-first-noteOn silence check (SC-014) and SC-027(e)'s "a noteOn from not sounding always
  arms". Phase 9 may read it for voice-reuse diagnostics, but nothing requires it.
- `pendingControls()`: SC-015's denormal read-back (C-11). It returns the sanitised pending copy.
- `MirrorState` / `mirrorState()`: the plan-internal test `AnchorOscillator_MirrorTracksCore` (S8.3). These are **test-only
  diagnostics**. The header marks them `// TEST-ONLY diagnostic: not a Phase 8/9/11 contract, never serialised`. They
  carry no append-only obligation, and Phase 8 `MorphState` and Phase 11 state serialisation must never read them.

No bar changes.

### C-5 (refinement of FR-031): the coefficient law is mirrored bit-faithfully, not re-derived in double

FR-031 says the Anchor uses "the same MCF coefficient law ε = 2·sin(π·f/fs) at each update". The h1 actually rotates by
2·asin(ε_eff/2) with ε_eff = clamp(float(bankEps(base)) · float(shadow), ±1.99f), where bankEps is computed in **float**
(`harmonic_oscillator_bank.h:1063–1064`) and the shadow multiplier carries a float division and product
(`profundum_core.h:726–738`). `anchor-model.js eps`, 60 s drift against that rotation, worst over E0/E1/E2/C2/C3 ×
22.05–192 kHz:

| Anchor increment law | Worst 60 s drift | Where |
|---|---|---|
| Double law 2π·f/fs (FR-031 literal) | 0.168° | C3, 48 kHz |
| **S5 mirror (bit-faithful float chain)** | **0.000°** | — |
| S5 mirror, bankEps off by ±1 ulp (fast-math reassociation of `kPi*freq*invFs`) | 0.307° | C3, 48 kHz |

Separately, the core's own float MCF state rounding (`anchor-model.js mcf`, plain and FMA-emulated, E0/E1/C3, 48/96 kHz)
walks at most **2.6e-3°** in 60 s. That figure confirms D-1's ISA estimate.

The mirror reproduces the core's base chase, the bank's float ε, the shadow product and the clamp from the same inputs,
so its expected error is 0. A one-ulp disagreement from fast-math reassociation stays inside SC-003's 0.5° bar. Two ulps at
C3 would not, and that is what FR-032's fallback exists for. The fallback is pre-designed in S5.4 and invoked only on a red
SC-003. **Residual (recorded, not gated in Phase 3):** a recipe-control slew while the note's f₀ is static makes the core
call `refreshMultipliers` with an unchanged f₀ (`profundum_core.h:495–502`). That can move the shadow by ≤ 2 ulp, which the
Anchor cannot see. Static-control fixtures never exercise it. Phase 9's soak measures it (S13).

### C-6 (refinement of FR-016): the asymmetric sine's onset ramp is one period of f₀

The h2 term −0.25·cos 2φ is multiplied by a raised-cosine ramp r(n) = ½(1 − cos(π·n/N_on)), with N_on = round(fs/f₀) (one
period at the note-on f₀), clamped to [1, 4·fs]. `anchor-model.js onset`, ratio of max \|Δy\| in the first 10 ms to the
steady-state max \|Δy\|, worst over E0–C3 × 22.05–192 kHz: **1.0000** at P = 1 period (0.25 periods: 1.0000; 0.5 periods:
1.0062). The SC-017 bar is 1.1. The first sample is exactly 0.0 (u = sin 0 = 0 and r(0) = 0). The ramp runs on every Reset
noteOn, and on a FreeRunning noteOn from not sounding, for the asymmetric sine only (FR-016). A waveform switch into the
asymmetric sine does not ramp: the crossfade covers it.

### C-7 (refinement, spec silent): before the first noteOn the Anchor is silent

FR-006 defines *sounding* from the first `noteOn`. Before it, the core is silent because the bank has no frame loaded
(`harmonic_oscillator_bank.h:694, the stereo path the core uses`). After `prepare` / `reset` and before a noteOn, `processBlock` writes zeros. It keeps only
the f₀ / ratio trajectory bookkeeping (`heldF0_`, `prevSampleF0_` and their ratio twins), exactly as the core does
(:285–287), so a non-finite noteOn f₀ falls back to the same held value in both.

### C-8 (refinement, E-7 and spec-silent timing): latching, queueing and arm-time parameters

- `setControls` writes a pending copy. It is latched at **offset 0 of the next block**, after a pending noteOn is applied,
  so a Reset noteOn snaps to the pending Controls (FR-006).
- **E-7: a waveform switch during a running crossfade is queued** (latest wins) and starts when the running crossfade
  completes. No step, and no three-way blend.
- `punchBoostDb`, `punchTimeMs` and `ghostTimeMs` are read **at arm time**. A change mid-envelope applies at the next arm,
  so a running segment never changes length or height.
- A mode switch and a re-arm in the same block: the re-arm wins (it arms the new mode's trajectory).
- There is no standalone `rearm()` (FR-048 allows one). `noteOn(currentF0, true)` under FreeRunning is the same event. Phase 4
  may add `rearm()` append-only if Note Memory needs a re-arm without a pitch call.

### C-9 ⚠ RE-RULE (SC-012(b)): the 40 dB span bar is the exact analytic value of the level law

The span of E_sub between level 0.01 and 1 is 20·log₁₀(1/0.01f) = 40.0000000198 dB for a linear gain. The rendered ratio
carries float rounding of ~±6e-8 relative per sample product (≈ ±5e-7 dB). A correct implementation can therefore read
39.9999995 dB and fail. **Proposed:** gate the span at **≥ 40 dB − 1e-4 dB**, recorded as the measurement allowance under
FR-065, with the strict-monotonicity clause unchanged. The alternative, measuring between 0.0099 and 1, changes the spec's
sweep points instead.

### C-10 (refinement of FR-066 / SC-021): the "Phase 11 300–399 block" lives in Phase 10's text

The roadmap defines the 300–399 anchor block in **Phase 10** (line 534). Phase 11 (lines 547–559) registers parameters
without listing blocks. If SC-022 passes, the amendment adds `anchorFoldFeed` to **Phase 11's paragraph** ("…registered in
the 300–399 anchor block"), which is what SC-021 searches. Phase 10's block list is left alone, since the block already
names the anchor. If SC-022 fails, nothing is written (S13).

### C-11 (refinement of FR-054 / SC-015 wording): flush first, then clamp, and the read-back is per field

FR-054 says "a finite denormal in a Controls field is flushed to 0.0", and SC-015 says it "reads back as 0.0". Three Controls
fields have a lower bound above 0 (`punchTimeMs` ≥ 5, `ghostTimeMs` ≥ 1; `level` and `punchBoostDb` have 0). Under
either order the clamp then applies, so those two fields cannot read 0.0. **Ruling:** sanitise as
`isFinite(x) ? clamp(flushDenormal(x), lo, hi) : previous`, flush first and then clamp (S4.3). Flushing first means a denormal
is treated as the value 0.0 the user would have meant. The clamp then makes the field valid. **SC-015 read-back per field**,
through the new observer `pendingControls()` (C-4): `level` and `punchBoostDb` read back exactly 0.0f; `punchTimeMs` reads
back `kMinPunchTimeMs` (5.0f) and `ghostTimeMs` reads back `kMinGhostTimeMs` (1.0f). **Proposed SC-015 wording:** "A finite
denormal in a Controls field is flushed to 0.0 and then clamped: it reads back as 0.0 for `level` and `punchBoostDb` and as the
field's minimum for `punchTimeMs` and `ghostTimeMs`."

### C-12 (FR-065 wording refinement of SC-004(b), no bar change): the PUNCH return is measured as a bitwise 1.0

SC-004(b) says g "returns to 1 (within 0.01 dB) at t = `kPunchRiseMs` + `punchTimeMs` ± 1 sample". A raised-cosine fall is within
0.01 dB of 1 well before it ends: about 150 samples early for a 250 ms, +12 dB fall at 48 kHz. A "first sample within 0.01 dB"
search therefore reads early on a correct implementation. A search that starts at the predicted index cannot see an early
end at all. The design sets g to exactly 1.0 at the fall's end (S4.7 stores the end value literally). **Measurement:** the
first sample after the peak at which g == 1.0 **bitwise** is N_rise + N_time ± 1, and g[N_rise + N_time − 1] > 1. The whole
rise-and-fall trace also matches `anchorRaisedCosine` within 1e-9 absolute at every sample (S8.3), which catches a
shortened or reshaped fall anywhere. The "within 0.01 dB" clause becomes the Hold value after the return: |20·log₁₀ g| ≤
0.01 dB for every later sample (it is exactly 0 dB). **Proposed SC-004(b) wording:** "g reaches exactly 1.0 at t =
`kPunchRiseMs` + `punchTimeMs` ± 1 sample (the first sample after the peak with g == 1.0), stays within 0.01 dB of 1 after
it, and is > 1 + 0.1 dB at t = `kPunchRiseMs` + 0.5·`punchTimeMs`."

### C-13 (SC-024(d), no bar change): "no compensation" is structural in Phase 3

In Phase 3 `ProfundumCore` and `AnchorOscillator` are independent objects with no shared state, and the test performs the sum
itself. SC-024(d)'s memcmp and the `summed[i] == core[i] + anchor[i]` check therefore hold by construction. They are kept
as a documented structural assertion, not presented as a gate with teeth. The parts of SC-024 with teeth in Phase 3 are:
- **Static:** the Anchor API (S3) has no method or parameter that takes a `ProfundumCore` or an input buffer, and the header
  includes nothing from `systems/` (SC-019's layer grep). So the Anchor cannot compensate the core.
- **Rendered:** SC-024(b)'s summed-w₁ rise equals 20·log₁₀(1 + level·k) ± 0.25 dB, at level 1 and at `kDefaultLevel`. Any gain
  or normalisation stage in the sum would move it.

FR-025's real risk is auto-compensation inside the voice, which has no subject until Phase 9. S13 adds that obligation to
Phase 9's success criteria.

### C-14 (refinement of SC-012(a) measurement, no bar change): harmonics below the floor do not separate waveforms

Harmonics a waveform does not carry read as float rounding noise near −130 dB. Two noise floors differ by more than 6 dB
at random, so a raw dB difference passes every pair on noise, including two identical renders. **Measurement:** each
entry is clamped to the THD gate floor, max(10·log₁₀(wₙ/w₁), −90 dB), before differencing. A pair is separated only on a
harmonic where at least one of the two is above the floor. Sine and octave-down have identical harmonic shapes by
construction (FR-015: octave-down is a sine at f₀/2), so that one pair is separated by the spec's second clause ("octave-down's
fundamental is 1 octave below the others", measured as the w₁ peak at f₀/2 ± 1 cent and power at f₀ ≤ −90 dB re it). The
other nine pairs need the ≥ 6 dB harmonic. **Positive control:** two renders of the same waveform (sine vs sine, triangle vs
triangle) must be reported as *not separated*. **Proposed SC-012(a) wording addition:** "harmonic dB values are floored at
−90 dB re w₁; the sine/octave-down pair is separated by the octave clause."

---

## S2. Architecture

```
            noteOn(f0, rearm) / setFrequency / f0PerSample           followRatioPerSample / setFollowRatio
                         │  (the trajectory the voice selects per mode, FR-049)        │
                         ▼                                                              ▼
   ┌───────────────── U = 16 grid (restart on Reset noteOn, kept on FreeRunning) ──────────────────┐
   │ S5 coefficient mirror of ProfundumCore's h1:  chaseBase → setBase → bankEps(float) →          │
   │    shadow·m (float) → ε_eff = clamp(bankEps·shadow) → δ_lock = asin(ε_eff/2)/π  [cycles/sample] │
   │ r_eff = (mode == Follow) ? r : 1 ;  δ_target = min(r_eff·δ_lock, cap/fs) ; r step → 16-sample δ glide │
   └──────────────────────────────────────────────┬───────────────────────────────────────────────────┘
                                                  ▼
            φ₂ ∈ [0, 2) double, advanced by δ after each output sample (octave-down uses φ₂/2)
                                                  ▼
   waveform(s) at unit RMS (S4.5), crossfade weight c (S4.8), asym onset ramp (C-6)
                                                  ▼
   y = (kAnchorToH1/√2) · a₁(smoothed) · level(smoothed) · g(mode envelope, S4.7) · w_unit
                                                  ▼
                           left[n] = y ; right[n] = y   (overwrite; aliased buffer written once)
```

The Anchor has no audio input (FR-034, FR-052). One instance per voice (FR-051). It never includes `ProfundumCore`: S5
re-states the five pieces of the core's arithmetic it mirrors and cites each source line.

---

## S3. `AnchorOscillator` (Layer 3, NEW) — header and public API

**Header:** `dsp/include/krate/dsp/systems/anchor_oscillator.h`. The banner states "Layer 3: System Component — Profundum
Phase 3 (specs/profundum-phase3-anchor)" and "Includes Layer 0, Layer 1 and Layer 2 only".

```cpp
#pragma once
#include <krate/dsp/core/db_utils.h>                      // L0: detail::isFinite, detail::flushDenormal
#include <krate/dsp/core/math_constants.h>                // L0: kPi (the bank's float π, S5.1)
#include <krate/dsp/primitives/smoother.h>                // L1: OnePoleSmoother
#include <krate/dsp/processors/spectral_shape_recipe.h>   // L2: capFrequency, cap-taper constants, kCenterPanGain
#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>

namespace Krate::DSP {

/// @note Precondition (FR-057): the owner runs processBlock with FTZ/DAZ on (as ProfundumCore, :89–91).
class AnchorOscillator {
public:
    enum class Waveform : std::uint8_t { Sine, Triangle, RoundedSine, AsymmetricSine, OctaveDownSine };   // FR-003, append-only
    enum class Mode : std::uint8_t { Lock, Follow, Punch, Ghost };                                       // FR-003, append-only
    enum class RetriggerPhase : std::uint8_t { Reset, FreeRunning };   // D-6: same values as ProfundumCore::RetriggerPhase

    // ---- grid and range (mirroring ProfundumCore, profundum_core.h:97–105) ----
    static constexpr std::size_t kControlInterval = 32;
    static constexpr std::size_t kPitchUpdateInterval = 16;
    static constexpr float kMinF0Hz = 8.0f;
    static constexpr float kDefaultF0Hz = 55.0f;
    static constexpr double kMinSampleRate = 22050.0;
    static constexpr double kMaxSampleRate = 192000.0;

    // ---- calibration (FR-023, FR-024; S1 C-2) ----
    static constexpr float kAnchorToH1 = 1.0f;                          // k
    static constexpr float kMaxCoreH1Amplitude = 0.3552343859f;         // == HarmonicFold::kCalibrationPeak (test static_assert)
    static constexpr float kDefaultLevel = 0.5f;

    // ---- time constants (D-3, D-4, D-5, FR-045; all <= 10 ms where the spec bounds them) ----
    static constexpr float kLevelSmoothMs = 5.0f;          // FR-020 (OnePoleSmoother: 99.3 % at 5 ms)
    static constexpr float kWaveformCrossfadeMs = 5.0f;    // FR-014
    static constexpr float kPunchRiseMs = 1.0f;            // D-4
    static constexpr float kGhostDipMs = 2.0f;             // D-5
    static constexpr float kModeBridgeMs = 5.0f;           // FR-044
    static constexpr std::size_t kFollowGlideSamples = 16; // FR-045 (== kPitchUpdateInterval: a glide ends on the next grid point)
    static constexpr float kAsymOnsetPeriods = 1.0f;       // FR-016, S1 C-6

    // ---- Controls ranges (D-3, D-4) and FOLLOW ratio range (FR-045) ----
    static constexpr float kMinPunchBoostDb = 0.0f,  kMaxPunchBoostDb = 12.0f;
    static constexpr float kMinPunchTimeMs = 5.0f,   kMaxPunchTimeMs = 250.0f;
    static constexpr float kMinGhostTimeMs = 1.0f,   kMaxGhostTimeMs = 100.0f;
    static constexpr float kMinFollowRatio = 0.25f,  kMaxFollowRatio = 4.0f;

    // ---- waveform set (S1 C-1, FR-017) ----
    static constexpr std::size_t kTriangleTerms = 16;      // odd harmonics 1..31
    static constexpr float kRoundedSineA3 = 0.25f;         // FR-017
    static constexpr float kAsymmetricSineA2 = 0.25f;      // FR-017

    // ---- FR-055 output ceiling: (k·a1max/√2) · √3 · 10^(12/20) · 1.01 = 1.7493 at k = 1 ----
    static constexpr float kOutputCeiling = 1.75f * kAnchorToH1;
    static_assert(kOutputCeiling <= 2.0f, "FR-055: k must keep the ceiling at or below 2.0");

    /// FR-002: plain interpolable aggregate. Designated initialisers only.
    struct Controls {
        Waveform waveform = Waveform::Sine;
        Mode mode = Mode::Lock;
        float level = kDefaultLevel;   // [0, 1]
        float punchBoostDb = 6.0f;     // [0, 12]
        float punchTimeMs = 40.0f;     // [5, 250]
        float ghostTimeMs = 15.0f;     // [1, 100]
    };

    // TEST-ONLY diagnostic: not a Phase 8/9/11 contract, never serialised (S1 C-4; S8.3 MirrorTracksCore)
    struct MirrorState {
        float baseHz; float currentHz; float maskHz; float shadow; float bankEps; float epsEff;
    };

    AnchorOscillator() noexcept = default;   // copyable and movable (FR-001): every member is a trivially copyable value

    // ---- control thread ----
    void prepare(double sampleRate) noexcept;   // clamps to [22050, 192000], sizes, resets. Allocation-free.
    void reset() noexcept;                      // keeps pending Controls, follow ratio and a1; clears sounding

    // ---- audio thread (noexcept, allocation-free, no-ops before prepare) ----
    void setControls(const Controls& controls) noexcept;
    void setFrequency(float f0Hz) noexcept;                // held f0 (FR-005)
    void setFollowRatio(float r) noexcept;                 // held r (FR-045)
    void setCoreH1Amplitude(float a1) noexcept;            // FR-023, per control block
    void setRetriggerPhase(RetriggerPhase policy) noexcept;
    void noteOn(float f0Hz, bool rearm = true) noexcept;   // effective at offset 0 of the next block (FR-005, FR-048)
    void processBlock(float* left, float* right, std::size_t numSamples,
                      const float* f0PerSample = nullptr,
                      const float* followRatioPerSample = nullptr) noexcept;

    // ---- observers (FR-008, S1 C-4) ----
    [[nodiscard]] double modeGain() const noexcept;        // g at the last rendered sample
    [[nodiscard]] double phase() const noexcept;           // fundamental phase in cycles [0, 1)
    [[nodiscard]] double phaseIncrement() const noexcept;  // δ applied after the last rendered sample (cycles)
    [[nodiscard]] bool isSounding() const noexcept;        // C-4: C-7 / SC-014, SC-027(e)
    [[nodiscard]] Controls pendingControls() const noexcept; // C-4, C-11: the sanitised pending copy (SC-015)
    [[nodiscard]] bool stateFinite() const noexcept;
    [[nodiscard]] MirrorState mirrorState() const noexcept;   // TEST-ONLY diagnostic (C-4)
    [[nodiscard]] static constexpr std::size_t latencySamples() noexcept { return 0; }   // FR-033

private:
    static constexpr double kPiDouble = 3.14159265358979323846;   // == ProfundumCore::kPiDouble (profundum_core.h:813)
    static constexpr double kTwoPiD = 2.0 * kPiDouble;            // S4.5 angle reduction
    // ... the state of S4.1
};

}  // namespace Krate::DSP
```

Bodies are inline in the header. Private helpers are static or member functions. No namespace-scope helper is added, so
there is nothing new to ODR-sweep beyond the class name.

---

## S4. `AnchorOscillator` internals

### S4.1 State layout (fixed-size, ≈ 0.5 KB; no static non-const data member, SC-013)

| Group | Members | Notes |
|---|---|---|
| Constants | `kPiDouble`, `kTwoPiD` (private `static constexpr double`, S3) | |
| Lifecycle | `bool prepared_`, `double sampleRate_`, `float capHz_`, `float invFsF_` (= `1.0f / static_cast<float>(sampleRate_)`, the bank's :135 expression), `float taperStartHz_`, `float tailFloor_` | set in `prepare` |
| Controls | `Controls pending_`, `Controls active_`, `bool controlsDirty_` | latch at block start (C-8) |
| Note | `RetriggerPhase policy_`, `bool sounding_`, `bool noteOnPending_`, `float noteOnF0_`, `bool noteOnRearm_` | |
| Pitch trajectory | `float heldF0_`, `float prevSampleF0_`, `float heldRatio_`, `float prevSampleRatio_`, `std::uint32_t intervalPhase_` | core's FR-062 semantics, twice |
| Mirror (S5) | `float mBase_`, `double mEpsBase_`, `float mBankEps_`, `float mShadow_`, `float mCurrentF_`, `float mMaskF_`, `float mEpsEff_`, `double deltaLock_` | n = 1 only |
| Frequency | `double phi2_` ∈ [0, 2), `double delta_` (in force), `double deltaFrom_`, `double deltaTo_`, `std::uint32_t glideLeft_`, `float rEffPrev_`, `bool snapRatio_` | |
| Waveform | `Waveform current_`, `Waveform next_`, `bool xfadeActive_`, `std::uint32_t xfadeN_`, `xfadeLen_`, `bool queued_`, `Waveform queuedWave_`; `std::array<double, kTriangleTerms> triCoef_` (tapered), `std::size_t triActive_`, `float a3Eff_`, `float a2Eff_`, `float taperFr_` (f_T, the clamped effective fundamental at the last taper evaluation, S4.4) | |
| Onset ramp | `std::uint32_t onsetN_`, `onsetLen_` (0 = inactive) | asym only |
| Mode envelope (S4.7) | `std::array<Segment, 3> seg_` (a small program), `std::size_t segIdx_`, `std::size_t segCount_`, `double g_`, `double gSlope_` (per sample) | |
| Gain | `OnePoleSmoother levelS_`, `OnePoleSmoother a1S_`, `float a1Held_` | |

`struct Segment { enum class Kind : std::uint8_t { Hold, Cosine, Hermite } kind; double g0, a, m0, d; std::uint32_t n, len;
double rc, rs, rcStep, rsStep; }`. It is nested and private. Cosine: g = g0 + a·½(1 − cos(π·n/len)), with cos/sin advanced
by a double rotation recurrence (rc, rs) ← (rc·cδ − rs·sδ, rs·cδ + rc·sδ), cδ = cos(π/len), sδ = sin(π/len), evaluated once
at segment start with `std::cos`/`std::sin` in double. Hermite: g(t) = h00(t)·g0 + h10(t)·m0 + h01(t)·(g0 + d), t = n/len.

### S4.2 Lifecycle

- **`prepare(sr)`**: `sampleRate_ = clamp(sr, 22050, 192000)`; `capHz_ = SpectralShapeRecipe::capFrequency(sampleRate_)`;
  `invFsF_ = 1.0f / static_cast<float>(sampleRate_)`; `taperStartHz_ = capHz_·exp2(−kCapTaperCents/1200)` and
  `tailFloor_ = pow(10, −kCapTaperDbPerCent·(kCapTaperCents − kCapTailCents)/20)`, both in float as `evaluateMask` computes
  them (`spectral_shape_recipe.h:161–163`). `levelS_.configure(kLevelSmoothMs, fs)`, `a1S_.configure(kLevelSmoothMs, fs)`. Then `reset()` with the
  pending Controls defaulted (first prepare) or kept (re-prepare). Allocation-free. Documented control-thread (FR-004).
- **`reset()`**: `sounding_ = false`, `noteOnPending_ = false`, `heldF0_ = prevSampleF0_ = kDefaultF0Hz`,
  `heldRatio_ = prevSampleRatio_ = 1`, `intervalPhase_ = 0`, `phi2_ = 0`, `delta_ = 0`, envelope Hold(1), crossfade cleared,
  `active_ = pending_`, both smoothers snapped to their targets (level = `pending_.level`, a1 = `a1Held_`, whose first value is
  `kMaxCoreH1Amplitude`). Pending Controls, held ratio and held a₁ are kept (FR-004).
- **Before `prepare`:** every setter returns; `processBlock` zero-fills both non-null buffers (FR-004).

### S4.3 Setters and sanitising (FR-054)

- `setControls(c)`: each float field `x` → `detail::isFinite(x) ? clamp(flushDenormal(x), lo, hi) : pending_.field` (flush first, then
  clamp, S1 C-11). An
  enum outside its enumerator range keeps the pending value: a `static_cast<std::uint8_t>` compare against the last
  enumerator. `controlsDirty_` is set on any bitwise change (`std::bit_cast`, as `ProfundumCore::sameBits`).
- `setFrequency(f)`: `heldF0_ = sanitizeF0(f, heldF0_)`, i.e. non-finite → previous, else clamp to [`kMinF0Hz`, `capHz_`]
  (the core's :795–800 verbatim).
- `setFollowRatio(r)`: non-finite → previous, else `clamp(r, 0.25f, 4.0f)` → `heldRatio_`.
- `setCoreH1Amplitude(a)`: non-finite → previous, else `clamp(a, 0, kMaxCoreH1Amplitude)` → `a1Held_`,
  `a1S_.setTarget(a1Held_)`.
- `noteOn(f, rearm)`: `noteOnF0_ = sanitizeF0(f, heldF0_)`, `noteOnRearm_ = rearm`, `noteOnPending_ = true`. The last call
  before a block wins (as the core, :204–209).

### S4.4 `processBlock` contract (normative order)

```
if (left == nullptr || right == nullptr || numSamples == 0) return;                // FR-007, no state change
if (!prepared_) { zero-fill; return; }                                              // FR-004
if (noteOnPending_) applyNoteOn();                                                  // S4.6, offset 0
else if (controlsDirty_) latchControls();                                           // C-8 (applyNoteOn latches itself)
if (!sounding_) { trajectory bookkeeping only (C-7); zero-fill; return; }
float trajF = prevSampleF0_, trajR = prevSampleRatio_; size_t resolvedF = 0, resolvedR = 0;
for (i = 0; i < numSamples; ++i) {
    if (intervalPhase_ % kPitchUpdateInterval == 0) {
        float f = heldF0_;  if (f0PerSample)          { trajF = trajectoryF0(f0PerSample, resolvedF, i, trajF); resolvedF = i + 1; f = trajF; }
        float r = heldRatio_; if (followRatioPerSample) { trajR = trajectoryRatio(followRatioPerSample, resolvedR, i, trajR); resolvedR = i + 1; r = trajR; }
        if (intervalPhase_ == 0) mirrorControlUpdate(f); else mirrorPitchUpdate(f);   // S5
        frequencyUpdate(r);                                                          // S4.10: δ_tgt (capped), glide
        const double dT = glideLeft_ > 0 ? std::max(deltaFrom_, deltaTo_) : delta_;  // the larger δ in force until the next grid
        const float fT = static_cast<float>(dT * sampleRate_);                       // f_T: clamped effective fundamental
        if (!sameBits(fT, taperFr_)) { refreshTaper(fT); taperFr_ = fT; }           // S4.5: keyed on f_T, not on f
    }
    const double w = waveformSample();                                              // S4.5 + S4.8 + C-6
    const double g = envelopeStep();                                                // S4.7 (value at n, then advance)
    const float  y = static_cast<float>(gainScale_ · levelS_.process() · a1S_.process() · g · w);
    left[i] = y;  if (right != left) right[i] = y;                                  // FR-050: one value, both channels
    advancePhase();                                                                 // φ₂ += δ (glide step if active); wrap at 2
    intervalPhase_ = (intervalPhase_ + 1) % kControlInterval;
}
end-of-block trajectory bookkeeping, as the core :285–287 (f0 and ratio)
```

**Taper key.** The taper is keyed on f_T, the frequency the waveform runs at, not on the mirror's f. So a change of r alone
(`setFollowRatio`, a `followRatioPerSample` step), a LOCK ↔ FOLLOW switch (r_eff changes, f does not) and a change of f all
refresh it at the grid sample where they take effect. f_T comes from δ after `frequencyUpdate`, so it is already capped at
`capHz_` (S4.10): above the cap the waveform keeps the cap's weights instead of losing its h1 weight. During the 16-sample
r glide, f_T is the larger of the two endpoints, so no harmonic of either endpoint passes the cap while the glide runs. A
static note at a static r refreshes once. A pitch trajectory above the fast-path threshold refreshes at every grid sample
(S4.5 bounds the cost and SC-018 gates it).

Here `gainScale_ = kAnchorToH1 · 0.70710678f` (k/√2, so a unit-RMS waveform gives RMS k·a₁/√2 at level 1, FR-011). The
gain product is formed in double and rounded once. The order *update at the grid sample, output, then advance* is the
core's: its pitch update precedes the run whose first sample is s_j, and the advance from j to j + 1 uses the new ε
(`profundum_core.h:251–266`; bank output-then-advance, `harmonic_oscillator_bank.h:743–753`, SIMD `:94–105`).

### S4.5 Waveforms (D-2 as ratified by Q5, FR-010–FR-017, S1 C-1)

Per sample: φ₁ = φ₂ ≥ 1 ? φ₂ − 1 : φ₂. The reduced angle is
`a = static_cast<float>(kTwoPiD · (φ₁ ≥ 0.5 ? φ₁ − 1.0 : φ₁))` ∈ [−π, π), and **u = std::sin(a)** in float, the only
transcendental for every waveform except the octave-down. Using −π…π keeps the float angle ulp ≤ 2.4e-7 rad. The phase
error does not accumulate because φ₂ is double.

| Waveform | Unnormalised w(u) | Unit-RMS scale | Notes |
|---|---|---|---|
| Sine | u | √2 | |
| Triangle | Clenshaw: c = 2 − 4u² (= 2·cos 2x); b_k = a_k + c·b_{k+1} − b_{k+2} for k = K_a−1…0; w = u·(b₀ − b₁), in double | 1/√(Σ_{k<16} a_k²/2) with a_k = 8/(π²(2k+1)²) (untapered) | a_k = (−1)ᵏ·8/(π²(2k+1)²)·T((2k+1)·f). u = 0 gives exactly 0 (FR-016) |
| Rounded sine | u + a₃ₑ·(3u − 4u³) (sin 3x = 3u − 4u³) | 1/√0.53125 | a₃ₑ = 0.25·T(3f) |
| Asymmetric sine | u − a₂ₑ·r(n)·(1 − 2u²) (cos 2x = 1 − 2u²) | 1/√0.53125 | a₂ₑ = 0.25·T(2f); r(n) the C-6 onset ramp, else 1 |
| Octave-down | v = std::sin(static_cast<float>(π·(φ₂ ≥ 1 ? φ₂ − 2 : φ₂))) | √2 | fundamental at f/2; sin(φ/2) phase (FR-015) |

In the table above and below, the taper argument f is f_T (S4.4), the clamped frequency the waveform runs at.

**T(nf), the cap taper (FR-012).** This is the per-harmonic cap branch of `SpectralShapeRecipe::evaluateMask`
(:168–180), re-stated **without the low-note guard** so the Anchor's harmonic ratios hold at E0:
`nf >= capHz_ → 0`; `nf > taperStartHz_` → y = 1200·log₂(capHz_/nf), and `y > kCapTailCents` gives
`pow(10, −kCapTaperDbPerCent·(kCapTaperCents − y)/20)`, else `tailFloor_·max(0, y)/kCapTailCents`; otherwise 1.
`refreshTaper(f_T)` runs when f_T changed bitwise at a grid sample (S4.4):
- `K_a = min(16, number of odd n with n·f_T < capHz_)`.
- Fast path: if 31·f_T ≤ `taperStartHz_`, every weight is 1 and the coefficients are a precomputed constant array. That
  covers f_T ≤ 249 Hz at 44.1 kHz, ≤ 271 Hz at 48 kHz and ≤ 542 Hz at 96 kHz.
- Otherwise only the terms in the taper band call `log2` and `pow`. The band spans 2^(1430/1200) = 2.284× in frequency, so
  at most **9** odd n ≤ 31 lie in it at once (n = 15…31 at the worst f_T), plus T(2f_T) and T(3f_T) when those lie in it.
  **Worst case: ≤ 11 `log2` + `pow` pairs per grid sample**, i.e. one refresh per 16 samples under a moving f_T. This is not
  rare work under a vibrato above the threshold, and SC-018's taper-band cells measure it (S8.3).

The unit-RMS scale is not re-derived as harmonics taper out. Near the cap the waveform's RMS falls by at most −0.06 dB (h1
carries 99.3 % of the triangle's power). FR-011 is gated at E1 and C3, where no taper is active.

**Exactness.** The rounded and asymmetric sines are polynomials in u, so their harmonic content is exactly {h1, h3} and
{h1, h2} (SC-001(c)(d) to rounding). The triangle is odd in u and half-wave antisymmetric, so even harmonics come only from
float rounding of the angle reduction (< −130 dB). All are zero-mean (FR-013).

### S4.6 Note-on (offset 0; FR-006, FR-048)

```
applyNoteOn():
  f = noteOnF0_; heldF0_ = f; prevSampleF0_ = f; prevSampleRatio_ = heldRatio_;
  const bool fromSilence = !sounding_;
  const bool rearm = noteOnRearm_ || fromSilence;                         // FR-048 (e): from not sounding always arms
  latchControls(/*snap=*/policy_ == Reset || fromSilence);                // C-8
  if (policy_ == Reset || fromSilence) {                                  // Reset branch (mirrors core :528–557)
      phi2_ = 0; intervalPhase_ = 0;
      mirrorResetNoteOn(f);                                               // S5.2
      snapRatio_ = true;                                                  // FR-006: the r glide snaps
      levelS_.snapToTarget(); a1S_.snapToTarget();
      crossfade completes: current_ = active_.waveform; xfadeActive_ = queued_ = false;
      onset ramp: if current_ == AsymmetricSine → onsetLen_ = clamp(round(fs/f), 1, 4·fs), onsetN_ = 0 (C-6)
      if (rearm) snapArm(active_.mode);                                    // S4.7
  } else {                                                                // FreeRunning while sounding
      mirrorFreeRunningNoteOn(f);                                         // S5.3
      if (rearm) bridgedArm(active_.mode);                                 // S4.7
  }
  sounding_ = true; noteOnPending_ = false;   // the taper is refreshed by the S4.4 f_T check at the next grid sample
                                             // (Reset: sample 0 of this block; FreeRunning: the f_T of the new δ)
```

`rearm == false` never touches the envelope state (bit-identical g trace, SC-027(d)). The phase branch never reads `rearm`,
so the `phase()` trace does not depend on it (SC-027(d)).

### S4.7 Mode envelope g (D-4, D-5, FR-041–FR-044; S1 C-3)

**Segment programs** (lengths in samples: N = max(1, round(ms·fs/1000))):

| Event | Program |
|---|---|
| Snap arm, LOCK / FOLLOW | Hold(1) |
| Snap arm, PUNCH (B = 10^(boost/20)) | g := 1, then Cosine(g0 = 1, a = B − 1, N_rise), Cosine(g0 = B, a = 1 − B, N_time), Hold(1) |
| Snap arm, GHOST | g := 0, then Cosine(g0 = 0, a = 1, N_ghost), Hold(1) |
| Bridged arm, PUNCH | Hermite(g, g′ → B, N_b ≤ N_rise), Cosine(B → 1, N_time), Hold(1) |
| Bridged arm, GHOST | Hermite(g, g′ → 0, N_b ≤ N_dip), Cosine(0 → 1, N_ghost), Hold(1) |
| Bridged arm, LOCK / FOLLOW, or a mode switch (FR-044) | Hermite(g, g′ → 1, N_b ≤ N_modeBridge), Hold(1); nothing if g = 1 and g′ = 0 |

`envelopeStep()` returns g at the current sample, then advances n. When n reaches len, the value is set **exactly** to
the segment's stored end value, so the recurrence's ~1e-12 drift never leaves a residue, and the next segment starts. The
end value is stored when the segment is built (g0 + a or g0 + d). Every segment that returns to 1 (the PUNCH fall, the GHOST
emergence, a LOCK/FOLLOW bridge) stores the literal 1.0, so g == 1.0 bitwise at its end (C-12).
`gSlope_` holds the analytic per-sample derivative at the current sample:
- Cosine: a·π/(2·len)·sin(π·n/len), using the recurrence's rs.
- Hermite: (h00′·g0 + h10′·m0 + h01′·g1)/len.

A bridge leaves the exact analytic (g, g′).

**Hermite bridge (S1 C-3).** With g0, g′₀ (per sample), target g1, Δ = g1 − g0, and length N, the tangent is m₀ = N·g′₀. The
interior extrema are the roots in (0, 1) of g′(t) ∝ (3m₀ − 6Δ)t² + (6Δ − 4m₀)t + m₀. `fits(N)` is true iff the cubic at each
root lies in [lo, hi] = [0, max(B_current, g0)] (B_current from the active Controls; B ≥ 1, so the bound also contains 1).
N_b = N_nom if `fits(N_nom)`; else the largest N in [1, N_nom) with `fits(N)`, found by integer bisection. Excursion is
monotone in m₀, so `fits` is monotone in N. Peak \|Δ²g\| = max(\|6Δ − 4m₀\|, \|6Δ − 2m₀\|)/N_b², SC-006(a)'s reference. If
Δ = 0 and g′₀ = 0 there is no bridge.

**Curve bounds.** Every Cosine is between its two end values. Every Hermite stays in [lo, hi] by construction. So
g ∈ [0, max(B, g₀)] ⊆ [0, 10^(12/20)] (FR-041, FR-055).

**Timing (SC-004(b), SC-005(b)).** The PUNCH return reaches 1 at N_rise + N_time samples after a snap arm. Each N is rounded
to the nearest sample, which is within ±1 sample of `kPunchRiseMs + punchTimeMs`. g(0.1·T) of a GHOST emergence =
½(1 − cos 0.1π) = 0.0245 (−32.2 dB ≤ −30 dB).

### S4.8 Waveform crossfade (FR-014; C-8)

A `waveform` change while sounding starts a crossfade from `current_` to `next_` over N_x = round(5 ms·fs). The crossfade
weight is c(n) = ½(1 − cos(π·n/N_x)), computed with the same rotation recurrence as S4.7. Output is w = w_old + c·(w_new − w_old)
(equal gain, both on the same φ, so the fundamentals are coherent). The peak is ≤ max(peak_old, peak_new). Δc_max = π/(2N_x) is
SC-006(c)'s documented S_max. A switch arriving mid-crossfade is queued (C-8). A Reset noteOn completes it instantly (FR-006).

### S4.9 Level, a₁ and the ceiling (FR-020–FR-025, FR-055)

- `levelS_` and `a1S_` are both `OnePoleSmoother` at `kLevelSmoothMs` (99.3 % in 5 ms). Both snap exactly once within 1e-4
  of the target (smoother.h:197–211), so level 0 gives an exact 0.0 output (FR-021, E-13). 99.3 % at 5 ms does not mean
  exact zero at 5 ms. The coefficient exp(−5000/(ms·fs)) (smoother.h:77–92) is a time constant of `kLevelSmoothMs`/5 = 1 ms,
  and the snap needs |current − target| < `kCompletionThreshold` = 1e-4 (smoother.h:55). A step of size Δ therefore reaches
  exact 0.0 after ln(Δ/1e-4) time constants: ≈ 8.2 ms for a₁ from `kMaxCoreH1Amplitude` to 0, and ≈ 9.2 ms for level 1 → 0. The first step of a level change
  ΔL is (1 − e^(−5000/(5·fs)))·ΔL, SC-006(c)'s S_max for the level smoother.
- The gain is k/√2 · a₁ · level · g. With a₁ ≤ 0.35523, level ≤ 1, g ≤ 3.981 and crest ≤ √3, \|y\| ≤ 1.7320·k·1.0 < 1.75·k
  (`kOutputCeiling`). The Anchor never touches the core's buffer (FR-025).
- **Source of a₁ in Phase 9 (FR-023):** `core.deliveredGains()[0] * SpectralShapeRecipe::kCenterPanGain`, read once per host
  block after the core's `processBlock` (`profundum_core.h:294`, recipe :51). This is the recipe's h1 at the core's output
  normalisation. The bank's 2 ms amplitude smoothing delays it by < 1 block. The per-channel peak equals it because the bank's
  `mcfCorrection = cos(πf/fs)` cancels the MCF amplitude 1/cos(θ/2) (`harmonic_oscillator_bank.h:1088–1099`). No shared
  header changes. SC-024(a) checks this identity against the rendered core.

### S4.10 FOLLOW ratio and glide (FR-045, FR-049)

At each grid sample, after the mirror has produced δ_lock:
```
r_eff = (active_.mode == Mode::Follow) ? r : 1.0f;
δ_tgt = min(static_cast<double>(r_eff) · δ_lock, capHz_ / sampleRate_);
if (snapRatio_ || sameBits(r_eff, rEffPrev_)) { delta_ = δ_tgt; glideLeft_ = 0; snapRatio_ = false; }
else { deltaFrom_ = delta_; deltaTo_ = δ_tgt; glideLeft_ = kFollowGlideSamples; }
rEffPrev_ = r_eff;
```
`advancePhase()` with a glide uses δ = deltaFrom_ + (deltaTo_ − deltaFrom_)·(k + 1)/16 for the k-th sample of the glide. The
glide ends exactly at the next grid sample, so a step in r is complete within ≤ 16 + 16 = 32 samples (SC-008(b)), and f_inst
is monotone between the old and new values (no overshoot). With r ≡ 1 (or any non-FOLLOW mode), δ = 1.0·δ_lock exactly, so
FOLLOW and LOCK are bit-identical (SC-008(c)). A mode switch to or from FOLLOW changes r_eff and therefore glides (FR-044).
δ_lock = asin(ε/2)/π is exactly the core's per-sample rotation in cycles, so r·δ_lock is the frequency r·f
(2·asin(sin(πf/fs))/2π = f/fs).

### S4.11 Observers

- `modeGain()` = `g_` (double).
- `phase()` = `phi2_ ≥ 1 ? phi2_ − 1 : phi2_`.
- `phaseIncrement()` = the δ applied after the last rendered sample.
- `stateFinite()` = `isFinite` on `phi2_`, `delta_`, `deltaFrom_`, `deltaTo_`, `g_`, `gSlope_`, the segment doubles, both
  smoothers' current values, `a1Held_`, `heldF0_`, `prevSampleF0_`, `heldRatio_`, `prevSampleRatio_` and every mirror scalar.
- `mirrorState()` copies the S5 scalars.

---

## S5. Phase-lock mirror (D-1, FR-030–FR-036; S1 C-5)

The Anchor reproduces, for partial n = 1 only, the arithmetic by which `ProfundumCore` sets h1's effective MCF coefficient. It
receives the same `noteOn` / `setFrequency` / `f0PerSample` / `setRetriggerPhase` calls (FR-030) and samples the same grid.
Every expression below keeps **the operand order and types of its source line**, so that in the same build it evaluates to
the same bits.

### S5.1 Primitives

```
bankEps(b)  = clamp(2.0f * std::sin(kPi * b * invFsF_), -1.99f, 1.99f)      // bank :1063–1065 (n = 1, deviation 0: freq = b exactly)
epsBase(b)  = clamp(2.0 * std::sin(1.0 * (kPiDouble * double(b) / sampleRate_)), -1.99, 1.99)   // core setBase :692–696, k = 0
epsStar(f)  = double(1)*f >= double(capHz_) ? 2·sin(kPiDouble·double(capHz_)/sampleRate_) : 2·std::sin(kPiDouble·double(f)/sampleRate_)   // :704–720, k = 0
refresh(f): dT = |mEpsBase_| > 1e-9 ? float(epsStar(f) / mEpsBase_) : 1.0f;   m = dT / mShadow_;   mShadow_ = mShadow_ * m;   // :722–738
            mEpsEff_ = clamp(mBankEps_ * mShadow_, -1.99f, 1.99f);   deltaLock_ = std::asin(0.5 * double(mEpsEff_)) / kPiDouble;  // bank :732 / simd :102
chase(f)    = ProfundumCore::chaseBase restated (:666–687): double ratio = exp2(0.9/12), float bound rounded toward mBase_ with nextafter
```

`kPiDouble` is restated as the core's literal 3.14159265358979323846 (:813). The bank's `kPi` is
`Krate::DSP::kPi` (math_constants.h:28), included directly.

### S5.2 Reset noteOn (core Reset branch :528–557)

`mShadow_ = 1.0f`; `mBase_ = f`; `mEpsBase_ = epsBase(f)`; `mBankEps_ = bankEps(f)` (the first-frame and the reload
expressions agree for h1 below the cap); `refresh(f)`; `mCurrentF_ = f`; `mMaskF_ = f`. The core's first grid sample then
runs `controlUpdate(f)`, and so does the mirror (S5.3). With an unchanged f it does nothing in either.

### S5.3 Grid updates and FreeRunning noteOn

```
mirrorControlUpdate(f):          // core controlUpdate :448–507, n = 1, shapeChanged assumed false (C-5 residual)
  fChanged = !sameBits(f, mCurrentF_); mCurrentF_ = f;
  baseNew = chase(f);            // sounding_ is true whenever the mirror runs (C-7)
  maskChanged = !sameBits(f, mMaskF_); if (maskChanged) mMaskF_ = f;
  baseChanged = !sameBits(baseNew, mBase_);
  if (maskChanged || baseChanged) { if (baseChanged) { mBase_ = baseNew; mEpsBase_ = epsBase(baseNew); mBankEps_ = bankEps(baseNew); } refresh(f); }
  else if (fChanged) refresh(f);
mirrorPitchUpdate(f):            // :511–517
  if (sameBits(f, mCurrentF_)) return; mCurrentF_ = f; refresh(f);
mirrorFreeRunningNoteOn(f):      // :560–600
  fChanged = !sameBits(f, mCurrentF_); mCurrentF_ = f;
  maskChanged = !sameBits(f, mMaskF_); if (maskChanged) mMaskF_ = f;
  baseChanged = !sameBits(f, mBase_);                          // no chase clamp (E-7 of Phase 1)
  if (maskChanged || baseChanged) { if (baseChanged) { mBase_ = f; mEpsBase_ = epsBase(f); mBankEps_ = bankEps(f); } refresh(f); }
  else if (fChanged) refresh(f);
```

A > 1-semitone jump makes the bank crossfade **levels** (`harmonic_oscillator_bank.h:262–269`), not phases, so the mirror
needs nothing for it (E-11).

### S5.4 Precision argument (E-10) and the FR-032 fallback

- **φ₂ precision:** double in [0, 2), ulp 4.4e-16 cycles. Even a fully biased rounding over 1 h at 192 kHz (6.9e8 samples)
  is ≤ 1.5e-7 cycles (5.5e-5°); the realistic random walk is ~6e-12 cycles. The per-sample δ is a value, not an
  accumulator, so it does not drift.
- **Lock budget (S9.1, S9.2):** mirror 0°, one-ulp ε disagreement ≤ 0.31° / 60 s at C3, core MCF state walk ≤ 2.6e-3°,
  against SC-003's 0.5°.
- **FR-032 fallback (invoked only on a red SC-003 on any CI ISA; not built otherwise):**
  - append to `HarmonicOscillatorBank`: `[[nodiscard]] float effectiveEpsilon(std::size_t i) const noexcept { return std::clamp(epsilon_[i] * detuneMultiplier_[i], -1.99f, 1.99f); }`;
  - append to `ProfundumCore`: `[[nodiscard]] float h1EffectiveEpsilon() const noexcept { return bank_.effectiveEpsilon(0); }`;
  - the Anchor gains an optional `const float* h1EpsilonPerGrid` input, with the voice reading the core per grid interval;
  - FR-063 then requires the Innexus, Ruinae, Seraphis and Vorago suites green, and SC-019 records the append-only diff.

---

## S6. Algorithm choices — why

| Choice | Alternatives rejected (evidence) |
|---|---|
| Coefficient mirror + own double phase (S5) | Double law alone: 0.17° worst, but not a guarantee (S9.1). Phase observer on the bank: none exists (spec D-1). `PhaseAccumulator`: wraps at 1, and its `setFrequency` is a plain f/fs without the bank's float rotation. `SubOscillator`: needs master wrap flags and hard-resets phase (`sub_oscillator.h:273–276`) |
| One float `sin` per sample + polynomials in u | `std::sin` per harmonic (5 sin/sample for the rounded and asymmetric sines). A float MCF for the Anchor: it would need its own phase-reset and convention logic and could not give a double `phase()` |
| Clenshaw over 16 odd terms (S1 C-1) | polyBLAMP closed form (`core/polyblep.h:198`): sinc² roll-off leaves components between the cap and Nyquist (FR-012 bullet 1) and cannot taper continuously through the cap region (E-2). Mipmapped wavetable (`primitives/wavetable_generator.h`): ≈ 90 KB per instance (11 levels × 2052 floats), and level switches need their own crossfade logic. 32 terms: 0.27 % CPU (S9.8) |
| Raised cosine + cubic Hermite bridge, rotation recurrence | `std::cos` per sample (≈ 10 ns, 16 % of the budget). A one-pole envelope is not C¹ at its start. A linear ramp is SC-006(a)'s negative control |
| `OnePoleSmoother` for level and a₁ | `LinearRamp`: a constant-rate ramp is a slope step at both ends (clicks under SC-006(b)'s detector are unlikely, but its first step is larger). The one-pole's exact snap gives FR-021 for free |
| No DC blocker on the Anchor path | Every waveform is zero-mean by construction (FR-013), and a DC blocker would rotate an E0 fundamental by degrees (FR-013, SC-003) |
| r applied as a multiplier on δ_lock | Running a second mirror on r·f: bit-identity to LOCK at r = 1 would then depend on two chains agreeing |

---

## S7. RT safety, determinism, portability

- **RT (FR-053):** no allocation anywhere, including `prepare`. No locks, exceptions or I/O. Every audio-thread method is
  `noexcept` (`static_assert(noexcept(...))` in SC-014). The bridge bisection is bounded (≤ 1 + ⌈log₂ N_nom⌉ ≤ 11 closed-form
  evaluations at 192 kHz, at events only; C-3). `refreshTaper` runs when f_T changes (S4.4): at most once per 16 samples,
  with ≤ 11 `log2` + `pow` pairs (S4.5), and SC-018 gates that worst case.
- **Denormals (FR-057):** the envelope recurrences are bounded away from denormals (g ∈ [0, 4], rc/rs unit circle). The
  smoothers flush (smoother.h:209). The owner's FTZ/DAZ covers the rest. The `-ffast-math` NaN rule holds: no `std::isnan`;
  every guard is `detail::isFinite`.
- **Determinism (FR-056):** no RNG. All per-sample state advances once per sample, independent of block boundaries. Setters
  take effect at the next block's offset 0. SC-016's partition test includes a pending envelope and a vibrato.
- **Mono (FR-050):** one float y per sample written to both channels. There is no per-channel state.
- **Portability (FR-058):**
  - designated initialisers for `Controls` and `Segment`, and no narrowing (every float literal carries `f`);
  - `std::size_t` / `std::uint32_t` counters with explicit casts (MSVC C4267);
  - no SIMD in the header;
  - `std::bit_cast` comparisons as the core;
  - no `<windows.h>`-style names; the nested names avoid macOS globals (`lint-apple-globals.js`: no `Rect`, `MAC`, `Point`
    at namespace scope).

  Both `node tools/check-portability.js` and `node tools/lint-apple-globals.js` run in comply (SC-023).
- **Fast-math and the mirror:** the mirror's float expressions are written in the bank's operand order. A reassociation that
  differs between the two TUs' inlined copies costs ≤ 1 ulp, which S9.1 shows passes. The test TUs that gate SC-003 stay in
  the shipping FP mode (fast-math), not the `-fno-fast-math` block, so they prove the lock in the mode the header ships in.

---

## S8. Test plan

### S8.1 Files, targets, tags

All TUs go in **`dsp_systems_tests`** (Layer 3). Each is enumerated in `dsp/tests/CMakeLists.txt` after the Phase 2 block
(:625–630), with a comment block in the Phase 1/2 style.

| TU | Holds | Tags |
|---|---|---|
| `unit/systems/anchor_oscillator_test_helpers.h` | S8.2 helpers (`inline`, namespace `Krate::DSP::ProfundumTest`) | — |
| `unit/systems/anchor_oscillator_test.cpp` | Interface, lifecycle, envelopes, retrigger, determinism, RT: SC-004, 005, 006, 007 (helper), 013, 014, 016, 017, 026, 027; enum `static_assert`s; `MirrorTracksCore` | `[systems][profundum][anchor]` |
| `unit/systems/anchor_oscillator_spectral_test.cpp` | SC-001, 002, 008(a)(b)(c), 010, 011, 012, 025 | `[systems][profundum][anchor]` |
| `unit/systems/anchor_oscillator_nonfinite_test.cpp` | SC-015; added to the `-fno-fast-math` block (ending :1114) | `[systems][profundum][anchor]` |
| `unit/systems/profundum_anchor_chain_test.cpp` | SC-003 (all arms), SC-008(r), SC-009, SC-022, SC-024 | `[systems][profundum][anchor-chain]`; 60 s cases `[long]` |
| `unit/systems/anchor_oscillator_perf_test.cpp` | SC-018 | `[systems][profundum][anchor][.perf]` |

FTZ/DAZ comes from the test main (`tests/test_helpers/enable_ftz_daz.h:26–28`, `enableFTZDAZ()`). One anchor TU
(`anchor_oscillator_test.cpp`) asserts `_MM_GET_FLUSH_ZERO_MODE() == _MM_FLUSH_ZERO_ON` under
`KRATE_HAS_SSE_DENORMAL_CONTROL` (included from `core/scoped_denormal_mode.h` for its feature macro), as
`profundum_core_test.cpp:830–833` does. No fixture constructs a `ScopedDenormalMode`, so the FR-057 precondition is checked
by the same mechanism the tests run under.

### S8.2 Helpers (`anchor_oscillator_test_helpers.h`)

```cpp
struct AnchorRender { std::vector<float> L, R; std::vector<double> g, phase; };   // g/phase filled only when perSample
AnchorRender renderAnchor(AnchorOscillator& a, std::size_t n, std::size_t block,
                          const float* f0Traj = nullptr, const float* rTraj = nullptr, bool perSample = false);
// perSample = true renders in 1-sample blocks and records modeGain()/phase() after each (SC-006(a), SC-008(b), SC-026)
double anchorRaisedCosine(double g0, double a, std::size_t n, std::size_t len);   // D-4 closed form, double
double anchorHermite(double g0, double gp0, double g1, std::size_t n, std::size_t len);
double anchorHermitePeakD2(double g0, double gp0, double g1, std::size_t len);     // FR-041 analytic
struct C1Report { double worstRatio; double worstBoundaryJump; bool inRange; };
C1Report anchorCheckC1(std::span<const double> g, std::span<const AnchorSegmentSpec> segs, double lo, double hi);
double anchorInstFreqHz(const std::vector<double>& phase, std::size_t n, double fs); // wrap(phase[n]-phase[n-1])·fs
double anchorWindowEnergy(std::span<const float> x, std::size_t from, std::size_t to);
double anchorShsF0(const float* x, double fs, double f0Hint);   // SC-008(r): Hermes subharmonic summation, see below
std::vector<std::string> anchorFindMutableStatics(std::string_view source);   // SC-013 scanner, see S8.3
bool anchorWaveformsSeparated(std::span<const double> wA, std::span<const double> wB);  // SC-012(a), C-14 floor rule
```

`AnchorSegmentSpec` (test-side) lists the analytic segments the test expects, so the C¹ checker compares each segment with its
own bound. The positive control feeds a linear ramp and must fail. `requireLREqual` (:61) runs inside `renderAnchor` on every
render (SC-007).

**SC-008(r) estimator (offline, double).**
1. Take the Hann magnitude spectrum of one 262144-sample frame (`lowFreq::magnitudeSpectrum`, the same frame
   `renderedHarmonicPowers` uses).
2. Score S(f) = Σ_{h=1}^{15} 0.84^{h−1}·M(h·f), with M linearly interpolated between bins (Hermes 1988 compression factor
   0.84).
3. Search f on a 0.05-cent log grid over [f₀/2.2, 2.2·f₀], then refine with a parabola through the best three points.

Because Fold is harmonic, S(f₀/2) gets only the even half-harmonics at reduced weights and S(2f₀) misses the odd harmonics,
so the argmax is f₀. The test asserts 1200·\|log₂(f̂/f₀)\| ≤ 1 cent.

### S8.3 Per-criterion tests

| SC | TEST_CASE | Fixture and assertion strategy |
|---|---|---|
| SC-001 | `AnchorOscillator_WaveformThd`, `_ThdToolFloor` | Five waveforms × {E1, C2, C3} × {44.1, 48, 96} kHz. Level 1, LOCK, a₁ = `kMaxCoreH1Amplitude`. Render ≥ 262144 settle samples (descriptor rule), then one 262144 frame → `renderedHarmonicPowers(x, fs, f_a, N)`. **(a)** THD ≤ −90 dB. **(b)** odd n ≤ 15: 10·log₁₀(wₙ/w₁) − (−40·log₁₀ n) within ±0.5 dB; even n ≤ −90 dB; total THD −18.3 ± 0.3 dB (expected −18.335, S9.4). **(c)(d)** h3 or h2 at −12.04 ± 0.25 dB, others ≤ −90 dB; rounded h3 − triangle h3 ≥ 7.0 dB. **(e)** octave-down at f_a = f₀/2: THD ≤ −90 dB and power at f₀ ≤ −90 dB re f₀/2. **(f)** a `std::sin` float reference sine reads ≤ −100 dB at every note/rate, and the triangle at f₀ × 1.0003 reproduces (b) |
| SC-002 | `AnchorOscillator_RmsCalibration` | Every waveform × {E1, C3} × 3 rates; a₁ ∈ {`kMaxCoreH1Amplitude`, 0.25·that}; RMS over an integer number of f_a periods ≥ 1 s after settling (`rmsDbOverPeriods`, :126). Assert 20·log₁₀(RMS) = 20·log₁₀(k·a₁/√2) ± 0.1 dB, pairwise waveform spread ≤ 0.1 dB, the a₁ ratio 20·log₁₀(4) ± 0.05 dB; a₁ = 0 → every sample exactly 0.0f from 12 ms after the change (12 time constants of the smoother's 1 ms τ; the derived snap time is ln(a₁/1e-4)·`kLevelSmoothMs`/5 ≈ 8.2 ms, S4.9), and the same for level 1 → 0 (≈ 9.2 ms); the test also asserts the first exact-zero sample is ≤ the derived time + 1 sample |
| SC-003 | `AnchorOscillator_PhaseLockStatic60s` `[long]`, `_PhaseLockStaticSmoke` (10 s, E1), `_PhaseLockTrajectory` `[long]` (+ smoke), `_PhaseLockRetrigger`, `ProfundumAnchor_PhaseLockProcessedVoice` `[long]` (+ smoke) | `ProfundumCore` (mid grid, excitation mix 0) and `AnchorOscillator` (sine, LOCK; octave-down for (d)) get identical call sequences, host blocks 37 and 512. Δθ(t) = wrap(θ_a − θ₁), with `goertzelPhase` over round(k·fs/f₀)-sample windows (k = ceil(f₀) periods, ≥ 1 s) centred at t, on the anchor-alone and core-alone buffers (octave-down: 2·θ_a at f₀/2). **(a)** E0/E1/E2/C3 × 3 rates: \|Δθ(1 s)\| ≤ 1°, \|Δθ(60 s) − Δθ(1 s)\| ≤ 0.5°. **(b)** `f0PerSample` = 5 Hz ±1-semitone vibrato plus a ±12-semitone 200 ms bend every 5 s; Δθ sampled at windows starting 100 ms after each bend, same bars. **(c)** Reset and FreeRunning noteOns (same f₀; +7 semitones) at 2 s, same bars after them. **(e)** excitation (1, 1, 1) → core output, and → `HarmonicFold` at 100 (`setFundamentalHz(f0)`), drift ≤ 0.5° at E0/E1/E2/C3 48 kHz; static offset recorded with `std::printf` beside Phase 2's 0.07°. Per-push smoke: 10 s, E1, 48 kHz, every arm |
| SC-004 | `AnchorOscillator_PunchBoostAndDuration` | boost {3, 6, 12} × `punchTimeMs` {5, 40, 250} × {E1, C3}, 1-sample blocks. max g vs 10^(b/20) ± 0.1 dB (observer). **(b), per C-12:** the first n > N_rise (after the peak) with g == 1.0 bitwise equals N_rise + N_time ± 1, where N_rise + N_time is computed in the test from `kPunchRiseMs + punchTimeMs` at fs; g[N_rise + N_time − 1] > 1; every later sample has \|20·log₁₀ g\| ≤ 0.01 dB; the whole trace from the arm to the return equals the D-4 closed form (`anchorRaisedCosine` for the rise and the fall) within 1e-9 absolute at every sample, so an early or reshaped fall fails. g > 10^(0.1/20) at N_rise + 0.5·N_time. Gain trace y_punch/y_lock at \|y_lock\| ≥ 0.1·peak vs observer within 0.05 dB |
| SC-005 | `AnchorOscillator_GhostEmergence` | `ghostTimeMs` {1, 5, 20, 100} × {E1, C3}. **P1**: 1 s sounding, then a Reset noteOn (t = 0), plus a fresh-instance control (bit-identical, `std::memcmp`). **P2**: FreeRunning `noteOn(f, true)` at g = 1; t = 0 at N_dip (g = 0 exactly at segment end). **P3**: `rearm = false` under both policies; g trace bit-identical to the uninterrupted one. (a) g(0) = 0, g ≤ −30 dB for n ≤ 0.1·T; (b) \|20·log₁₀ g\| ≤ 0.01 dB for n ≥ T + 1; (c) non-decreasing on [0, T]; (d) Σ_{[0,T]} y_ghost² / Σ y_lock² vs Σ g_D4²·y_lock² / Σ y_lock² (g_D4 = `anchorRaisedCosine`) within ±0.25 dB |
| SC-006 | `AnchorOscillator_EnvelopesAreC1`, `_C1CheckerPositiveControl`, `_TransitionsClickFree`, `_ClickDetectorPositiveControl`, `_TransitionDeltaBound` | The coverage list of the spec row, as a table-driven fixture of named transitions, each rendered with `perSample = true`. **(a)** `anchorCheckC1` with the expected segments (heights, N, N_b read from the analytic program); ratio ≤ 1.05 and boundary jump ≤ the larger in-segment bound; range [0, max(B, g₀)]. **(b)** `ClickDetector` default config with `sampleRate` set, 0 detections on each transition render; positive control: a spliced level step gives ≥ 1. **(c)** \|y[n] − y[n−1]\| ≤ 1.1·(G_max·D_max + S_max·P_max), with D_max/P_max from a 1 s steady unit-gain render of each waveform involved (the E-2 sweep uses its top frequency), and S_max analytic: \|A\|π/(2N), the Hermite's analytic peak slope, (1 − e^(−1000/fs))·ΔL for the level, Δc_max·max\|w_new − w_old\| for crossfades |
| SC-007 | `AnchorOscillator_LrBitIdentical` | Waveform × mode × policy × blocks {1, 37, 64, 512, 4096}, plus the non-finite fixtures (in the nonfinite TU). `requireLREqual` and `L[i] + L[i] == L[i] + R[i]` (mono sum = 2·L). Every other fixture calls `requireLREqual` through `renderAnchor` |
| SC-008 | `ProfundumAnchor_FollowReferenceOnFoldedVoice` (chain TU), `AnchorOscillator_FollowRatioAccuracy`, `_FollowLatency` | **(r)** core `kSawAnchor` / `kGrowl` → excitation (1, 1, 1) → Fold 100, E0/E1/E2/C3; `anchorShsF0` within 1 cent of f₀. **(a)** r ∈ {0.5, 0.75, 1.5, 2} × {E1, C2, C3}: `renderedHarmonicPowers` peak-refined fundamental (Goertzel phase slope over two 1 s windows: f = f_a + Δθ/(2π·Δt)) = r·f₀ ± 1 cent; a ±12-semitone 200 ms glide at r = 1.5: `anchorInstFreqHz` within ±5 cents of 1.5·f₀(grid-sampled t) from 50 ms after the end. **(b)** 1-sample blocks; f₀ step: first n with \|cents\| ≤ 5 is ≤ 32 after the step; r step: ≤ 32; f_inst stays within [min, max] of the old and new frequencies at every sample. **(c)** FOLLOW r = 1 vs LOCK on a static note and on the SC-003(b) trajectory: `std::memcmp` equal |
| SC-009 | `ProfundumAnchor_SubPreservedThroughMaxFold` | S = Fold100(excite(1,1,1)(core)) + anchor; R = core(mix 0) + anchor (Fold 0 = not inserted). Same a₁ (from the unprocessed core's `deliveredGains()[0]·kCenterPanGain`). Core states mid grid and `kSawAnchor`. Gated waveforms: sine, triangle, rounded, asymmetric. E0/E1/E2: \|10·log₁₀(w₁(S)/w₁(R))\| ≤ 1 dB. Recorded per row: the core-only w₁ change and `goertzelPhase` of core h1 vs anchor in both chains. Octave-down rows are recorded only |
| SC-010 | `AnchorOscillator_NoAliasing`, `_AliasGatePositiveControl` | Every waveform rendered at level 1 in LOCK, with +12.0 dB added analytically to the measured aliased power for the PUNCH +12 dB peak (SC-010 note below) × {E0, E1, E2} × {44.1, 48, 96}: `aliasedPower(x, aliasFftLength(fs, f_a), fs, f_a).aliasedDbfs` + 12.0 ≤ −80. **FOLLOW arm (the ratio path, S4.4):** triangle, rounded sine and asymmetric sine in FOLLOW with r = 4 at E2, C3 and C4 × 44.1 kHz (f_a = 4·f₀), plus an r step 1 → 4 at constant f₀ (C3, `setFollowRatio` once and `followRatioPerSample` once) with the frame taken after the step's glide: the same −80 dBFS bar (+12.0 dB), **and** no harmonic at or above the cap: every wₙ of `renderedHarmonicPowers(x, fs, f_a, N)` with n·f_a ≥ `capFrequency(fs)` and < fs/2 is ≤ −90 dB re w₁ (`aliasedPower` excludes harmonic bins, so this second check is what sees a stale taper below Nyquist). Positive control: a test-local naive piecewise-linear triangle (4·\|φ − 0.5\| − 1, sine phase) at C4 / 44.1 kHz at the same gain reads > −80 (model: −74.6 dBFS at 0 dB boost, −62.6 dBFS at +12, S9.7). Recorded: every waveform per octave E2 → C5 |
| SC-011 | `AnchorOscillator_NoDc` | Every waveform × {E0, E1, C3}, integer-period window ≥ 1 s: 20·log₁₀\|mean\| ≤ −80 dBFS |
| SC-012 | `AnchorOscillator_AudibilityGate` | C2 and E1. (a) per C-14: harmonic-power vectors n ≤ 15 in dB re own w₁, each entry floored at −90 dB; `anchorWaveformsSeparated` requires ≥ 6 dB on a harmonic where at least one of the pair is above the floor, for the nine pairs that do not involve both sine and octave-down; the sine/octave-down pair by octave-down's w₁ peak at f₀/2 ± 1 cent with power at f₀ ≤ −90 dB re it; positive control: sine vs sine and triangle vs triangle (two renders) are reported not separated; (b) 33-point level sweep, E_sub strictly increasing, span ≥ 40 dB − 1e-4 dB (C-9); (c) boost 0..12 in 13 points, energy over [0, rise + time] strictly increasing, span ≥ 6 dB; (d) 9 punch times, energy over [rise, rise + 250 ms] strictly increasing, span ≥ 3 dB; (e) 9 ghost times, first-10 ms energy strictly decreasing, span ≥ 10 dB; (f) r ∈ {0.5, 2} → r·f₀ ± 1 cent; (g) the three mode-pair ratios over their own windows at the D-4 defaults vs the spec's bars, with the analytic values recomputed in double in the test |
| SC-013 | `AnchorOscillator_InstancesIndependent` | Two instances (C2 Reset LOCK triangle / E1 FreeRunning PUNCH asym) interleaved in blocks {1, 37, 512} vs each alone: `std::memcmp`. Source scan: the test reads `anchor_oscillator.h` (path from a CMake `target_compile_definitions` macro, as Phase 2's footprint tests) and runs `anchorFindMutableStatics` over the **whole header**: class scope and the inline member bodies (function-local statics). After stripping comments and string literals, every declaration containing the token `\bstatic\b` (so `static_cast` / `static_assert` never match) is flagged unless it is `static constexpr`, `static const`, or a function declaration. A declaration is a function iff a `(` appears before the first `=`, `[`, `{` or `;` (so `static double y_ = std::sqrt(2.0);` is data). Return type is never used to decide. Assert the result is empty. **Positive controls** (the scanner must flag each): `static double x_;`, `static float table_[16];`, `static bool once_ = false;`, `static double y_ = std::sqrt(2.0);`, `void f() { static int n = 0; }`, `static thread_local float t_;`. **Negative controls** (must not flag): `static constexpr float k = 1.0f;`, `static const int m = 2;`, `[[nodiscard]] static constexpr std::size_t latencySamples() noexcept { return 0; }`, `static float bankEps(float b) noexcept;` |
| SC-014 | `AnchorOscillator_RealTimeSafe` | `AllocationScope` around every audio-thread call × blocks {1, 37, 64, 512, 4096} × every waveform and mode with transitions active: count 0. `static_assert(noexcept(a.processBlock(...)))` and the same for every setter. Before `prepare`: zeros, `stateFinite()`, observers unchanged. Before the first noteOn (C-7): zeros and `isSounding() == false`. **FR-007:** (i) a render into one aliased buffer (`processBlock(L, L, n)`) equals the L channel of the same sequence rendered into two buffers (`memcmp`), for every waveform and mode with transitions active; (ii) `processBlock(nullptr, R, n)`, `processBlock(L, nullptr, n)` and `processBlock(L, R, 0)` interleaved into a sequence (including mid-PUNCH, mid-crossfade and mid-glide) leave the subsequent output, `phase()` and `modeGain()` traces bit-identical to the sequence without them, and leave the non-null buffer untouched (filled with a sentinel before the call) |
| SC-015 | `AnchorOscillator_NonFiniteInputs` (nonfinite TU) | NaN / ±Inf built with `std::bit_cast<float>(0x7FC00000u)` etc. into each Controls field, `noteOn`, `setFrequency`, `setFollowRatio`, `setCoreH1Amplitude` and trajectory elements: the previous value is kept (render bit-identical to the run without the bad call). Denormal 1e-40f (built by bit pattern) in each Controls float field, read back through `pendingControls()` (C-4, C-11): `level` and `punchBoostDb` read exactly 0.0f; `punchTimeMs` reads `kMinPunchTimeMs` and `ghostTimeMs` reads `kMinGhostTimeMs`. Output finite, \|y\| ≤ `kOutputCeiling`, `stateFinite()` true |
| SC-016 | `AnchorOscillator_Deterministic`, `_BlockSizeIndependent`, `_SampleRateChange` | Two instances: `memcmp`. Partitions {512, 64, 37, 1} with a pending PUNCH and a vibrato: `memcmp`. `prepare` 48 → 96 → 44.1 → 22.05 → 192 kHz: SC-001 E1 sine/triangle THD smoke, SC-002 RMS, and the SC-003(a) 10 s smoke at each rate; `fingerprintRender` / `compareFingerprints` only as a cross-rate smoke, never a golden |
| SC-017 | `AnchorOscillator_RetriggerPolicies`, `_OnsetClickFree` | Reset noteOn: first sample `== 0.0f` for every waveform. Prior states (other f₀/Controls, mid-crossfade, moving level smoother, mid-PUNCH, mid-GHOST, 0.3 s vs 2 s elapsed) → identical renders after a Reset `noteOn(f, true)` (`memcmp`, every mode). `static_assert(static_cast<std::uint8_t>(AnchorOscillator::RetriggerPhase::Reset) == static_cast<std::uint8_t>(ProfundumCore::RetriggerPhase::Reset))` and the same for FreeRunning. FreeRunning same f₀ LOCK: bit-identical to the uninterrupted render. `rearm = false`: g trace equal under both policies. Onset: max \|Δy\| over 10 ms ≤ 1.1 × the steady max \|Δy\|, every waveform, E0/E1/C3 (model 1.0000, S9.5) |
| SC-018 | `AnchorOscillator_CpuBudget` `[.perf]` | For each of the 20 waveform × mode cells: 10 s at 48 kHz, block 64, a waveform switch and a PUNCH re-arm (FreeRunning `noteOn(f, true)`) every 100 ms, 5 Hz vibrato `f0PerSample`, FOLLOW with `followRatioPerSample` ≡ 1.25. Each cell runs at **two notes**: E1 (the fast path), and f₀ = 494 Hz with the ±1-semitone vibrato, which keeps f_T in the taper band at 48 kHz so `refreshTaper` takes its slow path at every grid sample (7 triangle terms in the band at r_eff = 1, 9 at r_eff = 1.25, the maximum S4.5 allows; `setFrequency` clamps only to [`kMinF0Hz`, `capHz_`], S4.3, so the note is reachable). 40 cells. Median of 5 per cell, worst cell ≤ 0.3 % (time / audio time); the E1 and taper-band cells are reported side by side. Run only via `node tools/run-cpu-tests.js dsp_systems_tests`, alone |
| SC-019 | (comply) | `node specs/profundum-phase3-anchor/check-footprint.js 9b1a4cf5` (adapted from Phase 2's): 0 diff lines in `dsp/include/krate/dsp/**` except the new header; 0 in `profundum_core.h`, `harmonic_fold.h`, `harmonic_oscillator_bank.h`, `spectral_shape_recipe.h` and the Phase 1/2 TUs and helpers; the layer grep (the new header includes nothing from `systems/` or `effects/`). If S5.4 was invoked: append-only check plus the four plugin suites |
| SC-020 | (comply) | `check-bars.js` copied from Phase 2 (`baseline` at build start, `check` at comply) |
| SC-021 | (comply) | `node specs/profundum-phase3-anchor/check-handoff.js`: section-scoped string search per the spec row (the Phase 11 paragraph per C-10) |
| SC-022 | `ProfundumAnchor_FoldFeedAudible`, `_FoldFeedSubPreserved`, `_FoldFeedMono` | Anchor sine, LOCK, level 1, rendered once into A. Core (mid grid) + g·A → `HarmonicFold` {65, 100} → + A, g ∈ {0, 1}, E1 and C2. (1) \|ΔTHD\| ≥ 3 dB or \|Δ centroid\| ≥ 0.25 oct (`describeRendered`, :251) at both Fold settings; (2) \|ΔE_sub\| ≤ 1 dB; (3) `requireLREqual`. The verdict is printed; a fail is a recorded outcome (feed not shipped), so (1) is a `CHECK` whose result drives S13's branch, not a build break. (2) and (3) are `REQUIRE` |
| SC-023 | (comply) | outputs of both scripts quoted |
| SC-024 | `ProfundumAnchor_LevelCalibration`, `_NoCompensation` | The six states × {E1, C2}, 48 kHz. (a) a₁ = `deliveredGains()[0]·kCenterPanGain` vs the rendered core h1 amplitude, measured **by ratio**: a test-local float reference sine of amplitude a₁ at the same f₀ and rate goes through the same `renderedHarmonicPowers` frame, and 10·log₁₀(w₁(core L)/w₁(ref)) is within ±0.1 dB, the same for R (the main-lobe ratio is exact at any bin offset, low_frequency_metrics.h:172–177; w₁ itself scales as (A·N)²·3/32, :214–216, so it is never converted with √(2·w₁)); and Anchor RMS / (a₁/√2) = k ± 0.1 dB, spread ≤ 0.1 dB. (b) w₁(core + anchor at level 1) − w₁(core) = 20·log₁₀(1 + k) ± 0.25 dB (6.02 dB), and at `kDefaultLevel` = 20·log₁₀(1 + 0.5·k) ± 0.25 dB (3.52 dB), the C-13 rendered check. (c) RMS rise at `kDefaultLevel` recorded per state and ≤ 3.6 dB once ratified (model 0.00…3.52 dB, S9.3). (d) structural in Phase 3 (C-13): the core buffer rendered with and without an anchor beside it: `memcmp`; summed[i] == core[i] + anchor[i] for every i. Kept as a documented tautology; the teeth are (b) and the static API check (C-13). Phase 9 re-runs it on the voice (S13) |
| SC-025 | `AnchorOscillator_TrajectoryContract` | (1) a fifth up via `setFrequency` (LOCK, FOLLOW r = 1): ≤ 32 samples to ±1 cent (`anchorInstFreqHz`). (2) 200 ms ±7-semitone glide: ±5 cents from 50 ms after it ends. (3) Drop: the core gets −12 st over 100 ms and back; LOCK fed the pre-Drop trajectory: f_inst = note f₀ ± 1 cent throughout; FOLLOW (r = 1.5) fed the post-Drop trajectory: 1.5·f₀(grid t) ± 5 cents from 32 samples after each grid update. The lock arm uses SC-003's Δθ on the pre-Drop window |
| SC-026 | `AnchorOscillator_PhaseContinuity`, `_PhaseConvention`, `_PhaseReference` | (a) `perSample` renders across FreeRunning noteOn (rearm both), glide, Drop shape, LOCK ↔ FOLLOW, trajectory switch, waveform switch: wrap(phase[n] − phase[n−1]) − expected(n) ≤ 1e-9 cycles, where expected(n) is the test's independent double law r_eff·f_grid/fs (with the 16-sample linear glide where r_eff changes). The mirror's deviation from that law is ≤ 6e-8 relative ≈ 1e-10 cycles (S9.1), inside the bar. (b) Reset noteOn: first sample 0.0; `goertzelPhase` of anchor vs core h1 over the window starting at 1 s: ≤ 1° (octave-down via 2·θ_a). (c) core fed a Drop, anchor LOCK fed pre-Drop: φ(end of Drop) − φ(start) vs Σ f_grid/fs over the same window ≤ 1e-6 cycles (≤ 6.5 cycles at C2, so the mirror deviation is ≤ 4e-7) |
| SC-027 | `AnchorOscillator_RearmEvent` | (a) a re-arm noteOn in host blocks {1, 37, 512}: g traces identical (`memcmp` on the per-sample observer trace), and the first g change is at the block's offset 0. (b) PUNCH bridged: max g = B ± 0.1 dB, return per SC-004(b) from the arm. (c) GHOST bridged: g = 0 at ≤ N_dip + 1 samples, then SC-005. (d) `rearm = false` × both policies × four modes: g trace identical to the uninterrupted one; `phase()` trace identical between rearm true and false. (e) noteOn(f, false) from not sounding: armed |
| Plan-internal | `AnchorOscillator_MirrorTracksCore` | Core and anchor with identical calls (Reset/FreeRunning noteOns, ±12 st bend, vibrato), 1-sample blocks: after each block `mirrorState().baseHz == core.baseFrequency()` and `.currentHz == core.currentFrequency()` bitwise. This proves the chase and grid replication directly, before the 60 s drift test |
| Plan-internal | `AnchorOscillator_CalibrationConstants` | `static_assert(AnchorOscillator::kMaxCoreH1Amplitude == HarmonicFold::kCalibrationPeak)`; \|√kPowerTarget·kCenterPanGain − kMaxCoreH1Amplitude\| ≤ 1e-7 |

**SC-010 note.** The PUNCH +12 dB "loudest state" lasts only `kPunchRiseMs + punchTimeMs` ≤ 251 ms, shorter than an alias FFT
frame at E0 (2^19 samples). The aliased-power gate is linear in gain and the Anchor has no gain-dependent nonlinearity, so the
test renders at level 1, LOCK (and FOLLOW for the ratio arm), and adds 20·log₁₀(10^(12/20)) = +12.0 dB to the measured
aliased power. That is the exact PUNCH-peak figure without a held peak state, and the S3 API has no test-only Controls
arm. This is a test method, not a bar change.

---

## S9. Calibration and evidence record (measured 2026-10-11)

### S9.1 Coefficient-law drift (`anchor-model.js eps`)
60 s drift against the h1 rotation 2·asin(ε_eff/2), with the float chain of S5:
- double law: E0 ≤ 0.025°, E1 ≤ 0.056°, E2 ≤ 0.111°, C2 ≤ 0.048°, C3 ≤ 0.168° (worst at 48 kHz);
- bit-faithful mirror: 0.000° everywhere;
- mirror with bankEps ±1 ulp: E0 0.038°, E1 0.077°, E2/C2 0.154°, C3 0.307°.

### S9.2 Float MCF state-rounding walk (`anchor-model.js mcf`)
Δθ(60 s) − Δθ(1 s) of the core's own float MCF against its exact rotation, plain and FMA-emulated: max 2.57e-3° (C3, 48 kHz,
FMA). E0/E1/C3 at 48/96 kHz all ≤ 2.6e-3°.

### S9.3 Calibration (`anchor-model.js cal`)
The table in S1 C-2. kMaxCoreH1Amplitude = √P₀·0.70710678 = 0.3552343859 = `HarmonicFold::kCalibrationPeak`.

### S9.4 Triangle truncation (`anchor-model.js tri`)
The table in S1 C-1.

### S9.5 Asymmetric onset ramp (`anchor-model.js onset`)
Worst ratio over E0–C3 × 22.05–192 kHz: 1.0000 (P = 0.25), 1.0062 (P = 0.5), 1.0000 (P = 1, chosen). First sample 0.

### S9.6 Hermite bridge rule (`anchor-model.js bridge`)
The table in S1 C-3.

### S9.7 Alias positive control (`anchor-model.js alias`)
Naive sampled triangle at k = 1, a₁ = `kMaxCoreH1Amplitude`:
- C4 / 44.1 kHz: −74.6 dBFS at 0 dB boost and −62.6 dBFS at +12 dB (both above −80, so the gate has teeth);
- E2 +12 dB: −77.7 dBFS;
- E0 +12 dB: −95.7 dBFS.

The plan's additive waveforms generate nothing above the cap.

### S9.8 CPU (`bench/anchor-kernel-bench.cpp`, clang −O2 −ffast-math −march=x86-64-v2, pinned, 5 reps)
Worst-case kernel: a crossfade active every sample (triangle and rounded sine both evaluated), the envelope always on a cosine
segment, both smoothers, and the mirror with a 5 Hz vibrato on the grid; block 64, 48 kHz.
- K = 16: **0.144 / 0.158 / 0.159 / 0.163 / 0.164 %** (median 0.159 %).
- K = 32: 0.239 / 0.256 / 0.267 / 0.273 / 0.279 % (median 0.267 %).

The bench omits `processBlock`'s branches and stereo store, so the C++ test (SC-018) is the authority. **Coverage gap
(recorded by the plan-stage review):** the bench runs at E1 (41.2 Hz) and models the taper only as a term count, with no
`log2`/`pow`. It never enters `refreshTaper`'s slow path, so these figures do not certify the taper-band worst case. T010
re-runs the bench with the real taper loop at f₀ = 494 Hz plus the vibrato before the SC-018 gate, and SC-018's taper-band
cells (S8.3) are the gate. If the slow path breaks the budget, the remedy is to cache per-term weights in the cents domain
and recompute only terms whose band membership changed. The budget is never relaxed. MSVC typically runs such
scalar double kernels 1.2–1.5× slower, which keeps K = 16 inside 0.3 % and puts K = 32 at risk.

---

## S10. Build integration

### S10.1 CMake (`dsp/tests/CMakeLists.txt`, enumerated lists)

- **`dsp_systems_tests` sources** (after :630), with a comment block naming each TU's SCs (the S8.1 table):
  - `unit/systems/anchor_oscillator_test.cpp`
  - `unit/systems/anchor_oscillator_spectral_test.cpp`
  - `unit/systems/anchor_oscillator_nonfinite_test.cpp`
  - `unit/systems/profundum_anchor_chain_test.cpp`
  - `unit/systems/anchor_oscillator_perf_test.cpp`
- **`-fno-fast-math` block** (before the `PROPERTIES` line :1114): `unit/systems/anchor_oscillator_nonfinite_test.cpp` only, with a comment. The perf
  and chain TUs stay in the shipping FP mode (S7).
- SC-013's source scan needs the header path. It is added as a compile definition on that TU only
  (`set_source_files_properties(... COMPILE_DEFINITIONS ANCHOR_HEADER_PATH="${CMAKE_SOURCE_DIR}/dsp/include/krate/dsp/systems/anchor_oscillator.h")`),
  unless the T001 sweep finds an existing helper macro to reuse.
- No library CMake change: KrateDSP is header-only for this file.

### S10.2 Commands (Windows, full CMake path) and parallelism

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests 2>&1 | tee build-anchor.log | tail -5
# per-push lane (concurrent with anything not timing-sensitive), excluding [long] and perf:
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "[anchor],[anchor-chain]~[long]~[.perf]" 2>&1 | tee anchor-pushlane.log | tail -5
# [long] lane, concurrently with the per-push lane (not timing-sensitive), sharded by tag/name:
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "AnchorOscillator_PhaseLock*" 2>&1 | tee anchor-long-a.log | tail -3 &
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "ProfundumAnchor_PhaseLockProcessedVoice" 2>&1 | tee anchor-long-b.log | tail -3
# regression: the whole systems suite (Phase 1/2 included) once, after the anchor lanes:
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "~[long]~[.perf]" 2>&1 | tee systems.log | tail -5
# CPU lane, ALONE, after everything else has finished and the machine has idled:
node tools/run-cpu-tests.js dsp_systems_tests
```

Static checks, run concurrently with the test lanes: `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja`,
`node tools/check-portability.js`, `node tools/lint-apple-globals.js`.

### S10.3 Scripts (Node, in the spec folder)

| Script | Purpose |
|---|---|
| `anchor-model.js` | This plan's evidence (S9) |
| `check-footprint.js` | SC-019, adapted from Phase 2's (base `9b1a4cf5`) |
| `check-bars.js` + `bars-baseline.json` | SC-020, Phase 2's script with this spec's SC table |
| `check-handoff.js` | SC-021 (S13 strings; SC-022 outcome as an argument: `pass` / `fail`) |

### S10.4 Expected runtimes

- Systems build: an incremental relink after a new header plus 5 TUs takes minutes on the existing build tree.
- Per-push anchor lane: SC-001 is 45 renders × ≈ 0.5 M samples plus one 262144 FFT each; SC-003's smokes are 10 s renders
  of the core, ≈ 0.1–0.2 s each. Seconds to tens of seconds.
- `[long]` lane: ≈ 12 static × 60 s core renders (core ≈ 1 % CPU: ≈ 0.6 s each), trajectory and processed-voice arms with
  Fold (≈ 2.5 % → 1.5 s per 60 s render), plus 1 s Goertzel windows. Under two minutes per shard.
- CPU lane: 20 cells × 5 × 10 s audio at ≈ 0.16 % ≈ 2 s of wall clock plus the runner's settle.

---

## S11. Risks and mitigations

| Risk | Mitigation |
|---|---|
| Fast-math reassociation makes the mirror's bankEps differ from the bank's by ≥ 2 ulp, so SC-003 is red at C3 on some ISA | One ulp measured inside the bar (S9.1). `MirrorTracksCore` localises a divergence at the base/current level. FR-032 is pre-designed (S5.4), append-only, with the four-suite gate. Never a re-ruling (spec SC-003) |
| Recipe-control slews move the core's shadow by ulps the Anchor cannot see (C-5 residual) | Bounded at ≤ 2 ulp, non-accumulating (re-anchored at every refresh). Static-control Phase 3 fixtures do not see it. Phase 9's 30 min soak measures the lock with Motion on (S13 obligation), and S5.4 is the remedy if needed |
| `ClickDetector` false positives on legitimate fast PUNCH (1 ms rise at +12 dB) | SC-006(b) runs the detector on the documented defaults with the positive control. If a legitimate raised cosine reads as a click, that is the detector's sigma statistic on a 1 ms event, and the C¹ and delta-bound gates (a)(c) are the analytic evidence. Any re-ruling goes through FR-065, never a silent config change |
| SC-012(b) knife-edge | C-9 |
| a₁ relative calibration leaves Hollow/Growl unanchored (C-2 finding) | Surfaced for ratification. The API supports every option without change |
| SC-022 fails (feed not audible) | Pre-planned: the feed is not shipped, FR-066's three feed strings are not written, and `check-handoff.js fail` asserts they are absent |
| Taper slow path under a vibrato above the fast-path threshold costs more than the bench measured | SC-018's taper-band cells gate it (S8.3); T010 re-runs the bench with the real loop; cents-domain weight cache as the remedy (S9.8). Never a budget relaxation |
| MSVC slower than the clang bench | K = 16 holds ≈ 47 % margin at clang. The C++ SC-018 test on the shipping toolchain is the gate; the budget is never relaxed |
| Denormal stalls in a smoother or recurrence without FTZ | FR-057 precondition. The tests run under the test main's FTZ/DAZ, and one anchor TU asserts it is on (S8.1). The smoother flushes, and the recurrences live on the unit circle |
| Long renders in `[long]` blow the per-push budget | 60 s cases tagged `[long]` (nightly on all three OSes), with 10 s per-push smokes for every arm |
| ODR collision of a test helper | `anchor`-prefixed names in `ProfundumTest`, swept by T001. The class nests every type |

---

## S12. Implementation order (input to tasks.md)

1. **T001** ODR re-sweep (class, nested names, helper names). Copy `check-bars.js`, write the baseline, and adapt
   `check-footprint.js` / `check-handoff.js`. → verify: scripts run; baseline written.
2. **T002** Header skeleton: API of S3, lifecycle (S4.2), setters (S4.3), sine only, LOCK only, the S5 mirror,
   `processBlock` (S4.4). → verify: `MirrorTracksCore`, SC-014, SC-007 and the `static_assert`s written first, failing, then
   green.
3. **T003** Phase-lock gates: SC-003 smokes and `[long]` arms, SC-026, SC-025(1)(2). → verify: drift figures recorded;
   FR-032 is decided here (green → no shared diff).
4. **T004** Waveforms: S4.5 (triangle Clenshaw + taper keyed on f_T, rounded, asym + C-6 ramp, octave-down), crossfade
   (S4.8). → verify: SC-001, SC-002, SC-010 (LOCK arms; the FOLLOW arm lands with T007), SC-011, SC-017 onset (tests first).
5. **T005** Level/a₁ (S4.9). → verify: SC-002 a₁ arms, SC-024(a)(b)(d).
6. **T006** Mode envelope (S4.7) and the re-arm event. → verify: SC-004, SC-005, SC-006, SC-027, SC-017.
7. **T007** FOLLOW (S4.10). → verify: SC-008(a)(b)(c), SC-025(3), SC-012(f), SC-010's FOLLOW r = 4 arm and r-step arm.
8. **T008** Non-finite TU (SC-015), determinism (SC-016), independence (SC-013), audibility (SC-012).
9. **T009** Chain TU: SC-008(r), SC-009, SC-022, SC-024(c); record the SC-022 outcome.
10. **T010** Re-run `bench/anchor-kernel-bench.cpp` with the real taper loop at f₀ = 494 Hz + vibrato and record it in S9.8.
    Then the perf TU (SC-018, E1 and taper-band cells), run alone via `run-cpu-tests.js`.
11. **T011** Roadmap amendments per S13 (branch on SC-022). → verify: `check-handoff.js`.
12. **T012** clang-tidy `dsp`, `check-portability.js`, `lint-apple-globals.js`, `check-footprint.js`, `check-bars.js check`,
    then a full `dsp_systems_tests` regression run (Phase 1/2 unmodified).

Pluginval is not applicable (library only).

---

## S13. Deferred-clause handoff text (FR-066, SC-021)

The tasks paste these sentences, adjusted only to fit the surrounding prose, so `check-handoff.js` can find the quoted strings.

- **Phase 9 success criteria** (roadmap :511–515, appended):
  - "Anchor sub energy (w1) within 1 dB through maximum Fold + every Character algorithm at maximum Amount, at E0–E2
    (Phase 3 D-7)."
  - "FOLLOW tracks the perceived fundamental of a voice through a real pitch-moving stage (accuracy ±5 cents, latency
    measured), and the capability audibility table carries a FOLLOW row driven by that stage, or FOLLOW is flagged inert
    (Phase 3 Q1)."
  - "The Anchor joins after Character by default, the direct Anchor path is always present, and Drop is routed to the core
    only: the Anchor receives the resolved note pitch before Drop in LOCK / PUNCH / GHOST and the core's post-Drop f0 in
    FOLLOW (Phase 3 Q3, Q4)."
  - Plan addition, the C-5 residual: "the Anchor–h1 lock is measured in the 30 min soak with Motion modulating the core
    spectral controls."
  - Plan addition, C-13: "inside the voice, the core's output is bit-identical with the Anchor attached and detached, and
    the core + Anchor sum is a plain add with no compensation or normalisation stage (Phase 3 FR-025, SC-024(d))."
- **Phase 5 `BassCharacterStage` bullet** (:374–379): "Rectify, Crush and Fracture each ship an r-law deliverable: the
  perceived-pitch ratio r = f_perceived / f0 as a function of the stage's controls with a measured reference, or r ≡ 1 recorded
  with evidence (Phase 3 FR-047)."
- **Phase 4 Note Memory bullet** (:349–352): "Note Memory decides when the Anchor's re-arm event is issued (legato, retrigger,
  overlapping notes, voice reuse); the core and the Anchor share a common phase reference at the resolved note pitch before
  Drop (Phase 3 FR-036, FR-048)."
- **Phase 6 Mass** (:403): "The voice supplies the core's h1 amplitude per control block (deliveredGains()[0] ·
  kCenterPanGain, Phase 3 FR-023), and the summed core + Anchor signal is validated through Mass dynamics and the output
  safety stages, not by compensation in the Anchor (FR-025)."
- **Only if SC-022 passes:**
  - Phase 9: "voice-level send `anchorFoldFeed` ∈ [0, 1], default 0, feeding the Anchor into Fold with the direct path
    always present";
  - Phase 8 `MorphState` (:452–455): add `anchorFoldFeed` to the listed fields;
  - Phase 11 paragraph (C-10): "`anchorFoldFeed` is registered in the 300–399 anchor block."

---

## Review notes

- **Partly rejected: "ProfundumCore::prepare clamp/bank_.prepare lines (:129/:131/:136) a few lines off" (reuse-reality,
  minor).** Re-read on 2026-10-11: `prepare(double, int)` is at `profundum_core.h:129`, the `std::clamp(sampleRate, …)` at :131,
  and `bank_.prepare(sr)` at :136. The ledger's anchors are correct and are kept. The other half of that issue was right:
  S4.2's recipe anchor is corrected to `spectral_shape_recipe.h:161–163`.
- **Partly adopted: SC-012(a)'s sine/octave-down pair.** The review proposed separating this pair by the octave clause only.
  That reads the spec's "≥ 6 dB on at least one harmonic per pair" as not applying to a pair whose harmonic shapes are
  identical by FR-015. This is recorded as C-14 for ratification rather than applied silently. The nine other pairs keep the
  full bar, and the floor makes it stricter (a noise difference no longer passes).
- **SC-018 taper-band cells** add a second note per cell instead of replacing E1, so the gate covers both the fast path and
  the slow path. The 0.3 % budget is unchanged.
