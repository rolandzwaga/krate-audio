# Tasks: Vorago Phase 13b — Ecosystem Audibility

**Spec:** `specs/vorago-phase13b-ecosystem-audibility/spec.md` · **Plan:** `plan.md` (same directory)
**Branch / base:** `feat/vorago-phase1-events-modulation` at `9ecd6d10`
**Status:** TASKS. Dependency-ordered. No commit tasks; commits happen outside this workflow.

### Plan decisions, ruled 2026-09-27 (spec Clarifications, plan stage R-1 … R-3)

- R-1: the CMake registration split (T005 registers both TUs; T037 re-verifies) is accepted as written.
- R-2: `VoragoEngine_EcosystemLeverBounded` stays per-push at 60 s; T022 prints its wall clock and T031
  cites it; **if it exceeds 4 minutes measured locally, STOP and surface the figure** — never shorten,
  split or tag it `[long]`.
- Build-stage ruling (2026-09-28, T025): `VoragoEngine_GhostExtensionWiring` clause (a) is the one
  FR-031(b) surfaced item — expected red through the ladder, re-pinned once at T028 on the final tree.
  Also fixed at T025 by the main loop (FR-031(a) defect in this phase's SC-012): `clearRunState()`
  now restores the prepare-time note defaults before the component resets (see
  `artifacts/t025_resolution.md`).
- R-3: **L8 is pre-authorised** in T026: if L7 is reached, lower the noise wake base default (parameter
  301 / `kNoiseWakeId`, 0.35, and its macro base) as a recorded ruling, update the Phase 12
  parameter-table expectation (`param_table_expected.h`) under FR-031's surfaced list, re-run Gate 1,
  SC-005 and SC-020's Noise arm; stop and surface only if L8 also fails. Macro-row bases and
  `EcosystemEngine` stay out.

## How to read this file

- Tasks are grouped. **Groups run strictly in order.** Inside a group, a task marked **[P]** may run in
  parallel with the other [P] tasks of that group: it creates **new files only**, and no other task in the
  group touches them. An unmarked task runs alone, in listed order.
- Every task is self-contained. It names the files, the test to write **first** (and what makes it fail),
  the implementation intent, and the target that verifies it.
- Canonical loop for every code task: failing test → implement → **zero warnings** → tests pass.
- Build command (Windows, always the full path):
  `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target <target>`
- Run one suite directly (never `ctest -R <exe>`):
  `build/windows-x64-release/bin/Release/<target>.exe "<TestName*>" 2>&1 | tail -5`
- Long probe runs and CPU runs go to a log file in `specs/vorago-phase13b-ecosystem-audibility/artifacts/`,
  **one at a time, nothing else executing** (no build, no other suite, no clang-tidy). Never re-run a slow
  suite to see its output again: read the log.
- **Stop-and-surface** items are listed in the task that can hit them. Stop means: record the measured
  figures, do not change a threshold, candidate, range or test file, and report to the user.

### Fixed facts every task relies on (verified this session)

| Fact | Where |
|---|---|
| `namespace detail` friend forward declarations end with `struct VoragoEcosystemRuleProbe;` | `dsp/include/krate/dsp/systems/vorago_voice.h:170` (block `:155-171`) |
| `VoragoVoice` befriends them; `friend struct detail::VoragoEcosystemRuleProbe;` | `vorago_voice.h:1535` |
| `VoragoEngine` friends; `friend struct detail::VoragoEcosystemRuleProbe;` | `dsp/include/krate/dsp/systems/vorago_engine.h:1304` |
| Prepare step 5 literals: `setNoiseLevelDb(-18.0f)` `:569`, `setNoiseWakeBase(0.35f)` `:571`, `resonance_.setPeakLevel(p, -9.0f)` `:581`, `resonance_.setFreqWander(p, 1.5f)` `:582`, `peakWakeBase_.fill(0.50f)` `:585`, `setEcologyLoopGain(FeedbackEcology::kDefaultLoopGain)` `:590`, ring coupling `0.12f` `:596`, `loopWakeBase_.fill(0.50f)` `:598` | `vorago_voice.h` |
| `applyIdentityLanes(const IdentityLanes&)` — wake writes, Partial pair `:1939-1941`, ghost request | `vorago_voice.h:1911-1944` |
| `installIdentityNeutral()` = `applyIdentityLanes(kNoLanes)` + gravity | `vorago_voice.h:1970-1979` |
| `publishIdentity()` = gather eco → gather sched → apply → life lanes → event-rate scale | `vorago_voice.h:2022-2028` |
| Identity-routing members (`noiseWakeBase_` array per source, `peakWakeBase_`, `loopWakeBase_`, `ecosystemDepth_`, `mutationBase_`, `bloomDepthBase_`) | `vorago_voice.h:2360-2376` |
| `static_assert(sizeof(VoragoVoice) <= VoragoVoice::kVoiceSizeBound, …)` | `vorago_voice.h:2428` |
| Voice API: `prepare(double, const VoragoVoiceConfig&)` `:475`, `silence()` `:774`, `noteOn(float hz, float velocity)` `:907`, `scheduler(std::size_t) const` `:1519`, `advanceOneChunkLifeOnly()` `:2274` | `vorago_voice.h` |
| Engine API: `prepare(double, const VoragoEngineConfig&)` `:329`, `setPolyphony` `:565`, `setSeed` `:602`, `noteOn(uint8, uint8)` `:647`, `processStereoBlock` `:1093`, `processOutputStage` `:1208`, `getNonFiniteRecoveryCount` `:1293`, `isRendering(v)` (private) `:1345`, `getAllocatedBytes` `:534` | `vorago_engine.h` |
| Probe TU: header options `:26-38`, friend `struct VoragoEcosystemRuleProbe` `:70-88`, `kCandidates` `:217-232`, `RenderResult` `:234-239`, `stereoRmsDb` `:242-253`, `renderOnce` `:258-363`, `TEST_CASE("Vorago_EcosystemRuleProbe", "[.probe][vorago]")` `:380` | `plugins/vorago/tests/integration/ecosystem_rule_probe_test.cpp` (534 lines) |
| `ClickDetectorConfig{sampleRate, frameSize, hopSize, detectionThreshold, energyThresholdDb, mergeGap}` | `tests/test_helpers/artifact_detection.h:37-44` |
| `fingerprintRender(span)` `:73`, `compareFingerprints(actual, ref, metricTol = kMetricTolerance (2.5e-4), sampleTol)` `:122`, `.withinTolerance()` `:108` | `tests/test_helpers/render_fingerprint.h` |
| `detail::isFinite(float)` (bit-pattern) | `dsp/include/krate/dsp/core/db_utils.h:118` |
| `dsp_systems_tests` Vorago TU list ends at `unit/systems/vorago_macro_retune_probe_test.cpp` | `dsp/tests/CMakeLists.txt:551` (closing `)` `:552`) |

**ODR sweep (FR-024), re-run this session:** `grep -rn "VoragoEcosystemLeverProbe\|shapeLeverInput\|injectedEco_\|kLeverInputGain" dsp plugins tests`
→ 0 hits. Plan §1.7 lists the other introduced names (0 hits). **Any task that introduces a name not in
plan §1.7 re-runs `grep -rn "<Name>" dsp/ plugins/ tests/` first and stops if it hits.**

---

## Group 0 — Baseline (sequential)

### T001 — Confirm the starting tree and create the evidence directory
- **Do:** `git rev-parse --abbrev-ref HEAD` must print `feat/vorago-phase1-events-modulation`; `git log -1
  --format=%h` records the phase base (plan: `9ecd6d10`; if HEAD has moved, record the actual hash — it is
  the `<phase-base>` for SC-015's diff). Create `specs/vorago-phase13b-ecosystem-audibility/artifacts/`.
- **Build:** `dsp_systems_tests vorago_tests` → 0 warnings.
- **Verify:** `dsp_systems_tests.exe "Vorago*" 2>&1 | tail -5` and `vorago_tests.exe 2>&1 | tail -5` both
  "All tests passed" (these are the pre-change reference; the timing-tag exclusions are the CI ones:
  `"~[performance]~[perf]~[benchmark]~[!benchmark]~[long]"`).
- **No code change.**

---

## Group 1 — Instrument seams (sequential; shared headers)

### T002 — Declare the new friend `detail::VoragoEcosystemLeverProbe` (no behaviour change)
- **Files (edit):** `dsp/include/krate/dsp/systems/vorago_voice.h`, `dsp/include/krate/dsp/systems/vorago_engine.h`.
- **Failing test first:** none possible (a declaration). Its first user is T003; T003 does not compile
  without this task.
- **Implement:**
  - `vorago_voice.h`, inside the `namespace detail { … }` block, after `struct VoragoEcosystemRuleProbe;`
    (`:170`): add
    `/// Phase 13b FR-023 lane-injection / lever probe. B-4: DEFINED IN THE TEST TU` +
    `struct VoragoEcosystemLeverProbe;`
  - `vorago_voice.h`, after `friend struct detail::VoragoEcosystemRuleProbe;` (`:1535`):
    `friend struct detail::VoragoEcosystemLeverProbe;  // Phase 13b FR-023 (B-4)`
  - `vorago_engine.h`, after `:1304`: `friend struct detail::VoragoEcosystemLeverProbe;  // Phase 13b FR-023 (B-4)`
    (the forward declaration is visible because `vorago_engine.h` includes `vorago_voice.h`).
- **Verify:** build `dsp_systems_tests vorago_tests`, 0 warnings. No test result may change.

---

## Group 2 — New test TUs (parallel: two new files)

### T003 [P] — Create `vorago_ecosystem_lever_test.cpp` with the friend definition and the lane survey
- **File (new):** `dsp/tests/unit/systems/vorago_ecosystem_lever_test.cpp`.
- **Content:**
  1. File header comment: Phase 13b, plan §5.1/§5.2; "the ONE definition of
     `detail::VoragoEcosystemLeverProbe` in `dsp_systems_tests` — no other TU may define it"; tags
     `[systems][vorago]`; finiteness by `detail::isFinite` only (no `std::isnan`); voices/engines on the
     heap (`std::make_unique`).
  2. `namespace Krate::DSP::detail { struct VoragoEcosystemLeverProbe { … }; }` with **only** the accessors
     that touch existing members (plan §7 step 1):
     - `using Lanes = std::array<std::array<float, VoragoVoice::kMaxSlotsPerKind>, EcosystemEngine::kNumKinds>;`
     - `static void advanceLifeOnly(VoragoVoice& v) { v.advanceOneChunkLifeOnly(); }`
     - `static EcosystemEngine& ecosystem(VoragoVoice& v) { return v.ecosystem_; }`
     - `static void gatherEco(const VoragoVoice& v, Lanes& out)` — builds a value-initialised
       `VoragoVoice::IdentityLanes`, calls `v.gatherEcosystemLanes(l)`, copies `l.eco` to `out`.
     - `static bool slotAddressed(const VoragoVoice& v, std::size_t kind, std::size_t slot)` — true iff some
       agent `i < v.ecosystem().getAgentCount()` has `v.agentValid_[i] != 0`, kind
       `static_cast<std::size_t>(v.ecosystem().getAgentKind(i)) == kind` and `v.agentSlot_[i] == slot`.
     - `static float peakWakeBase(const VoragoVoice& v, std::size_t p)`, `loopWakeBase(v, l)`,
       `noiseWakeBase(v, s)` returning `v.peakWakeBase_[p]` / `v.loopWakeBase_[l]` / `v.noiseWakeBase_[s]`.
     - `static std::uint8_t lastEventTarget(const VoragoVoice& v, std::size_t k) { return v.lastEventTarget_[k]; }`
     - `static void setLimiterCeilingDb(VoragoEngine& e, float db) { e.limiter_.setCeilingDb(db); }`
     - `static bool isRendering(const VoragoEngine& e, std::size_t v) { return e.isRendering(v); }`
  3. `TEST_CASE("VoragoVoice_EcosystemLaneSurvey", "[systems][vorago][.probe]")` (plan §3.4), **no REQUIRE**
     except that `prepare` succeeded:
     - Voice prepared at 48 000 Hz with the FR-090 default config, seed
       `deriveStreamSeed(1u, VoragoEngine::kVoiceSaltBase + 0u)` (`core/random.h:102`), ecosystem depth left
       at the prepare default 0.85, `noteOn(65.406f, 100.0f / 127.0f)`.
     - Advance by `advanceLifeOnly` to 340 s (`340 * 48000 / 64` chunks). On every chunk where
       `ecosystem().getControlStepCount()` increased and time ∈ [155 s, 340 s], gather eco lanes and append
       `lanes[k][s]` for k ∈ {Resonator, Feedback, Noise}, slots with `slotAddressed == true` only (Q6).
     - Print, per kind: the addressed (step, slot) count; `Q_k(p)` for p ∈ {0.10, 0.25, 0.50, 0.60, 0.75, 0.90}
       (sorted multiset, nearest-rank); the fraction of addressed pairs with lane `>` the base currently in
       the tree (read via `peakWakeBase(v,s)` / `loopWakeBase(v,s)` / `noiseWakeBase(v,s)` for the pair's
       slot); and the §3.4 rule's result
       `clamp(floor(Q_k(0.60) / 0.05) * 0.05, 0.05, 0.50)` for Resonator and Feedback.
     - If `Q_Resonator(0.60) < 0.05` or `Q_Feedback(0.60) < 0.05`, print `SURVEY STOP: floor wins for <kind>`.
- **Failing test first:** the survey is a diagnostic with no gate; its "red" state is that it does not
  build until T005 registers it.
- **Verify:** after T005, `dsp_systems_tests.exe "VoragoVoice_EcosystemLaneSurvey" 2>&1 | tail -40` prints the
  table. It must not appear in a default (untagged) run (hidden `[.probe]`).

### T004 [P] — Create the `[long]` TU skeleton
- **File (new):** `dsp/tests/unit/systems/vorago_ecosystem_lever_longrun_test.cpp`.
- **Content:** file header comment (Phase 13b, plan §5.3, SC-010(b), "public API only — does NOT define
  `detail::VoragoEcosystemLeverProbe`"), the includes it will need (`<krate/dsp/systems/vorago_voice.h>`,
  `<krate/dsp/core/db_utils.h>`, `artifact_detection.h`, `<catch2/catch_test_macros.hpp>`, `<memory>`,
  `<vector>`, `<cstdio>`). **No TEST_CASE yet** — the case is written in T024 against the tuned levers.
- **Verify:** compiles after T005 with 0 warnings.

---

## Group 3 — CMake registration (sequential; shared file)

> **Ordering note.** The workflow format puts CMake registration in the last group. These two TUs must be
> registered **before** any of their failing tests can be built and seen to fail, so the single
> registration task lives here; the last group re-verifies it (T037). This is the only deviation from the
> format and it is required by the test-first rule.

### T005 — Register both new TUs in `dsp_systems_tests` (the ONE CMake task)
- **File (edit):** `dsp/tests/CMakeLists.txt`.
- **Implement:** after `unit/systems/vorago_macro_retune_probe_test.cpp` (`:551`) and before the closing
  `)` (`:552`), add the plan §6 comment block verbatim and the two lines:
  ```cmake
      # Vorago Phase 13b (specs/vorago-phase13b-ecosystem-audibility): ecosystem levers.
      # ENUMERATED, not globbed - an unregistered TU silently drops out of the build.
      #   vorago_ecosystem_lever_test.cpp          SC-006..SC-009, SC-010 (a), SC-011, SC-012,
      #                                            SC-013, SC-018, SC-020, SC-021, the
      #                                            LaneSurvey [.probe]; + the ONE definition of
      #                                            detail::VoragoEcosystemLeverProbe
      #   vorago_ecosystem_lever_longrun_test.cpp  SC-010 (b)            (the [long] set)
      unit/systems/vorago_ecosystem_lever_test.cpp
      unit/systems/vorago_ecosystem_lever_longrun_test.cpp
  ```
  Neither TU joins the `-fno-fast-math` block.
- **Verify:** build `dsp_systems_tests`, 0 warnings; `dsp_systems_tests.exe --list-tests "[.probe]" | grep
  VoragoVoice_EcosystemLaneSurvey` finds the case. No plugin CMake change (the probe TU is already in
  `vorago_tests`).

---

## Group 4 — Probe TU extensions (sequential; all edit one existing file)

All four tasks edit `plugins/vorago/tests/integration/ecosystem_rule_probe_test.cpp`. The TU keeps its
`[.probe][vorago]` tag, stimulus, timeline, `VoragoTest::describe`/`meanOf`/`descriptorDistance`, the 14
`kCandidates` unchanged (`:217-232`) and the `leakExponent` exclusion (FR-002, FR-003). **Do not touch
`kCandidates`.** Verification target: `vorago_tests`.

### T006 — Per-minute RMS and colony output (FR-004, FR-005)
- **Failing test first:** in the `TEST_CASE` body, after the base render, add
  `REQUIRE(base.minuteRmsDb[0] == base.m1RmsDb);` and
  `REQUIRE(base.minuteColony[0] >= 0.0);` — does not compile until the fields exist.
- **Implement:**
  - Friend (`:70-88`) gains `static double meanColonyOutput(const VoragoEngine& e)`: mean over voices with
    `e.isRendering(v)` of `voices_[v].ecosystem().getAgentOutput(i)` for every `i < getAgentCount()`;
    returns `-1.0` if no voice renders.
  - `RenderResult` (`:234-239`) gains `std::array<double, 3> minuteRmsDb{}` and
    `std::array<double, 3> minuteColony{}`; `m1RmsDb` stays and is set from `minuteRmsDb[0]`.
  - `renderOnce`: `minuteRmsDb[m] = stereoRmsDb(l.subspan(m * kMinuteSamples, kMinuteSamples), r.subspan(…))`
    for m = 0,1,2 (replacing the M1-only line at `:361`); `minuteColony[m]` = mean of `meanColonyOutput`
    sampled after every `process()` block whose samples fall in minute m (skip `-1` samples).
- **Verify:** build `vorago_tests`, 0 warnings. Run
  `VORAGO_PROBE_KNOBS=- vorago_tests.exe "Vorago_EcosystemRuleProbe" > artifacts/instr_t006.log 2>&1`; the log
  shows the new REQUIREs passed.

### T007 — True ecosystem-off reference (FR-007, FR-006)
- **Failing test first:** add, in the reference path, `REQUIRE(ref.offValid);` with `offValid` not yet a
  member → does not compile.
- **Implement:**
  - Friend gains `static void silenceColony(VoragoEngine& e)` — for every `v < VoragoEngine::kMaxVoices`,
    `setAgentDormant(i, true)` for every agent of `e.voices_[v].ecosystem_` — and
    `static bool colonySilent(const VoragoEngine& e)` — true iff every rendering voice's every
    `getAgentOutput(i) == 0.0f`.
  - `renderOnce` gains `bool colonyOff`. When set: call `silenceColony` **after every** `process()` block
    (dormancy survives the per-block macro apply that rewrites depth, plan §4.1); from the first M1 block
    to the end of M3 AND `offValid &= colonySilent(engine)` after every block. `RenderResult` gains
    `bool offValid = true`.
  - `VORAGO_PROBE_REF=off` selects `colonyOff = true` with no parameter change; any other value keeps
    today's override behaviour. Whenever the knob filter is not `"-"`, the true-off reference is
    **always** rendered (FR-004 (iii)). When `VORAGO_PROBE_SURFACE` is empty, also render the `900=0`
    reference and print `d(true-off, 900=0)/t0` (FR-007 cross-check).
  - Header comment (`:26-38`) documents `VORAGO_PROBE_REF=off`.
- **Verify:** build, 0 warnings. `VORAGO_PROBE_KNOBS=- VORAGO_PROBE_REF=off vorago_tests.exe
  "Vorago_EcosystemRuleProbe" > artifacts/instr_t007.log 2>&1`: `REQUIRE(ref.offValid)` passes and the log
  prints the reference distance and the cross-check line.

### T008 — Limiter gain-reduction twin (SC-005, plan §4.4) — `VORAGO_PROBE_GR=1`
- **Failing test first:** add the two validity REQUIREs (below) against `base.capL/capR` and
  `unl.capL/unl.capR`, which do not yet exist → does not compile.
- **Implement:**
  - Friend gains `static void setLimiterCeilingDb(VoragoEngine& e, float db) { e.limiter_.setCeilingDb(db); }`.
  - `renderOnce` gains `bool unlimited` (calls `setLimiterCeilingDb(engine, +60.0f)` once after
    `setActive(true)`; skips the `peak <= 0.9661` check for that render only) and `bool keepCapture`
    (keeps `std::vector<float> capL, capR` in `RenderResult`; every other render drops them).
  - With `VORAGO_PROBE_GR=1`, render the base twice (shipped `y`, unlimited `u`) with `keepCapture`.
    Over M1–M3:
    - `GR_dB = max over i with max(|yL|,|yR|) > 1e-6 of 20·log10(max(|uL|,|uR|) / max(|yL|,|yR|))`.
    - Print `GR max <dB> at <s>`, the fraction of samples with GR > 0.01 dB (labelled "includes release
      tails"), the attack-event count (per-sample gain drop > 0.01 dB), and the first index with `y != u`
      or `none`.
    - **Validity check 1 (REQUIRE on every sample with `max(|uL|,|uR|) > 1e-6`):**
      `max(|yL|,|yR|) <= max(|uL|,|uR|) * (1 + 1e-6)`.
    - **Validity check 2 (REQUIRE on every sample with `min(|uL|,|uR|) > 1e-3`):**
      `|yL/uL - yR/uR| <= 1e-5`.
    - Do **not** assert `GR <= 1 dB` (read from the log by compliance, FR-006).
  - Header comment documents `VORAGO_PROBE_GR`.
- **Verify:** build, 0 warnings. `VORAGO_PROBE_KNOBS=- VORAGO_PROBE_GR=1 vorago_tests.exe
  "Vorago_EcosystemRuleProbe" > artifacts/instr_t008.log 2>&1`: both checks pass; `GR max` line present.

### T009 — Flags, verdict lines, descriptor dump, hand-off line (FR-004, FR-005, FR-010–FR-014, FR-030, FR-034)
- **Failing test first:** none new (verdicts are **not** asserted, FR-006). The check is the log format,
  verified below.
- **Implement (plan §4.5):** per extreme against the base render, print `d`, `d/t0`,
  `dOff/t0 = descriptorDistance(extreme, trueOff)/t0`, `rms[m]`, `ΔRMS[m] = rms[m] - base.rms[m]`,
  `colony[m]`, `colony[m]/base.colony[m]` for m = M1..M3, and flags:
  - `KILL` ⇔ ∃m: `|ΔRMS[m]| > 6.0` dB **or** `rms[m] < -60.0` dBFS;
  - `OFF-LIKE` ⇔ `dOff < t0`;
  - `INAUDIBLE` ⇔ `d < 2·t0`.
  - **`t0` (ruling 2026-09-28, applied by the main loop at T026):** `d(true-off, true-off seed twin)`, one
    extra render inside the true-off block; `d(default, seed twin)` prints as `t0on`; `GATE1` carries both.
  A counted extreme is `d >= 2·t0 && !KILL && !OFF-LIKE`; a knob's best is its best counted extreme. Lines:
  - `DESCRIPTOR default band=b0,…,b8 motion=… flux=… corr=… energySpread=… crest=…` and the same for
    `trueoff` (all 14 `PresetDescriptor` components, `%.6f`), every run;
  - `GATE1 surface=<default|109=1> t0=… d(on,off)=… d/t0=… verdict=PASS|FAIL` (PASS ⇔ `d/t0 >= f`, **f = 0.5, ruled 2026-09-28**; the six-seed `GATE1M` line is the gate);
  - `audible non-kill knobs: N of 14`;
  - `GATE2 verdict=PASS|FAIL (N >= 4)` only on a default-surface full-table run;
  - replace the Phase 14 roster/`STOP` block with `table for Phase 14 Q2 — no roster named here`.
  Assertions stay exactly: finite by bit pattern, `peak <= 0.9661` (not on the unlimited twin), default
  M1 RMS `>= -60` dBFS, `offValid`, GR checks 1 and 2.
- **Verify:** build, 0 warnings. `git diff` of the TU shows `kCandidates` unchanged. A
  `VORAGO_PROBE_KNOBS=predation VORAGO_PROBE_REF=off vorago_tests.exe "Vorago_EcosystemRuleProbe" >
  artifacts/instr_t009.log 2>&1` run shows every column and flag for that one knob and both DESCRIPTOR lines.

---

## Group 5 — Instrument checkpoint (sequential)

### T010 — Prove the instrument tree changes no production behaviour
- **Do:** build `dsp_systems_tests vorago_tests`, **0 warnings**. Run `dsp_systems_tests.exe
  "~[performance]~[perf]~[benchmark]~[!benchmark]~[long]" 2>&1 | tail -5` and
  `vorago_tests.exe "~[performance]~[perf]~[benchmark]~[!benchmark]~[long]" 2>&1 | tail -5` → "All tests
  passed", same case counts as T001 (the two new TUs add only a hidden case).
- **Verify:** `git diff --name-only` lists only `vorago_voice.h`, `vorago_engine.h` (friend lines only),
  `dsp/tests/CMakeLists.txt`, the two new dsp TUs and the probe TU.

---

## Group 6 — FR-001 before-record (sequential; one run at a time, machine otherwise idle)

Record each run's wall-clock (`date` before/after) in the log's first/last line; later runs are estimated
from these, never guessed.

### T011 — Before Gate-1, default surface, with GR
`VORAGO_PROBE_KNOBS=- VORAGO_PROBE_REF=off VORAGO_PROBE_GR=1 build/windows-x64-release/bin/Release/vorago_tests.exe "Vorago_EcosystemRuleProbe" > specs/vorago-phase13b-ecosystem-audibility/artifacts/before_gate1_default.log 2>&1`
- **Verify:** log has `DESCRIPTOR default`, `DESCRIPTOR trueoff`, `default M1 stereo RMS`, `t0`, `GATE1`
  (expected FAIL, d/t0 ≈ 0.37), the `900=0` cross-check line, `GR max`, and all REQUIREs passed.

### T012 — Before Gate-1, Life max
Same command with `VORAGO_PROBE_SURFACE=109=1` and without `VORAGO_PROBE_GR` →
`artifacts/before_gate1_lifemax.log`. **Verify:** `GATE1 surface=109=1` line and `offValid` passed. This
is SC-002's **before** value (replaces run 3's depth-0.15 figure).

### T013 — Before full tables, both surfaces
`vorago_tests.exe "Vorago_EcosystemRuleProbe" > artifacts/before_table_default.log 2>&1`, then
`VORAGO_PROBE_SURFACE=109=1 … > artifacts/before_table_lifemax.log 2>&1`. **Verify:** 14 knob rows with
every column; `audible non-kill knobs: N of 14` present in both.

### T014 — Before CPU
`node tools/run-cpu-tests.js dsp_systems_tests > specs/vorago-phase13b-ecosystem-audibility/artifacts/before_cpu.log 2>&1`,
alone, machine idle and cooled. **Verify:** the log contains `VoragoEngine_CpuBudget`'s WARN block with
both the "clause (i) ceiling" and "clause (ii) baseline" lines. If it fails: confirm nothing else ran,
let the machine idle, re-run once alone; never relax a budget.

### T015 — Lane survey (SC-020 before, §3.4 input)
`dsp_systems_tests.exe "VoragoVoice_EcosystemLaneSurvey" > artifacts/lane_survey.log 2>&1`.
**Verify:** the log has the three kinds' quantiles, addressed counts, before-fractions at bases
0.50 / 0.50 / 0.35 and the rule's proposed `kPeakWakeBase` / `kLoopWakeBase`.
**Stop-and-surface:** a `SURVEY STOP` line (Q(0.60) < 0.05) — SC-020 is then at risk before any code
change (plan §8).

---

## Group 7 — Lever test cases, part 1: inert-refactor contracts (sequential; one file)

All tasks in Groups 7–8 edit `dsp/tests/unit/systems/vorago_ecosystem_lever_test.cpp`. Tags
`[systems][vorago]`. Target `dsp_systems_tests`.

### T016 — Friend lever accessors + `VoragoVoice_EcosystemLeverBaseReadBack` (SC-009, FR-021)
- **Write first** (fails to compile: the members do not exist until T020):
  - Friend gains: `injectEco(VoragoVoice&, const Lanes&)` (sets `v.injectedEco_ = eco; v.ecoInjectionActive_ = true;`),
    `clearInjection(VoragoVoice&)`, `injectEcoAll(VoragoEngine&, const Lanes&)` (every `voices_[v]`),
    `noiseLevelBase(v)` → `v.noiseLevelBaseDb_`, `loopGainBase(v)` → `v.loopGainBase_`,
    `shapeLeverInput(float, float)` → `VoragoVoice::shapeLeverInput`, and
    `schedLanes(const VoragoVoice&, Lanes&)` per plan §5.1: zero `out`; for each scheduler k with
    `v.scheduler(k).isEventActive()`, family `< kNumEventFamilies` and `!= BloomTrigger`, write
    `out[kindForFamily(family)][min(v.drawnSlot_[k], kMaxSlotsPerKind - 1)] = max(existing, max(0.0f, getCurrentValue()))`.
    Valid only immediately after `advanceLifeOnly` (comment it).
  - `TEST_CASE("VoragoVoice_EcosystemLeverBaseReadBack", "[systems][vorago]")`:
    after `prepare(48000)`: `REQUIRE(v.getNoiseLevelDb() == -18.0f)`,
    `REQUIRE(v.getEcologyLoopGain() == FeedbackEcology::kDefaultLoopGain)` (0.72).
    `noteOn`, inject L = 1 on every slot of every kind, advance 16 chunks. For dB ∈ {−30, −12, 0}:
    `setNoiseLevelDb(dB)`, REQUIRE `getNoiseLevelDb() == dB` exactly, and after one more chunk REQUIRE
    `noise().getSourceLevel(s) == std::clamp(dB + v.kNoiseLevelLeverSpanDb * 1.0f, -96.0f, 12.0f)` for every
    source. For g ∈ {0.5, 0.8}: `setEcologyLoopGain(g)`, REQUIRE `getEcologyLoopGain() == g`, component
    `getLoopGain(l) == clamp(g + v.kLoopGainLeverSpan, kMinLoopGain, kMaxLoopGain)`.
    A non-finite write (bit pattern `0x7FC00000` via `std::bit_cast`, volatile) leaves the getter unchanged.
    Writing the same value twice leaves `getSourceLevel(s)` unchanged (E-7 early-out).
    (Component getters reach the components through the voice's public const accessors; confirm each
    accessor name in `vorago_voice.h` before use — do not invent one.)

### T017 — `VoragoVoice_EcosystemLeverReset` (SC-012, FR-022, E-11, E-13)
- **Write first:** Voice A: prepare 48 kHz, noteOn, inject L = 1, advance 200 chunks. For each clearing
  path — `reset()`; `silence()` + `resetForSteal()`; `resetForRecovery()`; `prepare(96000)` then
  `prepare(48000)` — compare against a freshly prepared voice B (same config and seed): every
  `getSourceLevel(s)`, `getPeakLevel(p)`, `getLoopGain(l)`, every wake getter, every
  `noiseLevelOffsetDb_[s]` / `loopGainOffset_[l]` (via friend) REQUIRE `==`. Then `clearInjection(A)`,
  `setEcosystemDepth(0)` on both, noteOn both, render 1 s each via `processStereoBlock(…, 64)`;
  `REQUIRE(compareFingerprints(fingerprintRender(a), fingerprintRender(b)).withinTolerance())` for L and R.

### T018 — `VoragoVoice_EcosystemLeverLifeOnlyParity` (FR-022)
- **Write first:** two voices, same seed, `setEcosystemDepth(1.0f)`, noteOn. A renders 30 s via
  `processStereoBlock(…, 64)`; B advances via `advanceLifeOnly` chunk-for-chunk. After every chunk,
  REQUIRE exact equality of every lever destination getter and the three wake getters
  (`getSourceWakeAmount`, `getPeakWakeAmount`, `getLoopWakeAmount`).

### T019 — `VoragoVoice_EcosystemLeverMapping`, `…Neutral`, `…SchedulerBlind`, `…LaneShapingFidelity`
- **Write first** (plan §5.2 rows, verbatim criteria):
  - **Mapping** (FR-015, FR-017, FR-019, FR-020): prepare 48 kHz, noteOn, `setEventRateScale(10.0f)`. For
    uniform injected `L ∈ {0.0f, 0.25f, 0.5f, 1.0f}`, 10 min of `advanceLifeOnly` (`600*48000/64` chunks).
    After every chunk read `S = schedLanes(v)` and REQUIRE, **exact**:
    `getSourceLevel(s) == std::clamp(noiseLevelBase + kNoiseLevelLeverSpanDb * L, -96.0f, 12.0f)`;
    `getPeakLevel(p) == std::clamp(kPeakLevelBaseDb + kPeakLevelLeverSpanDb * L, kMinPeakLevelDb, kMaxPeakLevelDb)`;
    `getLoopGain(l) == std::clamp(loopGainBase + kLoopGainLeverSpan * L, kMinLoopGain, kMaxLoopGain)` —
    each expected value computed with the **same float expression** as production (including
    `shapeLeverInput(L, kLeverInputGain[k])`); and each wake getter `== VoragoVoice::combineWake(base, L, S[k][slot])`.
    Non-vacuity: REQUIRE ≥ 1 chunk per L with `S[k][slot] > L` on some wake kind and ≥ 1 with `S` all zero;
    print both counts.
  - **Neutral** (SC-006, SC-018): for fs ∈ {44100, 48000, 96000}: `setEcosystemDepth(0.0f)`, schedulers
    as shipped, noteOn, 10 min (`fs*600/64` chunks) of `advanceLifeOnly`; after every chunk REQUIRE
    `getSourceLevel(s) == noiseLevelBase`, `getPeakLevel(p) == kPeakLevelBaseDb`,
    `getLoopGain(l) == loopGainBase`. REQUIRE ≥ 1 scheduler event seen (print count).
  - **SchedulerBlind** (SC-007): depth 0, `setEventRateScale(10.0f)`, fixed printed seed, 10 min
    `advanceLifeOnly`. Count onset edges per family (changes of `lastEventTarget(v,k)` and rising edges of
    `scheduler(k).isEventActive()`). **REQUIRE NoiseWake ≥ 1, PeakWake ≥ 1, LoopWake ≥ 1** (fail, never
    skip), then the Neutral assertions on every chunk. Print per-family counts.
  - **LaneShapingFidelity** (SC-021): `if` every `v.kLeverInputGain[k] == 1.0f` → `SUCCEED("shaping not used")`.
    Otherwise for each shaped kind (Noise, Resonator, Feedback only): `shapeLeverInput(0.0f, g) == 0.0f`;
    output ∈ [0, 1] for 10 001 evenly spaced inputs on [0, 1]; inject `L_raw` on every kind, advance one
    chunk, `S = schedLanes`; wake getters `== combineWake(base, L_raw, S[k][slot])` (raw lane); lever
    `== clamp(base + span * shapeLeverInput(L_raw, g))`; `cloud().getMutation() == clamp(mutationBase_ + L_raw, 0, 1)`
    and `bloom().getDepth() == clamp(bloomDepthBase_ + L_raw, 0, 1)` (Partial unshaped, FR-016).

---

## Group 8 — Production: inert lever refactor (sequential; `vorago_voice.h`)

### T020 — Implement plan §2.1–§2.5 in `VoragoVoice` at **no-change values**
- **File (edit):** `dsp/include/krate/dsp/systems/vorago_voice.h` only (Layer 3; no new include).
- **Tests that must go from not-compiling to green:** T016–T019 cases.
- **Implement:**
  - §2.1 private constants: `kPeakLevelBaseDb = -9.0f`; `kNoiseLevelLeverSpanDb = 0.0f`,
    `kPeakLevelLeverSpanDb = 0.0f`, `kLoopGainLeverSpan = 0.0f` (**spans 0 at this step**, plan §7 step 3);
    `kLeverInputGain{1.0f, 1.0f, 1.0f, 1.0f, 1.0f}` with the Partial/Ghost `static_assert`;
    `kMinRetunedWakeBase = 0.05f`; `kPeakWakeBase = 0.50f`, `kLoopWakeBase = 0.50f` with their
    `static_assert(… >= kMinRetunedWakeBase)`.
  - §2.2 members after `:2376`: `noiseLevelBaseDb_ = -18.0f`, `loopGainBase_ = FeedbackEcology::kDefaultLoopGain`,
    `noiseLevelOffsetDb_{}`, `loopGainOffset_{}`, `injectedEco_{}`, `ecoInjectionActive_ = false`.
  - §2.3: replace `setNoiseLevelDb` (`:1236-1257`) and `getNoiseLevelDb` (`:1258`),
    `setEcologyLoopGain` (`:1321-1326`) and `getEcologyLoopGain` (`:1327`) with the shadow versions; add
    private `writeNoiseLevel(s)` / `writeLoopGain(l)` that early-out against the **component** getter.
    Prepare step 5: `resonance_.setPeakLevel(p, kPeakLevelBaseDb)`, `peakWakeBase_.fill(kPeakWakeBase)`,
    `loopWakeBase_.fill(kLoopWakeBase)`, offsets zero-filled; the freq-wander (`:582`) and coupling
    (`:596`) literals stay.
  - §2.4: private static `shapeLeverInput(float lane, float gain)` = `std::clamp(gain * lane, 0.0f, 1.0f)`;
    append the Noise / Resonator / Feedback lever loops at the end of `applyIdentityLanes`, reading only
    `lanes.eco`; Resonator write early-outs against `resonance_.getPeakLevel(p)`. Partial, Ghost and the
    three `combineWake` calls are untouched.
  - §2.5: `publishIdentity` substitutes `injectedEco_` for `gatherEcosystemLanes` when
    `ecoInjectionActive_`; `gatherSchedulerLanes` still runs.
- **Stop-and-surface:** the `static_assert` at `:2428` fires (do not raise `kVoiceSizeBound`).
- **Verify:** build `dsp_systems_tests vorago_tests`, **0 warnings**.
  `dsp_systems_tests.exe "VoragoVoice_EcosystemLever*"` green;
  `dsp_systems_tests.exe "~[performance]~[perf]~[benchmark]~[!benchmark]~[long]" | tail -5` and
  `vorago_tests.exe "~[performance]~[perf]~[benchmark]~[!benchmark]~[long]" | tail -5` all green
  (the refactor is inert: spans 0, bases unchanged). No existing test file edited.

---

## Group 9 — Lever test cases, part 2: tuned-lever contracts (sequential; one file)

These cases are written against the spans-0 tree of T020 and **must fail there** where noted; T023 makes
them pass.

### T021 — `VoragoVoice_EcosystemLeverAttribution` (SC-008, SC-018) and `VoragoVoice_EcosystemWakeUnmasked` (SC-020)
- **Write first:**
  - **Attribution:** per fs ∈ {44100, 48000, 96000}: depth 1, noteOn, 155 s warm-up by `advanceLifeOnly`,
    then for K ∈ {Noise, Resonator, Feedback} one minute each. (1) Over the minute before K, count per lever
    the value changes ≥ min step — level ≥ 0.5 dB, loop gain ≥ 0.01 (coupling ≥ 0.01, wander ≥ 0.1 st only
    if §2.8 shipped them); **REQUIRE ≥ 3** for every kind's lever. (2) `setAgentDormant(i, true)` for every
    agent of kind K (`ecosystem(v)`); `T_settle = (rampSteps + 1)·dt + T_lever` with
    `dt = getStepIntervalChunks()*64/fs`, `rampSteps = max(1, ceil(0.050/dt))`, `T_lever = 0` for
    level/gain (print `T_settle`); after `T_settle` REQUIRE K's lever `==` base on every chunk to minute end,
    while every other kind still reaches ≥ 3 changes in that minute. Un-dormant K, continue.
    **Expected red on the T020 tree** (spans 0 → 0 changes).
  - **WakeUnmasked:** 48 kHz, §3.4 seed and stimulus, depth 0.85, `advanceLifeOnly` to 340 s. On every
    simulation step in [155, 340] s over addressed slots, fraction with `gatherEco > shipped base`
    (`peakWakeBase`/`loopWakeBase`/`noiseWakeBase`). **Print** the three bases and three fractions first,
    then **REQUIRE Resonator ≥ 0.25 and Feedback ≥ 0.25**; Noise printed only (Q1).
    Expected red on the T020 tree only if the survey's before-fractions (T015) are < 0.25.

### T022 — `VoragoVoice_EcosystemLeverClickFree`, `VoragoEngine_EcosystemLeverBounded`, `VoragoEngine_EcosystemLeverDeterminism`
- **Write first:**
  - **ClickFree** (SC-010(a)): 48 kHz, fixed seed, one voice,
    `ClickDetectorConfig{.sampleRate = 48000.0f, .frameSize = 512, .hopSize = 256, .detectionThreshold = 5.0f, .energyThresholdDb = -60.0f, .mergeGap = 5}`.
    20 s warm-up with fast attack (as `applyFastAttack` does in the existing Vorago tests — reuse, do not
    re-invent). Schedule per K in {Noise, Resonator, Feedback, Partial, Ghost} one at a time, then all:
    1 s at 0, 1 s at 1 on every slot of K, 1 s at 0; render via `processStereoBlock`. Twin: same seed and
    schedule with injection held at 0. **REQUIRE clicks(stress) ≤ clicks(twin)** for L and R separately;
    print both.
  - **Bounded** (SC-011, SC-018, E-4, E-5): engine at `setPolyphony(VoragoEngine::kMaxVoices)` (6), notes
    36, 43, 48, 55, 60, 67, a `VoragoMacroMatrix` with Life = 1 applied every block (the
    `vorago_macro_test.cpp` pattern), chain `processStereoBlock` → `processOutputStage` (no cavern: a
    systems TU may not name a Layer 4 type). Arm (a) `injectEcoAll` all 1; arm (b) natural colony. Before
    each arm REQUIRE `isRendering(e, v)` for all 6. Each 60 s at fs ∈ {44100, 48000, 96000}: REQUIRE every
    sample `detail::isFinite` and `|out| <= 0.9661f`, `getNonFiniteRecoveryCount() == 0`,
    `getAllocatedBytes()` equal before/after. At 48 kHz only, a GR twin (`setLimiterCeilingDb(+60)`, §4.4
    formula) **prints** max reduction for (a) and (b). **Not** `[long]` (bounds sentinel). Print the
    case's wall-clock; **ruling R-2:** if it exceeds 4 minutes measured locally (alone), STOP and surface
    the figure — do not shorten, split or retag it.
  - **Determinism** (SC-013, FR-025): copy the `VoragoEngine_DeterminismHarness` shape
    (`vorago_engine_test.cpp:1498-1560`; do not edit that file) with `setEcosystemDepth(1.0f)` on every
    voice. Seed A twice: `compareFingerprints(…).withinTolerance()`; seed A vs A+1: worst metric
    `> 100 * kMetricTolerance`.
- **Verify on the T020 tree:** build, 0 warnings; Bounded and Determinism pass (spans 0 are trivially
  bounded); ClickFree passes; Attribution is red (expected, recorded).

---

## Group 10 — L1 + L2: retune and initial spans (sequential; `vorago_voice.h`)

### T023 — Set the §3.4 wake bases and the §2.1 initial spans
- **File (edit):** `dsp/include/krate/dsp/systems/vorago_voice.h` (constants only).
- **Tests that must go green:** T021 Attribution and WakeUnmasked, plus every T016–T022 case.
- **Implement:** `kPeakWakeBase` / `kLoopWakeBase` = the values the §3.4 rule printed in
  `artifacts/lane_survey.log` (cite the log lines in the constant's comment); `kNoiseLevelLeverSpanDb = 9.0f`,
  `kPeakLevelLeverSpanDb = 12.0f`, `kLoopGainLeverSpan = 0.15f`.
- **Verify:** build, 0 warnings; `dsp_systems_tests.exe "VoragoVoice_EcosystemLever*"`,
  `"VoragoEngine_EcosystemLever*"`, `"VoragoVoice_EcosystemWakeUnmasked"` green. If Attribution's ≥ 3
  changes/min fails for a lever, that is a finding against the spans (go to the §3.5 ladder, T026), never a
  test edit.

### T024 — `VoragoVoice_EcosystemLeverClickFreeNatural` (SC-010(b), `[long]`)
- **File (edit):** `dsp/tests/unit/systems/vorago_ecosystem_lever_longrun_test.cpp` (public API only).
- **Write:** `TEST_CASE("VoragoVoice_EcosystemLeverClickFreeNatural", "[systems][vorago][long]")`: 48 kHz,
  one voice, depth 1, fast attack, 10 min via `processStereoBlock(…, 64)`. Record the sample index of every
  chunk where `ecosystem().getControlStepCount()` incremented. `ClickDetector` (T022's pinned config) on L
  and R. **REQUIRE zero detections within ±64 samples of any boundary** (Q7). Print detections elsewhere
  with times, and a depth-0 twin's count beside them (record only).
- **Verify:** build, 0 warnings; `dsp_systems_tests.exe "VoragoVoice_EcosystemLeverClickFreeNatural"`
  green (qualifies for `[long]`: >15 s, failure not toolchain-specific).

### T025 — Full per-push regression on the L2 tree, then the Gate-1 pair and SC-005
- **Do:** `dsp_systems_tests` and `vorago_tests` with the CI exclusions → green. Classify any failure per
  FR-031: (a) behavioural bound → fix the code; (b) old voicing encoded as data → **stop and surface**
  with the test name and reason (watch-list: `vorago_macro_test.cpp` macro-axis deltas,
  `vorago_engine_longrun_test.cpp` SC-005 centroid-CV soak, `processor_audio_test.cpp`, `soak_test.cpp`).
  Then, alone, one after another:
  `VORAGO_PROBE_KNOBS=- VORAGO_PROBE_REF=off VORAGO_PROBE_GR=1 … > artifacts/L2_gate1_default.log` and
  `VORAGO_PROBE_KNOBS=- VORAGO_PROBE_REF=off VORAGO_PROBE_SURFACE=109=1 … > artifacts/L2_gate1_lifemax.log`.
- **Read from the logs:** both `GATE1M … ratio` (target ≥ f = 0.5, ruled 2026-09-28); SC-005: default M1 RMS within ±3 dB of
  `before_gate1_default.log`'s value and `GR max ≤ 1.0` dB.

---

## Group 11 — Tuning ladder (sequential; conditional; plan §3.5)

### T026 — Climb the §3.5 ladder only as far as needed
- **Entry condition:** T025 shows either Gate-1 arm `< 2.0`, or SC-005 out of bounds. If both Gate-1 arms
  pass and SC-005 holds, **skip to Group 12** and record "ladder stopped at L2".
- **Rungs, in order, each kept only if needed.** After each rung: rebuild (0 warnings), re-run
  `dsp_systems_tests.exe "VoragoVoice_EcosystemLever*" "VoragoEngine_EcosystemLever*"
  "VoragoVoice_EcosystemWakeUnmasked"`, then the Gate-1 pair + GR as in T025 into
  `artifacts/L<n>_gate1_{default,lifemax}.log`. Record each rung's constants before/after.
  - **L3** loudness: if default M1 RMS is outside ±1.5 dB of the before value, move `kPeakLevelBaseDb`
    opposite in 1 dB steps within [−18, −6].
  - **L4** raise spans (noise ≤ +12 dB, peak ≤ +18 dB, loop gain ≤ +0.18); then, if still needed, the §2.8
    edit for the kept ladder lever(s) only: ring coupling (`kRingCouplingBase = 0.12f` replacing `:596`,
    `kCouplingLeverSpan ≤ 0.30`, `(l, (l+1)%n)` pair only, Q8) and/or voice-slewed freq wander
    (`kFreqWanderBaseSemis = 1.5f` replacing `:582`, `kFreqWanderLeverSpanSemis ≤ 6`,
    `kFreqWanderLeverSlewSeconds = 0.050f`, `freqWanderApplied_`, `freqWanderSlewPerChunk_` derived in
    prepare step 7, snapped in `installIdentityNeutral`). When a ladder lever ships, extend
    Mapping/Neutral/Reset/Attribution/BaseReadBack assertions to it (coupling min step 0.01, wander
    0.1 st, `T_lever` = the slew time rounded up to whole chunks) and re-run `…Bounded` after each change.
  - **L5** lower `kRingCouplingBase` / `kFreqWanderBaseSemis` (only for a lever L4 added).
  - **L6** `kLeverInputGain[k] ∈ {1.5, 2, 3}` for Noise/Resonator/Feedback only; `…LaneShapingFidelity`
    becomes active and must pass. **Ruled 2026-09-28: L6 runs BEFORE L4 step 2** (it targets the measured
    cause); from L4 on, Gate 1 is the multi-seed `GATE1M` line (`VORAGO_PROBE_SEEDS=6 VORAGO_PROBE_KNOBS=-
    VORAGO_PROBE_REF=off`, default surface per rung, Life max at the end), and the threshold is ruled on the
    data after the ladder (spec Clarifications "Build stage").
  - **L7** voice-owned rungs exhausted → **L8** (ruling R-3): lower the noise wake base default —
    parameter 301 in `noise_params.h` (registration default and the pack default), the macro base in
    `param_routes.h`, and `param_table_expected.h` — as a recorded ruling in `compliance.md`; re-run
    Gate 1, SC-005 and SC-020's Noise arm. If L8 also fails, stop and surface the measured table.
- **Never:** change a threshold, candidate, range, a macro-row base, or any file outside
  `vorago_voice.h` / `vorago_engine.h` production — except the L8 files above, and only at L8.
- **Verify:** final rung's logs show both `GATE1 verdict=PASS` and SC-005 in bounds.
- **Main-loop note (2026-09-28, FR-018b):** the wander lever's span 6 → 3 st zipper fix left Phase 10
  SC-008 (`VoragoMacro_SweepAxes`) at rho 0.867 for every base/span pair under the zipper ceiling
  (`artifacts/base15_sweepaxes.log`, `cand_*_sweepaxes.log`). Rung added: the span is scaled by
  (0.03 Hz / resonance wander rate)^0.75 above the default rate (`wanderLeverRateComp`; k = 1 flattened
  the sweep's endpoint to +19 %, k = 0.5 inverted the mean's top pair, k = 0.75 reads rho 1.000 / +21 %), verified by
  `VoragoVoice_EcosystemLeverRateCompensation` (SC-022) and by re-running `VoragoMacro_NoZipper` and
  `VoragoMacro_SweepAxes` (`artifacts/ratecomp_*.log`). Gate 1 / Gate 2 surfaces run at 0.03 Hz where the
  factor is 1.

---

## Group 12 — Gate 2 and knob tables (sequential; long runs, alone)

### T027 — After tables on the Gate-1-passing tree
**Main-loop note (2026-09-28):** the ladder (T026) and these tables were run by the main loop; the logs `artifacts/after_table_default.log` (t0 pinned 4.0438) and `artifacts/after_table_lifemax.log` (t0 pinned 4.0656) are already produced by the main loop. An agent verifies them against the clauses below and transcribes; it does NOT re-run them.
`vorago_tests.exe "Vorago_EcosystemRuleProbe" > artifacts/after_table_default.log 2>&1`, then
`VORAGO_PROBE_SURFACE=109=1 … > artifacts/after_table_lifemax.log 2>&1`.
- **Verify (ruled 2026-09-28):** a knob counts on EITHER surface — the union of the counted knobs of the default and
  Life-max tables is ≥ 4 (the default log alone printed `GATE2 verdict=FAIL (N >= 4)` at 2; the union is 4:
  moveRate, leakRate, syncRate, freqDrift at 6 st). **Final tree (wander span 3 st after the SC-010 zipper fix, ruled
  2026-09-28): union = 2 (syncRate, selfAffinity) — Gate 2 recorded as UNMET (2 of 4); logs `final_table_*.log`.** each counted knob row shows `d/t0 ≥ 0.5` (f), all three
  `|ΔRMS[m]| ≤ 6` and `rms[m] ≥ −60`, `dOff/t0 ≥ 0.25` (f/2); t0 pinned with `VORAGO_PROBE_T0=<GATE1M median_t0off>`. Record `git rev-parse HEAD`-equivalent tree state
  (same as the Gate-1 logs; FR-014).
- **If Gate 2 fails with Gate 1 green:** re-enter T026 at L4–L6 aimed at statistic sensitivity, re-run
  Gate 1 + SC-005, then T027. If it still fails: **stop and surface**.

### T028 — Final Gate-1 logs on the shipped tree
**Main-loop note (2026-09-28):** `artifacts/after_gate1m_default.log` and `artifacts/after_gate1m_lifemax.log` (six-seed `GATE1M`, f = 0.5) plus `artifacts/L5_gate1_default.log` (SC-005 guard, GR twin) are produced by the main loop; the fingerprint re-pin below is DONE by the main loop (`artifacts/t028_ghost_fingerprint_{before_repin,before_repin_run2,after_repin}.log`, fourth-harvest PROVENANCE in `atmosphere_ghost_fixtures.h`). Verify only.
Re-run the T025 pair on the final tree into `artifacts/after_gate1_default.log` (with GR) and
`artifacts/after_gate1_lifemax.log` **only if** the tree changed after the last `L<n>` Gate-1 logs.
If it did not change, cite the `L<n>` logs directly and do not copy them under a new name. **Verify:** both `GATE1 verdict=PASS`,
SC-005 bounds, `DESCRIPTOR default` present for the FR-030 before/after table.

**FR-031(b) re-pin (user ruling, 2026-09-28, surfaced at T025):** on this final tree, and only here,
re-measure `kBaseCommitVoragoFingerprint` (`dsp/tests/unit/systems/atmosphere_ghost_fixtures.h` §6.2,
consumed by `VoragoEngine_GhostExtensionWiring` clause (a), `vorago_ghost_ext_test.cpp:458/507`):
run the case, paste the literal it prints, and refill its PROVENANCE block naming this phase, the
shipped spans/bases and the log. Until this step the case is an **expected red** (FR-031(b): it pins
the pre-13b default voicing as data) and every earlier regression run cites it as such. Re-pin ONCE;
if the tree changes again afterwards, repeat only then.

---

## Group 13 — Integration and evidence (sequential; last)

### T029 — `[long]` suite
`dsp_systems_tests.exe "[long]" > artifacts/after_long.log 2>&1` (includes
`VoragoVoice_EcosystemLeverClickFreeNatural`, Phase 10 `VoragoVoice_EcosystemRouting`, SC-004b and the
overnight-soak equivalent) and `vorago_tests.exe "[long]" > artifacts/after_vorago_long.log 2>&1`.
**Verify:** all passed; no test file edited except any listed under FR-031's surfaced list.

### T030 — After CPU (alone, idle ≥ 15 min, cooled)
**Amended by the main loop (2026-09-27) after T014:** the runner's filter includes `[long]`, so
`node tools/run-cpu-tests.js dsp_systems_tests` re-runs the multi-hour `[long]` roster T029 has just
run (T014's run took 5 h and ended on a hot machine with a false red — see
`artifacts/t014_cpu_resolution.md`). Run the perf roster alone through the same pinned path, without
`[long]`:
`pwsh -NoProfile -File tools/pin-perf-cores.ps1 -Exe build/windows-x64-release/bin/Release/dsp_systems_tests.exe -ExeArgs "[performance],[perf],[.perf],[benchmark],[!benchmark]" > artifacts/after_cpu.log 2>&1`
(one process, nothing else executing, machine idle ≥ 15 min and not straight after T029). **Verify:**
`VoragoEngine_CpuBudget` passes both clauses (i) engine + `kCavernMeasuredNsPerBlock` ≤ 3 200 000 ns and
(ii) engine ≤ `kEngineBaselineNsAtPoly4` × 1.5; compute the delta vs the **isolated before figure**
(`artifacts/before_cpu_isolated.log`: engine 2.75016e+06, with Cavern 2.87466e+06 ns/block — never
`before_cpu.log`'s hot-lane figures) in ns and %. `VoragoVoice_CompositionOverhead` gates on
whole / parts-back-to-back ≤ 1.15 (commit `e0beed68`). Never relax a budget; a failure is re-run once
alone after idling before it is treated as a defect.

### T031 — Full regression suite (SC-014)
Build every target (`dsp_core_tests dsp_primitives_tests dsp_processors_tests dsp_systems_tests
dsp_effects_tests vorago_tests seraphis_tests shared_tests`), **0 warnings**. Run each exe directly, CI
exclusions applied, logs to `artifacts/after_suite_<target>.log`. **Verify:** 100 % pass, including
`VoragoVoice_WakeCombineRule`, `VoragoVoice_AgentReductionRule`, `param_table_test`, `state_v2_test`,
`state_roundtrip_test` unedited. Classify failures as in T025.

### T032 — Scope and surface check (SC-015, SC-017, FR-026–FR-028)
`git diff --name-only <phase-base>..` plus the working tree. **Verify:** production changes only in
`vorago_voice.h` and `vorago_engine.h` (the latter: one friend line); no `plugins/seraphis/**`, no
`plugins/vorago/src/**`, no `harmonic_cloud.h`, `atmosphere_engine.h`, `continuous_body.h`,
`aether_reverb.h`, `entropy_processor.h`, life-modulator or `seraphis_*.h`, no `noise_organism.h`,
`resonance_drift_network.h`, `feedback_ecology.h`, `bloom_engine.h`, `ecosystem_engine.h`,
`vorago_macro_matrix.h`; `kCurrentStateVersion == 2`. Save to `artifacts/scope_diff.log`.

### T033 — ODR re-sweep and size bound (FR-024)
For every name added to the tree in this phase, `grep -rn "<Name>" dsp/ plugins/ tests/` shows only
the intended definitions/uses; `detail::VoragoEcosystemLeverProbe` is **defined** in exactly one TU
(`grep -rn "struct VoragoEcosystemLeverProbe {" dsp plugins tests` → 1 hit). The `kVoiceSizeBound`
`static_assert` compiles unchanged.

### T034 — clang-tidy
`./tools/run-clang-tidy.ps1 -Target dsp -BuildDir build/windows-ninja` and `-Target vorago` → 0 findings
(fix every one; none is "pre-existing"). Logs to `artifacts/`.

### T035 — pluginval
`tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Vorago.vst3" >
artifacts/pluginval.log 2>&1` → passes (the plugin binary changes with the voice).

### T036 — Portability
`node tools/check-portability.js > artifacts/portability.log 2>&1` → clean. Afterwards run
`wsl --shutdown` (the check boots WSL). No `std::isnan`, no narrowing brace-init, no new SIMD.

### T037 — CMake registration re-check (closes T005)
`dsp_systems_tests.exe --list-tests "VoragoVoice_EcosystemLever*"`, `"VoragoEngine_EcosystemLever*"`,
`"VoragoVoice_EcosystemWakeUnmasked"`, `"[.probe]"` and `"[long]"` list every case written in T003,
T016–T019, T021, T022, T024 (and `…LaneShapingFidelity`); `dsp/tests/CMakeLists.txt` shows the two TUs
enumerated exactly once.

---

## Dependency summary

```
G0 T001 → G1 T002 → G2 {T003 ∥ T004} → G3 T005 → G4 T006→T007→T008→T009 → G5 T010
→ G6 T011→T012→T013→T014→T015 → G7 T016→T017→T018→T019 → G8 T020
→ G9 T021→T022 → G10 T023→T024→T025 → G11 T026 (conditional) → G12 T027→T028
→ G13 T029→…→T037
```

## Traceability (FR/SC → task)

| ID | Task(s) |
|---|---|
| FR-001(a)/(b) | T011–T015 |
| FR-002, FR-003, FR-006 | T006–T009 (kCandidates untouched) |
| FR-004, FR-005 | T006, T009 |
| FR-007 | T007 |
| FR-010, FR-011, SC-001, SC-002 | T025/T026/T028 |
| FR-012–FR-014, SC-003, SC-004 | T009, T013, T027 |
| FR-015, FR-017, FR-019, FR-020 | T019 (Mapping), T020, T023 |
| FR-016 | T019 (LaneShapingFidelity Partial check), T020 `static_assert` |
| FR-018, SC-020 | T015, T021 (WakeUnmasked), T023 |
| FR-018a | T026 (L3, L5) |
| FR-018b, SC-022 | T026 (main-loop rung, rate compensation) |
| FR-019a, SC-021 | T019, T026 (L6) |
| FR-021, SC-009 | T016, T020 |
| FR-022, SC-012 | T017, T018, T020 |
| FR-023, SC-010, SC-011 | T020 (seam), T022, T024, T029 |
| FR-024 | T020, T033 |
| FR-025, SC-013 | T022 (Determinism) |
| FR-026–FR-028, SC-015, SC-017 | T032, T031 |
| FR-030, SC-005 | T008, T009, T011, T025, T028 |
| FR-031, SC-014 | T025, T029, T031 |
| FR-032, SC-016 | T014, T030 |
| FR-033, SC-019 | T034, T035, T036 |
| FR-034 | T009 |
| SC-006, SC-007, SC-018 | T019, T021, T022 |
| SC-008 | T021 |
