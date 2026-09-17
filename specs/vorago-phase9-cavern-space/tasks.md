# Tasks: Vorago Phase 9 — Cavern Space Engine

**Spec:** `specs/vorago-phase9-cavern-space/spec.md`
**Plan:** `specs/vorago-phase9-cavern-space/plan.md`
**Roadmap:** `specs/Vorago-roadmap.md` → Part A → Phase 9 (lines 410–430), Open Question 3 (569–570)
**Deliverables:** one new Layer 4 header `dsp/include/krate/dsp/effects/cavern_verb.h`; six append-only
edits to the shipped `dsp/include/krate/dsp/effects/aether_reverb.h`; one lifted test helper
(`tests/test_helpers/reverb_metrics.h`); five new test TUs in `dsp_effects_tests`.
**Plugin work:** none (Vorago's plugin starts at Phase 11).

All paths are repo-relative to `f:/projects/iterum`.

---

## How to execute these tasks

**Canonical order inside every task, no exceptions:**

1. Write the failing test **first**, in the named file, with the named `TEST_CASE` / `SECTION` and
   the exact assertions given. Build, run it, and confirm it **fails** (or fails to compile, where
   the task says so).
2. Implement the minimum that makes it pass.
3. Fix **all** compiler warnings introduced by the change — zero warnings is the gate, not a goal.
4. Run the named verification command and confirm green.

**Build and run (Windows, always the full CMake path):**

```bash
CMAKE="/c/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_effects_tests
build/windows-x64-release/bin/Release/dsp_effects_tests.exe "~[long]~[.perf]" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_effects_tests.exe "CavernVerb_EchoDensity" 2>&1 | tail -5   # one case
```

Catch2 filters are **positional** (`<exe> "CavernVerb_*"`), tags go in brackets
(`"[long][cavern]"`). Never use `ctest -R <exe>` — `catch_discover_tests` registers case names, not
executable names.

**Parallelism.** Tasks marked **[P]** inside the same GROUP touch fully disjoint **new** files and
may run concurrently. Every task that edits a shared file (`dsp/tests/CMakeLists.txt`,
`aether_reverb.h`, `cavern_verb.h`, `cavern_verb_test.cpp`) is alone in its group — that is why most
groups hold exactly one task.

**No commit tasks.** Commits happen outside this workflow.

**Rulings these tasks assume** (all from `plan.md` S0.2 / S14; if the user overrules one, the
affected task changes and nothing else does):

| Ruling | Value used here | Source |
|---|---|---|
| B-1 | `kIncommensurabilityOrder = 6` (not 8); shipped table `{1,53,199,277,547,709,929,1049,1381,1583,1721,1997}`, measured metric **0.05411**, `Σ\|g\| = 1.865762` | plan S0.2 B-1 |
| B-2 (i) | SC-003's per-chunk bounds are **0.19562** oct/chunk and **0.04988** in coefficient units — the spec's 0.02655 / 0.00677 are arithmetically wrong (`smoothTimeMs` is a time to 99 %, `smoother.h:77-93`, verified this session) and a **correct** implementation fails them. Tests write the **law**, never a literal | plan S0.2 B-2 |
| B-2 (ii) | `getDamperOffsetOctaves` returns the **post-clamp** published value; FR-037's "pre-clamp" is struck | plan S14 Q2 |
| B-3 | Damper depth is applied **once**, via `BrownianDrift::setDepth` — never a second multiply on `getCurrentValue()` | plan S0.2 B-3 |
| S14 Q4 | SC-007 (b) "≥ 30 % lower" is implemented as `centroid_cavern <= 0.70 × centroid_bare` | plan S14 Q4 |
| S14 Q5 | FR-025's absorption law carries the Nyquist guard `fcMax = min(18000, 0.45·sr)`, `fcMin = min(1200, 0.40·fcMax)`, with both fractions as named public constants | plan S5.3, S14 Q5 |

**FR-082 stop-and-surface (binding on every task):** no baseline may be raised, no threshold
relaxed, no workload shrunk, no tap count reduced to make a figure fit. Reduce cost, or stop and
surface the measurement to the user.

---

## Group and task map

| Group | Tasks | Theme |
|---|---|---|
| G1 | T001 [P], T002 [P] | Build wiring; the lifted NED / analysis helper |
| G2 | T003 | The `AetherReverb` append-only extension (E-1 … E-6) + its own TU |
| G3 | T004 | `CavernVerb` constants, ER tap table, lifecycle, setters, static accessors |
| G4 | T005 | The process loop, ER audio path, alignment, mix, latency |
| G5 | T006 | Stone absorption (per-tap one-poles) |
| G6 | T007 | Shaped ramps, dormancy, equal-power mix law |
| G7 | T008 | The damper bank and its publication |
| G8 | T009 | SC-004 decorrelation + SC-005 spectral motion (`[long]`) |
| G9 | T010 | SC-002 echo density |
| G10 | T011 | SC-007 dark tuning + SC-016 mirrored constants |
| G11 | T012 | SC-010 no-allocation + SC-011 determinism |
| G12 | T013 | SC-015 sample rates + non-finite sentinel + `CavernVerb_BreathDualTarget` |
| G13 | T014 [P], T015 [P], T016 [P] | Freeze TU, soak TU, perf TU (three disjoint new files) |
| G14 | T017, T018, T019 | Registration audit, full-suite run, portability + lint + Seraphis-green gates |

---

# GROUP 1 — Wiring and shared test helper

## T001 [P] — Build wiring: five TU stubs, the header stub, and every registration point

**Files created**

- `dsp/include/krate/dsp/effects/cavern_verb.h`
- `dsp/tests/unit/effects/cavern_verb_test.cpp`
- `dsp/tests/unit/effects/cavern_verb_freeze_test.cpp`
- `dsp/tests/unit/effects/cavern_verb_soak_test.cpp`
- `dsp/tests/unit/effects/cavern_verb_perf_test.cpp`
- `dsp/tests/unit/effects/aether_reverb_damper_offset_test.cpp`

**Files edited**

- `dsp/tests/CMakeLists.txt` — inside the **enumerated** `add_executable(dsp_effects_tests …)` list,
  immediately after `unit/effects/aether_reverb_nonfinite_test.cpp` (verified this session: the list
  opens at `:504` and that entry is its last source line), add:
  ```cmake
      # Vorago Phase 9 (specs/vorago-phase9-cavern-space)
      unit/effects/cavern_verb_test.cpp
      unit/effects/cavern_verb_freeze_test.cpp
      unit/effects/cavern_verb_soak_test.cpp
      unit/effects/cavern_verb_perf_test.cpp
      unit/effects/aether_reverb_damper_offset_test.cpp
  ```
  A TU that is not listed here silently drops out of the build and its cases never run. All five
  must live in **this** target: it carries `target_compile_definitions(dsp_effects_tests PRIVATE
  KRATE_DSP_AETHER_TEST_HOOKS)` target-wide (verified, `dsp/tests/CMakeLists.txt`, the comment above
  it states the define changes `AetherReverb`'s class definition, so it cannot be per-source without
  an ODR violation).
- `dsp/lint_all_headers.cpp` — in the `// Layer 4: Effects` block, beside
  `#include <krate/dsp/effects/aether_reverb.h>` (verified at `:197`), add
  `#include <krate/dsp/effects/cavern_verb.h>  // Vorago Phase 9 (specs/vorago-phase9-cavern-space), FR-001`.
  A header absent from this TU is never strict-lint compiled.
- `dsp/CMakeLists.txt` — add `include/krate/dsp/effects/cavern_verb.h` to `KRATE_DSP_EFFECTS_HEADERS`
  (verified at `:180-194`; IDE/source-group list, kept complete).

**Test first.** The only assertion in this task is that the image still builds and the suite is still
green with five new (empty) TUs linked. Each new TU carries exactly one scaffold case, which the
first real task on that TU deletes:

```cpp
TEST_CASE("CavernVerb_Scaffold", "[effects][cavern]") { SUCCEED("wiring only — deleted by T004"); }
```

(`aether_reverb_damper_offset_test.cpp` uses `AetherReverb_Scaffold`, deleted by T003.)

**Implement.**

- `cavern_verb.h`: `#pragma once`, the house banner (Layer 4; spec slug `vorago-phase9-cavern-space`;
  roadmap lines 410–430; the Open-Question-3 ruling "separate L4 class owning an `AetherReverb` by
  value, plus one append-only damper hook"; the `aether_reverb.h:1-10` banner is the format), the
  include block exactly as plan S2.1 pins it:
  ```cpp
  #include <krate/dsp/effects/aether_reverb.h>       // same-layer, permitted (tools/lint-layers.js:74)
  #include <krate/dsp/primitives/delay_line.h>       // L1
  #include <krate/dsp/primitives/smoother.h>         // L1 (OnePoleSmoother, LinearRamp)
  #include <krate/dsp/processors/brownian_drift.h>   // L2
  #include <krate/dsp/core/random.h>                 // L0 (deriveStreamSeed)
  #include <algorithm>
  #include <cmath>
  #include <cstddef>
  #include <cstdint>
  ```
  then `namespace Krate { namespace DSP { class CavernVerb { public: CavernVerb() noexcept = default; };
  }}`. No Vorago Phase 1–8 header. No `effects/fdn_reverb.h`. No `std::isnan`/`isinf`/`isfinite`
  anywhere, ever (FR-071).
- The four `cavern_verb_*` TUs include `<catch2/catch_all.hpp>` and
  `<krate/dsp/effects/cavern_verb.h>`; the extension TU includes `<catch2/catch_all.hpp>` and
  `<krate/dsp/effects/aether_reverb.h>`.
- **Never** include `<allocation_operator_overrides.h>` in any new TU:
  `dsp/tests/unit/effects/aether_reverb_test.cpp` already owns the global `operator new/delete`
  replacement for this image (verified at its `:38` with the banner explaining it at `:25-37`), and a
  second include is a duplicate-symbol link error. New TUs include `<allocation_detector.h>` only.

**Verify.** `--target dsp_effects_tests` builds with **zero warnings**;
`dsp_effects_tests.exe "~[long]~[.perf]" 2>&1 | tail -5` is green and the case count has risen by 5.

---

## T002 [P] — Lift the NED metric into a shared helper, and add the analysis toolkit (FR-080)

**Files created**

- `tests/test_helpers/reverb_metrics.h` (header-only; **no CMake edit** — `test_helpers` is an
  INTERFACE target that exposes the whole directory, verified in `tests/test_helpers/CMakeLists.txt`)

**Files edited**

- `dsp/tests/unit/effects/fdn_reverb_test.cpp` — inside
  `TEST_CASE("FDNReverb: echo density NED >= 0.8 within 50ms (SC-005)", "[effects][fdn]")` (verified
  at `:300`), replace the inline NED body (the `constexpr size_t windowSize = 48;` … `double ned =`
  block, `:338-366`) with a call to the helper. Keep the `INFO(...)` lines and keep
  `REQUIRE(ned >= 0.8);` **verbatim**.

**Test first.** The call site is written before the helper exists, so the build fails with "no member
named `normalisedEchoDensity`". That compile failure is the failing test; the behavioural gate is the
untouched `REQUIRE(ned >= 0.8)` continuing to pass after the lift:

```cpp
const double ned = Krate::DSP::TestUtils::normalisedEchoDensity(
    std::span<const float>(irMono), /*windowSamples*/ 48u, /*startWindow*/ 0u,
    /*windowCount*/ irLength / 48u);
```

`fdn_reverb_test.cpp` is neither an `aether_reverb_*` nor a `seraphis_*` TU, so SC-012 (b)'s freeze
does not cover it — this is the one existing test file this phase may edit.

**Implement** `tests/test_helpers/reverb_metrics.h`, `namespace Krate { namespace DSP { namespace TestUtils {`,
everything `inline`, no `std::isnan`/`isinf`/`isfinite`:

- `[[nodiscard]] inline double normalisedEchoDensity(std::span<const float> monoIr,
  std::size_t windowSamples, std::size_t startWindow, std::size_t windowCount)` — the lift, unchanged
  in substance: per-window RMS over `windowSamples`, `threshold = peak * 0.01` (−40 dB) with the
  **peak taken over the analysed window range only**, return `occupied / windowCount`. With
  `startWindow = 0` and `windowCount = all` this is bit-for-bit the old behaviour.
  Returns 0.0 for `windowCount == 0`.
- `struct Biquad { float b0,b1,b2,a1,a2,x1,x2,y1,y2; float process(float) noexcept; }`,
  `makeLowpass(double sr, double fc, double q)`, `makeHighpass(double sr, double fc, double q)` (RBJ).
- `struct OctaveBand { Biquad hp[2], lp[2]; float process(float) noexcept; }` and
  `makeOctaveBand(double sr, double centreHz)` — 4th-order Butterworth band, edges
  `centreHz / sqrt(2)` and `centreHz * sqrt(2)`.
- `[[nodiscard]] inline double schroederT60(const std::vector<double>& hopEnergy, double hopSeconds)`
  — backward Schroeder integration, straight-line fit over the −5 dB … −25 dB span, extrapolated to
  60 dB; returns 0.0 when the span is not reached.
- `[[nodiscard]] inline double spectralCentroidHz(std::span<const float> x, double sr)` — Hann
  window, magnitude-weighted bin centroid (use the repo `FFT` via
  `<krate/dsp/primitives/fft.h>` or a local DFT; either is acceptable, the helper is test-only).
- `[[nodiscard]] inline double pearson(std::span<const double> a, std::span<const double> b)`.
- `struct NoiseState { … };` and
  `inline void fillBandLimitedNoise(std::span<float> dst, double sr, std::uint32_t seed,
  std::uint64_t startSample, NoiseState& state)` — **P-3's G-2 as a generator, never a looped
  buffer**: white noise from `Xorshift32` (`core/random.h:41`) seeded once from `seed`, through a
  2nd-order 80 Hz high-pass and a 2nd-order 11 kHz low-pass whose **state lives in `NoiseState`**, so
  successive fills continue one stream with no seam; `startSample` is used only to (re)initialise at
  `startSample == 0`. A 30-minute render must carry no periodicity (SC-014, SC-005 depend on this).

These band / T60 / noise helpers are **re-derived**, not moved: the equivalents in
`dsp/tests/unit/effects/aether_reverb_test.cpp` are file-local and that TU is frozen by SC-012 (b).

**Verify.** `--target dsp_effects_tests`, zero warnings, then
`dsp_effects_tests.exe "FDNReverb: echo density*" 2>&1 | tail -5` — green, `ned` unchanged in
substance.

---

# GROUP 2 — The `AetherReverb` extension

## T003 — Append-only damper-offset hook (FR-040 … FR-048) and its dedicated TU

Only change to a shipped shared header in this phase. Roadmap lines 560–562 govern it; SC-012 gates
it. **No file under `dsp/tests/unit/effects/aether_reverb_*` or `dsp/tests/unit/systems/seraphis_*`
may be edited by this task or any other in this phase.**

**Files edited**

- `dsp/include/krate/dsp/effects/aether_reverb.h` (edits E-1 … E-6 below)
- `dsp/tests/unit/effects/aether_reverb_damper_offset_test.cpp` (delete the T001 scaffold case)

**Test first** — three `TEST_CASE`s in `aether_reverb_damper_offset_test.cpp`, tags
`[effects][cavern]`:

1. `TEST_CASE("AetherReverb_DamperOffsetInert", "[effects][cavern]")` — **SC-012 (a), the clause with
   teeth is a recomputation, not a two-instance render.** Sweep `setSize`, `setDecaySeconds`,
   `setDensity`, `setDamping` over **≥ 5 values each** × `numChannels ∈ {8, 16}` at 48 kHz, offsets
   never set. For every line `i`, recompute the shipped law from public data exactly as
   `aether_reverb.h:3140-3148` does — `m = getEffectiveDelayLengthSamples(i)`,
   `T60_dc = decaySeconds`, `gDC = pow(10, -3*m/(T60_dc*sr))`,
   `T60_nyq = T60_dc * pow(0.05f, damping)`, `gNyq = pow(10, -3*m/(T60_nyq*sr))`,
   `ratio = clamp(gNyq/gDC, 0, 1)`, `c = clamp(2*ratio/(1+ratio), 0.001f, 1.0f)` — and
   `REQUIRE` `getEffectiveDampingCoefficient(i)` within a **relative `1e-5`** of `c`
   (amended 2026-09-17; `==` is unattainable — see SC-012 (a)).
   Then a **labelled smoke check**: a bare engine and one handed an all-zero 8-vector produce
   bit-identical output over a 10 s render (two runs of the same build; nothing is committed, so
   `tools/lint-float-bit-goldens.js` and the no-bit-exact-goldens rule are not engaged).
2. `TEST_CASE("AetherReverb_DamperOffsetHostileInput", "[effects][cavern]")` — FR-040, FR-044,
   FR-045. Publish `{NaN, +Inf, −Inf, 1e30f, −1e30f, 0.0f, 7.5f, −7.5f}` with `count = 32`
   (> `numChannels_`): every `getEffectiveDampingCoefficient(i)` is **finite** (bit-pattern test, not
   `std::isnan`) and within `[0.001f, 1.0f]`; render 10 s of band-limited noise under that vector and
   require `max|out| < 10.0f` and every sample finite. Then publish `nullptr` (any count), and a
   valid pointer with `count == 0`: after the next control chunk every
   `getEffectiveDampingCoefficient(i)` equals `dampCoeff_[i]` — assert it via the T003-case-1
   recomputation, by **exact float equality**.
   Build NaN/Inf through bit patterns via `volatile`, never `std::numeric_limits<float>::quiet_NaN()`
   folded under `-ffast-math`.
3. `TEST_CASE("AetherReverb_DamperOffsetFrozenInit", "[effects][cavern]")` — FR-047's invariant.
   `prepare()`, then `setFreeze(true)` **immediately** (before any `processStereoBlock`), then render
   1 s of noise: every `getEffectiveDampingCoefficient(i)` equals the recomputed shipped coefficient
   by exact float equality, and the output is finite and non-zero.

All three fail to compile before the implementation (no `setDamperOffsetsOctaves`, no
`getEffectiveDampingCoefficient`).

**Implement — six append-only edits, nothing else in the file moves (FR-042):**

- **E-1.** Public constant beside the other public constants (~`:1398`, verified this session that
  `kModExcursionFraction` lives there):
  ```cpp
  /// Vorago Phase 9 (FR-040): hostile-input ceiling on a published damper offset, in OCTAVES.
  /// >= CavernVerb::kMaxDamperOctaves, so a legitimate excursion is never clipped here.
  static constexpr float kMaxDamperOffsetOctaves = 4.0f;
  ```
- **E-2.** Public `void setDamperOffsetsOctaves(const float* offsets, std::size_t count) noexcept`
  after `setSeed` (verified at `:2361`): null pointer or `count == 0` clears the whole array to zero;
  otherwise copy `min(count, numChannels_)` values, replacing non-finite with `0.0f` through the
  existing `isFinite` helper (`:2937`, `ITERUM_NOINLINE`) and clamping each to
  `±kMaxDamperOffsetOctaves`; entries `[n, kMaxChannels)` are zeroed. RT-safe, allocation-free, plain
  member (no virtual, no new interface).
- **E-3.** Public
  `[[nodiscard]] float getEffectiveDampingCoefficient(std::size_t channel) const noexcept` beside
  `getEffectiveDelayLengthSamples` (verified at `:2506`), returning
  `(channel < kMaxChannels) ? effectiveDampCoeff_[channel] : 0.0f` — the `:2506-2509` out-of-range
  idiom.
- **E-4.** Two private arrays beside `dampCoeff_` (verified at `:4489`):
  `alignas(32) float damperOffset_[kMaxChannels]{};` and
  `alignas(32) float effectiveDampCoeff_[kMaxChannels]{};`, plus private
  `void applyDamperOffsets() noexcept`:
  ```cpp
  for (std::size_t i = 0; i < numChannels_; ++i) {
      const float off = damperOffset_[i];
      if (off == 0.0f) { effectiveDampCoeff_[i] = dampCoeff_[i]; continue; }  // FR-044: ASSIGNMENT
      const float c  = std::clamp(dampCoeff_[i], 0.001f, 1.0f - 1.0e-6f);     // FR-048 step 1
      const float p  = std::exp2(-off);                                        // step 2
      const float cp = 1.0f - std::pow(1.0f - c, p);                           // step 3, closed form
      effectiveDampCoeff_[i] = std::clamp(cp, 0.001f, 1.0f);                   // step 4 / FR-045
  }
  for (std::size_t i = numChannels_; i < kMaxChannels; ++i) { effectiveDampCoeff_[i] = dampCoeff_[i]; }
  ```
  The closed form is FR-048 steps 1–3 with the logarithm and `sr` cancelled exactly
  (`c = 1 − e^{−u}`, `u' = u·2^{−off}` ⇒ `c' = 1 − (1−c)^{2^{−off}}`): two transcendentals per moving
  line, never three, and no division by `sr`. The `1 − 1e-6` upper clamp is **load-bearing**: without
  it `c = 1` gives `1 − 0^p = 1` for every offset and a line at "no damping" would ignore the dampers
  completely. Sign check (SC-005 asserts it): `off = +0.5` ⇒ `p = 0.7071` ⇒ `(1−c)^p > (1−c)` ⇒
  `c' < c` ⇒ **darker**.
- **E-5.** Two call-site edits.
  (1) `refreshControlState()` (verified at `:3610-3618`) gains `applyDamperOffsets();` as the third
  line **inside the existing `if (!freezeTarget_)` branch**, after `updateDecayAndDamping()` — FR-046
  inherits the latch exactly, for the reason the header already states at `:3600-3606`. It is
  **inside** the branch but **not** behind `updateDecayAndDamping()`'s epsilon gate (`:3128-3137`),
  because the offsets move on every chunk while `dampCoeff_` does not — which is why a moving offset
  costs **zero** additional `powf` (FR-043).
  (2) `renderSlice` step 3 (verified at `:4282`): `const float c = dampCoeff_[i];` becomes
  `const float c = effectiveDampCoeff_[i];`. One token; nothing else in that loop changes.
- **E-6.** In `reset()`, **before** the existing `lastJot* = -1` / `refreshControlState()` block
  (`:2094-2099`):
  ```cpp
  for (std::size_t i = 0; i < kMaxChannels; ++i) {
      damperOffset_[i] = 0.0f;
      effectiveDampCoeff_[i] = dampCoeff_[i];   // FR-044's plain assignment; FR-047
  }
  ```
  This is **defensive redundancy and documentation, not a bug fix** — `reset()` sets
  `freezeTarget_ = false` at `:1979` before reaching `refreshControlState()`, so the branch always
  runs at least once. It removes the dependence of the invariant on a control-flow ordering 120 lines
  away. Say exactly that in the comment; do not repeat the struck claim.

Every edit carries a comment naming `vorago-phase9-cavern-space` and its FR.

**Verify.**
```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_effects_tests
build/windows-x64-release/bin/Release/dsp_effects_tests.exe "AetherReverb_DamperOffset*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_effects_tests.exe "~[long]~[.perf]" 2>&1 | tail -5
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe 2>&1 | tail -5
```
All green, zero warnings. `dsp_systems_tests` (all seven `seraphis_*` TUs) must pass **unedited** —
that is SC-012 (b)'s first half and it is checked here, not deferred to G14.

---

# GROUP 3 — `CavernVerb` skeleton: constants, table, lifecycle, controls

## T004 — Constants, the ER tap table, `PrepareConfig`, lifecycle, the seventeen setters, static accessors

**Files edited**

- `dsp/include/krate/dsp/effects/cavern_verb.h`
- `dsp/tests/unit/effects/cavern_verb_test.cpp` (delete the T001 scaffold case)

**Test first** — in `cavern_verb_test.cpp`, tags `[effects][cavern]`:

`TEST_CASE("CavernVerb_EarlyReflectionGeometry", "[effects][cavern]")` — create it now with **only**
these sections (T005 appends the rendering sections into the same case):

- `SECTION("(d) incommensurability, FR-022 / SC-006 (d)")` — evaluate, over the 12 delays in
  **milliseconds at the default ER size**, `min over i≠j, p,q ∈ 1..K of |p·d_i − q·d_j| / min(d_i,d_j)`
  for `K ∈ {2, 4, 6}`. `REQUIRE(measuredMin >= CavernVerb::kEarlyIncommensurabilityTol)` (= 0.05f).
  `WARN` the measured minimum — **expected 0.05411** — and additionally report the order-8 value
  (expected 0.04519) as a documented non-gate (plan B-1). "Not an integer multiple" is **not** the
  property and must not be what is asserted.
- `SECTION("(b) first arrival floor, FR-021 / SC-006 (b)")` — a prepared instance at 48 kHz, default
  ER size: `getEarlyTapDelaySamples(0)` equals `60.0f ms × 48` samples within ±0.01 ms; the table is
  **strictly ascending** in `i`; `getEarlyTapCount() == 12`.
- `SECTION("gain law and sum, FR-020 / FR-028")` — `getEarlyTapGain(i)` equals
  `0.5f * exp(-0.01f * d_i(ms))` within 1e-6 for every `i`; the sum of `|g_i|` equals
  `CavernVerb::kEarlyGainSum` within 1e-5 and `kEarlyGainSum <= 2.0f`; expected sum **1.865762**;
  every `g_i > 0` (no negative-polarity taps). Out-of-range: `getEarlyTapGain(12) == 0.0f`,
  `getEarlyTapDelaySamples(99) == 0.0f`.
- `SECTION("(c) size biased huge, SC-007 (c)")` — `Σ_i getEffectiveDelayLengthSamples(i)` on a
  `CavernVerb` at `setSize(0.0f)` is **≥ 4.4×** the same sum on a bare `AetherReverb` at
  `setSize(0.0f)`. The bare instance is seeded `deriveStreamSeed(1u, CavernVerb::kCavernReverbSalt)`
  — the exact seed `CavernVerb` hands its owned engine — both are sampled at the same absolute sample
  index after identical render lengths, and breath is zeroed on both sides (`setBreath(0)` /
  `setSizeBreathDepth(0)` + `setDimensionalityTideDepth(0)`), so the two jitter realisations are
  identical and cancel. `WARN` the measured ratio (nominal **4.594**) and the residual spread.
  `setModDepth` must **not** be added to `CavernVerb`'s surface for this measurement.

`TEST_CASE("CavernVerb_SampleRates", "[effects][cavern]")` — create it now with **only**:

- `SECTION("allocation formula and latency, FR-075 / SC-015")` — `prepare()` at 44 100 / 48 000 /
  96 000 / 192 000 Hz. `getLatencySamples()` equals the configured `diffusionFftSize` when
  `spectralDiffusionEnabled` is true and exactly 0 when false. `getAllocatedBytes()` is within
  `[1×, 2×]` of `4 * (nextPowerOf2(sr*maxEarlySeconds + 5) + 4*nextPowerOf2(alignSamples + 5))`
  bytes — assert the **formula**, never linearity in rate (the buffers are power-of-two quantised and
  the alignment lines are sized in samples of engine latency, which is rate-independent: 48 kHz/FFT
  1024/0.30 s = 24 576 samples, 192 kHz/FFT 4096/0.60 s = 163 840, a 6.67× step for a 4× rate change).
- `SECTION("ER tap times are rate-invariant, SC-015")` — `getEarlyTapDelaySamples(i) / sr` agrees
  across all four rates within **±0.05 ms** (a time-unit tolerance: "±1 sample" is unit-ambiguous
  across four rates, and 0.05 ms is ~2.2 samples at 44.1 kHz and ~9.6 at 192 kHz, well inside the
  4.1683 ms minimum tap spacing).

**Implement** in `cavern_verb.h` (plan S2.2 – S2.5, S3.1, S3.2, S3.4, S4):

- **Public constants** exactly as plan S2.2 pins them, including `kControlChunkSamples =
  AetherReverb::kControlChunkSamples`, `kCavernSizeFloor = 0.55f`, `kCavernDampingFloor = 0.50f`,
  `kCavernDecayMinSeconds = 0.5f`, `kCavernDecayMaxSeconds = 60.0f`, `kMaxChannels = 16` (the three
  mirrors of **private** `AetherReverb` constants — each carries a comment citing `:2735-2736` /
  `:2725`, and SC-016 pins them at runtime because no `static_assert` can reach a private member),
  `kEarlyTapCount = 12`,
  `kEarlyTapSeries[12] = {1u,53u,199u,277u,547u,709u,929u,1049u,1381u,1583u,1721u,1997u}`,
  `kEarlyFirstArrivalFloorMs = 60.0f`, `kDefaultEarlySizeMs = 220.0f`, `kEarlySizeMinMs = 80.0f`,
  `kEarlySizeMaxMs = 600.0f`, `kEarlyGainG0 = 0.5f`, `kEarlyGainAlphaPerMs = 0.01f`,
  `kEarlyGainSum = 1.865762f`, `kIncommensurabilityOrder = 6`, `kEarlyIncommensurabilityTol = 0.05f`,
  `kEarlyAbsorptionFcMaxHz = 18000.0f`, `kEarlyAbsorptionFcMinHz = 1200.0f`,
  `kEarlyAbsorptionNyquistFraction = 0.45f`, `kEarlyAbsorptionSpanFraction = 0.40f`,
  `kMaxDamperOctaves = 1.5f`, `kDefaultDamperDepth = 0.35f`, `kDefaultDamperRate = 0.15f`,
  `kCavernReverbSalt = 64`, `kCavernDamperSaltBase = 96`, the FR-066 default table
  (`kDefaultSize 0.50`, `kDefaultDarkness 0.80`, `kDefaultDecaySeconds 20.0`, `kDefaultDensity 0.75`,
  `kDefaultDimensionality 0.50`, `kDefaultBreath 0.50`, `kDefaultFog 0.30`, `kDefaultEarlyLevel 0.80`,
  `kDefaultEarlyAbsorption 0.60`, `kDefaultEarlySend 0.70`, `kDefaultWidth 1.00`, `kDefaultMix 1.00`,
  `kDefaultMaxEarlySeconds 0.30`), and the smoothing times
  (`kEarlySizeSmoothingMs 300`, `kAbsorptionSmoothingMs 100`, `kGateRampMs 50`).
- **FR-084 `static_assert`s — public facts only** (plan S2.2): `kControlChunkSamples` matches;
  `AetherReverb::kSizeScaleMin == 0.25f && kSizeScaleMax == 4.0f`; `kMinSampleRate == 8000.0f &&
  kMaxSampleRate == 192000.0f`; `AetherReverb::kDriftSaltBase == 16`;
  `kCavernDamperSaltBase >= AetherReverb::kDriftSaltBase + (kMaxChannels/2u) + 8u`;
  `AetherReverb::kRefDelays8[0] == 967u`; `kEarlyGainSum <= 2.0f`;
  `kMaxDamperOctaves <= AetherReverb::kMaxDamperOffsetOctaves`; `kCavernSizeFloor >= 0.55f`.
  Plus the `detail`-namespace table asserts: strictly ascending, pairwise coprime, and
  `cavernTableIncommensurabilityMin() >= kEarlyIncommensurabilityTol` — all three are exact
  integer/ratio arithmetic and are genuinely `constexpr`. The **gain sum** needs `exp`, which is not
  `constexpr`: either write a range-reduced `constexpr expApprox` whose agreement with `std::exp` to
  1e-5 is asserted in the geometry case, **or** carry the sum as the `REQUIRE` this task already
  places there (permitted by FR-022's own "otherwise by a table-driven test" clause). Record which
  branch was taken in a header comment — it is a choice, not a silent drop.
- **`PrepareConfig`** exactly as plan S2.3: `numChannels = 8`, `maxBlockSamples = 2048`,
  `maxEarlySeconds = 0.30f`, `maxDelaySeconds = 0.50f`, `spectralDiffusionEnabled = true`,
  `diffusionFftSize = 1024`, `seed = 1`. Every field **clamped in place, never rejected** (FR-003);
  designated-initialiser friendly with **no narrowing** (each default literal has its member's type).
- **`prepare()`** in plan S3.1's numbered order. Load-bearing points:
  `maxEarlySeconds_` is clamped to `[kEarlySizeMinMs * 0.001f, 0.60f]` — the floor is **0.08 s, not
  0.05 s**, because `setEarlySizeMs` clamps into `[80, min(600, maxEarlySeconds_*1000)]` and a
  prepared 0.05 s would make `hi < lo`, which is UB by `std::clamp`'s precondition and a hard abort
  under MSVC's debug iterator checks; the owned engine is prepared with
  `shimmerEnabled = false` and `bloomEnabled = false` (**not constructed**, FR-010), seed
  `deriveStreamSeed(config.seed, kCavernReverbSalt)`, then `setMix(1.0f)` and `setPreDelayMs(0.0f)`;
  `maxBlockSamples` is forwarded **unchanged**; `erLine_.prepare(sampleRate_, maxEarlySeconds_ +
  4.0f/sr)` (four samples of headroom, because `readLinear` takes `index1 = min(index0+1,
  maxDelaySamples_)`); **all four alignment lines are prepared unconditionally**, each at
  `(alignSamples_ + 4) / sr` — there is **no** `alignSamples_ == 0` bypass anywhere in this design,
  because an unprepared `DelayLine` is an out-of-bounds heap write on the audio thread for every
  sample whenever `spectralDiffusionEnabled == false` (a configuration SC-013 renders); the tap table
  is built by the affine law `delayMs = 60 + 160*(n_i − n_0)/(n_11 − n_0)`,
  `gain = kEarlyGainG0 * exp(-kEarlyGainAlphaPerMs * delayMs)`, `rightSide = ((i & 1u) != 0u)`;
  dampers are prepared for **all** `kMaxChannels` slots, not only `numChannels_`; then `prepared_ =
  true`, re-apply every shadowed control, `reseed()`, `reset()`.
- **`reset()` / `silence()`** per plan S3.2. `reset()` re-applies the **complete** shadow set
  including `engine_.setFreeze(ctlFreeze_)` — `AetherReverb::reset()` unconditionally clears
  `freezeTarget_` (`:1979`) and freeze is not one of the controls its doc comment promises to
  preserve, so without the re-issue a frozen instance silently thaws while `ctlFreeze_` still reads
  true. `silence()` forwards to `engine_.silence()` and clears **only** the ER line and the 12 tap
  one-pole states: the four alignment lines are deliberately **not** cleared (they carry input
  history; clearing them punches an `fftSize`-long hole ending in a full-amplitude step — the
  reasoning at `aether_reverb.h:3625-3639`, copied into the header so it cannot be "tidied away").
- **`reseed()` / `setSeed`** per plan S3.4: `engine_.setSeed(deriveStreamSeed(seed_,
  kCavernReverbSalt))`, and for every `i < kMaxChannels`
  `damper_[i].setSeed(deriveStreamSeed(seed_, kCavernDamperSaltBase + i))` followed by
  `damper_[i].reset()` (verified: `BrownianDrift::setSeed` stores and re-seeds the RNG,
  `brownian_drift.h:145-148`; `reset()` → `initState()` is what rewinds the walk, `:133`, `:243-249`).
  The ER stage uses **no** RNG — say so in the doxygen so a later reader does not add one.
- **The seventeen setters** (FR-061 — no eighteenth, and none of `setPreDelayMs`, `setModDepth`,
  `setModSmoothness`, any shimmer/bloom control or `bloomNoteOn`/`bloomNoteOff`), each in the house
  form `clamp(isFinite(x) ? x : default, lo, hi)` → shadow copy → forward or smoother target,
  `noexcept`, accepted before `prepare()`. Mappings per plan S4:
  `setSize` → `engine_.setSize(kCavernSizeFloor + v*(1 − kCavernSizeFloor))`;
  `setDarkness` → `engine_.setDamping(kCavernDampingFloor + v*(1 − kCavernDampingFloor))`;
  `setDecaySeconds` → clamp to `[kCavernDecayMinSeconds, kCavernDecayMaxSeconds]`;
  `setDensity`, `setDimensionality`, `setWidth`, `setFreeze` forward directly;
  `setBreath` → **both** `setSizeBreathDepth(v)` **and** `setDimensionalityTideDepth(v)`;
  `setFog` → `setSpectralDiffusion(v)`. Smoothers snap rather than ramp while
  `prepared_ && !anySamplesProcessed_` (the `:2950-2958` rule).
- **Private state** exactly as plan S2.5, including the seven `kControlChunkSamples`-sized scratch
  arrays (1 792 B of member storage; never sized by `maxBlockSamples`), and `getAllocatedBytes()`
  reporting `CavernVerb`'s own buffers only.
- **Forwarded accessors** (FR-008): `isPrepared`, `isFrozen`, `isShimmerActive`,
  `getEffectiveDelayLengthSamples`, `getModalDensityPerHz`, `getMaxSizeScale`, `getStateEnergy`,
  `getNonFiniteRecoveryCount`, `isRecovering`, `getLatencySamples`. Out-of-range indices return
  `0.0f` everywhere.
- `processStereoBlock` may remain a stub that fills silence in this task; T005 implements it.

**Verify.** `--target dsp_effects_tests`, zero warnings, then
`dsp_effects_tests.exe "CavernVerb_EarlyReflectionGeometry" "CavernVerb_SampleRates" 2>&1 | tail -5`.

---

# GROUP 4 — The process loop and the ER audio path

## T005 — Control grid, ER taps, alignment, mix, latency (FR-005, FR-007, FR-019 – FR-024, FR-026, FR-062, FR-063)

**Files edited**

- `dsp/include/krate/dsp/effects/cavern_verb.h`
- `dsp/tests/unit/effects/cavern_verb_test.cpp`

**Test first** — append to `cavern_verb_test.cpp`:

Fixtures first (plan S10.3), used by every later task:
- `makeDefaultCavern(sr, cfg)` — **P-1**: 48 kHz, `numChannels = 8`, `maxBlockSamples = 512`, seed 1,
  and **every FR-066 default applied explicitly** so a future default change breaks the fixture
  loudly instead of drifting a criterion.
- `makeLongEarlyCavern(sr)` — **P-1b**: P-1 except `maxEarlySeconds = 0.60`, so the full FR-023 range
  `[80, 600]` ms is reachable.
- `renderRagged(engine, in, out, {37, 111, 513})` — **P-2**'s deliberately non-multiple-of-64 block
  partitions, cycled. 512 is *not* used for this purpose (it is `8 × 64`, control-grid aligned).
- `clickConfig48()` — **P-5** verbatim, in designated-initialiser form, with the sample rate
  **corrected to 48 kHz** (the struct default is 44 100, verified at
  `tests/test_helpers/artifact_detection.h:38`, which would silently mis-scale every reported
  detection time): `ClickDetectorConfig{.sampleRate = 48000.0f, .frameSize = 512, .hopSize = 256,
  .detectionThreshold = 5.0f, .energyThresholdDb = -60.0f, .mergeGap = 5}`, plus the inherited
  calibration ladder — a no-transition reference render must give zero false positives, else
  `detectionThreshold` rises to the smallest value that does, **capped at 8.0**, and the calibrated
  config must still report ≥ 1 detection on a control render carrying a single-sample step of
  amplitude 0.1. `WARN` the threshold, the false-positive count and the control count. **The
  zero-detection requirement is never relaxed.**

`TEST_CASE("CavernVerb_ProcessContract", "[effects][cavern]")` — FR-005, the three cases
`spec.md:1243-1248` enumerates:
- output buffers poisoned with a sentinel (e.g. `-12345.0f`) are left **untouched** when any one of
  the four pointers is null;
- a `processStereoBlock(inL, inR, outL, outR, 0)` call inserted between two 64-sample calls leaves
  the render **bit-identical** to the same render without it (no state advances — a regression that
  advanced `sampleCounter_` on a zero-length call would also break FR-007's grid alignment and is
  caught by nothing else);
- an **unprepared** instance writes silence over the poisoned buffers;
- a block **larger than `maxBlockSamples`** (e.g. 5000 samples at P-1's 512) is processed correctly
  and its output matches the same audio rendered in `{37, 111, 513}` partitions within
  `kSampleTolerance`.

Append to `TEST_CASE("CavernVerb_EarlyReflectionGeometry", …)` (created in T004) — all render at
G-1 (unit impulse), `setEarlyLevel(1.0)`, `setMix(1.0)`, **`setEarlyAbsorption(0.60)` pinned
explicitly** (the Nyquist guard makes the cutoff set rate-dependent, so an unpinned absorption would
let the 44.1 kHz and 96 kHz arms run under different laws), and **`setEarlySend(0.0)`** for (a), (c),
(f), (g), (h) — a reachable product state in which the owned engine receives digital silence and the
render carries the ER pattern **alone**:
- `SECTION("(a) arrival times")` — arrivals located by local-maximum picking on `|out|`; each within
  **±1 sample** of `getLatencySamples() + getEarlyTapDelaySamples(i)`, over
  `setEarlySizeMs ∈ {80, 220, 600}` × `sr ∈ {44100, 48000, 96000}`, with the **600 ms arm rendered on
  fixture P-1b**. Paired with it, on **P-1** (`maxEarlySeconds = 0.30`): after
  `setEarlySizeMs(600.0f)`, `REQUIRE(getEarlyTapDelaySamples(11) ≈ 300 ms × sr/1000)` within ±1
  sample — the buffer-derived clamp, asserted rather than assumed.
- `SECTION("(c) sparsity")` — NED (T002's helper) over `[first arrival, last arrival]` is **< 0.5**:
  the negative control that distinguishes a reflection pattern from a diffuse field.
- `SECTION("(e) the send is not vacuous")` — with `setEarlySend(1.0)`, the render's energy after
  `t = 2 × (last tap delay)` exceeds the `setEarlySend(0.0)` render's by **≥ 20 dB**.
- `SECTION("(f) mono-sum invariance")` — at `setEarlySend(0)`, an impulse on **L only** and an
  impulse on **R only** produce ER outputs identical within `kSampleTolerance` (5.0e-4f).
- `SECTION("(g) side rule")` — locate arrivals **per channel**: tap `i` appears on L for even `i` and
  on R for odd `i` (picking from `|out|` cannot distinguish the bus, so this section must analyse
  `outL` and `outR` separately). `WARN` the measured `Σg_L/Σg_R` — expected **+0.80 dB**
  (`Σg_L = 0.97599`, `Σg_R = 0.88977`), a consequence of the pinned side rule and gain law, not a
  defect; no normalisation is applied.
- `SECTION("(h) width independence")` — repeat (g) at `setWidth(0)` and `setWidth(1)`: the ER
  arrivals are unchanged, since `setWidth` governs the late field only.

`TEST_CASE("CavernVerb_Latency", "[effects][cavern]")` — SC-013, **two renders**:
- render (i), dry path: an impulse at `setMix(0.0)` emerges delayed by **exactly**
  `getLatencySamples()` samples;
- render (ii), ER path: an impulse at `setMix(1.0)`, `setEarlyLevel(1.0)`, `setEarlySend(0.0)`:
  arrival `i` lands at `getLatencySamples() + getEarlyTapDelaySamples(i)` within ±1 sample.
Both at `spectralDiffusionEnabled ∈ {true, false}` × `diffusionFftSize ∈ {256, 1024, 4096}`.

**Implement** plan S3.3, S5.1, S5.2, S5.4:

- `processStereoBlock`: null-pointer guard → return without writing; `n == 0` → return, **advancing
  nothing**; not prepared → fill both outputs with silence. Then the slicing loop:
  `phase = sampleCounter_ % kControlChunkSamples`; `runControlStep()` **only** at `phase == 0`;
  `slice = min(n − done, kControlChunkSamples − phase)`; `renderSlice(...)`; `sampleCounter_ +=
  slice`. The counter is **absolute**, started at `prepare()`/`reset()` and never reset by a caller's
  block boundary — that is what makes render output invariant to host partitioning (FR-007, P-2,
  SC-011). A trailing partial sub-block runs **no** control step.
- `runControlStep()` order is normative (mirrors `aether_reverb.h:3871-3913`): (1) dampers advance
  unconditionally by a **full** `kControlChunkSamples` — T008 fills this in; (2)
  `absorptionSm_.advanceSamples(kControlChunkSamples)` and **`erSizeSm_` is deliberately NOT advanced
  here**; (3) publish the damper offsets — T008; (4) `refreshControlState()`, which is the **only**
  place `erSizeSm_` is advanced, with `prevMs`/`nextMs` captured around it; (5) inside `renderSlice`,
  one `engine_.processStereoBlock(...)` call per sub-block. Advancing `erSizeSm_` twice would halve
  the effective 300 ms smoothing time **and** put a step at every chunk boundary (R-16).
- ER size scaling (S5.2): `setEarlySizeMs` clamps to `[kEarlySizeMinMs, hi]` with
  `hi = max(kEarlySizeMinMs, min(kEarlySizeMaxMs, maxEarlySeconds_ * 1000.0f))` — the outer `max` so
  the pair can never invert — then targets `erSizeSm_`. Per control chunk, compute
  `chunkTapDelayStart_[i]` from `prevMs` and `chunkTapDelayEnd_[i]` from `nextMs`; **inside the slice
  the tap delay is linearly interpolated per sample** with `t = (phase + k) / 64.0f` derived from the
  absolute phase. `DelayLine::makeLinearTap` is **NOT** used: it pins the delay for a whole chunk,
  and with `kEarlySizeSmoothingMs = 300` (a time to 99 %) an 80 → 600 ms jump moves the last tap by
  **11.4 ms per chunk** — a 548-sample staircase every 1.33 ms at 48 kHz, exactly the discontinuity
  FR-023 forbids (`aether_reverb.h:4270-4281` is the measured precedent). `getEarlyTapDelaySamples(i)`
  reports `chunkTapDelayStart_[i]`.
- `renderSlice` (S5.4): sanitise the input **once, before any consumer** —
  `xl = isFinite(inL[k]) ? inL[k] : 0.0f` (a replacement, **never** a counter increment,
  `:4163-4172`) — and feed **both** the ER line and the dry scratch from `xl`/`xr`. Write
  `0.5f*(xl+xr)` into the **single mono** `erLine_` after flushing magnitudes below `1e-20f`. Sum the
  12 taps into `erL`/`erR` by the side rule and into `erMono`; `sendScratch_[k] = erMono *
  earlySend_.process()`; `erScratchL_/R_` hold the **ungained** ER (level is applied after the
  alignment). Excite the owned engine with **the same mono send buffer on both inputs**, once per
  slice: `engine_.processStereoBlock(sendScratch_, sendScratch_, engineOutL_, engineOutR_, slice)` —
  post-absorption only, no direct or unabsorbed path reaches it by any other route (FR-026 i–iii).
  Then per sample, with all four alignment lines written and read every sample, apply the equal-power
  dry/wet and the ER level **after** the alignment. Each `ShapedRamp` is advanced **exactly once per
  output sample**, hoisted above both channel assignments — a second `process()` call for R would
  halve `kGateRampMs` and break FR-065 (iii). (T007 supplies the `ShapedRamp` and the zero-gate
  branches; in this task `earlyLevel_`, `earlySend_` and `mix_` may be plain smoothed scalars, but the
  once-per-sample rule already applies.)
- The deliberate onset gap is a feature, not a bug: the first ER arrival at 60 ms plus the shortest
  line (`kRefDelays8[0] = 967` samples = 20.15 ms at 48 kHz) at `S ≥ 1.1487` puts the late field
  ≈ 83 ms after the input.

**Verify.** `--target dsp_effects_tests`, zero warnings, then
`dsp_effects_tests.exe "CavernVerb_ProcessContract" "CavernVerb_EarlyReflectionGeometry" "CavernVerb_Latency" 2>&1 | tail -5`.

---

# GROUP 5 — Stone absorption

## T006 — Per-tap absorption one-poles (FR-025, FR-027)

**Files edited**

- `dsp/include/krate/dsp/effects/cavern_verb.h`
- `dsp/tests/unit/effects/cavern_verb_test.cpp`

**Test first** — `TEST_CASE("CavernVerb_EarlyAbsorption", "[effects][cavern]")`:

- **(i) the cutoff law.** `getEarlyTapAbsorptionCutoffHz(i)` matches
  `fc_i = fcMax * pow(fcMin/fcMax, v*i/(n−1))` within **1 %** at `v ∈ {0, 0.5, 1}`, with
  `fcMax = min(kEarlyAbsorptionFcMaxHz, kEarlyAbsorptionNyquistFraction * sr)` and
  `fcMin = min(kEarlyAbsorptionFcMinHz, kEarlyAbsorptionSpanFraction * fcMax)` computed in the test
  from the **same public constants** the header uses. At `v = 0` the accessor returns **exactly
  `fcMax` for every tap** (the FR-025 bypass). Out-of-range index returns `0.0f`.
- **(ii) one state per tap, gated behaviourally.** At `setEarlySend(0)`, measure the spectral centroid
  (T002's `spectralCentroidHz`) of a short window around each arrival: at `v = 1` it is
  **non-increasing in tap index**, and tap 11 is **at least 6 dB duller** than tap 0 (compare the
  HF-band energy ratio above 4 kHz); at `v = 0` the per-tap centroids are **flat within ±5 %**.
  A single shared filter passes (i) but fails (ii) — under one shared state the tap ordering would
  not matter. This is how "one state per tap, not one filter shared across taps" is actually gated.
- **(iii) bypass entry/exit is click-free.** A `setEarlyAbsorption` transition `0.6 → 0 → 0.6` during
  a continuous G-2 render, through P-5's detector: **zero** detections.

**Implement** plan S5.3: `n = kEarlyTapCount` **independent** one-pole states, one per tap, cutoff
geometric in tap index with the Nyquist guard above; coefficient `a_i = 1 − exp(−2π·fc_i/sr)`, filter
`state += a*(x − state)`. Coefficients are evaluated **once per control chunk and only when `v` moved
by more than 1e-7** (the `kJotRecomputeEpsilon` idiom, `aether_reverb.h:2785`) — 12 `pow` + 12 `exp`
per chunk at worst, none at all while settled. **At `v == 0` the filter is an exact bypass by
assignment**: `absorptionBypass_ = true`, the one-pole is not evaluated, no coefficient is computed,
and the accessor reports `fcMax`. The bypass is a pass-through assignment and **not** `a = 1` (the
FR-024/FR-044 rule: a path that may carry a stale value must be replaced, never scaled by a
coefficient that happens to be identity). On **leaving** bypass, zero every `taps_[i].state` first —
it holds history the filter was not running on. Flush every tap state below `1e-20f` once per control
chunk: the per-tap one-pole **is** a recirculating IIR, and the per-chunk flush is what bounds the
denormal exposure to at most 64 samples. The one-pole is non-expansive for every `a ∈ (0, 1]` (the
same argument as `aether_reverb.h:3085-3086`), so FR-028's `Σ|g_i|` peak bound survives absorption.

**Verify.** `--target dsp_effects_tests`, zero warnings,
`dsp_effects_tests.exe "CavernVerb_EarlyAbsorption" "CavernVerb_EarlyReflectionGeometry" 2>&1 | tail -5`.

---

# GROUP 6 — Shaped ramps, dormancy, equal-power mix

## T007 — `ShapedRamp`, the dormancy rules, and the mix law (FR-024, FR-063, FR-065)

**Files edited**

- `dsp/include/krate/dsp/effects/cavern_verb.h`
- `dsp/tests/unit/effects/cavern_verb_test.cpp`

**Test first**

`TEST_CASE("CavernVerb_MixLaw", "[effects][cavern]")` — FR-063. Uncorrelated dry and wet (G-2 in, a
long-decay configuration so the wet bus is uncorrelated with the input), rendered at
`setMix ∈ {0, 0.25, 0.5, 0.75, 1}`: **total output power stays within ±0.5 dB across the sweep**. A
linear crossfade dips ≈ 3 dB at `mix = 0.5` and fails. This case is also lever **L-3's acceptance
gate** (plan S12.3): an L-3 control-grid lerp whose error exceeds the claimed 0.1 % shows up here.

`TEST_CASE("CavernVerb_DamperMotionSmoothness", "[effects][cavern]")` — create the case now with
**only** these two sections (T008 appends (a), (b), (c) and the depth step):
- `SECTION("(d) dormancy re-entry")` — in one render, step `setEarlyLevel`, `setEarlySend` and
  `setMix` each `0 → 1` in a single call at pinned times **≥ 5 s apart**, and again `1 → 0`: P-5's
  detector reports **zero** detections across every transition. This is the clause that catches a
  skipped chain re-entered as a step rather than on a ≥ 50 ms shaped ramp.
- `SECTION("(d) ER size sweep")` — on fixture **P-1b** (`maxEarlySeconds = 0.60`), sweep
  `setEarlySizeMs` `80 → 600 → 80` at two pinned times ≥ 5 s apart during a continuous G-2 render:
  P-5's detector reports **zero** detections. **This is the clause that fails if `makeLinearTap` or
  any other per-chunk delay pinning is used** (T005/plan S5.2). Before this section existed, no test
  rendered a `setEarlySizeMs` change at all.

**Implement** plan S2.5's `ShapedRamp` and S5.5:

- `struct ShapedRamp { LinearRamp r; float from, to; … }` — `value = from + smoothstep(r) * (to −
  from)` over `kGateRampMs = 50.0f`: **exact at both endpoints** (so the control law is undistorted
  when settled) and **zero derivative at both** (the `aether_reverb.h:4237-4241` shaping, for the
  measured C0-corner failure at `:4270-4281`). A re-target to the **same** value is dropped, so a
  per-block setter cannot stall the ramp — the `feedback_ecology.h:2290-2296` failure the spec's Edge
  Cases cite. `settledAtZero()` is `to == 0 && r.isComplete()`.
- `earlyLevel_`, `earlySend_` and `mix_` become `ShapedRamp`s, advanced **exactly once per output
  sample**.
- Dormancy, ruled per slot (FR-065):
  - `erChainSkipped_` becomes true **only when both** `earlyLevel_` and `earlySend_` are settled at
    exactly `0.0f`. While skipped: `erLine_.write()` **still runs every sample** (the pattern stays
    charged); the 12 `readLinear` + one-pole + accumulate are skipped; `sendScratch_[k] = 0.0f` **by
    assignment**, so the owned engine receives literal digital silence — which is also the reachable
    product state SC-006 (a)/(c) render. `erChainSkipped_` is cleared **in the setter**, before the
    ramp starts moving, so the first ramped sample already has taps behind it.
  - `setMix == 0` **never skips anything**: the wet path (owned engine + ER) keeps running.
    Reproduce the justification at the branch in the header — the FDN is a 60-second-decay
    recirculating state a drone player holds, freezes and returns to; a `mix` automated to 0 and back
    is a *duck*, not a *stop*, and skipping the chain would drain the state.
  - At `mix` settled at zero the output is the aligned dry signal **by assignment**, not by an `x*0`
    product (FR-024/FR-063: a buffer that may hold a non-finite value must be replaced, because
    `NaN · 0` is `NaN`); likewise the ER contribution at `earlyLevel` settled at zero.
- Equal-power: `dryG = cos(mx * π/2)`, `wetG = sin(mx * π/2)` with `mx` the shaped mix value, both
  hoisted above the channel assignments.

**Verify.** `--target dsp_effects_tests`, zero warnings,
`dsp_effects_tests.exe "CavernVerb_MixLaw" "CavernVerb_DamperMotionSmoothness" 2>&1 | tail -5`.

---

# GROUP 7 — The damper bank

## T008 — `BrownianDrift` bank, offset publication, SC-003 (FR-030 … FR-037)

**Files edited**

- `dsp/include/krate/dsp/effects/cavern_verb.h`
- `dsp/tests/unit/effects/cavern_verb_test.cpp`

**Test first** — append to `TEST_CASE("CavernVerb_DamperMotionSmoothness", …)`, all at
`setDamperDepth(1.0)` and `setDamperRate(1.0)` (the fastest, worst case):

- `SECTION("(a) trajectory")` — 120 s of G-2 rendered in exact 64-sample steps, sampling
  `getDamperOffsetOctaves(i)` once per chunk. `REQUIRE(maxStep <= kMaxOffsetStepPerChunk)` with the
  bound written as the **parameterised expression, never a literal**:
  ```cpp
  // smoother.h:77-93 — smoothTimeMs is the time to 99 %, so coeff = exp(-5000/(ms*sr)) and the
  // per-chunk approach fraction is 1 - coeff^kControlChunkSamples.
  const float alpha = 1.0f - std::pow(
      calculateOnePolCoefficient(BrownianDrift::kDriftOutputSmoothMs, static_cast<float>(sr)),
      static_cast<float>(CavernVerb::kControlChunkSamples));
  const float kMaxOffsetStepPerChunk =
      alpha * (CavernVerb::kMaxDamperOctaves / BrownianDrift::kInternalStd
               + CavernVerb::kMaxDamperOctaves);
  ```
  At 48 kHz this is `alpha = 0.0434712`, bound **0.19562 octaves/chunk**. The spec's literal 0.02655
  is wrong on two independent factors (plan B-2) and a correct implementation fails it — do **not**
  write 0.02655. `WARN` the measured maximum.
- `SECTION("(b) coefficient")` — replay the same recorded offset record into a **bare**
  `AetherReverb` via `setDamperOffsetsOctaves` (this is exactly what FR-041 exists for; `CavernVerb`
  exposes no engine reference), reading `getEffectiveDampingCoefficient(i)` once per chunk. Bound:
  `0.25499f * kMaxOffsetStepPerChunk` = **0.04988** (FR-048's darkness-independent Lipschitz
  constant `ln2 · e^{−1}`; the spec's 0.00677 carries the same error). `WARN` the measured maximum.
- `SECTION("(c) audible")` — P-5's `ClickDetector` over the same 120 s render: **zero** detections.
- `SECTION("(d) depth step")` — in the T007 transition render, additionally step `setDamperDepth`
  `0.35 → 1.0` and back at two further pinned times ≥ 5 s apart: **zero** detections. This is a
  different mechanism from the dormancy ramp — the click-freedom comes from routing
  `setDamperDepth` through `BrownianDrift::setDepth`'s output smoother.

**Implement** plan S6.1, S6.2 and S3.3 step 1/3:

- `BrownianDrift damper_[kMaxChannels]`; exactly `numChannels_` are advanced, all `kMaxChannels` are
  prepared and seeded (T004).
- `setDamperDepth(v)`: clamp, store, then `damper_[i].setDepth(damperDepth_)` for every slot.
  **Depth is applied exactly once, here** — `getCurrentValue()` already carries it
  (`outputTarget() = clamp(depth_ * x_, −1, 1)`, verified at `brownian_drift.h:249-251`, and the
  output clamp at `:212-214`); a second multiply would square the depth (plan B-3). Put that citation
  in a header comment so a later reader cannot re-add the multiply.
- `setDamperRate(v)`: `damper_[i].setSmoothness(1.0f − damperRate_)`. `v = 0` → smoothness 1 →
  `τ = kTauMax = 30 s`; the default 0.15 → smoothness 0.85 → `τ = 0.2 + 0.85·29.8 = 25.53 s` (inside
  the decided 20–30 s "wanders slowly" band); `v = 1` → `τ = kTauMin = 0.2 s`.
- Offset generation, **once per control chunk**, before the owned engine's own control step:
  ```cpp
  constexpr float kOctavesPerUnit = kMaxDamperOctaves / BrownianDrift::kInternalStd;   // = 3.0
  for (std::size_t i = 0; i < numChannels_; ++i) {
      float o = kOctavesPerUnit * damper_[i].getCurrentValue();   // already depth-scaled, |.| <= 1
      o = std::clamp(o, -kMaxDamperOctaves, kMaxDamperOctaves);   // THIS is what is published
      if (std::abs(o) < 1.0e-20f) { o = 0.0f; }                   // FR-073 denormal flush
      damperOffset_[i] = o;
  }
  for (std::size_t i = numChannels_; i < kMaxChannels; ++i) { damperOffset_[i] = 0.0f; }
  engine_.setDamperOffsetsOctaves(damperOffset_, numChannels_);
  ```
  `getDamperOffsetOctaves(line)` returns this **post-clamp** published value (plan B-2 ruling);
  out-of-range returns 0.
- Each damper advances by a **full** `kControlChunkSamples` per control step, never by a slice length
  (`processBlock(36) + processBlock(28)` is not the same state as `processBlock(64)`,
  `brownian_drift.h:194-206`), and advances **unconditionally** — including while frozen and on
  digital silence (FR-036; what freeze suspends is the *application* of the offsets, not their
  generation). No second smoother is added: `BrownianDrift`'s internal 150 ms output smoother **is**
  FR-033's slew limit.
- At `depth == 0` the drift's `outputTarget()` is exactly `0.0f`, so every published offset is exactly
  `0.0f` **by value** and FR-044's inertness precondition holds without a tolerance. A depth automated
  1 → 0 mid-render reaches exact zero a few chunks later, which is the desired click-free behaviour.

**Verify.** `--target dsp_effects_tests`, zero warnings,
`dsp_effects_tests.exe "CavernVerb_DamperMotionSmoothness" 2>&1 | tail -5`, then the full per-push
lane `"~[long]~[.perf]"`.

---

# GROUP 8 — Decorrelation and spectral motion

## T009 — SC-004 and SC-005 (`[long]`), test-only

**Files edited:** `dsp/tests/unit/effects/cavern_verb_test.cpp` (test-only; fix the header **only**
if a criterion genuinely fails, and never by relaxing the criterion — FR-082).

**Test first**

`TEST_CASE("CavernVerb_DamperDecorrelation", "[effects][cavern][long]")` — 10 minutes at
`setDamperDepth(1.0)`, offsets sampled once per control chunk:
- **Arm (a), the gating arm:** `setDamperRate(1.0)` (`τ = 0.2 s`, ≈ 1500 effective independent
  samples, `s.d.(r) ≈ 0.026`). Over the 28 pairs of an `N = 8` configuration:
  **mean pairwise |r| ≤ 0.30** and **max pairwise |r| ≤ 0.60**.
- **Arm (b), the shipped default rate:** the same record at `kDefaultDamperRate`, gated against an
  **in-test null distribution** — a bank of `numChannels` independently re-seeded `BrownianDrift`
  instances advanced over the same record length, its 28 pairwise `|r|` computed, and
  `REQUIRE(max|r| <= P99(null))`. (A fixed threshold is wrong here: at `τ ≈ 25.5 s` a 600 s record
  holds only ≈ 10 independent samples, so `s.d.(r) ≈ 0.32` and a correct implementation would
  routinely exceed 0.60.)
- **Negative control (the teeth):** the same statistic on a record where **one** drift value is
  broadcast to all lines must **exceed** those bounds — otherwise the metric cannot discriminate and
  the criterion is vacuous.
- **Salt separation (FR-034):** no damper trajectory may equal a `BrownianDrift` seeded
  `deriveStreamSeed(seed, AetherReverb::kDriftSaltBase + j)` **nor** one seeded
  `deriveStreamSeed(deriveStreamSeed(seed, CavernVerb::kCavernReverbSalt),
  AetherReverb::kDriftSaltBase + j)` — the engine's *actual* stream, which is where a real collision
  would show.
- `WARN` both arms' mean and max.

`TEST_CASE("CavernVerb_DamperSpectralMotion", "[effects][cavern][long]")` — `setDamperRate(1.0)`
pinned (at the slow default, 115 s of 1 s frames hold only ≈ 2–6 independent samples). Statistic: the
**standard deviation over time of the tail's spectral centroid**, in 1 s frames after the first 5 s
of a 120 s G-2 render, **averaged over ≥ 8 seeds** at each depth.
- (a) **endpoint separation:** the statistic at `setDamperDepth(1.0)` is **≥ 3×** its value at
  `setDamperDepth(0.0)`.
- (b) **ordering, non-strict:** Spearman rank correlation between depth and the statistic over
  `setDamperDepth ∈ {0, 0.25, 0.5, 0.75, 1.0}` is **≥ 0.9**.
- (c) **inertness at zero, as a number:** at `depth = 0` the statistic is **≤ 1.05 ×** a reference
  render made with the extension's offsets never set.
- (d) **direction, not merely monotonicity (FR-048's sign convention):** on a **bare**
  `AetherReverb`, publish a **static** `+0.5` octave vector to every line and separately a static
  `−0.5`: the measured T60 in the 8 kHz octave band is **lower** for `+0.5` (a positive offset lowers
  the cutoff and darkens the line). Run this at `setDarkness(0.0)` **and** at the default, so a
  `c = 1` total-inertness bug at the bright end cannot hide (R-5).
- Report, not gate: the absolute centroid excursion in Hz.

**Verify.** `--target dsp_effects_tests`, zero warnings,
`dsp_effects_tests.exe "[long][cavern]" 2>&1 | tail -5` (expect ~9 minutes; these two are the
`[long]` lane, excluded from per-push CI and run nightly on all three OS legs).

---

# GROUP 9 — Echo density

## T010 — SC-002 (`CavernVerb_EchoDensity`), test-only

**Files edited:** `dsp/tests/unit/effects/cavern_verb_test.cpp`.

**Test first** — `TEST_CASE("CavernVerb_EchoDensity", "[effects][cavern]")`, G-1 impulse, `mix = 1`,
through T002's lifted `normalisedEchoDensity` helper (1 ms windows, per-window RMS, fraction above
`peak · 0.01`, on the mono sum):

- **Window, geometry-derived:** `t_start` = the first 1 ms window whose RMS exceeds `peak · 0.01`;
  `W = max(250 ms, 3·m_long)` with `m_long = max_i getEffectiveDelayLengthSamples(i)`. (A fixed window
  is arithmetically unsatisfiable at `S = 4`.)
- **Clause 1:** `REQUIRE(ned >= 0.8)` over the grid `setSize ∈ {0, 0.5, 1}` ×
  `setDimensionality ∈ {0, 1}`, `N = 8`, `setDensity` at its 0.75 default.
- **Clause 2 (the ER must not punch a sparse hole):** NED at `setEarlyLevel(1.0)` is **not lower**
  than at `setEarlyLevel(0.0)` by more than **0.05** — and **both figures are measured over one
  common window, derived from the `setEarlyLevel(0.0)` render** (at level 1, `t_start` is the first
  ER arrival; at level 0 it is the FDN onset ≈ 20 ms later, and comparing differently-anchored
  windows does not test the claim).
- **Clause 3 (FR-065, without which the level-only skip is undetectable):** (i) on the
  `setEarlyLevel(0.0)` render, `REQUIRE` the peak exceeds **−60 dBFS before** `t_start` is derived
  from it — under a buggy level-only chain skip that render is digital silence, `t_start` is
  undefined and `ned0` degenerates, making clause 2 pass vacuously; (ii) that render's energy after
  `t = 2 ×` the last tap delay exceeds the `setEarlySend(0.0)` render's by **≥ 20 dB**.
- `WARN` both `t_start` values side by side, `m_long`, `W`, the excluded-window count, both NED
  figures per configuration, the `earlyLevel = 0` peak, and the 20 dB margin.

**Implement.** Test-only. If a configuration fails, the fix is in the header (density, diffusion,
tap geometry) — **never** a lowered NED threshold (FR-082).

**Verify.** `dsp_effects_tests.exe "CavernVerb_EchoDensity" 2>&1 | tail -5`.

---

# GROUP 10 — Dark tuning and the mirrored constants

## T011 — SC-007 and SC-016, test-only

**Files edited:** `dsp/tests/unit/effects/cavern_verb_test.cpp`.

**Test first**

`TEST_CASE("CavernVerb_DarkTuning", "[effects][cavern]")`, at FR-066's pinned defaults:
- **(a) HF decay shortened:** banded Schroeder T60 (T002's helper) — `T60(8 kHz) ≤ 0.35 × T60(250 Hz)`
  from a G-1 impulse.
- **(b) darker than the Seraphis default:** the tail's long-term spectral centroid at `CavernVerb`
  defaults is **≤ 0.70 ×** that of a bare `AetherReverb` at its own defaults (`kDefaultDamping = 0.40`,
  `kDefaultSize = 0.50`), same input, same render length. `WARN` both figures so a near-miss is
  diagnosable rather than a bare red.
- **(d) no shimmer:** G-4 (220 Hz sine) rendered 20 s wet-only — narrow-band energy at **440 Hz and
  330 Hz is ≤ −40 dB** relative to the 220 Hz band, and `isShimmerActive()` reports **false**.
- (c) already lives in `CavernVerb_EarlyReflectionGeometry` from T004; cross-reference it in a
  comment so the criterion is traceable.

`TEST_CASE("CavernVerb_MirroredConstants", "[effects][cavern]")` — the three **private** `AetherReverb`
constants `CavernVerb` mirrors, probed through public behaviour only (no `static_assert` can reach a
private member, which is why FR-084's compile-time gate stops at the public facts):
- **(a) decay clamp:** `setDecaySeconds(0.4f)` vs `(0.5f)`, and `(61.0f)` vs `(60.0f)`: measured
  broadband T60s **within 5 %** of each other in each pair.
- **(b) channel count:** prepared at `numChannels = 16`,
  `getEffectiveDelayLengthSamples(15) != 0.0f` and `getEffectiveDelayLengthSamples(16) == 0.0f`.
- **(c) Nyquist ratio:** at `setDarkness(1.0)` (damping 1) the measured T60 in the 8 kHz octave band
  is **within 15 %** of `0.05 ×` the measured T60 in the 125 Hz band.

**Verify.** `dsp_effects_tests.exe "CavernVerb_DarkTuning" "CavernVerb_MirroredConstants" 2>&1 | tail -5`.

---

# GROUP 11 — Allocation and determinism

## T012 — SC-010 and SC-011, test-only

**Files edited:** `dsp/tests/unit/effects/cavern_verb_test.cpp`.

**Test first**

`TEST_CASE("CavernVerb_NoAllocation", "[effects][cavern]")`:
- **Clause 0 first, or the criterion is vacuous:** a deliberate `std::vector<float> v(1024)` inside an
  `AllocationScope` **must be counted** — the counter is only armed because
  `aether_reverb_test.cpp:38` owns `<allocation_operator_overrides.h>` for this image, and this clause
  is what fails loudly if that ever stops being linked.
- Then `AllocationScope` around **60 s** of rendering with every setter exercised mid-render —
  including `setEarlySizeMs`, `setFreeze`, `setSeed` and `setDamperDepth` — records **zero**
  allocations, and `getAllocatedBytes()` is constant across the render.
- Include `<allocation_detector.h>` only. **Never** `<allocation_operator_overrides.h>` (duplicate
  symbol).

`TEST_CASE("CavernVerb_Determinism", "[effects][cavern]")`:
- two instances, same seed, P-2's ragged `{37, 111, 513}` partitions: `compareFingerprints` inside
  `kSampleTolerance = 5.0e-4f` / `kMetricTolerance = 2.5e-4`;
- different seeds: `totalVariation` differs by **more** than those tolerances;
- `setSeed(s); reset();` mid-life restores the opening trajectory;
- partition invariance: `{37, 111, 513}` vs one 4096-sample call → same fingerprint (FR-007's
  absolute control grid);
- **arm 4 (FR-060):** configure a non-default control set (`setSize`, `setDarkness`,
  `setEarlySizeMs`, `setDamperDepth`, `setMix`) **before** `prepare()`, prepare, render, and require
  the fingerprint to match an instance configured identically **after** `prepare()`. No other test
  configures an instance before preparing, so a shadow copy that is never re-applied would otherwise
  be invisible;
- **arm 5 (R-15):** freeze, render, `reset()`, and require `isFrozen()` to still agree with the
  recorded state — `AetherReverb::reset()` clears `freezeTarget_` (`:1979`), so this fails if
  `reset()` drops the `engine_.setFreeze(ctlFreeze_)` re-issue.
- **No committed digests** (`tools/lint-float-bit-goldens.js`; roadmap line 556).

**Verify.** `dsp_effects_tests.exe "CavernVerb_NoAllocation" "CavernVerb_Determinism" 2>&1 | tail -5`.

---

# GROUP 12 — Sample rates, non-finite sentinel, breath

## T013 — SC-015 render arms, the per-push non-finite sentinel, `CavernVerb_BreathDualTarget`

**Files edited:** `dsp/tests/unit/effects/cavern_verb_test.cpp`.

**Test first**

Append to `TEST_CASE("CavernVerb_SampleRates", …)` (created in T004):
- `SECTION("renders at four rates")` — a 10 s render at 44 100 / 48 000 / 96 000 / 192 000 Hz each:
  **no non-finite output** (bit-pattern test, never `std::isnan`);
- `SECTION("re-prepare leaves no stale state")` — re-`prepare()` at a new rate mid-life, then a
  second render matching a **freshly constructed** instance within `render_fingerprint.h` tolerances.

`TEST_CASE("CavernVerb_NonFiniteSentinel", "[effects][cavern]")` — deliberately **not** `[long]` (the
project's tag convention: never tag a NaN/Inf-guard test, they are the cross-platform sentinels and
stay in the per-push lane). 60 s at SC-009 arm (b)'s configuration:
- `getNonFiniteRecoveryCount() == 0` and no non-finite output sample;
- **non-finite input clause:** inject NaN and +Inf into `inL`/`inR` for **one block** (built from bit
  patterns through `volatile`, never `std::numeric_limits::quiet_NaN()` folded under `-ffast-math`),
  then require that once the ER line has flushed, output is **finite for the remainder of the
  render**, `getEarlyTapAbsorptionCutoffHz(i)` and `getStateEnergy()` are finite, and
  `getNonFiniteRecoveryCount()` is **unchanged** (FR-064 is a *replacement*, not a recovery).
  Without this clause the S5.1 defect it guards — a single NaN latching all 12 per-tap one-pole states
  permanently, while the forwarded recovery counter reports 0 — would ship green.

`TEST_CASE("CavernVerb_BreathDualTarget", "[effects][cavern]")` — FR-017, at `setDamperDepth(0)` and
everything else at FR-066 defaults, comparing `setBreath(0)` against `setBreath(1)`:
- **(i)** the variance over time of `getEffectiveDelayLengthSamples(i)`, sampled once per control
  chunk over 10 s, is **exactly 0** at breath 0 and **> 0** at breath 1 on at least `numChannels_ − 1`
  lines — that gates `setSizeBreathDepth`;
- **(ii)** with the size-breath contribution held fixed (force `setSizeBreathDepth` equal on both
  sides through a bare `AetherReverb` reference pair), the impulse responses still differ beyond
  `kMetricTolerance` in total variation — that gates `setDimensionalityTideDepth`, the only remaining
  difference.
Dropping either target fails one clause; nothing else can detect it (SC-001 clause 1 sets
`setDimensionality(1.0)`, which is the morph *target*, not the tide depth).

**Verify.** `dsp_effects_tests.exe "CavernVerb_SampleRates" "CavernVerb_NonFiniteSentinel" "CavernVerb_BreathDualTarget" 2>&1 | tail -5`.

---

# GROUP 13 — The three remaining TUs (disjoint files)

## T014 [P] — Freeze TU: SC-001, SC-008, `CavernVerb_SilenceContract`

**Files edited:** `dsp/tests/unit/effects/cavern_verb_freeze_test.cpp` (delete the T001 scaffold).
This TU needs its own copies of the T005 fixtures (`makeDefaultCavern`, `clickConfig48`, the G-2
generator call) — duplicate them locally or put them in a small local anonymous-namespace block; do
**not** add a second `#include <allocation_operator_overrides.h>`.

**Test first**

`TEST_CASE("CavernVerb_FreezeEnergyConservation", "[effects][cavern]")`:
- **Clause 1 — the conserved quantity, with the matrix morphing.** Configuration: every Phase-9
  addition at maximum **before** the freeze — `setDamperDepth(1)`, `setDamperRate(1)`,
  `setEarlyLevel(1)`, `setEarlySend(1)`, `setSize(1)`, `setDarkness(1)`, **`setBreath(1)` and
  `setDimensionality(1)`** (the last two are not decoration: the inherited criterion mandates a
  morphing matrix, and leaving them at defaults would freeze a near-static matrix — the easy
  configuration the source criterion rejects). Timeline: 2 s G-2 → `setFreeze(true)` → 0.25 s →
  `REQUIRE(isFrozen())` → 60 s of G-3 (digital silence), sampling the forwarded `getStateEnergy()`
  **once per second**. Every sample within **±0.5 dB** of the first post-latch sample.
- **Clause 2 — per octave.** The same, but with **`setBreath(0)`** so the tide is at 0 and the matrix
  is a fixed orthogonal map (a morphing mixer moves energy *across* frequency while conserving the
  total, so a per-band bound does not follow while it morphs). Apply the same ±0.5 dB window bound
  independently to octave bands at 125, 250, 500, 1 k, 2 k, 4 k, 8 kHz, with a **−80 dBFS noise-floor
  gate** (a band whose reference-window level is below it is skipped) and `REQUIRE` that **at least 6
  of the 7** bands qualified. `WARN` the worst deviation and the qualifying count.
- **Clause 3 — the frozen-damper clause (FR-036 + FR-046).** Over the frozen, digital-silence span at
  `setDamperDepth(1)` / `setDamperRate(1)`: sample `getDamperOffsetOctaves(i)` at the first and last
  control chunk and `REQUIRE` that at least one line moved by **more than 10 × `kMaxOffsetStepPerChunk`**
  (T008's parameterised bound) — an implementation that gates `damper_[i].processBlock()` on
  `!frozen` or on input activity stops dead and fails **only here** (clauses 1 and 2 are made
  strictly *easier* by a damper bank that stopped moving). Over the same span, replay the recorded
  offsets into a bare `AetherReverb` and `REQUIRE` that `getEffectiveDampingCoefficient(i)` is
  **bit-identical at every control chunk** and resumes moving after thaw — that is FR-046's latch,
  and it is what gates E-5's placement inside the `!freezeTarget_` branch. (Do **not** claim clause 1
  gates E-5's placement: while frozen the damping one-pole contributes nothing at all, because
  `crossfade(filterState_[i], delRead[i], freezeRamp)` returns **exactly** `delRead[i]` at
  `freezeRamp == 1`, `aether_reverb.h:2996-3001`, read site `:4277-4284`.)
- Clauses 2 and 4 of the Seraphis source criterion (output-tap level; the shimmer/bloom sends) are
  **not** inherited — the first measures an engine tap this phase does not touch, the second has no
  configuration here because those stages are not constructed.

`TEST_CASE("CavernVerb_FreezeCycles", "[effects][cavern]")` — SC-008, the **pinned timeline** (not
"ten cycles"), at P-1 with `setDamperDepth(1.0)`:
```
2 s G-2   →   [ setFreeze(true), 5 s G-3, setFreeze(false), 3 s G-2 ] × 10
```
- **(a)** conservation **within** each frozen span, never across cycles: `getStateEnergy()` at the
  moment `isFrozen()` first reports true versus at the sample before `setFreeze(false)`, for each
  span independently — every pair within **±0.5 dB**; record the ten figures.
- **(b)** **zero** `ClickDetector` detections over the whole render, with P-5's configuration.
- **(c)** during the **third** frozen span only, the input is G-2 rather than G-3: the output is
  non-zero and correlated with the input at the ER tap delays (cross-correlation peak within ±1
  sample of `getLatencySamples() + getEarlyTapDelaySamples(i)`). That span is **excluded** from (a);
  the other nine carry it.

`TEST_CASE("CavernVerb_SilenceContract", "[effects][cavern]")` — FR-006, the only test that calls
`silence()`. Prepared with `spectralDiffusionEnabled = true` so `alignSamples_ > 0`; render 2 s of
G-2, call `silence()` mid-render, continue: **zero** P-5 detections over the following second, and
`getStateEnergy()` at or below the −80 dBFS gate once the engine's own 20 ms gate and amortized clear
have completed. This is the only case that fails if the four alignment lines are "tidied" into the
clear (R-14).

**Verify.** `--target dsp_effects_tests`, zero warnings,
`dsp_effects_tests.exe "CavernVerb_Freeze*" "CavernVerb_SilenceContract" 2>&1 | tail -5`.

## T015 [P] — Soak TU: SC-014 (`[long]`)

**Files edited:** `dsp/tests/unit/effects/cavern_verb_soak_test.cpp` (delete the T001 scaffold).

**Test first** — `TEST_CASE("CavernVerb_Soak", "[effects][cavern][long]")`, at SC-009 arm (b)'s
configuration (`setSize(1)`, `setDamperDepth(1)`, `setDamperRate(1)`, `setFog(1)`,
`setEarlyLevel(1)`, `setEarlySend(1)`, `setEarlySizeMs(max)`, `setBreath(1)`, `N = 16`, prepared at
`diffusionFftSize = 4096` and `maxEarlySeconds = 0.60`):
- a **30-minute** render driven by **continuously generated** G-2 for the full 30 minutes (T002's
  generator — **never** a looped buffer), peak-normalised `|x| ≤ 1.0`, unfrozen;
- **(a) peak:** `max |output| ≤ 4.0`. The derivation, so it can be checked rather than trusted: the
  ER stage is feed-forward with `Σ|g_i| = kEarlyGainSum = 1.865762 ≤ 2.0`, the owned engine's loop
  gain is ≤ 1.0 outside freeze (re-established for every admissible offset by FR-045/FR-048), and the
  equal-power dry/wet has unit maximum gain. Record the measured peak; **a measurement above 4.0 is a
  defect to fix, never an occasion to re-derive the bound**;
- **(b) no drift:** the per-minute RMS neither grows nor collapses monotonically across the 30
  samples (no strictly monotone run of length 30, and the last minute within **±3 dB** of the tenth);
- **(c)** `getNonFiniteRecoveryCount() == 0` and no output sample non-finite (bit-pattern test);
- **(d) the frozen soak:** a **second** 30-minute render with `setFreeze(true)` entered at 60 s and
  G-3 thereafter, asserting **±0.5 dB** `getStateEnergy()` conservation over the remaining 29 minutes.
  This is where "neither dies nor explodes overnight" is actually tested; clause (b) cannot carry it
  (unfrozen with decay clamped at ≤ 60 s, a silent tail decays to zero **by construction**, which is
  why the "G-2 for 60 s then silence" form was unsatisfiable).

**Verify.** `dsp_effects_tests.exe "CavernVerb_Soak" 2>&1 | tail -5` (expect ~12 minutes; `[long]`
lane).

## T016 [P] — Perf TU: SC-009 (`[.perf]`)

**Files edited:** `dsp/tests/unit/effects/cavern_verb_perf_test.cpp` (delete the T001 scaffold).

**RULED AND APPLIED 2026-09-17 (after the build stage):** the step-1 measurement was taken alone and
pinned, the four baselines are transcribed (129 150 / 190 343 / 117 604 / 125 485 ns/block), and arms
(a), (c) and (d) are timed in one interleaved trial loop (`measureTrio`) because separate timing moved
(c)/(d) by 16–20 % run to run against a 10 % relative window; the 1.10 tolerance is unchanged (spec
SC-009, plan S12.2 record). The sketch below is what T016 built; the interleaving supersedes its
per-arm `measureArm` for those three arms.

**Test first** — `TEST_CASE("CavernVerb_CpuBudget", "[.perf][cavern]")`, inheriting the
`dsp/tests/unit/effects/aether_reverb_perf_test.cpp:130-150` construction **verbatim** (verified this
session):
```cpp
constexpr double kSr48 = 48000.0;
constexpr std::size_t kBlockSize = 512;
constexpr double kBlockBudgetNs  = (static_cast<double>(kBlockSize) / kSr48) * 1.0e9;  // 10 666 666.7
constexpr double kRegressionFactor = 1.5;
constexpr double kReferenceNs      = kBlockBudgetNs * 0.05;        // 533 333.3
constexpr double kMaxAdmissibleNs  = kReferenceNs / kRegressionFactor;  // 355 555.6
```
Measurement: **best-of-25 runs × 500 blocks after 400 warm-up blocks**. Each checked-in baseline
carries **both** `static_assert(baseline * kRegressionFactor <= kReferenceNs)` **and**
`static_assert(baseline <= kMaxAdmissibleNs)`, plus the runtime `REQUIRE(measured <= baseline *
kRegressionFactor)` — so the absolute roadmap figure binds on every machine even though `[.perf]`
keeps the timing out of CI.

Arms — **every arm pins its prepare-time fields**, because `diffusionFftSize` and `maxEarlySeconds`
dominate the STFT and ER cost and an unpinned arm is not reproducible:
- **(a) default:** FR-066's pinned table, `diffusionFftSize = 1024`, `maxEarlySeconds = 0.30`;
- **(b) worst case:** `setSize(1)`, `setDamperDepth(1)`, `setDamperRate(1)`, `setFog(1)`,
  `setEarlyLevel(1)`, `setEarlySend(1)`, `setEarlySizeMs(max)`, `setBreath(1)`, `N = 16`, prepared at
  **`diffusionFftSize = 4096`** and **`maxEarlySeconds = 0.60`** — pinned at 4096 precisely so the
  comparison with the shipped `AetherReverb` worst baseline (200 114 ns/block, which is
  shimmer/bloom-**inclusive** and 4096-point) is apples to apples;
- **(c) frozen;**
- **(d) damper delta:** arm (a) at `setDamperDepth(0)`, reported as a **difference**, so the cost
  attributable to the moving dampers is a number rather than an assertion in the abstract.

Baselines are `ceil(firstCleanMeasurement × 1.05)` and are **never raised afterwards**. Headroom to
expect (plan S12.2 projection): arm (a) ≈ 150 000 ns (42 % of `kMaxAdmissibleNs`), arm (b) ≈ 250 000
ns (70 %). If an arm exceeds the ceiling, apply plan S12.3's ordered levers **L-1 … L-4** (hoist
`1−dampCoeff_`; `exp2(p·log2(1−c))` with the log hoisted into the decay recompute; hoist the
equal-power `cos`/`sin` to the control grid and lerp — its acceptance gate is `CavernVerb_MixLaw`;
skip absorption for Nyquist-clamped taps) and then **L-5: stop and surface** the arm, the
configuration and the number to the user. Do **not** reduce `kEarlyTapCount`, shrink a workload or
relax the reference.

**Verify — alone, nothing else executing (P-4):**
```bash
node tools/run-cpu-tests.js dsp_effects_tests
```
A test that flips verdicts between runs is measuring the machine, not the code: confirm nothing else
was running, let the machine idle, re-run that suite alone, and only then treat it as a defect.

---

# GROUP 14 — Integration

## T017 — Registration audit and full-suite run

**Files checked (edit only if something is missing):** `dsp/tests/CMakeLists.txt`,
`dsp/lint_all_headers.cpp`, `dsp/CMakeLists.txt`.

1. Confirm all five new TUs are in the enumerated `add_executable(dsp_effects_tests …)` list — a TU
   that is not listed silently drops out of the build and its cases never run. Confirm none of them
   was placed in another target (they must inherit `KRATE_DSP_AETHER_TEST_HOOKS`, which is
   target-wide by ODR necessity).
2. Confirm `#include <krate/dsp/effects/cavern_verb.h>` is in `dsp/lint_all_headers.cpp`'s Layer 4
   block and `include/krate/dsp/effects/cavern_verb.h` is in `KRATE_DSP_EFFECTS_HEADERS`.
3. Confirm `tests/test_helpers/reverb_metrics.h` needed **no** CMake edit (INTERFACE target) and that
   no new TU includes `<allocation_operator_overrides.h>`.
4. Confirm the phase diff touches **no** file under `dsp/tests/unit/effects/aether_reverb_*` or
   `dsp/tests/unit/systems/seraphis_*` (SC-012 (b)):
   `git diff --name-only main... | grep -E "aether_reverb_.*test|seraphis_"` must print nothing.
5. Full suite:
```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_effects_tests dsp_systems_tests seraphis_tests
build/windows-x64-release/bin/Release/dsp_effects_tests.exe "~[long]~[.perf]" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_effects_tests.exe "[long][cavern]" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe 2>&1 | tail -5
build/windows-x64-release/bin/Release/seraphis_tests.exe 2>&1 | tail -5
```
All green, **zero warnings** in the build log (own every warning; "pre-existing" is not a category).

## T018 — Portability and lint gates (FR-071, FR-072, FR-080)

```bash
node tools/check-portability.js
node tools/lint-odr.js
node tools/lint-layers.js
node tools/lint-arch-guarded-includes.js
node tools/lint-float-bit-goldens.js
node tools/lint-nonfinite-symbols.js
```
All must pass. Reminders: MSVC-green proves nothing for the Linux/macOS legs; no `std::isnan` /
`isinf` / `isfinite` anywhere in `cavern_verb.h` or the new TUs (bit patterns via `volatile`); no
narrowing in brace init and designated initialisers for both `PrepareConfig`s; every `std::size_t`
literal suffixed `u`; no arch-guarded krate include; no committed bit-exact float golden.

## T019 — clang-tidy, Seraphis-green, pluginval (SC-012 b, c)

```bash
./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Seraphis.vst3"
```
clang-tidy must be clean (fix **all** warnings, no suppressions added to dodge one). `Seraphis.vst3`
must pass strictness 5 unchanged — the extension is append-only and default-inert, and that is what
this proves. On Windows use the `.ps1`, never the `.sh`.

---

## Compliance ledger (fill from actual output, never from memory)

When the phase reports complete, every FR row cites a file:line in `cavern_verb.h` /
`aether_reverb.h` that was opened and read, and every SC row cites the `TEST_CASE` name **and the
measured figure from the run log** (NED, `t_start`, the measured incommensurability minimum, max
`|Δoffset|` per chunk, max `|Δc|`, the mean/max `|r|`, the T60 ratios, the centroid ratio, the size
ratio, the measured peak, the per-arm ns/block). A table of ✅ without those numbers is worse than an
honest ❌.
