# Tasks: Vorago Phase 5 — Feedback Ecology

**Spec:** `specs/vorago-phase5-feedback-ecology/spec.md` (1799 lines)
**Plan:** `specs/vorago-phase5-feedback-ecology/plan.md` (3125 lines, the **OQ-1 revision** — the loop
resonator is a plain `Biquad` fed directly-computed RBJ coefficients, **not** a `SmoothedBiquad`; every
`SmoothedBiquad` decision from any earlier revision is dead)
**Deliverable:** one new Layer 3 header `dsp/include/krate/dsp/systems/feedback_ecology.h`
(header-only), four new test TUs under `dsp/tests/unit/systems/`, one new test-helper header
`tests/test_helpers/coherence.h`, two edits in `dsp/tests/CMakeLists.txt`, one include line in
`dsp/lint_all_headers.cpp`, and the bounded documentation write-back OQ-3 + S16 C-4…C-9 schedule.
**Test target:** `dsp_systems_tests` (all four new TUs).
**Regression set that must stay green (FR-090, SC-020):** `dsp_primitives_tests`,
`dsp_processors_tests`, `dsp_systems_tests`.
**Plugin work:** none. Vorago's plugin starts at Phase 11; phases 1–10 are KrateDSP-only.
**Shipped components amended: none.** `filter_feedback_matrix.h`, `feedback_network.h`,
`flexible_feedback_network.h`, `i_feedback_processor.h`, `multimode_filter.h`, `resonator_bank.h`,
`svf.h`, `biquad.h`, `dc_blocker.h`, `crossfading_delay_line.h`, `delay_line.h`,
`envelope_follower.h`, `brownian_drift.h` and `smoother.h` must be **byte-unchanged** at the end of
the phase (FR-090, FR-092, SC-020).

---

## How to read this file

* Tasks are grouped into **ordered groups**. A group starts only when every task in the previous group
  is green: it builds warning-free, its own cases pass, and `dsp_systems_tests` still passes.
* `[P]` marks tasks that are parallel-safe **within their group**: their file sets are fully disjoint
  and consist of new files only. **Every task that edits
  `dsp/include/krate/dsp/systems/feedback_ecology.h` is unmarked and sits in its own group** — that
  header is this phase's single shared file. So are `dsp/tests/CMakeLists.txt` and each of the four
  test TUs; only one task per group may touch any one of them.
* Each task is **self-contained**: exact files to create/edit, the failing test to write **first**
  (TU, `TEST_CASE` name, the numeric assertions), then the implementation intent with the plan section
  that carries the code shape, then the command that verifies it. An executor needs nothing beyond
  the spec/plan sections each task cites.
* Canonical order inside every task: **write the failing test → watch it fail → implement → zero
  compiler warnings → the test passes → `dsp_systems_tests` still passes.**
* No commit tasks. Commits happen outside this workflow.

### Three deliberate deviations from the requested layout, each forced by the repo

**1. CMake registration is T001, not a final task.** `dsp/tests/CMakeLists.txt`'s `dsp_systems_tests`
source list is **enumerated, not globbed** — verified this session: `add_executable(dsp_systems_tests`
at `dsp/tests/CMakeLists.txt:324`, the list closes with `)` at `:436`, immediately after the Vorago
Phase-3 block at `:421-435`. An unregistered TU compiles into nothing and its cases silently never
run, so registering all four TUs up front is the only ordering under which every later task's "run the
suite" step proves anything (plan R-13). The final group still carries a
**registration-completeness audit** (T021) plus the full-suite run (T022) and the
portability/lint gate (T023).

**2. Measurement precedes implementation.** Plan S14.1 and FR-080 require the ten-stage stage-cost
probe to run **before the component is written**, because two realisation decisions (D-1: `SVF` vs
`MultimodeFilter`; OQ-1's realisation: direct `Biquad` vs a single-slot `ResonatorBank`) are taken
from its table, and two more (levers 4 and 5) are user decisions under the stop-and-surface rule.
Groups B and C are therefore probe-only, with no component.

**3. `dsp/lint_all_headers.cpp`'s include lands with the header, not before it.** That file is
compiled by `add_library(dsp_lint_stub OBJECT lint_all_headers.cpp)` (`dsp/CMakeLists.txt:212`), so an
include of a header that does not yet exist breaks the whole build. It is edited inside T006, in the
same task that creates the header.

### Build and run commands (Windows; the full CMake path is mandatory)

```bash
CMAKE="C:/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "FeedbackEcology_*" 2>&1 | tail -20
```

* Catch2 filters: the test-case name as a **positional** argument (`"FeedbackEcology_Dormancy"`),
  tags in `[brackets]`. Never `-c` — that filters SECTIONs.
* `[long]` cases run locally by default and are excluded from the per-push CI filter
  `~[performance]~[perf]~[benchmark]~[!benchmark]~[long]`.
* **Timing runs alone**: `node tools/run-cpu-tests.js dsp_systems_tests`, nothing else executing — no
  build, no clang-tidy, no second suite, no parallel agent.
* Capture slow runs (`[long]`, perf, clang-tidy) to a log file on the **first** run and read the log.
  Never re-run a suite just to grep its output.

### Conventions every task inherits

* Namespace `Krate::DSP`; classes PascalCase, methods camelCase, members trailing underscore,
  constants `kPascalCase`. Header banner comment `// Layer: 3 (Systems)`.
* Every new case is tagged `[feedback_ecology]`, plus `[long]` or `[.perf]` where the task says so:
  `TEST_CASE("FeedbackEcology_Xxx", "[feedback_ecology]")`.
* Finiteness is `Krate::DSP::detail::isFinite` / `isNaN` / `isInf` (`core/db_utils.h:118`, `:99`,
  `:260`) — **never** `std::isnan`/`std::isinf`/`std::isfinite`, in the header **or** in any test
  (`tools/lint-nonfinite-symbols.js` gates it).
* **Non-finite values may be named in exactly one TU**,
  `dsp/tests/unit/systems/feedback_ecology_nonfinite_test.cpp`, and there they are built from bit
  patterns through a volatile sink — never `std::numeric_limits<float>::quiet_NaN()`/`infinity()`,
  which fold to finite garbage on the macOS/Linux `-ffast-math` legs. Transcribable idiom:
  `dsp/tests/unit/systems/resonance_drift_network_nonfinite_test.cpp` (`makeNonFinite(bits)`;
  patterns `0x7FC00000`, `0x7F800000`, `0xFF800000`).
* **No bit-exact float goldens** anywhere (`tools/lint-float-bit-goldens.js`). Where a render must be
  pinned use `tests/test_helpers/render_fingerprint.h` (`kSampleTolerance = 5.0e-4f`,
  `kMetricTolerance = 2.5e-4`, `compareFingerprints`). The only exact comparisons allowed in this
  phase are the four structural ones: SC-016's tapped/untapped bit-identity, SC-024 (b)/(c)/(d)'s
  same-code-path identities, SC-018 (a)'s `mix == 0` dry pass-through, and SC-002 (a)'s `== 0.0f` on
  an undriven tap.
* Brace-initialised aggregates use **designated initialisers**
  (`FeedbackEcology::PrepareConfig{.maxBlockSamples = 512, .numLoops = 5}`) — Clang errors on
  narrowing where MSVC does not.
* Tests include `tests/test_helpers/allocation_detector.h` only; **never**
  `allocation_operator_overrides.h` — `dsp_systems_tests` already has its single owner and a second
  include is a duplicate-symbol link error.
* **Streaming statistics are mandatory in every `[long]` case**: a materialised 30-minute stereo
  render is 86.4 M samples/channel ≈ 691 MB. Accumulate peak / RMS / per-window RMS / finiteness
  block by block and keep at most one analysis window in memory.
* Every `ClickDetector` fixture sets `ClickDetectorConfig::sampleRate` to the **render rate** — the
  struct's default is `44100.0f` (`tests/test_helpers/artifact_detection.h:38`), which at a 48 kHz
  render mis-reports every `timeSeconds` it returns.
* **Zero compiler warnings is part of every task's definition of done**, not a later cleanup.

### The reference patch and the reference drive (plan S12)

* **Reference patch:** `numLoops = 6`, all loops awake, the FR-013/FR-022/FR-033/FR-052/FR-053 default
  tables, wander on at `kDefaultWanderRateHz = 0.03f`, governor at its defaults, `mix = 1.0` (so a
  criterion measures the wet path, not the crossfade), `wetGain = 0 dB`, fixed seed. Provide it as one
  fixture helper per TU: `makeReference(fs = 48000.0, loops = 6, seed = 0x5EEDu)`.
* **Reference drive:** white noise at **−12 dBFS RMS** (not peak — the two differ by 10–12 dB for
  white noise and every governor assertion turns on which is meant), fixed `Xorshift32` seed, both
  channels.
* **`configure → reset() → render` is mandatory wherever an arm asserts on a configuration being in
  force from sample 0.** `prepare()` snaps every ramp and smoother to the **default** tables, so a
  setter called after it *glides* (`setLoopInputGain(i, 0.0f)` takes `kMixRampMs = 20 ms`;
  `setCoupling(..., 0.0f)` takes `kCouplingSmoothMs = 20 ms`) and the energy injected in those 20 ms
  then circulates at `kDefaultLoopGain = 0.72` for seconds. `reset()` snaps all of it and clears the
  audio in one call (plan S3.2).

---

## Group A — Registration, documentation write-back, ODR

### T001 — Create the four TU stubs and register them (`dsp/tests/CMakeLists.txt`)

**Files to create**

* `dsp/tests/unit/systems/feedback_ecology_test.cpp`
* `dsp/tests/unit/systems/feedback_ecology_spectral_test.cpp`
* `dsp/tests/unit/systems/feedback_ecology_perf_test.cpp`
* `dsp/tests/unit/systems/feedback_ecology_nonfinite_test.cpp`

Each stub is `#include <catch2/catch_test_macros.hpp>` plus a one-line comment naming the criteria it
will own (plan S12.2). No `TEST_CASE` yet except in the perf TU (T004 owns it).

**Files to edit**

* `dsp/tests/CMakeLists.txt`, the `dsp_systems_tests` source list — append immediately **after** the
  Phase-3 block ending at `:435` and **before** the closing `)` at `:436`, using the Phase-3 comment
  shape verbatim (plan S13 edit 1):

  ```cmake
      # Vorago Phase 5 (specs/vorago-phase5-feedback-ecology): FeedbackEcology.
      # This list is ENUMERATED, not globbed - an unregistered TU silently drops
      # out of the build and its cases never run.
      #   feedback_ecology_test.cpp           SC-005, SC-006, SC-007, SC-008, SC-009,
      #                                       SC-010, SC-011, SC-013, SC-014, SC-015,
      #                                       SC-016, SC-017, SC-018, SC-022, SC-023,
      #                                       SC-024, SC-025
      #   feedback_ecology_spectral_test.cpp  SC-001, SC-002, SC-003, SC-021   (the [long] set)
      #   feedback_ecology_perf_test.cpp      SC-004 (a)-(f) + the FR-080 stage probe   [.perf]
      #   feedback_ecology_nonfinite_test.cpp SC-012 only
      unit/systems/feedback_ecology_test.cpp
      unit/systems/feedback_ecology_spectral_test.cpp
      unit/systems/feedback_ecology_perf_test.cpp
      unit/systems/feedback_ecology_nonfinite_test.cpp
  ```

* `dsp/tests/CMakeLists.txt`, the `-fno-fast-math` block (opens `:528`
  `if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")`, Phase-3 entry `:838`, Phase-4 entry `:852`,
  `PROPERTIES` at `:853`) — add **exactly one** of the four TUs, after the Phase-4 entry, with the
  comment from plan S13 edit 2 explaining why the other three stay out (their guards must be proved in
  the `/fp:fast` + `-ffast-math` mode the header actually ships in, and `-fno-fast-math` would move
  the perf figures the baselines are pinned to):

  ```cmake
          unit/systems/feedback_ecology_nonfinite_test.cpp
  ```

  A file may appear in only one `set_source_files_properties()` call — this TU must **not** also
  appear in the `-O2` block below it (`:862`).

**Test first:** not applicable — this task creates the harness the later tests live in. Its own
falsification is the build: before the edit the four TUs are invisible to CMake.

**Verify**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe 2>&1 | tail -5
```

Links and passes unchanged (no new cases yet). Grep the generated build files or the CMake
configure output to confirm all four TU paths appear exactly once.

---

### T002 [P] — Documentation write-back (plan T0: OQ-3 + S16 C-4…C-9)

**Files to edit**

1. `specs/Vorago-roadmap.md` lines 272–273 — amend the micro-loop bullet:
   * "filter (`MultimodeFilter`)" → "filter (`SVF`)";
   * "resonator (single `IResonator` mode)" → "resonator (one RBJ bandpass, the `ResonatorBank`
     slot's Q range)".
   Phase-4 precedent: amend the roadmap rather than carry deviations.
2. `specs/vorago-phase5-feedback-ecology/spec.md`, Traceability table — delete the two ratified
   `DEVIATION` annotations for roadmap line 272: the `SVF` row (`spec.md:1685`) and the `Biquad` row
   (`spec.md:1687`). The **other two** deviation notes stay: line 278's pre-summed input
   (`spec.md:1695`) and the Dormancy row's FR-063 exception — neither is written back to the roadmap.
3. `spec.md` SC-019 (`:1477`) — drop the case name `FeedbackEcology_StaticGates`. Every clause of
   SC-019 is a `node tools/lint-*.js` / `check-portability.js` invocation; nothing a runtime test can
   observe. It is discharged by T023's transcript (S16 C-4).
4. `spec.md` SC-002's fixture description — add the one-line `configure → reset() → render` rule that
   makes its `== 0.0f` reachable on a correct build (S16 C-5).
5. `spec.md` Success Criteria + Traceability — **add SC-023, SC-024, SC-025** exactly as plan S12.3
   specifies them, and give each a Traceability row (SC-023 → FR-013; SC-024 → FR-003; SC-025 →
   FR-050…FR-056) (S16 C-6).
6. `spec.md` FR-071 — add `getLoopAppliedCoupling(from, to)` to the enumerated read surface
   (S16 C-7).
7. `spec.md` FR-019 — amend to state the split: `prepare()`/`reset()` keep the O(buffer)
   `delay.reset()` wipe (control-thread calls); the two **audio-thread** callers (the FR-063 sleep
   edge and the FR-047 trap) use an O(1) read-mute window of exactly one delay length instead
   (S16 C-8, plan S3.1).
8. `spec.md` FR-001 — add `primitives/delay_line.h` to the include enumeration (for
   `nextPowerOf2`, `delay_line.h:26`) and remove `core/audio_constants.h` (no user remains)
   (S16 C-9).

**Test first:** not applicable — documentation. The falsification is the checklist below.

**Verify**

* `grep -n "MultimodeFilter\|IResonator" specs/Vorago-roadmap.md` returns nothing on lines 272–273.
* `grep -c "DEVIATION" specs/vorago-phase5-feedback-ecology/spec.md` returns **2**, not 4.
* `grep -n "SC-025" spec.md` hits both the Success Criteria list and the Traceability table.
* `grep -n "FeedbackEcology_StaticGates" spec.md` returns nothing.
* `grep -n "getLoopAppliedCoupling" spec.md` hits FR-071.

Nothing in `dsp/` may cite any of these edits before this task lands.

---

### T003 [P] — Re-run the ODR and near-name sweeps, and record the transcript

**Files:** none (verification only; the transcript belongs in the build log).

Run verbatim from the repo root:

```bash
grep -rn "class FeedbackEcology"                 dsp/ plugins/
grep -rn "class MicroLoop\|class EcologyLoop\|class FeedbackLoop"  dsp/ plugins/
grep -rn "class EnergyGovernor\|class LoopMatrix\|class CouplingMatrix" dsp/ plugins/
grep -rn "FeedbackEcologyNonFiniteProbe"         dsp/ plugins/ tools/
grep -rni "ecology"                              dsp/ plugins/ tools/
ls dsp/include/krate/dsp/systems/feedback_ecology.h
```

**Expected:** zero hits everywhere except `class CouplingMatrix` →
`plugins/membrum/src/dsp/coupling_matrix.h:21` in `namespace Membrum` (no collision), and the `ls`
reporting no such file. If any other sweep returns a hit, **stop** — the class name is taken and the
naming decision has to be re-made before T006 writes a line.

**Verify:** the transcript above, plus `node tools/lint-odr.js` exits 0 on the current tree (the
baseline the later runs are compared against).

---

## Group B — Measure before writing (plan S14.1, FR-080, SC-004 (e))

### T004 — The FR-080 stage-cost probe, with no component in existence

**File to edit:** `dsp/tests/unit/systems/feedback_ecology_perf_test.cpp` (the T001 stub).

**Test first — this task IS the test.** One case,
`TEST_CASE("FeedbackEcology_StageCostProbe", "[feedback_ecology][.perf]")`. It is a **probe, not a
gate**: it `REQUIRE`s only that every measured figure is **finite and strictly positive** (a zero or a
NaN means the measurement is broken, which is the one thing that would make the table lie) and emits
the table through `WARN` so it lands in the transcript.

Measurement basis, inherited verbatim from
`dsp/tests/unit/systems/resonance_drift_network_perf_test.cpp:59-84`: **nanoseconds per 512-sample
block at 48 kHz**, best-of-25 trials × 500 blocks after 400 warm-up blocks. One block period is
**10 666 667 ns**; the roadmap's 1 %/voice ceiling is **106 666 ns/block**.

Stages, each measured in its loop position, six instances unless stated:

| # | stage | shape | decides |
|---|---|---|---|
| (a) | `SVF::process` × 6 | smoothing enabled, one `setCutoff` per 64 samples | D-1, against (b) |
| (b) | `MultimodeFilter::processSample` × 6 | the roadmap's named filter; note `processSample` (`multimode_filter.h:204`) calls `updateCoefficientsFromSmoothed()` at `:218` **every sample** | D-1 |
| (c) | `CrossfadingDelayLine::process` × 6 | 520 ms lines, one `setDelayMs` per 64 samples | the delay term |
| (d) | `Biquad::process` × 6 | fed directly-computed RBJ coefficients (plan S4.1) | OQ-1 realisation, against (g) |
| (e) | `DCBlocker::process` × 6 | | |
| (f) | `std::tanh` × 6 **and** `FastMath::fastTanh` × 6 (`core/fast_math.h:65`) | | OQ-2 lever 1 |
| (g) | six single-slot `ResonatorBank`s, one enabled slot each | | OQ-1 realisation, against (d) |
| (h) | `EnvelopeFollower` RMS tracker | one `processSample` per sample | the governor term |
| (i) | twelve `BrownianDrift` lanes | `processBlock(64)` at decimation 1, 2 and 17 | FR-055's cost |
| (j) | the per-sample ramp bank | **fifteen** `LinearRamp::process` per sample: `mix`, wet trim, 6 input gains, 6 gates, governor, `normGain` | the fixed overhead |

**Implementation intent:** the probe instantiates the shipped primitives directly — it must **not**
reference `FeedbackEcology`, which does not exist yet. Print each stage's ns/block plus the sum of
(a)+(c)+(d)+(e)+(f-`std::tanh`)+(h)+(i)+(j) as a first-order projection against the **71 111 ns**
gated baseline (`kBaseline × kRegressionFactor(1.5) <= 106 666`).

**Verify:** `"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests`,
then the case builds and is *excluded* from a plain suite run (`[.perf]`). Do **not** time it here —
T005 owns the isolated run.

---

## Group C — Run the probe, take the realisation decisions

### T005 — Run the probe alone, record the table, decide D-1 and the OQ-1 realisation

**Files:** none edited. Output is the recorded table (paste it into the build transcript and into
plan S14.1 as the measured row set).

```bash
node tools/run-cpu-tests.js dsp_systems_tests 2>&1 | tee /tmp/fe_stage_probe.log
```

Nothing else may be running — no build, no clang-tidy, no second suite, no parallel agent. Sustained
benchmarking heats the CPU and figures drift ~14 % across a session.

**Decisions this task must record, each citing a measured figure:**

1. **D-1** — `SVF` (a) vs `MultimodeFilter` (b) for the loop filter. FR-011 already specifies `SVF`;
   this measurement is the evidence, not a re-opening. If (b) somehow beat (a), **stop and surface**
   rather than switching: `MultimodeFilter::process(float*, size_t)`'s cheap path updates coefficients
   once per block and is structurally unusable inside a feedback loop.
2. **OQ-1 realisation** — direct `Biquad` (d) vs six single-slot `ResonatorBank`s (g). D-3 predicts
   (g) loses: a `ResonatorBank` costs three `OnePoleSmoother` advances plus a 16-iteration loop with
   15 `continue`s per sample to reach one enabled biquad.
3. **OQ-2 lever 1** — whether `std::tanh` × 6 fits. The plan's first-order estimate predicts it does
   **not** (the whole-component budget is 71 111 ns / 512 samples = **139 ns per sample**), and
   `FastMath::fastTanh` is pre-authorised: it returns exactly ±1 beyond ±3.5 and is within 0.05 %
   below, so FR-042's bound is preserved exactly. If lever 1 is taken, T020 adds its error-bound test.

**Stop-and-surface rule, non-negotiable (inherited from
`resonance_drift_network_perf_test.cpp:59-65`):** no implementing agent may lower `kMaxLoops`, raise
the budget, relax a threshold or shrink a workload to make a figure fit. Reduce cost, never move the
line. Levers 4 (drop the resonator to a second `SVF`) and 5 (default `numLoops` 6 → 5) are **user**
decisions taken from the surfaced table.

**Verify:** the log contains a finite, strictly positive figure for every stage, and the three
decisions above are written down with the numbers that justify them.

---

## Group D — The header skeleton

### T006 — `feedback_ecology.h`: includes, constants, nested types, state, API, probe friend

**Files to create:** `dsp/include/krate/dsp/systems/feedback_ecology.h`
**Files to edit:** `dsp/lint_all_headers.cpp` — after the Phase-3 include at `:182`, before the
`// Layer 4: Effects` block at `:184`:

```cpp
// Vorago Phase 5 (specs/vorago-phase5-feedback-ecology), FR-001
#include <krate/dsp/systems/feedback_ecology.h>
```

**Test first:** one compile-level case in `feedback_ecology_test.cpp`,
`TEST_CASE("FeedbackEcology_ConstantsTable", "[feedback_ecology]")`, asserting the S1.2 constants
exist with the specified values so a later "tidy-up" cannot move one silently:

* `FeedbackEcology::kMaxLoops == 6`; `kControlChunkSamples == 64`; `kMaxLaneDecimation == 17`.
* `kMaxLoopGain == 0.90f`; `kDefaultLoopGain == 0.72f`; `kMaxCouplingPerPair == 0.5f`;
  `kDefaultCoupling == 0.04f`; `kMaxTotalLoopGain == 0.95f`; `kOutputClamp == 4.0f`.
* `kMinDelayMs == 10.0f`; `kMaxDelayMs == 500.0f`; `kMaxDelaySeconds == 0.52f`;
  `kCrossfadeMs == 20.0f`.
* `kDefaultLoopDelayMs == {41, 67, 109, 173, 281, 449}`;
  `kDefaultLoopCutoffHz == {2400, 1700, 1200, 850, 600, 420}`;
  `kDefaultLoopResonanceHz == {1200, 850, 600, 425, 300, 210}`;
  `kDefaultDelayWanderFraction == {0.16, 0.10, 0.06, 0.04, 0.03, 0.02}`.
* `kDefaultResonanceRt60 == 1.0f`; `kDefaultFilterQ == SVF::kButterworthQ`;
  `kMaxFilterQ == SVF::kButterworthQ`; `kMinFilterQ == SVF::kMinQ`.
* Governor: `kDefaultGovernorThresholdDb == -52.0f` (amended at T012 by SC-006's own
  re-measurement protocol; it read `-6.0f` here and `kMinGovernorThresholdDb` read `-36.0f`, both
  sized from the input level rather than from FR-043's tracker — see the header's DERIVATION
  TABLE 3), `kDefaultGovernorRatio == 8.0f`,
  `kGovernorMinGain == 0.05f`, `kGovernorAttackMs == 20.0f`, `kGovernorReleaseMs == 800.0f`.
* Wander: `kDefaultWanderRateHz == 0.03f`, `kMinWanderRateHz == 0.002f`, `kMaxWanderRateHz == 1.0f`,
  `kMaxDelayWanderFraction == 0.5f`, `kMaxCutoffWanderOctaves == 4.0f`,
  `kDefaultCutoffWanderOctaves == 0.5f`.
* Ramps: `kGainRampMs == 50.0f`, `kMixRampMs == 20.0f`, `kCouplingSmoothMs == 20.0f`,
  `kGovernorRampMs == 20.0f`. Output: `kDefaultMix == 0.15f`, `kDefaultWetGainDb == 0.0f`,
  `kMinWetGainDb == -24.0f`, `kMaxWetGainDb == 24.0f`. Life cycle:
  `kWakeSilenceEpsilon == 1.0e-6f`, `kMinUsableSampleRate == 8000.0`,
  `kConstructionSampleRate == 48000.0`.

**Implementation intent (plan S1.1–S1.6):**

* Include list exactly as S1.1: `core/db_utils.h`, `core/math_constants.h`, `core/random.h`,
  `primitives/biquad.h`, `primitives/crossfading_delay_line.h`, `primitives/dc_blocker.h`,
  `primitives/delay_line.h` (for `nextPowerOf2`), `primitives/smoother.h`, `primitives/svf.h`,
  `processors/brownian_drift.h`, `processors/envelope_follower.h`, `processors/resonator_bank.h`
  (**`rt60ToQ` and the namespace constants only** — the class is never instantiated), plus
  `<algorithm> <array> <cmath> <cstddef> <cstdint>`. **No Layer 3 or Layer 4 include** — in particular
  not `filter_feedback_matrix.h`, `feedback_network.h`, `flexible_feedback_network.h`. The header must
  carry the reason: the bar is `FilterFeedbackMatrix`'s capacity `static_assert(N >= 2 && N <= 4)`
  (`filter_feedback_matrix.h:72-73`), not the layer.
* All S1.2 constants with the **two derivation tables written into the header as comments** (the
  `kDefaultDelayWanderFraction` σ_Δ table and the per-centre realised-RT60 table), plus the four
  `static_assert`s: the control-grid pin, the `kMaxLaneDecimation × kTauMax >= 500` reach, and the two
  clamp-pair ordering asserts at `kMinUsableSampleRate` (`kMaxResonatorFrequencyRatio × 8000 > 20`
  and `SVF::kMaxCutoffRatio × 8000 > kMinCutoffHz`), plus
  `kConstructionSampleRate >= kMinUsableSampleRate` and the two `EnvelopeFollower` range asserts on
  the governor attack/release.
* Nested `enum class FilterMode : std::uint8_t { Lowpass = 0, Bandpass = 1, Highpass = 2 }`
  (**APPEND ONLY** — it becomes a persisted plugin parameter at Phase 12) and nested `PrepareConfig`
  (`maxBlockSamples = 2048`, `numLoops = kMaxLoops`, designated initialisers mandatory).
* Private nested `struct Loop` and the whole S1.5 member list, **including** the construction-seeded
  `sampleRate_ = kConstructionSampleRate`, `maxCutoffHz_` and `maxResonanceHz_` (never `0.0f` — an
  inverted `std::clamp` pair is UB that MSVC's `_STL_VERIFY` traps, R-8) and the invariant comment
  that any later getter clamping against a rate-derived bound must be audited against this rule.
* The full S1.4 public API **declared** with stub bodies (returning the documented neutral / doing
  nothing). Every method `noexcept`; every getter `[[nodiscard]]`. No virtual dispatch anywhere;
  `BrownianDrift` is held by concrete type.
* S1.6 salt table (`kSaltDelayLane = 0`, `kSaltCutoffLane = 16`, `kSaltNextFree = 32`) with both
  overlap `static_assert`s, marked **APPEND ONLY**.
* `namespace detail { struct FeedbackEcologyNonFiniteProbe; }` before the class and
  `friend struct detail::FeedbackEcologyNonFiniteProbe;` as the first line of the private section —
  declared, **never defined** by the library (`seraphis_engine.h:181-196` pattern).

**Verify**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "FeedbackEcology_ConstantsTable" 2>&1 | tail -5
node tools/lint-layers.js && node tools/lint-odr.js && node tools/lint-nonfinite-symbols.js
```

Zero warnings. The whole-tree build must also succeed, which is what proves the
`lint_all_headers.cpp` include compiles.

---

## Group E — The control surface, the FR-009 contract, and the resonator coefficients

### T007 — Every setter and getter, `updateResonator()`, and the truthful RT60 getter

**Files to edit:** `dsp/include/krate/dsp/systems/feedback_ecology.h`,
`dsp/tests/unit/systems/feedback_ecology_test.cpp`.

**Test first — `TEST_CASE("FeedbackEcology_ControlSurfaceClamps", "[feedback_ecology]")` (SC-017):**

* *Clamping.* Every float setter driven to **±10× its range** reports the clamped value through its
  getter: `setLoopGain(0, 9.0f)` → `getLoopGain(0) == kMaxLoopGain (0.90f)`; `setLoopGain(0, -5.0f)`
  → `kMinLoopGain (0.0f)`; `setCoupling(0, 1, 5.0f)` → `getCoupling(0,1) == kMaxCouplingPerPair`;
  `setLoopFilterQ(0, 30.0f)` → `getLoopFilterQ(0) == SVF::kButterworthQ` (FR-012's rung-1 ceiling);
  `setLoopDelayMs(0, 5000.0f)` → `500.0f`; `setLoopDelayMs(0, 0.1f)` → `10.0f`;
  `setWetGain(100.0f)` → `24.0f`; `setMix(-1.0f)` → `0.0f`; `setGovernorRatio(1000.0f)` → `20.0f`;
  `setGovernorThresholdDb(10.0f)` → `0.0f`; `setLoopDelayWander(0, 5.0f)` → `0.5f`;
  `setLoopCutoffWander(0, 40.0f)` → `4.0f`; `setNumLoops(0)` → `getNumLoops() == 1`;
  `setNumLoops(99)` → `6`.
* *Out-of-range index.* Every index-taking setter at `kMaxLoops`, `kMaxLoops + 1` and `SIZE_MAX` is a
  **silent no-op**; every index-taking getter at those indices returns the documented neutral
  (`0.0f`, `0`, `false`, `FilterMode::Lowpass`) **without indexing the array** — run this arm under
  ASan.
* *Diagonal.* `setCoupling(2, 2, 0.4f)` is a no-op and `getCoupling(2, 2) == 0.0f`;
  `setCouplingMatrix` applies the same per-entry rule and ignores the diagonal, and one non-finite
  entry leaves only that pair's previous value standing.
* *Unprepared state (S9).* On a **default-constructed, never-prepared** instance every configuration
  getter returns its post-construction default: `getNumLoops() == 6`, `getMix() == kDefaultMix`,
  `getLoopDelayMs(0) == 41.0f`, `getLoopCutoffHz(3) == 850.0f`, `getLoopGain(i) == 0.72f`,
  `getCoupling` reads FR-033's default ring. **Exactly two exceptions:**
  `getAllocatedBytes() == 0` and `isPrepared() == false`.
* *The seeded-bounds arm (not optional).* `getLoopResonanceRt60(i)` on that same never-prepared
  instance returns the realised table to within **1e-3 s**:
  `{0.1833, 0.2587, 0.3665, 0.5174, 0.7330, 1.0000}`. With a `0.0f` initialiser for
  `maxResonanceHz_` this call is `std::clamp(1200.0f, 20.0f, 0.0f)` — UB, and MSVC's `<algorithm>`
  traps it with `_STL_VERIFY`. **Run this arm under an MSVC Debug build as well as under ASan**, since
  the defect it guards is a trap rather than a wrong value and a Release run can sail past it.
* *The no-`prepared_`-gate arm.* On an unprepared instance `setMix(0.42f)`, `setLoopGain(2, 0.5f)`,
  `setLoopDelayMs(3, 200.0f)` and `setCoupling(0, 1, 0.3f)` are each honoured by their getter
  immediately (FR-006, FR-009 — there is no fourth rule and no `prepared_` gate).
* *Non-finite rejection* is proved in the non-finite TU (T016), not here — this TU must never name a
  non-finite value.
* *Constexpr-log equivalence.* The `detail::constexprLn(x) / detail::kLn2` log2 constants used by the
  cutoff mapping agree with `std::log2` at runtime to within **1e-6**. `constexpr std::log2` is a
  GCC/MSVC builtin extension **Clang rejects**; this arm pins the series.

**Test first — `TEST_CASE("FeedbackEcology_ResonatorRing", "[feedback_ecology]")`, arm (c) only
(SC-023 (c), table half):** for every loop, `getLoopResonanceRt60(i)` reproduces the six-row realised
table above to within `1e-3 s` on a prepared instance at 48 kHz, and
`setLoopResonanceRt60(5, 2.0f)` still reports **1.0000 s** (`rt60ToQ(210, 2.0) = 191.0`, clamped to
`kMaxResonatorQ = 100`). Arms (a) and (b) — the **measured** ring — need the render path and are
written in T015; leave a `SECTION` placeholder naming them so the criterion is not lost.

**Implementation intent (plan S4, S8.6, S9, S11):**

* Every float setter opens with `if (!detail::isFinite(v)) return;`, then clamps, then stores, then
  drives its FR-076 mechanism. Every index-taking method opens with
  `if (loop >= kMaxLoops) return <neutral>;` **before** touching the array.
* `updateResonator(i)` (S4.1) computes the RBJ constant-peak-gain bandpass **directly** —
  `f = clamp(resonanceHz, kMinResonatorFrequency, maxResonanceHz_)`,
  `q = clamp(rt60ToQ(f, rt60Req), kMinResonatorQ, kMaxResonatorQ)`, store `appliedResonanceQ = q`,
  `omega = kTwoPi·f/fs`, `alpha = sin(omega)/(2q)`, `a0 = 1+alpha`, `b0 = alpha/a0`, `b1 = 0`,
  `b2 = -alpha/a0`, `a1 = -2cos(omega)/a0`, `a2 = (1-alpha)/a0` — and writes them with
  `Biquad::setCoefficients` (`biquad.h:325`). **Never** `Biquad::configure` and never
  `SmoothedBiquad`: both route through `BiquadCoefficients::calculate`, which clamps `Q` at
  `biquad.h`'s `kMaxQ = 30` (`:53`, `:673`) and silently undercuts the `kMaxResonatorQ = 100` ceiling
  FR-013 specifies. Retunes are a **stepped hard swap** (FR-076) — the centre and RT60 are rare
  user-set controls, not wander targets.
* `getLoopResonanceRt60(i)` returns `(appliedResonanceQ · kLn1000) / (kPi · f)` — derived from the
  **applied** Q, never the request, and deliberately **not** re-clamped into
  `[kMinDecayTime, kMaxDecayTime]` on the way out (a second clamp restores the lie the getter exists
  to prevent).
* `setLoopCutoffHz` caches `baseLog2Cutoff` so the control step costs one `exp2` and **no**
  `std::log2`.
* The read surface follows S9: `getLoopCurrentCutoffHz(i)` is `svf.getCutoff()` (the last **commanded**
  value); `getLoopAppliedCoupling(from, to)` returns `0.0f` for an out-of-range index or `from == to`
  without indexing.
* `applyDefaults()` (needed by the constructor, T008 re-uses it) writes the four default tables, the
  FR-033 coupling ring (`kDefaultCoupling` on cyclic neighbours, `0.0f` elsewhere), `kDefaultLoopGain`,
  `kDefaultLoopInputGain`, `kDefaultMix`, `kDefaultWetGainDb`, the governor threshold/ratio and
  `wake = 1 / dormant = false`. It must **not** touch `PrepareConfig` fields or any audio-object
  state. The user-provided default constructor's body is `applyDefaults();` followed by
  `updateResonator(i)` for every loop at `kConstructionSampleRate` — without the latter,
  `appliedResonanceQ` sits at its `kMinResonatorQ = 0.1` initialiser and the unprepared RT60 arm reads
  0.0003 s.

**Verify**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "FeedbackEcology_*" 2>&1 | tail -10
```

Plus one MSVC **Debug** run of `FeedbackEcology_ControlSurfaceClamps` for the seeded-bounds arm.

---

## Group F — Lifecycle and the memory contract

### T008 — `prepare()`, `reset()`, `setSeed()`, `clearLoopAudio()`, `getAllocatedBytes()`

**Files to edit:** `feedback_ecology.h`, `feedback_ecology_test.cpp`.

**Test first — `TEST_CASE("FeedbackEcology_Footprint", "[feedback_ecology]")` (SC-008):**
`getAllocatedBytes()` equals, **exactly**:

| sample rate | bytes |
|---|---|
| 44 100 | **786 432** |
| 48 000 | **786 432** |
| 96 000 | **1 572 864** |
| 192 000 | **3 145 728** |

(44.1 and 48 kHz coincide because both land in the same 32 768-sample power-of-two bucket — assert the
equality, do **not** write an inequality between them.) Identical for `maxBlockSamples` of 64, 512 and
8192 and for `numLoops` of 1 and 6 (the multiplier is `kMaxLoops`, not `numLoops`: all six lines are
prepared regardless because `setNumLoops` must not allocate). **0** before `prepare()`.

**Test first — `TEST_CASE("FeedbackEcology_SampleRate", "[feedback_ecology]")`, the two
lifecycle arms (SC-011, remaining arms in T015):**

* *Degenerate rates.* `prepare()` at **1 Hz**, at **0**, at a **negative** rate and at a **NaN** rate
  each leave a usable object whose `getSampleRate() >= kMinUsableSampleRate (8000.0)`, and rendering
  512 samples of the reference drive through it produces finite output on both channels.
* *Re-`prepare()` restores defaults (FR-005 (c)).* From a prepared reference patch, set every
  off-diagonal coupling to `kMaxCouplingPerPair` and every loop gain to `kMinLoopGain`, then call
  `prepare()` again at the same rate. Assert `getCoupling(from, to)` reads back FR-033's default ring
  (`0.04f` on cyclic neighbours, `0.0f` elsewhere) and `getLoopGain(i) == kDefaultLoopGain (0.72f)`.
  Then assert the complement: the same configuration followed by **`reset()`** is **preserved**
  (`getCoupling` still `0.5f`, `getLoopGain` still `0.0f`).

**Implementation intent (plan S2, S3, S10):**

* `prepare()` is the **only** allocating method, and implements S2's **15 numbered steps** with each
  comment stating *what breaks if the step moves*. Three orderings are load-bearing and must be
  called out by name in the header:
  * **(a)** each `delay.setCrossfadeTime(kCrossfadeMs)` comes **after** that line's `prepare()`,
    because `CrossfadingDelayLine::prepare` sets `sampleRate_` (`:101`) and then overwrites the
    crossfade time with its own default (`:119`);
  * **(b)** `setSeed(seed_)` is **last** (step 14), because `BrownianDrift::setSeed` reseeds the RNG
    and the mandatory per-lane `reset()` snaps the output smoother;
  * **(c)** `applyDefaults()` (step 7) runs on **every** call, including a re-`prepare()` on a live
    object, discarding the caller's configuration. `prepare()` is not idempotent with respect to
    configuration; only `reset()` is.
* Step 1 floors the rate at `kMinUsableSampleRate = 8000.0` and sanitises a non-finite request to
  48 000 — **not** 1 Hz, because `[kMinResonatorFrequency, 0.45·fs]` inverts below ~44 Hz and
  `std::clamp` with `hi < lo` is UB MSVC traps.
* Step 3 **re-caches** `maxCutoffHz_` / `maxResonanceHz_` (they are seeded at construction, never
  `0.0f`). Step 6 configures the follower: `DetectionMode::RMS`, attack 20 ms, release 800 ms,
  `setSidechainEnabled(false)` — the governor **must** see the sub content, which is Vorago's
  identity. Step 9 calls `setWanderRate(kDefaultWanderRateHz)`, the **single owner** of
  `laneDecimation_` and of every lane's `setSmoothness`. Step 11 configures the 42 control-rate
  `OnePoleSmoother`s at **`fs / kControlChunkSamples`** and snaps them; step 12 configures the
  per-sample `LinearRamp`s at **`fs`** and snaps them. Step 15's `snapControlState()` evaluates both
  lane mappings once and writes them with `snapToDelayMs` + `setCutoff`/`snapToTarget`, so the first
  control step does not open every loop with a spurious crossfade from tap position 0.
* **`clearLoopAudio(i)` is FR-019's ONE owner and is O(1)** (plan S3.1): `svf.reset()`,
  `resonator.reset()`, `dcBlocker.reset()`, then the delay half as
  `const float d = delay.getCurrentDelaySamples(); delay.snapToDelaySamples(d);
  readMuteSamples = size_t(std::ceil(d)) + 1u;` plus `prevY_[i] = prevOut_[i] = 0.0f;` and
  `lastCrossfading = false`. **No `DelayLine::reset()`** — that is a `std::fill` over 131 072 B per
  loop at 48 kHz (524 288 B at 192 kHz) and two of the four callers are audio-thread paths, where a
  six-loop drop can fire six of them inside one 64-sample control chunk against an **8 889 ns** chunk
  budget. Exactly four callers: `prepare()` step 13, `reset()`, the FR-063 sleep edge (T013) and the
  FR-047 trap (T009).
* `prepare()` and `reset()` **additionally** call `delay.reset()` and set `readMuteSamples = 0`:
  they are control-thread calls, and a literally zeroed buffer is what SC-009 (b)'s reproducibility
  needs.
* `reset()` follows S3.2's seven steps and preserves configuration; it re-derives every lane stream
  via `setSeed(seed_)`, which is the one line that makes a render reproducible from the top.
* `setSeed()` (S3.3) derives twelve streams via `deriveStreamSeed(seed_, salt)` and calls each lane's
  `reset()` **after** `setSeed` (setSeed reseeds the RNG but does not rewind the walk). The
  guaranteed-non-zero result matters: `Xorshift32::seed(0)` silently substitutes its default, so two
  lanes hashing to 0 would collapse onto one stream.
* `allocatedBytes_ = kMaxLoops · nextPowerOf2(size_t(sampleRate_ · kMaxDelaySeconds) + 1) ·
  sizeof(float)`, computed once in step 13.

**Verify**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "FeedbackEcology_*" 2>&1 | tail -10
```

---

## Group G — The render path and the absolute control grid

### T009 — `processBlockTapped` / `processBlock`, the guard ladder, `renderChunk`, the output stage

**Files to edit:** `feedback_ecology.h`, `feedback_ecology_test.cpp`.

**Test first — `TEST_CASE("FeedbackEcology_EntryPointContract", "[feedback_ecology]")` (SC-024):**

* **(a) Null pointers write nothing and advance nothing.** For each of `inL`, `inR`, `outL`, `outR`
  passed as `nullptr` in turn, and all four at once: pre-fill both output buffers with the sentinel
  `-7.5f`, record `getLoopCurrentDelayMs(i)`, `getLoopGate(i)`, `getGovernorRms()` and
  `getLoopCrossfadeCount(i)` for all six loops, call `processBlock`, then assert the output buffers
  are **unchanged sample for sample** and every recorded value is unchanged.
* **(b) `numSamples == 0` consumes no control step.** Render 512 samples, call `processBlock(..., 0)`
  a hundred times, render another 512; the concatenation is **bit-identical** to an unbroken
  1 024-sample render on a second identically-configured instance. This is the only way to observe
  `controlPhase_`.
* **(c) An unprepared instance writes exactly `numSamples` zeros.** Default-constructed instance,
  outputs pre-filled with the sentinel beyond `numSamples`; `processBlock(..., 333)` leaves the first
  333 samples of both channels exactly `0.0f`, sample 333 onward still holding `-7.5f`, and every
  FR-071 getter unchanged.
* **(d) Both legal aliasings are bit-identical to the non-aliased render.** `inL == outL` with
  `inR == outR`, and the crossed pairing `inL == outR` with `inR == outL`, each 10 s, compared with
  exact `==` against a non-aliased reference. Same code path; any difference is a real divergence.

**Test first — `TEST_CASE("FeedbackEcology_BlockPartition", "[feedback_ecology]")` (SC-010):** the
same 60 s render produced with block sizes **512, 64, 7, 4096** and an irregular pseudo-random
partition, all within `render_fingerprint.h` tolerances (`kSampleTolerance = 5.0e-4f`,
`kMetricTolerance = 2.5e-4`). A block-relative control grid runs two control steps for a 36 + 28 split
where an unsplit 64 runs one, and fails here.

**Test first — `TEST_CASE("FeedbackEcology_TapEquivalence", "[feedback_ecology]")` (SC-016):** a 60 s
render through `processBlock` and the same render through `processBlockTapped` with all six taps
installed are **bit-identical** on both channels (exact `==`). Additionally,
`clamp(wetGain_linear × (Σ_i tap_i) / sqrt(numLoops), ±kOutputClamp)` — S6 step 5's block, **in that
order** — reproduces the wet output within `kSampleTolerance` (at `mix = 1` the wet output is the
output channel).

**Test first — `TEST_CASE("FeedbackEcology_DryIdentity", "[feedback_ecology]")` (SC-018):**

* **(a)** With `mix = 0.0f` settled, a 10 s stereo render is **bit-identical** to its input on both
  channels. The fixture **must include a `-0.0f` input sample**: `(1-0)·dry + 0·wet` is bit-exact for
  every finite dry *except* `-0.0f`, where `-0.0f + 0.0f` is `+0.0f`, which is why S6 step 5 takes an
  explicit `m == 0.0f` branch.
* **(b)** During that render `getGovernorRms()`, `getLoopCurrentDelayMs(i)` and `getLoopGate(i)` all
  change, and a subsequent `setMix(1.0f)` produces — after the crossfade settles — a wet signal within
  `render_fingerprint.h` tolerances of a reference that ran at `mix = 1` throughout. The loops were
  charged, not asleep.

**Test first — `TEST_CASE("FeedbackEcology_OutputClamp", "[feedback_ecology]")` (SC-013):**

* **(b) first, because (a) is worthless until (b) proves the counter is wired.** Reference patch,
  `mix = 1`, `setWetGain(+24.0f)` (×15.85), drive at 0 dBFS RMS for 10 s:
  `getClampEngagementCount() > 0`; every output sample inside `[-4.0f, +4.0f]`; every sample finite;
  `getNonFiniteResetCount() == 0`. Report the measured pre-clamp peak. If a build's wet path is too
  quiet to reach `4.0 / 15.85 = 0.2524`, the response is a louder drive or a longer render — **never**
  a lower `kOutputClamp`.
* **(a)** `getClampEngagementCount() == 0` on a representative `wetGain <= 0 dB` arm (60 s reference
  patch). With `wetGain <= 0 dB` the bound is structural — rung 2 caps the wet sum at
  `sqrt(6) = 2.4495` against a ceiling of 4.0 — so this is a defect detector, not a limiter.

**Implementation intent (plan S5.1, S6):**

* `processBlockTapped` carries FR-003's guard ladder in exactly this order: any null pointer → return,
  writing nothing and advancing nothing; `numSamples == 0` → return, consuming no control step;
  `!prepared_` → `std::fill_n` exactly `numSamples` zeros on both channels and no state advance.
  `processBlock` **forwards** to it with `loopTaps = nullptr` — one function body, which is what makes
  SC-016's bit-identity structural.
* The chunk loop keeps `controlPhase_` as an **absolute residue carried across calls**:
  `if (controlPhase_ == 0) updateControl();`,
  `chunk = min(numSamples - done, kControlChunkSamples - controlPhase_)`,
  `controlPhase_ = (controlPhase_ + chunk) % kControlChunkSamples`.
* `renderChunk` implements S6's six steps verbatim, and the seven notes there are decisions, not
  style: (0) sanitise `dryL`/`dryR` **per channel** before the mono sum — without it a non-finite
  input reaches `outL` through the crossfade at any `mix < 1`; (1) advance **every** per-sample ramp
  unconditionally for **all six** slots, dormant and out-of-count included, or SC-010 fails;
  (2) form **all** inputs from the previous sample before any loop's stages run — own feedback reads
  `prevY_` (**pre-gate**), cross-coupling reads `prevOut_` (**post-gate**); (3) run the chains, with
  the read-mute branch (`if (readMuteSamples != 0) { delay.write(sf); d = 0.0f; --readMuteSamples; }
  else d = delay.process(sf);`), the FR-047 non-finite trap on `b`, `bSum += b`,
  `y = tanh(b · govGain)` with **no loop-gain factor** (there is exactly one loop-gain factor per
  round trip and it lives in the input sum), `o = y · gate[i]`, `flushDenormal` on both stored
  previous-sample values; (4) feed the follower `normGain · bSum` **every sample**; (5) the output
  stage in normative order — `wet = wetTrim · (wetSum · normGain)`, `flushDenormal`, the ordered clamp
  with `++engagements`, then the mix crossfade with the explicit `m == 0.0f` branch; (6) the
  observation-only tap write, reading the already-computed `prevOut_[i]` after the loop.
* A skipped loop's `continue` does **no** stores: `prevY_[i]`/`prevOut_[i]` are already exactly `0.0f`
  from the sleep-edge clear, which is what makes FR-062 true by construction and buys SC-004 (c)'s
  dormant saving.
* `updateControl()` may remain a stub in this task **except** for the parts already implemented; leave
  a `// T010/T011/T012 fill these steps` marker so the ordering in S5.2 is not invented later.

**Verify**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "FeedbackEcology_*" 2>&1 | tail -10
```

---

## Group H — The life-modulation lanes and the two mappings

### T010 — Lane advance, FR-055 decimation, the two mappings, `setWanderRate`, the crossfade counter

**Files to edit:** `feedback_ecology.h`, `feedback_ecology_test.cpp`.

**Test first — `TEST_CASE("FeedbackEcology_WanderRateMapping", "[feedback_ecology]")` (SC-025):**

* **(a) The mapping, against S11's table.** `setWanderRate(kMinWanderRateHz = 0.002f)` →
  `getLaneDecimation() == 17`; `setWanderRate(kDefaultWanderRateHz = 0.03f)` → `2`;
  `setWanderRate(kMaxWanderRateHz = 1.0f)` → `1`. Clamp ends: `setWanderRate(0.0f)` reports
  `getWanderRate() == 0.002f` with decimation 17; `setWanderRate(1e6f)` reports `1.0f` with
  decimation 1. (A non-finite argument is a no-op — asserted in the non-finite TU, T016.)
* **(b) The behavioural arm a hard-wired decimation fails.** Two 300 s renders of the reference patch
  at 48 kHz from the same seed, one at `0.002 Hz` (decimation 17, effective correlation time 500 s)
  and one at `0.0333 Hz` (decimation 1, `tau` saturated at `kTauMax = 30 s`). For every loop record
  the **range** (max − min) of `getLoopTargetDelayMs(i)` sampled once per block. **Assert the mean
  range across the six loops at 0.002 Hz is at most half the mean range at 0.0333 Hz**, and report
  both. A build with `laneDecimation_` hard-wired to 1 produces a ratio of 1.0 and fails. The **factor
  of 2 is the floor** and may be re-pinned upward, never downward, from a recorded measurement.
* **(c) The rebase, not a reset.** Render 60 s of the reference patch while calling
  `setWanderRate(getWanderRate())` — the same value, so nothing changes — every **96** samples (a
  phase that is not a multiple of 64). Compare against an unmolested 60 s reference from the same
  seed: within `render_fingerprint.h` tolerances. A build that wrote `laneCounter_ = 0` instead of
  `laneCounter_ %= newDecimation` advances the lanes on every control step instead of every second one
  and diverges grossly.

**Test first — `TEST_CASE("FeedbackEcology_DelayMotion", "[feedback_ecology]")` (SC-005):**

* **(a)** At **44.1 kHz** (the binding rate — 100 samples is 2.268 ms there against 2.083 ms at 48 kHz
  and 0.521 ms at 192 kHz) on the reference patch, over **300 s**: for **every** loop,
  `getLoopCurrentDelayMs(i)` takes at least **3 distinct settled values** whose total spread is at
  least **2 %** of that loop's base delay; and within the first **120 s**, every loop whose base delay
  is ≥ 109 ms takes at least **4**.
  *Sampling, stated once:* the render is driven in **512-sample blocks** and both
  `getLoopCurrentDelayMs(i)` and `getLoopCrossfadeCount(i)` are read **once per block, between
  blocks**. A reading counts as **settled** only when `getLoopCrossfadeCount(i)` is unchanged from the
  previous sampled block **and** unchanged at the next — `getCurrentDelaySamples()` is the
  gain-weighted tap average (`crossfading_delay_line.h:306-309`), so an in-flight reading would
  inflate the count with intermediate positions. Report `getLoopCrossfadeCount(i)` per loop.
  **The floor that may never be weakened is ≥ 2 distinct settled values (one completed step) for every
  loop.** The counts above the floor are provisional (O-2) and may be re-pinned only from a recorded
  measurement. If the shortest loop cannot clear the floor, the fix is FR-023's derivation — raise
  that loop's default wander fraction — never a shorter assertion.
* **(b)** At 44.1 kHz with `baseDelayMs = 10` and `delayWanderFraction = 0.05` (peak excursion
  0.5 ms = 22 samples, below `kCrossfadeThresholdSamples = 100`), `getLoopCurrentDelayMs(0)` is
  **constant** over 120 s — the documented consequence of the shipped delay line, asserted so it is a
  known property rather than a discovered bug.
* **(c)** At 192 kHz the reference patch also satisfies (a).

**Test first — `TEST_CASE("FeedbackEcology_SeedDeterminism", "[feedback_ecology]")`, arm (d) only
(the rest in T015):**
*Statistical:* over a 120 s render the pairwise Pearson correlation between the **per-block first
differences** of the twelve **lane target** trajectories read through `getLoopTargetDelayMs(i)` /
`getLoopTargetCutoffHz(i)` is **below 0.25** for every pair, plus a positive control (two lanes on one
shared stream, read through the two different mappings) **above 0.9**.
**AMENDED after this task's first build — see spec.md SC-009 (d) for the measurements.** The original
wording correlated the trajectory LEVELS, which over a 120 s window of 33.3 s-decorrelation-time lanes
supplies N_eff ≈ 2 samples: twelve provably independent shipped `BrownianDrift` streams measured worst
|r| = 0.6916 at seed `0x5EED` and 0.5994…0.8894 across 64 base seeds, i.e. the criterion was
unsatisfiable by any correct implementation. The increments measure 0.0715 / 0.0922 respectively
against the **unchanged** 0.25 bound, and a shared stream still reports 0.9855.
The realised readings must **not** be used — the staircase has near-zero variance and its
correlation is 0/0 or dominated by two quantisation steps.
*Deterministic:* the twelve `deriveStreamSeed(seed, salt)` values from the S1.6 salt table are
pairwise **distinct**, computed directly in the test. This is the actual salt-collision guard.

**Implementation intent (plan S5.2 steps 1 and 4, S5.4, S5.5, S11):**

* `updateControl()` step 1 is the **decimated lane advance, first, before anything reads a lane**:
  `if (laneCounter_ == 0) for each loop { delayLane.processBlock(64); cutoffLane.processBlock(64); }`
  then `laneCounter_ = (laneCounter_ + 1) % laneDecimation_`. It is **unconditional** in three ways,
  each with a criterion behind it: a zero-depth lane advances; a **dormant** loop's lanes advance; and
  `setWanderEnabled(false)` scales the **depth term** via `wanderScale()` — it does not freeze the
  motion (Q2, Phase-3 verbatim). The fixed 64-sample argument is what makes the decimation
  sample-rate independent.
* Step 4a maps both lanes for **all six loops**, including skipped ones — the target getters are the
  only observable proof the lanes are alive. Delay is **multiplicative**
  (`clamp(baseDelayMs · (1 + wanderScale · frac · laneValue), kMinDelayMs, kMaxDelayMs)`); cutoff is
  **log2-domain** (`clamp(exp2(baseLog2Cutoff + wanderScale · octaves · laneValue), kMinCutoffHz,
  maxCutoffHz_)`). `laneValue()` re-clamps to `[-1, +1]` and maps a non-finite reading to 0.
* Step 4d writes `delay.setDelayMs(targetDelayMs)` **only when `readMuteSamples == 0`** — freezing
  the position while the read-mute window runs is load-bearing, not an optimisation: the window is
  exactly one delay length only if the delay does not grow while it runs. Step 4e writes
  `svf.setCutoff(targetCutoffHz)`; the SVF's own per-sample smoother carries it and there is **no**
  `std::tan` on the per-sample path.
* The crossfade-onset counter (S5.5): immediately after `setDelayMs`,
  `const bool xf = delay.isCrossfading(); if (xf && !lastCrossfading) ++crossfadeCount;
  lastCrossfading = xf;`. A crossfade can begin only inside `setDelaySamples`, which this component
  calls only here, so the counter is an exact count of **onsets**; a 20 ms crossfade spans ~15 control
  steps at 48 kHz, so no onset can be missed.
* `setWanderRate` (S11) is the single owner of `laneDecimation_` and of every lane's `setSmoothness`,
  transcribed from `resonance_drift_network.h:715-740`, and ends with
  `laneCounter_ %= newDecimation; laneDecimation_ = newDecimation;` — a **rebase**, never a reset.

**Verify**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "FeedbackEcology_*" 2>&1 | tail -10
```

---

## Group I — Row-sum normalisation, the structural half of boundedness

### T011 — `normaliseRows()` with FR-075's count mask, and the two smoothing clocks

**Files to edit:** `feedback_ecology.h`, `feedback_ecology_test.cpp`.

**Test first — `TEST_CASE("FeedbackEcology_GainNormalisation", "[feedback_ecology]")` (SC-015).**
Every arm renders **≥ 100 ms** after writing the configuration and before reading the applied getters
(FR-035 normalises the **smoothed** values; 100 ms is 5 × the 20 ms one-pole time-to-99 % plus the
`kCompletionThreshold` snap).

* **(a) The coefficients in force, not the reported total.** For **1 000** randomly drawn `ownFb` +
  coupling configurations, reconstruct
  `S_i = getLoopAppliedOwnFeedback(i) + Σ_{j≠i} getLoopAppliedCoupling(j, i)` and assert
  `S_i <= kMaxTotalLoopGain + 1e-6f` for every loop; `S_i == kMaxTotalLoopGain` within `1e-5`
  whenever the raw configured sum exceeded it; and `getLoopAppliedTotalGain(i) == S_i` within `1e-6`.
  The `<=` half and the agreement clause are additionally sampled **once per control chunk during the
  glide**, before settling, to prove the bound holds at every instant.
  *Why this shape:* `getLoopAppliedTotalGain(i)` is `std::min(g, kMaxTotalLoopGain)` by construction,
  so on its own it satisfies both clauses whatever the coefficients in force are — a build that
  dropped the `* scale` from the coefficient writes would pass. `getLoopAppliedCoupling` is what turns
  the criterion into a measurement.
* **(b)** The **configuration** getters (`getLoopGain`, `getCoupling`) still report the caller's
  clamped values, unchanged by the normalisation.
* **(c)** Worst case: `ownFb = 0.90` on every loop with all five neighbours at `0.5` → raw
  `g = 3.40`, `scale = 0.279412`, `getLoopAppliedOwnFeedback(i) == 0.251471` (±1e-5),
  `getLoopAppliedCoupling(j, i) == 0.139706` (±1e-5), `getLoopAppliedTotalGain(i) == 0.95` (±1e-6).
* **(d) Count-inert / dormancy-inert.** *First arm:* `numLoops = 1`, established through
  `PrepareConfig{.numLoops = 1}` **or** `setNumLoops(1)` followed by `reset()` so the count mask is
  **snapped** rather than gliding, with every matrix entry — including every pair among the unused
  slots 1…5 — written to `kMaxCouplingPerPair`; then
  `getLoopAppliedTotalGain(0) == getLoopGain(0)` within `1e-6` and
  `getLoopAppliedCoupling(j, 0) == 0.0f` **exactly** for every `j`. *Second arm:* `numLoops = 6`,
  every pair at `kMaxCouplingPerPair`, loops 1…5 put to sleep one at a time; at every step
  `getLoopAppliedTotalGain(0)` is unchanged to within `1e-6` of its all-awake value (dormancy never
  touches the mask).
* **A targeted assertion for R-1**, which no criterion otherwise measures: after
  `setCoupling(0, 1, 0.3f)` on a settled instance, `getLoopAppliedCoupling(0, 1)` reaches its target
  within **~25 ms** of rendered time. Configured at `fs` instead of `fs/64` the realised constant is
  `64 × 20 ms = 1.28 s` and this fails.

**Implementation intent (plan S5.0, S5.2 steps 2–3, S5.3, S7.5's mask targets):**

* **S5.0 is the single most likely trap in this phase.**
  `calculateOnePolCoefficient(smoothTimeMs, sampleRate) = exp(-5000/(smoothTimeMs · sampleRate))`
  takes a **per-sample** rate. The 42 control-rate `OnePoleSmoother`s (36 coupling + 6 own-feedback +
  6 count-mask) are advanced once per 64-sample control step and **must** be configured with
  `fs / kControlChunkSamples`. Every per-sample `LinearRamp` is configured with `fs`. Repeat the
  reason in a comment at the call site.
* `updateControl()` step 2 advances **all 42** smoothers on **every** control step — the
  `filter_feedback_matrix.h:595-598` `else` branch exists precisely because a smoother that stops
  advancing desynchronises and steps when its path re-engages. **The one carve-out** (OQ-2 lever 3):
  a smoother whose current value is **bit-exact** at its target may skip `process()`, because
  `OnePoleSmoother::process()` snaps `current_ = target_` inside `kCompletionThreshold = 1e-4`. Never
  a licence to skip an unsettled smoother.
* `normaliseRows()` (step 3) is transcribed from S5.3: read the count mask
  `cm[j] = countSmoother_[j].getCurrentValue()`; for each `i`, if `cm[i] == 0.0f && !engineActive`
  zero that row's applied values and continue (**both** halves are required — `cm[i] == 0` alone would
  zero a row whose gate is still fading, and `!engineActive` alone would zero a dormant row and break
  SC-015 (d)'s second arm); otherwise
  `g = own + Σ_{j≠i} cm[j]·couplingSmoother_[j][i]`,
  `scale = (g > kMaxTotalLoopGain) ? kMaxTotalLoopGain / g : 1.0f`,
  `appliedOwnFb_[i] = own·scale`, `appliedCoupling_[j][i] = cm[j]·couplingSmoother_[j][i]·scale`
  (diagonal `0.0f`), `appliedTotalGain_[i] = min(g, kMaxTotalLoopGain)`.
* The mask is applied on the **source** index `j` only, and only the **applied** values are scaled —
  the stored targets survive round-trip through `getLoopGain`/`getCoupling`.
* The row sum ranges over `j < numLoops` excluding `i`, **ignoring wake/dormancy** (Q3), and reaches
  the count-excluded answer exactly because `countSmoother_[j]` snaps to `0.0f`.

**Verify**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "FeedbackEcology_*" 2>&1 | tail -10
```

---

## Group J — The energy governor

### T012 — Tracker, law, ramp, and the follower's own finiteness guard (FR-043 – FR-045)

**Files to edit:** `feedback_ecology.h`, `feedback_ecology_test.cpp`.

**Test first — `TEST_CASE("FeedbackEcology_Governor", "[feedback_ecology]")` (SC-006):**

* **(a)** Drive the reference patch with white noise (RMS, fixed seed) swept **−40 → 0 dBFS in 6 dB
  steps, 20 s per step**. The threshold crossing is **measured, not assumed**: run the sweep twice —
  once at `ratio = 1` (off), recording `getGovernorRms()` at the end of each step, then at the
  defaults. Assert `getGovernorGain()` is **non-increasing** across the sweep; **exactly `1.0f`** at
  every step whose recorded RMS is below `dbToGain(getGovernorThresholdDb())`; **strictly below
  `1.0f`** at every step above it; and **< 0.6** at 0 dBFS. The sweep must **straddle** the threshold
  and the straddle is itself asserted. If it does not straddle, the response is to re-measure and
  record `kDefaultGovernorThresholdDb` the Phase-3 way — never to move an assertion.
* **(b)** Output RMS rises **sub-linearly**: the total output RMS increase from −40 to 0 dBFS is at
  least **12 dB less** than the 40 dB input increase, at `ratio = 8`.
* **(c)** `getGovernorGain()` never below `kGovernorMinGain = 0.05` on a representative subset of
  SC-001's configurations re-run here.
* **(d)** At `ratio = 1.0` the governor gain is **exactly `1.0f`** at every input level — the exponent
  `1/ratio − 1` is exactly `0.0f` and `std::pow(x, 0.0f)` is exactly `1.0f` for every finite positive
  `x`.
* **(e) No zipper.** On a 20 dB input step, `ClickDetector` reports **zero** detections in the 200 ms
  window beginning **one sample after** the step sample. The offset is load-bearing: at `mix = 1` the
  reference patch passes the drive's own discontinuity into the wet path. Apply the step at a zero
  crossing of the noise generator's output where one exists within ±1 sample.
* **(f) Loop-count invariance — the arm a raw, unscaled tracker fails.** Repeat (a)'s sweep at
  `numLoops = 1` and at `numLoops = 6` and record each count's threshold-crossing input level (the
  input dBFS at which `getGovernorGain()` first drops below `1.0f`). The two agree **within 1 dB**.
  Under the rejected raw `Σ_i b_i` reading they differ by `10·log10(6) ≈ 7.8 dB`.

**Implementation intent (plan S5.6, S8.3):**

* The tracker is fed **every sample** in `renderChunk` step 4 with `normGain · bSum` — the **same**
  `1/sqrt(numLoops)` normalisation FR-017 applies to the wet sum (Q6), using the `normGain` already
  computed for the output stage. One multiply, no second ramp. Without it `-6 dB` would name a level
  up to 7.8 dB apart between `numLoops = 1` and 6.
* The law runs **once per control step** (750/s at 48 kHz, never per sample):
  `rms = follower_.getCurrentValue()`; `threshold = dbToGain(governorThresholdDb_)`;
  `over = rms / threshold`;
  `target = (over <= 1.0f) ? 1.0f : clamp(pow(over, 1.0f/ratio - 1.0f), kGovernorMinGain, 1.0f)`;
  `governorRamp_.setTarget(...)`, advanced per sample.
* **The follower's own health guard is mandatory and is NOT redundant with the per-sample trap**
  (R-16). Before anything else in the control step:
  `if (!detail::isFinite(rms)) { follower_.reset(); ++nonFiniteResets_;
  governorRamp_.setTarget(1.0f); return; }`. The chain it closes, verified end to end:
  `EnvelopeFollower::processRMS` never self-heals from a NaN (`envelope_follower.h:313-325`); the law
  then yields a NaN target; and `LinearRamp::setTarget` does **not** propagate a NaN — it **mutes**
  (`target_ = current_ = increment_ = 0.0f`, `smoother.h:342-348`), after which every `b_i` stays
  finite, the per-sample rung-5 trap can never fire, and the component sits permanently silent with
  both health counters reading clean. A second `detail::isFinite(target)` test guards the computed
  target so a later phase adding a term to the law cannot reintroduce the mode.
* The measurement point is **pre-governor, pre-gate, pre-`tanh`** — the quantity being controlled,
  measured before its own action, which makes the loop a first-order regulator rather than an
  oscillator.

**Verify**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "FeedbackEcology_*" 2>&1 | tail -10
```

---

## Group K — The loop life cycle and the Dormancy rule

### T013 — `gateSteady`, `refreshGates`, the sleep and wake edges, `setNumLoops`

**Files to edit:** `feedback_ecology.h`, `feedback_ecology_test.cpp`.

**Test first — `TEST_CASE("FeedbackEcology_Dormancy", "[feedback_ecology]")` (SC-014):**

* **(a)** `setLoopDormant(i, true)` and `setLoopWake(i, 0.0f)` produce **the same render** within
  `render_fingerprint.h` tolerances — the Dormancy rule's core claim.
* **(b)** A settled-dormant loop reports `isLoopEngineActive(i) == false` and
  `getLoopGate(i) == 0.0f` **exactly**, while `getLoopTargetDelayMs(i)` and
  `getLoopTargetCutoffHz(i)` **keep changing** and `getLoopCurrentDelayMs(i)` /
  `getLoopCurrentCutoffHz(i)` are **frozen**. Two distinct claims, asserted separately: the delay
  freeze (no `read()` call, so no tap advance) and the cutoff freeze (no `setCutoff` call, so the last
  commanded value stands).
* **(b2)** Behavioural lane-advance proof: sleep a loop, hold **60 s** of silence, wake it; the delay
  and cutoff it wakes with (read **after** the FR-064 snap) differ from the values it slept with by
  more than one FR-023 step (delay) and by more than **1 %** (cutoff).
* **(c)** Wake re-entry is a 50 ms ramp: `getLoopGate(i)` reaches its target in `50 ms × sampleRate`
  samples ±1 control chunk, **monotonically**, and `ClickDetector` reports **zero** detections across
  the edge — **measured with every off-diagonal coupling at `kMaxCouplingPerPair = 0.5`**, the fixture
  at which a neighbour reading the *pre-gate* value would see a 0.5-amplitude step. Zero detections
  asserted on every neighbour's tap as well as on the sleeping loop's own gate.
* **(d) The sleep edge clears the loop (FR-063).** Charge a loop with 30 s of drive, sleep it, wait
  60 s of silence, then wake it with the input still silent: the loop's tap stays below **−80 dBFS**
  for the first **500 ms** after the gate opens. A build that only skips the chain replays the frozen
  ring and fails by 60 dB or more.
* **(e)** `setLoopWake(i, 1e-9f)` snaps the gate target to exactly `0.0f` and `isLoopEngineActive(i)`
  becomes false after the ramp settles and the next control step runs the sleep edge.

**Test first — `TEST_CASE("FeedbackEcology_LoopCount", "[feedback_ecology]")` (SC-022):**

* **(a)** `numLoops = 5` and `numLoops = 6` each render non-silent and satisfy the four bounded
  assertions (peak `< kOutputClamp`; `getClampEngagementCount() == 0`;
  `getNonFiniteResetCount() == 0`; every sample finite) on a 60 s corner render.
* **(b) The FR-017 divisor is verified exactly, not through a level tolerance.** At each count the wet
  output reproduces `clamp(wetGain_linear × Σ_i tap_i / sqrt(numLoops))` within `kSampleTolerance`. A
  build that omits FR-017, divides by the awake count, or divides by `kMaxLoops`, misses by 0.79 dB or
  produces NaN. **The wet RMS ratio between the two counts is reported, never gated** — the correct
  five-against-six ratio lies anywhere between 0 dB and +0.79 dB depending on loop correlation.
* **(c) The mid-render count change is click-free.** `setNumLoops(6 → 5)` during a render:
  `ClickDetector` reports **zero** detections across the call and for 500 ms after it. **Run twice:
  once on the default 0.04 ring, and once with every off-diagonal pair pre-seeded at
  `kMaxCouplingPerPair = 0.5`.** The second fixture is the one with teeth — at 0.04 an
  instantly-deleted coupling coefficient is a −28 dB event the detector can miss.

**Implementation intent (plan S7):**

* `gateSteady(i)`: `0.0f` if `i >= numLoops` (an out-of-count loop sleeps), `0.0f` if `dormant`,
  `0.0f` if `wakeAmount <= kWakeSilenceEpsilon`, else `wakeAmount`. The epsilon snap happens **here,
  at the source**, which keeps the sleep edge's exact `== 0.0f` test valid once a Phase-8 agent writes
  `envelope × depth` into `setLoopWake`. `setLoopWake` clamps to `[0, 1]`.
* **The sleep edge** lives in `updateControl()` step 4b, **before** any write:
  `if (engineActive && gate.isComplete() && gate.getCurrentValue() == 0.0f) { clearLoopAudio(i);
  engineActive = false; }`. The exact comparison is correct by construction — `gateSteady()` returns a
  literal `0.0f` and `LinearRamp::process()` lands exactly on its target. The clear is the ratified
  deviation from the Dormancy rule and the header must carry its justification: a feedback loop has no
  generator behind it, so "skipping" it freezes a fully charged delay line that would be re-injected
  minutes later as a stale burst.
* **The wake edge** lives in `refreshGates()`, i.e. **in the setter**, not at the next control step —
  a loop left `engineActive == false` while its gate rises would be silent for up to 63 samples
  (a ≤ 1.3 ms attack notch). It does `delay.snapToDelayMs(targetDelayMs)` (**not** `setDelayMs`: a
  queued crossfade would fire from a stale tap), **recomputes** `readMuteSamples` from the
  **post-snap** position, `svf.setCutoff(targetCutoffHz)` + `svf.snapToTarget()`,
  `lastCrossfading = false`, `engineActive = true`.
* **The `target != lastGateTarget` guard is load-bearing**, not an optimisation:
  `LinearRamp::setTarget` recomputes `increment_` on **every** call, so re-targeting a mid-ramp gate to
  the value it is already heading for **restarts** its 50 ms. A Phase-8 agent writing one loop's wake
  per block would otherwise stretch every other loop's ramp without bound.
* Callers of `refreshGates()`: `setLoopWake`, `setLoopDormant`, `setNumLoops` — and nothing else.
  `prepare()`/`reset()` bypass it deliberately, snapping the gate and seeding `lastGateTarget` in the
  same breath.
* `setNumLoops(n)` (S7.5): clamp to `[1, kMaxLoops]`, `normGainRamp_.setTarget(1/sqrt(n))`,
  `countSmoother_[i].setTarget(i < n ? 1.0f : 0.0f)` for all six, then `refreshGates()`. Allocates
  nothing. **Three quantities move and all three are ramped at `kGainRampMs`**: the dropped gates, the
  `normGain` divisor (`1/sqrt(6) → 1/sqrt(5)` is 0.792 dB — a step here makes SC-022 (c) unreachable),
  and the count mask on the dropped loops' outgoing coupling. The divisor is **always** `numLoops`,
  never the awake count: an awake-count reading computes `1/sqrt(0) = inf` when every loop sleeps, and
  `inf · 0.0f` is NaN.

**Verify**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "FeedbackEcology_*" 2>&1 | tail -10
```

---

## Group L — Allocation and the clamp, under the finished surface

### T014 — `FeedbackEcology_NoAllocation` (SC-007) and the SC-013 (a) cross-arm

**Files to edit:** `feedback_ecology_test.cpp` (and `feedback_ecology.h` only if a defect is found).

**Test first — `TEST_CASE("FeedbackEcology_NoAllocation", "[feedback_ecology]")` (SC-007):**
`AllocationScope` (`tests/test_helpers/allocation_detector.h:111`) around, with the `prepare()` call
**outside** the scope:

* 1 000 `processBlock` calls at irregular sizes cycling **1, 7, 64, 511, 512, 4096, 8193**;
* every setter on the surface driven to its extremes;
* `reset()`; `setSeed()`; `setNumLoops()` across `[1, 6]`; `setLoopDormant`/`setLoopWake` edges (which
  fire `clearLoopAudio` on the audio path); `setCouplingMatrix`; `processBlockTapped` with all six
  taps.

Assert **zero allocations and zero frees** (`hadAllocations() == false`, `getAllocationCount() == 0`).

**Also in this task:** complete SC-013 (a) now that every criterion's fixture exists — assert
`getClampEngagementCount() == 0` across a representative arm of SC-002, SC-003, SC-006 and SC-011
(all `wetGain <= 0 dB`), alongside the SC-013 (b) arm already written in T009.

**Implementation intent:** none expected — this is a gate on work already done. If an allocation
appears, find its owner (`EnvelopeFollower::prepare` ignores `maxBlockSize` and allocates nothing;
`BrownianDrift`, `SVF`, `Biquad`, `DCBlocker`, `OnePoleSmoother` and `LinearRamp` are all
allocation-free; the only heap is the six `CrossfadingDelayLine` buffers) and remove it — **never**
widen the scope of the assertion.

**Verify**

```bash
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "FeedbackEcology_*" 2>&1 | tail -10
```

---

## Group M — Determinism, sample rate, and the measured resonator ring

### T015 — SC-009 (a)–(c), SC-011's render arms, SC-023 (a)–(b)

**Files to edit:** `feedback_ecology_test.cpp`.

**Test first — `FeedbackEcology_SeedDeterminism` arms (a)–(c) (SC-009):**

* **(a)** Two instances prepared identically with the same seed produce renders equal within
  `render_fingerprint.h` tolerances.
* **(b)** `reset()` followed by the same render reproduces the first render within the same
  tolerances.
* **(c)** Two different seeds differ: `compareFingerprints(...).withinTolerance()` is **false**, the
  mean absolute difference exceeds **50 % of the reference render's own `meanAbs`**, and it also
  exceeds `kSampleTolerance` in absolute terms.
  **AMENDED after this task's first build — see spec.md SC-009 (c) for the measurements.** The
  original wording floored the difference at `100 × kSampleTolerance` = 0.05 in absolute sample
  units. The reference patch renders at **RMS 0.0014655 (−56.7 dBFS), meanAbs 0.0011695** — quiet by
  design, for the same reason `kDefaultGovernorThresholdDb` had to move to −52 dB at T012 — so
  `mean|a−b| ≤ mean|a| + mean|b| = 0.00234` and the old floor was unsatisfiable by **any**
  implementation, by 43×. Measured here: `mean|a−b|` = **0.0013306** = **1.138 ×** the render's own
  meanAbs, against √2 = 1.414 for two statistically independent renders and **0** for a build whose
  seed never reaches the lanes. The threshold's intent, the render length and the seeds did not move.

**Test first — `FeedbackEcology_SampleRate` render arms (SC-011):** rendered at **44.1, 48, 96 and
192 kHz** with the reference patch: RMS within **1 dB** across rates, spectral centroid within
**10 %**, no clamp engagements and no non-finite resets at any rate. Plus the staircase-aware realised
delay bound, with `setWanderEnabled(false)` called immediately after `prepare()`:

```
|getLoopCurrentDelayMs(i) − configured ms| <= max(1 % of the configured ms,
                                                 kCrossfadeThresholdSamples / sampleRate in ms)
```

i.e. **2.268 ms** at 44.1 kHz, **2.083** at 48, **1.042** at 96, **0.521** at 192. Report the measured
error at every rate; with wander off and the FR-005 snap it is expected to be **exactly zero**, and
the staircase term is a tolerance that keeps the criterion honest, not a licence to drift.

**Test first — `FeedbackEcology_ResonatorRing` arms (a) and (b) (SC-023).**
*Fixture, per loop under test, one `SECTION` each:* `prepare()`; `setLoopGain(i, 0.0f)` on **every**
loop and every coupling to `0.0f` — so there is **no feedback** and the tap is the loop chain's own
impulse response, not a loop resonance; `setLoopCutoffHz(i, maxCutoffHz)` and
`setLoopFilterMode(i, Lowpass)` so the SVF is near-transparent at the resonator centre;
`setWanderEnabled(false)`; `setLoopInputGain` 1.0 on the loop under test and 0.0 elsewhere; then
**`reset()`**; then a single-sample unit impulse followed by 3 s of silence, read through the FR-073
tap.

* **(a)** Loop 5 (`210 Hz`, request 1.0 s, `rt60ToQ(210, 1.0) = 95.50 < kMaxResonatorQ` so nothing is
  clamped). Measure the decay: peak of `|tap|` in each successive 1-cycle window → dB →
  least-squares fit over the span from −5 dB to −40 dB below the initial peak → extrapolate to
  −60 dB. **Assert the measured RT60 is within ±10 % of 1.000 s**, and report it.
* **(b)** Loop 0 (`1200 Hz`, request 1.0 s, `rt60ToQ` returns 545.70 and is clamped to
  `kMaxResonatorQ = 100`, realised **0.1833 s**). Same measurement, within **±10 %**.
* **The floor that may never be weakened: loop 5's measured RT60 must exceed 0.5 s.** At
  `biquad.h`'s `kMaxQ = 30` — the value `configure()`/`calculate()`/`SmoothedBiquad::setTarget` would
  silently impose — the realised figure is `30 · kLn1000 / (π · 210) = 0.3141 s`, and loop 0's is
  0.0550 s: the defect misses by 219 % and 233 % against a ±10 % band. If the band proves too tight
  for the measurement method (the delay and DC blocker are in the path, and the fit is over a finite
  window), it may be re-pinned from a recorded measurement under the stop-and-surface rule; **the
  0.5 s floor and the shape of the assertion may not move**, and the response to a genuine miss is to
  fix the coefficient path.

**Implementation intent:** none expected; these are gates on T007/T008/T009/T010. `getLoopResonanceRt60`
must agree with the measured ring to within the same ±10 % (SC-023 (c), whose table half was asserted
in T007) — that agreement is what ties the bookkeeping field to the coefficients actually in force.

**Verify**

```bash
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "FeedbackEcology_*" 2>&1 | tail -10
```

---

## Group N — The fault-injection probe and the coherence helper (disjoint new files)

### T016 [P] — `feedback_ecology_nonfinite_test.cpp`: the probe definition and SC-012

**Files to edit:** `dsp/tests/unit/systems/feedback_ecology_nonfinite_test.cpp` (the T001 stub; this
TU is the **only** one in the `-fno-fast-math` block and the **only** definition of the probe).

**Non-finite values are built from bit patterns behind a volatile sink**, never
`std::numeric_limits<float>::quiet_NaN()`/`infinity()`:

```cpp
[[nodiscard]] inline float bitNaN() noexcept {
    volatile std::uint32_t bits = 0x7FC00000u;
    std::uint32_t v = bits; float f; std::memcpy(&f, &v, sizeof f); return f;
}
[[nodiscard]] inline float bitInf(bool negative) noexcept {
    volatile std::uint32_t bits = negative ? 0xFF800000u : 0x7F800000u;
    std::uint32_t v = bits; float f; std::memcpy(&f, &v, sizeof f); return f;
}
```

**The probe definition** (plan S8.5), in this TU and nowhere else:

```cpp
namespace Krate::DSP::detail {
struct FeedbackEcologyNonFiniteProbe {
    static void poisonDcBlocker(FeedbackEcology& fe, std::size_t loop, float nonFinite) noexcept {
        static_cast<void>(fe.loops_[loop].dcBlocker.process(nonFinite));   // sticks in y1_
    }
    static void poisonFollower(FeedbackEcology& fe, float nonFinite) noexcept {
        static_cast<void>(fe.follower_.processSample(nonFinite));          // sticks in squaredEnvelope_
    }
};
}  // namespace Krate::DSP::detail
```

**Test first — `TEST_CASE("FeedbackEcology_NonFinite", "[feedback_ecology]")` (SC-012):**

* **(a)** Every float setter, given NaN and ±Inf, is a **no-op** and the matching getter still reports
  the previous value (FR-009). Include `setWanderRate`, `setMix`, `setWetGain`,
  `setGovernorThresholdDb`, `setGovernorRatio`, every per-loop float setter, and `setCouplingMatrix`
  with one poisoned entry (only that pair's previous value stands).
* **(b) The public path.** One non-finite **input sample** injected into a settled render: the render
  is finite and bounded from the injection sample onward; `getNonFiniteResetCount()` stays **0** (the
  poison never reaches `b_i`, so FR-047 correctly does not fire); `getClampEngagementCount()` stays 0;
  and the recovery edge is click-bounded — `ClickDetector` over the following 200 ms reports **at most
  1** detection, with the maximum absolute first difference inside SC-003 (b)'s shape bound. Expect
  **zero** in practice: this implementation intercepts at the dry sanitiser, one stage earlier than
  the spec's narrative (S16 C-2), so the bound is slack rather than tight.
* **(c1) The in-loop `DCBlocker`, reaching the per-sample rung-5 trap.**
  `poisonDcBlocker(fe, 2, bitNaN())` on a settled render. Assertions:
  `getNonFiniteResetCount()` increments by **exactly 1**; `clearLoopAudio(2)` ran — loop 2's tap is
  `0.0f` at the trap sample and stays `0.0f` for the read-mute window (one delay length), after which
  the loop refills from its input tap rather than resuming its ring; the governor's follower was reset
  (`getGovernorGain()` finite at every subsequent sample and `getGovernorRms()` back within **1 dB**
  of its pre-poison value within **2 s**); and the main output is finite and bounded from the trap
  sample onward.
  **The other five loops, scoped correctly:** the trap also calls `follower_.reset()`, which zeroes
  the governor's RMS state **globally**, so a blanket "bit-identical over the same block" claim is
  false on a correct build. Assert instead: the five other loops' taps are **bit-identical** to an
  unpoisoned reference for every sample **before the first control step after the injection**, and
  converge back within `kSampleTolerance` once `kGovernorRampMs` + `kGovernorReleaseMs` have elapsed.
  **Repeat the arm at `ratio = 1`** (governor off, `govGain` exactly `1.0f`), where `follower_.reset()`
  is unobservable and the bit-identity holds for the whole block — that repeat is the clean isolation
  proof.
* **(c2) The governor's `EnvelopeFollower`, reaching the S5.6 control-step guard.**
  `poisonFollower(fe, bitNaN())`. **This path cannot reach the per-sample trap** — `LinearRamp::
  setTarget` converts a NaN target into an unramped step to **zero**, so `govGain` becomes `0.0f`,
  every `y_i` becomes 0, every `b_i` stays finite and the trap never fires. Assertions, all against
  the S5.6 guard: `getNonFiniteResetCount()` increments by **exactly 1** at the **next control step**
  (≤ 64 samples after the injection); `getGovernorRms()` is finite from the control step **after**
  that one and non-decreasing back toward its pre-poison value; `getGovernorGain()` is finite at every
  sample and returns to `1.0f` within `kGovernorRampMs` + one control chunk; the render is finite and
  bounded from the injection onward; and — **the anti-mute clause, the whole point of the arm** — the
  output RMS of the **2 s after** the injection is within **1 dB** of the 2 s before it. A build that
  merely muted itself fails here by 60 dB or more while passing everything else.
* **(d)** After a probe injection, `reset()` returns the component to a state whose subsequent render
  matches a never-poisoned reference within `render_fingerprint.h` tolerances.

**Verify**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "FeedbackEcology_NonFinite" 2>&1 | tail -5
```

Confirm the TU appears in the `-fno-fast-math` block (T001) and in **no** other
`set_source_files_properties()` call, and that the probe symbol exists in no other TU:
`grep -rn "FeedbackEcologyNonFiniteProbe" dsp/ plugins/` returns the header declaration and this TU
only.

---

### T017 [P] — `tests/test_helpers/coherence.h` and its smoke case

**Files to create:** `tests/test_helpers/coherence.h`.
**Files to edit:** `dsp/tests/unit/systems/feedback_ecology_spectral_test.cpp` (the T001 stub; T018
and T019 edit it in later groups, so there is no in-group conflict).
**No CMake edit** — `tests/test_helpers/CMakeLists.txt` is `add_library(test_helpers INTERFACE)` with
an include directory and **no source list** (verified this session; spec correction C-1).

**Test first — `TEST_CASE("FeedbackEcology_CoherenceHelperSmoke", "[feedback_ecology]")`:**

* coherence of a signal with **itself** over `[40 Hz, 4 kHz]` is `> 0.99`;
* coherence of two **independent** white-noise streams (different seeds) is `< 0.2`;
* coherence of a signal with a **filtered copy of itself** (one `SVF` lowpass, cutoff 2 kHz) is
  `> 0.9` in the passband — a linear time-invariant transform does not reduce coherence, which is
  precisely why SC-002 (d) reports rather than gates;
* every ordered input guard returns `0.0`: null pointer, zero length, non-power-of-two `fftSize`,
  `hopSize == 0` or `> fftSize`, non-finite or non-positive `sampleRate`, non-finite band edges.

**Implementation intent (plan S12.1):** built on `primitives/fft.h` and `core/window_functions.h`,
following `spectral_flux.h`'s shape (own STFT, never reads a processor's internals, allocates,
test-only):

```cpp
/// Welch magnitude-squared coherence between two equal-length signals.
/// @return mean coherence over [lowHz, highHz]; 0.0 when nothing measurable was produced.
/// NOT real-time safe (allocates). Test-only.
[[nodiscard]] inline double computeMeanCoherence(const float* a, const float* b,
                                                 std::size_t numSamples, double sampleRate,
                                                 std::size_t fftSize, std::size_t hopSize,
                                                 float lowHz, float highHz);
```

Hann window, 4096-point, 50 % overlap; accumulate `Sxx`, `Syy` and complex `Sxy` per bin over all
frames; `C(f) = |Sxy|² / (Sxx·Syy)`; average over the bin range; guard `Sxx·Syy` against zero with the
same `1e-12` floor `render_fingerprint.h` uses. Same ordered input guards as `computeMagnitudeFlux`
(`spectral_flux.h:78-84`).

**Verify**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "FeedbackEcology_CoherenceHelperSmoke" 2>&1 | tail -5
```

---

## Group O — Cross-loop transfer and click freedom (`[long]`)

### T018 — `FeedbackEcology_CrossLoopTransfer` (SC-002) and `FeedbackEcology_DelayDriftClickFree` (SC-003)

**Files to edit:** `dsp/tests/unit/systems/feedback_ecology_spectral_test.cpp`.

**Test first — `TEST_CASE("FeedbackEcology_CrossLoopTransfer", "[feedback_ecology][long]")`
(SC-002).** Gated instrument: single-loop excitation, cross-loop transfer.
`setLoopInputGain(0, 1.0f)`, `setLoopInputGain(i, 0.0f)` for `i >= 1`; reference patch otherwise;
drive throughout a 60 s render with all six FR-073 taps installed.
**Fixture rule, mandatory at every sweep point: `prepare()` → write the whole configuration →
`reset()` → render.** Without the `reset()` the criterion is unsatisfiable on a *correct* build:
`prepare()` snaps every `inputRamp` to `1.0f` and every coupling smoother to FR-033's 0.04 ring, so
loops 1–5 would be directly driven for ~960 samples and that energy would circulate at 0.72 through a
1.0 s-RT60 resonator for seconds. Define

```
T(c) = 20·log10( RMS over the taps of loops 1..5 / RMS of loop 0's tap )
```

with every off-diagonal coupling set uniformly to `c`; sweep `c ∈ {0, 0.02, 0.05, 0.10, 0.20}`.

* **(a) The floor is exact.** At `c = 0` every undriven loop's tap is **exactly `0.0f`** on every
  sample from the first — no warm-up, no discarded window. Anti-vacuity guard in the same arm:
  `getLoopInputGain(i) == 0.0f` and `getLoopAppliedCoupling(j, i) == 0.0f` for every `j != i` at the
  moment the render starts.
* **(b) Monotone rise.** `T(c)` strictly increasing across the four non-zero points.
* **(c) Anti-vacuity: the rise must be more than mixing.** Repeat the sweep with every `ownFb = 0`
  (six parallel feed-forward chains) and record `T_ff(c)`; then assert
  `T(0.20) − T(0.02) >= (T_ff(0.20) − T_ff(0.02)) + kInteractionMarginDb`. The feed-forward baseline
  is **measured in the same test**, never assumed.
* **(d) Reported, never gated.** Mean pairwise magnitude-squared coherence over `[40 Hz, 4 kHz]` from
  T017's estimator, printed beside the transfer table. It cannot gate: all six loops are driven by the
  same mono signal, so the coherence of two outputs of a common source is 1 at every frequency
  independent of the coupling. The roadmap's named metric goes on the record; the gate is the transfer
  measurement.
* **`kInteractionMarginDb` (provisional 1.5 dB) is pinned by measurement (O-1).** Run the sweep as a
  **probe first**, print `T(c)` and `T_ff(c)`, and pin the constant from the numbers. **The shape
  assertions — (a)'s exact zero, (b)'s strict monotonicity, (c)'s strict inequality against the
  measured baseline — may never be weakened**, and the margin may be re-pinned only by recording the
  measurement that justifies it.

**Test first — `TEST_CASE("FeedbackEcology_DelayDriftClickFree", "[feedback_ecology][long]")`
(SC-003).** Input: **110 Hz sine at −12 dBFS**, **300 s**, reference patch but
`delayWanderFraction = kMaxDelayWanderFraction = 0.5` and `wanderRate = kMaxWanderRateHz` on every
loop, so the crossfade fires as often as the component can make it.

* **(a)** `ClickDetector` reports **zero** detections over the whole render, at a config whose
  `sampleRate` is the render rate.
* **(b)** Two clauses, both relative to the render's own level so a quiet render cannot pass: the peak
  absolute sample is **≥ 0.05**, and given that, the maximum absolute first difference is **below 5 %**
  of that measured peak **and** no first difference exceeds **8×** the median of the largest 1 000
  first differences.
* **(c) The crossfades actually happened.** `Σ_i getLoopCrossfadeCount(i) >= 200` over the 300 s.
  Without this a build whose delay never moves passes (a) and (b) trivially.

**Verify**

```bash
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "FeedbackEcology_CrossLoopTransfer" 2>&1 | tee /tmp/fe_sc002.log | tail -20
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "FeedbackEcology_DelayDriftClickFree" 2>&1 | tail -10
```

Capture on the **first** run; do not re-run to read the output.

---

## Group P — The phase's central criterion (`[long]`)

### T019 — `FeedbackEcology_BoundednessSoak` (SC-001) and `FeedbackEcology_LongEvolution` (SC-021)

**Files to edit:** `dsp/tests/unit/systems/feedback_ecology_spectral_test.cpp`.

**Test first — `TEST_CASE("FeedbackEcology_BoundednessSoak", "[feedback_ecology][long]")` (SC-001),
five `SECTION`s.**
*The worst-case corner:* `numLoops = 6`; every `ownFb = kMaxLoopGain = 0.90`; every off-diagonal
`coupling = kMaxCouplingPerPair = 0.5` (raw row sum 3.40, normalised to 0.95); every filter
`Q = kButterworthQ`; every resonator RT60 at `kMaxDecayTime = 30 s`; wander at `kMaxWanderRateHz` with
`delayWanderFraction = 0.5` and `cutoffWanderOctaves = 4.0`.
*The four bounded assertions:* peak `|sample| < kOutputClamp (4.0)`;
`getClampEngagementCount() == 0`; `getNonFiniteResetCount() == 0`; every sample finite by
`detail::isFinite`.

* **(a1) Worst-case corner, wander on throughout, governor `ratio = 1` (off)** so the structural rungs
  are measured without it. Reference drive for the first 60 s, silence for the remaining 29 min. The
  four bounded assertions over the whole render, plus **non-divergence**: the RMS of the final 60 s is
  not greater than the RMS of the 60 s ending at input cut-off; the measured decay across the tail is
  **reported**. **No decay threshold on this arm** — with wander at `kMaxWanderRateHz` the equal-power
  two-tap crossfade blend can contribute up to `sqrt(2)` (+3.010 dB), so the worst-case instantaneous
  round-trip gain is `0.95 × 1.41421 × 1.000654 = 1.34438 > 1` and rung 1's contraction argument does
  not hold inside a crossfade window. Rungs 2–5 still bound the output, which is what this arm
  measures.
* **(a2) The contraction arm.** Identical to (a1) except `setWanderEnabled(false)` at the input
  cut-off, so no new crossfade fires during the tail and the loop is the strict contraction
  (0.950621 per circulation, −0.4399 dB). Same four bounded assertions **plus the RMS of the final
  60 s at least 60 dB below the RMS of the 60 s ending at input cut-off**. Margin: the shortest loop
  circulates 24.39 times/s (−10.73 dB/s, 60 dB in 5.6 s), the longest 2.227 times/s (−0.980 dB/s,
  60 dB in 61.2 s); in series with a 30 s resonator RT60 an order-of-magnitude estimate for the
  composite tail is ~91 s against this arm's 1 740 s — a factor of ~19. **The 60 dB threshold is not
  relaxed by the split; it is moved onto the fixture where the argument it tests is operative.**
* **(b) Sustained drive, 30 min, governor on.** Same corner, drive for the full 30 min, governor at
  defaults. The four bounded assertions plus **level stationarity**: the RMS of each of the thirty
  1-minute windows within **±1.5 dB** of the median window RMS.
* **(c) Randomised sweep, accelerated.** **256** seeded configurations (`Xorshift32`, seed index
  0…255), every scalar drawn uniformly from its full clamped range and every off-diagonal coupling
  from `[0, 0.5]`; each rendered 60 s with drive on for the first 10 s. Same four assertions.
  **`wetGain` is drawn from `[-24, 0]` dB, not its full range** — above `+4.27 dB`
  (`4.0/sqrt(6) = 1.633`) the trim can legitimately drive the clamp and this arm's third assertion is
  that it never engages; SC-013 (b) covers the positive half. Every other scalar, `mix` included, is
  drawn from its full clamped range.
* **(d) Parameter-jump arm.** Reference patch **with every off-diagonal pair pre-seeded at
  `kMaxCouplingPerPair`**. Every 500 ms for 5 minutes, jump one randomly chosen parameter to a
  randomly chosen extreme, including `setNumLoops`, `setLoopDormant` and whole-matrix
  `setCouplingMatrix` writes; `wetGain` again from `[-24, 0]` dB. The four bounded assertions apply to
  **every** setter without exception. **The click assertion** (no detection above SC-003's threshold at
  any jump instant) applies to every setter FR-076 declares **smoothed** — `setNumLoops` among them.
  Classify the seven stepped setters explicitly, in a named `constexpr` array so the exemption cannot
  quietly widen:
  * **not jumped at all:** `prepare`, `reset` (lifecycle calls, not parameter jumps);
  * **jumped and held to the click assertion:** `setWanderRate` — it steps no mapped value in the
    signal path, so it cannot click and must not be excused from proving it;
  * **jumped and exempt from the click assertion only** — exactly these five:
    `setLoopFilterMode`, `setLoopResonanceHz`, `setLoopResonanceRt60`, `setSeed`, `setWanderEnabled`.

*If this criterion cannot be made to pass by reducing gain — never by widening the clamp or shortening
the render — the component is wrong.*

**Test first — `TEST_CASE("FeedbackEcology_LongEvolution", "[feedback_ecology][long]")` (SC-021).**
A 30-minute render of the reference patch on 60 s of drive followed by a slow 0.05 Hz noise
excitation. Spectral centroid and an 8-octave-band energy vector on 10-second windows (180 windows).

* **(a) Not static.** The standard deviation of the centroid across the 180 windows is **> 3 %** of
  its mean.
* **(b) Not periodic.** For every lag from **15 to 25 minutes**, the **cosine similarity between the
  log-band-energy vectors** (natural log of each of the 8 band energies) of the two windows at that
  lag is **< 0.99**; report the maximum.
* **(c) Not divergent.** The centroid never leaves `[0.5×, 2×]` its median.
* **(d) Neither dying nor creeping.** The **least-squares slope** of the window RMS series in dB
  against time, multiplied by the render length, is within **±1 dB**. (d) deliberately overlaps
  SC-001 (b) — a drift that appears under only one of the two is exactly the finding worth having.

**Streaming statistics are mandatory here** — never materialise 691 MB.

**Verify**

```bash
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "FeedbackEcology_BoundednessSoak" 2>&1 | tee /tmp/fe_sc001.log | tail -30
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "FeedbackEcology_LongEvolution" 2>&1 | tail -20
```

---

## Group Q — The CPU gate

### T020 — `FeedbackEcology_CpuBudget` (SC-004 (a)–(f)) and the checked-in baselines

**Files to edit:** `dsp/tests/unit/systems/feedback_ecology_perf_test.cpp` (the stage probe from T004
stays; the gated arms are added beside it), and `feedback_ecology.h` **only** if a pre-authorised
lever is applied.

**Basis:** ns per 512-sample block at 48 kHz, best-of-25 × 500 blocks after 400 warm-up blocks. One
block period is **10 666 667 ns**; the roadmap's 1 %/voice is **106 666 ns/block**; the gated
baseline is `kBaseline × kRegressionFactor(1.5) <= 106 666`, i.e. **`kBaseline <= 71 111 ns`**.
Tagged `[.perf]`, with the baselines pinned so the **absolute** ceiling is evaluated on every CI leg
even though the case never runs there:

```cpp
static_assert(kBaseline * kRegressionFactor <= kReferenceNs, "SC-004: over the 1 %/voice ceiling");
static_assert(kBaseline >= kReferenceNs / 50.0,              "SC-004: baseline implausibly low");
```

**Arms**

* **(a)** reference patch, six loops awake, wander on — **this is `kBaseline`**.
* **(b)** six loops, wander **off** — reported, and required to be **within +2 % of (a)**, a band and
  not a bare inequality: FR-056 keeps the lanes advancing and only zeroes the depth term, so the true
  difference is a handful of control-rate multiplies. An untoleranced comparison fails on a scheduling
  accident, not a defect (the Phase-3 lesson).
* **(c)** all six loops dormant — must be **at least 40 % cheaper** than (a). This is the only thing
  that proves FR-062's skipped chain is not cosmetic.
* **(d)** `numLoops = 1` — reported; sets the per-loop marginal cost.
* **(e)** the T004 stage-probe table, printed not gated.
* **(f) The transition blocks — the arm S3.1's correction requires.** Two 512-sample blocks, each
  gated against the **absolute** 106 666 ns/block ceiling (not against `kBaseline` — a transition may
  cost more than steady state, just not more than the block period):
  * the block containing a **simultaneous multi-loop sleep edge** — drive to steady state, then
    `setNumLoops(6 → 1)` and time the block in which all five dropped gates settle at zero and
    `clearLoopAudio` runs five times inside one 64-sample control chunk;
  * the block containing a **rung-5 trap fire**, injected through the FR-048 probe.
  **Repeat both at 192 kHz**, where the block period is 2 666 667 ns and the ceiling is
  **26 667 ns** — that is where an O(buffer) regression shows first. Record the measured figures into
  plan S14.2.

**Pre-authorised levers, applied only if (a) misses (OQ-2):**

1. `std::tanh` → `FastMath::fastTanh` (`core/fast_math.h:65`), **under its own error-bound test**:
   `|fastTanh(x) − std::tanh(x)| < 5e-4` over `x ∈ [-6, 6]` on a 10 001-point grid, and
   `|fastTanh(x)| <= 1` everywhere. FR-042's bound is preserved exactly.
2. Stepped resonator retunes — **already the default** after OQ-1; banked in the baseline, not a
   further step.
3. Skip a coupling-pair smoother's advance once it is bit-exactly at its target (already specified in
   T011).

**If the total still exceeds 71 111 ns/block after levers 1–3, STOP and surface the measured
per-stage table.** Levers 4 (drop the resonator to a second `SVF` in Bandpass mode, losing the RT60
surface, `kMaxResonatorQ = 100` and SC-023 entirely) and 5 (default `numLoops` 6 → 5) are **user**
decisions. No agent may lower `kMaxLoops`, raise the budget, relax a threshold or shrink a workload.

**Verify — in isolation, nothing else executing:**

```bash
node tools/run-cpu-tests.js dsp_systems_tests 2>&1 | tee /tmp/fe_perf.log
```

A test that flips verdicts between runs is measuring the machine, not the code: re-run alone after the
machine idles before treating a miss as a defect.

---

## Group R — Integration gates

### T021 — Registration-completeness audit

**Files:** none edited (audit only; if it finds a gap, fix it in `dsp/tests/CMakeLists.txt` here).

* All four TU paths appear **exactly once** in the `dsp_systems_tests` source list between `:324` and
  the closing `)`.
* `unit/systems/feedback_ecology_nonfinite_test.cpp` appears **exactly once** in the `-fno-fast-math`
  block and in **no** other `set_source_files_properties()` call.
* `dsp/lint_all_headers.cpp` includes `<krate/dsp/systems/feedback_ecology.h>` exactly once.
* `build/windows-x64-release/bin/Release/dsp_systems_tests.exe "FeedbackEcology_*" --list-tests`
  reports a **non-zero** case count and lists every case name this file names: `ConstantsTable`,
  `ControlSurfaceClamps`, `ResonatorRing`, `Footprint`, `SampleRate`, `EntryPointContract`,
  `BlockPartition`, `TapEquivalence`, `DryIdentity`, `OutputClamp`, `WanderRateMapping`,
  `DelayMotion`, `SeedDeterminism`, `GainNormalisation`, `Governor`, `Dormancy`, `LoopCount`,
  `NoAllocation`, `NonFinite`, `CoherenceHelperSmoke`, `CrossLoopTransfer`, `DelayDriftClickFree`,
  `BoundednessSoak`, `LongEvolution`, `StageCostProbe`, `CpuBudget`.
* No case carries `[long]` that must stay in the per-push lane: `[long]` belongs to SC-001, SC-002,
  SC-003 and SC-021 **only** — never to the non-finite sentinel (SC-012), the bounded-grid/rate
  sentinels (SC-010, SC-011) or the state-format case (SC-008).

---

### T022 — Full-suite run and the SC-020 shared-component check

```bash
"$CMAKE" --build build/windows-x64-release --config Release \
    --target dsp_primitives_tests dsp_processors_tests dsp_systems_tests
for t in dsp_primitives_tests dsp_processors_tests dsp_systems_tests; do \
    build/windows-x64-release/bin/Release/$t.exe 2>&1 | tail -3; done

build/windows-x64-release/bin/Release/dsp_systems_tests.exe "FeedbackEcology_*" 2>&1 | tee /tmp/fe_all.log | tail -20

git diff --stat -- dsp/include/krate/dsp/systems/filter_feedback_matrix.h \
  dsp/include/krate/dsp/systems/feedback_network.h \
  dsp/include/krate/dsp/systems/flexible_feedback_network.h \
  dsp/include/krate/dsp/primitives/i_feedback_processor.h \
  dsp/include/krate/dsp/processors/multimode_filter.h \
  dsp/include/krate/dsp/processors/resonator_bank.h \
  dsp/include/krate/dsp/primitives/svf.h dsp/include/krate/dsp/primitives/biquad.h \
  dsp/include/krate/dsp/primitives/dc_blocker.h \
  dsp/include/krate/dsp/primitives/crossfading_delay_line.h \
  dsp/include/krate/dsp/primitives/delay_line.h \
  dsp/include/krate/dsp/primitives/smoother.h \
  dsp/include/krate/dsp/processors/envelope_follower.h \
  dsp/include/krate/dsp/processors/brownian_drift.h        # must print NOTHING
```

All three suites green, including the `[long]` set (run locally by default). The `git diff --stat`
must print **nothing** (FR-090, FR-092, SC-020). A failure here is never "pre-existing" — halt and fix
it.

---

### T023 — Portability, lints, clang-tidy (SC-019)

```bash
node tools/lint-layers.js && node tools/lint-odr.js && node tools/lint-nonfinite-symbols.js \
  && node tools/lint-float-bit-goldens.js && node tools/lint-arch-guarded-includes.js \
  && node tools/lint-simd-aligned-loadstore.js && node tools/check-portability.js

./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja 2>&1 | tee /tmp/fe_tidy.log
```

All clean, zero warnings. Specific traps this gate exists for:

* no `std::isnan` / `std::isinf` / `std::isfinite` anywhere in the header or any of the four TUs;
* no `constexpr std::log2` — Clang rejects the builtin extension MSVC and GCC accept, so the macOS and
  Linux legs break while Windows stays green;
* no narrowing in brace init — designated initialisers throughout;
* no bit-exact float golden;
* naming: classes PascalCase, methods camelCase, members trailing underscore, constants
  `kPascalCase`.

**MSVC-green proves nothing about the Linux and macOS legs.** `check-portability.js` only *compiles* —
if it is unavailable on this machine, syntax-check the new TUs and the header against libstdc++
(MSYS2 g++ or WSL) and record that in the transcript, the Phase-3 precedent.

Fix **all** clang-tidy warnings the change set produces, not only the ones in new code.

---

## Open items the build carries (each has a named owner above)

| # | Item | Owner task | Rule |
|---|---|---|---|
| **O-1** | `kInteractionMarginDb` (SC-002 (c)), provisional **1.5 dB** — nothing in the tree anchors it | T018 | Probe first, pin from the measurement. The shape assertions may never be weakened |
| **O-2** | SC-005 (a)'s counts above the floor (≥ 3 over 300 s; ≥ 4 within 120 s for loops ≥ 109 ms) | T010 | The floor — **≥ 2 distinct settled values for every loop** — may never move. If the shortest loop misses it, raise that loop's default wander fraction (FR-023's derivation), never the assertion |
| **O-3** | `kDefaultWetGainDb = 0.0f` is **unmeasured** (D-8, R-12) | T009 (SC-018 (b)), T013 (SC-022) | If the wet is unusably quiet or loud, add a `[.calibration]` case that prints the figure and record the constant the Phase-3 way — never nudge one |
| **O-4** | SC-004 may reach lever 4 or 5 | T020 | Both are **user** decisions. Stop and surface the measured per-stage table |
| **O-5** | SC-023's ±10 % band, if the measurement method proves it too tight | T015 | Re-pin from a recorded measurement; the **0.5 s floor** on loop 5 and the shape of the assertion may not move |
