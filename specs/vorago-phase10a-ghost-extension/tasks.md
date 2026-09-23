# Tasks: Vorago Phase 10a — AtmosphereEngine Ghost Extension

**Spec:** `specs/vorago-phase10a-ghost-extension/spec.md`
**Plan:** `specs/vorago-phase10a-ghost-extension/plan.md`
**Base commit:** `374580d7d0f0631561413310bd3085e15ba7279c` ("feat(vorago): Phase 10 Voice and Engine")
**Production files this phase may touch (and no others):**
`dsp/include/krate/dsp/systems/atmosphere_engine.h`, `dsp/include/krate/dsp/systems/vorago_engine.h`.
**Test targets:** `dsp_systems_tests` (every new case), plus `seraphis_tests` / `Seraphis` /
`pluginval` as the untouched-consumer gate (SC-006 clause 5).

---

## How to read this list

- Tasks are **T001…**, grouped into **GROUPS** that run in order. Inside a group, a task marked
  **[P]** is parallel-safe: it creates or edits **only files no other task in that group touches**.
  Everything that edits a shared file — `atmosphere_engine.h`, `vorago_engine.h`,
  `dsp/tests/CMakeLists.txt`, `tools/check-seraphis-green.js`, or a TU a sibling task also edits —
  is alone in its group.
- Every task follows the repo's canonical order: **failing test first → implement → zero warnings →
  tests pass**. Where a task is test-only, its "verify" step names what must be **red** and why.
- Build command, everywhere (Windows, always the full CMake path):
  ```bash
  CMAKE="C:/Program Files/CMake/bin/cmake.exe"
  "$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
  build/windows-x64-release/bin/Release/dsp_systems_tests.exe "<CaseName>*" 2>&1 | tail -20
  ```
- **No commit tasks.** Commits happen outside this workflow.
- **Every threshold below is a floor/ceiling that may not be relaxed, and no workload may be shrunk,
  to make a case pass.** A red case is a finding.

### Deviation from the "CMake registration last" convention, stated once

The instruction template puts CMake registration in the final group. That is impossible here: a TU
that is not enumerated in `dsp/tests/CMakeLists.txt` never compiles and never runs
(`dsp/tests/CMakeLists.txt:377-379` says so in the file itself), so every "write the failing test
first" step would be unverifiable. Registration is therefore **one single task, T003, placed early**,
and the final group keeps a **registration audit** (T024) that re-checks the enumerated list and the
`-fno-fast-math` list against the TUs that actually exist at the end of the phase.

---

## GROUP 1 — Scaffolding: budget header, fixtures, CMake, the seraphis-green exception

Sequential. Every task here touches a file another task in the phase also touches, or must exist
before anything else compiles.

---

### T001 — Extract the Phase 10 perf constants into `vorago_perf_budget.h` (FR-047, Q7, §8 ruling 3)

**Create** `dsp/tests/unit/systems/vorago_perf_budget.h` (new, test-only header, no CMake entry needed
— it is a header in an already-enumerated directory).

**Edit** `dsp/tests/unit/systems/vorago_perf_test.cpp` — **this is the only modification this phase may
make to that file, anywhere** (FR-047, SC-006 clause 1).

Move, **unedited in value**, with their existing provenance comments copied verbatim:

| Constant | Current site | Value |
|---|---|---|
| `kSr48` | `vorago_perf_test.cpp:155` | `48000.0` |
| `kBlockSize` | `:157` | `512` |
| `kBlockBudgetNs` | `:160` | `(kBlockSize / kSr48) * 1.0e9` |
| `kReferenceNs` | `:168` | `kBlockBudgetNs * 0.30` (= 3 200 000) |
| `kCavernMeasuredNsPerBlock` | `:215` | `124497.0` |
| `kEngineMeasuredNsAtPoly4` | `:256` | `2566170.0` |
| `kEngineBaselineNsAtPoly4` | `:257` | `2694479.0` |

`kSr48`, `kBlockSize` and `kBlockBudgetNs` move **because `kReferenceNs` is derived from them**
(verified: `:160`, `:168`) — this is §8 ruling item 3, recorded explicitly in the compliance table as
"seven definitions moved, four of them the FR-047 four, three of them `kReferenceNs`'s derivation
inputs; no value edited".

In `vorago_perf_test.cpp`, replace those seven definitions with `#include "vorago_perf_budget.h"`.
**Leave in that TU, unedited:** `kRegressionFactor` (`:165`), `kBaselineWithCavernNs`,
`kAvailableRegressionHeadroom` (`:258-260`), `kCavernBaselineNsPerBlock`, `kMaxAdmissibleNs`, and all
three `static_assert`s (`:263-266` FR-083's `baseline == ceil(measured × 1.05)`, `:269`
`baseline + Cavern <= kReferenceNs`, `:272-274`).

**Test first:** none — this is a pure move whose test is the *existing* `static_assert` set. If any of
the three static_asserts stops compiling, the move was not value-preserving.

**Verify:** `dsp_systems_tests` builds with **zero warnings**; run
`build/windows-x64-release/bin/Release/dsp_systems_tests.exe "Vorago*" 2>&1 | tail -5` green.
Record the `git diff HEAD --numstat -- dsp/tests/unit/systems/vorago_perf_test.cpp` row in the
compliance table.

---

### T002 — Create the shared ghost fixture header `atmosphere_ghost_fixtures.h`

**Create** `dsp/tests/unit/systems/atmosphere_ghost_fixtures.h`.

It must compile **against the base commit** (T005 copies it into a base-commit worktree), so it may
use **only shipped API** — no `setGrainReverseProbability`, no `triggerGrain`, no new accessor.

Contents, all `inline` in `namespace VoragoGhostFix`:

1. `struct GhostConfig` + `inline void applyVoragoGhost(AtmosphereEngine&)` — FR-017's seven values
   exactly as `vorago_perf_test.cpp:609-630`'s `buildAtmosphere()` writes them:
   `density = 0.30f`, `grainSeconds = 12.0f`, `pitchSemitones = -12.0f`, `positionSpread = 0.90f`,
   `blur = 0.85f`, `decorrelation = 0.85f`, `setLevel(0.60f)`; `captureSeconds = 20.0f`, seed 1.
2. `inline std::size_t preRollFullRing(AtmosphereEngine&, ...)` — the **full-ring pre-roll defined
   once** (plan §3.2 / P-2): from a fresh `prepare()`/`reset()`, render exactly
   `engine.getCaptureCapacitySamples()` samples (`atmosphere_engine.h:1135-1137`), because
   `RollingCaptureBuffer::getAvailableSamples()` is `std::min(samplesWritten_, capacity_)`
   (`rolling_capture_buffer.h:443-445`). Returns the rendered sample count. **No fixture anywhere in
   this phase states a pre-roll in seconds.**
3. `inline double availableSamples(std::size_t rendered, std::size_t capacity)` =
   `std::min(rendered, capacity)` — `AtmosphereEngine` exposes no `getAvailableSamples()` and this
   phase does not add one.
4. `inline void excitePinkPlusTone(std::span<float> l, std::span<float> r, std::uint32_t seed)` — a
   deterministic excitation built with `Krate::DSP::Xorshift32` (`core/random.h`). **Never
   `<random>`** (not portable; `vorago_perf_test.cpp:376-379`).
5. `inline void exciteChirp(std::span<float>, double sr, double f0, double f1, double seconds)` —
   SC-002's linear chirp.
6. `inline bool isNonFiniteBits(float)` — bit-pattern non-finiteness (exponent all ones), used instead
   of `std::isnan`/`std::isinf` anywhere (FR-043, §3.3).
7. `inline std::size_t schedulerTicks(std::size_t renderedSamples, double sampleRate, float density)`
   = `1 + floor(renderedSeconds / (1.0 / density))` — the free tick at sample 0 plus the periodic ones
   (`grain_scheduler.h:40` sets `samplesUntilNextGrain_ = 0.0f`, `:73-76` decrements *before* the
   `<= 0.0f` test), used by SC-005 (b), SC-005 (d) and SC-008 (d) so no test hard-codes 65.

**Test first:** none (a fixture header). Its correctness is exercised by every case that uses it.

**Verify:** the header compiles standalone (it is `#include`d by T003's stubs); zero warnings.

> **Accepted at the 2026-09-23 plan-stage rulings:** the spec's *New components* table now names
> this header beside `vorago_perf_budget.h` as the phase's two test-only headers (ODR sweep recorded
> there: 0 hits for the file name and for `VoragoGhostFix`). The rejected fallback — duplicating the
> fixtures into each of the five TUs with a citation — would have made "the base-commit worktree runs
> the *identical* fixture function" (SC-001's mechanism) a claim no reader can check.

---

### T003 — Create the five TU stubs and register everything in CMake (FR-042) — the single CMake task

**Create**, each a compiling stub (`#include <catch2/catch_test_macros.hpp>`, the fixture header, and
one `TEST_CASE("AtmosphereGhost_Placeholder", "[.placeholder]") { SUCCEED(); }` that later tasks
delete):

| TU (`dsp/tests/unit/systems/`) | Will hold |
|---|---|
| `atmosphere_ghost_test.cpp` | SC-001, SC-002, SC-004 (short arm, **never** `[long]`), SC-005, SC-006, SC-007 (a)–(c), SC-008 (a)(c)(d)(e), SC-011, SC-012, FR-049's short twin |
| `atmosphere_ghost_longrun_test.cpp` | SC-003 `[long]`, SC-007 (d) `[long]`, SC-004's sweep sibling `[long]` (only if measured > 15 s) |
| `atmosphere_ghost_nonfinite_test.cpp` | SC-008 (b) — the **only** case that injects NaN/±Inf bit patterns |
| `atmosphere_ghost_perf_test.cpp` | SC-009 `[.perf]` |
| `vorago_ghost_ext_test.cpp` | SC-010 |

**Edit** `dsp/tests/CMakeLists.txt`:

1. Append to the `dsp_systems_tests` source list, in a new commented block **after** the Phase 10
   Vorago block that ends at `:516`:
   ```cmake
   # Vorago Phase 10a (specs/vorago-phase10a-ghost-extension): the AtmosphereEngine
   # ghost extension (reverse grains + triggerGrain). ENUMERATED, not globbed -
   # an unregistered TU silently drops out of the build and its cases never run.
   unit/systems/atmosphere_ghost_test.cpp
   unit/systems/atmosphere_ghost_longrun_test.cpp
   unit/systems/atmosphere_ghost_nonfinite_test.cpp
   unit/systems/atmosphere_ghost_perf_test.cpp
   unit/systems/vorago_ghost_ext_test.cpp
   ```
2. Append to the `-fno-fast-math` opt-in list, beside
   `unit/systems/atmosphere_engine_nonfinite_test.cpp` at **`:896`** (verified this session by
   `grep -n`), **exactly one** entry:
   ```cmake
   # Vorago Phase 10a: SC-008 (b) injects NaN/Inf via bit patterns in this TU.
   # ONLY this one of the five Phase 10a TUs is listed - the perf TU in particular
   # must stay out, or -fno-fast-math would move the figures it measures.
   unit/systems/atmosphere_ghost_nonfinite_test.cpp
   ```
   The rule the block itself states at `:881-895` is: only the TU that **injects** bit patterns is
   listed. SC-004's *detection-only* NaN/Inf check stays on the fast-math path and uses T002's
   bit-pattern predicate.

`tests/test_helpers/` is an INTERFACE target exposing the whole directory
(`tests/test_helpers/CMakeLists.txt:7-12`), so `<render_fingerprint.h>`, `<allocation_detector.h>`,
`<artifact_detection.h>` and `vorago_fixtures.h` need no CMake edit.

**Verify:** `dsp_systems_tests` builds with zero warnings; `dsp_systems_tests.exe --list-tests` shows
the five placeholders.

---

### T004 — `tools/check-seraphis-green.js`: the single named exception (SC-006 clause 1, Q7)

**Edit** `tools/check-seraphis-green.js` only.

Add `'vorago_perf_test.cpp'` to `ALLOWED_SYSTEMS_TUS` (the array at `:126-132`, verified this
session), with a comment naming the **one** permitted modification: "Phase 10a FR-047 — the only edit
is replacing seven constant definitions with `#include \"vorago_perf_budget.h\"`; every other line is
unchanged and its own static_asserts stay in place."

Do **not** touch `ALLOWED_SERAPHIS_PLUGIN_FILES` (`:120-124`): zero modified files under
`plugins/seraphis/` stands. Do **not** touch `APPEND_ONLY_HEADERS` yet — that is T023, after the real
deletion counts are measurable.

**Verify:** `node tools/check-seraphis-green.js` is clean with T001's edit in the tree.

---

### T005 — Harvest the base-commit references (SC-001 clauses 1–3, SC-010 (a), SC-011 (c))

No production or test file in the working tree changes except the constants appended to
`atmosphere_ghost_fixtures.h` at the end.

1. `git worktree add ../iterum-basecommit 374580d7d0f0631561413310bd3085e15ba7279c`
2. Copy **T002's `atmosphere_ghost_fixtures.h`** into the worktree unchanged, plus a throwaway
   reference-dump TU (registered in the worktree's CMake only — the worktree is never committed).
3. Configure and build `dsp_systems_tests` there; run the dump.
4. Transcribe the following back into `atmosphere_ghost_fixtures.h` as `constexpr` definitions, each
   under a **PROVENANCE block** naming the base commit SHA, the measuring machine, the
   compiler/toolchain and the date, and carrying the perf-TU's own wording
   (`vorago_perf_test.cpp:236-253`): *stored references, re-measured under a documented rule, never
   hand-edited — a failure here is a finding, not an invitation to update the literal.*

| Constant | Fixture that produces it |
|---|---|
| `kBaseCommitFingerprint` (`RenderFingerprint`: `rms`, `peak`, `meanAbs`, `totalVariation`, 32 `checkpoints`, `render_fingerprint.h:63-69`) | 60 s at 48 kHz, `applyVoragoGhost`, `captureSeconds = 20`, seed 1, `excitePinkPlusTone`, left channel |
| `kBaseCommitGrainRngState` (`std::uint32_t`) | `getGrainRngState()` (`atmosphere_engine.h:1118`) after that same render |
| `kBaseCommitTotalBorn`, `kBaseCommitTotalRetired`, `kBaseCommitSkipPoolFull`, `kBaseCommitSkipRingCold`, `kBaseCommitLatencySamples` | the same render (`:1053-1070`, `:1128`) |
| `kBaseCommitVoragoFingerprint` | 60 s Vorago render at `VoragoEngineConfig` defaults via `TestUtils::Vorago::makeEngine` (`tests/test_helpers/vorago_fixtures.h:734`) / `renderEngine` (`:750`) |
| `kBaseCommitShortPreRollBorn / …Retired / …RingCold / …PoolFull / …GrainRngState` | SC-011 (c)'s forward arm: 5 s (240 000-sample) pre-roll at SC-011's configuration, probability irrelevant (base commit has none), then the 40 000-sample span |

5. `git worktree remove ../iterum-basecommit`.

**Verify:** the constants compile; the dump is reproducible twice on the same machine (integers
bit-identical, fingerprint metrics identical). **No bit-exact float golden is introduced** — the
fingerprint is only ever consumed through `compareFingerprints(...).withinTolerance()` at measured
bounds (roadmap line 605, FR-046).

---

## GROUP 2 — Failing tests for the reverse control surface (S1) and introspection (S5)

Both tasks create **disjoint new-file content**: T006 owns `atmosphere_ghost_nonfinite_test.cpp`,
T007 owns `atmosphere_ghost_test.cpp`. **[P] — run them in parallel.**

---

### T006 [P] — SC-008 (b): the non-finite setter contract (FR-003, Q2)

**Edit** `dsp/tests/unit/systems/atmosphere_ghost_nonfinite_test.cpp` (delete its placeholder).

`TEST_CASE("AtmosphereGhost_NonFiniteSetter", "[atmosphere][ghost][nonfinite]")`

Build NaN and ±Inf from bit patterns laundered through a `volatile std::uint32_t` (never a literal,
never `std::isnan` — this TU is the one registered for `-fno-fast-math`, T003):
`0x7FC00000` (quiet NaN), `0x7F800000` (+Inf), `0xFF800000` (−Inf).

Assertions, on a `prepare()`d engine:
- `setGrainReverseProbability(1.0f)` then `setGrainReverseProbability(nan)` →
  `REQUIRE(engine.getGrainReverseProbability() == 0.0f)` — **substitution with the control default,
  not retention of the previous `1.0f`**. This is the component's own shipped setter shape
  (`atmosphere_engine.h:816`, `:829`, `:903`, `:983`) and differs by design from Phase 10's FR-069
  `MacroMatrix` rule.
- same for `+Inf` and `−Inf`.
- `setGrainReverseProbability(-1.0f)` → getter reports `0.0f`; `setGrainReverseProbability(2.0f)` →
  getter reports `1.0f` (clamp, with the getter reporting the clamp).
- default, before any write: `REQUIRE(engine.getGrainReverseProbability() == 0.0f)` (FR-002).

**Verify (red):** the TU does not compile — `setGrainReverseProbability` does not exist. That is the
intended red state; it turns green at T008.

---

### T007 [P] — SC-008 (c), SC-007 (c) seed-state arms, SC-006's compile-time half

**Edit** `dsp/tests/unit/systems/atmosphere_ghost_test.cpp` (delete its placeholder).

1. `TEST_CASE("AtmosphereGhost_RtSafety", "[atmosphere][ghost]")` — **clause (c) only for now**
   (clauses (a)(d)(e) arrive at T016/T014):
   - a default-constructed, **un-`prepare()`d** engine: `triggerGrain()` is a no-op that moves no
     counter — `REQUIRE(engine.getDroppedTriggerCount() == 0)` and
     `REQUIRE(engine.getTotalTriggeredGrainsBorn() == 0)` (FR-023's third clause).
2. `TEST_CASE("AtmosphereGhost_Determinism", "[atmosphere][ghost]")` — **clause (c) part 1 only**:
   - `getReverseRngState()` after `reset()` equals its value after `prepare()` at the same seed;
   - `setSeed(s2)` mid-render changes `getReverseRngState()`;
   - `getReverseRngState()` at seed `s` is **not** equal to `deriveStreamSeed(s, kGrainSalt)` and not
     equal to the raw seed — the stream is its own (`core/random.h:102-111`, which never yields 0).
3. `TEST_CASE("AtmosphereGhost_AppendOnly", "[atmosphere][ghost]")` — the **compile-time half** of
   SC-006 (the case shells nothing; clauses 1, 2, 5, 6 are discharged by T023 and the §4.4 runs):
   ```cpp
   static_assert(AtmosphereEngine::kReverseSalt > AtmosphereEngine::kDriftSaltBase
                                                  + AtmosphereEngine::kMaxGrains,
                 "salt ranges must not overlap");
   static_assert(AtmosphereEngine::kMaxGrains == 64);            // :189, unchanged
   static_assert(AtmosphereEngine::kMinAgeSamples == 64);        // :251, unchanged
   static_assert(AtmosphereEngine::kControlChunkSamples == 64);  // :271, unchanged
   ```
   plus `SUCCEED()` and an `INFO` listing the six clauses and where each is discharged.

**Verify (red):** the TU does not compile — `getDroppedTriggerCount`, `getTotalTriggeredGrainsBorn`,
`getReverseRngState`, `triggerGrain` and `kReverseSalt` do not exist yet.

---

## GROUP 3 — Implement S1 + S5

---

### T008 — `atmosphere_engine.h`: control surface, salt, seeding, counters, accessors (FR-001–FR-005, FR-009, FR-024, FR-026)

**Edit** `dsp/include/krate/dsp/systems/atmosphere_engine.h` only. **Include list (`:141-155`) gains
nothing** (FR-044). Every change here is an **insertion** — no deletion at this task except the two
seeding sites listed below (FR-045 anchor (iv)).

1. **Setter/getter**, inserted after `setDecorrelation`/`getDecorrelation` (`:902-905`):
   ```cpp
   void setGrainReverseProbability(float probability) noexcept {
       reverseProbability_ = std::clamp(isFinite(probability) ? probability : 0.0f, 0.0f, 1.0f);
   }
   [[nodiscard]] float getGrainReverseProbability() const noexcept { return reverseProbability_; }
   ```
   Routed through the component's own `ITERUM_NOINLINE static bool isFinite(float)` (`:1269`) —
   **never `std::isnan`** (FR-043).
2. **Salt**, beside the block at `:331-334`: `static constexpr std::size_t kReverseSalt = 0x5000;`
   and, beside the existing disjointness assert at `:360`:
   `static_assert(kReverseSalt > kDriftSaltBase + kMaxGrains, "salt ranges must not overlap");`
   (`kDriftSaltBase = 0x4000` `:334`, `kMaxGrains = 64` `:189`, so the bar is `0x4040`).
3. **Members** (private): `float reverseProbability_ = 0.0f;` and `Xorshift32 reverseRng_{1};` beside
   `decorrelation_` / `grainRng_`; `std::uint32_t pendingTriggers_ = 0;`,
   `std::uint64_t droppedTriggers_ = 0;`, `std::uint64_t totalTriggered_ = 0;`,
   `std::uint64_t totalReverseBorn_ = 0;`, `bool lastBirthReversed_ = false;` beside
   `lastBirthPanR_`. `std::uint64_t` matches `skipPoolFull_` / `totalBorn_`.
4. **Seeding (FR-005) — FR-045 anchor (iv), the only two real sites** (plan P-3: `prepare()` does not
   seed; it ends with `reset()` at `:527`):
   - `reset()` (verified `:552` scheduler, `:555` grain, `:556` blur) gains
     `reverseRng_.seed(deriveStreamSeed(seed_, kReverseSalt));`
   - `setSeed()` (verified `:1015-1017`) gains
     `reverseRng_.seed(deriveStreamSeed(seedValue, kReverseSalt));`
   No other method re-seeds it.
5. **Counter clearing (FR-024)**, appended to `reset()`'s counter block (`:627-638`):
   `pendingTriggers_`, `droppedTriggers_`, `totalTriggered_`, `totalReverseBorn_` → 0;
   `lastBirthReversed_ = false`. **`silence()` (`:653-658`) is NOT touched.**
6. **`AtmosphereGrain` field (FR-008)**, one line beside `bool active = false;` (`:1200`):
   `bool reversed = false;  ///< snapshot at birth (FR-008); never re-read from the control surface`
   (`grains_.fill(AtmosphereGrain{})` in `reset()` at `:539` clears it).
7. **Five accessors (S5)**, appended to the FR-072 block after `getGrainRngState()` (`:1118`), all
   `[[nodiscard]] … const noexcept`, all allocation-free: `getLastBornGrainReversed()`,
   `getTotalReverseGrainsBorn()`, `getReverseRngState()` (returns `reverseRng_.state()`),
   `getTotalTriggeredGrainsBorn()`, `getDroppedTriggerCount()`.
8. **`triggerGrain()` is NOT added here** — it lands with its consumption point at T017, so the
   header never carries an entry point nothing drains.
   For T007's compile, add the accessors only; T007's `triggerGrain()` sub-arm stays commented with a
   `// T017` marker and is uncommented there.

**ODR sweep before writing any of these names** (roadmap line 594), re-run now and pasted into the
compliance record:
```bash
grep -rn "setGrainReverseProbability\|getGrainReverseProbability\|getLastBornGrainReversed\|\
getTotalReverseGrainsBorn\|getReverseRngState\|getTotalTriggeredGrainsBorn\|getDroppedTriggerCount\|\
kReverseSalt" dsp/ plugins/ tools/
```
Expected: 0 hits for each (the spec's *New components* table records 0 at spec time).

**Verify:** zero warnings; T006 and T007 green; and the regression bar —
`dsp_systems_tests.exe "AtmosphereEngine_*"` and `"Seraphis*"` green with **no test edited**.

---

## GROUP 4 — Failing tests for the birth draw (SC-001 clauses 2–3, SC-007 (c) replica)

---

### T009 — SC-001 clauses 2 and 3 + FR-006's unconditional-draw replica

**Edit** `dsp/tests/unit/systems/atmosphere_ghost_test.cpp`.

1. `TEST_CASE("AtmosphereGhost_DefaultInert", "[atmosphere][ghost]")` — clauses 2 and 3 (clause 1
   arrives at T018, once T025's measured bounds exist):
   - **Clause 2, no-trigger arm (integer equality, never a float golden).** The 60 s fixture of T005.
     `REQUIRE(engine.getGrainRngState() == VoragoGhostFix::kBaseCommitGrainRngState)` at
     `setGrainReverseProbability(0.0f)` **and** at `1.0f`. This is the check that fails if the reverse
     bit were drawn from `grainRng_` — ADR-2's structural claim, which no tolerance can absorb.
     *Status now: green and trivially so (no draw exists yet). It is a **regression guard** whose
     falsification target is T010; it must be green before T010 and still green after.*
   - **Clause 3, counter identity.** `getTotalGrainsBorn()`, `getTotalGrainsRetired()`,
     `getSkippedTriggerCountPoolFull()`, `getSkippedTriggerCountRingCold()` (`:1053-1070`) and
     `getLatencySamples()` (`:1128`) equal their transcribed base-commit values at probability 0 with
     no trigger.
2. `TEST_CASE("AtmosphereGhost_Determinism", …)` gains **clause (c) part 2 — the replica arm, which
   is genuinely red now** (FR-006's "exactly one draw, unconditionally"):
   - fixture: T002's **full-ring pre-roll**, then a fixed further render;
   - prove attempts equal admitted births in the measured span:
     `REQUIRE(Δ getSkippedTriggerCountRingCold() == 0)` and
     `REQUIRE(Δ getSkippedTriggerCountPoolFull() == 0)`;
   - a test-held `Xorshift32 replica{deriveStreamSeed(seed, AtmosphereEngine::kReverseSalt)}`
     advanced **exactly one `nextUnipolar()` per `Δ getTotalGrainsBorn()`**, then
     `REQUIRE(replica.state() == engine.getReverseRngState())`;
   - run the identical fixed render at probability **`0.0f` and `1.0f`** — the same replica identity
     must hold at both. A guarded draw (`if (reverseProbability_ > 0.0f)`) fails the p = 0 arm; two
     draws per birth fail both.
   - retain the two weaker equalities (state equal across the two probability runs; state ≠
     `deriveStreamSeed(seed, kReverseSalt)`) — they cost nothing and localise a failure.

**Verify (red):** the replica arm fails — `getReverseRngState()` never advances, because no draw
exists. Clauses 2–3 pass and must keep passing.

---

## GROUP 5 — Implement the birth draw

---

### T010 — `tryBirthGrain()`: the fifth draw, on its own stream (FR-006, FR-007, FR-008, FR-009)

**Edit** `dsp/include/krate/dsp/systems/atmosphere_engine.h` only. Pure insertion — **no deletion**.

1. **The draw**, one inserted line immediately **after** the four `grainRng_` draws at `:1617-1620`
   (i.e. after the slot sweep's `skipPoolFull_` early-out at `:1601-1612`, before the liveness
   arithmetic):
   ```cpp
   // --- The FIFTH draw, on its OWN stream (FR-006). UNCONDITIONAL: a draw taken
   //     only when the probability is non-zero would make the stream position a
   //     function of the control value, and setGrainReverseProbability would stop
   //     being a pure gain on a fixed stream. Consumed even when the admission
   //     tests below then reject the birth - exactly as the four draws above are.
   const bool reversed = reverseRng_.nextUnipolar() < reverseProbability_;
   ```
   `nextUnipolar()` returns `[0, 1]` (`core/random.h:65-67`), so `< 0.0f` is never true at the default
   and `< 1.0f` is true except for the single exact value `1.0f` — document that at probability 1 the
   expected forward-grain rate is 2⁻³².
2. **The four `grainRng_` draws at `:1617-1620` are not moved, not reordered, not added to** (FR-007).
   The banner at `:1613-1616` states why: a fifth draw *there* re-shuffles every Seraphis render.
3. **Commit + introspection**, appended inside the existing commit block (`:1780-1806`):
   `grain.reversed = reversed;` (beside `grain.active = true` at `:1795`),
   `lastBirthReversed_ = reversed;` (beside `lastBirthPanR_` at `:1805`),
   `if (reversed) { ++totalReverseBorn_; }`.

**Verify:** zero warnings; T009's replica arm green at both probabilities; T009 clauses 2–3 **still**
green (the `grainRng_` state is bit-identical); `AtmosphereEngine_*` and `Seraphis*` green, unedited.

---

## GROUP 6 — Failing tests for the direction-dependent birth window and FR-050

Two tasks on **disjoint files**. **[P].**

---

### T011 [P] — SC-011, SC-004 and FR-049's short twin (`atmosphere_ghost_test.cpp`)

**Edit** `dsp/tests/unit/systems/atmosphere_ghost_test.cpp`.

**(A) `TEST_CASE("AtmosphereGhost_ReverseFillDeficit", "[atmosphere][ghost]")` — untagged, cheap. The
binding criterion for FR-050.** Every *other* reverse fixture pre-rolls to a full ring, which makes
FR-050's `t* = 0` and the clause a no-op, so a wrong-signed, wrong-termed or **absent** clause passes
all of them. This is the one case that renders the filling-ring regime.

Configuration = SC-003's Vorago ghost point (probability 1, `density = 0.30`, `grainSeconds = 12`,
`captureSeconds = 20`, `pitchSemitones = -12`, `positionSpread = 0.9`, `decorrelation = 0.85`,
48 kHz, seed 1) with **one declared deviation: `pitchSpread = 0` and `driftRangeSemitones = 0`**, so
`ratioMax == ratioMin == 2^(-12/12) = 0.5` exactly and every threshold is computable **by the test**
from public accessors. Driven in 64-sample blocks.
Derived quantities, **all recomputed in the test, never transcribed**:
`C = getCaptureCapacitySamples() = 1 048 576`, `L' = 576 000`,
deficit `= ceil(0.5 × 576 000) = 288 000`,
`kAdmitThresholdSamples = kMinAgeSamples + kMinAgeSamples + ceil(rMax × min(L', C − A)) = 288 128`
(6.003 s), branch crossover `C − L' = 472 576`.

- **(a) Rejection arm.** Pre-roll **exactly 240 000 samples** (5 s — *not* the full-ring pre-roll;
  stated as a sample count). Snapshot `born0`, `cold0`, `poolFull0`. Fire **one** `triggerGrain()`
  (uncomment at T017; until then drive the arm on the density scheduler alone and mark the trigger
  line `// T017`). Render `kRejectSpan = 40 000` samples in 64-sample blocks, ending at
  `A = 280 000` — 8 128 samples clear of the threshold. Guard first:
  `REQUIRE(240000 + kRejectSpan < kAdmitThresholdSamples)` so the arm cannot drift into the admitting
  regime if a constant moves. Then:
  - `REQUIRE(Δ getTotalGrainsBorn() == 0)` — the teeth;
  - `REQUIRE(Δ getSkippedTriggerCountPoolFull() == 0)`;
  - `REQUIRE(1 <= Δ getSkippedTriggerCountRingCold() && Δ … <= 2)` — the fired trigger was attempted
    and rejected, plus at most one scheduler tick (shortest interonset at `jitter = 0.5` is
    `160 000 × 0.75 = 120 000 > 40 000`). **Attempts are bounded, never predicted** —
    `samplesUntilNextGrain_` is a draw (`grain_scheduler.h:80-84`).
- **(a2) Admission arm.** Continue the same fixture to the end of the grain life — **816 000 samples
  (17 s) total** — reading counters at every 64-sample boundary. At the first boundary where
  `getTotalGrainsBorn()` advances, record `A_born = min(samplesRendered, getCaptureCapacitySamples())`
  and `birthAge = getLastBornGrainBirthAgeSamples()` (verified `:1106`), and
  ```
  REQUIRE(A_born >= std::ceil(birthAge) + kMinAgeSamples
                    + std::ceil(rMax * std::min(double(Lp), double(C) - A_born)));
  ```
  with `Lp = getLastBornGrainLifetimeSamples()` (`:1112`). Necessary-condition form: the drawn
  `decorrAge >= 0` is unobservable, so the decorr-free lower bound on `needed` is used.
- **(b) is DELETED and must not be written.** Admitted-implies-safe is a theorem (plan §A-5), so any
  arm that first REQUIREs a birth and then bounds the read age cannot fail. The protected quantity is
  measured only in T013's clause-disabled run.
- **(c) Forward arm.** Repeat (a)'s fixture at probability **0** and REQUIRE `getTotalGrainsBorn()`,
  `getTotalGrainsRetired()`, `getSkippedTriggerCountRingCold()`, `getSkippedTriggerCountPoolFull()`
  and `getGrainRngState()` equal their **transcribed base-commit short-pre-roll values** (T005).
  Integers, no tolerance. FR-050 is reverse-only and a forward render at a *filling* ring is exactly
  where a misplaced clause shows up; SC-001 clause 3 only pins the full-ring case.

**(B) `TEST_CASE("AtmosphereGhost_ReverseTruncation", "[atmosphere][ghost]")` — untagged, and NEVER
`[long]` at any measured cost** (FR-048's closing sentence + `CLAUDE.md` Build Commands: NaN/Inf-guard
and bounded-grid cases stay in the per-push lane). If it measures > 15 s, **split** it: this case keeps
the sentinel assertions on a handful of births and T012 takes an
`AtmosphereGhost_ReverseTruncation_Sweep` `[long]` sibling holding only the exhaustive sweep.

Configuration: `captureSeconds = 1` (`kMinCaptureSeconds`, `:317`) so
`getCaptureCapacitySamples() = nextPowerOf2(48 000) = 65 536` (1.365 s) — full-ring pre-rolled, so
FR-050 is vacuous; `grainSeconds = 30` (`kMaxGrainSeconds`, `:302`); probability 1;
`density = kMinDensity = 0.1` (`:303`); **`decorrelation = 0`** (added by the plan so `slack` is
exactly recomputable from public accessors — `decorrAge` is a per-grain draw with no getter).
- *exact arm*: `pitchSpread = 0`, `pitchSemitones = +24`, `driftRangeSemitones = 12` → the
  `kMaxAbsGrainSemitones = 36` clamp (`:311`, `:1622-1635`) → `rMax = 8` exactly → `w = 9`.
  `slack = C − 2 − 2×kMinAgeSamples − 2 = 65 404`, so
  `REQUIRE(getLastBornGrainLifetimeSamples() == std::floor(slack / 9.0))` = **7 267** samples
  (0.151 s) — with `slack` recomputed in the test from `getCaptureCapacitySamples()` and
  `kMinAgeSamples`, never the literal.
- *corner arm*: `pitchSpread = 1` (so `rMax` is draw-dependent in `[4, 8]`) →
  `REQUIRE(lifetime <= std::floor(slack / 5.0))`, the bound that holds for **every** admissible draw.
- both arms: **no birth with `lifetime < 2`** (`:1696-1699`); **no NaN and no Inf** in the render
  (T002's bit-pattern predicate — never `std::isnan`); **no `|sample| > kMaxLevel = 2.0`** (`:316`).
- **Observation granularity** (closes "every admitted grain"): drive in 64-sample blocks; read
  `getLastBornGrainLifetimeSamples()` after **every** block; drive births with **at most one**
  `triggerGrain()` per block (`// T017` until then); `REQUIRE(Δ getTotalGrainsBorn() <= 1)` per block
  — a block seeing two births **fails the case as a fixture defect**, it is not silently skipped; and
  at the end `REQUIRE(observations == Δ getTotalGrainsBorn())`.

**(C) `TEST_CASE("AtmosphereGhost_ReverseLiveness_Short", "[atmosphere][ghost]")` — FR-049's per-push
twin, untagged.** SC-003's configuration and the **same full-ring pre-roll** (T002's helper:
`getCaptureCapacitySamples() = 1 048 576` samples = 21.845 s at 48 kHz), then a **~30 s measured
span**, carrying **clause (a) only**: `REQUIRE(coldStartSkips <= 9)` at the end of the pre-roll and
`REQUIRE(getSkippedTriggerCountRingCold() == coldStartSkips)` across the span. `9 = 1 + floor(21.845 /
2.5)` — the free tick at sample 0 (`grain_scheduler.h:40`, `:73-76`) plus the ticks the shortest
interval `jitter = 0.5` permits at `density = 0.30` (`(1/0.30)×(1 − 0.5×0.5) = 2.5 s`,
`grain_scheduler.h:78-88`, interonset at `:102`).

**Verify (red):** (A)(a)/(a2) fail (no FR-050 clause), (B)'s exact arm fails (forward `w` is used),
(C) may pass now and must still pass at T013.

---

### T012 [P] — SC-003 `[long]` (`atmosphere_ghost_longrun_test.cpp`)

**Edit** `dsp/tests/unit/systems/atmosphere_ghost_longrun_test.cpp`.

`TEST_CASE("AtmosphereGhost_ReverseLiveness", "[atmosphere][ghost][long]")` — tagged `[long]` because
its measured runtime exceeds ~15 s **and** its assertions are toolchain-independent (FR-048).

Configuration: the Vorago ghost operating point in full — probability 1, `density = 0.30`
(`vorago_perf_test.cpp:620`), `grainSeconds = 12`, `captureSeconds = 20`, `pitchSemitones = -12`,
`positionSpread = 0.9`, `decorrelation = 0.85`, 48 kHz, seed 1.

- **(a)** full-ring pre-roll (T002's helper — `1 048 576` samples = 21.845 s, recorded as a measured
  figure), `coldStartSkips = getSkippedTriggerCountRingCold()`, `REQUIRE(coldStartSkips <= 9)`; then
  **10 minutes** of render and `REQUIRE(getSkippedTriggerCountRingCold() == coldStartSkips)` — the
  counter **does not advance at all**. That is the assertion with teeth: the birth window is non-empty
  at the Vorago operating point.
- **(b)** click freedom, **as a relative comparison** (the per-grain endpoint clause is deleted from
  SC-003 by §8 ruling 10 — `processStereoBlock` emits only the summed grain bus after the FR-028
  population gain (`:2171-2192`) and ~3.6 grains are concurrent, so no individual endpoint reaches the
  output; that assertion lives in SC-002, T014). Detector: `TestUtils::ClickDetector`
  (`tests/test_helpers/artifact_detection.h:99`) with `ClickDetectorConfig` (`:38`) — set
  `sampleRate = 48000.0f`, `frameSize`/`hopSize`/`detectionThreshold` **transcribed from
  `atmosphere_engine_test.cpp`'s own forward-liveness cell, quoted by file:line in the compliance
  record, never re-invented**; `detect(...)` returns `std::vector<ClickDetection>` (`:130`).
  Assertion: **no additional detections at probability 1 versus probability 0** over the same render —
  a *relative* bound, so a sigma mismatch cannot make it vacuously true.
- **(c)** `REQUIRE(getMinObservedGrainAgeSamples() >= kMinAgeSamples)` and
  `REQUIRE(getMaxObservedGrainAgeSamples() <= capacity - 2)` (`:1099-1103`) — FR-015.
  **Comment in the test that this is a bound on the COMPUTED age and is explicitly NOT FR-050's
  backstop** (plan P-1: the computed age stays inside `[64, C−2]` precisely while the read is clamped
  and stale). FR-050's binding criterion is T011 (A).

**Verify (red):** (a) fails — with forward `wUp`/`wDown` a reverse grain does not exist yet, so run it
at probability 1 and observe that the direction has no effect (the case is a no-op guard until T013,
where it becomes meaningful). Record its measured runtime.

---

## GROUP 7 — Implement the direction-dependent window and FR-050

---

### T013 — `tryBirthGrain()`: direction-dependent `wUp`/`wDown` + the reverse fill clause (FR-014–FR-016, FR-050)

**Edit** `dsp/include/krate/dsp/systems/atmosphere_engine.h` only.

1. **FR-045 anchor (i) — the one sanctioned deletion here**, at `:1651-1652`:
   ```cpp
   // wUp: the age SHRINKS at this rate. wDown: the age GROWS at this rate.
   // A REVERSE grain's read walks backwards while the write head walks forwards,
   // so its age can only GROW, at 1 + r per sample - it never catches up with the
   // write head and wUp is identically 0 (Phase 10a FR-014).
   const double wUp = reversed ? 0.0 : std::max(static_cast<double>(ratioMax) - 1.0, 0.0);
   const double wDown = reversed ? (1.0 + static_cast<double>(ratioMax))
                                 : std::max(1.0 - static_cast<double>(ratioMin), 0.0);
   ```
   **Everything downstream is unchanged in form and unchanged in code:** `w = wUp + wDown` (`:1658`),
   `headroom` (`:1676`), the `headroom <= 2.0` rejection (`:1677-1680`), `slack` (`:1691`),
   `lifetime = (w*requested > slack) ? floor(slack/w) : requested` (`:1692`), the `lifetime < 2.0`
   rejection (`:1696-1699`), `ageLo = ceil(wUp*lifetime) + guard` (`:1700`), `ageHi` (`:1704-1705`),
   the birth-age clamp (`:1708-1712`) and the FR-014 admission test (`:1736-1742`).
   Two consequences the tests rely on: `wUp = 0` ⇒ `ageLo = kMinAgeSamples = 64` for **every** reverse
   grain (FR-015 follows: every read is ≥ 64 samples old, the precondition `renderGrainChunk`'s banner
   states at `:1999-2004`, carried by the `static_assert` at `:345`); and window non-emptiness
   survives by the shipped one-step argument at `:1701-1703`, which uses only `w` and the truncation.
2. **FR-050, a pure insertion immediately after `:1742`** (deletes nothing):
   ```cpp
   // (e2) FR-050 (Phase 10a): a REVERSE grain's read age grows at 1 + r per sample
   //      while the ring fills at 1, so the shipped birth-sample test is only a
   //      statement about t = 0. The deficit accumulates at ratioMax until the ring
   //      saturates, after which step (c)'s window (wDown = 1 + ratioMax) already
   //      bounds the whole life. So the ONE extra quantity is the deficit up to
   //      saturation:  t* = min(lifetime, capacity - available).
   //      VACUOUS ON A FULL RING (t* == 0) and never evaluated for a forward grain,
   //      which is what keeps every shipped admission decision bit-identical.
   if (reversed) {
       const double avail = static_cast<double>(capture_.getAvailableSamples());
       const double fillDeficit =
           std::ceil(static_cast<double>(ratioMax) * std::min(lifetime, capacity - avail));
       if (avail < needed + fillDeficit) { ++skipRingCold_; return; }
   }
   ```
3. **Untouched (FR-016, FR-017):** the decorrelation offset and its `birthAge + decorr` use
   (`:1641-1642`, `:1736`), equal-power pan (`:1748-1751`), the drift-lane birth zeroing
   (`:1763-1765`), `refreshGrainRatio` (`:1571-1577`), the envelope phase increment (`:1793`), the
   active-list append and round-robin cursor (`:1795-1797`), both birth-age folds (`:1807-1808`).

**Verify — the FR-050 differential is a THREE-measurement protocol and all three results go in the
compliance table** (plan §6 step 4):
1. T011 (A)(a) **red with the FR-050 block commented out** (the trigger's birth is admitted at
   `A = 240 000`, since the shipped test needs only `needed <= 92 488`) and **green with it in**;
2. T011 (A)(a2) **green with the clause in** and **red with it out**;
3. with the clause **still commented out**, the **clause-disabled stale-read measurement** that
   replaces the deleted SC-011 (b): run the 5 s-pre-roll fixture to the end of the grain life and
   record that `getMaxObservedGrainAgeSamples()` exceeds
   `min(samplesRendered, getCaptureCapacitySamples()) - 2.0` at some block boundary. Predicted
   crossing: `t > 2 × (239 998 − birthAge − decorr)` grain-samples, i.e. ≈ 297 600 … 470 400 for the
   `[4 800, 91 200]` birth ages this fixture draws — all inside the 576 000-sample life. **This is the
   only way the protected quantity can be observed at all**; with the clause in, admitted-implies-safe
   is a theorem.

Then: zero warnings; T011 (A)(B)(C) and T012 green; `AtmosphereEngine_*` and `Seraphis*` green.

---

## GROUP 8 — Failing tests for the backwards read walk

---

### T014 — SC-002 (a)–(d) and SC-008 (e)

**Edit** `dsp/tests/unit/systems/atmosphere_ghost_test.cpp`.

`TEST_CASE("AtmosphereGhost_ReverseIsTimeReversed", "[atmosphere][ghost]")`

**Configuration, complete because every clause depends on it:** 48 kHz, `captureSeconds = 4`, seed 1,
`setGrainReverseProbability(1.0)`, `setPitchSemitones(0)`, `setPitchSpread(0)`,
`setDriftRangeSemitones(0)`, `setDriftDepth(0)` — so `ratio == 1.0` exactly for the whole life —
`setPositionSeconds(0)`, `setPositionSpread(0)` — so the birth age is the `kMinAgeSamples = 64` clamp
(`:251`), **known, not drawn** — `setDecorrelation(0)`, `setPanSpread(0)`, `blurEnabled = false`,
`freezeEnabled = false`, `setLevel(1.0)`, `setJitter(0)`, `setDensity(kMinDensity = 0.1)` (`:303`),
`grainSeconds = 0.5`.
**Excitation:** a linear chirp **200 Hz → 4 kHz over exactly 1.0 s** (sweep rate **3 800 Hz/s**) whose
midpoint coincides with the trigger sample `T`, i.e. covering source `[T − 0.5 s, T + 0.5 s]`; silence
outside (T002's `exciteChirp`).

**Isolation is ASSERTED, never assumed:**
1. render the pre-roll;
2. **wait until `getActiveGrainCount() == 0`** before triggering — the scheduler births on the first
   render sample because `samplesUntilNextGrain_` starts at `0.0f` (`grain_scheduler.h:40`, `:73-76`);
3. snapshot `getTotalGrainsBorn()`, call `triggerGrain()`, render the grain's life in 64-sample blocks;
4. `REQUIRE(getActiveGrainCount() == 1)` at **every** block boundary of the measured span and
   `Δ getTotalGrainsBorn() == 1` across it; take the span from `getLastBornGrainLifetimeSamples()`
   (`:1112-1114`). **A colliding scheduler birth fails the case loudly** — a fixture defect to fix,
   never a reason to widen (a) or (b).
5. Immediately after the Δ check, pin the per-birth draw:
   `REQUIRE(engine.getLastBornGrainReversed())` in the probability-1 arm and `REQUIRE_FALSE(...)` in
   the probability-0 comparison arm (FR-009, FR-006 — otherwise the accessor is read by no criterion).

Clauses:
- **(a)** normalised cross-correlation peak **≥ 0.90** against the Hann-windowed **time-reversed**
  reference segment (source `[T − 0.5 s, T]`, reversed, windowed with the same Hann the grain envelope
  applies), and **≤ 0.30** against the identically windowed **forward** segment (`[T, T + 0.5 s]`).
  Plus the endpoint guarantee relocated here from SC-003 (§8 ruling 10):
  `REQUIRE(span.front() == 0.0f && span.back() == 0.0f)` — exactly and bit-wise, the envelope endpoint
  guarantee at banner `:96-101`, observable because this is the one-grain protocol.
- **(b)** STFT spectral-centroid trajectory over the grain's life: least-squares slope **negative** for
  the reverse grain and **positive** for the same grain born at probability 0, with
  `|slope| >= 500 Hz/s` in both. Expected ∓3 800 Hz/s, i.e. a **7.6× margin** — this is visibly a
  floor. Frames via `TestUtils::Vorago::frameMagnitudes`
  (`tests/test_helpers/vorago_fixtures.h:137`, verified) and `TestUtils::spectralCentroidHz`
  (`tests/test_helpers/reverb_metrics.h:291`, re-exported at `vorago_fixtures.h:86`, verified) —
  **reused, not re-implemented**. (b) exists because (a) alone is satisfiable by a symmetric artefact.
- **(c) FRACTIONAL-RATIO ARM — the clause that makes FR-012 falsifiable.** (a) and (b) both run at
  `ratio == 1.0`, where the borrow's ceil-correction branch never fires, so an off-by-one borrow
  renders deterministically, NaN-free, bounded and partition-invariantly — passing every other
  criterion while the read position is wrong at **every production ratio**.
  Same fixture, one change: `setPitchSemitones(-5)` (spread and drift still 0), so every grain has the
  same known `ratio = 2^(-5/12) = 0.749154…`. Exact identity on the read walk:
  ```cpp
  REQUIRE(getMaxObservedGrainAgeSamples() - getMinObservedGrainAgeSamples()
          == Approx(double(1.0 + ratio) * double(Lp - 1)).margin(1.0));
  ```
  with `Lp = getLastBornGrainLifetimeSamples()`; folds read through `:1099-1103`. Clean despite the
  folds being engine-lifetime: `positionSeconds = positionSpread = 0` and `wUp = 0` put every grain's
  birth age at the 64-sample clamp, and `pitchSpread = 0`, `decorrelation = 0` make every grain
  identical in ratio and lifetime (no truncation: `w·requested = 41 981` against `slack ≈ 262 000` at
  `captureSeconds = 4`). Secondary, from clause (b)'s frames: the centroid slope at this ratio is
  `−(ratio × 3800) ≈ −2 847 Hz/s`, REQUIREd negative with `|slope| >= 500 Hz/s`.
- **(d) FR-008 mid-life snapshot, asserted where it is observable.** Birth one grain at probability 1,
  call `setGrainReverseProbability(0.0f)` at the span's **midpoint**, and REQUIRE clause (a)'s
  `>= 0.90` reversed-reference cross-correlation over the **whole** grain span, and clause (b)'s slope
  negative **across the change point**, not just before it. A per-sample re-read of
  `reverseProbability_` halves the correlation and flips the second half's slope.

`TEST_CASE("AtmosphereGhost_RtSafety", …)` gains **clause (e) — partition invariance**: the same
reverse render driven in blocks of **512, 64, 37 and 1** sample compares within the **default**
`render_fingerprint.h` tolerances (`kMetricTolerance = 2.5e-4`, `kSampleTolerance = 5.0e-4f`,
`:58-61`, verified) — a same-binary comparison, so the shared constants are the right bounds. This is
the property `:2002-2012` and `rolling_capture_buffer.h:243-255` exist to protect, now on the
backwards path.

**Verify (red):** every clause fails — there is no backwards walk yet.

---

## GROUP 9 — Implement the backwards walk

---

### T015 — `renderGrainSpan()`: the borrow advance and the direction hoist (FR-010–FR-013, anchor (vi) part 2)

**Edit** `dsp/include/krate/dsp/systems/atmosphere_engine.h` only. **No new buffer, no new allocation,
no new include, no `ReverseBuffer`** (ADR-1 / FR-010). `accumulateGrainSpanSIMD`
(`processors/grain_span_simd.h`, called at `:1950-1966`) is **not touched**.

1. **FR-045 anchor (ii), amended by §8 ruling 5** — the sanctioned deletions are the `advance` lambda
   at `:1876-1882` (verified `:1876`) **together with the two loops in `renderGrainSpan` that call
   it**: the cold path at `:1888-1891` and the scalar index-generation loop at `:1917-1930`. Those
   three ranges and no other.
2. **The backwards decomposition** — no `std::floor`, no CRT call (the codegen rule the lambda's own
   banner states at `:1871-1875` and `rolling_capture_buffer.h:302-318` measures):
   ```cpp
   const auto advanceBack = [&]() noexcept {
       readFrac -= ratio;                                               // in (-8, 1)
       auto borrow = static_cast<std::int32_t>(-readFrac);              // trunc toward 0
       if (readFrac + static_cast<float>(borrow) < 0.0f) { ++borrow; }  // ceil correction
       readIndexInt -= static_cast<std::uint64_t>(borrow);
       readFrac += static_cast<float>(borrow);
       ++age;
   };
   ```
   Exact for every legal `ratio ∈ [0.125, 8]` (the range the birth-time `kMaxAbsGrainSemitones = 36`
   clamp guarantees, `:1622-1635`, `:311`). **The invariant is `readFrac ∈ [0, 1]`, not `[0, 1)`:** for
   `readFrac' ∈ [-2.98e-8, 0)` the correction bumps `borrow` to 1 and `readFrac' + 1.0f` rounds to
   exactly `1.0f`. That is harmless — `readFrac` is consumed in exactly one place in the span renderer,
   the `ageAt` subtraction at `:1854-1857`, and never as an interpolation weight (`fracL`/`fracR` come
   from `reader.indexAt(ageNow, …)` at `:1922-1928`, which derives its own fraction inside
   `LinearReader::index0` from the *clamped* age, `rolling_capture_buffer.h:313-321`).
   `readIndexInt` is `std::uint64_t` and **may wrap below 0**; harmless, because the age is a
   modulo-2⁶⁴ subtraction cast to `std::int64_t` (`:1852-1857`), exact for the true difference (< 2²²).
3. **Anchor (vi) part 2 — correct the falsified field comment** at `:1183` (verified: *"absolute source
   index, fraction in [0,1)"*) to *"absolute source index, fraction in [0,1] — a reverse grain's borrow
   can land on exactly 1.0f; see the advanceBack table"*. This is **§8 ruling 9, ruled option (a) on
   2026-09-23** (spec FR-012 amended, Clarifications R-9); option (b)'s one-compare clamp
   `if (readFrac >= 1.0f) { readFrac = 0.0f; --readIndexInt; }` is **not** added.
4. **Direction resolved ONCE per span (FR-013); zero `reversed` tokens inside the loops** (the token
   rule SC-006 clause 6 counts). Wrap the cold path and the scalar pass in a **C++20 templated
   lambda** instantiated twice, so the `kBackwards == false` instantiation is the shipped code
   instruction for instruction:
   ```cpp
   const auto advanceBy = [&]<bool kBackwards>() noexcept { /* shipped five lines | borrow form */ };
   const auto runSpan  = [&]<bool kBackwards>() noexcept { /* shipped loops, advance() ->
                                                              advanceBy.template operator()<kBackwards>() */ };
   if (grain.reversed) { runSpan.template operator()<true>(); }
   else                { runSpan.template operator()<false>(); }
   ```
   `grain.reversed` must be the **only** occurrence of the token in the function, sitting above both
   loops. **Portability (R-5):** templated lambdas and `.template operator()<…>()` are C++20 and
   accepted by MSVC 19.29+, GCC 10+, Clang 12+, but unusual in this repo — the WSL/GCC probe in T027
   is **mandatory, not optional**. Documented fallback if any leg objects: promote `runSpan` to a
   private member function template `template <bool kBackwards> void renderGrainSpanDirected(...)`;
   same two instantiations, same gate evidence.
5. **Unchanged in the span renderer:** `ageAt` (`:1852-1857`), `foldAt` (`:1859-1869`), the envelope
   phase multiplication and endpoint conditioning (`:1936-1948`), the six gathers / three lerps of the
   SIMD pass (`:1950-1966`), the single state store-back (`:1968-1970`).

**Verify:** zero warnings; T014's SC-002 (a)(b)(c)(d) and SC-008 (e) green; T011, T012 still green;
`AtmosphereEngine_*` and `Seraphis*` green, unedited.

---

## GROUP 10 — Failing tests for the trigger path and the pass-A scratch bound

---

### T016 — SC-005 (all arms), SC-012, SC-008 (a) and (d)

**Edit** `dsp/tests/unit/systems/atmosphere_ghost_test.cpp`. Also uncomment every `// T017`
`triggerGrain()` line left in T011 and T014.

**(A) `TEST_CASE("AtmosphereGhost_TriggerAccounting", "[atmosphere][ghost]")`** — all arms at
`density = kMinDensity = 0.1` (`:303`) so the scheduler contributes at most one grain per window.

- **(a)** N ∈ {1, 5, 64} calls on a **warm ring** (full-ring pre-roll), then a render of ≥ N samples.
  Pin `setGrainSeconds(kMinGrainSeconds)` (0.05 s, `:301`) for this arm and, immediately before firing,
  `REQUIRE(getActiveGrainCount() == 0)` after a short idle render (at 0.05 s a pre-roll grain retires
  within 2 400 samples). Then, in the `k0`-aware form:
  `Δ getTotalTriggeredGrainsBorn() == N − poolFullDelta` with
  `Δ getSkippedTriggerCountPoolFull() == poolFullDelta`, and `REQUIRE(poolFullDelta == 0)` for every N
  given the precondition. Plus `N <= Δ getTotalGrainsBorn() <= N + 1` (the one possible scheduler
  birth — `getTotalGrainsBorn()` is bounded, **not** equated).
- **(b) cold arm** (no prior render): `kMaxGrains + 10 = 74` calls **before any render**, then a render
  of **exactly 128 samples** (≥ 64, so all 64 pending are consumed at one per sample):
  `REQUIRE(getDroppedTriggerCount() == 10)`; `REQUIRE(getTotalGrainsBorn() == 0)`;
  `REQUIRE(getSkippedTriggerCountRingCold() == 64 + schedulerTicks)` with `schedulerTicks` computed by
  T002's helper from the rendered length (**= 1** at `kMinDensity` for any render under 10 s, so the
  concrete value is **65** — the bare `== 64` is wrong on correct code because the scheduler's free
  first tick also fails admission).
  Then the **rate assertion**, the only purchase in the phase on FR-020's "at most one per sample, in
  sample order": snapshot, render **exactly 1 sample**, `REQUIRE(Δ == 2)` (the scheduler's free tick
  plus **exactly one** consumed trigger, both rejected ring-cold at `:1736-1741`); render **1 more
  sample**, `REQUIRE(Δ == 1)`; over the remaining 126 samples the delta is **62**, post-first-sample
  total **63**. Without this, an implementation that drains the whole queue on one sample passes every
  other criterion **and invalidates FR-051's derivation**.
  This arm tests **pending-queue saturation and the cold-ring path**; it does not reach the pool cap.
- **(b) warm arm** (the pool bound, roadmap line 509; run at **probability 0** so all 64 births are
  admitted by the shipped test — the reverse warm case is T012's): full-ring pre-roll, record
  `k0 = getActiveGrainCount()` and the three counters, fire 74 calls, render 64 samples:
  `Δ getTotalGrainsBorn() == kMaxGrains − k0`; `Δ getSkippedTriggerCountPoolFull() == k0`;
  `Δ getDroppedTriggerCount() == 10`; `getActiveGrainCount() == kMaxGrains`. Then **10 further** calls
  and a render: `Δ getTotalGrainsBorn() == 0`; `Δ getSkippedTriggerCountPoolFull() == 10`;
  `getActiveGrainCount()` still `kMaxGrains` — the cap holding with **no grain stolen** (`:1609-1612`).
- **(c)** calls into a **cold ring** are consumed exactly once: `getTotalGrainsBorn()` unmoved,
  `getSkippedTriggerCountRingCold()` advances by the number consumed, and a later **warm** render shows
  **no deferred burst** (FR-022).
- **(d) three sub-arms**:
  1. **Latched no-op (FR-023).** After `silence()` has latched (`getActiveGrainCount() == 0`, output
     exactly `0.0f`), `triggerGrain()` produces no grain and moves no counter.
  2. **`reset()` clears the queue — asserted on CONSUMPTION, not on births** (a births assertion is
     vacuous: `reset()` also empties the ring). Two halves:
     - *cold-ring, consumption-counted*: on a non-latched cold-ring engine fire 74 calls,
       `REQUIRE(getDroppedTriggerCount() == 10)` (so the queue is provably at 64), `reset()`, render
       **≥ 64 samples**, then `REQUIRE(getSkippedTriggerCountRingCold() == schedulerTicks)` — **not a
       Δ**, because `reset()` zeroes `skipRingCold_` at `:628`. An uncleared 64-deep queue gives
       `64 + schedulerTicks`. Plus `getTotalGrainsBorn() == 0` and `getDroppedTriggerCount() == 0`.
     - *warm-ring, counters provably advanced*: full-ring pre-roll at probability 1 and
       `kMinGrainSeconds`, fire and consume several triggers, `REQUIRE(getTotalTriggeredGrainsBorn() >
       0)` and `REQUIRE(getTotalReverseGrainsBorn() > 0)` **before** the reset; then `reset()` and
       `REQUIRE` all three of `getTotalTriggeredGrainsBorn()`, `getTotalReverseGrainsBorn()`,
       `getDroppedTriggerCount()` are **0**. Without this an implementation that forgot
       `totalReverseBorn_ = 0;` or `totalTriggered_ = 0;` passes every other criterion.
  3. **`Silencing` ACCEPTS triggers (FR-023 clause 3)** — the clause an over-broad
     `runState_ != Running` guard silently breaks. Warm ring, grains alive, `silence()`, then **while
     `getActiveGrainCount() > 0`** (still `Silencing`, not yet `Latched`) call `triggerGrain()` and
     REQUIRE `getDroppedTriggerCount()` unmoved **and** `Δ getTotalTriggeredGrainsBorn() >= 1` over the
     remaining Silencing render. **The birth, not the absence of a drop, is the assertion with teeth.**

**(B) `TEST_CASE("AtmosphereGhost_PassAScratchBound", "[atmosphere][ghost]")` — untagged, the binding
criterion for FR-051.** `AllocationScope` **cannot** see this failure (writing past a `std::vector`'s
size allocates nothing), so the portable assertion is paired with T027's ASan run.

Fixture, exactly plan P-5's corrected reaching configuration: `sampleRate = 20`,
`density = kMaxDensity = 20.0f` (`:304`) so the interonset is exactly `1.0` and the scheduler fires on
**every** sample (`grain_scheduler.h:74-76`), `captureSeconds = 30` so
`getCaptureCapacitySamples() = nextPowerOf2(600) = 1024`, blur and freeze off, reverse probability 0
(direction is irrelevant to the sizing), **ring pre-rolled full (1 024 samples)**, and `kMaxGrains`
`triggerGrain()` calls issued before each block so the FR-019 queue is at its cap at every chunk
boundary.
- **chunk 1** at `setGrainSeconds(3.2f)` (`L = round(3.2 × 20) = 64`): render **exactly 64 samples**;
  `REQUIRE(getActiveGrainCount() == kMaxGrains)`; `REQUIRE(Δ getTotalGrainsRetired() <= 1)` (only a
  birth at `i = 0` can be due in this chunk).
- **chunk 2** at `setGrainSeconds(0.1f)` (`L = 2`; `kMinGrainSeconds × 20 = 1` is rejected by the
  `lifetime < 2` test at `:1696-1699`): render **exactly 64 more samples**, then
  ```cpp
  REQUIRE(deltaRetired > AtmosphereEngine::kMaxGrains * 2);   // the teeth: > 128
  REQUIRE(deltaRetired <= AtmosphereEngine::kMaxGrains * 3);  // FR-051's own bound: <= 192
  ```
  Expected ≈ **186**; the REQUIRE is the structural `> 128`, not the measured figure. Every retirement
  inside a chunk is one `retiredScratch_` write and one consumed `dueScratch_` entry, and `dueCount`
  never decreases within a chunk (`:2041-2053`, `:2119-2134`, drain at `:2136-2141`), so a delta above
  128 **is** a write past the old sizing.
- Wrap both renders in `[[maybe_unused]] const TestHelpers::AllocationScope scope;` with a comment
  saying what that **can** prove here (no allocation moved onto the audio thread) and what it
  **cannot** (the overflow).

**(C) `TEST_CASE("AtmosphereGhost_RtSafety", …)` gains clauses (a) and (d)**:
- **(a)** `#include <allocation_detector.h>` **only** — never `<allocation_operator_overrides.h>`: the
  single owner of the global `operator new`/`delete` replacements in `dsp_systems_tests` is
  `unit/systems/selectable_oscillator_test.cpp:388` (`tests/test_helpers/vorago_fixtures.h:27-32`), and
  a second include anywhere in the image is a duplicate-symbol link error.
  `[[maybe_unused]] const TestHelpers::AllocationScope scope;` around every render, at probability
  0 / 0.5 / 1, with and without triggers, read **inside** the open scope
  (`vorago_engine_test.cpp:1705-1710`'s idiom).
- **(d)** `prepare()` at **44 100 / 48 000 / 96 000 / 192 000 Hz**: `getReverseRngState()` equal across
  rates at the same seed; a reverse render at each rate NaN/Inf-free (bit-pattern predicate) and
  bounded by `kMaxLevel = 2.0` (`:316`). **Plus FR-024's `prepare()` half, on consumption evidence**:
  on a non-latched cold-ring engine fire 74 calls, `REQUIRE(getDroppedTriggerCount() == 10)`, call
  `prepare()` at the next rate, render ≥ 64 samples, `REQUIRE(getSkippedTriggerCountRingCold() ==
  schedulerTicks)` (an uncleared queue gives `64 + schedulerTicks`; `prepare()` ends with `reset()` at
  `:527`, which zeroes the counter at `:628`), `getTotalGrainsBorn() == 0`,
  `getDroppedTriggerCount() == 0`.

**Verify (red):** the TU does not compile — `triggerGrain()` does not exist; and SC-012's chunk-2
assertion is red against the shipped `kMaxGrains * 2` sizing.

---

## GROUP 11 — Implement the trigger path and the scratch sizing

---

### T017 — `triggerGrain()`, pass-A consumption, and FR-051's sizing **in one step**

**Edit** `dsp/include/krate/dsp/systems/atmosphere_engine.h` only. The sizing must land **with**, not
after, the second birth site.

1. **Public API**, inserted after `setGrainEnvelope`/`getGrainEnvelope` (`:995-1000`):
   ```cpp
   void triggerGrain() noexcept {
       if (!prepared_ || runState_ == RunState::Latched) { return; }
       if (pendingTriggers_ >= static_cast<std::uint32_t>(kMaxGrains)) { ++droppedTriggers_; return; }
       ++pendingTriggers_;
   }
   ```
   Doxygen must state, in this order: RT-safe, allocation-free, lock-free; the request is a
   **saturating counter bounded by `kMaxGrains`** and a call at the cap is **dropped, never queued**
   (FR-019 — the "skip, never steal" philosophy of `skipPoolFull_`, `:1609-1612`, banner `:60-63`); a
   no-op before `prepare()` and while `Latched` (FR-023 — a latched engine returns from
   `processStereoBlock` before pass A, `:691-696`, and would never drain the queue), **accepted while
   `Silencing`**; and **THREADING — AUDIO THREAD ONLY (§8 ruling 6)**: unlike every other mutator on
   this component, which is a pure store of a value the audio thread only reads (`:816`, `:829`,
   `:903`, `:983`), this is a **read-modify-write of `pendingTriggers_`/`droppedTriggers_`, counters
   pass A also read-modify-writes** — an off-thread caller is a data race that breaks FR-022's
   "consumed exactly once". The banner's block-rate automation contract at `:28-35` **does not extend
   to it**. Add the matching note to that banner. No atomic is introduced **because no caller is
   off-thread**; a control-thread caller would need `std::atomic<std::uint32_t>` with a CAS saturation
   loop, not this.
2. **FR-045 anchor (iii) — the sanctioned deletion at `:2115`.** Extract the shipped
   `if (scheduler_.process()) { … }` body into a lambda beside `bookkeepingRetire` (`:2063-2081`), so
   the scheduler and the trigger path share **one** implementation:
   ```cpp
   const auto birthAndTrack = [&](std::size_t i) noexcept -> bool {
       const std::size_t before = activeCount_;
       tryBirthGrain();
       if (activeCount_ <= before) { return false; }
       const std::size_t slot = activeIdx_[activeCount_ - 1];
       bornAt[slot] = static_cast<std::uint32_t>(i) + 1u;
       const auto lifetime = static_cast<std::size_t>(grains_[slot].lifetime);
       if (i + lifetime <= numSamples) { /* the shipped sorted insert, :2124-2133 */ }
       return true;
   };
   // FR-025: ONE test per chunk, not one per sample.
   const bool anyPending = pendingTriggers_ > 0u;
   ```
   and the per-sample body becomes:
   ```cpp
   // Scheduling (FR-021), then FR-020's trigger. THE ORDER IS FIXED AND
   // DOCUMENTED: scheduler first, trigger second, at most one per sample, in
   // sample order.
   if (scheduler_.process()) { static_cast<void>(birthAndTrack(i)); }
   if (anyPending && pendingTriggers_ > 0u) {
       --pendingTriggers_;                    // FR-022: consumed EXACTLY ONCE,
       if (birthAndTrack(i)) { ++totalTriggered_; }   //        whether or not it births
   }
   ```
   **`pendingTriggers_` must appear in the per-sample body exactly once, as the right operand of the
   short-circuited `&&` whose left operand is the loop-invariant `anyPending` declared above the
   `for`** — that is the token rule SC-006 clause 6 counts (§8 ruling 7).
3. **FR-051 — FR-045 anchor (v)**, `prepare()` step 5b (verified `:439-441`):
   `retiredScratch_.assign(kMaxGrains * 3, RetiredGrainSpan{});` and
   `dueScratch_.assign(kMaxGrains * 3, DueEntry{});`, with the derivation comment restated: pass A now
   has **two** birth sites per sample, a newborn only inserts a due entry from `i <= numSamples - 2`
   (`lifetime >= 2`, `:1696-1699`), i.e. 63 positions × 2 births = 126, plus ≤ `kMaxGrains = 64`
   already active at the chunk start = **190 ≤ 3 × kMaxGrains = 192**; `retiredScratch_` is bounded by
   the same count. Both vectors are written by **unchecked index** on the audio thread
   (`retiredScratch_[retiredCount]` `:2064-2066`, `dueScratch_[k]` `:2128-2134`) — this is a
   correctness fix, not headroom.
4. **Anchor (vi) part 1** — correct the now-false declaration comment at `:2595-2596` (*"Sized once in
   prepare() (2 \* kMaxGrains each); indexed by count, never pushed on the audio thread."*) to name
   `3 * kMaxGrains` and the two birth sites, pointing at the assign site for the derivation.

**Verify:** zero warnings; T016 (A)(B)(C) green — including SC-012's portable half; T009, T011, T012,
T014 still green; `AtmosphereEngine_*` and `Seraphis*` green, unedited.
**The ASan half of SC-012 must be recorded here, both ways, before this task is called done**
(commands in T027): **red (heap-buffer-overflow) at `kMaxGrains * 2`** and **green at `kMaxGrains *
3`**. Both outcomes go in the compliance table.

---

## GROUP 12 — Determinism, the stored-fingerprint clause, and the long-run sweep

Two tasks on **disjoint files**. **[P].**

---

### T018 [P] — SC-001 clause 1 and SC-007 (a)(b) (`atmosphere_ghost_test.cpp`)

**Edit** `dsp/tests/unit/systems/atmosphere_ghost_test.cpp`.

1. `AtmosphereGhost_DefaultInert` gains **clause 1 — render identity under FR-046's measured-bounds
   protocol**: the 60 s fixture of T005, `fingerprintRender(std::span<const float>)`
   (`render_fingerprint.h:73`) over the left channel, compared by
   `compareFingerprints(actual, kBaseCommitFingerprint, kMeasuredMetricTolerance,
   kMeasuredSampleTolerance)` (`:122-124`, verified), verdict `withinTolerance()` (`:108-110` — there
   is **no `passes()`**).
   Reproduce the `noise_organism_test.cpp:3288-3406` discipline exactly (verified: it defines
   `kMeasuredMetricTolerance` at `:3288` and neighbours):
   - two `constexpr` per-comparison bounds, each with a `static_assert` that it is **looser** than the
     shared `kMetricTolerance = 2.5e-4` / `kSampleTolerance = 5.0e-4f` **and** `static_assert`s that
     the shared constants still hold their shipped values (so a "fix" that edits
     `render_fingerprint.h` breaks the build rather than passing);
   - a **paste-ready literal printed on comparison failure**;
   - a **PROVENANCE block** beside the stored fingerprint (base commit
     `374580d7d0f0631561413310bd3085e15ba7279c`, machine, compiler, date).
   The bound **values** come from T025's three-toolchain probe; until then use a placeholder and leave
   the case `[!mayfail]`-free but skipped by a `// T025` marker comment — it must be un-skipped and
   green before the phase closes. **No bound may be widened after it is recorded** (FR-046).
2. `AtmosphereGhost_Determinism` gains:
   - **(a)** two engines at the same seed, configuration and trigger schedule:
     `compareFingerprints(...).withinTolerance()` at the **default** tolerances, at probability 0, 0.5
     and 1 (same-binary comparison — the shared constants are the right bounds; the measured-bounds
     protocol is only for *stored* references);
   - **(b)** two engines at **different** seeds at probability 0.5:
     `REQUIRE(worstMetricRelativeError > 100 * kMetricTolerance)` (the Phase-10 SC-026 separation
     form).
   - retain the weak consistency check that a render with no birth does not move
     `getTotalReverseGrainsBorn()` — **never** as FR-008's evidence (that is T014 (d)).

**Verify:** (a)(b) green; clause 1 green once T025 lands its bounds.

---

### T019 [P] — SC-007 (d) `[long]` (`atmosphere_ghost_longrun_test.cpp`)

**Edit** `dsp/tests/unit/systems/atmosphere_ghost_longrun_test.cpp`.

`TEST_CASE("AtmosphereGhost_Determinism_ReverseFraction", "[atmosphere][ghost][long]")` — tagged
`[long]` per FR-048 (a 100 000-grain sweep, toolchain-independent assertion).

Accelerated run (`kMinGrainSeconds` grains at high density on a small **warm** ring) of **100 000
grains** at `p ∈ {0.25, 0.5, 0.75}`:
`REQUIRE(std::abs(double(getTotalReverseGrainsBorn()) / double(getTotalGrainsBorn()) - p) <= 0.02)`.
A 3σ band for that sample count is ±0.005, so this is a **calibration check, not a tight RNG test** —
say so in the case comment so nobody later tightens it into a flake.

If SC-004 measures > 15 s at T011, this TU also takes
`AtmosphereGhost_ReverseTruncation_Sweep` `[long]` holding **only** the exhaustive per-block truncation
sweep — the NaN/Inf, `kMaxLevel` and `lifetime >= 2` sentinels stay in T011's untagged case.

**Verify:** green; record the measured runtime of every case in this TU.

---

## GROUP 13 — Failing tests for the Vorago wiring

---

### T020 — SC-010 (a)–(e) (`vorago_ghost_ext_test.cpp`)

**Edit** `dsp/tests/unit/systems/vorago_ghost_ext_test.cpp`.

The accessor is **`engine.atmosphere()`** — `[[nodiscard]] const AtmosphereEngine& atmosphere() const
noexcept` (`vorago_engine.h:1066`, verified). There is **no `VoragoEngine::atmos()`**; `atmos_` is the
private member at `:1498`.

`TEST_CASE("VoragoEngine_GhostExtensionWiring", "[vorago][ghost]")` — clauses (a), (c), (e).
`TEST_CASE("VoragoEngine_GhostExtensionWiring_Engaged", "[vorago][ghost][long]")` — clauses (b), (d),
per decision D-4 (Phase 10's own 600 s ghost case is untagged at `vorago_engine_test.cpp:2759`, but
this arm actually renders grains). **If (b) measures under 15 s, fold it back into the untagged case;
record the measured runtime either way.**

- **(a)** `VoragoEngineConfig` at its **defaults**, 60 s Vorago render via
  `TestUtils::Vorago::makeEngine` (`vorago_fixtures.h:734`) / `renderEngine` (`:750`), compared against
  the transcribed `kBaseCommitVoragoFingerprint` (T005) under the **same FR-046 measured-bounds
  protocol** as T018 clause 1; plus
  `REQUIRE(engine.atmosphere().getTotalTriggeredGrainsBorn() == 0)` and
  `REQUIRE(engine.atmosphere().getGrainReverseProbability() == 0.0f)`.
- **(b)** Phase 10's `makeGhostEngine()` shape (`vorago_engine_test.cpp:2730-2756`): **8 kHz**,
  polyphony 1, seed `0x6057`, `setEventRateScale(10.0f)` (the `[0.1, 10]` ceiling,
  `vorago_voice.h:1353-1358`), **`setEcosystemDepth(0.0f)`** (the write without which the ecosystem
  term `ecosystemDepth_[kGhost] * output[i]`, `vorago_voice.h:1734`, keeps the request continuously
  non-zero), `atmosGhostEventTriggers = true`, and — **unlike Phase 10's fixture** —
  `atmosCaptureSeconds` left at the shipped `20.0f` (`vorago_engine.h:120`, verified; Phase 10 used
  `1.0f` at `:2742`) so grains can actually be admitted.
  **Full-ring pre-roll per T002**: at 8 kHz / `captureSeconds = 20` that is
  `getCaptureCapacitySamples() = nextPowerOf2(160 000) = 262 144` samples = **32.768 s** (the "25 s"
  of earlier drafts was short by 7.8 s; the ring is power-of-two rounded,
  `rolling_capture_buffer.h:75-93`). Then snapshot every counter and render **600 s in 64-sample
  blocks**, so the observation grid equals the control grid and no edge is missed by aliasing.
  Edges counted with SC-027's two-state detector (`vorago_engine_test.cpp:2699-2704`) over
  `engine.atmosphere().getLevel()` at `kGhostRiseThreshold = 0.5 × kGhostBurstPeak` /
  `kGhostFallThreshold = 0.05 × kGhostBurstPeak` (`:2666-2667`; `kGhostBurstPeak = 0.60f` verified at
  `vorago_engine.h:206`, so 0.30 and 0.03).
  Assertions:
  - `REQUIRE(edges <= Δ getTotalTriggeredGrainsBorn())` and
    `REQUIRE(Δ getTotalTriggeredGrainsBorn() <= edges + 1)` — the detector counts *completed*
    excursions while FR-031 fires on the rise, so a burst still in flight at the end is the whole of
    the `+1`;
  - `REQUIRE(Δ getDroppedTriggerCount() == 0)`;
  - `REQUIRE(Δ getSkippedTriggerCountPoolFull() == 0 && Δ getSkippedTriggerCountRingCold() == 0)` —
    so the band cannot be satisfied by rejections;
  - `REQUIRE(Δ getTotalTriggeredGrainsBorn() >= 6)` — SC-027's own floor (`:2819`).
  Write the failure modes into the case comment, because there is **no independent edge source**
  (`SlowEventScheduler::isEventActive()`, `slow_event_scheduler.h:361`, lives on `sched_`, private to
  `VoragoVoice`): level-polling gives `Δ ≫ edges`; a missing or mis-gated wire gives `Δ == 0`; a
  per-voice rather than per-fold latch gives `Δ > edges + 1`; a trigger consumed but never turned into
  a grain gives `Δ < edges` with a skip counter moving; a latch that never re-arms gives `Δ == 1`.
- **(c)** `setGhostPeakLevel(0.0f)` (`vorago_engine.h:814`, Phase 10's gated arm at
  `vorago_engine_test.cpp:2793-2795`), everything else as (b): `getLevel()` holds its `0.0` base for
  the whole render and `REQUIRE(getTotalTriggeredGrainsBorn() == 0)` — because FR-031 latches on
  `ghostPeak_ * ghost`, identically zero there — **while** `maxGhostRequest >= kGhostRiseThreshold`
  (`:2828`) proves the events did fire, so the zero is the gate closing rather than a silent lane.
- **(d)** arm (b)'s `getLevel()` still shows **≥ 6** burst edges by the same detector: the spawn path
  did not replace the level gate (FR-033).
- **(e) the reverse-probability wire** (§8 ruling 11 — without it, half the Vorago wiring is
  unfalsifiable, since the component default is already `0.0f`): arm (b)'s fixture with
  `atmosGhostEventTriggers = false` and `atmosGhostReverseProbability = 1.0f`; after `prepare()`
  `REQUIRE(engine.atmosphere().getGrainReverseProbability() == 1.0f)`; then the **full-ring pre-roll**
  and a further render long enough for several density-scheduler births, and
  `REQUIRE(engine.atmosphere().getTotalGrainsBorn() > 0)` and
  `REQUIRE(engine.atmosphere().getTotalReverseGrainsBorn() == engine.atmosphere().getTotalGrainsBorn())`
  (at probability 1 the only forward outcome is the single exact draw `1.0f`, expected rate 2⁻³²). A
  zero birth count is a **fixture defect to fix**, never a reason to weaken the arm.

**Verify (red):** the TU does not compile — `VoragoEngineConfig` has no `atmosGhostReverseProbability`
and no `atmosGhostEventTriggers`.

---

## GROUP 14 — Implement the Vorago wiring

---

### T021 — `vorago_engine.h`: two inert config fields, the prepare-time forwarding, the rising-edge latch (FR-030–FR-034)

**Edit** `dsp/include/krate/dsp/systems/vorago_engine.h` only.

1. **Config (FR-030)** — two fields appended to the atmos block after `atmosFreezeFftSize`
   (verified `:125`), **both inert by default** (ADR-3, Q5):
   ```cpp
   /// Phase 10a. BOTH INERT BY DEFAULT (ADR-3, Clarifications 2026-09-22 Q5):
   /// Phase 10's SC-027 and its checked-in kEngineBaselineNsAtPoly4 were measured
   /// on the shipped ghost path, and a phase whose own criteria say the ceiling is
   /// unchanged may not move either. Phase 14 presets engage them.
   float atmosGhostReverseProbability = 0.0f;
   bool  atmosGhostEventTriggers = false;
   ```
2. **Prepare-time forwarding (FR-034)** — **exactly one line** added to `prepare()`'s FR-017 block
   (`:290-307`), after `atmos_.setDecorrelation(0.85f);`:
   `atmos_.setGrainReverseProbability(cfg.atmosGhostReverseProbability);  // Phase 10a, default 0.0`
   **Nothing else in that block moves**; the seven FR-017 values (`:299-307`) are untouched and
   `VoragoEngine_GhostConfiguration` clause 1 (`vorago_engine_test.cpp:2759-2777`) stays green with no
   edit.
   The config **shadow** goes in `prepare()`'s **step 2, "clamp the config"** (`:266-271`, beside
   `maxBlock` and the `polyphony_` clamp), **not** in the FR-017 block:
   `ghostEventTriggers_ = cfg.atmosGhostEventTriggers;`. Both anchors are recorded separately in the
   compliance table.
3. **Constants**, beside `kGhostBurstPeak = 0.60f` (verified `:206`) — defined **from** it so they
   cannot drift from `vorago_engine_test.cpp:2666-2667`:
   ```cpp
   static constexpr float kGhostTriggerRise = 0.5f  * kGhostBurstPeak;
   static constexpr float kGhostTriggerFall = 0.05f * kGhostBurstPeak;
   ```
4. **Members**, beside `ghostPeak_` (verified `:1515`):
   `bool ghostEventTriggers_ = false;` and `bool ghostTriggerHigh_ = false;` (the latch, in the shape
   `VoragoVoice::eventWasActive_` already uses, `vorago_voice.h:1775-1784`). Both cleared in
   `prepare()` and in `reset()` beside `atmos_.reset()` (`:1162`).
5. **The latch (FR-031)**, appended **immediately after** the level write at `:1272` — which is
   **neither moved nor conditioned** (FR-033):
   ```cpp
   if (ghostEventTriggers_) {
       const float gated = ghostPeak_ * ghost;   // the very expression written to setLevel above
       if (!ghostTriggerHigh_ && gated >= kGhostTriggerRise) {
           ghostTriggerHigh_ = true;
           atmos_.triggerGrain();                // FR-018
       } else if (ghostTriggerHigh_ && gated <= kGhostTriggerFall) {
           ghostTriggerHigh_ = false;
       }
   }
   ```
   The comment must record **why a hysteresis band and not "was exactly 0, now > 0"**: the combined
   request is `combineWake(0, eco, sched) = max` (`vorago_voice.h:1011-1014`, `:1846`) whose ecosystem
   term `ecosystemDepth_[k] * output[i]` (`:1734`) is **continuous** and `prepare()` installs depth
   `0.85` for every kind (`:617`), so an exact-zero predicate would fire at most **once per render**.
   And **why the latched quantity is the gated value**: it closes the spawn path with the level gate,
   makes SC-010 (c) literally SC-027 clause 2's closed arm, and stops a grain being spawned for a
   ghost nobody can hear. **FR-032** follows: with the flag false the block is skipped entirely,
   `triggerGrain()` is never called and the latch is not advanced.
   `runPreRenderControlStep()` runs inside `processStereoBlock` on the **audio thread**, which is what
   makes T017's audio-thread-only contract satisfied (`vorago_engine.h:887-889` calls it only at
   `phase == 0`, so Vorago issues at most one trigger per control chunk).

**Verify:** zero warnings; T020's clauses green (the `_Engaged` `[long]` case included); Phase 10's
`VoragoEngine_GhostConfiguration` green **with no edit**; `dsp_systems_tests.exe "Vorago*"` green.

---

## GROUP 15 — The CPU-delta criterion

---

### T022 — SC-009 `AtmosphereGhost_CpuDelta` `[.perf]` (`atmosphere_ghost_perf_test.cpp`)

**Edit** `dsp/tests/unit/systems/atmosphere_ghost_perf_test.cpp`. This TU is **never** in the
`-fno-fast-math` list — that would move the figures it measures.

**The measurement shape is pinned to the STAGE PROBE, not the gating arm.** `kStageWarmupBlocks = 300`,
`kStageTrials = 12`, `kStageBlocksPerTrial = 200` (`vorago_perf_test.cpp:300-302`), each 512-sample
block driven as **8 × 64-sample `processStereoBlock` calls** (`:1264-1277`), 48 kHz. The subject is one
`AtmosphereEngine` built exactly as `buildAtmosphere()` builds it (`:609-630`): `captureSeconds = 20`,
FR-017's seven values, `setLevel(VoragoEngine::kGhostBurstPeak = 0.60f)`. A best-of-25 × 500
measurement reads systematically lower than a best-of-12 × 200 one, so the shapes are not
interchangeable.
`bestNsPerBlock` / `warmThenMeasure` are **file-local** in `vorago_perf_test.cpp:330-353`, so
reimplement them locally (~20 lines) **with a citation** — exporting them would modify a file FR-047
pins.
`#include "vorago_perf_budget.h"` (T001) for `kEngineMeasuredNsAtPoly4`, `kEngineBaselineNsAtPoly4`,
`kCavernMeasuredNsPerBlock`, `kReferenceNs`. **No value is edited** (FR-041).

| Arm | Runs | REQUIRE (in-run) | WARN (recorded, never a gate) |
|---|---|---|---|
| 1 inert | p = 0, no triggers | — (it *is* the reference) | 31 114 = 28 285.5 × 1.10 |
| 2 reverse | p = 1.0, no triggers | `<= arm1 × 1.10` | 31 114 |
| 3 triggers, realistic | one `triggerGrain()` per **8.33 s** | `<= arm1 × 1.50` | 42 428 |
| 4 triggers, stress | one per **0.833 s** | `<= arm1 × 2.50` | 70 714 |
| 5 saturated drain | `setGrainSeconds(kMinGrainSeconds)`; `kMaxGrains` calls issued per 512-sample stage block so the queue is at the FR-019 cap at every chunk boundary | `<= arm1 × kSaturatedDrainFactor` | the measured figure |

- **28 285.5 ns/block is an artifact-log figure**
  (`specs/vorago-phase10-voice-engine/artifacts/perf.log:68`), not a checked-in constant, produced by
  the stage probe at the shape above. Absolute bounds derived from it are **WARN-only** (Q6): its own
  documented run-to-run drift is 0.6 %–7.4 % (`vorago_perf_test.cpp:239-247`), so an absolute REQUIRE
  would go red on a different machine on untouched code. **Arm 1 carries no REQUIRE of its own.**
- **Arm 3's cadence is derived at ENGINE level:** `2 schedulers/voice` (`vorago_voice.h:241`) × `6
  voices` (`kMaxVoices`) × `1 event / 20 s` (`slow_event_scheduler.h:164`) × `1/5 ghost share`
  (`kNumEventFamilies = 5`, `vorago_voice.h:259`, `:261`) = **0.12 ghost events/s = one per 8.33 s**.
  Overlapping bursts merge under the `std::max` fold (`vorago_engine.h:1248-1254`), which only lowers
  the edge count, so this is an upper bound. Concurrency `3.6 → 5.04` = **+40 %**; 1.50 is that with
  margin.
- **Arm 4** = `0.12 × 10` at `setEventRateScale`'s ceiling (`vorago_voice.h:1353-1358`) = **1.2/s**.
  Concurrency `3.6 → 18.0` = 5.0×, but a large part of the inert block cost (blur FFT, capture write,
  level smoother) is concurrency-independent, so **2.50 is this phase's own in-run regression bar** —
  a measurement above it is **reported as a finding, not widened in place**.
- **Arm 5 exists because arms 2–4 never measure the configuration the trigger API permits**: FR-019
  caps the queue at 64 and FR-020 drains it at one per sample, i.e. up to 48 000 extra birth attempts
  per second at 48 kHz — four orders of magnitude above arm 4. Each attempt costs the round-robin slot
  sweep (up to 64 integer tests, `:1601-1608`) and, if it clears, four `grainRng_` draws plus the
  reverse draw, two `ratioAtPitch` calls (`:1536-1538`) and `std::round`/`floor`/`ceil` on doubles
  (`:1665`, `:1692`, `:1700`, `:1705`, `:1737`) **before** it can be rejected at `:1740`.
  `kSaturatedDrainFactor` is taken from the **first** clean measurement (alone, cooled, P-core-pinned)
  plus the documented 0.6–7.4 % drift headroom, transcribed as a `constexpr`, recorded, and thereafter
  **frozen — never widened to make a later run pass**. Carry the comment that Vorago itself issues at
  most one trigger per control chunk (`vorago_engine.h:887-889`), so arm 5 bounds the shared
  component's **public contract**, not the Vorago operating point.
- **Clause 5 — computed and printed, never REQUIREd** (Q6): with `Δ` = arm 3 (and separately arm 4)
  minus arm 1, print
  `ceil((kEngineMeasuredNsAtPoly4 + Δ) × 1.05) + kCavernMeasuredNsPerBlock <= kReferenceNs`, i.e.
  `ceil((2 566 170 + Δ) × 1.05) + 124 497 <= 3 200 000`, i.e. **`Δ <= 362 880 ns/block`**. The ×1.05 is
  not optional — `vorago_perf_test.cpp:263-266` static-asserts it and `:269` the ceiling. Label the
  clause **dominated**: arm 4's own bound caps `Δ` at ≈ 42 429, about **8.6× tighter**. The binding
  clauses are 2, 3, 4 and 5's arm-5 sibling.

**Verify:** run **alone**, after a cool-down, P-core-pinned:
`node tools/run-cpu-tests.js dsp_systems_tests`. Never concurrently and never back-to-back with
another suite. A case that flips verdicts between runs is measuring the machine, not the code —
re-run alone after the machine idles before treating it as a defect. **Never relax a budget or shrink
a workload to make it pass.** All five arm figures plus the clause-5 arithmetic go in the compliance
table.

---

## GROUP 16 — The append-only gate and its evidence

---

### T023 — `tools/check-seraphis-green.js`: the `APPEND_ONLY_HEADERS` entry, with measured counts (FR-045, SC-006 clauses 1–2, 5–6)

**Edit** `tools/check-seraphis-green.js` only.

Add an `atmosphere_engine.h` entry to `APPEND_ONLY_HEADERS` (the rule table at `:102-117`, verified —
`:40-47` is the prose comment describing check 3, **not** the rule objects), in the shipped
`{ file, what, expected: [{ count, name, pattern }] }` shape used by the `multi_stage_envelope.h` and
`growth_envelope.h` entries:

```js
{
  file: 'dsp/include/krate/dsp/systems/atmosphere_engine.h',
  what: 'the Phase 10a ghost extension (reverse grains + triggerGrain)',
  expected: [
    { count: 2, name: 'the wUp/wDown rate lines',
      pattern: /const double w(Up|Down) = std::max\(/ },
    { count: /* measured, then frozen */ 0,
      name: 'the advance lambda AND the two renderGrainSpan loops that call it',
      pattern: /* anchored on the amended FR-045 site (ii) and ONLY those three ranges:
                   :1876-1882, :1888-1891, :1917-1930 */ },
    { count: /* measured, then frozen */ 0,
      name: 'the scheduler-tick block lines',
      pattern: /* anchored on scheduler_\.process, before = activeCount_, tryBirthGrain, bornAt\[slot\] */ },
    { count: 2, name: 'the reset()/setSeed() blur-seed lines',
      pattern: /blurRng_\.seed\(deriveStreamSeed\(/ },
    { count: 2, name: 'the pass-A scratch assign lines',
      pattern: /(retired|due)Scratch_\.assign\(kMaxGrains \* 2,/ },
    { count: 1, name: 'the pass-A scratch declaration comment (:2595-2596)',
      pattern: /Sized once in prepare\(\) \(2 \* kMaxGrains each\)/ },
    { count: 1, name: "AtmosphereGrain::readFrac's range comment (:1183)",
      pattern: /fraction in \[0,1\)/ },
  ],
}
```

The two `count`s marked *measured* are filled from the **real diff** now and then **frozen**. Patterns
must be written so a deletion **anywhere else** in the header matches nothing and fails the gate.

**Capture SC-006's evidence into the compliance record — quoted, not paraphrased.** The range is
**`git diff HEAD …` before this phase's work is committed** (the form the script itself runs: `:77`,
`numstat()` at `:152-155`, `deletedLines()` at `:171-177`), and
`git diff 374580d7d0f0631561413310bd3085e15ba7279c..HEAD …` after. **A bare `git diff --numstat
<path>` is never the range** — post-commit it reports nothing and passes vacuously.

1. `git diff HEAD --numstat --diff-filter=M -- dsp/tests/unit/systems/` names **zero** files except
   `vorago_perf_test.cpp`; every new TU and both new headers are `--diff-filter=A`.
2. `git diff HEAD -U0 -- dsp/include/krate/dsp/systems/atmosphere_engine.h` deletes lines at **only**
   the **six** anchors, with the per-anchor measured deletion counts tabulated:
   (i) `:1651-1652`; (ii) `:1876-1882` + `:1888-1891` + `:1917-1930`; (iii) `:2115`; (iv) `:555-556`
   and `:1015-1017`; (v) `:436-441`; (vi) `:1183` and `:2595-2596`.
5. `git diff HEAD --numstat --diff-filter=M -- plugins/seraphis/` names **zero** files.
6. **The structural gate for FR-013 and FR-025**, quoted from the `-U0` excerpt as token counts:
   - at `renderGrainSpan` (`:1829`): the `grain.reversed` test appears **above** the per-sample loops
     and **zero** occurrences of `reversed` appear between each loop's `for` and its closing brace;
   - at the pass-A loop (`:2115`): `pendingTriggers_` appears in the per-sample body **exactly once**,
     as the right operand of a short-circuited `&&` whose left operand is the loop-invariant
     `anyPending` declared above the `for`.

**Verify:** `node tools/check-seraphis-green.js` clean.

---

## GROUP 17 — Integration and gates

T024–T027 touch disjoint concerns but several run the same build; run T024 first, then T025 and T026
may run in parallel with T027's static half. T028 is last and alone.

---

### T024 [P] — CMake registration audit (the single registration task's closing half)

Re-read `dsp/tests/CMakeLists.txt` and confirm:
- every TU that exists under `dsp/tests/unit/systems/` with a `atmosphere_ghost_*` or
  `vorago_ghost_ext` name is **enumerated** in the `dsp_systems_tests` source list (an unlisted TU
  silently drops out and its cases never run — the file says so at `:377-379`);
- **exactly one** of them — `atmosphere_ghost_nonfinite_test.cpp` — is in the `-fno-fast-math` opt-in
  list beside `:896`, and the perf TU is **not**;
- `vorago_perf_budget.h` and `atmosphere_ghost_fixtures.h` are headers and correctly have **no** CMake
  entry;
- `dsp_systems_tests.exe --list-tests` lists every case named in this document and **no placeholder
  from T003 survives**.

**Verify:** the list-tests output is pasted into the compliance record.

---

### T025 [P] — The three-toolchain probe and the FR-046 measured bounds

For each stored-golden comparison this phase adds — **T018 clause 1** (`kBaseCommitFingerprint`) and
**T020 (a)** (`kBaseCommitVoragoFingerprint`) — run the identical fixture on three toolchains:
- **MSVC**, measured directly here;
- **GNU** and **LLVM**, from the CI legs or a local WSL run.

Take the **worst observed deviation across all three, add headroom**, and set
`kMeasuredMetricTolerance` / `kMeasuredSampleTolerance` per comparison. Each must be **looser** than
the shared `kMetricTolerance = 2.5e-4` / `kSampleTolerance = 5.0e-4f` and carry the two
`static_assert`s (bound is looser; shared constants still hold their shipped values). Transcribe the
measured bounds **back into `spec.md`** under FR-046, with the probe's three figures. **No bound may be
widened afterwards without a ruling.**

**Verify:** T018 clause 1 and T020 (a) green at the measured bounds, with the `// T025` skip markers
removed; the paste-ready-literal-on-failure path exercised once by deliberately perturbing the
reference locally and then reverting.

---

### T026 — Full-suite run and the untouched-consumer gate (SC-006 clauses 3–5)

```bash
CMAKE="C:/Program Files/CMake/bin/cmake.exe"
"$CMAKE" --build build/windows-x64-release --config Release --target dsp_systems_tests
build/windows-x64-release/bin/Release/dsp_systems_tests.exe 2>&1 | tail -20
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "AtmosphereEngine_*" 2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "Seraphis*"          2>&1 | tail -5
build/windows-x64-release/bin/Release/dsp_systems_tests.exe "Vorago*"            2>&1 | tail -5
"$CMAKE" --build build/windows-x64-release --config Release \
         --target dsp_effects_tests dsp_processors_tests seraphis_tests Seraphis
build/windows-x64-release/bin/Release/dsp_effects_tests.exe    2>&1 | tail -3
build/windows-x64-release/bin/Release/dsp_processors_tests.exe 2>&1 | tail -3
build/windows-x64-release/bin/Release/seraphis_tests.exe       2>&1 | tail -3
tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Seraphis.vst3"
```

Clause 3: the full `dsp_systems_tests` suite passes including **every** `atmosphere_engine_*` and
`seraphis_*` case, with **no Seraphis or Phase-5 assertion, threshold, baseline or default modified** —
`AtmosphereEngine_NonFiniteGuardSurvivesFastMath` (the deliberate `-fno-fast-math` *exclusion* at
`dsp/tests/CMakeLists.txt:881-895`) among them, unedited.
Clause 4: `dsp_effects_tests`, `dsp_processors_tests` and the `vorago_*` cases pass unchanged; Phase
10's `VoragoEngine_GhostConfiguration` is green with **no edit**.
Clause 5: `Seraphis` and `seraphis_tests` build with **zero warnings**, `seraphis_tests` is green, and
pluginval passes on the rebuilt bundle.

**Every case's measured runtime is recorded in the compliance table, tagged or not** (FR-048). Any
case measured over ~15 s with a toolchain-independent assertion is re-checked against FR-048's rule —
but **SC-004 is never tagged `[long]` at any cost** (NaN/Inf-guard + bounded-grid); if long, it splits
(T011 keeps the sentinels, T019 takes the sweep).

**Verify:** all green; if anything is red, it is **owned and fixed here**, never labelled pre-existing.

---

### T027 — Static gates, ASan, and the portability probe

```bash
node tools/check-portability.js     # FR-043 - MSVC-green proves nothing for Linux/macOS
node tools/lint-layers.js           # FR-044 - atmosphere_engine.h's include list gained NOTHING
node tools/lint-odr.js              # roadmap line 594
node tools/check-seraphis-green.js  # T004 + T023
./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja
# The GCC leg for R-5 (templated lambdas / .template operator()<...>()):
#   build dsp_systems_tests under WSL GCC. If any leg objects, fall back to the
#   private member function template documented in T015 step 4.
# SC-012's sanitizer half - the ONLY detector of the FR-051 overflow:
"$CMAKE" -S . -B build-asan -G "Visual Studio 17 2022" -A x64 -DENABLE_ASAN=ON
"$CMAKE" --build build-asan --config Debug --target dsp_systems_tests
build-asan/bin/Debug/dsp_systems_tests.exe "AtmosphereGhost_PassAScratchBound" 2>&1 | tail -30
```

The ASan run is done **twice** and **both outcomes are recorded**: with the sizing left at
`kMaxGrains * 2` it must **abort with a heap-buffer-overflow**; at `kMaxGrains * 3` it must **pass**.

Also re-run the ODR sweep from T008 and paste the result.

**Verify:** every tool clean; **all clang-tidy warnings fixed, none deferred**; zero compiler warnings
across every target built in T026.

---

### T028 — SC-009 alone, after a cool-down (final measurement)

`node tools/run-cpu-tests.js dsp_systems_tests` — **nothing else running, not concurrently and not
back-to-back**; the runner settles 20 s between suites. P-core-pinned. Record all five arm figures,
the four in-run ratios, the WARN absolutes and clause 5's printed arithmetic.

**Verify:** arms 2, 3, 4 and 5 satisfy their in-run REQUIREs. A figure above a bound is **a finding
reported for a ruling**, never a bound widened in place.

---

### T029 — Compliance record

Fill the phase's compliance table with **concrete evidence only** — file paths, line numbers, test
case names, actual measured numbers, quoted log lines. Every row must be verified against code or
output produced in this phase; a row that says "implemented" or "test passes" without specifics is not
acceptable, and a ✅ that was not just now verified is worse than an honest ❌.

Rows that must be present because they are easy to fake and load-bearing here:
- **FR-045** — the six per-anchor measured deletion counts and the quoted `git diff HEAD -U0`
  excerpts, plus the `--numstat` row for `vorago_perf_test.cpp`;
- **SC-006 clause 6** — the two token counts, quoted;
- **FR-050** — all three of T013's differential measurements (clause in / clause out / clause-disabled
  stale-read observation);
- **FR-051** — SC-012's portable delta (expected ≈ 186, bound `> 128` and `<= 192`) **and both** ASan
  outcomes;
- **FR-046** — the three-toolchain probe figures and the measured bounds now recorded in `spec.md`;
- **SC-009** — five arm figures, four ratios, the WARN absolutes, clause 5's arithmetic, and the frozen
  `kSaturatedDrainFactor`;
- **FR-048** — the measured runtime of **every** new case, tagged or not, with the tagging decision
  and its justification;
- the pre-roll **sample counts** (and the seconds they work out to) actually used by each fixture:
  1 048 576 / 21.845 s (48 kHz, `captureSeconds = 20`), 65 536 / 1.365 s (SC-004), 262 144 / 32.768 s
  (SC-010 (b), 8 kHz), 1 024 (SC-012), 240 000 (SC-011's deliberate 5 s partial fill).

---

## Dependency summary

```
G1  T001 -> T002 -> T003 -> T004 -> T005        scaffolding, CMake, base-commit references
G2  T006 [P]  T007 [P]                          red: setter contract, accessors
G3  T008                                        S1 + S5
G4  T009                                        red: RNG identity + reverse-stream replica
G5  T010                                        S2 (a)(d) the fifth draw
G6  T011 [P]  T012 [P]                          red: FR-050, truncation, liveness
G7  T013                                        S2 (b)(c) direction terms + fill clause
G8  T014                                        red: time-reversal, fractional ratio, partitioning
G9  T015                                        S3 backwards walk + direction hoist
G10 T016                                        red: trigger accounting, scratch bound, RT safety
G11 T017                                        S4 triggerGrain + pass-A + FR-051 sizing
G12 T018 [P]  T019 [P]                          fingerprint clause, determinism, [long] sweep
G13 T020                                        red: Vorago wiring
G14 T021                                        S6 config fields, forwarding, hysteresis latch
G15 T022                                        SC-009 perf TU
G16 T023                                        append-only gate + SC-006 evidence
G17 T024 [P] T025 [P] T026 -> T027 -> T028 -> T029   audit, bounds, suites, gates, perf, compliance
```
