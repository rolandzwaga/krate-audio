# Tasks: Vorago Phase 14 — Factory Presets & Release Readiness (second pass)

**Spec:** `specs/vorago-phase14-presets-release/spec.md` (second specify pass; Clarifications 2026-09-27 Q1–Q8 and
2026-09-29 Q1–Q8; plan-stage rulings R-1…R-8) · **Plan:** `specs/vorago-phase14-presets-release/plan.md` (second
pass, 2026-09-29). Both are normative; "§n" below is a plan section unless marked "spec".
**Roadmap:** `specs/Vorago-roadmap.md` Part B, Phase 14 (lines 615–645) and the Phase 13b hand-off (lines 598–610).
**Branch:** `feat/vorago-phase1-events-modulation` (one branch per roadmap; never renamed).
**Date:** 2026-09-29 · **Status:** TASKS — supersedes the first-pass task list of 2026-09-27.
**Phase base commit:** `339cd501` (the planning HEAD; every "git diff since base" below uses it).

---

## 0. Already done — do NOT redo (first pass, kept per spec status line and roadmap line 620)

| Artifact | Where (read 2026-09-29) |
|---|---|
| Audibility probe TU `Vorago_EcosystemRuleProbe` `[.probe][vorago]` | `plugins/vorago/tests/integration/ecosystem_rule_probe_test.cpp` — stays **unchanged** (FR-070) |
| Probe registration + fast-math exemption | `plugins/vorago/tests/CMakeLists.txt:52` and `:137` |
| `preset_test_support.h` part 0 (`PresetDescriptor`, `describe`, `descriptorDistance`, `meanOf`; namespace `VoragoTest`; Catch2-free) | `plugins/vorago/tests/preset_test_support.h:37-180` |
| Inert probe friends `detail::VoragoEcosystemRuleProbe` | `vorago_voice.h:171`, `:1568`; `vorago_engine.h:1304` |
| Gate G1 (roster ratification, FR-071) | **Discharged**: `R` = {`syncRate`, `selfAffinity`} (spec Clarifications 2026-09-29 Q1); IDs 901/902; `kStateV3Bytes = 436`; `kFieldCount = 33`; cells `E6.hi`, `E7.hi` only; N = 40 |

The probe runs already in `compliance.md` (FR-070 runs 1–4) are first-pass history; tasks below append new
sections to that file and never rewrite those.

## 1. How to read this file

- Tasks `T001…T063` sit in **groups that run strictly in order**. Inside a group, tasks tagged **[P]** touch fully
  disjoint files, each either brand new or a skeleton TU created by T003 and owned by that one task within the
  group; they may run in parallel. A task that edits an existing file, or a file several tasks of this phase edit
  (a CMake list, an existing header, `preset_test_support.h`, `vorago_preset_host.h`, `tools/vorago_preset_defs.h`,
  `factory_preset_test.cpp`, `preset_sweep_test.cpp`, …), is alone in its own group.
- **Canonical order in every code task:** write the failing test first (file, `TEST_CASE` name, tags, exact
  assertions) → confirm it fails (compile error or red assertion) → implement → zero warnings → the named tests
  pass. Where a behaviour already ships (T005, FR-061) the test is an *enforcing* test expected green at once; a
  red result there is an FR-017 finding, never a reason to edit `dsp/`.
- **Stops (never skipped, never worked around):**
  - **T001** — plan §12's six items are ruled before any code (plan "Open questions … before C1").
  - **T008 (E0)** — `2·t_8(default) > 4.0` → FR-017a STOP: surface the curve, author nothing (plan P2-1).
  - **T039 (G2)** — rule K; any pilot failure is an FR-017 STOP.
  - **T043** — the measured default-state set must keep `|requiredPrimaryCells()| == 40`, else FR-017 STOP.
  - **T050** — the FR-042 listening checkpoint is the user's.
  - **FR-017 anywhere:** a cell no authorable preset verifies, a failed C-7.3 control, a failed boundedness arm, a
    CPU breach, a near-variant pair at `d ≥ 4.0`, an E-ext candidate below the primary bar → stop and report with
    the measurements. Never relax a predicate, floor, control, window, bar or budget; never drop a
    cell/arm/preset/sample rate; never raise F above 4.0 or K above 8; never edit `dsp/` beyond T010's R-1 append;
    never touch the colony levers or wake bases (Phase 13b owns them, spec Non-goals).
- **CMake registration (FR-027a, ruling R-7(i)).** `plugins/vorago/tests/CMakeLists.txt` is ENUMERATED, never
  globbed (`:5-6`: "an unregistered TU silently drops out of the build"). The probe TU was registered before G1
  (`:52`). **T003 is the ONE mid-phase registration task**: every other new test TU (as a compiling skeleton) and
  the generator targets. Registration cannot wait for the last group because E0 (T008) and the pilot (T039) run
  code mid-phase. **The final group's T060 only audits** — it adds nothing. This is the spec's ruled structure and
  deliberately differs from a "register everything last" layout.
- **Build (Windows, always the full CMake path):**
  `"C:/Program Files/CMake/bin/cmake.exe" --build build/windows-x64-release --config Release --target <t>`.
  Run a suite directly: `build/windows-x64-release/bin/Release/<t>.exe "<TestName>" 2>&1 | tail -5` (positional
  name/tag filter; never `ctest -R`). Any run over ~1 min goes to a log under `f:/tmp/p14/`, and the log is read;
  a slow suite is never re-run just to see its output.
- **Per-push filter** (CI per push, `.github/workflows/ci.yml:374`): `"~[performance]~[perf]~[benchmark]~[!benchmark]~[long]"`.
  From T041 on (the first `[vorago-sweep]` case) **never run `vorago_tests.exe` without a filter**: those cases are
  not hidden and an unfiltered run starts a multi-hour sweep.
- **CPU-timed runs** only via `node tools/run-cpu-tests.js <suite>`, alone, nothing else running (root
  `CLAUDE.md`). T002 changes that runner's filter first so it can never pick up the sweep (ruling R-6).
- **Cross-cutting rules for every task:** no allocation/lock/exception/IO on the audio thread; finite checks by bit
  pattern only (`Krate::DSP::detail::isFinite`, `db_utils.h`), never `std::isnan`/`std::isfinite`; every new TU
  that checks finiteness is on the `-fno-fast-math -fno-finite-math-only` list (T003); no brace-init narrowing
  (designated initialisers in declaration order, explicit `static_cast`, `size_t`→`int32` casts); **no bit-exact
  float goldens** — `render_fingerprint.h` tolerances or measured tolerances only (spec C-8); `std::thread` + join,
  never `std::jthread`; `getenv_s` under `_MSC_VER`, `std::getenv` elsewhere; naming per root `CLAUDE.md` (classes
  PascalCase, functions camelCase, members `trailing_`, constants `kPascalCase`, IDs `k{Section}{Param}Id`).
- **ODR before every new name:** `grep -rn -E "(class|struct|enum class|enum) <Name>\b" dsp/ plugins/ tools/ tests/`.
  Plan §5 recorded 0 hits for every new name except `ParamSetting`, `SweepTimeline`, `DecodedPresetState`,
  `PresetFile`, `RenderSpec` (one hit each in a Seraphis namespace/target — allowed; no TU may `using namespace`
  both `Vorago` and `Seraphis`). Re-run the sweep at the task; a new hit is a stop.
- **Namespace hazard:** `Krate::DSP::TestUtils::Vorago` exists (`tests/test_helpers/vorago_fixtures.h:70-73`), so
  inside `namespace VoragoTest` the plugin namespace is spelled `::Vorago::` and no `using namespace` of either
  appears (as `preset_test_support.h:22-24` already documents).
- **Reused test helpers (read 2026-09-29):** `Krate::Test::ParameterChanges::addChange(ParamID, double)`
  (`tests/test_helpers/vst_param_changes.h:31`, `:110`); `Krate::Test::EventList::addNoteOn(int16 pitch, float
  velocity, int32 offset = 0)` / `addNoteOff(int16 pitch, int32 offset = 0)` (`vst_event_list.h:34`, `:63`, `:77`);
  both Catch2-free. Metrics `bandEnergyDb`, `crestFactorDb`, `blockRmsDb`, `perBandTotalVariation`,
  `perBinMagnitudeFlux` (`vorago_fixtures.h:245`, `:316`, `:344`, `:441`, `:502`); `calculateCorrelation`
  (`low_frequency_metrics.h:478`); `fingerprintRender`, `compareFingerprints`, `kSampleTolerance = 5.0e-4f`,
  `kMetricTolerance = 2.5e-4` (`render_fingerprint.h:58-63`, `:73`, `:122-124`); `TestHelpers::AllocationScope` /
  `ThreadScopedAllocationScope` (`allocation_detector.h:111`, `:149`; read the live count inside the scope via
  `AllocationDetector::instance().getAllocationCount()`, precedent `automation_rt_test.cpp:115-119`). Stimulus
  constants `kVelocity100 = 100.0f / 127.0f`, `kOutputCeiling = 0.9661f` (`soak_test.cpp:44-45`); `kCpuNotes{36,
  40, 43, 47}`, `kCpuPolyphony = 4` (`processor_cpu_test.cpp:84-85`). `ProcessorFixture` (`vorago_test_fixture.h:
  166-330`, `prepare(double sr = 48000.0, int32 maxBlock = 2048)` at `:192`) is Catch2-dependent: tests only.
- **Macro IDs:** M*m* = `99 + m` (M1 Darkness 100 … M5 Gravity 104 … M10 Life 109, M11 Depth 110, M12 Mass 111),
  every macro default 0.0 except Gravity 0.5 (`macro_params.h:33-42`, plan §4).
- **Out of scope for every task:** commits and pushes (outside this workflow; a task that needs a CI run on pushed
  code says so and waits for the user's explicit permission), any new DSP class, any `dsp/` edit other than T010,
  any Seraphis file, any parameter outside the ecosystem block, any edit to the probe TU.

---

## Stage C1 — rulings, registration, host, E0 (plan §2 "C1", "E0")

## Group 1 — pre-code rulings (STOP)

### T001 — Record the plan §12 rulings before any code

- **Why:** plan §12 lists six items "for the user (before C1)". Every task below is written on the plan's default
  for each; a different ruling changes only the tasks named here.
- **Do:** append `## Plan §12 rulings (second pass)` to `specs/vorago-phase14-presets-release/compliance.md` and
  check whether the user has ruled each item:
  1. P2-2 take sets `A_K = {(s+j) mod 16 : j < K}` for `D(P)`, `B_K = {(s+K+j) mod 16 : j < K}` for `t_K`
     (default; affects T006 `takeSeedIndex`).
  2. P2-3 controls (a), (a′), (c) are same-seed single-take comparisons (default; affects T036, T042).
  3. P2-4 SC-026a access path: keep the `detail::VoragoEcosystemRosterProbe` friend as the spec states (plan
     default) **or** drop it and read through the existing public const `Processor::engineForTest()`
     (`processor.h:116-118`). Affects T012 and T018 only.
  4. P2-1 order: E0 (T007/T008) runs before stage B (default).
  5. §6.11 skip rules (default: skip a render only where its verdict is known exactly, record the reason) **or**
     "render regardless" (≈ 8 150 s instead of ≈ 6 400 s per preset, §10). Affects T035 only.
  6. SC-011's envelope clause applies to the D8.2/D9.1 **primaries**; D8/D9 secondaries are always-audible state
     cells (default; affects T026, T035).
- **If any item is unruled: STOP** and surface the six items verbatim (plan §12). Record each ruling with its date.
  No later task starts until all six are recorded.
- **RULED 2026-09-29 (spec Clarifications "Plan stage (2026-09-29)"; already transcribed to compliance.md):**
  1 acknowledged; 2 same-seed single take; 3 **DROP the friend** — T012 reads `*p.engineForTest()` and defines no
  struct, T018 skips its SC-026a bullet; 4 acknowledged; 5 skips acknowledged; 6 acknowledged. Confirmed too: T003
  as the one mid-phase registration (T060 audits), R-6 at T002, the added per-push cases, E0 tolerance 0.0015.
  T001's remaining job is to verify the compliance.md section is present; nothing is unruled.

## Group 2 — CPU runner excludes the sweep (ruling R-6; existing file)

### T002 — `tools/run-cpu-tests.js`: `FILTER` gains `~[vorago-sweep]`

- **Edit:** `tools/run-cpu-tests.js:55`, today `const FILTER = '[performance],[perf],[.perf],[benchmark],[!benchmark],[long]';`.
- **Ordering note:** plan §2 lists this under stage H. The runner hands `[long]` to every suite (`:92-93`), so from
  T041 on a CPU run of `vorago_tests` would start the multi-hour sweep. Landing it first removes that hazard and
  changes nothing for any other suite (no other suite uses `[vorago-sweep]`).
- **Test first:** `grep -n "vorago-sweep" tools/run-cpu-tests.js` → 0 hits (failing state).
- **Implement:** `const FILTER = '[performance],[perf],[.perf],[benchmark],[!benchmark],[long]~[vorago-sweep]';`
  with the comment `// Phase 14 R-6: the Vorago preset sweep never shares a CPU run`.
- **Verify:** the grep finds it once; `node --check tools/run-cpu-tests.js` exits 0. Do **not** run CPU suites here.

## Group 3 — the one mid-phase registration (FR-027a; shared CMake files)

### T003 — Register every new TU (as skeletons) and the generator targets

- **Create skeletons:** `plugins/vorago/tests/unit/ecosystem_roster_test.cpp`, `unit/state_v3_test.cpp`,
  `unit/tail_samples_test.cpp`, `unit/ghost_triggers_additive_test.cpp`, `unit/preset/factory_preset_test.cpp`
  (new `unit/preset/` directory), `integration/preset_sweep_test.cpp`, `integration/preset_matrix_test.cpp`,
  `integration/preset_pilot_test.cpp`, `integration/preset_load_rt_test.cpp`, `integration/preset_cpu_test.cpp`,
  and `tools/vorago_preset_generator.cpp`. Each test skeleton = a banner (`// Vorago Phase 14 — <purpose>; FR/SC
  ids; filled by T0xx`) plus `#include <catch2/catch_test_macros.hpp>`, no `TEST_CASE`. Generator skeleton =
  banner, `#include <cstdio>`, `void* moduleHandle = nullptr;` (the Vorago stub `plugins/vorago/tests/
  vstgui_test_stubs.cpp` defines only `GetPluginFactory`), and `int main() { std::fprintf(stderr,
  "vorago_preset_generator: skeleton (Phase 14 T028)\n"); return 1; }`.
- **Test first:** build `--target vorago_preset_generator` → "no such target" (failing state).
- **`plugins/vorago/tests/CMakeLists.txt`:**
  - under the existing `# Phase 14 (specs/vorago-phase14-presets-release)` comment (after `:52`), the ten test TUs
    in the order above;
  - add `${CMAKE_SOURCE_DIR}/tools` to `target_include_directories(vorago_tests …)` — needed from T024 for
    `vorago_preset_defs.h` (precedent `plugins/seraphis/tests/CMakeLists.txt:109`); plan §9 omits it, it belongs
    to this single registration task;
  - on the `-fno-fast-math -fno-finite-math-only` list (`:108-138`), after the probe line `:137`, each with
    `# bit-pattern finiteness`: `unit/ecosystem_roster_test.cpp`, `unit/state_v3_test.cpp`,
    `unit/tail_samples_test.cpp`, `unit/preset/factory_preset_test.cpp`, `integration/preset_sweep_test.cpp`,
    `integration/preset_matrix_test.cpp`, `integration/preset_pilot_test.cpp`, `integration/preset_load_rt_test.cpp`.
    **Not** `preset_cpu_test.cpp` (the `processor_cpu_test.cpp` reason, comment at `:140`) and **not**
    `ghost_triggers_additive_test.cpp` (no finiteness check; plan §9).
- **Root `CMakeLists.txt`:** after `generate_seraphis_presets` (`:664-669`), a block copied from the Seraphis block
  (`:594-662`) with a banner citing `release.yml:150-182` (target name and `build/bin` are fixed):
  - `add_executable(vorago_preset_generator tools/vorago_preset_generator.cpp plugins/vorago/src/processor/
    processor.cpp` + the same five SDK sources as `:608-612` + `plugins/vorago/tests/vstgui_test_stubs.cpp)`;
  - `target_link_libraries(vorago_preset_generator PRIVATE KrateDSP KratePluginsShared sdk)` — no VSTGUI, no Catch2;
  - include dirs `plugins/vorago/src`, `plugins/vorago/tests`, `tests/test_helpers` (header-only use, NOT the
    Catch2-linking `test_helpers` target), `tools`, `${vst3sdk_SOURCE_DIR}`;
  - `cxx_std_20`; `RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"`; the Seraphis MSVC/GCC warning sets
    (`:643-660`, including `/wd4459`);
  - `add_custom_target(generate_vorago_presets COMMAND vorago_preset_generator
    "${CMAKE_SOURCE_DIR}/plugins/vorago/resources/presets" DEPENDS vorago_preset_generator COMMENT "Generating
    Vorago factory presets (38 presets across 7 categories)" VERBATIM)` (38 since the G2 ruling; T003 wrote 40).
- **Verify:** reconfigure (`--preset windows-x64-release`); build `vorago_tests` and `vorago_preset_generator` with
  **zero warnings**; `vorago_tests.exe "~[performance]~[perf]~[benchmark]~[!benchmark]~[long]" 2>&1 | tail -3` → all
  pass; `git diff -- plugins/vorago/tests/integration/ecosystem_rule_probe_test.cpp` empty.

## Group 4 — host, FR-061 enforcing test ([P], disjoint files)

### T004 [P] — `plugins/vorago/tests/vorago_preset_host.h`: `VoragoTest::PresetHost` (FR-021, §5.6 C1 part)

- **ODR first:** `PresetHost` → 0 hits (§5.6).
- **Test first** (`unit/preset/factory_preset_test.cpp`) `TEST_CASE("Vorago_PresetHost_DriveContract", "[vorago][preset]")`:
  - `PresetHost h; REQUIRE(h.prepare(48000.0, 512) == kResultOk);`
  - `Krate::Test::EventList ev; ev.addNoteOn(36, 100.0f / 127.0f, 0);` `REQUIRE(h.process(512, &ev, nullptr) ==
    kResultOk);` `REQUIRE(h.outL().size() == 512 && h.outR().size() == 512);` every sample finite by bit pattern;
  - 188 more blocks with no events: every sample finite and `|x| ≤ 0.9661f`;
  - `h.saveState(a)` true; first little-endian int32 of `a` == `::Vorago::kCurrentStateVersion`; a second host
    `prepare` → `loadState(a) == kResultOk` → `saveState(b)` → `REQUIRE(a == b)`;
  - `static_assert(!std::is_copy_constructible_v<VoragoTest::PresetHost>)`.
- **Implement** the §5.6 class **without** `buildPresetComponentState` (T027): `std::make_unique<::Vorago::Processor>()`;
  `prepare` = `initialize(nullptr)` → `setupProcessing(ProcessSetup{.processMode = kRealtime, .symbolicSampleSize
  = kSample32, .maxSamplesPerBlock = maxBlock, .sampleRate = sampleRate})` → `setActive(true)`; `outL_/outR_`
  sized to `maxBlock` once, never regrown; `process(n, ev, pc)` builds a stereo-out `ProcessData` (no audio input —
  event-in + stereo-out) with `numSamples = n ≤ maxBlock`; `loadState`/`saveState` through
  `Steinberg::MemoryStream`; destructor `setActive(false)` if active, then `terminate()`. Includes
  `"processor/processor.h"`, `"plugin_ids.h"`, `<vst_param_changes.h>`, `<vst_event_list.h>`,
  `public.sdk/source/common/memorystream.h`, std. **No Catch2 include** (T028's generator build, which does not
  link Catch2, is the proof).
- **Verify:** build `vorago_tests` zero warnings; the case passes.

### T005 [P] — `unit/ghost_triggers_additive_test.cpp`: FR-061 enforcing case (Clarification Q8)

- **Test (enforcing, expected green):** `TEST_CASE("Vorago_Ghost_TriggersAddToDensityScheduler", "[vorago][ghost]")`:
  - two heap `Krate::DSP::AtmosphereEngine`s configured exactly as `VoragoEngine::prepare` configures `atmos_`
    (`vorago_engine.h:361-374`): same `setSeed(x)`; `prepare(48000.0, AtmosphereEngine::PrepareConfig{
    .captureSeconds = 20.0f, .blurEnabled = true, .freezeEnabled = false, .blurFftSize = 1024, .freezeFftSize =
    2048, .maxBlockSamples = 512})` (the `VoragoEngineConfig{}` defaults, `vorago_engine.h:121-126`);
    `setDensity(0.30f)`, `setGrainSeconds(12.0f)`, `setPitchSemitones(-12.0f)`, `setPositionSpread(0.90f)`,
    `setBlur(0.85f)`, `setDecorrelation(0.85f)`, `setGrainReverseProbability(0.0f)`;
  - identical deterministic excitation into both through `processStereoBlock` (a 65.4 Hz sine at −12 dBFS, same
    buffers to both): 20 s warm-up (fills the capture ring), then a 60 s span in 512-sample blocks;
  - arm Off never triggers; arm On calls `triggerGrain()` (`atmosphere_engine.h:1083`) N = 12 times, every 5 s;
  - over the span: `getSkippedTriggerCountPoolFull()` and `getSkippedTriggerCountRingCold()` deltas == 0 in both
    arms (`:1148`, `:1154`); On's `getTotalTriggeredGrainsBorn()` delta == 12 (`:1235`); On's
    `(getTotalGrainsBorn() − getTotalTriggeredGrainsBorn())` delta == Off's `getTotalGrainsBorn()` delta (`:1159`),
    and Off's delta > 0.
  - banner cites the Vorago half: `setGhostEventTriggers` writes only the flag and latch (`vorago_engine.h:1004-1010`),
    the trigger site only calls `triggerGrain()` (`:1530-1537`), `:369` is the only ghost `setDensity` writer.
- **If red:** FR-017 finding (shipped behaviour differs from Q8) — STOP and report; never edit `atmosphere_engine.h`.
- **Verify:** build zero warnings; the case passes.

## Group 5 — harness support part 1 (existing shared header)

### T006 — `preset_test_support.h`: Constants, Take sets, Render, Env, Pool (§5.8 C1 rows, P2-2)

- **Edit:** `plugins/vorago/tests/preset_test_support.h` (part 0 body stays byte-for-byte) and
  `unit/preset/factory_preset_test.cpp` (tests).
- **ODR first:** `RenderSpec` (one allowed Seraphis anonymous-namespace hit), `SweepCapture`, `takeSeedIndex`,
  `seedNormalized`, `renderPreset`, `sweepEnv`, `runJobs` → no other hits.
- **Tests first** (`factory_preset_test.cpp`):
  - `TEST_CASE("Vorago_PresetSupport_TakeSets", "[vorago][preset]")`: `takeSeedIndex(0, 0, j, 8) == j` (j = 0…7);
    `takeSeedIndex(0, 1, j, 8) == 8 + j`; `takeSeedIndex(15, 0, 1, 4) == 0`; `takeSeedIndex(14, 1, 3, 2) == 3`; for
    every stored s ∈ 0…15 and K ∈ {1, 2, 4, 8}: A-set and B-set each hold K distinct indices, are disjoint, and
    `A_K ⊂ A_8`; `seedNormalized(0) == 0.0`, `seedNormalized(15) == 1.0`, and `::Vorago::indexFromNormalized(
    seedNormalized(i), 16) == i` for all i (`param_mapping.h:55-58`); `static_assert(2 * kMaxTakes == kNumSeedIndices)`.
  - `TEST_CASE("Vorago_PresetSupport_RenderStreams", "[vorago][preset]")`: `renderPreset(RenderSpec{.comp = {},
    .end = 2.0, .capture = {{0.5, 1.5}}})` → `finite`, `peak ≤ 0.9661f`, `blockPowerL.size() == blockPowerR.size()
    == 188` (`ceil(96000 / 512)`), `capL.size() == 1`, `capL[0].size() == 48000`; with `.seedIndex = 3` finite; with
    `.notes = {36, 40, 43, 47}, .forcePolyIndex = 3` finite.
  - `TEST_CASE("Vorago_PresetSupport_PoolRunsAllJobs", "[vorago][preset]")`: 37 jobs each writing its own slot of a
    pre-sized `std::vector<int>`; `runJobs(jobs, 4)` → every slot written exactly once; same with 1 thread.
  - `TEST_CASE("Vorago_PresetSupport_EnvUnset", "[vorago][preset]")`: `sweepEnv("VORAGO_PHASE14_SURELY_UNSET") == std::nullopt`.
- **Implement** (namespace `VoragoTest`, Catch2-free):
  - `inline constexpr double kFloorF = 4.0, kSecondaryBar = 1.5, kSeedTwinMargin = kFloorF / 2, kLevelTwinBound =
    0.05, kFreezeFloorDb = 20.0, kTailDropDb = 40.0; inline constexpr int kNumSeedIndices = 16, kMaxTakes = 8;` —
    **no `kRuledTakes`** (T040 adds it after G2);
  - `int takeSeedIndex(int stored, int set, int j, int K) noexcept` → `(stored + set·K + j) mod 16`;
    `double seedNormalized(int index)` → `index / 15.0`;
  - `struct RenderSpec` (fields in this order: `comp`, `block0` as plain `(ParamID, normalized)` pairs, `seedIndex
    = -1`, `sr = 48000`, `notes{36}`, `forcePolyIndex = -1`, `noteOffAt = -1`, `freezeAt = -1`, `end`, `capture`)
    and `struct SweepCapture {finite, peak, blockPowerL, blockPowerR, capL, capR}` (§5.8); `SweepCapture
    renderPreset(const RenderSpec&)` on a fresh `PresetHost` (T004): prepare at `spec.sr`, block 512; empty `comp`
    skips `loadState`; block 0 carries every `block0` point, `kSeedId → seedNormalized(seedIndex)` when ≥ 0,
    `kPolyphonyId → indexToNormalized(forcePolyIndex, 6)` when ≥ 0, all at offset 0, plus NoteOn for every note at
    `kVelocity100`, offset 0 (parameter changes latch before events, `processor.cpp:298`); `noteOffAt ≥ 0` →
    NoteOff at sample `llround(noteOffAt·sr)` at its in-block offset; `freezeAt ≥ 0` → `kSpaceFreezeId → 1.0` at
    offset 0 of the first block whose start sample ≥ `llround(freezeAt·sr)` (§6.1); streaming: per block update
    bit-pattern finiteness and the stereo peak, append one power sum per channel; copy samples only inside
    `capture` windows (capacity reserved before the loop);
  - `std::optional<std::string> sweepEnv(const char*)` (`getenv_s` under `_MSC_VER`, the probe's pattern
    `ecosystem_rule_probe_test.cpp:211`; the probe's own `readEnv` is untouched);
  - `void runJobs(std::vector<std::function<void()>>& jobs, unsigned threads)` — plain `std::thread` + join, an
    atomic next-index; jobs never call Catch2 macros;
  - banner line "T022 and T025 extend this header" → "Phase 14 second-pass tasks T006, T025, T026, T033–T036,
    T040 extend this header".
- **Verify:** build zero warnings; the four cases and `Vorago_PresetHost_DriveContract` pass.

## Group 6 — E0 case (pilot TU, owned by this task)

### T007 — `Vorago_PresetPilot_DefaultSurfaceTakeCurve` (FR-017a, §6.16 step 1)

- **Test (the measurement; hidden):** `TEST_CASE("Vorago_PresetPilot_DefaultSurfaceTakeCurve", "[.probe][vorago]")`
  in `integration/preset_pilot_test.cpp`.
- **Implement:**
  - hard-coded default-surface timeline (banner cites `vorago_voice.h:324-328`, stage times `{20000, 30000, 45000,
    60000}` ms, Standard): `A = 155 s`, `M1 = [160, 220]`, `M2 = [220, 280]`, `M3 = [280, 340]`, end = `H` = 340 s;
    no NoteOff, no tail. No `decodePresetState`, `makeTimeline` or defs (they do not exist yet).
  - 16 renders `renderPreset(RenderSpec{.comp = {}, .seedIndex = i, .end = 340.0, .capture = {M1, M2, M3}})`,
    i = 0…15, through `runJobs` with `VORAGO_SWEEP_THREADS` (via `sweepEnv`) or `min(hardware_concurrency, 4)`;
  - `m_i^k = describe(M_k)` (part 0); for K ∈ {1, 2, 4, 8}: `D_A = meanOf` of the 3K minute descriptors of seeds
    `takeSeedIndex(0, 0, j, K)`, `D_B` likewise for set 1, `t_K = descriptorDistance(D_A, D_B)`;
  - pool trust check (plan §11): render seed 0 once more **serially** and compare its M1 with the pooled seed-0
    M1 via `compareFingerprints(fingerprintRender(…), …).withinTolerance()` per channel;
  - print each seed's stereo M1 RMS dBFS; the table `K  t_K  2·t_K  pass(2·t_K ≤ 4.0)`; the cross-check line
    `t_1 = <x>  13b t0on = 5.9515  |diff| = <y>  tolerance = 0.0015` (0.0015 ≈ the 5e-5 print rounding +
    `kMetricTolerance` 2.5e-4 × 5.95; the 13b figure is `final2_table_default.log:43`); the verdict line
    `E0: 2*t_8 = <v> -> PROCEED | STOP (FR-017a)`.
  - **Assertions (only):** every render `finite` with `peak ≤ 0.9661f`; every seed's M1 stereo RMS ≥ −60 dBFS; the
    pool trust check. Verdicts are read from the log.
- **Verify:** build zero warnings; `--list-tests` lists it; the per-push run skips it (hidden).

## Group 7 — run E0 (early-warning gate)

### T008 — Run E0 alone; record; STOP if `2·t_8 > 4.0` (FR-017a, plan P2-1)

- **Run** (alone; ~9 min local by §10): `build/windows-x64-release/bin/Release/vorago_tests.exe
  "Vorago_PresetPilot_DefaultSurfaceTakeCurve" > f:/tmp/p14/e0.log 2>&1` (background; wait for completion; never
  re-run to see output).
- **Record:** copy to `specs/vorago-phase14-presets-release/artifacts/e0_default_take_curve.log`; append
  `## FR-017a E0 — default-surface t_K curve` to `compliance.md` with the command, measured wall clock, the `t_K`
  table, the `t_1` cross-check line and the verdict.
- **Gate:**
  - `2·t_8 > 4.0` → **STOP**: surface the measured curve with P2-1's prediction table (plan §3). Author nothing,
    build nothing further. K is never raised past 8 and F never raised (spec C-7.3, FR-017a).
  - `|t_1 − 5.9515| > 0.0015` → surface the mismatch in the same report (the harness does not reproduce 13b's
    figure); do not proceed until ruled.
  - Otherwise proceed to stage B.

---

## Stage B — ratified surface (FR-071a…FR-074, SC-026a, SC-027) + `getTailSamples()` (FR-060)

## Group 8 — DSP tests first (existing DSP test TU)

### T009 — `dsp/tests/unit/systems/vorago_param_surface_test.cpp`: forwarders and field count (FR-071a, FR-076)

- **Edit** (already registered; no CMake change). **Write failing tests:**
  - new `TEST_CASE("VoragoVoice_EcosystemRuleForwarders", "[systems][vorago]")` on a heap `VoragoVoice` prepared at
    48 kHz: `setEcosystemSyncRate(0.25f)` → `ecosystem().getSyncRate() == 0.25f`; `(0.9f)` → `0.5f` (clamp); a NaN
    by bit pattern (`std::bit_cast<float>(0x7FC00000u)`) → unchanged (`0.5f`); `setEcosystemSelfAffinity(1.5f)` →
    `getAffinity(k, k) == 1.5f` for all five `EcosystemEngine::Kind`s and every off-diagonal `getAffinity(a, b) ==
    0.45f` (untouched); `(3.0f)` → diagonal `2.0f`; NaN → unchanged.
  - `VoragoEngine_ApplyVoiceParamsDefaultIsNoOp_Short` (`:1086`): `STATIC_REQUIRE(VoragoVoiceParams::kFieldCount ==
    31u)` (`:1088`) → `33u`; add `REQUIRE(VoragoVoiceParams{}.ecosystemSyncRate == eco->getSyncRate())` and
    `REQUIRE(VoragoVoiceParams{}.ecosystemSelfAffinity == eco->getAffinity(Kind::Partial, Kind::Partial))` on a
    heap `EcosystemEngine{}` (defaults 0.0 / −1.0: `ecosystem_engine.h:2386`, `:2403-2411`). The existing
    `runDefaultBroadcastNoOp` then covers the new fields.
  - extend `VoragoEngine_ApplyVoiceParamsReachesAllSlots` (`:1096`): `ecosystemSyncRate = 0.25f`,
    `ecosystemSelfAffinity = 1.5f` → for v = 0…5 `getVoice(v).ecosystem().getSyncRate() == 0.25f` and all five
    diagonal entries `== 1.5f`.
- **Verify red:** build `dsp_systems_tests` → compile errors naming the missing forwarders and fields.

## Group 9 — DSP append (R-1; the phase's only `dsp/` edit)

### T010 — Forwarders, `VoragoVoiceParams` fields, `applyVoiceParams` lines (§5.1)

- **Edit (append-only):** `dsp/include/krate/dsp/systems/vorago_voice.h`, `dsp/include/krate/dsp/systems/vorago_engine.h`.
- **ODR first:** `setEcosystemSyncRate`, `setEcosystemSelfAffinity`, `ecosystemSyncRate`, `ecosystemSelfAffinity` → 0 hits.
- **Implement exactly §5.1:**
  - `vorago_voice.h`, public, right after `getEcosystemDepthFor` (`:1472-1476`): `void setEcosystemSyncRate(float v)
    noexcept { ecosystem_.setSyncRate(v); }` and `void setEcosystemSelfAffinity(float v) noexcept` looping `k <
    EcosystemEngine::kNumKinds` (= 5, `ecosystem_engine.h:168`) with `ecosystem_.setAffinity(kind, kind, v)`; doc
    comments per §5.1 (`setSyncRate` rejects non-finite and clamps [0, 0.5], `ecosystem_engine.h:595-600`;
    `setAffinity` index guard first, non-finite rejected, clamp [−2, 2], not symmetrised, `:707-720`). No getters
    (tests read `ecosystem()`, `vorago_voice.h:1549`).
  - `vorago_engine.h` `VoragoVoiceParams`, after `ecologyLoopFilterMode` (`:183-186`): `float ecosystemSyncRate =
    0.0f;` and `float ecosystemSelfAffinity = -1.0f;` with the §5.1 comments; `kFieldCount` 31 → 33 with `/// 2 + 2
    + 4 x 5 + 1 + 6 + 2 = 33 scalar values (FR-003; Phase 14 R-1 +2).` (`:187-188`). No field names a
    `VoragoMacroTarget` (`:152-154`).
  - `applyVoiceParams` (`:859-876`): inside the slot loop, after the loop-filter loop:
    `voice.setEcosystemSyncRate(p.ecosystemSyncRate); voice.setEcosystemSelfAffinity(p.ecosystemSelfAffinity);`
  - Nothing else: not `ecosystem_engine.h`, not the probe friends, no other `dsp/` file.
- **Verify:** build `dsp_systems_tests` zero warnings; `dsp_systems_tests.exe
  "VoragoVoice_EcosystemRuleForwarders,VoragoEngine_ApplyVoiceParams*" 2>&1 | tail -3` → pass (this also runs the
  `[long]` `…DefaultIsNoOp`; log it to `f:/tmp/p14/`); per-push filter of `dsp_systems_tests` → pass;
  `node tools/check-seraphis-green.js` → green.

## Group 10 — new failing plugin tests + tail helper ([P], disjoint files)

`vorago_tests` does not compile from T012/T013 until T018 lands; that compile failure is the red state. Do not stub
IDs early to make it build.

### T011 [P] — `plugins/vorago/src/processor/tail_estimate.h` (FR-060, §5.3)

- **ODR first:** `effectiveCavernDecaySeconds`, `kGhostGrainCeilingSeconds`, `tailSeconds`, `tail_estimate` → 0 hits.
- **Create** the §5.3 header: `namespace Vorago { inline constexpr double kGhostGrainCeilingSeconds =
  static_cast<double>(Krate::DSP::AtmosphereEngine::kMaxGrainSeconds);` (30.0f, `atmosphere_engine.h:311`);
  `[[nodiscard]] inline float effectiveCavernDecaySeconds(const Krate::DSP::VoragoMacroValues& knobs, float
  storedDecaySeconds) noexcept` — stack `VoragoMacroMatrix m{}`; `m.setTargetBase(VoragoMacroTarget::
  CavernDecaySeconds, storedDecaySeconds)`; `m.setMacros(knobs)`; clamp `m.computeCavernTargets().decaySeconds` to
  `[kSpaceDecayMinSeconds, kSpaceDecayMaxSeconds]` (`space_params.h:58-59`) as floats; `[[nodiscard]] inline double
  tailSeconds(float releaseMs, float rt60Seconds) noexcept` = `releaseMs/1000 + rt60 + kGhostGrainCeilingSeconds`.
  Includes `"parameters/space_params.h"`, `<krate/dsp/systems/atmosphere_engine.h>`,
  `<krate/dsp/systems/vorago_macro_matrix.h>`, `<algorithm>`.
- **Verify:** through T014 once T018 lands.

### T012 [P] — `unit/ecosystem_roster_test.cpp` (FR-071a, FR-076, SC-026a, SC-027 engine half)

- **Write failing tests** (access path per T001 item 3 — **RULED 2026-09-29: drop the friend**: use
  `*p.engineForTest()` and define no struct; the superseded default follows). Define, once in the program,
  in this TU: `namespace Vorago::detail { struct VoragoEcosystemRosterProbe { static const
  Krate::DSP::VoragoEngine& engine(const Processor& p) { return *p.engine_; } }; }` (if T001 ruled "drop": use
  `*p.engineForTest()` and define no struct).
  - `TEST_CASE("Vorago_EcosystemRosterReachesEngine", "[vorago][ecosystem]")` on `ProcessorFixture`
    (`prepare(48000.0, 512)`): pass 1, no note — `kEcosystemSyncRateId → 1.0`, `kEcosystemSelfAffinityId → 1.0`
    via `IParameterChanges` in one `processBlock`; for v = 0…5 through `engine(proc).getVoice(v)`
    (`vorago_engine.h:1280`): `ecosystem().getSyncRate() == 0.5f`, `getAffinity(k, k) == 2.0f` for all five kinds,
    every off-diagonal `== 0.45f`; a voices-checked counter `== 6` (`kMaxVoices`, `vorago_engine.h:225`). Pass 2:
    the same with NoteOn 36 held. Pass 3: `901 → 0.0`, `902 → 0.25` → `0.0f` / `-1.0f` on all 6, counter == 6.
    Exact float equality is valid (linear taper at range ends / quarter point, exactly representable).
  - `TEST_CASE("Vorago_State_V2LoadsWithRosterDefaults_Engine", "[vorago][state]")` (SC-027's engine half; its
    persisted half is T013's): drive 901 → 1.0 and 902 → 1.0, one `process()`; build the v2 stream as in T013;
    `setState(v2)`; one `process()`; on all 6 voices `getSyncRate() == 0.0f` and all five `getAffinity(k, k) ==
    -1.0f`; counter == 6. (It lives here so the probe struct has exactly one definition — ODR.)
  - `TEST_CASE("Vorago_VoiceParams_FieldCount", "[vorago][ecosystem]")`: `static_assert(Krate::DSP::
    VoragoVoiceParams::kFieldCount == 33)`; the two default-member equalities of T009 on a
    `std::make_unique<Krate::DSP::EcosystemEngine>()`.
- **Waits on:** T015, T016, T017, T018.

### T013 [P] — `unit/state_v3_test.cpp` (FR-072, FR-074, SC-027 persisted half)

- **Write failing tests** (`ProcessorFixture`, 48 kHz / 512; the v3 extension follows the life pack, so the two
  knob floats sit at byte offsets 428 and 432, §5.2):
  - `TEST_CASE("Vorago_StateRoundTripV3", "[vorago][state]")`: 901 → 0.8, 902 → 0.8 in one block; `getState` length
    `== ::Vorago::kStateV3Bytes` (436); first int32 `== 3`; floats at 428/432 == 0.4 / 1.2 (`Approx().margin(1e-6)`);
    `setState` into a fresh processor then `getState` → `memcmp == 0`.
  - `TEST_CASE("Vorago_State_V2LoadsWithRosterDefaults", "[vorago][state]")` (from a NON-default state): drive 901 →
    1.0, 902 → 1.0, one `process()`; v2 stream = that processor's `getState()` bytes, version int overwritten to 2,
    truncated to `kStateV2Bytes` (428; exact because v2 is a strict prefix of v3); `setState(v2) == kResultOk`;
    one `process()`; `getState` floats at 428/432 == 0.0 / −1.0.
  - `TEST_CASE("Vorago_State_V3TruncatedKeepsPrefix", "[vorago][state]")`: processor X at 901/902 = 0.8/0.8 (plain
    0.4 / 1.2); load processor Y's v3 stream (0.2 / 0.6 normalized) truncated to 428 bytes → `kResultOk`; X's knobs
    still 0.4 / 1.2; every v2 field equals Y's.
  - `TEST_CASE("Vorago_State_V3NonFiniteKnobRejected", "[vorago][state]")`: overwrite the float at 428 with
    `0x7FC00000u` → `kResultOk`; syncRate unchanged; selfAffinity loaded.
  - `TEST_CASE("Vorago_State_V4Rejected", "[vorago][state]")`: version int 4 → `kResultFalse`.
  - `TEST_CASE("Vorago_ControllerState_V3AndV2", "[vorago][state]")` (FR-074 controller half): a
    `::Vorago::Controller` `initialize(nullptr)`; `setComponentState(v3 with 901 = 902 = 1.0)` →
    `getParamNormalized(901) == 1.0`, `(902) == 1.0`; `setComponentState(v2 stream)` → 0.0 and 0.25 (margin 1e-9).
- **Waits on:** T015, T016, T018, T019.

### T014 [P] — `unit/tail_samples_test.cpp` cases (i)–(iv) (FR-060, SC-030)

- **Write failing test** `TEST_CASE("Vorago_Processor_GetTailSamplesMatchesState", "[vorago][tail]")` on a
  processor prepared at 48 kHz (values via `IParameterChanges` + one `process()`; the decay ID's normalized value
  comes from `space_params.h`'s own plain→normalized mapping for `kSpaceDecayId`):
  - (iv) default surface → `4 560 000` ((45 + 20 + 30) s × 48 000: release 45 s `vorago_voice.h:324-328`, decay 20 s
    `space_params.h:42`, G = 30 s);
  - (i) decay 0.5 s, macros at default → `3 624 000` ((45 + 0.5 + 30) × 48 000);
  - (ii) decay 60 s with Depth (110) = 1.0 and Age (101) = 1.0 → effective clamped to 60 → `6 480 000`;
  - (iii) `kSpaceFreezeId` = 1.0 → `Steinberg::Vst::kInfiniteTail`;
  - each finite case within ±1 sample; `REQUIRE(::Vorago::kGhostGrainCeilingSeconds ==
    static_cast<double>(Krate::DSP::AtmosphereEngine::kMaxGrainSeconds))`.
  - Case (v), every factory preset from independently decoded state, is T030's.
- **Waits on:** T011, T018.

## Group 11 — IDs and state constants (existing header)

### T015 — `plugin_ids.h`: 901/902 and the v3 constants (FR-072)

- **Test first:** T012/T013 are the failing tests.
- **Implement (§5.2):** after `kEcosystemDepthId = 900,` (`:173`): `kEcosystemSyncRateId = 901,  // Phase 14 FR-072,
  R ratified 2026-09-29` and `kEcosystemSelfAffinityId = 902,  // Phase 14 FR-072`; `kCurrentStateVersion = 3`
  (`:23`, comment "Phase 14 FR-072: v3 = v2 + the ecosystem rule-knob extension"); keep `kStateV2Bytes = 428` and
  its `static_assert` (`:28-47`); add `constexpr std::size_t kStateV3Bytes = kStateV2Bytes + 4 /* float syncRate */
  + 4 /* float selfAffinity */; static_assert(kStateV3Bytes == 436, "spec FR-006: 428 + 4 * |R|, |R| = 2");`. Both
  IDs `< kEcosystemParamRangeEnd = 1000` (`:236`). No registered type changes (`:82-83`).
- **Verify:** compiles as far as the next missing piece.

## Group 12 — ecosystem pack (existing header + its contract test)

### T016 — `ecosystem_params.h`: fields, handler, registration, format, v3 extension I/O (FR-072, FR-074)

- **Tests first** — extend `unit/params/ecosystem_params_test.cpp` `Vorago_EcosystemParamsContract` (`:88`):
  registered default normalized 0.0 (sync) / 0.25 (affinity) and plain == engine default 0.0 / −1.0;
  `handleEcosystemParamChange(p, 901, 1.0)` → `p.syncRate == 0.5f`; `(902, 0.0)` → `-2.0f`; `(902, 1.0)` → `2.0f`;
  normalized ↔ plain round-trip within 1e-6 at n ∈ {0, 0.3, 1}; `saveEcosystemParamsV3Ext` writes exactly 8 bytes;
  `loadEcosystemParamsV3Ext` restores both; a NaN bit pattern in either float leaves that field unchanged (returns
  true); finite out-of-range (0.9 / −3.0) clamps to 0.5 / −2.0; EOF after 4 bytes → false, second field unchanged;
  `formatEcosystemParam(901, 0.5)` → `"0.25"`, `(902, 0.25)` → `"-1.00"`, `(902, 0.75)` → `"+1.00"`;
  `loadEcosystemParamsV3ExtToController` calls `setParam(901, 0.5)` and `setParam(902, 0.75)` for plain 0.25 / +1.0.
- **Implement (§5.2 "Pack"):** constants `kEcosystemSyncRate{Min,Max,Default} = 0.0/0.5/0.0`,
  `kEcosystemSelfAffinity{Min,Max,Default} = -2.0/2.0/-1.0` + `static_assert` against
  `Krate::DSP::EcosystemEngine::kMinAffinity/kMaxAffinity` (`ecosystem_engine.h:237-238`; add the include);
  `EcosystemParams` gains `std::atomic<float> syncRate{0.0f}; std::atomic<float> selfAffinity{-1.0f};`; two handler
  cases (`linearFromNormalized`, relaxed store); `registerEcosystemParams` gains `addParameter(STR16("Ecosystem
  Sync"), STR16(""), 0, 0.0, ParameterInfo::kCanAutomate, kEcosystemSyncRateId)` and `(STR16("Ecosystem Self
  Affinity"), STR16(""), 0, 0.25, ParameterInfo::kCanAutomate, kEcosystemSelfAffinityId)`; `formatEcosystemParam`
  prints sync `"%.2f"`, affinity `"%+.2f"`; `saveEcosystemParamsV3Ext`, EOF-safe `loadEcosystemParamsV3Ext`
  (isFinite then clamp; false at the first failed read), `loadEcosystemParamsV3ExtToController`. The v2
  `save/loadEcosystemParams` do not change. Banner updated (IDs 900–902; v2 pack 4 bytes + v3 extension 8 bytes).
- **Verify:** checked in T018 (the TU is in `vorago_tests`).

## Group 13 — routes (existing header + param-table tests)

### T017 — `param_routes.h`, `param_table_expected.h`, `param_table_test.cpp` (FR-074)

- **Tests first:** rows for 901/902 in `param_table_expected.h` and the four cases of `param_table_test.cpp`
  (`Vorago_ParamIdMap` `:53`, `Vorago_RouteTable` `:326`, `Vorago_ParameterInfoTable` `:607`,
  `Vorago_ParamInputHygiene` `:859`): names "Ecosystem Sync" / "Ecosystem Self Affinity", unit `""`, defaults 0.0 /
  0.25, stepCount 0, `kCanAutomate`, route VP; VP count 33; NaN / ±Inf normalized inputs on both IDs leave the
  stored value finite (bit pattern).
- **Implement:** `{kEcosystemSyncRateId, Route::VP}` and `{kEcosystemSelfAffinityId, Route::VP}` directly after
  `{kEcosystemDepthId, Route::MB}` (`:115`); `std::array<ParamRouteEntry, 108>` → `110` (`:36`); the 108 in the
  comments at `:6` and `:162` → 110; `countRoute(Route::VP) == 31` → `33` (`:281`). `idsStrictlyAscending()` holds.
- **Verify:** in T018.

## Group 14 — processor (existing files)

### T018 — `processor.h`/`.cpp`: VP push, v3 state, `getTailSamples()`, roster friend (FR-060, FR-071a, FR-072)

- **Tests first:** T012, T013 (processor cases), T014, T016, T017 are the failing tests.
- **Implement (§5.2 "Processor", §5.3):**
  - `pushVoiceParams()` (`processor.cpp:826-864`): before `engine_->applyVoiceParams(p)` (`:862`),
    `p.ecosystemSyncRate = ecosystemParams_.syncRate.load(kRelaxed);` and `p.ecosystemSelfAffinity =
    ecosystemParams_.selfAffinity.load(kRelaxed);`. The band dispatch already reaches the handler and `markDirty`
    bumps the VP generation from the route table — no change there.
  - `getState()` (`:640-661`): `saveEcosystemParamsV3Ext(ecosystemParams_, s);` after `saveLifeParams` (`:659`).
  - `setState()` (`:569-633`): the `[[maybe_unused]] const bool v2Complete` local (`:596`) becomes `bool v2Complete =
    false;` declared before the `if`, assigned from `loadV2Tail(s)` in the `version >= 2` branch and `true` after the
    default-tail load of the `else` branch; then `if (version >= 3) { if (v2Complete) { [[maybe_unused]] const bool
    v3 = loadEcosystemParamsV3Ext(ecosystemParams_, s); } } else {` serialize `EcosystemParams{}`'s extension into a
    stack `std::array<char, kStateV3Bytes - kStateV2Bytes>` `MemoryStream`, seek 0,
    `loadEcosystemParamsV3Ext`, `assert(defaultsLoaded); }` (the `:605-625` pattern). Header comment `:564-568` →
    "v3, 436 bytes (kStateV3Bytes); v2 and v1 are strict prefixes".
  - `getTailSamples()`: replace the comment at `processor.h:89` with `Steinberg::uint32 PLUGIN_API getTailSamples()
    override;`. Body: `spaceParams_.freeze` (relaxed) non-zero → `Steinberg::Vst::kInfiniteTail`; else a
    `Krate::DSP::VoragoMacroValues` from `macroParams_` **knobs only** (not `buildMacroVector()`, which adds channel
    pressure — not state), then `static_cast<uint32>(std::llround(tailSeconds(<release ms>,
    effectiveCavernDecaySeconds(knobs, <decay s>)) * processSetup.sampleRate))`. Include
    `"processor/tail_estimate.h"`. Relaxed atomics and a stack matrix only: allocation-free on any host thread.
  - SC-026a — **RULED 2026-09-29 "drop": skip this bullet entirely** (superseded default follows): `processor.h` `detail` block (`:58-63`) gains `struct VoragoEcosystemRosterProbe;`
    with a comment mirroring `:59-61` (defined only in `unit/ecosystem_roster_test.cpp`; ODR 0 hits); beside `:186`
    add `friend struct detail::VoragoEcosystemRosterProbe;  // Phase 14 SC-026a`. Skip both if T001 ruled "drop".
  - Do **not** touch `pushCavernParams` (`:964-991`), the freeze push (`:985-988`) or the `loadSpaceParams` call
    (SC-024: the freeze load path is frozen).
- **Verify:** build `vorago_tests` zero warnings; run `"Vorago_EcosystemRosterReachesEngine,Vorago_VoiceParams_FieldCount,
  Vorago_State_V2LoadsWithRosterDefaults*,Vorago_StateRoundTripV3,Vorago_State_V3*,Vorago_State_V4Rejected,
  Vorago_Processor_GetTailSamplesMatchesState,Vorago_EcosystemParamsContract,Vorago_ParamIdMap,Vorago_RouteTable,
  Vorago_ParameterInfoTable,Vorago_ParamInputHygiene"` → pass. Other count-pinning cases may be red until T020/T021;
  the controller case waits for T019.

## Group 15 — controller (existing file)

### T019 — `controller.cpp` `applyStateStream`: the v3 mirror (FR-072, FR-074)

- **Test first:** `Vorago_ControllerState_V3AndV2` (T013) is red.
- **Implement:** after the `version >= 2` / `else` block (`controller.cpp:210-234`): `if (version >= 3) {
  loadEcosystemParamsV3ExtToController(streamer, setParam); } else {` serialize `EcosystemParams{}`'s extension
  into `owned(new MemoryStream())`, seek 0, feed `loadEcosystemParamsV3ExtToController` `}` (the `:212-234` pattern).
  `registerEcosystemParams` (`:103`) and `formatEcosystemParam` already dispatch the band.
- **Verify:** the controller case passes; zero warnings.

## Group 16 — count-bearing test sweep (many existing test files)

### T020 — Update every test that pins the registered surface or the current stream (FR-074)

- **Find:** `grep -rn "108\|106\|kStateV2Bytes\|kCurrentStateVersion\|kFieldCount" plugins/vorago/tests
  dsp/tests/unit/systems/vorago_param_surface_test.cpp`; read every hit in context. A line meaning "the registered
  surface" or "the current stream" moves to a **named expression** — `108 + kNumEcosystemRosterParams` (declare
  `inline constexpr std::size_t kNumEcosystemRosterParams = 2;` once, in `plugins/vorago/tests/vorago_test_fixture.h`)
  or `kStateV3Bytes`; a line meaning a v2 literal stays.
- **Known hits (plan §5.2):** `unit/state_roundtrip_test.cpp` `Vorago_StateRoundTrip` (`:110`) — current-stream
  sizes and `memcmp` at `kStateV3Bytes`, version field 3, the `kCurrentStateVersion + 1` rejection now rejects 4;
  `unit/state_v2_test.cpp` `Vorago_StateRoundTripV2` (`:388`) becomes the **legacy v2 load** test (v2 stream =
  `getState()` bytes with version 2, truncated to `kStateV2Bytes`; every v2 field matches; its `setComponentState`
  half likewise); `automation_rt_test.cpp`, `continuity_test.cpp`, `ecosystem_frame_test.cpp`,
  `param_surface_test.cpp`, `preset_browser_test.cpp`, `processor_cpu_test.cpp`, `soak_test.cpp`,
  `body_params_test.cpp`, `envelope_params_test.cpp`, `param_denorm_test.cpp` — each hit read and classified.
  `editor_layout_test.cpp` is T021's.
- **Test-first note:** these are already red after T018 (their pins are stale); this task is the fix.
- **Verify:** zero warnings; per-push filter of `vorago_tests` → all pass except `editor_layout_test.cpp` cases and
  `Vorago_EditorLifecycle` / `EditorBindsSurface` (`unit/controller/editor_lifecycle_test.cpp`): its built-view
  bound-control count and tag set are pinned at `106 + kNumEcosystemRosterParams` (the SC-028 "bound-ID count"
  test), which cannot be green until T021 adds the 901/902 knobs to the uidesc. Expected-red until T021.

## Group 17 — ecosystem page UI (uidesc via XSLT + layout test)

### T021 — Two knobs on page 6 (FR-073, SC-028)

- **Tests first** (`unit/controller/editor_layout_test.cpp`): `kIdNames` (`:109`) gains 901 `EcosystemSyncRate` and
  902 `EcosystemSelfAffinity`; the page-6 set (`:252-256`) gains 901 and 902; counts at `:838`, `:839`, `:873`,
  `:909`, `:922`, `:1008`, pageUnion 90 → 92 (`:1127`), `getParameterCount()` (`:1305`, `:1348`) through
  `108 + kNumEcosystemRosterParams`; header comment (`:9-10`); new `TEST_CASE("Vorago_Ecosystem_PageBindsRosterIds",
  "[vorago][ui]")`: page 6 binds 901 and 902; the unbound allowlist is still exactly {4, 5}; both knobs lie inside
  the 1100 × 296 page and overlap no other view.
- **Implement (uidesc edits by XSLT — the project rule):** write `f:/tmp/p14/phase14_roster_knobs.xslt` (identity
  transform + inserts) and apply it to `plugins/vorago/resources/editor.uidesc`:
  - after `<control-tag name="EcosystemDepth" tag="900"/>` (`:111`): `<control-tag name="EcosystemSyncRate"
    tag="901"/>` and `<control-tag name="EcosystemSelfAffinity" tag="902"/>`;
  - page 6 row r0, beside EventsRateScale (`origin="18, 4"`, `:385`) and EcosystemDepth (`origin="86, 4"`, `:387`)
    on the 68-px pitch: `ArcKnob` `control-tag="EcosystemSyncRate"` `origin="154, 4"`, label "Eco Sync" at
    `142, 52`; `ArcKnob` `control-tag="EcosystemSelfAffinity"` `origin="222, 4"`, label "Self Affin" at `210, 52`;
    `size`, `arc-color`, `guide-color` and label attributes copied from `:387-388`; tooltips "How strongly the
    colony's agents fall into step" / "How much each agent kind attracts its own kind".
  `git diff` of the uidesc shows only those added lines.
- **Verify:** every `editor_layout_test.cpp` case passes; `Vorago_EditorLifecycle` / `EditorBindsSurface`
  (`unit/controller/editor_lifecycle_test.cpp`, already pinned at `106 + kNumEcosystemRosterParams` by T020) passes;
  the full per-push filter of `vorago_tests` passes.

## Group 18 — stage B gate (no code)

### T022 — Stage B verification

- Build `dsp_systems_tests`, `vorago_tests`, `Vorago` with **zero warnings** (a failing POST_BUILD copy to Program
  Files is acceptable, root `CLAUDE.md`). Per-push filters of both suites → pass.
- `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Vorago.vst3"` → exit 0.
- `./tools/run-clang-tidy.ps1 -Target vorago -BuildDir build/windows-ninja` and `-Target dsp` → 0 findings.
- `node tools/check-portability.js` → clean; then `wsl --shutdown`.
- Record in `compliance.md` `## Stage B` with the log lines.

---

## Stage C2 — preset infrastructure (FR-001, FR-018…FR-035, FR-039, FR-040)

## Group 19 — categories (existing header + test)

### T023 — `vorago_preset_config.h`: the seven categories (FR-001, SC-001 config half)

- **Test first** (`factory_preset_test.cpp`) `TEST_CASE("Vorago_FactoryPresets_CategoriesMatchConfig",
  "[vorago][preset]")`, config half: `subcategoryNames == {"Drones", "Abyss", "Caverns", "Organisms", "Machines",
  "Textures", "Ghosts"}` (order and bytes); `pluginName == "Vorago"`, `pluginCategoryDesc == "Synth"`,
  `processorUID == ::Vorago::kProcessorUID`; `makeVoragoPresetTabLabels() == {"All"} + those seven`. (T029 adds the
  directory half.)
- **Implement:** `plugins/vorago/src/preset/vorago_preset_config.h:29` → `/*.subcategoryNames  =*/{"Drones", "Abyss",
  "Caverns", "Organisms", "Machines", "Textures", "Ghosts"}`; the banner (`:3-12`) records the seven-name ruling
  (Clarifications 2026-09-27 Q6; additive-only; `Drones` verbatim). `makeVoragoPresetTabLabels()` (`:36-44`) unchanged.
- **Verify:** the case passes; `Vorago_PresetBrowser_SaveLoadRoundTrip` (`preset_browser_test.cpp:221`) still passes.

## Group 20 — definitions header (new file + static tests)

### T024 — `tools/vorago_preset_defs.h` (FR-010, FR-022, §5.5)

- **ODR first:** `VoragoPresetDef`, `CellSpec`, `CapabilityGroup`, `Capability`, `Verification`,
  `kRecordedDefaultStateCells`, `requiredPrimaryCells` → 0 hits; `ParamSetting` → only
  `Seraphis::PresetDefs::ParamSetting` (`tools/seraphis_preset_defs.h:59`).
- **Tests first** (`factory_preset_test.cpp`; expected values are literals typed from spec C-2.1, never read back
  from `cellSpecs()`):
  - `TEST_CASE("Vorago_PresetDefs_CellSpecsMatchSpec", "[vorago][preset]")`: `Capability::Count == 79`; group counts
    S 10 / M 12 / E 7 / D 50; S overrides (ID → normalized; `ablationCount`): S1 {300→0}, S2 {401→0}, S3 {700→0,
    701→0}, S4 {500→0}, S5 {610→0, 611→0, 612→0, 600→0} (4), S6 {1300→0}, S7 {900→0}, S8 {1105→0}, S9 {1400→0},
    S10 {1003→0}; M*m* {`99 + m` → 0.0} except M5 {104 → 0.5}; E6.hi {901 → 0.0} and E7.hi {902 → 0.25}, both
    `ExtReversion`; E1–E5 `RouteIsolated`; D13.1/D13.2 {800 → 0.5}, D14.1 {1500 → 0.0}, D14.2 {1502 → 0.0}, all
    `StateWithReversion`; `sConjunct` D1.*/D2 → S10, D3.*/D4.* → S1, D5.* → S2, D6.* → S4, D7.* → S5, D11/D12.1 → S9,
    D13.* → S7, D14.* → `Count`; D8.*/D9.* `AttackWindow`; D10.1 `FreezeGesture`; D10.2/D12.2 `StateOnly`; every S/M
    `Ablation`; D1–D7, D11, D12.1 `StateWithS`.
  - `TEST_CASE("Vorago_PresetDefs_ClaimsWellFormed", "[vorago][preset]")` over `allPresets()`: unique primaries; no
    primary in `kRecordedDefaultStateCells()`; no primary `D10FreezeHolds`; `D10FreezeHolds` claimed only by the
    `S8Cavern`-primary preset; secondaries exclude the primary and hold no duplicate; every `ParamSetting` finite
    (bit pattern) and in [0, 1]; no ID 4 or 5; no duplicate ID within a def; `category ∈ kCategories`;
    descriptions contain none of `" & < >`.
  - `TEST_CASE("Vorago_PresetDefs_RequiredPrimaries", "[vorago][preset]")`: `requiredPrimaryCells().size() == 40`
    and equals S1–S10 ∪ M1–M12 ∪ E1–E5 ∪ {E6.hi, E7.hi} ∪ the 9 non-default D1 materials ∪ {D8Growth,
    D9FastAttack} (P2-6's predicted set; T043 re-checks against the measured set).
  - `TEST_CASE("Vorago_FactoryPresets_LibraryShape", "[vorago][preset]")` (R-7(ii), SC-029, SC-031; **the one case
    allowed to stay red** until T047): `requiredPrimaryCells().size() == 40` (any other size is the §6.12 FR-017
    stop); `allPresets().size() == requiredPrimaryCells().size()`; each required cell is exactly one preset's
    primary; every one of the seven categories holds ≥ 3 presets.
  - `TEST_CASE("Vorago_PresetDefs_InfoXmlBytes", "[vorago][preset]")`: `buildVoragoInfoXml("Name", "Abyss", "Desc")`
    equals the literal bytes `preset_manager.cpp:265-277` writes for those values (attribute order MediaType,
    PlugInName, PlugInCategory, Name, MusicalCategory, MusicalInstrument, Comment); an empty description omits the
    `Comment` line.
- **Implement** §5.5 exactly: namespace `Vorago::PresetDefs`; every function `inline`; every table a function-local
  `static const`; project include `plugin_ids.h` only, plus std; `ParamSetting`, `CapabilityGroup`, `Capability`
  (§5.5 order, 79 + `Count`; D4 labels 1-based, `D4Type<t>` = `kNoiseTypeByIndex[t−1]`, `param_mapping.h:107-112`),
  `Verification`, `CellSpec`, `cellSpecs()`, `kRecordedDefaultStateCells()` initialised from P2-6 (D1StoneChamber,
  D1SteelTank, D2BlendBoth, D3Direct, D3FilteredWind, D3GranularDust, D3MetallicHiss, D4Type6, D5Hybrid, D6Lowpass,
  D7Div2, D8Standard, D9SlowAttack, D10FreezeOff, D12TriggersOff — corrected by T043), `requiredPrimaryCells()`
  **derived** from it, `VoragoPresetDef`, `allPresets()` returning an **empty** vector for now, `kCategories`,
  `buildVoragoInfoXml`.
- **Verify:** zero warnings; every case passes except `Vorago_FactoryPresets_LibraryShape` (0 presets).

## Group 21 — support part 2a (shared header)

### T025 — `preset_test_support.h`: Container, Info, Typed decode, Timeline (FR-028, FR-031, C-6, §5.8, §6.1)

- **Tests first** (`factory_preset_test.cpp`):
  - `TEST_CASE("Vorago_PresetSupport_DecodeDefaultSurface", "[vorago][preset]")`: `decodePresetState(getState of a
    fresh PresetHost)` → true; `version == 3`; `bytesConsumed == ::Vorago::kStateV3Bytes`; stage-time sum 155 000 ms,
    release 45 000 ms, mode Standard, decay 20 s, freeze 0, seed index 0, syncRate 0.0, selfAffinity −1.0; the
    stream truncated by one byte → false.
  - `TEST_CASE("Vorago_PresetSupport_TimelineDefault", "[vorago][preset]")`: `makeTimeline(default, false)` → `A ==
    155`, `rel == 45`, `rt60 == 20` (margin 1e-4), `sus = [160, 220]`, `m = {[160, 220], [220, 280], [280, 340]}`,
    `H == 340`, `tail = [410, 420]` (H + Rel + RT60 + 5 / + 15), `total == 420`, `freezeOnTail == false`;
    `makeTimeline(default, true)` → `tail = [395, 455]` (H + Rel + 10 / + 70), `freezeOnTail == true`; a decoded
    state switched to Growth with growth 30 s → `A == 30`.
  - `TEST_CASE("Vorago_PresetSupport_ParseVstPresetRejectsGarbage", "[vorago][preset]")`: a 10-byte file → `ok ==
    false`, non-empty `why`; a correct header with the list offset past EOF → `ok == false`.
- **Implement (§5.8 rows):** `PresetFile parseVstPreset(const std::filesystem::path&)` (magic `VST3`, version,
  32-char class id, list offset in bounds, `Comp` and `Info` present, every offset + size in bounds);
  `std::map<std::string, std::string> parseInfoAttributes(std::string_view)`; `struct DecodedPresetState` (one
  member per pack, as the processor holds them, plus `std::int32_t version; std::size_t bytesConsumed;`;
  non-copyable, filled through the out-param) and `bool decodePresetState(std::span<const std::uint8_t>,
  DecodedPresetState&)` calling the shipped `load*Params` in `getState()` order (`processor.cpp:643-659`) then
  `loadEcosystemParamsV3Ext`, true only when every loader returned true and exactly 436 bytes were consumed;
  `struct SweepTimeline {A, rel, rt60, sus0, sus1, m[3][2], H, tail0, tail1, total, freezeOnTail}` and
  `SweepTimeline makeTimeline(const DecodedPresetState&, bool freezeGesture)` per §6.1 with
  `effectiveCavernDecaySeconds` (T011) over the decoded **knob** macros.
- **Verify:** the three cases pass; zero warnings.

## Group 22 — support part 2b (shared header)

### T026 — Descriptor overload, Outcomes, Vector, predicates (C-7.2, C-7.4, FR-011a, FR-075; §6.4, §6.10, §6.12)

- **Tests first** (`factory_preset_test.cpp`):
  - `TEST_CASE("Vorago_PresetDescriptor_RefactorIsIdentity", "[vorago][preset]")`: on a fixed synthetic stereo signal
    (48 000 samples: 65.4 Hz + 440 Hz sines with a slow amplitude ramp; R phase-shifted), `describe(L, R, 48000)`
    equals `detail::describeImpl(L, R, 48000, std::nullopt)` component for component with `==` (same binary, not a
    stored digest); `describeWithEnergyFloor(L, R, 48000, -1000.0)` equals `describe`; with a floor above every
    one-second value `energySpread == 0`.
  - `TEST_CASE("Vorago_PresetMatrix_NonSubsetRule", "[vorago][preset]")` — the plan §6.12 hand-built cases: claim X
    with `1.5 < d < 4.0` and every conjunct true → SUBSET (`std::nullopt`); `d = 1.0` → witness X; `2·s(Q) > d` →
    witness X; `StateWithS` D3.2 held with S1 true, `rendered = false`, `d = 0` → SUBSET; S1 false → witness D3.2;
    state false → witness D3.2; `StateOnly` D12.2 true → SUBSET, false → witness; `AttackWindow` secondary D9.2
    true without render → SUBSET; D10.1 with `stateOk` false → witness D10.1; `verifiedAt(…, Primary)` on a D1 entry
    with state and S10 true: `d = 3.9` → false, `d = 4.1` with `twoS ≤ 4.1` → true; a D3 entry at `Primary` → false
    whatever `d` holds.
  - `TEST_CASE("Vorago_PresetDefs_EExtSidePredicate", "[vorago][preset]")`: normalized 901 = 0.5 (plain 0.25) passes
    E6.hi, 0.49 fails; 902 = 0.625 (plain +0.5) passes E7.hi, 0.62 fails; defaults (0.0 / 0.25) fail both.
  - `TEST_CASE("Vorago_PresetDefs_DStatePredicates", "[vorago][preset]")`: D1 — A = Glass with blend 0.65 credits
    D1Glass, blend 0.66 does not; B = Ice with blend 0.35 credits D1Ice, 0.34 does not; D2 true at 0.35 and 0.65,
    false at 0.66; D4 — a `Direct` slot of type index t−1 credits `D4Type<t>`, a non-Direct slot with that type does
    not; D7 — f/2 −18, f/4 −24, fifth −30 → D7Div2 only; f/2 == f/4 == −18 → neither (ties fail); D9 — A = 10 →
    D9FastAttack, 10.01 → not, A = 90 → D9SlowAttack; D13 — rate scale 0.3 → D13SlowEvents, 3.0 → D13FastEvents;
    D14 — breathing 0.7 → D14Breathing, 0.69 → not; M displacement — macro 0.5 → displaced, 0.49 → not; Gravity 0.85
    and 0.15 → displaced, 0.84 → not.
- **Implement (§5.8 Descriptor / Outcomes / Vector, §6.4, §6.10, §6.12):** move `describe`'s body verbatim into
  `detail::describeImpl(L, R, sr, std::optional<double> energyFloorDb)` (the floor clamps each one-second stereo dB
  value before the spread); `describe` forwards `std::nullopt`; add `describeWithEnergyFloor` (never a −∞ sentinel:
  the macOS leg and the generator build with `-ffast-math`); `#include "vorago_preset_defs.h"`; `enum class
  ClaimRole : std::uint8_t {Secondary, Primary}`; `struct CellOutcome {bool stateOk, conjunctOk, rendered; double d,
  twoS, attribBase /* < 0: no attributability term */; std::string skip;}`; `bool verifiedAt(const CellOutcome&,
  Verification, ClaimRole) noexcept` per the §6.12 "`verifiedAt` by kind" table; `struct VerificationVector`;
  `std::optional<Capability> findWitness(const VoragoPresetDef& p, const VerificationVector& q, Capability
  qPrimary)` (primary first, then secondaries in definition order; `c == qPrimary` also needs the Primary role);
  `bool statePredicate(Capability, const DecodedPresetState&)` for every §6.10 row; the M displacement conjunct
  (≥ 0.5; Gravity `|g − 0.5| ≥ 0.35`); the FR-075 side predicate (`n ≥ n₀ + 0.5·(1 − n₀)`).
- **Verify:** the four cases pass; T007's E0 case still compiles.

## Group 23 — host: the C-4 drive (shared header)

### T027 — `vorago_preset_host.h`: `buildPresetComponentState` (FR-021, C-4)

- **Test first** (`factory_preset_test.cpp`) `TEST_CASE("Vorago_PresetHost_BuildPresetComponentState",
  "[vorago][preset]")`: a def with `{kMasterGainId, 0.25}` and `{kEcosystemSyncRateId, 1.0}` → true; `comp.size() ==
  436`; `decodePresetState(comp)` → syncRate == 0.5, master gain == the `global_params.h` mapping of 0.25; rejected
  (false, non-empty `why`): value 1.0000001; −0.1; NaN by bit pattern; a point for ID 4; a point for ID 5; a
  duplicate ID; the empty def → `comp` equals a fresh host's `saveState` bytes.
- **Implement:** `bool buildPresetComponentState(const Vorago::PresetDefs::VoragoPresetDef&,
  std::vector<std::uint8_t>& comp, std::string& why)` — validate first, then a `PresetHost` `prepare(48000, 512)` →
  **one** `process(512, nullptr, &changes)` carrying every point at offset 0 (`Krate::Test::ParameterChanges`) →
  `saveState(comp)`. The host now includes `vorago_preset_defs.h`.
- **Verify:** the case passes; zero warnings.

## Group 24 — generator body (skeleton owned by this task)

### T028 — `tools/vorago_preset_generator.cpp` (FR-018…FR-020, FR-023, FR-025, §5.7)

- **Test first:** `build/bin/vorago_preset_generator f:/tmp/p14/gen` returns 1 today (skeleton).
- **Implement** per §5.7 (following `tools/seraphis_preset_generator.cpp:77-126`, `:224-349`, without its partials
  block): `void* moduleHandle = nullptr;`; class id from `::Vorago::kProcessorUID.toString(buf)`, exactly 32 chars
  required; output base `argv[1]` (default `plugins/vorago/resources/presets`); create the seven `kCategories`
  directories; iterate `allPresets()` in definition order → `buildPresetComponentState` → write the 48-byte header +
  `Comp` + `Info` (`buildVoragoInfoXml`) + `List` layout of Seraphis's `writeVstPreset` (`:224-265`), duplicated
  into this TU's anonymous namespace (the Seraphis tool is not edited); print `wrote N presets`; exit 1 on any
  failure; no timestamp, no directory iteration, no RNG (FR-023). No Catch2 anywhere in the include graph.
- **Verify:** build `vorago_preset_generator` zero warnings (MSVC); run on `f:/tmp/p14/gen` → exit 0, seven
  directories, `wrote 0 presets`; a **WSL/GCC build of the target** succeeds (FR-025), then `wsl --shutdown`.

## Group 25 — per-push harness cases (factory_preset_test.cpp)

### T029 — Container, Info, round-trip, browser, stream shape, tree, parameter space (FR-002…FR-009, FR-014, FR-028…FR-032, FR-035)

- **Tests** (they hold on 0 presets and bite from T037 on):
  - `Vorago_FactoryPresets_CategoriesMatchConfig` **directory half**: under `VORAGO_RESOURCES_DIR "/presets"` the
    directory set == the seven names both ways; every regular file is a `.vstpreset` directly inside a category
    directory, or a `.gitkeep` in a category directory holding no preset.
  - `TEST_CASE("Vorago_FactoryPresets_ContainerAndInfo", "[vorago][preset]")` (SC-002): every file passes
    `parseVstPreset`; class id == `kProcessorUID`'s 32 chars; `Info` bytes == `buildVoragoInfoXml(def)`; and,
    independently, the parsed attributes against literals: `MediaType == "VstPreset"`, `PlugInName == "Vorago"`,
    `PlugInCategory == "Synth"`, `Name ==` file stem, `MusicalCategory == MusicalInstrument ==` parent directory,
    `Comment ==` the definition's description (absent iff empty), in the attribute order of
    `preset_manager.cpp:266-275`; the on-disk file set == `allPresets()` (count and names).
  - `TEST_CASE("Vorago_FactoryPresets_InfoMatchesSavePreset", "[vorago][preset]")`: per category a `PresetHost`
    loaded with the first definition of that category (the default surface while the category has none);
    `Krate::Plugins::PresetManager(makeVoragoPresetConfig(), &host.processor(), nullptr, tempUserDir)`
    (`preset_manager.h:55-61`) → `savePreset(name, category, description)` (`preset_manager.cpp:227-277`); the
    written file's `Info` (via `parseVstPreset`) is byte-equal to `buildVoragoInfoXml(name, category,
    description)`; one more call with an empty description covers the no-`Comment` branch (`:273-275`).
  - `TEST_CASE("Vorago_FactoryPresets_RoundTrip", "[vorago][preset]")` (SC-003): `loadState(Comp) == kResultOk`,
    then `saveState` byte-identical to `Comp`.
  - `TEST_CASE("Vorago_FactoryPresets_BrowserScan", "[vorago][preset]")` (SC-004): `PresetManager(
    makeVoragoPresetConfig(), nullptr, nullptr, tempUserDir, factoryRoot)` → `scanPresets()` count == N; all
    `isFactory`; no empty `subcategory`; `getPresetsForSubcategory(c)` count == defs in c, all seven.
  - `TEST_CASE("Vorago_FactoryPresets_StreamShape", "[vorago][preset]")` (SC-005): per file decode true, version 3,
    length 436; `PresetManager::isValidPresetName(name)`; unique, ASCII, no path separator; polyphony index ≤ 3;
    `A ≤ 180 s`; `Rel ≤ 60 s`; every stored float finite by bit pattern.
  - `TEST_CASE("Vorago_FactoryPresets_TreeToleranceProbe", "[.measure][vorago]")`: regenerate each def through
    `buildPresetComponentState`; print per float field the worst `|c − r| / max(|c|, 1e-30)` (§6.15).
  - `TEST_CASE("Vorago_FactoryPresets_TreeMatchesGenerator", "[vorago][preset]")` (SC-006): same path set;
    byte-identical `Info`; version 3; length 436; every int32 field equal; every float field within
    `kTreeFloatRelTol` — a named constant in this TU, **initially `1.19e-7`** (§6.15's one-ULP floor), re-pinned by
    T048 to `max(10 × worst over MSVC/GCC/AppleClang, 1.19e-7)`; prints its per-field worst on every leg.
  - `TEST_CASE("Vorago_PresetMatrix_ParameterSpaceDistinct", "[vorago][preset]")` (SC-009, §6.14): one
    `::Vorago::Controller` initialised; per preset `setComponentState(comp)`, then `getParamNormalized` for every
    registered ID except 4 and 5 (108 IDs); list IDs (`stepCount > 0`) differ iff `round(n·stepCount)` differs,
    continuous iff `|Δn| ≥ 0.10`; every pair and every preset vs the fresh controller ≥ 8 differing IDs; prints the
    minimum count and its pair.
- **Verify:** zero warnings; per-push filter → every case passes except `Vorago_FactoryPresets_LibraryShape`.

## Group 26 — per-push render guards ([P], disjoint files)

### T030 [P] — `tail_samples_test.cpp` case (v): every factory preset (FR-060, SC-030)

- **Extend** `Vorago_Processor_GetTailSamplesMatchesState`: for every def in `allPresets()`, a processor loaded with
  its `Comp`; expected = `llround(tailSeconds(decoded release ms, effectiveCavernDecaySeconds(decoded knob macros,
  decoded decay)) · 48000)` from `decodePresetState(comp)` (independent of the processor's atomics), or
  `kInfiniteTail` when decoded freeze is On; `|reported − expected| ≤ 1`.
- **Verify:** passes (the loop is empty until T037; cases (i)–(iv) still bite).

### T031 [P] — `preset_sweep_test.cpp`: `Vorago_PresetSweep_ShortBounded` (FR-034, SC-014; per-push)

- **Test** `TEST_CASE("Vorago_PresetSweep_ShortBounded", "[vorago][preset]")`: for the default surface and every
  def, `renderPreset` with note 36 for 8 s at 48 kHz, 4 s at 44.1 kHz, 4 s at 96 kHz, and the `kCpuNotes` chord for
  8 s at 48 kHz with `forcePolyIndex = 3`; each `finite` and `peak ≤ 0.9661f`; jobs via `runJobs` with 2 threads;
  assertions on the test thread.
- **Verify:** passes on the default surface; bites from T037.

### T032 [P] — `preset_load_rt_test.cpp`: RT-safe loading (FR-039, FR-040, SC-016)

- **Tests:**
  - `TEST_CASE("Vorago_FactoryPresets_SequentialLoadNoAlloc", "[vorago][preset]")`: warm `ProcessorFixture` (48 kHz
    / 512, NoteOn 36 held, 20 blocks); per preset plus a default-surface stream (the loop is never empty):
    `setState(comp)` outside any scope, then an `AllocationScope` around each of the next 4 `processBlock` calls,
    the count read inside the scope → 0.
  - `TEST_CASE("Vorago_FactoryPresets_ConcurrentLoadIsRtSafe", "[vorago][preset]")`: streams prebuilt (default
    surface included); a message thread loops `setState` over them until stopped; the audio thread renders 4 s
    under `ThreadScopedAllocationScope` → 0 allocations; every `setState` returns `kResultOk`; output finite by bit
    pattern and `≤ 0.9661f`. The global `AllocationScope` is forbidden here (FR-040).
- **Verify:** both pass.

---

## Stage D — sweep harness mechanics (per-push unit cases; the long cases follow G2)

`kRuledTakes` does not exist until T040, so every stage-D function takes the take count as a parameter and every
stage-D test is a fast per-push case on synthetic data or short renders. The `[long][vorago-sweep]` cases are T041.

## Group 27 — records and sharding (shared header + sweep TU)

### T033 — Record, Shard (§5.8 rows Record / Sharding)

- **ODR first:** `SweepRecord`, `TakeRecord`, `GestureResult`, `RateResult`, `Shard`, `parseShard` → 0 hits.
- **Tests first** (`preset_sweep_test.cpp`, per-push):
  - `TEST_CASE("Vorago_PresetSupport_RecordRoundTrip", "[vorago][preset]")`: a hand-filled `SweepRecord` (2 takes,
    a vector mixing outcomes and skip strings, gesture and rate results) → `writeRecord` to a temp file →
    `readRecord` → every double `==` (written `%.17g`: a text round-trip inside one binary, not a golden), every
    string and bool equal; a truncated file → `readRecord` false.
  - `TEST_CASE("Vorago_PresetSupport_ShardParse", "[vorago][preset]")`: `parseShard("3/10") == {3, 10}`,
    `("0/1") == {0, 1}`; `"10/10"`, `"x"`, `"3/"`, `"3/0"`, `""` → `{0, 0}`; `inShard(i, {3, 10})` iff `i % 10 == 3`;
    `shardFromEnv()` with the variable unset → `{0, 1}`.
- **Implement:** `TakeRecord` (§5.8), `GestureResult {loudestDb, lastDb, floorDb, dryLoudestDb, pass}`, `RateResult
  {finite441, peak441, worstHi441, finite96, peak96, worstHi96}`, `SweepRecord` (§5.8 fields + `int takes`),
  `writeRecord`/`readRecord` (text `key value…` lines); `Shard`, pure `parseShard(std::string_view)`,
  `shardFromEnv()` = `parseShard(sweepEnv("VORAGO_SWEEP_SHARD").value_or("0/1"))`, `inShard`. Index N (= number of
  defs) is the default-surface pseudo-preset. Records are transient CI artifacts, never committed (C-8).
- **Verify:** both cases pass.

## Group 28 — windows, arms, main take (shared header + sweep TU)

### T034 — §6.2 windows, §6.3 arms, the K-take main render (C-6, FR-033)

- **Tests first** (`preset_sweep_test.cpp`, per-push, synthetic block-power vectors at 48 kHz / 512):
  - `TEST_CASE("Vorago_PresetSupport_WindowRule", "[vorago][preset]")`: `[a, b]` with `b − a = 25 s` → exactly 3
    windows `[a, a+10]`, `[a+10, a+20]`, `[b−10, b]`; `b − a = 30` → exactly 3; every window snapped inward to block
    edges with ≤ 511 samples slack per edge.
  - `TEST_CASE("Vorago_PresetSupport_ArmsOnSynthetic", "[vorago][preset]")`: constant −20 dBFS over the hold with a
    −70 dBFS Freeze-Off tail → arms 1–3 pass (late − early = 0 dB) and arm 4 passes (−70 ≤ −20 − 40); tail at −59 →
    arm 4 fails; one 10 s window at −5 → arm 1 fails (≤ −6 rule); a window at −61 inside `[A, H]` → arm 2 fails; late
    window 13 dB above `Sus` → arm 3 fails; Freeze-On tail flat −30 with `Sus` −20 → passes; flat −41 → fails the
    −20 dB floor; rising 1.5 dB over the six windows → fails non-growing; an all-floor (−240) tail → fails the floor.
- **Implement:** `struct ArmResult {finite, peak, worstHiDb, worstLoDb, lateVsSusDb, tailDb, tailLastDb,
  tailLoudestDb, pass1, pass2, pass3, pass4}`; `ArmResult evaluateArms(const SweepCapture&, const SweepTimeline&,
  double sr)` (§6.2: stereo power `(ΣL² + ΣR²)/(2n)`, dB `10·log10(max(p, 1e-24))`); `TakeRecord renderTake(const
  std::vector<std::uint8_t>& comp, const SweepTimeline&, int seedIndex, std::optional<double> attackWindowEnd)`
  (NoteOff at `H`, end `Total`, captures `M1..M3` and optionally `[0, W_end]`, fills the minute descriptors and the
  arm figures); `std::vector<TakeRecord> computeTakes(comp, tl, storedSeed, int K, unsigned threads)` rendering the
  K takes of `A_K` through `runJobs`.
- **Verify:** both cases pass.

## Group 29 — twins and the verification vector (shared header + sweep TU)

### T035 — §6.7 ablation, §6.8 attack window, §6.9 routes, §6.11 skips, vector fill (FR-012, FR-037)

- **Tests first** (`preset_sweep_test.cpp`, per-push):
  - `TEST_CASE("Vorago_PresetSupport_TwinOverrides", "[vorago][preset]")`: `twinOverrides(S5Sub, …)` == {610→0,
    611→0, 612→0, 600→0}; `routeOverrides(E3NoiseWake)` == the S-overrides of S6, S2, S4, S9 (destinations E1↔S6,
    E2↔S2, E3↔S1, E4↔S4, E5↔S9); the `R_∅` set == all five destinations; the `⁰` variants add 900 → 0.0; D1
    per-material reversion for Material A = Glass → {1004 → `indexToNormalized(5, 11)`}; envelope reversion of a
    Growth preset → {1200 → 0.0}.
  - `TEST_CASE("Vorago_PresetSupport_SkipRules", "[vorago][preset]")` (T001 item 5 default; if ruled "render
    regardless", assert instead that `skipReason` only annotates and every render runs): override == stored for
    every ID → `"override equals stored"` with `d = 0`, not verified; M displacement 0.4 → `"M displacement"`; 900
    stored 0.0 → E1–E5 `"ecosystem depth 0"`; D state false → `"state false"`; E6.hi side false → `"side
    predicate"`; otherwise empty.
  - `TEST_CASE("Vorago_PresetSupport_VectorOnShortTimeline", "[vorago][preset]")`: `computeVerificationVector` on
    the default surface with an injected test timeline `A = 2, M1 = [2, 12], M2 = [12, 22], M3 = [22, 32], H = 32`
    and K = 1 → 79 entries; every render-scored entry `rendered == true` or a non-empty `skip`; every `d` finite;
    every state-only entry `rendered == false` with `skip == "state-only kind"`. Mechanics only (no verdicts on a
    shortened timeline); runs in < 90 s locally.
- **Implement (§6.7–§6.11):** `twinOverrides(Capability, const DecodedPresetState&)`, `routeOverrides(…)`; a
  single-take twin render to `Sus.end` at the stored seed with the overrides as offset-0 block-0 points; twin `s` =
  `d(describe(first half of Sus), describe(second half))`; `P_Sus` = `describe(M1)` of the stored-seed take; the
  §6.8 attack-window reversion (`W_end = max(A_P, A_rev) + 5`, `P_rev` rendered to `max(W_end, A_rev + 65)`,
  `floorDb = RMS(Sus_P) − 60`, `d_att`, `d_Sus`, printed time to first reach `RMS(Sus) − 6 dB`) — rendered only for
  the D8.2/D9.1 **primary** presets; route-isolated E (12 renders per preset, `attribBase = d(R_∅, R_∅⁰)`); `std::string
  skipReason(…)` per §6.11; `VerificationVector computeVerificationVector(const VoragoPresetDef* def /* nullptr =
  default surface */, const std::vector<std::uint8_t>& comp, const SweepTimeline& tl, double selfDistance, const
  PresetDescriptor& pSus, unsigned threads)` filling every `CellOutcome` (state predicates from T026; `conjunctOk`
  of `StateWithS` from the same vector's S entry at the secondary bar; D10.1 is T036's).
- **Verify:** the three cases pass.

## Group 30 — gesture, rates, reproducibility, controls, record assembly (shared header + sweep TU)

### T036 — §6.3 freeze gesture, FR-033a rates, FR-038 reproducibility, §6.13 controls, `computeSweepRecord`

- **Tests first** (`preset_sweep_test.cpp`, per-push):
  - `TEST_CASE("Vorago_PresetSupport_FreezeGestureBlock", "[vorago][preset]")`: `freezeGestureBlock(155.0, 48000,
    512)` → block 20 625, start sample 10 560 000 (exact); `freezeGestureBlock(10.3, 48000, 512)` → block 7 060,
    start 3 614 720, lateness 320 samples (≤ 511, i.e. ≤ 10.7 ms).
  - `TEST_CASE("Vorago_PresetSupport_ControlSet", "[vorago][preset]")`: on hand-built records the control set = argmax
    and argmin `s(P)`, the S8 preset, highest stored Pressure (106), highest stored Weight (107), deduplicated;
    fewer than 3 distinct → reported as failure (FR-017 path); level-twin math on T026's synthetic signal: the
    buffers scaled by `10^(−6/20)` score `d < 0.05` against the unscaled.
- **Implement:** gesture render G (stored seed, `freezeAt = A + 65`, rendered to `H + Rel + 70`; arm 1 + Freeze-On
  arm 4 with `RMS(Sus)` from the **ungestured** stored-seed take) and dry-residue twin G₀ (`kSpaceMixId` 1105 → 0.0 at
  block 0; loudest 10 s over the same `Tail` ≤ `RMS(Sus) − 40`) filling `GestureResult` and the D10.1 entry
  (`stateOk` = "this def's primary is S8"); rates: stored-seed renders over `[0, A + 65]` at 44.1 and 96 kHz, arm 1
  only; reproducibility: two fresh hosts on two threads over `[0, A + 65]` at the stored seed,
  `compareFingerprints(…).withinTolerance()` per channel; control helpers `controlSet(records)`, `levelTwinD`,
  `gainTwinD` (0 → `0.5 × stored normalized`), `subTwinD` (600 at `stored ± 0.125`, in-range sides only, sides
  recorded), `seedTwinTK` (render `B_K`, `d(D_A, D_B)`) — all same-seed per T001 item 2 except (b); `SweepRecord
  computeSweepRecord(std::size_t defIndex, int takes, unsigned threads)` assembling timeline, takes (arms 1–4 on
  every take), `D(P)` = `meanOf` of the 3K minute descriptors, `s(P)` from the K-take-averaged minutes (§6.5), level
  twin, vector, gesture (S8 preset only), rates, reproducibility.
- **Verify:** both cases pass; zero warnings; per-push green except LibraryShape.

---

## Stage E — pilot and gate G2 (FR-017a)

## Group 31 — pilot presets (shared defs header)

### T037 — Author the six pilot presets (§6.16 P1–P6 = §7 rows 28, 29, 5, 8, 39, 30)

- **Edit:** `tools/vorago_preset_defs.h` `allPresets()`:
  - ~~"Locked Choir" — Organisms — primary `E6SyncRateHi`~~ (withdrawn at gate G2, 2026-09-29: E6.hi is a secondary; its former definition: 901 normalized ≥ 0.5 (plain ≥ 0.25; author at 1.0 = the
    counted extreme 0.5), colony-forward per P2-5: Life (109) high, Ecosystem Depth (900) 1.0, the sections the colony
    drives (noise, resonance, ecology levels) prominent.)
  - ~~"Clotting Colony" — Organisms — primary `E7SelfAffinityHi`~~ (withdrawn at G2: E7.hi is a secondary; former definition: 902 normalized ≥ 0.625 (plain ≥ +0.5; author at 1.0
    = +2, the measured extreme at Life max); Life high, Depth 1.0, colony-driven sections prominent.)
  - "Tectonic Floor" — Abyss — primary `S5Sub`; secondary `D7FifthBelow` (fifth-below strictly loudest).
  - "Cathedral Void" — Caverns — primary `S8Cavern`; secondary `D10FreezeHolds`; Freeze stored **Off**; long decay;
    `Comment` includes "Engage Freeze once the drone has bloomed".
  - "Growth Ring" — Organisms — primary `D8Growth`; mode Growth, growth duration ≤ 180 s.
  - "Glass Well" — Caverns — primary `D1Glass`; Material A = Glass with blend ≤ 0.65 (or B with blend ≥ 0.35), body audible.
  Every row: polyphony index ≤ 3 (normalized ≤ 0.6); `A ≤ 180 s`, `Rel ≤ 60 s`; Freeze Off; no ID 4/5; output
  saturation only via its MB base (C-5); description free of `" & < >`; ≥ 8 IDs displaced ≥ 0.10 from the default
  and from each other (C-7.1). Library order is finalised in T044–T047 (they insert rows around these).
- **Then:** build and run `generate_vorago_presets` (six files into the committed tree). Keep
  `presets/Drones/.gitkeep` (Drones holds no preset yet).
- **Verify:** per-push filter → green except `Vorago_FactoryPresets_LibraryShape` (6 of 40).

## Group 32 — pilot calibration case (pilot TU)

### T038 — `Vorago_PresetPilot_Calibrate` (FR-017a, §6.16 steps 2–5)

- **Test (the measurement; hidden):** `TEST_CASE("Vorago_PresetPilot_Calibrate", "[.probe][vorago]")` in
  `integration/preset_pilot_test.cpp`.
- **Implement:**
  - timeline cross-check: `makeTimeline(decode(default getState), false)` gives `A = 155`, `M1–M3` and `H = 340`
    exactly as T007's hard-coded constants — `REQUIRE`d;
  - P0 = default surface; P1…P6 = the six T037 defs by name; P3′ = "Tectonic Floor" with 600 moved +0.125
    normalized (+6 dB) and one small section tweak (`kSmearAmountId` + 0.05), built **in this TU**, never added to
    `allPresets()`;
  - render every P0–P6 at all 16 seed indices (`A_8 ∪ B_8`) on its own timeline via `computeTakes` / `runJobs`;
    `t_K` for K ∈ {1, 2, 4, 8}; **rule K** = the smallest K with `2·t_K ≤ 4.0` for every P0–P6;
  - with K: `d(P3, P3′)` over `A_K`; P1 and P2 primary scores through their §6.7 reversions (901 → 0.0 / 902 → 0.25)
    at the stored seed, `s(P)` from `A_K`, bar `max(4.0, 2·s(P))`; P3 (S5 ablation), P4 (S8 ablation), P5 (§6.8
    attack window), P6 (per-material reversion) scored likewise;
  - print each preset's `t_K` curve, the ruled K (or `NONE`), `d(P3, P3′)` with `ok | STOP (Q6)`, each preset's
    `s(P)`, primary `d`, bar and verdict, and `G2: PROCEED with K = <k> | STOP (<reason>)`;
  - **assertions (only):** every render finite with peak ≤ 0.9661; the timeline cross-check.
- **Verify:** zero warnings; listed by `--list-tests`; hidden per push.

## Group 33 — run the pilot (gate G2)

### T039 — Run the pilot alone; rule K; STOP on any failure (FR-017a, FR-017, Q5, Q6)

**Main-loop note (2026-09-29):** run 1 (`artifacts/pilot_calibrate.log`) stopped at G2 and its three fronts were ruled by the user (spec Clarifications "Gate G2 rulings"); the main loop encoded the rulings, re-authored Growth Ring (v3) and Glass Well (v4) through the new `Vorago_PresetPilot_PrimaryProbe` case, and ran the pilot again: **run 3 = `artifacts/pilot_calibrate_run3.log`, `G2: PROCEED with K = 4`**, every pilot primary at or above F = 4.0, the near-variant floor holding at 1.64 (compliance "Pilot run 3 — gate G2"). T040 freezes **K = 4** from that log; nothing here is re-run by an agent.

- **Run:** `VORAGO_SWEEP_THREADS=4 vorago_tests.exe "Vorago_PresetPilot_Calibrate" > f:/tmp/p14/pilot.log 2>&1`
  alone (§10: ≈ 20 min on 4 threads). Copy to `artifacts/pilot_calibrate.log`.
- **Record** `## FR-017a pilot / G2` in `compliance.md`: every `t_K` curve, the ruled K, `d(P3, P3′)`, each
  candidate's score, the wall clock.
- **STOP and surface (FR-017) if:** no K ≤ 8 works for every preset (surface all curves; K never past 8, F never
  raised, nothing silently retried); `d(P3, P3′) ≥ 4.0` (Q6); P1 or P2 below `max(4.0, 2·s)` (Q5) — a further
  colony-forward re-author only if the user rules it. Another pilot primary below its bar may be re-authored (C-2.2),
  logged, and the pilot re-run; a persistent failure is FR-017.
- **Proceed** only on `G2: PROCEED`.

## Group 34 — freeze K (shared header)

### T040 — `kRuledTakes` and the memo (§5.8 Memo)

- **Test first** (`preset_sweep_test.cpp`) `TEST_CASE("Vorago_PresetSupport_RuledTakes", "[vorago][preset]")`:
  `static_assert(kRuledTakes >= 1 && kRuledTakes <= kMaxTakes)`; `REQUIRE(kRuledTakes == <K from T039>)` with a
  comment citing `compliance.md` G2 and `artifacts/pilot_calibrate.log`.
- **Implement:** `inline constexpr int kRuledTakes = <K>;` in `preset_test_support.h` with the citation;
  `const SweepRecord& sweepRecordFor(std::size_t defIndex)` — function-local static cache (a `std::mutex` held only
  while inserting; test threads only); `VORAGO_SWEEP_IN` set → load `<dir>/record_<defIndex>.txt` (a missing file is
  reported to the caller, which REQUIREs); otherwise `computeSweepRecord(defIndex, kRuledTakes, threads)`;
  `VORAGO_SWEEP_OUT` set → write each computed record to `<dir>/record_<defIndex>.txt`.
- **Verify:** the case passes; per-push green except LibraryShape.

## Group 35 — the long sweep cases (sweep TU)

### T041 — `[long][vorago-sweep]` cases (FR-033, FR-033a, FR-037, FR-038; SC-011, SC-012, SC-015, SC-022, SC-024)

- **Tests** in `integration/preset_sweep_test.cpp`; each iterates the definition indices `0…N−1` in
  `shardFromEnv()` (plus the pseudo-preset index N where stated), `REQUIRE(shard.count > 0)`, runs jobs via
  `runJobs`, asserts on the test thread, prints per preset:
  - `TEST_CASE("Vorago_PresetSweep_LongRender", "[vorago][preset][long][vorago-sweep]")` — arms 1–4 on every take
    of `A_K`; prints A, Rel, RT60 and each arm's figure per take.
  - `TEST_CASE("Vorago_PresetSweep_FreezeGesture", "[vorago][preset][long][vorago-sweep]")` — the S8 preset (skipped
    when not in the shard): G passes arm 1 and Freeze-On arm 4 including the −20 dB floor; G₀ loudest ≤ `RMS(Sus) −
    40`; prints G loudest/last/floor, G₀ loudest, G − G₀.
  - `TEST_CASE("Vorago_PresetSweep_SustainAtAllRates", "[vorago][preset][long][vorago-sweep]")` — arm 1 over `[0, A +
    65]` at 44.1 and 96 kHz.
  - `TEST_CASE("Vorago_PresetSweep_AblationVerifiesClaims", "[vorago][preset][long][vorago-sweep]")` — includes index
    N (the default surface: vector recorded, no claims); every claim verifies at its role's bar (`verifiedAt`);
    prints every vector entry's raw terms and skip reason.
  - `TEST_CASE("Vorago_PresetSweep_RendersAreReproducible", "[vorago][preset][long][vorago-sweep]")` —
    `withinTolerance()` per channel.
- **Verify:** zero warnings; `vorago_tests.exe "~[performance]~[perf]~[benchmark]~[!benchmark]~[long]" --list-tests`
  lists none of the five. From now on never run `vorago_tests.exe` unfiltered.

## Group 36 — the aggregate cases (matrix TU)

### T042 — Coverage, non-subset, sound-space distinctness + controls (FR-011a, FR-013, FR-015, FR-036; SC-008, SC-010, SC-018, SC-028, SC-029)

- **Tests** in `integration/preset_matrix_test.cpp`, all `[vorago][preset][long][vorago-sweep][vorago-aggregate]`,
  records for every index `0…N` via `sweepRecordFor` (REQUIRE one per index, so a missing shard fails loudly):
  - `Vorago_PresetMatrix_CoverageComplete` (§6.12 "Coverage"): every cell has ≥ 1 factory verifier (indices 0…N−1);
    E1–E5, E6.hi, E7.hi each the verified primary at 4.0 of a distinct preset (seven); no primary in the **measured**
    default-state set (the pseudo-preset's verified D cells at the secondary bar); no primary D10.1; measured set ==
    `kRecordedDefaultStateCells()`; the primary set == `requiredPrimaryCells()` recomputed from the measured set;
    ≥ 1 verified D11 and D12.1 (SC-018); prints the full preset × cell matrix (primary / secondary / claimed-failed
    / unclaimed) and the default-state cells.
  - `Vorago_PresetMatrix_NoShowcaseSubset`: every ordered pair (P, Q) has a witness; prints `P vs Q: witness <cell>`
    or `SUBSET`.
  - `Vorago_PresetSweep_SoundSpaceDistinct` (§6.6, §6.13): control set C (≥ 3 distinct, printed); (a) every preset's
    level twin `d < 0.05`; (a′) gain twin on the highest-Pressure preset `d ≤ 2.0`; (b) seed twins `t_K ≤ 2.0` per C
    preset, `t_max` = the largest; (c) sub twins `d < 4.0` on the in-range side(s) (printed); then all C(N, 2) pairs
    `d(P, Q) ≥ max(4.0, 2·max(s(P), s(Q)), 2·t_max)`; prints min / median / max `d`, the minimum pair's names, every
    `s(P)`, `t_max`, K and the effective floor. A control failure is an FR-017 stop, never a widening.
- **Verify:** zero warnings; not in the per-push list.

## Group 37 — measured default-state set (shared defs header)

### T043 — Measure the default-state set before authoring; correct the constant or STOP (§6.12, §7 step 2, §11)

**RULED 2026-09-29 (main loop):** the measured set is 9 cells (`artifacts/default_state_vector.log`; D3.1–D3.4, D4.6 and D6.1 fell out: S1 d 0.0110, S4 d 0.0009 on the default surface) → |requiredPrimaryCells()| = **42**; the user accepted **N = 42**. `kRecordedDefaultStateCells` now holds the 9 measured cells; the RequiredPrimaries / LibraryShape counts are 42; Stage F authors four noise-model rows (D3.1 Direct, D3.2 FilteredWind, D3.3 GranularDust, D3.4 MetallicHiss), each with an audible noise organism (S1 verified as its secondary at 1.5) and D4.6 / D6.1 attached as secondaries where the noise is audible, keeping every category ≥ 3. Spec Clarifications "T043 ruling". Nothing here is re-run by an agent.

- **Run** (alone, logged): `VORAGO_SWEEP_SHARD=6/7 vorago_tests.exe "Vorago_PresetSweep_AblationVerifiesClaims" >
  f:/tmp/p14/default_state.log 2>&1` — with the six pilot defs, index N = 6 is the pseudo-preset and `6 mod 7 == 6`
  selects it alone. Copy to `artifacts/default_state_vector.log`.
- **Decide:**
  - measured set == P2-6's prediction → record; no edit;
  - it differs but `|requiredPrimaryCells()|` recomputed from it is still 40 → set `kRecordedDefaultStateCells()` in
    `tools/vorago_preset_defs.h` to the measured set and re-derive §7's secondary attachments in the compliance record;
  - `|requiredPrimaryCells()| ≠ 40` (e.g. S1 fails on the default surface → D3.* need primaries → 44; S10 fails →
    D1.StoneChamber/SteelTank need primaries → 42) → **FR-017 STOP** with the measured set. N and the primary list
    are not re-chosen by the executor.
- **Verify:** `Vorago_PresetDefs_RequiredPrimaries` updated to the recorded set (still 40) and passing.

---

## Stage F — the matrix-derived library (FR-016; plan §7, **N = 42** after the T043 ruling: 38 after G2 — rows 28/29 withdrawn, E6.hi / E7.hi carried as secondaries by Organisms rows that store the knobs at the Q7 margin on a Life-high surface — plus four noise-model rows D3.1–D3.4, each with an audible noise organism)

Authoring constraints for **every** row (§7): polyphony index ≤ 3 (normalized ≤ 0.6); `A ≤ 180 s`, `Rel ≤ 60 s`;
Freeze stored Off; no point for ID 4 or 5; output saturation only via its MB base; description (the `Comment`) free
of `" & < >`; ≥ 8 IDs displaced ≥ 0.10 from the default and from every other preset (C-7.1); secondaries per §7 as
corrected by T043. Names and categories may move if the harness or listening says so; primaries do not. After each
authoring task: build + run `generate_vorago_presets`, then the per-push filter (green except LibraryShape until T047).

## Group 38 — S rows (shared defs header)

### T044 — §7 rows 1–4, 6, 7, 9, 10 (S1–S4, S6, S7, S9, S10 primaries)

- 1 "Wind Through Basalt" (S1; D3.1 Direct, D3.2 FilteredWind, D4.1–D4.3 over three Direct slots + one FilteredWind
  slot; Textures) · 2 "Resonant Shaft" (S2; D5.2 Keyed; Caverns) · 3 "Smeared Horizon" (S3; D8.1, D10.2; Textures) ·
  4 "Feedback Mire" (S4; D6.1/D6.2/D6.3, at least one loop each; Machines) · 6 "Slow Bloom" (S6; D14.1 breathing
  ≥ 0.7, D9.2 A ≥ 90 s; Drones) · 7 "Colony Pulse" (S7; D13.2 rate scale ≥ 3.0; Organisms) · 9 "Choir of Absence"
  (S9; D11 reverse ≥ 0.5, D12.1 triggers On — additive per FR-061; Ghosts) · 10 "Hull Resonance" (S10; D2 blend in
  [0.35, 0.65], D1.StoneChamber, D1.SteelTank; Machines).
- **Verify:** per-push green except LibraryShape; `ClaimsWellFormed` and `ParameterSpaceDistinct` pass.

## Group 39 — M rows (shared defs header)

### T045 — §7 rows 11–22 (M1–M12 primaries)

- Each M row stores its macro displaced ≥ 0.5 from default (Gravity `|g − 0.5| ≥ 0.35`): 11 "Lightless" (M1; D12.2;
  Abyss) · 12 "Erosion" (M2; D3.3 GranularDust, D4.4–D4.6; Textures) · 13 "Crowded Dark" (M3; Drones) · 14 "Drifting
  Strata" (M4; D14.2 tidal ≥ 0.7; Drones) · 15 "Stone Gravity" (M5; D5.3 Hybrid; Abyss) · 16 "Entropic Hum" (M6;
  Machines) · 17 "Pressure Front" (M7; Machines) · 18 "Weighted Deep" (M8; D7.2 f/4 strictly loudest; Abyss) · 19
  "Fogbound" (M9; D4.10–D4.12, verifies S1 as a secondary; Ghosts) · 20 "Teeming" (M10; D13.1 rate scale ≤ 0.3;
  Organisms) · 21 "Endless Descent" (M11; Caverns) · 22 "Monolith" (M12; D7.1 f/2; Drones).
- **Verify:** as T044.

## Group 40 — E rows (shared defs header)

### T046 — §7 rows 23–27 (E1–E5 primaries; rows 28–29 exist from T037)

- 23 "Bloom Colony" (E1; Organisms) · 24 "Singing Colony" (E2; D5.1 Free; Organisms) · 25 "Swarm Breath" (E3; D3.4
  MetallicHiss, D4.7–D4.9; Textures) · 26 "Feeding Loops" (E4; Machines) · 27 "Haunted Colony" (E5; Ghosts). Each:
  Ecosystem Depth (900) high, its destination section present, the other four quiet enough that the route-isolated
  measurement attributes the change to route *k* (C-2.1 Group E; a macro-carried route fails).
- **Verify:** as T044.

## Group 41 — D rows (shared defs header)

### T047 — §7 rows 31–38 and 40 (D1 materials, D9.1; rows 30 and 39 exist from T037)

- 31 "Strung Abyss" (D1.Strings; Drones) · 32 "Iron Plate" (D1.MetalPlate; Machines) · 33 "Chamber Drone"
  (D1.Chamber; Drones) · 34 "Ice Shelf" (D1.Ice; Textures) · 35 "Hull Ark" (D1.WoodenHull; Drones) · 36 "Column Hymn"
  (D1.CathedralColumn; Caverns) · 37 "Cavern Wall" (D1.CavernWall; Caverns) · 38 "Glass Sphere" (D1.GlassSphere;
  Ghosts) · 40 "Sudden Chasm" (D9.1 A ≤ 10 s; Abyss). Each D1 row: the material at blend weight ≥ 0.35 on its side,
  body audible (S10 verifies).
- Remove `plugins/vorago/resources/presets/Drones/.gitkeep` (Drones now holds presets).
- **Verify:** per-push filter → **all green including `Vorago_FactoryPresets_LibraryShape`** (N == 42, every
  required primary present once, every category ≥ 3; §7 counts Textures 5, Caverns 6, Machines 6, Abyss 5, Drones
  7, Organisms 7, Ghosts 4).

## Group 42 — full local sweep loop

### T048 — Full sweep, matrix, distinctness; re-author loop; pin the tree tolerance (FR-011…FR-017, FR-032…FR-038)

**Main-loop note (2026-09-30):** sweep 1 (`f:/tmp/p14/shard_{0..3}.log`, `aggregate.log`, 2 h 38 min + 8 min)
verified 17 of 42 primaries; three findings were ruled (spec Clarifications "Sweep rulings"): S-1 noise-bus
make-up gain (`kNoiseBusMakeupDb` 30 dB in `vorago_voice.h`, FR-077 / SC-033), S-2 pair floor
max(F, 2·t_max), S-3 twins scored on M1…M3. Re-authored before sweep 2: Stone Gravity (over the limiter
ceiling) and Slow Bloom (no bloom inside the window). Sweep 2 runs the same four-shard protocol on the amended
tree; the re-author loop continues from its results.

**Main-loop note (2026-09-30, sweep 2):** sweep 2 (`artifacts/sweep2_*.log`, 4 h 38 min + 13 min) verified
14 of 42 primaries and left 99 pairs under the floor. Three rulings (spec Clarifications "Sweep 2 rulings"):
S-4 D3 primary rule (state ∧ S1 ablation d ≥ F; harness pass-3 copy, `applyD3PrimaryRule` in the pilot),
S-5 make-up stays +30 dB, S-6 reset defect fixed in `NoiseGenerator::snapLevelSmoothers()` /
`NoiseOrganism::applySlotConfiguration` (FR-077a / SC-033a). The pilot probe gained `VORAGO_PILOT_OVERRIDE`
(a re-author candidate measured in ~2 min without a rebuild). The re-author loop continues from sweep 2's
data; sweep 3 follows.

**Main-loop note (2026-09-30, re-author rulings S-7..S-10):** S-7 ghost-tap make-up measured before ruling (engine `setGhostTapMakeupDb`); S-8 ecosystem- and bloom-limited primaries (E1, E3, E4, M10, S6; S7 still authoring) recorded UNMET at their measured ceilings, N stays 42; S-9 the attack window is the audible attack (`audibleAttackSeconds`, plan 6.8 amended); S-10 one more probe round, then regenerate and sweep 3.
S-7 ruled 17:25: the ghost-tap make-up ships at +12 dB (FR-077b / SC-033b); S9 / E5 UNMET at 1.83 / 1.19. Presets regenerated; final-tree chain then sweep 3.

- **Run** (alone, logged): four shard processes `VORAGO_SWEEP_SHARD=i/4 VORAGO_SWEEP_OUT=f:/tmp/p14/sweep-out
  VORAGO_SWEEP_THREADS=1 vorago_tests.exe "[vorago-sweep]~[vorago-aggregate]" -d yes > f:/tmp/p14/shard_i.log 2>&1`
  (i = 0…3), then `VORAGO_SWEEP_IN=f:/tmp/p14/sweep-out vorago_tests.exe "[vorago-aggregate]" -d yes >
  f:/tmp/p14/aggregate.log 2>&1`. Estimate the wall clock before starting from T039's measured pilot times and §10;
  record the measured durations.
- **Loop:** a claimed cell that fails, a failed arm or a SUBSET pair → re-author that preset in
  `tools/vorago_preset_defs.h` (stronger contrast; never a relaxed predicate), regenerate, re-run its shard
  (`defIndex mod 4`) and the aggregate. A cell no authorable preset verifies, a failed control, or a boundedness
  failure re-authoring cannot fix → **FR-017 STOP** with measurements.
- **Tree tolerance (§6.15):** `Vorago_FactoryPresets_TreeToleranceProbe` on MSVC and WSL/GCC; AppleClang through one
  `workflow_dispatch` run with a temporary step (needs a push — **ask the user**; remove the step afterwards); set
  `kTreeFloatRelTol = max(10 × worst over the three, 1.19e-7)` in `factory_preset_test.cpp`; record the three worsts
  and the run URL.
- **Record** in `compliance.md`: the printed matrix, default-state cells, witnesses, min / median / max `d`, minimum
  pair, `t_max`, K, floor, every control, every arm figure, durations.
- **Verify:** every `[vorago-sweep]` case passes locally; per-push green.

## Group 43 — preset CPU (CPU TU)

### T049 — `Vorago_PresetCpu` (FR-041, SC-017, §6.17)

- **Test** `TEST_CASE("Vorago_PresetCpu", "[.perf][vorago]")` in `integration/preset_cpu_test.cpp`: per preset and
  the default surface — host 48 kHz / 512, `setState`, `kPolyphonyId → 0.6` (index 3, 4 voices), NoteOn all four
  `kCpuNotes` at t = 0, untimed pre-roll to the patch's own `A + 5 s`; 16 trials × 100 blocks,
  `std::chrono::steady_clock`, interleaving one preset block and one default block (the default host re-pre-rolled
  whenever its next block would leave its own `Sus`); **gate** worst `min-trial(preset) / min-trial(default) ≤ 1.15`;
  printed, not gated: the absolute figure vs `kReferenceNs` (3 200 000 ns, through `VORAGO_PERF_BUDGET_HEADER`) and
  the stored-polyphony figure. No fast-math exemption for this TU (T003).
- **Run:** `node tools/run-cpu-tests.js vorago_tests`, alone, machine idle. A breach → re-run once alone after
  idling; a repeat breach → re-author the preset; never relax (FR-041).
- **Record** the per-preset table in `compliance.md`.

## Group 44 — listening checkpoint (STOP)

### T050 — FR-042 / SC-020: the user auditions every preset

- Prepare in `compliance.md` a table (preset, category, primary, "character note", "category fit", "reads as a
  variant of"). **STOP** and ask the user to audition the 42 presets (installed by the `Vorago` build's POST_BUILD
  step to `%PROGRAMDATA%\Krate Audio\Vorago\`). Nothing automated substitutes. Re-file / re-author only on the
  user's notes, then re-run T048's affected shards and the aggregate.

---

## Stage G — deferred items and docs (FR-061, FR-062)

## Group 45 — docs page (new files)

### T051 — `plugins/vorago/docs/index.html` (+ `assets/`) (FR-062)

- **Create** on the structure of `plugins/seraphis/docs/index.html` and its `assets/`: what Vorago is; the twelve
  concept macros; the ecosystem view and the two rule knobs (Ecosystem Sync, Self Affinity); the seven categories,
  one line each; the freeze-gesture instruction; system requirements; links (`https://krateaudio.com/vorago/`,
  `plugins/vorago/version.json`). Remove `plugins/vorago/docs/.gitkeep` if tracked. `docs.yml` needs no edit (C-11).
- **Verify:** every `src`/`href` resolves inside `docs/`.

## Group 46 — plugin CLAUDE.md (existing file)

### T052 — `plugins/vorago/CLAUDE.md` (FR-064 note, §5.2)

- Ecosystem band of the ID table (900–902); "110 registered IDs, 108 persisted"; route totals (VP 33); the state table
  (a v3 row, 436 bytes, v2/v1 strict prefixes); the page-6 row (four knobs); the seven fixed categories; the 1.0.0
  controller-interface freeze (decision 1); the generator and `generate_vorago_presets`; the sweep lane and
  `[vorago-sweep]`; FR-061's ruling (additive ghost triggers) cited once.
- **Verify:** every number matches `plugin_ids.h` / `param_routes.h` after T015/T017.

---

## Stage H — tooling and CI (FR-024, FR-063, FR-066; SC-007, SC-013, SC-021, SC-023)

## Group 47 — determinism script (existing file)

### T053 — `tools/check-preset-generator-determinism.js --plugin <name>` (FR-024, SC-007)

- **Test first:** `node tools/check-preset-generator-determinism.js --plugin vorago` fails today.
- **Implement:** `--plugin`, default `seraphis`; a table `{seraphis: 'seraphis_preset_generator', vorago:
  'vorago_preset_generator'}` driving the default binaries (`:53-55`), the not-found message (`:111-113`) and the
  temp prefix (`:209`, `${plugin}-presets-`); USAGE updated; the no-flag path byte-for-byte unchanged.
- **Verify:** `--plugin vorago` exit 0 (two runs byte-identical; re-run over the committed tree changes 0 bytes); the
  no-flag Seraphis run exit 0.

## Group 48 — release rosters (existing files)

### T054 — `release-readiness.js` and the release skill (FR-063, SC-021)

- `.claude/workflows/release-readiness.js` `PLUGIN_MAP` (`:14-22`) gains `vorago: { testTarget: 'vorago_tests',
  bundle: 'Vorago.vst3' }`; `.claude/skills/release/SKILL.md` gains vorago in its plugin list and target/bundle table.
- **Verify:** `grep -n vorago` in both shows the entries; `node tools/lint-plugin-roster.js` exit 0.

## Group 49 — nightly filter in ci.yml (existing workflow)

### T055 — `ci.yml` nightly filters → `[long]~[vorago-sweep]` (FR-066, SC-023)

- The three `FILTER='[long]'` lines (`.github/workflows/ci.yml:369`, `:655`, `:1116`) → `FILTER='[long]~[vorago-sweep]'`.
  The per-push lines (`:374`, `:660`, `:1121`) and everything else stay.
- **Verify:** `grep -n "FILTER=" .github/workflows/ci.yml` shows exactly those three changed; `actionlint` if on PATH.

## Group 50 — nightly sweep jobs (existing workflow)

### T056 — `long-tests-nightly.yml`: `vorago-sweep` + `vorago-sweep-aggregate` (FR-066, §5.10)

- The §5.10 jobs: both gated on `check-activity` like `long-tests` (aggregate `needs: [check-activity,
  vorago-sweep]`, `if: ${{ !cancelled() && needs.check-activity.outputs.should_run == 'true' }}`); matrix `os:
  [windows-2022, macos-latest, ubuntu-latest]`, sweep `shard: [0, 1, …, 9]` (n = 10, §10); `fail-fast: false`;
  `timeout-minutes: 180`; checkout; configure with the leg's `ci.yml` configure flags minus AU/ccache options;
  build `--target vorago_tests` only; run `VORAGO_SWEEP_SHARD=${{ matrix.shard }}/10 VORAGO_SWEEP_OUT=sweep-out
  <bin>/vorago_tests "[vorago-sweep]~[vorago-aggregate]" -d yes`; upload `vorago-sweep-${{ matrix.os }}-${{
  matrix.shard }}`; the aggregate downloads `vorago-sweep-${{ matrix.os }}-*` (merge-multiple) and runs
  `VORAGO_SWEEP_IN=sweep-in <bin>/vorago_tests "[vorago-aggregate]" -d yes`.
- **Verify:** YAML well-formed (`actionlint` if on PATH); job names unique; every `timeout-minutes ≤ 180`.

## Group 51 — CI measurements (needs pushes; STOP for permission)

### T057 — SC-013 / SC-023 on the runners

- **Ask the user** for permission to push and to dispatch `long-tests-nightly.yml`. Without it SC-013 and SC-023 are
  recorded as pending — never estimated.
- **With it:** from the logs, per OS: each sweep job's step wall clock vs its `timeout-minutes` (≤ 60 %), the
  aggregate's, the macOS queue time (recorded, not gated, P2-7), and the per-push "Run Tests" step total (≤ 80 % of
  20 min) with the added Vorago preset TUs' time. Adjust n from measurement (more shards first, threads second;
  never fewer presets/arms/windows) and record the ruling. A pre-existing per-push overrun is an FR-017 finding, not
  a trim. Cite the run URLs.

---

## Stage I — release bookkeeping (FR-026, FR-027, FR-064; SC-019, SC-024…SC-026, SC-032)

## Group 52 — version (existing files)

### T058 — `version.json` 1.0.0 + `CHANGELOG.md` `[1.0.0]` (FR-064)

- `plugins/vorago/version.json` `"version": "0.2.0"` → `"1.0.0"`; `plugins/vorago/CHANGELOG.md` gains `## [1.0.0] -
  <date>` above `[0.2.0]` describing the factory library (42 presets, seven categories, the two ecosystem rule knobs
  and state v3, the tail-length report, the freeze-gesture note) in the file's existing style. Only these two files
  carry the version (release skill).
- **Verify:** `node tools/check-changelog-coverage.js` passes.

## Group 53 — compliance evidence (no code)

### T059 — Install path, freeze load path, `dsp/` bound, roster evidence (FR-026, FR-027, FR-061, FR-070, FR-076; SC-024…SC-026, SC-032)

- **SC-032:** build `Vorago`; list `%PROGRAMDATA%\Krate Audio\Vorago\` (seven dirs, 40 files); cite
  `plugins/vorago/CMakeLists.txt:123`, `plugins/shared/src/platform/preset_paths.h:27`,
  `plugins/vorago/installers/windows/setup.iss:66-68`, `plugins/vorago/installers/linux/README.txt:29-44`.
- **SC-024:** `git diff 339cd501..HEAD -- dsp/include/krate/dsp/effects/cavern_verb.h
  dsp/include/krate/dsp/effects/aether_reverb.h plugins/vorago/src/parameters/space_params.h` is empty; the full
  `git diff 339cd501..HEAD -- plugins/vorago/src/processor/processor.cpp` with every hunk attributed (v3
  `setState`/`getState` → FR-072; `pushVoiceParams` → FR-071a; `getTailSamples` → FR-060); no hunk in
  `pushCavernParams` or the `loadSpaceParams` call.
- **FR-076:** `git diff --stat 339cd501..HEAD -- dsp/include` lists exactly `systems/vorago_voice.h` and
  `systems/vorago_engine.h`; the diff has added lines only, except the `kFieldCount` value line and its comment.
- **SC-025 / SC-026:** cite both 13b tables' rows for `syncRate` and `selfAffinity`
  (`specs/vorago-phase13b-ecosystem-audibility/artifacts/final2_table_default.log`, `final2_table_lifemax.log`) and
  the ratification date 2026-09-29 preceding the first commit adding 901/902 (`git log` order).
- **FR-061:** cite Clarification Q8 and `Vorago_Ghost_TriggersAddToDensityScheduler`'s passing log line on the S9
  preset's row.

---

## Final group — integration (sequential)

### T060 — CMake registration audit (FR-027a; adds nothing)

- `plugins/vorago/tests/CMakeLists.txt` lists the probe TU (`:52`) plus exactly the ten T003 TUs under the Phase 14
  comment, each present on disk; `git ls-files plugins/vorago/tests` holds no unregistered Phase 14 `.cpp`; the
  fast-math list holds the probe + the eight T003 entries and **not** `preset_cpu_test.cpp` or
  `ghost_triggers_additive_test.cpp`; `${CMAKE_SOURCE_DIR}/tools` is on the include path; the root `CMakeLists.txt`
  holds `vorago_preset_generator` and `generate_vorago_presets`; `vorago_tests.exe --list-tests` (listing only)
  shows every TEST_CASE named in this file. A missing item is fixed in the task that owned it and recorded here.

### T061 — Full-suite run (clean build first)

- Build `dsp_systems_tests`, `vorago_tests`, `vorago_preset_generator`, `Vorago` — zero warnings.
- Per-push filters of `dsp_systems_tests` and `vorago_tests` → all pass (tail lines recorded);
  `node tools/check-seraphis-green.js` → green.
- `[long]` lane, locally, alone, logged: `vorago_tests.exe "[long]~[vorago-sweep]"` and `dsp_systems_tests.exe
  "[long]"` → pass. The `[vorago-sweep]` lane is T048's (re-run only the shards whose presets changed since).
- `node tools/check-preset-generator-determinism.js --plugin vorago` → exit 0.
- `tools/pluginval.exe --strictness-level 5 --validate "build/windows-x64-release/VST3/Release/Vorago.vst3"` → exit 0.
- `./tools/run-clang-tidy.ps1 -Target vorago -BuildDir build/windows-ninja` and `-Target dsp` → 0 findings.

### T062 — Portability check

- `node tools/check-portability.js` → clean (covers `lint-float-bit-goldens.js` and `lint-nonfinite-symbols.js`);
  then `wsl --shutdown`.

### T063 — Release gate verdict (FR-065, SC-019)

- Run the `release-readiness` flow for `vorago` (build, `vorago_tests`, pluginval 5, version/CHANGELOG sync) and
  record its row. auval's evidence source is `ci.yml`'s "Run Vorago AU Validation" step on the release commit, which
  needs a push (**ask the user**); until then SC-019's auval clause is recorded as pending, never assumed.
- Write the FR / SC-001…SC-032 compliance table in `compliance.md`, every row citing a file:line, a test name and its
  actual log line (root `CLAUDE.md` "Completion Honesty"); green/red verdict.

**PAUSED at T048 (2026-09-30 22:30, second pause; ruling in the main chat):** sweep 3 verified 28 of 42
primaries with 46 pairs under the floor, and the re-author loop measured the remaining cells as inaudible at any
preset setting (compliance "Re-author loop after sweep 2", "Sweep 3"). The user ruled that no feature ships
inaudible and no preset ships generic: rulings S-8's "record UNMET at the ceiling" is WITHDRAWN as a release
outcome — it stands only as the measurement record. **Phase 13c (capability audibility,
`vorago-phase13c-capability-audibility`, roadmap entry 2026-09-30)** fixes the features first; this phase
resumes at T048 on the new levers with sweep 4 as the confirming run. Sweep 4 on the sweep-3 repairs (level-arm
defects, quiet noise types moved) runs overnight 2026-09-30/10-01 as the record of the current tree. T049 (the
pinned CPU lane) ran once after sweep 3 (`artifacts/t049_cpu_arm.log`) and is re-run on the final tree.

**Resume items handed over by Phase 13c (2026-10-01/02, its `artifacts/rulings.md`):**
- Haunted Colony: choose a stored seed on which E5 clears at the ruled ghost levers (`kGhostTapMakeupDb` 21,
  `kGhostDensitySpan` 1.2): seed index 13 read d 6.04 and index 15 read 4.62; the shipped index 12 reads 3.80
  (13c `l3_span_1.2_E5_takes4.log`).
- Choir of Absence: a level trim — its stored take sits on the limiter at the 21 dB make-up (hi −5.99 dB); the
  −6 dB re-read is green at d 4.04–4.25 (13c `l3_ghost_21_S9_minus6.log`, `l3_density_1.5_S9_minus6.log`).
- Resonant Shaft: master gain 0.2 (sweep 4 take 1 / seed 4 read hi −5.59 dB at 0.25).
- Stone Gravity: re-author after 13c's Gravity (M5) fix.
- T049's CPU arm was measured under a runaway process (13c `t006_cpu_resolution.md`): re-run on a quiet box
  with VS Code's WSL session, Docker and the browser closed; the box read ~15 % slower than in late September.
- Feedback Mire: breathing depth 0 in `tools/vorago_preset_defs.h` (13c ruling B-12, 2026-10-03); its preset file was regenerated in 13c. Every preset inherits the default breathing depth 0.30 under the breath gravity lane gain 3.0, so check each preset's breathing depth against its character at the re-author.
- Drifting Strata: re-author (13c ruling B-13, 2026-10-03). Its M4 Movement primary reads 0.91 on the 13c tree (3.00 before): the ghost make-up and child gain mask the Movement contrast (3.07 at ghost 12 dB, 2.29 at child gain 0.35) and its Movement-to-breathing-depth row swings the gravity lane three times harder under the breath gain 3.0. Its D14.2 tidal cell cannot clear by any lane gain or tide rate (cavern fog at space mix 0.2 is the ceiling; 0.19 even at space mix 1.0). Consider breathing depth 0, a higher space mix, and whether D14.2 stays a claim.
- Default ghost peak is now 0 (13c ruling B-14, 2026-10-04: `VoragoEngine::kGhostDefaultPeakLevel`, plugin default `ghost_params.h`). Six presets that inherited the old 0.60 now pin `kGhostPeakLevelId` 0.60 explicitly (Tectonic Floor, Drifting Strata, Entropic Hum, Teeming, Endless Descent, Steam Vent) so their renders did not move; revisit those six at the re-author (0.60 under the 21 dB make-up is far above where the other 36 sit).
- Re-author against the 13c engine (13c ruling B-15, 2026-10-05): Resonant Shaft (S2 2.69), Lightless (M1 1.78), Singing Colony (E2 3.79) and Spore Drift (D3.3 1.81) lost their sweep-4 verification to the child gain 1.5 / breath gain 3.0 / ghost 21 dB lifts; per-preset breathing, bloom and ghost overrides did not carry them (`specs/vorago-phase13c-capability-audibility/artifacts/f2_preset_fix_summary.txt`). Wind Through Basalt (breathing 0) and Smeared Horizon (bloom off) were fixed in 13c and regenerated.
- `FeedbackEcology_CpuBudget` (Phase 5 SC-004 (a)) is red on this machine on both the 13b base and the 13c final binaries, alone and pinned (13c ruling B-17, 2026-10-05; `specs/vorago-phase13c-capability-audibility/artifacts/cpu_ab_rerun_summary.txt`: final nsRef 111763.6, x1.5 = 167645.4 vs 160000). Levers 1-3 are in the tree; levers 4 (resonator -> SVF bandpass) and 5 (numLoops 6 -> 5) need a user decision. Open it in this phase's CPU lane.
- **MUST (user ruling, 2026-10-05): shave hours off every phase close.** The 13c close measured (`specs/vorago-phase13c-capability-audibility/artifacts/final_g16_summary.txt`, `final_long_*.log`): eight per-push suites sequential ~45 min; the `dsp_systems_tests` `[long]` lane alone ~4 h (13b: 10:55 to 14:53); then the `vorago_tests` `[long]` lane, pluginval, clang-tidy and portability, all serial. Only the CPU lane needs isolation. Three changes, in payoff order: (1) a runner (`tools/run-close-lanes.js`, Node) that runs the non-timing suites and both `[long]` lanes concurrently across cores, one log per lane, timing cases still excluded and run alone afterwards; (2) a per-preset render cache for the pilot/sweep probes keyed on preset-def hash + tree hash, so F1/F2/A-B re-reads are reads, not renders; (3) the Phase 10 soaks stay nightly-only (CI already does this) and leave the phase-close gate. Each change needs an FR and a before/after wall-clock row in compliance.
- Route cells E3 (Swarm Breath 1.1652), E4 (Feeding Loops 2.9182) and E5 (Haunted Colony 3.9093) are surfaced by 13c ruling B-19 (2026-10-06) next to E1 (B-11): no measured lever reached 4.0 (`specs/vorago-phase13c-capability-audibility/artifacts/{l2_ladder_summary.txt,l3_span_ladder_summary.txt,final_E{3,4,5}.log}`). Re-author or re-seed at T048; E5 is 0.09 under the bar.
- Five sweep-4 secondaries read not verified on the 13c tree (ruling B-20, 2026-10-06; `final_secondaries_030b.log`): E6.hi Colony Pulse 1.3700 (now rendered because Gate 2 counts syncRate again), Spore Drift D3.3 1.4396, Erosion D4.4-D4.6 and Fogbound D4.8 / D4.10-D4.12 (their S1 conjunct copies read 1.44 vs 1.5), Cathedral Void D10.1 (pilot conjunct FAIL; the sweep aggregate still marks it S). Levers stay; re-verify at the re-author.
- Choir of Absence: sweep arm 1 on take 3 (seed 13) reads hi -5.58 dB against the -6.0 bar on the 13c tree (ruling B-18, 2026-10-06; sweep 4 read -7.85). Measured and declined in 13c: master 0.42 (-1.5 dB) -> -5.66 (the limiter sets the hi RMS); ghost peak 0.8 / 0.7 / 0.6 -> S9 3.87 / 3.57 / 3.21 FAIL. Master -6 dB passes both (S9 4.4586, take 3 -6.16) with 0.16 dB margin. The def is unchanged; re-level at the re-author (`specs/vorago-phase13c-capability-audibility/artifacts/b18_*.log`).
- `Vorago_PresetPilot_Calibrate` on the 13c tree reads `ruled K = NONE` / `G2: STOP` (`specs/vorago-phase13c-capability-audibility/artifacts/default_after_p0.log:21,24`): Glass Well (P4) t_K 3.0451 / 2.3836 / 3.8071 / 2.4264 at K = 1 / 2 / 4 / 8 against 2*t_K <= 4.0, where sweep 4 read 1.9786 / 2.7190 / 1.4267 / 1.0502 and ruled K = 4 (`default_before_p0.log:21`). The P0 default surface itself tightened (t_1 5.9161 -> 1.1895). Re-run the calibration at T048 before the confirming sweep; K = 4 is the sweep contract in force.
