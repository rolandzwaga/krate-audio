# Tasks: Vorago Phase 8 — Ecosystem Engine

**Spec:** `specs/vorago-phase8-ecosystem/spec.md` (1525 lines)
**Plan:** `specs/vorago-phase8-ecosystem/plan.md` (2029 lines) — **the S-numbers below refer to its
sections.** Every code shape a task needs is already written there and is deliberately **not**
re-typed here. An executor reads the cited S-section for the code, and this file for the order, the
file set, the failing test and the numbers.
**Prototype:** `specs/vorago-phase8-ecosystem/prototype/{ecosystem-sim.js, run.js, FINDINGS.md}` —
every formula in the plan is a transcription from `ecosystem-sim.js` at a cited line. When plan and
prototype disagree, the plan wins (it records the deviation in its S14).

**Deliverable**

* one new Layer 3 header `dsp/include/krate/dsp/systems/ecosystem_engine.h` (header-only, no `.cpp`)
* four new test TUs under `dsp/tests/unit/systems/`
* one test-local helper header `dsp/tests/unit/systems/ecosystem_metrics_test_helpers.h`
  (plan S10.1 / S14 D-D — no CMake edit; the enumerated list names `.cpp` only)
* **two** edits in `dsp/tests/CMakeLists.txt`
* **one** include line in `dsp/lint_all_headers.cpp` (plan S11.3 / S14 D-F)

**Test target:** `dsp_systems_tests` (all four new TUs).
**Regression set that must stay green (FR-090, SC-016):** `dsp_core_tests`, `dsp_primitives_tests`,
`dsp_processors_tests`, `dsp_systems_tests`, `dsp_effects_tests`.
**Plugin work:** none. Vorago's plugin starts at Phase 11; phases 1–10 are KrateDSP-only.

**Shipped headers amended: ZERO (FR-090).** `modulation_engine.h`, `modulation_matrix.h`,
`voice_mod_router.h`, `voice_mod_types.h`, `modulation_source.h`, `modulation_types.h`,
`envelope_follower.h`, `slow_event_scheduler.h`, `noise_organism.h`, `resonance_drift_network.h`,
`feedback_ecology.h`, `subharmonic_engine.h`, `bloom_engine.h`, `harmonic_cloud.h`,
`atmosphere_engine.h`, **and `core/random.h`** are read-only for the whole phase.
`git diff --name-only -- dsp/include/` must name `systems/ecosystem_engine.h` and nothing else, at
every point in the build.

---

## How to read this file

* Tasks are grouped into **ordered groups**. A group starts only when every task in the previous
  group is green: it builds warning-free, its own cases pass, and `dsp_systems_tests` still passes.
* `[P]` marks tasks that are parallel-safe **within their group**: their file sets are fully
  disjoint and every file they touch is new or owned solely by them.
* **Shared files, and the rule that follows from them.** This phase has exactly six files more than
  one task could want: the header `ecosystem_engine.h`, the four test TUs, and
  `dsp/tests/CMakeLists.txt`. **Every task that edits the header sits alone in its own group.** No
  two tasks in the same group touch the same TU. `dsp/tests/CMakeLists.txt` is touched by exactly
  two tasks (T001, and T022's read-only audit).
* Each task is **self-contained**: exact files, the failing test to write **first** (TU,
  `TEST_CASE` name, the numeric assertions), then the implementation intent with the plan section
  that carries the code shape, then the command that verifies it.
* Canonical order inside every task: **write the failing test → watch it fail → implement → zero
  compiler warnings → the test passes → `dsp_systems_tests` still passes.**
* **Test-only tasks** (T002, T006, T007, T012, T014, T018, T019, T020, T021, T022–T024) carry a
  named **falsification** instead of a fail-first step — a mutation or fixture inversion that must
  make the new assertion fail — because the behaviour they measure was implemented in an earlier
  task. A test-only task is not done until its falsification has been run and its result recorded
  in the compliance notes.
* **No commit tasks.** Commits happen outside this workflow.

### Three deliberate deviations from the requested layout, each forced by the repo or the plan

**1. CMake registration is T001, not a final task.** `dsp/tests/CMakeLists.txt`'s `dsp_systems_tests`
source list is **enumerated, not globbed** — verified this session: `add_executable(dsp_systems_tests`
at `dsp/tests/CMakeLists.txt:324`, the Phase-7 TU block at `:473-476`, the closing `)` at `:477`, and
the list's own comment says "an unregistered TU silently drops out of the build and its cases never
run". An unregistered TU compiles into nothing, so registering all four TUs up front is the only
ordering under which every later task's "run the suite" step proves anything. The final group still
carries a **registration-completeness audit** (T022).

**2. `dsp/lint_all_headers.cpp`'s include lands with the header, not before it.** That file is
compiled into the lint stub target; an include of a header that does not yet exist breaks the whole
build. It is edited inside T003, the task that creates the header (Phase-7 include at
`dsp/lint_all_headers.cpp:191`, the Layer-4 block opens at `:193`).

**3. Implementation tasks carry their own fail-first case; test-only tasks fill in the rest.** The
plan's S15 lists "implement, then test" per stage. Written that way a task has no failing test to
open with, which the canonical order forbids. Each implementation task below therefore opens with
the one criterion that is the natural observable of the code it adds (e.g. FR-021's
`predation = 0.5` case for the exchange stage, SC-021 for the denormal snap), and the remaining
criteria over that code are picked up by the test-only task in the next group.

### Conventions every task obeys

* Namespace `Krate::DSP`; classes PascalCase, methods camelCase, members trailing underscore,
  constants `kPascalCase`. Header banner carries `@par Layer: 3 (systems/). Dependencies: Layer 0 +
  stdlib ONLY.` and the real-time-safety line (plan S1.1).
* Every new case is tagged `[ecosystem_engine]`, plus `[long]` or `[.perf]` where the task says so:
  `TEST_CASE("EcosystemEngine_Xxx", "[ecosystem_engine]")`.
* **Finiteness is `Krate::DSP::detail::isFinite` / `isNaN` / `isInf`** (`core/db_utils.h:118`, `:99`,
  `:260`, and the `double` overload at `:125-129`) — **never** `std::isnan`/`std::isinf`/
  `std::isfinite`, in the header **or** in any test (`tools/lint-nonfinite-symbols.js` gates it,
  FR-083).
* **Non-finite values are built from bit patterns through a `volatile` sink** — never
  `std::numeric_limits<float>::quiet_NaN()`/`infinity()`, which fold to finite garbage on the
  macOS/Linux `-ffast-math` legs. Transcribable idiom:
  `dsp/tests/unit/systems/resonance_drift_network_nonfinite_test.cpp:149-155` (`makeNonFinite(bits)`
  / `makeNonFiniteDouble(bits)`; patterns `0x7FC00000`, `0x7F800000`, `0xFF800000`, and
  `0x7FF8000000000000` for the double). Only `ecosystem_engine_nonfinite_test.cpp` is compiled
  `-fno-fast-math`, so it is the **only** TU that may **assert IEEE semantics** on such a value;
  other TUs may *construct* them and assert on counters or on "the previous value still stands".
* **No bit-exact float goldens** anywhere (`tools/lint-float-bit-goldens.js`, roadmap line 546,
  SC-006 (d)). Where a cross-run comparison is not same-binary, use
  `tests/test_helpers/render_fingerprint.h` (`kSampleTolerance = 5.0e-4f`, `kMetricTolerance =
  2.5e-4`). The exact (`==`, `std::memcmp`) comparisons this phase **does** allow are all
  **same-binary structural identities**: SC-006 (a)/(c), SC-008, SC-014 (a)/(e), SC-020's
  `scale == 1.0`, SC-021's `cell == 0.0`, and FR-071's `delta == 0.0` clauses.
* Brace-initialised aggregates use **designated initialisers**
  (`EcosystemEngine::PrepareConfig{.agentCount = 24, .resourceCells = 96}`) — Clang errors on
  narrowing where MSVC does not (plan R-8, `resonance_drift_network.h:297-306`). Every literal
  carries its `f`/`u` suffix.
* Tests include `tests/test_helpers/allocation_detector.h` only; **never**
  `allocation_operator_overrides.h` — `dsp_systems_tests` already has its single owner and a second
  include is a duplicate-symbol link error.
* **The engine object is ~21.5 KB** (plan S9). Construct it as a `static`, a member, or through
  `std::make_unique` — **never** a plain stack local in a test, and in SC-007 construct it
  **outside** the `AllocationScope` or the allocation itself is counted.
* **Streaming statistics in every `[long]` case.** A `Trace` (plan S10.2) of 48 agents × 1800 s on a
  1 Hz grid is ~700 KB and is legal; a 28 800 s soak trace is **not** — SC-018 accumulates its
  late-window statistics incrementally and materialises only the final 600 samples.
* **Zero compiler warnings is part of every task's definition of done**, not a later cleanup.

### The stop-and-surface rule (FR-085, inherited verbatim from `resonance_drift_network_perf_test.cpp:57-64`)

**NON-NEGOTIABLE.** No implementing agent may lower `kMaxAgents`, raise the 53 333 ns/block ceiling,
relax a threshold, shrink a workload, widen a tolerance, or cut SC-001's 1000 configurations or 900 s
duration to make a figure fit. **Reduce cost, never move the line.** If a gated figure misses, the
build **stops** and surfaces the measured table for a user ruling — the route that amended Phase 2
(1 → 1.75 %) and Phase 5 (1 → 1.5 %).

Three specific applications, all normative:

* **Restating FR-085's per-block budget as a per-step budget is forbidden by name** — it is an 8×
  relaxation wearing a derivation (spec FR-085, plan S12.3 L6).
* **Levers L1–L3 are pre-authorised** (plan S12.3): the two-stage cutoff (S4.0), the
  `syncRate == 0` guard on the Kuramoto `sin` (A-7), and the `leakExponent == 1` fast path (S4.7).
  All three are exact — not one published number moves — and they are part of the design, written in
  from the start, not bolted on after a measurement.
* **Levers L4 (kernel LUT) and L5 (phase-sine LUT) are NOT pre-authorised** (plan S14 D-M). They
  replace formulas the spec states as normative (FR-012's `exp(−d²/2σ²)`, FR-050's and FR-035's
  `sin`). They go to the **user** with the measured table, exactly as L6 does. T016 is the task that
  owns that escalation; no earlier task may adopt them.

### Build and run commands (Windows; the full CMake path is mandatory)

```bash
CMAKE="C:/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "EcosystemEngine_*" 2>&1 | tail -20
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "[long]" 2>&1 | tail -20
# perf ALONE, nothing else running:
node tools/run-cpu-tests.js dsp_systems_tests
```

Capture slow runs to a log on the **first** run and inspect the log; never re-run a `[long]` batch
just to look at output.

---

## Group A — Registration and the ODR sweep

Two disjoint file sets: T001 owns `dsp/tests/CMakeLists.txt` and the four new TU stubs; T002 creates
and edits nothing. Both `[P]`.

### T001 [P] — Create the four TU stubs and register them

**Files to create**

* `dsp/tests/unit/systems/ecosystem_engine_test.cpp`
* `dsp/tests/unit/systems/ecosystem_engine_longrun_test.cpp`
* `dsp/tests/unit/systems/ecosystem_engine_perf_test.cpp`
* `dsp/tests/unit/systems/ecosystem_engine_nonfinite_test.cpp`

Each stub is `#include <catch2/catch_test_macros.hpp>` plus a one-line comment naming the criteria it
will own (plan S10.1 table). No `TEST_CASE` yet.

**Files to edit**

* `dsp/tests/CMakeLists.txt`, the enumerated `dsp_systems_tests` source list — append **after** the
  last Phase-7 TU line (`unit/systems/bloom_engine_nonfinite_test.cpp`, `:476`) and **before** the
  closing `)` at `:477`. The exact block, comment text included, is **plan S11 edit (1)**.
  **Re-read the file before splicing** — the plan records that an earlier draft cited `:474`/`:475`,
  which would have spliced the Phase-8 block into the middle of the Phase-7 list.
* `dsp/tests/CMakeLists.txt`, the `-fno-fast-math` block (opens `:569`
  `if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")`; the Phase-7 entry at `:920`;
  `PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"` at `:921`) — insert **exactly
  one** of the four TUs immediately before `:921`, with the comment from **plan S11 edit (2)**
  explaining why the other three stay out:

  ```cmake
          unit/systems/ecosystem_engine_nonfinite_test.cpp
  ```

  A file may appear in only one `set_source_files_properties()` call — this TU must **not** also
  appear in any other property block.

**Do not edit** `tests/test_helpers/CMakeLists.txt`: the helper header of T017 lives beside its TUs
and the enumerated lists name `.cpp` only (plan S11.4, precedents
`dsp/tests/unit/processors/arpeggiator_core_test_helpers.h`,
`dsp/tests/unit/systems/harmonic_cloud_pre_amendment_fingerprints.h`).

**Test first:** not applicable — this task creates the harness the later tests live in. Its own
falsification is the build: before the edit the four TUs are invisible to CMake and a `TEST_CASE`
added to any of them would never run.

**Verify**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe 2>&1 | tail -5
```

**Done when:** the suite builds warning-free, its pass count is unchanged, and all four TU paths
appear exactly once in the generated build files.

---

### T002 [P] — Re-run the ODR and near-name sweeps, record the transcript

**Files:** none created or edited. Output goes into the phase's compliance notes.

Run, verbatim, from the repo root, and paste the transcript with hit counts:

```bash
grep -rn "class EcosystemEngine"  dsp/ plugins/ tools/
grep -rn "struct EcosystemEngine" dsp/ plugins/ tools/
grep -rn "EcosystemEngineNonFiniteProbe" dsp/ plugins/ tools/
grep -rn "EcosystemEngineInspectProbe"   dsp/ plugins/ tools/
grep -rn "enum class AgentKind"   dsp/ plugins/ tools/
grep -rn "\(class\|struct\) Agent\b"         dsp/ plugins/ tools/
grep -rn "\(class\|struct\) AgentTable\b"    dsp/ plugins/ tools/
grep -rn "\(class\|struct\) ResourceField\b" dsp/ plugins/ tools/
grep -rn "\(class\|struct\) Habitat\b"       dsp/ plugins/ tools/
grep -rn "\(class\|struct\) EcosystemAgent\b" dsp/ plugins/ tools/
grep -rn "\(class\|struct\) EnergyPool\b"    dsp/ plugins/ tools/
grep -rn "Ecosystem" dsp/include dsp/tests plugins/ --include=*.h --include=*.cpp
```

**Expected (spec new-components table, plan S0.1): zero hits for every line.** The roadmap's
near-name hazard list (`ResonatorBank`, `FeedbackNetwork`, `NoiseGenerator`, `GranularEngine`,
`PatternScheduler`, roadmap lines 127–129) is *not* introduced or shadowed — confirm by reading the
include block T003 writes, not by grepping for those names.

**Falsification:** if **any** line returns a hit, **stop** and surface it before T003 runs. Two
classes with the same name in the same namespace is undefined behaviour, and this phase's whole ODR
claim is that it adds exactly **one** namespace-scope name to `Krate::DSP` (`EcosystemEngine`) plus
two forward declarations in `Krate::DSP::detail`.

**Done when:** the transcript is recorded with a zero-hit line for every grep.

---

## Group B — The header skeleton

### T003 — Create `ecosystem_engine.h` (declarations only) and wire it into the lint stub

**Files to create:** `dsp/include/krate/dsp/systems/ecosystem_engine.h`
**Files to edit:** `dsp/lint_all_headers.cpp`; `dsp/tests/unit/systems/ecosystem_engine_test.cpp`

**Write this test FIRST** (it fails to compile before the header exists, and fails at runtime if the
header ever grows a non-Layer-0 include):

`ecosystem_engine_test.cpp` → `TEST_CASE("EcosystemEngine_HeaderIncludesOnlyLayerZero",
"[ecosystem_engine]")`

* Locate the header from the TU's own path: `std::filesystem::path(__FILE__).parent_path() /
  ".." / ".." / ".." / "include" / "krate" / "dsp" / "systems" / "ecosystem_engine.h"`.
  If it cannot be opened, `FAIL("header not found at " + path)` — a moved file must surface, never
  silently pass.
* Read every line; for each line matching `#include <krate/dsp/`, `REQUIRE` the path segment after
  `krate/dsp/` begins with `core/`.
* `REQUIRE` at least **two** such lines were seen (`core/random.h`, `core/db_utils.h`), so a header
  with the include block deleted cannot pass vacuously.
* Also `REQUIRE` the file contains no `std::isnan`, `std::isinf`, `std::isfinite`, `#include <vector>`,
  `#include <memory>`, `new `, `malloc` token.

**Why a test and not a lint:** `tools/lint-layers.js` only forbids *upward* includes
(`tools/lint-layers.js:5-8`), and Layer 3 → Layer 1/2 is legal to it, so it structurally cannot see
FR-001's stricter "Layer 0 + stdlib only" rule (plan S14 D-J).

**Implement** — the header's complete declarative skeleton, code shapes in plan S1.1–S1.6:

* Include set exactly S1.1: `<krate/dsp/core/random.h>`, `<krate/dsp/core/db_utils.h>`, `<algorithm>`,
  `<array>`, `<cmath>`, `<cstddef>`, `<cstdint>`. Nothing else. `primitives/smoother.h` is **not**
  included — the wake ramp is four lines of arithmetic on the control-step grid (S6.2).
* Every constant of **S1.2** with its live `static_assert`s: `kMaxAgents = 48`, `kMinAgents = 1`,
  `kMaxResourceCells = 96`, `kNumKinds = 5`, `kMaxPairs = 1128` (+ `static_assert(kMaxPairs == 1128)`
  and `static_assert(kMaxAgents <= 255)`), `kControlChunkSamples = 64` (+ its `static_assert`),
  `kMinStepIntervalChunks = 1`, `kMaxStepIntervalChunks = 64`, `kDefaultStepIntervalChunks = 8`,
  `kMinUsableSampleRate = 8000.0`, `kDefaultSampleRate = 48000.0`, `kMinEnergyBudget = 1.0e-3`,
  `kMaxEnergyBudget = 1.0e3`, `kWakeSilenceEpsilon = 1.0e-6f`, `kGainRampMs = 50.0f`,
  `kOutputAnchor = 0.5`, `kMinAffinity = -2.0f`, `kMaxAffinity = +2.0f`,
  `kNeighbourWeightCutoff = 1.0e-6`, `kDenormalCellGuard = 1.0e-30`, `kUnitVectorEpsilon = 1.0e-9`,
  and **`kConfigKnobCount = 28`** with the "RAISE IT IN THE SAME COMMIT THAT ADDS A SETTER" comment
  (plan A-2 — T019's fuzz coverage `static_assert`s against it).
* The **salt table** of S1.6: `kSaltPositions = 0` … `kSaltResourceFill = 6`, `kSaltNextFree = 7`,
  marked **APPEND ONLY**, with the strictly-increasing `static_assert` and the load-bearing reason
  (`Xorshift32::seed()` substitutes its default for 0 — `core/random.h:72-74` — so two lanes hashing
  to 0 would collapse onto one stream).
* `enum class Kind : std::uint8_t { Partial = 0, Resonator, Noise, Feedback, Ghost }` with the
  **APPEND ONLY** note and the `static_assert(static_cast<std::size_t>(Kind::Ghost) + 1u ==
  kNumKinds)`.
* `struct PrepareConfig` — **exactly** FR-006's five prepare-time fields (S1.3), documented
  designated-initialisers only, with the narrowing rationale.
* The two probe forward declarations in `namespace Krate::DSP::detail`, each naming its **one**
  defining TU by path (plan R-11), and both `friend` lines at the bottom of the private section.
* The **complete public API of S1.4**, every method declared and stubbed: lifecycle, `processChunk`,
  the 23 runtime setters + getters (share-unit ones named `…Shares`, plan A-4/D-H),
  `setFreqRangeHz(lo, hi)` with two getters, `setAffinity(Kind, Kind, float)`, the sleep/wake/event
  trio, the indexed output surface, the prepare-time read-backs, `getControlStepCount()` (plan A-3),
  and the diagnostics. Stubs return the S8 neutral (`0.0f` / `0.0` / `0` / `Kind::Partial` /
  `false`); **knob getters return their S1.5 member value even in stub form** — they have no neutral.
* The **private SoA state block of S1.5, verbatim**: 9 persistent agent `double` arrays, 10 scratch
  `double` arrays, `wake_`/`gate_`/`output_` floats, `kind_`/`dormant_`/`divided_`/`cellTouched_`/
  `clampedSteps_`, `res_`/`cellPos_`/`pool_`, the pair scratch, the clock residues, the five
  counters, and `Xorshift32 driftRng_{1u}`. **No `Agent` nested struct** (plan S14 D-A).
* The `nextUnipolarD` / `nextBipolarD` / `rangeD` private statics of S1.6 with
  `kToDouble = 1.0 / 4294967295.0` and the comment that the range is **(0, 1]** because
  `Xorshift32::next()` returns `[1, 2^32−1]` (`core/random.h:51-55`). `core/random.h` is **not**
  modified (FR-090).

**The header text the spec makes normative — all of it, in this task**, because a later task will
not go back for prose:

| Where | Required text | Source |
|---|---|---|
| above the output surface | the FR-060 "intended use, not coupling" sentence naming roadmap lines 386–388 | plan S1.4 final bullet, quoted verbatim there |
| on `syncRate_` and above the Kuramoto block | **"synchronize is a macro, not a default rule."** + the −18 % activity measurement | FR-035, plan S4.2 |
| on the exchange `flow` line | the FR-021 `predation == 0.5f` trap paragraph, including "bit-identical to baseline, to every printed digit" | FR-021, plan S4.2 |
| on `leakExponent_` and above stage 7 | the FR-052 macro band ("above ~1.3 kills the ecosystem… macros must stay at or below 1.3") | FR-052, plan S4.7 |
| on the publication function | FR-062's quote from `feedback_ecology.h:2292-2296` and FR-070's ramp-quantisation note | plan S4.11 |
| on the pool update | FR-056's "the pool is never clamped" rationale | plan S4.10 |
| on `setAgentDormant` | FR-072's mechanism-level argument (the agent *is* the generator; conservation forbids excluding it) | plan S6.2 |
| on `processChunk` | `bloom_engine.h:899`'s named failure mode (`numSamples / kControlChunkSamples` per call) | plan S3 |
| class banner | the ≈ 21.5 KB footprint and "construct as a member or via `make_unique`, not a stack local" | plan S9 / R-9 |
| `setSeed` | "re-derives the initial state (a re-`prepare()` in effect), not re-seeding lanes in place" | spec Edge Cases, plan S2.4 |

**Files to edit — `dsp/lint_all_headers.cpp`:** after the Phase-7 include at `:191` and before the
Layer-4 block at `:193`:

```cpp
// Vorago Phase 8 (specs/vorago-phase8-ecosystem), FR-001
#include <krate/dsp/systems/ecosystem_engine.h>
```

This is **not** in the spec's FR-091 file list; plan S14 D-F records the correction. Without it
`run-clang-tidy.ps1 -Target dsp` never analyses the new header and SC-015's clang-tidy clause is
vacuous.

**Verify**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "EcosystemEngine_*" 2>&1 | tail -5
```

**Done when:** the header compiles into both `lint_all_headers.cpp` and the four TUs with zero
warnings, `EcosystemEngine_HeaderIncludesOnlyLayerZero` passes, and `node tools/lint-odr.js` and
`node tools/lint-layers.js` are clean.

---

## Group C — Lifecycle, the control clock, and the share-unit conversion

### T004 — `prepare()`, `initialiseState()`, `reset()`, `setSeed()`, `refreshDerivedScales()`, `processChunk()`

**Files to edit:** `dsp/include/krate/dsp/systems/ecosystem_engine.h`;
`dsp/tests/unit/systems/ecosystem_engine_test.cpp`

**Write this test FIRST:**

`TEST_CASE("EcosystemEngine_PrepareIsAConservingPartition", "[ecosystem_engine]")`

At the Appendix-A defaults (`agentCount = 32`, `resourceCells = 64`, `energyBudget = 1.0`,
`initialPoolFraction = 0.5`, `stepIntervalChunks = 8`), `prepare(48000.0, cfg)`, seed `0xC0FFEE`:

1. `|getTotalEnergy() − getEnergyBudget()| / getEnergyBudget() <= 1e-12` (FR-041's three-way split
   must close exactly to `double` rounding).
2. `Σ_i getAgentEnergy(i)` is within `1e-12` relative of `(1 − 0.5) · 1.0 = 0.5`.
3. **No cell is empty:** `getCellEnergy(k) > 0.0` for every `k ∈ [0, 64)`. Starting cells at zero
   gave every seed the same opening transient and cross-seed correlation 0.60 (FR-041,
   `ecosystem-sim.js:219-232`) — this assertion is the guard against re-introducing it.
4. `getPoolEnergy() > 0.0` and `getPoolEnergy() == getEnergyBudget() − Σagents − Σcells` to `1e-12`.
5. `Σ_k getCellEnergy(k) <= 0.5 · (getEnergyBudget() − Σagents) + 1e-12` (the
   `min(resSum, remaining · 0.5)` cap).
6. `getStepDurationSeconds() == 8 * 64 / 48000.0` exactly; `getSampleRate() == 48000.0`;
   `getControlStepCount() == 0`; `isPrepared() == true`; `getAllocatedBytes() == 0`.
7. Two instances prepared identically have **bit-identical** `getAgentEnergy/PositionX/PositionY/
   Phase` for all 32 agents and identical `getPoolEnergy()` (`std::memcmp` over a gathered array).

**Implement** (plan S2.1 numbered order, S2.2's pseudocode, S5, S3):

* `prepare()` steps 1–10 of S2.1 exactly, including the **sanitise-then-floor** sample-rate form
  (`bloom_engine.h:385`, definitions `:879-882`), the five clamps, `dt_`/`sqrtDt_`/
  `rampSteps_ = max(1, ceil(0.050 / dt_))`, `refreshDerivedScales()`, the kernel derivatives of
  S4.0, clearing **every** counter (Clarification Q8), and the step-8 rule that **`prepare()`
  re-derives state, never configuration** — the affinity matrix and all 23 rule knobs survive it.
* `initialiseState()` exactly as S2.2 (a)–(e): the **stratified** kind deal + seeded remainder +
  Fisher–Yates shuffle on the `kSaltKinds` stream (FR-011, Clarification Q6 — **not** the
  prototype's i.i.d. draw), the per-agent draw order `x, y, energy, phase, freq` matching
  `ecosystem-sim.js:192-206`, energy normalisation to `(1 − initialPoolFrac_) · energyBudget_`, the
  **seeded** resource fill with `cellPos_[k] = (k + 0.5)/resourceCells_` (FR-040's strip geometry),
  the `min(resSum, remaining · 0.5)` scaling, `pool_ = remaining − resTarget`, and the **gate snap**
  (no ramp at t = 0, `bloom_engine.h:399-400`) followed by one publication.
* `refreshDerivedScales()` of S5 — the FR-008 share→absolute conversion, called from `prepare()` and
  (in T005) from each of the four `…Shares` setters, **never per step**.
* `reset()` = re-run `initialiseState()` with the current seed and configuration, clear every counter
  and both clock residues, publish. `setSeed(s)` = `seed_ = s;` then exactly `reset()`.
* `processChunk()` verbatim from **S3** — both residues (`samplePhase_` within 64, `chunkPhase_`
  within `stepChunks_`) carried **across calls**; `numSamples == 0` returns immediately and draws
  from no RNG stream.
* `simulationStep()` for now does **only** `++stepCount_` and the stage-13 publication is deferred:
  mark it `// STAGES 2-13 LAND IN T008-T011` so the stub cannot be mistaken for the design.

**Verify:** build `dsp_systems_tests`, run `"EcosystemEngine_*"`.

**Done when:** the seven clauses pass, zero warnings, `dsp_systems_tests` green.

---

## Group D — The runtime setters and the getter contract

### T005 — 23 setters + getters, `setAffinity`, `setFreqRangeHz`, and the S8 neutral table

**Files to edit:** `dsp/include/krate/dsp/systems/ecosystem_engine.h`;
`dsp/tests/unit/systems/ecosystem_engine_test.cpp`

**Write this test FIRST:** `TEST_CASE("EcosystemEngine_SettersClampToRange", "[ecosystem_engine]")`
(spec plan S14 D-O — FR-064 specifies **three** normative setter behaviours and **no spec criterion
gated value clamping**; S7.3's whole containment argument leans on `sigmaSq_ > 0` *because*
`setKernelSigma` clamps to `[0.01, 0.35]`, and SC-001/SC-013 draw every knob **inside** its range so
nothing else ever probes past a bound).

Drive every knob from a **static table in the test** sharing the same Appendix-A bounds the header
uses. Three calls per row — `min − ε`, `max + ε`, one in-range value (ε = `1e-4` scaled to the
knob) — asserting the getter reports the bound (clamped) or the in-range value (stored), **exactly**:

| Knob | min | max | in-range probe |
|---|---|---|---|
| `kernelSigma` | 0.01 | 0.35 | 0.12 |
| `exchangeRate` | 0 | 3.0 | 1.0 |
| `predation` | 0 | 1 | 0.55 |
| `preyFloorShares` | 0 | 1.6 | 0.5 |
| `capacityShares` | 0.32 | 32 | 16.0 |
| `leakRate` | 0 | 1.0 | 0.06 |
| `leakExponent` | 1.0 | 2.5 | 1.2 |
| `moveRate` | 0 | 0.5 | 0.20 |
| `maxSpeed` | 0.001 | 0.05 | 0.03 |
| `forageRate` | 0 | 0.05 | 0.01 |
| `crowding` | 0 | 0.2 | 0.05 |
| `crowdingRadius` | 0.005 | 0.05 | 0.02 |
| `syncRate` | 0 | 0.5 | 0.1 |
| `cellCapacityShares` | 0.32 | 12.8 | 3.2 |
| `regenRate` | 0 | 1.0 | 0.05 |
| `grazeRate` | 0 | 3.0 | 0.75 |
| `feedRate` | 0 | 1.0 | 0.0 |
| `satiationShares` | 0 | 16 | 4.0 |
| `appetiteDepth` | 0 | 1 | 0.8 |
| `freqLoHz` | 0.0005 | 0.005 | 0.0015 |
| `freqHiHz` | 0.006 | 0.05 | 0.018 |
| `freqDrift` | 0 | 0.0002 | 0.00004 |

Plus:

* `setAffinity` at `+3.0f` → `getAffinity == +2.0f` and at `−3.0f` → `−2.0f`, at **four** `Kind`
  pairs including a diagonal one; and an out-of-range `Kind` (reachable only by a cast) is a silent
  no-op whose getter returns `0.0f`.
* `setFreqRangeHz` **inversion**: call with `hi < lo`, both individually in range, and assert
  `getFreqHiHz() >= getFreqLoHz() + 1e-4f` holds afterwards (S1.4's ordering rule — FR-013's hard
  clamp inverts otherwise).
* The whole table is run **before** and **after** `prepare()` — S8 (iii): knob getters have **no**
  unprepared neutral and must report their Appendix-A default, not `0.0f`.
* An `AllocationScope` wraps the whole case: `getAllocationCount() == 0`.

**Implement** (plan S1.4 notes, S5, S8):

* All 23 setter/getter pairs, each with FR-064's three behaviours in this order:
  `if (!detail::isFinite(v)) return;` (previous value stands) → `std::clamp` to the Appendix-A
  range → store. The four `…Shares` setters additionally call `refreshDerivedScales()`.
* `setAffinity(from, to, v)` exactly as S1.4: index guard (silent no-op), finiteness guard, then
  `std::clamp(v, kMinAffinity, kMaxAffinity)`. **The matrix is not symmetrised** — FR-031 is per
  entry and the pair pass reads `affinity_[kind_i][kind_j]` for the `i`-side force only.
* `setFreqRangeHz(lo, hi)` — one setter for two knobs, clamps each then enforces `hi >= lo + 1e-4f`.
* `setKernelSigma` recomputes the S4.0 kernel derivatives (`sigmaSq_`, `twoSigmaSq_`,
  `invTwoSigmaSq_`, `cutDistSq_`) — **not** per step.
* The **S8 three-class getter contract** implemented and documented: (i) indexed getters neutral on
  an out-of-range index only; (ii) state getters neutral on an unprepared object; (iii) knob getters
  and the prepare-time read-backs have **no** neutral and always report the current configuration.

**Verify:** build; run `"EcosystemEngine_SettersClampToRange"`.

**Done when:** every row of the table passes exactly, before and after `prepare()`, with zero
allocations and zero warnings.

---

## Group E — The first two test-only tasks (disjoint TUs)

### T006 [P] — Behaviour TU: the unprepared object, the clock, `reset()`, and the kind histogram

**Files to edit:** `dsp/tests/unit/systems/ecosystem_engine_test.cpp` only.

Four cases; all behaviour is already implemented by T004/T005, so each carries a **falsification**.

**1. `EcosystemEngine_UnpreparedIsNeutral`** (SC-007 (b)). On a **default-constructed** instance,
asserting S8's three classes **separately**:

* state getters: `isPrepared() == false`, `getAllocatedBytes() == 0`, `getPoolEnergy()`,
  `getTotalEnergy()`, `getStepDurationSeconds()`, `getEnergyEntropy()` all `== 0.0`, every counter
  and `getControlStepCount() == 0`, `getPairInteractionCount() == 0`;
* **knob** getters at their **Appendix-A defaults**, not `0.0f`: `getKernelSigma() == 0.03f`,
  `getPredation() == 0.55f`, `getLeakExponent() == 1.0f`, `getSyncRate() == 0.0f`,
  `getFeedRate() == 0.0f`, `getSatiationShares() == 0.0f`, `getAffinity(Partial, Partial) == -1.0f`,
  `getAffinity(Partial, Resonator) == +0.45f`, and the prepare-time read-backs at their
  `PrepareConfig` defaults (`getAgentCount() == 32`, `getResourceCells() == 64`,
  `getEnergyBudget() == 1.0`, `getStepIntervalChunks() == 8`, `getInitialPoolFraction() == 0.5`,
  `getSampleRate() == 48000.0`);
* `processChunk(0)`, `processChunk(1)`, `processChunk(64)`, `processChunk(1'000'000)` before
  `prepare()`: no crash, no state change, `getControlStepCount() == 0`;
* every setter callable before `prepare()` and its value then visible through its getter;
* every **indexed** accessor at indices `agentCount`, `agentCount + 1`, `SIZE_MAX` — before **and**
  after `prepare()` — returns its S8 (i) neutral (`0.0f` / `0.0` / `0` / `Kind::Partial` / `false`),
  no crash, no allocation.

*Falsification:* change one knob getter to return `0.0f` when `!prepared_` — the case must fail.

**2. `EcosystemEngine_BlockPartitionInvariance`** (SC-008). 1 000 000 samples delivered four ways —
(i) one call, (ii) 512-sample blocks, (iii) 64-sample chunks, (iv) a seeded irregular partition
(1, 7, 513, 63, 4096, 2, …) — from four identically-prepared instances at seed `0xC0FFEE`:
**bit-identical** on every agent `double` (`energy`, `x`, `y`, `phase`), every `getCellEnergy(k)`,
`getPoolEnergy()`, **and** `getControlStepCount()` (all four `== 1'000'000 / 512 = 1953`).

*Falsification:* reset `samplePhase_` at the top of `processChunk` — arms (iii) and (iv) must fail.

**3. `EcosystemEngine_StepIntervalBand`** (SC-010 (b)). For `sampleRate ∈ {44100, 48000, 96000}` ×
`stepIntervalChunks ∈ {4, 8, 16}` (nine cells), feed `duration = 60.0 s` worth of samples in
512-sample blocks and assert `getControlStepCount()` is within **one** of
`duration · sampleRate / (stepIntervalChunks · 64)`. Then call `prepare()` a **second** time at a
different rate and assert `getStepDurationSeconds()` and `getControlStepCount()` both reflect the new
rate with no stale `dt_` and both residues zeroed.

**4. `EcosystemEngine_KindAssignmentIsStratified`** (FR-011 / FR-060, plan S14 D-O). For
`agentCount ∈ {1, 4, 5, 32, 47, 48}` × 8 seeds:

* `Σ_k getAgentCountOfKind(k) == getAgentCount()`;
* `max_k − min_k <= 1` (a deal plus a one-at-a-time remainder differs by at most one);
* every kind's count `>= 1` whenever `agentCount >= 5`; below 5, zeroes are permitted (FR-011 names
  that edge);
* `getAgentKind(i)` is `Kind::Partial` for out-of-range `i`;
* **the shuffle**: across the 8 seeds at `agentCount = 48`, the per-index kind vector differs from
  the deal order (`kind_[i] == Kind(i / base)`) in at least **7 of 8**, and the mean over seeds of
  the Spearman rank correlation between agent index and kind index is within **±0.25 of 0** — a
  dealt-but-unshuffled implementation scores ≈ +1.0.

*Falsification:* delete the Fisher–Yates loop in `initialiseState()` — the last two clauses must
fail. Restore it.

**5. `EcosystemEngine_ResetReproducesPrepare`** (SC-006 (c)). Instance A prepared at seed `0xC0FFEE`;
instance B prepared identically, stepped 10 000 control steps, then `reset()`. Assert A and B are
**bit-identical** on every agent `double`, every cell, the pool, every counter, `getControlStepCount()
== 0`, and — the clause the plan adds to SC-006 (a) and that applies here too — on
`getAgentOutput(i)` and `getAgentWake(i)` for every agent.

**Verify:** build; run `"EcosystemEngine_*"`.

---

### T007 [P] — Non-finite TU: setter rejection and the divisor-knob extremes

**Files to edit:** `dsp/tests/unit/systems/ecosystem_engine_nonfinite_test.cpp` only.
This TU is the **only** one compiled `-fno-fast-math` (T001's second CMake edit), so it is the only
place IEEE semantics may be asserted.

**1. `EcosystemEngine_NonFiniteInputsRejected`** (SC-009 (a)). Build NaN / +Inf / −Inf **from bit
patterns through a `volatile` sink** — `makeNonFinite(0x7FC00000)`, `(0x7F800000)`, `(0xFF800000)`,
and `makeNonFiniteDouble(0x7FF8000000000000)`; idiom at
`resonance_drift_network_nonfinite_test.cpp:149-155`. Feed each to **every** setter (all 23 knobs,
`setAffinity`, both `setFreqRangeHz` arguments, `setAgentWake`, `perturbAgent`) and assert through
the matching getter that the **previous value stands**, exactly. `std::numeric_limits` is forbidden
by name here — it folds to finite garbage under `-ffast-math` on the macOS CI leg.

**2. `EcosystemEngine_DivisorKnobExtremes`** (SC-009 (c)). Call `prepare()` with each of:

* `energyBudget` ∈ {`0.0`, `−1.0`, `1e-300`, `1e300`, NaN, +Inf} → `getEnergyBudget()` reports the
  FR-005 clamp (`1e-3` for the first four low cases, `1e3` for `1e300`, and the `sanitise` default
  `1.0` then clamped for the non-finite ones);
* `agentCount` ∈ {`0`, `SIZE_MAX`} → `getAgentCount()` ∈ {1, 48};
* `resourceCells` ∈ {`0`, `SIZE_MAX`} → `getResourceCells()` ∈ {1, 96};
* `stepIntervalChunks` ∈ {`0`, `SIZE_MAX`} → `getStepIntervalChunks()` ∈ {1, 64};
* **sample rate, both halves**: non-finite (NaN, ±Inf, bit-pattern-built) **and** below the floor —
  `{0.0, −48000.0, 1.0, 7999.0}` — each asserting `getSampleRate() == 8000.0` and
  `getStepDurationSeconds() == getStepIntervalChunks() * 64 / 8000.0`. Only the sub-floor arm
  exercises S2.1 step 1's `std::max` floor, on which `dt_`, `sqrtDt_` and `rampSteps_` all depend.

In every cell, step 1000 control steps and assert every `getAgentOutput(i)` is finite (by
`detail::isFinite`) and in `[0, 1]`. Without this arm FR-061's division by `energyBudget` is
reachable at zero through documented API.

*Falsification:* remove the `kMinEnergyBudget` clamp from `prepare()` — the `energyBudget = 0.0` cell
must produce a non-finite output and fail.

---

## Group F — The step, part 1: pair pass and exchange

### T008 — Stages 2–4 (pair pass, exchange pass two, appetite) + the `InspectProbe`

**Files to edit:** `dsp/include/krate/dsp/systems/ecosystem_engine.h`;
`dsp/tests/unit/systems/ecosystem_engine_test.cpp`

**Define `Krate::DSP::detail::EcosystemEngineInspectProbe` in this task**, in
`ecosystem_engine_test.cpp`, with the **eleven** accessors of plan S7.4 (`pairCount`, `pairFlow`,
`pairI`, `pairJ`, `scale`, `outflow`, `divided`, `divisions`, `lastSnap`, `clampedSteps`, `setPool`).
This TU is its **only** definition — a second definition anywhere is an ODR violation the linker may
not diagnose (plan R-11).

**Write these tests FIRST:**

**1. `EcosystemEngine_ExchangeScaleIsWellFormed`** (SC-020 — a probe-level criterion *because*
SC-001 provably cannot discriminate it: a sign-reversed pair flow is still antisymmetric, so
conservation and boundedness hold exactly while the exchange rule runs backwards). Three arms,
10 000 steps each: (a) the defaults; (b) `agentCount = 1` (no pair exists at all); (c)
`preyFloorShares = 1.6` (so most agents sit **below** the floor) with `kernelSigma = 0.01` (so most
agents have **no** surviving pair). Per step, per agent `i`:

* `scale(i)` is finite (`detail::isFinite`) and in **`[0, 1]`**;
* where `want_i = outflow(i) * getStepDurationSeconds()` is exactly `0.0`:
  **`scale(i) == 1.0` exactly AND `divided(i) == false`** — the pair of clauses a
  `min(1, spare/want)` paraphrase fails on both counts (it evaluates `0/0` → NaN **and** it performs
  the division);
* for every applied pair `p`, the **sign of the applied flow equals the sign of `pairFlow(p)`**, or
  the applied flow is exactly `0.0`.

**2. `EcosystemEngine_ExchangeDisabledAtPredationHalf`** (FR-021, plan S14 D-O). Defaults,
`setPredation(0.5f)`, 2000 steps: at every step `InspectProbe::pairCount() > 0` (pairs still survive
the `w < 1e-6` cutoff, so the zero is the `(1 − 2·predation)` factor and **not** an empty pair list)
**and** every `pairFlow(p)` for `p ∈ [0, pairCount())` is **exactly `0.0`**. Also assert the run
stays finite and `getConservationViolationCount() == 0`. **Control arm** at `predation = 0.55`
asserts at least one non-zero `pairFlow` — so the case is demonstrated to discriminate rather than
assumed to.

**Implement** (plan S4.0, S4.2, S4.3, S4.4):

* `wrapDelta()` (`ecosystem-sim.js:240-246`) and the **two-stage cutoff** of S4.0 —
  `if (d2 > cutDistSq_) continue;` then `w = std::exp(−d2 · invTwoSigmaSq_);` then the **verbatim**
  normative `if (w < kNeighbourWeightCutoff) continue;`. `cutDistSq_ = twoSigmaSq_ ·
  13.815510557964274 · (1 + 1e-9)`; the pre-test is monotone-equivalent and strictly conservative, so
  the interacting **set** is exactly the prototype's. This is lever **L1** and it is part of the
  design, not a later optimisation.
* Stage 2 exactly as S4.2: one pair pass that **records only** — exchange flow, affinity force
  (scaled by the *neighbour's* energy), crowding repulsion, and the Kuramoto term behind the
  `syncRate_ > 0` guard (lever **L2**, plan A-7 — the product is zero either way).
* Stage 3 exactly as S4.3 — **write the ternary verbatim**:
  `scale_[i] = (want > 0.0 && want > spare) ? (spare > 0.0 ? spare/want : 0.0) : 1.0`, with
  `++exchangeDivisions_` and `divided_[i] = true` on the division branch only, and each pair applied
  scaled by `min(scale_[i], scale_[j])`. `divided_` is cleared over `[0, agentCount_)` at the top of
  the stage.
* Stage 4 exactly as S4.4 — `appetite_[i] = max(0, 1 + appetiteDepth_·sin(2π·phase_[i]))`, read from
  the phase at the **start** of the step, accumulating `appetiteSum`.

**Verify:** build; run `"EcosystemEngine_Exchange*"`.

**Done when:** both cases pass including their control arms, zero warnings, suite green.

---

## Group G — The step, part 2: the economy

### T009 — Stages 5–8 (regrowth, grazing + foraging + denormal snap, feed + leak, integrate)

**Files to edit:** `dsp/include/krate/dsp/systems/ecosystem_engine.h`;
`dsp/tests/unit/systems/ecosystem_engine_test.cpp`

**Write these tests FIRST:**

**1. `EcosystemEngine_NegativePoolIsNotAbsorbedByAnAgent`** (SC-009 (d); plan S4.5's blocker
regression — one step, so untagged). Prepare at the defaults (`feedRate_ == 0`), settle **200**
steps, snapshot every `getAgentEnergy(i)`; `InspectProbe::setPool(engine, −0.25 · energyBudget)`;
step **once**. Bound the legal per-agent movement by re-running the identical step from the same
snapshot with `pool_ = 0.0` and requiring the two per-agent deltas to agree to **`1e-12` relative**;
in particular assert **agent 0** did not absorb ≈ the full deficit. Repeat the whole case at
`feedRate_ = 0.5`, where the deficit is *split* by appetite share rather than dumped on agent 0, so
the case is not accidentally specific to the `-0.0 > avail` path.

*Why this is a blocker and not an edge case:* with `avail = pool_` initialised **inside**
`if (avail > 0)`, a negative pool skips the branch and a negative `avail` walks into stage 7, where
`if (globalFeed > avail) globalFeed = avail` stops being a `min` and becomes an **assignment** —
at `feedRate_ = 0` the product is `0.0 · negative = −0.0` and `−0.0 > avail` is true, so agent 0 is
charged the whole deficit, stage 8's zero clamp returns it, and FR-056's "a negative pool is a
readable, latched signal" is quietly undone by destroying agent 0's state, with **no counter
recording it**.

**2. `EcosystemEngine_DenormalCellSnapConserves`** (SC-021 — unobservable to SC-001, whose 1e-9
*relative* gate on a budget of order 1 sits fourteen decades above a ≤ 1e-30 per-cell quantity).
`cellCapacityShares` at its Appendix-A minimum (0.32), `grazeRate` at its maximum (3.0),
`regenRate = 0`. Step until a cell goes sub-guard, then assert:

* `getCellEnergy(k)` is **exactly `0.0`** (not subnormal, not `1e-234`);
* **no** cell in the field holds a subnormal value (test each with `std::fpclassify`-free bit
  inspection or by `c == 0.0 || std::abs(c) >= 1e-300`);
* `getPoolEnergy()` increased by **exactly** `InspectProbe::lastSnap()` over that step.

*An implementation that drops the snapped energy on the floor passes every other criterion in this
spec and fails here.*

**Implement** (plan S4.5, S4.6, S4.7, S4.8):

* **Stage 5** exactly as S4.5, and **hoist the floor outside the branch**:
  `avail = (pool_ > 0.0) ? pool_ : 0.0;` **before** `if (avail > 0)`. This is FR-054's single
  withdrawal balance serving **both** withdrawal sites, and the reason is on the line in the header,
  not only in the plan. Proportional regrowth: sum every cell's desired
  `regenRate_ · (cellCapAbs_ − res_[k]) · dt_` **first**, then scale by `share = min(1, avail/want)`.
  **Index-order regrowth is forbidden by name** — it made cells 0–19 take every joule and 70 % of the
  habitat a permanent desert (`FINDINGS.md:192-200`).
* **Stage 6** exactly as S4.6 — demand-scaled grazing with the **touch list** (`cellTouched_`,
  `touchCount`) replacing the prototype's per-cell allocation; `hunger` from `satiationAbs_`;
  foraging gradient `(res_[k]/cellCapAbs_) · w · (d/sigmaSq_)` behind `forageRate_ > 0`;
  `d = wrapDelta(cellPos_[k] − x_[i])` — **x separation only** (FR-040's strip geometry). **The
  fixed-amount-split-by-share form is forbidden by name** — it made a lone grazer eat the same
  whatever its appetite said and every configuration settled at a starvation fixed point
  (`FINDINGS.md:202-207`).
* **Stage 6b** — the FR-043 denormal snap (plan S4.6, placed here by plan S14 D-B): a **two-sided**
  magnitude test, `res_[k] = 0.0`, the amount added to `poolDelta` **and** recorded in
  `lastDenormalSnap_` (cleared at the top of the stage).
* **Stage 7** exactly as S4.7 — the global feed drawn from `avail` only, the `dE_[i] * dt_`
  conversion (stage 3's flows are per-second rates), `influx = graze_[i] + globalFeed`, the
  **`leakExponent_ == 1.0f` fast path** (lever **L3**, exact: `pow(x,1) == x`), and
  `poolDelta += leak − globalFeed` — only the global feed is debited from the pool; the grazed part
  already left the cells, and debiting it twice destroys energy silently.
* **Stage 8** exactly as S4.8 — both clamps **return their delta to the pool**.

**Verify:** build; run `"EcosystemEngine_NegativePool*"` and `"EcosystemEngine_Denormal*"`.

---

## Group H — The step, part 3: motion and the pool

### T010 — Stages 9–12 (movement, frequency drift, phase, pool update)

**Files to edit:** `dsp/include/krate/dsp/systems/ecosystem_engine.h`;
`dsp/tests/unit/systems/ecosystem_engine_test.cpp`

**Write this test FIRST:** `TEST_CASE("EcosystemEngine_ConservesOverAShortRun", "[ecosystem_engine]")`

Defaults, three seeds (`0xC0FFEE`, `0xC0FFEF`, `0x5EED`), **50 000** control steps each. Sampling
every 93rd step (~1 Hz at the default `dt`):

* `|getTotalEnergy() − getEnergyBudget()| / getEnergyBudget() <= 1e-9` (SC-001 (c)'s bound —
  the prototype measured 2.4e-15 to 1.9e-13, so this is four decades of margin);
* `getConservationViolationCount() == 0`;
* `getNonFiniteContainmentCount() == 0` (asserted **separately**, FR-056/FR-083 — one counter
  carrying two meanings cannot say which failure mode fired);
* every `getAgentEnergy(i)`, `getAgentPositionX/Y(i)`, `getAgentPhase(i)`, `getCellEnergy(k)` and
  `getPoolEnergy()` finite by `detail::isFinite`;
* every position in `[0, 1)` and every phase in `[0, 1)` (torus wrap, FR-012/FR-014);
* every `getAgentEnergy(i) >= 0.0` and `<= capacityAbs_` (both clamps of stage 8).

**Implement** (plan S4.9, S4.10):

* Stage 9 — velocity `(moveRate_·fx + forageRate_·forage_x, moveRate_·fy) · dt_`, **slew-limited** to
  `maxSpeed_ · dt_` in magnitude, then `wrap01(t) = t − std::floor(t)` on both axes. Foraging enters
  **x only** (FR-040).
* Stage 10 — the bounded OU drift, with the draw **UNCONDITIONAL** (plan A-8): `nz` is drawn from
  `driftRng_` every step for every agent, and only its *application* is guarded on `freqDrift_ > 0`.
  Drawing conditionally makes the RNG stream's position a function of a runtime knob, which breaks
  SC-006 and SC-008 the moment a test or a macro toggles it (`bloom_engine.h:914-916` rule).
* Stage 11 — `phase_[i] = wrap01(phase_[i] + (freq_[i] + dPhase_[i]) · dt_)`.
* Stage 12 — `pool_ += poolDelta;` then `if (pool_ < 0.0) ++conservationViolations_;`. **The pool is
  never clamped.** The counter increments **once per step whose end-of-step pool is negative** — not
  once per transition — and the header carries that choice with its reason so a later reader does not
  "fix" it.

**Verify:** build; run `"EcosystemEngine_ConservesOverAShortRun"`.

---

## Group I — Publication, the wake ramp, dormancy, and the event hook

### T011 — Stage 13, `setAgentWake`/`setAgentDormant`, `perturbAgent`

**Files to edit:** `dsp/include/krate/dsp/systems/ecosystem_engine.h`;
`dsp/tests/unit/systems/ecosystem_engine_test.cpp`

**Write this test FIRST:** `TEST_CASE("EcosystemEngine_DormancyMatchesHouseRule",
"[ecosystem_engine]")` — SC-014 (a)(b)(c):

* **(a)** `setAgentWake(i, 0.0f)` and `setAgentDormant(i, true)` on two identically-seeded instances
  produce **identical** `getAgentOutput(i)` trajectories over 2000 control steps (bit-identical,
  every step, every agent).
* **(b)** A wake 0 → 1 reaches its target in exactly
  `rampSteps = max(1, ceil(0.050 / getStepDurationSeconds()))` control steps, **tolerance ±1 step**,
  monotone non-decreasing, with no single step larger than `1/rampSteps` of full scale. Asserted at
  `stepIntervalChunks` = **1, 8 and 64**. At 8 that is 5 steps = 53.33 ms; at **64** (4096 samples =
  85.3 ms at 48 kHz) one step already exceeds 50 ms, so `rampSteps == 1` and the test asserts the
  **single-step jump explicitly** rather than an unobservable ramp. **A "50 ms ± 1 ms" form is
  forbidden by name** — FR-062 recomputes the published value only at a control step, so the measured
  duration is quantised to {42.7, 53.3} ms and can never land inside 50 ± 1 ms.
* **(c)** An output driven `<= 1e-6` publishes **exactly `0.0f`** (FR-063) — both through
  `setAgentWake(i, 1e-8f)` (the setter's snap) and through a near-zero *energy* (the publication's
  snap). Both snaps exist and are not redundant.

**Implement** (plan S4.11, S6.1, S6.2, S6.3):

* Stage 13 verbatim from S4.11: `++stepCount_`; the gate ramp toward
  `target = dormant_[i] ? 0.0f : wake_[i]` in `stepGain = 1.0f/rampSteps_` increments; the
  normalisation **with the gate inside it** —
  `raw = kOutputAnchor · energy_[i] · agentCount_ / energyBudget_ · gate_[i]` (`clamp(raw·gate)` and
  `clamp(raw)·gate` differ, and only the former makes a dormant agent's output exactly 0); the
  **upper-rail** counters `clampedSteps_[i]` and `outputClampEngagements_` (Clarification Q4 —
  **a published `0.0f` from dormancy, wake gating or the FR-063 snap is a designed state and is
  never counted**); the FR-063 epsilon snap at the source.
* `getAgentClampedStepFraction(i) = stepCount_ ? float(double(clampedSteps_[i]) / double(stepCount_))
  : 0.0f`.
* `setAgentWake` / `setAgentDormant` exactly as S6.1 — neither advances the simulation nor touches
  `gate_`; the ramp target is recomputed at the next publication, which is the whole point of FR-062.
* **Dormancy gates the OUTPUT, not the simulation** (FR-072): a dormant agent keeps grazing,
  exchanging and leaking. The mechanism-level argument is already in the header from T003; this task
  makes the code match it.
* `perturbAgent` exactly as S6.3, **with the corrected negative branch** (plan S14 **D-L** — the
  spec's own FR-071 formula is defective and this is a blocker, not a nicety):

  ```cpp
  const double delta = (a >= 0.0)
      ? std::min(a * (capacityAbs_ - energy_[i]), std::max(0.0, pool_))
      : std::max(a * std::max(0.0, energy_[i] - preyFloorAbs_), -energy_[i]);
  ```

  Without the inner `std::max(0.0, …)`, an agent **below** the refuge floor takes a *negative* inner
  term, `a × negative` is positive, `max` selects it, and a "take energy" call **gives** the agent up
  to `|a|·preyFloorAbs_` out of an unguarded pool — driving `pool_` negative. At steady state the
  pool *is* empty (agents hold ~96 % of the budget), so this is the **normal** case, not a corner,
  and SC-001 (d)'s `getConservationViolationCount() == 0` would fail by construction.

**Record this in the compliance notes:** `spec.md` FR-071 carries the same defect and should be
corrected in the same pass, or the two documents disagree on a line an implementer will copy verbatim
(plan Review notes, item 1).

**Verify:** build; run `"EcosystemEngine_Dormancy*"`.

---

## Group J — Publication criteria (behaviour TU)

### T012 — SC-014 (d)(e), SC-019, SC-006 (a)(b)(d), and the `perturbAgent` edges

**Files to edit:** `dsp/tests/unit/systems/ecosystem_engine_test.cpp` only.

**1. `EcosystemEngine_OutputsAreHeldBetweenSteps`** (SC-014 (e), FR-062 — plan S14, and the one
criterion that reads FR-062 directly). Both bit-identity criteria are doubles-scoped, so an
implementation that recomputed the published value on **every** `processChunk` call would keep
identical doubles and pass SC-006 (a) and SC-008 unchanged. At `stepIntervalChunks = 8` (512 samples
per step), start a wake ramp 0 → 1 so the published value is **in motion** (a held-vs-republished
distinction is invisible on a settled output), then deliver the step's 512 samples as **16 calls of
32 samples**: after each of the first 15, `getAgentOutput(i)` and `getAgentWake(i)` are
**bit-unchanged** from the value published at the last step boundary, for every agent; only the call
that crosses the boundary may change them. Repeat with `numSamples = 1` × 512.

**2. `EcosystemEngine_DormantAgentsStayInTheEconomy`** (SC-014 (d) — the one place the spec argues
*against* the cross-cutting Dormancy rule, so it carries the strongest available evidence). Half the
population dormant for **1800 simulated seconds** at the defaults: SC-001's conservation clauses
still hold, **and** the dormant agents' mean per-agent activity `std(e_i)/grandMean` over the late
600 s window is **≥ 0.30** and within **0.5×–2×** of the awake population's in the same run.
"Their energies still move" is **not** the criterion — one ULP of a neighbour's rounding would
satisfy it, including in an implementation that freezes the dormant agent entirely.
*(Uses `lateWindow()` from T017's helper header — if this task runs before T017, code the two
statistics file-locally and delete them when T017 lands. Preferred: run this clause after T017.)*

**3. `EcosystemEngine_OutputAnchorIsScaleInvariant`** (SC-019 — the consumer contract Phase 10's
depth mapping rests on; SC-009 only asserts outputs stay finite and in `[0,1]`, which a wrongly
scaled anchor would also satisfy). `energyBudget` ∈ {0.1, 1.0, 10.0} × `agentCount` ∈ {24, 32, 48}
(nine cells) × 3 seeds × 1800 s:

* the late-window **mean of `getAgentOutput` over the population is 0.5 ± 0.05**;
* the population's late-window output range (5th–95th percentile) agrees across all nine cells to
  **±0.05 absolute**;
* **also report** (`WARN`) `Σ energy_ / energyBudget` per cell — the mean output is
  `0.5 · (Σe)/B`, so a cell outside the band immediately says whether the *anchor* or the agents'
  *share of the budget* moved (plan R-5; at steady state agents hold ~96 %, so the expected mean is
  ~0.48).

**4. `EcosystemEngine_DeterministicUnderSeed`** (SC-006). (a) Two instances, same seed, same binary,
**100 000** control steps: `memcmp`-equal on every agent `double` (energy, x, y, phase, freq), every
cell, the pool, **and explicitly `getAgentOutput(i)` and `getAgentWake(i)` for every agent** — a
doubles-only comparison leaves FR-062's published surface outside both bit-identity criteria.
(b) Seeds `n` and `n+1`: adjacent-seed **entropy** correlation `|ρ| <= 0.2` (prototype −0.02), and
per-agent cross-seed `|corr|` under SC-004 (b)'s relative bound (deferred to T018, which owns the
within-run floor). (d) **No bit-exact float golden is checked in**: any non-same-binary comparison
uses `render_fingerprint.h`'s `kSampleTolerance = 5.0e-4f` / `kMetricTolerance = 2.5e-4`;
`node tools/lint-float-bit-goldens.js` must pass.

**5. `EcosystemEngine_NoAllocationAfterPrepare`** (SC-007 (a)). Engine constructed **outside** the
scope (21.5 KB — `std::make_unique` inside would count as an allocation and fail the criterion for
the wrong reason). `AllocationScope` (`tests/test_helpers/allocation_detector.h:111`) around
`prepare()`, **10 000** `processChunk` calls, **every** setter, `reset()`, `setSeed()` and
`perturbAgent()`: `getAllocationCount() == 0`, `getAllocatedBytes() == 0`.

**6. `EcosystemEngine_PerturbAgentConservesUnderFuzz`** (SC-001 (d)'s targeted edges — the coverage
a random schedule reaches only by luck, and the **D-L regression**). Defaults, after 5000 settling
steps, assert per call that `getTotalEnergy()` is **bit-unchanged** and
`getConservationViolationCount()` does not advance, for:

* an agent at exactly `capacity` with `+1.0f` → `delta == 0.0`, energy **and** pool bit-unchanged;
* an agent at energy `0` with `−1.0f` → `delta == 0.0`, pool bit-unchanged;
* **an agent strictly below `preyFloorAbs_` with `amount < 0`** → `delta == 0.0` **exactly** and
  `getPoolEnergy()` **bit-unchanged** — this is the D-L regression; the *un*corrected FR-071 formula
  pays the agent `+|a|·preyFloorAbs_` out of the pool and fails here;
* a probe-injected **negative** `pool_` (`InspectProbe::setPool`) with `amount > 0` → pays nothing,
  and the violation is not deepened;
* an out-of-range index → silent no-op, total bit-unchanged;
* a non-finite amount (bit-pattern built) → rejected, total bit-unchanged.

*Falsification:* revert `perturbAgent`'s negative branch to the spec's uncorrected form — clause 3
must fail. Restore.

**Verify:** build; run `"EcosystemEngine_*"` (≈ 30 s expected; if SC-019's nine cells push a single
case past ~15 s, tag it `[long]` and move it to the longrun TU as a **recorded** deviation, plan
S14 D-K).

---

## Group K — Non-finite containment

### T013 — The guard ladder and the repair rule (FR-083)

**Files to edit:** `dsp/include/krate/dsp/systems/ecosystem_engine.h` only.

**Write the failing test in T014** — it lives in the `-fno-fast-math` TU and needs the
`NonFiniteProbe`, which T014 defines. This task's fail-first is therefore T014's case run against the
unguarded engine: **write T014's `EcosystemEngine_NonFiniteStateIsContained` first, watch it fail,
then implement here.** (The two tasks are split only because they touch different files; run them
back to back.)

**Implement** (plan S7.1, S7.2, S7.3) — **stage 1, at the TOP of every step, before any read**:

* the ladder of S7.2 over agents (energy, x, y, phase, freq), cells, and the pool, using
  **`detail::isFinite(double)`** (`core/db_utils.h:125-129`) only;
* **repair, not abandonment**: a non-finite agent's energy is reset to `meanShare_`, its position,
  phase and frequency to `x0_`/`y0_`/`phase0_`/`freq0_` — the values `prepare()` produced for **that
  index** under the current seed — and its `gate_` snapped to its steady target so no half-ramp is
  left behind; a non-finite cell to `0.0`; a non-finite pool to `0.0`;
* **the conserved total restored exactly**: `pool_ += (energyBudget_ − postRepairTotal)`. Plan S14
  **D-C** records why FR-083's stated arithmetic ("the difference between the pre-repair and
  post-repair totals") is unimplementable — NaN minus anything is NaN — and why this is the only form
  that can be written. Intent preserved.
* `++nonFiniteContainments_` once per containment event, and **the step then proceeds normally**.
  "Abandon the write and carry on" is not containment: the offending value stays in place, the next
  step's guard fires again, and with FR-062 holding outputs between steps the component would freeze
  at its last published values **forever** while every finiteness assertion passed.

Also add the S7.3 comment block recording why no other rung is needed (every divisor is guarded:
`energyBudget_ >= 1e-3`, `agentCount_ >= 1`, `resourceCells_ >= 1`, `dist = sqrt(d2) + 1e-9 > 0`,
`demandSum > 0` tested, `want > 0` tested, `appetiteSum > 0` tested, `sigmaSq_ > 0` **because**
`setKernelSigma` clamps to `[0.01, 0.35]` — T005's `SettersClampToRange` is what keeps that true).

**Verify:** build; run T014's case.

---

## Group L — Containment criteria

### T014 — Non-finite TU: the probe and SC-009 (b)

**Files to edit:** `dsp/tests/unit/systems/ecosystem_engine_nonfinite_test.cpp` only.

**Define `Krate::DSP::detail::EcosystemEngineNonFiniteProbe` here** — its **only** definition —
with `injectAgentEnergy`, `injectCellEnergy`, `injectPool` (plan S7.4).

`TEST_CASE("EcosystemEngine_NonFiniteStateIsContained", "[ecosystem_engine]")` (SC-009 (b)):

Prepare at the defaults, settle 1000 steps, then inject — separately, one arm each — a non-finite
agent energy, a non-finite cell energy and a non-finite pool (bit-pattern built). After the **next**
step:

* every `getAgentOutput(i)` is finite and in `[0, 1]`;
* `getNonFiniteContainmentCount()` incremented by exactly 1;
* `getConservationViolationCount()` **unchanged** — asserted separately, because one counter with
  two meanings cannot say which failure mode fired;
* `|getTotalEnergy() − getEnergyBudget()| / getEnergyBudget() <= 1e-9` (FR-083's repair rule
  re-establishes the invariant in one step rather than abandoning it);
* then **1000 further steps produce finite, in-range outputs that CHANGE** — at least one agent's
  published output differs from its value at the containment step. This clause is what fails an
  implementation that merely abandons the write and freezes forever at its last published values
  while every finiteness assertion passes.

*Falsification:* replace the repair with "skip this agent this step" — the last clause must fail.

**Verify:** build; run the non-finite TU's cases.

---

## Group M — The measurement that can change the phase

### T015 — Perf TU: SC-011 and the stage cost probe

**Files to edit:** `dsp/tests/unit/systems/ecosystem_engine_perf_test.cpp` only.
**Run it ALONE**, nothing else executing — no build, no other suite, no parallel agent
(`node tools/run-cpu-tests.js dsp_systems_tests`). A test that flips verdicts between runs is
measuring the machine, not the code.

Copy the measurement idiom and the **stop-and-surface rule verbatim** from
`dsp/tests/unit/systems/resonance_drift_network_perf_test.cpp:52-88` into this TU's header comment.

**1. `EcosystemEngine_CpuBudget` `[.perf]`** (SC-011). Nanoseconds per **512-sample block at
48 kHz**; one block period is **10 666 667 ns**, so FR-085's 0.5 % ceiling is **53 333 ns/block**.
Best-of-25 trials × 500 blocks after 400 warm-up blocks. Worst-case rule configuration:
`agentCount = 48`, `resourceCells = 96`, `kernelSigma = 0.35` (so **every** pair survives the
cutoff), `syncRate` on, `leakExponent > 1`. **Two gated arms at the same ceiling:**

* **(a)** `stepIntervalChunks = 8` — one step per block;
* **(b)** `stepIntervalChunks = 1` — one step per 64-sample chunk, **8× the step rate and 8× the
  cost**, reachable through the documented `PrepareConfig` and the case a Phase-10 CPU regression
  would hit. Gating only (a) would verify one eighth of the cost the API allows.

A percent-of-core figure is **reported, never asserted**.

**2. `EcosystemEngine_StageCostProbe` `[.perf]`** — `WARN`-reported; `REQUIRE`s only that every
figure is finite and `> 0`. Breaks the step into **pair loop / grazing loop / integrate+movement /
publication**, at `stepIntervalChunks` 1, 8 and 64, plus the defaults configuration **and an
all-dormant configuration** (FR-072 predicts all-dormant is **not** cheaper — the probe is where that
prediction is confirmed or found wrong).

**The plan projects (a) to miss by ~10 % and (b) by ~9×** (plan S12.2), which is stated in advance on
purpose: the Phase-3 precedent is that the plan projected the over-budget shape and the probe
confirmed it, which is how the phase avoided discovering it at the end.

**Capture the full output to a log on the first run** and read the log; never re-run a perf batch to
look at output.

**Done when:** both cases have run alone on an idle machine and the measured per-arm table is
recorded in the compliance notes — **whether or not it passes**.

---

## Group N — The lever decision

### T016 — Decide the lever list from the measured table (and escalate if needed)

**Files:** none, unless the user authorises L4/L5 — in which case this task edits
`dsp/include/krate/dsp/systems/ecosystem_engine.h` (alone in its group) **and**
`dsp/tests/unit/systems/ecosystem_engine_test.cpp`.

Read T015's measured table and apply plan **S12.3** in order:

* **L1, L2, L3 are already in the code** (T008's two-stage cutoff and `syncRate == 0` guard, T009's
  `leakExponent == 1` fast path). They are exact; no number moves; nothing to decide. Confirm all
  three are present.
* If **(a)** and **(b)** are both inside 53 333 ns/block: record the table, **stop here**, no
  escalation.
* If either misses: **L4 (kernel LUT) and L5 (phase-sine LUT) are NOT pre-authorised.** They replace
  formulas the spec states as normative (FR-012's `w = exp(−d²/2σ²)`, FR-050's appetite `sin`,
  FR-035's Kuramoto `sin`) and the spec's Assumption 1 rests on them. **Put the measured table to
  the user** with the projected effect of each lever and await sign-off — the same route as L6.
* **L6** is FR-085's named escalation when cost cannot be reduced further: propose **narrowing
  FR-082's minimum** (likely `kMinStepIntervalChunks = 4`, projected ~28 000 ns/block), which is a
  **spec amendment** decided by the user. Raising the ceiling, restating the budget per *step*,
  lowering `kMaxAgents`, or exempting the cheap end are all **forbidden by name**.

**If L4 and/or L5 are adopted**, three things land in the **same commit** as the lever (plan S14 D-M):

1. `TEST_CASE("EcosystemEngine_ApproximationTablesAreAccurate", "[ecosystem_engine]")` in the
   behaviour TU, asserting **directly against `std::exp`/`std::sin`**: over 100 001 uniform samples
   of `u ∈ [0, 13.8155]`, `max |LUT(u) − exp(−u)| / exp(−u) <= 2.3e-5` with both endpoints exact;
   over 100 001 uniform samples of `φ ∈ [0, 1)`, `max |LUT(φ) − sin(2πφ)| <= 1.0e-5` with
   `φ ∈ {0, 0.25, 0.5, 0.75}` asserted against their exact values; plus the table populated after
   `prepare()` at three sample rates and unchanged by `reset()`.
   **"Re-measure SC-002/SC-004" is NOT the gate** — their ±30 % and ≤ 0.35 margins cannot see a
   2e-5 error, correct *or* incorrect, so a mis-built table (wrong index scale, off-by-one at entry
   1024, table built before `prepare()` knows σ) is invisible to them.
2. Plan S9's conditional footprint row moved into the main ledger (≈ 21.5 KB → ≈ 29.7 KB with one
   LUT, ≈ 37.9 KB with both) and the header's stack-local line restated against the new figure.
3. The deviation recorded in the compliance notes as plan S14 D-M.

**Done when:** the table is recorded and either (i) both arms pass with L1–L3, or (ii) the user has
ruled on the escalation and the ruling is recorded.

---

## Group O — The shared metric helper

### T017 — Create `ecosystem_metrics_test_helpers.h`

**Files to create:** `dsp/tests/unit/systems/ecosystem_metrics_test_helpers.h`
**Files to edit:** none. **No CMake edit** — the enumerated lists name `.cpp` only, and a test-local
header beside its TUs is house-legal with two precedents
(`dsp/tests/unit/processors/arpeggiator_core_test_helpers.h`,
`dsp/tests/unit/systems/harmonic_cloud_pre_amendment_fingerprints.h`). Plan S10.1 / S14 **D-D**.

Namespace `Krate::DSP::TestUtils::Eco`. **All statistics in `double`.**
`tests/test_helpers/statistical_utils.h`'s `computeMean`/`computeVariance`/`computeStdDev` are
**`float`-only** (`:41`, `:59`, `:76`) and a `float` std/mean over 1800 samples of a 0.03-magnitude
signal loses the discrimination SC-002's 0.30 gate needs — the helper is read and **deliberately not
consumed** (plan S14 D-E).

Contents, each a transcription with its prototype line (plan S10.2):

* `struct Trace { std::size_t agents; std::vector<std::vector<double>> energy, output; double
  sampleHz; }` and `Trace runTrace(EcosystemEngine&, double seconds, double sampleHz = 1.0)`.
  Test-side heap is legal; the engine's own RT-safety is what SC-007 gates.
* `meanD`, `stdDevD`, `pearsonD`, `autocorrD(series, lag)` (`ecosystem-sim.js:606-614`).
* `Liveness lateWindow(const Trace&, double windowSeconds = 600.0)` (`:688-716`): per agent
  `s_i = std(e_i)/grandMean` with `grandMean` = mean over agents of `mean(e_i)`, **floored at
  1e-12**; `lateActivity = mean_i s_i`; `lateFrozen = count(s_i < 0.02)`;
  `latePairCorr = mean_{i<j} |pearson(e_i, e_j)|`.
* `bool hasShortCycle(std::span<const double>)` (`:724-757`, SC-005): scan `lag = 1 …` until
  autocorrelation first falls **below 0.2** → `decorrLag`; if none within `len/2`, **no cycle** (one
  slow trend). Otherwise the maximum autocorrelation over `[decorrLag, len/2)` must be `<= 0.8`.
  **Guarded escape:** a series whose sample variance over the window is exactly 0 **returns true
  (cycle) outright** rather than taking the escape — a railed constant output also never decorrelates,
  and this criterion would otherwise award it a pass. The naïve "maximum autocorrelation over all
  lags ≥ 10 s" form is **forbidden by name**: for any smooth signal that maximum always sits at the
  shortest lag scanned, and it reported "limit cycle: 0.909 at lag 9.0 s" for a run whose scan merely
  started at 9 s.
* `bool verdictAlive(const Trace&)` — **SC-002's verdict function, which is NOT its defaults gate**:
  over the last 600 s of a run of ≥ 900 s, alive iff `lateActivity >= 0.10` **and**
  `lateFrozen <= 0.25 · agents` **and** no agent's **output** series has a short cycle. Clause (iii)
  runs on **output**, never on entropy (Clarification Q3) — the one place the C++ deliberately
  differs from `ecosystem-sim.js:576-583`, whose cycle clause ran on `hSeries`.
* `std::size_t distinctPositions(const EcosystemEngine&, int dp = 2)` — SC-002 (c)'s metric, positions
  rounded to 2 dp.

**Falsification:** feed `hasShortCycle` a pure sine of period 60 s sampled at 1 Hz over 1800 s — it
must return `true`; feed it a monotone ramp — it must return `false`; feed it a constant — it must
return `true` (the guarded escape). Record all three.

**Verify:** the header compiles into both consuming TUs with zero warnings. **`std::span` and
`std::array` CTAD are the portability risk here** — run the WSL g++ probe (`reference_wsl_linux_
verification`) or at minimum `node tools/check-portability.js` before declaring done.

---

## Group P — The defaults gates

### T018 — Behaviour TU: SC-002 (a)–(d) and SC-004 (a)(b)

**Files to edit:** `dsp/tests/unit/systems/ecosystem_engine_test.cpp` only.

**Read this first.** FR-011's kind draw is **stratified**, not the prototype's unconstrained i.i.d.
draw, so **every prototype reference figure below is a comparison baseline and must be re-measured**.
A measured deviation is a **finding to surface under FR-085's stop-and-surface rule**, with the
per-kind histogram attached, never a threshold to move.

**1. `EcosystemEngine_LateWindowLiveness`** (SC-002 (a)(b)). 1800 s, three seeds (`0xC0FFEE`,
`0xC0FFEF`, `0x5EED`), defaults; all three must pass:
`lateActivity >= 0.30` (prototype **0.44** — a ~30 % margin, not a re-derivation) and
`lateFrozen == 0` **exactly** (prototype 0 of 32), **not** the verdict function's 25 %. Print the
measured activity, frozen count and per-kind histogram.

**2. `EcosystemEngine_AgentsDoNotCollapseSpatially`** (SC-002 (c)).
`distinctPositions(engine, 2) >= 0.8 · agentCount` after 1800 s (prototype 31 of 32), **plus a
`crowding = 0` control arm asserted to FAIL that bound** (prototype **6** of 32). The control arm is
mandatory: the ablation prices crowding removal at −2 % activity and −2 % correlation, "within noise
in 2-D", so SC-002 (a) and SC-004 **provably cannot** discriminate FR-032 and a positional metric is
the only thing that can.

**3. `EcosystemEngine_OutputsAreNotRailed`** (SC-002 (d)) — **windowed, not cumulative.**
`getAgentClampedStepFraction(i)` is cumulative since `prepare()`, so reading it once at the end
measures the whole 1800 s run and *dilutes* an implementation that rails only late — precisely the
case this criterion exists to catch. Compute the fraction from `InspectProbe::clampedSteps(i)` and
`getControlStepCount()` read at **both** late-window boundaries and differenced:
`(clamped_end − clamped_start) / (steps_end − steps_start)`. **Mean over agents ≤ 0.05; no single
agent > 0.25.** Transcribe the measured C++ figure into the test comment.

*Why this clause exists:* (a)–(c) are stated on raw energy `e_i` because that is the conserved
quantity every rule acts on, but the only value a consumer ever sees is FR-061's clamped output,
which saturates at twice the mean share. Without (d) a configuration whose published outputs are
constant could pass (a) while `std(e_i)/grandMean` reported its railed agent as the liveliest in
the run.

**4. `EcosystemEngine_AgentsDecorrelate`** (SC-004 (a)). 1800 s at the defaults, last 600 s: mean
pairwise `|corr(e_i, e_j)| <= 0.35` (prototype 0.18; the single-global-pool failure regime this
guards against measured **0.84–0.99**).

**5. `EcosystemEngine_SeedsProduceDifferentVoices`** (SC-004 (b)) — **relative, never absolute**
(Clarification Q7). Over **900 s** runs on a fixed ~1 Hz grid, on agent **energies**: the
**within-run floor** is the mean `|corr(e_i, e_j)|` between different agents of the **seed-0** run
over the **full 900 s** (not the last-600-s window (a) uses). Across **8 seeds**, the mean per-agent
cross-seed `|corr|` (same quantity, same grid, same duration) must be **≤ 1.5 × that floor**.
Reference at the shipped defaults: cross-seed **0.13** against a floor of **0.12**.
**The absolute form is forbidden by name**: these signals decorrelate over ~150 s, so a 900 s window
holds only a handful of independent samples and unrelated slow signals correlate high by chance —
judged against a fixed 0.5 threshold the *passing* configuration was declared "SEED-BLIND". **The
round-1 figures (0.61 at 900 s, 0.50 at 3600 s) are struck** — they were measured under
no-longer-shipped defaults.

**6. `EcosystemEngine_HeaderIncludesOnlyLayerZero`** — already written in T003; confirm it still
passes after every header edit.

**Verify:** build; run `"EcosystemEngine_*"`; capture to a log. Projected ~20 s total (plan S10.5).
If any single case measures > ~15 s, tag it `[long]` and move it to the longrun TU as a **recorded**
deviation (plan S14 D-K).

---

## Group Q — The fuzz harness

### T019 — Longrun TU: `RuleConfig`, both boxes, and SC-012

**Files to edit:** `dsp/tests/unit/systems/ecosystem_engine_longrun_test.cpp` only.

SC-012 is built **first** because it gates the other two fuzz criteria: it is the assertion that the
harness cannot silently stop testing a rule. The prototype's first fuzzer omitted `predation`,
`capacity`, `leakExponent` and the whole resource field, so `predation` sat at its **0.5** default —
the exact value that zeroes the exchange rule — and reported "0 unbounded / 1000" while never
visiting the regime already known to break conservation.

**Build** (plan S10.3):

* `struct RuleConfig` mirroring **all 28 Appendix-A knobs** (the affinity matrix as one
  `affinityMagnitude` driving `randomAffinity`'s range, matching `run.js:283-296`), plus
  `applyTo(EcosystemEngine&)` which calls `prepare()` with the five prepare-time fields and then
  every runtime setter.
* the **static knob table** `constexpr std::array<Knob, EcosystemEngine::kConfigKnobCount> kKnobs`
  with the `static_assert(kKnobs.size() == EcosystemEngine::kConfigKnobCount, "a knob was added to
  the engine without a fuzz-coverage entry")`.
* `constexpr std::array<const char*, 2> kExemptHostile{ "energyBudget", "stepIntervalChunks" };`
  `constexpr std::array<const char*, 3> kExemptSane { "energyBudget", "stepIntervalChunks",
  "feedRate" };`
  **Each array is sized to its contents — no sentinel, no padding to a common length.** Clause (c)
  is an unconditional loop over every element and a `nullptr` entry would be `strcmp`'d: UB, and on a
  hardened runtime a crash in the `[long]` lane rather than a test failure, in the one test whose
  whole purpose is that nothing is exempted by omission or by typo.
* the **hostile box** (`run.js:302-332`) and the **sane box** (`run.js:338-377`), with the three
  documented departures of plan S10.3: `dimensions` dropped (FR-012 fixes 2-D, so the prototype's
  500/500 covers a **superset**); the four share-unit knobs drawn over their **Appendix-A share
  ranges**; the prototype's meta-RNG seeds (`0xf0f0f0` hostile, `0x5a5e0001` sane) kept so the
  batches are comparable, while the comparison itself stays **statistical, never per-config**
  (`Xorshift32::nextUnipolar()` is `float` in C++, `double` in JS).

**`TEST_CASE("EcosystemEngine_FuzzCoverageIsComplete", "[ecosystem_engine][long]")`** — SC-012's
three clauses, each a hard assertion:

* **(a)** the table length equals `kConfigKnobCount` (the `static_assert` — adding a setter without
  raising the constant is a lie the author has to write, and raising it without extending the table
  fails to compile);
* **(b)** for every knob **not exempt**, `min < max` across the batch, or the test **fails
  outright**. *(Note: plan S14 **D-G** — the spec's "the number of numeric fields in `PrepareConfig`"
  would assert coverage of five knobs while the failure it exists to prevent concerns a **runtime**
  knob. The full 28-knob set is asserted instead. This strengthens the criterion.)*
* **(c)** every exemption name is asserted present in `kKnobs`, by an **unconditional** loop over
  every element of both arrays.

**Falsification:** delete one knob's write from the hostile box generator — clause (b) must fail
naming that knob. Restore.

---

## Group R — The boundedness batch

### T020 — Longrun TU: SC-001 (a)–(d)

**Files to edit:** `dsp/tests/unit/systems/ecosystem_engine_longrun_test.cpp` only.

`TEST_CASE("EcosystemEngine_HostileFuzzStaysBounded", "[ecosystem_engine][long]")`

**1000 seeded hostile configurations × 900 simulated seconds** (= **84 375 control steps each**,
~8.4e7 for the batch) plus a **sub-batch of 50 configurations at 1800 s**. One engine instance
reused across configurations — FR-065's counters clear on `prepare()`, which is why Clarification Q8
added that guarantee.

**Clause (d)'s seeded `perturbAgent` schedule runs INSIDE this batch, throughout every
configuration** (plan S14 **D-P**) — that is SC-001's letter, and it is also the cheapest reading: a
separate batch would double the 93 M steps and blow the budget, while a 100-config sub-batch would be
a silent 10× shrink of a spec-stated workload. Schedule: index drawn over `[0, 2·kMaxAgents)` so
out-of-range is hit; amount over `[−2, +2]` so FR-071's `clamp(−1, 1)` is exercised past both ends;
NaN/±Inf amounts built **from bit patterns through a `volatile` sink**; fired on a seeded ~1 Hz
schedule so the per-config cost rises by **< 2 %**.

Per configuration assert:

* **(a)** **zero** non-finite values in any agent energy, position, phase or the pool — tested by
  `detail::isFinite(double)` (bit pattern), never `std::isnan`;
* **(b)** `getConservationViolationCount() == 0` (the pool never went negative) **and, separately,**
  `getNonFiniteContainmentCount() == 0` — the two failure modes must not be confusable;
* **(c)** `|getTotalEnergy() − getEnergyBudget()| / getEnergyBudget() <= 1e-9` at every sampled step
  (sample every 93 steps, ~1 Hz);
* **(d)** (a), (b) and (c) still hold with the perturb schedule running.

**Prototype reference under the same rules at 900 s: 500/500 with 0 non-finite, 0 pool-negative,
0 unbounded, drift 2.4e-15–1.9e-13.** A regression in either conservation guard (FR-023's two-pass
exchange, FR-054's single withdrawal balance) fails **here first**.

**Runtime budget — part of the criterion.** The batch must complete in **≤ 30 minutes**
single-threaded, Release; the test **prints its own wall clock**. Projection: ~25.5 min (plan S10.5).
If it misses, **FR-085's stop-and-surface rule applies: the config count (the roadmap's own 1000),
the duration (the 900 s the 500/500 reference was measured at) and the perturb schedule's coverage
are NOT to be shrunk** — reduce per-step cost, or put the measured wall-clock table to the user.
A Debug build is 10–50× slower and will never fit; the `[long]` lane is Release-only.

**Capture to a log on the first run.**

---

## Group S — The remaining `[long]` criteria

### T021 — Longrun TU: SC-003, SC-005, SC-013, SC-017, SC-018

**Files to edit:** `dsp/tests/unit/systems/ecosystem_engine_longrun_test.cpp` only.
All five use T017's helper header. Projected total ~11 min (plan S10.5).

**1. `EcosystemEngine_LivenessIsDurationStable` `[long]`** (SC-003). The same configuration at
**600 / 900 / 1200 / 1800 / 3600** simulated seconds × 3 seeds (15 cells): `verdictAlive` true in
every cell, **and** the spread of `lateActivity` across the five lengths, stated as
**`(max − min)/mean < 0.20`**. The statistic is written out because "varies by < 20 %" has three
incompatible readings that differ by up to 2× on the prototype's own numbers. Prototype:
0.430 / 0.434 / 0.433 / 0.442 / 0.459 → **0.066**. *This criterion exists because round 1's "best"
configuration was a decaying transient whose verdict flipped between 1200 s and 1800 s.*

**2. `EcosystemEngine_NoShortLimitCycle` `[long]`** (SC-005). 1800 s at the defaults; for **each**
agent's `getAgentOutput` series, `hasShortCycle(series) == false`. The escape clause is guarded: a
series whose late-window sample variance is exactly 0 **fails outright**. Prototype reference: worst
post-decay peak **0.175 at 557 s**.

**3. `EcosystemEngine_SaneBoxLiveness` `[long]`** (SC-013). **500** configurations from the **sane**
box × **900 s**: **≥ 70 %** alive under SC-002's *verdict function*, and **0** unbounded. Prototype
reference **83.0 % (415/500), 0 unbounded** — but **that figure was produced by the prototype's own
`liveness()`, whose cycle clause ran on the entropy series, not on per-agent output as the corrected
verdict function requires** (Clarification Q3). A divergence from 83.0 % is a **finding to surface**,
not a threshold to move. The test additionally **reports** the alive fraction per tercile of each
knob, so Phase 10 inherits the death predictors rather than rediscovering them.

**4. `EcosystemEngine_RefugeFloorPreventsPredationCliff` `[long]`** (SC-017). Defaults with
`predation` swept over **{0.6, 0.7, 0.85}** × 3 seeds × 1800 s: `lateFrozen == 0` at every point, and
no agent's energy sits at 0 for any part of the late window. **A `preyFloorShares = 0` control arm at
`predation = 0.7` must FAIL that bound** (the prototype measured about a third of the agents parked
at zero permanently there — "a cliff 0.05 away from the default").

*The control arm is not optional.* At the **defaults** the ablation shows that removing the floor
**raises** activity (0.522 vs 0.44, **+18 %**), so a missing or mis-scaled `preyFloor` makes SC-002
*greener*, not redder, and SC-013 randomises `preyFloor` alongside every other knob and gates only an
aggregate. Without SC-017, an implementation that silently dropped FR-022 would pass **every other
criterion in this spec**.

**5. `EcosystemEngine_OvernightSoak` `[long]`** (SC-018 — the theme-level constraint: "a drone
instrument that can run away or die overnight is broken by definition"). **28 800 simulated seconds
(8 hours) × 3 seeds = 2 700 000 control steps per seed.** Assert SC-001's (a), (b) and (c) throughout
(sampled ~1 Hz, **streaming** — do not materialise the full trace), **and** SC-002's defaults gate
(a) `lateActivity >= 0.30` and (b) `lateFrozen == 0` measured over the **final 600 s** window. No
other criterion in this spec reaches a drone-realistic duration — SC-001 runs 900 s, SC-003 tops out
at 3600 s.

**Verify:** `build/windows-x64-release/bin/Release/dsp_systems_tests.exe "[long]" 2>&1 | tee log`,
read the log.

---

## Group T — Integration

Three disjoint concerns: T022 owns `dsp/tests/CMakeLists.txt` (read-only unless it finds a gap);
T023 and T024 create and edit nothing. All `[P]`.

### T022 [P] — CMake registration audit (the single registration task)

**Files:** `dsp/tests/CMakeLists.txt` — **read-only unless a gap is found**, in which case this task
fixes it and nothing else does.

Assert, by reading the file and the generated build files:

1. All **four** TU paths appear exactly once in the enumerated `dsp_systems_tests` list, between the
   Phase-7 block and the closing `)` — and the list's "ENUMERATED, not globbed" comment plus the
   per-TU criteria map from plan S11 edit (1) are present.
2. **Exactly one** of them — `unit/systems/ecosystem_engine_nonfinite_test.cpp` — appears in the
   `-fno-fast-math` `set_source_files_properties` block (opens `:569`, `PROPERTIES COMPILE_FLAGS` at
   `:921`), with the plan S11 edit (2) comment explaining why the other three stay out. A file may
   appear in only **one** `set_source_files_properties()` call — confirm it appears in no other.
3. `dsp/lint_all_headers.cpp` contains exactly one `#include <krate/dsp/systems/ecosystem_engine.h>`,
   after the Phase-7 include and before the Layer-4 block.
4. `tests/test_helpers/CMakeLists.txt` is **unmodified** — the helper header lives beside its TUs.
5. Every `TEST_CASE` name this phase introduced is discovered by the built binary:
   `dsp_systems_tests.exe --list-tests | grep EcosystemEngine` lists **every** case named in this
   file (behaviour, longrun, perf, non-finite), and the count matches.

**Falsification:** comment out one TU line and confirm its cases disappear from `--list-tests`.
Restore.

---

### T023 [P] — Full-suite run and the FR-090 byte-unchanged check

**Files:** none.

```bash
CMAKE="C:/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release \
    --target dsp_core_tests dsp_primitives_tests dsp_processors_tests dsp_systems_tests dsp_effects_tests
for t in dsp_core_tests dsp_primitives_tests dsp_processors_tests dsp_systems_tests dsp_effects_tests; do \
    build/windows-x64-release/bin/Release/$t.exe 2>&1 | tail -3; done
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "[long]" 2>&1 | tail -20
```

Then **SC-016 / FR-090**:

```bash
git diff --stat
git diff --name-only -- dsp/include/
```

`git diff --name-only -- dsp/include/` must name **`krate/dsp/systems/ecosystem_engine.h` and nothing
else**. `git diff --stat` over the phase must touch only: that header, the four TUs, the helper
header, the two `dsp/tests/CMakeLists.txt` lines, the one `dsp/lint_all_headers.cpp` line, and this
spec's own directory. Confirm by name that all sixteen headers listed under "Shipped headers
amended: ZERO" at the top of this file are untouched.

**Do not dismiss any failure as pre-existing.** The constitution forbids it: a red suite halts the
phase until it is fixed or a justified suppression is recorded.

**Done when:** all five layer suites report "All tests passed", the `[long]` lane is green, and the
diff is confined to the file list above.

---

### T024 [P] — Portability, lints and clang-tidy (SC-015)

**Files:** none.

```bash
node tools/check-portability.js
node tools/lint-layers.js
node tools/lint-odr.js
node tools/lint-nonfinite-symbols.js
node tools/lint-float-bit-goldens.js
node tools/lint-simd-aligned-loadstore.js
./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja
```

* **`check-portability.js` is mandatory and a green MSVC build proves nothing about it** — MSVC
  accepts narrowing in brace init and `std::size_t`/`double` conversions that Clang rejects.
* `lint-simd-aligned-loadstore.js` is **vacuous** here (no SIMD is introduced) but must still pass.
* `lint-nonfinite-symbols.js` is the enforcement behind FR-083: no `std::isnan`/`std::isinf`/
  `std::isfinite` in the header **or** in any of the four TUs.
* `lint-layers.js` passing is **necessary but not sufficient** for FR-001 — it only forbids upward
  includes and cannot see "Layer 0 + stdlib only" (plan S14 D-J). The actual gate is
  `EcosystemEngine_HeaderIncludesOnlyLayerZero` (T003), which must be green here too.
* **Capture clang-tidy to a log on the first run** and inspect the log; on Windows prefer the `.ps1`
  for a tree or a single-TU `clang-tidy -p build/windows-ninja dsp/lint_all_headers.cpp` for a
  one-file change. Never the `.sh` on Windows (it globs `.h` and runs serially for an hour).
* **Fix ALL clang-tidy warnings this phase's files produce**, not only the ones in new code paths.

If Linux behaviour is in doubt anywhere (the helper header's `std::span`, `std::array` CTAD, or any
`double`/`size_t` conversion), run the **WSL g++ 13 probe** rather than waiting ~40 min for CI.

**Done when:** every command above exits clean and the transcripts are recorded in the compliance
notes.

---

## Deviations this task list carries forward (for the compliance pass)

Each is already argued in the plan's S14; they are listed here so the compliance table carries a row
for each rather than discovering them.

| ID | Deviation | Owned by |
|---|---|---|
| D-A | No nested `Agent` struct — structure-of-arrays instead (spec's new-components table lists one) | T003 |
| D-B | FR-087's stage list does not name the FR-043 denormal snap; it is placed as stage 6b | T009 |
| D-C | FR-083's repair arithmetic is unimplementable as written (NaN − anything = NaN); the operative form charges `energyBudget_ − postRepairTotal` | T013 |
| D-D | One test-local helper header beyond FR-091's "four new test TUs" | T017 |
| D-E | `tests/test_helpers/statistical_utils.h` read and deliberately **not** consumed (`float`-only) | T017 |
| D-F | FR-091's file list omits `dsp/lint_all_headers.cpp`; one include line is required | T003 |
| D-G | SC-012 (a) reworded from `PrepareConfig`'s five fields to the full 28-knob set — a strengthening | T019 |
| D-H | Share-unit setters named `…Shares` | T003, T005 |
| D-I | `getControlStepCount()` added (SC-010 (b) and FR-061's fraction have no denominator without it) | T003 |
| D-J | `lint-layers.js` cannot enforce FR-001; an in-TU literal check does | T003, T024 |
| D-K | SC-002's runtime may cross the `[long]` threshold; retag and move if measured > ~15 s | T018 |
| **D-L** | **FR-071's negative branch is defective in `spec.md` as well as in the plan's first draft** — it inverts sign inside the refuge and pays the agent out of an unguarded pool. The corrected formula is in T011; **the spec should be corrected in the same pass** | T011 |
| D-M | S12.3's L4/L5 replace normative formulas and are **not** pre-authorised — user sign-off from the measured table only | T015, T016 |
| D-N | SC-020's per-agent clause and SC-002 (d)'s windowed clause needed probe surface (`outflow`, `divided`, `clampedSteps`, `setPool`) | T008, T018 |
| D-O | Four FR-level cases with no SC of their own: `SettersClampToRange`, `KindAssignmentIsStratified`, `ExchangeDisabledAtPredationHalf`, `NegativePoolIsNotAbsorbedByAnAgent` | T005, T006, T008, T009 |
| D-P | SC-001 (d)'s perturb schedule runs inside the 1000-config batch (not a 100-config sub-batch) | T020 |

---

## Group / task index

| Group | Tasks | What lands |
|---|---|---|
| A | T001 [P], T002 [P] | CMake registration + four TU stubs; ODR sweep |
| B | T003 | The header skeleton (declarations, constants, salt table, probes, required doxygen) + lint stub include |
| C | T004 | `prepare`/`initialiseState`/`reset`/`setSeed`/`refreshDerivedScales`/`processChunk` |
| D | T005 | 23 setters + getters, `setAffinity`, `setFreqRangeHz`, the S8 neutral contract |
| E | T006 [P], T007 [P] | SC-007 (b), SC-008, SC-010 (b), SC-006 (c), kind histogram; SC-009 (a)(c) |
| F | T008 | Stages 2–4 + `InspectProbe`; SC-020, FR-021 |
| G | T009 | Stages 5–8; SC-009 (d), SC-021 |
| H | T010 | Stages 9–12; short-run conservation |
| I | T011 | Stage 13, wake/dormancy, `perturbAgent` (D-L corrected); SC-014 (a)(b)(c) |
| J | T012 | SC-014 (d)(e), SC-019, SC-006 (a)(b)(d), SC-007 (a), perturb edges |
| K | T013 | The non-finite guard ladder and repair |
| L | T014 | `NonFiniteProbe` + SC-009 (b) |
| M | T015 | Perf TU — SC-011 (a)(b) + stage probe, **run alone** |
| N | T016 | The lever decision / escalation to the user |
| O | T017 | `ecosystem_metrics_test_helpers.h` |
| P | T018 | SC-002 (a)–(d), SC-004 (a)(b) |
| Q | T019 | Fuzz harness + SC-012 |
| R | T020 | SC-001 (a)–(d) |
| S | T021 | SC-003, SC-005, SC-013, SC-017, SC-018 |
| T | T022 [P], T023 [P], T024 [P] | Registration audit; full-suite + FR-090 diff check; portability/lints/clang-tidy |
