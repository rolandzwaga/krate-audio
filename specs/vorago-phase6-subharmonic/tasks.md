# Tasks: Vorago Phase 6 — Subharmonic Engine

**Spec:** `specs/vorago-phase6-subharmonic/spec.md` (1580 lines)
**Plan:** `specs/vorago-phase6-subharmonic/plan.md` (1848 lines) — S-numbers below refer to its
sections; every code shape a task needs is already written there and is **not** re-typed here.
**Deliverable:** one new Layer 3 header `dsp/include/krate/dsp/systems/subharmonic_engine.h`
(header-only), four new test TUs under `dsp/tests/unit/systems/`, one new test-helper header
`tests/test_helpers/low_frequency_metrics.h`, two edits in `dsp/tests/CMakeLists.txt`, one include
line in `dsp/lint_all_headers.cpp`, the S14 spec amendments, and the FR-076 / SC-018 placement
ruling in the compliance record.
**Test target:** `dsp_systems_tests` (all four new TUs).
**Regression set that must stay green (FR-080, SC-016):** `dsp_core_tests`, `dsp_primitives_tests`,
`dsp_processors_tests` (in particular `sub_oscillator_test.cpp`), `dsp_systems_tests`.
**Plugin work:** none. Vorago's plugin starts at Phase 11; phases 1–10 are KrateDSP-only.
**Shipped components amended: one, by ruling (2026-09-14, SC-013 (c)):** `processors/sub_oscillator.h`
gains an append-only `advance()`; `process()` is untouched. The other nine headers must be **byte-unchanged** at the end of the
phase (FR-080, SC-016): `processors/sub_oscillator.h`, `primitives/minblep_table.h`,
`processors/envelope_follower.h`, `primitives/two_pole_lp.h`, `processors/saturation_processor.h`,
`primitives/dc_blocker.h`, `processors/breathing_modulator.h`, `primitives/smoother.h`,
`core/phase_utils.h`, `core/random.h`.

---

## How to read this file

* Tasks are grouped into **ordered groups**. A group starts only when every task in the previous
  group is green: it builds warning-free, its own cases pass, and `dsp_systems_tests` still passes.
* `[P]` marks tasks that are parallel-safe **within their group**: their file sets are fully
  disjoint. **Every task that edits `dsp/include/krate/dsp/systems/subharmonic_engine.h` is unmarked
  and sits in its own group** — that header is this phase's single shared file. So are
  `dsp/tests/CMakeLists.txt` and each of the four test TUs; only one task per group may touch any
  one of them.
* Each task is **self-contained**: exact files, the failing test to write **first** (TU,
  `TEST_CASE` name, the numeric assertions), then the implementation intent with the plan section
  carrying the code shape, then the command that verifies it. An executor needs nothing beyond the
  spec/plan sections each task cites.
* Canonical order inside every task: **write the failing test → watch it fail → implement → zero
  compiler warnings → the test passes → `dsp_systems_tests` still passes.**
* **Test-only tasks** (T013–T019, T021) carry a named **falsification** instead — a mutation or a
  fixture inversion that must make the new assertion fail — because the behaviour they measure was
  implemented in an earlier task. A test-only task is not done until its falsification has been run
  and its result recorded in the compliance notes.
* No commit tasks. Commits happen outside this workflow.

### Four deliberate deviations from the requested layout, each forced by the repo or the plan

**1. CMake registration is T001, not a final task.** `dsp/tests/CMakeLists.txt`'s `dsp_systems_tests`
source list is **enumerated, not globbed** — verified this session: `add_executable(dsp_systems_tests`
at `dsp/tests/CMakeLists.txt:324`, the Vorago Phase-5 block at `:438-449`, the list closing `)` at
`:450`. An unregistered TU compiles into nothing and its cases silently never run, so registering all
four TUs up front is the only ordering under which every later task's "run the suite" step proves
anything. The final group still carries a **registration-completeness audit** (T022), the full-suite
run (T023) and the portability/lint gate (T024).

**2. Measurement precedes implementation.** Plan S15 T1 and FR-071 require the stage-cost probe arms
(a)–(h) — the composed primitives, no engine — to run **before** the component is written, so the
three `std::sin`, the `tanh` divide and the S7.6 follower guard are priced while the pre-authorised
levers (plan S12.4) are still cheap to take. Groups B and C are therefore probe-only.

**3. `dsp/lint_all_headers.cpp`'s include lands with the header, not before it.** That file is
compiled by `add_library(dsp_lint_stub OBJECT lint_all_headers.cpp)`, so an include of a header that
does not yet exist breaks the whole build. It is edited inside T007, in the same task that creates
the header.

**4. The helper header precedes the criteria that consume it** (plan S15 T2). `low_frequency_metrics.h`
plus its self-validation cases is T006, so a helper bug fails as a helper bug and never as an engine
defect.

### Build and run commands (Windows; the full CMake path is mandatory)

```bash
CMAKE="C:/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "SubharmonicEngine_*" 2>&1 | tail -20
```

* Catch2 filters: the test-case name as a **positional** argument
  (`"SubharmonicEngine_Dormancy"`), tags in `[brackets]`. Never `-c` — that filters SECTIONs.
* `[long]` cases run locally by default and are excluded from the per-push CI filter
  `~[performance]~[perf]~[benchmark]~[!benchmark]~[long]`.
* **Timing runs alone**: `node tools/run-cpu-tests.js dsp_systems_tests`, nothing else executing —
  no build, no clang-tidy, no second suite, no parallel agent.
* Capture slow runs (`[long]`, perf, clang-tidy) to a log file on the **first** run and read the log.
  Never re-run a suite just to grep its output.

### Conventions every task inherits

* Namespace `Krate::DSP`; classes PascalCase, methods camelCase, members trailing underscore,
  constants `kPascalCase`. Header banner comment `// Layer: 3 (Systems)`.
* Every new case is tagged `[subharmonic_engine]`, plus `[long]` or `[.perf]` where the task says so:
  `TEST_CASE("SubharmonicEngine_Xxx", "[subharmonic_engine]")`.
* Finiteness is `Krate::DSP::detail::isFinite` / `isNaN` / `isInf` (`core/db_utils.h:118`, `:99`,
  `:260`) — **never** `std::isnan`/`std::isinf`/`std::isfinite`, in the header **or** in any test
  (`tools/lint-nonfinite-symbols.js` gates it).
* **Non-finite values may be named in exactly one TU**,
  `dsp/tests/unit/systems/subharmonic_engine_nonfinite_test.cpp`, and there they are built from bit
  patterns through a `volatile` sink — never `std::numeric_limits<float>::quiet_NaN()`/`infinity()`,
  which fold to finite garbage on the macOS/Linux `-ffast-math` legs. Transcribable idiom:
  `dsp/tests/unit/systems/resonance_drift_network_nonfinite_test.cpp:149-155` (`makeNonFinite(bits)`;
  patterns `0x7FC00000`, `0x7F800000`, `0xFF800000`).
* **No bit-exact float goldens** anywhere (`tools/lint-float-bit-goldens.js`). Where a render must be
  pinned use `tests/test_helpers/render_fingerprint.h` (`kSampleTolerance = 5.0e-4f`,
  `kMetricTolerance = 2.5e-4`, `compareFingerprints`). The exact (`==`) comparisons this phase does
  allow are all **within-render structural identities**: SC-012 (c) tapped-vs-untapped, SC-014 (a)
  dormant pass-through, SC-022 (a1)/(b)/(b2), and the `== 0.0f` dormancy predicate.
* Brace-initialised aggregates use **designated initialisers**
  (`SubharmonicEngine::PrepareConfig{.maxBlockSamples = 512}`) — Clang errors on narrowing where
  MSVC does not.
* Tests include `tests/test_helpers/allocation_detector.h` only; **never**
  `allocation_operator_overrides.h` — `dsp_systems_tests` already has its single owner and a second
  include is a duplicate-symbol link error.
* **Streaming statistics are mandatory in every `[long]` case**: a 10-minute stereo render at 48 kHz
  is 230 MB. Render in 512-sample blocks and accumulate per-minute RMS / peak / finiteness
  incrementally; never materialise more than one block plus one analysis window.
* Every `ClickDetector` fixture sets `ClickDetectorConfig::sampleRate` to the **render rate** — the
  struct's default is `44100.0f` (`tests/test_helpers/artifact_detection.h:38`), which at a 48 kHz
  render mis-reports every `timeSeconds` it returns.
* **Zero compiler warnings is part of every task's definition of done**, not a later cleanup.

### The two shared fixtures (code them once per TU, plan S10.3)

* **`IsolatedSub`** (spectral TU): 48 kHz, `PrepareConfig{.maxBlockSamples = 512}`,
  `setTrackingAmount(0.0f)`, `setWetGainDb(0.0f)`, `setLowpassCutoffHz(kMaxLowpassHz)`,
  `setDriveDb(kMinDriveDb)`, exactly ONE tone at `setToneLevelDb(t, -20.0f)` with the other two at
  `kMinToneLevelDb` (exact zero gain), `setToneBreathDepth(t, 0.0f)`, silent stereo input, measure
  `subTap` from `processBlockTapped`, discard the first 2 s. **Gate:** analysed `subTap` RMS above
  −60 dBFS. The gate applies to SC-003, SC-004, SC-005 and SC-020 (b) only — **SC-002 opts out** and
  uses its own two-part anchor gate (T017), because the unconditional floor fails a *correct*
  implementation at the −60 dBFS body (predicted tap −64.5 dBFS).
* **`makeDefaults`** (main TU): 48 kHz, `PrepareConfig{.maxBlockSamples = 512}`, every shipped
  default untouched, fixed seed, a steady −12 dBFS 55 Hz sine on both channels as the body.

### The stop-and-surface rule (FR-071, inherited verbatim from `resonance_drift_network_perf_test.cpp:57-64`)

**NON-NEGOTIABLE.** No implementing agent may lower `kNumTones`, raise `kBudgetNs = 53333`, relax a
threshold, shrink a workload, or widen a tolerance to make a figure fit. Reduce cost, never move the
line. If a gated figure misses, the build **stops** and surfaces the measured per-stage table for a
user ruling. The two thresholds most likely to be tested by measurement are SC-013 (c)'s 15 %
dormancy margin and SC-004's 2 % THD ceiling; both have a written response (plan R-12, S4.2) and
neither may be moved.

---

## Group A — Registration, the spec amendments, and the ODR sweep

Three disjoint file sets: T001 owns `dsp/tests/CMakeLists.txt` + the four new TU stubs, T002 owns
`spec.md`, T003 owns nothing (read-only). All three are `[P]`.

### T001 [P] — Create the four TU stubs and register them

**Files to create**

* `dsp/tests/unit/systems/subharmonic_engine_test.cpp`
* `dsp/tests/unit/systems/subharmonic_engine_spectral_test.cpp`
* `dsp/tests/unit/systems/subharmonic_engine_perf_test.cpp`
* `dsp/tests/unit/systems/subharmonic_engine_nonfinite_test.cpp`

Each stub is `#include <catch2/catch_test_macros.hpp>` plus a one-line comment naming the criteria it
will own (plan S10.2). No `TEST_CASE` yet — T004 writes the perf TU's first case.

**Files to edit**

* `dsp/tests/CMakeLists.txt`, the `dsp_systems_tests` source list — append **after** the Phase-5 block
  ending at `:449` and **before** the closing `)` at `:450`, using the Phase-5 comment shape verbatim.
  The exact block to insert is plan S11 Edit 1 (four `unit/systems/subharmonic_engine_*.cpp` lines
  under the "ENUMERATED, not globbed" header and the per-TU criteria map).
* `dsp/tests/CMakeLists.txt`, the `-fno-fast-math` block (opens `:542`
  `if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")`; the Phase-5 entry is `:876`, the `PROPERTIES` line
  `:877`) — insert **exactly one** of the four TUs immediately before `:877`, with the comment from
  plan S11 Edit 2 explaining why the other three stay out:

  ```cmake
          unit/systems/subharmonic_engine_nonfinite_test.cpp
  ```

  A file may appear in only one `set_source_files_properties()` call — this TU must **not** also
  appear in the `-O2` block below it (`:886`).

**Test first:** not applicable — this task creates the harness the later tests live in. Its own
falsification is the build: before the edit the four TUs are invisible to CMake.

**Verify**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe 2>&1 | tail -5
```

Links and passes unchanged (no new cases yet). Confirm all four TU paths appear exactly once in the
CMake configure output or the generated build files.

**Done when:** the suite builds warning-free and its pass count is unchanged.

---

### T002 [P] — The S14 spec amendments (plan T0)

**Files to edit:** `specs/vorago-phase6-subharmonic/spec.md` only.

Land the plan's S14 corrections in the spec text, so the FRs the build asserts against are the ones
on disk. Each is a transcription except the last two.

| Correction | Edit |
|---|---|
| **C-3** | FR-055: add one clause recording the **sensor guard** — a per-sample `detail::isFinite` on the follower's input and a per-control-step rejection of a non-finite `getCurrentValue()` with `follower_.reset()`. State explicitly that the dry path is **not** sanitised. Without this, SC-009 (c2) fails on a correct-by-the-old-spec implementation (plan S7.6). |
| **C-4** | FR-052 bullet 1: carry the `SubOscillator` output bound. `kMaxPreSaturationMagnitude = kNumTones × 2.0 × dbToGain(+6) × (1 + 0.45) ≈ 17.36`, not ≈ 8.68. Note that `kMaxPreClampMagnitude` is unchanged at ≈ 2.00. |
| **C-5** | FR-021 default table: add `irregularity = 0.25` on all three breathers (`kDefaultBreathIrregularity`), with the reason — at the modulator's shipped `irregularity_ = 0` the RNG is never drawn, the FR-070 seed is inert and SC-011 (b) cannot pass. Not exposed as a setter; FR-060's list stays closed. |
| **C-6** | A-2: `SaturationProcessor`'s three parameter smoothers are `OnePoleSmoother`s, not `LinearRamp`s (`saturation_processor.h:415-417`). Type name only; the three-advances-per-`processSample` assumption is unaffected. |
| **C-7** | A-4: `measureTruePeakDb` gains a fourth parameter, `double sampleRate` — `Oversampler::prepare` requires it. |
| **C-12** | FR-026: one clause noting that `SaturationProcessor::reset()` clears no audio state reachable from `processSample`, and that its `std::fill` over `dryBuffer_` is an O(`maxBlockSamples`) memset at every dormancy edge. The call **stays** (FR-026 names it). Restate SC-014 (c2) as a **two-line** mutation (`lowpass_.reset()`, `blocker_.reset()`). |
| **C-11** (rewrite) | Replace SC-022 (a) with the (a1)/(a2) text from plan S10.4: the stated operand ("a render taken before FR-064 existed") is not producible, so the criterion cannot fail as written. (a1) silent input, flag `true`, after 100 ms settling `outL[i] == outR[i]` and `outL[i] == clamp(subTap[i] * wetGainLinear, ±kOutputClamp)` bit-exactly at every sample; (a2) with the decorrelated body of (b), `(outL−inL)` and `(outR−inR)` agree within `1e-6 · max(1, \|in[i]\|)`. |
| **C-10** (surface, do not act) | Add a Clarifications note: `EnvelopeFollower`'s ms figures are ~99 %-settling times, `coeff = exp(-2π/(ms·fs))`, so FR-031's `120 / 800 ms` are τ = **19.1 / 127.3 ms** (the release in the squared domain, i.e. amplitude τ = 254.6 ms) — a **faster** follower than FR-031's own rationale describes. **The defaults are not changed by this task.** Record that if the user wants the stated behaviour the values become ~750 / 5000 ms and SC-001 (c2)'s window moves with them, and carry it as an open question to the phase report. |

**Test first:** not applicable (documentation). Falsification: after the edit, `grep -n "8.68"
spec.md` returns nothing, `grep -n "irregularity" spec.md` hits FR-021, and SC-022 (a) contains no
"before FR-064 existed" phrasing.

**Done when:** all eight rows are on disk and C-10 is recorded as an open question, not silently
applied.

---

### T003 [P] — Re-run the ODR and near-name sweeps, record the transcript

**Files:** none created or edited. Output goes into the phase's compliance notes.

Re-run, verbatim, from the repo root, and paste the transcript with hit counts:

```bash
grep -rn "class SubharmonicEngine"    dsp/ plugins/
grep -rn "class SubEngine"            dsp/ plugins/
grep -rn "class SubharmonicGenerator" dsp/ plugins/
grep -rn "class SubharmonicBank"      dsp/ plugins/
grep -rn "class SubOscillatorBank"    dsp/ plugins/
grep -rn "class SubDivider"           dsp/ plugins/
grep -rn "class SubVoice"             dsp/ plugins/
grep -rn "class SubTone"              dsp/ plugins/
grep -rn "struct SubTone"             dsp/ plugins/
grep -rn "class SubChain"             dsp/ plugins/
grep -rn "class SubStack"             dsp/ plugins/
grep -rn "class FifthBelowOscillator" dsp/ plugins/
grep -rn "class WeightEngine"         dsp/ plugins/
grep -rn "SubharmonicEngineNonFiniteProbe" dsp/ plugins/ tools/
ls dsp/include/krate/dsp/systems/subharmonic_engine.h
```

Every one must return **zero hits** (the last must report "No such file"). The one near-name in the
tree, `SubharmonicValidator` (`processors/subharmonic_validator.h:47`), is a YIN octave-error
corrector and is unrelated — record that it was checked, not merely assumed.

**Done when:** the transcript is recorded. If any sweep returns a hit, **stop** and surface it: a
colliding name is a redesign decision, not an implementer's rename.

---

## Group B — Measure before writing (plan S15 T1, FR-071 arms (a)–(h))

### T004 — The stage-cost probe, with no component in existence

**Files to edit:** `dsp/tests/unit/systems/subharmonic_engine_perf_test.cpp` (the T001 stub) only.

**Test to write (this task's whole deliverable):**
`TEST_CASE("SubharmonicEngine_StageCostProbe", "[subharmonic_engine][.perf]")`.

Measurement basis, inherited verbatim from `resonance_drift_network_perf_test.cpp:1-90`:
**nanoseconds per 512-sample block at 48 kHz**, best-of-**25** trials × **500** blocks after **400**
warm-up blocks. Carry the constants and the rule as file-scope comments:

```cpp
constexpr double kBlockPeriodNs = (512.0 / 48000.0) * 1.0e9;   // 10 666 666.67
constexpr double kBudgetNs      = 53333.0;                     // 0.5 % — NO AGENT RAISES IT
static_assert(kBudgetNs >= kBlockPeriodNs * 0.0049 && kBudgetNs <= kBlockPeriodNs * 0.0051,
              "SC-013's budget is 0.5 % of one 512-sample block at 48 kHz");
```

Arms in this task — **composed primitives only, no `SubharmonicEngine`** (plan S12.2):

| Arm | Workload |
|---|---|
| (a) | two `PhaseAccumulator`s, `advance()` ×2 per sample |
| (b) | one `SubOscillator` at `SubWaveform::Sine`, driven by a 55 Hz master |
| (c) | one `SubOscillator` at `SubWaveform::Square`, same master |
| (d) | three `SubOscillator`s summed with three `LinearRamp` gains + a held breath scalar |
| (e) | `EnvelopeFollower::processSample` in `DetectionMode::RMS` **including** a `detail::isFinite` guard on its input |
| (f) | `TwoPoleLP::process` per sample **plus** one `std::exp2` + one `LinearRamp::process()` + one compare per 64 samples |
| (g) | `SaturationProcessor::processSample` at `SaturationType::Tape`, input gain +3 dB / output gain −3 dB |
| (h) | `DCBlocker2::process` prepared at 18 Hz |

Every arm `REQUIRE`s only that its figure is **finite and strictly positive** — a zero or a NaN means
the measurement is broken, which is the one thing that would make the table lie. **No arm is gated in
this task**; the gates arrive with the engine arms in T020.

`INFO`/`WARN` each figure as ns/block and as a percentage of `kBlockPeriodNs`, so T005 can transcribe
them without re-running.

**Verify** (alone, nothing else executing):

```bash
node tools/run-cpu-tests.js dsp_systems_tests 2>&1 | tee /tmp/p6-probe-primitives.log
```

**Done when:** all eight arms report finite positive figures, the TU builds warning-free, and the log
is captured for T005.

---

## Group C — Read the probe, take the realisation decisions

### T005 — Record the primitive table; decide whether a lever is needed now

**Files:** none edited. Output goes into the phase's compliance notes.

From T004's captured log, transcribe arms (a)–(h) as ns/block and as a percentage of
`kBlockPeriodNs = 10 666 667 ns`. Then compute the **predicted engine total** at defaults:

```
(a) + 3 × (b) + (e) + (f) + (g) + (h) + the eight LinearRamp advances
```

and compare it with `kBudgetNs = 53333`.

* If the prediction is under budget with margin: record it and proceed to Group D unchanged.
* If the prediction is at or over budget: **stop and surface**. The pre-authorised levers, in order
  (plan S12.4), are (1) replacing the `SaturationProcessor` stage with a direct `Sigmoid::tanh` plus
  this component's own two scalars — **a departure from FR-041 that must be raised as a spec
  question, never taken silently**; and (2) if the three `std::sin` dominate, nothing in this phase
  can remove them without abandoning D-4's reuse mandate — surface arms (b) and (d) and let the user
  rule. **Neither lever may be applied before the measured table is on the record.**

**Done when:** the eight figures, the predicted total and the margin are recorded, and either the
"proceed" note or the surfaced ruling is written down.

---

## Group D — The test-helper header

### T006 — `low_frequency_metrics.h` and its self-validation cases

**Files to create:** `tests/test_helpers/low_frequency_metrics.h` (namespace
`Krate::DSP::TestUtils`, header-only). **No CMake edit** — `tests/test_helpers/CMakeLists.txt` is
`add_library(test_helpers INTERFACE)` with no source list. **No edit to any shipped helper** —
`spectral_analysis.h`, `signal_metrics.h` and `buffer_comparison.h` are consumed by dozens of TUs.

**Files to edit:** `dsp/tests/unit/systems/subharmonic_engine_spectral_test.cpp` and
`dsp/tests/unit/systems/subharmonic_engine_test.cpp` (the T001 stubs), for the self-validation cases.

**Contents** — the five declarations and the full doc comments are in plan S10.1 and must be
transcribed, not paraphrased:

* `inline constexpr std::size_t kLowFrequencyFftSize = 262144;` (0.1831 Hz bins at 48 kHz, 5.46 s).
* `estimatePeakFrequencyHz(const float* x, std::size_t n, float sampleRate)` — Hann-windowed
  magnitude spectrum, peak bin, then **parabolic interpolation of the log magnitude** across the peak
  bin and its two neighbours. Negative sentinel if the frame's RMS is below −60 dBFS.
* `measureLowFrequencyThdPercent(const float* x, std::size_t n, float fundamentalHz, float sampleRate, int maxHarmonic = 10)`
  — power summed over peak bin ±2 (the Hann main lobe) at the fundamental and at each harmonic
  `k = 2..maxHarmonic`; `THD = sqrt(Σ P_k) / sqrt(P_1) × 100`. **Negative sentinel** when the
  fundamental's summed power is below −60 dBFS, so a silent tone fails rather than reporting 0 %.
* `inline constexpr std::size_t kPeakExclusionBins = 4;` and
  `findSpectralPeaks(const float* x, std::size_t n, float relativeThresholdDb)` — candidates at or
  above the threshold relative to the global maximum, visited in **descending magnitude order**,
  accepted only if no already-accepted peak lies within ±4 bins. The rule is part of SC-005, not an
  implementation detail: a plain local-maxima scan reports the Hann window's own first sidelobe
  (−31.5 dB, ~2.4 bins off) and fails a correct implementation.
* `measureTruePeakDb(const float* l, const float* r, std::size_t n, double sampleRate)` — 4×
  `Oversampler` per channel on the `true_peak_limiter.h:125-146` basis, with the raw sample folded
  into the max (C-7: the `sampleRate` parameter is a deviation from spec A-4's three-argument sketch).
* `calculateCorrelation(const float* a, const float* b, std::size_t n)` — pointer/length zero-lag
  normalised correlation. Different namespace from `TestHelpers::calculateCorrelation`
  (`buffer_comparison.h:201`, a `template<size_t N>` over `std::array`), so no overload interaction.

**Tests to write FIRST** — each helper validated against a synthetic signal of known truth, so a
helper bug fails as a helper bug:

* `TEST_CASE("SubharmonicEngine_LowFrequencyMetricsSelfCheck", "[subharmonic_engine]")` in the
  **spectral** TU:
  * `estimatePeakFrequencyHz` within **0.005 Hz** of synthetic 27.5 / 36.67 / 110 Hz sines at 48 kHz
    over `kLowFrequencyFftSize` samples;
  * `measureLowFrequencyThdPercent` **≤ 0.01 %** on a pure 27.5 Hz sine, and within **1 % relative**
    of a synthesised third-harmonic reference constructed at a known **5 %**;
  * `measureLowFrequencyThdPercent` returns a **negative** value on a silent buffer;
  * `findSpectralPeaks(..., -40.0f)` on a synthetic 55 Hz sine returns **exactly one** peak, within
    ±1 bin of 55 Hz; on a synthetic 55 Hz + 82.5 Hz pair, **exactly two**.
* `TEST_CASE("SubharmonicEngine_OutputMetricsSelfCheck", "[subharmonic_engine]")` in the **main** TU:
  * `measureTruePeakDb` within **0.1 dB** of 0 dBTP on a full-scale 1 kHz sine, and **≥ the sample
    peak** on a seeded noise buffer;
  * `calculateCorrelation` returns **1.0**, **−1.0** and **~0.0** (|r| < 0.05) on identical, inverted
    and independently-seeded pairs of 100 000 samples.

**Implementation note (mandatory):** one `kLowFrequencyFftSize` frame is a 1 MB input buffer plus
131 073 `Complex` bins plus the `FFT`'s three internal aligned buffers (~3 MB). Prepare **one** `FFT`
per TU in a function-local `static` or a fixture member — five criteria share the size. Every
consumer `REQUIRE`s `fft.isPrepared()` before trusting a result (`fft.h:255`); `FFT::prepare` only
requires a power of two, and its doc comment's "[256, 8192]" describes the sizes the original spec
exercised, not a limit in the code.

**Verify**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "SubharmonicEngine_*SelfCheck" 2>&1 | tail -5
```

**Done when:** both self-check cases pass, warning-free, and `dsp_systems_tests` is still green.

---

## Group E — The header skeleton

### T007 — `subharmonic_engine.h`: constants, nested types, state, the full public surface

**Files to create:** `dsp/include/krate/dsp/systems/subharmonic_engine.h`.
**Files to edit:** `dsp/lint_all_headers.cpp` — after the Phase-5 include at `:185`:

```cpp
// Vorago Phase 6 (specs/vorago-phase6-subharmonic), FR-001
#include <krate/dsp/systems/subharmonic_engine.h>
```

**Write, transcribing plan S1.1 – S1.6 exactly:**

* The include list of plan S1.1 and **nothing above Layer 2**. `tools/lint-layers.js` must pass.
* **All four copy/move operations deleted** (plan S1.1, S14 C-1). This is not defensiveness:
  `MinBlepTable` is non-copyable but **movable** (`minblep_table.h:55-59`), so an implicit move would
  leave all three `SubOscillator`s pointing at the moved-from object's table — a use-after-free no
  test would provoke.
* `SubharmonicEngine() noexcept { applyDefaults(); }` (FR-003) and `~SubharmonicEngine() = default;`.
* Every constant of plan S1.2 with **every `static_assert` live**, in particular: the
  `kControlChunkSamples == 64` grid pin; `kBlepZeroCrossings * 2 <= 64`; both clamp-ordering asserts
  at the 8 kHz floor; `kMasterNyquistRatio * (4/3) < 1.0`; the breath-rate and follower-range mirrors
  against `BreathingModulator::kMinRate`/`kMaxRate` and `EnvelopeFollower::kMin/kMaxAttackMs` /
  `kMin/kMaxReleaseMs`; the "FR-031's defaults differ from the shipped 10/100 ms" assert;
  `kMaxDriveDb <= 24 && kMinDriveDb >= -24`; `kMaxPreSaturationMagnitude > kSaturatorOutputBound`;
  `kMaxPreClampMagnitude < kOutputClamp`; `kMinToneHz < kInfrasonicFilterHz`.
* Nested `enum class Tone : std::uint8_t { Div2 = 0, Div4 = 1, FifthBelow = 2 }` — **APPEND ONLY**,
  it becomes a persisted plugin parameter at Phase 12 — nested `struct PrepareConfig`
  (`std::size_t maxBlockSamples = 2048`, designated initialisers mandatory), and
  `static constexpr std::size_t index(Tone)`.
* The **complete** public surface of plan S1.4 with stub bodies (setters empty, getters returning the
  member or the documented neutral). Every setter and getter takes `std::size_t tone`, never `Tone`.
* The private state of plan S1.5 in declaration order, including the comment explaining why
  `breathRateHz`, `followerAttackMs_` and `followerReleaseMs_` are **deliberately not stored** (S8's
  forwarding getters, S14 C-13).
* The salt table of plan S1.6 (`kSaltBreath = 0`, `kSaltNextFree = 8`, overlap `static_assert`).
* The probe forward declaration and the friend, transcribed from plan S7.5:
  `namespace detail { struct SubharmonicEngineNonFiniteProbe; }` — **declared and never defined by
  the library** — plus `friend struct detail::SubharmonicEngineNonFiniteProbe;` inside the class.

**Test first:** the falsification for a skeleton is compilation, and it is real here — every
`static_assert` above is a live assertion about shipped constants. Additionally add
`TEST_CASE("SubharmonicEngine_StructuralBounds", "[subharmonic_engine]")` to the main TU with the
compile-time half only for now: `STATIC_REQUIRE(SubharmonicEngine::kNumTones == 3)`,
`STATIC_REQUIRE(SubharmonicEngine::kControlChunkSamples == 64)`,
`STATIC_REQUIRE(SubharmonicEngine::kMaxPreClampMagnitude < SubharmonicEngine::kOutputClamp)`,
`STATIC_REQUIRE(SubharmonicEngine::kMaxPreSaturationMagnitude > SubharmonicEngine::kSaturatorOutputBound)`.
The allocation-ledger half of this case arrives in T008.

**Verify**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
node tools/lint-layers.js
node tools/lint-odr.js
node tools/lint-nonfinite-symbols.js
```

**Done when:** the header compiles inside `dsp_lint_stub` and the suite, warning-free; the three
lints pass; `SubharmonicEngine_StructuralBounds` passes.

---

## Group F — Lifecycle, defaults and the memory contract

### T008 — `prepare()` / `reset()` / `setSeed()` / `applyDefaults()` / the clamp helpers

**Files to edit:** `dsp/include/krate/dsp/systems/subharmonic_engine.h`,
`dsp/tests/unit/systems/subharmonic_engine_test.cpp`.

**Tests to write FIRST** (all in the main TU; all fail against T007's stubs):

* `TEST_CASE("SubharmonicEngine_ControlSurfaceContract", "[subharmonic_engine]")` —
  * **FR-003, default-constructed and unprepared:** `getToneLevelDb` = `{-18, -24, -30}`,
    `getToneBreathRate` = `{0.037, 0.023, 0.014}`, `getToneBreathDepth` = `{0.35, 0.25, 0.45}`,
    `getToneWaveform` = `SubWaveform::Sine` ×3, `getFollowerAttackMs` = **120**,
    `getFollowerReleaseMs` = **800**, `getFundamentalHz` = 55, `getTrackingAmount` = 1.0,
    `getTrackReferenceDb` = −18, `getLowpassCutoffHz` = 120, `getDriveDb` = 3, `getWetGainDb` = 0,
    `getSubToMainEnabled` = true, `isPrepared()` = false. *(This is the arm that fails if the FR-031
    push is omitted — the follower would report its own shipped 10 / 100 ms.)*
  * **FR-009 clamping, both ends of every named range**, each written and read back:
    fundamental `[8, min(4186, 0.3·fs)]`; tone level `[-60, +6]`; breath rate `[0.01, 0.5]`; breath
    depth `[0, 1]`; tracking amount `[0, 1]`; track reference `[-48, 0]`; follower attack
    `[0.1, 500]`; follower release `[1, 5000]`; low-pass `[40, min(2000, 0.45·fs)]`; drive
    `[0, 12]`; wet gain `[-60, +6]`. The three forwarding getters (`getToneBreathRate`,
    `getFollowerAttackMs`, `getFollowerReleaseMs`) prove the engine's clamp and the composed object's
    clamp agree.
  * **Out-of-range tone index** (`3`, `SIZE_MAX`): every tone setter is a silent no-op (a
    neighbouring tone's getter is unchanged) and every tone getter returns the documented neutral —
    `0.0f`, `false`, `SubWaveform::Sine`.
  * *(Non-finite **rejection** is SC-009 (a) and lives in the IEEE TU, T019 — not here.)*
* `TEST_CASE("SubharmonicEngine_NoAllocation", "[subharmonic_engine]")` (SC-010) — inside an
  `AllocationScope`: 10 000 `processBlock` calls of mixed sizes, every setter exercised, `reset()`,
  and `processBlockTapped` → `getAllocationCount() == 0`. Plus: `getAllocatedBytes() > 0`, **identical
  across two `prepare()` calls** with the same config, **unchanged by `reset()`**, and equal to
  `8384 + 4 × maxBlockSamples`.
* Extend `SubharmonicEngine_StructuralBounds` (T007) with the FR-073 ledger: `getAllocatedBytes()`
  equals the formula for `maxBlockSamples ∈ {64, 512, 8192}`; a request of **16 clamps to 64** and
  **99 999 clamps to 8192**, read back through `getMaxBlockSamples()`.
* `TEST_CASE("SubharmonicEngine_RateAndReprepare", "[subharmonic_engine]")` (SC-019) —
  (a) `prepare(44100)` then `prepare(96000)` on the same object with the full setter sequence
  re-pushed after each: all three `getToneFrequencyHz` unchanged within **1e-3 Hz**; a write above
  each rate-derived ceiling shows `getFundamentalHz` / `getLowpassCutoffHz` moving with the rate;
  10 s renders at each rate agree in sub-band RMS within **1 dB**. (b) `prepare()` twice on a fully
  configured object → **every** FR-061 getter equals a freshly prepared instance's, asserted getter
  by getter. (c) `prepare(4000)` → `getSampleRate() == 8000.0`, and both ends of the FR-013 and
  FR-040 ranges write and read back cleanly (no inverted `std::clamp` bounds; MSVC's `<algorithm>`
  traps them).

**Implement:** plan S2 steps 1–13 in that exact order, S2.1's four clamp helpers, S2.2's default
table (**every** row, including the three that differ from the composed object's own shipped default:
waveform `Sine` over `SubOscillator`'s `Square`, `DetectionMode::RMS` over `Amplitude`, follower
`120 / 800` over `10 / 100`), S2.3's `setFundamentalHz` and `getToneFrequencyHz`, S3.1's `reset()` and
S3.2's `setSeed()`. Three traps the plan calls out and this task must not fall into:

1. **`blepTable_.prepare(64, 8)` runs BEFORE any `SubOscillator::prepare`** — on a null or unprepared
   table `SubOscillator::prepare` sets `prepared_ = false` and `process()` returns `0.0f` **forever**
   (`sub_oscillator.h:143-147`, `:224-226`). A Sine-only configuration then renders **silence**, and
   several "≤ −80 dBFS" criteria would pass on a dead engine.
2. **`cutoffGlide_.configure(kGlideMs, fs / kControlChunkSamples)`** — the *control* rate, not the
   audio rate (`smoother.h:100-108`). Getting this wrong makes the glide 64× too slow (3.2 s) and
   SC-020 (c)'s 52 ms window unreachable.
3. **`setSeed(seed_)` is step 12, after the breathers are prepared** — `BreathingModulator::prepare`
   calls `initState()`, which re-seeds from `configuredSeed_` and would discard an earlier
   distribution. And `setSeed` must call `breath.reset()` per tone, or the same seed does not
   re-render identically from the top.

**Verify**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "SubharmonicEngine_*" 2>&1 | tail -10
```

**Done when:** the four cases pass, warning-free, and `dsp_systems_tests` is green.

---

## Group G — The tones, the two masters, and the frequency read surface

### T009 — Tone mapping, the `4f/3` construction, and the backstop law

**Files to edit:** `dsp/include/krate/dsp/systems/subharmonic_engine.h`,
`dsp/tests/unit/systems/subharmonic_engine_test.cpp`.

**Test to write FIRST:** `TEST_CASE("SubharmonicEngine_ToneMapping", "[subharmonic_engine]")`.

* **Frequencies (FR-012).** At `f ∈ {20, 55, 110, 220, 440}` Hz, 48 kHz:
  `getToneFrequencyHz(0) == f/2`, `(1) == f/4`, `(2) == 2f/3`, each within **1e-3 Hz**.
* **The fifth is exact, not approximate.** At every `f` above,
  `getToneFrequencyHz(2) / getToneFrequencyHz(0)` is within **2 cents** (1.2e-3 relative) of `4/3`.
  This is the assertion that proves the `4f/3`-master construction rather than "some low tone
  appeared"; it fails on the rejected `setFrequency(4.0f/3.0f * f, fs)` form only over long horizons,
  so also assert the **double**-precision ratio directly:
  `|getToneFrequencyHz(2) / getToneFrequencyHz(0) − 4.0/3.0| < 1e-6`.
* **Ceiling (FR-013).** `setFundamentalHz(1e6f)` → `getFundamentalHz() == min(4186, 0.3·fs)`, and at
  `prepare(8000)` the ceiling is 2400 Hz.
* **Phase continuity (FR-014).** A `setFundamentalHz` write does not touch either accumulator's
  phase; assert indirectly through the read surface now (the audio arm is SC-008 in T014).
* **SC-020 (d), the backstop boundaries.** `isToneInfrasonicFloored` probed one Hz either side of
  `f = 48` (Div4 crosses 12 Hz), `f = 24` (Div2) and `f = 18` (FifthBelow): `true` below, `false`
  above, on the named tone only, with the other two unaffected.
* **At the FR-013 default `f = 55`, none of the three is floored** (13.75 / 27.5 / 36.67 Hz, and
  `13.75 > kMinToneHz = 12`) — the fixture precondition every "all three tones awake" criterion
  (SC-013, SC-017, SC-021) rests on.

**Implement:** plan S2.3 (`setFundamentalHz` as the ONE owner of both increments;
`masterFifth_.increment = masterUnison_.increment * (4.0 / 3.0)` in **double**, never
`setFrequency(4/3·f)`), `getToneFrequencyHz` derived **from the increments** so the read surface and
the rendered pitch cannot disagree, plan S4.1's structural octave assignment
(`Div2 → OneOctave`, `Div4 → TwoOctaves`, `FifthBelow → OneOctave`, assigned in `prepare()` step 5 and
exposed by no setter), and plan S5.2's `gateSteady(tone)` with its **literal** `0.0f` / `1.0f` plus
`isToneInfrasonicFloored`.

**Verify:** build the target, run `"SubharmonicEngine_ToneMapping"`, then the whole suite.

**Done when:** the case passes warning-free and the suite is green.

---

## Group H — The render path

### T010 — Entry points, the guard ladder, the control step, and the chain

The single largest task in the phase, and it is indivisible: nothing renders until the chunk loop and
the chain both exist. **Dormancy is deliberately excluded** and belongs to T011 — write
`chainActive_ = true` unconditionally here and do **not** add the `if (!chainActive_)` branch.

**Files to edit:** `dsp/include/krate/dsp/systems/subharmonic_engine.h`,
`dsp/tests/unit/systems/subharmonic_engine_test.cpp`.

**Tests to write FIRST:**

* Extend `SubharmonicEngine_ControlSurfaceContract` with the **FR-050 guard ladder** (three
  normative contracts no criterion otherwise owns):
  1. **Pre-`prepare()` passthrough** — a default-constructed engine rendering non-trivial input
     copies in → out on both channels and writes **zeros** to `subTap`. Run it **in place**
     (`outL == inL`, `outR == inR`) too. This is a deliberate divergence from `FeedbackEcology`,
     which zero-fills (`:951-955`); a copy-the-neighbour implementation silences the dry path.
  2. **Null pointers** — each of the four channel pointers nulled in turn, `subTap` nulled, and all
     six positions per `spec.md:1233-1234`: the output buffer is **byte-unchanged** from a pre-filled
     sentinel pattern, and nothing advances.
  3. **`numSamples == 0` consumes no control step** — interleave a zero-length call into a
     64-sample-aligned render and fingerprint against the uninterrupted render: identical, because
     the FR-007 residue is **absolute**, not block-relative.
* `TEST_CASE("SubharmonicEngine_BlockInvariance", "[subharmonic_engine]")` (SC-012) —
  (a) 30 s in 512-sample blocks vs a **seeded pseudo-random partition** of chunk sizes in `[1, 1024]`
  → `compareFingerprints` within `kSampleTolerance = 5.0e-4f` / `kMetricTolerance = 2.5e-4`;
  (b) in-place (`outL == inL`, `outR == inR`) vs out-of-place → same;
  (c) `processBlockTapped`'s main output **bit-identical** (`==`) to `processBlock`'s — structural,
  because S5.1 has one body;
  plus the edge cases `numSamples = 1` × 100 000 and a single `numSamples = 8192` call.

**Implement:** plan S5.1 (the guard ladder in exactly that order; the `if (outL != inL)` skip rather
than relying on `std::copy_n` with overlapping ranges, which is UB; `processBlock` delegating to
`processBlockTapped` so FR-063's bit-identity is structural), plan S5.2 steps (1)–(5), and plan S6's
`renderChunk` steps (0)–(3) and (5)–(9).

The order inside `updateControl()` is **normative** and each line has a reason recorded in S5.2:

1. `breath.processBlock(kControlChunkSamples)` — **always 64, never `chunk`**, or the breath rate
   becomes a function of the host's block size and SC-012 (a) fails.
2. `refreshBreath(i)`: `b = clamp(sanitise(breath.getCurrentValue(), 0), -1, 1)`;
   `breathGain = 1 + kBreathGainSpan(0.45) * breathDepth * b`, held constant across the chunk
   (three **virtual** calls per 64 samples, never per sample).
3. The FR-016 backstop gate, **edge-detected only** — `LinearRamp::setTarget` recomputes from the
   *remaining* distance, so a ramp retargeted every step never arrives and SC-020 (c) becomes
   unmeasurable.
4. The tracking law, retargeted **every** step (deliberately): `env = follower_.getCurrentValue()`,
   `if (!detail::isFinite(env)) { follower_.reset(); env = 0; }`,
   `trackedEnvNorm_ = clamp(env / trackReferenceRms_, 0, 1)`,
   `trackGainRamp_.setTarget((1 − a) + a · trackedEnvNorm_)`.
5. The S5.3 cutoff glide: one `cutoffGlide_.process()` and one `std::exp2` per control step, pushed
   to `lowpass_.setCutoff` only when `|applied − pushedCutoffHz_| > 1e-3 · pushedCutoffHz_`.
   `setLowpassCutoffHz` writes `cutoffGlide_.setTarget(std::log2(clamped))` and **never** touches the
   biquad directly — a direct `configure()` swaps `b0` by 3 orders while `z1_`/`z2_` hold old-pole
   state, which is exactly the step `ClickDetector`'s 5-σ test finds (SC-008, T014).

And in `renderChunk`, per sample: read **both** inputs before any write (aliasing, SC-012 (b));
advance both masters and all three oscillators **unconditionally**; advance **every** per-sample ramp
unconditionally; feed the follower `detail::isFinite(mono) ? mono : 0.0f` (S7.6 — one bit test, and
the whole reason SC-009 (c2) is reachable); then the chain in the FR-040 order and no other —
`sum → TwoPoleLP → SaturationProcessor::processSample → × trackGain → DCBlocker2` — then the
non-finite trap (`recoverNonFinite()`: follower, low-pass, saturator, blocker; it does **not** touch
the dry path, the clamp counter or `chainActive_`), then the **pre-wet-gain, pre-clamp** tap, then the
FR-054 clamp on `y * wg` **only** with `bumpClampCount()` saturating at `0xFFFFFFFF`, then the add of
the **same scalar** to both channels.

The tracking multiply sits **after** the saturator, not before: that is what makes the drive
level-independent (SC-004).

**Verify**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "SubharmonicEngine_*" 2>&1 | tail -10
```

**Done when:** both cases pass warning-free and the suite is green.

---

## Group I — Dormancy and the sleep edge

### T011 — `chainActive_`, the sleep edge, and SC-014

**Files to edit:** `dsp/include/krate/dsp/systems/subharmonic_engine.h`,
`dsp/tests/unit/systems/subharmonic_engine_test.cpp`.

**Test to write FIRST:** `TEST_CASE("SubharmonicEngine_Dormancy", "[subharmonic_engine]")` (SC-014).

* **(a)** All three tones at `kMinToneLevelDb`, ramps settled → output **bit-identical** (`==`) to a
  decorrelated stereo input over 10 s on both channels. Plus the asymmetric arm: wet gain at
  `kMinWetGainDb` with tones **awake** → also bit-identical, chain still running.
* **(b)** `isToneDormant` true for all three; false on the first sample after a level write above the
  floor, **within the ≤ 64-sample control latency** Q5 grants.
* **(c)** The four-step sequence with `trackingAmount` at its FR-033 default of **1.0** and a
  **≥ 2 s charging render** before sleep: output ≤ **−80 dBFS** over the first 500 ms after the wake.
  (At 0 the restored sub itself is ≈ −13 dBFS because the tones are generators; at 1.0 the tracking
  mute sits upstream of the blocker, whose un-reset state still rings ≈ −36.6 dBFS into the window.)
* **(c2) Mutation check — run once during implementation and recorded in compliance.** With the
  **two** sleep-edge lines `lowpass_.reset()` and `blocker_.reset()` removed, (c) must **fail**. It is
  deliberately a two-line mutation: `saturator_.reset()` clears no audio state reachable from
  `processSample` (plan S5.4, S14 C-12), so including it would over-claim what the check proves.
* **(c3) The sensor is NOT reset at the sleep edge** — FR-026's last sentence, and the natural
  mistake, since the other three chain stages are. Hold a **−30 dBFS** steady 55 Hz body throughout
  (below the −18 dBFS reference, so `envNorm ≈ 0.251` and is **unclamped**; at −12 dBFS the clamp at
  1.0 would hide a reset within ~35 ms and the check would be vacuous), drive the tones to
  `kMinToneLevelDb`, render 500 ms sampling `getTrackedEnvelope()` once per 64 samples: after the
  50 ms level ramp has parked, **every** sample stays within **1e-3** of a never-dormant twin fed
  byte-identical input, and within a measured **5e-3** of the pre-dormancy value (the follower's
  110 Hz ripple on a 55 Hz body is 3.25e-3 peak-to-peak; a 1e-3 absolute band fails on a correct build).
* **(d)** Two seeded instances, A dormant for **37 s**: (d1) `getToneBreathValue` agree within
  **1e-3**; (d2) A's own value moved by **> 0.05** across the dormancy on **≥ 2** tones (at 37 s the
  27 / 43 / 71 s breathers have moved 1.37 / 0.86 / 0.52 of a cycle); (d3) a 5 s post-wake render with
  all tones restored agrees within `render_fingerprint.h` tolerances.

**Implement:** plan S5.4 — `isToneDormant` (`levelRamp` target **and** current both exactly `0.0f`,
valid by construction because `toneLevelGain(-60)` returns a literal `0.0f` and `LinearRamp::process`
lands *on* its target), `allTonesDormant()`, the `chainActive_` **edge latch** in `updateControl()`
step (6) with the three resets in FR-026's order, and `renderChunk` step (4)'s skip
(`out = in`, `tap = 0`, `continue`). The follower is **never** reset here — it is the sensor and must
keep tracking the body while the subs sleep. Everything the plan lists as always-advancing keeps
advancing: both masters, all three oscillators, all eight per-sample ramps and the follower.

`chainActive_` is a **member**, not a predicate recomputed per chunk: FR-026's clear must fire exactly
once, on the edge, and re-running three `reset()` calls every chunk would erase the FR-025 CPU saving
SC-013 (c) gates.

**Verify:** build, run `"SubharmonicEngine_Dormancy"`, run the mutation, restore, run the suite.

**Done when:** all arms pass, the (c2) mutation result is recorded, and the suite is green.

---

## Group J — Routing, the tap, and the clamp's scope

### T012 — `setSubToMainEnabled`, SC-022, and the clamp-scope edge case

**Files to edit:** `dsp/include/krate/dsp/systems/subharmonic_engine.h`,
`dsp/tests/unit/systems/subharmonic_engine_test.cpp`.

**Tests to write FIRST:**

* `TEST_CASE("SubharmonicEngine_SubToMainRouting", "[subharmonic_engine]")` (SC-022, with (a) in the
  C-11 rewritten form) —
  * **(a1)** silent stereo input, flag `true`, after 100 ms of settling: `outL[i] == outR[i]` and
    `outL[i] == clamp(subTap[i] * wetGainLinear, ±kOutputClamp)` at **every** sample, bit-exactly,
    where `wetGainLinear = wetGain(getWetGainDb())`. Exact because a settled `LinearRamp` lands *on*
    its target and silent-in makes `out` the add itself.
  * **(a2)** with the decorrelated body of (b): `(outL[i] − inL[i])` and `(outR[i] − inR[i])` agree
    within `1e-6 · max(1, |in[i]|)`.
  * **(b)** flag `false` → main output **bit-identical** to the dry input on both channels and both
    entry points, while `subTap` is **bit-identical** to the flag-`true` render's `subTap`.
  * **(b2) the tap is PRE-wet-gain.** Render twice, identical but for `setWetGainDb(0.0f)` vs
    `setWetGainDb(-12.0f)`: `subTap` **bit-identical** between the two, while `out − in` drops by
    **12 dB ± 0.1 dB** in sub-band RMS. Without this arm an implementation that taps *after* the wet
    multiply is green everywhere (every other tap-reading criterion runs at 0 dB wet) and Phase 10
    inherits the wrong signal.
  * **(c)** toggling the flag mid-render adds nothing beyond its own step: `ClickDetector`
    (`ClickDetectorConfig{.sampleRate = 48000.0f}`) reports no detection outside a one-sample window
    of each toggle index, and at least one toggle step exceeds the detector threshold (non-vacuity).
  * **(d)** `isToneDormant`, `isToneInfrasonicFloored` and `getClampEngagementCount()` are unaffected
    by the flag's state.
* `TEST_CASE("SubharmonicEngine_ClampScope", "[subharmonic_engine]")` — **the only assertion in the
  phase that can fail if FR-054's clamp is mis-scoped.** Every other criterion reading
  `getClampEngagementCount()` asserts `== 0`, and every planned render keeps the sum well under
  `kOutputClamp = 4.0`, so an implementation clamping `in + sub` passes all of them. Fixture: subs at
  all defaults, flag `true`, dry input held at a constant **+6.0 linear** (≈ +15.6 dBFS) on both
  channels for 2 s and **−6.0** for 2 s. Discarding 100 ms either side of the step, assert
  (i) `|out[i]| > 5.0` at **every** sample — an implementation clamping the sum caps it at 4.0 and
  fails by 1.0 linear, while the true sub contribution at defaults is ≤ ~0.15;
  (ii) `getClampEngagementCount() == 0`;
  (iii) `outL[i] − inL[i] == outR[i] − inR[i]` exactly.

**Implement:** the `subToMainEnabled_` member and its setter/getter (FR-064, default `true`), and
`renderChunk` step (9)'s gate `const float add = subToMainEnabled_ ? sub : 0.0f;`. The tap stays
**independent** of the flag (FR-062) and stays **post-tracking** — a pre-tracking tap would read a
flat line at every body level and pass for a broken implementation (SC-002 measures the FR-032 law
through it).

**Verify:** build, run both cases, run the suite.

**Done when:** both cases pass warning-free and the suite is green.

---

## Group K — The tracking behaviour on the main TU

### T013 — SC-001 (a)–(c2), the free-running and release brackets

Test-only. **Falsification:** (c2) is itself the falsification — it separates the FR-031 pushed
release (800 ms) from the un-pushed shipped default (100 ms) by ~90 dB. Run the case once with
`applyDefaults()`'s `follower_.setReleaseTime(...)` line commented out and confirm it fails, then
restore.

**Files to edit:** `dsp/tests/unit/systems/subharmonic_engine_test.cpp` only.

**Test:** `TEST_CASE("SubharmonicEngine_TrackingSuppressesFreeRunning", "[subharmonic_engine]")`.

* **(a) No free-running boom.** Tracking 1.0 (default), all three levels at `kMaxToneLevelDb`, wet
  `kMaxWetGainDb`, `f = 55`, **silent** stereo input, 60 s at 48 kHz → peak `|out| ≤ dbToGain(-80)`.
  Mechanically exact: `envNorm = 0` puts the tracking ramp on exactly `0.0f`.
* **(b) Wake responsiveness.** 10 s silent, then a step to a −12 dBFS 55 Hz body. Steady state =
  sub-band (`< 200 Hz`) RMS of `out − in` over the last 1 s of a 10 s post-step render; the same
  quantity over `[step, step + 400 ms]` must be **≥ 50 %** of it, and no sample exceeds
  `kOutputClamp`. *(Prediction ≈ 100 %, per S14 C-10's corrected convention; the floor is 50 %.)*
* **(c) Release.** Body removed → sub-band RMS of `out − in` **< −80 dBFS within 5 s**.
* **(c2) The FR-031 release constant is actually pushed.** With the −12 dBFS body removed, the
  sub-band RMS of `out − in` over `[400 ms, 600 ms]` after removal stays **above −20 dB relative to
  the pre-removal steady state**. Arithmetic: squared-domain release τ = `800/2π` = 127.3 ms, so
  amplitude τ = 254.6 ms; `envNorm` is clamped at 1.0 until the envelope falls 6 dB (176 ms), giving
  ≈ **−11 dB** at 500 ms — 9 dB of margin. With the un-pushed 100 ms release (amplitude τ = 31.8 ms)
  the same window reads **below −100 dB**. *(The attack constant has no constructible audio bracket —
  S14 C-10 — and is bracketed on the read surface in T008's contract case instead.)*

**Verify:** build, run the case, run the falsification, restore, run the suite.

---

## Group L — Infrasonic handling and click freedom

### T014 — SC-020 (a)–(c) and SC-008

Test-only. **Falsification:** for SC-008, temporarily replace `setLowpassCutoffHz`'s glide with a
direct `lowpass_.setCutoff(clamped)` and confirm the cutoff arm reports detections; restore. For
SC-020, invert the fixture — at `f = 40` assert the tone is *audible* — and confirm it fails.

**Files to edit:** `dsp/tests/unit/systems/subharmonic_engine_test.cpp` only.

**Test 1:** `TEST_CASE("SubharmonicEngine_InfrasonicFloor", "[subharmonic_engine]")` (SC-020 (a)–(c);
(d) already landed in T009). Fixture: **Div4 alone** at `kMaxToneLevelDb`, tracking 0, wet 0 dB, and
**`setDriveDb(kMinDriveDb)` — pinned**, because at the FR-041 default of +3 dB the stage is
`tanh(1.4125 × 1.995) × 0.7079`, a peak of 0.703 rather than 1.995 (≈ 9 dB of gain reduction), and
the arithmetic below is only valid with the pin.

* **(a)** `f = 40` (Div4 = 10 Hz < 12) → `subTap` ≤ **−80 dBFS** over 10 s and
  `isToneInfrasonicFloored(Div4)` **true**.
* **(b)** `f = 55` (Div4 = 13.75 Hz) → `subTap` RMS **≥ −20 dBFS** and floored **false** — the pair
  that proves "attenuated, not muted". *(A +6 dB tone through the 18 Hz Bessel HP at 13.75 Hz sees
  ≈ −7.5 dB → RMS ≈ −4.5 dBFS: ~15.5 dB of margin.)*
* **(c)** Step `f = 55 ↔ 40` mid-render in both directions: click-free, and `getToneCurrentGain(Div4)`
  reaches its new target within **52 ms** of the write (50 ms ramp + ≤ 1.333 ms control latency +
  1 ms tolerance) and is **monotonic** once the ramp begins moving.

**Test 2:** `TEST_CASE("SubharmonicEngine_ClickFreedom", "[subharmonic_engine]")` (SC-008). 60 s,
steady −12 dBFS 55 Hz body, `ClickDetectorConfig{.sampleRate = 48000.0f, …}` — **the default is
44100 and must be overridden or every `timeSeconds` it reports is wrong.** Step, one at a time:

* every tone level `kMin ↔ kMax`;
* wet gain over the same range;
* tracking 0 ↔ 1;
* **the low-pass cutoff over its full range** (40 ↔ 2000 Hz — the arm the S5.3 glide exists for);
* the fundamental `50 ↔ 44 Hz` (Div4 12.5 → 11 Hz, crossing `f = 48`) and `26 ↔ 20 Hz` (Div2
  13 → 10 Hz, crossing `f = 24`).

**Zero detections.** Each floor arm additionally asserts `isToneInfrasonicFloored` **flipped in both
directions** — without that, a future change to `kMinToneHz` silently voids the arm. Excluded by
name, in a comment: `setToneWaveform`, `prepare()`.

**Verify:** build, run both cases, run both falsifications, restore, run the suite.

---

## Group M — Whole-render measurement on the main TU

### T015 — SC-006, SC-007, SC-011, SC-021

Test-only. **Falsification:** for SC-007 (a), temporarily add a per-channel `+0.001` offset to one
channel's add and confirm the side-energy assertion fails; for SC-011 (b), the two-different-seeds arm
is itself the falsification (it must **fail** the fingerprint comparison); for SC-021, the (b)
mutation is written into the case.

**Files to edit:** `dsp/tests/unit/systems/subharmonic_engine_test.cpp` only. Consumes T006's
`measureTruePeakDb` and `calculateCorrelation`.

**Test 1:** `TEST_CASE("SubharmonicEngine_TruePeakHeadroom", "[subharmonic_engine]")` (SC-006).
−6 dBFS 55 Hz body + −12 dBFS pink bed (`primitives/pink_noise_filter.h` over a seeded `Xorshift32`,
the `resonance_drift_network_perf_test.cpp` source), `f = 55`, all levels `kMaxToneLevelDb`, wet
`kMaxWetGainDb`, drive `kMaxDriveDb`, tracking default.
(a) `getClampEngagementCount() == 0` **and** `measureTruePeakDb(out) ≤ +9.5 dBTP`, value transcribed
into compliance. (b) **amended 2026-09-14 (spec D-16)** — through a default `TruePeakLimiter`
(ceiling −1 dB): every **sample** **≤ −0.9 dBFS** (the limiter's exact guarantee, and the spec's
figure), the residual **true** peak **≤ +0.5 dBTP**, and that true peak **no worse than +0.25 dB**
against a **dry control** (the same fixture input, no engine in the path, scaled to the engine
render's sample peak, through the same limiter). The earlier "≤ −0.9 dBTP" is **withdrawn**: this
limiter is zero-latency with an instantaneous attack, and its own unit test pins the inter-sample
slack at `ceil + 0.06` linear ≈ −0.43 dBTP on a pure sine (`true_peak_limiter_test.cpp:88-89`), so
−0.9 dBTP is unreachable for any input, engine or not. **`true_peak_limiter.h` is not modified.**
(c) at **default** tone levels and wet gain the limiter's minimum gain over the render stays **above
−6 dB**. `TruePeakLimiter` exposes no gain read surface and **no getter may be added to it**: keep an
**unlimited copy** of the render, run the limiter on the copy, and compute the per-sample gain as
`limited[i] / unlimited[i]` wherever `|unlimited[i]| > 1e-6`. The quotient is exact — the limiter's
last act is one **linked** gain applied to both channels (`true_peak_limiter.h:159-160`). Assert
`min(gain) > dbToGain(-6)` and transcribe the measured minimum.

**Test 2:** `TEST_CASE("SubharmonicEngine_MonoCompatibility", "[subharmonic_engine]")` (SC-007).
Decorrelated stereo input (independent seeded pink per channel + a common 55 Hz body), default tone
levels; `d = out − in` per channel.
(a) side energy of `d` **≤ −100 dB** relative to its mid energy, `mid = 0.5(dL+dR)`,
`side = 0.5(dL−dR)`; (a2) full-output L/R correlation with subs active **≥** the same render with
`setWetGainDb(kMinWetGainDb)`, within `1e-6`; (b) `0.5(dL+dR)` retains **≥ 99 %** of `d`'s `< 200 Hz`
energy relative to the mean of `dL`, `dR` (`spectral_analysis.h:207 sumBinPower`); (c) each output
channel's DC over a 30 s render **≤ 1e-4** in magnitude.

**Test 3:** `TEST_CASE("SubharmonicEngine_Determinism", "[subharmonic_engine]")` (SC-011).
Two instances, same `PrepareConfig`, same seed, same setter sequence, 120 s → `compareFingerprints`
within `kSampleTolerance` / `kMetricTolerance`. (b) different seeds → the comparison **fails**, and at
`t = 60 s` at least **two of the three** `getToneBreathValue` differ by **> 0.01** in absolute value.
*(Only reachable because of `kDefaultBreathIrregularity = 0.25` — at the modulator's shipped
irregularity of 0 the RNG is never drawn and two seeds render identically. If the arm misses, the
pre-authorised lever is to raise that constant — never to lower 0.01 or extend the 60 s horizon.)*
No bit-exact goldens anywhere.

**Test 4:** `TEST_CASE("SubharmonicEngine_DefaultSubToBodyRatio", "[subharmonic_engine]")` (SC-021).
All shipped defaults, steady −12 dBFS 55 Hz sine on both channels, 10 s, first 2 s discarded, body RMS
and `subTap` RMS each measured over the final 5 s, ratio transcribed.
(a) ratio in **[−12, −6] dB** *(plan's arithmetic: tone gains 0.1259/0.0631/0.0316 → LP at 120 Hz
≈ unity → `tanh` at drive 3 dB ≈ ×0.97 → Bessel HP at 27.5/13.75/36.67 Hz = 0.788/0.42/0.878 →
`trackGain = 1`; sub RMS ≈ −22.7 dBFS against a body RMS of ≈ −15.0 dBFS → **≈ −7.7 dB**)*.
(b) mutation arm with the pre-Q3 defaults `-6 / -12 / -18 dB` restored: predicted **≈ +4.5 dB**, i.e.
above 0 dB, recorded — the arm that shows the criterion has teeth.

**Verify:** build, run the four cases, run the falsifications, restore, run the suite.

---

## Group N — The spectral criteria: dividers

### T016 — The `IsolatedSub` fixture, SC-003 and SC-004

Test-only. **Falsification:** SC-003's `getToneFrequencyHz`-vs-spectrum cross-check is itself the
falsification (the two are computed by different routes); for SC-004, run one arm with
`setDriveDb(kMaxDriveDb)` and confirm the THD ceiling is breached, then restore the pin.

**Files to edit:** `dsp/tests/unit/systems/subharmonic_engine_spectral_test.cpp` only.

First, code the shared `IsolatedSub` fixture in an anonymous namespace exactly as the header of this
file states it (48 kHz, `maxBlockSamples = 512`, tracking 0, wet 0 dB, LP `kMaxLowpassHz`, drive
`kMinDriveDb`, one tone at −20 dBFS with the other two at `kMinToneLevelDb`, breath depth 0, silent
input, `subTap` measured, first 2 s discarded, RMS gate above −60 dBFS). `-20 dBFS` is the tone level
for a reason: FR-041's `tanh` is in circuit even at drive 0 and its own relative third harmonic is
≈ `A²/24` = 0.042 % at `A = 0.1`, two orders below SC-004's ceiling. Note in a comment that the two
"silent" tones' oscillators still run (FR-025's deviation), so the engine is **not** dormant here and
the chain is live — that is intended: the fixture measures the chain.

**Test 1:** `TEST_CASE("SubharmonicEngine_DividerFrequencyAccuracy", "[subharmonic_engine]")`
(SC-003). One tone at a time, `Sine`, the per-tone sweeps Div2 `{55, 110, 220}`, Div4
`{110, 220, 440}`, FifthBelow `{55, 110, 220}` Hz; `estimatePeakFrequencyHz` over 262 144 samples.
(a) within **±0.5 %** of target **and** `getToneFrequencyHz` within the same tolerance of the
estimate; (b) `FifthBelow` within **±2 cents** of `f · 2/3` at every swept fundamental — the arm that
proves FR-012's `4f/3`-master construction rather than "some low tone appeared"; (c) the helper
validated on synthetic 27.5 / 36.67 / 110 Hz sines to **0.005 Hz** first (T006's self-check case runs
in the same TU and must precede this one in declaration order).

**Test 2:** `TEST_CASE("SubharmonicEngine_DividerTHD", "[subharmonic_engine]")` (SC-004). Same fixture
and sweeps, `measureLowFrequencyThdPercent(..., maxHarmonic = 10)`.
(a) **≤ 2.0 %** for every tone at every fundamental, **every value transcribed** into compliance;
(b) **amended 2026-09-14 (spec D-15)** — the **FR-042-corrected** THD is **flat within each tone's
own** descending sweep (220→110→55 for Div2/Fifth; 440→220→110 for Div4): peak-to-peak spread
**≤ 4 % of the sweep mean**, with the **raw** spread required to **exceed 5 %** so the correction
cannot become the identity; never compared across tones. The earlier "monotonically non-increasing"
form is **withdrawn** — measured, the raw figure *rises* as `f` descends on all three tones and the
whole rise is the FR-042 tilt the correction removes. A **negative sentinel FAILS** the case.
The 2 % ceiling is derived, not guessed, and is **untouched** — plan S4.2's table bounds the Sine
phase-reset *step* at 1.44 % (Div2 @ 220 Hz), 1.44 % (Div4 @ 440 Hz) and 1.92 % (Fifth @ 220 Hz); it
is an upper bound, not a prediction, and measured the reset contributes ≈ 0.007 % while FR-041's
`tanh` supplies ≈ 100 % of what is measured. **If measurement disagrees with the ceiling, stop and
surface**: the pre-authorised lever is D-4's rejected alternative (a private `PhaseAccumulator` +
`std::sin` for the Sine path), **never** a relaxed ceiling.

**Verify:** build, run both cases (they are minutes-long — capture to a log on the first run), run the
falsification, restore, run the suite.

---

## Group O — The spectral criteria: tracking law and the Square path

### T017 — SC-002 and SC-005

Test-only. **Falsification:** for SC-002, run the sweep with `setTrackingAmount(0.0f)` and confirm
arm (a)'s unity slope fails; for SC-005 (b), prepare a `SubOscillator` against an **unprepared**
`MinBlepTable` in a scratch fixture and confirm the RMS assertion catches the resulting silence.

**Files to edit:** `dsp/tests/unit/systems/subharmonic_engine_spectral_test.cpp` only.

**Test 1:** `TEST_CASE("SubharmonicEngine_TrackingLaw", "[subharmonic_engine]")` (SC-002). The
`IsolatedSub` fixture **except** its RMS gate: SC-002 opts out and uses a two-part **anchor gate**
instead, because the unconditional −60 dBFS floor fails a *correct* implementation at the −60 dBFS
body (predicted tap −64.5 dBFS, and suppressing that point would remove the unity-slope region the
criterion exists to measure):

* at the **top** of the sweep (−6 dBFS body, `envNorm` clamped to 1) the tap RMS must be **above
  −40 dBFS** — the "engine is alive" assertion, ~17 dB of margin against the −22.5 dBFS prediction;
* every lower step must read **above −75 dBFS** — an absolute sanity floor ~10 dB under the lowest
  predicted point.

Configuration: tracking 1.0 (the quantity under test), all three tones at their FR-020 defaults,
breath depths 0, `f = 55`, input a **mono-identical** 55 Hz sine held 8 s per step with the last 4 s
measured. Sweep body RMS **−60 / −48 / −36 / −24 / −18 / −12 / −6 dBFS**; measure `subTap` RMS.
(a) unity slope in dB, **±1.0 dB**, across −60…−24 (the −18 point is already past the knee — the
RMS sensor over-reads a steady sine by ≈ 1.9 dB, so the knee sits at reference − 1.9 dB; ruled
2026-09-13); (b) flat **±0.5 dB** across −18…−6; (c) with
tracking 0, flat **±0.5 dB** across the whole sweep; (d) `getTrackingGain()` agrees with
`(1−a) + a·getTrackedEnvelope()` within **max(1e-4, 2 % of predicted)** (the sensor's ≈ 1.5 % 2f
ripple is averaged by the ramp, not by the getter; ruled 2026-09-13); (e) the knee lies at reference − 1.9 dB (the
recorded sensor bias) **±1.0 dB** at both the default reference and `setTrackReferenceDb(-30)`, and
moves by **−12 dB ±1.0 dB** between them (the −24 point becomes flat at the −30 reference). The follower is
RMS-mode, so a sine of peak `A` reads `A/√2` — compute the body's actual RMS from the rendered buffer
rather than assuming a dBFS convention. Every measured point is transcribed into compliance.

**Test 2:** `TEST_CASE("SubharmonicEngine_SquareSpectrum", "[subharmonic_engine]")` (SC-005).
`IsolatedSub`, **Div2 alone**, `SubWaveform::Square`, `f = 110` (sub = 55 Hz), drive `kMinDriveDb`,
LP `kMaxLowpassHz`, 262 144-point Hann frame.
(a) every peak returned by `findSpectralPeaks(..., -40.0f)` lies within **±1 bin** of an integer
multiple of 55 Hz. **The peak-selection rule is part of the criterion** (T006's `kPeakExclusionBins`
greedy descending rule); without it, a plain local-maxima scan reports the Hann window's own first
sidelobe and fails a correct implementation, while a global-maximum scan cannot fail at all. T006's
self-check (exactly one peak on a synthetic 55 Hz sine) must precede this case in the TU.
(b) Square-path `subTap` RMS **above −40 dBFS** — the assertion that proves the shared `MinBlepTable`
is prepared, because an unprepared table makes the tone **silent** rather than un-BLEPped (predicted
≈ −20.5 dBFS at the fixture's −20 dB level: 20 dB of margin). (c) note in the case that the `tanh`'s
own odd harmonics of a single tone are integer multiples and are admitted by (a) by construction —
not attributed to the divider.

**Verify:** build, run both cases (capture to a log), run the falsifications, restore, run the suite.

---

## Group P — The long soak and the fault-injection TU (disjoint TUs)

T018 owns the spectral TU; T019 owns the nonfinite TU. Fully disjoint — both `[P]`.

### T018 [P] — SC-017 (A) + (B), long-render stationarity

Test-only. **Falsification:** run arm (B) with the FR-016 backstop gate forced to 1.0 and a
fundamental of 20 Hz, and confirm the trend/peak-to-peak assertions can fail; restore.

**Files to edit:** `dsp/tests/unit/systems/subharmonic_engine_spectral_test.cpp` only.

**Test:** `TEST_CASE("SubharmonicEngine_LongRenderStationarity", "[subharmonic_engine][long]")`.
Two **10-minute** arms at 48 kHz:

* **(A)** all defaults + a steady −12 dBFS 55 Hz body;
* **(B)** tracking 0, all levels `kMaxToneLevelDb`, drive `kMaxDriveDb`, wet `kMaxWetGainDb`, all
  three breath depths 1.0, LP `kMaxLowpassHz`, same body.

Both: (a) per-minute sub-band (`< 200 Hz`) RMS varies **≤ 3 dB** peak-to-peak; (b) the linear trend of
that RMS is **≤ 0.3 dB/minute** in magnitude; (c) **no non-finite sample**; (d) clamp count `== 0` on
(A), **transcribed** on (B).

**Mandatory:** render in 512-sample blocks and accumulate the per-minute statistics incrementally — a
10-minute stereo buffer is 230 MB and must never be materialised.

**Verify** (explicitly; the per-push CI filter excludes `[long]`):

```bash
build/windows-x64-release/bin/Release/dsp_systems_tests.exe \
  "SubharmonicEngine_LongRenderStationarity" 2>&1 | tee /tmp/p6-long.log | tail -5
```

---

### T019 [P] — The probe definition and SC-009

**Files to edit:** `dsp/tests/unit/systems/subharmonic_engine_nonfinite_test.cpp` only. This TU is the
**sole definition** of `detail::SubharmonicEngineNonFiniteProbe` and the **only** TU in this phase
built with `-fno-fast-math` (registered in T001).

Define the probe with **exactly two** operations, both reaching private state (plan S7.5) — a probe
that can write any member is a second, untested API:

* `injectChainNonFinite(SubharmonicEngine&, float nonFinite)` — pushes one bit-pattern non-finite
  value directly through `blocker_.process()` so `y1_`/`y2_` are poisoned before the next render;
* `readChainHealth(const SubharmonicEngine&)` — returns the follower's current value and the
  blocker's `y1_`.

**Test:** `TEST_CASE("SubharmonicEngine_NonFinite", "[subharmonic_engine]")`. Non-finite values are
built from bit patterns `0x7FC00000`, `0x7F800000`, `0xFF800000` through a `volatile` sink (the
`resonance_drift_network_nonfinite_test.cpp:149-155` idiom) — **never** `std::numeric_limits`.

* **(a)** every float setter rejects NaN and ±Inf with the **previous value standing**, verified
  through the getter: `setFundamentalHz`, `setToneLevelDb`, `setToneBreathRate`,
  `setToneBreathDepth`, `setTrackingAmount`, `setTrackReferenceDb`, `setFollowerAttackMs`,
  `setFollowerReleaseMs`, `setLowpassCutoffHz`, `setDriveDb`, `setWetGainDb`. *(Rejection, not
  clamping: `std::clamp` returns NaN unchanged, and a NaN reaching `masterUnison_.increment` poisons
  `masterPhaseEstimate_`, which `SubOscillator` never guards.)*
* **(b)** the probe injects into the blocker → the output is **finite within one sample** and the
  engine renders correct audio thereafter.
* **(c)** a 30 s adversarial parameter sweep with a **non-finite input buffer for the middle 10 s**:
  (c1) `subTap` finite at **every** sample of the whole render; (c2) main output finite from the first
  finite input sample, and the **final 10 s matches a finite-input reference render** within
  `render_fingerprint.h` tolerances; (c3) `getClampEngagementCount() == 0`.
  **(c2) is the criterion S7.6's guard exists to make reachable** — without the two follower guards
  the follower is poisoned, `trackGainRamp_.setTarget(NaN)` silently **mutes** the ramp to zero with
  no counter, and the comparison fails by a wide margin. A finite *output* during the non-finite
  window is deliberately **not** asserted: FR-050 is an add, and sanitising a host's audio behind its
  back hides the host's bug.

**Falsification:** comment out the per-sample `detail::isFinite(mono)` guard in `renderChunk` step (3)
and confirm (c2) fails; restore.

**Verify:** build, run `"SubharmonicEngine_NonFinite"`, run the falsification, restore, run the suite.

---

## Group Q — The CPU gate

### T020 — Perf arms (i)–(n), the SC-013 gates, and the checked-in baselines

**Files to edit:** `dsp/tests/unit/systems/subharmonic_engine_perf_test.cpp` only.

Add to T004's probe, on the same measurement basis (best-of-25 × 500 blocks after 400 warm-up,
512-sample blocks at 48 kHz):

| Arm | Workload |
|---|---|
| (i) | the whole engine at defaults (`f = 55`, all three tones awake) with a live body — **SC-013 (a)'s gated arm** |
| (j) | the whole engine, all three tones `Square` — **SC-013 (b)'s gated arm** |
| (k) | the whole engine dormant (FR-025's skip) — **SC-013 (c)** and OQ-1's cheap-voice term |
| (n) | the whole engine at `setFundamentalHz(40.0f)` — Div4 floored, two tones awake — SC-013 (d), reported not gated |
| (l) | **one** instance at defaults — OQ-1's global-placement arm |
| (m) | **eight** instances at defaults, all rendered per block — OQ-1's per-voice-placement arm |

Arms (l) and (m) are separate measurements, **not** `(i) × 8`: eight instances share L1/L2 and the
scaling is the thing being measured.

**Gates** — `TEST_CASE("SubharmonicEngine_CpuBudget", "[subharmonic_engine][.perf]")`:

* **(a)** arm (i) **≤ 53 333 ns/block**. The percent figure is reported, never asserted.
* **(b)** arm (j) gated at the **same** ceiling. `Square` is a shipped configuration and roadmap line
  321's budget is unqualified; if the arm misses, stop-and-surface and the *waveform option* is
  reconsidered — never the budget.
* **(c)** arm (k) is **at least 15 %** cheaper than arm (i), with the absolute ns saving transcribed.
  The margin is tied to the ~14 % session-to-session drift the perf idiom records
  (`resonance_drift_network_perf_test.cpp:78-90`). If the saving is real but below 15 %,
  stop-and-surface and reconsider what FR-025 skips; **do not lower the margin.**
* **(d)** arm (n) measured and reported, not separately gated.

Pin the measured figures as checked-in baselines carried as `static_assert`s, so the absolute ceiling
is still evaluated on every CI leg even though `[.perf]` is excluded from the per-push filter.

**Verify** (alone, nothing else executing — no build, no clang-tidy, no second suite, no parallel
agent; the runner settles 20 s between suites):

```bash
node tools/run-cpu-tests.js dsp_systems_tests 2>&1 | tee /tmp/p6-perf.log
```

A test that flips verdicts between runs is measuring the machine, not the code: confirm nothing else
was running, re-run that suite alone after the machine has idled, and only then treat it as a defect.

**Done when:** all four gates pass with their figures transcribed, or the stop-and-surface table is on
the record with a user ruling.

---

## Group R — The Open Question 4 ruling

### T021 — FR-076 / SC-018: fill the placement table, present it, and stop

**Files:** none edited. Output goes into the phase's compliance record.

From T020's isolated run, transcribe arms **(k)**, **(l)** and **(m)** as ns per 512-sample block at
48 kHz, then fill plan S12.3's table:

| Placement | Instances | Cost/block | % of one core | Fits the roadmap envelope? |
|---|---|---|---|---|
| Global (post-voice-sum) | 1 | arm (l) | (l)/10 666 667 | global budget, not per-voice |
| Per-voice, 4 voices | 4 | ≈ 4 × arm (l) | | 4 × per-voice line |
| Per-voice, 6 voices | 6 | | | |
| Per-voice, 8 voices | 8 | arm (m) measured | | |
| Per-voice, 8 voices, 6 dormant | 8 | 2·(l) + 6·(k) | | the case OQ-1 says makes per-voice affordable |

Set the percentages beside the roadmap's 4–5 % per-voice envelope (roadmap line 92) **net of what
phases 2, 3 and 5 have already spent — 1.75 % + 0.75 % + 1.5 % = 4.0 %** — i.e. the per-voice
placement has ≈ 0.5–1.0 % of headroom left and a 0.5 % component fits only at the top of that range.

**Then present the table to the user and STOP.** The ruling on roadmap Open Question 4 (global
post-voice-sum vs per-voice) is the **user's**, and the phase is not complete until both the table and
the recorded ruling with its reasoning are in the compliance record. The ruling binds Phase 10's
wiring, not this component's code — the component is placement-agnostic by construction (pitch enters
through one scalar, the body as ordinary stereo audio, and FR-035's settable reference is what makes a
per-voice calibration possible).

---

## Group S — Integration gates

### T022 — Registration-completeness audit

**Files:** none edited (audit only).

* All four TUs appear **exactly once** in `dsp/tests/CMakeLists.txt`'s `dsp_systems_tests` list,
  between the Phase-5 block and the closing `)`.
* `unit/systems/subharmonic_engine_nonfinite_test.cpp` appears in the `-fno-fast-math` block and in
  **no other** `set_source_files_properties()` call (in particular not the `-O2` block).
* `dsp/lint_all_headers.cpp` includes `<krate/dsp/systems/subharmonic_engine.h>` exactly once.
* `tests/test_helpers/low_frequency_metrics.h` exists and no `tests/test_helpers/CMakeLists.txt` edit
  was made (INTERFACE library, no source list).
* Every `TEST_CASE` named in this file exists and runs: compare
  `dsp_systems_tests.exe "SubharmonicEngine_*" --list-tests` against the list in plan S10.2 and this
  document, naming any that are missing.

---

### T023 — Full-suite run and the SC-016 shared-component check

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target \
  dsp_core_tests dsp_primitives_tests dsp_processors_tests dsp_systems_tests
for t in dsp_core_tests dsp_primitives_tests dsp_processors_tests dsp_systems_tests; do \
  build/windows-x64-release/bin/Release/$t.exe 2>&1 | tail -3; done
```

All four green, including `sub_oscillator_test.cpp` inside `dsp_processors_tests` and the
`seraphis_*` cases inside `dsp_systems_tests` (they consume the same shipped headers).

Then the **byte-unchanged** check — this must print **nothing** for nine headers; `sub_oscillator.h`
shows only the appended `advance()` (ruled 2026-09-14):

```bash
git diff --stat -- dsp/include/krate/dsp/processors/sub_oscillator.h \
  dsp/include/krate/dsp/primitives/minblep_table.h \
  dsp/include/krate/dsp/processors/envelope_follower.h \
  dsp/include/krate/dsp/primitives/two_pole_lp.h \
  dsp/include/krate/dsp/processors/saturation_processor.h \
  dsp/include/krate/dsp/primitives/dc_blocker.h \
  dsp/include/krate/dsp/processors/breathing_modulator.h \
  dsp/include/krate/dsp/primitives/smoother.h \
  dsp/include/krate/dsp/core/phase_utils.h \
  dsp/include/krate/dsp/core/random.h
```

Also run the `[long]` set explicitly if T018 has not been re-run since the last header change.

---

### T024 — Portability, lints, clang-tidy (SC-015)

```bash
node tools/lint-layers.js
node tools/lint-odr.js
node tools/lint-nonfinite-symbols.js
node tools/lint-float-bit-goldens.js
node tools/lint-simd-aligned-loadstore.js
node tools/check-portability.js
clang-tidy -p build/windows-ninja dsp/tests/unit/systems/subharmonic_engine_test.cpp
```

All clean; transcribe the evidence into compliance. A green Windows build proves nothing about the
Linux/macOS legs — `check-portability` is the gate that catches narrowing in brace init, `std::isnan`
under `-ffast-math`, and the MSVC leniencies GCC/Clang reject. For any residual Linux doubt, use the
WSL g++ probe rather than waiting on CI. On Windows use the single-TU `clang-tidy` invocation above
for a small change set, or `./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja` for
a tree; never the `.sh` (it globs headers and runs serially for an hour+).

---

## Open items the build carries (each has a named owner above)

* **OQ-1 / roadmap Open Question 4** — the placement ruling. Owner: **T021**. The build stage presents
  the measured table and **stops**; the ruling is the user's and the phase is incomplete without it.
* **S14 C-10 — FR-031's follower constants.** Owner: **T002** (surface only). `120 / 800 ms` behave as
  τ = 19.1 / 127.3 ms, a faster follower than FR-031's own rationale describes. The defaults stay as
  specified unless the user rules otherwise; if they want the stated behaviour the values become
  ~750 / 5000 ms and SC-001 (c2)'s window moves with them (T013).
* **SC-013 (c)'s 15 % dormancy margin** and **SC-004's 2 % THD ceiling** — the two thresholds most
  likely to be tested by measurement. Owners: **T020** and **T016**. Both have a written
  stop-and-surface response; **neither may be moved.**
* **Plan S12.4's levers** — if the engine total misses the budget, lever 1 (replacing the
  `SaturationProcessor` stage with a direct `Sigmoid::tanh`) is a **departure from FR-041** and must
  be raised as a spec question, never taken silently. Owner: **T005** (early warning), **T020**
  (the gate).
