# Implementation Plan: Vorago Phase 9 — Cavern Space Engine

**Spec:** `specs/vorago-phase9-cavern-space/spec.md`
**Roadmap:** `specs/Vorago-roadmap.md` → Part A → Phase 9 (lines 410–430), Open Question 3 (569–570)
**Deliverables:** one new Layer 4 header `dsp/include/krate/dsp/effects/cavern_verb.h`; one
append-only extension to `dsp/include/krate/dsp/effects/aether_reverb.h`; one lifted test helper;
five new test TUs in `dsp_effects_tests`.
**Plugin work:** none (Phase 11).

---

## S0. Verification ledger — every signature below was read this session

No API in this plan is recalled; each row was opened at the cited line during planning.

| Fact | Location (verified) |
|---|---|
| `class AetherReverb` | `effects/aether_reverb.h:1377` |
| `struct PrepareConfig { numChannels=8; maxBlockSamples=2048; maxDelaySeconds=0.50f; shimmerEnabled=true; shimmerMode; bloomEnabled=true; spectralDiffusionEnabled=true; diffusionFftSize=1024; seed=1; }` | `:1577-1588` |
| `void prepare(double, const PrepareConfig&) noexcept` — clamps sr into `[kMinSampleRate,kMaxSampleRate]`, ends with `reset()` | `:1614`, `:1615-1616`, `:1953` |
| `void reset() noexcept` — forces `lastJot* = -1`, then `refreshControlState()` | `:1971`, `:2094-2099` |
| `void silence() noexcept`, `void processStereoBlock(const float*,const float*,float*,float*,std::size_t) noexcept` | `:2145`, `:2164` |
| Process loop: `phase = sampleCounter_ % kControlChunkSamples`; `runControlStep()` at phase 0; slice `min(remaining, 64-phase)` | `:2185-2198` |
| Controls used: `setSize` `:2208`, `setDensity` `:2211`, `setDecaySeconds` `:2214`, `setFreeze` `:2230`, `setDimensionality` `:2239`, `setDamping` `:2244`, `setPreDelayMs` `:2247`, `setSpectralDiffusion` `:2310`, `setSizeBreathDepth` `:2320`, `setDimensionalityTideDepth` `:2328`, `setWidth` `:2333`, `setMix` `:2336`, `setSeed` `:2361` | as cited |
| Accessors: `isPrepared` `:2486`, `isFrozen` `:2493`, `isShimmerActive` `:2498`, `getEffectiveDelayLengthSamples` `:2506`, `getModalDensityPerHz` `:2511`, `getMaxSizeScale` `:2523`, `getStateEnergy` `:2559`, `getNonFiniteRecoveryCount` `:2588`, `isRecovering` `:2603`, `getLatencySamples` `:2612` | as cited |
| Public constants: `kControlChunkSamples = 64` `:1386`, `kFreezeLatchMs = 50` `:1388`, `kMinSampleRate/kMaxSampleRate` `:1392-1393`, `kSizeScaleMin/Max = 0.25/4.0` `:1396-1397`, `kModExcursionFraction = 0.005` `:1398`, salts `kMatrixSalt=0 … kDriftSaltBase=16` `:1546-1552`, `kRefDelays8` `:1564`, `kRefDelays16` `:1568` | as cited |
| Private (NOT reachable from `CavernVerb`): `kMaxChannels = 16` `:2725`, `kDecayMinSeconds = 0.5f` / `kDecayMaxSeconds = 60.0f` `:2735-2736`, `kDefaultDamping = 0.40f` `:2740`, `kMaxPreDelayMs = 200.0f` `:2743`, `kJotRecomputeEpsilon = 1e-7f` `:2785`, `kDampingNyquistRatio = 0.05f` `:2788`, `dampCoeff_[kMaxChannels]` `:4489` | as cited |
| `sizeScale(v) = min(0.25f * exp2(4v), maxSizeScale_)` | `:3011-3015` |
| `updateDecayAndDamping()` — epsilon gate on `currentSizeScale_`/`decaySeconds`/`damping`, then `gDC`, `gNyq`, `feedbackGain_[i]=min(gDC,1)`, `dampCoeff_[i]=clamp(2r/(1+r),0.001f,1.0f)` | `:3124`, `:3128-3137`, `:3140-3150` |
| `refreshControlState()` — `updateGeometry()` + `updateDecayAndDamping()` only on the `!freezeTarget_` branch; morph/diffuser/snapshot always | `:3610-3618` |
| `runControlStep()` — modulators first (`processBlock(kControlChunkSamples)`, never a slice length), then the smoothers, the gate machine, `refreshControlState()` | `:3871-3913` |
| Damping read site: `const float c = dampCoeff_[i]; filterState_[i] = c*delRead[i] + (1-c)*filterState_[i];` | `:4282-4283` |
| One-pole stability note: "DC gain exactly 1 and Nyquist gain c/(2-c)" | `:3085-3086` |
| Non-finite input replacement, "a replacement, never a counter increment" | `:4163-4172` |
| Smoothstep gate shaping, and the measured C0-corner failure at 105.019 s | `:4237-4241`, `:4270-4281` |
| `isFinite()` — `ITERUM_NOINLINE`, composes `detail::isNaN`/`detail::isInf` | `:2937` |
| `reseedStreams()` — `deriveStreamSeed(seed_, kDriftSaltBase + j)` for `j < kMaxChannels/2`; setSeed-then-reset per object | `:2980-2996` |
| `class BrownianDrift : public ModulationSource` | `processors/brownian_drift.h:94` |
| `kTauMin=0.2f` `:97`, `kTauMax=30.0f` `:99`, `kInternalStd=0.5f` `:101`, `kDriftOutputSmoothMs=150.0f` `:103`, `kControlRateInterval=32` `:105` | as cited |
| `prepare(double)` (allocation-free) `:121`, `reset()` `:133`, `setSeed` `:145`, `setSmoothness` `:152`, `setDepth` `:159`, `processBlock(size_t)` `:194`, `getCurrentValue()` (clamped `[-1,1]`) `:212` | as cited |
| **`getCurrentValue()` already carries `depth_`**: `outputTarget() = clamp(depth_ * x_, -1, 1)` is what the 150 ms smoother chases | `:170`, `:212-214`, `:249-251` |
| `tau = kTauMin + smoothness*(kTauMax - kTauMin)` | `:231-234` |
| `class DelayLine` `:57`; `prepare(double,float)` allocates `nextPowerOf2(maxDelaySamples+1)` `:267-279`; `write` `:287`; `read(size_t)` clamps to `maxDelaySamples_` `:292`; `readLinear(float)` clamps, `index1 = min(index0+1, maxDelaySamples_)` `:302-318`; `maxDelaySamples()` `:235` | `primitives/delay_line.h` |
| `class OnePoleSmoother` `:134` — `configure(float ms,float sr)` `:160`, `setTarget` `:170`, `getCurrentValue` `:191`, `advanceSamples(size_t)` `:243`, `snapToTarget` `:257`, `snapTo` `:263` | `primitives/smoother.h` |
| `calculateOnePolCoefficient(ms, sr) = exp(-5000/(ms*sr))` — `smoothTimeMs` is documented as the **time to 99 %**, so the real time constant is `ms/5`. This is the fact B-2 turns on. | `primitives/smoother.h:77-93` (the exponent at `:91`), `configure` `:160-163`; `BrownianDrift` configures its output smoother exactly so at `processors/brownian_drift.h:126-128` |
| `class LinearRamp` `:305` — `configure` `:329`, `setTarget` (recomputes the increment **from the current value**) `:342`, `getCurrentValue` `:364`, `process()` `:370`, `isComplete` `:409`, `snapTo` `:421` | `primitives/smoother.h` |
| `deriveStreamSeed(std::uint32_t base, std::size_t salt)` (lowbias32, non-zero substitution) `:102`; `class Xorshift32` `:41` | `core/random.h` |
| `class ClickDetector` `:99`, `struct ClickDetectorConfig` (`sampleRate` default **44100**) `:38-48` | `tests/test_helpers/artifact_detection.h` |
| `kSampleTolerance = 5.0e-4f` `:58`, `kMetricTolerance = 2.5e-4` `:61`, `struct RenderFingerprint` `:63`, `compareFingerprints` `:122` | `tests/test_helpers/render_fingerprint.h` |
| `class AllocationScope` `:111`; the global `operator new/delete` replacement lives in `<allocation_operator_overrides.h>` and **`aether_reverb_test.cpp:38` already owns it for `dsp_effects_tests`** — a second include is a duplicate-symbol link error | `tests/test_helpers/allocation_detector.h:111`, `dsp/tests/unit/effects/aether_reverb_test.cpp:25-38` |
| NED, inline in a `TEST_CASE` body: 1 ms windows, per-window RMS, `threshold = peakAmp * 0.01`, `ned = occupied/numWindows`, `REQUIRE(ned >= 0.8)` | `dsp/tests/unit/effects/fdn_reverb_test.cpp:328-373` |
| Perf idiom: `kBlockBudgetNs`, `kRegressionFactor = 1.5`, `kReferenceNs = 533 333.3`, `kMaxAdmissibleNs = 355 555.6`; best-of-25 × 500 blocks after 400 warm-up; paired `static_assert`s per baseline | `dsp/tests/unit/effects/aether_reverb_perf_test.cpp:133-147`, `:350-390`, `:418-423` |
| Shipped baselines: core 69 593 / default 114 595 / worst 200 114 / frozen 98 443 / morph 118 464 / clear-chunk 53 760 ns per 512-block | `aether_reverb_perf_test.cpp:322`, `:326`, `:330`, `:334`, `:338`, `:343` |
| `dsp_effects_tests` is an **enumerated** source list; `KRATE_DSP_AETHER_TEST_HOOKS` is target-wide and why | `dsp/tests/CMakeLists.txt:504-533`, `:541-549` |
| `lint-layers.js` flags only `layerIndex(to) > layerIndex(from)` → an `effects/` → `effects/` include is legal | `tools/lint-layers.js:74` |
| `lint-odr.js` qualifies nested types by their enclosing class | `tools/lint-odr.js:19-21` |
| Layer-4 block of the strict-lint TU | `dsp/lint_all_headers.cpp:197` |
| Analysis code Phase 9 **cannot** reuse (file-local, and SC-012 (b) freezes that TU): `OctaveBandFilter` `:148`, `makeOctaveBand` `:164`, `makeBandLimitedNoise` `:177`, `schroederT60` `:217`, `measureImpulseT60` `:410`, `kOctaveCentres` `:428` | `dsp/tests/unit/effects/aether_reverb_test.cpp` |
| `test_helpers` is an INTERFACE target exposing the whole directory — a new header needs no CMake edit | `tests/test_helpers/CMakeLists.txt:7-12` |
| `KRATE_DSP_EFFECTS_HEADERS` list (IDE/source group) | `dsp/CMakeLists.txt:180-194` |

### S0.1 ODR sweep, re-run this session from the repo root

```
grep -rn "class CavernVerb|struct CavernVerb|CavernVerb"      dsp/ plugins/ tools/  -> 0
grep -rn "struct EarlyTap|class EarlyTap"                     dsp/ plugins/         -> 0
grep -rn "ShapedRamp"                                         dsp/ plugins/         -> 0
grep -rn "getEffectiveDampingCoefficient|setDamperOffsetsOctaves|
          effectiveDampCoeff_|damperOffset_"                  dsp/ plugins/         -> 0
grep -rn "normalisedEchoDensity|normalizedEchoDensity"        dsp/ tests/           -> 0
```

`CavernVerb` adds exactly one namespace-scope name to `Krate::DSP`. `EarlyTap` and `ShapedRamp` are
private nested types, qualified by their enclosing class by `lint-odr.js:19-21`.

---

## S0.2 THREE THINGS THE IMPLEMENTER MUST NOT DISCOVER AT BUILD TIME

Found by numerically evaluating the spec's own constants during planning. Two are blocking; all three
are carried into **S14 Open questions** with a recommended ruling.

### B-1 (BLOCKING). FR-020 + FR-021 + FR-022 + FR-028 are jointly infeasible at `kIncommensurabilityOrder = 8`

FR-022 requires, over the 12 tap delays in ms at the default ER size (`d_0 = 60`, `d_11 = 220`):

```
min over i != j, p,q in 1..kIncommensurabilityOrder of |p*d_i - q*d_j| / min(d_i, d_j)  >=  0.05
```

and FR-028 additionally requires `Σ g_i = Σ 0.5*exp(-0.01*d_i) <= 2.0`. Both were evaluated
numerically over the admissible design space (simulated annealing + coordinate ascent on the ten
interior delays, several minutes of search; then a pairwise-coprime integer search over
`{1} ∪ {11 distinct primes}`, which satisfies FR-020's "pairwise coprime integer series" by
construction):

| Constraint set | Best achievable min-metric | Verdict vs the required 0.05 |
|---|---|---|
| order 8, real delays, **no** gain cap | **0.05044** | passes by 0.9 % — knife-edge |
| order 8, real delays, **with** `Σg ≤ 2.0` | **0.04250** | **fails** |
| order 8, coprime integer series, `Σg ≤ 2.0` | **0.04519** | **fails** |
| order 6, coprime integer series, `Σg ≤ 2.0` | **0.05411** | passes by 8.2 % |
| order 4, coprime integer series, `Σg ≤ 2.0` | **0.07390** | passes by 48 % |

The binding pair at order 8 is a high-order near-coincidence (`p:q = 5:8` and similar), not an
audible flutter relation: two taps ~25 dB down whose *fifth* and *eighth* repetitions nearly align.

**Recommended ruling: `kIncommensurabilityOrder = 6`.** It is a one-constant change that leaves
`kEarlyIncommensurabilityTol = 0.05`, `kEarlyTapCount = 12`, `kEarlyFirstArrivalFloorMs = 60`,
`kDefaultEarlySizeMs = 220`, `kEarlyGainG0 = 0.5`, `kEarlyGainAlphaPerMs = 0.01` and
`kEarlyGainSum ≤ 2.0` **all exactly as the spec pins them**. It is not a relaxed criterion: SC-006 (d)
asserts FR-022's inequality *with the named constants* and reports the measured minimum either way.
Alternatives for the record: keep order 8 and lower `kEarlyIncommensurabilityTol` to 0.04
(achievable 0.04519, 13 % margin); or keep order 8 and tol 0.05 and drop `kEarlyGainG0` to 0.40 so
the gain cap stops binding — rejected, because even unconstrained the ceiling is 0.05044 and no
coprime-integer realisation of it was found.

**The shipped table under the recommended ruling** (verified this session: `metric = 0.05411` at
orders 2, 4 and 6; `Σg = 1.865762 ≤ 2.0`; strictly ascending; pairwise coprime):

```
n_i  = { 1, 53, 199, 277, 547, 709, 929, 1049, 1381, 1583, 1721, 1997 }
d_i  = 60 + 160 * (n_i - 1) / (1997 - 1)                       [ms, at the default ER size]
     =  60.000000,  64.168337,  75.871743,  82.124248, 103.767535, 116.753507,
       134.388778, 144.008016, 170.621242, 186.813627, 197.875752, 220.000000
g_i  = 0.5 * exp(-0.01 * d_i)
     = 0.274406, 0.263203, 0.234133, 0.219942, 0.177139, 0.155566,
       0.130415, 0.118454, 0.090776, 0.077206, 0.069120, 0.055402
Σ|g| = 1.865762         (kEarlyGainSum; FR-028's ceiling is 2.0)
min adjacent spacing = 4.1683 ms  (200 samples at 48 kHz - SC-006 (a)'s arrivals are isolated)
```

The metric is a ratio of delays, so it is invariant to `setEarlySizeMs` and to the sample rate: the
table is verified once and holds at every size in `[80, 600]` ms and every rate in `[8k, 192k]`.

Side assignment (FR-020's fixed even→L / odd→R rule) gives `Σg_L = 0.97599`, `Σg_R = 0.88977`, i.e.
a **fixed +0.80 dB L bias on the ER bus**. That is a consequence of the spec's pinned side rule and
gain law, not a defect; the geometry test reports it as a figure and no normalisation is applied
(normalising would contradict FR-020's exact `g_i`).

### B-2 (BLOCKING). SC-003 (a)/(b)'s bound uses the wrong time constant **and** the wrong swing

FR-031 publishes `offset_i = depth * kMaxDamperOctaves * (drift_i.getCurrentValue() / kInternalStd)`
with `kInternalStd = 0.5f` (`brownian_drift.h:101`), i.e. the published octave value is `3.0 * s_i`
where `s_i` is the drift's own `[-1,+1]` smoother output. The spec bounds the per-chunk step by

```
kMaxOffsetStepPerChunk = (1 - exp(-dt/tau_s)) * 2 * kMaxDamperOctaves
                       = 0.0088495 * 3.0 = 0.026549          (spec.md:931-941, :951)
```

**Two independent errors, both checked against the shipped headers this session:**

1. **`kDriftOutputSmoothMs` is not a time constant.** `OnePoleSmoother::configure(smoothTimeMs, sr)`
   documents `smoothTimeMs` as the **time to 99 %** and computes `coeff = exp(-5000/(ms*sr))`
   (`smoother.h:77-93`, the exponent at `:91`; `configure` at `:160-163`) — so the real τ is
   `150/5 = 30 ms`, not 150 ms, and `BrownianDrift` configures it exactly that way
   (`brownian_drift.h:126-128`). The true per-chunk approach fraction is

   ```
   alpha = 1 - coeff^64 = 1 - exp(-5000 * 64 / (kDriftOutputSmoothMs * sr))
         = 1 - exp(-0.0444444) = 0.0434712                   at sr = 48 kHz
   ```

   i.e. **4.91×** the spec's 0.0088495.

2. **The published swing is not `2 * kMaxDamperOctaves`.** The published quantity is
   `clamp(3*s, ±1.5)` (B-3's expression), so inside the unclamped region `|s| <= 0.5` while the
   smoother's *target* may sit at `±1` (`outputTarget() = clamp(depth_ * x_, -1, 1)`,
   `brownian_drift.h:249-251`; the output clamp at `:212-214`). The largest single-chunk approach is
   therefore `alpha * (0.5 + 1.0)` in `s`, which in octaves is
   `alpha * (kMaxDamperOctaves/kInternalStd + kMaxDamperOctaves) = alpha * 4.5`, not `alpha * 3.0`.

**The corrected bounds** at 48 kHz, `damperDepth 1`, `damperRate 1` — exactly SC-003's pinned
configuration, i.e. where a *correct* implementation would have gone red against the spec's numbers:

| Quantity | Spec literal | Corrected |
|---|---|---|
| per-chunk approach fraction `alpha` | 0.0088495 | **0.0434712** |
| published swing factor | 3.0 (`2·kMaxDamperOctaves`) | **4.5** (`kMaxDamperOctaves/kInternalStd + kMaxDamperOctaves`) |
| SC-003 (a) `kMaxOffsetStepPerChunk` | 0.026549 | **0.195621** octaves/chunk |
| SC-003 (b) `0.25499 × kMaxOffsetStepPerChunk` | 0.00677 | **0.049880** |

This is **not** a threshold relaxed to fit an implementation: the spec's own derivation is wrong about
a shipped header's documented semantics, and the corrected figure is still a hard smoother property,
not a statistical one — it binds at every chunk, on every line, at the worst-case rate.
**FR-082's stop-and-surface rule is discharged here rather than silently:** the two literals at
`spec.md:931-941` (0.02655) and `:951` (0.00677) are to be corrected to **0.19562** and **0.04988**
when this plan is accepted.

The test writes no literal at all — it writes the law:

```cpp
// smoother.h:77-93 - smoothTimeMs is the time to 99 %, so coeff = exp(-5000/(ms*sr))
// and the per-chunk approach fraction is 1 - coeff^kControlChunkSamples.
const float alpha = 1.0f - std::pow(
    calculateOnePolCoefficient(BrownianDrift::kDriftOutputSmoothMs, static_cast<float>(sr)),
    static_cast<float>(CavernVerb::kControlChunkSamples));
const float kMaxOffsetStepPerChunk =
    alpha * (CavernVerb::kMaxDamperOctaves / BrownianDrift::kInternalStd
             + CavernVerb::kMaxDamperOctaves);
```

**Recommended ruling, unchanged by the arithmetic: `getDamperOffsetOctaves` returns the POST-clamp
value** — what is actually published to `AetherReverb`, and what Phase 13 will visualise — and
FR-037's parenthetical "pre-clamp" is struck. It is also the tighter of the two readings: a pre-clamp
accessor swings over `±3.0` and its bound is
`alpha * 2 * kMaxDamperOctaves/kInternalStd = alpha * 6.0 = 0.260827`. Either resolution is one line;
the implementer must not silently pick one, and the parameterised expression above keeps the choice
visible in the source.

The same 5× time-constant error appears once more in this plan — S5.2's rejection of
`makeLinearTap` costed the ER-size zipper at "4.6 ms per chunk". Corrected there to **11.4 ms per
chunk**, which strengthens rather than changes that decision.

### B-3 (ambiguity, non-blocking). `setDamperDepth` must be applied **once**

FR-031 states the scale as `offset_i = v * kMaxDamperOctaves * (drift.getCurrentValue()/kInternalStd)`
**and** states that `v` is routed through `BrownianDrift::setDepth`. `getCurrentValue()` already
carries the depth — `outputTarget() = clamp(depth_ * x_, -1, 1)` is what the 150 ms smoother chases
(`brownian_drift.h:249-251`, `:212-214`). Applying both would square the depth. **Ruling taken in this
plan: depth is applied exactly once, by `setDepth`**, and the emitted expression is

```
offset_i = clamp( (kMaxDamperOctaves / kInternalStd) * drift_i.getCurrentValue(),
                  -kMaxDamperOctaves, +kMaxDamperOctaves )
```

This is the reading the Clarifications session fixed ("routed through `BrownianDrift::setDepth` so
depth automation is ramped click-free by the 150 ms output smoother"), and the only one under which
SC-003 (d)'s depth-step clause is click-free by construction. SC-005 (b)'s Spearman ordering is
unaffected (a monotone reparameterisation of depth); SC-005 (a)'s 3× endpoint separation is *helped*
by the linear rather than quadratic law.

---

## S1. Architecture at a glance

```
 in L/R --+--------------------------------------------- dry L/R --> [alignL/R: latency] --+
          |                                                                                |
          +--> 0.5*(L+R) --> [ erLine_ : mono DelayLine, maxEarlySeconds ]                 |
                               |                                                           |
                               +- tap 0..11  readLinear(d_i(k)) --> per-tap one-pole ------+
                               |     (size-scaled, per-sample ramp)  (absorption, fc_i)    |
                               |                                                           |
                               +--> erL / erR  (side rule, gains g_i) --> [alignL/R] ------+
                               |                                                           |
                               +--> erMono = SUM(post-absorption taps) * earlySend --+      |
                                                                                     v      v
                      AetherReverb (owned BY VALUE; mix = 1, preDelay = 0,      +-----------------+
                      shimmer/bloom NOT constructed, size/damping dark)  ------>| equal-power     |
                            ^                                                   | dry/wet + level |--> out L/R
                            |  setDamperOffsetsOctaves(offsets, N)  [per 64]    +-----------------+
                  BrownianDrift damper_[0..N-1] --> octave offsets
```

Ownership and cadence:

- One `AetherReverb` **by value**; `CavernVerb` is non-copyable, movable (FR-002).
- `CavernVerb` owns an absolute `sampleCounter_` started at `prepare()`/`reset()` and slices every
  `processStereoBlock` call at `kControlChunkSamples = 64` boundaries, exactly as the owned engine
  does (`aether_reverb.h:2185-2198`). At each boundary it advances its own modulators and smoothers,
  publishes the damper offsets, and then calls the owned engine with that sub-block. Because the two
  counters start together and consume the same contiguous stream, the two control grids are
  phase-aligned by construction (FR-007, P-2, SC-011).
- `prepare()` is the only allocating method (FR-003).

---

## S2. `CavernVerb` — header, constants, API, state

### S2.1 File, banner, includes (FR-001)

`dsp/include/krate/dsp/effects/cavern_verb.h`, `#pragma once`, house banner naming Layer 4, the spec
slug `vorago-phase9-cavern-space`, the roadmap lines (410–430) and the Open-Question-3 ruling.

```cpp
#include <krate/dsp/effects/aether_reverb.h>       // same-layer, permitted (tools/lint-layers.js:74)
#include <krate/dsp/primitives/delay_line.h>       // L1
#include <krate/dsp/primitives/smoother.h>         // L1  (OnePoleSmoother, LinearRamp)
#include <krate/dsp/processors/brownian_drift.h>   // L2
#include <krate/dsp/core/random.h>                 // L0  (deriveStreamSeed)

#include <algorithm>  // clamp, min, fill
#include <cmath>      // exp, exp2, pow, cos, sin
#include <cstddef>
#include <cstdint>
```

No Vorago Phase 1–8 header. No `effects/fdn_reverb.h`. No `std::isnan`/`isinf`/`isfinite` anywhere
(FR-071) — finiteness goes through a file-local `ITERUM_NOINLINE isFinite()` composed from
`detail::isNaN`/`detail::isInf` (`core/db_utils.h`), mirroring `aether_reverb.h:2937`. The
`ITERUM_NOINLINE` is load-bearing, not style: without it the guard folds away under `-ffast-math` on
the macOS leg.

### S2.2 Public constants (all `static constexpr`, class scope, `kPascalCase`)

```cpp
// --- cadence, mirrored from the owned engine (FR-084) ---
static constexpr std::size_t kControlChunkSamples = AetherReverb::kControlChunkSamples;  // 64

// --- dark tuning (FR-012 .. FR-014) ---
static constexpr float kCavernSizeFloor       = 0.55f;  // FR-012, >= 0.55 (SC-007 (c))
static constexpr float kCavernDampingFloor    = 0.50f;  // FR-013
static constexpr float kCavernDecayMinSeconds = 0.5f;   // mirrors the PRIVATE :2735; SC-016 (a) pins it
static constexpr float kCavernDecayMaxSeconds = 60.0f;  // mirrors the PRIVATE :2736
static constexpr std::size_t kMaxChannels     = 16;     // mirrors the PRIVATE :2725; SC-016 (b) pins it

// --- early reflections (FR-020 .. FR-028) ---
static constexpr std::size_t kEarlyTapCount = 12;
static constexpr std::size_t kEarlyTapSeries[kEarlyTapCount] =
    {1u, 53u, 199u, 277u, 547u, 709u, 929u, 1049u, 1381u, 1583u, 1721u, 1997u};
static constexpr float kEarlyFirstArrivalFloorMs = 60.0f;
static constexpr float kDefaultEarlySizeMs       = 220.0f;
static constexpr float kEarlySizeMinMs           = 80.0f;
static constexpr float kEarlySizeMaxMs           = 600.0f;
static constexpr float kEarlyGainG0              = 0.5f;
static constexpr float kEarlyGainAlphaPerMs      = 0.01f;
static constexpr float kEarlyGainSum             = 1.865762f;  // Sum|g_i|; static_assert'd <= 2.0
static constexpr std::size_t kIncommensurabilityOrder = 6;     // S0.2 B-1 ruling
static constexpr float kEarlyIncommensurabilityTol    = 0.05f;
static constexpr float kEarlyAbsorptionFcMaxHz = 18000.0f;
static constexpr float kEarlyAbsorptionFcMinHz =  1200.0f;
// The Nyquist guard is PART OF THE LAW, not an implementation detail (S5.3, S14 Q5), so it is
// named here and the test reads the same two constants the header does:
static constexpr float kEarlyAbsorptionNyquistFraction = 0.45f;  // fcMax = min(18 kHz, 0.45*sr)
static constexpr float kEarlyAbsorptionSpanFraction    = 0.40f;  // fcMin = min(1200, 0.40*fcMax)

// --- dampers (FR-030 .. FR-034) ---
static constexpr float kMaxDamperOctaves   = 1.5f;
static constexpr float kDefaultDamperDepth = 0.35f;
static constexpr float kDefaultDamperRate  = 0.15f;   // smoothness 0.85 -> tau ~= 25.5 s
static constexpr std::size_t kCavernReverbSalt     = 64;  // clear of {0..4} and [16,24)
static constexpr std::size_t kCavernDamperSaltBase = 96;  // 96 .. 111

// --- FR-066's pinned default table ---
static constexpr float kDefaultSize            = 0.50f;
static constexpr float kDefaultDarkness        = 0.80f;
static constexpr float kDefaultDecaySeconds    = 20.0f;
static constexpr float kDefaultDensity         = 0.75f;
static constexpr float kDefaultDimensionality  = 0.50f;
static constexpr float kDefaultBreath          = 0.50f;
static constexpr float kDefaultFog             = 0.30f;
static constexpr float kDefaultEarlyLevel      = 0.80f;
static constexpr float kDefaultEarlyAbsorption = 0.60f;
static constexpr float kDefaultEarlySend       = 0.70f;
static constexpr float kDefaultWidth           = 1.00f;
static constexpr float kDefaultMix             = 1.00f;
static constexpr float kDefaultMaxEarlySeconds = 0.30f;

// --- smoothing ---
static constexpr float kEarlySizeSmoothingMs  = 300.0f;  // matches the engine's own Size smoother
static constexpr float kAbsorptionSmoothingMs = 100.0f;
static constexpr float kGateRampMs            = 50.0f;   // FR-065 (iii)'s >= 50 ms re-entry
```

FR-084 `static_assert`s — **public facts only**, because the three private mirrors are unreachable
from `CavernVerb` and are pinned at runtime by SC-016 instead:

```cpp
static_assert(kControlChunkSamples == AetherReverb::kControlChunkSamples);
static_assert(AetherReverb::kSizeScaleMin == 0.25f && AetherReverb::kSizeScaleMax == 4.0f);
static_assert(AetherReverb::kMinSampleRate == 8000.0f && AetherReverb::kMaxSampleRate == 192000.0f);
static_assert(AetherReverb::kDriftSaltBase == 16);
static_assert(kCavernDamperSaltBase >= AetherReverb::kDriftSaltBase + (kMaxChannels / 2u) + 8u);
static_assert(AetherReverb::kRefDelays8[0] == 967u);     // SC-006's onset-gap arithmetic
static_assert(kEarlyGainSum <= 2.0f);                    // FR-028
static_assert(kMaxDamperOctaves <= AetherReverb::kMaxDamperOffsetOctaves);  // FR-040
static_assert(kCavernSizeFloor >= 0.55f);                // FR-012 / SC-007 (c)
```

Table asserts (FR-020, FR-022, FR-028) via `constexpr` helpers in a `detail` namespace:

```cpp
static constexpr float earlyTapDelayMsAtDefaultSize(std::size_t i) noexcept;  // the affine law
static_assert(detail::cavernTableStrictlyAscending());
static_assert(detail::cavernTablePairwiseCoprime());
static_assert(detail::cavernTableIncommensurabilityMin() >= kEarlyIncommensurabilityTol);
```

The first three are exact integer/ratio arithmetic and are genuinely `constexpr`. The **gain-sum**
assert needs `exp`, which is not `constexpr`; it is therefore either (i) written against a small
range-reduced `constexpr expApprox` whose agreement with `std::exp` to `1e-5` is itself asserted in
the geometry test, or (ii) moved into the geometry `TEST_CASE` as a `REQUIRE` reporting the measured
sum. (ii) is permitted by FR-022's own "otherwise by a table-driven test" clause; (i) is preferred.
This is a recorded choice, not a silent drop.

### S2.3 `PrepareConfig` (FR-004)

```cpp
struct PrepareConfig {
    std::size_t   numChannels     = 8;      // 8 or 16, forwarded
    std::size_t   maxBlockSamples = 2048;   // clamped [64, 8192], forwarded
    float         maxEarlySeconds = 0.30f;  // clamped [kEarlySizeMinMs*0.001, 0.60]; sizes the ER line
    float         maxDelaySeconds = 0.50f;  // forwarded (the engine clamps to [0.05, 1.0])
    bool          spectralDiffusionEnabled = true;
    std::size_t   diffusionFftSize = 1024;  // forwarded; the engine clamps + bit_floors
    std::uint32_t seed             = 1;
};
```

Every field clamped in place, never rejected (FR-003). Designated-initialiser friendly with no
narrowing (FR-072): every member's default literal has the member's own type.

### S2.4 Public API — the complete shape the implementer types

```cpp
class CavernVerb {
public:
    CavernVerb() noexcept = default;
    ~CavernVerb() noexcept = default;
    CavernVerb(const CavernVerb&) = delete;
    CavernVerb& operator=(const CavernVerb&) = delete;
    CavernVerb(CavernVerb&&) noexcept = default;
    CavernVerb& operator=(CavernVerb&&) noexcept = default;

    // --- lifecycle --------------------------------------------------------
    void prepare(double sampleRate, const PrepareConfig& config) noexcept;  // ONLY allocator
    void reset() noexcept;
    void silence() noexcept;
    void processStereoBlock(const float* inLeft, const float* inRight,
                            float* outLeft, float* outRight,
                            std::size_t numSamples) noexcept;

    // --- the seventeen controls (FR-061), in the spec's order --------------
    void setSize(float v) noexcept;            // [0,1] -> engine size [kCavernSizeFloor, 1]
    void setDarkness(float v) noexcept;        // [0,1] -> engine damping [kCavernDampingFloor, 1]
    void setDecaySeconds(float seconds) noexcept;  // [0.5, 60]
    void setDensity(float v) noexcept;
    void setDimensionality(float v) noexcept;
    void setBreath(float v) noexcept;          // -> sizeBreathDepth AND dimensionalityTideDepth
    void setFog(float v) noexcept;             // -> spectral diffusion (phase smear, not damping)
    void setEarlySizeMs(float ms) noexcept;    // [80, min(600, maxEarlySeconds*1000)]
    void setEarlyLevel(float v) noexcept;      // [0,1], zero-gated (FR-065)
    void setEarlyAbsorption(float v) noexcept; // [0,1], 0 == exact bypass by assignment
    void setEarlySend(float v) noexcept;       // [0,1], zero-gated (FR-065)
    void setDamperDepth(float v) noexcept;     // [0,1] -> BrownianDrift::setDepth on every damper
    void setDamperRate(float v) noexcept;      // [0,1] -> setSmoothness(1 - v) on every damper
    void setFreeze(bool on) noexcept;
    void setWidth(float v) noexcept;           // late field only
    void setMix(float v) noexcept;             // [0,1] equal-power, zero-gated (FR-065)
    void setSeed(std::uint32_t seed) noexcept;

    // --- introspection (FR-008, FR-009, FR-027, FR-037, FR-062) -----------
    [[nodiscard]] bool isPrepared() const noexcept;
    [[nodiscard]] bool isFrozen() const noexcept;        // forwarded: latch-complete only (:2493)
    [[nodiscard]] bool isShimmerActive() const noexcept; // forwarded; always false (FR-010)
    [[nodiscard]] float getEffectiveDelayLengthSamples(std::size_t channel) const noexcept;
    [[nodiscard]] float getModalDensityPerHz() const noexcept;
    [[nodiscard]] float getMaxSizeScale() const noexcept;
    [[nodiscard]] float getStateEnergy() const noexcept;
    [[nodiscard]] std::size_t getNonFiniteRecoveryCount() const noexcept;
    [[nodiscard]] bool isRecovering() const noexcept;
    [[nodiscard]] std::size_t getLatencySamples() const noexcept;
    [[nodiscard]] std::size_t getAllocatedBytes() const noexcept;  // CavernVerb's own buffers only

    [[nodiscard]] std::size_t getEarlyTapCount() const noexcept;                      // 12
    [[nodiscard]] float getEarlyTapDelaySamples(std::size_t tap) const noexcept;       // size-scaled
    [[nodiscard]] float getEarlyTapGain(std::size_t tap) const noexcept;               // static g_i
    [[nodiscard]] float getEarlyTapAbsorptionCutoffHz(std::size_t tap) const noexcept; // fc_i
    [[nodiscard]] float getDamperOffsetOctaves(std::size_t line) const noexcept;       // published
};
```

Out-of-range indices return `0.0f` everywhere (the `getEffectiveDelayLengthSamples` idiom,
`aether_reverb.h:2506-2509`); `getEarlyTapAbsorptionCutoffHz` returns the effective `fcMax` for every
in-range tap while absorption is bypassed at `v == 0`.

**Deliberately absent** (FR-011, FR-019): every shimmer and bloom control, `bloomNoteOn`/`bloomNoteOff`,
`setPreDelayMs`, `setModDepth`, `setModSmoothness`.

### S2.5 Private state layout

```cpp
private:
    struct EarlyTap {
        float gain      = 0.0f;   // g_i, static (FR-027 reports THIS, unaffected by size/absorption)
        float delayMs   = 0.0f;   // d_i at the DEFAULT ER size
        float fcHz      = 0.0f;   // current absorption cutoff (FR-025)
        float a         = 0.0f;   // one-pole coefficient for fcHz
        float state     = 0.0f;   // one-pole state - ONE PER TAP, not shared
        bool  rightSide = false;  // FR-020's even->L / odd->R rule
    };

    /// Shaped, per-sample, >= 50 ms control ramp (FR-065 (iii), FR-023's C1 requirement).
    ///   value = from + smoothstep(r) * (to - from)
    /// Exact at BOTH endpoints (so the control law is undistorted when settled) and zero
    /// derivative at both (the aether_reverb.h:4237-4241 shaping, for the measured reason at
    /// :4270-4281). A re-target to the SAME value is dropped, so a per-block setter cannot stall
    /// the ramp - the feedback_ecology.h:2290-2296 failure the spec's Edge Cases cite.
    struct ShapedRamp {
        LinearRamp r;                 // 0 -> 1 over kGateRampMs
        float from = 0.0f, to = 0.0f;
        void  configure(float sr) noexcept;
        void  snapTo(float v) noexcept;         // from = to = v; r.snapTo(1)
        void  setTarget(float v) noexcept;      // no-op when v == to
        [[nodiscard]] float process() noexcept; // advances r by ONE sample, returns the shaped value
        [[nodiscard]] float current() const noexcept;
        [[nodiscard]] bool  settledAtZero() const noexcept;  // to == 0 && r.isComplete()
    };

    // --- prepared configuration ---
    bool prepared_ = false;
    bool anySamplesProcessed_ = false;
    double sampleRate_ = 48000.0;
    std::size_t numChannels_ = 8;
    std::size_t alignSamples_ = 0;        // == engine_.getLatencySamples()
    float maxEarlySeconds_ = kDefaultMaxEarlySeconds;
    float msToSamples_ = 48.0f;           // sampleRate_ / 1000
    std::uint32_t seed_ = 1;
    std::uint64_t sampleCounter_ = 0;

    AetherReverb engine_;                 // owned BY VALUE (FR-002)

    // --- ER stage ---
    DelayLine erLine_;                    // MONO (FR-020)
    EarlyTap  taps_[kEarlyTapCount]{};
    OnePoleSmoother erSizeSm_;            // ms, 300 ms
    OnePoleSmoother absorptionSm_;        // [0,1], 100 ms
    float erSizeMsCurrent_ = kDefaultEarlySizeMs;
    float chunkTapDelayStart_[kEarlyTapCount]{};  // samples, at the chunk's first sample
    float chunkTapDelayEnd_[kEarlyTapCount]{};    // samples, one past the chunk's last
    bool  absorptionBypass_ = false;      // exact bypass at v == 0 (FR-025)
    bool  erChainSkipped_ = false;        // FR-065 dormancy of the tap loop

    ShapedRamp earlyLevel_, earlySend_, mix_;

    // --- alignment (FR-062): FOUR mono lines, dry L/R and ER L/R ---
    DelayLine dryAlignL_, dryAlignR_, erAlignL_, erAlignR_;

    // --- dampers ---
    BrownianDrift damper_[kMaxChannels];
    float damperOffset_[kMaxChannels]{};  // published octaves (post-clamp; S0.2 B-2)
    float damperDepth_ = kDefaultDamperDepth;
    float damperRate_  = kDefaultDamperRate;

    // --- scratch: exactly one control chunk, never sized by maxBlockSamples ---
    float erScratchL_[kControlChunkSamples]{};
    float erScratchR_[kControlChunkSamples]{};
    float sendScratch_[kControlChunkSamples]{};
    float engineOutL_[kControlChunkSamples]{};
    float engineOutR_[kControlChunkSamples]{};
    float dryScratchL_[kControlChunkSamples]{};
    float dryScratchR_[kControlChunkSamples]{};

    // --- shadow copies of every control, so prepare()/reset() can re-apply them (FR-060) ---
    float ctlSize_, ctlDarkness_, ctlDecay_, ctlDensity_, ctlDim_, ctlBreath_, ctlFog_,
          ctlEarlySizeMs_, ctlEarlyLevel_, ctlAbsorption_, ctlEarlySend_, ctlWidth_, ctlMix_;
    bool  ctlFreeze_ = false;
```

A slice never exceeds one control chunk by construction (S3.3), so the seven scratch arrays are the
size, not an upper bound: 7 × 64 × 4 B = 1 792 B of member storage, no heap, no VLA.

---

## S3. Lifecycle and the process loop

### S3.1 `prepare(double sampleRate, const PrepareConfig& config)` — numbered order

1. `sampleRate_ = clamp(sampleRate, kMinSampleRate, kMaxSampleRate)` — the engine's own clamp
   (`:1615-1616`), re-applied here so the ER geometry derives from the same number the engine uses.
   `msToSamples_ = float(sampleRate_) * 0.001f`.
2. `numChannels_ = (config.numChannels == 16u) ? 16u : 8u`;
   `maxEarlySeconds_ = clamp(config.maxEarlySeconds, kEarlySizeMinMs * 0.001f, 0.60f)` — the floor is
   **0.08 s, not 0.05 s**. `setEarlySizeMs` clamps into `[kEarlySizeMinMs, hi]` with
   `hi = min(kEarlySizeMaxMs, maxEarlySeconds_*1000)` (S5.2); a prepared `maxEarlySeconds_` in
   `[0.05, 0.08)` makes `hi < lo` (e.g. 50 ms < 80 ms), which violates `std::clamp`'s precondition
   (UB, and a hard `_STL_VERIFY` abort under MSVC `_ITERATOR_DEBUG_LEVEL`) and would collapse every
   tap below FR-021's 60 ms first-arrival floor. The ER line must always be able to hold the minimum
   admissible pattern, so 0.08 s is the recorded floor. S5.2 additionally writes its own upper bound
   with a `max`, so neither guard is left implicit.
3. Build and prepare the owned engine:
   ```cpp
   AetherReverb::PrepareConfig ac{};
   ac.numChannels             = numChannels_;
   ac.maxBlockSamples         = std::clamp(config.maxBlockSamples, std::size_t{64}, std::size_t{8192});
   ac.maxDelaySeconds         = config.maxDelaySeconds;   // the engine clamps to [0.05, 1.0]
   ac.shimmerEnabled          = false;                    // FR-010: NOT CONSTRUCTED, not zeroed
   ac.bloomEnabled            = false;                    // FR-010
   ac.spectralDiffusionEnabled= config.spectralDiffusionEnabled;
   ac.diffusionFftSize        = config.diffusionFftSize;
   ac.seed                    = deriveStreamSeed(config.seed, kCavernReverbSalt);  // FR-034
   engine_.prepare(sampleRate_, ac);
   engine_.setMix(1.0f);         // D-2: the owned engine is permanently fully wet
   engine_.setPreDelayMs(0.0f);  // FR-019: the cavern's pre-delay IS the ER geometry
   ```
   `maxBlockSamples` is forwarded **unchanged** even though the engine only ever receives <= 64
   samples: it is part of the documented surface and sizes engine-internal scratch; passing a smaller
   value would be a silent behaviour change under a future engine edit.
   Consequence of `shimmerEnabled = false`, verified: `shimmerAllocated_ = config.shimmerEnabled &&
   (sr >= 44100)` (`:1631`), so no `PitchShiftProcessor` is prepared and `isShimmerActive()` is false
   — which is also why `CavernVerb` has **no 44.1 kHz floor** (FR-074).
4. `alignSamples_ = engine_.getLatencySamples()` (`:2612`).
5. `erLine_.prepare(sampleRate_, maxEarlySeconds_ + 4.0f / float(sampleRate_))` — **four samples of
   headroom above the largest reachable tap**, because `readLinear` clamps at `maxDelaySamples_` and
   takes `index1 = min(index0+1, maxDelaySamples_)` (`delay_line.h:302-318`): at
   `setEarlySizeMs(maxEarlySeconds_*1000)` the last tap would otherwise sit exactly on the clamp and
   lose its interpolation partner.
6. Alignment lines: **all four are prepared unconditionally**, each as
   `prepare(sampleRate_, float(alignSamples_ + 4) / float(sampleRate_))`. `alignSamples_ == 0` is a
   legal argument and costs 8 floats per line after the `nextPowerOf2` rounding
   (`delay_line.h:267-279`). Preparing unconditionally is load-bearing, not tidy: S5.4's render block
   writes and reads all four lines on **every** sample; `DelayLine::write` is
   `buffer_[writeIndex_] = sample` and `read` indexes `buffer_[readIndex]` with no emptiness check
   (`primitives/delay_line.h:287-299`), so an unprepared line (empty `buffer_`, `mask_ == 0`) is an
   out-of-bounds heap write on the audio thread for the whole render whenever
   `spectralDiffusionEnabled == false` — a configuration SC-013 explicitly renders. There is
   therefore **no** "copy directly when `alignSamples_ == 0`" branch anywhere in this plan.
7. Build the tap table: for `i < kEarlyTapCount`,
   `delayMs = 60 + 160*(n_i - n_0)/(n_11 - n_0)`,
   `gain = kEarlyGainG0 * std::exp(-kEarlyGainAlphaPerMs * delayMs)`,
   `rightSide = ((i & 1u) != 0u)`, `state = 0`, coefficients recomputed in step 10's refresh.
8. Smoothers: `erSizeSm_.configure(kEarlySizeSmoothingMs, sr)`,
   `absorptionSm_.configure(kAbsorptionSmoothingMs, sr)`,
   `earlyLevel_/earlySend_/mix_.configure(sr)`.
9. Dampers: for `i < kMaxChannels` (**every slot, not only `numChannels_`**, so a later `prepare()` at
   N = 16 cannot inherit a stale stream — the `reseedStreams()` reasoning at `:2993-2995`):
   `damper_[i].prepare(sampleRate_)` (allocation-free, `brownian_drift.h:121`),
   `setDepth(damperDepth_)`, `setSmoothness(1.0f - damperRate_)`.
10. `prepared_ = true`; re-apply every shadowed control (FR-060); `reseed()` (S3.4); `reset()`.

Allocation ledger (FR-009, FR-075) at P-1 (48 kHz, `maxEarlySeconds = 0.30`, FFT 1024):

| Buffer | Samples (power-of-two) | Bytes |
|---|---|---|
| `erLine_` (above 14 404) | 16 384 | 65 536 |
| 4 × alignment lines (above 1 029) | 4 × 2 048 | 32 768 |
| **`getAllocatedBytes()`** | | **98 304 (96 KiB)** |

Against the owned engine's own 432 KiB `delayBuffer_` + 128 KiB pre-delay pair (`:480`) this is the
"small fraction" FR-075 asks for. At 192 kHz / `maxEarlySeconds = 0.60` / FFT 4096 it is
`131 072 + 4 × 8 192` samples = 640 KiB — **reported only**. SC-015 must **not** assert linearity in
rate, and the earlier wording here that said it did was wrong: the buffers are power-of-two quantised
and the four alignment lines are sized in *samples* of engine latency, which is rate-independent.
48 kHz / FFT 1024 / 0.30 s gives 24 576 samples; 192 kHz / FFT 4096 / 0.60 s gives 163 840 — a 6.67×
step for a 4× rate change. Holding the config fixed, 48 → 96 kHz is 1.67×, and 0.30 s and 0.35 s at
48 kHz both round to the same 16 384. If a bound is wanted, SC-015 asserts the **formula** —
`getAllocatedBytes()` within `[1×, 2×]` of
`4 * (nextPowerOf2(sr*maxEarlySeconds + 5) + 4*nextPowerOf2(alignSamples + 5))` — never linearity.

### S3.2 `reset()` and `silence()` (FR-006)

`reset()`: `sampleCounter_ = 0`; `anySamplesProcessed_ = false`; `engine_.reset()`; then
**re-apply the complete shadow set to the owned engine, exactly as `prepare()` step 10 does, so the
two lifecycle paths cannot diverge**. `engine_.setFreeze(ctlFreeze_)` is the one re-issue that is not
merely tidy: `AetherReverb::reset()` unconditionally clears the latch (`freezeTarget_ = false`,
`aether_reverb.h:1979`), and freeze is **not** one of the FR-009 control targets its doc comment
promises to preserve (`:1958-1963`). Without it a frozen `CavernVerb` silently thaws on `reset()`
while its own `ctlFreeze_` still reads `true`, the forwarded `isFrozen()` disagrees with the class's
recorded state, and nothing ever restores it — no setter re-issues freeze, so only a fresh host
`setFreeze(true)` would. Then `erLine_.reset()`;
all four alignment lines `reset()`; every `taps_[i].state = 0`; `erSizeSm_.snapToTarget()`,
`absorptionSm_.snapToTarget()`, `erSizeMsCurrent_ = erSizeSm_.getCurrentValue()`;
`earlyLevel_/earlySend_/mix_.snapTo(their target)`; `reseed()` (S3.4); then one `refreshControlState()`
so a post-reset render starts fully materialised (the engine does the same at `:2099`).
Allocation-free, `noexcept`, and documented as **not** an audio-thread operation (the engine's own
wording, `:1966-1970`).

`silence()`: forwards to `engine_.silence()` — which owns the 20 ms gate and the amortized clear —
and clears `CavernVerb`'s own **feed-forward** audio state: the ER line and the 12 tap one-pole
states. The four alignment lines are **NOT cleared**, for the reason the engine records verbatim for
its own `dryAlignL_/R_` at `:3625-3639`: those lines carry *input history*, nothing recirculates
through them, and clearing them punches an `fftSize`-long hole that ends in a full-amplitude step
after the gate has already returned to unity — the single-sample discontinuity SC-008 (b) forbids.
This reasoning is copied into the header so it cannot be "tidied away" — and, unlike before this
revision, it is now **gated**: `CavernVerb_SilenceContract` (S10.4a) calls `silence()` mid-render
with the spectral stage enabled (so `alignSamples_ > 0`) and requires zero P-5 detections over the
following second plus `getStateEnergy() ≈ 0`. SC-008 never calls `silence()` — its pinned timeline is
`2 s G-2 → [freeze, 5 s G-3, thaw, 3 s G-2] × 10` (spec.md:1088) — so the R-14 mitigation that used
to point at SC-008 (b) was false, and this case replaces it.

### S3.3 `processStereoBlock` — the exact loop

```cpp
void processStereoBlock(const float* inL, const float* inR, float* outL, float* outR,
                        std::size_t n) noexcept {
    if ((inL == nullptr) || (inR == nullptr) || (outL == nullptr) || (outR == nullptr)) return; // FR-005
    if (n == 0u) return;                                  // no state advances (:2171-2173)
    if (!prepared_) { std::fill(outL, outL + n, 0.0f); std::fill(outR, outR + n, 0.0f); return; }

    std::size_t done = 0;
    while (done < n) {
        const auto phase = static_cast<std::size_t>(sampleCounter_ % kControlChunkSamples);
        if (phase == 0u) { runControlStep(); }            // FR-007: ABSOLUTE boundaries only
        const std::size_t slice = std::min(n - done, kControlChunkSamples - phase);
        renderSlice(inL + done, inR + done, outL + done, outR + done, slice, phase);
        sampleCounter_ += slice;
        done += slice;
    }
    anySamplesProcessed_ = true;
}
```

`runControlStep()`, order normative (mirrors `aether_reverb.h:3871-3913`):

1. **Dampers advance unconditionally** (FR-036), each by a **full** `kControlChunkSamples`:
   `for (i < numChannels_) damper_[i].processBlock(kControlChunkSamples);`. Never by a slice length —
   `processBlock(36) + processBlock(28)` is not the same state as `processBlock(64)`
   (`brownian_drift.h:194-206`). This is what makes SC-011's partition invariance structural, and it
   runs while frozen and on digital silence (FR-036, the Dormancy rule's clause (ii)).
2. `absorptionSm_.advanceSamples(kControlChunkSamples)`.
   **`erSizeSm_` is deliberately NOT advanced here.** It is advanced **exactly once per control
   chunk**, inside step 4's `refreshControlState()`, where `prevMs`/`nextMs` are captured around it
   (S5.2). Advancing it in both places would step the 300 ms smoother by 128 samples per 64-sample
   chunk (`OnePoleSmoother::advanceSamples` is a real closed-form advance, `smoother.h:243-254`):
   the effective ER-size smoothing time would halve to ~150 ms, **and** `chunkTapDelayStart_[i]`
   (derived from `prevMs`) would no longer equal the delay actually used at the previous chunk's last
   sample, putting a step at every chunk boundary — precisely the zipper R-6 and FR-023 exist to
   prevent.
   `earlyLevel_`, `earlySend_` and `mix_` are per-sample and are deliberately **not** advanced here.
3. Compute and publish the damper offsets (S6.2), then
   `engine_.setDamperOffsetsOctaves(damperOffset_, numChannels_)` — **before** the owned engine's own
   control step, which happens inside step 5's `engine_.processStereoBlock` call at the same absolute
   boundary.
4. `refreshControlState()`: `chunkTapDelayStart_/End_` (S5.2), the absorption coefficients (S5.3), the
   ER-chain skip decision (S5.5), and the per-chunk denormal flush of the tap states (S9).
5. (inside `renderSlice`) one `engine_.processStereoBlock(...)` call per sub-block.

A trailing partial sub-block (because the caller's block ran out mid-chunk) runs **no** control step
and publishes **no** new offset; the next call resumes at the same `phase` (FR-007's stated rule).

### S3.4 Seeding (FR-034, FR-035)

```cpp
void reseed() noexcept {
    engine_.setSeed(deriveStreamSeed(seed_, kCavernReverbSalt));        // :2361
    for (std::size_t i = 0; i < kMaxChannels; ++i) {
        damper_[i].setSeed(deriveStreamSeed(seed_, kCavernDamperSaltBase + i));
        damper_[i].reset();   // setSeed stores + re-seeds the RNG; reset() is what rewinds the walk
    }
}
void setSeed(std::uint32_t s) noexcept { seed_ = s; reseed(); }
```

Separation argument, checked against the engine's own derivation: the engine builds its delay-jitter
streams as `deriveStreamSeed(engineSeed, kDriftSaltBase + j)` (`:2986`) where
`engineSeed = deriveStreamSeed(seed_, 64)` — a different base **and** a disjoint salt range from the
dampers' `deriveStreamSeed(seed_, 96 + i)`. SC-004 asserts both separations (S10.4). Seed 0 is a valid
distinct configuration: `deriveStreamSeed` substitutes a non-zero value (`core/random.h:110-116`).

The ER stage uses **no** RNG (FR-035, D-3); the doxygen says so, so a later reader does not add one.

---

## S4. Dark tuning — the exact mappings (FR-010 – FR-019)

| Control | Mapping onto the owned engine | Check |
|---|---|---|
| `setSize(v)` | `engine_.setSize(kCavernSizeFloor + v*(1 - kCavernSizeFloor))` | default 0.50 → 0.775 → `S = 0.25*2^3.1 = 2.1435` (FR-066's "≈ 2.14"); at `v = 0`, `S = 0.25*2^2.2 = 1.1487 = 4.594 × kSizeScaleMin` (SC-007 (c) needs ≥ 4.4) |
| `setDarkness(v)` | `engine_.setDamping(kCavernDampingFloor + v*(1 - kCavernDampingFloor))` | default 0.80 → damping 0.90; `v = 1` → damping 1 → `T60_nyq = 0.05 · T60_dc` (`:2787-2788`, SC-016 (c)) |
| `setDecaySeconds(s)` | `engine_.setDecaySeconds(clamp(s, kCavernDecayMinSeconds, kCavernDecayMaxSeconds))` | mirrors the private `:2735-2736`; a comment cites those lines; SC-016 (a) pins it at runtime |
| `setDensity(v)` | `engine_.setDensity(v)` | default 0.75, at/above the engine's own `kDefaultDensity = 0.70` (`:2732`) |
| `setDimensionality(v)` | `engine_.setDimensionality(v)` | exposed because the morph is the only motion freeze leaves alive (`:3616-3618`) |
| `setBreath(v)` | `engine_.setSizeBreathDepth(v)` **and** `engine_.setDimensionalityTideDepth(v)` | identity, monotone, documented; both unsmoothed by contract (`:2320`, `:2328`) — their targets are already slew-limited internally. **Both targets are gated by `CavernVerb_BreathDualTarget` (S10.4a)**: dropping `setDimensionalityTideDepth` is otherwise invisible — SC-001 clause 1 sets `setDimensionality(1.0)`, which is the morph *target*, not the tide depth, so it would run the easier near-static-matrix configuration and still pass; SC-001 clause 2 and SC-007 (c) zero breath deliberately; and SC-005, the Traceability table's criterion for FR-017, measures damper-driven centroid motion, which the tide does not touch |
| `setFog(v)` | `engine_.setSpectralDiffusion(v)` | documented as a per-bin **phase** smear, not damping (Overview fact 2) |
| `setWidth(v)` | `engine_.setWidth(v)` | late field only; the ER bus never sees it (FR-019) |
| `setFreeze(b)` | `engine_.setFreeze(b)` | idempotent at the engine (`:2225-2229`), so a per-block call cannot stall the latch |

Every setter body is the house form (FR-060): `clamp(isFinite(x) ? x : default, lo, hi)`, store the
shadow copy, then forward or target a smoother; `noexcept`; accepted before `prepare()` and re-applied
by `prepare()`/`reset()`. Smoothers snap rather than ramp while `prepared_ && !anySamplesProcessed_`
(the `applyControl` rule at `:2950-2958`), so "configure then render" means what the caller expects.

---

## S5. The cavern early-reflection stage

### S5.1 Input and topology (FR-020)

Input is sanitised **once, at the top of `renderSlice`, before anything consumes it** (FR-064):

```cpp
const float xl = isFinite(inL[k]) ? inL[k] : 0.0f;   // :4163-4172: a replacement,
const float xr = isFinite(inR[k]) ? inR[k] : 0.0f;   //   never a counter increment
```

and **both** the ER line and the dry scratch are fed from `xl`/`xr`. Sanitising only the dry bus (as
an earlier draft of S5.4 did) is a defect, not a cosmetic asymmetry: the ER is feed-forward in the
*signal*, but S5.4's per-tap one-pole `taps_[i].state += taps_[i].a * (x - taps_[i].state)` is **not**
— it is a first-order IIR, so a single NaN/Inf input sample latches all 12 tap states to NaN
**permanently** (FR-073's flush is `abs(o) < 1e-20f`, which is false for NaN, and nothing else in the
design ever clears them). Output would then be non-finite for the rest of the render while the
forwarded `getNonFiniteRecoveryCount()` — the owned *engine's* counter — reports 0 throughout. The
per-push non-finite sentinel in S10.4 injects NaN and +Inf for one block specifically to gate this.

Per sample: `erLine_.write(0.5f * (xl + xr))` — **one mono line**. All 12 taps read it; the
side rule places them. The ER pattern is therefore invariant to input panning and its width is purely
geometric, so `setWidth(0)` cannot collapse it — both claims are gated by the mono-sum, side-rule and
width clauses of `CavernVerb_EarlyReflectionGeometry` (S10.4), which did not exist before this
revision.

### S5.2 Size scaling and the per-sample delay ramp (FR-023)

`setEarlySizeMs(ms)` clamps to `[kEarlySizeMinMs, hi]` with

```cpp
const float hi = std::max(kEarlySizeMinMs,
                          std::min(kEarlySizeMaxMs, maxEarlySeconds_ * 1000.0f));
```

— the buffer-derived upper bound (the ER never reads past `DelayLine::maxDelaySamples()`), written
with the outer `max` so the pair can never invert. That is belt-and-braces beside S3.1 step 2's 0.08 s
`maxEarlySeconds` floor, because `std::clamp(v, lo, hi)` with `hi < lo` is UB by precondition and
aborts under MSVC's debug iterator checks. It then targets
`erSizeSm_`. At each control step — the **only** place `erSizeSm_` is advanced (S3.3 step 2):

```cpp
const float prevMs = erSizeMsCurrent_;                 // value at the previous chunk boundary
erSizeSm_.advanceSamples(kControlChunkSamples);
const float nextMs = erSizeSm_.getCurrentValue();
const float sPrev = prevMs / kDefaultEarlySizeMs, sNext = nextMs / kDefaultEarlySizeMs;
for (std::size_t i = 0; i < kEarlyTapCount; ++i) {
    chunkTapDelayStart_[i] = taps_[i].delayMs * sPrev * msToSamples_;
    chunkTapDelayEnd_[i]   = taps_[i].delayMs * sNext * msToSamples_;
}
erSizeMsCurrent_ = nextMs;
```

and inside the slice the tap delay is **linearly interpolated per sample**:

```cpp
const float t = static_cast<float>(phase + k) * (1.0f / static_cast<float>(kControlChunkSamples));
const float d = chunkTapDelayStart_[i] + t * (chunkTapDelayEnd_[i] - chunkTapDelayStart_[i]);
const float x = erLine_.readLinear(d);
```

`t` is derived from the absolute phase, so it is partition-invariant (SC-011).

**Why `DelayLine::makeLinearTap` (the documented per-chunk optimisation, `delay_line.h:174-177`) is
NOT used:** it pins the delay for a whole control chunk. `kEarlySizeSmoothingMs = 300` is a **time to
99 %**, not a time constant (`smoother.h:77-93`), so the per-chunk approach fraction is
`1 - exp(-5000·64/(300·48000)) = 0.021969`, and a `setEarlySizeMs(80 → 600)` jump moves the last tap
by `0.021969 × 520 ms ≈ 11.4 ms` **per chunk** — a 548-sample staircase every 1.33 ms at 48 kHz, i.e.
exactly the discontinuity FR-023 forbids and the `aether_reverb.h:4270-4281` precedent warns about.
(The figure recorded here before this revision, 4.6 ms, carried the same 5× time-constant error B-2
corrects; the decision it supports only gets stronger.) The per-sample ramp removes it for one
multiply-add per tap per sample, and turns a size sweep into a smooth glide rather than a stair.
**SC-003 (d)'s `setEarlySizeMs` sweep is the clause that fails if `makeLinearTap` — or any other
per-chunk delay pinning — is used instead** (S10.4).

`getEarlyTapDelaySamples(i)` reports `chunkTapDelayStart_[i]` — the current, size-scaled value at the
chunk boundary, which is what SC-006 (a) and SC-013 (ii) compare arrivals against.

### S5.3 Stone absorption (FR-025)

Per-tap cutoff, geometric in tap index, with the Nyquist guard the low-rate legs need:

```
fcMax = min(kEarlyAbsorptionFcMaxHz, kEarlyAbsorptionNyquistFraction * sr)  // 18 kHz > Nyquist < 40 kHz
fcMin = min(kEarlyAbsorptionFcMinHz, kEarlyAbsorptionSpanFraction * fcMax)
fc_i  = fcMax * pow(fcMin / fcMax, v * i / (n - 1))   // tap 0 stays at fcMax for every v
a_i   = 1 - exp(-2*pi*fc_i / sr)                      // one-pole LP:  y += a * (x - y)
```

**This is a stated deviation from FR-025 as written** and is now carried as S14 open question 5 with a
recommended ruling, rather than only in R-8: `spec.md:440-444` pins `fc_max = 18000` and
`fc_min = 1200` as *the constants of the law*, with no rate dependence, so below 40 kHz the guard
changes the law. The ruling recommended is to amend FR-025 to state the guard as part of the law and
to name `kEarlyAbsorptionNyquistFraction` / `kEarlyAbsorptionSpanFraction` as public constants (S2.2),
so the header, the spec and `CavernVerb_EarlyAbsorption` all read the same law. For the same reason
SC-006 now pins `setEarlyAbsorption` explicitly: its 44.1 kHz arm would otherwise run under a
silently different cutoff set from its 96 kHz arm.

`n = kEarlyTapCount`, **one state per tap**, never one filter shared across taps. Evaluated once per
control chunk and only when `v` moved by more than `1e-7` (the `kJotRecomputeEpsilon` idiom,
`:2785`) — 12 `pow` + 12 `exp` per chunk at worst, none at all while absorption is settled.

**At `v == 0` the filter is an exact bypass by assignment** (FR-025): `absorptionBypass_ = true`, the
one-pole is not evaluated, no coefficient is computed, and `getEarlyTapAbsorptionCutoffHz` reports
`fcMax`. The bypass is a pass-through assignment and **not** `a = 1`, for the FR-024/FR-044 reason: a
path that may carry a stale value must be replaced, never scaled by a coefficient that happens to be
identity. On *leaving* bypass every `taps_[i].state` is zeroed first (it holds history the filter was
not running on); the one-pole converges within a few samples at every admissible `fc`, and the ER
contribution is additionally gated by the shaped ramps whenever a gain was at zero.

The one-pole is non-expansive for every `a ∈ (0, 1]` — the same form and the same argument as
`aether_reverb.h:3085-3086` — so FR-028's `Σ|g_i|` peak bound survives absorption unchanged.

### S5.4 Output, send and alignment (FR-024, FR-026, FR-062)

Per sample `k` of the slice:

```cpp
// FR-064: sanitise ONCE, before ANY consumer - the ER line included (S5.1).
const float xl = isFinite(inL[k]) ? inL[k] : 0.0f;                  // :4163-4172: a replacement,
const float xr = isFinite(inR[k]) ? inR[k] : 0.0f;                  //   never a counter increment
float mono = 0.5f * (xl + xr);
if (std::abs(mono) < 1.0e-20f) { mono = 0.0f; }                     // S9: ER-line denormal guard
erLine_.write(mono);

float erL = 0.0f, erR = 0.0f, erMono = 0.0f;
if (!erChainSkipped_) {
    for (std::size_t i = 0; i < kEarlyTapCount; ++i) {
        float x = erLine_.readLinear(d_i(k));                       // S5.2
        if (!absorptionBypass_) {
            taps_[i].state += taps_[i].a * (x - taps_[i].state);
            x = taps_[i].state;
        }
        const float y = x * taps_[i].gain;
        (taps_[i].rightSide ? erR : erL) += y;
        erMono += y;                                                // FR-026 (ii): the SAME reads
    }
}
sendScratch_[k]  = erMono * earlySend_.process();                   // FR-026 (i)+(iii)
erScratchL_[k]   = erL;    erScratchR_[k] = erR;                    // UNGAINED (level applied post-align)
dryScratchL_[k]  = xl;                                              // FR-064: the SAME sanitised value
dryScratchR_[k]  = xr;                                              //   the ER line was written from
```

Once per slice, the owned engine is excited by the ER sum and nothing else (FR-026 (i): no direct or
unabsorbed path reaches it by any route):

```cpp
engine_.processStereoBlock(sendScratch_, sendScratch_, engineOutL_, engineOutR_, slice);
```

— the **same mono buffer on both inputs** (FR-026 (iii)); the engine's own matrix and per-line drift
are what decorrelate the two channels of the late field. The deliberate consequence is an onset gap:
the first ER arrival at 60 ms plus the shortest line (`kRefDelays8[0] = 967` samples = 20.15 ms at
48 kHz, `:1564`) with `S ≥ 1.1487` (FR-012), i.e. the late field starts ≈ 83 ms after the input.

Then per sample, with the four alignment lines — **always prepared**, S3.1 step 6; there is no
`alignSamples_ == 0` bypass path, because an unprepared `DelayLine` would be written and read
out of bounds here on every sample:

```cpp
// Each ShapedRamp is advanced EXACTLY ONCE PER OUTPUT SAMPLE, never once per channel -
// which is why the three values are hoisted above BOTH channel assignments and both
// assignments are written out in full. A second process() call for R would halve
// kGateRampMs (50 ms -> 25 ms) and break FR-065 (iii)'s ">= 50 ms re-entry" guarantee,
// on exactly the transitions SC-003 (d)'s ClickDetector certifies.
const float mx   = mix_.process();                     // shaped, per sample
const float dryG = std::cos(mx * 1.57079633f);         // equal-power (FR-063)
const float wetG = std::sin(mx * 1.57079633f);
const float lvl  = earlyLevel_.process();              // shaped, per sample
const bool  mixOff = mix_.settledAtZero();
const bool  erOff  = earlyLevel_.settledAtZero();

dryAlignL_.write(dryScratchL_[k]);  const float dL = dryAlignL_.read(alignSamples_);
dryAlignR_.write(dryScratchR_[k]);  const float dR = dryAlignR_.read(alignSamples_);
erAlignL_.write(erScratchL_[k]);    const float eL = erAlignL_.read(alignSamples_);
erAlignR_.write(erScratchR_[k]);    const float eR = erAlignR_.read(alignSamples_);

if (mixOff) {
    outL[k] = dL;                                      // FR-063: ASSIGNMENT, not a x*0 product
    outR[k] = dR;
} else {
    const float erLg = erOff ? 0.0f : (lvl * eL);      // FR-024, same rule
    const float erRg = erOff ? 0.0f : (lvl * eR);
    outL[k] = (dryG * dL) + (wetG * (engineOutL_[k] + erLg));
    outR[k] = (dryG * dR) + (wetG * (engineOutR_[k] + erRg));
}
```

**Why the gains are applied after the alignment** (FR-062): the dry and ER busses carry *different*
gains (`dryG` vs `wetG * lvl`), so one shared aligned pair cannot serve both — hence four lines — and
applying a gain before a 1024-sample line would delay every gain change by the full latency.

The two `cos`/`sin` per sample are the one obvious hoist if SC-009 arm (b) is tight; both are
functions of `mx` alone. Held as lever L-3 (S12.3), not applied pre-emptively — and
`CavernVerb_MixLaw` (S10.4a) is L-3's **acceptance gate**: it measures the equal-power law across a
mix sweep, so a linear crossfade, or an L-3 control-grid lerp whose error exceeds the claimed 0.1 %,
fails there.

### S5.5 Dormancy of the tap loop (FR-065)

`erChainSkipped_` becomes true only when **both** `earlyLevel_` and `earlySend_` are settled at
exactly `0.0f`. While skipped:

- `erLine_.write()` still runs every sample — the pattern stays charged, so a returning gain does not
  fade in from a hole;
- the 12 `readLinear` + one-pole + accumulate are skipped (the actual saving, S12.2);
- `sendScratch_[k] = 0.0f` **by assignment**, so the owned engine receives literal digital silence
  (which is also the reachable product state SC-006 (a)/(c) render at `setEarlySend(0)`);
- the dampers, the smoothers and the engine keep running (FR-036; FR-065's `mix == 0` ruling).

Re-entry is the `ShapedRamp`'s ≥ 50 ms smoothstep, and `erChainSkipped_` is cleared **in the setter**,
before the ramp starts moving, so the first ramped sample already has taps behind it.

`setMix == 0` never skips anything — FR-065's stated justification (the FDN state is the instrument a
drone player holds, freezes and returns to) is reproduced in the header at the branch.

---

## S6. The damper bank

### S6.1 Configuration

- `setDamperDepth(v)`: `damperDepth_ = clamp(isFinite(v) ? v : kDefaultDamperDepth, 0, 1)`; then
  `for (i < kMaxChannels) damper_[i].setDepth(damperDepth_)`. Depth is applied **once**, here (B-3),
  and is therefore ramped click-free by each drift's own 150 ms output smoother (FR-031, SC-003 (d)).
- `setDamperRate(v)`: `damperRate_ = clamp(...)`; `damper_[i].setSmoothness(1.0f - damperRate_)` —
  `v = 0` → `smoothness 1` → `τ = kTauMax = 30 s` (slowest); the default `0.15` → `smoothness 0.85` →
  `τ = 0.2 + 0.85 × 29.8 = 25.53 s`, inside the decided 20–30 s band (`brownian_drift.h:97-99`,
  `:231-234`); `v = 1` → `τ = 0.2 s` (fastest, the worst case SC-003/SC-004/SC-005 pin).

### S6.2 Offset generation, once per control chunk

```cpp
constexpr float kOctavesPerUnit = kMaxDamperOctaves / BrownianDrift::kInternalStd;   // = 3.0
for (std::size_t i = 0; i < numChannels_; ++i) {
    float o = kOctavesPerUnit * damper_[i].getCurrentValue();   // already depth-scaled, |.| <= 1
    o = std::clamp(o, -kMaxDamperOctaves, kMaxDamperOctaves);   // S0.2 B-2: THIS is what is published
    if (std::abs(o) < 1.0e-20f) { o = 0.0f; }                   // FR-073 denormal flush
    damperOffset_[i] = o;
}
for (std::size_t i = numChannels_; i < kMaxChannels; ++i) { damperOffset_[i] = 0.0f; }
engine_.setDamperOffsetsOctaves(damperOffset_, numChannels_);
```

Properties relied on, all verified: `getCurrentValue()` is hard-clamped to `[-1,+1]`
(`brownian_drift.h:212-214`); the internal walk is clamped at `±kWalkLimit = 4` and flushed below
`1e-20` (`:226`, `:228`); the output passes an internal 150 ms `OnePoleSmoother` (`:103`), which **is**
FR-033's slew limit — **no second smoother is added**. Zero-mean bipolar by construction (the walk
reverts to `mean_ = 0`), so a line wanders both darker and brighter: "breathes", not "only darkens".

At `depth == 0` the drift's `outputTarget()` is exactly `0.0f` and the smoother converges to it (it is
snapped there by `reset()`), so every published offset is exactly `0.0f` and FR-044's inertness
precondition holds **by value**, not by tolerance. One stated asymmetry: a depth automated 1 → 0
mid-render leaves the smoother ramping to zero over ~150 ms rather than jumping — the desired
click-free behaviour, and it means "offsets are exactly zero" becomes true a few chunks after the
setter. SC-005 (c) renders at a settled depth 0, so it is unaffected.

---

## S7. The `AetherReverb` extension — the exact diff

Six append-only edits inside `effects/aether_reverb.h`. Nothing else in the file moves (FR-042). Each
carries a comment naming `vorago-phase9-cavern-space` and the FR.

**E-1. One public constant** (beside the other public constants, ~`:1398`):

```cpp
/// Vorago Phase 9 (FR-040): hostile-input ceiling on a published damper offset, in OCTAVES.
/// >= CavernVerb::kMaxDamperOctaves, so a legitimate excursion is never clipped here and
/// SC-005's depth ordering cannot be flattened at the top by the other class's clamp.
static constexpr float kMaxDamperOffsetOctaves = 4.0f;
```

**E-2. One public setter** (after `setSeed`, ~`:2380`):

```cpp
/// @brief Vorago Phase 9 (FR-040): accept a per-line damping-cutoff offset in OCTAVES.
///        A POSITIVE offset LOWERS that line's damping cutoff (darker, shorter HF decay).
/// @note This class never GENERATES these - it only applies them. Not persisted, not exposed
///       through any Seraphis-facing path, and EXACTLY inert at its all-zero default
///       (see applyDamperOffsets()).
/// @note Real-time safe. Allocates nothing. Plain member: no virtual, no new interface.
void setDamperOffsetsOctaves(const float* offsets, std::size_t count) noexcept {
    if ((offsets == nullptr) || (count == 0u)) {
        for (std::size_t i = 0; i < kMaxChannels; ++i) { damperOffset_[i] = 0.0f; }
        return;
    }
    const std::size_t n = std::min(count, numChannels_);
    for (std::size_t i = 0; i < n; ++i) {
        const float v = offsets[i];
        damperOffset_[i] = std::clamp(isFinite(v) ? v : 0.0f,
                                      -kMaxDamperOffsetOctaves, kMaxDamperOffsetOctaves);
    }
    for (std::size_t i = n; i < kMaxChannels; ++i) { damperOffset_[i] = 0.0f; }
}
```

**E-3. One public accessor** (beside `getEffectiveDelayLengthSamples`, ~`:2510`):

```cpp
/// @brief Vorago Phase 9 (FR-041): the damping coefficient the loop is CURRENTLY applying to
///        this line - i.e. what renderSlice step 3 reads. Exists so SC-003/004/005/012 can
///        measure the dampers without a test hook or a friend declaration.
[[nodiscard]] float getEffectiveDampingCoefficient(std::size_t channel) const noexcept {
    return (channel < kMaxChannels) ? effectiveDampCoeff_[channel] : 0.0f;
}
```

**E-4. Two private arrays + one private method** (beside `dampCoeff_`, `:4489`):

```cpp
alignas(32) float damperOffset_[kMaxChannels]{};        ///< Vorago Phase 9, FR-040. Octaves.
alignas(32) float effectiveDampCoeff_[kMaxChannels]{};  ///< Vorago Phase 9, FR-043. What :4283 reads.
```

```cpp
/// @brief Vorago Phase 9 (FR-043, FR-044, FR-045, FR-048).
///
/// Runs AFTER the epsilon-gated updateDecayAndDamping(), never inside it: the :3128-3137 gate
/// and its lastJotScale_/lastJotDecay_/lastJotDamping_ sentinels are untouched, so a
/// continuously-moving offset costs ZERO additional powf - which is the whole reason the
/// naive "modulate setDamping" route was rejected.
void applyDamperOffsets() noexcept {
    for (std::size_t i = 0; i < numChannels_; ++i) {
        const float off = damperOffset_[i];
        if (off == 0.0f) {
            effectiveDampCoeff_[i] = dampCoeff_[i];   // FR-044: ASSIGNMENT. Bit-identical, by value.
            continue;
        }
        // FR-048 steps 1-3 in the algebraically identical closed form:
        //     c   = 1 - exp(-u),  u = 2*pi*fc/sr        (the form the :4283 one-pole applies)
        //     fc' = fc * 2^(-off)   =>   u' = u * 2^(-off)
        //     c'  = 1 - exp(-u * 2^(-off)) = 1 - (1 - c)^(2^(-off))
        // The logarithm and the sample rate cancel EXACTLY, so this is two transcendentals per
        // line (exp2 + pow) rather than three (log + exp2 + exp), and it cannot divide by sr.
        const float c  = std::clamp(dampCoeff_[i], 0.001f, 1.0f - 1.0e-6f);  // FR-048 step 1's clamp
        const float p  = std::exp2(-off);
        const float cp = 1.0f - std::pow(1.0f - c, p);
        effectiveDampCoeff_[i] = std::clamp(cp, 0.001f, 1.0f);   // FR-045: the :3148 range, unchanged
    }
    for (std::size_t i = numChannels_; i < kMaxChannels; ++i) {
        effectiveDampCoeff_[i] = dampCoeff_[i];
    }
}
```

The `1 - 1e-6` upper clamp is load-bearing, not an artefact: without it `c = 1` gives `1 - 0^p = 1`
for every offset, i.e. a line at "no damping" would ignore the dampers completely. With it, `c = 1`
and `off = +1.5` gives `1 - (1e-6)^0.35355 = 0.99266` — exactly what FR-048 step 1 specifies.
Sign check (asserted by SC-005's direction clause): `off = +0.5` → `p = 0.70711` →
`(1-c)^p > (1-c)` → `c' < c` → **darker**. ✓ Boundedness: `c' ∈ [0.001, 1]` for every offset vector,
including a hostile one, so the `y = c·x + (1-c)·y` one-pole stays non-expansive (`:3085-3086`) and
`AetherReverb`'s own FR-032 ("loop gain ≤ 1.0 outside freeze") survives unchanged. No new stability
argument is introduced.

**E-5. Two call-site edits.**

1. `refreshControlState()` (`:3610-3618`) gains one line **inside the existing `!freezeTarget_`
   branch**, so FR-046's latch is inherited exactly, for the reason the header already gives at
   `:3600-3606`:

```cpp
void refreshControlState() noexcept {
    if (!freezeTarget_) {
        updateGeometry();          // step 4
        updateDecayAndDamping();   // step 5
        applyDamperOffsets();      // Vorago Phase 9, FR-043/FR-046: the SAME branch as dampCoeff_
    }
    updateMorph(); updateDiffuser(); snapshotBlockScalars();
}
```

   It runs **unconditionally within** that branch — not behind `updateDecayAndDamping()`'s early
   return — because the offsets move on every control chunk while `dampCoeff_` does not.

2. `renderSlice` step 3 (`:4282`): `const float c = dampCoeff_[i];` becomes
   `const float c = effectiveDampCoeff_[i];`. One token; nothing else in that loop changes.

**E-6. `reset()` initialisation (FR-047's second half) — kept, with its rationale corrected.**
The justification previously written here was wrong, and is struck: it claimed that an engine
prepared (or reset) and frozen **before** its first thawed control chunk would never take E-5's branch
and would run the `:4283` one-pole on an array that was never written. That scenario **cannot occur**.
`AetherReverb::reset()` sets `freezeTarget_ = false` at `:1979`, *before* it reaches
`refreshControlState()` at `:2099`, so E-5's `!freezeTarget_` branch (`:3610-3618`) — and with it
`applyDamperOffsets()` — always runs at least once during every `prepare()`/`reset()`, whatever the
caller does immediately afterwards.

E-6 is therefore **defensive redundancy and documentation, not a bug fix**: it makes
`effectiveDampCoeff_` well-defined by construction at the point `damperOffset_` is zeroed, so the
invariant does not depend on a control-flow accident in a method 120 lines away that a future edit
could reorder. It costs `kMaxChannels` stores per `reset()`. `AetherReverb_DamperOffsetFrozenInit`
(S10.4a) gates the *invariant* (prepare → immediate `setFreeze(true)` → render → every
`getEffectiveDampingCoefficient(i)` equals the recomputed shipped coefficient by exact float
equality), which is what FR-047 actually promises, rather than gating E-6's presence. So `reset()`
gains, placed **before** its `lastJot* = -1` / `refreshControlState()` block:

```cpp
for (std::size_t i = 0; i < kMaxChannels; ++i) {
    damperOffset_[i] = 0.0f;
    effectiveDampCoeff_[i] = dampCoeff_[i];   // FR-044's plain assignment; FR-047
}
```

**Cost.** Two transcendentals per non-zero line per control chunk: at N = 16 and 750 chunks/s that is
128 `pow`+`exp2` pairs per 512-sample block; at a pessimistic 100 ns per `pow` ≈ 13 µs/block, i.e.
≤ 3.6 % of `kMaxAdmissibleNs` in the worst arm, and **zero** on the Seraphis path (the `off == 0.0f`
branch is an assignment). Measured as SC-009 arm (d)'s difference.

**Seraphis impact.** With no caller, `damperOffset_` is all-zero forever, `applyDamperOffsets()` is
`numChannels_` float stores per control chunk, and `effectiveDampCoeff_[i]` holds the identical
`float` object value `dampCoeff_[i]` holds. SC-012 (a) recomputes the published law and requires exact
float equality; SC-012 (b) requires `dsp_effects_tests` and `dsp_systems_tests` green with no test
file edited.

---

## S8. Latency, and what `getLatencySamples()` means here (FR-062, SC-013)

`CavernVerb::getLatencySamples()` returns `engine_.getLatencySamples()` — `diffusionFftSize` when the
spectral stage was enabled at prepare, otherwise exactly 0 (`:2612`). The three busses:

| Bus | Path | Delay from input |
|---|---|---|
| dry | `dryAlign*` at `alignSamples_` | `L` |
| ER | tap `i`, then `erAlign*` at `alignSamples_` | `L + d_i` |
| late field | ER sum → engine (its own `L` internally) | `L + d_i + (engine line lengths)` |

SC-013 needs two renders because at `mix = 0` the wet path contributes exactly `0.0f` by assignment,
so a single render cannot carry both the dry and the ER clause.

---

## S9. RT safety, denormals, portability

- **No allocation on the audio path** (FR-070). Everything is sized in `prepare()`; scratch is
  fixed-size member storage. `std::vector` appears only inside `DelayLine`, which allocates only in
  its own `prepare()`. SC-010 is the gate — and it must first prove the counter is armed (S10.4).
- **No locks, exceptions or I/O.** Every method is `noexcept`; nothing throws, nothing logs.
- **No `std::isnan`/`isinf`/`isfinite`** (FR-071); `tools/lint-nonfinite-symbols.js` enforces it.
- **Denormals** (FR-073). Three flushes, and the rationale for the middle one is stated correctly
  here for the first time:
  - the damper offsets flush below `1e-20f` (S6.2);
  - the 12 ER tap one-pole states are flushed the same way once per control chunk. The justification
    is **not** "the ER is feed-forward, so a denormal cannot recirculate" — that claim was wrong:
    `taps_[i].state += taps_[i].a * (x - taps_[i].state)` is a first-order IIR and its state is
    precisely a recirculating quantity, which with no input decays geometrically into the denormal
    range and stays there. The true argument is that the per-chunk flush **bounds the exposure to at
    most 64 samples**: the stage is not recursive *across* taps and each state is re-zeroed before it
    can persist, so the worst case is one control chunk of denormal arithmetic on 12 scalars, not an
    unbounded tail. The cost of the guard is one comparison per tap per 64 samples;
  - the **ER line input** flushes below `1e-20f` before `erLine_.write()` (S5.4) — one compare per
    sample. Without it a decaying host input tail leaves the entire ER path (12 `readLinear` + 12
    multiplies per sample, over up to `maxEarlySeconds` of history) in denormal arithmetic with no
    guard at all, which is not covered by the tap-state flush.

  The test image sets FTZ/DAZ in `dsp_test_main.cpp`; with all three flushes in place the header
  genuinely does not rely on it.
- **No narrowing in brace init** (FR-072); designated initialisers for both `PrepareConfig`s; every
  `std::size_t` literal suffixed `u`.
- **No SIMD at all** in this component — the ER loop is 12 scalar taps and the damper maths is
  control-rate — so there is no aligned-load hazard for `tools/lint-arch-guarded-includes.js` or the
  aligned-load lint to find.
- **Sample rates `[8 kHz, 192 kHz]`** (FR-074): tap delays are specified in **milliseconds** and
  re-derived per rate, so the pattern is rate-invariant; the absorption cutoffs are Nyquist-guarded
  (S5.3). There is no 44.1 kHz floor, because shimmer — the only stage that had one
  (`kShimmerMinSampleRate`, `:1394`, `:1631`) — is never constructed.
- **Non-finite input** is replaced with `0.0f` at the boundary and does **not** increment any recovery
  counter (FR-064, the `:4163-4172` discipline). Non-finite *state* remains the owned engine's
  business, reported through the forwarded `getNonFiniteRecoveryCount()`.

---

## S10. Test plan

### S10.1 TU assignment (all five added to the enumerated `dsp_effects_tests` list)

| TU | Criteria | Notes |
|---|---|---|
| `dsp/tests/unit/effects/cavern_verb_test.cpp` | SC-002, SC-003, SC-004, SC-005, SC-006, SC-007, SC-010, SC-011, SC-013, SC-015, SC-016, **plus the S10.4a cases** `CavernVerb_EarlyAbsorption`, `CavernVerb_ProcessContract`, `CavernVerb_BreathDualTarget`, `CavernVerb_MixLaw` | main behavioural TU; includes `<allocation_detector.h>` **only** |
| `dsp/tests/unit/effects/cavern_verb_freeze_test.cpp` | SC-001, SC-008, **plus** `CavernVerb_SilenceContract` (S10.4a) | the two long freeze protocols, and the one case that calls `silence()` |
| `dsp/tests/unit/effects/cavern_verb_soak_test.cpp` | SC-014 | `[long]` |
| `dsp/tests/unit/effects/cavern_verb_perf_test.cpp` | SC-009 | `[.perf]`, run alone via `node tools/run-cpu-tests.js` |
| `dsp/tests/unit/effects/aether_reverb_damper_offset_test.cpp` | SC-012 (a), **plus** `AetherReverb_DamperOffsetHostileInput` and `AetherReverb_DamperOffsetFrozenInit` (S10.4a) | **a new TU, never an edit to an `aether_reverb_*` TU** — SC-012 (b) freezes those files. FR-040's hostile-input contract and FR-047's invariant are appended to a **shipped** header, so both are gated here rather than left to SC-012 (a)'s all-zero sweep |

Tags: `[effects][cavern]` on every case, plus `[long]` / `[.perf]` where stated. Case names are
exactly the spec's (`CavernVerb_EchoDensity`, …) so a compliance table can cite them.

### S10.2 Shared test code

**`tests/test_helpers/reverb_metrics.h`** (new, header-only; **no CMake edit** — `test_helpers` is an
INTERFACE target exposing the directory, `tests/test_helpers/CMakeLists.txt:7-12`):

```cpp
namespace Krate { namespace DSP { namespace TestUtils {

/// FR-080's lift of dsp/tests/unit/effects/fdn_reverb_test.cpp:328-373, unchanged in substance:
/// windowSamples-long windows, per-window RMS, fraction of windows whose RMS exceeds peak * 0.01,
/// on a mono IR. The PEAK is taken over the ANALYSED window range - which for startWindow = 0 and
/// windowCount = all is exactly the old behaviour, so fdn_reverb_test's REQUIRE(ned >= 0.8) holds
/// through the lift unchanged.
[[nodiscard]] inline double normalisedEchoDensity(std::span<const float> monoIr,
                                                  std::size_t windowSamples,
                                                  std::size_t startWindow,
                                                  std::size_t windowCount);

struct Biquad { /* b0,b1,b2,a1,a2,x1,x2,y1,y2 */ float process(float) noexcept; };
[[nodiscard]] inline Biquad     makeLowpass(double sr, double fc, double q);
[[nodiscard]] inline Biquad     makeHighpass(double sr, double fc, double q);
struct OctaveBand { Biquad hp[2], lp[2]; float process(float) noexcept; };     // 4th-order Butterworth
[[nodiscard]] inline OctaveBand makeOctaveBand(double sr, double centreHz);
[[nodiscard]] inline double     schroederT60(const std::vector<double>& hopEnergy, double hopSeconds);
[[nodiscard]] inline double     spectralCentroidHz(std::span<const float> x, double sr);
[[nodiscard]] inline double     pearson(std::span<const double> a, std::span<const double> b);

/// G-2 as a GENERATOR, not a buffer: fills `dst` with the band-limited (80 Hz - 11 kHz) noise
/// stream for absolute sample offset `startSample`, so a 30-minute render carries no loop
/// periodicity (P-3's explicit requirement).
inline void fillBandLimitedNoise(std::span<float> dst, double sr, std::uint32_t seed,
                                 std::uint64_t startSample, NoiseState& state);

}}}  // namespace
```

The band / T60 / noise helpers are **re-derived** from `aether_reverb_test.cpp:140-245` (whose copies
are file-local and whose TU SC-012 (b) freezes), not moved.

**The one edit to an existing test file:** `dsp/tests/unit/effects/fdn_reverb_test.cpp:328-373` is
replaced by `normalisedEchoDensity(irMono, 48, 0, irLength / 48)` with its `REQUIRE(ned >= 0.8)` kept
verbatim (FR-080). That file is neither an `aether_reverb_*` nor a `seraphis_*` TU, so SC-012 (b)'s
freeze does not cover it.

### S10.3 Shared fixtures in the main TU

- `makeDefaultCavern(sr, cfg)` — P-1: 48 kHz, N = 8, `maxBlockSamples = 512`, seed 1, and **every
  FR-066 default applied explicitly** so a future default change breaks the fixture loudly instead of
  drifting a criterion.
- `makeLongEarlyCavern(sr)` — **P-1b**: P-1 in every respect except `maxEarlySeconds = 0.60`, so the
  full FR-023 range `[80, 600]` ms is actually reachable. Required by SC-006 (a)'s 600 ms arm and by
  SC-003 (d)'s `setEarlySizeMs` sweep: under P-1's 0.30 s default, `setEarlySizeMs(600)` clamps to
  300 ms (S5.2), and because SC-006 (a) compares arrivals against `getEarlyTapDelaySamples(i)` — which
  reports the same clamped geometry — the arm would pass while testing nothing beyond the 300 ms case.
- `renderRagged(engine, in, out, {37, 111, 513})` — P-2's deliberately non-multiple-of-64 partitions,
  cycled. 512 is *not* used for this purpose (it is `8 × 64`, control-grid aligned).
- `clickConfig48()` — P-5 verbatim in designated-initialiser form **with `.sampleRate = 48000.0f`**
  (the struct default is 44 100, `artifact_detection.h:38`, which would silently mis-scale every
  reported detection time), plus the inherited calibration ladder: a no-transition reference render
  must give zero false positives, else `detectionThreshold` rises to the smallest value that does,
  **capped at 8.0**, and the calibrated config must still report ≥ 1 detection on a control render
  carrying a single-sample step of amplitude 0.1. Threshold, false-positive count and control count
  are `WARN`ed. The zero-detection requirement is never relaxed.

### S10.4 Criterion by criterion

| Criterion | `TEST_CASE` | Assertion strategy |
|---|---|---|
| **SC-001** freeze conserves energy | `CavernVerb_FreezeEnergyConservation` (freeze TU) | **Clause 1:** every Phase-9 addition at maximum before the freeze — `damperDepth 1`, `damperRate 1`, `earlyLevel 1`, `earlySend 1`, `size 1`, `darkness 1`, **`breath 1`, `dimensionality 1`**; 2 s G-2 → `setFreeze(true)` → 0.25 s → `REQUIRE(isFrozen())` → 60 s G-3, sampling `getStateEnergy()` once per second; every sample within **±0.5 dB** of the first post-latch sample. **Clause 2:** the same with `setBreath(0)` (a morphing mixer moves energy *across* frequency, so a per-band bound does not follow while it morphs); per-octave energy via `makeOctaveBand` at {125, 250, 500, 1k, 2k, 4k, 8k}, one unmeasured warm-up second before the reference window, −80 dBFS gate, **≥ 6 of 7 bands** qualifying and each within ±0.5 dB. Worst deviation and qualifying count `WARN`ed. **Clause 3 (FR-036 + FR-046, the frozen-damper clause, new in this revision):** over the frozen,
digital-silence G-3 span at `damperDepth 1`/`damperRate 1`, sample `getDamperOffsetOctaves(i)` at the
first and last control chunk and `REQUIRE` that at least one line moved by more than **10 ×
`kMaxOffsetStepPerChunk`** (B-2's corrected bound) — FR-036 requires every damper to advance
unconditionally, including while frozen and on digital silence, and an implementation that gates
`damper_[i].processBlock()` on `!frozen` or on input activity stops dead and fails **only** here
(clauses 1 and 2 are made strictly *easier* by a damper bank that stopped moving). Over that same span,
replay the recorded offsets into a bare `AetherReverb` as SC-003 (b) does and `REQUIRE`
`getEffectiveDampingCoefficient(i)` is **bit-identical at every control chunk**, then resumes moving
after thaw — that is FR-046's latch, and it is what actually gates E-5's placement inside the
`!freezeTarget_` branch. **The claim this row used to make — "this criterion is what fails if E-5 is
placed outside the branch" — was false and is struck:** while frozen the damping one-pole contributes
nothing at all, because `crossfade(filterState_[i], delRead[i], freezeRamp)` is written so that
`freezeRamp == 1` returns **exactly** `delRead[i]` (`aether_reverb.h:2996-3001`, read site
`:4277-4284`), and SC-001 waits 0.25 s and `REQUIRE`s `isFrozen()`, i.e. the latch is complete. The
frozen output is therefore independent of `dampCoeff_` and of `effectiveDampCoeff_`, and clause 1's
±0.5 dB energy bound is insensitive to the damper path by construction (R-4). Clauses 2 and 4 of the Seraphis source criterion are explicitly not inherited (output-tap level; the shimmer/bloom sends, which are not constructed here). |
| **SC-002** echo density | `CavernVerb_EchoDensity` | G-1 impulse, `mix = 1`, grid `size ∈ {0, 0.5, 1} × dimensionality ∈ {0, 1}`, `density` at its 0.75 default. Window: `t_start` = first 1 ms window whose RMS exceeds `peak*0.01`; `W = max(250 ms, 3·m_long)` with `m_long = max_i getEffectiveDelayLengthSamples(i)`. `REQUIRE(ned >= 0.8)` through the lifted helper. **Second clause:** NED at `earlyLevel 1` vs `earlyLevel 0` measured over **one common window derived from the `earlyLevel = 0` render** (at `earlyLevel 1`, `t_start` is the first ER arrival; at 0 it is the FDN onset ~20 ms later — comparing two differently-anchored windows would not test the claim); `REQUIRE(ned1 >= ned0 - 0.05)`. **Third clause (FR-065, new in this revision), without which the level-only skip is undetectable:**
FR-065 skips the tap loop only when **both** `earlyLevel` and `earlySend` are settled at zero, because
at `earlyLevel == 0` alone the taps are still the FDN's only excitation. So (i) on the
`setEarlyLevel(0.0)` render, `REQUIRE` the peak exceeds a stated floor (−60 dBFS) **before** `t_start`
is derived from it — under the buggy level-only skip that render is digital silence, `t_start` is
undefined and `ned0` degenerates to 0 or NaN, making `REQUIRE(ned1 >= ned0 - 0.05)` pass vacuously
rather than fail; and (ii) `REQUIRE` that render's energy after `t = 2 ×` the last tap delay exceeds
the `setEarlySend(0.0)` render's by **≥ 20 dB** (the mirror of SC-006 (e)). Records both `t_start`s,
`m_long`, `W`, the excluded-window count, both NEDs, the `earlyLevel = 0` peak and the 20 dB margin. |
| **SC-003** damper smoothness | `CavernVerb_DamperMotionSmoothness` | `damperDepth 1`, `damperRate 1` (worst case). **(a)** 120 s G-2 rendered in exact 64-sample steps, sampling `getDamperOffsetOctaves(i)` per chunk; `REQUIRE(maxStep <= kMaxOffsetStepPerChunk)` with the bound written as the **parameterised expression** `alpha * (kMaxDamperOctaves/BrownianDrift::kInternalStd + kMaxDamperOctaves)`, `alpha = 1 - pow(calculateOnePolCoefficient(BrownianDrift::kDriftOutputSmoothMs, sr), kControlChunkSamples)` — **not** the spec's `(1 - exp(-64/(sr*0.150))) * 2 * kMaxDamperOctaves`, which is wrong on both factors (B-2: `smoothTimeMs` is a time to 99 %, `smoother.h:77-93`, and the published quantity is `clamp(3s, ±1.5)`). Corrected value **0.19562** octaves/chunk at 48 kHz, against the spec's 0.02655; a correct implementation fails the spec's literal. Measured max `WARN`ed. **(b)** the same record replayed into a bare `AetherReverb` via `setDamperOffsetsOctaves`, reading `getEffectiveDampingCoefficient(i)` per chunk (this is exactly what FR-041 exists for — `CavernVerb` exposes no engine reference); bound `0.25499 * kMaxOffsetStepPerChunk` = **0.04988** under the corrected constant (spec literal 0.00677), measured max `WARN`ed. **(c)** `ClickDetector` (P-5) over the same 120 s render: **zero** detections. **(d)** one further render stepping `earlyLevel`, `earlySend` and `mix` each 0→1 and 1→0 at pinned times ≥ 5 s apart, **plus** `damperDepth` 0.35→1.0→0.35 at two further pinned times, **plus a `setEarlySizeMs` sweep
80 → 600 → 80 at two further pinned times ≥ 5 s apart** (rendered on fixture **P-1b**,
`maxEarlySeconds = 0.60`, so the full FR-023 range is reachable): **zero** detections across every
transition. The size sweep is new in this revision and is not optional — FR-023 mandates a **shaped
(C1)** size ramp, citing the measured C0-corner failure at `aether_reverb.h:4270-4281`, and R-6 names
the per-sample delay ramp as the mitigation, yet before this clause **no test rendered a
`setEarlySizeMs` change at all**: SC-003 (c) runs at a fixed size, SC-006 changes size only between
separate renders, and SC-010 calls the setter mid-render but asserts allocations, not artifacts. This
is the clause that fails if `makeLinearTap` or any other per-chunk delay pinning is used (S5.2). |
| **SC-004** per-line decorrelation | `CavernVerb_DamperDecorrelation` | 10 min at `damperDepth 1`. **Arm (a)** `damperRate 1` (τ = 0.2 s, ≈ 1500 effective samples, s.d.(r) ≈ 0.026): 28 pairwise Pearson `|r|`; `mean <= 0.30`, `max <= 0.60`. **Arm (b)** the default rate, gated against an **in-test null distribution**: `numChannels` independently re-seeded `BrownianDrift`s advanced over the same record length, their 28 `|r|` computed, `REQUIRE(max <= P99(null))`. Both arms record mean and max. **Negative control (the teeth):** the same statistic on a record where one drift value is broadcast to all lines must **exceed** the bounds. **Salt separation:** the published offsets must differ from a `BrownianDrift` seeded `deriveStreamSeed(seed, kDriftSaltBase + j)` (the spec's literal form) **and** from one seeded `deriveStreamSeed(deriveStreamSeed(seed, kCavernReverbSalt), kDriftSaltBase + j)` — the engine's *actual* stream, which is where a real collision would show. |
| **SC-005** dampers change the sound | `CavernVerb_DamperSpectralMotion` (`[long]`) | `damperRate 1` (pinned: at the slow default 115 s of 1 s frames hold only ~2–6 independent samples). Statistic (**amended 2026-09-17 to the SEED-PAIRED form**): `M(d) = sd over time of [c_d(t) − c_0(t)]` in 1 s frames after the first 5 s of a 120 s G-2 render, averaged over **≥ 8 seeds**, where the same seed drives the same input at every depth so the excitation-driven component cancels. The un-paired form it replaced is unachievable by any correct implementation — measured, it *falls* across the depth grid (331.74 → 293.07 Hz), because a single 8192-point periodogram of a noise realisation scatters ≈ 332 Hz per frame while the dampers add ≈ 63 Hz in quadrature. **(a)** `M(1) ≥ **3×** M(0.25)` (the factor is unchanged; `M(0) = 0` exactly, so it cannot be the denominator). Measured: 0, 14.1701, 34.6169, 51.7552, 62.6485 Hz, ratio **4.42×**. **(b)** Spearman(depth, `M`) ≥ **0.9** over `{0, 0.25, 0.5, 0.75, 1}`. **(c)** on a bare `AetherReverb` at the same operating point, the un-paired statistic from an all-zero-offset render is ≤ **1.05×** the one from a render whose offsets were never set (the only place both states are constructible — it is what makes `M(0) = 0` a fact rather than a tautology). **Direction (not merely monotonicity):** a separate pair of renders on a bare `AetherReverb` with a **static** `+0.5` / `−0.5` octave vector published to every line — measured 8 kHz-band T60 must be **lower** for `+0.5`; run at `setDarkness(0.0)` **and** at the default, so R-5's `c = 1` inertness bug cannot hide. Absolute centroid excursion in Hz `WARN`ed, not gated. |
| **SC-006** ER geometry | `CavernVerb_EarlyReflectionGeometry` | G-1, `earlyLevel 1`, `mix 1`, **`setEarlyAbsorption` pinned explicitly at 0.60** (S5.3's Nyquist guard makes the cutoff set rate-dependent, so an unpinned absorption would let the 44.1 kHz and 96 kHz arms run under different laws), and **`earlySend 0`** for (a)/(c) — a reachable product state in which the owned engine receives digital silence and the render carries the ER alone. **(a)** arrivals located by local-maximum picking on `|out|`; each within **±1 sample** of `getLatencySamples() + getEarlyTapDelaySamples(i)`, at `earlySizeMs ∈ {80, 220, 600}` × `sr ∈ {44100, 48000, 96000}`, with the **600 ms arm rendered on
fixture P-1b** (`maxEarlySeconds = 0.60`, S10.3) — under P-1's 0.30 s default `setEarlySizeMs(600)`
clamps to 300 ms and, because the arm compares arrivals against `getEarlyTapDelaySamples(i)` which
reports that same clamped geometry, it would pass while testing nothing beyond the 300 ms case.
Paired with it, on **P-1** (`maxEarlySeconds = 0.30`): `setEarlySizeMs(600)` then
`REQUIRE(getEarlyTapDelaySamples(11) ≈ 300 ms × scale)` — the buffer-derived clamp of FR-023 and
`spec.md:1267-1268`, asserted rather than assumed. **(b)** first arrival ≥ `kEarlyFirstArrivalFloorMs`. **(c)** NED (the same helper) over `[first, last]` **< 0.5** — the negative control that distinguishes a reflection pattern from a diffuse field. **(d)** FR-022's inequality evaluated in ms over the shipped table at orders `{2, 4, 6}`; `REQUIRE(min >= kEarlyIncommensurabilityTol)`; the **measured minimum is `WARN`ed** (expected 0.05411) and the order-8 value reported beside it as a documented non-gate (B-1). **(e)** with `earlySend 1`, energy after `t = 2 × last tap` exceeds the `earlySend 0` render's by **≥ 20 dB**. **(f) mono-sum invariance (FR-020), new in this revision:** at `earlySend 0`, render an impulse on L
only and again on R only; the two ER outputs must be identical within `kSampleTolerance` — the ER is
mono-summed, so its pattern is invariant to input panning. **(g) side rule (FR-020):** locate arrivals
**per channel**; tap `i` must appear on L for even `i` and on R for odd `i` (picking arrivals from
`|out|`, as (a) does, cannot distinguish which bus a tap landed on). **(h) width independence
(FR-019):** repeat (g) at `setWidth(0)` and `setWidth(1)` — the ER arrivals must be unchanged, since
`setWidth` governs the late field only and cannot collapse the ER geometry. Also reports
`Σg_L/Σg_R` (expected +0.80 dB). |
| **SC-007** dark tuning | `CavernVerb_DarkTuning` | **(a)** banded Schroeder T60 at FR-066 defaults: `T60(8k) <= 0.35 × T60(250)`. **(b)** long-term spectral centroid at `CavernVerb` defaults ≤ **0.70×** a bare `AetherReverb` at its own defaults, same input and length; both figures `WARN`ed so a near-miss is diagnosable. **(c)** `Σ_i getEffectiveDelayLengthSamples(i)` at `setSize(0)` ≥ **4.4×** the bare engine's at `setSize(0)`, with the bare instance seeded `deriveStreamSeed(1, kCavernReverbSalt)` (so the two jitter realisations are identical and cancel), both sampled at the same absolute sample index after identical render lengths, and breath zeroed on both sides (`setBreath(0)` / `setSizeBreathDepth(0)`+`setDimensionalityTideDepth(0)`); residual spread `WARN`ed, nominal ratio 4.594. `setModDepth` is **not** added to the surface for this. **(d)** G-4 220 Hz sine, 20 s wet-only: narrow-band energy at 440 and 330 Hz ≤ **−40 dB** relative to 220 Hz, and `isShimmerActive()` false. |
| **SC-008** infinite hold | `CavernVerb_FreezeCycles` (freeze TU) | The pinned timeline `2 s G-2 → [freeze, 5 s G-3, thaw, 3 s G-2] × 10` at `damperDepth 1`. **(a)** per span, `getStateEnergy()` at latch completion vs the sample before `setFreeze(false)`: within **±0.5 dB**, ten figures recorded. **(b)** **zero** `ClickDetector` (P-5) detections over the whole render. **(c)** during the **third** frozen span only, the input is G-2: output non-zero and correlated with the input at the ER tap delays (cross-correlation peak within ±1 sample of `L + d_i`); that span is excluded from (a), the other nine carry it. |
| **SC-009** CPU | `CavernVerb_CpuBudget` (perf TU, `[.perf]`) | The `aether_reverb_perf_test.cpp` construction inherited verbatim: ns per 512-block at 48 kHz, best-of-25 × 500 blocks after 400 warm-up, per-baseline `static_assert(b*1.5 <= 533333.3)` **and** `static_assert(b <= 355555.6)` plus runtime `REQUIRE(measured <= b*1.5)`. Arms: **(a)** FR-066 defaults; **(b)** worst — `size 1`, `damperDepth 1`, `damperRate 1`, `fog 1`, `earlyLevel 1`, `earlySend 1`, `earlySizeMs` max, `breath 1`, `N = 16`, prepared at **`diffusionFftSize = 4096`, `maxEarlySeconds = 0.60`**; **(c)** frozen; **(d)** arm (a) at `damperDepth 0`, reported as a **difference**. Arms (a), (c), (d) prepare at FFT 1024 / `maxEarlySeconds 0.30`. Baselines are `ceil(firstCleanMeasurement × 1.05)` and **are never raised afterwards** (FR-082). |
| **SC-010** no allocation | `CavernVerb_NoAllocation` | **Clause 0 first:** a deliberate `std::vector<float> v(1024)` inside an `AllocationScope` must be counted, so the criterion cannot pass vacuously if the operator-new override ever stops being linked (the `aether_reverb_test.cpp:36-37` lesson). Then `AllocationScope` around 60 s of rendering with every setter exercised mid-render — including `setEarlySizeMs`, `setFreeze`, `setSeed`, `setDamperDepth` — records **zero** allocations, and `getAllocatedBytes()` is constant across the render. |
| **SC-011** determinism | `CavernVerb_Determinism` | Two instances, same seed, P-2's ragged partitions: `compareFingerprints` inside `kSampleTolerance`/`kMetricTolerance`. Different seeds: `totalVariation` differs by more than the tolerance. `setSeed(s); reset();` mid-life restores the opening trajectory. Third arm: `{37,111,513}` vs one 4096-sample call → same fingerprint (FR-007's absolute grid). **Fourth arm (FR-060), new in this revision:** configure a non-default control set (`size`,
`darkness`, `earlySizeMs`, `damperDepth`, `mix`) **before** `prepare()`, prepare, render, and require
the fingerprint to match an instance configured identically **after** `prepare()`. FR-060 requires
every setter to be accepted before `prepare()` and re-applied by `prepare()`/`reset()`, and the plan
implements it with the shadow-copy block (S2.5) plus S3.1 step 10 — but no other test configures an
instance before preparing (S10.3's fixture applies defaults *after* preparing), so a shadow copy that
is never re-applied would be invisible. A fifth arm does the same across `reset()`: freeze, render,
`reset()`, and require `isFrozen()` to still agree with the recorded state (S3.2's
`engine_.setFreeze(ctlFreeze_)` re-issue). **No committed digests**
(`tools/lint-float-bit-goldens.js`). |
| **SC-012** extension inert | `AetherReverb_DamperOffsetInert` (new TU) + the whole suites | **(a)** the clause with teeth is a **recomputation**: over `size × decaySeconds × density × damping` (≥ 5 values each) × `N ∈ {8,16}`, with all offsets zero, recompute `gDC = 10^(-3m/(T60_dc·sr))`, `gNyq` at `T60_nyq = T60_dc·0.05^damping`, `ratio = clamp(gNyq/gDC,0,1)`, `c = clamp(2r/(1+r), 0.001f, 1.0f)` from `getEffectiveDelayLengthSamples(i)` and the sample rate exactly as `:3140-3148` does, and `REQUIRE` agreement with `getEffectiveDampingCoefficient(i)` for every line **to within a relative `1e-5`** (amended 2026-09-17; the earlier "exact float equality" is unattainable because the shipped engine's `dampCoeff_` is not the correctly-rounded value of its own documented law under `/fp:fast` — the measurement and the `damping == 0` proof are in SC-012 (a) and in the TU). The two-instance render comparison is kept only as a **labelled smoke check** (bare engine vs one given an all-zero vector, bit-identical over 10 s — two runs of the same build, so the cross-toolchain objection does not apply and nothing is committed). **(b)** `dsp_effects_tests` **and** `dsp_systems_tests` pass with **no file** under `dsp/tests/unit/effects/aether_reverb_*` or `dsp/tests/unit/systems/seraphis_*` edited, asserted by inspecting the phase diff. **(c)** `Seraphis.vst3` passes pluginval strictness 5. FR-040's hostile-input contract and FR-047's frozen-initialisation invariant are **not** covered by (a)'s all-zero sweep and are gated separately by the two S10.4a cases in this same TU. |
| **SC-013** latency | `CavernVerb_Latency` | Render (i): impulse at `mix 0` emerges delayed by exactly `getLatencySamples()`. Render (ii): impulse at `mix 1`, `earlyLevel 1`, `earlySend 0`; arrival `i` lands at `getLatencySamples() + getEarlyTapDelaySamples(i)` ±1 sample. Both at `spectralDiffusionEnabled ∈ {true,false}` × `diffusionFftSize ∈ {256, 1024, 4096}`. |
| **SC-014** soak | `CavernVerb_Soak` (`[long]`) | 30 min at arm (b)'s configuration, **continuously generated** G-2 for the full 30 minutes (never a looped buffer), peak-normalised `|x| ≤ 1`, unfrozen. **(a)** `max|out| <= 4.0` — derived as `1.0·kEarlyGainSum` (feed-forward, FR-028) + an order-unity late field under an equal-power mix of unit maximum gain; the measured peak is recorded, and a measurement above 4.0 is a defect to fix, never an occasion to re-derive the bound. **(b)** per-minute RMS: no strictly monotone run of length 30, and the last minute within ±3 dB of the tenth. **(c)** `getNonFiniteRecoveryCount() == 0` and no non-finite output sample (bit-pattern test, not `std::isnan`). **(d)** a second 30-min render frozen at 60 s with G-3 thereafter: `getStateEnergy()` within **±0.5 dB** over the remaining 29 minutes — this is where "neither dies nor explodes overnight" is actually tested. |
| **SC-015** sample rates | `CavernVerb_SampleRates` | `prepare()` at 44 100 / 48 000 / 96 000 / 192 000 Hz + a 10 s render each: no non-finite output; **ER tap times agree across rates within ±0.05 ms** (a time-unit tolerance, because "±1 sample" is unit-ambiguous across four rates); `getLatencySamples()` consistent with the configured FFT size; `getAllocatedBytes()` **reported, and asserted against the formula, never against linearity** — within `[1×, 2×]` of `4 * (nextPowerOf2(sr*maxEarlySeconds + 5) + 4*nextPowerOf2(alignSamples + 5))` (S3.1: the buffers are power-of-two quantised and the alignment lines are rate-independent, so 4× the rate is a 6.67× step at the extremes and 1.67× at a fixed config); re-`prepare()` at a new rate mid-life matches a freshly constructed instance within `render_fingerprint.h` tolerances. |
| **SC-016** mirrored constants | `CavernVerb_MirroredConstants` | **(a)** `setDecaySeconds(0.4)` vs `(0.5)`, and `(61)` vs `(60)`: measured broadband T60s within 5 %. **(b)** prepared at `numChannels = 16`: `getEffectiveDelayLengthSamples(15) != 0.0f` and `(16) == 0.0f`. **(c)** amended 2026-09-17, the original target being arithmetically unreachable (the law places the 0.05 between DC and **Nyquist**, not between two octave bands): **(c-1)** `kDampingNyquistRatio` is solved back out of `getEffectiveDampingCoefficient(i)` and `getEffectiveDelayLengthSamples(i)` over the swept `setDarkness` arms and must agree with `0.05f` to within **1 %** on every line; **(c-2)** at `setDarkness(1.0)` the measured 8 kHz octave-band T60 is **≤ 0.35 ×** the measured 125 Hz band T60, reported as a figure. |

Additionally, a **small non-finite sentinel** (60 s, arm (b)'s configuration) lives in
`cavern_verb_test.cpp` rather than only in the `[long]` soak, so the cross-platform NaN/Inf guard
stays in the per-push lane (the project's `[long]` tag convention: never tag a NaN/Inf-guard test).
It carries the SC-014 (c) assertions **and a non-finite INPUT clause**: inject NaN and +Inf samples
into `inL`/`inR` for one block, then `REQUIRE` that once the ER line has flushed the output is finite
for the remainder of the render, that `getEarlyTapAbsorptionCutoffHz(i)` and `getStateEnergy()` are
finite, and that `getNonFiniteRecoveryCount()` is unchanged (FR-064 is a *replacement*, not a
recovery). Without this clause, FR-064's boundary sanitisation is untested — SC-014 (c) only asserts
finite output under the **finite** G-2 input — and the S5.1 defect it guards (a NaN latching all 12
per-tap one-pole states permanently) would ship green.

### S10.4a FR-gated cases with no criterion of their own

The spec's criteria leave a set of load-bearing FRs unmeasured: each row below is a case this revision
adds so that dropping the FR **fails a test** instead of passing silently. Same TUs, same
`[effects][cavern]` tags, all in the per-push lane.

| Case (TU) | FR | Assertion strategy |
|---|---|---|
| `CavernVerb_EarlyAbsorption` (main) | FR-025, FR-027 | **(i)** `getEarlyTapAbsorptionCutoffHz(i)` matches `fc_i = fcMax·(fcMin/fcMax)^(v·i/(n−1))` within 1 % at `v ∈ {0, 0.5, 1}`, with `fcMax`/`fcMin` derived from `kEarlyAbsorptionNyquistFraction`/`kEarlyAbsorptionSpanFraction` (S5.3) — and **exactly `fcMax` for every tap at `v = 0`** (the FR-025 bypass). **(ii)** at `earlySend 0`, per-arrival HF content (spectral centroid of a short window around each arrival, `spectralCentroidHz`) is **non-increasing in tap index at `v = 1`**, with tap 11 at least 6 dB duller than tap 0, and **flat within ±5 % at `v = 0`**. A single shared filter passes (i) but fails (ii) — under one shared state the tap ordering would not matter — which is how "one state per tap" is actually gated. **(iii)** a `v: 0.6 → 0 → 0.6` transition through P-5's detector: **zero** detections, gating the bypass entry/exit path (S5.3 zeroes each `taps_[i].state` on leaving bypass). Before this revision no test read `getEarlyTapAbsorptionCutoffHz` at all, and `setEarlyAbsorption` appeared only in SC-010's allocation sweep. |
| `CavernVerb_ProcessContract` (main) | FR-005 | Output buffers poisoned with a sentinel and left **untouched** when any of the four pointers is null; a `processStereoBlock(.., 0)` call inserted between two 64-sample calls leaves the render **bit-identical** to the same render without it (no state advances — a regression that advanced `sampleCounter_` on a zero-length call would also break FR-007's grid alignment and is otherwise caught by nothing); an **unprepared** instance writes silence over the poisoned buffers. `spec.md:1243-1245` enumerates all three. |
| `CavernVerb_SilenceContract` (freeze) | FR-006 | Prepared with `spectralDiffusionEnabled = true` so `alignSamples_ > 0`; render 2 s of G-2, call `silence()` mid-render, continue: **zero** P-5 detections over the following second, and `getStateEnergy()` at or below the −80 dBFS gate once the engine's own 20 ms gate and amortized clear have completed. This is the only test that fails if the four alignment lines are "tidied" into the clear (S3.2, R-14) — no criterion in the spec calls `silence()`. |
| `CavernVerb_BreathDualTarget` (main) | FR-017 | With `damperDepth 0` and everything else at FR-066 defaults, compare `setBreath(0)` and `setBreath(1)`: **(i)** the variance over time of `getEffectiveDelayLengthSamples(i)`, sampled once per control chunk over 10 s, is exactly 0 at breath 0 and > 0 at breath 1 on at least `numChannels_ − 1` lines — that gates `setSizeBreathDepth`; **(ii)** with the size-breath contribution held fixed (`setSizeBreathDepth` forced equal on both sides through a bare `AetherReverb` reference pair), the impulse responses still differ beyond `render_fingerprint.h`'s `kMetricTolerance` in total variation — that gates `setDimensionalityTideDepth`, the only remaining difference. Dropping either target fails one clause. Nothing else can detect it: SC-001 clause 1 sets `setDimensionality(1.0)`, which is the morph *target*, not the tide depth. |
| `CavernVerb_MixLaw` (main) | FR-063 | Uncorrelated dry and wet (G-2 in, a long-decay configuration so the wet bus is uncorrelated with the input), rendered at `mix ∈ {0, 0.25, 0.5, 0.75, 1}`: total output power stays within **±0.5 dB** across the sweep. A linear crossfade dips ≈ 3 dB at `mix = 0.5` and fails. This is also lever **L-3's acceptance gate** (S12.3): an L-3 control-grid lerp whose error exceeds the claimed 0.1 % shows up here. Before this revision every criterion but SC-013 ran at `mix = 1`, and SC-013 renders only `mix ∈ {0, 1}`. |
| `AetherReverb_DamperOffsetHostileInput` (extension) | FR-040, FR-044, FR-045 | Publish `{NaN, +Inf, −Inf, 1e30, −1e30, …}` with `count > numChannels_`: every `getEffectiveDampingCoefficient(i)` is finite and within `[0.001f, 1.0f]`; render 10 s of G-2 under that vector and require bounded, finite output — FR-045's "no offset vector can make the loop expansive". Then publish `nullptr`, and `count == 0`, and require every coefficient to return to `dampCoeff_` by **exact float equality** (FR-044). `spec.md:1264-1266` enumerates exactly this call, and the method is being appended to a **shipped** header, so the contract is gated rather than asserted. |
| `AetherReverb_DamperOffsetFrozenInit` (extension) | FR-047 | `prepare()`, then `setFreeze(true)` **immediately**, then render 1 s: every `getEffectiveDampingCoefficient(i)` equals the recomputed shipped coefficient by exact float equality, and the output is finite and non-zero. This gates FR-047's *invariant* directly; E-6's own rationale for how the invariant could be broken was wrong (S7) and the invariant still needs a test, because SC-012 (a) sweeps unfrozen configurations and SC-001/SC-008 always render before freezing. |

### S10.5 Runtime ledger (so a slow suite is a known number, not a surprise)

| Test | Audio rendered | Expected wall clock (Release, 48 kHz) | Lane |
|---|---|---|---|
| `CavernVerb_EchoDensity` | 6 × ~1 s IR | < 5 s | per-push |
| `CavernVerb_EarlyReflectionGeometry` | 9 × ~1 s IR | < 5 s | per-push |
| `CavernVerb_DarkTuning` | ~60 s | ~10 s | per-push |
| `CavernVerb_DamperMotionSmoothness` | 2 × 120 s + one ~40 s transition render (P-1b size sweep) | ~30 s | per-push (borderline) |
| `CavernVerb_EarlyAbsorption` | 5 × ~1 s IR + one ~3 s transition | < 5 s | per-push |
| `CavernVerb_ProcessContract` | < 1 s | < 1 s | per-push |
| `CavernVerb_SilenceContract` | ~4 s | < 5 s | per-push |
| `CavernVerb_BreathDualTarget` | 4 × ~10 s | ~5 s | per-push |
| `CavernVerb_MixLaw` | 5 × ~5 s | ~5 s | per-push |
| `AetherReverb_DamperOffsetHostileInput` | ~10 s | ~2 s | per-push |
| `AetherReverb_DamperOffsetFrozenInit` | ~1 s | < 1 s | per-push |
| `CavernVerb_DamperDecorrelation` | 2 × 600 s + null bank | ~60 s | **`[long]`** |
| `CavernVerb_DamperSpectralMotion` | 5 depths × 8 seeds × 120 s | ~8 min | **`[long]`** |
| `CavernVerb_FreezeEnergyConservation` | 2 × ~65 s | ~25 s | per-push |
| `CavernVerb_FreezeCycles` | ~82 s | ~20 s | per-push |
| `CavernVerb_Soak` | 2 × 1800 s | ~12 min | **`[long]`** |

`[long]` is applied per the project convention (> ~15 s **and** a toolchain-independent failure mode).

---

## S11. Build integration — the exact edits

1. **`dsp/tests/CMakeLists.txt`**, inside the enumerated `add_executable(dsp_effects_tests …)` block
   (`:504-533`), after `unit/effects/aether_reverb_nonfinite_test.cpp`:
   ```cmake
   # Vorago Phase 9 (specs/vorago-phase9-cavern-space)
   unit/effects/cavern_verb_test.cpp
   unit/effects/cavern_verb_freeze_test.cpp
   unit/effects/cavern_verb_soak_test.cpp
   unit/effects/cavern_verb_perf_test.cpp
   unit/effects/aether_reverb_damper_offset_test.cpp
   ```
   A TU not listed here silently drops out of the build and its cases never run. All five must live in
   **this** target: it carries `KRATE_DSP_AETHER_TEST_HOOKS` target-wide (`:549`), and the comment at
   `:541-548` states why that cannot be per-source — the define changes `AetherReverb`'s class
   definition, so two differently-defined images would be an ODR violation.
2. **`dsp/lint_all_headers.cpp`**, Layer 4 block beside `:197`:
   `#include <krate/dsp/effects/cavern_verb.h>` with a `// Vorago Phase 9 …, FR-001` comment. A header
   absent from this TU is never strict-lint compiled.
3. **`dsp/CMakeLists.txt`**, `KRATE_DSP_EFFECTS_HEADERS` (`:180-194`): add
   `include/krate/dsp/effects/cavern_verb.h` (IDE/source-group list; not lint-gated, but the effects
   block is complete today and should stay so).
4. **`tests/test_helpers/reverb_metrics.h`** — new file; **no CMake edit** (`test_helpers` is an
   INTERFACE target exposing the whole directory).
5. **`dsp/tests/unit/effects/fdn_reverb_test.cpp:328-373`** — replaced by the lifted call (FR-080);
   `REQUIRE(ned >= 0.8)` unchanged.
6. **`dsp/include/krate/dsp/effects/aether_reverb.h`** — the six append-only edits E-1 … E-6 (S7).
7. **No** workflow edit and **no** clang-tidy script edit: a DSP-library phase with no new target and
   no new plugin.

### S11.1 Commands

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"

# build + run the layer that owns the new TUs
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_effects_tests
build/windows-x64-release/bin/Release/dsp_effects_tests.exe "~[long]~[.perf]" 2>&1 | tail -5

# SC-012 (b): the untouched consumers of the extended header
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests seraphis_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe 2>&1 | tail -5
build/windows-x64-release/bin/Release/seraphis_tests.exe 2>&1 | tail -5

# the [long] set, explicitly (per-push CI excludes it; it runs nightly on all three OS legs)
build/windows-x64-release/bin/Release/dsp_effects_tests.exe "[long][cavern]" 2>&1 | tail -5

# gates
node tools/lint-odr.js && node tools/lint-layers.js && node tools/lint-arch-guarded-includes.js \
  && node tools/lint-float-bit-goldens.js && node tools/lint-nonfinite-symbols.js
node tools/check-portability.js
./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja

# SC-012 (c)
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Seraphis.vst3"

# perf, ALONE, nothing else executing (P-4)
node tools/run-cpu-tests.js dsp_effects_tests
```

---

## S12. CPU budget

### S12.1 Basis

Inherited verbatim from `aether_reverb_perf_test.cpp:133-147`: nanoseconds per 512-sample block at
48 kHz (a percent-of-core figure is not reproducible across machines); `kReferenceNs = 533 333.3`;
`kMaxAdmissibleNs = 355 555.6`; `kRegressionFactor = 1.5`. A **global** figure — one instance for the
whole engine — which does not multiply by polyphony (`:49-53`).

### S12.2 Projection, made before the code is written

| Item | Per 512-block | Basis |
|---|---|---|
| owned `AetherReverb`, arm (a) analogue | ~114 600 ns | shipped default baseline `:326`, which **includes** shimmer and bloom that this phase does not construct — so it over-estimates |
| owned `AetherReverb`, arm (b) analogue | ~200 100 ns | shipped worst `:330`, likewise shimmer/bloom-inclusive, at N = 16 and spectral @4096 |
| ER stage: 12 taps × (readLinear + one-pole + MAC + delay lerp) ≈ 14 flops/tap/sample | ~35 000 ns | 512 × 12 × 14 ops at ~0.4 ns/op |
| damper generation: N × `processBlock(64)` × 8 chunks | ~2 000 ns | 2 control steps + a smoother advance each |
| `applyDamperOffsets`: 2 transcendentals × N × 8 chunks | ~13 000 ns at N = 16 | pessimistic 100 ns per `pow` |
| mix + alignment: 4 line reads, 2 `cos`/`sin`, ~6 MAC per sample | ~12 000 ns | the transcendentals dominate; lever L-3 removes them |
| **arm (a) projection** | **≈ 150 000 ns** | 42 % of `kMaxAdmissibleNs` |
| **arm (b) projection** | **≈ 250 000 ns** | 70 % of `kMaxAdmissibleNs` |

Both arms project inside the ceiling with ≥ 30 % margin, consistent with the spec's "≳ 150 000 ns of
room measured against that shimmer-inclusive, 4096-point worst case". The FR-065 dormancy skip
removes the ~35 000 ns ER term when both ER gains are zero, which is also part of arm (d)'s difference.

**Measured 2026-09-17 (record; the projection above is history).** Pinned to the performance cores,
alone, best-of-25 × 500 blocks, ns per 512-sample block at 48 kHz:

| Arm | DATASET 1 (each arm in its own loop) | DATASET 2 (interleaved trio) | Baseline ⌈× 1.05⌉ | % of `kMaxAdmissibleNs` |
|---|---|---|---|---|
| (a) default | 123 000 | 124 497 | **129 150** (D1) | 35 % |
| (b) worst case | 181 279 | 205 107 | **190 343** (D1) | 51–58 % |
| (c) frozen | 110 811 | 112 003 | **117 604** (D2) | 31 % |
| (d) damper off | 144 737 | 119 509 | **125 485** (D2) | 34 % |

The projection over-estimated arm (a) by ~20 % and arm (b) by ~30 %. Two isolated pinned runs one
minute apart moved (c)/(d) by +16 % / −20 % when each arm was timed in its own loop (run 1 failed (d)'s
1.10 relative clause, run 2 passed); ruled: (a), (c), (d) are timed interleaved (S14 item 6), tolerance
unchanged. The interleaved (c)/(d) figures repeat within 1 % run to run. No lever from S12.3 was needed.

### S12.3 Ordered lever list, if an arm misses (apply in order; never raise a baseline — FR-082)

- **L-1.** In `applyDamperOffsets`, hoist `1 - dampCoeff_[i]` and recompute only when either
  `dampCoeff_[i]` or `damperOffset_[i]` actually changed (in the steady state only the offset moves,
  so this mainly pays while Size/Decay/Damping are still settling).
- **L-2.** Replace `pow(1-c, p)` with `exp2(p * log2(1-c))` and hoist `log2(1-c)` into the
  `updateDecayAndDamping` recompute — one `exp2` per line per chunk in the steady state, to the same
  float law.
- **L-3.** Hoist the equal-power `cos`/`sin` to the control grid and lerp both gains per sample (the
  mix smoother is 50 ms; the lerp error over 64 samples is < 0.1 % of full scale).
- **L-4.** Skip the per-tap absorption one-pole for taps whose `fc_i` has been Nyquist-clamped to
  `fcMax` (at low rates the early taps are effectively bypassed anyway).
- **L-5.** If and only if L-1…L-4 are insufficient: **stop and surface** the measurement with the arm,
  the configuration and the number, as Phases 2, 5 and 6 did. Do **not** reduce `kEarlyTapCount`, do
  not shrink a workload, do not relax the reference.

---

## S13. Risks and mitigations

| # | Risk | Mitigation |
|---|---|---|
| R-1 | **The ER tap table cannot satisfy FR-022 as written** (S0.2 B-1) | Resolved before implementation: the shipped table, its verified metric and the one constant that must move are all in this plan, with the alternatives costed. |
| R-2 | **SC-003 (a)/(b)'s constants are wrong by 4.91× (time constant) and 1.5× (published swing)** (B-2) | Corrected in S0.2 B-2 and in the SC-003 row: the test writes the bound as the parameterised expression `alpha * (kMaxDamperOctaves/kInternalStd + kMaxDamperOctaves)`, never a literal; the two spec literals are surfaced for correction under FR-082; the accessor's clamp semantics remain one ruling, visible in the source. |
| R-3 | **Depth applied twice** (B-3) | Ruled: `setDepth` only; the header comment cites `brownian_drift.h:249-251` (`outputTarget()`) so a later reader cannot re-add the multiply. |
| R-4 | `applyDamperOffsets()` placed outside the `!freezeTarget_` branch → the latch is not inherited | **The SC-001 claim previously made here was false and is struck.** While frozen the damping one-pole contributes nothing: `crossfade(filterState_[i], delRead[i], freezeRamp)` is written so `freezeRamp == 1` returns **exactly** `delRead[i]` (`aether_reverb.h:2996-3001`, read site `:4277-4284`, and the comment at `:4277-4280` says so), and SC-001 waits 0.25 s and `REQUIRE`s `isFrozen()` — so the frozen output is independent of `dampCoeff_` and of `effectiveDampCoeff_`, and SC-001's ±0.5 dB energy clause is insensitive to the damper path by construction. E-5's placement is justified on **cost and latch-consistency** grounds (the `:3600-3606` reason, quoted in the header), and it is **gated** by SC-001 clause 3 (S10.4): sample `getEffectiveDampingCoefficient(i)` through E-3 once per control chunk over a frozen span at `damperDepth 1`/`damperRate 1` and require bit-identity across the span, then motion again after thaw. That is observable regardless of `freezeRamp`. |
| R-5 | `c = 1.0` (damping 0) silently makes the dampers inert | The `1 - 1e-6` upper clamp in E-4, with the reason in the header; SC-005's direction clause is rendered at `setDarkness(0.0)` **and** at the default, so a total-inertness bug at the bright end cannot hide. |
| R-6 | Per-chunk tap-delay steps zipper on a `setEarlySizeMs` sweep | Per-sample delay ramp (S5.2); `DelayLine::LinearTap` deliberately not used, with the corrected arithmetic that rejects it recorded (**11.4 ms per chunk**, 548 samples at 48 kHz — the earlier 4.6 ms figure carried B-2's time-constant error). **Gated** by SC-003 (d)'s `setEarlySizeMs` 80 → 600 → 80 sweep on fixture P-1b through P-5's detector, which is new in this revision: before it, no test rendered a size change at all. |
| R-7 | Denormals in the ER path after a long silence | Per-chunk flush of the 12 tap states below `1e-20f`, **plus** a per-sample flush of the ER-line input (S5.4, S9). The rationale previously given — "the ER is feed-forward, so a denormal cannot recirculate" — was wrong: the per-tap one-pole *is* a recirculating IIR. The correct argument is that the per-chunk flush **bounds** the tap-state exposure to 64 samples (the stage is not recursive across taps), and the ER-line flush covers the line contents, which the tap-state flush does not touch. With both, the "FTZ/DAZ not relied on" claim holds. |
| R-8 | An 18 kHz absorption cutoff above Nyquist at 8–32 kHz rates | `fcMax = min(18 kHz, 0.45·sr)`, `fcMin = min(1200, 0.4·fcMax)` (S5.3); SC-015 renders at four rates, SC-006 (a) at three. |
| R-9 | A second TU including `<allocation_operator_overrides.h>` → duplicate-symbol link error | Only `aether_reverb_test.cpp:38` owns it in `dsp_effects_tests`; the new TUs include `<allocation_detector.h>` only, and SC-010's clause 0 proves the counter is armed. |
| R-10 | The `constexpr` gain-sum assert needs a `constexpr exp` | The ascent, coprimality and incommensurability asserts are exact integer/ratio arithmetic and are genuinely `constexpr`; only the gain sum needs `exp`, and the fallback (a `REQUIRE` in the geometry case, permitted by FR-022's own wording) is recorded so it is a choice, not a silent drop. |
| R-11 | Cross-toolchain drift in the ER arrival picker (SC-006's ±1 sample) | Feed-forward reads of a mono line with linear interpolation; arrivals are isolated by the 60 ms floor and the **4.1683 ms** minimum tap spacing (≥ 200 samples at 48 kHz), far outside any float-order sensitivity. |
| R-12 | MSVC-green, GCC/Clang-red | `node tools/check-portability.js` over all touched files; no narrowing in brace init; designated initialisers for both `PrepareConfig`s; `std::size_t` literals suffixed; no `std::isnan` family; no arch-guarded include. |
| R-15 | `reset()` silently thaws a frozen instance (`AetherReverb::reset()` clears `freezeTarget_`, `:1979`) while `ctlFreeze_` still reads `true` | `reset()` re-applies the **full** shadow set, `engine_.setFreeze(ctlFreeze_)` included, exactly as `prepare()` step 10 does (S3.2); SC-011's fifth arm renders freeze → `reset()` → `isFrozen()` and fails if the re-issue is dropped. |
| R-16 | `erSizeSm_` advanced twice per control chunk (once in S3.3 step 2, once in S5.2) → halved smoothing time and a step at every chunk boundary | The advance is specified **once**, inside `refreshControlState()` where `prevMs`/`nextMs` bracket it; S3.3 step 2 states explicitly that it does **not** advance it (S3.3, S5.2). SC-003 (d)'s size sweep is the behavioural gate. |
| R-17 | An unprepared alignment `DelayLine` written and read on the audio thread when `alignSamples_ == 0` (out-of-bounds heap write, every sample, whenever `spectralDiffusionEnabled == false`) | All four lines are prepared unconditionally (S3.1 step 6); the conditional-copy path is deleted from S5.4. SC-013 renders that configuration. |
| R-18 | A non-finite input sample latches all 12 per-tap one-pole states to NaN permanently, with `getNonFiniteRecoveryCount()` reporting 0 | Input is sanitised **once**, before the ER line and the dry scratch (S5.1, S5.4); the per-push non-finite sentinel injects NaN and +Inf for one block and requires finite output thereafter (S10.4). |
| R-13 | `getStateEnergy()` is `O(Σ m_i)` and SC-001/SC-008/SC-014 (d) sample it repeatedly | Never called from `processStereoBlock` (the engine's own note, `:2552-2556`); the freeze and soak TUs call it between blocks only, once per second at most. |
| R-14 | The four alignment lines cleared by `silence()` punch an `fftSize` hole ending in a full-amplitude step | They are deliberately **not** cleared, with `:3625-3639`'s reasoning copied into the header. **The stated mitigation — "SC-008 (b) would catch a regression" — was false and is struck:** SC-008's pinned timeline is `2 s G-2 → [freeze, 5 s G-3, thaw, 3 s G-2] × 10` (`spec.md:1088`) and never calls `silence()`; no criterion in the spec calls it. The real mitigation is `CavernVerb_SilenceContract` (S10.4a), added in this revision, which calls `silence()` mid-render with `alignSamples_ > 0` and requires zero P-5 detections plus `getStateEnergy() ≈ 0`. |

---

## S14. Open questions for the user

**Rulings 2026-09-16 (all five items closed, encoded in spec.md's Clarifications log and the FR/SC
bodies):** 1 → order 6, everything else unchanged. 2 → post-clamp accessor, FR-037 amended; SC-003
(a)/(b) rewritten as the parameterised law (0.19562 / 0.04988). 3 → confirmed, depth once via
`setDepth`. 4 → confirmed, `≤ 0.70 ×` against the bare defaults. 5 → FR-025 amended to include the
guard with the two named constants. Also confirmed: registration up front in T001, and R-10's
`static_assert`-or-`REQUIRE` choice left to the implementer. The items are kept below as the record
of what was asked.

**Ruling 2026-09-17 (build stage), item 6:** SC-009's arms (a), (c) and (d) are timed in one
interleaved trial loop (`measureTrio` in the perf TU), tolerance 1.10 unchanged; the baselines are
⌈measured × 1.05⌉ from DATASET 1 for (a)/(b) and from DATASET 2 for (c)/(d) — see S12.2's record.

1. **B-1 — `kIncommensurabilityOrder`.** FR-022's inequality at order 8 with tol 0.05 is not
   satisfiable together with FR-028's `Σ|g_i| ≤ 2.0` (best achievable 0.0425 over real delays,
   0.0452 over pairwise-coprime integer series, against a required 0.05).
   **Recommendation: order 6**, everything else unchanged; shipped table and its measured 0.05411 are
   in S0.2. Alternatives: tol → 0.04 at order 8 (0.0452 achieved, 13 % margin), or
   `kEarlyGainG0` → 0.40 at order 8 (still knife-edge at 0.0504, no coprime realisation found).
2. **B-2 — SC-003 (a)/(b) and FR-037.** Two things, one of which needs a decision and one of which is
   simply an arithmetic correction the spec must take:
   (i) **The spec's bound is wrong on two independent factors** and a correct implementation fails it:
   `BrownianDrift::kDriftOutputSmoothMs = 150` is a **time to 99 %**, not a time constant
   (`smoother.h:77-93`), so the per-chunk approach fraction at 48 kHz is **0.0434712**, not 0.0088495;
   and the published quantity is `clamp(3·s, ±1.5)`, whose worst single-chunk step is
   `alpha · (kMaxDamperOctaves/kInternalStd + kMaxDamperOctaves) = alpha · 4.5`, not `alpha · 3`.
   `kMaxOffsetStepPerChunk` becomes **0.19562** octaves/chunk and clause (b)'s bound **0.04988**.
   Under FR-082's stop-and-surface rule the literals at `spec.md:931-941` and `:951` (0.02655,
   0.00677) are surfaced here for correction, not silently kept. No threshold is being relaxed — the
   corrected number is still a hard smoother property that binds every chunk on every line.
   (ii) **The decision that remains:** `getDamperOffsetOctaves` publishes the **post-clamp** value
   (recommended, and what the engine is actually told; bound `alpha · 4.5`) or the pre-clamp value
   (bound `alpha · 6.0 = 0.26083`). **Recommendation: post-clamp**, and strike "pre-clamp" from
   FR-037. The test writes the bound as the parameterised expression either way.
3. **B-3 — `setDamperDepth` path.** Ruled in this plan: depth applied once, via
   `BrownianDrift::setDepth`; the `v ·` in FR-031's written formula is realised by that call and is
   **not** a second multiply. Flagged for acknowledgement rather than decision.
4. **SC-007 (b)'s "at least 30 % lower".** Implemented as `centroid_cavern <= 0.70 × centroid_bare`,
   with the bare reference at `AetherReverb`'s own defaults (`kDefaultDamping = 0.40`,
   `kDefaultSize = 0.50`, `:2740`, `:2730`). No ambiguity expected; both figures are reported so a
   near-miss is diagnosable rather than a bare red.
5. **FR-025's Nyquist guard is a spec deviation, and is recorded as one.** FR-025 pins
   `kEarlyAbsorptionFcMaxHz = 18000` and `kEarlyAbsorptionFcMinHz = 1200` as *the constants of the
   law* (`spec.md:440-444`), with no rate dependence; S5.3 computes
   `fcMax = min(18000, kEarlyAbsorptionNyquistFraction·sr)` and
   `fcMin = min(1200, kEarlyAbsorptionSpanFraction·fcMax)`, which changes the law below 40 kHz.
   It is legitimate (R-8 — 18 kHz is above Nyquist at 8–32 kHz rates) but it was previously carried
   only as a risk, not as a deviation. **Recommendation: amend FR-025 to state the guard as part of
   the law, naming `kEarlyAbsorptionNyquistFraction = 0.45f` and
   `kEarlyAbsorptionSpanFraction = 0.40f` as public constants (S2.2)**, so the header, the spec and
   `CavernVerb_EarlyAbsorption` (S10.4a) all read the same law. Independently of the ruling, SC-006
   now pins `setEarlyAbsorption` explicitly, so its 44.1 kHz and 96 kHz arms cannot run under
   different cutoff sets by accident.

---

## S15. Review notes

The review of this plan produced 26 issues (2 blockers, 12 majors, 12 minors). **All 26 were accepted
and applied; none was rejected**, and no threshold was relaxed to resolve one. Two entries are
recorded here because the resolution taken is one of the two the issue permitted, and the choice
should be visible:

1. **E-6 (`AetherReverb::reset()` damper-array initialisation).** The issue showed that E-6's stated
   rationale was wrong — `reset()` clears `freezeTarget_` at `:1979` before reaching
   `refreshControlState()` at `:2099`, so the `!freezeTarget_` branch always runs at least once and
   the scenario E-6 claimed to prevent cannot occur — and offered either dropping E-6 or keeping it
   with a corrected rationale. **E-6 is kept, with the rationale corrected and the false claim
   struck** (S7): it costs `kMaxChannels` stores per `reset()` and it removes the dependence of an
   invariant on a control-flow ordering 120 lines away that a future edit could reorder. Separately,
   and regardless of that choice, FR-047's *invariant* is now gated by
   `AetherReverb_DamperOffsetFrozenInit` (S10.4a) — the reviewer's other minor on the same section —
   because no existing criterion exercised prepare → immediate freeze.
2. **B-2's ruling.** The arithmetic corrections (approach fraction and published swing) are applied
   unconditionally, because they are facts about shipped headers read this session. The *choice*
   between a post-clamp and a pre-clamp accessor remains an open question for the user (S14 item 2),
   as it was before the review; the recommendation is unchanged (post-clamp), only its number moved.

Two spec literals are surfaced for correction under FR-082 rather than being quietly carried:
`spec.md:931-941`'s 0.02655 → **0.19562** and `:951`'s 0.00677 → **0.04988** (S14 item 2). The
deviation at FR-025 is likewise promoted from a risk row to S14 item 5.
