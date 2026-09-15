# Tasks: Vorago Phase 7 — Harmonic Bloom

**Spec:** `specs/vorago-phase7-harmonic-bloom/spec.md` (1416 lines)
**Plan:** `specs/vorago-phase7-harmonic-bloom/plan.md` (1646 lines) — the **S-numbers below refer to
its sections**. Every code shape a task needs is already written there and is deliberately **not**
re-typed here. An executor reads the cited S-section, not this file, for the code.
**Deliverable:** one new Layer 3 header `dsp/include/krate/dsp/systems/bloom_engine.h`
(header-only, no `.cpp`), four new test TUs under `dsp/tests/unit/systems/`, **two** edits in
`dsp/tests/CMakeLists.txt`, **one** include line in `dsp/lint_all_headers.cpp`. **No new test
helper** (plan S10.1) and therefore no `tests/test_helpers/CMakeLists.txt` edit.
**Test target:** `dsp_systems_tests` (all four new TUs).
**Regression set that must stay green (FR-080, FR-081, SC-012):** `dsp_core_tests`,
`dsp_primitives_tests`, `dsp_processors_tests`, `dsp_systems_tests`, `dsp_effects_tests`.
**Plugin work:** none. Vorago's plugin starts at Phase 11; phases 1–10 are KrateDSP-only.
**Shipped headers amended: ZERO.** `harmonic_cloud.h`, `entropy_processor.h`, `spectral_state.h`,
`spectral_morph_engine.h`, `seraphis_voice.h`, `seraphis_engine.h`, `harmonic_snapshot.h`,
`spectral_coring_estimator.h`, `fft_autocorrelation.h`, `sympathetic_resonance_simd.h` and
`slow_event_scheduler.h` are **read-only for the whole phase** (FR-080). `git diff --name-only --
dsp/include/` must name `systems/bloom_engine.h` and nothing else, at every point in the build.

---

## How to read this file

* Tasks are grouped into **ordered groups**. A group starts only when every task in the previous
  group is green: it builds warning-free, its own cases pass, and `dsp_systems_tests` still passes.
* `[P]` marks tasks that are parallel-safe **within their group**: their file sets are fully
  disjoint and every file they touch is new or owned solely by them. **Every task that edits
  `dsp/include/krate/dsp/systems/bloom_engine.h` is unmarked and sits alone in its own group** —
  that header is this phase's one shared file. `dsp/tests/CMakeLists.txt` is the other shared file
  and is touched by exactly two tasks (T001, and T021's read-only audit). Each of the four test TUs
  is likewise treated as a shared file: **no two tasks in the same group touch the same TU.**
* Each task is **self-contained**: exact files, the failing test to write **first** (TU,
  `TEST_CASE` name, the numeric assertions), then the implementation intent with the plan section
  that carries the code shape, then the command that verifies it.
* Canonical order inside every task: **write the failing test → watch it fail → implement → zero
  compiler warnings → the test passes → `dsp_systems_tests` still passes.**
* **Test-only tasks** (T005, T006, T011–T019, T021–T023) carry a named **falsification** instead of
  a fail-first step — a mutation or fixture inversion that must make the new assertion fail —
  because the behaviour they measure was implemented in an earlier task. A test-only task is not
  done until its falsification has been run and its result recorded in the compliance notes.
* **No commit tasks.** Commits happen outside this workflow.

### Four deliberate deviations from the requested layout, each forced by the repo or the plan

**1. CMake registration is T001, not a final task.** `dsp/tests/CMakeLists.txt`'s `dsp_systems_tests`
source list is **enumerated, not globbed** — verified this session: `add_executable(dsp_systems_tests`
at `dsp/tests/CMakeLists.txt:324`, the Vorago Phase-6 block at `:450-463`, the closing `)` at `:464`.
An unregistered TU compiles into nothing and its cases silently never run, so registering all four
TUs up front is the only ordering under which every later task's "run the suite" step proves
anything. The final group still carries a **registration-completeness audit** (T021), the full-suite
run (T022) and the portability/lint gate (T023).

**2. Measurement precedes implementation** (plan S15 step 1, FR-073). The stage probe's four arms are
written against *stand-ins* (a raw 16-entry table walk, a 48-float store loop) in T003, **before**
`BloomEngine` exists, so the dominant term — plan S12.2 predicts it is arm (b), the owned-slot write
phase — is known while the caller-side levers (plan S12.2, levers 1 and 2) are still cheap to choose.
T020 re-points the same arms at the real engine.

**3. `dsp/lint_all_headers.cpp`'s include lands with the header, not before it.** That file is
compiled by `add_library(dsp_lint_stub OBJECT lint_all_headers.cpp)`; an include of a header that
does not yet exist breaks the whole build. It is edited inside T004, the task that creates the
header (Phase-6 include at `:188`, the Layer-4 block opens at `:190`).

**4. Implementation order is grid → spawn → lifecycle, not the plan's S4-before-S5 section order.**
Plan S15 lists the lifecycle (S5) before the spawn event (S4) because that is the reading order of
the design. It is **not** a testable build order: with no spawn path no child can exist and the
envelope has no observable; with no lifecycle a child never retires and only `numChildSlots` spawns
can ever be observed. The build order below is T007 (grid + gate + pass-through) → T008 (spawn,
tested with at most `numChildSlots` children so no retirement is needed) → T009 (lifecycle, which
unlocks every long-run criterion). Each of the three has assertions that fail before it and pass
after it.

### Build and run commands (Windows; the full CMake path is mandatory)

```bash
CMAKE="C:/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "BloomEngine_*" 2>&1 | tail -20
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "[long]" 2>&1 | tail -5
```

* Catch2 filters: the test-case name as a **positional** argument (`"BloomEngine_SeedDeterminism"`),
  tags in `[brackets]`. Never `-c` — that filters SECTIONs, not cases.
* `[long]` cases run locally by default and are excluded from the per-push CI filter
  `~[performance]~[perf]~[benchmark]~[!benchmark]~[long]`; they run nightly on all three OSes.
* **Timing runs alone**: `node tools/run-cpu-tests.js dsp_systems_tests`, nothing else executing —
  no build, no clang-tidy, no second suite, no parallel agent.
* Capture slow runs (`[long]`, perf, clang-tidy) to a log file on the **first** run and read the log.
  Never re-run a suite just to grep its output.

### Conventions every task inherits

* Namespace `Krate::DSP`; classes PascalCase, methods camelCase, members trailing underscore,
  constants `kPascalCase`. Header banner carries `@par Layer: 3 (systems/). Dependencies: Layer 0/1
  + stdlib only.` and the real-time-safety line (plan S1.1).
* Every new case is tagged `[bloom_engine]`, plus `[long]` or `[.perf]` where the task says so:
  `TEST_CASE("BloomEngine_Xxx", "[bloom_engine]")`.
* Finiteness is `Krate::DSP::detail::isFinite` / `isNaN` / `isInf` (`core/db_utils.h:118`, `:99`,
  `:260`) — **never** `std::isnan`/`std::isinf`/`std::isfinite`, in the header **or** in any test
  (`tools/lint-nonfinite-symbols.js` gates it, FR-008).
* **Non-finite values are built from bit patterns through a `volatile` sink** — never
  `std::numeric_limits<float>::quiet_NaN()`/`infinity()`, which fold to finite garbage on the
  macOS/Linux `-ffast-math` legs. Transcribable idiom:
  `dsp/tests/unit/systems/resonance_drift_network_nonfinite_test.cpp:149-155` (`makeNonFinite(bits)`
  and `makeNonFiniteDouble(bits)`; patterns `0x7FC00000`, `0x7F800000`, `0xFF800000`, and
  `0x7FF8000000000000` for the double). Only `bloom_engine_nonfinite_test.cpp` is compiled
  `-fno-fast-math`, so it is the **only** TU that may **assert IEEE semantics** on such a value;
  T008's FR-014 arm merely *constructs* them and asserts on counters, which is legal in a fast-math
  TU.
* **No bit-exact float goldens** anywhere (`tools/lint-float-bit-goldens.js`, roadmap line 536).
  Where a render must be pinned use `tests/test_helpers/render_fingerprint.h`
  (`kSampleTolerance = 5.0e-4f`, `kMetricTolerance = 2.5e-4`, `compareFingerprints`). The exact
  (`==`, `std::memcmp`) comparisons this phase does allow are all **within-run structural
  identities**: SC-014's pass-through arms, SC-003's canary/snapshot arms, SC-008's same-seed child
  tables (integer/enum surface only), and the `getSmoothedDepth() == 0.0f` / `== 1.0f` preconditions.
* Brace-initialised aggregates use **designated initialisers**
  (`BloomEngine::PrepareConfig{.capacity = 32, .numChildSlots = 8}`) — Clang errors on narrowing
  where MSVC does not (plan R7). Every literal carries its `f`/`u` suffix.
* Tests include `tests/test_helpers/allocation_detector.h` only; **never**
  `allocation_operator_overrides.h` — `dsp_systems_tests` already has its single owner and a second
  include is a duplicate-symbol link error.
* **Streaming statistics are mandatory in every `[long]` case**: accumulate per-second/per-minute
  RMS, peak and finiteness incrementally; never materialise more than one block plus one analysis
  window.
* Every `ClickDetectorConfig` fixture sets **all six fields** with designated initialisers and sets
  `.sampleRate` to the render rate — the struct default is `44100.0f`
  (`tests/test_helpers/artifact_detection.h:38`) and `isValid()` only range-checks it, so a wrong
  rate is used silently. Transcribable idiom: `atmosphere_engine_spectral_test.cpp:399-423`.
* **Zero compiler warnings is part of every task's definition of done**, not a later cleanup.
* `processChunk` is `[[nodiscard]]` and `LinearRamp::process()` is `[[nodiscard]]`
  (`primitives/smoother.h:370`) — bind both returns; discarding either is C4834 /
  `-Wunused-result` under the zero-warning rule.

### The four shared fixtures, and which task codes each

Coded **once per TU** as file-local free functions/structs (plan S10.1). A later task in the same TU
reuses the existing copy and does not re-declare it.

| Fixture | Shape | Coded by |
|---|---|---|
| `stepEngine(engine, r, a, pc, steps)` | cloud-free driver: a loop of `processChunk(r, a, pc, 64)` over a fixed synthetic parent array | **T007** in `bloom_engine_test.cpp`; **T009** in `bloom_engine_spectral_test.cpp` |
| `CloudRig` | `HarmonicCloud` + `BloomEngine` at the Phase-10 call shape (`seraphis_voice.h:1050-1054`): fill parents → `bloom.processChunk(r, a, n, 64)` → `cloud.setSpectralTarget(r, a, returned)` → `cloud.processStereoBlock(...)`, once per 64-sample control chunk | **T013** in `bloom_engine_spectral_test.cpp`; **T014** in `bloom_engine_test.cpp` |
| `countClicks(buffer, sigma)` / `smallestZeroSigma(buffer)` | `atmosphere_engine_spectral_test.cpp:399-423`, all six `ClickDetectorConfig` fields designated-initialised | **T013** in `bloom_engine_spectral_test.cpp` |
| `makeNonFinite(bits)` / `makeNonFiniteDouble(bits)` | `resonance_drift_network_nonfinite_test.cpp:149-155` | **T008** in `bloom_engine_test.cpp` (construction only); **T012** in `bloom_engine_nonfinite_test.cpp` |

### The stop-and-surface rule (FR-072, inherited verbatim from `resonance_drift_network_perf_test.cpp:57-64`)

**NON-NEGOTIABLE.** No implementing agent may lower `kMaxChildren`, raise `kBudgetNs = 10667`, relax
a threshold, shrink a workload, or widen a tolerance to make a figure fit. **Reduce cost, never move
the line.** If a gated figure misses, the build **stops** and surfaces the measured per-arm table for
a user ruling — the route that amended Phase 2 (1 → 1.75 %) and Phase 5 (1 → 1.5 %). Two levers are
pre-authorised (plan S12.2): call `processChunk` at a larger `numSamples` (caller-side, exact by
FR-006), and narrow `capacity - parentCount`. **The dirty-flag skip is UNAVAILABLE and must not be
implemented** (plan S12.2, normative): the arrays are the caller's, the engine cannot know they are
unchanged, and skipping the rewrite fails SC-003's padding arms and SC-016's poisoned-gap arm.

---

## Group A — Registration and the ODR sweep

Two disjoint file sets: T001 owns `dsp/tests/CMakeLists.txt` and the four new TU stubs; T002 creates
and edits nothing. Both `[P]`.

### T001 [P] — Create the four TU stubs and register them

**Files to create**

* `dsp/tests/unit/systems/bloom_engine_test.cpp`
* `dsp/tests/unit/systems/bloom_engine_spectral_test.cpp`
* `dsp/tests/unit/systems/bloom_engine_perf_test.cpp`
* `dsp/tests/unit/systems/bloom_engine_nonfinite_test.cpp`

Each stub is `#include <catch2/catch_test_macros.hpp>` plus a one-line comment naming the criteria it
will own (plan S10.2). No `TEST_CASE` yet — T003 writes the perf TU's first case.

**Files to edit**

* `dsp/tests/CMakeLists.txt`, the `dsp_systems_tests` enumerated source list — append **after** the
  Phase-6 block (`:450-463`) and **before** the closing `)` at `:464`. The exact block to insert,
  comment text included, is **plan S11 edit (1)**: the four `unit/systems/bloom_engine_*.cpp` lines
  under the "ENUMERATED, not globbed" header and the per-TU criteria map.
* `dsp/tests/CMakeLists.txt`, the `-fno-fast-math` block (opens `:556`
  `if(CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")`; `set_source_files_properties(` at `:557`; the
  Phase-6 entry at `:899`; `PROPERTIES COMPILE_FLAGS "-fno-fast-math -fno-finite-math-only"` at
  `:900`) — insert **exactly one** of the four TUs immediately before `:900`, with the comment from
  **plan S11 edit (2)** explaining why the other three stay out:

  ```cmake
          unit/systems/bloom_engine_nonfinite_test.cpp
  ```

  A file may appear in only one `set_source_files_properties()` call — this TU must **not** also
  appear in the `-O2` block below it.

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

Re-run, verbatim, from the repo root, and paste the transcript with hit counts:

```bash
grep -rn "class BloomEngine"    dsp/ plugins/ tools/
grep -rn "struct BloomEngine"   dsp/ plugins/ tools/
grep -rn "class BloomChild"     dsp/ plugins/ tools/
grep -rn "struct BloomChild"    dsp/ plugins/ tools/
grep -rn "class HarmonicBloom"  dsp/ plugins/ tools/
grep -rn "class BloomLifecycle" dsp/ plugins/ tools/
grep -rn "class ChildPartial"   dsp/ plugins/ tools/
grep -rn "struct ChildPartial"  dsp/ plugins/ tools/
grep -rn "class BloomEvent"     dsp/ plugins/ tools/
grep -rn "struct BloomEvent"    dsp/ plugins/ tools/
grep -rn "BloomEngineNonFiniteProbe" dsp/ plugins/ tools/
ls dsp/include/krate/dsp/systems/bloom_engine.h
```

Every one must return **zero hits** (the last must report "No such file"). Record — do not merely
assume — that the **three** near-name families in the tree were checked and are all **class-scoped**,
so none can collide (plan S0, spec ODR table): (1) `AetherReverb`'s reverb-side harmonic-bloom
resonators (`effects/aether_reverb.h:1442-1454`, `kMaxBloomResonators = 32`); (2)
`SpectralMorphEngine`'s morph-stagger `bloom` (`systems/spectral_morph_engine.h:100,339,447`); (3)
`SeraphisEngine::BloomEvents` / `kBloomPartialCap` (`systems/seraphis_engine.h:261,293,968`), plus
`SeraphisMacro::Bloom` (`seraphis_macro_matrix.h:50`) and `SeraphisVoice::setBloom`
(`seraphis_voice.h:680`). Also record that nested `Phase` enums already coexist
(`slow_event_scheduler.h:180`, `growth_envelope.h:207`) and that `tools/lint-odr.js:18-21` qualifies
nested types by their enclosing class.

**Done when:** the transcript is recorded. If any sweep returns a hit, **stop** and surface it: a
colliding name is a redesign decision, not an implementer's rename.

---

## Group B — Measure before the component exists (plan S15 step 1, FR-073)

### T003 — The FR-073 stage probe, with no component in existence

**Files to edit:** `dsp/tests/unit/systems/bloom_engine_perf_test.cpp` only.

**Write first (this IS the deliverable):**
`TEST_CASE("BloomEngine_StageCostProbe", "[bloom_engine][.perf]")`.

Transcribe the measurement preamble and trial shape from
`dsp/tests/unit/systems/resonance_drift_network_perf_test.cpp:55-90`:

* basis **ns per 512-sample block at 48 kHz**; one block period is **10 666 667 ns**; FR-072's
  0.1 %-of-one-core ceiling is therefore `constexpr double kBudgetNs = 10667.0;` — declare it here,
  in this TU, now;
* trial shape **best-of-25 × 500 blocks after 400 warm-up blocks**;
* percent-of-core figures `WARN`-reported, never asserted;
* the stop-and-surface rule block, copied verbatim, naming `kMaxChildren` and `kBudgetNs`;
* "RUN IT ALONE: `node tools/run-cpu-tests.js dsp_systems_tests`".

Four arms, each a **stand-in** for the corresponding plan S12.2 arm, all `WARN`-reported and **none
asserted** at this stage:

| Arm | Stand-in workload | Priced because |
|---|---|---|
| (a) child bookkeeping | walk a local 16-entry record array, one `u*u*(3.0f-2.0f*u)` smoothstep + three integer compares per entry, **8 iterations per block** | the per-block lifecycle floor |
| (b) owned-slot writes | write `(64 - 48) + 2*16 = 48` floats into a 64-float array, **8 times per block** (≤ 1 152 stores/block, plan S3.4) | plan S12.2 predicts this **dominates** |
| (c) spawn-event scan | one pass of 48 × 8 amplitude compares + 64 `std::log2` + 20 candidate evaluations, executed **once per 15 000 blocks**, reported both raw and amortised | so the amortisation is visible rather than assumed |
| (d) clock only | one `Xorshift32::nextUnipolar()` + one `LinearRamp::process()` per control step, 8 steps per block | the floor a caller pays for having the component in the chain at all |

Use a `volatile` sink on each arm's accumulator so nothing is optimised away.

**Emit, via `WARN`,** a table of the four arms in ns/block, their sum, the sum as a percentage of
`kBudgetNs`, and the ordering check "(b) ≫ (a) ≫ (d) ≫ amortised (c)". If the measured sum already
exceeds `kBudgetNs` **before the component exists**, **STOP** and surface the table — that is the
stop-and-surface rule firing at the cheapest possible moment, and the answer is a caller-side lever
(plan S12.2), never a smaller `kMaxChildren`.

**Implementation intent:** none — there is no component yet. This task exists so the budget's shape
is known before a line of `bloom_engine.h` is typed (plan S15 step 1, the Phase-3 pattern).

**Verify (ALONE, nothing else running)**

```bash
node tools/run-cpu-tests.js dsp_systems_tests 2>&1 | tee bloom_probe.log | tail -40
```

**Done when:** the four arm figures and their sum are recorded in the compliance notes,
`kBudgetNs = 10667.0` is checked in, and the case is tagged `[.perf]` so the per-push CI filter
excludes it.

---

## Group C — The header skeleton (the one shared file; alone in its group)

### T004 — Create `bloom_engine.h` as an explicit pass-through, and register it with the lint stub

**Files to create:** `dsp/include/krate/dsp/systems/bloom_engine.h`
**Files to edit:** `dsp/lint_all_headers.cpp` — insert after the Phase-6 include (`:188`), before the
Layer-4 block (`:190`), exactly as **plan S11 edit (3)**:

```cpp
// Vorago Phase 7 (specs/vorago-phase7-harmonic-bloom), FR-001
#include <krate/dsp/systems/bloom_engine.h>
```

**Test first:** this task's fail-first evidence is the build: add the `lint_all_headers.cpp` include
**before** creating the header and confirm the build breaks with "cannot open source file
bloom_engine.h" — proof that the lint stub actually compiles the new header. Then write the header.
(The first `TEST_CASE` assertions belong to T005, next group.)

**Implementation intent — transcribe, do not invent.** The complete shape is plan S1.1–S1.6 and S2:

* **S1.1** banner (Layer 3, "Dependencies: Layer 0/1 + stdlib only", the real-time-safety line
  stating that **every** method including `prepare()` is `noexcept` and allocation-free) and the
  exact include list — four `krate/dsp` headers (`core/db_utils.h`, `core/pitch_utils.h`,
  `core/random.h`, `primitives/smoother.h`) plus `<algorithm> <array> <cmath> <cstddef> <cstdint>`.
  **No `<vector>`, `<memory>`, `<functional>`, `<random>`, no I/O, no exceptions, and no
  `systems/harmonic_cloud.h`, `processors/entropy_processor.h`, `processors/spectral_state.h` or
  `systems/slow_event_scheduler.h`** (FR-080, D-1/D-2). Record in the banner the three near-name
  `bloom` families from T002 and what each actually is (plan R13).
* **S1.2** every constant, **class-scoped `static constexpr`** (FR-003), each restated one carrying a
  source comment naming the header and line it was copied from, with the live `static_assert`s:
  `kMaxSlots == 64`, `kMaxSlots <= 255`, `kControlChunkSamples == 64`, `kMaxDetuneCents <= 50.0f`,
  `kMinDetuneCents < kMaxDetuneCents`.
* **S1.3** nested `enum class Relation : std::uint8_t { Octave = 0, Fifth = 1, DetunedNeighbour = 2 }`
  (**APPEND ONLY** — it becomes a persisted plugin parameter at Phase 12), nested
  `enum class Phase : std::uint8_t { Idle = 0, FadeIn = 1, Hold = 2, FadeOut = 3 }`, the
  designated-initialiser-only `struct PrepareConfig { std::size_t capacity = kMaxSlots;
  std::size_t numChildSlots = kDefaultChildSlots; }` with **both** doc paragraphs (the
  `getActivePartialCount()` contract, and "THIS FIELD DOES NOT BOUND THE WRITE REGION"), and the
  **private** `struct Child` with its eleven fields in the S1.3 declaration order.
* **S1.4** the complete public API, every method `noexcept`, verbatim from the S1.4 listing —
  including the **normative `processChunk` doxygen precondition**: both arrays address at least
  `kMaxSlots` (64) writable floats regardless of `parentCount` and regardless of `capacity()`,
  because `setCapacity()` can raise the write ceiling after `prepare()` (plan S14 **C-11**). Also
  the out-of-range read-surface neutral block in the `noise_organism.h:856-861` form.
* **S1.5** the private state layout in the given declaration order; **S1.6** the salt table
  (`kSaltClock = 0`, `kSaltEvent = 1`, `kSaltNextFree = 2`) with its overlap `static_assert`, and
  the two differently-shaped RNGs (persistent `clockRng_`, per-event re-seeded `eventRng_`).
* **S2** `prepare()` in the numbered seven-step order, `reset()` configuration-preserving with
  `cursor_ = reserveBase()`, `setSeed()`.
* **Every setter obeys FR-009 now** (plan S7.6's table): a non-finite float is **rejected** (previous
  value stands), an out-of-range index is a **silent no-op**, an out-of-range float is **clamped and
  the getter reports the clamp**. That is what T005 asserts.
* `processChunk` at this stage is an **explicit pass-through**: guard ladder steps (1) and (2) of
  S3.1, then `return parentCount;` with a `// T007` marker. No control loop, no writes yet.
* `getAllocatedBytes()` returns `0u`; `stateFinite()` walks the 16 `Child` records with
  `detail::isNaN`/`detail::isInf`; `friend struct detail::BloomEngineNonFiniteProbe;` with the
  **forward declaration only** (`namespace detail { struct BloomEngineNonFiniteProbe; }`), never a
  definition — its one definition lives in `bloom_engine_nonfinite_test.cpp` (T012, the
  `feedback_ecology.h:166-172` idiom). Putting the friend declaration here is deliberate: T012 must
  not have to edit this header.

**Verify**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
node tools/lint-layers.js && node tools/lint-odr.js && node tools/lint-nonfinite-symbols.js
git diff --name-only -- dsp/include/
```

**Done when:** the suite builds warning-free, the three lints are clean, and `git diff --name-only --
dsp/include/` names `systems/bloom_engine.h` and nothing else.

---

## Group D — The cheapest gates, run before any behaviour exists

Two disjoint tasks: T005 owns `bloom_engine_test.cpp`, T006 owns no files. Both `[P]`.

### T005 [P] — SC-012's contract `static_assert`s and SC-009's range-and-neutral arm

**Files to edit:** `dsp/tests/unit/systems/bloom_engine_test.cpp` only.

**Write first, case 1:** `TEST_CASE("BloomEngine_CloudContractAssumptions", "[bloom_engine]")`.
This TU is the **only** place `harmonic_cloud.h`, `entropy_processor.h` and `bloom_engine.h` meet in
one translation unit (plan S10.4) — deliberately, because that is the compile that would fail on an
ODR or namespace-scope collision. Include all three and `static_assert`:

* `HarmonicCloud::kMaxPartials == 64` and `BloomEngine::kMaxSlots == HarmonicCloud::kMaxPartials`;
* `HarmonicCloud::kControlChunkSamples == 64` and
  `BloomEngine::kControlChunkSamples == HarmonicCloud::kControlChunkSamples`;
* `HarmonicCloud::kTargetAmpEpsilon == 1.0e-5f` and
  `BloomEngine::kSilentParentAmplitude == HarmonicCloud::kTargetAmpEpsilon`;
* `EntropyProcessor::kMinRatioSpacingCents == 24.0f` and
  `BloomEngine::kMinRatioSpacingCents == EntropyProcessor::kMinRatioSpacingCents`;
* `BloomEngine::kMinChildRatio == 0.5f` and `BloomEngine::kMaxChildRatio == 128.0f` (the
  `SpectralState::kMinStateRatio` / `kMaxStateRatio` figures, restated — plan D-2).

**`sizeof(HarmonicCloud)` is deliberately NOT pinned.**

**Write first, case 2:** `TEST_CASE("BloomEngine_ArgumentContract", "[bloom_engine]")` — SC-009's
**range-and-neutral** arm, which needs no IEEE semantics and therefore lives in this TU, not in the
`-fno-fast-math` one (plan S10.3, SC-009 row). Drive **every** float setter past **both** range ends
and assert the getter reports **exactly the clamp**:

`setDepth(-1.0f)` → `0.0f`, `setDepth(2.0f)` → `1.0f`; `setSpawnRateHz(-1.0f)` → `0.0f`,
`setSpawnRateHz(10.0f)` → `kMaxSpawnRateHz` (`0.05f`); `setChildGain(-0.5f)` → `0.0f`,
`setChildGain(5.0f)` → `1.0f`; `setFadeInSeconds(0.0f)` → `1.0f`, `setFadeInSeconds(1e6f)` →
`300.0f`; `setHoldSeconds(-1.0f)` → `0.0f`, `setHoldSeconds(1e6f)` → `900.0f`;
`setFadeOutSeconds(0.0f)` → `1.0f`, `setFadeOutSeconds(1e6f)` → `600.0f`;
`setHoldJitterFraction(-1.0f)` → `0.0f`, `(2.0f)` → `1.0f`; `setConsumerTiltDb(-99.0f)` → `-12.0f`,
`(99.0f)` → `12.0f`; `setWake(-1.0f)` → `0.0f`, `(2.0f)` → `1.0f`;
`setRelationWeight(r, -1.0f)` → `0.0f` and `(r, 2.0f)` → `1.0f` for each of the three relations.

Size setters likewise: `setParentCount(0)` → `1`, `(99)` → `kMaxParents` (8);
`setChildrenPerEvent(0)` → `1`, `(99)` → `kMaxChildrenPerEvent` (4); `setCapacity(0)` → `1`,
`(99)` → `kMaxSlots` (64), with `numChildSlots()` re-deriving `min(requested, capacity())` after each
(plan S6.3).

Every indexed read called at `kMaxChildren`, `kMaxParents` and `SIZE_MAX` must return the S1.4
documented neutrals: `0` for size getters, `0.0f` for float getters, `Relation::Octave`,
`Phase::Idle`, `false` for `getIsChildFallback`, and `kMaxSlots` for `getLastParentIndex` and for
`getChildSlotIndex` on an `Idle` table entry. `setRelationWeight` with a `Relation` cast from
`std::uint8_t{7}` is a **silent no-op** leaving all three weights unchanged.

**Falsification (test-only task):** temporarily change one mirrored constant in `bloom_engine.h`
(e.g. `kSilentParentAmplitude = 1.0e-4f`) and confirm case 1 fails **at compile time**; temporarily
replace one setter's `std::clamp` with a plain assignment and confirm case 2 fails. Revert both.

**Verify**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "BloomEngine_*" 2>&1 | tail -10
```

**Done when:** both cases pass, the build is warning-free, and both falsifications have been run and
recorded.

---

### T006 [P] — The lint and portability gate on the skeleton (SC-013, first pass)

**Files:** none created or edited.

Run the full SC-013 gate against the skeleton now, while the header is small enough that a failure
names its own cause:

```bash
node tools/lint-odr.js
node tools/lint-layers.js
node tools/lint-nonfinite-symbols.js
node tools/lint-float-bit-goldens.js
node tools/lint-simd-aligned-loadstore.js
node tools/check-portability.js
```

All six clean. Then compile the header under **g++/libstdc++** (MSYS2 or WSL — the Phase-3
precedent): a green MSVC build proves nothing about the Linux/macOS legs. A one-line TU that
includes `<krate/dsp/systems/bloom_engine.h>` and instantiates a `BloomEngine`, compiled
`-std=c++20 -fsyntax-only -I dsp/include`, is sufficient.

**Falsification:** none needed — record the exit codes so the gate is demonstrably *run*, not
assumed.

**Done when:** all six lints are clean and the g++ syntax check passes. T023 re-runs this gate
against the finished header; this early pass exists so a layer/ODR/portability mistake is caught
while the file is one page long.

---

## Group E — The control grid, the gate, and the pass-through contract

Edits `bloom_engine.h`: alone in its group.

### T007 — Control-step loop, spawn gate, and the disabled-is-bit-identical contract

**Files to edit:** `dsp/include/krate/dsp/systems/bloom_engine.h`,
`dsp/tests/unit/systems/bloom_engine_test.cpp`.

**Write first — three cases, all failing against T004's pass-through:**

1. `TEST_CASE("BloomEngine_DisabledIsBitIdenticalPassThrough", "[bloom_engine]")` — SC-014's
   **cold-start** arms (a), (b), (c) and the unclamped-return arm (f). Each cold-start arm runs
   **100 000** chunks of `processChunk(r, a, pc, 64)` over a synthetic parent array:
   * (a) `setDepth(0.0f)` **before** `prepare()`, with `getSmoothedDepth() == 0.0f` asserted on the
     first chunk as an explicit precondition (S8 addition A-6 exists for exactly this);
   * (b) `PrepareConfig{.capacity = 64, .numChildSlots = 0}` (FR-054);
   * (c) `setDormant(true)`.
   In each: the return value is **exactly `parentCount`**, both arrays are `std::memcmp`-unchanged
   over all 64 floats, `isEngaged() == false`, `getLiveChildCount() == 0`, `stateFinite()` true.
   * (f) **unclamped return**: `parentCount = 64` with `PrepareConfig{.capacity = 32}`, disengaged —
     the returned count is **64**, not 32, and both arrays are `std::memcmp`-unchanged. (An engine
     returning the clamped `pc` truncates the caller's spectrum; every other arm runs with
     `parentCount <= capacity()` and structurally cannot see it — plan S3.1.)
2. `TEST_CASE("BloomEngine_BlockPartitionInvariance", "[bloom_engine]")` — the early SC-008 (b) arm,
   whose observable is the depth ramp, not a child. Set `setDepth(1.0f)` on an engine prepared at
   depth `0.0f` so the 50 ms ramp is in flight (50 ms at 48 kHz ≈ **37 control steps**; the run is
   **1 600 samples = 25 steps**, so the ramp never saturates). Drive the same 1 600 samples as
   `{64×25}`, `{512, 512, 512, 64}`, `{1600}` and the ragged `{1, 7, 383, 1209}` — after each,
   `getSmoothedDepth()` must be **bitwise identical** across all four partitions.
   **Rejected-call arm** (FR-005's three otherwise-unasserted clauses): repeat the ragged partition
   with interleaved `processChunk(nullptr, amplitudes, pc, 512)`,
   `processChunk(ratios, nullptr, pc, 512)` and `processChunk(ratios, amplitudes, pc, 0)` calls —
   `getSmoothedDepth()` must be **unchanged** by them, and both `nullptr` calls must return the
   caller's **unclamped** `parentCount`.
3. `TEST_CASE("BloomEngine_NoAllocationAfterPrepare", "[bloom_engine]")` — SC-007. An
   `AllocationScope` (`tests/test_helpers/allocation_detector.h:111`) around **10 000**
   `processChunk` calls plus **every** setter and `triggerBloom()`: **0 allocations**. A second
   scope around `prepare()` itself: **0 allocations**. `getAllocatedBytes() == 0`. **FR-074's
   footprint report lives here and nowhere else:** a `WARN(sizeof(BloomEngine))` beside the
   `getAllocatedBytes() == 0` assertion — **reported, never asserted to a byte** (plan S9: the
   ≈ 1.2 KB figure is an estimate and pinning it would be a portability trap).

**Implementation intent** — plan S3.1, S3.2, S3.3, S6.1, S6.2, S6.4:

* the four-step guard ladder of **S3.1** including the normative step (0) precondition comment;
* the **absolute-residue** control loop of **S3.2** (`controlPhase_` carried **across calls**, the
  `subharmonic_engine.h:592-607` idiom) — a block-relative grid is exactly what case 2 fails on;
* `controlStep()` in the **normative order** of **S3.3**: (1) the unconditional
  `clockRng_.nextUnipolar()` draw, (2) the single bound `depthRamp_.process()` value, (3) the single
  `gate = dormant_ ? 0.0f : wake_ * smoothedDepth` and the probability
  `p = spawnRateHz_ * gate * kControlChunkSamples * invSampleRate_`, (4) `advanceChildren()` (a
  declared no-op with a `// T009` marker at this stage), (5) arm consumption with `armed_ = false`
  and `++discardedEvents_` when `gate == 0.0f`, `runEvent()` otherwise (a declared no-op with a
  `// T008` marker), (6) `++controlStep_`;
* `setDormant` / `setWake` (with the `<= kWakeSilenceEpsilon` snap to exactly `0.0f`) / `setDepth`
  (clamp + `depthRamp_.setTarget`) / `triggerBloom()` (`armed_ = true`, **edge-like**) per S6.1,
  S6.2, S6.4;
* `applyOutput()`'s **not-engaged early return only** — `if (!engaged_) return parentCount;`. The
  engaged write region is T008's, because `engaged_` cannot become true until a child spawns and an
  untestable write path is exactly the kind of code this ordering exists to prevent.

**Verify**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "BloomEngine_*" 2>&1 | tail -10
```

**Done when:** the three cases pass, the suite is green, the build is warning-free, and
`getSmoothedDepth()` was confirmed to **differ** across partitions before the absolute-residue loop
was written (the fail-first evidence for case 2).

---

## Group F — The spawn event

Edits `bloom_engine.h`: alone in its group.

### T008 — Parent scan, draws, acceptance, slot selection, latch, and the engaged write region

**Files to edit:** `dsp/include/krate/dsp/systems/bloom_engine.h`,
`dsp/tests/unit/systems/bloom_engine_test.cpp`.

Every assertion below is reachable **without** the lifecycle clock: each fixture spawns at most
`numChildSlots()` children, so no child needs to retire. The retirement-dependent arms are T009's.

**Write first — three cases:**

1. `TEST_CASE("BloomEngine_StrongestKParentSelection", "[bloom_engine]")` — SC-005.
   **20 crafted** parent arrays (known ordering, exact ties, sub-threshold slots) + **200 random**
   arrays. The reference ordering is a **`std::stable_sort` over `(amplitude, index)` keyed on
   `(-amplitude, index)`** — an unqualified `std::partial_sort` is **not acceptable** (plan R8).
   Read the selection through `getLastParentSelectionCount()` / `getLastParentIndex(k)` (S8
   addition A-4). Assert: exactly the K largest; ties resolved to the **lower** index; no slot with
   amplitude `<= 1e-5f` selected; no index `>= min(parentCount, reserveBase())`.
   **FR-013 arm:** after a run spanning many events, `getParentScanCount() == getSpawnEventCount()`
   **exactly** (a per-chunk scan would make the first far larger; a CPU budget cannot police this —
   spec FR-013).
   **FR-014 arm (plan S14 C-10; FR-014 has no other criterion anywhere):** drive a forced event
   (`triggerBloom()` + one control step) against (i) a parent array whose every amplitude is at or
   below `kSilentParentAmplitude` and (ii) a parent array whose every entry is non-finite (bit
   patterns through the `makeNonFinite` volatile sink — construction only, no IEEE assertion in this
   TU). In both: `getSpawnEventCount()` advanced by **exactly 1**, `getParentScanCount()` by
   **exactly 1**, `getOfferedChildCount()` **unchanged**, `getRejectedSpawnCount()` **unchanged**,
   `getLiveChildCount() == 0`, `stateFinite()` true, and the array byte-unchanged outside the FR-051
   pad regions.
2. `TEST_CASE("BloomEngine_ChildRatioRelationships", "[bloom_engine]")` — SC-006, every arm except
   the consumer arm and its negative control (those need a real cloud and a fade-in, and are T009's).
   Over **500 seeded events**: a non-fallback `Octave` child's ratio is within **0.1 cent** of
   `2.0 ×` its parent's ratio (`getChildParentIndex(i)` names the parent); a non-fallback `Fifth`
   within **0.1 cent** of `1.5 ×`; a `DetunedNeighbour`'s offset lies in **[24, 50] cents**; no
   child lands within **24 cents** of any partial present at spawn **or of any sibling of the same
   event**; no emitted ratio lies outside **[0.5, 128]**, and such a candidate shows up in
   `getRejectedSpawnCount()` rather than being **clamped**.
   **Fallback arm:** every child with `getIsChildFallback(i) == true` reports relation `Octave` or
   `Fifth` through `getChildRelation(i)` (the **original** relation) and lies **[24, 50] cents** off
   the exact interval.
   **Path-is-live arm:** 500 further events against an exactly-harmonic parent spectrum (integer
   ratios) must produce `getRejectedSpawnCount() > 0` **and** `getFallbackChildCount() > 0` — without
   it FR-026's retry/fallback path is dead code (plan R5).
   **Relation-weight arm (FR-060):** with weights `{1,0,0}`, `{0,1,0}`, `{0,0,1}` in turn over
   **200 events each**, **every** child carries the enabled relation; with `{0,0,0}` all three
   relations appear over 200 events (the "all-zero falls back to uniform" edge case). Without this
   arm every per-relation clause above is a conditional that an engine always returning
   `Relation::Octave` satisfies vacuously.
   **Without-replacement arm (FR-016):** for every event with
   `getChildrenPerEvent() <= getLastParentSelectionCount()`, that event's children have **pairwise
   distinct** `getChildParentIndex()` values; for larger events the first
   `getLastParentSelectionCount()` children do.
   **Per-event cap arm (FR-015):** no single event raises `getOfferedChildCount()` by more than
   `getChildrenPerEvent()`, swept over `childrenPerEvent ∈ {1, 2, 3, 4}`.
   **FR-025 accounting arm:** with `numChildSlots = 2` and `childrenPerEvent = 4`, an event offered
   against a full table refuses each slot-exhausted child **exactly once** —
   `getRefusedChildCount()` rises by the number of unplaced children and `getRejectedSpawnCount()`
   by **exactly one per refused child, not four** (plan S4.2 records the draft bug that counted it
   up to five times), and the exact identity
   `getOfferedChildCount() == getSpawnedChildCount() + getRefusedChildCount()` holds (plan S14 C-7).
3. `TEST_CASE("BloomEngine_SlotAccountingInvariantsUnderFuzz", "[bloom_engine]")` — SC-003's
   write-region arms (the 1 000-config fuzz, the FR-032 death arm, the sticky arm and the capacity
   arm are added later by T009 and T010 to this same case):
   * **Padding, hard, over the WHOLE write region.** Poison both arrays before **every** call (NaN
     through a volatile sink, `-1.0f`, large garbage). After **every** call, every index in
     `[parentCount, capacity())` that is **not** held by a live child — cross-checked against
     `getChildSlotIndex(i)` over the `getLiveChildCount()` live entries — is **exactly**
     `float(i + 1)` and **exactly** `0.0f`. That covers the FR-051 gap `[parentCount, reserveBase())`
     **and** the unoccupied part of the owned region.
   * **Canary and snapshot.** An out-of-capacity canary that must be bit-unchanged, **plus** a full
     pre-call snapshot after which `[0, min(parentCount, reserveBase()))` is byte-identical.
   * **Overlap counter, both directions (FR-052).** `getOverlapEngagementCount() == 0` **exactly**
     while `parentCount <= reserveBase()`; and in a dedicated arm with `parentCount = capacity()` and
     `numChildSlots() > 0`, **N engaged calls give exactly N**, with the returned count still
     `capacity()`. Without the positive direction an engine that has lost FR-052's only observable
     passes, and Phase 10's "assert it is zero" integration test is silently green forever.
   * **Buffer-precondition arms (plan S1.4, S14 C-11).** (i) run the engine against a
     `kMaxSlots`-float working array embedded in a larger poisoned buffer whose surrounding bytes
     must be bit-unchanged — place the canary where a **short-buffer** caller would be overrun, not
     only past index 63; (ii) `prepare({.capacity = 16, .numChildSlots = 8})` followed by
     `setCapacity(64)` **must** write indices 16..63 — the case proving the precondition has to be
     stated against `kMaxSlots` and **not** against the prepare-time capacity.
   * **Structural invariants every call:** `getLiveChildCount() <= numChildSlots()`; no two live
     children share a slot; the returned count `<= capacity()`.

**Implementation intent** — plan S4.1–S4.5 and S3.4:

* **S4.1** the strongest-K scan with `scanEnd = min(pc, reserveBase())`, the FR-009 (e) finiteness
  and `r > 0` disqualification, the `kSilentParentAmplitude` eligibility floor, and
  `insertDescending` with a **strict `>`** on ascending `i` so FR-011's tie-break falls out of the
  loop shape; `++spawnEvents_` and `++parentScans_` adjacent; the FR-014 early return that touches
  **no other counter** (plan S14 C-10).
* **S4.2** the normative per-child draw sequence — `(s0)` the pure `peekSlot` precondition **before
  any draw**, then (d1) parent, (d2) relation, (d3) detune cents, (d4) the final-attempt fallback
  cents, (d5) the hold jitter inside the latch. `kMaxSpawnAttempts = 4`. **Refusal is counted
  exactly once per offered child**, at `(s0)` or at the `!placed` tail, never both.
* **S4.3** `accept()`'s four tests in the given order — finiteness/positivity, ratio bounds
  (**reject, never clamp**), 24-cent log-domain spacing against `occupiedLog2_` (built once per
  event, siblings appended as they are accepted), and the **latched-target finiteness** test that
  plan S14 **C-4** adds (the tilt divisor reaches ×3993 at `consumerTiltDb = -12`, slot 63).
* **S4.4** the `peekSlot` / `commitSlot` split — the peek mutates **nothing**, the commit advances
  `cursor_` past the taken slot; slot choice consumes **no draw** (Clarification Q6). The split is
  load-bearing: a cursor advanced on a refused child makes SC-018 (d) fail on correct code.
* **S4.5** the latch, including the tilt-compensating divisor `/ tiltGain(slot)` with `tiltGain`'s
  identity branch copied verbatim so `consumerTiltDb == 0` is bit-inert, the **unclamped** target
  (amplitudes above 1 are accepted by `setSpectralTarget`), and `secondsToSteps`.
* **S1.6 / S4.2** `eventRng_.seed(deriveStreamSeed(deriveStreamSeed(seed_, kSaltEvent),
  std::size_t(controlStep_)))` as the **first** statement of `runEvent()`.
* **S3.4** the full `applyOutput()` body — the saturating overlap counter, the gap pad, the
  unconditional owned-region pad, the child write with
  `amplitudes[s] = detail::flushDenormal(ch.amplitude);`, the legacy-slot skip, and
  `return capacity_;`. `engaged_` is set **once**, in the latch, and cleared only by
  `prepare()`/`reset()` (Clarification Q8) — it is **not** level-tracking.

**Verify**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "BloomEngine_*" 2>&1 | tail -10
```

**Done when:** the three cases pass, the suite is green and warning-free, and the pre-implementation
run of each case was recorded as failing.

---

## Group G — The lifecycle clock and the envelope

Edits `bloom_engine.h`: alone in its group.

### T009 — Child clock, smoothstep envelope, retirement, and the criteria they unlock

**Files to edit:** `dsp/include/krate/dsp/systems/bloom_engine.h`,
`dsp/tests/unit/systems/bloom_engine_spectral_test.cpp`,
`dsp/tests/unit/systems/bloom_engine_test.cpp`.

**Write first — one new case plus three arms added to existing cases:**

1. `TEST_CASE("BloomEngine_LifecycleTimingAndC1Shape", "[bloom_engine][long]")` in the **spectral**
   TU — SC-002. Explicit overrides `childGain = 1.0f`, `depth = 1.0f`; parent amplitude swept over
   `{1.1e-5f, 0.01f, 0.35f, 1.0f}`; sample `getChildAmplitude(i)` every control chunk at 48 kHz.
   (Code `stepEngine` in this TU here — it is the first spectral-TU task that needs it.)
   * (a) at the **default 45 s** fade-in: 50 % of the latched target at `0.5·fadeIn ± 1 %`, 99 % at
     `>= 0.9·fadeIn`;
   * (b) mirrored at the **default 180 s** fade-out, ending at **exactly `0.0f`**;
   * (c) `mean(|d|)` over the first and last **2 %** of each segment `<= 10 %` of `max(|d|)`
     (smoothstep ≈ 6 %, a linear ramp = 100 %), and `|d²|` at the FadeIn→Hold and Hold→FadeOut
     junctions within **3×** the segment-interior median `|d²|`;
   * (d) monotone non-decreasing over the fade-in and non-increasing over the fade-out; the target
     and `0.0f` are reached within the configured durations; **no plateau of identical consecutive
     samples exceeds 1 s** (plan S5.3 computes the worst case at 0.67 s on the 300 s maximum fade).
     **No minimum-first-difference threshold** — that was the retracted FR-031 step floor;
   * (e) **jitter:** over 200 seeded events with `>= 2` children, `>= 90 %` have differing
     `getChildHoldSeconds`, every latched hold lies inside `holdSeconds·(1 ± 0.5)`; at
     `holdJitterFraction = 0` every latched hold equals `getHoldSeconds()` **exactly** and (a)–(d)
     are unchanged;
   * (f) **FR-033, setters moved while a child is in flight.** Spawn a child at the 45/120/180 s
     defaults; at **20 s** elapsed call `setFadeInSeconds(300.0f)`, `setHoldSeconds(0.0f)` and
     `setFadeOutSeconds(600.0f)`. The in-flight child's phase transitions must still land at the
     **originally latched** step offsets (through `getChildPhase` and `getChildElapsedSeconds`),
     `getChildHoldSeconds(i)` must be unchanged, and the amplitude series must stay monotone across
     the setter instant with **no first-difference outlier** there (the same 3×-of-interior-median
     test as (c)); the **next** spawned child must use the new values. An implementation that
     recomputed `fadeInSteps` per chunk from the current setter value — the natural mistake — passes
     every other clause of every other criterion.
2. **`BloomEngine_SlotAccountingInvariantsUnderFuzz` (behaviour TU) gains three arms:**
   * **the 1 000-seeded-config fuzz**: `capacity ∈ [1, 64]`, `numChildSlots ∈ [0, 16]`,
     `parentCount ∈ [0, 64]`, max spawn rate, max `childrenPerEvent`, **30 simulated minutes** on a
     coarse grid via `stepEngine`, with every T008 invariant re-asserted throughout;
   * **the FR-032 death arm**: spawn exactly one child, step until `getCompletedChildCount()`
     increments, and **on that same chunk** assert its former slot reads **exactly** `float(slot+1)`
     and **exactly** `0.0f` — the direct test that a retired child's slot is repadded rather than
     left sounding forever at its last fade-out value;
   * **the sticky arm (Clarification Q8)**: after all children have completed, the next
     `capacity() - 1` calls still return `capacity()` and still pad, and `isEngaged()` stays `true`.
3. **`BloomEngine_ChildRatioRelationships` (behaviour TU) gains the consumer arm and its negative
   control:** through a real `HarmonicCloud`, `getPartialFrequencyHz(slot)` equals
   `fundamentalHz × childRatio` within **0.1 cent** and `getPartialTargetAmplitude(slot)` rises
   **monotonically** over the fade-in. **Negative control:** repeat with `capacity` set *above*
   `cloud.getActivePartialCount()` and assert the consumer arm **fails** (Overview fact 1 —
   `harmonic_cloud.h:1469-1473`).

**Implementation intent** — plan S5.1, S5.2, S5.3:

* **S5.1** time counted in **integer control steps**, never accumulated seconds
  (`getChildElapsedSeconds(i) = float(step) * controlDtSec_`); the arithmetic reason is in the plan
  (225 000 float additions would drift ≈ 0.67 % on a 300 s fade, outside SC-010's ±0.5 % band);
* **S5.2** `advanceChildren()` as one pass executed **before** any spawn in the control step, phase
  derived from a single `step` counter against three latched bounds (which is what makes FR-033 true
  by construction), and `retire()` clearing the slot bit, decrementing `liveCount_` and incrementing
  `completedChildren_` **on the same control step** the amplitude reaches 0;
* **S5.3** `smoothstep(u) = u*u*(3.0f - 2.0f*u)`, the fade-out written as `1 - smoothstep(v)` and
  **never** `smoothstep(1 - v)` (only the first lands on exact `0.0f`).

**Verify**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "BloomEngine_*" 2>&1 | tail -10
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "[long]" 2>&1 | tee bloom_long.log | tail -5
```

**Done when:** all arms pass, the suite is green and warning-free, and the SC-002 (f) arm was
confirmed to fail against a deliberately per-chunk-recomputed `fadeInSteps` before the latched
implementation was written.

---

## Group H — Dormancy, depth scaling, and live capacity change

Edits `bloom_engine.h`: alone in its group.

### T010 — `setCapacity` semantics, legacy children, and the wake/depth scaling laws

**Files to edit:** `dsp/include/krate/dsp/systems/bloom_engine.h`,
`dsp/tests/unit/systems/bloom_engine_test.cpp`.

**Write first — arms added to two existing cases:**

1. **`BloomEngine_DisabledIsBitIdenticalPassThrough` gains three arms:**
   * **(d) fractional wake.** `wake ∈ {0.0f, 0.25f, 0.5f, 1.0f}` at a fixed seed and the **maximum**
     spawn rate, long enough that the `wake = 1` arm records `N₁ >= 500` events. Each arm's
     `getSpawnEventCount()` must lie within **4 σ of Poisson**, i.e.
     `|observed − wake·N₁| <= 4·sqrt(wake·N₁)`, and be **exactly 0** at `wake = 0`. Additionally,
     `getDiscardedEventCount()` must count a `triggerBloom()` issued during the `wake = 0` arm, and
     **no spawn may follow the subsequent wake** — there is no catch-up burst (Clarification Q7).
   * **(e) fractional depth**, mirroring (d): `depth ∈ {0.0f, 0.25f, 0.5f, 1.0f}` snapped before the
     run, same seed and rate, same 4 σ band, and **exactly 0** at `depth = 0` once
     `getSmoothedDepth() == 0.0f`. (FR-042 (b)'s linear scaling of the clock probability has no other
     test anywhere; SC-015 runs at a single constant depth.)
   * **in-flight `1 → 0` arm:** spawn children at `depth = 1`, then `setDepth(0.0f)` — every live
     child's `getChildTargetAmplitude(i)` is **unchanged** (FR-042 (a) scales the *latched* amplitude
     at spawn, never a live one), `isEngaged()` stays `true`, and the returned count stays
     `capacity()` until the last child completes.
2. **`BloomEngine_SlotAccountingInvariantsUnderFuzz` gains the capacity arm (Clarification Q3):**
   **200 runs**: `prepare({.capacity = 64, .numChildSlots = 8})`, spawn, `setCapacity(32)`, 30
   simulated seconds more. Assert no write at any index `>= 32`; every **legacy** child (slot outside
   `[reserveBase(), capacity())`) keeps advancing — `getChildElapsedSeconds` strictly increases and
   its phase transitions land at the **unshifted latched offsets**; no two live children share a
   slot; then `setCapacity(64)` and assert a legacy-held slot is re-used **only after** that child
   completes (FR-053).

**Implementation intent** — plan S6.1, S6.2, S6.3:

* `setCapacity(std::size_t)` clamps to `[1, kMaxSlots]` and writes `capacity_`; `numChildSlots()`
  **re-derives** `min(requestedChildSlots_, capacity_)` on every read, which is what makes a
  shrink/grow pair reversible (plan S14 **C-9**);
* growth takes effect immediately (`peekSlot` re-clamps the cursor **as a local**, `commitSlot`
  writes it back); shrinkage is **deferred** — a live child above the new capacity is **not** killed,
  retimed or evicted, is skipped by S3.4's write loop, and frees its slot on completion with the
  padding write skipped;
* the `gate` composition is already T007's; this task only proves it and adds the capacity path. The
  `wake` snap at `kWakeSilenceEpsilon` is what makes `setWake(0.0f)` and `setDormant(true)`
  behaviourally identical **by construction** rather than by luck.

**Verify**

```bash
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "BloomEngine_*" 2>&1 | tail -10
```

**Done when:** both cases pass with their new arms, the suite is green and warning-free, and the
fail-first run of the capacity arm (against a `numChildSlots()` that overwrote the request rather
than re-deriving it) was recorded.

---

## Group I — Three test-only tasks on three disjoint TUs

The header is feature-complete after Group H. T011 owns `bloom_engine_test.cpp`, T012 owns
`bloom_engine_nonfinite_test.cpp`, T013 owns `bloom_engine_spectral_test.cpp`. All three `[P]`.

### T011 [P] — SC-018: owned-slot rotation and the edge-like trigger

**Files to edit:** `dsp/tests/unit/systems/bloom_engine_test.cpp` only.

**Write:** `TEST_CASE("BloomEngine_OwnedSlotRoundRobinRotation", "[bloom_engine]")` — **200
spawn/death cycles** at `numChildSlots = 8`, **driven by `triggerBloom()`**, never by the internal
clock (plan S14 **C-6**: a seeded clock arms at different steps under a different seed, which makes
clause (d) unfalsifiable).

* (a) all 8 owned slots are used within the first **16** cycles;
* (b) two consecutive cycles never reuse a slot while another is free;
* (c) with `childrenPerEvent >= 2`, simultaneous children take **different** slots and the cursor
  still advances (cross-checked against SC-003's no-shared-slot invariant);
* (d) the slot-index sequence is identical across two runs with the **same** seed **and identical to
  a run with a different seed** — slot choice consumes no draw (Clarification Q6);
* (e) **FR-043, `triggerBloom()` is edge-like.** Call `triggerBloom()` **50 times** inside one
  control chunk (a run of `processChunk` calls whose `numSamples` sum to `< 64`, so no control step
  runs between them), then advance one control step: `getSpawnEventCount()` advances by **exactly 1**
  and `getOfferedChildCount()` by **at most `getChildrenPerEvent()`**.

**Falsification (run both, record both):** (1) make `armed_` a counter instead of a `bool` — clause
(e) must fail while every other clause still passes (that is the whole reason (e) exists: a caller
polling `SlowEventScheduler::isEventActive()` as a **level** produces `n × childrenPerEvent`
simultaneous children); (2) advance `cursor_` inside `peekSlot` instead of `commitSlot` — clause (d)
must fail.

**Verify:** `build/windows-x64-release/bin/Release/dsp_systems_tests.exe
"BloomEngine_OwnedSlotRoundRobinRotation" 2>&1 | tail -5`

**Done when:** all five clauses pass and both falsifications are recorded.

---

### T012 [P] — SC-009: the non-finite TU and the trap probe

**Files to edit:** `dsp/tests/unit/systems/bloom_engine_nonfinite_test.cpp` only. This is the
**only** TU compiled `-fno-fast-math -fno-finite-math-only` (registered by T001) and the **only**
definition of `detail::BloomEngineNonFiniteProbe` (declared in the header by T004).

**Write:** `TEST_CASE("BloomEngine_NonFiniteGuards", "[bloom_engine]")`. Every non-finite value is
built from a **bit pattern through a `volatile` sink** — `makeNonFinite(0x7FC00000)` (NaN),
`0x7F800000` (+Inf), `0xFF800000` (−Inf), `makeNonFiniteDouble(0x7FF8000000000000)` — never
`std::numeric_limits<float>::quiet_NaN()`/`infinity()`.

* (a) NaN and each Inf into **every** float setter → **rejected**, the previous value stands and the
  getter reports it (FR-009 (a));
* (b) NaN/Inf planted in `ratios[i]` / `amplitudes[i]` → that slot is **never** selected as a parent
  and **never** copied into an owned slot; the engine's own writes stay finite;
* (c) a non-finite sample rate to `prepare()` → substituted by **48 000** and then floored at
  **8 000**, with `getSampleRate()` reporting the applied value;
* after each: `stateFinite()` is **true**, and every written slot is finite with `ratio > 0` and
  `amplitude >= 0` (FR-009 (d));
* **probe arm:** define `struct detail::BloomEngineNonFiniteProbe` here (the
  `feedback_ecology.h:166-172` idiom), poison a **live** `Child::amplitude` with a NaN bit pattern,
  and assert `stateFinite()` returns **false**. Without this arm the trap is only ever observed
  returning `true`, which proves nothing.

**Falsification:** replace one setter's `detail::isFinite` guard with an unconditional assignment —
arm (a) must fail; remove the probe's poison — the probe arm must fail.

**Verify:** `build/windows-x64-release/bin/Release/dsp_systems_tests.exe "BloomEngine_NonFiniteGuards"
2>&1 | tail -5`, and confirm on a GCC/Clang leg (WSL/MSYS2) that this TU alone carries
`-fno-fast-math`.

**Done when:** every arm passes on both toolchains and `tools/lint-nonfinite-symbols.js` is clean.

---

### T013 [P] — SC-001: children appear and disappear without clicks

**Files to edit:** `dsp/tests/unit/systems/bloom_engine_spectral_test.cpp` only. Code `CloudRig`,
`countClicks(buffer, sigma)` and `smallestZeroSigma(buffer)` here (first spectral-TU task that needs
them; the click fixture is transcribed from `atmosphere_engine_spectral_test.cpp:399-423` with all
six `ClickDetectorConfig` fields designated-initialised and `.sampleRate` set to the render rate).

**Write:** `TEST_CASE("BloomEngine_ChildSpawnAndDeathAreClickFree", "[bloom_engine]")` — `CloudRig`
at `richness = 1.0` (so `getActivePartialCount() == 64`), fades compressed to **2 s / 1 s / 3 s**.
For each of **10 seeds**, render twice — bloom engaged, and `numChildSlots = 0` as the reference.

* The gate is **differential**: `countClicks(bloom, 5.0) <= countClicks(ref, 5.0)`;
* **plus 0 detections inside the 200 ms window after every spawn instant and every death instant**
  (the `feedback_ecology_test.cpp:3689-3696` windowed idiom; instants taken from
  `getSpawnEventCount()` / `getCompletedChildCount()` transitions);
* on failure, report `smallestZeroSigma()` for **both** runs.

**Tagging:** add `[long]` **only if the measured runtime says so** (> ~15 s). The roadmap rule tags a
case `[long]` only when it is expensive **and** toolchain-independent; record the measured runtime
either way.

**Falsification:** replace the smoothstep fade-in with an instantaneous jump to the latched target —
the windowed clause must fail while the differential clause may not.

**Verify:** `build/windows-x64-release/bin/Release/dsp_systems_tests.exe
"BloomEngine_ChildSpawnAndDeathAreClickFree" 2>&1 | tee bloom_clicks.log | tail -10`

**Done when:** all 10 seeds pass both clauses, the tagging decision is recorded with its measured
runtime, and the falsification is recorded.

---

## Group J — Determinism and sample-rate independence

T014 owns `bloom_engine_test.cpp`, T015 owns `bloom_engine_spectral_test.cpp`. Both `[P]`.

### T014 [P] — SC-008: seed determinism, partition invariance, and RNG step-purity

**Files to edit:** `dsp/tests/unit/systems/bloom_engine_test.cpp` only. Code `CloudRig` in this TU
here (first behaviour-TU task that needs it).

**Write three cases** (the early partition case from T007 is **extended**, not duplicated):

* `TEST_CASE("BloomEngine_SeedDeterminism", "[bloom_engine]")` — two instances, same seed,
  configuration and input, **10 simulated minutes**: identical child tables (**exact** on the
  integer/enum surface: slot, phase, relation, fallback flag, latched step counts) and
  `compareFingerprints` within `kMetricTolerance`/`kSampleTolerance` on a `CloudRig` render.
  **No bit-exact float golden is checked in** (`tools/lint-float-bit-goldens.js`).
* `BloomEngine_BlockPartitionInvariance` (extend T007's case) — the same total sample count
  delivered as `64`, `512`, `2048` and a ragged `{1, 7, 383, 4096, …}` sequence gives an **identical
  child table** and fingerprint-equal output; the rejected-call arm is extended to assert
  `getChildElapsedSeconds(i)` is unchanged across a `numSamples == 0` call.
* `TEST_CASE("BloomEngine_RngPositionIsStepPure", "[bloom_engine]")` — SC-008 (c), restated per plan
  S14 **C-1**:
  (i) **four** instances suppressed for the same prefix by *different* means — `setDormant(true)`,
  `setWake(0.0f)`, `setDepth(0.0f)`, `setSpawnRateHz(0.0f)` — then enabled, produce **identical**
  child tables and outputs over the post-enable window, where **the window opens
  `ceil(kGainRampMs · controlRateHz / 1000)` control steps after the enable instant** (50 ms ≈ **37
  steps** at 48 kHz) — write it as that expression, not as a magic number. The window is required
  because only three of the four re-enable instantly; `setDepth(1.0f)` re-enables through the ramp,
  whose gate is strictly lower for the whole 50 ms, so an event can arm for one instance and not
  another (~0.25 % of runs per seed at the max rate) and the arm would fail on **correct code**.
  Assert `getSmoothedDepth() == 1.0f` **exactly** at the window's open as an explicit precondition,
  and compare each instance's child table and event-step list only from that step onward.
  (ii) an instance awake and spawning throughout arms events at **exactly the same control-step
  indices** (recorded from `getSpawnEventCount()` transitions) as the suppressed instances do after
  their enable point.
  (iii) slot choice consumes no draw — cross-reference SC-018 (d), asserted by T011.

**Falsification:** make `eventRng_` a persistent sequential stream instead of the per-event
counter-based re-seed (plan S1.6) — case 3 (i) must fail, because an instance that spawned during
the prefix consumes draws a suppressed one does not.

**Verify:** `build/windows-x64-release/bin/Release/dsp_systems_tests.exe "BloomEngine_*" 2>&1 | tail -10`

**Done when:** all three cases pass and the falsification is recorded.

---

### T015 [P] — SC-010: sample-rate independence and re-prepare

**Files to edit:** `dsp/tests/unit/systems/bloom_engine_spectral_test.cpp` only.

**Write:** `TEST_CASE("BloomEngine_SampleRateIndependence", "[bloom_engine][long]")` at **44 100 /
48 000 / 88 200 / 96 000 / 192 000 Hz**, `depth = 1`, `wake = 1`.

* **Event-rate arm**, driven with `numChildSlots = 0` (the engine stays disengaged, so the run is a
  bare clock and costs almost nothing) over **N = 2 000 events**: the mean inter-event time lies
  inside `1/spawnRateHz · (1 ± 4/sqrt(N))` — write it as that expression so the 4 σ arithmetic is
  visible rather than a magic ±8.94 %.
* **Timing arm:** a child's fade-in duration **in seconds** is within **±0.5 %** of the configured
  value at every rate (this is the band plan S5.1's integer-step design exists to hold).
* **Re-prepare arm:** re-preparing mid-lifecycle leaves the exact post-prepare state — no half-faded
  child, `getSeed()` retained, every counter zeroed, `isEngaged() == false`.

**Falsification:** accumulate `elapsedSeconds += controlDtSec_` instead of counting integer steps —
the timing arm must fail at the 300 s fade (plan R1: ≈ 0.67 % drift).

**Verify:** `build/windows-x64-release/bin/Release/dsp_systems_tests.exe
"BloomEngine_SampleRateIndependence" 2>&1 | tee bloom_srate.log | tail -10`

**Done when:** all three arms pass at all five rates and the falsification is recorded.

---

## Group K — The cloud-capacity contract and the 30-minute trajectory

T016 owns `bloom_engine_test.cpp`, T017 owns `bloom_engine_spectral_test.cpp`. Both `[P]`.

### T016 [P] — SC-016: children land where the cloud can actually sound them

**Files to edit:** `dsp/tests/unit/systems/bloom_engine_test.cpp` only.

**Write:** `TEST_CASE("BloomEngine_ChildrenAreAudibleWithinCloudActiveCount", "[bloom_engine]")` —
a real `HarmonicCloud` at a `richness` chosen so `getActivePartialCount() ≈ 32`, with
`PrepareConfig{.capacity = cloud.getActivePartialCount(), …}`.

* every live child's slot is `< cloud.getActivePartialCount()`;
* `cloud.getPartialTargetAmplitude(slot) > 0` once past the fade-in onset — **audible**, not merely
  written (the cloud zeroes and `continue`s every slot at or above `activeCount_` **before** the
  spectral-target branch, `harmonic_cloud.h:1469-1473`);
* `cloud.hasSpectralTarget()` is **true after every handoff** — the direct test that the array was
  **accepted** rather than wholesale-rejected (`harmonic_cloud.h:812-818`);
* repeat with `parentCount ∈ {0, 1, reserveBase()/2}` and a **poisoned gap**, so an implementation
  that does not pad `[parentCount, reserveBase())` fails here.

**Falsification:** set `capacity = HarmonicCloud::kMaxPartials` while the cloud's richness leaves
`activeCount_ ≈ 32` — the first clause must fail. This is plan **R14**'s tripwire (a Phase-10 caller
sizing capacity from `kMaxPartials` instead of `getActivePartialCount()` silences every child), and
it is the criterion that proves Overview fact 1 is real rather than inferred.

**Verify:** `build/windows-x64-release/bin/Release/dsp_systems_tests.exe
"BloomEngine_ChildrenAreAudibleWithinCloudActiveCount" 2>&1 | tail -5`

**Done when:** every clause passes at all three `parentCount` values and the falsification is
recorded.

---

### T017 [P] — SC-004: the 30-minute evolution trajectory

**Files to edit:** `dsp/tests/unit/systems/bloom_engine_spectral_test.cpp` only.

**Write:** `TEST_CASE("BloomEngine_ThirtyMinuteEvolutionTrajectory", "[bloom_engine][long]")` — a
**30-minute** `CloudRig` render at defaults, logging **per second** (streaming statistics only):
`centroidHz` (`tests/test_helpers/audio_features.h:88`), the count of `i <
cloud.getActivePartialCount()` whose `cloud.getPartialCurrentAmplitude(i)` exceeds −60 dB of the
largest such value, and broadband RMS.

* (a) **differential** — the same seed and configuration rendered twice (bloom on;
  `numChildSlots = 0` off): `max|centroid_on − centroid_off| >= 5 %` of the reference mean,
  **sustained for ≥ 60 s** after a spawn, and the bloom run must reach a partial count the reference
  never reaches;
* (b) RMS of the last 5 minutes within **±1.5 dB** of minutes 5–10; bloom peak below
  `HarmonicCloud::kOutputClamp` (2.0) and within **+6 dB** of the reference run's peak (ruled
  2026-09-14; the absolute `< 1.0` bound contradicted FR-023);
  `cloud.stateFinite()` true throughout (never static, never divergent — roadmap line 353);
* (c) **pooled over 5 seeds**: `Σ getSpawnEventCount() >= 30` against `λ_total = 37.5` written into
  the test as a **named constant**, with the per-seed counts reported via `WARN`.

**Falsification:** run the bloom arm with `depth = 0` — clause (a) must fail (the two renders become
identical).

**Verify:** `build/windows-x64-release/bin/Release/dsp_systems_tests.exe
"BloomEngine_ThirtyMinuteEvolutionTrajectory" 2>&1 | tee bloom_30min.log | tail -20`

**Done when:** all three clauses pass, the per-seed event counts are recorded, and the falsification
is recorded.

---

## Group L — Tilt compensation and the boundedness soak

T018 owns `bloom_engine_test.cpp`, T019 owns `bloom_engine_spectral_test.cpp`. Both `[P]`.

### T018 [P] — SC-017: tilt-compensated children land at the intended level

**Files to edit:** `dsp/tests/unit/systems/bloom_engine_test.cpp` only.

**Write:** `TEST_CASE("BloomEngine_TiltCompensationMatchesIntendedLevel", "[bloom_engine]")` — a
cloud at `richness = 1.0` with `setSpectralTiltDb(-6.0f)`, the engine with
`setConsumerTiltDb(-6.0f)`, `childGain = 1.0f`, `depth = 1.0f` (explicit overrides), children spread
across both parent and reserved slots.

* Once a fade-in completes, `cloud.getPartialCurrentAmplitude(childSlot)` lies within **±0.5 dB** of
  `parentAmplitude_at_spawn` — whatever slot the child landed in (the latch's `/ tiltGain(slot)`
  divisor cancels the cloud's own slot-indexed `tiltGain(i)` multiply, `harmonic_cloud.h:1493`);
* **negative control:** with `setConsumerTiltDb(0.0f)` against a cloud still at `-6.0f`, the measured
  level must be **outside the band by > 10 dB**.

The ±0.5 dB band has ~7 orders of margin over any `std::log2`/`std::exp2` last-bit difference between
toolchains (plan R11) — do **not** tighten it into a bit-exact comparison.

**Falsification:** delete the `/ tiltGain(slot)` divisor — the first clause must fail for children in
high slots while children in slot 0 still pass (the identity branch returns exactly `1.0f`).

**Verify:** `build/windows-x64-release/bin/Release/dsp_systems_tests.exe
"BloomEngine_TiltCompensationMatchesIntendedLevel" 2>&1 | tail -5`

**Done when:** both clauses pass and the falsification is recorded.

---

### T019 [P] — SC-015: the eight-hour accelerated soak

**Files to edit:** `dsp/tests/unit/systems/bloom_engine_spectral_test.cpp` only.

**Write:** `TEST_CASE("BloomEngine_EightHourAcceleratedSoak", "[bloom_engine][long]")` — **8
simulated hours** via `stepEngine` (no audio render), at SC-011's worst case
(`numChildSlots = 16`, `parentCount = 48`, `K = 8`, `childrenPerEvent = 4`, **max spawn rate**),
over **25 seeds**, streaming statistics only. This is the roadmap's boundedness gate (lines 522–524):
a drone component that can run away or die overnight is broken by definition.

* (a) the live-child count never exceeds `numChildSlots()`;
* (b) every emitted amplitude lies in `[0, getChildTargetAmplitude(i)]` and every target equals
  `parentAmp · childGain · depth / tiltGain(slot)` within **1e-6** (at the default
  `consumerTiltDb = 0`, `tiltGain ≡ 1`). **No absolute `[0, 1]` bound** — FR-023 deliberately does
  not clamp the target, and `setSpectralTarget` accepts amplitudes above 1;
* (c) every emitted ratio lies in **[0.5, 128]**;
* (d) `stateFinite()` true throughout;
* (e) `getSpawnEventCount() ≈ spawnRateHz · depth · wake · T` within **±25 %**;
  `getCompletedChildCount() ≈ min(childrenPerEvent · events, numChildSlots · T/(fadeIn+hold+fadeOut))`
  within **±25 %**, **reporting which branch of the `min` is active**; and the **exact** identity
  `getOfferedChildCount() == getSpawnedChildCount() + getRefusedChildCount()` (plan S14 **C-7**);
* (f) **no stall**: `getLiveChildCount()` does not hold one value for more than **60 simulated
  minutes**, and `getSpawnEventCount()` advances at least once per 60 simulated minutes.

**Falsification:** skip the `retire()` slot release — clause (a) or (f) must fail.

**Verify:** `build/windows-x64-release/bin/Release/dsp_systems_tests.exe
"BloomEngine_EightHourAcceleratedSoak" 2>&1 | tee bloom_soak.log | tail -20`

**Done when:** all six clauses pass on all 25 seeds, the active `min` branch is recorded, and the
falsification is recorded.

---

## Group M — The CPU budget, measured alone

### T020 — SC-011: re-point the probe at the real engine and gate the budget

**Files to edit:** `dsp/tests/unit/systems/bloom_engine_perf_test.cpp` only. **Nothing else may be
running** during any measurement in this task.

**Write:** `TEST_CASE("BloomEngine_CpuBudget", "[bloom_engine][.perf]")`, beside T003's probe case,
and **re-point T003's four probe arms at the real `BloomEngine`** (arm (a) `advanceChildren()` only,
(b) `applyOutput()` only, (c) one full `runEvent()`, (d) the disengaged clock) so a future regression
can be attributed rather than guessed (FR-073). The probe stays `WARN`-reported and **unasserted**.

* Workload: worst case — `numChildSlots = 16` with all 16 live, `parentCount = 48`, `K = 8`,
  `childrenPerEvent = 4`, maximum spawn rate;
* Basis: **ns per 512-sample block at 48 kHz**, **best-of-25 × 500 blocks after 400 warm-up blocks**;
* **Threshold: `<= kBudgetNs = 10 667 ns/block`** (0.1 % of one core, FR-072), `static_assert`ed
  against the checked-in baseline so the ceiling is evaluated on every CI leg even though the case
  never runs there;
* the percent-of-core figure is **reported, never asserted**.

**If the figure misses:** **STOP.** Do not lower `kMaxChildren`, raise `kBudgetNs`, relax a threshold
or shrink the workload. The two pre-authorised levers are caller-side (plan S12.2): call
`processChunk` at a larger `numSamples` (one 512-sample call is exactly equivalent to eight 64-sample
calls, FR-006, asserted by SC-008 (b)) and narrow `capacity - parentCount`. **The dirty-flag skip is
unavailable** (plan S12.2, normative — it would fail SC-003 and SC-016). If neither closes the gap,
surface the measured per-arm table to the user for a ruling, the route that amended Phase 2 and
Phase 5.

**Diagnostic anchor:** plan S12.3 predicts ~400 stores and ~16 polynomial evaluations per block —
roughly two orders below the ceiling. **If the measured figure is anywhere near 10 667 ns, something
structural is wrong** (most likely a per-chunk parent scan violating FR-013), and SC-005's
`getParentScanCount() == getSpawnEventCount()` clause is the test that names it.

**Verify (ALONE)**

```bash
node tools/run-cpu-tests.js dsp_systems_tests 2>&1 | tee bloom_perf.log | tail -40
```

**Done when:** the gated figure is under `kBudgetNs`, the baseline `static_assert` is checked in, the
four re-pointed arm figures are recorded, and the measurement was taken with nothing else executing
(no build, no clang-tidy, no second suite, no parallel agent) on an idle machine.

---

## Group N — Integration: registration audit, full-suite run, portability gate

Three tasks. T021 and T023 are read-only over the repo; T022 runs builds and suites. Run them in the
listed order (T022's evidence is what T023's clang-tidy pass is taken against).

### T021 — Registration-completeness audit and the FR-080 byte-unchanged check

**Files:** read-only, plus any **fix** the audit finds in `dsp/tests/CMakeLists.txt` or
`dsp/lint_all_headers.cpp`.

1. All four TUs appear **exactly once** in the `dsp_systems_tests` enumerated list
   (`dsp/tests/CMakeLists.txt`, list opens `:324`) — an unregistered TU silently never runs;
2. **exactly one** of them (`bloom_engine_nonfinite_test.cpp`) appears in the `-fno-fast-math`
   `set_source_files_properties()` block, and it appears in **no other**
   `set_source_files_properties()` call (a file may be in only one);
3. `#include <krate/dsp/systems/bloom_engine.h>` is present in `dsp/lint_all_headers.cpp` in the
   Layer-3 section, before the Layer-4 block;
4. **FR-080:** `git diff --name-only -- dsp/include/` names `systems/bloom_engine.h` and **nothing
   else**; `git status --short` shows no unexpected file;
5. `git diff --stat` for the phase shows exactly: the one new header, the four new TUs, the two
   `dsp/tests/CMakeLists.txt` edits, the one `dsp/lint_all_headers.cpp` line — and no
   `tests/test_helpers/` change (plan S10.1: this phase adds no helper).

**Done when:** all five checks pass and their output is recorded in the compliance notes.

---

### T022 — The full-suite run, the `[long]` set, and the consumer regression

**Files:** none. This is the evidence run.

```bash
CMAKE="C:/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe 2>&1 | tee bloom_suite.log | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "[long]" 2>&1 | tee bloom_long_all.log | tail -5

# SC-012 / FR-081: the suites that compile the untouched shared headers
"$CMAKE" --build build/windows-x64-release --config Release \
    --target dsp_core_tests dsp_primitives_tests dsp_processors_tests dsp_effects_tests
for t in dsp_core_tests dsp_primitives_tests dsp_processors_tests dsp_effects_tests; do \
  build/windows-x64-release/bin/Release/$t.exe 2>&1 | tail -3; done
```

* Zero compiler warnings in the build output — check it, do not assume it.
* Every Seraphis-facing case stays green, in particular the cloud, morph, entropy and
  `seraphis_voice` cases: none of them **can** change if FR-080 holds, so a failure there is a
  violation of FR-080, not a flake.
* Capture each run to its log on the **first** run; never re-run a suite to grep its output.
* **Do not** run the `[.perf]` cases here — they were measured alone in T020 and a suite run
  invalidates them.
* Any failure — including one that looks pre-existing — **halts the phase** and is fixed or
  explicitly justified. "Pre-existing" and "flaky" are not outcomes.

**Done when:** `dsp_systems_tests` (both the default set and `[long]`) and the four other layer
suites are green, with the tail of each log recorded.

---

### T023 — Portability, lints, and clang-tidy (SC-013, final pass)

**Files:** none created or edited.

```bash
node tools/check-portability.js
node tools/lint-odr.js
node tools/lint-layers.js
node tools/lint-nonfinite-symbols.js
node tools/lint-float-bit-goldens.js
node tools/lint-simd-aligned-loadstore.js
clang-tidy -p build/windows-ninja dsp/tests/unit/systems/bloom_engine_test.cpp 2>&1 | tee tidy_bloom.log
```

* All six gates clean. `check-portability.js` **only compiles** — it cannot see a runtime platform
  gate, so also compile the header and all four TUs under **g++/libstdc++** (MSYS2 or WSL, the
  Phase-3 precedent) and confirm the `-fno-fast-math` TU is the only one carrying that flag.
* clang-tidy: single-TU on Windows for a change set this small (never the `.sh`, which globs headers
  and runs for an hour); repeat for the other three TUs and for the header via any TU that includes
  it. **Fix every warning, not only the ones in new code.**
* `[long]` and perf cases are excluded from the per-push CI filter but run nightly on all three OSes
  — confirm the tags are as T013/T009/T015/T017/T019/T020 left them: `[long]` only where the case is
  expensive **and** toolchain-independent, `[.perf]` on both perf cases, and **never** on SC-003 or
  SC-009, which are the cross-platform sentinels.

**Done when:** all six gates are clean, the g++ leg compiles, clang-tidy is warning-free on all four
TUs, and the tag audit is recorded.
